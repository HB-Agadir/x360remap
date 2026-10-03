#include "input_state_reader.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

// Meme garde que X360Remap/mapping.h (voir ce fichier pour l'explication
// complete) - necessaire ICI AUSSI car ce .cpp inclut rapidjson
// directement, independamment de mapping.h. Erreur reelle obtenue sans ce
// garde au premier build Xbox : "fatal error C1189: Unknown machine
// endianess detected" (rapidjson n'arrive pas a autodetecter sur ce
// toolset).
#ifndef RAPIDJSON_ENDIAN
#ifdef _XBOX
#define RAPIDJSON_ENDIAN RAPIDJSON_BIGENDIAN
#endif
#endif
#include <rapidjson/document.h>

static const char* kDefaultInputStatePath = "HDD:\\X360RemapStudio\\input_state.json";

InputStateSnapshot ReadInputState() {
    return ReadInputState(kDefaultInputStatePath);
}

InputStateSnapshot ReadInputState(const char* path) {
    InputStateSnapshot snap;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return snap; // valid stays false

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();

    rapidjson::Document doc;
    doc.Parse(content.c_str());
    if (doc.HasParseError() || !doc.IsObject())
        return snap;

    if (doc.HasMember("keyCode") && doc["keyCode"].IsInt())
        snap.keyCode = doc["keyCode"].GetInt();
    if (doc.HasMember("mouseButton") && doc["mouseButton"].IsInt())
        snap.mouseButton = doc["mouseButton"].GetInt();
    if (doc.HasMember("mouseDX") && doc["mouseDX"].IsInt())
        snap.mouseDX = doc["mouseDX"].GetInt();
    if (doc.HasMember("mouseDY") && doc["mouseDY"].IsInt())
        snap.mouseDY = doc["mouseDY"].GetInt();
    if (doc.HasMember("mouseButtons") && doc["mouseButtons"].IsInt())
        snap.mouseButtonsMask = doc["mouseButtons"].GetInt();
    // Absent des fichiers ecrits par une version anterieure du plugin ->
    // wheelDelta reste a 0, comportement identique a avant ce champ (pas de
    // defilement a la molette, mais rien ne casse).
    if (doc.HasMember("wheelDelta") && doc["wheelDelta"].IsInt())
        snap.wheelDelta = doc["wheelDelta"].GetInt();
    // Octets bruts, transmis en hexadecimal continu ("00ff0a00"). Absents des
    // fichiers ecrits par une version anterieure du plugin -> rawLen reste a
    // 0 et l'assistant de calibration affiche simplement "en attente de
    // donnees", plutot que de faire echouer toute la lecture.
    if (doc.HasMember("raw") && doc["raw"].IsString()) {
        const char* hex = doc["raw"].GetString();
        uint8_t n = 0;
        for (int i = 0; hex[i] && hex[i + 1] && n < 16; i += 2) {
            int hi = -1, lo = -1;
            char c = hex[i];
            if (c >= '0' && c <= '9') hi = c - '0';
            else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
            c = hex[i + 1];
            if (c >= '0' && c <= '9') lo = c - '0';
            else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
            if (hi < 0 || lo < 0) break; // caractere invalide : on garde ce qui precede
            snap.raw[n++] = (uint8_t)((hi << 4) | lo);
        }
        snap.rawLen = n;
    }
    // rawLen annonce par le plugin, borne par ce qu'on a reellement decode -
    // les deux doivent concorder, mais on ne fait jamais confiance au champ
    // seul pour dimensionner une lecture.
    if (doc.HasMember("rawLen") && doc["rawLen"].IsInt()) {
        int declared = doc["rawLen"].GetInt();
        if (declared >= 0 && declared < snap.rawLen)
            snap.rawLen = (uint8_t)declared;
    }

    // "k:046d:c33a;m:1532:0067" - voir FormatConnectedDevices (X360Remap/main.cpp).
    if (doc.HasMember("devs") && doc["devs"].IsString()) {
        snap.hasDevs = true;
        const char* d = doc["devs"].GetString();
        while (*d && snap.devCount < 4) {
            unsigned int v = 0, p = 0;
            char t = 0;
            if (sscanf(d, "%c:%4x:%4x", &t, &v, &p) == 3 && (t == 'k' || t == 'm')) {
                snap.devType[snap.devCount] = t;
                snap.devVid[snap.devCount]  = (uint16_t)v;
                snap.devPid[snap.devCount]  = (uint16_t)p;
                snap.devCount++;
            }
            const char* next = strchr(d, ';');
            if (!next) break;
            d = next + 1;
        }
    }

    if (doc.HasMember("tick") && doc["tick"].IsUint())
        snap.tick = doc["tick"].GetUint();
    if (doc.HasMember("rawSeq") && doc["rawSeq"].IsUint()) {
        snap.rawSeq = doc["rawSeq"].GetUint();
        snap.hasRawSeq = true;
    }

    snap.valid = true;
    return snap;
}
