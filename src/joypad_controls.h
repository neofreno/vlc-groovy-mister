#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace joy_control {
enum class Action { none, pause, stop, previous, next, forward, backward, chapter_next, chapter_previous };
inline Action decode(uint16_t buttons) {
    switch (buttons) {
    case 1u << 5: return Action::pause; // B2 / A
    case 1u << 6: return Action::stop; // B3 / Y
    case 1u << 7: return Action::previous; // B4 / X
    case 1u << 4:
    case (1u << 13) | (1u << 4): return Action::next; // B1 / B; legacy B10+B1
    case 1u << 0: return Action::forward;
    case 1u << 1: return Action::backward;
    case 1u << 3: return Action::chapter_next;
    case 1u << 2: return Action::chapter_previous;
    default: return Action::none; // Ignore ambiguous diagonals/combinations.
    }
}
struct Buttons {
    Action previous = Action::none;
    int64_t repeat_at = 0;
    bool armed = true;
    void reset(bool wait_release = false) { previous = Action::none; repeat_at = 0; armed = !wait_release; }
    Action update(uint16_t mask, int64_t now) {
        if (!armed) { if (!mask) armed = true; return Action::none; }
        const Action action = decode(mask);
        if (action != previous) {
            previous = action; repeat_at = now + 400000;
            return action;
        }
        if ((action == Action::forward || action == Action::backward) && now >= repeat_at) {
            repeat_at = now + 150000; // No catch-up burst after a stall.
            return action;
        }
        return Action::none;
    }
};
// Absolute targets avoid overflow/modulo-zero and stay inside the media.
inline int64_t seekTarget(int64_t time, int64_t length, Action action) {
    if (time < 0) return -1;
    if (length > 0 && time > length) time = length;
    int64_t target = time;
    if (action == Action::forward) {
        if (time > INT64_MAX - 1000000) return -1;
        target += 1000000;
    } else if (action == Action::backward) {
        target = time > 1000000 ? time - 1000000 : 0;
    } else {
        const int64_t step = length / 10;
        if (step <= 0) return -1;
        const int64_t remainder = time % step;
        if (action == Action::chapter_next) {
            const int64_t delta = step - remainder;
            target = delta > length - time ? length : time + delta;
        } else if (action == Action::chapter_previous) {
            const int64_t delta = remainder > 1000000 ? remainder : step;
            target = delta > time ? 0 : time - delta;
        } else return -1;
    }
    return length > 0 && target > length ? length : target;
}
// Production transfers a retained input reference with each queued command.
// Queue itself does not own/release it; the controller drains on invalidation.
struct Command { Action action = Action::none; int64_t date = 0; void* input = nullptr; uint64_t generation = 0; };
// Protected by the caller's mutex; no allocation on the video thread.
struct Queue {
    std::array<Command, 16> commands{};
    size_t head = 0, count = 0;
    void clear() { head = count = 0; }
    bool push(Action action, int64_t date, void* input = nullptr, uint64_t generation = 0) {
        if (count == commands.size()) return false;
        commands[(head + count) % commands.size()] = {action, date, input, generation}; ++count;
        return true;
    }
    bool pop(Command& command) {
        if (!count) return false;
        command = commands[head]; head = (head + 1) % commands.size(); --count;
        return true;
    }
};
}
