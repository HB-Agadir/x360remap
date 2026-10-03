// Unite de compilation UNIQUE qui active le code de stb_image.
//
// stb_image.h (Sean Barrett, domaine public - github.com/nothings/stb) est une
// bibliotheque "header-only" : le meme fichier contient les declarations ET le
// code. Sans precaution, chaque .cpp qui l'inclut compilerait une copie
// complete du code, et l'editeur de liens verrait des dizaines de definitions
// en double. La convention de ces bibliotheques est donc : UN SEUL fichier
// definit STB_IMAGE_IMPLEMENTATION (celui-ci), tous les autres incluent
// simplement l'en-tete pour les declarations.
//
// A quoi ca sert ici : decoder une image PNG embarquee dans le binaire vers un
// tableau de pixels RGBA, que l'on recopie ensuite dans une texture
// Direct3D9 (manette, souris, jaquettes de jeux - voir la maquette
// _design_refs/maquette_v1.2.html). C'est la seule brique qui manquait : la
// creation de texture elle-meme existe deja et fonctionne sur console, c'est
// celle qu'utilise l'atlas de police (third_party/imgui/imgui_impl_dx9_xenon.cpp).
//
// Options desactivees ci-dessous : on ne charge que du PNG, inutile
// d'embarquer les decodeurs des autres formats. Ca reduit la taille du binaire
// et surtout la surface de code jamais exercee sur cette plateforme.
#define STB_IMAGE_IMPLEMENTATION

// Pas de chargement depuis un chemin de fichier : on decode uniquement depuis
// la memoire (stbi_load_from_memory), donc aucun besoin de stdio - qui se
// comporte de toute facon differemment sur Xbox 360.
#define STBI_NO_STDIO

// Formats dont on n'a pas l'usage. A reactiver au cas par cas si un jour on
// embarque autre chose que du PNG.
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

// PREMIER JET NON COMPILE SUR CONSOLE. Points a surveiller au premier build
// avec le toolset XDK (VS2010, PowerPC gros-boutiste) :
//  - stb_image gere lui-meme l'ordre des octets du format PNG (gros-boutiste
//    par specification), donc l'architecture ne devrait pas poser probleme -
//    mais c'est le premier endroit ou regarder si une image sort deformee.
//  - la bibliotheque utilise abondamment malloc/free : rien d'exotique, mais
//    une image de 720 px de large en RGBA represente environ 1,4 Mo alloues
//    le temps du decodage, a liberer aussitot la texture creee.
#include "../stb/stb_image.h"
