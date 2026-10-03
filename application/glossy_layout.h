// FICHIER GENERE par _design_refs/xui_kit/gen_glossy_cpp.py - ne pas modifier a la main.
#pragma once
#include "glossy_assets.h"

// Positions de la page Clavier & souris, en pixels sur 1280x720.
// Image : x,y = coin haut-gauche de la zone utile, w,h = sa taille.
// Texte : x,y = point d'ancrage (y = milieu vertical du texte), w=h=0.

struct GlRect { float x, y, w, h; };

static const GlRect GL_ImgFond = { 0.0f, 0.0f, 1280.0f, 720.0f };
static const GlRect GL_ImgLogo = { 40.0f, 10.0f, 72.0f, 72.0f };
static const GlRect GL_ImgNom = { 126.0f, 12.0f, 128.0f, 53.0f };
static const GlRect GL_TxtStudio = { 126.0f, 75.0f, 0.0f, 0.0f };
static const GlRect GL_Onglets = { 536.0f, 26.0f, 528.0f, 44.0f };
static const GlRect GL_OngletActif = { 536.0f, 26.0f, 132.0f, 44.0f };
static const GlRect GL_OngletSoulign = { 536.0f, 67.0f, 132.0f, 3.0f };
static const GlRect GL_TxtOnglet0 = { 602.0f, 47.0f, 0.0f, 0.0f };
static const GlRect GL_TxtOnglet1 = { 734.0f, 47.0f, 0.0f, 0.0f };
static const GlRect GL_TxtOnglet2 = { 866.0f, 47.0f, 0.0f, 0.0f };
static const GlRect GL_TxtOnglet3 = { 998.0f, 47.0f, 0.0f, 0.0f };
static const GlRect GL_BarreInfo = { 44.0f, 92.0f, 1192.0f, 48.0f };
static const GlRect GL_PuceClavier = { 548.0f, 97.0f, 80.0f, 38.0f };
static const GlRect GL_IcoClavier = { 560.0f, 103.0f, 38.0f, 26.0f };
static const GlRect GL_DotClavier = { 602.0f, 111.0f, 10.0f, 10.0f };
static const GlRect GL_ChevronClavier = { 614.0f, 113.0f, 10.0f, 6.0f };
static const GlRect GL_PuceSouris = { 652.0f, 97.0f, 80.0f, 38.0f };
static const GlRect GL_IcoSouris = { 664.0f, 100.0f, 22.0f, 32.0f };
static const GlRect GL_DotSouris = { 706.0f, 111.0f, 10.0f, 10.0f };
static const GlRect GL_ChevronSouris = { 718.0f, 113.0f, 10.0f, 6.0f };
static const GlRect GL_LangueEN = { 1040.0f, 104.0f, 36.0f, 24.0f };
static const GlRect GL_AnneauLangue = { 1085.0f, 101.0f, 36.0f, 24.0f };
static const GlRect GL_LangueFR = { 1088.0f, 104.0f, 36.0f, 24.0f };
static const GlRect GL_LangueES = { 1136.0f, 104.0f, 36.0f, 24.0f };
static const GlRect GL_LanguePT = { 1184.0f, 104.0f, 36.0f, 24.0f };
static const GlRect GL_IcoTitreClavier = { 42.0f, 162.0f, 38.0f, 26.0f };
static const GlRect GL_TxtTitreClavier = { 92.0f, 175.0f, 0.0f, 0.0f };
static const GlRect GL_RegleClavier = { 40.0f, 196.0f, 346.0f, 1.0f };
static const GlRect GL_BtnAssistant = { 40.0f, 206.0f, 344.0f, 42.0f };
static const GlRect GL_TxtAssistant = { 212.0f, 227.0f, 0.0f, 0.0f };
static const GlRect GL_CaseA = { 40.0f, 258.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeA = { 48.0f, 267.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheA = { 140.0f, 281.0f, 0.0f, 0.0f };
static const GlRect GL_CaseB = { 40.0f, 311.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeB = { 48.0f, 320.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheB = { 140.0f, 334.0f, 0.0f, 0.0f };
static const GlRect GL_CaseX = { 40.0f, 364.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeX = { 48.0f, 373.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheX = { 140.0f, 387.0f, 0.0f, 0.0f };
static const GlRect GL_CaseY = { 40.0f, 417.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeY = { 48.0f, 426.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheY = { 140.0f, 440.0f, 0.0f, 0.0f };
static const GlRect GL_CaseLB = { 40.0f, 470.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeLB = { 48.0f, 479.0f, 42.0f, 28.0f };
static const GlRect GL_TxtToucheLB = { 140.0f, 493.0f, 0.0f, 0.0f };
static const GlRect GL_CaseRB = { 40.0f, 523.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeRB = { 48.0f, 532.0f, 42.0f, 28.0f };
static const GlRect GL_TxtToucheRB = { 140.0f, 546.0f, 0.0f, 0.0f };
static const GlRect GL_CaseLT = { 40.0f, 576.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeLT = { 48.0f, 585.0f, 31.0f, 28.0f };
static const GlRect GL_TxtToucheLT = { 140.0f, 599.0f, 0.0f, 0.0f };
static const GlRect GL_CaseRT = { 158.0f, 258.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeRT = { 166.0f, 267.0f, 31.0f, 28.0f };
static const GlRect GL_TxtToucheRT = { 258.0f, 281.0f, 0.0f, 0.0f };
static const GlRect GL_CaseL3 = { 158.0f, 311.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeL3 = { 166.0f, 320.0f, 26.0f, 28.0f };
static const GlRect GL_TxtToucheL3 = { 258.0f, 334.0f, 0.0f, 0.0f };
static const GlRect GL_CaseR3 = { 158.0f, 364.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeR3 = { 166.0f, 373.0f, 26.0f, 28.0f };
static const GlRect GL_TxtToucheR3 = { 258.0f, 387.0f, 0.0f, 0.0f };
static const GlRect GL_CaseSTART = { 158.0f, 417.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeSTART = { 166.0f, 426.0f, 42.0f, 28.0f };
static const GlRect GL_TxtToucheSTART = { 258.0f, 440.0f, 0.0f, 0.0f };
static const GlRect GL_CaseBACK = { 158.0f, 470.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeBACK = { 166.0f, 479.0f, 42.0f, 28.0f };
static const GlRect GL_TxtToucheBACK = { 258.0f, 493.0f, 0.0f, 0.0f };
static const GlRect GL_CaseGUIDE = { 158.0f, 523.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeGUIDE = { 166.0f, 532.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheGUIDE = { 258.0f, 546.0f, 0.0f, 0.0f };
static const GlRect GL_CaseDUP = { 158.0f, 576.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeDUP = { 166.0f, 585.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheDUP = { 258.0f, 599.0f, 0.0f, 0.0f };
static const GlRect GL_CaseDDOWN = { 276.0f, 258.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeDDOWN = { 284.0f, 267.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheDDOWN = { 376.0f, 281.0f, 0.0f, 0.0f };
static const GlRect GL_CaseDLEFT = { 276.0f, 311.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeDLEFT = { 284.0f, 320.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheDLEFT = { 376.0f, 334.0f, 0.0f, 0.0f };
static const GlRect GL_CaseDRIGHT = { 276.0f, 364.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeDRIGHT = { 284.0f, 373.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheDRIGHT = { 376.0f, 387.0f, 0.0f, 0.0f };
static const GlRect GL_CaseLSUP = { 276.0f, 417.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeLSUP = { 284.0f, 426.0f, 25.0f, 28.0f };
static const GlRect GL_TxtToucheLSUP = { 376.0f, 440.0f, 0.0f, 0.0f };
static const GlRect GL_CaseLSDOWN = { 276.0f, 470.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeLSDOWN = { 284.0f, 479.0f, 28.0f, 28.0f };
static const GlRect GL_TxtToucheLSDOWN = { 376.0f, 493.0f, 0.0f, 0.0f };
static const GlRect GL_CaseLSLEFT = { 276.0f, 523.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeLSLEFT = { 284.0f, 532.0f, 31.0f, 28.0f };
static const GlRect GL_TxtToucheLSLEFT = { 376.0f, 546.0f, 0.0f, 0.0f };
static const GlRect GL_CaseLSRIGHT = { 276.0f, 576.0f, 108.0f, 46.0f };
static const GlRect GL_BadgeLSRIGHT = { 284.0f, 585.0f, 31.0f, 28.0f };
static const GlRect GL_TxtToucheLSRIGHT = { 376.0f, 599.0f, 0.0f, 0.0f };
static const GlRect GL_ImgManetteFace = { 420.0f, 190.0f, 500.0f, 323.0f };
static const GlRect GL_LueurBouton = { 815.0f, 384.0f, 1.0f, 1.0f };
static const GlRect GL_TxtEtiquetteBtn = { 628.8f, 560.0f, 0.0f, 0.0f };
static const GlRect GL_TxtEtiquette = { 638.8f, 560.0f, 0.0f, 0.0f };
static const GlRect GL_BtnMesurer = { 432.0f, 602.0f, 234.0f, 40.0f };
static const GlRect GL_TxtMesurer = { 549.0f, 622.0f, 0.0f, 0.0f };
static const GlRect GL_BtnCalibrer = { 676.0f, 602.0f, 234.0f, 40.0f };
static const GlRect GL_TxtCalibrer = { 793.0f, 622.0f, 0.0f, 0.0f };
static const GlRect GL_IcoTitreSouris = { 964.0f, 159.0f, 22.0f, 32.0f };
static const GlRect GL_TxtTitreSouris = { 998.0f, 175.0f, 0.0f, 0.0f };
static const GlRect GL_RegleSouris = { 960.0f, 196.0f, 276.0f, 1.0f };
static const GlRect GL_CaseMLEFT = { 962.0f, 206.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMLEFT = { 974.0f, 211.0f, 23.0f, 30.0f };
static const GlRect GL_FlecheMLEFT = { 1012.0f, 226.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMLEFT = { 1024.0f, 214.0f, 26.0f, 24.0f };
static const GlRect GL_TxtActionMLEFT = { 1058.0f, 226.0f, 0.0f, 0.0f };
static const GlRect GL_CaseMRIGHT = { 1104.0f, 206.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMRIGHT = { 1116.0f, 211.0f, 23.0f, 30.0f };
static const GlRect GL_FlecheMRIGHT = { 1154.0f, 226.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMRIGHT = { 1166.0f, 214.0f, 26.0f, 24.0f };
static const GlRect GL_TxtActionMRIGHT = { 1200.0f, 226.0f, 0.0f, 0.0f };
static const GlRect GL_CaseMMID = { 962.0f, 252.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMMID = { 974.0f, 257.0f, 23.0f, 30.0f };
static const GlRect GL_FlecheMMID = { 1012.0f, 272.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMMID = { 1024.0f, 260.0f, 22.0f, 24.0f };
static const GlRect GL_CaseMWUP = { 1104.0f, 252.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMWUP = { 1116.0f, 257.0f, 20.0f, 30.0f };
static const GlRect GL_FlecheMWUP = { 1154.0f, 272.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMWUP = { 1166.0f, 260.0f, 24.0f, 24.0f };
static const GlRect GL_CaseMWDOWN = { 962.0f, 298.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMWDOWN = { 974.0f, 303.0f, 23.0f, 30.0f };
static const GlRect GL_FlecheMWDOWN = { 1012.0f, 318.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMWDOWN = { 1024.0f, 306.0f, 24.0f, 24.0f };
static const GlRect GL_CaseAjouter = { 1104.0f, 298.0f, 132.0f, 40.0f };
static const GlRect GL_IcoAjouter = { 1116.0f, 304.0f, 30.0f, 29.0f };
static const GlRect GL_TxtAjouter = { 1186.0f, 318.0f, 0.0f, 0.0f };
static const GlRect GL_GroupeCurseurs = { 956.0f, 348.0f, 286.0f, 284.0f };
static const GlRect GL_TxtCurseur0 = { 966.0f, 368.0f, 0.0f, 0.0f };
static const GlRect GL_Rail0 = { 966.0f, 392.0f, 266.0f, 6.0f };
static const GlRect GL_Rempli0 = { 966.0f, 392.0f, 266.0f, 6.0f };
static const GlRect GL_Poignee0 = { 1062.0f, 385.0f, 20.0f, 20.0f };
static const GlRect GL_TxtCurseur1 = { 966.0f, 434.0f, 0.0f, 0.0f };
static const GlRect GL_Rail1 = { 966.0f, 458.0f, 266.0f, 6.0f };
static const GlRect GL_Rempli1 = { 966.0f, 458.0f, 266.0f, 6.0f };
static const GlRect GL_Poignee1 = { 987.0f, 451.0f, 20.0f, 20.0f };
static const GlRect GL_TxtCurseur2 = { 966.0f, 500.0f, 0.0f, 0.0f };
static const GlRect GL_Rail2 = { 966.0f, 524.0f, 266.0f, 6.0f };
static const GlRect GL_Rempli2 = { 966.0f, 524.0f, 266.0f, 6.0f };
static const GlRect GL_Poignee2 = { 1022.0f, 517.0f, 20.0f, 20.0f };
static const GlRect GL_TxtCurseur3 = { 966.0f, 566.0f, 0.0f, 0.0f };
static const GlRect GL_Rail3 = { 966.0f, 590.0f, 266.0f, 6.0f };
static const GlRect GL_Rempli3 = { 966.0f, 590.0f, 266.0f, 6.0f };
static const GlRect GL_Poignee3 = { 1035.0f, 583.0f, 20.0f, 20.0f };
static const GlRect GL_TxtDpi = { 1099.0f, 616.0f, 0.0f, 0.0f };
static const GlRect GL_TxtVersionBas = { 42.0f, 674.0f, 0.0f, 0.0f };
static const GlRect GL_DotPlugin = { 98.0f, 669.0f, 10.0f, 10.0f };
static const GlRect GL_TxtPluginBas = { 116.0f, 674.0f, 0.0f, 0.0f };
static const GlRect GL_TxtAideB = { 1187.5f, 674.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeAideB = { 1149.5f, 660.0f, 28.0f, 28.0f };
static const GlRect GL_TxtAideX = { 1067.1f, 674.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeAideX = { 1029.1f, 660.0f, 28.0f, 28.0f };
static const GlRect GL_TxtAideA = { 939.9f, 674.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeAideA = { 901.9f, 660.0f, 28.0f, 28.0f };
static const GlRect GL_TxtConsigne = { 405.7f, 116.0f, 0.0f, 0.0f };
static const GlRect GL_TxtConsigneBtn = { 756.1f, 116.0f, 0.0f, 0.0f };
static const GlRect GL_TxtSep = { 774.6f, 116.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeB_consigne = { 802.5f, 104.0f, 24.0f, 24.0f };
static const GlRect GL_TxtPasser = { 832.5f, 116.0f, 0.0f, 0.0f };
static const GlRect GL_CaseMSIDE1 = { 1104.0f, 298.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMSIDE1 = { 1116.0f, 304.0f, 30.0f, 29.0f };
static const GlRect GL_FlecheMSIDE1 = { 1154.0f, 318.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMSIDE1 = { 1166.0f, 306.0f, 36.0f, 24.0f };
static const GlRect GL_CaseMSIDE2 = { 962.0f, 344.0f, 132.0f, 40.0f };
static const GlRect GL_IcoMSIDE2 = { 974.0f, 350.0f, 30.0f, 29.0f };
static const GlRect GL_FlecheMSIDE2 = { 1012.0f, 364.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeMSIDE2 = { 1024.0f, 352.0f, 36.0f, 24.0f };
static const GlRect GL_TxtAideBACK = { 1110.4f, 674.0f, 0.0f, 0.0f };
static const GlRect GL_BadgeAideBACK = { 1062.4f, 661.0f, 38.0f, 26.0f };

// Grille clavier : 3 colonnes x 7 lignes, ordre de l'assistant colonne par colonne.
static const float GL_KGRID_DX = 118.0f, GL_KGRID_DY = 53.0f;
// Grille souris : 2 colonnes.
static const float GL_MGRID_DX = 142.0f, GL_MGRID_DY = 46.0f;

// Ou chaque bouton s'allume sur la manette (index = case de la grille).
// labelX/labelY = centre de l'etiquette "A Â· Espace".
struct GlPadPoint { float x, y; int glowAsset; float labelX, labelY; };
static const GlPadPoint GL_PAD_POINTS[21] = {
    { 815.5f, 384.8f, GA_GLOW_FACE, 670.0f, 560.0f },   // A
    { 856.0f, 352.8f, GA_GLOW_FACE, 670.0f, 560.0f },   // B
    { 775.0f, 352.8f, GA_GLOW_FACE, 670.0f, 560.0f },   // X
    { 821.0f, 320.5f, GA_GLOW_FACE, 670.0f, 560.0f },   // Y
    { 527.5f, 258.5f, GA_GLOW_BUMPER, 670.0f, 560.0f },   // LB
    { 810.5f, 258.5f, GA_GLOW_BUMPER, 670.0f, 560.0f },   // RB
    { 535.5f, 234.6f, GA_GLOW_TRIGGER, 670.0f, 560.0f },   // LT
    { 804.0f, 234.6f, GA_GLOW_TRIGGER, 670.0f, 560.0f },   // RT
    { 527.5f, 358.6f, GA_GLOW_STICK, 670.0f, 560.0f },   // L3
    { 739.0f, 419.0f, GA_GLOW_STICK, 670.0f, 560.0f },   // R3
    { 719.0f, 355.1f, GA_GLOW_SMALL, 670.0f, 560.0f },   // START
    { 619.5f, 355.1f, GA_GLOW_SMALL, 670.0f, 560.0f },   // BACK
    { 670.5f, 352.8f, GA_GLOW_GUIDE, 670.0f, 560.0f },   // GUIDE
    { 592.5f, 419.0f, GA_GLOW_DPAD, 670.0f, 560.0f },   // DUP
    { 592.5f, 419.0f, GA_GLOW_DPAD, 670.0f, 560.0f },   // DDOWN
    { 592.5f, 419.0f, GA_GLOW_DPAD, 670.0f, 560.0f },   // DLEFT
    { 592.5f, 419.0f, GA_GLOW_DPAD, 670.0f, 560.0f },   // DRIGHT
    { 527.5f, 358.6f, GA_GLOW_STICK, 670.0f, 560.0f },   // LSUP
    { 527.5f, 358.6f, GA_GLOW_STICK, 670.0f, 560.0f },   // LSDOWN
    { 527.5f, 358.6f, GA_GLOW_STICK, 670.0f, 560.0f },   // LSLEFT
    { 527.5f, 358.6f, GA_GLOW_STICK, 670.0f, 560.0f },   // LSRIGHT
};

// Touche du clavier : libelle -> icone de touche + petit prefixe ("NUM", "L", "R").
struct GlKeyIcon { const char* label; int asset; const char* prefix; };
static const GlKeyIcon GL_KEY_ICONS[] = {
    { "-", GA_K_MINUS, "" },
    { "0", GA_K_0, "" },
    { "1", GA_K_1, "" },
    { "2", GA_K_2, "" },
    { "3", GA_K_3, "" },
    { "4", GA_K_4, "" },
    { "5", GA_K_5, "" },
    { "6", GA_K_6, "" },
    { "7", GA_K_7, "" },
    { "8", GA_K_8, "" },
    { "9", GA_K_9, "" },
    { "=", GA_K_EQUALS, "" },
    { "A", GA_K_A, "" },
    { "B", GA_K_B, "" },
    { "BACKSP", GA_K_BACKSPACE, "" },
    { "C", GA_K_C, "" },
    { "D", GA_K_D, "" },
    { "DEL", GA_K_DELETE, "" },
    { "DOWN", GA_K_ARROW_DOWN, "" },
    { "E", GA_K_E, "" },
    { "END", GA_K_END, "" },
    { "ENTER", GA_K_ENTER, "" },
    { "ESC", GA_K_ESCAPE, "" },
    { "F", GA_K_F, "" },
    { "F1", GA_K_F1, "" },
    { "F10", GA_K_F10, "" },
    { "F11", GA_K_F11, "" },
    { "F12", GA_K_F12, "" },
    { "F2", GA_K_F2, "" },
    { "F3", GA_K_F3, "" },
    { "F4", GA_K_F4, "" },
    { "F5", GA_K_F5, "" },
    { "F6", GA_K_F6, "" },
    { "F7", GA_K_F7, "" },
    { "F8", GA_K_F8, "" },
    { "F9", GA_K_F9, "" },
    { "G", GA_K_G, "" },
    { "H", GA_K_H, "" },
    { "HOME", GA_K_HOME, "" },
    { "I", GA_K_I, "" },
    { "INS", GA_K_INSERT, "" },
    { "J", GA_K_J, "" },
    { "K", GA_K_K, "" },
    { "L", GA_K_L, "" },
    { "L ALT", GA_K_ALT, "L" },
    { "L CTRL", GA_K_CTRL, "L" },
    { "L SHIFT", GA_K_SHIFT, "L" },
    { "L WIN", GA_K_WIN, "L" },
    { "LEFT", GA_K_ARROW_LEFT, "" },
    { "M", GA_K_M, "" },
    { "N", GA_K_N, "" },
    { "NUM *", GA_K_ASTERISK, "NUM" },
    { "NUM +", GA_K_NUMPAD_PLUS, "" },
    { "NUM -", GA_K_MINUS, "NUM" },
    { "NUM .", GA_K_PERIOD, "NUM" },
    { "NUM /", GA_K_SLASH_FORWARD, "NUM" },
    { "NUM 0", GA_K_0, "NUM" },
    { "NUM 1", GA_K_1, "NUM" },
    { "NUM 2", GA_K_2, "NUM" },
    { "NUM 3", GA_K_3, "NUM" },
    { "NUM 4", GA_K_4, "NUM" },
    { "NUM 5", GA_K_5, "NUM" },
    { "NUM 6", GA_K_6, "NUM" },
    { "NUM 7", GA_K_7, "NUM" },
    { "NUM 8", GA_K_8, "NUM" },
    { "NUM 9", GA_K_9, "NUM" },
    { "NUM ENT", GA_K_NUMPAD_ENTER, "" },
    { "NUM LOCK", GA_K_NUMLOCK, "" },
    { "O", GA_K_O, "" },
    { "P", GA_K_P, "" },
    { "PG DN", GA_K_PAGE_DOWN, "" },
    { "PG UP", GA_K_PAGE_UP, "" },
    { "Q", GA_K_Q, "" },
    { "R", GA_K_R, "" },
    { "R ALT", GA_K_ALT, "R" },
    { "R CTRL", GA_K_CTRL, "R" },
    { "R SHIFT", GA_K_SHIFT, "R" },
    { "RIGHT", GA_K_ARROW_RIGHT, "" },
    { "S", GA_K_S, "" },
    { "SPACE", GA_K_SPACE, "" },
    { "T", GA_K_T, "" },
    { "TAB", GA_K_TAB, "" },
    { "U", GA_K_U, "" },
    { "UP", GA_K_ARROW_UP, "" },
    { "V", GA_K_V, "" },
    { "W", GA_K_W, "" },
    { "X", GA_K_X, "" },
    { "Y", GA_K_Y, "" },
    { "Z", GA_K_Z, "" },
};
static const int GL_KEY_ICON_COUNT = (int)(sizeof(GL_KEY_ICONS) / sizeof(GL_KEY_ICONS[0]));
