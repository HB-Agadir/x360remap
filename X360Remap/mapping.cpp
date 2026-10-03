#include "mapping.h"
#include "mouse_calibration.h" // IsLayoutUsable, pour valider une disposition relue du JSON
#include <string.h>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>
#include <cstdio>
#include <math.h> // powf/fabsf, utilises par ApplySensitivityCurve (Jalon 7)

static const HidAxisMapEntry kDefaultAxisMap[] = {
    { HID_USAGE_AXIS_X,  &ButtonsReport::x  },
    { HID_USAGE_AXIS_Y,  &ButtonsReport::y  },
    { HID_USAGE_AXIS_Z,  &ButtonsReport::z  },
    { HID_USAGE_AXIS_RX, &ButtonsReport::rx },
    { HID_USAGE_AXIS_RY, &ButtonsReport::ry },
    { HID_USAGE_AXIS_RZ, &ButtonsReport::rz },
};

static const HidButtonMapEntry kPlayStationButtonMapping[] = {
    { 1,  &ButtonsReport::a_button    },
    { 2,  &ButtonsReport::b_button   },
    { 0,  &ButtonsReport::x_button   },
    { 3,  &ButtonsReport::y_button },
    { 4,  &ButtonsReport::l1       },
    { 5,  &ButtonsReport::r1       },
    { 8,  &ButtonsReport::back     },
    { 9,  &ButtonsReport::start    },
    { 10, &ButtonsReport::l3       },
    { 11, &ButtonsReport::r3       },
    { 12, &ButtonsReport::xbox     },
};

static const HidButtonMapEntry kDualShock3ButtonMapping[] = {
    { 14,  &ButtonsReport::a_button    },
    { 13,  &ButtonsReport::b_button   },
    { 15,  &ButtonsReport::x_button   },
    { 12,  &ButtonsReport::y_button },
    { 10,  &ButtonsReport::l1       },
    { 11,  &ButtonsReport::r1       },
    { 8,  &ButtonsReport::l2       },
    { 9,  &ButtonsReport::r2       },
    { 0,  &ButtonsReport::back     },
    { 3,  &ButtonsReport::start    },
    { 1, &ButtonsReport::l3       },
    { 2, &ButtonsReport::r3       },
    { 16, &ButtonsReport::xbox     },
    { 7, &ButtonsReport::dpad_left },
    { 5, &ButtonsReport::dpad_right },
    { 4, &ButtonsReport::dpad_up },
    { 6, &ButtonsReport::dpad_down },
};


static HidDeviceMapping kStaticDeviceMappings[] = {
    // ds3
    {   
        1356, 616,
        kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
        kDualShock3ButtonMapping, sizeof(kDualShock3ButtonMapping) / sizeof(kDualShock3ButtonMapping[0]),
        {false, true, false, false, false, true},
    },
    // ds4 v2
    {
        1356, 2508,
        kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
        kPlayStationButtonMapping, sizeof(kPlayStationButtonMapping) / sizeof(kPlayStationButtonMapping[0]),
        {false, true, false, false, false, true},
    },

    // ds4 v1
    {
        1356, 1476,
        kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
        kPlayStationButtonMapping, sizeof(kPlayStationButtonMapping) / sizeof(kPlayStationButtonMapping[0]),
        {false, true, false, false, false, true},
    },

    // ds4 wireless adapter
   {
       1356, 0x0BA0,
       kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
       kPlayStationButtonMapping, sizeof(kPlayStationButtonMapping) / sizeof(kPlayStationButtonMapping[0]),
       {false, true, false, false, false, true},
   },

    // dualsense
    {
        1356, 3302,
        kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
        kPlayStationButtonMapping, sizeof(kPlayStationButtonMapping) / sizeof(kPlayStationButtonMapping[0]),
        {false, true, false, false, false, true},
    },

    // dualsense edge
    {
        1356, 0x0DF2,
        kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
        kPlayStationButtonMapping, sizeof(kPlayStationButtonMapping) / sizeof(kPlayStationButtonMapping[0]),
        {false, true, false, false, false, true},
    },

    // switch pro controller(This is a dummy mapping as their HID descriptor is broken)
    {
        0x057E, 0x2009,
        kDefaultAxisMap,  sizeof(kDefaultAxisMap) / sizeof(kDefaultAxisMap[0]),
        kPlayStationButtonMapping, sizeof(kPlayStationButtonMapping) / sizeof(kPlayStationButtonMapping[0]),
        {false, true, false, false, false, true},
    },
};

std::vector<HidDeviceMapping> g_dynamicMappings;
std::vector<std::unique_ptr<DynamicMappingData>> g_dynamicData;

void ApplyStickLayout(int16_t* lx, int16_t* ly, int16_t* rx, int16_t* ry, int layout) {
    if (!lx || !ly || !rx || !ry) return;
    const bool swapX = (layout == STICKS_SOUTHPAW || layout == STICKS_LEGACY);
    const bool swapY = (layout == STICKS_SOUTHPAW || layout == STICKS_LEGACY_SOUTHPAW);
    if (swapX) { int16_t t = *lx; *lx = *rx; *rx = t; }
    if (swapY) { int16_t t = *ly; *ly = *ry; *ry = t; }
}

void ApplyAntiDeadzone(int32_t* x, int32_t* y, int32_t adX, int32_t adY) {
    if (!x || !y) return;
    if (adX < 0) adX = 0;
    if (adY < 0) adY = 0;
    if (adX <= 0 && adY <= 0) return;
    if (*x == 0 && *y == 0) return;              // au repos : rien
    if (adX > 32000) adX = 32000;
    if (adY > 32000) adY = 32000;
    const float fx = (float)*x, fy = (float)*y;
    const float mag = sqrtf(fx * fx + fy * fy);
    if (mag <= 0.0f) return;
    // Seuil dans la direction du geste : point (adX*cos, adY*sin) de l'ellipse.
    const float c = fx / mag, sn = fy / mag;
    const float ex = (float)adX * c, ey = (float)adY * sn;
    const float antiDeadzone = sqrtf(ex * ex + ey * ey);
    float m = mag > 32767.0f ? 32767.0f : mag;
    const float outMag = antiDeadzone + m * (32767.0f - antiDeadzone) / 32767.0f;
    const float k = outMag / mag;
    float ox = fx * k, oy = fy * k;
    if (ox > 32767.0f) ox = 32767.0f; if (ox < -32768.0f) ox = -32768.0f;
    if (oy > 32767.0f) oy = 32767.0f; if (oy < -32768.0f) oy = -32768.0f;
    // Arrondi vers l'exterieur : un axe non nul ne doit jamais retomber a 0.
    int32_t ix = (int32_t)(ox >= 0.0f ? ox + 0.5f : ox - 0.5f);
    int32_t iy = (int32_t)(oy >= 0.0f ? oy + 0.5f : oy - 0.5f);
    if (*x != 0 && ix == 0) ix = (*x > 0) ? 1 : -1;
    if (*y != 0 && iy == 0) iy = (*y > 0) ? 1 : -1;
    *x = ix; *y = iy;
}

void SmoothMouseDelta(SmoothingState* st, uint32_t nowMs, int32_t* dx, int32_t* dy, int32_t windowMs) {
    if (!st || !dx || !dy) return;
    uint32_t dt = st->lastT ? (nowMs - st->lastT) : 16;
    st->lastT = nowMs ? nowMs : 1;
    if (dt < 1) dt = 1;          // deux lectures dans la meme milliseconde
    if (dt > 200) dt = 200;      // longue pause (menu, chargement)

    float outX = 0.0f, outY = 0.0f;
    if (windowMs <= 0) {
        // Desactive : tout ce qui etait en attente sort maintenant, puis
        // passage direct.
        for (int k = 0; k < SMOOTH_SLOTS; k++) {
            if (!st->used[k]) continue;
            outX += st->remX[k]; outY += st->remY[k];
            st->used[k] = 0;
        }
        outX += (float)*dx; outY += (float)*dy;
    } else {
        if (windowMs > 200) windowMs = 200;
        // Nouveau deplacement dans un emplacement libre (sinon on vide le
        // plus ancien d'un coup - ne peut arriver qu'avec une fenetre longue
        // et un jeu qui lit tres vite).
        if (*dx != 0 || *dy != 0) {
            int slot = -1;
            for (int k = 0; k < SMOOTH_SLOTS; k++) if (!st->used[k]) { slot = k; break; }
            if (slot < 0) {
                slot = 0;
                outX += st->remX[0]; outY += st->remY[0];
            }
            st->used[slot] = 1;
            st->totX[slot] = st->remX[slot] = (float)*dx;
            st->totY[slot] = st->remY[slot] = (float)*dy;
        }
        const float share = (float)dt / (float)windowMs;   // part du total rendue a cette lecture
        for (int k = 0; k < SMOOTH_SLOTS; k++) {
            if (!st->used[k]) continue;
            float rx = st->totX[k] * share, ry = st->totY[k] * share;
            if (fabsf(rx) >= fabsf(st->remX[k])) rx = st->remX[k];
            if (fabsf(ry) >= fabsf(st->remY[k])) ry = st->remY[k];
            st->remX[k] -= rx; st->remY[k] -= ry;
            outX += rx; outY += ry;
            if (fabsf(st->remX[k]) < 1e-4f && fabsf(st->remY[k]) < 1e-4f) st->used[k] = 0;
        }
    }
    const float fx = outX + st->fracX, fy = outY + st->fracY;
    *dx = (int32_t)fx;
    *dy = (int32_t)fy;
    st->fracX = fx - (float)*dx;
    st->fracY = fy - (float)*dy;
}

HidDeviceMapping* FindStaticMapping(uint16_t vid, uint16_t pid) {
    for (size_t i = 0; i < sizeof(kStaticDeviceMappings) / sizeof(kStaticDeviceMappings[0]); i++) {
        auto& m = kStaticDeviceMappings[i];
        if (m.vendorId == vid && m.productId == pid)
            return &m;
    }
    return nullptr;
}


HidDeviceMapping* FindMapping(uint16_t vid, uint16_t pid) {
    for (int i = 0; i < g_dynamicMappings.size(); i++) {
        auto& m = g_dynamicMappings[i];
        if (m.vendorId == vid && m.productId == pid)
            return &m;
    }

    // Fall back to static mappings
    return FindStaticMapping(vid, pid);
}

static const char* kAxisFieldNames[] = {
    "x", "y", "z", "rx", "ry", "rz"
};

static const char* kButtonFieldNames[] = {
    "a_button", "b_button", "x_button", "y_button",
    "l1", "r1", "l2", "r2",
    "back", "start", "l3", "r3",
    "xbox", "dpad_left", "dpad_right", "dpad_up", "dpad_down"
};

// Keyboard action names as they appear in X360Remap.json.
static const struct { const char* name; uint8_t action; } kKeyActionNames[] = {
    { "a_button",      KEYACT_A },
    { "b_button",      KEYACT_B },
    { "x_button",      KEYACT_X },
    { "y_button",      KEYACT_Y },
    { "l1",            KEYACT_LB },
    { "r1",            KEYACT_RB },
    { "l2",            KEYACT_LT },
    { "r2",            KEYACT_RT },
    { "l3",            KEYACT_L3 },
    { "r3",            KEYACT_R3 },
    { "start",         KEYACT_START },
    { "back",          KEYACT_BACK },
    { "xbox",          KEYACT_GUIDE },
    { "dpad_up",       KEYACT_DPAD_UP },
    { "dpad_down",     KEYACT_DPAD_DOWN },
    { "dpad_left",     KEYACT_DPAD_LEFT },
    { "dpad_right",    KEYACT_DPAD_RIGHT },
    { "lstick_up",     KEYACT_LSTICK_UP },
    { "lstick_down",   KEYACT_LSTICK_DOWN },
    { "lstick_left",   KEYACT_LSTICK_LEFT },
    { "lstick_right",  KEYACT_LSTICK_RIGHT },
    { "rstick_up",     KEYACT_RSTICK_UP },
    { "rstick_down",   KEYACT_RSTICK_DOWN },
    { "rstick_left",   KEYACT_RSTICK_LEFT },
    { "rstick_right",  KEYACT_RSTICK_RIGHT },
};

static uint8_t CurveTypeFromName(const char* name) {
    if (name && strcmp(name, "exponential") == 0)
        return CURVE_EXPONENTIAL;
    return CURVE_LINEAR;
}

static const char* CurveTypeToName(uint8_t type) {
    return type == CURVE_EXPONENTIAL ? "exponential" : "linear";
}

uint8_t KeyActionFromName(const char* name) {
    if (!name) return KEYACT_NONE;
    for (size_t i = 0; i < sizeof(kKeyActionNames) / sizeof(kKeyActionNames[0]); i++) {
        if (strcmp(name, kKeyActionNames[i].name) == 0)
            return kKeyActionNames[i].action;
    }
    return KEYACT_NONE;
}

const char* KeyActionToName(uint8_t action) {
    for (size_t i = 0; i < sizeof(kKeyActionNames) / sizeof(kKeyActionNames[0]); i++) {
        if (kKeyActionNames[i].action == action)
            return kKeyActionNames[i].name;
    }
    return nullptr;
}

static int16_t ButtonsReport::* GetAxisFieldPtr(const char* name) {
    if (strcmp(name, "x") == 0)  return &ButtonsReport::x;
    if (strcmp(name, "y") == 0)  return &ButtonsReport::y;
    if (strcmp(name, "z") == 0)  return &ButtonsReport::z;
    if (strcmp(name, "rx") == 0) return &ButtonsReport::rx;
    if (strcmp(name, "ry") == 0) return &ButtonsReport::ry;
    if (strcmp(name, "rz") == 0) return &ButtonsReport::rz;
    return nullptr;
}

uint8_t ButtonsReport::* GetButtonFieldPtr(const char* name) {
    if (strcmp(name, "a_button") == 0)     return &ButtonsReport::a_button;
    if (strcmp(name, "b_button") == 0)    return &ButtonsReport::b_button;
    if (strcmp(name, "x_button") == 0)    return &ButtonsReport::x_button;
    if (strcmp(name, "y_button") == 0)    return &ButtonsReport::y_button;
    if (strcmp(name, "l1") == 0)          return &ButtonsReport::l1;
    if (strcmp(name, "r1") == 0)          return &ButtonsReport::r1;
    if (strcmp(name, "l2") == 0)          return &ButtonsReport::l2;
    if (strcmp(name, "r2") == 0)          return &ButtonsReport::r2;
    if (strcmp(name, "back") == 0)        return &ButtonsReport::back;
    if (strcmp(name, "start") == 0)       return &ButtonsReport::start;
    if (strcmp(name, "l3") == 0)          return &ButtonsReport::l3;
    if (strcmp(name, "r3") == 0)          return &ButtonsReport::r3;
    if (strcmp(name, "xbox") == 0)        return &ButtonsReport::xbox;
    if (strcmp(name, "dpad_left") == 0)   return &ButtonsReport::dpad_left;
    if (strcmp(name, "dpad_right") == 0)  return &ButtonsReport::dpad_right;
    if (strcmp(name, "dpad_up") == 0)     return &ButtonsReport::dpad_up;
    if (strcmp(name, "dpad_down") == 0)   return &ButtonsReport::dpad_down;
    return nullptr;
}

static const char* GetAxisFieldName(int16_t ButtonsReport::* ptr) {
    if (ptr == &ButtonsReport::x)  return "x";
    if (ptr == &ButtonsReport::y)  return "y";
    if (ptr == &ButtonsReport::z)  return "z";
    if (ptr == &ButtonsReport::rx) return "rx";
    if (ptr == &ButtonsReport::ry) return "ry";
    if (ptr == &ButtonsReport::rz) return "rz";
    return nullptr;
}

const char* GetButtonFieldName(uint8_t ButtonsReport::* ptr) {
    if (ptr == &ButtonsReport::a_button)     return "a_button";
    if (ptr == &ButtonsReport::b_button)    return "b_button";
    if (ptr == &ButtonsReport::x_button)    return "x_button";
    if (ptr == &ButtonsReport::y_button)    return "y_button";
    if (ptr == &ButtonsReport::l1)          return "l1";
    if (ptr == &ButtonsReport::r1)          return "r1";
    if (ptr == &ButtonsReport::l2)          return "l2";
    if (ptr == &ButtonsReport::r2)          return "r2";
    if (ptr == &ButtonsReport::back)        return "back";
    if (ptr == &ButtonsReport::start)       return "start";
    if (ptr == &ButtonsReport::l3)          return "l3";
    if (ptr == &ButtonsReport::r3)          return "r3";
    if (ptr == &ButtonsReport::xbox)        return "xbox";
    if (ptr == &ButtonsReport::dpad_left)   return "dpad_left";
    if (ptr == &ButtonsReport::dpad_right)  return "dpad_right";
    if (ptr == &ButtonsReport::dpad_up)     return "dpad_up";
    if (ptr == &ButtonsReport::dpad_down)   return "dpad_down";
    return nullptr;
}

// --- Jalon 7 : profils par jeu + courbe de sensibilite (application/ROADMAP_APPLICATION.md) ---
// Logique pure (aucune API Xbox), testee via g++ dans
// X360Remap/tests/test_mapping_profiles.cpp avant d'etre appelee depuis
// X360Remap/main.cpp (methode de travail habituelle de ce projet).

const HidDeviceProfile* FindActiveProfile(const HidDeviceMapping* map, uint32_t titleId) {
    if (!map || titleId == 0)
        return nullptr;
    for (uint8_t i = 0; i < map->profileCount; i++) {
        if (map->profiles[i].titleId == titleId)
            return &map->profiles[i];
    }
    return nullptr;
}

EffectiveMouseSettings ResolveEffectiveMouseSettings(const HidDeviceMapping* map, uint32_t titleId) {
    EffectiveMouseSettings out;
    // Base = reglages du device, memes valeurs/conventions que le code
    // existant de main.cpp avant ce jalon (0/false/CURVE_LINEAR = pas
    // d'override, comportement inchange pour qui n'utilise pas de profil).
    out.mouseSensitivity = map ? map->mouseSensitivity : 0;
    out.invertMouseY = map ? map->invertMouseY : false;
    out.deadzone = map ? map->deadzone : 0;
    out.sensitivityCurveType = map ? map->sensitivityCurveType : CURVE_LINEAR;
    out.sensitivityCurveExponent = map ? map->sensitivityCurveExponent : 0.0f;
    out.antiDeadzone = map ? map->antiDeadzone : 0;
    int32_t adYRaw = map ? map->antiDeadzoneY : 0;
    out.smoothingMs = map ? map->smoothingMs : 0;

    const HidDeviceProfile* profile = FindActiveProfile(map, titleId);
    if (profile) {
        if (profile->mouseSensitivity != 0)
            out.mouseSensitivity = profile->mouseSensitivity;
        if (profile->invertMouseY != -1)
            out.invertMouseY = (profile->invertMouseY != 0);
        if (profile->deadzone != -1)
            out.deadzone = profile->deadzone;
        if (profile->antiDeadzone >= 0)
            out.antiDeadzone = profile->antiDeadzone;
        if (profile->antiDeadzoneY >= 0)
            adYRaw = profile->antiDeadzoneY;
        if (profile->smoothingMs >= 0)
            out.smoothingMs = profile->smoothingMs;
        if (profile->curveType != -1) {
            out.sensitivityCurveType = static_cast<uint8_t>(profile->curveType);
            out.sensitivityCurveExponent = profile->curveExponent;
        }
    }
    // Vertical : 0 = identique a l'horizontal.
    out.antiDeadzoneY = adYRaw > 0 ? adYRaw : out.antiDeadzone;
    return out;
}

const HidButtonMapEntry* ResolveEffectiveButtonMap(const HidDeviceMapping* map, uint32_t titleId, uint8_t* outCount) {
    if (outCount)
        *outCount = 0;
    if (!map || !outCount)
        return nullptr;

    const HidDeviceProfile* profile = FindActiveProfile(map, titleId);
    if (profile && profile->buttonMapCount > 0) {
        *outCount = profile->buttonMapCount;
        return profile->buttonMap;
    }
    *outCount = map->buttonMapCount;
    return map->buttonMap;
}

const HidKeyMapEntry* ResolveEffectiveKeyMap(const HidDeviceMapping* map, uint32_t titleId, uint8_t* outCount) {
    if (outCount)
        *outCount = 0;
    if (!map || !outCount)
        return nullptr;

    const HidDeviceProfile* profile = FindActiveProfile(map, titleId);
    if (profile && profile->keyMapCount > 0) {
        *outCount = profile->keyMapCount;
        return profile->keyMap;
    }
    *outCount = map->keyMapCount;
    return map->keyMap;
}

// --- Resolution par FUSION (2026-08-03) -----------------------------------
// BUG REEL, confirme par le X360Remap.json de l'utilisateur. Les deux
// fonctions Resolve* ci-dessus REMPLACENT integralement la table du device
// par celle du profil des que celle-ci est non vide. Or la liaison rapide
// (Back+Start) n'ecrit qu'UNE SEULE entree dans le profil. Consequence :
// lier un seul bouton pour un jeu tuait silencieusement TOUT le reste pour
// ce jeu.
//
// Constate dans le fichier reel : le profil clavier du titleId 1396901868 ne
// contenait que 2 touches (dpad_up/dpad_down) - les 19 autres, pourtant
// definies au niveau device, etaient mortes des qu'on lancait ce jeu. D'ou
// "j'ai perdu mes commandes clavier, j'ai du les refaire au wizard" : elles
// n'avaient jamais ete effacees du fichier, elles etaient juste masquees en
// jeu. Meme mecanisme cote souris (profil 1297287339 : seul idx 0 survivait).
//
// Semantique correcte, et celle que tout utilisateur attend : le profil
// SURCHARGE les entrees du device qui portent le meme idx/code, et laisse
// vivre toutes les autres. On ecrit dans un tampon fourni par l'appelant
// plutot que de renvoyer un pointeur : ces fonctions sont appelees depuis le
// contexte USB a chaque rapport, aucune allocation n'y est admissible.
uint8_t ResolveMergedButtonMap(const HidDeviceMapping* map, uint32_t titleId,
                                HidButtonMapEntry* out, uint8_t outCapacity) {
    if (!map || !out || outCapacity == 0)
        return 0;

    uint8_t n = 0;
    for (uint8_t i = 0; i < map->buttonMapCount && n < outCapacity; i++)
        out[n++] = map->buttonMap[i];

    const HidDeviceProfile* profile = FindActiveProfile(map, titleId);
    if (!profile)
        return n;

    for (uint8_t i = 0; i < profile->buttonMapCount; i++) {
        const HidButtonMapEntry& pe = profile->buttonMap[i];
        bool replaced = false;
        for (uint8_t j = 0; j < n; j++) {
            if (out[j].idx == pe.idx) { out[j] = pe; replaced = true; break; }
        }
        if (!replaced && n < outCapacity)
            out[n++] = pe;
    }
    return n;
}

uint8_t ResolveMergedKeyMap(const HidDeviceMapping* map, uint32_t titleId,
                             HidKeyMapEntry* out, uint8_t outCapacity) {
    if (!map || !out || outCapacity == 0)
        return 0;

    uint8_t n = 0;
    for (uint8_t i = 0; i < map->keyMapCount && n < outCapacity; i++)
        out[n++] = map->keyMap[i];

    const HidDeviceProfile* profile = FindActiveProfile(map, titleId);
    if (!profile)
        return n;

    for (uint8_t i = 0; i < profile->keyMapCount; i++) {
        const HidKeyMapEntry& pe = profile->keyMap[i];
        bool replaced = false;
        for (uint8_t j = 0; j < n; j++) {
            if (out[j].code == pe.code) { out[j] = pe; replaced = true; break; }
        }
        if (!replaced && n < outCapacity)
            out[n++] = pe;
    }
    return n;
}

bool SetMouseProfile(uint16_t vid, uint16_t pid, uint32_t titleId,
                      int32_t sensitivity, bool invertY, int32_t deadzone,
                      uint8_t curveType, float curveExponent,
                      const char* gameName,
                      int32_t antiDeadzone,
                      int stickLayout,
                      int32_t antiDeadzoneY,
                      int32_t smoothingMs) {
    if (titleId == 0)
        return false; // 0 = "hors d'un jeu" (voir FindActiveProfile) - pas une cle de profil valide

    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }

    // Copie les profils existants du device (autres jeux, et eventuel
    // buttonMap/keyMap deja present sur celui qu'on modifie - hors scope,
    // voir mapping.h) avant d'ajouter/remplacer celui vise.
    std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
    if (existing) {
        for (uint8_t i = 0; i < existing->profileCount; i++) {
            data->profileEntries.push_back(existing->profiles[i]);
        }
    }

    HidDeviceProfile* target = nullptr;
    for (std::vector<HidDeviceProfile>::iterator it = data->profileEntries.begin(); it != data->profileEntries.end(); ++it) {
        if (it->titleId == titleId) {
            target = &(*it);
            break;
        }
    }
    if (!target) {
        HidDeviceProfile fresh = {};
        fresh.titleId = titleId;
        fresh.antiDeadzone = -1;   // herite (zero-init donnerait "0 explicite")
        fresh.antiDeadzoneY = -1;
        fresh.smoothingMs = -1;
        fresh.buttonMap = nullptr;
        fresh.buttonMapCount = 0;
        fresh.keyMap = nullptr;
        fresh.keyMapCount = 0;
        data->profileEntries.push_back(fresh);
        target = &data->profileEntries.back(); // dernier push_back de cette fonction, pointeur stable ensuite
    }

    target->mouseSensitivity = sensitivity;
    target->invertMouseY = invertY ? 1 : 0;
    target->deadzone = deadzone;
    if (antiDeadzone != -2)          // -2 = ne pas toucher (F9 depuis le jeu)
        target->antiDeadzone = antiDeadzone;
    if (stickLayout >= 0 && stickLayout < STICKS_LAYOUT_COUNT)
        target->stickLayout = (int8_t)stickLayout;
    if (antiDeadzoneY != -2)
        target->antiDeadzoneY = antiDeadzoneY;
    if (smoothingMs != -2)
        target->smoothingMs = smoothingMs > 200 ? 200 : smoothingMs;
    if (curveType == CURVE_EXPONENTIAL) {
        target->curveType = static_cast<int8_t>(CURVE_EXPONENTIAL);
        target->curveExponent = curveExponent;
    } else {
        target->curveType = -1; // pas de courbe pour ce profil - retombe sur le reglage du device
        target->curveExponent = 0.0f;
    }

    // gameName vide/nul = on garde le nom deja present sur ce profil (voir le
    // commentaire de la declaration dans mapping.h) - important pour F9, qui
    // appelle toujours cette fonction sans nom et ne doit pas effacer un nom
    // saisi depuis l'appli.
    if (gameName != nullptr && gameName[0] != '\0') {
        strncpy(target->gameName, gameName, sizeof(target->gameName) - 1);
        target->gameName[sizeof(target->gameName) - 1] = '\0';
    }

    if (existing) {
        existing->profiles = data->profileEntries.data();
        existing->profileCount = static_cast<uint8_t>(data->profileEntries.size());
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.profiles = data->profileEntries.data();
        fresh.profileCount = static_cast<uint8_t>(data->profileEntries.size());
        g_dynamicMappings.push_back(fresh);
    }

    g_dynamicData.push_back(std::move(data));

    return true;
}

// Trouve (en creant device+profil si besoin, meme pattern que SetMouseProfile
// ci-dessus) le profil vise et prepare une copie de travail de son
// buttonMap/keyMap existant - factorise entre SetProfileButtonOverride et
// SetProfileKeyOverride, qui ne different qu'apres cet appel (quel tableau
// elles modifient). Retourne nullptr (via *outTarget) si titleId == 0.
// *outData recoit le DynamicMappingData qui doit etre pousse dans
// g_dynamicData par l'appelant une fois sa modification terminee (pas fait
// ici pour laisser chaque appelant ajouter SA nouvelle entree
// profileButtonStorage/profileKeyStorage au meme objet avant de le publier).
static HidDeviceProfile* PrepareProfileOverrideTarget(uint16_t vid, uint16_t pid, uint32_t titleId,
                                                        std::unique_ptr<DynamicMappingData>* outData,
                                                        HidDeviceMapping** outExisting) {
    HidDeviceMapping* existing = nullptr;
    for (std::vector<HidDeviceMapping>::iterator it = g_dynamicMappings.begin(); it != g_dynamicMappings.end(); ++it) {
        if (it->vendorId == vid && it->productId == pid) {
            existing = &(*it);
            break;
        }
    }
    *outExisting = existing;

    std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
    if (existing) {
        for (uint8_t i = 0; i < existing->profileCount; i++) {
            data->profileEntries.push_back(existing->profiles[i]);
        }
    }

    HidDeviceProfile* target = nullptr;
    for (std::vector<HidDeviceProfile>::iterator it = data->profileEntries.begin(); it != data->profileEntries.end(); ++it) {
        if (it->titleId == titleId) {
            target = &(*it);
            break;
        }
    }
    if (!target) {
        HidDeviceProfile fresh = {};
        fresh.titleId = titleId;
        // Sentinelles "herite" explicites (voir la convention documentee sur
        // HidDeviceProfile dans mapping.h) - PAS un simple {} zero-init pour
        // invertMouseY/deadzone/curveType, dont 0 est une valeur EXPLICITE
        // valide ("pas invertee"/"0 deadzone"/"CURVE_LINEAR"), differente de
        // "herite du device". Contrairement a SetMouseProfile, cette fonction
        // ne fixe QUE le bouton/la touche visee - le reste doit rester
        // "herite", pas silencieusement fige a des valeurs qui ressemblent
        // aux defauts sans en avoir la semantique.
        fresh.mouseSensitivity = 0;   // 0 = herite (seule sentinelle qui coincide avec zero-init)
        fresh.invertMouseY = -1;
        fresh.deadzone = -1;
        fresh.antiDeadzone = -1;
        fresh.antiDeadzoneY = -1;
        fresh.smoothingMs = -1;
        fresh.curveType = -1;
        fresh.curveExponent = 0.0f;
        fresh.gameName[0] = '\0';
        fresh.buttonMap = nullptr;
        fresh.buttonMapCount = 0;
        fresh.keyMap = nullptr;
        fresh.keyMapCount = 0;
        data->profileEntries.push_back(fresh);
        target = &data->profileEntries.back();
    }

    *outData = std::move(data);
    return target;
}

// Publie data/existing dans g_dynamicMappings/g_dynamicData - dernier pas
// commun a SetProfileButtonOverride et SetProfileKeyOverride, une fois que
// chacune a fini d'ecrire dans target->buttonMap/keyMap.
static void PublishProfileOverride(uint16_t vid, uint16_t pid, HidDeviceMapping* existing,
                                    std::unique_ptr<DynamicMappingData> data) {
    if (existing) {
        existing->profiles = data->profileEntries.data();
        existing->profileCount = static_cast<uint8_t>(data->profileEntries.size());
    } else {
        HidDeviceMapping fresh = {};
        fresh.vendorId = vid;
        fresh.productId = pid;
        fresh.profiles = data->profileEntries.data();
        fresh.profileCount = static_cast<uint8_t>(data->profileEntries.size());
        g_dynamicMappings.push_back(fresh);
    }
    g_dynamicData.push_back(std::move(data));
}

bool SetProfileButtonOverride(uint16_t vid, uint16_t pid, uint32_t titleId,
                               uint8_t idx, uint8_t ButtonsReport::* field) {
    if (titleId == 0)
        return false;

    std::unique_ptr<DynamicMappingData> data;
    HidDeviceMapping* existing = nullptr;
    HidDeviceProfile* target = PrepareProfileOverrideTarget(vid, pid, titleId, &data, &existing);

    // Copie les entrees EXISTANTES du profil, sauf celle visee par idx (elle
    // est reajoutee juste apres avec la nouvelle cible) - preserve les autres
    // boutons deja lies sur ce profil (voir le meme raisonnement que la
    // copie des profils dans PrepareProfileOverrideTarget).
    auto ownedButtons = std::unique_ptr<std::vector<HidButtonMapEntry>>(new std::vector<HidButtonMapEntry>());
    for (uint8_t i = 0; i < target->buttonMapCount; i++) {
        if (target->buttonMap[i].idx != idx)
            ownedButtons->push_back(target->buttonMap[i]);
    }
    HidButtonMapEntry entry;
    entry.idx = idx;
    entry.field = field;
    ownedButtons->push_back(entry);

    target->buttonMap = ownedButtons->data();
    target->buttonMapCount = static_cast<uint8_t>(ownedButtons->size());
    data->profileButtonStorage.push_back(std::move(ownedButtons));

    PublishProfileOverride(vid, pid, existing, std::move(data));
    return true;
}

bool SetProfileKeyOverride(uint16_t vid, uint16_t pid, uint32_t titleId,
                            uint8_t code, uint8_t action) {
    if (titleId == 0)
        return false;

    std::unique_ptr<DynamicMappingData> data;
    HidDeviceMapping* existing = nullptr;
    HidDeviceProfile* target = PrepareProfileOverrideTarget(vid, pid, titleId, &data, &existing);

    auto ownedKeys = std::unique_ptr<std::vector<HidKeyMapEntry>>(new std::vector<HidKeyMapEntry>());
    for (uint8_t i = 0; i < target->keyMapCount; i++) {
        if (target->keyMap[i].code != code)
            ownedKeys->push_back(target->keyMap[i]);
    }
    HidKeyMapEntry entry;
    entry.code = code;
    entry.action = action;
    ownedKeys->push_back(entry);

    target->keyMap = ownedKeys->data();
    target->keyMapCount = static_cast<uint8_t>(ownedKeys->size());
    data->profileKeyStorage.push_back(std::move(ownedKeys));

    PublishProfileOverride(vid, pid, existing, std::move(data));
    return true;
}

int32_t ApplySensitivityCurve(int32_t scaledValue, uint8_t curveType, float curveExponent) {
    if (curveType != CURVE_EXPONENTIAL || curveExponent <= 0.0f)
        return scaledValue;

    float normalized = (float)scaledValue / 32767.0f;
    float sign = normalized < 0.0f ? -1.0f : 1.0f;
    float magnitude = fabsf(normalized);
    if (magnitude > 1.0f)
        magnitude = 1.0f;

    float curved = powf(magnitude, curveExponent);
    int32_t result = (int32_t)(sign * curved * 32767.0f);
    if (result > 32767) result = 32767;
    if (result < -32768) result = -32768;
    return result;
}

bool LoadMappingsFromJson(const std::string& jsonString) {
    using namespace rapidjson;

    Document doc;
    doc.Parse(jsonString.c_str());
    if (doc.HasParseError()) {
        return false;
    }

    if (!doc.IsArray()) {
        return false;
    }

    // Clear existing dynamic mappings
    ClearDynamicMappings();

    for (SizeType i = 0; i < doc.Size(); i++) {
        const Value& entry = doc[i];
        if (!entry.IsObject()) continue;

        // Required fields
        if (!entry.HasMember("vid") || !entry.HasMember("pid")) continue;
        if (!entry["vid"].IsUint() || !entry["pid"].IsUint()) continue;

        std::unique_ptr<DynamicMappingData> data(new DynamicMappingData());
        HidDeviceMapping mapping = {};

        mapping.vendorId = static_cast<uint16_t>(entry["vid"].GetUint());
        mapping.productId = static_cast<uint16_t>(entry["pid"].GetUint());

        // Parse axis map
        if (entry.HasMember("axes") && entry["axes"].IsArray()) {
            const Value& axes = entry["axes"];
            for (SizeType j = 0; j < axes.Size(); j++) {
                const Value& axis = axes[j];
                if (!axis.IsObject()) continue;
                if (!axis.HasMember("usage") || !axis.HasMember("field")) continue;
                // Types verifies AVANT lecture (AUDIT_2026-09.md, I5) : sur un
                // mauvais type, rapidjson renvoie une valeur incoherente, et
                // GetString() un pointeur invalide passe ensuite a strcmp -
                // plantage du plugin DANS le processus du tableau de bord,
                // des le demarrage. Une entree mal typee est simplement ignoree.
                if (!axis["usage"].IsUint() || !axis["field"].IsString()) continue;

                HidAxisMapEntry ae = {};
                ae.usage = static_cast<uint16_t>(axis["usage"].GetUint());

                const char* fieldName = axis["field"].GetString();
                ae.field = GetAxisFieldPtr(fieldName);

                if (ae.field != nullptr) {
                    data->axisEntries.push_back(ae);
                }
            }
        }

        // Parse button map
        if (entry.HasMember("buttons") && entry["buttons"].IsArray()) {
            const Value& buttons = entry["buttons"];
            for (SizeType j = 0; j < buttons.Size(); j++) {
                const Value& btn = buttons[j];
                if (!btn.IsObject()) continue;
                if (!btn.HasMember("idx") || !btn.HasMember("field")) continue;
                if (!btn["idx"].IsUint() || !btn["field"].IsString()) continue; // AUDIT I5

                HidButtonMapEntry be = {};
                be.idx = static_cast<uint8_t>(btn["idx"].GetUint());

                const char* fieldName = btn["field"].GetString();
                be.field = GetButtonFieldPtr(fieldName);

                if (be.field != nullptr) {
                    data->buttonEntries.push_back(be);
                }
            }
        }

        // Parse keyboard layout: [ { "code": 26, "action": "lstick_up" }, ... ]
        // "code" is the HID usage code of the key (see HidKeyMapEntry).
        if (entry.HasMember("keys") && entry["keys"].IsArray()) {
            const Value& keys = entry["keys"];
            for (SizeType j = 0; j < keys.Size(); j++) {
                const Value& k = keys[j];
                if (!k.IsObject()) continue;
                if (!k.HasMember("code") || !k.HasMember("action")) continue;
                if (!k["code"].IsUint() || !k["action"].IsString()) continue;

                HidKeyMapEntry ke = {};
                ke.code = static_cast<uint8_t>(k["code"].GetUint());
                ke.action = KeyActionFromName(k["action"].GetString());

                if (ke.action != KEYACT_NONE) {
                    data->keyEntries.push_back(ke);
                }
            }
        }

        // Parse invert flags
        if (entry.HasMember("invert") && entry["invert"].IsObject()) {
            const Value& inv = entry["invert"];
            // IsBool() en plus de HasMember (AUDIT I5) - "true" ecrit entre
            // guillemets par une edition manuelle ne doit rien faire planter.
            if (inv.HasMember("x")  && inv["x"].IsBool())  mapping.invert.invertX = inv["x"].GetBool();
            if (inv.HasMember("y")  && inv["y"].IsBool())  mapping.invert.invertY = inv["y"].GetBool();
            if (inv.HasMember("z")  && inv["z"].IsBool())  mapping.invert.invertZ = inv["z"].GetBool();
            if (inv.HasMember("rx") && inv["rx"].IsBool()) mapping.invert.invertRX = inv["rx"].GetBool();
            if (inv.HasMember("ry") && inv["ry"].IsBool()) mapping.invert.invertRY = inv["ry"].GetBool();
            if (inv.HasMember("rz") && inv["rz"].IsBool()) mapping.invert.invertRZ = inv["rz"].GetBool();
        }

        // Parse optional mouse tuning (only meaningful for devices classified
        // as relative-motion HID devices at runtime - see DetectIsMouse()).
        mapping.mouseSensitivity = 0;
        mapping.invertMouseY = false;
        if (entry.HasMember("mouseSensitivity") && entry["mouseSensitivity"].IsInt()) {
            mapping.mouseSensitivity = entry["mouseSensitivity"].GetInt();
        }
        if (entry.HasMember("invertMouseY") && entry["invertMouseY"].IsBool()) {
            mapping.invertMouseY = entry["invertMouseY"].GetBool();
        }

        // Deadzone / sensitivity curve for the mouse-driven stick axis.
        mapping.deadzone = 0;
        if (entry.HasMember("deadzone") && entry["deadzone"].IsInt()) {
            mapping.deadzone = entry["deadzone"].GetInt();
        }

        mapping.sensitivityCurveType = CURVE_LINEAR;
        mapping.sensitivityCurveExponent = 1.0f;
        if (entry.HasMember("sensitivityCurve") && entry["sensitivityCurve"].IsObject()) {
            const Value& curve = entry["sensitivityCurve"];
            if (curve.HasMember("type") && curve["type"].IsString()) {
                mapping.sensitivityCurveType = CurveTypeFromName(curve["type"].GetString());
            }
            if (curve.HasMember("exponent") && curve["exponent"].IsNumber()) {
                mapping.sensitivityCurveExponent = curve["exponent"].GetFloat();
            }
        }

        // Disposition de rapport apprise par calibration (voir
        // HidReportLayout dans mapping.h). Absente pour tout device jamais
        // calibre -> valid reste false et le plugin garde son comportement
        // historique. Relue integralement puis revalidee par IsLayoutUsable
        // cote appelant : un fichier edite a la main avec des offsets
        // incoherents ne doit pas faire lire le plugin hors du paquet.
        memset(&mapping.reportLayout, 0, sizeof(mapping.reportLayout));
        mapping.reportLayout.buttonsOffset = REPORT_FIELD_ABSENT;
        mapping.reportLayout.xOffset = REPORT_FIELD_ABSENT;
        mapping.reportLayout.yOffset = REPORT_FIELD_ABSENT;
        mapping.reportLayout.wheelOffset = REPORT_FIELD_ABSENT;
        if (entry.HasMember("reportLayout") && entry["reportLayout"].IsObject()) {
            const Value& rl = entry["reportLayout"];
            if (rl.HasMember("size") && rl["size"].IsUint())
                mapping.reportLayout.reportSize = (uint8_t)rl["size"].GetUint();
            if (rl.HasMember("buttonsOffset") && rl["buttonsOffset"].IsUint())
                mapping.reportLayout.buttonsOffset = (uint8_t)rl["buttonsOffset"].GetUint();
            if (rl.HasMember("xOffset") && rl["xOffset"].IsUint())
                mapping.reportLayout.xOffset = (uint8_t)rl["xOffset"].GetUint();
            if (rl.HasMember("xSize") && rl["xSize"].IsUint())
                mapping.reportLayout.xSize = (uint8_t)rl["xSize"].GetUint();
            if (rl.HasMember("yOffset") && rl["yOffset"].IsUint())
                mapping.reportLayout.yOffset = (uint8_t)rl["yOffset"].GetUint();
            if (rl.HasMember("ySize") && rl["ySize"].IsUint())
                mapping.reportLayout.ySize = (uint8_t)rl["ySize"].GetUint();
            if (rl.HasMember("wheelOffset") && rl["wheelOffset"].IsUint())
                mapping.reportLayout.wheelOffset = (uint8_t)rl["wheelOffset"].GetUint();
            if (rl.HasMember("wheelSize") && rl["wheelSize"].IsUint())
                mapping.reportLayout.wheelSize = (uint8_t)rl["wheelSize"].GetUint();
            mapping.reportLayout.valid = IsLayoutUsable(mapping.reportLayout);
        }

        // Resolution mesuree (voir HidDeviceMapping::countsPerCm). Bornes
        // larges (50 a 400 DPI... 25 000 DPI) : une valeur hors bornes vient
        // d'une edition manuelle ou d'une mesure ratee, on l'ignore plutot
        // que de multiplier la sensibilite par un facteur absurde.
        mapping.countsPerCm = 0.0f;
        if (entry.HasMember("countsPerCm") && entry["countsPerCm"].IsNumber()) {
            const float cpc = entry["countsPerCm"].GetFloat();
            if (cpc >= 20.0f && cpc <= 10000.0f)
                mapping.countsPerCm = cpc;
        }

        // Anti-zone morte (voir HidDeviceMapping::antiDeadzone), bornee.
        mapping.antiDeadzone = 0;
        if (entry.HasMember("antiDeadzone") && entry["antiDeadzone"].IsInt()) {
            const int ad = entry["antiDeadzone"].GetInt();
            if (ad > 0 && ad <= 32767) mapping.antiDeadzone = ad;
        }
        mapping.antiDeadzoneY = 0;   // 0 = identique a l'horizontal
        if (entry.HasMember("antiDeadzoneY") && entry["antiDeadzoneY"].IsInt()) {
            const int ad = entry["antiDeadzoneY"].GetInt();
            if (ad > 0 && ad <= 32767) mapping.antiDeadzoneY = ad;
        }
        mapping.smoothingMs = 0;
        if (entry.HasMember("smoothingMs") && entry["smoothingMs"].IsInt()) {
            const int sm = entry["smoothingMs"].GetInt();
            if (sm > 0 && sm <= 200) mapping.smoothingMs = sm;
        }

        // Per-game profiles: [ { "titleId": ..., "mouseSensitivity": ...,
        // "deadzone": ..., "sensitivityCurve": {...}, "buttons": [...],
        // "keys": [...] }, ... ]. Every field besides titleId is optional and
        // falls back to the device's base settings above when absent - see
        // HidDeviceProfile's sentinel convention in mapping.h.
        if (entry.HasMember("profiles") && entry["profiles"].IsArray()) {
            const Value& profiles = entry["profiles"];
            for (SizeType j = 0; j < profiles.Size(); j++) {
                const Value& p = profiles[j];
                if (!p.IsObject() || !p.HasMember("titleId") || !p["titleId"].IsUint())
                    continue;

                HidDeviceProfile profile = {};
                profile.titleId = p["titleId"].GetUint();

                profile.gameName[0] = '\0';
                if (p.HasMember("gameName") && p["gameName"].IsString()) {
                    strncpy(profile.gameName, p["gameName"].GetString(), sizeof(profile.gameName) - 1);
                    profile.gameName[sizeof(profile.gameName) - 1] = '\0';
                }

                profile.mouseSensitivity = 0;
                if (p.HasMember("mouseSensitivity") && p["mouseSensitivity"].IsInt()) {
                    profile.mouseSensitivity = p["mouseSensitivity"].GetInt();
                }

                profile.invertMouseY = -1;
                if (p.HasMember("invertMouseY") && p["invertMouseY"].IsBool()) {
                    profile.invertMouseY = p["invertMouseY"].GetBool() ? 1 : 0;
                }

                profile.deadzone = -1;
                if (p.HasMember("deadzone") && p["deadzone"].IsInt()) {
                    profile.deadzone = p["deadzone"].GetInt();
                }

                profile.stickLayout = 0;
                if (p.HasMember("stickLayout") && p["stickLayout"].IsInt()) {
                    const int sl = p["stickLayout"].GetInt();
                    if (sl > 0 && sl < STICKS_LAYOUT_COUNT) profile.stickLayout = (int8_t)sl;
                }

                profile.antiDeadzone = -1;
                if (p.HasMember("antiDeadzone") && p["antiDeadzone"].IsInt()) {
                    const int ad = p["antiDeadzone"].GetInt();
                    if (ad >= 0 && ad <= 32767) profile.antiDeadzone = ad;
                }
                profile.antiDeadzoneY = -1;
                if (p.HasMember("antiDeadzoneY") && p["antiDeadzoneY"].IsInt()) {
                    const int ad = p["antiDeadzoneY"].GetInt();
                    if (ad >= 0 && ad <= 32767) profile.antiDeadzoneY = ad;
                }
                profile.smoothingMs = -1;
                if (p.HasMember("smoothingMs") && p["smoothingMs"].IsInt()) {
                    const int sm = p["smoothingMs"].GetInt();
                    if (sm >= 0 && sm <= 200) profile.smoothingMs = sm;
                }

                profile.curveType = -1;
                profile.curveExponent = 0.0f;
                if (p.HasMember("sensitivityCurve") && p["sensitivityCurve"].IsObject()) {
                    const Value& curve = p["sensitivityCurve"];
                    if (curve.HasMember("type") && curve["type"].IsString()) {
                        profile.curveType = static_cast<int8_t>(CurveTypeFromName(curve["type"].GetString()));
                    }
                    if (curve.HasMember("exponent") && curve["exponent"].IsNumber()) {
                        profile.curveExponent = curve["exponent"].GetFloat();
                    }
                }

                profile.buttonMap = nullptr;
                profile.buttonMapCount = 0;
                if (p.HasMember("buttons") && p["buttons"].IsArray()) {
                    auto ownedButtons = std::unique_ptr<std::vector<HidButtonMapEntry>>(new std::vector<HidButtonMapEntry>());
                    const Value& buttons = p["buttons"];
                    for (SizeType k = 0; k < buttons.Size(); k++) {
                        const Value& btn = buttons[k];
                        if (!btn.IsObject() || !btn.HasMember("idx") || !btn.HasMember("field")) continue;
                        if (!btn["idx"].IsUint() || !btn["field"].IsString()) continue; // AUDIT I5
                        HidButtonMapEntry be = {};
                        be.idx = static_cast<uint8_t>(btn["idx"].GetUint());
                        be.field = GetButtonFieldPtr(btn["field"].GetString());
                        if (be.field != nullptr) ownedButtons->push_back(be);
                    }
                    if (!ownedButtons->empty()) {
                        profile.buttonMap = ownedButtons->data();
                        profile.buttonMapCount = static_cast<uint8_t>(ownedButtons->size());
                    }
                    data->profileButtonStorage.push_back(std::move(ownedButtons));
                }

                profile.keyMap = nullptr;
                profile.keyMapCount = 0;
                if (p.HasMember("keys") && p["keys"].IsArray()) {
                    auto ownedKeys = std::unique_ptr<std::vector<HidKeyMapEntry>>(new std::vector<HidKeyMapEntry>());
                    const Value& keys = p["keys"];
                    for (SizeType k = 0; k < keys.Size(); k++) {
                        const Value& kv = keys[k];
                        if (!kv.IsObject() || !kv.HasMember("code") || !kv.HasMember("action")) continue;
                        if (!kv["code"].IsUint() || !kv["action"].IsString()) continue;
                        HidKeyMapEntry ke = {};
                        ke.code = static_cast<uint8_t>(kv["code"].GetUint());
                        ke.action = KeyActionFromName(kv["action"].GetString());
                        if (ke.action != KEYACT_NONE) ownedKeys->push_back(ke);
                    }
                    if (!ownedKeys->empty()) {
                        profile.keyMap = ownedKeys->data();
                        profile.keyMapCount = static_cast<uint8_t>(ownedKeys->size());
                    }
                    data->profileKeyStorage.push_back(std::move(ownedKeys));
                }

                data->profileEntries.push_back(profile);
            }
        }
        if (!data->profileEntries.empty()) {
            mapping.profiles = data->profileEntries.data();
            mapping.profileCount = static_cast<uint8_t>(data->profileEntries.size());
        }

        // Finalize mapping pointers
        if (!data->axisEntries.empty()) {
            mapping.axisMap = data->axisEntries.data();
            mapping.axisMapCount = static_cast<uint8_t>(data->axisEntries.size());
        }
        if (!data->buttonEntries.empty()) {
            mapping.buttonMap = data->buttonEntries.data();
            mapping.buttonMapCount = static_cast<uint8_t>(data->buttonEntries.size());
        }
        if (!data->keyEntries.empty()) {
            mapping.keyMap = data->keyEntries.data();
            mapping.keyMapCount = static_cast<uint8_t>(data->keyEntries.size());
        }

        g_dynamicData.push_back(std::move(data));
        g_dynamicMappings.push_back(mapping);
    }

    return true;
}

bool LoadMappingsFromFile(const std::string& filepath) {
    using namespace rapidjson;

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string jsonContent = ss.str();

    file.close();

    return LoadMappingsFromJson(jsonContent);
}

std::string SaveMappingsToJson() {
    using namespace rapidjson;

    Document doc(kArrayType);
    Document::AllocatorType& alloc = doc.GetAllocator();

    // Save dynamic mappings
    for (int j = 0; j < g_dynamicMappings.size(); j++) {
        const auto& m = g_dynamicMappings[j];

        Value entry(kObjectType);
        entry.AddMember("vid", m.vendorId, alloc);
        entry.AddMember("pid", m.productId, alloc);

        // Axes
        Value axes(kArrayType);
        for (uint8_t i = 0; i < m.axisMapCount; i++) {
            const auto& ae = m.axisMap[i];
            Value axis(kObjectType);
            axis.AddMember("usage", ae.usage, alloc);

            const char* name = GetAxisFieldName(ae.field);
            if (name) {
                axis.AddMember("field", StringRef(name), alloc);
                axes.PushBack(axis, alloc);
            }
        }
        if (!axes.Empty()) {
            entry.AddMember("axes", axes, alloc);
        }

        // Buttons
        Value buttons(kArrayType);
        for (uint8_t i = 0; i < m.buttonMapCount; i++) {
            const auto& be = m.buttonMap[i];
            Value btn(kObjectType);
            btn.AddMember("idx", be.idx, alloc);

            const char* name = GetButtonFieldName(be.field);
            if (name) {
                btn.AddMember("field", StringRef(name), alloc);
                buttons.PushBack(btn, alloc);
            }
        }
        if (!buttons.Empty()) {
            entry.AddMember("buttons", buttons, alloc);
        }

        // Keyboard layout - was previously never written back out, so any
        // keyboard mapping produced dynamically (e.g. by a future config
        // assistant) would be silently lost on the next save. See
        // LoadMappingsFromJson's "keys" parsing, which this mirrors.
        Value keys(kArrayType);
        for (uint8_t i = 0; i < m.keyMapCount; i++) {
            const auto& ke = m.keyMap[i];
            const char* name = KeyActionToName(ke.action);
            if (name) {
                Value key(kObjectType);
                key.AddMember("code", ke.code, alloc);
                key.AddMember("action", StringRef(name), alloc);
                keys.PushBack(key, alloc);
            }
        }
        if (!keys.Empty()) {
            entry.AddMember("keys", keys, alloc);
        }

        // Invert flags
        Value inv(kObjectType);
        inv.AddMember("x", m.invert.invertX, alloc);
        inv.AddMember("y", m.invert.invertY, alloc);
        inv.AddMember("z", m.invert.invertZ, alloc);
        inv.AddMember("rx", m.invert.invertRX, alloc);
        inv.AddMember("ry", m.invert.invertRY, alloc);
        inv.AddMember("rz", m.invert.invertRZ, alloc);
        entry.AddMember("invert", inv, alloc);

        // Mouse tuning - only written when set to a non-default value, so
        // regular gamepad entries in X360Remap.json stay unchanged.
        if (m.mouseSensitivity != 0) {
            entry.AddMember("mouseSensitivity", m.mouseSensitivity, alloc);
        }
        if (m.invertMouseY) {
            entry.AddMember("invertMouseY", m.invertMouseY, alloc);
        }
        if (m.deadzone != 0) {
            entry.AddMember("deadzone", m.deadzone, alloc);
        }
        if (m.sensitivityCurveType != CURVE_LINEAR || m.sensitivityCurveExponent != 1.0f) {
            Value curve(kObjectType);
            curve.AddMember("type", StringRef(CurveTypeToName(m.sensitivityCurveType)), alloc);
            curve.AddMember("exponent", m.sensitivityCurveExponent, alloc);
            entry.AddMember("sensitivityCurve", curve, alloc);
        }

        // Disposition apprise par calibration - ecrite seulement si elle est
        // valide, pour ne pas polluer le fichier des devices jamais calibres
        // (et garder X360Remap.json lisible a la main).
        if (m.reportLayout.valid) {
            Value rl(kObjectType);
            rl.AddMember("size", m.reportLayout.reportSize, alloc);
            rl.AddMember("buttonsOffset", m.reportLayout.buttonsOffset, alloc);
            rl.AddMember("xOffset", m.reportLayout.xOffset, alloc);
            rl.AddMember("xSize", m.reportLayout.xSize, alloc);
            rl.AddMember("yOffset", m.reportLayout.yOffset, alloc);
            rl.AddMember("ySize", m.reportLayout.ySize, alloc);
            rl.AddMember("wheelOffset", m.reportLayout.wheelOffset, alloc);
            rl.AddMember("wheelSize", m.reportLayout.wheelSize, alloc);
            entry.AddMember("reportLayout", rl, alloc);
        }

        if (m.countsPerCm > 0.0f) {
            entry.AddMember("countsPerCm", m.countsPerCm, alloc);
        }
        if (m.antiDeadzone > 0) {
            entry.AddMember("antiDeadzone", m.antiDeadzone, alloc);
        }
        if (m.antiDeadzoneY > 0) {
            entry.AddMember("antiDeadzoneY", m.antiDeadzoneY, alloc);
        }
        if (m.smoothingMs > 0) {
            entry.AddMember("smoothingMs", m.smoothingMs, alloc);
        }

        // Per-game profiles - see HidDeviceProfile's sentinel convention in
        // mapping.h for why some fields check ">= 0" instead of "!= 0".
        if (m.profileCount > 0) {
            Value profiles(kArrayType);
            for (uint8_t i = 0; i < m.profileCount; i++) {
                const auto& p = m.profiles[i];
                Value prof(kObjectType);
                prof.AddMember("titleId", p.titleId, alloc);
                if (p.gameName[0] != '\0') {
                    // Value(const char*, Allocator&) copies into the document's
                    // own allocator - required here since p.gameName is a
                    // stack/vector-owned buffer that won't outlive this loop.
                    prof.AddMember("gameName", Value(p.gameName, alloc), alloc);
                }
                if (p.mouseSensitivity != 0) {
                    prof.AddMember("mouseSensitivity", p.mouseSensitivity, alloc);
                }
                if (p.invertMouseY >= 0) {
                    prof.AddMember("invertMouseY", p.invertMouseY != 0, alloc);
                }
                if (p.deadzone >= 0) {
                    prof.AddMember("deadzone", p.deadzone, alloc);
                }
                if (p.antiDeadzone >= 0) {
                    prof.AddMember("antiDeadzone", p.antiDeadzone, alloc);
                }
                if (p.antiDeadzoneY >= 0) {
                    prof.AddMember("antiDeadzoneY", p.antiDeadzoneY, alloc);
                }
                if (p.smoothingMs >= 0) {
                    prof.AddMember("smoothingMs", p.smoothingMs, alloc);
                }
                if (p.stickLayout > 0) {
                    prof.AddMember("stickLayout", (int)p.stickLayout, alloc);
                }
                if (p.curveType >= 0) {
                    Value curve(kObjectType);
                    curve.AddMember("type", StringRef(CurveTypeToName(static_cast<uint8_t>(p.curveType))), alloc);
                    curve.AddMember("exponent", p.curveExponent, alloc);
                    prof.AddMember("sensitivityCurve", curve, alloc);
                }
                if (p.buttonMapCount > 0) {
                    Value buttons(kArrayType);
                    for (uint8_t j = 0; j < p.buttonMapCount; j++) {
                        const char* name = GetButtonFieldName(p.buttonMap[j].field);
                        if (name) {
                            Value btn(kObjectType);
                            btn.AddMember("idx", p.buttonMap[j].idx, alloc);
                            btn.AddMember("field", StringRef(name), alloc);
                            buttons.PushBack(btn, alloc);
                        }
                    }
                    if (!buttons.Empty()) {
                        prof.AddMember("buttons", buttons, alloc);
                    }
                }
                if (p.keyMapCount > 0) {
                    Value keys(kArrayType);
                    for (uint8_t j = 0; j < p.keyMapCount; j++) {
                        const char* name = KeyActionToName(p.keyMap[j].action);
                        if (name) {
                            Value key(kObjectType);
                            key.AddMember("code", p.keyMap[j].code, alloc);
                            key.AddMember("action", StringRef(name), alloc);
                            keys.PushBack(key, alloc);
                        }
                    }
                    if (!keys.Empty()) {
                        prof.AddMember("keys", keys, alloc);
                    }
                }
                profiles.PushBack(prof, alloc);
            }
            entry.AddMember("profiles", profiles, alloc);
        }

        doc.PushBack(entry, alloc);
    }

    StringBuffer buffer;
    PrettyWriter<StringBuffer> writer(buffer);
    writer.SetIndent(' ', 2);
    doc.Accept(writer);

    return buffer.GetString();
}

// Ecriture directe, telle qu'elle existait avant le 2026-09-23 - conservee
// comme REPLI si le renommage n'est pas pris en charge par la plateforme.
static bool WriteJsonDirect(const std::string& filepath, const std::string& json) {
    std::ofstream file(filepath.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    file.flush();
    bool ok = file.good();
    file.close();
    return ok;
}

static bool FileExists(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.is_open();
}

static std::string ReadWholeFile(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open())
        return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// --- Sauvegarde : REVUE LE 2026-09-23 apres une perte de configuration ------
//
// La premiere version (meme jour) ecrivait un .tmp, SUPPRIMAIT l'original,
// puis renommait le .tmp. Retour HB le soir meme : au redemarrage, le plugin
// annoncait "installation incomplete" - X360Remap.json avait disparu.
//
// Deux defauts de cette version, tous deux de mon fait :
//   1. Le plugin ouvre ce fichier toutes les 2 s (horodatage du rechargement
//      a chaud), sans partage en suppression. Supprimer puis renommer un
//      fichier qu'un autre programme tient ouvert est exactement le cas ou
//      la suppression reste "en attente" et ou le renommage echoue.
//   2. En cas d'echec, le repli supprimait AUSSI le .tmp - meme quand
//      l'ecriture de secours avait echoue. Plus aucune copie ne restait.
//
// Nouvelle regle, sans suppression ni renommage de l'original :
//   - le .tmp est ecrit en premier : c'est une COPIE DE SECOURS du nouveau
//     contenu ;
//   - l'original est ensuite reecrit directement, comme avant le 2026-09-23 ;
//   - le .tmp n'est supprime QUE si cette ecriture a reussi.
// Un lecteur peut encore tomber sur un fichier en cours d'ecriture ; c'est
// couvert cote plugin (nouvel essai 2 s plus tard, notification seulement au
// deuxieme echec). Et si l'original est perdu ou casse, RecoverConfigFromTmp
// le reconstruit depuis la copie de secours.
bool SaveMappingsToFile(const std::string& filepath) {
    std::string json = SaveMappingsToJson();
    if (json.empty())
        return false;

    const std::string tmpPath = filepath + ".tmp";
    const bool backupOk = WriteJsonDirect(tmpPath, json);

    const bool ok = WriteJsonDirect(filepath, json);
    if (ok && backupOk)
        std::remove(tmpPath.c_str());
    return ok;
}

bool RecoverConfigFromTmp(const std::string& filepath) {
    const std::string tmpPath = filepath + ".tmp";
    if (!FileExists(tmpPath))
        return false;

    const std::string backup = ReadWholeFile(tmpPath);
    if (backup.empty())
        return false;

    // La copie de secours doit etre un JSON valide ET un tableau (le format
    // du fichier) - sinon on n'y touche pas.
    {
        rapidjson::Document probe;
        probe.Parse(backup.c_str());
        if (probe.HasParseError() || !probe.IsArray())
            return false;
    }

    if (FileExists(filepath)) {
        // Original present : on ne le remplace QUE s'il est illisible.
        rapidjson::Document current;
        const std::string content = ReadWholeFile(filepath);
        current.Parse(content.c_str());
        if (!current.HasParseError() && current.IsArray()) {
            // Original sain : la copie de secours est simplement perimee.
            std::remove(tmpPath.c_str());
            return false;
        }
    }

    if (!WriteJsonDirect(filepath, backup))
        return false;       // on garde le .tmp : c'est la seule copie
    std::remove(tmpPath.c_str());
    return true;
}

bool LoadMappingsForUpdate(const std::string& filepath) {
    // Original perdu ou casse mais copie de secours valide : on repare d'abord.
    RecoverConfigFromTmp(filepath);

    if (!FileExists(filepath))
        return true;   // vrai premier lancement : on part de l'etat en memoire

    // Present : il DOIT se lire. Sinon false, et l'appelant ne sauvegarde pas.
    return LoadMappingsFromFile(filepath);
}

// Generation precedente, gardee en vie jusqu'au prochain ClearDynamicMappings
// (voir le commentaire de la declaration dans mapping.h, AUDIT C4).
static std::vector<HidDeviceMapping> s_retiredMappings;
static std::vector<std::unique_ptr<DynamicMappingData>> s_retiredData;

void ClearDynamicMappings() {
    // 1. L'AVANT-derniere generation est liberee maintenant : plus aucun
    //    pointeur ne peut y mener (elle a ete remplacee depuis au moins un
    //    cycle complet de detachement/re-resolution cote plugin).
    std::vector<HidDeviceMapping>().swap(s_retiredMappings);
    s_retiredData.clear();

    // 2. La generation courante est RETIREE, pas liberee : swap deplace la
    //    propriete des tampons sans les deplacer en memoire, donc tout
    //    pointeur Controller::map lu juste avant reste valide.
    s_retiredMappings.swap(g_dynamicMappings);
    s_retiredData.swap(g_dynamicData);
}
