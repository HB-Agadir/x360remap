// Tests for the controllers known in advance (known_controllers.*).
//   g++ -std=c++11 -I.. -I../rapidjson/include test_known_controllers.cpp
//       ../known_controllers.cpp ../known_controllers_db.cpp ../mapping.cpp
//       ../mouse_calibration.cpp -o test_known_controllers
#include <cstdio>
#include <cstring>
#include "../known_controllers.h"

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static const HidAxisMapEntry* AxisFor(const HidDeviceMapping* m, int16_t ButtonsReport::* f) {
    for (int i = 0; i < m->axisMapCount; i++)
        if (m->axisMap[i].field == f) return &m->axisMap[i];
    return nullptr;
}
static int ButtonIdxFor(const HidDeviceMapping* m, uint8_t ButtonsReport::* f) {
    for (int i = 0; i < m->buttonMapCount; i++)
        if (m->buttonMap[i].field == f) return m->buttonMap[i].idx;
    return -1;
}

static void TestTableSortedAndUnique() {
    std::printf("table sorted, unique, sane (%u entries)\n", kKnownControllerCount);
    CHECK(kKnownControllerCount > 300);
    for (unsigned i = 1; i < kKnownControllerCount; i++) {
        const uint32_t a = ((uint32_t)kKnownControllers[i - 1].vid << 16) | kKnownControllers[i - 1].pid;
        const uint32_t b = ((uint32_t)kKnownControllers[i].vid << 16) | kKnownControllers[i].pid;
        CHECK(a < b);
    }
    for (unsigned i = 0; i < kKnownControllerCount; i++) {
        const KnownControllerEntry& e = kKnownControllers[i];
        CHECK(e.name && e.name[0]);
        for (int b = 0; b < KC_BUTTONS; b++) CHECK(e.btn[b] >= -1 && e.btn[b] < 64);
        for (int a = 0; a < KC_AXES; a++) CHECK(e.axis[a] >= -1 && e.axis[a] < 8);
        CHECK(FindKnownController(e.vid, e.pid) == &e);
    }
    CHECK(FindKnownController(0xFFFF, 0xFFFF) == nullptr);
    CHECK(FindKnownController(0x0000, 0x0000) == nullptr);
}

// Logitech Dual Action: X Y Z Rz in its descriptor (no Rx / Ry).
// SDL: leftx a0, lefty a1, rightx a2, righty a3 -> a3 must be Rz, not Rx.
static void TestAxesFollowTheDescriptor() {
    std::printf("SDL axis numbers resolved from the descriptor\n");
    ResetKnownControllerPool();
    const KnownControllerEntry* e = FindKnownController(0x046D, 0xC216);
    CHECK(e != nullptr);
    if (!e) return;
    // Descriptor order with the hat (0x39) and a duplicate in the middle.
    const uint16_t desc[] = { 0x30, 0x31, 0x32, 0x35, 0x39, 0x30 };
    const char* name = nullptr;
    HidDeviceMapping* m = AttachKnownController(0x046D, 0xC216, desc, 6, &name);
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(name && strstr(name, "Logitech"));
    const HidAxisMapEntry* lx = AxisFor(m, &ButtonsReport::x);
    const HidAxisMapEntry* ly = AxisFor(m, &ButtonsReport::y);
    const HidAxisMapEntry* rx = AxisFor(m, &ButtonsReport::z);
    const HidAxisMapEntry* ry = AxisFor(m, &ButtonsReport::rz);
    CHECK(lx && lx->usage == HID_USAGE_AXIS_X);
    CHECK(ly && ly->usage == HID_USAGE_AXIS_Y);
    CHECK(rx && rx->usage == HID_USAGE_AXIS_Z);
    CHECK(ry && ry->usage == HID_USAGE_AXIS_RZ);
    // Up = positive on the Xbox: both Y axes inverted, nothing else.
    CHECK(m->invert.invertY && m->invert.invertRZ);
    CHECK(!m->invert.invertX && !m->invert.invertZ && !m->invert.invertRX && !m->invert.invertRY);
    // Buttons: SDL a:b1 b:b2 x:b0 y:b3 (HID usage = number + 1).
    CHECK(ButtonIdxFor(m, &ButtonsReport::a_button) == 1);
    CHECK(ButtonIdxFor(m, &ButtonsReport::b_button) == 2);
    CHECK(ButtonIdxFor(m, &ButtonsReport::x_button) == 0);
    CHECK(ButtonIdxFor(m, &ButtonsReport::y_button) == 3);
    CHECK(ButtonIdxFor(m, &ButtonsReport::start) == 9);
    // Same pad plugged again: same model, no second slot.
    CHECK(AttachKnownController(0x046D, 0xC216, desc, 6, nullptr) == m);
}

// The same SDL number means a different usage when Rx / Ry exist.
static void TestFullAxisSet() {
    std::printf("full X Y Z Rx Ry Rz set\n");
    ResetKnownControllerPool();
    const KnownControllerEntry* e = FindKnownController(0x0079, 0x0002);   // righty = a4
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(e->axis[3] == 4);
    const uint16_t desc[] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 };
    HidDeviceMapping* m = AttachKnownController(0x0079, 0x0002, desc, 6, nullptr);
    CHECK(m != nullptr);
    if (!m) return;
    const HidAxisMapEntry* ry = AxisFor(m, &ButtonsReport::rz);
    CHECK(ry && ry->usage == HID_USAGE_AXIS_RY);
    CHECK(m->invert.invertRY);
    // An axis the descriptor does not have is left out, not guessed.
    const uint16_t small[] = { 0x30, 0x31 };
    ResetKnownControllerPool();
    m = AttachKnownController(0x0079, 0x0002, small, 2, nullptr);
    CHECK(m && m->axisMapCount == 2);
}

// HB's controller (2026-10-23): 0079:0006 "USB Joystick" (DragonRise chip),
// descriptor X Y Z Rz + hat, buttons 1..12. Several list lines share this
// id; the merged entry asks right Y = a4, which this pad does not have ->
// the usual layout must be used: right X = Z, right Y = Rz.
static void TestDragonRiseFallback() {
    std::printf("0079:0006: merged entry + right stick fallback\n");
    ResetKnownControllerPool();
    const KnownControllerEntry* e = FindKnownController(0x0079, 0x0006);
    CHECK(e != nullptr);
    if (!e) return;
    const uint16_t desc[] = { 0x30, 0x31, 0x32, 0x35, 0x39 };
    HidDeviceMapping* m = AttachKnownController(0x0079, 0x0006, desc, 5, nullptr);
    CHECK(m != nullptr);
    if (!m) return;
    const HidAxisMapEntry* rx = AxisFor(m, &ButtonsReport::z);
    const HidAxisMapEntry* ry = AxisFor(m, &ButtonsReport::rz);
    CHECK(rx && rx->usage == HID_USAGE_AXIS_Z);
    CHECK(ry && ry->usage == HID_USAGE_AXIS_RZ);
    CHECK(!m->invert.invertZ);          // majority of the lines: not inverted
    CHECK(m->invert.invertRZ);          // Y axes: up = positive on the Xbox
    CHECK(ButtonIdxFor(m, &ButtonsReport::a_button) == 2);   // usage 3
    CHECK(ButtonIdxFor(m, &ButtonsReport::y_button) == 0);   // usage 1
    CHECK(ButtonIdxFor(m, &ButtonsReport::start) == 9);      // usage 10
}

static void TestFindMappingOrder() {
    std::printf("FindMapping: JSON / built-in first, then the known list\n");
    ResetKnownControllerPool();
    g_extraMappingLookup = FindKnownControllerMapping;
    CHECK(FindMapping(0x046D, 0xC216) == nullptr);      // not plugged yet
    const uint16_t desc[] = { 0x30, 0x31, 0x32, 0x35 };
    HidDeviceMapping* m = AttachKnownController(0x046D, 0xC216, desc, 4, nullptr);
    CHECK(m && FindMapping(0x046D, 0xC216) == m);       // survives a JSON reload
    // DualShock 4 is in the SDL list too, but the built-in mapping wins.
    HidDeviceMapping* ds4 = FindStaticMapping(1356, 2508);
    CHECK(ds4 && FindMapping(1356, 2508) == ds4);
    // Unknown pad: nothing, the controller assistant handles it.
    CHECK(AttachKnownController(0x30FA, 0x0399, desc, 4, nullptr) == nullptr);
    g_extraMappingLookup = nullptr;
    CHECK(FindMapping(0x046D, 0xC216) == nullptr);
}

static void TestPoolFull() {
    std::printf("pool full -> nullptr, earlier models untouched\n");
    ResetKnownControllerPool();
    const uint16_t desc[] = { 0x30, 0x31, 0x32, 0x35 };
    HidDeviceMapping* first = nullptr;
    unsigned made = 0;
    for (unsigned i = 0; i < kKnownControllerCount && made < KC_POOL_SIZE; i++) {
        HidDeviceMapping* m = AttachKnownController(kKnownControllers[i].vid, kKnownControllers[i].pid, desc, 4, nullptr);
        if (m) { if (!first) first = m; made++; }
    }
    CHECK(made == KC_POOL_SIZE);
    const KnownControllerEntry& last = kKnownControllers[kKnownControllerCount - 1];
    const char* name = nullptr;
    CHECK(AttachKnownController(last.vid, last.pid, desc, 4, &name) == nullptr);
    CHECK(name != nullptr);
    CHECK(first && first->vendorId == kKnownControllers[0].vid);
}

static void TestTriggerConversion() {
    std::printf("trigger byte: same as before for 8-bit, right for 10-bit\n");
    for (int raw = 0; raw <= 255; raw++) {
        // HidFillButtonsReport scaling, logical 0..255
        int32_t scaled = (int32_t)(((int64_t)raw * 65535 + 127) / 255) - 32768;
        int16_t v = (int16_t)scaled;
        unsigned char oldByte = (unsigned char)v;
        unsigned char newByte = (unsigned char)(((int32_t)v + 32768) >> 8);
        CHECK(oldByte == newByte);
    }
    // 10-bit trigger at half travel: about 128, not a random low byte.
    int32_t scaled = (int32_t)(((int64_t)512 * 65535 + 511) / 1023) - 32768;
    unsigned char b = (unsigned char)(((int32_t)(int16_t)scaled + 32768) >> 8);
    CHECK(b >= 126 && b <= 130);
}

int main() {
    TestTableSortedAndUnique();
    TestAxesFollowTheDescriptor();
    TestFullAxisSet();
    TestDragonRiseFallback();
    TestFindMappingOrder();
    TestPoolFull();
    TestTriggerConversion();
    if (g_failures) { std::printf("%d FAILURE(S)\n", g_failures); return 1; }
    std::printf("ALL KNOWN CONTROLLER TESTS PASSED\n");
    return 0;
}
