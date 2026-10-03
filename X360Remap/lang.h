#pragma once
// Systeme de traduction FR/EN pour les notifications a l'ecran (XNotifyUI)
// de X360Remap.xex (le plugin). Contrairement a application/i18n.h, ce
// fichier N'A PAS besoin de rester portable/g++ (le plugin X360Remap n'est pas compile
// par la suite de tests portable de application/) : xtl.h et les types
// wide-char de la XDK sont utilisables librement ici.
//
// X360Remap.xex et application.xex sont deux binaires .xex separes, sans
// memoire partagee : chacun lit independamment le meme fichier de
// preference sur HDD: pour rester synchronise (voir LoadPluginLanguageSetting
// ci-dessous, et SaveLanguagePref/LoadLanguagePref dans application/i18n.h/
// i18n.cpp pour le cote qui ECRIT ce fichier).
//
// LANG_FR/LANG_EN/LANG_PT_BR doivent garder exactement les memes valeurs
// numeriques que application/i18n.h (Lang) : les deux binaires lisent le
// meme octet "0"/"1"/"2" sur disque avec la meme convention.
//
// Portugais AJOUTE le 2026-08-24, mais ICI SEULEMENT EN ASCII PUR - a la
// difference de application/i18n.cpp, qui utilise desormais de vrais accents
// (police ImGui verifiee, voir son commentaire). XNotifyUI utilise la police
// SYSTEME du dashboard, dont le comportement avec des caracteres accentues
// n'a jamais ete verifie sur hardware - donc meme convention ASCII-only que
// le francais ici, par prudence, jusqu'a verification.
// LANG_ES ajoute le 2026-09-13, EN DERNIERE POSITION (la valeur numerique des
// langues existantes est ecrite dans X360Remap_lang.txt chez les utilisateurs).
// DOIT rester synchronise avec l'enum Lang de application/i18n.h.
enum Lang { LANG_FR = 0, LANG_EN = 1, LANG_PT_BR = 2, LANG_ES = 3, LANG_COUNT };

// Un NotifyStrId par gabarit XNotifyUI distinct dans X360Remap/main.cpp.
// Regroupe dans l'ordre d'apparition des appels XNotifyUI() dans main.cpp.
enum NotifyStrId {
	// MappingManagerThreadProc : notification souris/clavier detecte
	STR_MOUSE_DETECTED_FMT,          // "Mouse detected. VID:%04x PID:%04x map:%hs btns:%d sens:%d slot:%d"
	STR_KEYBOARD_READY_FMT,          // "Keyboard ready. ... (slot %d)"

	// MappingManagerThreadProc : hot-reload de X360Remap.json
	STR_JSON_RELOADED,               // "X360Remap.json reloaded"
	STR_JSON_INVALID,                // "X360Remap.json invalid - check syntax"

	// MappingManagerThreadProc : raccourci F9 "sauvegarder le profil"
	STR_PROFILE_SAVED,               // "Profil enregistre pour ce jeu"
	STR_PROFILE_SAVE_FAILED,         // "Echec de l'enregistrement du profil"

	// MappingManagerThreadProc : liaison rapide (quick bind)
	STR_QUICKBIND_CANCELED,          // "Liaison rapide annulee"
	STR_QUICKBIND_ARMED,             // "Liaison rapide : appuie sur le bouton manette a lier"
	STR_QUICKBIND_TARGET_FMT,        // "Cible : %s - appuie sur la touche ou le clic a associer"
	STR_QUICKBIND_BOUND_FMT,         // "%s lie pour ce jeu"
	STR_QUICKBIND_BIND_FAILED_FMT,   // "Echec de la liaison de %s"

	// MappingThreadProc : assistant de mapping manette
	STR_UNKNOWN_CONTROLLER,          // "Unknown controller connected. Starting mapping process..."
	STR_PRESS_BUTTON_FMT,            // "Press %hs on controller (hold 3s to skip)"
	STR_MAPPING_SKIPPED,             // "Mapping skipped"
	STR_MAPPING_COMPLETE,            // "Mapping complete! Controller ready."

	// DllMain : aucune configuration trouvee au demarrage (2026-09-13).
	// Ajoute EN DERNIERE POSITION volontairement - toute insertion au milieu
	// decalerait silencieusement toutes les chaines suivantes dans
	// g_notifyStrings (incident du 2026-08-14, voir le filet anti-decalage
	// dans application/main.cpp).
	STR_INSTALL_INCOMPLETE,          // "X360Remap loaded but install is incomplete - X360RemapStudio app is also required"

	// MappingManagerThreadProc : reglages en jeu F5..F8/F10/F11 (2026-09-25).
	// Ajoutes EN DERNIER, meme raison que ci-dessus.
	STR_LIVE_SENS_FMT,               // "Sensibilite : %d (ce jeu)"
	STR_LIVE_DZ_FMT,                 // "Zone morte : %d (ce jeu)"
	STR_LIVE_ADZ_FMT,                // "Anti-zone morte : %d (ce jeu)"
	STR_LIVE_NEED_GAME,              // "Reglage en jeu : lancez un jeu avec une souris branchee"
	STR_LIVE_ADZY_FMT,               // "Anti-zone morte verticale : %d (ce jeu)" (2026-09-26)
	STR_LIVE_SMOOTH_FMT,             // "Lissage : %d ms (ce jeu)"
	STR_MOUSEHOOK_ON_FMT,            // "Visee directe active : %hs" (2026-09-26)

	STR_NOTIFY_COUNT
};

// Langue active des notifications du plugin. Par defaut LANG_FR, comme
// g_currentLang dans application/i18n.cpp, jusqu'a ce que
// LoadPluginLanguageSetting() lise le fichier de preference (ou que ce
// fichier soit absent/non reconnu, auquel cas on reste sur LANG_FR).
extern Lang g_pluginLang;

// Lu une seule fois au demarrage (voir l'appel unique juste a cote de
// LoadBootProtocolSetting() dans main.cpp) - PAS a chaque tick du thread de
// polling. Meme fichier, meme convention "0"/"1" qu'application/i18n.h :
// "0" = LANG_FR, "1" = LANG_EN, absent/non reconnu = LANG_FR. Meme pattern
// std::ifstream que LoadBootProtocolSetting() (voir main.cpp) pour rester
// coherent avec le reste du fichier.
// Prend le chemin complet du fichier de preference en parametre depuis le
// 2026-09-13 : il ne vit plus forcement sur "HDD:" (console sans disque dur -
// voir g_studioDrive/StudioPath dans main.cpp, qui resout le support reel au
// demarrage). Ce fichier ne pouvant pas voir cette fonction interne a
// main.cpp, c'est l'appelant qui construit le chemin.
void LoadPluginLanguageSetting(const char* langFilePath);

// Renvoie la chaine large (wchar_t) de l'id donne pour g_pluginLang. Si la
// case courante est nulle (table mal remplie), retombe sur la ligne LANG_FR
// plutot que de renvoyer nullptr - un XNotifyUI(nullptr) serait dangereux.
const wchar_t* WTr(NotifyStrId id);
