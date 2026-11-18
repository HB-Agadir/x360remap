// Portable test harness for the application.xex side: input_state.json
// reading + WizardSession + config_writer, compiled and run for real
// against the actual mapping.cpp used in production - same spirit as
// X360Remap/tests/test_mapping_assistant.cpp. Only main.cpp (the XUI screen
// itself) is untested here, since it can't be compiled or run off-console -
// see ARCHITECTURE.md.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "../../X360Remap/mapping.h"
#include "../config_writer.h"
#include "../input_state_reader.h"
#include "../wizard_session.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static void WriteFile(const char* path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

static void TestReadInputState() {
    std::printf("TestReadInputState\n");

    const char* path = "/tmp/test_input_state.json";
    WriteFile(path, R"JSON({ "keyCode": 82, "mouseButton": -1, "tick": 4242 })JSON");

    InputStateSnapshot snap = ReadInputState(path);
    CHECK(snap.valid);
    CHECK(snap.keyCode == 82);
    CHECK(snap.mouseButton == -1);
    CHECK(snap.tick == 4242u);
    // mouseDX/mouseDY/mouseButtonsMask absent from this JSON (older-shaped
    // payload, or a build of plugin.xex that predates the cursor fields) -
    // must fall back to the constructor defaults (0), not crash or leave
    // garbage - same "fails closed on missing fields" principle as a
    // missing file below.
    CHECK(snap.mouseDX == 0);
    CHECK(snap.mouseDY == 0);
    CHECK(snap.mouseButtonsMask == 0);

    // Missing file (e.g. plugin not loaded, or no keyboard/mouse connected -
    // see main.cpp, the plugin only writes this file when a boot-protocol
    // device is present) must come back invalid, not crash or fabricate data.
    InputStateSnapshot missing = ReadInputState("/tmp/does_not_exist_12345.json");
    CHECK(!missing.valid);
    CHECK(missing.keyCode == -1);
    CHECK(missing.mouseButton == -1);

    // Real payload shape written by plugin.xex since 2026-08-01 (cursor
    // fields, see X360Remap/main.cpp) - mouseDX/mouseDY can be negative
    // (motion left/up), mouseButtons is a raw bitmask (bit0=left set here).
    const char* pathCursor = "/tmp/test_input_state_cursor.json";
    WriteFile(pathCursor, R"JSON({ "keyCode": -1, "mouseButton": 0, "mouseDX": -7, "mouseDY": 3, "mouseButtons": 1, "tick": 5000 })JSON");
    InputStateSnapshot cursorSnap = ReadInputState(pathCursor);
    CHECK(cursorSnap.valid);
    CHECK(cursorSnap.mouseDX == -7);
    CHECK(cursorSnap.mouseDY == 3);
    CHECK(cursorSnap.mouseButtonsMask == 1);

    std::printf("  reads real values (including cursor fields), and fails closed on a missing file\n");
}

// Drives a full wizard session the way the XUI screen eventually will: one
// Tick() per simulated frame, feeding whatever ReadInputState() would have
// returned, until complete, then saves and verifies the file on disk is
// something plugin.xex's own LoadMappingsFromJson can read back correctly.
static void TestWizardSessionEndToEnd() {
    std::printf("TestWizardSessionEndToEnd\n");

    const char* configPath = "/tmp/test_x360remap_config.json";
    std::remove(configPath); // start from a clean slate

    WizardSession wizard(1121, 16193, DefaultKeyboardWizardSteps());
    CHECK(wizard.IsActive());

    // DefaultKeyboardWizardSteps() has 25 entries - assign each a distinct
    // HID code by pressing then releasing it, exactly like a real frame loop
    // reading input_state.json would drive it.
    int32_t code = 4; // starts at HID_KEY_A
    int stepsDone = 0;
    while (wizard.IsActive() && stepsDone < 30) {
        std::string prompt, message;
        wizard.Tick(code, &prompt, &message);       // press
        wizard.Tick(code, &prompt, &message);        // held one more tick
        wizard.Tick(MappingAssistant::NO_CODE, &prompt, &message); // release -> resolves
        code++;
        stepsDone++;
    }

    CHECK(wizard.IsComplete());
    CHECK(!wizard.WasCancelled());

    bool saved = wizard.SaveResult(configPath);
    CHECK(saved);

    // Verify with the SAME loader plugin.xex uses (LoadMappingsFromFile),
    // not by re-parsing the file by hand - if this passes, the file plugin.xex
    // reads at hot-reload really would pick this mapping up correctly.
    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));

    HidDeviceMapping* m = FindMapping(1121, 16193);
    CHECK(m != nullptr);
    if (m) {
        // 21, pas 25 - le stick droit a ete retire du wizard clavier par
        // defaut le 2026-08-02 (demande utilisateur : deja couvert par la
        // souris, pas besoin de le dupliquer sur le clavier).
        CHECK(m->keyMapCount == 21);
        if (m->keyMapCount == 21) {
            CHECK(m->keyMap[0].code == 4);
            CHECK(m->keyMap[0].action == KEYACT_A);
            CHECK(m->keyMap[20].code == 24); // 4 + 20
            CHECK(m->keyMap[20].action == KEYACT_LSTICK_RIGHT);
        }
        std::printf("  wizard result round-trips through the real plugin.xex loader (%d touches)\n", m->keyMapCount);
    }

    ClearDynamicMappings();
    std::remove(configPath);
}

// A device that already has a mapping in X360Remap.json (e.g. the mouse
// entries already there) must keep that entry - only the keyboard's own
// entry should be touched. This matters because ApplyKeyboardMapping loads
// the whole file, mutates one entry, and saves the whole file back out.
static void TestWizardPreservesOtherDeviceEntries() {
    std::printf("TestWizardPreservesOtherDeviceEntries\n");

    const char* configPath = "/tmp/test_x360remap_config_multi.json";
    WriteFile(configPath, R"JSON(
[
  { "vid": 1121, "pid": 20021, "mouseSensitivity": 10000,
    "buttons": [ { "idx": 0, "field": "start" } ] }
]
)JSON");

    WizardSession wizard(1121, 16193, {
        {"A", KEYACT_A},
        {"B", KEYACT_B},
    });

    wizard.Tick(4, nullptr, nullptr);
    wizard.Tick(4, nullptr, nullptr);
    wizard.Tick(MappingAssistant::NO_CODE, nullptr, nullptr);
    wizard.Tick(5, nullptr, nullptr);
    wizard.Tick(5, nullptr, nullptr);
    wizard.Tick(MappingAssistant::NO_CODE, nullptr, nullptr);

    CHECK(wizard.IsComplete());
    CHECK(wizard.SaveResult(configPath));

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));

    HidDeviceMapping* mouse = FindMapping(1121, 20021);
    CHECK(mouse != nullptr);
    if (mouse) {
        CHECK(mouse->mouseSensitivity == 10000);
        CHECK(mouse->buttonMapCount == 1);
    }

    HidDeviceMapping* keyboard = FindMapping(1121, 16193);
    CHECK(keyboard != nullptr);
    if (keyboard) {
        CHECK(keyboard->keyMapCount == 2);
    }

    std::printf("  mouse entry untouched, keyboard entry added alongside it\n");

    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestApplyMouseSettings() {
    std::printf("TestApplyMouseSettings\n");

    const char* configPath = "/tmp/test_mouse_settings.json";
    std::remove(configPath);

    CHECK(ApplyMouseSettings(1121, 20021, 18000, true, 2500, configPath));

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->mouseSensitivity == 18000);
        CHECK(m->invertMouseY == true);
        CHECK(m->deadzone == 2500);
    }

    // Second call with sensitivity<=0 must leave the existing value alone
    // (the "inherit" sentinel convention) rather than zeroing it out.
    CHECK(ApplyMouseSettings(1121, 20021, -1, false, -1, configPath));
    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->mouseSensitivity == 18000); // untouched
        CHECK(m->invertMouseY == false);      // bool has no "inherit" sentinel, always applied
        CHECK(m->deadzone == 2500);           // untouched
    }

    std::printf("  sensitivity/deadzone sentinels respected across two saves\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestConfigDefaultBackupRestore() {
    std::printf("TestConfigDefaultBackupRestore\n");

    const char* configPath = "/tmp/test_config_for_default.json";
    const char* backupPath = "/tmp/test_config_default_backup.json";
    std::remove(configPath);
    std::remove(backupPath);

    // Pas de defaut sauvegarde -> HasConfigDefault false, restore echoue
    // proprement et NE TOUCHE PAS a un configPath absent (ne doit rien
    // creer a partir de rien).
    CHECK(!HasConfigDefault(backupPath));
    CHECK(!RestoreConfigDefault(configPath, backupPath));

    WriteFile(configPath, R"JSON([{"vid":1121,"pid":20021,"mouseSensitivity":10000}])JSON");

    CHECK(SaveConfigAsDefault(configPath, backupPath));
    CHECK(HasConfigDefault(backupPath));

    // L'utilisateur modifie ensuite ses reglages...
    WriteFile(configPath, R"JSON([{"vid":1121,"pid":20021,"mouseSensitivity":25000}])JSON");

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) CHECK(m->mouseSensitivity == 25000);

    // ...puis restaure le defaut - doit retrouver exactement l'ancienne valeur.
    CHECK(RestoreConfigDefault(configPath, backupPath));

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) CHECK(m->mouseSensitivity == 10000);

    std::printf("  sauvegarde/restauration round-trip correctement, echec propre sans defaut prealable\n");

    ClearDynamicMappings();
    std::remove(configPath);
    std::remove(backupPath);
}

// Reproduit le bug reel du 2026-08-02 : DrawWizardTab() (application.xex)
// appelle Tick() une fois par frame ImGui (~60fps), pas une fois par 50ms
// comme l'assistant manette original pour lequel holdTicksToSkip=60 (le
// defaut de WizardSession) a ete calibre. Avec le defaut, tenir une touche
// 60 ticks la faisait sauter (skip) apres ~1s au lieu des "3 secondes"
// annoncees a l'ecran - un utilisateur relachant chaque touche un peu tard
// se retrouvait avec un wizard "termine" mais Results() presque vide.
static void TestWizardHoldTicksMatchesCallerCadence() {
    std::printf("TestWizardHoldTicksMatchesCallerCadence\n");

    // Avec le defaut (60, pense pour un appelant a 50ms/tick) : tenir 60
    // ticks avant de relacher declenche un skip, pas un Assigned - c'est
    // EXACTEMENT le bug quand l'appelant est en realite a ~16ms/tick (donc
    // "60 ticks" ne represente qu'~1s de temps reel, pas 3s).
    {
        WizardSession wizardOldDefault(1121, 16193, {
            {"A", KEYACT_A},
            {"B", KEYACT_B},
        });
        wizardOldDefault.Tick(4, nullptr, nullptr); // press
        for (int i = 0; i < 60; i++)
            wizardOldDefault.Tick(4, nullptr, nullptr); // held 60 ticks -> skip
        wizardOldDefault.Tick(MappingAssistant::NO_CODE, nullptr, nullptr); // release after skip

        CHECK(wizardOldDefault.ResultsCount() == 0); // rien assigne - la touche A a ete "sautee"
    }

    // Avec 180 (le nouveau reglage cote application.xex, voir main.cpp) : les
    // memes 60 ticks de maintien restent bien en dessous du seuil - la touche
    // se resout normalement en Assigned au relachement, pas de skip.
    {
        WizardSession wizardFixed(1121, 16193, {
            {"A", KEYACT_A},
            {"B", KEYACT_B},
        }, 180);
        wizardFixed.Tick(4, nullptr, nullptr); // press
        for (int i = 0; i < 60; i++)
            wizardFixed.Tick(4, nullptr, nullptr); // held 60 ticks - largement sous 180
        wizardFixed.Tick(MappingAssistant::NO_CODE, nullptr, nullptr); // release -> Assigned

        CHECK(wizardFixed.ResultsCount() == 1); // A bien assignee, pas sautee
        CHECK(wizardFixed.CurrentStepIndex() == 1);
    }

    std::printf("  60 ticks skip a 50ms/tick (defaut) mais pas a la cadence 60fps corrigee (180)\n");
}

static void TestApplyMouseButtonMapping() {
    std::printf("TestApplyMouseButtonMapping\n");

    const char* configPath = "/tmp/test_mouse_buttons.json";
    std::remove(configPath);

    std::vector<MouseButtonBinding> bindings;
    bindings.push_back({0, "l2"});       // clic gauche -> l2 (au lieu du defaut r2)
    bindings.push_back({1, "r2"});       // clic droit  -> r2 (au lieu du defaut l2)
    bindings.push_back({2, "a_button"}); // clic milieu -> A  (au lieu du defaut r3)
    bindings.push_back({9, "n_importe_quoi"}); // nom invalide - doit etre ignore, pas planter

    CHECK(ApplyMouseButtonMapping(1121, 20021, bindings, configPath));

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->buttonMapCount == 3); // le binding invalide a ete saute
        if (m->buttonMapCount == 3) {
            CHECK(GetButtonFieldName(m->buttonMap[0].field) != nullptr);
            CHECK(std::string(GetButtonFieldName(m->buttonMap[0].field)) == "l2");
            CHECK(std::string(GetButtonFieldName(m->buttonMap[1].field)) == "r2");
            CHECK(std::string(GetButtonFieldName(m->buttonMap[2].field)) == "a_button");
        }
    }

    std::printf("  3 boutons souris reassignes, le nom de champ invalide a ete ignore proprement\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

// idx 3/4 = convention "molette avant/arriere" (voir FindWheelOverride dans
// X360Remap/main.cpp, cote Xbox uniquement donc non testable ici) - ce test
// verifie juste que config_writer/mapping.cpp acceptent et round-trippent
// ces idx sans les traiter differemment de 0/1/2, puisque HidButtonMapEntry
// ne fait aucune distinction particuliere entre les indices.
static void TestApplyMouseButtonMappingWheelIndices() {
    std::printf("TestApplyMouseButtonMappingWheelIndices\n");

    const char* configPath = "/tmp/test_mouse_wheel.json";
    std::remove(configPath);

    std::vector<MouseButtonBinding> bindings;
    bindings.push_back({3, "x_button"}); // molette avant -> X
    bindings.push_back({4, "y_button"}); // molette arriere -> Y

    CHECK(ApplyMouseButtonMapping(1121, 20021, bindings, configPath));

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->buttonMapCount == 2);
        if (m->buttonMapCount == 2) {
            CHECK(m->buttonMap[0].idx == 3);
            CHECK(std::string(GetButtonFieldName(m->buttonMap[0].field)) == "x_button");
            CHECK(m->buttonMap[1].idx == 4);
            CHECK(std::string(GetButtonFieldName(m->buttonMap[1].field)) == "y_button");
        }
    }

    std::printf("  idx 3/4 (molette) round-trippent comme n'importe quel autre idx\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

// Les 4 directions du D-Pad ont ete ajoutees aux cibles proposees pour les
// clics/molette le 2026-08-03 (kMouseButtonTargets, application/main.cpp -
// non testable ici puisque dans main.cpp). Ce test verrouille la partie
// testable : que ces 4 noms de champ sont bien reconnus par
// GetButtonFieldPtr/GetButtonFieldName et round-trippent sur les idx molette
// (3/4). Si quelqu'un renomme un champ dans mapping.cpp sans mettre a jour
// kMouseButtonTargets, ce test tombe - alors que sur console le symptome
// serait juste "cette cible ne fait rien", silencieusement.
static void TestDpadTargetsRoundTrip() {
    std::printf("TestDpadTargetsRoundTrip\n");

    const char* configPath = "/tmp/test_dpad_targets.json";
    std::remove(configPath);

    const char* names[4] = {"dpad_up", "dpad_down", "dpad_left", "dpad_right"};
    for (int i = 0; i < 4; i++)
        CHECK(GetButtonFieldPtr(names[i]) != nullptr);

    std::vector<MouseButtonBinding> bindings;
    bindings.push_back({3, "dpad_left"});  // molette avant -> D-Pad Gauche
    bindings.push_back({4, "dpad_right"}); // molette arriere -> D-Pad Droite
    CHECK(ApplyMouseButtonMapping(1121, 20021, bindings, configPath));

    ClearDynamicMappings();
    CHECK(LoadMappingsFromFile(configPath));
    HidDeviceMapping* m = FindMapping(1121, 20021);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->buttonMapCount == 2);
        if (m->buttonMapCount == 2) {
            CHECK(std::string(GetButtonFieldName(m->buttonMap[0].field)) == "dpad_left");
            CHECK(std::string(GetButtonFieldName(m->buttonMap[1].field)) == "dpad_right");
        }
    }

    std::printf("  les 4 cibles D-Pad sont reconnues et round-trippent sur les idx molette\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

// Diagnostic ajoute le 2026-08-03 (voir PROJECT_NOTES.md, section molette) -
// verifie que GetMouseButtonMapCountOnDisk relit bien le disque (pas un
// etat en memoire perime) et distingue "jamais enregistre" (-1) de
// "enregistre avec 0 entree" (0, cas non exerce ici mais le contrat est
// documente dans config_writer.h).
static void TestGetMouseButtonMapCountOnDisk() {
    std::printf("TestGetMouseButtonMapCountOnDisk\n");

    const char* configPath = "/tmp/test_mouse_buttonmap_count.json";
    std::remove(configPath);

    CHECK(GetMouseButtonMapCountOnDisk(1121, 20021, configPath) == -1); // fichier absent

    std::vector<MouseButtonBinding> bindings;
    bindings.push_back({0, "start"});
    bindings.push_back({1, "a_button"});
    bindings.push_back({2, "b_button"});
    bindings.push_back({3, "l2"});
    bindings.push_back({4, "r2"});
    CHECK(ApplyMouseButtonMapping(1121, 20021, bindings, configPath));

    CHECK(GetMouseButtonMapCountOnDisk(1121, 20021, configPath) == 5);
    CHECK(GetMouseButtonMapCountOnDisk(1130, 45202, configPath) == -1); // autre device, jamais enregistre

    std::printf("  compte relu depuis le disque = 5, device inconnu = -1\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

// --- Jalon 7 : profils par jeu (application/ROADMAP_APPLICATION.md) -------
// ApplyMouseProfile/ListMouseProfiles/RemoveMouseProfile (config_writer.cpp)
// - meme esprit que TestApplyMouseButtonMapping ci-dessus : exerce le vrai
// code de production via un fichier temporaire, pas une reimplementation.
static void TestApplyMouseProfileCreatesAndReads() {
    std::printf("TestApplyMouseProfileCreatesAndReads\n");

    const char* configPath = "/tmp/test_mouse_profile_create.json";
    std::remove(configPath);

    CHECK(ApplyMouseProfile(1121, 20021, 1234567890u, 22000, true, 1800, CURVE_EXPONENTIAL, 1.8f, nullptr, configPath));

    std::vector<MouseProfileInfo> profiles = ListMouseProfiles(1121, 20021, configPath);
    CHECK(profiles.size() == 1);
    if (profiles.size() == 1) {
        CHECK(profiles[0].titleId == 1234567890u);
        CHECK(profiles[0].mouseSensitivity == 22000);
        CHECK(profiles[0].invertMouseY == 1);
        CHECK(profiles[0].deadzone == 1800);
        CHECK(profiles[0].curveType == CURVE_EXPONENTIAL);
        CHECK(profiles[0].curveExponent > 1.79f && profiles[0].curveExponent < 1.81f);
    }

    std::printf("  profil cree et relu correctement depuis le disque\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestApplyMouseProfileUpdatesExistingWithoutDuplicating() {
    std::printf("TestApplyMouseProfileUpdatesExistingWithoutDuplicating\n");

    const char* configPath = "/tmp/test_mouse_profile_update.json";
    std::remove(configPath);

    CHECK(ApplyMouseProfile(1121, 20021, 111u, 10000, false, 0, CURVE_LINEAR, 0.0f, nullptr, configPath));
    // Meme titleId, nouvelles valeurs - doit REMPLACER, pas ajouter une 2e entree.
    CHECK(ApplyMouseProfile(1121, 20021, 111u, 15000, true, 500, CURVE_EXPONENTIAL, 2.2f, nullptr, configPath));

    std::vector<MouseProfileInfo> profiles = ListMouseProfiles(1121, 20021, configPath);
    CHECK(profiles.size() == 1); // pas de doublon
    if (profiles.size() == 1) {
        CHECK(profiles[0].mouseSensitivity == 15000);
        CHECK(profiles[0].invertMouseY == 1);
        CHECK(profiles[0].deadzone == 500);
        CHECK(profiles[0].curveType == CURVE_EXPONENTIAL);
    }

    std::printf("  meme titleId => remplacement en place, pas de doublon\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestApplyMouseProfilePreservesOtherProfiles() {
    std::printf("TestApplyMouseProfilePreservesOtherProfiles\n");

    const char* configPath = "/tmp/test_mouse_profile_preserve.json";
    std::remove(configPath);

    CHECK(ApplyMouseProfile(1121, 20021, 111u, 10000, false, 0, CURVE_LINEAR, 0.0f, nullptr, configPath));
    CHECK(ApplyMouseProfile(1121, 20021, 222u, 20000, true, 1000, CURVE_LINEAR, 0.0f, nullptr, configPath));

    std::vector<MouseProfileInfo> profiles = ListMouseProfiles(1121, 20021, configPath);
    CHECK(profiles.size() == 2); // le profil 111 n'a pas ete efface en ajoutant le 222

    std::printf("  ajouter un profil pour un 2e jeu ne supprime pas celui du 1er\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestApplyMouseProfileRejectsTitleIdZero() {
    std::printf("TestApplyMouseProfileRejectsTitleIdZero\n");

    const char* configPath = "/tmp/test_mouse_profile_zero.json";
    std::remove(configPath);

    // 0 = "hors d'un jeu" (voir FindActiveProfile, mapping.h) - ne doit
    // jamais devenir une cle de profil valide, sans quoi les reglages
    // "hors jeu" (dashboard) seraient silencieusement pris pour un profil.
    CHECK(!ApplyMouseProfile(1121, 20021, 0u, 10000, false, 0, CURVE_LINEAR, 0.0f, nullptr, configPath));
    CHECK(ListMouseProfiles(1121, 20021, configPath).empty());

    std::printf("  titleId 0 rejete, aucun fichier/profil cree\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestRemoveMouseProfile() {
    std::printf("TestRemoveMouseProfile\n");

    const char* configPath = "/tmp/test_mouse_profile_remove.json";
    std::remove(configPath);

    CHECK(ApplyMouseProfile(1121, 20021, 111u, 10000, false, 0, CURVE_LINEAR, 0.0f, nullptr, configPath));
    CHECK(ApplyMouseProfile(1121, 20021, 222u, 20000, true, 1000, CURVE_LINEAR, 0.0f, nullptr, configPath));
    CHECK(RemoveMouseProfile(1121, 20021, 111u, configPath));

    std::vector<MouseProfileInfo> profiles = ListMouseProfiles(1121, 20021, configPath);
    CHECK(profiles.size() == 1);
    if (profiles.size() == 1) {
        CHECK(profiles[0].titleId == 222u); // seul celui vise a ete retire
    }

    CHECK(!RemoveMouseProfile(1121, 20021, 999u, configPath)); // profil inexistant -> false

    std::printf("  suppression ciblee, l'autre profil survit, cible inexistante => false\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

static void TestListKnownTitleIds() {
    std::printf("TestListKnownTitleIds\n");

    const char* path = "/tmp/test_known_titles.txt";
    std::remove(path);

    // Fichier absent -> liste vide, pas une erreur (aucun jeu lance encore).
    CHECK(ListKnownTitleIds(path).empty());

    std::ofstream out(path, std::ios::binary);
    out << "4D5308BC\n1234ABCD\n";
    out.close();

    std::vector<uint32_t> ids = ListKnownTitleIds(path);
    CHECK(ids.size() == 2);
    if (ids.size() == 2) {
        CHECK(ids[0] == 0x4D5308BCu);
        CHECK(ids[1] == 0x1234ABCDu);
    }

    std::printf("  liste vide si fichier absent, sinon relue correctement\n");
    std::remove(path);
}

// 2026-08-02 (suite Jalon 7) - retour utilisateur reel : le Title ID en hex
// seul n'est pas assez lisible pour reconnaitre un jeu ("Tomb Raider"). Verifie
// le meme cote application que TestSetMouseProfileCreatesAndUpdates cote
// plugin (X360Remap/tests/test_mapping_assistant.cpp) : un nom fourni est
// enregistre et relu, un appel ulterieur sans nom (gameName == nullptr) ne
// l'efface pas.
static void TestApplyMouseProfileGameName() {
    std::printf("TestApplyMouseProfileGameName\n");

    const char* configPath = "/tmp/test_mouse_profile_gamename.json";
    std::remove(configPath);

    CHECK(ApplyMouseProfile(1121, 20021, 0x4D5308BCu, 10000, false, 0, CURVE_LINEAR, 0.0f, "Tomb Raider", configPath));
    std::vector<MouseProfileInfo> profiles = ListMouseProfiles(1121, 20021, configPath);
    CHECK(profiles.size() == 1);
    if (profiles.size() == 1) {
        CHECK(std::string(profiles[0].gameName) == "Tomb Raider");
    }

    // Reglages modifies SANS repasser de nom (gameName == nullptr, le defaut)
    // - simule un enregistrement F9 depuis le jeu - le nom deja saisi cote
    // appli ne doit pas disparaitre.
    CHECK(ApplyMouseProfile(1121, 20021, 0x4D5308BCu, 25000, true, 1000, CURVE_LINEAR, 0.0f, nullptr, configPath));
    profiles = ListMouseProfiles(1121, 20021, configPath);
    CHECK(profiles.size() == 1);
    if (profiles.size() == 1) {
        CHECK(std::string(profiles[0].gameName) == "Tomb Raider"); // preserve
        CHECK(profiles[0].mouseSensitivity == 25000); // les autres champs, eux, sont bien mis a jour
    }

    std::printf("  nom enregistre, relu, et preserve par un appel ulterieur sans nom\n");
    ClearDynamicMappings();
    std::remove(configPath);
}

// Jalon 8 (application/ROADMAP_APPLICATION.md), corrige le 2026-08-03 -
// ListKnownDevices() lit maintenant known_devices.txt (X360Remap/known_devices.h),
// alimente par X360Remap.xex a la detection USB reelle - PAS une heuristique
// sur ce qui a ete configure via cette appli (premiere version, abandonnee le
// meme jour : ne listait rien pour un peripherique jamais touche par le
// wizard/reglages souris, bloquant des le premier vrai test avec une 2e
// souris - voir le commentaire de KnownDeviceInfo dans config_writer.h pour
// l'historique complet). Ce test verifie donc juste la lecture du fichier,
// pas de classification a testerici : la logique de parsing/serialisation
// elle-meme est deja testee cote plugin (X360Remap/tests/test_mapping_assistant.cpp,
// TestParseKnownDevicesBasic etc.) - une seule source de verite pour ce
// format, pas de logique dupliquee entre les deux suites de tests.
static void TestListKnownDevices() {
    std::printf("TestListKnownDevices\n");

    const char* path = "/tmp/test_known_devices.txt";
    std::remove(path);

    // Fichier absent -> liste vide, pas une erreur (aucun peripherique encore
    // vu par le plugin depuis l'installation).
    CHECK(ListKnownDevices(path).empty());

    std::ofstream out(path, std::ios::binary);
    out << "0461:3F41:K\n0461:4E35:M\n";
    out.close();

    std::vector<KnownDeviceInfo> devices = ListKnownDevices(path);
    CHECK(devices.size() == 2);
    if (devices.size() == 2) {
        CHECK(devices[0].vid == 0x0461);
        CHECK(devices[0].pid == 0x3F41);
        CHECK(!devices[0].isMouse);
        CHECK(devices[1].vid == 0x0461);
        CHECK(devices[1].pid == 0x4E35);
        CHECK(devices[1].isMouse);
    }

    // Type C = USB pad: isPad set, never a mouse.
    { std::ofstream o(path, std::ios::binary); o << "045E:028E:C:Pad One\n0461:3F41:K\n"; }
    devices = ListKnownDevices(path);
    CHECK(devices.size() == 2);
    if (devices.size() == 2) {
        CHECK(devices[0].isPad && !devices[0].isMouse && std::strcmp(devices[0].name, "Pad One") == 0);
        CHECK(!devices[1].isPad);
    }

    std::printf("  liste vide si fichier absent, sinon relue correctement (type exact, pas une heuristique)\n");
    std::remove(path);
}

// USB controller page: edit only an entry that already has axes, keep the axes.
static void TestPadConfig() {
    std::printf("TestPadConfig\n");
    const char* path = "/tmp/test_pad_config.json";
    WriteFile(path, R"JSON([
 {"vid":1234,"pid":1,"axes":[{"usage":48,"field":"x"},{"usage":49,"field":"y"}],
  "buttons":[{"idx":0,"field":"a_button"},{"idx":3,"field":"y_button"}]},
 {"vid":1234,"pid":2,"buttons":[{"idx":0,"field":"a_button"}]}
])JSON");
    PadConfig pc;
    ClearDynamicMappings();
    CHECK(ReadPadConfig(1234, 1, pc, path));
    CHECK(pc.hasEntry && pc.hasAxes && !pc.invertRY);
    CHECK(pc.btnIdx[0] == 0 && pc.btnIdx[3] == 3 && pc.btnIdx[1] == -1);

    int idx[PAD_CELLS];
    for (int i = 0; i < PAD_CELLS; i++) idx[i] = -1;
    idx[0] = 5; idx[1] = 0;           // A <- B6, B <- B1
    PadMacro mac[1];                              // B4 = A + B held; B4 was Y: must leave "buttons"
    mac[0].btn = 3; mac[0].stepCount = 1; mac[0].steps[0].mask = 3; mac[0].steps[0].ms = 0;
    idx[3] = 3;
    CHECK(ApplyPadButtons(1234, 1, idx, mac, 1, path));
    CHECK(ApplyPadStick(1234, 1, true, 900, path));
    ClearDynamicMappings();
    CHECK(ReadPadConfig(1234, 1, pc, path));
    CHECK(pc.hasAxes && pc.invertRY && pc.deadzone == 900);
    CHECK(pc.btnIdx[0] == 5 && pc.btnIdx[1] == 0 && pc.btnIdx[3] == -1);
    CHECK(pc.macroCount == 1 && pc.macros[0].btn == 3 && pc.macros[0].stepCount == 1);
    CHECK(pc.macros[0].steps[0].mask == 3 && pc.macros[0].steps[0].ms == 0);

    CHECK(!ApplyPadButtons(1234, 2, idx, mac, 1, path));   // entry without axes: refused
    CHECK(!ApplyPadButtons(1234, 9, idx, mac, 1, path));   // unknown pad: refused
    ClearDynamicMappings();
    CHECK(ReadPadConfig(1234, 2, pc, path) && !pc.hasAxes);
    ClearDynamicMappings();
    std::remove(path);

    // Sequences: save then read back (order, masks, durations, pause, held step), and
    // the old "fields" form is read as one held step.
    WriteFile(path, R"JSON([
 {"vid":1234,"pid":1,"axes":[{"usage":48,"field":"x"}],"buttons":[{"idx":0,"field":"a_button"}],
  "macros":[{"idx":4,"fields":["a_button","b_button"]}]}
])JSON");
    ClearDynamicMappings();
    CHECK(ReadPadConfig(1234, 1, pc, path));
    CHECK(pc.macroCount == 1 && pc.macros[0].btn == 4 && pc.macros[0].stepCount == 1 && pc.macros[0].steps[0].mask == 3);
    PadMacro seq[2];
    seq[0].btn = 6; seq[0].stepCount = 4;
    seq[0].steps[0].mask = 1u << 14; seq[0].steps[0].ms = 50;                       // D-down 50 ms
    seq[0].steps[1].mask = (1u << 14) | (1u << 16); seq[0].steps[1].ms = 33;        // D-down + D-right
    seq[0].steps[2].mask = 0;       seq[0].steps[2].ms = 100;                       // pause
    seq[0].steps[3].mask = 1u << 2; seq[0].steps[3].ms = 0;                         // X while held
    seq[1].btn = 7; seq[1].stepCount = 1; seq[1].steps[0].mask = 1u << 3; seq[1].steps[0].ms = 9000;   // clamped to 5000
    ClearDynamicMappings();
    CHECK(ApplyPadButtons(1234, 1, idx, seq, 2, path));
    ClearDynamicMappings();
    CHECK(ReadPadConfig(1234, 1, pc, path));
    CHECK(pc.macroCount == 2 && pc.macros[0].btn == 6 && pc.macros[0].stepCount == 4);
    CHECK(pc.macros[0].steps[0].mask == (1u << 14) && pc.macros[0].steps[0].ms == 50);
    CHECK(pc.macros[0].steps[1].mask == ((1u << 14) | (1u << 16)) && pc.macros[0].steps[1].ms == 33);
    CHECK(pc.macros[0].steps[2].mask == 0 && pc.macros[0].steps[2].ms == 100);
    CHECK(pc.macros[0].steps[3].mask == (1u << 2) && pc.macros[0].steps[3].ms == 0);
    CHECK(pc.macros[1].stepCount == 1 && pc.macros[1].steps[0].ms == 5000);
    CHECK(pc.btnIdx[0] == 5 && pc.btnIdx[1] == 0);       // plain buttons untouched
    std::remove(path);

    // 'c' entries (USB pads) and the optional padBtn mask of input_state.json
    const char* sp = "/tmp/test_pad_state.json";
    WriteFile(sp, R"JSON({ "keyCode": -1, "tick": 7, "devs": "k:046d:c33a;c:0079:0006", "padBtn": "0005" })JSON");
    InputStateSnapshot s = ReadInputState(sp);
    CHECK(s.valid && s.devCount == 2 && s.devType[1] == 'c' && s.devVid[1] == 0x79 && s.devPid[1] == 6);
    CHECK(s.hasPadBtn && s.padBtn == 5);
    std::remove(sp);
}

int main() {
    TestPadConfig();
    TestReadInputState();
    TestWizardSessionEndToEnd();
    TestWizardPreservesOtherDeviceEntries();
    TestApplyMouseSettings();
    TestConfigDefaultBackupRestore();
    TestWizardHoldTicksMatchesCallerCadence();
    TestApplyMouseButtonMapping();
    TestApplyMouseButtonMappingWheelIndices();
    TestDpadTargetsRoundTrip();
    TestGetMouseButtonMapCountOnDisk();
    TestApplyMouseProfileCreatesAndReads();
    TestApplyMouseProfileUpdatesExistingWithoutDuplicating();
    TestApplyMouseProfilePreservesOtherProfiles();
    TestApplyMouseProfileRejectsTitleIdZero();
    TestRemoveMouseProfile();
    TestListKnownTitleIds();
    TestApplyMouseProfileGameName();
    TestListKnownDevices();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("\n%d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
}
