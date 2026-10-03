#include "known_titles.h"
#include <sstream>
#include <cstdlib>
#include <cstdio>

std::vector<uint32_t> ParseKnownTitleIds(const std::string& content) {
    std::vector<uint32_t> result;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        // Tolere un '\r' final (fichier ecrit/relu entre outils differents) -
        // strtoul s'arrete de toute facon au premier caractere non-hex, donc
        // ce n'est pas strictement necessaire, mais explicite vaut mieux
        // qu'implicite ici.
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) {
            line.erase(line.size() - 1);
        }
        if (line.empty())
            continue;

        char* endPtr = nullptr;
        unsigned long value = std::strtoul(line.c_str(), &endPtr, 16);
        if (endPtr == line.c_str())
            continue; // ligne qui ne commence pas par un chiffre hex - ignoree, pas fatale

        result.push_back(static_cast<uint32_t>(value));
    }

    return result;
}

std::string SerializeKnownTitleIds(const std::vector<uint32_t>& titleIds) {
    std::string out;
    char buf[16];
    for (std::vector<uint32_t>::const_iterator it = titleIds.begin(); it != titleIds.end(); ++it) {
        std::sprintf(buf, "%08X\n", (unsigned int)*it);
        out += buf;
    }
    return out;
}

bool AddKnownTitleId(std::vector<uint32_t>* titleIds, uint32_t titleId) {
    if (!titleIds || titleId == 0)
        return false;

    for (std::vector<uint32_t>::const_iterator it = titleIds->begin(); it != titleIds->end(); ++it) {
        if (*it == titleId)
            return false; // deja present, rien a faire
    }

    titleIds->push_back(titleId);
    return true;
}
