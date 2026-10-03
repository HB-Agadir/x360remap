#pragma once
// ============================================================================
// SYSTEME DE GRILLE - 12 colonnes, comme une mise en page web
//
// POURQUOI (2026-08-14, demande explicite de HB)
//
// La mise en page etait faite de largeurs ecrites en dur - 244, 300, 226, 186,
// 14 - posees au jugé les unes apres les autres. Consequence : chaque
// correction en cassait une autre, et on passait la journee sur des details
// sans jamais stabiliser l'ensemble. HB l'a formule directement : "si on reste
// sur ces petits details sans une revision generale, on va tourner toute la
// journee et rien accomplir".
//
// Ici, une seule chose est definie : la zone utile et son decoupage en 12
// colonnes. Tout le reste s'en deduit. Un panneau ne dit plus "je fais 244 px"
// mais "j'occupe 3 colonnes" - il reste donc aligne quoi qu'il arrive, et
// changer une proportion ne demande qu'un chiffre.
//
// METHODE DE TRAVAIL associee : on place d'abord les PANNEAUX (le squelette),
// on verifie les alignements, et seulement ensuite on remplit leur contenu.
// L'inverse - ajuster le contenu en esperant que les panneaux tombent juste -
// est ce qui a fait perdre du temps.
// ============================================================================
#include "third_party/imgui/imgui.h"

struct UiGrid {
    float x, y;        // coin haut-gauche de la zone utile
    float w, h;        // dimensions de la zone utile
    int   cols;        // nombre de colonnes (12 par convention)
    float gutter;      // espace entre deux colonnes

    // Largeur d'UNE colonne, gouttieres deduites.
    float colW() const {
        return (w - gutter * (float)(cols - 1)) / (float)cols;
    }

    // Largeur d'un bloc de `span` colonnes, gouttieres INTERIEURES incluses.
    // C'est ce qu'on passe a BeginChild.
    float spanW(int span) const {
        if (span < 1) span = 1;
        return colW() * (float)span + gutter * (float)(span - 1);
    }

    // Position horizontale du debut de la colonne `col` (0 = premiere).
    float colX(int col) const {
        return x + (colW() + gutter) * (float)col;
    }
};

// Construit une grille a partir de la zone courante d'ImGui. A appeler apres
// avoir positionne le curseur au debut de la zone utile.
UiGrid UiMakeGrid(int cols = 12, float gutter = 14.0f);

// Hauteurs de rangee, exprimees en FRACTION de la hauteur utile plutot qu'en
// pixels : la page reste equilibree si la zone change de taille, et on n'a plus
// de "186" magique dont personne ne sait d'ou il vient.
float UiRowH(const UiGrid& g, float fraction);

// ============================================================================
// UiStack - la meme discipline, mais A L'INTERIEUR d'un panneau
//
// POURQUOI (2026-08-14, deuxieme remarque de HB : "comme vous avez fait une
// grille pour les panneaux, trouver une autre astuce pour mieux gerer les
// elements qui composent cette UI")
//
// La grille avait regle les PANNEAUX, mais leur contenu restait pose a la
// main : chaque widget calculait sa propre largeur de libelle
// (`avail * 0.46f`, ecrit a trois endroits differents), sa propre hauteur, et
// on intercalait des Dummy(0, 6) au jugé pour respirer. Resultat : les lignes
// d'un meme panneau ne s'alignaient pas entre elles, et ajouter une ligne
// obligeait a re-regler les autres.
//
// Ici on declare une bonne fois : "ce panneau contient N lignes". La pile en
// deduit la hauteur de chaque ligne et la colonne des libelles, communes a
// toutes. Un widget ne decide plus de sa geometrie, il la RECOIT - c'est ce
// qui garantit l'alignement au lieu de l'esperer.
// ============================================================================
struct UiStack {
    float x, y;        // coin haut-gauche de la zone de contenu
    float w, h;        // dimensions de la zone de contenu
    int   rows;        // nombre de lignes declarees
    float gap;         // espace vertical entre deux lignes
    float labelFrac;   // part de la largeur reservee aux libelles

    float rowH() const {
        if (rows < 1) return h;
        return (h - gap * (float)(rows - 1)) / (float)rows;
    }
    float rowY(int i) const { return y + (rowH() + gap) * (float)i; }

    // Colonne des libelles, IDENTIQUE pour toutes les lignes de la pile :
    // c'est elle qui aligne verticalement tous les curseurs et toutes les
    // listes, quelle que soit la longueur des traductions.
    float labelW()   const { return w * labelFrac; }
    float controlX() const { return x + labelW(); }
    float controlW() const { return w - labelW(); }
};

// Construit une pile sur la zone courante d'ImGui.
//
// `maxRowH` plafonne la hauteur de ligne : sans lui, un panneau haut avec deux
// lignes seulement produirait deux lignes geantes. Au-dela du plafond, la
// place restante est simplement laissee libre en bas.
UiStack UiMakeStack(int rows, float gap = 8.0f, float labelFrac = 0.42f,
                    float maxRowH = 34.0f);
