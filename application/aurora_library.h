#pragma once
// ============================================================================
// BIBLIOTHEQUE AURORA - noms exacts et pochettes des jeux (2026-09-25)
//
// Demande HB : "les covers des jeux, les noms exacts des jeux" sur la page
// Profils de jeux. Aurora (le dashboard) a deja tout sur le disque :
//
//   <Aurora>\Data\Databases\content.db          SQLite : TitleId -> TitleName
//   <Aurora>\Data\GameData\<TID>_<...>\GC<TID>.asset   pochette (format Aurora)
//
// Ce fichier ne fait QUE decoder des octets deja lus en memoire : il est
// PORTABLE (aucun xtl.h), donc teste sur PC (tests/test_aurora_library.cpp).
// La recherche des fichiers sur la console est dans main.cpp.
//
// LECTURE SEULE, toujours : on ne modifie jamais les fichiers d'Aurora.
//
// Sources du format :
//  - .asset : libaustralis (jrobiche, GPL-3.0) - en-tete 0x800 octets,
//    25 entrees de 0x40 octets, texture GPU Xbox 360 (BC3 ou RGBA8).
//  - content.db : format de fichier SQLite 3 documente (sqlite.org/fileformat2).
//    Lecteur minimal ecrit ici (parcours de table uniquement) plutot que
//    d'embarquer SQLite entier dans le .xex.
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct AuroraTitle {
    uint32_t    titleId;
    std::string name;   // UTF-8, tel qu'Aurora l'affiche
};

// Parcourt content.db et renvoie chaque (TitleId, TitleName) trouve. La table
// et les colonnes sont retrouvees par leur NOM dans le schema (sqlite_master),
// pas par leur position : un Aurora plus recent qui ajoute des colonnes reste
// lisible. Les doublons (meme TitleId, plusieurs disques) sont gardes une fois.
// false si le fichier n'est pas une base SQLite lisible.
bool ReadAuroraTitles(const unsigned char* db, size_t len, std::vector<AuroraTitle>& out);

// Types d'image d'un fichier .asset (index dans l'en-tete).
enum AuroraAssetType { AURORA_ASSET_ICON = 0, AURORA_ASSET_BANNER = 1, AURORA_ASSET_BOXART = 2 };

// Decode une image d'un fichier .asset en RGBA 8 bits, reduite (en gardant
// ses proportions) pour tenir dans maxW x maxH - une pochette 900x1233 pleine
// taille ne sert a rien a l'ecran et couterait 4 Mo par jeu.
// false si l'image est absente ou dans un format non gere.
bool DecodeAuroraAsset(const unsigned char* data, size_t len, int assetType,
                       int maxW, int maxH,
                       std::vector<unsigned char>& rgba, int& outW, int& outH,
                       bool frontOnly = false);
// frontOnly (2026-09-25, remarque HB) : une jaquette Xbox 360 "a plat" est
// plus large que haute (dos | tranche | face). On ne garde alors que la FACE,
// la partie droite (a partir de 52,7 % de la largeur). Une image deja en
// portrait est laissee telle quelle.

// Dossier du tableau de bord declare dans le launch.ini de DashLaunch
// (section [Paths], cle "Default", ex. "Hdd:\\App\\Aurora\\Aurora.xex" ->
// "HDD:\\App\\Aurora"). Idee HB (2026-09-25) : c'est la que DashLaunch lance
// Aurora, pas besoin de fouiller le disque. Le prefixe "Hdd:" est ramene a
// "HDD:", le nom de montage utilise par le Studio. false si absent.
bool DashboardDirFromLaunchIni(const std::string& ini, std::string& dirOut);
