// Tests for the extension-allowlist path classifier promoted from the GUI into
// libdiffy (looks_binary_path), living beside the content-sniff looks_binary.
#include "util/binary_detect.hpp"

#include <doctest.h>

#include <string>
#include <vector>

using namespace diffy;

TEST_CASE("looks_binary_path: every allowlisted extension is binary") {
    // The full allowlist — each suffix must classify as binary regardless of the
    // directory prefix. Kept in sync with looks_binary_path's own table.
    const std::vector<std::string> binary_exts = {
        ".ldr",  ".bin",  ".o",    ".a",    ".so",   ".dylib", ".dll",  ".exe",  ".lib",
        ".elf",  ".hex",  ".img",  ".dat",  ".wasm", ".pyc",   ".class", ".pdb",
        ".png",  ".jpg",  ".jpeg", ".gif",  ".bmp",  ".ico",   ".webp", ".tiff", ".tif",
        ".pdf",  ".zip",  ".gz",   ".bz2",  ".xz",   ".7z",    ".tar",  ".rar",  ".jar",
        ".mp3",  ".wav",  ".flac", ".ogg",  ".mp4",  ".mov",   ".avi",  ".mkv",  ".webm",
        ".ttf",  ".otf",  ".woff", ".woff2", ".eot",
        ".xlsx", ".xls",  ".docx", ".doc",  ".pptx", ".ppt",   ".odt",  ".ods",
    };
    for (const auto& ext : binary_exts) {
        INFO("extension: " << ext);
        CHECK(looks_binary_path("assets/file" + ext));
    }
}

TEST_CASE("looks_binary_path: source / text extensions are not binary") {
    CHECK_FALSE(looks_binary_path("src/main.cpp"));
    CHECK_FALSE(looks_binary_path("util/binary_detect.cc"));
    CHECK_FALSE(looks_binary_path("util/binary_detect.hpp"));
    CHECK_FALSE(looks_binary_path("notes.txt"));
    CHECK_FALSE(looks_binary_path("build.py"));
    CHECK_FALSE(looks_binary_path("README.md"));
    CHECK_FALSE(looks_binary_path("data.json"));
}

TEST_CASE("looks_binary_path: extension match is case-insensitive") {
    CHECK(looks_binary_path("a/b/c.PNG"));
    CHECK(looks_binary_path("IMG.JpG"));
    CHECK(looks_binary_path("archive.TAR"));
    CHECK(looks_binary_path("font.WOFF2"));
}

TEST_CASE("looks_binary_path: no extension is treated as text") {
    CHECK_FALSE(looks_binary_path("README"));    // extensionless name
    CHECK_FALSE(looks_binary_path("Makefile"));
    CHECK_FALSE(looks_binary_path("bin/tool"));  // no dot at all
    CHECK_FALSE(looks_binary_path(""));          // empty path
}

TEST_CASE("looks_binary_path: a dot before the last slash isn't an extension") {
    CHECK_FALSE(looks_binary_path(".gitignore"));   // dotfile: not an allowlisted suffix
    CHECK_FALSE(looks_binary_path("dir.bin/file")); // dot is before the last slash
    CHECK_FALSE(looks_binary_path("a.d/README"));   // dotted dir, extensionless file
    CHECK(looks_binary_path("dir.src/logo.png"));   // real extension after a dotted dir
}
