#include "mouse_calibration.h"
#include <string.h>

// Voir mouse_calibration.h pour la raison d'etre de ce module.

int32_t ReadSignedField(const uint8_t* payload, uint8_t length,
                        uint8_t offset, uint8_t size) {
    if (!payload || offset == REPORT_FIELD_ABSENT || size == 0)
        return 0;
    if (offset + size > length)
        return 0; // hors limites : on renvoie "aucun mouvement" plutot que de lire n'importe quoi

    if (size == 1)
        return (int32_t)(int8_t)payload[offset];

    // 2 octets, petit-boutiste : convention de tous les rapports HID.
    uint16_t raw = (uint16_t)payload[offset] | ((uint16_t)payload[offset + 1] << 8);
    return (int32_t)(int16_t)raw;
}

CalibrationBaseline BuildBaseline(const std::vector<CalibrationSample>& samples) {
    CalibrationBaseline out;
    memset(&out, 0, sizeof(out));
    for (int i = 0; i < 16; i++)
        out.stable[i] = true;

    if (samples.empty())
        return out;

    out.length = samples[0].length;
    if (out.length > 16) out.length = 16;
    memcpy(out.bytes, samples[0].bytes, out.length);

    // On MESURE le bruit de fond de chaque octet au lieu de le disqualifier.
    // Un octet qui frémit de +/-2 au repos reste un champ parfaitement
    // exploitable : il suffira d'exiger qu'un vrai geste depasse ces 2.
    for (size_t s = 1; s < samples.size(); s++) {
        uint8_t n = samples[s].length < out.length ? samples[s].length : out.length;
        for (uint8_t i = 0; i < n; i++) {
            int d = (int)(int8_t)(samples[s].bytes[i] - out.bytes[i]);
            if (d < 0) d = -d;
            if (d > (int)out.jitter[i])
                out.jitter[i] = (uint8_t)(d > 255 ? 255 : d);
            if (samples[s].bytes[i] != out.bytes[i])
                out.stable[i] = false;
        }
    }
    return out;
}

void MarkFieldExcluded(const DetectedField& field, bool* exclude) {
    if (!field.found || !exclude || field.offset == REPORT_FIELD_ABSENT)
        return;
    uint8_t n = field.size > 0 ? field.size : 1;
    for (uint8_t k = 0; k < n && (field.offset + k) < 16; k++)
        exclude[field.offset + k] = true;
}

// Somme des valeurs signees d'un champ candidat sur tous les paquets d'une
// action. Un vrai champ d'axe accumule une valeur franche et de signe
// constant ; un octet parasite oscille autour de zero.
//
// On lit la valeur BRUTE, sans soustraire le repos, et ce point est subtil :
// les axes et la molette d'une souris sont RELATIFS, leur repos vaut zero par
// definition. Soustraire un echantillon de repos bruite cassait tout - un
// octet au repos a 0xFF (soit -1) lu comme poids faible d'un champ 16 bits
// donne +255, et la soustraction rendait les deux sens de meme signe, donc
// le champ etait rejete. Defaut trouve en reproduisant le scenario console de
// l'utilisateur (2026-08-03). Le bruit de repos reste pris en compte, mais au
// bon endroit : dans ByteVariesDuringAction.
static int32_t AccumulateDelta(const CalibrationBaseline& baseline,
                               const std::vector<CalibrationSample>& samples,
                               uint8_t offset, uint8_t size) {
    (void)baseline;
    int32_t total = 0;
    for (size_t s = 0; s < samples.size(); s++) {
        if (offset + size > samples[s].length)
            continue;
        total += ReadSignedField(samples[s].bytes, samples[s].length, offset, size);
    }
    return total;
}

// Cet octet change-t-il de valeur pendant l'action, par rapport au repos ?
// Sert a localiser le debut d'un champ et a decider s'il fait 1 ou 2 octets.
// Doit DEPASSER le bruit de fond mesure au repos, pas simplement differer.
// C'est ce qui permet d'accepter un octet legerement bruite (cas normal d'un
// capteur optique) tout en ignorant ce bruit quand on cherche un vrai geste.
//
// Le seuil est le bruit LUI-MEME, sans marge supplementaire, et ce detail
// compte : un cran de molette ne vaut que +/-1. Avec une marge de 1 sur un
// octet parfaitement propre (bruit 0), le seuil devenait 1 et la molette
// etait systematiquement rejetee - defaut trouve par le test reproduisant le
// scenario console de l'utilisateur. Sur un octet propre le seuil vaut donc
// 0 (toute variation compte), et sur un octet bruite a 2 il faut depasser 2.
static bool ByteVariesDuringAction(const CalibrationBaseline& baseline,
                                   const std::vector<CalibrationSample>& positive,
                                   const std::vector<CalibrationSample>& negative,
                                   uint8_t offset) {
    if (offset >= baseline.length)
        return false;
    int tolerance = (int)baseline.jitter[offset];
    for (int pass = 0; pass < 2; pass++) {
        const std::vector<CalibrationSample>& set = (pass == 0) ? positive : negative;
        for (size_t s = 0; s < set.size(); s++) {
            if (offset >= set[s].length)
                continue;
            int d = (int)(int8_t)(set[s].bytes[offset] - baseline.bytes[offset]);
            if (d < 0) d = -d;
            if (d > tolerance)
                return true;
        }
    }
    return false;
}

// L'octet suivant est-il le POIDS FORT du meme champ (axe 16 bits) ou un
// AUTRE champ (Y juste apres X sur une souris 8 bits) ? Ajoute le 2026-09-25
// apres un echec reel : la main derivant un peu verticalement pendant le
// geste "droite", l'octet Y variait, X etait lu sur 16 bits et avalait Y.
// Le poids fort d'un vrai champ 16 bits suit la DIRECTION du geste : 0 ou
// positif pendant le geste positif (0x00, 0x01...), 0 ou negatif pendant le
// geste oppose (0xFF, 0xFE...). Un octet independant (Y qui tremble) prend
// les deux signes dans les deux gestes. On exige 80 % des paquets, et au moins
// un poids fort non nul.
static bool LooksLikeHighByte(const std::vector<CalibrationSample>& positive,
                              const std::vector<CalibrationSample>& negative,
                              uint8_t off) {
    size_t total = 0, ok = 0, nonZero = 0;
    for (int pass = 0; pass < 2; pass++) {
        const std::vector<CalibrationSample>& set = (pass == 0) ? positive : negative;
        for (size_t i = 0; i < set.size(); i++) {
            if (off + 1 >= set[i].length) continue;
            const int hi = (int)(int8_t)set[i].bytes[off + 1];
            total++;
            if (hi != 0) nonZero++;
            if (hi == 0 || (pass == 0 ? hi > 0 : hi < 0)) ok++;
        }
    }
    return total > 0 && nonZero > 0 && ok * 5 >= total * 4;
}

static DetectedField DetectAxisLike(const CalibrationBaseline& baseline,
                                    const std::vector<CalibrationSample>& positive,
                                    const std::vector<CalibrationSample>& negative,
                                    const bool* exclude) {
    DetectedField best;
    best.found = false;
    best.offset = REPORT_FIELD_ABSENT;
    best.size = 0;
    best.bitIndex = 0;

    int32_t bestScore = 0;

    for (uint8_t off = 0; off < baseline.length; off++) {
        // Octet deja attribue a un champ precedemment detecte : on passe.
        // C'est ce qui empeche le mouvement parasite de la souris pendant
        // qu'on tourne la molette d'etre pris pour la molette elle-meme.
        if (exclude && exclude[off])
            continue;
        // L'octet de poids faible d'un champ varie forcement des le moindre
        // mouvement. S'il ne bouge pas, ce n'est pas le debut d'un champ -
        // c'est ce qui elimine les lectures 16 bits a cheval sur un octet
        // constant (identifiant de rapport, octet de boutons, remplissage),
        // qui produisaient sinon une amplitude artificiellement enorme et
        // remportaient le score a tort.
        if (!ByteVariesDuringAction(baseline, positive, negative, off))
            continue;

        // Taille decidee sur un indice reel plutot que par essai/erreur : le
        // champ fait 2 octets si l'octet SUIVANT varie lui aussi pendant la
        // meme action (poids fort d'un axe 16 bits), 1 octet sinon. Sur une
        // souris 8 bits classique, l'octet suivant X est Y - il reste immobile
        // quand on ne bouge qu'horizontalement, d'ou une detection correcte.
        uint8_t size = 1;
        if (off + 1 < baseline.length &&
            !(exclude && exclude[off + 1]) &&
            ByteVariesDuringAction(baseline, positive, negative, (uint8_t)(off + 1)) &&
            LooksLikeHighByte(positive, negative, off))
            size = 2;

        if (off + size > baseline.length)
            continue;

        int32_t dPos = AccumulateDelta(baseline, positive, off, size);
        int32_t dNeg = AccumulateDelta(baseline, negative, off, size);

        // Le critere qui distingue un vrai champ d'une coincidence : les deux
        // sens doivent produire des variations de signes OPPOSES.
        if (dPos == 0 || dNeg == 0)
            continue;
        if ((dPos > 0) == (dNeg > 0))
            continue;

        int32_t magPos = dPos < 0 ? -dPos : dPos;
        int32_t magNeg = dNeg < 0 ? -dNeg : dNeg;
        int32_t score = magPos < magNeg ? magPos : magNeg; // le plus faible des deux sens

        if (score > bestScore) {
            bestScore = score;
            best.found = true;
            best.offset = off;
            best.size = size;
        }
    }
    return best;
}

static DetectedField DetectButton(const CalibrationBaseline& baseline,
                                  const std::vector<CalibrationSample>& pressed,
                                  const bool* exclude) {
    DetectedField best;
    best.found = false;
    best.offset = REPORT_FIELD_ABSENT;
    best.size = 1;
    best.bitIndex = 0;

    if (pressed.empty())
        return best;

    // On cherche un bit a 0 au repos, et a 1 dans TOUS les paquets captures
    // pendant que le bouton est maintenu. Exiger la totalite plutot qu'une
    // majorite evite de confondre un bouton avec un bit de mouvement qui
    // passerait a 1 par intermittence.
    for (uint8_t off = 0; off < baseline.length; off++) {
        // Les octets d'axes sont exclus : en maintenant le clic, la main fait
        // presque toujours bouger un peu la souris, et un bit de poids faible
        // de X ou Y passerait alors pour un bouton.
        if (exclude && exclude[off])
            continue;
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t mask = (uint8_t)(1 << bit);
            if (baseline.bytes[off] & mask)
                continue; // deja a 1 au repos

            // 80 % des paquets au lieu de 100 % (2026-09-25) : un seul paquet
            // parasite (petit mouvement pendant le clic, relachement capte)
            // suffisait a faire echouer la detection sur console.
            size_t setCount = 0;
            for (size_t s = 0; s < pressed.size(); s++) {
                if (off < pressed[s].length && (pressed[s].bytes[off] & mask))
                    setCount++;
            }
            const bool allSet = setCount * 5 >= pressed.size() * 4;
            if (allSet) {
                best.found = true;
                best.offset = off;
                best.bitIndex = bit;
                return best;
            }
        }
    }
    return best;
}

DetectedField DetectField(const CalibrationBaseline& baseline,
                          const std::vector<CalibrationSample>& positive,
                          const std::vector<CalibrationSample>& negative,
                          CalibrationTarget target,
                          const bool* exclude) {
    if (target == CALIB_BUTTON)
        return DetectButton(baseline, positive, exclude);
    return DetectAxisLike(baseline, positive, negative, exclude);
}

HidReportLayout BuildLayout(uint8_t reportSize,
                            const DetectedField& x,
                            const DetectedField& y,
                            const DetectedField& wheel,
                            const DetectedField& button) {
    HidReportLayout out;
    memset(&out, 0, sizeof(out));
    out.reportSize = reportSize;

    // Un champ non detecte est enregistre comme ABSENT plutot que de faire
    // echouer toute la calibration : une souris sans molette reste tres
    // utilisable, et une disposition partielle correcte vaut mieux qu'un rejet.
    out.xOffset     = x.found      ? x.offset      : REPORT_FIELD_ABSENT;
    out.xSize       = x.found      ? x.size        : 0;
    out.yOffset     = y.found      ? y.offset      : REPORT_FIELD_ABSENT;
    out.ySize       = y.found      ? y.size        : 0;
    out.wheelOffset = wheel.found  ? wheel.offset  : REPORT_FIELD_ABSENT;
    out.wheelSize   = wheel.found  ? wheel.size    : 0;
    out.buttonsOffset = button.found ? button.offset : REPORT_FIELD_ABSENT;

    out.valid = IsLayoutUsable(out);
    return out;
}

bool IsLayoutUsable(const HidReportLayout& layout) {
    if (layout.reportSize == 0 || layout.reportSize > 16)
        return false;
    // Sans X ni Y, le plugin n'a rien a appliquer : ce n'est pas une souris
    // exploitable, autant retomber sur le comportement historique.
    if (layout.xOffset == REPORT_FIELD_ABSENT || layout.yOffset == REPORT_FIELD_ABSENT)
        return false;
    if (layout.xSize != 1 && layout.xSize != 2) return false;
    if (layout.ySize != 1 && layout.ySize != 2) return false;
    if (layout.xOffset + layout.xSize > layout.reportSize) return false;
    if (layout.yOffset + layout.ySize > layout.reportSize) return false;
    if (layout.wheelOffset != REPORT_FIELD_ABSENT) {
        if (layout.wheelSize != 1 && layout.wheelSize != 2) return false;
        if (layout.wheelOffset + layout.wheelSize > layout.reportSize) return false;
    }
    if (layout.buttonsOffset != REPORT_FIELD_ABSENT &&
        layout.buttonsOffset >= layout.reportSize)
        return false;
    return true;
}
