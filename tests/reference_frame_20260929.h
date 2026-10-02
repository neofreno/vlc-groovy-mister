// Frozen pre-optimization converter, 2026-09-29. Test/benchmark oracle only.
#pragma once

// CPU-only helpers, independent of VLC/Win32 so boundary cases can be tested.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace video_reference {
constexpr size_t api_capacity = 720u * 576u * 4u;
constexpr size_t frame_capacity = api_capacity * 2u; // Full image before extracting a field.

enum class Matrix { bt601, bt709, bt2020 };
struct Plane {
    const uint8_t* pixels = nullptr;
    int pitch = 0;
    int lines = 0;
};
struct I420View {
    Plane planes[3];
    unsigned coded_width = 0, coded_height = 0;
    unsigned x = 0, y = 0, width = 0, height = 0;
    unsigned sar_num = 1, sar_den = 1;
    Matrix matrix = Matrix::bt601;
    bool full_range = false;
};

inline bool frameSize(unsigned width, unsigned height, bool interlace, size_t& bytes)
{
    const uint64_t size = uint64_t(width) * height * 3;
    if (!width || !height || width > 65535 || height > 65535 ||
        (interlace && (height & 1)) || size > frame_capacity ||
        size / (interlace ? 2 : 1) > api_capacity)
        return false;
    bytes = size_t(size);
    return true;
}

inline bool valid(const I420View& src)
{
    if (!src.width || !src.height || !src.coded_width || !src.coded_height ||
        uint64_t(src.x) + src.width > src.coded_width ||
        uint64_t(src.y) + src.height > src.coded_height)
        return false;
    for (unsigned p = 0; p < 3; ++p) {
        const uint64_t end_x = (uint64_t(src.x) + src.width + (p ? 1 : 0)) / (p ? 2 : 1);
        const uint64_t end_y = (uint64_t(src.y) + src.height + (p ? 1 : 0)) / (p ? 2 : 1);
        if (!src.planes[p].pixels || src.planes[p].pitch <= 0 || src.planes[p].lines <= 0 ||
            end_x > unsigned(src.planes[p].pitch) || end_y > unsigned(src.planes[p].lines))
            return false;
    }
    return true;
}

inline uint8_t channel(int64_t value)
{
    const int64_t rounded = (value + 8192) / 16384;
    return uint8_t((std::max)(int64_t(0), (std::min)(int64_t(255), rounded)));
}

struct DrawSize { unsigned width, height; };

// The active raster fills a physical 4:3 CRT, regardless of its pixel count.
// Missing SAR means square source pixels. Do not infer output DAR from timings.
inline DrawSize fitAspect(unsigned source_width, unsigned source_height,
                          unsigned sar_num, unsigned sar_den,
                          unsigned width, unsigned height, bool keep_aspect)
{
    DrawSize result{ width, height };
    if (!keep_aspect || !source_width || !source_height || !width || !height)
        return result;
    if (!sar_num || !sar_den) sar_num = sar_den = 1;
    const double relative = (double(source_width) / source_height) *
        (double(sar_num) / sar_den) / (4.0 / 3.0);
    if (relative > 1.0)
        result.height = (std::max)(1u, unsigned(std::lround(height / relative)));
    else
        result.width = (std::max)(1u, unsigned(std::lround(width * relative)));
    return result;
}

// Groovy's RGB888 wire buffer is little-endian 0xRRGGBB: byte order B, G, R.
inline int areaSample(const Plane& plane, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    uint64_t total = 0;
    for (unsigned y=y0; y<y1; ++y) {
        const uint8_t* row = plane.pixels + size_t(y)*plane.pitch;
        for (unsigned x=x0; x<x1; ++x) total += row[x];
    }
    const uint64_t count = uint64_t(x1-x0)*(y1-y0);
    return int((total + count/2)/count);
}

inline bool convert(const I420View& src, uint8_t* dst, size_t capacity,
                    unsigned width, unsigned height, bool keep_aspect, bool smooth = false)
{
    const uint64_t bytes = uint64_t(width) * height * 3;
    if (!valid(src) || !dst || !width || !height || bytes > capacity || bytes > frame_capacity)
        return false;

    const DrawSize draw = fitAspect(src.width, src.height, src.sar_num, src.sar_den,
        width, height, keep_aspect);
    const unsigned draw_w = draw.width, draw_h = draw.height;
    const bool reduce = smooth && src.width >= draw_w && src.height >= draw_h &&
        (src.width > draw_w || src.height > draw_h);
    const unsigned left = (width - draw_w) / 2, top = (height - draw_h) / 2;
    if (draw_w != width || draw_h != height)
        std::memset(dst, 0, size_t(bytes));

    const double kr = src.matrix == Matrix::bt709 ? 0.2126 : src.matrix == Matrix::bt2020 ? 0.2627 : 0.299;
    const double kb = src.matrix == Matrix::bt709 ? 0.0722 : src.matrix == Matrix::bt2020 ? 0.0593 : 0.114;
    const double kg = 1.0 - kr - kb;
    const double chroma_scale = src.full_range ? 1.0 : 255.0 / 224.0;
    const int cy = int(std::lround(16384.0 * (src.full_range ? 1.0 : 255.0 / 219.0)));
    const int cr = int(std::lround(16384.0 * 2.0 * (1.0 - kr) * chroma_scale));
    const int cb = int(std::lround(16384.0 * 2.0 * (1.0 - kb) * chroma_scale));
    const int cgu = int(std::lround(16384.0 * 2.0 * kb * (1.0 - kb) / kg * chroma_scale));
    const int cgv = int(std::lround(16384.0 * 2.0 * kr * (1.0 - kr) / kg * chroma_scale));
    const unsigned step_x = src.width / draw_w, remainder_x = src.width % draw_w;
    for (unsigned y = 0; y < draw_h; ++y) {
        const unsigned sy = src.y + unsigned(uint64_t(y) * src.height / draw_h);
        const uint8_t* py = src.planes[0].pixels + size_t(sy) * src.planes[0].pitch;
        const uint8_t* pu = src.planes[1].pixels + size_t(sy / 2) * src.planes[1].pitch;
        const uint8_t* pv = src.planes[2].pixels + size_t(sy / 2) * src.planes[2].pitch;
        uint8_t* out = dst + (size_t(y + top) * width + left) * 3;
        unsigned sx = src.x, error_x = 0;
        for (unsigned x = 0; x < draw_w; ++x) {
            int yy = py[sx], uu = pu[sx/2], vv = pv[sx/2];
            if (reduce) {
                const unsigned ex = src.x + unsigned(uint64_t(x+1)*src.width/draw_w);
                const unsigned ey = src.y + unsigned(uint64_t(y+1)*src.height/draw_h);
                yy = areaSample(src.planes[0], sx, sy, ex, ey);
                uu = areaSample(src.planes[1], sx/2, sy/2, (ex+1)/2, (ey+1)/2);
                vv = areaSample(src.planes[2], sx/2, sy/2, (ex+1)/2, (ey+1)/2);
            }
            const int64_t luminance = int64_t(yy - (src.full_range ? 0 : 16)) * cy;
            const int u = uu - 128, v = vv - 128;
            out[x * 3] = channel(luminance + int64_t(cb) * u);
            out[x * 3 + 1] = channel(luminance - int64_t(cgu) * u - int64_t(cgv) * v);
            out[x * 3 + 2] = channel(luminance + int64_t(cr) * v);
            sx += step_x;
            error_x += remainder_x;
            if (error_x >= draw_w) { ++sx; error_x -= draw_w; }
        }
    }
    return true;
}

inline bool copyForSend(uint8_t* dst, size_t capacity, const uint8_t* src,
                        size_t bytes, unsigned width, unsigned height, bool interlace, unsigned field)
{
    size_t expected = 0;
    if (!dst || !src || field > 1 || (!interlace && field != 0) ||
        !frameSize(width, height, interlace, expected) || bytes != expected ||
        expected / (interlace ? 2 : 1) > capacity)
        return false;
    const size_t row_bytes = size_t(width) * 3;
    if (!interlace) std::memcpy(dst, src, bytes);
    else for (unsigned row = 0; row < height / 2; ++row)
        std::memcpy(dst + row * row_bytes, src + (row * 2 + field) * row_bytes, row_bytes);
    return true;
}
}
