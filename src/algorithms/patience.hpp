#pragma once

#include "algorithm.hpp"
#include "myers_linear.hpp"

#include <gsl/span>
#include <map>
#include <numeric>  // std::accumulate
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
            std::uint16_t a_count = 0;
            std::uint16_t b_count = 0;
            int64_t a_index = 0;
            int64_t b_index = 0;
        };

        std::unordered_map<uint32_t, record> records;

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
            if (kv.second.a_count == 1 && kv.second.b_count == 1) {
                matches.push_back({kv.second.a_index, kv.second.b_index});
            }
        }

        std::sort(matches.begin(), matches.end(), [](Match& a, Match& b) { return a.a_index < b.a_index; });

        return matches;
    }

    Match*
    patience_sort(std::vector<Match>& matches) {
        std::vector<Match*> stacks;

        for (auto& match : matches) {
            const auto& found = std::lower_bound(stacks.cbegin(), stacks.cend(), &match,
                                                 [](Match* a, Match* b) { return a->b_index < b->b_index; });

            if (found != stacks.end()) {
                match.prev = *found;
                stacks.insert(found, &match);
            } else if (stacks.empty()) {
                stacks.insert(stacks.begin(), &match);
            }
        }

        if (stacks.empty())
            return nullptr;

        auto& match = stacks.back();
        if (!match)
            return nullptr;

        while (match->prev != nullptr) {
            match->prev->next = match;
            match = match->prev;
        }

        return match;
    }

    std::vector<Edit>
    do_diff(const Slice& in_slice) {
        auto unique_lines = index_unique_lines(in_slice);
        auto* match = patience_sort(unique_lines);
        if (!match) {
            std::vector<Edit> edits;
            auto a_count = in_slice.a_high - in_slice.a_low;
            auto b_count = in_slice.b_high - in_slice.b_low;
            assert(a_count >= 0 && "negative a span?");
            assert(b_count >= 0 && "negative b span?");

            DiffInput<Unit> algo_input{A.subspan(in_slice.a_low, a_count), B.subspan(in_slice.b_low, b_count),
                                       "A", "B"};
            MyersLinear<Unit> diffctx{algo_input};
            auto result = diffctx.compute();

            // TODO: This should be properly done by the diff algo.
            // TODO: Line contains line number. use that instead of tracking this shit?
            //       Other Units must implement index number too.
            for (auto& e : result.edit_sequence) {
                e.a_index.value += static_cast<uint64_t>(in_slice.a_low);
                e.b_index.value += static_cast<uint64_t>(in_slice.b_low);
            }

            return result.edit_sequence;
        }

        std::vector<Edit> edit_sequence;

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
            auto adjust_head = [this](Slice& slice) {
                std::vector<Edit> head;
                while (!slice.empty() && A[slice.a_low] == B[slice.b_low]) {
                    head.push_back({EditType::Common, EditIndex(slice.a_low), EditIndex(slice.b_low)});
                    slice.a_low += 1;
                    slice.b_low += 1;
                }
                return head;
            };
            auto adjust_tail = [this](Slice& slice) {
                std::vector<Edit> tail;
                while (!slice.empty() && A[slice.a_high - 1] == B[slice.b_high - 1]) {
                    slice.a_high -= 1;
                    slice.b_high -= 1;
                    // opt: push_back and reverse iteration?
                    tail.insert(tail.begin(),
                                {EditType::Common, EditIndex(slice.a_high), EditIndex(slice.b_high)});
                }
                return tail;
            };

            auto head = adjust_head(subslice);
            auto tail = adjust_tail(subslice);

            for (const auto& e : head) {
                edit_sequence.push_back(e);
            }
            for (const auto& e : do_diff(subslice)) {
                edit_sequence.push_back(e);
            }
            for (const auto& e : tail) {
                edit_sequence.push_back(e);
            }

            if (match == nullptr) {
                return edit_sequence;
            }

            edit_sequence.push_back({
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
        auto s = Slice{0, N, 0, M};
        DiffResult result;
        result.edit_sequence = do_diff(s);
        int64_t common_count = std::accumulate(
            result.edit_sequence.begin(), result.edit_sequence.end(), (int64_t) 0,
            [](uint64_t acc, const auto& e) { return e.type == EditType::Common ? acc + 1 : acc; });
        result.status = (N == M && N == common_count) ? DiffResultStatus::NoChanges : DiffResultStatus::OK;
        return result;
    }
};

}  // namespace diffy
