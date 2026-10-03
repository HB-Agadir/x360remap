#pragma once
// Chargement d'images PNG vers des textures Direct3D9 affichables par ImGui
// (manette, souris, jaquettes de jeux - voir _design_refs/maquette_v1.2.html).
//
// POURQUOI CE FICHIER EXISTE : l'application ne creait jusqu'ici qu'UNE seule
// texture, celle de l'atlas de police, dans le backend
// (third_party/imgui/imgui_impl_dx9_xenon.cpp). Toute la refonte visuelle v1.2
// suppose d'en afficher d'autres.
//
// LE PIEGE XBOX 360, deja rencontre et resolu le 2026-08-01 sur l'atlas de
// police : les textures de cette console sont TUILEES par defaut (reagencees
// en memoire pour le cache GPU). Un simple LockRect + memcpy ligne par ligne
// ecrit des octets corrects en apparence mais physiquement desordonnes des
// qu'on les echantillonne - a l'epoque, le texte etait tout simplement
// invisible. La parade est le format D3DFMT_LIN_A8R8G8B8 (LIN = lineaire),
// qui impose le meme agencement que sur PC.
//
// Ce fichier reproduit donc EXACTEMENT la sequence de creation de l'atlas de
// police - meme usage, meme format, meme boucle de copie - parce que c'est la
// seule sequence dont on sait qu'elle fonctionne sur le hardware reel. Ne pas
// la "moderniser" en s'inspirant d'un tutoriel Direct3D9 PC : ce n'est pas le
// meme Direct3D9.

#include <xtl.h>
#include <d3d9.h>

// Decode un PNG deja en memoire et en fait une texture.
//   data/len : octets du fichier PNG (embarque dans le binaire, ou lu depuis
//              le disque au prealable).
//   outTex   : recoit la texture creee. A liberer avec Release() a la
//              fermeture ; nullptr en cas d'echec.
//   outW/outH: dimensions en pixels, utiles pour ImGui::Image(). Peuvent etre
//              nullptr si on ne s'en sert pas.
// Renvoie false sans rien allouer si le decodage ou la creation echoue :
// l'appelant doit toujours prevoir un repli visuel (cadre vide, initiale du
// jeu...) plutot que de supposer l'image presente.
bool LoadTextureFromMemory(IDirect3DDevice9* device,
                           const void* data, int len,
                           IDirect3DTexture9** outTex,
                           int* outW, int* outH);

// Meme chose depuis un fichier du disque dur de la console, par exemple
// "HDD:\\X360RemapStudio\\assets\\manette.png".
//
// Utile tant que les images ne sont pas embarquees dans le binaire : la
// conversion d'un PNG en tableau d'octets compile (comme font_data_inter.cpp)
// demande un script, donc un environnement d'execution. Cette variante permet
// d'avancer sans, au prix de fichiers a deployer a cote du .xex.
bool LoadTextureFromFile(IDirect3DDevice9* device,
                         const char* path,
                         IDirect3DTexture9** outTex,
                         int* outW, int* outH);

// Meme creation de texture, a partir de pixels RGBA 8 bits deja decodes
// (pochettes Aurora, 2026-09-25 - voir aurora_library.h).
bool LoadTextureFromRGBA(IDirect3DDevice9* device,
                         const unsigned char* rgba, int w, int h,
                         IDirect3DTexture9** outTex);
