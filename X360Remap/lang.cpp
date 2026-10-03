#include "lang.h"
#include <fstream>
#include <string>
#include <iterator>

// FileLog() is defined in main.cpp (no header of its own - forward-declared
// wherever it is used outside main.cpp, same as here). Same signature/
// convention as every other FileLog() call site in this project.
void FileLog(const char* fmt, ...);

// ANGLAIS PAR DEFAUT depuis le 2026-09-13 (etait LANG_FR).
//
// Signale par un testeur hispanophone : "could you please change the default
// language to english because I don't understand french when the console boots
// and detects the input devices". Le probleme est structurel, pas cosmetique :
// les notifications du plugin apparaissent DES le premier demarrage, avant que
// l'utilisateur ait pu ouvrir l'application pour choisir sa langue. Un defaut
// francais condamne donc tout non-francophone a ne rien comprendre au premier
// contact avec le produit - exactement le moment ou il decide si ca marche ou
// non.
//
// L'anglais n'est pas "mieux", c'est le plus petit denominateur commun d'une
// audience internationale. Les francophones basculent en deux clics dans
// l'application, et ce choix est memorise (X360Remap_lang.txt).
Lang g_pluginLang = LANG_EN;

// [LANG_FR][...] chaines existantes de main.cpp copiees verbatim (aucune
// reformulation), y compris l'absence d'accents deja en usage dans ce
// projet (meme convention que application/i18n.cpp). [LANG_EN][...]
// traductions/originaux anglais - ton technique/terse identique, pas de
// texte marketing. Les gabarits swprintf (%s/%hs/%04x/%d/...) gardent
// exactement les memes specificateurs, dans le meme ordre, dans les deux
// lignes : les appels swprintf existants dans main.cpp n'ont pas a changer.
static const wchar_t* g_notifyStrings[LANG_COUNT][STR_NOTIFY_COUNT] = {
	// LANG_FR
	{
		/* STR_MOUSE_DETECTED_FMT        */ L"Souris detectee. VID:%04x PID:%04x map:%hs btns:%d sens:%d slot:%d",
		/* STR_KEYBOARD_READY_FMT        */ L"Clavier pret. WASD=deplacement, Espace=A, C=B, R=X, E=Y, Echap=Start (slot %d)",
		/* STR_JSON_RELOADED             */ L"X360Remap.json recharge",
		/* STR_JSON_INVALID              */ L"X360Remap.json invalide - verifier la syntaxe",
		/* STR_PROFILE_SAVED             */ L"Profil enregistre pour ce jeu",
		/* STR_PROFILE_SAVE_FAILED       */ L"Echec de l'enregistrement du profil",
		/* STR_QUICKBIND_CANCELED        */ L"Liaison rapide annulee",
		/* STR_QUICKBIND_ARMED           */ L"Liaison rapide : appuie sur le bouton manette a lier",
		/* STR_QUICKBIND_TARGET_FMT      */ L"Cible : %s - appuie sur la touche ou le clic a associer",
		/* STR_QUICKBIND_BOUND_FMT       */ L"%s lie pour ce jeu",
		/* STR_QUICKBIND_BIND_FAILED_FMT */ L"Echec de la liaison de %s",
		/* STR_UNKNOWN_CONTROLLER        */ L"Manette inconnue connectee. Demarrage du processus de mapping...",
		/* STR_PRESS_BUTTON_FMT          */ L"Appuie sur %hs sur la manette (maintenir 3s pour passer)",
		/* STR_MAPPING_SKIPPED           */ L"Mapping ignore",
		/* STR_MAPPING_COMPLETE          */ L"Mapping termine ! Manette prete.",
		/* STR_INSTALL_INCOMPLETE        */ L"X360Remap actif avec les commandes par defaut - lancez X360RemapStudio une fois pour terminer l'installation",
		/* STR_LIVE_SENS_FMT             */ L"Sensibilite : %d (ce jeu)",
		/* STR_LIVE_DZ_FMT               */ L"Zone morte : %d (ce jeu)",
		/* STR_LIVE_ADZ_FMT              */ L"Anti-zone morte : %d (ce jeu)",
		/* STR_LIVE_NEED_GAME            */ L"Reglage en jeu : lancez un jeu avec une souris branchee",
		/* STR_LIVE_ADZY_FMT             */ L"Anti-zone morte verticale : %d (ce jeu)",
		/* STR_LIVE_SMOOTH_FMT           */ L"Lissage : %d ms (ce jeu)",
		/* STR_MOUSEHOOK_ON_FMT          */ L"Visee directe prete : %hs (active en partie)",
	},
	// LANG_EN
	{
		/* STR_MOUSE_DETECTED_FMT        */ L"Mouse detected. VID:%04x PID:%04x map:%hs btns:%d sens:%d slot:%d",
		/* STR_KEYBOARD_READY_FMT        */ L"Keyboard ready. WASD=move, Space=A, C=B, R=X, E=Y, Esc=Start (slot %d)",
		/* STR_JSON_RELOADED             */ L"X360Remap.json reloaded",
		/* STR_JSON_INVALID              */ L"X360Remap.json invalid - check syntax",
		/* STR_PROFILE_SAVED             */ L"Profile saved for this game",
		/* STR_PROFILE_SAVE_FAILED       */ L"Failed to save profile",
		/* STR_QUICKBIND_CANCELED        */ L"Quick bind canceled",
		/* STR_QUICKBIND_ARMED           */ L"Quick bind: press the controller button to bind",
		/* STR_QUICKBIND_TARGET_FMT      */ L"Target: %s - press the key or click to assign",
		/* STR_QUICKBIND_BOUND_FMT       */ L"%s bound for this game",
		/* STR_QUICKBIND_BIND_FAILED_FMT */ L"Failed to bind %s",
		/* STR_UNKNOWN_CONTROLLER        */ L"Unknown controller connected. Starting mapping process...",
		/* STR_PRESS_BUTTON_FMT          */ L"Press %hs on controller (hold 3s to skip)",
		/* STR_MAPPING_SKIPPED           */ L"Mapping skipped",
		/* STR_MAPPING_COMPLETE          */ L"Mapping complete! Controller ready.",
		/* STR_INSTALL_INCOMPLETE        */ L"X360Remap active with default controls - run X360RemapStudio once to finish setup",
		/* STR_LIVE_SENS_FMT             */ L"Sensitivity: %d (this game)",
		/* STR_LIVE_DZ_FMT               */ L"Dead zone: %d (this game)",
		/* STR_LIVE_ADZ_FMT              */ L"Anti-deadzone: %d (this game)",
		/* STR_LIVE_NEED_GAME            */ L"In-game tuning: start a game with a mouse plugged in",
		/* STR_LIVE_ADZY_FMT             */ L"Vertical anti-deadzone: %d (this game)",
		/* STR_LIVE_SMOOTH_FMT           */ L"Smoothing: %d ms (this game)",
		/* STR_MOUSEHOOK_ON_FMT          */ L"Direct aim ready: %hs (works in gameplay)",
	},
	// LANG_PT_BR (2026-08-24) - ASCII pur volontairement, voir le commentaire
	// dans lang.h : police systeme XNotifyUI non verifiee avec des accents.
	{
		/* STR_MOUSE_DETECTED_FMT        */ L"Mouse detectado. VID:%04x PID:%04x map:%hs btns:%d sens:%d slot:%d",
		/* STR_KEYBOARD_READY_FMT        */ L"Teclado pronto. WASD=movimento, Espaco=A, C=B, R=X, E=Y, Esc=Start (slot %d)",
		/* STR_JSON_RELOADED             */ L"X360Remap.json recarregado",
		/* STR_JSON_INVALID              */ L"X360Remap.json invalido - verificar sintaxe",
		/* STR_PROFILE_SAVED             */ L"Perfil salvo para este jogo",
		/* STR_PROFILE_SAVE_FAILED       */ L"Falha ao salvar o perfil",
		/* STR_QUICKBIND_CANCELED        */ L"Vinculacao rapida cancelada",
		/* STR_QUICKBIND_ARMED           */ L"Vinculacao rapida: pressione o botao do controle a vincular",
		/* STR_QUICKBIND_TARGET_FMT      */ L"Alvo: %s - pressione a tecla ou o clique a associar",
		/* STR_QUICKBIND_BOUND_FMT       */ L"%s vinculado para este jogo",
		/* STR_QUICKBIND_BIND_FAILED_FMT */ L"Falha ao vincular %s",
		/* STR_UNKNOWN_CONTROLLER        */ L"Controle desconhecido conectado. Iniciando processo de mapeamento...",
		/* STR_PRESS_BUTTON_FMT          */ L"Pressione %hs no controle (segure 3s para pular)",
		/* STR_MAPPING_SKIPPED           */ L"Mapeamento ignorado",
		/* STR_MAPPING_COMPLETE          */ L"Mapeamento concluido! Controle pronto.",
		/* STR_INSTALL_INCOMPLETE        */ L"X360Remap ativo com os controles padrao - abra o X360RemapStudio uma vez para concluir a instalacao",
		/* STR_LIVE_SENS_FMT             */ L"Sensibilidade: %d (este jogo)",
		/* STR_LIVE_DZ_FMT               */ L"Zona morta: %d (este jogo)",
		/* STR_LIVE_ADZ_FMT              */ L"Antizona morta: %d (este jogo)",
		/* STR_LIVE_NEED_GAME            */ L"Ajuste no jogo: abra um jogo com um mouse conectado",
		/* STR_LIVE_ADZY_FMT             */ L"Antizona morta vertical: %d (este jogo)",
		/* STR_LIVE_SMOOTH_FMT           */ L"Suavizacao: %d ms (este jogo)",
		/* STR_MOUSEHOOK_ON_FMT          */ L"Mira direta pronta: %hs (ativa na partida)",
	},
	// LANG_ES (2026-09-13) - ASCII pur volontairement, meme prudence que le
	// francais : ces chaines passent par XNotifyUI, dont le rendu des accents
	// avec la police SYSTEME du dashboard n'a jamais ete verifie sur hardware
	// (voir le commentaire en tete de lang.h). Les accents espagnols sont donc
	// omis ici, contrairement a l'application (qui utilise sa propre police
	// ImGui, verifiee). A corriger si le rendu accentue est un jour confirme.
	{
		/* STR_MOUSE_DETECTED_FMT        */ L"Raton detectado. VID:%04x PID:%04x map:%hs btns:%d sens:%d slot:%d",
		/* STR_KEYBOARD_READY_FMT        */ L"Teclado listo. WASD=movimiento, Espacio=A, C=B, R=X, E=Y, Esc=Start (slot %d)",
		/* STR_JSON_RELOADED             */ L"X360Remap.json recargado",
		/* STR_JSON_INVALID              */ L"X360Remap.json no valido - revisa la sintaxis",
		/* STR_PROFILE_SAVED             */ L"Perfil guardado para este juego",
		/* STR_PROFILE_SAVE_FAILED       */ L"Error al guardar el perfil",
		/* STR_QUICKBIND_CANCELED        */ L"Asignacion rapida cancelada",
		/* STR_QUICKBIND_ARMED           */ L"Asignacion rapida: pulsa el boton del mando a reasignar",
		/* STR_QUICKBIND_TARGET_FMT      */ L"Objetivo: %s - pulsa la tecla o el clic a asociar",
		/* STR_QUICKBIND_BOUND_FMT       */ L"%s asignado para este juego",
		/* STR_QUICKBIND_BIND_FAILED_FMT */ L"Error al asignar %s",
		/* STR_UNKNOWN_CONTROLLER        */ L"Mando desconocido conectado. Iniciando el proceso de asignacion...",
		/* STR_PRESS_BUTTON_FMT          */ L"Pulsa %hs en el mando (manten 3s para saltar)",
		/* STR_MAPPING_SKIPPED           */ L"Asignacion omitida",
		/* STR_MAPPING_COMPLETE          */ L"Asignacion completada! Mando listo.",
		/* STR_INSTALL_INCOMPLETE        */ L"X360Remap activo con los controles por defecto - abre X360RemapStudio una vez para terminar la instalacion",
		/* STR_LIVE_SENS_FMT             */ L"Sensibilidad: %d (este juego)",
		/* STR_LIVE_DZ_FMT               */ L"Zona muerta: %d (este juego)",
		/* STR_LIVE_ADZ_FMT              */ L"Anti-zona muerta: %d (este juego)",
		/* STR_LIVE_NEED_GAME            */ L"Ajuste en el juego: abre un juego con un raton conectado",
		/* STR_LIVE_ADZY_FMT             */ L"Anti-zona muerta vertical: %d (este juego)",
		/* STR_LIVE_SMOOTH_FMT           */ L"Suavizado: %d ms (este juego)",
		/* STR_MOUSEHOOK_ON_FMT          */ L"Punteria directa lista: %hs (activa en la partida)",
	},
};

// Meme pattern std::ifstream que LoadBootProtocolSetting() (voir main.cpp) :
// un seul fichier, un seul octet significatif, lu une fois au demarrage.
// Meme fichier et meme convention qu'application/i18n.cpp
// (LoadLanguagePref/SaveLanguagePref) - "1" = LANG_EN, "2" = LANG_PT_BR
// (ajoute 2026-08-24), sinon (fichier absent, "0", contenu non reconnu) =
// LANG_FR. "1" est cherche AVANT "2" pour preserver le comportement exact
// d'avant cet ajout si jamais les deux caracteres se trouvaient dans le
// fichier (ne devrait jamais arriver en pratique, ce fichier ne contient
// qu'un seul octet significatif).
void LoadPluginLanguageSetting(const char* langFilePath) {
	std::ifstream in(langFilePath, std::ios::binary);
	if (!in.is_open()) {
		FileLog("X360Remap_lang.txt missing - plugin notifications in EN (default)");
		g_pluginLang = LANG_EN;
		return;
	}
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	// Defaut EN depuis le 2026-09-13 (voir g_pluginLang). "0" reste traite
	// explicitement : c'est un choix DELIBERE de l'utilisateur pour le
	// francais, il doit continuer d'etre respecte. Seul le cas "contenu non
	// reconnu" bascule desormais vers l'anglais au lieu du francais.
	g_pluginLang = LANG_EN;
	bool found0 = false, found1 = false, found2 = false, found3 = false;
	for (size_t i = 0; i < content.size(); i++) {
		if (content[i] == '0') found0 = true;
		if (content[i] == '1') found1 = true;
		if (content[i] == '2') found2 = true;
		if (content[i] == '3') found3 = true;   // espagnol, ajoute le 2026-09-13
	}
	if (found1) g_pluginLang = LANG_EN;
	else if (found2) g_pluginLang = LANG_PT_BR;
	else if (found3) g_pluginLang = LANG_ES;
	else if (found0) g_pluginLang = LANG_FR;

	FileLog("X360Remap_lang.txt read - plugin notifications in %hs",
		g_pluginLang == LANG_EN ? "EN" :
		(g_pluginLang == LANG_PT_BR ? "PT" :
		(g_pluginLang == LANG_ES ? "ES" : "FR")));
}

const wchar_t* WTr(NotifyStrId id) {
	if (id < 0 || id >= STR_NOTIFY_COUNT)
		return L"";
	const wchar_t* s = g_notifyStrings[g_pluginLang][id];
	if (!s)
		s = g_notifyStrings[LANG_FR][id];
	return s;
}
