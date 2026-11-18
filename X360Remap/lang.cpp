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
		/* STR_LEARN_START               */ L"Recherche camera : lachez tout, regardez droit devant (~10 s)",
		/* STR_LEARN_PROGRESS_FMT        */ L"Recherche camera : %d%%",
		/* STR_LEARN_CONFIRM_FMT         */ L"Bougez la souris GAUCHE-DROITE. La camera suit : F12. Non : Retour arriere (%d/%d)",
		/* STR_LEARN_SAVED               */ L"Visee directe apprise et enregistree pour ce jeu",
		/* STR_LEARN_SAVED_H             */ L"Visee horizontale enregistree. Le vertical reste sur le stick",
		/* STR_LEARN_NOT_FOUND           */ L"Camera introuvable : ce jeu n'est pas encore compatible",
		/* STR_LEARN_NO_INPUT            */ L"Recherche camera : le jeu ne lit pas clavier/souris (joueur 1 ?)",
		/* STR_LEARN_CANCELED            */ L"Recherche camera annulee",
		/* STR_LEARN_FAILED              */ L"Echec de la recherche camera - voir X360Remap_plugin.log",
		/* STR_LEARN_NEED_GAME           */ L"Recherche camera : lancez d'abord un jeu",
		/* STR_LEARN_SAVING              */ L"Camera trouvee - enregistrement (~30 s), vous pouvez jouer",
		/* STR_LEARN_SESSION_ONLY        */ L"Visee directe active jusqu'a la fin de cette partie (pas d'adresse fixe)",
		/* STR_LEARN_CONFIRM_V_FMT       */ L"Maintenant HAUT-BAS. La camera suit : F12. Non : Retour arriere (%d/%d)",
		/* STR_LEARN_STATS_H_FMT         */ L"Horizontal : %d valeurs > %d > %d",
		/* STR_LEARN_STATS_V_FMT         */ L"Vertical : %d valeurs > %d > %d",
		/* STR_LEARN_VERTICAL_START      */ L"Horizontal OK. Recherche verticale : lachez tout",
		/* STR_LEARN_RECOMPUTED          */ L"Camera trouvee mais recalculee par le jeu a chaque image : pas encore utilisable",
		/* STR_REMAP_PAUSED              */ L"X360Remap en pause : la manette est le joueur 1 (Win+Maj+X pour reprendre)",
		/* STR_REMAP_ACTIVE              */ L"X360Remap actif : clavier et souris sont le joueur 1",
		/* STR_PLAYERS_MERGED            */ L"Joueurs : fusion - manette + clavier/souris = un seul joueur (Arret defil.)",
		/* STR_PLAYERS_SEPARATE          */ L"Joueurs : separes - clavier/souris = joueur 1, manette = joueur 2 (Arret defil.)",
		/* STR_LEARN_BOX_TITLE            */ L"Recherche camera",
		/* STR_LEARN_BOX_START            */ L"Placez-vous en jeu, personnage libre de bouger, hors de tout menu.\n\nApres Commencer, ne touchez a rien pendant environ 10 secondes : le plugin tourne la camera tout seul.",
		/* STR_LEARN_BOX_TEST_H_FMT       */ L"Essai %d sur %d - horizontal.\n\nApres OK, bougez la souris de GAUCHE a DROITE pendant 5 secondes.",
		/* STR_LEARN_BOX_TEST_V_FMT       */ L"Essai %d sur %d - vertical.\n\nApres OK, bougez la souris de HAUT en BAS pendant 5 secondes.",
		/* STR_LEARN_BOX_ASK_H            */ L"La camera a-t-elle suivi la souris de gauche a droite ?",
		/* STR_LEARN_BOX_ASK_V            */ L"La camera a-t-elle suivi la souris de haut en bas ?",
		/* STR_LEARN_BOX_VERTICAL         */ L"Horizontal valide.\n\nApres OK, ne touchez a rien : le plugin cherche le vertical.",
		/* STR_LEARN_BTN_START            */ L"Commencer",
		/* STR_LEARN_BTN_OK               */ L"OK",
		/* STR_LEARN_BTN_YES              */ L"Oui",
		/* STR_LEARN_BTN_NO               */ L"Non",
		/* STR_LEARN_BTN_STOP             */ L"Arreter",
		/* STR_AIM_OFF                    */ L"Visee directe coupee pour ce jeu (Win+Maj+D pour la remettre)",
		/* STR_AIM_ON                     */ L"Visee directe active",
		/* STR_CONFIG_RELOADED            */ L"Reglages recharges (Win+Maj+R)",
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
		/* STR_LEARN_START               */ L"Camera search: hands off, look straight ahead (~10 s)",
		/* STR_LEARN_PROGRESS_FMT        */ L"Camera search: %d%%",
		/* STR_LEARN_CONFIRM_FMT         */ L"Move the mouse LEFT-RIGHT. Camera follows: F12. No: Backspace (%d/%d)",
		/* STR_LEARN_SAVED               */ L"Direct aim learned and saved for this game",
		/* STR_LEARN_SAVED_H             */ L"Horizontal aim saved. Vertical stays on the stick",
		/* STR_LEARN_NOT_FOUND           */ L"Camera not found: this game is not supported yet",
		/* STR_LEARN_NO_INPUT            */ L"Camera search: the game does not read keyboard/mouse (player 1?)",
		/* STR_LEARN_CANCELED            */ L"Camera search canceled",
		/* STR_LEARN_FAILED              */ L"Camera search failed - see X360Remap_plugin.log",
		/* STR_LEARN_NEED_GAME           */ L"Camera search: start a game first",
		/* STR_LEARN_SAVING              */ L"Camera found - saving (~30 s), you can play",
		/* STR_LEARN_SESSION_ONLY        */ L"Direct aim active until you quit this game (no permanent address)",
		/* STR_LEARN_CONFIRM_V_FMT       */ L"Now UP-DOWN. Camera follows: F12. No: Backspace (%d/%d)",
		/* STR_LEARN_STATS_H_FMT         */ L"Horizontal: %d values > %d > %d",
		/* STR_LEARN_STATS_V_FMT         */ L"Vertical: %d values > %d > %d",
		/* STR_LEARN_VERTICAL_START      */ L"Horizontal OK. Vertical search: hands off",
		/* STR_LEARN_RECOMPUTED          */ L"Camera found but recalculated by the game each frame: not usable yet",
		/* STR_REMAP_PAUSED              */ L"X360Remap paused: the controller is player 1 (Win+Shift+X to resume)",
		/* STR_REMAP_ACTIVE              */ L"X360Remap active: keyboard and mouse are player 1",
		/* STR_PLAYERS_MERGED            */ L"Players: merged - controller + keyboard/mouse = one player (Scroll Lock)",
		/* STR_PLAYERS_SEPARATE          */ L"Players: separate - keyboard/mouse = player 1, controller = player 2 (Scroll Lock)",
		/* STR_LEARN_BOX_TITLE            */ L"Camera search",
		/* STR_LEARN_BOX_START            */ L"Get into gameplay, with your character free to move and no menu open.\n\nAfter Start, do not touch anything for about 10 seconds: the plugin turns the camera by itself.",
		/* STR_LEARN_BOX_TEST_H_FMT       */ L"Try %d of %d - horizontal.\n\nAfter OK, move the mouse LEFT and RIGHT for 5 seconds.",
		/* STR_LEARN_BOX_TEST_V_FMT       */ L"Try %d of %d - vertical.\n\nAfter OK, move the mouse UP and DOWN for 5 seconds.",
		/* STR_LEARN_BOX_ASK_H            */ L"Did the camera follow the mouse left and right?",
		/* STR_LEARN_BOX_ASK_V            */ L"Did the camera follow the mouse up and down?",
		/* STR_LEARN_BOX_VERTICAL         */ L"Horizontal confirmed.\n\nAfter OK, do not touch anything: the plugin looks for the vertical.",
		/* STR_LEARN_BTN_START            */ L"Start",
		/* STR_LEARN_BTN_OK               */ L"OK",
		/* STR_LEARN_BTN_YES              */ L"Yes",
		/* STR_LEARN_BTN_NO               */ L"No",
		/* STR_LEARN_BTN_STOP             */ L"Stop",
		/* STR_AIM_OFF                    */ L"Direct aim off for this game (Win+Shift+D to turn it back on)",
		/* STR_AIM_ON                     */ L"Direct aim on",
		/* STR_CONFIG_RELOADED            */ L"Settings reloaded (Win+Shift+R)",
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
		/* STR_LEARN_START               */ L"Busca da camera: solte tudo, olhe para frente (~10 s)",
		/* STR_LEARN_PROGRESS_FMT        */ L"Busca da camera: %d%%",
		/* STR_LEARN_CONFIRM_FMT         */ L"Mova o mouse ESQUERDA-DIREITA. Camera segue: F12. Nao: Backspace (%d/%d)",
		/* STR_LEARN_SAVED               */ L"Mira direta aprendida e salva para este jogo",
		/* STR_LEARN_SAVED_H             */ L"Mira horizontal salva. O vertical fica no analogico",
		/* STR_LEARN_NOT_FOUND           */ L"Camera nao encontrada: este jogo ainda nao e compativel",
		/* STR_LEARN_NO_INPUT            */ L"Busca da camera: o jogo nao le teclado/mouse (jogador 1?)",
		/* STR_LEARN_CANCELED            */ L"Busca da camera cancelada",
		/* STR_LEARN_FAILED              */ L"Falha na busca da camera - veja X360Remap_plugin.log",
		/* STR_LEARN_NEED_GAME           */ L"Busca da camera: inicie um jogo primeiro",
		/* STR_LEARN_SAVING              */ L"Camera encontrada - salvando (~30 s), pode jogar",
		/* STR_LEARN_SESSION_ONLY        */ L"Mira direta ativa ate sair deste jogo (sem endereco fixo)",
		/* STR_LEARN_CONFIRM_V_FMT       */ L"Agora CIMA-BAIXO. Camera segue: F12. Nao: Backspace (%d/%d)",
		/* STR_LEARN_STATS_H_FMT         */ L"Horizontal: %d valores > %d > %d",
		/* STR_LEARN_STATS_V_FMT         */ L"Vertical: %d valores > %d > %d",
		/* STR_LEARN_VERTICAL_START      */ L"Horizontal OK. Busca vertical: solte tudo",
		/* STR_LEARN_RECOMPUTED          */ L"Camera encontrada mas recalculada pelo jogo a cada quadro: ainda nao utilizavel",
		/* STR_REMAP_PAUSED              */ L"X360Remap pausado: o controle e o jogador 1 (Win+Shift+X para voltar)",
		/* STR_REMAP_ACTIVE              */ L"X360Remap ativo: teclado e mouse sao o jogador 1",
		/* STR_PLAYERS_MERGED            */ L"Jogadores: unidos - controle + teclado/mouse = um jogador (Scroll Lock)",
		/* STR_PLAYERS_SEPARATE          */ L"Jogadores: separados - teclado/mouse = jogador 1, controle = jogador 2 (Scroll Lock)",
		/* STR_LEARN_BOX_TITLE            */ L"Busca da camera",
		/* STR_LEARN_BOX_START            */ L"Entre no jogo, com o personagem livre para se mover e sem menu aberto.\n\nDepois de Iniciar, nao toque em nada por cerca de 10 segundos: o plugin gira a camera sozinho.",
		/* STR_LEARN_BOX_TEST_H_FMT       */ L"Teste %d de %d - horizontal.\n\nDepois de OK, mova o mouse para ESQUERDA e DIREITA por 5 segundos.",
		/* STR_LEARN_BOX_TEST_V_FMT       */ L"Teste %d de %d - vertical.\n\nDepois de OK, mova o mouse para CIMA e BAIXO por 5 segundos.",
		/* STR_LEARN_BOX_ASK_H            */ L"A camera seguiu o mouse para esquerda e direita?",
		/* STR_LEARN_BOX_ASK_V            */ L"A camera seguiu o mouse para cima e baixo?",
		/* STR_LEARN_BOX_VERTICAL         */ L"Horizontal confirmado.\n\nDepois de OK, nao toque em nada: o plugin procura o vertical.",
		/* STR_LEARN_BTN_START            */ L"Iniciar",
		/* STR_LEARN_BTN_OK               */ L"OK",
		/* STR_LEARN_BTN_YES              */ L"Sim",
		/* STR_LEARN_BTN_NO               */ L"Nao",
		/* STR_LEARN_BTN_STOP             */ L"Parar",
		/* STR_AIM_OFF                    */ L"Mira direta desligada neste jogo (Win+Shift+D para religar)",
		/* STR_AIM_ON                     */ L"Mira direta ligada",
		/* STR_CONFIG_RELOADED            */ L"Configuracoes recarregadas (Win+Shift+R)",
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
		/* STR_LEARN_START               */ L"Busqueda de camara: suelta todo, mira al frente (~10 s)",
		/* STR_LEARN_PROGRESS_FMT        */ L"Busqueda de camara: %d%%",
		/* STR_LEARN_CONFIRM_FMT         */ L"Mueve el raton IZQUIERDA-DERECHA. La camara sigue: F12. No: Retroceso (%d/%d)",
		/* STR_LEARN_SAVED               */ L"Punteria directa aprendida y guardada para este juego",
		/* STR_LEARN_SAVED_H             */ L"Punteria horizontal guardada. El vertical sigue en el stick",
		/* STR_LEARN_NOT_FOUND           */ L"Camara no encontrada: este juego aun no es compatible",
		/* STR_LEARN_NO_INPUT            */ L"Busqueda de camara: el juego no lee teclado/raton (jugador 1?)",
		/* STR_LEARN_CANCELED            */ L"Busqueda de camara cancelada",
		/* STR_LEARN_FAILED              */ L"Fallo la busqueda de camara - ver X360Remap_plugin.log",
		/* STR_LEARN_NEED_GAME           */ L"Busqueda de camara: inicia un juego primero",
		/* STR_LEARN_SAVING              */ L"Camara encontrada - guardando (~30 s), puedes jugar",
		/* STR_LEARN_SESSION_ONLY        */ L"Punteria directa activa hasta salir de este juego (sin direccion fija)",
		/* STR_LEARN_CONFIRM_V_FMT       */ L"Ahora ARRIBA-ABAJO. La camara sigue: F12. No: Retroceso (%d/%d)",
		/* STR_LEARN_STATS_H_FMT         */ L"Horizontal: %d valores > %d > %d",
		/* STR_LEARN_STATS_V_FMT         */ L"Vertical: %d valores > %d > %d",
		/* STR_LEARN_VERTICAL_START      */ L"Horizontal OK. Busqueda vertical: suelta todo",
		/* STR_LEARN_RECOMPUTED          */ L"Camara encontrada pero recalculada por el juego en cada imagen: aun no utilizable",
		/* STR_REMAP_PAUSED              */ L"X360Remap en pausa: el mando es el jugador 1 (Win+Mayus+X para volver)",
		/* STR_REMAP_ACTIVE              */ L"X360Remap activo: teclado y raton son el jugador 1",
		/* STR_PLAYERS_MERGED            */ L"Jugadores: unidos - mando + teclado/raton = un jugador (Bloq Despl)",
		/* STR_PLAYERS_SEPARATE          */ L"Jugadores: separados - teclado/raton = jugador 1, mando = jugador 2 (Bloq Despl)",
		/* STR_LEARN_BOX_TITLE            */ L"Busqueda de camara",
		/* STR_LEARN_BOX_START            */ L"Entra en el juego, con el personaje libre para moverse y sin ningun menu abierto.\n\nDespues de Empezar, no toques nada durante unos 10 segundos: el plugin gira la camara solo.",
		/* STR_LEARN_BOX_TEST_H_FMT       */ L"Prueba %d de %d - horizontal.\n\nDespues de OK, mueve el raton a IZQUIERDA y DERECHA durante 5 segundos.",
		/* STR_LEARN_BOX_TEST_V_FMT       */ L"Prueba %d de %d - vertical.\n\nDespues de OK, mueve el raton ARRIBA y ABAJO durante 5 segundos.",
		/* STR_LEARN_BOX_ASK_H            */ L"La camara siguio el raton a izquierda y derecha?",
		/* STR_LEARN_BOX_ASK_V            */ L"La camara siguio el raton arriba y abajo?",
		/* STR_LEARN_BOX_VERTICAL         */ L"Horizontal confirmado.\n\nDespues de OK, no toques nada: el plugin busca el vertical.",
		/* STR_LEARN_BTN_START            */ L"Empezar",
		/* STR_LEARN_BTN_OK               */ L"OK",
		/* STR_LEARN_BTN_YES              */ L"Si",
		/* STR_LEARN_BTN_NO               */ L"No",
		/* STR_LEARN_BTN_STOP             */ L"Parar",
		/* STR_AIM_OFF                    */ L"Apuntado directo desactivado en este juego (Win+Mayus+D para activarlo)",
		/* STR_AIM_ON                     */ L"Apuntado directo activo",
		/* STR_CONFIG_RELOADED            */ L"Ajustes recargados (Win+Mayus+R)",
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
