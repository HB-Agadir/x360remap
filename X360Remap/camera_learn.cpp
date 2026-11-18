#include "camera_learn.h"
#include <algorithm>
#include <vector>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef _MSC_VER
#define _snprintf snprintf   // g++ test build
#endif

// See camera_learn.h. Pure logic - compiled by VS2010 for the console and by
// g++ for the tests, so: no C++11 library features, no platform headers.

static float AbsF(float v) { return v < 0.0f ? -v : v; }

// Finite check without _finite / std::isfinite (they differ between the two
// compilers): NaN fails v == v, +-inf gives inf - inf = NaN.
static bool IsFiniteF(float v) {
	if (v != v) return false;
	const float d = v - v;
	return d == d;
}

CamLearnLimits CamDefaultLimits() {
	CamLearnLimits l;
	l.minDelta = 0.005f;
	l.maxDelta = 2000.0f;
	l.maxAbs   = 1000000.0f;
	l.ratioMin = 0.25f;
	l.ratioMax = 4.0f;
	l.idleTol  = 0.0001f;
	return l;
}

bool CamPlausibleValue(float v, const CamLearnLimits& lim) {
	if (!IsFiniteF(v)) return false;
	const float a = AbsF(v);
	if (a > lim.maxAbs) return false;
	// Tiny non-zero values are almost always integers / pointers seen as
	// floats (0x82xxxxxx reads as about -1e-37), never camera angles.
	if (a != 0.0f && a < 1e-20f) return false;
	return true;
}

float CamAngleDelta(float before, float after) {
	float d = after - before;
	const float ab = AbsF(before), aa = AbsF(after);
	// Degrees wrapping at +-180 or 0..360: a short push never turns 300 deg.
	if (ab <= 360.5f && aa <= 360.5f && AbsF(d) > 300.0f) {
		d += (d > 0.0f) ? -360.0f : 360.0f;
	}
	// Radians wrapping at +-pi or 0..2pi: a short push never turns 5.5 rad.
	else if (ab <= 6.3f && aa <= 6.3f && AbsF(d) > 5.5f) {
		d += (d > 0.0f) ? -6.2831853f : 6.2831853f;
	}
	return d;
}

size_t CamCollect(const float* before, const float* after, size_t words, uint32_t baseAddr,
                  const CamLearnLimits& lim, CamCandidate* out, size_t count, size_t cap) {
	for (size_t i = 0; i < words && count < cap; i++) {
		const float a = before[i], b = after[i];
		if (a == b) continue;                       // fast path: most memory does not move
		if (!CamPlausibleValue(a, lim) || !CamPlausibleValue(b, lim)) continue;
		const float d = CamAngleDelta(a, b);
		const float ad = AbsF(d);
		if (ad < lim.minDelta || ad > lim.maxDelta) continue;
		CamCandidate& c = out[count++];
		c.addr = baseAddr + (uint32_t)(i * 4);
		c.last = b;
		c.firstDelta = d;
		c.sign = (int8_t)(d > 0.0f ? 1 : -1);
		c.hits = 1;
		c.persistent = 0;
		c.followers = 0;
	}
	return count;
}

size_t CamFilter(CamCandidate* c, size_t n, const float* now, int expect, const CamLearnLimits& lim) {
	size_t kept = 0;
	for (size_t i = 0; i < n; i++) {
		CamCandidate cand = c[i];
		const float v = now[i];
		if (!CamPlausibleValue(v, lim)) continue;
		const float d = CamAngleDelta(cand.last, v);
		const float fd = AbsF(cand.firstDelta);
		bool ok;
		if (expect == 0) {
			float tol = lim.idleTol * (AbsF(cand.last) > 1.0f ? AbsF(cand.last) : 1.0f);
			if (tol < 0.02f * fd) tol = 0.02f * fd;
			ok = AbsF(d) <= tol;
		} else {
			const int want = expect * (int)cand.sign;
			const int got = (d > 0.0f) ? 1 : ((d < 0.0f) ? -1 : 0);
			const float ratio = (fd > 0.0f) ? AbsF(d) / fd : 0.0f;
			ok = (got == want) && ratio >= lim.ratioMin && ratio <= lim.ratioMax;
			if (ok && cand.hits < 255) cand.hits++;
		}
		if (!ok) continue;
		cand.last = v;
		c[kept++] = cand;
	}
	return kept;
}

size_t CamEstablish(CamCandidate* c, size_t n, const float* now, const CamLearnLimits& lim) {
	size_t kept = 0;
	for (size_t i = 0; i < n; i++) {
		CamCandidate cand = c[i];
		const float v = now[i];
		if (!CamPlausibleValue(v, lim) || !CamPlausibleValue(cand.last, lim)) continue;
		const float d = CamAngleDelta(cand.last, v);
		const float ad = AbsF(d);
		if (ad < lim.minDelta || ad > lim.maxDelta) continue;
		cand.firstDelta = d;
		cand.sign = (int8_t)(d > 0.0f ? 1 : -1);
		cand.hits = 1;
		cand.last = v;
		c[kept++] = cand;
	}
	return kept;
}

bool CamPersistent(float written, float readBack, float firstDelta) {
	const float d = AbsF(CamAngleDelta(written, readBack));
	return d <= 0.01f * AbsF(firstDelta) + 0.0001f;
}

bool CamFollowsWrite(float before, float after, float written) {
	const float d = CamAngleDelta(before, after);
	const float w = written;
	if (w == 0.0f) return false;
	if ((d > 0.0f) != (w > 0.0f)) return false;
	const float r = AbsF(d) / AbsF(w);
	return r >= 0.5f && r <= 2.0f;
}

// Distance (in log scale) between a candidate's movement and the median.
static float RankDistance(const CamCandidate& c, float median) {
	const float fd = AbsF(c.firstDelta);
	if (fd <= 0.0f || median <= 0.0f) return 1e9f;
	const float r = fd > median ? fd / median : median / fd;
	return r;   // 1 = exactly the median, grows both ways
}

static bool RankBefore(const CamCandidate& a, const CamCandidate& b, float median) {
	if (a.followers != b.followers) return a.followers > b.followers;
	if (a.persistent != b.persistent) return a.persistent > b.persistent;
	if (a.hits != b.hits) return a.hits > b.hits;
	return RankDistance(a, median) < RankDistance(b, median);
}

void CamRank(CamCandidate* c, size_t n) {
	if (n < 2) return;
	// Median of |firstDelta| (n is small - a few dozen at most - insertion sorts are fine).
	float tmp[64];
	size_t m = n < 64 ? n : 64;
	for (size_t i = 0; i < m; i++) tmp[i] = AbsF(c[i].firstDelta);
	for (size_t i = 1; i < m; i++) {
		float v = tmp[i]; size_t j = i;
		while (j > 0 && tmp[j - 1] > v) { tmp[j] = tmp[j - 1]; j--; }
		tmp[j] = v;
	}
	const float median = tmp[m / 2];
	for (size_t i = 1; i < n; i++) {
		CamCandidate v = c[i]; size_t j = i;
		while (j > 0 && RankBefore(v, c[j - 1], median)) { c[j] = c[j - 1]; j--; }
		c[j] = v;
	}
}

// --- Dedupe of the physical windows ------------------------------------------
struct ViewKey { uint32_t phys; size_t index; };
static bool ViewKeyLess(const ViewKey& a, const ViewKey& b) {
	return a.phys != b.phys ? a.phys < b.phys : a.index < b.index;
}

size_t CamDedupeViews(CamCandidate* c, size_t n) {
	if (n < 2) return n;
	std::vector<ViewKey> keys;
	keys.reserve(n);
	for (size_t i = 0; i < n; i++) {
		if (c[i].addr < 0xA0000000u) continue;                 // virtual / executable: unique
		ViewKey k;
		// 0xE0000000 window = physical + 0x1000 (Xenia and the console alike)
		k.phys = (c[i].addr >= 0xE0000000u) ? ((c[i].addr & 0x1FFFFFFFu) + 0x1000u) : (c[i].addr & 0x1FFFFFFFu);
		k.index = i;
		keys.push_back(k);
	}
	std::sort(keys.begin(), keys.end(), ViewKeyLess);
	std::vector<char> drop(n, 0);
	for (size_t i = 1; i < keys.size(); i++)
		if (keys[i].phys == keys[i - 1].phys) drop[keys[i].index] = 1;   // keep the first seen
	size_t k = 0;
	for (size_t i = 0; i < n; i++) if (!drop[i]) c[k++] = c[i];
	return k;
}

static uint32_t PhysOf(uint32_t a) {
	return (a >= 0xE0000000u) ? ((a & 0x1FFFFFFFu) + 0x1000u) : (a & 0x1FFFFFFFu);
}

int32_t CamViewDistance(uint32_t from, uint32_t to) {
	if (from >= 0xA0000000u && to >= 0xA0000000u) return (int32_t)(PhysOf(to) - PhysOf(from));
	return (int32_t)(to - from);
}

// --- Rarest movement first ----------------------------------------------------
struct RarityKey { size_t crowd; float absDelta; size_t index; };
static bool RarityLess(const RarityKey& a, const RarityKey& b) {
	if (a.crowd != b.crowd) return a.crowd < b.crowd;
	if (a.absDelta != b.absDelta) return a.absDelta > b.absDelta;
	return a.index < b.index;
}

void CamRankByRarity(CamCandidate* c, size_t n) {
	if (n < 2) return;
	std::vector<int> bucket(n), sorted(n);
	for (size_t i = 0; i < n; i++) {
		const float d = AbsF(c[i].firstDelta);
		bucket[i] = d > 0.0f ? (int)floor(log((double)d) / 0.02) : -1000000;   // 2 % wide buckets
		sorted[i] = bucket[i];
	}
	std::sort(sorted.begin(), sorted.end());
	std::vector<RarityKey> keys(n);
	for (size_t i = 0; i < n; i++) {
		keys[i].crowd = (size_t)(std::upper_bound(sorted.begin(), sorted.end(), bucket[i] + 1) -
		                         std::lower_bound(sorted.begin(), sorted.end(), bucket[i] - 1));
		keys[i].absDelta = AbsF(c[i].firstDelta);
		keys[i].index = i;
	}
	std::sort(keys.begin(), keys.end(), RarityLess);
	std::vector<CamCandidate> tmp(c, c + n);
	for (size_t i = 0; i < n; i++) c[i] = tmp[keys[i].index];
}

CamUnit CamGuessUnit(float typicalAbsDelta) {
	return (AbsF(typicalAbsDelta) >= 3.0f) ? CAM_UNIT_DEGREES : CAM_UNIT_RADIANS;
}

static void CleanName(const char* in, char* out, size_t outSize) {
	size_t k = 0;
	for (size_t i = 0; in[i] && k < outSize - 1; i++) {
		const char ch = in[i];
		out[k++] = (ch == '\r' || ch == '\n' || ch == '\t') ? ' ' : ch;
	}
	out[k] = '\0';
}

uint32_t CamParseVersion(const char* s) {
	if (!s || !*s || (s[0] == '*' && s[1] == '\0')) return 0;
	unsigned int p[4] = { 0, 0, 0, 0 };
	int k = 0;
	for (const char* q = s; *q && k < 4; q++) {
		if (*q == '.') { k++; continue; }
		if (*q < '0' || *q > '9') return 0;
		p[k] = p[k] * 10 + (unsigned int)(*q - '0');
	}
	return ((p[0] & 0xF) << 28) | ((p[1] & 0xF) << 24) | ((p[2] & 0xFFFF) << 8) | (p[3] & 0xFF);
}

void CamFormatVersion(uint32_t v, char* out, size_t outSize) {
	if (!out || outSize < 2) return;
	if (v == 0) { out[0] = '*'; out[1] = '\0'; return; }
	_snprintf(out, outSize - 1, "%u.%u.%u.%u", (unsigned int)(v >> 28), (unsigned int)((v >> 24) & 0xF),
		(unsigned int)((v >> 8) & 0xFFFF), (unsigned int)(v & 0xFF));
	out[outSize - 1] = '\0';
}

bool CamMatches(const LearnedCamera& e, uint32_t titleId, uint32_t stamp, uint32_t imageSize, uint32_t version) {
	if (e.titleId != titleId) return false;
	if (e.timeStamp != 0) return e.timeStamp == stamp && (e.imageSize == 0 || e.imageSize == imageSize);
	return e.version == 0 || e.version == version;
}

int CamFormatLine(const LearnedCamera& e, char* out, size_t outSize) {
	if (!out || outSize < 96) return 0;
	char name[sizeof(e.name)];
	CleanName(e.name, name, sizeof(name));
	const char* unit = e.unit == CAM_UNIT_RADIANS ? "rad" : "deg";
	int n;
	if (e.timeStamp == 0) {
		char ver[24];
		CamFormatVersion(e.version, ver, sizeof(ver));
		n = _snprintf(out, outSize - 1, "v3 %08X %s %d %08X %08X %08X %08X %d %d %s %s",
			(unsigned int)e.titleId, ver, (int)e.levels, (unsigned int)e.base, (unsigned int)e.off1,
			(unsigned int)e.yawAddr, (unsigned int)e.pitchAddr, (int)e.signX, (int)e.signY, unit, name);
	} else if (e.levels == 0) {
		n = _snprintf(out, outSize - 1, "%08X %08X %08X %08X %08X %d %d %s %s",
			(unsigned int)e.titleId, (unsigned int)e.timeStamp, (unsigned int)e.imageSize,
			(unsigned int)e.yawAddr, (unsigned int)e.pitchAddr,
			(int)e.signX, (int)e.signY, unit, name);
	} else {
		n = _snprintf(out, outSize - 1, "v2 %08X %08X %08X %d %08X %08X %08X %08X %d %d %s %s",
			(unsigned int)e.titleId, (unsigned int)e.timeStamp, (unsigned int)e.imageSize,
			(int)e.levels, (unsigned int)e.base, (unsigned int)e.off1,
			(unsigned int)e.yawAddr, (unsigned int)e.pitchAddr,
			(int)e.signX, (int)e.signY, unit, name);
	}
	out[outSize - 1] = '\0';
	return (n > 0 && (size_t)n < outSize) ? n : 0;
}

static bool ParseUnit(const char* u, uint8_t* out) {
	if (strcmp(u, "deg") == 0) { *out = CAM_UNIT_DEGREES; return true; }
	if (strcmp(u, "rad") == 0) { *out = CAM_UNIT_RADIANS; return true; }
	return false;
}

static void CopyName(const char* name, LearnedCamera* e) {
	size_t k = 0;
	while (name[k] && name[k] != '\r' && name[k] != '\n' && k < sizeof(e->name) - 1) {
		e->name[k] = name[k];
		k++;
	}
	e->name[k] = '\0';
	while (k > 0 && e->name[k - 1] == ' ') e->name[--k] = '\0';
}

// Pointer just after 'n' whitespace-separated tokens (and the spaces after
// them). Used for the name instead of sscanf's %n: on the console (VS2010 CRT)
// %n came back as 0 and the whole line ended up as the game's name
// (plugin log 2026-10-08, Metro: Last Light).
static const char* SkipTokens(const char* s, int n) {
	for (int k = 0; k < n; k++) {
		while (*s == ' ' || *s == '\t') s++;
		while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') s++;
	}
	while (*s == ' ' || *s == '\t') s++;
	return s;
}

bool CamParseLine(const char* line, LearnedCamera* out) {
	if (!line || !out) return false;
	while (*line == ' ' || *line == '\t') line++;
	if (*line == '\0' || *line == '#' || *line == '\r' || *line == '\n') return false;
	LearnedCamera e;
	memset(&e, 0, sizeof(e));
	char unit[8] = { 0 };
	int sx = 0, sy = 0, used = 0;
	int tokens = 8;                       // fields before the name (old format)
	if (line[0] == 'v' && line[1] == '3' && line[2] == ' ') {
		unsigned int tid = 0, base = 0, off1 = 0, ya = 0, pa = 0;
		int levels = 0;
		char ver[24] = { 0 };
		if (sscanf(line + 3, "%x %23s %d %x %x %x %x %d %d %7s %n", &tid, ver, &levels, &base, &off1,
		           &ya, &pa, &sx, &sy, unit, &used) < 10)
			return false;
		if (tid == 0 || levels < 0 || levels > 2 || (levels > 0 && base == 0) || (levels == 0 && ya == 0)) return false;
		e.titleId = tid; e.version = CamParseVersion(ver);
		if (e.version == 0 && strcmp(ver, "*") != 0) return false;
		e.levels = (uint8_t)levels; e.base = base; e.off1 = off1;
		e.yawAddr = ya; e.pitchAddr = pa;
		line += 3;
		tokens = 10;
	} else if (line[0] == 'v' && line[1] == '2' && line[2] == ' ') {
		unsigned int tid = 0, ts = 0, sz = 0, base = 0, off1 = 0, ya = 0, pa = 0;
		int levels = 0;
		if (sscanf(line + 3, "%x %x %x %d %x %x %x %x %d %d %7s %n", &tid, &ts, &sz, &levels, &base, &off1,
		           &ya, &pa, &sx, &sy, unit, &used) < 11)
			return false;
		if (tid == 0 || levels < 1 || levels > 2 || base == 0) return false;
		e.titleId = tid; e.timeStamp = ts; e.imageSize = sz;
		e.levels = (uint8_t)levels; e.base = base; e.off1 = off1;
		e.yawAddr = ya; e.pitchAddr = pa;
		line += 3;
		tokens = 11;
	} else {
		unsigned int tid = 0, ts = 0, sz = 0, ya = 0, pa = 0;
		if (sscanf(line, "%x %x %x %x %x %d %d %7s %n", &tid, &ts, &sz, &ya, &pa, &sx, &sy, unit, &used) < 8)
			return false;
		if (tid == 0 || ya == 0) return false;
		e.titleId = tid; e.timeStamp = ts; e.imageSize = sz;
		e.levels = 0; e.yawAddr = ya; e.pitchAddr = pa;
	}
	if ((sx != 1 && sx != -1) || (sy != 1 && sy != -1)) return false;
	if (!ParseUnit(unit, &e.unit)) return false;
	e.signX = (int8_t)sx; e.signY = (int8_t)sy;
	(void)used;
	CopyName(SkipTokens(line, tokens), &e);
	*out = e;
	return true;
}

bool CamResolve(const LearnedCamera& e, CamRead32 read, void* ctx, uint32_t* yaw, uint32_t* pitch) {
	*yaw = 0; *pitch = 0;
	if (e.levels == 0) {
		*yaw = e.yawAddr;
		*pitch = e.pitchAddr;
		return e.yawAddr != 0;
	}
	uint32_t p = 0;
	if (!read(e.base, &p, ctx) || p == 0 || (p & 3)) return false;
	if (e.levels == 2) {
		uint32_t q = 0;
		if (!read(p + e.off1, &q, ctx) || q == 0 || (q & 3)) return false;
		p = q;
	}
	if ((e.yawAddr & 3) || ((p + e.yawAddr) & 3)) return false;   // a float is never unaligned
	*yaw = p + e.yawAddr;
	*pitch = e.pitchAddr ? (uint32_t)((int32_t)*yaw + (int32_t)e.pitchAddr) : 0;
	return true;
}

size_t CamFindPointers(const uint32_t* words, size_t n, uint32_t blockAddr, uint32_t target, uint32_t maxOff,
                       CamPointerHit* out, size_t count, size_t cap) {
	const uint32_t lo = target >= maxOff ? target - maxOff : 0;
	for (size_t i = 0; i < n && count < cap; i++) {
		const uint32_t v = words[i];
		// Real object pointers are aligned (2026-10-12: an unaligned value that
		// happened to fall in range was saved as MW2's pointer, [826d4c60] + 5da,
		// and writing through it crashed the game).
		if (v & 3) continue;
		if (v < lo || v > target) continue;
		out[count].loc = blockAddr + (uint32_t)(i * 4);
		out[count].value = v;
		count++;
	}
	return count;
}

size_t CamFindPointersMulti(const uint32_t* words, size_t n, uint32_t blockAddr, const uint32_t* t,
                            size_t nt, uint32_t maxOff, CamPointerHit* out, size_t* hitTarget,
                            size_t count, size_t cap) {
	if (nt == 0) return count;
	const uint32_t lowest = t[0] >= maxOff ? t[0] - maxOff : 0;
	const uint32_t highest = t[nt - 1];
	for (size_t i = 0; i < n && count < cap; i++) {
		const uint32_t v = words[i];
		if (v & 3) continue;   // aligned pointers only, see CamFindPointers
		if (v < lowest || v > highest) continue;
		// smallest target >= v
		size_t lo = 0, hi = nt;
		while (lo < hi) {
			const size_t mid = (lo + hi) / 2;
			if (t[mid] < v) lo = mid + 1; else hi = mid;
		}
		if (lo >= nt || t[lo] - v > maxOff) continue;
		out[count].loc = blockAddr + (uint32_t)(i * 4);
		out[count].value = v;
		hitTarget[count] = lo;
		count++;
	}
	return count;
}

bool CamProtectIsData(uint32_t p) {
	if (p == 0) return false;
	if (p & (0x01u | 0x100u | 0x200u | 0x400u)) return false;   // no-access, guard, no-cache, write-combine
	return (p & (0x04u | 0x08u | 0x40u | 0x80u)) != 0;             // read-write, write-copy, execute-read-write(-copy)
}

size_t CamBuildRanges(CamQueryProtect query, void* ctx, uint32_t start, uint32_t end, uint32_t step,
                      CamRange* out, size_t count, size_t cap) {
	// end = 0 means "up to 4 GB". 64-bit counter: no wrap-around tricks.
	const uint64_t stop = end ? (uint64_t)end : 0x100000000ull;
	bool open = false;
	for (uint64_t a = start; a < stop; a += step) {
		const bool data = CamProtectIsData(query((uint32_t)a, ctx));
		if (data) {
			if (!open) {
				if (count >= cap) return count;
				out[count].start = (uint32_t)a; out[count].size = 0; open = true;
			}
			out[count].size += step;
		} else if (open) {
			count++; open = false;
		}
	}
	if (open) count++;
	return count;
}

void CamApplyMouse(float* yaw, float* pitch, int32_t dx, int32_t dy, int32_t sensitivity,
                   bool invertY, const LearnedCamera& e) {
	const float s = (sensitivity > 0 ? (float)sensitivity : 10000.0f) / 10000.0f;
	float k = s / 10.5f;                          // degrees per mouse count
	if (e.unit == CAM_UNIT_RADIANS) k *= 0.017453293f;
	if (yaw) *yaw = *yaw + (float)e.signX * (float)dx * k;
	if (pitch) {
		const int32_t down = invertY ? -dy : dy;  // mouse +Y = down = stick down
		*pitch = *pitch - (float)e.signY * (float)down * k;
	}
}

// --- PE section table -------------------------------------------------------

static uint16_t Rd16(const uint8_t* p, bool be) {
	return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]);
}
static uint32_t Rd32(const uint8_t* p, bool be) {
	return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
	          : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static bool IsPeSig(const uint8_t* p) { return p[0] == 'P' && p[1] == 'E' && p[2] == 0 && p[3] == 0; }

size_t CamParseWritableSections(const uint8_t* nt, size_t bytes, uint32_t imageBase,
                                CamRange* out, size_t cap) {
	if (!nt || bytes < 64 || !out || cap == 0) return 0;
	size_t peOff = (size_t)-1;
	if (IsPeSig(nt)) {
		peOff = 0;
	} else if (nt[0] == 'M' && nt[1] == 'Z') {
		const uint32_t c1 = Rd32(nt + 0x3C, false), c2 = Rd32(nt + 0x3C, true);
		if (c1 + 24 < bytes && IsPeSig(nt + c1)) peOff = c1;
		else if (c2 + 24 < bytes && IsPeSig(nt + c2)) peOff = c2;
	}
	if (peOff == (size_t)-1) return 0;
	const uint8_t* pe = nt + peOff;
	bool be;
	if (Rd16(pe + 4, true) == 0x01F2) be = true;        // IMAGE_FILE_MACHINE_POWERPCBE
	else if (Rd16(pe + 4, false) == 0x01F2) be = false;
	else return 0;
	const uint16_t nsec = Rd16(pe + 6, be);
	const uint16_t optSize = Rd16(pe + 20, be);
	size_t off = peOff + 24 + optSize;
	size_t written = 0;
	for (uint16_t i = 0; i < nsec && i < 96 && written < cap; i++, off += 40) {
		if (off + 40 > bytes) break;
		const uint8_t* s = nt + off;
		uint32_t vsize = Rd32(s + 8, be);
		const uint32_t rva = Rd32(s + 12, be);
		const uint32_t raw = Rd32(s + 16, be);
		const uint32_t ch = Rd32(s + 36, be);
		if (vsize == 0) vsize = raw;
		const bool writable = (ch & 0x80000000u) != 0;     // IMAGE_SCN_MEM_WRITE
		const bool code = (ch & 0x20000000u) != 0;         // IMAGE_SCN_MEM_EXECUTE
		if (!writable || code || vsize == 0) continue;
		out[written].start = imageBase + rva;
		out[written].size = vsize;
		written++;
	}
	return written;
}
