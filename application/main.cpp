// application.xex - homebrew graphique de configuration (wizard clavier,
// reglages souris, profils a venir). Voir ARCHITECTURE.md pour la decision
// XUI -> Dear ImGui du 2026-08-01.
//
// STATUT : PREMIER JET NON COMPILE, comme le reste des fichiers touchant a
// D3D9/XUI/XInput dans ce dossier - je ne peux ni compiler ni executer de
// code Xbox depuis mon bac a sable. Voir BUILD_APPLICATION.md pour la marche
// a suivre en cas d'erreur de build (attendu, pas une surprise).
//
// Dependances a ajouter au projet avant de compiler (voir BUILD_APPLICATION.md) :
//  - Coeur Dear ImGui (third_party/imgui/, voir VENDORING.md - fichiers non
//    fournis ici, a copier depuis la release officielle v1.90.9)
//  - XexUtils (github.com/ClementDreptin/XexUtils, MIT) comme reference de
//    projet VS, pour Xam::Notify (notifications natives) et Input (detection
//    d'appui bouton) plutot que du code XNotifyUI/XInput fait main.

#include <xtl.h>
#include <d3d9.h>

// Absente des en-tetes XDK de l'application (erreur C2065 au premier build du
// 2026-09-23). Le plugin l'obtient via xkelib, que ce projet-ci n'inclut pas.
// Valeur standard Windows : GetFileAttributesA renvoie 0xFFFFFFFF en cas
// d'echec (fichier ou dossier absent).
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_dx9_xenon.h"

#include "wizard_session.h"
#include "calibration_session.h" // assistant de calibration souris (2026-08-03)
#include "input_state_reader.h"
#include "config_writer.h"
#include "i18n.h"
#include "theme.h"
#include "donation_links.h" // liens de soutien au projet, onglet "Soutenir le projet" (2026-08-11)
#include "texture_loader.h"
#include "ui_assets.h"
#include "image_data_manette.h"
#include "image_data_souris.h"
#include "image_data_qrcode.h"
#include "ui_mapping.h" // ECRAN PARTAGE avec la previsualisation - voir preview/LISEZ-MOI.md
#include "ui_glossy.h"     // page Clavier & souris "glossy" (2026-09-24)
#include "glossy_assets.h" // ses textures PNG embarquees
#include "aurora_library.h" // noms exacts + pochettes des jeux (page Profils de jeux)
#include <algorithm>

// XexUtils - vendorise dans XexUtils/ (voir
// BUILD_APPLICATION.md et ARCHITECTURE.md pour comment il est integre).
// API reelle confirmee en lisant XexUtils/src/Xam.h et Input.h directement
// (2026-08-01) - remplace les noms devines dans une premiere version de ce
// fichier (Xam::Notify(wstring), XInputGetState brut) :
//  - XexUtils::Xam::XNotify(const std::string&, XNOTIFYQUEUEUI_TYPE) : notif
//    systeme, prend une string etroite (pas wstring) et convertit en interne.
//  - XexUtils::Input::GetInput(uint32_t userIndex = 0) -> Gamepad* : jamais
//    nullptr (retourne toujours un pointeur vers un slot statique interne,
//    meme si la manette est deconnectee), avec wButtons/PressedButtons deja
//    debounces et ThumbLeftX/Y deja normalises en [-1,+1] avec deadzone
//    appliquee - plus besoin de le refaire a la main.
//
// Chemin relatif explicite (pas juste "Input.h"/"Xam.h") et PAS de
// XexUtils/src dans AdditionalIncludeDirectories - voir application.vcxproj
// pour pourquoi (Math.h y portait le meme nom que le math.h standard et
// shadowait tous les #include <math.h>/<cmath> du projet, insensibilite a
// la casse de Windows oblige - cause reelle de la cascade d'erreurs
// "acosf n'est pas membre de global namespace" du premier build). Un chemin
// relatif explicite n'a pas ce probleme : seul le dossier du fichier
// includant (ici XexUtils/src lui-meme, pour les #include internes de
// Input.h/Xam.h) est cherche, jamais tout le projet.
//
// <cstdint>/<string>/<vector> ajoutes explicitement avant : Input.h/Xam.h
// utilisent uint32_t/std::string/std::vector sans les inclure eux-memes (ils
// sont ecrits pour etre inclus apres XexUtils/src/pch.h, qui les fournit -
// voir Input.cpp/Xam.cpp qui font "#include \"pch.h\"" en premiere ligne).
// Erreur reelle obtenue au premier build : "'string': is not a member of
// 'std'" sur Xam.h.
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <cmath> // cosf/sinf - etoile du drapeau marocain, page Soutenir (2026-08-17)
#include <cstdarg>
#include <cstdlib> // strtoul - saisie du titleId hexadecimal, onglet Profils (Jalon 7)
#include <cstdio>  // std::remove - bascule du mode compatibilite, onglet Calibration
#include <string.h>
#include "../XexUtils/src/Input.h"
#include "../XexUtils/src/Xam.h"
#include "../XexUtils/src/Filesystem.h"

// --- Log persistant cote appli --------------------------------------------
// Demande utilisateur du 2026-08-02 : "un systeme plus robuste de
// verification et de log... pour eviter l'aller-venu entre pc et console".
// Meme esprit que FileLog() dans X360Remap/main.cpp (deja existant, cote
// plugin) mais ici cote application.xex - un seul fichier a recuperer via
// "rgh fs cat" apres coup regroupe l'historique des sauvegardes tentees
// (wizard, reglages souris, boutons souris, defaut), avec succes/echec
// explicite plutot qu'une notification XNotify ephemere qu'on peut manquer.
// Troncature au premier appel de chaque lancement de application.xex
// (2026-08-03, meme demande HB que pour X360Remap_plugin.log : le fichier
// s'accumulait sur plusieurs lancements/redemarrages, melangeant plusieurs
// sessions de test dans le meme rgh fs cat). L'appli est mono-thread au
// sens ou AppLog n'est appele que depuis la boucle ImGui principale, pas de
// synchronisation requise.
static bool g_appLogTruncatedThisRun = false;
static void AppLog(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    buf[sizeof(buf) - 1] = '\0';
    va_end(args);

    std::ios_base::openmode mode = std::ios::binary;
    mode |= g_appLogTruncatedThisRun ? std::ios::app : std::ios::trunc;

    std::ofstream f("HDD:\\X360RemapStudio\\X360Remap_studio.log", mode);
    if (f.is_open()) {
        f << "[" << GetTickCount() << "ms] " << buf << "\r\n";
        g_appLogTruncatedThisRun = true;
    }
}

// --- Etat de l'application -----------------------------------------------
// Refonte v1.2 (2026-08-13) : 4 onglets au lieu de 6. Voir PLAN_V1.2.md.
//
// L'interface melangeait deux natures differentes dans la meme barre
// d'onglets : des PAGES D'ETAT (on consulte, on ajuste, on passe de l'une a
// l'autre librement) et des PARCOURS GUIDES (wizard clavier, calibration
// souris - ils prennent la main du debut a la fin, en plusieurs etapes).
// Les parcours ne sont plus des onglets : ils se lancent depuis la page
// Mapping et s'affichent en SUPERPOSITION PLEIN ECRAN, sans barre d'onglets
// visible pendant leur deroulement.
//
// Il n'y a volontairement PAS d'enum d'etat de superposition : g_wizard et
// g_calib (deja existants) suffisent a savoir qu'un parcours est en cours.
// Une variable de plus serait un deuxieme etat a garder synchronise avec
// eux, donc une source de desynchronisation pour rien.
enum AppTab {
    TAB_MAPPING = 0,   // clavier + souris reunis, avec bascule (idee HB)
    TAB_PROFILES,
    TAB_HOWTO,
    TAB_SUPPORT,
};

// Bascule du panneau inferieur de la page Mapping. Le haut de la page ne
// change pas d'un mode a l'autre - seul ce panneau est remplace.
enum MappingMode {
    MAPPING_KEYBOARD = 0,
    MAPPING_MOUSE,
};
static MappingMode g_mappingMode = MAPPING_KEYBOARD;

// --- Visuels manette / souris (refonte v1.2, 2026-08-13) --------------------
// Images PNG embarquees dans le binaire (image_data_manette.cpp / _souris.cpp,
// generees depuis _design_refs/) et decodees UNE SEULE FOIS au demarrage vers
// des textures Direct3D9 - voir texture_loader.h pour le piege des textures
// tuilees propre a cette console.
//
// Pourquoi embarquees plutot que lues sur le disque : meme raison que pour la
// police (voir theme.h) - l'utilisateur ne doit avoir aucun fichier a se
// procurer ni a deposer. Les deux images pesent environ 700 Ko dans le
// binaire, et 1,4 Mo chacune en memoire video une fois decodees : negligeable
// sur les 512 Mo de la console, et sans effet sur les performances puisque le
// decodage n'a lieu qu'au lancement.
//
// Ces pointeurs restent a nullptr si le chargement echoue. TOUT affichage doit
// donc les tester : une image absente ne doit jamais empecher l'ecran de
// fonctionner, seulement le priver de son illustration.
// Skin visuel charge depuis HDD:\X360RemapStudio\ui_skin\<nom>\ (voir
// ui_assets.h). Nom en dur pour l'instant - un seul skin existe. Deviendra un
// choix utilisateur (comme la langue) quand plusieurs skins existeront.
static const char* kActiveSkinName = "default";
static UiSkin g_uiSkin;

static IDirect3DTexture9* g_texManette = nullptr;
static int g_texManetteW = 0, g_texManetteH = 0;
static IDirect3DTexture9* g_texSouris = nullptr;
static int g_texSourisW = 0, g_texSourisH = 0;
// QR Ko-fi (page Soutenir, 2026-08-17) - meme mecanique que les deux
// textures ci-dessus (PNG embarque, voir image_data_qrcode.h).
static IDirect3DTexture9* g_texQrKofi = nullptr;
static int g_texQrKofiW = 0, g_texQrKofiH = 0;

static AppTab g_activeTab = TAB_MAPPING;
// Device D3D, pour charger les pochettes a la demande depuis la page Profils.
static IDirect3DDevice9* g_device = nullptr;

// ============================================================================
// PAGE "CLAVIER & SOURIS" GLOSSY (2026-09-24) - interrupteur
//
// 1 = nouvelle page (ui_glossy.cpp : textures du kit _design_refs/xui_kit,
//     embarquees dans le .xex). 0 = ancienne page, exactement comme avant.
// Meme si c'est 1, l'ancienne page revient toute seule si UNE texture n'a pas
// pu etre creee au demarrage (voir "App: glossy" dans X360Remap_studio.log).
// Seul le DESSIN change : la page lit et ecrit le meme UiMappingModel, toutes
// les actions (assistant, capture, sauvegarde, mesure, calibration) passent
// par les memes request* traites dans DrawMappingTab().
// ============================================================================
#define EN_GLOSSY_UI 1

static IDirect3DTexture9* g_glossyTex[GA_COUNT];
static UiSkinTex          g_glossySkinTex[GA_COUNT];
// Onglet choisi depuis la page glossy (elle dessine sa propre barre d'onglets).
static int                g_glossyTabOut = 0;

static bool GlossyActive() {
#if EN_GLOSSY_UI
    return UiGlossyReady();
#else
    return false;
#endif
}
static WizardSession* g_wizard = nullptr;
static CalibrationSession* g_calib = nullptr;
// Page glossy "Calibrer" ouverte (2026-09-25) : elle montre d'abord
// l'explication + les reglages avances ; la session ne demarre qu'au clic
// "Demarrer la calibration".
static bool g_calibPageOpen = false;
// Sortie vers le tableau de bord (2026-09-26) : Echap deux fois.
static bool g_gamesRenaming = false;    // saisie du nom d'un jeu en cours (Echap = annuler)
static bool g_exitToDashboard = false;

// static plutot que locales a UpdateWizardModel() : evite d'appeler
// SaveResult() a CHAQUE FRAME tant que l'utilisateur n'a pas clique "OK" (bug
// reel corrige le 2026-08-02 - en plus d'etre inutile, ca aurait aussi
// pollue X360Remap_studio.log d'une ligne par frame). A l'echelle du fichier
// (pas static-locales a la fonction) car le clic "OK" qui doit les remettre a
// zero est desormais traite dans DrawMappingTab(), pas dans la fonction qui
// les a posees (voir requestWizardClose, 2026-08-17).
static bool g_wizardSaveAttempted = false;
static bool g_wizardSaveOk = false;

// --- Reglages manuels (2026-08-17) ------------------------------------------
// "un click, une touche" : g_manualMode est la bascule persistante (bouton
// dans stagel), g_manualSession/g_manualCaptureIndex n'existent que pendant
// la capture d'UNE case precise. Reutilise WizardSession/MappingAssistant
// avec une liste d'UN SEUL AssistantStep - meme machine a etats deja testee
// que le wizard sequentiel, voir UpdateManualAssignModel plus bas pour la
// seule vraie difference : la sauvegarde FUSIONNE au lieu de remplacer tout
// le keyMap (ApplyKeyboardMappingEntry, pas SaveResult()).
static bool g_manualMode = false;
static WizardSession* g_manualSession = nullptr;
static int g_manualCaptureIndex = -1;

// Snapshot lu une seule fois par frame (dans main(), voir la boucle plus
// bas) et partage par UpdateWizardModel()/UpdateMouseCursor() - evite 2 lectures
// disque separees du meme fichier par frame comme le faisait la toute
// premiere version. Ajoute le 2026-08-01 en meme temps que le diagnostic
// visuel ci-dessous (voir DrawDebugOverlay), suite a un retour utilisateur
// ou ni le clavier ni le curseur souris ne fonctionnaient - ce global rend
// aussi possible d'afficher exactement ce que le jeu voit, plutot que de
// deviner cote code.
static InputStateSnapshot g_lastSnapshot;

// VID/PID en dur pour ce premier jet - un vrai ecran de selection de
// peripherique (liste de ce qui est present dans X360Remap.json) viendra une
// fois cet ecran unique confirme buildable et navigable sur console.
static const uint16_t kKeyboardVid = 1121;
static const uint16_t kKeyboardPid = 16193;
static const uint16_t kMouseVid = 1121;
static const uint16_t kMousePid = 20021;

// Jalon 8 (application/ROADMAP_APPLICATION.md) - peripherique EFFECTIVEMENT
// utilise par tous les appels config_writer.h de ce fichier ; les constantes
// ci-dessus ne restent qu'une valeur de repli (premier lancement, aucun
// device encore dans X360Remap.json - voir ListKnownDevices). Initialisees
// au repli par defaut, changees uniquement par DrawDeviceSelector plus bas
// (voir aussi son commentaire pour le detail des heuristiques clavier/souris).
static uint16_t g_selectedKeyboardVid = kKeyboardVid;
static uint16_t g_selectedKeyboardPid = kKeyboardPid;
static uint16_t g_selectedMouseVid = kMouseVid;
static uint16_t g_selectedMousePid = kMousePid;

// Reglages souris en cours d'edition - charges une fois au demarrage de
// l'onglet, ecrits sur disque uniquement quand l'utilisateur confirme
// (bouton "Enregistrer"), pas a chaque frame.
static int g_mouseSensitivity = 10000;
static int g_mouseDeadzone = 0;
static int g_mouseAntiDeadzone = 0;   // 2026-09-25, 0 = desactivee
static int g_mouseAntiDeadzoneY = 0;  // 2026-09-26, 0 = identique a l'horizontal
static int g_mouseSmoothing = 0;      // 2026-09-26, ms
static bool g_mouseInvertY = false;
static bool g_mouseSettingsLoaded = false;

// Mapping des 3 clics souris (gauche/droit/milieu, idx 0/1/2 - convention
// HID boot-mouse) vers un bouton Xbox. Demande utilisateur du 2026-08-02
// ("pourquoi y'a pas un mapping pour les buttons souris"). Liste
// volontairement restreinte aux cibles les plus utiles plutot que les 17
// champs de kButtonFieldNames en entier (mapping.cpp) - "Par defaut" (index
// 0) signifie "ne pas ecrire d'entree pour cet index", ce qui laisse
// X360Remap.xex appliquer son fallback code en dur (gauche->r2, droit->l2,
// milieu->r3, voir HidFillMouseState dans X360Remap/main.cpp).
// ATTENTION (limite connue, acceptee pour ce round) : "Enregistrer boutons
// souris" remplace TOUJOURS entierement les entrees idx 0/1/2 par ce que
// montrent ces 3 combos - une entree existante avec un champ hors de cette
// liste (improbable en pratique pour un clic souris) serait affichee comme
// "Par defaut" et donc perdue si on clique Enregistrer sans y prendre garde.
struct MouseButtonTarget { StrId labelId; const char* fieldName; };
static const MouseButtonTarget kMouseButtonTargets[] = {
    { STR_SETTINGS_TARGET_DEFAULT, nullptr },
    { STR_SETTINGS_TARGET_A, "a_button" },
    { STR_SETTINGS_TARGET_B, "b_button" },
    { STR_SETTINGS_TARGET_X, "x_button" },
    { STR_SETTINGS_TARGET_Y, "y_button" },
    { STR_SETTINGS_TARGET_LB, "l1" },
    { STR_SETTINGS_TARGET_RB, "r1" },
    { STR_SETTINGS_TARGET_LT, "l2" },
    { STR_SETTINGS_TARGET_RT, "r2" },
    { STR_SETTINGS_TARGET_L3, "l3" },
    { STR_SETTINGS_TARGET_R3, "r3" },
    { STR_SETTINGS_TARGET_START, "start" },
    { STR_SETTINGS_TARGET_BACK, "back" },
    // D-Pad ajoute le 2026-08-03 (retour HB : "les choix de touche pour la
    // molette avant arriere sont limites"). Utile en particulier pour la
    // molette, dont le comportement PAR DEFAUT est justement D-Pad Haut/Bas
    // pour la navigation Aurora : sans ces entrees, il etait impossible de
    // reassigner explicitement la molette sur D-Pad Gauche/Droite, ni de
    // remettre Haut/Bas apres avoir essaye autre chose (le seul retour en
    // arriere possible etait "Par defaut"). Necessite le correctif du meme
    // jour dans ApplyButtonFieldToXInput (X360Remap/main.cpp), qui ignorait
    // silencieusement toute cible D-Pad.
    { STR_SETTINGS_TARGET_DPAD_UP, "dpad_up" },
    { STR_SETTINGS_TARGET_DPAD_DOWN, "dpad_down" },
    { STR_SETTINGS_TARGET_DPAD_LEFT, "dpad_left" },
    { STR_SETTINGS_TARGET_DPAD_RIGHT, "dpad_right" },
};
static const int kMouseButtonTargetCount = sizeof(kMouseButtonTargets) / sizeof(kMouseButtonTargets[0]);

// Badge affiche par la page glossy pour chaque cible ci-dessus, MEME ORDRE.
// -1 = pas de badge ("Par defaut" : le libelle texte est affiche a la place).
// La ligne typedef suivante refuse de compiler si les deux tableaux n'ont pas
// la meme longueur (cible ajoutee d'un cote seulement).
static const int kMouseButtonTargetBadge[] = {
    -1,
    GA_KP_A_S, GA_KP_B_S, GA_KP_X_S, GA_KP_Y_S,
    GA_KP_LB_S, GA_KP_RB_S, GA_KP_LT_S, GA_KP_RT_S, GA_KP_L3_S, GA_KP_R3_S,
    GA_KP_START_S, GA_KP_BACK_S,
    GA_KP_DUP_S, GA_KP_DDOWN_S, GA_KP_DLEFT_S, GA_KP_DRIGHT_S,
};
typedef char kMouseButtonTargetBadge_meme_longueur[
    (sizeof(kMouseButtonTargetBadge) / sizeof(kMouseButtonTargetBadge[0]) ==
     sizeof(kMouseButtonTargets) / sizeof(kMouseButtonTargets[0])) ? 1 : -1];

static int g_mouseBtnLeftSel = 0;
static int g_mouseBtnRightSel = 0;
static int g_mouseBtnMiddleSel = 0;

// Molette avant/arriere (idx 3/4, convention reservee - voir
// FindWheelOverride dans X360Remap/main.cpp). Par defaut ("Par defaut" =
// index 0, comme les clics ci-dessus) elle reste cablee sur D-Pad Haut/Bas
// pour la navigation Aurora - demande utilisateur du 2026-08-02 de la rendre
// configurable, au prix de perdre ce raccourci une fois assignee.
static int g_mouseWheelForwardSel = 0;
static int g_mouseWheelBackwardSel = 0;

static bool MouseButtonTargetGetter(void*, int idx, const char** out_text) {
    if (idx < 0 || idx >= kMouseButtonTargetCount)
        return false;
    *out_text = Tr(kMouseButtonTargets[idx].labelId);
    return true;
}

static int FindMouseButtonTargetIndex(uint8_t ButtonsReport::* field) {
    const char* name = GetButtonFieldName(field);
    if (!name)
        return 0;
    for (int i = 1; i < kMouseButtonTargetCount; i++) {
        if (strcmp(kMouseButtonTargets[i].fieldName, name) == 0)
            return i;
    }
    return 0; // hors de la liste restreinte - voir le commentaire au-dessus
}

// Ecrit les cinq affectations de boutons souris dans le fichier de config.
//
// EXTRAIT de l'ancien bouton "Enregistrer boutons souris" (2026-08-14) pour
// que la nouvelle page Mapping et l'ancien onglet Reglages appellent
// exactement le MEME code. Dupliquer ces cinq blocs aurait garanti qu'un jour
// l'un des deux oublie la molette - c'est deja arrive une fois avec les idx
// 3/4 (voir PROJECT_NOTES.md, section molette du 2026-08-03).
static bool SaveMouseButtonBindings() {
    std::vector<MouseButtonBinding> bindings;
    const int sel[5] = {
        g_mouseBtnLeftSel, g_mouseBtnRightSel, g_mouseBtnMiddleSel,
        g_mouseWheelForwardSel, g_mouseWheelBackwardSel
    };
    // idx 3 et 4 sont la convention reservee "molette avant / arriere" -
    // voir FindWheelOverride dans X360Remap/main.cpp.
    for (uint8_t i = 0; i < 5; i++) {
        if (sel[i] < 0 || sel[i] >= kMouseButtonTargetCount) continue;
        if (!kMouseButtonTargets[sel[i]].fieldName) continue;   // "Par defaut"
        MouseButtonBinding b;
        b.idx = i;
        b.fieldName = kMouseButtonTargets[sel[i]].fieldName;
        bindings.push_back(b);
    }

    bool ok = ApplyMouseButtonMapping(g_selectedMouseVid, g_selectedMousePid, bindings);
    AppLog(ok ? "Mouse buttons saved (%d custom entries)"
              : "FAILED to save mouse buttons", (int)bindings.size());

    // Relecture immediate : bindings.size() est ce que l'appli a CALCULE avant
    // l'appel, onDiskCount est la verite disque. Les deux ont deja diverge.
    int onDiskCount = GetMouseButtonMapCountOnDisk(g_selectedMouseVid, g_selectedMousePid);
    AppLog("DIAG buttons: vid=%04x pid=%04x requested=%d on disk=%d",
           g_selectedMouseVid, g_selectedMousePid, (int)bindings.size(), onDiskCount);
    return ok;
}

// Resolution MESUREE de la souris selectionnee, en points par cm (0 = jamais
// mesuree) - voir HidDeviceMapping::countsPerCm et l'ecran de mesure
// (DrawMeasureOverlay). Rechargee avec les autres reglages de la souris.
static float g_mouseCountsPerCm = 0.0f;

static void LoadMouseSettingsOnce() {
    if (g_mouseSettingsLoaded)
        return;
    g_mouseSettingsLoaded = true;

    // Une seule fois par lancement : reparation depuis la copie de secours si
    // l'original manque ou est casse (voir RecoverConfigFromTmp).
    static bool s_recoveryChecked = false;
    if (!s_recoveryChecked) {
        s_recoveryChecked = true;
        if (RecoverConfigFromTmp("HDD:\\X360RemapStudio\\X360Remap.json"))
            AppLog("X360Remap.json rebuilt from its backup copy");
    }

    // Reset a "Par defaut" avant de relire le fichier - sans ca, un
    // peripherique qui n'a pas (encore) d'entree pour un idx donne (ou pas
    // de mapping du tout, m == nullptr) HERITAIT silencieusement des valeurs
    // du peripherique precedemment selectionne, encore en memoire dans ces
    // globales statiques. Bug reel trouve le 2026-08-03 en tracant la perte
    // des entrees molette (idx 3/4) : en changeant de souris dans le
    // selecteur (Jalon 8), les combos "Clic gauche/droit/milieu" et "Molette
    // avant/arriere" gardaient l'affichage de l'ANCIENNE souris, et un
    // "Enregistrer boutons souris" declenche dans cet etat ecrivait ces
    // valeurs heritees sur le mauvais peripherique.
    g_mouseBtnLeftSel = 0;
    g_mouseBtnRightSel = 0;
    g_mouseBtnMiddleSel = 0;
    g_mouseWheelForwardSel = 0;
    g_mouseWheelBackwardSel = 0;

    LoadMappingsFromFile("HDD:\\X360RemapStudio\\X360Remap.json");
    HidDeviceMapping* m = FindMapping(g_selectedMouseVid, g_selectedMousePid);
    // Resolution mesuree : remise a zero AVANT lecture, meme raison que les
    // combos ci-dessus (ne pas heriter de la souris precedente).
    g_mouseCountsPerCm = m ? m->countsPerCm : 0.0f;
    g_mouseAntiDeadzone = m ? m->antiDeadzone : 0;   // meme raison
    g_mouseAntiDeadzoneY = m ? m->antiDeadzoneY : 0;
    g_mouseSmoothing = m ? m->smoothingMs : 0;
    if (m) {
        if (m->mouseSensitivity > 0) g_mouseSensitivity = m->mouseSensitivity;
        g_mouseInvertY = m->invertMouseY;
        g_mouseDeadzone = m->deadzone;
        g_mouseAntiDeadzone = m->antiDeadzone;

        for (uint8_t i = 0; i < m->buttonMapCount; i++) {
            const HidButtonMapEntry& e = m->buttonMap[i];
            if (e.idx == 0) g_mouseBtnLeftSel = FindMouseButtonTargetIndex(e.field);
            else if (e.idx == 1) g_mouseBtnRightSel = FindMouseButtonTargetIndex(e.field);
            else if (e.idx == 2) g_mouseBtnMiddleSel = FindMouseButtonTargetIndex(e.field);
            else if (e.idx == 3) g_mouseWheelForwardSel = FindMouseButtonTargetIndex(e.field);
            else if (e.idx == 4) g_mouseWheelBackwardSel = FindMouseButtonTargetIndex(e.field);
        }
    }
}

// --- Profils par jeu (Jalon 7) ---------------------------------------------
// Un profil surcharge sensibilite/deadzone/invertY/courbe pour UN jeu precis
// (identifie par son titleId Xbox), par-dessus les reglages globaux de
// l'onglet "Reglages souris" - voir ResolveEffectiveMouseSettings dans
// X360Remap/mapping.h, cable cote plugin dans X360Remap/main.cpp
// (XInputdReadStateHook). L'appli tourne HORS d'un jeu (lancee depuis
// Aurora, pas depuis l'interieur d'un titre) : contrairement au plugin, elle
// ne peut donc pas lire XamGetCurrentTitleId() pour deviner "le jeu qu'on
// est en train de configurer". Corrige le 2026-08-02 (demande utilisateur -
// "ce n'est pas a l'utilisateur de chercher [le Title ID]") : le plugin
// detecte deja chaque changement de jeu en temps reel et persiste
// desormais chaque nouveau titleId vu dans un petit historique
// (X360Remap/known_titles.h, HDD:\X360RemapStudio\known_titles.txt) - lu ici
// via ListKnownTitleIds() pour proposer un combo "jeux detectes" qui remplit
// le champ automatiquement. La saisie manuelle en hexadecimal reste
// disponible en repli (un jeu jamais lance avec le plugin actif n'apparait
// pas encore dans l'historique). Ecran de selection dynamique du Jalon 8
// (VID/PID) est un besoin different (choisir un PERIPHERIQUE, pas un jeu) -
// pas fusionne ici.
static bool g_profilesLoaded = false;
static std::vector<MouseProfileInfo> g_profiles;

// Historique des jeux vus (voir le commentaire ci-dessus) - charge une fois,
// rafraichi a l'ouverture de l'onglet comme g_profiles (RefreshProfiles).
static bool g_knownTitlesLoaded = false;
static std::vector<uint32_t> g_knownTitleIds;
static int g_knownTitlePickIdx = 0;
// Espace d'index NATIF du combo (0 = "Nouveau profil", 1..N = g_profiles[i-1])
// - reste dans cet espace d'un bout a l'autre, y compris entre les frames.
// Le convertir en -1/i "ailleurs" et le reconvertir a chaque frame (premiere
// version de ce fichier) cassait le contrat d'ImGui::Combo, qui s'attend a
// garder le meme entier tel quel entre deux frames pour rester coherent avec
// son getter - corrige avant meme un premier build, voir la revue de ce
// fichier.
static int g_profileComboIdx = 0;
static char g_profileTitleIdHex[9] = "";
// Nom lisible du jeu (2026-08-02, retour utilisateur reel - voir
// HidKeyCodeToAsciiChar plus bas et MouseProfileInfo::gameName). Saisi de la
// meme facon que g_profileTitleIdHex (une touche HID par frame, pas de
// machinerie clavier ImGui - voir le commentaire au-dessus du champ Title ID
// dans DrawProfilesTab), mais avec l'alphabet complet plutot que l'hexa.
static char g_profileGameName[32] = "";
static int g_profileSensitivity = 10000;
static int g_profileDeadzone = 0;
static int g_profileAntiDeadzone = 0;
static int g_profileStickLayout = 0;   // StickLayout (mapping.h), 2026-09-25
static int g_profileAntiDeadzoneY = 0; // 0 = identique a l'horizontal
static int g_profileSmoothing = 0;
static bool g_profileInvertY = false;
static int g_profileCurveTypeIdx = 0; // 0 = Lineaire, 1 = Exponentielle
static float g_profileCurveExponent = 1.5f;

static void RefreshProfiles() {
    g_profiles = ListMouseProfiles(g_selectedMouseVid, g_selectedMousePid);
}

static void LoadProfilesOnce() {
    if (g_profilesLoaded)
        return;
    g_profilesLoaded = true;
    RefreshProfiles();
}

static void RefreshKnownTitles() {
    g_knownTitleIds = ListKnownTitleIds();
}

static void LoadKnownTitlesOnce() {
    if (g_knownTitlesLoaded)
        return;
    g_knownTitlesLoaded = true;
    RefreshKnownTitles();
}

// Retrouve le gameName deja enregistre pour ce titleId parmi les profils
// existants (g_profiles), ou "" si aucun profil n'a encore ete cree pour ce
// jeu - cas le plus courant ici, cette liste sert justement A CREER le
// premier profil. Simple recherche lineaire (au plus quelques dizaines de
// profils, pas un souci de perf).
static const char* FindKnownGameName(uint32_t titleId) {
    for (size_t i = 0; i < g_profiles.size(); i++) {
        if (g_profiles[i].titleId == titleId)
            return g_profiles[i].gameName;
    }
    return "";
}

static bool KnownTitleComboGetter(void*, int idx, const char** out_text) {
    static char s_label[48]; // un seul combo actif a la fois, meme precaution que ProfileComboGetter
    if (idx < 0 || idx >= (int)g_knownTitleIds.size())
        return false;
    uint32_t titleId = g_knownTitleIds[idx];
    const char* known = FindKnownGameName(titleId);
    if (known[0] != '\0') {
        _snprintf(s_label, sizeof(s_label) - 1, "%s (%08X)", known, (unsigned int)titleId);
    } else {
        _snprintf(s_label, sizeof(s_label) - 1, "%08X", (unsigned int)titleId);
    }
    s_label[sizeof(s_label) - 1] = '\0';
    *out_text = s_label;
    return true;
}

// Remplit les champs d'edition a partir d'un profil existant, en retombant
// sur les reglages globaux (deja charges par LoadMouseSettingsOnce, voir
// g_mouseSensitivity/g_mouseDeadzone/g_mouseInvertY plus haut) pour tout
// champ a sa sentinelle "herite" - c'est ce que ce profil applique REELLEMENT
// tant qu'on ne le modifie pas, pas une valeur arbitraire.
static void LoadProfileIntoEditor(const MouseProfileInfo& p) {
    _snprintf(g_profileTitleIdHex, sizeof(g_profileTitleIdHex) - 1, "%08X", (unsigned int)p.titleId);
    g_profileTitleIdHex[sizeof(g_profileTitleIdHex) - 1] = '\0';

    _snprintf(g_profileGameName, sizeof(g_profileGameName) - 1, "%s", p.gameName);
    g_profileGameName[sizeof(g_profileGameName) - 1] = '\0';

    g_profileSensitivity = (p.mouseSensitivity != 0) ? p.mouseSensitivity : g_mouseSensitivity;
    g_profileInvertY = (p.invertMouseY != -1) ? (p.invertMouseY != 0) : g_mouseInvertY;
    g_profileDeadzone = (p.deadzone != -1) ? p.deadzone : g_mouseDeadzone;
    g_profileAntiDeadzone = (p.antiDeadzone >= 0) ? p.antiDeadzone : g_mouseAntiDeadzone;
    g_profileStickLayout = (p.stickLayout > 0 && p.stickLayout < STICKS_LAYOUT_COUNT) ? p.stickLayout : 0;
    g_profileAntiDeadzoneY = (p.antiDeadzoneY >= 0) ? p.antiDeadzoneY : g_mouseAntiDeadzoneY;
    g_profileSmoothing = (p.smoothingMs >= 0) ? p.smoothingMs : g_mouseSmoothing;
    if (p.curveType == CURVE_EXPONENTIAL) {
        g_profileCurveTypeIdx = 1;
        g_profileCurveExponent = p.curveExponent;
    } else {
        g_profileCurveTypeIdx = 0;
    }
}

static void ResetProfileEditorForNew() {
    g_profileTitleIdHex[0] = '\0';
    g_profileGameName[0] = '\0';
    g_profileSensitivity = g_mouseSensitivity;
    g_profileInvertY = g_mouseInvertY;
    g_profileDeadzone = g_mouseDeadzone;
    g_profileAntiDeadzone = g_mouseAntiDeadzone;
    g_profileStickLayout = 0;
    g_profileAntiDeadzoneY = g_mouseAntiDeadzoneY;
    g_profileSmoothing = g_mouseSmoothing;
    g_profileCurveTypeIdx = 0;
    g_profileCurveExponent = 1.5f;
}

// Page HID Keyboard/Keypad (0x07) - memes codes que X360Remap/mapping.h et
// le tableau du README. '\0' si le code ne correspond a aucun chiffre
// hexadecimal (touche ignoree pour ce champ).
static char HidKeyCodeToHexChar(int32_t code) {
    if (code >= 0x1E && code <= 0x26) return (char)('1' + (code - 0x1E)); // Keyboard 1..9
    if (code == 0x27) return '0';                                        // Keyboard 0
    if (code >= 0x04 && code <= 0x09) return (char)('A' + (code - 0x04)); // Keyboard a..f
    return '\0';
}

// Meme page HID que HidKeyCodeToHexChar ci-dessus, mais alphabet complet
// (lettres a..z + chiffres + espace) pour le champ "Nom du jeu" (2026-08-02,
// retour utilisateur reel : "comment comprendre que c'est de ce jeu la si
// c'est pas ecrit Tomb Raider"). Minuscules uniquement : le snapshot
// input_state.json ne transporte qu'UN SEUL code de touche par tick, pas
// l'etat des touches modificatrices (Shift) - voir InputStateSnapshot dans
// input_state_reader.h, aucun champ pour ca. Suffisant pour reconnaitre un
// nom de jeu au premier coup d'oeil, largement mieux que le Title ID en hex
// seul, sans avoir a cabler tout io.KeyMap/AddInputCharacter d'ImGui pour un
// seul champ (meme choix que HidKeyCodeToHexChar juste au-dessus).
static char HidKeyCodeToAsciiChar(int32_t code) {
    if (code >= 0x04 && code <= 0x1D) return (char)('a' + (code - 0x04)); // Keyboard a..z
    if (code >= 0x1E && code <= 0x26) return (char)('1' + (code - 0x1E)); // Keyboard 1..9
    if (code == 0x27) return '0';                                        // Keyboard 0
    if (code == 0x2C) return ' ';                                        // Barre d'espace
    return '\0';
}

static bool ProfileComboGetter(void*, int idx, const char** out_text) {
    // 32 (gameName) + " (" + 8 (hex) + ")" + marge, ou juste le hex seul si
    // gameName est vide (2026-08-02 - avant ce champ, seul le Title ID en hex
    // etait affiche ici, illisible pour un humain, voir le commentaire de
    // g_profileGameName plus haut).
    static char s_label[48]; // un seul combo actif a la fois, pas de risque de reecriture concurrente
    if (idx == 0) {
        *out_text = Tr(STR_PROFILES_COMBO_NEW);
        return true;
    }
    int profileIdx = idx - 1;
    if (profileIdx < 0 || profileIdx >= (int)g_profiles.size())
        return false;
    const MouseProfileInfo& p = g_profiles[profileIdx];
    if (p.gameName[0] != '\0') {
        _snprintf(s_label, sizeof(s_label) - 1, "%s (%08X)", p.gameName, (unsigned int)p.titleId);
    } else {
        _snprintf(s_label, sizeof(s_label) - 1, "%08X", (unsigned int)p.titleId);
    }
    s_label[sizeof(s_label) - 1] = '\0';
    *out_text = s_label;
    return true;
}

// --- Assistant clavier en direct (2026-08-17) -------------------------------
// Remplace l'ancienne DrawWizardTab(), qui dessinait le parcours en
// SUPERPOSITION PLEIN ECRAN (voir l'ancienne note sur overlayActive plus
// bas). HB a fait remarquer que masquer la grille des 21 boutons pendant le
// wizard n'avait pas de sens : "pourquoi on a visuellement 21 touches de la
// manette si on peut pas les voir s'activer pour les choix du wizard."
//
// Cette fonction ne dessine plus RIEN : elle fait avancer g_wizard (Tick) et
// remplit les champs wizard* du modele partage (ui_mapping.h). C'est
// DrawUiMapping (application/ui_mapping.cpp, identique a la previsualisation)
// qui affiche desormais le parcours SUR la page Mapping, avec la case de la
// grille correspondant a l'etape courante surlignee en direct. Logique de
// session deja testee par ailleurs (application/tests/test_wizard_session.cpp)
// - aucune decision n'est prise ici, seulement de la lecture + mise en forme.
static void UpdateWizardModel(UiMappingModel& m) {
    if (!g_wizard) {
        m.wizardActive = false;
        return;
    }

    const InputStateSnapshot& snap = g_lastSnapshot;
    int32_t code = snap.valid ? snap.keyCode : MappingAssistant::NO_CODE;

    // Annulation sur ECHAP physique, pas BACK (2026-08-17, bug reel signale
    // par HB) : BACK annulait via UpdateGamepadNavigation() en lisant l'etat
    // XInput REEL (XexUtils::Input::GetInput(0)), c'est-a-dire APRES
    // traduction par le mapping ACTUELLEMENT ACTIF (celui d'avant cette
    // session du wizard, pas celui en cours de construction). Consequence :
    // appuyer sur la touche deja assignee a Back/Guide dans l'ancien mapping
    // annulait le wizard EN PLUS d'etre candidate pour la nouvelle etape - un
    // conflit d'autant plus probable que Back/Guide font partie des 21
    // boutons qu'on est justement en train de configurer. ECHAP (code HID 41)
    // vient du meme canal brut que les etapes du wizard (input_state.json),
    // independant de tout mapping actif, et n'est deja assigne a rien par
    // defaut (voir README.md, tableau de mapping clavier).
    if (code == 41 /* Echap, USB HID Usage ID 0x29 */) {
        g_wizard->Cancel();
    }

    std::string prompt, message;
    g_wizard->Tick(code, &prompt, &message);

    m.wizardActive       = true;
    m.wizardStepIndex    = (int)g_wizard->CurrentStepIndex();
    m.wizardStepTotal    = (int)g_wizard->TotalSteps();
    m.wizardResultsCount = (int)g_wizard->ResultsCount();
    m.wizardNoKeyboard   = !snap.valid;
    m.wizardComplete     = false;
    m.wizardCancelled    = false;

    // Tampons statiques : le modele ne garde que des pointeurs (voir
    // ui_mapping.h), ils doivent survivre jusqu'au DrawUiMapping() de cette
    // meme frame - largement suffisant ici puisqu'on est appele juste avant
    // depuis DrawMappingTab(). STR_WIZARD_PRESS_KEY_FOR_FMT est un format
    // printf (%s) alors que le modele attend une chaine deja formee (voir
    // le commentaire "doit rester portable" dans ui_mapping.h) : on la
    // formate ici, cote console/previsualisation, pas dans l'ecran partage.
    static char s_promptBuf[128];
    static char s_messageBuf[192];
    if (!prompt.empty()) {
        _snprintf(s_promptBuf, sizeof(s_promptBuf) - 1, Tr(STR_WIZARD_PRESS_KEY_FOR_FMT), prompt.c_str());
        s_promptBuf[sizeof(s_promptBuf) - 1] = '\0';
        m.wizardPrompt = s_promptBuf;
    } else {
        m.wizardPrompt = 0;
    }
    if (!message.empty()) {
        _snprintf(s_messageBuf, sizeof(s_messageBuf) - 1, "%s", message.c_str());
        s_messageBuf[sizeof(s_messageBuf) - 1] = '\0';
        m.wizardMessage = s_messageBuf;
    } else {
        m.wizardMessage = 0;
    }

    if (g_wizard->IsComplete()) {
        m.wizardComplete = true;
        // Reset au clic "OK" (requestWizardClose, traite dans
        // DrawMappingTab) plutot qu'ici - pret pour le prochain wizard.
        if (!g_wizardSaveAttempted) {
            g_wizardSaveAttempted = true;
            g_wizardSaveOk = g_wizard->SaveResult();
            size_t count = g_wizard->ResultsCount();
            AppLog(g_wizardSaveOk ? "Keyboard wizard: %d keys saved" : "Keyboard wizard: FAILED to save (%d keys resolved)",
                (int)count);
            XexUtils::Xam::XNotify(g_wizardSaveOk ? "Configuration clavier enregistree" : "ECHEC sauvegarde configuration clavier");
        }
        m.wizardSaveOk       = g_wizardSaveOk;
        m.wizardResultsCount = (int)g_wizard->ResultsCount();
    } else if (g_wizard->WasCancelled()) {
        m.wizardCancelled = true;
    }
}

// --- Reglages manuels (2026-08-17) ------------------------------------------
// Meme principe qu'UpdateWizardModel ci-dessus (fait avancer une session,
// remplit le modele, aucun dessin ici), pour le mode "un clic, une touche"
// demande par HB : "a chaque click sur un button l'utilisateur choisi une
// touche clavier". g_manualSession est une WizardSession d'UN SEUL pas
// (DefaultKeyboardWizardSteps()[index] uniquement) plutot qu'un mecanisme de
// capture ecrit a part - meme machine a etats deja testee
// (X360Remap/tests/test_mapping_assistant.cpp), meme gestion ECHAP/hold-to-
// skip que le wizard sequentiel. La seule vraie difference est la sauvegarde
// (voir plus bas) : ApplyKeyboardMappingEntry FUSIONNE cette unique entree
// dans ce qui existe deja sur le disque, alors que WizardSession::SaveResult
// (utilise par le wizard sequentiel) remplace tout le keyMap - normal pour
// lui puisqu'il reconstruit les 21 boutons a chaque session, mais destructeur
// ici ou un seul bouton doit changer.
static void UpdateManualAssignModel(UiMappingModel& m) {
    m.manualMode = g_manualMode;

    if (!g_manualSession) {
        m.manualCaptureIndex = -1;
        m.manualPrompt  = 0;
        m.manualMessage = 0;
        m.manualNoKeyboard = false;
        return;
    }

    const InputStateSnapshot& snap = g_lastSnapshot;
    int32_t code = snap.valid ? snap.keyCode : MappingAssistant::NO_CODE;

    // Meme raison d'etre que dans UpdateWizardModel : ECHAP physique, pas
    // BACK, pour ne pas annuler la capture en appuyant sur la touche deja
    // assignee a Back/Guide.
    if (code == 41 /* Echap, USB HID Usage ID 0x29 */) {
        g_manualSession->Cancel();
    }

    std::string prompt, message;
    g_manualSession->Tick(code, &prompt, &message);

    m.manualCaptureIndex = g_manualCaptureIndex;
    m.manualNoKeyboard   = !snap.valid;

    static char s_mPromptBuf[128];
    static char s_mMessageBuf[192];
    if (!prompt.empty()) {
        _snprintf(s_mPromptBuf, sizeof(s_mPromptBuf) - 1, Tr(STR_WIZARD_PRESS_KEY_FOR_FMT), prompt.c_str());
        s_mPromptBuf[sizeof(s_mPromptBuf) - 1] = '\0';
        m.manualPrompt = s_mPromptBuf;
    } else {
        m.manualPrompt = 0;
    }
    if (!message.empty()) {
        _snprintf(s_mMessageBuf, sizeof(s_mMessageBuf) - 1, "%s", message.c_str());
        s_mMessageBuf[sizeof(s_mMessageBuf) - 1] = '\0';
        m.manualMessage = s_mMessageBuf;
    } else {
        m.manualMessage = 0;
    }

    if (g_manualSession->IsComplete()) {
        // Pas de bouton "OK" ici (a la difference du wizard sequentiel) :
        // une seule touche vient d'etre capturee, la case l'affiche
        // immediatement (BuildGridEntries relit le fichier a chaque frame -
        // voir son commentaire "Relecture a chaque frame"). La confirmation
        // passe par XNotify, comme tous les autres enregistrements de cet
        // ecran (reglages souris, boutons souris...), pas par un ecran a
        // fermer explicitement - reglages manuels doit rester rapide.
        const std::vector<AssistantResult>& results = g_manualSession->Results();
        bool ok = false;
        if (!results.empty()) {
            HidKeyMapEntry entry;
            entry.code   = static_cast<uint8_t>(results[0].code);
            entry.action = results[0].action;
            ok = ApplyKeyboardMappingEntry(g_selectedKeyboardVid, g_selectedKeyboardPid, entry);
            AppLog(ok ? "Manual binding: key assigned (action=%d)" : "FAILED manual binding (action=%d)",
                   (int)entry.action);
        }
        XexUtils::Xam::XNotify(ok ? "Touche assignee" : "ECHEC assignation touche");
        delete g_manualSession;
        g_manualSession = nullptr;
        g_manualCaptureIndex = -1;
        m.manualCaptureIndex = -1;
        m.manualPrompt  = 0;
        m.manualMessage = 0;
    } else if (g_manualSession->WasCancelled()) {
        delete g_manualSession;
        g_manualSession = nullptr;
        g_manualCaptureIndex = -1;
        m.manualCaptureIndex = -1;
        m.manualPrompt  = 0;
        m.manualMessage = 0;
    }
}

// --- Ecran assistant de calibration souris (2026-08-03) --------------------
// Idee de l'utilisateur : plutot que de deviner la disposition du paquet
// d'une souris (impossible depuis le VID/PID) ou de lire son descripteur HID
// (possible mais via la chaine USB asynchrone responsable de tous les gels
// historiques), on la fait DECOUVRIR par l'utilisateur - exactement le
// principe du wizard clavier.
//
// Toute la logique vit dans CalibrationSession (testee en g++). Cet ecran ne
// fait que l'alimenter en octets bruts et dessiner son etat.
//
// PILOTE A LA MANETTE, et c'est structurel : pendant la calibration la souris
// n'est justement PAS encore calibree - sur le modele qu'on cherche a
// reparer, son curseur est inutilisable. Naviguer l'assistant a la souris
// serait donc impossible precisement dans le cas ou il sert. La souris ne
// fait que produire les gestes demandes.
static bool g_calibSaveAttempted = false;
static bool g_calibSaveOk = false;

static void DrawCalibrationTab() {
    ImGui::TextWrapped(Tr(STR_CALIB_INTRO));
    ImGui::TextDisabled(Tr(STR_CALIB_NAV_HINT));
    ImGui::Separator();

    if (!g_calib) {
        ImGui::Text(Tr(STR_CALIB_TARGET_MOUSE_FMT), g_selectedMouseVid, g_selectedMousePid);
        ImGui::TextDisabled(Tr(STR_CALIB_TARGET_MOUSE_HINT));
        ImGui::Spacing();
        if (ImGui::Button(Tr(STR_CALIB_BTN_START))) {
            g_calib = new CalibrationSession(g_selectedMouseVid, g_selectedMousePid);
            g_calibSaveAttempted = false;
            g_calibSaveOk = false;
            AppLog("Mouse calibration started (%04X:%04X)", g_selectedMouseVid, g_selectedMousePid);
        }
        ImGui::Spacing();
        ImGui::Separator();
        // Le cercle vicieux explique dans ShouldForceBootProtocol
        // (X360Remap/main.cpp) : une souris a paquet long est forcee en mode
        // boot tant qu'elle n'est pas calibree, or ce mode SUPPRIME la
        // molette. L'assistant ne verrait donc jamais de molette a detecter.
        // Il faut observer la souris dans son format natif au moins le temps
        // d'une calibration - d'ou ce bouton, qui evite a l'utilisateur de
        // creer le fichier a la main via XeCLI.
        // Depuis le 2026-08-03, le plugin lit le DESCRIPTEUR de chaque souris
        // et n'a plus besoin de calibration dans la quasi-totalite des cas.
        // Ce mode legacy ne sert plus que de filet : un modele qui se
        // comporterait mal avec son propre descripteur peut etre ramene a
        // l'ancienne lecture figee.
        // ETAT AFFICHE EN PERMANENCE (2026-08-04). Sans ca, ces deux boutons
        // sont un piege : l'utilisateur a active le mode de secours, l'a
        // oublie, et a passe une session entiere a chercher pourquoi sa
        // molette ne marchait plus - alors que l'interface connaissait la
        // reponse et ne la montrait pas. Un reglage qui modifie le
        // comportement DOIT afficher son etat courant.
        {
            std::ifstream fbp("HDD:\\X360RemapStudio\\force_boot_protocol.txt", std::ios::binary);
            bool secoursActif = false;
            if (fbp.is_open()) {
                char c = 0;
                fbp.get(c);
                secoursActif = (c == '1');
            }
            if (secoursActif) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                    Tr(STR_CALIB_FALLBACK_ACTIVE));
                ImGui::TextWrapped(Tr(STR_CALIB_FALLBACK_ACTIVE_HINT));
            } else {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                    Tr(STR_CALIB_AUTO_ACTIVE));
            }
        }

        ImGui::TextWrapped(Tr(STR_CALIB_FALLBACK_EXPLAIN));
        if (ImGui::Button(Tr(STR_CALIB_BTN_FORCE_FALLBACK))) {
            std::ofstream f("HDD:\\X360RemapStudio\\force_boot_protocol.txt",
                std::ios::binary | std::ios::trunc);
            bool ok = f.is_open();
            if (ok) f << "1";
            AppLog(ok ? "Fallback mode on - reboot required"
                      : "FAILED to write force_boot_protocol.txt");
            XexUtils::Xam::XNotify(ok ? "Mode de secours - redemarre la console"
                                      : "ECHEC - impossible d'ecrire le fichier");
        }
        if (ImGui::Button(Tr(STR_CALIB_BTN_RESTORE_AUTO))) {
            bool ok = (std::remove("HDD:\\X360RemapStudio\\force_boot_protocol.txt") == 0);
            AppLog(ok ? "Automatic reading restored (file deleted)"
                      : "Already in automatic reading");
            XexUtils::Xam::XNotify(ok ? "Automatique - redemarre la console"
                                      : "Deja en mode automatique");
        }

        ImGui::Spacing();
        ImGui::TextDisabled(Tr(STR_CALIB_CLEAR_HINT));
        if (ImGui::Button(Tr(STR_CALIB_BTN_CLEAR))) {
            bool ok = ClearMouseReportLayout(g_selectedMouseVid, g_selectedMousePid);
            AppLog(ok ? "Calibration cleared (%04X:%04X)" : "No calibration to clear (%04X:%04X)",
                g_selectedMouseVid, g_selectedMousePid);
            XexUtils::Xam::XNotify(ok ? "Calibration effacee" : "Aucune calibration enregistree");
        }
        return;
    }

    // Alimentation : les octets bruts viennent du meme snapshot deja lu une
    // fois par frame dans main(), pas d'une lecture disque supplementaire.
    const InputStateSnapshot& snap = g_lastSnapshot;
    if (snap.valid && snap.rawLen > 0)
        g_calib->Feed(snap.raw, snap.rawLen, snap.tick, snap.rawSeq, snap.hasRawSeq);

    if (!snap.valid || snap.rawLen == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            Tr(STR_CALIB_NO_DATA));
    }

    if (g_calib->Finished()) {
        if (g_calib->Succeeded()) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), Tr(STR_CALIB_SUCCESS));
            ImGui::TextWrapped("%s", g_calib->Summary().c_str());
            ImGui::Spacing();

            if (!g_calibSaveAttempted) {
                if (ImGui::Button(Tr(STR_CALIB_BTN_SAVE))) {
                    g_calibSaveAttempted = true;
                    g_calibSaveOk = ApplyMouseReportLayout(
                        g_calib->Vid(), g_calib->Pid(), g_calib->Layout());
                    AppLog(g_calibSaveOk ? "Calibration saved (%04X:%04X)"
                                         : "FAILED to save calibration (%04X:%04X)",
                        g_calib->Vid(), g_calib->Pid());
                    XexUtils::Xam::XNotify(g_calibSaveOk ? "Calibration enregistree"
                                                         : "ECHEC sauvegarde calibration");
                }
            } else {
                ImGui::TextColored(
                    g_calibSaveOk ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    g_calibSaveOk ? Tr(STR_CALIB_SAVED_APPLIED)
                                  : Tr(STR_CALIB_SAVE_FAILED));
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", g_calib->Prompt());
            ImGui::TextWrapped("%s", g_calib->Summary().c_str());
        }

        ImGui::Spacing();
        if (ImGui::Button(Tr(STR_CALIB_BTN_CLOSE))) {
            delete g_calib;
            g_calib = nullptr;
            g_calibSaveAttempted = false;
            g_calibSaveOk = false;
        }
        return;
    }

    // Etape en cours : consigne + jauge de progression.
    ImGui::Spacing();
    ImGui::TextWrapped("%s", g_calib->Prompt());
    ImGui::Spacing();

    // La jauge est le coeur de l'ergonomie ici : elle ne se remplit que si le
    // paquet change REELLEMENT par rapport au repos. Un utilisateur qui ne
    // fait pas le geste voit une jauge vide et comprend tout de suite, au lieu
    // d'enchainer les etapes et d'enregistrer une disposition fausse.
    float p = g_calib->Progress();
    ImGui::ProgressBar(p, ImVec2(-1.0f, 0.0f));
    ImGui::TextDisabled(p >= 1.0f ? Tr(STR_CALIB_GESTURE_DONE)
                                  : Tr(STR_CALIB_GESTURE_CONTINUE));

    ImGui::Spacing();
    if (p >= 1.0f) {
        if (ImGui::Button(Tr(STR_CALIB_BTN_VALIDATE_STEP)))
            g_calib->Advance();
    } else {
        ImGui::TextDisabled(Tr(STR_CALIB_VALIDATE_DISABLED_HINT));
    }

    if (g_calib->CanSkip()) {
        ImGui::SameLine();
        if (ImGui::Button(Tr(STR_CALIB_BTN_NO_WHEEL)))
            g_calib->SkipWheel();
    }

    ImGui::Spacing();
    if (ImGui::Button(Tr(STR_CALIB_BTN_CANCEL))) {
        g_calib->Cancel();
        AppLog("Mouse calibration canceled");
    }
}

// --- Ecran reglages souris -------------------------------------------------
// Exactement le cas d'usage cite par l'utilisateur pour ImGui : glissieres
// pour la sensibilite plutot que de coder des boutons +/- a la main.
static void DrawMouseSettingsTab() {
    LoadMouseSettingsOnce();

    // Le selecteur de langue etait ici jusqu'au 2026-08-14. Il vit desormais
    // dans DrawDeviceSelector(), a cote du choix des peripheriques : la langue
    // concerne TOUTE l'application, et enfouie dans les reglages de la souris
    // elle restait invisible pour qui n'ouvrait jamais cet onglet.

    ImGui::SliderInt(Tr(STR_SETTINGS_SENSITIVITY_LABEL), &g_mouseSensitivity, 1000, 30000);
    ImGui::SliderInt(Tr(STR_SETTINGS_DEADZONE_LABEL), &g_mouseDeadzone, 0, 10000);
    ImGui::Checkbox(Tr(STR_SETTINGS_INVERT_Y_LABEL), &g_mouseInvertY);

    ImGui::Separator();
    if (ImGui::Button(Tr(STR_SETTINGS_BTN_SAVE))) {
        bool ok = ApplyMouseSettings(g_selectedMouseVid, g_selectedMousePid, g_mouseSensitivity, g_mouseInvertY, g_mouseDeadzone);
        AppLog(ok ? "Mouse settings saved (sensitivity=%d deadzone=%d invertY=%d)" : "FAILED to save mouse settings",
            g_mouseSensitivity, g_mouseDeadzone, g_mouseInvertY ? 1 : 0);

        // DIAG (2026-08-03) - ce bouton ne devrait JAMAIS toucher buttonMap,
        // seulement sensibilite/deadzone/invertY. On verifie ici que le
        // buttonMapCount du device n'a pas change pour confirmer/infirmer
        // qu'un save "settings" ecrase par erreur les boutons/molette d'un
        // save precedent - voir PROJECT_NOTES.md, section molette 2026-08-03.
        int onDiskCount = GetMouseButtonMapCountOnDisk(g_selectedMouseVid, g_selectedMousePid);
        AppLog("DIAG settings: vid=%04x pid=%04x buttonMapCount_on_disk=%d",
            g_selectedMouseVid, g_selectedMousePid, onDiskCount);

        XexUtils::Xam::XNotify(ok ? "Reglages souris enregistres" : "ECHEC sauvegarde reglages souris");
    }

    // Mapping des clics souris - demande utilisateur du 2026-08-02
    // ("pourquoi y'a pas un mapping pour les buttons souris"). Voir le
    // commentaire au-dessus de kMouseButtonTargets pour la portee/limites.
    ImGui::Separator();
    ImGui::TextUnformatted(Tr(STR_SETTINGS_MOUSE_BUTTONS_HEADER));
    ImGui::Combo(Tr(STR_SETTINGS_CLICK_LEFT_LABEL), &g_mouseBtnLeftSel, MouseButtonTargetGetter, nullptr, kMouseButtonTargetCount);
    ImGui::Combo(Tr(STR_SETTINGS_CLICK_RIGHT_LABEL), &g_mouseBtnRightSel, MouseButtonTargetGetter, nullptr, kMouseButtonTargetCount);
    ImGui::Combo(Tr(STR_SETTINGS_CLICK_MIDDLE_LABEL), &g_mouseBtnMiddleSel, MouseButtonTargetGetter, nullptr, kMouseButtonTargetCount);
    ImGui::TextDisabled(Tr(STR_SETTINGS_CLICK_DEFAULT_HINT));

    ImGui::Spacing();
    ImGui::Combo(Tr(STR_SETTINGS_WHEEL_FORWARD_LABEL), &g_mouseWheelForwardSel, MouseButtonTargetGetter, nullptr, kMouseButtonTargetCount);
    ImGui::Combo(Tr(STR_SETTINGS_WHEEL_BACKWARD_LABEL), &g_mouseWheelBackwardSel, MouseButtonTargetGetter, nullptr, kMouseButtonTargetCount);
    ImGui::TextDisabled(Tr(STR_SETTINGS_WHEEL_DEFAULT_HINT));

    // Meme code que la nouvelle page Mapping : une seule fonction ecrit ces
    // cinq entrees, il ne peut plus y avoir deux comportements.
    if (ImGui::Button(Tr(STR_SETTINGS_BTN_SAVE_BUTTONS))) {
        bool ok = SaveMouseButtonBindings();
        XexUtils::Xam::XNotify(ok ? "Boutons souris enregistres" : "ECHEC sauvegarde boutons souris");
    }

    // Reglages par defaut ("comme un BIOS") - demande utilisateur du
    // 2026-08-02 apres avoir cru perdre son mapping clavier en testant le
    // wizard. Porte sur TOUT X360Remap.json (clavier + souris), pas juste
    // cet onglet - placee ici car c'est le seul endroit avec un bouton
    // "Enregistrer" existant, mais le libelle le precise explicitement.
    ImGui::Separator();
    ImGui::TextUnformatted(Tr(STR_SETTINGS_DEFAULTS_HEADER));
    if (ImGui::Button(Tr(STR_SETTINGS_BTN_SAVE_AS_DEFAULT))) {
        bool ok = SaveConfigAsDefault();
        AppLog(ok ? "Current settings saved as default" : "FAILED to save default settings");
        XexUtils::Xam::XNotify(ok ? "Current settings saved as default" : "Echec sauvegarde des reglages par defaut");
    }
    if (!HasConfigDefault()) {
        ImGui::TextUnformatted(Tr(STR_SETTINGS_NO_DEFAULT_SAVED));
    } else if (ImGui::Button(Tr(STR_SETTINGS_BTN_RESTORE_DEFAULT))) {
        bool ok = RestoreConfigDefault();
        if (ok) {
            g_mouseSettingsLoaded = false; // force le rechargement des sliders/combos
        }
        AppLog(ok ? "Default settings restored" : "FAILED to restore default settings");
        XexUtils::Xam::XNotify(ok ? "Default settings restored" : "Echec restauration des reglages par defaut");
    }
}

// --- Ecran profils par jeu (Jalon 7) ---------------------------------------
// Voir le commentaire au-dessus de g_profilesLoaded pour la limite connue
// (titleId saisi a la main, pas de detection automatique du jeu en cours
// depuis cet ecran).
static void DrawProfilesTab() {
    LoadMouseSettingsOnce(); // pour les valeurs "heritees" affichees ci-dessous
    LoadProfilesOnce();

    // Densite reduite (2026-08-17, retour HB : la page debordait, obligeait
    // a defiler pour tout voir). Cette page empile ~15 widgets ImGui par
    // defaut (Combo, 2 InputText, 2 Slider, Checkbox, Combo, boutons...) SANS
    // jamais avoir recu le traitement "police compacte" que le reste de
    // l'appli utilise deja (voir g_fontSmall/PushX360Font, utilise partout
    // dans ui_mapping.cpp et par DrawDeviceSelector plus haut) - chaque ligne
    // heritait donc de g_fontBody (24px, la taille "texte courant" choisie
    // pour rester lisible a 2-3m d'une TV) avec le FramePadding/ItemSpacing
    // globaux (12x8 / 12x12, voir ApplyX360RemapTheme), soit largement plus
    // que la hauteur disponible du panneau. Police plus petite (g_fontSmall,
    // 0.72x) + marges resserrees, SCOPEES A CETTE PAGE (Pop en bas, le reste
    // de l'appli n'est pas concerne) : la page reste lisible, juste moins
    // aeree qu'un panneau qui n'a que quelques lignes a montrer.
    PushX360Font(g_fontSmall);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 7.0f));

    ImGui::TextWrapped(Tr(STR_PROFILES_INTRO));
    ImGui::TextDisabled(Tr(STR_PROFILES_F9_HINT));
    ImGui::Separator();

    int comboCount = (int)g_profiles.size() + 1; // +1 pour "Nouveau profil" (index 0)
    // Garde-fou si g_profiles a retreci depuis la derniere frame (ex: profil
    // supprime ailleurs) - eviter de laisser le combo pointer hors plage,
    // ce qu'ImGui ne clamp pas tout seul.
    if (g_profileComboIdx >= comboCount)
        g_profileComboIdx = 0;

    if (ImGui::Combo(Tr(STR_PROFILES_COMBO_LABEL), &g_profileComboIdx, ProfileComboGetter, nullptr, comboCount)) {
        if (g_profileComboIdx == 0) {
            ResetProfileEditorForNew();
        } else {
            LoadProfileIntoEditor(g_profiles[g_profileComboIdx - 1]);
        }
    }

    bool isNewProfile = (g_profileComboIdx == 0);

    if (isNewProfile) {
        // BUG REEL signale par l'utilisateur (2026-08-02) : ImGui::InputText
        // ne recevait AUCUN caractere - cette appli n'a jamais cable le
        // clavier physique vers la machinerie texte d'ImGui
        // (io.AddInputCharacter/io.KeyMap, jamais initialises, voir
        // UpdateGamepadNavigation plus bas qui ne gere QUE la manette). Le
        // seul canal clavier existant est le snapshot MONO-TOUCHE de
        // input_state.json (deja utilise par le wizard, deja confirme
        // fonctionnel sur hardware) - reutilise directement ici plutot que
        // de cabler toute la machinerie clavier d'ImGui pour un seul champ :
        // une touche HID par frame, detection de front montant (une pression
        // = un caractere, pas de repetition tant que la touche reste
        // enfoncee) - meme principe que MappingAssistant, en plus simple.

        // Combo "Games detected by the plugin" RETIRE (2026-08-08, decision
        // utilisateur : "ça sert a rien il faut le supprimer si il detect 4
        // sur 60 jeux par exemple"). Le principe (historique des jeux vus
        // par le plugin, X360Remap/known_titles.h) ne couvre que les jeux
        // deja lances avec le plugin actif depuis l'installation - une
        // bibliotheque de 60 jeux n'en montre que quelques-uns tant qu'ils
        // n'ont pas tous ete lances au moins une fois, ce qui le rend peu
        // utile en pratique comme point d'entree principal. La saisie
        // manuelle du Title ID (hex, ci-dessous) reste le seul chemin.
        // LoadKnownTitlesOnce/RefreshKnownTitles/g_knownTitleIds/
        // g_knownTitlePickIdx/KnownTitleComboGetter restent dans le fichier
        // (inoffensifs, plus appeles) - a retirer une prochaine fois si
        // confirme definitivement inutile plutot que de tout retoucher dans
        // la meme session qu'un correctif calibration.

        // Le snapshot input_state.json ne porte qu'UNE touche par tick (voir
        // le commentaire au-dessus) - impossible de remplir le Title ID et le
        // nom du jeu en meme temps sans savoir lequel doit recevoir la
        // frappe. Bascule explicite via deux boutons radio, cliquables a la
        // souris (le curseur souris est deja cable, voir UpdateMouseCursor) -
        // ajoute le 2026-08-02 en meme temps que le champ "Nom du jeu"
        // (retour utilisateur reel : Title ID en hex seul illisible pour un
        // humain).
        static int s_activeProfileField = 0; // 0 = Title ID, 1 = Nom du jeu
        if (ImGui::RadioButton(Tr(STR_PROFILES_RADIO_TYPE_TITLEID), s_activeProfileField == 0)) s_activeProfileField = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton(Tr(STR_PROFILES_RADIO_TYPE_GAMENAME), s_activeProfileField == 1)) s_activeProfileField = 1;

        static int32_t s_lastProfileKeyCode = -1;
        int32_t code = g_lastSnapshot.valid ? g_lastSnapshot.keyCode : -1;
        if (code != s_lastProfileKeyCode && code != -1) {
            if (s_activeProfileField == 0) {
                size_t len = strlen(g_profileTitleIdHex);
                char hexChar = HidKeyCodeToHexChar(code);
                if (hexChar != '\0' && len < 8) {
                    g_profileTitleIdHex[len] = hexChar;
                    g_profileTitleIdHex[len + 1] = '\0';
                } else if (code == 42 /* Retour arriere, meme code HID que documente dans README.md */ && len > 0) {
                    g_profileTitleIdHex[len - 1] = '\0';
                }
            } else {
                size_t len = strlen(g_profileGameName);
                char asciiChar = HidKeyCodeToAsciiChar(code);
                if (asciiChar != '\0' && len < sizeof(g_profileGameName) - 1) {
                    g_profileGameName[len] = asciiChar;
                    g_profileGameName[len + 1] = '\0';
                } else if (code == 42 && len > 0) {
                    g_profileGameName[len - 1] = '\0';
                }
            }
        }
        s_lastProfileKeyCode = code;

        // ReadOnly : le contenu est entierement pilote par le bloc ci-dessus,
        // pas par la saisie native d'ImGui (jamais cablee, voir plus haut) -
        // afficher quand meme via InputText (pas juste ImGui::Text) pour
        // garder l'apparence d'un champ de saisie a l'ecran.
        ImGui::InputText(Tr(STR_PROFILES_INPUT_TITLEID), g_profileTitleIdHex, sizeof(g_profileTitleIdHex),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::TextDisabled(Tr(STR_PROFILES_HINT_TITLEID_CHARS));
        ImGui::InputText(Tr(STR_PROFILES_INPUT_GAME_NAME), g_profileGameName, sizeof(g_profileGameName),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::TextDisabled(Tr(STR_PROFILES_HINT_GAME_NAME_CHARS));
    } else {
        ImGui::Text(Tr(STR_PROFILES_TITLEID_DISPLAY_FMT), g_profileTitleIdHex);

        // Un profil existant a deja son Title ID (non modifiable, c'est la
        // cle) - seul le nom peut etre tape/edite ici, pas de bascule
        // necessaire contrairement au bloc "Nouveau profil" ci-dessus.
        static int32_t s_lastExistingGameNameKeyCode = -1;
        int32_t code = g_lastSnapshot.valid ? g_lastSnapshot.keyCode : -1;
        if (code != s_lastExistingGameNameKeyCode && code != -1) {
            size_t len = strlen(g_profileGameName);
            char asciiChar = HidKeyCodeToAsciiChar(code);
            if (asciiChar != '\0' && len < sizeof(g_profileGameName) - 1) {
                g_profileGameName[len] = asciiChar;
                g_profileGameName[len + 1] = '\0';
            } else if (code == 42 && len > 0) {
                g_profileGameName[len - 1] = '\0';
            }
        }
        s_lastExistingGameNameKeyCode = code;

        ImGui::InputText(Tr(STR_PROFILES_INPUT_GAME_NAME), g_profileGameName, sizeof(g_profileGameName),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::TextDisabled(Tr(STR_PROFILES_HINT_GAME_NAME_CHARS));
    }

    ImGui::SliderInt(Tr(STR_PROFILES_SENSITIVITY_LABEL), &g_profileSensitivity, 1000, 30000);
    ImGui::SliderInt(Tr(STR_PROFILES_DEADZONE_LABEL), &g_profileDeadzone, 0, 10000);
    ImGui::Checkbox(Tr(STR_PROFILES_INVERT_Y_LABEL), &g_profileInvertY);

    const char* curveNames[] = { Tr(STR_PROFILES_CURVE_LINEAR), Tr(STR_PROFILES_CURVE_EXPONENTIAL) };
    ImGui::Combo(Tr(STR_PROFILES_CURVE_COMBO_LABEL), &g_profileCurveTypeIdx, curveNames, 2);
    if (g_profileCurveTypeIdx == 1) {
        ImGui::SliderFloat(Tr(STR_PROFILES_EXPONENT_LABEL), &g_profileCurveExponent, 1.0f, 3.0f);
        ImGui::TextDisabled(Tr(STR_PROFILES_EXPONENT_HINT));
    }

    ImGui::Separator();
    if (ImGui::Button(Tr(STR_PROFILES_BTN_SAVE))) {
        uint32_t titleId = (uint32_t)strtoul(g_profileTitleIdHex, nullptr, 16);
        uint8_t curveType = (g_profileCurveTypeIdx == 1) ? CURVE_EXPONENTIAL : CURVE_LINEAR;
        // g_profileGameName vide n'efface PAS un nom deja enregistre (meme
        // convention "vide = ne pas toucher" que le raccourci F9, voir
        // ApplyMouseProfile dans config_writer.h) - un champ laisse vide en
        // renommant un profil existant conserve donc son ancien nom plutot
        // que de le vider silencieusement.
        bool ok = ApplyMouseProfile(g_selectedMouseVid, g_selectedMousePid, titleId,
            g_profileSensitivity, g_profileInvertY, g_profileDeadzone,
            curveType, g_profileCurveExponent, g_profileGameName);
        AppLog(ok ? "Profile %08X saved (name=%s sensitivity=%d deadzone=%d invertY=%d curve=%d)"
                  : "FAILED to save profile %08X",
            titleId, g_profileGameName, g_profileSensitivity, g_profileDeadzone, g_profileInvertY ? 1 : 0, (int)curveType);
        XexUtils::Xam::XNotify(ok ? "Profil enregistre" : "ECHEC sauvegarde du profil - Title ID invalide ?");
        if (ok) {
            RefreshProfiles();
            // Retrouve l'index du profil qu'on vient d'enregistrer (nouveau ou
            // modifie) pour que le combo reste sur cette entree plutot que de
            // retomber silencieusement sur "Nouveau profil".
            g_profileComboIdx = 0;
            for (size_t i = 0; i < g_profiles.size(); i++) {
                if (g_profiles[i].titleId == titleId) {
                    g_profileComboIdx = (int)i + 1; // +1 : espace natif du combo, voir g_profileComboIdx
                    break;
                }
            }
        }
    }

    if (!isNewProfile) {
        ImGui::SameLine();
        if (ImGui::Button(Tr(STR_PROFILES_BTN_DELETE))) {
            uint32_t titleId = (uint32_t)strtoul(g_profileTitleIdHex, nullptr, 16);
            bool ok = RemoveMouseProfile(g_selectedMouseVid, g_selectedMousePid, titleId);
            AppLog(ok ? "Profile %08X deleted" : "FAILED to delete profile %08X", titleId);
            XexUtils::Xam::XNotify(ok ? "Profil supprime" : "ECHEC suppression du profil");
            if (ok) {
                RefreshProfiles();
                g_profileComboIdx = 0;
                ResetProfileEditorForNew();
            }
        }
    }

    if (g_profiles.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(Tr(STR_PROFILES_NONE_SAVED));
    }

    ImGui::PopStyleVar(2);
    PopX360Font(g_fontSmall);
}

// --- Navigation manette via XexUtils::Input --------------------------------
// XexUtils::Input::GetInput() ne retourne jamais nullptr (voir Input.cpp -
// pointe toujours vers un slot statique interne, meme manette deconnectee) et
// fournit deja wButtons/PressedButtons debounces et ThumbLeftX/Y normalises
// en [-1,+1] avec deadzone appliquee - plus besoin de refaire cette logique
// a la main comme dans la toute premiere version de ce fichier.
static void UpdateGamepadNavigation(ImGuiIO& io) {
    XexUtils::Input::Gamepad* pad = XexUtils::Input::GetInput(0);

    // wButtons reste a 0 si la manette est deconnectee (voir GetInput) - pas
    // besoin de detecter explicitement l'absence de manette pour ce cas.
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    const WORD b = pad->wButtons;
    io.NavInputs[ImGuiNavInput_Activate]  = (b & XINPUT_GAMEPAD_A) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_Cancel]    = (b & XINPUT_GAMEPAD_B) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_DpadUp]    = (b & XINPUT_GAMEPAD_DPAD_UP) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_DpadDown]  = (b & XINPUT_GAMEPAD_DPAD_DOWN) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_DpadLeft]  = (b & XINPUT_GAMEPAD_DPAD_LEFT) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_DpadRight] = (b & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_FocusPrev] = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_FocusNext] = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f;

    // Deja normalise et deadzone par GetInput() - simple clamp au signe pour
    // ImGui (qui attend une magnitude positive par direction).
    io.NavInputs[ImGuiNavInput_LStickLeft]  = (pad->ThumbLeftX < 0.0f) ? -pad->ThumbLeftX : 0.0f;
    io.NavInputs[ImGuiNavInput_LStickRight] = (pad->ThumbLeftX > 0.0f) ?  pad->ThumbLeftX : 0.0f;
    io.NavInputs[ImGuiNavInput_LStickUp]    = (pad->ThumbLeftY > 0.0f) ?  pad->ThumbLeftY : 0.0f;
    io.NavInputs[ImGuiNavInput_LStickDown]  = (pad->ThumbLeftY < 0.0f) ? -pad->ThumbLeftY : 0.0f;

    // L'annulation du wizard sur BACK a ete retiree d'ici le 2026-08-17 (voir
    // le commentaire dans UpdateWizardModel, remplacee par ECHAP) : BACK lu ici
    // vient de l'etat XInput REEL (post-traduction par le mapping ACTUELLEMENT
    // ACTIF), donc appuyer sur la touche deja assignee a Back/Guide dans
    // l'ancien mapping annulait le wizard EN PLUS d'etre une touche candidate
    // pour l'etape en cours - conflit direct et frequent, Back/Guide faisant
    // partie des boutons qu'on configure justement.
}

// --- Curseur souris (2026-08-01) --------------------------------------------
// Ajoute suite au retour utilisateur du 2026-08-01 : la navigation manette
// seule (UpdateGamepadNavigation ci-dessus) etait jugee peu pratique pour
// choisir entre les deux onglets - et vu que cette appli configure
// justement une souris, autant qu'elle se pilote avec. Xbox 360 n'a pas de
// curseur systeme comme un bureau PC - ImGui dessine son propre curseur
// logiciel (io.MouseDrawCursor, deja active dans main() plus bas) a partir
// d'io.MousePos, qu'on met a jour ici a la main.
//
// mouseDX/mouseDY (voir input_state_reader.h) sont un DELTA accumule cote
// plugin.xex depuis la derniere lecture, pas une position absolue - on les
// ajoute donc a une position cumulee ici, note en pixels ecran (0,0 = coin
// haut-gauche, meme convention qu'io.MousePos). g_mouseSensitivityCursor est
// un facteur d'echelle simple, distinct du reglage "Sensibilite" du stick
// droit (onglet Reglages souris, qui modifie X360Remap.json cote manette) -
// celui-ci n'affecte que la vitesse du curseur menu, pas le jeu.
static float g_cursorX = 640.0f;
static float g_cursorY = 360.0f;
static bool g_cursorInitialized = false;
// 0.02f (valeur de depart arbitraire) confirme trop lent sur console reelle
// le 2026-08-01 (curseur bouge mais a peine) - x15 pour un ressenti plus
// proche d'un curseur de bureau normal. Encore une estimation, pas une
// valeur mesuree - a ajuster de nouveau si besoin.
// 0.3 -> 1.8 le 2026-09-23 (AUDIT_2026-09.md, I2). L'ancienne valeur avait
// ete reglee a l'oeil alors que chaque deplacement etait applique ~6 fois
// (voir la lecture de input_state.json dans la boucle principale). Chaque
// deplacement n'etant plus applique qu'une fois, 0.3 x 6 = 1.8 conserve la
// meme vitesse moyenne. A AJUSTER au test d'ergonomie si besoin.
static const float kCursorSensitivity = 1.8f;

static void UpdateMouseCursor(ImGuiIO& io, const InputStateSnapshot& snap, float screenWidth, float screenHeight) {
    if (!g_cursorInitialized) {
        // Centre de l'ecran au premier appel seulement - les frames
        // suivantes partent de la position deja cumulee, pas de reset.
        g_cursorX = screenWidth * 0.5f;
        g_cursorY = screenHeight * 0.5f;
        g_cursorInitialized = true;
    }

    if (snap.valid) {
        g_cursorX += (float)snap.mouseDX * kCursorSensitivity;
        g_cursorY += (float)snap.mouseDY * kCursorSensitivity;
        if (g_cursorX < 0.0f) g_cursorX = 0.0f;
        if (g_cursorY < 0.0f) g_cursorY = 0.0f;
        if (g_cursorX > screenWidth)  g_cursorX = screenWidth;
        if (g_cursorY > screenHeight) g_cursorY = screenHeight;

        io.MousePos = ImVec2(g_cursorX, g_cursorY);
        // bit0 = clic gauche, convention HID boot-mouse deja utilisee partout
        // ailleurs dans ce projet (voir X360Remap/main.cpp,
        // HidFillBootMouseState).
        io.MouseDown[0] = (snap.mouseButtonsMask & 0x01) != 0;
        io.MouseDown[1] = (snap.mouseButtonsMask & 0x02) != 0;
        io.MouseDown[2] = (snap.mouseButtonsMask & 0x04) != 0;
    } else {
        // Pas de plugin.xex charge / pas de souris branchee - curseur hors
        // ecran plutot que fige a une position potentiellement trompeuse
        // (ImGui traite une position negative comme "souris absente").
        io.MousePos = ImVec2(-1.0f, -1.0f);
        io.MouseDown[0] = io.MouseDown[1] = io.MouseDown[2] = false;
    }
}

// --- Selection de peripherique (Jalon 8, application/ROADMAP_APPLICATION.md) --
// Remplace le VID/PID en dur (kKeyboardVid/kMouseVid, repli par defaut
// toujours propose en index 0 de chaque combo) par une vraie liste lue dans
// known_devices.txt via ListKnownDevices() (config_writer.h) - historique que
// X360Remap.xex alimente lui-meme des qu'il classe un device en souris/
// clavier a l'enumeration USB, PAS une configuration explicite via cette
// appli (voir le commentaire de KnownDeviceInfo dans config_writer.h pour
// l'historique de cette decision, 2026-08-03 : la premiere version listait
// seulement les devices deja configures ici, et une saisie manuelle de
// VID/PID a ete explicitement rejetee par l'utilisateur). isMouse est un
// booleen exact (classification reelle du plugin) - deux listes filtrees
// simples, sans l'ambiguite d'heuristique de la premiere version.
static bool g_devicesLoaded = false;
static std::vector<KnownDeviceInfo> g_knownDevices;
// Indices DANS g_knownDevices, pas des VID/PID directement - evite de
// dupliquer les infos device, juste un filtre. Reconstruits par
// RefreshKnownDevices() (jamais modifies frame par frame ailleurs).
static std::vector<int> g_keyboardDeviceIndices;
static std::vector<int> g_mouseDeviceIndices;
// Espace d'index NATIF du combo : 0 = "Par defaut", 1..N = g_xDeviceIndices[i-1]
// - meme convention que g_profileComboIdx plus haut (voir son commentaire),
// pour la meme raison (ImGui::Combo veut un entier stable entre les frames).
static int g_keyboardComboIdx = 0;
static int g_mouseComboIdx = 0;

static void RefreshKnownDevices() {
    g_knownDevices = ListKnownDevices();
    g_keyboardDeviceIndices.clear();
    g_mouseDeviceIndices.clear();
    for (size_t i = 0; i < g_knownDevices.size(); i++) {
        const KnownDeviceInfo& d = g_knownDevices[i];
        if (!d.isMouse) g_keyboardDeviceIndices.push_back((int)i);
        if (d.isMouse) g_mouseDeviceIndices.push_back((int)i);
    }
}

static void LoadDevicesOnce() {
    if (g_devicesLoaded)
        return;
    g_devicesLoaded = true;
    RefreshKnownDevices();
}

// Libelle d'un peripherique dans les listes deroulantes. Le NOM d'abord, le
// VID:PID seulement en repli - remarque de l'utilisateur (2026-08-03) :
// afficher un matricule hexadecimal a quelqu'un qui veut juste choisir sa
// souris n'a aucun sens, d'autant qu'on interroge deja le peripherique pour
// savoir lire ses donnees. Le nom vient du descripteur de chaine USB, recupere
// par le plugin et stocke dans known_devices.txt.
static void FormatDeviceLabel(char* out, size_t outSize, const KnownDeviceInfo& d) {
    if (d.name[0] != '\0')
        _snprintf(out, outSize - 1, "%hs", d.name);
    else
        _snprintf(out, outSize - 1, Tr(STR_DEVICE_NAME_UNAVAILABLE_FMT), d.vid, d.pid);
    out[outSize - 1] = '\0';
}

static bool KeyboardComboGetter(void*, int idx, const char** out_text) {
    static char s_label[48]; // un seul combo actif a la fois, meme precaution que ProfileComboGetter
    if (idx == 0) {
        _snprintf(s_label, sizeof(s_label) - 1, Tr(STR_DEVICE_DEFAULT_LABEL_FMT), kKeyboardVid, kKeyboardPid);
        s_label[sizeof(s_label) - 1] = '\0';
        *out_text = s_label;
        return true;
    }
    int di = idx - 1;
    if (di < 0 || di >= (int)g_keyboardDeviceIndices.size())
        return false;
    const KnownDeviceInfo& d = g_knownDevices[g_keyboardDeviceIndices[di]];
    FormatDeviceLabel(s_label, sizeof(s_label), d);
    *out_text = s_label;
    return true;
}

static bool MouseComboGetter(void*, int idx, const char** out_text) {
    static char s_label[48];
    if (idx == 0) {
        _snprintf(s_label, sizeof(s_label) - 1, Tr(STR_DEVICE_DEFAULT_LABEL_FMT), kMouseVid, kMousePid);
        s_label[sizeof(s_label) - 1] = '\0';
        *out_text = s_label;
        return true;
    }
    int di = idx - 1;
    if (di < 0 || di >= (int)g_mouseDeviceIndices.size())
        return false;
    const KnownDeviceInfo& d = g_knownDevices[g_mouseDeviceIndices[di]];
    FormatDeviceLabel(s_label, sizeof(s_label), d);
    *out_text = s_label;
    return true;
}

// --- Barre superieure : logo + onglets (2026-08-14) -------------------------
// Le logo occupe la gauche, les onglets suivent sur la meme ligne, exactement
// comme dans la maquette. Techniquement, ImGui ne sait pas mettre un logo
// "dans" une barre d'onglets : on dessine donc le logo, puis SameLine, puis la
// barre - l'alignement vertical se regle a la main.
//
// Le logo est du TEXTE, pas une image : deux mots dans deux couleurs suffisent
// a le rendre, et ca reste net a toute taille sans consommer de texture. Une
// version graphique pourra le remplacer plus tard sans toucher au reste.
// Marges de securite TV. Le FOND occupe tout l'ecran (voir la creation de la
// fenetre dans main()), seul le CONTENU LISIBLE respecte ces marges. L'axe
// vertical est plus rogne que l'horizontal sur la plupart des televiseurs,
// d'ou deux valeurs distinctes.
// kUiSafeX / kUiSafeY sont definis dans ui_mapping.h (source unique).

// Barre superieure : bandeau pleine largeur (il touche les bords, ce qui donne
// l'aspect plein ecran), logo et onglets a l'interieur de la zone sure.
//
// Onglets DESSINES A LA MAIN plutot que ImGui::BeginTabBar : la barre native
// impose son propre cadre et, surtout, decale horizontalement tout le contenu
// de l'onglet quand on la place apres un SameLine - defaut vu en
// previsualisation le 2026-08-14, qui amputait la page de plus de 300 px sur
// la gauche. Ici on maitrise le placement, et un soulignement anime pourra
// s'ajouter sans rien casser.
// La barre superieure vit desormais dans ui_mapping.cpp (DrawUiTopBar),
// partagee avec la previsualisation - voir preview/LISEZ-MOI.md. La copie qui
// se trouvait ici a ete retiree le 2026-08-14 : c'est exactement le genre de
// duplication qui avait fait diverger l'ecran de sa maquette.

static void DrawDeviceSelector() {
    LoadDevicesOnce();

    PushX360Font(g_fontSmall);
    ImGui::TextColored(kColorKeyboardAccent, Tr(STR_DEVICE_HEADER));
    PopX360Font(g_fontSmall);
    ImGui::Spacing();

    // Colonne etroite (250 px) : les listes deroulantes sont EMPILEES et leur
    // libelle est place AU-DESSUS, pas a cote. Cote a cote comme avant, elles
    // debordaient et leurs libelles disparaissaient hors de la zone visible.
    // La largeur est bornee explicitement pour la meme raison.
    ImGui::PushItemWidth(-1.0f); // -1 = toute la largeur disponible de la colonne

    PushX360Font(g_fontSmall);
    ImGui::TextDisabled(Tr(STR_DEVICE_KEYBOARD_LABEL));
    PopX360Font(g_fontSmall);
    // "##" : identifiant interne unique, sans libelle visible - celui-ci est
    // deja affiche au-dessus.
    if (ImGui::Combo("##kbdev", &g_keyboardComboIdx, KeyboardComboGetter, nullptr,
                      (int)g_keyboardDeviceIndices.size() + 1)) {
        if (g_keyboardComboIdx == 0) {
            g_selectedKeyboardVid = kKeyboardVid;
            g_selectedKeyboardPid = kKeyboardPid;
        } else {
            const KnownDeviceInfo& d = g_knownDevices[g_keyboardDeviceIndices[g_keyboardComboIdx - 1]];
            g_selectedKeyboardVid = d.vid;
            g_selectedKeyboardPid = d.pid;
        }
        // Aucun ecran ne met en cache un etat charge specifiquement pour le
        // clavier (contrairement a la souris, voir g_mouseSettingsLoaded/
        // g_profilesLoaded ci-dessous) - le wizard relit toujours a l'ouverture,
        // rien a invalider ici.
    }
    ImGui::Spacing();
    PushX360Font(g_fontSmall);
    ImGui::TextDisabled(Tr(STR_DEVICE_MOUSE_LABEL));
    PopX360Font(g_fontSmall);
    if (ImGui::Combo("##mousedev", &g_mouseComboIdx, MouseComboGetter, nullptr,
                      (int)g_mouseDeviceIndices.size() + 1)) {
        if (g_mouseComboIdx == 0) {
            g_selectedMouseVid = kMouseVid;
            g_selectedMousePid = kMousePid;
        } else {
            const KnownDeviceInfo& d = g_knownDevices[g_mouseDeviceIndices[g_mouseComboIdx - 1]];
            g_selectedMouseVid = d.vid;
            g_selectedMousePid = d.pid;
        }
        // Force le rechargement des ecrans "Reglages souris"/"Profils" au
        // prochain affichage - sans ca, ils garderaient affiches les reglages
        // de l'ANCIEN device jusqu'a la fermeture de l'appli (LoadMouseSettingsOnce/
        // LoadProfilesOnce ne rechargent que si ce flag est a false).
        g_mouseSettingsLoaded = false;
        g_profilesLoaded = false;
    }
    ImGui::PopItemWidth();

    ImGui::Spacing();
    if (ImGui::Button(Tr(STR_DEVICE_BTN_REFRESH))) {
        RefreshKnownDevices();
    }

    ImGui::Spacing();
    // TextWrapped, pas Text : ce conseil est long et serait tronque a droite
    // dans une colonne de 250 px. Il doit passer a la ligne.
    PushX360Font(g_fontSmall);
    ImGui::TextWrapped(Tr(STR_DEVICE_ONE_DEFAULT_HINT));
    PopX360Font(g_fontSmall);

    // Selecteur de langue (2026-08-07, deplace ici le 2026-08-14). Il vit a
    // cote du selecteur de peripheriques parce qu'il concerne TOUTE
    // l'application : enfoui dans les reglages de la souris, il restait
    // invisible pour qui n'ouvrait jamais cet onglet.
    //
    // Les noms "Francais"/"English" restent volontairement EN DUR (pas de
    // Tr()) : un selecteur de langue affiche conventionnellement chaque langue
    // dans SA PROPRE langue, quelle que soit la langue active - sinon
    // "English" deviendrait illisible pour un francophone qui veut justement
    // basculer vers l'anglais, et inversement.
    {
        Lang currentLang = GetLanguage();
        if (ImGui::RadioButton("Francais", currentLang == LANG_FR))
            SaveLanguagePref(LANG_FR);
        ImGui::SameLine();
        if (ImGui::RadioButton("English", currentLang == LANG_EN))
            SaveLanguagePref(LANG_EN);
        ImGui::SameLine();
        // Portugais ajoute le 2026-08-24 - voir i18n.h/i18n.cpp. "Portugues"
        // volontairement sans accent ICI MEME (contrairement au reste de la
        // table PT-BR) : ce libelle est un des trois noms de langue en dur
        // juste au-dessus, affiches dans LEUR PROPRE langue independamment de
        // la langue active - meme regle qu'eux, donc.
        if (ImGui::RadioButton("Portugues", currentLang == LANG_PT_BR))
            SaveLanguagePref(LANG_PT_BR);
        ImGui::SameLine();
        // Espagnol ajoute le 2026-09-13. "Espanol" sans tilde ICI MEME, meme
        // regle que "Portugues" juste au-dessus : les noms de langue de ce
        // selecteur sont ecrits en ASCII, chacun dans SA propre langue et
        // independamment de la langue active.
        if (ImGui::RadioButton("Espanol", currentLang == LANG_ES))
            SaveLanguagePref(LANG_ES);
    }

    ImGui::Separator();
}

// --- Icones de la page "Comment ca marche" (2026-08-17) ---------------------
// Retour HB : "une mise en page est necessaire, ajouter des icones et faire
// une page qui engage et facilite la comprehension de comment ca marche".
// Traits ImDrawList a main levee (meme technique que DrawTeaCupIcon/
// DrawMoroccoFlagIcon, page Soutenir) plutot que des textures : ce sont de
// petits pictogrammes simples, pas la peine d'embarquer 6 images de plus
// pour ca. Un pictogramme DIFFERENT par etape (prise, clavier, roue dentee,
// etoile) aide a distinguer "brancher" de "regler" d'un coup d'oeil, avant
// meme de lire le texte - c'est le "engage et facilite" demande.
enum HowToIconKind { HOWICON_PLUG, HOWICON_KEYS, HOWICON_GEAR, HOWICON_STAR };

static void DrawHowToIcon(HowToIconKind kind, ImVec2 topLeft, float size, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    switch (kind) {
    case HOWICON_PLUG: {
        // Connecteur USB simplifie : corps + deux broches + cable vers le bas.
        float w = size * 0.6f, h = size * 0.8f;
        ImVec2 tl(topLeft.x + (size - w) * 0.5f, topLeft.y + size * 0.15f);
        dl->AddRect(tl, ImVec2(tl.x + w, tl.y + h), col, 1.0f, 0, 1.3f);
        dl->AddLine(ImVec2(tl.x + w * 0.25f, tl.y), ImVec2(tl.x + w * 0.25f, tl.y - size * 0.18f), col, 1.3f);
        dl->AddLine(ImVec2(tl.x + w * 0.75f, tl.y), ImVec2(tl.x + w * 0.75f, tl.y - size * 0.18f), col, 1.3f);
        dl->AddLine(ImVec2(topLeft.x + size * 0.5f, tl.y + h), ImVec2(topLeft.x + size * 0.5f, topLeft.y + size), col, 1.3f);
        break;
    }
    case HOWICON_KEYS: {
        // Grille 3x2 : evoque un clavier sans dessiner chaque touche.
        float cell = size * 0.28f, gap = size * 0.06f;
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 3; c++) {
                ImVec2 tl(topLeft.x + c * (cell + gap), topLeft.y + size * 0.15f + r * (cell + gap));
                dl->AddRect(tl, ImVec2(tl.x + cell, tl.y + cell), col, 1.0f, 0, 1.2f);
            }
        break;
    }
    case HOWICON_GEAR: {
        // Roue dentee simplifiee : cercle + 8 crans radiaux.
        ImVec2 c(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);
        float rOuter = size * 0.42f, rBody = rOuter * 0.72f, rInner = size * 0.12f;
        dl->AddCircle(c, rBody, col, 16, 1.3f);
        for (int i = 0; i < 8; i++) {
            float ang = (float)i * 3.14159265f / 4.0f;
            ImVec2 p0(c.x + cosf(ang) * rBody, c.y + sinf(ang) * rBody);
            ImVec2 p1(c.x + cosf(ang) * rOuter, c.y + sinf(ang) * rOuter);
            dl->AddLine(p0, p1, col, 1.6f);
        }
        dl->AddCircle(c, rInner, col, 8, 1.2f);
        break;
    }
    case HOWICON_STAR: {
        // Meme construction que DrawMoroccoFlagIcon (page Soutenir) - 5
        // points externes + 5 internes en alternance, remplis.
        ImVec2 c(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);
        float rOuter = size * 0.46f, rInner = rOuter * 0.42f;
        ImVec2 pts[10];
        for (int i = 0; i < 10; i++) {
            float ang = -3.14159265f * 0.5f + (float)i * 3.14159265f / 5.0f;
            float r = (i % 2 == 0) ? rOuter : rInner;
            pts[i] = ImVec2(c.x + r * cosf(ang), c.y + r * sinf(ang));
        }
        dl->AddConvexPolyFilled(pts, 10, col);
        break;
    }
    }
}

// Eclair : action rapide, sans passer par un menu (liaison rapide). Trace en
// traits (pas rempli) - un eclair est une forme non convexe, AddConvexPolyFilled
// la deformerait ; un simple trait en Z reste lisible en tres petit.
static void DrawLightningIcon(ImVec2 topLeft, float size, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(topLeft.x + size * 0.55f, topLeft.y);
    ImVec2 b(topLeft.x + size * 0.15f, topLeft.y + size * 0.55f);
    ImVec2 c(topLeft.x + size * 0.55f, topLeft.y + size * 0.55f);
    ImVec2 d(topLeft.x + size * 0.20f, topLeft.y + size);
    dl->AddLine(a, b, col, 2.0f);
    dl->AddLine(b, c, col, 2.0f);
    dl->AddLine(c, d, col, 2.0f);
}

// Disquette simplifiee : cadre + coin plie + etiquette - convention
// "sauvegarder" reconnaissable meme sur une console qui n'en a jamais vu.
static void DrawSaveIcon(ImVec2 topLeft, float size, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(topLeft, ImVec2(topLeft.x + size, topLeft.y + size), col, 2.0f, 0, 1.3f);
    dl->AddLine(ImVec2(topLeft.x + size * 0.65f, topLeft.y), ImVec2(topLeft.x + size, topLeft.y + size * 0.35f), col, 1.3f);
    ImVec2 lt(topLeft.x + size * 0.22f, topLeft.y + size * 0.4f);
    dl->AddRect(lt, ImVec2(lt.x + size * 0.56f, lt.y + size * 0.42f), col, 1.0f, 0, 1.2f);
}

// Une etape numerotee, avec son icone a gauche - remplace BulletText.
static void DrawHowToStepRow(HowToIconKind icon, const char* text, const ImVec4& accent) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float iconSize = ImGui::GetTextLineHeight() * 0.95f;
    DrawHowToIcon(icon, p, iconSize, ImGui::GetColorU32(accent));
    ImGui::SetCursorScreenPos(ImVec2(p.x + iconSize + 10.0f, p.y));
    ImGui::TextWrapped("%s", text);
    ImGui::Dummy(ImVec2(0, 3.0f));
}

// --- Onglet "Comment ca marche" (2026-08-11, redesign 2026-08-17) -----------
// Demande utilisateur d'origine : "page how it works... explique comment
// l'application marche et les raccourcis de la liaison rapide rien de
// complique on laisse simple". Retour du 2026-08-17 : "une mise en page est
// necessaire ajouter des icones et faire une page qui engage et facilite la
// comprehension" - le CONTENU reste le meme texte simple qu'avant (relu
// depuis i18n.h/.cpp), seule la PRESENTATION change : icone par etape,
// icone par section, et la meme reduction de densite que la page Profils
// (voir son commentaire "Densite reduite" plus haut) pour eviter tout
// defilement ici aussi.
static void DrawHowItWorksTab() {
    PushX360Font(g_fontSmall);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));

    ImGui::TextWrapped(Tr(STR_HOWTO_INTRO));
    ImGui::Spacing();

    DrawHowToStepRow(HOWICON_PLUG,  Tr(STR_HOWTO_STEP_PLUG),      kColorKeyboardAccent);
    DrawHowToStepRow(HOWICON_KEYS,  Tr(STR_HOWTO_STEP_WIZARD),    kColorKeyboardAccent);
    DrawHowToStepRow(HOWICON_GEAR,  Tr(STR_HOWTO_STEP_SETTINGS),  kColorMouseAccent);
    DrawHowToStepRow(HOWICON_STAR,  Tr(STR_HOWTO_STEP_PROFILES),  kColorMouseAccent);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Bleu (kColorMouseAccent) et vert (kColorKeyboardAccent) utilises ici
    // juste comme deux couleurs de section distinctes pour separer
    // visuellement "liaison rapide" de "sauvegarde F9" - aucun lien avec
    // leur usage clavier/souris habituel (voir theme.h).
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float iconSize = ImGui::GetTextLineHeight() * 0.85f;
        DrawLightningIcon(p, iconSize, ImGui::GetColorU32(kColorMouseAccent));
        ImGui::SetCursorScreenPos(ImVec2(p.x + iconSize + 8.0f, p.y));
        ImGui::TextColored(kColorMouseAccent, "%s", Tr(STR_HOWTO_QUICKBIND_HEADER));
    }
    ImGui::TextWrapped(Tr(STR_HOWTO_QUICKBIND_TEXT));
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float iconSize = ImGui::GetTextLineHeight() * 0.85f;
        DrawSaveIcon(p, iconSize, ImGui::GetColorU32(kColorKeyboardAccent));
        ImGui::SetCursorScreenPos(ImVec2(p.x + iconSize + 8.0f, p.y));
        ImGui::TextColored(kColorKeyboardAccent, "%s", Tr(STR_HOWTO_F9_HEADER));
    }
    ImGui::TextWrapped(Tr(STR_HOWTO_F9_TEXT));

    ImGui::PopStyleVar();
    PopX360Font(g_fontSmall);
}

// --- Onglet "Soutenir le projet" (2026-08-11) -------------------------------
// Demande utilisateur : message du developpeur + liens de don (PayPal/
// Ko-fi/GitHub/YouTube), dans un style volontairement "plus attractif et
// revendeur" que le reste de l'interface (plutot neutre/technique
// partout ailleurs) - exception assumee, une seule page.
//
// Chaque lien passe par IsLinkIntact() (donation_links.h) avant d'etre
// affiche - meme principe de protection legere qu'evoque pour le QR code
// de don : si quelqu'un patche la chaine dans le binaire compile sans
// repatcher le hash juste a cote, ce lien precis disparait plutot que
// d'afficher une valeur alteree. Ce n'est PAS une protection contre
// quelqu'un qui recompile depuis les sources (droit legitime sur un fork
// GPL) - juste un obstacle a un patch binaire a la main.
//
// Pas de QR code pour l'instant (2026-08-11) : en attente du fichier SVG
// que HB doit fournir - voir PROJECT_NOTES.md. Cette page n'affiche que du
// texte lisible depuis la TV ; le QR s'ajoutera a cote sans restructurer
// cette fonction.
// (DrawSupportRow retiree a la refonte v1.2 - remplacee par DrawSupportCard,
//  plus bas, qui porte en plus un sous-titre et un libelle d'intention.)

// Version affichee sur la page Soutenir. A incrementer a chaque release
// publiee - c'est aussi ce que l'utilisateur lira pour savoir s'il doit
// mettre a jour.
static const char* kAppVersion = "1.2.0";

// Une "carte" de la page Soutenir : titre d'intention, sous-titre, et la
// valeur concrete (email/URL) protegee par son hash. Voir DrawSupportTab
// pour le raisonnement sur les libelles.
static void DrawSupportCard(const ImVec4& accent, const char* title, const char* subtitle,
                            const char* value, uint32_t expectedHash) {
    ImGui::Spacing();
    ImGui::TextColored(accent, "%s", title);
    ImGui::TextDisabled("%s", subtitle);
    if (IsLinkIntact(value, expectedHash))
        ImGui::TextWrapped("%s", value);
    else
        ImGui::TextDisabled("%s", Tr(STR_SUPPORT_LINK_HIDDEN_HINT));
    ImGui::Spacing();
    ImGui::Separator();
}

// --- Icones "tasse de the" et "drapeau du Maroc" (2026-08-17) ---------------
// Demande HB : le titre de la carte de don ("Invite me to a tea with mint")
// doit porter une tasse a the ET le drapeau marocain, visible - la reference
// culturelle au the a la menthe marocain, pas juste une formule en l'air.
// Dessinees ici (traits ImDrawList, pas des PNG) : ce sont deux petits
// pictogrammes simples, pas la peine d'ajouter deux textures embarquees
// (voir image_data_qrcode.h pour la difference : le QR, lui, DOIT etre une
// image exacte, un dessin vectoriel ne le rendrait pas scannable).
static void DrawTeaCupIcon(ImVec2 topLeft, float size, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bodyW = size * 0.72f, bodyH = size * 0.60f;
    ImVec2 bodyTL(topLeft.x, topLeft.y + size * 0.30f);
    ImVec2 bodyBR(bodyTL.x + bodyW, bodyTL.y + bodyH);
    // Corps legerement evase (trapeze) plutot qu'un simple rectangle - se
    // lit mieux comme "tasse" a cette taille.
    ImVec2 p0(bodyTL.x + bodyW * 0.08f, bodyTL.y);
    ImVec2 p1(bodyTL.x + bodyW * 0.92f, bodyTL.y);
    ImVec2 p2(bodyBR.x, bodyBR.y);
    ImVec2 p3(bodyTL.x, bodyBR.y);
    dl->AddQuad(p0, p1, p2, p3, col, 1.4f);
    // Soucoupe : simple trait horizontal qui deborde legerement du corps.
    dl->AddLine(ImVec2(bodyTL.x - bodyW * 0.12f, bodyBR.y + 1.5f),
                ImVec2(bodyBR.x + bodyW * 0.12f, bodyBR.y + 1.5f), col, 1.4f);
    // Anse : cercle non rempli accroche au bord droit.
    ImVec2 handleC(bodyBR.x + bodyW * 0.16f, bodyTL.y + bodyH * 0.42f);
    dl->AddCircle(handleC, bodyW * 0.16f, col, 10, 1.3f);
    // Vapeur : deux petits traits obliques au-dessus, signature "boisson
    // chaude" reconnaissable meme en tres petit.
    float sy = topLeft.y;
    dl->AddLine(ImVec2(bodyTL.x + bodyW * 0.30f, sy + size * 0.22f),
                ImVec2(bodyTL.x + bodyW * 0.22f, sy), col, 1.2f);
    dl->AddLine(ImVec2(bodyTL.x + bodyW * 0.62f, sy + size * 0.22f),
                ImVec2(bodyTL.x + bodyW * 0.54f, sy), col, 1.2f);
}

// Champ rouge + etoile verte a 5 branches (pas entrelacee, simplifiee pour
// rester lisible en tres petit) - suffisant pour etre reconnu comme le
// drapeau marocain a cote du texte, sans viser une reproduction heraldique
// exacte.
static void DrawMoroccoFlagIcon(ImVec2 topLeft, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 red   = IM_COL32(0xC1, 0x27, 0x2D, 255);
    const ImU32 green = IM_COL32(0x00, 0x62, 0x33, 255);
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), red, 1.5f);
    dl->AddRect(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), IM_COL32(0, 0, 0, 60), 1.5f);

    // Etoile a 5 branches : 5 points externes + 5 points internes en
    // alternance, angle de depart -90deg (une pointe vers le haut, comme sur
    // le vrai drapeau).
    ImVec2 c(topLeft.x + size.x * 0.5f, topLeft.y + size.y * 0.5f);
    float rOuter = size.y * 0.42f, rInner = rOuter * 0.42f;
    ImVec2 pts[10];
    for (int i = 0; i < 10; i++) {
        float ang = -3.14159265f * 0.5f + (float)i * 3.14159265f / 5.0f;
        float r = (i % 2 == 0) ? rOuter : rInner;
        pts[i] = ImVec2(c.x + r * cosf(ang), c.y + r * sinf(ang));
    }
    dl->AddConvexPolyFilled(pts, 10, green);
}

static void DrawSupportTab() {
    // Refonte v1.2 (2026-08-13). Deux changements de fond par rapport a la
    // premiere version :
    //
    // 1. "EN COURS DE DEVELOPPEMENT" remplace par la VERSION. Ca dit la meme
    //    chose - le projet vit et bouge - mais de facon valorisante et utile
    //    plutot que comme un aveu d'inachevement.
    //
    // 2. Les libelles disent l'INTENTION, pas le nom du service : "Verifier
    //    les mises a jour" plutot que "GitHub", "Offre-moi un cafe" plutot
    //    que "faire un don Ko-fi". L'utilisateur type est un joueur, pas un
    //    developpeur - "GitHub" ne lui evoque rien.
    //
    // Aucun lien n'est cliquable (pas de navigateur sur console) : ce sont
    // des informations a lire et a recopier, d'ou l'importance de les garder
    // courtes et lisibles a 2-3 m.
    ImGui::TextColored(kColorKeyboardAccent, Tr(STR_SUPPORT_VERSION_FMT), kAppVersion);
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", Tr(STR_SUPPORT_WHATSNEW));
    ImGui::Spacing();

    ImGui::TextWrapped(Tr(STR_SUPPORT_HEADLINE));
    ImGui::Spacing();
    ImGui::TextWrapped(Tr(STR_SUPPORT_MESSAGE));
    ImGui::Spacing();
    ImGui::TextColored(kColorKeyboardAccent, Tr(STR_SUPPORT_CTA));
    ImGui::Spacing();
    ImGui::Separator();

    // Le don en premier : c'est la raison d'etre de la page. Mais il vient
    // APRES l'accroche "gratuit et open-source" plus haut - demander un
    // soutien juste apres avoir rappele que l'outil est et restera gratuit
    // le presente comme un remerciement, pas comme un peage.
    //
    // Carte Ko-fi speciale (2026-08-17, demande HB) : tasse a the + drapeau
    // marocain a cote du titre ("Invite me to a tea with mint" - reference
    // au the a la menthe marocain, pas une formule en l'air), et le QR Ko-fi
    // fourni par HB affiche a droite du texte, scannable directement depuis
    // l'ecran. Les autres cartes (PayPal, mises a jour, YouTube, feedback)
    // gardent DrawSupportCard tel quel - ce traitement special est reserve
    // au premier point de contact de la page, pas generalise partout.
    {
        ImGui::Spacing();
        const float iconY = ImGui::GetCursorPosY();
        const float iconSize = 15.0f;
        const ImVec2 iconsScreenStart = ImGui::GetCursorScreenPos();
        DrawTeaCupIcon(iconsScreenStart, iconSize, ImGui::GetColorU32(kColorKeyboardAccent));
        DrawMoroccoFlagIcon(ImVec2(iconsScreenStart.x + iconSize + 6.0f, iconsScreenStart.y + iconSize * 0.15f),
                            ImVec2(20.0f, 13.0f));
        ImGui::SetCursorPosY(iconY);
        ImGui::Dummy(ImVec2(iconSize + 6.0f + 20.0f + 8.0f, iconSize));
        ImGui::SameLine();
        ImGui::TextColored(kColorKeyboardAccent, "%s", Tr(STR_SUPPORT_CARD_DONATE));

        // Deux colonnes : texte (Ko-fi/PayPal + lien) a gauche, QR a droite -
        // meme zone que le reste de la page (pas de pleine largeur), pour que
        // le QR reste a portee visuelle du texte qui l'explique.
        ImVec2 rowTop = ImGui::GetCursorScreenPos();
        const float qrSize = 72.0f;
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", Tr(STR_SUPPORT_CARD_DONATE_SUB));
        if (IsLinkIntact(kKofiUrl, kKofiUrlHash))
            ImGui::TextWrapped("%s", kKofiUrl);
        else
            ImGui::TextDisabled("%s", Tr(STR_SUPPORT_LINK_HIDDEN_HINT));
        ImGui::EndGroup();

        if (g_texQrKofi) {
            ImVec2 qrPos(rowTop.x + 260.0f, rowTop.y);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(qrPos.x - 4.0f, qrPos.y - 4.0f),
                              ImVec2(qrPos.x + qrSize + 4.0f, qrPos.y + qrSize + 4.0f),
                              IM_COL32(255, 255, 255, 255), 3.0f);
            dl->AddImage((ImTextureID)g_texQrKofi, qrPos, ImVec2(qrPos.x + qrSize, qrPos.y + qrSize));
            ImGui::SetCursorScreenPos(ImVec2(qrPos.x - 4.0f, qrPos.y + qrSize + 8.0f));
            ImGui::TextDisabled("%s", Tr(STR_SUPPORT_QR_HINT));
        }

        ImGui::Spacing();
        ImGui::Separator();
    }
    // PayPal partage la meme carte que Ko-fi cote maquette ; ici on l'affiche
    // juste en dessous, meme intention, autre moyen.
    DrawSupportCard(kColorKeyboardAccent, Tr(STR_SUPPORT_PAYPAL_LABEL),
                    Tr(STR_SUPPORT_CARD_DONATE_SUB), kPayPalEmail, kPayPalEmailHash);
    DrawSupportCard(kColorMouseAccent, Tr(STR_SUPPORT_CARD_UPDATE),
                    Tr(STR_SUPPORT_CARD_UPDATE_SUB), kGithubUrl, kGithubUrlHash);
    // (Carte YouTube retiree le 2026-09-25 : chaine supprimee, nouvelle a venir.)
    // Retours : meme adresse que PayPal, mais intention differente - d'ou une
    // carte distincte plutot qu'une mention en petit sous le don.
    DrawSupportCard(ImVec4(1.0f, 0.67f, 0.16f, 1.0f), Tr(STR_SUPPORT_CARD_FEEDBACK),
                    "", kPayPalEmail, kPayPalEmailHash);
}

// --- Grille des assignations clavier (2026-08-14) ---------------------------
// Nom lisible d'un code de touche HID (page Keyboard/Keypad 0x07). Volontairement
// limite aux touches qu'on peut reellement croiser sur un clavier utilise en
// jeu : lettres, chiffres, modificateurs, fleches, touches d'edition. Tout le
// reste retombe sur un code hexadecimal, ce qui reste plus informatif qu'une
// case vide.
//
// Buffer statique : la valeur est consommee immediatement par l'appelant
// (affichage d'une cellule), jamais conservee.
static const char* HidKeyCodeToLabel(uint8_t code) {
    static char s_buf[16];

    if (code >= 0x04 && code <= 0x1D) { // A..Z
        s_buf[0] = (char)('A' + (code - 0x04)); s_buf[1] = '\0'; return s_buf;
    }
    if (code >= 0x1E && code <= 0x26) { // 1..9
        s_buf[0] = (char)('1' + (code - 0x1E)); s_buf[1] = '\0'; return s_buf;
    }
    if (code == 0x27) return "0";
    if (code >= 0x3A && code <= 0x45) { // F1..F12
        _snprintf(s_buf, sizeof(s_buf) - 1, "F%d", (int)(code - 0x3A) + 1);
        s_buf[sizeof(s_buf) - 1] = '\0';
        return s_buf;
    }
    // PAVE NUMERIQUE (0x53..0x63). Ajoute le 2026-08-14 apres verification sur
    // console : la configuration de HB assigne la plupart des boutons manette
    // au pave numerique, et ces codes s'affichaient donc en "0x5A", "0x5B"...
    // faute d'etre couverts ici. Attention, l'ordre HID n'est PAS 0,1,2,3 : il
    // commence a 1 (0x59) et met le zero a la fin (0x62).
    if (code >= 0x59 && code <= 0x61) { // pave 1..9
        _snprintf(s_buf, sizeof(s_buf) - 1, "NUM %d", (int)(code - 0x59) + 1);
        s_buf[sizeof(s_buf) - 1] = '\0';
        return s_buf;
    }
    // NOMS DE TOUCHES : ABREVIATIONS UNIVERSELLES, PAS TRADUITES (2026-09-13).
    //
    // Ces libelles etaient en francais en dur (ENTREE, ECHAP, ESPACE, MAJ G.,
    // PAVE 1...) et s'affichaient tels quels meme avec l'interface en anglais
    // - signale par HB en testant la version anglaise.
    //
    // Choix retenu : les abreviations imprimees sur les claviers physiques de
    // la plupart des pays (ESC, TAB, CTRL, ALT, SHIFT, ENTER, DEL, INS, HOME,
    // END, NUM...). Elles ne sont donc pas vraiment "de l'anglais" mais une
    // convention clavier, lisible quelle que soit la langue de l'interface -
    // et c'est aussi ce que font la quasi-totalite des jeux PC.
    //
    // L'alternative - passer ces ~30 libelles dans le systeme de traduction -
    // representait 120 chaines supplementaires pour quatre langues, sur des
    // etiquettes de trois caracteres affichees dans des cases de grille
    // etroites. Disproportionne ici ; reste faisable plus tard si la demande
    // vient des utilisateurs.
    switch (code) {
        case 0x53: return "NUM LOCK";
        case 0x54: return "NUM /";
        case 0x55: return "NUM *";
        case 0x56: return "NUM -";
        case 0x57: return "NUM +";
        case 0x58: return "NUM ENT";
        case 0x62: return "NUM 0";
        case 0x63: return "NUM .";
        case 0x49: return "INS";
        case 0x4A: return "HOME";
        case 0x4B: return "PG UP";
        case 0x4C: return "DEL";
        case 0x4D: return "END";
        case 0x4E: return "PG DN";
        case 0x28: return "ENTER";
        case 0x29: return "ESC";
        case 0x2A: return "BACKSP";
        case 0x2B: return "TAB";
        case 0x2C: return "SPACE";
        case 0x2D: return "-";
        case 0x2E: return "=";
        case 0x4F: return "RIGHT";
        case 0x50: return "LEFT";
        case 0x51: return "DOWN";
        case 0x52: return "UP";
        case 0xE0: return "L CTRL";
        case 0xE1: return "L SHIFT";
        case 0xE2: return "L ALT";
        case 0xE3: return "L WIN";
        case 0xE4: return "R CTRL";
        case 0xE5: return "R SHIFT";
        case 0xE6: return "R ALT";
        default: break;
    }
    _snprintf(s_buf, sizeof(s_buf) - 1, "0x%02X", (unsigned)code);
    s_buf[sizeof(s_buf) - 1] = '\0';
    return s_buf;
}

// Grille des assignations : croise les etapes de l'assistant (liste des
// boutons + leur KeyAction) avec le fichier de configuration (action -> code
// de touche). Le RESULTAT est passe a l'ecran partage ; la mise en page, elle,
// vit dans ui_mapping.cpp.
//
// Les libelles de l'assistant sont ecrits pour une consigne en pleine largeur
// ("L3 (clic stick gauche)") ; dans une colonne de grille ils se font trancher
// en plein milieu. On les coupe donc a la parenthese.
static void BuildGridEntries(std::vector<UiGridEntry>& out,
                             std::vector<std::string>& labelStore,
                             std::vector<std::string>& keyStore) {
    std::vector<AssistantStep> steps = DefaultKeyboardWizardSteps();

    // Relecture a chaque frame : le fichier peut changer sous nos pieds (F9 en
    // jeu, liaison rapide) et la grille doit refleter l'etat reel.
    LoadMappingsFromFile("HDD:\\X360RemapStudio\\X360Remap.json");
    HidDeviceMapping* m = FindMapping(g_selectedKeyboardVid, g_selectedKeyboardPid);

    // Les chaines sont stockees dans labelStore/keyStore, qui VIVENT jusqu'a la
    // fin de la frame : UiGridEntry ne garde que des pointeurs.
    labelStore.clear(); keyStore.clear(); out.clear();
    labelStore.reserve(steps.size());
    keyStore.reserve(steps.size());

    for (size_t i = 0; i < steps.size(); i++) {
        // Libelle COURT, choisi sur l'action et non derive du texte de
        // l'assistant. Celui-ci est ecrit pour une consigne en pleine largeur
        // ("Left stick - Right") : dans une case de grille large de 100 px il
        // se faisait tronquer en "Left stick ...", et sept cases de suite
        // affichaient exactement la meme chose - la grille ne renseignait plus
        // sur rien. Couper a la parenthese ne suffisait pas.
        const char* shortLbl = 0;
        switch (steps[i].action) {
        case KEYACT_A:  shortLbl = "A";  break;
        case KEYACT_B:  shortLbl = "B";  break;
        case KEYACT_X:  shortLbl = "X";  break;
        case KEYACT_Y:  shortLbl = "Y";  break;
        case KEYACT_LB: shortLbl = "LB"; break;
        case KEYACT_RB: shortLbl = "RB"; break;
        case KEYACT_LT: shortLbl = "LT"; break;
        case KEYACT_RT: shortLbl = "RT"; break;
        case KEYACT_L3: shortLbl = "L3"; break;
        case KEYACT_R3: shortLbl = "R3"; break;
        case KEYACT_START: shortLbl = "START"; break;
        case KEYACT_BACK:  shortLbl = "BACK";  break;
        case KEYACT_GUIDE: shortLbl = "XBOX";  break;
        case KEYACT_DPAD_UP:      shortLbl = "D-Up";    break;
        case KEYACT_DPAD_DOWN:    shortLbl = "D-Down";  break;
        case KEYACT_DPAD_LEFT:    shortLbl = "D-Left";  break;
        case KEYACT_DPAD_RIGHT:   shortLbl = "D-Right"; break;
        case KEYACT_LSTICK_UP:    shortLbl = "L-Up";    break;
        case KEYACT_LSTICK_DOWN:  shortLbl = "L-Down";  break;
        case KEYACT_LSTICK_LEFT:  shortLbl = "L-Left";  break;
        case KEYACT_LSTICK_RIGHT: shortLbl = "L-Right"; break;
        case KEYACT_RSTICK_UP:    shortLbl = "R-Up";    break;
        case KEYACT_RSTICK_DOWN:  shortLbl = "R-Down";  break;
        case KEYACT_RSTICK_LEFT:  shortLbl = "R-Left";  break;
        case KEYACT_RSTICK_RIGHT: shortLbl = "R-Right"; break;
        default: break;
        }

        std::string lbl;
        if (shortLbl) {
            lbl = shortLbl;
        } else {
            // Action inconnue (ajout futur) : repli sur le texte de
            // l'assistant, coupe a la parenthese. Rien ne disparait.
            lbl = steps[i].label;
            std::string::size_type par = lbl.find('(');
            if (par != std::string::npos && par > 0) {
                lbl = lbl.substr(0, par);
                while (!lbl.empty() && lbl[lbl.size() - 1] == ' ') lbl.erase(lbl.size() - 1);
            }
        }
        labelStore.push_back(lbl);

        const char* keyLabel = "-";
        if (m) {
            for (uint8_t k = 0; k < m->keyMapCount; k++) {
                if (m->keyMap[k].action == steps[i].action) {
                    keyLabel = HidKeyCodeToLabel(m->keyMap[k].code);
                    break;
                }
            }
        }
        keyStore.push_back(std::string(keyLabel));
    }

    for (size_t i = 0; i < steps.size(); i++) {
        UiGridEntry e;
        e.button = labelStore[i].c_str();
        e.key    = keyStore[i].c_str();
        out.push_back(e);
    }
}

// --- Onglet "Mapping" -------------------------------------------------------
// Ne contient PLUS de mise en page : il remplit le modele avec les vraies
// donnees (peripheriques, textures, assignations) et delegue le dessin a
// DrawUiMapping (ui_mapping.cpp), le meme code que celui que la
// previsualisation affiche hors console. Voir preview/LISEZ-MOI.md pour le
// pourquoi de cette separation.
// --- Mesure de la resolution de la souris (2026-09-23) ---------------------
//
// Retour HB : deux souris a sensibilite maximale donnent des resultats
// differents, et le projet vise des milliers d'utilisateurs avec autant de
// souris. Aucune souris ne communique son DPI par USB : on le MESURE, une
// fois par modele. L'utilisateur glisse la souris sur la largeur d'une
// feuille A4 (21 cm, un objet que tout le monde a) ; les points recus pendant
// ce geste donnent des points par centimetre. Le plugin ramene ensuite cette
// souris a une reference commune (voir kReferenceCountsPerCm).
//
// Les points viennent de input_state.json (mouseDX/mouseDY), deja dedoublonnes
// par releve dans la boucle principale : chaque point n'est compte qu'une fois.
enum MeasureState {
    MEASURE_WAIT_RELEASE,   // le clic qui a ouvert l'ecran est peut-etre encore enfonce
    MEASURE_WAIT_PRESS,
    MEASURE_DRAGGING,
    MEASURE_RESULT
};
static bool         g_measureActive = false;
static MeasureState g_measureState = MEASURE_WAIT_RELEASE;
static int32_t      g_measureSumX = 0;
static int32_t      g_measureSumY = 0;
static float        g_measureResultCpc = 0.0f;
static bool         g_measureImplausible = false;
static bool         g_measureSaveFailed = false;
static const float  kMeasureSheetCm = 21.0f;   // largeur d'une feuille A4
// Seuils qui distinguent une vraie mesure d'un simple clic (CORRIGES le
// 2026-09-23 apres le retour HB : "l'enregistrement se declenche a chaque
// clic"). L'ancien seuil de 100 points valait 1,6 mm avec une souris a
// 1600 DPI : le moindre frottement pendant un clic etait pris pour une mesure.
// Une vraie mesure fait 21 cm - soit au moins ~1600 points meme a 200 DPI -
// et dure bien plus d'une demi-seconde quand on glisse "lentement".
static const int32_t kMeasureMinCounts = 1000;
static const DWORD   kMeasureMinHoldMs = 600;
static DWORD         g_measurePressMs = 0;

static void StartMouseMeasure() {
    g_measureActive = true;
    g_measureState = MEASURE_WAIT_RELEASE;
    g_measureSumX = g_measureSumY = 0;
    g_measureResultCpc = 0.0f;
    g_measureImplausible = false;
    g_measureSaveFailed = false;
    AppLog("Mouse measurement started (%04X:%04X)", g_selectedMouseVid, g_selectedMousePid);
}

static void DrawMeasureOverlay() {
    const InputStateSnapshot& snap = g_lastSnapshot;
    const bool leftHeld = snap.valid && (snap.mouseButtonsMask & 0x01) != 0;

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(Tr(STR_MEASURE_TITLE));
    PopX360Font(g_fontTitle);
    ImGui::Spacing();
    ImGui::TextWrapped("%s", Tr(STR_MEASURE_STEP_SHEET));
    ImGui::Spacing();
    ImGui::TextWrapped("%s", Tr(STR_MEASURE_STEP_DRAG));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", Tr(STR_MEASURE_WHY));
    ImGui::Separator();
    ImGui::Spacing();

    bool close = false;
    switch (g_measureState) {
    case MEASURE_WAIT_RELEASE:
        if (!leftHeld)
            g_measureState = MEASURE_WAIT_PRESS;
        break;

    case MEASURE_WAIT_PRESS:
        if (!snap.valid)
            ImGui::TextWrapped("%s", Tr(STR_MEASURE_NO_MOUSE));
        // Un clic qui COMMENCE sur un bouton (Annuler...) n'est jamais une
        // mesure : c'est l'utilisateur qui clique un bouton.
        if (leftHeld && !ImGui::IsAnyItemHovered()) {
            g_measureSumX = g_measureSumY = 0;
            g_measurePressMs = GetTickCount();
            g_measureState = MEASURE_DRAGGING;
        }
        if (ImGui::Button(Tr(STR_MEASURE_BTN_CANCEL)))
            close = true;
        break;

    case MEASURE_DRAGGING: {
        g_measureSumX += snap.mouseDX;
        g_measureSumY += snap.mouseDY;
        // Geste en ligne droite : la distance parcourue, meme si la feuille
        // n'est pas parfaitement alignee avec l'axe X de la souris.
        const float dist = sqrtf((float)g_measureSumX * (float)g_measureSumX +
                                 (float)g_measureSumY * (float)g_measureSumY);
        ImGui::Text(Tr(STR_MEASURE_LIVE_FMT), (int)dist);
        if (!leftHeld) {
            const DWORD heldMs = GetTickCount() - g_measurePressMs;
            if (dist < (float)kMeasureMinCounts || heldMs < kMeasureMinHoldMs) {
                g_measureState = MEASURE_WAIT_PRESS;   // simple clic : on ignore
            } else {
                g_measureResultCpc = dist / kMeasureSheetCm;
                // Bornes identiques a celles du plugin et de la sauvegarde.
                g_measureImplausible = (g_measureResultCpc < 20.0f || g_measureResultCpc > 10000.0f);
                g_measureState = MEASURE_RESULT;
                AppLog("Mouse measurement: %d counts over %.0f cm -> %.1f counts/cm", (int)dist, kMeasureSheetCm, g_measureResultCpc);
            }
        }
        break;
    }

    case MEASURE_RESULT:
        ImGui::Text(Tr(STR_MEASURE_RESULT_FMT), (int)(g_measureResultCpc + 0.5f),
                    (int)(g_measureResultCpc * 2.54f + 0.5f));
        ImGui::Spacing();
        if (g_measureImplausible)
            ImGui::TextWrapped("%s", Tr(STR_MEASURE_IMPLAUSIBLE));
        if (g_measureSaveFailed)
            ImGui::TextWrapped("%s", Tr(STR_MEASURE_SAVE_FAILED));

        if (!g_measureImplausible) {
            if (ImGui::Button(Tr(STR_MEASURE_BTN_SAVE))) {
                bool ok = ApplyMouseCountsPerCm(g_selectedMouseVid, g_selectedMousePid, g_measureResultCpc);
                AppLog(ok ? "Mouse resolution saved (%.1f counts/cm)" : "FAILED to save mouse resolution (%.1f)",
                       g_measureResultCpc);
                if (ok) {
                    g_mouseCountsPerCm = g_measureResultCpc;
                    close = true;
                } else {
                    g_measureSaveFailed = true;
                }
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(Tr(STR_MEASURE_BTN_RETRY))) {
            g_measureState = MEASURE_WAIT_RELEASE;   // le clic sur ce bouton est encore enfonce
            g_measureSaveFailed = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr(STR_MEASURE_BTN_CANCEL)))
            close = true;
        break;
    }

    if (close) {
        g_measureActive = false;
        AppLog("Mouse measurement closed");
    }
}

// ============================================================================
// Pages glossy "Mesurer la souris" / "Calibrer" (2026-09-25)
//
// Meme logique que DrawMeasureOverlay / DrawCalibrationTab (qui restent pour
// EN_GLOSSY_UI 0), seul le dessin passe par ui_glossy.cpp. Toute correction
// de logique doit etre faite DANS LES DEUX.
// ============================================================================
static void DrawMeasureGlossy(float screenW, float screenH) {
    const InputStateSnapshot& snap = g_lastSnapshot;
    const bool leftHeld = snap.valid && (snap.mouseButtonsMask & 0x01) != 0;
    static GlossyMeasureModel mm;
    mm.reqSave = mm.reqRetry = mm.reqClose = false;
    mm.noMouse = !snap.valid;
    mm.savedDpi = (g_mouseCountsPerCm > 0.0f) ? (int)(g_mouseCountsPerCm * 2.54f + 0.5f) : 0;

    // Etat d'affichage AVANT le dessin (le dessin produit les clics).
    mm.state = (g_measureState == MEASURE_DRAGGING) ? 1 : (g_measureState == MEASURE_RESULT) ? 2 : 0;
    const float dist = sqrtf((float)g_measureSumX * (float)g_measureSumX + (float)g_measureSumY * (float)g_measureSumY);
    mm.liveCounts  = (int)dist;
    mm.resultCpc   = (int)(g_measureResultCpc + 0.5f);
    mm.resultDpi   = (int)(g_measureResultCpc * 2.54f + 0.5f);
    mm.implausible = g_measureImplausible;
    mm.saveFailed  = g_measureSaveFailed;
    DrawUiGlossyMeasure(mm, screenW, screenH);

    bool close = mm.reqClose;
    switch (g_measureState) {
    case MEASURE_WAIT_RELEASE:
        if (!leftHeld) g_measureState = MEASURE_WAIT_PRESS;
        break;
    case MEASURE_WAIT_PRESS:
        // Un clic qui commence sur un bouton n'est jamais une mesure.
        if (leftHeld && !ImGui::IsAnyItemHovered()) {
            g_measureSumX = g_measureSumY = 0;
            g_measurePressMs = GetTickCount();
            g_measureState = MEASURE_DRAGGING;
        }
        break;
    case MEASURE_DRAGGING: {
        g_measureSumX += snap.mouseDX;
        g_measureSumY += snap.mouseDY;
        const float d = sqrtf((float)g_measureSumX * (float)g_measureSumX + (float)g_measureSumY * (float)g_measureSumY);
        if (!leftHeld) {
            const DWORD heldMs = GetTickCount() - g_measurePressMs;
            if (d < (float)kMeasureMinCounts || heldMs < kMeasureMinHoldMs) {
                g_measureState = MEASURE_WAIT_PRESS;
            } else {
                g_measureResultCpc = d / kMeasureSheetCm;
                g_measureImplausible = (g_measureResultCpc < 20.0f || g_measureResultCpc > 10000.0f);
                g_measureState = MEASURE_RESULT;
                AppLog("Mouse measurement: %d counts over %.0f cm -> %.1f counts/cm", (int)d, kMeasureSheetCm, g_measureResultCpc);
            }
        }
        break;
    }
    case MEASURE_RESULT:
        if (mm.reqSave && !g_measureImplausible) {
            bool ok = ApplyMouseCountsPerCm(g_selectedMouseVid, g_selectedMousePid, g_measureResultCpc);
            AppLog(ok ? "Mouse resolution saved (%.1f counts/cm)" : "FAILED to save mouse resolution (%.1f)",
                   g_measureResultCpc);
            if (ok) { g_mouseCountsPerCm = g_measureResultCpc; close = true; }
            else g_measureSaveFailed = true;
        }
        if (mm.reqRetry) {
            g_measureState = MEASURE_WAIT_RELEASE;   // le clic sur ce bouton est encore enfonce
            g_measureSaveFailed = false;
        }
        break;
    }
    if (close) {
        g_measureActive = false;
        AppLog("Mouse measurement closed");
    }
}

static void DrawCalibrationGlossy(float screenW, float screenH) {
    static GlossyCalibModel cm;
    cm.reqStart = cm.reqValidate = cm.reqSkipWheel = cm.reqCancel = cm.reqSave = cm.reqClose = cm.reqClearCalib = false;
    cm.reqFallback = -1;
    cm.mouseVid = g_selectedMouseVid;
    cm.mousePid = g_selectedMousePid;
    {
        std::ifstream fbp("HDD:\\X360RemapStudio\\force_boot_protocol.txt", std::ios::binary);
        char c = 0;
        cm.fallbackActive = fbp.is_open() && fbp.get(c) && c == '1';
    }
    cm.running = (g_calib != nullptr);
    if (g_calib) {
        const InputStateSnapshot& snap = g_lastSnapshot;
        if (snap.valid && snap.rawLen > 0)
            g_calib->Feed(snap.raw, snap.rawLen, snap.tick, snap.rawSeq, snap.hasRawSeq);
        static std::string s_summary;
        s_summary = g_calib->Summary();
        cm.noData = !snap.valid || snap.rawLen == 0;
        cm.prompt = g_calib->Prompt();
        cm.progress = g_calib->Progress();
        cm.canSkip = g_calib->CanSkip();
        cm.finished = g_calib->Finished();
        cm.succeeded = cm.finished && g_calib->Succeeded();
        cm.summary = s_summary.c_str();
        cm.saveAttempted = g_calibSaveAttempted;
        cm.saveOk = g_calibSaveOk;
    }
    DrawUiGlossyCalib(cm, screenW, screenH);

    if (cm.reqStart && !g_calib) {
        g_calib = new CalibrationSession(g_selectedMouseVid, g_selectedMousePid);
        g_calibSaveAttempted = false;
        g_calibSaveOk = false;
        AppLog("Mouse calibration started (%04X:%04X)", g_selectedMouseVid, g_selectedMousePid);
    }
    if (cm.reqValidate && g_calib) g_calib->Advance();
    if (cm.reqSkipWheel && g_calib) g_calib->SkipWheel();
    if (cm.reqCancel && g_calib) { g_calib->Cancel(); AppLog("Mouse calibration canceled"); }
    if (cm.reqSave && g_calib && !g_calibSaveAttempted) {
        g_calibSaveAttempted = true;
        g_calibSaveOk = ApplyMouseReportLayout(g_calib->Vid(), g_calib->Pid(), g_calib->Layout());
        AppLog(g_calibSaveOk ? "Calibration saved (%04X:%04X)" : "FAILED to save calibration (%04X:%04X)",
               g_calib->Vid(), g_calib->Pid());
        XexUtils::Xam::XNotify(g_calibSaveOk ? "Calibration enregistree" : "ECHEC sauvegarde calibration");
    }
    if (cm.reqClose) {
        // Fermer apres une session : retour a l'explication ; fermer la page : retour a la page 1.
        if (g_calib) {
            delete g_calib;
            g_calib = nullptr;
            g_calibSaveAttempted = false;
            g_calibSaveOk = false;
        } else {
            g_calibPageOpen = false;
        }
    }
    if (cm.reqFallback == 1) {
        std::ofstream f("HDD:\\X360RemapStudio\\force_boot_protocol.txt", std::ios::binary | std::ios::trunc);
        bool ok = f.is_open();
        if (ok) f << "1";
        AppLog(ok ? "Fallback mode on - reboot required" : "FAILED to write force_boot_protocol.txt");
        XexUtils::Xam::XNotify(ok ? "Mode de secours - redemarre la console" : "ECHEC - impossible d'ecrire le fichier");
    } else if (cm.reqFallback == 0) {
        bool ok = (std::remove("HDD:\\X360RemapStudio\\force_boot_protocol.txt") == 0);
        AppLog(ok ? "Automatic reading restored (file deleted)" : "Already in automatic reading");
        XexUtils::Xam::XNotify(ok ? "Automatique - redemarre la console" : "Deja en mode automatique");
    }
    if (cm.reqClearCalib) {
        bool ok = ClearMouseReportLayout(g_selectedMouseVid, g_selectedMousePid);
        AppLog(ok ? "Calibration cleared (%04X:%04X)" : "No calibration to clear (%04X:%04X)",
               g_selectedMouseVid, g_selectedMousePid);
        XexUtils::Xam::XNotify(ok ? "Calibration effacee" : "Aucune calibration enregistree");
    }
}

// Branche = present dans la liste "devs" du plugin. Plugin plus ancien (pas de
// liste) : ancien critere, "selectionne ET plugin vivant".
static bool IsDevicePlugged(char type, uint16_t vid, uint16_t pid, uint16_t selVid, uint16_t selPid) {
    if (!g_lastSnapshot.valid) return false;
    if (!g_lastSnapshot.hasDevs) return vid == selVid && pid == selPid;
    for (int i = 0; i < g_lastSnapshot.devCount; i++)
        if (g_lastSnapshot.devType[i] == type && g_lastSnapshot.devVid[i] == vid && g_lastSnapshot.devPid[i] == pid)
            return true;
    return false;
}

// ============================================================================
// PAGE PROFILS DE JEUX glossy (2026-09-25) : bibliotheque Aurora + profils
// ============================================================================
struct GameEntry {
    uint32_t           tid;
    std::string        name;        // nom AFFICHE (nom Aurora, ou nom donne par l'utilisateur)
    std::string        auroraName;  // nom exact d'Aurora, vide si inconnu
    std::string        assetPath;   // ...\GC<TID>.asset, vide si inconnu
    IDirect3DTexture9* tex;
    int                w, h;
    bool               tried;       // pochette deja tentee (reussie ou non)
};
static std::vector<GameEntry> g_games;
static bool        g_gamesBuilt = false;
static bool        g_auroraFound = false;
static std::string g_auroraRoot;
static IDirect3DTexture9* g_bigCover = nullptr;
static int         g_bigW = 0, g_bigH = 0;
static uint32_t    g_bigTid = 0;

static bool FileExistsA(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const std::streamoff size = f.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    f.read((char*)&out[0], size);
    return !f.fail();
}

// Sous-dossiers d'un dossier (noms seuls).
static std::vector<std::string> ListSubdirs(const std::string& dir) {
    std::vector<std::string> r;
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return r;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
            r.push_back(fd.cFileName);
    } while (FindNextFile(h, &fd));
    FindClose(h);
    return r;
}

// Aurora n'a pas d'emplacement fixe. CORRIGE le 2026-09-25 : chez HB, Aurora
// est dans HDD:\App\Aurora (capture "DeviceHarddisk0Partition1AppAurora...") -
// dossier "App" et non "Apps", absent de la premiere liste : la page restait
// sans noms ni pochettes. On essaie les chemins connus, puis on parcourt DEUX
// niveaux de sous-dossiers de HDD:\ a la recherche de Data\Databases\content.db.
static bool IsAuroraRoot(const std::string& dir) {
    return FileExistsA((dir + "\\Data\\Databases\\content.db").c_str());
}
static bool FindAuroraRoot(std::string& root) {
    // 1) Le plus direct (idee HB) : le tableau de bord que DashLaunch lance,
    //    lu dans son launch.ini. Verifie ensuite comme les autres candidats.
    {
        const char* inis[] = { "HDD:\\launch.ini", "HDD:\\DashLaunch\\launch.ini" };
        for (size_t i = 0; i < sizeof(inis) / sizeof(inis[0]); i++) {
            std::vector<unsigned char> raw;
            std::string dir;
            if (ReadWholeFile(inis[i], raw) && !raw.empty() &&
                DashboardDirFromLaunchIni(std::string(raw.begin(), raw.end()), dir)) {
                if (IsAuroraRoot(dir)) { root = dir; AppLog("Aurora found via %s", inis[i]); return true; }
                AppLog("%s: dashboard %s (no Data\\Databases\\content.db)", inis[i], dir.c_str());
            }
        }
    }
    // 2) Repli : chemins connus, puis deux niveaux de dossiers.
    const char* known[] = { "HDD:\\App\\Aurora", "HDD:\\Apps\\Aurora", "HDD:\\Aurora",
                            "HDD:\\Aurora0.7b", "HDD:\\Dashboards\\Aurora", "HDD:\\Homebrew\\Aurora" };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (IsAuroraRoot(known[i])) { root = known[i]; return true; }
    std::vector<std::string> lvl1 = ListSubdirs("HDD:");
    for (size_t i = 0; i < lvl1.size(); i++) {
        const std::string a = std::string("HDD:\\") + lvl1[i];
        if (IsAuroraRoot(a)) { root = a; return true; }
        std::vector<std::string> lvl2 = ListSubdirs(a);
        for (size_t k = 0; k < lvl2.size(); k++) {
            const std::string b = a + "\\" + lvl2[k];
            if (IsAuroraRoot(b)) { root = b; return true; }
        }
    }
    return false;
}

static bool NameLess(const GameEntry& a, const GameEntry& b) {
    return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
}

static void FreeGameCovers() {
    for (size_t i = 0; i < g_games.size(); i++)
        if (g_games[i].tex) { g_games[i].tex->Release(); g_games[i].tex = nullptr; }
    if (g_bigCover) { g_bigCover->Release(); g_bigCover = nullptr; g_bigTid = 0; }
}

// Construit la liste : jeux d'Aurora (noms exacts) + jeux vus par le plugin
// + jeux qui ont deja un profil. Une seule fois (bouton "Actualiser" : a venir).
static void BuildGameLibrary() {
    g_gamesBuilt = true;
    FreeGameCovers();
    g_games.clear();
    std::vector<AuroraTitle> titles;
    g_auroraFound = FindAuroraRoot(g_auroraRoot);
    if (g_auroraFound) {
        std::vector<unsigned char> db;
        if (ReadWholeFile(g_auroraRoot + "\\Data\\Databases\\content.db", db) && !db.empty())
            ReadAuroraTitles(&db[0], db.size(), titles);
        AppLog("Aurora found: %s (%d games in content.db)", g_auroraRoot.c_str(), (int)titles.size());
    } else {
        AppLog("Aurora not found (HDD:\\App\\Aurora, HDD:\\Apps\\Aurora, HDD:\\Aurora... and 2 levels of HDD:) - library limited to games seen by the plugin");
    }
    for (size_t i = 0; i < titles.size(); i++) {
        GameEntry e; e.tid = titles[i].titleId; e.name = titles[i].name; e.auroraName = titles[i].name; e.tex = nullptr; e.w = e.h = 0; e.tried = false;
        g_games.push_back(e);
    }
    std::vector<uint32_t> seen = ListKnownTitleIds();
    LoadProfilesOnce();
    for (size_t i = 0; i < g_profiles.size(); i++) seen.push_back(g_profiles[i].titleId);
    for (size_t i = 0; i < seen.size(); i++) {
        bool have = false;
        for (size_t k = 0; k < g_games.size() && !have; k++) have = g_games[k].tid == seen[i];
        // Title ID systeme (0xFFFE..../0xFFFF.... : tableaux de bord, Aurora,
        // outils) : pas des jeux, on ne les montre que s'ils ont deja un profil.
        const bool system = (seen[i] >> 16) >= 0xFFFE;
        bool hasProfile = false;
        for (size_t k = 0; k < g_profiles.size(); k++) hasProfile = hasProfile || g_profiles[k].titleId == seen[i];
        if (have || seen[i] == 0 || (system && !hasProfile)) continue;
        GameEntry e; e.tid = seen[i]; e.tex = nullptr; e.w = e.h = 0; e.tried = false;
        const char* pn = FindKnownGameName(seen[i]);
        char buf[48];
        if (pn[0]) e.name = pn;
        else { _snprintf(buf, sizeof(buf) - 1, "Title ID %08X", (unsigned)seen[i]); buf[sizeof(buf) - 1] = 0; e.name = buf; }
        g_games.push_back(e);
    }
    // pochettes : Data\GameData\<TID>_...\GC<TID>.asset
    if (g_auroraFound) {
        const std::string gd = g_auroraRoot + "\\Data\\GameData";
        std::vector<std::string> dirs = ListSubdirs(gd);
        for (size_t d = 0; d < dirs.size(); d++) {
            if (dirs[d].size() < 8) continue;
            const uint32_t tid = (uint32_t)strtoul(dirs[d].substr(0, 8).c_str(), nullptr, 16);
            for (size_t k = 0; k < g_games.size(); k++) {
                if (g_games[k].tid != tid || !g_games[k].assetPath.empty()) continue;
                char f[32];
                _snprintf(f, sizeof(f) - 1, "\\GC%08X.asset", (unsigned)tid); f[sizeof(f) - 1] = 0;
                g_games[k].assetPath = gd + "\\" + dirs[d] + f;
            }
        }
    }
    std::sort(g_games.begin(), g_games.end(), NameLess);
}

static IDirect3DTexture9* LoadCover(const std::string& path, int maxW, int maxH, int& w, int& h, uint32_t tid) {
    w = h = 0;
    if (!g_device) return nullptr;
    if (path.empty()) {
        // Repli : pochette deposee par l'utilisateur, HDD:\X360RemapStudio\covers\<TID>.png
        char p[80];
        _snprintf(p, sizeof(p) - 1, "HDD:\\X360RemapStudio\\covers\\%08X.png", (unsigned)tid); p[sizeof(p) - 1] = 0;
        IDirect3DTexture9* t = nullptr;
        if (LoadTextureFromFile(g_device, p, &t, &w, &h)) return t;
        return nullptr;
    }
    std::vector<unsigned char> bytes, rgba;
    if (!ReadWholeFile(path, bytes) || bytes.empty()) return nullptr;
    if (!DecodeAuroraAsset(&bytes[0], bytes.size(), AURORA_ASSET_BOXART, maxW, maxH, rgba, w, h, true)) return nullptr;   // recto seul
    IDirect3DTexture9* tex = nullptr;
    if (!LoadTextureFromRGBA(g_device, &rgba[0], w, h, &tex)) return nullptr;
    return tex;
}

static void DrawGamesGlossy(int& activeTab, float screenW, float screenH) {
    if (!g_gamesBuilt) BuildGameLibrary();
    LoadProfilesOnce();
    static GlossyGamesModel gm;
    static std::vector<GlossyGame> s_view;

    // profils : relus si la souris a change (g_profilesLoaded remis a false ailleurs)
    // Nom affiche : celui donne par l'utilisateur (profil) s'il l'a change,
    // sinon le nom exact d'Aurora, sinon "Title ID ...". Un nom de profil qui
    // n'est que le debut du nom Aurora (coupe a 31 caracteres a l'enregistrement)
    // n'est pas un renommage : on garde le nom Aurora complet.
    for (size_t i = 0; i < g_games.size(); i++) {
        GameEntry& e = g_games[i];
        const char* pn = FindKnownGameName(e.tid);
        const bool prefix = pn[0] && !e.auroraName.empty() && e.auroraName.compare(0, strlen(pn), pn) == 0;
        if (pn[0] && !prefix) e.name = pn;
        else if (!e.auroraName.empty()) e.name = e.auroraName;
        else if (pn[0]) e.name = pn;
    }
    s_view.resize(g_games.size());
    for (size_t i = 0; i < g_games.size(); i++) {
        GlossyGame& v = s_view[i];
        v.titleId = g_games[i].tid;
        v.name = g_games[i].name.c_str();
        v.cover = (ImTextureID)g_games[i].tex;
        v.coverW = g_games[i].w; v.coverH = g_games[i].h;
        v.hasProfile = false;
        for (size_t k = 0; k < g_profiles.size(); k++)
            if (g_profiles[k].titleId == v.titleId) { v.hasProfile = true; break; }
    }
    gm.games = s_view.empty() ? 0 : &s_view[0];
    gm.gameCount = (int)s_view.size();
    gm.auroraFound = g_auroraFound;
    static char s_mouse[64];
    s_mouse[0] = 0;
    for (size_t i = 0; i < g_knownDevices.size(); i++)
        if (g_knownDevices[i].vid == g_selectedMouseVid && g_knownDevices[i].pid == g_selectedMousePid)
            FormatDeviceLabel(s_mouse, sizeof(s_mouse), g_knownDevices[i]);
    gm.mouseName = s_mouse[0] ? s_mouse : nullptr;
    gm.sensitivity = &g_profileSensitivity;
    gm.deadzone = &g_profileDeadzone;
    gm.antiDeadzone = &g_profileAntiDeadzone;
    gm.stickLayout = &g_profileStickLayout;
    gm.antiDeadzoneY = &g_profileAntiDeadzoneY;
    gm.smoothing = &g_profileSmoothing;
    gm.invertY = &g_profileInvertY;
    gm.curveType = &g_profileCurveTypeIdx;
    gm.curveExp = &g_profileCurveExponent;
    if (gm.selected >= gm.gameCount) gm.selected = -1;
    const bool sel = gm.selected >= 0;
    gm.bigCover = (sel && g_bigTid == s_view[gm.selected].titleId) ? (ImTextureID)g_bigCover : 0;
    gm.bigW = g_bigW; gm.bigH = g_bigH;

    // Renommage au clavier (meme canal mono-touche que l'ancienne page Profils :
    // input_state.json ne transmet qu'une touche a la fois). Entree = valider,
    // Echap = annuler, Retour arriere = effacer.
    static bool s_editing = false;
    static bool s_commit = false;
    static char s_nameBuf[32] = "";
    static int32_t s_lastKey = -1;
    if (s_editing && gm.selected >= 0) {
        const int32_t code = g_lastSnapshot.valid ? g_lastSnapshot.keyCode : -1;
        if (code != s_lastKey && code != -1) {
            size_t len = strlen(s_nameBuf);
            if (code == 0x28 || code == 0x58) {                 // Entree : enregistrer
                s_editing = false;
                s_commit = s_nameBuf[0] != 0;
            } else if (code == 0x29) {                            // Echap : annuler
                s_editing = false;
            } else if (code == 42) {
                if (len > 0) s_nameBuf[len - 1] = 0;
            } else {
                char ch = HidKeyCodeToAsciiChar(code);
                if (ch && len < sizeof(s_nameBuf) - 1) {
                    if (ch >= 'a' && ch <= 'z' && (len == 0 || s_nameBuf[len - 1] == ' ')) ch = (char)(ch - 32);
                    s_nameBuf[len] = ch; s_nameBuf[len + 1] = 0;
                }
            }
        }
        s_lastKey = code;
    }
    gm.nameEditing = s_editing;
    gm.nameBuf = s_nameBuf;

    DrawUiGlossyGames(gm, activeTab, screenW, screenH);

    if (gm.reqEditName && gm.selected >= 0 && !s_editing) {
        s_editing = true;
        s_lastKey = g_lastSnapshot.valid ? g_lastSnapshot.keyCode : -1;
        _snprintf(s_nameBuf, sizeof(s_nameBuf) - 1, "%s", g_games[gm.selected].name.c_str());
        s_nameBuf[sizeof(s_nameBuf) - 1] = 0;
    }
    if (gm.reqSelect) s_editing = false;

    // --- pochettes a la demande : une par image au plus, cases visibles d'abord
    int loaded = 0;
    for (size_t i = 0; i < g_games.size(); i++) if (g_games[i].tex) loaded++;
    bool didOne = false;
    for (int i = gm.firstVisible; i <= gm.lastVisible && i < (int)g_games.size() && !didOne; i++) {
        GameEntry& e = g_games[i];
        if (e.tried) continue;
        e.tried = true;
        e.tex = LoadCover(e.assetPath, 128, 176, e.w, e.h, e.tid);
        didOne = true;
    }
    if (loaded > 60) {                         // memoire : on libere ce qui n'est plus visible
        for (int i = 0; i < (int)g_games.size(); i++) {
            if ((i < gm.firstVisible || i > gm.lastVisible) && i != gm.selected && g_games[i].tex) {
                g_games[i].tex->Release(); g_games[i].tex = nullptr; g_games[i].tried = false;
            }
        }
    }
    // grande pochette du jeu selectionne
    if (gm.selected >= 0 && g_bigTid != g_games[gm.selected].tid && !didOne) {
        if (g_bigCover) { g_bigCover->Release(); g_bigCover = nullptr; }
        g_bigTid = g_games[gm.selected].tid;
        g_bigCover = LoadCover(g_games[gm.selected].assetPath, 300, 410, g_bigW, g_bigH, g_games[gm.selected].tid);
    }

    // --- actions
    if (gm.reqSelect && gm.selected >= 0) {
        const uint32_t tid = g_games[gm.selected].tid;
        bool found = false;
        for (size_t k = 0; k < g_profiles.size(); k++)
            if (g_profiles[k].titleId == tid) { LoadProfileIntoEditor(g_profiles[k]); found = true; break; }
        if (!found) {
            ResetProfileEditorForNew();
            _snprintf(g_profileTitleIdHex, sizeof(g_profileTitleIdHex) - 1, "%08X", (unsigned)tid);
            g_profileTitleIdHex[sizeof(g_profileTitleIdHex) - 1] = 0;
        }
    }
    g_gamesRenaming = s_editing;
    const bool validatedRename = s_commit;
    s_commit = false;
    if ((gm.reqSave || validatedRename) && gm.selected >= 0) {
        const GameEntry& e = g_games[gm.selected];
        char name[32];
        _snprintf(name, sizeof(name) - 1, "%s", validatedRename ? s_nameBuf : e.name.c_str()); name[sizeof(name) - 1] = 0;   // champ limite a 31 caracteres
        const uint8_t curveType = (g_profileCurveTypeIdx == 1) ? CURVE_EXPONENTIAL : CURVE_LINEAR;
        bool ok = ApplyMouseProfile(g_selectedMouseVid, g_selectedMousePid, e.tid,
            g_profileSensitivity, g_profileInvertY, g_profileDeadzone, curveType, g_profileCurveExponent, name,
            g_profileAntiDeadzone, g_profileStickLayout, g_profileAntiDeadzoneY, g_profileSmoothing);
        AppLog(ok ? "Profile %08X saved (%s)" : "FAILED to save profile %08X (%s)", (unsigned)e.tid, name);
        // Pas de notification a chaque curseur relache (enregistrement
        // automatique) : un retour discret dans le panneau suffit.
        if (!ok || gm.reqSaveExplicit) XexUtils::Xam::XNotify(ok ? "Profil enregistre" : "ECHEC sauvegarde du profil");
        gm.savedAt = ImGui::GetTime();
        gm.savedOk = ok;
        if (ok) RefreshProfiles();
    }
    if (gm.reqDeleteAll) {
        // "Supprimer tous les profils" (confirme deux fois cote page).
        int n = 0;
        std::vector<MouseProfileInfo> all = g_profiles;
        for (size_t k = 0; k < all.size(); k++)
            if (RemoveMouseProfile(g_selectedMouseVid, g_selectedMousePid, all[k].titleId)) n++;
        AppLog("%d profile(s) deleted for %04X:%04X", n, g_selectedMouseVid, g_selectedMousePid);
        XexUtils::Xam::XNotify(n ? "Profils supprimes" : "Aucun profil a supprimer");
        RefreshProfiles();
        ResetProfileEditorForNew();
    }
    if (gm.reqDelete && gm.selected >= 0) {
        const uint32_t tid = g_games[gm.selected].tid;
        bool ok = RemoveMouseProfile(g_selectedMouseVid, g_selectedMousePid, tid);
        AppLog(ok ? "Profile %08X deleted" : "FAILED to delete profile %08X", (unsigned)tid);
        XexUtils::Xam::XNotify(ok ? "Profil supprime" : "ECHEC suppression du profil");
        if (ok) { RefreshProfiles(); ResetProfileEditorForNew(); }
    }
}

static void DrawMappingTab() {
    LoadDevicesOnce();

    // Actualisation AUTOMATIQUE des peripheriques (2026-09-25) : si le plugin
    // signale un appareil branche que la liste ne connait pas encore, on la
    // relit - plus besoin du bouton "Actualiser" pour voir un clavier qu'on
    // vient de brancher. Au plus une fois toutes les 10 secondes.
    {
        static DWORD s_lastAutoRefresh = 0;
        const DWORD nowMs = GetTickCount();
        if (g_lastSnapshot.valid && g_lastSnapshot.hasDevs && nowMs - s_lastAutoRefresh > 10000) {
            bool unknown = false;
            for (int i = 0; i < g_lastSnapshot.devCount && !unknown; i++) {
                bool found = false;
                for (size_t k = 0; k < g_knownDevices.size() && !found; k++)
                    found = g_knownDevices[k].vid == g_lastSnapshot.devVid[i] && g_knownDevices[k].pid == g_lastSnapshot.devPid[i];
                unknown = !found;
            }
            if (unknown) {
                s_lastAutoRefresh = nowMs;
                RefreshKnownDevices();
                AppLog("Plugged device detected - list refreshed");
            }
        }
    }
    LoadMouseSettingsOnce();

    // Tampons persistants : le modele ne garde que des pointeurs, ils doivent
    // survivre jusqu'a la fin du dessin. Statiques plutot que locaux pour
    // eviter une reallocation a chaque frame.
    static std::vector<UiDeviceEntry> s_kb, s_ms;
    static std::vector<std::string>   s_kbName, s_kbId, s_msName, s_msId;
    static std::vector<UiGridEntry>   s_grid;
    static std::vector<std::string>   s_gridLabels, s_gridKeys;
    static UiMappingModel             s_model;

    // --- Peripheriques ---
    s_kb.clear(); s_kbName.clear(); s_kbId.clear();
    s_ms.clear(); s_msName.clear(); s_msId.clear();

    char buf[64];
    for (size_t i = 0; i < g_keyboardDeviceIndices.size(); i++) {
        const KnownDeviceInfo& d = g_knownDevices[g_keyboardDeviceIndices[i]];
        FormatDeviceLabel(buf, sizeof(buf), d);
        s_kbName.push_back(std::string(buf));
        _snprintf(buf, sizeof(buf) - 1, "%04X:%04X", d.vid, d.pid);
        buf[sizeof(buf) - 1] = '\0';
        s_kbId.push_back(std::string(buf));
    }
    for (size_t i = 0; i < g_mouseDeviceIndices.size(); i++) {
        const KnownDeviceInfo& d = g_knownDevices[g_mouseDeviceIndices[i]];
        FormatDeviceLabel(buf, sizeof(buf), d);
        s_msName.push_back(std::string(buf));
        _snprintf(buf, sizeof(buf) - 1, "%04X:%04X", d.vid, d.pid);
        buf[sizeof(buf) - 1] = '\0';
        s_msId.push_back(std::string(buf));
    }
    for (size_t i = 0; i < s_kbName.size(); i++) {
        UiDeviceEntry e;
        e.name = s_kbName[i].c_str();
        e.id   = s_kbId[i].c_str();
        // "Actif" = BRANCHE (point vert). Depuis le 2026-09-25 le plugin liste
        // les peripheriques reellement connectes (champ "devs") ; avant, on ne
        // pouvait que supposer, et un clavier debranche restait vert des que
        // la souris, elle, faisait vivre input_state.json.
        e.active = IsDevicePlugged('k', g_knownDevices[g_keyboardDeviceIndices[i]].vid,
                                   g_knownDevices[g_keyboardDeviceIndices[i]].pid,
                                   g_selectedKeyboardVid, g_selectedKeyboardPid);
        s_kb.push_back(e);
    }
    for (size_t i = 0; i < s_msName.size(); i++) {
        UiDeviceEntry e;
        e.name = s_msName[i].c_str();
        e.id   = s_msId[i].c_str();
        e.active = IsDevicePlugged('m', g_knownDevices[g_mouseDeviceIndices[i]].vid,
                                   g_knownDevices[g_mouseDeviceIndices[i]].pid,
                                   g_selectedMouseVid, g_selectedMousePid);
        s_ms.push_back(e);
    }

    BuildGridEntries(s_grid, s_gridLabels, s_gridKeys);

    // --- Remplissage du modele ---
    s_model.mode      = (int)g_mappingMode;
    // 0=FR, 1=EN, 2=PT, 3=ES - voir ui_mapping.h (langIndex) et les puces
    // FR/EN/PT/ES dans ui_mapping.cpp (DrawUiMapping). PT ajoute le
    // 2026-08-24, ES le 2026-09-13.
    //
    // Cette conversion et son inverse (plus bas, "Lang wanted") doivent
    // TOUJOURS etre modifiees ensemble : n'en mettre a jour qu'une seule
    // donne une langue qui s'affiche mais ne se selectionne pas, ou
    // l'inverse.
    s_model.langIndex = (GetLanguage() == LANG_EN) ? 1
                       : (GetLanguage() == LANG_PT_BR) ? 2
                       : (GetLanguage() == LANG_ES) ? 3
                       : 0;

    s_model.keyboards     = s_kb.empty() ? 0 : &s_kb[0];
    s_model.keyboardCount = (int)s_kb.size();
    s_model.mice          = s_ms.empty() ? 0 : &s_ms[0];
    s_model.mouseCount    = (int)s_ms.size();
    s_model.detectedCount = (int)(s_kb.size() + s_ms.size());
    s_model.pluginAlive   = g_lastSnapshot.valid;

    s_model.texKeyboardSide = (ImTextureID)g_texManette;
    s_model.texKbW = g_texManetteW; s_model.texKbH = g_texManetteH;
    s_model.texMouseSide    = (ImTextureID)g_texSouris;
    s_model.texMsW = g_texSourisW;  s_model.texMsH = g_texSourisH;

    // Le panneau contextuel n'est PLUS alimente d'ici : il derive de
    // s_model.selection, que l'ecran met a jour quand l'utilisateur clique une
    // case ou une ligne de reglage. On poussait auparavant "BOUTON A" en dur,
    // donc l'application affichait en permanence un reglage que personne
    // n'avait demande (remarque de HB, 2026-08-14).

    s_model.grid      = s_grid.empty() ? 0 : &s_grid[0];
    s_model.gridCount = (int)s_grid.size();

    s_model.sensitivity = &g_mouseSensitivity;
    s_model.sensitivityMin = 1000; s_model.sensitivityMax = 30000;
    s_model.deadzone = &g_mouseDeadzone;
    s_model.deadzoneMin = 0; s_model.deadzoneMax = 10000;
    s_model.antiDeadzone = &g_mouseAntiDeadzone;
    s_model.antiDeadzoneMin = 0; s_model.antiDeadzoneMax = 12000;
    s_model.antiDeadzoneY = &g_mouseAntiDeadzoneY;
    s_model.smoothing = &g_mouseSmoothing; s_model.smoothingMax = 100;
    s_model.invertY = &g_mouseInvertY;
    s_model.mouseDpiMeasured = (g_mouseCountsPerCm > 0.0f) ? (int)(g_mouseCountsPerCm * 2.54f + 0.5f) : 0;

    // --- Mapping des boutons souris ---
    // Les valeurs sont chargees depuis le fichier au premier passage (et a
    // chaque changement de souris) : sans ca la page afficherait "Par defaut"
    // partout alors que le plugin applique tout autre chose.
    LoadMouseSettingsOnce();

    // Libelles TRADUITS reconstruits a chaque frame : Tr() renvoie un pointeur
    // sur la table de la langue COURANTE, qui change quand l'utilisateur clique
    // sur un drapeau. Les figer une seule fois laisserait la liste en francais
    // apres un passage a l'anglais.
    static std::vector<const char*> s_targets;
    s_targets.clear();
    s_targets.reserve(kMouseButtonTargetCount);
    for (int i = 0; i < kMouseButtonTargetCount; i++)
        s_targets.push_back(Tr(kMouseButtonTargets[i].labelId));
    s_model.buttonTargets     = s_targets.empty() ? 0 : &s_targets[0];
    s_model.buttonTargetCount = (int)s_targets.size();
    s_model.btnLeft       = &g_mouseBtnLeftSel;
    s_model.btnRight      = &g_mouseBtnRightSel;
    s_model.btnMiddle     = &g_mouseBtnMiddleSel;
    s_model.wheelForward  = &g_mouseWheelForwardSel;
    s_model.wheelBackward = &g_mouseWheelBackwardSel;
    s_model.buttonTargetBadge = kMouseButtonTargetBadge;
    s_model.appVersion        = kAppVersion;
    {
        // Relu une fois par seconde environ : c'est une ouverture de fichier.
        static int s_defCheck = 0;
        static bool s_hasDef = false;
        if (s_defCheck-- <= 0) { s_hasDef = HasConfigDefault(); s_defCheck = 60; }
        s_model.hasSavedDefault = s_hasDef;
    }

    // Etat du mode de secours, relu depuis le disque : c'est le plugin qui le
    // consomme au demarrage, l'application n'en garde aucune copie en memoire.
    {
        std::ifstream fbp("HDD:\\X360RemapStudio\\force_boot_protocol.txt", std::ios::binary);
        bool actif = false;
        if (fbp.is_open()) { char c = 0; fbp.get(c); actif = (c == '1'); }
        s_model.fallbackActive = actif;
    }

    // --- Assistant clavier en direct (2026-08-17) ---
    // Doit etre rempli AVANT DrawUiMapping : c'est ce que la grille lit pour
    // savoir quelle case surligner (voir UpdateWizardModel plus haut).
    UpdateWizardModel(s_model);
    UpdateManualAssignModel(s_model);

    // --- Dessin (code partage avec la previsualisation) ---
    if (GlossyActive()) {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        DrawUiGlossyMapping(s_model, g_glossyTabOut, ds.x, ds.y);
    } else {
        DrawUiMapping(s_model);
    }

    // --- Retour des interactions ---
    g_mappingMode = (s_model.mode == 1) ? MAPPING_MOUSE : MAPPING_KEYBOARD;

    Lang wanted = (s_model.langIndex == 1) ? LANG_EN
                : (s_model.langIndex == 2) ? LANG_PT_BR
                : (s_model.langIndex == 3) ? LANG_ES
                : LANG_FR;
    if (wanted != GetLanguage())
        SaveLanguagePref(wanted);

    if (s_model.keyboardIndex >= 0 && s_model.keyboardIndex < (int)g_keyboardDeviceIndices.size()) {
        const KnownDeviceInfo& d = g_knownDevices[g_keyboardDeviceIndices[s_model.keyboardIndex]];
        g_selectedKeyboardVid = d.vid;
        g_selectedKeyboardPid = d.pid;
    }
    if (s_model.mouseIndex >= 0 && s_model.mouseIndex < (int)g_mouseDeviceIndices.size()) {
        const KnownDeviceInfo& d = g_knownDevices[g_mouseDeviceIndices[s_model.mouseIndex]];
        if (d.vid != g_selectedMouseVid || d.pid != g_selectedMousePid) {
            g_selectedMouseVid = d.vid;
            g_selectedMousePid = d.pid;
            // Force le rechargement des ecrans qui mettent en cache l'etat du
            // device precedent - sans ca ils afficheraient l'ANCIENNE souris.
            g_mouseSettingsLoaded = false;
            g_profilesLoaded = false;
        }
    }

    if (s_model.requestRefreshDevices) {
        s_model.requestRefreshDevices = false;
        RefreshKnownDevices();
    }
    if (s_model.requestStartWizard) {
        s_model.requestStartWizard = false;
        // 180 = 60fps * 3s, PAS le defaut de 60 (calibre pour l'ancien
        // assistant manette a 50ms/tick, voir wizard_session.h) - avec 60
        // ici, une etape se faisait sauter au bout d'~1s au lieu des
        // "3 secondes" annoncees, ce qui pouvait vider silencieusement le
        // wizard entier si chaque touche etait relachee un peu tard (bug
        // reel du 2026-08-02). Ne bascule plus en superposition plein ecran
        // depuis le 2026-08-17 : g_wizard != nullptr fait juste apparaitre
        // le bloc de statut sur CETTE page (voir UpdateWizardModel).
        std::vector<AssistantStep> steps = DefaultKeyboardWizardSteps();
        g_wizard = new WizardSession(g_selectedKeyboardVid, g_selectedKeyboardPid, steps, 180);
        AppLog("Keyboard wizard started (%04X:%04X, %d steps)", g_selectedKeyboardVid, g_selectedKeyboardPid, (int)steps.size());
        // Le bouton "Reglages manuels" reste visible a cote de "Demarrer" -
        // par precaution, on desactive quand meme la bascule manuelle ici
        // pour qu'un clic de grille pendant le wizard ne puisse jamais partir
        // dans les deux mecanismes a la fois (voir aussi le garde-fou
        // !wizardActive dans ui_mapping.cpp).
        g_manualMode = false;
    }
    if (s_model.requestToggleManualMode) {
        s_model.requestToggleManualMode = false;
        g_manualMode = !g_manualMode;
        // Annule toute capture en cours en quittant le mode - eviter une
        // session orpheline qui continuerait a consommer g_lastSnapshot en
        // arriere-plan alors que l'ecran n'affiche plus rien a son sujet.
        if (!g_manualMode && g_manualSession) {
            delete g_manualSession;
            g_manualSession = nullptr;
            g_manualCaptureIndex = -1;
        }
    }
    if (s_model.requestManualAssignIndex >= 0) {
        const int idx = s_model.requestManualAssignIndex;
        s_model.requestManualAssignIndex = -1;
        // Un seul AssistantStep - meme table que le wizard sequentiel
        // (DefaultKeyboardWizardSteps), juste reduite a la case cliquee, pour
        // que la case n5 du wizard et la case n5 des reglages manuels
        // demandent EXACTEMENT le meme bouton (aucune table dupliquee a
        // faire diverger).
        std::vector<AssistantStep> allSteps = DefaultKeyboardWizardSteps();
        if (!g_manualSession && idx >= 0 && idx < (int)allSteps.size()) {
            std::vector<AssistantStep> oneStep;
            oneStep.push_back(allSteps[idx]);
            g_manualSession = new WizardSession(g_selectedKeyboardVid, g_selectedKeyboardPid, oneStep, 180);
            g_manualCaptureIndex = idx;
            AppLog("Manual binding started (%04X:%04X, cell %d)", g_selectedKeyboardVid, g_selectedKeyboardPid, idx);
        }
    }
    if (s_model.requestWizardClose) {
        // Clic sur "OK" apres un wizard termine ou annule (voir le bloc
        // "##stagel" de ui_mapping.cpp) - remet tout a plat pour la
        // prochaine session, meme reset qu'avant le passage en page unique.
        s_model.requestWizardClose = false;
        delete g_wizard;
        g_wizard = nullptr;
        g_wizardSaveAttempted = false;
        g_wizardSaveOk = false;
    }
    if (s_model.requestSaveMouse) {
        s_model.requestSaveMouse = false;
        bool ok = ApplyMouseSettings(g_selectedMouseVid, g_selectedMousePid,
                                     g_mouseSensitivity, g_mouseInvertY, g_mouseDeadzone,
                                     (int32_t)g_mouseAntiDeadzone, (int32_t)g_mouseAntiDeadzoneY,
                                     (int32_t)g_mouseSmoothing);
        AppLog(ok ? "Mouse settings saved (sensitivity=%d deadzone=%d invertY=%d antiDeadzone=%d)"
                  : "FAILED to save mouse settings",
               g_mouseSensitivity, g_mouseDeadzone, g_mouseInvertY ? 1 : 0, g_mouseAntiDeadzone);

        // Le mapping des boutons part avec, dans le MEME enregistrement.
        // L'ancienne page avait deux boutons "Enregistrer" distincts et il
        // etait facile de regler ses clics puis de quitter sans les garder.
        bool okBtn = SaveMouseButtonBindings();

        XexUtils::Xam::XNotify((ok && okBtn) ? "Reglages souris enregistres"
                                             : "ECHEC sauvegarde reglages souris");
    }
    if (s_model.requestSaveButtons) {
        s_model.requestSaveButtons = false;
        bool ok = SaveMouseButtonBindings();
        XexUtils::Xam::XNotify(ok ? "Boutons souris enregistres" : "ECHEC sauvegarde boutons souris");
    }
    // Reglages par defaut (page glossy, 2026-09-25)
    if (s_model.requestSaveDefault) {
        s_model.requestSaveDefault = false;
        bool ok = SaveConfigAsDefault();
        AppLog(ok ? "Current settings saved as default" : "FAILED to save default settings");
        XexUtils::Xam::XNotify(ok ? "Reglages actuels enregistres comme defaut" : "Echec sauvegarde des reglages par defaut");
    }
    if (s_model.requestRestoreDefault) {
        s_model.requestRestoreDefault = false;
        const bool hadDefault = HasConfigDefault();
        bool ok = hadDefault ? RestoreConfigDefault() : RestoreFactoryConfig();
        if (ok) {
            g_mouseSettingsLoaded = false;   // recharge curseurs, boutons, DPI
            g_profilesLoaded = false;
        }
        AppLog(ok ? (hadDefault ? "Default settings restored" : "Factory settings restored (empty config)")
                  : "FAILED to restore default settings");
        XexUtils::Xam::XNotify(ok ? (hadDefault ? "Default settings restored" : "Reglages d'origine restaures")
                                  : "Echec restauration des reglages");
    }
    if (s_model.requestMeasureMouse) {
        s_model.requestMeasureMouse = false;
        StartMouseMeasure();
    }
    if (s_model.requestStartCalibration) {
        s_model.requestStartCalibration = false;
        // Page glossy : on ouvre la page (explication + reglages avances),
        // la session demarre depuis la page.
        if (GlossyActive()) {
            g_calibPageOpen = true;
        } else {
            // Meme creation que l'ancien bouton de l'onglet Calibration : des que
            // g_calib existe, la boucle principale bascule en superposition.
            g_calib = new CalibrationSession(g_selectedMouseVid, g_selectedMousePid);
            g_calibSaveAttempted = false;
            g_calibSaveOk = false;
            AppLog("Mouse calibration started (%04X:%04X)", g_selectedMouseVid, g_selectedMousePid);
        }
    }
    if (s_model.requestFallback >= 0) {
        const int want = s_model.requestFallback;
        s_model.requestFallback = -1;
        if (want == 1) {
            std::ofstream f("HDD:\\X360RemapStudio\\force_boot_protocol.txt",
                            std::ios::binary | std::ios::trunc);
            bool ok = f.is_open();
            if (ok) f << "1";
            AppLog(ok ? "Fallback mode on - reboot required"
                      : "FAILED to write force_boot_protocol.txt");
            XexUtils::Xam::XNotify(ok ? "Mode de secours - redemarre la console"
                                      : "ECHEC - impossible d'ecrire le fichier");
        } else {
            bool ok = (std::remove("HDD:\\X360RemapStudio\\force_boot_protocol.txt") == 0);
            AppLog(ok ? "Automatic reading restored (file deleted)"
                      : "Already in automatic reading");
            XexUtils::Xam::XNotify(ok ? "Automatique - redemarre la console"
                                      : "Deja en mode automatique");
        }
    }
    // requestAdvanced n'est plus qu'une trace : l'ouverture du sous-panneau est
    // geree par l'ecran lui-meme (m.advancedOpen), il n'y a rien a faire ici.
    if (s_model.requestAdvanced) {
        s_model.requestAdvanced = false;
        AppLog("Advanced settings opened");
    }
}


// --- Diagnostic visuel (2026-08-01) -----------------------------------------
// Ajoute suite a un retour utilisateur ou ni le clavier (deja signale plus
// tot) ni le nouveau curseur souris ne semblaient recevoir de donnees, alors
// que X360Remap.xex les detecte bien cote log. Plutot que deviner encore une
// fois entre "input_state.json mal ecrit", "mal lu", ou "jamais rafraichi",
// affiche directement ce que l'appli voit reellement chaque frame - "tick"
// notamment: s'il reste fige d'une capture d'ecran a l'autre, le fichier
// n'est jamais rafraichi (bug cote ecriture ou tout simplement jamais
// atteint) ; s'il avance mais que les autres champs restent a leurs valeurs
// par defaut, le souci est plus cible. A retirer une fois le probleme
// identifie et corrige.
static void DrawDebugOverlay(const InputStateSnapshot& snap) {
    XexUtils::Input::Gamepad* pad = XexUtils::Input::GetInput(0);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
        "DEBUG snap: valid=%d key=%d mBtn=%d mDX=%d mDY=%d mMask=%d tick=%u",
        snap.valid ? 1 : 0, (int)snap.keyCode, (int)snap.mouseButton,
        (int)snap.mouseDX, (int)snap.mouseDY, (int)snap.mouseButtonsMask,
        (unsigned int)snap.tick);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
        "DEBUG pad: wButtons=0x%04x LX=%.2f LY=%.2f",
        (unsigned int)pad->wButtons, pad->ThumbLeftX, pad->ThumbLeftY);
    ImGui::Separator();
}

// --- Point d'entree ---------------------------------------------------------
void __cdecl main() {
    // Monte "HDD:" en lien symbolique EN TOUT PREMIER, avant le moindre appel
    // a AppLog (2026-08-17, bug reel trouve en tracant le rapport "clavier
    // detecte cote plugin, mais 'aucun clavier detecte' + wizard casse cote
    // appli" - voir PROJECT_NOTES.md). Ce bloc existait deja (fix du
    // 2026-08-01, voir commentaire ci-dessous) mais etait place APRES les
    // tout premiers AppLog("App: main() started") et les filets d'integrite -
    // or AppLog() ecrit sur "HDD:\..." sans aucun tampon memoire (contrairement
    // au FileLog()/g_logBuffer du plugin, voir PROJECT_NOTES.md section
    // "Persistent file logging"). Consequence concrete : TOUTES les lignes
    // de log emises avant ce point etaient silencieusement perdues (f.is_open()
    // == false, AppLog ne fait rien d'autre dans ce cas) - y compris, de facon
    // ironique, "App: MountHdd failed" elle-meme si l'echec se produisait :
    // le seul message qui aurait explique le probleme ne pouvait jamais
    // atteindre le disque, precisement PARCE QUE le montage avait echoue.
    // Deplace ici pour que plus aucun appel a AppLog ne puisse le precéder.
    //
    // Contrairement a X360Remap.xex, un plugin DashLaunch qui tourne DANS le
    // processus xam.xex du dashboard - lequel a deja "hdd:" mappe comme lien
    // symbolique SYSTEME - application.xex est un titre lance de facon
    // independante, avec son propre namespace d'objets prive, et n'herite
    // d'aucun mapping "HDD:". Sans cet appel, TOUT std::ifstream/ofstream vers
    // "HDD:\..." echoue silencieusement (is_open() == false) - confirme par le
    // diagnostic visuel (DrawDebugOverlay : valid=0, tick=0, alors que le
    // fichier existe bel et bien et est rafraichi cote plugin). Reutilise
    // XexUtils::Fs::MountHdd() (deja vendorise, voir Filesystem.cpp) plutot
    // que reimplementer ObCreateSymbolicLink a la main. Si le lien existe deja
    // (peu probable ici mais inoffensif), l'appel echoue avec une collision -
    // pas traite comme une erreur bloquante.
    bool hddMounted = SUCCEEDED(XexUtils::Fs::MountHdd());

    // --- Repli sur un autre support quand il n'y a PAS de disque dur ---
    // (2026-09-13, signale par un testeur : Slim 4 Go sans disque dur, lance
    // depuis une cle USB via BadUpdate. Symptome rapporte : "Creation dossier
    // X360RemapStudio echouee" au demarrage.)
    //
    // Tout le projet ecrit dans "HDD:\X360RemapStudio\" - une vingtaine de
    // chemins en dur des deux cotes (appli et plugin). Plutot que de reprendre
    // ces vingt-cinq litteraux (gros risque de regression pour un benefice nul
    // sur les consoles qui marchent deja), on exploite le fait que "HDD:" n'est
    // qu'un LIEN SYMBOLIQUE cree par MountHdd() : si le disque dur n'existe pas,
    // on fait pointer ce meme lien vers un autre peripherique. Les chemins
    // "HDD:\..." existants continuent alors de fonctionner tels quels, sans
    // qu'une seule ligne d'acces fichier ne change ailleurs.
    //
    // PROPRIETE DE SECURITE IMPORTANTE : ce bloc n'est atteint QUE si
    // MountHdd() a echoue. Sur une console avec disque dur (cas de HB et de la
    // grande majorite des utilisateurs), il ne s'execute jamais - ce changement
    // ne peut donc pas casser une installation qui fonctionne aujourd'hui.
    //
    // Les chemins de peripheriques testes : "\Device\Mass0\" est celui utilise
    // par XexUtils::Fs::MountUsb() (donc confirme correct pour une cle USB).
    // Les deux autres visent le stockage interne des modeles Slim/E et sont, eux,
    // NON VERIFIES - d'ou la strategie "essayer puis valider reellement" plutot
    // que de faire confiance au code de retour du montage : on ne garde un
    // candidat que si la creation du dossier reussit vraiment dessus.
    if (!hddMounted) {
        // ORDRE ET REGLE ALIGNES SUR LE PLUGIN (AUDIT_2026-09.md, I3).
        //
        // Avant : premier support inscriptible, en commencant par la cle USB.
        // Le plugin, lui, commencait par la memoire interne. Sur une Slim 4 Go
        // avec une cle branchee (configuration du testeur BadUpdate), les deux
        // ne regardaient pas au meme endroit : la configuration ecrite ici
        // n'arrivait jamais au plugin. Regle commune desormais, meme ordre
        // (memoire interne, puis USB) :
        //   passe 0 : le support qui contient DEJA X360Remap.json ;
        //   passe 1 : sinon celui qui contient deja le dossier X360RemapStudio
        //             (le plugin le cree au demarrage de la console, avant que
        //             la Studio ne soit jamais lancee) ;
        //   passe 2 : sinon le premier inscriptible.
        static const char* kFallbackDevices[] = {
            "\\Device\\BuiltInMuSb0\\",     // stockage interne Slim 4 Go (non verifie)
            "\\Device\\Flash\\",            // autre nom possible du stockage interne (non verifie)
            "\\Device\\Mass0\\",            // cle USB (confirme : MountUsb)
        };
        const int kFallbackCount = (int)(sizeof(kFallbackDevices) / sizeof(kFallbackDevices[0]));

        for (int pass = 0; pass < 3 && !hddMounted; pass++) {
            for (int i = 0; i < kFallbackCount && !hddMounted; i++) {
                if (FAILED(XexUtils::Fs::MountPath("hdd:", kFallbackDevices[i])))
                    continue;

                bool keep = false;
                if (pass == 0) {
                    keep = GetFileAttributesA("HDD:\\X360RemapStudio\\X360Remap.json") != INVALID_FILE_ATTRIBUTES;
                } else if (pass == 1) {
                    keep = GetFileAttributesA("HDD:\\X360RemapStudio") != INVALID_FILE_ATTRIBUTES;
                } else {
                    // Validation reelle : le montage peut "reussir" en creant un
                    // lien vers un peripherique absent. Seule une ecriture
                    // effective le prouve. ERROR_ALREADY_EXISTS compte aussi
                    // comme un succes.
                    keep = CreateDirectoryA("HDD:\\X360RemapStudio", nullptr) ||
                           GetLastError() == ERROR_ALREADY_EXISTS;
                }

                if (keep) {
                    hddMounted = true;
                    break;
                }

                // Candidat non retenu : on retire le lien avant d'essayer le
                // suivant, sinon le prochain MountPath("hdd:", ...) echouerait
                // sur collision.
                XexUtils::Fs::UnmountPath("hdd:");
            }
        }
    }

    // X360Remap.json/input_state.json vivent dans un sous-dossier dedie
    // (HDD:\X360RemapStudio\) plutot qu'a la racine du disque dur - demande
    // utilisateur du 2026-08-02 pour ne pas polluer HDD:\. Cree le dossier
    // s'il n'existe pas encore (premier lancement) ; s'il existe deja,
    // CreateDirectoryA echoue avec ERROR_ALREADY_EXISTS, ce qui est normal et
    // pas une erreur bloquante. X360Remap.xex cree ce meme dossier de son cote
    // (voir X360Remap/main.cpp) puisque les deux .xex y ecrivent. Doit rester
    // APRES MountHdd() (meme raison) et AVANT le premier AppLog (meme bug que
    // ci-dessus, sinon un dossier absent au tout premier lancement produirait
    // aussi un journal vide sans aucune trace de la vraie cause).
    if (!CreateDirectoryA("HDD:\\X360RemapStudio", nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            // EN ANGLAIS, VOLONTAIREMENT (2026-09-13) : ce message ne passe pas
            // - et ne PEUT pas passer - par le systeme de traduction (i18n).
            // La preference de langue est lue dans X360Remap_lang.txt, qui vit
            // precisement dans le dossier qu'on vient d'echouer a creer : au
            // moment de cette erreur, la langue choisie par l'utilisateur est
            // par definition inconnue. Un testeur anglophone a donc recu ce
            // message en francais (signale le 2026-09-13). L'anglais est le
            // choix le moins mauvais pour un message d'erreur precoce destine
            // a une audience internationale.
            XexUtils::Xam::XNotify("X360Remap: no writable storage found (HDD/USB)");
        }
    }

    // Points de controle ajoutes le 2026-08-01 pour diagnostiquer un "rien ne
    // s'affiche" reporte au premier lancement reel. Passaient a l'origine par
    // XexUtils::Xam::XNotify (notification visible a l'ecran, systeme
    // d'Aurora/Xam, independant de notre propre device D3D9 - donc visible
    // meme si la creation du device ou l'init ImGui echoue silencieusement,
    // return anticipe sans aucune trace jusque-la).
    //
    // Rebascules en AppLog silencieux le 2026-08-09 (signale par HB : "je
    // veux mettre au silence les notifications qui se lancent avec l'app") -
    // le rendu est confirme stable depuis, ces points de repere n'avaient
    // plus de raison de s'afficher a l'ecran a CHAQUE lancement. Seuls les
    // ECHECs (creation D3D9/device/ImGui) restent en XNotify visible plus
    // bas : ce sont les seuls cas ou l'utilisateur a besoin de savoir que
    // quelque chose s'est reellement mal passe - les etapes de succes
    // (creation reussie, boucle demarree...) n'apportent rien a l'ecran et
    // restent desormais uniquement dans le journal.
    AppLog("App: main() started");

    // --- Installation fraiche : la Studio cree la configuration (2026-09-23) --
    //
    // Regle de livraison fixee par HB : seuls les deux .xex sont distribues,
    // TOUS les autres fichiers doivent etre generes par le plugin ou la Studio.
    // Jusqu'ici, X360Remap.json n'etait cree qu'a la premiere SAUVEGARDE dans
    // la Studio. Un utilisateur qui lancait la Studio sans rien changer - les
    // reglages par defaut lui convenant - gardait donc un plugin qui affichait
    // "installation incomplete" a chaque demarrage, alors que tout etait en
    // place. Desormais, le simple fait de lancer la Studio une fois cree un
    // fichier de configuration vide ("[]" = aucun reglage personnalise, le
    // plugin applique ses reglages par defaut) : c'est la preuve, pour le
    // plugin, que l'application est installee.
    //
    // Ordre : d'abord la reparation depuis une copie de secours eventuelle, et
    // on ne cree QUE si le fichier est absent - jamais d'ecrasement d'un
    // fichier existant, meme illisible (le plugin le signale alors par un
    // autre message, "X360Remap.json invalide").
    {
        const char* cfg = "HDD:\\X360RemapStudio\\X360Remap.json";
        if (RecoverConfigFromTmp(cfg))
            AppLog("App: X360Remap.json rebuilt from its backup copy");
        if (GetFileAttributesA(cfg) == INVALID_FILE_ATTRIBUTES) {
            std::ofstream fresh(cfg, std::ios::binary | std::ios::trunc);
            if (fresh.is_open()) {
                fresh << "[]\n";
                AppLog("App: first install - X360Remap.json created (default settings)");
            } else {
                AppLog("App: FAILED to create X360Remap.json");
            }
        }
    }
    if (!hddMounted) {
        // Cas courant et inoffensif (lien deja monte) le plus souvent, mais
        // maintenant que cet appel arrive AVANT toute ecriture, ce message a
        // enfin une vraie chance d'atteindre le disque si l'echec est reel.
        AppLog("App: MountHdd failed (already mounted?)");
    }

    // Filet contre le decalage de la table de traduction (voir i18n.h). Un
    // decalage n'empeche pas l'application de tourner - elle affiche juste le
    // mauvais texte au mauvais endroit - donc on le TRACE sans bloquer. Sans
    // ca, le symptome se decouvre a l'oeil, longtemps apres, et se diagnostique
    // mal (c'est arrive le 2026-08-14).
    {
        int badIndex = CheckStringTableIntegrity();
        if (badIndex >= 0)
            AppLog("App: WARNING translation table SHIFTED at index %d - wrong texts on screen", badIndex);
    }

    // Meme filet pour le manifeste de skin (voir ui_assets.h) - un tableau
    // desaligne y serait tout aussi silencieux qu'en i18n : pas d'erreur de
    // compilation, juste une texture affichee au mauvais endroit.
    {
        int badAsset = CheckAssetTableIntegrity();
        if (badAsset >= 0)
            AppLog("App: WARNING skin manifest SHIFTED at index %d - wrong textures on screen", badAsset);
    }

    // Langue de l'UI (2026-08-07, voir application/i18n.h) - lue une seule
    // fois ici, apres le montage HDD: et la creation du dossier
    // X360RemapStudio ci-dessus (meme ordre de dependance que
    // force_boot_protocol.txt, qui suppose lui aussi HDD: deja monte).
    // Retombe sur LANG_FR (comportement actuel avant ce systeme) si le
    // fichier est absent ou son contenu non reconnu - voir LoadLanguagePref().
    LoadLanguagePref();

    // Creation du device D3D9 - meme schema que n'importe quel homebrew Xbox
    // 360 (snes360-enhanced, reference GPL-3.0 citee dans ARCHITECTURE.md).
    // Contrairement au plan XUI initial, application.xex possede son propre
    // device : pas besoin de hooker celui d'Aurora, on prend l'ecran comme
    // n'importe quel jeu/emulateur homebrew le temps de tourner.
    LPDIRECT3D9 pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        // Echec reel, garde en visible - l'utilisateur doit savoir que
        // l'application n'a pas pu demarrer du tout.
        // En anglais comme les autres notifications systeme (2026-09-13) :
        // elles ne peuvent pas passer par l'i18n (echec d'init du rendu = plus
        // aucun affichage possible), et le projet a des utilisateurs
        // internationaux.
        XexUtils::Xam::XNotify("X360Remap: FAILED Direct3DCreate9");
        return;
    }
    AppLog("App: Direct3D9 created");

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = FALSE;
    // D3DPRESENT_INTERVAL_ONE_OR_IMMEDIATE n'existe pas en D3D9 standard
    // (erreur reelle "undeclared identifier" au build) - probablement
    // confondu avec une constante D3D9Ex PC. D3DPRESENT_INTERVAL_ONE
    // (vsync, ~60Hz) est le choix standard pour une interface, evite le
    // tearing et une boucle qui tourne sans limite.
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    LPDIRECT3DDEVICE9 pDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDevice))) {
        // Echec reel, garde en visible - meme raisonnement que ECHEC
        // Direct3DCreate9 ci-dessus.
        XexUtils::Xam::XNotify("X360Remap: FAILED CreateDevice");
        pD3D->Release();
        return;
    }
    AppLog("App: D3D9 device created");

    ImGui::CreateContext();
    // Theme visuel "X360Remap Studio" (2026-08-08, debut de la refonte
    // demandee par HB - voir theme.h et ETAT_DU_PROJET.md section 3). Doit
    // s'appliquer avant la premiere ImGui::NewFrame() plus bas ; APRES
    // CreateContext() puisque ImGui::GetStyle() n'existe qu'une fois le
    // contexte cree.
    ApplyX360RemapTheme();
    ImGuiIO& io = ImGui::GetIO();
    // Navigation manette gardee en secours (utile si la souris n'est pas
    // branchee pendant la config) - le curseur souris (UpdateMouseCursor,
    // 2026-08-01) est desormais le moyen principal, voir sa note en tete.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.DisplaySize = ImVec2((float)pp.BackBufferWidth, (float)pp.BackBufferHeight);
    // Xbox 360 n'a pas de curseur systeme - ImGui doit dessiner le sien a
    // partir d'io.MousePos (voir UpdateMouseCursor).
    io.MouseDrawCursor = true;

    // Police custom "Inter" (2026-08-08, voir theme.h) - APRES avoir pose
    // io.DisplaySize/ConfigFlags (elle ne les lit pas, mais autant garder
    // toute la configuration io.* groupee) et AVANT ImGui_ImplDX9_Init() :
    // l'atlas de police doit etre construit avant que l'implementation DX9
    // ne cree sa texture a partir de lui. 24px : taille de depart pour le
    // texte courant, a ajuster une fois la mise en page de la maquette 2 en
    // place (la consigne HB de 41px visait le texte d'instruction principal
    // du wizard, pas le texte courant general).
    LoadX360RemapFont(24.0f);

    // Etat de l'atlas de police, ecrit dans le journal (2026-09-13). ImGui ne
    // signale jamais un glyphe qu'il n'a pas pu ranger : sans cette ligne, un
    // accent casse ne se decouvre qu'a l'oeil. "manquants : 0" partout =
    // atlas sain ; tout autre chiffre designe la taille de police fautive.
    AppLog("%hs", GetFontAtlasReport());

    g_device = pDevice;
    if (!ImGui_ImplDX9_Init(pDevice)) {
        // Echec reel, garde en visible - meme raisonnement que les deux
        // echecs D3D9 ci-dessus.
        XexUtils::Xam::XNotify("X360Remap: FAILED ImGui_ImplDX9_Init");
        pDevice->Release();
        pD3D->Release();
        return;
    }
    AppLog("App: ImGui ready, render loop started");

    // Visuels de la refonte v1.2, decodes une seule fois ici. Volontairement
    // APRES ImGui_ImplDX9_Init : le backend y cree l'atlas de police, donc si
    // la creation de texture devait echouer sur cette console, l'erreur
    // apparaitrait la-bas en premier (police absente = symptome bien plus
    // visible qu'une illustration manquante).
    //
    // Un echec ici n'est PAS fatal : l'application continue sans illustration.
    // On le trace dans le journal plutot que par une notification a l'ecran -
    // c'est une degradation cosmetique, pas une panne.
    if (LoadTextureFromMemory(pDevice, kImageManette, kImageManette_size,
                              &g_texManette, &g_texManetteW, &g_texManetteH))
        AppLog("App: controller texture loaded (%dx%d)", g_texManetteW, g_texManetteH);
    else
        AppLog("App: FAILED to load controller texture - shown without picture");

    if (LoadTextureFromMemory(pDevice, kImageSouris, kImageSouris_size,
                              &g_texSouris, &g_texSourisW, &g_texSourisH))
        AppLog("App: mouse texture loaded (%dx%d)", g_texSourisW, g_texSourisH);
    else
        AppLog("App: FAILED to load mouse texture - shown without picture");

    if (LoadTextureFromMemory(pDevice, kImageQrKofi, kImageQrKofi_size,
                              &g_texQrKofi, &g_texQrKofiW, &g_texQrKofiH))
        AppLog("App: Ko-fi QR texture loaded (%dx%d)", g_texQrKofiW, g_texQrKofiH);
    else
        AppLog("App: FAILED to load Ko-fi QR texture - Support page without QR");

    // Skin visuel (voir ui_assets.h) : chaque asset absent retombe sur le
    // rendu trace existant, donc un skin incomplet ou totalement absent (cas
    // d'aujourd'hui, tant qu'aucun fichier n'a ete livre) n'empeche jamais
    // l'application de demarrer. Le detail asset-par-asset va au journal :
    // c'est la ou HB pourra verifier precisement quels fichiers ont ete vus
    // par la console, sans avoir a deviner en regardant l'ecran.
    LoadUiSkin(pDevice, kActiveSkinName, g_uiSkin);
    {
        int found = 0;
        for (int i = 0; i < UI_ASSET_COUNT; i++) {
            if (g_uiSkin.present[i]) {
                found++;
                AppLog("App: skin '%s' - %s loaded (%dx%d)",
                       kActiveSkinName, g_uiAssets[i].fileName, g_uiSkin.w[i], g_uiSkin.h[i]);
            }
        }
        AppLog("App: skin '%s' - %d/%d textures found, the rest uses drawn rendering",
               kActiveSkinName, found, (int)UI_ASSET_COUNT);
    }

    // Conversion vers le type portable que ui_mapping.cpp comprend (voir
    // UiSkinSlot dans ui_mapping.h) : CASE PAR CASE, par nom, pas par copie en
    // boucle - g_uiSkin est indexe par UiAssetId (application/ui_assets.h),
    // g_skinTex par UiSkinSlot (application/ui_mapping.h), deux enums qui
    // n'ont aucune obligation de rester dans le meme ordre. Un oubli ici
    // laisse simplement ce slot a "absent" (repli procedural), jamais un
    // decalage silencieux vers le mauvais fichier.
    static UiSkinTex g_skinTex[SKIN_SLOT_COUNT];
    {
        struct Map { UiSkinSlot slot; UiAssetId asset; };
        const Map kMap[] = {
            { SKIN_PANEL_DEFAULT, UI_ASSET_PANEL_DEFAULT },
            { SKIN_PANEL_CTX,     UI_ASSET_PANEL_CTX },
            { SKIN_CARD_DEVICE,   UI_ASSET_CARD_DEVICE },
            { SKIN_CELL_GRID,     UI_ASSET_CELL_GRID },
            { SKIN_FIELD_COMBO,   UI_ASSET_FIELD_COMBO },
            { SKIN_BUTTON_ACCENT, UI_ASSET_BUTTON_ACCENT },
            { SKIN_BUTTON_GHOST,  UI_ASSET_BUTTON_GHOST },
            { SKIN_ICON_KEYBOARD, UI_ASSET_ICON_KEYBOARD },
            { SKIN_ICON_MOUSE,    UI_ASSET_ICON_MOUSE },
            { SKIN_ICON_GEAR,     UI_ASSET_ICON_GEAR },
            { SKIN_ICON_PLAY,     UI_ASSET_ICON_PLAY },
            { SKIN_ICON_LIST,     UI_ASSET_ICON_LIST },
            { SKIN_ICON_HELP,     UI_ASSET_ICON_HELP },
            { SKIN_ICON_HEART,    UI_ASSET_ICON_HEART },
            { SKIN_LOGO_WORDMARK, UI_ASSET_LOGO_WORDMARK },
        };
        for (int i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); i++) {
            UiSkinSlot slot = kMap[i].slot;
            UiAssetId  a    = kMap[i].asset;
            UiSkinTex& out  = g_skinTex[slot];
            out.present = g_uiSkin.present[a];
            out.tex     = out.present ? (ImTextureID)g_uiSkin.tex[a] : 0;
            out.w       = g_uiSkin.w[a];
            out.h       = g_uiSkin.h[a];
            out.sliceL  = g_uiAssets[a].sliceL;
            out.sliceT  = g_uiAssets[a].sliceT;
            out.sliceR  = g_uiAssets[a].sliceR;
            out.sliceB  = g_uiAssets[a].sliceB;
        }
    }
    UiSetSkin(g_skinTex);

    // --- Page glossy (2026-09-24) : textures embarquees ---------------------
    // Meme chargeur que la manette/souris (PNG en memoire -> texture lineaire).
    // Une seule texture manquante et la page glossy reste desactivee
    // (UiGlossyReady) : on garde alors l'ancienne page plutot qu'une page a trous.
    {
        const int bad = CheckGlossyAssetTable();
        if (bad >= 0)
            AppLog("App: glossy - ERROR manifest misaligned at entry %d, glossy page disabled", bad);
        int ok = 0;
        for (int i = 0; i < GA_COUNT; i++) {
            UiSkinTex& t = g_glossySkinTex[i];
            g_glossyTex[i] = 0;
            int w = 0, h = 0;
            if (bad < 0 && LoadTextureFromMemory(pDevice, g_glossyAssets[i].data, g_glossyAssets[i].size,
                                                 &g_glossyTex[i], &w, &h)) {
                t.tex = (ImTextureID)g_glossyTex[i];
                t.w = w; t.h = h; t.present = true;
                ok++;
            } else {
                AppLog("App: glossy - FAILED texture %s", g_glossyAssets[i].name);
            }
        }
        UiGlossySetTextures(g_glossySkinTex);
        AppLog("App: glossy - %d/%d textures loaded, page %s", ok, (int)GA_COUNT,
               (EN_GLOSSY_UI && ok == GA_COUNT) ? "glossy active" : "classic");
    }

    bool firstFramePresented = false;
    bool running = true;
    while (running) {
        // Une seule lecture de input_state.json par frame, partagee par
        // UpdateWizardModel/UpdateMouseCursor/DrawDebugOverlay (voir g_lastSnapshot).
        //
        // CORRIGE LE 2026-09-23 (AUDIT_2026-09.md, I2). Cette appli lit le
        // fichier 60 fois par seconde, le plugin ne l'ecrit que 10 fois par
        // seconde : le MEME deplacement souris et le MEME cran de molette
        // etaient donc reappliques sur ~6 images de suite - curseur saccade et
        // accelere, molette x6. Et une lecture tombee pendant que le plugin
        // reecrivait le fichier declarait la souris "absente" le temps d'une
        // image : bouton relache puis re-presse, d'ou des clics fantomes.
        //
        // Desormais :
        //   - deplacements et molette appliques UNE fois par nouveau releve
        //     (champ tick du plugin) ;
        //   - une lecture ratee conserve le dernier etat valide (sans rejouer
        //     ses deplacements) pendant 500 ms avant de conclure a l'absence.
        {
            static uint32_t s_lastAppliedTick = 0;
            static DWORD    s_lastValidReadMs = 0;
            InputStateSnapshot fresh = ReadInputState();
            if (fresh.valid) {
                const bool isNewSample = (fresh.tick != s_lastAppliedTick);
                s_lastAppliedTick = fresh.tick;
                s_lastValidReadMs = GetTickCount();
                if (!isNewSample) {
                    fresh.mouseDX = 0;
                    fresh.mouseDY = 0;
                    fresh.wheelDelta = 0;
                }
                g_lastSnapshot = fresh;
            } else if (g_lastSnapshot.valid && (GetTickCount() - s_lastValidReadMs) < 500) {
                g_lastSnapshot.mouseDX = 0;
                g_lastSnapshot.mouseDY = 0;
                g_lastSnapshot.wheelDelta = 0;
            } else {
                g_lastSnapshot = fresh;   // vraiment absent (plugin non charge, aucun peripherique)
            }
        }

        // Signale au plugin que la Studio tourne (AUDIT I1) : il n'ecrit plus
        // input_state.json qu'en presence de ce fichier, et le supprime a
        // chaque changement de jeu. Recree s'il a disparu - verification
        // toutes les 2 s, simple lecture d'attributs le reste du temps.
        {
            static DWORD s_lastFlagCheckMs = 0;
            if (s_lastFlagCheckMs == 0 || (GetTickCount() - s_lastFlagCheckMs) > 2000) {
                s_lastFlagCheckMs = GetTickCount();
                if (GetFileAttributesA("HDD:\\X360RemapStudio\\studio_active.flag") == INVALID_FILE_ATTRIBUTES) {
                    std::ofstream flag("HDD:\\X360RemapStudio\\studio_active.flag", std::ios::binary | std::ios::trunc);
                }
            }
        }

        // Defilement a la molette (2026-08-09, demande utilisateur : "je vais
        // pas cliquer sur les fleches ou presser sur la barre"). io.MouseWheel
        // doit etre pose AVANT ImGui::NewFrame() - c'est ce que NewFrame lit
        // pour mettre a jour le defilement de la fenetre/zone sous le
        // curseur. g_lastSnapshot.wheelDelta est deja le delta accumule
        // depuis la derniere lecture (voir input_state_reader.h), pas une
        // valeur absolue - directement utilisable tel quel.
        io.MouseWheel = (float)g_lastSnapshot.wheelDelta;

        UpdateGamepadNavigation(io);
        UpdateMouseCursor(io, g_lastSnapshot, (float)pp.BackBufferWidth, (float)pp.BackBufferHeight);

        ImGui_ImplDX9_NewFrame();
        ImGui::NewFrame();

        // Zone sure TV : marge de 7% tout autour (2026-08-09, demande
        // utilisateur : "je veux que l'affichage ne prenne pas tout
        // l'ecran"). Remplace l'ancienne marge fixe de 40px, qui ne
        // respectait pas la consigne deja posee dans ETAT_DU_PROJET.md
        // section 3 (40px ~= 3-5% de 1280x720 selon l'axe, sous les 7%
        // vises) - insuffisant sur une TV avec overscan notable.
        // PLEIN ECRAN (2026-08-14, demande HB : "je veux que l'affichage prenne
        // toute la surface de l'ecran au lieu d'etre comme un cadre avec un
        // vide noir sur les cotes").
        //
        // La marge de securite TV n'est pas supprimee pour autant : beaucoup
        // de televiseurs rognent les bords (overscan) et un texte colle au
        // bord y disparait. On applique donc la solution des interfaces
        // console professionnelles - le FOND occupe 100% de l'ecran, le
        // CONTENU LISIBLE reste dans la zone sure (voir kUiSafeX/kUiSafeY dans
        // DrawMappingTab et le bandeau superieur ci-dessous).
        const float screenW = (float)pp.BackBufferWidth;
        const float screenH = (float)pp.BackBufferHeight;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(screenW, screenH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##root", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Refonte v1.2 : un PARCOURS GUIDE en cours prend tout l'ecran.
        //
        // Ni barre d'onglets, ni selecteur de peripheriques, ni bandeau de
        // debug pendant ce temps : l'utilisateur suit des etapes, tout le
        // reste est une distraction et un risque de sortie accidentelle en
        // plein milieu. Ne concerne plus que la calibration souris depuis le
        // 2026-08-17 : le wizard clavier est sorti de la superposition (voir
        // UpdateWizardModel plus haut) - HB a fait remarquer que masquer les
        // 21 boutons de la grille pendant le wizard n'avait pas de sens, il
        // reste desormais visible sur la page Mapping avec la case courante
        // surlignee. La calibration souris, elle, garde la superposition :
        // aucun equivalent "grille a montrer en direct" pour elle, et elle
        // est pilotee a la manette pendant que la souris est justement
        // hors d'usage (voir le commentaire au-dessus de DrawCalibrationTab).
        const bool overlayActive = (g_calib != nullptr) || g_measureActive || g_calibPageOpen;

        // --- Echap : retour a Aurora (2026-09-26, demande HB) ------------------
        // Un premier appui affiche "Appuie encore sur Echap..." pendant 2,5 s,
        // le second quitte. Ignore si Echap sert deja a autre chose (assistant,
        // capture d'une touche, saisie d'un nom, pages Mesurer/Calibrer) - y
        // compris l'image qui suit, pour que l'Echap qui annule un assistant ne
        // compte pas aussi comme demande de sortie.
        static bool  s_prevEsc = false, s_prevBusy = true;
        static DWORD s_exitArmedMs = 0;
        {
            const bool esc  = g_lastSnapshot.valid && g_lastSnapshot.keyCode == 41;   // HID 0x29
            const bool busy = (g_wizard != nullptr) || (g_manualSession != nullptr) || overlayActive || g_gamesRenaming;
            if (esc && !s_prevEsc && !busy && !s_prevBusy) {
                if (s_exitArmedMs != 0 && (GetTickCount() - s_exitArmedMs) < 2500) {
                    AppLog("App: Esc x2 - back to the dashboard");
                    g_exitToDashboard = true;
                    running = false;
                } else {
                    s_exitArmedMs = GetTickCount();
                }
            }
            s_prevEsc = esc;
            s_prevBusy = busy;
            if (s_exitArmedMs != 0 && (GetTickCount() - s_exitArmedMs) >= 2500) s_exitArmedMs = 0;
        }

        if (overlayActive && GlossyActive()) {
            // Pages glossy plein ecran (2026-09-25)
            if (g_measureActive) DrawMeasureGlossy(screenW, screenH);
            else                 DrawCalibrationGlossy(screenW, screenH);
        } else if (overlayActive) {
            // La fenetre n'a plus aucune marge interieure depuis le passage en
            // plein ecran : sans ce recalage, le parcours guide s'afficherait
            // colle au bord de l'ecran, la ou une TV peut le rogner.
            ImGui::SetCursorPos(ImVec2(screenW * kUiSafeX, screenH * kUiSafeY));
            ImGui::BeginChild("##overlay",
                              ImVec2(screenW * (1.0f - kUiSafeX * 2.0f),
                                     screenH * (1.0f - kUiSafeY * 2.0f)), false);
            if (g_calib)
                DrawCalibrationTab();
            else
                DrawMeasureOverlay();
            ImGui::EndChild();
        } else {
            // Bandeau de diagnostic retire de l'affichage le 2026-08-13 : il
            // avait ete ajoute le 2026-08-01 pour traquer un bug precis, avec
            // la mention "a retirer une fois le probleme identifie et
            // corrige" - le probleme l'est depuis longtemps, mais le bandeau
            // etait reste. C'etait la premiere chose que lisait l'utilisateur.
            // La fonction est conservee (voir DrawDebugOverlay plus haut) :
            // il suffit de reactiver cet appel pour la reprendre le jour ou
            // un nouveau bug d'entree demande d'observer ce que l'appli voit
            // reellement chaque frame.
            //   DrawDebugOverlay(g_lastSnapshot);
            if (GlossyActive() && g_activeTab == TAB_SUPPORT) {
                // Page Soutenir glossy (2026-09-25)
                GlossySupportModel sm;
                sm.version     = kAppVersion;
                sm.pluginAlive = g_lastSnapshot.valid;
                sm.releasesUrl = IsLinkIntact(kReleasesUrl, kReleasesUrlHash) ? kReleasesUrl : nullptr;
                sm.kofiUrl     = IsLinkIntact(kKofiUrl, kKofiUrlHash) ? kKofiUrl : nullptr;
                sm.paypal      = IsLinkIntact(kPayPalEmail, kPayPalEmailHash) ? kPayPalEmail : nullptr;
                sm.githubUrl   = IsLinkIntact(kGithubUrl, kGithubUrlHash) ? kGithubUrl : nullptr;
                // Chaine YouTube supprimee (2026-09-25) : lien retire en attendant la nouvelle.
                sm.youtubeUrl  = nullptr;
                int tab = (int)g_activeTab;
                DrawUiGlossySupport(sm, tab, screenW, screenH);
                g_activeTab = (AppTab)tab;
            } else if (GlossyActive() && g_activeTab == TAB_HOWTO) {
                // Page Comment ca marche glossy (2026-09-25)
                int tab = (int)g_activeTab;
                DrawUiGlossyHowTo(tab, screenW, screenH);
                g_activeTab = (AppTab)tab;
            } else if (GlossyActive() && g_activeTab == TAB_PROFILES) {
                // Page Profils de jeux glossy (2026-09-25)
                int tab = (int)g_activeTab;
                DrawGamesGlossy(tab, screenW, screenH);
                g_activeTab = (AppTab)tab;
            } else if (GlossyActive() && g_activeTab == TAB_MAPPING) {
                // Page glossy : plein ecran, sa propre barre d'onglets. Elle
                // se dessine dans la fenetre racine en coordonnees absolues.
                g_glossyTabOut = (int)g_activeTab;
                DrawMappingTab();
                g_activeTab = (AppTab)g_glossyTabOut;
            } else {
            {
                // g_activeTab est un enum ; DrawUiTopBar travaille sur un int
                // pour rester independant des types de main.cpp.
                int tab = (int)g_activeTab;
                DrawUiTopBar(screenW, screenH, tab);
                g_activeTab = (AppTab)tab;
            }

            // Corps, place SOUS le bandeau et dans la zone sure. Chaque page
            // recoit la place restante ; c'est elle qui organise ses colonnes.
            const float marginX = screenW * kUiSafeX;
            const float marginY = screenH * kUiSafeY;
            const float bodyTop = UiTopBarHeight(screenH) + 14.0f;
            const float bodyW   = screenW - marginX * 2.0f;
            const float bodyH   = screenH - bodyTop - marginY * 0.75f;

            ImGui::SetCursorPos(ImVec2(marginX, bodyTop));
            ImGui::BeginChild("##body", ImVec2(bodyW, bodyH), false);
            switch (g_activeTab) {
                case TAB_MAPPING:  DrawMappingTab();     break;
                case TAB_PROFILES: DrawProfilesTab();    break;
                case TAB_HOWTO:    DrawHowItWorksTab();  break;
                case TAB_SUPPORT:  DrawSupportTab();     break;
            }
            ImGui::EndChild();
            }   // fin de l'ancienne mise en page (hors page glossy)
        }

        // Bandeau "Appuie encore sur Echap" (voir plus haut)
        if (s_exitArmedMs != 0 && GlossyActive()) {
            const float k = (float)(GetTickCount() - s_exitArmedMs) / 2500.0f;
            DrawUiGlossyToast(Tr(STR_EXIT_CONFIRM), k < 0.8f ? 1.0f : (1.0f - k) * 5.0f, screenW, screenH);
        }

        // Ecran de demarrage anime (2026-09-26, idee HB), par-dessus la page :
        // son fondu de sortie la revele. Une touche ou un clic l'abrege.
        {
            static DWORD s_splashStart = 0;
            static bool  s_splashDone = false;
            if (!s_splashDone) {
                if (!GlossyActive()) {
                    s_splashDone = true;
                } else {
                    if (s_splashStart == 0) s_splashStart = GetTickCount();
                    float t = (float)(GetTickCount() - s_splashStart) / 1000.0f;
                    const bool skip = t > 0.4f && t < 1.75f &&
                        (io.MouseClicked[0] || (g_lastSnapshot.valid && g_lastSnapshot.keyCode >= 0));
                    if (skip) { s_splashStart = GetTickCount() - 1750; t = 1.75f; }
                    static char s_ver[16] = "";
                    if (!s_ver[0]) _snprintf(s_ver, sizeof(s_ver) - 1, "v%s", kAppVersion);
                    if (!DrawUiGlossySplash(t, screenW, screenH, s_ver)) s_splashDone = true;
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(3);

        pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 20, 20, 24), 1.0f, 0);
        if (SUCCEEDED(pDevice->BeginScene())) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            pDevice->EndScene();
        }
        pDevice->Present(nullptr, nullptr, nullptr, nullptr);

        // Point de controle une seule fois (pas a chaque frame, ca
        // spammerait des notifications en continu) - confirme que la
        // boucle de rendu tourne reellement jusqu'au bout au moins une
        // fois, distinct de "la boucle demarre" ci-dessus (qui ne prouve
        // pas que BeginScene/Clear/Present ne plantent pas).
        if (!firstFramePresented) {
            firstFramePresented = true;
            AppLog("App: first frame presented");
        }

        // Pas de condition de sortie explicite pour ce premier jet - a
        // ajouter (ex: combinaison de boutons, ou bouton "Quitter" dans un
        // futur menu principal) une fois l'ecran unique confirme buildable.
    }

    delete g_wizard;
    delete g_manualSession;

    // Textures de la refonte v1.2, liberees AVANT la destruction du device
    // (les objets Direct3D9 doivent l'etre pendant que leur device existe
    // encore). Les pointeurs sont remis a nullptr : si un jour ce code est
    // deplace ou appele deux fois, on ne relachera pas une seconde fois.
    if (g_texManette) { g_texManette->Release(); g_texManette = nullptr; }
    if (g_texSouris)  { g_texSouris->Release();  g_texSouris  = nullptr; }
    if (g_texQrKofi)  { g_texQrKofi->Release();  g_texQrKofi  = nullptr; }
    ReleaseUiSkin(g_uiSkin);
    UiGlossySetTextures(0);
    FreeGameCovers();
    for (int i = 0; i < GA_COUNT; i++) {
        if (g_glossyTex[i]) { g_glossyTex[i]->Release(); g_glossyTex[i] = 0; }
    }

    ImGui_ImplDX9_Shutdown();
    ImGui::DestroyContext();
    pDevice->Release();
    pD3D->Release();

    // Echap x2 (2026-09-26) : retour a l'application par defaut de DashLaunch
    // (Aurora chez HB), comme le bouton Guide > Tableau de bord.
    if (g_exitToDashboard) {
        AppLog("App: XLaunchNewImage(dashboard)");
        XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
    }
}
