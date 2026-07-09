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

    SUBCASE("embedded NUL is preserved and matches readlines_from_string") {
        // TXT-7: std::string(line) truncated at the first NUL, so path-based
        // readlines disagreed with readlines_from_string on binary-ish text.
        const std::string content = std::string("a\0b\n", 4) + "tail\n";
        auto p = write_temp("diffy_rl_nul.txt", content);
        auto from_file = readlines(p, false);
        auto from_str = readlines_from_string(content, false);
        REQUIRE(from_file.size() == 2);
        REQUIRE(from_str.size() == 2);
        CHECK(from_file[0].line.size() == 4);              // "a\0b\n", not truncated to "a"
        CHECK(from_file[0].line == std::string("a\0b\n", 4));
        CHECK(from_file[0].line == from_str[0].line);
        CHECK(from_file[0].checksum == from_str[0].checksum);
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

TEST_CASE("Line::operator== byte-verifies on checksum match (TXT-1)") {
    // Two DIFFERENT lines forced to share a checksum must NOT compare equal — a
    // 32-bit crc32c collision must never let the diff render changed as unchanged.
    Line a{1, 0xDEADBEEFu, "alpha\n", "", false};
    Line b{2, 0xDEADBEEFu, "bravo\n", "", false};
    CHECK_FALSE(a == b);
    Line c{3, 0xDEADBEEFu, "alpha\n", "", false};
    CHECK(a == c);  // same checksum + same content

    // Under ignore_whitespace the normalized key is compared, not the display text:
    // reindent-only lines match; a real change differs even on a checksum collision.
    Line d{4, 0x12345678u, "\tif (x)\n", "if(x)", true};
    Line e{5, 0x12345678u, "    if ( x )\n", "if(x)", true};
    CHECK(d == e);
    Line f{6, 0x12345678u, "    if ( y )\n", "if(y)", true};
    CHECK_FALSE(d == f);
}

TEST_CASE("readlines ignore_whitespace") {
    auto hash_of = [](const std::string& content) {
        return readlines_from_string(content, /*ignore_line_endings=*/false,
                                     /*ignore_whitespace=*/true);
    };

    SUBCASE("lines differing only in whitespace hash equal") {
        auto a = hash_of("\tif (x) {\n");
        auto b = hash_of("    if (x) {\n");      // tab vs spaces, reindented
        auto c = hash_of("if(x){\n");           // inter-token spacing removed
        REQUIRE(a.size() == 1);
        REQUIRE(b.size() == 1);
        REQUIRE(c.size() == 1);
        CHECK(a[0].checksum == b[0].checksum);
        CHECK(a[0].checksum == c[0].checksum);
        // Display text is preserved; only the comparison checksum is normalized.
        CHECK(a[0].line == "\tif (x) {\n");
        CHECK(b[0].line == "    if (x) {\n");
    }

    SUBCASE("a real content change still differs") {
        auto a = hash_of("int a = 1;\n");
        auto b = hash_of("int a = 2;\n");
        REQUIRE(a.size() == 1);
        REQUIRE(b.size() == 1);
        CHECK(a[0].checksum != b[0].checksum);
    }

    SUBCASE("blank and whitespace-only lines collapse together") {
        auto a = hash_of("\n");
        auto b = hash_of("   \t\n");
        REQUIRE(a.size() == 1);
        REQUIRE(b.size() == 1);
        CHECK(a[0].checksum == b[0].checksum);
    }

    SUBCASE("operator== matches on the normalized key, not the display text") {
        auto a = hash_of("\tif (x) {\n");
        auto b = hash_of("    if (x) {\n");  // reindented
        auto c = hash_of("int a = 2;\n");    // genuinely different
        REQUIRE(a.size() == 1);
        REQUIRE(b.size() == 1);
        REQUIRE(c.size() == 1);
        CHECK(a[0] == b[0]);        // whitespace-only difference -> equal
        CHECK_FALSE(a[0] == c[0]);  // real content difference -> not equal
    }
}
