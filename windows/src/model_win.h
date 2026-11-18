#pragma once
// Fills the shared page models (UiMappingModel, GlossyGamesModel...) from the
// working copy and handles what the pages ask for: the job application/main.cpp
// does on the console, with PC files, PC keys and toasts instead of Xbox APIs.
// The pages themselves are the console ones (application/ui_glossy.cpp).
#include <stdint.h>
#include <string>
#include <vector>

struct ID3D11Device;

extern const char* const kWinAppVersion;

void ModelInit(ID3D11Device* device);    // decodes the embedded textures
void ModelShutdown();
// Draws the current page inside the root window (canvas of w x h pixels).
void ModelDrawPages(float w, float h);

// X360Remap.json of the working copy.
std::string ModelConfigPath();
// true if the plugin's own parser (mapping.cpp) accepts this content.
bool ModelValidateConfigJson(const std::string& json);
// The working copy was replaced from outside the pages (pull, open, import).
void ModelWorkingCopyChanged();
// A function of this build just wrote X360Remap.json through config_writer.
void ModelOwnWriteDone();

int  ModelActiveTab();      // 0 keyboard & mouse, 1 game profiles, 2 how it works, 3 support
void ModelSetTab(int tab);
void ModelOpenPad(int index);   // --open=pad: the first USB controller of the console
void ModelSelectGame(uint32_t titleId);
void ModelSkipSplash();
// A gaming keypad with a known layout is among the console's devices: its
// specific help may be shown (and only then).
bool ModelKnownKeypadPresent();

// Keyboards, mice and controllers plugged into this PC.
struct ModelPcDevice {
    uint16_t    vid, pid;
    char        kind;     // 'K' keyboard, 'M' mouse, 'G' game controller
    std::string name;
    int         state;    // 0 not asked yet, 1 same as on the console, 2 not used for the console, 3 the console knows it
};
std::vector<ModelPcDevice> ModelPcDevices();
int  ModelPcDevicesToAsk();     // keyboards / mice never answered and unknown to the console
void ModelAnswerPcDevice(uint16_t vid, uint16_t pid, char kind, bool same);
void ModelRescanPcDevices();    // a device was plugged or unplugged
// Background picture of the pages, to continue it around the 16:9 canvas.
void* ModelBackgroundTexture();
bool ModelCapturing();      // a key capture is waiting for a key press

struct ModelDevice {
    bool     valid;
    uint16_t vid, pid;
    char     name[64];
};
ModelDevice ModelSelectedMouse();
ModelDevice ModelSelectedKeyboard();
bool ModelSelectedGame(uint32_t& titleId, std::string& name);
void ModelRefreshGames();   // profiles or cameras changed

// Mouse settings the console pages do not show yet.
struct MouseExtra {
    int   adsSensitivity;        // % of the normal speed while aiming, 0 = 100 %
    int   directAimSmoothingMs;  // 0 = off
    int   curveType;             // 0 linear, 1 exponential
    float curveExponent;
};
bool ModelGetMouseExtra(MouseExtra& out);
bool ModelSetMouseExtra(const MouseExtra& in);

// RGBA pixels -> texture usable as ImTextureID. Null on failure.
void* ModelCreateTexture(const unsigned char* rgba, int w, int h);
void  ModelReleaseTexture(void* texture);
