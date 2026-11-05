#pragma once

#include <cstdint>
#include <string_view>

namespace fut::game::app { class App; }

namespace fut::platform {

// Stable JNI wire values, independent of Character::type (which orders Leela before Bender).
enum class TouchHudCharacter : std::uint8_t { Generic, Fry, Bender, Leela, Zoidberg };
enum class TouchHudInteraction : std::uint8_t { Generic, Interact, Door, Examine, Mechanism };
// Zero is unknown / no weapon. The remaining values are the real WeaponType + 1.
enum class TouchHudWeapon : std::uint8_t { None, Hammer, Pistol, Shotgun, Railgun, MachineGun };

struct TouchHudState {
    TouchHudCharacter character = TouchHudCharacter::Generic;
    TouchHudInteraction interaction = TouchHudInteraction::Generic;
    TouchHudWeapon weapon = TouchHudWeapon::None;
    bool gameplay = false;
    bool nonGameplay = true;
    bool firstPerson = false;
};

// Bits 0..2: character; 3..5: Y interaction; 6: gameplay; 7: non-gameplay;
// 8..15: held weapon category; 16: the current camera is in first-person mode.
constexpr std::uint64_t PackTouchHudState(const TouchHudState& state) {
    return (std::uint64_t(state.character) & 7u) |
           ((std::uint64_t(state.interaction) & 7u) << 3) |
           (std::uint64_t(state.gameplay) << 6) |
           (std::uint64_t(state.nonGameplay) << 7) |
           (std::uint64_t(state.weapon) << 8) |
           (std::uint64_t(state.firstPerson) << 16);
}

// Value-only input makes mode classification testable without a disc, renderer or Android runtime.
struct TouchHudFacts {
    std::uint32_t playerType = 0;
    int weaponType = -1;
    bool ingame = false;
    bool sessionReady = false;
    bool hasPlayer = false;
    bool playerControllable = false;
    bool playerAlive = false;
    bool pauseShown = false;
    bool breakdownOpen = false;
    bool memoryCardFlow = false;
    bool intermissionActive = false;
    bool moviePlaying = false;
    bool leavingIngame = false;
    bool firstPersonCamera = false;
    TouchHudInteraction interaction = TouchHudInteraction::Generic;
};

TouchHudState ClassifyTouchHudState(const TouchHudFacts& facts);
// Exact shipped hint IDs give semantics; a live prompt must also contain the Xbox Y glyph.
TouchHudInteraction ClassifyTouchHudHint(std::string_view id, std::u16string_view text);
// Call only on the game thread, after App::Frame. UI callers receive only the packed atomic copy.
TouchHudState CaptureTouchHudState(const game::app::App& app);

}  // namespace fut::platform
