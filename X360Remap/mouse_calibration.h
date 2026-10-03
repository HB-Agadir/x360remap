#pragma once
#include "mapping.h"
#include <stdint.h>
#include <vector>

// --- Moteur de calibration souris (2026-08-03) ----------------------------
//
// Idee de l'utilisateur, retenue parce qu'elle est meilleure que la mienne :
// plutot que de deviner la disposition d'un paquet souris (impossible depuis
// le VID/PID) ou de lire le descripteur HID (possible mais via la chaine USB
// asynchrone responsable de tous les gels historiques du projet), on la fait
// DECOUVRIR par l'utilisateur. C'est exactement le principe du wizard clavier
// deja en place : il ne devine pas quelle touche est "A", il la demande.
//
// Deroulement : l'assistant demande une action precise ("bouge la souris vers
// la droite", "clique gauche", "molette vers l'avant"), collecte les paquets
// bruts recus pendant cette action, et compare a l'etat de repos. L'octet -
// ou la paire d'octets - qui varie de facon coherente EST le champ recherche.
//
// Tout ce fichier est de la logique pure : aucune API Xbox, aucun acces
// materiel. Il est compile et execute pour de vrai en g++ dans le bac a
// sable (X360Remap/tests/test_mouse_calibration.cpp), avec des paquets
// synthetiques reproduisant des dispositions reelles - 8 bits, 16 bits
// petit-boutiste, champs signes, octet d'identifiant de rapport en tete.

// Un paquet brut observe, tel que capture par le plugin.
struct CalibrationSample {
    uint8_t bytes[16];
    uint8_t length;
};

// Ce qu'on cherche a chaque etape de l'assistant.
enum CalibrationTarget {
    CALIB_AXIS_X = 0,   // "bouge vers la droite" puis "vers la gauche"
    CALIB_AXIS_Y,       // "bouge vers le bas" puis "vers le haut"
    CALIB_WHEEL,        // "molette vers l'avant" puis "vers l'arriere"
    CALIB_BUTTON        // "clique et maintiens"
};

// Resultat de la detection d'UN champ.
struct DetectedField {
    bool    found;
    uint8_t offset;    // premier octet du champ
    uint8_t size;      // 1 ou 2 octets
    uint8_t bitIndex;  // uniquement pour CALIB_BUTTON : bit allume dans l'octet
};

// Etat de repos : octets qui ne varient jamais quand l'utilisateur ne touche
// a rien. Sert de reference pour tout le reste, et permet d'ignorer les
// octets constants (identifiant de rapport, remplissage) qui feraient
// autrement de faux positifs.
struct CalibrationBaseline {
    uint8_t bytes[16];
    uint8_t length;
    // Amplitude maximale observee au repos, octet par octet. REMPLACE le
    // critere binaire "stable / instable" de la premiere version, qui
    // disqualifiait un octet des qu'il frémissait une seule fois au repos.
    // Defaut reel constate sur hardware (2026-08-03) : une souris optique
    // n'est jamais parfaitement immobile - la main la frole, le capteur
    // bruite - et ce sont justement les octets X et Y qui bougent le plus.
    // Ils etaient donc exclus d'office, et AUCUNE disposition n'etait
    // detectee. On retient desormais le bruit de fond de chaque octet, et un
    // geste ne compte que s'il le depasse franchement.
    uint8_t jitter[16];
    bool    stable[16];  // conserve pour lisibilite : jitter == 0
};

// Construit l'etat de repos a partir de plusieurs paquets captures pendant
// que l'utilisateur ne touche a rien. Un octet est declare instable des qu'il
// varie - typiquement le bruit d'un capteur optique tres sensible, qu'il ne
// faut surtout pas prendre pour un champ.
CalibrationBaseline BuildBaseline(const std::vector<CalibrationSample>& samples);

// Detecte le champ correspondant a `target` a partir des paquets captures
// pendant l'action demandee. `positive` contient les paquets de l'action dans
// un sens (droite / bas / molette avant / bouton enfonce) ; `negative` ceux du
// sens oppose, vide pour un bouton.
//
// Pour un axe ou la molette : on cherche l'octet (ou la paire) dont la valeur
// signee varie de facon coherente et de signe oppose entre les deux sens -
// c'est ce qui distingue un vrai champ d'un octet qui bouge par hasard.
// Pour un bouton : on cherche un bit qui passe a 1 et le reste.
// `exclude` : tableau de 16 booleens marquant les octets DEJA attribues a un
// champ precedemment detecte. nullptr = aucune exclusion.
//
// Indispensable, et signale par l'utilisateur apres un test reel : pour
// tourner la molette, la main fait forcement bouger la souris - les octets X
// et Y changent donc pendant l'etape molette, et la detection les prenait
// pour la molette. On detecte desormais dans l'ordre X, Y, molette, bouton,
// en retirant a chaque fois les octets deja pris.
DetectedField DetectField(const CalibrationBaseline& baseline,
                          const std::vector<CalibrationSample>& positive,
                          const std::vector<CalibrationSample>& negative,
                          CalibrationTarget target,
                          const bool* exclude);

// Marque dans `exclude` les octets occupes par un champ detecte, pour que les
// detections suivantes les ignorent.
void MarkFieldExcluded(const DetectedField& field, bool* exclude);

// Assemble une disposition complete a partir des champs detectes. Un champ
// non trouve est enregistre comme absent (REPORT_FIELD_ABSENT) plutot que de
// faire echouer l'ensemble : une souris sans molette reste parfaitement
// utilisable, et il vaut mieux une disposition partielle correcte qu'un rejet
// total.
HidReportLayout BuildLayout(uint8_t reportSize,
                            const DetectedField& x,
                            const DetectedField& y,
                            const DetectedField& wheel,
                            const DetectedField& button);

// Verifie qu'une disposition est exploitable par le plugin : X et Y presents,
// tailles 1 ou 2, offsets contenus dans le paquet. Sans ca, une disposition
// incoherente ecrite dans X360Remap.json ferait lire le plugin hors des
// limites du paquet.
bool IsLayoutUsable(const HidReportLayout& layout);

// Lit un champ signe (1 ou 2 octets, petit-boutiste) a l'offset donne.
// Utilisee par le plugin au runtime ET par les tests, pour garantir que la
// lecture testee est exactement celle executee sur console.
int32_t ReadSignedField(const uint8_t* payload, uint8_t length,
                        uint8_t offset, uint8_t size);
