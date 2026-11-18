#pragma once
// Turns NetISO's recorder state (rec_state.txt, see X360_NetISO_Aurora/docs/
// video-capture-design.md) into an overlay message. Pure text, header only:
// tests/test_record_toast.cpp. Upper case, no accents: the overlay font has none.
#include <stdio.h>
#include <string.h>

// out = "" when nothing is worth showing (no change, same recording, idle).
static void RecordToastText(const char* prev, const char* cur, char* out, int cap, bool fr = true) {
	out[0] = '\0';
	if (!strcmp(prev, cur)) return;
	char quality[16] = "";
	int secs = 0;
	if (sscanf(cur, "REC %15s", quality) == 1) {
		if (!strncmp(prev, "REC", 3)) return;   // the same recording, its time moved on
		const char* res = !strcmp(quality, "light") ? "320X180, 15" : !strcmp(quality, "high") ? "640X360, 10" : "426X240, 15";
		_snprintf(out, cap - 1, fr ? "ENREGISTREMENT DEMARRE: %s I/S" : "RECORDING STARTED: %s FPS", res);
	} else if (!strncmp(cur, "IDLE saved", 10)) {
		const char* s = strstr(cur, " samples ");
		if (s) sscanf(s, " samples %d", &secs);
		_snprintf(out, cap - 1, fr ? "ENREGISTREMENT FINI: %d S, ENVOYE AU PC DANS AURORA" : "RECORDING DONE: %d S, SENT TO THE PC FROM AURORA", secs);
	} else if (strstr(cur, "still starting")) {
		_snprintf(out, cap - 1, fr ? "JEU EN DEMARRAGE: REESSAIE DANS QUELQUES SECONDES" : "GAME STILL STARTING: TRY AGAIN IN A FEW SECONDS");
	} else if (strstr(cur, "disk almost full")) {
		_snprintf(out, cap - 1, fr ? "DISQUE PRESQUE PLEIN: PAS D'ENREGISTREMENT" : "DISK ALMOST FULL: NOT RECORDING");
	} else if (!strncmp(cur, "IDLE ", 5) && strcmp(cur, "IDLE ready") && strcmp(cur, "IDLE no game running")) {
		_snprintf(out, cap - 1, fr ? "ENREGISTREMENT: %s" : "RECORDING: %s", cur + 5);
		for (char* c = out; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
	}
	out[cap - 1] = '\0';
}
