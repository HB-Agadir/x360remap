// Overlay messages from NetISO's rec_state.txt (2026-11-04).
// From X360Remap/: cl /EHsc tests/test_record_toast.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../record_toast.h"

int main() {
	char out[56];
	RecordToastText("IDLE ready", "REC normal clip_20261104_101010 0 s", out, sizeof(out));
	assert(!strcmp(out, "ENREGISTREMENT DEMARRE: 426X240, 15 I/S"));
	RecordToastText("IDLE ready", "REC high clip_x 0 s", out, sizeof(out));
	assert(strstr(out, "640X360, 10 I/S"));
	RecordToastText("REC normal clip_x 0 s", "REC normal clip_x 5 s", out, sizeof(out));
	assert(out[0] == '\0');                         // same recording
	RecordToastText("REC normal clip_x 10 s", "IDLE saved clip_x 180 samples 12 s (stopped)", out, sizeof(out));
	assert(!strcmp(out, "ENREGISTREMENT FINI: 12 S, ENVOYE AU PC DANS AURORA"));
	RecordToastText("IDLE ready", "IDLE game still starting, wait a few seconds", out, sizeof(out));
	assert(strstr(out, "JEU EN DEMARRAGE"));
	RecordToastText("REC normal c 1 s", "IDLE ready", out, sizeof(out));
	assert(out[0] == '\0');
	RecordToastText("IDLE ready", "IDLE ready", out, sizeof(out));
	assert(out[0] == '\0');
	RecordToastText("IDLE ready", "IDLE unsupported frame 1920x1080 and a very long tail of words", out, sizeof(out));
	assert(!strncmp(out, "ENREGISTREMENT: UNSUPPORTED FRAME 1920X1080", 43) && strlen(out) == sizeof(out) - 1);
	RecordToastText("IDLE ready", "REC high clip_x 0 s", out, sizeof(out), false);
	assert(!strcmp(out, "RECORDING STARTED: 640X360, 10 FPS"));
	printf("record toast OK\n");
	return 0;
}
