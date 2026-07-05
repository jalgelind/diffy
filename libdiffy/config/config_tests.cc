// Tests for config helpers that don't touch the filesystem.

#include "config/config.hpp"

#include <doctest.h>

using namespace diffy;

TEST_CASE("algo_from_string") {
    CHECK(algo_from_string("patience") == Algo::kPatience);
    CHECK(algo_from_string("p") == Algo::kPatience);
    CHECK(algo_from_string("default") == Algo::kPatience);
    CHECK(algo_from_string("myers-greedy") == Algo::kMyersGreedy);
    CHECK(algo_from_string("mg") == Algo::kMyersGreedy);
    CHECK(algo_from_string("myers-linear") == Algo::kMyersLinear);
    CHECK(algo_from_string("ml") == Algo::kMyersLinear);
    CHECK(algo_from_string("nonsense") == Algo::kInvalid);
    CHECK(algo_from_string("") == Algo::kInvalid);
}
