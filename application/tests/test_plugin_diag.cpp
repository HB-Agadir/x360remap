// g++ -std=c++11 test_plugin_diag.cpp -o test_plugin_diag
#include <cstdio>
#include <string>
#include "../plugin_diag.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

static bool g_exists = true;
static bool Exists(const std::string& p) { (void)p; return g_exists; }

int main() {
    CHECK(DiagnoseLaunchIni(nullptr, Exists).diag == DIAG_NO_INI);

    std::string a = "[Paths]\r\nDefault = Hdd:\\App\\Aurora\\Aurora.xex\r\n[Plugins]\r\nplugin1 = Hdd:\\xbdm.xex\r\n";
    CHECK(DiagnoseLaunchIni(&a, Exists).diag == DIAG_NO_LINE);

    // Commented line does not count; a line outside [Plugins] does not count.
    std::string b = "[Plugins]\nplugin1 = Hdd:\\xbdm.xex\n; plugin2 = Hdd:\\X360Remap.xex\n[Settings]\nplugin3 = Hdd:\\X360Remap.xex\n";
    CHECK(DiagnoseLaunchIni(&b, Exists).diag == DIAG_NO_LINE);

    // The Studio xex is not the plugin.
    std::string c = "[Plugins]\nplugin2 = Hdd:\\X360RemapStudio\\X360RemapStudio.xex\n";
    CHECK(DiagnoseLaunchIni(&c, Exists).diag == DIAG_NO_LINE);

    std::string d = "[plugins]\r\nplugin2 = Hdd:\\X360RemapStudio\\X360Remap.xex ; 1.3\r\n";
    g_exists = false;
    PluginDiagResult r = DiagnoseLaunchIni(&d, Exists);
    CHECK(r.diag == DIAG_FILE_MISSING);
    CHECK(r.localPath == "HDD:\\X360RemapStudio\\X360Remap.xex");
    CHECK(r.pluginPath == "Hdd:\\X360RemapStudio\\X360Remap.xex");

    g_exists = true;
    CHECK(DiagnoseLaunchIni(&d, Exists).diag == DIAG_LINE_OK);

    std::string e = "[Plugins]\nplugin1 = Hdd:\\hiddriver360.xex\nplugin2 = \\Device\\Harddisk0\\Partition1\\X360Remap.xex\n";
    r = DiagnoseLaunchIni(&e, Exists);
    CHECK(r.diag == DIAG_HIDDRIVER);
    CHECK(r.localPath == "HDD:\\X360Remap.xex");
    CHECK(r.otherPath == "Hdd:\\hiddriver360.xex");

    // USB path: cannot be checked from here -> not reported as missing.
    std::string f = "[Plugins]\nplugin2 = Usb:\\X360Remap.xex\n";
    g_exists = false;
    CHECK(DiagnoseLaunchIni(&f, Exists).diag == DIAG_LINE_OK);

    if (g_fail) { std::printf("%d FAILURE(S)\n", g_fail); return 1; }
    std::printf("ALL PLUGIN DIAG TESTS PASSED\n");
    return 0;
}
