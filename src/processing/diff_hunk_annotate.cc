#include "diff_hunk_annotate.hpp"

#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "processing/tokenizer.hpp"
#include "util/ordered_task_queue.hpp"
#include "util/thread_pool.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <memory>

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
        return token.length == other.token.length &&
            token.hash == other.token.hash;
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

AnnotatedHunk
annotate_tokens_for_hunk(const DiffInput<diffy::Line>& diff_input,
                         const Hunk& hunk,
                         bool ignore_whitespace) {
    std::vector<TokenEdit> a;
    std::vector<TokenEdit> b;

    AnnotatedHunk ahunk;
    ahunk.from_start = hunk.from_start;
    ahunk.from_count = hunk.from_count;
    ahunk.to_start = hunk.to_start;
    ahunk.to_count = hunk.to_count;

    ahunk.a_lines.resize(static_cast<size_t>(hunk.from_count));
    ahunk.b_lines.resize(static_cast<size_t>(hunk.to_count));

    if (hunk.edit_units.empty()) {
        return ahunk;
    }

    std::size_t context_head = 0;
    std::size_t context_tail = hunk.edit_units.size() - 1;
    bool found_change = false;
    for (std::size_t i = 0; i < hunk.edit_units.size(); ++i) {
        if (hunk.edit_units[i].type != EditType::Common) {
            context_head = i;
            found_change = true;
            break;
        }
    }
    if (found_change) {
        for (std::size_t i = hunk.edit_units.size(); i-- > 0;) {
            if (hunk.edit_units[i].type != EditType::Common) {
                context_tail = i;
                break;
            }
        }
    } else {
        context_tail = context_head;
    }

    std::size_t a_hunk_line_index = 0;
    std::size_t b_hunk_line_index = 0;
    for (std::size_t edit_iter = 0; edit_iter < hunk.edit_units.size(); ++edit_iter) {
        const auto& edit = hunk.edit_units[edit_iter];
        if (edit.a_index.valid) {
            const auto& input_line = diff_input.A[static_cast<long>(edit.a_index)].line;
            const auto tokens = tokenize(input_line);
            if (edit_iter >= context_head && edit_iter <= context_tail) {
                for (const auto& token : tokens) {
                    a.push_back({a_hunk_line_index, token, input_line});
                }
            } else {
                for (const auto& token : tokens) {
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
            const auto tokens = tokenize(input_line);
            if (edit_iter >= context_head && edit_iter <= context_tail) {
                for (const auto& token : tokens) {
                    b.push_back({b_hunk_line_index, token, input_line});
                }
            } else {
                for (const auto& token : tokens) {
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
    Patience<TokenEdit> diff_context(hunk_input);
    auto result = diff_context.compute();
    if (result.status != diffy::DiffResultStatus::OK) {
        assert(!"bad diff");
    }

    return annotate_tokens_in_hunk(hunk_input, result, ahunk, ignore_whitespace);
}

AnnotatedHunk
annotate_lines_for_hunk(const DiffInput<diffy::Line>& diff_input,
                        const Hunk& hunk,
                        bool ignore_whitespace) {
    AnnotatedHunk ahunk;
    ahunk.from_start = hunk.from_start;
    ahunk.from_count = hunk.from_count;
    ahunk.to_start = hunk.to_start;
    ahunk.to_count = hunk.to_count;

    for (const auto& edit : hunk.edit_units) {
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

    return ahunk;
}

}  // namespace

AnnotatedHunk
diffy::annotate_hunk(const DiffInput<diffy::Line>& diff_input,
                     const Hunk& hunk,
                     EditGranularity granularity,
                     bool ignore_whitespace) {
    switch (granularity) {
        case EditGranularity::Line:
            return annotate_lines_for_hunk(diff_input, hunk, ignore_whitespace);
        case EditGranularity::Token:
            return annotate_tokens_for_hunk(diff_input, hunk, ignore_whitespace);
        default:
            break;
    }
    return {};
}

std::vector<AnnotatedHunk>
diffy::annotate_hunks(const DiffInput<diffy::Line>& diff_input,
                      const std::vector<Hunk>& hunks,
                      EditGranularity granularity,
                      bool ignore_whitespace) {
    std::vector<AnnotatedHunk> output(hunks.size());
    if (hunks.empty()) {
        return output;
    }

    auto& pool = global_thread_pool();
    const std::size_t capacity = std::max<std::size_t>(1, pool.thread_count() * 2);
    auto queue = std::make_shared<OrderedTaskQueue<AnnotatedHunk>>(capacity);

    for (std::size_t idx = 0; idx < hunks.size(); ++idx) {
        pool.enqueue([queue, &diff_input, &hunks, granularity, ignore_whitespace, idx] {
            try {
                auto annotated = annotate_hunk(diff_input, hunks[idx], granularity, ignore_whitespace);
                queue->push(idx, std::move(annotated));
            } catch (...) {
                queue->push_exception(idx, std::current_exception());
            }
        });
    }

    for (std::size_t idx = 0; idx < hunks.size(); ++idx) {
        output[idx] = queue->pop(idx);
    }

    return output;
}
