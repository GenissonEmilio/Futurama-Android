#include "platform/android/AndroidInput.h"
#include <algorithm>
#include <cmath>

namespace fut::platform {
namespace {
s16 Stick(float v) { return std::isfinite(v) ? static_cast<s16>(std::clamp(v, -1.f, 1.f) * 32767.f) : 0; }
u8 Trigger(float v) { return std::isfinite(v) ? static_cast<u8>(std::clamp(v, 0.f, 1.f) * 255.f) : 0; }
// Invert Pad::RemapAxis, whose input scale is 32768 (not Stick's 32767).
// Round away from the dead zone so even a very slow drag produces motion.
s16 LookAxis(double value, bool halfDeadZone, bool capAtFullDeadZone) {
    if (value == 0) return 0;
    const double deadZone = double(0.4f) * (halfDeadZone ? 0.5 : 1.0);
    const double raw = deadZone + std::abs(value) * (1.0 - deadZone) / double(1.3f);
    const double maximum = capAtFullDeadZone ? std::floor(double(0.4f) * 32768.0) : 32767.0;
    const int encoded = static_cast<int>(std::min(maximum, std::ceil(raw * 32768.0)));
    return static_cast<s16>(value < 0 ? -encoded : encoded);
}
void LookStick(double dx, double dy, input::GamepadState& s) {
    // At the game's fixed 60 Hz, 1/60 of the short edge equals one frame of
    // full camera stick. These are impulses, never a held stick or a timer.
    // FollowMode rotates its camera offset by -orbitX and raises it for +Y:
    // negative X turns the view right; negative Y tilts the view up.
    const double x = std::clamp(-dx * 60.0, -1.0, 1.0);
    const double y = std::clamp(dy * 60.0, -1.0, 1.0);
    const double ax = std::abs(x), ay = std::abs(y);
    // A half-dead-zone axis reaches raw .4 at remapped .325. Below that,
    // encode the dominant axis with the full dead zone and only the smaller
    // axis with half. That keeps Pad's coupled diagonal dead zones consistent.
    const double halfBoundary = double(0.4f) * 0.5 * double(1.3f) / (1.0 - double(0.4f) * 0.5);
    const bool bothOutside = ax > halfBoundary && ay > halfBoundary;
    const bool halfX = bothOutside || ax < ay, halfY = bothOutside || ay <= ax;
    s.thumbRX = LookAxis(x, halfX, halfX && !bothOutside);
    s.thumbRY = LookAxis(y, halfY, halfY && !bothOutside);
}
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
void AndroidInput::Look(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Bound the accumulator before conversion, even for malformed JNI input.
    if (std::isfinite(dx)) lookX_ = std::clamp(lookX_ + double(dx), -1.0, 1.0);
    if (std::isfinite(dy)) lookY_ = std::clamp(lookY_ + double(dy), -1.0, 1.0);
}
void AndroidInput::ClearTouch() {
    std::lock_guard<std::mutex> lock(mutex_);
    touch_ = {};
    lookX_ = lookY_ = 0;
}
void AndroidInput::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_.fill(false); controller_ = {}; touch_ = {};
    lookX_ = lookY_ = 0;
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
    input::GamepadState look;
    LookStick(lookX_, lookY_, look);
    lookX_ = lookY_ = 0;
    s.thumbRX = strongest(s.thumbRX, look.thumbRX); s.thumbRY = strongest(s.thumbRY, look.thumbRY);
    return s;
}
}
