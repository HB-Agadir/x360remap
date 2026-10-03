// Voir aurora_library.h. Code portable : teste sur PC (tests/test_aurora_library.cpp).
#include "aurora_library.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace {

// --------------------------------------------------------------------------
// Utilitaires gros-boutistes (SQLite et Aurora stockent tout en big-endian,
// independamment de la machine qui lit : ces fonctions marchent sur PC comme
// sur Xbox 360).
// --------------------------------------------------------------------------
uint32_t Be16(const unsigned char* p) { return ((uint32_t)p[0] << 8) | p[1]; }
uint32_t Be32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// ==========================================================================
// SQLITE - lecteur minimal (parcours de tables en lecture seule)
// ==========================================================================
struct Db {
    const unsigned char* p;
    size_t   len;
    uint32_t pageSize;
    uint32_t usable;
    int      enc;       // 1 = UTF-8, 2 = UTF-16LE, 3 = UTF-16BE
};

const unsigned char* PageAt(const Db& db, uint32_t pgno) {
    if (pgno == 0) return 0;
    const uint64_t off = (uint64_t)(pgno - 1) * db.pageSize;
    if (off + db.pageSize > db.len) return 0;
    return db.p + off;
}

// Entier a longueur variable de SQLite (1 a 9 octets).
bool Varint(const unsigned char*& p, const unsigned char* end, uint64_t& v) {
    v = 0;
    for (int i = 0; i < 9; i++) {
        if (p >= end) return false;
        const unsigned char b = *p++;
        if (i == 8) { v = (v << 8) | b; return true; }
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return true;
    }
    return true;
}

// Contenu complet d'une cellule de feuille de table (avec pages de debordement).
bool LeafPayload(const Db& db, const unsigned char* cell, const unsigned char* pageEnd,
                 std::vector<unsigned char>& out, int64_t& rowid) {
    uint64_t P = 0, rid = 0;
    const unsigned char* c = cell;
    if (!Varint(c, pageEnd, P) || !Varint(c, pageEnd, rid)) return false;
    rowid = (int64_t)rid;
    if (P > 64u * 1024u * 1024u) return false;              // garde-fou
    const uint64_t U = db.usable, X = U - 35;
    uint64_t local = P;
    if (P > X) {
        const uint64_t M = ((U - 12) * 32 / 255) - 23;
        const uint64_t K = M + ((P - M) % (U - 4));
        local = (K <= X) ? K : M;
    }
    if (c + local > pageEnd) return false;
    out.assign(c, c + (size_t)local);
    if (P > local) {
        if (c + local + 4 > pageEnd) return false;
        uint32_t ovfl = Be32(c + local);
        int guard = 0;
        while (out.size() < P && ovfl != 0 && guard++ < 100000) {
            const unsigned char* pg = PageAt(db, ovfl);
            if (!pg) return false;
            const size_t take = (size_t)((P - out.size()) < (U - 4) ? (P - out.size()) : (U - 4));
            out.insert(out.end(), pg + 4, pg + 4 + take);
            ovfl = Be32(pg);
        }
        if (out.size() != P) return false;
    }
    return true;
}

struct Val {
    int         kind;   // 0 null, 1 entier, 2 reel, 3 texte, 4 blob
    int64_t     i;
    std::string s;
};

std::string Utf16ToUtf8(const unsigned char* p, size_t n, bool be) {
    std::string r;
    for (size_t k = 0; k + 1 < n; k += 2) {
        const unsigned c = be ? ((p[k] << 8) | p[k + 1]) : ((p[k + 1] << 8) | p[k]);
        if (c < 0x80) r += (char)c;
        else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 0x3F)); }
        else { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 0x3F)); r += (char)(0x80 | (c & 0x3F)); }
    }
    return r;
}

// Decode un enregistrement SQLite (en-tete de types + valeurs).
bool DecodeRecord(const Db& db, const std::vector<unsigned char>& rec, std::vector<Val>& vals) {
    vals.clear();
    if (rec.empty()) return false;
    const unsigned char* p = &rec[0];
    const unsigned char* end = p + rec.size();
    uint64_t hdrLen = 0;
    const unsigned char* h = p;
    if (!Varint(h, end, hdrLen) || hdrLen > rec.size()) return false;
    const unsigned char* hEnd = p + hdrLen;
    const unsigned char* body = hEnd;
    while (h < hEnd) {
        uint64_t t = 0;
        if (!Varint(h, hEnd, t)) return false;
        Val v; v.kind = 0; v.i = 0;
        size_t n = 0;
        if (t == 0) { v.kind = 0; }
        else if (t >= 1 && t <= 6) {
            static const size_t kSz[7] = { 0, 1, 2, 3, 4, 6, 8 };
            n = kSz[t];
            if (body + n > end) return false;
            int64_t x = (body[0] & 0x80) ? -1 : 0;           // signe
            for (size_t k = 0; k < n; k++) x = (int64_t)(((uint64_t)x << 8) | body[k]);
            v.kind = 1; v.i = x;
        } else if (t == 7) { n = 8; v.kind = 2; }
        else if (t == 8 || t == 9) { v.kind = 1; v.i = (t == 9) ? 1 : 0; }
        else if (t >= 12) {
            n = (size_t)((t - (t & 1 ? 13 : 12)) / 2);
            if (body + n > end) return false;
            if (t & 1) {
                v.kind = 3;
                if (db.enc == 1) v.s.assign((const char*)body, n);
                else v.s = Utf16ToUtf8(body, n, db.enc == 3);
            } else {
                v.kind = 4;
            }
        } else {
            return false;                                     // types 10/11 : reserves
        }
        body += n;
        vals.push_back(v);
    }
    return true;
}

// Parcours complet d'une table (b-arbre), appelle cb pour chaque ligne.
template <class F>
void WalkTable(const Db& db, uint32_t pgno, F& cb, int depth) {
    if (depth > 40) return;
    const unsigned char* pg = PageAt(db, pgno);
    if (!pg) return;
    const unsigned char* pageEnd = pg + db.pageSize;
    const unsigned char* hdr = pg + (pgno == 1 ? 100 : 0);
    const unsigned type = hdr[0];
    const uint32_t nCells = Be16(hdr + 3);
    if (type == 0x0D) {                                       // feuille de table
        const unsigned char* ptrs = hdr + 8;
        for (uint32_t k = 0; k < nCells; k++) {
            if (ptrs + 2 * k + 2 > pageEnd) return;
            const uint32_t off = Be16(ptrs + 2 * k);
            if (off >= db.pageSize) continue;
            std::vector<unsigned char> payload;
            int64_t rowid = 0;
            if (LeafPayload(db, pg + off, pageEnd, payload, rowid)) cb(payload, rowid);
        }
    } else if (type == 0x05) {                                // noeud interieur
        const unsigned char* ptrs = hdr + 12;
        for (uint32_t k = 0; k < nCells; k++) {
            if (ptrs + 2 * k + 2 > pageEnd) return;
            const uint32_t off = Be16(ptrs + 2 * k);
            if (off + 4 > db.pageSize) continue;
            WalkTable(db, Be32(pg + off), cb, depth + 1);
        }
        WalkTable(db, Be32(hdr + 8), cb, depth + 1);
    }
}

// --- schema ------------------------------------------------------------------
struct Table { std::string name; uint32_t root; std::string sql; };

struct SchemaCb {
    const Db* db;
    std::vector<Table>* tables;
    void operator()(const std::vector<unsigned char>& payload, int64_t) {
        std::vector<Val> v;
        if (!DecodeRecord(*db, payload, v) || v.size() < 5) return;
        if (v[0].kind != 3 || v[0].s != "table" || v[3].kind != 1 || v[4].kind != 3) return;
        Table t; t.name = v[1].s; t.root = (uint32_t)v[3].i; t.sql = v[4].s;
        tables->push_back(t);
    }
};

std::string Lower(const std::string& s) {
    std::string r(s);
    for (size_t i = 0; i < r.size(); i++) r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}

// Noms des colonnes d'un CREATE TABLE, dans l'ordre. rowidAlias = index de la
// colonne "INTEGER PRIMARY KEY" (stockee NULL, valeur = rowid), ou -1.
std::vector<std::string> Columns(const std::string& sql, int& rowidAlias) {
    std::vector<std::string> cols;
    rowidAlias = -1;
    const size_t a = sql.find('(');
    if (a == std::string::npos) return cols;
    int depth = 0;
    std::string cur;
    std::vector<std::string> defs;
    for (size_t i = a + 1; i < sql.size(); i++) {
        const char ch = sql[i];
        if (ch == '(') depth++;
        if (ch == ')') { if (depth == 0) { defs.push_back(cur); break; } depth--; }
        if (ch == ',' && depth == 0) { defs.push_back(cur); cur.clear(); continue; }
        cur += ch;
    }
    for (size_t d = 0; d < defs.size(); d++) {
        std::string s = defs[d];
        size_t b = 0;
        while (b < s.size() && isspace((unsigned char)s[b])) b++;
        s = s.substr(b);
        const std::string low = Lower(s);
        if (low.compare(0, 11, "primary key") == 0 || low.compare(0, 6, "unique") == 0 ||
            low.compare(0, 5, "check") == 0 || low.compare(0, 11, "foreign key") == 0 ||
            low.compare(0, 10, "constraint") == 0)
            continue;
        std::string name;
        size_t k = 0;
        if (k < s.size() && (s[k] == '"' || s[k] == '[' || s[k] == '`' || s[k] == '\'')) {
            const char close = (s[k] == '[') ? ']' : s[k];
            k++;
            while (k < s.size() && s[k] != close) name += s[k++];
        } else {
            while (k < s.size() && !isspace((unsigned char)s[k])) name += s[k++];
        }
        if (low.find("integer primary key") != std::string::npos) rowidAlias = (int)cols.size();
        cols.push_back(name);
    }
    return cols;
}

struct TitleCb {
    const Db* db;
    int colId, colName, rowidAlias;
    std::vector<AuroraTitle>* out;
    void operator()(const std::vector<unsigned char>& payload, int64_t rowid) {
        std::vector<Val> v;
        if (!DecodeRecord(*db, payload, v)) return;
        const int need = colId > colName ? colId : colName;
        if ((int)v.size() <= need) return;
        uint32_t tid = 0;
        const Val& vi = v[colId];
        if (colId == rowidAlias && vi.kind == 0) tid = (uint32_t)rowid;
        else if (vi.kind == 1) tid = (uint32_t)vi.i;
        else if (vi.kind == 3) tid = (uint32_t)strtoul(vi.s.c_str(), 0, 16);   // "4D5307E6" ou "0x..."
        if (tid == 0 || v[colName].kind != 3 || v[colName].s.empty()) return;
        for (size_t i = 0; i < out->size(); i++)
            if ((*out)[i].titleId == tid) return;              // plusieurs disques : un seul
        AuroraTitle t; t.titleId = tid; t.name = v[colName].s;
        out->push_back(t);
    }
};

// ==========================================================================
// .ASSET - decodeur
// ==========================================================================
inline unsigned char Clamp255(int v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }

void Rgb565(unsigned c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

// Un bloc BC3 (DXT5) de 16 octets -> 4x4 pixels RGBA, ecrits dans dst (pas = stride octets).
void DecodeBc3Block(const unsigned char* s, unsigned char* dst, size_t stride) {
    int a[8];
    a[0] = s[0]; a[1] = s[1];
    if (a[0] > a[1]) { for (int k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a[0] + k * a[1]) / 7; }
    else { for (int k = 1; k < 5; k++) a[k + 1] = ((5 - k) * a[0] + k * a[1]) / 5; a[6] = 0; a[7] = 255; }
    uint64_t abits = 0;
    for (int k = 0; k < 6; k++) abits |= (uint64_t)s[2 + k] << (8 * k);
    const unsigned c0 = s[8] | (s[9] << 8), c1 = s[10] | (s[11] << 8);
    int col[4][3];
    Rgb565(c0, col[0][0], col[0][1], col[0][2]);
    Rgb565(c1, col[1][0], col[1][1], col[1][2]);
    for (int k = 0; k < 3; k++) {                             // BC3 : toujours 4 couleurs
        col[2][k] = (2 * col[0][k] + col[1][k]) / 3;
        col[3][k] = (col[0][k] + 2 * col[1][k]) / 3;
    }
    const uint32_t idx = s[12] | (s[13] << 8) | (s[14] << 16) | ((uint32_t)s[15] << 24);
    for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++) {
            const int i = py * 4 + px;
            unsigned char* d = dst + py * stride + px * 4;
            const int ci = (idx >> (2 * i)) & 3;
            d[0] = (unsigned char)col[ci][0];
            d[1] = (unsigned char)col[ci][1];
            d[2] = (unsigned char)col[ci][2];
            d[3] = (unsigned char)a[(abits >> (3 * i)) & 7];
        }
}

} // namespace

bool ReadAuroraTitles(const unsigned char* data, size_t len, std::vector<AuroraTitle>& out) {
    out.clear();
    if (!data || len < 512 || memcmp(data, "SQLite format 3", 16) != 0) return false;
    Db db;
    db.p = data; db.len = len;
    db.pageSize = Be16(data + 16);
    if (db.pageSize == 1) db.pageSize = 65536;
    if (db.pageSize < 512 || (db.pageSize & (db.pageSize - 1))) return false;
    db.usable = db.pageSize - data[20];
    db.enc = (int)Be32(data + 56);
    if (db.enc < 1 || db.enc > 3) db.enc = 1;

    std::vector<Table> tables;
    SchemaCb sc; sc.db = &db; sc.tables = &tables;
    WalkTable(db, 1, sc, 0);

    // Table qui a a la fois TitleId et TitleName ; on prefere "ContentItems".
    int best = -1, bestId = -1, bestName = -1, bestAlias = -1, bestScore = -1;
    for (size_t t = 0; t < tables.size(); t++) {
        int alias = -1;
        std::vector<std::string> cols = Columns(tables[t].sql, alias);
        int ci = -1, cn = -1;
        for (size_t c = 0; c < cols.size(); c++) {
            const std::string l = Lower(cols[c]);
            if (l == "titleid") ci = (int)c;
            if (l == "titlename") cn = (int)c;
        }
        if (ci < 0 || cn < 0) continue;
        const int score = (Lower(tables[t].name).find("content") != std::string::npos) ? 2 : 1;
        if (score > bestScore) { best = (int)t; bestId = ci; bestName = cn; bestAlias = alias; bestScore = score; }
    }
    if (best < 0) return false;
    TitleCb tc; tc.db = &db; tc.colId = bestId; tc.colName = bestName; tc.rowidAlias = bestAlias; tc.out = &out;
    WalkTable(db, tables[best].root, tc, 0);
    return true;
}

bool DecodeAuroraAsset(const unsigned char* data, size_t len, int assetType,
                       int maxW, int maxH,
                       std::vector<unsigned char>& rgba, int& outW, int& outH,
                       bool frontOnly) {
    rgba.clear(); outW = outH = 0;
    if (!data || len < 0x800 || assetType < 0 || assetType > 24) return false;
    if (Be32(data) != 0x52584541) return false;                  // "RXEA"
    const uint32_t flags = Be32(data + 0xC);
    if (!(flags & (1u << assetType))) return false;
    const unsigned char* e = data + 0x14 + assetType * 0x40;
    const uint32_t index = Be32(e), length = Be32(e + 4);
    const unsigned char* fc = e + 0xC + 0x1C;                    // constantes de fetch GPU
    const uint32_t c0 = Be32(fc), c1 = Be32(fc + 4), c2 = Be32(fc + 8), c3 = Be32(fc + 12), c5 = Be32(fc + 20);

    if (c0 >> 31) return false;                                  // texture tuilee : non geree
    const uint32_t pitch = (c0 & 0x7FC00000) >> 22;
    const uint32_t fmt = c1 & 0x3F, endian = (c1 >> 6) & 3;
    const bool stacked = ((c1 >> 10) & 1) != 0;
    const uint32_t dim = (c5 >> 9) & 3;
    if (stacked || dim != 1 || pitch == 0) return false;
    const int w = (int)(c2 & 0x1FFF) + 1, h = (int)((c2 & 0x03FFE000) >> 13) + 1;
    const int bpp = (fmt == 6) ? 4 : (fmt == 20 ? 1 : 0);
    if (!bpp) return false;
    const int pw = (int)(32 * pitch);
    const int ph = (int)(length / (uint32_t)(pw * bpp));
    if (w > pw || h > ph || ph <= 0) return false;
    if ((uint64_t)0x800 + index + length > len) return false;

    std::vector<unsigned char> buf(data + 0x800 + index, data + 0x800 + index + length);
    // ordre des octets propre au GPU Xbox 360
    if (endian == 1) for (size_t k = 0; k + 1 < buf.size(); k += 2) { unsigned char t = buf[k]; buf[k] = buf[k + 1]; buf[k + 1] = t; }
    else if (endian == 2) for (size_t k = 0; k + 3 < buf.size(); k += 4) { unsigned char t = buf[k]; buf[k] = buf[k + 3]; buf[k + 3] = t; t = buf[k + 1]; buf[k + 1] = buf[k + 2]; buf[k + 2] = t; }
    else if (endian == 3) for (size_t k = 0; k + 3 < buf.size(); k += 4) { unsigned char t0 = buf[k], t1 = buf[k + 1]; buf[k] = buf[k + 2]; buf[k + 1] = buf[k + 3]; buf[k + 2] = t0; buf[k + 3] = t1; }

    std::vector<unsigned char> full((size_t)pw * ph * 4);
    if (fmt == 6) {
        memcpy(&full[0], &buf[0], full.size() < buf.size() ? full.size() : buf.size());
    } else {
        const int bw = pw / 4, bh = ph / 4;
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                const size_t so = ((size_t)by * bw + bx) * 16;
                if (so + 16 > buf.size()) break;
                DecodeBc3Block(&buf[so], &full[((size_t)by * 4 * pw + bx * 4) * 4], (size_t)pw * 4);
            }
    }
    // swizzle des canaux (4 = 0, 5 = 1 selon la convention GPU)
    const int sw[4] = { (int)((c3 >> 1) & 7), (int)((c3 >> 4) & 7), (int)((c3 >> 7) & 7), (int)((c3 >> 10) & 7) };
    if (!(sw[0] == 0 && sw[1] == 1 && sw[2] == 2 && sw[3] == 3)) {
        for (size_t k = 0; k < full.size(); k += 4) {
            unsigned char in[4] = { full[k], full[k + 1], full[k + 2], full[k + 3] };
            for (int ch = 0; ch < 4; ch++)
                full[k + ch] = sw[ch] < 4 ? in[sw[ch]] : (sw[ch] == 4 ? 0 : 255);
        }
    }

    // recto seul : jaquette a plat (dos | tranche | face) -> partie droite
    int x0 = 0;
    int w0 = w;
    if (frontOnly && w * 10 > h * 12) {
        x0 = (int)(w * 0.527f);
        w0 = w - x0;
    }
    // reduction a l'echelle (moyenne des pixels couverts), proportions gardees
    float k = 1.0f;
    if (maxW > 0 && w0 > maxW) k = (float)maxW / w0;
    if (maxH > 0 && h * k > maxH) k = (float)maxH / h;
    outW = (int)(w0 * k + 0.5f); if (outW < 1) outW = 1;
    outH = (int)(h * k + 0.5f); if (outH < 1) outH = 1;
    rgba.resize((size_t)outW * outH * 4);
    for (int y = 0; y < outH; y++) {
        const int sy0 = y * h / outH, sy1 = ((y + 1) * h / outH) > sy0 ? (y + 1) * h / outH : sy0 + 1;
        for (int x = 0; x < outW; x++) {
            const int sx0 = x * w0 / outW, sx1 = ((x + 1) * w0 / outW) > sx0 ? (x + 1) * w0 / outW : sx0 + 1;
            unsigned acc[4] = { 0, 0, 0, 0 }, n = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++) {
                    const unsigned char* p = &full[((size_t)sy * pw + x0 + sx) * 4];
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3]; n++;
                }
            unsigned char* d = &rgba[((size_t)y * outW + x) * 4];
            for (int c = 0; c < 4; c++) d[c] = Clamp255((int)(acc[c] / n));
        }
    }
    return true;
}

bool DashboardDirFromLaunchIni(const std::string& ini, std::string& dirOut) {
    dirOut.clear();
    bool inPaths = false;
    size_t pos = 0;
    while (pos <= ini.size()) {
        size_t eol = ini.find('\n', pos);
        if (eol == std::string::npos) eol = ini.size();
        std::string line = ini.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t sc = line.find(';');                  // commentaire
        if (sc != std::string::npos) line = line.substr(0, sc);
        size_t a = 0, b = line.size();
        while (a < b && isspace((unsigned char)line[a])) a++;
        while (b > a && isspace((unsigned char)line[b - 1])) b--;
        line = line.substr(a, b - a);
        if (line.empty()) continue;
        if (line[0] == '[') { inPaths = (Lower(line) == "[paths]"); continue; }
        if (!inPaths) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        while (!key.empty() && isspace((unsigned char)key[key.size() - 1])) key.erase(key.size() - 1);
        size_t v0 = 0;
        while (v0 < val.size() && isspace((unsigned char)val[v0])) v0++;
        val = val.substr(v0);
        if (Lower(key) != "default" || val.empty()) continue;
        const size_t slash = val.find_last_of("\\/");
        if (slash == std::string::npos) return false;
        std::string dir = val.substr(0, slash);
        if (Lower(dir.substr(0, 4)) == "hdd:") dir = "HDD:" + dir.substr(4);
        dirOut = dir;
        return true;
    }
    return false;
}
