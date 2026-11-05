// Standalone Android input regressions; no JNI, device or game assets needed.
#include "platform/android/AndroidInput.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

using namespace fut;
using fut::platform::AndroidInput;

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void Near(double actual, double expected, const char* message) {
    // One raw step after the half dead zone is < .00005.
    if (std::abs(actual - expected) > .00007) {
        std::fprintf(stderr, "FAIL: %s: got %.9f, expected %.9f\n", message, actual, expected);
        ++failures;
    }
}

bool Equal(const input::GamepadState& a, const input::GamepadState& b) {
    return a.buttons == b.buttons && a.analog == b.analog && a.thumbLX == b.thumbLX &&
           a.thumbLY == b.thumbLY && a.thumbRX == b.thumbRX && a.thumbRY == b.thumbRY;
}

struct PadReader {
    input::InputSystem system;
    input::Pad pad{system, 0};
    PadReader() { pad.SetDeviceOpen(true); }
    void Read(const input::GamepadState& state) { Check(pad.Poll(&state), "pad accepts Android snapshot"); }
};

void RelativeConsumption() {
    AndroidInput split, single;
    split.Look(.001f, -.002f);
    split.Look(.002f, -.001f);
    single.Look(.003f, -.003f);
    Check(Equal(split.Snapshot(), single.Snapshot()), "relative events add before a snapshot");
    Check(Equal(split.Snapshot(), {}), "motion is consumed exactly once while a finger stays still");
    split.Look(.002f, -.001f);
    split.Look(-.002f, .001f);
    Check(Equal(split.Snapshot(), {}), "opposite relative movements cancel before consumption");
    split.Look(0, 0);
    Check(Equal(split.Snapshot(), {}), "a stationary pointer never creates camera motion");
}

void DirectionsAndDeadZones() {
    AndroidInput input;
    PadReader reader;
    // Exercise the actual game's Pad remap, including both sides of the .325
    // diagonal boundary and subpixel movement below the ordinary stick dead zone.
    constexpr float magnitudes[] = {0, .00006f, .1f, .3249f, .325f, .3251f, .7f, 1};
    for (float x : magnitudes) for (float y : magnitudes) {
        for (float signX : {-1.f, 1.f}) for (float signY : {-1.f, 1.f}) {
            input.Look(signX * x / 60.f, signY * y / 60.f);
            reader.Read(input.Snapshot());
            Near(reader.pad.StickX(1), -signX * x, "drag right maps to negative follow-camera orbit X");
            Near(reader.pad.StickY(1), signY * y, "drag up maps to negative follow-camera orbit Y");
        }
    }
    input.Look(.000001f, 0);
    reader.Read(input.Snapshot());
    Check(reader.pad.StickX(1) < 0, "slow horizontal drag survives the game's .4 dead zone");
    input.Look(0, -.000001f);
    reader.Read(input.Snapshot());
    Check(reader.pad.StickY(1) < 0, "slow vertical drag survives the game's .4 dead zone");
}

void LegacyAndSimultaneousInput() {
    AndroidInput input;
    input.Axes(.3f, -.4f, .95f, .9f, .25f, .75f, -1, 1);
    input.Key(108, true); // physical Start
    input.Touch(0x1000 | 0x4000 | 0x80, -.8f, .9f, -.2f, .2f);
    const auto baseline = input.Snapshot();
    Check(baseline.thumbLX == static_cast<s16>(-.8f * 32767.f) &&
          baseline.thumbLY == static_cast<s16>(.9f * 32767.f), "touch movement keeps strongest-axis merge");
    Check(baseline.thumbRX == static_cast<s16>(.95f * 32767.f) &&
          baseline.thumbRY == static_cast<s16>(-.9f * 32767.f), "physical camera axes keep their original signs and scaling");
    Check((baseline.buttons & (4 | 2 | 16 | 128)) == (4 | 2 | 16 | 128), "keyboard, hat and touch digital actions combine");
    Check(baseline.analog[0] == 255 && baseline.analog[2] == 255 &&
          baseline.analog[6] == 63 && baseline.analog[7] == 191, "touch actions and controller triggers coexist");
    input.Look(.001f, -.001f);
    Check(Equal(input.Snapshot(), baseline), "stronger physical camera input remains available during a touch drag");
    Check(Equal(input.Snapshot(), baseline), "consuming touch look does not release movement or held actions");

    AndroidInput simultaneous;
    simultaneous.Axes(.5f, 0, 0, 0, 0, .7f, 0, 0);
    simultaneous.Key(108, true);
    simultaneous.Touch(0x1000, 0, .8f, 0, 0);
    simultaneous.Look(.001f, -.001f);
    const auto state = simultaneous.Snapshot();
    Check(state.thumbLX > 0 && state.thumbLY > 0 && state.thumbRX < 0 && state.thumbRY < 0 &&
          state.analog[0] == 255 && state.analog[7] > 0 && (state.buttons & 16),
          "movement, action, physical input and camera drag work in one snapshot");
}

void CancellationAndReset() {
    AndroidInput input, physical;
    for (AndroidInput* value : {&input, &physical}) {
        value->Axes(.3f, -.4f, .5f, -.6f, .7f, .8f, 1, -1);
        value->Key(108, true);
    }
    input.Touch(0xffff, -1, 1, -1, 1);
    input.Look(-.1f, .1f);
    input.ClearTouch();
    Check(Equal(input.Snapshot(), physical.Snapshot()), "ClearTouch removes touch and pending look while preserving controller and keyboard");
    input.Look(.1f, -.1f);
    input.Reset();
    Check(Equal(input.Snapshot(), {}), "Reset clears every input source and pending look");
    input.Look(.001f, -.001f);
    Check(input.Snapshot().thumbRX < 0, "a new gesture works immediately after reset");
    Check(Equal(input.Snapshot(), {}), "a released gesture cannot leave a held camera axis");
}

void MalformedValues() {
    AndroidInput input;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    input.Look(nan, inf);
    input.Look(-inf, nan);
    Check(Equal(input.Snapshot(), {}), "nonfinite motion cannot generate integer conversions or camera movement");
    input.Look(.001f, -.001f);
    input.Look(nan, inf);
    AndroidInput finite;
    finite.Look(.001f, -.001f);
    Check(Equal(input.Snapshot(), finite.Snapshot()), "malformed motion does not discard pending finite deltas");
    input.Axes(nan, inf, -inf, nan, nan, inf, nan, nan);
    input.Touch(0, nan, inf, -inf, nan);
    Check(Equal(input.Snapshot(), {}), "nonfinite legacy axes and triggers are neutral");
    input.Look(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    input.Look(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    PadReader reader;
    reader.Read(input.Snapshot());
    Near(reader.pad.StickX(1), -1, "extreme horizontal deltas saturate safely");
    Near(reader.pad.StickY(1), -1, "extreme vertical deltas saturate safely");
    Check(Equal(input.Snapshot(), {}), "extreme deltas also consume once");
}

void ConcurrentAccumulation() {
    AndroidInput input;
    auto producer = [&input] { for (int i = 0; i < 1000; ++i) input.Look(.000001f, -.000001f); };
    std::thread first(producer), second(producer);
    first.join(); second.join();
    PadReader reader;
    reader.Read(input.Snapshot());
    Near(reader.pad.StickX(1), -.12, "concurrent producers do not lose horizontal deltas");
    Near(reader.pad.StickY(1), -.12, "concurrent producers do not lose vertical deltas");
    Check(Equal(input.Snapshot(), {}), "concurrent accumulated motion is consumed once");
}
}

int main() {
    RelativeConsumption();
    DirectionsAndDeadZones();
    LegacyAndSimultaneousInput();
    CancellationAndReset();
    MalformedValues();
    ConcurrentAccumulation();
    if (failures) return 1;
    std::puts("Android input regressions passed.");
    return 0;
}
