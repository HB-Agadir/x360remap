// Portable test harness for MappingAssistant + the JSON round-trip in
// mapping.cpp. Compiles and runs on any platform (no Xbox headers involved) -
// see X360Remap/tests/README.md for the exact build command. This is what
// lets the keyboard/mouse config assistant's logic be validated for real
// (compiled + executed, not guessed) before it's wired into main.cpp, which
// can only be tested by pushing a build to the console.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../mapping.h"
#include "../mapping_assistant.h"
#include "../known_titles.h"
#include "../known_devices.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static std::vector<AssistantStep> DefaultKeyboardSteps() {
    return {
        {"A", KEYACT_A},
        {"B", KEYACT_B},
        {"X", KEYACT_X},
        {"Y", KEYACT_Y},
        {"LB", KEYACT_LB},
        {"RB", KEYACT_RB},
        {"Start", KEYACT_START},
        {"Back", KEYACT_BACK},
        {"D-Pad Up", KEYACT_DPAD_UP},
        {"D-Pad Down", KEYACT_DPAD_DOWN},
        {"D-Pad Left", KEYACT_DPAD_LEFT},
        {"D-Pad Right", KEYACT_DPAD_RIGHT},
        {"Left Stick Up", KEYACT_LSTICK_UP},
        {"Left Stick Down", KEYACT_LSTICK_DOWN},
        {"Left Stick Left", KEYACT_LSTICK_LEFT},
        {"Left Stick Right", KEYACT_LSTICK_RIGHT},
    };
}

// Simulates a clean press-then-release of `code`, one Tick() per phase, and
// returns the outcome from the release tick (where the assistant actually
// resolves the step) - this is what a real polling loop looks like once you
// strip out the Sleep(50) calls.
static AssistantOutcome::Type PressAndRelease(MappingAssistant& a, int32_t code, std::string* msg = nullptr) {
    a.Tick(code);          // press
    a.Tick(code);          // still held one more tick, matches original's pressWaitCount>0 requirement
    return a.Tick(MappingAssistant::NO_CODE, msg); // release -> resolves
}

static void TestNormalSequence() {
    std::printf("TestNormalSequence\n");
    MappingAssistant a(DefaultKeyboardSteps());

    // HID codes for Z,X,C,V,Q,E,Enter,Backspace,arrows... just need distinct
    // small integers here, real HID codes are exercised in TestJsonRoundTrip.
    int32_t codes[] = {29, 27, 6, 25, 20, 8, 40, 42, 82, 81, 80, 79, 4, 22, 8 + 100, 7 + 100};
    // ^ deliberately reusing "8" is avoided below by using +100 offsets for two entries

    size_t i = 0;
    while (a.IsActive() && i < 16) {
        AssistantOutcome::Type outcome = PressAndRelease(a, codes[i]);
        CHECK(outcome == AssistantOutcome::Assigned);
        i++;
    }

    CHECK(a.IsComplete());
    CHECK(!a.WasCancelled());
    CHECK(a.Results().size() == 16);
    if (a.Results().size() == 16) {
        CHECK(a.Results()[0].code == 29 && a.Results()[0].action == KEYACT_A);
        CHECK(a.Results()[15].code == 107 && a.Results()[15].action == KEYACT_LSTICK_RIGHT);
    }
    std::printf("  %zu steps assigned, complete=%d\n", a.Results().size(), a.IsComplete());
}

static void TestHoldToSkip() {
    std::printf("TestHoldToSkip\n");
    MappingAssistant a(DefaultKeyboardSteps(), /*holdTicksToSkip=*/5); // small value for a fast test

    // Press code 10 and hold it without releasing -> should skip once held
    // long enough. First tick just registers the press (holdCount stays 0,
    // matching MappingThreadProc's original loop); the hold counter only
    // starts climbing from the second tick onward, so reaching
    // holdTicksToSkip takes holdTicksToSkip+1 ticks in total.
    AssistantOutcome::Type last = AssistantOutcome::Pending;
    for (int t = 0; t < 6; t++) {
        last = a.Tick(10);
    }
    CHECK(last == AssistantOutcome::Skipped);
    CHECK(a.Results().empty()); // skipped step produced no binding

    // The held key must be released before the NEXT step can register a
    // press - otherwise the same physical key-still-down would immediately
    // bind to step 2 as well.
    AssistantOutcome::Type whileStillHeld = a.Tick(10);
    CHECK(whileStillHeld == AssistantOutcome::Pending);
    CHECK(a.CurrentPrompt() == "B"); // advanced to next step regardless

    a.Tick(MappingAssistant::NO_CODE); // release
    AssistantOutcome::Type assigned = PressAndRelease(a, 11);
    CHECK(assigned == AssistantOutcome::Assigned);
    CHECK(a.Results().size() == 1);
    CHECK(a.Results()[0].code == 11 && a.Results()[0].action == KEYACT_B);
    std::printf("  skip + next-step press handled correctly\n");
}

static void TestConflictDetection() {
    std::printf("TestConflictDetection\n");
    MappingAssistant a(DefaultKeyboardSteps());

    CHECK(PressAndRelease(a, 50) == AssistantOutcome::Assigned); // A = 50

    // Try to bind B to the same code 50 - must be rejected as a conflict,
    // not silently accepted (a real key can't fire two actions cleanly).
    std::string msg;
    AssistantOutcome::Type pressOutcome = a.Tick(50, &msg);
    CHECK(pressOutcome == AssistantOutcome::Conflict);
    CHECK(!msg.empty());
    // Holding the conflicting key longer must not spam a new Conflict event
    // every tick.
    AssistantOutcome::Type heldAgain = a.Tick(50);
    CHECK(heldAgain == AssistantOutcome::Pending);

    a.Tick(MappingAssistant::NO_CODE); // release the conflicting key
    // Now bind B to a fresh, unused code.
    CHECK(PressAndRelease(a, 51) == AssistantOutcome::Assigned);

    CHECK(a.Results().size() == 2);
    CHECK(a.Results()[0].code == 50 && a.Results()[1].code == 51);
    // No duplicate codes anywhere in the final result set.
    CHECK(a.Results()[0].code != a.Results()[1].code);
    std::printf("  conflict rejected, distinct code accepted afterward\n");
}

static void TestCancelMidway() {
    std::printf("TestCancelMidway\n");
    MappingAssistant a(DefaultKeyboardSteps());

    CHECK(PressAndRelease(a, 1) == AssistantOutcome::Assigned);
    CHECK(PressAndRelease(a, 2) == AssistantOutcome::Assigned);
    a.Cancel();

    CHECK(a.WasCancelled());
    CHECK(!a.IsActive());
    CHECK(!a.IsComplete()); // cancelled != complete, caller must check this before saving
    CHECK(a.Results().size() == 2); // partial results exist but the caller must discard them

    // Ticking after cancel must be a no-op, matching MappingThreadProc's
    // "if (!g_mappingState.active) { ... don't save ... }" guard.
    AssistantOutcome::Type afterCancel = a.Tick(3);
    CHECK(afterCancel == AssistantOutcome::Pending);
    CHECK(a.Results().size() == 2);
    std::printf("  cancel stops the assistant and leaves results marked as discardable\n");
}

// Exercises the REAL production code in mapping.cpp (rapidjson-based
// LoadMappingsFromJson/SaveMappingsToJson), not a reimplementation. This is
// what caught the actual bug: SaveMappingsToJson never wrote the "keys"
// array back out, so any keyboard mapping produced by an assistant would
// vanish the next time X360Remap.json was saved (e.g. after adding a mouse
// via the same mechanism). Fixed in mapping.cpp; this test proves it.
static void TestJsonRoundTrip() {
    std::printf("TestJsonRoundTrip\n");
    MappingAssistant a(DefaultKeyboardSteps());

    // Real HID keyboard/keypad usage codes (page 0x07): Z,X,C,V,Q,E,Enter,
    // Backspace, arrow keys, WASD.
    int32_t realCodes[] = {29, 27, 6, 25, 20, 8, 40, 42, 82, 81, 80, 79, 26, 22, 4, 7};
    size_t i = 0;
    while (a.IsActive() && i < 16) {
        CHECK(PressAndRelease(a, realCodes[i]) == AssistantOutcome::Assigned);
        i++;
    }
    CHECK(a.IsComplete());

    ClearDynamicMappings();

    std::vector<HidKeyMapEntry> keyEntries;
    for (const auto& r : a.Results())
        keyEntries.push_back({static_cast<uint8_t>(r.code), r.action});

    auto dynData = std::unique_ptr<DynamicMappingData>(new DynamicMappingData());
    dynData->keyEntries = keyEntries;

    HidDeviceMapping mapping = {};
    mapping.vendorId = 1121;   // 0x0461
    mapping.productId = 16193; // 0x3f41
    mapping.keyMap = dynData->keyEntries.data();
    mapping.keyMapCount = static_cast<uint8_t>(dynData->keyEntries.size());

    g_dynamicData.push_back(std::move(dynData));
    g_dynamicMappings.push_back(mapping);

    std::string json = SaveMappingsToJson();
    CHECK(!json.empty());
    CHECK(json.find("\"keys\"") != std::string::npos);
    std::printf("  serialized JSON contains a \"keys\" array (%zu bytes)\n", json.size());

    // Now reload from that exact JSON string and verify every code/action
    // pair survives the round trip unchanged.
    ClearDynamicMappings();
    bool loaded = LoadMappingsFromJson(json);
    CHECK(loaded);

    HidDeviceMapping* reloaded = FindMapping(1121, 16193);
    CHECK(reloaded != nullptr);
    if (reloaded) {
        CHECK(reloaded->keyMapCount == keyEntries.size());
        bool allMatch = true;
        for (size_t j = 0; j < keyEntries.size() && j < reloaded->keyMapCount; j++) {
            if (reloaded->keyMap[j].code != keyEntries[j].code ||
                reloaded->keyMap[j].action != keyEntries[j].action) {
                allMatch = false;
            }
        }
        CHECK(allMatch);
        std::printf("  reloaded %d key entries, all match original\n", reloaded->keyMapCount);
    }

    ClearDynamicMappings();
}

// Exercises the new profiles/deadzone/sensitivityCurve schema (mapping.h /
// mapping.cpp) end to end through the real JSON parser+serializer - same
// spirit as TestJsonRoundTrip: compiled and run for real, not eyeballed.
static void TestProfilesDeadzoneCurveRoundTrip() {
    std::printf("TestProfilesDeadzoneCurveRoundTrip\n");
    ClearDynamicMappings();

    const char* input = R"JSON(
[
  {
    "vid": 1121,
    "pid": 20021,
    "mouseSensitivity": 10000,
    "invertMouseY": false,
    "deadzone": 1500,
    "sensitivityCurve": { "type": "exponential", "exponent": 1.6 },
    "buttons": [ { "idx": 0, "field": "start" } ],
    "profiles": [
      {
        "titleId": 1234567890,
        "gameName": "Tomb Raider",
        "mouseSensitivity": 18000,
        "deadzone": 3000,
        "sensitivityCurve": { "type": "linear", "exponent": 1.0 },
        "buttons": [ { "idx": 0, "field": "a_button" } ]
      },
      {
        "titleId": 987654321
      }
    ]
  }
]
)JSON";

    bool loaded = LoadMappingsFromJson(input);
    CHECK(loaded);

    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (!m) return;

    CHECK(m->deadzone == 1500);
    CHECK(m->sensitivityCurveType == CURVE_EXPONENTIAL);
    CHECK(m->sensitivityCurveExponent > 1.59f && m->sensitivityCurveExponent < 1.61f);
    CHECK(m->profileCount == 2);

    if (m->profileCount == 2) {
        const HidDeviceProfile& p0 = m->profiles[0];
        CHECK(p0.titleId == 1234567890u);
        CHECK(std::string(p0.gameName) == "Tomb Raider");
        CHECK(p0.mouseSensitivity == 18000);
        CHECK(p0.deadzone == 3000);
        CHECK(p0.curveType == CURVE_LINEAR);
        CHECK(p0.buttonMapCount == 1);
        if (p0.buttonMapCount == 1) {
            CHECK(p0.buttonMap[0].idx == 0);
            CHECK(p0.buttonMap[0].field == &ButtonsReport::a_button);
        }

        // Second profile supplies only titleId - every other field must read
        // back as "inherit" (the sentinel values), not silently become 0
        // and get misread as an explicit override.
        const HidDeviceProfile& p1 = m->profiles[1];
        CHECK(p1.titleId == 987654321u);
        CHECK(std::string(p1.gameName).empty()); // pas de "gameName" dans le JSON -> chaine vide, pas de garbage
        CHECK(p1.mouseSensitivity == 0);
        CHECK(p1.invertMouseY == -1);
        CHECK(p1.deadzone == -1);
        CHECK(p1.curveType == -1);
        CHECK(p1.buttonMapCount == 0);
    }

    // Round-trip: serialize back out and reload, everything must survive
    // identically.
    std::string json = SaveMappingsToJson();
    CHECK(json.find("\"profiles\"") != std::string::npos);
    CHECK(json.find("\"deadzone\"") != std::string::npos);
    CHECK(json.find("\"sensitivityCurve\"") != std::string::npos);
    CHECK(json.find("\"gameName\"") != std::string::npos);
    CHECK(json.find("Tomb Raider") != std::string::npos);

    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(json));

    HidDeviceMapping* reloaded = FindMapping(1121, 20021);
    CHECK(reloaded != nullptr);
    if (reloaded) {
        CHECK(reloaded->deadzone == 1500);
        CHECK(reloaded->sensitivityCurveType == CURVE_EXPONENTIAL);
        CHECK(reloaded->profileCount == 2);
        if (reloaded->profileCount == 2) {
            CHECK(reloaded->profiles[0].titleId == 1234567890u);
            CHECK(std::string(reloaded->profiles[0].gameName) == "Tomb Raider");
            CHECK(reloaded->profiles[0].mouseSensitivity == 18000);
            CHECK(reloaded->profiles[1].titleId == 987654321u);
            CHECK(reloaded->profiles[1].deadzone == -1);
        }
        std::printf("  profiles/deadzone/curve survive a full JSON round trip\n");
    }

    ClearDynamicMappings();
}

// --- Jalon 7 : resolution des reglages effectifs (device + profil) et courbe
// de sensibilite - logique pure ajoutee dans mapping.cpp/.h, jamais consultee
// au runtime avant ce jalon (voir PROJECT_NOTES.md, section "Jalon 7"). Ne
// passe pas par le JSON ici (deja couvert par TestProfilesDeadzoneCurveRoundTrip
// ci-dessus) - construit des HidDeviceMapping/HidDeviceProfile a la main pour
// isoler FindActiveProfile/ResolveEffectiveMouseSettings/ApplySensitivityCurve.
static void TestFindActiveProfile() {
    std::printf("TestFindActiveProfile\n");

    HidDeviceProfile profiles[2] = {};
    profiles[0].titleId = 1111;
    profiles[0].mouseSensitivity = 5000;
    profiles[0].invertMouseY = -1;
    profiles[0].deadzone = -1;
    profiles[0].curveType = -1;
    profiles[1].titleId = 2222;
    profiles[1].mouseSensitivity = 7000;
    profiles[1].invertMouseY = -1;
    profiles[1].deadzone = -1;
    profiles[1].curveType = -1;

    HidDeviceMapping map = {};
    map.vendorId = 1;
    map.productId = 2;
    map.profiles = profiles;
    map.profileCount = 2;

    CHECK(FindActiveProfile(&map, 1111) == &profiles[0]);
    CHECK(FindActiveProfile(&map, 2222) == &profiles[1]);
    CHECK(FindActiveProfile(&map, 9999) == nullptr); // aucun profil pour ce jeu
    CHECK(FindActiveProfile(&map, 0) == nullptr);     // 0 = pas de jeu en cours (dashboard)
    CHECK(FindActiveProfile(nullptr, 1111) == nullptr);

    HidDeviceMapping mapNoProfiles = {};
    CHECK(FindActiveProfile(&mapNoProfiles, 1111) == nullptr);

    std::printf("  profil trouve par titleId, ou nullptr si aucun/0/map nul\n");
}

static void TestResolveEffectiveMouseSettings() {
    std::printf("TestResolveEffectiveMouseSettings\n");

    // Cas 1 : pas de profil pour ce jeu -> les reglages du device s'appliquent
    // tels quels (comportement identique a avant ce jalon).
    HidDeviceMapping map = {};
    map.mouseSensitivity = 10000;
    map.invertMouseY = false;
    map.deadzone = 500;
    map.sensitivityCurveType = CURVE_LINEAR;
    map.sensitivityCurveExponent = 0.0f;

    EffectiveMouseSettings none = ResolveEffectiveMouseSettings(&map, 4242);
    CHECK(none.mouseSensitivity == 10000);
    CHECK(none.invertMouseY == false);
    CHECK(none.deadzone == 500);
    CHECK(none.sensitivityCurveType == CURVE_LINEAR);

    // Cas 2 : profil qui surcharge TOUT.
    HidDeviceProfile fullOverride = {};
    fullOverride.titleId = 4242;
    fullOverride.mouseSensitivity = 25000;
    fullOverride.invertMouseY = 1;
    fullOverride.deadzone = 2000;
    fullOverride.curveType = CURVE_EXPONENTIAL;
    fullOverride.curveExponent = 2.0f;
    map.profiles = &fullOverride;
    map.profileCount = 1;

    EffectiveMouseSettings full = ResolveEffectiveMouseSettings(&map, 4242);
    CHECK(full.mouseSensitivity == 25000);
    CHECK(full.invertMouseY == true);
    CHECK(full.deadzone == 2000);
    CHECK(full.sensitivityCurveType == CURVE_EXPONENTIAL);
    CHECK(full.sensitivityCurveExponent > 1.99f && full.sensitivityCurveExponent < 2.01f);

    // Cas 3 : profil qui ne surcharge QUE la sensibilite (tout le reste a sa
    // sentinelle "inherit") -> les autres champs doivent rester ceux du
    // device, pas retomber a 0/false/CURVE_LINEAR par erreur.
    HidDeviceProfile partialOverride = {};
    partialOverride.titleId = 4242;
    partialOverride.mouseSensitivity = 15000;
    partialOverride.invertMouseY = -1;
    partialOverride.deadzone = -1;
    partialOverride.curveType = -1;
    map.profiles = &partialOverride;

    EffectiveMouseSettings partial = ResolveEffectiveMouseSettings(&map, 4242);
    CHECK(partial.mouseSensitivity == 15000);   // du profil
    CHECK(partial.invertMouseY == false);       // herite du device
    CHECK(partial.deadzone == 500);             // herite du device
    CHECK(partial.sensitivityCurveType == CURVE_LINEAR); // herite du device

    // Cas 4 : jeu different de celui du profil -> reglages du device tels quels.
    EffectiveMouseSettings otherGame = ResolveEffectiveMouseSettings(&map, 9999);
    CHECK(otherGame.mouseSensitivity == 10000);
    CHECK(otherGame.deadzone == 500);

    std::printf("  override complet, override partiel (sentinelles heritees), et aucun profil : tous corrects\n");
}

static void TestApplySensitivityCurve() {
    std::printf("TestApplySensitivityCurve\n");

    // CURVE_LINEAR (ou exponent <= 0) doit etre un no-op strict - c'est le
    // comportement par defaut existant, ne doit JAMAIS changer une valeur
    // deja calculee par le pipeline lineaire actuel.
    CHECK(ApplySensitivityCurve(12345, CURVE_LINEAR, 2.0f) == 12345);
    CHECK(ApplySensitivityCurve(-12345, CURVE_LINEAR, 2.0f) == -12345);
    CHECK(ApplySensitivityCurve(5000, CURVE_EXPONENTIAL, 0.0f) == 5000); // exponent invalide -> no-op

    // Extremes exacts inchanges (1^exposant == 1 quel que soit l'exposant).
    CHECK(ApplySensitivityCurve(32767, CURVE_EXPONENTIAL, 2.0f) == 32767);
    CHECK(ApplySensitivityCurve(-32768, CURVE_EXPONENTIAL, 2.0f) >= -32768); // clamp, pas de depassement

    // Signe toujours preserve.
    int32_t curvedPos = ApplySensitivityCurve(16000, CURVE_EXPONENTIAL, 2.0f);
    int32_t curvedNeg = ApplySensitivityCurve(-16000, CURVE_EXPONENTIAL, 2.0f);
    CHECK(curvedPos > 0);
    CHECK(curvedNeg < 0);
    CHECK(curvedPos == -curvedNeg || (curvedPos + curvedNeg >= -1 && curvedPos + curvedNeg <= 1)); // symetrique (a l'arrondi pres)

    // Exposant > 1 attenue les petits mouvements (plus de precision pres du
    // centre) : une valeur a mi-course doit ressortir PLUS PETITE qu'en
    // lineaire, pas plus grande ni inchangee.
    int32_t half = 16383; // ~moitie de 32767
    int32_t curvedHalf = ApplySensitivityCurve(half, CURVE_EXPONENTIAL, 2.0f);
    CHECK(curvedHalf < half);
    CHECK(curvedHalf > 0); // attenue, mais pas ecrase a 0

    std::printf("  lineaire = no-op, exponentielle attenue le centre en preservant le signe et les extremes\n");
}

// Jalon 7 (suite, 2026-08-02) - SetMouseProfile est desormais partage entre
// application/config_writer.cpp (ecran Profils) ET X360Remap/main.cpp
// directement (raccourci clavier "sauvegarder depuis le jeu") - teste ici
// aussi, en memoire pure (pas de fichier), pour couvrir le point de vue
// X360Remap.xex sans dependre des tests d'application/tests/.
static void TestSetMouseProfileCreatesAndUpdates() {
    std::printf("TestSetMouseProfileCreatesAndUpdates\n");
    ClearDynamicMappings();

    CHECK(SetMouseProfile(1121, 20021, 555u, 12000, false, 200, CURVE_LINEAR, 0.0f));
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->profileCount == 1);
        if (m->profileCount == 1) {
            CHECK(m->profiles[0].titleId == 555u);
            CHECK(m->profiles[0].mouseSensitivity == 12000);
            CHECK(std::string(m->profiles[0].gameName).empty()); // pas de nom fourni -> vide, pas garbage
        }
    }

    // Meme titleId, nouvelles valeurs -> remplace en place, pas de doublon
    // (exactement le comportement deja teste via ApplyMouseProfile, mais ici
    // en appelant directement la fonction partagee, sans passer par le
    // fichier - c'est ce chemin que X360Remap/main.cpp emprunte).
    CHECK(SetMouseProfile(1121, 20021, 555u, 30000, true, 0, CURVE_EXPONENTIAL, 2.5f));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->profileCount == 1);
        if (m->profileCount == 1) {
            CHECK(m->profiles[0].mouseSensitivity == 30000);
            CHECK(m->profiles[0].invertMouseY == 1);
            CHECK(m->profiles[0].curveType == CURVE_EXPONENTIAL);
        }
    }

    // Un nom fourni est enregistre...
    CHECK(SetMouseProfile(1121, 20021, 555u, 30000, true, 0, CURVE_EXPONENTIAL, 2.5f, "Tomb Raider"));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1) {
        CHECK(std::string(m->profiles[0].gameName) == "Tomb Raider");
    }

    // ...et un appel ulterieur SANS nom (gameName == nullptr, le cas du
    // raccourci F9 - voir X360Remap/main.cpp) NE DOIT PAS l'effacer : F9 n'a
    // aucun moyen de saisir un nom depuis le jeu, il ne doit donc pas ecraser
    // celui deja saisi depuis l'appli.
    CHECK(SetMouseProfile(1121, 20021, 555u, 15000, false, 100, CURVE_LINEAR, 0.0f));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1) {
        CHECK(std::string(m->profiles[0].gameName) == "Tomb Raider");
        CHECK(m->profiles[0].mouseSensitivity == 15000); // les autres champs, eux, sont bien mis a jour
    }

    CHECK(!SetMouseProfile(1121, 20021, 0u, 10000, false, 0, CURVE_LINEAR, 0.0f)); // titleId 0 rejete

    std::printf("  creation, remplacement en place, gameName preserve si absent, titleId 0 rejete - tous corrects\n");
    ClearDynamicMappings();
}

// --- Jalon 7 (suite, 2026-08-02) : buttonMap/keyMap PAR PROFIL, jusqu'ici
// jamais consultes au runtime bien que deja dans le schema JSON - retour
// utilisateur reel (associer RB au clic droit uniquement pour un jeu donne).
static void TestResolveEffectiveButtonMap() {
    std::printf("TestResolveEffectiveButtonMap\n");

    HidButtonMapEntry deviceButtons[1] = {};
    deviceButtons[0].idx = 0;
    deviceButtons[0].field = &ButtonsReport::r2; // defaut "logique" habituel (clic gauche -> RT)

    HidDeviceMapping map = {};
    map.buttonMap = deviceButtons;
    map.buttonMapCount = 1;

    // Cas 1 : pas de profil pour ce jeu -> le buttonMap du DEVICE s'applique
    // tel quel (comportement inchange).
    uint8_t count = 0xFF;
    const HidButtonMapEntry* eff = ResolveEffectiveButtonMap(&map, 4242, &count);
    CHECK(count == 1);
    CHECK(eff == deviceButtons);

    // Cas 2 : un profil pour CE jeu avec son propre buttonMap -> remplace
    // ENTIEREMENT celui du device (pas de fusion champ par champ, voir le
    // commentaire de la declaration dans mapping.h).
    HidButtonMapEntry profileButtons[1] = {};
    profileButtons[0].idx = 1; // clic droit
    profileButtons[0].field = &ButtonsReport::r1; // RB

    HidDeviceProfile profile = {};
    profile.titleId = 4242;
    profile.buttonMap = profileButtons;
    profile.buttonMapCount = 1;
    map.profiles = &profile;
    map.profileCount = 1;

    count = 0xFF;
    eff = ResolveEffectiveButtonMap(&map, 4242, &count);
    CHECK(count == 1);
    CHECK(eff == profileButtons);
    CHECK(eff[0].idx == 1);
    CHECK(eff[0].field == &ButtonsReport::r1);

    // Cas 3 : meme profil mais un AUTRE jeu -> retombe sur le device (le
    // profil ne s'applique pas hors du jeu vise).
    count = 0xFF;
    eff = ResolveEffectiveButtonMap(&map, 9999, &count);
    CHECK(count == 1);
    CHECK(eff == deviceButtons);

    // Cas 4 : profil pour ce jeu mais SANS buttonMap propre (count == 0) ->
    // retombe aussi sur le device - un profil qui ne configure QUE la
    // sensibilite ne doit pas silencieusement vider les boutons.
    profile.buttonMap = nullptr;
    profile.buttonMapCount = 0;
    count = 0xFF;
    eff = ResolveEffectiveButtonMap(&map, 4242, &count);
    CHECK(count == 1);
    CHECK(eff == deviceButtons);

    // map nul -> 0/nullptr, pas de crash.
    count = 0xFF;
    eff = ResolveEffectiveButtonMap(nullptr, 4242, &count);
    CHECK(count == 0);
    CHECK(eff == nullptr);

    std::printf("  profil (s'il configure ses boutons) prefere au device, sinon device inchange\n");
}

// Regression du 2026-08-03 : un profil ne doit JAMAIS faire disparaitre les
// commandes qu'il ne redefinit pas. Avant ce correctif, la liaison rapide
// (qui n'ecrit qu'une entree de profil) rendait muets tous les autres clics
// et toutes les autres touches pour le jeu concerne - l'utilisateur a cru
// avoir perdu sa configuration clavier alors qu'elle etait intacte dans le
// fichier, simplement masquee en jeu.
static void TestMergedMapsOverrideWithoutErasing() {
    std::printf("TestMergedMapsOverrideWithoutErasing\n");

    HidButtonMapEntry devButtons[3];
    devButtons[0].idx = 0; devButtons[0].field = &ButtonsReport::a_button;
    devButtons[1].idx = 1; devButtons[1].field = &ButtonsReport::r1;
    devButtons[2].idx = 3; devButtons[2].field = &ButtonsReport::dpad_up;

    HidKeyMapEntry devKeys[3];
    devKeys[0].code = 26; devKeys[0].action = KEYACT_A;
    devKeys[1].code = 22; devKeys[1].action = KEYACT_B;
    devKeys[2].code = 4;  devKeys[2].action = KEYACT_X;

    // Le profil ne redefinit QU'UNE entree de chaque type, comme le fait la
    // liaison rapide.
    HidButtonMapEntry profButtons[1];
    profButtons[0].idx = 0; profButtons[0].field = &ButtonsReport::l1;
    HidKeyMapEntry profKeys[1];
    profKeys[0].code = 26; profKeys[0].action = KEYACT_Y;

    HidDeviceProfile profile = {};
    profile.titleId = 4242;
    profile.buttonMap = profButtons; profile.buttonMapCount = 1;
    profile.keyMap = profKeys;       profile.keyMapCount = 1;

    HidDeviceMapping map = {};
    map.buttonMap = devButtons; map.buttonMapCount = 3;
    map.keyMap = devKeys;       map.keyMapCount = 3;
    map.profiles = &profile;    map.profileCount = 1;

    HidButtonMapEntry mb[MAX_MERGED_BUTTONS];
    uint8_t nb = ResolveMergedButtonMap(&map, 4242, mb, MAX_MERGED_BUTTONS);
    CHECK(nb == 3); // surcharge de idx 0, les idx 1 et 3 SURVIVENT
    for (uint8_t i = 0; i < nb; i++) {
        if (mb[i].idx == 0) CHECK(mb[i].field == &ButtonsReport::l1);   // surcharge
        if (mb[i].idx == 1) CHECK(mb[i].field == &ButtonsReport::r1);   // preserve
        if (mb[i].idx == 3) CHECK(mb[i].field == &ButtonsReport::dpad_up); // preserve (molette)
    }

    HidKeyMapEntry mk[MAX_MERGED_KEYS];
    uint8_t nk = ResolveMergedKeyMap(&map, 4242, mk, MAX_MERGED_KEYS);
    CHECK(nk == 3); // les 2 autres touches ne disparaissent pas
    for (uint8_t i = 0; i < nk; i++) {
        if (mk[i].code == 26) CHECK(mk[i].action == KEYACT_Y); // surcharge
        if (mk[i].code == 22) CHECK(mk[i].action == KEYACT_B); // preserve
        if (mk[i].code == 4)  CHECK(mk[i].action == KEYACT_X); // preserve
    }

    // Une entree de profil sur un idx/code inconnu du device s'AJOUTE.
    profButtons[0].idx = 7;
    nb = ResolveMergedButtonMap(&map, 4242, mb, MAX_MERGED_BUTTONS);
    CHECK(nb == 4);

    // Hors du jeu concerne, le device s'applique tel quel.
    nb = ResolveMergedButtonMap(&map, 9999, mb, MAX_MERGED_BUTTONS);
    CHECK(nb == 3);
    nk = ResolveMergedKeyMap(&map, 9999, mk, MAX_MERGED_KEYS);
    CHECK(nk == 3);
    CHECK(ResolveMergedButtonMap(nullptr, 4242, mb, MAX_MERGED_BUTTONS) == 0);

    std::printf("  le profil surcharge sans effacer, ajoute si nouveau, et n'agit que sur son jeu\n");
}

static void TestResolveEffectiveKeyMap() {
    std::printf("TestResolveEffectiveKeyMap\n");

    HidKeyMapEntry deviceKeys[1] = {};
    deviceKeys[0].code = 0x1A; // W
    deviceKeys[0].action = KEYACT_LSTICK_UP;

    HidDeviceMapping map = {};
    map.keyMap = deviceKeys;
    map.keyMapCount = 1;

    uint8_t count = 0xFF;
    const HidKeyMapEntry* eff = ResolveEffectiveKeyMap(&map, 4242, &count);
    CHECK(count == 1);
    CHECK(eff == deviceKeys);

    HidKeyMapEntry profileKeys[1] = {};
    profileKeys[0].code = 0x2C; // Espace
    profileKeys[0].action = KEYACT_A;

    HidDeviceProfile profile = {};
    profile.titleId = 4242;
    profile.keyMap = profileKeys;
    profile.keyMapCount = 1;
    map.profiles = &profile;
    map.profileCount = 1;

    count = 0xFF;
    eff = ResolveEffectiveKeyMap(&map, 4242, &count);
    CHECK(count == 1);
    CHECK(eff == profileKeys);
    CHECK(eff[0].code == 0x2C);
    CHECK(eff[0].action == KEYACT_A);

    count = 0xFF;
    eff = ResolveEffectiveKeyMap(&map, 9999, &count);
    CHECK(count == 1);
    CHECK(eff == deviceKeys);

    std::printf("  profil (s'il configure ses touches) prefere au device, sinon device inchange\n");
}

static void TestSetProfileButtonOverrideCreatesAndUpdates() {
    std::printf("TestSetProfileButtonOverrideCreatesAndUpdates\n");
    ClearDynamicMappings();

    // Cree un profil (avec ses reglages numeriques deja poses) AVANT de lier
    // un bouton - reproduit l'ordre reel : sensibilite/deadzone configures
    // depuis l'appli, puis un bouton lie a la volee depuis le jeu (F3).
    CHECK(SetMouseProfile(1121, 20021, 4242u, 15000, false, 0, CURVE_LINEAR, 0.0f, "Test"));

    CHECK(SetProfileButtonOverride(1121, 20021, 4242u, 1, &ButtonsReport::r1)); // clic droit -> RB
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1) {
        CHECK(m->profiles[0].buttonMapCount == 1);
        if (m->profiles[0].buttonMapCount == 1) {
            CHECK(m->profiles[0].buttonMap[0].idx == 1);
            CHECK(m->profiles[0].buttonMap[0].field == &ButtonsReport::r1);
        }
        // Les reglages numeriques/le nom poses avant ne doivent pas avoir
        // disparu - SetProfileButtonOverride ne doit toucher QUE le bouton.
        CHECK(m->profiles[0].mouseSensitivity == 15000);
        CHECK(std::string(m->profiles[0].gameName) == "Test");
    }

    // Meme idx -> remplace en place, pas de doublon.
    CHECK(SetProfileButtonOverride(1121, 20021, 4242u, 1, &ButtonsReport::l1));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1) {
        CHECK(m->profiles[0].buttonMapCount == 1);
        if (m->profiles[0].buttonMapCount == 1)
            CHECK(m->profiles[0].buttonMap[0].field == &ButtonsReport::l1);
    }

    // Un idx DIFFERENT s'ajoute a cote, ne remplace pas le premier.
    CHECK(SetProfileButtonOverride(1121, 20021, 4242u, 2, &ButtonsReport::r3));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1)
        CHECK(m->profiles[0].buttonMapCount == 2);

    CHECK(!SetProfileButtonOverride(1121, 20021, 0u, 1, &ButtonsReport::r1)); // titleId 0 rejete

    std::printf("  creation, remplacement en place par idx, ajout d'un 2e idx, reste du profil preserve\n");
    ClearDynamicMappings();
}

static void TestSetProfileKeyOverrideCreatesAndUpdates() {
    std::printf("TestSetProfileKeyOverrideCreatesAndUpdates\n");
    ClearDynamicMappings();

    CHECK(SetProfileKeyOverride(1121, 20021, 4242u, 0x2C, KEYACT_RB)); // Espace -> RB
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1) {
        CHECK(m->profiles[0].keyMapCount == 1);
        if (m->profiles[0].keyMapCount == 1) {
            CHECK(m->profiles[0].keyMap[0].code == 0x2C);
            CHECK(m->profiles[0].keyMap[0].action == KEYACT_RB);
        }
        // Profil cree A LA VOLEE (pas via SetMouseProfile au prealable) -
        // les autres champs doivent porter leurs sentinelles "herite", pas
        // des zeros qui ressembleraient a des valeurs explicites (voir le
        // commentaire de PrepareProfileOverrideTarget dans mapping.cpp).
        CHECK(m->profiles[0].mouseSensitivity == 0);
        CHECK(m->profiles[0].invertMouseY == -1);
        CHECK(m->profiles[0].deadzone == -1);
        CHECK(m->profiles[0].curveType == -1);
    }

    // Meme code -> remplace en place.
    CHECK(SetProfileKeyOverride(1121, 20021, 4242u, 0x2C, KEYACT_LB));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr && m->profileCount == 1);
    if (m && m->profileCount == 1) {
        CHECK(m->profiles[0].keyMapCount == 1);
        if (m->profiles[0].keyMapCount == 1)
            CHECK(m->profiles[0].keyMap[0].action == KEYACT_LB);
    }

    CHECK(!SetProfileKeyOverride(1121, 20021, 0u, 0x2C, KEYACT_RB)); // titleId 0 rejete

    std::printf("  creation avec sentinelles correctes, remplacement en place par code, titleId 0 rejete\n");
    ClearDynamicMappings();
}

// --- Jalon 7 (suite) : historique des jeux vus (X360Remap/known_titles.h/.cpp) ---
static void TestParseKnownTitleIdsBasic() {
    std::printf("TestParseKnownTitleIdsBasic\n");

    std::vector<uint32_t> ids = ParseKnownTitleIds("4D5308BC\n1234ABCD\n");
    CHECK(ids.size() == 2);
    if (ids.size() == 2) {
        CHECK(ids[0] == 0x4D5308BCu);
        CHECK(ids[1] == 0x1234ABCDu);
    }

    std::printf("  2 titleId hex parses correctement\n");
}

static void TestParseKnownTitleIdsToleratesJunk() {
    std::printf("TestParseKnownTitleIdsToleratesJunk\n");

    // Lignes vides, ligne avec \r final (fichier ecrit/relu entre outils
    // differents), ligne totalement invalide - aucune ne doit faire echouer
    // le parsing des lignes valides autour.
    std::vector<uint32_t> ids = ParseKnownTitleIds("AAAAAAAA\r\n\n\nzzz-pas-du-hex\nBBBBBBBB\n");
    CHECK(ids.size() == 2);
    if (ids.size() == 2) {
        CHECK(ids[0] == 0xAAAAAAAAu);
        CHECK(ids[1] == 0xBBBBBBBBu);
    }

    CHECK(ParseKnownTitleIds("").empty());

    std::printf("  lignes vides/invalides ignorees proprement, pas de crash ni de perte des lignes valides\n");
}

static void TestSerializeKnownTitleIdsRoundTrip() {
    std::printf("TestSerializeKnownTitleIdsRoundTrip\n");

    std::vector<uint32_t> ids;
    ids.push_back(0x11223344u);
    ids.push_back(0x00000001u); // titleId a un seul chiffre significatif - le zero-padding doit survivre

    std::string serialized = SerializeKnownTitleIds(ids);
    CHECK(serialized.find("11223344") != std::string::npos);
    CHECK(serialized.find("00000001") != std::string::npos); // pas juste "1"

    std::vector<uint32_t> reloaded = ParseKnownTitleIds(serialized);
    CHECK(reloaded.size() == 2);
    if (reloaded.size() == 2) {
        CHECK(reloaded[0] == ids[0]);
        CHECK(reloaded[1] == ids[1]);
    }

    std::printf("  serialisation/reparsing symetriques, zero-padding preserve\n");
}

static void TestAddKnownTitleId() {
    std::printf("TestAddKnownTitleId\n");

    std::vector<uint32_t> ids;
    CHECK(AddKnownTitleId(&ids, 0x11111111u));  // nouveau -> true
    CHECK(ids.size() == 1);
    CHECK(!AddKnownTitleId(&ids, 0x11111111u)); // deja present -> false, pas de doublon
    CHECK(ids.size() == 1);
    CHECK(AddKnownTitleId(&ids, 0x22222222u));  // 2e jeu -> true
    CHECK(ids.size() == 2);
    CHECK(!AddKnownTitleId(&ids, 0u));          // 0 = hors d'un jeu -> toujours rejete
    CHECK(ids.size() == 2);
    CHECK(!AddKnownTitleId(nullptr, 0x33333333u)); // pointeur nul -> false, pas de crash

    std::printf("  ajout evite les doublons, rejette 0 et un pointeur nul\n");
}

// --- Jalon 8 (suite) : historique des peripheriques vus (X360Remap/known_devices.h/.cpp) ---
static void TestParseKnownDevicesBasic() {
    std::printf("TestParseKnownDevicesBasic\n");

    std::vector<KnownDeviceEntry> devices = ParseKnownDevices("0461:3F41:K\n0461:4E35:M\n");
    CHECK(devices.size() == 2);
    if (devices.size() == 2) {
        CHECK(devices[0].vid == 0x0461u);
        CHECK(devices[0].pid == 0x3F41u);
        CHECK(!devices[0].isMouse);
        CHECK(devices[1].vid == 0x0461u);
        CHECK(devices[1].pid == 0x4E35u);
        CHECK(devices[1].isMouse);
    }

    std::printf("  clavier et souris parses correctement, type distingue\n");
}

static void TestParseKnownDevicesToleratesJunk() {
    std::printf("TestParseKnownDevicesToleratesJunk\n");

    // Lignes vides, \r final, type invalide (ni M ni K), pas assez de ':' -
    // aucune ne doit faire echouer le parsing des lignes valides autour.
    std::vector<KnownDeviceEntry> devices = ParseKnownDevices(
        "0461:3F41:K\r\n\n\npas-du-tout-le-bon-format\n0461:4E35:X\n1234:5678:M\n");
    CHECK(devices.size() == 2);
    if (devices.size() == 2) {
        CHECK(devices[0].vid == 0x0461u);
        CHECK(devices[0].pid == 0x3F41u);
        CHECK(devices[1].vid == 0x1234u);
        CHECK(devices[1].pid == 0x5678u);
        CHECK(devices[1].isMouse);
    }

    CHECK(ParseKnownDevices("").empty());

    std::printf("  lignes vides/invalides ignorees proprement, pas de crash ni de perte des lignes valides\n");
}

static void TestSerializeKnownDevicesRoundTrip() {
    std::printf("TestSerializeKnownDevicesRoundTrip\n");

    std::vector<KnownDeviceEntry> devices;
    KnownDeviceEntry kb; kb.vid = 0x0461; kb.pid = 0x3F41; kb.isMouse = false;
    KnownDeviceEntry ms; ms.vid = 0x0461; ms.pid = 0x4E35; ms.isMouse = true;
    devices.push_back(kb);
    devices.push_back(ms);

    std::string serialized = SerializeKnownDevices(devices);
    CHECK(serialized.find("0461:3F41:K") != std::string::npos);
    CHECK(serialized.find("0461:4E35:M") != std::string::npos);

    std::vector<KnownDeviceEntry> reloaded = ParseKnownDevices(serialized);
    CHECK(reloaded.size() == 2);
    if (reloaded.size() == 2) {
        CHECK(reloaded[0].vid == kb.vid && reloaded[0].pid == kb.pid && reloaded[0].isMouse == kb.isMouse);
        CHECK(reloaded[1].vid == ms.vid && reloaded[1].pid == ms.pid && reloaded[1].isMouse == ms.isMouse);
    }

    std::printf("  serialisation/reparsing symetriques\n");
}

// Nom lisible ajoute le 2026-08-03 (remarque de l'utilisateur : afficher un
// VID:PID a quelqu'un qui veut choisir sa souris n'a aucun sens). Le format
// gagne un 4e champ OPTIONNEL - les fichiers ecrits par une version anterieure
// doivent rester lisibles tels quels.
static void TestKnownDeviceNames() {
    std::printf("TestKnownDeviceNames\n");

    // Ancien format, sans nom : toujours lu correctement.
    std::vector<KnownDeviceEntry> old = ParseKnownDevices("0461:4E35:M\n0461:3F41:K\n");
    CHECK(old.size() == 2);
    if (old.size() == 2) {
        CHECK(old[0].name[0] == '\0');
        CHECK(old[0].isMouse);
        CHECK(!old[1].isMouse);
    }

    // Nouveau format, avec nom - y compris avec des espaces.
    std::vector<KnownDeviceEntry> withNames =
        ParseKnownDevices("046A:B092:M:Logitech USB Mouse\n0461:3F41:K:Dell Keyboard\n");
    CHECK(withNames.size() == 2);
    if (withNames.size() == 2) {
        CHECK(std::string(withNames[0].name) == "Logitech USB Mouse");
        CHECK(std::string(withNames[1].name) == "Dell Keyboard");
    }

    // Aller-retour complet.
    std::string ser = SerializeKnownDevices(withNames);
    std::vector<KnownDeviceEntry> back = ParseKnownDevices(ser);
    CHECK(back.size() == 2);
    if (back.size() == 2)
        CHECK(std::string(back[0].name) == "Logitech USB Mouse");

    // Un peripherique deja connu SANS nom doit pouvoir en recevoir un : c'est
    // le cas d'un fichier ecrit avant cette version, qu'on complete au premier
    // rebranchement plutot que de laisser un matricule a l'ecran.
    std::vector<KnownDeviceEntry> upgrade = ParseKnownDevices("046A:B092:M\n");
    CHECK(upgrade.size() == 1);
    CHECK(AddKnownDevice(&upgrade, 0x046A, 0xB092, true, "Gaming Mouse")); // modifie -> true
    CHECK(upgrade.size() == 1);                                            // pas de doublon
    if (upgrade.size() == 1)
        CHECK(std::string(upgrade[0].name) == "Gaming Mouse");
    // Deuxieme passage : rien a changer, donc pas de reecriture du fichier.
    CHECK(!AddKnownDevice(&upgrade, 0x046A, 0xB092, true, "Gaming Mouse"));

    // Un nom contenant un ':' casserait le format - il doit etre neutralise.
    std::vector<KnownDeviceEntry> weird;
    CHECK(AddKnownDevice(&weird, 1, 2, true, "Bad:Name\nHere"));
    std::vector<KnownDeviceEntry> weirdBack = ParseKnownDevices(SerializeKnownDevices(weird));
    CHECK(weirdBack.size() == 1);
    if (weirdBack.size() == 1)
        CHECK(std::string(weirdBack[0].name).find(':') == std::string::npos);

    std::printf("  ancien format lu, nom round-trippe, completion d'une entree existante, ':' neutralise\n");
}

static void TestAddKnownDevice() {
    std::printf("TestAddKnownDevice\n");

    std::vector<KnownDeviceEntry> devices;
    CHECK(AddKnownDevice(&devices, 0x0461, 0x3F41, false)); // nouveau -> true
    CHECK(devices.size() == 1);
    CHECK(!AddKnownDevice(&devices, 0x0461, 0x3F41, false)); // deja present -> false, pas de doublon
    CHECK(devices.size() == 1);
    CHECK(AddKnownDevice(&devices, 0x0461, 0x4E35, true)); // 2e device (meme VID, PID different) -> true
    CHECK(devices.size() == 2);
    CHECK(!AddKnownDevice(nullptr, 0x1234, 0x5678, true)); // pointeur nul -> false, pas de crash

    std::printf("  ajout evite les doublons (cle vid+pid), pointeur nul gere proprement\n");
}

// Anti-zone morte (2026-09-25) : calcul, JSON aller-retour, heritage profil,
// et F9 (SetMouseProfile sans valeur) qui ne doit pas l'effacer.
static void TestAntiDeadzone() {
    std::printf("TestAntiDeadzone\n");
    int32_t x = 0, y = 0;
    ApplyAntiDeadzone(&x, &y, 8000, 8000);
    CHECK(x == 0 && y == 0);                     // repos : inchange
    x = 1; y = 0;
    ApplyAntiDeadzone(&x, &y, 8000, 8000);
    CHECK(x >= 8000 && x <= 8002 && y == 0);     // petit geste -> saute la zone morte
    x = -1; y = 0;
    ApplyAntiDeadzone(&x, &y, 8000, 8000);
    CHECK(x <= -8000 && x >= -8002);
    x = 32767; y = 0;
    ApplyAntiDeadzone(&x, &y, 8000, 8000);
    CHECK(x == 32767);                           // plein stick : inchange
    x = 100; y = 100;
    ApplyAntiDeadzone(&x, &y, 8000, 8000);
    CHECK(x == y && x > 5700 && x < 5760);       // diagonale conservee (~8107/sqrt2)
    x = 300; y = 0;
    ApplyAntiDeadzone(&x, &y, 0, 0);
    CHECK(x == 300);                             // 0 = desactivee
    x = 32767; y = 32767;
    ApplyAntiDeadzone(&x, &y, 8000, 8000);
    CHECK(x <= 32767 && y <= 32767 && x > 0);    // pas de debordement

    ClearDynamicMappings();
    const char* input = R"JSON([
  { "vid": 4660, "pid": 22136, "antiDeadzone": 6000,
    "profiles": [ { "titleId": 1111, "antiDeadzone": 9000 },
                  { "titleId": 2222, "mouseSensitivity": 5000 } ] }
])JSON";
    CHECK(LoadMappingsFromJson(input));
    HidDeviceMapping* m = FindMapping(4660, 22136);
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(m->antiDeadzone == 6000);
    CHECK(ResolveEffectiveMouseSettings(m, 0).antiDeadzone == 6000);
    CHECK(ResolveEffectiveMouseSettings(m, 1111).antiDeadzone == 9000);
    CHECK(ResolveEffectiveMouseSettings(m, 2222).antiDeadzone == 6000);   // herite

    std::string out = SaveMappingsToJson();
    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(out));
    m = FindMapping(4660, 22136);
    CHECK(m && m->antiDeadzone == 6000);
    CHECK(m && ResolveEffectiveMouseSettings(m, 1111).antiDeadzone == 9000);
    CHECK(m && ResolveEffectiveMouseSettings(m, 2222).antiDeadzone == 6000);

    // F9 (argument omis = -2) : garde la valeur du profil
    CHECK(SetMouseProfile(4660, 22136, 1111, 7000, false, 0, CURVE_LINEAR, 1.0f));
    m = FindMapping(4660, 22136);
    CHECK(m && ResolveEffectiveMouseSettings(m, 1111).antiDeadzone == 9000);
    // Studio : valeur explicite, puis -1 = herite
    CHECK(SetMouseProfile(4660, 22136, 1111, 7000, false, 0, CURVE_LINEAR, 1.0f, nullptr, 3000));
    m = FindMapping(4660, 22136);
    CHECK(m && ResolveEffectiveMouseSettings(m, 1111).antiDeadzone == 3000);
    CHECK(SetMouseProfile(4660, 22136, 1111, 7000, false, 0, CURVE_LINEAR, 1.0f, nullptr, -1));
    m = FindMapping(4660, 22136);
    CHECK(m && ResolveEffectiveMouseSettings(m, 1111).antiDeadzone == 6000);
    // Nouveau profil sans valeur : herite
    CHECK(SetMouseProfile(4660, 22136, 3333, 7000, false, 0, CURVE_LINEAR, 1.0f));
    m = FindMapping(4660, 22136);
    CHECK(m && ResolveEffectiveMouseSettings(m, 3333).antiDeadzone == 6000);
    ClearDynamicMappings();
}

// Disposition des sticks (2026-09-25) : permutations conformes aux ecrans de
// Call of Duty, et aller-retour JSON.
static void TestStickLayout() {
    std::printf("TestStickLayout\n");
    // standard : LX=pas de cote(1) LY=avancer(2) RX=tourner(3) RY=regarder(4)
    int16_t lx, ly, rx, ry;
    lx = 1; ly = 2; rx = 3; ry = 4; ApplyStickLayout(&lx, &ly, &rx, &ry, STICKS_DEFAULT);
    CHECK(lx == 1 && ly == 2 && rx == 3 && ry == 4);
    lx = 1; ly = 2; rx = 3; ry = 4; ApplyStickLayout(&lx, &ly, &rx, &ry, STICKS_SOUTHPAW);
    CHECK(lx == 3 && ly == 4 && rx == 1 && ry == 2);          // gauche : tourner/regarder
    lx = 1; ly = 2; rx = 3; ry = 4; ApplyStickLayout(&lx, &ly, &rx, &ry, STICKS_LEGACY);
    CHECK(lx == 3 && ly == 2 && rx == 1 && ry == 4);          // gauche X tourner, droite X pas de cote
    lx = 1; ly = 2; rx = 3; ry = 4; ApplyStickLayout(&lx, &ly, &rx, &ry, STICKS_LEGACY_SOUTHPAW);
    CHECK(lx == 1 && ly == 4 && rx == 3 && ry == 2);          // gauche Y regarder, droite Y avancer

    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(R"JSON([{ "vid": 1, "pid": 2, "profiles": [ { "titleId": 77, "stickLayout": 2 }, { "titleId": 78, "stickLayout": 9 } ] }])JSON"));
    HidDeviceMapping* m = FindMapping(1, 2);
    CHECK(m && FindActiveProfile(m, 77) && FindActiveProfile(m, 77)->stickLayout == STICKS_LEGACY);
    CHECK(m && FindActiveProfile(m, 78) && FindActiveProfile(m, 78)->stickLayout == STICKS_DEFAULT);  // hors bornes ignore
    // F9 (non fourni) garde la valeur ; la Studio la change
    CHECK(SetMouseProfile(1, 2, 77, 9000, false, 0, CURVE_LINEAR, 1.0f));
    m = FindMapping(1, 2);
    CHECK(m && FindActiveProfile(m, 77)->stickLayout == STICKS_LEGACY);
    CHECK(SetMouseProfile(1, 2, 77, 9000, false, 0, CURVE_LINEAR, 1.0f, nullptr, -2, STICKS_SOUTHPAW));
    std::string out = SaveMappingsToJson();
    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(out));
    m = FindMapping(1, 2);
    CHECK(m && FindActiveProfile(m, 77)->stickLayout == STICKS_SOUTHPAW);
    ClearDynamicMappings();
}

// Anti-zone morte H/V separees + lissage temporel (2026-09-26).
static void TestAxisAntiDeadzoneAndSmoothing() {
    std::printf("TestAxisAntiDeadzoneAndSmoothing\n");
    int32_t x = 0, y = 1;
    ApplyAntiDeadzone(&x, &y, 3000, 9000);
    CHECK(x == 0 && y >= 9000 && y <= 9002);         // vertical pur -> seuil vertical
    x = 1; y = 0;
    ApplyAntiDeadzone(&x, &y, 3000, 9000);
    CHECK(x >= 3000 && x <= 3002 && y == 0);         // horizontal pur -> seuil horizontal
    x = 2000; y = 30;                                 // geste horizontal avec leger tremblement
    ApplyAntiDeadzone(&x, &y, 3000, 9000);
    CHECK(y > 0 && y < 200);                          // le tremblement reste petit
    x = 0; y = -1;
    ApplyAntiDeadzone(&x, &y, 0, 7000);
    CHECK(y <= -7000);                                // H desactive, V actif

    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(R"JSON([{ "vid": 5, "pid": 6, "antiDeadzone": 3000, "smoothingMs": 30,
      "profiles": [ { "titleId": 10, "antiDeadzoneY": 8000, "smoothingMs": 0 }, { "titleId": 11 } ] }])JSON"));
    HidDeviceMapping* m = FindMapping(5, 6);
    CHECK(m != nullptr);
    if (!m) return;
    EffectiveMouseSettings e0 = ResolveEffectiveMouseSettings(m, 0);
    CHECK(e0.antiDeadzone == 3000 && e0.antiDeadzoneY == 3000 && e0.smoothingMs == 30);  // Y non regle = X
    EffectiveMouseSettings e10 = ResolveEffectiveMouseSettings(m, 10);
    CHECK(e10.antiDeadzone == 3000 && e10.antiDeadzoneY == 8000 && e10.smoothingMs == 0);
    EffectiveMouseSettings e11 = ResolveEffectiveMouseSettings(m, 11);
    CHECK(e11.antiDeadzoneY == 3000 && e11.smoothingMs == 30);
    std::string out = SaveMappingsToJson();
    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(out));
    m = FindMapping(5, 6);
    CHECK(m && ResolveEffectiveMouseSettings(m, 10).antiDeadzoneY == 8000);
    CHECK(m && ResolveEffectiveMouseSettings(m, 10).smoothingMs == 0);
    CHECK(m && ResolveEffectiveMouseSettings(m, 11).smoothingMs == 30);
    ClearDynamicMappings();

    // Lissage : jeu a 60 lectures/s, souris a 125 rapports/s -> 2 ou 3
    // "paquets" par lecture selon le moment (ici 16 puis 24 points,
    // alternes). Avec 34 ms de fenetre, la sortie devient reguliere (~20) et
    // le total est conserve.
    SmoothingState st;
    memset(&st, 0, sizeof(st));
    uint32_t t = 1000;
    int64_t inSum = 0, outSum = 0;
    int minOut = 1 << 30, maxOut = -(1 << 30);
    for (int i = 0; i < 60; i++) {
        int32_t dx = (i % 2) ? 24 : 16, dy = 0;
        inSum += dx;
        SmoothMouseDelta(&st, t, &dx, &dy, 34);
        outSum += dx;
        if (i > 3) { if (dx < minOut) minOut = dx; if (dx > maxOut) maxOut = dx; }
        t += 17;
    }
    for (int i = 0; i < 10; i++) { int32_t dx = 0, dy = 0; SmoothMouseDelta(&st, t, &dx, &dy, 34); outSum += dx; t += 17; }
    CHECK(maxOut - minOut <= 1);                     // 16/24 -> 20/20
    CHECK(outSum >= inSum - 1 && outSum <= inSum + 1);   // rotation conservee
    // desactive : inchange
    int32_t dx = 7, dy = -3;
    SmoothMouseDelta(&st, t, &dx, &dy, 0);
    CHECK(dx >= 6 && dx <= 8 && dy >= -4 && dy <= -2);
}

int main() {
    TestNormalSequence();
    TestHoldToSkip();
    TestConflictDetection();
    TestCancelMidway();
    TestJsonRoundTrip();
    TestProfilesDeadzoneCurveRoundTrip();
    TestFindActiveProfile();
    TestAntiDeadzone();
    TestStickLayout();
    TestAxisAntiDeadzoneAndSmoothing();
    TestResolveEffectiveMouseSettings();
    TestApplySensitivityCurve();
    TestSetMouseProfileCreatesAndUpdates();
    TestResolveEffectiveButtonMap();
    TestResolveEffectiveKeyMap();
    TestMergedMapsOverrideWithoutErasing();
    TestSetProfileButtonOverrideCreatesAndUpdates();
    TestSetProfileKeyOverrideCreatesAndUpdates();
    TestParseKnownTitleIdsBasic();
    TestParseKnownTitleIdsToleratesJunk();
    TestSerializeKnownTitleIdsRoundTrip();
    TestAddKnownTitleId();
    TestParseKnownDevicesBasic();
    TestParseKnownDevicesToleratesJunk();
    TestSerializeKnownDevicesRoundTrip();
    TestAddKnownDevice();
    TestKnownDeviceNames();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
}
