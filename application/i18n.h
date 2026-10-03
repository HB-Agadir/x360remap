#pragma once
// Systeme de traduction FR/EN de l'interface (application.xex uniquement -
// X360Remap.xex, le plugin, n'a pas d'UI). Fichier volontairement en C++
// standard PUR (pas de windows.h/xtl.h, pas d'ImGui, pas de _snprintf) : il
// doit rester includable par les suites de tests portables g++
// (application/tests/test_wizard_session.cpp, test_calibration_session.cpp)
// exactement comme wizard_session.h/calibration_session.h.
//
// Toutes les chaines FR existantes de l'appli sont ecrites SANS accents
// (ex: "Demarrer", "Peripheriques", "Sensibilite") - contournement
// deliberement conserve ici : la police par defaut d'ImGui utilisee par
// application.xex est ASCII seul, sans configuration de plage de glyphes
// (glyph ranges). Ne PAS ajouter d'accents aux chaines FR tant que ce travail
// de police n'est pas fait (phase UI-overhaul ulterieure) - ce fichier ne
// touche ni au chargement de police ni aux glyph ranges.
//
// Portugais (Bresil) : AJOUTE le 2026-08-24 (LANG_PT_BR ci-dessous). Le
// blocage documente plus haut n'est plus d'actualite - theme.cpp
// (LoadX360RemapFont) charge deja les plages Latin-1 Supplement ET Latin
// Extended-A (0x0020-0x00FF, 0x0100-0x017F), qui couvrent tous les
// caracteres accentues du portugais (ã õ ç á é í ó ú â ê...). Ce
// commentaire etait reste obsolete apres que quelqu'un a corrige les
// glyph ranges sans le mettre a jour - le francais, lui, reste ecrit sans
// accents par pure convention historique du projet, pas par contrainte
// technique restante.

#include <cstddef>

enum Lang {
    LANG_FR = 0,
    LANG_EN = 1,
    LANG_PT_BR = 2,
    // Espagnol ajoute le 2026-09-13. Demande née d'un testeur hispanophone
    // qui proposait de forker le projet pour le traduire : mieux vaut une
    // langue de plus ici qu'un fork de plus dehors. Ajoute EN DERNIERE
    // POSITION, comme toute nouvelle langue doit l'etre - la valeur numerique
    // des langues existantes est ecrite dans X360Remap_lang.txt sur les
    // consoles des utilisateurs, la decaler invaliderait leur choix.
    LANG_ES = 3,
    LANG_COUNT
};

// Un StrId par chaine d'UI distincte, groupe par ecran/fichier d'origine.
// Les commentaires de section suivent exactement le catalogue de la passe de
// recherche prealable (voir le rapport qui a precede ce fichier) : onglet
// wizard, onglet calibration, onglet reglages, onglet profils, selecteur de
// peripherique, titre fenetre/barre d'onglets (tous dans application/main.cpp),
// puis les libelles d'etape de application/wizard_session.cpp et enfin les
// consignes/resume de application/calibration_session.cpp.
enum StrId {
    // --- main.cpp : DrawWizardTab (onglet "Wizard clavier") ---
    STR_WIZARD_BTN_START,
    STR_WIZARD_NO_KEYBOARD,
    STR_WIZARD_STEP_PROGRESS_FMT,
    STR_WIZARD_PRESS_KEY_FOR_FMT,
    STR_WIZARD_HOLD_TO_SKIP,
    STR_WIZARD_SAVED_FMT,
    STR_WIZARD_SAVE_FAILED,
    STR_WIZARD_BTN_OK,
    STR_WIZARD_CANCELLED,

    // --- main.cpp : DrawCalibrationTab (onglet "Calibration souris") ---
    STR_CALIB_INTRO,
    STR_CALIB_NAV_HINT,
    STR_CALIB_TARGET_MOUSE_FMT,
    STR_CALIB_TARGET_MOUSE_HINT,
    STR_CALIB_BTN_START,
    STR_CALIB_FALLBACK_ACTIVE,
    STR_CALIB_FALLBACK_ACTIVE_HINT,
    STR_CALIB_AUTO_ACTIVE,
    STR_CALIB_FALLBACK_EXPLAIN,
    STR_CALIB_BTN_FORCE_FALLBACK,
    STR_CALIB_BTN_RESTORE_AUTO,
    STR_CALIB_CLEAR_HINT,
    STR_CALIB_BTN_CLEAR,
    STR_CALIB_NO_DATA,
    STR_CALIB_SUCCESS,
    STR_CALIB_BTN_SAVE,
    STR_CALIB_SAVED_APPLIED,
    STR_CALIB_SAVE_FAILED,
    STR_CALIB_BTN_CLOSE,
    STR_CALIB_GESTURE_DONE,
    STR_CALIB_GESTURE_CONTINUE,
    STR_CALIB_BTN_VALIDATE_STEP,
    STR_CALIB_VALIDATE_DISABLED_HINT,
    STR_CALIB_BTN_NO_WHEEL,
    STR_CALIB_BTN_CANCEL,

    // --- main.cpp : DrawMouseSettingsTab (onglet "Reglages souris") ---
    STR_SETTINGS_SENSITIVITY_LABEL,
    STR_SETTINGS_DEADZONE_LABEL,
    STR_SETTINGS_INVERT_Y_LABEL,
    STR_SETTINGS_BTN_SAVE,
    STR_SETTINGS_MOUSE_BUTTONS_HEADER,
    STR_SETTINGS_CLICK_LEFT_LABEL,
    STR_SETTINGS_CLICK_RIGHT_LABEL,
    STR_SETTINGS_CLICK_MIDDLE_LABEL,
    STR_SETTINGS_CLICK_DEFAULT_HINT,
    STR_SETTINGS_WHEEL_FORWARD_LABEL,
    STR_SETTINGS_WHEEL_BACKWARD_LABEL,
    STR_SETTINGS_WHEEL_DEFAULT_HINT,
    STR_SETTINGS_BTN_SAVE_BUTTONS,
    STR_SETTINGS_DEFAULTS_HEADER,
    STR_SETTINGS_BTN_SAVE_AS_DEFAULT,
    STR_SETTINGS_NO_DEFAULT_SAVED,
    STR_SETTINGS_BTN_RESTORE_DEFAULT,
    // Libelles de kMouseButtonTargets (combos "Clic gauche/droit/milieu" et
    // "Molette avant/arriere") - section separee des libelles d'etape de
    // wizard_session.cpp meme quand le texte se ressemble (ex: "Start"),
    // ce sont deux tableaux independants dans deux fichiers differents.
    STR_SETTINGS_TARGET_DEFAULT,
    STR_SETTINGS_TARGET_A,
    STR_SETTINGS_TARGET_B,
    STR_SETTINGS_TARGET_X,
    STR_SETTINGS_TARGET_Y,
    STR_SETTINGS_TARGET_LB,
    STR_SETTINGS_TARGET_RB,
    STR_SETTINGS_TARGET_LT,
    STR_SETTINGS_TARGET_RT,
    STR_SETTINGS_TARGET_L3,
    STR_SETTINGS_TARGET_R3,
    STR_SETTINGS_TARGET_START,
    STR_SETTINGS_TARGET_BACK,
    STR_SETTINGS_TARGET_DPAD_UP,
    STR_SETTINGS_TARGET_DPAD_DOWN,
    STR_SETTINGS_TARGET_DPAD_LEFT,
    STR_SETTINGS_TARGET_DPAD_RIGHT,

    // --- main.cpp : DrawProfilesTab (onglet "Profils") ---
    STR_PROFILES_INTRO,
    STR_PROFILES_F9_HINT,
    STR_PROFILES_COMBO_LABEL,
    STR_PROFILES_COMBO_NEW,
    STR_PROFILES_KNOWN_TITLES_COMBO_LABEL,
    STR_PROFILES_KNOWN_TITLES_HINT,
    STR_PROFILES_NO_KNOWN_TITLES,
    STR_PROFILES_RADIO_TYPE_TITLEID,
    STR_PROFILES_RADIO_TYPE_GAMENAME,
    STR_PROFILES_INPUT_TITLEID,
    STR_PROFILES_HINT_TITLEID_CHARS,
    STR_PROFILES_INPUT_GAME_NAME,
    STR_PROFILES_HINT_GAME_NAME_CHARS,
    STR_PROFILES_TITLEID_DISPLAY_FMT,
    STR_PROFILES_SENSITIVITY_LABEL,
    STR_PROFILES_DEADZONE_LABEL,
    STR_PROFILES_INVERT_Y_LABEL,
    STR_PROFILES_CURVE_LINEAR,
    STR_PROFILES_CURVE_EXPONENTIAL,
    STR_PROFILES_CURVE_COMBO_LABEL,
    STR_PROFILES_EXPONENT_LABEL,
    STR_PROFILES_EXPONENT_HINT,
    STR_PROFILES_BTN_SAVE,
    STR_PROFILES_BTN_DELETE,
    STR_PROFILES_NONE_SAVED,

    // --- main.cpp : DrawDeviceSelector ---
    STR_DEVICE_HEADER,
    STR_DEVICE_KEYBOARD_LABEL,
    STR_DEVICE_MOUSE_LABEL,
    STR_DEVICE_BTN_REFRESH,
    STR_DEVICE_ONE_DEFAULT_HINT,
    STR_DEVICE_NAME_UNAVAILABLE_FMT,
    STR_DEVICE_DEFAULT_LABEL_FMT,

    // --- main.cpp : titre fenetre / barre d'onglets (main()) ---
    STR_APP_WINDOW_TITLE,
    STR_APP_TAB_WIZARD,
    STR_APP_TAB_SETTINGS,
    STR_APP_TAB_PROFILES,
    STR_APP_TAB_CALIBRATION,
    // Ajoutes 2026-08-11 (demande utilisateur : deux pages supplementaires -
    // "comment ca marche" et une page de soutien/dons)
    STR_APP_TAB_HOWTO,
    STR_APP_TAB_SUPPORT,
    // Refonte v1.2 : onglet unique clavier+souris, avec bascule
    STR_APP_TAB_MAPPING,
    STR_MAPPING_MODE_KEYBOARD,
    STR_MAPPING_MODE_MOUSE,
    // Texte d'accompagnement de la colonne de droite en mode Clavier : sans
    // lui, le panneau ne contient qu'un bouton isole dans une grande zone
    // vide, ce qui donne l'impression d'un ecran inacheve.
    STR_MAPPING_KEYBOARD_HINT,
    // Reglages manuels (2026-08-17, demande HB) : reassigner UN bouton sans
    // repasser par les 21 etapes du wizard - clic sur la case, une touche.
    STR_MANUAL_BTN_START,
    STR_MANUAL_BTN_STOP,
    STR_MANUAL_HINT,
    STR_MANUAL_CAPTURE_TITLE,
    STR_MANUAL_CAPTURE_HINT,
    // Panneau contextuel du bouton selectionne (colonne de droite, Mapping)
    STR_MAPPING_CTX_ASSIGNED,
    STR_MAPPING_CTX_HINT,
    // Bloc d'etat et grille (ui_mapping.cpp, source unique de l'ecran Mapping)
    STR_UI_PLUGIN_ALIVE,
    STR_UI_PLUGIN_SILENT,
    STR_UI_DEVICES_DETECTED_FMT,
    STR_UI_ADVANCED,
    STR_UI_GRID_TITLE_FMT,
    STR_MAPPING_MOUSE_HINT,
    STR_UI_EXTRA_BUTTONS_SOON,
    // Titre du panneau bas en mode souris - pendant de STR_UI_GRID_TITLE_FMT
    // cote clavier. Ajoute le 2026-08-14 : le panneau reutilisait le libelle
    // "Sensibilite", qui annoncait donc un seul reglage pour tout le panneau.
    STR_UI_MOUSE_PANEL_TITLE,
    // Titre COURT de la colonne des peripheriques. STR_DEVICE_HEADER existait
    // deja mais dit "Peripheriques actifs (Clavier / Souris)" - trop long pour
    // une colonne de 3 unites de grille, et redondant avec les deux
    // sous-titres juste en dessous.
    STR_UI_DEVICES_PANEL_TITLE,
    // Titres des deux cartes du sous-panneau "Reglages avances". Les chaines
    // STR_CALIB_* existantes sont des PHRASES D'ETAT ("Lecture automatique
    // active (recommande) - molette geree."), pas des titres : utilisees comme
    // telles elles debordaient de leur carte.
    STR_UI_ADV_CALIB_TITLE,
    STR_UI_ADV_FALLBACK_TITLE,
    // Le panneau contextuel n'existe QUE s'il y a une selection (voir
    // UiSelection dans ui_mapping.h). Ces deux chaines accompagnent cet etat :
    // l'invitation quand rien n'est selectionne, et la fermeture explicite.
    STR_MAPPING_PICK_HINT,
    STR_MAPPING_CTX_CLOSE,
    // Variantes SOURIS du panneau contextuel. Les deux chaines clavier
    // ("TOUCHE ASSIGNEE", "Appuie sur une touche de ton clavier") etaient
    // affichees telles quelles pour un reglage de souris, ou elles ne veulent
    // rien dire.
    STR_MAPPING_CTX_MAPPED_TO,
    STR_MAPPING_CTX_HINT_MOUSE,

    // --- wizard_session.cpp : DefaultKeyboardWizardSteps (libelles d'etape) ---
    STR_WIZSTEP_A,
    STR_WIZSTEP_B,
    STR_WIZSTEP_X,
    STR_WIZSTEP_Y,
    STR_WIZSTEP_LB,
    STR_WIZSTEP_RB,
    STR_WIZSTEP_LT,
    STR_WIZSTEP_RT,
    STR_WIZSTEP_L3,
    STR_WIZSTEP_R3,
    STR_WIZSTEP_START,
    STR_WIZSTEP_BACK,
    STR_WIZSTEP_GUIDE,
    STR_WIZSTEP_DPAD_UP,
    STR_WIZSTEP_DPAD_DOWN,
    STR_WIZSTEP_DPAD_LEFT,
    STR_WIZSTEP_DPAD_RIGHT,
    STR_WIZSTEP_LSTICK_UP,
    STR_WIZSTEP_LSTICK_DOWN,
    STR_WIZSTEP_LSTICK_LEFT,
    STR_WIZSTEP_LSTICK_RIGHT,

    // --- calibration_session.cpp : Prompt() (consigne par etape) ---
    STR_CALSTEP_REST,
    STR_CALSTEP_RIGHT,
    STR_CALSTEP_LEFT,
    STR_CALSTEP_DOWN,
    STR_CALSTEP_UP,
    STR_CALSTEP_WHEEL_FWD,
    STR_CALSTEP_WHEEL_BACK,
    STR_CALSTEP_CLICK_LEFT,
    STR_CALSTEP_DONE,
    STR_CALSTEP_FAILED,

    // --- calibration_session.cpp : Summary() (resume construit via _snprintf) ---
    STR_CALSUM_FAIL_FMT,
    STR_CALSUM_YES,
    STR_CALSUM_NO_CAPS,
    STR_CALSUM_NO,
    STR_CALSUM_WHEEL_NONE,
    STR_CALSUM_WHEEL_BYTE_FMT,
    STR_CALSUM_SUCCESS_FMT,

    // --- main.cpp : DrawHowItWorksTab (onglet "Comment ca marche",
    // ajoute 2026-08-11) - reste simple, texte uniquement, pas de nouvel
    // ecran/wizard : explique le fonctionnement general + la liaison
    // rapide (Back+Start) + le raccourci F9. ---
    STR_HOWTO_INTRO,
    STR_HOWTO_STEP_PLUG,
    STR_HOWTO_STEP_WIZARD,
    STR_HOWTO_STEP_SETTINGS,
    STR_HOWTO_STEP_PROFILES,
    STR_HOWTO_QUICKBIND_HEADER,
    STR_HOWTO_QUICKBIND_TEXT,
    STR_HOWTO_F9_HEADER,
    STR_HOWTO_F9_TEXT,

    // --- main.cpp : DrawSupportTab (onglet "Soutenir le projet",
    // ajoute 2026-08-11) - message du developpeur + liens de don, voir
    // donation_links.h pour les chaines de lien elles-memes (non traduites,
    // ce sont des identifiants/URLs, pas du texte d'interface). ---
    STR_SUPPORT_BADGE,
    STR_SUPPORT_MESSAGE,
    STR_SUPPORT_CTA,
    STR_SUPPORT_PAYPAL_LABEL,
    STR_SUPPORT_KOFI_LABEL,
    STR_SUPPORT_GITHUB_LABEL,
    STR_SUPPORT_YOUTUBE_LABEL,
    STR_SUPPORT_LINK_HIDDEN_HINT,
    // Refonte v1.2 : les libelles disent l'INTENTION, pas le nom du service
    // ("Verifier les mises a jour" plutot que "GitHub" - l'utilisateur type
    // est un joueur, pas un developpeur).
    STR_SUPPORT_VERSION_FMT,
    STR_SUPPORT_WHATSNEW,
    STR_SUPPORT_HEADLINE,
    STR_SUPPORT_CARD_DONATE,
    STR_SUPPORT_CARD_DONATE_SUB,
    STR_SUPPORT_CARD_UPDATE,
    STR_SUPPORT_CARD_UPDATE_SUB,
    STR_SUPPORT_CARD_FOLLOW,
    STR_SUPPORT_CARD_FOLLOW_SUB,
    STR_SUPPORT_CARD_FEEDBACK,
    // QR Ko-fi (2026-08-17, demande HB - fichier fourni) : petite legende
    // sous le QR, pour le cas ou le lecteur ne sait pas ce qu'un QR fait la.
    STR_SUPPORT_QR_HINT,

    // Mesure de la resolution de la souris (2026-09-23) - ajoutees EN
    // DERNIER, comme toujours (voir CheckStringTableIntegrity).
    STR_MEASURE_STATUS_UNKNOWN,
    STR_MEASURE_STATUS_PREFIX,
    STR_MEASURE_BTN_START,
    STR_MEASURE_TITLE,
    STR_MEASURE_STEP_SHEET,
    STR_MEASURE_STEP_DRAG,
    STR_MEASURE_LIVE_FMT,
    STR_MEASURE_RESULT_FMT,
    STR_MEASURE_IMPLAUSIBLE,
    STR_MEASURE_NO_MOUSE,
    STR_MEASURE_NO_DEVICE,
    STR_MEASURE_WHY,
    STR_MEASURE_BTN_SAVE,
    STR_MEASURE_BTN_RETRY,
    STR_MEASURE_BTN_CANCEL,
    STR_MEASURE_SAVE_FAILED,

    // --- Page Clavier & souris "glossy" (ui_glossy.cpp, 2026-09-24) ---
    STR_GLOSSY_TAB_KBMOUSE,
    STR_GLOSSY_KB_TITLE,
    STR_GLOSSY_MS_TITLE,
    STR_GLOSSY_WIZARD_FMT,
    STR_GLOSSY_ADD,
    STR_GLOSSY_DPI_FMT,
    STR_GLOSSY_NONE,
    STR_GLOSSY_CAPTURE,
    STR_GLOSSY_INVERT,
    STR_GLOSSY_CALIBRATE,
    STR_GLOSSY_HINT_CHANGE,
    STR_GLOSSY_REFRESH,
    STR_GLOSSY_TAB_GAMES,
    STR_GLOSSY_TAB_SUPPORT,
    STR_GLOSSY_DEF_RESTORE,
    STR_GLOSSY_DEF_SAVE,
    STR_GLOSSY_CONFIRM,
    STR_GLOSSY_DEF_RESTORE_TIP,
    STR_GLOSSY_DEF_FACTORY_TIP,
    STR_GLOSSY_DEF_SAVE_TIP,
    STR_GLOSSY_MEASURE_WAIT,

    // --- Page Profils de jeux (2026-09-25) ---
    STR_GAMES_PROFILES_FOR_FMT,
    STR_GAMES_FILTER_ALL,
    STR_GAMES_FILTER_PROFILE,
    STR_GAMES_EMPTY,
    STR_GAMES_PICK,
    STR_GAMES_NO_AURORA,
    STR_GAMES_HAS_PROFILE,
    STR_GAMES_NO_PROFILE,
    STR_GAMES_SAVE,
    STR_GAMES_DELETE,
    STR_GAMES_RENAME_HINT,
    STR_GAMES_RENAME_EDIT,
    STR_GAMES_COUNT_FMT,
    STR_GAMES_DELETE_ALL,
    STR_GAMES_SAVED,

    // --- Page Comment ca marche glossy (2026-09-25) ---
    STR_HT_INTRO,
    STR_HT_S1_T,
    STR_HT_S1,
    STR_HT_S2_T,
    STR_HT_S2,
    STR_HT_S3_T,
    STR_HT_S3,
    STR_HT_S4_T,
    STR_HT_S4,
    STR_HT_INGAME,
    STR_HT_HELP_T,
    STR_HT_HELP,
    STR_GLOSSY_TAB_HOWTO,

    // --- Page Soutenir glossy (2026-09-25) ---
    STR_SUP_UPDATES_T,
    STR_SUP_CURRENT,
    STR_SUP_UPDATES_TXT,
    STR_SUP_UPDATES_WHY,
    STR_SUP_SUPPORT_T,
    STR_SETTINGS_ANTIDZ_LABEL,   // 2026-09-25
    STR_SETTINGS_ANTIDZ_HINT,
    STR_HT_LIVE_T,               // reglages en jeu F5..F11 (2026-09-25)
    STR_HT_LIVE,
    STR_GAMES_STICKS,
    STR_STICKS_DEFAULT,
    STR_STICKS_SOUTHPAW,
    STR_STICKS_LEGACY,
    STR_STICKS_LEGACY_SP,
    STR_GAMES_STICKS_TIP,
    STR_SETTINGS_SMOOTH_LABEL,
    STR_SETTINGS_SMOOTH_OFF,
    STR_SETTINGS_SMOOTH_HINT,
    STR_SETTINGS_ANTIDZ_Y_HINT,
    STR_GAMES_CURVE_LABEL,
    STR_GAMES_CURVE_HINT,
    STR_GAMES_COUNT1_FMT,        // singulier : 0 ou 1 profil
    STR_EXIT_CONFIRM,            // Echap x2 -> Aurora (2026-09-26)

    STR_COUNT
};

// Renvoie la chaine de l'id donne pour la langue active (GetLanguage()). Si
// jamais la case courante est nulle (table mal remplie), retombe sur LANG_FR
// plutot que de renvoyer nullptr - un ImGui::Text(nullptr) planterait.
const char* Tr(StrId id);

// --- Verification d'integrite de la table (2026-08-14) ----------------------
//
// POURQUOI : la table de i18n.cpp est indexee POSITIONNELLEMENT par StrId. Une
// chaine inseree au bon endroit dans l'enum mais a un autre endroit dans la
// table decale tout un bloc, silencieusement - aucune erreur de compilation,
// et l'application affiche simplement le mauvais texte au mauvais endroit.
// C'est arrive le 2026-08-14 : le panneau contextuel montrait le texte de
// l'assistant, et la colonne voisine affichait "ASSIGNED KEY".
//
// Compter les chaines vides ne detecte PAS ce cas : toutes les cases sont
// remplies, elles sont juste decalees. Chaque entree porte donc desormais son
// propre identifiant, et cette fonction verifie qu'il correspond bien a sa
// position.
//
// Renvoie -1 si tout est coherent, sinon l'INDEX de la premiere entree fautive
// (a appeler une fois au demarrage, et dans les suites de tests portables).
int CheckStringTableIntegrity();

Lang GetLanguage();

// En memoire uniquement - pas d'ecriture disque ici (voir SaveLanguagePref
// pour la variante qui persiste). Utile pour changer la langue sans vouloir
// forcement la rendre permanente (ex: tests).
void SetLanguage(Lang lang);

// A appeler une fois au demarrage de l'appli (voir main(), application/main.cpp) :
// lit la langue sauvegardee sur disque et l'applique. Si le fichier est
// absent ou son contenu non reconnu, reste sur LANG_FR (comportement actuel
// avant l'ajout de ce systeme).
void LoadLanguagePref();

// Ecrit la langue choisie sur disque ET l'applique immediatement (appelle
// SetLanguage) - c'est ce que l'UI doit appeler quand l'utilisateur choisit
// une langue (voir le selecteur de langue dans DrawMouseSettingsTab).
void SaveLanguagePref(Lang lang);
