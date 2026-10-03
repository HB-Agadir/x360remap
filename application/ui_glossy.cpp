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
#include <cfloat>
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
    ImgAt(GA_TABBAR, GL_Onglets);
    {
        const StrId tabIds[4] = { STR_GLOSSY_TAB_KBMOUSE, STR_GLOSSY_TAB_GAMES, STR_GLOSSY_TAB_HOWTO, STR_GLOSSY_TAB_SUPPORT };
        const float tw = GL_OngletActif.w;
        for (int i = 0; i < 4; i++) {
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
        const UiDeviceEntry* list = d == 0 ? m.keyboards : m.mice;
        const int count = (list && (d == 0 ? m.keyboardCount : m.mouseCount) > 0) ? (d == 0 ? m.keyboardCount : m.mouseCount) : 0;
        int& sel = d == 0 ? m.keyboardIndex : m.mouseIndex;
        const GlRect& c0 = d == 0 ? GL_PuceClavier : GL_PuceSouris;
        const float rowH = 40.0f, mw = 380.0f;
        const int rows = (count > 0 ? count : 1) + 1;   // + ligne "Actualiser"
        const float mx = c0.x + c0.w * 0.5f - mw * 0.5f, my = c0.y + c0.h + 8.0f;
        const float mh = rows * rowH + 12.0f;

        // ombre, fond, bordure
        s_dl->AddRectFilled(P(mx - 4.0f, my + 2.0f), P(mx + mw + 4.0f, my + mh + 10.0f), IM_COL32(0, 0, 0, 90), 8.0f * s_k);
        s_dl->AddRectFilled(P(mx, my), P(mx + mw, my + mh), Rgb(0x0F1824, 248), 5.0f * s_k);
        s_dl->AddRect(P(mx, my), P(mx + mw, my + mh), Rgb(0x6CA2DE, 200), 5.0f * s_k, ImDrawCornerFlags_All, 1.0f);
        s_dl->AddLine(P(mx + 10.0f, my + 1.0f), P(mx + mw - 10.0f, my + 1.0f), Rgb(0xBFE2FF, 160), 1.0f);

        bool picked = false;
        if (count == 0) {
            Text(g_fontGBody, 24.0f, mx + 16.0f, my + 6.0f + rowH * 0.5f, Rgb(kTxt3), Tr(STR_GLOSSY_NONE));
        }
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
            if (h.pressed) { sel = i; picked = true; }
        }

        {
            // Derniere ligne : actualiser la liste (les appareils branches
            // apparaissent aussi tout seuls, voir main.cpp).
            const float ry = my + 6.0f + rowH * (rows - 1);
            s_dl->AddLine(P(mx + 10.0f, ry), P(mx + mw - 10.0f, ry), Rgb(0x2C3B4F), 1.0f);
            Hit h = HitRect("##gl_menu_refresh", mx + 4.0f, ry, mw - 8.0f, rowH);
            if (h.hot) s_dl->AddRectFilled(P(mx + 4.0f, ry + 2.0f), P(mx + mw - 4.0f, ry + rowH - 2.0f), Rgb(kGold, 38), 3.0f * s_k);
            Text(g_fontGBody, 24.0f, mx + 34.0f, ry + rowH * 0.5f, Rgb(h.hot ? kGold : kTxt2), Tr(STR_GLOSSY_REFRESH));
            Text(g_fontGBodyM, 24.0f, mx + 16.0f, ry + rowH * 0.5f, Rgb(h.hot ? kGold : kTxt2), "\xE2\x80\xA6", AL_M);
            if (h.pressed) { m.requestRefreshDevices = true; picked = true; }
        }

        // Fermeture : choix fait, B a la manette, ou clic en dehors du menu.
        const ImVec2 mp = io.MousePos;
        const bool inside = mp.x >= mx * s_k && mp.x <= (mx + mw) * s_k && mp.y >= (c0.y) * s_k && mp.y <= (my + mh) * s_k;
        if (picked || (ImGui::IsMouseClicked(0) && !inside))
            s_menu = -1;
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
    gm.reqSave = gm.reqDelete = gm.reqSelect = gm.reqEditName = gm.reqSaveExplicit = gm.reqDeleteAll = false;

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
