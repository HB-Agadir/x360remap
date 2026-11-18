// "How it works" on Windows (2026-11-18, HB): one tutorial for the console Studio,
// one for this app, one step per page. Texts only exist here (win_i18n L4); the
// console keeps its one-page DrawUiGlossyHowTo.
#include "guide_win.h"

#include "../../application/glossy_assets.h"
#include "../../application/ui_glossy.h"
#include "win_i18n.h"

namespace {

int s_guide = 1;   // the Windows guide first: this is the Windows app
int s_step = 0;

GlossyGuideStep Step(const char* title, const char* text, int a0 = -1, int a1 = -1, int a2 = -1, int a3 = -1) {
    GlossyGuideStep s;
    s.title = title;
    s.text = text;
    s.art[0] = a0; s.art[1] = a1; s.art[2] = a2; s.art[3] = a3;
    return s;
}

}  // namespace

void GuideShowStep(int guide, int step) { s_guide = guide; s_step = step; }

void DrawGuidePageWin(float w, float h, int& tab) {
    static GlossyGuideStep console[8], windows[8];

    // --- the console Studio, on the TV -------------------------------------------
    console[0] = Step(
        L4("Installer (une seule fois)", "Install (once)", "Instalar (una sola vez)", "Instalar (uma vez)"),
        L4("Le plus simple : l'application Windows (onglet Windows, étape 3) fait tout depuis le PC.\n"
           "À la main : X360Remap.xex dans les plugins de DashLaunch (launch.ini), le dossier X360RemapStudio sur le disque, et la Studio ajoutée dans Aurora.\n"
           "Redémarre la console : le plugin se charge tout seul à chaque démarrage.",
           "The easiest way: the Windows app (Windows tab, step 3) does it all from the PC.\n"
           "By hand: X360Remap.xex in DashLaunch's plugins (launch.ini), the X360RemapStudio folder on the drive, and the Studio added to Aurora.\n"
           "Restart the console: the plugin loads by itself at every start.",
           "Lo más sencillo: la aplicación de Windows (pestaña Windows, paso 3) lo hace todo desde el PC.\n"
           "A mano: X360Remap.xex en los plugins de DashLaunch (launch.ini), la carpeta X360RemapStudio en el disco y la Studio añadida en Aurora.\n"
           "Reinicia la consola: el plugin se carga solo en cada arranque.",
           "O mais simples: o aplicativo do Windows (aba Windows, passo 3) faz tudo pelo PC.\n"
           "À mão: X360Remap.xex nos plugins do DashLaunch (launch.ini), a pasta X360RemapStudio no disco e a Studio adicionada no Aurora.\n"
           "Reinicie o console: o plugin carrega sozinho a cada inicialização."),
        GA_PG_GEAR);
    console[1] = Step(
        L4("Brancher clavier et souris", "Plug in keyboard and mouse", "Conectar teclado y ratón", "Conectar teclado e mouse"),
        L4("Branche-les sur les ports USB de la console. Le point vert en haut de la page Clavier & souris confirme qu'ils sont vus.\n"
           "Seuls les appareils branchés sont listés, et celui qui est branché est choisi.\n"
           "Un appareil branché avant l'allumage : débranche-le et rebranche-le une fois, quand Aurora est affiché.",
           "Plug them into the console's USB ports. The green dot at the top of the Keyboard & mouse page confirms they are seen.\n"
           "Only plugged devices are listed, and the plugged one is selected.\n"
           "A device plugged in before power-on: unplug it and plug it back once, when Aurora is on screen.",
           "Conéctalos a los puertos USB de la consola. El punto verde arriba en la página Teclado y ratón confirma que se ven.\n"
           "Solo se listan los dispositivos conectados, y el conectado queda elegido.\n"
           "Un dispositivo conectado antes de encender: desconéctalo y vuelve a conectarlo una vez, con Aurora en pantalla.",
           "Conecte-os nas portas USB do console. O ponto verde no alto da página Teclado e mouse confirma que foram vistos.\n"
           "Só os dispositivos conectados aparecem, e o conectado fica escolhido.\n"
           "Um dispositivo conectado antes de ligar: desconecte e conecte de novo uma vez, com o Aurora na tela."),
        GA_ICON_KB, GA_ICON_MS);
    console[2] = Step(
        L4("Assigner tes touches", "Assign your keys", "Asignar tus teclas", "Atribuir suas teclas"),
        L4("Page Clavier & souris : lance l'Assistant (21 étapes), ou clique une seule case puis appuie sur la touche voulue.\n"
           "Chaque bouton de la manette peut recevoir une touche du clavier ou un bouton de la souris.\n"
           "« Réglages par défaut » en bas de la page remet tout à l'origine.",
           "Keyboard & mouse page: run the Wizard (21 steps), or click a single cell and press the key you want.\n"
           "Every controller button can take a keyboard key or a mouse button.\n"
           "\"Default settings\" at the bottom of the page puts everything back to the start.",
           "Página Teclado y ratón: lanza el Asistente (21 pasos), o pulsa una sola casilla y luego la tecla que quieras.\n"
           "Cada botón del mando puede recibir una tecla o un botón del ratón.\n"
           "\"Ajustes por defecto\" abajo en la página lo devuelve todo al principio.",
           "Página Teclado e mouse: rode o Assistente (21 passos), ou clique numa casa e aperte a tecla desejada.\n"
           "Cada botão do controle pode receber uma tecla ou um botão do mouse.\n"
           "\"Configurações padrão\" no fim da página volta tudo ao início."),
        GA_K_W, GA_K_A, GA_KP_A_S);
    console[3] = Step(
        L4("Régler la souris", "Tune the mouse", "Ajustar el ratón", "Ajustar o mouse"),
        L4("Mesure ta souris une fois avec une feuille A4 (bouton Mesurer la souris) : la sensibilité devient la même d'une souris à l'autre.\n"
           "Ensuite : sensibilité, zone morte, anti-zone morte, lissage, inverser Y. Clique un bouton de la souris pour changer ce qu'il fait.\n"
           "Si la souris se comporte mal : page Calibrer.",
           "Measure your mouse once with an A4 sheet (Measure the mouse button): sensitivity then stays the same from one mouse to another.\n"
           "Then: sensitivity, dead zone, anti-deadzone, smoothing, invert Y. Click a mouse button to change what it does.\n"
           "If the mouse acts strange: Calibrate page.",
           "Mide tu ratón una vez con una hoja A4 (botón Medir el ratón): la sensibilidad queda igual de un ratón a otro.\n"
           "Después: sensibilidad, zona muerta, anti zona muerta, suavizado, invertir Y. Pulsa un botón del ratón para cambiar lo que hace.\n"
           "Si el ratón se porta mal: página Calibrar.",
           "Meça seu mouse uma vez com uma folha A4 (botão Medir o mouse): a sensibilidade fica igual de um mouse para outro.\n"
           "Depois: sensibilidade, zona morta, antizona morta, suavização, inverter Y. Clique num botão do mouse para mudar o que ele faz.\n"
           "Se o mouse se comportar mal: página Calibrar."),
        GA_PG_MEASURE);
    console[4] = Step(
        L4("Manette USB et macros", "USB controller and macros", "Mando USB y macros", "Controle USB e macros"),
        L4("Une manette USB branchée sur la console apparaît dans la barre des appareils. Clique-la pour ouvrir sa page.\n"
           "Là : réattribuer ses boutons, régler ses sticks, et créer des macros (une suite de boutons déclenchée par un seul).",
           "A USB controller plugged into the console shows up in the device bar. Click it to open its page.\n"
           "There: remap its buttons, tune its sticks, and make macros (a sequence of buttons started by a single one).",
           "Un mando USB conectado a la consola aparece en la barra de dispositivos. Púlsalo para abrir su página.\n"
           "Ahí: reasignar sus botones, ajustar sus sticks y crear macros (una serie de botones lanzada por uno solo).",
           "Um controle USB conectado ao console aparece na barra de dispositivos. Clique nele para abrir sua página.\n"
           "Lá: reatribuir os botões, ajustar os analógicos e criar macros (uma sequência de botões disparada por um só)."),
        GA_PAD_FRONT);
    console[5] = Step(
        L4("Profils de jeu", "Game profiles", "Perfiles de juego", "Perfis de jogo"),
        L4("Des réglages différents pour un jeu : page Profils de jeux, choisis sa jaquette et règle-le. Le profil s'active tout seul quand le jeu démarre.\n"
           "En jeu, maintiens F9 quelques secondes : les réglages du moment sont enregistrés dans le profil de ce jeu.",
           "Different settings for one game: Game profiles page, pick its cover and tune it. The profile switches on by itself when the game starts.\n"
           "In game, hold F9 for a few seconds: the current settings are saved into this game's profile.",
           "Ajustes distintos para un juego: página Perfiles de juegos, elige su carátula y ajústalo. El perfil se activa solo cuando arranca el juego.\n"
           "En el juego, mantén F9 unos segundos: los ajustes del momento se guardan en el perfil de ese juego.",
           "Configurações diferentes para um jogo: página Perfis de jogos, escolha a capa e ajuste. O perfil liga sozinho quando o jogo começa.\n"
           "No jogo, segure F9 alguns segundos: as configurações do momento são salvas no perfil desse jogo."),
        GA_K_F9);
    console[6] = Step(
        L4("En jeu, sans revenir ici", "In game, without coming back here", "En el juego, sin volver aquí", "No jogo, sem voltar aqui"),
        L4("BACK + START (maintenir) : liaison rapide, puis le bouton de la manette et la touche qui le remplace.\n"
           "F5/F6 sensibilité, F10/F11 zone morte, F7/F8 et F3/F4 anti-zone morte, F1/F2 lissage.\n"
           "F12 (maintenir) : recherche de la caméra pour la visée directe.\n"
           "Arrêt défil : joueurs fusionnés / séparés. Win + Maj + X : pause. Alt droite : bouton Guide.",
           "BACK + START (hold): quick bind, then the controller button and the key that replaces it.\n"
           "F5/F6 sensitivity, F10/F11 dead zone, F7/F8 and F3/F4 anti-deadzone, F1/F2 smoothing.\n"
           "F12 (hold): camera search for direct aim.\n"
           "Scroll Lock: players merged / separate. Win + Shift + X: pause. Right Alt: Guide button.",
           "BACK + START (mantener): asignación rápida, luego el botón del mando y la tecla que lo sustituye.\n"
           "F5/F6 sensibilidad, F10/F11 zona muerta, F7/F8 y F3/F4 anti zona muerta, F1/F2 suavizado.\n"
           "F12 (mantener): búsqueda de la cámara para el apuntado directo.\n"
           "Bloq Despl: jugadores fusionados / separados. Win + Mayús + X: pausa. Alt derecha: botón Guide.",
           "BACK + START (segurar): atribuição rápida, depois o botão do controle e a tecla que o substitui.\n"
           "F5/F6 sensibilidade, F10/F11 zona morta, F7/F8 e F3/F4 antizona morta, F1/F2 suavização.\n"
           "F12 (segurar): busca da câmera para a mira direta.\n"
           "Scroll Lock: jogadores mesclados / separados. Win + Shift + X: pausa. Alt direito: botão Guide."),
        GA_KP_BACK_S, GA_KP_START_S);
    console[7] = Step(
        L4("Affichage en jeu et dépannage", "In-game display and help", "Pantalla en el juego y ayuda", "Exibição no jogo e ajuda"),
        L4("Page Affichage en jeu : dessin du clavier ou de la manette par-dessus le jeu, et infos console (FPS, températures).\n"
           "En jeu : la touche Windows affiche ou cache le dessin, Windows + I les infos console.\n"
           "Échap deux fois dans la Studio : retour à Aurora.\n"
           "Un souci : les journaux sont dans X360RemapStudio (X360Remap_plugin.log, X360Remap_studio.log).",
           "In-game display page: drawing of the keyboard or controller on top of the game, and console info (FPS, temperatures).\n"
           "In game: the Windows key shows or hides the drawing, Windows + I the console info.\n"
           "Esc twice in the Studio: back to Aurora.\n"
           "A problem: the logs are in X360RemapStudio (X360Remap_plugin.log, X360Remap_studio.log).",
           "Página Pantalla en juego: dibujo del teclado o del mando encima del juego, e información de la consola (FPS, temperaturas).\n"
           "En el juego: la tecla Windows muestra u oculta el dibujo, Windows + I la información de la consola.\n"
           "Esc dos veces en la Studio: volver a Aurora.\n"
           "Un problema: los registros están en X360RemapStudio (X360Remap_plugin.log, X360Remap_studio.log).",
           "Página Exibição no jogo: desenho do teclado ou do controle sobre o jogo, e informações do console (FPS, temperaturas).\n"
           "No jogo: a tecla Windows mostra ou oculta o desenho, Windows + I as informações do console.\n"
           "Esc duas vezes na Studio: volta ao Aurora.\n"
           "Um problema: os registros ficam em X360RemapStudio (X360Remap_plugin.log, X360Remap_studio.log)."),
        GA_K_WIN, GA_K_I);

    // --- this app, on the PC ---------------------------------------------------------
    windows[0] = Step(
        L4("Ce que fait l'application", "What this app does", "Qué hace la aplicación", "O que o aplicativo faz"),
        L4("Tout se règle ici, sur le PC, avec une vraie souris et un vrai clavier, puis part sur la console par le serveur FTP d'Aurora. Le plugin recharge les réglages tout seul.\n"
           "La console doit être allumée, dans Aurora, sur le même réseau que le PC.\n"
           "Un seul fichier .exe, rien à installer. Avec portable.txt à côté, tout reste dans le dossier data.",
           "Everything is set here, on the PC, with a real mouse and keyboard, then goes to the console through Aurora's FTP server. The plugin reloads the settings by itself.\n"
           "The console must be on, in Aurora, on the same network as the PC.\n"
           "A single .exe, nothing to install. With portable.txt next to it, everything stays in the data folder.",
           "Todo se ajusta aquí, en el PC, con un ratón y un teclado de verdad, y luego va a la consola por el servidor FTP de Aurora. El plugin recarga los ajustes solo.\n"
           "La consola debe estar encendida, en Aurora, en la misma red que el PC.\n"
           "Un solo .exe, nada que instalar. Con portable.txt al lado, todo se queda en la carpeta data.",
           "Tudo se ajusta aqui, no PC, com mouse e teclado de verdade, e depois vai para o console pelo servidor FTP do Aurora. O plugin recarrega as configurações sozinho.\n"
           "O console precisa estar ligado, no Aurora, na mesma rede do PC.\n"
           "Um único .exe, nada para instalar. Com portable.txt ao lado, tudo fica na pasta data."),
        GA_SPLASH_BADGE);
    windows[1] = Step(
        L4("Se connecter à la console", "Connect to the console", "Conectar con la consola", "Conectar ao console"),
        L4("L'application cherche la console toute seule sur le réseau, même si son adresse change.\n"
           "Sinon : clique le texte d'état en bas à gauche, entre l'adresse IP de la console, Connecter. « Trouver ma console » liste les consoles du réseau.\n"
           "Réglages d'Aurora par défaut : port 21, utilisateur et mot de passe xboxftp.",
           "The app finds the console on the network by itself, even when its address changes.\n"
           "Otherwise: click the status text at the bottom left, type the console's IP address, Connect. \"Find my console\" lists the consoles of the network.\n"
           "Aurora's defaults: port 21, user and password xboxftp.",
           "La aplicación busca la consola sola en la red, aunque cambie su dirección.\n"
           "Si no: pulsa el texto de estado abajo a la izquierda, escribe la IP de la consola, Conectar. \"Buscar mi consola\" lista las consolas de la red.\n"
           "Valores de Aurora por defecto: puerto 21, usuario y contraseña xboxftp.",
           "O aplicativo procura o console sozinho na rede, mesmo se o endereço mudar.\n"
           "Senão: clique no texto de estado embaixo à esquerda, digite o IP do console, Conectar. \"Encontrar meu console\" lista os consoles da rede.\n"
           "Padrões do Aurora: porta 21, usuário e senha xboxftp."),
        GA_BRAND_BADGE);
    windows[2] = Step(
        L4("Installer et réparer depuis le PC", "Install and repair from the PC", "Instalar y reparar desde el PC", "Instalar e reparar pelo PC"),
        L4("Fichier > Installation et réparation : l'application vérifie le launch.ini de DashLaunch et installe le plugin et la Studio (dernière version de GitHub, ou un zip que tu choisis).\n"
           "Les réglages de la console sont d'abord copiés sur le PC. La modification du launch.ini est montrée avant d'être faite (copie launch.ini.bak).\n"
           "Un petit script Aurora ajoute la tuile de la Studio. Puis redémarrage et vérification du plugin.",
           "File > Install & repair: the app checks DashLaunch's launch.ini and installs the plugin and the Studio (latest release from GitHub, or a zip you pick).\n"
           "The console settings are copied to the PC first. The launch.ini change is shown before it is made (launch.ini.bak kept).\n"
           "A small Aurora script adds the Studio's tile. Then a restart and a check of the plugin.",
           "Archivo > Instalación y reparación: la aplicación revisa el launch.ini de DashLaunch e instala el plugin y la Studio (última versión de GitHub, o un zip que elijas).\n"
           "Los ajustes de la consola se copian antes en el PC. El cambio del launch.ini se muestra antes de hacerse (copia launch.ini.bak).\n"
           "Un pequeño script de Aurora añade el icono de la Studio. Luego reinicio y comprobación del plugin.",
           "Arquivo > Instalação e reparo: o aplicativo verifica o launch.ini do DashLaunch e instala o plugin e a Studio (última versão do GitHub, ou um zip que você escolher).\n"
           "As configurações do console são copiadas antes para o PC. A mudança do launch.ini é mostrada antes de ser feita (cópia launch.ini.bak).\n"
           "Um pequeno script do Aurora adiciona o bloco da Studio. Depois reinício e verificação do plugin."),
        GA_PG_GEAR);
    windows[3] = Step(
        L4("Modifier, puis Envoyer", "Edit, then Send", "Cambiar y luego Enviar", "Alterar e depois Enviar"),
        L4("Chaque changement est gardé sur le PC tout de suite ; « Changements non envoyés » apparaît en bas.\n"
           "Envoyer met les réglages sur la console. Recevoir charge ceux de la console.\n"
           "L'envoi est sûr : fichier vérifié avant, relu après, ancienne version gardée (.bak). Si la console a changé entre-temps (F9, Studio console), l'application te demande lequel garder.",
           "Every change is kept on the PC at once; \"Changes not sent\" appears at the bottom.\n"
           "Send puts the settings on the console. Receive loads the console's.\n"
           "Sending is safe: the file is checked first, read back after, and the old version kept (.bak). If the console changed in the meantime (F9, console Studio), the app asks which one to keep.",
           "Cada cambio se guarda enseguida en el PC; abajo aparece \"Cambios sin enviar\".\n"
           "Enviar pone los ajustes en la consola. Recibir carga los de la consola.\n"
           "El envío es seguro: el archivo se revisa antes, se relee después y se guarda la versión anterior (.bak). Si la consola cambió mientras tanto (F9, Studio de la consola), la aplicación pregunta cuál conservar.",
           "Cada mudança fica salva no PC na hora; embaixo aparece \"Alterações não enviadas\".\n"
           "Enviar coloca as configurações no console. Receber carrega as do console.\n"
           "O envio é seguro: o arquivo é verificado antes, relido depois, e a versão anterior fica guardada (.bak). Se o console mudou nesse meio-tempo (F9, Studio do console), o aplicativo pergunta qual manter."),
        GA_K_ENTER);
    windows[4] = Step(
        L4("Touches et souris sur le PC", "Keys and mouse on the PC", "Teclas y ratón en el PC", "Teclas e mouse no PC"),
        L4("Clique un bouton de la manette dans la grille, relâche toutes les touches, puis appuie sur la touche du clavier du PC.\n"
           "C'est la touche physique qui compte : la même touche du clavier de la console reçoit la liaison, quelle que soit la disposition (AZERTY, QWERTY).\n"
           "Souris avancé... (en bas) : sensibilité en visée, lissage, courbe. Mesurer et calibrer la souris se font sur la console.",
           "Click a controller button in the grid, release every key, then press the key on the PC keyboard.\n"
           "The physical key is what counts: the same key on the console's keyboard gets the binding, whatever the layout (AZERTY, QWERTY).\n"
           "Mouse advanced... (bottom): aiming sensitivity, smoothing, curve. Measuring and calibrating the mouse are done on the console.",
           "Pulsa un botón del mando en la cuadrícula, suelta todas las teclas y luego pulsa la tecla en el teclado del PC.\n"
           "Cuenta la tecla física: la misma tecla del teclado de la consola recibe la asignación, sea cual sea la distribución (AZERTY, QWERTY).\n"
           "Ratón avanzado... (abajo): sensibilidad al apuntar, suavizado, curva. Medir y calibrar el ratón se hacen en la consola.",
           "Clique num botão do controle na grade, solte todas as teclas e aperte a tecla no teclado do PC.\n"
           "O que conta é a tecla física: a mesma tecla do teclado do console recebe a atribuição, seja qual for o layout (AZERTY, QWERTY).\n"
           "Mouse avançado... (embaixo): sensibilidade na mira, suavização, curva. Medir e calibrar o mouse se faz no console."),
        GA_K_Q, GA_K_W, GA_K_E);
    windows[5] = Step(
        L4("Appareils de ce PC", "Devices of this PC", "Dispositivos de este PC", "Dispositivos deste PC"),
        L4("Un clavier ou une souris que la console n'affiche pas encore peut être branché sur le PC.\n"
           "Si c'est le même modèle que sur la console, dis-le une fois : il se règle ici et ses réglages partent sous son identifiant (VID:PID).\n"
           "« Non » : rien sur cet appareil n'est envoyé à la console. (Fichier > Périphériques du PC)",
           "A keyboard or mouse the console does not show yet can be plugged into the PC.\n"
           "If it is the same model as on the console, say so once: it is set up here and its settings go out under its id (VID:PID).\n"
           "\"No\": nothing about this device is sent to the console. (File > Devices of this PC)",
           "Un teclado o ratón que la consola aún no muestra puede conectarse al PC.\n"
           "Si es el mismo modelo que en la consola, indícalo una vez: se ajusta aquí y sus ajustes se envían con su identificador (VID:PID).\n"
           "\"No\": nada de este dispositivo se envía a la consola. (Archivo > Dispositivos del PC)",
           "Um teclado ou mouse que o console ainda não mostra pode ser conectado ao PC.\n"
           "Se for o mesmo modelo do console, diga uma vez: ele é ajustado aqui e as configurações vão com o seu identificador (VID:PID).\n"
           "\"Não\": nada deste dispositivo é enviado ao console. (Arquivo > Dispositivos do PC)"),
        GA_ICON_KB, GA_ICON_MS);
    windows[6] = Step(
        L4("Jeux, jaquettes et profils", "Games, covers and profiles", "Juegos, carátulas y perfiles", "Jogos, capas e perfis"),
        L4("La page Profils de jeux lit les jeux et les jaquettes d'Aurora sur la console.\n"
           "Choisis un jeu pour lui donner son profil.\n"
           "Visée directe du jeu... (en bas) : caméra du jeu, import d'une ligne de Xenia Camera Finder.\n"
           "Fichier > Exporter le profil du jeu / Importer un profil de jeu : un fichier .json pour le partager.",
           "The Game profiles page reads Aurora's games and covers on the console.\n"
           "Pick a game to give it its profile.\n"
           "Game direct aim... (bottom): the game's camera, import of a Xenia Camera Finder line.\n"
           "File > Export the game's profile / Import a game profile: a .json file to share it.",
           "La página Perfiles de juegos lee los juegos y carátulas de Aurora en la consola.\n"
           "Elige un juego para darle su perfil.\n"
           "Apuntado directo del juego... (abajo): cámara del juego, importar una línea de Xenia Camera Finder.\n"
           "Archivo > Exportar el perfil del juego / Importar un perfil de juego: un archivo .json para compartirlo.",
           "A página Perfis de jogos lê os jogos e as capas do Aurora no console.\n"
           "Escolha um jogo para dar a ele seu perfil.\n"
           "Mira direta do jogo... (embaixo): câmera do jogo, importar uma linha do Xenia Camera Finder.\n"
           "Arquivo > Exportar o perfil do jogo / Importar um perfil de jogo: um arquivo .json para compartilhar."),
        GA_COVER_FRAME_F);
    windows[7] = Step(
        L4("Journaux, options et raccourcis", "Logs, options and shortcuts", "Registros, opciones y atajos", "Registros, opções e atalhos"),
        L4("Journaux : les quatre journaux de la console, et un zip à envoyer en cas de souci.\n"
           "Avancé : les interrupteurs du plugin. Fichier : ouvrir ou sauver un X360Remap.json.\n"
           "Raccourcis : le tableau complet des touches en jeu. F11 : plein écran.\n"
           "Page Soutenir : recherche des nouvelles versions sur GitHub (désactivée par défaut).",
           "Logs: the four console logs, and a zip to send when something goes wrong.\n"
           "Advanced: the plugin's switches. File: open or save an X360Remap.json.\n"
           "Shortcuts: the full table of in-game keys. F11: full screen.\n"
           "Support page: looks for new releases on GitHub (off by default).",
           "Registros: los cuatro registros de la consola, y un zip para enviar si algo falla.\n"
           "Avanzado: los interruptores del plugin. Archivo: abrir o guardar un X360Remap.json.\n"
           "Atajos: la tabla completa de teclas en el juego. F11: pantalla completa.\n"
           "Página Apoyar: busca nuevas versiones en GitHub (desactivado por defecto).",
           "Registros: os quatro registros do console, e um zip para enviar se algo der errado.\n"
           "Avançado: os interruptores do plugin. Arquivo: abrir ou salvar um X360Remap.json.\n"
           "Atalhos: a tabela completa das teclas no jogo. F11: tela cheia.\n"
           "Página Apoiar: procura novas versões no GitHub (desligado por padrão)."),
        GA_K_F11);

    GlossyGuideModel gm;
    gm.guideName[0] = L4("Studio console", "Console Studio", "Studio de la consola", "Studio do console");
    gm.guideName[1] = L4("Application Windows", "Windows app", "Aplicación Windows", "Aplicativo Windows");
    gm.intro[0] = L4("Sur la télé, avec la console", "On the TV, with the console", "En la tele, con la consola", "Na TV, com o console");
    gm.intro[1] = L4("Sur le PC, envoyé à la console", "On the PC, sent to the console", "En el PC, enviado a la consola", "No PC, enviado ao console");
    gm.steps[0] = console;  gm.stepCount[0] = 8;
    gm.steps[1] = windows;  gm.stepCount[1] = 8;
    gm.prevLabel = L4("Précédent", "Previous", "Anterior", "Anterior");
    gm.nextLabel = L4("Suivant", "Next", "Siguiente", "Próximo");
    gm.guide = s_guide;
    gm.step = s_step;
    DrawUiGlossyGuide(gm, tab, w, h);
    s_guide = gm.guide;
    s_step = gm.step;
}
