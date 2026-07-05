#include "binary/hex_align.hpp"

#include "algorithms/algorithm.hpp"
#include "algorithms/needleman_wunsch.hpp"
#include "algorithms/patience.hpp"

#include <cstring>
#include <gsl/span>

namespace diffy {

namespace {

void
push_merge(HexAlignment& out, HexSegKind kind, uint64_t ao, uint64_t al, uint64_t bo, uint64_t bl) {
    if (!out.empty() && out.back().kind == kind) {
        out.back().a_len += al;
        out.back().b_len += bl;
        return;
    }
    out.push_back({kind, ao, al, bo, bl});
}

// Byte-align two regions with Needleman-Wunsch and append the coalesced result.
void
append_byte_aligned(const uint8_t* a, uint64_t a_off, size_t na, const uint8_t* b, uint64_t b_off, size_t nb,
                    HexAlignment& out) {
    const auto ops = needleman_wunsch_bytes(a, na, b, nb);
    uint64_t ac = a_off, bc = b_off;
    for (const AlignOp op : ops) {
        switch (op) {
            case AlignOp::Equal:
                push_merge(out, HexSegKind::Equal, ac, 1, bc, 1);
                ++ac;
                ++bc;
                break;
            case AlignOp::Replace:
                push_merge(out, HexSegKind::Replace, ac, 1, bc, 1);
                ++ac;
                ++bc;
                break;
            case AlignOp::DeleteA:
                push_merge(out, HexSegKind::OnlyA, ac, 1, bc, 0);
                ++ac;
                break;
            case AlignOp::InsertB:
                push_merge(out, HexSegKind::OnlyB, ac, 0, bc, 1);
                ++bc;
                break;
        }
    }
}

}  // namespace

HexAlignment
hex_align(gsl::span<const uint8_t> a, gsl::span<const uint8_t> b, const HexAlignParams& params,
          bool* truncated) {
    if (truncated) {
        *truncated = false;
    }

    HexAlignment out;

    // Whole-file byte alignment: opt-in, or automatic for files small enough that
    // the O(n*m) aligner is cheap. Gives the tightest possible alignment.
    if (params.force_global || (a.size() <= params.byte_cap && b.size() <= params.byte_cap)) {
        append_byte_aligned(a.data(), 0, a.size(), b.data(), 0, b.size(), out);
        return out;
    }

    // Coarse pass: chunk both files and align the chunk sequences with the
    // existing line-diff engine (patience over Chunk units).
    auto ca = chunk_bytes(a.data(), a.size(), params.chunk);
    auto cb = chunk_bytes(b.data(), b.size(), params.chunk);

    gsl::span<Chunk> a_span{ca};
    gsl::span<Chunk> b_span{cb};
    DiffInput<Chunk> in{a_span, b_span, "A", "B"};
    DiffResult res = Patience<Chunk>(in).compute();

    // Identical (some algorithms return an empty edit sequence for no changes).
    if (res.edit_sequence.empty()) {
        if (!a.empty() || !b.empty()) {
            push_merge(out, HexSegKind::Equal, 0, a.size(), 0, b.size());
        }
        return out;
    }

    // Walk chunk edits into raw segments. Cursors track the cross-side byte
    // position for deletions/insertions (which only carry one index).
    HexAlignment coarse;
    uint64_t a_cur = 0, b_cur = 0;
    for (const Edit& e : res.edit_sequence) {
        if (e.type == EditType::Common) {
            const Chunk& ac = ca[e.a_index];
            const Chunk& bc = cb[e.b_index];
            const bool equal = ac.length == bc.length &&
                               std::memcmp(a.data() + ac.offset, b.data() + bc.offset, ac.length) == 0;
            if (equal) {
                push_merge(coarse, HexSegKind::Equal, ac.offset, ac.length, bc.offset, bc.length);
            } else {
                // Checksum collision: the chunks aren't really equal. Emit as a
                // removed+added region so refinement byte-aligns it.
                push_merge(coarse, HexSegKind::OnlyA, ac.offset, ac.length, bc.offset, 0);
                push_merge(coarse, HexSegKind::OnlyB, ac.offset + ac.length, 0, bc.offset, bc.length);
            }
            a_cur = ac.offset + ac.length;
            b_cur = bc.offset + bc.length;
        } else if (e.type == EditType::Delete) {
            const Chunk& ac = ca[e.a_index];
            push_merge(coarse, HexSegKind::OnlyA, ac.offset, ac.length, b_cur, 0);
            a_cur = ac.offset + ac.length;
        } else if (e.type == EditType::Insert) {
            const Chunk& bc = cb[e.b_index];
            push_merge(coarse, HexSegKind::OnlyB, a_cur, 0, bc.offset, bc.length);
            b_cur = bc.offset + bc.length;
        }
    }

    // Fine pass: refine each changed region (a maximal run of OnlyA/OnlyB) with
    // byte-level alignment when its DP table fits our budget; otherwise keep it
    // coarse. The budget is a cell count (so a tall/thin region — a big deletion
    // against a few added bytes — still refines) plus a per-side ceiling so the
    // (na+1)*(nb+1) table can't blow up. Both derive from byte_cap, so a square
    // region up to byte_cap x byte_cap still refines, as before.
    const uint64_t cap = params.byte_cap;
    const uint64_t max_cells = cap * cap;
    const uint64_t hard_side = cap * 16;
    size_t i = 0;
    while (i < coarse.size()) {
        const HexSegKind k = coarse[i].kind;
        if (k == HexSegKind::Equal || k == HexSegKind::Replace) {
            push_merge(out, k, coarse[i].a_offset, coarse[i].a_len, coarse[i].b_offset, coarse[i].b_len);
            ++i;
            continue;
        }

        // Gather the changed run.
        size_t j = i;
        const uint64_t a_start = coarse[i].a_offset;
        const uint64_t b_start = coarse[i].b_offset;
        uint64_t a_len = 0, b_len = 0;
        while (j < coarse.size() &&
               (coarse[j].kind == HexSegKind::OnlyA || coarse[j].kind == HexSegKind::OnlyB)) {
            a_len += coarse[j].a_len;
            b_len += coarse[j].b_len;
            ++j;
        }

        if (a_len > 0 && b_len > 0) {
            // Trim the common prefix and suffix of the changed region. Content-
            // defined chunking re-synchronises a byte or two late after a length
            // change, so a shifted insertion/deletion surfaces as a big region
            // whose bytes are actually identical apart from the true edit. Trimming
            // collapses it to just the differing core (and keeps that core within
            // the byte aligner's cap).
            uint64_t as = a_start, bs = b_start, al = a_len, bl = b_len;
            uint64_t pre = 0;
            while (pre < al && pre < bl && a[as + pre] == b[bs + pre]) {
                ++pre;
            }
            if (pre > 0) {
                push_merge(out, HexSegKind::Equal, as, pre, bs, pre);
                as += pre;
                bs += pre;
                al -= pre;
                bl -= pre;
            }
            uint64_t suf = 0;
            while (suf < al && suf < bl && a[as + al - 1 - suf] == b[bs + bl - 1 - suf]) {
                ++suf;
            }
            const uint64_t core_al = al - suf;
            const uint64_t core_bl = bl - suf;

            if (core_al > 0 && core_bl > 0) {
                const bool fits = core_al <= hard_side && core_bl <= hard_side &&
                                  core_al <= max_cells / core_bl;  // core_bl > 0 here
                if (fits) {
                    append_byte_aligned(a.data() + as, as, static_cast<size_t>(core_al), b.data() + bs, bs,
                                        static_cast<size_t>(core_bl), out);
                } else {
                    push_merge(out, HexSegKind::OnlyA, as, core_al, bs, 0);
                    push_merge(out, HexSegKind::OnlyB, as + core_al, 0, bs, core_bl);
                    if (truncated) {
                        *truncated = true;
                    }
                }
            } else if (core_al > 0) {
                push_merge(out, HexSegKind::OnlyA, as, core_al, bs, 0);
            } else if (core_bl > 0) {
                push_merge(out, HexSegKind::OnlyB, as, 0, bs, core_bl);
            }

            if (suf > 0) {
                push_merge(out, HexSegKind::Equal, as + core_al, suf, bs + core_bl, suf);
            }
        } else {
            // Pure deletion or insertion; already minimal.
            for (size_t k2 = i; k2 < j; ++k2) {
                push_merge(out, coarse[k2].kind, coarse[k2].a_offset, coarse[k2].a_len, coarse[k2].b_offset,
                           coarse[k2].b_len);
            }
        }
        i = j;
    }

    return out;
}

}  // namespace diffy
