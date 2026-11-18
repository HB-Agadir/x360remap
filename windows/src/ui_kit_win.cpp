#include "ui_kit_win.h"

#include <stdio.h>
#include <float.h>

namespace kit {

float       g_k = 1.0f;
ImDrawList* g_dl = nullptr;

// green (Xbox 360), gold, blue, orange, purple, red
static const unsigned kAccents[kAccentCount] = { 0x8BD41A, 0xF5D459, 0x3FA9F5, 0xFF8A2A, 0xB57BFF, 0xF0505A };
unsigned kGold = kAccents[0], kInk = 0x0B0F14, kHover = 0xA8E84A;

unsigned AccentColor(int i) { return kAccents[i >= 0 && i < kAccentCount ? i : 0]; }
void SetAccent(int i) {
    kGold = AccentColor(i);
    kHover = 0;
    for (int sh = 0; sh < 24; sh += 8) {   // a quarter of the way to white
        const unsigned c = (kGold >> sh) & 0xFF;
        kHover |= (c + (255 - c) / 4) << sh;
    }
}

void Begin(float canvasW) { g_k = canvasW / 1280.0f; g_dl = ImGui::GetWindowDrawList(); }
ImVec2 P(float x, float y) { return ImVec2(x * g_k, y * g_k); }
ImU32 Col(unsigned rgb, int a) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a); }

float Txt(ImFont* f, float size, float x, float y, unsigned rgb, const char* t, int align, int alpha) {
    const ImVec2 sz = f->CalcTextSizeA(size * g_k, FLT_MAX, 0.0f, t);
    float px = x * g_k;
    if (align == 1) px -= sz.x * 0.5f;
    else if (align == 2) px -= sz.x;
    g_dl->AddText(f, size * g_k, ImVec2(px, y * g_k), Col(rgb, alpha), t);
    return sz.x / g_k;
}
float TxtW(ImFont* f, float size, const char* t) { return f->CalcTextSizeA(size * g_k, FLT_MAX, 0.0f, t).x / g_k; }

bool Button(const char* id, float x, float y, float w, float h, bool* hovered) {
    ImGui::SetCursorScreenPos(P(x, y));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w * g_k, h * g_k));
    if (hovered) *hovered = ImGui::IsItemHovered();
    return clicked;
}

void Card(float x, float y, float w, float h) {
    g_dl->AddRectFilled(P(x, y), P(x + w, y + h), Col(0x0A1324, 238), 12.0f * g_k);
    g_dl->AddRect(P(x, y), P(x + w, y + h), Col(0x22375A), 12.0f * g_k, ImDrawCornerFlags_All, 1.0f);
}

void Section(float x, float y, const char* label) { Txt(g_fontGCaps, 15.0f, x, y, kTxt3, label); }

int Segmented(const char* id, float x, float y, float w, float h, const char* const* labels, int n, int current) {
    int clicked = -1;
    g_dl->AddRectFilled(P(x, y), P(x + w, y + h), Col(0x060C18), h * 0.5f * g_k);
    g_dl->AddRect(P(x, y), P(x + w, y + h), Col(0x22375A), h * 0.5f * g_k, ImDrawCornerFlags_All, 1.0f);
    const float sw = w / n;
    for (int i = 0; i < n; i++) {
        char bid[48];
        _snprintf(bid, sizeof(bid) - 1, "%s%d", id, i); bid[sizeof(bid) - 1] = 0;
        bool hot = false;
        if (Button(bid, x + sw * i, y, sw, h, &hot)) clicked = i;
        const float sx = x + sw * i + 3.0f, ex = x + sw * (i + 1) - 3.0f;
        if (i == current) g_dl->AddRectFilled(P(sx, y + 3.0f), P(ex, y + h - 3.0f), Col(kGold), (h - 6.0f) * 0.5f * g_k);
        else if (hot) g_dl->AddRectFilled(P(sx, y + 3.0f), P(ex, y + h - 3.0f), Col(0x1A2B47), (h - 6.0f) * 0.5f * g_k);
        Txt(g_fontGBodyM, 16.0f, x + sw * (i + 0.5f), y + (h - 16.0f) * 0.5f - 1.0f, i == current ? kInk : kTxt2, labels[i], 1);
    }
    return clicked;
}

bool Toggle(const char* id, float x, float y, bool on) {
    bool hot = false;
    const bool clicked = Button(id, x, y, 48.0f, 26.0f, &hot);
    g_dl->AddRectFilled(P(x, y), P(x + 48.0f, y + 26.0f), Col(on ? kGold : (hot ? 0x22375A : 0x16233A)), 13.0f * g_k);
    g_dl->AddCircleFilled(P(on ? x + 35.0f : x + 13.0f, y + 13.0f), 9.5f * g_k, Col(on ? kInk : kTxt2), 20);
    return clicked;
}

bool PillButton(const char* id, float x, float y, float w, float h, const char* label, bool primary) {
    bool hot = false;
    const bool clicked = Button(id, x, y, w, h, &hot);
    if (primary) {
        g_dl->AddRectFilled(P(x, y), P(x + w, y + h), Col(hot ? kHover : kGold), h * 0.5f * g_k);
        Txt(g_fontGBodyM, 18.0f, x + w * 0.5f, y + (h - 18.0f) * 0.5f - 1.0f, kInk, label, 1);
    } else {
        g_dl->AddRectFilled(P(x, y), P(x + w, y + h), Col(hot ? 0x1A2B47 : 0x0E1A2E), h * 0.5f * g_k);
        g_dl->AddRect(P(x, y), P(x + w, y + h), Col(0x2F4A70), h * 0.5f * g_k, ImDrawCornerFlags_All, 1.0f);
        Txt(g_fontGBodyM, 18.0f, x + w * 0.5f, y + (h - 18.0f) * 0.5f - 1.0f, kTxt, label, 1);
    }
    return clicked;
}

bool Slider(const char* id, float x, float y, float w, int* value, int minV, int maxV) {
    bool hot = false;
    Button(id, x - 8.0f, y - 10.0f, w + 16.0f, 20.0f, &hot);
    bool changed = false;
    if (ImGui::IsItemActive()) {
        float t = (ImGui::GetIO().MousePos.x / g_k - x) / w;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const int v = minV + (int)(t * (maxV - minV) + 0.5f);
        changed = v != *value;
        *value = v;
    }
    const float t = maxV > minV ? (float)(*value - minV) / (float)(maxV - minV) : 0.0f;
    g_dl->AddRectFilled(P(x, y - 3.0f), P(x + w, y + 3.0f), Col(0x16233A), 3.0f * g_k);
    g_dl->AddRectFilled(P(x, y - 3.0f), P(x + w * t, y + 3.0f), Col(kGold), 3.0f * g_k);
    g_dl->AddCircleFilled(P(x + w * t, y), (hot || ImGui::IsItemActive() ? 9.0f : 8.0f) * g_k, Col(0xFFFFFF), 20);
    return changed;
}

// Line icons in a size x size box centred on (cx, cy).
void DrawIcon(Icon icon, float cx, float cy, float s, unsigned rgb) {
    const ImU32 c = Col(rgb);
    const float t = 1.6f * g_k, h = s * 0.5f;
    switch (icon) {
    case IC_KEYBOARD:
        g_dl->AddRect(P(cx - h, cy - h * 0.6f), P(cx + h, cy + h * 0.6f), c, 3.0f * g_k, ImDrawCornerFlags_All, t);
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++)
                g_dl->AddRectFilled(P(cx - h * 0.7f + i * h * 0.42f, cy - h * 0.32f + r * h * 0.3f),
                                    P(cx - h * 0.7f + i * h * 0.42f + h * 0.2f, cy - h * 0.32f + r * h * 0.3f + h * 0.16f), c);
        g_dl->AddLine(P(cx - h * 0.45f, cy + h * 0.36f), P(cx + h * 0.45f, cy + h * 0.36f), c, t);
        break;
    case IC_PAD: {
        const ImVec2 pts[8] = { P(cx - h * 0.62f, cy - h * 0.42f), P(cx + h * 0.62f, cy - h * 0.42f), P(cx + h * 0.95f, cy + h * 0.25f),
                                P(cx + h * 0.82f, cy + h * 0.62f), P(cx + h * 0.48f, cy + h * 0.5f), P(cx - h * 0.48f, cy + h * 0.5f),
                                P(cx - h * 0.82f, cy + h * 0.62f), P(cx - h * 0.95f, cy + h * 0.25f) };
        g_dl->AddPolyline(pts, 8, c, true, t);
        g_dl->AddLine(P(cx - h * 0.55f, cy - h * 0.02f), P(cx - h * 0.25f, cy - h * 0.02f), c, t);
        g_dl->AddLine(P(cx - h * 0.4f, cy - h * 0.17f), P(cx - h * 0.4f, cy + h * 0.13f), c, t);
        g_dl->AddCircleFilled(P(cx + h * 0.32f, cy - h * 0.12f), h * 0.09f, c, 8);
        g_dl->AddCircleFilled(P(cx + h * 0.5f, cy + h * 0.06f), h * 0.09f, c, 8);
        break;
    }
    case IC_MACRO:
        for (int i = 0; i < 3; i++) {
            const float x0 = cx - h * 0.85f + i * h * 0.6f;
            g_dl->AddTriangleFilled(P(x0, cy - h * 0.45f), P(x0 + h * 0.5f, cy), P(x0, cy + h * 0.45f), Col(rgb, 255 - i * 60));
        }
        break;
    case IC_GAMES:
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 2; i++)
                g_dl->AddRect(P(cx - h * 0.85f + i * h * 0.95f, cy - h * 0.85f + r * h * 0.95f),
                              P(cx - h * 0.85f + i * h * 0.95f + h * 0.75f, cy - h * 0.85f + r * h * 0.95f + h * 0.75f), c, 2.0f * g_k,
                              ImDrawCornerFlags_All, t);
        break;
    case IC_DISPLAY:
        g_dl->AddRect(P(cx - h, cy - h * 0.7f), P(cx + h, cy + h * 0.45f), c, 2.0f * g_k, ImDrawCornerFlags_All, t);
        g_dl->AddLine(P(cx - h * 0.4f, cy + h * 0.8f), P(cx + h * 0.4f, cy + h * 0.8f), c, t);
        g_dl->AddLine(P(cx, cy + h * 0.45f), P(cx, cy + h * 0.8f), c, t);
        g_dl->AddRectFilled(P(cx - h * 0.75f, cy + h * 0.05f), P(cx - h * 0.35f, cy + h * 0.25f), c);
        break;
    case IC_CONSOLE:
        g_dl->AddRect(P(cx - h * 0.45f, cy - h), P(cx + h * 0.45f, cy + h), c, 3.0f * g_k, ImDrawCornerFlags_All, t);
        g_dl->AddCircle(P(cx, cy - h * 0.45f), h * 0.2f, c, 12, t);
        g_dl->AddLine(P(cx - h * 0.2f, cy + h * 0.5f), P(cx + h * 0.2f, cy + h * 0.5f), c, t);
        break;
    case IC_HELP:
        g_dl->AddCircle(P(cx, cy), h * 0.9f, c, 24, t);
        Txt(g_fontGBodyM, s * 0.8f, cx, cy - s * 0.42f, rgb, "?", 1);
        break;
    case IC_HEART:
        g_dl->AddCircleFilled(P(cx - h * 0.35f, cy - h * 0.2f), h * 0.42f, c, 16);
        g_dl->AddCircleFilled(P(cx + h * 0.35f, cy - h * 0.2f), h * 0.42f, c, 16);
        g_dl->AddTriangleFilled(P(cx - h * 0.76f, cy - h * 0.05f), P(cx + h * 0.76f, cy - h * 0.05f), P(cx, cy + h * 0.8f), c);
        break;
    case IC_RECORD:
        g_dl->AddCircleFilled(P(cx, cy), h * 0.7f, c, 20);
        break;
    case IC_PLAY:
        g_dl->AddTriangleFilled(P(cx - h * 0.5f, cy - h * 0.7f), P(cx + h * 0.7f, cy), P(cx - h * 0.5f, cy + h * 0.7f), c);
        break;
    case IC_PLUS:
        g_dl->AddLine(P(cx - h * 0.7f, cy), P(cx + h * 0.7f, cy), c, t * 1.3f);
        g_dl->AddLine(P(cx, cy - h * 0.7f), P(cx, cy + h * 0.7f), c, t * 1.3f);
        break;
    case IC_TRASH:
        g_dl->AddRect(P(cx - h * 0.5f, cy - h * 0.45f), P(cx + h * 0.5f, cy + h * 0.85f), c, 2.0f * g_k, ImDrawCornerFlags_All, t);
        g_dl->AddLine(P(cx - h * 0.75f, cy - h * 0.6f), P(cx + h * 0.75f, cy - h * 0.6f), c, t);
        g_dl->AddLine(P(cx - h * 0.2f, cy - h * 0.85f), P(cx + h * 0.2f, cy - h * 0.85f), c, t);
        break;
    }
}

}  // namespace kit
