#include "model_win.h"

#include <d3d11.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "../../application/aurora_library.h"
#include "../../application/config_writer.h"
#include "../../application/donation_links.h"
#include "../../application/glossy_assets.h"
#include "../../application/i18n.h"
#include "../../application/overlay_preview.h"
#include "../../application/theme.h"
#include "../../application/ui_glossy.h"
#include "../../application/ui_mapping.h"
#include "../../application/wizard_session.h"
#include "../../X360Remap/camera_learn.h"
#include "../../X360Remap/mapping.h"

#define STBI_NO_STDIO
#include "../../stb/stb_image.h"

#include "guide_win.h"
#include "preview_bg_win.h"
#include "pad_input_win.h"
#include "key_capture_win.h"
#include "platform_win.h"
#include "sync.h"
#include "win_i18n.h"

// Same number as the plugin and the console Studio (CONTRACT.md, "Version").
const char* const kWinAppVersion = "1.4.1-pre";

namespace {

ID3D11Device* g_device = nullptr;
UiSkinTex     g_glossyTex[GA_COUNT];
int           g_tab = 0;
bool          g_skipSplash = false;

// ============================================================================
// Working copy of X360Remap.json
//
// g_dynamicMappings (mapping.cpp) is the parsed file. It is reloaded when the
// file changes on disk. "External" changes (pull, open, restore) also reset
// the values being edited on screen; our own saves do not.
// ============================================================================
uint64_t g_cfgStamp = ~0ull;
uint32_t g_cfgGen = 1;        // any reload or own write
uint32_t g_externalGen = 1;   // the file was replaced from outside the pages

void EnsureConfigLoaded() {
    const std::string path = ModelConfigPath();
    const uint64_t stamp = plat::FileStamp(path);
    if (stamp == g_cfgStamp) return;
    g_cfgStamp = stamp;
    if (!LoadMappingsFromFile(path)) ClearDynamicMappings();
    g_cfgGen++;
    g_externalGen++;
}

// ============================================================================
// Devices: the ones the plugin has seen (known_devices.txt), plus the ones
// that only exist in X360Remap.json (a file opened without its console).
// ============================================================================
struct Device {
    uint16_t    vid, pid;
    bool        isMouse;
    std::string name, id;
};
std::vector<Device> g_keyboards, g_mice;
std::vector<KnownDeviceInfo> g_padNames;   // C lines of known_devices.txt (names of the USB controllers)
uint16_t g_kbVid = 0, g_kbPid = 0, g_msVid = 0, g_msPid = 0;
bool     g_kbValid = false, g_msValid = false;

void AddDevice(std::vector<Device>& list, uint16_t vid, uint16_t pid, bool isMouse, const char* name) {
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].vid == vid && list[i].pid == pid) return;
    Device d;
    d.vid = vid; d.pid = pid; d.isMouse = isMouse;
    char buf[64];
    _snprintf(buf, sizeof(buf) - 1, "%04X:%04X", vid, pid); buf[sizeof(buf) - 1] = 0;
    d.id = buf;
    if (name && name[0]) d.name = name;
    else {
        _snprintf(buf, sizeof(buf) - 1, Tr(STR_DEVICE_NAME_UNAVAILABLE_FMT), vid, pid);
        buf[sizeof(buf) - 1] = 0;
        d.name = buf;
    }
    list.push_back(d);
}

// ============================================================================
// Devices plugged into this PC. The user says, for each, whether it is the
// same model as the one on the console: if yes, it is listed with the
// console's devices and its settings go into X360Remap.json under its
// VID:PID; if no, nothing about it ever reaches the console. The answers stay
// on the PC (pc_devices.txt: "VVVV:PPPP:K:Y").
// ============================================================================
std::vector<plat::PcDevice> g_pcDevices;
std::vector<std::string>    g_pcAnswers;       // "VVVV:PPPP:K:Y" / ":N"
bool     g_pcScanWanted = true;
uint32_t g_pcGen = 1;

std::string PcKey(uint16_t vid, uint16_t pid, char kind) {
    char buf[16];
    _snprintf(buf, sizeof(buf) - 1, "%04X:%04X:%c", vid, pid, kind); buf[sizeof(buf) - 1] = 0;
    return buf;
}

int PcAnswer(uint16_t vid, uint16_t pid, char kind) {   // 1 yes, 0 no, -1 not asked
    const std::string key = PcKey(vid, pid, kind) + ":";
    for (size_t i = 0; i < g_pcAnswers.size(); i++)
        if (g_pcAnswers[i].compare(0, key.size(), key) == 0) return g_pcAnswers[i][key.size()] == 'Y' ? 1 : 0;
    return -1;
}

void LoadPcAnswers() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    std::string text;
    plat::ReadWholeFile(plat::DataDir() + "pc_devices.txt", text);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
        if (line.size() == 13 && line[11] == ':' && (line[12] == 'Y' || line[12] == 'N')) g_pcAnswers.push_back(line);
    }
}

// Plugged into this PC right now: its dot is green, it really works here.
bool PcPresent(uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < g_pcDevices.size(); i++)
        if (g_pcDevices[i].vid == vid && g_pcDevices[i].pid == pid) return true;
    return false;
}

void ScanPcDevicesIfWanted() {
    LoadPcAnswers();
    if (!g_pcScanWanted) return;
    g_pcScanWanted = false;
    g_pcDevices = plat::ListPcDevices();
    g_pcGen++;
}

void RefreshDevices() {
    static uint64_t s_stamp = ~0ull;
    static uint32_t s_gen = 0, s_pcGen = 0;
    static int      s_lang = -1;
    const std::string path = plat::WorkFile("known_devices.txt");
    const uint64_t stamp = plat::FileStamp(path);
    if (stamp == s_stamp && s_gen == g_cfgGen && s_lang == (int)GetLanguage() && s_pcGen == g_pcGen) return;
    s_stamp = stamp; s_gen = g_cfgGen; s_lang = (int)GetLanguage(); s_pcGen = g_pcGen;

    g_keyboards.clear();
    g_mice.clear();
    const std::vector<KnownDeviceInfo> known = ListKnownDevices(path.c_str());
    g_padNames.clear();
    for (size_t i = 0; i < known.size(); i++) {
        if (known[i].isPad) { g_padNames.push_back(known[i]); continue; }   // USB controller (C line): its own pill
        AddDevice(known[i].isMouse ? g_mice : g_keyboards, known[i].vid, known[i].pid, known[i].isMouse, known[i].name);
    }
    // PC devices the user said are the same as on the console.
    for (size_t i = 0; i < g_pcDevices.size(); i++) {
        const plat::PcDevice& d = g_pcDevices[i];
        if (d.kind == 'G' || PcAnswer(d.vid, d.pid, d.kind) != 1) continue;
        const std::string name = (d.name.empty() ? std::string() : d.name + " ") + "(PC)";
        AddDevice(d.kind == 'M' ? g_mice : g_keyboards, d.vid, d.pid, d.kind == 'M', name.c_str());
    }
    for (size_t i = 0; i < g_dynamicMappings.size(); i++) {
        const HidDeviceMapping& m = g_dynamicMappings[i];
        if (m.keyMapCount > 0) AddDevice(g_keyboards, m.vendorId, m.productId, false, nullptr);
        const bool mouseLike = m.mouseSensitivity != 0 || m.deadzone != 0 || m.antiDeadzone != 0 || m.smoothingMs != 0 ||
                               m.countsPerCm > 0.0f || m.profileCount > 0 || m.adsSensitivity != 0 ||
                               (m.buttonMapCount > 0 && m.axisMapCount == 0);
        if (mouseLike) AddDevice(g_mice, m.vendorId, m.productId, true, nullptr);
    }
}

int IndexOf(const std::vector<Device>& list, uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].vid == vid && list[i].pid == pid) return (int)i;
    return -1;
}

void NoDeviceToast() {
    plat::Toast("%s", L4("Aucun périphérique connu : connecte-toi à la console d'abord",
                         "No known device: connect to the console first",
                         "Ningún dispositivo conocido: conéctate primero a la consola",
                         "Nenhum dispositivo conhecido: conecte-se ao console primeiro"));
}

// ============================================================================
// Mouse settings being edited (same fields as application/main.cpp)
// ============================================================================
int   g_mouseSensitivity = 10000, g_mouseDeadzone = 0, g_mouseAntiDeadzone = 0;
int   g_mouseAntiDeadzoneY = 0, g_mouseSmoothing = 0;
bool  g_mouseInvertY = false;
float g_mouseCountsPerCm = 0.0f;
int   g_btnLeft = 0, g_btnRight = 0, g_btnMiddle = 0, g_wheelFwd = 0, g_wheelBack = 0;

struct MouseButtonTarget { StrId labelId; const char* fieldName; };
const MouseButtonTarget kMouseButtonTargets[] = {
    { STR_SETTINGS_TARGET_DEFAULT, nullptr },
    { STR_SETTINGS_TARGET_A, "a_button" }, { STR_SETTINGS_TARGET_B, "b_button" },
    { STR_SETTINGS_TARGET_X, "x_button" }, { STR_SETTINGS_TARGET_Y, "y_button" },
    { STR_SETTINGS_TARGET_LB, "l1" }, { STR_SETTINGS_TARGET_RB, "r1" },
    { STR_SETTINGS_TARGET_LT, "l2" }, { STR_SETTINGS_TARGET_RT, "r2" },
    { STR_SETTINGS_TARGET_L3, "l3" }, { STR_SETTINGS_TARGET_R3, "r3" },
    { STR_SETTINGS_TARGET_START, "start" }, { STR_SETTINGS_TARGET_BACK, "back" },
    { STR_SETTINGS_TARGET_DPAD_UP, "dpad_up" }, { STR_SETTINGS_TARGET_DPAD_DOWN, "dpad_down" },
    { STR_SETTINGS_TARGET_DPAD_LEFT, "dpad_left" }, { STR_SETTINGS_TARGET_DPAD_RIGHT, "dpad_right" },
};
const int kMouseButtonTargetCount = (int)(sizeof(kMouseButtonTargets) / sizeof(kMouseButtonTargets[0]));
const int kMouseButtonTargetBadge[] = {
    -1,
    GA_KP_A_S, GA_KP_B_S, GA_KP_X_S, GA_KP_Y_S,
    GA_KP_LB_S, GA_KP_RB_S, GA_KP_LT_S, GA_KP_RT_S, GA_KP_L3_S, GA_KP_R3_S,
    GA_KP_START_S, GA_KP_BACK_S,
    GA_KP_DUP_S, GA_KP_DDOWN_S, GA_KP_DLEFT_S, GA_KP_DRIGHT_S,
};
typedef char kBadgeTableSameLength[
    (sizeof(kMouseButtonTargetBadge) / sizeof(kMouseButtonTargetBadge[0]) ==
     sizeof(kMouseButtonTargets) / sizeof(kMouseButtonTargets[0])) ? 1 : -1];

int FindMouseButtonTargetIndex(uint8_t ButtonsReport::* field) {
    const char* name = GetButtonFieldName(field);
    if (!name) return 0;
    for (int i = 1; i < kMouseButtonTargetCount; i++)
        if (strcmp(kMouseButtonTargets[i].fieldName, name) == 0) return i;
    return 0;
}

void LoadMouseEditor() {
    g_mouseSensitivity = 10000; g_mouseDeadzone = 0; g_mouseAntiDeadzone = 0;
    g_mouseAntiDeadzoneY = 0; g_mouseSmoothing = 0; g_mouseInvertY = false; g_mouseCountsPerCm = 0.0f;
    g_btnLeft = g_btnRight = g_btnMiddle = g_wheelFwd = g_wheelBack = 0;
    if (!g_msValid) return;
    const HidDeviceMapping* m = FindMapping(g_msVid, g_msPid);
    if (!m) return;
    if (m->mouseSensitivity > 0) g_mouseSensitivity = m->mouseSensitivity;
    g_mouseInvertY = m->invertMouseY;
    g_mouseDeadzone = m->deadzone;
    g_mouseAntiDeadzone = m->antiDeadzone;
    g_mouseAntiDeadzoneY = m->antiDeadzoneY;
    g_mouseSmoothing = m->smoothingMs;
    g_mouseCountsPerCm = m->countsPerCm;
    for (uint8_t i = 0; i < m->buttonMapCount; i++) {
        const HidButtonMapEntry& e = m->buttonMap[i];
        int* dst = e.idx == 0 ? &g_btnLeft : e.idx == 1 ? &g_btnRight : e.idx == 2 ? &g_btnMiddle
                 : e.idx == 3 ? &g_wheelFwd : e.idx == 4 ? &g_wheelBack : nullptr;
        if (dst) *dst = FindMouseButtonTargetIndex(e.field);
    }
}

bool SaveMouseButtonBindings() {
    std::vector<MouseButtonBinding> bindings;
    const int sel[5] = { g_btnLeft, g_btnRight, g_btnMiddle, g_wheelFwd, g_wheelBack };
    for (uint8_t i = 0; i < 5; i++) {
        if (sel[i] < 0 || sel[i] >= kMouseButtonTargetCount) continue;
        if (!kMouseButtonTargets[sel[i]].fieldName) continue;   // "Default"
        MouseButtonBinding b;
        b.idx = i;
        b.fieldName = kMouseButtonTargets[sel[i]].fieldName;
        bindings.push_back(b);
    }
    const bool ok = ApplyMouseButtonMapping(g_msVid, g_msPid, bindings, ModelConfigPath().c_str());
    ModelOwnWriteDone();
    plat::Log(ok ? "mouse buttons saved (%d custom entries)" : "FAILED to save mouse buttons", (int)bindings.size());
    return ok;
}

void SavedToast(bool ok) {
    plat::Toast("%s", ok ? L4("Enregistré sur le PC - « Envoyer » pour la console", "Saved on the PC - \"Send\" for the console",
                              "Guardado en el PC - «Enviar» para la consola", "Salvo no PC - \"Enviar\" para o console")
                         : L4("ÉCHEC de l'enregistrement", "Saving FAILED", "ERROR al guardar", "FALHA ao salvar"));
}

// ============================================================================
// Key grid: what each controller button is bound to
// ============================================================================
const char* ShortActionLabel(uint8_t action) {
    switch (action) {
        case KEYACT_A:  return "A";      case KEYACT_B:  return "B";
        case KEYACT_X:  return "X";      case KEYACT_Y:  return "Y";
        case KEYACT_LB: return "LB";     case KEYACT_RB: return "RB";
        case KEYACT_LT: return "LT";     case KEYACT_RT: return "RT";
        case KEYACT_L3: return "L3";     case KEYACT_R3: return "R3";
        case KEYACT_START: return "START"; case KEYACT_BACK: return "BACK"; case KEYACT_GUIDE: return "XBOX";
        case KEYACT_DPAD_UP: return "D-Up";       case KEYACT_DPAD_DOWN: return "D-Down";
        case KEYACT_DPAD_LEFT: return "D-Left";   case KEYACT_DPAD_RIGHT: return "D-Right";
        case KEYACT_LSTICK_UP: return "L-Up";     case KEYACT_LSTICK_DOWN: return "L-Down";
        case KEYACT_LSTICK_LEFT: return "L-Left"; case KEYACT_LSTICK_RIGHT: return "L-Right";
        case KEYACT_RSTICK_UP: return "R-Up";     case KEYACT_RSTICK_DOWN: return "R-Down";
        case KEYACT_RSTICK_LEFT: return "R-Left"; case KEYACT_RSTICK_RIGHT: return "R-Right";
        default: return nullptr;
    }
}

// Shows what the keyboard REALLY does on the console (same resolution as the
// plugin: default layout, the device's own keys on top, keypad preset), not
// only the keys stored in the file - an untouched button shows its default key
// instead of an empty cell.
void BuildGrid(std::vector<UiGridEntry>& out, std::vector<std::string>& labels, std::vector<std::string>& keys) {
    const std::vector<AssistantStep> steps = DefaultKeyboardWizardSteps();
    HidKeyMapEntry eff[MAX_MERGED_KEYS];
    // No keyboard known: nothing is shown as bound (an empty-state panel
    // explains why, see DrawMappingPage). Showing a layout here would be
    // showing the settings of a device that does not exist.
    uint8_t n = 0;
    bool keypad = false;
    if (g_kbValid) {
        n = ResolveEffectiveKeyMap(FindMapping(g_kbVid, g_kbPid), 0, eff, MAX_MERGED_KEYS);
        keypad = IsKnownKeypad(g_kbVid, g_kbPid);
        if (keypad) n = AddKeypadPreset(g_kbVid, g_kbPid, eff, n, MAX_MERGED_KEYS);
    }
    labels.clear(); keys.clear(); out.clear();
    labels.reserve(steps.size()); keys.reserve(steps.size());
    for (size_t i = 0; i < steps.size(); i++) {
        const char* s = ShortActionLabel(steps[i].action);
        labels.push_back(s ? s : steps[i].label);
        std::string key = "-";
        // Last match wins: the device's own entries come after the defaults.
        for (uint8_t k = 0; k < n; k++) {
            if (eff[k].action != steps[i].action) continue;
            char buf[24];
            const char* pad = keypad ? KeypadKeyLabel(eff[k].code) : nullptr;
            key = pad ? pad : HidKeyLabel(eff[k].code, buf, sizeof(buf));
        }
        keys.push_back(key);
    }
    for (size_t i = 0; i < steps.size(); i++) {
        UiGridEntry e;
        e.button = labels[i].c_str();
        e.key = keys[i].c_str();
        out.push_back(e);
    }
}

// ============================================================================
// Key capture: the 21-step assistant and "one click, one key"
// The sessions are the console ones (WizardSession); only the source of the
// key code differs: the PC keyboard instead of input_state.json.
// ============================================================================
WizardSession* g_wizard = nullptr;
WizardSession* g_manual = nullptr;
int  g_manualIndex = -1;
bool g_wizardSaveAttempted = false, g_wizardSaveOk = false;
bool g_wizardJustStarted = false, g_manualJustStarted = false;

// "Hold 3 seconds to skip": the sessions count calls, one per frame.
uint32_t HoldTicks() {
    const float fps = ImGui::GetIO().Framerate;
    const float ticks = (fps > 20.0f && fps < 500.0f ? fps : 60.0f) * 3.0f;
    return (uint32_t)ticks;
}

void UpdateWizardModel(UiMappingModel& m) {
    if (!g_wizard) { m.wizardActive = false; return; }
    int32_t code = KeyCaptureHeldCode();
    // Release first: the key that started the capture may still be held.
    static bool s_armed = false;
    if (g_wizardJustStarted) { g_wizardJustStarted = false; s_armed = false; }
    if (!s_armed) {
        if (code == MappingAssistant::NO_CODE) s_armed = true;
        code = MappingAssistant::NO_CODE;
    }
    if (code == 0x29) g_wizard->Cancel();   // Esc

    std::string prompt, message;
    g_wizard->Tick(code, &prompt, &message);

    m.wizardActive       = true;
    m.wizardStepIndex    = (int)g_wizard->CurrentStepIndex();
    m.wizardStepTotal    = (int)g_wizard->TotalSteps();
    m.wizardResultsCount = (int)g_wizard->ResultsCount();
    m.wizardNoKeyboard   = false;
    m.wizardComplete     = false;
    m.wizardCancelled    = false;

    static char s_prompt[128], s_message[192];
    if (!prompt.empty()) {
        _snprintf(s_prompt, sizeof(s_prompt) - 1, Tr(STR_WIZARD_PRESS_KEY_FOR_FMT), prompt.c_str());
        s_prompt[sizeof(s_prompt) - 1] = '\0';
        m.wizardPrompt = s_prompt;
    } else m.wizardPrompt = nullptr;
    if (!message.empty()) {
        _snprintf(s_message, sizeof(s_message) - 1, "%s", message.c_str());
        s_message[sizeof(s_message) - 1] = '\0';
        m.wizardMessage = s_message;
    } else m.wizardMessage = nullptr;

    if (g_wizard->IsComplete()) {
        m.wizardComplete = true;
        if (!g_wizardSaveAttempted) {
            g_wizardSaveAttempted = true;
            g_wizardSaveOk = g_wizard->SaveResult(ModelConfigPath().c_str());
            ModelOwnWriteDone();
            plat::Log(g_wizardSaveOk ? "keyboard wizard: %d keys saved" : "keyboard wizard: FAILED to save (%d keys)",
                      (int)g_wizard->ResultsCount());
            SavedToast(g_wizardSaveOk);
        }
        m.wizardSaveOk = g_wizardSaveOk;
    } else if (g_wizard->WasCancelled()) {
        m.wizardCancelled = true;
    }
}

void EndManual(UiMappingModel& m) {
    delete g_manual;
    g_manual = nullptr;
    g_manualIndex = -1;
    m.manualCaptureIndex = -1;
    m.manualPrompt = nullptr;
    m.manualMessage = nullptr;
}

void UpdateManualModel(UiMappingModel& m) {
    m.manualMode = true;   // on the PC a click on a cell always starts a capture
    m.manualNoKeyboard = false;
    if (!g_manual) {
        m.manualCaptureIndex = -1;
        m.manualPrompt = nullptr;
        m.manualMessage = nullptr;
        return;
    }
    int32_t code = KeyCaptureHeldCode();
    static bool s_armed = false;
    if (g_manualJustStarted) { g_manualJustStarted = false; s_armed = false; }
    if (!s_armed) {
        if (code == MappingAssistant::NO_CODE) s_armed = true;
        code = MappingAssistant::NO_CODE;
    }
    if (code == 0x29) g_manual->Cancel();   // Esc

    std::string prompt, message;
    g_manual->Tick(code, &prompt, &message);
    m.manualCaptureIndex = g_manualIndex;

    static char s_prompt[128], s_message[192];
    if (!prompt.empty()) {
        _snprintf(s_prompt, sizeof(s_prompt) - 1, Tr(STR_WIZARD_PRESS_KEY_FOR_FMT), prompt.c_str());
        s_prompt[sizeof(s_prompt) - 1] = '\0';
        m.manualPrompt = s_prompt;
    } else m.manualPrompt = nullptr;
    if (!message.empty()) {
        _snprintf(s_message, sizeof(s_message) - 1, "%s", message.c_str());
        s_message[sizeof(s_message) - 1] = '\0';
        m.manualMessage = s_message;
    } else m.manualMessage = nullptr;

    if (g_manual->IsComplete()) {
        const std::vector<AssistantResult>& results = g_manual->Results();
        bool ok = false;
        if (!results.empty()) {
            HidKeyMapEntry entry;
            entry.code = (uint8_t)results[0].code;
            entry.action = results[0].action;
            ok = ApplyKeyboardMappingEntry(g_kbVid, g_kbPid, entry, ModelConfigPath().c_str());
            ModelOwnWriteDone();
            plat::Log(ok ? "key 0x%02X bound to action %d" : "FAILED to bind key 0x%02X to action %d",
                      (unsigned)entry.code, (int)entry.action);
            SavedToast(ok);
        }
        EndManual(m);
    } else if (g_manual->WasCancelled()) {
        EndManual(m);
    }
}

// Empty state drawn over a part of a shared page (design coordinates of the
// 1280x720 layout): says why there is nothing here and what to do. Rule: no
// placeholder device, no sample value - when there is nothing real to show,
// say so.
void EmptyStatePanel(float pageW, float x, float y, float w, float h, const char* title, const char* text) {
    const float k = pageW / 1280.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(x * k, y * k), b((x + w) * k, (y + h) * k);
    dl->AddRectFilled(a, b, IM_COL32(8, 14, 24, 244), 6.0f * k);
    dl->AddRect(a, b, IM_COL32(108, 162, 222, 150), 6.0f * k, ImDrawCornerFlags_All, 1.0f);
    ImFont* ft = g_fontGBodyM ? g_fontGBodyM : ImGui::GetFont();
    ImFont* fb = g_fontGBody ? g_fontGBody : ImGui::GetFont();
    const float pad = 18.0f * k, wrap = w * k - pad * 2.0f;
    const float ts = 25.0f * k, bs = 22.0f * k;
    const ImVec2 tsz = ft->CalcTextSizeA(ts, FLT_MAX, wrap, title);
    const ImVec2 bsz = fb->CalcTextSizeA(bs, FLT_MAX, wrap, text);
    float cy = (a.y + b.y) * 0.5f - (tsz.y + 10.0f * k + bsz.y) * 0.5f;
    dl->AddText(ft, ts, ImVec2(a.x + pad, cy), IM_COL32(245, 211, 90, 255), title, nullptr, wrap);
    cy += tsz.y + 10.0f * k;
    dl->AddText(fb, bs, ImVec2(a.x + pad, cy), IM_COL32(200, 211, 224, 255), text, nullptr, wrap);
}

const char* HowToGetDevices() {
    return consync::State() == consync::LINK_CONNECTED
        ? L4("Branche-le sur la console (pas sur le PC), attends quelques secondes, puis clique « Recevoir ».",
             "Plug it into the console (not the PC), wait a few seconds, then click \"Receive\".",
             "Conéctalo a la consola (no al PC), espera unos segundos y pulsa «Recibir».",
             "Conecte-o ao console (não ao PC), aguarde alguns segundos e clique em \"Receber\".")
        : L4("La liste vient de la console : connecte-toi (en bas à gauche), ou ouvre un X360Remap.json avec « Fichier ».",
             "The list comes from the console: connect (bottom left), or open an X360Remap.json with \"File\".",
             "La lista viene de la consola: conéctate (abajo a la izquierda) o abre un X360Remap.json con «Archivo».",
             "A lista vem do console: conecte-se (canto inferior esquerdo) ou abra um X360Remap.json em \"Arquivo\".");
}

// ============================================================================
// USB controllers plugged into the console (2026-11-18, HB: "like the console
// Studio"): one pill each in the device bar, and the shared controller page
// (buttons, sticks, macros). The list comes from the plugin's status; a button
// is caught when the same controller is plugged into this PC (winmm button n =
// HID idx n, pad_input_win.h), the console sends no live input to the PC.
// ============================================================================
int      g_padView = -1;
int      g_padWanted = -1;   // --open=pad: opened once the status lists it
int      s_padCount = 0;
uint16_t s_padVid[4], s_padPid[4];
char     s_padIdText[4][12];
char     s_padName[4][48];
UiDeviceEntry s_padEntries[4];

void CollectPadsWin() {
    s_padCount = consync::ConsolePluggedPads(s_padVid, s_padPid, 4);
    for (int n = 0; n < s_padCount; n++) {
        _snprintf(s_padIdText[n], sizeof(s_padIdText[n]) - 1, "%04X:%04X", s_padVid[n], s_padPid[n]);
        s_padIdText[n][sizeof(s_padIdText[n]) - 1] = '\0';
        const char* nm = Tr(STR_PAD_USB);
        for (size_t k = 0; k < g_padNames.size(); k++)
            if (g_padNames[k].vid == s_padVid[n] && g_padNames[k].pid == s_padPid[n] && g_padNames[k].name[0]) { nm = g_padNames[k].name; break; }
        _snprintf(s_padName[n], sizeof(s_padName[n]) - 1, "%s", nm);
        s_padName[n][sizeof(s_padName[n]) - 1] = '\0';
        s_padEntries[n].name = s_padName[n];
        s_padEntries[n].id = s_padIdText[n];
        s_padEntries[n].active = true;
    }
    // Same name twice (two identical pads): number them in the plugin's order.
    for (int a = 0; a < s_padCount; a++) {
        int rank = 0, same = 0;
        for (int b = 0; b < s_padCount; b++)
            if (!strcmp(s_padName[a], s_padName[b])) { same++; if (b < a) rank++; }
        if (same < 2) continue;
        char tmp[48];
        _snprintf(tmp, sizeof(tmp) - 1, "%.40s %d", s_padName[a], rank + 1);
        tmp[sizeof(tmp) - 1] = '\0';
        strcpy(s_padName[a], tmp);
    }
    if (g_padWanted >= 0 && g_padWanted < s_padCount) { g_padView = g_padWanted; g_padWanted = -1; }
    if (g_padView >= s_padCount) g_padView = -1;   // unplugged: back to the keyboard & mouse page
}

void DrawPadPageWin(float w, float h) {
    static GlossyPadModel pm;
    static int s_loadedFor = -2;
    static uint16_t s_loadedVid = 0, s_loadedPid = 0;
    static uint32_t s_loadedGen = 0;
    static double s_saveButtonsAt = -1.0, s_saveStickAt = -1.0;
    const double now = ImGui::GetTime();
    CollectPadsWin();
    if (g_padView < 0) return;
    const uint16_t vid = s_padVid[g_padView], pid = s_padPid[g_padView];

    // (Re)load: another pad, a pull from the console, or no entry written by the plugin yet.
    static double s_lastLoad = 0.0;
    if (vid != s_loadedVid || pid != s_loadedPid || s_loadedGen != g_cfgGen ||
        (!(pm.hasEntry && pm.hasAxes) && now - s_lastLoad > 1.0)) {
        static PadConfig pc;   // 8 KB: not on the stack
        ReadPadConfig(vid, pid, pc, ModelConfigPath().c_str());
        pm.hasEntry = pc.hasEntry; pm.hasAxes = pc.hasAxes;
        for (int i = 0; i < PAD_CELLS; i++) pm.btnIdx[i] = pc.btnIdx[i];
        pm.invertRY = pc.invertRY;
        pm.deadzone = pc.deadzone > 10000 ? 10000 : pc.deadzone;
        pm.macroCount = pc.macroCount;
        for (int i = 0; i < pc.macroCount; i++) {
            pm.macros[i].btn = pc.macros[i].btn;
            pm.macros[i].stepCount = pc.macros[i].stepCount;
            for (int s = 0; s < pc.macros[i].stepCount; s++) { pm.macros[i].steps[s].mask = pc.macros[i].steps[s].mask; pm.macros[i].steps[s].ms = pc.macros[i].steps[s].ms; }
        }
        pm.captureCell = -1; pm.wizard = false;
        if (vid != s_loadedVid || pid != s_loadedPid) { pm.editMacro = -1; pm.recording = pm.testing = pm.capTrigger = false; }
        s_loadedVid = vid; s_loadedPid = pid; s_loadedFor = g_padView; s_loadedGen = g_cfgGen;
        s_lastLoad = now;
    }
    pm.pads = s_padEntries; pm.padCount = s_padCount; pm.sel = g_padView;
    pm.hasKeyboard = pm.hasMouse = true;   // both pills lead back to the keyboard & mouse page
    pm.pluginAlive = consync::State() == consync::LINK_CONNECTED;
    pm.appVersion = kWinAppVersion;
    // Live buttons: the same controller plugged into this PC.
    PcPadRaw raw;
    const bool live = ReadPcPad(raw) && raw.vid == vid && raw.pid == pid;
    pm.hasLive = live;
    pm.liveMask = live ? raw.buttons : 0;

    DrawUiGlossyPad(pm, g_tab, w, h);

    // Saved on the PC 1 s after the last change (one write per burst); Send takes it to the console.
    if (pm.reqSaveButtons) { pm.reqSaveButtons = false; s_saveButtonsAt = now; }
    if (pm.reqSaveStick)   { pm.reqSaveStick = false;   s_saveStickAt = now; }
    if (s_saveButtonsAt > 0.0 && now - s_saveButtonsAt > 1.0) {
        s_saveButtonsAt = -1.0;
        static PadMacro macros[PAD_MACROS_MAX_CFG];
        for (int i = 0; i < pm.macroCount; i++) {
            macros[i].btn = pm.macros[i].btn;
            macros[i].stepCount = pm.macros[i].stepCount;
            for (int s = 0; s < pm.macros[i].stepCount; s++) { macros[i].steps[s].mask = pm.macros[i].steps[s].mask; macros[i].steps[s].ms = pm.macros[i].steps[s].ms; }
        }
        const bool ok = ApplyPadButtons(vid, pid, pm.btnIdx, macros, pm.macroCount, ModelConfigPath().c_str());
        ModelOwnWriteDone();
        s_loadedGen = g_cfgGen;   // our own write: keep the wizard / editor state
        plat::Log("pad %04X:%04X buttons %s", vid, pid, ok ? "saved" : "NOT saved");
        SavedToast(ok);
    }
    if (s_saveStickAt > 0.0 && now - s_saveStickAt > 1.0) {
        s_saveStickAt = -1.0;
        const bool ok = ApplyPadStick(vid, pid, pm.invertRY, pm.deadzone, ModelConfigPath().c_str());
        ModelOwnWriteDone();
        s_loadedGen = g_cfgGen;   // our own write: keep the wizard / editor state
        plat::Log("pad %04X:%04X stick %s", vid, pid, ok ? "saved" : "NOT saved");
        SavedToast(ok);
    }
    if (pm.reqOpenPad >= 0) { g_padView = pm.reqOpenPad; pm.reqOpenPad = -1; }
    if (pm.reqRefresh) { pm.reqRefresh = false; consync::Pull(); }
    if (pm.reqBack >= 0) { pm.reqBack = -1; g_padView = -1; s_loadedFor = -2; }
}

// ============================================================================
// Page 1: keyboard & mouse
// ============================================================================
void DrawMappingPage(float w, float h) {
    static std::vector<UiDeviceEntry> s_kb, s_ms;
    static std::vector<UiGridEntry>   s_grid;
    static std::vector<std::string>   s_gridLabels, s_gridKeys;
    static std::vector<const char*>   s_targets;
    static UiMappingModel             s_model;

    const bool connected = consync::State() == consync::LINK_CONNECTED;

    // Same rule as the console Studio (2026-11-18, HB: an absent mouse stayed first and red):
    // the plugin's status says what is plugged into the console now; as soon as one keyboard
    // (mouse) is plugged there, the absent ones are hidden and the selection moves to a
    // plugged one. No fresh status (offline, old plugin) = every device, as before.
    // s_kbIdx / s_msIdx: position in the shown list -> index in g_keyboards / g_mice.
    static std::vector<int> s_kbIdx, s_msIdx;
    struct Lists {
        static void Build(const std::vector<Device>& all, char kind, bool connected,
                          std::vector<int>& idx, std::vector<UiDeviceEntry>& out, uint16_t& selVid, uint16_t& selPid) {
            const bool hide = consync::ConsoleHasPlugged(kind);
            idx.clear(); out.clear();
            for (size_t i = 0; i < all.size(); i++) {
                const int plugged = consync::ConsolePlugged(kind, all[i].vid, all[i].pid);
                const bool onPc = PcPresent(all[i].vid, all[i].pid) && PcAnswer(all[i].vid, all[i].pid, kind == 'k' ? 'K' : 'M') == 1;
                if (hide && plugged != 1 && !onPc) continue;
                UiDeviceEntry e;
                e.name = all[i].name.c_str(); e.id = all[i].id.c_str();
                e.active = plugged == 1 || onPc || (plugged < 0 && connected);
                idx.push_back((int)i);
                out.push_back(e);
            }
            bool selShown = false;
            for (size_t k = 0; k < idx.size(); k++)
                selShown = selShown || (all[idx[k]].vid == selVid && all[idx[k]].pid == selPid);
            if (!selShown && !idx.empty()) { selVid = all[idx[0]].vid; selPid = all[idx[0]].pid; }
        }
        static int Pos(const std::vector<Device>& all, const std::vector<int>& idx, uint16_t vid, uint16_t pid) {
            for (size_t k = 0; k < idx.size(); k++)
                if (all[idx[k]].vid == vid && all[idx[k]].pid == pid) return (int)k;
            return 0;
        }
    };
    Lists::Build(g_keyboards, 'k', connected, s_kbIdx, s_kb, g_kbVid, g_kbPid);
    Lists::Build(g_mice, 'm', connected, s_msIdx, s_ms, g_msVid, g_msPid);
    const int kbIndex = Lists::Pos(g_keyboards, s_kbIdx, g_kbVid, g_kbPid);
    const int msIndex = Lists::Pos(g_mice, s_msIdx, g_msVid, g_msPid);
    BuildGrid(s_grid, s_gridLabels, s_gridKeys);

    s_model.langIndex = (GetLanguage() == LANG_EN) ? 1 : (GetLanguage() == LANG_PT_BR) ? 2 : (GetLanguage() == LANG_ES) ? 3 : 0;
    s_model.keyboards = s_kb.empty() ? nullptr : &s_kb[0];
    s_model.keyboardCount = (int)s_kb.size();
    s_model.mice = s_ms.empty() ? nullptr : &s_ms[0];
    s_model.mouseCount = (int)s_ms.size();
    CollectPadsWin();
    s_model.pads = s_padEntries;
    s_model.padCount = s_padCount;
    s_model.keyboardIndex = kbIndex < 0 ? 0 : kbIndex;
    s_model.mouseIndex = msIndex < 0 ? 0 : msIndex;
    s_model.detectedCount = (int)(s_kb.size() + s_ms.size());
    s_model.pluginAlive = connected;
    s_model.grid = s_grid.empty() ? nullptr : &s_grid[0];
    s_model.gridCount = (int)s_grid.size();

    s_model.sensitivity = &g_mouseSensitivity; s_model.sensitivityMin = 1000; s_model.sensitivityMax = 30000;
    s_model.deadzone = &g_mouseDeadzone;       s_model.deadzoneMin = 0;       s_model.deadzoneMax = 10000;
    s_model.antiDeadzone = &g_mouseAntiDeadzone; s_model.antiDeadzoneMin = 0; s_model.antiDeadzoneMax = 12000;
    s_model.antiDeadzoneY = &g_mouseAntiDeadzoneY;
    s_model.smoothing = &g_mouseSmoothing; s_model.smoothingMax = 100;
    s_model.invertY = &g_mouseInvertY;
    s_model.mouseDpiMeasured = (g_mouseCountsPerCm > 0.0f) ? (int)(g_mouseCountsPerCm * 2.54f + 0.5f) : 0;

    s_targets.clear();
    for (int i = 0; i < kMouseButtonTargetCount; i++) s_targets.push_back(Tr(kMouseButtonTargets[i].labelId));
    s_model.buttonTargets = &s_targets[0];
    s_model.buttonTargetCount = (int)s_targets.size();
    s_model.btnLeft = &g_btnLeft; s_model.btnRight = &g_btnRight; s_model.btnMiddle = &g_btnMiddle;
    s_model.wheelForward = &g_wheelFwd; s_model.wheelBackward = &g_wheelBack;
    s_model.buttonTargetBadge = kMouseButtonTargetBadge;
    s_model.appVersion = kWinAppVersion;
    s_model.hasSavedDefault = HasConfigDefault(plat::WorkFile("X360Remap_default.json").c_str());

    UpdateWizardModel(s_model);
    UpdateManualModel(s_model);

    DrawUiGlossyMapping(s_model, g_tab, w, h);

    if (!g_kbValid)
        EmptyStatePanel(w, 38.0f, 254.0f, 348.0f, 372.0f,
                        L4("Aucun clavier vu par la console", "No keyboard seen by the console yet",
                           "Ningún teclado visto por la consola", "Nenhum teclado visto pelo console"),
                        HowToGetDevices());
    if (!g_msValid)
        EmptyStatePanel(w, 956.0f, 200.0f, 288.0f, 436.0f,
                        L4("Aucune souris vue par la console", "No mouse seen by the console yet",
                           "Ningún ratón visto por la consola", "Nenhum mouse visto pelo console"),
                        HowToGetDevices());

    // --- what the page asked for --------------------------------------------
    const Lang wanted = (s_model.langIndex == 1) ? LANG_EN : (s_model.langIndex == 2) ? LANG_PT_BR
                      : (s_model.langIndex == 3) ? LANG_ES : LANG_FR;
    if (wanted != GetLanguage()) {
        SetLanguage(wanted);
        plat::Settings().language = (int)wanted;
        plat::SaveSettings();
    }
    if (s_model.keyboardIndex >= 0 && s_model.keyboardIndex < (int)s_kbIdx.size()) {
        g_kbVid = g_keyboards[s_kbIdx[s_model.keyboardIndex]].vid;
        g_kbPid = g_keyboards[s_kbIdx[s_model.keyboardIndex]].pid;
    }
    if (s_model.mouseIndex >= 0 && s_model.mouseIndex < (int)s_msIdx.size()) {
        g_msVid = g_mice[s_msIdx[s_model.mouseIndex]].vid;
        g_msPid = g_mice[s_msIdx[s_model.mouseIndex]].pid;
    }

    if (s_model.requestOpenPad >= 0) {
        if (s_model.requestOpenPad < s_padCount) g_padView = s_model.requestOpenPad;
        s_model.requestOpenPad = -1;
    }
    if (s_model.requestRefreshDevices) {
        s_model.requestRefreshDevices = false;
        consync::Pull();   // the list comes from the console
    }
    if (s_model.requestStartWizard) {
        s_model.requestStartWizard = false;
        if (!g_kbValid) NoDeviceToast();
        else {
            const std::vector<AssistantStep> steps = DefaultKeyboardWizardSteps();
            g_wizard = new WizardSession(g_kbVid, g_kbPid, steps, HoldTicks());
            g_wizardJustStarted = true;
            plat::Log("keyboard wizard started (%04X:%04X, %d steps)", g_kbVid, g_kbPid, (int)steps.size());
        }
    }
    s_model.requestToggleManualMode = false;
    if (s_model.requestManualAssignIndex >= 0) {
        const int idx = s_model.requestManualAssignIndex;
        s_model.requestManualAssignIndex = -1;
        const std::vector<AssistantStep> all = DefaultKeyboardWizardSteps();
        if (!g_kbValid) NoDeviceToast();
        else if (!g_manual && !g_wizard && idx < (int)all.size()) {
            std::vector<AssistantStep> one;
            one.push_back(all[idx]);
            g_manual = new WizardSession(g_kbVid, g_kbPid, one, HoldTicks());
            g_manualIndex = idx;
            g_manualJustStarted = true;
        }
    }
    if (s_model.requestWizardClose) {
        s_model.requestWizardClose = false;
        delete g_wizard;
        g_wizard = nullptr;
        g_wizardSaveAttempted = false;
        g_wizardSaveOk = false;
    }
    if (s_model.requestSaveMouse) {
        s_model.requestSaveMouse = false;
        if (!g_msValid) NoDeviceToast();
        else {
            const bool ok = ApplyMouseSettingsFull(g_msVid, g_msPid, g_mouseSensitivity, g_mouseInvertY, g_mouseDeadzone,
                                                   g_mouseAntiDeadzone, ModelConfigPath().c_str(),
                                                   g_mouseAntiDeadzoneY, g_mouseSmoothing);
            ModelOwnWriteDone();
            const bool okBtn = SaveMouseButtonBindings();
            plat::Log(ok ? "mouse settings saved (sensitivity=%d deadzone=%d antiDeadzone=%d)" : "FAILED to save mouse settings",
                      g_mouseSensitivity, g_mouseDeadzone, g_mouseAntiDeadzone);
            SavedToast(ok && okBtn);
        }
    }
    if (s_model.requestSaveButtons) {
        s_model.requestSaveButtons = false;
        if (!g_msValid) NoDeviceToast();
        else SavedToast(SaveMouseButtonBindings());
    }
    if (s_model.requestSaveDefault) {
        s_model.requestSaveDefault = false;
        const bool ok = SaveConfigAsDefault(ModelConfigPath().c_str(), plat::WorkFile("X360Remap_default.json").c_str());
        plat::Toast("%s", ok ? L4("Réglages actuels enregistrés comme défaut", "Current settings saved as default",
                                  "Ajustes actuales guardados por defecto", "Configurações atuais salvas como padrão")
                             : L4("ÉCHEC de l'enregistrement", "Saving FAILED", "ERROR al guardar", "FALHA ao salvar"));
    }
    if (s_model.requestRestoreDefault) {
        s_model.requestRestoreDefault = false;
        const std::string def = plat::WorkFile("X360Remap_default.json");
        const bool hadDefault = HasConfigDefault(def.c_str());
        const bool ok = hadDefault ? RestoreConfigDefault(ModelConfigPath().c_str(), def.c_str())
                                   : RestoreFactoryConfig(ModelConfigPath().c_str());
        ModelWorkingCopyChanged();
        plat::Toast("%s", ok ? (hadDefault ? L4("Réglages par défaut restaurés", "Default settings restored",
                                                "Ajustes por defecto restaurados", "Configurações padrão restauradas")
                                           : L4("Réglages d'origine restaurés", "Factory settings restored",
                                                "Ajustes de fábrica restaurados", "Configurações de fábrica restauradas"))
                             : L4("ÉCHEC de la restauration", "Restore FAILED", "ERROR al restaurar", "FALHA ao restaurar"));
    }
    if (s_model.requestMeasureMouse || s_model.requestStartCalibration) {
        s_model.requestMeasureMouse = false;
        s_model.requestStartCalibration = false;
        // Both need the raw reports of the mouse plugged into the console.
        plat::Toast("%s", L4("Mesure et calibration : à faire dans le Studio de la console",
                             "Measuring and calibrating: done in the console Studio",
                             "Medición y calibración: se hacen en el Studio de la consola",
                             "Medição e calibração: feitas no Studio do console"));
    }
    s_model.requestFallback = -1;
    s_model.requestAdvanced = false;
}

// ============================================================================
// Page 2: game profiles
// ============================================================================
struct GameEntry {
    uint32_t    tid;
    std::string name, auroraName, cameraName;
    void*       tex;
    int         w, h;
    int         coverState;   // 0 not tried, 1 asked to the console, 2 done (loaded or absent)
};
std::vector<GameEntry> g_games;
std::vector<MouseProfileInfo> g_profiles;
bool     g_gamesDirty = true;
void*    g_bigCover = nullptr;
int      g_bigW = 0, g_bigH = 0;
uint32_t g_bigTid = 0;
uint32_t g_selectedTid = 0;

int   g_profSensitivity = 10000, g_profDeadzone = 0, g_profAntiDeadzone = 0, g_profStickLayout = 0;
int   g_profAntiDeadzoneY = 0, g_profSmoothing = 0, g_profCurveType = 0;
bool  g_profInvertY = false;
float g_profCurveExp = 1.5f;

const char* ProfileName(uint32_t tid) {
    for (size_t i = 0; i < g_profiles.size(); i++)
        if (g_profiles[i].titleId == tid) return g_profiles[i].gameName;
    return "";
}
bool HasProfile(uint32_t tid) {
    for (size_t i = 0; i < g_profiles.size(); i++)
        if (g_profiles[i].titleId == tid) return true;
    return false;
}

void LoadProfileIntoEditor(const MouseProfileInfo& p) {
    g_profSensitivity = (p.mouseSensitivity != 0) ? p.mouseSensitivity : g_mouseSensitivity;
    g_profInvertY = (p.invertMouseY != -1) ? (p.invertMouseY != 0) : g_mouseInvertY;
    g_profDeadzone = (p.deadzone != -1) ? p.deadzone : g_mouseDeadzone;
    g_profAntiDeadzone = (p.antiDeadzone >= 0) ? p.antiDeadzone : g_mouseAntiDeadzone;
    g_profStickLayout = (p.stickLayout > 0 && p.stickLayout < STICKS_LAYOUT_COUNT) ? p.stickLayout : 0;
    g_profAntiDeadzoneY = (p.antiDeadzoneY >= 0) ? p.antiDeadzoneY : g_mouseAntiDeadzoneY;
    g_profSmoothing = (p.smoothingMs >= 0) ? p.smoothingMs : g_mouseSmoothing;
    if (p.curveType == CURVE_EXPONENTIAL) { g_profCurveType = 1; g_profCurveExp = p.curveExponent; }
    else g_profCurveType = 0;
}

void ResetProfileEditor() {
    g_profSensitivity = g_mouseSensitivity; g_profInvertY = g_mouseInvertY; g_profDeadzone = g_mouseDeadzone;
    g_profAntiDeadzone = g_mouseAntiDeadzone; g_profStickLayout = 0; g_profAntiDeadzoneY = g_mouseAntiDeadzoneY;
    g_profSmoothing = g_mouseSmoothing; g_profCurveType = 0; g_profCurveExp = 1.5f;
}

void LoadEditorForSelection() {
    for (size_t k = 0; k < g_profiles.size(); k++)
        if (g_profiles[k].titleId == g_selectedTid) { LoadProfileIntoEditor(g_profiles[k]); return; }
    ResetProfileEditor();
}

void FreeCovers() {
    for (size_t i = 0; i < g_games.size(); i++) ModelReleaseTexture(g_games[i].tex);
    ModelReleaseTexture(g_bigCover);
    g_bigCover = nullptr;
    g_bigTid = 0;
}

void AddGame(uint32_t tid, const char* auroraName, const char* cameraName) {
    if (tid == 0) return;
    for (size_t i = 0; i < g_games.size(); i++) {
        if (g_games[i].tid != tid) continue;
        if (auroraName && auroraName[0] && g_games[i].auroraName.empty()) g_games[i].auroraName = auroraName;
        if (cameraName && cameraName[0] && g_games[i].cameraName.empty()) g_games[i].cameraName = cameraName;
        return;
    }
    GameEntry e;
    e.tid = tid; e.tex = nullptr; e.w = e.h = 0; e.coverState = 0;
    if (auroraName) e.auroraName = auroraName;
    if (cameraName) e.cameraName = cameraName;
    g_games.push_back(e);
}

bool GameNameLess(const GameEntry& a, const GameEntry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; }

// Games of Aurora (exact names) + games seen by the plugin + games that have a
// profile + games that have a learned camera line.
void BuildGameLibrary() {
    FreeCovers();
    g_games.clear();
    g_profiles = g_msValid ? ListMouseProfiles(g_msVid, g_msPid, ModelConfigPath().c_str()) : std::vector<MouseProfileInfo>();
    ModelOwnWriteDone();   // ListMouseProfiles reloaded the same file

    std::string db;
    if (plat::ReadWholeFile(plat::CacheFile("content.db"), db) && !db.empty()) {
        std::vector<AuroraTitle> titles;
        ReadAuroraTitles((const unsigned char*)db.data(), db.size(), titles);
        for (size_t i = 0; i < titles.size(); i++) AddGame(titles[i].titleId, titles[i].name.c_str(), nullptr);
    }
    const std::vector<uint32_t> seen = ListKnownTitleIds(plat::WorkFile("known_titles.txt").c_str());
    for (size_t i = 0; i < seen.size(); i++) {
        // System Title IDs (dashboards, Aurora, tools) are not games.
        if ((seen[i] >> 16) >= 0xFFFE && !HasProfile(seen[i])) continue;
        AddGame(seen[i], nullptr, nullptr);
    }
    for (size_t i = 0; i < g_profiles.size(); i++) AddGame(g_profiles[i].titleId, nullptr, nullptr);

    std::string cams;
    plat::ReadWholeFile(plat::WorkFile("camera_addresses.txt"), cams);
    size_t start = 0;
    while (start < cams.size()) {
        size_t end = cams.find('\n', start);
        if (end == std::string::npos) end = cams.size();
        const std::string line = cams.substr(start, end - start);
        start = end + 1;
        LearnedCamera cam;
        if (CamParseLine(line.c_str(), &cam)) AddGame(cam.titleId, nullptr, cam.name);
    }
    // Built-in cameras only give a readable name to games already listed.
    for (size_t i = 0; i < kCamKnownCount; i++) {
        LearnedCamera cam;
        if (!CamParseLine(kCamKnownLines[i], &cam)) continue;
        for (size_t k = 0; k < g_games.size(); k++)
            if (g_games[k].tid == cam.titleId && g_games[k].cameraName.empty()) g_games[k].cameraName = cam.name;
    }
    for (size_t i = 0; i < g_games.size(); i++) {
        GameEntry& e = g_games[i];
        const char* pn = ProfileName(e.tid);
        const bool prefix = pn[0] && !e.auroraName.empty() && e.auroraName.compare(0, strlen(pn), pn) == 0;
        if (pn[0] && !prefix) e.name = pn;
        else if (!e.auroraName.empty()) e.name = e.auroraName;
        else if (pn[0]) e.name = pn;
        else if (!e.cameraName.empty()) e.name = e.cameraName;
        else {
            char buf[32];
            _snprintf(buf, sizeof(buf) - 1, "Title ID %08X", (unsigned)e.tid); buf[sizeof(buf) - 1] = 0;
            e.name = buf;
        }
    }
    std::sort(g_games.begin(), g_games.end(), GameNameLess);
}

void* LoadCoverFromCache(uint32_t tid, int maxW, int maxH, int& w, int& h) {
    char name[40];
    _snprintf(name, sizeof(name) - 1, "covers\\%08X.asset", (unsigned)tid); name[sizeof(name) - 1] = 0;
    std::string bytes;
    if (!plat::ReadWholeFile(plat::CacheFile(name), bytes) || bytes.empty()) return nullptr;
    std::vector<unsigned char> rgba;
    if (!DecodeAuroraAsset((const unsigned char*)bytes.data(), bytes.size(), AURORA_ASSET_BOXART, maxW, maxH, rgba, w, h, true))
        return nullptr;
    return ModelCreateTexture(&rgba[0], w, h);
}

bool CoverCached(uint32_t tid) {
    char name[40];
    _snprintf(name, sizeof(name) - 1, "covers\\%08X.asset", (unsigned)tid); name[sizeof(name) - 1] = 0;
    return plat::FileExists(plat::CacheFile(name));
}

void DrawGamesPage(float w, float h) {
    static GlossyGamesModel gm;
    static std::vector<GlossyGame> s_view;
    static uint64_t s_titlesStamp = ~0ull, s_camStamp = ~0ull;
    static uint32_t s_extGen = 0, s_auroraGen = ~0u;
    static uint16_t s_vid = 0, s_pid = 0;
    static char s_nameBuf[32] = "";
    static bool s_nameOpen = false;

    const uint64_t titlesStamp = plat::FileStamp(plat::WorkFile("known_titles.txt"));
    const uint64_t camStamp = plat::FileStamp(plat::WorkFile("camera_addresses.txt"));
    const uint32_t auroraGen = consync::AuroraGeneration();
    const bool auroraChanged = (s_auroraGen != auroraGen);
    static bool s_dbSeen = false;
    const bool dbNow = plat::FileExists(plat::CacheFile("content.db"));
    if (g_gamesDirty || titlesStamp != s_titlesStamp || camStamp != s_camStamp || s_extGen != g_externalGen ||
        s_vid != g_msVid || s_pid != g_msPid || dbNow != s_dbSeen) {
        g_gamesDirty = false;
        s_titlesStamp = titlesStamp; s_camStamp = camStamp; s_extGen = g_externalGen;
        s_vid = g_msVid; s_pid = g_msPid; s_dbSeen = dbNow;
        BuildGameLibrary();
        LoadEditorForSelection();
    }
    if (auroraChanged) {
        // A cover arrived: games still waiting look in the cache again.
        s_auroraGen = auroraGen;
        for (size_t i = 0; i < g_games.size(); i++)
            if (g_games[i].coverState == 1) g_games[i].coverState = 0;
        if (!g_bigCover) g_bigTid = 0;
    }

    s_view.resize(g_games.size());
    int selected = -1;
    for (size_t i = 0; i < g_games.size(); i++) {
        GlossyGame& v = s_view[i];
        v.titleId = g_games[i].tid;
        v.name = g_games[i].name.c_str();
        v.cover = (ImTextureID)g_games[i].tex;
        v.coverW = g_games[i].w; v.coverH = g_games[i].h;
        v.hasProfile = HasProfile(v.titleId);
        if (v.titleId == g_selectedTid) selected = (int)i;
    }
    gm.games = s_view.empty() ? nullptr : &s_view[0];
    gm.gameCount = (int)s_view.size();
    gm.selected = selected;
    gm.auroraFound = consync::AuroraFound() || dbNow;
    const ModelDevice mouse = ModelSelectedMouse();
    gm.mouseName = mouse.valid ? mouse.name : nullptr;
    gm.sensitivity = &g_profSensitivity; gm.deadzone = &g_profDeadzone; gm.antiDeadzone = &g_profAntiDeadzone;
    gm.stickLayout = &g_profStickLayout; gm.antiDeadzoneY = &g_profAntiDeadzoneY; gm.smoothing = &g_profSmoothing;
    gm.invertY = &g_profInvertY; gm.curveType = &g_profCurveType; gm.curveExp = &g_profCurveExp;
    gm.bigCover = (selected >= 0 && g_bigTid == g_selectedTid) ? (ImTextureID)g_bigCover : 0;
    gm.bigW = g_bigW; gm.bigH = g_bigH;
    gm.nameEditing = false;
    gm.nameBuf = s_nameBuf;

    DrawUiGlossyGames(gm, g_tab, w, h);

    if (gm.selected >= 0 && gm.selected < (int)g_games.size()) g_selectedTid = g_games[gm.selected].tid;
    else g_selectedTid = 0;
    if (gm.reqSelect && gm.selected >= 0) LoadEditorForSelection();

    // --- covers: one per frame at most, visible cells first ---------------------
    bool didOne = false;
    for (int i = gm.firstVisible; i <= gm.lastVisible && i < (int)g_games.size() && !didOne; i++) {
        if (i < 0) continue;
        GameEntry& e = g_games[i];
        if (e.coverState != 0) continue;
        if (CoverCached(e.tid)) {
            e.tex = LoadCoverFromCache(e.tid, 128, 176, e.w, e.h);
            e.coverState = 2;
            didOne = true;
        } else {
            consync::RequestCover(e.tid);
            e.coverState = 1;
        }
    }
    if (gm.selected >= 0 && g_bigTid != g_selectedTid && !didOne) {
        ModelReleaseTexture(g_bigCover);
        g_bigCover = LoadCoverFromCache(g_selectedTid, 300, 410, g_bigW, g_bigH);
        g_bigTid = g_selectedTid;
    }

    // --- rename: a real text field on the PC -------------------------------------
    if (gm.reqEditName && gm.selected >= 0) {
        _snprintf(s_nameBuf, sizeof(s_nameBuf) - 1, "%s", g_games[gm.selected].name.c_str());
        s_nameBuf[sizeof(s_nameBuf) - 1] = 0;
        s_nameOpen = true;
        ImGui::OpenPopup("##rename_game");
    }
    bool renamed = false;
    if (s_nameOpen) {
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("##rename_game", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextUnformatted(L4("Nom du jeu", "Game name", "Nombre del juego", "Nome do jogo"));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(420.0f * (w / 1280.0f));
            const bool enter = ImGui::InputText("##name", s_nameBuf, sizeof(s_nameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("OK") || enter) { renamed = s_nameBuf[0] != 0; s_nameOpen = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(L4("Annuler", "Cancel", "Cancelar", "Cancelar")) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
                s_nameOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else {
            s_nameOpen = false;
        }
    }

    // --- actions --------------------------------------------------------------------
    if ((gm.reqSave || renamed) && gm.selected >= 0) {
        if (!g_msValid) NoDeviceToast();
        else {
            const GameEntry& e = g_games[gm.selected];
            char name[32];
            _snprintf(name, sizeof(name) - 1, "%s", renamed ? s_nameBuf : e.name.c_str()); name[sizeof(name) - 1] = 0;
            const uint8_t curveType = (g_profCurveType == 1) ? CURVE_EXPONENTIAL : CURVE_LINEAR;
            const bool ok = ApplyMouseProfileFull(g_msVid, g_msPid, e.tid, g_profSensitivity, g_profInvertY, g_profDeadzone,
                                                  curveType, g_profCurveExp, name, g_profAntiDeadzone,
                                                  ModelConfigPath().c_str(), g_profStickLayout, g_profAntiDeadzoneY, g_profSmoothing);
            ModelOwnWriteDone();
            plat::Log(ok ? "profile %08X saved (%s)" : "FAILED to save profile %08X (%s)", (unsigned)e.tid, name);
            if (!ok || gm.reqSaveExplicit || renamed) SavedToast(ok);
            gm.savedAt = ImGui::GetTime();
            gm.savedOk = ok;
            if (ok) g_gamesDirty = true;
        }
    }
    if (gm.reqDeleteAll && g_msValid) {
        int n = 0;
        const std::vector<MouseProfileInfo> all = g_profiles;
        for (size_t k = 0; k < all.size(); k++)
            if (RemoveMouseProfile(g_msVid, g_msPid, all[k].titleId, ModelConfigPath().c_str())) n++;
        ModelOwnWriteDone();
        plat::Log("%d profile(s) deleted for %04X:%04X", n, g_msVid, g_msPid);
        plat::Toast("%s", n ? L4("Profils supprimés", "Profiles deleted", "Perfiles eliminados", "Perfis excluídos")
                            : L4("Aucun profil à supprimer", "No profile to delete", "Ningún perfil que eliminar", "Nenhum perfil para excluir"));
        g_gamesDirty = true;
    }
    if (gm.reqDelete && gm.selected >= 0 && g_msValid) {
        const uint32_t tid = g_games[gm.selected].tid;
        const bool ok = RemoveMouseProfile(g_msVid, g_msPid, tid, ModelConfigPath().c_str());
        ModelOwnWriteDone();
        plat::Log(ok ? "profile %08X deleted" : "FAILED to delete profile %08X", (unsigned)tid);
        plat::Toast("%s", ok ? L4("Profil supprimé", "Profile deleted", "Perfil eliminado", "Perfil excluído")
                             : L4("ÉCHEC de la suppression", "Delete FAILED", "ERROR al eliminar", "FALHA ao excluir"));
        if (ok) g_gamesDirty = true;
    }
}

}  // namespace

// ============================================================================
// Public
// ============================================================================

std::string ModelConfigPath() { return plat::WorkFile("X360Remap.json"); }

bool ModelValidateConfigJson(const std::string& json) {
    // A parse error leaves the tables untouched; a success replaces them, so
    // the next frame re-reads the working copy to be sure both match.
    const bool ok = LoadMappingsFromJson(json);
    g_cfgStamp = ~0ull;
    return ok;
}

void ModelWorkingCopyChanged() {
    g_cfgStamp = ~0ull;
    g_gamesDirty = true;
}

void ModelOwnWriteDone() {
    // The tables in memory already hold what was just written.
    g_cfgStamp = plat::FileStamp(ModelConfigPath());
    g_cfgGen++;
}

void ModelRefreshGames() { g_gamesDirty = true; }

int  ModelActiveTab() { return g_tab; }
void ModelOpenPad(int index) { g_padWanted = index; }
void ModelSetTab(int tab) { if (tab >= 0 && tab <= 4) g_tab = tab; }
void ModelSelectGame(uint32_t titleId) { g_selectedTid = titleId; g_gamesDirty = true; }
void ModelSkipSplash() { g_skipSplash = true; }

void ModelRescanPcDevices() { g_pcScanWanted = true; }

std::vector<ModelPcDevice> ModelPcDevices() {
    std::vector<ModelPcDevice> out;
    const std::vector<KnownDeviceInfo> console = ListKnownDevices(plat::WorkFile("known_devices.txt").c_str());
    for (size_t i = 0; i < g_pcDevices.size(); i++) {
        const plat::PcDevice& d = g_pcDevices[i];
        ModelPcDevice v;
        v.vid = d.vid; v.pid = d.pid; v.kind = d.kind; v.name = d.name;
        bool known = false;
        for (size_t k = 0; k < console.size() && !known; k++)
            known = console[k].vid == d.vid && console[k].pid == d.pid && console[k].isMouse == (d.kind == 'M');
        const int a = PcAnswer(d.vid, d.pid, d.kind);
        v.state = known ? 3 : a == 1 ? 1 : a == 0 ? 2 : 0;
        out.push_back(v);
    }
    return out;
}

int ModelPcDevicesToAsk() {
    const std::vector<ModelPcDevice> all = ModelPcDevices();
    int n = 0;
    for (size_t i = 0; i < all.size(); i++) n += all[i].state == 0 && all[i].kind != 'G';
    return n;
}

void ModelAnswerPcDevice(uint16_t vid, uint16_t pid, char kind, bool same) {
    const std::string key = PcKey(vid, pid, kind);
    for (size_t i = 0; i < g_pcAnswers.size(); i++)
        if (g_pcAnswers[i].compare(0, key.size(), key) == 0) { g_pcAnswers.erase(g_pcAnswers.begin() + i); break; }
    g_pcAnswers.push_back(key + (same ? ":Y" : ":N"));
    std::string text;
    for (size_t i = 0; i < g_pcAnswers.size(); i++) text += g_pcAnswers[i] + "\r\n";
    plat::WriteWholeFile(plat::DataDir() + "pc_devices.txt", text);
    g_pcGen++;
    // Yes: this device becomes the one being set up, so the next change applies to it.
    if (same && kind == 'K') { g_kbVid = vid; g_kbPid = pid; }
    if (same && kind == 'M') { g_msVid = vid; g_msPid = pid; }
}

bool ModelKnownKeypadPresent() {
    for (size_t i = 0; i < g_keyboards.size(); i++)
        if (IsKnownKeypad(g_keyboards[i].vid, g_keyboards[i].pid)) return true;
    return false;
}

void* ModelBackgroundTexture() {
    return g_glossyTex[GA_BG_PAGE_HALF].present ? (void*)g_glossyTex[GA_BG_PAGE_HALF].tex : nullptr;
}
bool ModelCapturing() { return g_manual != nullptr || (g_wizard && g_wizard->IsActive()); }

static ModelDevice MakeDevice(const std::vector<Device>& list, bool valid, uint16_t vid, uint16_t pid) {
    ModelDevice d;
    memset(&d, 0, sizeof(d));
    const int i = valid ? IndexOf(list, vid, pid) : -1;
    if (i < 0) return d;
    d.valid = true;
    d.vid = vid; d.pid = pid;
    _snprintf(d.name, sizeof(d.name) - 1, "%s", list[i].name.c_str());
    return d;
}
ModelDevice ModelSelectedMouse()    { return MakeDevice(g_mice, g_msValid, g_msVid, g_msPid); }
ModelDevice ModelSelectedKeyboard() { return MakeDevice(g_keyboards, g_kbValid, g_kbVid, g_kbPid); }

bool ModelSelectedGame(uint32_t& titleId, std::string& name) {
    for (size_t i = 0; i < g_games.size(); i++) {
        if (g_games[i].tid != g_selectedTid) continue;
        titleId = g_games[i].tid;
        name = g_games[i].name;
        return true;
    }
    return false;
}

bool ModelGetMouseExtra(MouseExtra& out) {
    memset(&out, 0, sizeof(out));
    out.curveExponent = 1.5f;
    if (!g_msValid) return false;
    EnsureConfigLoaded();
    const HidDeviceMapping* m = FindMapping(g_msVid, g_msPid);
    if (!m) return true;
    out.adsSensitivity = m->adsSensitivity;
    out.directAimSmoothingMs = m->directAimSmoothingMs;
    out.curveType = (m->sensitivityCurveType == CURVE_EXPONENTIAL) ? 1 : 0;
    if (out.curveType == 1 && m->sensitivityCurveExponent > 0.0f) out.curveExponent = m->sensitivityCurveExponent;
    return true;
}

bool ModelSetMouseExtra(const MouseExtra& in) {
    if (!g_msValid) return false;
    const std::string path = ModelConfigPath();
    if (!LoadMappingsForUpdate(path)) return false;
    HidDeviceMapping* m = nullptr;
    for (size_t i = 0; i < g_dynamicMappings.size(); i++)
        if (g_dynamicMappings[i].vendorId == g_msVid && g_dynamicMappings[i].productId == g_msPid) m = &g_dynamicMappings[i];
    if (!m) {
        HidDeviceMapping fresh = {};
        fresh.vendorId = g_msVid;
        fresh.productId = g_msPid;
        g_dynamicMappings.push_back(fresh);
        m = &g_dynamicMappings.back();
    }
    m->adsSensitivity = (in.adsSensitivity > 0 && in.adsSensitivity <= 400 && in.adsSensitivity != 100) ? in.adsSensitivity : 0;
    m->directAimSmoothingMs = (in.directAimSmoothingMs > 0 && in.directAimSmoothingMs <= 200) ? in.directAimSmoothingMs : 0;
    m->sensitivityCurveType = (in.curveType == 1) ? CURVE_EXPONENTIAL : CURVE_LINEAR;
    m->sensitivityCurveExponent = (in.curveType == 1) ? in.curveExponent : 0.0f;
    const bool ok = SaveMappingsToFile(path);
    ModelOwnWriteDone();
    return ok;
}

void* ModelCreateTexture(const unsigned char* rgba, int w, int h) {
    if (!g_device || !rgba || w <= 0 || h <= 0) return nullptr;
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)w; desc.Height = (UINT)h;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub;
    sub.pSysMem = rgba; sub.SysMemPitch = (UINT)(w * 4); sub.SysMemSlicePitch = 0;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &sub, &tex)) || !tex) return nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    g_device->CreateShaderResourceView(tex, nullptr, &view);
    tex->Release();
    return view;
}

void ModelReleaseTexture(void* texture) {
    if (texture) ((ID3D11ShaderResourceView*)texture)->Release();
}

void ModelInit(ID3D11Device* device) {
    g_device = device;
    const int bad = CheckGlossyAssetTable();
    if (bad >= 0) plat::Log("glossy: manifest misaligned at entry %d", bad);
    int ok = 0;
    for (int i = 0; i < GA_COUNT; i++) {
        int w = 0, h = 0, comp = 0;
        unsigned char* px = bad < 0 ? stbi_load_from_memory(g_glossyAssets[i].data, g_glossyAssets[i].size, &w, &h, &comp, 4) : nullptr;
        if (px) {
            g_glossyTex[i].tex = (ImTextureID)ModelCreateTexture(px, w, h);
            g_glossyTex[i].w = w; g_glossyTex[i].h = h;
            g_glossyTex[i].present = g_glossyTex[i].tex != 0;
            stbi_image_free(px);
        }
        if (g_glossyTex[i].present) ok++;
        else plat::Log("glossy: FAILED texture %s", g_glossyAssets[i].name);
    }
    UiGlossySetTextures(g_glossyTex);
    plat::Log("glossy: %d/%d textures loaded", ok, (int)GA_COUNT);

    // First start: an empty configuration, exactly like the console Studio
    // ("[]" = no custom setting, the plugin applies its defaults).
    if (!plat::FileExists(ModelConfigPath())) plat::WriteWholeFile(ModelConfigPath(), "[]\n");
    RecoverConfigFromTmp(ModelConfigPath());
}

void ModelShutdown() {
    FreeCovers();
    g_games.clear();
    for (int i = 0; i < GA_COUNT; i++) {
        ModelReleaseTexture((void*)g_glossyTex[i].tex);
        g_glossyTex[i] = UiSkinTex();
    }
    delete g_wizard; g_wizard = nullptr;
    delete g_manual; g_manual = nullptr;
}

// ============================================================================
// In-game display page (overlay_settings.txt, CONTRACT.md): the console Studio's
// page on the PC's working copy. "Save" writes that copy; "Send" takes it to the
// console with the other files (sync.cpp also keeps enable_overlay.txt in step).
// ============================================================================
static uint32_t PackOverlayTexel(unsigned r, unsigned g, unsigned b, unsigned a) {
    return r | (g << 8) | (b << 16) | (a << 24);   // R, G, B, A bytes: DXGI_FORMAT_R8G8B8A8_UNORM
}

static void DrawOverlayPage(float w, float h) {
    static OverlaySettings s_set, s_shown;
    static GlossyOverlayModel s_om;
    static uint64_t s_stamp = 0;
    static bool s_loaded = false;
    static void* s_tex = nullptr;
    static int s_prevKey = -1;

    // Follows the file: first visit, a pull from the console, a restore.
    const std::string path = plat::WorkFile("overlay_settings.txt");
    const uint64_t stamp = plat::FileStamp(path);
    if (!s_loaded || stamp != s_stamp) {
        s_loaded = true;
        s_stamp = stamp;
        OverlayLoad(path.c_str(), s_set);   // no file yet = the plugin's defaults
        s_om.s = &s_set;
    }
    if (!s_tex || !OverlaySameLook(s_shown, s_set)) {
        static std::vector<unsigned char> s_pixels;
        s_pixels.resize((size_t)OVPV_W * OVPV_H * 4);
        OverlayPreviewRender(s_set, &s_pixels[0], OVPV_W * 4, PackOverlayTexel);
        ModelReleaseTexture(s_tex);
        s_tex = ModelCreateTexture(&s_pixels[0], OVPV_W, OVPV_H);
        s_shown = s_set;
    }
    s_om.overlayTex = (ImTextureID)s_tex;
    // A game picture behind the drawing (HB, 2026-11-18), decoded once.
    static void* s_bg = nullptr;
    static bool s_bgTried = false;
    if (!s_bgTried) {
        s_bgTried = true;
        int bw = 0, bh = 0, comp = 0;
        unsigned char* px = stbi_load_from_memory(kPreviewBgJpg, (int)kPreviewBgJpgSize, &bw, &bh, &comp, 4);
        if (px) { s_bg = ModelCreateTexture(px, bw, bh); stbi_image_free(px); }
        else plat::Log("preview: game picture could not be decoded");
    }
    s_om.previewTex = (ImTextureID)s_bg;
    const int key = (int)KeyCaptureHeldCode();
    s_om.keyEvent = (key > 0 && key != s_prevKey) ? key : 0;
    s_prevKey = key;

    DrawUiGlossyOverlay(s_om, g_tab, w, h);
    if (s_om.reqSave) {
        s_om.reqSave = false;
        s_om.savedOk = OverlaySave(path.c_str(), s_set);
        s_stamp = plat::FileStamp(path);   // our own write: what is on screen stays
        s_om.savedAt = ImGui::GetTime();
        if (s_om.savedOk)
            plat::Toast("%s", L4("Enregistr\xC3\xA9 sur ce PC : clique sur Envoyer pour la console",
                                 "Saved on this PC: click Send for the console",
                                 "Guardado en este PC: pulsa Enviar para la consola",
                                 "Salvo neste PC: clique em Enviar para o console"));
    }
}

void ModelDrawPages(float w, float h) {
    EnsureConfigLoaded();
    ScanPcDevicesIfWanted();
    RefreshDevices();
    // Selection kept by VID:PID so it survives a refreshed list; first device
    // of each list when the previous one is gone.
    g_msValid = IndexOf(g_mice, g_msVid, g_msPid) >= 0;
    if (!g_msValid && !g_mice.empty()) { g_msValid = true; g_msVid = g_mice[0].vid; g_msPid = g_mice[0].pid; }
    g_kbValid = IndexOf(g_keyboards, g_kbVid, g_kbPid) >= 0;
    if (!g_kbValid && !g_keyboards.empty()) { g_kbValid = true; g_kbVid = g_keyboards[0].vid; g_kbPid = g_keyboards[0].pid; }

    // The values on screen follow the file when it was replaced from outside
    // (pull, open, restore) or when another mouse is selected.
    {
        static uint32_t s_editorGen = 0;
        static uint16_t s_editorVid = 0, s_editorPid = 0;
        static bool     s_editorValid = false;
        if (s_editorGen != g_externalGen || s_editorVid != g_msVid || s_editorPid != g_msPid || s_editorValid != g_msValid) {
            s_editorGen = g_externalGen; s_editorVid = g_msVid; s_editorPid = g_msPid; s_editorValid = g_msValid;
            LoadMouseEditor();
        }
    }

    if (!UiGlossyReady()) {
        ImGui::SetCursorPos(ImVec2(40.0f, 40.0f));
        ImGui::TextUnformatted("X360Remap: textures could not be created (see X360RemapWindows.log).");
        return;
    }

    // Same five tabs, in the same order, as the header the console Studio draws (ui_glossy.cpp).
    if (g_tab == 4) {
        GlossySupportModel sm;
        sm.version = kWinAppVersion;
        sm.pluginAlive = consync::State() == consync::LINK_CONNECTED;
        sm.releasesUrl = IsLinkIntact(kReleasesUrl, kReleasesUrlHash) ? kReleasesUrl : nullptr;
        sm.kofiUrl = IsLinkIntact(kKofiUrl, kKofiUrlHash) ? kKofiUrl : nullptr;
        sm.paypal = IsLinkIntact(kPayPalEmail, kPayPalEmailHash) ? kPayPalEmail : nullptr;
        sm.githubUrl = IsLinkIntact(kGithubUrl, kGithubUrlHash) ? kGithubUrl : nullptr;
        sm.youtubeUrl = nullptr;
        DrawUiGlossySupport(sm, g_tab, w, h);
    } else if (g_tab == 3) {
        DrawGuidePageWin(w, h, g_tab);   // two paged tutorials (guide_win.cpp)
    } else if (g_tab == 2) {
        DrawOverlayPage(w, h);
    } else if (g_tab == 1) {
        DrawGamesPage(w, h);
    } else {
        g_tab = 0;
        if (g_padView >= 0) DrawPadPageWin(w, h);   // a USB controller of the console (pill of the device bar)
        if (g_padView < 0) DrawMappingPage(w, h);
    }

    std::string toast;
    float alpha = 1.0f;
    if (plat::CurrentToast(toast, alpha)) DrawUiGlossyToast(toast.c_str(), alpha, w, h);

    static double s_splashStart = -1.0;
    static bool   s_splashDone = false;
    if (!s_splashDone && !g_skipSplash) {
        if (s_splashStart < 0.0) s_splashStart = plat::Seconds();
        float t = (float)(plat::Seconds() - s_splashStart);
        if (t > 0.4f && t < 1.75f && (ImGui::GetIO().MouseClicked[0] || KeyCaptureHeldCode() >= 0)) {
            s_splashStart = plat::Seconds() - 1.75;
            t = 1.75f;
        }
        char ver[24];
        _snprintf(ver, sizeof(ver) - 1, "v%s", kWinAppVersion); ver[sizeof(ver) - 1] = 0;
        if (!DrawUiGlossySplash(t, w, h, ver)) s_splashDone = true;
    }
}
