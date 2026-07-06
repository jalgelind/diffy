#include <doctest.h>

#include "util/mapped_file.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace diffy;

namespace {

std::vector<uint8_t>
pseudo_random(size_t n, uint32_t seed) {
    std::vector<uint8_t> out;
    out.reserve(n);
    uint32_t x = seed;
    for (size_t i = 0; i < n; ++i) {
        x = 1103515245u * x + 12345u;
        out.push_back(static_cast<uint8_t>((x >> 16) & 0xff));
    }
    return out;
}

// Write bytes to a unique temp path and return it. Caller removes it.
std::string
write_temp(const std::vector<uint8_t>& data, const char* tag) {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / (std::string("diffy_mmap_test_") + tag + ".bin");
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    f.close();
    return p.string();
}

bool
matches(const FileBytes& fb, const std::vector<uint8_t>& expected) {
    if (fb.size() != expected.size()) {
        return false;
    }
    const auto bytes = fb.bytes();
    for (size_t i = 0; i < expected.size(); ++i) {
        if (bytes[static_cast<std::ptrdiff_t>(i)] != expected[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("FileBytes: small file uses the read path and returns exact bytes") {
    const auto data = pseudo_random(1024, 5);  // below kMmapThreshold
    const std::string path = write_temp(data, "small");

    FileBytes fb;
    REQUIRE(fb.load(path));
    CHECK(fb.size() == data.size());
    CHECK(matches(fb, data));

    std::filesystem::remove(path);
}

TEST_CASE("FileBytes: large file uses mmap and returns exact bytes") {
    const auto data = pseudo_random(kMmapThreshold + 4096, 9);  // above threshold
    const std::string path = write_temp(data, "large");

    FileBytes fb;
    REQUIRE(fb.load(path));
    CHECK(fb.size() == data.size());
    CHECK(matches(fb, data));

    std::filesystem::remove(path);
}

TEST_CASE("FileBytes: move keeps the bytes valid (owned and mapped)") {
    for (size_t n : {size_t(2048), kMmapThreshold + 1000}) {
        const auto data = pseudo_random(n, 11);
        const std::string path = write_temp(data, "move");

        FileBytes src;
        REQUIRE(src.load(path));
        FileBytes dst = std::move(src);
        CHECK(matches(dst, data));

        std::filesystem::remove(path);
    }
}

TEST_CASE("FileBytes: missing file fails cleanly") {
    FileBytes fb;
    CHECK_FALSE(fb.load("/no/such/diffy/file/xyzzy.bin"));
    CHECK(fb.size() == 0);
}

TEST_CASE("FileBytes: empty file loads as zero bytes (read path, no mmap)") {
    const std::string path = write_temp({}, "empty");
    FileBytes fb;
    REQUIRE(fb.load(path));
    CHECK(fb.size() == 0);
    CHECK(fb.bytes().empty());
    std::filesystem::remove(path);
}

TEST_CASE("FileBytes: non-regular input uses the read fallback") {
    // A character device isn't a regular file, so mmap is skipped and the read
    // path handles it (reading /dev/null yields zero bytes).
    if (std::filesystem::exists("/dev/null")) {
        FileBytes fb;
        REQUIRE(fb.load("/dev/null"));
        CHECK(fb.size() == 0);
    }
}
