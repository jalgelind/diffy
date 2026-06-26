// Tests for compose_hunks: grouping an edit sequence into hunks with context.

#include "processing/diff_hunk.hpp"

#include <doctest.h>

#include <vector>

using namespace diffy;

namespace {

Edit
ec(int64_t a, int64_t b) {
    return Edit{EditType::Common, EditIndex(a), EditIndex(b)};
}
Edit
ed(int64_t a) {
    return Edit{EditType::Delete, EditIndex(a), EditIndexInvalid};
}
Edit
ei(int64_t b) {
    return Edit{EditType::Insert, EditIndexInvalid, EditIndex(b)};
}

// Flatten the changes (non-Common edits) into comparable tuples.
std::vector<std::tuple<int, int64_t, int64_t>>
changes(const std::vector<Edit>& s) {
    std::vector<std::tuple<int, int64_t, int64_t>> out;
    for (const auto& e : s)
        if (e.type != EditType::Common)
            out.emplace_back(static_cast<int>(e.type), e.a_index.value, e.b_index.value);
    return out;
}

}  // namespace

TEST_CASE("compose_hunks") {
    SUBCASE("empty sequence yields no hunks (and no underflow)") {
        CHECK(compose_hunks({}, 3).empty());
    }

    SUBCASE("all-common yields no hunks") {
        std::vector<Edit> s = {ec(0, 0), ec(1, 1), ec(2, 2)};
        CHECK(compose_hunks(s, 3).empty());
    }

    SUBCASE("single substitution — exact indices, context 0") {
        // a = x y z ; b = x w z ; line 2 changes.
        std::vector<Edit> s = {ec(0, 0), ed(1), ei(1), ec(2, 2)};
        auto h = compose_hunks(s, 0);
        REQUIRE(h.size() == 1);
        CHECK(h[0].from_start == 2);
        CHECK(h[0].from_count == 1);
        CHECK(h[0].to_start == 2);
        CHECK(h[0].to_count == 1);
        // edit_units hold exactly the delete + insert.
        REQUIRE(h[0].edit_units.size() == 2);
        CHECK(h[0].edit_units[0].type == EditType::Delete);
        CHECK(h[0].edit_units[1].type == EditType::Insert);
    }

    SUBCASE("context merge rule (gap < 2*context + 2)") {
        // Change at index 2, change at index 11, 8 common lines between.
        // gap = 11 - 2 = 9, so hunks merge once 9 < 2*context + 2, i.e. context >= 4.
        std::vector<Edit> s;
        s.push_back(ec(0, 0));
        s.push_back(ec(1, 1));
        s.push_back(ed(2));
        for (int i = 0; i < 8; i++)
            s.push_back(ec(3 + i, 2 + i));
        s.push_back(ei(10));
        s.push_back(ec(11, 11));

        CHECK(compose_hunks(s, 0).size() == 2);
        CHECK(compose_hunks(s, 3).size() == 2);
        CHECK(compose_hunks(s, 4).size() == 1);
        CHECK(compose_hunks(s, 100).size() == 1);
    }

    SUBCASE("change at very start and very end") {
        std::vector<Edit> s = {ed(0), ec(1, 0), ec(2, 1), ei(2)};
        auto h = compose_hunks(s, 0);
        REQUIRE(h.size() == 2);
        CHECK(h[0].edit_units.front().type == EditType::Delete);
        CHECK(h[1].edit_units.back().type == EditType::Insert);
    }

    SUBCASE("hunks partition all changes, in order") {
        // Two well-separated changes; every non-common edit must appear in exactly
        // one hunk, preserving order.
        std::vector<Edit> s;
        s.push_back(ed(0));
        for (int i = 0; i < 6; i++)
            s.push_back(ec(1 + i, i));
        s.push_back(ei(6));
        s.push_back(ec(7, 7));

        auto h = compose_hunks(s, 1);
        std::vector<Edit> flat;
        for (const auto& hunk : h)
            for (const auto& e : hunk.edit_units)
                flat.push_back(e);
        CHECK(changes(flat) == changes(s));
    }
}
