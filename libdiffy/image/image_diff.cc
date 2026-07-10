#include "image/image_diff.hpp"

#include <algorithm>
#include <cmath>

// A C++ port of pixelmatch (https://github.com/mapbox/pixelmatch), ISC-licensed:
// YIQ perceptual colour distance with anti-alias detection so AA fringes aren't
// flagged as changes. Produces a changed-pixel count and an optional overlay.

namespace diffy {

namespace {

inline double
rgb2y(double r, double g, double b) {
    return r * 0.29889531 + g * 0.58662247 + b * 0.11448223;
}
inline double
rgb2i(double r, double g, double b) {
    return r * 0.59597799 - g * 0.27417610 - b * 0.32180189;
}
inline double
rgb2q(double r, double g, double b) {
    return r * 0.21147017 - g * 0.52261711 + b * 0.31114694;
}

// Blend a colour channel `c` with white by opacity `a` (0..1).
inline double
blend(double c, double a) {
    return 255.0 + (c - 255.0) * a;
}

// Squared YIQ colour delta between pixel `k` of `a` and `m` of `b`. Signed by
// which pixel is brighter (used by the AA detector); callers compare |delta|.
// When `y_only`, returns just the brightness difference.
double
color_delta(const uint8_t* a, const uint8_t* b, size_t k, size_t m, bool y_only) {
    double r1 = a[k], g1 = a[k + 1], b1 = a[k + 2], a1 = a[k + 3];
    double r2 = b[m], g2 = b[m + 1], b2 = b[m + 2], a2 = b[m + 3];

    if (a1 == a2 && r1 == r2 && g1 == g2 && b1 == b2) {
        return 0.0;
    }
    if (a1 < 255.0) {
        a1 /= 255.0;
        r1 = blend(r1, a1);
        g1 = blend(g1, a1);
        b1 = blend(b1, a1);
    }
    if (a2 < 255.0) {
        a2 /= 255.0;
        r2 = blend(r2, a2);
        g2 = blend(g2, a2);
        b2 = blend(b2, a2);
    }
    const double y1 = rgb2y(r1, g1, b1), y2 = rgb2y(r2, g2, b2);
    const double y = y1 - y2;
    if (y_only) {
        return y;
    }
    const double i = rgb2i(r1, g1, b1) - rgb2i(r2, g2, b2);
    const double q = rgb2q(r1, g1, b1) - rgb2q(r2, g2, b2);
    const double delta = 0.5053 * y * y + 0.299 * i * i + 0.1957 * q * q;
    return y1 > y2 ? -delta : delta;
}

// True if pixel (x1,y1) has >=3 neighbours identical to it (a flat region edge).
bool
has_many_siblings(const uint8_t* img, int x1, int y1, int width, int height) {
    const int x0 = std::max(x1 - 1, 0), y0 = std::max(y1 - 1, 0);
    const int x2 = std::min(x1 + 1, width - 1), y2 = std::min(y1 + 1, height - 1);
    const size_t pos = (static_cast<size_t>(y1) * width + x1) * 4;
    int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;
    for (int x = x0; x <= x2; ++x) {
        for (int y = y0; y <= y2; ++y) {
            if (x == x1 && y == y1) {
                continue;
            }
            const size_t pos2 = (static_cast<size_t>(y) * width + x) * 4;
            if (img[pos] == img[pos2] && img[pos + 1] == img[pos2 + 1] &&
                img[pos + 2] == img[pos2 + 2] && img[pos + 3] == img[pos2 + 3]) {
                if (++zeroes > 2) {
                    return true;
                }
            }
        }
    }
    return false;
}

// True if pixel (x1,y1) looks anti-aliased (many equal-brightness siblings and a
// strongest darker/lighter sibling that sits in a flat region in both images).
bool
antialiased(const uint8_t* img, int x1, int y1, int width, int height, const uint8_t* other) {
    const int x0 = std::max(x1 - 1, 0), y0 = std::max(y1 - 1, 0);
    const int x2 = std::min(x1 + 1, width - 1), y2 = std::min(y1 + 1, height - 1);
    const size_t pos = (static_cast<size_t>(y1) * width + x1) * 4;
    int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;
    double mn = 0.0, mx = 0.0;
    int min_x = -1, min_y = -1, max_x = -1, max_y = -1;

    for (int x = x0; x <= x2; ++x) {
        for (int y = y0; y <= y2; ++y) {
            if (x == x1 && y == y1) {
                continue;
            }
            const double delta = color_delta(img, img, pos, (static_cast<size_t>(y) * width + x) * 4, true);
            if (delta == 0.0) {
                if (++zeroes > 2) {
                    return false;
                }
            } else if (delta < mn) {
                mn = delta;
                min_x = x;
                min_y = y;
            } else if (delta > mx) {
                mx = delta;
                max_x = x;
                max_y = y;
            }
        }
    }
    if (mn == 0.0 || mx == 0.0) {
        return false;
    }
    return (has_many_siblings(img, min_x, min_y, width, height) &&
            has_many_siblings(other, min_x, min_y, width, height)) ||
           (has_many_siblings(img, max_x, max_y, width, height) &&
            has_many_siblings(other, max_x, max_y, width, height));
}

void
draw(std::vector<uint8_t>& out, size_t pos, uint8_t r, uint8_t g, uint8_t b) {
    out[pos] = r;
    out[pos + 1] = g;
    out[pos + 2] = b;
    out[pos + 3] = 255;
}

}  // namespace

ImageDiffResult
image_diff(const std::vector<uint8_t>& a_rgba, const std::vector<uint8_t>& b_rgba, int width, int height,
           const ImageDiffOptions& opts) {
    ImageDiffResult res;
    const size_t need = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (width <= 0 || height <= 0 || a_rgba.size() < need || b_rgba.size() < need) {
        return res;  // not comparable
    }
    res.comparable = true;
    res.width = width;
    res.height = height;
    res.total_px = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);

    const uint8_t* a = a_rgba.data();
    const uint8_t* b = b_rgba.data();
    if (opts.compute_overlay) {
        res.overlay_rgba.assign(need, 0);
    }

    const double max_delta = 35215.0 * opts.threshold * opts.threshold;
    constexpr double kDimAlpha = 0.1;  // how faintly the unchanged base shows

    // Identical buffers: no changed pixels, so skip the per-pixel color-delta and
    // antialiasing work entirely. The overlay (if requested) is the all-dimmed
    // base — the same output the unchanged branch below produces for every pixel.
    if (std::memcmp(a, b, need) == 0) {
        if (opts.compute_overlay) {
            for (size_t pos = 0; pos < need; pos += 4) {
                const uint8_t v = static_cast<uint8_t>(std::lround(
                    blend(rgb2y(a[pos], a[pos + 1], a[pos + 2]), kDimAlpha * a[pos + 3] / 255.0)));
                draw(res.overlay_rgba, pos, v, v, v);
            }
        }
        res.similarity = 1.0;
        return res;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pos = (static_cast<size_t>(y) * width + x) * 4;
            const double delta = color_delta(a, b, pos, pos, false);
            if (std::fabs(delta) > max_delta) {
                const bool aa = !opts.include_aa &&
                                (antialiased(a, x, y, width, height, b) ||
                                 antialiased(b, x, y, width, height, a));
                if (aa) {
                    // Anti-aliasing difference: not counted as a real change, but
                    // tinted yellow so it's visible and distinct from magenta changes
                    // (matches upstream pixelmatch's aaColor). --include-aa promotes
                    // these to real changes instead (aa is false there).
                    if (opts.compute_overlay) {
                        draw(res.overlay_rgba, pos, 255, 255, 0);  // yellow
                    }
                } else {
                    ++res.changed_px;
                    if (opts.compute_overlay) {
                        draw(res.overlay_rgba, pos, 255, 0, 255);  // magenta
                    }
                }
            } else if (opts.compute_overlay) {
                const uint8_t v = static_cast<uint8_t>(std::lround(
                    blend(rgb2y(a[pos], a[pos + 1], a[pos + 2]), kDimAlpha * a[pos + 3] / 255.0)));
                draw(res.overlay_rgba, pos, v, v, v);
            }
        }
    }

    res.similarity = res.total_px ? 1.0 - static_cast<double>(res.changed_px) / res.total_px : 1.0;
    return res;
}

}  // namespace diffy
