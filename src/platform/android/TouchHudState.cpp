#include "platform/android/TouchHudState.h"

#include "game/app/App.h"
#include "game/app/Breakdown.h"
#include "game/app/Contexts.h"
#include "game/app/MemoryCards.h"
#include "game/app/PauseMenu.h"
#include "game/level/Camera.h"
#include "game/level/Hud.h"
#include "game/object/Character.h"
#include "game/object/Pickup.h"

namespace fut::platform {
namespace {
bool HintIdEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char c = a[i];
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (c != b[i]) return false;
    }
    return true;
}
void ObserveHint(std::string_view id, std::u16string_view text, void* user) {
    auto& interaction = *static_cast<TouchHudInteraction*>(user);
    const auto candidate = ClassifyTouchHudHint(id, text);
    // Prefer a specific active prompt over a generic Y tutorial when both are displayed.
    if (candidate != TouchHudInteraction::Generic &&
        (interaction == TouchHudInteraction::Generic || interaction == TouchHudInteraction::Interact))
        interaction = candidate;
}
}

TouchHudInteraction ClassifyTouchHudHint(std::string_view id, std::u16string_view text) {
    // Verified in language/hints_xbox.ldb: U+0010=A, U+0011=B, U+0012=X, U+0013=Y.
    // Semantic IDs are shared by all languages, so no translated words or nearby object names are guessed.
    if (text.find(u'\x13') == std::u16string_view::npos) return TouchHudInteraction::Generic;
    if (HintIdEquals(id, "HINTS_024")) return TouchHudInteraction::Door;  // open / close
    if (HintIdEquals(id, "HINTS_029") || HintIdEquals(id, "HINTS_032") || HintIdEquals(id, "HINTS_035"))
        return TouchHudInteraction::Mechanism;  // push button, turn wheel, pull lever
    // No shipped Y hint identifies examine. Leave that enum reserved rather than inventing an action.
    return TouchHudInteraction::Interact;
}

TouchHudState ClassifyTouchHudState(const TouchHudFacts& facts) {
    TouchHudState state;
    if (!facts.ingame || !facts.hasPlayer) return state;
    switch (facts.playerType) {
    case 1: state.character = TouchHudCharacter::Fry; break;
    case 2: state.character = TouchHudCharacter::Leela; break;
    case 3: state.character = TouchHudCharacter::Bender; break;
    case 4: state.character = TouchHudCharacter::Zoidberg; break;
    default: break;
    }
    if (facts.playerType == 1 && facts.weaponType >= 0 && facts.weaponType <= 4)
        state.weapon = static_cast<TouchHudWeapon>(facts.weaponType + 1);
    state.gameplay = facts.sessionReady && facts.playerControllable && facts.playerAlive &&
                     !facts.pauseShown && !facts.breakdownOpen && !facts.memoryCardFlow &&
                     !facts.intermissionActive && !facts.moviePlaying && !facts.leavingIngame;
    state.nonGameplay = !state.gameplay;
    state.firstPerson = state.gameplay && facts.firstPersonCamera;
    if (state.gameplay) state.interaction = facts.interaction;
    return state;
}

TouchHudState CaptureTouchHudState(const game::app::App& app) {
    TouchHudFacts facts;
    facts.ingame = app.ingame && app.Current() == app.ingame;
    if (!facts.ingame) return ClassifyTouchHudState(facts);
    const auto* loaded = app.ingame->Level();
    facts.sessionReady = loaded && loaded->session && loaded->session->IsReady();
    facts.leavingIngame = app.ingame->leave;
    facts.pauseShown = app.pause && app.pause->shown;
    facts.breakdownOpen = app.breakdown && app.breakdown->open;
    facts.memoryCardFlow = app.memoryCards && app.memoryCards->running;
    facts.intermissionActive = app.intermission.active;
    facts.moviePlaying = bool(app.movie);
    const auto* level = app.CurrentLevel();
    const auto* player = level ? level->player : nullptr;
    facts.hasPlayer = player != nullptr;
    if (player) {
        facts.playerType = player->type;
        facts.playerControllable = player->hasControl != 0;
        facts.playerAlive = !player->IsDead();
        if (player->weapon) facts.weaponType = static_cast<int>(player->weapon->WeaponType());
    }
    facts.firstPersonCamera = level && level->camera && level->camera->mode == 3;
    if (level && level->hud) level->hud->VisitVisibleHints(ObserveHint, &facts.interaction);
    return ClassifyTouchHudState(facts);
}

}  // namespace fut::platform
