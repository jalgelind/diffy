#include "diff_hunk_annotate.hpp"

#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "doctest.h"
#include "processing/tokenizer.hpp"

#include <fmt/format.h>

using namespace diffy;

namespace {

struct TokenEdit {
    std::size_t hunk_line_index;
    Token token;
    const std::string& line_ref;

    // for std::map
    bool
    operator<(const TokenEdit& other) const {
        return token.hash < other.token.hash;
    }

    bool
    operator==(const TokenEdit& other) const {
        // guard against collisions in debug..?
        // TODO: also check token.length?
        return token.hash == other.token.hash;
    }

    uint32_t
    hash() const {
        return token.hash;
    }

    operator std::string() const {
        assert(token.start + token.length <= line_ref.size());
        return line_ref.substr(token.start, token.length);
    }
};

EditType
resolve_edit_type_from_indices(EditIndex idx_a, EditIndex idx_b) {
    if (idx_a.valid && !idx_b.valid) {
        return EditType::Delete;
    } else if (!idx_a.valid && idx_b.valid) {
        return EditType::Insert;
    }
    return EditType::Common;
}

AnnotatedHunk
annotate_tokens_in_hunk(const DiffInput<TokenEdit>& diff_input,
                        const DiffResult& result,
                        AnnotatedHunk& ahunk,
                        bool ignore_whitespace) {
    ;
    for (std::vector<Edit>::size_type j = 0; j < result.edit_sequence.size(); j++) {
        auto& token_edit = result.edit_sequence[j];
        auto a_idx = token_edit.a_index;
        auto b_idx = token_edit.b_index;
        auto token_edit_type = resolve_edit_type_from_indices(a_idx, b_idx);

        if (a_idx.valid) {
            const auto& iedit = diff_input.A[static_cast<long>(a_idx)];
            const auto& token = iedit.token;
            if (ignore_whitespace && (token.flags & (TokenFlagSpace | TokenFlagTab))) {
                token_edit_type = EditType::Common;
            }
            ahunk.a_lines[iedit.hunk_line_index].segments.push_back({
                token.start,
                token.length,
                token.flags,
                token_edit_type,
            });
        }

        if (b_idx.valid) {
            const auto& iedit = diff_input.B[static_cast<long>(b_idx)];
            const auto& token = iedit.token;
            if (ignore_whitespace && (token.flags & (TokenFlagSpace | TokenFlagTab))) {
                token_edit_type = EditType::Common;
            }
            ahunk.b_lines[iedit.hunk_line_index].segments.push_back({
                token.start,
                token.length,
                token.flags,
                token_edit_type,
            });
        }
    }

    return ahunk;
}

std::vector<AnnotatedHunk>
annotate_tokens(const DiffInput<diffy::Line>& diff_input,
                const std::vector<Hunk>& hunks,
                bool ignore_whitespace) {
    // TODO: This could be threaded.
    std::vector<AnnotatedHunk> output;
    std::vector<TokenEdit> a;
    std::vector<TokenEdit> b;

    for (auto& hunk : hunks) {
        a.clear();
        b.clear();

        AnnotatedHunk ahunk;
        ahunk.from_start = hunk.from_start;
        ahunk.from_count = hunk.from_count;
        ahunk.to_start = hunk.to_start;
        ahunk.to_count = hunk.to_count;

        ahunk.a_lines.resize(static_cast<size_t>(hunk.from_count));
        ahunk.b_lines.resize(static_cast<size_t>(hunk.to_count));

        // Figure out how many context lines there are at the beginning and
        // end of the hunk.
        std::vector<Edit>::size_type context_head = 0;
        for (auto i = 0U; i < hunk.edit_units.size(); i++) {
            if (hunk.edit_units[i].type != EditType::Common) {
                context_head = i;
                break;
            }
        }

        std::vector<Edit>::size_type context_tail = 0;
        for (auto i = hunk.edit_units.size() - 1; i > 0; i--) {
            if (hunk.edit_units[i].type != EditType::Common) {
                context_tail = i;
                break;
            }
        }

        // Run diff on all tokens in the hunk, except for the context lines.
        std::vector<Edit>::size_type edit_iter = 0;
        std::size_t a_hunk_line_index = 0;
        std::size_t b_hunk_line_index = 0;
        // TODO: We could deduplicate some code here
        for (; edit_iter < hunk.edit_units.size(); edit_iter++) {
            const auto& edit = hunk.edit_units[edit_iter];
            if (edit.a_index.valid) {
                const auto& input_line = diff_input.A[static_cast<long>(edit.a_index)].line;

                if (edit_iter >= context_head && edit_iter <= context_tail) {
                    for (const auto& token : tokenize(input_line)) {
                        a.push_back({a_hunk_line_index, token, input_line});
                    }
                } else {
                    for (const auto& token : tokenize(input_line)) {
                        ahunk.a_lines[a_hunk_line_index].segments.push_back(
                            {token.start, token.length, token.flags, edit.type});
                    }
                }

                ahunk.a_lines[a_hunk_line_index].type = edit.type;
                ahunk.a_lines[a_hunk_line_index].line_index = edit.a_index;
                a_hunk_line_index++;
            }

            if (edit.b_index.valid) {
                const auto& input_line = diff_input.B[static_cast<long>(edit.b_index)].line;
                if (edit_iter >= context_head && edit_iter <= context_tail) {
                    for (const auto& token : tokenize(input_line)) {
                        b.push_back({b_hunk_line_index, token, input_line});
                    }
                } else {
                    for (const auto& token : tokenize(input_line)) {
                        ahunk.b_lines[b_hunk_line_index].segments.push_back(
                            {token.start, token.length, token.flags, edit.type});
                    }
                }

                ahunk.b_lines[b_hunk_line_index].type = edit.type;
                ahunk.b_lines[b_hunk_line_index].line_index = edit.b_index;
                b_hunk_line_index++;
            }
        }

        DiffInput<TokenEdit> hunk_input{a, b, "left side", "right side"};
        // TODO: Re-use the same diff_context?
        Patience<TokenEdit> diff_context(hunk_input);
        auto result = diff_context.compute();
        if (result.status != diffy::DiffResultStatus::OK) {
            // TODO: report error.
            assert(0 && "bad diff");
        }

        output.push_back(annotate_tokens_in_hunk(hunk_input, result, ahunk, ignore_whitespace));
    }
    return output;
}

std::vector<AnnotatedHunk>
annotate_lines(const DiffInput<diffy::Line>& diff_input,
               const std::vector<Hunk>& hunks,
               bool ignore_whitespace) {
    std::vector<AnnotatedHunk> output;

    for (auto& hunk : hunks) {
        AnnotatedHunk ahunk;
        ahunk.from_start = hunk.from_start;
        ahunk.from_count = hunk.from_count;
        ahunk.to_start = hunk.to_start;
        ahunk.to_count = hunk.to_count;

        for (auto& edit : hunk.edit_units) {
            if (edit.a_index.valid) {
                const auto& a_line = diff_input.A[static_cast<long>(edit.a_index)].line;
                ahunk.a_lines.push_back({edit.type, edit.a_index, {}});
                for (const auto& token : tokenize(a_line)) {
                    auto edit_type = edit.type;
                    if (ignore_whitespace && (token.flags & (TokenFlagSpace | TokenFlagTab))) {
                        edit_type = EditType::Common;
                    }
                    ahunk.a_lines.back().segments.push_back(
                        {token.start, token.length, token.flags, edit_type});
                }
            }

            if (edit.b_index.valid) {
                const auto& b_line = diff_input.B[static_cast<long>(edit.b_index)].line;
                ahunk.b_lines.push_back({edit.type, edit.b_index, {}});
                for (const auto& token : tokenize(b_line)) {
                    auto edit_type = edit.type;
                    if (ignore_whitespace && (token.flags & (TokenFlagSpace | TokenFlagTab))) {
                        edit_type = EditType::Common;
                    }
                    ahunk.b_lines.back().segments.push_back(
                        {token.start, token.length, token.flags, edit_type});
                }
            }
        }
        output.push_back(ahunk);
    }
    return output;
}

}  // namespace

// TODO: Report failure?
std::vector<AnnotatedHunk>
diffy::annotate_hunks(const DiffInput<diffy::Line>& diff_input,
                      const std::vector<Hunk>& hunks,
                      EditGranularity granularity,
                      bool ignore_whitespace) {
    std::vector<AnnotatedHunk> hunks_annotated;
    switch (granularity) {
        case EditGranularity::Line:
            hunks_annotated = annotate_lines(diff_input, hunks, ignore_whitespace);
            break;
        case EditGranularity::Token:
            hunks_annotated = annotate_tokens(diff_input, hunks, ignore_whitespace);
            break;
        default:
            break;
    }
    return hunks_annotated;
}

#ifndef DOCTEST_CONFIG_DISABLE

// HACK: Without this, we'll get link errors on Darwin.
// See: https://github.com/onqtam/doctest/issues/126
#include <iostream>
#endif

TEST_CASE("diff_hunk_annotate") {
    SUBCASE("empty") {
    }
}