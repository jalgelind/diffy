#include "image/term_image.hpp"

#include <algorithm>

#include <fmt/format.h>

// PNG encoding for the iTerm2 payload (single-header, public domain).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace diffy {

TermImageProtocol
detect_term_image_protocol(const TermEnv& env) {
    if (env.disabled) {
        return TermImageProtocol::None;
    }
    // Crisp protocols only when we're really on that terminal (a query handshake
    // would be more robust, but env detection covers the common terminals).
    if (env.is_tty) {
        if (!env.kitty_window_id.empty() || env.term.find("kitty") != std::string::npos) {
            return TermImageProtocol::Kitty;
        }
        if (env.term_program == "iTerm.app" || env.term_program == "WezTerm") {
            return TermImageProtocol::ITerm2;
        }
    }
    if (env.force || env.is_tty) {
        return TermImageProtocol::HalfBlock;  // universal truecolor fallback
    }
    return TermImageProtocol::None;
}

namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string
base64(const uint8_t* data, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        const uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kB64[(v >> 18) & 0x3f]);
        out.push_back(kB64[(v >> 12) & 0x3f]);
        out.push_back(kB64[(v >> 6) & 0x3f]);
        out.push_back(kB64[v & 0x3f]);
    }
    if (i < n) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < n) ? data[i + 1] : 0;
        const uint32_t v = (b0 << 16) | (b1 << 8);
        out.push_back(kB64[(v >> 18) & 0x3f]);
        out.push_back(kB64[(v >> 12) & 0x3f]);
        out.push_back((i + 1 < n) ? kB64[(v >> 6) & 0x3f] : '=');
        out.push_back('=');
    }
    return out;
}

// Box-average downscale of an RGBA8 image to tw x th. (Upscaling isn't needed —
// callers only ever shrink to fit the terminal.)
std::vector<uint8_t>
box_downscale(const std::vector<uint8_t>& src, int w, int h, int tw, int th) {
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th * 4);
    for (int ty = 0; ty < th; ++ty) {
        const int sy0 = static_cast<int>(static_cast<int64_t>(ty) * h / th);
        const int sy1 = std::max(sy0 + 1, static_cast<int>(static_cast<int64_t>(ty + 1) * h / th));
        for (int tx = 0; tx < tw; ++tx) {
            const int sx0 = static_cast<int>(static_cast<int64_t>(tx) * w / tw);
            const int sx1 = std::max(sx0 + 1, static_cast<int>(static_cast<int64_t>(tx + 1) * w / tw));
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < h; ++sy) {
                for (int sx = sx0; sx < sx1 && sx < w; ++sx) {
                    const size_t p = (static_cast<size_t>(sy) * w + sx) * 4;
                    r += src[p];
                    g += src[p + 1];
                    b += src[p + 2];
                    a += src[p + 3];
                    ++n;
                }
            }
            if (n == 0) {
                n = 1;
            }
            const size_t o = (static_cast<size_t>(ty) * tw + tx) * 4;
            out[o] = static_cast<uint8_t>(r / n);
            out[o + 1] = static_cast<uint8_t>(g / n);
            out[o + 2] = static_cast<uint8_t>(b / n);
            out[o + 3] = static_cast<uint8_t>(a / n);
        }
    }
    return out;
}

// Composite a pixel over a mid-grey checkerboard-free flat background so alpha
// doesn't render as black.
void
flatten(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t a) {
    if (a == 255) {
        return;
    }
    const int bg = 30;  // dark backdrop
    r = static_cast<uint8_t>((r * a + bg * (255 - a)) / 255);
    g = static_cast<uint8_t>((g * a + bg * (255 - a)) / 255);
    b = static_cast<uint8_t>((b * a + bg * (255 - a)) / 255);
}

}  // namespace

std::string
render_halfblock(const std::vector<uint8_t>& rgba, int w, int h, int max_cols, int max_rows) {
    if (w <= 0 || h <= 0 || max_cols <= 0 || max_rows <= 0 ||
        rgba.size() < static_cast<size_t>(w) * h * 4) {
        return "";
    }
    // One cell = 1 px wide, 2 px tall. Fit within max_cols x (2*max_rows) pixels,
    // preserving aspect.
    const double sx = static_cast<double>(max_cols) / w;
    const double sy = static_cast<double>(2 * max_rows) / h;
    const double scale = std::min({sx, sy, 1.0});
    int tw = std::max(1, static_cast<int>(w * scale));
    int th = std::max(2, static_cast<int>(h * scale));
    if (th % 2 != 0) {
        ++th;  // even, so every cell has an upper and lower pixel
    }

    const std::vector<uint8_t> img = box_downscale(rgba, w, h, tw, th);
    auto at = [&](int x, int y, uint8_t& r, uint8_t& g, uint8_t& b) {
        const size_t p = (static_cast<size_t>(y) * tw + x) * 4;
        r = img[p];
        g = img[p + 1];
        b = img[p + 2];
        flatten(r, g, b, img[p + 3]);
    };

    std::string out;
    out.reserve(static_cast<size_t>(tw) * (th / 2) * 40);
    for (int cy = 0; cy < th / 2; ++cy) {
        for (int x = 0; x < tw; ++x) {
            uint8_t tr, tg, tb, br, bg, bb;
            at(x, cy * 2, tr, tg, tb);
            at(x, cy * 2 + 1, br, bg, bb);
            fmt::format_to(std::back_inserter(out), "\033[38;2;{};{};{};48;2;{};{};{}m▀", tr, tg, tb,
                           br, bg, bb);
        }
        out += "\033[0m\n";
    }
    return out;
}

namespace {

// Downscale so the longest side is <= max_dim, to bound transmitted data (the
// terminal scales to `c` cells for display regardless).
std::vector<uint8_t>
bound_downscale(const std::vector<uint8_t>& rgba, int w, int h, int max_dim, int& tw, int& th) {
    const int longest = std::max(w, h);
    if (longest <= max_dim) {
        tw = w;
        th = h;
        return rgba;
    }
    const double s = static_cast<double>(max_dim) / longest;
    tw = std::max(1, static_cast<int>(w * s));
    th = std::max(1, static_cast<int>(h * s));
    return box_downscale(rgba, w, h, tw, th);
}

void
png_collect(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    const uint8_t* p = static_cast<const uint8_t*>(data);
    v->insert(v->end(), p, p + size);
}

std::string
render_kitty(const std::vector<uint8_t>& rgba, int w, int h, int max_cols) {
    int tw = 0, th = 0;
    const std::vector<uint8_t> img = bound_downscale(rgba, w, h, 1024, tw, th);
    const std::string b64 = base64(img.data(), static_cast<size_t>(tw) * th * 4);
    const int cols = std::min(max_cols, 120);

    std::string out;
    const size_t chunk = 4096;
    bool first = true;
    for (size_t i = 0; i < b64.size(); i += chunk) {
        const bool more = i + chunk < b64.size();
        const std::string piece = b64.substr(i, chunk);
        if (first) {
            fmt::format_to(std::back_inserter(out), "\033_Ga=T,f=32,s={},v={},c={},m={};{}\033\\", tw, th,
                           cols, more ? 1 : 0, piece);
            first = false;
        } else {
            fmt::format_to(std::back_inserter(out), "\033_Gm={};{}\033\\", more ? 1 : 0, piece);
        }
    }
    out += "\n";
    return out;
}

std::string
render_iterm2(const std::vector<uint8_t>& rgba, int w, int h, int max_cols) {
    int tw = 0, th = 0;
    const std::vector<uint8_t> img = bound_downscale(rgba, w, h, 1024, tw, th);
    std::vector<uint8_t> png;
    if (!stbi_write_png_to_func(png_collect, &png, tw, th, 4, img.data(), tw * 4) || png.empty()) {
        return "";
    }
    const std::string b64 = base64(png.data(), png.size());
    const int cols = std::min(max_cols, 120);
    std::string out;
    fmt::format_to(std::back_inserter(out),
                   "\033]1337;File=inline=1;width={};preserveAspectRatio=1;size={}:{}\a\n", cols,
                   png.size(), b64);
    return out;
}

}  // namespace

std::string
render_term_image(TermImageProtocol protocol, const std::vector<uint8_t>& rgba, int w, int h,
                  int max_cols, int max_rows) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) {
        return "";
    }
    switch (protocol) {
        case TermImageProtocol::HalfBlock:
            return render_halfblock(rgba, w, h, max_cols, max_rows);
        case TermImageProtocol::Kitty:
            return render_kitty(rgba, w, h, max_cols);
        case TermImageProtocol::ITerm2:
            return render_iterm2(rgba, w, h, max_cols);
        case TermImageProtocol::None:
            break;
    }
    return "";
}

}  // namespace diffy
