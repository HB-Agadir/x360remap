#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// Historique des jeux vus par le plugin (Jalon 7, suite - demande
// utilisateur du 2026-08-02 : "ce n'est pas a l'utilisateur de chercher [le
// Title ID]"). Le plugin detecte deja chaque changement de jeu en temps reel
// (g_lastTitleId dans X360Remap/main.cpp, deja confirme sur hardware) - ce
// petit format persiste chaque NOUVEAU titleId vu, pour qu'application.xex
// puisse peupler l'ecran "Profils" sans saisie manuelle. Limite assumee : un
// jeu doit avoir ete lance au moins une fois (plugin actif) pour apparaitre -
// pas de scan du contenu du disque dur (plus risque, hors scope ici).
//
// Format volontairement simple (pas de JSON) : un titleId hexadecimal 8
// caracteres par ligne, fin de ligne "\n". Choisi plutot que de reutiliser
// rapidjson comme X360Remap.json - cette liste ne sert qu'a une chose (eviter
// les doublons, lister), pas besoin de parser/reserialiser tout un document a
// chaque changement de jeu pour un gain de simplicite negligeable.
//
// Logique pure (aucune API Xbox, aucun I/O disque ici) - testee en g++ dans
// X360Remap/tests/test_mapping_assistant.cpp avant integration (methode
// habituelle de ce projet). Le vrai I/O (lecture/ecriture du fichier) reste
// cote appelant : X360Remap/main.cpp (MappingManagerThreadProc, thread deja
// utilise pour tout I/O disque sur hot-reload/logs) et
// application/config_writer.cpp (ecran Profils).

// Parse un contenu existant en liste de titleId. Une ligne invalide/vide est
// ignoree plutot que de faire echouer tout le parsing - un fichier
// partiellement corrompu ne doit pas faire perdre tout l'historique.
std::vector<uint32_t> ParseKnownTitleIds(const std::string& content);

// Serialise la liste dans le meme format (un titleId hexadecimal par ligne).
std::string SerializeKnownTitleIds(const std::vector<uint32_t>& titleIds);

// Ajoute titleId a la liste s'il n'y est pas deja. Renvoie true si la liste a
// ete modifiee (l'appelant doit alors reserialiser/reecrire le fichier),
// false si titleId y figurait deja (rien a faire, evite une ecriture disque
// inutile a chaque frame ou le meme jeu reste actif) ou si titleId == 0
// ("hors d'un jeu", jamais une entree valide - voir FindActiveProfile dans
// mapping.h).
bool AddKnownTitleId(std::vector<uint32_t>* titleIds, uint32_t titleId);
