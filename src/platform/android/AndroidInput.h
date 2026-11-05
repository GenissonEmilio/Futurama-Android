#pragma once
#include "game/input/Pad.h"
#include <array>
#include <mutex>

namespace fut::platform {
// UI and game thread exchange complete snapshots, never individual game Pad fields.
class AndroidInput {
public:
    void Key(int key, bool down);
    void Axes(float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy);
    void Touch(unsigned buttons, float lx, float ly, float rx, float ry);
    // Relative screen motion / short screen edge (+x right, +y down). Each
    // Snapshot consumes the accumulated motion once, including the final move
    // before a normal finger release. Reset discards cancelled/paused motion.
    void Look(float dx, float dy);
    void ClearTouch();
    void Reset();
    input::GamepadState Snapshot();
private:
    std::mutex mutex_;
    std::array<bool, 320> keys_{};
    input::GamepadState controller_{}, touch_{};
    double lookX_ = 0, lookY_ = 0;
};
}
