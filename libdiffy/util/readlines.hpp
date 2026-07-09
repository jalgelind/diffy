#pragma once

// TODO: MappedFile with a get_lines that returns gsl::string_span's?

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace diffy {

struct Line {
    uint32_t line_number;
    uint32_t checksum;
    std::string line;  // display text, kept verbatim for rendering

    // The exact bytes the checksum was computed from, stored only when they differ
    // from `line` — i.e. under ignore_whitespace, where `line` keeps the original
    // spacing but the checksum is over a whitespace-stripped copy. Empty (and
    // has_cmp_key == false) otherwise, so the default path costs no extra memory.
    std::string cmp_key;
    bool has_cmp_key = false;

    uint32_t
    hash() const {
        return checksum;
    }

    bool
    operator<(const Line& other) const {
        return checksum < other.checksum;
    }

    // Equality is checksum-first, then a byte compare of the hashed bytes. The
    // content check guards 32-bit crc32c collisions: two *different* lines that
    // happen to share a checksum must NOT compare equal, or the diff would show
    // changed content as unchanged (a diff tool must never do that). It compares
    // the whitespace-normalized key under ignore_whitespace so reindent-only lines
    // still match, and only runs once the checksums already agree (equal lines or a
    // rare collision), so it stays cheap.
    bool
    operator==(const Line& other) const {
        if (checksum != other.checksum) {
            return false;
        }
        const std::string& a = has_cmp_key ? cmp_key : line;
        const std::string& b = other.has_cmp_key ? other.cmp_key : other.line;
        return a == b;
    }
};

// `ignore_whitespace` makes line matching whitespace-insensitive: each line's
// checksum is computed from a whitespace-stripped copy of its text, so lines that
// differ only in indentation or inter-token spacing compare equal (the `diff -w` /
// `git diff -w` semantics). Line matching is done purely by checksum, so this is
// what collapses whitespace-only line changes to unchanged. The stored `line`
// text is always preserved for display; only the comparison checksum is affected.
std::vector<Line>
readlines(const std::string& path, bool ignore_line_endings, bool ignore_whitespace = false);

// Split an in-memory buffer into Lines using the same rules as readlines():
// each line keeps its trailing '\n' (a final line without one is kept as-is).
// Used by frontends that already hold the content in memory rather than reading
// it from a path.
std::vector<Line>
readlines_from_string(const std::string& content, bool ignore_line_endings,
                      bool ignore_whitespace = false);

}  // namespace diffy
