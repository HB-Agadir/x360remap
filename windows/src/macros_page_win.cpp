// Macros page of the Windows Studio (2026-11-17, HB): one controller button plays a
// combo once per press (CONTRACT.md "macros", sequence form; at most 16 per controller,
// 64 steps each, 16..5000 ms per step, 0 = while the button stays held). Same data as the
// console Studio's macro editor (PadMacro / PadStep, ApplyPadButtons), with what only the
// PC can offer: a timeline, recording the combo from the controller plugged into the PC,
// templates of classic fighting-game motions, and a playback before sending.
// No turbo / rapid fire on purpose (VISION.md 5f): a macro plays once per press.
#include "macros_page_win.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string>

#include "../../application/config_writer.h"
#include "../../application/ui_glossy.h"
#include "controller_page_win.h"
#include "model_win.h"
#include "pad_input_win.h"
#include "platform_win.h"
#include "ui_kit_win.h"
#include "win_i18n.h"

using namespace kit;

namespace {

// Cells (kPadFieldNames order) and how a step shows them.
const char* const kShort[PAD_CELLS] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L3", "R3", "ST", "BK", "G", "^", "v", "<", ">" };
const unsigned kCellColor[PAD_CELLS] = { 0x6FBF3A, 0xE5413B, 0x2F8DE8, 0xF0B429, 0x8FA0B8, 0x8FA0B8, 0x8FA0B8, 0x8FA0B8, 0x8FA0B8, 0x8FA0B8,
                                         0x8FA0B8, 0x8FA0B8, 0x2E7D32, 0xD6F6FF, 0xD6F6FF, 0xD6F6FF, 0xD6F6FF };
enum { C_A, C_B, C_X, C_Y, C_LB, C_RB, C_LT, C_RT, C_L3, C_R3, C_ST, C_BK, C_G, C_DU, C_DD, C_DL, C_DR };
unsigned Bit(int c) { return 1u << c; }

// Xbox view of the PC controller -> cells held.
unsigned CellsHeld(const OverlayLiveInput& x) {
    static const struct { uint16_t bit; int cell; } kMap[] = {
        { 0x1000, C_A }, { 0x2000, C_B }, { 0x4000, C_X }, { 0x8000, C_Y }, { 0x0100, C_LB }, { 0x0200, C_RB },
        { 0x0040, C_L3 }, { 0x0080, C_R3 }, { 0x0010, C_ST }, { 0x0020, C_BK }, { 0x0001, C_DU }, { 0x0002, C_DD },
        { 0x0004, C_DL }, { 0x0008, C_DR } };
    unsigned m = 0;
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) if (x.padButtons & kMap[i].bit) m |= Bit(kMap[i].cell);
    if (x.lt > 100) m |= Bit(C_LT);
    if (x.rt > 100) m |= Bit(C_RT);
    return m;
}

struct Template { const char* fr; const char* en; int n; PadStep steps[6]; };
const Template kTemplates[] = {
    { "Quart de cercle avant + X", "Quarter circle forward + X", 3,
      { { Bit(C_DD), 50 }, { Bit(C_DD) | Bit(C_DR), 33 }, { Bit(C_DR) | Bit(C_X), 80 } } },
    { "Dragon (avant, bas, bas-avant) + X", "Dragon punch (fwd, down, down-fwd) + X", 3,
      { { Bit(C_DR), 50 }, { Bit(C_DD), 33 }, { Bit(C_DD) | Bit(C_DR) | Bit(C_X), 80 } } },
    { "Quart de cercle arri\xC3\xA8re + Y", "Quarter circle back + Y", 3,
      { { Bit(C_DD), 50 }, { Bit(C_DD) | Bit(C_DL), 33 }, { Bit(C_DL) | Bit(C_Y), 80 } } },
    { "Double avant (dash)", "Double forward (dash)", 3, { { Bit(C_DR), 50 }, { 0, 50 }, { Bit(C_DR), 80 } } },
};

int MacroMs(const PadMacro& m) { int t = 0; for (int i = 0; i < m.stepCount; i++) t += m.steps[i].ms; return t; }

void StepChips(float x, float y, unsigned mask, float maxW) {
    float cx = x;
    for (int c = 0; c < PAD_CELLS && cx < x + maxW - 14.0f; c++) {
        if (!(mask & Bit(c))) continue;
        const float w = TxtW(g_fontGBodyM, 13.0f, kShort[c]) + 12.0f;
        g_dl->AddRectFilled(P(cx, y), P(cx + w, y + 20.0f), Col(kCellColor[c], 230), 10.0f * g_k);
        Txt(g_fontGBodyM, 13.0f, cx + w * 0.5f, y + 2.0f, c < 4 || c == C_G ? 0xFFFFFF : kInk, kShort[c], 1);
        cx += w + 4.0f;
    }
    if (!mask) Txt(g_fontGBody, 13.0f, x, y + 2.0f, kTxt3, L4("pause", "pause", "pausa", "pausa"));
}

}  // namespace

void DrawMacrosPageWin(float w, float h, int& tab) {
    (void)h;
    static uint16_t s_vid = 0, s_pid = 0;
    static PadConfig s_cfg;
    static PadMacro s_mac[PAD_MACROS_MAX_CFG];
    static int s_count = 0, s_sel = 0, s_step = 0;
    static uint64_t s_stamp = ~0ull;
    static bool s_dirty = false;
    static bool s_rec = false, s_capTrigger = false;
    static unsigned s_recMask = 0;
    static double s_recSince = 0, s_playStart = -1.0, s_savedAt = -1.0;
    static bool s_savedOk = false;
    static uint32_t s_prevButtons = 0;

    UiGlossyHeader(tab, w);
    Begin(w);
    Txt(g_fontGBody, 19.0f, 640.0f, 108.0f, kTxt2,
        L4("Un bouton de ta manette USB joue un combo, une fois par appui. Enregistre-le directement depuis ta manette.",
           "One button of your USB controller plays a combo, once per press. Record it straight from your controller.",
           "Un bot\xC3\xB3n de tu mando USB hace un combo, una vez por pulsaci\xC3\xB3n. Gr\xC3\xA1" "balo desde tu mando.",
           "Um bot\xC3\xA3o do seu controle USB faz um combo, uma vez por toque. Grave-o direto do seu controle."), 1);

    uint16_t vid = 0, pid = 0;
    std::string name;
    const float lx = 44.0f, ly = 146.0f;
    if (!ControllerPageCurrentPad(vid, pid, name)) {
        Card(lx, ly, 1192.0f, 524.0f);
        Txt(g_fontGTitle, 28.0f, 640.0f, ly + 190.0f, kGold, L4("Aucune manette USB", "No USB controller", "Ning\xC3\xBAn mando USB", "Nenhum controle USB"), 1);
        Txt(g_fontGBody, 18.0f, 640.0f, ly + 240.0f, kTxt2, L4("Les macros appartiennent \xC3\xA0 une manette : branche-la sur la console, puis Recevoir.",
            "Macros belong to a controller: plug it into the console, then Receive.", "Las macros pertenecen a un mando: con\xC3\xA9" "ctalo a la consola y Recibir.",
            "As macros pertencem a um controle: conecte-o ao console e Receber."), 1);
        DrawIcon(IC_MACRO, 640.0f, ly + 340.0f, 80.0f, kTxt3);
        return;
    }
    const std::string path = ModelConfigPath();
    const uint64_t stamp = plat::FileStamp(path);
    if (vid != s_vid || pid != s_pid || (stamp != s_stamp && !s_dirty)) {
        s_vid = vid; s_pid = pid; s_stamp = stamp;
        if (!ReadPadConfig(vid, pid, s_cfg, path.c_str())) memset(&s_cfg, 0, sizeof(s_cfg));
        s_count = s_cfg.macroCount;
        memcpy(s_mac, s_cfg.macros, sizeof(s_mac));
        s_sel = s_count ? 0 : -1; s_step = 0; s_dirty = false; s_rec = false;
    }
    const bool canSave = s_cfg.hasEntry && s_cfg.hasAxes;

    // PC controller: recording and the trigger button
    PcPadRaw raw;
    const bool live = ReadPcPad(raw) && raw.vid == vid && raw.pid == pid;
    OverlayLiveInput xb;
    if (live) PcPadToXbox(raw, xb);
    const unsigned held = live ? CellsHeld(xb) : 0;
    const uint32_t rising = (live ? raw.buttons : 0) & ~s_prevButtons;
    s_prevButtons = live ? raw.buttons : 0;
    const double now = ImGui::GetTime();

    // ------------------------------------------------------------- list (left)
    const float lw = 330.0f, lh = 524.0f;
    Card(lx, ly, lw, lh);
    {
        const float nw = Txt(g_fontGTitle, 26.0f, lx + 20.0f, ly + 14.0f, kGold, L4("Macros", "Macros", "Macros", "Macros"));
        char cnt[16];
        _snprintf(cnt, sizeof(cnt) - 1, "%d / %d", s_count, PAD_MACROS_MAX_CFG); cnt[sizeof(cnt) - 1] = 0;
        Txt(g_fontGSmall, 15.0f, lx + 28.0f + nw, ly + 24.0f, kTxt3, cnt);
        Txt(g_fontGBody, 14.0f, lx + 20.0f, ly + 50.0f, kTxt3, name.c_str());
    }
    float y = ly + 80.0f;
    for (int i = 0; i < s_count && y < ly + lh - 110.0f; i++) {
        char id[16], line[64], sub[64];
        _snprintf(id, sizeof(id) - 1, "##mc_row%d", i); id[sizeof(id) - 1] = 0;
        bool hot = false;
        if (Button(id, lx + 12.0f, y, lw - 24.0f, 50.0f, &hot)) { s_sel = i; s_step = 0; s_rec = false; }
        const bool on = i == s_sel;
        g_dl->AddRectFilled(P(lx + 12.0f, y), P(lx + lw - 12.0f, y + 50.0f), Col(on ? 0x1A2B47 : (hot ? 0x13213A : 0x0C1628)), 10.0f * g_k);
        if (on) g_dl->AddRectFilled(P(lx + 12.0f, y + 8.0f), P(lx + 16.0f, y + 42.0f), Col(kGold), 2.0f * g_k);
        _snprintf(line, sizeof(line) - 1, "%s B%d", L4("Bouton", "Button", "Bot\xC3\xB3n", "Bot\xC3\xA3o"), s_mac[i].btn + 1); line[sizeof(line) - 1] = 0;
        _snprintf(sub, sizeof(sub) - 1, "%d %s  -  %d ms", s_mac[i].stepCount, L4("\xC3\xA9tapes", "steps", "pasos", "passos"), MacroMs(s_mac[i])); sub[sizeof(sub) - 1] = 0;
        DrawIcon(IC_MACRO, lx + 38.0f, y + 25.0f, 18.0f, on ? kGold : kTxt2);
        Txt(g_fontGBodyM, 17.0f, lx + 60.0f, y + 6.0f, on ? kGold : kTxt, line);
        Txt(g_fontGBody, 14.0f, lx + 60.0f, y + 28.0f, kTxt3, sub);
        y += 56.0f;
    }
    if (!s_count) Txt(g_fontGBody, 15.0f, lx + 20.0f, y + 6.0f, kTxt3, L4("Aucune macro pour cette manette.", "No macro for this controller yet.", "Ninguna macro todav\xC3\xAD" "a.", "Nenhuma macro ainda."));
    if (s_count < PAD_MACROS_MAX_CFG &&
        PillButton("##mc_new", lx + 20.0f, ly + lh - 100.0f, lw - 40.0f, 36.0f, L4("+ Nouvelle macro", "+ New macro", "+ Nueva macro", "+ Nova macro"), s_count == 0)) {
        PadMacro& m = s_mac[s_count];
        memset(&m, 0, sizeof(m));
        m.btn = 15;   // a high button idx, usually free; the player changes it
        for (int b = 15; b >= 4; b--) {
            bool used = false;
            for (int c = 0; c < PAD_CELLS; c++) used = used || s_cfg.btnIdx[c] == b;
            for (int k = 0; k < s_count; k++) used = used || s_mac[k].btn == b;
            if (!used) { m.btn = b; break; }
        }
        m.stepCount = 1; m.steps[0].mask = Bit(C_A); m.steps[0].ms = 80;
        s_sel = s_count++; s_step = 0; s_dirty = true;
    }
    if (s_sel >= 0 && PillButton("##mc_del", lx + 20.0f, ly + lh - 56.0f, lw - 40.0f, 36.0f, L4("Supprimer la macro", "Delete macro", "Borrar macro", "Excluir macro"), false)) {
        for (int i = s_sel; i < s_count - 1; i++) s_mac[i] = s_mac[i + 1];
        s_count--; s_sel = s_count ? (s_sel > 0 ? s_sel - 1 : 0) : -1; s_step = 0; s_dirty = true;
    }

    // ------------------------------------------------------------- editor (right)
    const float rx = lx + lw + 16.0f, rw = 1236.0f - rx, ex = rx + 20.0f, ew = rw - 40.0f;
    Card(rx, ly, rw, lh);
    if (s_sel < 0) {
        Txt(g_fontGTitle, 26.0f, rx + rw * 0.5f, ly + 200.0f, kTxt2, L4("Cr\xC3\xA9" "e une macro \xC3\xA0 gauche", "Create a macro on the left", "Crea una macro a la izquierda", "Crie uma macro \xC3\xA0 esquerda"), 1);
    } else {
        PadMacro& m = s_mac[s_sel];
        // trigger button
        char trig[64];
        _snprintf(trig, sizeof(trig) - 1, "%s B%d", L4("D\xC3\xA9" "clench\xC3\xA9" "e par le bouton", "Played by button", "La activa el bot\xC3\xB3n", "Ativada pelo bot\xC3\xA3o"), m.btn + 1); trig[sizeof(trig) - 1] = 0;
        Txt(g_fontGTitle, 24.0f, ex, ly + 16.0f, kGold, trig);
        if (s_capTrigger && rising) {
            int n = 0;
            while (!(rising & (1u << n))) n++;
            m.btn = n; s_capTrigger = false; s_dirty = true;
        }
        const char* chg = s_capTrigger ? L4("Appuie sur un bouton...", "Press a button...", "Pulsa un bot\xC3\xB3n...", "Aperte um bot\xC3\xA3o...")
                                       : L4("Changer", "Change", "Cambiar", "Mudar");
        if (PillButton("##mc_trig", ex + ew - 300.0f, ly + 16.0f, 150.0f, 30.0f, chg, false)) s_capTrigger = live ? !s_capTrigger : false;
        if (PillButton("##mc_m", ex + ew - 140.0f, ly + 16.0f, 30.0f, 30.0f, "-", false) && m.btn > 0) { m.btn--; s_dirty = true; }
        if (PillButton("##mc_p", ex + ew - 100.0f, ly + 16.0f, 30.0f, 30.0f, "+", false) && m.btn < 31) { m.btn++; s_dirty = true; }
        for (int c = 0; c < PAD_CELLS; c++)
            if (s_cfg.btnIdx[c] == m.btn) {
                Txt(g_fontGSmall, 13.0f, ex, ly + 50.0f, 0xF0B429, L4("Ce bouton fait d\xC3\xA9j\xC3\xA0 autre chose : la macro le remplacera.",
                    "This button already does something else: the macro will replace it.", "Este bot\xC3\xB3n ya hace otra cosa: la macro lo sustituir\xC3\xA1.",
                    "Este bot\xC3\xA3o j\xC3\xA1 faz outra coisa: a macro o substituir\xC3\xA1."));
                break;
            }

        // timeline
        const float tx = ex, ty = ly + 78.0f, tw = ew, th = 70.0f;
        g_dl->AddRectFilled(P(tx, ty), P(tx + tw, ty + th), Col(0x060C18), 10.0f * g_k);
        int total = 0;
        for (int i = 0; i < m.stepCount; i++) total += m.steps[i].ms > 0 ? m.steps[i].ms : 200;
        const float scale = total > 0 ? (tw - 8.0f) / (float)total : 1.0f;
        const int playMs = s_playStart >= 0.0 ? (int)((now - s_playStart) * 1000.0) : -1;
        if (playMs > total) s_playStart = -1.0;
        float bx = tx + 4.0f;
        int acc = 0;
        for (int i = 0; i < m.stepCount; i++) {
            const int ms = m.steps[i].ms > 0 ? m.steps[i].ms : 200;
            const float bw = ms * scale;
            char id[16];
            _snprintf(id, sizeof(id) - 1, "##mc_st%d", i); id[sizeof(id) - 1] = 0;
            bool hot = false;
            if (Button(id, bx, ty + 4.0f, bw - 3.0f, th - 8.0f, &hot)) s_step = i;
            const bool playing = playMs >= acc && playMs < acc + ms;
            g_dl->AddRectFilled(P(bx, ty + 4.0f), P(bx + bw - 3.0f, ty + th - 4.0f), Col(playing ? 0x3A2F0C : (i == s_step ? 0x1A2B47 : (hot ? 0x13213A : 0x0E1A2E))), 8.0f * g_k);
            if (i == s_step) g_dl->AddRect(P(bx, ty + 4.0f), P(bx + bw - 3.0f, ty + th - 4.0f), Col(kGold), 8.0f * g_k, ImDrawCornerFlags_All, 1.5f * g_k);
            StepChips(bx + 6.0f, ty + 12.0f, m.steps[i].mask, bw - 10.0f);
            char d[16];
            if (m.steps[i].ms > 0) _snprintf(d, sizeof(d) - 1, "%d ms", m.steps[i].ms); else _snprintf(d, sizeof(d) - 1, "%s", L4("tenu", "held", "mantener", "segurar"));
            d[sizeof(d) - 1] = 0;
            if (bw > 44.0f) Txt(g_fontGSmall, 13.0f, bx + 6.0f, ty + th - 24.0f, kTxt3, d);
            bx += bw; acc += ms;
        }
        if (playMs >= 0) {
            const float px = tx + 4.0f + playMs * scale;
            g_dl->AddLine(P(px, ty), P(px, ty + th), Col(kGold), 2.0f * g_k);
        }

        // actions on the timeline
        float ax = ex, ay = ty + th + 12.0f;
        if (PillButton("##mc_play", ax, ay, 110.0f, 34.0f, L4("Jouer", "Play", "Reproducir", "Tocar"), false)) s_playStart = now;
        ax += 118.0f;
        const char* recLabel = s_rec ? L4("Stop", "Stop", "Parar", "Parar") : L4("Enregistrer", "Record", "Grabar", "Gravar");
        if (PillButton("##mc_rec", ax, ay, 140.0f, 34.0f, recLabel, s_rec)) {
            if (!live) plat::Toast("%s", L4("Branche cette manette sur ce PC pour enregistrer", "Plug this controller into this PC to record",
                                          "Conecta este mando a este PC para grabar", "Conecte este controle a este PC para gravar"));
            else if (!s_rec) { s_rec = true; m.stepCount = 0; s_recMask = held; s_recSince = now; s_step = 0; }
            else s_rec = false;
        }
        DrawIcon(IC_RECORD, ax + 18.0f, ay + 17.0f, 12.0f, s_rec ? (fmod(now, 1.0) < 0.5 ? 0xFF3030 : 0x801818) : 0xF0605A);
        ax += 148.0f;
        if (PillButton("##mc_add", ax, ay, 150.0f, 34.0f, L4("+ \xC3\x89tape", "+ Step", "+ Paso", "+ Passo"), false) && m.stepCount < PAD_STEPS_MAX_CFG) {
            for (int i = m.stepCount; i > s_step + 1; i--) m.steps[i] = m.steps[i - 1];
            const int at = m.stepCount ? s_step + 1 : 0;
            m.steps[at].mask = 0; m.steps[at].ms = 50;
            m.stepCount++; s_step = at; s_dirty = true;
        }
        ax += 158.0f;
        if (m.stepCount > 1 && PillButton("##mc_rm", ax, ay, 150.0f, 34.0f, L4("- \xC3\x89tape", "- Step", "- Paso", "- Passo"), false)) {
            for (int i = s_step; i < m.stepCount - 1; i++) m.steps[i] = m.steps[i + 1];
            m.stepCount--; if (s_step >= m.stepCount) s_step = m.stepCount - 1; s_dirty = true;
        }

        // recording: a new step each time the buttons held change, closed after 1.5 s idle
        if (s_rec) {
            if (held != s_recMask || now - s_recSince > 1.5) {
                const int ms = (int)((now - s_recSince) * 1000.0);
                if (s_recMask && m.stepCount < PAD_STEPS_MAX_CFG) {
                    m.steps[m.stepCount].mask = s_recMask;
                    m.steps[m.stepCount].ms = ms < 16 ? 16 : (ms > 5000 ? 5000 : ms);
                    m.stepCount++; s_dirty = true;
                } else if (!s_recMask && m.stepCount > 0 && held && ms >= 16 && m.stepCount < PAD_STEPS_MAX_CFG) {
                    m.steps[m.stepCount].mask = 0; m.steps[m.stepCount].ms = ms > 5000 ? 5000 : ms;   // a pause between two presses
                    m.stepCount++;
                }
                if (!held && m.stepCount > 0 && now - s_recSince > 1.5) s_rec = false;
                s_recMask = held; s_recSince = now;
            }
            Txt(g_fontGBodyM, 15.0f, ex + ew, ay + 8.0f, 0xFF7070, L4("Fais ton combo sur la manette...", "Do your combo on the controller...", "Haz tu combo en el mando...", "Fa\xC3\xA7" "a seu combo no controle..."), 2);
        }
        if (m.stepCount == 0 && !s_rec) { m.stepCount = 1; m.steps[0].mask = Bit(C_A); m.steps[0].ms = 80; }
        if (s_step >= m.stepCount) s_step = m.stepCount - 1;

        // selected step: buttons held and duration
        if (m.stepCount > 0 && !s_rec) {
            PadStep& st = m.steps[s_step];
            float sy = ay + 52.0f;
            char hdr[48];
            _snprintf(hdr, sizeof(hdr) - 1, "%s %d", L4("\xC3\x89TAPE", "STEP", "PASO", "PASSO"), s_step + 1); hdr[sizeof(hdr) - 1] = 0;
            Section(ex, sy, hdr);
            sy += 22.0f;
            const float cwid = (ew - 16.0f * 3.0f) / 17.0f;
            for (int c = 0; c < PAD_CELLS; c++) {
                const float cx = ex + c * (cwid + 3.0f);
                char id[16];
                _snprintf(id, sizeof(id) - 1, "##mc_cell%d", c); id[sizeof(id) - 1] = 0;
                bool hot = false;
                if (Button(id, cx, sy, cwid, 34.0f, &hot)) { st.mask ^= Bit(c); s_dirty = true; }
                const bool on = (st.mask & Bit(c)) != 0;
                g_dl->AddRectFilled(P(cx, sy), P(cx + cwid, sy + 34.0f), Col(on ? kCellColor[c] : (hot ? 0x1A2B47 : 0x0E1A2E)), 8.0f * g_k);
                Txt(g_fontGBodyM, 14.0f, cx + cwid * 0.5f, sy + 9.0f, on ? (c < 4 || c == C_G ? 0xFFFFFF : kInk) : kTxt2, kShort[c], 1);
            }
            sy += 50.0f;
            char d[48];
            if (st.ms > 0) _snprintf(d, sizeof(d) - 1, "%s  %d ms", L4("Dur\xC3\xA9" "e", "Duration", "Duraci\xC3\xB3n", "Dura\xC3\xA7\xC3\xA3o"), st.ms);
            else _snprintf(d, sizeof(d) - 1, "%s", L4("Tant que le bouton reste appuy\xC3\xA9", "As long as the button stays held", "Mientras el bot\xC3\xB3n siga pulsado", "Enquanto o bot\xC3\xA3o ficar apertado"));
            d[sizeof(d) - 1] = 0;
            Txt(g_fontGBody, 16.0f, ex, sy, kTxt, d);
            int ms = st.ms;
            if (st.ms > 0 && Slider("##mc_ms", ex + 6.0f, sy + 34.0f, ew * 0.6f, &ms, 16, 1000)) { st.ms = ms; s_dirty = true; }
            Txt(g_fontGBody, 15.0f, ex + ew - 60.0f, sy + 2.0f, kTxt2, L4("Tenu", "Held", "Mantener", "Segurar"), 2);
            if (Toggle("##mc_hold", ex + ew - 48.0f, sy - 2.0f, st.ms == 0)) { st.ms = st.ms == 0 ? 80 : 0; s_dirty = true; }
            sy += 60.0f;

            Section(ex, sy, L4("MOD\xC3\x88LES", "TEMPLATES", "PLANTILLAS", "MODELOS"));
            sy += 22.0f;
            for (int i = 0; i < (int)(sizeof(kTemplates) / sizeof(kTemplates[0])); i++) {
                const float bw = (ew - 8.0f) * 0.5f, bxx = ex + (i % 2) * (bw + 8.0f), byy = sy + (i / 2) * 40.0f;
                char id[16];
                _snprintf(id, sizeof(id) - 1, "##mc_tpl%d", i); id[sizeof(id) - 1] = 0;
                if (PillButton(id, bxx, byy, bw, 34.0f, GetLanguage() == LANG_FR ? kTemplates[i].fr : kTemplates[i].en, false)) {
                    m.stepCount = kTemplates[i].n;
                    for (int k = 0; k < kTemplates[i].n; k++) m.steps[k] = kTemplates[i].steps[k];
                    s_step = 0; s_dirty = true;
                }
            }
        }
    }

    // save
    const float by = ly + lh - 56.0f, bw2 = 200.0f;
    if (s_savedAt >= 0.0 && now - s_savedAt < 6.0)
        Txt(g_fontGSmall, 14.0f, 1216.0f - bw2 - 12.0f, by + 12.0f, s_savedOk ? 0x9BE36A : 0xF0605A,
            s_savedOk ? L4("Enregistr\xC3\xA9 : Envoyer pour la console", "Saved: Send for the console", "Guardado: Enviar", "Salvo: Enviar")
                      : L4("\xC3\x89" "chec de l'enregistrement", "Save failed", "Error al guardar", "Falha ao salvar"), 2);
    else if (!canSave)
        Txt(g_fontGSmall, 14.0f, 1216.0f - bw2 - 12.0f, by + 12.0f, 0xF0B429, L4("Manette absente du X360Remap.json de la console", "Controller not in the console's X360Remap.json",
            "Mando ausente de X360Remap.json", "Controle ausente do X360Remap.json"), 2);
    if (PillButton("##mc_save", 1216.0f - bw2, by, bw2, 40.0f, L4("Enregistrer", "Save", "Guardar", "Salvar"), canSave && s_dirty) && canSave && s_dirty) {
        s_savedOk = ApplyPadButtons(s_vid, s_pid, s_cfg.btnIdx, s_mac, s_count, path.c_str());
        ModelOwnWriteDone();
        s_savedAt = now;
        if (s_savedOk) { s_dirty = false; s_stamp = ~0ull; }
    }
}
