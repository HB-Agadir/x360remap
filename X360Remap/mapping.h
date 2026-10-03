#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include "usb.h"

// The Xbox 360 (Xenon/PowerPC) is big-endian, so the real build must force
// rapidjson to that mode - it can't rely on autodetection because _XBOX is a
// custom XDK target rapidjson's own autodetect (rapidjson/rapidjson.h) doesn't
// know about. On any other target (e.g. a PC test harness compiling this same
// file to validate the mapping logic off-console, see X360Remap/tests/),
// forcing big-endian on a little-endian host silently corrupts every integer
// field rapidjson touches (vid/pid/key codes all read back as 0) - so only
// force it when actually building for the console; everywhere else, let
// rapidjson's own autodetection (which is correct for x86/x64) apply.
#ifndef RAPIDJSON_ENDIAN
#ifdef _XBOX
#define RAPIDJSON_ENDIAN RAPIDJSON_BIGENDIAN
#endif
#endif

struct HidAxisMapEntry {
    uint16_t usage;
    int16_t ButtonsReport::* field;
};

struct HidButtonMapEntry {
    uint8_t idx;
    uint8_t ButtonsReport::* field;
};

// What a keyboard key does once pressed. Deliberately an enum rather than a
// ButtonsReport member pointer (like HidButtonMapEntry uses), because a key can
// also drive a stick DIRECTION, which is not a single field: "left stick up"
// means writing a specific value into ButtonsReport::y. An enum also gives the
// future config UI a finite list of actions to offer.
enum KeyAction {
    KEYACT_NONE = 0,
    KEYACT_A, KEYACT_B, KEYACT_X, KEYACT_Y,
    KEYACT_LB, KEYACT_RB, KEYACT_LT, KEYACT_RT,
    KEYACT_L3, KEYACT_R3,
    KEYACT_START, KEYACT_BACK, KEYACT_GUIDE,
    KEYACT_DPAD_UP, KEYACT_DPAD_DOWN, KEYACT_DPAD_LEFT, KEYACT_DPAD_RIGHT,
    KEYACT_LSTICK_UP, KEYACT_LSTICK_DOWN, KEYACT_LSTICK_LEFT, KEYACT_LSTICK_RIGHT,
    KEYACT_RSTICK_UP, KEYACT_RSTICK_DOWN, KEYACT_RSTICK_LEFT, KEYACT_RSTICK_RIGHT
};

struct HidKeyMapEntry {
    // HID usage code on the Keyboard/Keypad page (0x07). Regular keys are
    // 0x04..0xA4 (A=0x04, W=0x1A...). Modifier keys use their own usage codes
    // 0xE0..0xE7 (LCtrl=0xE0, LShift=0xE1, LAlt=0xE2 ... RAlt=0xE6), even though
    // the boot report carries them as bits rather than in the keycode array -
    // the lookup handles both, so a config file can name any key uniformly.
    uint8_t code;
    uint8_t action;   // KeyAction
};

struct HidAxisInvertFlags {
    bool invertX;
    bool invertY;
    bool invertZ;
    bool invertRX;
    bool invertRY;
    bool invertRZ;
};

// How raw mouse movement maps to stick displacement. CURVE_LINEAR is today's
// existing behavior (unchanged default). CURVE_EXPONENTIAL lets small
// movements stay precise for aiming while still reaching full deflection
// quickly - the exponent controls how aggressive the curve is (1.0 == linear,
// higher = more precision near center).
enum SensitivityCurveType : uint8_t {
    CURVE_LINEAR = 0,
    CURVE_EXPONENTIAL = 1,
};

// Per-game override of a device's settings, looked up by
// XamGetCurrentTitleId() (already used elsewhere in the plugin). Every field
// uses the same "sentinel means inherit from the device's base settings"
// convention as HidDeviceMapping::mouseSensitivity (0 = inherit) - negative
// sentinels are used for fields where 0 is itself a valid value (deadzone,
// invertMouseY, curveType).
struct HidDeviceProfile {
    uint32_t titleId;

    int32_t mouseSensitivity;  // 0 = inherit
    int8_t invertMouseY;       // -1 = inherit, 0 = false, 1 = true
    int32_t deadzone;          // -1 = inherit, else 0..32767
    int8_t curveType;          // -1 = inherit, else SensitivityCurveType
    float curveExponent;       // only meaningful when curveType == CURVE_EXPONENTIAL

    // Nom lisible du jeu (ex: "Tomb Raider"), facultatif - ajoute le
    // 2026-08-02 suite a un retour utilisateur reel ("comment comprendre que
    // c'est de ce jeu la qu'il s'agit si c'est pas ecrit"). Chaine vide =
    // aucun nom saisi, l'UI retombe alors sur l'affichage du Title ID en hex.
    // Taille fixe (pas de std::string) : ce struct est aussi construit par
    // rapidjson en mode big-endian pour la Xbox, un std::string y serait
    // fragile (allocation/byte-order) - deja la convention pour buttonMap/
    // keyMap ci-dessous (pointeurs bruts, pas de conteneur STL).
    char gameName[32];

    const HidButtonMapEntry* buttonMap;  // nullptr = inherit device's buttons
    uint8_t buttonMapCount;
    const HidKeyMapEntry* keyMap;        // nullptr = inherit device's keys
    uint8_t keyMapCount;

    // Anti-zone morte (2026-09-25) : -1 = herite du device, sinon 0..32767.
    // Ajoute EN DERNIER (meme raison que HidDeviceMapping::reportLayout).
    int32_t antiDeadzone;

    // Disposition des sticks choisie DANS LE JEU (2026-09-25), voir
    // StickLayout. 0 = standard (aucune permutation).
    int8_t stickLayout;

    // 2026-09-26, voir HidDeviceMapping::antiDeadzoneY / smoothingMs.
    // -1 = herite du device.
    int32_t antiDeadzoneY;
    int32_t smoothingMs;
};

// Disposition des sticks proposee par beaucoup de jeux (ex. Call of Duty :
// Default / Southpaw / Legacy / Legacy Southpaw). Le plugin produit toujours
// la disposition STANDARD : souris = stick droit (tourner/regarder), ZQSD =
// stick gauche (avancer/pas de cote). Si le jeu est regle autrement, la
// souris ferait marcher et le clavier tourner. On permute donc les axes pour
// correspondre au reglage du jeu :
//   Southpaw        : les deux sticks echanges ;
//   Legacy          : axes horizontaux echanges (tourner <-> pas de cote) ;
//   Legacy Southpaw : axes verticaux echanges (avancer <-> regarder).
// Les clics de stick (L3/R3) ne sont pas touches.
enum StickLayout {
    STICKS_DEFAULT = 0,
    STICKS_SOUTHPAW = 1,
    STICKS_LEGACY = 2,
    STICKS_LEGACY_SOUTHPAW = 3,
    STICKS_LAYOUT_COUNT = 4
};
void ApplyStickLayout(int16_t* lx, int16_t* ly, int16_t* rx, int16_t* ry, int layout);

// --- Disposition de rapport apprise par calibration (2026-08-03) ----------
// Le chemin rapide "boot protocol" lit chaque paquet souris a travers une
// structure FIGEE (BootMouseReport : boutons, X, Y, molette sur 4 octets).
// Toute souris dont la disposition native differe voyait donc son curseur et
// ses boutons corrompus - d'ou le contournement consistant a forcer le boot
// protocol, qui repare la disposition mais SUPPRIME la molette (prouve sur
// 425 releves : le 4e octet reste a zero en permanence).
//
// Lire le descripteur de rapport HID resoudrait le probleme proprement, mais
// passerait par la chaine USB asynchrone a l'origine de tous les gels
// historiques de ce projet. Idee de l'utilisateur, retenue parce qu'elle est
// meilleure : ne pas DEVINER la disposition, la faire DECOUVRIR par
// l'utilisateur - exactement le principe deja applique au clavier, ou le
// wizard ne devine pas quelle touche est "A", il la demande.
//
// L'assistant fait bouger/cliquer/scroller l'utilisateur, observe quels
// octets changent, et enregistre le resultat ici. Le plugin lit ensuite les
// champs a ces emplacements. Aucun acces au descripteur, donc aucun risque
// de gel. Bonus : cette disposition est publiable et partageable, indexee par
// VID/PID - un utilisateur calibre une fois, tous ceux qui ont le meme modele
// en profitent.
#define REPORT_FIELD_ABSENT 0xFF
struct HidReportLayout {
    bool     valid;          // false = aucune calibration, comportement historique
    uint8_t  reportSize;     // taille du paquet observee pendant la calibration
    uint8_t  buttonsOffset;  // octet portant les bits de boutons (REPORT_FIELD_ABSENT si aucun)
    uint8_t  xOffset;        // premier octet du champ X (REPORT_FIELD_ABSENT si aucun)
    uint8_t  xSize;          // 1 ou 2 octets
    uint8_t  yOffset;
    uint8_t  ySize;
    uint8_t  wheelOffset;    // REPORT_FIELD_ABSENT si cette souris n'emet pas de molette
    uint8_t  wheelSize;
};

struct HidDeviceMapping {
    uint16_t vendorId;
    uint16_t productId;

    const HidAxisMapEntry* axisMap;
    uint8_t axisMapCount;

    const HidButtonMapEntry* buttonMap;
    uint8_t buttonMapCount;

    HidAxisInvertFlags invert;

    // Optional per-device tuning for relative-motion HID devices (mice/trackballs,
    // see DetectIsMouse() in main.cpp). Ignored for regular gamepads.
    // mouseSensitivity == 0 means "use the built-in default" (see main.cpp).
    // When buttonMap is set for a mouse device, it overrides the built-in
    // left/right/middle -> r2/l2/r3 default (indices 0/1/2 are still the HID
    // button-page convention: left, right, middle).
    int32_t mouseSensitivity;
    bool invertMouseY;

    // Optional keyboard layout ("keys" in X360Remap.json). When empty, the
    // built-in default layout applies (WASD/Space/C/R/E..., revised
    // 2026-09-13 - see HidFillBootKeyboardState in main.cpp).
    const HidKeyMapEntry* keyMap;
    uint8_t keyMapCount;

    // Deadzone applied to the mouse-driven stick axis (0..32767, matches the
    // XInput short range). 0 = no deadzone, i.e. today's existing behavior.
    int32_t deadzone;

    uint8_t sensitivityCurveType;    // SensitivityCurveType, default CURVE_LINEAR
    float sensitivityCurveExponent;  // only used when type == CURVE_EXPONENTIAL

    // Per-game overrides. Empty = same behavior in every game (today's
    // existing behavior, unchanged).
    const HidDeviceProfile* profiles;
    uint8_t profileCount;

    // Disposition de rapport apprise par calibration (voir HidReportLayout
    // au-dessus). valid == false pour tout device jamais calibre : le
    // comportement reste alors strictement celui d'avant cette
    // fonctionnalite.
    //
    // AJOUTE EN FIN DE STRUCTURE, ET C'EST DELIBERE : les mappings statiques
    // en tete de mapping.cpp (manettes PlayStation, etc.) sont initialises
    // par listes POSITIONNELLES. Inserer un champ ailleurs qu'a la fin decale
    // silencieusement toutes leurs valeurs - un pointeur atterrit dans un
    // compteur, un compteur dans un booleen. Trouve immediatement par g++
    // ici, mais ce genre d'erreur passerait beaucoup plus discretement sur le
    // toolset Xbox. Tout nouveau champ doit donc etre ajoute ICI, en dernier.
    HidReportLayout reportLayout;

    // Resolution MESUREE de la souris, en points par centimetre (2026-09-23).
    // 0 = jamais mesuree : le plugin garde alors exactement son comportement
    // d'avant. Voir la normalisation dans XInputdReadStateHook : une souris
    // mesuree est ramenee a une reference commune (800 DPI), pour qu'une MEME
    // sensibilite donne le MEME ressenti quelle que soit la souris. Aucune
    // souris ne communique son DPI par USB - d'ou la mesure, faite une fois
    // dans la Studio (feuille A4). Ajoute EN DERNIER, meme raison que
    // reportLayout juste au-dessus.
    float countsPerCm;

    // ANTI-ZONE MORTE (2026-09-25), 0..32767, 0 = desactivee (comportement
    // d'avant). Les jeux ignorent les petites deviations du stick (leur
    // propre zone morte interne) : un petit geste de souris ne produit alors
    // RIEN, puis la camera part d'un coup. Des que la souris bouge, la sortie
    // demarre a cette valeur au lieu de 0 - elle saute par-dessus la zone
    // morte du jeu (meme principe que l'"inner deadzone" des adaptateurs
    // souris->manette du commerce). Ajoute EN DERNIER, meme raison que
    // reportLayout.
    int32_t antiDeadzone;

    // ANTI-ZONE MORTE VERTICALE (2026-09-26). Beaucoup de jeux (Call of Duty
    // en tete) ont une zone morte plus grande en vertical qu'en horizontal :
    // gauche/droite passe, haut/bas "colle" puis part d'un coup. Les
    // adaptateurs du commerce reglent donc X et Y separement (Cronus : 30/40
    // pour Black Ops). 0 = identique a antiDeadzone (horizontal).
    int32_t antiDeadzoneY;

    // LISSAGE TEMPOREL (2026-09-26), en millisecondes, 0 = desactive. La
    // souris envoie ses rapports a son rythme (125 a 1000 par seconde), le jeu
    // lit la manette au sien (30 ou 60 fois par seconde, parfois deux fois de
    // suite) : d'une lecture a l'autre, le nombre de rapports accumules varie
    // (1, 2, 3...) et la vitesse du stick saute d'autant - la camera saccade,
    // surtout a vitesse lente et reguliere. Le lissage moyenne les lectures des
    // derniers N ms : vitesse reguliere, rotation totale conservee, au prix
    // d'un retard d'environ N/2 ms. Meme role que le "Smoothing" de XIM et le
    // "Smoothness" de Cronus.
    int32_t smoothingMs;
};

struct DynamicMappingData {
    std::vector<HidAxisMapEntry> axisEntries;
    std::vector<HidButtonMapEntry> buttonEntries;
    std::vector<HidKeyMapEntry> keyEntries;
    std::vector<HidDeviceProfile> profileEntries;

    // Backing storage for each profile's own button/key overrides. Wrapped in
    // unique_ptr so a profile's buttonMap/keyMap pointer (handed out via
    // .data()) stays valid even as this outer vector itself grows/reallocates
    // - moving a vector moves its heap buffer's ownership, not the buffer
    // itself, but a unique_ptr makes that guarantee obvious at a glance
    // rather than relying on remembering that fact.
    std::vector<std::unique_ptr<std::vector<HidButtonMapEntry>>> profileButtonStorage;
    std::vector<std::unique_ptr<std::vector<HidKeyMapEntry>>> profileKeyStorage;
};

// Action name <-> enum, shared by the JSON parser, the serializer and (later)
// the on-console config UI.
uint8_t KeyActionFromName(const char* name);
const char* KeyActionToName(uint8_t action);

// Exposees pour application/config_writer.cpp (mapping souris des boutons,
// voir ApplyMouseButtonMapping) - jusqu'ici usage interne uniquement au
// parseur/serialiseur JSON de ce fichier. nullptr si le nom ne correspond a
// aucun champ de ButtonsReport (voir kButtonFieldNames dans mapping.cpp pour
// la liste complete des noms valides).
uint8_t ButtonsReport::* GetButtonFieldPtr(const char* name);
const char* GetButtonFieldName(uint8_t ButtonsReport::* ptr);

// Jalon 7 (application/ROADMAP_APPLICATION.md) - resolution des reglages
// souris effectifs pour la partie en cours. Le schema HidDeviceProfile
// (au-dessus) etait deja parse/serialise depuis/vers X360Remap.json, mais
// jamais consulte au runtime avant ce jalon - voir PROJECT_NOTES.md, section
// "Jalon 7".
struct EffectiveMouseSettings {
    int32_t mouseSensitivity;        // 0 = aucun override device/profil ; l'appelant applique alors son propre defaut code en dur (DEFAULT_MOUSE_SENSITIVITY dans main.cpp)
    bool invertMouseY;
    int32_t deadzone;                // 0 = pas de zone morte
    uint8_t sensitivityCurveType;    // SensitivityCurveType
    float sensitivityCurveExponent;  // valable seulement si sensitivityCurveType == CURVE_EXPONENTIAL
    int32_t antiDeadzone;            // 0 = desactivee (voir HidDeviceMapping::antiDeadzone)
    int32_t antiDeadzoneY;           // RESOLU : jamais 0 si antiDeadzone > 0 et Y non regle
    int32_t smoothingMs;             // 0 = pas de lissage
};

// nullptr si aucun profil de `map` ne correspond a `titleId`, si `map` est
// nul, ou si `titleId` vaut 0 (XamGetCurrentTitleId() renvoie 0 hors d'un
// jeu, par ex. depuis le dashboard - dans ce cas les reglages du device
// s'appliquent sans override, comportement inchange).
const HidDeviceProfile* FindActiveProfile(const HidDeviceMapping* map, uint32_t titleId);

// Combine les reglages de base du device et, si un profil correspond a
// titleId, ses overrides - meme convention de sentinelles que le schema JSON
// (voir HidDeviceProfile plus haut) : un champ de profil laisse a sa valeur
// "inherit" ne change pas la valeur du device.
EffectiveMouseSettings ResolveEffectiveMouseSettings(const HidDeviceMapping* map, uint32_t titleId);

// Applique la courbe de sensibilite configuree a un axe DEJA mis a l'echelle
// lineairement et clampe (plage -32768..32767, comme sThumbRX/RY). No-op
// (renvoie scaledValue tel quel) si curveType != CURVE_EXPONENTIAL. Pour
// CURVE_EXPONENTIAL : normalise en [-1,1], eleve la magnitude a la puissance
// curveExponent (signe preserve), puis remet a l'echelle - un exposant > 1.0
// donne plus de precision pres du centre (petits mouvements attenues) tout
// en atteignant toujours la deflexion max aux extremes (1^exposant == 1,
// quel que soit l'exposant).
int32_t ApplySensitivityCurve(int32_t scaledValue, uint8_t curveType, float curveExponent);

// 2026-08-02 (suite Jalon 7) - meme idee que ResolveEffectiveMouseSettings,
// mais pour les boutons/touches : jusqu'ici HidDeviceProfile::buttonMap/
// keyMap existaient dans le schema JSON (lus/ecrits, voir
// TestProfilesDeadzoneCurveRoundTrip) mais n'etaient JAMAIS consultes au
// runtime - tout le code de X360Remap/main.cpp lisait directement
// device->buttonMap/device->keyMap, ignorant tout profil actif. Retour
// utilisateur reel : vouloir associer RB au clic droit UNIQUEMENT pour un
// jeu precis, sans changer le mapping global a chaque partie.
//
// Convention "tout ou rien" (pas de fusion champ par champ comme
// ResolveEffectiveMouseSettings) : si le profil actif definit son PROPRE
// buttonMap/keyMap (count > 0), il remplace ENTIEREMENT celui du device pour
// ce jeu - sinon le device s'applique tel quel, comportement inchange pour
// tout profil qui ne configure pas ses boutons/touches. Coherent avec le
// commentaire deja present sur HidDeviceProfile::buttonMap ("nullptr =
// inherit device's buttons").
//
// *outCount recoit toujours le nombre d'entrees du tableau retourne (0 si
// aucune config device NI profil - le tableau retourne est alors nullptr,
// mais l'appelant doit se fier a *outCount, pas a un pointeur non-nul, avant
// d'iterer). map nul ou outCount nul -> *outCount (si possible) mis a 0,
// retourne nullptr.
const HidButtonMapEntry* ResolveEffectiveButtonMap(const HidDeviceMapping* map, uint32_t titleId, uint8_t* outCount);
const HidKeyMapEntry* ResolveEffectiveKeyMap(const HidDeviceMapping* map, uint32_t titleId, uint8_t* outCount);

// Resolution par FUSION - a preferer partout aux deux fonctions ci-dessus,
// qui remplacent la table du device au lieu de la surcharger et faisaient
// donc disparaitre toutes les commandes non redefinies par le profil (voir
// le commentaire detaille dans mapping.cpp). Renvoient le nombre d'entrees
// ecrites dans `out`. Tampon fourni par l'appelant : ces fonctions tournent
// dans le contexte USB, aucune allocation n'y est admissible.
#define MAX_MERGED_BUTTONS 16
#define MAX_MERGED_KEYS    96
uint8_t ResolveMergedButtonMap(const HidDeviceMapping* map, uint32_t titleId,
                                HidButtonMapEntry* out, uint8_t outCapacity);
uint8_t ResolveMergedKeyMap(const HidDeviceMapping* map, uint32_t titleId,
                             HidKeyMapEntry* out, uint8_t outCapacity);

// Cree/met a jour UNE SEULE entree du buttonMap ou keyMap du profil `titleId`
// du device vid/pid, sans toucher aux autres entrees ni aux reglages
// numeriques (mouseSensitivity/deadzone/etc, voir SetMouseProfile) deja
// presents sur ce profil - a l'inverse de SetMouseProfile qui remplace TOUT
// le profil, celles-ci ne touchent qu'UNE entree a la fois (par idx pour les
// boutons, par code HID pour les touches), pour permettre le "lier une seule
// touche a la volee" (raccourci in-game, voir X360Remap/main.cpp) sans
// ecraser un profil deja configure par ailleurs (sensibilite, autres
// boutons...). Cree le device ET le profil s'ils n'existent pas encore -
// mais AUCUN I/O (comme SetMouseProfile, l'appelant charge/sauvegarde le
// fichier lui-meme). false si titleId == 0.
bool SetProfileButtonOverride(uint16_t vid, uint16_t pid, uint32_t titleId,
                               uint8_t idx, uint8_t ButtonsReport::* field);
bool SetProfileKeyOverride(uint16_t vid, uint16_t pid, uint32_t titleId,
                            uint8_t code, uint8_t action);

// Cree ou remplace integralement le profil `titleId` du device vid/pid dans
// g_dynamicMappings (qui doit deja etre charge - LoadMappingsFromFile/Json -
// cette fonction ne fait AUCUN I/O elle-meme, seulement de la manipulation en
// memoire). Extrait ici plutot que laisse uniquement dans
// application/config_writer.cpp pour rester appelable a la fois depuis
// application.xex (ecran "Profils", Jalon 7) ET depuis X360Remap.xex
// directement (raccourci clavier "sauvegarder le profil depuis le jeu",
// meme jalon) sans dupliquer cette logique a deux endroits - une seule
// source de verite pour "qu'est-ce qu'enregistrer un profil veut dire".
// Cree le device s'il n'existe pas encore. Ne modifie PAS buttonMap/keyMap
// d'un profil existant (hors scope, voir HidDeviceProfile) - seulement les
// champs numeriques + gameName. false si titleId == 0 (voir FindActiveProfile).
// gameName : nullptr ou chaine vide = ne touche pas au nom existant du profil
// (utile pour le raccourci F9, qui n'a aucun moyen de saisir un nom depuis le
// jeu - voir X360Remap/main.cpp) ; une chaine non vide remplace le nom
// (utilise par l'ecran Profils de l'appli, ou l'utilisateur tape un nom).
// Tronque silencieusement a 31 caracteres + '\0' si plus long que gameName[32].
bool SetMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                      int32_t sensitivity, bool invertY, int32_t deadzone,
                      uint8_t curveType, float curveExponent,
                      const char* gameName = nullptr,
                      int32_t antiDeadzone = -2,    // -2 = ne pas toucher (F9), -1 = herite, sinon valeur
                      int stickLayout = -2,         // -2 = ne pas toucher, sinon StickLayout
                      int32_t antiDeadzoneY = -2,   // -2 = ne pas toucher, -1 = herite
                      int32_t smoothingMs = -2);    // -2 = ne pas toucher, -1 = herite

extern std::vector<HidDeviceMapping> g_dynamicMappings;
extern std::vector<std::unique_ptr<DynamicMappingData>> g_dynamicData;

HidDeviceMapping* FindStaticMapping(uint16_t vid, uint16_t pid);

// Anti-zone morte RADIALE (2026-09-25) : si (x,y) != (0,0), la longueur du
// vecteur est ramenee de [0..32767] vers [antiDeadzone..32767] en gardant la
// direction. Aucun effet si antiDeadzone <= 0 ou si le stick est au repos.
// Radiale et non par axe : la plupart des jeux ont une zone morte ronde, et
// un decalage par axe deformerait les diagonales. Pas d'allocation, appelable
// depuis le hook de lecture.
// 2026-09-26 : seuils SEPARES horizontal/vertical. Le seuil suit la direction
// du geste (horizontal pur -> adX, vertical pur -> adY, entre les deux :
// interpolation elliptique), et la direction est conservee - un petit
// tremblement vertical pendant un geste horizontal reste petit.
void ApplyAntiDeadzone(int32_t* x, int32_t* y, int32_t adX, int32_t adY);

// Lissage temporel (2026-09-26, voir HidDeviceMapping::smoothingMs). Etat par
// souris, zero-initialisable (memset) : aucun constructeur, aucune allocation.
//
// Principe : chaque deplacement lu n'est pas rendu d'un coup, il est etale
// sur `windowMs` millisecondes, au prorata du temps ecoule entre deux lectures
// du jeu. Deux lectures a 60/s et 34 ms : 50 % a la lecture ou il arrive,
// 50 % a la suivante. Le TOTAL rendu est exactement le total recu (rotation
// conservee), quelle que soit la cadence de lecture du jeu.
#define SMOOTH_SLOTS 16
struct SmoothingState {
    float    totX[SMOOTH_SLOTS], totY[SMOOTH_SLOTS];   // deplacement d'origine
    float    remX[SMOOTH_SLOTS], remY[SMOOTH_SLOTS];   // reste a rendre
    uint8_t  used[SMOOTH_SLOTS];
    uint32_t lastT;                                     // heure de la lecture precedente (0 = aucune)
    float    fracX, fracY;                              // restes d'arrondi
};
// A appeler a CHAQUE lecture du jeu avec le deplacement brut de cette
// lecture ; *dx/*dy sont remplaces par la part a rendre maintenant.
// windowMs <= 0 : rien n'est modifie (et ce qui restait en attente est rendu
// tout de suite, pour qu'une desactivation ne perde rien).
void SmoothMouseDelta(SmoothingState* st, uint32_t nowMs, int32_t* dx, int32_t* dy, int32_t windowMs);

HidDeviceMapping* FindMapping(uint16_t vid, uint16_t pid);

bool LoadMappingsFromJson(const std::string& jsonString);
bool LoadMappingsFromFile(const std::string& filepath);
std::string SaveMappingsToJson();

// Ecriture avec COPIE DE SECOURS (revue le 2026-09-23, voir mapping.cpp) :
// le contenu est d'abord ecrit dans "<fichier>.tmp", puis l'original est
// reecrit directement. Le .tmp n'est supprime que si cette ecriture a reussi.
// Jamais de suppression ni de renommage de l'original : la premiere version
// le faisait, et a fait disparaitre X360Remap.json chez HB.
bool SaveMappingsToFile(const std::string& filepath);

// Si l'original est absent ou illisible et qu'une copie de secours VALIDE
// existe, reconstruit l'original depuis elle. true si une reparation a eu
// lieu. A appeler au demarrage (plugin et Studio) et avant toute mise a jour.
bool RecoverConfigFromTmp(const std::string& filepath);

// A appeler AVANT toute modification suivie d'une sauvegarde (AUDIT I4).
//
// Renvoie false si le fichier EXISTE mais ne peut pas etre lu (syntaxe
// cassee, fichier en cours d'ecriture...) : l'appelant NE DOIT PAS
// sauvegarder dans ce cas. Avant ce correctif, l'echec de lecture etait
// ignore et la sauvegarde qui suivait ecrasait le fichier avec l'etat en
// memoire - ancien, voire vide - effacant la configuration de l'utilisateur.
//
// Renvoie true si le fichier a ete lu, ou s'il est simplement ABSENT
// (premier lancement : on part de l'etat en memoire, comme avant).
// Appelle d'abord RecoverConfigFromTmp.
bool LoadMappingsForUpdate(const std::string& filepath);

// Vide les tables dynamiques. La generation remplacee n'est PAS liberee
// immediatement (AUDIT_2026-09.md, C4) : elle est conservee jusqu'au
// prochain appel. Un gestionnaire USB qui avait lu un pointeur Controller::map
// juste avant le rechargement continue ainsi de lire une memoire valide, au
// lieu d'une memoire liberee. Meme principe que QueueDriverFree dans
// X360Remap/main.cpp. Cout : une copie de la configuration en memoire, soit
// quelques kilo-octets.
void ClearDynamicMappings();
