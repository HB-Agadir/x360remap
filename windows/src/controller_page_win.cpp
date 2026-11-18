// Controller page of the Windows Studio (2026-11-17, HB; VISION.md 5f): the USB
// controller's buttons for the console, set from the PC.
//  - a large controller picture; the same controller plugged into the PC lights it live,
//    through the mapping being edited (so what lights is what the console will do);
//  - click an Xbox button, press the controller's button: bound (a button already used
//    elsewhere is swapped, never duplicated);
//  - presets: SDL default layout, swap bumpers and triggers;
//  - live sticks with the dead zone drawn, right stick Y inversion.
// Reads and writes the working copy of X360Remap.json through config_writer (same
// functions as the console Studio's Controller page); "Send" takes it to the console.
#include "controller_page_win.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "../../application/config_writer.h"
#include "../../application/ui_glossy.h"
#include "../../X360Remap/mapping.h"
#include "key_capture_win.h"
#include "model_win.h"
#include "pad_art_win.h"
#include "pad_input_win.h"
#include "platform_win.h"
#include "ui_kit_win.h"
#include "win_i18n.h"

using namespace kit;

namespace {

// Cells, in kPadFieldNames order: A B X Y LB RB LT RT L3 R3 START BACK GUIDE DU DD DL DR.
const char* const kCellLabel[PAD_CELLS] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L3", "R3",
                                            "START", "BACK", "GUIDE", "D-UP", "D-DOWN", "D-LEFT", "D-RIGHT" };
// SDL list order (KnownControllerEntry::btn) -> cell.
const int kSdlToCell[KC_BUTTONS] = { 0, 1, 2, 3, 4, 5, 6, 7, 11, 10, 8, 9, 12, 15, 16, 13, 14 };

struct Pad { uint16_t vid, pid; std::string name; bool onPc; };

// Each cell on the white controller picture (pad_art_win.h), in picture pixels:
// centre x, y, width, height, shape 0 circle / 1 rounded bar.
struct Spot { float x, y, w, h; int shape; };
const Spot kSpot[PAD_CELLS] = {
    { 428, 217, 34, 34, 0 }, { 467, 184, 34, 34, 0 }, { 386, 184, 34, 34, 0 }, { 428, 151, 34, 34, 0 },   // A B X Y
    { 108, 95, 104, 22, 1 }, { 432, 95, 104, 22, 1 },                                                       // LB RB
    { 110, 42, 52, 58, 1 }, { 430, 42, 52, 58, 1 },                                                         // LT RT
    { 113, 183, 70, 70, 0 }, { 340, 262, 70, 70, 0 },                                                       // L3 R3 (sticks)
    { 323, 183, 26, 26, 0 }, { 216, 183, 26, 26, 0 }, { 270, 183, 46, 46, 0 },                              // START BACK GUIDE
    { 195, 236, 24, 26, 1 }, { 195, 289, 24, 26, 1 }, { 168, 262, 26, 24, 1 }, { 222, 262, 26, 24, 1 },     // D-pad
};

Pad PadFrom(const HidDeviceMapping& m) {
    Pad p; p.vid = m.vendorId; p.pid = m.productId; p.onPc = false;
    const KnownControllerEntry* e = FindKnownController(p.vid, p.pid);
    p.name = e ? e->name : L4("Manette USB", "USB controller", "Mando USB", "Controle USB");
    return p;
}

}  // namespace

static uint16_t s_vid = 0, s_pid = 0;   // controller shown (shared with the Macros page)

bool ControllerPageCurrentPad(uint16_t& vid, uint16_t& pid, std::string& name) {
    if (!s_vid && !s_pid) {   // page never opened: first controller of the file
        for (size_t i = 0; i < g_dynamicMappings.size(); i++)
            if (g_dynamicMappings[i].axisMapCount > 0) { s_vid = g_dynamicMappings[i].vendorId; s_pid = g_dynamicMappings[i].productId; break; }
        if (!s_vid && !s_pid) return false;
    }
    vid = s_vid; pid = s_pid;
    const KnownControllerEntry* e = FindKnownController(vid, pid);
    name = e ? e->name : L4("Manette USB", "USB controller", "Mando USB", "Controle USB");
    return true;
}

void DrawControllerPageWin(float w, float h, int& tab) {
    (void)h;
    static PadConfig s_cfg, s_edit;
    static uint64_t s_stamp = ~0ull;
    static bool s_dirty = false;
    static int s_capture = -1, s_prevKey = -1;
    static uint32_t s_prevButtons = 0;
    static double s_savedAt = -1.0;
    static bool s_savedOk = false;

    UiGlossyHeader(tab, w);
    Begin(w);

    // Controllers: the ones the console wrote into X360Remap.json, plus the one on this PC.
    PcPadRaw raw;
    const bool pcPad = ReadPcPad(raw);
    std::vector<Pad> pads;
    for (size_t i = 0; i < g_dynamicMappings.size(); i++)
        if (g_dynamicMappings[i].axisMapCount > 0) pads.push_back(PadFrom(g_dynamicMappings[i]));
    if (pcPad) {
        bool found = false;
        for (size_t i = 0; i < pads.size(); i++)
            if (pads[i].vid == raw.vid && pads[i].pid == raw.pid) { pads[i].onPc = true; pads[i].name = raw.name; found = true; }
        if (!found) { Pad p; p.vid = raw.vid; p.pid = raw.pid; p.name = raw.name; p.onPc = true; pads.push_back(p); }
    }
    int sel = -1;
    for (size_t i = 0; i < pads.size(); i++) if (pads[i].vid == s_vid && pads[i].pid == s_pid) sel = (int)i;
    if (sel < 0 && !pads.empty()) {
        sel = 0;
        for (size_t i = 0; i < pads.size(); i++) if (pads[i].onPc) { sel = (int)i; break; }
    }

    const std::string path = ModelConfigPath();
    const uint64_t stamp = plat::FileStamp(path);
    if (sel >= 0 && (pads[sel].vid != s_vid || pads[sel].pid != s_pid || (stamp != s_stamp && !s_dirty))) {
        s_vid = pads[sel].vid; s_pid = pads[sel].pid; s_stamp = stamp;
        if (!ReadPadConfig(s_vid, s_pid, s_cfg, path.c_str())) { memset(&s_cfg, 0, sizeof(s_cfg)); for (int c = 0; c < PAD_CELLS; c++) s_cfg.btnIdx[c] = -1; }
        s_edit = s_cfg; s_dirty = false; s_capture = -1;
    }

    Txt(g_fontGBody, 19.0f, 640.0f, 108.0f, kTxt2,
        L4("Les boutons de ta manette USB pour la console. Branche-la aussi sur ce PC : tout s'allume en direct.",
           "Your USB controller's buttons for the console. Plug it into this PC too: everything lights up live.",
           "Los botones de tu mando USB para la consola. Con\xC3\xA9" "ctalo tambi\xC3\xA9n a este PC: todo se ilumina en directo.",
           "Os bot\xC3\xB5" "es do seu controle USB para o console. Conecte-o tamb\xC3\xA9m a este PC: tudo acende ao vivo."), 1);

    // ------------------------------------------------------------- picture (left)
    const float lx = 44.0f, ly = 146.0f, lw = 800.0f, lh = 524.0f;
    Card(lx, ly, lw, lh);
    if (sel < 0) {
        Txt(g_fontGTitle, 28.0f, lx + lw * 0.5f, ly + 190.0f, kGold, L4("Aucune manette USB", "No USB controller", "Ning\xC3\xBAn mando USB", "Nenhum controle USB"), 1);
        Txt(g_fontGBody, 18.0f, lx + lw * 0.5f, ly + 240.0f, kTxt2,
            L4("Branche-la sur ce PC, ou sur la console puis clique sur Recevoir.", "Plug it into this PC, or into the console then click Receive.",
               "Con\xC3\xA9" "ctalo a este PC, o a la consola y pulsa Recibir.", "Conecte-o a este PC, ou ao console e clique em Receber."), 1);
        DrawIcon(IC_PAD, lx + lw * 0.5f, ly + 360.0f, 90.0f, kTxt3);
        return;
    }
    const Pad& pad = pads[sel];
    const bool live = pad.onPc && pcPad;
    // header: name, id, verdict
    {
        char id[16];
        _snprintf(id, sizeof(id) - 1, "%04X:%04X", pad.vid, pad.pid); id[sizeof(id) - 1] = 0;
        const float nw = Txt(g_fontGTitle, 26.0f, lx + 20.0f, ly + 14.0f, kGold, pad.name.c_str());
        Txt(g_fontGSmall, 15.0f, lx + 28.0f + nw, ly + 24.0f, kTxt3, id);
        if (pads.size() > 1) {   // next controller
            if (PillButton("##cp_next", lx + lw - 130.0f, ly + 16.0f, 110.0f, 30.0f, L4("Suivante", "Next", "Siguiente", "Pr\xC3\xB3ximo"), false)) {
                const Pad& nx = pads[(sel + 1) % pads.size()];
                s_vid = nx.vid; s_pid = nx.pid; s_stamp = ~0ull; s_dirty = false;
            }
        }
        const bool ready = s_cfg.hasEntry && s_cfg.hasAxes;
        const char* verdict = ready ? L4("Pr\xC3\xAA" "te pour la console", "Ready for the console", "Lista para la consola", "Pronto para o console")
                                    : L4("Branche-la une fois sur la console, ouvre la Studio, puis Recevoir",
                                         "Plug it into the console once, open the Studio, then Receive",
                                         "Con\xC3\xA9" "ctalo una vez a la consola, abre el Studio y luego Recibir",
                                         "Conecte-o uma vez ao console, abra o Studio e depois Receber");
        const float vw = TxtW(g_fontGBodyM, 14.0f, verdict) + 30.0f;
        g_dl->AddRectFilled(P(lx + 20.0f, ly + 50.0f), P(lx + 20.0f + vw, ly + 74.0f), Col(ready ? 0x12301C : 0x3A2A0C), 12.0f * g_k);
        g_dl->AddCircleFilled(P(lx + 32.0f, ly + 62.0f), 4.5f * g_k, Col(ready ? kGreen : 0xF0B429), 12);
        Txt(g_fontGBodyM, 14.0f, lx + 42.0f, ly + 54.0f, ready ? 0xDFF7E2 : 0xFFE7B0, verdict);
        if (live) {
            const char* l = L4("EN DIRECT sur ce PC", "LIVE on this PC", "EN DIRECTO en este PC", "AO VIVO neste PC");
            const float x = lx + 30.0f + vw;
            g_dl->AddRectFilled(P(x, ly + 50.0f), P(x + TxtW(g_fontGBodyM, 14.0f, l) + 30.0f, ly + 74.0f), Col(0x0E2440), 12.0f * g_k);
            g_dl->AddCircleFilled(P(x + 12.0f, ly + 62.0f), 4.5f * g_k, Col(0x37D6FF), 12);
            Txt(g_fontGBodyM, 14.0f, x + 22.0f, ly + 54.0f, 0xD6F6FF, l);
        }
    }

    // capture: the next button pressed on the PC controller
    const uint32_t pressed = live ? raw.buttons : 0;
    const uint32_t rising = pressed & ~s_prevButtons;
    s_prevButtons = pressed;
    {
        const int key = (int)KeyCaptureHeldCode();
        if (s_capture >= 0 && key == 0x29 && key != s_prevKey) s_capture = -1;   // Esc
        s_prevKey = key;
    }
    if (s_capture >= 0 && rising) {
        int n = 0;
        while (!(rising & (1u << n))) n++;
        for (int c = 0; c < PAD_CELLS; c++)
            if (c != s_capture && s_edit.btnIdx[c] == n) s_edit.btnIdx[c] = s_edit.btnIdx[s_capture];   // swap, no duplicate
        s_edit.btnIdx[s_capture] = n;
        s_capture = -1;
        s_dirty = true;
    }

    // picture: the white controller, hot spots and live presses on top
    const float sc = 600.0f / PAD_ART_W, pw = PAD_ART_W * sc, ph = PAD_ART_H * sc;
    const float ox = lx + (lw - pw) * 0.5f, oy = ly + 100.0f;
    const float cx = lx + lw * 0.5f;
    if (void* art = ModelPadArtTexture()) g_dl->AddImage((ImTextureID)art, P(ox, oy), P(ox + pw, oy + ph));
    OverlayLiveInput xb;
    if (live) PcPadToXbox(raw, xb);
    const double t = ImGui::GetTime();
    for (int c = 0; c < PAD_CELLS; c++) {
        const Spot& s = kSpot[c];
        const float x = ox + s.x * sc, y = oy + s.y * sc, sw = s.w * sc, sh = s.h * sc;
        const int n = s_edit.btnIdx[c];
        bool lit = live && n >= 0 && (pressed & (1u << n));
        if (c >= 13 && live && raw.pov >= 0) {   // d-pad on the hat
            const uint16_t bit = c == 13 ? 0x0001 : c == 14 ? 0x0002 : c == 15 ? 0x0004 : 0x0008;
            lit = lit || (xb.padButtons & bit);
        }
        char id[16];
        _snprintf(id, sizeof(id) - 1, "##cp_c%d", c); id[sizeof(id) - 1] = 0;
        bool hot = false;
        if (Button(id, x - sw * 0.5f, y - sh * 0.5f, sw, sh, &hot)) s_capture = s_capture == c ? -1 : c;
        const bool cap = s_capture == c;
        const bool unbound = n < 0 && c < 12;
        const int alpha = lit ? 170 : (unbound ? 120 : 0);
        if (alpha) {
            const ImU32 tint = Col(lit ? kGold : 0x05090F, alpha);
            if (s.shape == 0) g_dl->AddCircleFilled(P(x, y), sw * 0.5f * g_k, tint, 32);
            else g_dl->AddRectFilled(P(x - sw * 0.5f, y - sh * 0.5f), P(x + sw * 0.5f, y + sh * 0.5f), tint, 7.0f * g_k);
        }
        if ((c == 8 || c == 9) && live) {   // where the live stick is
            const float sx = c == 8 ? xb.lx / 32768.0f : xb.rx / 32768.0f, sy = c == 8 ? -xb.ly / 32768.0f : -xb.ry / 32768.0f;
            g_dl->AddCircleFilled(P(x + sx * sw * 0.32f, y + sy * sw * 0.32f), 6.0f * g_k, Col(kGold), 16);
        }
        if (cap || hot) {
            const int a = cap ? (int)(155 + 100 * sin(t * 6.0)) : 210;
            if (s.shape == 0) g_dl->AddCircle(P(x, y), (sw * 0.5f + 4.0f) * g_k, Col(kGold, a), 32, 2.5f * g_k);
            else g_dl->AddRect(P(x - sw * 0.5f - 4, y - sh * 0.5f - 4), P(x + sw * 0.5f + 4, y + sh * 0.5f + 4), Col(kGold, a), 8.0f * g_k, ImDrawCornerFlags_All, 2.5f * g_k);
        }
    }
    // callouts outside the picture: the controller button behind each Xbox button ("B5" = HID idx 4),
    // per side sorted from top to bottom and kept 20 px apart so they never overlap
    for (int side = 0; side < 2; side++) {
        int order[12], n = 0;
        for (int c = 0; c < 12; c++) if ((kSpot[c].x < PAD_ART_W * 0.5f) == (side == 0)) order[n++] = c;
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && kSpot[order[j]].y < kSpot[order[j - 1]].y; j--) { const int k = order[j]; order[j] = order[j - 1]; order[j - 1] = k; }
        float lastY = -1000.0f;
        for (int i = 0; i < n; i++) {
            const int c = order[i];
            const Spot& s = kSpot[c];
            const float x = ox + s.x * sc, y = oy + s.y * sc;
            const float ly2 = y < lastY + 20.0f ? lastY + 20.0f : y;
            lastY = ly2;
            const bool left = side == 0;
            const float ex = left ? ox - 14.0f : ox + pw + 14.0f;
            const int b = s_edit.btnIdx[c];
            char tag[24];
            if (b >= 0) _snprintf(tag, sizeof(tag) - 1, "%s  B%d", kCellLabel[c], b + 1); else _snprintf(tag, sizeof(tag) - 1, "%s  -", kCellLabel[c]);
            tag[sizeof(tag) - 1] = 0;
            const float sx2 = x + (left ? -s.w : s.w) * sc * 0.5f, kx = left ? ex + 30.0f : ex - 30.0f;
            g_dl->AddLine(P(sx2, y), P(kx, ly2), Col(kGold, 80), 1.0f * g_k);
            g_dl->AddLine(P(kx, ly2), P(ex, ly2), Col(kGold, 80), 1.0f * g_k);
            g_dl->AddCircleFilled(P(ex, ly2), 2.5f * g_k, Col(kGold, 200), 10);
            Txt(g_fontGBodyM, 13.0f, left ? ex - 6.0f : ex + 6.0f, ly2 - 8.0f, b >= 0 ? kTxt : 0x5A6A80, tag, left ? 2 : 0);
        }
    }
    // instruction line under the picture
    {
        char line[160];
        if (s_capture >= 0)
            _snprintf(line, sizeof(line) - 1, L4("Appuie sur le bouton de ta manette pour %s (\xC3\x89" "chap annule)", "Press your controller's button for %s (Esc cancels)",
                                                 "Pulsa el bot\xC3\xB3n de tu mando para %s (Esc cancela)", "Aperte o bot\xC3\xA3o do seu controle para %s (Esc cancela)"), kCellLabel[s_capture]);
        else
            _snprintf(line, sizeof(line) - 1, "%s", live ? L4("Clique un bouton Xbox, puis appuie sur le bouton de ta manette qui doit le faire.",
                                                             "Click an Xbox button, then press the controller button that must do it.",
                                                             "Haz clic en un bot\xC3\xB3n Xbox y pulsa el bot\xC3\xB3n del mando que debe hacerlo.",
                                                             "Clique num bot\xC3\xA3o Xbox e aperte o bot\xC3\xA3o do controle que deve faz\xC3\xAA-lo.")
                                                        : L4("Branche cette manette sur ce PC pour la r\xC3\xA9gler en appuyant sur ses boutons.",
                                                             "Plug this controller into this PC to set it by pressing its buttons.",
                                                             "Conecta este mando a este PC para ajustarlo pulsando sus botones.",
                                                             "Conecte este controle a este PC para ajust\xC3\xA1-lo apertando os bot\xC3\xB5" "es."));
        line[sizeof(line) - 1] = 0;
        Txt(g_fontGBodyM, 16.0f, cx, ly + lh - 36.0f, s_capture >= 0 ? kGold : kTxt2, line, 1);
    }

    // ------------------------------------------------------------- settings (right)
    const float rx = 860.0f, ry = 146.0f, rw = 376.0f, rh = 524.0f, cxr = rx + 20.0f, cw = rw - 40.0f;
    Card(rx, ry, rw, rh);
    Txt(g_fontGTitle, 28.0f, cxr, ry + 12.0f, kGold, L4("R\xC3\xA9glages", "Settings", "Ajustes", "Ajustes"));
    float y = ry + 62.0f;
    Section(cxr, y, L4("PR\xC3\x89R\xC3\x89GLAGES", "PRESETS", "PREAJUSTES", "PREDEFINI\xC3\x87\xC3\x95" "ES"));
    {
        const KnownControllerEntry* e = FindKnownController(pad.vid, pad.pid);
        if (PillButton("##cp_def", cxr, y + 22.0f, cw * 0.5f - 4.0f, 34.0f, L4("Disposition d'origine", "Original layout", "Disposici\xC3\xB3n original", "Layout original"), false) && e) {
            for (int c = 0; c < PAD_CELLS; c++) s_edit.btnIdx[c] = -1;
            for (int b = 0; b < KC_BUTTONS; b++) s_edit.btnIdx[kSdlToCell[b]] = e->btn[b];
            s_dirty = true;
        }
        if (PillButton("##cp_swap", cxr + cw * 0.5f + 4.0f, y + 22.0f, cw * 0.5f - 4.0f, 34.0f, L4("G\xC3\xA2" "chettes <> bumpers", "Triggers <> bumpers", "Gatillos <> bumpers", "Gatilhos <> bumpers"), false)) {
            int tmp = s_edit.btnIdx[4]; s_edit.btnIdx[4] = s_edit.btnIdx[6]; s_edit.btnIdx[6] = tmp;
            tmp = s_edit.btnIdx[5]; s_edit.btnIdx[5] = s_edit.btnIdx[7]; s_edit.btnIdx[7] = tmp;
            s_dirty = true;
        }
        if (!e) Txt(g_fontGSmall, 13.0f, cxr, y + 60.0f, kTxt3, L4("Disposition d'origine : manette absente de la liste SDL", "Original layout: controller not in the SDL list",
                                                                   "Disposici\xC3\xB3n original: mando fuera de la lista SDL", "Layout original: controle fora da lista SDL"));
    }
    y += 86.0f;

    Section(cxr, y, L4("STICKS", "STICKS", "STICKS", "ANAL\xC3\x93GICOS"));
    {
        const float r = 50.0f, c1 = cxr + 70.0f, c2 = cxr + cw - 70.0f, cyc = y + 82.0f;
        const float dz = s_edit.deadzone / 10000.0f;
        const float sticks[2][2] = { { xb.lx / 32768.0f, -xb.ly / 32768.0f }, { xb.rx / 32768.0f, (s_edit.invertRY ? 1.0f : -1.0f) * xb.ry / 32768.0f } };
        for (int i = 0; i < 2; i++) {
            const float sx = i == 0 ? c1 : c2;
            g_dl->AddCircleFilled(P(sx, cyc), r * g_k, Col(0x060C18), 40);
            g_dl->AddCircle(P(sx, cyc), r * g_k, Col(0x2F4A70), 40, 1.5f * g_k);
            if (dz > 0.0f) g_dl->AddCircleFilled(P(sx, cyc), r * dz * g_k, Col(0xF0605A, 60), 32);
            g_dl->AddLine(P(sx - r, cyc), P(sx + r, cyc), Col(0x22375A), 1.0f);
            g_dl->AddLine(P(sx, cyc - r), P(sx, cyc + r), Col(0x22375A), 1.0f);
            const float mag = sqrtf(sticks[i][0] * sticks[i][0] + sticks[i][1] * sticks[i][1]);
            const bool dead = mag < dz;
            g_dl->AddCircleFilled(P(sx + sticks[i][0] * r, cyc + sticks[i][1] * r), 6.0f * g_k, Col(live ? (dead ? 0xF0605A : kGold) : kTxt3), 16);
            Txt(g_fontGSmall, 13.0f, sx, cyc + r + 6.0f, kTxt3, i == 0 ? L4("Gauche", "Left", "Izquierdo", "Esquerdo") : L4("Droit", "Right", "Derecho", "Direito"), 1);
        }
        y += 172.0f;
        char v[48];
        _snprintf(v, sizeof(v) - 1, "%s  %d %%", L4("Zone morte", "Dead zone", "Zona muerta", "Zona morta"), s_edit.deadzone / 100); v[sizeof(v) - 1] = 0;
        Txt(g_fontGBody, 16.0f, cxr, y, kTxt, v);
        int dzv = s_edit.deadzone / 100;
        if (Slider("##cp_dz", cxr + 6.0f, y + 32.0f, cw - 12.0f, &dzv, 0, 40)) { s_edit.deadzone = dzv * 100; s_dirty = true; }
        Txt(g_fontGSmall, 13.0f, cxr, y + 44.0f, kTxt3, L4("Point rouge = dans la zone morte (stick ignor\xC3\xA9)", "Red dot = inside the dead zone (stick ignored)",
                                                           "Punto rojo = dentro de la zona muerta", "Ponto vermelho = dentro da zona morta"));
        y += 70.0f;
        Txt(g_fontGBody, 16.0f, cxr, y + 4.0f, kTxt, L4("Inverser le stick droit (haut / bas)", "Invert the right stick (up / down)", "Invertir el stick derecho", "Inverter o anal\xC3\xB3gico direito"));
        if (Toggle("##cp_inv", cxr + cw - 48.0f, y, s_edit.invertRY)) { s_edit.invertRY = !s_edit.invertRY; s_dirty = true; }
    }

    // save / revert
    const float by = ry + rh - 62.0f;
    const bool canSave = s_cfg.hasEntry && s_cfg.hasAxes;
    if (s_savedAt >= 0.0 && ImGui::GetTime() - s_savedAt < 6.0)
        Txt(g_fontGSmall, 14.0f, cxr, by - 22.0f, s_savedOk ? 0x9BE36A : 0xF0605A,
            s_savedOk ? L4("Enregistr\xC3\xA9 : clique sur Envoyer pour la console", "Saved: click Send for the console", "Guardado: pulsa Enviar", "Salvo: clique em Enviar")
                      : L4("\xC3\x89" "chec de l'enregistrement", "Save failed", "Error al guardar", "Falha ao salvar"));
    else if (s_dirty)
        Txt(g_fontGSmall, 14.0f, cxr, by - 22.0f, 0xF0B429, L4("Modifications non enregistr\xC3\xA9" "es", "Unsaved changes", "Cambios sin guardar", "Altera\xC3\xA7\xC3\xB5" "es n\xC3\xA3o salvas"));
    if (PillButton("##cp_undo", cxr, by, 120.0f, 44.0f, L4("Annuler", "Revert", "Deshacer", "Desfazer"), false)) { s_edit = s_cfg; s_dirty = false; s_capture = -1; }
    if (PillButton("##cp_save", cxr + 132.0f, by, cw - 132.0f, 44.0f, L4("Enregistrer", "Save", "Guardar", "Salvar"), canSave && s_dirty)) {
        if (!canSave) plat::Toast("%s", L4("Cette manette n'est pas encore dans X360Remap.json de la console", "This controller is not in the console's X360Remap.json yet",
                                           "Este mando a\xC3\xBAn no est\xC3\xA1 en X360Remap.json", "Este controle ainda n\xC3\xA3o est\xC3\xA1 no X360Remap.json"));
        else if (s_dirty) {
            s_savedOk = ApplyPadButtons(s_vid, s_pid, s_edit.btnIdx, s_cfg.macros, s_cfg.macroCount, path.c_str()) &&
                        ApplyPadStick(s_vid, s_pid, s_edit.invertRY, s_edit.deadzone, path.c_str());
            ModelOwnWriteDone();
            s_savedAt = ImGui::GetTime();
            if (s_savedOk) { s_dirty = false; s_stamp = ~0ull; }
        }
    }
}
