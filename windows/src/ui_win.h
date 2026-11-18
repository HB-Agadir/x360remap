#pragma once
// What exists only on the PC: the status bar under the pages (console link,
// send / receive, local files) and the dialogs it opens (connection, conflict,
// direct aim, logs, switches, shortcuts). Plain ImGui widgets with the Studio
// theme; the four pages above stay the console ones.

// Height of the status bar for a canvas of this width.
float UiWinBarHeight(float canvasW);

// Draws the bar at (0, canvasH) and any open dialog. hwnd: owner of the file
// dialogs.
void UiWinDraw(float canvasW, float canvasH, void* hwnd);

// Opens a dialog by name at the next frame: "connection", "aim" (mouse),
// "game" (direct aim of the game), "logs", "advanced", "shortcuts".
// Used by the --open= command line option (screenshots, documentation).
void UiWinOpenDialog(const char* name);
