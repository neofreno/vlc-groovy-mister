#pragma once
#include <cmath>
#include <cstddef>
#include <limits>
#include <cstdint>

namespace video_stream {
struct AutomaticModeInputs {
    unsigned rate = 0, base = 0;
    bool only15k = true;
    void updateRate(unsigned numerator, unsigned denominator) {
        // Missing metadata must not replace an already known source rate.
        if (numerator && denominator) { rate = numerator; base = denominator; }
    }
    bool requiresReselect(int preset, const AutomaticModeInputs& applied) const {
        if (preset != 1) return false; // Fixed presets and Manual are unaffected.
        const bool known = rate && base, wasKnown = applied.rate && applied.base;
        return only15k != applied.only15k || known != wasKnown ||
            (known && uint64_t(rate)*applied.base != uint64_t(applied.rate)*base);
    }
};

template<class Mode, size_t N>
int selectMode(unsigned width, unsigned height, unsigned rate, unsigned base,
               const Mode (&modes)[N], bool only15k)
{
    const double fps = rate && base ? double(rate)/base : 0;
    // 24p uses the conventional 2:3 cadence. 25/50 use PAL; 29.97/59.94 use NTSC.
    const double desired = fps > 23 && fps < 24.1 ? fps*2.5 : fps > 0 && fps <= 30 ? fps*2 : fps;
    double best = (std::numeric_limits<double>::max)();
    int selected = -1;
    for (size_t i=2; i<N; ++i) {
        const auto& mode = modes[i];
        if (!mode.hTotal || !mode.vTotal || !mode.hActive || !mode.vActive) continue;
        const double horizontal = mode.pClock*1000000/mode.hTotal;
        if (only15k && (horizontal < 15000 || horizontal > 16500)) continue;
        const double refresh = horizontal/mode.vTotal*(mode.interlace ? 2 : 1);
        const double cadence = desired > 0 ? std::abs(refresh-desired)/desired*20000 : 0;
        const double score = std::abs(double(width)-mode.hActive) + std::abs(double(height)-mode.vActive) + cadence +
            (mode.interlace && height <= 288 ? 500 : 0);
        if (score < best) { best=score; selected=int(i); }
    }
    return selected;
}
}
