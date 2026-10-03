#include "mapping_assistant.h"

MappingAssistant::MappingAssistant(std::vector<AssistantStep> steps, uint32_t holdTicksToSkip)
    : steps_(std::move(steps)), stepIndex_(0), holdTicksToSkip_(holdTicksToSkip),
      prevCode_(NO_CODE), pressWaitCount_(0), holdCount_(0),
      active_(false), cancelled_(false), awaitingRelease_(false) {
    // Liste d'initialisation plutot que des NSDMI sur les membres (voir
    // mapping_assistant.h) - active_ mis a jour juste apres selon
    // steps_.empty(), comme avant.
    active_ = !steps_.empty();
}

bool MappingAssistant::IsCodeAlreadyUsed(int32_t code) const {
    // Boucle par iterateur plutot qu'un for-range (C++11, ajoute par MSVC en
    // VS2012 seulement) - le toolset XDK Xbox 360 (VS2010) ne le supporte
    // pas, confirme par l'erreur C2864 (NSDMI, feature VS2013) obtenue au
    // premier vrai build de application.vcxproj le 2026-08-01 : puisque
    // NSDMI echoue deja, ce toolset est bien VS2010 pur, donc range-for
    // (posterieur) echouerait aussi. Corrige preventivement ici plutot que
    // d'attendre un nouveau round d'erreurs pour la meme categorie de bug.
    for (std::vector<AssistantResult>::const_iterator it = results_.begin(); it != results_.end(); ++it) {
        if (it->code == static_cast<uint16_t>(code))
            return true;
    }
    return false;
}

const std::string& MappingAssistant::CurrentPrompt() const {
    static const std::string empty;
    if (stepIndex_ >= steps_.size())
        return empty;
    return steps_[stepIndex_].label;
}

void MappingAssistant::Cancel() {
    cancelled_ = true;
    active_ = false;
}

AssistantOutcome::Type MappingAssistant::Tick(int32_t code, std::string* outMessage) {
    if (outMessage)
        outMessage->clear();

    if (!active_ || stepIndex_ >= steps_.size()) {
        active_ = false;
        return AssistantOutcome::Pending;
    }

    // After a skip, the code that triggered the hold must be released before
    // the next step starts - otherwise the same held key would immediately
    // register as the press for the following step too.
    if (awaitingRelease_) {
        if (code == NO_CODE) {
            awaitingRelease_ = false;
            prevCode_ = NO_CODE;
        }
        return AssistantOutcome::Pending;
    }

    // Conflict guard: a code already bound to an earlier step in this
    // session can't be pressed again. Treat it as "nothing pressed" for the
    // state machine, but surface it once (not every tick it's held).
    if (code != NO_CODE && IsCodeAlreadyUsed(code)) {
        AssistantOutcome::Type outcome = AssistantOutcome::Pending;
        if (prevCode_ != code) {
            outcome = AssistantOutcome::Conflict;
            if (outMessage)
                *outMessage = "Deja assignee a une autre action - choisis une autre touche";
        }
        prevCode_ = code;
        return outcome;
    }

    AssistantOutcome::Type outcome = AssistantOutcome::Pending;

    if (prevCode_ == NO_CODE && code != NO_CODE) {
        // Just pressed.
        pressWaitCount_ = 0;
        holdCount_ = 0;
    } else if (prevCode_ != NO_CODE && code == NO_CODE) {
        // Just released.
        if (pressWaitCount_ > 0) {
            // Variable nommee + aggregate init plutot qu'un brace-init passe
            // directement a push_back (C++11, non supporte par le toolset
            // XDK Xbox 360 - voir les autres corrections de ce fichier et
            // application/wizard_session.cpp pour le contexte complet).
            AssistantResult result;
            result.code = static_cast<uint16_t>(prevCode_);
            result.action = steps_[stepIndex_].action;
            results_.push_back(result);
            outcome = AssistantOutcome::Assigned;
            stepIndex_++;
        }
        pressWaitCount_ = 0;
        holdCount_ = 0;
    } else if (code != NO_CODE) {
        // Held.
        pressWaitCount_++;
        holdCount_++;
        if (holdCount_ >= holdTicksToSkip_) {
            outcome = AssistantOutcome::Skipped;
            stepIndex_++;
            awaitingRelease_ = true;
            if (outMessage)
                *outMessage = "Etape ignoree";
        }
    }

    prevCode_ = code;

    if (stepIndex_ >= steps_.size())
        active_ = false;

    return outcome;
}
