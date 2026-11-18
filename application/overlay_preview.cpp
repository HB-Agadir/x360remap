// Studio preview of the in-game drawing. Mirrors X360Remap/main.cpp (HudKeyShape,
// HudText, HudDrawPad, the mouse outline) and X360Remap/hud_layout.cpp (HudLayout,
// HudKeyLabel). If the plugin drawing changes, change this file the same way.
#include "overlay_preview.h"

#include <string.h>
#include <stdio.h>

#include "../X360Remap/hud_font.h"
#include "../X360Remap/hud_art.h"

namespace {

// Layout constants of X360Remap/hud_layout.cpp.
const int kMaxCaps = 24;   // HUD_MAX_CAPS
const int kCapHeight = 30, kCapGap = 6, kRowIndent = 6;
const int kCharWidth = 10, kCapPadding = 34, kCapMinWidth = 48;

struct Cap {
    uint8_t code, action;
    char    label[7];
    int     x, y, w;
};

// State of the render in progress.
uint8_t* g_base = 0;
int g_pitch = 0;
uint32_t (*g_pack)(unsigned, unsigned, unsigned, unsigned) = 0;
unsigned g_on = 0, g_off = 0, g_shade = 0, g_tag = 0;
// Rectangle of what is drawn while g_track is on (the input drawing).
bool g_track = false;
int g_minX, g_minY, g_maxX, g_maxY;

// Same cut as the plugin: alpha 8 and more is a full pixel, less is not drawn.
void Blend(int x, int y, unsigned rgb, unsigned alpha) {
    if (alpha < 8 || x < 0 || y < 0 || x >= OVPV_W || y >= OVPV_H) return;
    if (g_track) {
        if (x < g_minX) g_minX = x;
        if (y < g_minY) g_minY = y;
        if (x > g_maxX) g_maxX = x;
        if (y > g_maxY) g_maxY = y;
    }
    uint32_t* p = (uint32_t*)(g_base + y * g_pitch + x * 4);
    *p = g_pack((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
}

void KeyShape(int x, int y, int w, int h, int skew, bool filled, unsigned rgb) {
    static const int kInset[5] = { 5, 3, 2, 1, 1 };
    for (int j = 0; j < h; j++) {
        const int lean = skew * 16 * (h - 1 - j) / (h - 1);   // 1/16 pixel
        const int frac = lean & 15, base = x + (lean >> 4);
        int inset = 0;
        if (j < 5) inset = kInset[j];
        else if (j >= h - 5) inset = kInset[h - 1 - j];
        const int left = base + inset, right = base + w - 1 - inset;
        const bool edgeRow = j < 2 || j >= h - 2;
        if (filled || edgeRow) {
            for (int i = left + 1; i < right; i++) Blend(i, y + j, rgb, 15);
        } else {
            Blend(left + 1, y + j, rgb, 15);
            Blend(right - 1, y + j, rgb, 15);
        }
        Blend(left, y + j, rgb, 15 - frac);
        Blend(right, y + j, rgb, frac ? frac : 15);
    }
}

int Text(int x, int y, const char* text, unsigned rgb, bool small) {
    const HudGlyph* glyphs = small ? kHudSmallGlyphs : kHudBigGlyphs;
    const uint8_t* data = small ? kHudSmallData : kHudBigData;
    const int h = small ? HUD_SMALL_HEIGHT : HUD_BIG_HEIGHT;
    for (; *text; text++) {
        const unsigned char ch = (unsigned char)*text;
        if (ch < 32 || ch > 126) continue;
        const HudGlyph& g = glyphs[ch - 32];
        for (int j = 0; j < h; j++)
            for (int i = 0; i < g.w; i++) Blend(x + i, y + j, rgb, data[g.offset + j * g.w + i]);
        x += g.advance;
    }
    return x;
}

int TextWidth(const char* text, bool small) {
    int w = 0;
    for (; *text; text++) {
        const unsigned char ch = (unsigned char)*text;
        if (ch >= 32 && ch <= 126) w += (small ? kHudSmallGlyphs : kHudBigGlyphs)[ch - 32].advance;
    }
    return w;
}

// A soft dark copy under the text, so it reads on any picture.
void TextShadow(int x, int y, const char* text, unsigned rgb, bool small) {
    Text(x + 1, y + 1, text, g_shade, small);
    Text(x, y, text, rgb, small);
}

// Short label of a HID code (X360Remap/hud_layout.cpp HudKeyLabel).
void KeyLabel(uint8_t code, char* out, int outSize) {
    static const struct { uint8_t code; const char* text; } kNames[] = {
        {0x28, "ENTER"}, {0x29, "ESC"},  {0x2A, "BKSP"}, {0x2B, "TAB"},  {0x2C, "SPACE"},
        {0x49, "INS"},   {0x4A, "HOME"}, {0x4B, "PGUP"}, {0x4C, "DEL"},  {0x4D, "END"},   {0x4E, "PGDN"},
        {0x4F, ">"},     {0x50, "<"},    {0x51, "v"},    {0x52, "^"},
        {0x54, "/"},     {0x55, "*"},    {0x56, "-"},    {0x57, "+"},    {0x58, "ENTER"}, {0x63, "."},
        {0xE0, "CTRL"},  {0xE1, "SHIFT"},{0xE2, "ALT"},  {0xE3, "WIN"},
        {0xE4, "RCTRL"}, {0xE5, "RSHFT"},{0xE6, "RALT"}, {0xE7, "RWIN"},
    };
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
        if (kNames[i].code == code) { _snprintf(out, outSize - 1, "%s", kNames[i].text); out[outSize - 1] = 0; return; }
    if (code >= 0x04 && code <= 0x1D) _snprintf(out, outSize - 1, "%c", 'A' + (code - 0x04));
    else if (code >= 0x1E && code <= 0x26) _snprintf(out, outSize - 1, "%c", '1' + (code - 0x1E));
    else if (code == 0x27 || code == 0x62) _snprintf(out, outSize - 1, "0");
    else if (code >= 0x59 && code <= 0x61) _snprintf(out, outSize - 1, "%c", '1' + (code - 0x59));
    else if (code >= 0x3A && code <= 0x45) _snprintf(out, outSize - 1, "F%d", 1 + (code - 0x3A));
    else _snprintf(out, outSize - 1, "%02X", (unsigned)code);
    out[outSize - 1] = 0;
}

// Same order as enum KeyAction in X360Remap/mapping.h.
const char* ActionLabel(uint8_t action) {
    static const char* const kNames[] = { "", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "L3", "R3", "ST", "BK", "G",
        "D^", "Dv", "D<", "D>", "L^", "Lv", "L<", "L>", "R^", "Rv", "R<", "R>" };
    return action < sizeof(kNames) / sizeof(kNames[0]) ? kNames[action] : "";
}

// Same as HudLayout in X360Remap/hud_layout.cpp (fixedWidth 0 = sized by the label).
int Layout(const uint8_t* codes, const uint8_t* actions, int count, Cap* caps, int maxRowWidth, int fixedWidth,
           int rowStep, int* totalWidth, int* totalHeight) {
    int n = 0, x = 0, row = 0, widest = 0;
    for (int i = 0; i < count && n < kMaxCaps; i++) {
        bool seen = false;
        for (int k = 0; k < n; k++) seen = seen || caps[k].code == codes[i];
        if (seen) continue;
        Cap& c = caps[n];
        c.code = codes[i];
        c.action = actions[i];
        KeyLabel(codes[i], c.label, sizeof(c.label));
        int w = (int)strlen(c.label) * kCharWidth + kCapPadding;
        if (w < kCapMinWidth) w = kCapMinWidth;
        if (fixedWidth) w = fixedWidth;
        if (x > 0 && row * kRowIndent + x + w > maxRowWidth) { x = 0; row++; }
        c.x = row * kRowIndent + x;
        c.y = row * rowStep;
        c.w = w;
        x += w + kCapGap;
        if (c.x + w > widest) widest = c.x + w;
        n++;
    }
    if (totalWidth) *totalWidth = widest;
    if (totalHeight) *totalHeight = n ? (row + 1) * rowStep - (rowStep - kCapHeight) : 0;
    return n;
}

// Key picture from the Kenney pack (X360Remap hud_font.h). False when there is none.
bool KeySprite(uint8_t code, int x, int y, bool down) {
    for (size_t i = 0; i < sizeof(kHudSprites) / sizeof(kHudSprites[0]); i++) {
        if (kHudSprites[i].code != code) continue;
        const uint8_t* p = kHudSpriteData + kHudSprites[i].offset;
        if (down) KeyShape(x + 4, y + 4, 28, 28, 0, true, g_on);
        const unsigned color = down ? g_shade : g_off;
        for (int k = 0; k < kHudSprites[i].bytes; k += 2) Blend(x + p[k], y + p[k + 1], color, 15);
        return true;
    }
    return false;
}

// Small Xbox button: coloured disc for A B X Y, plain text for the others.
void ActionIcon(int x, int y, uint8_t action) {
    static const unsigned kFace[4] = { 0x6FBF3A, 0xE5413B, 0x2F8DE8, 0xF0B429 };
    const char* tag = ActionLabel(action);
    if (action >= 1 && action <= 4) {
        const unsigned color = kFace[action - 1];
        for (int j = 0; j < HUD_DISC; j++)
            for (int i = 0; i < HUD_DISC; i++) Blend(x + i, y + j, color, kHudDisc[j * HUD_DISC + i]);
        Text(x + 3, y + 2, tag, 0xFFFFFF, true);
    } else {
        Text(x, y + 2, tag, g_tag, true);
    }
}

// Same as HudPadArea / HudPadTrigger / HudPadStick / HudDrawPad in the plugin.
void PadArea(int x, int y, int area, unsigned rgb) {
    const uint8_t* p = kHudPadAreas[area];
    for (int i = 0; i < kHudPadAreaBytes[area]; i += 2) Blend(x + p[i], y + p[i + 1], rgb, 15);
}

void PadTrigger(int x, int y, int area, uint8_t value) {
    if (value < 8) return;
    const uint8_t* p = kHudPadAreas[area];
    int top = 255, bottom = 0;
    for (int i = 1; i < kHudPadAreaBytes[area]; i += 2) {
        if (p[i] < top) top = p[i];
        if (p[i] > bottom) bottom = p[i];
    }
    const int cut = bottom - (bottom - top + 1) * value / 255;
    for (int i = 0; i < kHudPadAreaBytes[area]; i += 2)
        if (p[i + 1] > cut) Blend(x + p[i], y + p[i + 1], g_on, 15);
}

void PadStick(int x, int y, int sx, int sy, unsigned rgb) {
    const int dx = sx * HUD_PAD_STICK_REACH / 32768, dy = -sy * HUD_PAD_STICK_REACH / 32768;
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++)
            if (i * i + j * j <= 5) Blend(x + dx + i, y + dy + j, rgb, 15);
}

// XInput button bits (same values as XINPUT_GAMEPAD_*), no Xbox / Windows header here.
void DrawPad(int x, int y, const OverlayLiveInput* live) {
    static const struct { uint16_t bit; uint8_t area; } kBits[] = {
        { 0x1000, HUD_PAD_A }, { 0x2000, HUD_PAD_B }, { 0x4000, HUD_PAD_X }, { 0x8000, HUD_PAD_Y },
        { 0x0020, HUD_PAD_BACK }, { 0x0010, HUD_PAD_START }, { 0x0040, HUD_PAD_LS }, { 0x0080, HUD_PAD_RS },
        { 0x0100, HUD_PAD_LB }, { 0x0200, HUD_PAD_RB }, { 0x0001, HUD_PAD_DU }, { 0x0002, HUD_PAD_DD },
        { 0x0004, HUD_PAD_DL }, { 0x0008, HUD_PAD_DR },
    };
    const uint16_t buttons = live ? live->padButtons : 0;
    for (size_t i = 0; i < sizeof(kBits) / sizeof(kBits[0]); i++)
        if (buttons & kBits[i].bit) PadArea(x, y, kBits[i].area, g_on);
    if (live) {
        PadTrigger(x, y, HUD_PAD_LT, live->lt);
        PadTrigger(x, y, HUD_PAD_RT, live->rt);
    }
    for (int i = 0; i < (int)sizeof(kHudPadOutline); i += 2)
        Blend(x + kHudPadOutline[i], y + kHudPadOutline[i + 1], g_off, 15);
    PadStick(x + HUD_PAD_LS_X, y + HUD_PAD_LS_Y, live ? live->lx : 0, live ? live->ly : 0, (buttons & 0x0040) ? g_shade : g_on);
    PadStick(x + HUD_PAD_RS_X, y + HUD_PAD_RS_Y, live ? live->rx : 0, live ? live->ry : 0, (buttons & 0x0080) ? g_shade : g_on);
}

void DrawMouse(int x, int y, uint8_t buttons) {
    for (int area = 0; area < 5; area++) {
        if (!(buttons & (1 << area))) continue;
        for (int i = 0; i < kHudMouseAreaBytes[area]; i += 2)
            Blend(x + kHudMouseAreas[area][i], y + kHudMouseAreas[area][i + 1], g_on, 15);
    }
    for (int i = 0; i < (int)sizeof(kHudMouseOutline); i += 2)
        Blend(x + kHudMouseOutline[i], y + kHudMouseOutline[i + 1], g_off, 15);
}

bool LiveKeyDown(const OverlayLiveInput* live, uint8_t code) {
    if (!live || !live->valid) return code == 0x1A || code == 0x04;   // sample: W and A held
    if (code >= 0xE0 && code <= 0xE7) return ((live->mods >> (code - 0xE0)) & 1) != 0;
    for (int i = 0; i < 6; i++) if (live->keys[i] == code) return true;
    return false;
}

}  // namespace

void OverlayPreviewRender(const OverlaySettings& s, uint8_t* base, int pitch,
                          uint32_t (*pack)(unsigned r, unsigned g, unsigned b, unsigned a)) {
    OverlayPreviewRenderLive(s, 0, base, pitch, pack, 0, 0, 0, 0);
}

void OverlayPreviewRenderLive(const OverlaySettings& s, const OverlayLiveInput* live, uint8_t* base, int pitch,
                              uint32_t (*pack)(unsigned r, unsigned g, unsigned b, unsigned a),
                              int* blockX, int* blockY, int* blockW, int* blockH) {
    g_base = base;
    g_pitch = pitch;
    g_pack = pack;
    for (int y = 0; y < OVPV_H; y++) memset(base + y * pitch, 0, OVPV_W * 4);
    g_on = s.colorOn & 0xFFFFFF;
    g_off = s.colorOff & 0xFFFFFF;
    g_shade = 0x05090C;
    g_tag = 0xB8C6D0;

    const int keysX = (int)s.keysX, keysY = (int)s.keysY;
    g_track = true;
    g_minX = OVPV_W; g_minY = OVPV_H; g_maxX = -1; g_maxY = -1;
    if (s.showKeys) {
        // Default key layout of the plugin (main.cpp kDefault), the first keys held in the sample.
        static const struct { uint8_t code, action; } kDefault[] = {
            {0x1A, 18}, {0x04, 20}, {0x16, 19}, {0x07, 21}, {0x2C, 1}, {0x06, 2},
            {0x15, 3},  {0x08, 4},  {0x14, 5},  {0x09, 6},  {0xE1, 7}, {0xE0, 8},
        };
        uint8_t codes[12], actions[12];
        int count = 0;
        for (size_t i = 0; i < sizeof(kDefault) / sizeof(kDefault[0]); i++) {
            codes[count] = kDefault[i].code;
            actions[count++] = kDefault[i].action;
        }
        const bool style = s.style != 0;
        Cap caps[kMaxCaps];
        int blockW = 0, blockHeight = 0;
        const int n = Layout(codes, actions, count, caps, 420, style ? 60 : 0, style ? 38 : 35, &blockW, &blockHeight);
        const int blockH = blockHeight > 112 ? blockHeight : 112;
        const int oy = keysY + blockH > 712 ? 712 - blockH : keysY;   // as the plugin keeps the block on screen
        for (int k = 0; k < n; k++) {
            const Cap& cap = caps[k];
            const bool down = LiveKeyDown(live, cap.code);
            const int x = keysX + cap.x, y = oy + cap.y;
            if (style && KeySprite(cap.code, x, y, down)) {
                ActionIcon(x + HUD_SPRITE + 1, y + 11, cap.action);
                continue;
            }
            KeyShape(x, y, cap.w, 30, 5, down, down ? g_on : g_off);
            TextShadow(x + 8, y + 6, cap.label, down ? g_shade : g_off, false);
            ActionIcon(x + cap.w - 17, y + 8, cap.action);
        }
        DrawMouse(keysX + blockW + 14, oy, live && live->valid ? live->mouse : 0);
    } else if (s.showPad) {
        DrawPad(keysX, keysY + HUD_PAD_H > 712 ? 712 - HUD_PAD_H : keysY, live && live->valid ? live : 0);
    }
    g_track = false;
    const bool any = g_maxX >= g_minX;
    if (blockX) *blockX = any ? g_minX : 0;
    if (blockY) *blockY = any ? g_minY : 0;
    if (blockW) *blockW = any ? g_maxX - g_minX + 1 : 0;
    if (blockH) *blockH = any ? g_maxY - g_minY + 1 : 0;

    if (s.showStats) {
        const char* line = "FPS 60   CPU 45C   GPU 52C";   // sample values
        const int w = TextWidth(line, false);
        TextShadow(w + 20 < OVPV_W ? OVPV_W - 12 - w : 8, 8, line, 0x7BE36A, false);
    }
}
