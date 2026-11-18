// Page "Clavier & souris", rendu glossy - voir ui_glossy.h pour le principe.
//
// Organisation volontairement simple (demande HB, 2026-09-24 : "on commence
// petit") : une poignee de fonctions de dessin locales a ce fichier, puis la
// page elle-meme. On verra apres la page 1 ce qui merite d'etre partage.
//
// Unites : tout est exprime en pixels de la maquette 1280x720 (glossy_layout.h)
// puis multiplie par s_k au moment de dessiner - la console tourne en 1280x720,
// s_k vaut donc 1 et chaque texture tombe pixel pour pixel.
//
// Tailles de police : en pixels ImGui (hauteur ascendante + descendante), soit
// 1,2 x la taille des maquettes (voir theme.h, g_fontG*).
#include "ui_glossy.h"
#include "glossy_layout.h"
#include "theme.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cfloat>
#include <cmath>
#include <vector>

namespace {

const UiSkinTex* s_tex = 0;
float            s_k   = 1.0f;
ImDrawList*      s_dl  = 0;
double           s_btnDirtyAt = -1.0;   // boutons souris modifies, pas encore enregistres
// Menu deroulant des peripheriques : -1 = ferme, 0 = claviers, 1 = souris.
// Tant qu'il est ouvert, le reste de la page ne recoit plus aucun clic
// (s_blockHits) : sinon un clic dans le menu tomberait aussi sur la case
// dessinee dessous.
int              s_menu = -1;
bool             s_blockHits = false;

// --- couleurs -----------------------------------------------------------------
ImU32 Rgb(unsigned rgb, int a = 255) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}
const unsigned kGold  = 0xF5D35A;
const unsigned kTxt   = 0xE9EEF4;
const unsigned kTxt2  = 0xA9B8C9;
const unsigned kTxt3  = 0x7F90A4;
const unsigned kWhite = 0xF2F6FA;
const unsigned kMsg   = 0xF3E7B0;

enum { AL_L, AL_M, AL_R };

ImVec2 P(float x, float y) { return ImVec2(x * s_k, y * s_k); }

// --- images ---------------------------------------------------------------------
bool Has(int id) { return s_tex && id >= 0 && id < GA_COUNT && s_tex[id].present; }

// x, y = coin haut-gauche de la ZONE UTILE ; la marge de lueur est ajoutee ici.
void Img(int id, float x, float y, int alpha = 255) {
    if (!Has(id)) return;
    const UiSkinTex& t = s_tex[id];
    const GlossyAssetSpec& a = g_glossyAssets[id];
    float w = (float)t.w, h = (float)t.h;
    if (id == GA_BG_PAGE_HALF) { w = (float)a.w; h = (float)a.h; }   // agrandi x2
    const float x0 = x - a.pad, y0 = y - a.pad;
    s_dl->AddImage(t.tex, P(x0, y0), P(x0 + w, y0 + h), ImVec2(0, 0), ImVec2(1, 1),
                   IM_COL32(255, 255, 255, alpha));
}
void ImgAt(int id, const GlRect& r, int alpha = 255) { Img(id, r.x, r.y, alpha); }

// Image centree sur un point (badges, lueurs).
void ImgCentered(int id, float cx, float cy) {
    if (!Has(id)) return;
    const GlossyAssetSpec& a = g_glossyAssets[id];
    Img(id, cx - a.w * 0.5f, cy - a.h * 0.5f);
}

// Rognee a une largeur utile (remplissage du curseur).
void ImgClipW(int id, float x, float y, float usefulW) {
    if (!Has(id) || usefulW <= 0.0f) return;
    const UiSkinTex& t = s_tex[id];
    const GlossyAssetSpec& a = g_glossyAssets[id];
    float w = usefulW + 2.0f * a.pad;
    if (w > (float)t.w) w = (float)t.w;
    s_dl->AddImage(t.tex, P(x - a.pad, y - a.pad), P(x - a.pad + w, y - a.pad + t.h),
                   ImVec2(0, 0), ImVec2(w / (float)t.w, 1.0f), IM_COL32(255, 255, 255, 255));
}

int AssetW(int id) { return (id >= 0 && id < GA_COUNT) ? g_glossyAssets[id].w : 0; }
int AssetH(int id) { return (id >= 0 && id < GA_COUNT) ? g_glossyAssets[id].h : 0; }

// --- texte ---------------------------------------------------------------------
ImFont* F(ImFont* f) { return f ? f : ImGui::GetFont(); }

float TextW(ImFont* f, float px, const char* s) {
    if (!s || !s[0]) return 0.0f;
    return F(f)->CalcTextSizeA(px * s_k, FLT_MAX, 0.0f, s).x / s_k;
}

// y = milieu vertical. maxW > 0 : tronque avec "..." (sans couper un caractere
// UTF-8 en deux). Renvoie la largeur dessinee.
float Text(ImFont* f, float px, float x, float cy, ImU32 col, const char* s, int al = AL_L, float maxW = 0.0f) {
    if (!s || !s[0]) return 0.0f;
    f = F(f);
    char buf[256];
    const char* t = s;
    float w = TextW(f, px, s);
    // D'abord retrecir un peu (jusqu'a 80 %), ne couper qu'en dernier recours.
    if (maxW > 0.0f && w > maxW) {
        float k = maxW / w;
        if (k < 0.8f) k = 0.8f;
        px *= k;
        w = TextW(f, px, s);
    }
    if (maxW > 0.0f && w > maxW) {
        int n = (int)strlen(s);
        if (n > (int)sizeof(buf) - 4) n = (int)sizeof(buf) - 4;
        while (n > 0) {
            while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;   // debut de caractere
            memcpy(buf, s, n);
            memcpy(buf + n, "\xE2\x80\xA6", 4);                            // "..." + zero final
            if (TextW(f, px, buf) <= maxW) break;
            n--;
        }
        t = buf;
        w = TextW(f, px, t);
    }
    if (al == AL_M) x -= w * 0.5f;
    else if (al == AL_R) x -= w;
    const float size = px * s_k;
    s_dl->AddText(f, size, ImVec2(x * s_k, cy * s_k - size * 0.5f), col, t);
    return w;
}

// Texte avec espacement des lettres (ASCII seulement : "STUDIO").
void TextSpaced(ImFont* f, float px, float x, float cy, ImU32 col, const char* s, float spacing) {
    char one[2] = { 0, 0 };
    for (int i = 0; s && s[i]; i++) {
        one[0] = s[i];
        x += Text(f, px, x, cy, col, one) + spacing;
    }
}

// Texte qui retrecit pour tenir dans maxW (onglets : les libelles traduits
// n'ont pas tous la meme longueur).
void TextFit(ImFont* f, float px, float cx, float cy, ImU32 col, const char* s, float maxW) {
    const float w = TextW(f, px, s);
    if (w > maxW && w > 0.0f) px *= maxW / w;
    Text(f, px, cx, cy, col, s, AL_M);
}


// --- interaction -------------------------------------------------------------
struct Hit {
    bool pressed, hot, active, rclick, deactivated;
};

// Bouton invisible ImGui sur un rectangle de la maquette. "hot" = survole a la
// souris, OU selectionne a la manette quand la navigation est visible.
Hit HitRect(const char* id, float x, float y, float w, float h) {
    Hit r;
    if (s_blockHits) {
        r.pressed = r.hot = r.active = r.rclick = r.deactivated = false;
        return r;
    }
    ImGui::SetCursorScreenPos(P(x, y));
    r.pressed = ImGui::InvisibleButton(id, ImVec2(w * s_k, h * s_k));
    const bool hovered = ImGui::IsItemHovered();
    r.hot = hovered || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
    r.active = ImGui::IsItemActive();
    r.rclick = hovered && ImGui::IsMouseClicked(1);
    r.deactivated = ImGui::IsItemDeactivated();
    return r;
}
Hit HitAt(const char* id, const GlRect& r) { return HitRect(id, r.x, r.y, r.w, r.h); }

// Bouton complet : texture normale / selectionnee + libelle centre.
bool Button(const char* id, const GlRect& r, const GlRect& txt, int texN, int texF,
            const char* label, bool forceFocus = false) {
    Hit h = HitAt(id, r);
    const bool f = forceFocus || h.hot;
    ImgAt(f ? texF : texN, r);
    Text(g_fontGLarge, 26.0f, txt.x, txt.y, Rgb(f ? kGold : kWhite), label, AL_M, r.w - 16.0f);
    return h.pressed;
}


// Pad icon in the blue of the keyboard / mouse icons (no image asset exists for it).
void DrawPadGlyph(float gx, float gy, float gw, float gh) {
    const ImU32 c = Rgb(0x6FC3FF);
    const float x = gx, y = gy + 3.0f, w = gw, hh = gh - 6.0f;
    s_dl->AddRect(P(x, y), P(x + w, y + hh), c, 9.0f * s_k, ImDrawCornerFlags_All, 2.0f);
    s_dl->AddLine(P(x + 10.0f, y + hh * 0.5f), P(x + 18.0f, y + hh * 0.5f), c, 2.0f);
    s_dl->AddLine(P(x + 14.0f, y + hh * 0.5f - 4.0f), P(x + 14.0f, y + hh * 0.5f + 4.0f), c, 2.0f);
    s_dl->AddCircleFilled(P(x + w - 11.0f, y + hh * 0.5f - 3.0f), 2.0f * s_k, c);
    s_dl->AddCircleFilled(P(x + w - 7.0f, y + hh * 0.5f + 3.0f), 2.0f * s_k, c);
}

// USB controller chip of the device bar (2026-11-05): the keyboard / mouse chip
// look (chip, icon, dot, arrow). There is no pad image asset, so the icon is drawn.
bool PadChip(bool selected, bool alive) {
    Hit h = HitAt("##gl_devpad", GL_PucePad);
    ImgAt((h.hot || selected || s_menu == 2) ? GA_DEV_CHIP_ON : GA_DEV_CHIP, GL_PucePad);
    if (selected) s_dl->AddRect(P(GL_PucePad.x - 2.0f, GL_PucePad.y - 2.0f), P(GL_PucePad.x + GL_PucePad.w + 2.0f, GL_PucePad.y + GL_PucePad.h + 2.0f), Rgb(kGold), 6.0f * s_k, ImDrawCornerFlags_All, 2.0f);
    DrawPadGlyph(GL_IcoPad.x, GL_IcoPad.y, GL_IcoPad.w, GL_IcoPad.h);
    ImgAt(alive ? GA_DOT_ON : GA_DOT_RED, GL_DotPad);
    ImgAt(GA_CHEVRON, GL_ChevronPad);
    return h.pressed;
}

// Drop-down list under a chip (name + VID:PID, then "Refresh"). Returns the picked
// index or -1; sets refresh when the last row is clicked. Closes on a pick or an outside click.
int DeviceMenu(int d, const UiDeviceEntry* list, int count, int sel, const GlRect& c0, bool& refresh) {
    ImGuiIO& io = ImGui::GetIO();
    char buf[64];
    if (!list) count = 0;
    const float rowH = 40.0f, mw = 380.0f;
    const int rows = (count > 0 ? count : 1) + 1;   // + "Refresh" row
    const float mx = c0.x + c0.w * 0.5f - mw * 0.5f, my = c0.y + c0.h + 8.0f;
    const float mh = rows * rowH + 12.0f;
    s_dl->AddRectFilled(P(mx - 4.0f, my + 2.0f), P(mx + mw + 4.0f, my + mh + 10.0f), IM_COL32(0, 0, 0, 90), 8.0f * s_k);
    s_dl->AddRectFilled(P(mx, my), P(mx + mw, my + mh), Rgb(0x0F1824, 248), 5.0f * s_k);
    s_dl->AddRect(P(mx, my), P(mx + mw, my + mh), Rgb(0x6CA2DE, 200), 5.0f * s_k, ImDrawCornerFlags_All, 1.0f);
    s_dl->AddLine(P(mx + 10.0f, my + 1.0f), P(mx + mw - 10.0f, my + 1.0f), Rgb(0xBFE2FF, 160), 1.0f);
    int picked = -1;
    bool close = false;
    if (count == 0) Text(g_fontGBody, 24.0f, mx + 16.0f, my + 6.0f + rowH * 0.5f, Rgb(kTxt3), Tr(STR_GLOSSY_NONE));
    for (int i = 0; i < count; i++) {
        const float ry = my + 6.0f + rowH * i;
        sprintf(buf, "##gl_menu%d_%d", d, i);
        Hit h = HitRect(buf, mx + 4.0f, ry, mw - 8.0f, rowH);
        const bool on = (i == sel);
        if (h.hot) s_dl->AddRectFilled(P(mx + 4.0f, ry + 2.0f), P(mx + mw - 4.0f, ry + rowH - 2.0f), Rgb(kGold, 38), 3.0f * s_k);
        Img(list[i].active ? GA_DOT_ON : GA_DOT_RED, mx + 14.0f, ry + rowH * 0.5f - 5.0f);
        const float idW = TextW(g_fontGBody, 22.0f, list[i].id);
        Text(on ? g_fontGBodyM : g_fontGBody, 24.0f, mx + 34.0f, ry + rowH * 0.5f,
             Rgb(on || h.hot ? kGold : kTxt), list[i].name, AL_L, mw - 34.0f - idW - 30.0f);
        Text(g_fontGBody, 22.0f, mx + mw - 14.0f, ry + rowH * 0.5f, Rgb(kTxt3), list[i].id, AL_R);
        if (h.pressed) { picked = i; close = true; }
    }
    {
        const float ry = my + 6.0f + rowH * (rows - 1);
        s_dl->AddLine(P(mx + 10.0f, ry), P(mx + mw - 10.0f, ry), Rgb(0x2C3B4F), 1.0f);
        Hit h = HitRect("##gl_menu_refresh", mx + 4.0f, ry, mw - 8.0f, rowH);
        if (h.hot) s_dl->AddRectFilled(P(mx + 4.0f, ry + 2.0f), P(mx + mw - 4.0f, ry + rowH - 2.0f), Rgb(kGold, 38), 3.0f * s_k);
        Text(g_fontGBody, 24.0f, mx + 34.0f, ry + rowH * 0.5f, Rgb(h.hot ? kGold : kTxt2), Tr(STR_GLOSSY_REFRESH));
        Text(g_fontGBodyM, 24.0f, mx + 16.0f, ry + rowH * 0.5f, Rgb(h.hot ? kGold : kTxt2), "…", AL_M);
        if (h.pressed) { refresh = true; close = true; }
    }
    const ImVec2 mp = io.MousePos;
    const bool inside = mp.x >= mx * s_k && mp.x <= (mx + mw) * s_k && mp.y >= c0.y * s_k && mp.y <= (my + mh) * s_k;
    if (close || (ImGui::IsMouseClicked(0) && !inside)) s_menu = -1;
    return picked;
}

// Badge de chaque case de la grille, dans l'ordre de l'assistant.
const int kGridBadge[21] = {
    // Icones Kenney "Input Prompts" (CC0) - 2026-09-25
    GA_KP_A, GA_KP_B, GA_KP_X, GA_KP_Y, GA_KP_LB, GA_KP_RB, GA_KP_LT, GA_KP_RT, GA_KP_L3, GA_KP_R3,
    GA_KP_START, GA_KP_BACK, GA_KP_GUIDE,
    GA_KP_DUP, GA_KP_DDOWN, GA_KP_DLEFT, GA_KP_DRIGHT,
    GA_KP_LSUP, GA_KP_LSDOWN, GA_KP_LSLEFT, GA_KP_LSRIGHT,
};

} // namespace

void UiGlossySetTextures(const UiSkinTex* tex) { s_tex = tex; }

void UiGlossyOpenDeviceMenu(int which) { s_menu = which; }

bool UiGlossyReady() {
    if (!s_tex) return false;
    for (int i = 0; i < GA_COUNT; i++)
        if (!s_tex[i].present) return false;
    return true;
}

// Windows Studio: it draws its own tab bar (more pages, icons): only background and logo here.
static bool s_externalTabs = false;
void UiGlossySetExternalTabs(bool on) { s_externalTabs = on; }

// En-tete commun a toutes les pages a onglets : fond, logo, onglets, LB/RB.
static void HeaderAndTabs(int& activeTab, bool busy) {
    char buf[64];
    Img(GA_BG_PAGE_HALF, 0.0f, 0.0f);
    ImgAt(GA_BRAND_BADGE, GL_ImgLogo);
    ImgAt(GA_BRAND_STACK, GL_ImgNom);
    {
        // STUDIO etale sur la largeur exacte de X360 / REMAP
        float sum = 0.0f;
        char one[2] = { 0, 0 };
        for (const char* p = "STUDIO"; *p; p++) { one[0] = *p; sum += TextW(g_fontGCaps, 18.0f, one); }
        TextSpaced(g_fontGCaps, 18.0f, GL_ImgNom.x, GL_TxtStudio.y, Rgb(kTxt2), "STUDIO", (GL_ImgNom.w - sum) / 5.0f);
    }
    // (Badges LB/RB retires le 2026-09-25, decision HB : pas de navigation
    //  manette affichee tant que la v1.3 ne gere pas vraiment les manettes.)
    if (s_externalTabs) return;
    ImgAt(GA_TABBAR, GL_Onglets);
    {
        const StrId tabIds[5] = { STR_GLOSSY_TAB_KBMOUSE, STR_GLOSSY_TAB_GAMES, STR_OV_TAB, STR_GLOSSY_TAB_HOWTO, STR_GLOSSY_TAB_SUPPORT };
        const float tw = GL_OngletActif.w;
        // The tab bar texture holds four tabs: the fifth cell is drawn next to it.
        {
            const float x5 = GL_Onglets.x + GL_Onglets.w, y5 = GL_Onglets.y, h5 = GL_Onglets.h;
            s_dl->AddRectFilledMultiColor(P(x5, y5), P(x5 + tw, y5 + h5), Rgb(0x14243C), Rgb(0x14243C), Rgb(0x0B1626), Rgb(0x0B1626));
            s_dl->AddRect(P(x5, y5), P(x5 + tw, y5 + h5), Rgb(0x2C4468), 0.0f, ImDrawCornerFlags_All, 1.0f);
        }
        for (int i = 0; i < 5; i++) {
            const float x = GL_OngletActif.x + tw * i;
            sprintf(buf, "##gl_tab%d", i);
            Hit h = HitRect(buf, x, GL_OngletActif.y, tw, GL_OngletActif.h);
            const bool on = (activeTab == i);
            if (on) {
                Img(GA_TAB_ON, x, GL_OngletActif.y);
                Img(GA_TAB_UNDERLINE, x, GL_OngletSoulign.y);
            }
            TextFit(on ? g_fontGBodyM : g_fontGLarge, 25.0f, x + tw * 0.5f, GL_TxtOnglet0.y,
                    Rgb(on ? kGold : (h.hot ? 0xFFFFFF : 0xD5DEE8)), Tr(tabIds[i]), tw - 14.0f);
            if (h.pressed) activeTab = i;
        }
    }

}

void DrawUiGlossyMapping(UiMappingModel& m, int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    char buf[256];

    // Session en cours : assistant (21 etapes) ou capture d'une seule case.
    const bool wizRunning = m.wizardActive && !m.wizardComplete && !m.wizardCancelled;
    const bool wizDone    = m.wizardActive && (m.wizardComplete || m.wizardCancelled);
    const bool capturing  = m.manualCaptureIndex >= 0;
    const bool busy       = m.wizardActive || capturing;

    // =========================================================================
    // Fond + en-tete
    // =========================================================================
    if (s_menu == 2 && m.padCount == 0) s_menu = -1;   // the last pad was unplugged
    s_blockHits = (s_menu >= 0);
    HeaderAndTabs(activeTab, busy);

    // =========================================================================
    // Barre d'information (ou consigne pendant une capture)
    // =========================================================================
    const GlRect& bar = GL_BarreInfo;
    const float barCy = GL_BarreInfo.y + GL_BarreInfo.h * 0.5f;
    if (wizRunning || wizDone || capturing) {
        ImgAt(GA_BAR_INFO_MSG, bar);
        const char* a = "";
        const char* b = "";
        if (wizRunning) {
            a = m.wizardNoKeyboard ? Tr(STR_WIZARD_NO_KEYBOARD) : (m.wizardPrompt ? m.wizardPrompt : "");
            b = (m.wizardMessage && m.wizardMessage[0]) ? m.wizardMessage : Tr(STR_WIZARD_HOLD_TO_SKIP);
        } else if (wizDone) {
            if (m.wizardCancelled) a = Tr(STR_WIZARD_CANCELLED);
            else if (m.wizardSaveOk) { sprintf(buf, Tr(STR_WIZARD_SAVED_FMT), m.wizardResultsCount); a = buf; }
            else a = Tr(STR_WIZARD_SAVE_FAILED);
        } else {
            a = m.manualNoKeyboard ? Tr(STR_WIZARD_NO_KEYBOARD) : (m.manualPrompt ? m.manualPrompt : "");
            b = (m.manualMessage && m.manualMessage[0]) ? m.manualMessage : Tr(STR_MANUAL_CAPTURE_HINT);
        }
        char line[512];
        if (b[0]) sprintf(line, "%.240s   \xC2\xB7   %.240s", a, b);
        else      sprintf(line, "%.500s", a);
        Text(g_fontGBody, 24.0f, bar.x + bar.w * 0.5f, barCy, Rgb(kMsg), line, AL_M, bar.w - 40.0f);
    } else {
        ImgAt(GA_BAR_INFO, bar);
        // Peripheriques : une puce par type, centree. Point vert = branche et
        // lu par le plugin, rouge = absent ou muet. Clic = menu deroulant qui
        // donne le nom et le VID:PID (le nom n'interesse que celui qui le cherche).
        const GlRect* chip[2] = { &GL_PuceClavier, &GL_PuceSouris };
        const GlRect* ico[2]  = { &GL_IcoClavier, &GL_IcoSouris };
        const GlRect* dot[2]  = { &GL_DotClavier, &GL_DotSouris };
        const GlRect* chev[2] = { &GL_ChevronClavier, &GL_ChevronSouris };
        const int     icoId[2] = { GA_ICON_KB, GA_ICON_MS };
        for (int d = 0; d < 2; d++) {
            const UiDeviceEntry* list = d == 0 ? m.keyboards : m.mice;
            const int count = d == 0 ? m.keyboardCount : m.mouseCount;
            int& sel = d == 0 ? m.keyboardIndex : m.mouseIndex;
            if (sel < 0 || sel >= count) sel = 0;
            const bool alive = list && count > 0 && list[sel].active;
            sprintf(buf, "##gl_dev%d", d);
            Hit h = HitAt(buf, *chip[d]);
            ImgAt((h.hot || s_menu == d) ? GA_DEV_CHIP_ON : GA_DEV_CHIP, *chip[d]);
            ImgAt(icoId[d], *ico[d]);
            ImgAt(alive ? GA_DOT_ON : GA_DOT_RED, *dot[d]);
            ImgAt(GA_CHEVRON, *chev[d]);
            if (h.pressed) s_menu = (s_menu == d) ? -1 : d;
        }
        // USB controllers: one chip of the same kind, shown when a pad is plugged.
        if (m.padCount > 0) {
            bool any = false;
            for (int i = 0; i < m.padCount; i++) any = any || m.pads[i].active;
            if (PadChip(false, any)) s_menu = (s_menu == 2) ? -1 : 2;
        }

        // Langues : drapeaux (EN FR ES PT), index du modele FR=0 EN=1 PT=2 ES=3.
        const GlRect* flags[4] = { &GL_LangueEN, &GL_LangueFR, &GL_LangueES, &GL_LanguePT };
        const int     flagId[4] = { GA_FLAG_EN, GA_FLAG_FR, GA_FLAG_ES, GA_FLAG_PT };
        const int     langOf[4] = { 1, 0, 3, 2 };
        for (int i = 0; i < 4; i++) {
            const GlRect& f = *flags[i];
            sprintf(buf, "##gl_lang%d", i);
            Hit h = HitRect(buf, f.x - 4.0f, f.y - 4.0f, f.w + 8.0f, f.h + 8.0f);
            const bool on = (m.langIndex == langOf[i]);
            if (on || h.hot) Img(GA_FLAG_RING, f.x - 3.0f, f.y - 3.0f, on ? 255 : 150);
            ImgAt(flagId[i], f, (on || h.hot) ? 255 : 170);
            if (h.pressed) m.langIndex = langOf[i];
        }
    }

    // =========================================================================
    // Colonne clavier
    // =========================================================================
    ImgAt(GA_ICON_KB, GL_IcoTitreClavier);
    Text(g_fontGTitle, 37.0f, GL_TxtTitreClavier.x, GL_TxtTitreClavier.y, Rgb(0xF2F5F8), Tr(STR_GLOSSY_KB_TITLE));
    ImgAt(GA_TITLE_RULE, GL_RegleClavier);

    {
        const char* label;
        if (wizRunning) {
            sprintf(buf, Tr(STR_WIZARD_STEP_PROGRESS_FMT), m.wizardStepIndex + 1, m.wizardStepTotal, m.wizardResultsCount);
            label = buf;
        } else if (wizDone) {
            label = Tr(STR_WIZARD_BTN_OK);
        } else {
            sprintf(buf, Tr(STR_GLOSSY_WIZARD_FMT), m.gridCount > 0 ? m.gridCount : 21);
            label = buf;
        }
        if (Button("##gl_wizard", GL_BtnAssistant, GL_TxtAssistant, GA_BTN_WIDE, GA_BTN_WIDE_F, label, wizRunning || wizDone)) {
            if (wizDone) m.requestWizardClose = true;
            else if (!busy) m.requestStartWizard = true;
        }
    }

    // Case mise en avant sur la manette : capture > etape de l'assistant > survol.
    int focusIdx = capturing ? m.manualCaptureIndex : (wizRunning ? m.wizardStepIndex : -1);
    const int cells = m.gridCount < 21 ? m.gridCount : 21;
    for (int i = 0; i < cells; i++) {
        const int col = i / 7, row = i % 7;
        const float x = GL_CaseA.x + GL_KGRID_DX * col;
        const float y = GL_CaseA.y + GL_KGRID_DY * row;
        sprintf(buf, "##gl_key%d", i);
        Hit h = HitRect(buf, x, y, GL_CaseA.w, GL_CaseA.h);

        const bool waiting = (capturing && i == m.manualCaptureIndex) || (wizRunning && i == m.wizardStepIndex);
        const bool done    = wizRunning && m.wizardStepIndex >= 0 && i < m.wizardStepIndex;
        const bool lit     = waiting || (h.hot && !busy);
        Img(lit ? GA_KCELL_F : (done ? GA_KCELL_DONE : GA_KCELL), x, y);

        const int badge = kGridBadge[i];
        Img(badge, x + 8.0f, y + GL_CaseA.h * 0.5f - AssetH(badge) * 0.5f);
        const float tx = x + 8.0f + AssetW(badge) + 8.0f;          // debut de la zone "touche"
        const float kRight = x + GL_CaseA.w - 8.0f;                  // touches alignees a DROITE
        const float maxW = kRight - tx;
        const char* key = (m.grid && m.grid[i].key) ? m.grid[i].key : "-";
        const bool none = (strcmp(key, "-") == 0);
        const float cyc = y + GL_CaseA.h * 0.5f;
        // Icone de touche (Kenney) quand on en a une, sinon le libelle ecrit.
        int keyIcon = -1;
        const char* keyPrefix = "";
        for (int k = 0; !waiting && !none && k < GL_KEY_ICON_COUNT; k++) {
            if (strcmp(GL_KEY_ICONS[k].label, key) == 0) { keyIcon = GL_KEY_ICONS[k].asset; keyPrefix = GL_KEY_ICONS[k].prefix; break; }
        }
        if (waiting) {
            Text(g_fontGBodyM, 21.0f, kRight, cyc, Rgb(kGold), Tr(STR_GLOSSY_CAPTURE), AL_R, maxW);
        } else if (keyIcon >= 0) {
            // [prefixe][touche] cale a droite ; touche trop large = reduite, jamais deformee.
            const float preW = keyPrefix[0] ? TextW(g_fontGCaps, 17.0f, keyPrefix) + 4.0f : 0.0f;
            const float room = maxW - preW;
            float kw = (float)AssetW(keyIcon), kh = (float)AssetH(keyIcon);
            if (kw > room && room > 8.0f) { kh *= room / kw; kw = room; }
            const float kx = kRight - kw;
            if (keyPrefix[0])
                Text(g_fontGCaps, 17.0f, kx - 4.0f, cyc, Rgb(lit ? kGold : kTxt2), keyPrefix, AL_R);
            if (Has(keyIcon))
                s_dl->AddImage(s_tex[keyIcon].tex, P(kx, cyc - kh * 0.5f), P(kx + kw, cyc + kh * 0.5f),
                               ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, lit ? 255 : 235));
        } else {
            Text(lit ? g_fontGBodyM : g_fontGBody, 24.0f, kRight, cyc,
                 Rgb(lit ? kGold : (none ? 0x8C9AAB : kTxt)), key, AL_R, maxW);
        }

        if (h.hot && focusIdx < 0 && !busy) focusIdx = i;
        if (h.pressed && !busy) m.requestManualAssignIndex = i;
    }

    // ("Actualiser les peripheriques" : deplace dans les menus des peripheriques,
    //  et fait tout seul quand le plugin signale un appareil inconnu - main.cpp)

    // =========================================================================
    // Manette (face + dessus) et bouton mis en avant
    // =========================================================================
    // Pas de trait de liaison : il barrait forcement d'autres boutons. Le bouton
    // s'allume sur le dessin et l'etiquette se centre entre les deux vues.
    const bool showPad = focusIdx >= 0 && focusIdx < 21;
    ImgAt(GA_PAD_FRONT, GL_ImgManetteFace);
    // (vue unique depuis la manette de HB : plus de vue de dessus)
    if (showPad) {
        const GlPadPoint& p = GL_PAD_POINTS[focusIdx];
        ImgCentered(p.glowAsset, p.x, p.y);
        const char* btn = (m.grid && focusIdx < m.gridCount && m.grid[focusIdx].button) ? m.grid[focusIdx].button : "";
        const char* key = (m.grid && focusIdx < m.gridCount && m.grid[focusIdx].key) ? m.grid[focusIdx].key : "-";
        sprintf(buf, "  \xC2\xB7  %.120s", (capturing || wizRunning) ? "?" : key);
        const float w1 = TextW(g_fontGBodyM, 26.0f, btn);
        float w2 = TextW(g_fontGBody, 25.0f, buf);
        if (w2 > 300.0f) w2 = 300.0f;
        const float x = p.labelX - (w1 + w2) * 0.5f;
        Text(g_fontGBodyM, 26.0f, x, p.labelY, Rgb(kGold), btn);
        Text(g_fontGBody, 25.0f, x + w1, p.labelY, Rgb(kTxt), buf, AL_L, 300.0f);
    }

    if (Button("##gl_measure", GL_BtnMesurer, GL_TxtMesurer, GA_BTN_HALF, GA_BTN_HALF_F, Tr(STR_MEASURE_BTN_START)) && !busy)
        m.requestMeasureMouse = true;
    if (Button("##gl_calib", GL_BtnCalibrer, GL_TxtCalibrer, GA_BTN_HALF, GA_BTN_HALF_F, Tr(STR_GLOSSY_CALIBRATE)) && !busy)
        m.requestStartCalibration = true;

    // =========================================================================
    // Colonne souris
    // =========================================================================
    ImgAt(GA_ICON_MS, GL_IcoTitreSouris);
    Text(g_fontGTitle, 37.0f, GL_TxtTitreSouris.x, GL_TxtTitreSouris.y, Rgb(0xF2F5F8), Tr(STR_GLOSSY_MS_TITLE), AL_L, 240.0f);
    ImgAt(GA_TITLE_RULE_S, GL_RegleSouris);

    {
        const StrId names[5] = { STR_SETTINGS_CLICK_LEFT_LABEL, STR_SETTINGS_CLICK_RIGHT_LABEL, STR_SETTINGS_CLICK_MIDDLE_LABEL,
                                 STR_SETTINGS_WHEEL_FORWARD_LABEL, STR_SETTINGS_WHEEL_BACKWARD_LABEL };
        int* vals[5] = { m.btnLeft, m.btnRight, m.btnMiddle, m.wheelForward, m.wheelBackward };
        const float cw = GL_CaseMLEFT.w, ch = GL_CaseMLEFT.h;
        const int icons[6] = { GA_M_LEFT, GA_M_RIGHT, GA_M_MID, GA_M_WUP, GA_M_WDOWN, GA_M_SIDE };
        for (int i = 0; i < 6; i++) {
            const float x = GL_CaseMLEFT.x + GL_MGRID_DX * (i % 2);
            const float y = GL_CaseMLEFT.y + GL_MGRID_DY * (i / 2);
            const float cy = y + ch * 0.5f;
            sprintf(buf, "##gl_mbtn%d", i);
            Hit h = HitRect(buf, x, y, cw, ch);
            if (i == 5) {
                // Boutons lateraux : pas encore geres par le plugin - case
                // visible mais attenuee, l'info-bulle dit pourquoi.
                Img(GA_MCELL_S_ADD, x, y, h.hot ? 200 : 130);
                Img(GA_M_SIDE, x + 12.0f, cy - AssetH(GA_M_SIDE) * 0.5f, h.hot ? 220 : 120);
                sprintf(buf, "+  %s", Tr(STR_GLOSSY_ADD));
                Text(g_fontGBody, 24.0f, x + 82.0f, cy, Rgb(0xC3CFDC, h.hot ? 230 : 140), buf, AL_M, cw - 56.0f);
                if (h.hot) ImGui::SetTooltip("%s", Tr(STR_UI_EXTRA_BUTTONS_SOON));
                continue;
            }
            Img(h.hot ? GA_MCELL_S_F : GA_MCELL_S, x, y);
            Img(icons[i], x + 12.0f, cy - AssetH(icons[i]) * 0.5f);
            Text(g_fontGBody, 26.0f, x + 50.0f, cy, Rgb(h.hot ? kGold : kTxt3), "\xE2\x80\xBA", AL_M);   // "›"
            // le nom du bouton souris n'est plus ecrit : l'icone le montre, l'info-bulle le dit
            if (h.hot) ImGui::SetTooltip("%s", Tr(names[i]));

            int* v = vals[i];
            const int n = m.buttonTargetCount;
            if (!v || n <= 0) continue;
            if (*v < 0 || *v >= n) *v = 0;
            const int badge = m.buttonTargetBadge ? m.buttonTargetBadge[*v] : -1;
            const float bx = x + 62.0f;
            if (badge >= 0)
                Img(badge, bx, cy - AssetH(badge) * 0.5f);
            else if (m.buttonTargets && m.buttonTargets[*v])
                Text(g_fontGBody, 22.0f, bx, cy, Rgb(kTxt), m.buttonTargets[*v], AL_L, x + cw - 8.0f - bx);

            // Clic = cible suivante, clic droit = precedente. Enregistrement
            // groupe 1,5 s apres le dernier changement (une seule notification).
            if (h.pressed) { *v = (*v + 1) % n;     s_btnDirtyAt = now; }
            if (h.rclick)  { *v = (*v + n - 1) % n; s_btnDirtyAt = now; }
        }
        if (s_btnDirtyAt > 0.0 && now - s_btnDirtyAt > 1.5) {
            m.requestSaveButtons = true;
            s_btnDirtyAt = -1.0;
        }
    }

    // Curseurs sensibilite / zone morte + inversion Y
    ImgAt(GA_GROUP_SLIDERS, GL_GroupeCurseurs);
    {
        // Quatre rangees (2026-09-26) : sensibilite, zone morte, anti-zone
        // morte horizontale | verticale (Rail2 coupe en deux), lissage.
        const StrId names[4] = { STR_SETTINGS_SENSITIVITY_LABEL, STR_SETTINGS_DEADZONE_LABEL,
                                 STR_SETTINGS_ANTIDZ_LABEL, STR_SETTINGS_SMOOTH_LABEL };
        const GlRect* rails[4]  = { &GL_Rail0, &GL_Rail1, &GL_Rail2, &GL_Rail3 };
        const GlRect* titles[4] = { &GL_TxtCurseur0, &GL_TxtCurseur1, &GL_TxtCurseur2, &GL_TxtCurseur3 };
        // Une "piste" = un curseur ; la rangee 2 en porte deux.
        struct Track { int row; int* v; int vmin, vmax, step; float x, w; const char* tag; };
        const float hw = (GL_Rail2.w - 34.0f) * 0.5f;
        Track tracks[5] = {
            { 0, m.sensitivity,   m.sensitivityMin,  m.sensitivityMax,  50, GL_Rail0.x, GL_Rail0.w, 0 },
            { 1, m.deadzone,      m.deadzoneMin,     m.deadzoneMax,     50, GL_Rail1.x, GL_Rail1.w, 0 },
            { 2, m.antiDeadzone,  m.antiDeadzoneMin, m.antiDeadzoneMax, 50, GL_Rail2.x + 14.0f, hw, "H" },
            { 2, m.antiDeadzoneY, 0,                 m.antiDeadzoneMax, 50, GL_Rail2.x + 34.0f + hw, hw, "V" },
            { 3, m.smoothing,     0,                 m.smoothingMax,     1, GL_Rail3.x, GL_Rail3.w, 0 },
        };
        for (int j = 0; j < 4; j++)
            Text(g_fontGTitle, 30.0f, titles[j]->x, titles[j]->y, Rgb(0xF2F5F8), Tr(names[j]), AL_L, 180.0f);
        for (int t = 0; t < 5; t++) {
            const Track& tr = tracks[t];
            int* v = tr.v;
            const int span = tr.vmax - tr.vmin;
            if (!v || span <= 0) continue;
            const GlRect& r = *rails[tr.row];
            sprintf(buf, "##gl_slider%d", t);
            Hit h = HitRect(buf, tr.x - 10.0f, r.y - 14.0f, tr.w + 20.0f, r.h + 28.0f);
            if (h.active) {
                float f = (io.MousePos.x / s_k - tr.x) / tr.w;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                int nv = tr.vmin + (int)(f * span + 0.5f);
                nv = (nv / tr.step) * tr.step;
                if (nv < tr.vmin) nv = tr.vmin;
                if (nv > tr.vmax) nv = tr.vmax;
                *v = nv;
            }
            if (h.deactivated) m.requestSaveMouse = true;
            // "V" a 0 = identique a "H" : affiche et positionne comme tel.
            const bool follows = (tr.tag && tr.tag[0] == 'V' && *v == 0 && m.antiDeadzone);
            const int shown = follows ? *m.antiDeadzone : *v;
            float frac = (float)(shown - tr.vmin) / (float)span;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            if (Has(GA_SLIDER_TRACK)) {
                const UiSkinTex& tt = s_tex[GA_SLIDER_TRACK];
                const float pad = (float)g_glossyAssets[GA_SLIDER_TRACK].pad;
                s_dl->AddImage(tt.tex, P(tr.x - pad, r.y - pad), P(tr.x + tr.w + pad, r.y - pad + tt.h));
            }
            if (Has(GA_SLIDER_FILL) && frac > 0.0f) {
                const UiSkinTex& tf = s_tex[GA_SLIDER_FILL];
                const float pad = (float)g_glossyAssets[GA_SLIDER_FILL].pad;
                s_dl->AddImage(tf.tex, P(tr.x - pad, r.y - pad), P(tr.x + frac * tr.w + pad, r.y - pad + tf.h),
                               ImVec2(0, 0), ImVec2(frac, 1.0f), IM_COL32(255, 255, 255, follows ? 120 : 255));
            }
            ImgCentered(GA_SLIDER_KNOB, tr.x + frac * tr.w, r.y + r.h * 0.5f);
            if (tr.tag)
                Text(g_fontGSmall, 18.0f, tr.x - 12.0f, r.y + 3.0f, Rgb(kTxt3), tr.tag, AL_M);
            if (h.hot && tr.row == 2) ImGui::SetTooltip("%s", Tr(STR_SETTINGS_ANTIDZ_HINT));
            if (h.hot && tr.row == 3) ImGui::SetTooltip("%s", Tr(STR_SETTINGS_SMOOTH_HINT));
            if (h.hot && tr.row == 2 && tr.tag[0] == 'V') ImGui::SetTooltip("%s", Tr(STR_SETTINGS_ANTIDZ_Y_HINT));
        }
        // Valeurs a droite, sur la ligne du titre
        for (int j = 0; j < 4; j++) {
            const float tx = GL_Rail0.x + GL_Rail0.w;
            if (j == 0 && m.sensitivity) sprintf(buf, "%d", *m.sensitivity);
            else if (j == 1 && m.deadzone) sprintf(buf, "%d", *m.deadzone);
            else if (j == 2 && m.antiDeadzone) {
                if (m.antiDeadzoneY && *m.antiDeadzoneY > 0) sprintf(buf, "%d \xC2\xB7 %d", *m.antiDeadzone, *m.antiDeadzoneY);
                else sprintf(buf, "%d", *m.antiDeadzone);
            }
            else if (j == 3 && m.smoothing) {
                if (*m.smoothing > 0) sprintf(buf, "%d ms", *m.smoothing);
                else sprintf(buf, "%s", Tr(STR_SETTINGS_SMOOTH_OFF));
            }
            else continue;
            Text(g_fontGBodyM, 22.0f, tx, titles[j]->y + 2.0f, Rgb(kGold), buf, AL_R);
        }

        // Inversion de l'axe vertical : petite case en haut a droite du groupe.
        if (m.invertY) {
            // 2026-09-26 : descendue sur la ligne du DPI (la ligne du titre porte
            // maintenant la valeur).
            const float right = GL_GroupeCurseurs.x + GL_GroupeCurseurs.w - 12.0f;
            const char* lbl = Tr(STR_GLOSSY_INVERT);
            const float lw = TextW(g_fontGSmall, 20.0f, lbl);
            const float bx = right - lw - 24.0f, cy = GL_TxtDpi.y;
            Hit h = HitRect("##gl_invert", bx - 4.0f, cy - 12.0f, lw + 32.0f, 24.0f);
            s_dl->AddRect(P(bx, cy - 8.0f), P(bx + 16.0f, cy + 8.0f), Rgb(h.hot ? kGold : 0x8FA2B8), 3.0f * s_k, ImDrawCornerFlags_All, 1.5f);
            if (*m.invertY)
                s_dl->AddRectFilled(P(bx + 4.0f, cy - 4.0f), P(bx + 12.0f, cy + 4.0f), Rgb(kGold), 2.0f * s_k);
            Text(g_fontGSmall, 20.0f, right, cy, Rgb(h.hot ? kGold : kTxt2), lbl, AL_R);
            if (h.pressed) { *m.invertY = !*m.invertY; m.requestSaveMouse = true; }
        }
    }

    if (m.mouseDpiMeasured > 0) {
        sprintf(buf, Tr(STR_GLOSSY_DPI_FMT), m.mouseDpiMeasured);
        Text(g_fontGSmall, 20.0f, GL_Rail0.x, GL_TxtDpi.y, Rgb(kTxt2), buf, AL_L, 150.0f);
    } else {
        Text(g_fontGSmall, 20.0f, GL_Rail0.x, GL_TxtDpi.y, Rgb(kTxt3), Tr(STR_MEASURE_STATUS_UNKNOWN), AL_L, 150.0f);
    }

    // =========================================================================
    // Pied de page gauche : version + etat du plugin (vert actif, rouge muet)
    // =========================================================================
    {
        sprintf(buf, "v%s", m.appVersion ? m.appVersion : "?");
        float x = GL_TxtVersionBas.x;
        x += Text(g_fontGBodyM, 24.0f, x, GL_TxtVersionBas.y, Rgb(0xFFFFFF), buf) + 18.0f;
        Img(m.pluginAlive ? GA_DOT_ON : GA_DOT_RED, x, GL_DotPlugin.y);
        Text(g_fontGBody, 24.0f, x + 18.0f, GL_TxtVersionBas.y, Rgb(m.pluginAlive ? kTxt2 : 0xF08A80),
             Tr(m.pluginAlive ? STR_UI_PLUGIN_ALIVE : STR_UI_PLUGIN_SILENT));
    }

    // =========================================================================
    // Aide en bas a droite
    // =========================================================================
    // (Aide "A Changer" a la manette retiree le 2026-09-25 - reportee a la v1.3.)

    // =========================================================================
    // Reglages par defaut (bas a droite, en petit - demande HB 2026-09-25)
    //   "Reglages par defaut" : remet ceux enregistres comme defaut, ou a
    //       defaut ceux d'origine (commandes FPS du plugin). Confirmation : le
    //       premier clic affiche "Confirmer ?" pendant 3 secondes.
    //   "Enregistrer comme defaut" : les reglages actuels deviennent ceux-la.
    // =========================================================================
    {
        static double s_confirmUntil = -1.0;
        const bool confirming = s_confirmUntil > now;
        const char* lblSave = Tr(STR_GLOSSY_DEF_SAVE);
        const char* lblRest = confirming ? Tr(STR_GLOSSY_CONFIRM) : Tr(STR_GLOSSY_DEF_RESTORE);
        const float right = GL_BarreInfo.x + GL_BarreInfo.w, cy = GL_TxtAideA.y;
        const float ws = TextW(g_fontGSmall, 20.0f, lblSave), wr = TextW(g_fontGSmall, 20.0f, lblRest);
        const float xs = right - ws, xr = xs - 26.0f - wr;
        Hit hs = HitRect("##gl_defsave", xs - 4.0f, cy - 12.0f, ws + 8.0f, 24.0f);
        Hit hr = HitRect("##gl_defrest", xr - 4.0f, cy - 12.0f, wr + 8.0f, 24.0f);
        Text(g_fontGSmall, 20.0f, xr, cy, Rgb(confirming ? kGold : (hr.hot ? 0xFFFFFF : kTxt2)), lblRest);
        Text(g_fontGSmall, 20.0f, xs - 13.0f, cy, Rgb(kTxt3), "\xC2\xB7", AL_M);
        Text(g_fontGSmall, 20.0f, xs, cy, Rgb(hs.hot ? 0xFFFFFF : kTxt2), lblSave);
        s_dl->AddLine(P(xr, cy + 11.0f), P(xr + wr, cy + 11.0f), Rgb(confirming ? kGold : kTxt3, hr.hot || confirming ? 220 : 90), 1.0f);
        s_dl->AddLine(P(xs, cy + 11.0f), P(xs + ws, cy + 11.0f), Rgb(kTxt3, hs.hot ? 220 : 90), 1.0f);
        if (hr.hot && !confirming) ImGui::SetTooltip("%s", Tr(m.hasSavedDefault ? STR_GLOSSY_DEF_RESTORE_TIP : STR_GLOSSY_DEF_FACTORY_TIP));
        if (hs.hot) ImGui::SetTooltip("%s", Tr(STR_GLOSSY_DEF_SAVE_TIP));
        if (hr.pressed && !busy) {
            if (confirming) { m.requestRestoreDefault = true; s_confirmUntil = -1.0; }
            else s_confirmUntil = now + 3.0;
        }
        if (hs.pressed && !busy) m.requestSaveDefault = true;
    }

    // =========================================================================
    // Menu deroulant des peripheriques (dessine en dernier : au-dessus de tout)
    // =========================================================================
    if (s_menu >= 0) {
        s_blockHits = false;
        const int d = s_menu;
        bool refresh = false;
        int picked;
        if (d == 2) {
            picked = DeviceMenu(2, m.pads, m.padCount, -1, GL_PucePad, refresh);
            if (picked >= 0 && !busy) m.requestOpenPad = picked;
        } else {
            const UiDeviceEntry* list = d == 0 ? m.keyboards : m.mice;
            const int count = d == 0 ? m.keyboardCount : m.mouseCount;
            int& sel = d == 0 ? m.keyboardIndex : m.mouseIndex;
            picked = DeviceMenu(d, list, count, sel, d == 0 ? GL_PuceClavier : GL_PuceSouris, refresh);
            if (picked >= 0) sel = picked;
        }
        if (refresh) m.requestRefreshDevices = true;
    }
    s_blockHits = false;
}

// ============================================================================
// PAGES PLEIN ECRAN : Mesurer la souris / Calibrer (2026-09-25)
// Positions ecrites ici (et non dans glossy_layout.h) : ces pages n'ont pas
// de maquette PIL, elles reprennent la grille de la page 1 (marges 44 px,
// panneau 44..1236 x 120..642).
// ============================================================================
namespace {

const float kPgX0 = 44.0f, kPgY0 = 120.0f, kPgW = 1192.0f, kPgH = 522.0f;

// Texte sur plusieurs lignes ; renvoie la hauteur utilisee.
float TextWrap(ImFont* f, float px, float x, float y, float wrapW, ImU32 col, const char* s) {
    if (!s || !s[0]) return 0.0f;
    f = F(f);
    const float size = px * s_k;
    const ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, wrapW * s_k, s);
    s_dl->AddText(f, size, ImVec2(x * s_k, y * s_k), col, s, 0, wrapW * s_k);
    return sz.y / s_k;
}

bool PgButton(const char* id, float x, float y, bool wide, const char* label, bool primary = false) {
    GlRect r; r.x = x; r.y = y; r.w = wide ? 344.0f : 234.0f; r.h = wide ? 42.0f : 40.0f;
    GlRect t; t.x = x + r.w * 0.5f; t.y = y + r.h * 0.5f; t.w = t.h = 0.0f;
    return Button(id, r, t, wide ? GA_BTN_WIDE : GA_BTN_HALF, wide ? GA_BTN_WIDE_F : GA_BTN_HALF_F, label, primary);
}

// Jauge : rail + remplissage du curseur de la page 1 (266 px de large).
void Gauge(float x, float y, float frac) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    Img(GA_SLIDER_TRACK, x, y);
    ImgClipW(GA_SLIDER_FILL, x, y, frac * AssetW(GA_SLIDER_TRACK));
}

void PageFrame(int iconAsset, const char* title, float screenW) {
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    Img(GA_BG_PAGE_HALF, 0.0f, 0.0f);
    ImgAt(GA_BRAND_BADGE, GL_ImgLogo);
    ImgAt(GA_BRAND_STACK, GL_ImgNom);
    {
        float sum = 0.0f;
        char one[2] = { 0, 0 };
        for (const char* p = "STUDIO"; *p; p++) { one[0] = *p; sum += TextW(g_fontGCaps, 18.0f, one); }
        TextSpaced(g_fontGCaps, 18.0f, GL_ImgNom.x, GL_TxtStudio.y, Rgb(kTxt2), "STUDIO", (GL_ImgNom.w - sum) / 5.0f);
    }
    // titre de la page : grande icone + intitule, a droite du logo
    const float ix = 470.0f;
    Img(iconAsset, ix, 60.0f - AssetH(iconAsset) * 0.5f);
    Text(g_fontGTitle, 44.0f, ix + AssetW(iconAsset) + 18.0f, 58.0f, Rgb(0xF2F5F8), title);
    Img(GA_PAGE_PANEL, kPgX0, kPgY0);
}

} // namespace

void DrawUiGlossyMeasure(GlossyMeasureModel& mm, float screenW, float screenH) {
    (void)screenH;
    PageFrame(GA_PG_MEASURE, Tr(STR_MEASURE_TITLE), screenW);
    char buf[256];

    // --- colonne gauche : la methode, illustree --------------------------------
    const float lx = kPgX0 + 40.0f, lw = 560.0f;
    float y = kPgY0 + 34.0f;
    y += TextWrap(g_fontGBody, 25.0f, lx, y, lw, Rgb(kTxt), Tr(STR_MEASURE_STEP_SHEET)) + 16.0f;
    Img(GA_A4_DIAGRAM, lx + (lw - AssetW(GA_A4_DIAGRAM)) * 0.5f, y);
    y += AssetH(GA_A4_DIAGRAM) + 16.0f;
    y += TextWrap(g_fontGBody, 25.0f, lx, y, lw, Rgb(kTxt), Tr(STR_MEASURE_STEP_DRAG)) + 14.0f;
    TextWrap(g_fontGSmall, 20.0f, lx, y, lw, Rgb(kTxt3), Tr(STR_MEASURE_WHY));

    // separateur vertical
    s_dl->AddLine(P(kPgX0 + 650.0f, kPgY0 + 30.0f), P(kPgX0 + 650.0f, kPgY0 + kPgH - 30.0f), Rgb(0x2C3B4F), 1.0f);

    // --- colonne droite : ce qui se passe maintenant -----------------------------
    const float rx = kPgX0 + 690.0f, rw = 460.0f, rcx = rx + rw * 0.5f;
    float ry = kPgY0 + 60.0f;
    if (mm.state == 0) {
        Img(GA_M_LEFT_BIG, rcx - AssetW(GA_M_LEFT_BIG) * 0.5f, ry + 10.0f, mm.noMouse ? 110 : 255);
        ry += AssetH(GA_M_LEFT_BIG) + 40.0f;
        const char* t = mm.noMouse ? Tr(STR_MEASURE_NO_MOUSE) : Tr(STR_GLOSSY_MEASURE_WAIT);
        const float tw = TextW(g_fontGBodyM, 28.0f, t);
        TextWrap(g_fontGBodyM, 28.0f, tw < rw ? rcx - tw * 0.5f : rx, ry, rw, Rgb(mm.noMouse ? 0xF08A80 : kGold), t);
    } else if (mm.state == 1) {
        sprintf(buf, "%d", mm.liveCounts);
        Text(g_fontGTitle, 74.0f, rcx, ry + 40.0f, Rgb(kGold), buf, AL_M);
        sprintf(buf, Tr(STR_MEASURE_LIVE_FMT), mm.liveCounts);
        Text(g_fontGBody, 24.0f, rcx, ry + 100.0f, Rgb(kTxt2), buf, AL_M, rw);
        Gauge(rcx - AssetW(GA_SLIDER_TRACK) * 0.5f, ry + 140.0f, mm.liveCounts / 2000.0f);
    } else {
        sprintf(buf, "%d DPI", mm.resultDpi);
        Text(g_fontGTitle, 74.0f, rcx, ry + 40.0f, Rgb(mm.implausible ? 0xF08A80 : kGold), buf, AL_M);
        sprintf(buf, Tr(STR_MEASURE_RESULT_FMT), mm.resultCpc, mm.resultDpi);
        ry += 100.0f;
        ry += TextWrap(g_fontGBody, 24.0f, rx, ry, rw, Rgb(kTxt), buf) + 10.0f;
        if (mm.implausible) ry += TextWrap(g_fontGBody, 22.0f, rx, ry, rw, Rgb(0xF08A80), Tr(STR_MEASURE_IMPLAUSIBLE)) + 8.0f;
        if (mm.saveFailed)  ry += TextWrap(g_fontGBody, 22.0f, rx, ry, rw, Rgb(0xF08A80), Tr(STR_MEASURE_SAVE_FAILED)) + 8.0f;
    }
    if (mm.savedDpi > 0 && mm.state != 2) {
        sprintf(buf, Tr(STR_GLOSSY_DPI_FMT), mm.savedDpi);
        Text(g_fontGSmall, 20.0f, rcx, kPgY0 + kPgH - 110.0f, Rgb(kTxt3), buf, AL_M, rw);
    }

    // --- boutons, en bas de la colonne droite ----------------------------------
    const float by = kPgY0 + kPgH - 70.0f;
    if (mm.state == 2) {
        if (!mm.implausible && PgButton("##ms_save", rx, by, false, Tr(STR_MEASURE_BTN_SAVE), true)) mm.reqSave = true;
        if (PgButton("##ms_retry", rx + 246.0f, by, false, Tr(STR_MEASURE_BTN_RETRY))) mm.reqRetry = true;
    }
    if (PgButton("##ms_close", 1236.0f - 234.0f, 30.0f, false, Tr(STR_MEASURE_BTN_CANCEL))) mm.reqClose = true;
}

void DrawUiGlossyCalib(GlossyCalibModel& cm, float screenW, float screenH) {
    (void)screenH;
    PageFrame(GA_PG_CALIB, Tr(STR_UI_ADV_CALIB_TITLE), screenW);
    char buf[256];
    const float lx = kPgX0 + 40.0f, lw = cm.running ? 1100.0f : 600.0f;
    float y = kPgY0 + 34.0f;

    if (!cm.running) {
        y += TextWrap(g_fontGBody, 25.0f, lx, y, lw, Rgb(kTxt), Tr(STR_CALIB_INTRO)) + 14.0f;
        y += TextWrap(g_fontGSmall, 21.0f, lx, y, lw, Rgb(kTxt2), Tr(STR_CALIB_NAV_HINT)) + 22.0f;
        sprintf(buf, Tr(STR_CALIB_TARGET_MOUSE_FMT), cm.mouseVid, cm.mousePid);
        Img(GA_ICON_MS, lx, y);
        Text(g_fontGBodyM, 25.0f, lx + 34.0f, y + 16.0f, Rgb(kTxt), buf);
        y += 48.0f;
        if (PgButton("##cal_start", lx, y, true, Tr(STR_CALIB_BTN_START), true)) cm.reqStart = true;

        // --- Reglages avances (colonne droite) --------------------------------
        s_dl->AddLine(P(kPgX0 + 690.0f, kPgY0 + 30.0f), P(kPgX0 + 690.0f, kPgY0 + kPgH - 30.0f), Rgb(0x2C3B4F), 1.0f);
        const float rx = kPgX0 + 720.0f, rw = 430.0f;
        float ry = kPgY0 + 30.0f;
        Img(GA_PG_GEAR, rx, ry);
        Text(g_fontGTitle, 32.0f, rx + 46.0f, ry + 17.0f, Rgb(0xF2F5F8), Tr(STR_UI_ADVANCED));
        ry += 54.0f;
        Img(cm.fallbackActive ? GA_DOT_RED : GA_DOT_ON, rx, ry + 5.0f);
        ry += TextWrap(g_fontGBodyM, 22.0f, rx + 20.0f, ry, rw - 20.0f, Rgb(cm.fallbackActive ? 0xF0B070 : 0x9BE36A),
                       Tr(cm.fallbackActive ? STR_CALIB_FALLBACK_ACTIVE : STR_CALIB_AUTO_ACTIVE)) + 8.0f;
        ry += TextWrap(g_fontGSmall, 19.0f, rx, ry, rw, Rgb(kTxt2), Tr(STR_CALIB_FALLBACK_EXPLAIN)) + 12.0f;
        // La desactivation est aussi accessible que l'activation.
        if (cm.fallbackActive) { if (PgButton("##cal_auto", rx, ry, true, Tr(STR_CALIB_BTN_RESTORE_AUTO))) cm.reqFallback = 0; }
        else                   { if (PgButton("##cal_fb",   rx, ry, true, Tr(STR_CALIB_BTN_FORCE_FALLBACK))) cm.reqFallback = 1; }
        ry += 62.0f;
        s_dl->AddLine(P(rx, ry - 8.0f), P(rx + rw, ry - 8.0f), Rgb(0x2C3B4F), 1.0f);
        ry += TextWrap(g_fontGSmall, 19.0f, rx, ry, rw, Rgb(kTxt2), Tr(STR_CALIB_CLEAR_HINT)) + 10.0f;
        if (PgButton("##cal_clear", rx, ry, true, Tr(STR_CALIB_BTN_CLEAR))) cm.reqClearCalib = true;

        if (PgButton("##cal_close", 1236.0f - 234.0f, 30.0f, false, Tr(STR_CALIB_BTN_CLOSE))) cm.reqClose = true;
        return;
    }

    if (cm.noData)
        y += TextWrap(g_fontGBodyM, 23.0f, lx, y, lw, Rgb(0xF0B070), Tr(STR_CALIB_NO_DATA)) + 12.0f;

    if (cm.finished) {
        if (cm.succeeded) {
            y += TextWrap(g_fontGTitle, 34.0f, lx, y, lw, Rgb(0x9BE36A), Tr(STR_CALIB_SUCCESS)) + 10.0f;
            y += TextWrap(g_fontGBody, 23.0f, lx, y, lw, Rgb(kTxt), cm.summary) + 20.0f;
            if (!cm.saveAttempted) {
                if (PgButton("##cal_save", lx, y, true, Tr(STR_CALIB_BTN_SAVE), true)) cm.reqSave = true;
            } else {
                TextWrap(g_fontGBodyM, 24.0f, lx, y, lw, Rgb(cm.saveOk ? 0x9BE36A : 0xF08A80),
                         Tr(cm.saveOk ? STR_CALIB_SAVED_APPLIED : STR_CALIB_SAVE_FAILED));
            }
        } else {
            y += TextWrap(g_fontGTitle, 30.0f, lx, y, lw, Rgb(0xF08A80), cm.prompt) + 10.0f;
            TextWrap(g_fontGBody, 23.0f, lx, y, lw, Rgb(kTxt), cm.summary);
        }
        if (PgButton("##cal_close2", 1236.0f - 234.0f, 30.0f, false, Tr(STR_CALIB_BTN_CLOSE))) cm.reqClose = true;
        return;
    }

    // Etape en cours : consigne + jauge (elle ne se remplit que si le geste est reel).
    y += TextWrap(g_fontGTitle, 36.0f, lx, y + 20.0f, lw, Rgb(kGold), cm.prompt) + 50.0f;
    {
        // jauge large : trois rails cote a cote pour ~800 px
        const float gx = lx, gw = 3.0f * AssetW(GA_SLIDER_TRACK);
        for (int k = 0; k < 3; k++) {
            const float seg0 = k / 3.0f, seg1 = (k + 1) / 3.0f;
            float f = (cm.progress - seg0) / (seg1 - seg0);
            Gauge(gx + k * AssetW(GA_SLIDER_TRACK), y, f);
        }
        sprintf(buf, "%d %%", (int)(cm.progress * 100.0f + 0.5f));
        Text(g_fontGBodyM, 26.0f, gx + gw + 20.0f, y + 3.0f, Rgb(cm.progress >= 1.0f ? kGold : kTxt), buf);
    }
    y += 34.0f;
    y += TextWrap(g_fontGBody, 23.0f, lx, y, lw, Rgb(kTxt2),
                  Tr(cm.progress >= 1.0f ? STR_CALIB_GESTURE_DONE : STR_CALIB_GESTURE_CONTINUE)) + 26.0f;

    // Plus de bouton "Valider" (2026-09-25) : l'etape suivante demarre toute
    // seule quand la jauge est pleine (CalibrationSession, CALIB_AUTO_ADVANCE_MS).
    // Aller cliquer avec la souris salissait les donnees du geste.
    TextWrap(g_fontGSmall, 20.0f, lx, y + 10.0f, 344.0f, Rgb(kTxt3), Tr(STR_CALIB_VALIDATE_DISABLED_HINT));
    if (cm.canSkip && PgButton("##cal_skip", lx + 364.0f, y, true, Tr(STR_CALIB_BTN_NO_WHEEL))) cm.reqSkipWheel = true;
    if (PgButton("##cal_cancel", 1236.0f - 234.0f, 30.0f, false, Tr(STR_CALIB_BTN_CANCEL))) cm.reqCancel = true;
}

// ============================================================================
// PAGE PROFILS DE JEUX (2026-09-25)
// ============================================================================
namespace {

const float kGridX = 44.0f, kGridY = 156.0f, kColW = 150.0f, kRowH = 236.0f;
const float kCovW = 132.0f, kCovH = 180.0f;
const int   kCols = 5, kRows = 2;

// Vignette generee quand la pochette manque : degrade tire du Title ID + initiales.
void CoverPlaceholder(float x, float y, float w, float h, unsigned tid, const char* name) {
    const unsigned h1 = tid * 2654435761u;
    const int r = 40 + (h1 & 0x3F), g = 60 + ((h1 >> 8) & 0x3F), b = 90 + ((h1 >> 16) & 0x5F);
    s_dl->AddRectFilledMultiColor(P(x, y), P(x + w, y + h),
        IM_COL32(r, g, b, 255), IM_COL32(r, g, b, 255),
        IM_COL32(r / 3, g / 3, b / 3, 255), IM_COL32(r / 3, g / 3, b / 3, 255));
    char ini[4] = { 0, 0, 0, 0 };
    int n = 0;
    bool start = true;
    for (const char* p = name; p && *p && n < 2; p++) {
        const unsigned char ch = (unsigned char)*p;
        const bool alnum = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        if (alnum && start) { ini[n++] = (char)((ch >= 'a' && ch <= 'z') ? ch - 32 : ch); }
        start = !alnum;
    }
    Text(g_fontGTitle, h * 0.28f, x + w * 0.5f, y + h * 0.45f, IM_COL32(255, 255, 255, 200), ini, AL_M);
}

void CoverImage(ImTextureID tex, int tw, int th, float x, float y, float w, float h, unsigned tid, const char* name) {
    if (!tex || tw <= 0 || th <= 0) { CoverPlaceholder(x, y, w, h, tid, name); return; }
    // ajuste dans la case en gardant les proportions, centre
    float k = w / tw;
    if (th * k > h) k = h / th;
    const float dw = tw * k, dh = th * k;
    const float dx = x + (w - dw) * 0.5f, dy = y + (h - dh) * 0.5f;
    s_dl->AddRectFilled(P(x, y), P(x + w, y + h), IM_COL32(8, 12, 18, 255));
    s_dl->AddImage(tex, P(dx, dy), P(dx + dw, dy + dh));
}

// Curseur generique (rail de la page 1) avec titre et valeur. Renvoie true au relachement.
bool SliderRow(const char* id, const char* label, float x, float y, float* value, float vmin, float vmax,
               float step, const char* fmt) {
    ImGuiIO& io = ImGui::GetIO();
    const float tw = (float)AssetW(GA_SLIDER_TRACK);
    Text(g_fontGBody, 24.0f, x, y, Rgb(0xF2F5F8), label);
    const float ty = y + 22.0f;
    Hit h = HitRect(id, x - 8.0f, ty - 12.0f, tw + 16.0f, 30.0f);
    if (h.active) {
        float f = (io.MousePos.x / s_k - x) / tw;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        float v = vmin + f * (vmax - vmin);
        if (step > 0.0f) v = vmin + (float)(int)((v - vmin) / step + 0.5f) * step;
        *value = v < vmin ? vmin : (v > vmax ? vmax : v);
    }
    float frac = (*value - vmin) / (vmax - vmin);
    frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    Img(GA_SLIDER_TRACK, x, ty);
    ImgClipW(GA_SLIDER_FILL, x, ty, frac * tw);
    ImgCentered(GA_SLIDER_KNOB, x + frac * tw, ty + 3.0f);
    char buf[32];
    sprintf(buf, fmt, *value);
    Text(g_fontGBodyM, 24.0f, x + tw + 18.0f, ty + 3.0f, Rgb(h.hot || h.active ? kGold : kTxt), buf);
    return h.deactivated;
}

// Variante a largeur libre (2026-09-26) : piste etiree a `tw`, etiquette
// courte a gauche de la piste, valeur a droite. `ghost` : valeur affichee en
// transparence (ex. V a 0 = suit H).
bool SliderRowW(const char* id, const char* tag, float x, float y, float tw, float* value, float vmin, float vmax,
                float step, const char* fmt, bool ghost) {
    ImGuiIO& io = ImGui::GetIO();
    const float ty = y + 22.0f;
    if (tag) Text(g_fontGSmall, 19.0f, x, ty + 3.0f, Rgb(kTxt3), tag);
    const float tx = x + (tag ? 18.0f : 0.0f);
    Hit h = HitRect(id, tx - 8.0f, ty - 12.0f, tw + 16.0f, 30.0f);
    if (h.active) {
        float f = (io.MousePos.x / s_k - tx) / tw;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        float v = vmin + f * (vmax - vmin);
        if (step > 0.0f) v = vmin + (float)(int)((v - vmin) / step + 0.5f) * step;
        *value = v < vmin ? vmin : (v > vmax ? vmax : v);
    }
    float frac = (*value - vmin) / (vmax - vmin);
    frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    if (Has(GA_SLIDER_TRACK)) {
        const UiSkinTex& tt = s_tex[GA_SLIDER_TRACK];
        const float pad = (float)g_glossyAssets[GA_SLIDER_TRACK].pad;
        s_dl->AddImage(tt.tex, P(tx - pad, ty - pad), P(tx + tw + pad, ty - pad + tt.h));
    }
    if (Has(GA_SLIDER_FILL) && frac > 0.0f) {
        const UiSkinTex& tf = s_tex[GA_SLIDER_FILL];
        const float pad = (float)g_glossyAssets[GA_SLIDER_FILL].pad;
        s_dl->AddImage(tf.tex, P(tx - pad, ty - pad), P(tx + frac * tw + pad, ty - pad + tf.h),
                       ImVec2(0, 0), ImVec2(frac, 1.0f), IM_COL32(255, 255, 255, ghost ? 110 : 255));
    }
    ImgCentered(GA_SLIDER_KNOB, tx + frac * tw, ty + 3.0f);
    char buf[32];
    sprintf(buf, fmt, *value);
    Text(g_fontGBodyM, 22.0f, tx + tw + 12.0f, ty + 3.0f, Rgb(h.hot || h.active ? kGold : (ghost ? kTxt3 : kTxt)), buf);
    return h.deactivated;
}

} // namespace

void DrawUiGlossyGames(GlossyGamesModel& gm, int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    ImGuiIO& io = ImGui::GetIO();
    char buf[256];
    gm.reqSave = gm.reqDelete = gm.reqSelect = gm.reqEditName = gm.reqSaveExplicit = gm.reqDeleteAll = gm.reqDeleteCam = false;

    HeaderAndTabs(activeTab, false);

    // --- liste filtree -------------------------------------------------------
    static std::vector<int> s_list;
    s_list.clear();
    for (int i = 0; i < gm.gameCount; i++)
        if (gm.filter == 0 || gm.games[i].hasProfile) s_list.push_back(i);
    const int n = (int)s_list.size();
    const int rowsTotal = (n + kCols - 1) / kCols;
    const int maxRow = rowsTotal > kRows ? rowsTotal - kRows : 0;

    // --- barre : a qui appartiennent les profils, filtres, pagination ---------
    const GlRect& bar = GL_BarreInfo;
    const float barCy = bar.y + bar.h * 0.5f;
    ImgAt(GA_BAR_INFO, bar);
    Img(GA_ICON_MS, bar.x + 20.0f, barCy - AssetH(GA_ICON_MS) * 0.5f);
    sprintf(buf, Tr(STR_GAMES_PROFILES_FOR_FMT), gm.mouseName ? gm.mouseName : Tr(STR_GLOSSY_NONE));
    Text(g_fontGBody, 24.0f, bar.x + 52.0f, barCy, Rgb(kTxt2), buf, AL_L, 420.0f);
    {
        const StrId fl[2] = { STR_GAMES_FILTER_ALL, STR_GAMES_FILTER_PROFILE };
        float fx = 520.0f;
        for (int k = 0; k < 2; k++) {
            const float w = TextW(g_fontGBodyM, 24.0f, Tr(fl[k])) + 28.0f;
            sprintf(buf, "##gm_filter%d", k);
            Hit h = HitRect(buf, fx, bar.y + 8.0f, w, bar.h - 16.0f);
            const bool on = gm.filter == k;
            if (on || h.hot) {
                s_dl->AddRectFilled(P(fx, bar.y + 8.0f), P(fx + w, bar.y + bar.h - 8.0f), Rgb(kGold, on ? 40 : 20), 4.0f * s_k);
                s_dl->AddRect(P(fx, bar.y + 8.0f), P(fx + w, bar.y + bar.h - 8.0f), Rgb(kGold, on ? 220 : 120), 4.0f * s_k, ImDrawCornerFlags_All, 1.2f);
            }
            Text(on ? g_fontGBodyM : g_fontGBody, 24.0f, fx + w * 0.5f, barCy, Rgb(on ? kGold : kTxt), Tr(fl[k]), AL_M);
            if (h.pressed && gm.filter != k) { gm.filter = k; gm.scrollRow = 0; }
            fx += w + 10.0f;
        }
    }
    // compteur + "Supprimer tous les profils" (double confirmation)
    {
        int profiles = 0;
        for (int i = 0; i < gm.gameCount; i++) if (gm.games[i].hasProfile) profiles++;
        sprintf(buf, Tr(profiles <= 1 ? STR_GAMES_COUNT1_FMT : STR_GAMES_COUNT_FMT), gm.gameCount, profiles);
        Text(g_fontGSmall, 20.0f, 730.0f, barCy, Rgb(kTxt2), buf);
        if (profiles > 0) {
            static double s_allConfirm = -1.0;
            const double now = ImGui::GetTime();
            const bool confirming = s_allConfirm > now;
            const char* lbl = confirming ? Tr(STR_GLOSSY_CONFIRM) : Tr(STR_GAMES_DELETE_ALL);
            const float lx = 730.0f + TextW(g_fontGSmall, 20.0f, buf) + 22.0f, lw = TextW(g_fontGSmall, 20.0f, lbl);
            Hit h = HitRect("##gm_delall", lx - 4.0f, barCy - 12.0f, lw + 8.0f, 24.0f);
            Text(g_fontGSmall, 20.0f, lx, barCy, Rgb(confirming ? 0xF08A80 : (h.hot ? 0xF08A80 : kTxt3)), lbl);
            s_dl->AddLine(P(lx, barCy + 11.0f), P(lx + lw, barCy + 11.0f), Rgb(0xF08A80, confirming || h.hot ? 200 : 70), 1.0f);
            if (h.pressed) {
                if (confirming) { gm.reqDeleteAll = true; s_allConfirm = -1.0; }
                else s_allConfirm = now + 3.0;
            }
        }
    }
    // pagination : "11-20 / 57" + fleches
    if (gm.scrollRow > maxRow) gm.scrollRow = maxRow;
    if (gm.scrollRow < 0) gm.scrollRow = 0;
    {
        const int a = n ? gm.scrollRow * kCols + 1 : 0;
        int b = (gm.scrollRow + kRows) * kCols; if (b > n) b = n;
        sprintf(buf, "%d\xE2\x80\x93%d / %d", a, b, n);
        const float right = bar.x + bar.w - 20.0f;
        Hit hn = HitRect("##gm_next", right - 30.0f, bar.y + 6.0f, 30.0f, bar.h - 12.0f);
        Hit hp = HitRect("##gm_prev", right - 66.0f, bar.y + 6.0f, 30.0f, bar.h - 12.0f);
        Text(g_fontGTitle, 34.0f, right - 15.0f, barCy - 2.0f, Rgb(gm.scrollRow < maxRow ? (hn.hot ? kGold : kTxt) : 0x3A4A5E), "\xE2\x80\xBA", AL_M);
        Text(g_fontGTitle, 34.0f, right - 51.0f, barCy - 2.0f, Rgb(gm.scrollRow > 0 ? (hp.hot ? kGold : kTxt) : 0x3A4A5E), "\xE2\x80\xB9", AL_M);
        Text(g_fontGBody, 24.0f, right - 80.0f, barCy, Rgb(kTxt2), buf, AL_R);
        if (hn.pressed) gm.scrollRow += kRows;
        if (hp.pressed) gm.scrollRow -= kRows;
    }
    // molette sur la grille = une rangee
    if (io.MousePos.x < 800.0f * s_k && io.MousePos.y > 150.0f * s_k && io.MouseWheel != 0.0f)
        gm.scrollRow += io.MouseWheel < 0.0f ? 1 : -1;
    if (gm.scrollRow > maxRow) gm.scrollRow = maxRow;
    if (gm.scrollRow < 0) gm.scrollRow = 0;

    // --- grille des pochettes -------------------------------------------------
    gm.firstVisible = gm.gameCount; gm.lastVisible = -1;
    if (n == 0) {
        TextWrap(g_fontGBody, 25.0f, kGridX + 20.0f, kGridY + 40.0f, 700.0f, Rgb(kTxt2), Tr(STR_GAMES_EMPTY));
    }
    for (int slot = 0; slot < kCols * kRows; slot++) {
        const int li = gm.scrollRow * kCols + slot;
        if (li >= n) break;
        const int gi = s_list[li];
        const GlossyGame& g = gm.games[gi];
        if (gi < gm.firstVisible) gm.firstVisible = gi;
        if (gi > gm.lastVisible) gm.lastVisible = gi;
        const float x = kGridX + 9.0f + (slot % kCols) * kColW, y = kGridY + (slot / kCols) * kRowH;
        sprintf(buf, "##gm_tile%u", g.titleId);
        Hit h = HitRect(buf, x, y, kCovW, kRowH - 12.0f);
        const bool sel = (gi == gm.selected);
        Img((sel || h.hot) ? GA_COVER_FRAME_F : GA_COVER_FRAME, x, y);
        CoverImage(g.cover, g.coverW, g.coverH, x + 3.0f, y + 3.0f, kCovW - 6.0f, kCovH - 6.0f, g.titleId, g.name);
        if (g.hasProfile) {
            // pastille "profil" en haut a droite de la pochette
            s_dl->AddCircleFilled(P(x + kCovW - 14.0f, y + 14.0f), 9.0f * s_k, Rgb(0x10161F, 230));
            s_dl->AddCircleFilled(P(x + kCovW - 14.0f, y + 14.0f), 6.0f * s_k, Rgb(kGold));
        }
        TextWrap(sel ? g_fontGBodyM : g_fontGBody, 20.0f, x, y + kCovH + 6.0f, kCovW,
                 Rgb(sel || h.hot ? kGold : kTxt), g.name);
        if (h.pressed && gm.selected != gi) { gm.selected = gi; gm.reqSelect = true; }
    }

    // --- panneau du jeu selectionne --------------------------------------------
    const float px = 816.0f, py = 150.0f, pw = 420.0f;
    Img(GA_GAMES_PANEL, px, py);
    if (gm.selected < 0 || gm.selected >= gm.gameCount) {
        TextWrap(g_fontGBody, 24.0f, px + 24.0f, py + 30.0f, pw - 48.0f, Rgb(kTxt2), Tr(STR_GAMES_PICK));
        if (!gm.auroraFound)
            TextWrap(g_fontGSmall, 20.0f, px + 24.0f, py + 120.0f, pw - 48.0f, Rgb(kTxt3), Tr(STR_GAMES_NO_AURORA));
        return;
    }
    const GlossyGame& g = gm.games[gm.selected];
    const float cx = px + 16.0f, cy = py + 16.0f, cw = 118.0f, ch = 160.0f;   // reduite le 2026-09-25 : place pour l'anti-zone morte
    CoverImage(gm.bigCover ? gm.bigCover : g.cover, gm.bigCover ? gm.bigW : g.coverW, gm.bigCover ? gm.bigH : g.coverH,
               cx, cy, cw, ch, g.titleId, g.name);
    s_dl->AddRect(P(cx, cy), P(cx + cw, cy + ch), Rgb(0x4C6079), 2.0f * s_k, ImDrawCornerFlags_All, 1.0f);
    float ty = cy + 2.0f;
    {
        // Nom : cliquable pour le renommer (souligne au survol) ; en saisie,
        // le texte tape s'affiche en or avec un curseur.
        const float nx = cx + cw + 16.0f, nw = pw - cw - 48.0f;
        if (gm.nameEditing && gm.nameBuf) {
            sprintf(buf, "%s_", gm.nameBuf);
            const float hgt = TextWrap(g_fontGTitle, 30.0f, nx, ty, nw, Rgb(kGold), buf);
            s_dl->AddLine(P(nx, ty + hgt + 2.0f), P(nx + nw, ty + hgt + 2.0f), Rgb(kGold, 200), 1.0f);
            ty += hgt + 8.0f;
            ty += TextWrap(g_fontGSmall, 18.0f, nx, ty, nw, Rgb(kTxt2), Tr(STR_GAMES_RENAME_EDIT)) + 4.0f;
        } else {
            const ImVec2 sz = F(g_fontGTitle)->CalcTextSizeA(30.0f * s_k, FLT_MAX, nw * s_k, g.name);
            const float hgt = sz.y / s_k;
            Hit h = HitRect("##gm_name", nx, ty, nw, hgt);
            TextWrap(g_fontGTitle, 30.0f, nx, ty, nw, Rgb(h.hot ? kGold : 0xF2F5F8), g.name);
            if (h.hot) {
                s_dl->AddLine(P(nx, ty + hgt + 1.0f), P(nx + sz.x / s_k, ty + hgt + 1.0f), Rgb(kGold, 160), 1.0f);
                ImGui::SetTooltip("%s", Tr(STR_GAMES_RENAME_HINT));
            }
            if (h.pressed) gm.reqEditName = true;
            ty += hgt + 6.0f;
        }
    }
    char tid[16];
    sprintf(tid, "%08X", g.titleId);
    sprintf(buf, Tr(STR_PROFILES_TITLEID_DISPLAY_FMT), tid);
    Text(g_fontGSmall, 19.0f, cx + cw + 16.0f, ty + 8.0f, Rgb(kTxt3), buf);
    ty += 26.0f;
    Img(g.hasProfile ? GA_DOT_ON : GA_DOT_OFF, cx + cw + 16.0f, ty + 3.0f);
    const float stH = TextWrap(g_fontGBodyM, 20.0f, cx + cw + 36.0f, ty, pw - cw - 70.0f, Rgb(g.hasProfile ? 0x9BE36A : kTxt2),
             Tr(g.hasProfile ? STR_GAMES_HAS_PROFILE : STR_GAMES_NO_PROFILE));

    // Disposition des sticks choisie dans le jeu (2026-09-25, voir StickLayout
    // dans mapping.h). Clic = suivante, clic droit = precedente.
    if (gm.stickLayout) {
        const float sx = cx + cw + 16.0f, sw = pw - cw - 52.0f;
        float sy = ty + stH + 14.0f;
        Text(g_fontGSmall, 19.0f, sx, sy + 8.0f, Rgb(kTxt3), Tr(STR_GAMES_STICKS));
        sy += 22.0f;
        int v = *gm.stickLayout;
        if (v < 0 || v > 3) v = 0;
        const StrId names[4] = { STR_STICKS_DEFAULT, STR_STICKS_SOUTHPAW, STR_STICKS_LEGACY, STR_STICKS_LEGACY_SP };
        sprintf(buf, "%s  \xE2\x80\xBA", Tr(names[v]));
        const float bw = TextW(g_fontGBody, 21.0f, buf) + 22.0f;
        Hit h = HitRect("##gm_sticks", sx, sy, bw < sw ? bw : sw, 30.0f);
        const bool isStd = (v == 0);
        s_dl->AddRect(P(sx, sy), P(sx + (bw < sw ? bw : sw), sy + 30.0f), Rgb(isStd ? 0x4C6079 : kGold, h.hot ? 230 : 170),
                      4.0f * s_k, ImDrawCornerFlags_All, 1.2f);
        Text(g_fontGBody, 21.0f, sx + 11.0f, sy + 15.0f, Rgb(h.hot ? kGold : (isStd ? kTxt : kGold)), buf, AL_L, sw - 14.0f);
        if (h.hot) ImGui::SetTooltip("%s", Tr(STR_GAMES_STICKS_TIP));
        if (h.pressed) { *gm.stickLayout = (v + 1) % 4; gm.reqSave = true; }
        if (h.rclick)  { *gm.stickLayout = (v + 3) % 4; gm.reqSave = true; }
        // Learned camera: status, two clicks delete it.
        if (gm.camState) {
            static double s_camConfirm = -1.0;
            const double now = ImGui::GetTime();
            const bool confirming = s_camConfirm > now;
            const char* lbl = confirming ? Tr(STR_GLOSSY_CONFIRM) : Tr(gm.camState == 2 ? STR_GAMES_CAM_BOTH : STR_GAMES_CAM_H);
            const float cwid = TextW(g_fontGSmall, 19.0f, lbl) + 20.0f, cxr = sx + sw - cwid;
            Hit c = HitRect("##gm_cam", cxr, sy, cwid, 30.0f);
            const unsigned col = confirming || c.hot ? 0xF08A80 : 0x9BE36A;
            s_dl->AddRect(P(cxr, sy), P(cxr + cwid, sy + 30.0f), Rgb(col, 170), 4.0f * s_k, ImDrawCornerFlags_All, 1.2f);
            Text(g_fontGSmall, 19.0f, cxr + cwid * 0.5f, sy + 15.0f, Rgb(col), lbl, AL_M, cwid - 8.0f);
            if (c.hot) ImGui::SetTooltip("%s", Tr(STR_GAMES_CAM_TIP));
            if (c.pressed) {
                if (confirming) { gm.reqDeleteCam = true; s_camConfirm = -1.0; }
                else s_camConfirm = now + 3.0;
            }
        }
    }

    // reglages (2026-09-26) : sensibilite (+ inversion Y a droite), zone morte,
    // anti-zone morte H | V, lissage, courbe (1 = lineaire, < 1 = petits gestes
    // amplifies pour compenser la courbe du jeu, > 1 = l'inverse).
    float ry = cy + ch + 20.0f;
    const float rx = px + 20.0f;
    const float pitch = 48.0f;
    if (gm.sensitivity) {
        float v = (float)*gm.sensitivity;
        if (SliderRow("##gm_sens", Tr(STR_SETTINGS_SENSITIVITY_LABEL), rx, ry, &v, 1000.0f, 30000.0f, 50.0f, "%.0f")) gm.reqSave = true;
        *gm.sensitivity = (int)v;
    }
    if (gm.invertY) {
        const char* lbl = Tr(STR_GLOSSY_INVERT);
        const float right = px + pw - 20.0f, lw = TextW(g_fontGSmall, 20.0f, lbl), bx = right - lw - 24.0f, ccy = ry;
        Hit h = HitRect("##gm_inv", bx - 4.0f, ccy - 12.0f, lw + 32.0f, 24.0f);
        s_dl->AddRect(P(bx, ccy - 8.0f), P(bx + 16.0f, ccy + 8.0f), Rgb(h.hot ? kGold : 0x8FA2B8), 3.0f * s_k, ImDrawCornerFlags_All, 1.5f);
        if (*gm.invertY) s_dl->AddRectFilled(P(bx + 4.0f, ccy - 4.0f), P(bx + 12.0f, ccy + 4.0f), Rgb(kGold), 2.0f * s_k);
        Text(g_fontGSmall, 20.0f, right, ccy, Rgb(h.hot ? kGold : kTxt2), lbl, AL_R);
        if (h.pressed) { *gm.invertY = !*gm.invertY; gm.reqSave = true; }
    }
    ry += pitch;
    if (gm.deadzone) {
        float v = (float)*gm.deadzone;
        if (SliderRow("##gm_dz", Tr(STR_SETTINGS_DEADZONE_LABEL), rx, ry, &v, 0.0f, 10000.0f, 50.0f, "%.0f")) gm.reqSave = true;
        *gm.deadzone = (int)v;
        ry += pitch;
    }
    if (gm.antiDeadzone) {
        Text(g_fontGBody, 24.0f, rx, ry, Rgb(0xF2F5F8), Tr(STR_SETTINGS_ANTIDZ_LABEL));
        const float hw = 108.0f;
        float v = (float)*gm.antiDeadzone;
        if (SliderRowW("##gm_adz", "H", rx, ry, hw, &v, 0.0f, 12000.0f, 50.0f, "%.0f", false)) gm.reqSave = true;
        *gm.antiDeadzone = (int)v;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(STR_SETTINGS_ANTIDZ_HINT));
        if (gm.antiDeadzoneY) {
            const bool follows = (*gm.antiDeadzoneY == 0);
            float vy = follows ? (float)*gm.antiDeadzone : (float)*gm.antiDeadzoneY;
            const float before = vy;
            if (SliderRowW("##gm_adzy", "V", rx + hw + 84.0f, ry, hw, &vy, 0.0f, 12000.0f, 50.0f, "%.0f", follows)) gm.reqSave = true;
            if (vy != before || !follows) *gm.antiDeadzoneY = (int)vy;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(STR_SETTINGS_ANTIDZ_Y_HINT));
        }
        ry += pitch;
    }
    if (gm.smoothing) {
        float v = (float)*gm.smoothing;
        if (SliderRow("##gm_sm", Tr(STR_SETTINGS_SMOOTH_LABEL), rx, ry, &v, 0.0f, 100.0f, 1.0f, "%.0f ms")) gm.reqSave = true;
        *gm.smoothing = (int)v;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(STR_SETTINGS_SMOOTH_HINT));
        ry += pitch;
    }
    if (gm.curveType && gm.curveExp) {
        float v = (*gm.curveType == 1) ? *gm.curveExp : 1.0f;
        if (SliderRow("##gm_curve", Tr(STR_GAMES_CURVE_LABEL), rx, ry, &v, 0.3f, 3.0f, 0.05f, "%.2f")) gm.reqSave = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tr(STR_GAMES_CURVE_HINT));
        if (v > 0.97f && v < 1.03f) { *gm.curveType = 0; *gm.curveExp = 1.0f; }
        else { *gm.curveType = 1; *gm.curveExp = v; }
    }

    // boutons : Enregistrer (explicite) + Supprimer le profil (si le jeu en a un)
    const float by = py + 492.0f - 56.0f;
    if (PgButton("##gm_save", rx, by, false, Tr(STR_GAMES_SAVE), true)) { gm.reqSave = true; gm.reqSaveExplicit = true; }
    // retour discret : "Enregistre" pendant 2 secondes
    if (gm.savedAt >= 0.0 && ImGui::GetTime() - gm.savedAt < 2.0) {
        Text(g_fontGSmall, 20.0f, rx + 117.0f, by - 14.0f, Rgb(gm.savedOk ? 0x9BE36A : 0xF08A80),
             Tr(gm.savedOk ? STR_GAMES_SAVED : STR_CALIB_SAVE_FAILED), AL_M);
    }
    if (g.hasProfile) {
        static double s_delConfirm = -1.0;
        const double now = ImGui::GetTime();
        const bool confirming = s_delConfirm > now;
        const char* lbl = confirming ? Tr(STR_GLOSSY_CONFIRM) : Tr(STR_GAMES_DELETE);
        const float bx = rx + 246.0f, bw = px + pw - 20.0f - bx, bh = 40.0f;
        Hit h = HitRect("##gm_del", bx, by, bw, bh);
        const unsigned col = confirming || h.hot ? 0xF08A80 : 0xB85A55;
        s_dl->AddRectFilled(P(bx, by), P(bx + bw, by + bh), Rgb(0x3A1614, confirming ? 230 : 170), 5.0f * s_k);
        s_dl->AddRect(P(bx, by), P(bx + bw, by + bh), Rgb(col), 5.0f * s_k, ImDrawCornerFlags_All, 1.4f);
        Text(g_fontGBody, 22.0f, bx + bw * 0.5f, by + bh * 0.5f, Rgb(col), lbl, AL_M, bw - 10.0f);
        if (h.pressed) {
            if (confirming) { gm.reqDelete = true; s_delConfirm = -1.0; }
            else s_delConfirm = now + 3.0;
        }
    }
}

// ============================================================================
// PAGE COMMENT CA MARCHE (2026-09-25)
// ============================================================================
namespace {

void ImgScaled(int id, float x, float y, float w, float h) {
    if (!Has(id)) return;
    s_dl->AddImage(s_tex[id].tex, P(x, y), P(x + w, y + h));
}

void Card(float x, float y, float w, float h) {
    s_dl->AddRectFilled(P(x, y), P(x + w, y + h), Rgb(0x121C29, 225), 6.0f * s_k);
    s_dl->AddRect(P(x, y), P(x + w, y + h), Rgb(0x3A4C63, 200), 6.0f * s_k, ImDrawCornerFlags_All, 1.0f);
    s_dl->AddLine(P(x + 12.0f, y + 1.0f), P(x + w - 12.0f, y + 1.0f), Rgb(0x9FC8F0, 60), 1.0f);
}

// Pastille numerotee doree.
void StepNumber(float cx, float cy, int n) {
    s_dl->AddCircleFilled(P(cx, cy), 19.0f * s_k, Rgb(0x2A2410), 24);
    s_dl->AddCircle(P(cx, cy), 19.0f * s_k, Rgb(kGold), 24, 1.6f);
    char b[4]; sprintf(b, "%d", n);
    Text(g_fontGBodyM, 28.0f, cx, cy, Rgb(kGold), b, AL_M);
}

} // namespace

void DrawUiGlossyHowTo(int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    HeaderAndTabs(activeTab, false);

    const GlRect& bar = GL_BarreInfo;
    ImgAt(GA_BAR_INFO, bar);
    Text(g_fontGBody, 24.0f, bar.x + bar.w * 0.5f, bar.y + bar.h * 0.5f, Rgb(kTxt), Tr(STR_HT_INTRO), AL_M, bar.w - 40.0f);

    // --- quatre etapes --------------------------------------------------------
    const StrId titles[4] = { STR_HT_S1_T, STR_HT_S2_T, STR_HT_S3_T, STR_HT_S4_T };
    const StrId texts[4]  = { STR_HT_S1, STR_HT_S2, STR_HT_S3, STR_HT_S4 };
    for (int i = 0; i < 4; i++) {
        const float x = 44.0f, y = 156.0f + i * 121.0f, w = 752.0f, h = 112.0f;
        Card(x, y, w, h);
        StepNumber(x + 36.0f, y + h * 0.5f, i + 1);
        // illustration
        const float ix = x + 70.0f, icy = y + h * 0.5f;
        if (i == 0) {
            Img(GA_ICON_KB, ix, icy - AssetH(GA_ICON_KB) * 0.5f);
            Img(GA_ICON_MS, ix + 46.0f, icy - AssetH(GA_ICON_MS) * 0.5f);
        } else if (i == 1) {
            Img(GA_K_W, ix, icy - 30.0f);
            Img(GA_K_A, ix + 34.0f, icy + 2.0f);
            Img(GA_KP_A_S, ix + 40.0f, icy - 28.0f);
        } else if (i == 2) {
            ImgScaled(GA_PG_MEASURE, ix, icy - 24.0f, 66.0f, 48.0f);
        } else {
            // mini pochette
            s_dl->AddRectFilledMultiColor(P(ix + 12.0f, icy - 30.0f), P(ix + 56.0f, icy + 30.0f),
                IM_COL32(60, 120, 60, 255), IM_COL32(60, 120, 60, 255), IM_COL32(20, 40, 25, 255), IM_COL32(20, 40, 25, 255));
            s_dl->AddRect(P(ix + 12.0f, icy - 30.0f), P(ix + 56.0f, icy + 30.0f), Rgb(kGold, 200), 3.0f * s_k, ImDrawCornerFlags_All, 1.2f);
            s_dl->AddCircleFilled(P(ix + 50.0f, icy - 24.0f), 5.0f * s_k, Rgb(kGold));
        }
        const float tx = x + 160.0f, tw = w - 180.0f;
        Text(g_fontGTitle, 30.0f, tx, y + 26.0f, Rgb(0xF2F5F8), Tr(titles[i]));
        TextWrap(g_fontGBody, 22.0f, tx, y + 46.0f, tw, Rgb(kTxt2), Tr(texts[i]));
    }

    // --- en jeu + depannage (colonne droite) -----------------------------------
    const float px = 816.0f, py = 150.0f, pw = 420.0f;
    Img(GA_GAMES_PANEL, px, py);
    float y = py + 20.0f;
    const float x = px + 20.0f, w = pw - 40.0f;
    Text(g_fontGTitle, 30.0f, x, y + 10.0f, Rgb(kGold), Tr(STR_HT_INGAME));
    y += 36.0f;
    // liaison rapide : BACK + START
    Img(GA_KP_BACK_S, x, y);
    Text(g_fontGBodyM, 24.0f, x + AssetW(GA_KP_BACK_S) + 12.0f, y + AssetH(GA_KP_BACK_S) * 0.5f, Rgb(kTxt), "+", AL_M);
    Img(GA_KP_START_S, x + AssetW(GA_KP_BACK_S) + 24.0f, y);
    Text(g_fontGBodyM, 22.0f, x + AssetW(GA_KP_BACK_S) + AssetW(GA_KP_START_S) + 36.0f, y + AssetH(GA_KP_BACK_S) * 0.5f,
         Rgb(0xF2F5F8), Tr(STR_HOWTO_QUICKBIND_HEADER), AL_L, w - AssetW(GA_KP_BACK_S) - AssetW(GA_KP_START_S) - 36.0f);
    y += AssetH(GA_KP_BACK_S) + 8.0f;
    y += TextWrap(g_fontGBody, 19.0f, x, y, w, Rgb(kTxt2), Tr(STR_HOWTO_QUICKBIND_TEXT)) + 10.0f;
    // F9
    Img(GA_K_F9, x, y);
    Text(g_fontGBodyM, 22.0f, x + AssetW(GA_K_F9) + 12.0f, y + AssetH(GA_K_F9) * 0.5f, Rgb(0xF2F5F8), Tr(STR_HOWTO_F9_HEADER), AL_L,
         w - AssetW(GA_K_F9) - 12.0f);
    y += AssetH(GA_K_F9) + 8.0f;
    y += TextWrap(g_fontGBody, 19.0f, x, y, w, Rgb(kTxt2), Tr(STR_HOWTO_F9_TEXT)) + 8.0f;
    // reglages en jeu F5..F11 (2026-09-25) : six touches en ligne + une phrase
    {
        // Titre, puis les touches F1..F8, F10, F11 (2026-09-26 : F1-F4 ajoutees)
        Text(g_fontGBodyM, 22.0f, x, y + 10.0f, Rgb(0xF2F5F8), Tr(STR_HT_LIVE_T), AL_L, w);
        y += 24.0f;
        const int keys[10] = { GA_K_F1, GA_K_F2, GA_K_F3, GA_K_F4, GA_K_F5, GA_K_F6, GA_K_F7, GA_K_F8, GA_K_F10, GA_K_F11 };
        float kx = x;
        for (int i = 0; i < 10; i++) {
            Img(keys[i], kx, y);
            kx += AssetW(keys[i]) + ((i % 2) ? 8.0f : 2.0f);
        }
        y += AssetH(GA_K_F5) + 8.0f;
        y += TextWrap(g_fontGBody, 19.0f, x, y, w, Rgb(kTxt2), Tr(STR_HT_LIVE)) + 8.0f;
    }
    // depannage
    s_dl->AddLine(P(x, y), P(x + w, y), Rgb(0x2C3B4F), 1.0f);
    y += 10.0f;
    Img(GA_PG_GEAR, x, y - 2.0f);
    Text(g_fontGBodyM, 22.0f, x + 44.0f, y + 16.0f, Rgb(0xF2F5F8), Tr(STR_HT_HELP_T));
    y += 42.0f;
    TextWrap(g_fontGSmall, 18.0f, x, y, w, Rgb(kTxt3), Tr(STR_HT_HELP));
}

// ============================================================================
// GUIDE (Windows "How it works", 2026-11-18): same frame as the page above, the
// content comes from the caller (two guides, one step per page).
// ============================================================================
void DrawUiGlossyGuide(GlossyGuideModel& gm, int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    HeaderAndTabs(activeTab, false);
    if (gm.guide < 0 || gm.guide > 1) gm.guide = 0;
    const int count = gm.stepCount[gm.guide];
    if (gm.step >= count) gm.step = count - 1;
    if (gm.step < 0) gm.step = 0;
    char buf[64];

    // --- bar: the two guides, then "3 / 8" and the arrows ----------------------
    const GlRect& bar = GL_BarreInfo;
    ImgAt(GA_BAR_INFO, bar);
    const float barCy = bar.y + bar.h * 0.5f;
    {
        float fx = bar.x + 20.0f;
        for (int k = 0; k < 2; k++) {
            const float w = TextW(g_fontGBodyM, 24.0f, gm.guideName[k]) + 28.0f;
            sprintf(buf, "##guide_pick%d", k);
            Hit h = HitRect(buf, fx, bar.y + 8.0f, w, bar.h - 16.0f);
            const bool on = gm.guide == k;
            if (on || h.hot) {
                s_dl->AddRectFilled(P(fx, bar.y + 8.0f), P(fx + w, bar.y + bar.h - 8.0f), Rgb(kGold, on ? 40 : 20), 4.0f * s_k);
                s_dl->AddRect(P(fx, bar.y + 8.0f), P(fx + w, bar.y + bar.h - 8.0f), Rgb(kGold, on ? 220 : 120), 4.0f * s_k, ImDrawCornerFlags_All, 1.2f);
            }
            Text(on ? g_fontGBodyM : g_fontGBody, 24.0f, fx + w * 0.5f, barCy, Rgb(on ? kGold : kTxt), gm.guideName[k], AL_M);
            if (h.pressed && !on) { gm.guide = k; gm.step = 0; }
            fx += w + 10.0f;
        }
        Text(g_fontGSmall, 20.0f, fx + 14.0f, barCy, Rgb(kTxt2), gm.intro[gm.guide], AL_L, bar.x + bar.w - 150.0f - fx);
    }
    const int n = gm.stepCount[gm.guide];
    bool goPrev = false, goNext = false;
    {
        const float right = bar.x + bar.w - 20.0f;
        Hit hn = HitRect("##guide_next", right - 30.0f, bar.y + 6.0f, 30.0f, bar.h - 12.0f);
        Hit hp = HitRect("##guide_prev", right - 66.0f, bar.y + 6.0f, 30.0f, bar.h - 12.0f);
        Text(g_fontGTitle, 34.0f, right - 15.0f, barCy - 2.0f, Rgb(gm.step < n - 1 ? (hn.hot ? kGold : kTxt) : 0x3A4A5E), "\xE2\x80\xBA", AL_M);
        Text(g_fontGTitle, 34.0f, right - 51.0f, barCy - 2.0f, Rgb(gm.step > 0 ? (hp.hot ? kGold : kTxt) : 0x3A4A5E), "\xE2\x80\xB9", AL_M);
        sprintf(buf, "%d / %d", gm.step + 1, n);
        Text(g_fontGBody, 24.0f, right - 80.0f, barCy, Rgb(kTxt2), buf, AL_R);
        goNext = hn.pressed;
        goPrev = hp.pressed;
    }
    if (n <= 0) return;
    const GlossyGuideStep& st = gm.steps[gm.guide][gm.step];

    // --- the step: number, title, text -----------------------------------------
    const float x = 44.0f, y = 156.0f, w = 752.0f, h = 430.0f;
    Card(x, y, w, h);
    StepNumber(x + 40.0f, y + 44.0f, gm.step + 1);
    Text(g_fontGTitle, 36.0f, x + 76.0f, y + 44.0f, Rgb(0xF2F5F8), st.title, AL_L, w - 96.0f);
    s_dl->AddLine(P(x + 24.0f, y + 78.0f), P(x + w - 24.0f, y + 78.0f), Rgb(0x2C3B4F), 1.0f);
    {   // one paragraph per line of the text, a gold dot in front of each
        float ty = y + 96.0f;
        const char* a = st.text;
        while (a && *a) {
            const char* e = strchr(a, '\n');
            std::string para = e ? std::string(a, e) : std::string(a);
            s_dl->AddCircleFilled(P(x + 34.0f, ty + 15.0f), 3.5f * s_k, Rgb(kGold), 12);
            ty += TextWrap(g_fontGBody, 26.0f, x + 48.0f, ty, w - 76.0f, Rgb(kTxt), para.c_str()) + 16.0f;
            a = e ? e + 1 : nullptr;
        }
    }

    // --- picture on the right ----------------------------------------------------
    const float px = 816.0f, py = 150.0f, pw = 420.0f, ph = 442.0f;
    Img(GA_GAMES_PANEL, px, py);
    {
        int ids[4], cnt = 0;
        for (int i = 0; i < 4; i++) if (st.art[i] >= 0 && st.art[i] < GA_COUNT) ids[cnt++] = st.art[i];
        const float cx = px + pw * 0.5f, cy = py + ph * 0.5f;
        if (cnt == 1 && AssetW(ids[0]) > 200) {   // one big picture: fit the panel
            const float aw = (float)AssetW(ids[0]), ah = (float)AssetH(ids[0]);
            float sc = (pw - 40.0f) / aw;
            if (ah * sc > ph - 40.0f) sc = (ph - 40.0f) / ah;
            ImgScaled(ids[0], cx - aw * sc * 0.5f, cy - ah * sc * 0.5f, aw * sc, ah * sc);
        } else if (cnt > 0) {   // small pictures (keys, icons) side by side, twice their size
            float total = 0.0f;
            for (int i = 0; i < cnt; i++) total += AssetW(ids[i]) * 2.0f + (i ? 16.0f : 0.0f);
            float ix = cx - total * 0.5f;
            for (int i = 0; i < cnt; i++) {
                const float aw = AssetW(ids[i]) * 2.0f, ah = AssetH(ids[i]) * 2.0f;
                ImgScaled(ids[i], ix, cy - ah * 0.5f, aw, ah);
                ix += aw + 16.0f;
            }
        }
    }

    // --- bottom: previous / dots / next ------------------------------------------
    const float by = 604.0f;
    if (gm.step > 0 && PgButton("##guide_prevbtn", x, by, false, gm.prevLabel)) goPrev = true;
    if (gm.step < n - 1 && PgButton("##guide_nextbtn", x + w - 234.0f, by, false, gm.nextLabel, true)) goNext = true;
    {
        const float dx = 22.0f, d0 = x + w * 0.5f - dx * (n - 1) * 0.5f;
        for (int i = 0; i < n; i++) {
            sprintf(buf, "##guide_dot%d", i);
            Hit hd = HitRect(buf, d0 + dx * i - 9.0f, by + 11.0f, 18.0f, 18.0f);
            const bool on = i == gm.step;
            s_dl->AddCircleFilled(P(d0 + dx * i, by + 20.0f), (on ? 6.0f : 4.5f) * s_k, Rgb(on ? kGold : (hd.hot ? kTxt : 0x3A4A5E)), 16);
            if (hd.pressed) gm.step = i;
        }
    }
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow), false)) goPrev = true;
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow), false)) goNext = true;
    if (goPrev && gm.step > 0) gm.step--;
    if (goNext && gm.step < n - 1) gm.step++;
}

// ============================================================================
// PAGE SOUTENIR (2026-09-25)
//
// Mises a jour : la console NE PEUT PAS interroger GitHub elle-meme. GitHub
// n'accepte que des connexions chiffrees modernes (TLS 1.2+), que le systeme
// de la Xbox 360 ne sait pas etablir. La page affiche donc la version
// installee et un QR code vers la page des versions : le telephone fait la
// verification. Une vraie verification automatique demanderait d'embarquer
// une bibliotheque TLS complete dans le .xex (suivi interne).
// ============================================================================
namespace {
void LinkRow(float x, float y, float w, const char* label, const char* value) {
    if (!value) return;
    Text(g_fontGBodyM, 22.0f, x, y, Rgb(0xF2F5F8), label, AL_L, 150.0f);
    Text(g_fontGBody, 22.0f, x + 160.0f, y, Rgb(kTxt2), value, AL_L, w - 160.0f);
}
} // namespace

void DrawUiGlossySupport(const GlossySupportModel& sm, int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    char buf[256];
    HeaderAndTabs(activeTab, false);

    const GlRect& bar = GL_BarreInfo;
    ImgAt(GA_BAR_INFO, bar);
    Text(g_fontGBody, 24.0f, bar.x + bar.w * 0.5f, bar.y + bar.h * 0.5f, Rgb(kTxt), Tr(STR_SUPPORT_HEADLINE), AL_M, bar.w - 40.0f);

    // --- Mises a jour (gauche) -------------------------------------------------
    {
        const float x = 44.0f, y = 156.0f, w = 560.0f, h = 486.0f;
        Card(x, y, w, h);
        Text(g_fontGTitle, 32.0f, x + 24.0f, y + 32.0f, Rgb(kGold), Tr(STR_SUP_UPDATES_T));
        Text(g_fontGSmall, 20.0f, x + 24.0f, y + 74.0f, Rgb(kTxt3), Tr(STR_SUP_CURRENT));
        sprintf(buf, "v%s", sm.version ? sm.version : "?");
        Text(g_fontGTitle, 64.0f, x + 24.0f, y + 116.0f, Rgb(0xF2F5F8), buf);
        Img(sm.pluginAlive ? GA_DOT_ON : GA_DOT_RED, x + 26.0f, y + 162.0f);
        Text(g_fontGBody, 21.0f, x + 44.0f, y + 167.0f, Rgb(kTxt2), Tr(sm.pluginAlive ? STR_UI_PLUGIN_ALIVE : STR_UI_PLUGIN_SILENT));

        // QR vers les versions + explication
        const float qx = x + w - 24.0f - 150.0f, qy = y + 40.0f;
        s_dl->AddRectFilled(P(qx - 8.0f, qy - 8.0f), P(qx + 158.0f, qy + 158.0f), IM_COL32(255, 255, 255, 255), 6.0f * s_k);
        Img(GA_QR_RELEASES, qx, qy);
        Text(g_fontGSmall, 18.0f, qx + 75.0f, qy + 172.0f, Rgb(kTxt3), Tr(STR_SUPPORT_QR_HINT), AL_M, 190.0f);

        float ty = y + 230.0f;
        ty += TextWrap(g_fontGBody, 22.0f, x + 24.0f, ty, w - 48.0f, Rgb(kTxt), Tr(STR_SUP_UPDATES_TXT)) + 12.0f;
        if (sm.releasesUrl) {
            Text(g_fontGBodyM, 22.0f, x + 24.0f, ty + 10.0f, Rgb(0x7DB8FF), sm.releasesUrl, AL_L, w - 48.0f);
            ty += 34.0f;
        }
        TextWrap(g_fontGSmall, 18.0f, x + 24.0f, ty, w - 48.0f, Rgb(kTxt3), Tr(STR_SUP_UPDATES_WHY));
    }

    // --- Soutenir le projet (droite) -----------------------------------------
    {
        const float x = 620.0f, y = 156.0f, w = 616.0f, h = 486.0f;
        Card(x, y, w, h);
        Text(g_fontGTitle, 32.0f, x + 24.0f, y + 32.0f, Rgb(kGold), Tr(STR_SUP_SUPPORT_T));
        float ty = y + 60.0f;
        ty += TextWrap(g_fontGBody, 21.0f, x + 24.0f, ty, w - 230.0f, Rgb(kTxt), Tr(STR_SUPPORT_MESSAGE)) + 10.0f;
        TextWrap(g_fontGBodyM, 21.0f, x + 24.0f, ty, w - 230.0f, Rgb(0x9BE36A), Tr(STR_SUPPORT_CTA));

        // QR Ko-fi
        const float qx = x + w - 24.0f - 150.0f, qy = y + 64.0f;
        s_dl->AddRectFilled(P(qx - 8.0f, qy - 8.0f), P(qx + 158.0f, qy + 158.0f), IM_COL32(255, 255, 255, 255), 6.0f * s_k);
        Img(GA_QR_KOFI, qx, qy);
        Text(g_fontGBodyM, 20.0f, qx + 75.0f, qy + 172.0f, Rgb(0xF2F5F8), Tr(STR_SUPPORT_CARD_DONATE), AL_M, 200.0f);

        // liens
        float ly = y + 300.0f;
        s_dl->AddLine(P(x + 24.0f, ly - 20.0f), P(x + w - 24.0f, ly - 20.0f), Rgb(0x2C3B4F), 1.0f);
        const float lw = w - 48.0f;
        LinkRow(x + 24.0f, ly, lw, Tr(STR_SUPPORT_KOFI_LABEL), sm.kofiUrl);        ly += 34.0f;
        LinkRow(x + 24.0f, ly, lw, Tr(STR_SUPPORT_PAYPAL_LABEL), sm.paypal);       ly += 34.0f;
        LinkRow(x + 24.0f, ly, lw, "GitHub", sm.githubUrl);                         ly += 34.0f;
        LinkRow(x + 24.0f, ly, lw, "YouTube", sm.youtubeUrl);                       ly += sm.youtubeUrl ? 40.0f : 6.0f;
        sprintf(buf, "%s  %s", Tr(STR_SUPPORT_CARD_FEEDBACK), sm.paypal ? sm.paypal : "");
        Text(g_fontGSmall, 19.0f, x + 24.0f, ly, Rgb(kTxt3), buf, AL_L, lw);
    }
}


// ============================================================================
// ECRAN DE DEMARRAGE (2026-09-26, idee HB) : le logo apparait, une onde part
// du badge, le nom glisse en place, une barre de chargement se remplit, puis
// tout s'efface sur la page. ~2 s ; une touche ou un clic l'abrege.
// Dessine PAR-DESSUS la page (liste d'avant-plan) : le fondu de sortie la
// revele sans coupure.
// ============================================================================
namespace {
float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float EaseOut(float v) { v = Clamp01(v); return 1.0f - (1.0f - v) * (1.0f - v) * (1.0f - v); }
float EaseOutBack(float v) {
    v = Clamp01(v);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * (v - 1.0f) * (v - 1.0f) * (v - 1.0f) + c1 * (v - 1.0f) * (v - 1.0f);
}
void ImgScaledA(int id, float cx, float cy, float scale, int alpha) {
    if (!Has(id) || alpha <= 0) return;
    const UiSkinTex& t = s_tex[id];
    const float w = (float)t.w * scale, h = (float)t.h * scale;
    s_dl->AddImage(t.tex, P(cx - w * 0.5f, cy - h * 0.5f), P(cx + w * 0.5f, cy + h * 0.5f),
                   ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha));
}
} // namespace

bool DrawUiGlossySplash(float t, float screenW, float screenH, const char* version) {
    const float kTotal = 2.1f;
    if (t >= kTotal) return false;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetForegroundDrawList();
    (void)screenH;

    // opacite generale : entree rapide, sortie en fondu sur la page
    float A = 1.0f;
    if (t > 1.75f) A = 1.0f - Clamp01((t - 1.75f) / (kTotal - 1.75f));
    const int a255 = (int)(A * 255.0f);

    // fond : image de la page, assombrie
    if (Has(GA_BG_PAGE_HALF)) {
        const UiSkinTex& bg = s_tex[GA_BG_PAGE_HALF];
        s_dl->AddImage(bg.tex, P(0, 0), P(1280, 720), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a255));
    }
    s_dl->AddRectFilled(P(0, 0), P(1280, 720), IM_COL32(4, 8, 14, (int)(A * 150.0f)));

    const float bx = 470.0f, by = 330.0f;           // centre du badge
    // onde verte qui part du badge
    {
        const float k = Clamp01((t - 0.55f) / 0.7f);
        if (k > 0.0f && k < 1.0f) {
            const float r = 104.0f + 90.0f * EaseOut(k);
            s_dl->AddCircle(P(bx, by), r * s_k, IM_COL32(123, 234, 42, (int)((1.0f - k) * 170.0f * A)), 64, 3.0f * s_k);
        }
    }
    // badge : grossit avec un leger rebond
    {
        const float k = (t - 0.1f) / 0.6f;
        const float sc = 0.7f + 0.3f * EaseOutBack(k);
        ImgScaledA(GA_SPLASH_BADGE, bx, by, sc, (int)(Clamp01(k * 1.6f) * a255));
    }
    // nom X360 / REMAP : glisse depuis la droite
    float stackBottom = by;
    if (Has(GA_SPLASH_STACK)) {
        const float k = EaseOut((t - 0.45f) / 0.5f);
        const GlossyAssetSpec& a = g_glossyAssets[GA_SPLASH_STACK];
        const float x = 610.0f + 40.0f * (1.0f - k), y = by - a.h * 0.5f - 18.0f;
        Img(GA_SPLASH_STACK, x, y, (int)(k * a255));
        stackBottom = y + a.h;
        // STUDIO : apparait en se resserrant
        const float k2 = EaseOut((t - 0.8f) / 0.45f);
        if (k2 > 0.0f) {
            float sum = 0.0f;
            char one[2] = { 0, 0 };
            for (const char* p = "STUDIO"; *p; p++) { one[0] = *p; sum += TextW(g_fontGTitle, 34.0f, one); }
            const float gapEnd = (a.w - sum) / 5.0f, gap = gapEnd + 18.0f * (1.0f - k2);
            TextSpaced(g_fontGTitle, 34.0f, 610.0f, stackBottom + 26.0f, Rgb(kTxt2, (int)(k2 * a255)), "STUDIO", gap);
        }
    }
    // barre de chargement
    {
        const float w = 420.0f, x = 640.0f - w * 0.5f, y = 488.0f;
        const float k = EaseOut(t / 1.6f);
        s_dl->AddRectFilled(P(x, y), P(x + w, y + 4.0f), IM_COL32(40, 56, 74, (int)(A * 200.0f)), 2.0f * s_k);
        s_dl->AddRectFilledMultiColor(P(x, y), P(x + w * k, y + 4.0f),
            IM_COL32(123, 234, 42, a255), IM_COL32(242, 205, 90, a255),
            IM_COL32(242, 205, 90, a255), IM_COL32(123, 234, 42, a255));
        if (version && version[0])
            Text(g_fontGSmall, 18.0f, 640.0f, y + 26.0f, Rgb(kTxt3, (int)(A * 200.0f)), version, AL_M);
    }
    return true;
}

// Bandeau d'information en bas d'ecran (2026-09-26), par-dessus la page.
// News-style band at the bottom of the screen (2026-10-20, idea HB): the
// plugin is not running, and why. `label` stays on the left, `msg` scrolls.
void DrawUiGlossyTicker(const char* label, const char* msg, float timeSec, float screenW, float screenH) {
    (void)screenH;
    if (!msg || !msg[0]) return;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetForegroundDrawList();
    const float y = 648.0f, h = 40.0f, cy = y + h * 0.5f;   // inside the TV safe area
    s_dl->AddRectFilled(P(0.0f, y), P(1280.0f, y + h), IM_COL32(60, 8, 12, 235));
    s_dl->AddLine(P(0.0f, y), P(1280.0f, y), Rgb(0xF0605A, 230), 2.0f * s_k);
    // Label capsule (pulses gently so the eye finds it)
    const float pulse = 0.75f + 0.25f * sinf(timeSec * 3.3f);
    const float lw = TextW(g_fontGBodyM, 22.0f, label) + 36.0f;
    s_dl->AddRectFilled(P(0.0f, y), P(lw, y + h), Rgb(0xD23A35, (int)(255 * pulse)));
    Text(g_fontGBodyM, 22.0f, 18.0f, cy, Rgb(kWhite), label);
    // Scrolling text, repeated, clipped to the right of the label
    char line[1024];
    size_t n = strlen(msg);
    if (n > sizeof(line) - 32) n = sizeof(line) - 32;
    memcpy(line, msg, n);
    strcpy(line + n, "     \xC2\xB7     ");
    const float tw = TextW(g_fontGBody, 22.0f, line);
    if (tw <= 0.0f) return;
    const float speed = 70.0f;   // canvas pixels per second
    const float off = fmodf(timeSec * speed, tw);
    s_dl->PushClipRect(P(lw + 8.0f, y), P(1280.0f, y + h), true);
    for (float x = lw + 8.0f - off; x < 1280.0f; x += tw)
        Text(g_fontGBody, 22.0f, x, cy, Rgb(kMsg), line);
    s_dl->PopClipRect();
}

void DrawUiGlossyToast(const char* msg, float alpha, float screenW, float screenH) {
    (void)screenH;
    if (!msg || !msg[0] || alpha <= 0.0f) return;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetForegroundDrawList();
    const int a = (int)(alpha * 255.0f);
    const float w = TextW(g_fontGBodyM, 24.0f, msg) + 48.0f, x = 640.0f - w * 0.5f, y = 610.0f, h = 44.0f;
    s_dl->AddRectFilled(P(x, y), P(x + w, y + h), IM_COL32(10, 16, 26, (int)(alpha * 235.0f)), 8.0f * s_k);
    s_dl->AddRect(P(x, y), P(x + w, y + h), Rgb(kGold, (int)(alpha * 220.0f)), 8.0f * s_k, ImDrawCornerFlags_All, 1.5f);
    Text(g_fontGBodyM, 24.0f, 640.0f, y + h * 0.5f, Rgb(kTxt, a), msg, AL_M);
}


// ============================================================================
// PAGE AFFICHAGE EN JEU (2026-11-01) : apercu a gauche, reglages a droite.
// ============================================================================
namespace {

float OvChipW(const char* label) { return TextW(g_fontGBody, 20.0f, label) + 26.0f; }

// Pastille cliquable ; sel = choix actif (dore).
bool OvChip(const char* id, float x, float y, const char* label, bool sel) {
    const float w = OvChipW(label), h = 32.0f;
    Hit hit = HitRect(id, x, y, w, h);
    const bool lit = sel || hit.hot;
    // Glossy pill: soft glow when lit, vertical gradient, bright top edge.
    if (lit) {
        s_dl->AddRect(P(x - 3.0f, y - 3.0f), P(x + w + 3.0f, y + h + 3.0f), Rgb(kGold, 40), 6.0f * s_k, ImDrawCornerFlags_All, 3.0f);
        s_dl->AddRect(P(x - 1.5f, y - 1.5f), P(x + w + 1.5f, y + h + 1.5f), Rgb(kGold, 90), 5.0f * s_k, ImDrawCornerFlags_All, 2.0f);
    }
    s_dl->AddRectFilled(P(x, y), P(x + w, y + h), Rgb(0x0A1424), 4.0f * s_k);
    s_dl->AddRectFilledMultiColor(P(x + 1.0f, y + 1.0f), P(x + w - 1.0f, y + h - 1.0f),
        Rgb(sel ? 0x5A4A18 : 0x2A4C7C), Rgb(sel ? 0x5A4A18 : 0x2A4C7C), Rgb(sel ? 0x1E1808 : 0x0F2142), Rgb(sel ? 0x1E1808 : 0x0F2142));
    s_dl->AddRectFilled(P(x + 3.0f, y + 2.0f), P(x + w - 3.0f, y + h * 0.45f), Rgb(0xFFFFFF, sel ? 26 : 18), 3.0f * s_k);
    s_dl->AddRect(P(x, y), P(x + w, y + h), Rgb(lit ? kGold : 0x3F66A0), 4.0f * s_k, ImDrawCornerFlags_All, sel ? 1.6f : 1.0f);
    Text(g_fontGBody, 20.0f, x + w * 0.5f, y + h * 0.5f, Rgb(lit ? kGold : 0xC8D4E6), label, AL_M, w - 6.0f);
    return hit.pressed;
}

bool OvSwatch(const char* id, float x, float y, unsigned rgb) {
    Hit hit = HitRect(id, x, y, 46.0f, 30.0f);
    s_dl->AddRectFilled(P(x, y), P(x + 46.0f, y + 30.0f), Rgb(rgb), 3.0f * s_k);
    s_dl->AddRect(P(x, y), P(x + 46.0f, y + 30.0f), hit.hot ? Rgb(kGold) : Rgb(0xFFFFFF, 120), 3.0f * s_k, ImDrawCornerFlags_All, hit.hot ? 2.0f : 1.0f);
    return hit.pressed;
}

// 3 x 3 positions ; renvoie la case cliquee (rangee * 3 + colonne) ou -1.
int OvGrid(const char* id, float x, float y, int selCol, int selRow) {
    int clicked = -1;
    char b[32];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            const float gx = x + c * 26.0f, gy = y + r * 18.0f;
            sprintf(b, "%s%d", id, r * 3 + c);
            Hit hit = HitRect(b, gx, gy, 22.0f, 14.0f);
            const bool on = (c == selCol && r == selRow);
            s_dl->AddRectFilled(P(gx, gy), P(gx + 22.0f, gy + 14.0f), Rgb(on ? kGold : (hit.hot ? 0x2F4A70 : 0x0E1A2E)), 2.0f * s_k);
            s_dl->AddRect(P(gx, gy), P(gx + 22.0f, gy + 14.0f), Rgb(on ? kGold : 0x35527E), 2.0f * s_k, ImDrawCornerFlags_All, 1.0f);
            if (hit.pressed) clicked = r * 3 + c;
        }
    return clicked;
}

void OvLabel(float x, float cy, const char* title, const char* hint) {
    Text(g_fontGBodyM, 21.0f, x, cy - (hint ? 8.0f : 0.0f), Rgb(kTxt), title, AL_L, 128.0f);
    if (hint) Text(g_fontGSmall, 15.0f, x, cy + 11.0f, Rgb(kTxt3), hint, AL_L, 130.0f);
}

// What the game will show, at 600 / 1280 of the real screen.
unsigned OvClampPos(float v, float maxV) { return (unsigned)(v < 0.0f ? 0.0f : (v > maxV ? maxV : v)); }

// Drag one block with the mouse: the grab offset is kept so it does not jump.
// (px, py) = block corner in canvas units; returns true while it is being dragged.
bool OvDrag(const char* id, float px, float py, float bw, float bh, float* gx, float* gy, float* outX, float* outY) {
    Hit hit = HitRect(id, px, py, bw, bh);
    if (ImGui::IsItemActivated()) {
        *gx = ImGui::GetIO().MousePos.x / s_k - px;
        *gy = ImGui::GetIO().MousePos.y / s_k - py;
    }
    if (hit.hot) s_dl->AddRect(P(px - 2.0f, py - 2.0f), P(px + bw + 2.0f, py + bh + 2.0f), Rgb(kGold, hit.active ? 255 : 160), 3.0f * s_k, ImDrawCornerFlags_All, 1.5f);
    if (!hit.active) return false;
    *outX = ImGui::GetIO().MousePos.x / s_k - *gx;
    *outY = ImGui::GetIO().MousePos.y / s_k - *gy;
    return true;
}

void OvPreview(OverlaySettings& s, ImTextureID tex, ImTextureID overlay, float x, float y, float w, float h) {
    const float k = w / 1280.0f, ky = h / 720.0f;
    s_dl->PushClipRect(P(x, y), P(x + w, y + h), true);
    if (tex) {
        s_dl->AddImage(tex, P(x, y), P(x + w, y + h));   // a real game capture
    } else {
        s_dl->AddRectFilledMultiColor(P(x, y), P(x + w, y + h * 0.5f), Rgb(0x5B7EA8), Rgb(0x5B7EA8), Rgb(0xA9B79A), Rgb(0xA9B79A));
        s_dl->AddRectFilledMultiColor(P(x, y + h * 0.5f), P(x + w, y + h), Rgb(0x6D6A55), Rgb(0x6D6A55), Rgb(0x3C3A30), Rgb(0x3C3A30));
        const float bx[4] = { 20.0f, 150.0f, 330.0f, 470.0f }, bw[4] = { 80.0f, 120.0f, 90.0f, 110.0f }, bh[4] = { 110.0f, 80.0f, 130.0f, 95.0f };
        for (int i = 0; i < 4; i++)
            s_dl->AddRectFilled(P(x + bx[i], y + h * 0.5f - bh[i]), P(x + bx[i] + bw[i], y + h * 0.5f), Rgb(0x8A7C68));
    }
    // The in-game drawing itself (overlay_preview.cpp, same code as the plugin), uploaded when the look changes.
    if (overlay) s_dl->AddImage(overlay, P(x, y), P(x + w, y + h));
    s_dl->PopClipRect();

    // Drag the blocks on the preview; the real X / Y are what gets saved.
    static float s_gkx = 0, s_gky = 0;
    float nx, ny;
    if (s.showKeys && OvDrag("##ov_dragk", x + s.keysX * k, y + s.keysY * ky, 170.0f, 62.0f, &s_gkx, &s_gky, &nx, &ny)) {
        s.keysX = OvClampPos((nx - x) / k, 790.0f);
        s.keysY = OvClampPos((ny - y) / ky, 590.0f);
    }
}

} // namespace

// Header and tabs only, for a page drawn elsewhere (the Windows Studio's In-game display page).
void UiGlossyHeader(int& activeTab, float screenW) {
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    HeaderAndTabs(activeTab, false);
}

void DrawUiGlossyOverlay(GlossyOverlayModel& om, int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    s_blockHits = false;
    HeaderAndTabs(activeTab, false);
    if (!om.s) return;
    OverlaySettings& s = *om.s;
    char buf[64];

    const GlRect& bar = GL_BarreInfo;
    ImgAt(GA_BAR_INFO, bar);
    Text(g_fontGBody, 22.0f, bar.x + bar.w * 0.5f, bar.y + bar.h * 0.5f, Rgb(kTxt), Tr(STR_OV_HEAD), AL_M, bar.w - 40.0f);

    // --- Preview (left) ------------------------------------------------------------
    {
        const float x = 44.0f, y = 152.0f, w = 640.0f, h = 514.0f;
        Card(x, y, w, h);
        Text(g_fontGTitle, 30.0f, x + 20.0f, y + 30.0f, Rgb(kGold), Tr(STR_OV_PREVIEW));
        OvPreview(s, om.previewTex, om.overlayTex, x + 20.0f, y + 58.0f, 600.0f, 340.0f);
        // exact X / Y under the preview, then the drag hint and the shortcut
        char nm[16], hk[96];
        sprintf(buf, "%s  X %u  Y %u", Tr(s.showPad ? STR_OV_C_PAD : STR_OV_C_KEYS), s.keysX, s.keysY);
        Text(g_fontGBodyM, 19.0f, x + 20.0f, y + 414.0f, Rgb(kTxt), buf, AL_L, w - 40.0f);
        TextWrap(g_fontGSmall, 17.0f, x + 20.0f, y + 430.0f, w - 40.0f, Rgb(kTxt3), Tr(STR_OV_DRAG));
        sprintf(hk, Tr(STR_OV_HK_LINE), OverlayKeyName(s.hotkey, nm, sizeof(nm)));
        Text(g_fontGBodyM, 19.0f, x + 20.0f, y + 466.0f, Rgb(kGold), hk, AL_L, w - 40.0f);
        sprintf(hk, Tr(STR_OV_HK_INFO), nm);
        Text(g_fontGBodyM, 19.0f, x + 20.0f, y + 490.0f, Rgb(kGold), hk, AL_L, w - 40.0f);
    }

    // --- Settings (right) ----------------------------------------------------------
    const float cx = 700.0f, cy0 = 152.0f, cw = 536.0f, ch = 514.0f;
    Card(cx, cy0, cw, ch);
    Text(g_fontGTitle, 30.0f, cx + 20.0f, cy0 + 30.0f, Rgb(kGold), Tr(STR_OV_SETTINGS));
    const float lx = cx + 20.0f, vx = cx + 160.0f, vmax = cx + cw - 16.0f;
    float y = cy0 + 62.0f;

    // Content: aim panel and console info, then ONE input-drawing choice (learning aid)
    OvLabel(lx, y + 16.0f, Tr(STR_OV_CONTENT), 0);
    {
        float x = vx;
        if (OvChip("##ov_cs", x, y, Tr(STR_OV_C_STATS), s.showStats)) s.showStats = !s.showStats;
    }
    y += 42.0f;
    OvLabel(lx, y + 16.0f, Tr(STR_OV_HELP), 0);
    {
        float x = vx;
        const bool none = !s.showKeys && !s.showPad;
        if (OvChip("##ov_h0", x, y, Tr(STR_OV_H_NONE), none)) s.showKeys = s.showPad = false;
        x += OvChipW(Tr(STR_OV_H_NONE)) + 8.0f;
        if (OvChip("##ov_h1", x, y, Tr(STR_OV_C_KEYS), s.showKeys)) { s.showKeys = true; s.showPad = false; }
        x += OvChipW(Tr(STR_OV_C_KEYS)) + 8.0f;
        if (OvChip("##ov_h2", x, y, Tr(STR_OV_C_PAD), s.showPad)) { s.showPad = true; s.showKeys = false; }
    }
    y += 42.0f;

    // Theme: five chips, wrapped if they overflow
    OvLabel(lx, y + 16.0f, Tr(STR_OV_THEME), 0);
    {
        const StrId names[5] = { STR_OV_TH0, STR_OV_TH1, STR_OV_TH2, STR_OV_TH3, STR_OV_TH4 };
        const int cur = OverlayThemeOf(s);
        float x = vx, yy = y;
        for (int i = 0; i < 5; i++) {
            const float w = OvChipW(Tr(names[i]));
            if (x + w > vmax) { x = vx; yy += 38.0f; }
            sprintf(buf, "##ov_th%d", i);
            if (OvChip(buf, x, yy, Tr(names[i]), cur == i)) { s.colorOn = kOverlayThemes[i][0]; s.colorOff = kOverlayThemes[i][1]; }
            x += w + 6.0f;
        }
        y = yy + 42.0f;
    }

    // Colours: a click on a swatch moves to the next palette colour
    OvLabel(lx, y + 15.0f, Tr(STR_OV_COLORS), Tr(STR_OV_COL_HINT));
    {
        unsigned* cols[2] = { &s.colorOn, &s.colorOff };
        for (int i = 0; i < 2; i++) {
            sprintf(buf, "##ov_col%d", i);
            if (OvSwatch(buf, vx + i * 56.0f, y, *cols[i])) {
                int at = -1;
                for (int p = 0; p < 8; p++) if (kOverlayPalette[p] == *cols[i]) at = p;
                *cols[i] = kOverlayPalette[(at + 1) & 7];
            }
        }
        Text(g_fontGSmall, 15.0f, vx + 118.0f, y + 15.0f, Rgb(kTxt3), Tr(STR_OV_CLICK_COL), AL_L, vmax - vx - 118.0f);
    }
    y += 50.0f;

    // Position: keys, then panel
    OvLabel(lx, y + 25.0f, Tr(STR_OV_POS), Tr(STR_OV_POS_HINT));
    {
        const int kc = OverlayGridIndex(s.keysX, kOverlayKeysXs), kr = OverlayGridIndex(s.keysY, kOverlayKeysYs);
        const int hit = OvGrid("##ov_pk", vx, y, kc, kr);
        if (hit >= 0) { s.keysX = kOverlayKeysXs[hit % 3]; s.keysY = kOverlayKeysYs[hit / 3]; }
    }
    y += 62.0f;

    // Key style
    OvLabel(lx, y + 16.0f, Tr(STR_OV_STYLE), 0);
    {
        float x = vx;
        if (OvChip("##ov_s1", x, y, Tr(STR_OV_ST_IMG), s.style == 1)) s.style = 1;
        x += OvChipW(Tr(STR_OV_ST_IMG)) + 8.0f;
        if (OvChip("##ov_s0", x, y, Tr(STR_OV_ST_DRAW), s.style == 0)) s.style = 0;
    }
    y += 42.0f;

    // Show / hide shortcut: "Change" waits for the next key press
    OvLabel(lx, y + 15.0f, Tr(STR_OV_HOTKEY), Tr(STR_OV_HOTKEY_HINT));
    {
        if (om.capturing && om.keyEvent > 0) {
            // Only modifier keys (E0-E7, the Windows key is E3 or E7) are stored: the plugin
            // reads any other key as Windows. Esc cancels; other keys are ignored.
            if (om.keyEvent >= 0xE0 && om.keyEvent <= 0xE7) { s.hotkey = (unsigned)om.keyEvent; om.capturing = false; }
            else if (om.keyEvent == 0x29) om.capturing = false;
        }
        char name[16];
        const char* shown = om.capturing ? Tr(STR_OV_PRESS) : OverlayKeyName(s.hotkey, name, sizeof(name));
        const float w = OvChipW(shown) < 70.0f ? 70.0f : OvChipW(shown);
        s_dl->AddRectFilled(P(vx, y), P(vx + w, y + 32.0f), Rgb(0x0A1424), 4.0f * s_k);
        s_dl->AddRect(P(vx, y), P(vx + w, y + 32.0f), Rgb(0x2F4A70), 4.0f * s_k, ImDrawCornerFlags_All, 1.0f);
        Text(g_fontGBodyM, 20.0f, vx + w * 0.5f, y + 16.0f, Rgb(kGold), shown, AL_M, w - 6.0f);
        if (OvChip("##ov_hk", vx + w + 8.0f, y, Tr(STR_OV_CHANGE), om.capturing)) om.capturing = !om.capturing;
    }

    // Save / Reset + result
    const float by = cy0 + ch - 80.0f;
    if (om.savedAt >= 0.0 && ImGui::GetTime() - om.savedAt < 6.0)
        Text(g_fontGSmall, 17.0f, lx, by + 46.0f, Rgb(om.savedOk ? 0x9BE36A : 0xF0605A), Tr(om.savedOk ? STR_OV_SAVED : STR_OV_SAVE_FAIL), AL_L, cw - 40.0f);
    {
        const float bw = (cw - 50.0f) * 0.5f;
        GlRect r1 = { lx, by, bw, 42.0f }, t1 = { lx + bw * 0.5f, by + 21.0f, 0.0f, 0.0f };
        if (Button("##ov_reset", r1, t1, GA_BTN_HALF, GA_BTN_HALF_F, Tr(STR_OV_RESET))) { s = OverlaySettings(); om.capturing = false; }
        GlRect r2 = { lx + bw + 10.0f, by, bw, 42.0f }, t2 = { lx + bw * 1.5f + 10.0f, by + 21.0f, 0.0f, 0.0f };
        if (Button("##ov_save", r2, t2, GA_BTN_HALF, GA_BTN_HALF_F, Tr(STR_OV_SAVE), true)) om.reqSave = true;
    }
}

// ============================================================================
// USB controller page (2026-11-03; redone 2026-11-06 with the components of the
// keyboard & mouse page: same cells, badges, Assistant button, pad render, slider
// panel, mouse-style cells) and its macro editor (up to 64 steps per macro).
// ============================================================================
static const char* const kPadLbl[PAD_CELLS] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L3", "R3",
                                                "START", "BACK", "XBOX", "D-Up", "D-Down", "D-Left", "D-Right" };

// A cell asset stretched to width w: both caps stay as drawn, the middle is stretched.
static void Cell9(int id, float x, float y, float w, int alpha = 255) {
    if (!Has(id)) return;
    const UiSkinTex& t = s_tex[id];
    const GlossyAssetSpec& a = g_glossyAssets[id];
    const float p = (float)a.pad, cap = 10.0f, tw = (float)t.w;
    const float y0 = y - p, y1 = y0 + (float)t.h;
    const ImU32 col = IM_COL32(255, 255, 255, alpha);
    const float u1 = (p + cap) / tw, u2 = (p + (float)a.w - cap) / tw;
    s_dl->AddImage(t.tex, P(x - p, y0), P(x + cap, y1), ImVec2(0.0f, 0.0f), ImVec2(u1, 1.0f), col);
    s_dl->AddImage(t.tex, P(x + cap, y0), P(x + w - cap, y1), ImVec2(u1, 0.0f), ImVec2(u2, 1.0f), col);
    s_dl->AddImage(t.tex, P(x + w - cap, y0), P(x + w + p, y1), ImVec2(u2, 0.0f), ImVec2(1.0f, 1.0f), col);
}

// Same slider panel as the mouse settings, cut to topH + botH rows (its titles and rails
// keep their offsets from the top).
static void SlidersPanel(float x, float y, float topH, float botH) {
    const int id = GA_GROUP_SLIDERS;
    if (!Has(id)) return;
    const UiSkinTex& t = s_tex[id];
    const GlossyAssetSpec& a = g_glossyAssets[id];
    const float p = (float)a.pad, tw = (float)t.w, th = (float)t.h;
    s_dl->AddImage(t.tex, P(x - p, y - p), P(x - p + tw, y + topH), ImVec2(0.0f, 0.0f), ImVec2(1.0f, (p + topH) / th));
    s_dl->AddImage(t.tex, P(x - p, y + topH), P(x - p + tw, y + topH + botH + p),
                   ImVec2(0.0f, (p + (float)a.h - botH) / th), ImVec2(1.0f, 1.0f));
}

// Rail, fill and knob exactly as the mouse sliders draw them (ry = top of the rail).
static void SliderRail(float x, float ry, float w, float frac) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    if (Has(GA_SLIDER_TRACK)) {
        const UiSkinTex& tt = s_tex[GA_SLIDER_TRACK];
        const float pad = (float)g_glossyAssets[GA_SLIDER_TRACK].pad;
        s_dl->AddImage(tt.tex, P(x - pad, ry - pad), P(x + w + pad, ry - pad + tt.h));
    }
    if (Has(GA_SLIDER_FILL) && frac > 0.0f) {
        const UiSkinTex& tf = s_tex[GA_SLIDER_FILL];
        const float pad = (float)g_glossyAssets[GA_SLIDER_FILL].pad;
        s_dl->AddImage(tf.tex, P(x - pad, ry - pad), P(x + frac * w + pad, ry - pad + tf.h), ImVec2(0.0f, 0.0f), ImVec2(frac, 1.0f));
    }
    ImgCentered(GA_SLIDER_KNOB, x + frac * w, ry + 3.0f);
}

// Key-style chip (white rounded, dark text), right aligned at `right`.
static float KeyChip(float right, float cy, const char* s, bool dim) {
    float w = TextW(g_fontGBodyM, 20.0f, s) + 14.0f;
    if (w < 28.0f) w = 28.0f;
    s_dl->AddRectFilled(P(right - w, cy - 13.0f), P(right, cy + 13.0f), Rgb(dim ? 0x46526A : 0xECEFF2), 6.0f * s_k);
    Text(g_fontGBodyM, 20.0f, right - w * 0.5f, cy, Rgb(dim ? 0x96A2B6 : 0x1B2740), s, AL_M);
    return right - w;
}

// The grid badge of a pad cell, scaled and centred.
static void BadgeScaled(int cell, float cx, float cy, float sc, int alpha) {
    const int id = kGridBadge[cell];
    if (!Has(id)) return;
    const UiSkinTex& t = s_tex[id];
    const GlossyAssetSpec& a = g_glossyAssets[id];
    const float hw = ((float)a.w * 0.5f + (float)a.pad) * sc, hh = ((float)a.h * 0.5f + (float)a.pad) * sc;
    s_dl->AddImage(t.tex, P(cx - hw, cy - hh), P(cx + hw, cy + hh), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha));
}

static void DrawStepsGlyph(float x, float y) {
    const ImU32 c = Rgb(0x6FC3FF);
    for (int i = 0; i < 3; i++) {
        const float xx = x + 2.0f + (i == 2 ? 6.0f : 0.0f), yy = y + 3.0f + 8.0f * i, ww = (i == 1) ? 26.0f : 18.0f;
        s_dl->AddRect(P(xx, yy), P(xx + ww, yy + 5.0f), c, 2.0f * s_k, ImDrawCornerFlags_All, 1.8f);
    }
}

static float MsToFrac(int ms) {
    if (ms < 16) ms = 16;
    if (ms > 5000) ms = 5000;
    return logf((float)ms / 16.0f) / logf(5000.0f / 16.0f);
}
static int FracToMs(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    int v = (int)(16.0f * expf(f * logf(5000.0f / 16.0f)) + 0.5f);
    if (v >= 1000) v = (v / 50) * 50; else if (v >= 200) v = (v / 10) * 10; else if (v >= 50) v = (v / 5) * 5;
    return v < 16 ? 16 : (v > 5000 ? 5000 : v);
}

// Cells held now, through the pad's own mapping (bit i = cell i).
static unsigned LiveCellMask(const GlossyPadModel& pm) {
    unsigned m = 0;
    for (int i = 0; i < PAD_CELLS; i++)
        if (pm.btnIdx[i] >= 0 && (pm.liveMask & (1u << pm.btnIdx[i]))) m |= 1u << i;
    return m;
}

// Display name of a pad cell: the D-pad names are translated, the others are the same everywhere.
static const char* PadName(int i) {
    switch (i) {
        case 13: return Tr(STR_PAD_DUP);
        case 14: return Tr(STR_PAD_DDOWN);
        case 15: return Tr(STR_PAD_DLEFT);
        case 16: return Tr(STR_PAD_DRIGHT);
        default: return kPadLbl[i];
    }
}

static void MaskText(unsigned mask, char* out) {   // "A + D-Right"
    out[0] = 0;
    for (int i = 0; i < PAD_CELLS; i++)
        if (mask & (1u << i)) { if (out[0]) strcat(out, " + "); strcat(out, PadName(i)); }
}

// Pad glow of every held cell (the four D-pad cells share one glow).
static void GlowMask(unsigned mask) {
    bool dpad = false;
    for (int i = 0; i < PAD_CELLS; i++) {
        if (!(mask & (1u << i))) continue;
        if (i >= 13) { if (dpad) continue; dpad = true; }
        const GlPadPoint& p = GL_PAD_POINTS[i];
        ImgCentered(p.glowAsset, p.x, p.y);
    }
}

static int FreeTrigger(const GlossyPadModel& pm) {
    for (int b = 0; b < 32; b++) {
        bool used = false;
        for (int i = 0; i < PAD_CELLS; i++) used = used || pm.btnIdx[i] == b;
        for (int j = 0; j < pm.macroCount; j++) used = used || pm.macros[j].btn == b;
        if (!used) return b;
    }
    return 0;
}

static void OpenEditor(GlossyPadModel& pm, int idx) {
    pm.editMacro = idx; pm.editStep = 0; pm.stepScroll = 0;
    pm.capTrigger = false; pm.recording = false; pm.testing = false;
    pm.wizard = false; pm.captureCell = -1;
}

static void DeleteMacro(GlossyPadModel& pm, int i) {
    for (int k = i; k + 1 < pm.macroCount; k++) pm.macros[k] = pm.macros[k + 1];
    pm.macroCount--;
    pm.reqSaveButtons = true;
}

static void MacroSummary(const GlossyPadMacro& mc, char* out) {   // "6 pas  �  1.2 s"
    int total = 0;
    for (int s = 0; s < mc.stepCount; s++) total += mc.steps[s].ms;
    char a[24];
    sprintf(a, Tr(STR_PAD_STEPS_FMT), mc.stepCount);
    if (total > 0) sprintf(out, "%s  \xC2\xB7  %d.%d s", a, total / 1000, (total % 1000) / 100);
    else strcpy(out, a);
}

// ---------------------------------------------------------------------------------
// Main page of a controller
// ---------------------------------------------------------------------------------
static void PadMainPage(GlossyPadModel& pm, double now, ImGuiIO& io, unsigned newBits) {
    char buf[128];
    const bool editable = pm.hasEntry && pm.hasAxes;

    // capture: next pad button pressed while a cell waits
    if (pm.captureCell >= 0) {
        if (now > pm.captureUntil || !editable) { pm.captureCell = -1; pm.wizard = false; }
        else if (pm.hasLive && newBits) {
            int b = 0;
            while (b < 31 && !(newBits & (1u << b))) b++;
            pm.btnIdx[pm.captureCell] = b;
            for (int j = 0; j < pm.macroCount; j++) if (pm.macros[j].btn == b) pm.macros[j].btn = FreeTrigger(pm);
            pm.reqSaveButtons = true;
            if (pm.wizard && pm.captureCell + 1 < PAD_CELLS) { pm.captureCell++; pm.captureUntil = now + 10.0; }
            else { pm.captureCell = -1; pm.wizard = false; }
        }
    }
    const bool capturing = pm.captureCell >= 0;

    // ---- left column: title, Assistant, the 21 cells ------------------------------
    DrawPadGlyph(GL_IcoTitreClavier.x, GL_IcoTitreClavier.y, GL_IcoTitreClavier.w, GL_IcoTitreClavier.h);
    Text(g_fontGTitle, 37.0f, GL_TxtTitreClavier.x, GL_TxtTitreClavier.y, Rgb(0xF2F5F8), Tr(STR_PAD_TITLE));
    ImgAt(GA_TITLE_RULE, GL_RegleClavier);
    {
        sprintf(buf, Tr(STR_PAD_WIZARD), PAD_CELLS);
        if (Button("##gp_wiz", GL_BtnAssistant, GL_TxtAssistant, GA_BTN_WIDE, GA_BTN_WIDE_F, buf, pm.wizard) && editable && pm.hasLive) {
            pm.wizard = !pm.wizard;
            pm.captureCell = pm.wizard ? 0 : -1;
            pm.captureUntil = now + 10.0;
        }
    }
    int focusIdx = capturing ? pm.captureCell : -1;
    for (int i = 0; i < 21; i++) {
        const int col = i / 7, row = i % 7;
        const float x = GL_CaseA.x + GL_KGRID_DX * col;
        const float y = GL_CaseA.y + GL_KGRID_DY * row;
        const float cyc = y + GL_CaseA.h * 0.5f;
        const bool real = i < PAD_CELLS;
        sprintf(buf, "##gp_cell%d", i);
        Hit h = real ? HitRect(buf, x, y, GL_CaseA.w, GL_CaseA.h) : Hit();
        const bool waiting = capturing && i == pm.captureCell;
        const bool held = real && pm.hasLive && pm.btnIdx[i] >= 0 && (pm.liveMask & (1u << pm.btnIdx[i]));
        const bool done = pm.wizard && i < pm.captureCell;
        const bool lit = waiting || held || (h.hot && editable);
        const int alpha = (real && editable) ? 255 : 120;
        Img(lit ? GA_KCELL_F : (done ? GA_KCELL_DONE : GA_KCELL), x, y, alpha);
        Img(kGridBadge[i], x + 8.0f, cyc - AssetH(kGridBadge[i]) * 0.5f, alpha);
        const float kRight = x + GL_CaseA.w - 8.0f;
        if (waiting) {
            Text(g_fontGBodyM, 21.0f, kRight, cyc, Rgb(kGold), Tr(STR_GLOSSY_CAPTURE), AL_R, GL_CaseA.w - 60.0f);
        } else {
            const int b = real ? pm.btnIdx[i] : -1;
            if (b >= 0) sprintf(buf, "B%d", b + 1); else strcpy(buf, "-");
            KeyChip(kRight, cyc, buf, b < 0);
        }
        if (real && (held || (h.hot && focusIdx < 0))) focusIdx = i;
        if (real && editable) {
            if (h.pressed) {
                if (pm.hasLive) { pm.captureCell = waiting ? -1 : i; pm.wizard = false; pm.captureUntil = now + 10.0; }
                else { pm.btnIdx[i] = pm.btnIdx[i] >= 15 ? -1 : pm.btnIdx[i] + 1; pm.reqSaveButtons = true; }
            }
            if (h.rclick) { pm.btnIdx[i] = -1; pm.captureCell = -1; pm.reqSaveButtons = true; }
            if (h.hot && !pm.hasLive) ImGui::SetTooltip("%s", Tr(STR_PAD_NOLIVE));
        }
    }

    // ---- centre: the same white pad, the lit button, one status line --------------
    ImgAt(GA_PAD_FRONT, GL_ImgManetteFace);
    if (focusIdx >= 0 && focusIdx < PAD_CELLS) {
        const GlPadPoint& p = GL_PAD_POINTS[focusIdx];
        ImgCentered(p.glowAsset, p.x, p.y);
        const int b = pm.btnIdx[focusIdx];
        if (b >= 0) sprintf(buf, "  \xC2\xB7  B%d", b + 1); else strcpy(buf, "  \xC2\xB7  -");
        const float w1 = TextW(g_fontGBodyM, 26.0f, PadName(focusIdx)), w2 = TextW(g_fontGBody, 25.0f, buf);
        const float x = p.labelX - (w1 + w2) * 0.5f;
        Text(g_fontGBodyM, 26.0f, x, p.labelY, Rgb(kGold), PadName(focusIdx));
        Text(g_fontGBody, 25.0f, x + w1, p.labelY, Rgb(kTxt), buf);
    }
    if (!editable)      Text(g_fontGBody, 22.0f, 670.0f, 622.0f, Rgb(kMsg), Tr(STR_PAD_NOENTRY), AL_M, 500.0f);
    else if (capturing) Text(g_fontGBody, 24.0f, 670.0f, 622.0f, Rgb(kMsg), Tr(STR_PAD_PRESS), AL_M, 500.0f);

    // ---- right column: right stick (the mouse slider panel, one slider + invert Y) -
    BadgeScaled(PAD_CELLS - 8, 975.0f, 175.0f, 1.0f, 255);   // R3 badge as the title icon
    Text(g_fontGTitle, 37.0f, GL_TxtTitreSouris.x, GL_TxtTitreSouris.y, Rgb(0xF2F5F8), Tr(STR_PAD_STICK), AL_L, 240.0f);
    ImgAt(GA_TITLE_RULE_S, GL_RegleSouris);
    const float py = 206.0f;
    SlidersPanel(956.0f, py, 72.0f, 28.0f);
    {
        Text(g_fontGTitle, 30.0f, 966.0f, py + 20.0f, Rgb(0xF2F5F8), Tr(STR_PAD_DZ), AL_L, 180.0f);
        sprintf(buf, "%d %%", pm.deadzone / 100);
        Text(g_fontGBodyM, 22.0f, 1232.0f, py + 22.0f, Rgb(kGold), buf, AL_R);
        const float tx = 966.0f, tw = 266.0f, ry = py + 44.0f;
        Hit h = HitRect("##gp_dz", tx - 10.0f, ry - 14.0f, tw + 20.0f, 34.0f);
        if (h.active && editable) {
            float f = (io.MousePos.x / s_k - tx) / tw;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            pm.deadzone = ((int)(f * 100.0f + 0.5f)) * 100;
        }
        if (h.deactivated && editable) pm.reqSaveStick = true;
        SliderRail(tx, ry, tw, pm.deadzone / 10000.0f);

        const char* lbl = Tr(STR_GLOSSY_INVERT);
        const float lw = TextW(g_fontGSmall, 20.0f, lbl), right = 1230.0f, cy = py + 85.0f, bx = right - lw - 24.0f;
        Hit hi = HitRect("##gp_inv", bx - 4.0f, cy - 12.0f, lw + 32.0f, 24.0f);
        s_dl->AddRect(P(bx, cy - 8.0f), P(bx + 16.0f, cy + 8.0f), Rgb(hi.hot ? kGold : 0x8FA2B8), 3.0f * s_k, ImDrawCornerFlags_All, 1.5f);
        if (pm.invertRY) s_dl->AddRectFilled(P(bx + 4.0f, cy - 4.0f), P(bx + 12.0f, cy + 4.0f), Rgb(kGold), 2.0f * s_k);
        Text(g_fontGSmall, 20.0f, right, cy, Rgb(hi.hot ? kGold : kTxt2), lbl, AL_R);
        if (hi.pressed && editable) { pm.invertRY = !pm.invertRY; pm.reqSaveStick = true; }
    }

    // ---- right column: macros (mouse-style cells) -----------------------------------
    DrawStepsGlyph(964.0f, 318.0f);
    Text(g_fontGTitle, 37.0f, GL_TxtTitreSouris.x, 331.0f, Rgb(0xF2F5F8), Tr(STR_PAD_MACROS), AL_L, 240.0f);
    { GlRect rule = { 960.0f, 352.0f, 276.0f, 1.0f }; ImgAt(GA_TITLE_RULE_S, rule); }
    {
        const int rows = 6;
        const int items = pm.macroCount + (pm.macroCount < PAD_MACROS_MAX ? 1 : 0);
        const float x = 962.0f, w = 276.0f, y0 = 364.0f, pitch = 46.0f;
        const ImVec2 mp = io.MousePos;
        if (io.MouseWheel != 0.0f && mp.x >= x * s_k && mp.x <= (x + w) * s_k && mp.y >= y0 * s_k && mp.y <= (y0 + rows * pitch) * s_k)
            pm.macroScroll -= (int)(io.MouseWheel > 0.0f ? 1 : -1);
        if (pm.macroScroll > items - rows) pm.macroScroll = items - rows;
        if (pm.macroScroll < 0) pm.macroScroll = 0;
        int del = -1;
        for (int r = 0; r < rows; r++) {
            const int k = pm.macroScroll + r;
            if (k >= items) break;
            const float y = y0 + pitch * r, cy = y + 20.0f;
            if (k < pm.macroCount) {
                GlossyPadMacro& mc = pm.macros[k];
                sprintf(buf, "##gp_mac%d", k);
                Hit h = HitRect(buf, x, y, w - 30.0f, 40.0f);
                Cell9(h.hot ? GA_MCELL_S_F : GA_MCELL_S, x, y, w);
                sprintf(buf, "B%d", mc.btn + 1);
                KeyChip(x + 52.0f, cy, buf, false);
                Text(g_fontGBody, 26.0f, x + 70.0f, cy, Rgb(h.hot ? kGold : kTxt3), "\xE2\x80\xBA", AL_M);
                MacroSummary(mc, buf);
                Text(g_fontGBody, 22.0f, x + 86.0f, cy, Rgb(kTxt), buf, AL_L, w - 86.0f - 34.0f);
                char id[24];
                sprintf(id, "##gp_macx%d", k);
                Hit hx = HitRect(id, x + w - 30.0f, y, 30.0f, 40.0f);
                Text(g_fontGBodyM, 22.0f, x + w - 16.0f, cy, Rgb(hx.hot ? 0xF0605A : kTxt3), "x", AL_M);
                if (h.pressed) OpenEditor(pm, k);
                if (hx.pressed || h.rclick) del = k;
            } else {
                Hit h = HitRect("##gp_madd", x, y, w, 40.0f);
                Cell9(GA_MCELL_S_ADD, x, y, w, h.hot ? 255 : 170);
                sprintf(buf, "+  %s", Tr(STR_PAD_ADD_MACRO));
                Text(g_fontGBody, 22.0f, x + w * 0.5f, cy, Rgb(h.hot ? kGold : kTxt2), buf, AL_M, w - 20.0f);
                if (h.pressed) {
                    GlossyPadMacro& mc = pm.macros[pm.macroCount];
                    mc.btn = FreeTrigger(pm); mc.stepCount = 1; mc.steps[0].mask = 1u; mc.steps[0].ms = 80;
                    pm.macroCount++;
                    OpenEditor(pm, pm.macroCount - 1);
                    pm.capTrigger = pm.hasLive;   // pick the trigger button right away
                    pm.reqSaveButtons = true;
                }
            }
        }
        if (del >= 0) DeleteMacro(pm, del);
    }
}

// ---------------------------------------------------------------------------------
// Macro editor: step list (left), the pad lit like the real page does (centre),
// toggles of the 17 buttons + pause and the step duration (right).
// ---------------------------------------------------------------------------------
static void PadEditorPage(GlossyPadModel& pm, double now, ImGuiIO& io, unsigned newBits) {
    char buf[160], id[32];
    if (pm.editMacro < 0 || pm.editMacro >= pm.macroCount) { pm.editMacro = -1; return; }
    GlossyPadMacro& mc = pm.macros[pm.editMacro];
    static int s_lastStep = -1;

    // ---- trigger button: the next pad button pressed -------------------------------
    if (pm.capTrigger && pm.hasLive && newBits) {
        int b = 0;
        while (b < 31 && !(newBits & (1u << b))) b++;
        bool taken = false;
        for (int j = 0; j < pm.macroCount; j++) taken = taken || (j != pm.editMacro && pm.macros[j].btn == b);
        if (!taken) {
            for (int i = 0; i < PAD_CELLS; i++) if (pm.btnIdx[i] == b) pm.btnIdx[i] = -1;   // a macro trigger is not a plain button
            mc.btn = b;
            pm.capTrigger = false;
            pm.reqSaveButtons = true;
        }
    }

    // ---- recording: every change of the held cells becomes a step with its real time
    if (pm.recording) {
        const unsigned cm = LiveCellMask(pm);
        bool stop = false;
        if (!pm.recBegan) {
            if (cm) { pm.recBegan = true; pm.recMask = cm; pm.recStart = now; mc.stepCount = 0; }
        } else if (cm != pm.recMask) {
            int ms = (int)((now - pm.recStart) * 1000.0 + 0.5);
            if (ms < 16) ms = 16;
            if (ms > 5000) ms = 5000;
            mc.steps[mc.stepCount].mask = pm.recMask; mc.steps[mc.stepCount].ms = ms; mc.stepCount++;
            pm.recMask = cm; pm.recStart = now;
            if (mc.stepCount >= PAD_STEPS_MAX) stop = true;
        } else if (cm == 0 && now - pm.recStart > 2.0) {
            stop = true;   // released and quiet: the sequence is over
        }
        if (stop) {
            pm.recording = false; pm.recBegan = false;
            if (mc.stepCount > 0) pm.editStep = mc.stepCount - 1;
            pm.reqSaveButtons = true;
        }
    }

    // ---- test: replay the steps on the pad drawing (the plugin plays them in the game)
    unsigned testMask = 0;
    int testStep = -1;
    if (pm.testing) {
        const double t = (now - pm.testStart) * 1000.0;
        double cum = 0.0;
        for (int s = 0; s < mc.stepCount; s++) {
            const double dur = mc.steps[s].ms > 0 ? (double)mc.steps[s].ms : 500.0;
            if (t < cum + dur) { testStep = s; testMask = mc.steps[s].mask; break; }
            cum += dur;
        }
        if (testStep < 0) pm.testing = false;
    }

    if (pm.editStep >= mc.stepCount) pm.editStep = mc.stepCount - 1;
    if (pm.editStep < 0 && mc.stepCount > 0) pm.editStep = 0;
    GlossyPadStep* cur = (pm.editStep >= 0 && pm.editStep < mc.stepCount) ? &mc.steps[pm.editStep] : 0;

    // ======================= left column ===============================================
    DrawStepsGlyph(GL_IcoTitreClavier.x + 2.0f, GL_IcoTitreClavier.y - 2.0f);
    const float tw0 = Text(g_fontGTitle, 37.0f, GL_TxtTitreClavier.x, GL_TxtTitreClavier.y, Rgb(0xF2F5F8), Tr(STR_PAD_ED_TITLE));
    {   // trigger chip: click, then press the button
        const float cx0 = GL_TxtTitreClavier.x + tw0 + 16.0f, cy = GL_TxtTitreClavier.y;
        if (pm.capTrigger) strcpy(buf, Tr(STR_PAD_TRIG_WAIT)); else sprintf(buf, "B%d", mc.btn + 1);
        float w = TextW(g_fontGBodyM, 20.0f, buf) + 14.0f;
        if (w < 28.0f) w = 28.0f;
        Hit h = HitRect("##gp_trig", cx0, cy - 13.0f, w, 26.0f);
        s_dl->AddRectFilled(P(cx0, cy - 13.0f), P(cx0 + w, cy + 13.0f), Rgb(pm.capTrigger ? 0x3A3216 : 0xECEFF2), 6.0f * s_k);
        if (pm.capTrigger || h.hot) s_dl->AddRect(P(cx0, cy - 13.0f), P(cx0 + w, cy + 13.0f), Rgb(kGold), 6.0f * s_k, ImDrawCornerFlags_All, 1.5f);
        Text(g_fontGBodyM, 20.0f, cx0 + w * 0.5f, cy, Rgb(pm.capTrigger ? kGold : 0x1B2740), buf, AL_M);
        if (h.hot) ImGui::SetTooltip("%s", Tr(STR_PAD_TRIG_HINT));
        if (h.pressed) {
            if (pm.hasLive) pm.capTrigger = !pm.capTrigger;
            else { mc.btn = (mc.btn + 1) % 16; pm.reqSaveButtons = true; }
        }
    }
    ImgAt(GA_TITLE_RULE, GL_RegleClavier);
    {
        if (pm.recording) sprintf(buf, Tr(STR_PAD_REC_STOP), mc.stepCount);
        else strcpy(buf, Tr(STR_PAD_REC));
        const char* label = (pm.recording && !pm.recBegan) ? Tr(STR_PAD_REC_WAIT) : buf;
        if (Button("##gp_rec", GL_BtnAssistant, GL_TxtAssistant, GA_BTN_WIDE, GA_BTN_WIDE_F, label, pm.recording) && pm.hasLive) {
            if (pm.recording) {
                if (pm.recBegan && pm.recMask && mc.stepCount < PAD_STEPS_MAX) {
                    int ms = (int)((now - pm.recStart) * 1000.0 + 0.5);
                    mc.steps[mc.stepCount].mask = pm.recMask; mc.steps[mc.stepCount].ms = ms < 16 ? 16 : (ms > 5000 ? 5000 : ms); mc.stepCount++;
                }
                pm.recording = false; pm.recBegan = false;
                if (mc.stepCount > 0) pm.editStep = mc.stepCount - 1;
                pm.reqSaveButtons = true;
            } else {
                pm.recording = true; pm.recBegan = false; pm.recMask = 0; pm.testing = false; pm.capTrigger = false;
            }
        }
    }

    // step list: 7 rows, the last item is "Add a step"
    {
        const int rows = 7;
        const int items = mc.stepCount + (mc.stepCount < PAD_STEPS_MAX ? 1 : 0);
        const float x = GL_CaseA.x, w = 344.0f, y0 = GL_CaseA.y;
        if (pm.editStep != s_lastStep) {
            s_lastStep = pm.editStep;
            if (pm.editStep >= 0 && pm.editStep < pm.stepScroll) pm.stepScroll = pm.editStep;
            if (pm.editStep >= pm.stepScroll + rows) pm.stepScroll = pm.editStep - rows + 1;
        }
        const ImVec2 mp = io.MousePos;
        if (io.MouseWheel != 0.0f && mp.x >= x * s_k && mp.x <= (x + w) * s_k && mp.y >= y0 * s_k && mp.y <= (y0 + rows * GL_KGRID_DY) * s_k)
            pm.stepScroll -= (int)(io.MouseWheel > 0.0f ? 1 : -1);
        if (pm.stepScroll > items - rows) pm.stepScroll = items - rows;
        if (pm.stepScroll < 0) pm.stepScroll = 0;
        int mv = -1, dir = 0, del = -1;
        for (int r = 0; r < rows; r++) {
            const int k = pm.stepScroll + r;
            if (k >= items) break;
            const float y = y0 + GL_KGRID_DY * r, cy = y + GL_CaseA.h * 0.5f;
            if (k < mc.stepCount) {
                GlossyPadStep& st = mc.steps[k];
                sprintf(id, "##gp_step%d", k);
                Hit h = HitRect(id, x, y, w - 84.0f, GL_CaseA.h);
                const bool sel = (k == pm.editStep), playing = (k == testStep);
                Cell9((sel || h.hot || playing) ? GA_KCELL_F : GA_KCELL, x, y, w);
                sprintf(buf, "%d", k + 1);
                KeyChip(x + 38.0f, cy, buf, false);
                if (st.mask == 0) {
                    Text(g_fontGBody, 22.0f, x + 52.0f, cy, Rgb(kTxt3), Tr(STR_PAD_PAUSE), AL_L);
                } else {
                    float gx = x + 66.0f;
                    int shown = 0;
                    for (int i = 0; i < PAD_CELLS; i++) {
                        if (!(st.mask & (1u << i))) continue;
                        if (shown == 4) { Text(g_fontGBodyM, 20.0f, gx - 6.0f, cy, Rgb(kTxt2), "+", AL_L); break; }
                        BadgeScaled(i, gx, cy, 0.85f, 255);
                        gx += 33.0f; shown++;
                    }
                }
                if (st.ms > 0) sprintf(buf, "%d ms", st.ms); else strcpy(buf, Tr(STR_PAD_HELD));
                KeyChip(x + w - 86.0f, cy, buf, false);
                sprintf(id, "##gp_stup%d", k);
                Hit hu = HitRect(id, x + w - 78.0f, y + 8.0f, 24.0f, 30.0f);
                sprintf(id, "##gp_stdn%d", k);
                Hit hd = HitRect(id, x + w - 54.0f, y + 8.0f, 24.0f, 30.0f);
                sprintf(id, "##gp_stx%d", k);
                Hit hx = HitRect(id, x + w - 30.0f, y + 8.0f, 24.0f, 30.0f);
                const float ax = x + w - 66.0f, bx = x + w - 42.0f;
                s_dl->AddTriangleFilled(P(ax - 6.0f, cy + 4.0f), P(ax + 6.0f, cy + 4.0f), P(ax, cy - 5.0f), Rgb(hu.hot ? kGold : kTxt3));
                s_dl->AddTriangleFilled(P(bx - 6.0f, cy - 4.0f), P(bx + 6.0f, cy - 4.0f), P(bx, cy + 5.0f), Rgb(hd.hot ? kGold : kTxt3));
                Text(g_fontGBodyM, 22.0f, x + w - 18.0f, cy, Rgb(hx.hot ? 0xF0605A : kTxt3), "x", AL_M);
                if (h.pressed) { pm.editStep = k; pm.testing = false; }
                if (hu.pressed && k > 0) { mv = k; dir = -1; }
                if (hd.pressed && k + 1 < mc.stepCount) { mv = k; dir = 1; }
                if (hx.pressed) del = k;
            } else {
                Hit h = HitRect("##gp_stadd", x, y, w, GL_CaseA.h);
                Cell9(GA_MCELL_S_ADD, x, y + 3.0f, w, h.hot ? 255 : 170);
                sprintf(buf, "+  %s        %d / %d", Tr(STR_PAD_ADD_STEP), mc.stepCount, PAD_STEPS_MAX);
                Text(g_fontGBody, 22.0f, x + w * 0.5f, cy, Rgb(h.hot ? kGold : kTxt2), buf, AL_M, w - 20.0f);
                if (h.pressed) {
                    mc.steps[mc.stepCount].mask = 0; mc.steps[mc.stepCount].ms = 80;
                    pm.editStep = mc.stepCount; mc.stepCount++;
                    pm.testing = false; pm.reqSaveButtons = true;
                }
            }
        }
        if (mv >= 0) {
            GlossyPadStep tmp = mc.steps[mv]; mc.steps[mv] = mc.steps[mv + dir]; mc.steps[mv + dir] = tmp;
            if (pm.editStep == mv) pm.editStep = mv + dir; else if (pm.editStep == mv + dir) pm.editStep = mv;
            pm.reqSaveButtons = true;
        }
        if (del >= 0) {
            for (int k = del; k + 1 < mc.stepCount; k++) mc.steps[k] = mc.steps[k + 1];
            mc.stepCount--;
            if (pm.editStep >= mc.stepCount) pm.editStep = mc.stepCount - 1;
            pm.reqSaveButtons = true;
        }
    }

    // ======================= centre ======================================================
    ImgAt(GA_PAD_FRONT, GL_ImgManetteFace);
    GlowMask(pm.testing ? testMask : (pm.recording ? LiveCellMask(pm) : (cur ? cur->mask : 0)));
    {
        const unsigned m = pm.testing ? testMask : (cur ? cur->mask : 0);
        const int ms = pm.testing ? (testStep >= 0 ? mc.steps[testStep].ms : 0) : (cur ? cur->ms : 0);
        if (cur || pm.testing) {
            char names[96];
            MaskText(m, names);
            if (!names[0]) strcpy(names, Tr(STR_PAD_PAUSE));
            char dur[24];
            if (ms > 0) sprintf(dur, "%d ms", ms); else strcpy(dur, Tr(STR_PAD_HELD));
            sprintf(buf, Tr(STR_PAD_ED_STEP), (pm.testing ? testStep : pm.editStep) + 1);
            strcat(buf, "  \xC2\xB7  "); strcat(buf, names); strcat(buf, "  \xC2\xB7  "); strcat(buf, dur);
            Text(g_fontGBody, 24.0f, 670.0f, 560.0f, Rgb(kTxt), buf, AL_M, 480.0f);
        }
    }
    if (Button("##gp_test", GL_BtnMesurer, GL_TxtMesurer, GA_BTN_HALF, GA_BTN_HALF_F, Tr(pm.testing ? STR_PAD_TEST_STOP : STR_PAD_TEST), pm.testing)) {
        pm.testing = !pm.testing; pm.testStart = now; pm.recording = false;
    }
    if (Button("##gp_done", GL_BtnCalibrer, GL_TxtCalibrer, GA_BTN_HALF, GA_BTN_HALF_F, Tr(STR_PAD_DONE))) {
        if (mc.stepCount == 0) DeleteMacro(pm, pm.editMacro);
        pm.editMacro = -1; pm.recording = false; pm.testing = false; pm.capTrigger = false;
        s_lastStep = -1;
        return;
    }

    // ======================= right column ================================================
    DrawStepsGlyph(962.0f, GL_IcoTitreSouris.y - 2.0f);
    sprintf(buf, Tr(STR_PAD_ED_STEP), pm.editStep + 1);
    Text(g_fontGTitle, 37.0f, GL_TxtTitreSouris.x, GL_TxtTitreSouris.y, Rgb(0xF2F5F8), cur ? buf : "", AL_L, 240.0f);
    ImgAt(GA_TITLE_RULE_S, GL_RegleSouris);
    if (!cur) return;
    // 17 buttons + pause: tick to hold the button during the step
    for (int n = 0; n <= PAD_CELLS; n++) {
        const int c = n % 3, r = n / 3;
        const float x = 962.0f + 94.0f * c, y = 206.0f + 48.0f * r;
        sprintf(id, "##gp_tg%d", n);
        Hit h = HitRect(id, x, y, 88.0f, 46.0f);
        const bool pause = (n == PAD_CELLS);
        const bool on = pause ? (cur->mask == 0) : ((cur->mask & (1u << n)) != 0);
        Cell9((on || h.hot) ? GA_KCELL_F : GA_KCELL, x, y, 88.0f);
        if (pause) Text(g_fontGBody, 22.0f, x + 44.0f, y + 23.0f, Rgb(on ? kGold : kTxt2), Tr(STR_PAD_PAUSE), AL_M, 80.0f);
        else BadgeScaled(n, x + 44.0f, y + 23.0f, 0.95f, on ? 255 : 110);
        if (h.pressed) {
            if (pause) cur->mask = 0; else cur->mask ^= (1u << n);
            if (cur->mask == 0 && cur->ms == 0) cur->ms = pm.heldLastMs > 0 ? pm.heldLastMs : 100;   // a pause needs a time
            pm.testing = false; pm.reqSaveButtons = true;
        }
    }
    // duration: the mouse slider panel, one slider + "while held"
    const float py = 504.0f;
    SlidersPanel(956.0f, py, 72.0f, 28.0f);
    {
        const bool held = (cur->ms == 0);
        Text(g_fontGTitle, 30.0f, 966.0f, py + 20.0f, Rgb(0xF2F5F8), Tr(STR_PAD_DURATION), AL_L, 180.0f);
        if (held) strcpy(buf, Tr(STR_PAD_HELD)); else sprintf(buf, "%d ms", cur->ms);
        Text(g_fontGBodyM, 22.0f, 1232.0f, py + 22.0f, Rgb(kGold), buf, AL_R);
        const float tx = 966.0f, tw = 266.0f, ry = py + 44.0f;
        Hit h = HitRect("##gp_dur", tx - 10.0f, ry - 14.0f, tw + 20.0f, 34.0f);
        if (h.active) {
            cur->ms = FracToMs((io.MousePos.x / s_k - tx) / tw);
            pm.heldLastMs = cur->ms;
            pm.testing = false;
        }
        if (h.deactivated) pm.reqSaveButtons = true;
        SliderRail(tx, ry, tw, held ? 1.0f : MsToFrac(cur->ms));

        const char* lbl = Tr(STR_PAD_HELD_CHK);
        const float lw = TextW(g_fontGSmall, 20.0f, lbl), right = 1230.0f, cy = py + 85.0f, bx = right - lw - 24.0f;
        Hit hi = HitRect("##gp_held", bx - 4.0f, cy - 12.0f, lw + 32.0f, 24.0f);
        s_dl->AddRect(P(bx, cy - 8.0f), P(bx + 16.0f, cy + 8.0f), Rgb(hi.hot ? kGold : 0x8FA2B8), 3.0f * s_k, ImDrawCornerFlags_All, 1.5f);
        if (held) s_dl->AddRectFilled(P(bx + 4.0f, cy - 4.0f), P(bx + 12.0f, cy + 4.0f), Rgb(kGold), 2.0f * s_k);
        Text(g_fontGSmall, 20.0f, right, cy, Rgb(hi.hot ? kGold : kTxt2), lbl, AL_R);
        if (hi.pressed) {
            if (held) cur->ms = pm.heldLastMs > 0 ? pm.heldLastMs : 80;
            else { pm.heldLastMs = cur->ms; cur->ms = 0; }
            if (cur->mask == 0 && cur->ms == 0) cur->ms = 100;
            pm.testing = false; pm.reqSaveButtons = true;
        }
    }
    Text(g_fontGSmall, 18.0f, 962.0f, 626.0f, Rgb(kTxt3), Tr(STR_PAD_ED_HINT), AL_L, 276.0f);
}

void DrawUiGlossyPad(GlossyPadModel& pm, int& activeTab, float screenW, float screenH) {
    (void)screenH;
    s_k  = screenW / 1280.0f;
    s_dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    char buf[128];
    static unsigned s_prevMask = 0;

    if (s_menu == 2 && pm.padCount == 0) s_menu = -1;
    s_blockHits = (s_menu >= 0);
    HeaderAndTabs(activeTab, false);

    // ---- device bar: the same chips as the keyboard & mouse page ---------------
    ImgAt(GA_BAR_INFO, GL_BarreInfo);
    {
        const GlRect* chip[2] = { &GL_PuceClavier, &GL_PuceSouris };
        const GlRect* ico[2]  = { &GL_IcoClavier, &GL_IcoSouris };
        const GlRect* dot[2]  = { &GL_DotClavier, &GL_DotSouris };
        const GlRect* chev[2] = { &GL_ChevronClavier, &GL_ChevronSouris };
        const int     icoId[2] = { GA_ICON_KB, GA_ICON_MS };
        const bool    has[2] = { pm.hasKeyboard, pm.hasMouse };
        for (int d = 0; d < 2; d++) {
            sprintf(buf, "##gp_dev%d", d);
            Hit h = HitAt(buf, *chip[d]);
            ImgAt(h.hot ? GA_DEV_CHIP_ON : GA_DEV_CHIP, *chip[d]);
            ImgAt(icoId[d], *ico[d]);
            ImgAt(has[d] ? GA_DOT_ON : GA_DOT_RED, *dot[d]);
            ImgAt(GA_CHEVRON, *chev[d]);
            if (h.pressed) pm.reqBack = d;   // back to the keyboard & mouse page
        }
        bool any = false;
        for (int i = 0; i < pm.padCount; i++) any = any || pm.pads[i].active;
        if (PadChip(true, any)) s_menu = (s_menu == 2) ? -1 : 2;
    }

    const unsigned newBits = pm.liveMask & ~s_prevMask;
    s_prevMask = pm.liveMask;
    if (pm.editMacro >= 0) PadEditorPage(pm, now, io, newBits);
    if (pm.editMacro < 0)  PadMainPage(pm, now, io, newBits);

    // ---- footer: version and plugin state ----------------------------------------
    {
        sprintf(buf, "v%s", pm.appVersion ? pm.appVersion : "?");
        float x = GL_TxtVersionBas.x;
        x += Text(g_fontGBodyM, 24.0f, x, GL_TxtVersionBas.y, Rgb(0xFFFFFF), buf) + 18.0f;
        Img(pm.pluginAlive ? GA_DOT_ON : GA_DOT_RED, x, GL_DotPlugin.y);
        Text(g_fontGBody, 24.0f, x + 18.0f, GL_TxtVersionBas.y, Rgb(pm.pluginAlive ? kTxt2 : 0xF08A80),
             Tr(pm.pluginAlive ? STR_UI_PLUGIN_ALIVE : STR_UI_PLUGIN_SILENT));
    }

    // ---- pad drop-down, drawn last (on top) ---------------------------------------
    if (s_menu == 2) {
        s_blockHits = false;
        bool refresh = false;
        const int picked = DeviceMenu(2, pm.pads, pm.padCount, pm.sel, GL_PucePad, refresh);
        if (picked >= 0) pm.reqOpenPad = picked;
        if (refresh) pm.reqRefresh = true;
    }
    s_blockHits = false;
}
