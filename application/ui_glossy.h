#pragma once
// ============================================================================
// PAGE "CLAVIER & SOURIS" - RENDU GLOSSY (2026-09-24)
//
// Meme ecran, meme logique, autre peinture. Cette page lit et ecrit EXACTEMENT
// le meme UiMappingModel que DrawUiMapping (ui_mapping.cpp) : main.cpp remplit
// le modele, traite les request* - rien ne change de ce cote. Seul le dessin
// est remplace : des textures PNG (kit _design_refs/xui_kit, embarque dans le
// .xex via glossy_assets.cpp) posees aux positions de glossy_layout.h, et des
// boutons invisibles ImGui par-dessus pour les clics, le survol et la
// navigation a la manette.
//
// Bascule : EN_GLOSSY_UI dans main.cpp. A 0, l'ancienne page revient telle
// quelle. Si une seule texture manque au chargement, main.cpp reste aussi sur
// l'ancienne page (UiGlossyReady).
//
// PORTABLE comme ui_mapping.h : ImGui + theme + i18n uniquement, pour que la
// previsualisation PC dessine le meme code que la console.
// ============================================================================
#include "ui_mapping.h"
#include "overlay_settings.h"

// tex : tableau de GA_COUNT entrees (voir glossy_assets.h), indexe par
// GlossyAssetId. Doit rester valide tant qu'il n'est pas remplace.
void UiGlossySetTextures(const UiSkinTex* tex);

// true si TOUTES les textures sont presentes - sinon on garde l'ancienne page
// plutot que d'afficher une page a trous.
bool UiGlossyReady();

// Dessine la page plein ecran (fenetre racine, coordonnees absolues) :
// en-tete avec onglets, barre d'info, clavier, manette, souris.
// activeTab est modifie si l'utilisateur change d'onglet (clic ou LB/RB).
void DrawUiGlossyMapping(UiMappingModel& m, int& activeTab, float screenW, float screenH);
void UiGlossyHeader(int& activeTab, float screenW);   // header + tabs only
void UiGlossySetExternalTabs(bool on);   // Windows: no tab bar in the header, the app draws its own


// Previsualisation uniquement : ouvre le menu des peripheriques (0 = claviers,
// 1 = souris) pour pouvoir le regarder sans clic.
void UiGlossyOpenDeviceMenu(int which);

// ============================================================================
// PAGES PLEIN ECRAN "Mesurer la souris" et "Calibrer" (2026-09-25)
//
// Meme principe que la page 1 : main.cpp garde TOUTE la logique (machine a
// etats de la mesure, CalibrationSession, fichiers) et remplit ces modeles ;
// la page dessine et renvoie les clics dans les champs req*.
// La page Calibrer accueille aussi les "Reglages avances" (mode de secours,
// effacer la calibration), qui n'etaient plus visibles depuis la page glossy.
// ============================================================================
struct GlossyMeasureModel {
    int  state;          // 0 = en attente du geste, 1 = glisser en cours, 2 = resultat
    bool noMouse;        // aucune donnee souris du plugin
    int  liveCounts;     // points parcourus pendant le geste
    int  resultCpc;      // points par cm (etat 2)
    int  resultDpi;
    bool implausible;
    bool saveFailed;
    int  savedDpi;       // mesure deja enregistree pour cette souris (0 = aucune)
    bool reqSave, reqRetry, reqClose;
    GlossyMeasureModel() : state(0), noMouse(false), liveCounts(0), resultCpc(0), resultDpi(0),
        implausible(false), saveFailed(false), savedDpi(0), reqSave(false), reqRetry(false), reqClose(false) {}
};
void DrawUiGlossyMeasure(GlossyMeasureModel& mm, float screenW, float screenH);

struct GlossyCalibModel {
    bool        running;        // une session de calibration existe
    const char* prompt;         // consigne de l'etape en cours
    float       progress;       // 0..1
    bool        canSkip;        // "pas de molette" propose
    bool        noData;         // plugin muet / souris absente
    bool        finished, succeeded, saveAttempted, saveOk;
    const char* summary;
    int         mouseVid, mousePid;
    bool        fallbackActive; // mode de secours en vigueur
    bool reqStart, reqValidate, reqSkipWheel, reqCancel, reqSave, reqClose, reqClearCalib;
    int  reqFallback;           // -1 rien, 0 = revenir en automatique, 1 = forcer le secours
    GlossyCalibModel() : running(false), prompt(0), progress(0.0f), canSkip(false), noData(false),
        finished(false), succeeded(false), saveAttempted(false), saveOk(false), summary(0),
        mouseVid(0), mousePid(0), fallbackActive(false), reqStart(false), reqValidate(false),
        reqSkipWheel(false), reqCancel(false), reqSave(false), reqClose(false), reqClearCalib(false),
        reqFallback(-1) {}
};
void DrawUiGlossyCalib(GlossyCalibModel& cm, float screenW, float screenH);

// ============================================================================
// PAGE "PROFILS DE JEUX" (2026-09-25)
//
// Bibliotheque des jeux avec pochette et nom exact (lus dans les donnees
// d'Aurora par main.cpp, voir aurora_library.h), et reglages souris du jeu
// selectionne. Le Title ID reste la cle mais n'est affiche qu'en petit.
// main.cpp charge les pochettes a la demande : la page lui dit quelles cases
// sont visibles (firstVisible/lastVisible).
// ============================================================================
struct GlossyGame {
    unsigned int titleId;
    const char*  name;        // nom exact (Aurora), ou nom saisi, ou "Title ID ..."
    ImTextureID  cover;       // 0 = pas encore chargee / absente -> vignette generee
    int          coverW, coverH;
    bool         hasProfile;
};

struct GlossyGamesModel {
    const GlossyGame* games;  int gameCount;
    int   selected;           // index dans games[], -1 = aucun (modifie par la page)
    int   filter;             // 0 = tous, 1 = avec profil (modifie par la page)
    int   scrollRow;          // premiere rangee affichee (modifie par la page)
    const char* mouseName;    // les profils appartiennent a cette souris
    bool  auroraFound;
    ImTextureID bigCover;  int bigW, bigH;   // pochette du jeu selectionne
    // reglages du profil edite (pointeurs vers l'etat de main.cpp)
    int*   sensitivity; int* deadzone; bool* invertY;
    int*   antiDeadzone;      // 2026-09-25 (0 = desactivee)
    int*   stickLayout;       // 2026-09-25, StickLayout (mapping.h)
    int*   antiDeadzoneY;     // 2026-09-26, 0 = identique a l'horizontal
    int*   smoothing;         // 2026-09-26, ms
    int*   curveType;   float* curveExp;
    // sorties
    int   firstVisible, lastVisible;       // indices dans games[] des cases visibles
    bool  reqSave, reqDelete, reqSelect;   // reqSelect : la selection vient de changer
    // renommage du jeu selectionne (saisie geree par main.cpp)
    bool        nameEditing;   // saisie en cours
    const char* nameBuf;       // texte en cours de saisie
    bool        reqEditName;   // clic sur le nom : commencer la saisie
    bool        reqSaveExplicit;  // clic sur "Enregistrer" (sinon : enregistrement auto au relachement)
    bool        reqDeleteAll;     // "Supprimer tous les profils" confirme
    int         camState;         // camera_addresses.txt : 0 = rien, 1 = horizontal, 2 = les deux axes
    bool        reqDeleteCam;     // "Supprimer la camera apprise" confirme
    double      savedAt;          // heure du dernier enregistrement (retour visuel), -1 = aucun
    bool        savedOk;
    GlossyGamesModel() : games(0), gameCount(0), selected(-1), filter(0), scrollRow(0), mouseName(0),
        auroraFound(false), bigCover(0), bigW(0), bigH(0), sensitivity(0), deadzone(0), invertY(0), antiDeadzone(0), stickLayout(0), antiDeadzoneY(0), smoothing(0),
        curveType(0), curveExp(0), firstVisible(0), lastVisible(-1), reqSave(false), reqDelete(false),
        reqSelect(false), nameEditing(false), nameBuf(0), reqEditName(false), reqSaveExplicit(false),
        reqDeleteAll(false), camState(0), reqDeleteCam(false), savedAt(-1.0), savedOk(false) {}
};
void DrawUiGlossyGames(GlossyGamesModel& gm, int& activeTab, float screenW, float screenH);

// ============================================================================
// PAGE "COMMENT CA MARCHE" (2026-09-25) : quatre etapes + raccourcis en jeu +
// depannage. Page statique : aucune donnee, seulement les onglets.
// ============================================================================
void DrawUiGlossyHowTo(int& activeTab, float screenW, float screenH);

// Paged tutorial (Windows "How it works", 2026-11-18): two guides (console Studio /
// Windows app) chosen at the top, one step per page, arrows or Left / Right to move.
// art: up to 4 glossy asset ids drawn on the right (-1 = none); a page-sized picture
// (pad, measure, gear) alone is scaled to the panel.
struct GlossyGuideStep {
    const char* title;
    const char* text;      // '\n' starts a new line
    int         art[4];
};
struct GlossyGuideModel {
    const char*            guideName[2];
    const char*            intro[2];
    const GlossyGuideStep* steps[2];
    int                    stepCount[2];
    const char*            prevLabel;
    const char*            nextLabel;
    int                    guide;   // in / out
    int                    step;    // in / out
};
void DrawUiGlossyGuide(GlossyGuideModel& gm, int& activeTab, float screenW, float screenH);

// ============================================================================
// PAGE "SOUTENIR" (2026-09-25) : version installee + mises a jour (QR vers la
// page des versions GitHub) + dons et liens. Les liens sont fournis par
// main.cpp deja verifies (IsLinkIntact) : 0 = lien altere, non affiche.
// ============================================================================
struct GlossySupportModel {
    const char* version;
    bool        pluginAlive;
    const char* releasesUrl;
    const char* kofiUrl;
    const char* paypal;
    const char* githubUrl;
    const char* youtubeUrl;
    GlossySupportModel() : version(0), pluginAlive(false), releasesUrl(0), kofiUrl(0), paypal(0),
        githubUrl(0), youtubeUrl(0) {}
};
void DrawUiGlossySupport(const GlossySupportModel& sm, int& activeTab, float screenW, float screenH);

// Ecran de demarrage anime (2026-09-26). t = secondes depuis le lancement.
// Renvoie false une fois termine (~2 s). Dessine par-dessus la page.
bool DrawUiGlossySplash(float t, float screenW, float screenH, const char* version);
void DrawUiGlossyToast(const char* msg, float alpha, float screenW, float screenH);
// Scrolling news-style band at the bottom (plugin not running and why).
void DrawUiGlossyTicker(const char* label, const char* msg, float timeSec, float screenW, float screenH);

// ============================================================================
// PAGE "AFFICHAGE EN JEU" (2026-11-01) : ce que l'overlay du plugin montre
// par-dessus le jeu. main.cpp charge / enregistre overlay_settings.txt
// (overlay_settings.h) ; la page edite *s et renvoie reqSave.
// ============================================================================
struct GlossyOverlayModel {
    OverlaySettings* s;
    ImTextureID previewTex;   // game capture for the preview, 0 = drawn scene
    ImTextureID overlayTex;   // in-game drawing for the preview (overlay_preview.cpp), 0 = none
    int    keyEvent;     // HID usage tout juste appuye (0 = rien), rempli par main.cpp
    bool   capturing;    // attend la touche du raccourci (modifie par la page)
    bool   reqSave;
    double savedAt;      // heure du dernier enregistrement, -1 = aucun
    bool   savedOk;
    GlossyOverlayModel() : s(0), previewTex(0), overlayTex(0), keyEvent(0), capturing(false), reqSave(false), savedAt(-1.0), savedOk(false) {}
};
void DrawUiGlossyOverlay(GlossyOverlayModel& om, int& activeTab, float screenW, float screenH);

// ============================================================================
// USB CONTROLLER PAGE (2026-11-03): same layout as the keyboard & mouse page.
// Each of the 17 Xbox outputs is bound to a button of the pad (idx = HID button
// usage - 1, as in X360Remap.json "buttons"). main.cpp reads/saves the entry
// (config_writer.h ReadPadConfig / ApplyPad*); the page edits the arrays and
// raises the req* flags. Macros are step lists (CONTRACT.md "macros", PadMacro in config_writer.h).
// ============================================================================
#ifndef PAD_CELLS
#define PAD_CELLS 17
#endif
#define PAD_MACROS_MAX 16       // MAX_MACROS of the plugin
#define PAD_STEPS_MAX 64        // MAX_MACRO_STEPS
// One step: the cells held (bit i = grid cell i) for ms milliseconds, 0 = while the trigger is held;
// mask 0 = pause. A macro = trigger button idx + its steps.
struct GlossyPadStep { unsigned mask; int ms; };
struct GlossyPadMacro { int btn; int stepCount; GlossyPadStep steps[PAD_STEPS_MAX]; };
struct GlossyPadModel {
    const UiDeviceEntry* pads; int padCount;
    int    sel;                      // controller shown (index in pads)
    bool   hasKeyboard, hasMouse;    // chips shown on this page too
    bool   reqRefresh;               // "Refresh devices" row of the pad drop-down
    bool   pluginAlive;
    const char* appVersion;
    bool   hasEntry, hasAxes;        // the plugin wrote this pad's settings
    int    btnIdx[PAD_CELLS];        // -1 = none (edited in place)
    bool   invertRY;
    int    deadzone;                 // 0..10000
    bool   hasLive;                  // the plugin sends padBtn
    unsigned liveMask;               // buttons held now
    int    captureCell;              // -1 = none; wizard when wizard = true
    bool   wizard;
    double captureUntil;
    GlossyPadMacro macros[PAD_MACROS_MAX]; int macroCount;
    // macro editor state (editMacro -1 = the main page of the controller)
    int    editMacro, editStep, stepScroll, macroScroll;
    bool   capTrigger;               // waiting for the trigger button press
    bool   recording, recBegan;      // "record from the controller"
    unsigned recMask; double recStart;
    bool   testing; double testStart;
    int    heldLastMs;               // duration restored when "while held" is unticked
    bool   reqSaveButtons, reqSaveStick;
    int    reqBack;                  // 0 keyboard page, 1 mouse page, -1 nothing
    int    reqOpenPad;
    GlossyPadModel() : pads(0), padCount(0), sel(0), hasKeyboard(false), hasMouse(false), reqRefresh(false), pluginAlive(false),
        appVersion(0), hasEntry(false), hasAxes(false), invertRY(false), deadzone(0), hasLive(false), liveMask(0),
        captureCell(-1), wizard(false), captureUntil(0.0), macroCount(0),
        editMacro(-1), editStep(0), stepScroll(0), macroScroll(0), capTrigger(false), recording(false), recBegan(false),
        recMask(0), recStart(0.0), testing(false), testStart(0.0), heldLastMs(80),
        reqSaveButtons(false), reqSaveStick(false), reqBack(-1), reqOpenPad(-1) {
        for (int i = 0; i < PAD_CELLS; i++) btnIdx[i] = -1;
        for (int i = 0; i < PAD_MACROS_MAX; i++) { macros[i].btn = 0; macros[i].stepCount = 0; }
    }
};
void DrawUiGlossyPad(GlossyPadModel& pm, int& activeTab, float screenW, float screenH);
