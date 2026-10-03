#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// Portable core of the interactive mapping assistant - deliberately has NO
// dependency on any Xbox header (no xtl.h, no XNotifyUI, no Sleep/GetTickCount).
// It only knows about "codes" (a key HID code or a controller button index)
// and "steps" (which action a code gets bound to once resolved).
//
// Why this file exists: the original controller assistant (MappingThreadProc
// in main.cpp) has this exact state machine (press/release/hold-3s-to-skip)
// inlined twice, driven live by real hardware and XNotifyUI, which made it
// impossible to test without a console. This class extracts the same logic
// so it can be compiled and exercised on any platform (see tests/) before
// being wired into main.cpp for the keyboard/mouse config assistant.
//
// New behavior not present in the original controller assistant: conflict
// detection. A gamepad's button indices can't collide with each other, but a
// keyboard has far more actions than comfortably-reachable keys, so a user
// picking the same key twice is a real scenario the assistant needs to catch
// rather than silently produce a X360Remap.json where one key drives two
// actions at once.

// struct+enum imbrique plutot que `enum class` (C++11, non supporte par le
// toolset XDK Xbox 360 - erreur reelle obtenue au build : C2332/C2236/C3381)
// - cet idiome C++03 classique preserve la syntaxe qualifiee
// AssistantOutcome::Pending partout ou elle est deja utilisee (les valeurs
// d'un enum imbrique restent visibles via la portee de la structure
// englobante, meme sans les qualifier par ::Type). Seul le TYPE lui-meme
// (la ou "AssistantOutcome" designe un type de variable/retour, pas une
// valeur) doit s'ecrire AssistantOutcome::Type desormais - voir
// mapping_assistant.cpp et tests/test_mapping_assistant.cpp.
struct AssistantOutcome {
    enum Type {
        Pending,   // nothing resolved this tick
        Assigned,  // current step just got a code bound to it, moved to next step
        Skipped,   // current step was held 3s and skipped, moved to next step
        Conflict,  // pressed code is already bound to an earlier step this session
    };
};

struct AssistantStep {
    std::string label;  // shown to the user, e.g. "A", "D-Pad Up", "Left Stick Up"
    uint8_t action;      // KeyAction value (see mapping.h) this step assigns
};

struct AssistantResult {
    uint16_t code;
    uint8_t action;
};

class MappingAssistant {
public:
    // static const (pas constexpr, non supporte avant VS2015) - un entier
    // integral statique const initialise dans la classe reste autorise en
    // C++03/11 "classique" (c'est litteralement ce que dit l'erreur C2864
    // obtenue au premier build reel : "only static const integral data
    // members can be initialized within a class").
    static const int32_t NO_CODE = -1;

    // holdTicksToSkip matches the original 60 ticks @ 50ms/tick = 3s.
    explicit MappingAssistant(std::vector<AssistantStep> steps, uint32_t holdTicksToSkip = 60);

    // Feed one polling sample (call once per ~50ms tick, matching
    // MappingManagerThreadProc's cadence). code = the single currently-held
    // key/button code, or NO_CODE if nothing is pressed. Only one code at a
    // time is modeled, same as the original assistant (it reads
    // g_mappingState.pressedButtonIdx, a single slot).
    AssistantOutcome::Type Tick(int32_t code, std::string* outMessage = nullptr);

    void Cancel();
    bool IsActive() const { return active_; }
    bool IsComplete() const { return !cancelled_ && !active_ && stepIndex_ >= steps_.size(); }
    bool WasCancelled() const { return cancelled_; }

    const std::string& CurrentPrompt() const;
    const std::vector<AssistantResult>& Results() const { return results_; }

    // Pour un indicateur de progression cote UI ("etape 7/25") - permet de
    // voir en direct qu'une etape vient d'etre ignoree (skip) au lieu de
    // decouvrir apres coup, une fois le wizard "termine", que Results() est
    // vide ou incomplet (voir le bug de cadence corrige le 2026-08-02 :
    // holdTicksToSkip_ suppose un appelant a 50ms/tick, application.xex
    // appelle Tick() une fois par frame ~16ms, donc 3x plus vite que prevu).
    size_t CurrentStepIndex() const { return stepIndex_; }
    size_t TotalSteps() const { return steps_.size(); }

private:
    std::vector<AssistantStep> steps_;
    std::vector<AssistantResult> results_;

    // Initialisation via constructeur plutot que des valeurs par defaut
    // dans la classe (NSDMI, C++11, non supporte - meme raison et meme
    // erreur reelle C2864 que InputStateSnapshot, voir
    // application/input_state_reader.h) - voir le constructeur dans
    // mapping_assistant.cpp.
    size_t stepIndex_;
    uint32_t holdTicksToSkip_;

    int32_t prevCode_;
    uint32_t pressWaitCount_;
    uint32_t holdCount_;
    bool active_;
    bool cancelled_;
    bool awaitingRelease_;

    bool IsCodeAlreadyUsed(int32_t code) const;
};
