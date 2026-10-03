#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// Historique des peripheriques (souris/clavier) vus par le plugin (Jalon 8,
// suite - retour utilisateur explicite du 2026-08-03 : la premiere version de
// l'ecran de selection d'application.xex ne listait que les devices deja
// CONFIGURES via l'appli (heuristique sur X360Remap.json), donc rien pour un
// peripherique jamais touche par le wizard/reglages souris - et une saisie
// manuelle de VID/PID a ete explicitement rejetee ("le mieux c'est que cela
// ce fait a la detection du branchement sur la console"). Le plugin classe
// deja chaque device en souris/clavier a l'enumeration USB (DetectIsMouse/
// DetectIsKeyboard, voir X360Remap/main.cpp) - cette classification n'etait
// jusqu'ici jamais persistee, seulement affichee via notification/log. Ce
// format la capture directement : c'est la source de verite reelle (pas une
// heuristique cote application comme la premiere version de
// application/config_writer.h::ListKnownDevices()).
//
// Format volontairement simple (pas de JSON), meme esprit que
// known_titles.txt (Jalon 7) : une ligne par device, "VVVV:PPPP:TYPE\n" ou
// VVVV/PPPP sont 4 chiffres hexadecimaux (vid/pid) et TYPE vaut "M" (souris)
// ou "K" (clavier).
//
// Logique pure (aucune API Xbox, aucun I/O disque ici) - testee en g++ dans
// X360Remap/tests/test_mapping_assistant.cpp avant integration (methode
// habituelle de ce projet). Le vrai I/O reste cote appelant :
// X360Remap/main.cpp (MappingManagerThreadProc, thread deja utilise pour
// known_titles.txt/logs/hot-reload - le fichier est ECRIT uniquement par le
// plugin, jamais par application.xex, qui n'a lui-meme aucun acces USB brut
// pour detecter quoi que ce soit) et application/config_writer.cpp (lecture
// seule, pour peupler l'ecran de selection).
struct KnownDeviceEntry {
    uint16_t vid;
    uint16_t pid;
    bool isMouse; // false = clavier
    // Nom lisible declare par le peripherique lui-meme (descripteur de chaine
    // USB, index iProduct), ajoute le 2026-08-03 sur remarque de l'utilisateur :
    // "pourquoi lui afficher les peripheriques par hex au lieu de nom". On
    // interroge deja le peripherique pour savoir lire ses donnees, lui demander
    // son nom au passage ne coute presque rien et evite d'afficher un matricule
    // a quelqu'un qui veut juste choisir sa souris.
    // Chaine vide si le peripherique n'en declare pas, ou si la version du
    // plugin qui a ecrit le fichier ne le recuperait pas encore - l'affichage
    // retombe alors sur le VID:PID.
    char name[32];
};

// Parse un contenu existant en liste de devices. Une ligne invalide/vide est
// ignoree plutot que de faire echouer tout le parsing - meme tolerance que
// ParseKnownTitleIds (known_titles.h).
std::vector<KnownDeviceEntry> ParseKnownDevices(const std::string& content);

// Serialise la liste dans le meme format (une ligne "VVVV:PPPP:TYPE" par
// device).
std::string SerializeKnownDevices(const std::vector<KnownDeviceEntry>& devices);

// Ajoute (vid, pid, isMouse) a la liste s'il n'y est pas deja (cle = vid+pid
// uniquement - un meme VID/PID reclassifie differemment d'un boot a l'autre,
// tres improbable en pratique, garde son classement d'origine plutot que
// d'ecrire une seconde entree contradictoire). Renvoie true si la liste a ete
// modifiee (l'appelant doit alors reserialiser/reecrire le fichier), false si
// deja present (evite une ecriture disque a chaque tick pour un device deja
// vu) - meme convention que AddKnownTitleId.
bool AddKnownDevice(std::vector<KnownDeviceEntry>* devices, uint16_t vid, uint16_t pid, bool isMouse);

// Variante avec le nom lisible du peripherique. Renvoie true si la liste a ete
// MODIFIEE - donc aussi quand un peripherique deja connu se voit completer son
// nom, cas d'un fichier ecrit par une version anterieure du plugin. L'appelant
// doit alors reecrire le fichier.
bool AddKnownDevice(std::vector<KnownDeviceEntry>* devices, uint16_t vid, uint16_t pid,
                    bool isMouse, const char* name);
