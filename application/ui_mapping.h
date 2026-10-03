#pragma once
// ============================================================================
// ECRAN "MAPPING" - SOURCE UNIQUE
//
// POURQUOI CE FICHIER EXISTE (2026-08-14)
//
// La mise en page vivait en double : dans application/main.cpp pour la console,
// et dans preview/preview_screen.cpp pour la previsualisation. Les deux ont
// diverge des la premiere journee - la previsualisation avait des cartes de
// peripheriques soignees, un bloc d'etat et les langues en bas de colonne, la
// console n'avait recu que le squelette. HB l'a constate directement :
// "beaucoup de choses ne concordent pas avec l'HTML".
//
// Ce fichier est desormais la SEULE source de la mise en page. La console et
// la previsualisation le compilent tous les deux et lui passent leurs donnees.
// Ce que la previsualisation montre est donc, litteralement, ce que la console
// affiche - la divergence devient impossible, pas seulement improbable.
//
// CONTRAINTE : ce fichier doit rester PORTABLE. Pas de xtl.h, pas de D3D9, pas
// de XexUtils, pas de chemins "HDD:\". Uniquement ImGui, theme.h et i18n.h -
// meme discipline que wizard_session.h/calibration_session.h, deja ecrits pour
// etre testables hors console.
//
// Les textures sont passees en ImTextureID (un simple pointeur opaque cote
// ImGui) : la console y met sa texture Direct3D9, la previsualisation y met
// zero et le code affiche alors un cadre a la place. Aucun des deux n'a besoin
// de connaitre l'autre.
// ============================================================================
#include "third_party/imgui/imgui.h"

// Marges de securite TV, en fraction de l'ecran. Le FOND occupe 100% de
// l'ecran (aspect plein ecran demande par HB), seul le CONTENU LISIBLE
// respecte ces marges - beaucoup de televiseurs rognent les bords, un texte
// colle au bord y disparait. Verticalement l'overscan est plus agressif,
// d'ou deux valeurs distinctes.
//
// DEFINIES ICI ET NULLE PART AILLEURS : elles l'etaient en double
// (main.cpp + ui_mapping.cpp), ce qui est exactement le piege qui avait fait
// diverger la mise en page de sa maquette.
extern const float kUiSafeX;
extern const float kUiSafeY;

// Une entree de la liste des peripheriques (colonne de gauche).
struct UiDeviceEntry {
    const char* name;   // "Logitech G213", ou libelle par defaut
    const char* id;     // "0461:3F41"
    bool        active; // le plugin recoit-il des donnees de ce peripherique
};

// Une case de la grille des assignations.
struct UiGridEntry {
    const char* button; // libelle court du bouton manette ("A", "LB", "D-Pad ^")
    const char* key;    // touche assignee, ou "-" si aucune
};

// ============================================================================
// SELECTION - ce que l'utilisateur est en train de regler
//
// POURQUOI (2026-08-14, remarque de HB : "pour le cadre ou y'a ecrit Bouton A,
// normalement c'est affichable seulement quand l'utilisateur commence a faire
// des reglages, et pas du tout de cette facon")
//
// Le panneau contextuel etait dessine a chaque frame avec un libelle ECRIT EN
// DUR ("BOUTON A") que main.cpp poussait dans le modele. Il ne representait
// rien : ni un choix de l'utilisateur, ni un etat de l'application. C'est
// exactement le "copier-coller sans lien reel entre fonctionnalite et
// interface" que HB pointe.
//
// Cette structure est ce lien. Elle dit UNE chose - quel element est en cours
// de reglage - et tout le contextuel en decoule :
//   - le panneau contextuel n'existe que si kind != UISEL_NONE ;
//   - sans selection, la zone qu'il occupait revient a l'image ;
//   - la case de grille selectionnee, le repere sur l'image et le trait de
//     liaison lisent tous les trois cette meme valeur.
//
// Consequence pratique : il devient impossible d'afficher un panneau
// contextuel qui ne corresponde a rien, parce qu'il n'y a plus de texte a
// afficher quand rien n'est selectionne.
// ============================================================================
enum UiSelKind {
    UISEL_NONE = 0,     // rien en cours - le panneau contextuel n'est pas dessine
    UISEL_PAD_BUTTON,   // une case de la grille des 21 boutons (mode clavier)
    UISEL_MOUSE_ROW     // une ligne de reglage souris (mode souris)
};

struct UiSelection {
    UiSelKind kind;
    int       index;    // index dans grid[], ou numero de ligne souris

    UiSelection() : kind(UISEL_NONE), index(-1) {}
};

// Tout ce dont l'ecran a besoin. L'appelant remplit, l'ecran dessine : aucune
// lecture de fichier, aucun acces materiel ici.
struct UiMappingModel {
    // Etat de navigation (l'ecran peut les MODIFIER : ce sont les interactions)
    int   mode;             // 0 = clavier, 1 = souris
    int   langIndex;        // 0 = FR, 1 = EN, 2 = PT
    int   keyboardIndex;    // index selectionne dans keyboards[]
    int   mouseIndex;       // index selectionne dans mice[]

    // Peripheriques
    const UiDeviceEntry* keyboards;   int keyboardCount;
    const UiDeviceEntry* mice;        int mouseCount;
    int   detectedCount;              // pour le bloc d'etat
    bool  pluginAlive;                // le plugin ecrit-il toujours son etat

    // Visuel du peripherique courant. Zero = pas d'image, un cadre est affiche
    // a la place (cas de la previsualisation, ou d'un echec de chargement).
    ImTextureID texKeyboardSide;  int texKbW, texKbH;   // la manette
    ImTextureID texMouseSide;     int texMsW, texMsH;   // la souris

    // Ce que l'utilisateur regle en ce moment. Modifie PAR L'ECRAN quand il
    // clique une case ou une ligne ; l'appelant n'a rien a y mettre.
    //
    // Remplace les anciens ctxButton/ctxKey, qui etaient deux chaines poussees
    // depuis main.cpp et qui valaient toujours "BOUTON A" - un panneau qui
    // affichait donc en permanence un reglage que personne n'avait demande.
    UiSelection selection;

    // Grille des assignations (mode clavier)
    const UiGridEntry* grid;  int gridCount;

    // Reglages souris (mode souris) - pointeurs pour que l'ecran puisse les
    // modifier directement, comme n'importe quel widget ImGui.
    int*  sensitivity;  int sensitivityMin, sensitivityMax;
    int*  deadzone;     int deadzoneMin,    deadzoneMax;
    int*  antiDeadzone; int antiDeadzoneMin, antiDeadzoneMax;   // 2026-09-25, 0 = desactivee
    int*  antiDeadzoneY;                                         // 2026-09-26, 0 = identique a l'horizontal
    int*  smoothing;    int smoothingMax;                        // 2026-09-26, ms, 0 = desactive
    bool* invertY;

    // Resolution MESUREE de la souris selectionnee, en DPI (2026-09-23).
    // 0 = jamais mesuree. Affichee sous les reglages de deplacement, avec le
    // bouton qui lance la mesure (requestMeasureMouse).
    int   mouseDpiMeasured;

    // --- Mapping des boutons souris (retabli le 2026-08-14) ----------------
    // Ces cinq reglages existaient dans l'ancien onglet "Reglages souris" et
    // avaient disparu dans la refonte : la nouvelle page ne les affichait
    // nulle part, alors que la couche de sauvegarde et le plugin les gerent
    // toujours. Les valeurs restaient donc actives sans etre modifiables.
    //
    // Les libelles de cible sont fournis PAR L'APPELANT (tableau de chaines
    // deja traduites) : l'ecran n'a pas a connaitre la liste des boutons
    // manette ni son ordre.
    const char* const* buttonTargets;  int buttonTargetCount;
    int* btnLeft;
    int* btnRight;
    int* btnMiddle;
    int* wheelForward;
    int* wheelBackward;

    // Page "glossy" (ui_glossy.cpp, 2026-09-24) : pour chaque cible de
    // buttonTargets[], l'image de badge a afficher (valeur GlossyAssetId, -1 =
    // pas de badge, cible "Par defaut"). Meme ordre que buttonTargets, fourni
    // par l'appelant pour la meme raison : l'ecran ne connait pas la liste.
    const int* buttonTargetBadge;
    // Version affichee dans la barre d'information ("1.2.0").
    const char* appVersion;
    // Reglages par defaut (page glossy, 2026-09-25). hasSavedDefault : un
    // defaut a deja ete enregistre (sinon "restaurer" = reglages d'origine).
    bool hasSavedDefault;
    bool requestSaveDefault;
    bool requestRestoreDefault;

    // --- Reglages avances --------------------------------------------------
    // Troisieme etat du panneau du bas, au meme titre que Clavier et Souris
    // (structure de la maquette). L'etat vit ICI plutot que dans l'appelant :
    // c'est une affaire d'affichage, l'ouvrir ne declenche aucune action.
    bool advancedOpen;
    bool fallbackActive;      // mode de secours en vigueur pour cette souris

    // --- Assistant clavier en direct (2026-08-17) ---------------------------
    // Retour HB : l'assistant prenait tout l'ecran en noir - "pourquoi on a
    // visuellement 21 touches de la manette si on peut pas les voir
    // s'activer pour les choix du wizard". L'assistant reste desormais SUR
    // cette page : la case en cours s'allume dans la grille (voir grid[] et
    // wizardStepIndex, meme ordre/index que DefaultKeyboardWizardSteps cote
    // appelant), et ce bloc remplace juste le bouton "Demarrer" + l'accroche
    // pendant que la session est active. Cet ecran ne fait que LIRE ces
    // champs (portabilite, voir en-tete de fichier) - toute la logique
    // (WizardSession/MappingAssistant) reste cote appelant.
    bool  wizardActive;        // une session existe (meme complete/annulee, tant qu'elle n'est pas fermee)
    int   wizardStepIndex;     // index dans grid[] du bouton actuellement demande
    int   wizardStepTotal;
    int   wizardResultsCount;
    const char* wizardPrompt;   // "Appuie sur une touche pour : A"
    const char* wizardMessage;  // message ponctuel (etape passee, etc) - vide si rien
    bool  wizardNoKeyboard;     // input_state.json indisponible/perime
    bool  wizardComplete;
    bool  wizardCancelled;
    bool  wizardSaveOk;         // valide seulement si wizardComplete
    bool  requestWizardClose;   // l'utilisateur a clique OK - l'appelant detruit la session

    // --- Reglages manuels (2026-08-17) ---------------------------------------
    // Retour HB : refaire les 21 etapes du wizard pour changer UN SEUL bouton
    // est inutile - "on ajoute un button reglages manuelle et a chaque click
    // sur un button l'utilisateur choisi une touche clavier". manualMode est
    // une simple bascule (bouton dans stagel) ; une fois active, cliquer une
    // case de la grille demande une capture pour CETTE case uniquement (pas
    // de sequence, pas d'etapes). Volontairement un jeu de champs SEPARE de
    // wizard* plutot que reutilise : ce mode n'a ni etapes ni bouton OK final,
    // la capture se referme d'elle-meme des qu'une touche est assignee.
    // Jamais actifs en meme temps que wizardActive cote appelant (une seule
    // session a la fois, voir main.cpp).
    bool  manualMode;             // bascule "Reglages manuels" (bouton dans stagel)
    int   manualCaptureIndex;     // -1 = aucune capture en cours, sinon index dans grid[]
    const char* manualPrompt;     // "Appuie sur une touche pour : A" - deja forme cote appelant
    const char* manualMessage;    // message ponctuel (conflit, etc) - vide si rien
    bool  manualNoKeyboard;       // input_state.json indisponible/perime

    // Actions demandees par l'utilisateur pendant cette frame. L'ecran les
    // met a true ; l'appelant les traite et les remet a false. Ca evite de
    // passer des fonctions de rappel, donc de rendre ce fichier dependant de
    // quoi que ce soit.
    bool  requestToggleManualMode;
    int   requestManualAssignIndex; // -1 = rien ; sinon case cliquee en mode manuel (hors capture)
    bool  requestStartCalibration;
    bool  requestMeasureMouse;   // bouton "Mesurer la souris" (2026-09-23)
    bool  requestSaveButtons;
    int   requestFallback;    // -1 = rien, 0 = desactiver, 1 = activer

    // Actions demandees par l'utilisateur pendant cette frame. L'ecran les
    // met a true ; l'appelant les traite et les remet a false. Ca evite de
    // passer des fonctions de rappel, donc de rendre ce fichier dependant de
    // quoi que ce soit.
    bool  requestStartWizard;
    bool  requestRefreshDevices;
    bool  requestSaveMouse;
    bool  requestAdvanced;

    UiMappingModel();   // valeurs par defaut sures (tout a zero/nullptr)
};

// ============================================================================
// SKIN - textures optionnelles qui remplacent le dessin au trait
//
// POURQUOI UN ENUM SEPARE DE UiAssetId (application/ui_assets.h) : ce fichier
// doit rester portable (voir la contrainte en tete de fichier), et
// ui_assets.h ne peut pas l'etre - il inclut xtl.h/d3d9.h pour IDirect3DTexture9.
// Impossible donc de reutiliser directement UiAssetId ici.
//
// Plutot que de dupliquer l'ORDRE des deux enums (et risquer exactement le
// piege de desynchronisation que ce projet a deja rencontre avec la table
// i18n), l'appelant (main.cpp) remplit ce tableau CASE PAR CASE, par nom, pas
// par copie en boucle - un decalage d'un seul cran ne peut donc pas se
// produire silencieusement, une entree oubliee laisse juste ce slot a
// "absent" (repli procedural normal, pas un plantage).
// ============================================================================
enum UiSkinSlot {
    SKIN_PANEL_DEFAULT, SKIN_PANEL_CTX, SKIN_CARD_DEVICE,
    SKIN_CELL_GRID, SKIN_FIELD_COMBO, SKIN_BUTTON_ACCENT, SKIN_BUTTON_GHOST,
    SKIN_ICON_KEYBOARD, SKIN_ICON_MOUSE, SKIN_ICON_GEAR, SKIN_ICON_PLAY,
    SKIN_ICON_LIST, SKIN_ICON_HELP, SKIN_ICON_HEART,
    SKIN_LOGO_WORDMARK,
    SKIN_SLOT_COUNT
};

// Une texture de skin, deja convertie en type portable (ImTextureID au lieu
// de IDirect3DTexture9*). sliceL/T/R/B = 0 partout : pas de 9-slice, l'image
// est redimensionnee entiere en gardant ses proportions (icones, logo).
struct UiSkinTex {
    ImTextureID tex;
    int w, h;
    int sliceL, sliceT, sliceR, sliceB;
    bool present;
    UiSkinTex() : tex(0), w(0), h(0), sliceL(0), sliceT(0), sliceR(0), sliceB(0), present(false) {}
};

// Fournit les textures de skin pour cette frame. tex doit pointer vers un
// tableau de SKIN_SLOT_COUNT entrees et rester valide tant qu'il n'est pas
// remplace ; 0/nullptr desactive le skin (repli complet sur le dessin
// procedural - c'est le comportement par defaut si cette fonction n'est
// jamais appelee, donc la previsualisation n'a rien a faire de special).
void UiSetSkin(const UiSkinTex* tex);

// Dessine l'ecran complet dans la region courante. L'appelant a deja place le
// curseur et cree la zone (voir main.cpp / preview_main.cpp).
void DrawUiMapping(UiMappingModel& m);

// Barre superieure : bandeau pleine largeur, logo, onglets. Separee parce
// qu'elle est commune a toutes les pages, pas seulement a Mapping.
// activeTab est modifie si l'utilisateur change d'onglet.
void DrawUiTopBar(float screenW, float screenH, int& activeTab);

// Hauteur totale du bandeau superieur, bord haut de l'ecran inclus.
//
// EXISTE POUR EVITER UNE VALEUR EN DOUBLE : la hauteur etait ecrite "46.0f"
// dans trois fichiers (la barre elle-meme, main.cpp et la previsualisation).
// La changer a un seul endroit decalait silencieusement le corps de page dans
// les deux autres - le corps passait sous la barre ou laissait un trou.
float UiTopBarHeight(float screenH);
