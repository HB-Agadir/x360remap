// Macros in X360Remap.json: held combos (2026-11-03), sequences (2026-11-05), and
// the copy of a built-in pad mapping.
// From X360Remap/: cl /EHsc /I. tests/test_macros.cpp mapping.cpp mouse_calibration.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "../mapping.h"

static const uint32_t A = 1u << 0, B = 1u << 1, X = 1u << 2, DOWN = 1u << 16, RIGHT = 1u << 14;

int main() {
	const char* json =
		"[ { \"vid\": 121, \"pid\": 6,"
		"    \"buttons\": [ { \"idx\": 0, \"field\": \"a_button\" } ],"
		"    \"macros\": [ { \"idx\": 4, \"fields\": [\"a_button\", \"b_button\"] },"
		"                  { \"idx\": 5, \"fields\": [\"x_button\"] },"
		"                  { \"idx\": 6, \"steps\": [ { \"fields\": [\"dpad_down\"], \"ms\": 50 },"
		"                                             { \"fields\": [\"dpad_down\", \"dpad_right\"], \"ms\": 5 },"
		"                                             { \"fields\": [], \"ms\": 9000 },"
		"                                             { \"fields\": [\"dpad_right\", \"x_button\", \"nope\"], \"ms\": 80 } ] } ] } ]";
	assert(LoadMappingsFromJson(json));
	const HidDeviceMapping* m = FindMapping(121, 6);
	assert(m && IsDynamicMapping(m));
	assert(m->macroCount == 2);                     // a single field is not a macro
	assert(m->macros[0].idx == 4 && m->macros[0].stepCount == 1);
	assert(m->macros[0].steps[0].mask == (A | B) && m->macros[0].steps[0].ms == 0);
	const HidButtonMacro& seq = m->macros[1];
	assert(seq.stepCount == 4 && seq.steps[0].mask == DOWN && seq.steps[1].mask == (DOWN | RIGHT));
	assert(seq.steps[1].ms == 16 && seq.steps[2].ms == 5000 && seq.steps[2].mask == 0);   // clamped, a pause
	assert(seq.steps[3].mask == (RIGHT | X));      // unknown names skipped

	// Saved and read back unchanged: a Studio save must not drop them.
	const std::string saved = SaveMappingsToJson();
	assert(saved.find("\"steps\"") != std::string::npos);
	ClearDynamicMappings();
	assert(LoadMappingsFromJson(saved));
	m = FindMapping(121, 6);
	assert(m && m->macroCount == 2 && m->macros[1].stepCount == 4 && m->macros[1].steps[3].ms == 80 && m->buttonMapCount == 1);
	assert(m->macros[0].stepCount == 1 && m->macros[0].steps[0].mask == (A | B));

	// Playback of a held combo: while held only.
	MacroPlay p = {};
	assert(MacroAdvance(m->macros[0], p, 0, false) == 0);
	assert(MacroAdvance(m->macros[0], p, 10, true) == (A | B));
	assert(MacroAdvance(m->macros[0], p, 500, true) == (A | B));
	assert(MacroAdvance(m->macros[0], p, 510, false) == 0 && !p.playing);

	// Playback of a sequence: one press plays it all, even released; every step reaches a read.
	HidButtonMacro s = {};
	s.idx = 1; s.stepCount = 3;
	s.steps[0].mask = DOWN; s.steps[0].ms = 50;
	s.steps[1].mask = DOWN | RIGHT; s.steps[1].ms = 16;
	s.steps[2].mask = RIGHT | X; s.steps[2].ms = 80;
	p = MacroPlay();
	assert(MacroAdvance(s, p, 1000, true) == DOWN);
	assert(MacroAdvance(s, p, 1033, false) == DOWN);          // released: it goes on
	assert(MacroAdvance(s, p, 1066, false) == (DOWN | RIGHT)); // step 0 over at 1050
	assert(MacroAdvance(s, p, 1200, true) == (RIGHT | X));     // a late read still sees step 2; a press now is ignored
	assert(MacroAdvance(s, p, 1250, true) == (RIGHT | X));
	assert(MacroAdvance(s, p, 1290, true) == 0 && !p.playing); // done
	assert(MacroAdvance(s, p, 1300, true) == 0);               // still held: no restart
	assert(MacroAdvance(s, p, 1310, false) == 0);
	assert(MacroAdvance(s, p, 1320, true) == DOWN);            // a new press plays it again

	// A held step at the end lasts while the button is held.
	HidButtonMacro h = {};
	h.stepCount = 2; h.steps[0].mask = A; h.steps[0].ms = 20; h.steps[1].mask = B; h.steps[1].ms = 0;
	p = MacroPlay();
	assert(MacroAdvance(h, p, 0, true) == A);
	assert(MacroAdvance(h, p, 30, true) == B);
	assert(MacroAdvance(h, p, 900, true) == B);
	assert(MacroAdvance(h, p, 910, false) == 0 && !p.playing);

	ButtonsReport r = {};
	ApplyMacroMask(&r, RIGHT | X);
	assert(r.dpad_right == 1 && r.x_button == 1 && r.a_button == 0);

	// A built-in mapping copied into the tables keeps its axes and buttons.
	static const HidAxisMapEntry axes[] = { { HID_USAGE_AXIS_X, &ButtonsReport::x } };
	static const HidButtonMapEntry buttons[] = { { 1, &ButtonsReport::b_button }, { 2, &ButtonsReport::x_button } };
	HidDeviceMapping builtIn = {};
	builtIn.vendorId = 0x1234; builtIn.productId = 0x5678;
	builtIn.axisMap = axes; builtIn.axisMapCount = 1;
	builtIn.buttonMap = buttons; builtIn.buttonMapCount = 2;
	assert(!IsDynamicMapping(&builtIn));
	AddDynamicCopy(builtIn);
	m = FindMapping(0x1234, 0x5678);
	assert(m && IsDynamicMapping(m) && m->axisMapCount == 1 && m->buttonMapCount == 2 && m->macroCount == 0);
	assert(SaveMappingsToJson().find("22136") != std::string::npos);

	printf("macros OK\n");
	return 0;
}
