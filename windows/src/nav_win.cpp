// Tab bar of the Windows Studio (2026-11-17): more pages than the console (Controller,
// Macros, Console), one icon each. The shared header draws only background and logo
// (UiGlossySetExternalTabs); this bar is drawn on top, where the console's tabs are.
#include "nav_win.h"

#include <stdio.h>

#include "platform_win.h"
#include "ui_kit_win.h"
#include "win_i18n.h"

using namespace kit;

void DrawWinNav(float canvasW, int& tab) {
    Begin(canvasW);
    struct Entry { int tab; Icon icon; const char* label; };
    const Entry entries[] = {
        { 0, IC_KEYBOARD, L4("Clavier", "Keyboard", "Teclado", "Teclado") },
        { 5, IC_PAD,      L4("Manette", "Controller", "Mando", "Controle") },
        { 6, IC_MACRO,    L4("Macros", "Macros", "Macros", "Macros") },
        { 1, IC_GAMES,    L4("Jeux", "Games", "Juegos", "Jogos") },
        { 2, IC_DISPLAY,  L4("Affichage", "Display", "Pantalla", "Tela") },
        { 7, IC_CONSOLE,  L4("Console", "Console", "Consola", "Console") },
        { 3, IC_HELP,     L4("Aide", "Help", "Ayuda", "Ajuda") },
    };
    const int n = (int)(sizeof(entries) / sizeof(entries[0]));
    const float x0 = 300.0f, y0 = 24.0f, h = 50.0f, heartW = 50.0f, gap = 6.0f;
    const float xEnd = 1236.0f - 2.0f * (heartW + gap);   // palette and heart on the right
    const float w = (xEnd - x0) / n;
    static bool s_palette = false;
    SetAccent(plat::Settings().accent);
    g_dl->AddRectFilled(P(x0, y0), P(xEnd, y0 + h), Col(0x0A1324, 220), 12.0f * g_k);
    g_dl->AddRect(P(x0, y0), P(xEnd, y0 + h), Col(0x22375A), 12.0f * g_k, ImDrawCornerFlags_All, 1.0f);
    if (s_palette) {   // the bar shows the colours instead of the tabs until one is picked
        const char* title = L4("Couleur", "Colour", "Color", "Cor");
        Txt(g_fontGBodyM, 17.0f, x0 + 24.0f, y0 + (h - 17.0f) * 0.5f - 1.0f, kTxt2, title);
        const float sx0 = x0 + 48.0f + TxtW(g_fontGBodyM, 17.0f, title), step = 56.0f;
        bool picked = false;
        for (int i = 0; i < kAccentCount; i++) {
            const float cx = sx0 + step * i + 18.0f, cy = y0 + h * 0.5f;
            char id[16];
            _snprintf(id, sizeof(id) - 1, "##acc%d", i); id[sizeof(id) - 1] = 0;
            bool hot = false;
            if (Button(id, cx - 22.0f, y0, 44.0f, h, &hot)) {
                plat::Settings().accent = i;
                plat::SaveSettings();
                SetAccent(i);
                picked = true;
            }
            const bool cur = plat::Settings().accent == i;
            g_dl->AddCircleFilled(P(cx, cy), (hot ? 14.0f : 12.0f) * g_k, Col(AccentColor(i)), 24);
            if (cur) g_dl->AddCircle(P(cx, cy), 17.0f * g_k, Col(0xFFFFFF), 24, 2.0f * g_k);
        }
        if (picked || (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered())) s_palette = false;
    }
    for (int i = 0; i < n && !s_palette; i++) {
        const float x = x0 + w * i;
        char id[16];
        _snprintf(id, sizeof(id) - 1, "##nav%d", i); id[sizeof(id) - 1] = 0;
        bool hot = false;
        if (Button(id, x, y0, w, h, &hot)) tab = entries[i].tab;
        const bool on = tab == entries[i].tab;
        if (on) {
            g_dl->AddRectFilled(P(x + 4.0f, y0 + 4.0f), P(x + w - 4.0f, y0 + h - 4.0f), Col(0x1A2B47), 9.0f * g_k);
            g_dl->AddRectFilled(P(x + 18.0f, y0 + h - 5.0f), P(x + w - 18.0f, y0 + h - 2.0f), Col(kGold), 2.0f * g_k);
        } else if (hot) {
            g_dl->AddRectFilled(P(x + 4.0f, y0 + 4.0f), P(x + w - 4.0f, y0 + h - 4.0f), Col(0x13213A), 9.0f * g_k);
        }
        const unsigned c = on ? kGold : (hot ? 0xFFFFFF : kTxt2);
        const float lw = TxtW(on ? g_fontGBodyM : g_fontGBody, 17.0f, entries[i].label);
        const float total = 22.0f + 8.0f + lw, sx = x + (w - total) * 0.5f;
        DrawIcon(entries[i].icon, sx + 11.0f, y0 + h * 0.5f, 20.0f, c);
        Txt(on ? g_fontGBodyM : g_fontGBody, 17.0f, sx + 30.0f, y0 + (h - 17.0f) * 0.5f - 1.0f, c, entries[i].label);
    }
    // Colour of the pages: a palette button, the bar turns into the colour choice
    {
        const float x = 1236.0f - 2.0f * heartW - gap;
        bool hot = false;
        if (Button("##nav_palette", x, y0, heartW, h, &hot)) s_palette = !s_palette;
        g_dl->AddRectFilled(P(x, y0), P(x + heartW, y0 + h), Col(s_palette ? 0x1A2B47 : (hot ? 0x13213A : 0x0A1324), 220), 12.0f * g_k);
        g_dl->AddRect(P(x, y0), P(x + heartW, y0 + h), Col(s_palette ? kGold : 0x22375A), 12.0f * g_k, ImDrawCornerFlags_All, 1.0f);
        const float cx = x + heartW * 0.5f, cy = y0 + h * 0.5f;
        for (int i = 0; i < 4; i++) {   // four dots of the palette, the current colour first
            static const float d[4][2] = { { -5, -5 }, { 5, -5 }, { -5, 5 }, { 5, 5 } };
            const unsigned c = i == 0 ? kGold : AccentColor((plat::Settings().accent + i) % kAccentCount);
            g_dl->AddCircleFilled(P(cx + d[i][0], cy + d[i][1]), 4.5f * g_k, Col(c), 12);
        }
    }
    // Support: a heart on its own
    {
        const float x = 1236.0f - heartW;
        bool hot = false;
        if (Button("##nav_support", x, y0, heartW, h, &hot)) tab = 4;
        const bool on = tab == 4;
        g_dl->AddRectFilled(P(x, y0), P(x + heartW, y0 + h), Col(on ? 0x1A2B47 : (hot ? 0x13213A : 0x0A1324), 220), 12.0f * g_k);
        g_dl->AddRect(P(x, y0), P(x + heartW, y0 + h), Col(on ? kGold : 0x22375A), 12.0f * g_k, ImDrawCornerFlags_All, 1.0f);
        DrawIcon(IC_HEART, x + heartW * 0.5f, y0 + h * 0.5f, 20.0f, on || hot ? 0xFF5A7A : kTxt2);
    }
}
