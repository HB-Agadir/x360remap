// Cameras known in advance, loaded by the plugin (after camera_addresses.txt,
// which always wins) and by Xenia Camera Finder --play / --check.
//
// Two sources:
//
// 1. Found with Xenia Camera Finder on HB's games (build-stamp lines, valid for
//    that exact executable).
//
// 2. Imported from Xenia Canary Mousehook (Clippy95 / marinesciencedude,
//    src/xenia/hid/winkey/hookables/*.cc, branch mousehook_b, 2026-10-08),
//    "Copyright Ben Vanik. Released under the BSD license". Version-keyed (v3)
//    like Mousehook itself. Only games whose camera is plain floats reached by
//    a fixed address or 1-2 pointers are imported; the others need a special
//    kind of hook (see the list at the end of this file).
//
// Format: see camera_learn.h (CamParseLine). Signs and units were converted
// from each Mousehook formula: yaw += signX * mouseX, pitch -= signY * mouseDown.
// The speed differs from Mousehook's (one common scale here): adjust with the
// sensitivity setting (F5/F6).
#include "camera_learn.h"

const char* const kCamKnownLines[] = {
	// --- Found with Xenia Camera Finder (2026-10-08) -------------------------
	"415608CB 4E77262A 00000000 82743CBC 82743CB8 -1 -1 deg Modern Warfare 3 (solo)",
	"41560817 4ABDA6F4 00000000 82648B60 82648B5C -1 -1 deg Modern Warfare 2 (solo)",
	"v2 54510842 4B6AD04C 00000000 1 82994CC0 00000000 00000354 FFFFFFFC 1 -1 deg Metro 2033",
	"v2 4B4D07F5 5162D4B3 00000000 2 82ED6028 000001C0 00002E90 FFFFFFFC 1 -1 rad Metro Last Light",
	// Found on a tester's console with the F12 search (2026-10-13), version
	// 0.0.9.21 (title update 9), played in private match.
	"v2 41560817 5B11C269 01DE0000 1 82682350 00000000 0000335C FFFFFFFC -1 -1 deg Modern Warfare 2 TU9",
	// Found by rgdshock with the F12 search (GitHub issue 7, 2026-11-07). Each line
	// is tied to one executable (build stamp + size). His games: Wolfenstein TNO,
	// Quake 2, Quake 4 (two versions), CoD Classic, CoD 2, Doom 3 BFG, Wolfenstein
	// 2009, Prey, Bodycount, Oblivion, RAGE; which line is which game is not said.
	"425307F5 5360E4F4 016C0000 83491DF8 83491DF4 -1 -1 deg Title 425307F5",
	"415607D6 43669365 00AF0000 82A34338 82A34334 -1 -1 deg Title 415607D6",
	"415607D2 435E8E45 01B70000 830B99F8 830B99F4 -1 -1 deg Title 415607D2",
	"415607D2 435F196C 01B70000 830B99F8 830B99F4 -1 -1 deg Title 415607D2",
	"58410957 4AAB41F9 018A0000 834C5070 834C506C -1 -1 deg Title 58410957",
	"415607D1 43E7A218 022B0000 828E9F0C 828E9F08 -1 -1 deg Title 415607D1",
	"425307F1 502EB6A2 00CB0000 82675490 8267548C -1 -1 deg Title 425307F1",
	"v2 415607DE 4ACCF45B 01280000 1 82C8D9D0 00000000 00000FA4 FFFFFFFC -1 -1 deg Title 415607DE",
	"545407E0 44C8D18C 02170000 836E9880 836E987C -1 -1 deg Title 545407E0",
	"v2 434D0839 4E403E2D 01320000 1 8327B700 00000000 000001C4 FFFFFFFC -1 -1 rad Title 434D0839",
	"v2 425307D1 462382D0 00B60000 1 82AF112C 00000000 00000028 FFFFFFF8 1 -1 rad Title 425307D1",
	"425307EC 4FC6A773 01770000 834A0F48 834A0F44 -1 -1 deg Title 425307EC",

	// --- Imported from Xenia Mousehook (BSD) ---------------------------------
	// Far Cry Instincts: Predator TU0 - [829138B8] + 3AC (yaw), + 3A4 (pitch), degrees
	"v3 555307DC 0.0.0.1 1 829138B8 00000000 000003AC FFFFFFF8 -1 -1 deg Far Cry Instincts Predator",
	// Dead Rising 2: Case Zero / Case West - fixed addresses, radians
	"v3 58410A8D * 0 00000000 00000000 AA4D2388 AA4D238C -1 -1 rad Dead Rising 2 Case Zero",
	"v3 58410B00 * 0 00000000 00000000 A94DF458 A94DF45C -1 -1 rad Dead Rising 2 Case West",
	// Just Cause TU0 - camera controller [46965100] + 130 / + 12C (on foot only)
	"v3 534307D5 1.0 1 46965100 00000000 00000130 FFFFFFFC 1 -1 deg Just Cause (on foot)",
	// Crackdown 2 TU0 / TU5 - [base] + 7EC / + 7E8, radians (on foot only)
	"v3 4D5308BC 0.0.0.1 1 836C6520 00000000 000007EC FFFFFFFC -1 1 rad Crackdown 2 (on foot)",
	"v3 4D5308BC 0.0.5.1 1 83800F88 00000000 000007EC FFFFFFFC -1 1 rad Crackdown 2 TU5 (on foot)",
	// Minecraft TU75 - [[82C8518C] + 38] + 148 / + 14C, degrees
	"v3 584111F7 0.0.80.1 2 82C8518C 00000038 00000148 00000004 1 -1 deg Minecraft TU75",
	// Source engine games with a fixed angle address (QAngle: pitch +0, yaw +4), degrees
	"v3 4541080F 0.0.0.4 0 00000000 00000000 863F53AC 863F53A8 -1 -1 deg The Orange Box",
	"v3 58410960 0.0.1.3 0 00000000 00000000 863F56B4 863F56B0 -1 -1 deg Portal Still Alive",
	"v3 4541080F 1.0.1.1 0 00000000 00000000 86438704 86438700 -1 -1 deg Postal III",
	"v3 584109B3 0.0.0.3 0 00000000 00000000 8644A6B4 8644A6B0 -1 -1 deg Bloody Good Time",
	// Saints Row 2 TU3 - fixed addresses, radians
	"v3 545107FC 0.0.3.8 0 00000000 00000000 82B7A570 82B7A590 1 -1 rad Saints Row 2 TU3",
};
const size_t kCamKnownCount = sizeof(kCamKnownLines) / sizeof(kCamKnownLines[0]);

// NOT imported yet - they need a kind of hook this format does not describe:
//  - Call of Duty 4 / MW2 / MW3 / WaW / CoD3 / AW: already built into the plugin
//    (kCodBuilds in main.cpp, signature-checked, with FOV scaling).
//  - Halo 3 / ODST / Reach / 4: camera reached through the game thread's TLS.
//  - CS:GO, Left 4 Dead 1/2, Portal 2: player pointer obtained by CALLING a
//    game function.
//  - Dark Messiah: solo and multi share the same Title ID and version but use
//    different addresses (needs the module name).
//  - Gears of War 1/2/3/Judgment, Section 8: 16-bit (Unreal) angles + start-up
//    conditions.
//  - GoldenEye / Perfect Dark XBLA, Perfect Dark Zero, Red Dead Redemption,
//    Saints Row 1: several camera modes (aim mode, cover, vehicles...).
