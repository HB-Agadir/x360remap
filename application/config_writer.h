#pragma once
#include "../X360Remap/mapping.h"
#include <vector>
#include <stdint.h>

// Loads HDD:\X360Remap.json, replaces (or adds) the keyboard mapping for the
// given device, and saves it back. Reuses the exact same
// LoadMappingsFromFile / g_dynamicMappings / SaveMappingsToFile machinery
// plugin.xex already uses for its own hot-reload, so the file this writes is
// read back correctly by the plugin without a reboot - same JSON schema,
// already covered by X360Remap/tests/.
bool ApplyKeyboardMapping(uint16_t vid, uint16_t pid, const std::vector<HidKeyMapEntry>& keys);

// Test seam: production code always uses HDD:\X360Remap.json (the default
// argument). Tests pass a temp file path instead, since HDD:\ doesn't exist
// off-console.
bool ApplyKeyboardMapping(uint16_t vid, uint16_t pid, const std::vector<HidKeyMapEntry>& keys,
                           const char* configPath);

// Reglages manuels (2026-08-17, demande HB : "a chaque click sur un button
// l'utilisateur choisi une touche clavier", pour eviter de refaire les 21
// etapes du wizard pour changer UN seul bouton). A LA DIFFERENCE
// d'ApplyKeyboardMapping ci-dessus (qui remplace TOUT le keyMap du device -
// c'est le contrat du wizard sequentiel, qui reconstruit la liste entiere a
// chaque session), cette fonction charge ce qui existe deja sur le disque,
// remplace seulement l'entree dont l'action correspond (ou l'ajoute si
// absente), et resauvegarde la liste complete. Sans cette fusion, assigner
// une seule touche depuis les reglages manuels effacerait silencieusement
// les 20 autres deja enregistrees.
bool ApplyKeyboardMappingEntry(uint16_t vid, uint16_t pid, const HidKeyMapEntry& entry);
bool ApplyKeyboardMappingEntry(uint16_t vid, uint16_t pid, const HidKeyMapEntry& entry,
                                const char* configPath);

// Same idea for the mouse settings screen (sensitivity slider, deadzone
// slider, invert-Y checkbox in application.xex's ImGui UI). sensitivity <= 0
// or deadzone < 0 leave the corresponding field untouched (matches the
// mapping.h "0/negative sentinel means inherit/default" convention already
// used throughout this schema).
bool ApplyMouseSettings(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone);
// Variante avec l'anti-zone morte (2026-09-25, voir HidDeviceMapping::antiDeadzone
// dans mapping.h) : antiDeadzone < 0 = ne pas toucher, sinon 0..32767 (0 = desactivee).
// antiDeadzoneY / smoothingMs (2026-09-26) : < 0 = ne pas toucher.
bool ApplyMouseSettings(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone,
                         int32_t antiDeadzone, int32_t antiDeadzoneY = -1, int32_t smoothingMs = -1);
bool ApplyMouseSettingsFull(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone,
                             int32_t antiDeadzone, const char* configPath,
                             int32_t antiDeadzoneY = -1, int32_t smoothingMs = -1);
bool ApplyMouseSettings(uint16_t vid, uint16_t pid, int32_t sensitivity, bool invertY, int32_t deadzone,
                         const char* configPath);

// Reassigne les clics souris (gauche/droit/milieu = idx 0/1/2, convention HID
// boot-mouse - voir HidFillBootMouseState dans X360Remap/main.cpp) a un
// bouton Xbox. fieldNames[i] doit etre un nom valide de kButtonFieldNames
// (mapping.cpp) - "a_button", "l2", "start", etc. Un nom invalide/inconnu
// pour un idx donne fait sauter cette entree plutot que d'ecrire un pointeur
// null (voir GetButtonFieldPtr). Meme mecanique de remplacement complet que
// ApplyKeyboardMapping (charge, remplace l'entree "buttons" de ce device,
// resauvegarde tout le fichier).
struct MouseButtonBinding {
    uint8_t idx;
    const char* fieldName;
};
bool ApplyMouseButtonMapping(uint16_t vid, uint16_t pid, const std::vector<MouseButtonBinding>& bindings);
bool ApplyMouseButtonMapping(uint16_t vid, uint16_t pid, const std::vector<MouseButtonBinding>& bindings,
                              const char* configPath);

// Diagnostic (2026-08-03) - relit X360Remap.json depuis le disque et renvoie
// le buttonMapCount REEL du device (pas ce que l'appel de sauvegarde vient
// de calculer en memoire). Sert a verifier, juste apres un
// ApplyMouseButtonMapping, que ce qui a ete demande est vraiment ce qui a
// atterri sur le disque - voir l'appel depuis DrawMouseSettingsTab
// (application/main.cpp) et PROJECT_NOTES.md, section molette du 2026-08-03.
// -1 si le device n'existe pas (encore) dans le fichier.
int GetMouseButtonMapCountOnDisk(uint16_t vid, uint16_t pid);
int GetMouseButtonMapCountOnDisk(uint16_t vid, uint16_t pid, const char* configPath);

// Enregistre la disposition de rapport apprise par l'assistant de calibration
// (voir X360Remap/mouse_calibration.h). Le plugin lira ensuite X/Y/molette/
// boutons a ces offsets au lieu d'appliquer la structure figee BootMouseReport
// - c'est ce qui permet de faire fonctionner une souris dont le paquet ne suit
// pas la disposition legacy, sans forcer le boot protocol et donc sans lui
// coûter sa molette.
// Refuse une disposition invalide (IsLayoutUsable) plutot que d'ecrire des
// offsets qui feraient lire le plugin hors du paquet.
// Enregistre la resolution MESUREE de la souris (points par centimetre) -
// voir HidDeviceMapping::countsPerCm. false si la valeur est hors bornes
// (mesure ratee) ou si le fichier existe mais ne se lit pas (AUDIT I4).
bool ApplyMouseCountsPerCm(uint16_t vid, uint16_t pid, float countsPerCm);
bool ApplyMouseCountsPerCm(uint16_t vid, uint16_t pid, float countsPerCm, const char* configPath);

bool ApplyMouseReportLayout(uint16_t vid, uint16_t pid, const HidReportLayout& layout);
bool ApplyMouseReportLayout(uint16_t vid, uint16_t pid, const HidReportLayout& layout,
                             const char* configPath);

// Efface la calibration d'un peripherique : il repasse au comportement
// historique (structure figee + forcage boot protocol si necessaire). Utile si
// une calibration s'avere pire que le defaut.
bool ClearMouseReportLayout(uint16_t vid, uint16_t pid);
bool ClearMouseReportLayout(uint16_t vid, uint16_t pid, const char* configPath);

// Reglages par defaut ("comme un BIOS") : copie l'etat courant de
// X360Remap.json de cote (SaveConfigAsDefault), ou le restaure plus tard
// (RestoreConfigDefault) si un reglage recent ne convient pas. Simple copie
// de fichier - pas de reparsing JSON necessaire, donc pas de risque de
// corrompre le contenu en le recopiant.
bool SaveConfigAsDefault();
bool SaveConfigAsDefault(const char* configPath, const char* backupPath);

// false si aucun defaut n'a jamais ete sauvegarde (backupPath absent) - ne
// touche pas a configPath dans ce cas.
bool RestoreConfigDefault();
bool RestoreConfigDefault(const char* configPath, const char* backupPath);

// Pour griser/masquer le bouton "Restaurer" dans l'UI tant qu'aucun defaut
// n'a ete sauvegarde.
bool HasConfigDefault();

// Reglages d'ORIGINE (2026-09-25) : configuration vide "[]", exactement
// l'etat d'une installation neuve - le plugin applique alors ses commandes
// FPS integrees (WASD, Espace = A...). La configuration actuelle est d'abord
// copiee dans X360Remap.json.tmp (meme filet que SaveMappingsToFile).
bool RestoreFactoryConfig();
bool RestoreFactoryConfig(const char* configPath);
bool HasConfigDefault(const char* backupPath);

// Jalon 7 (application/ROADMAP_APPLICATION.md) - profils par jeu (deadzone,
// sensibilite, courbe) pour l'onglet "Profils" de application.xex. Le
// schema JSON existait deja et etait deja lu/ecrit correctement (voir
// X360Remap/tests/test_mapping_assistant.cpp, TestProfilesDeadzoneCurveRoundTrip)
// - ce qui manquait etait 1) le runtime du plugin qui applique le profil actif
// (voir ResolveEffectiveMouseSettings dans X360Remap/mapping.h, cable dans
// X360Remap/main.cpp), et 2) cet ecran pour creer/modifier un profil sans
// editer X360Remap.json a la main.
//
// Champs avec la meme convention de sentinelles que HidDeviceProfile
// (mapping.h) : mouseSensitivity 0 = herite du device ; invertMouseY -1 =
// herite/0 = false/1 = true ; deadzone -1 = herite ; curveType -1 = herite
// (sinon SensitivityCurveType, voir mapping.h).
struct MouseProfileInfo {
    uint32_t titleId;
    // Nom lisible saisi par l'utilisateur (ex: "Tomb Raider"), chaine vide si
    // jamais renseigne - voir HidDeviceProfile::gameName dans mapping.h pour
    // la convention complete (2026-08-02, retour utilisateur reel).
    char gameName[32];
    int32_t mouseSensitivity;
    int8_t invertMouseY;
    int32_t deadzone;
    int8_t curveType;
    float curveExponent;
    int32_t antiDeadzone;   // -1 = herite du device (2026-09-25)
    int8_t  stickLayout;    // StickLayout (mapping.h), 0 = standard
    int32_t antiDeadzoneY;  // -1 = herite, 0 = identique a l'horizontal (2026-09-26)
    int32_t smoothingMs;    // -1 = herite
};

// Liste les profils deja enregistres pour ce device, tels quels (sentinelles
// non resolues - a l'ecran, un champ "herite" doit se lire comme "identique
// au reglage global affiche dans l'onglet Reglages souris"). Vide si le
// device n'a aucun profil ou n'existe pas encore dans le fichier.
std::vector<MouseProfileInfo> ListMouseProfiles(uint16_t vid, uint16_t pid);
std::vector<MouseProfileInfo> ListMouseProfiles(uint16_t vid, uint16_t pid, const char* configPath);

// Cree (si titleId inconnu pour ce device) ou remplace integralement (si deja
// present) un profil - PAS de fusion partielle : contrairement a
// ApplyMouseSettings, chaque appel ecrit les 5 valeurs explicitement (pas de
// notion "laisser inchange" cote UI, le formulaire affiche toujours des
// valeurs concretes). curveType != CURVE_EXPONENTIAL efface toute courbe pour
// ce profil (retombe sur le reglage du device). titleId == 0 echoue
// (0 = "hors d'un jeu", ne peut pas etre la cle d'un profil, voir
// FindActiveProfile dans mapping.h).
// gameName : nullptr/chaine vide = ne touche pas au nom deja enregistre pour
// ce profil (meme convention que SetMouseProfile, voir X360Remap/mapping.h) -
// une chaine non vide le remplace. Parametre par defaut nullptr pour ne pas
// casser les appels existants qui ne geraient pas encore de nom.
bool ApplyMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                        int32_t sensitivity, bool invertY, int32_t deadzone,
                        uint8_t curveType, float curveExponent,
                        const char* gameName = nullptr);
bool ApplyMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                        int32_t sensitivity, bool invertY, int32_t deadzone,
                        uint8_t curveType, float curveExponent,
                        const char* gameName,
                        const char* configPath);
// Variante avec l'anti-zone morte du profil (2026-09-25) : -1 = herite du
// device, sinon 0..32767. Les variantes ci-dessus ne la modifient PAS.
bool ApplyMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                        int32_t sensitivity, bool invertY, int32_t deadzone,
                        uint8_t curveType, float curveExponent,
                        const char* gameName, int32_t antiDeadzone,
                        int stickLayout = -2,    // -2 = ne pas toucher
                        int32_t antiDeadzoneY = -2, int32_t smoothingMs = -2);
bool ApplyMouseProfileFull(uint16_t vid, uint16_t pid, uint32_t titleId,
                            int32_t sensitivity, bool invertY, int32_t deadzone,
                            uint8_t curveType, float curveExponent,
                            const char* gameName, int32_t antiDeadzone,
                            const char* configPath, int stickLayout = -2,
                            int32_t antiDeadzoneY = -2, int32_t smoothingMs = -2);

// Supprime le profil de ce titleId pour ce device (le jeu retombe sur les
// reglages globaux du device). false si le device ou le profil n'existe pas.
bool RemoveMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId);
bool RemoveMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId, const char* configPath);

// Historique des jeux vus par le plugin (X360Remap/known_titles.h) - pour
// que l'ecran "Profils" propose une liste au lieu de faire chercher/taper le
// Title ID a la main (demande utilisateur du 2026-08-02). Vide si le fichier
// n'existe pas encore (aucun jeu lance avec le plugin actif depuis
// l'installation, ou fraichement deplace vers X360RemapStudio).
std::vector<uint32_t> ListKnownTitleIds();
std::vector<uint32_t> ListKnownTitleIds(const char* path);

// Jalon 8 (application/ROADMAP_APPLICATION.md) - remplace les VID/PID en dur
// (kKeyboardVid/kKeyboardPid/kMouseVid/kMousePid dans application/main.cpp)
// par un vrai ecran de selection. PREMIERE VERSION (2026-08-03, abandonnee le
// jour meme) : classait les devices par heuristique sur ce qui avait deja ete
// ECRIT dans X360Remap.json par cette appli - ne listait donc RIEN pour un
// peripherique jamais configure via le wizard/reglages souris, ce qui s'est
// revele bloquant des le premier vrai test (deuxieme souris jamais proposee).
// Retour utilisateur explicite : une saisie manuelle de VID/PID serait "une
// mauvaise idee", la detection doit se faire "au branchement sur la
// console". Cette version lit donc `known_devices.txt`
// (X360Remap/known_devices.h) - un historique que X360Remap.xex alimente
// lui-meme des qu'il classe un device en souris/clavier a l'enumeration USB
// (DetectIsMouse/DetectIsKeyboard, X360Remap/main.cpp), AVANT toute
// configuration explicite. isMouse est donc la classification REELLE faite
// par le plugin, pas une heuristique cote application. Fichier absent (aucun
// device jamais vu par le plugin depuis l'installation) -> liste vide, pas
// une erreur - le repli "Par defaut" (kKeyboardVid/kMouseVid) reste toujours
// propose en plus de cette liste cote UI (voir application/main.cpp).
struct KnownDeviceInfo {
    uint16_t vid;
    uint16_t pid;
    bool isMouse; // false = clavier
    // Nom declare par le peripherique (descripteur de chaine USB), recupere
    // par le plugin. Chaine vide s'il n'en declare pas ou si le fichier a ete
    // ecrit par une version anterieure : l'affichage retombe sur le VID:PID.
    char name[32];
};
std::vector<KnownDeviceInfo> ListKnownDevices();
std::vector<KnownDeviceInfo> ListKnownDevices(const char* knownDevicesPath);
