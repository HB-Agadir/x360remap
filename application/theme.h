#pragma once

// Theme visuel "X360Remap Studio" (2026-08-08) - premiere brique de la
// refonte visuelle demandee par HB (graphiste), d'apres les trois maquettes
// de reference deja validees. Voir ETAT_DU_PROJET.md section 3 pour la
// palette complete et les contraintes (zone sure TV 7%, texte large lu a
// 2-3m, focus manette franc, pas d'ombres portees simples en ImGui).
//
// Volontairement isole dans son propre fichier (comme i18n.h/.cpp) : le
// theme ne depend de rien d'autre dans le projet, et rien d'autre ne doit
// dependre de lui au-dela d'un seul appel a ApplyX360RemapTheme() au
// demarrage - le mettre a part permet de le retoucher (couleurs, arrondis)
// sans jamais toucher a la logique des ecrans.
//
// Police custom (2026-08-08) : Inter (SIL OFL 1.1, gratuite), recommandee a
// HB pour sa lisibilite d'ecran et sa large couverture Unicode (accents FR
// ET PT-BR). Coupe optique "24pt" pour le texte courant (choix HB - le
// "24pt" affine les proportions des lettres pour cette plage de taille,
// n'a AUCUN lien avec la taille reellement affichee a l'ecran, qui reste
// pilotee par le parametre `pixelSize` de LoadX360RemapFont ci-dessous).
// Reserve pour plus tard, une fois la hierarchie de titres necessaire (mise
// en page de la maquette 2) : la coupe 28pt en graisse Bold pour les gros
// titres.
//
// CHARGEE DEPUIS LA MEMOIRE, PAS DEPUIS UN FICHIER (revu le 2026-08-08 suite
// a une remarque de HB) : le tableau d'octets kInterFont24ptRegularData
// (font_data_inter.h/.cpp, genere depuis le .ttf qu'il a fourni) est compile
// directement dans X360RemapStudio.xex - la police est donc presente pour
// TOUT utilisateur du binaire, sans qu'il ait besoin de se procurer et
// deposer un fichier lui-meme. Le fichier .ttf source est conserve dans
// assets/fonts/ pour pouvoir regenerer ce tableau si la police change.
//
// Repli automatique et SILENCIEUX sur la police par defaut d'ImGui (ASCII
// uniquement) si le chargement echoue quand meme (donnees corrompues, atlas
// hors memoire...) - c'est pour ca que application/i18n.cpp garde le
// francais sans accents pour l'instant : tant que la police custom n'est pas
// confirmee fonctionnelle sur console, des accents s'afficheraient en
// caracteres manquants avec la police de repli. A revoir une fois
// LoadX360RemapFont() confirme sur hardware.

#include "third_party/imgui/imgui.h"

// Couleurs d'accent nommees, a utiliser EXPLICITEMENT sur les elements
// specifiquement "clavier" (puces de touches, lignes de connexion depuis la
// colonne gauche) ou "specifiquement souris" (colonne droite, lignes de
// connexion associees) - reprises a l'identique des maquettes de HB. Le
// theme global (ImGui::GetStyle().Colors) ne les utilise que pour des
// elements neutres d'interaction (curseur navigation, slider, etc) : les
// puces clavier/souris elles-memes se dessinent au cas par cas avec ces
// constantes quand on construira la mise en page de la maquette 2.
extern const ImVec4 kColorKeyboardAccent; // vert lime ~#8BD41A
extern const ImVec4 kColorMouseAccent;    // bleu ~#2E9BF0

// Applique le theme sombre complet (fond, cartes, texte, bordures, arrondis,
// espacements) sur le style ImGui GLOBAL (ImGui::GetStyle()) - PAS une pile
// Push/Pop a depiler plus tard, un reglage permanent. A appeler UNE SEULE
// FOIS au demarrage, juste apres ImGui::CreateContext() et avant la premiere
// ImGui::NewFrame().
void ApplyX360RemapTheme();

// Charge la police Inter embarquee (voir font_data_inter.h) avec des plages
// de glyphes couvrant le Latin de base + Latin-1 Supplement + Latin
// Extended-A (accents FR : é è ê à ù ç ; PT-BR : ã õ, entre autres). Si le
// chargement echoue quand meme, repli silencieux sur AddFontDefault() -
// JAMAIS d'atlas de police vide, ce qui plante le rendu des la premiere
// frame. A appeler UNE SEULE FOIS, apres ApplyX360RemapTheme() et avant
// ImGui_ImplDX9_Init() (l'atlas de police doit etre construit avant que
// l'implementation DX9 ne cree sa texture).
//
// `pixelSize` : taille en pixels du texte a cette resolution d'ecran -
// contrainte HB : lecture a 2-3m, consigne "41px pour 1280 de large" pour
// le texte d'instruction principal (voir ETAT_DU_PROJET.md). Un seul appel
// pour l'instant (une seule taille) ; charger plusieurs tailles/poids en
// ImFont* distincts (via PushFont/PopFont) est une etape ulterieure, une
// fois la mise en page de la maquette 2 en place et les besoins de
// hierarchie (titres vs texte courant) plus clairs.
void LoadX360RemapFont(float pixelSize);

// Construit l'atlas et verifie qu'un echantillon de caracteres accentues
// existe bien a chacune des quatre tailles chargees. Appelee automatiquement
// par LoadX360RemapFont() - exposee surtout pour la clarte.
void BuildFontAtlasReport();

// Resultat de cette verification, sous forme lisible, a ecrire dans le
// journal de l'application. ImGui ne signale jamais un glyphe qu'il n'a pas
// reussi a ranger dans son atlas (voir le commentaire dans theme.cpp) : sans
// cette trace, un rendu casse ne se constate qu'a l'oeil, sur une chaine
// precise, dans un panneau precis. Un "manquants : 0" partout confirme que
// l'atlas est sain ; tout autre chiffre designe la taille fautive.
const char* GetFontAtlasReport();

// --- Hierarchie typographique (2026-08-14) ---------------------------------
// Quatre tailles issues du meme fichier Inter, remplies par
// LoadX360RemapFont(). Elles existent parce que la maquette repose sur une
// hierarchie : avec une seule taille, la mise en page est correcte mais le
// rendu reste plat.
//
//   g_fontBody  : texte courant. C'est la police PAR DEFAUT (chargee en
//                 premier), donc tout code qui n'appelle pas PushX360Font
//                 l'utilise sans rien changer.
//   g_fontSmall : libelles compacts, titres de section en capitales, valeurs
//                 secondaires (VID:PID, sous-titres).
//   g_fontTitle : titres de panneau, noms de jeu.
//   g_fontLogo  : le logo "X360 REMAP" de la barre superieure, uniquement.
//
// Peuvent valoir nullptr si le chargement de police a echoue et qu'on est
// retombe sur la police par defaut d'ImGui - d'ou PushX360Font/PopX360Font,
// qui tolerent ce cas.
extern ImFont* g_fontBody;
extern ImFont* g_fontSmall;
extern ImFont* g_fontTitle;
extern ImFont* g_fontLogo;

// Polices de la page "glossy" (Barlow Condensed, 2026-09-24 - voir
// ui_glossy.cpp). Tailles en pixels ImGui, c'est-a-dire hauteur
// ascendante+descendante : 1,2 x la taille "em" des maquettes (PIL/CSS).
// nullptr si le chargement a echoue : ui_glossy.cpp retombe alors sur la
// police courante.
extern ImFont* g_fontGBody;    // Barlow 400, 24 px - texte courant
extern ImFont* g_fontGBodyM;   // Barlow 500, 24 px - valeurs, onglet actif
extern ImFont* g_fontGLarge;   // Barlow 400, 26 px - boutons, onglets
extern ImFont* g_fontGTitle;   // Barlow 400, 37 px - titres de colonne
extern ImFont* g_fontGSmall;   // Barlow 400, 20 px - bornes, DPI
extern ImFont* g_fontGCaps;    // Barlow 500, 18 px - libelles en capitales

// A utiliser par PAIRES, avec le MEME pointeur des deux cotes :
//   PushX360Font(g_fontSmall);  ...  PopX360Font(g_fontSmall);
// Passer nullptr ne fait rien (ni push, ni pop), ce qui evite a chaque
// appelant de verifier lui-meme que la police existe.
void PushX360Font(ImFont* font);
void PopX360Font(ImFont* font);
