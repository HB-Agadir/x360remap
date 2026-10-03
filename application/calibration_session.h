#pragma once
#include "../X360Remap/mouse_calibration.h"
#include <stdint.h>
#include <string>
#include <vector>

// --- Machine a etats de l'assistant de calibration souris (2026-08-03) -----
//
// Meme role que WizardSession pour le clavier : toute la logique de
// l'assistant vit ICI, hors de main.cpp, pour etre compilee et executee pour
// de vrai en g++ (application/tests/test_calibration_session.cpp). main.cpp
// ne fait que dessiner ce que cette classe expose et lui transmettre les
// octets bruts lus dans input_state.json - il reste la seule partie non
// testable, et donc la plus mince possible.
//
// Principe rappele : on ne devine pas la disposition du paquet, on la fait
// decouvrir par l'utilisateur (voir mouse_calibration.h).

enum CalibrationStep {
    CALSTEP_REST = 0,      // "ne touche a rien"
    CALSTEP_RIGHT,         // "bouge franchement vers la DROITE"
    CALSTEP_LEFT,
    CALSTEP_DOWN,
    CALSTEP_UP,
    CALSTEP_WHEEL_FWD,     // "tourne la molette vers l'AVANT"
    CALSTEP_WHEEL_BACK,
    CALSTEP_CLICK_LEFT,    // "maintiens le clic GAUCHE"
    CALSTEP_DONE,
    CALSTEP_FAILED
};

// Nombre d'echantillons a collecter avant qu'une etape puisse etre validee.
// Les axes en demandent plus : c'est ce qui garantit un geste franc plutot
// qu'une secousse, et donc une detection fiable.
const int CALIB_SAMPLES_REST = 12;
const int CALIB_SAMPLES_MOTION = 10;
const int CALIB_SAMPLES_WHEEL = 3;   // un cran de molette ne produit que quelques rapports
const int CALIB_SAMPLES_CLICK = 6;
// Refonte 2026-09-25 (echec reel chez HB : "X=NON, bouton=non") :
const uint32_t CALIB_GRACE_MS        = 700;  // debut d'etape ignore : la main finit le geste precedent
const uint32_t CALIB_AUTO_ADVANCE_MS = 600;  // jauge pleine -> etape suivante toute seule

class CalibrationSession {
public:
    CalibrationSession(uint16_t vid, uint16_t pid);

    // Appelee a chaque image avec le dernier paquet brut lu dans
    // input_state.json. `tick` sert a ignorer les paquets identiques
    // (le plugin reecrit le fichier meme quand rien n'a change).
    // Ne collecte que si l'echantillon apporte une information utile pour
    // l'etape en cours - voir SampleIsUsefulForStep.
    //
    // seq/hasSeq (2026-09-25) : numero du rapport souris ecrit par le plugin
    // (input_state.json, "rawSeq"). Quand il est connu :
    //  - un geste ne compte que sur un NOUVEAU rapport (plus de paquet
    //    perime compte dix fois, ni reporte d'une etape sur la suivante) ;
    //  - le debut de chaque etape est ignore (CALIB_GRACE_MS) ;
    //  - l'etape suivante demarre toute seule quand la jauge est pleine
    //    (CALIB_AUTO_ADVANCE_MS) : plus besoin d'aller cliquer "Valider" avec
    //    la souris, ce trajet salissait les donnees du geste.
    // Sans seq (ancien plugin), comportement d'avant.
    void Feed(const uint8_t* raw, uint8_t len, uint32_t tick, uint32_t seq = 0, bool hasSeq = false);

    // L'utilisateur valide l'etape (bouton A). Sans effet tant que
    // CanAdvance() est faux : c'est ce qui empeche d'enregistrer une
    // calibration ratee sans s'en rendre compte.
    void Advance();

    // Echappatoire pour les etapes molette : beaucoup de souris n'en ont pas,
    // et certaines n'en emettent rien meme quand elles en ont une. Sans cette
    // sortie, la jauge resterait vide et l'utilisateur serait bloque au
    // milieu de l'assistant. Saute directement aux boutons ; la disposition
    // produite sera valide, simplement sans molette. Sans effet en dehors des
    // deux etapes molette.
    void SkipWheel();

    // true quand l'etape en cours peut legitimement etre sautee (etapes
    // molette uniquement) - l'ecran n'affiche le bouton correspondant que
    // dans ce cas.
    bool CanSkip() const {
        return step_ == CALSTEP_WHEEL_FWD || step_ == CALSTEP_WHEEL_BACK;
    }

    void Cancel();

    CalibrationStep Step() const { return step_; }
    bool Finished() const { return step_ == CALSTEP_DONE || step_ == CALSTEP_FAILED; }
    bool Succeeded() const { return step_ == CALSTEP_DONE; }

    // Texte de consigne de l'etape en cours, affiche tel quel.
    const char* Prompt() const;

    // 0.0 a 1.0 : remplissage de la jauge affichee a l'ecran. Atteint 1.0
    // quand assez d'echantillons exploitables ont ete collectes.
    float Progress() const;
    bool CanAdvance() const { return Progress() >= 1.0f; }

    // Disposition construite une fois l'assistant termine. valid == false si
    // la detection n'a pas abouti (l'appelant ne doit alors rien ecrire).
    const HidReportLayout& Layout() const { return layout_; }

    uint16_t Vid() const { return vid_; }
    uint16_t Pid() const { return pid_; }

    // Resume lisible de ce qui a ete detecte, pour l'ecran final - permet a
    // l'utilisateur de voir ce qui a ete compris avant d'enregistrer.
    std::string Summary() const;

private:
    bool SampleIsUsefulForStep(const uint8_t* raw, uint8_t len) const;
    void ResetStepState();
    void DetectAxesEarly();
    int  NeededSamples() const;
    void FinishDetection();

    uint16_t vid_, pid_;
    CalibrationStep step_;
    uint32_t lastTick_;

    // Resultat par champ, pour dire a l'utilisateur CE QUI a echoue plutot
    // qu'un "recommence" sans information - retour du premier test reel, ou
    // l'echec ne donnait aucune piste.
    bool foundX_, foundY_, foundWheel_, foundButton_;

    std::vector<CalibrationSample> rest_, right_, left_, down_, up_,
                                   wheelFwd_, wheelBack_, click_;
    CalibrationBaseline baseline_;
    HidReportLayout layout_;
    uint8_t reportSize_;

    // Etat de l'etape en cours (2026-09-25)
    uint32_t stepStartTick_;   // 0 = pas encore vu de paquet dans cette etape
    uint32_t readyTick_;       // 0 = jauge pas encore pleine
    uint32_t lastSeq_;
    bool     haveSeq_;
    CalibrationSample lastAccepted_;
    bool     haveLastAccepted_;
    // Octets X/Y detectes a la fin des gestes de deplacement : pendant la
    // molette et le clic, un paquet qui ne differe du repos QUE sur ces
    // octets est un simple mouvement de la main, pas le geste demande.
    bool     axisBytes_[16];
};
