#pragma once
#include <stdint.h>

struct InputStateSnapshot {
    // Initialisation via constructeur plutot que des valeurs par defaut
    // dans la classe (NSDMI, C++11) - le toolchain XDK Xbox 360 (VS2010,
    // PlatformToolset 2010-01) ne supporte pas les NSDMI (erreur C2864,
    // trouvee au premier vrai build de application.vcxproj le 2026-08-01).
    // g++ en C++14 (utilise pour les tests portables) les acceptait sans
    // broncher, d'ou le fait que ce bug n'a ete detecte qu'au build reel.
    InputStateSnapshot()
        : keyCode(-1), mouseButton(-1), mouseDX(0), mouseDY(0),
          mouseButtonsMask(0), wheelDelta(0), rawLen(0), tick(0), rawSeq(0), hasRawSeq(false), valid(false),
          hasDevs(false), devCount(0) {
        for (int i = 0; i < 16; i++) raw[i] = 0;
        for (int i = 0; i < 4; i++) { devType[i] = 0; devVid[i] = devPid[i] = 0; }
    }

    int32_t keyCode;      // HID keycode of the key currently held on the
                           // first connected keyboard, -1 = none
    int32_t mouseButton;  // 0=left/1=right/2=middle on the first
                           // connected mouse, -1 = none
    // mouseDX/mouseDY/mouseButtonsMask added 2026-08-01 for the application
    // mouse cursor (see PROJECT_NOTES.md "Jalon 4"). mouseDX/mouseDY are the
    // relative motion accumulated by plugin.xex since the last time it wrote
    // this file (its own dedicated accumulator, separate from the one that
    // feeds the right-stick emulation - see Controller::cursorAccumX in
    // X360Remap/main.cpp) - NOT an absolute position, add to a running
    // on-screen cursor position every frame. mouseButtonsMask is the raw HID
    // button bitmask (bit0=left/bit1=right/bit2=middle...), 0 when nothing is
    // held or no mouse is connected - unlike mouseButton above, 0 is already
    // the correct "nothing held" value here so no -1 sentinel is needed.
    int32_t mouseDX;
    int32_t mouseDY;
    int32_t mouseButtonsMask;

    // Molette accumulee depuis la derniere lecture de ce fichier (2026-08-09,
    // demande utilisateur : pouvoir faire defiler l'interface a la molette
    // au lieu de cliquer sur des fleches). Meme principe que mouseDX/mouseDY
    // ci-dessus - propre accumulateur cote plugin (Controller::uiWheelAccum,
    // X360Remap/main.cpp), drainé et remis a zero a chaque ecriture. Positif
    // = molette vers l'avant. A ne pas confondre avec le champ "molette"
    // utilise pour le binding manette (mouseWheelAccum/bindWheelDir cote
    // plugin) - celui-ci est dedie au defilement de CETTE interface.
    int32_t wheelDelta;

    // Octets bruts du dernier rapport USB de la souris (2026-08-03), tels
    // que recus par le plugin avant toute interpretation. Alimentent
    // l'assistant de calibration : c'est en observant QUELS octets changent
    // pendant un geste demande que l'application deduit la disposition du
    // paquet, sans jamais lire le descripteur HID (voir mouse_calibration.h).
    // rawLen == 0 signifie que le plugin n'a pas encore recu de rapport, ou
    // qu'aucune souris n'est connectee.
    uint8_t raw[16];
    uint8_t rawLen;

    // Peripheriques REELLEMENT branches (champ "devs", 2026-09-25) : 'k'
    // clavier / 'm' souris + VID:PID. hasDevs = false avec un plugin plus
    // ancien qui n'ecrit pas ce champ (on retombe alors sur l'ancien critere).
    bool     hasDevs;
    int      devCount;
    char     devType[4];
    uint16_t devVid[4];
    uint16_t devPid[4];

    uint32_t tick;         // plugin.xex's GetTickCount() at write time
    // Numero du dernier rapport souris recu par le plugin (2026-09-25). Change
    // seulement quand un NOUVEAU rapport arrive, contrairement a tick.
    // hasRawSeq == false : plugin plus ancien, champ absent.
    uint32_t rawSeq;
    bool     hasRawSeq;
    bool valid;            // false if the file couldn't be read/parsed -
                            // e.g. plugin not loaded yet, or no
                            // keyboard/mouse connected (plugin doesn't
                            // write the file in that case, see main.cpp)
};

// Reads HDD:\input_state.json, written continuously by plugin.xex - see
// ARCHITECTURE.md for why this file exists instead of application.xex
// reading the USB device itself. Uses the same std::ifstream whole-file read
// already proven reliable in this project for X360Remap.json
// (LoadMappingsFromFile in mapping.cpp) - not CreateFileA/GetFileTime, which
// is only needed for change-DETECTION via timestamps (the actual problem
// plugin.xex's own hot-reload had to solve); a plain content read every
// frame doesn't need that.
//
// path defaults to the real console location; tests pass a temp file
// instead, since HDD:\ doesn't exist off-console.
InputStateSnapshot ReadInputState();
InputStateSnapshot ReadInputState(const char* path);
