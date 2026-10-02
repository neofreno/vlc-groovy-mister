#include "../src/video_frame.h"
#include "reference_frame_20260929.h"
#include <array>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static uint32_t randomState = 0x817293u;
static uint32_t randomValue() {
    randomState ^= randomState << 13; randomState ^= randomState >> 17; randomState ^= randomState << 5;
    return randomState;
}
struct Fixture {
    video_stream::I420View current;
    video_reference::I420View reference;
    std::array<std::vector<uint8_t>, 3> planes;
    Fixture(unsigned w, unsigned h, unsigned crop = 0, unsigned padding = 7) {
        current.coded_width = reference.coded_width = w + crop;
        current.coded_height = reference.coded_height = h + crop;
        current.x = current.y = reference.x = reference.y = crop;
        current.width = reference.width = w;
        current.height = reference.height = h;
        for (unsigned p = 0; p < 3; ++p) {
            const unsigned shift = p ? 2 : 1;
            const unsigned pitch = (w + crop + shift - 1) / shift + padding;
            const unsigned rows = (h + crop + shift - 1) / shift;
            planes[p].resize(size_t(pitch) * rows);
            for (auto& b : planes[p]) b = uint8_t(randomValue());
            current.planes[p] = {planes[p].data(), int(pitch), int(rows)};
            reference.planes[p] = {planes[p].data(), int(pitch), int(rows)};
        }
    }
    void format(unsigned matrix, bool full, unsigned num, unsigned den) {
        current.matrix = static_cast<video_stream::Matrix>(matrix);
        reference.matrix = static_cast<video_reference::Matrix>(matrix);
        current.full_range = reference.full_range = full;
        current.sar_num = reference.sar_num = num;
        current.sar_den = reference.sar_den = den;
    }
    void compare(unsigned w, unsigned h, bool aspect, bool smooth) {
        const size_t bytes = size_t(w) * h * 3;
        std::vector<uint8_t> old(bytes + 2, 0xA5), now(bytes + 2, 0xA5);
        check(video_reference::convert(reference, old.data() + 1, bytes, w, h, aspect, smooth), "reference conversion");
        check(video_stream::convert(current, now.data() + 1, bytes, w, h, aspect, smooth), "candidate conversion");
        if (old != now) {
            std::fprintf(stderr, "Mismatch: source %ux%u crop %u -> %ux%u aspect=%d smooth=%d matrix=%d full=%d\n",
                current.width, current.height, current.x, w, h, int(aspect), int(smooth), int(current.matrix), int(current.full_range));
            check(false, "bit-exact pixels and canaries");
        }
    }
};

static void correctness() {
    for (unsigned count = 1; count <= 64; ++count)
        for (unsigned sum = 0; sum <= count * 255; ++sum)
            check(video_stream::conversion_detail::roundedSmallMean(sum,count) == int((sum+count/2)/count),
                "exhaustive exact mean, every possible sum for 1..64 bytes");
    // Includes the complete reachable range of all supported YUV matrices,
    // positive and negative half-steps, clipping and out-of-gamut components.
    for (int value = -12000000; value <= 12000000; ++value)
        check(video_stream::conversion_detail::channel32(value) == video_reference::channel(value),
            "exhaustive exact RGB rounding/saturation");
    for (unsigned i = 0; i < 1200; ++i) {
        Fixture f(1 + randomValue() % 137, 1 + randomValue() % 113, randomValue() % 4, randomValue() % 17);
        const unsigned ratios[][2] = {{1,1},{0,0},{16,15},{64,45},{3979,3976}};
        const auto& sar = ratios[randomValue() % 5];
        f.format(i % 3, bool(i % 2), sar[0], sar[1]);
        f.compare(1 + randomValue() % 150, 1 + randomValue() % 129, bool(randomValue() % 2), bool(randomValue() % 2));
    }
    for (auto dimensions : {std::array<unsigned,2>{1280,720}, {1920,1034}, {1920,1080}, {3840,2160}, {240,320}}) {
        Fixture f(dimensions[0], dimensions[1], 1);
        for (unsigned matrix = 0; matrix < 3; ++matrix) for (bool full : {false, true}) {
            f.format(matrix, full, 3979, 3976);
            for (bool smooth : {false, true}) for (bool aspect : {false, true})
                f.compare(720, 480, aspect, smooth);
        }
    }
    Fixture wide(4097, 7, 1); wide.compare(3001, 3, false, true); wide.compare(8193, 9, false, false);
    for (unsigned width : {255u,256u,257u,511u,512u,513u}) wide.compare(width,5,false,true);
    Fixture tiny(1,1); tiny.compare(720,576,true,true);
    Fixture a(1280,720,1), b(1920,1034,3);
    auto parallel = [](Fixture& f) {
        for (unsigned i = 0; i < 12; ++i) {
            f.format(i%3, bool(i%2), 3979,3976);
            f.compare(720,480,bool(i%2),true);
        }
    };
    std::thread first([&] { parallel(a); }), second([&] { parallel(b); });
    first.join(); second.join();
    std::puts("PASS: frozen-reference pixel equality, all matrices/ranges, crop/padding/SAR/bars, smooth/nearest and wide rasters");
}

static volatile uint64_t checksum = 0;
static double measure(Fixture& f, bool reference, bool smooth, std::vector<uint8_t>& out, unsigned iterations) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        const bool ok = reference ? video_reference::convert(f.reference, out.data(), out.size(), 720,480,true,smooth)
                                 : video_stream::convert(f.current, out.data(), out.size(), 720,480,true,smooth);
        check(ok, "benchmark conversion");
        checksum += out[(size_t(i) * 997 + 720 * 240 * 3) % out.size()];
    }
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start).count() / iterations;
}
static void benchmark() {
    std::puts("Synthetic CPU-only benchmark, same process/toolchain, median of 5 alternating batches x 10 frames; not VLC/CRT");
    for (auto dimensions : {std::array<unsigned,2>{1280,720}, {1920,1034}, {1920,1080}, {3840,2160}}) {
        Fixture f(dimensions[0], dimensions[1]); f.format(1,false, dimensions[1] == 1034 ? 3979 : 1, dimensions[1] == 1034 ? 3976 : 1);
        std::vector<uint8_t> out(720*480*3);
        for (bool smooth : {false,true}) {
            std::array<double,5> before{}, after{};
            measure(f,true,smooth,out,2); measure(f,false,smooth,out,2);
            for (unsigned round = 0; round < 5; ++round) {
                if (round % 2) { after[round] = measure(f,false,smooth,out,10); before[round] = measure(f,true,smooth,out,10); }
                else { before[round] = measure(f,true,smooth,out,10); after[round] = measure(f,false,smooth,out,10); }
            }
            std::sort(before.begin(),before.end()); std::sort(after.begin(),after.end());
            std::printf("%ux%u -> 720x480 smooth=%d: reference=%.3f ms candidate=%.3f ms speedup=%.2fx\n",
                dimensions[0], dimensions[1], int(smooth), before[2], after[2], before[2]/after[2]);
        }
    }
    std::printf("checksum=%llu\n", static_cast<unsigned long long>(checksum));
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") benchmark();
    else correctness();
}
