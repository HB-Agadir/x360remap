#include "known_devices.h"
#include <sstream>
#include <cstdlib>
#include <cstdio>

std::vector<KnownDeviceEntry> ParseKnownDevices(const std::string& content) {
    std::vector<KnownDeviceEntry> result;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        // Tolere un '\r' final (fichier ecrit/relu entre outils differents) -
        // meme precaution que ParseKnownTitleIds.
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\n')) {
            line.erase(line.size() - 1);
        }
        if (line.empty())
            continue;

        size_t sep1 = line.find(':');
        if (sep1 == std::string::npos)
            continue; // ligne malformee - ignoree, pas fatale (un fichier partiellement corrompu ne doit pas faire perdre tout l'historique)
        size_t sep2 = line.find(':', sep1 + 1);
        if (sep2 == std::string::npos)
            continue;

        // Le nom est OPTIONNEL et arrive apres un 3e separateur. Les fichiers
        // ecrits par une version anterieure du plugin n'en ont pas : ils
        // restent parfaitement lisibles, le nom vaut alors chaine vide et
        // l'affichage retombe sur le VID:PID.
        size_t sep3 = line.find(':', sep2 + 1);

        std::string vidStr = line.substr(0, sep1);
        std::string pidStr = line.substr(sep1 + 1, sep2 - sep1 - 1);
        std::string typeStr = (sep3 == std::string::npos)
            ? line.substr(sep2 + 1)
            : line.substr(sep2 + 1, sep3 - sep2 - 1);
        std::string nameStr = (sep3 == std::string::npos)
            ? std::string()
            : line.substr(sep3 + 1);

        char* endPtr = nullptr;
        unsigned long vid = std::strtoul(vidStr.c_str(), &endPtr, 16);
        if (vidStr.empty() || endPtr != vidStr.c_str() + vidStr.size())
            continue;

        endPtr = nullptr;
        unsigned long pid = std::strtoul(pidStr.c_str(), &endPtr, 16);
        if (pidStr.empty() || endPtr != pidStr.c_str() + pidStr.size())
            continue;

        if (typeStr != "M" && typeStr != "K")
            continue;

        KnownDeviceEntry entry;
        entry.vid = static_cast<uint16_t>(vid);
        entry.pid = static_cast<uint16_t>(pid);
        entry.isMouse = (typeStr == "M");
        size_t n = nameStr.size();
        if (n > sizeof(entry.name) - 1)
            n = sizeof(entry.name) - 1;
        for (size_t k = 0; k < n; k++)
            entry.name[k] = nameStr[k];
        entry.name[n] = '\0';
        result.push_back(entry);
    }

    return result;
}

std::string SerializeKnownDevices(const std::vector<KnownDeviceEntry>& devices) {
    std::string out;
    char buf[24];
    for (std::vector<KnownDeviceEntry>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        std::sprintf(buf, "%04X:%04X:%s", (unsigned int)it->vid, (unsigned int)it->pid, it->isMouse ? "M" : "K");
        out += buf;
        if (it->name[0] != '\0') {
            out += ':';
            // Les ':' et sauts de ligne d'un nom farfelu casseraient le format :
            // on les neutralise plutot que de rejeter le nom.
            for (size_t k = 0; k < sizeof(it->name) && it->name[k] != '\0'; k++) {
                char ch = it->name[k];
                out += (ch == ':' || ch == '\n' || ch == '\r') ? ' ' : ch;
            }
        }
        out += '\n';
    }
    return out;
}

bool AddKnownDevice(std::vector<KnownDeviceEntry>* devices, uint16_t vid, uint16_t pid, bool isMouse) {
    return AddKnownDevice(devices, vid, pid, isMouse, nullptr);
}

bool AddKnownDevice(std::vector<KnownDeviceEntry>* devices, uint16_t vid, uint16_t pid,
                    bool isMouse, const char* name) {
    if (!devices)
        return false;

    for (std::vector<KnownDeviceEntry>::iterator it = devices->begin(); it != devices->end(); ++it) {
        if (it->vid == vid && it->pid == pid) {
            // Deja connu, mais on complete son nom si on vient tout juste de
            // l'apprendre : un fichier ecrit par une version anterieure du
            // plugin n'en contient pas, et il serait absurde de continuer a
            // afficher un VID:PID alors que le nom est desormais disponible.
            if (name && name[0] != '\0' && it->name[0] == '\0') {
                size_t k = 0;
                for (; name[k] != '\0' && k < sizeof(it->name) - 1; k++)
                    it->name[k] = name[k];
                it->name[k] = '\0';
                return true; // modifie -> le fichier doit etre reecrit
            }
            return false;
        }
    }

    KnownDeviceEntry entry;
    entry.vid = vid;
    entry.pid = pid;
    entry.isMouse = isMouse;
    entry.name[0] = '\0';
    if (name) {
        size_t k = 0;
        for (; name[k] != '\0' && k < sizeof(entry.name) - 1; k++)
            entry.name[k] = name[k];
        entry.name[k] = '\0';
    }
    devices->push_back(entry);
    return true;
}
