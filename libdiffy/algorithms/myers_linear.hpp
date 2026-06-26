#pragma once

// Linear version of Myers difference algorithm.
// O((M+N) D) in time, linear in space.
// https://blog.jcoglan.com/2017/04/25/myers-diff-in-linear-space-implementation/

#include "algorithm.hpp"
#include "util/bipolar_array.hpp"

#include <numeric>  // std::accumulate
#include <optional>

namespace diffy {

template <typename Unit>
struct MyersLinear : public Algorithm<Unit> {
    struct Box {
        int64_t left;
        int64_t top;
        int64_t right;
        int64_t bottom;

        int64_t
        width() const {
            return right - left;
        }
        int64_t
        height() const {
            return bottom - top;
        }
        int64_t
        size() const {
            return width() + height();
        }
        int64_t
        delta() const {
            return width() - height();
        }
    };

    int64_t N;
    int64_t M;

    const gsl::span<Unit>& A;
    const gsl::span<Unit>& B;

    MyersLinear(DiffInput<Unit>& diff_input)
        : Algorithm<Unit>(diff_input)
        , N(static_cast<int64_t>(diff_input.A.size()))
        , M(static_cast<int64_t>(diff_input.B.size()))
        , A(diff_input.A)
        , B(diff_input.B) {
    }

    virtual ~MyersLinear() {
    }

    bool
    is_odd(int64_t v) {
        return (v & 1) == 1;
    }

    bool
    is_between(int64_t v, int64_t low, int64_t high) {
        if (v > high)
            return false;
        if (v < low)
            return false;
        return true;
    }

    bool
    find_path(int64_t left, int64_t top, int64_t right, int64_t bottom, std::vector<Coordinate>& out) {
        Box box{left, top, right, bottom};

        assert(left >= 0);
        assert(top >= 0);
        assert(right >= 0);
        assert(bottom >= 0);

        auto snake = midpoint(box);
        if (!snake)
            return false;

        auto start = snake.value().from;
        auto finish = snake.value().to;

        auto head = find_path(box.left, box.top, start.x, start.y, out);
        auto tail = find_path(finish.x, finish.y, box.right, box.bottom, out);

        if (!head) {
            out.push_back(start);
        }
        if (!tail) {
            out.push_back(finish);
        }

        return true;
    }

    std::optional<Move>
    midpoint(Box box) {
        if (box.size() == 0) {
            return std::nullopt;
        }

        const int64_t max = 1 + ((box.size() - 1) / 2);

        BipolarArray<int64_t> vf{-max, max};
        vf[1] = box.left;

        BipolarArray<int64_t> vb{-max, max};
        vb[1] = box.bottom;

        for (auto d = 0; d <= max; d++) {
            if (auto m = forwards(box, vf, vb, d)) {
                return m;
            }
            if (auto m = backwards(box, vf, vb, d)) {
                return m;
            }
        }
        return std::nullopt;
    }

    std::optional<Move>
    forwards(Box box, BipolarArray<int64_t>& vf, BipolarArray<int64_t>& vb, int64_t d) {
        int64_t px = 0, py = 0, x = 0, y = 0;
        for (int64_t k = d; k >= -d; k -= 2) {
            auto c = k - box.delta();
            if ((k == -d) || (k != d && vf[k - 1] < vf[k + 1])) {
                px = vf[k + 1];
                x = px;
            } else {
                px = vf[k - 1];
                x = px + 1;
            }

            y = box.top + (x - box.left) - k;
            py = (d == 0 || x != px) ? y : y - 1;

            while (x < box.right && y < box.bottom && A[x] == B[y]) {
                x = x + 1;
                y = y + 1;
            }

            vf[k] = x;

            if (is_odd(box.delta()) && is_between(c, -(d - 1), d - 1) && y >= vb[c]) {
                return {{{px, py}, {x, y}}};
            }
        }
        return std::nullopt;
    }

    std::optional<Move>
    backwards(Box box, BipolarArray<int64_t>& vf, BipolarArray<int64_t>& vb, int64_t d) {
        int64_t px = -1, py = -1, x = -1, y = -1;
        for (int64_t c = d; c >= -d; c -= 2) {
            auto k = c + box.delta();

            if ((c == -d) || (c != d && vb[c - 1] > vb[c + 1])) {
                py = vb[c + 1];
                y = py;
            } else {
                py = vb[c - 1];
                y = py - 1;
            }

            x = box.left + (y - box.top) + k;
            px = (d == 0 || y != py) ? x : x + 1;

            while (x > box.left && y > box.top && A[x - 1] == B[y - 1]) {
                x = x - 1;
                y = y - 1;
            }

            vb[c] = y;

            if (!is_odd(box.delta()) and is_between(k, -d, d) && x <= vf[k]) {
                return {{{x, y}, {px, py}}};
            }
        }
        return std::nullopt;
    }

    std::vector<Move>
    walk_snakes(std::vector<Coordinate>& path) {
        std::vector<Move> moves;
        for (size_t i = 0; i < path.size() - 1; i++) {
            auto from = path[i];
            auto to = path[i + 1];

            from = walk_diagonal({from, to}, moves);
            auto xdiff = to.x - from.x;
            auto ydiff = to.y - from.y;
            if (xdiff < ydiff) {
                moves.push_back({{from.x, from.y}, {from.x, from.y + 1}});
                from.y += 1;
            } else if (xdiff > ydiff) {
                moves.push_back({{from.x, from.y}, {from.x + 1, from.y}});
                from.x += 1;
            }
            walk_diagonal({from, to}, moves);
        }
        return moves;
    }

    Coordinate
    walk_diagonal(Move move, std::vector<Move>& moves) {
        while (move.from.x < move.to.x && move.from.y < move.to.y && A[move.from.x] == B[move.from.y]) {
            Coordinate next = {move.from.x + 1, move.from.y + 1};
            moves.push_back({move.from, next});
            move.from = next;
        }
        return moves.empty() ? move.from : moves.back().to;
    }

    void
    do_diff(std::vector<Edit>& edit_sequence, std::vector<Move>& solution) {
        std::transform(solution.cbegin(), solution.cend(), std::back_inserter(edit_sequence),
                       [](const Move& move) -> Edit {
                           auto& from = move.from;
                           auto& to = move.to;

                           if (from.x == to.x) {
                               return {EditType::Insert, {}, {from.y}};
                           } else if (to.y == from.y) {
                               return {EditType::Delete, {from.x}, {}};
                           } else {
                               return {EditType::Common, {from.x}, {from.y}};
                           }
                       });
    }

    DiffResult
    diff() {
        DiffResult result;

        std::vector<Coordinate> path;
        bool found = find_path(0, 0, N, M, path);
        if (!found)
            return result;

        auto solution = walk_snakes(path);

        do_diff(result.edit_sequence, solution);

        int64_t common_count = std::accumulate(
            result.edit_sequence.begin(), result.edit_sequence.end(), 0,
            [](uint64_t acc, const auto& e) { return e.type == EditType::Common ? acc + 1 : acc; });
        result.status = (N == M && N == common_count) ? DiffResultStatus::NoChanges : DiffResultStatus::OK;
        return result;
    }
};

}  // namespace diffy
