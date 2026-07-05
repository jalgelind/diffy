#include "bipolar_array.hpp"

#include <doctest.h>

TEST_CASE("BipolarArray") {
    SUBCASE("simple") {
        auto a = diffy::BipolarArray<unsigned int>(-1, 1);
        a[0] = 1;
        a[-1] = 1;

        REQUIRE(a[0] == 1);
        REQUIRE(a[-1] == 1);
    }

    SUBCASE("check_range") {
        auto a = diffy::BipolarArray<int>(-5, 5);
        for (int i = -5; i <= 5; i++) {
            a[i] = i;
        }
        for (int i = -5; i <= 5; i++) {
            CHECK(a[i] == i);
        }
    }

    SUBCASE("check_range2") {
        auto a = diffy::BipolarArray<int>(-1, 2);
        for (int i = -1; i <= 2; i++) {
            a[i] = i;
        }
        for (int i = -1; i <= 2; i++) {
            CHECK(a[i] == i);
        }
    }
}