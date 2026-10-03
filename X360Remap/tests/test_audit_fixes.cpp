// Tests des correctifs AUDIT_2026-09.md (C4, I4, I5) - logique pure, compilee
// et executee hors console avec g++, comme les autres tests de ce dossier.
//
// Commande (depuis X360Remap/) :
//   g++ -std=c++11 -fsanitize=address -I. -o test_audit tests/test_audit_fixes.cpp \
//       mapping.cpp mouse_calibration.cpp && ./test_audit
//
// -fsanitize=address n'est pas decoratif : c'est lui qui detecterait une
// lecture de memoire liberee dans TestRetiredGenerationStaysReadable.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include "../mapping.h"

static const char* kPath = "test_audit_config.json";

static void WriteRaw(const char* path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

static std::string ReadRaw(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool Exists(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return f.is_open();
}

static void Cleanup() {
    std::remove(kPath);
    std::remove((std::string(kPath) + ".tmp").c_str());
}

// C4 : la generation remplacee reste lisible jusqu'au rechargement suivant.
static void TestRetiredGenerationStaysReadable() {
    const std::string json =
        "[ { \"vid\": 1133, \"pid\": 49291, \"mouseSensitivity\": 12345,"
        "    \"buttons\": [ { \"idx\": 0, \"field\": \"r2\" }, { \"idx\": 1, \"field\": \"l2\" } ] } ]";
    assert(LoadMappingsFromJson(json));
    // Ce que tiendrait un callback USB au moment du rechargement : le mapping
    // ET le tableau de boutons qu'il est en train de parcourir.
    const HidDeviceMapping* held = FindMapping(1133, 49291);
    assert(held && held->mouseSensitivity == 12345 && held->buttonMapCount == 2);
    const HidButtonMapEntry* heldButtons = held->buttonMap;

    // Rechargement (sauvegarde depuis la Studio, F9...). Avec l'ancien
    // ClearDynamicMappings, le tableau de boutons etait libere ici : ASan
    // signale alors heap-use-after-free sur la lecture ci-dessous
    // (verifie le 2026-09-23 en recompilant ce test contre l'ancien code).
    assert(LoadMappingsFromJson(json));
    assert(held->mouseSensitivity == 12345);
    assert(heldButtons[0].idx == 0 && heldButtons[1].idx == 1);
    printf("TestRetiredGenerationStaysReadable\n  ancien pointeur lisible apres un rechargement\n");
}

// I5 : types incorrects -> entree ignoree, jamais de plantage.
static void TestWrongTypesAreIgnored() {
    const std::string json =
        "[ { \"vid\": 1, \"pid\": 2,"
        "    \"axes\":    [ { \"usage\": \"48\", \"field\": \"x\" }, { \"usage\": 49, \"field\": 7 } ],"
        "    \"buttons\": [ { \"idx\": \"0\", \"field\": \"a_button\" }, { \"idx\": 1, \"field\": false },"
        "                   { \"idx\": 2, \"field\": \"b_button\" } ],"
        "    \"invert\":  { \"x\": \"true\", \"y\": 1, \"z\": true },"
        "    \"profiles\": [ { \"titleId\": 42, \"buttons\": [ { \"idx\": \"3\", \"field\": \"x_button\" } ] } ] } ]";
    assert(LoadMappingsFromJson(json));
    const HidDeviceMapping* m = FindMapping(1, 2);
    assert(m);
    assert(m->axisMapCount == 0);          // les deux axes sont mal types
    assert(m->buttonMapCount == 1);        // seul idx 2 est valide
    assert(m->buttonMap[0].idx == 2);
    assert(m->invert.invertX == false);    // "true" en chaine : ignore
    assert(m->invert.invertY == false);    // 1 en entier : ignore
    assert(m->invert.invertZ == true);
    assert(m->profileCount == 1 && m->profiles[0].buttonMapCount == 0);
    printf("TestWrongTypesAreIgnored\n  entrees mal typees ignorees sans plantage\n");
}

// I4 : sauvegarde complete, pas de .tmp residuel.
static void TestAtomicSaveRoundTrip() {
    Cleanup();
    assert(LoadMappingsFromJson("[ { \"vid\": 10, \"pid\": 20, \"deadzone\": 500 } ]"));
    assert(SaveMappingsToFile(kPath));
    assert(Exists(kPath));
    assert(!Exists((std::string(kPath) + ".tmp").c_str()));

    ClearDynamicMappings();
    assert(LoadMappingsFromFile(kPath));
    assert(FindMapping(10, 20) && FindMapping(10, 20)->deadzone == 500);

    // Ecraser un fichier existant fonctionne aussi (remove + rename).
    assert(LoadMappingsFromJson("[ { \"vid\": 10, \"pid\": 20, \"deadzone\": 700 } ]"));
    assert(SaveMappingsToFile(kPath));
    ClearDynamicMappings();
    assert(LoadMappingsFromFile(kPath));
    assert(FindMapping(10, 20)->deadzone == 700);
    Cleanup();
    printf("TestAtomicSaveRoundTrip\n  ecriture via .tmp, relecture exacte, ecrasement ok\n");
}

// I4 : fichier present mais illisible -> refus, et le fichier n'est PAS touche.
static void TestUpdateRefusesUnreadableFile() {
    Cleanup();
    const std::string broken = "[ { \"vid\": 10, \"pid\": 20, ";   // virgule en trop, tronque
    WriteRaw(kPath, broken);
    assert(!LoadMappingsForUpdate(kPath));
    assert(ReadRaw(kPath) == broken);   // intact : c'est tout l'objet du correctif
    Cleanup();
    printf("TestUpdateRefusesUnreadableFile\n  fichier casse detecte, non ecrase\n");
}

// I4 : fichier absent -> autorise (premier lancement).
static void TestUpdateAllowsMissingFile() {
    Cleanup();
    assert(LoadMappingsForUpdate(kPath));
    printf("TestUpdateAllowsMissingFile\n  premier lancement autorise\n");
}

// I4 : coupure entre suppression et renommage -> .tmp recupere.
static void TestUpdateRecoversInterruptedSave() {
    Cleanup();
    WriteRaw((std::string(kPath) + ".tmp").c_str(), "[ { \"vid\": 77, \"pid\": 88, \"deadzone\": 321 } ]");
    assert(!Exists(kPath));
    assert(LoadMappingsForUpdate(kPath));
    assert(Exists(kPath));
    assert(FindMapping(77, 88) && FindMapping(77, 88)->deadzone == 321);
    Cleanup();
    printf("TestUpdateRecoversInterruptedSave\n  sauvegarde interrompue terminee au prochain acces\n");
}

// Resolution mesuree de la souris (2026-09-23) : lecture, bornes, aller-retour.
static void TestCountsPerCmRoundTrip() {
    Cleanup();
    assert(LoadMappingsFromJson(
        "[ { \"vid\": 1, \"pid\": 1, \"countsPerCm\": 629.9 },"
        "  { \"vid\": 2, \"pid\": 2, \"countsPerCm\": 5 },"          // mesure ratee : ignoree
        "  { \"vid\": 3, \"pid\": 3, \"countsPerCm\": \"630\" },"    // mauvais type : ignore
        "  { \"vid\": 4, \"pid\": 4 } ]"));                         // jamais mesuree
    assert(FindMapping(1, 1)->countsPerCm > 629.8f && FindMapping(1, 1)->countsPerCm < 630.0f);
    assert(FindMapping(2, 2)->countsPerCm == 0.0f);
    assert(FindMapping(3, 3)->countsPerCm == 0.0f);
    assert(FindMapping(4, 4)->countsPerCm == 0.0f);

    assert(SaveMappingsToFile(kPath));
    const std::string saved = ReadRaw(kPath);
    // Une souris jamais mesuree ne doit pas polluer le fichier.
    assert(saved.find("countsPerCm") == saved.rfind("countsPerCm"));
    ClearDynamicMappings();
    assert(LoadMappingsFromFile(kPath));
    assert(FindMapping(1, 1)->countsPerCm > 629.8f);
    assert(FindMapping(4, 4)->countsPerCm == 0.0f);
    Cleanup();
    printf("TestCountsPerCmRoundTrip\n  valeur relue, bornes et types respectes, rien d'ecrit si non mesuree\n");
}

// Revue du 2026-09-23 (perte de X360Remap.json chez HB) : la copie de
// secours repare un original CASSE, et ne touche jamais un original SAIN.
static void TestRecoverCorruptedOriginal() {
    Cleanup();
    const std::string tmpPath = std::string(kPath) + ".tmp";
    WriteRaw(kPath, "[ { \"vid\": 5, ");                                  // original casse
    WriteRaw(tmpPath.c_str(), "[ { \"vid\": 5, \"pid\": 6, \"deadzone\": 42 } ]");
    assert(RecoverConfigFromTmp(kPath));
    assert(!Exists(tmpPath.c_str()));
    assert(LoadMappingsFromFile(kPath) && FindMapping(5, 6)->deadzone == 42);

    // Original sain + copie perimee : l'original n'est PAS remplace.
    WriteRaw(tmpPath.c_str(), "[ { \"vid\": 9, \"pid\": 9 } ]");
    const std::string before = ReadRaw(kPath);
    assert(!RecoverConfigFromTmp(kPath));
    assert(ReadRaw(kPath) == before);
    assert(!Exists(tmpPath.c_str()));

    // Copie de secours elle-meme invalide : on ne touche a rien.
    WriteRaw(kPath, "[ casse");
    WriteRaw(tmpPath.c_str(), "[ aussi casse");
    assert(!RecoverConfigFromTmp(kPath));
    assert(ReadRaw(kPath) == "[ casse");
    Cleanup();
    printf("TestRecoverCorruptedOriginal\n  original casse repare, original sain intact, copie invalide ignoree\n");
}

int main() {
    TestRecoverCorruptedOriginal();
    TestCountsPerCmRoundTrip();
    TestRetiredGenerationStaysReadable();
    TestWrongTypesAreIgnored();
    TestAtomicSaveRoundTrip();
    TestUpdateRefusesUnreadableFile();
    TestUpdateAllowsMissingFile();
    TestUpdateRecoversInterruptedSave();
    printf("\nALL TESTS PASSED\n");
    return 0;
}
