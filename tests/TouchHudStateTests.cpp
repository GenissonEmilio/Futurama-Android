#include "platform/android/TouchHudState.h"
#include "game/app/App.h"
#include "game/GameTime.h"
#include "game/level/Hud.h"
#include "game/text/LanguageDb.h"
#include "game/text/TextObject.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace fut::platform;
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
TouchHudFacts Playing(unsigned type = 1) {
    TouchHudFacts facts;
    facts.ingame = facts.sessionReady = facts.hasPlayer = true;
    facts.playerControllable = facts.playerAlive = true;
    facts.playerType = type;
    return facts;
}
void TestCharactersAndWeapons() {
    const TouchHudCharacter expected[] = {TouchHudCharacter::Generic, TouchHudCharacter::Fry,
        TouchHudCharacter::Leela, TouchHudCharacter::Bender, TouchHudCharacter::Zoidberg,
        TouchHudCharacter::Generic};
    for (unsigned type = 0; type < 6; ++type) {
        auto facts = Playing(type);
        const auto state = ClassifyTouchHudState(facts);
        Check(state.character == expected[type], "engine character type maps to stable UI identity");
        Check(state.gameplay && !state.nonGameplay, "controllable loaded player is gameplay");
    }
    auto facts = Playing();
    for (int type = -1; type <= 5; ++type) {
        facts.weaponType = type;
        const auto state = ClassifyTouchHudState(facts);
        Check(unsigned(state.weapon) == unsigned(type >= 0 && type <= 4 ? type + 1 : 0),
              "only actual known held weapon types are exposed");
    }
    facts.playerType = 3;
    facts.weaponType = 1;
    Check(ClassifyTouchHudState(facts).weapon == TouchHudWeapon::None, "non-Fry cannot expose firearm controls");
    facts.hasPlayer = false;
    Check(ClassifyTouchHudState(facts).character == TouchHudCharacter::Generic, "no stale character without player");
}
void TestNonGameplay() {
    bool TouchHudFacts::* blockers[] = {&TouchHudFacts::pauseShown, &TouchHudFacts::breakdownOpen,
        &TouchHudFacts::memoryCardFlow, &TouchHudFacts::intermissionActive, &TouchHudFacts::moviePlaying,
        &TouchHudFacts::leavingIngame};
    for (auto blocker : blockers) {
        auto facts = Playing();
        facts.firstPersonCamera = true;
        facts.*blocker = true;
        const auto state = ClassifyTouchHudState(facts);
        Check(!state.gameplay && state.nonGameplay && !state.firstPerson,
              "menus, movies, cutscenes and pending exits suppress gameplay and camera mode");
        Check(state.character == TouchHudCharacter::Fry, "pause retains actual player identity");
    }
    bool TouchHudFacts::* required[] = {&TouchHudFacts::ingame, &TouchHudFacts::sessionReady,
        &TouchHudFacts::hasPlayer, &TouchHudFacts::playerControllable, &TouchHudFacts::playerAlive};
    for (auto requirement : required) {
        auto facts = Playing();
        facts.*requirement = false;
        const auto state = ClassifyTouchHudState(facts);
        Check(!state.gameplay && state.nonGameplay, "loading, absent, dead or disabled player is not gameplay");
    }
    auto facts = Playing();
    facts.firstPersonCamera = true;
    Check(ClassifyTouchHudState(facts).firstPerson, "first-person mode follows actual camera during gameplay");
    Check(ClassifyTouchHudState(facts).interaction == TouchHudInteraction::Generic,
          "player presence and camera do not invent a script interaction");
    facts.interaction = TouchHudInteraction::Door;
    Check(ClassifyTouchHudState(facts).interaction == TouchHudInteraction::Door, "active prompt reaches gameplay snapshot");
    facts.pauseShown = true;
    Check(ClassifyTouchHudState(facts).interaction == TouchHudInteraction::Generic, "pause suppresses old world prompt");
}

void TestHintClassification() {
    Check(ClassifyTouchHudHint("HINTS_024", u"\x13 abrir / fechar") == TouchHudInteraction::Door,
          "localized open/close prompt uses exact semantic ID and Y glyph");
    for (const char* id : {"HINTS_029", "hints_032", "HINTS_035"})
        Check(ClassifyTouchHudHint(id, u"\x13") == TouchHudInteraction::Mechanism,
              "button, wheel and lever prompts classify independent of language and ID case");
    Check(ClassifyTouchHudHint("HINTS_025", u"\x13") == TouchHudInteraction::Interact, "generic interaction prompt");
    Check(ClassifyTouchHudHint("unrecognized", u"\x13") == TouchHudInteraction::Interact, "unknown live Y prompt keeps generic action");
    Check(ClassifyTouchHudHint("HINTS_024", u"\x10") == TouchHudInteraction::Generic, "same ID with A glyph cannot claim Y action");
    Check(ClassifyTouchHudHint("HINTS_035", u"lever") == TouchHudInteraction::Generic, "words or names alone cannot claim availability");
}

void Put16(std::vector<fut::u8>& data, size_t at, unsigned value) {
    data[at] = fut::u8(value); data[at + 1] = fut::u8(value >> 8);
}
void Put32(std::vector<fut::u8>& data, size_t at, unsigned value) {
    Put16(data, at, value); Put16(data, at + 2, value >> 16);
}
std::vector<fut::u8> SyntheticFont() {
    std::vector<fut::u8> data(10 + 256 * 10);
    Put16(data, 0, 256); Put16(data, 2, 256);
    Put16(data, 4, 16); Put16(data, 6, 8); Put16(data, 8, 16);
    for (unsigned i = 0; i < 256; ++i) {
        Put16(data, 10 + i * 10, i);
        Put16(data, 16 + i * 10, 8); Put16(data, 18 + i * 10, 16);
    }
    return data;
}
std::vector<fut::u8> SyntheticHints() {
    std::vector<fut::u8> data(0x1F0);
    std::memcpy(data.data(), "0BDL", 4);
    Put32(data, 4, 3); Put32(data, 8, 0x1F0);
    const char* ids[] = {"HINTS_024", "HINTS_029", "HINTS_025"};
    for (const char* id : ids) {
        while (*id) data.push_back(fut::u8(*id++));
        data.insert(data.end(), 4, 0);  // ID terminator plus the three unused name fields.
    }
    Put32(data, 12, unsigned(data.size() - 0x1F0));
    Put32(data, 0x10, unsigned(data.size()));
    std::memcpy(data.data() + 0x18, "English", 7);
    const size_t start = data.size();
    for (const char16_t* text : {u"\x13 open", u"\x13 button", u"\x13 use"}) {
        do { const char16_t c = *text; data.push_back(fut::u8(c)); data.push_back(fut::u8(c >> 8)); }
        while (*text++);
    }
    Put32(data, 0x14, unsigned(data.size() - start));
    return data;
}
struct SeenHints { unsigned count = 0; TouchHudInteraction interaction = TouchHudInteraction::Generic; };
SeenHints Observe(const fut::game::Hud& hud) {
    SeenHints seen;
    hud.VisitVisibleHints([](std::string_view id, std::u16string_view text, void* user) {
        auto& result = *static_cast<SeenHints*>(user);
        ++result.count;
        result.interaction = ClassifyTouchHudHint(id, text);
    }, &seen);
    return seen;
}
void TestVisibleHintLifecycle() {
    using namespace fut;
    text::DestroyAllFonts();
    const float previousStep = game::g_frameStep;
    game::g_frameStep = 0.1f;
    {
        text::TextManager texts({});
        Check(texts.LoadDbFromMemory(SyntheticHints(), "synthetic.ldb") != nullptr, "load synthetic localized hints");
        // HUDCheckpoint releases its (zero) sound handle at destruction; a level always owns a sound service.
        sound::XboxSound silentSound(nullptr, {});
        game::LevelData level;
        level.text = &texts;
        level.sound = &silentSound;
        game::Hud hud(level, [](const std::string& path, std::vector<u8>& out) {
            if (path != "fonts/Latin1.fnt") return false;
            out = SyntheticFont(); return true;
        });
        hud.Build("Level1-1");
        auto* info = hud.Find("info");
        Check(info != nullptr, "real HudInfo exists without disc assets");
        if (info) {
            info->Command("add", "HINTS_024");
            Check(Observe(hud).count == 0, "added but not drawn hint is not an available visible prompt");
            info->Draw();
            Check(Observe(hud).count == 0, "transparent initial hint is not visible");
            info->Update(); info->Draw();
            Check(Observe(hud).interaction == TouchHudInteraction::Door, "faded-in real HudInfo prompt classifies door");
            info->SetActive(false);
            Check(Observe(hud).count == 0, "hidden info clears prompt immediately");
            info->SetActive(true);
            Check(Observe(hud).count == 0, "reshown info waits until next draw");
            info->Draw();
            Check(Observe(hud).count == 1, "redrawn active info restores prompt");
            hud.enabled = 0;
            Check(Observe(hud).count == 0, "disabled entire HUD suppresses prompt");
            hud.enabled = 1;
            info->Command("remove", "HINTS_024");
            Check(Observe(hud).count == 0, "removed hint suppressed before next update");
            info->Update();
            info->Command("add", "HINTS_029"); info->Update(); info->Draw();
            Check(Observe(hud).interaction == TouchHudInteraction::Mechanism, "new actual mechanism hint replaces removed door");
            info->Command("HINTS_029", 0.01f); info->Update();
            Check(Observe(hud).count == 0, "expired timed prompt disappears");
            info->Command("add", "HINTS_025"); info->Update(); info->Draw();
            Check(Observe(hud).count == 1, "new generic prompt appears");
            info->Command("remove_all", "");
            Check(Observe(hud).count == 0, "remove_all suppresses prompts immediately");
        }
    }
    game::g_frameStep = previousStep;
    text::DestroyAllFonts();
}
void TestWireProtocol() {
    Check(PackTouchHudState({}) == 0x80, "idle snapshot has only non-gameplay flag");
    TouchHudState state;
    state.character = TouchHudCharacter::Bender;
    state.interaction = TouchHudInteraction::Mechanism;
    state.weapon = TouchHudWeapon::MachineGun;
    state.gameplay = true;
    state.nonGameplay = false;
    state.firstPerson = true;
    Check(PackTouchHudState(state) == (2u | (4u << 3) | (1u << 6) | (5u << 8) | (1u << 16)),
          "character, interaction, weapon, camera and mode have disjoint documented wire bits");
    fut::game::app::App app({});
    Check(PackTouchHudState(CaptureTouchHudState(app)) == 0x80,
          "uninitialized app capture needs no player, level, renderer or disc");
}
}

int main() {
    TestCharactersAndWeapons();
    TestNonGameplay();
    TestWireProtocol();
    TestHintClassification();
    TestVisibleHintLifecycle();
    if (failures) return 1;
    std::puts("Touch HUD state tests passed");
    return 0;
}
