#include "ui_layout.h"

UiGrid UiMakeGrid(int cols, float gutter) {
    UiGrid g;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a = ImGui::GetContentRegionAvail();
    g.x = p.x; g.y = p.y;
    g.w = a.x; g.h = a.y;
    g.cols = (cols < 1) ? 1 : cols;
    g.gutter = gutter;
    return g;
}

float UiRowH(const UiGrid& g, float fraction) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    // On retire une gouttiere : deux rangees empilees en ont une entre elles.
    return (g.h - g.gutter) * fraction;
}

UiStack UiMakeStack(int rows, float gap, float labelFrac, float maxRowH) {
    UiStack s;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a = ImGui::GetContentRegionAvail();
    s.x = p.x; s.y = p.y;
    s.w = a.x; s.h = a.y;
    s.rows = (rows < 1) ? 1 : rows;
    s.gap = gap;
    s.labelFrac = labelFrac;

    // Plafonnement : les lignes gardent une taille lisible (maxRowH), mais
    // l'exces est redistribue DANS L'ESPACEMENT entre les lignes plutot que
    // laisse en bloc mort au bas du panneau (retour HB, 2026-08-14 : le
    // panneau du bas semblait "changer de taille" entre Clavier et Souris,
    // alors que son cadre exterieur est identique - c'est le CONTENU qui
    // s'arretait a mi-hauteur cote Souris. Un format qui remplit le meme
    // cadre dans les deux cas donne le style stable/pro demande).
    const float natural = s.rowH();
    if (maxRowH > 0.0f && natural > maxRowH) {
        const float totalRowH = maxRowH * (float)s.rows;
        const float remaining = a.y - totalRowH;
        const float spreadGap = (s.rows > 1) ? remaining / (float)(s.rows - 1) : gap;
        if (spreadGap > gap) {
            s.gap = spreadGap;
            s.h   = a.y;
        } else {
            s.h = maxRowH * (float)s.rows + gap * (float)(s.rows - 1);
        }
    }

    return s;
}
