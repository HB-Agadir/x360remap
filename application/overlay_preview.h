#pragma once
// Studio preview of the in-game drawing (keyboard caps, controller, mouse, stats).
// Same drawing code as the plugin (X360Remap/main.cpp: HudKeyShape, HudText,
// HudPadArea...), drawn on a 1280x720 canvas, so the preview matches the game.
// Keys: the plugin's default layout (WASD, Space...) because the Studio does not
// know the player's keyboard map; the plugin uses the saved map.
// Plain C++ with no Xbox API, so a PC test can run it.
#include <stdint.h>
#include <string.h>
#include "overlay_settings.h"

const int OVPV_W = 1280, OVPV_H = 720;

// Writes the canvas into base (rows `pitch` bytes apart, OVPV_W pixels per row).
// Pixels not drawn are cleared to 0. pack() turns r, g, b, a (0-255) into the
// texel value of the target texture (IM_COL32 on the console).
void OverlayPreviewRender(const OverlaySettings& s, uint8_t* base, int pitch,
                          uint32_t (*pack)(unsigned r, unsigned g, unsigned b, unsigned a));

// Live preview (Windows Studio, 2026-11-17): what the player presses on the PC is drawn
// as the plugin would draw it in the game. keys / mods are HID usages and the modifier
// byte, mouse the HID button bits, pad the XInput fields. valid = false keeps the
// sample (W and A held). block* receives the input drawing's rectangle on the 1280x720
// canvas (0 size when none), for dragging it on the preview.
struct OverlayLiveInput {
    bool     valid;
    uint8_t  keys[6], mods, mouse;
    uint16_t padButtons;
    uint8_t  lt, rt;
    int16_t  lx, ly, rx, ry;
    OverlayLiveInput() { memset(this, 0, sizeof(*this)); }
};
void OverlayPreviewRenderLive(const OverlaySettings& s, const OverlayLiveInput* live, uint8_t* base, int pitch,
                              uint32_t (*pack)(unsigned r, unsigned g, unsigned b, unsigned a),
                              int* blockX, int* blockY, int* blockW, int* blockH);
