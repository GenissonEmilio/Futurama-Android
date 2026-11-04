#include "platform/android/AndroidInput.h"
#include <algorithm>
#include <cmath>

namespace fut::platform {
namespace {
s16 Stick(float v) { return static_cast<s16>(std::clamp(v, -1.f, 1.f) * 32767.f); }
u8 Trigger(float v) { return static_cast<u8>(std::clamp(v, 0.f, 1.f) * 255.f); }
void Buttons(input::GamepadState& s, unsigned mask) {
    s.buttons = mask & 0xff;
    constexpr unsigned bits[] = {0x1000, 0x2000, 0x4000, 0x8000, 0x200, 0x100, 0x400, 0x800};
    for (unsigned i = 0; i < 8; ++i) s.analog[i] = (mask & bits[i]) ? 255 : 0;
}
}
void AndroidInput::Key(int key, bool down) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (key >= 0 && key < static_cast<int>(keys_.size())) keys_[key] = down;
}
void AndroidInput::Axes(float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy) {
    std::lock_guard<std::mutex> lock(mutex_);
    controller_.thumbLX = Stick(lx); controller_.thumbLY = Stick(-ly);
    controller_.thumbRX = Stick(rx); controller_.thumbRY = Stick(-ry);
    controller_.analog[6] = Trigger(lt); controller_.analog[7] = Trigger(rt);
    controller_.buttons = (hx < -.5f ? 4 : hx > .5f ? 8 : 0) | (hy < -.5f ? 1 : hy > .5f ? 2 : 0);
}
void AndroidInput::Touch(unsigned buttons, float lx, float ly, float rx, float ry) {
    std::lock_guard<std::mutex> lock(mutex_);
    Buttons(touch_, buttons);
    touch_.thumbLX = Stick(lx); touch_.thumbLY = Stick(ly);
    touch_.thumbRX = Stick(rx); touch_.thumbRY = Stick(ry);
}
void AndroidInput::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_.fill(false); controller_ = {}; touch_ = {};
}
input::GamepadState AndroidInput::Snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    input::GamepadState s = controller_;
    // Android KeyEvent values: gamepad, d-pad, Enter/Back and keyboard navigation.
    constexpr int key[] = {19,20,21,22,108,109,106,107,96,97,99,100,103,102,104,105,66,4,62,23};
    constexpr unsigned mask[] = {1,2,4,8,16,32,64,128,0x1000,0x2000,0x4000,0x8000,0x200,0x100,0x400,0x800,16,32,0x1000,0x1000};
    unsigned m = 0;
    for (unsigned i = 0; i < sizeof(key)/sizeof(key[0]); ++i) if (keys_[key[i]]) m |= mask[i];
    input::GamepadState k; Buttons(k, m);
    s.buttons |= k.buttons | touch_.buttons;
    for (unsigned i = 0; i < 8; ++i) s.analog[i] = std::max({s.analog[i], k.analog[i], touch_.analog[i]});
    auto strongest = [](s16 a, s16 b) { return std::abs(int(a)) > std::abs(int(b)) ? a : b; };
    s.thumbLX = strongest(s.thumbLX, touch_.thumbLX); s.thumbLY = strongest(s.thumbLY, touch_.thumbLY);
    s.thumbRX = strongest(s.thumbRX, touch_.thumbRX); s.thumbRY = strongest(s.thumbRY, touch_.thumbRY);
    return s;
}
}
