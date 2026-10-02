#pragma once
#include "video_frame.h"
#include <array>
#include <memory>
#include <new>

namespace video_stream {
enum class Ownership { free, writing, ready, sending };
struct Frame {
    std::unique_ptr<uint8_t[]> pixels;
    Ownership owner = Ownership::free;
    uint64_t generation = 0, sequence = 0;
    unsigned width = 0, height = 0;
    bool interlace = false;
    size_t bytes = 0;
    int64_t pts = 0;
};

// The caller serializes all state transitions. Pixel access is exclusive to
// the writer or sender; changing generation never reclaims either's storage.
class FrameQueue {
public:
    static constexpr unsigned pending_limit = 4;
    static constexpr unsigned slot_count = pending_limit + 2;
    static constexpr int64_t max_pending_age_us = 250000;
    std::array<Frame, slot_count> slots;
    uint64_t generation = 1;
    uint64_t dropped_full = 0, dropped_mode = 0, dropped_late = 0;
    uint64_t dropped_expired = 0, dropped_discontinuity = 0, pts_regressions = 0;
    unsigned high_water = 0;

    bool allocate()
    {
        clear();
        for (auto& frame : slots) {
            frame.pixels.reset(new (std::nothrow) uint8_t[frame_capacity]);
            if (!frame.pixels) { clear(); return false; }
        }
        return true;
    }
    void clear()
    {
        for (auto& frame : slots) frame = Frame{};
        generation = 1; next_sequence = 0;
        dropped_full = dropped_mode = dropped_late = 0; high_water = 0;
        dropped_expired = dropped_discontinuity = pts_regressions = 0;
        have_last_pts = false;
    }
    unsigned pending() const
    {
        unsigned count = 0;
        for (const auto& frame : slots) if (frame.owner == Ownership::ready) ++count;
        return count;
    }
    int reserve()
    {
        for (unsigned i = 0; i < slot_count; ++i) {
            Frame& frame = slots[i];
            if (frame.pixels && frame.owner == Ownership::free) {
                frame.owner = Ownership::writing;
                frame.generation = generation;
                return int(i);
            }
        }
        return -1;
    }
    bool submit(int index)
    {
        Frame& frame = slots[index];
        if (frame.owner != Ownership::writing) return false;
        if (frame.generation != generation) {
            release(index); ++dropped_mode; return false;
        }
        // A backwards display date invalidates older pending pictures, which
        // might otherwise become due after the new timeline. Never reclaim
        // the slot already being converted or sent.
        if (have_last_pts && frame.pts < last_pts) {
            ++pts_regressions;
            for (auto& pending : slots) if (pending.owner == Ownership::ready) {
                pending.owner = Ownership::free; ++dropped_discontinuity;
            }
        }
        last_pts = frame.pts; have_last_pts = true;
        if (pending() >= pending_limit) {
            release(oldest()); ++dropped_full;
        }
        frame.sequence = ++next_sequence;
        frame.owner = Ownership::ready;
        high_water = (std::max)(high_water, pending());
        return true;
    }
    int take()
    {
        const int index = oldest();
        if (index >= 0) slots[index].owner = Ownership::sending;
        return index;
    }
    // VLC 3 display() supplies a monotonic presentation date. Keep the newest
    // due picture after stalls; never drain a stale FIFO one frame per refresh.
    int takeDue(int64_t now)
    {
        for (auto& frame : slots)
            if (frame.owner == Ownership::ready && frame.pts < now &&
                uint64_t(now)-uint64_t(frame.pts) > uint64_t(max_pending_age_us)) {
                frame.owner = Ownership::free; ++dropped_expired;
            }
        int selected = -1;
        for (unsigned i=0; i<slot_count; ++i)
            if (slots[i].owner == Ownership::ready && slots[i].pts <= now &&
                (selected < 0 || slots[i].sequence > slots[selected].sequence)) selected = int(i);
        if (selected < 0) return -1;
        for (unsigned i=0; i<slot_count; ++i)
            if (int(i) != selected && slots[i].owner == Ownership::ready &&
                slots[i].sequence < slots[selected].sequence) { release(int(i)); ++dropped_late; }
        slots[selected].owner = Ownership::sending;
        return selected;
    }
    void release(int index)
    {
        if (index >= 0) slots[index].owner = Ownership::free;
    }
    void changeGeneration()
    {
        ++generation;
        have_last_pts = false;
        for (auto& frame : slots) if (frame.owner == Ownership::ready) {
            frame.owner = Ownership::free; ++dropped_mode;
        }
    }
private:
    uint64_t next_sequence = 0;
    int64_t last_pts = 0;
    bool have_last_pts = false;
    int oldest() const
    {
        int result = -1;
        for (unsigned i = 0; i < slot_count; ++i)
            if (slots[i].owner == Ownership::ready &&
                (result < 0 || slots[i].sequence < slots[result].sequence))
                result = int(i);
        return result;
    }
};

template<class Mode> bool sameMode(const Mode& a, const Mode& b)
{
    return a.pClock == b.pClock && a.hActive == b.hActive && a.hBegin == b.hBegin &&
        a.hEnd == b.hEnd && a.hTotal == b.hTotal && a.vActive == b.vActive &&
        a.vBegin == b.vBegin && a.vEnd == b.vEnd && a.vTotal == b.vTotal && a.interlace == b.interlace;
}

// Frame generations also change for content-only settings. Only effective
// timings require a hardware switch. Failed calls invalidate the API state.
template<class Mode> class ModeState {
    Mode applied{};
    bool valid = false;
public:
    template<class Switch> bool apply(const Mode& requested, Switch switchMode)
    {
        if (valid && sameMode(applied, requested)) return true;
        valid = false;
        if (!switchMode(requested)) return false;
        applied = requested;
        valid = true;
        return true;
    }
};

template<class Mode> bool validMode(const Mode& mode)
{
    size_t bytes = 0;
    return std::isfinite(mode.pClock) && mode.pClock > 0 && mode.interlace <= 1 &&
        mode.hActive <= mode.hBegin && mode.hBegin < mode.hEnd && mode.hEnd < mode.hTotal &&
        mode.vActive <= mode.vBegin && mode.vBegin < mode.vEnd && mode.vEnd < mode.vTotal &&
        frameSize(mode.hActive, mode.vActive, mode.interlace != 0, bytes);
}
}
