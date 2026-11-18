#include "../../application/ui_glossy.h"
#include "ui_win.h"

#include <windows.h>

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "../../application/donation_links.h"
#include "../../application/theme.h"

#include "camfinder_win.h"
#include "game_tools_win.h"
#include "live_watch.h"
#include "model_win.h"
#include "platform_win.h"
#include "sync.h"
#include "win_i18n.h"

namespace {

float g_k = 1.0f;          // canvas width / 1280
void* g_hwnd = nullptr;

const ImVec4 kGold  = ImVec4(0.96f, 0.83f, 0.35f, 1.0f);
const ImVec4 kGreen = ImVec4(0.55f, 0.83f, 0.10f, 1.0f);
const ImVec4 kRed   = ImVec4(0.94f, 0.54f, 0.50f, 1.0f);
const ImVec4 kDim   = ImVec4(0.66f, 0.72f, 0.79f, 1.0f);

bool g_openConnection = false, g_openMouseExtra = false, g_openGameAim = false;
bool g_openLogs = false, g_openAdvanced = false, g_openShortcuts = false;
bool g_openCamFinder = false;
const char* g_camDemo = nullptr;    // --open=camfinder-<state>: screenshots

void CenterNext(float w, float h) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (w > 0.0f) ImGui::SetNextWindowSize(ImVec2(w * g_k, h * g_k), ImGuiCond_Appearing);
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 620.0f * g_k);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

const char* TxtClose()  { return L4("Fermer", "Close", "Cerrar", "Fechar"); }

// Close cross at the top right of the current dialog, like a Windows window;
// Esc does the same. true when the dialog was closed this frame.
bool DialogCloseCross() {
    const float s = 30.0f * g_k, m = 8.0f * g_k;
    const ImVec2 back = ImGui::GetCursorPos();
    // Right edge = window width minus its padding: an auto-sized dialog keeps its width.
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - s - ImGui::GetStyle().WindowPadding.x, m));
    const bool clicked = ImGui::InvisibleButton("##close_cross", ImVec2(s, s));
    const bool hot = ImGui::IsItemHovered();
    if (hot) ImGui::SetTooltip("%s", TxtClose());
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hot) dl->AddRectFilled(a, b, IM_COL32(196, 43, 28, 255), 4.0f * g_k);
    const float in = s * 0.32f;
    const ImU32 col = hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 211, 224, 255);
    dl->AddLine(ImVec2(a.x + in, a.y + in), ImVec2(b.x - in, b.y - in), col, 1.6f * g_k);
    dl->AddLine(ImVec2(b.x - in, a.y + in), ImVec2(a.x + in, b.y - in), col, 1.6f * g_k);
    ImGui::SetCursorPos(back);
    const bool esc = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape));
    if (clicked || esc) { ImGui::CloseCurrentPopup(); return true; }
    return false;
}
const char* TxtCancel() { return L4("Annuler", "Cancel", "Cancelar", "Cancelar"); }

// ============================================================================
// Local files
// ============================================================================
void OpenLocalConfig() {
    std::string path, text;
    if (!plat::OpenFileDialog(g_hwnd, L"X360Remap.json\0*.json\0\0", path)) return;
    if (!plat::ReadWholeFile(path, text) || !ModelValidateConfigJson(text)) {
        plat::Toast("%s", L4("Ce fichier n'est pas un X360Remap.json valide", "This file is not a valid X360Remap.json",
                             "Este archivo no es un X360Remap.json válido", "Este arquivo não é um X360Remap.json válido"));
        return;
    }
    plat::WriteWholeFile(ModelConfigPath(), text);
    ModelWorkingCopyChanged();
    plat::Toast("%s", L4("Fichier ouvert - « Envoyer » pour le mettre sur la console", "File opened - \"Send\" to put it on the console",
                         "Archivo abierto - «Enviar» para ponerlo en la consola", "Arquivo aberto - \"Enviar\" para colocá-lo no console"));
}

void SaveLocalCopy() {
    std::string path, text;
    if (!plat::ReadWholeFile(ModelConfigPath(), text)) return;
    if (!plat::SaveFileDialog(g_hwnd, L"X360Remap.json\0*.json\0\0", L"X360Remap.json", L"json", path)) return;
    const bool ok = plat::WriteWholeFile(path, text);
    plat::Toast("%s", ok ? L4("Copie enregistrée", "Copy saved", "Copia guardada", "Cópia salva")
                         : L4("ÉCHEC de l'enregistrement", "Saving FAILED", "ERROR al guardar", "FALHA ao salvar"));
}

// ============================================================================
// Dialog: connection
// ============================================================================
void DrawConnectionDialog() {
    if (g_openConnection) { ImGui::OpenPopup("##dlg_connection"); g_openConnection = false; }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_connection", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    if (DialogCloseCross()) plat::SaveSettings();
    WinSettings& s = plat::Settings();

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Connexion à la console", "Console connection", "Conexión con la consola", "Conexão com o console"));
    PopX360Font(g_fontTitle);
    Hint(L4("Le serveur FTP d'Aurora doit être actif sur la console (il l'est par défaut).",
            "Aurora's FTP server must be running on the console (it is by default).",
            "El servidor FTP de Aurora debe estar activo en la consola (lo está por defecto).",
            "O servidor FTP do Aurora deve estar ativo no console (está por padrão)."));
    ImGui::Spacing();

    const float fieldW = 320.0f * g_k;
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText(L4("Adresse IP de la console", "Console IP address", "Dirección IP de la consola", "Endereço IP do console"),
                     s.consoleIp, sizeof(s.consoleIp));
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputInt(L4("Port", "Port", "Puerto", "Porta"), &s.ftpPort, 0);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText(L4("Utilisateur", "User", "Usuario", "Usuário"), s.ftpUser, sizeof(s.ftpUser));
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText(L4("Mot de passe", "Password", "Contraseña", "Senha"), s.ftpPassword, sizeof(s.ftpPassword), ImGuiInputTextFlags_Password);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText(L4("Dossier sur la console", "Folder on the console", "Carpeta en la consola", "Pasta no console"),
                     s.remoteDir, sizeof(s.remoteDir));
    if (!IsStudioDir(s.remoteDir))
        ImGui::TextColored(kRed, "%s", L4("Le dossier doit se terminer par X360RemapStudio : rien ne sera envoyé ailleurs.",
                                          "The folder must end with X360RemapStudio: nothing is ever sent elsewhere.",
                                          "La carpeta debe terminar en X360RemapStudio: nunca se envía nada a otro sitio.",
                                          "A pasta deve terminar em X360RemapStudio: nada é enviado para outro lugar."));
    ImGui::Checkbox(L4("Se connecter au démarrage", "Connect at start", "Conectar al iniciar", "Conectar ao iniciar"), &s.autoConnect);
    if (s.ftpPort <= 0 || s.ftpPort > 65535) s.ftpPort = 21;
    ImGui::Spacing();

    const bool busy = consync::Busy();
    if (ImGui::Button(busy ? L4("En cours...", "Working...", "En curso...", "Em andamento...")
                           : L4("Se connecter", "Connect", "Conectar", "Conectar")) && !busy) {
        plat::SaveSettings();
        consync::Pull();
    }
    ImGui::SameLine();
    if (ImGui::Button(consync::ScanRunning() ? L4("Recherche...", "Searching...", "Buscando...", "Procurando...")
                                             : L4("Trouver ma console", "Find my console", "Buscar mi consola", "Encontrar meu console")))
        consync::Scan();

    const std::vector<FoundFtp>& found = consync::ScanResults();
    for (size_t i = 0; i < found.size(); i++) {
        char label[256];
        _snprintf(label, sizeof(label) - 1, "%s   %s", found[i].ip.c_str(), found[i].banner.c_str());
        label[sizeof(label) - 1] = 0;
        if (ImGui::Selectable(label, false, ImGuiSelectableFlags_DontClosePopups)) {
            _snprintf(s.consoleIp, sizeof(s.consoleIp) - 1, "%s", found[i].ip.c_str());
            s.consoleIp[sizeof(s.consoleIp) - 1] = 0;
        }
    }

    ImGui::Spacing();
    if (consync::State() == consync::LINK_CONNECTED && !busy)
        ImGui::TextColored(kGreen, "%s", L4("Connecté - les réglages de la console sont chargés.", "Connected - the console settings are loaded.",
                                            "Conectado - los ajustes de la consola están cargados.", "Conectado - as configurações do console foram carregadas."));
    else if (consync::State() == consync::LINK_ERROR && !busy)
        ImGui::TextColored(kRed, "%s", consync::LastError().c_str());
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: conflict (changed on the console)
// ============================================================================
void DrawConflictDialog() {
    const consync::Conflict* c = consync::PendingConflict();
    if (!c) return;
    if (!ImGui::IsPopupOpen("##dlg_conflict")) ImGui::OpenPopup("##dlg_conflict");
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    PushX360Font(g_fontTitle);
    ImGui::TextColored(kGold, "%s", L4("Modifié sur la console", "Changed on the console", "Cambiado en la consola", "Alterado no console"));
    PopX360Font(g_fontTitle);
    ImGui::Text("%s", c->name.c_str());
    Hint(c->duringPush
         ? L4("Ce fichier a changé sur la console depuis ta dernière réception (Studio de la console, touche F9 en jeu...). Que veux-tu garder ?",
              "This file changed on the console since you last received it (console Studio, F9 in game...). What do you want to keep?",
              "Este archivo cambió en la consola desde tu última recepción (Studio de la consola, F9 en el juego...). ¿Qué quieres conservar?",
              "Este arquivo mudou no console desde o último recebimento (Studio do console, F9 no jogo...). O que você quer manter?")
         : L4("La console et le PC ont chacun des changements pour ce fichier. Que veux-tu garder ?",
              "The console and the PC both have changes for this file. What do you want to keep?",
              "La consola y el PC tienen cambios en este archivo. ¿Qué quieres conservar?",
              "O console e o PC têm alterações neste arquivo. O que você quer manter?"));
    ImGui::Spacing();
    int choice = -1;
    if (ImGui::Button(L4("Garder les miens (PC)", "Keep mine (PC)", "Conservar los míos (PC)", "Manter os meus (PC)"))) choice = 0;
    ImGui::SameLine();
    if (ImGui::Button(L4("Prendre ceux de la console", "Take the console's", "Tomar los de la consola", "Usar os do console"))) choice = 1;
    ImGui::SameLine();
    if (ImGui::Button(TxtCancel())) choice = 2;
    Hint(L4("Rien n'est perdu : le fichier remplacé sur la console est gardé à côté en .bak.",
            "Nothing is lost: the file replaced on the console is kept next to it as .bak.",
            "No se pierde nada: el archivo reemplazado en la consola se guarda al lado como .bak.",
            "Nada se perde: o arquivo substituído no console é mantido ao lado como .bak."));
    if (choice >= 0) {
        ImGui::CloseCurrentPopup();
        consync::ResolveConflict(choice);
    }
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: direct aim and curve of the selected mouse
// ============================================================================
void DrawMouseExtraDialog() {
    static MouseExtra s_extra;
    static bool s_loaded = false;
    if (g_openMouseExtra) { ImGui::OpenPopup("##dlg_mouse_extra"); g_openMouseExtra = false; s_loaded = false; }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_mouse_extra", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();
    const ModelDevice mouse = ModelSelectedMouse();
    if (!s_loaded) { ModelGetMouseExtra(s_extra); s_loaded = true; }

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Réglages souris avancés", "Advanced mouse settings", "Ajustes avanzados del ratón", "Configurações avançadas do mouse"));
    PopX360Font(g_fontTitle);
    if (!mouse.valid) {
        ImGui::TextColored(kRed, "%s", L4("Aucune souris connue : connecte-toi à la console d'abord.", "No known mouse: connect to the console first.",
                                          "Ningún ratón conocido: conéctate primero a la consola.", "Nenhum mouse conhecido: conecte-se ao console primeiro."));
    } else {
        ImGui::TextColored(kDim, "%s", mouse.name);
        ImGui::Spacing();
        bool changed = false;
        const float sliderW = 360.0f * g_k;

        int ads = s_extra.adsSensitivity > 0 ? s_extra.adsSensitivity : 100;
        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt(L4("Sensibilité en visée (LT)", "Sensitivity while aiming (LT)", "Sensibilidad al apuntar (LT)", "Sensibilidade ao mirar (LT)"),
                         &ads, 5, 200, "%d %%");
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        s_extra.adsSensitivity = ads;
        Hint(L4("Vitesse de la souris pendant que LT est tenu, dans les jeux à visée directe. 100 % = inchangée ; 25 % = quatre fois plus lente.",
                "Mouse speed while LT is held, in games with direct aim. 100 % = unchanged; 25 % = four times slower.",
                "Velocidad del ratón mientras se mantiene LT, en juegos con apuntado directo. 100 % = sin cambios; 25 % = cuatro veces más lenta.",
                "Velocidade do mouse enquanto LT está pressionado, em jogos com mira direta. 100 % = sem mudança; 25 % = quatro vezes mais lenta."));

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt(L4("Lissage de la visée directe", "Direct aim smoothing", "Suavizado del apuntado directo", "Suavização da mira direta"),
                         &s_extra.directAimSmoothingMs, 0, 100, "%d ms");
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        Hint(L4("Moyenne le mouvement sur ce nombre de millisecondes. 0 = désactivé.", "Averages the movement over this many milliseconds. 0 = off.",
                "Promedia el movimiento durante estos milisegundos. 0 = desactivado.", "Faz a média do movimento nesses milissegundos. 0 = desligado."));

        ImGui::Spacing();
        const char* curveNames[2] = { L4("Linéaire", "Linear", "Lineal", "Linear"), L4("Exponentielle", "Exponential", "Exponencial", "Exponencial") };
        ImGui::SetNextItemWidth(sliderW);
        if (ImGui::Combo(L4("Courbe de sensibilité", "Sensitivity curve", "Curva de sensibilidad", "Curva de sensibilidade"),
                         &s_extra.curveType, curveNames, 2)) changed = true;
        if (s_extra.curveType == 1) {
            ImGui::SetNextItemWidth(sliderW);
            ImGui::SliderFloat(L4("Exposant", "Exponent", "Exponente", "Expoente"), &s_extra.curveExponent, 1.0f, 3.0f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
            Hint(L4("Plus l'exposant est grand, plus les petits gestes sont précis ; le geste rapide atteint toujours la vitesse maximale.",
                    "The higher the exponent, the finer small movements are; a fast movement still reaches full speed.",
                    "Cuanto mayor el exponente, más precisos los gestos pequeños; el gesto rápido sigue alcanzando la velocidad máxima.",
                    "Quanto maior o expoente, mais precisos os gestos pequenos; o gesto rápido ainda atinge a velocidade máxima."));
        }
        if (changed) {
            const bool ok = ModelSetMouseExtra(s_extra);
            plat::Toast("%s", ok ? L4("Enregistré sur le PC - « Envoyer » pour la console", "Saved on the PC - \"Send\" for the console",
                                      "Guardado en el PC - «Enviar» para la consola", "Salvo no PC - \"Enviar\" para o console")
                                 : L4("ÉCHEC de l'enregistrement", "Saving FAILED", "ERROR al guardar", "FALHA ao salvar"));
        }
    }
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: direct aim of the selected game (camera lines) + share
// ============================================================================
void DrawGameAimDialog() {
    static char s_paste[2048] = "";
    static std::string s_error;
    if (g_openGameAim) { ImGui::OpenPopup("##dlg_game_aim"); g_openGameAim = false; s_paste[0] = 0; s_error.clear(); }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_game_aim", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();

    uint32_t tid = 0;
    std::string name;
    const bool haveGame = ModelSelectedGame(tid, name);
    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Visée directe du jeu", "Direct aim of the game", "Apuntado directo del juego", "Mira direta do jogo"));
    PopX360Font(g_fontTitle);

    if (haveGame) {
        ImGui::TextColored(kDim, "%s   (%08X)", name.c_str(), (unsigned)tid);
        ImGui::Spacing();
        GameCameraInfo cams;
        GameCameras(tid, cams);
        if (cams.learned.empty() && cams.builtin.empty())
            ImGui::TextUnformatted(L4("Aucune : la souris pilote le stick droit, comme une manette.",
                                      "None: the mouse drives the right stick, like a controller.",
                                      "Ninguno: el ratón mueve el stick derecho, como un mando.",
                                      "Nenhuma: o mouse controla o analógico direito, como um controle."));
        if (!cams.builtin.empty())
            ImGui::TextColored(kGreen, "%s (%d)", L4("Intégrée au plugin", "Built into the plugin", "Integrado en el plugin", "Integrada ao plugin"),
                               (int)cams.builtin.size());
        if (!cams.learned.empty()) {
            ImGui::TextColored(kGreen, "%s (%d)", L4("Apprise", "Learned", "Aprendido", "Aprendida"), (int)cams.learned.size());
            PushX360Font(g_fontSmall);
            for (size_t i = 0; i < cams.learned.size(); i++) ImGui::TextUnformatted(cams.learned[i].c_str());
            PopX360Font(g_fontSmall);
            if (ImGui::Button(L4("Retirer les lignes apprises", "Remove the learned lines", "Quitar las líneas aprendidas", "Remover as linhas aprendidas"))) {
                const int n = RemoveGameCameras(tid);
                plat::Toast("%d %s", n, L4("ligne(s) retirée(s)", "line(s) removed", "línea(s) quitada(s)", "linha(s) removida(s)"));
            }
        }
        ImGui::Spacing();
        if (ImGui::Button(L4("Trouver la caméra (Xenia)", "Find the camera (Xenia)", "Encontrar la cámara (Xenia)", "Encontrar a câmera (Xenia)"))) {
            g_openCamFinder = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
    }

    if (haveGame && ImGui::Button(L4("Importer depuis Xenia Camera Finder", "Import from Xenia Camera Finder",
                         "Importar desde Xenia Camera Finder", "Importar do Xenia Camera Finder"))) {
        std::string err;
        const int n = ImportFoundCameras(tid, err);
        if (!err.empty()) s_error = err;
        else if (n == 0)
            plat::Toast("%s", L4("Aucune caméra trouvée pour ce jeu sur ce PC", "No camera found for this game on this PC",
                                 "Ninguna cámara encontrada para este juego en este PC", "Nenhuma câmera encontrada para este jogo neste PC"));
        else {
            s_error.clear();
            plat::Toast("%d %s", n, L4("ligne(s) importée(s) - « Envoyer » pour la console", "line(s) imported - \"Send\" for the console",
                                       "línea(s) importada(s) - «Enviar» para la consola", "linha(s) importada(s) - \"Enviar\" para o console"));
        }
    }
    ImGui::TextUnformatted(L4("Ou coller une ligne (Xenia Camera Finder)", "Or paste a line (Xenia Camera Finder)",
                              "O pegar una línea (Xenia Camera Finder)", "Ou colar uma linha (Xenia Camera Finder)"));
    PushX360Font(g_fontSmall);
    ImGui::InputTextMultiline("##paste", s_paste, sizeof(s_paste), ImVec2(760.0f * g_k, 96.0f * g_k));
    PopX360Font(g_fontSmall);
    if (ImGui::Button(L4("Coller", "Paste", "Pegar", "Colar"))) {
        _snprintf(s_paste, sizeof(s_paste) - 1, "%s", plat::GetClipboard().c_str());
        s_paste[sizeof(s_paste) - 1] = 0;
        s_error.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(L4("Importer", "Import", "Importar", "Importar")) && s_paste[0]) {
        std::string err;
        const int n = ImportCameraLines(s_paste, 0, err);
        if (!err.empty()) s_error = err;
        else if (n == 0) s_error = "-";
        else {
            s_error.clear();
            s_paste[0] = 0;
            plat::Toast("%d %s", n, L4("ligne(s) importée(s) - « Envoyer » pour la console", "line(s) imported - \"Send\" for the console",
                                       "línea(s) importada(s) - «Enviar» para la consola", "linha(s) importada(s) - \"Enviar\" para o console"));
        }
    }
    if (!s_error.empty()) {
        ImGui::TextColored(kRed, "%s", L4("Ligne refusée (format non reconnu) :", "Line refused (format not recognised):",
                                          "Línea rechazada (formato no reconocido):", "Linha recusada (formato não reconhecido):"));
        PushX360Font(g_fontSmall);
        ImGui::TextUnformatted(s_error.c_str());
        PopX360Font(g_fontSmall);
    }
    Hint(L4("Le plugin relit ces lignes au lancement du jeu : pas besoin de redémarrer la console.",
            "The plugin reads these lines again when the game starts: no need to restart the console.",
            "El plugin vuelve a leer estas líneas al iniciar el juego: no hace falta reiniciar la consola.",
            "O plugin relê estas linhas ao iniciar o jogo: não é preciso reiniciar o console."));
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: find the camera with Xenia (camfinder_win.cpp runs the search)
// ============================================================================
void CamStep(int n, const char* title, const char* text) {
    ImGui::TextColored(kGold, "%d.", n);
    ImGui::SameLine();
    ImGui::TextUnformatted(title);
    ImGui::Indent(22.0f * g_k);
    Hint(text);
    ImGui::Unindent(22.0f * g_k);
    ImGui::Spacing();
}

void DrawCamFinderDialog() {
    if (g_openCamFinder) {
        ImGui::OpenPopup("##dlg_cam_finder");
        g_openCamFinder = false;
        CamFinderReset();
        if (g_camDemo) CamFinderDemo(g_camDemo);
    }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_cam_finder", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const CamJobView v = CamFinderView();
    const bool running = v.state == CAMJOB_RUNNING;
    if (!running) DialogCloseCross();

    uint32_t tid = 0;
    std::string name;
    const bool haveGame = ModelSelectedGame(tid, name);
    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Trouver la caméra", "Find the camera", "Encontrar la cámara", "Encontrar a câmera"));
    PopX360Font(g_fontTitle);
    if (haveGame) ImGui::TextColored(kDim, "%s   (%08X)", name.c_str(), (unsigned)tid);
    ImGui::Spacing();

    if (v.state == CAMJOB_IDLE) {
        CamStep(1, L4("Lance le jeu dans Xenia", "Start the game in Xenia", "Inicia el juego en Xenia", "Inicie o jogo no Xenia"),
                L4("Xenia Canary, le clavier comme manette : keyboard_mode = 1 dans xenia-canary.config.toml. Les flèches doivent tourner la caméra.",
                   "Xenia Canary, keyboard as the controller: keyboard_mode = 1 in xenia-canary.config.toml. The arrow keys must turn the camera.",
                   "Xenia Canary, el teclado como mando: keyboard_mode = 1 en xenia-canary.config.toml. Las flechas deben girar la cámara.",
                   "Xenia Canary, o teclado como controle: keyboard_mode = 1 no xenia-canary.config.toml. As setas devem girar a câmera."));
        CamStep(2, L4("Va en jeu normal, quelques minutes après le début", "Go to normal gameplay, a few minutes in",
                      "Ve al juego normal, unos minutos después del inicio", "Vá para o jogo normal, alguns minutos depois do início"),
                L4("Pas la première scène, une intro, une cinématique ou un menu : leur caméra est souvent bloquée. Tu dois pouvoir marcher et tourner. Puis reste immobile, regard droit devant.",
                   "Not the first scene, an intro, a cutscene or a menu: their camera is often locked. You must be free to walk and turn. Then stand still, looking straight ahead.",
                   "Ni la primera escena, ni una intro, una cinemática o un menú: su cámara suele estar bloqueada. Debes poder caminar y girar. Luego quédate quieto, mirando al frente.",
                   "Nem a primeira cena, uma intro, uma cutscene ou um menu: a câmera deles costuma ser travada. Você precisa poder andar e girar. Depois fique parado, olhando para frente."));
        CamStep(3, L4("Lâche les mains", "Hands off", "Suelta las manos", "Tire as mãos"),
                L4("L'outil passe Xenia devant et appuie lui-même sur les flèches, quelques minutes. Ne touche à rien. Besoin du PC ? Pause (ou Fin), encore une fois pour reprendre.",
                   "The tool brings Xenia to the front and presses the arrow keys itself, a few minutes. Don't touch anything. Need the PC? Pause (or End), again to resume.",
                   "La herramienta pone Xenia delante y pulsa ella misma las flechas, unos minutos. No toques nada. ¿Necesitas el PC? Pausa (o Fin), otra vez para seguir.",
                   "A ferramenta coloca o Xenia na frente e aperta as setas sozinha, alguns minutos. Não toque em nada. Precisa do PC? Pause (ou End), de novo para continuar."));
        if (!haveGame) ImGui::TextColored(kRed, "%s", L4("Choisis d'abord un jeu", "Pick a game first", "Elige primero un juego", "Escolha primeiro um jogo"));
        if (haveGame && ImGui::Button(L4("Lancer la recherche", "Start the search", "Iniciar la búsqueda", "Iniciar a busca"))) {
            CamFinderStart(tid, false, "", g_hwnd);
        }
        GameCameraInfo cams;
        if (haveGame) GameCameras(tid, cams);
        if (!cams.learned.empty() || !cams.builtin.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(L4("Vérifier", "Check", "Comprobar", "Verificar"))) {
                std::string saved;
                for (size_t i = 0; i < cams.learned.size(); i++) saved += cams.learned[i] + "\n";
                for (size_t i = 0; i < cams.builtin.size(); i++) saved += cams.builtin[i] + "\n";
                CamFinderStart(tid, true, saved, g_hwnd);
            }
            Hint(L4("Vérifier : après un redémarrage du jeu, reteste la ligne et les autres chemins gardés, et garde celle qui marche encore.",
                    "Check: after a game restart, tests the line and the other kept paths again, and keeps the one that still works.",
                    "Comprobar: tras reiniciar el juego, vuelve a probar la línea y los otros caminos guardados, y conserva la que aún funciona.",
                    "Verificar: depois de reiniciar o jogo, testa de novo a linha e os outros caminhos guardados, e fica com a que ainda funciona."));
        }
    } else if (running) {
        ImGui::TextColored(kGold, "%s", v.check ? L4("Vérification en cours - ne touche à rien", "Checking - don't touch anything",
                                                     "Comprobando - no toques nada", "Verificando - não toque em nada")
                                                : L4("Recherche en cours - ne touche à rien", "Searching - don't touch anything",
                                                     "Buscando - no toques nada", "Buscando - não toque em nada"));
        if (v.scanning) {
            char pct[16];
            _snprintf(pct, sizeof(pct), "%d %%", v.percent);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kGreen);   // the Studio's accent, not ImGui's blue
            ImGui::ProgressBar(v.percent / 100.0f, ImVec2(620.0f * g_k, 0.0f), pct);
            ImGui::PopStyleColor();
        }
        PushX360Font(g_fontSmall);
        ImGui::TextColored(kDim, "%s", v.detail.c_str());
        PopX360Font(g_fontSmall);
        Hint(L4("Xenia reste devant pendant la recherche : c'est normal. Pause (ou Fin) met en pause.",
                "Xenia stays in front during the search: that is normal. Pause (or End) pauses.",
                "Xenia queda delante durante la búsqueda: es normal. Pausa (o Fin) pausa.",
                "O Xenia fica na frente durante a busca: é normal. Pause (ou End) pausa."));
        if (ImGui::Button(TxtCancel())) CamFinderCancel();
    } else if (v.state == CAMJOB_FOUND) {
        ImGui::TextColored(kGreen, "%s", v.check ? (v.replaced ? L4("Ligne remplacée par un chemin qui marche encore", "Line replaced by a path that still works",
                                                                      "Línea sustituida por un camino que aún funciona", "Linha trocada por um caminho que ainda funciona")
                                                               : L4("La ligne marche encore", "The line still works", "La línea aún funciona", "A linha ainda funciona"))
                                                 : L4("Caméra trouvée", "Camera found", "Cámara encontrada", "Câmera encontrada"));
        ImGui::TextUnformatted(L4("Ligne utilisée pour ce jeu :", "Line now used for this game:", "Línea usada para este juego:", "Linha usada para este jogo:"));
        PushX360Font(g_fontSmall);
        ImGui::TextUnformatted(v.line.c_str());
        PopX360Font(g_fontSmall);
        if (v.verticalLost)
            ImGui::TextColored(kRed, "%s", L4("Vertical trouvé mais trop loin pour la ligne : la souris vise seulement gauche-droite.",
                                              "Vertical found but too far for the line: the mouse aims left-right only.",
                                              "Vertical encontrado pero demasiado lejos para la línea: el ratón apunta solo izquierda-derecha.",
                                              "Vertical encontrado mas longe demais para a linha: o mouse mira só esquerda-direita."));
        if (!v.check || v.replaced)
            Hint(L4("Ajoutée aux lignes de visée directe de ce jeu : « Envoyer » pour la console.",
                    "Added to this game's direct aim lines: \"Send\" for the console.",
                    "Añadida a las líneas de apuntado directo de este juego: «Enviar» para la consola.",
                    "Adicionada às linhas de mira direta deste jogo: \"Enviar\" para o console."));
        if (ImGui::Button(L4("Terminé", "Done", "Hecho", "Pronto"))) ImGui::CloseCurrentPopup();
    } else {
        if (v.state == CAMJOB_CANCELLED) ImGui::TextColored(kDim, "%s", L4("Annulé", "Cancelled", "Cancelado", "Cancelado"));
        else {
            const char* why = "";
            switch (v.error) {
            case CAMERR_NO_XENIA: why = L4("Xenia ou le jeu introuvable : lance Xenia Canary et le jeu d'abord.", "Xenia or the game not found: start Xenia Canary and the game first.",
                                           "Xenia o el juego no encontrado: inicia primero Xenia Canary y el juego.", "Xenia ou o jogo não encontrado: inicie primeiro o Xenia Canary e o jogo."); break;
            case CAMERR_OTHER_GAME: why = L4("Xenia fait tourner un autre jeu que celui choisi ici.", "Xenia is running another game than the one picked here.",
                                             "Xenia ejecuta otro juego que el elegido aquí.", "O Xenia está rodando outro jogo, não o escolhido aqui."); break;
            case CAMERR_STUCK: why = L4("La caméra ne répond pas au stick ici (flèches sans effet) : scène bloquée, cinématique, menu, ou le clavier n'est pas la manette dans Xenia.",
                                        "The camera does not answer the stick here (arrow keys had no effect): locked scene, cutscene, menu, or the keyboard is not the controller in Xenia.",
                                        "La cámara no responde al stick aquí (las flechas no hacen nada): escena bloqueada, cinemática, menú, o el teclado no es el mando en Xenia.",
                                        "A câmera não responde ao analógico aqui (as setas não fazem nada): cena travada, cutscene, menu, ou o teclado não é o controle no Xenia."); break;
            case CAMERR_SESSION_ONLY: why = L4("Caméra trouvée, mais sans adresse permanente : elle ne marcherait que jusqu'au redémarrage du jeu.",
                                               "Camera found, but with no permanent address: it would only work until the game restarts.",
                                               "Cámara encontrada, pero sin dirección permanente: solo funcionaría hasta reiniciar el juego.",
                                               "Câmera encontrada, mas sem endereço permanente: só funcionaria até reiniciar o jogo."); break;
            case CAMERR_REFUSED: why = L4("Ligne refusée (format non reconnu) :", "Line refused (format not recognised):",
                                          "Línea rechazada (formato no reconocido):", "Linha recusada (formato não reconhecido):"); break;
            default: why = v.check ? L4("Aucune ligne ne suit plus la caméra.", "No line follows the camera any more.",
                                        "Ninguna línea sigue ya la cámara.", "Nenhuma linha segue mais a câmera.")
                                   : L4("Pas trouvée.", "Not found.", "No encontrada.", "Não encontrada."); break;
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 620.0f * g_k);
            ImGui::TextColored(kRed, "%s", why);
            ImGui::PopTextWrapPos();
            if (v.error == CAMERR_REFUSED || v.error == CAMERR_OTHER_GAME) {
                PushX360Font(g_fontSmall);
                ImGui::TextUnformatted(v.note.c_str());
                PopX360Font(g_fontSmall);
            }
            if (v.error == CAMERR_STUCK || v.error == CAMERR_NOT_FOUND || v.error == CAMERR_SESSION_ONLY)
                Hint(L4("Le plus souvent c'est la scène : avance dans le jeu, là où tu marches et tournes librement, et relance.",
                        "Most often it is the scene: move further into the game, where you walk and turn freely, and run it again.",
                        "Casi siempre es la escena: avanza en el juego, donde caminas y giras libremente, y vuelve a lanzarla.",
                        "Quase sempre é a cena: avance no jogo, onde você anda e gira livremente, e rode de novo."));
        }
        if (ImGui::Button(L4("Recommencer", "Start again", "Empezar de nuevo", "Recomeçar"))) CamFinderReset();
        ImGui::SameLine();
        if (ImGui::Button(TxtClose())) ImGui::CloseCurrentPopup();
        Hint(L4("Détails : camera_finder_log.txt dans le dossier de données du Studio.", "Details: camera_finder_log.txt in the Studio's data folder.",
                "Detalles: camera_finder_log.txt en la carpeta de datos del Studio.", "Detalhes: camera_finder_log.txt na pasta de dados do Studio."));
    }
    ImGui::EndPopup();
}

void ExportSelectedProfile() {
    uint32_t tid = 0;
    std::string name, path;
    if (!ModelSelectedGame(tid, name)) {
        plat::Toast("%s", L4("Choisis d'abord un jeu", "Pick a game first", "Elige primero un juego", "Escolha primeiro um jogo"));
        return;
    }
    wchar_t def[64];
    swprintf(def, 64, L"X360Remap_profile_%08X.json", (unsigned)tid);
    if (!plat::SaveFileDialog(g_hwnd, L"X360Remap profile\0*.json\0\0", def, L"json", path)) return;
    const bool ok = ExportGameProfile(ModelSelectedMouse(), tid, name, path);
    plat::Toast("%s", ok ? L4("Profil exporté", "Profile exported", "Perfil exportado", "Perfil exportado")
                         : L4("ÉCHEC de l'export", "Export FAILED", "ERROR al exportar", "FALHA ao exportar"));
}

void ImportProfileFile() {
    std::string path, err;
    if (!plat::OpenFileDialog(g_hwnd, L"X360Remap profile\0*.json\0\0", path)) return;
    uint32_t tid = 0;
    if (ImportGameProfile(ModelSelectedMouse(), path, tid, err))
        plat::Toast("%s (%08X)", L4("Profil importé", "Profile imported", "Perfil importado", "Perfil importado"), (unsigned)tid);
    else
        plat::Toast("%s: %s", L4("Import refusé", "Import refused", "Importación rechazada", "Importação recusada"), err.c_str());
}

// ============================================================================
// Dialog: logs
// ============================================================================
bool RemoteHas(const char* name);

void DrawLiveTab(float footer) {
    const livewatch::Snapshot live = livewatch::Get();
    if (!live.running) {
        if (ImGui::Button(L4("Surveiller jusqu'au gel", "Watch until freeze", "Vigilar hasta el bloqueo", "Vigiar até travar"))) {
            const WinSettings& cfg = plat::Settings();
            livewatch::Params p;
            p.host = cfg.consoleIp; p.ftpPort = cfg.ftpPort; p.ftpUser = cfg.ftpUser;
            p.ftpPassword = cfg.ftpPassword; p.remoteDir = cfg.remoteDir;
            p.outDir = plat::WorkFile("live");
            CreateDirectoryA(p.outDir.c_str(), nullptr);
            livewatch::Start(p);
        }
    } else {
        if (ImGui::Button(L4("Arrêter", "Stop", "Detener", "Parar"))) livewatch::Stop();
        ImGui::SameLine();
        if (ImGui::Button(L4("Enregistrer un rapport", "Save a report", "Guardar un informe", "Salvar um relatório"))) livewatch::SaveReport();
    }
    ImGui::SameLine();
    ImGui::TextColored(live.consoleAlive ? kDim : kRed, "%s",
                       live.consoleAlive ? L4("Console vivante", "Console alive", "Consola activa", "Console ativo")
                                         : L4("Console muette", "Console silent", "Consola sin respuesta", "Console sem resposta"));
    if (!RemoteHas("enable_dbgprint.txt"))
        Hint(L4("Lignes du plugin en direct : active « Sortie de débogage » dans Avancé, puis redémarre la console une fois.",
                "Live plugin lines: turn on \"Debug output\" in Advanced, then restart the console once.",
                "Líneas del plugin en directo: activa «Salida de depuración» en Avanzado y reinicia la consola una vez.",
                "Linhas do plugin ao vivo: ative \"Saída de depuração\" em Avançado e reinicie o console uma vez."));
    if (!live.deathTime.empty())
        ImGui::TextColored(kRed, "%s %s", L4("Arrêt de réponse à", "Stopped answering at", "Dejó de responder a las", "Parou de responder às"), live.deathTime.c_str());
    if (!live.reportPath.empty()) ImGui::TextColored(kDim, "%s", live.reportPath.c_str());
    PushX360Font(g_fontSmall);
    ImGui::BeginChild("##live_lines", ImVec2(-1.0f, ImGui::GetContentRegionAvail().y - footer));
    for (size_t i = 0; i < live.tail.size(); i++) ImGui::TextUnformatted(live.tail[i].c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    PopX360Font(g_fontSmall);
}

void DrawLogsDialog() {
    static std::string s_text[4];
    static uint64_t s_stamp[4] = { 1, 1, 1, 1 };
    if (g_openLogs) { ImGui::OpenPopup("##dlg_logs"); g_openLogs = false; }
    CenterNext(1040.0f, 600.0f);
    if (!ImGui::BeginPopupModal("##dlg_logs", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) return;
    DialogCloseCross();

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Journaux de la console", "Console logs", "Registros de la consola", "Registros do console"));
    PopX360Font(g_fontTitle);

    const char* tabNames[4] = {
        L4("Plugin", "Plugin", "Plugin", "Plugin"),
        L4("Plugin (session précédente)", "Plugin (previous session)", "Plugin (sesión anterior)", "Plugin (sessão anterior)"),
        L4("Studio", "Studio", "Studio", "Studio"),
        L4("Studio (session précédente)", "Studio (previous session)", "Studio (sesión anterior)", "Studio (sessão anterior)"),
    };
    const float footer = ImGui::GetFrameHeightWithSpacing() + 6.0f * g_k;
    if (ImGui::BeginTabBar("##logs_tabs")) {
        for (int i = 0; i < 4; i++) {
            if (!ImGui::BeginTabItem(tabNames[i])) continue;
            const std::string path = plat::WorkFile(consync::kLogFiles[i]);
            const uint64_t stamp = plat::FileStamp(path);
            if (stamp != s_stamp[i]) {
                s_stamp[i] = stamp;
                s_text[i].clear();
                plat::ReadWholeFile(path, s_text[i]);
            }
            PushX360Font(g_fontSmall);
            if (s_text[i].empty()) {
                ImGui::TextColored(kDim, "%s", L4("Pas encore reçu : « Actualiser » le télécharge depuis la console.",
                                                  "Not received yet: \"Refresh\" downloads it from the console.",
                                                  "Aún no recibido: «Actualizar» lo descarga de la consola.",
                                                  "Ainda não recebido: \"Atualizar\" baixa do console."));
            } else {
                ImGui::InputTextMultiline("##log", &s_text[i][0], s_text[i].size() + 1,
                                          ImVec2(-1.0f, ImGui::GetContentRegionAvail().y - footer), ImGuiInputTextFlags_ReadOnly);
            }
            PopX360Font(g_fontSmall);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(L4("Live", "Live", "Live", "Live"))) {
            DrawLiveTab(footer);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footer - ImGui::GetStyle().WindowPadding.y + 6.0f * g_k);
    if (ImGui::Button(consync::Busy() ? L4("En cours...", "Working...", "En curso...", "Em andamento...")
                                      : L4("Actualiser", "Refresh", "Actualizar", "Atualizar")))
        consync::RefreshLogs();
    ImGui::SameLine();
    if (ImGui::Button(L4("Enregistrer un zip à envoyer", "Save a zip to send", "Guardar un zip para enviar", "Salvar um zip para enviar"))) {
        std::string path;
        if (plat::SaveFileDialog(g_hwnd, L"Zip\0*.zip\0\0", L"X360Remap_logs.zip", L"zip", path)) {
            const char* extra[] = { "X360Remap_switches.txt", "known_devices.txt", "known_titles.txt", "X360Remap.json", "camera_addresses.txt" };
            std::vector<std::pair<std::string, std::string> > files;
            std::string data;
            for (int i = 0; i < 4; i++)
                if (plat::ReadWholeFile(plat::WorkFile(consync::kLogFiles[i]), data)) files.push_back(std::make_pair(std::string(consync::kLogFiles[i]), data));
            for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
                if (plat::ReadWholeFile(plat::WorkFile(extra[i]), data)) files.push_back(std::make_pair(std::string(extra[i]), data));
            const bool ok = !files.empty() && plat::WriteZip(path, files);
            plat::Toast("%s", ok ? L4("Zip enregistré", "Zip saved", "Zip guardado", "Zip salvo")
                                 : L4("ÉCHEC : aucun journal à enregistrer", "FAILED: no log to save", "ERROR: ningún registro que guardar", "FALHA: nenhum registro para salvar"));
        }
    }
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: advanced (switch files of the plugin)
// ============================================================================
struct SwitchInfo { const char* file; const char* fr; const char* en; const char* es; const char* pt; };
const SwitchInfo kSwitches[] = {
    { "disable_notifications.txt", "Couper les notifications du plugin", "Turn the plugin notifications off",
      "Desactivar las notificaciones del plugin", "Desativar as notificações do plugin" },
    { "enable_input_trace.txt", "Tracer les entrées dans le journal (diagnostic)", "Trace inputs in the log (diagnosis)",
      "Registrar las entradas en el registro (diagnóstico)", "Registrar entradas no log (diagnóstico)" },
    { "enable_packet_on_change.txt", "Numéro de paquet seulement quand l'état change", "Packet number only when the state changes",
      "Número de paquete solo cuando cambia el estado", "Número de pacote só quando o estado muda" },
    { "enable_stick_release_ramp.txt", "Relâchement progressif du stick", "Gradual stick release",
      "Liberación gradual del stick", "Liberação gradual do analógico" },
    { "disable_defer_report_free.txt", "Débranchement : ancien comportement (comparaison)", "Unplug: previous behaviour (comparison)",
      "Desconexión: comportamiento anterior (comparación)", "Desconexão: comportamento anterior (comparação)" },
    { "enable_interrupt_status_check.txt", "Débranchement : ne pas relancer un transfert en erreur", "Unplug: do not requeue a failed transfer",
      "Desconexión: no reencolar una transferencia fallida", "Desconexão: não reenfileirar transferência com erro" },
    { "disable_clean_unplug.txt", "Débranchement : sans nettoyage", "Unplug: without clean-up",
      "Desconexión: sin limpieza", "Desconexão: sem limpeza" },
    { "disable_generic_hid.txt", "Manettes HID laissées à la console (test de gel)", "HID controllers left to the console (freeze test)",
      "Mandos HID dejados a la consola (prueba de bloqueo)", "Controles HID deixados ao console (teste de travamento)" },
    { "disable_own_link.txt", "Sans le lien disque du plugin (test de gel)", "Without the plugin's own disk link (freeze test)",
      "Sin el enlace de disco del plugin (prueba de bloqueo)", "Sem o link de disco do plugin (teste de travamento)" },
    { "disable_usb_reset.txt", "Sans la réinitialisation USB au démarrage (test Wi-Fi)", "No USB reset after boot (Wi-Fi test)",
      "Sin reinicio USB tras el arranque (prueba Wi-Fi)", "Sem reset USB após iniciar (teste de Wi-Fi)" },
    { "enable_dbgprint.txt", "Sortie de débogage (DbgPrint)", "Debug output (DbgPrint)", "Salida de depuración (DbgPrint)", "Saída de depuração (DbgPrint)" },
};

bool RemoteHas(const char* name) {
    const std::vector<std::string>& names = consync::RemoteNames();
    for (size_t i = 0; i < names.size(); i++)
        if (_stricmp(names[i].c_str(), name) == 0) return true;
    return false;
}

void DrawAdvancedDialog() {
    if (g_openAdvanced) { ImGui::OpenPopup("##dlg_advanced"); g_openAdvanced = false; }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_advanced", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();
    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Avancé", "Advanced", "Avanzado", "Avançado"));
    PopX360Font(g_fontTitle);
    Hint(L4("Interrupteurs du plugin : un fichier vide dans X360RemapStudio. Pris en compte au prochain démarrage de la console.",
            "Plugin switches: an empty file in X360RemapStudio. Taken into account at the next console start.",
            "Interruptores del plugin: un archivo vacío en X360RemapStudio. Se aplican en el próximo arranque de la consola.",
            "Interruptores do plugin: um arquivo vazio em X360RemapStudio. Valem na próxima inicialização do console."));
    ImGui::Spacing();

    const bool connected = consync::State() == consync::LINK_CONNECTED;
    if (!connected)
        ImGui::TextColored(kRed, "%s", L4("Connecte-toi à la console pour voir et changer les interrupteurs.",
                                          "Connect to the console to see and change the switches.",
                                          "Conéctate a la consola para ver y cambiar los interruptores.",
                                          "Conecte-se ao console para ver e alterar os interruptores."));
    const bool busy = consync::Busy();
    PushX360Font(g_fontSmall);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * g_k, 3.0f * g_k));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * g_k, 6.0f * g_k));
    for (size_t i = 0; i < sizeof(kSwitches) / sizeof(kSwitches[0]); i++) {
        bool on = RemoteHas(kSwitches[i].file);
        char label[200];
        _snprintf(label, sizeof(label) - 1, "%s##sw%d", L4(kSwitches[i].fr, kSwitches[i].en, kSwitches[i].es, kSwitches[i].pt), (int)i);
        label[sizeof(label) - 1] = 0;
        if (ImGui::Checkbox(label, &on) && connected && !busy) consync::SetSwitch(kSwitches[i].file, on);
        ImGui::SameLine(470.0f * g_k);
        ImGui::TextColored(kDim, "%s", kSwitches[i].file);
    }
    ImGui::PopStyleVar(2);
    PopX360Font(g_fontSmall);

    std::string active;
    if (plat::ReadWholeFile(plat::WorkFile("X360Remap_switches.txt"), active) && !active.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(L4("Lu par le plugin à son dernier démarrage (X360Remap_switches.txt) :",
                                  "Read by the plugin at its last start (X360Remap_switches.txt):",
                                  "Leído por el plugin en su último arranque (X360Remap_switches.txt):",
                                  "Lido pelo plugin na última inicialização (X360Remap_switches.txt):"));
        PushX360Font(g_fontSmall);
        ImGui::TextUnformatted(active.c_str());
        PopX360Font(g_fontSmall);
    }
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: install & repair (VISION.md 5c)
// ============================================================================
bool g_openInstall = false;

void DrawInstallDialog() {
    static std::string s_plugin, s_studio, s_zipName;   // files picked, waiting for the yes
    static bool s_autoCheck = false;
    static bool s_fromRelease = false;   // files came from a GitHub release
    if (g_openInstall) {
        ImGui::OpenPopup("##dlg_install");
        g_openInstall = false;
        // Detect, don't ask: the check runs on its own (once the link is free).
        s_autoCheck = !consync::Install().checked && plat::Settings().consoleIp[0];
    }
    if (s_autoCheck && !consync::Busy()) { s_autoCheck = false; consync::CheckInstallation(); }
    // Re-centred every frame: the dialog grows once the check result arrives.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    // Taller than a small window once the proposed change is shown: scroll instead of cutting.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.94f));
    if (!ImGui::BeginPopupModal("##dlg_install", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();
    const consync::InstallState& st = consync::Install();
    const bool busy = consync::Busy();

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Installation et réparation", "Install & repair", "Instalación y reparación", "Instalação e reparo"));
    PopX360Font(g_fontTitle);

    if (ImGui::Button(busy ? L4("En cours...", "Working...", "En curso...", "Em andamento...")
                           : L4("Vérifier la console", "Check the console", "Comprobar la consola", "Verificar o console")) && !busy)
        consync::CheckInstallation();
    ImGui::Spacing();

    if (!st.checked) {
        Hint(L4("Lit launch.ini de DashLaunch et le dossier X360RemapStudio sur la console, sans rien modifier.",
                "Reads DashLaunch's launch.ini and the X360RemapStudio folder on the console, without changing anything.",
                "Lee el launch.ini de DashLaunch y la carpeta X360RemapStudio en la consola, sin cambiar nada.",
                "Lê o launch.ini do DashLaunch e a pasta X360RemapStudio no console, sem alterar nada."));
    } else {
        const InstallCheck& c = st.check;
        char buf[768];
        // Same sentences as the console Studio's band (application/i18n.cpp).
        switch (c.diag.diag) {
            case DIAG_NO_INI:       _snprintf(buf, sizeof(buf) - 1, "%s", Tr(STR_DIAG_NO_INI)); break;
            case DIAG_NO_LINE:      _snprintf(buf, sizeof(buf) - 1, "%s", Tr(STR_DIAG_NO_LINE)); break;
            case DIAG_FILE_MISSING: _snprintf(buf, sizeof(buf) - 1, Tr(STR_DIAG_FILE_MISSING_FMT), c.diag.pluginPath.c_str()); break;
            case DIAG_HIDDRIVER:    _snprintf(buf, sizeof(buf) - 1, Tr(STR_DIAG_HIDDRIVER_FMT), c.diag.otherPath.c_str()); break;
            default:                _snprintf(buf, sizeof(buf) - 1, Tr(STR_DIAG_LINE_OK_FMT), c.diag.pluginPath.c_str()); break;
        }
        buf[sizeof(buf) - 1] = 0;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 760.0f * g_k);
        ImGui::TextColored(c.diag.diag == DIAG_LINE_OK ? kGreen : kGold, "%s", buf);
        ImGui::PopTextWrapPos();

        PushX360Font(g_fontSmall);
        ImGui::TextColored(kDim, "launch.ini : %s", c.iniFound ? c.iniFtpPath.c_str() : L4("introuvable", "not found", "no encontrado", "não encontrado"));
        const std::string& ver = consync::PluginVersion();
        ImGui::TextColored(kDim, "%s %s", L4("Version du plugin :", "Plugin version:", "Versión del plugin:", "Versão do plugin:"),
                           ver.empty() ? L4("inconnue (aucun journal reçu)", "unknown (no log received)", "desconocida (ningún registro)", "desconhecida (nenhum registro)") : ver.c_str());
        ImGui::TextColored(st.logActive == 1 ? kGreen : kDim, "%s %s", L4("Journal du plugin :", "Plugin log:", "Registro del plugin:", "Registro do plugin:"),
                           st.logActive == 1 ? L4("il bouge (plugin vivant)", "changing (plugin alive)", "cambia (plugin activo)", "mudando (plugin ativo)")
                         : st.logActive == 0 ? L4("immobile pendant 3 s", "unchanged for 3 s", "sin cambios durante 3 s", "sem mudança por 3 s")
                                             : L4("inconnu", "unknown", "desconocido", "desconhecido"));
        const std::string& sver = consync::StudioVersion();
        const bool mismatch = !sver.empty() && !ver.empty() && sver != ver;
        ImGui::TextColored(mismatch ? kGold : kDim, "%s %s%s", L4("Version du Studio de la console :", "Console Studio version:", "Versión del Studio de la consola:", "Versão do Studio do console:"),
                           sver.empty() ? L4("inconnue (aucun journal reçu)", "unknown (no log received)", "desconocida (ningún registro)", "desconhecida (nenhum registro)") : sver.c_str(),
                           mismatch ? L4(" - différente du plugin : installe la même version des deux", " - differs from the plugin: install the same version of both",
                                         " - distinta del plugin: instala la misma versión de los dos", " - diferente do plugin: instale a mesma versão dos dois") : "");
        ImGui::TextColored(kDim, "Hdd:\\X360Remap.xex : %s   Hdd:\\X360RemapStudio\\X360RemapStudio.xex : %s",
                           c.pluginPresent ? "OK" : "-", c.studioAppPresent ? "OK" : "-");
        PopX360Font(g_fontSmall);

        if (c.fixOffered) {
            ImGui::Separator();
            ImGui::TextUnformatted(L4("Modification proposée de launch.ini (une seule ligne) :", "Proposed change to launch.ini (one line only):",
                                      "Cambio propuesto en launch.ini (una sola línea):", "Alteração proposta no launch.ini (uma só linha):"));
            PushX360Font(g_fontSmall);
            if (!c.fixOldLine.empty()) ImGui::TextColored(kRed, "- %s", c.fixOldLine.c_str());
            else ImGui::TextColored(kDim, "%s", L4("(ligne ajoutée dans [Plugins])", "(line added to [Plugins])", "(línea añadida en [Plugins])", "(linha adicionada em [Plugins])"));
            ImGui::TextColored(kGreen, "+ %s", c.fixNewLine.c_str());
            PopX360Font(g_fontSmall);
            Hint(L4("Rien d'autre n'est touché. L'ancien fichier est gardé en launch.ini.bak. Redémarre la console ensuite.",
                    "Nothing else is touched. The old file is kept as launch.ini.bak. Restart the console afterwards.",
                    "No se toca nada más. El archivo anterior se guarda como launch.ini.bak. Reinicia la consola después.",
                    "Nada mais é alterado. O arquivo anterior fica em launch.ini.bak. Reinicie o console depois."));
            ImGui::PushStyleColor(ImGuiCol_Text, kGold);
            if (ImGui::Button(L4("Oui, modifier cette ligne", "Yes, change this line", "Sí, cambiar esta línea", "Sim, alterar esta linha")) && !busy)
                consync::ApplyLaunchIniFix();
            ImGui::PopStyleColor();
        } else if (c.slotsFull && c.pluginPresent) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 760.0f * g_k);
            ImGui::TextColored(kRed, "%s", L4("Les 5 emplacements de plugins de DashLaunch sont déjà pris par d'autres plugins : X360Remap ne peut pas être ajouté. Rien n'a été modifié.",
                                              "The 5 DashLaunch plugin slots are already used by other plugins: X360Remap cannot be added. Nothing was changed.",
                                              "Los 5 espacios de plugins de DashLaunch ya están ocupados por otros plugins: no se puede añadir X360Remap. No se cambió nada.",
                                              "Os 5 espaços de plugins do DashLaunch já estão ocupados por outros plugins: o X360Remap não pode ser adicionado. Nada foi alterado."));
            ImGui::PopTextWrapPos();
        } else if (!c.pluginPresent && (c.diag.diag == DIAG_NO_LINE || c.diag.diag == DIAG_FILE_MISSING)) {
            Hint(L4("Installe d'abord le plugin ci-dessous : la ligne de launch.ini sera proposée ensuite.",
                    "Install the plugin below first: the launch.ini line will be offered afterwards.",
                    "Instala primero el plugin abajo: después se propondrá la línea de launch.ini.",
                    "Instale primeiro o plugin abaixo: a linha do launch.ini será proposta depois."));
        }
    }

    ImGui::Separator();
    // --- GitHub: nothing goes out unless the user turned it on or clicks ---
    {
        WinSettings& set = plat::Settings();
        const consync::UpdateState& up = consync::UpdateInfo();
        bool changed = ImGui::Checkbox(L4("Chercher les nouvelles versions au démarrage (GitHub)", "Look for new versions at start (GitHub)",
                                          "Buscar nuevas versiones al iniciar (GitHub)", "Procurar novas versões ao iniciar (GitHub)"), &set.checkUpdates);
        changed |= ImGui::Checkbox(L4("Compter aussi les préversions", "Count previews too", "Contar también las versiones previas", "Contar também as prévias"),
                                   &set.updatePreviews);
        if (changed) plat::SaveSettings();
        if (ImGui::Button(L4("Chercher maintenant", "Check now", "Buscar ahora", "Procurar agora"))) consync::CheckForUpdate();
        if (up.checked && !up.err.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kRed, "%s", up.err.c_str());
        } else if (up.checked && up.release.id) {
            ImGui::SameLine();
            ImGui::TextColored(up.available ? kGold : kDim, "%s %s (%.10s)%s",
                               L4("Dernière version :", "Latest version:", "Última versión:", "Última versão:"), up.release.name.c_str(),
                               up.release.publishedAt.c_str(), up.release.prerelease ? L4(", préversion", ", preview", ", previa", ", prévia") : "");
            if (s_plugin.empty()) {
                if (ImGui::Button(up.downloading ? L4("Téléchargement...", "Downloading...", "Descargando...", "Baixando...")
                                                 : L4("Télécharger cette version", "Download this version", "Descargar esta versión", "Baixar esta versão")))
                    consync::DownloadUpdate();
                ImGui::SameLine();
                if (ImGui::Button(L4("Page de la version", "Release page", "Página de la versión", "Página da versão"))) plat::OpenUrl(up.release.pageUrl.c_str());
                if (up.available) {
                    ImGui::SameLine();
                    if (ImGui::Button(L4("Ne plus me la signaler", "Do not tell me again", "No avisarme más", "Não avisar de novo"))) consync::MarkUpdateSeen();
                }
            }
        }
        // Downloaded: same confirmation as a zip picked on the PC.
        if (s_plugin.empty() && consync::TakeDownloadedUpdate(s_plugin, s_studio, s_zipName)) s_fromRelease = true;
    }
    ImGui::Separator();
    if (s_plugin.empty()) {
        if (ImGui::Button(L4("Installer / mettre à jour depuis un zip...", "Install / update from a zip...",
                             "Instalar / actualizar desde un zip...", "Instalar / atualizar a partir de um zip...")) && !busy) {
            std::string zip;
            if (plat::OpenFileDialog(g_hwnd, L"X360Remap zip\0*.zip\0\0", zip)) {
                // A new folder per zip, so files of an older zip are never picked.
                // shortcut: the extracted folders stay in the cache, clean it if it ever grows.
                const std::string dir = plat::CacheFile("install") + "\\" + std::to_string((long long)(plat::Seconds() * 1000.0));
                if (!plat::ExtractZip(zip, dir)) {
                    plat::Toast("%s", L4("Zip illisible", "Zip could not be read", "Zip ilegible", "Zip ilegível"));
                } else {
                    const std::string p = plat::FindFileBelow(dir, "X360Remap.xex"), a = plat::FindFileBelow(dir, "X360RemapStudio.xex");
                    if (p.empty() || a.empty() || !plat::ReadWholeFile(p, s_plugin) || !plat::ReadWholeFile(a, s_studio) ||
                        s_plugin.empty() || s_studio.empty()) {
                        s_plugin.clear(); s_studio.clear();
                        plat::Toast("%s", L4("Ce zip ne contient pas X360Remap.xex et X360RemapStudio.xex",
                                             "This zip does not hold X360Remap.xex and X360RemapStudio.xex",
                                             "Este zip no contiene X360Remap.xex y X360RemapStudio.xex",
                                             "Este zip não contém X360Remap.xex e X360RemapStudio.xex"));
                    } else {
                        s_zipName = zip.substr(zip.find_last_of('\\') + 1);
                    }
                }
            }
        }
    } else {
        ImGui::Text("%s", s_zipName.c_str());
        ImGui::TextColored(kDim, "X360Remap.xex %d Ko, X360RemapStudio.xex %d Ko -> %s", (int)(s_plugin.size() / 1024),
                           (int)(s_studio.size() / 1024), plat::Settings().remoteDir);
        Hint(L4("Les réglages de la console sont d'abord copiés sur le PC. Lance ensuite X360RemapStudio.xex depuis le dossier X360RemapStudio.",
                "The console settings are first copied to the PC. Then start X360RemapStudio.xex from the X360RemapStudio folder.",
                "Primero se copian los ajustes de la consola al PC. Luego inicia X360RemapStudio.xex desde la carpeta X360RemapStudio.",
                "As configurações do console são copiadas primeiro para o PC. Depois inicie X360RemapStudio.xex na pasta X360RemapStudio."));
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        const bool yes = ImGui::Button(L4("Oui, installer", "Yes, install", "Sí, instalar", "Sim, instalar"));
        ImGui::PopStyleColor();
        if (yes && !busy) {
            consync::InstallXex(s_plugin, s_studio);
            if (s_fromRelease) consync::MarkUpdateSeen();
            s_plugin.clear(); s_studio.clear(); s_fromRelease = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(TxtCancel())) { s_plugin.clear(); s_studio.clear(); s_fromRelease = false; }
    }

    if (ImGui::Button(L4("Copier les réglages de la console sur le PC", "Copy the console settings to the PC",
                         "Copiar los ajustes de la consola al PC", "Copiar as configurações do console para o PC")) && !busy)
        consync::BackupSettings();
    if (!st.lastBackupDir.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(L4("Ouvrir la sauvegarde", "Open the backup", "Abrir la copia", "Abrir a cópia"))) plat::OpenFolder(st.lastBackupDir);
    }
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: devices plugged into this PC
// ============================================================================
bool g_openPcDevices = false;

void DrawPcDevicesDialog() {
    // Asked once per session on its own, when a keyboard or mouse of the PC is
    // unknown to the console and was never answered.
    static bool s_askedThisSession = false;
    if (!s_askedThisSession && ModelPcDevicesToAsk() > 0 && !ImGui::IsPopupOpen("##dlg_pcdev") && !ImGui::IsPopupOpen("##dlg_wizard") &&
        !ImGui::IsPopupOpen("##dlg_cam_finder")) {   // never over a camera search
        s_askedThisSession = true;
        g_openPcDevices = true;
    }
    if (g_openPcDevices) { ImGui::OpenPopup("##dlg_pcdev"); g_openPcDevices = false; }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_pcdev", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Périphériques branchés sur ce PC", "Devices plugged into this PC",
                              "Dispositivos conectados a este PC", "Dispositivos conectados a este PC"));
    PopX360Font(g_fontTitle);
    Hint(L4("Un clavier ou une souris que la console n'affiche pas : branche-le ici. Si c'est le même modèle que sur la console, ses réglages sont faits ici et envoyés à la console sous son identifiant (VID:PID). « Non » : rien sur ce périphérique n'est envoyé à la console.",
            "A keyboard or mouse the console does not show: plug it in here. If it is the same model as on the console, its settings are made here and sent to the console under its id (VID:PID). \"No\": nothing about this device is sent to the console.",
            "Un teclado o ratón que la consola no muestra: conéctalo aquí. Si es el mismo modelo que en la consola, sus ajustes se hacen aquí y se envían a la consola con su identificador (VID:PID). «No»: no se envía nada de este dispositivo a la consola.",
            "Um teclado ou mouse que o console não mostra: conecte-o aqui. Se for o mesmo modelo do console, as configurações são feitas aqui e enviadas ao console com seu identificador (VID:PID). \"Não\": nada deste dispositivo é enviado ao console."));
    ImGui::Spacing();

    const std::vector<ModelPcDevice> devs = ModelPcDevices();
    if (devs.empty())
        ImGui::TextColored(kDim, "%s", L4("Aucun clavier, souris ou manette USB trouvé sur ce PC.", "No USB keyboard, mouse or controller found on this PC.",
                                          "Ningún teclado, ratón o mando USB encontrado en este PC.", "Nenhum teclado, mouse ou controle USB encontrado neste PC."));
    for (size_t i = 0; i < devs.size(); i++) {
        const ModelPcDevice& d = devs[i];
        const char* kind = d.kind == 'K' ? L4("Clavier", "Keyboard", "Teclado", "Teclado")
                         : d.kind == 'M' ? L4("Souris", "Mouse", "Ratón", "Mouse")
                                         : L4("Manette", "Controller", "Mando", "Controle");
        ImGui::PushID((int)i);
        ImGui::Text("%s  %s", kind, d.name.empty() ? "-" : d.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%04X:%04X", d.vid, d.pid);
        PushX360Font(g_fontSmall);
        if (d.kind == 'G') {
            ImGui::TextColored(kDim, "%s", L4("Manettes : leurs réglages ne sont pas encore dans le Studio (identifiant noté ci-dessus).",
                                              "Controllers: their settings are not in the Studio yet (id noted above).",
                                              "Mandos: sus ajustes aún no están en el Studio (identificador indicado arriba).",
                                              "Controles: as configurações ainda não estão no Studio (identificador acima)."));
        } else if (d.state == 3) {
            ImGui::TextColored(kGreen, "%s", L4("La console le connaît déjà.", "The console already knows it.", "La consola ya lo conoce.", "O console já o conhece."));
        } else {
            if (d.state == 1) ImGui::TextColored(kGreen, "%s", L4("Même que sur la console : réglé ici, envoyé à la console.", "Same as on the console: set here, sent to the console.",
                                                                  "Igual que en la consola: se ajusta aquí y se envía a la consola.", "Igual ao do console: ajustado aqui, enviado ao console."));
            else if (d.state == 2) ImGui::TextColored(kDim, "%s", L4("Pas utilisé pour la console.", "Not used for the console.", "No se usa para la consola.", "Não usado para o console."));
            else ImGui::TextColored(kGold, "%s", L4("Est-ce le même que celui branché sur la console ?", "Is it the same as the one plugged into the console?",
                                                   "¿Es el mismo que el conectado a la consola?", "É o mesmo que está conectado ao console?"));
            if (d.state != 1 && ImGui::SmallButton(L4("Oui, le même", "Yes, the same", "Sí, el mismo", "Sim, o mesmo"))) ModelAnswerPcDevice(d.vid, d.pid, d.kind, true);
            if (d.state == 0) ImGui::SameLine();
            if (d.state != 2 && ImGui::SmallButton(L4("Non", "No", "No", "Não"))) ModelAnswerPcDevice(d.vid, d.pid, d.kind, false);
        }
        PopX360Font(g_fontSmall);
        ImGui::PopID();
        ImGui::Spacing();
    }
    Hint(L4("Rappel : le plugin applique ces réglages quand il reconnaît ce périphérique sur la console. S'il ne le reconnaît pas du tout (rien dans le journal), c'est un problème USB côté console : envoie le journal.",
            "Note: the plugin applies these settings when it recognises this device on the console. If it does not recognise it at all (nothing in the log), it is a USB issue on the console side: send the log.",
            "Nota: el plugin aplica estos ajustes cuando reconoce este dispositivo en la consola. Si no lo reconoce en absoluto (nada en el registro), es un problema USB de la consola: envía el registro.",
            "Nota: o plugin aplica estas configurações quando reconhece este dispositivo no console. Se não o reconhecer (nada no registro), é um problema USB do console: envie o registro."));
    ImGui::EndPopup();
}

// ============================================================================
// Install wizard: the whole first install for someone who never did it -
// find the console, download the latest X360Remap, install it, plugin line in
// launch.ini, the Studio in Aurora, reboot, check. One step after the other,
// each with its own button; nothing is written without the user's click.
// ============================================================================
bool g_openWizard = false;

// "OK" green / "->" gold (current) / "-" dim (later): the font has no check mark.
void WizardStepTitle(int index, int state, const char* title) {
    const ImVec4 col = state == 2 ? kGreen : state == 1 ? kGold : kDim;
    ImGui::TextColored(col, "%s  %d. %s", state == 2 ? "OK" : state == 1 ? "->" : " -", index, title);
}

void DrawInstallWizard() {
    static std::string s_plugin, s_studio, s_name;
    static bool s_skipAurora = false, s_askReboot = false, s_wantDownload = false;
    if (g_openWizard) {
        ImGui::OpenPopup("##dlg_wizard");
        g_openWizard = false;
        if (plat::Settings().consoleIp[0] == 0 && !consync::ScanRunning()) consync::Scan();
    }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ds.y * 0.94f));
    if (!ImGui::BeginPopupModal("##dlg_wizard", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();
    WinSettings& set = plat::Settings();
    const bool busy = consync::Busy();
    const bool connected = consync::State() == consync::LINK_CONNECTED;
    const consync::InstallState& st = consync::Install();
    const consync::UpdateState& up = consync::UpdateInfo();
    const InstallCheck& c = st.check;

    // Where we are: each step is done or not, from what the console says.
    const bool sConsole = connected;
    const bool sFiles = st.checked && c.pluginPresent && c.studioAppPresent;
    const bool sIni = st.checked && c.diag.diag == DIAG_LINE_OK && !c.fixOffered;
    const bool sAurora = s_skipAurora || (st.checked && c.auroraHasStudio);
    const bool sReboot = st.reboot == consync::REBOOT_PLUGIN_RUNNING;
    const int current = !sConsole ? 1 : !sFiles ? 2 : !sIni ? 3 : !sAurora ? 4 : !sReboot ? 5 : 6;
    if (connected && !st.checked && !busy) consync::CheckInstallation();   // detect, don't ask

    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Installer X360Remap sur la console", "Install X360Remap on the console",
                              "Instalar X360Remap en la consola", "Instalar o X360Remap no console"));
    PopX360Font(g_fontTitle);
    Hint(L4("Console allumée sur Aurora, sur le même réseau que ce PC. Chaque étape a son bouton ; rien n'est écrit sans ton clic.",
            "Console on, in Aurora, on the same network as this PC. Each step has its button; nothing is written without your click.",
            "Consola encendida en Aurora, en la misma red que este PC. Cada paso tiene su botón; no se escribe nada sin tu clic.",
            "Console ligado no Aurora, na mesma rede deste PC. Cada etapa tem seu botão; nada é gravado sem o seu clique."));
    ImGui::Spacing();

    // 1. Console --------------------------------------------------------------
    WizardStepTitle(1, sConsole ? 2 : 1, L4("Trouver la console", "Find the console", "Encontrar la consola", "Encontrar o console"));
    if (!sConsole) {
        ImGui::Indent(30.0f * g_k);
        const std::vector<FoundFtp>& found = consync::ScanResults();
        if (consync::ScanRunning()) ImGui::TextColored(kDim, "%s", L4("Recherche sur le réseau...", "Searching the network...", "Buscando en la red...", "Procurando na rede..."));
        for (size_t i = 0; i < found.size(); i++) {
            char label[256];
            _snprintf(label, sizeof(label) - 1, "%s   %s", found[i].ip.c_str(), found[i].banner.c_str()); label[sizeof(label) - 1] = 0;
            ImGui::PushID((int)i);
            if (ImGui::Button(L4("Utiliser", "Use", "Usar", "Usar")) && !busy) {
                _snprintf(set.consoleIp, sizeof(set.consoleIp) - 1, "%s", found[i].ip.c_str()); set.consoleIp[sizeof(set.consoleIp) - 1] = 0;
                plat::SaveSettings();
                consync::Pull();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(220.0f * g_k);
        ImGui::InputText(L4("ou adresse IP", "or IP address", "o dirección IP", "ou endereço IP"), set.consoleIp, sizeof(set.consoleIp));
        ImGui::SameLine();
        if (ImGui::Button(busy ? L4("En cours...", "Working...", "En curso...", "Em andamento...") : L4("Se connecter", "Connect", "Conectar", "Conectar")) && !busy && set.consoleIp[0]) {
            plat::SaveSettings();
            consync::Pull();
        }
        ImGui::SameLine();
        if (ImGui::Button(L4("Chercher encore", "Search again", "Buscar de nuevo", "Procurar de novo")) && !consync::ScanRunning()) consync::Scan();
        if (!consync::ScanRunning() && found.empty() && consync::State() != consync::LINK_CONNECTED)
            Hint(L4("Aucune console trouvée : vérifie qu'elle est allumée, sur Aurora, et sur le même réseau. L'adresse IP est dans Aurora (Paramètres > Réseau).",
                    "No console found: check that it is on, in Aurora, and on the same network. The IP address is in Aurora (Settings > Network).",
                    "Ninguna consola encontrada: comprueba que está encendida, en Aurora y en la misma red. La dirección IP está en Aurora (Ajustes > Red).",
                    "Nenhum console encontrado: verifique se está ligado, no Aurora e na mesma rede. O endereço IP está no Aurora (Configurações > Rede)."));
        if (consync::State() == consync::LINK_ERROR) ImGui::TextColored(kRed, "%s", consync::LastError().c_str());
        ImGui::Unindent(30.0f * g_k);
    }

    // 2. Download + install the two files -------------------------------------
    WizardStepTitle(2, sFiles ? 2 : current == 2 ? 1 : 0, L4("Télécharger et installer X360Remap", "Download and install X360Remap",
                                                                "Descargar e instalar X360Remap", "Baixar e instalar o X360Remap"));
    if (current == 2 && st.checked) {
        ImGui::Indent(30.0f * g_k);
        if (s_plugin.empty()) consync::TakeDownloadedUpdate(s_plugin, s_studio, s_name);
        // The newest release, previews included: a tester wants the latest build.
        if (s_wantDownload && up.checked && up.release.id && !up.downloading && s_plugin.empty() && !busy) {
            s_wantDownload = false;
            consync::DownloadUpdate();
        }
        if (s_plugin.empty()) {
            if (ImGui::Button(up.downloading || s_wantDownload ? L4("Téléchargement...", "Downloading...", "Descargando...", "Baixando...")
                                                               : L4("Télécharger la dernière version (GitHub)", "Download the latest version (GitHub)",
                                                                    "Descargar la última versión (GitHub)", "Baixar a última versão (GitHub)")) && !busy && !s_wantDownload) {
                set.updatePreviews = true;
                plat::SaveSettings();
                s_wantDownload = true;
                consync::CheckForUpdate();
            }
            if (up.checked && !up.err.empty()) ImGui::TextColored(kRed, "GitHub: %s", up.err.c_str());
        } else {
            ImGui::Text("%s  (X360Remap.xex %d Ko, X360RemapStudio.xex %d Ko)", s_name.c_str(), (int)(s_plugin.size() / 1024), (int)(s_studio.size() / 1024));
            Hint(L4("Les réglages déjà sur la console sont d'abord copiés sur ce PC.", "The settings already on the console are first copied to this PC.",
                    "Los ajustes que ya están en la consola se copian primero a este PC.", "As configurações já no console são copiadas primeiro para este PC."));
            ImGui::PushStyleColor(ImGuiCol_Text, kGold);
            if (ImGui::Button(L4("Installer sur la console", "Install on the console", "Instalar en la consola", "Instalar no console")) && !busy) {
                consync::InstallXex(s_plugin, s_studio);
                consync::MarkUpdateSeen();
                s_plugin.clear(); s_studio.clear();
            }
            ImGui::PopStyleColor();
        }
        ImGui::Unindent(30.0f * g_k);
    }

    // 3. launch.ini ----------------------------------------------------------------
    WizardStepTitle(3, sIni ? 2 : current == 3 ? 1 : 0, L4("Charger le plugin au démarrage (launch.ini)", "Load the plugin at start (launch.ini)",
                                                              "Cargar el plugin al inicio (launch.ini)", "Carregar o plugin na inicialização (launch.ini)"));
    if (current == 3) {
        ImGui::Indent(30.0f * g_k);
        if (c.fixOffered) {
            PushX360Font(g_fontSmall);
            if (!c.fixOldLine.empty()) ImGui::TextColored(kRed, "- %s", c.fixOldLine.c_str());
            ImGui::TextColored(kGreen, "+ %s", c.fixNewLine.c_str());
            PopX360Font(g_fontSmall);
            Hint(L4("Une seule ligne change ; l'ancien fichier est gardé en launch.ini.bak.", "Only one line changes; the old file is kept as launch.ini.bak.",
                    "Solo cambia una línea; el archivo anterior se guarda como launch.ini.bak.", "Só uma linha muda; o arquivo anterior fica em launch.ini.bak."));
            ImGui::PushStyleColor(ImGuiCol_Text, kGold);
            if (ImGui::Button(L4("Oui, modifier cette ligne", "Yes, change this line", "Sí, cambiar esta línea", "Sim, alterar esta linha")) && !busy)
                consync::ApplyLaunchIniFix();
            ImGui::PopStyleColor();
        } else if (c.slotsFull) {
            ImGui::TextColored(kRed, "%s", L4("Les 5 emplacements de plugins de DashLaunch sont pris : X360Remap ne peut pas être ajouté. Rien n'a été modifié.",
                                              "The 5 DashLaunch plugin slots are taken: X360Remap cannot be added. Nothing was changed.",
                                              "Los 5 espacios de plugins de DashLaunch están ocupados: no se puede añadir X360Remap. No se cambió nada.",
                                              "Os 5 espaços de plugins do DashLaunch estão ocupados: o X360Remap não pode ser adicionado. Nada foi alterado."));
        } else if (!c.iniFound) {
            ImGui::TextColored(kRed, "%s", L4("launch.ini introuvable : DashLaunch ne semble pas installé sur cette console.",
                                              "launch.ini not found: DashLaunch does not seem to be installed on this console.",
                                              "launch.ini no encontrado: DashLaunch no parece instalado en esta consola.",
                                              "launch.ini não encontrado: o DashLaunch não parece instalado neste console."));
        } else if (busy) {
            ImGui::TextColored(kDim, "%s", L4("Vérification...", "Checking...", "Comprobando...", "Verificando..."));
        }
        ImGui::Unindent(30.0f * g_k);
    }

    // 4. Aurora -----------------------------------------------------------------------
    WizardStepTitle(4, sAurora ? 2 : current == 4 ? 1 : 0, L4("Mettre la Studio dans les applications d'Aurora", "Put the Studio in Aurora's applications",
                                                                 "Poner el Studio en las aplicaciones de Aurora", "Colocar o Studio nos aplicativos do Aurora"));
    if (current == 4) {
        ImGui::Indent(30.0f * g_k);
        if (!st.auroraScriptWritten) {
            if (ImGui::Button(L4("Préparer le script pour Aurora", "Prepare the script for Aurora", "Preparar el script para Aurora", "Preparar o script para o Aurora")) && !busy)
                consync::InstallAuroraSetupScript();
        } else {
            Hint(L4("Sur la console, dans Aurora : ouvre les scripts utilitaires, lance « X360Remap Setup » et réponds Oui. Puis clique « Vérifier » ici.",
                    "On the console, in Aurora: open the utility scripts, run \"X360Remap Setup\" and answer Yes. Then click \"Check\" here.",
                    "En la consola, en Aurora: abre los scripts de utilidades, ejecuta «X360Remap Setup» y responde Sí. Luego pulsa «Comprobar» aquí.",
                    "No console, no Aurora: abra os scripts utilitários, execute \"X360Remap Setup\" e responda Sim. Depois clique em \"Verificar\" aqui."));
            if (ImGui::Button(L4("Vérifier", "Check", "Comprobar", "Verificar")) && !busy) consync::CheckInstallation();
        }
        ImGui::SameLine();
        if (ImGui::Button(L4("Passer (je lance la Studio autrement)", "Skip (I start the Studio another way)", "Saltar (inicio el Studio de otra forma)", "Pular (abro o Studio de outro jeito)")))
            s_skipAurora = true;
        if (st.checked && !c.auroraChecked)
            ImGui::TextColored(kDim, "%s", L4("Les réglages d'Aurora n'ont pas pu être lus : vérifie à la main après le redémarrage.",
                                              "Aurora's settings could not be read: check by hand after the restart.",
                                              "No se pudieron leer los ajustes de Aurora: compruébalo a mano después del reinicio.",
                                              "Não foi possível ler as configurações do Aurora: verifique à mão após reiniciar."));
        ImGui::Unindent(30.0f * g_k);
    }

    // 5. Reboot + check ----------------------------------------------------------------
    WizardStepTitle(5, sReboot ? 2 : current == 5 ? 1 : 0, L4("Redémarrer la console et vérifier", "Restart the console and check",
                                                                 "Reiniciar la consola y comprobar", "Reiniciar o console e verificar"));
    if (current == 5) {
        ImGui::Indent(30.0f * g_k);
        const int rb = st.reboot;
        if (rb == consync::REBOOT_WAITING) {
            ImGui::TextColored(kGold, "%s", L4("Redémarrage... la console revient dans une minute environ.", "Restarting... the console comes back in about a minute.",
                                               "Reiniciando... la consola vuelve en un minuto aproximadamente.", "Reiniciando... o console volta em cerca de um minuto."));
        } else if (!s_askReboot) {
            if (ImGui::Button(L4("Redémarrer la console", "Restart the console", "Reiniciar la consola", "Reiniciar o console")) && !busy) s_askReboot = true;
            if (rb == consync::REBOOT_NO_XBDM)
                ImGui::TextColored(kRed, "%s", L4("La console n'accepte pas le redémarrage à distance (xbdm absent). Redémarre-la toi-même, puis clique « Vérifier ».",
                                                  "The console does not accept a remote restart (no xbdm). Restart it yourself, then click \"Check\".",
                                                  "La consola no acepta el reinicio remoto (sin xbdm). Reiníciala tú mismo y luego pulsa «Comprobar».",
                                                  "O console não aceita reinício remoto (sem xbdm). Reinicie-o você mesmo e clique em \"Verificar\"."));
            if (rb == consync::REBOOT_NO_PLUGIN)
                ImGui::TextColored(kRed, "%s", L4("La console est revenue mais le plugin n'a pas démarré. Ouvre « Installation et réparation » pour voir la cause.",
                                                  "The console is back but the plugin did not start. Open \"Install & repair\" to see the cause.",
                                                  "La consola volvió pero el plugin no arrancó. Abre «Instalación y reparación» para ver la causa.",
                                                  "O console voltou, mas o plugin não iniciou. Abra \"Instalação e reparo\" para ver a causa."));
            if (rb == consync::REBOOT_NOT_BACK)
                ImGui::TextColored(kRed, "%s", L4("La console ne répond pas encore. Vérifie qu'elle est revenue sur Aurora, puis réessaie.",
                                                  "The console does not answer yet. Check it is back in Aurora, then try again.",
                                                  "La consola aún no responde. Comprueba que volvió a Aurora y reinténtalo.",
                                                  "O console ainda não responde. Verifique se voltou ao Aurora e tente de novo."));
        } else {
            ImGui::TextColored(kGold, "%s", L4("Un jeu en cours sera fermé. Redémarrer maintenant ?", "A game in progress will be closed. Restart now?",
                                               "Se cerrará un juego en curso. ¿Reiniciar ahora?", "Um jogo em andamento será fechado. Reiniciar agora?"));
            if (ImGui::Button(L4("Oui, redémarrer", "Yes, restart", "Sí, reiniciar", "Sim, reiniciar")) && !busy) { s_askReboot = false; consync::RebootAndWait(); }
            ImGui::SameLine();
            if (ImGui::Button(TxtCancel())) s_askReboot = false;
        }
        ImGui::Unindent(30.0f * g_k);
    }

    // Done -----------------------------------------------------------------------------
    if (current == 6) {
        ImGui::Spacing();
        ImGui::TextColored(kGreen, "%s %s", L4("X360Remap est installé et tourne : plugin", "X360Remap is installed and running: plugin",
                                               "X360Remap está instalado y funciona: plugin", "O X360Remap está instalado e funcionando: plugin"),
                           st.pluginVersionAfter.c_str());
        if (ImGui::Button(L4("Régler mon clavier et ma souris", "Set up my keyboard and mouse", "Configurar mi teclado y ratón", "Configurar meu teclado e mouse"))) {
            ModelSetTab(0);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

// ============================================================================
// Dialog: every shortcut (PLAN_v1.3.md, section 3)
// ============================================================================
struct ShortcutRow { const char* key; const char* fr; const char* en; const char* es; const char* pt; };
const ShortcutRow kShortcuts[] = {
    { "F1 / F2", "Lissage - / +", "Smoothing - / +", "Suavizado - / +", "Suavização - / +" },
    { "F3 / F4", "Anti-zone morte verticale - / +", "Vertical anti-dead zone - / +", "Anti zona muerta vertical - / +", "Anti zona morta vertical - / +" },
    { "F5 / F6", "Sensibilité - / +", "Sensitivity - / +", "Sensibilidad - / +", "Sensibilidade - / +" },
    { "F7 / F8", "Anti-zone morte horizontale - / +", "Horizontal anti-dead zone - / +", "Anti zona muerta horizontal - / +", "Anti zona morta horizontal - / +" },
    { "F9 (hold)", "Enregistrer les réglages dans le profil du jeu", "Save the settings into the game's profile",
      "Guardar los ajustes en el perfil del juego", "Salvar as configurações no perfil do jogo" },
    { "F10 / F11", "Zone morte - / +", "Dead zone - / +", "Zona muerta - / +", "Zona morta - / +" },
    { "F12 (hold 1.5 s)", "Recherche automatique de la caméra", "Automatic camera search", "Búsqueda automática de la cámara", "Busca automática da câmera" },
    { "F12", "Pendant la confirmation : garder la caméra trouvée", "During the confirmation: keep the camera found",
      "Durante la confirmación: conservar la cámara encontrada", "Durante a confirmação: manter a câmera encontrada" },
    { "Backspace", "Pendant la recherche : essayer la suivante / annuler", "During the search: try the next one / cancel",
      "Durante la búsqueda: probar la siguiente / cancelar", "Durante a busca: tentar a próxima / cancelar" },
    { "BACK + START (hold)", "Manette, en jeu : liaison rapide d'un bouton", "Controller, in game: quick bind of a button",
      "Mando, en el juego: asignación rápida de un botón", "Controle, no jogo: atribuição rápida de um botão" },
    { "Right Alt", "Bouton Guide", "Guide button", "Botón Guide", "Botão Guide" },
    { "Scroll Lock", "Joueurs séparés (par défaut, clavier/souris = joueur 1) / fusion, gardé dans players_merged.txt",
      "Players separate (default, keyboard/mouse = player 1) / merged, kept in players_merged.txt",
      "Jugadores separados (por defecto, teclado/ratón = jugador 1) / unidos, guardado en players_merged.txt",
      "Jogadores separados (padrão, teclado/mouse = jogador 1) / mesclados, guardado em players_merged.txt" },
    { "Esc x2", "Studio de la console : retour à Aurora", "Console Studio: back to Aurora", "Studio de la consola: volver a Aurora", "Studio do console: voltar ao Aurora" },
    { "Win + Shift + X", "X360Remap en pause / actif (la vraie manette pilote le joueur 1)", "X360Remap paused / active (the real controller drives player 1)",
      "X360Remap en pausa / activo (el mando real controla al jugador 1)", "X360Remap em pausa / ativo (o controle real comanda o jogador 1)" },
    { "Shift + Alt + X", "Même interrupteur, pour les claviers sans touche Windows",
      "Same switch, for keyboards without a Windows key",
      "Mismo interruptor, para teclados sin tecla Windows",
      "Mesmo interruptor, para teclados sem tecla Windows" },
};
// Shown only when a keypad with this layout is among the console's devices.
const ShortcutRow kKeypadShortcuts[] = {
    { "Keypad 16 + thumb + 18", "X360Remap en pause / actif (ce pavé envoie Maj + Alt + X)", "X360Remap paused / active (this keypad sends Shift + Alt + X)",
      "X360Remap en pausa / activo (este teclado envía Mayús + Alt + X)", "X360Remap em pausa / ativo (este keypad envia Shift + Alt + X)" },
    { "Keypad 01 / 05 / 11 / 17", "START / Guide / R3 / RT (préréglage, sauf touches assignées)", "START / Guide / R3 / RT (preset, unless assigned)",
      "START / Guide / R3 / RT (preajuste, salvo teclas asignadas)", "START / Guide / R3 / RT (predefinição, salvo teclas atribuídas)" },
};

void DrawShortcutsDialog() {
    if (g_openShortcuts) { ImGui::OpenPopup("##dlg_shortcuts"); g_openShortcuts = false; }
    CenterNext(0, 0);
    if (!ImGui::BeginPopupModal("##dlg_shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    DialogCloseCross();
    PushX360Font(g_fontTitle);
    ImGui::TextUnformatted(L4("Tous les raccourcis", "Every shortcut", "Todos los atajos", "Todos os atalhos"));
    PopX360Font(g_fontTitle);
    ImGui::Spacing();
    PushX360Font(g_fontSmall);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * g_k, 7.0f * g_k));
    for (size_t i = 0; i < sizeof(kShortcuts) / sizeof(kShortcuts[0]); i++) {
        ImGui::TextColored(kGold, "%s", kShortcuts[i].key);
        ImGui::SameLine(230.0f * g_k);
        ImGui::TextUnformatted(L4(kShortcuts[i].fr, kShortcuts[i].en, kShortcuts[i].es, kShortcuts[i].pt));
    }
    if (ModelKnownKeypadPresent()) {
        for (size_t i = 0; i < sizeof(kKeypadShortcuts) / sizeof(kKeypadShortcuts[0]); i++) {
            ImGui::TextColored(kGold, "%s", kKeypadShortcuts[i].key);
            ImGui::SameLine(230.0f * g_k);
            ImGui::TextUnformatted(L4(kKeypadShortcuts[i].fr, kKeypadShortcuts[i].en, kKeypadShortcuts[i].es, kKeypadShortcuts[i].pt));
        }
    }
    ImGui::PopStyleVar();
    PopX360Font(g_fontSmall);
    ImGui::EndPopup();
}

// ============================================================================
// Status bar
// ============================================================================
void DrawStatusText() {
    const WinSettings& s = plat::Settings();
    const consync::LinkState state = consync::State();
    const bool busy = consync::Busy();
    char line[256];
    ImVec4 col = kDim;
    if (busy) {
        _snprintf(line, sizeof(line) - 1, "%s %s - %s", L4("Console", "Console", "Consola", "Console"), s.consoleIp,
                  L4("échange en cours...", "exchange in progress...", "intercambio en curso...", "troca em andamento..."));
        col = kGold;
    } else if (state == consync::LINK_CONNECTED) {
        if (!consync::PluginVersion().empty() && consync::DashboardBuild() > 0)
            _snprintf(line, sizeof(line) - 1, "%s %s - %s - plugin %s - dashboard %d", L4("Console", "Console", "Consola", "Console"),
                      s.consoleIp, L4("connectée", "connected", "conectada", "conectado"), consync::PluginVersion().c_str(),
                      consync::DashboardBuild());
        else if (!consync::PluginVersion().empty())
            _snprintf(line, sizeof(line) - 1, "%s %s - %s - plugin %s", L4("Console", "Console", "Consola", "Console"), s.consoleIp,
                      L4("connectée", "connected", "conectada", "conectado"), consync::PluginVersion().c_str());
        else
            _snprintf(line, sizeof(line) - 1, "%s %s - %s - %s", L4("Console", "Console", "Consola", "Console"), s.consoleIp,
                      L4("connectée", "connected", "conectada", "conectado"),
                      L4("plugin : version inconnue (aucun journal reçu)", "plugin: version unknown (no log received)",
                         "plugin: versión desconocida (ningún registro recibido)", "plugin: versão desconhecida (nenhum registro recebido)"));
        col = kGreen;
    } else if (state == consync::LINK_ERROR) {
        _snprintf(line, sizeof(line) - 1, "%s %s - %s", L4("Console", "Console", "Consola", "Console"), s.consoleIp,
                  L4("injoignable", "unreachable", "inaccesible", "inacessível"));
        col = kRed;
    } else {
        _snprintf(line, sizeof(line) - 1, "%s", L4("Hors ligne - fichier local", "Offline - local file", "Sin conexión - archivo local", "Offline - arquivo local"));
    }
    line[sizeof(line) - 1] = 0;

    // The whole status text is the button that opens the connection settings.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float dot = 5.0f * g_k, h = ImGui::GetFrameHeight();
    const ImVec2 textSize = ImGui::CalcTextSize(line);
    if (ImGui::InvisibleButton("##status", ImVec2(textSize.x + dot * 2.0f + 14.0f * g_k, h))) g_openConnection = true;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(pos.x + dot + 2.0f, pos.y + h * 0.5f), dot, ImGui::GetColorU32(col), 16);
    dl->AddText(ImVec2(pos.x + dot * 2.0f + 10.0f * g_k, pos.y + (h - textSize.y) * 0.5f),
                ImGui::GetColorU32(hovered ? ImVec4(1, 1, 1, 1) : col), line);
    if (hovered) ImGui::SetTooltip("%s", L4("Réglages de connexion", "Connection settings", "Ajustes de conexión", "Configurações de conexão"));

    if (consync::UpdateInfo().available) {
        ImGui::SameLine(0.0f, 18.0f * g_k);
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        char upd[160];
        _snprintf(upd, sizeof(upd) - 1, "%s %s", L4("Nouvelle version :", "New version:", "Nueva versión:", "Nova versão:"),
                  consync::UpdateInfo().release.name.c_str());
        upd[sizeof(upd) - 1] = 0;
        if (ImGui::SmallButton(upd)) g_openInstall = true;
        ImGui::PopStyleColor();
    }
    if (consync::Dirty()) {
        ImGui::SameLine(0.0f, 18.0f * g_k);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kGold, "%s", L4("Changements non envoyés", "Changes not sent", "Cambios sin enviar", "Alterações não enviadas"));
    }
}

void DrawStatusBar(float canvasW, float canvasH) {
    const float barH = UiWinBarHeight(canvasW);
    ImGui::SetNextWindowPos(ImVec2(0.0f, canvasH));
    ImGui::SetNextWindowSize(ImVec2(canvasW, barH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * g_k, 4.0f * g_k));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * g_k, 4.0f * g_k));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * g_k, 4.0f * g_k));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.055f, 0.085f, 1.0f));
    ImGui::Begin("##statusbar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    PushX360Font(g_fontSmall);

    DrawStatusText();

    // Buttons, right-aligned: [page tools] [Receive] [Send] [File] [Logs] [Advanced]
    const int tab = ModelActiveTab();
    const char* toolLabel = tab == 0 ? L4("Souris avancé...", "Mouse advanced...", "Ratón avanzado...", "Mouse avançado...")
                          : tab == 1 ? L4("Visée directe du jeu...", "Game direct aim...", "Apuntado directo del juego...", "Mira direta do jogo...")
                          : tab <= 3 ? L4("Raccourcis", "Shortcuts", "Atajos", "Atalhos")   // in-game display and how-to pages
                                     : L4("Liens", "Links", "Enlaces", "Links");
    const char* labels[6] = {
        toolLabel,
        L4("Recevoir", "Receive", "Recibir", "Receber"),
        L4("Envoyer", "Send", "Enviar", "Enviar"),
        L4("Fichier", "File", "Archivo", "Arquivo"),
        L4("Journaux", "Logs", "Registros", "Registros"),
        L4("Avancé", "Advanced", "Avanzado", "Avançado"),
    };
    const ImGuiStyle& st = ImGui::GetStyle();
    float total = 0.0f;
    for (int i = 0; i < 6; i++) total += ImGui::CalcTextSize(labels[i]).x + st.FramePadding.x * 2.0f + (i ? st.ItemSpacing.x : 0.0f);
    ImGui::SameLine(canvasW - total - st.WindowPadding.x);

    const bool busy = consync::Busy();
    if (ImGui::Button(labels[0])) {
        if (tab == 0) g_openMouseExtra = true;
        else if (tab == 1) g_openGameAim = true;
        else if (tab <= 3) g_openShortcuts = true;
        else ImGui::OpenPopup("##tools_menu");
    }
    if (ImGui::BeginPopup("##tools_menu")) {
        {
            if (IsLinkIntact(kKofiUrl, kKofiUrlHash) && ImGui::Selectable("Ko-fi")) plat::OpenUrl(kKofiUrl);
            if (IsLinkIntact(kGithubUrl, kGithubUrlHash) && ImGui::Selectable("GitHub - X360Remap")) plat::OpenUrl(kGithubUrl);
            if (IsLinkIntact(kReleasesUrl, kReleasesUrlHash) &&
                ImGui::Selectable(L4("Dernière version", "Latest version", "Última versión", "Última versão"))) plat::OpenUrl(kReleasesUrl);
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(labels[1]) && !busy) {
        if (plat::Settings().consoleIp[0]) consync::Pull();
        else g_openConnection = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L4("Charger les réglages de la console", "Load the settings of the console",
                                   "Cargar los ajustes de la consola", "Carregar as configurações do console"));
    ImGui::SameLine();
    const bool dirty = consync::Dirty();
    if (dirty) ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    const bool send = ImGui::Button(labels[2]);
    if (dirty) ImGui::PopStyleColor();
    if (send && !busy) {
        if (plat::Settings().consoleIp[0]) consync::Push();
        else g_openConnection = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L4("Envoyer les réglages à la console (vérifié, ancienne version gardée en .bak)",
                                   "Send the settings to the console (verified, previous version kept as .bak)",
                                   "Enviar los ajustes a la consola (verificado, versión anterior guardada como .bak)",
                                   "Enviar as configurações ao console (verificado, versão anterior mantida como .bak)"));
    ImGui::SameLine();
    if (ImGui::Button(labels[3])) ImGui::OpenPopup("##file_menu");
    if (ImGui::BeginPopup("##file_menu")) {
        if (ImGui::Selectable(L4("Installer X360Remap sur la console (assistant)...", "Install X360Remap on the console (wizard)...",
                                 "Instalar X360Remap en la consola (asistente)...", "Instalar o X360Remap no console (assistente)..."))) g_openWizard = true;
        if (ImGui::Selectable(L4("Ouvrir un X360Remap.json...", "Open an X360Remap.json...", "Abrir un X360Remap.json...", "Abrir um X360Remap.json..."))) OpenLocalConfig();
        if (ImGui::Selectable(L4("Enregistrer une copie...", "Save a copy...", "Guardar una copia...", "Salvar uma cópia..."))) SaveLocalCopy();
        if (ImGui::Selectable(L4("Connexion à la console...", "Console connection...", "Conexión con la consola...", "Conexão com o console..."))) g_openConnection = true;
        if (ImGui::Selectable(L4("Installation et réparation...", "Install & repair...", "Instalación y reparación...", "Instalação e reparo..."))) g_openInstall = true;
        if (tab == 1) {
            if (ImGui::Selectable(L4("Exporter le profil du jeu...", "Export the game's profile...", "Exportar el perfil del juego...", "Exportar o perfil do jogo..."))) ExportSelectedProfile();
            if (ImGui::Selectable(L4("Importer un profil de jeu...", "Import a game profile...", "Importar un perfil de juego...", "Importar um perfil de jogo..."))) ImportProfileFile();
        }
        if (ImGui::Selectable(L4("Périphériques du PC...", "Devices of this PC...", "Dispositivos del PC...", "Dispositivos do PC..."))) g_openPcDevices = true;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(labels[4])) g_openLogs = true;
    ImGui::SameLine();
    if (ImGui::Button(labels[5])) g_openAdvanced = true;

    // The dialogs are opened from this window so that they share its ID stack,
    // but with the normal theme, not the compact style of the bar.
    PopX360Font(g_fontSmall);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);
    DrawConnectionDialog();
    DrawConflictDialog();
    DrawMouseExtraDialog();
    DrawGameAimDialog();
    DrawCamFinderDialog();
    DrawLogsDialog();
    DrawAdvancedDialog();
    DrawShortcutsDialog();
    DrawInstallDialog();
    DrawPcDevicesDialog();
    DrawInstallWizard();

    ImGui::End();
}

}  // namespace

float UiWinBarHeight(float canvasW) { return 36.0f * (canvasW / 1280.0f); }

void UiWinOpenDialog(const char* name) {
    if (!name) return;
    if (strcmp(name, "connection") == 0) g_openConnection = true;
    else if (strcmp(name, "aim") == 0) g_openMouseExtra = true;
    else if (strcmp(name, "game") == 0) g_openGameAim = true;
    else if (strcmp(name, "camfinder") == 0) g_openCamFinder = true;
    else if (strncmp(name, "camfinder-", 10) == 0) { g_openCamFinder = true; g_camDemo = name + 10; }
    else if (strcmp(name, "logs") == 0) g_openLogs = true;
    else if (strcmp(name, "advanced") == 0) g_openAdvanced = true;
    else if (strcmp(name, "shortcuts") == 0) g_openShortcuts = true;
    else if (strcmp(name, "install") == 0) g_openInstall = true;
    else if (strcmp(name, "pcdevices") == 0) g_openPcDevices = true;
    else if (strcmp(name, "wizard") == 0) g_openWizard = true;
    else if (strcmp(name, "keyboards") == 0) UiGlossyOpenDeviceMenu(0);   // screenshots of the device lists
    else if (strcmp(name, "mice") == 0) UiGlossyOpenDeviceMenu(1);
    else if (strcmp(name, "pad") == 0) ModelOpenPad(0);
}

void UiWinDraw(float canvasW, float canvasH, void* hwnd) {
    g_k = canvasW / 1280.0f;
    g_hwnd = hwnd;
    DrawStatusBar(canvasW, canvasH);
}
