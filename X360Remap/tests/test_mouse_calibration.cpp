// Harnais de test du moteur de calibration souris (mouse_calibration.cpp).
// Compile et execute pour de vrai en g++ : les paquets sont synthetiques mais
// reproduisent des dispositions REELLES - la souris boot classique 4 octets,
// et une souris a paquet long avec identifiant de rapport et axes 16 bits,
// qui est exactement le cas de la souris VID:046a PID:b092 de l'utilisateur
// (pkt:8, curseur casse avant le forcage boot protocol).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../mouse_calibration.h"
#include "../mapping.h"
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static CalibrationSample MakeSample(const uint8_t* bytes, uint8_t len) {
    CalibrationSample s;
    std::memset(&s, 0, sizeof(s));
    s.length = len;
    std::memcpy(s.bytes, bytes, len);
    return s;
}

// --- Disposition A : souris boot classique, 4 octets -----------------------
// [0]=boutons  [1]=X (int8)  [2]=Y (int8)  [3]=molette (int8)
static void TestClassicBootLayout() {
    std::printf("TestClassicBootLayout\n");

    std::vector<CalibrationSample> rest;
    for (int i = 0; i < 5; i++) {
        uint8_t b[4] = {0x00, 0x00, 0x00, 0x00};
        rest.push_back(MakeSample(b, 4));
    }
    CalibrationBaseline base = BuildBaseline(rest);
    CHECK(base.length == 4);

    std::vector<CalibrationSample> right, left;
    for (int i = 0; i < 4; i++) {
        uint8_t r[4] = {0x00, 0x08, 0x00, 0x00}; right.push_back(MakeSample(r, 4));
        uint8_t l[4] = {0x00, 0xF8, 0x00, 0x00}; left.push_back(MakeSample(l, 4)); // -8
    }
    DetectedField x = DetectField(base, right, left, CALIB_AXIS_X, nullptr);
    CHECK(x.found); CHECK(x.offset == 1); CHECK(x.size == 1);

    std::vector<CalibrationSample> down, up;
    for (int i = 0; i < 4; i++) {
        uint8_t d[4] = {0x00, 0x00, 0x06, 0x00}; down.push_back(MakeSample(d, 4));
        uint8_t u[4] = {0x00, 0x00, 0xFA, 0x00}; up.push_back(MakeSample(u, 4)); // -6
    }
    DetectedField y = DetectField(base, down, up, CALIB_AXIS_Y, nullptr);
    CHECK(y.found); CHECK(y.offset == 2); CHECK(y.size == 1);

    std::vector<CalibrationSample> fwd, back;
    for (int i = 0; i < 3; i++) {
        uint8_t f[4] = {0x00, 0x00, 0x00, 0x01}; fwd.push_back(MakeSample(f, 4));
        uint8_t b[4] = {0x00, 0x00, 0x00, 0xFF}; back.push_back(MakeSample(b, 4));
    }
    DetectedField w = DetectField(base, fwd, back, CALIB_WHEEL, nullptr);
    CHECK(w.found); CHECK(w.offset == 3); CHECK(w.size == 1);

    std::vector<CalibrationSample> click;
    for (int i = 0; i < 4; i++) {
        uint8_t c[4] = {0x01, 0x00, 0x00, 0x00}; click.push_back(MakeSample(c, 4));
    }
    std::vector<CalibrationSample> none;
    DetectedField btn = DetectField(base, click, none, CALIB_BUTTON, nullptr);
    CHECK(btn.found); CHECK(btn.offset == 0); CHECK(btn.bitIndex == 0);

    HidReportLayout layout = BuildLayout(4, x, y, w, btn);
    CHECK(layout.valid);
    CHECK(layout.xOffset == 1 && layout.yOffset == 2 && layout.wheelOffset == 3);

    std::printf("  disposition boot 4 octets retrouvee entierement\n");
}

// --- Disposition B : paquet 8 octets, report ID, axes 16 bits -------------
// [0]=reportId constant  [1]=boutons  [2..3]=X int16  [4..5]=Y int16
// [6]=molette int8  [7]=remplissage
// C'est le profil type de la 2e souris de l'utilisateur (pkt:8), dont le
// curseur "n'allait que de haut en bas" quand on lisait le paquet comme une
// structure boot figee : l'octet 2 (poids faible de X) etait interprete comme
// les boutons, et ainsi de suite.
static void TestLongReportWithIdAnd16BitAxes() {
    std::printf("TestLongReportWithIdAnd16BitAxes\n");

    std::vector<CalibrationSample> rest;
    for (int i = 0; i < 5; i++) {
        uint8_t b[8] = {0x02, 0x00, 0,0, 0,0, 0x00, 0x00};
        rest.push_back(MakeSample(b, 8));
    }
    CalibrationBaseline base = BuildBaseline(rest);
    CHECK(base.length == 8);
    CHECK(base.stable[0]); // le report ID est constant, donc "stable" - mais il ne variera jamais

    // X = +300 puis -300, sur 16 bits petit-boutiste.
    std::vector<CalibrationSample> right, left;
    for (int i = 0; i < 4; i++) {
        uint8_t r[8] = {0x02, 0x00, 0x2C, 0x01, 0,0, 0x00, 0x00}; // +300
        uint8_t l[8] = {0x02, 0x00, 0xD4, 0xFE, 0,0, 0x00, 0x00}; // -300
        right.push_back(MakeSample(r, 8));
        left.push_back(MakeSample(l, 8));
    }
    DetectedField x = DetectField(base, right, left, CALIB_AXIS_X, nullptr);
    CHECK(x.found); CHECK(x.offset == 2); CHECK(x.size == 2);

    std::vector<CalibrationSample> down, up;
    for (int i = 0; i < 4; i++) {
        uint8_t d[8] = {0x02, 0x00, 0,0, 0x90, 0x01, 0x00, 0x00}; // +400
        uint8_t u[8] = {0x02, 0x00, 0,0, 0x70, 0xFE, 0x00, 0x00}; // -400
        down.push_back(MakeSample(d, 8));
        up.push_back(MakeSample(u, 8));
    }
    DetectedField y = DetectField(base, down, up, CALIB_AXIS_Y, nullptr);
    CHECK(y.found); CHECK(y.offset == 4); CHECK(y.size == 2);

    std::vector<CalibrationSample> fwd, back;
    for (int i = 0; i < 3; i++) {
        uint8_t f[8] = {0x02, 0x00, 0,0, 0,0, 0x01, 0x00};
        uint8_t b[8] = {0x02, 0x00, 0,0, 0,0, 0xFF, 0x00};
        fwd.push_back(MakeSample(f, 8));
        back.push_back(MakeSample(b, 8));
    }
    DetectedField w = DetectField(base, fwd, back, CALIB_WHEEL, nullptr);
    CHECK(w.found); CHECK(w.offset == 6); CHECK(w.size == 1);

    std::vector<CalibrationSample> rightClick;
    for (int i = 0; i < 4; i++) {
        uint8_t c[8] = {0x02, 0x02, 0,0, 0,0, 0x00, 0x00}; // bit 1 de l'octet 1
        rightClick.push_back(MakeSample(c, 8));
    }
    std::vector<CalibrationSample> none;
    DetectedField btn = DetectField(base, rightClick, none, CALIB_BUTTON, nullptr);
    CHECK(btn.found); CHECK(btn.offset == 1); CHECK(btn.bitIndex == 1);

    HidReportLayout layout = BuildLayout(8, x, y, w, btn);
    CHECK(layout.valid);
    CHECK(layout.xSize == 2 && layout.ySize == 2);

    // La lecture du plugin doit retrouver les valeurs d'origine.
    uint8_t probe[8] = {0x02, 0x00, 0x2C, 0x01, 0x70, 0xFE, 0xFF, 0x00};
    CHECK(ReadSignedField(probe, 8, layout.xOffset, layout.xSize) == 300);
    CHECK(ReadSignedField(probe, 8, layout.yOffset, layout.ySize) == -400);
    CHECK(ReadSignedField(probe, 8, layout.wheelOffset, layout.wheelSize) == -1);

    std::printf("  report ID ignore, axes 16 bits signes et molette retrouves\n");
}

// Le bruit ne disqualifie un octet QUE s'il est du meme ordre que le geste.
// Version initiale trop brutale (rejet des qu'un octet frémissait une fois au
// repos) : sur du vrai materiel, X et Y frémissent toujours un peu, ils
// etaient donc exclus d'office et plus aucune disposition n'etait detectee -
// echec constate par l'utilisateur sur console le 2026-08-03.
static void TestNoiseRejectedOnlyWhenComparableToSignal() {
    std::printf("TestNoiseRejectedOnlyWhenComparableToSignal\n");

    // Octet 3 tres bruite au repos : amplitude ~64.
    std::vector<CalibrationSample> rest;
    uint8_t a[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t b[4] = {0x00, 0x00, 0x00, 0x40};
    rest.push_back(MakeSample(a, 4));
    rest.push_back(MakeSample(b, 4));
    CalibrationBaseline base = BuildBaseline(rest);
    CHECK(!base.stable[3]);
    CHECK(base.jitter[3] >= 60);

    // Geste FAIBLE devant ce bruit : doit etre rejete.
    std::vector<CalibrationSample> fwd, back;
    uint8_t f[4] = {0x00, 0x00, 0x00, 0x10};
    uint8_t k[4] = {0x00, 0x00, 0x00, 0xF0};
    fwd.push_back(MakeSample(f, 4));
    back.push_back(MakeSample(k, 4));
    CHECK(!DetectField(base, fwd, back, CALIB_WHEEL, nullptr).found);

    // Un octet PROPRE (bruit 0) doit accepter un geste d'amplitude 1 - c'est
    // exactement le cas d'un cran de molette, qui vaut +/-1. Une marge de
    // securite supplementaire le rejetterait, defaut trouve par les tests.
    std::vector<CalibrationSample> rest2;
    uint8_t z[4] = {0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 4; i++) rest2.push_back(MakeSample(z, 4));
    CalibrationBaseline clean = BuildBaseline(rest2);
    CHECK(clean.jitter[3] == 0);

    std::vector<CalibrationSample> w1, w2;
    uint8_t p[4] = {0x00, 0x00, 0x00, 0x01};
    uint8_t m[4] = {0x00, 0x00, 0x00, 0xFF};
    w1.push_back(MakeSample(p, 4));
    w2.push_back(MakeSample(m, 4));
    DetectedField w = DetectField(clean, w1, w2, CALIB_WHEEL, nullptr);
    CHECK(w.found); CHECK(w.offset == 3);

    std::printf("  bruit fort = rejet ; octet propre = un cran de molette a +/-1 suffit\n");
}

// L'exclusion des octets deja attribues, sur le moteur seul.
static void TestExclusionPreventsStealingBytes() {
    std::printf("TestExclusionPreventsStealingBytes\n");

    std::vector<CalibrationSample> rest;
    uint8_t z[4] = {0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 4; i++) rest.push_back(MakeSample(z, 4));
    CalibrationBaseline base = BuildBaseline(rest);

    // Pendant l'etape molette, l'octet X (1) bouge BEAUCOUP plus que l'octet
    // molette (3) - la main derive en tournant. Sans exclusion, X gagne.
    std::vector<CalibrationSample> fwd, back;
    uint8_t f[4] = {0x00, 0x20, 0x00, 0x01};
    uint8_t k[4] = {0x00, 0xE0, 0x00, 0xFF};
    fwd.push_back(MakeSample(f, 4));
    back.push_back(MakeSample(k, 4));

    DetectedField naive = DetectField(base, fwd, back, CALIB_WHEEL, nullptr);
    CHECK(naive.found); CHECK(naive.offset == 1); // le piege, sans exclusion

    bool exclude[16];
    for (int i = 0; i < 16; i++) exclude[i] = false;
    DetectedField xField; xField.found = true; xField.offset = 1; xField.size = 1; xField.bitIndex = 0;
    MarkFieldExcluded(xField, exclude);
    CHECK(exclude[1]);

    DetectedField fixed = DetectField(base, fwd, back, CALIB_WHEEL, exclude);
    CHECK(fixed.found); CHECK(fixed.offset == 3); // la vraie molette

    std::printf("  sans exclusion la molette est volee par l'axe ; avec exclusion elle est correcte\n");
}

// Une souris sans molette doit produire une disposition VALIDE, simplement
// sans molette - pas un echec de calibration.
static void TestMissingWheelStillUsable() {
    std::printf("TestMissingWheelStillUsable\n");

    DetectedField x; x.found = true; x.offset = 1; x.size = 1; x.bitIndex = 0;
    DetectedField y; y.found = true; y.offset = 2; y.size = 1; y.bitIndex = 0;
    DetectedField w; w.found = false; w.offset = REPORT_FIELD_ABSENT; w.size = 0; w.bitIndex = 0;
    DetectedField b; b.found = true; b.offset = 0; b.size = 1; b.bitIndex = 0;

    HidReportLayout layout = BuildLayout(4, x, y, w, b);
    CHECK(layout.valid);
    CHECK(layout.wheelOffset == REPORT_FIELD_ABSENT);
    CHECK(ReadSignedField(nullptr, 0, layout.wheelOffset, layout.wheelSize) == 0);

    // Sans X ni Y en revanche, rien n'est exploitable : on retombe sur le
    // comportement historique plutot que d'ecrire une disposition bancale.
    DetectedField none; none.found = false; none.offset = REPORT_FIELD_ABSENT; none.size = 0; none.bitIndex = 0;
    HidReportLayout bad = BuildLayout(4, none, y, w, b);
    CHECK(!bad.valid);

    // Offsets hors du paquet : refuses, sinon le plugin lirait hors limites.
    HidReportLayout oob = layout;
    oob.xOffset = 9;
    CHECK(!IsLayoutUsable(oob));

    std::printf("  molette absente = disposition valide ; X/Y absents ou hors limites = refus\n");
}

// La disposition doit survivre a un aller-retour par X360Remap.json, et une
// disposition incoherente ecrite a la main doit etre refusee a la relecture -
// sinon le plugin lirait des octets hors du paquet.
static void TestLayoutJsonRoundTrip() {
    std::printf("TestLayoutJsonRoundTrip\n");

    ClearDynamicMappings();
    HidDeviceMapping m;
    std::memset(&m, 0, sizeof(m));
    m.vendorId = 0x046A;
    m.productId = 0xB092;
    m.reportLayout.valid = true;
    m.reportLayout.reportSize = 8;
    m.reportLayout.buttonsOffset = 1;
    m.reportLayout.xOffset = 2; m.reportLayout.xSize = 2;
    m.reportLayout.yOffset = 4; m.reportLayout.ySize = 2;
    m.reportLayout.wheelOffset = 6; m.reportLayout.wheelSize = 1;
    CHECK(IsLayoutUsable(m.reportLayout));
    g_dynamicMappings.push_back(m);

    std::string json = SaveMappingsToJson();
    CHECK(json.find("reportLayout") != std::string::npos);

    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson(json));
    HidDeviceMapping* back = FindMapping(0x046A, 0xB092);
    CHECK(back != nullptr);
    if (back) {
        CHECK(back->reportLayout.valid);
        CHECK(back->reportLayout.reportSize == 8);
        CHECK(back->reportLayout.xOffset == 2 && back->reportLayout.xSize == 2);
        CHECK(back->reportLayout.yOffset == 4 && back->reportLayout.ySize == 2);
        CHECK(back->reportLayout.wheelOffset == 6);
        CHECK(back->reportLayout.buttonsOffset == 1);
    }

    // Offsets hors du paquet, comme apres une edition manuelle malheureuse.
    ClearDynamicMappings();
    const char* bad =
        "[{\"vid\":1,\"pid\":2,\"reportLayout\":{\"size\":4,\"xOffset\":9,\"xSize\":2,"
        "\"yOffset\":2,\"ySize\":1,\"wheelOffset\":255,\"wheelSize\":0,\"buttonsOffset\":0}}]";
    CHECK(LoadMappingsFromJson(bad));
    HidDeviceMapping* badMap = FindMapping(1, 2);
    CHECK(badMap != nullptr);
    if (badMap)
        CHECK(!badMap->reportLayout.valid); // refusee, on retombe sur le comportement historique

    // Un device sans calibration ne doit pas se voir inventer de disposition.
    ClearDynamicMappings();
    CHECK(LoadMappingsFromJson("[{\"vid\":3,\"pid\":4}]"));
    HidDeviceMapping* plain = FindMapping(3, 4);
    CHECK(plain != nullptr);
    if (plain)
        CHECK(!plain->reportLayout.valid);

    ClearDynamicMappings();
    std::printf("  disposition round-trippe ; offsets incoherents et absence de calibration refuses\n");
}

int main() {
    TestLayoutJsonRoundTrip();
    TestClassicBootLayout();
    TestLongReportWithIdAnd16BitAxes();
    TestNoiseRejectedOnlyWhenComparableToSignal();
    TestExclusionPreventsStealingBytes();
    TestMissingWheelStillUsable();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d FAILURE(S)\n", g_failures);
    return 1;
}
