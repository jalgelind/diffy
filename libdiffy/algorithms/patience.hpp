#pragma once

#include "algorithm.hpp"
#include "myers_linear.hpp"

#include <gsl/span>
#include <algorithm>  // std::reverse, std::sort, std::max
#include <map>
#include <numeric>  // std::accumulate
#include <optional>
#include <set>
#include <unordered_map>

namespace diffy {

template <typename Unit>
struct Patience : public Algorithm<Unit> {
    struct Slice {
        int64_t a_low = 0;
        int64_t a_high = 0;
        int64_t b_low = 0;
        int64_t b_high = 0;

        bool
        empty() {
            return !(a_low < a_high && b_low < b_high);
        }
    };

    struct Match {
        int64_t a_index;
        int64_t b_index;
        Match* prev = nullptr;
        Match* next = nullptr;
    };

    int64_t N;
    int64_t M;

    const gsl::span<Unit>& A;
    const gsl::span<Unit>& B;

    Patience(DiffInput<Unit>& diff_input)
        : Algorithm<Unit>(diff_input)
        , N(static_cast<int64_t>(diff_input.A.size()))
        , M(static_cast<int64_t>(diff_input.B.size()))
        , A(diff_input.A)
        , B(diff_input.B) {
    }

    virtual ~Patience() {
    }

    std::vector<Match>
    index_unique_lines(const Slice& s) {
        struct record {
            // 32-bit so a line repeated > 65535 times (large generated files with
            // many blank lines) can't wrap the count back to 1 and masquerade as a
            // unique anchor.
            std::uint32_t a_count = 0;
            std::uint32_t b_count = 0;
            int64_t a_index = 0;
            int64_t b_index = 0;
        };

        std::unordered_map<uint32_t, record> records;
        records.reserve(static_cast<std::size_t>((s.a_high - s.a_low) + (s.b_high - s.b_low)));

        for (auto i = s.a_low; i < s.a_high; i++) {
            const auto& a = A[i].hash();
            records[a].a_count++;
            records[a].a_index = i;
        }

        for (auto i = s.b_low; i < s.b_high; i++) {
            const auto& b = B[i].hash();
            records[b].b_count++;
            records[b].b_index = i;
        }

        std::vector<Match> matches;
        for (const auto& kv : records) {
            // A hash unique in both slices is an anchor candidate — but only if the
            // two lines are really equal. Byte-verify (Unit::operator==) to reject a
            // 32-bit checksum collision between two different unique lines, which
            // would otherwise anchor them together and render changed text as
            // unchanged (TXT-1).
            if (kv.second.a_count == 1 && kv.second.b_count == 1 &&
                A[kv.second.a_index] == B[kv.second.b_index]) {
                matches.push_back({kv.second.a_index, kv.second.b_index});
            }
        }

        std::sort(matches.begin(), matches.end(), [](Match& a, Match& b) { return a.a_index < b.a_index; });

        return matches;
    }

    // Histogram-style fallback anchor. When no line is unique on both sides,
    // patience has nothing to split on and would hand the whole slice to Myers.
    // Instead pick the rarest line the two slices share -- the shared line with
    // the lowest occurrence count -- and anchor on it, like git's histogram diff.
    // The count is capped (git xdiff uses 64) so a line repeated hundreds of
    // times can't become an O(n*m) anchor. Returns nullopt when the slices share
    // no line within the cap, in which case the caller falls back to Myers.
    std::optional<Match>
    find_rare_anchor(const Slice& s) {
        constexpr std::uint32_t kMaxOccurrences = 64;
        struct record {
            std::uint32_t a_count = 0;
            std::uint32_t b_count = 0;
            int64_t a_index = 0;  // first occurrence in A
            int64_t b_index = 0;  // first occurrence in B
        };

        std::unordered_map<uint32_t, record> records;
        records.reserve(static_cast<std::size_t>((s.a_high - s.a_low) + (s.b_high - s.b_low)));

        for (auto i = s.a_low; i < s.a_high; i++) {
            auto& r = records[A[i].hash()];
            if (r.a_count == 0) {
                r.a_index = i;
            }
            r.a_count++;
        }
        for (auto i = s.b_low; i < s.b_high; i++) {
            auto& r = records[B[i].hash()];
            if (r.b_count == 0) {
                r.b_index = i;
            }
            r.b_count++;
        }

        std::optional<Match> best;
        std::uint32_t best_occ = kMaxOccurrences + 1;
        for (const auto& kv : records) {
            const auto& r = kv.second;
            if (r.a_count == 0 || r.b_count == 0) {
                continue;  // not shared
            }
            const std::uint32_t occ = std::max(r.a_count, r.b_count);
            if (occ > kMaxOccurrences || occ >= best_occ) {
                continue;  // too common, or no better than what we have
            }
            // Byte-verify to reject a 32-bit checksum collision (same guard as
            // index_unique_lines): a false anchor would render changed text as
            // unchanged.
            if (!(A[r.a_index] == B[r.b_index])) {
                continue;
            }
            best = Match{r.a_index, r.b_index};
            best_occ = occ;
        }
        return best;
    }

    Match*
    patience_sort(std::vector<Match>& matches) {
        // Patience sort for the longest increasing subsequence of b_index
        // (matches are sorted by a_index); recover the LIS via the prev links.
        std::vector<Match*> piles;
        piles.reserve(matches.size());

        for (auto& match : matches) {
            auto found = std::lower_bound(piles.begin(), piles.end(), &match,
                                          [](Match* a, Match* b) { return a->b_index < b->b_index; });
            match.prev = (found == piles.begin()) ? nullptr : *(found - 1);
            if (found == piles.end()) {
                piles.push_back(&match);
            } else {
                *found = &match;
            }
        }

        if (piles.empty())
            return nullptr;

        Match* match = piles.back();
        while (match->prev != nullptr) {
            match->prev->next = match;
            match = match->prev;
        }

        return match;
    }

    // Appends this slice's edits to `out`.
    void
    do_diff(const Slice& in_slice, std::vector<Edit>& out) {
        auto unique_lines = index_unique_lines(in_slice);
        auto* match = patience_sort(unique_lines);
        if (!match) {
            // No unique anchor. Before handing the whole slice to Myers, try a
            // histogram-style split on the rarest shared line: anchor on it, then
            // recurse on the (strictly smaller) regions on either side. Only when
            // the slices share nothing within the occurrence cap do we fall back.
            if (auto anchor = find_rare_anchor(in_slice)) {
                do_diff({in_slice.a_low, anchor->a_index, in_slice.b_low, anchor->b_index}, out);
                out.push_back({EditType::Common, EditIndex(anchor->a_index), EditIndex(anchor->b_index)});
                do_diff({anchor->a_index + 1, in_slice.a_high, anchor->b_index + 1, in_slice.b_high}, out);
                return;
            }

            auto a_count = in_slice.a_high - in_slice.a_low;
            auto b_count = in_slice.b_high - in_slice.b_low;
            assert(a_count >= 0 && "negative a span?");
            assert(b_count >= 0 && "negative b span?");

            DiffInput<Unit> algo_input{A.subspan(in_slice.a_low, a_count), B.subspan(in_slice.b_low, b_count),
                                       "A", "B"};
            auto result = MyersLinear<Unit>{algo_input}.compute();

            for (auto& e : result.edit_sequence) {
                e.a_index.value += static_cast<int32_t>(in_slice.a_low);
                e.b_index.value += static_cast<int32_t>(in_slice.b_low);
                out.push_back(e);
            }

            return;
        }

        auto a_index = in_slice.a_low;
        auto b_index = in_slice.b_low;

        while (true) {
            int64_t a_next;
            int64_t b_next;

            if (match != nullptr) {
                a_next = match->a_index;
                b_next = match->b_index;
            } else {
                a_next = in_slice.a_high;
                b_next = in_slice.b_high;
            }

            assert(a_index <= a_next);
            assert(b_index <= b_next);

            Slice subslice = {a_index, a_next, b_index, b_next};
            auto adjust_head = [this](Slice& slice, std::vector<Edit>& dst) {
                while (!slice.empty() && A[slice.a_low] == B[slice.b_low]) {
                    dst.push_back({EditType::Common, EditIndex(slice.a_low), EditIndex(slice.b_low)});
                    slice.a_low += 1;
                    slice.b_low += 1;
                }
            };
            auto adjust_tail = [this](Slice& slice) {
                std::vector<Edit> tail;
                // push_back + reverse, not insert-at-front (which is O(n^2)).
                while (!slice.empty() && A[slice.a_high - 1] == B[slice.b_high - 1]) {
                    slice.a_high -= 1;
                    slice.b_high -= 1;
                    tail.push_back({EditType::Common, EditIndex(slice.a_high), EditIndex(slice.b_high)});
                }
                std::reverse(tail.begin(), tail.end());
                return tail;
            };

            adjust_head(subslice, out);
            auto tail = adjust_tail(subslice);

            do_diff(subslice, out);
            for (const auto& e : tail) {
                out.push_back(e);
            }

            if (match == nullptr) {
                return;
            }

            out.push_back({
                EditType::Common,
                match->a_index,
                match->b_index,
            });

            a_index = match->a_index + 1;
            b_index = match->b_index + 1;
            match = match->next;
        }
    }

    DiffResult
    diff() {
        // Refresh sizes from the (possibly prefix/suffix-trimmed) spans compute()
        // hands us; A/B are references so they already point at the trimmed core.
        N = static_cast<int64_t>(A.size());
        M = static_cast<int64_t>(B.size());
        auto s = Slice{0, N, 0, M};
        DiffResult result;
        do_diff(s, result.edit_sequence);
        int64_t common_count = std::accumulate(
            result.edit_sequence.begin(), result.edit_sequence.end(), (int64_t) 0,
            [](uint64_t acc, const auto& e) { return e.type == EditType::Common ? acc + 1 : acc; });
        result.status = (N == M && N == common_count) ? DiffResultStatus::NoChanges : DiffResultStatus::OK;
        return result;
    }
};

}  // namespace diffy
