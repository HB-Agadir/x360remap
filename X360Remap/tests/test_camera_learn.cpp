// Tests for the camera auto-detection logic (camera_learn.cpp), run for real
// with g++ against a SIMULATED game: a block of memory full of noise, timers,
// copies of the camera and the real yaw / pitch, updated by a fake "game
// frame" that reads our injected stick - exactly what the console side does.
//
// g++ -std=c++98 -Wall -I.. ../camera_learn.cpp test_camera_learn.cpp -o t && ./t
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "../camera_learn.h"

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

// ---------------------------------------------------------------------------
// Simulated game
// ---------------------------------------------------------------------------
struct FakeGame {
    enum { WORDS = 65536 };            // 256 KB of "data section"
    float mem[WORDS];
    uint32_t base;
    int yawIdx, pitchIdx, copyIdx, modelIdx, timerIdx, swayIdx;
    float degPerSecYaw, degPerSecPitch;
    bool yawDecreasesRight;            // Call of Duty style
    bool radians;
    bool wrap180;
    bool overwriteCopy;                // copyIdx is rewritten from yaw every frame

    void Init(unsigned seed) {
        std::srand(seed);
        base = 0x82500000;
        for (int i = 0; i < WORDS; i++) {
            const int kind = std::rand() % 4;
            if (kind == 0) mem[i] = 0.0f;
            else if (kind == 1) mem[i] = (float)(std::rand() % 1000) / 10.0f;   // static floats
            else { uint32_t w = 0x82000000u | (uint32_t)std::rand(); std::memcpy(&mem[i], &w, 4); } // pointers
        }
        yawIdx = 40000; pitchIdx = 39999; copyIdx = 51234; modelIdx = 12000; timerIdx = 777; swayIdx = 30000;
        degPerSecYaw = 180.0f; degPerSecPitch = 120.0f;
        yawDecreasesRight = true; radians = false; wrap180 = false; overwriteCopy = true;
        mem[yawIdx] = 37.0f; mem[pitchIdx] = 0.0f; mem[copyIdx] = 37.0f; mem[modelIdx] = 37.0f;
        mem[timerIdx] = 10.0f; mem[swayIdx] = 0.0f;
    }
    // One game frame (16 ms) with the stick at rx, ry in -1..1.
    void Frame(float rx, float ry, int frameNo) {
        const float dt = 0.016f;
        float unit = radians ? 0.017453293f : 1.0f;
        float dyaw = (yawDecreasesRight ? -rx : rx) * degPerSecYaw * dt * unit;
        mem[yawIdx] += dyaw;
        if (wrap180) {
            const float lim = radians ? 3.1415927f : 180.0f;
            if (mem[yawIdx] > lim) mem[yawIdx] -= 2 * lim;
            if (mem[yawIdx] < -lim) mem[yawIdx] += 2 * lim;
        }
        float p = mem[pitchIdx] - ry * degPerSecPitch * dt * unit;   // up = negative, like CoD
        const float plim = 85.0f * unit;
        if (p > plim) p = plim;
        if (p < -plim) p = -plim;
        mem[pitchIdx] = p;
        if (overwriteCopy) mem[copyIdx] = mem[yawIdx];               // render copy, derived every frame
        mem[modelIdx] += dyaw * 0.5f;                                // player model follows slower
        mem[timerIdx] += dt;                                         // game clock
        mem[swayIdx] = 0.3f * std::sin(frameNo * 0.2f);              // weapon sway
        // random noise on a few words
        for (int k = 0; k < 20; k++) {
            int i = 1000 + (std::rand() % 500);
            mem[i] = (float)(std::rand() % 1000);
        }
    }
};

// Runs the frames for 'ms' milliseconds with the stick held.
static int g_frame = 0;
static void Hold(FakeGame& g, float rx, float ry, int ms) {
    for (int t = 0; t < ms; t += 16) g.Frame(rx, ry, g_frame++);
}

static void ReadCandidates(const FakeGame& g, const CamCandidate* c, size_t n, float* out) {
    for (size_t i = 0; i < n; i++) out[i] = g.mem[(c[i].addr - g.base) / 4];
}

// The same sequence main.cpp runs on the console (v2, 2026-10-06): the camera
// turns CONTINUOUSLY during the scan, each block is compared with itself
// 80 ms later (fast: a few seconds for the whole executable), then short
// right / left / stop rounds keep only the camera, and a write test finds
// the master value the game really uses.
static void Rebase(const FakeGame& g, CamCandidate* c, size_t n) {
    for (size_t i = 0; i < n; i++) c[i].last = g.mem[(c[i].addr - g.base) / 4];
}
static size_t RunYawSearch(FakeGame& g, CamCandidate* cand, size_t cap, bool vertical) {
    const CamLearnLimits lim = CamDefaultLimits();
    static float before[FakeGame::WORDS], now[16384];
    const float px = vertical ? 0.0f : 1.0f, py = vertical ? 1.0f : 0.0f;
    const int BLOCK = 8192;                       // words per block (32 KB here)
    Hold(g, px, py, 300);                         // start turning, keep turning
    size_t n = 0;
    for (int b = 0; b < FakeGame::WORDS; b += BLOCK) {
        std::memcpy(before, g.mem + b, BLOCK * 4);
        Hold(g, px, py, 80);
        n = CamCollect(before, g.mem + b, BLOCK, g.base + 4u * (uint32_t)b, lim, cand, n, cap);
    }
    Hold(g, 0, 0, 250);
    for (int round = 0; round < 3; round++) {
        Hold(g, px, py, 150); Rebase(g, cand, n); Hold(g, px, py, 80);
        ReadCandidates(g, cand, n, now); n = CamFilter(cand, n, now, +1, lim);
        Hold(g, -px, -py, 150); Rebase(g, cand, n); Hold(g, -px, -py, 80);
        ReadCandidates(g, cand, n, now); n = CamFilter(cand, n, now, -1, lim);
        Hold(g, 0, 0, 250); Rebase(g, cand, n); Hold(g, 0, 0, 200);
        ReadCandidates(g, cand, n, now); n = CamFilter(cand, n, now, 0, lim);
    }
    CamRank(cand, n);
    if (n > 16) n = 16;
    // Write test: write +D into one candidate; does it stay (persistent)?
    // does another candidate follow (the game used our write = master)?
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) now[j] = g.mem[(cand[j].addr - g.base) / 4];
        float* p = &g.mem[(cand[i].addr - g.base) / 4];
        const float D = 2.0f * cand[i].firstDelta;
        const float w = *p + D;
        *p = w;
        Hold(g, 0, 0, 100);
        cand[i].persistent = CamPersistent(w, *p, D) ? 1 : 0;
        cand[i].followers = 0;
        for (size_t j = 0; j < n; j++) {
            if (j == i) continue;
            if (CamFollowsWrite(now[j], g.mem[(cand[j].addr - g.base) / 4], D)) cand[i].followers++;
        }
        if (cand[i].persistent) *p = *p - D;      // turn back
        Hold(g, 0, 0, 100);
    }
    CamRank(cand, n);
    return n;
}

static void TestFindsYawDegrees() {
    std::printf("TestFindsYawDegrees\n");
    static FakeGame g; g.Init(1);
    static CamCandidate c[16384];
    size_t n = RunYawSearch(g, c, 16384, false);
    CHECK(n >= 1);
    if (n >= 1) {
        CHECK(c[0].addr == g.base + 4u * (uint32_t)g.yawIdx);
        CHECK(c[0].sign == -1);                 // stick right -> yaw decreases (CoD)
        CHECK(c[0].persistent == 1);
        CHECK(c[0].followers >= 1);             // the render copy followed our write
        CHECK(CamGuessUnit(c[0].firstDelta) == CAM_UNIT_DEGREES);
    }
    // the render copy is rewritten every frame -> not persistent, ranked after
    for (size_t i = 0; i < n; i++)
        if (c[i].addr == g.base + 4u * (uint32_t)g.copyIdx) CHECK(i > 0 && c[i].persistent == 0);
    // timer, sway and noise must all be gone
    for (size_t i = 0; i < n; i++) {
        CHECK(c[i].addr != g.base + 4u * (uint32_t)g.timerIdx);
        CHECK(c[i].addr != g.base + 4u * (uint32_t)g.swayIdx);
        const uint32_t idx = (c[i].addr - g.base) / 4;
        CHECK(!(idx >= 1000 && idx < 1500));
    }
    std::printf("  %u candidate(s), best %08X\n", (unsigned)n, n ? c[0].addr : 0);
}

static void TestNoCopyStillYaw() {
    std::printf("TestNoCopyStillYaw\n");
    // No render copy to follow a write: the yaw must still win over the
    // player model that turns at half speed (closest to the median movement).
    static FakeGame g; g.Init(4);
    g.overwriteCopy = false;
    g.mem[g.copyIdx] = 5.0f;
    static CamCandidate c[16384];
    size_t n = RunYawSearch(g, c, 16384, false);
    CHECK(n >= 1);
    if (n >= 1) CHECK(c[0].addr == g.base + 4u * (uint32_t)g.yawIdx);
}

static void TestRadiansAndWrap() {
    std::printf("TestRadiansAndWrap\n");
    static FakeGame g; g.Init(2);
    g.radians = true; g.wrap180 = true; g.yawDecreasesRight = false;
    g.mem[g.yawIdx] = 3.0f;                     // close to +pi: the search wraps
    static CamCandidate c[16384];
    size_t n = RunYawSearch(g, c, 16384, false);
    CHECK(n >= 1);
    if (n >= 1) {
        CHECK(c[0].addr == g.base + 4u * (uint32_t)g.yawIdx);
        CHECK(c[0].sign == 1);
        CHECK(CamGuessUnit(c[0].firstDelta) == CAM_UNIT_RADIANS);
    }
}

static void TestPitchFromNeighbours() {
    std::printf("TestPitchFromNeighbours\n");
    static FakeGame g; g.Init(3);
    const CamLearnLimits lim = CamDefaultLimits();
    const uint32_t yaw = g.base + 4u * (uint32_t)g.yawIdx;
    const int offs[6] = { -4, 4, -8, 8, -12, 12 };
    CamCandidate c[6]; float now[6];
    for (int i = 0; i < 6; i++) {
        std::memset(&c[i], 0, sizeof(c[i]));
        c[i].addr = yaw + offs[i];
        c[i].last = g.mem[(c[i].addr - g.base) / 4];
    }
    Hold(g, 0, 1, 220); Hold(g, 0, 0, 120);
    ReadCandidates(g, c, 6, now);
    size_t n = CamEstablish(c, 6, now, lim);
    Hold(g, 0, -1, 220); Hold(g, 0, 0, 120);
    ReadCandidates(g, c, n, now); n = CamFilter(c, n, now, -1, lim);
    Hold(g, 0, 0, 250);
    ReadCandidates(g, c, n, now); n = CamFilter(c, n, now, 0, lim);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(c[0].addr == g.base + 4u * (uint32_t)g.pitchIdx);
        CHECK(c[0].sign == -1);                 // stick up -> pitch decreases (CoD)
    }
}

static void TestLineRoundTrip() {
    std::printf("TestLineRoundTrip\n");
    LearnedCamera e; std::memset(&e, 0, sizeof(e));
    e.titleId = 0x415608CB; e.timeStamp = 0x4E6F2A10; e.imageSize = 0x01A40000;
    e.yawAddr = 0x82743CBC; e.pitchAddr = 0x82743CB8; e.signX = -1; e.signY = -1;
    e.unit = CAM_UNIT_DEGREES;
    std::strcpy(e.name, "Modern Warfare 3 (solo)");
    char line[160];
    CHECK(CamFormatLine(e, line, sizeof(line)) > 0);
    std::printf("  %s\n", line);
    LearnedCamera r;
    CHECK(CamParseLine(line, &r));
    CHECK(r.titleId == e.titleId && r.timeStamp == e.timeStamp && r.imageSize == e.imageSize);
    CHECK(r.yawAddr == e.yawAddr && r.pitchAddr == e.pitchAddr);
    CHECK(r.signX == -1 && r.signY == -1 && r.unit == CAM_UNIT_DEGREES);
    CHECK(std::strcmp(r.name, e.name) == 0);
    CHECK(!CamParseLine("# comment", &r));
    CHECK(!CamParseLine("", &r));
    CHECK(!CamParseLine("415608CB 0 0 82743CBC 0 2 -1 deg bad sign", &r));
    CHECK(CamParseLine("415608CB 0 0 82743CBC 0 1 1 rad X\r\n", &r) && r.unit == CAM_UNIT_RADIANS && std::strcmp(r.name, "X") == 0);
}

static void TestApplyMouseMatchesCod() {
    std::printf("TestApplyMouseMatchesCod\n");
    // Must give exactly what MouseHookCod does for CoD (signX = signY = -1, degrees).
    LearnedCamera e; std::memset(&e, 0, sizeof(e));
    e.signX = -1; e.signY = -1; e.unit = CAM_UNIT_DEGREES;
    float yaw = 100.0f, pitch = 0.0f;
    CamApplyMouse(&yaw, &pitch, 21, 21, 10000, false, e);
    CHECK(std::fabs(yaw - (100.0f - 2.0f)) < 1e-4f);     // CoD: yaw -= dx / 10.5
    CHECK(std::fabs(pitch - 2.0f) < 1e-4f);              // CoD: pitch += dy / 10.5
    yaw = 0; pitch = 0;
    CamApplyMouse(&yaw, &pitch, 0, 21, 10000, true, e);
    CHECK(std::fabs(pitch + 2.0f) < 1e-4f);              // inverted
    e.unit = CAM_UNIT_RADIANS; yaw = 0;
    CamApplyMouse(&yaw, 0, 21, 0, 20000, false, e);
    CHECK(std::fabs(yaw + 4.0f * 0.017453293f) < 1e-5f);
}

static void PutBE16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void PutBE32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void PutLE16(uint8_t* p, uint16_t v) { p[1] = (uint8_t)(v >> 8); p[0] = (uint8_t)v; }
static void PutLE32(uint8_t* p, uint32_t v) { p[3] = (uint8_t)(v >> 24); p[2] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[0] = (uint8_t)v; }

static void BuildPe(uint8_t* buf, bool be, bool withMz) {
    std::memset(buf, 0, 1024);
    uint8_t* pe = buf;
    if (withMz) {
        buf[0] = 'M'; buf[1] = 'Z';
        if (be) PutBE32(buf + 0x3C, 0x80); else PutLE32(buf + 0x3C, 0x80);
        pe = buf + 0x80;
    }
    pe[0] = 'P'; pe[1] = 'E';
    void (*w16)(uint8_t*, uint16_t) = be ? PutBE16 : PutLE16;
    void (*w32)(uint8_t*, uint32_t) = be ? PutBE32 : PutLE32;
    w16(pe + 4, 0x01F2);      // machine
    w16(pe + 6, 4);           // sections
    w16(pe + 20, 0xE0);       // optional header size
    uint8_t* s = pe + 24 + 0xE0;
    const char* names[4] = { ".text", ".rdata", ".data", ".bss" };
    const uint32_t rva[4] = { 0x10000, 0x400000, 0x500000, 0x600000 };
    const uint32_t vs[4]  = { 0x3F0000, 0x100000, 0x80000, 0x0 };
    const uint32_t raw[4] = { 0x3F0000, 0x100000, 0x80000, 0x200000 };
    const uint32_t ch[4]  = { 0x60000020, 0x40000040, 0xC0000040, 0xC0000080 };
    for (int i = 0; i < 4; i++, s += 40) {
        std::memcpy(s, names[i], std::strlen(names[i]));
        w32(s + 8, vs[i]); w32(s + 12, rva[i]); w32(s + 16, raw[i]); w32(s + 36, ch[i]);
    }
}

static void TestPeSections() {
    std::printf("TestPeSections\n");
    static uint8_t buf[1024];
    for (int variant = 0; variant < 4; variant++) {
        const bool be = (variant & 1) != 0, mz = (variant & 2) != 0;
        BuildPe(buf, be, mz);
        CamRange r[8];
        size_t n = CamParseWritableSections(buf, sizeof(buf), 0x82000000, r, 8);
        CHECK(n == 2);
        if (n == 2) {
            CHECK(r[0].start == 0x82500000 && r[0].size == 0x80000);
            CHECK(r[1].start == 0x82600000 && r[1].size == 0x200000);   // .bss: size from raw when virtual is 0
        }
    }
    std::memset(buf, 0, sizeof(buf));
    CamRange r[8];
    CHECK(CamParseWritableSections(buf, sizeof(buf), 0x82000000, r, 8) == 0);
}


// --- v3: pointers and memory ranges ------------------------------------------
struct FakeMem { uint32_t base; uint32_t words[4096]; };
static bool FakeRead(uint32_t addr, uint32_t* out, void* ctx) {
    FakeMem* m = (FakeMem*)ctx;
    if (addr < m->base || addr >= m->base + 4096u * 4u || (addr & 3)) return false;
    *out = m->words[(addr - m->base) / 4];
    return true;
}

static void TestLineV2AndResolve() {
    std::printf("TestLineV2AndResolve\n");
    LearnedCamera e; std::memset(&e, 0, sizeof(e));
    e.titleId = 0x545408A7; e.timeStamp = 0x51234567; e.imageSize = 0x02000000;
    e.levels = 2; e.base = 0x10000010; e.off1 = 0x20; e.yawAddr = 0x1C0; e.pitchAddr = (uint32_t)-4;
    e.signX = 1; e.signY = -1; e.unit = CAM_UNIT_RADIANS;
    std::strcpy(e.name, "GTA V");
    char line[200];
    CHECK(CamFormatLine(e, line, sizeof(line)) > 0);
    std::printf("  %s\n", line);
    LearnedCamera r;
    CHECK(CamParseLine(line, &r));
    CHECK(r.levels == 2 && r.base == e.base && r.off1 == e.off1 && r.yawAddr == e.yawAddr);
    CHECK((int32_t)r.pitchAddr == -4 && r.signX == 1 && r.signY == -1 && r.unit == CAM_UNIT_RADIANS);
    CHECK(std::strcmp(r.name, "GTA V") == 0);
    // memory: [0x10000010] = 0x10000100 ; [0x10000100 + 0x20] = 0x10000800 ; yaw = 0x100009C0
    static FakeMem m; std::memset(&m, 0, sizeof(m)); m.base = 0x10000000;
    m.words[0x10 / 4] = 0x10000100;
    m.words[0x120 / 4] = 0x10000800;
    uint32_t yaw = 0, pitch = 0;
    CHECK(CamResolve(r, FakeRead, &m, &yaw, &pitch));
    CHECK(yaw == 0x100009C0 && pitch == 0x100009BC);
    m.words[0x120 / 4] = 0;                       // object gone (menu / loading)
    CHECK(!CamResolve(r, FakeRead, &m, &yaw, &pitch));
    // Rogue Warrior (2026-11-03): pitch 0x101C BEFORE the yaw, past the old 4 KB limit
    m.words[0x120 / 4] = 0x10000800;
    CHECK(CamParseLine("v2 425307D3 4AD9EB64 00000000 2 10000010 00000020 00002000 FFFFEFE4 1 -1 rad Rogue Warrior", &r));
    CHECK(r.pitchAddr == 0xFFFFEFE4u);
    CHECK(CamResolve(r, FakeRead, &m, &yaw, &pitch) && yaw == 0x10002800 && pitch == yaw - 0x101C);
    CHECK(CamFormatLine(r, line, sizeof(line)) > 0 && std::strstr(line, " FFFFEFE4 ") != 0);
    // old fixed-address lines still read
    CHECK(CamParseLine("415608CB 4E6F2A10 01A40000 82743CBC 82743CB8 -1 -1 deg MW3", &r) && r.levels == 0);
    CHECK(CamResolve(r, FakeRead, &m, &yaw, &pitch) && yaw == 0x82743CBC && pitch == 0x82743CB8);
    CHECK(!CamParseLine("v2 545408A7 0 0 3 10000010 0 1C0 0 1 1 deg bad levels", &r));
}

static void TestPointerSearch() {
    std::printf("TestPointerSearch\n");
    uint32_t words[8] = { 0, 0x40001000, 0x40002000, 0x40002300, 0x82000000, 0x40002400, 7, 0x40002350 };
    CamPointerHit hits[8];
    // camera at 0x40002350: objects starting at most 0x1000 before it
    size_t n = CamFindPointers(words, 8, 0x82500000, 0x40002350, 0x1000, hits, 0, 8);
    CHECK(n == 3);                                   // 0x40002000, 0x40002300, 0x40002350
    CHECK(hits[0].loc == 0x82500008 && hits[0].value == 0x40002000);
    // many targets at once (second level)
    uint32_t targets[3] = { 0x40001010, 0x40002310, 0x50000000 };
    size_t which[8];
    n = CamFindPointersMulti(words, 8, 0x82500000, targets, 3, 0x100, hits, which, 0, 8);
    CHECK(n == 2);
    CHECK(hits[0].value == 0x40001000 && which[0] == 0);
    CHECK(hits[1].value == 0x40002300 && which[1] == 1);
}

static uint32_t FakeProtect(uint32_t a, void*) {
    if (a >= 0x40000000 && a < 0x40030000) return 0x04;          // read-write
    if (a >= 0x40030000 && a < 0x40040000) return 0x04 | 0x400;  // GPU write-combine
    if (a >= 0x40040000 && a < 0x40060000) return 0x02;          // read-only
    if (a >= 0x40060000 && a < 0x40080000) return 0x04;
    return 0;
}

static void TestBuildRanges() {
    std::printf("TestBuildRanges\n");
    CamRange r[8];
    size_t n = CamBuildRanges(FakeProtect, 0, 0x40000000, 0x40100000, 0x10000, r, 0, 8);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(r[0].start == 0x40000000 && r[0].size == 0x30000);
        CHECK(r[1].start == 0x40060000 && r[1].size == 0x20000);
    }
    n = CamBuildRanges(FakeProtect, 0, 0xFFFF0000u, 0, 0x10000, r, 0, 8);   // up to 4 GB, no wrap
    CHECK(n == 0);
}

static void TestRarityAndViews() {
    std::printf("TestRarityAndViews\n");
    static CamCandidate c[1000];
    std::memset(c, 0, sizeof(c));
    // 900 copies moving by 0.247 (radians), one real angle moving by 11.168
    // (degrees), 99 others moving by 1.3 - like the MW3 log.
    for (int i = 0; i < 900; i++) { c[i].addr = 0x40000000u + 4u * i; c[i].firstDelta = 0.247f; }
    for (int i = 900; i < 999; i++) { c[i].addr = 0x50000000u + 4u * i; c[i].firstDelta = 1.3f + 0.0001f * i; }
    c[999].addr = 0x82743CBC; c[999].firstDelta = -11.168f;
    CamRankByRarity(c, 1000);
    CHECK(c[0].addr == 0x82743CBC);
    // views: same physical memory at 0xA0.., 0xC0.., 0xE0.. (+0x1000)
    CamCandidate v[5]; std::memset(v, 0, sizeof(v));
    v[0].addr = 0xA0123450; v[1].addr = 0xC0123450; v[2].addr = 0xE0122450;  // same physical
    v[3].addr = 0x40001000; v[4].addr = 0xC0999990;
    CHECK(CamViewDistance(0xD71882C4u, 0xB71882C0u) == -4);   // Metro: same object, two windows
    CHECK(CamViewDistance(0x82743CBCu, 0x82743CB8u) == -4);
    size_t n = CamDedupeViews(v, 5);
    CHECK(n == 3);
    CHECK(v[0].addr == 0xA0123450 && v[1].addr == 0x40001000 && v[2].addr == 0xC0999990);
}

static void TestKnownAndVersions() {
    std::printf("TestKnownAndVersions\n");
    CHECK(CamParseVersion("0.0.80.1") == ((80u << 8) | 1u));
    CHECK(CamParseVersion("1.0") == (1u << 28));
    CHECK(CamParseVersion("0.0.5.1") == ((5u << 8) | 1u));
    CHECK(CamParseVersion("*") == 0);
    char v[24]; CamFormatVersion(CamParseVersion("0.0.3.8"), v, sizeof(v)); CHECK(std::strcmp(v, "0.0.3.8") == 0);
    // every known line must parse
    for (size_t i = 0; i < kCamKnownCount; i++) {
        LearnedCamera e;
        const bool ok = CamParseLine(kCamKnownLines[i], &e);
        if (!ok) std::printf("  cannot parse: %s\n", kCamKnownLines[i]);
        CHECK(ok);
        // and survive a format / parse round trip
        char line[200]; LearnedCamera r;
        CHECK(CamFormatLine(e, line, sizeof(line)) > 0 && CamParseLine(line, &r));
        CHECK(r.titleId == e.titleId && r.version == e.version && r.timeStamp == e.timeStamp &&
              r.levels == e.levels && r.base == e.base && r.off1 == e.off1 && r.yawAddr == e.yawAddr &&
              r.pitchAddr == e.pitchAddr && r.signX == e.signX && r.signY == e.signY && r.unit == e.unit);
    }
    // matching
    LearnedCamera fc; CHECK(CamParseLine("v3 555307DC 0.0.0.1 1 829138B8 00000000 000003AC FFFFFFF8 -1 -1 deg Far Cry", &fc));
    CHECK(CamMatches(fc, 0x555307DC, 0x12345678, 0, CamParseVersion("0.0.0.1")));
    CHECK(!CamMatches(fc, 0x555307DC, 0x12345678, 0, CamParseVersion("0.0.1.1")));
    LearnedCamera dr; CHECK(CamParseLine("v3 58410A8D * 0 00000000 00000000 AA4D2388 AA4D238C -1 -1 rad DR2", &dr));
    CHECK(CamMatches(dr, 0x58410A8D, 1, 2, 3));                  // any version
    LearnedCamera mw3; CHECK(CamParseLine(kCamKnownLines[0], &mw3));
    CHECK(CamMatches(mw3, 0x415608CB, 0x4E77262A, 0x1234, 0));   // stamp line, size 0 = any
    CHECK(!CamMatches(mw3, 0x415608CB, 0x4E77262B, 0x1234, 0));
    // Minecraft: two pointer levels, pitch = yaw + 4
    LearnedCamera mc; CHECK(CamParseLine("v3 584111F7 0.0.80.1 2 82C8518C 00000038 00000148 00000004 1 -1 deg MC", &mc));
    static FakeMem m; std::memset(&m, 0, sizeof(m)); m.base = 0x10000000;
    // can't place 82C8518C in FakeMem: just check the fields
    CHECK(mc.levels == 2 && mc.off1 == 0x38 && mc.yawAddr == 0x148 && (int32_t)mc.pitchAddr == 4);
}

int main() {
    TestFindsYawDegrees();
    TestNoCopyStillYaw();
    TestRadiansAndWrap();
    TestPitchFromNeighbours();
    TestLineRoundTrip();
    TestApplyMouseMatchesCod();
    TestPeSections();
    TestLineV2AndResolve();
    TestPointerSearch();
    TestBuildRanges();
    TestRarityAndViews();
    TestKnownAndVersions();
    if (g_failures == 0) std::printf("ALL CAMERA LEARN TESTS PASSED\n");
    else std::printf("%d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
