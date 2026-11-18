// In-game display page of the Windows Studio (2026-11-17, HB: "the PC has none of the
// console's limits, it must be more attractive and modern than the console page").
// Same settings file and the same drawing code as the plugin (overlay_preview.cpp), but:
//  - a large preview that is LIVE: keys, mouse buttons and an XInput controller on this
//    PC light the drawing exactly as the plugin draws it in the game;
//  - an optional picture of the player's game behind it (any PNG / JPG);
//  - the keyboard or controller drawing is dragged directly on the preview;
//  - real colour pickers instead of cycling a palette.
// The console Studio keeps its own page (application/ui_glossy.cpp).
#include "overlay_page_win.h"

#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "../../application/third_party/imgui/imgui.h"
#include "../../application/theme.h"
#include "../../application/ui_glossy.h"
#include "../../application/overlay_preview.h"
#include "../../stb/stb_image.h"
#include "key_capture_win.h"
#include "model_win.h"
#include "platform_win.h"
#include "win_i18n.h"
#include "ui_kit_win.h"

#include "pad_input_win.h"

#pragma comment(lib, "xinput9_1_0.lib")



namespace {

using namespace kit;

uint32_t PackTexel(unsigned r, unsigned g, unsigned b, unsigned a) { return r | (g << 8) | (b << 16) | (a << 24); }

// --- Live input on this PC ---------------------------------------------------------
// Only the keys of the preview's sample layout (the plugin's default keys) matter.
const struct { uint8_t hid; int vk; } kLiveKeys[] = {
    { 0x1A, 'W' }, { 0x04, 'A' }, { 0x16, 'S' }, { 0x07, 'D' }, { 0x2C, VK_SPACE }, { 0x06, 'C' },
    { 0x15, 'R' }, { 0x08, 'E' }, { 0x14, 'Q' }, { 0x09, 'F' },
};
bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

std::string g_padName;

void ReadLive(OverlayLiveInput& in, bool mouseOverPreview, bool& padSeen) {
    in = OverlayLiveInput();
    in.valid = true;
    padSeen = false;
    for (DWORD i = 0; i < 4; i++) {
        XINPUT_STATE st;
        if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;
        padSeen = true;
        in.padButtons = st.Gamepad.wButtons;
        in.lt = st.Gamepad.bLeftTrigger; in.rt = st.Gamepad.bRightTrigger;
        in.lx = st.Gamepad.sThumbLX; in.ly = st.Gamepad.sThumbLY;
        in.rx = st.Gamepad.sThumbRX; in.ry = st.Gamepad.sThumbRY;
        g_padName = "Xbox (XInput)";
        break;
    }
    if (!padSeen) {
        PcPadRaw raw;
        if (ReadPcPad(raw) && raw.entry) { PcPadToXbox(raw, in); padSeen = true; g_padName = raw.name; }
    }
    if (GetActiveWindow() == nullptr) return;   // another program has the keyboard and mouse
    int n = 0;
    for (size_t i = 0; i < sizeof(kLiveKeys) / sizeof(kLiveKeys[0]) && n < 6; i++)
        if (Down(kLiveKeys[i].vk)) in.keys[n++] = kLiveKeys[i].hid;
    if (Down(VK_LCONTROL)) in.mods |= 0x01;
    if (Down(VK_LSHIFT))   in.mods |= 0x02;
    if (Down(VK_LMENU))    in.mods |= 0x04;
    if (mouseOverPreview) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseDown[0]) in.mouse |= 0x01;
        if (io.MouseDown[1]) in.mouse |= 0x02;
        if (io.MouseDown[2]) in.mouse |= 0x04;
        if (Down(VK_XBUTTON1)) in.mouse |= 0x08;
        if (Down(VK_XBUTTON2)) in.mouse |= 0x10;
    }
}

bool SameLive(const OverlayLiveInput& a, const OverlayLiveInput& b) { return memcmp(&a, &b, sizeof(a)) == 0; }

// --- Background picture -------------------------------------------------------------
void* g_bgTex = nullptr;
int   g_bgW = 0, g_bgH = 0;
bool  g_bgTried = false;

std::string BgSettingPath() { return plat::DataDir() + "overlay_background.txt"; }

bool LoadBackground(const std::string& path) {
    std::string bytes;
    if (path.empty() || !plat::ReadWholeFile(path, bytes) || bytes.empty()) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!px) return false;
    ModelReleaseTexture(g_bgTex);
    g_bgTex = ModelCreateTexture(px, w, h);
    g_bgW = w; g_bgH = h;
    stbi_image_free(px);
    return g_bgTex != nullptr;
}

// The default scene when no picture is chosen: dusk sky, horizon, a few buildings.
void DrawScene(float x, float y, float w, float h) {
    g_dl->AddRectFilledMultiColor(P(x, y), P(x + w, y + h * 0.62f), Col(0x16243B), Col(0x16243B), Col(0x3D5470), Col(0x3D5470));
    g_dl->AddRectFilledMultiColor(P(x, y + h * 0.62f), P(x + w, y + h), Col(0x23262A), Col(0x23262A), Col(0x101215), Col(0x101215));
    static const float bx[7] = { 0.02f, 0.10f, 0.21f, 0.36f, 0.55f, 0.70f, 0.84f }, bw[7] = { 0.07f, 0.09f, 0.12f, 0.10f, 0.11f, 0.12f, 0.14f },
                       bh[7] = { 0.18f, 0.30f, 0.22f, 0.34f, 0.26f, 0.38f, 0.24f };
    for (int i = 0; i < 7; i++)
        g_dl->AddRectFilled(P(x + w * bx[i], y + h * (0.62f - bh[i])), P(x + w * (bx[i] + bw[i]), y + h * 0.62f), Col(0x0C1220));
    // crosshair, as in a shooter
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    g_dl->AddLine(P(cx - 9.0f, cy), P(cx - 3.0f, cy), Col(0xFFFFFF, 160), 1.5f * g_k);
    g_dl->AddLine(P(cx + 3.0f, cy), P(cx + 9.0f, cy), Col(0xFFFFFF, 160), 1.5f * g_k);
    g_dl->AddLine(P(cx, cy - 9.0f), P(cx, cy - 3.0f), Col(0xFFFFFF, 160), 1.5f * g_k);
    g_dl->AddLine(P(cx, cy + 3.0f), P(cx, cy + 9.0f), Col(0xFFFFFF, 160), 1.5f * g_k);
}

// Picture shown like a game frame: cropped to 16:9, centred.
void DrawPicture(float x, float y, float w, float h) {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    const float aspect = (float)g_bgW / (float)(g_bgH ? g_bgH : 1), want = w / h;
    if (aspect > want) { const float c = (1.0f - want / aspect) * 0.5f; u0 = c; u1 = 1.0f - c; }
    else               { const float c = (1.0f - aspect / want) * 0.5f; v0 = c; v1 = 1.0f - c; }
    g_dl->AddImage((ImTextureID)g_bgTex, P(x, y), P(x + w, y + h), ImVec2(u0, v0), ImVec2(u1, v1));
}

// --- Colour picker popup --------------------------------------------------------------
bool ColourSwatch(const char* id, float x, float y, unsigned rgb, const char* label) {
    bool hot = false;
    const bool clicked = Button(id, x, y, 136.0f, 34.0f, &hot);
    g_dl->AddRectFilled(P(x, y), P(x + 136.0f, y + 34.0f), Col(hot ? 0x13213A : 0x0A1324), 8.0f * g_k);
    g_dl->AddRect(P(x, y), P(x + 136.0f, y + 34.0f), Col(hot ? 0x3D5C8A : 0x22375A), 8.0f * g_k, ImDrawCornerFlags_All, 1.0f);
    g_dl->AddRectFilled(P(x + 6.0f, y + 6.0f), P(x + 28.0f, y + 28.0f), Col(rgb), 5.0f * g_k);
    Txt(g_fontGBody, 16.0f, x + 36.0f, y + 8.0f, kTxt2, label);
    return clicked;
}

}  // namespace

void DrawOverlayPageWin(float w, float h, int& tab) {
    static OverlaySettings s_set, s_shown;
    static OverlayLiveInput s_liveShown;
    static uint64_t s_stamp = 0;
    static bool s_loaded = false, s_capturing = false;
    static void* s_tex = nullptr;
    static int s_prevKey = -1, s_blockX = 0, s_blockY = 0, s_blockW = 0, s_blockH = 0;
    static double s_savedAt = -1.0;
    static bool s_savedOk = false;
    static int s_picking = -1;            // colour being edited: 0 pressed, 1 resting
    static float s_pick[3];
    (void)h;

    // Follows the file: first visit, a pull from the console, a restore.
    const std::string path = plat::WorkFile("overlay_settings.txt");
    const uint64_t stamp = plat::FileStamp(path);
    if (!s_loaded || stamp != s_stamp) {
        s_loaded = true;
        s_stamp = stamp;
        OverlayLoad(path.c_str(), s_set);   // no file yet = the plugin's defaults
    }
    if (!g_bgTried) {
        g_bgTried = true;
        std::string saved;
        if (plat::ReadWholeFile(BgSettingPath(), saved)) {
            while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r')) saved.pop_back();
            LoadBackground(saved);
        }
    }

    UiGlossyHeader(tab, w);
    kit::Begin(w);
    OverlaySettings& s = s_set;

    Txt(g_fontGBody, 19.0f, 640.0f, 108.0f, kTxt2,
        L4("Ce qui s'affiche par-dessus le jeu. Appuie sur des touches ou une manette : l'aper\xC3\xA7u r\xC3\xA9" "agit en direct.",
           "What is drawn over the game. Press keys or a controller: the preview reacts live.",
           "Lo que se dibuja sobre el juego. Pulsa teclas o un mando: la vista previa reacciona en directo.",
           "O que aparece sobre o jogo. Aperte teclas ou um controle: a pr\xC3\xA9via reage ao vivo."), 1);

    // ---------------------------------------------------------------- preview (left)
    const float lx = 44.0f, ly = 146.0f, lw = 840.0f, lh = 524.0f;
    Card(lx, ly, lw, lh);
    Txt(g_fontGTitle, 28.0f, lx + 20.0f, ly + 12.0f, kGold, L4("Aper\xC3\xA7u", "Preview", "Vista previa", "Pr\xC3\xA9via"));
    // background chooser, right of the title
    {
        const char* labels[2] = { L4("Sc\xC3\xA8ne", "Scene", "Escena", "Cena"), L4("Mon image...", "My picture...", "Mi imagen...", "Minha imagem...") };
        const int cur = g_bgTex ? 1 : 0;
        const int hit = Segmented("##ovw_bg", lx + lw - 20.0f - 300.0f, ly + 16.0f, 300.0f, 32.0f, labels, 2, cur);
        if (hit == 0 && g_bgTex) { ModelReleaseTexture(g_bgTex); g_bgTex = nullptr; plat::WriteWholeFile(BgSettingPath(), ""); }
        if (hit == 1) {
            std::string pic;
            if (plat::OpenFileDialog(GetActiveWindow(), L"Image (PNG, JPG)\0*.png;*.jpg;*.jpeg;*.bmp\0\0", pic)) {
                if (LoadBackground(pic)) plat::WriteWholeFile(BgSettingPath(), pic + "\n");
                else plat::Toast("%s", L4("Image illisible", "Picture not readable", "Imagen ilegible", "Imagem ileg\xC3\xADvel"));
            }
        }
        Txt(g_fontGSmall, 15.0f, lx + lw - 330.0f, ly + 24.0f, kTxt3, L4("Fond", "Background", "Fondo", "Fundo"), 2);
    }

    const float px = lx + 20.0f, py = ly + 60.0f, pw = 800.0f, ph = 450.0f, pk = pw / 1280.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool overPreview = mouse.x >= px * g_k && mouse.x < (px + pw) * g_k && mouse.y >= py * g_k && mouse.y < (py + ph) * g_k;
    OverlayLiveInput live;
    bool padSeen = false;
    ReadLive(live, overPreview, padSeen);
    if (!s_tex || !OverlaySameLook(s_shown, s_set) || !SameLive(live, s_liveShown)) {
        static std::vector<unsigned char> s_pixels;
        s_pixels.resize((size_t)OVPV_W * OVPV_H * 4);
        OverlayPreviewRenderLive(s_set, &live, &s_pixels[0], OVPV_W * 4, PackTexel, &s_blockX, &s_blockY, &s_blockW, &s_blockH);
        ModelReleaseTexture(s_tex);
        s_tex = ModelCreateTexture(&s_pixels[0], OVPV_W, OVPV_H);
        s_shown = s_set;
        s_liveShown = live;
    }
    g_dl->PushClipRect(P(px, py), P(px + pw, py + ph), true);
    if (g_bgTex) DrawPicture(px, py, pw, ph); else DrawScene(px, py, pw, ph);
    if (s_tex) g_dl->AddImage((ImTextureID)s_tex, P(px, py), P(px + pw, py + ph));
    g_dl->PopClipRect();
    g_dl->AddRect(P(px, py), P(px + pw, py + ph), Col(0x2A4266), 4.0f * g_k, ImDrawCornerFlags_All, 1.0f);

    // live badge, bottom left of the picture
    {
        const bool inputOn = s.showKeys || s.showPad;
        const std::string padBadge = std::string(L4("EN DIRECT : ", "LIVE: ", "EN DIRECTO: ", "AO VIVO: ")) + g_padName;
        const char* badge = !inputOn
            ? L4("Aide aux touches : aucune", "Input help: none", "Ayuda de teclas: ninguna", "Ajuda de teclas: nenhuma")
            : s.showPad ? (padSeen ? padBadge.c_str()
                                   : L4("Branche ta manette au PC pour la voir r\xC3\xA9" "agir", "Plug your controller into the PC to see it react",
                                        "Conecta tu mando al PC para verlo reaccionar", "Conecte seu controle ao PC para v\xC3\xAA-lo reagir"))
                        : L4("EN DIRECT : W A S D, Espace, Maj, Ctrl, clics", "LIVE: W A S D, Space, Shift, Ctrl, clicks",
                             "EN DIRECTO: W A S D, Espacio, May\xC3\xBA" "s, Ctrl, clics", "AO VIVO: W A S D, Espa\xC3\xA7o, Shift, Ctrl, cliques");
        const float bw = TxtW(g_fontGBodyM, 15.0f, badge) + 34.0f;
        g_dl->AddRectFilled(P(px + 10.0f, py + 10.0f), P(px + 10.0f + bw, py + 36.0f), Col(0x05090F, 190), 13.0f * g_k);
        const bool liveOk = inputOn && (s.showKeys || padSeen);
        g_dl->AddCircleFilled(P(px + 23.0f, py + 23.0f), 4.5f * g_k, Col(liveOk ? 0x5BE06A : 0x7F90A4), 12);
        Txt(g_fontGBodyM, 15.0f, px + 33.0f, py + 14.0f, liveOk ? 0xDFF7E2 : kTxt2, badge);
    }

    // drag the keyboard or controller drawing
    if ((s.showKeys || s.showPad) && s_blockW > 0) {
        static float s_grabX = 0, s_grabY = 0;
        static unsigned s_startX = 0, s_startY = 0;
        const float bx = px + s_blockX * pk - 4.0f, by = py + s_blockY * pk - 4.0f, bw = s_blockW * pk + 8.0f, bh = s_blockH * pk + 8.0f;
        bool hot = false;
        Button("##ovw_drag", bx, by, bw, bh, &hot);
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemActivated()) { s_grabX = mouse.x; s_grabY = mouse.y; s_startX = s.keysX; s_startY = s.keysY; }
        if (active) {
            const float dx = (mouse.x - s_grabX) / g_k / pk, dy = (mouse.y - s_grabY) / g_k / pk;
            const float maxX = s.showPad ? (float)(1280 - 157)   /* HUD_PAD_W, X360Remap/hud_art.h */ : 790.0f, maxY = 590.0f;
            float nx = s_startX + dx, ny = s_startY + dy;
            nx = nx < 0 ? 0 : (nx > maxX ? maxX : nx);
            ny = ny < 0 ? 0 : (ny > maxY ? maxY : ny);
            s.keysX = (unsigned)nx; s.keysY = (unsigned)ny;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (hot) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (hot || active) {
            g_dl->AddRect(P(bx, by), P(bx + bw, by + bh), Col(kGold, active ? 255 : 170), 6.0f * g_k, ImDrawCornerFlags_All, 1.5f * g_k);
            char pos[48];
            _snprintf(pos, sizeof(pos) - 1, "X %u   Y %u", s.keysX, s.keysY); pos[sizeof(pos) - 1] = 0;
            const float tw = TxtW(g_fontGBodyM, 14.0f, pos) + 16.0f;
            const float ty = by > py + 26.0f ? by - 24.0f : by + bh + 4.0f;
            g_dl->AddRectFilled(P(bx, ty), P(bx + tw, ty + 20.0f), Col(kGold), 10.0f * g_k);
            Txt(g_fontGBodyM, 14.0f, bx + 8.0f, ty + 2.0f, 0x15110A, pos);
        }
    }

    // ---------------------------------------------------------------- settings (right)
    const float rx = 900.0f, ry = 146.0f, rw = 336.0f, rh = 524.0f, cx = rx + 20.0f, cw = rw - 40.0f;
    Card(rx, ry, rw, rh);
    Txt(g_fontGTitle, 28.0f, cx, ry + 12.0f, kGold, L4("R\xC3\xA9glages", "Settings", "Ajustes", "Ajustes"));
    float y = ry + 64.0f;

    Section(cx, y, L4("INFOS CONSOLE", "CONSOLE INFO", "INFO DE CONSOLA", "INFO DO CONSOLE"));
    Txt(g_fontGBody, 16.0f, cx, y + 24.0f, kTxt, L4("FPS, CPU, GPU en haut \xC3\xA0 droite", "FPS, CPU, GPU at the top right",
                                                  "FPS, CPU, GPU arriba a la derecha", "FPS, CPU, GPU no canto superior direito"));
    if (Toggle("##ovw_stats", cx + cw - 48.0f, y + 20.0f, s.showStats)) s.showStats = !s.showStats;
    Txt(g_fontGSmall, 14.0f, cx, y + 46.0f, kTxt3, L4("En jeu : touche du raccourci + I", "In game: shortcut key + I", "En el juego: atajo + I", "No jogo: atalho + I"));
    y += 78.0f;

    Section(cx, y, L4("AIDE AUX TOUCHES", "INPUT HELP", "AYUDA DE TECLAS", "AJUDA DE TECLAS"));
    {
        const char* labels[3] = { L4("Aucune", "None", "Ninguna", "Nenhuma"), L4("Clavier", "Keyboard", "Teclado", "Teclado"),
                                  L4("Manette", "Controller", "Mando", "Controle") };
        const int cur = s.showKeys ? 1 : (s.showPad ? 2 : 0);
        const int hit = Segmented("##ovw_help", cx, y + 22.0f, cw, 34.0f, labels, 3, cur);
        if (hit == 0) s.showKeys = s.showPad = false;
        if (hit == 1) { s.showKeys = true; s.showPad = false; }
        if (hit == 2) { s.showPad = true; s.showKeys = false; }
    }
    y += 72.0f;

    Section(cx, y, L4("COULEURS", "COLOURS", "COLORES", "CORES"));
    {
        // theme presets: two-tone dots
        for (int i = 0; i < 5; i++) {
            const float dx = cx + cw - 5 * 28.0f + i * 28.0f + 12.0f, dy = y + 7.0f;
            char id[24];
            _snprintf(id, sizeof(id) - 1, "##ovw_th%d", i); id[sizeof(id) - 1] = 0;
            bool hot = false;
            if (Button(id, dx - 11.0f, dy - 11.0f, 22.0f, 22.0f, &hot)) { s.colorOn = kOverlayThemes[i][0]; s.colorOff = kOverlayThemes[i][1]; }
            const bool cur = s.colorOn == kOverlayThemes[i][0] && s.colorOff == kOverlayThemes[i][1];
            g_dl->PathArcTo(P(dx, dy), 9.0f * g_k, 1.5708f, 4.7124f, 12);
            g_dl->PathFillConvex(Col(kOverlayThemes[i][0]));
            g_dl->PathArcTo(P(dx, dy), 9.0f * g_k, -1.5708f, 1.5708f, 12);
            g_dl->PathFillConvex(Col(kOverlayThemes[i][1]));
            if (cur || hot) g_dl->AddCircle(P(dx, dy), 12.0f * g_k, Col(cur ? kGold : 0x7F90A4), 20, 1.5f * g_k);
        }
        if (ColourSwatch("##ovw_c0", cx, y + 24.0f, s.colorOn, L4("Appuy\xC3\xA9" "e", "Pressed", "Pulsada", "Pressionada"))) s_picking = 0;
        if (ColourSwatch("##ovw_c1", cx + cw - 136.0f, y + 24.0f, s.colorOff, L4("Au repos", "Resting", "En reposo", "Em repouso"))) s_picking = 1;
        if (s_picking >= 0 && !ImGui::IsPopupOpen("##ovw_picker")) {
            const unsigned c = s_picking == 0 ? s.colorOn : s.colorOff;
            s_pick[0] = ((c >> 16) & 255) / 255.0f; s_pick[1] = ((c >> 8) & 255) / 255.0f; s_pick[2] = (c & 255) / 255.0f;
            ImGui::SetNextWindowPos(P(cx - 40.0f, y + 64.0f));
            ImGui::OpenPopup("##ovw_picker");
        }
        if (ImGui::BeginPopup("##ovw_picker")) {
            ImGui::PushItemWidth(240.0f * g_k);
            if (ImGui::ColorPicker3("##ovw_pick", s_pick, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueWheel)) {
                const unsigned c = ((unsigned)(s_pick[0] * 255.0f + 0.5f) << 16) | ((unsigned)(s_pick[1] * 255.0f + 0.5f) << 8) | (unsigned)(s_pick[2] * 255.0f + 0.5f);
                if (s_picking == 0) s.colorOn = c; else if (s_picking == 1) s.colorOff = c;
            }
            ImGui::PopItemWidth();
            ImGui::EndPopup();
        } else {
            s_picking = -1;
        }
    }
    y += 76.0f;

    Section(cx, y, L4("STYLE DES TOUCHES", "KEY STYLE", "ESTILO DE TECLAS", "ESTILO DAS TECLAS"));
    {
        const char* labels[2] = { L4("Images", "Pictures", "Im\xC3\xA1genes", "Imagens"), L4("Dessin\xC3\xA9" "es", "Drawn", "Dibujadas", "Desenhadas") };
        const int hit = Segmented("##ovw_style", cx, y + 22.0f, cw, 34.0f, labels, 2, s.style ? 0 : 1);
        if (hit >= 0) s.style = hit == 0 ? 1 : 0;
    }
    y += 72.0f;

    Section(cx, y, L4("RACCOURCI EN JEU (AFFICHER / CACHER)", "IN-GAME SHORTCUT (SHOW / HIDE)", "ATAJO EN EL JUEGO (MOSTRAR / OCULTAR)", "ATALHO NO JOGO (MOSTRAR / OCULTAR)"));
    {
        const int key = (int)KeyCaptureHeldCode();
        const int keyEvent = (key > 0 && key != s_prevKey) ? key : 0;
        s_prevKey = key;
        if (s_capturing && keyEvent > 0) {
            // Only modifier keys (E0-E7, Windows is E3 / E7): the plugin reads any other key as Windows.
            if (keyEvent >= 0xE0 && keyEvent <= 0xE7) { s.hotkey = (unsigned)keyEvent; s_capturing = false; }
            else if (keyEvent == 0x29) s_capturing = false;   // Esc
        }
        char name[16];
        const char* shown = s_capturing ? L4("Appuie sur Ctrl, Maj, Alt ou Windows", "Press Ctrl, Shift, Alt or Windows",
                                             "Pulsa Ctrl, May\xC3\xBA" "s, Alt o Windows", "Aperte Ctrl, Shift, Alt ou Windows")
                                        : OverlayKeyName(s.hotkey, name, sizeof(name));
        g_dl->AddRectFilled(P(cx, y + 22.0f), P(cx + cw - 110.0f, y + 56.0f), Col(0x060C18), 8.0f * g_k);
        g_dl->AddRect(P(cx, y + 22.0f), P(cx + cw - 110.0f, y + 56.0f), Col(s_capturing ? kGold : 0x22375A), 8.0f * g_k, ImDrawCornerFlags_All, 1.0f);
        Txt(s_capturing ? g_fontGBody : g_fontGBodyM, s_capturing ? 14.0f : 18.0f, cx + (cw - 110.0f) * 0.5f, y + (s_capturing ? 31.0f : 29.0f),
            s_capturing ? kGold : kTxt, shown, 1);
        if (PillButton("##ovw_hk", cx + cw - 100.0f, y + 22.0f, 100.0f, 34.0f,
                       s_capturing ? L4("Annuler", "Cancel", "Cancelar", "Cancelar") : L4("Changer", "Change", "Cambiar", "Mudar"), false))
            s_capturing = !s_capturing;
    }

    // Save / Reset
    const float by = ry + rh - 62.0f;
    if (s_savedAt >= 0.0 && ImGui::GetTime() - s_savedAt < 6.0)
        Txt(g_fontGSmall, 14.0f, cx, by - 22.0f, s_savedOk ? 0x9BE36A : 0xF0605A,
            s_savedOk ? L4("Enregistr\xC3\xA9 : clique sur Envoyer pour la console", "Saved: click Send for the console",
                           "Guardado: pulsa Enviar para la consola", "Salvo: clique em Enviar para o console")
                      : L4("\xC3\x89" "chec de l'enregistrement", "Save failed", "Error al guardar", "Falha ao salvar"));
    if (PillButton("##ovw_reset", cx, by, 120.0f, 44.0f, L4("D\xC3\xA9" "faut", "Defaults", "Por defecto", "Padr\xC3\xA3o"), false)) {
        s = OverlaySettings();
        s_capturing = false;
    }
    if (PillButton("##ovw_save", cx + 132.0f, by, cw - 132.0f, 44.0f, L4("Enregistrer", "Save", "Guardar", "Salvar"), true)) {
        s_savedOk = OverlaySave(path.c_str(), s);
        s_stamp = plat::FileStamp(path);   // our own write: what is on screen stays
        s_savedAt = ImGui::GetTime();
    }
}
