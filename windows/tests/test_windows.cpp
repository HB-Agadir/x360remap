// Tests of the Windows-only logic, without a window and without a console.
//
//   test_windows.exe                 key table + settings round trip
//   test_windows.exe ftp <port>      + the FTP client and the safe write,
//                                    against tests/fake_aurora_ftp.py
//   test_windows.exe ftpcut <port>   server started with --cut-new-after:
//                                    the console file must stay untouched
//
// Build and run: tests\run_tests.ps1
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../src/console_link.h"
#include "../src/live_watch.h"
#include "../src/install_win.h"
#include "../src/update_win.h"
#include "../src/platform_win.h"
#include "../src/key_capture_win.h"
#include "../../X360Remap/mapping.h"
#include "../../X360Remap/hud_layout.h"
#include "../../X360Remap/camera_learn.h"
#include "../../application/config_writer.h"

static int g_failed = 0, g_passed = 0;
#define CHECK(cond) do { if (cond) g_passed++; else { g_failed++; printf("FAILED line %d: %s\n", __LINE__, #cond); } } while (0)

// ---------------------------------------------------------------------------
static void TestKeyTable() {
    // Physical positions: the same scan code gives the same usage whatever the layout.
    CHECK(HidUsageFromScan(0x10, false, 'Q') == 0x14);     // key right of Tab (Q on QWERTY, A on AZERTY)
    CHECK(HidUsageFromScan(0x11, false, 'W') == 0x1A);
    CHECK(HidUsageFromScan(0x1E, false, 'A') == 0x04);
    CHECK(HidUsageFromScan(0x39, false, VK_SPACE) == 0x2C);
    CHECK(HidUsageFromScan(0x01, false, VK_ESCAPE) == 0x29);
    CHECK(HidUsageFromScan(0x02, false, '1') == 0x1E);
    CHECK(HidUsageFromScan(0x0B, false, '0') == 0x27);
    CHECK(HidUsageFromScan(0x3B, false, VK_F1) == 0x3A);
    CHECK(HidUsageFromScan(0x44, false, VK_F10) == 0x43);
    CHECK(HidUsageFromScan(0x57, false, VK_F11) == 0x44);
    CHECK(HidUsageFromScan(0x58, false, VK_F12) == 0x45);
    // Left / right modifiers
    CHECK(HidUsageFromScan(0x1D, false, VK_CONTROL) == 0xE0);
    CHECK(HidUsageFromScan(0x1D, true, VK_CONTROL) == 0xE4);
    CHECK(HidUsageFromScan(0x2A, false, VK_SHIFT) == 0xE1);
    CHECK(HidUsageFromScan(0x36, false, VK_SHIFT) == 0xE5);
    CHECK(HidUsageFromScan(0x38, false, VK_MENU) == 0xE2);
    CHECK(HidUsageFromScan(0x38, true, VK_MENU) == 0xE6);
    CHECK(HidUsageFromScan(0x5B, true, VK_LWIN) == 0xE3);
    // Numpad vs navigation block: same scan codes, the extended flag decides
    CHECK(HidUsageFromScan(0x47, false, VK_NUMPAD7) == 0x5F);
    CHECK(HidUsageFromScan(0x47, true, VK_HOME) == 0x4A);
    CHECK(HidUsageFromScan(0x48, true, VK_UP) == 0x52);
    CHECK(HidUsageFromScan(0x50, true, VK_DOWN) == 0x51);
    CHECK(HidUsageFromScan(0x4B, true, VK_LEFT) == 0x50);
    CHECK(HidUsageFromScan(0x4D, true, VK_RIGHT) == 0x4F);
    CHECK(HidUsageFromScan(0x1C, false, VK_RETURN) == 0x28);
    CHECK(HidUsageFromScan(0x1C, true, VK_RETURN) == 0x58);
    CHECK(HidUsageFromScan(0x35, true, VK_DIVIDE) == 0x54);
    CHECK(HidUsageFromScan(0x52, false, VK_NUMPAD0) == 0x62);
    CHECK(HidUsageFromScan(0x4F, false, VK_NUMPAD1) == 0x59);
    // Pause and Num Lock share a scan code
    CHECK(HidUsageFromScan(0x45, false, VK_PAUSE) == 0x48);
    CHECK(HidUsageFromScan(0x45, true, VK_NUMLOCK) == 0x53);
    CHECK(HidUsageFromScan(0x56, false, VK_OEM_102) == 0x64);

    // Every key of the plugin's default layout can be produced from the PC.
    HidKeyMapEntry defs[64];
    const uint8_t n = GetDefaultKeyMap(defs, 64);
    CHECK(n > 20);
    for (uint8_t i = 0; i < n; i++) {
        bool reachable = false;
        for (unsigned scan = 1; scan < 0x60 && !reachable; scan++)
            reachable = HidUsageFromScan(scan, false, 0) == defs[i].code || HidUsageFromScan(scan, true, 0) == defs[i].code;
        if (!reachable) printf("  default key 0x%02X not reachable\n", defs[i].code);
        CHECK(reachable);
    }

    // Labels: the abbreviations the shared page has icons for
    char buf[24];
    CHECK(strcmp(HidKeyLabel(0x2C, buf, sizeof(buf)), "SPACE") == 0);
    CHECK(strcmp(HidKeyLabel(0xE0, buf, sizeof(buf)), "L CTRL") == 0);
    CHECK(strcmp(HidKeyLabel(0x1E, buf, sizeof(buf)), "1") == 0);
    CHECK(strcmp(HidKeyLabel(0x45, buf, sizeof(buf)), "F12") == 0);
    CHECK(strcmp(HidKeyLabel(0x5D, buf, sizeof(buf)), "NUM 5") == 0);
    CHECK(HidKeyLabel(0x04, buf, sizeof(buf))[0] != '\0');
    // Tartarus factory layout (OpenTartarus)
    CHECK(strcmp(KeypadKeyLabel(0x1F), "02") == 0);
    CHECK(strcmp(KeypadKeyLabel(0x2C), "20") == 0);
    CHECK(strcmp(KeypadKeyLabel(0xE2), "THUMB") == 0);
    CHECK(KeypadKeyLabel(0x3A) == nullptr);
}

// ---------------------------------------------------------------------------
static std::string TempFile(const char* name) {
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    return std::string(dir) + name;
}

static std::string ReadAll(const std::string& path) {
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// What the pages do when a setting changes, then: is the file exactly what the
// plugin's own parser and serializer produce (mapping.cpp)?
static void TestSettingsRoundTrip() {
    const std::string path = TempFile("x360remap_win_test.json");
    remove(path.c_str());

    // Acceptance 2: key 02 of the Tartarus (sends "2", usage 0x1F) bound to Y
    HidKeyMapEntry y;
    y.code = 0x1F;
    y.action = KEYACT_Y;
    CHECK(ApplyKeyboardMappingEntry(0x1532, 0x022B, y, path.c_str()));
    // Acceptance 3: mouse settings
    CHECK(ApplyMouseSettingsFull(0x1E7D, 0x2E22, 14000, true, 600, 3500, path.c_str(), 6000, 25));
    CHECK(ApplyMouseProfileFull(0x1E7D, 0x2E22, 0x41560817, 9000, false, 0, CURVE_EXPONENTIAL, 1.6f, "Modern Warfare 2",
                                3000, path.c_str(), STICKS_LEGACY, 0, 10));

    const std::string onDisk = ReadAll(path);
    CHECK(!onDisk.empty());
    CHECK(LoadMappingsFromJson(onDisk));
    // Re-read then re-written by the plugin's code: identical bytes.
    CHECK(SaveMappingsToJson() == onDisk);

    const HidDeviceMapping* kb = FindMapping(0x1532, 0x022B);
    CHECK(kb != nullptr);
    if (kb) {
        HidKeyMapEntry eff[MAX_MERGED_KEYS];
        uint8_t n = ResolveEffectiveKeyMap(kb, 0, eff, MAX_MERGED_KEYS);
        n = AddKeypadPreset(0x1532, 0x022B, eff, n, MAX_MERGED_KEYS);
        bool key02IsY = false, yElsewhere = false;
        for (uint8_t i = 0; i < n; i++) {
            if (eff[i].code == 0x1F && eff[i].action == KEYACT_Y) key02IsY = true;
            if (eff[i].code != 0x1F && eff[i].action == KEYACT_Y) yElsewhere = true;
        }
        CHECK(key02IsY);
        CHECK(!yElsewhere);   // the default key for Y (E) was freed
    }
    const HidDeviceMapping* ms = FindMapping(0x1E7D, 0x2E22);
    CHECK(ms != nullptr);
    if (ms) {
        CHECK(ms->mouseSensitivity == 14000);
        CHECK(ms->invertMouseY);
        CHECK(ms->antiDeadzone == 3500 && ms->antiDeadzoneY == 6000 && ms->smoothingMs == 25);
        CHECK(ms->profileCount == 1 && ms->profiles[0].titleId == 0x41560817);
        CHECK(ms->profiles[0].stickLayout == STICKS_LEGACY);
    }

    // Direct aim settings survive the file (ADS 25 % = four times slower)
    CHECK(LoadMappingsForUpdate(path));
    for (size_t i = 0; i < g_dynamicMappings.size(); i++)
        if (g_dynamicMappings[i].vendorId == 0x1E7D) { g_dynamicMappings[i].adsSensitivity = 25; g_dynamicMappings[i].directAimSmoothingMs = 12; }
    CHECK(SaveMappingsToFile(path));
    CHECK(LoadMappingsFromFile(path));
    const EffectiveMouseSettings eff = ResolveEffectiveMouseSettings(FindMapping(0x1E7D, 0x2E22), 0);
    CHECK(eff.adsSensitivity == 25 && eff.directAimSmoothingMs == 12);

    // A broken file is refused (never uploaded)
    CHECK(!LoadMappingsFromJson("[ { \"vid\": 1, "));
    CHECK(!LoadMappingsFromJson("{ \"vid\": 1 }"));
    remove(path.c_str());
}

static void TestRemoteGuards() {
    CHECK(IsStudioDir("/Hdd1/X360RemapStudio/"));
    CHECK(IsStudioDir("/Usb0/X360RemapStudio"));
    CHECK(!IsStudioDir("/Hdd1/"));
    CHECK(!IsStudioDir("/X360RemapStudio"));
    CHECK(!IsStudioDir("/Hdd1/X360RemapStudio/../Aurora"));
    CHECK(!IsStudioDir("/Hdd1/Aurora"));
    CHECK(IsPlainFileName("X360Remap.json"));
    CHECK(IsPlainFileName("enable_input_trace.txt"));
    CHECK(!IsPlainFileName("../launch.ini"));
    CHECK(!IsPlainFileName("sub/file.txt"));
    CHECK(!IsPlainFileName(""));
    CHECK(JoinRemote("/Hdd1/X360RemapStudio", "a.txt") == "/Hdd1/X360RemapStudio/a.txt");

    // The line of acceptance test 6 (MW3, Xenia Camera Finder)
    LearnedCamera cam;
    CHECK(CamParseLine("415608CB 4E77262A 00000000 82743CBC 82743CB8 -1 -1 deg Modern Warfare 3", &cam));
    CHECK(cam.titleId == 0x415608CB);
    CHECK(!CamParseLine("this is not a camera line", &cam));
}

// GitHub answer (same shape as /releases, newest first): a preview, then a
// release with a zip, then one with the two .xex attached.
static void TestReleaseParse() {
    const char* json =
        "[{\"id\":3,\"name\":\"X360Remap_1.3-preview_pre12\",\"prerelease\":true,\"draft\":false,\"published_at\":\"2026-10-18T10:15:41Z\","
        "\"html_url\":\"https://github.com/x/y/releases/tag/p\",\"assets\":[{\"name\":\"X360Remap_1.3-preview_pre12.zip\",\"browser_download_url\":\"https://a/pre12.zip\"}]},"
        "{\"id\":2,\"name\":\"\",\"tag_name\":\"v1.2\",\"prerelease\":false,\"draft\":false,\"assets\":[{\"name\":\"X360Remap_v1.2.zip\",\"browser_download_url\":\"https://a/v12.zip\"}]},"
        "{\"id\":1,\"name\":\"v0.1\",\"prerelease\":false,\"assets\":[{\"name\":\"X360Remap.xex\",\"browser_download_url\":\"https://a/p.xex\"},"
        "{\"name\":\"X360RemapStudio.xex\",\"browser_download_url\":\"https://a/s.xex\"}]}]";
    ReleaseInfo r;
    CHECK(ParseNewestRelease(json, true, r) && r.id == 3 && r.prerelease && r.zipUrl == "https://a/pre12.zip");
    CHECK(ParseNewestRelease(json, false, r) && r.id == 2 && r.name == "v1.2" && r.zipUrl == "https://a/v12.zip");
    const char* xexOnly = "[{\"id\":1,\"name\":\"v0.1\",\"prerelease\":false,\"assets\":[{\"name\":\"X360Remap.xex\",\"browser_download_url\":\"https://a/p.xex\"},"
                          "{\"name\":\"X360RemapStudio.xex\",\"browser_download_url\":\"https://a/s.xex\"}]}]";
    CHECK(ParseNewestRelease(xexOnly, false, r) && r.pluginUrl == "https://a/p.xex" && r.studioUrl == "https://a/s.xex" && r.zipUrl.empty());
    // V1.3_pre_13 on GitHub: the Windows zip is listed before the console zip
    const char* both = "[{\"id\":4,\"tag_name\":\"V1.3_pre_13\",\"prerelease\":false,\"assets\":["
                       "{\"name\":\"X360RemapStudio-1.3-pre16-win64.zip\",\"browser_download_url\":\"https://a/win.zip\"},"
                       "{\"name\":\"X360Remap_1.3-preview_pre13.zip\",\"browser_download_url\":\"https://a/pre13.zip\"}]}]";
    CHECK(ParseNewestRelease(both, false, r) && r.zipUrl == "https://a/pre13.zip");
    CHECK(!ParseNewestRelease("{\"message\":\"rate limit\"}", true, r));
}

// Install & repair: the launch.ini change touches one line, nothing else.
static void TestLaunchIniFix() {
    std::string oldL, newL, fixed;
    // 1. the line points to a file that no longer exists (tester case)
    const std::string a = "[Paths]\r\nDefault = Hdd:\\App\\Aurora\\Aurora.xex\r\n\r\n[Plugins]\r\nplugin1 = Hdd:\\Old\\X360Remap.xex ; mine\r\nplugin2 = Hdd:\\xbdm.xex\r\n\r\n[Settings]\r\nfakelive = true\r\n";
    CHECK(ProposeLaunchIniFix(a, "Hdd:\\Old\\X360Remap.xex", kPluginDashPath, oldL, newL, fixed));
    CHECK(oldL == "plugin1 = Hdd:\\Old\\X360Remap.xex ; mine");
    CHECK(newL == "plugin1 = Hdd:\\X360Remap.xex");
    CHECK(fixed == "[Paths]\r\nDefault = Hdd:\\App\\Aurora\\Aurora.xex\r\n\r\n[Plugins]\r\nplugin1 = Hdd:\\X360Remap.xex\r\nplugin2 = Hdd:\\xbdm.xex\r\n\r\n[Settings]\r\nfakelive = true\r\n");
    // 2. no line: the first empty slot is used
    const std::string b = "[Plugins]\nplugin1 = Hdd:\\xbdm.xex\nplugin2 =\nplugin3 =\n[Settings]\n";
    CHECK(ProposeLaunchIniFix(b, "", kPluginDashPath, oldL, newL, fixed));
    CHECK(fixed == "[Plugins]\nplugin1 = Hdd:\\xbdm.xex\nplugin2 = Hdd:\\X360Remap.xex\nplugin3 =\n[Settings]\n");
    // 3. no free slot written: a new pluginN line at the end of the section
    const std::string c = "[Plugins]\r\nplugin1 = Hdd:\\xbdm.xex\r\n\r\n[Settings]\r\nx = 1\r\n";
    CHECK(ProposeLaunchIniFix(c, "", kPluginDashPath, oldL, newL, fixed));
    CHECK(oldL.empty() && newL == "plugin2 = Hdd:\\X360Remap.xex");
    CHECK(fixed == "[Plugins]\r\nplugin1 = Hdd:\\xbdm.xex\r\nplugin2 = Hdd:\\X360Remap.xex\r\n\r\n[Settings]\r\nx = 1\r\n");
    // 4. already right: nothing to change
    const std::string d = "[Plugins]\r\nplugin1 = Hdd:\\X360Remap.xex\r\n";
    CHECK(!ProposeLaunchIniFix(d, "Hdd:\\X360Remap.xex", kPluginDashPath, oldL, newL, fixed));
    // 6. plugin1..plugin5 all taken by other plugins: nothing proposed (DashLaunch has 5 slots)
    const std::string full = "[Plugins]\r\nplugin1 = Hdd:\\a.xex\r\nplugin2 = Hdd:\\b.xex\r\nplugin3 = Hdd:\\c.xex\r\n"
                             "plugin4 = Hdd:\\d.xex\r\nplugin5 = Hdd:\\e.xex\r\n";
    CHECK(!ProposeLaunchIniFix(full, "", kPluginDashPath, oldL, newL, fixed));
    // 5. no [Plugins] section
    CHECK(ProposeLaunchIniFix("[Settings]\nx = 1", "", kPluginDashPath, oldL, newL, fixed));
    CHECK(fixed == "[Settings]\nx = 1\n[Plugins]\nplugin1 = Hdd:\\X360Remap.xex\n");
}

// ---------------------------------------------------------------------------
static const char* kDir = "/Hdd1/X360RemapStudio/";

static void TestFtpInstall(FtpClient& ftp) {
    std::string err, data;
    bool found = false;
    const std::string ini = "[Plugins]\r\nplugin1 = Hdd:\\Gone\\X360Remap.xex\r\nplugin2 = Hdd:\\xbdm.xex\r\n";
    CHECK(ftp.Upload("/Hdd1/launch.ini", ini, err));
    CHECK(PluginFtpPath(kDir) == "/Hdd1/X360Remap.xex");
    ftp.Delete(PluginFtpPath(kDir), err);
    // a plugin left in X360RemapStudio\ (old layout) does not count
    CHECK(SafeWriteFile(ftp, kDir, "X360Remap.xex", "old", false, std::string(), true).result == SAFE_OK);

    InstallCheck c;
    CHECK(CheckInstall(ftp, kDir, c, err));
    CHECK(c.iniFound && c.iniFtpPath == "/Hdd1/launch.ini");
    CHECK(c.diag.diag == DIAG_FILE_MISSING);
    CHECK(!c.pluginPresent && !c.fixOffered);   // plugin not at the root yet

    // install, as the app does (safe write at the root of the drive)
    const SafeWriteOutcome w = SafeWriteAt(ftp, PluginFtpPath(kDir), std::string(4096, 'P'), false, std::string(), true);
    CHECK(w.result == SAFE_OK);
    CHECK(ftp.Download("/Hdd1/X360Remap.xex", data, found, err) && found && data == std::string(4096, 'P'));
    CHECK(CheckInstall(ftp, kDir, c, err));
    CHECK(c.pluginPresent && c.fixOffered);
    CHECK(c.fixNewLine == "plugin1 = Hdd:\\X360Remap.xex");

    // changed on the console between the check and the yes: nothing written
    CHECK(ftp.Upload("/Hdd1/launch.ini", ini + "; edited\r\n", err));
    SafeWriteOutcome f = WriteLaunchIni(ftp, c.iniFtpPath, c.iniText, c.fixedIni);
    CHECK(f.result == SAFE_CONFLICT);
    CHECK(ftp.Download("/Hdd1/launch.ini", data, found, err) && data == ini + "; edited\r\n");

    CHECK(ftp.Upload("/Hdd1/launch.ini", ini, err));
    f = WriteLaunchIni(ftp, c.iniFtpPath, c.iniText, c.fixedIni);
    CHECK(f.result == SAFE_OK);
    CHECK(ftp.Download("/Hdd1/launch.ini", data, found, err) && data == c.fixedIni);
    CHECK(ftp.Download("/Hdd1/launch.ini.bak", data, found, err) && found && data == ini);
    CHECK(CheckInstall(ftp, kDir, c, err) && c.diag.diag == DIAG_LINE_OK && !c.fixOffered);

    // only launch.ini can be written this way
    CHECK(WriteLaunchIni(ftp, "/Hdd1/Aurora/Aurora.xex", "", "x").result == SAFE_ERROR);
    CHECK(WriteLaunchIni(ftp, "/Hdd1/X360RemapStudio/../launch.ini", ini, "x").result == SAFE_ERROR);

    // backup before an update: the settings, not the .xex
    std::vector<std::pair<std::string, std::string> > files;
    CHECK(DownloadStudioSettings(ftp, kDir, files, err));
    bool json = false, xex = false;
    for (size_t i = 0; i < files.size(); i++) {
        json = json || files[i].first == "X360Remap.json";
        xex = xex || files[i].first == "X360Remap.xex";
    }
    CHECK(json && !xex);

    // Aurora: the setup script lands in Aurora's utility scripts, ASCII only,
    // and the check sees "\X360RemapStudio" in its settings
    CHECK(InstallAuroraScript(ftp, err));
    CHECK(ftp.Download(kAuroraScriptPath, data, found, err) && found && data == AuroraSetupScript());
    for (size_t i = 0; i < data.size(); i++) CHECK((unsigned char)data[i] < 128);
    CHECK(data.find("INSERT INTO Config.ScanPaths") != std::string::npos && data.find("'Applications'") != std::string::npos);
    ftp.MakeDir("/Game/Data", err);
    ftp.MakeDir("/Game/Data/Databases", err);
    CHECK(ftp.Upload("/Game/Data/Databases/settings.db", std::string("SQLite format 3...\\Games..."), err));
    CHECK(CheckInstall(ftp, kDir, c, err) && c.auroraChecked && !c.auroraHasStudio);
    CHECK(ftp.Upload("/Game/Data/Databases/settings.db", std::string("SQLite format 3...\\X360RemapStudio..Applications"), err));
    CHECK(CheckInstall(ftp, kDir, c, err) && c.auroraHasStudio);

    // first install: the Studio folder is created, and creating it again is fine
    CHECK(ftp.MakeDir("/Hdd1/X360RemapStudioTest/", err));
    CHECK(ftp.MakeDir("/Hdd1/X360RemapStudioTest/", err));
    CHECK(ftp.Upload("/Hdd1/X360RemapStudioTest/a.txt", "x", err));
    ftp.Delete("/Hdd1/X360RemapStudioTest/a.txt", err);

    ftp.Delete(JoinRemote(kDir, "X360Remap.xex"), err);
    ftp.Delete("/Hdd1/launch.ini", err);
    ftp.Delete("/Hdd1/launch.ini.bak", err);
}

static bool Fetch(FtpClient& ftp, const char* name, std::string& data, bool& found) {
    std::string err;
    return ftp.Download(JoinRemote(kDir, name), data, found, err);
}

static void TestFtp(int port) {
    FtpClient ftp;
    std::string err;
    CHECK(ftp.Connect("127.0.0.1", port, "xboxftp", "xboxftp", err));
    if (!ftp.IsOpen()) { printf("  cannot connect: %s\n", err.c_str()); return; }

    std::vector<std::string> names;
    CHECK(ftp.ListNames(kDir, names, err));
    bool hasJson = false;
    for (size_t i = 0; i < names.size(); i++) hasJson = hasJson || names[i] == "X360Remap.json";
    CHECK(hasJson);

    std::string original, data;
    bool found = false;
    CHECK(Fetch(ftp, "X360Remap.json", original, found) && found);
    CHECK(Fetch(ftp, "does_not_exist.txt", data, found) && !found);

    // Acceptance 1: nothing changed -> nothing sent, file byte-identical
    SafeWriteOutcome o = SafeWriteFile(ftp, kDir, "X360Remap.json", original, true, original, false);
    CHECK(o.result == SAFE_UNCHANGED);
    CHECK(Fetch(ftp, "X360Remap.json", data, found) && data == original);

    // Normal push: new content, verified, previous kept as .bak, no .new left
    const std::string v2 = "[\n    {\n        \"vid\": 5426,\n        \"pid\": 555\n    }\n]";
    o = SafeWriteFile(ftp, kDir, "X360Remap.json", v2, true, original, false);
    CHECK(o.result == SAFE_OK);
    if (o.result != SAFE_OK) printf("  push error: %s\n", o.err.c_str());
    CHECK(Fetch(ftp, "X360Remap.json", data, found) && data == v2);
    CHECK(Fetch(ftp, "X360Remap.json.bak", data, found) && found && data == original);
    CHECK(Fetch(ftp, "X360Remap.json.new", data, found) && !found);

    // Acceptance 4: edited on the console since our last download -> conflict, nothing written
    const std::string consoleEdit = "[\n    {\n        \"vid\": 1,\n        \"pid\": 2\n    }\n]";
    CHECK(ftp.Upload(JoinRemote(kDir, "X360Remap.json"), consoleEdit, err));
    const std::string v3 = "[]";
    o = SafeWriteFile(ftp, kDir, "X360Remap.json", v3, true, v2, false);
    CHECK(o.result == SAFE_CONFLICT);
    CHECK(o.consoleData == consoleEdit);
    CHECK(Fetch(ftp, "X360Remap.json", data, found) && data == consoleEdit);
    // "Keep mine": forced, and the console version is the .bak
    o = SafeWriteFile(ftp, kDir, "X360Remap.json", v3, true, v2, true);
    CHECK(o.result == SAFE_OK);
    CHECK(Fetch(ftp, "X360Remap.json", data, found) && data == v3);
    CHECK(Fetch(ftp, "X360Remap.json.bak", data, found) && data == consoleEdit);

    // A file that does not exist yet on the console
    std::string ignored;
    ftp.Delete(JoinRemote(kDir, "camera_addresses.txt"), ignored);
    const std::string cams = "415608CB 4E77262A 00000000 82743CBC 82743CB8 -1 -1 deg Modern Warfare 3\n";
    o = SafeWriteFile(ftp, kDir, "camera_addresses.txt", cams, false, std::string(), false);
    CHECK(o.result == SAFE_OK);
    CHECK(Fetch(ftp, "camera_addresses.txt", data, found) && data == cams);

    // Never outside the Studio folder
    o = SafeWriteFile(ftp, "/Hdd1/", "launch.ini", "x", false, std::string(), true);
    CHECK(o.result == SAFE_ERROR);
    o = SafeWriteFile(ftp, kDir, "../launch.ini", "x", false, std::string(), true);
    CHECK(o.result == SAFE_ERROR);
    CHECK(Fetch(ftp, "../launch.ini", data, found) && !found);

    TestFtpInstall(ftp);

    // Back to the starting content for the next run
    o = SafeWriteFile(ftp, kDir, "X360Remap.json", original, false, std::string(), true);
    CHECK(o.result == SAFE_OK);
}

// Acceptance 5: the network is cut in the middle of the upload.
static void TestFtpCut(int port) {
    FtpClient ftp;
    std::string err, original, data;
    bool found = false;
    CHECK(ftp.Connect("127.0.0.1", port, "xboxftp", "xboxftp", err));
    if (!ftp.IsOpen()) return;
    CHECK(Fetch(ftp, "X360Remap.json", original, found) && found);

    std::string big = "[";
    for (int i = 0; i < 400; i++) big += "\n    { \"vid\": 1, \"pid\": 2 },";
    big += "\n    { \"vid\": 3, \"pid\": 4 }\n]";
    const SafeWriteOutcome o = SafeWriteFile(ftp, kDir, "X360Remap.json", big, true, original, false);
    CHECK(o.result == SAFE_ERROR);
    printf("  cut reported as: %s\n", o.err.c_str());

    FtpClient again;
    CHECK(again.Connect("127.0.0.1", port, "xboxftp", "xboxftp", err));
    if (!again.IsOpen()) return;
    bool f2 = false;
    CHECK(again.Download(JoinRemote(kDir, "X360Remap.json"), data, f2, err) && f2 && data == original);
}

// Release zip unpacked in the process (no tar.exe): stored and deflated
// entries, folders, and an entry trying to leave the folder.
static void TestExtractZip() {
    static const char good[] =
        "\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x00\x00\x21\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0e\x00\x00\x00\x58\x33"
        "\x36\x30\x52\x65\x6d\x61\x70\x2d\x31\x2e\x33\x2f\x50\x4b\x03\x04\x14\x00\x00\x00\x08\x00\x00\x00\x21\x00\xe0\xd0\xc1\x88\x71\x01"
        "\x00\x00\x04\x28\x00\x00\x1b\x00\x00\x00\x58\x33\x36\x30\x52\x65\x6d\x61\x70\x2d\x31\x2e\x33\x2f\x58\x33\x36\x30\x52\x65\x6d\x61"
        "\x70\x2e\x78\x65\x78\x8b\x70\x8d\x30\x62\x60\x64\x62\x66\x61\x65\x63\xe7\xe0\xe4\xe2\xe6\xe1\xe5\xe3\x17\x10\x14\x12\x16\x11\x15"
        "\x13\x97\x90\x94\x92\x96\x91\x95\x93\x57\x50\x54\x52\x56\x51\x55\x53\xd7\xd0\xd4\xd2\xd6\xd1\xd5\xd3\x37\x30\x34\x32\x36\x31\x35"
        "\x33\xb7\xb0\xb4\xb2\xb6\xb1\xb5\xb3\x77\x70\x74\x72\x76\x71\x75\x73\xf7\xf0\xf4\xf2\xf6\xf1\xf5\xf3\x0f\x08\x0c\x0a\x0e\x09\x0d"
        "\x0b\x8f\x88\x8c\x8a\x8e\x89\x8d\x8b\x4f\x48\x4c\x4a\x4e\x49\x4d\x4b\xcf\xc8\xcc\xca\xce\xc9\xcd\xcb\x2f\x28\x2c\x2a\x2e\x29\x2d"
        "\x2b\xaf\xa8\xac\xaa\xae\xa9\xad\xab\x6f\x68\x6c\x6a\x6e\x69\x6d\x6b\xef\xe8\xec\xea\xee\xe9\xed\xeb\x9f\x30\x71\xd2\xe4\x29\x53"
        "\xa7\x4d\x9f\x31\x73\xd6\xec\x39\x73\xe7\xcd\x5f\xb0\x70\xd1\xe2\x25\x4b\x97\x2d\x5f\xb1\x72\xd5\xea\x35\x6b\xd7\xad\xdf\xb0\x71"
        "\xd3\xe6\x2d\x5b\xb7\x6d\xdf\xb1\x73\xd7\xee\x3d\x7b\xf7\xed\x3f\x70\xf0\xd0\xe1\x23\x47\x8f\x1d\x3f\x71\xf2\xd4\xe9\x33\x67\xcf"
        "\x9d\xbf\x70\xf1\xd2\xe5\x2b\x57\xaf\x5d\xbf\x71\xf3\xd6\xed\x3b\x77\xef\xdd\x7f\xf0\xf0\xd1\xe3\x27\x4f\x9f\x3d\x7f\xf1\xf2\xd5"
        "\xeb\x37\x6f\xdf\xbd\xff\xf0\xf1\xd3\xe7\x2f\x5f\xbf\x7d\xff\xf1\xf3\xd7\xef\x3f\x7f\xff\xfd\x1f\xf5\xff\xa8\xff\x47\xfd\x3f\xea"
        "\xff\x51\xff\x8f\xfa\x7f\xd4\xff\xa3\xfe\x1f\xf5\xff\xa8\xff\x47\xfd\x3f\xea\xff\x51\xff\x8f\xfa\x7f\xd4\xff\xa3\xfe\x1f\xf5\xff"
        "\xa8\xff\x47\xfd\x3f\xea\xff\x51\xff\x8f\xfa\x7f\xd4\xff\xa3\xfe\x1f\xf5\xff\xa8\xff\x47\xfd\x3f\xea\xff\x51\xff\x8f\xfa\x7f\xd4"
        "\xff\xa3\xfe\x1f\xf5\xff\xa8\xff\x47\xfd\x3f\xea\xff\x51\xff\x8f\xfa\x7f\x28\xfa\x1f\x00\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00"
        "\x00\x00\x21\x00\x86\xa6\x10\x36\x05\x00\x00\x00\x05\x00\x00\x00\x18\x00\x00\x00\x58\x33\x36\x30\x52\x65\x6d\x61\x70\x2d\x31\x2e"
        "\x33\x2f\x72\x65\x61\x64\x6d\x65\x2e\x74\x78\x74\x68\x65\x6c\x6c\x6f\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x00\x00\x00\x00\x21"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x00\x58"
        "\x33\x36\x30\x52\x65\x6d\x61\x70\x2d\x31\x2e\x33\x2f\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x08\x00\x00\x00\x21\x00\xe0\xd0\xc1"
        "\x88\x71\x01\x00\x00\x04\x28\x00\x00\x1b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x2c\x00\x00\x00\x58\x33\x36\x30\x52"
        "\x65\x6d\x61\x70\x2d\x31\x2e\x33\x2f\x58\x33\x36\x30\x52\x65\x6d\x61\x70\x2e\x78\x65\x78\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00"
        "\x00\x00\x00\x00\x21\x00\x86\xa6\x10\x36\x05\x00\x00\x00\x05\x00\x00\x00\x18\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01"
        "\xd6\x01\x00\x00\x58\x33\x36\x30\x52\x65\x6d\x61\x70\x2d\x31\x2e\x33\x2f\x72\x65\x61\x64\x6d\x65\x2e\x74\x78\x74\x50\x4b\x05\x06"
        "\x00\x00\x00\x00\x03\x00\x03\x00\xcb\x00\x00\x00\x11\x02\x00\x00\x00\x00";
    static const char evil[] =
        "\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x00\x00\x21\x00\x83\x16\xdc\x8c\x01\x00\x00\x00\x01\x00\x00\x00\x0b\x00\x00\x00\x2e\x2e"
        "\x2f\x65\x76\x69\x6c\x2e\x74\x78\x74\x78\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x00\x00\x00\x00\x21\x00\x83\x16\xdc\x8c\x01\x00"
        "\x00\x00\x01\x00\x00\x00\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x00\x2e\x2e\x2f\x65\x76\x69\x6c\x2e"
        "\x74\x78\x74\x50\x4b\x05\x06\x00\x00\x00\x00\x01\x00\x01\x00\x39\x00\x00\x00\x2a\x00\x00\x00\x00\x00";
    const std::string goodZip = TempFile("x360remap_test_good.zip"), evilZip = TempFile("x360remap_test_evil.zip");
    const std::string dir = TempFile("x360remap_test_unzip");
    FILE* f = fopen(goodZip.c_str(), "wb"); fwrite(good, 1, sizeof(good) - 1, f); fclose(f);
    f = fopen(evilZip.c_str(), "wb"); fwrite(evil, 1, sizeof(evil) - 1, f); fclose(f);
    CHECK(plat::ExtractZip(goodZip, dir));
    const std::string x = ReadAll(dir + "\\X360Remap-1.3\\X360Remap.xex");
    CHECK(x.size() == 10244 && x.compare(0, 4, "XEX2") == 0 && (unsigned char)x[4 + 255] == 255);
    CHECK(ReadAll(dir + "\\X360Remap-1.3\\readme.txt") == "hello");
    CHECK(!plat::ExtractZip(evilZip, dir + "\\inner"));
    CHECK(ReadAll(dir + "\\evil.txt").empty());
    std::string damaged(good, sizeof(good) - 1);
    damaged[damaged.size() / 2] ^= 0x55;
    f = fopen(goodZip.c_str(), "wb"); fwrite(damaged.data(), 1, damaged.size(), f); fclose(f);
    CHECK(!plat::ExtractZip(goodZip, dir + "\\damaged"));
}

void TestLiveWatch(int xbdmPort) {
    CHECK(livewatch::DebugStrText("debugstr thread=7 lf string=X360Remap_LOG:a b") == "X360Remap_LOG:a b");
    CHECK(livewatch::DebugStrText("create thread=7") == "create thread=7");
    CHECK(livewatch::JsonHex("{\r\n  \"logBuffer\": \"9150abcd\",\r\n  \"logBufferLen\": \"00000010\"", "logBuffer") == 0x9150abcd);
    CHECK(livewatch::JsonHex("{}", "logBuffer") == 0);
    uint32_t v = 0;
    std::vector<std::string> ctx(1, "Msr=0x1 R1=0x7000AB00");
    CHECK(livewatch::ContextValue(ctx, "r1", v) && v == 0x7000AB00);
    CHECK(!livewatch::ContextValue(ctx, "sr1", v));
    CHECK(livewatch::ExceptionThread("exception code=0xc0000005 thread=0xfb000050 address=0x800bdf80 read=0x19 first") == 0xfb000050);
    CHECK(livewatch::ExceptionThread("create thread=0xfb000050") == 0);

    XbdmConn x;
    std::string err, mem;
    CHECK(x.Connect("127.0.0.1", xbdmPort, 2000, err));
    CHECK(x.GetMem(0x1000, 3, mem) && mem == "hi\n");
    x.Close();

    livewatch::Params p;
    p.host = "127.0.0.1"; p.xbdmPort = xbdmPort; p.ftpPort = 1; p.remoteDir = "/Hdd1/X360RemapStudio/";
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    p.outDir = std::string(tmp) + "x360remap_live_test";
    CreateDirectoryA(p.outDir.c_str(), nullptr);
    livewatch::Start(p);
    for (int i = 0; i < 50 && livewatch::Get().lines < 7; i++) Sleep(100);
    const livewatch::Snapshot live = livewatch::Get();
    CHECK(live.consoleAlive && live.notifyUp);
    std::string joined;
    for (size_t i = 0; i < live.tail.size(); i++) joined += live.tail[i] + "\n";
    CHECK(joined.find("X360Remap_LOG:hello") != std::string::npos);
    const std::string report = livewatch::SaveReport();
    livewatch::Stop();
    std::ifstream in(report.c_str(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("thread 7:") != std::string::npos && text.find("X360Remap_LOG:hello") != std::string::npos);
    std::ifstream cap(live.capturePath.c_str(), std::ios::binary);
    const std::string capture((std::istreambuf_iterator<char>(cap)), std::istreambuf_iterator<char>());
    CHECK(capture.find("X360Remap_LOG:hello") != std::string::npos);   // on disk at once
    // The two "hello" lines are counted as one plus a note, unless a "[watch]" line of the
    // other thread landed between them (seen once in 4 runs, 2026-11-12): then both are written.
    const size_t firstHello = capture.find("X360Remap_LOG:hello");
    CHECK(capture.find("repeated 1 more times") != std::string::npos ||
          capture.find("X360Remap_LOG:hello", firstHello + 1) != std::string::npos);
    CHECK(capture.find("faulting thread") != std::string::npos && capture.find("Msr=0x1") != std::string::npos);
}

void TestWatchDeath(int xbdmPort, bool rebooted) {
    livewatch::Params p;
    p.host = "127.0.0.1"; p.xbdmPort = xbdmPort; p.ftpPort = 1; p.remoteDir = "/Hdd1/X360RemapStudio/";
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    p.outDir = std::string(tmp) + "x360remap_live_test";
    CreateDirectoryA(p.outDir.c_str(), nullptr);
    livewatch::Start(p);
    for (int i = 0; i < (rebooted ? 90 : 150) && livewatch::Get().reportPath.empty(); i++) Sleep(100);
    const livewatch::Snapshot live = livewatch::Get();
    livewatch::Stop();
    if (rebooted) { CHECK(live.reportPath.empty() && !live.consoleAlive); return; }
    CHECK(!live.reportPath.empty() && !live.deathTime.empty() && !live.consoleAlive);
    std::ifstream in(live.reportPath.c_str(), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("time of death: " + live.deathTime) != std::string::npos);
    CHECK(text.find("console stopped answering") != std::string::npos);
}

void TestHudLayout() {
	char label[7];
	HudKeyLabel(0x60, label, sizeof(label)); CHECK(strcmp(label, "8") == 0);      // keypad 8
	HudKeyLabel(0x1A, label, sizeof(label)); CHECK(strcmp(label, "W") == 0);
	HudKeyLabel(0xE5, label, sizeof(label)); CHECK(strcmp(label, "RSHFT") == 0);
	HudKeyLabel(0x45, label, sizeof(label)); CHECK(strcmp(label, "F12") == 0);
	HudKeyLabel(0x27, label, sizeof(label)); CHECK(strcmp(label, "0") == 0);
	HudCap caps[HUD_MAX_CAPS];
	uint8_t codes[40];
	for (int i = 0; i < 40; i++) codes[i] = (uint8_t)(0x04 + i);
	codes[1] = codes[0];   // a key bound twice shows once
	int w = 0, h = 0;
	uint8_t actions[40] = { 1, 2, 5 };
	const int n = HudLayout(codes, actions, 40, caps, 420, 0, 35, &w, &h);
	CHECK(n == HUD_MAX_CAPS);
	CHECK(w <= 420);
	for (int i = 0; i < n; i++) CHECK(caps[i].x + caps[i].w <= 420);
	CHECK(caps[1].code == 0x06 && caps[1].action == 5);
	CHECK(h > 0 && h <= 6 * 35);
	CHECK(HudLayout(codes, actions, 0, caps, 420, 0, 35, &w, &h) == 0 && h == 0);
	CHECK(HudLayout(codes, actions, 8, caps, 420, 60, 38, &w, &h) == 7 && caps[1].w == 60 && caps[6].y == 38);
	CHECK(strcmp(HudActionLabel(1), "A") == 0 && strcmp(HudActionLabel(8), "RT") == 0 && HudActionLabel(200)[0] == 0);
}

// The console's address comes from the router: when the saved one stops answering, the
// scan's only other console is taken; a NAS or two consoles are never picked.
void TestPickMovedConsole() {
    std::vector<FoundFtp> found(3);
    found[0].ip = "192.168.1.5";   found[0].banner = "220 NAS FTP ready";
    found[1].ip = "192.168.1.100"; found[1].banner = "220 FtpDll Ready";
    found[2].ip = "192.168.1.101"; found[2].banner = "220 whatever"; found[2].xbdm = true;
    CHECK(PickMovedConsole(found, "192.168.1.100") == "192.168.1.101");   // the saved one is skipped
    CHECK(PickMovedConsole(found, "192.168.1.101") == "192.168.1.100");
    CHECK(PickMovedConsole(found, "192.168.1.102").empty());             // two consoles: the user chooses
    found.resize(1);
    CHECK(PickMovedConsole(found, "192.168.1.102").empty());             // only a NAS
    CHECK(PickMovedConsole(std::vector<FoundFtp>(), "192.168.1.102").empty());
}

int main(int argc, char** argv) {
    TestKeyTable();
    TestPickMovedConsole();
    TestSettingsRoundTrip();
    TestRemoteGuards();
    TestLaunchIniFix();
    TestReleaseParse();
    TestExtractZip();
    {   // device paths Windows gives for USB and Bluetooth HID devices
        uint16_t v = 0, p = 0;
        CHECK(plat::ParseVidPid("\\?\\HID#VID_046D&PID_C52B&MI_00#7&2c2b&0&0000#{884b96c3}", v, p) && v == 0x046D && p == 0xC52B);
        CHECK(plat::ParseVidPid("\\?\\hid#vid_1532&pid_022b&mi_01#8&1&0&0000#{378de44c}", v, p) && v == 0x1532 && p == 0x022B);
        CHECK(plat::ParseVidPid("\\?\\HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&0002046d_PID&b023&Col01#9&1#{4d1e55b2}", v, p) && v == 0x046D && p == 0xB023);
        CHECK(!plat::ParseVidPid("\\?\\ACPI#PNP0303#4&1d401fb5&0#{884b96c3}", v, p));
    }
    // test_windows.exe github: the real GitHub, read only (not run by run_tests.ps1)
    if (argc >= 2 && strcmp(argv[1], "github") == 0) {
        for (int previews = 1; previews >= 0; previews--) {
            ReleaseInfo r;
            std::string err, zip;
            const bool ok = FetchNewestRelease(previews != 0, r, err);
            CHECK(ok);
            printf("  previews=%d: %s id=%lld zip=%s %s\n", previews, r.name.c_str(), r.id, r.zipUrl.c_str(), err.c_str());
            if (ok && !r.zipUrl.empty()) {
                CHECK(HttpGet(r.zipUrl, zip, err) && zip.size() > 1000 && zip.compare(0, 2, "PK") == 0);
                printf("  zip downloaded: %u bytes %s\n", (unsigned)zip.size(), err.c_str());
            }
        }
    }
    TestHudLayout();
    if (argc >= 3 && strcmp(argv[1], "ftp") == 0) {
        TestFtp(atoi(argv[2]));
        // fake xbdm on port + 1000 (run_tests.ps1): the reboot request
        std::string err;
        CHECK(XbdmColdReboot("127.0.0.1", err, atoi(argv[2]) + 1000));
        CHECK(!XbdmColdReboot("127.0.0.1", err, 1));   // nothing listening there
        TestLiveWatch(atoi(argv[2]) + 1000);
    }
    if (argc >= 3 && strcmp(argv[1], "ftpcut") == 0) TestFtpCut(atoi(argv[2]));
    if (argc >= 3 && strcmp(argv[1], "watchdeath") == 0) TestWatchDeath(atoi(argv[2]) + 1000, false);
    if (argc >= 3 && strcmp(argv[1], "watchreboot") == 0) TestWatchDeath(atoi(argv[2]) + 1000, true);
    printf("%d checks passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
