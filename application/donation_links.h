#pragma once
// Liens de soutien au projet (onglet "Soutenir le projet", voir
// DrawSupportTab() dans application/main.cpp) - PayPal, Ko-fi, GitHub,
// YouTube. Ecrit en C++ standard PUR (pas de windows.h/xtl.h, pas d'ImGui) -
// doit rester includable par les suites de tests portables g++, meme
// pattern que i18n.h/theme.h.
//
// Protection anti-alteration (2026-08-11, demande utilisateur : "je crois
// que votre idee de hash pour le QR code peut aussi s'appliquer a ces
// liens pour couvrir un peu nos arrieres") : chaque chaine est accompagnee
// d'un hash FNV-1a 32 bits precalcule et fige dans donation_links.cpp. Au
// demarrage/affichage de l'onglet, on recalcule le hash de la chaine EN
// MEMOIRE et on le compare a la valeur figee - en cas de desaccord
// (quelqu'un a patche uniquement la chaine dans le binaire, sans recalculer
// et repatcher le hash a cote), IsLinkIntact() renvoie false et
// DrawSupportTab() n'affiche PAS ce lien plutot que d'afficher une valeur
// alteree (fail-closed, un lien a la fois - pas de blocage general de
// l'appli).
//
// A ne pas confondre avec une vraie protection cryptographique : quelqu'un
// qui recompile le binaire depuis les sources (le projet est un fork GPL,
// c'est un droit legitime) peut trivialement changer une chaine ET son hash
// en meme temps - ca reste tout a fait possible et legal. Ceci protege
// uniquement contre un patch binaire "a la main" (hex editor sur le .xex
// deja compile) qui ne changerait que la chaine sans toucher au hash juste
// a cote - meme principe et memes limites que ce qui a ete explique pour le
// QR code de don (voir PROJECT_NOTES.md, section QR code).
#include <stdint.h>

extern const char* const kPayPalEmail;
extern const uint32_t kPayPalEmailHash;

extern const char* const kKofiUrl;
extern const uint32_t kKofiUrlHash;

extern const char* const kGithubUrl;
extern const uint32_t kGithubUrlHash;

extern const char* const kReleasesUrl;
extern const uint32_t kReleasesUrlHash;

extern const char* const kYoutubeUrl;
extern const uint32_t kYoutubeUrlHash;

// FNV-1a 32 bits (base 2166136261, prime 16777619) - deterministe, pas de
// table necessaire, calculable independamment (ex: petit script Python) si
// besoin de verifier une valeur figee un jour.
uint32_t Fnv1aHash(const char* s);

// true si le hash recalcule de s correspond a expectedHash. DrawSupportTab()
// doit conditionner l'affichage de chaque bouton/lien a cet appel.
bool IsLinkIntact(const char* s, uint32_t expectedHash);
