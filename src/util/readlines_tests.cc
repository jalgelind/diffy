// Tests for readlines: line splitting, the ignore_line_endings option, and
// missing-file handling.

#include "util/readlines.hpp"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace diffy;

namespace {

std::string
write_temp(const std::string& name, const std::string& content) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary);
    f << content;
    f.close();
    return p.string();
}

}  // namespace

TEST_CASE("readlines") {
    SUBCASE("splits on newlines and keeps the terminator") {
        auto p = write_temp("diffy_rl_a.txt", "alpha\nbeta\n");
        auto lines = readlines(p, false);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].line == "alpha\n");
        CHECK(lines[1].line == "beta\n");
    }

    SUBCASE("ignore_line_endings strips the trailing newline") {
        auto p = write_temp("diffy_rl_b.txt", "alpha\nbeta\n");
        auto lines = readlines(p, true);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0].line == "alpha");
        CHECK(lines[1].line == "beta");
    }

    SUBCASE("final line without trailing newline") {
        auto p = write_temp("diffy_rl_c.txt", "no newline");
        auto lines = readlines(p, false);
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].line == "no newline");
    }

    SUBCASE("missing file yields no lines") {
        auto lines = readlines("/definitely/not/here_xyz", false);
        CHECK(lines.empty());
    }

    SUBCASE("equal content hashes equal; different content differs") {
        auto p1 = write_temp("diffy_rl_d.txt", "same\n");
        auto p2 = write_temp("diffy_rl_e.txt", "same\n");
        auto p3 = write_temp("diffy_rl_f.txt", "different\n");
        auto l1 = readlines(p1, false);
        auto l2 = readlines(p2, false);
        auto l3 = readlines(p3, false);
        REQUIRE(l1.size() == 1);
        REQUIRE(l2.size() == 1);
        REQUIRE(l3.size() == 1);
        CHECK(l1[0].checksum == l2[0].checksum);
        CHECK(l1[0].checksum != l3[0].checksum);
    }
}
