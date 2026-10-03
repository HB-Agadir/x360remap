#pragma once
// ============================================================================
// UiAssetId - LE SYSTEME QUI EMPECHE LES TEXTURES DE SE MELANGER
//
// POURQUOI (2026-08-14, question directe de HB : "quel systeme on va suivre
// pour que chaque element soit a sa place et ne se melange pas avec les
// autres")
//
// MEME DISCIPLINE QUE i18n.h (StrId) - et pour la MEME raison. Le tableau de
// traductions avait deja produit exactement le bug que ce fichier evite :
// deux chaines inserees au mauvais endroit, aucune erreur de compilation,
// juste un texte qui apparaissait au mauvais endroit a l'ecran (voir
// PROJECT_NOTES.md, "piege d'i18n"). Corrige a l'epoque en donnant a CHAQUE
// entree son ID explicite plutot qu'une position implicite, plus une
// verification au demarrage (CheckStringTableIntegrity).
//
// Une texture de skin est exactement le meme risque, en pire : il n'y a pas
// de fichier texte a relire, juste un rendu qui a l'air "a peu pres bon" au
// premier coup d'oeil. D'ou :
//   - un identifiant EXPLICITE par role visuel (UiAssetId), jamais un nom de
//     fichier ecrit a la main dans le code de dessin ;
//   - UN SEUL fichier associe a CET identifiant, jamais reutilise ailleurs ;
//   - une verification au demarrage (CheckAssetTableIntegrity, meme forme que
//     son equivalent i18n) qui detecte un tableau desaligne AVANT que
//     quelqu'un le remarque a l'ecran.
//
// ============================================================================
// CONVENTION DE DOSSIER - la partie qui repond a "je vais devoir decouper
// et ou est-ce que je depose ca"
//
//   _design_refs/ui_skin/<nom_du_skin>/<fileName exact du manifeste>
//
// Exemple : _design_refs/ui_skin/default/panel_default.png
//
// Un skin = un dossier. Changer de skin = changer de dossier, jamais de
// code. C'est ce qui rend "plusieurs skins" (la demande initiale de HB)
// gratuit une fois ce systeme en place - le skin n'est qu'un chemin de base.
//
// ============================================================================
// PENDANT LA CONCEPTION : FICHIERS SUR LE DISQUE DUR, PAS EMBARQUES
//
// La manette et la souris sont EMBARQUEES dans le binaire (image_data_*.cpp,
// tableaux d'octets compiles) - un choix delibere pour que l'utilisateur
// final n'ait aucun fichier a deposer lui-meme. Mais ca veut dire
// recompiler + redeployer a CHAQUE retouche, exactement la boucle lente que
// preview/ existe pour eviter.
//
// Le chargement de skin passe donc par LoadTextureFromFile (HDD:\...) tant
// qu'on est en conception : HB retouche un PNG, le redepose par XeCLI (une
// commande, pas une recompilation), relance l'appli, regarde. Le passage a
// l'embarque (comme la manette/souris) est une etape de FINALISATION, une
// fois un skin stabilise - pas une contrainte pendant qu'on dessine encore.
//
// ============================================================================
// TAILLES - PAR ROLE, PAS UN GABARIT UNIQUE (revu 2026-08-14)
//
// Version initiale : tout en 96x96 (panneaux) ou 64x64 (icones). Corrige
// suite a une remarque justifiee : une carte peripherique est large et
// courte (256x64), un champ deroulant est court (128x48) - les forcer dans
// un carre de 96x96 gaspille soit de la largeur, soit de la hauteur.
// Chaque entree du manifeste porte donc SA PROPRE taille et SES PROPRES
// marges 9-slice - voir le detail (et le budget memoire complet, calcule)
// dans _design_refs/DESIGN_SYSTEM.md, section 10.
//
// Le principe 9-slice reste le meme quelle que soit la taille : les coins
// (marges L/T/R/B) restent FIXES quel que soit l'agrandissement affiche,
// seule la bande centrale s'etire. C'est ce qui permet a une petite source
// de couvrir un panneau bien plus grand sans flou ni repetition visible sur
// les coins - la meme technique que les jeux SNES/Megadrive, qui composaient
// deja leurs decors a partir de tuiles minuscules repetees, pour la meme
// raison de memoire (remarque de HB, 2026-08-14).
//
// Meme a des tailles variables et plus genereuses qu'au depart, un skin
// complet (7 panneaux/cartes/champs/boutons + 7 icones) pese environ 400 Ko
// en RGBA non compresse (contrainte Xbox 360, voir texture_loader.h) - a
// comparer aux 2,7 Mo deja charges aujourd'hui pour la manette et la souris
// (720x490 et 720x479), qui tournent sans probleme sur la console. La
// console a 512 Mo de memoire unifiee ; ce systeme n'en engage pas 1%.
// ============================================================================
// DOSSIERS - un sous-dossier par famille, comme prefixe du nom de fichier
// (BuildPath() ne fait qu'une concatenation, le "/" fait partie du nom) :
//
//   _design_refs/ui_skin/<skin>/panels/panel_default.png
//   _design_refs/ui_skin/<skin>/panels/card_device.png
//   _design_refs/ui_skin/<skin>/buttons/button_accent.png
//   _design_refs/ui_skin/<skin>/icons/icon_keyboard.png
// ============================================================================
#include <xtl.h>
#include <d3d9.h>

enum UiAssetId {
    // --- Panneaux (9-slice, 96x96) ---
    UI_ASSET_PANEL_DEFAULT,   // fond commun : peripheriques / actions / bas
    UI_ASSET_PANEL_CTX,       // panneau contextuel (bordure accentuee)
    UI_ASSET_CARD_DEVICE,     // carte peripherique (colonne de gauche)
    UI_ASSET_CELL_GRID,       // case de la grille des 21 boutons
    UI_ASSET_FIELD_COMBO,     // champ deroulant (reglages souris)
    UI_ASSET_BUTTON_ACCENT,   // bouton d'action principal
    UI_ASSET_BUTTON_GHOST,    // bouton secondaire (contour)

    // --- Icones (64x64, non etirees) ---
    UI_ASSET_ICON_KEYBOARD,
    UI_ASSET_ICON_MOUSE,
    UI_ASSET_ICON_GEAR,
    UI_ASSET_ICON_PLAY,
    UI_ASSET_ICON_LIST,
    UI_ASSET_ICON_HELP,
    UI_ASSET_ICON_HEART,

    // --- Marque (pas de 9-slice, image entiere redimensionnee en gardant
    // les proportions - voir ui_mapping.cpp/DrawUiTopBar) ---
    UI_ASSET_LOGO_WORDMARK,

    UI_ASSET_COUNT
};

// Une entree du manifeste. `id` porte le meme role que StrId dans i18n.h :
// CheckAssetTableIntegrity() verifie que g_assets[i].id == (UiAssetId)i,
// donc un tableau desaligne (entree oubliee, dupliquee, deplacee) est
// detecte au demarrage plutot que decouvert a l'ecran.
struct UiAssetSpec {
    UiAssetId   id;
    const char* fileName;              // nom EXACT attendu dans le dossier du skin
    int         nativeW, nativeH;      // dimensions attendues du PNG source
    // Marges 9-slice (0 pour les icones - non etirees). Coins fixes,
    // bande centrale seule etiree.
    int         sliceL, sliceT, sliceR, sliceB;
};

extern const UiAssetSpec g_uiAssets[UI_ASSET_COUNT];

// -1 si le manifeste est coherent, sinon l'index de la premiere entree dont
// l'id ne correspond pas a sa position.
int CheckAssetTableIntegrity();

// --- Chargement d'un skin ---------------------------------------------------
// Une texture chargee par asset ; les entrees manquantes restent a nullptr -
// AUCUNE n'est obligatoire. C'est la regle de degrade : un skin peut livrer
// UNE seule image, le reste continue de s'afficher avec le rendu trace au
// pinceau existant (theme.cpp / ui_mapping.cpp), pas de plantage, pas de
// trou visuel. HB peut donc livrer les assets un par un, dans n'importe
// quel ordre, sans jamais casser l'app le temps qu'il finisse le reste.
struct UiSkin {
    IDirect3DTexture9* tex[UI_ASSET_COUNT];
    int                w[UI_ASSET_COUNT];
    int                h[UI_ASSET_COUNT];
    bool               present[UI_ASSET_COUNT];   // false = repli sur le dessin trace
};

// Charge depuis "HDD:\\X360RemapStudio\\ui_skin\\<skinName>\\<fileName>" pour
// chaque entree du manifeste. Journalise (AppLog) chaque asset trouve/absent -
// c'est le "journal de chargement" qui remplace le controle a l'oeil : HB
// saura depuis les logs exactement quels fichiers ont ete vus, sans avoir a
// deviner en regardant l'ecran quel element vient d'une texture ou du trace.
bool LoadUiSkin(IDirect3DDevice9* device, const char* skinName, UiSkin& out);
void ReleaseUiSkin(UiSkin& skin);
