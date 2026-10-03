#include "ui_mapping.h"
#include "theme.h"
#include "i18n.h"
#include "ui_layout.h"
#include <cmath>

UiMappingModel::UiMappingModel()
    : mode(0), langIndex(0), keyboardIndex(0), mouseIndex(0),
      keyboards(0), keyboardCount(0), mice(0), mouseCount(0),
      detectedCount(0), pluginAlive(false),
      texKeyboardSide(0), texKbW(0), texKbH(0),
      texMouseSide(0), texMsW(0), texMsH(0),
      selection(),
      grid(0), gridCount(0),
      sensitivity(0), sensitivityMin(0), sensitivityMax(1),
      deadzone(0), deadzoneMin(0), deadzoneMax(1),
      antiDeadzone(0), antiDeadzoneMin(0), antiDeadzoneMax(1),
      antiDeadzoneY(0), smoothing(0), smoothingMax(1),
      invertY(0),
      mouseDpiMeasured(0),
      buttonTargets(0), buttonTargetCount(0),
      btnLeft(0), btnRight(0), btnMiddle(0), wheelForward(0), wheelBackward(0),
      buttonTargetBadge(0), appVersion(0),
      hasSavedDefault(false), requestSaveDefault(false), requestRestoreDefault(false),
      advancedOpen(false), fallbackActive(false),
      wizardActive(false), wizardStepIndex(-1), wizardStepTotal(0), wizardResultsCount(0),
      wizardPrompt(0), wizardMessage(0), wizardNoKeyboard(false),
      wizardComplete(false), wizardCancelled(false), wizardSaveOk(false),
      requestWizardClose(false),
      manualMode(false), manualCaptureIndex(-1), manualPrompt(0), manualMessage(0), manualNoKeyboard(false),
      requestToggleManualMode(false), requestManualAssignIndex(-1),
      requestStartCalibration(false), requestMeasureMouse(false), requestSaveButtons(false), requestFallback(-1),
      requestStartWizard(false), requestRefreshDevices(false),
      requestSaveMouse(false), requestAdvanced(false) {}

// ---------------------------------------------------------------------------
// Briques de dessin
//
// Volontairement dessinees au trait via ImDrawList plutot qu'assemblees a
// partir de widgets standards : c'est ce qui permet d'obtenir l'aspect de la
// maquette (cartes arrondies, pastilles a halo, soulignement d'onglet) sans
// aucune image. Et c'est aussi la voie pour les ajouts prevus - icones
// vectorielles, QR code (une grille de carres), liens animes.
// ---------------------------------------------------------------------------
const float kUiSafeX = 0.045f;
const float kUiSafeY = 0.055f;

// --- Skin (textures optionnelles) -------------------------------------------
// Un seul pointeur, remplace une fois par frame par l'appelant (voir
// UiSetSkin dans ui_mapping.h). 0 = aucun skin charge, tout retombe sur le
// dessin procedural existant - c'est le cas de la previsualisation, qui
// n'appelle jamais UiSetSkin.
static const UiSkinTex* g_skinTex = 0;

void UiSetSkin(const UiSkinTex* tex) { g_skinTex = tex; }

namespace {

const UiSkinTex& Skin(UiSkinSlot slot) {
    static UiSkinTex empty;
    if (!g_skinTex) return empty;
    return g_skinTex[slot];
}

// --- Rendu 9-slice -----------------------------------------------------------
// Decoupe une texture en 9 morceaux : les 4 coins gardent leur taille SOURCE
// (sliceL/T/R/B, en pixels de la texture), les 4 bords s'etirent sur un seul
// axe, le centre sur les deux. C'est ce qui permet a une petite source
// (128x128) de couvrir un panneau bien plus grand sans flou ni repetition
// visible sur les coins - meme principe que les jeux SNES/Megadrive, qui
// composaient deja leurs decors a partir de tuiles minuscules repetees, pour
// la meme raison de memoire (remarque de HB, 2026-08-14 - voir
// _design_refs/DESIGN_SYSTEM.md section 10).
//
// Marges a 0 partout (icones, logo) : les 4 tranches de coin/bord ont une
// largeur ou hauteur nulle et sont ignorees, il ne reste que le centre -
// dessine alors comme une image normale, etiree pour remplir tout le
// rectangle demande. Pas de cas particulier a ecrire pour ces assets-la.
void Draw9Slice(const UiSkinTex& t, ImVec2 pos, ImVec2 size, ImU32 tint = IM_COL32_WHITE) {
    if (!t.present || !t.tex || t.w <= 0 || t.h <= 0) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float l = (float)t.sliceL, top = (float)t.sliceT, r = (float)t.sliceR, b = (float)t.sliceB;
    // Garde-fou : si jamais on demandait un rectangle plus petit que ses
    // propres marges (ne devrait pas arriver avec les tailles actuelles), on
    // les reduit proportionnellement plutot que de laisser le centre
    // basculer en largeur/hauteur negative.
    if (l + r > size.x && (l + r) > 0.0f) { float s = size.x / (l + r); l *= s; r *= s; }
    if (top + b > size.y && (top + b) > 0.0f) { float s = size.y / (top + b); top *= s; b *= s; }

    const float ul = l / (float)t.w, ur = r / (float)t.w;
    const float ut = top / (float)t.h, ub = b / (float)t.h;

    const float xs[4] = { pos.x, pos.x + l, pos.x + size.x - r, pos.x + size.x };
    const float ys[4] = { pos.y, pos.y + top, pos.y + size.y - b, pos.y + size.y };
    const float us[4] = { 0.0f, ul, 1.0f - ur, 1.0f };
    const float vs[4] = { 0.0f, ut, 1.0f - ub, 1.0f };

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            ImVec2 a(xs[col], ys[row]);
            ImVec2 bq(xs[col + 1], ys[row + 1]);
            if (bq.x <= a.x || bq.y <= a.y) continue;
            dl->AddImage(t.tex, a, bq, ImVec2(us[col], vs[row]), ImVec2(us[col + 1], vs[row + 1]), tint);
        }
    }
}

ImU32 Col(const ImVec4& c)                  { return ImGui::GetColorU32(c); }
ImU32 Col(float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
ImU32 Accent(float a)                       { return Col(ImVec4(kColorKeyboardAccent.x, kColorKeyboardAccent.y, kColorKeyboardAccent.z, a)); }

// Icones dessinees au trait plutot qu'importees : aucune image a embarquer,
// elles s'adaptent a n'importe quelle taille et prennent la couleur qu'on leur
// donne. ICON_LIST / ICON_HELP / ICON_HEART ont ete ajoutees le 2026-08-14
// pour les onglets - un menu de quatre libelles seuls est difficile a parcourir
// du regard a 2-3 m d'une TV.
enum IconKind {
    ICON_KEYBOARD, ICON_MOUSE, ICON_GEAR, ICON_PLAY,
    ICON_LIST, ICON_HELP, ICON_HEART
};

// Correspondance explicite plutot qu'une hypothese d'ordre partage entre
// IconKind (prive a ce fichier) et UiSkinSlot (ui_mapping.h) : un cran de
// decalage entre les deux enums serait invisible a la compilation, cette
// fonction le rend impossible.
UiSkinSlot IconSkinSlot(IconKind k) {
    switch (k) {
    case ICON_KEYBOARD: return SKIN_ICON_KEYBOARD;
    case ICON_MOUSE:    return SKIN_ICON_MOUSE;
    case ICON_GEAR:     return SKIN_ICON_GEAR;
    case ICON_PLAY:     return SKIN_ICON_PLAY;
    case ICON_LIST:     return SKIN_ICON_LIST;
    case ICON_HELP:     return SKIN_ICON_HELP;
    case ICON_HEART:    return SKIN_ICON_HEART;
    }
    return SKIN_ICON_KEYBOARD;
}

void DrawIcon(IconKind kind, ImVec2 c, float size, ImU32 col) {
    const UiSkinTex& iconTex = Skin(IconSkinSlot(kind));
    if (iconTex.present) {
        Draw9Slice(iconTex, ImVec2(c.x - size * 0.5f, c.y - size * 0.5f), ImVec2(size, size), col);
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = size * 0.5f;
    switch (kind) {
    case ICON_KEYBOARD:
        dl->AddRect(ImVec2(c.x - s, c.y - s * 0.62f), ImVec2(c.x + s, c.y + s * 0.62f), col, 2.0f, 0, 1.4f);
        for (int i = -2; i <= 2; i++)
            dl->AddCircleFilled(ImVec2(c.x + i * s * 0.34f, c.y - s * 0.16f), 1.1f, col);
        dl->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.26f), ImVec2(c.x + s * 0.42f, c.y + s * 0.26f), col, 1.4f);
        break;
    case ICON_MOUSE:
        dl->AddRect(ImVec2(c.x - s * 0.52f, c.y - s), ImVec2(c.x + s * 0.52f, c.y + s), col, s * 0.52f, 0, 1.4f);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.55f), ImVec2(c.x, c.y - s * 0.15f), col, 1.4f);
        break;
    case ICON_GEAR:
        // 8 dents, pas 6 : moins de dents rendait l'icone proche d'un
        // asterisque plutot que d'un engrenage reconnaissable. Reprend
        // exactement la disposition de la maquette HTML (cercle + 8 segments
        // aux angles 0/45/90.../315), qui elle-meme lisait bien a cette taille.
        dl->AddCircle(c, s * 0.30f, col, 12, 1.4f);
        for (int i = 0; i < 8; i++) {
            float a = i * 3.14159f * 0.25f;
            dl->AddLine(ImVec2(c.x + cosf(a) * s * 0.55f, c.y + sinf(a) * s * 0.55f),
                        ImVec2(c.x + cosf(a) * s * 0.92f, c.y + sinf(a) * s * 0.92f), col, 1.5f);
        }
        break;
    case ICON_PLAY:
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.35f, c.y - s * 0.55f),
                              ImVec2(c.x - s * 0.35f, c.y + s * 0.55f),
                              ImVec2(c.x + s * 0.55f, c.y), col);
        break;
    case ICON_LIST:   // Profils : trois lignes precedees d'une puce
        for (int i = -1; i <= 1; i++) {
            float y = c.y + i * s * 0.55f;
            dl->AddCircleFilled(ImVec2(c.x - s * 0.72f, y), 1.5f, col);
            dl->AddLine(ImVec2(c.x - s * 0.35f, y), ImVec2(c.x + s * 0.8f, y), col, 1.4f);
        }
        break;
    case ICON_HELP:   // Comment ca marche : point d'interrogation cercle
        dl->AddCircle(c, s * 0.85f, col, 16, 1.4f);
        dl->AddBezierCurve(ImVec2(c.x - s * 0.28f, c.y - s * 0.22f),
                           ImVec2(c.x - s * 0.22f, c.y - s * 0.62f),
                           ImVec2(c.x + s * 0.34f, c.y - s * 0.52f),
                           ImVec2(c.x, c.y + s * 0.06f), col, 1.4f, 12);
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.42f), 1.5f, col);
        break;
    case ICON_HEART:  // Soutenir le projet
        dl->AddCircleFilled(ImVec2(c.x - s * 0.34f, c.y - s * 0.22f), s * 0.36f, col);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.34f, c.y - s * 0.22f), s * 0.36f, col);
        dl->AddTriangleFilled(ImVec2(c.x - s * 0.68f, c.y - s * 0.10f),
                              ImVec2(c.x + s * 0.68f, c.y - s * 0.10f),
                              ImVec2(c.x, c.y + s * 0.72f), col);
        break;
    }
}

// Ecrit un texte en le TRONQUANT avec des points de suspension s'il depasse
// la largeur donnee.
//
// ImGui ne le fait pas tout seul : un texte trop long est simplement coupe net
// par la zone parente, sans rien pour signaler qu'il manque quelque chose - ce
// qui donnait "0461:3F41 (name una" a l'ecran. Mesurer avec CalcTextSize et
// couper nous-memes rend la troncature visible et volontaire.
//
// Ce cas se produira plus souvent en francais qu'en anglais : les libelles y
// sont typiquement 15 a 25% plus longs.
void TextTruncated(const char* txt, float maxWidth) {
    if (ImGui::CalcTextSize(txt).x <= maxWidth) {
        ImGui::TextUnformatted(txt);
        return;
    }
    char buf[96];
    int n = 0;
    while (txt[n] && n < (int)sizeof(buf) - 4) {
        buf[n] = txt[n];
        buf[n + 1] = '\0';
        char probe[100];
        int k = 0;
        for (; buf[k]; k++) probe[k] = buf[k];
        probe[k] = '.'; probe[k+1] = '.'; probe[k+2] = '.'; probe[k+3] = '\0';
        if (ImGui::CalcTextSize(probe).x > maxWidth) { buf[n] = '\0'; break; }
        n++;
    }
    char out[100];
    int k = 0;
    for (; buf[k]; k++) out[k] = buf[k];
    out[k] = '.'; out[k+1] = '.'; out[k+2] = '.'; out[k+3] = '\0';
    ImGui::TextUnformatted(out);
}

// --- Palette, reprise a l'identique de la maquette -------------------------
// Ecrite une fois ici plutot que dispersee en litteraux : c'est la meme
// discipline que la grille. Une couleur se change a un seul endroit.
const ImVec4 kBg      = ImVec4(0.039f, 0.039f, 0.047f, 1.0f); // #0A0A0C
const ImVec4 kPanel   = ImVec4(0.075f, 0.078f, 0.090f, 1.0f); // #131417
const ImVec4 kPanel2  = ImVec4(0.090f, 0.098f, 0.118f, 1.0f); // #17191E
const ImVec4 kLine    = ImVec4(0.137f, 0.149f, 0.173f, 1.0f); // #23262C
const ImVec4 kLine2   = ImVec4(0.180f, 0.196f, 0.227f, 1.0f); // #2E323A
const ImVec4 kTxt     = ImVec4(0.910f, 0.918f, 0.929f, 1.0f); // #E8EAED
const ImVec4 kTxt2    = ImVec4(0.604f, 0.631f, 0.667f, 1.0f); // #9AA1AA
const ImVec4 kTxt3    = ImVec4(0.400f, 0.424f, 0.459f, 1.0f); // #666C75
const ImVec4 kWarn    = ImVec4(1.000f, 0.667f, 0.157f, 1.0f); // #FFAA28

// Rayons d'arrondi de la maquette : 8 panneaux, 6 cartes, 4 petits elements.
const float kRLg = 8.0f, kRMd = 6.0f, kRSm = 4.0f;

// Nombre de lignes de reglage souris. Declare une fois : la pile de gauche,
// celle de droite et la resolution de la selection s'y referent toutes les
// trois, donc ajouter une ligne se fait a un seul endroit.
const int kMouseRowCount = 8;

// Ecrit un entier en decimal. Existe parce que ce fichier doit compiler HORS
// console (previsualisation g++) : _snprintf est propre a MSVC et <cstdio>
// n'a rien a faire ici pour si peu.
void IntToStr(int v, char* out, int cap) {
    int n = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    char tmp[16]; int k = 0;
    if (v == 0) tmp[k++] = '0';
    while (v > 0 && k < 15) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
    while (k > 0 && n < cap - 1) out[n++] = tmp[--k];
    out[n] = '\0';
}

// --- Texte espace (letter-spacing) -----------------------------------------
// La maquette repose beaucoup sur des capitales espacees pour les titres de
// section - c'est ce qui les distingue du texte courant sans changer de
// graisse. ImGui n'a pas de letter-spacing : on dessine lettre par lettre.
float TrackedWidth(const char* s, float spacing) {
    float w = 0.0f;
    for (int i = 0; s[i]; i++) {
        char c[2]; c[0] = s[i]; c[1] = '\0';
        w += ImGui::CalcTextSize(c).x + spacing;
    }
    return w > 0.0f ? w - spacing : 0.0f;
}

void DrawTracked(ImVec2 p, const char* s, float spacing, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; s[i]; i++) {
        char c[2]; c[0] = s[i]; c[1] = '\0';
        dl->AddText(p, col, c);
        p.x += ImGui::CalcTextSize(c).x + spacing;
    }
}

// Passe en capitales SANS toucher a la chaine d'origine.
//
// Les titres de section sont en capitales dans la maquette, mais les chaines
// traduites, elles, sont ecrites normalement (i18n.cpp) : elles servent aussi
// ailleurs, en toutes lettres. Les stocker deja en capitales obligerait a
// dupliquer chaque libelle. On convertit donc a l'affichage.
//
// Volontairement limite a l'ASCII : le francais met rarement les accents sur
// les capitales, et une table Latin-1 complete ne servirait que ces titres.
const char* Upper(const char* s, char* buf, int cap) {
    int i = 0;
    for (; s[i] && i < cap - 1; i++) {
        char c = s[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    buf[i] = '\0';
    return buf;
}

// Titre de section : un SEUL style dans toute l'application (capitales,
// petite taille, espacement 1.5). Consomme la hauteur d'une ligne.
void SectionTitle(const char* txt, const ImVec4& col, float spacing = 1.5f) {
    char up[96];
    Upper(txt, up, sizeof(up));
    PushX360Font(g_fontSmall);
    ImVec2 p = ImGui::GetCursorScreenPos();

    // Coupe si le titre depasse la zone. Les titres traduits peuvent etre
    // longs ("LECTURE AUTOMATIQUE ACTIVE (RECOMMANDE)") et l'espacement des
    // lettres les allonge encore : sans ca, ils sortaient du panneau.
    const float avail = ImGui::GetContentRegionAvail().x;
    if (TrackedWidth(up, spacing) > avail) {
        int n = 0;
        while (up[n]) {
            char save = up[n + 1];
            up[n + 1] = '\0';
            float w = TrackedWidth(up, spacing);
            up[n + 1] = save;
            if (w > avail) { up[n] = '\0'; break; }
            n++;
        }
    }

    DrawTracked(p, up, spacing, Col(col));
    ImGui::Dummy(ImVec2(TrackedWidth(up, spacing), ImGui::GetTextLineHeight()));
    PopX360Font(g_fontSmall);
}

// Titre de groupe dans la colonne de gauche : libelle + filet qui remplit la
// largeur restante. Le filet evite d'avoir deux titres qui flottent dans le
// vide sans hierarchie visible.
void GroupLabel(const char* raw) {
    char txt[96];
    Upper(raw, txt, sizeof(txt));
    PushX360Font(g_fontSmall);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    float tw = TrackedWidth(txt, 1.5f);
    DrawTracked(p, txt, 1.5f, Col(kTxt3));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x + tw + 8.0f, p.y + ImGui::GetTextLineHeight() * 0.5f),
        ImVec2(p.x + avail,     p.y + ImGui::GetTextLineHeight() * 0.5f), Col(kLine), 1.0f);
    ImGui::Dummy(ImVec2(avail, ImGui::GetTextLineHeight()));
    PopX360Font(g_fontSmall);
}

// --- Materiau : ombre portee + lavis superieur -----------------------------
//
// POURQUOI (2026-08-14, HB, graphiste : "ca ne me plait pas du tout" - tout
// est plat). Jusqu'ici chaque surface etait un unique AddRectFilled : une
// couleur, un bord 1px, rien d'autre. C'est ce plat qui fait "wireframe qui
// compile" plutot que produit fini, a cote d'une vraie photo de manette.
//
// ImGui 1.72b n'a pas de flou (pas de box-shadow, pas de gaussien). On simule
// une ombre douce en empilant deux copies decalees et semi-transparentes de
// la MEME silhouette arrondie sous la surface - la technique classique du
// "poor man's blur" : plus une copie est grande et decalee, plus elle doit
// etre transparente, ce qui donne un degrade de bord approximatif sans achat
// de flou reel. Et un lavis clair sur la moitie haute (AddRectFilled avec
// ImDrawCornerFlags_Top, alpha tres faible) simule un eclairage zenithal -
// c'est ce qui donne l'impression de matiere plutot que de plat peint.
//
// Les DEUX techniques respectent la silhouette arrondie parce qu'elles
// utilisent le MEME rayon que la surface elle-meme (ombre) ou ne rognent que
// les coins hauts avec le meme rayon (lavis) - un degrade classique
// (AddRectFilledMultiColor) dessine un quad a coins carres qui aurait
// "carre" les coins arrondis en les recouvrant.
void PanelShadow(ImVec2 pos, ImVec2 size, float rounding) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(pos.x - 2.0f, pos.y + 5.0f),
                      ImVec2(pos.x + size.x + 2.0f, pos.y + size.y + 9.0f),
                      Col(0.0f, 0.0f, 0.0f, 0.10f), rounding + 3.0f);
    dl->AddRectFilled(ImVec2(pos.x, pos.y + 2.0f),
                      ImVec2(pos.x + size.x, pos.y + size.y + 4.0f),
                      Col(0.0f, 0.0f, 0.0f, 0.16f), rounding);
}

void TopSheen(ImVec2 pos, ImVec2 size, float rounding, float alpha = 0.05f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y * 0.55f),
                      Col(1.0f, 1.0f, 1.0f, alpha), rounding, ImDrawCornerFlags_Top);
}

// Lueur douce derriere une bordure active (2026-08-14, retour visuel : le
// vert des bordures actives est juste, mais lit "trait" plutot que "neon").
// Pas de vrai flou en ImGui 1.72b - meme famille de technique que
// PanelShadow/TopSheen : deux copies du meme contour, plus grandes et plus
// transparentes que le trait final, empilees AVANT lui. Reserve aux
// elements d'accent les plus visibles (bouton d'action principal, panneau
// contextuel) - l'appliquer aux 21 cases de la grille noierait l'effet et
// coûterait des passes de dessin pour rien.
void GlowBorder(ImVec2 pos, ImVec2 size, float rounding, const ImVec4& color, float spread = 10.0f,
                 ImDrawList* targetDl = nullptr) {
    ImDrawList* dl = targetDl ? targetDl : ImGui::GetWindowDrawList();
    // Halo large qui s'estompe vers l'exterieur : plusieurs bandes empilees
    // (ImGui 1.72b n'a pas de flou natif) pour simuler un vrai glow neon qui
    // bave largement au-dela du contour, comme sur la maquette de reference
    // (ecart mesure : le halo precedent, spread=3, restait quasi colle au
    // trait ; celui-ci s'etale sur ~10px avec un vrai degrade d'intensite).
    const int kPasses = 5;
    for (int i = kPasses; i >= 1; i--) {
        float t = (float)i / (float)kPasses;
        float s = spread * t;
        float a = 0.22f * (1.0f - t) + 0.03f;
        float thick = (s * 0.5f > 1.5f) ? s * 0.5f : 1.5f;
        dl->AddRect(ImVec2(pos.x - s, pos.y - s),
                   ImVec2(pos.x + size.x + s, pos.y + size.y + s),
                   Col(ImVec4(color.x, color.y, color.z, a)), rounding + s, 0, thick);
    }
}

// --- Panneaux ---------------------------------------------------------------
// Le fond est dessine sur la fenetre PARENTE, puis la zone enfant est creee
// par-dessus sans bordure : ImGui ne sait pas donner un coin arrondi a un
// BeginChild, et le faire soi-meme est la seule facon d'obtenir l'aspect de
// la maquette.
void PanelBg(ImVec2 pos, ImVec2 size, const ImVec4& border, const ImVec4& fill,
            UiSkinSlot skinSlot = SKIN_PANEL_DEFAULT, bool glow = false) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // La lueur (retour visuel 2026-08-14 : "la boite BOUTON A manque d'un
    // conteneur bien ancre") se dessine sur la couleur PLEINE de la bordure,
    // pas sur sa version deja rendue semi-transparente (border peut arriver
    // ici a 40% d'opacite pour le panneau contextuel) - sinon la lueur
    // serait deux fois trop faible.
    if (glow) GlowBorder(pos, size, kRLg, ImVec4(border.x, border.y, border.z, 1.0f));
    const UiSkinTex& t = Skin(skinSlot);
    if (t.present) {
        // L'image porte deja sa propre matiere (bakee ou non selon le skin) :
        // pas d'ombre/lavis procedural par-dessus, ca doublerait l'effet. La
        // bordure reste procedurale - jamais de couleur d'accent cuite dans
        // un fichier (voir ui_assets.h), le panneau contextuel doit pouvoir
        // changer de couleur (vert/bleu) sans changer de fichier.
        Draw9Slice(t, pos, size);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Col(border), kRLg, 0, 1.0f);
        return;
    }
    PanelShadow(pos, size, kRLg);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), Col(fill), kRLg);
    TopSheen(pos, size, kRLg);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Col(border), kRLg, 0, 1.0f);
}

void BeginPanel(const char* id, ImVec2 pos, ImVec2 size,
                const ImVec4& border = kLine, float pad = 16.0f,
                UiSkinSlot skinSlot = SKIN_PANEL_DEFAULT, bool glow = false) {
    PanelBg(pos, size, border, kPanel, skinSlot, glow);
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoScrollbar);
}

void EndPanel() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// Carte de peripherique cliquable. Renvoie true si l'utilisateur la choisit.
bool DeviceCard(IconKind icon, const UiDeviceEntry& d, bool selected,
                const ImVec4& accent, int uid) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;

    // Hauteur CALCULEE sur les vraies metriques de police plutot que fixee a
    // 42px (valeur qui datait d'une police plus petite) : au gabarit
    // actuel, le nom (police normale) et l'identifiant (police petite)
    // empiles depassaient 42px et l'identifiant chevauchait le nom (retour
    // HB, 2026-08-14 : "decale les texte du clavier et son identifiant").
    const float nameLineH = ImGui::GetTextLineHeight();
    float idLineH;
    PushX360Font(g_fontSmall);
    idLineH = ImGui::GetTextLineHeight();
    PopX360Font(g_fontSmall);
    const float textGap = 2.0f;
    const float textBlockH = nameLineH + textGap + idLineH;
    const float iconH = 26.0f;
    const float padTB = 9.0f; // .dev { padding: 9px 10px }
    const float h = padTB * 2.0f + (textBlockH > iconH ? textBlockH : iconH);

    // Zone cliquable posee AVANT le dessin : ImGui a besoin d'un item reel
    // pour gerer le survol, le clic et la navigation a la manette.
    ImGui::PushID(uid);
    bool clicked = ImGui::InvisibleButton("##dev", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImU32 bg = selected ? Col(ImVec4(accent.x, accent.y, accent.z, 0.06f)) : Col(kPanel2);
    ImU32 bd = selected ? Col(ImVec4(accent.x, accent.y, accent.z, 0.45f))
                        : (hovered ? Col(kLine2) : Col(kLine));
    const UiSkinTex& cardTex = Skin(SKIN_CARD_DEVICE);
    if (cardTex.present) {
        Draw9Slice(cardTex, p, ImVec2(w, h));
        if (selected) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, kRMd);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), bd, kRMd, 0, 1.0f);
    } else {
        // Ombre courte : une carte se detache legerement du panneau qui la
        // contient, comme une tuile posee dessus plutot que peinte avec lui.
        dl->AddRectFilled(ImVec2(p.x, p.y + 1.5f), ImVec2(p.x + w, p.y + h + 2.5f),
                          Col(0.0f, 0.0f, 0.0f, 0.14f), kRMd);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, kRMd);
        TopSheen(p, ImVec2(w, h), kRMd, selected ? 0.05f : 0.035f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), bd, kRMd, 0, 1.0f);
    }

    // Pastille d'icone carree, comme dans la maquette (.dev .ic) - centree
    // verticalement dans la carte (align-items:center en CSS).
    ImVec2 icP(p.x + 10.0f, p.y + (h - iconH) * 0.5f);
    dl->AddRectFilled(icP, ImVec2(icP.x + iconH, icP.y + iconH),
                      selected ? Col(ImVec4(accent.x, accent.y, accent.z, 0.13f)) : Col(0.125f, 0.137f, 0.161f),
                      kRSm);
    DrawIcon(icon, ImVec2(icP.x + iconH * 0.5f, icP.y + iconH * 0.5f), 15.0f,
             selected ? Col(accent) : Col(kTxt2));

    // Bloc nom+identifiant, lui aussi centre verticalement (et plus bloque
    // en haut de carte) - l'identifiant ne chevauche plus le nom.
    const float textX = icP.x + iconH + 10.0f;
    const float textW = w - (textX - p.x) - 22.0f;
    const float textY = p.y + (h - textBlockH) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(textX, textY));
    TextTruncated(d.name, textW);
    ImGui::SetCursorScreenPos(ImVec2(textX, textY + nameLineH + textGap));
    PushX360Font(g_fontSmall);
    ImGui::TextDisabled("%s", d.id);
    PopX360Font(g_fontSmall);

    ImVec2 dot(p.x + w - 14.0f, p.y + h * 0.5f);
    if (d.active) {
        dl->AddCircleFilled(dot, 6.5f, Col(ImVec4(accent.x, accent.y, accent.z, 0.16f)));
        dl->AddCircleFilled(dot, 3.5f, Col(accent));
    } else {
        dl->AddCircleFilled(dot, 3.5f, Col(0.204f, 0.220f, 0.247f));
    }

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 6.0f));
    return clicked;
}

// Bouton d'action principal : cadre accentue, icone a gauche, LARGEUR MESUREE
// sur le texte (CalcTextSize) - un libelle traduit plus long ne deborde donc
// jamais de son cadre.
// Le libelle passe sur DEUX LIGNES s'il ne tient pas : "Demarrer la
// configuration clavier" fait 240 px alors que le panneau en offre 226, et le
// texte sortait du cadre. Reduire la police ou tronquer effaceraient l'action
// principale de l'ecran - le bouton grandit donc a la place. Le probleme
// s'aggrave en traduction (le francais est 15 a 25% plus long que l'anglais).
// Metriques reprises EXACTEMENT de maquette_v1.2.html .cta (source de
// verite) : height:38px, padding:0 18px, fond rgba(accent,.08) / hover
// rgba(accent,.16), bordure 1px pleine couleur, AUCUNE ombre/glow (verifie
// dans le CSS : aucune regle box-shadow sur .cta). C'est un vrai ecart
// mesure avec le rendu precedent (36px/16px/hover .18) que HB a repere en
// comparant au rendu direct de son propre fichier HTML (2026-08-14).
bool AccentButton(const char* label, IconKind icon, const ImVec4& accent, float height = 38.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    PushX360Font(g_fontSmall);
    // pad 18 -> 22 (retour HB, 2026-08-17 : "pousse les texte de 4px loin
    // des cadre aussi le bouton demarrer la configuration clavier") - le
    // texte/icone doit respirer 4px de plus par rapport au cadre du bouton.
    const float pad = 22.0f, icoW = 20.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float textMax = avail - pad * 2.0f - icoW;
    const float lineH = ImGui::GetTextLineHeight();

    // Meme discipline que SectionTitle/GroupLabel/les onglets : capitales
    // pleines sur le libelle. L'ID du InvisibleButton reste base sur le
    // "label" original (voir plus bas), donc rien ne change cote logique.
    char upperLabel[128];
    Upper(label, upperLabel, sizeof(upperLabel));

    // Coupe au dernier espace qui tient encore.
    char l1[96], l2[96];
    l1[0] = l2[0] = '\0';
    if (TrackedWidth(upperLabel, 1.0f) <= textMax) {
        int i = 0; for (; upperLabel[i] && i < 95; i++) l1[i] = upperLabel[i]; l1[i] = '\0';
    } else {
        int cut = -1, i = 0;
        char probe[96];
        for (; upperLabel[i] && i < 95; i++) {
            probe[i] = upperLabel[i]; probe[i + 1] = '\0';
            if (TrackedWidth(probe, 1.0f) > textMax) break;
            if (upperLabel[i] == ' ') cut = i;
        }
        if (cut < 0) cut = i;
        int k = 0; for (; k < cut && k < 95; k++) l1[k] = upperLabel[k]; l1[k] = '\0';
        int j = 0; for (int s = cut + 1; upperLabel[s] && j < 95; s++, j++) l2[j] = upperLabel[s];
        l2[j] = '\0';
    }

    // Largeur COLLEE AU TEXTE, pas etiree a toute la colonne : la maquette
    // le declare "inline-flex, align-self:flex-start" (.cta), pas un bloc
    // pleine largeur - retour HB, 2026-08-14 : "le cadre du bouton doit se
    // limiter aux dimensions du texte, la il les depasse".
    float textW = TrackedWidth(l1, 1.0f);
    if (l2[0]) { float w2 = TrackedWidth(l2, 1.0f); if (w2 > textW) textW = w2; }
    float w = pad * 2.0f + icoW + textW;
    if (w > avail) w = avail;
    const float h = (l2[0] ? height + lineH : height);

    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    const UiSkinTex& btnTex = Skin(SKIN_BUTTON_ACCENT);
    if (btnTex.present) {
        // Fichier NEUTRE (blanc plein, voir ASSET_PROMPTS.md) : la teinte
        // finale vient d'ici, avec la MEME opacite que l'ancien remplissage
        // procedural - vert en mode clavier, bleu en mode souris, jamais
        // cuit dans le fichier.
        Draw9Slice(btnTex, p, ImVec2(w, h),
                   Col(ImVec4(accent.x, accent.y, accent.z, hov ? 0.16f : 0.08f)));
    } else {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          Col(ImVec4(accent.x, accent.y, accent.z, hov ? 0.16f : 0.08f)), kRMd);
    }
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Col(accent), kRMd, 0, 1.0f);
    DrawIcon(icon, ImVec2(p.x + pad + 6.0f, p.y + h * 0.5f), 13.0f, Col(accent));

    float ty = p.y + (h - (l2[0] ? lineH * 2.0f : lineH)) * 0.5f;
    DrawTracked(ImVec2(p.x + pad + icoW, ty), l1, 1.0f, Col(accent));
    if (l2[0]) DrawTracked(ImVec2(p.x + pad + icoW, ty + lineH), l2, 1.0f, Col(accent));
    PopX360Font(g_fontSmall);
    return clicked;
}

// Bouton secondaire, plus discret (contour seul).
bool GhostButton(const char* label, const ImVec4& col, float height = 30.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    PushX360Font(g_fontSmall);
    // pad 14 -> 18, meme discipline que AccentButton (retour HB, 2026-08-17).
    const float pad = 18.0f;
    float tw = TrackedWidth(label, 1.0f);
    float w = pad * 2.0f + tw;
    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, height));
    bool hov = ImGui::IsItemHovered();
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height),
                               Col(ImVec4(col.x, col.y, col.z, 0.10f)), kRMd);
    const UiSkinTex& ghostTex = Skin(SKIN_BUTTON_GHOST);
    if (ghostTex.present) {
        // Fichier NEUTRE (contour blanc, interieur transparent) : teinte a la
        // couleur demandee par l'appelant, comme les icones - un seul
        // fichier sert pour tous les appels de GhostButton, quelle que soit
        // la couleur voulue.
        Draw9Slice(ghostTex, p, ImVec2(w, height), Col(col));
    } else {
        dl->AddRect(p, ImVec2(p.x + w, p.y + height), Col(col), kRMd, 0, 1.0f);
    }
    DrawTracked(ImVec2(p.x + pad, p.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                label, 1.0f, Col(col));
    PopX360Font(g_fontSmall);
    return clicked;
}

// --- Drapeaux ---------------------------------------------------------------
// Dessines au trait, pas d'image : trois rectangles suffisent pour la France,
// et un drapeau seul serait illisible a 2-3 m d'une TV - d'ou le code texte
// a cote, et un fond plein sur la langue active plutot qu'une variation
// d'opacite (invisible de loin).
void DrawFlag(ImDrawList* dl, int lang, ImVec2 p, float w, float h, float alpha) {
    ImU32 a = (ImU32)(alpha * 255.0f) << 24;
    switch (lang) {
    case 0: // FR
        dl->AddRectFilled(p, ImVec2(p.x + w / 3, p.y + h), a | 0x00A45500);
        dl->AddRectFilled(ImVec2(p.x + w / 3, p.y), ImVec2(p.x + w * 2 / 3, p.y + h), a | 0x00F5F5F5);
        dl->AddRectFilled(ImVec2(p.x + w * 2 / 3, p.y), ImVec2(p.x + w, p.y + h), a | 0x003541EF);
        break;
    case 1: // EN (Union Jack simplifie : croix rouge sur fond bleu)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), a | 0x00692101);
        dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + h), a | 0x00FFFFFF, 2.0f);
        dl->AddLine(ImVec2(p.x + w, p.y), ImVec2(p.x, p.y + h), a | 0x00FFFFFF, 2.0f);
        dl->AddLine(ImVec2(p.x + w * 0.5f, p.y), ImVec2(p.x + w * 0.5f, p.y + h), a | 0x00FFFFFF, 4.0f);
        dl->AddLine(ImVec2(p.x, p.y + h * 0.5f), ImVec2(p.x + w, p.y + h * 0.5f), a | 0x00FFFFFF, 4.0f);
        dl->AddLine(ImVec2(p.x + w * 0.5f, p.y), ImVec2(p.x + w * 0.5f, p.y + h), a | 0x002E10C8, 2.0f);
        dl->AddLine(ImVec2(p.x, p.y + h * 0.5f), ImVec2(p.x + w, p.y + h * 0.5f), a | 0x002E10C8, 2.0f);
        break;
    case 3: // ES (2026-09-13) - bandes horizontales rouge / jaune / rouge.
        // CAS EXPLICITE INDISPENSABLE : sans lui, l'index 3 tombait dans le
        // "default" ci-dessous et l'espagnol s'affichait avec le drapeau
        // bresilien. Couleurs au format ABGR comme le reste de ce fichier
        // (rouge #C60B1E -> 0x001E0BC6, jaune #FFC400 -> 0x0000C4FF).
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h * 0.25f), a | 0x001E0BC6);
        dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.25f), ImVec2(p.x + w, p.y + h * 0.75f), a | 0x0000C4FF);
        dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.75f), ImVec2(p.x + w, p.y + h), a | 0x001E0BC6);
        break;
    default: // PT-BR
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), a | 0x003A9B00);
        dl->AddQuadFilled(ImVec2(p.x + w * 0.5f, p.y + h * 0.1f), ImVec2(p.x + w * 0.92f, p.y + h * 0.5f),
                          ImVec2(p.x + w * 0.5f, p.y + h * 0.9f), ImVec2(p.x + w * 0.08f, p.y + h * 0.5f),
                          a | 0x0000DFFE);
        dl->AddCircleFilled(ImVec2(p.x + w * 0.5f, p.y + h * 0.5f), h * 0.22f, a | 0x00762700);
        break;
    }
}

// fixedW : largeur commune aux trois puces (calculee par l'appelant sur le
// code le plus large). 0 = largeur auto sur SON PROPRE contenu, gardee pour
// compatibilite mais plus utilisee par l'ecran de mapping - trois puces de
// largeurs differentes ("FR" plus large que "PT" a cette police) donnaient un
// alignement en escalier ou le drapeau ne semblait pas centre dans son cadre
// (HB, 2026-08-14). Avec une largeur commune, le groupe drapeau+code est
// centre DANS cette largeur plutot que colle a bord gauche.
bool LangChip(int lang, const char* code, bool on, float fixedW = 0.0f, float h = 24.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    PushX360Font(g_fontSmall);
    const float fw = 18.0f, fh = 13.0f;
    const float gap = 6.0f;
    float contentW = fw + gap + TrackedWidth(code, 0.8f);
    float w = fixedW > 0.0f ? fixedW : (gap + contentW + 8.0f);
    ImGui::PushID(lang);
    bool clicked = ImGui::InvisibleButton("##lang", ImVec2(w, h));
    ImGui::PopID();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                      on ? Accent(0.12f) : Col(kPanel2), kRSm);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h),
                on ? Col(kColorKeyboardAccent) : Col(kLine), kRSm, 0, 1.0f);
    // Groupe drapeau+code centre horizontalement dans la largeur totale w,
    // plutot qu'ancre a 6px du bord gauche.
    float startX = p.x + (w - contentW) * 0.5f;
    DrawFlag(dl, lang, ImVec2(startX, p.y + (h - fh) * 0.5f), fw, fh, on ? 1.0f : 0.55f);
    DrawTracked(ImVec2(startX + fw + gap, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                code, 0.8f, on ? Col(kColorKeyboardAccent) : Col(kTxt3));
    PopX360Font(g_fontSmall);
    return clicked;
}

// --- Bascule segmentee CLAVIER / SOURIS ------------------------------------
// Largeur calculee sur le texte le plus long des deux, pour que la bascule ne
// change pas de taille selon la langue.
bool Segmented(const char* a, const char* b, int& value, float h = 28.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    PushX360Font(g_fontSmall);
    float wa = TrackedWidth(a, 1.1f), wb = TrackedWidth(b, 1.1f);
    float seg = (wa > wb ? wa : wb) + 32.0f;
    float total = seg * 2.0f + 9.0f;

    bool changed = false;
    ImGui::PushID("##seg");
    if (ImGui::InvisibleButton("##segbtn", ImVec2(total, h))) {
        int v = (ImGui::GetIO().MousePos.x - p.x < total * 0.5f) ? 0 : 1;
        if (v != value) { value = v; changed = true; }
    }
    ImGui::PopID();

    dl->AddRectFilled(p, ImVec2(p.x + total, p.y + h), Col(0.059f, 0.067f, 0.078f), kRMd);
    dl->AddRect(p, ImVec2(p.x + total, p.y + h), Col(kLine), kRMd, 0, 1.0f);

    const char*   lbl[2] = { a, b };
    const ImVec4  acc[2] = { kColorKeyboardAccent, kColorMouseAccent };
    for (int i = 0; i < 2; i++) {
        ImVec2 sp(p.x + 3.0f + (seg + 3.0f) * i, p.y + 3.0f);
        ImVec2 se(sp.x + seg, p.y + h - 3.0f);
        if (value == i) {
            dl->AddRectFilled(sp, se, Col(acc[i]), kRSm);
            TopSheen(sp, ImVec2(se.x - sp.x, se.y - sp.y), kRSm, 0.10f);
        }
        float tw = TrackedWidth(lbl[i], 1.1f);
        DrawTracked(ImVec2(sp.x + (seg - tw) * 0.5f, sp.y + (h - 6.0f - ImGui::GetTextLineHeight()) * 0.5f),
                    lbl[i], 1.1f,
                    value == i ? Col(kBg) : Col(kTxt3));
    }
    PopX360Font(g_fontSmall);
    return changed;
}

// --- Cellule de la grille des assignations ---------------------------------
void GridCell(ImVec2 p, ImVec2 size, const UiGridEntry& e, bool selected) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UiSkinTex& cellTex = Skin(SKIN_CELL_GRID);
    if (cellTex.present) {
        Draw9Slice(cellTex, p, size);
        if (selected) dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Accent(0.08f), kRMd);
        dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y),
                    selected ? Col(kColorKeyboardAccent) : Col(kLine), kRMd, 0, 1.0f);
    } else {
        // Ombre tres courte : 21 cases plates cote a cote donnaient un
        // damier peint. Un liseret d'ombre suffit a les faire lire comme des
        // touches physiques plutot que des cases de tableau.
        dl->AddRectFilled(ImVec2(p.x, p.y + 1.0f), ImVec2(p.x + size.x, p.y + size.y + 1.5f),
                          Col(0.0f, 0.0f, 0.0f, 0.12f), kRMd);
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                          selected ? Accent(0.08f) : Col(kPanel2), kRMd);
        TopSheen(p, size, kRMd, selected ? 0.05f : 0.035f);
        dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y),
                    selected ? Col(kColorKeyboardAccent) : Col(kLine), kRMd, 0, 1.0f);
    }

    // Couleur des quatre boutons de face, comme sur la manette reelle.
    ImVec4 bc = kTxt;
    if      (e.button[0] == 'A' && e.button[1] == '\0') bc = kColorKeyboardAccent;
    else if (e.button[0] == 'B' && e.button[1] == '\0') bc = ImVec4(0.878f, 0.294f, 0.294f, 1.0f);
    else if (e.button[0] == 'X' && e.button[1] == '\0') bc = kColorMouseAccent;
    else if (e.button[0] == 'Y' && e.button[1] == '\0') bc = ImVec4(0.878f, 0.776f, 0.098f, 1.0f);

    // Les deux libelles sont CENTRES ET TRONQUES sur la largeur de la cellule.
    // Sans troncature, "Left stick - Right" debordait sur ses voisines et la
    // grille devenait illisible ; ImGui ne coupe pas tout seul, il laisse
    // simplement le texte sortir.
    const float inner = size.x - 8.0f;
    float cx = p.x + size.x * 0.5f;

    ImVec2 bs = ImGui::CalcTextSize(e.button);
    if (bs.x <= inner) {
        dl->AddText(ImVec2(cx - bs.x * 0.5f, p.y + size.y * 0.5f - bs.y - 2.0f), Col(bc), e.button);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(p.x + 4.0f, p.y + size.y * 0.5f - bs.y - 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, bc);
        TextTruncated(e.button, inner);
        ImGui::PopStyleColor();
    }

    PushX360Font(g_fontSmall);
    ImVec2 ks = ImGui::CalcTextSize(e.key);
    if (ks.x <= inner) {
        dl->AddText(ImVec2(cx - ks.x * 0.5f, p.y + size.y * 0.5f + 2.0f), Col(kTxt2), e.key);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(p.x + 4.0f, p.y + size.y * 0.5f + 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, kTxt2);
        TextTruncated(e.key, inner);
        ImGui::PopStyleColor();
    }
    PopX360Font(g_fontSmall);
}

// ===========================================================================
// LIGNES DE REGLAGE - posees sur une UiStack, pas sur le curseur ImGui
//
// Chacune recoit sa GEOMETRIE (la pile + son numero de ligne) au lieu de la
// calculer. Les trois calculaient auparavant leur propre `avail * 0.46f` et
// leur propre hauteur : trois copies de la meme regle, donc trois occasions
// de la voir diverger - et elles avaient effectivement diverge.
//
// Elles renvoient true quand l'utilisateur SELECTIONNE la ligne (clic sur le
// libelle), ce qui alimente le panneau contextuel. C'est le lien reel entre
// l'element et l'interface : la ligne ne se contente pas de s'afficher, elle
// dit qu'on s'occupe d'elle.
// ===========================================================================

// Zone cliquable du libelle, commune aux trois types de ligne.
bool RowLabel(const UiStack& s, int row, const char* label, bool selected) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = s.rowY(row), h = s.rowH();
    ImGui::SetCursorScreenPos(ImVec2(s.x, y));
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##lbl", ImVec2(s.labelW(), h));
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    if (selected) {
        dl->AddRectFilled(ImVec2(s.x - 6.0f, y), ImVec2(s.x + s.labelW(), y + h),
                          Col(0.11f, 0.12f, 0.14f), kRSm);
        // Pastille locale : montre PRECISEMENT quelle ligne alimente le
        // panneau contextuel. Remplace le trait de liaison generique pour ce
        // cas (voir DrawUiMapping, PANNEAU 4) - une ligne unique tiree
        // depuis un panneau du HAUT ne pouvait pas pointer vers une ligne
        // quelconque d'une pile dans le panneau du BAS, HB l'a signale a
        // raison ("l'indicateur ne montre pas ou est le bouton").
        dl->AddCircleFilled(ImVec2(s.x - 6.0f, y + h * 0.5f), 3.0f, Col(kColorMouseAccent));
    }

    PushX360Font(g_fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? kTxt : (hov ? kTxt : kTxt2));
    ImGui::SetCursorScreenPos(ImVec2(s.x, y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    TextTruncated(label, s.labelW() - 10.0f);
    ImGui::PopStyleColor();
    PopX360Font(g_fontSmall);
    return clicked;
}

bool SliderRow(const UiStack& s, int row, const char* label, bool selected,
               int* value, int vmin, int vmax, const ImVec4& accent) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = s.rowY(row), h = s.rowH();
    bool picked = RowLabel(s, row, label, selected);

    const float valW = 42.0f;
    const float barX = s.controlX();
    const float barW = s.controlW() - valW - 10.0f;
    const float barY = y + h * 0.5f;

    float t = (vmax > vmin && value) ? (float)(*value - vmin) / (float)(vmax - vmin) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    dl->AddRectFilled(ImVec2(barX, barY - 2.5f), ImVec2(barX + barW, barY + 2.5f), Col(kLine), 3.0f);
    dl->AddRectFilled(ImVec2(barX, barY - 2.5f), ImVec2(barX + barW * t, barY + 2.5f), Col(accent), 3.0f);
    dl->AddCircleFilled(ImVec2(barX + barW * t, barY), 10.0f,
                        Col(ImVec4(accent.x, accent.y, accent.z, 0.18f)));
    dl->AddCircleFilled(ImVec2(barX + barW * t, barY), 6.5f, Col(accent));

    ImGui::SetCursorScreenPos(ImVec2(barX, y));
    ImGui::PushID(label);
    ImGui::InvisibleButton("##bar", ImVec2(barW, h));
    if (ImGui::IsItemActive() && value) {
        float nt = (ImGui::GetIO().MousePos.x - barX) / (barW > 0.0f ? barW : 1.0f);
        if (nt < 0.0f) nt = 0.0f;
        if (nt > 1.0f) nt = 1.0f;
        *value = vmin + (int)(nt * (float)(vmax - vmin));
        picked = true;   // toucher la jauge, c'est aussi s'occuper de ce reglage
    }
    ImGui::PopID();

    PushX360Font(g_fontSmall);
    char num[16];
    int n = 0, v = value ? *value : 0;
    if (v == 0) num[n++] = '0';
    else {
        char tmp[16]; int k = 0;
        while (v > 0 && k < 15) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
        while (k > 0) num[n++] = tmp[--k];
    }
    num[n] = '\0';
    ImVec2 ns = ImGui::CalcTextSize(num);
    dl->AddText(ImVec2(s.x + s.w - ns.x, barY - ns.y * 0.5f), Col(accent), num);
    PopX360Font(g_fontSmall);
    return picked;
}

bool CheckRow(const UiStack& s, int row, const char* label, bool selected,
              bool* value, const ImVec4& accent) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = s.rowY(row), h = s.rowH();
    bool picked = RowLabel(s, row, label, selected);

    ImVec2 bp(s.controlX(), y + (h - 16.0f) * 0.5f);
    ImGui::SetCursorScreenPos(bp);
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##chk", ImVec2(16.0f, 16.0f)) && value) {
        *value = !*value;
        picked = true;
    }
    ImGui::PopID();

    const bool on = value && *value;
    dl->AddRectFilled(bp, ImVec2(bp.x + 16.0f, bp.y + 16.0f), on ? Col(accent) : Col(kPanel2), kRSm);
    dl->AddRect(bp, ImVec2(bp.x + 16.0f, bp.y + 16.0f), on ? Col(accent) : Col(kLine2), kRSm, 0, 1.0f);
    if (on) {
        dl->AddLine(ImVec2(bp.x + 3.5f, bp.y + 8.0f),  ImVec2(bp.x + 6.5f, bp.y + 11.5f), Col(kBg), 2.0f);
        dl->AddLine(ImVec2(bp.x + 6.5f, bp.y + 11.5f), ImVec2(bp.x + 12.5f, bp.y + 4.5f), Col(kBg), 2.0f);
    }
    return picked;
}

// Stepper (fleches gauche/droite + molette), PAS une liste deroulante
// (retour HB, 2026-08-16 : "pourquoi avoir ce drop menu d'un kilometre,
// pourquoi pas defiler de la mollette ou utiliser les fleches"). L'ancienne
// popup ouvrait une liste de 15 entrees (Default, A, B, X, Y, LB, RB, LT,
// RT, L3, R3, Start, Back, D-Pad x4) - lisible sur un ecran de bureau, pas
// a 2-3m avec une manette. Une valeur unique qu'on fait defiler est le
// pattern standard des menus consoles pour ce type de choix.
bool ComboRow(const UiStack& s, int row, const char* label, bool selected,
              int* value, const char* const* items, int count, const ImVec4& accent) {
    if (!value || !items || count <= 0) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = s.rowY(row);
    const float h = s.rowH() < 30.0f ? s.rowH() : 30.0f;
    const float fy = y + (s.rowH() - h) * 0.5f;
    bool picked = RowLabel(s, row, label, selected);

    const float fx = s.controlX(), fw = s.controlW();
    const float arrowW = 22.0f;

    // Champ CREUSE, rendu identique a avant - seule l'interaction change.
    const UiSkinTex& fieldTex = Skin(SKIN_FIELD_COMBO);
    if (fieldTex.present) {
        Draw9Slice(fieldTex, ImVec2(fx, fy), ImVec2(fw, h));
        dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + fw, fy + h * 0.5f),
                          Col(0.0f, 0.0f, 0.0f, 0.10f), kRSm, ImDrawCornerFlags_Top);
    } else {
        dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + fw, fy + h), Col(kPanel2), kRSm);
        dl->AddRectFilled(ImVec2(fx, fy), ImVec2(fx + fw, fy + h * 0.5f),
                          Col(0.0f, 0.0f, 0.0f, 0.10f), kRSm, ImDrawCornerFlags_Top);
    }

    int v = *value;
    if (v < 0 || v >= count) v = 0;

    PushX360Font(g_fontSmall);
    ImGui::PushID(label);

    // Trois zones ADJACENTES (pas superposees, pas besoin de
    // SetItemAllowOverlap) : fleche gauche, valeur (survol + molette),
    // fleche droite.
    ImGui::SetCursorScreenPos(ImVec2(fx, fy));
    ImGui::InvisibleButton("##prev", ImVec2(arrowW, h));
    bool prevHov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) { v = (v - 1 + count) % count; *value = v; picked = true; }

    ImGui::SetCursorScreenPos(ImVec2(fx + arrowW, fy));
    ImGui::InvisibleButton("##val", ImVec2(fw - arrowW * 2.0f, h));
    bool valHov = ImGui::IsItemHovered();
    if (valHov && ImGui::GetIO().MouseWheel != 0.0f) {
        // Molette avant = valeur suivante (meme sens qu'on ferait defiler
        // une liste vers le bas).
        int dir = (ImGui::GetIO().MouseWheel > 0.0f) ? -1 : 1;
        v = (v + dir + count) % count; *value = v; picked = true;
    }

    ImGui::SetCursorScreenPos(ImVec2(fx + fw - arrowW, fy));
    ImGui::InvisibleButton("##next", ImVec2(arrowW, h));
    bool nextHov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) { v = (v + 1) % count; *value = v; picked = true; }

    const bool hov = prevHov || valHov || nextHov;
    dl->AddRect(ImVec2(fx, fy), ImVec2(fx + fw, fy + h),
                (selected || hov) ? Col(accent) : Col(kLine), kRSm, 0, 1.0f);

    dl->AddTriangleFilled(ImVec2(fx + arrowW * 0.66f, fy + h * 0.5f - 5.0f),
                          ImVec2(fx + arrowW * 0.66f, fy + h * 0.5f + 5.0f),
                          ImVec2(fx + arrowW * 0.28f, fy + h * 0.5f),
                          Col(prevHov ? accent : kTxt3));
    dl->AddTriangleFilled(ImVec2(fx + fw - arrowW * 0.66f, fy + h * 0.5f - 5.0f),
                          ImVec2(fx + fw - arrowW * 0.66f, fy + h * 0.5f + 5.0f),
                          ImVec2(fx + fw - arrowW * 0.28f, fy + h * 0.5f),
                          Col(nextHov ? accent : kTxt3));

    ImGui::PushStyleColor(ImGuiCol_Text, v == 0 ? kTxt3 : kTxt);
    const float labelAvail = fw - arrowW * 2.0f - 6.0f;
    const float tw = ImGui::CalcTextSize(items[v]).x;
    const float tx = fx + arrowW + 3.0f + ((tw < labelAvail) ? (labelAvail - tw) * 0.5f : 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(tx, fy + (h - ImGui::GetTextLineHeight()) * 0.5f));
    TextTruncated(items[v], labelAvail);
    ImGui::PopStyleColor();

    ImGui::PopID();
    PopX360Font(g_fontSmall);
    return picked;
}

// Paragraphe qui se replie DANS la zone courante.
//
// PushTextWrapPos attend une position en coordonnees de fenetre, pas une
// largeur - lui passer GetContentRegionAvail() faisait replier le texte trop
// tard des que le curseur n'etait pas colle a la marge, d'ou les paragraphes
// qui touchaient le bord du panneau.
void WrappedText(const char* txt, const ImVec4& col, float maxWidth = -1.0f) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    float avail = ImGui::GetContentRegionAvail().x;
    float w = (maxWidth > 0.0f && maxWidth < avail) ? maxWidth : avail;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    PushX360Font(g_fontSmall);
    ImGui::TextUnformatted(txt);
    PopX360Font(g_fontSmall);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}


// outImgPos/outImgSize (2026-08-17) : renvoient le rectangle REEL ou l'image
// vient d'etre dessinee (apres mise a l'echelle uniforme + centrage), pour
// que l'appelant puisse y poser un repere lumineux au bon endroit (voir
// DrawHotspot plus bas) sans dupliquer ce calcul. nullptr si l'appelant n'en
// a pas besoin (ecran mouse settings, qui n'affiche pas de repere).
void FittedImage(ImTextureID tex, int tw, int th, ImVec2 zonePos, ImVec2 zoneSize,
                 const ImVec4& accent, ImVec2* outImgPos = 0, ImVec2* outImgSize = 0) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!tex || tw <= 0 || th <= 0) {
        // Pas d'image : cadre en pointille plutot qu'un vide, pour que
        // l'absence se distingue d'un bug de mise en page.
        ImVec2 a(zonePos.x + 10.0f, zonePos.y + 10.0f);
        ImVec2 b(zonePos.x + zoneSize.x - 10.0f, zonePos.y + zoneSize.y - 10.0f);
        dl->AddRect(a, b, Col(kLine2), kRMd, 0, 1.0f);
        return;
    }
    // Reflet : le produit "pose" sur une surface plutot que de flotter,
    // collee au cadre (retour HB, 2026-08-17 : "peut-etre meme avec un
    // reflet" pour un rendu plus premium). Technique standard des pages
    // produit (Apple etc.) : copie MIROIR du bas de l'image, courte, qui
    // s'estompe dans le fond du panneau.
    //
    // La place du reflet est RESERVEE AVANT de calculer la taille de
    // l'image (pas ajoutee apres coup) : un premier essai calculait le
    // reflet APRES avoir ajuste l'image sur toute la hauteur de la zone,
    // et cherchait la marge restante en dessous - or l'image centree
    // remplit deja la quasi-totalite de la zone (225px de la maquette),
    // il ne restait presque rien et le reflet disparaissait,
    // silencieusement, sur cet ecran precis. Reserver la bande d'abord
    // garantit un reflet visible sur toutes les tailles d'image.
    const float gap = 3.0f;
    // 0.22 -> 0.26 et alpha 0.35 -> 0.5 (2026-08-17, retour HB : le reflet
    // "n'est pas bien visible", en particulier sur la souris). Bande un peu
    // plus haute, reflet un peu moins sombre - toujours nettement plus
    // discret que l'image reelle (0.5 contre l'image pleine opacite),
    // juste assez pour se voir sans dominer.
    const float reflBandH = zoneSize.y * 0.26f;
    const float imgZoneH = zoneSize.y - reflBandH - gap;

    float sx = zoneSize.x / (float)tw, sy = imgZoneH / (float)th;
    float s = sx < sy ? sx : sy;
    float w = tw * s, h = th * s;
    ImVec2 a(zonePos.x + (zoneSize.x - w) * 0.5f, zonePos.y + (imgZoneH - h) * 0.5f);
    dl->AddImage(tex, a, ImVec2(a.x + w, a.y + h));
    if (outImgPos) *outImgPos = a;
    if (outImgSize) *outImgSize = ImVec2(w, h);

    // ImGui n'a pas de degrade de couleur PAR PIXEL sur une image (AddImage
    // ne prend qu'une seule teinte uniforme) : le fondu est simule en
    // posant un rectangle a degrade (transparent -> fond du panneau,
    // AddRectFilledMultiColor) PAR-DESSUS la bande miroir.
    float reflH = (h * 0.30f < reflBandH) ? h * 0.30f : reflBandH;
    if (reflH > 4.0f) {
        const float srcFrac = reflH / h; // fraction du bas de l'image reprise
        ImVec2 rp(a.x, a.y + h + gap);
        // UV inversee verticalement (haut du reflet = bas de l'image
        // source) et recadree aux derniers srcFrac% du bas seulement -
        // un reflet plein-hauteur serait trop grand pour la place
        // disponible sous l'image.
        dl->AddImage(tex, rp, ImVec2(rp.x + w, rp.y + reflH),
                    ImVec2(0.0f, 1.0f), ImVec2(1.0f, 1.0f - srcFrac),
                    Col(1.0f, 1.0f, 1.0f, 0.5f)); // 0.35 -> 0.5 (retour HB, 2026-08-17 : pas assez visible)
        dl->AddRectFilledMultiColor(rp, ImVec2(rp.x + w, rp.y + reflH),
                                    Col(kPanel.x, kPanel.y, kPanel.z, 0.0f),
                                    Col(kPanel.x, kPanel.y, kPanel.z, 0.0f),
                                    Col(kPanel), Col(kPanel));
    }
    (void)accent;
}

// --- Repere lumineux sur l'image (2026-08-17) -------------------------------
// Retour HB : le trait de liaison partait bien de la case selectionnee, mais
// "rien n'est indique par une luminance sur la manette" - la ou est VRAIMENT
// le bouton sur l'illustration restait a deviner. Coordonnees en fraction
// [0,1] DE L'IMAGE DESSINEE (pas de la zone 330x225) : reappliquees au
// rectangle renvoye par FittedImage (outImgPos/outImgSize), donc justes quel
// que soit le facteur d'echelle. Repere visuellement sur
// _design_refs/Manette_X360Remap_crop.png et Souris_X360Remap_crop.png -
// approximatif par nature (un halo large plutot qu'un pixel exact), ce qui
// est le but : montrer LA ZONE du bouton, pas un point au pixel pres.
struct PadHotspot { float u, v; };

// Ordre IDENTIQUE a DefaultKeyboardWizardSteps() (application/wizard_session.cpp)
// et donc a m.grid[] / BuildGridEntries (main.cpp) - meme convention "index de
// grille = index d'etape" deja utilisee pour wizardStepIndex/manualCaptureIndex.
// D-Pad (13-16) et stick gauche (17-20) partagent chacun un seul repere : les
// 4 directions d'un meme stick/pave sont physiquement LE MEME element sur la
// manette, rien a distinguer visuellement entre elles ici.
static const PadHotspot kPadHotspot[21] = {
    { 0.80f, 0.49f },  // 0  A
    { 0.87f, 0.39f },  // 1  B
    { 0.73f, 0.39f },  // 2  X
    { 0.80f, 0.28f },  // 3  Y
    { 0.24f, 0.14f },  // 4  LB
    { 0.78f, 0.14f },  // 5  RB
    { 0.24f, 0.05f },  // 6  LT
    { 0.78f, 0.05f },  // 7  RT
    { 0.20f, 0.37f },  // 8  L3 (clic stick gauche)
    { 0.60f, 0.59f },  // 9  R3 (clic stick droit)
    { 0.62f, 0.36f },  // 10 START
    { 0.39f, 0.36f },  // 11 BACK
    { 0.50f, 0.39f },  // 12 GUIDE (logo Xbox)
    { 0.31f, 0.59f }, { 0.31f, 0.59f }, { 0.31f, 0.59f }, { 0.31f, 0.59f },  // 13-16 D-Pad
    { 0.20f, 0.37f }, { 0.20f, 0.37f }, { 0.20f, 0.37f }, { 0.20f, 0.37f },  // 17-20 stick gauche
};

// Ordre IDENTIQUE aux 5 lignes "boutons" de l'ecran Reglages souris
// (kLbl/ptr dans DrawUiMapping, rowId 3 a 7 - voir plus bas) : clic gauche,
// clic droit, clic milieu, molette avant, molette arriere. La molette avant/
// arriere partagent la meme zone (c'est la meme molette) - a defaut de
// pouvoir distinguer visuellement "avant" de "arriere" sur une image fixe.
static const PadHotspot kMouseHotspot[5] = {
    { 0.30f, 0.35f },  // clic gauche
    { 0.47f, 0.22f },  // clic droit
    { 0.40f, 0.30f },  // clic milieu (molette)
    { 0.40f, 0.25f },  // molette avant
    { 0.40f, 0.35f },  // molette arriere
};

void DrawHotspot(ImVec2 imgPos, ImVec2 imgSize, float u, float v, const ImVec4& accent) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(imgPos.x + imgSize.x * u, imgPos.y + imgSize.y * v);
    // Halo (cercles concentriques a opacite decroissante, meme esprit que
    // GlowBorder ailleurs dans ce fichier) + point plein au centre : reste
    // lisible meme quand le halo se fond dans un fond clair de l'image.
    for (int i = 3; i >= 1; i--) {
        float r = 6.0f + i * 5.0f;
        dl->AddCircleFilled(c, r, Col(accent.x, accent.y, accent.z, 0.10f * (4 - i)), 24);
    }
    dl->AddCircleFilled(c, 5.0f, Col(accent), 16);
    dl->AddCircle(c, 5.0f, Col(1.0f, 1.0f, 1.0f, 0.85f), 16, 1.5f);
}

} // namespace

// ---------------------------------------------------------------------------
// BARRE SUPERIEURE
//
// Composition revue le 2026-08-14 (remarque de HB : "revoir la composition du
// logo et son sous-titre, les icones des menu").
//
//   AVANT : logo et sous-titre sur la MEME ligne, separes par un espace - le
//           sous-titre passait pour un cinquieme onglet, et le logo n'avait
//           plus l'air d'un logo.
//   APRES : logo sur deux lignes (marque au-dessus, signature espacee en
//           dessous), comme dans la maquette ; onglets alignes sur le centre
//           optique du bloc, chacun precede d'une icone qui le distingue de
//           loin.
// ---------------------------------------------------------------------------
float UiTopBarHeight(float screenH) { return screenH * kUiSafeY + 52.0f; }

void DrawUiTopBar(float screenW, float screenH, int& activeTab) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float marginX = screenW * kUiSafeX;
    const float marginY = screenH * kUiSafeY;
    const float barH    = UiTopBarHeight(screenH);

    dl->AddRectFilled(ImVec2(0, 0), ImVec2(screenW, barH), Col(0.055f, 0.06f, 0.07f));
    dl->AddLine(ImVec2(0, barH), ImVec2(screenW, barH), Col(kLine), 1.0f);

    // --- Logo, deux lignes ---
    const float logoY = marginY * 0.42f;
    ImGui::SetCursorScreenPos(ImVec2(marginX, logoY));
    PushX360Font(g_fontTitle);
    ImVec2 lp = ImGui::GetCursorScreenPos();
    float x = lp.x;
    // La hauteur du logo etait calee sur g_fontTitle (utilise ailleurs pour
    // le titre du panneau contextuel, "BOUTON A") - beaucoup trop grand
    // pour une marque en haut d'ecran (retour HB, 2026-08-14 : "le logo
    // est trop grand"). g_fontBody (la taille de police "normale", sans
    // agrandissement) donne une proportion plus proche de .logo .l1 dans
    // la maquette (26px, nettement plus petit que les 31px du fontTitle).
    float logoLine1H = g_fontBody ? g_fontBody->FontSize : ImGui::GetTextLineHeight();
    const UiSkinTex& logoTex = Skin(SKIN_LOGO_WORDMARK);
    if (logoTex.present && logoTex.h > 0) {
        // Pas de teinte : les couleurs (vert + blanc) sont cuites dans le
        // fichier, volontairement - c'est LA marque, elle ne change jamais
        // de couleur selon le mode, contrairement aux boutons/panneaux.
        float lh = logoLine1H;
        float lw = lh * ((float)logoTex.w / (float)logoTex.h);
        Draw9Slice(logoTex, ImVec2(x, lp.y), ImVec2(lw, lh));
        x += lw;
    } else {
        dl->AddText(ImVec2(x, lp.y), Col(kColorKeyboardAccent), "X360");
        x += ImGui::CalcTextSize("X360").x + 7.0f;
        dl->AddText(ImVec2(x, lp.y), Col(kTxt), "REMAP");
        x += ImGui::CalcTextSize("REMAP").x;
    }
    PopX360Font(g_fontTitle);

    PushX360Font(g_fontSmall);
    const char* sub = "KEYBOARD / MOUSE";
    // Sous-titre CENTRE sous le mot-symbole (retour HB, 2026-08-14), pas
    // aligne a gauche : "X360 REMAP" et "KEYBOARD / MOUSE" n'ont pas la
    // meme largeur, un alignement a gauche laissait un vide asymetrique
    // a droite du sous-titre.
    const float wordmarkW = x - lp.x;
    const float subW = TrackedWidth(sub, 2.4f);
    const float subX = lp.x + (wordmarkW - subW) * 0.5f;
    DrawTracked(ImVec2(subX, lp.y + logoLine1H + 2.0f), sub, 2.4f, Col(kTxt3));
    float logoW = wordmarkW;
    if (subW > logoW) logoW = subW;
    float logoH = logoLine1H + 2.0f + ImGui::GetTextLineHeight();
    PopX360Font(g_fontSmall);

    // --- Onglets ---
    const StrId tabIds[4] = {
        STR_APP_TAB_MAPPING, STR_APP_TAB_PROFILES, STR_APP_TAB_HOWTO, STR_APP_TAB_SUPPORT
    };
    const IconKind tabIcons[4] = { ICON_KEYBOARD, ICON_LIST, ICON_HELP, ICON_HEART };

    PushX360Font(g_fontSmall);
    const float tabsY = logoY + logoH * 0.5f;              // centre optique du logo
    const float lineH = ImGui::GetTextLineHeight();
    float tx = marginX + logoW + 48.0f;
    for (int i = 0; i < 4; i++) {
        const bool on = (activeTab == i);
        // Capitales, comme dans la maquette : c'est ce qui distingue le menu
        // du texte courant a distance d'un canape.
        char label[48];
        Upper(Tr(tabIds[i]), label, sizeof(label));
        float tw = TrackedWidth(label, 1.0f);
        float w  = 18.0f + tw;

        ImGui::SetCursorScreenPos(ImVec2(tx, tabsY - lineH));
        ImGui::PushID(100 + i);
        if (ImGui::InvisibleButton("##tab", ImVec2(w, lineH * 2.0f))) activeTab = i;
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();

        ImVec4 c = on ? kColorKeyboardAccent : (hov ? kTxt2 : kTxt3);
        DrawIcon(tabIcons[i], ImVec2(tx + 6.0f, tabsY), 13.0f, Col(c));
        DrawTracked(ImVec2(tx + 18.0f, tabsY - lineH * 0.5f), label, 1.0f, Col(c));
        // Soulignement colle SOUS le libelle, pas au bas du bandeau : le
        // bandeau inclut la marge de securite TV, donc l'y accrocher laissait
        // 40 px de vide entre l'onglet et son trait.
        if (on)
            dl->AddLine(ImVec2(tx, tabsY + lineH * 0.7f), ImVec2(tx + w, tabsY + lineH * 0.7f),
                        Col(kColorKeyboardAccent), 2.0f);
        tx += w + 26.0f;
    }
    PopX360Font(g_fontSmall);
}

// ---------------------------------------------------------------------------
// ECRAN MAPPING - construit sur la grille 12 colonnes (ui_layout.h)
//
//   [ peripheriques 3 ][ actions 3 ][   visuel 4   ][ ctx 2 ]
//   [               ][            panneau du bas (9)        ]
//
// Aucune largeur en dur : chaque panneau declare un nombre de COLONNES, les
// hauteurs sont des FRACTIONS. Changer une proportion = changer un chiffre,
// et les alignements restent justes.
// ---------------------------------------------------------------------------
void DrawUiMapping(UiMappingModel& m) {
    const UiGrid g = UiMakeGrid(12, 12.0f);

    const int kSideSpan    = 3;
    const int kActionsSpan = 3;
    const int kVisualSpan  = 4;
    const int kCtxSpan     = 2;

    // 55% en haut / 45% en bas : proportions de la maquette (zone haute 303px,
    // zone basse 236px sur 551px utiles).
    //
    // Repartition CONSTANTE (retour HB, 2026-08-16 : "ces reglages avances
    // et leur cadre qui change de taille apres selection"). Avant, topH
    // valait 55% partout SAUF en reglages avances (42%) - toute la page
    // changeait donc de decoupage haut/bas au clic sur "Reglages avances",
    // deplacant l'image et le panneau du haut avec elle.
    //
    // Premiere correction (42% partout) verifiee au rendu : le paragraphe
    // d'intro clavier/souris deborde de "##stage" a 42% et chevauche
    // l'invite "CHOISIS UN BOUTON" en bas du panneau. On fixe donc plutot
    // sur le cas le plus GENEREUX (55%) : les reglages avances ont un peu
    // de vide en bas (pas de defaut visible), alors que l'inverse produisait
    // un chevauchement de texte reel.
    const float topH    = UiRowH(g, 0.55f);
    const float bottomH = g.h - topH - g.gutter;

    const bool  mouseMode = (m.mode == 1);
    const ImVec4 accent   = mouseMode ? kColorMouseAccent : kColorKeyboardAccent;

    // ---------------------------------------------------------------------
    // RESOLUTION DE LA SELECTION
    //
    // Un SEUL endroit traduit "quel element est selectionne" en "quel texte
    // afficher". Le panneau contextuel n'invente rien : il lit ces deux
    // chaines, et n'est meme pas dessine si elles n'existent pas.
    //
    // Une selection devenue invalide (on a change de mode, la grille a
    // retreci, la souris n'a plus de reglages) est effacee ICI plutot que
    // testee a chaque usage : c'est ce qui empeche d'afficher un reglage qui
    // ne correspond plus a rien.
    // ---------------------------------------------------------------------
    char ctxTitleBuf[64], ctxValueBuf[64];
    const char* ctxTitle = 0;
    const char* ctxValue = 0;

    if (m.selection.kind == UISEL_PAD_BUTTON) {
        if (mouseMode || m.selection.index < 0 || m.selection.index >= m.gridCount || !m.grid)
            m.selection.kind = UISEL_NONE;      // selection devenue caduque
        else {
            ctxTitle = m.grid[m.selection.index].button;
            ctxValue = m.grid[m.selection.index].key;
        }
    } else if (m.selection.kind == UISEL_MOUSE_ROW) {
        if (!mouseMode) {
            m.selection.kind = UISEL_NONE;
        } else {
            // Les huit lignes de reglage souris, dans l'ordre ou elles sont
            // dessinees. Cette table est la definition de reference : les
            // lignes elles-memes s'y referent par leur index.
            const StrId kRowLabel[kMouseRowCount] = {
                STR_SETTINGS_SENSITIVITY_LABEL, STR_SETTINGS_DEADZONE_LABEL,
                STR_SETTINGS_INVERT_Y_LABEL,
                STR_SETTINGS_CLICK_LEFT_LABEL, STR_SETTINGS_CLICK_RIGHT_LABEL,
                STR_SETTINGS_CLICK_MIDDLE_LABEL,
                STR_SETTINGS_WHEEL_FORWARD_LABEL, STR_SETTINGS_WHEEL_BACKWARD_LABEL
            };
            const int r = m.selection.index;
            if (r < 0 || r >= kMouseRowCount) {
                m.selection.kind = UISEL_NONE;
            } else {
                Upper(Tr(kRowLabel[r]), ctxTitleBuf, sizeof(ctxTitleBuf));
                ctxTitle = ctxTitleBuf;

                int* combo[5] = { m.btnLeft, m.btnRight, m.btnMiddle,
                                  m.wheelForward, m.wheelBackward };
                if (r == 0 || r == 1) {
                    const int v = (r == 0) ? (m.sensitivity ? *m.sensitivity : 0)
                                           : (m.deadzone ? *m.deadzone : 0);
                    IntToStr(v, ctxValueBuf, sizeof(ctxValueBuf));
                    ctxValue = ctxValueBuf;
                } else if (r == 2) {
                    ctxValue = Tr((m.invertY && *m.invertY) ? STR_CALSUM_YES : STR_CALSUM_NO);
                } else {
                    int* sel = combo[r - 3];
                    ctxValue = (sel && m.buttonTargets && *sel >= 0 && *sel < m.buttonTargetCount)
                             ? m.buttonTargets[*sel] : "-";
                }
            }
        }
    }

    const bool hasSel = (m.selection.kind != UISEL_NONE && ctxTitle && ctxValue);
    if (!hasSel) m.selection.kind = UISEL_NONE;

    // =====================================================================
    // PANNEAU 1 - PERIPHERIQUES (colonne de gauche, pleine hauteur)
    // =====================================================================
    {
        ImVec2 pos(g.colX(0), g.y), size(g.spanW(kSideSpan), g.h);
        BeginPanel("##side", pos, size);

        SectionTitle(Tr(STR_UI_DEVICES_PANEL_TITLE), kColorKeyboardAccent);
        ImGui::Dummy(ImVec2(0, 6.0f));

        GroupLabel(Tr(STR_DEVICE_KEYBOARD_LABEL));
        ImGui::Dummy(ImVec2(0, 2.0f));
        if (m.keyboardCount == 0) {
            PushX360Font(g_fontSmall);
            ImGui::TextDisabled("--");
            PopX360Font(g_fontSmall);
        }
        for (int i = 0; i < m.keyboardCount; i++)
            if (DeviceCard(ICON_KEYBOARD, m.keyboards[i], i == m.keyboardIndex,
                           kColorKeyboardAccent, i))
                m.keyboardIndex = i;

        ImGui::Dummy(ImVec2(0, 8.0f));
        GroupLabel(Tr(STR_DEVICE_MOUSE_LABEL));
        ImGui::Dummy(ImVec2(0, 2.0f));
        if (m.mouseCount == 0) {
            PushX360Font(g_fontSmall);
            ImGui::TextDisabled("--");
            PopX360Font(g_fontSmall);
        }
        for (int i = 0; i < m.mouseCount; i++)
            if (DeviceCard(ICON_MOUSE, m.mice[i], i == m.mouseIndex,
                           kColorMouseAccent, 100 + i))
                m.mouseIndex = i;

        // --- Pied de colonne : etat du plugin + langues, colles en bas ---
        // Position calculee depuis le BAS de la zone : le bloc reste ancre
        // quel que soit le nombre de peripheriques listes au-dessus.
        // Hauteur mesuree, pas devinee : filet + ecart + ligne d'etat + ecart
        // + puce de langue, plus l'espacement qu'ImGui insere entre chaque.
        // Puces de langue declarees ICI, avant le calcul de hauteur, et plus
        // bas dans la boucle de dessin : les deux DOIVENT partir du meme
        // tableau. Le 2026-09-13, l'ajout d'une seconde rangee (espagnol +
        // portugais) sans mettre a jour footH a fait sortir cette rangee du
        // cadre - constate a l'ecran par HB. En derivant la hauteur du
        // tableau, une future 5e langue ne peut plus reproduire ce bug.
        //
        // ATTENTION : l'ordre d'AFFICHAGE n'est pas l'ordre des valeurs de
        // langue. langIndex vaut 0=FR, 1=EN, 2=PT, 3=ES (voir main.cpp),
        // valeurs figees car ecrites dans X360Remap_lang.txt chez les
        // utilisateurs. On affiche EN/FR puis ES/PT, donc chaque puce
        // transporte explicitement SON index.
        struct LangPill { int index; const char* code; };
        static const LangPill pills[] = {
            { 1, "EN" }, { 0, "FR" },
            { 3, "ES" }, { 2, "PT" },
        };
        const int kLangCount = (int)(sizeof(pills) / sizeof(pills[0]));
        const int kPerRow    = 2;
        const int kLangRows  = (kLangCount + kPerRow - 1) / kPerRow;
        const float kChipH   = 24.0f;
        const float kRowGap  = 4.0f;

        const float lh = ImGui::GetTextLineHeight();
        const float isp = ImGui::GetStyle().ItemSpacing.y;
        const float chipsH = kChipH * (float)kLangRows + kRowGap * (float)(kLangRows - 1);
        const float footH = 1.0f + 10.0f + lh + 8.0f + chipsH
                          + isp * (float)(3 + kLangRows);
        float footY = size.y - 16.0f - footH;
        if (ImGui::GetCursorPos().y < footY) ImGui::SetCursorPosY(footY);

        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(sp.x, sp.y), ImVec2(sp.x + ImGui::GetContentRegionAvail().x, sp.y),
            Col(kLine), 1.0f);
        ImGui::Dummy(ImVec2(0, 10.0f));

        PushX360Font(g_fontSmall);
        ImVec2 dp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(dp.x + 4.0f, dp.y + ImGui::GetTextLineHeight() * 0.5f), 3.5f,
            m.pluginAlive ? Col(kColorKeyboardAccent) : Col(kWarn));
        ImGui::SetCursorScreenPos(ImVec2(dp.x + 14.0f, dp.y));
        ImGui::TextColored(m.pluginAlive ? kTxt2 : kWarn, "%s",
                           Tr(m.pluginAlive ? STR_UI_PLUGIN_ALIVE : STR_UI_PLUGIN_SILENT));
        PopX360Font(g_fontSmall);
        ImGui::Dummy(ImVec2(0, 8.0f));

        // Les puces (tableau pills, kLangCount, kPerRow) sont declarees plus
        // haut, avec le calcul de la hauteur du pied de colonne - voir la
        // note qui s'y trouve.
        //
        // Largeur commune a toutes les puces, sur le code le plus large a
        // cette police - voir la note sur LangChip() : sans ca, "FR" (plus
        // large que "PT") donnait des cadres de tailles differentes.
        float langW = 0.0f;
        {
            PushX360Font(g_fontSmall);
            for (int i = 0; i < kLangCount; i++) {
                float cw = 6.0f + 18.0f + 6.0f + TrackedWidth(pills[i].code, 0.8f) + 8.0f;
                if (cw > langW) langW = cw;
            }
            PopX360Font(g_fontSmall);
        }
        // Rangees CENTREES dans la colonne (retour HB, 2026-08-14), pas
        // collees a gauche : les puces ne remplissent jamais toute la largeur
        // du panneau, un alignement a gauche laissait un vide asymetrique a
        // droite.
        const float rowW = langW * (float)kPerRow + 6.0f * (float)(kPerRow - 1);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float indent = (avail > rowW) ? (avail - rowW) * 0.5f : 0.0f;

        for (int i = 0; i < kLangCount; i++) {
            const bool firstOfRow = (i % kPerRow) == 0;
            if (firstOfRow) {
                // Meme valeur que kRowGap utilise dans le calcul de footH
                // plus haut : les deux doivent rester identiques, sinon la
                // derniere rangee ressort du cadre.
                if (i) ImGui::Dummy(ImVec2(0, kRowGap));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            } else {
                ImGui::SameLine(0.0f, 6.0f);
            }
            if (LangChip(pills[i].index, pills[i].code,
                         m.langIndex == pills[i].index, langW, kChipH))
                m.langIndex = pills[i].index;
        }

        EndPanel();
    }

    // =====================================================================
    // PANNEAU 2+3 - ZONE HAUTE : action + visuel, DANS UN SEUL panneau
    //
    // La maquette (maquette_v1.2.html, .stage) n'a qu'UN cadre ici : il
    // contient l'accroche+bouton (.stage-l) et l'image de la manette
    // (.pad) SANS bordure propre a chacun - seul le panneau contextuel
    // (.ctx, dessine plus bas) a sa propre bordure visible. Avant ce
    // correctif, "actions" et "visual" etaient deux BeginPanel distincts,
    // donc deux cadres/ombres visibles cote a cote la ou la maquette
    // n'en montre qu'un seul - c'est l'ecart structurel que HB a repere
    // en comparant au rendu direct de son propre fichier HTML (2026-08-14
    // : "tu as fait une maquette HTML/CSS mais tu ne la respectes pas
    // pixel a pixel"). .pad fait une largeur FIXE de 330px dans le CSS
    // (pas une proportion de grille) - reprise telle quelle, le canvas
    // etant lui-meme un 1280x720 fixe (voir preview_main.cpp).
    // =====================================================================
    {
        // Largeur TOUJOURS PLEINE (retour HB, 2026-08-14 : le panneau
        // contextuel forcait ce panneau a retrecir de kCtxSpan colonnes des
        // qu'une selection existait - l'image et le bouton se decalaient a
        // chaque clic. Classe de bug "Cumulative Layout Shift" : une zone
        // qui change de taille selon l'etat plutot que de reserver sa place
        // a l'avance. Le badge de selection (plus bas) flotte maintenant
        // PAR-DESSUS ce panneau a largeur constante, il ne le retrecit plus.
        const int span = kActionsSpan + kVisualSpan + kCtxSpan;
        ImVec2 pos(g.colX(kSideSpan), g.y), size(g.spanW(span), topH);
        BeginPanel("##stage", pos, size);

        const float gap = 16.0f;   // .stage { gap: 16px }
        const float padW = 330.0f; // .pad { flex: 0 0 330px }
        const float innerH = ImGui::GetContentRegionAvail().y;
        const float actionsW = ImGui::GetContentRegionAvail().x - gap - padW;
        ImVec2 stageOrigin = ImGui::GetCursorScreenPos();

        // --- .stage-l : bouton d'action + accroche, sans bordure propre ---
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##stagel", ImVec2(actionsW, innerH), false, ImGuiWindowFlags_NoScrollbar);
        if (!mouseMode && m.wizardActive) {
            // Assistant EN DIRECT sur cette meme page (2026-08-17, retour HB -
            // voir le commentaire de wizardActive dans ui_mapping.h). La case
            // en cours s'allume dans la grille plus bas (wizTarget) ; ce bloc
            // ne fait que dire OU on en est et donner le resultat final.
            if (m.wizardComplete) {
                ImGui::TextColored(m.wizardSaveOk ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    Tr(m.wizardSaveOk ? STR_WIZARD_SAVED_FMT : STR_WIZARD_SAVE_FAILED), m.wizardResultsCount);
                ImGui::Dummy(ImVec2(0, 12.0f));
                if (AccentButton(Tr(STR_WIZARD_BTN_OK), ICON_PLAY, kColorKeyboardAccent))
                    m.requestWizardClose = true;
            } else if (m.wizardCancelled) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), Tr(STR_WIZARD_CANCELLED));
                ImGui::Dummy(ImVec2(0, 12.0f));
                if (AccentButton(Tr(STR_WIZARD_BTN_OK), ICON_PLAY, kColorKeyboardAccent))
                    m.requestWizardClose = true;
            } else {
                ImGui::TextColored(kTxt2, Tr(STR_WIZARD_STEP_PROGRESS_FMT),
                    m.wizardStepIndex + 1, m.wizardStepTotal, m.wizardResultsCount);
                ImGui::Dummy(ImVec2(0, 4.0f));
                // wizardPrompt est deja forme cote appelant (main.cpp), avec
                // le %s de STR_WIZARD_PRESS_KEY_FOR_FMT deja substitue - cet
                // ecran doit rester portable, pas de snprintf ici.
                if (m.wizardPrompt) WrappedText(m.wizardPrompt, kTxt, 254.0f);
                if (m.wizardNoKeyboard) {
                    ImGui::Dummy(ImVec2(0, 8.0f));
                    WrappedText(Tr(STR_WIZARD_NO_KEYBOARD), ImVec4(1.0f, 0.6f, 0.2f, 1.0f), 254.0f);
                } else if (m.wizardMessage && m.wizardMessage[0]) {
                    ImGui::Dummy(ImVec2(0, 8.0f));
                    WrappedText(m.wizardMessage, ImVec4(1.0f, 0.9f, 0.3f, 1.0f), 254.0f);
                }
                ImGui::Dummy(ImVec2(0, 12.0f));
                WrappedText(Tr(STR_WIZARD_HOLD_TO_SKIP), kTxt3, 254.0f);
            }
        } else if (!mouseMode && m.manualCaptureIndex >= 0) {
            // Capture d'UNE touche pour la case cliquee (reglages manuels,
            // 2026-08-17) - meme case surlignee dans la grille plus bas
            // (voir manualTarget), mais pas de sequence/etapes/bouton OK :
            // la capture se referme d'elle-meme des que l'appelant voit une
            // touche assignee (voir requestManualAssignIndex, main.cpp).
            ImGui::TextColored(kTxt2, Tr(STR_MANUAL_CAPTURE_TITLE));
            ImGui::Dummy(ImVec2(0, 4.0f));
            if (m.manualPrompt) WrappedText(m.manualPrompt, kTxt, 254.0f);
            if (m.manualNoKeyboard) {
                ImGui::Dummy(ImVec2(0, 8.0f));
                WrappedText(Tr(STR_WIZARD_NO_KEYBOARD), ImVec4(1.0f, 0.6f, 0.2f, 1.0f), 254.0f);
            } else if (m.manualMessage && m.manualMessage[0]) {
                ImGui::Dummy(ImVec2(0, 8.0f));
                WrappedText(m.manualMessage, ImVec4(1.0f, 0.9f, 0.3f, 1.0f), 254.0f);
            }
            ImGui::Dummy(ImVec2(0, 12.0f));
            WrappedText(Tr(STR_MANUAL_CAPTURE_HINT), kTxt3, 254.0f);
        } else if (!mouseMode) {
            if (AccentButton(Tr(STR_WIZARD_BTN_START), ICON_PLAY, kColorKeyboardAccent))
                m.requestStartWizard = true;
            ImGui::Dummy(ImVec2(0, 10.0f));
            // Bouton secondaire "Reglages manuels" (2026-08-17, demande HB) -
            // GhostButton plutot qu'AccentButton : c'est une action alternative,
            // pas l'action principale de la page.
            if (GhostButton(Tr(m.manualMode ? STR_MANUAL_BTN_STOP : STR_MANUAL_BTN_START), kColorKeyboardAccent))
                m.requestToggleManualMode = true;
            // Largeur plafonnee comme .hint { max-width: 254px } dans la
            // maquette - sans ca le texte s'etirait jusqu'au bord de la zone
            // manette, colle au cadre de droite (retour HB, 2026-08-14).
            ImGui::Dummy(ImVec2(0, 16.0f)); // .hint { margin-top: 16px }
            WrappedText(Tr(m.manualMode ? STR_MANUAL_HINT : STR_MAPPING_KEYBOARD_HINT), kTxt2, 254.0f);
        } else {
            if (AccentButton(Tr(STR_SETTINGS_BTN_SAVE), ICON_MOUSE, kColorMouseAccent))
                m.requestSaveMouse = true;
            // Mesure de la souris (deplacee ici le 2026-09-23, voir la zone du
            // bas des reglages souris) : meme emplacement et meme enchainement
            // "bouton principal + bouton secondaire" que le mode clavier
            // (Demarrer + Reglages manuels), dont on sait qu'il tient dans ce
            // bloc et qu'il reagit au clic.
            ImGui::Dummy(ImVec2(0, 10.0f));
            if (GhostButton(Tr(STR_MEASURE_BTN_START), kColorMouseAccent))
                m.requestMeasureMouse = true;
            ImGui::Dummy(ImVec2(0, 6.0f));
            // Etat TOUJOURS affiche : une souris non mesuree doit se voir,
            // c'est elle qui explique qu'une meme sensibilite donne des
            // resultats differents d'une souris a l'autre.
            if (m.mouseDpiMeasured > 0) {
                // Pas de _snprintf dans ce fichier (il doit compiler hors
                // console, voir IntToStr) : prefixe traduit + nombre.
                char dpiBuf[128];
                char num[16];
                IntToStr(m.mouseDpiMeasured, num, sizeof(num));
                int n = 0;
                for (const char* c = Tr(STR_MEASURE_STATUS_PREFIX); *c && n < (int)sizeof(dpiBuf) - 1; c++) dpiBuf[n++] = *c;
                for (const char* c = num; *c && n < (int)sizeof(dpiBuf) - 1; c++) dpiBuf[n++] = *c;
                for (const char* c = " DPI"; *c && n < (int)sizeof(dpiBuf) - 1; c++) dpiBuf[n++] = *c;
                dpiBuf[n] = '\0';
                WrappedText(dpiBuf, kColorMouseAccent, 254.0f);
            } else {
                WrappedText(Tr(STR_MEASURE_STATUS_UNKNOWN), kWarn, 254.0f);
            }
            ImGui::Dummy(ImVec2(0, 12.0f));
            WrappedText(Tr(STR_MAPPING_MOUSE_HINT), kTxt2, 254.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        // --- .pad : visuel du peripherique -----------------------------
        // CSS : .padwrap fait une taille FIXE 330x225, centree (align-
        // items/justify-content:center) dans la hauteur DISPONIBLE de
        // .pad - pas etiree pour la remplir. Avant ce correctif, l'image
        // grandissait avec la hauteur du panneau (jusqu'a ~314px sans
        // selection contre ~225 avec) : deux tailles differentes pour le
        // MEME element, ce que la maquette ne fait jamais.
        const float padH = 225.0f; // .padwrap { height: 225px }
        // Place reservee EN BAS pour l'invitation quand rien n'est selectionne :
        // sans elle le texte se superposait a la manette.
        //
        // POSITION FIXE, VOLONTAIREMENT INDEPENDANTE DE hasSel/du badge
        // (2026-08-17, retour HB explicite : "il ne doit pas changer" -
        // l'image ne doit jamais se decaler selon l'etat). Un essai
        // precedent reservait de la place en haut pour le badge, ce qui
        // deplacait l'image de ~120px des qu'une selection existait -
        // exactement la classe de bug "Cumulative Layout Shift" que ce
        // meme panneau evite deja ailleurs (voir le commentaire de
        // "##stage" plus haut). Le badge est desormais un bandeau bas et
        // compact qui se pose sur la bande de REFLET (decorative) au lieu
        // de recouvrir la manette - voir plus bas, aucune reserve requise.
        const float reserve = hasSel ? 0.0f : 26.0f;
        const float availH = innerH - reserve;
        const float centerY = stageOrigin.y + (availH - padH) * 0.5f;
        const ImVec2 zonePos(stageOrigin.x + actionsW + gap, centerY);
        const ImVec2 zoneSize(padW, padH);
        ImVec2 imgPos(0, 0), imgSize(0, 0);
        if (mouseMode)
            FittedImage(m.texMouseSide, m.texMsW, m.texMsH, zonePos, zoneSize, accent, &imgPos, &imgSize);
        else
            FittedImage(m.texKeyboardSide, m.texKbW, m.texKbH, zonePos, zoneSize, accent, &imgPos, &imgSize);

        // --- Repere lumineux sur le bouton physique (2026-08-17) -----------
        // Retour HB : "rien n'est indique par une luminance sur la manette".
        // En mode clavier, priorite selection manuelle > wizard en cours >
        // capture manuelle en cours - au plus une seule de ces trois est
        // vraie a la fois (voir les gardes-fous mutuels poses plus haut/dans
        // main.cpp). En mode souris, seules les 5 lignes bouton (rowId 3-7)
        // ont un repere physique - sensibilite/deadzone/inversion n'en ont
        // pas, il n'y a rien a montrer sur l'image pour elles.
        if (imgSize.x > 0.0f) {
            int hotIndex = -1;
            const PadHotspot* table = 0;
            if (!mouseMode) {
                table = kPadHotspot;
                if (m.selection.kind == UISEL_PAD_BUTTON) hotIndex = m.selection.index;
                else if (m.wizardActive && !m.wizardComplete && !m.wizardCancelled) hotIndex = m.wizardStepIndex;
                else if (m.manualCaptureIndex >= 0) hotIndex = m.manualCaptureIndex;
                if (hotIndex < 0 || hotIndex >= 21) hotIndex = -1;
            } else {
                table = kMouseHotspot;
                if (m.selection.kind == UISEL_MOUSE_ROW && m.selection.index >= 3 && m.selection.index <= 7)
                    hotIndex = m.selection.index - 3;
            }
            if (hotIndex >= 0)
                DrawHotspot(imgPos, imgSize, table[hotIndex].u, table[hotIndex].v, accent);
        }

        // =================================================================
        // BANDEAU COMPACT - ce que l'utilisateur est en train de regler
        //
        // Redessine le 2026-08-17 (retour HB, capture d'ecran a l'appui) :
        // l'ancienne carte (titre + puce + ligne "Fermer", ~112px de haut,
        // ancree en HAUT de la zone) recouvrait systematiquement le haut de
        // la manette (LB/RB/joysticks caches). Un essai intermediaire
        // reservait de la place en repoussant l'image vers le bas - HB a
        // explicitement refuse ce chemin : "il ne doit pas changer" (la
        // position de l'image doit rester FIXE, quel que soit l'etat, voir
        // le commentaire au-dessus de centerY).
        //
        // Solution retenue : un bandeau UNE SEULE LIGNE, ancre au bord BAS
        // de la zone .pad plutot qu'en haut. Il chevauche alors la bande de
        // REFLET (purement decorative, generee par FittedImage) et jamais
        // la manette/souris elle-meme - tous les boutons reels sont dans la
        // moitie superieure/centrale de l'image (voir kPadHotspot/
        // kMouseHotspot). Plus de trait de liaison vers la grille (retour
        // HB : "pas besoin d'une fleche juste illuminer la touche sur
        // l'image") - le repere lumineux pose directement sur l'image
        // (DrawHotspot, juste au-dessus) suffit maintenant a montrer QUEL
        // bouton, le bandeau se contente de confirmer LEQUEL en toutes
        // lettres pour l'accessibilite (texte, pas seulement une couleur).
        if (hasSel) {
            PushX360Font(g_fontSmall);
            const float lineH = ImGui::GetTextLineHeight();
            const float padIn = 10.0f;
            const float badgeH = lineH + padIn * 2.0f;
            const float badgeW = padW; // pleine largeur de la zone .pad, pas une carte a part
            const float closeW = 22.0f;
            ImVec2 bp(zonePos.x, zonePos.y + zoneSize.y - badgeH);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(bp, ImVec2(bp.x + badgeW, bp.y + badgeH), Col(0.055f, 0.063f, 0.075f, 0.92f), kRSm);
            dl->AddRect(bp, ImVec2(bp.x + badgeW, bp.y + badgeH), Col(accent), kRSm, 0, 1.0f);

            float tx = bp.x + padIn, ty = bp.y + (badgeH - lineH) * 0.5f;
            DrawIcon(mouseMode ? ICON_MOUSE : ICON_KEYBOARD, ImVec2(tx + 6.0f, bp.y + badgeH * 0.5f), 11.0f, Col(accent));
            tx += 18.0f;

            // "TITRE   VALEUR" sur une seule ligne (deux teintes, pas de
            // fleche/separateur dessine - le badge est deja associe au bon
            // bouton par le repere lumineux sur l'image). Chacun tronque
            // dans sa propre moitie de la place restante pour qu'un titre
            // long n'ecrase pas la valeur.
            const float avail = badgeW - (tx - bp.x) - closeW - padIn;
            ImGui::SetCursorScreenPos(ImVec2(tx, ty));
            ImGui::PushStyleColor(ImGuiCol_Text, kTxt3);
            TextTruncated(ctxTitle, avail * 0.42f);
            ImGui::PopStyleColor();

            ImGui::SameLine(0.0f, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            TextTruncated(ctxValue, avail * 0.55f);
            ImGui::PopStyleColor();

            // Fermer : petit "x" a droite plutot qu'un libelle complet - le
            // bandeau est trop bas pour un mot entier.
            ImGui::SetCursorScreenPos(ImVec2(bp.x + badgeW - closeW, bp.y));
            if (ImGui::InvisibleButton("##ctxclose", ImVec2(closeW, badgeH)))
                m.selection.kind = UISEL_NONE;
            bool closeHov = ImGui::IsItemHovered();
            DrawTracked(ImVec2(bp.x + badgeW - closeW + 6.0f, ty), "x", 0.6f,
                        Col(closeHov ? kTxt2 : kTxt3));
            PopX360Font(g_fontSmall);
        }

        // Sans selection, la zone dit ce qu'on peut y faire. Un grand visuel
        // muet ressemble sinon a un ecran inacheve.
        //
        // Centre sur la largeur TOTALE du panneau (pas seulement la zone
        // .pad de 330px) : .pad ayant maintenant une largeur fixe (comme
        // dans la maquette), le texte, plus long qu'elle, deborderait des
        // deux cotes si on le centrait dessus seule.
        if (!hasSel) {
            PushX360Font(g_fontSmall);
            const char* h = Tr(STR_MAPPING_PICK_HINT);
            float tw = TrackedWidth(h, 1.2f);
            float innerW = actionsW + gap + padW; // largeur de contenu reelle (hors padding du panneau)
            DrawTracked(ImVec2(stageOrigin.x + (innerW - tw) * 0.5f,
                               stageOrigin.y + innerH - 10.0f - ImGui::GetTextLineHeight()),
                        h, 1.2f, Col(kTxt3));
            PopX360Font(g_fontSmall);
        }

        EndPanel();
    }

    // =====================================================================
    // PANNEAU 5 - ZONE BASSE : bascule + grille OU reglages souris
    // =====================================================================
    {
        ImVec2 pos(g.colX(kSideSpan), g.y + topH + g.gutter);
        ImVec2 size(g.spanW(kActionsSpan + kVisualSpan + kCtxSpan), bottomH);
        // Marge 16px = .lower { padding: 14px 16px 16px } (maquette) - le
        // 14px du haut n'est pas repris ici (WindowPadding est symetrique
        // en ImGui, ecart de 2px non visible), mais gauche/droite/bas
        // suivent exactement le 16px de la maquette.
        BeginPanel("##bottom", pos, size, kLine, 16.0f);

        // --- Barre de tete : bascule | titre | lien reglages avances ---
        const float headY = ImGui::GetCursorPosY();
        Segmented(Tr(STR_MAPPING_MODE_KEYBOARD), Tr(STR_MAPPING_MODE_MOUSE), m.mode);
        ImGui::SameLine(0.0f, 16.0f);

        {
            // Titre de section, cale sur la hauteur de la bascule.
            char title[64];
            int n = 0;
            const char* fmt = Tr(STR_UI_GRID_TITLE_FMT);
            for (int i = 0; fmt[i] && n < (int)sizeof(title) - 4; i++) {
                if (fmt[i] == '%' && fmt[i + 1] == 'd') {
                    int v = m.gridCount; char tmp[8]; int k = 0;
                    if (v == 0) tmp[k++] = '0';
                    while (v > 0 && k < 7) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
                    while (k > 0) title[n++] = tmp[--k];
                    i++;
                } else title[n++] = fmt[i];
            }
            title[n] = '\0';
            PushX360Font(g_fontSmall);
            ImVec2 tp = ImGui::GetCursorScreenPos();
            DrawTracked(ImVec2(tp.x, tp.y + (28.0f - ImGui::GetTextLineHeight()) * 0.5f),
                        m.advancedOpen ? Tr(STR_UI_ADVANCED)
                                       : (mouseMode ? Tr(STR_UI_MOUSE_PANEL_TITLE) : title),
                        1.5f, Col(kTxt3));
            PopX360Font(g_fontSmall);
        }

        // Lien "Reglages avances", aligne a DROITE, mode souris uniquement.
        //
        // Il BASCULE maintenant reellement le panneau (m.advancedOpen) au lieu
        // de se contenter de lever un drapeau que personne ne traitait - c'est
        // ce qui faisait qu'un clic ne produisait rien. Les reglages avances
        // sont un troisieme etat du panneau du bas, au meme titre que Clavier
        // et Souris, donc ils se commandent depuis la meme barre.
        if (mouseMode) {
            PushX360Font(g_fontSmall);
            // Meme libelle dans les deux sens, seule la couleur change : un
            // "< retour" ecrit en dur reintroduisait du francais dans
            // l'interface anglaise, exactement le defaut qu'on vient de
            // corriger sur les titres de colonne.
            const char* adv = Tr(STR_UI_ADVANCED);
            float aw = 20.0f + TrackedWidth(adv, 0.8f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 16.0f - aw, headY));
            ImVec2 ap = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##adv", ImVec2(aw, 28.0f))) {
                m.advancedOpen = !m.advancedOpen;
                m.requestAdvanced = m.advancedOpen;
            }
            ImVec4 ac = (m.advancedOpen || ImGui::IsItemHovered()) ? kColorMouseAccent : kTxt3;
            DrawIcon(ICON_GEAR, ImVec2(ap.x + 7.0f, ap.y + 14.0f), 13.0f, Col(ac));
            DrawTracked(ImVec2(ap.x + 20.0f, ap.y + (28.0f - ImGui::GetTextLineHeight()) * 0.5f),
                        adv, 0.8f, Col(ac));
            PopX360Font(g_fontSmall);
        } else {
            // Quitter le mode souris referme les reglages avances : sinon on
            // revient dessus en rebasculant, sans avoir rien demande.
            m.advancedOpen = false;
        }

        // Retour a la MARGE GAUCHE : SetCursorPosY seul conserve le X laisse
        // par le dernier SameLine, ce qui decalait tout le contenu de 200 px
        // vers la droite. La meme erreur avait deja decale la page entiere une
        // fois (voir preview/LISEZ-MOI.md).
        const float pad = 16.0f;
        ImGui::SetCursorPos(ImVec2(pad, headY + 28.0f + 14.0f));

        // Place restante, calculee sur la taille REELLE du panneau plutot que
        // par GetContentRegionAvail : ce dernier ne retire pas la marge basse,
        // et la derniere rangee de la grille se retrouvait coupee.
        const float contentH = size.y - (headY + 28.0f + 14.0f) - pad;
        const float contentW = size.x - pad * 2.0f;

        // --- Contenu variable -------------------------------------------
        if (!mouseMode) {
            // Grille 7 x 3, cellules de taille EGALE calculees sur la place
            // reellement disponible - pas de dimension figee.
            const int cols = 7, rows = 3;
            const float gap = 6.0f;
            ImVec2 gp = ImGui::GetCursorScreenPos();
            float cw = (contentW - gap * (cols - 1)) / cols;
            float ch = (contentH - gap * (rows - 1)) / rows;

            for (int i = 0; i < m.gridCount && i < cols * rows; i++) {
                int r = i / cols, c = i % cols;
                ImVec2 cp(gp.x + (cw + gap) * c, gp.y + (ch + gap) * r);
                ImGui::SetCursorScreenPos(cp);
                ImGui::PushID(200 + i);
                bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cw, ch));
                bool hov = ImGui::IsItemHovered();
                ImGui::PopID();

                // C'est ICI que naît la selection. Un clic sur une case dit
                // "je m'occupe de ce bouton", et le panneau contextuel se
                // contente d'en rendre compte. Recliquer la meme case la
                // deselectionne : on referme ce qu'on a ouvert.
                //
                // Desactive pendant un assistant en direct (2026-08-17) :
                // cliquer une case pendant que le wizard tourne ouvrirait le
                // panneau contextuel/stepper PAR-DESSUS un assistant qui
                // attend deja une frappe pour cette meme case - deux
                // mecanismes d'edition en meme temps sur la meme donnee.
                // Egalement desactive en mode reglages manuels (2026-08-17) :
                // un clic y a un sens different (demarrer une capture, voir
                // ci-dessous), pas celui d'ouvrir le panneau contextuel.
                if (clicked && !m.wizardActive && !m.manualMode) {
                    if (m.selection.kind == UISEL_PAD_BUTTON && m.selection.index == i) {
                        m.selection.kind = UISEL_NONE;
                    } else {
                        m.selection.kind  = UISEL_PAD_BUTTON;
                        m.selection.index = i;
                    }
                }
                // Reglages manuels (2026-08-17) : un clic sur une case, HORS
                // capture en cours, demande a l'appelant de capturer UNE
                // touche pour CETTE case (voir requestManualAssignIndex,
                // main.cpp). Ignore les clics pendant qu'une capture tourne
                // deja - une seule a la fois, comme pour le wizard. !wizardActive
                // ajoute par precaution (defense en profondeur) : les deux
                // boutons "Demarrer"/"Reglages manuels" restent visibles en
                // meme temps, meme si main.cpp desactive deja manualMode des
                // qu'un wizard demarre (voir requestStartWizard).
                if (clicked && m.manualMode && !m.wizardActive && m.manualCaptureIndex < 0) {
                    m.requestManualAssignIndex = i;
                }
                const bool sel = (m.selection.kind == UISEL_PAD_BUTTON && m.selection.index == i);
                // Case actuellement demandee par l'assistant - meme surbrillance
                // que "sel", c'est la reponse directe au retour HB ("pourquoi on
                // a visuellement 21 touches si on peut pas les voir s'activer").
                const bool wizTarget = (m.wizardActive && !m.wizardComplete && !m.wizardCancelled &&
                                        m.wizardStepIndex == i);
                // Meme principe pour la capture manuelle : la case qu'on est
                // en train de reassigner s'allume, exactement comme wizTarget.
                const bool manualTarget = (m.manualCaptureIndex == i);
                GridCell(cp, ImVec2(cw, ch), m.grid[i], sel || wizTarget || manualTarget || hov);
            }
        } else if (m.advancedOpen) {
            // ---- REGLAGES AVANCES ----------------------------------------
            // Deux entrees seulement, chacune avec son explication et sa mise
            // en garde. Rien ici n'est un reglage de confort : on n'y vient
            // que pour resoudre un probleme, d'ou le texte plus long que
            // partout ailleurs.
            const float colW = (contentW - 24.0f) * 0.5f;
            ImVec2 mp = ImGui::GetCursorScreenPos();

            ImGui::BeginChild("##adv1", ImVec2(colW, contentH), false, ImGuiWindowFlags_NoScrollbar);
            SectionTitle(Tr(STR_UI_ADV_CALIB_TITLE), kColorMouseAccent);
            ImGui::Dummy(ImVec2(0, 6.0f));
            WrappedText(Tr(STR_CALIB_INTRO), kTxt2);
            ImGui::Dummy(ImVec2(0, 10.0f));
            if (GhostButton(Tr(STR_CALIB_BTN_START), kColorMouseAccent))
                m.requestStartCalibration = true;
            ImGui::EndChild();

            ImGui::SetCursorScreenPos(ImVec2(mp.x + colW + 24.0f, mp.y));
            ImGui::BeginChild("##adv2", ImVec2(colW, contentH), false, ImGuiWindowFlags_NoScrollbar);
            SectionTitle(Tr(STR_UI_ADV_FALLBACK_TITLE), kWarn);
            ImGui::Dummy(ImVec2(0, 6.0f));
            // Etat courant, TOUJOURS affiche. Un reglage qui casse la molette
            // et demande un redemarrage doit dire ou il en est : l'ancienne
            // version ne le montrait pas, et une session entiere a ete passee
            // a chercher pourquoi la molette ne repondait plus.
            WrappedText(Tr(m.fallbackActive ? STR_CALIB_FALLBACK_ACTIVE : STR_CALIB_AUTO_ACTIVE),
                        m.fallbackActive ? kWarn : kColorKeyboardAccent);
            ImGui::Dummy(ImVec2(0, 6.0f));
            WrappedText(Tr(STR_CALIB_FALLBACK_EXPLAIN), kTxt2);
            ImGui::Dummy(ImVec2(0, 10.0f));
            // La desactivation est AUSSI accessible que l'activation : un
            // reglage qui casse la molette et demande un redemarrage ne doit
            // jamais etre plus facile a mettre qu'a enlever.
            if (m.fallbackActive) {
                if (GhostButton(Tr(STR_CALIB_BTN_RESTORE_AUTO), kWarn)) m.requestFallback = 0;
            } else {
                if (GhostButton(Tr(STR_CALIB_BTN_FORCE_FALLBACK), kWarn)) m.requestFallback = 1;
            }
            ImGui::EndChild();
        } else {
            // ---- REGLAGES SOURIS -----------------------------------------
            // Deux colonnes : deplacement a gauche, boutons a droite.
            //
            // La colonne de droite avait DISPARU dans la refonte : les cinq
            // affectations (clics gauche/droit/milieu, molette avant/arriere)
            // existaient toujours dans le fichier de configuration et etaient
            // toujours appliquees par le plugin, mais plus rien ne permettait
            // de les changer. Elles sont retablies ici.
            // Les huit lignes sont numerotees 0..7 dans l'ordre d'affichage.
            // Ce numero EST l'identite de la ligne : c'est lui que la selection
            // memorise et que la resolution en haut de fonction retraduit en
            // texte. Aucune chaine n'est recopiee d'un endroit a l'autre.
            const float colW = (contentW - 24.0f) * 0.5f;
            ImVec2 mp = ImGui::GetCursorScreenPos();

            // -- Colonne gauche : deplacement (lignes 0 a 2) --
            //
            // Le texte "boutons lateraux bientot" n'est pas une ligne de la
            // pile : une hauteur lui est reservee EN BAS (hintH) avant
            // d'appeler UiMakeStack, pour que la pile (qui remplit desormais
            // toute la hauteur disponible - voir UiMakeStack, ui_layout.cpp)
            // ne l'ecrase pas hors-cadre.
            ImGui::BeginChild("##ms1", ImVec2(colW, contentH), false, ImGuiWindowFlags_NoScrollbar);
            {
                // Zone du bas : texte d'information, disposition d'avant le
                // 2026-09-23. Le bouton de mesure y avait ete place ce jour-la,
                // mais il ne reagissait pas sur console (retour HB) : cette
                // zone est en limite basse du panneau, et une ligne d'etat qui
                // passait sur deux lignes le poussait en partie hors du cadre,
                // la ou ImGui ne detecte plus le survol. Il est desormais sous
                // le bouton Enregistrer, a gauche (bloc "stagel").
                PushX360Font(g_fontSmall);
                const float hintH = 14.0f + ImGui::GetTextLineHeight() * 2.0f;
                PopX360Font(g_fontSmall);

                ImVec2 rowsPos = ImGui::GetCursorScreenPos();
                ImGui::BeginChild("##ms1rows", ImVec2(colW, contentH - hintH), false, ImGuiWindowFlags_NoScrollbar);
                const UiStack s = UiMakeStack(3, 8.0f, 0.44f);
                if (SliderRow(s, 0, Tr(STR_SETTINGS_SENSITIVITY_LABEL),
                              m.selection.kind == UISEL_MOUSE_ROW && m.selection.index == 0,
                              m.sensitivity, m.sensitivityMin, m.sensitivityMax, kColorMouseAccent))
                    { m.selection.kind = UISEL_MOUSE_ROW; m.selection.index = 0; }
                if (SliderRow(s, 1, Tr(STR_SETTINGS_DEADZONE_LABEL),
                              m.selection.kind == UISEL_MOUSE_ROW && m.selection.index == 1,
                              m.deadzone, m.deadzoneMin, m.deadzoneMax, kColorMouseAccent))
                    { m.selection.kind = UISEL_MOUSE_ROW; m.selection.index = 1; }
                if (CheckRow(s, 2, Tr(STR_SETTINGS_INVERT_Y_LABEL),
                             m.selection.kind == UISEL_MOUSE_ROW && m.selection.index == 2,
                             m.invertY, kColorMouseAccent))
                    { m.selection.kind = UISEL_MOUSE_ROW; m.selection.index = 2; }
                ImGui::EndChild();

                ImGui::SetCursorScreenPos(ImVec2(rowsPos.x, rowsPos.y + (contentH - hintH) + 14.0f));
                WrappedText(Tr(STR_UI_EXTRA_BUTTONS_SOON), kTxt3);
            }
            ImGui::EndChild();

            // -- Colonne droite : boutons (lignes 3 a 7) --
            ImGui::SetCursorScreenPos(ImVec2(mp.x + colW + 24.0f, mp.y));
            ImGui::BeginChild("##ms2", ImVec2(colW, contentH), false, ImGuiWindowFlags_NoScrollbar);
            {
                SectionTitle(Tr(STR_SETTINGS_MOUSE_BUTTONS_HEADER), kTxt3);
                ImGui::Dummy(ImVec2(0, 4.0f));

                const StrId kLbl[5] = {
                    STR_SETTINGS_CLICK_LEFT_LABEL, STR_SETTINGS_CLICK_RIGHT_LABEL,
                    STR_SETTINGS_CLICK_MIDDLE_LABEL,
                    STR_SETTINGS_WHEEL_FORWARD_LABEL, STR_SETTINGS_WHEEL_BACKWARD_LABEL
                };
                int* const ptr[5] = { m.btnLeft, m.btnRight, m.btnMiddle,
                                      m.wheelForward, m.wheelBackward };

                const UiStack s = UiMakeStack(5, 6.0f, 0.44f, 30.0f);
                for (int i = 0; i < 5; i++) {
                    const int rowId = 3 + i;   // numerotation commune aux 8 lignes
                    if (ComboRow(s, i, Tr(kLbl[i]),
                                 m.selection.kind == UISEL_MOUSE_ROW && m.selection.index == rowId,
                                 ptr[i], m.buttonTargets, m.buttonTargetCount, kColorMouseAccent))
                        { m.selection.kind = UISEL_MOUSE_ROW; m.selection.index = rowId; }
                }
            }
            ImGui::EndChild();
        }

        EndPanel();
    }
    // Plus de trait de liaison badge -> case ici (retire le 2026-08-17,
    // retour HB : "pas besoin d'une fleche juste illuminer la touche sur
    // l'image") - le repere lumineux pose directement sur l'image
    // (DrawHotspot) montre desormais QUEL bouton, sans qu'un trait ait
    // besoin de traverser deux fenetres enfant differentes pour le relier.
}
