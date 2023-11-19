#include "config_parser.hpp"
#include "config_parser_utils.hpp"
#include "config_serializer.hpp"

#include <doctest.h>

#include <fmt/format.h>

#include <string>
#include <vector>

using namespace diffy;

TEST_CASE("config_tokenizer_matcher") {
    SUBCASE("match-for-loop") {
                std::string cfg_text = R"foo({
int main() {
  for (int i = 0; i < 3; i++) {
      i++;
})foo";

        // fmt::print("{}\n", cfg_serialize_obj(value));
    }
}