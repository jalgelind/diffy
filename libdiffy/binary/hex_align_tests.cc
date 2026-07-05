#include <doctest.h>

#include "algorithms/needleman_wunsch.hpp"
#include "binary/chunker.hpp"
#include "binary/hex_align.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace diffy;

namespace {

// Deterministic pseudo-random bytes (LCG) — no Math.random, reproducible.
std::vector<uint8_t>
gen(size_t n, uint32_t seed) {
    std::vector<uint8_t> out;
    out.reserve(n);
    uint32_t x = seed;
    for (size_t i = 0; i < n; ++i) {
        x = 1103515245u * x + 12345u;
        out.push_back(static_cast<uint8_t>((x >> 16) & 0xff));
    }
    return out;
}

std::vector<uint8_t>
bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// Assert the alignment covers both files contiguously and exactly, and that each
// segment's byte content is consistent with its kind.
void
check_well_formed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const HexAlignment& al) {
    uint64_t ai = 0, bi = 0;
    for (const HexSegment& s : al) {
        if (s.a_len > 0) {
            CHECK(s.a_offset == ai);
            ai += s.a_len;
        }
        if (s.b_len > 0) {
            CHECK(s.b_offset == bi);
            bi += s.b_len;
        }
        switch (s.kind) {
            case HexSegKind::Equal:
                REQUIRE(s.a_len == s.b_len);
                for (uint64_t k = 0; k < s.a_len; ++k) {
                    CHECK(a[s.a_offset + k] == b[s.b_offset + k]);
                }
                break;
            case HexSegKind::Replace:
                REQUIRE(s.a_len == s.b_len);
                break;
            case HexSegKind::OnlyA:
                CHECK(s.b_len == 0);
                break;
            case HexSegKind::OnlyB:
                CHECK(s.a_len == 0);
                break;
        }
    }
    CHECK(ai == a.size());
    CHECK(bi == b.size());
}

uint64_t
equal_bytes(const HexAlignment& al) {
    uint64_t n = 0;
    for (const HexSegment& s : al) {
        if (s.kind == HexSegKind::Equal) {
            n += s.a_len;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("chunker is deterministic and covers the whole buffer") {
    const auto data = gen(50000, 99);
    const auto c1 = chunk_bytes(data.data(), data.size());
    const auto c2 = chunk_bytes(data.data(), data.size());

    REQUIRE(c1.size() == c2.size());
    REQUIRE(c1.size() > 1);  // multiple chunks for a 50 KB buffer

    uint64_t covered = 0;
    for (size_t i = 0; i < c1.size(); ++i) {
        CHECK(c1[i].offset == c2[i].offset);
        CHECK(c1[i].length == c2[i].length);
        CHECK(c1[i].checksum == c2[i].checksum);
        CHECK(c1[i].offset == covered);  // contiguous
        covered += c1[i].length;
    }
    CHECK(covered == data.size());
}

TEST_CASE("chunker re-synchronises after an insertion (anti-cascade)") {
    const auto base = gen(50000, 7);
    auto shifted = base;
    // Insert 3 bytes near the start.
    shifted.insert(shifted.begin() + 500, {0xDE, 0xAD, 0xBE});

    const auto ca = chunk_bytes(base.data(), base.size());
    const auto cb = chunk_bytes(shifted.data(), shifted.size());

    // The vast majority of chunk checksums must be shared: only the chunk(s)
    // around the insertion should differ, not everything after it.
    std::vector<uint32_t> hb;
    for (const auto& c : cb) {
        hb.push_back(c.checksum);
    }
    size_t shared = 0;
    for (const auto& c : ca) {
        for (uint32_t h : hb) {
            if (h == c.checksum) {
                ++shared;
                break;
            }
        }
    }
    CHECK(shared >= ca.size() - 3);
}

TEST_CASE("needleman_wunsch aligns an insertion without cascading") {
    const auto a = bytes("hello");
    const auto b = bytes("heXYllo");
    const auto ops = needleman_wunsch_bytes(a.data(), a.size(), b.data(), b.size());

    size_t eq = 0, ins = 0, del = 0, rep = 0;
    for (AlignOp op : ops) {
        eq += op == AlignOp::Equal;
        ins += op == AlignOp::InsertB;
        del += op == AlignOp::DeleteA;
        rep += op == AlignOp::Replace;
    }
    CHECK(eq == 5);   // h e l l o all matched
    CHECK(ins == 2);  // X Y inserted
    CHECK(del == 0);
    CHECK(rep == 0);
}

TEST_CASE("hex_align: identical files are one Equal segment") {
    const auto a = gen(10000, 3);
    bool truncated = false;
    const auto al = hex_align(a, a, {}, &truncated);
    check_well_formed(a, a, al);
    CHECK_FALSE(truncated);
    for (const auto& s : al) {
        CHECK(s.kind == HexSegKind::Equal);
    }
}

TEST_CASE("hex_align: small files use the byte-precise global path") {
    const auto a = bytes("The quick brown fox");
    const auto b = bytes("The quick red fox");
    bool truncated = false;
    const auto al = hex_align(a, b, {}, &truncated);
    check_well_formed(a, b, al);
    CHECK_FALSE(truncated);
    // "The quick " and " fox" survive as equal context.
    CHECK(equal_bytes(al) >= 14);
}

TEST_CASE("hex_align: large-file insertion collapses to a tiny change") {
    const auto a = gen(200000, 21);
    auto b = a;
    b.insert(b.begin() + 1234, {0x01, 0x02, 0x03, 0x04});

    bool truncated = false;
    const auto al = hex_align(a, b, {}, &truncated);
    check_well_formed(a, b, al);
    CHECK_FALSE(truncated);

    // Almost everything is equal; the change is ~4 bytes, not a cascade.
    CHECK(equal_bytes(al) >= a.size() - 64);

    uint64_t only_b = 0;
    for (const auto& s : al) {
        if (s.kind == HexSegKind::OnlyB) {
            only_b += s.b_len;
        }
    }
    CHECK(only_b >= 4);
    CHECK(only_b <= 64);
}

TEST_CASE("hex_align: --hex-global forces whole-file byte alignment") {
    const auto a = bytes("abcdefghij");
    const auto b = bytes("abXdefghij");
    HexAlignParams p;
    p.force_global = true;
    bool truncated = false;
    const auto al = hex_align(a, b, p, &truncated);
    check_well_formed(a, b, al);
    // One byte replaced, the rest equal.
    CHECK(equal_bytes(al) == 9);
}
