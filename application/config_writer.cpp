#include "config_writer.h"
#include "../X360Remap/known_titles.h"
#include "../X360Remap/known_devices.h"
#include "../X360Remap/mouse_calibration.h" // IsLayoutUsable, pour refuser une disposition bancale
#include <memory>
#include <fstream>
#include <iterator>
#include <string.h>

// Sous-dossier dedie de l'appli plutot que la racine du disque dur - evite
// de polluer HDD:\ avec des fichiers qui n'ont de sens que pour cet outil.
// Le plugin (X360Remap/main.cpp) utilise le meme chemin en dur de son cote -
// voir le commentaire de MountHdd() dans application/main.cpp pour le rappel
// que ce n'est PAS un dossier prive (les deux .xex tournent dans des
// process/namespaces differents), juste une convention de rangement partagee.
static const char* kDefaultConfigPath = "HDD:\\X360RemapStudio\\X360Remap.json";
static const char* kDefaultBackupPath = "HDD:\\X360RemapStudio\\X360Remap_default.json";
// Meme chemin en dur que X360Remap/main.cpp (voir sa section
// "Jalon 7 (suite)" pres de la detection de changement de jeu) - simple
// convention de rangement partagee, pas de dossier prive (voir le
// commentaire de kDefaultConfigPath au-dessus).
static const char* kDefaultKnownTitlesPath = "HDD:\\X360RemapStudio\\known_titles.txt";
// Meme convention - ecrit uniquement par X360Remap.xex (Jalon 8, suite), lu
// en lecture seule ici (voir le commentaire de ListKnownDevices dans
// config_writer.h).
static const char* kDefaultKnownDevicesPath = "HDD:\\X360RemapStudio\\known_devices.txt";

bool ApplyKeyboardMapping(uint16_t vid, uint16_t pid, const std::vector<HidKeyMapEntry>& keys) {
    return ApplyKeyboardMapping(vid, pid, keys, kDefaultConfigPath);
}

bool ApplyKeyboardMapping(uint16_t vid, uint16_t pid, const std::vector<HidKeyMapEntry>& keys,
                           const char* configPath) {
    // Load whatever is on disk right now. A missing/unreadable file just
    // means we start from an empty mapping set - not an error worth failing
    // on, since the whole point of this call is to create/update an entry.
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;

    // Iterateur plutot que for-range (C++11/VS2012, non supporte par le
    // toolset XDK Xbox 360 - voir mapping_assistant.cpp pour l'explication
    // complete, meme correction appliquee ici).
    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }

    std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
    data->keyEntries = keys;

    if (existing) {
        existing->keyMap = data->keyEntries.data();
        existing->keyMapCount = static_cast<uint8_t>(data->keyEntries.size());
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.keyMap = data->keyEntries.data();
        fresh.keyMapCount = static_cast<uint8_t>(data->keyEntries.size());
        g_dynamicMappings.push_back(fresh);
    }

    // g_dynamicData owns the backing storage every HidDeviceMapping::keyMap
    // pointer in g_dynamicMappings points into (see mapping.h) - must stay
    // alive at least until SaveMappingsToFile has read out of it below.
    g_dynamicData.push_back(std::move(data));

    return SaveMappingsToFile(configPath);
}

bool ApplyKeyboardMappingEntry(uint16_t vid, uint16_t pid, const HidKeyMapEntry& entry) {
    return ApplyKeyboardMappingEntry(vid, pid, entry, kDefaultConfigPath);
}

bool ApplyKeyboardMappingEntry(uint16_t vid, uint16_t pid, const HidKeyMapEntry& entry,
                                const char* configPath) {
    // Charge l'existant AVANT de construire la liste fusionnee - meme ordre
    // que ApplyKeyboardMapping, necessaire pour que FindMapping voie le
    // device une fois LoadMappingsFromFile passe.
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;

    HidDeviceMapping* existing = FindMapping(vid, pid);
    std::vector<HidKeyMapEntry> merged;
    if (existing && existing->keyMap) {
        merged.assign(existing->keyMap, existing->keyMap + existing->keyMapCount);
    }
    // No layout of its own yet: start from the DEFAULT layout (2026-10-11,
    // tester with a Razer Tartarus). Writing just the one key made the file
    // hold a single entry, and that single entry was the whole keyboard.
    if (merged.empty()) {
        HidKeyMapEntry defs[64];
        const uint8_t nd = GetDefaultKeyMap(defs, 64);
        merged.assign(defs, defs + nd);
    }
    // The key now used for this action loses whatever it did before.
    for (std::vector<HidKeyMapEntry>::iterator it = merged.begin(); it != merged.end(); ) {
        if (it->code == entry.code && it->action != entry.action) it = merged.erase(it);
        else ++it;
    }

    // Remplace par ACTION (le bouton manette), pas par code clavier : c'est
    // "quelle touche fait Y desormais" qu'on reassigne, pas "que fait cette
    // touche precise" - une meme action ne doit jamais avoir deux entrees.
    bool replaced = false;
    for (std::vector<HidKeyMapEntry>::iterator it = merged.begin(); it != merged.end(); ++it) {
        if (it->action == entry.action) {
            *it = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        merged.push_back(entry);

    // Delegue l'ecriture reelle a ApplyKeyboardMapping : meme chemin de code
    // que le wizard sequentiel a partir d'ici, juste avec une liste
    // pre-fusionnee au lieu d'une liste fraiche.
    return ApplyKeyboardMapping(vid, pid, merged, configPath);
}

// Copie brute octet-par-octet - pas besoin de reparser le JSON, on ne fait
// que dupliquer le fichier tel quel. std::ifstream/ofstream en binaire pour
// ne rien perdre/transformer (pas de conversion de fin de ligne).
static bool CopyFileRaw(const char* fromPath, const char* toPath) {
    std::ifstream in(fromPath, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    std::ofstream out(toPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << in.rdbuf();
    return in.good() || in.eof();
}

bool SaveConfigAsDefault() {
    return SaveConfigAsDefault(kDefaultConfigPath, kDefaultBackupPath);
}

bool SaveConfigAsDefault(const char* configPath, const char* backupPath) {
    // "Sauvegarder les reglages actuels comme reglages par defaut", comme un
    // profil BIOS - copie l'etat courant de X360Remap.json vers un fichier a
    // part, sans y toucher tant qu'on ne demande pas explicitement de
    // restaurer.
    return CopyFileRaw(configPath, backupPath);
}

bool RestoreConfigDefault() {
    return RestoreConfigDefault(kDefaultConfigPath, kDefaultBackupPath);
}

bool RestoreConfigDefault(const char* configPath, const char* backupPath) {
    // Echoue proprement (false) si aucun defaut n'a jamais ete sauvegarde -
    // ne doit surtout pas creer un X360Remap.json vide/casse dans ce cas.
    return CopyFileRaw(backupPath, configPath);
}

bool RestoreFactoryConfig() {
    return RestoreFactoryConfig(kDefaultConfigPath);
}

bool RestoreFactoryConfig(const char* configPath) {
    std::string tmp = std::string(configPath) + ".tmp";
    CopyFileRaw(configPath, tmp.c_str());   // filet : absent = rien a sauver
    std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << "[]\n";
    out.close();
    return !out.fail();
}

bool HasConfigDefault() {
    return HasConfigDefault(kDefaultBackupPath);
}

bool HasConfigDefault(const char* backupPath) {
    std::ifstream in(backupPath, std::ios::binary);
    return in.is_open();
}

bool ApplyMouseButtonMapping(uint16_t vid, uint16_t pid, const std::vector<MouseButtonBinding>& bindings) {
    return ApplyMouseButtonMapping(vid, pid, bindings, kDefaultConfigPath);
}

bool ApplyMouseButtonMapping(uint16_t vid, uint16_t pid, const std::vector<MouseButtonBinding>& bindings,
                              const char* configPath) {
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;

    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }

    std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
    data->buttonEntries.reserve(bindings.size());
    for (std::vector<MouseButtonBinding>::const_iterator it = bindings.begin(); it != bindings.end(); ++it) {
        uint8_t ButtonsReport::* field = GetButtonFieldPtr(it->fieldName);
        if (!field)
            continue; // nom invalide - voir le commentaire de ApplyMouseButtonMapping dans config_writer.h
        HidButtonMapEntry entry;
        entry.idx = it->idx;
        entry.field = field;
        data->buttonEntries.push_back(entry);
    }

    if (existing) {
        existing->buttonMap = data->buttonEntries.data();
        existing->buttonMapCount = static_cast<uint8_t>(data->buttonEntries.size());
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.buttonMap = data->buttonEntries.data();
        fresh.buttonMapCount = static_cast<uint8_t>(data->buttonEntries.size());
        g_dynamicMappings.push_back(fresh);
    }

    g_dynamicData.push_back(std::move(data));

    return SaveMappingsToFile(configPath);
}

const char* const kPadFieldNames[PAD_CELLS] = {
    "a_button", "b_button", "x_button", "y_button", "l1", "r1", "l2", "r2", "l3", "r3",
    "start", "back", "xbox", "dpad_up", "dpad_down", "dpad_left", "dpad_right"
};

static HidDeviceMapping* FindDynamicMapping(uint16_t vid, uint16_t pid) {
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it)
        if (it->vendorId == vid && it->productId == pid) return &(*it);
    return nullptr;
}

typedef char PadStepsMatchPlugin[(PAD_STEPS_MAX_CFG == MAX_MACRO_STEPS && PAD_MACROS_MAX_CFG == MAX_MACROS) ? 1 : -1];

static uint32_t PadCellBit(int cell) { return MacroFieldBit(GetButtonFieldPtr(kPadFieldNames[cell])); }

bool ReadPadConfig(uint16_t vid, uint16_t pid, PadConfig& out) {
    return ReadPadConfig(vid, pid, out, kDefaultConfigPath);
}

bool ReadPadConfig(uint16_t vid, uint16_t pid, PadConfig& out, const char* configPath) {
    out.hasEntry = out.hasAxes = out.invertRY = false;
    out.deadzone = 0;
    out.macroCount = 0;
    for (int i = 0; i < PAD_CELLS; i++) out.btnIdx[i] = -1;
    LoadMappingsFromFile(configPath);
    HidDeviceMapping* m = FindDynamicMapping(vid, pid);
    if (!m) return false;
    out.hasEntry = true;
    out.hasAxes = m->axisMapCount > 0;
    out.invertRY = m->invert.invertRY;
    out.deadzone = m->deadzone;
    for (uint8_t b = 0; b < m->buttonMapCount; b++) {
        const char* name = GetButtonFieldName(m->buttonMap[b].field);
        for (int i = 0; i < PAD_CELLS; i++)
            if (name && strcmp(name, kPadFieldNames[i]) == 0 && out.btnIdx[i] < 0)
                out.btnIdx[i] = m->buttonMap[b].idx;
    }
    for (uint8_t k = 0; k < m->macroCount && out.macroCount < PAD_MACROS_MAX_CFG; k++) {
        const HidButtonMacro& mc = m->macros[k];
        PadMacro& pm = out.macros[out.macroCount];
        pm.btn = mc.idx; pm.stepCount = 0;
        for (int s = 0; s < mc.stepCount && pm.stepCount < PAD_STEPS_MAX_CFG; s++) {
            PadStep& st = pm.steps[pm.stepCount];
            st.mask = 0;
            for (int i = 0; i < PAD_CELLS; i++) if (PadCellBit(i) & mc.steps[s].mask) st.mask |= 1u << i;
            st.ms = mc.steps[s].ms;
            pm.stepCount++;
        }
        if (pm.stepCount > 0) out.macroCount++;
    }
    return true;
}

bool ApplyPadButtons(uint16_t vid, uint16_t pid, const int btnIdx[PAD_CELLS], const PadMacro* macros, int macroCount) {
    return ApplyPadButtons(vid, pid, btnIdx, macros, macroCount, kDefaultConfigPath);
}

bool ApplyPadButtons(uint16_t vid, uint16_t pid, const int btnIdx[PAD_CELLS], const PadMacro* macros, int macroCount, const char* configPath) {
    if (!LoadMappingsForUpdate(configPath))
        return false;
    HidDeviceMapping* m = FindDynamicMapping(vid, pid);
    if (!m || m->axisMapCount == 0)
        return false;   // see config_writer.h: no entry / no axes = do not touch

    std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
    if (macroCount > PAD_MACROS_MAX_CFG) macroCount = PAD_MACROS_MAX_CFG;
    for (int k = 0; k < macroCount && data->macroEntries.size() < MAX_MACROS; k++) {
        if (macros[k].btn < 0 || macros[k].btn > 31) continue;
        HidButtonMacro mc = {};
        mc.idx = (uint8_t)macros[k].btn;
        for (int s = 0; s < macros[k].stepCount && mc.stepCount < MAX_MACRO_STEPS; s++) {
            const PadStep& in = macros[k].steps[s];
            HidMacroStep st;
            st.mask = 0;
            for (int i = 0; i < PAD_CELLS; i++) if (in.mask & (1u << i)) st.mask |= PadCellBit(i);
            st.ms = (uint16_t)(in.ms <= 0 ? 0 : in.ms < 16 ? 16 : in.ms > 5000 ? 5000 : in.ms);
            if (st.mask == 0 && st.ms == 0) continue;   // a pause needs a duration
            mc.steps[mc.stepCount++] = st;
        }
        if (mc.stepCount > 0) data->macroEntries.push_back(mc);
    }
    for (int i = 0; i < PAD_CELLS; i++) {
        if (btnIdx[i] < 0 || btnIdx[i] > 31) continue;
        bool isMacro = false;   // a macro button is not also a plain button
        for (size_t k = 0; k < data->macroEntries.size(); k++) isMacro = isMacro || data->macroEntries[k].idx == btnIdx[i];
        if (isMacro) continue;
        HidButtonMapEntry e;
        e.idx = (uint8_t)btnIdx[i];
        e.field = GetButtonFieldPtr(kPadFieldNames[i]);
        if (e.field) data->buttonEntries.push_back(e);
    }
    m->buttonMap = data->buttonEntries.data();
    m->buttonMapCount = static_cast<uint8_t>(data->buttonEntries.size());
    m->macros = data->macroEntries.data();
    m->macroCount = static_cast<uint8_t>(data->macroEntries.size());
    g_dynamicData.push_back(std::move(data));
    return SaveMappingsToFile(configPath);
}

bool ApplyPadStick(uint16_t vid, uint16_t pid, bool invertRY, int deadzone) {
    return ApplyPadStick(vid, pid, invertRY, deadzone, kDefaultConfigPath);
}

bool ApplyPadStick(uint16_t vid, uint16_t pid, bool invertRY, int deadzone, const char* configPath) {
    if (!LoadMappingsForUpdate(configPath))
        return false;
    HidDeviceMapping* m = FindDynamicMapping(vid, pid);
    if (!m || m->axisMapCount == 0)
        return false;
    m->invert.invertRY = invertRY;
    m->deadzone = deadzone < 0 ? 0 : (deadzone > 32767 ? 32767 : deadzone);
    return SaveMappingsToFile(configPath);
}

int GetMouseButtonMapCountOnDisk(uint16_t vid, uint16_t pid) {
    return GetMouseButtonMapCountOnDisk(vid, pid, kDefaultConfigPath);
}

int GetMouseButtonMapCountOnDisk(uint16_t vid, uint16_t pid, const char* configPath) {
    LoadMappingsFromFile(configPath);
    HidDeviceMapping* m = FindMapping(vid, pid);
    if (!m)
        return -1;
    return static_cast<int>(m->buttonMapCount);
}

bool ApplyMouseReportLayout(uint16_t vid, uint16_t pid, const HidReportLayout& layout) {
    return ApplyMouseReportLayout(vid, pid, layout, kDefaultConfigPath);
}

bool ApplyMouseReportLayout(uint16_t vid, uint16_t pid, const HidReportLayout& layout,
                             const char* configPath) {
    // Refus net plutot qu'ecriture d'offsets bancals : le plugin lit ces
    // valeurs dans le contexte USB, une disposition incoherente le ferait lire
    // hors du paquet a chaque rapport.
    if (!IsLayoutUsable(layout))
        return false;

    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;

    HidDeviceMapping* existing = FindMapping(vid, pid);
    if (existing) {
        existing->reportLayout = layout;
        existing->reportLayout.valid = true;
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.reportLayout = layout;
        fresh.reportLayout.valid = true;
        g_dynamicMappings.push_back(fresh);
    }

    return SaveMappingsToFile(configPath);
}

bool ApplyMouseCountsPerCm(uint16_t vid, uint16_t pid, float countsPerCm) {
    return ApplyMouseCountsPerCm(vid, pid, countsPerCm, kDefaultConfigPath);
}

bool ApplyMouseCountsPerCm(uint16_t vid, uint16_t pid, float countsPerCm, const char* configPath) {
    // Memes bornes que la relecture (mapping.cpp) : une valeur que le plugin
    // ignorerait ne doit pas etre ecrite en faisant croire qu'elle compte.
    if (countsPerCm < 20.0f || countsPerCm > 10000.0f)
        return false;

    if (!LoadMappingsForUpdate(configPath))
        return false;

    // Recherche dans les SEULES tables dynamiques (pas FindMapping) : un
    // mapping statique ne serait jamais reecrit dans le fichier.
    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }
    if (existing) {
        existing->countsPerCm = countsPerCm;
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.countsPerCm = countsPerCm;
        g_dynamicMappings.push_back(fresh);
    }
    return SaveMappingsToFile(configPath);
}

bool ClearMouseReportLayout(uint16_t vid, uint16_t pid) {
    return ClearMouseReportLayout(vid, pid, kDefaultConfigPath);
}

bool ClearMouseReportLayout(uint16_t vid, uint16_t pid, const char* configPath) {
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;
    HidDeviceMapping* existing = FindMapping(vid, pid);
    if (!existing)
        return false;
    memset(&existing->reportLayout, 0, sizeof(existing->reportLayout));
    existing->reportLayout.valid = false;
    return SaveMappingsToFile(configPath);
}

bool ApplyMouseSettings(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone) {
    return ApplyMouseSettings(vid, pid, sensitivity, invertY, deadzone, kDefaultConfigPath);
}

bool ApplyMouseSettings(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone,
                         const char* configPath) {
    return ApplyMouseSettingsFull(vid, pid, sensitivity, invertY, deadzone, -1, configPath);
}

bool ApplyMouseSettings(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone,
                         int32_t antiDeadzone, int32_t antiDeadzoneY, int32_t smoothingMs) {
    return ApplyMouseSettingsFull(vid, pid, sensitivity, invertY, deadzone, antiDeadzone, kDefaultConfigPath,
                                  antiDeadzoneY, smoothingMs);
}

bool ApplyMouseSettingsFull(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone,
                             int32_t antiDeadzone, const char* configPath,
                             int32_t antiDeadzoneY, int32_t smoothingMs) {
    if (antiDeadzone > 32767) antiDeadzone = 32767;
    if (antiDeadzoneY > 32767) antiDeadzoneY = 32767;
    if (smoothingMs > 200) smoothingMs = 200;
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;

    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }

    if (existing) {
        if (sensitivity > 0) existing->mouseSensitivity = sensitivity;
        existing->invertMouseY = invertY;
        if (deadzone >= 0) existing->deadzone = deadzone;
        if (antiDeadzone >= 0) existing->antiDeadzone = antiDeadzone;
        if (antiDeadzoneY >= 0) existing->antiDeadzoneY = antiDeadzoneY;
        if (smoothingMs >= 0) existing->smoothingMs = smoothingMs;
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.mouseSensitivity = sensitivity > 0 ? sensitivity : 0;
        fresh.invertMouseY = invertY;
        fresh.deadzone = deadzone >= 0 ? deadzone : 0;
        fresh.antiDeadzone = antiDeadzone >= 0 ? antiDeadzone : 0;
        fresh.antiDeadzoneY = antiDeadzoneY >= 0 ? antiDeadzoneY : 0;
        fresh.smoothingMs = smoothingMs >= 0 ? smoothingMs : 0;
        g_dynamicMappings.push_back(fresh);
    }

    return SaveMappingsToFile(configPath);
}

std::vector<MouseProfileInfo> ListMouseProfiles(uint16_t vid, uint16_t pid) {
    return ListMouseProfiles(vid, pid, kDefaultConfigPath);
}

std::vector<MouseProfileInfo> ListMouseProfiles(uint16_t vid, uint16_t pid, const char* configPath) {
    std::vector<MouseProfileInfo> result;

    LoadMappingsFromFile(configPath);
    HidDeviceMapping* m = FindMapping(vid, pid);
    if (!m)
        return result;

    result.reserve(m->profileCount);
    for (uint8_t i = 0; i < m->profileCount; i++) {
        const HidDeviceProfile& p = m->profiles[i];
        MouseProfileInfo info;
        info.titleId = p.titleId;
        strncpy(info.gameName, p.gameName, sizeof(info.gameName) - 1);
        info.gameName[sizeof(info.gameName) - 1] = '\0';
        info.mouseSensitivity = p.mouseSensitivity;
        info.invertMouseY = p.invertMouseY;
        info.deadzone = p.deadzone;
        info.curveType = p.curveType;
        info.curveExponent = p.curveExponent;
        info.antiDeadzone = p.antiDeadzone;
        info.stickLayout = p.stickLayout;
        info.antiDeadzoneY = p.antiDeadzoneY;
        info.smoothingMs = p.smoothingMs;
        result.push_back(info);
    }
    return result;
}

bool ApplyMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                        int32_t sensitivity, bool invertY, int32_t deadzone,
                        uint8_t curveType, float curveExponent,
                        const char* gameName) {
    return ApplyMouseProfile(vid, pid, titleId, sensitivity, invertY, deadzone, curveType, curveExponent, gameName, kDefaultConfigPath);
}

bool ApplyMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                        int32_t sensitivity, bool invertY, int32_t deadzone,
                        uint8_t curveType, float curveExponent,
                        const char* gameName,
                        const char* configPath) {
    return ApplyMouseProfileFull(vid, pid, titleId, sensitivity, invertY, deadzone, curveType, curveExponent,
                                 gameName, -2 /* ne pas toucher */, configPath);
}

bool ApplyMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                        int32_t sensitivity, bool invertY, int32_t deadzone,
                        uint8_t curveType, float curveExponent,
                        const char* gameName, int32_t antiDeadzone,
                        int stickLayout, int32_t antiDeadzoneY, int32_t smoothingMs) {
    return ApplyMouseProfileFull(vid, pid, titleId, sensitivity, invertY, deadzone, curveType, curveExponent,
                                 gameName, antiDeadzone, kDefaultConfigPath, stickLayout, antiDeadzoneY, smoothingMs);
}

bool ApplyMouseProfileFull(uint16_t vid, uint16_t pid, uint32_t titleId,
                            int32_t sensitivity, bool invertY, int32_t deadzone,
                            uint8_t curveType, float curveExponent,
                            const char* gameName, int32_t antiDeadzone,
                            const char* configPath, int stickLayout,
                            int32_t antiDeadzoneY, int32_t smoothingMs) {
    if (antiDeadzone > 32767) antiDeadzone = 32767;
    // Delegue a SetMouseProfile (X360Remap/mapping.cpp) - meme logique
    // desormais partagee avec X360Remap.xex, qui l'appelle directement pour
    // le raccourci "sauvegarder le profil depuis le jeu" (Jalon 7, suite) -
    // une seule source de verite pour "qu'est-ce qu'enregistrer un profil
    // veut dire", pas de logique dupliquee a faire diverger entre les deux
    // .xex. Cette fonction reste le seul point qui fait le I/O disque
    // (Load/Save), SetMouseProfile ne manipule que la memoire.
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;
    if (!SetMouseProfile(vid, pid, titleId, sensitivity, invertY, deadzone, curveType, curveExponent, gameName, antiDeadzone, stickLayout,
                         antiDeadzoneY, smoothingMs))
        return false;
    return SaveMappingsToFile(configPath);
}

bool RemoveMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId) {
    return RemoveMouseProfile(vid, pid, titleId, kDefaultConfigPath);
}

std::vector<uint32_t> ListKnownTitleIds() {
    return ListKnownTitleIds(kDefaultKnownTitlesPath);
}

std::vector<uint32_t> ListKnownTitleIds(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return std::vector<uint32_t>(); // pas encore de fichier - liste vide, pas une erreur

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseKnownTitleIds(content);
}

std::vector<KnownDeviceInfo> ListKnownDevices() {
    return ListKnownDevices(kDefaultKnownDevicesPath);
}

std::vector<KnownDeviceInfo> ListKnownDevices(const char* knownDevicesPath) {
    std::vector<KnownDeviceInfo> result;

    std::ifstream in(knownDevicesPath, std::ios::binary);
    if (!in.is_open())
        return result; // pas encore de fichier - aucun device jamais vu par le plugin, liste vide (pas une erreur)

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<KnownDeviceEntry> entries = ParseKnownDevices(content);

    result.reserve(entries.size());
    for (std::vector<KnownDeviceEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
        KnownDeviceInfo info;
        info.vid = it->vid;
        info.pid = it->pid;
        info.isMouse = it->isMouse;
        info.isPad = it->isPad;
        size_t n = 0;
        for (; n < sizeof(info.name) - 1 && it->name[n] != '\0'; n++)
            info.name[n] = it->name[n];
        info.name[n] = '\0';
        result.push_back(info);
    }
    return result;
}

bool RemoveMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId, const char* configPath) {
    // AUDIT_2026-09.md (I4) : fichier present mais illisible -> on refuse
    // d'ecrire, au lieu d'ecraser la configuration avec l'etat en memoire.
    if (!LoadMappingsForUpdate(configPath))
        return false;

    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }
    if (!existing)
        return false; // device inconnu - rien a supprimer

    std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
    for (uint8_t i = 0; i < existing->profileCount; i++) {
        if (existing->profiles[i].titleId != titleId) {
            data->profileEntries.push_back(existing->profiles[i]);
        }
    }

    // BUG REEL attrape par TestRemoveMouseProfile (titleId inexistant) :
    // sans cette verification, la boucle ci-dessus recopie simplement TOUS
    // les profils inchanges quand aucun ne correspond a titleId, et la
    // fonction renvoyait quand meme true (SaveMappingsToFile reussit
    // toujours sur un contenu inchange) au lieu de signaler qu'il n'y avait
    // rien a supprimer.
    if (data->profileEntries.size() == existing->profileCount)
        return false; // aucun profil de ce titleId - rien retire, pas de resauvegarde inutile

    existing->profiles = data->profileEntries.empty() ? nullptr : data->profileEntries.data();
    existing->profileCount = static_cast<uint8_t>(data->profileEntries.size());

    g_dynamicData.push_back(std::move(data));

    return SaveMappingsToFile(configPath);
}
