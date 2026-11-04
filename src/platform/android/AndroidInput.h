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
    void Reset();
    input::GamepadState Snapshot();
private:
    std::mutex mutex_;
    std::array<bool, 320> keys_{};
    input::GamepadState controller_{}, touch_{};
};
}
