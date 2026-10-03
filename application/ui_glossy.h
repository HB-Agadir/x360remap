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
    double      savedAt;          // heure du dernier enregistrement (retour visuel), -1 = aucun
    bool        savedOk;
    GlossyGamesModel() : games(0), gameCount(0), selected(-1), filter(0), scrollRow(0), mouseName(0),
        auroraFound(false), bigCover(0), bigW(0), bigH(0), sensitivity(0), deadzone(0), invertY(0), antiDeadzone(0), stickLayout(0), antiDeadzoneY(0), smoothing(0),
        curveType(0), curveExp(0), firstVisible(0), lastVisible(-1), reqSave(false), reqDelete(false),
        reqSelect(false), nameEditing(false), nameBuf(0), reqEditName(false), reqSaveExplicit(false),
        reqDeleteAll(false), savedAt(-1.0), savedOk(false) {}
};
void DrawUiGlossyGames(GlossyGamesModel& gm, int& activeTab, float screenW, float screenH);

// ============================================================================
// PAGE "COMMENT CA MARCHE" (2026-09-25) : quatre etapes + raccourcis en jeu +
// depannage. Page statique : aucune donnee, seulement les onglets.
// ============================================================================
void DrawUiGlossyHowTo(int& activeTab, float screenW, float screenH);

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
