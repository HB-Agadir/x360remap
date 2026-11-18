// USB pads in known_devices.txt, type C (2026-11-05).
// From X360Remap/: cl /EHsc tests/test_known_pads.cpp known_devices.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../known_devices.h"

int main() {
	std::vector<KnownDeviceEntry> d = ParseKnownDevices("24AE:2015:K:Rapoo\n24AE:2015:M:Rapoo\n0079:0006:C:Generic USB Joystick\n1234:5678:X\n");
	assert(d.size() == 3 && d[2].isPad && !d[2].isMouse && !strcmp(d[2].name, "Generic USB Joystick"));
	assert(!d[0].isPad && !d[1].isPad);
	// Same VID:PID as a keyboard: a separate entry, not a rename of the keyboard.
	assert(AddKnownDevice(&d, 0x24AE, 0x2015, false, "Pad", true) && d.size() == 4);
	assert(!AddKnownDevice(&d, 0x24AE, 0x2015, false, "Pad", true));
	assert(!AddKnownDevice(&d, 0x0079, 0x0006, false, "Other name", true));   // a known name stays
	const std::string s = SerializeKnownDevices(d);
	assert(s.find("0079:0006:C:Generic USB Joystick\n") != std::string::npos);
	assert(s.find("24AE:2015:K:Rapoo\n") != std::string::npos);
	assert(ParseKnownDevices(s).size() == 4);
	printf("known pads OK\n");
	return 0;
}
