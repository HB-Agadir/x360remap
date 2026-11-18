#pragma once
#include <stddef.h>
#include <stdint.h>

// --- Camera auto-detection for direct aim (v1.2.1, 2026-10-03) -------------
//
// What HB did by hand with Xenia + Cheat Engine, done by the plugin itself:
// the plugin already drives the right stick, so it can turn the camera by a
// known amount and watch which floats in the game's memory follow.
//
//   - push the stick right  -> the yaw changes by some amount d (sign s)
//   - push it left          -> it changes by about -d
//   - leave it alone        -> it does not move
//
// Anything that passes all three, several times in a row, is a camera angle.
//
// This file is PURE LOGIC: no Xbox API, no hardware access. The console side
// (reading memory, injecting the stick, timing, notifications) lives in
// main.cpp. Everything here is compiled and run for real with g++
// (X360Remap/tests/test_camera_learn.cpp) against a simulated game.

// ---------------------------------------------------------------------------
// Candidates
// ---------------------------------------------------------------------------

struct CamCandidate {
	uint32_t addr;        // address of the float in game memory
	float    last;        // last value read
	float    firstDelta;  // change seen on the first "positive" push (signed)
	int8_t   sign;        // sign of firstDelta: +1 / -1
	uint8_t  hits;        // signed tests passed
	uint8_t  persistent;  // 1 = a value written here is kept by the game
	uint8_t  followers;   // other candidates that followed a write here (master test)
};

struct CamLearnLimits {
	float minDelta;   // smallest change accepted for a stick push (radians games: ~0.05)
	float maxDelta;   // biggest change accepted (after wrap correction)
	float maxAbs;     // values beyond this are not angles
	float ratioMin;   // |reverse delta| / |first delta| must be within [ratioMin, ratioMax]
	float ratioMax;
	float idleTol;    // relative tolerance while the stick is centred
};

// Default limits, tuned for degree- and radian-based engines alike.
CamLearnLimits CamDefaultLimits();

// true if v can be a camera angle at all (finite, not tiny-denormal, |v| <= maxAbs).
bool CamPlausibleValue(float v, const CamLearnLimits& lim);

// after - before, corrected for a wrap at +-180 / 0..360 (degrees) or
// +-pi / 0..2pi (radians) when both values are inside such a range.
float CamAngleDelta(float before, float after);

// Step 1 - first positive push over one block of memory.
// before[i] / after[i] are the floats at baseAddr + 4*i before and after the
// push. Appends every word that changed by a plausible amount to out[],
// starting at out[count]. Returns the new count (never above cap).
size_t CamCollect(const float* before, const float* after, size_t words, uint32_t baseAddr,
                  const CamLearnLimits& lim, CamCandidate* out, size_t count, size_t cap);

// Filtering steps. now[i] is the value of c[i] read just after the step.
// expect = +1 : same push as the first one (must move like firstDelta)
// expect = -1 : opposite push (must move the other way, similar amount)
// expect =  0 : stick centred (must not move)
// Keeps the survivors at the front, updates last / hits, returns their count.
size_t CamFilter(CamCandidate* c, size_t n, const float* now, int expect, const CamLearnLimits& lim);

// Give a "first push" to candidates created without CamCollect (e.g. the
// neighbours of the yaw, tested as pitch). Sets sign / firstDelta from
// now - last, drops those that did not move plausibly. Returns survivors.
size_t CamEstablish(CamCandidate* c, size_t n, const float* now, const CamLearnLimits& lim);

// Persistence test: the plugin wrote 'written' (= value + firstDelta) and
// read 'readBack' a few frames later with the stick centred. true if the game
// kept it (a master value), false if it was rewritten from somewhere else (a
// render copy). Wrap-aware, so a write across +-180 / +-pi still counts.
bool CamPersistent(float written, float readBack, float firstDelta);

// Propagation test: after writing +written into one candidate, did ANOTHER
// candidate move by about the same amount (same sign)? If so, the game
// consumed our write: the written candidate is the master the camera uses,
// the other one a copy computed from it. 'before' / 'after' = values of the
// other candidate around the write.
bool CamFollowsWrite(float before, float after, float written);

// Best candidates first: most followers (the game used our write), then
// persistent ones, then most hits, then the one whose
// movement is closest to the median of all candidates (copies of the camera
// for weapons / models usually move differently).
void CamRank(CamCandidate* c, size_t n);

// Lessons from the PC tool (Xenia Camera Finder, MW3 2026-10-08):
//
// 1. The same physical memory is visible at several guest addresses
//    (0xA0000000 / 0xC0000000 / 0xE0000000 windows, the last one shifted by
//    0x1000): one value showed up 2-3 times. CamDedupeViews keeps one copy.
size_t CamDedupeViews(CamCandidate* c, size_t n);
// 2. The real camera angle is usually ALONE with its movement, while the
//    copies the game recomputes from it come in crowds (MW3: thousands of
//    values moving by exactly 0.247 - radians copies - and ONE value moving
//    by 11.168, the real angle in degrees, ranked #87 of 5995 this way and
//    far beyond the tested ones before). CamRankByRarity: rarest movement
//    first, bigger movement first among equals.
void CamRankByRarity(CamCandidate* c, size_t n);
// Signed distance from 'from' to 'to', seen through the same memory window:
// Metro 2033 (2026-10-08) gave yaw D71882C4 and pitch B71882C0 - the SAME
// object (pitch = yaw - 4) found through two different windows. A plain
// subtraction gave -0x20000004 and the pitch was dropped from the saved line.
int32_t CamViewDistance(uint32_t from, uint32_t to);

// Unit guess from a typical change produced by a short full-stick push:
// big changes = degrees, small = radians.
enum CamUnit { CAM_UNIT_DEGREES = 0, CAM_UNIT_RADIANS = 1 };
CamUnit CamGuessUnit(float typicalAbsDelta);

// ---------------------------------------------------------------------------
// Learned cameras (camera_addresses.txt, one line per game version)
// ---------------------------------------------------------------------------

struct CamRange { uint32_t start; uint32_t size; };

struct LearnedCamera {
	uint32_t titleId;
	uint32_t timeStamp;   // build identity: the executable's TimeDateStamp...
	uint32_t imageSize;   // ...and its image size (a title update changes both)
	uint8_t  levels;      // 0 = fixed addresses, 1 = [base] + yawAddr, 2 = [[base] + off1] + yawAddr
	int8_t   signX;       // sign of the yaw change when the stick goes RIGHT
	int8_t   signY;       // sign of the pitch change when the stick goes UP
	uint8_t  unit;        // CamUnit
	uint32_t base;        // levels >= 1: fixed address holding the first pointer
	uint32_t off1;        // levels == 2: offset added to [base] to reach the second pointer
	uint32_t yawAddr;     // levels 0: address; levels >= 1: offset from the last pointer
	uint32_t pitchAddr;   // levels 0: address (0 = none); levels >= 1: signed distance
	                      // from the yaw (0 = none: vertical stays on the stick)
	char     name[40];    // free text, shown in notifications / shared files
	uint32_t version;     // v3 lines: executable version (packed like the XEX
	                      // header: major 4 bits, minor 4, build 16, qfe 8),
	                      // used when timeStamp is 0; 0 = any version
};

// Text lines, easy to read and to share. Fixed addresses (old format, still read):
// "415608CB 4E6F2A10 01A40000 82743CBC 82743CB8 -1 -1 deg Modern Warfare 3 (solo)"
// With pointers (v2):
// "v2 415608CB 4E6F2A10 01A40000 1 826E0A80 00000000 000035F4 FFFFFFFC -1 -1 deg MW3 (multi)"
// Imported / version-keyed (v3, 2026-10-08): matched on Title ID + executable
// version instead of the build stamp ("*" = any version), like Xenia
// Mousehook does - lets known cameras be shared without a stamp:
// "v3 555307DC 0.0.0.1 1 829138B8 00000000 000003AC FFFFFFF8 -1 -1 deg Far Cry"
// Returns the number of characters written (0 on error).
int  CamFormatLine(const LearnedCamera& e, char* out, size_t outSize);
// Parses one line. Lines starting with '#' or empty lines return false.
bool CamParseLine(const char* line, LearnedCamera* out);

// "0.0.80.1" -> packed version; "1.0" -> 1.0.0.0; "*" or "" -> 0 (any).
uint32_t CamParseVersion(const char* s);
void     CamFormatVersion(uint32_t v, char* out, size_t outSize);

// Does this line apply to the running executable?
// Build stamp lines: same title + stamp (+ image size unless 0).
// Version lines (stamp 0): same title + version (or any version).
bool CamMatches(const LearnedCamera& e, uint32_t titleId, uint32_t stamp, uint32_t imageSize, uint32_t version);

// Cameras known in advance (camera_known.cpp): found with Xenia Camera
// Finder, or imported from Xenia Mousehook (BSD license, see that file).
extern const char* const kCamKnownLines[];
extern const size_t kCamKnownCount;

// Reads a 32-bit word of game memory; false if the address is not readable.
typedef bool (*CamRead32)(uint32_t addr, uint32_t* out, void* ctx);

// Current addresses of the yaw / pitch (pitch = 0 if none). Follows the
// pointers for levels >= 1. false if a pointer is null / unreadable (menus,
// loading screens: the mouse then stays on the stick).
bool CamResolve(const LearnedCamera& e, CamRead32 read, void* ctx, uint32_t* yaw, uint32_t* pitch);

// Pointer search: every word of the block (words[i] at blockAddr + 4*i) whose
// value v satisfies target - maxOff <= v <= target is a pointer to the object
// that contains target. Appends (location, value) pairs, returns the new count.
struct CamPointerHit { uint32_t loc; uint32_t value; };
size_t CamFindPointers(const uint32_t* words, size_t n, uint32_t blockAddr, uint32_t target, uint32_t maxOff,
                       CamPointerHit* out, size_t count, size_t cap);
// Same, against many targets at once (sorted ascending): returns, for each
// hit, the index of the target in hitTarget[]. Used for the second level.
size_t CamFindPointersMulti(const uint32_t* words, size_t n, uint32_t blockAddr, const uint32_t* sortedTargets,
                            size_t nTargets, uint32_t maxOff, CamPointerHit* out, size_t* hitTarget,
                            size_t count, size_t cap);

// Groups the readable, writable, cacheable pages of [start, end) into ranges,
// asking query(addr) for each step (MmQueryAddressProtect on the console).
// 0 / no-access / guard / no-cache / write-combine (GPU) pages are skipped.
typedef uint32_t (*CamQueryProtect)(uint32_t addr, void* ctx);
bool   CamProtectIsData(uint32_t protect);
size_t CamBuildRanges(CamQueryProtect query, void* ctx, uint32_t start, uint32_t end, uint32_t step,
                      CamRange* out, size_t count, size_t cap);

// Applies one mouse movement to the camera angles, same scale as the Call of
// Duty direct aim (10.5 mouse counts per degree at sensitivity 10000).
// yaw / pitch are updated in place (pitch may be null).
void CamApplyMouse(float* yaw, float* pitch, int32_t dx, int32_t dy, int32_t sensitivity,
                   bool invertY, const LearnedCamera& e);

// ---------------------------------------------------------------------------
// Where to search: the writable sections of the game's executable
// ---------------------------------------------------------------------------

// Reads the PE section table that ntHeaders points to (either the "PE\0\0"
// header itself or the "MZ" header in front of it; big- or little-endian
// fields, detected from the PowerPC machine id). Writes the writable,
// non-executable sections as absolute ranges (imageBase + RVA).
// headerBytes = how many bytes are readable at ntHeaders.
// Returns the number of ranges written, 0 if the header is not understood.
size_t CamParseWritableSections(const uint8_t* ntHeaders, size_t headerBytes, uint32_t imageBase,
                                CamRange* out, size_t cap);
