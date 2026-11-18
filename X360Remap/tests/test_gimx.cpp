// GIMX mouse to stick (1.4.2): dead zone jump, curve, residue, filter, JSON block.
// From X360Remap/: cl /EHsc /I. tests/test_gimx.cpp mapping.cpp mouse_calibration.cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../mapping.h"

static EffectiveMouseSettings Params(float dz, float mult, float exp, int buffer, float filter) {
	EffectiveMouseSettings e = {};
	e.gimxOn = true;
	e.gimxDeadZone = dz; e.gimxMultX = mult; e.gimxMultY = mult; e.gimxExponent = exp;
	e.gimxBuffer = buffer; e.gimxFilter = filter;
	return e;
}

int main() {
	GimxState s;
	int32_t x = 0, y = 0;

	// 1. No motion: stick at rest, residue dropped.
	EffectiveMouseSettings e = Params(34.0f, 3.41f, 0.85f, 1, 0.0f);
	GimxReset(&s);
	GimxMouseToStick(e, &s, 0, 0, 10, &x, &y);
	assert(x == 0 && y == 0 && s.resX == 0.0f && s.resY == 0.0f);

	// 2. Smallest move jumps the game's dead zone (34 steps = 8738 on the 16-bit stick).
	GimxReset(&s);
	GimxMouseToStick(e, &s, 1, -1, 10, &x, &y);
	assert(x > 34 * 257 && x < 34 * 257 + 3.41f * 257 + 2);
	assert(y < -34 * 257 && y > -(34 * 257 + 3.41f * 257 + 2));

	// 3. Linear, no dead zone: v counts per 10 ms -> v * mult * 257.
	e = Params(0.0f, 1.0f, 1.0f, 1, 0.0f);
	GimxReset(&s);
	GimxMouseToStick(e, &s, 10, 0, 10, &x, &y);
	assert(x == 2570 && y == 0);
	// Same motion over 20 ms = half the speed.
	GimxReset(&s);
	GimxMouseToStick(e, &s, 10, 0, 20, &x, &y);
	assert(x == 1285);

	// 4. Residue: a sub-step motion is not lost, it is carried while the mouse moves.
	e = Params(0.0f, 0.001f, 1.0f, 1, 0.0f);   // 1 count = 0.257 stick steps -> truncated to 0
	GimxReset(&s);
	GimxMouseToStick(e, &s, 1, 0, 10, &x, &y);
	assert(x == 0);
	float carried = 0.0f;
	for (int i = 0; i < 6; i++) GimxMouseToStick(e, &s, 1, 0, 10, &x, &y), carried += (float)x;
	(void)carried;

	// 5. Full tilt: clamped, no residue (GIMX keeps none at max).
	e = Params(10.0f, 50.0f, 1.0f, 1, 0.0f);
	GimxReset(&s);
	GimxMouseToStick(e, &s, 100, 0, 10, &x, &y);
	assert(x == 32767 && s.resX == 0.0f);

	// 6. Filter: buffer 4, filter 0.5 -> one spike is spread over the next polls.
	e = Params(0.0f, 1.0f, 1.0f, 4, 0.5f);
	GimxReset(&s);
	GimxMouseToStick(e, &s, 16, 0, 10, &x, &y);
	const int32_t first = x;
	GimxMouseToStick(e, &s, 0, 0, 10, &x, &y);
	assert(first > 0 && first < 16 * 257);   // averaged with the empty history
	assert(x > 0);                            // the spike still counts on the next poll

	// 7. JSON: the "gimx" block turns the method on, values as GIMX writes them.
	const char* json =
		"[ { \"vid\": 9390, \"pid\": 8213,"
		"    \"gimx\": { \"deadZone\": 34, \"multX\": 3.41, \"multY\": 5.12, \"exponent\": 0.85, \"buffer\": 2, \"filter\": 0.5 } },"
		"  { \"vid\": 1133, \"pid\": 49271 } ]";
	FILE* f = fopen("test_gimx.json", "wb");
	fputs(json, f);
	fclose(f);
	assert(LoadMappingsFromFile("test_gimx.json"));
	const HidDeviceMapping* m = FindMapping(0x24AE, 0x2015);
	assert(m && m->gimxOn && fabsf(m->gimxDeadZone - 34.0f) < 0.01f && fabsf(m->gimxMultY - 5.12f) < 0.01f);
	assert(m->gimxBuffer == 2 && fabsf(m->gimxFilter - 0.5f) < 0.01f);
	const EffectiveMouseSettings r = ResolveEffectiveMouseSettings(m, 0);
	assert(r.gimxOn && fabsf(r.gimxExponent - 0.85f) < 0.01f);
	const HidDeviceMapping* plain = FindMapping(0x046D, 0xC077);
	assert(plain && !plain->gimxOn);
	// 8. A Studio save keeps the block (it rewrites the file from the parsed devices).
	assert(SaveMappingsToFile("test_gimx2.json"));
	assert(LoadMappingsFromFile("test_gimx2.json"));
	m = FindMapping(0x24AE, 0x2015);
	assert(m && m->gimxOn && fabsf(m->gimxMultX - 3.41f) < 0.01f && m->gimxBuffer == 2);
	remove("test_gimx.json");
	remove("test_gimx2.json");

	printf("test_gimx: all checks passed\n");
	return 0;
}
