#include "calibration_session.h"
#include "i18n.h"
#include <stdio.h>
#include <string.h>

// Pont de compatibilite : `_snprintf` n'existe que chez MSVC, et le toolset
// XDK Xbox 360 (VS2010) n'a PAS `snprintf`, apparu seulement avec VS2015.
// Ce fichier devant compiler des deux cotes - g++ pour les tests portables,
// VS2010 pour la console - on nomme la fonction MSVC et on la fait pointer
// vers la version standard ailleurs. Meme esprit que les autres concessions
// au toolset 2010 documentees dans PROJECT_NOTES.md (pas de NSDMI, etc.).
#ifndef _MSC_VER
#define _snprintf snprintf
#endif

CalibrationSession::CalibrationSession(uint16_t vid, uint16_t pid)
    : vid_(vid), pid_(pid), step_(CALSTEP_REST), lastTick_(0),
      foundX_(false), foundY_(false), foundWheel_(false), foundButton_(false),
      reportSize_(0), stepStartTick_(0), readyTick_(0), lastSeq_(0), haveSeq_(false),
      haveLastAccepted_(false) {
    memset(&lastAccepted_, 0, sizeof(lastAccepted_));
    for (int i = 0; i < 16; i++) axisBytes_[i] = false;
    memset(&baseline_, 0, sizeof(baseline_));
    memset(&layout_, 0, sizeof(layout_));
    layout_.buttonsOffset = REPORT_FIELD_ABSENT;
    layout_.xOffset = REPORT_FIELD_ABSENT;
    layout_.yOffset = REPORT_FIELD_ABSENT;
    layout_.wheelOffset = REPORT_FIELD_ABSENT;
}

const char* CalibrationSession::Prompt() const {
    switch (step_) {
    case CALSTEP_REST:       return Tr(STR_CALSTEP_REST);
    case CALSTEP_RIGHT:      return Tr(STR_CALSTEP_RIGHT);
    case CALSTEP_LEFT:       return Tr(STR_CALSTEP_LEFT);
    case CALSTEP_DOWN:       return Tr(STR_CALSTEP_DOWN);
    case CALSTEP_UP:         return Tr(STR_CALSTEP_UP);
    case CALSTEP_WHEEL_FWD:  return Tr(STR_CALSTEP_WHEEL_FWD);
    case CALSTEP_WHEEL_BACK: return Tr(STR_CALSTEP_WHEEL_BACK);
    case CALSTEP_CLICK_LEFT: return Tr(STR_CALSTEP_CLICK_LEFT);
    case CALSTEP_DONE:       return Tr(STR_CALSTEP_DONE);
    case CALSTEP_FAILED:     return Tr(STR_CALSTEP_FAILED);
    }
    return "";
}

int CalibrationSession::NeededSamples() const {
    switch (step_) {
    case CALSTEP_REST:       return CALIB_SAMPLES_REST;
    case CALSTEP_WHEEL_FWD:
    case CALSTEP_WHEEL_BACK: return CALIB_SAMPLES_WHEEL;
    case CALSTEP_CLICK_LEFT: return CALIB_SAMPLES_CLICK;
    default:                 return CALIB_SAMPLES_MOTION;
    }
}

// Un echantillon ne compte que s'il porte l'information attendue. Sans ce
// filtre, la jauge se remplirait toute seule pendant que l'utilisateur ne
// fait rien - le plugin reecrit input_state.json en continu - et on
// enregistrerait une calibration vide en croyant qu'elle a reussi.
bool CalibrationSession::SampleIsUsefulForStep(const uint8_t* raw, uint8_t len) const {
    if (step_ == CALSTEP_REST)
        return true; // au repos, tout echantillon documente l'etat de repos

    if (baseline_.length == 0)
        return false;

    // Molette et clic : un changement limite aux octets X/Y deja identifies
    // n'est qu'un mouvement de la main, pas le geste demande (2026-09-25).
    const bool ignoreAxes = step_ == CALSTEP_WHEEL_FWD || step_ == CALSTEP_WHEEL_BACK ||
                            step_ == CALSTEP_CLICK_LEFT;
    uint8_t n = len < baseline_.length ? len : baseline_.length;
    for (uint8_t i = 0; i < n; i++) {
        if (ignoreAxes && axisBytes_[i])
            continue;
        if (raw[i] != baseline_.bytes[i])
            return true; // quelque chose a change par rapport au repos
    }
    return false;
}

void CalibrationSession::ResetStepState() {
    stepStartTick_ = 0;
    readyTick_ = 0;
    haveLastAccepted_ = false;
}

void CalibrationSession::Feed(const uint8_t* raw, uint8_t len, uint32_t tick, uint32_t seq, bool hasSeq) {
    if (Finished() || !raw || len == 0)
        return;
    // Le plugin reecrit le fichier a chaque tour de boucle meme sans nouveau
    // rapport : sans ce filtre, une meme ECRITURE serait lue a chaque image.
    if (tick == lastTick_)
        return;
    lastTick_ = tick;

    if (len > 16) len = 16;
    if (reportSize_ == 0) reportSize_ = len;

    const bool newReport = !hasSeq || !haveSeq_ || seq != lastSeq_;
    if (hasSeq) { lastSeq_ = seq; haveSeq_ = true; }

    if (stepStartTick_ == 0)
        stepStartTick_ = tick ? tick : 1;

    // Jauge pleine : on ne collecte plus rien (le trajet de la main vers un
    // bouton ne doit pas salir le geste) ; etape suivante toute seule.
    if (readyTick_ != 0) {
        if (hasSeq && (uint32_t)(tick - readyTick_) >= CALIB_AUTO_ADVANCE_MS)
            Advance();
        return;
    }

    if (hasSeq) {
        // Debut d'etape ignore : la main termine encore le geste precedent.
        if (step_ != CALSTEP_REST && (uint32_t)(tick - stepStartTick_) < CALIB_GRACE_MS)
            return;
        // Deplacements et molette : un NOUVEAU rapport seulement. Au clic,
        // un rapport deja accepte qui se repete compte aussi (bouton tenu :
        // la souris n'envoie plus rien tant qu'on ne bouge pas). Au repos,
        // tout compte (une souris immobile n'envoie rien).
        if (step_ == CALSTEP_CLICK_LEFT) {
            if (!newReport && !(haveLastAccepted_ && lastAccepted_.length == len &&
                                memcmp(lastAccepted_.bytes, raw, len) == 0))
                return;
        } else if (step_ != CALSTEP_REST && !newReport) {
            return;
        }
    }

    if (!SampleIsUsefulForStep(raw, len))
        return;

    CalibrationSample s;
    memset(&s, 0, sizeof(s));
    s.length = len;
    memcpy(s.bytes, raw, len);

    switch (step_) {
    case CALSTEP_REST:       rest_.push_back(s); break;
    case CALSTEP_RIGHT:      right_.push_back(s); break;
    case CALSTEP_LEFT:       left_.push_back(s); break;
    case CALSTEP_DOWN:       down_.push_back(s); break;
    case CALSTEP_UP:         up_.push_back(s); break;
    case CALSTEP_WHEEL_FWD:  wheelFwd_.push_back(s); break;
    case CALSTEP_WHEEL_BACK: wheelBack_.push_back(s); break;
    case CALSTEP_CLICK_LEFT: click_.push_back(s); break;
    default: break;
    }
    lastAccepted_ = s;
    haveLastAccepted_ = true;
    if (Progress() >= 1.0f)
        readyTick_ = tick ? tick : 1;
}

float CalibrationSession::Progress() const {
    size_t have = 0;
    switch (step_) {
    case CALSTEP_REST:       have = rest_.size(); break;
    case CALSTEP_RIGHT:      have = right_.size(); break;
    case CALSTEP_LEFT:       have = left_.size(); break;
    case CALSTEP_DOWN:       have = down_.size(); break;
    case CALSTEP_UP:         have = up_.size(); break;
    case CALSTEP_WHEEL_FWD:  have = wheelFwd_.size(); break;
    case CALSTEP_WHEEL_BACK: have = wheelBack_.size(); break;
    case CALSTEP_CLICK_LEFT: have = click_.size(); break;
    default: return 1.0f;
    }
    int need = NeededSamples();
    if (need <= 0) return 1.0f;
    float p = (float)have / (float)need;
    return p > 1.0f ? 1.0f : p;
}

void CalibrationSession::Advance() {
    if (Finished())
        return;
    // Verrou volontaire : tant que la jauge n'est pas pleine, valider ne fait
    // rien. C'est ce qui evite qu'un utilisateur enchaine les etapes sans
    // faire les gestes et enregistre une disposition fausse - decision prise
    // avec l'utilisateur, qui a explicitement prefere un assistant plus long
    // mais sur.
    if (!CanAdvance())
        return;

    if (step_ == CALSTEP_REST)
        baseline_ = BuildBaseline(rest_);
    if (step_ == CALSTEP_UP)
        DetectAxesEarly();
    ResetStepState();

    switch (step_) {
    case CALSTEP_REST:       step_ = CALSTEP_RIGHT; break;
    case CALSTEP_RIGHT:      step_ = CALSTEP_LEFT; break;
    case CALSTEP_LEFT:       step_ = CALSTEP_DOWN; break;
    case CALSTEP_DOWN:       step_ = CALSTEP_UP; break;
    case CALSTEP_UP:         step_ = CALSTEP_WHEEL_FWD; break;
    case CALSTEP_WHEEL_FWD:  step_ = CALSTEP_WHEEL_BACK; break;
    case CALSTEP_WHEEL_BACK: step_ = CALSTEP_CLICK_LEFT; break;
    case CALSTEP_CLICK_LEFT: FinishDetection(); break;
    default: break;
    }
}

void CalibrationSession::SkipWheel() {
    if (!CanSkip())
        return;
    // On vide les echantillons molette deja collectes : un demi-geste ne doit
    // pas produire une detection hasardeuse. Les deux ensembles vides feront
    // simplement conclure "aucune molette", ce que BuildLayout accepte.
    wheelFwd_.clear();
    wheelBack_.clear();
    step_ = CALSTEP_CLICK_LEFT;
    ResetStepState();
}

void CalibrationSession::Cancel() {
    step_ = CALSTEP_FAILED;
}

void CalibrationSession::DetectAxesEarly() {
    // Memes detections que FinishDetection, faites des la fin des gestes de
    // deplacement pour filtrer les etapes molette et clic (voir axisBytes_).
    bool exclude[16];
    for (int i = 0; i < 16; i++) exclude[i] = false;
    DetectedField x = DetectField(baseline_, right_, left_, CALIB_AXIS_X, exclude);
    MarkFieldExcluded(x, exclude);
    DetectedField y = DetectField(baseline_, down_, up_, CALIB_AXIS_Y, exclude);
    MarkFieldExcluded(y, exclude);
    for (int i = 0; i < 16; i++) axisBytes_[i] = exclude[i];
}

void CalibrationSession::FinishDetection() {
    // Detection SEQUENTIELLE, chaque champ retirant ses octets du jeu pour
    // les suivants. Correction issue d'un test reel (2026-08-03) : pour
    // tourner la molette, la main fait forcement bouger la souris - les
    // octets X et Y varient donc pendant l'etape molette, et la detection
    // les prenait pour la molette. Meme probleme au clic, ou l'on bouge
    // presque toujours un peu.
    //
    // L'ordre compte : X et Y d'abord (gestes les plus francs, donc les plus
    // fiables), puis la molette, puis le bouton sur ce qui reste.
    bool exclude[16];
    for (int i = 0; i < 16; i++) exclude[i] = false;

    DetectedField x = DetectField(baseline_, right_, left_, CALIB_AXIS_X, exclude);
    MarkFieldExcluded(x, exclude);

    DetectedField y = DetectField(baseline_, down_, up_, CALIB_AXIS_Y, exclude);
    MarkFieldExcluded(y, exclude);

    DetectedField wheel = DetectField(baseline_, wheelFwd_, wheelBack_, CALIB_WHEEL, exclude);
    MarkFieldExcluded(wheel, exclude);

    std::vector<CalibrationSample> empty;
    DetectedField button = DetectField(baseline_, click_, empty, CALIB_BUTTON, exclude);

    foundX_ = x.found;
    foundY_ = y.found;
    foundWheel_ = wheel.found;
    foundButton_ = button.found;

    layout_ = BuildLayout(reportSize_, x, y, wheel, button);
    step_ = layout_.valid ? CALSTEP_DONE : CALSTEP_FAILED;
}

std::string CalibrationSession::Summary() const {
    char buf[320];
    if (!layout_.valid) {
        // On dit CE QUI a manque. Un echec sans piste ne permet ni de
        // recommencer utilement, ni de nous remonter une information
        // exploitable - constat du premier test reel.
        _snprintf(buf, sizeof(buf) - 1,
            Tr(STR_CALSUM_FAIL_FMT),
            (int)reportSize_,
            foundX_ ? Tr(STR_CALSUM_YES) : Tr(STR_CALSUM_NO_CAPS),
            foundY_ ? Tr(STR_CALSUM_YES) : Tr(STR_CALSUM_NO_CAPS),
            foundWheel_ ? Tr(STR_CALSUM_YES) : Tr(STR_CALSUM_NO),
            foundButton_ ? Tr(STR_CALSUM_YES) : Tr(STR_CALSUM_NO));
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }

    char wheelTxt[48];
    if (layout_.wheelOffset == REPORT_FIELD_ABSENT) {
        _snprintf(wheelTxt, sizeof(wheelTxt) - 1, "%s", Tr(STR_CALSUM_WHEEL_NONE));
    } else {
        _snprintf(wheelTxt, sizeof(wheelTxt) - 1, Tr(STR_CALSUM_WHEEL_BYTE_FMT),
            (int)layout_.wheelOffset);
    }
    wheelTxt[sizeof(wheelTxt) - 1] = '\0';

    _snprintf(buf, sizeof(buf) - 1,
        Tr(STR_CALSUM_SUCCESS_FMT),
        (int)layout_.reportSize,
        (int)layout_.xOffset, (int)layout_.xSize,
        (int)layout_.yOffset, (int)layout_.ySize,
        (int)layout_.buttonsOffset, wheelTxt);
    buf[sizeof(buf) - 1] = '\0';
    return std::string(buf);
}
