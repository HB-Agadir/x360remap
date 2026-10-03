#pragma once
#include "../X360Remap/mapping.h"
#include "../X360Remap/mapping_assistant.h"
#include <string>
#include <vector>
#include <stdint.h>

// Ties together MappingAssistant (portable state machine, already tested in
// X360Remap/tests/) with the config file it eventually saves to. Deliberately
// has NO XUI/D3D call anywhere in it, so it can be driven by a test harness
// exactly like MappingAssistant already is - only main.cpp (the actual XUI
// screen) touches rendering, and its only job is to call Tick() once per
// frame and display whatever strings come back.
class WizardSession {
public:
    // holdTicksToSkip par defaut = 60, calibre pour l'assistant manette
    // original (cadence 50ms/tick dans MappingManagerThreadProc, voir
    // mapping_assistant.h) - a NE PAS utiliser tel quel pour un wizard pilote
    // par ImGui/application.xex, qui appelle Tick() une fois par frame rendue
    // (~16ms a 60fps, donc ~3x plus vite). Voir DrawWizardTab() dans
    // application/main.cpp, qui passe explicitement 180 (60fps*3s) plutot
    // que le defaut - bug reel du 2026-08-02 : avec 60, une etape se faisait
    // ignorer (skip) apres ~1s au lieu des "3 secondes" annoncees a l'ecran,
    // ce qui pouvait vider silencieusement tout le resultat du wizard si
    // l'utilisateur relachait chaque touche un peu tard.
    WizardSession(uint16_t vid, uint16_t pid, std::vector<AssistantStep> steps,
                  uint32_t holdTicksToSkip = 60);

    // Call once per frame with the live key/button code for the device kind
    // being configured (see ReadInputState - keyCode for a keyboard wizard).
    // promptOut/messageOut are optional out-params for what the screen
    // should currently display.
    void Tick(int32_t inputCode, std::string* promptOut = nullptr, std::string* messageOut = nullptr);

    bool IsActive() const { return assistant_.IsActive(); }
    bool IsComplete() const { return assistant_.IsComplete(); }
    bool WasCancelled() const { return assistant_.WasCancelled(); }
    void Cancel() { assistant_.Cancel(); }

    // Pour afficher "etape X/25" a l'ecran pendant le wizard - voir
    // DrawWizardTab() dans application/main.cpp. Permet de voir en direct
    // qu'une etape a ete sautee au lieu de le decouvrir seulement une fois
    // le wizard termine.
    size_t CurrentStepIndex() const { return assistant_.CurrentStepIndex(); }
    size_t TotalSteps() const { return assistant_.TotalSteps(); }
    size_t ResultsCount() const { return assistant_.Results().size(); }

    // Only meaningful once IsComplete() is true - saves the assistant's
    // result to X360Remap.json (or configPathOverride, for tests).
    bool SaveResult(const char* configPathOverride = nullptr);

    // Expose les resultats bruts (code+action) sans passer par SaveResult() -
    // ajoute pour les reglages manuels (2026-08-17) : une session d'un seul
    // pas doit pouvoir FUSIONNER son unique resultat dans le mapping deja sur
    // le disque (voir ApplyKeyboardMappingEntry, application/config_writer.h)
    // au lieu d'ecraser tout le keyMap comme le fait SaveResult(), pense pour
    // le wizard sequentiel qui reconstruit la liste entiere a chaque session.
    const std::vector<AssistantResult>& Results() const { return assistant_.Results(); }

private:
    uint16_t vid_;
    uint16_t pid_;
    MappingAssistant assistant_;
};

// The default keyboard wizard sequence - one step per KeyAction the JSON
// schema supports (see mapping.h). Kept here (not inlined at every call
// site) so the XUI screen, the test harness, and any future "just remap one
// button" flow all walk the exact same list.
std::vector<AssistantStep> DefaultKeyboardWizardSteps();
