#include "diff_hunk_annotate.hpp"

#include "algorithms/myers_linear.hpp"
#include "algorithms/patience.hpp"
#include "processing/tokenizer.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <unordered_map>

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

// Length-weighted token overlap (Dice coefficient), keyed by token hash so that
// long shared identifiers count for more than the spaces/punctuation every line
// shares. Returns 0..1 (1.0 for two empty lines). Used to decide which delete
// line pairs with which insert line.
double
line_similarity(const std::vector<Token>& ta, const std::vector<Token>& tb) {
    std::unordered_map<uint32_t, int> a_count, b_count;  // hash -> occurrences
    std::unordered_map<uint32_t, uint32_t> len;          // hash -> token length
    uint32_t tot_a = 0, tot_b = 0;
    for (const auto& t : ta) {
        a_count[t.hash]++;
        len[t.hash] = static_cast<uint32_t>(t.length);
        tot_a += static_cast<uint32_t>(t.length);
    }
    for (const auto& t : tb) {
        b_count[t.hash]++;
        len[t.hash] = static_cast<uint32_t>(t.length);
        tot_b += static_cast<uint32_t>(t.length);
    }
    if (tot_a + tot_b == 0) {
        return 1.0;
    }
    uint32_t common = 0;
    for (const auto& [h, ca] : a_count) {
        if (auto it = b_count.find(h); it != b_count.end()) {
            common += static_cast<uint32_t>(std::min(ca, it->second)) * len[h];
        }
    }
    return (2.0 * common) / static_cast<double>(tot_a + tot_b);
}

// Every token of `line` marked `type`; for context/common lines and for changed
// lines with no similar counterpart (a pure add or delete).
void
whole_line_segments(const std::string& line, EditType type, EditLine& out, bool ignore_whitespace) {
    for (const auto& token : tokenize(line)) {
        auto et = type;
        if (ignore_whitespace && (token.flags & (TokenFlagSpace | TokenFlagTab))) {
            et = EditType::Common;
        }
        out.segments.push_back({token.start, token.length, token.flags, et});
    }
}

// Token-diff two single lines and append the intra-line segments to their
// EditLines: a-side tokens come out Common (shared) or Delete, b-side Common or
// Insert. Confining the diff to one pair keeps highlight islands from jumping
// between unrelated lines (the old whole-hunk "token soup" bug).
void
diff_line_pair(const std::string& la,
               const std::string& lb,
               EditLine& aline,
               EditLine& bline,
               bool ignore_whitespace) {
    std::vector<TokenEdit> a, b;
    for (const auto& tk : tokenize(la)) {
        a.push_back({0, tk, la});
    }
    for (const auto& tk : tokenize(lb)) {
        b.push_back({0, tk, lb});
    }
    DiffInput<TokenEdit> in{a, b, "l", "r"};
    Patience<TokenEdit> differ(in);
    auto res = differ.compute();
    if (res.status == diffy::DiffResultStatus::Failed) {
        // Token-hash collision etc.: mark the whole lines changed rather than blank.
        whole_line_segments(la, EditType::Delete, aline, ignore_whitespace);
        whole_line_segments(lb, EditType::Insert, bline, ignore_whitespace);
        return;
    }
    for (const auto& e : res.edit_sequence) {
        const auto type = resolve_edit_type_from_indices(e.a_index, e.b_index);
        if (e.a_index.valid) {
            const auto& tk = a[static_cast<long>(e.a_index)].token;  // Delete or Common
            auto et = type;
            if (ignore_whitespace && (tk.flags & (TokenFlagSpace | TokenFlagTab))) {
                et = EditType::Common;
            }
            aline.segments.push_back({tk.start, tk.length, tk.flags, et});
        }
        if (e.b_index.valid) {
            const auto& tk = b[static_cast<long>(e.b_index)].token;  // Insert or Common
            auto et = type;
            if (ignore_whitespace && (tk.flags & (TokenFlagSpace | TokenFlagTab))) {
                et = EditType::Common;
            }
            bline.segments.push_back({tk.start, tk.length, tk.flags, et});
        }
    }
}

// ALG-3: intra-line highlighting by pairing changed lines. Instead of diffing all
// of a hunk's changed tokens as one concatenated stream per side (which let a
// unique token anchor line 1 of A to line 7 of B), pair each deleted line with
// its most similar inserted line and token-diff within the pair; unpaired lines
// are whole-line adds/deletes.
std::vector<AnnotatedHunk>
annotate_tokens(const DiffInput<diffy::Line>& diff_input,
                const std::vector<Hunk>& hunks,
                bool ignore_whitespace) {
    // Cap the O(deletes × inserts) similarity search so a pathological hunk can't
    // blow up; above it, changed lines fall back to whole-line add/delete.
    constexpr size_t kPairBudget = 4096;
    constexpr double kPairThreshold = 0.30;  // below this, treat as unrelated add + delete

    std::vector<AnnotatedHunk> output;
    for (const auto& hunk : hunks) {
        AnnotatedHunk ahunk;
        ahunk.from_start = hunk.from_start;
        ahunk.from_count = hunk.from_count;
        ahunk.to_start = hunk.to_start;
        ahunk.to_count = hunk.to_count;
        ahunk.a_lines.resize(static_cast<size_t>(hunk.from_count));
        ahunk.b_lines.resize(static_cast<size_t>(hunk.to_count));

        // Deleted lines (A-side) and inserted lines (B-side) are candidates for
        // pairing; context/common lines get whole-line Common segments immediately.
        struct ChangedLine {
            size_t line_idx;  // index into ahunk.a_lines / b_lines
            std::string text;
            std::vector<Token> tokens;
        };
        std::vector<ChangedLine> dels, inss;

        size_t a_i = 0, b_i = 0;
        for (const auto& edit : hunk.edit_units) {
            if (edit.a_index.valid) {
                const std::string& line = diff_input.A[static_cast<long>(edit.a_index)].line;
                ahunk.a_lines[a_i].type = edit.type;
                ahunk.a_lines[a_i].line_index = edit.a_index;
                if (edit.type == EditType::Delete) {
                    dels.push_back({a_i, line, tokenize(line)});
                } else {
                    whole_line_segments(line, edit.type, ahunk.a_lines[a_i], ignore_whitespace);
                }
                a_i++;
            }
            if (edit.b_index.valid) {
                const std::string& line = diff_input.B[static_cast<long>(edit.b_index)].line;
                ahunk.b_lines[b_i].type = edit.type;
                ahunk.b_lines[b_i].line_index = edit.b_index;
                if (edit.type == EditType::Insert) {
                    inss.push_back({b_i, line, tokenize(line)});
                } else {
                    whole_line_segments(line, edit.type, ahunk.b_lines[b_i], ignore_whitespace);
                }
                b_i++;
            }
        }

        // Greedy best-match pairing: score every delete×insert pair, then assign the
        // highest-scoring pairs first, each line used at most once.
        std::vector<bool> del_used(dels.size(), false), ins_used(inss.size(), false);
        if (!dels.empty() && !inss.empty() && dels.size() * inss.size() <= kPairBudget) {
            struct Cand {
                double sim;
                size_t di;
                size_t ii;
            };
            std::vector<Cand> cands;
            for (size_t di = 0; di < dels.size(); di++) {
                for (size_t ii = 0; ii < inss.size(); ii++) {
                    const double s = line_similarity(dels[di].tokens, inss[ii].tokens);
                    if (s >= kPairThreshold) {
                        cands.push_back({s, di, ii});
                    }
                }
            }
            std::stable_sort(cands.begin(), cands.end(),
                             [](const Cand& x, const Cand& y) { return x.sim > y.sim; });
            for (const auto& c : cands) {
                if (del_used[c.di] || ins_used[c.ii]) {
                    continue;
                }
                del_used[c.di] = ins_used[c.ii] = true;
                diff_line_pair(dels[c.di].text, inss[c.ii].text, ahunk.a_lines[dels[c.di].line_idx],
                               ahunk.b_lines[inss[c.ii].line_idx], ignore_whitespace);
            }
        }
        // Unpaired changed lines are pure delete / insert.
        for (size_t di = 0; di < dels.size(); di++) {
            if (!del_used[di]) {
                whole_line_segments(dels[di].text, EditType::Delete,
                                    ahunk.a_lines[dels[di].line_idx], ignore_whitespace);
            }
        }
        for (size_t ii = 0; ii < inss.size(); ii++) {
            if (!ins_used[ii]) {
                whole_line_segments(inss[ii].text, EditType::Insert,
                                    ahunk.b_lines[inss[ii].line_idx], ignore_whitespace);
            }
        }

        output.push_back(std::move(ahunk));
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

// Tag deleted/inserted lines that form a block reappearing verbatim elsewhere in
// the diff as a move (GAP-9): a run of >= kMinMoveLines deleted lines whose
// content matches a run of inserted lines gets a shared move_id, and each line
// records the counterpart block's start line so the UI can draw an arrow (within
// the file) or a reference. Runs as a cross-hunk pass over the whole file diff.
void
detect_moves(const DiffInput<diffy::Line>& in, std::vector<AnnotatedHunk>& hunks) {
    // A line-hash match alone does NOT make a move: a run of bare "}" / "});" / blank
    // lines matches all over a file yet relocates nothing. So every candidate run must
    // (a) be long enough and (b) carry real content — measured as the number of lines
    // holding at least one "content" character (an identifier/literal char: alphanumeric,
    // '_', or any non-ASCII/UTF-8 byte). A run of pure brackets/blank lines has zero
    // such lines and is rejected however long it is — this kills the "useless" moves
    // (a lone closing bracket, or several of them) that a plain hash match would flag.
    // A 2-line run is the most coincidence-prone, so it additionally needs
    // kMinShortMoveChars of non-whitespace; this still catches a small function whose
    // closing brace is shared as context (its pure-deleted run is one line short of the
    // source) while rejecting the trivial "}" + blank tail two blocks often share (GAP-9).
    constexpr int kMinMoveLines = 3;
    constexpr int kMinShortMoveLines = 2;
    constexpr size_t kMinShortMoveChars = 16;
    constexpr size_t kMinSubstantiveMoveLines = 2;  // lines that must carry real content
    constexpr size_t kMoveBudget = 1u << 22;  // skip pathological dels*inss searches

    struct Ref {
        EditLine* el;
        uint32_t hash;
        int64_t lineno;           // 1-based
        const std::string* text;  // for content-verify (guards hash collisions)
    };
    std::vector<Ref> dels, inss;
    for (auto& h : hunks) {
        for (auto& el : h.a_lines) {
            if (el.type == EditType::Delete && el.line_index.valid) {
                const auto& L = in.A[static_cast<long>(el.line_index)];
                dels.push_back({&el, L.checksum, static_cast<int64_t>(el.line_index) + 1, &L.line});
            }
        }
        for (auto& el : h.b_lines) {
            if (el.type == EditType::Insert && el.line_index.valid) {
                const auto& L = in.B[static_cast<long>(el.line_index)];
                inss.push_back({&el, L.checksum, static_cast<int64_t>(el.line_index) + 1, &L.line});
            }
        }
    }
    if (dels.empty() || inss.empty() || dels.size() * inss.size() > kMoveBudget) {
        return;
    }

    std::unordered_multimap<uint32_t, size_t> ins_by_hash;
    for (size_t k = 0; k < inss.size(); ++k) {
        ins_by_hash.emplace(inss[k].hash, k);
    }

    std::vector<bool> ins_used(inss.size(), false);
    int next_move_id = 0;
    for (size_t di = 0; di < dels.size();) {
        // Longest still-free insert run matching the delete run starting at di.
        size_t best_len = 0, best_ii = 0;
        auto range = ins_by_hash.equal_range(dels[di].hash);
        for (auto it = range.first; it != range.second; ++it) {
            size_t ii = it->second;
            size_t len = 0;
            while (di + len < dels.size() && ii + len < inss.size() && !ins_used[ii + len] &&
                   dels[di + len].hash == inss[ii + len].hash &&
                   *dels[di + len].text == *inss[ii + len].text) {
                ++len;
            }
            if (len > best_len) {
                best_len = len;
                best_ii = ii;
            }
        }
        // Quality gate (GAP-9): a hash match is not enough. Count, across the matched
        // block, the lines that carry real content and the total non-whitespace chars.
        // A "content" line has >= 1 identifier/literal char (alphanumeric, '_', or a
        // non-ASCII byte, which in UTF-8 is part of a word/string); pure bracket/blank
        // lines have none.
        auto is_content = [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                   (c >= 'a' && c <= 'z') || c == '_' || c > 0x7F;
        };
        size_t substantive = 0, nonws = 0;
        for (size_t k = 0; k < best_len; ++k) {
            bool has_content = false;
            for (unsigned char c : *dels[di + k].text) {
                if (c > ' ') {
                    ++nonws;
                }
                has_content = has_content || is_content(c);
            }
            if (has_content) {
                ++substantive;
            }
        }
        // Accept only a run that is long enough, has enough content lines (so a bracket-
        // only run of ANY length is dropped), and — for the coincidence-prone 2-line
        // case — enough non-whitespace overall.
        bool accept = best_len >= static_cast<size_t>(kMinShortMoveLines) &&
                      substantive >= kMinSubstantiveMoveLines &&
                      (best_len >= static_cast<size_t>(kMinMoveLines) ||
                       nonws >= kMinShortMoveChars);
        if (accept) {
            ++next_move_id;
            const int64_t ins_start = inss[best_ii].lineno;
            const int64_t del_start = dels[di].lineno;
            for (size_t k = 0; k < best_len; ++k) {
                dels[di + k].el->move_id = next_move_id;
                dels[di + k].el->move_line = ins_start;  // deleted block moved *to* here
                inss[best_ii + k].el->move_id = next_move_id;
                inss[best_ii + k].el->move_line = del_start;  // inserted block came *from* here
                ins_used[best_ii + k] = true;
            }
            di += best_len;
        } else {
            ++di;
        }
    }
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
    detect_moves(diff_input, hunks_annotated);
    return hunks_annotated;
}
