#pragma once

#include <doctest.h>

#include <cstring>
#include <memory>

namespace diffy {

// The myers algorithm use bipolar array indexing for tracking k-values (-D..D)
template <typename Type>
struct BipolarArray {
    int64_t min_;
    int64_t max_;
    std::size_t capacity_;
    std::unique_ptr<Type[]> arr_;  // TODO(opt): Align to 16 byte boundary for faster copies?

    BipolarArray(int64_t min, int64_t max)
        : min_(min), max_(max), capacity_(static_cast<std::size_t>(max - min + 1) /* +1 for zero */) {
        assert(max - min + 1 >= 0);
        arr_ = std::make_unique<Type[]>(capacity_);
    }

    BipolarArray(const BipolarArray& other) : min_(other.min_), max_(other.max_), capacity_(other.capacity_) {
        // NOTE: make_unique would initialize the array and it showed up as a significant offender when
        // profiling.
        arr_ = std::unique_ptr<Type[]>{new Type[capacity_]};
        std::memmove(arr_.get(), other.arr_.get(), other.capacity_ * sizeof(Type));
    }

    Type&
    operator[](int64_t index) {
        auto offset = -min_ + index;
        assert(offset >= 0);
        assert(offset < static_cast<int64_t>(capacity_));
        return arr_.get()[offset];
    }
};

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

}  // namespace diffy