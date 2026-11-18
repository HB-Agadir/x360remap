#include <xtl.h>
#include <xkelib.h>
#include <string>
#include <fstream>
#include <stddef.h> // offsetof - clean unplug close requests
#include <time.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <stdarg.h>
#include <iterator> // std::istreambuf_iterator - lecture de known_titles.txt (Jalon 7, suite)
#include <ctype.h>
#include <math.h>  // powf() - inertie souris->stick, voir XInputdReadStateHook
#include "Detours.h"
#include "hid_parser.h"
#include "usb.h"
#include "mapping.h"
#include "known_titles.h" // Jalon 7 (suite) - historique des jeux vus, raccourci "sauvegarder depuis le jeu"
#include "known_devices.h" // Jalon 8 (suite) - historique des peripheriques vus, pour l'ecran de selection d'application.xex
#include "mouse_calibration.h" // ReadSignedField : lecture des champs aux offsets appris par calibration
#include "lang.h" // FR/EN pour les notifications XNotifyUI - meme fichier de langue que application/i18n.h
// Plugin version: log header and X360Remap_status.json (CONTRACT.md).
#define X360REMAP_VERSION "1.4.1-pre"
#include "known_controllers.h" // controllers known in advance (SDL GameControllerDB)
#include "hud_layout.h"
#include "hud_font.h"
#include "hud_art.h"
#include "camera_learn.h" // v1.2.1: camera auto-detection for direct aim (pure logic, tested with g++)

Detour HidAddDeviceDetour;
Detour HidRemoveDeviceDetour;
Detour XamInputSetStateDetour;
Detour XamInputGetCapabilitiesDetour;
Detour XInputdReadStateDetour;
#define XNOTIFYUI_CUSTOM (XNOTIFYQUEUEUI_TYPE)80
uint16_t swap_endianness_16(uint16_t val) {
	return (val >> 8) | (val << 8);
}

// Console temperatures from the SMC (2026-10-30: freezes AND a shutdown
// after hours of tests - heat must be ruled in or out with data). Free60 SMC
// docs: command 0x07 = query temperatures; 4 values (CPU, GPU, memory, board)
// as 16-bit fixed point, 1/256 degree. The raw bytes are logged too, in case
// the layout differs. Polling thread only (never from a USB callback).
// Same layout that gave CPU 63 / GPU 57 on HB's console (2026-10-30): 8.8 fixed point,
// low byte first. Off with disable_temps.txt; called from the polling thread in a game only.
static void ReadSmcTemps(int* cpu, int* gpu) {
	BYTE in[0x10] = { 0 }, o[0x10] = { 0 };
	in[0] = 0x07;
	HalSendSMCMessage(in, o);
	const int c = ((int)o[1] | ((int)o[2] << 8)) / 256, g = ((int)o[3] | ((int)o[4] << 8)) / 256;
	if (c > 0 && c < 120 && g > 0 && g < 120) { *cpu = c; *gpu = g; }   // a bad answer keeps the last value
}

BOOL IsTrayOpen() {
	BYTE Input[0x10] = { 0 }, Output[0x10] = { 0 };
	Input[0] = 0xA;
	HalSendSMCMessage(Input, Output);
	return (Output[1] == 0x60);
}

// This console likes to kill non system threads on title switches
// Stack size (2026-11-12, FREEZE_STATIC_ANALYSIS.md): with 0 the kernel gives a system
// thread 16 KB (ExCreateThread reads 0x4000 from the system process). Read on HB's console
// after a good boot: the polling thread had used 15.2 KB of it, 11.6 KB in our own frames
// (CRT streams, rapidjson) and the rest in the file system below. One deeper path (a busy
// file cache while Aurora loads) runs off the end: the whole console stops, nothing is
// logged. 64 KB is what XAM gives its own worker threads.
#define PLUGIN_THREAD_STACK 0x10000
HANDLE MakeThread(LPTHREAD_START_ROUTINE Address, PVOID arg) {
	HANDLE Handle = 0;
	ExCreateThread(&Handle, PLUGIN_THREAD_STACK, 0, XapiThreadStartup, Address, arg, (EX_CREATE_FLAG_SUSPENDED | EX_CREATE_FLAG_SYSTEM | 0x18000424));
	XSetThreadProcessor(Handle, 4);
	SetThreadPriority(Handle, THREAD_PRIORITY_NORMAL);
	ResumeThread(Handle);
	return Handle;
}

// --- Garde-fou notifications (2026-08-22) ---
//
// SUSPECT ETABLI PAR LE CODE du gel signale par Cyper puis reproduit par HB :
// console qui gele au branchement d'une souris OU d'un clavier, uniquement
// quand jrpc2.xex n'est PAS charge dans DashLaunch.
//
// XNotifyUI() ci-dessous envoie un type de notification PERSONNALISE (80,
// XNOTIFYUI_CUSTOM) que xam.xex ne connait pas d'origine. Pour qu'il soit
// accepte, initFunctionPointers() applique des patchs memoire bruts sur
// xam.xex, dont celui-ci, CONDITIONNEL :
//
//     if (*(short*)(NotificationPatchPtr + 48) == 0x409A)
//         *(short*)(NotificationPatchPtr + 48) = 0x4800;
//
// dont le commentaire d'origine dit explicitement "Patches notification
// handling to work without JRPC2". Autrement dit : quand jrpc2.xex est
// charge, c'est LUI qui prepare le systeme de notification. Quand il ne l'est
// pas, tout repose sur ce patch conditionnel - et s'il ne s'applique pas
// (octet different de 0x409A : autre revision de xam, autre plugin passe
// avant, etat different), il est ignore SILENCIEUSEMENT. On appelle alors
// XNotifyQueueUI avec un type que xam ne sait pas traiter.
//
// Ce que les journaux reels confirment : le PREMIER appel a XNotifyUI de
// toute la session est la notification "souris detectee"/"clavier pret",
// envoyee depuis le thread de polling juste apres qu'un peripherique a ete
// accepte - exactement l'instant du gel constate. Et comme XNotifyUI etait
// appele AVANT le FileLog correspondant, la ligne "Mouse detected" n'apparait
// jamais dans le journal des sessions qui gelent, alors qu'elle est bien
// presente dans celles qui fonctionnent (jrpc2 charge). Le point d'arret du
// journal correspond donc exactement a cette ligne.
//
// Cela explique aussi le delai "Aurora demarre, 10 secondes, ca gele" avec un
// peripherique deja branche a l'allumage : le reset USB differe
// (DEFERRED_USB_RESET) se declenche 10 s apres stabilisation du titre,
// reenumere le peripherique, qui est alors accepte puis notifie.
//
// Correctif : on ne fait plus confiance au patch. initFunctionPointers()
// verifie qu'il est REELLEMENT en place et pose g_notifyUiSafe en
// consequence ; sinon toute notification est ignoree. Perdre une notification
// est sans consequence (le journal garde la meme information), geler la
// console ne l'est pas.
volatile bool g_notifyUiSafe = true;
void FileLog(const char* fmt, ...); // defini plus bas - trace des notifications ignorees

void XNotifyUI(XNOTIFYQUEUEUI_TYPE Type, PWCHAR String) {
	if (!g_notifyUiSafe) {
		// Aucune notification a l'ecran, mais le journal garde la trace :
		// c'est la seule perte fonctionnelle de ce correctif.
		FileLog("Notification skipped (notification system not patched - see g_notifyUiSafe)");
		return;
	}
	XNotifyQueueUI(Type, XUSER_INDEX_ANY, XNOTIFYUI_PRIORITY_DEFAULT, String, 0);
}

// --- Support de stockage : resolu une fois au demarrage (2026-09-13) ---
//
// POURQUOI : signale par un testeur sur Xbox 360 Slim SANS disque dur
// (stockage interne 4 Go + cle USB, lance via BadUpdate). Tout le projet
// ecrivait en dur dans "HDD:\X360RemapStudio\" - inexistant sur une telle
// console, donc plugin ET application inutilisables, pas seulement genants.
//
// POURQUOI PAS UN LIEN SYMBOLIQUE (comme cote application) : application.xex
// est un titre lance normalement, son lien "hdd:" est cree dans son namespace
// PRIVE (prefixe "\??\", voir XexUtils/src/Filesystem.cpp). Ce plugin, lui,
// tourne dans le process xam.xex donc en contexte SYSTEME : le meme appel
// creerait un lien "\System??\hdd:" visible par TOUTE la console, susceptible
// de perturber d'autres homebrews. Inacceptable comme effet de bord. On se
// contente donc d'essayer plusieurs prefixes de lecteur a l'ouverture.
//
// NOMS DES SUPPORTS : releves sur la console reelle de HB via Xbox 360
// Neighborhood (2026-09-13) - "Hdd1:", "INTUSB:", "OnBoardMU:", "System:".
// "OnBoardMU:" (On-Board Memory Unit) est le candidat le plus probable pour
// le stockage interne 4 Go des modeles Slim. Ces noms restent NON VERIFIES
// depuis un processus console (Neighborhood expose la vue de XBDM, qui n'est
// pas forcement identique) - d'ou la strategie "essayer puis valider par une
// vraie creation de dossier", et la trace ecrite dans le journal pour que le
// testeur puisse nous dire lequel a fonctionne.
//
// PROPRIETE DE SECURITE : "HDD:" est le PREMIER candidat essaye. Sur une
// console avec disque dur (cas de HB et de la majorite des utilisateurs), il
// reussit immediatement et g_studioDrive garde sa valeur par defaut - le
// comportement reste identique a l'octet pres. Ce code ne peut donc pas
// casser une installation qui fonctionne aujourd'hui.
static char g_studioDrive[16] = "HDD:";

// Construit un chemin complet vers un fichier du dossier de travail.
// Le tampon est fourni par l'appelant (variable locale) et non statique :
// ce plugin est multi-thread (callbacks USB + thread de polling), un tampon
// partage serait une course silencieuse.
static void StudioPath(char* out, int outSize, const char* fileName) {
	_snprintf(out, outSize - 1, "%s\\X360RemapStudio\\%s", g_studioDrive, fileName);
	out[outSize - 1] = '\0';
}

// --- Working folder resolution (2026-10-29) --------------------------------
// Tester Daniel (360 E Corona, internal hard drive): the plugin ran (keyboard
// and mouse worked in games) but wrote NOTHING - no log, no status file, so
// the Studio said "plugin not loaded" and the Studio settings were never read.
// HB's own log shows the same "no writable storage" at 4.6 s, fixed on his
// console only because "HDD:" starts working later. On Daniel's console it
// apparently never does from the plugin (system process).
//
// Fix: our OWN link to the hard drive, "X360R:" -> \Device\Harddisk0\
// Partition1, created in the system namespace under a name nobody else uses
// (so "Hdd:" and other homebrews are not touched - see the comment above), and
// retries from the polling thread for 60 s when the drive is not mounted yet.
static volatile bool g_studioDriveResolved = false;
// True when the working folder is our own "X360R:" link (last resort). The
// Studio files written many times a second (input_state.json, status file)
// are then NOT written: with them, every file going through "X360R:" froze the
// whole console within ~40 s of opening the Studio (HB, 2026-10-29/30). The
// log, settings and lists (rare writes) still go through it.
static volatile bool g_studioDriveIsOwnLink = false;
enum { STORAGE_PASS_BOOT = 0, STORAGE_PASS_RETRY = 1, STORAGE_PASS_FINAL = 2 };

static void MountOwnHddLink() {
	STRING link = {};
	STRING device = {};
	RtlInitAnsiString(&link, "\\System??\\X360R:");
	RtlInitAnsiString(&device, "\\Device\\Harddisk0\\Partition1\\");
	ObCreateSymbolicLink(&link, &device);   // fails harmlessly if it exists
}

// GitHub issue #8 (2026-11-17, BadUpdate, everything on a USB stick): the plugin ran in
// games but found no folder ("no writable storage"), so no log, no status file: the
// Studio said "plugin not loaded" / "keyboard not detected" and never reached the plugin.
// "Usb0:" does not exist in the system namespace; the Studio itself reaches the stick
// through \Device\Mass0 (XexUtils MountUsb, confirmed). Our own links to the USB mass
// storage devices, under names nobody else uses, like X360R: above.
static void MountOwnUsbLinks() {
	static const char* const kLinks[3][2] = {
		{ "\\System??\\X360U0:", "\\Device\\Mass0\\" },
		{ "\\System??\\X360U1:", "\\Device\\Mass1\\" },
		{ "\\System??\\X360U2:", "\\Device\\Mass2\\" },
	};
	for (int i = 0; i < 3; i++) {
		STRING link = {}, device = {};
		RtlInitAnsiString(&link, kLinks[i][0]);
		RtlInitAnsiString(&device, kLinks[i][1]);
		ObCreateSymbolicLink(&link, &device);   // fails harmlessly if it exists
	}
}

static bool TryStudioDrive(const char* drive, bool create) {
	char dir[64];
	_snprintf(dir, sizeof(dir) - 1, "%s\\X360RemapStudio", drive);
	dir[sizeof(dir) - 1] = '\0';
	bool ok;
	if (create)
		ok = CreateDirectoryA(dir, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
	else
		ok = GetFileAttributesA(dir) != INVALID_FILE_ATTRIBUTES;
	if (!ok)
		return false;
	char tmp[16];
	_snprintf(tmp, sizeof(tmp) - 1, "%s", drive);
	tmp[sizeof(tmp) - 1] = '\0';
	memcpy(g_studioDrive, tmp, sizeof(g_studioDrive));
	MemoryBarrier();
	g_studioDriveResolved = true;
	g_studioDriveIsOwnLink = (strcmp(tmp, "X360R:") == 0);
	return true;
}

// ORDER (same rule as the Studio, AUDIT_2026-09.md I3):
//  1. the hard drive: "HDD:", "Hdd1:", then our own "X360R:" link;
//  2. internal memory / USB that ALREADY hold X360Remap.json, then the folder;
//  3. only at the FINAL pass (60 s after boot): the first writable one.
static bool ResolveStudioDrive(int pass) {
	static const char* kHdd[] = { "HDD:", "Hdd1:" };
	static const char* kFallback[] = { "OnBoardMU:", "INTUSB:", "Usb0:", "X360U0:", "X360U1:", "X360U2:" };
	const int kFallbackCount = (int)(sizeof(kFallback) / sizeof(kFallback[0]));
	for (int i = 0; i < 2; i++)
		if (TryStudioDrive(kHdd[i], true)) return true;
	// No hard drive yet: the USB stick may hold the folder (issue #8). From the polling
	// thread only, never in DllMain, like X360R: (nothing new at load time).
	if (pass != STORAGE_PASS_BOOT) MountOwnUsbLinks();
	// Our own link. Test switch (2026-10-29, freeze with the Studio open on the
	// first pre14-dev build): disable_own_link.txt in that folder = do not use
	// "X360R:", keep waiting for "HDD:" / "Hdd1:" like pre13 did.
	// 2026-10-30: total console freeze (FTP dead too) 3 times with the Studio
	// open while every file went through "X360R:". Until that is understood,
	// "X360R:" is a LAST RESORT: only at the final pass, 60 s after boot, when
	// "HDD:" / "Hdd1:" never worked (Daniel's case). HB's console uses "HDD:"
	// again, as in pre13.
	if (pass == STORAGE_PASS_FINAL) {
		MountOwnHddLink();   // only now: pre13 behaviour until then
		const DWORD own = GetFileAttributesA("X360R:\\X360RemapStudio");
		if (own != INVALID_FILE_ATTRIBUTES) {
			if (GetFileAttributesA("X360R:\\X360RemapStudio\\disable_own_link.txt") != INVALID_FILE_ATTRIBUTES) {
				if (pass != STORAGE_PASS_FINAL)
					return false;          // pre13 behaviour: retry HDD: / Hdd1: later
			} else if (TryStudioDrive("X360R:", true)) {
				return true;
			}
		}
	}
	for (int i = 0; i < kFallbackCount; i++) {
		char probe[96];
		_snprintf(probe, sizeof(probe) - 1, "%s\\X360RemapStudio\\X360Remap.json", kFallback[i]);
		probe[sizeof(probe) - 1] = '\0';
		if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES && TryStudioDrive(kFallback[i], false))
			return true;
	}
	for (int i = 0; i < kFallbackCount; i++)
		if (TryStudioDrive(kFallback[i], false)) return true;
	if (pass == STORAGE_PASS_FINAL)
		for (int i = 0; i < kFallbackCount; i++)
			if (TryStudioDrive(kFallback[i], true)) return true;
	return false;
}

// X360Remap.json est charge/enregistre depuis sept endroits differents.
// Ces deux enveloppes evitent d'eparpiller la construction du chemin (et donc
// les occasions de se tromper) ; le tampon reste local a l'appel, donc sur la
// pile de chaque thread - pas d'etat partage.
static bool LoadMappingsFromStudioConfig() {
	char path[128];
	StudioPath(path, sizeof(path), "X360Remap.json");
	return LoadMappingsFromFile(path);
}

static bool SaveMappingsToStudioConfig() {
	char path[128];
	StudioPath(path, sizeof(path), "X360Remap.json");
	return SaveMappingsToFile(path);
}

// A utiliser AVANT une modification suivie d'une sauvegarde (F9, liaison
// rapide) - AUDIT_2026-09.md, I4. false = le fichier existe mais ne se lit
// pas : ne PAS sauvegarder, sinon on l'ecraserait avec l'etat en memoire.
static bool LoadMappingsForUpdateStudio() {
	char path[128];
	StudioPath(path, sizeof(path), "X360Remap.json");
	return LoadMappingsForUpdate(path);
}

// --- Persistent file log (2026-07-31) ---
// Goal: a readable trail of what the driver actually did on a given boot,
// retrievable after the fact (via the same file transfer path used for
// X360Remap.json) without needing a UART/JTAG cable or a full XBDM/Neighborhood
// setup, and without waiting for a crash.
//
// FileLog() itself NEVER touches the disk - it only formats a line and
// appends it to a fixed-size in-memory buffer (plain memcpy, no allocation).
// This makes it safe to call from anywhere, including raw USB completion
// callbacks like setConfigurationComplete/HidAddDeviceHook - the same context
// where calling XNotifyUI directly once froze the console (see the
// Controller::mouseNotified comment / PROJECT_NOTES.md, "Incident : gel
// total"). The actual disk write only ever happens in FlushLogBuffer(),
// called once per loop from MappingManagerThreadProc - the same safe polling
// thread context already used for the mouse-detected notification.
//
// Same non-locking accepted-race pattern already used for
// Controller::mouseAccumX/Y (single effective writer at a time, drained
// periodically, tiny window where a write during a flush could be missed -
// acceptable for a diagnostic log).
#define LOG_BUFFER_SIZE 8192
char g_logBuffer[LOG_BUFFER_SIZE];
volatile int g_logBufferLen = 0;

// Demande de flush immediat depuis un contexte qui n'a PAS le droit d'appeler
// FlushLogBuffer() lui-meme (voir son commentaire : ecriture disque reservee
// au thread de polling). Simple drapeau, exactement le meme motif que
// QueueDriverFree/DrainDriverFreeQueue : poser depuis n'importe ou (callback
// USB inclus, aussi sans risque qu'un memcpy), consommer uniquement depuis
// MappingManagerThreadProc, au tour de boucle suivant (~100ms de cadence, cf
// Sleep(100) en fin de boucle - largement assez reactif pour un test de
// diagnostic, sans jamais faire d'E/S disque hors du thread prevu pour ca).
volatile bool g_forceFlushRequested = false;

// Nombre de transferts USB termines en erreur et NON remis en file
// (AUDIT_2026-09.md, C1 - voir EN_TEST_INTERRUPT_STATUS_CHECK pres de
// interruptHandler). Ecrit depuis le contexte USB (simple increment), lu par
// le heartbeat du thread de polling. Reste a 0 tant que l'interrupteur est a 0.
volatile uint32_t g_interruptErrorStops = 0;

// --- Freeze on unplug: switches that need NO recompiling (2026-10-05) ---
//
// The remaining freeze happens sometimes, with any device, mostly on unplug:
// the signature of a race between the USB stack and our cleanup. Two
// suspects are left, and each can now be switched with an empty file in the
// X360RemapStudio folder, read once at boot (see LoadFreezeTestSettings):
//
//   1. g_deferReportFree (ON by default, disable_defer_report_free.txt = old
//      behaviour). The report buffer is the memory the USB stack writes each
//      packet into (interruptTrb.buffer). It was freed IMMEDIATELY on unplug,
//      while the interrupt transfer using it is never cancelled - a packet or
//      the stack's final completion could then write into freed heap memory,
//      corrupting whatever got that memory next: a random freeze, later, with
//      any device. Same fix as the driver block: free it 5 s later.
//   2. g_interruptStatusCheck (OFF by default, enable_interrupt_status_check.txt
//      turns it on). Never re-queue a transfer that completed with an error
//      (see EN_TEST_INTERRUPT_STATUS_CHECK near interruptHandler).
volatile bool g_deferReportFree = true;
volatile bool g_interruptStatusCheck = false;
// Clean unplug with dedicated close requests (2026-10-12, see
// HidControllerExtension::closeReq). ON by default; disable_clean_unplug.txt
// goes back to the old behaviour (default endpoint closed with controlTrb).
volatile bool g_cleanUnplug = true;

// Input trace (2026-10-08, HB: in Metro the character keeps walking after the
// key is released). enable_input_trace.txt in X360RemapStudio\ turns it on:
// every change of what we hand to the game (sticks, buttons, packet number)
// is logged, plus one line per second while a stick is held. Tells at once
// whether WE keep sending the movement, or the game keeps using an old value.
volatile bool g_inputTrace = false;
// Players mode. Separate (default since 1.4.2, HB 2026-11-18): the keyboard /
// mouse is ALWAYS player 1 and a pad is player 2, whatever the plug order -
// XAM gives slots in plug order, so when a pad holds player 1 the two inputs are
// swapped at read time (g_displacedPad). Merged (1.3, opt-in): pad + keyboard /
// mouse = ONE player. Scroll Lock switches in game; players_merged.txt keeps it.
volatile bool g_playersSeparate = true;
// Test switch (2026-10-23, freeze when HB's 0079:0006 pad is plugged in, only
// with the plugin loaded): disable_generic_hid.txt = every HID interface that
// is not a boot mouse / keyboard goes straight to the console's own driver,
// untouched by us. Tells whether the freeze comes from our HID path.
volatile bool g_disableGenericHid = false;

// Stick release ramp (2026-10-08, Metro 2033: the character keeps walking
// after the key is released - also under Xenia with its own keyboard mode, so
// it is the game, not us). A key drops the stick from full to 0 in ONE read,
// something a real stick never does: it always passes through the middle
// values on its way back. Hypothesis: Metro misses that instant return.
// enable_stick_release_ramp.txt turns on a 60 ms return to the centre (full
// -> 2/3 -> 1/3 -> 0) on the LEFT stick only. Off by default until a test
// proves it; harmless for every other game if it does.
volatile bool g_stickReleaseRamp = false;
// Second Metro hypothesis: a real controller's packet number only changes when
// its state changes; ours (and Xenia's keyboard mode, which shows the same bug)
// changes on EVERY read. enable_packet_on_change.txt = real-pad behaviour.
volatile bool g_packetOnChange = false;
// Metro 2033 keeps walking when the left stick is exactly 0, which a real stick
// at rest never is (proven on HB's console, 2026-10-31). On for every game, as a
// real pad behaves; disable_stick_rest_noise.txt turns it off.
volatile bool g_stickRestNoise = true;
const DWORD STICK_RAMP_MS = 60;

// --- Miroir DbgPrint : desactive par defaut (2026-08-21) ---
//
// SUSPECT PRINCIPAL d'un gel reel signale par un testeur (Cyper) : console qui
// gele des qu'un clavier OU une souris se connecte, plugin X360Remap seul actif
// (aucun autre plugin DashLaunch), gel disparaissant completement des que
// xbdm.xex + jrpc2.xex sont charges en plus. Rien d'autre n'a change entre les
// deux essais.
//
// Le point commun : TOUT le reste de ce fichier suppose implicitement que
// DbgPrint() est sans risque a appeler depuis n'importe quel contexte USB
// (le commentaire plus haut dit meme "already proven safe"). Mais cette
// affirmation n'a jamais ete verifiee que dans un environnement ou xbdm tourne
// deja - c'est precisement l'outil utilise pour observer les logs pendant tout
// le developpement de ce projet (rgh debug watch). Aucun test hardware de ce
// fichier n'a donc jamais ete fait SANS xbdm actif, jusqu'au retour de Cyper.
//
// Hypothese testable : sur un noyau RGH ou xbdm n'est pas charge, le canal que
// DbgPrint() essaie d'utiliser n'existe pas, et l'appel bloque/gele au lieu de
// ne rien faire silencieusement (ce qui serait le comportement attendu sur un
// noyau devkit officiel). C'est cette hypothese qui explique le mieux le fait
// que le gel touche indifferemment clavier ou souris seuls (les deux passent
// par ce miroir a chaque evenement), et qu'il disparaisse des que xbdm est la.
//
// Correctif prudent : le miroir DbgPrint est desormais DESACTIVE PAR DEFAUT.
// Il ne s'active que si HDD:\X360RemapStudio\enable_dbgprint.txt existe -
// reserve aux sessions de debug volontaires (avec xbdm sait deja charge dans
// ce cas). FileLog() continue de fonctionner normalement pour tout le monde
// (buffer memoire + flush disque), seul le miroir reseau live est concerne.
volatile bool g_dbgPrintMirrorEnabled = false;

// --- TOUS les DbgPrint passent desormais par ici (2026-08-22) ---
//
// CE QUI A ETE MANQUE LE 2026-08-21 : le correctif de la veille n'avait
// neutralise QUE le miroir DbgPrint situe dans FileLog(), plus trois appels
// dans HidAddDeviceHook. Les 40 appels bruts du reste du fichier, eux, sont
// restes intacts - dont celui-ci, tout premier de DllMain :
//
//     DbgPrint("X360Remap: HELLO from xbox 360 HID controller driver...")
//
// qui s'execute AVANT la moindre ecriture disque. Autrement dit : si DbgPrint
// bloque ou fait tomber le plugin lorsque xbdm.xex n'est pas charge, le plugin
// meurt sur cette ligne, avant d'avoir pu creer son journal. Le fichier
// X360Remap_plugin.log n'existe alors PAS DU TOUT - ce qui est exactement ce
// qu'on observe sans xbdm/jrpc2, et exactement ce que Cyper rapportait
// ("aucun fichier log n'a jamais ete cree", "l'application ne detecte rien").
//
// J'avais declare cette hypothese invalidee apres un test hardware. C'etait
// une erreur de ma part : le correctif teste ne couvrait qu'une fraction des
// appels, et le plugin mourait de toute facon avant de les atteindre. Le test
// n'a donc jamais porte sur ce qu'il pretendait tester.
//
// Desormais, aucun appel a DbgPrint n'a lieu tant que l'utilisateur n'a pas
// explicitement pose HDD:\X360RemapStudio\enable_dbgprint.txt - reserve aux
// sessions de debug ou xbdm est de toute facon charge. Le plugin n'a donc plus
// aucune dependance a xbdm : c'est la reponse a "peut-on sortir de l'impasse".
#define SafeDbgPrint(...) do { if (g_dbgPrintMirrorEnabled) DbgPrint(__VA_ARGS__); } while (0)

// Minimal formatter with NO C runtime call (2026-10-12). FileLog is called
// from USB callbacks and from the USB add / remove hooks, which run in the
// USB completion context. The CRT's _vsnprintf touches per-thread data and
// may take a heap lock there: with a key-report log line in the interrupt
// callback, the console froze after 1-3 key presses. Supports what the
// plugin uses: %d %i %u %x %X (width, 0 padding, l / h prefixes), %p, %s /
// %hs, %c, %%. Anything else prints '?'.
static int LogAppendNum(char* out, int pos, int cap, unsigned long v, int base, bool upper,
	bool negative, int width, char padChar) {
	char tmp[16];
	int n = 0;
	const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	do { tmp[n++] = digits[v % base]; v /= base; } while (v && n < 15);
	if (negative) tmp[n++] = '-';
	while (n < width && n < 15) tmp[n++] = padChar;
	while (n > 0 && pos < cap) out[pos++] = tmp[--n];
	return pos;
}

static int LogFormat(char* out, int cap, const char* fmt, va_list args) {
	int pos = 0;
	if (!fmt) fmt = "(null format)";
	for (const char* f = fmt; *f && pos < cap; f++) {
		if (*f != '%') { out[pos++] = *f; continue; }
		f++;
		if (*f == '%') { out[pos++] = '%'; continue; }
		char padChar = ' ';
		if (*f == '0') { padChar = '0'; f++; }
		int width = 0;
		while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
		while (*f == 'l' || *f == 'h') f++;   // all integers are 32-bit here
		switch (*f) {
		case 'd': case 'i': {
			int v = va_arg(args, int);   // 32-bit on Xbox 360, like long
			pos = LogAppendNum(out, pos, cap, v < 0 ? (unsigned long)(0u - (unsigned)v) : (unsigned long)v, 10, false, v < 0, width, padChar);
			break;
		}
		case 'u': pos = LogAppendNum(out, pos, cap, va_arg(args, unsigned int), 10, false, false, width, padChar); break;
		case 'x': pos = LogAppendNum(out, pos, cap, va_arg(args, unsigned int), 16, false, false, width, padChar); break;
		case 'X': pos = LogAppendNum(out, pos, cap, va_arg(args, unsigned int), 16, true, false, width, padChar); break;
		case 'f': case 'g': case 'e': (void)va_arg(args, double); out[pos++] = '?'; break;   // consume the argument
		case 'p': pos = LogAppendNum(out, pos, cap, (unsigned long)va_arg(args, void*), 16, true, false, 8, '0'); break;
		case 'c': out[pos++] = (char)va_arg(args, int); break;
		case 's': {
			const char* str = va_arg(args, const char*);
			if (!str) str = "(null)";
			while (*str && pos < cap) out[pos++] = *str++;
			break;
		}
		case '\0': f--; break;
		default: out[pos++] = '?'; break;
		}
	}
	return pos;
}

void FileLog(const char* fmt, ...) {
	char line[256];
	int prefixLen = 0;
	line[prefixLen++] = '[';
	prefixLen = LogAppendNum(line, prefixLen, (int)sizeof(line) - 2, GetTickCount(), 10, false, false, 0, ' ');
	line[prefixLen++] = 'm'; line[prefixLen++] = 's'; line[prefixLen++] = ']'; line[prefixLen++] = ' ';

	va_list args;
	va_start(args, fmt);
	int end = prefixLen + LogFormat(line + prefixLen, (int)sizeof(line) - 2 - prefixLen, fmt, args);
	va_end(args);
	if (end > (int)sizeof(line) - 2) end = (int)sizeof(line) - 2;
	line[end] = '\0';

	int lineLen = (int)strlen(line);

	// Mirror every line to DbgPrint as well, so it is visible LIVE over the
	// network via XBDM (xecli: "rgh debug watch") - the only channel that
	// survives a hard freeze, since FlushLogBuffer() below can only reach the
	// disk from the polling thread, which stops running the instant the console
	// hangs. DbgPrint from this context is already proven safe: the driver has
	// always called it from these same USB callbacks without issue (unlike
	// XNotifyUI, which froze the console - see Controller::mouseNotified).
	//
	// Spaces are replaced with '_' because "rgh debug watch" only displays the
	// FIRST whitespace-delimited token of each message - confirmed on hardware,
	// where every line showed up as just "X360Remap:" with the rest cut off. With
	// no spaces, the entire line survives as a single token.
	if (g_dbgPrintMirrorEnabled) {
		char dbgLine[256];
		int n = lineLen < (int)sizeof(dbgLine) - 1 ? lineLen : (int)sizeof(dbgLine) - 1;
		for (int i = 0; i < n; i++)
			dbgLine[i] = (line[i] == ' ') ? '_' : line[i];
		dbgLine[n] = '\0';
		SafeDbgPrint("X360Remap_LOG:%s\n", dbgLine);
	}

	int len = g_logBufferLen;
	if (len + lineLen + 2 >= LOG_BUFFER_SIZE) {
		return; // buffer full - dropped until the next flush, avoids growing/allocating here
	}
	memcpy(g_logBuffer + len, line, lineLen);
	g_logBuffer[len + lineLen] = '\n';
	g_logBufferLen = len + lineLen + 1;
}

// Lit HDD:\X360RemapStudio\enable_dbgprint.txt pour reactiver volontairement
// le miroir DbgPrint (voir le commentaire pres de g_dbgPrintMirrorEnabled plus
// haut). Appelee depuis DllMain, apres l'installation des hooks - meme place
// que LoadBootProtocolSetting/LoadMappings, aucun hook USB n'est encore
// actionnable a ce stade donc aucun risque de course avec HidAddDeviceHook.
static void LoadDbgPrintMirrorSetting() {
	char path[128];
	StudioPath(path, sizeof(path), "enable_dbgprint.txt");
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		FileLog("enable_dbgprint.txt missing - DbgPrint mirror off (default)");
		return;
	}
	in.close();
	g_dbgPrintMirrorEnabled = true;
	FileLog("enable_dbgprint.txt found - DbgPrint mirror on for this session");
}

// Permet de couper TOUTES les notifications a l'ecran sans recompiler, en
// posant HDD:\X360RemapStudio\disable_notifications.txt (2026-08-22).
//
// Sert a deux choses :
//   1. TEST DECISIF de l'hypothese du gel (voir g_notifyUiSafe) : si la
//      console ne gele plus au branchement avec ce fichier present, le
//      systeme de notification est bien la cause, sans ambiguite possible.
//   2. Soupape pour un utilisateur dont la console gelerait malgre la
//      detection automatique ci-dessus - il peut se depanner seul, sans
//      attendre une nouvelle version.
//
// Appelee depuis DllMain APRES initFunctionPointers(), pour que ce choix
// explicite prime sur la detection automatique du patch.
// Reads the two freeze-test switches (see g_deferReportFree). Called once
// from DllMain, before any USB hook can run.
// --- In-game overlay, step 2: one test rectangle (2026-10-31, enable_overlay.txt) ---
// VdSwap (kernel ordinal 603, arguments as in Xenia's xboxkrnl_video.cc) is
// called once per game frame with the front buffer. It runs in the game's render
// thread, so no log, CRT or XAM here: the polling thread writes the result.
typedef void (*VdSwapFn)(void*, void*, void*, void*, void*, DWORD*, DWORD*, DWORD*, DWORD*, DWORD*);
static Detour VdSwapDetour;
static volatile LONG  g_swapCount = 0;
static volatile DWORD g_swapFront = 0, g_swapFormat = 0, g_swapWidth = 0, g_swapHeight = 0;
static volatile DWORD g_swapFetch[6];
extern DWORD g_lastTitleId;
// 0 = Aurora, FFFE.... = dashboard, F5D1.... = boot animation (it froze on the overlay).
static bool IsGameTitle(DWORD title) { return title != 0 && (title >> 24) != 0xFF && (title >> 24) != 0xF5; }
// What the overlay shows. Written by the USB callbacks: plain fields only.
static volatile uint8_t g_overlayKeys[6], g_overlayMods, g_overlayMouse;
// Text lines, written by the polling thread only.
const int HUD_LINES = 7, HUD_LINE_CHARS = 26;
static char g_hudLines[HUD_LINES][HUD_LINE_CHARS];
static volatile bool g_hudVisible = true;
// HB (2026-11-09): the keyboard / controller drawing waits for the shortcut, even when chosen in the Studio.
static volatile bool g_hudInputShown = false;
// A game sets up its screen while it starts: drawing then froze the console (2026-11-01).
static volatile bool g_hudGameReady = false;
// overlay_settings.txt, one "name value" per line; colours are RRGGBB.
static DWORD g_hudColorOn = 0xFFE020, g_hudColorOff = 0xFFFFFF;
static DWORD g_hudKeysX = 20, g_hudKeysY = 590, g_hudInfoX = 1000, g_hudInfoY = 40;
static bool g_hudInfoAlways = false;
static volatile bool g_hudEnabled = true;
// HB (2026-11-04): the input drawing is a learning aid, off by default, and the
// player picks ONE: keyboard and mouse, or the controller (keys win if both are set).
static bool g_hudShowKeys = false, g_hudShowInfo = true, g_hudShowPad = false, g_hudShowStats = true;
static bool g_hudNoTemps = false;   // disable_temps.txt: the SMC read is suspect, see ReadSmcTemps
// Last pad state a game read, for the controller drawing. Written by the game's
// XInput thread, read by the render thread: a torn copy only shows one frame wrong.
static volatile XINPUT_GAMEPAD g_hudPad;
static volatile DWORD g_hudPadTick = 0;
static bool HudStickOff(int16_t v) { return v > 3000 || v < -3000; }
// Several slots are read each frame (pad, keyboard / mouse): the one in use wins,
// a resting one only after 80 ms without any.
static void HudNotePad(const XINPUT_GAMEPAD* p) {
	const bool active = p->wButtons || p->bLeftTrigger > 8 || p->bRightTrigger > 8 || HudStickOff(p->sThumbLX) ||
		HudStickOff(p->sThumbLY) || HudStickOff(p->sThumbRX) || HudStickOff(p->sThumbRY);
	const DWORD now = GetTickCount();
	if (!active && (now - g_hudPadTick) < 80) return;
	memcpy((void*)&g_hudPad, p, sizeof(*p));
	if (active) g_hudPadTick = now;
}
static DWORD g_hudInfoSecs = 4;
static DWORD g_hudHotkey = 0xE3;   // HID usage; 0xE3 = either Windows key
static int g_hudStyle = 1;   // 1 = Kenney key images, 0 = drawn keys
static volatile DWORD g_hudInfoUntil = 0;   // aim settings stay 4 s after a change
static volatile LONG g_hudFrames = 0;
static char g_hudStats[40] = "";
// Messages (recording started...), two buffers so the render thread never reads one being written.
static char g_hudToast[2][56];
static volatile int g_hudToastSet = 0;
static volatile DWORD g_hudToastUntil = 0;
static bool g_overlayProbe = false;

// Texel index of (x, y) in a tiled 32-bit surface (XGAddress2DTiledOffset).
static DWORD TiledOffset32(DWORD x, DWORD y, DWORD alignedWidth) {
	const DWORD macro = ((x >> 5) + (y >> 5) * (alignedWidth >> 5)) << 9;
	const DWORD micro = ((x & 7) + ((y & 6) << 2)) << 2;
	const DWORD off = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << 5) + ((y & 1) << 4);
	return (((off & ~511u) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
		((((y & 8) >> 2) + (x >> 3)) & 3) * 64) >> 2;
}

// The frame being drawn on, from VdSwap's fetch constant (bits as in Xenia's xenos.h:
// dword_0 pitch / 32 in bits 22-30, tiled in bit 31; dword_1 format in bits 0-5).
// 32-bit formats only; the game can repaint between two frames.
static volatile DWORD* g_hudBase;
static DWORD g_hudOn, g_hudOff, g_hudShade, g_hudTag;
static bool g_hudWide, g_hudTiled = true;
static DWORD g_hudW = 1280, g_hudH = 720, g_hudPitch = 1280;

static void HudPixel(DWORD x, DWORD y, DWORD color) {
	if (x >= g_hudW || y >= g_hudH) return;
	volatile DWORD* p = g_hudBase + (g_hudTiled ? TiledOffset32(x, y, g_hudPitch) : y * g_hudPitch + x);
	*p = color;
	__dcbf(0, (void*)p);
}

// Never reads the frame: reading made the overlay late and speckled (2026-11-01).
static void HudBlend(DWORD x, DWORD y, DWORD color, DWORD alpha) {
	if (alpha >= 8) HudPixel(x, y, color);
}

// Positions are set for 1280x720: on another screen size each one keeps its
// distance to the nearest edge, and the drawings keep their size.
static DWORD HudAx(DWORD x) { const int v = (int)x + (int)g_hudW - 1280; return x > 640 ? (DWORD)(v > 0 ? v : 0) : x; }
static DWORD HudAy(DWORD y) { const int v = (int)y + (int)g_hudH - 720; return y > 360 ? (DWORD)(v > 0 ? v : 0) : y; }

// Rounded key, leaning right by skew pixels; edges are smoothed.
static void HudKeyShape(DWORD x, DWORD y, DWORD w, DWORD h, DWORD skew, bool filled, DWORD color) {
	static const DWORD kInset[5] = { 5, 3, 2, 1, 1 };
	for (DWORD j = 0; j < h; j++) {
		const DWORD lean = skew * 16 * (h - 1 - j) / (h - 1);    // 1/16 pixel
		const DWORD frac = lean & 15, base = x + (lean >> 4);
		DWORD inset = 0;
		if (j < 5) inset = kInset[j];
		else if (j >= h - 5) inset = kInset[h - 1 - j];
		const DWORD left = base + inset, right = base + w - 1 - inset;
		const bool edgeRow = j < 2 || j >= h - 2;
		if (filled || edgeRow) {
			for (DWORD i = left + 1; i < right; i++) HudBlend(i, y + j, color, 15);
		} else {
			HudBlend(left + 1, y + j, color, 15);
			HudBlend(right - 1, y + j, color, 15);
		}
		HudBlend(left, y + j, color, 15 - frac);
		HudBlend(right, y + j, color, frac ? frac : 15);
	}
}

static DWORD HudText(DWORD x, DWORD y, const char* text, DWORD color, bool small) {
	const HudGlyph* glyphs = small ? kHudSmallGlyphs : kHudBigGlyphs;
	const uint8_t* data = small ? kHudSmallData : kHudBigData;
	const DWORD h = small ? HUD_SMALL_HEIGHT : HUD_BIG_HEIGHT;
	for (; *text; text++) {
		const unsigned char ch = (unsigned char)*text;
		if (ch < 32 || ch > 126) continue;
		const HudGlyph& g = glyphs[ch - 32];
		for (DWORD j = 0; j < h; j++)
			for (DWORD i = 0; i < g.w; i++) HudBlend(x + i, y + j, color, data[g.offset + j * g.w + i]);
		x += g.advance;
	}
	return x;
}
static DWORD HudTextWidth(const char* text, bool small) {
	DWORD w = 0;
	for (; *text; text++) {
		const unsigned char ch = (unsigned char)*text;
		if (ch >= 32 && ch <= 126) w += (small ? kHudSmallGlyphs : kHudBigGlyphs)[ch - 32].advance;
	}
	return w;
}
// Readable anywhere: a soft dark copy under the text.
static DWORD HudTextShadow(DWORD x, DWORD y, const char* text, DWORD color, bool small) {
	HudText(x + 1, y + 1, text, g_hudShade, small);
	return HudText(x, y, text, color, small);
}

#include "hud_color.h"
static DWORD g_hudFetch1, g_hudFetch3;   // the frame being drawn on, set by OverlayDraw
static DWORD HudColor(DWORD rgb, bool wide) { return HudPackColor(rgb, wide, g_hudFetch1, g_hudFetch3); }

// The polling thread fills the unused set and flips g_hudCapSet, so the render
// thread never reads a list being rewritten.
static HudCap g_hudCaps[2][HUD_MAX_CAPS];
static int g_hudCapCount[2];
static volatile int g_hudCapSet = 0;
static DWORD g_hudBlockY, g_hudBlockW;

static bool HudKeyDown(uint8_t code) {
	if (code >= 0xE0 && code <= 0xE7) return (g_overlayMods >> (code - 0xE0)) & 1;
	for (int i = 0; i < 6; i++) if (g_overlayKeys[i] == code) return true;
	return false;
}

// Kenney "Input Prompts" key (CC0). False when the pack has no image for this key.
static bool HudDrawKeySprite(uint8_t code, DWORD x, DWORD y, bool down) {
	for (size_t i = 0; i < sizeof(kHudSprites) / sizeof(kHudSprites[0]); i++) {
		if (kHudSprites[i].code != code) continue;
		const uint8_t* p = kHudSpriteData + kHudSprites[i].offset;
		if (down) HudKeyShape(x + 4, y + 4, 28, 28, 0, true, g_hudOn);
		const DWORD color = down ? g_hudShade : g_hudOff;
		for (int k = 0; k < kHudSprites[i].bytes; k += 2) HudPixel(x + p[k], y + p[k + 1], color);
		return true;
	}
	return false;
}

// Small Xbox button: coloured disc for A B X Y, plain text for the others.
static void HudActionIcon(DWORD x, DWORD y, uint8_t action, bool wide) {
	static const DWORD kFace[4] = { 0x6FBF3A, 0xE5413B, 0x2F8DE8, 0xF0B429 };
	const char* tag = HudActionLabel(action);
	if (action >= KEYACT_A && action <= KEYACT_Y) {
		const DWORD color = HudColor(kFace[action - KEYACT_A], wide);
		for (int j = 0; j < HUD_DISC; j++)
			for (int i = 0; i < HUD_DISC; i++) HudBlend(x + i, y + j, color, kHudDisc[j * HUD_DISC + i]);
		HudText(x + 3, y + 2, tag, HudColor(0xFFFFFF, wide), true);
	} else {
		HudText(x, y + 2, tag, g_hudTag, true);
	}
}

static void HudPadArea(DWORD x, DWORD y, int area, DWORD color) {
	const uint8_t* p = kHudPadAreas[area];
	for (int i = 0; i < kHudPadAreaBytes[area]; i += 2) HudBlend(x + p[i], y + p[i + 1], color, 15);
}

// A trigger fills from the bottom, as far as it is pressed.
static void HudPadTrigger(DWORD x, DWORD y, int area, uint8_t value) {
	if (value < 8) return;
	const uint8_t* p = kHudPadAreas[area];
	int top = 255, bottom = 0;
	for (int i = 1; i < kHudPadAreaBytes[area]; i += 2) {
		if (p[i] < top) top = p[i];
		if (p[i] > bottom) bottom = p[i];
	}
	const int cut = bottom - (bottom - top + 1) * value / 255;
	for (int i = 0; i < kHudPadAreaBytes[area]; i += 2)
		if (p[i + 1] > cut) HudBlend(x + p[i], y + p[i + 1], g_hudOn, 15);
}

static void HudPadStick(DWORD x, DWORD y, int16_t sx, int16_t sy, DWORD color) {
	const int dx = sx * HUD_PAD_STICK_REACH / 32768, dy = -sy * HUD_PAD_STICK_REACH / 32768;
	for (int j = -2; j <= 2; j++)
		for (int i = -2; i <= 2; i++)
			if (i * i + j * j <= 5) HudBlend(x + dx + i, y + dy + j, color, 15);
}

// HB's two-view controller (2026-11-03): what the game receives, so a keyboard
// key lights the Xbox button it stands for.
static void HudDrawPad(DWORD x, DWORD y) {
	XINPUT_GAMEPAD pad;
	memcpy(&pad, (const void*)&g_hudPad, sizeof(pad));
	static const struct { WORD bit; uint8_t area; } kBits[] = {
		{ XINPUT_GAMEPAD_A, HUD_PAD_A }, { XINPUT_GAMEPAD_B, HUD_PAD_B }, { XINPUT_GAMEPAD_X, HUD_PAD_X },
		{ XINPUT_GAMEPAD_Y, HUD_PAD_Y }, { XINPUT_GAMEPAD_BACK, HUD_PAD_BACK }, { XINPUT_GAMEPAD_START, HUD_PAD_START },
		{ XINPUT_GAMEPAD_LEFT_THUMB, HUD_PAD_LS }, { XINPUT_GAMEPAD_RIGHT_THUMB, HUD_PAD_RS },
		{ XINPUT_GAMEPAD_LEFT_SHOULDER, HUD_PAD_LB }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, HUD_PAD_RB },
		{ XINPUT_GAMEPAD_DPAD_UP, HUD_PAD_DU }, { XINPUT_GAMEPAD_DPAD_DOWN, HUD_PAD_DD },
		{ XINPUT_GAMEPAD_DPAD_LEFT, HUD_PAD_DL }, { XINPUT_GAMEPAD_DPAD_RIGHT, HUD_PAD_DR },
	};
	for (size_t i = 0; i < sizeof(kBits) / sizeof(kBits[0]); i++)
		if (pad.wButtons & kBits[i].bit) HudPadArea(x, y, kBits[i].area, g_hudOn);
	HudPadTrigger(x, y, HUD_PAD_LT, pad.bLeftTrigger);
	HudPadTrigger(x, y, HUD_PAD_RT, pad.bRightTrigger);
	for (int i = 0; i < (int)sizeof(kHudPadOutline); i += 2)
		HudBlend(x + kHudPadOutline[i], y + kHudPadOutline[i + 1], g_hudOff, 15);
	HudPadStick(x + HUD_PAD_LS_X, y + HUD_PAD_LS_Y, pad.sThumbLX, pad.sThumbLY,
		(pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) ? g_hudShade : g_hudOn);
	HudPadStick(x + HUD_PAD_RS_X, y + HUD_PAD_RS_Y, pad.sThumbRX, pad.sThumbRY,
		(pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) ? g_hudShade : g_hudOn);
}

static void OverlayDraw(DWORD front, DWORD fetch0, DWORD fetch1, DWORD fetch3, DWORD width, DWORD height) {
	if (!MmIsAddressValid((PVOID)front)) return;
	// 6 / 50 = 8_8_8_8, 7 / 54 (0x36, Metro) = 2_10_10_10; any other format is not drawn on.
	const DWORD format = fetch1 & 0x3F;
	const bool wide = format == 7 || format == 54;
	if (!wide && format != 6 && format != 50) return;
	const DWORD pitch = ((fetch0 >> 22) & 0x1FF) * 32;
	if (pitch < width) return;
	g_hudBase = (volatile DWORD*)front;
	g_hudW = width; g_hudH = height; g_hudPitch = pitch; g_hudTiled = (fetch0 >> 31) != 0;
	g_hudWide = wide;
	g_hudFetch1 = fetch1; g_hudFetch3 = fetch3;
	g_hudOn = HudColor(g_hudColorOn, wide);
	g_hudOff = HudColor(g_hudColorOff, wide);
	g_hudShade = HudColor(0x05090C, wide);
	g_hudTag = HudColor(0xB8C6D0, wide);
	if (g_hudShowKeys && g_hudInputShown) {
		const DWORD ox = HudAx(g_hudKeysX), oy = HudAy(g_hudBlockY);
		const int set = g_hudCapSet;
		for (int k = 0; k < g_hudCapCount[set]; k++) {
			const HudCap& cap = g_hudCaps[set][k];
			const bool down = HudKeyDown(cap.code);
			const DWORD x = ox + cap.x, y = oy + cap.y;
			if (g_hudStyle && HudDrawKeySprite(cap.code, x, y, down)) {
				HudActionIcon(x + HUD_SPRITE + 1, y + 11, cap.action, wide);
				continue;
			}
			HudKeyShape(x, y, cap.w, 30, 5, down, down ? g_hudOn : g_hudOff);
			HudTextShadow(x + 8, y + 6, cap.label, down ? g_hudShade : g_hudOff, false);
			HudActionIcon(x + cap.w - 17, y + 8, cap.action, wide);
		}
		// Mouse: left, right, wheel, body, side buttons (HID bits 0, 1, 2, 3, 4).
		const uint8_t m = g_overlayMouse;
		const DWORD mx = ox + g_hudBlockW + 14, my = oy;
		for (int area = 0; area < 5; area++) {
			if (!(m & (1 << area))) continue;
			for (int i = 0; i < kHudMouseAreaBytes[area]; i += 2)
				HudPixel(mx + kHudMouseAreas[area][i], my + kHudMouseAreas[area][i + 1], g_hudOn);
		}
		for (int i = 0; i < (int)sizeof(kHudMouseOutline); i += 2)
			HudPixel(mx + kHudMouseOutline[i], my + kHudMouseOutline[i + 1], g_hudOff);
	}
	if (g_hudShowPad && !g_hudShowKeys && g_hudInputShown)
		HudDrawPad(HudAx(g_hudKeysX), HudAy(g_hudKeysY + HUD_PAD_H > 712 ? 712 - HUD_PAD_H : g_hudKeysY));

	if (g_hudShowStats) {
		const DWORD w = HudTextWidth(g_hudStats, false);
		HudTextShadow(w + 20 < g_hudW ? g_hudW - 12 - w : 8, 8, g_hudStats, HudColor(0x7BE36A, wide), false);
	}
	if (GetTickCount() < g_hudToastUntil) {
		const char* text = g_hudToast[g_hudToastSet];
		const DWORD w = HudTextWidth(text, false) + 32, tx = w < g_hudW ? (g_hudW - w) / 2 : 0, ty = 48;
		for (DWORD j = 2; j < 36; j++)
			for (DWORD i = 2 + (j & 1); i < w - 2; i += 2) HudBlend(tx + i, ty + j, g_hudShade, 15);
		HudKeyShape(tx, ty, w, 38, 0, false, g_hudOn);
		HudText(tx + 16, ty + 10, text, g_hudOff, false);
	}

	// HB (2026-11-09): no aim panel on the picture, F12 and the tuning keys stay Xbox notifications.
	__sync();
}

// Flicker / tremble (HB, rgdshock): VdSwap only QUEUES the swap. The game's resolve
// of this frame into *front runs on the GPU later, and overwrites what we draw right
// after VdSwap on the frames where the GPU is behind: the drawing is there on some
// frames only. draw_mode 1 (overlay_settings.txt, read live) draws into the PREVIOUS
// front instead, whose resolve is over. Only when the game's buffer rotation is
// confirmed (the front from 2 or 3 swaps ago is the current one) and the previous
// front has the same fetch constant (size, pitch, format): a front the game just
// freed (menu, resolution change) breaks the pattern and nothing is drawn.
static volatile DWORD g_hudDrawMode = 0;
static DWORD g_prevFront[3] = { 0, 0, 0 };   // [0] = previous swap, [1] = 2 swaps ago, [2] = 3
static DWORD g_prevFetch0 = 0, g_prevFetch1 = 0;

// Exit hangs (2026-11-12, 11-13, and 11-17 quitting Metro 2033 through the Guide's "Aurora
// Home"): the game froze while quitting, the console alive, our drawing still writing into
// its pictures. Nothing is drawn while the Xbox Guide is open nor 1.5 s after it closes:
// quitting through the Guide happens entirely inside that window. Set by the polling
// thread (XamIsUIActive is XAM: never called from the game's render thread).
static volatile DWORD g_hudUiQuietUntil = 0;

// Restored from 6e14257 (removed in 71fa5a6 with another change when game launches froze;
// the capture module was later found to carry those freezes). When a game quits, or
// between two titles, it presents frames from buffers it is about to free or never used
// before: draw only on a buffer seen for 30 of its own frames, at a size unchanged for 2 s.
static bool FrontIsSettled(DWORD front, DWORD width, DWORD height) {
	static DWORD s_w = 0, s_h = 0, s_sameSize = 0;
	static DWORD s_front[4] = { 0, 0, 0, 0 }, s_seen[4] = { 0, 0, 0, 0 };
	if (width != s_w || height != s_h) {
		s_w = width; s_h = height; s_sameSize = 0;
		for (int i = 0; i < 4; i++) s_front[i] = s_seen[i] = 0;
	}
	if (s_sameSize < 0x7FFFFFFF) s_sameSize++;
	int slot = -1;
	for (int i = 0; i < 4 && slot < 0; i++) if (s_front[i] == front) slot = i;
	if (slot < 0) {   // a buffer not seen yet: take the least used slot
		slot = 0;
		for (int i = 1; i < 4; i++) if (s_seen[i] < s_seen[slot]) slot = i;
		s_front[slot] = front; s_seen[slot] = 0;
	}
	if (s_seen[slot] < 0x7FFFFFFF) s_seen[slot]++;
	return s_sameSize > 120 && s_seen[slot] > 30;
}

static void VdSwapHook(void* buffer, void* fetch, void* a2, void* a3, void* a4,
                       DWORD* front, DWORD* format, DWORD* colorSpace, DWORD* width, DWORD* height) {
	InterlockedIncrement(&g_swapCount);
	InterlockedIncrement(&g_hudFrames);
	if (front) g_swapFront = *front;
	if (format) g_swapFormat = *format;
	if (width) g_swapWidth = *width;
	if (height) g_swapHeight = *height;
	if (fetch) memcpy((void*)g_swapFetch, fetch, sizeof(g_swapFetch));
	((VdSwapFn)VdSwapDetour.GetOriginal<VdSwapFn>())(buffer, fetch, a2, a3, a4, front, format, colorSpace, width, height);
	const bool settled = front && *front && width && height && FrontIsSettled(*front, *width, *height);
	if (settled && (LONG)(GetTickCount() - g_hudUiQuietUntil) >= 0 &&
		g_hudVisible && g_hudEnabled && g_hudGameReady && fetch &&
		*width >= 640 && *width <= 1920 && *height >= 360 && *height <= 1080) {
		// Not the scan-out buffer (register 7FC86110): writing there froze games a
		// few menus in, 3 times out of 3 (2026-11-01).
		if (g_hudDrawMode == 1) {
			const DWORD prev = g_prevFront[0];
			const bool rotation = prev && prev != *front && (g_prevFront[1] == *front || g_prevFront[2] == *front);
			if (rotation && g_prevFetch0 == g_swapFetch[0] && g_prevFetch1 == g_swapFetch[1])
				OverlayDraw(prev, g_swapFetch[0], g_swapFetch[1], g_swapFetch[3], *width, *height);
		} else {
			OverlayDraw(*front, g_swapFetch[0], g_swapFetch[1], g_swapFetch[3], *width, *height);
		}
	}
	if (front && *front && fetch) {
		g_prevFront[2] = g_prevFront[1]; g_prevFront[1] = g_prevFront[0]; g_prevFront[0] = *front;
		g_prevFetch0 = g_swapFetch[0]; g_prevFetch1 = g_swapFetch[1];
	}
}

// Polling thread, end of boot (the disk is mounted by then).
static DWORD HudClamp(DWORD v, DWORD maxValue) { return v > maxValue ? maxValue : v; }

// The limits keep every pixel inside 1280x720, whatever the file says.
static void LoadOverlayLook() {
	char path[128];
	StudioPath(path, sizeof(path), "overlay_settings.txt");
	std::ifstream in(path, std::ios::binary);
	std::string line;
	g_hudDrawMode = 0;   // test key: removing the line goes back to mode 0
	while (in.is_open() && std::getline(in, line)) {
		char name[16];
		unsigned int v = 0;
		if (sscanf(line.c_str(), "%15s %x", name, &v) != 2) continue;
		if (!strcmp(name, "color_on")) g_hudColorOn = v & 0xFFFFFF;
		else if (!strcmp(name, "color_off")) g_hudColorOff = v & 0xFFFFFF;
		else if (!strcmp(name, "keys_x")) g_hudKeysX = HudClamp(v, 790);
		else if (!strcmp(name, "keys_y")) g_hudKeysY = HudClamp(v, 590);
		else if (!strcmp(name, "info_x")) g_hudInfoX = HudClamp(v, 1010);
		else if (!strcmp(name, "info_y")) g_hudInfoY = HudClamp(v, 540);
		else if (!strcmp(name, "info_always")) g_hudInfoAlways = v != 0;
		else if (!strcmp(name, "style")) g_hudStyle = v != 0;
		else if (!strcmp(name, "show_keys")) g_hudShowKeys = v != 0;
		else if (!strcmp(name, "show_info")) g_hudShowInfo = v != 0;
		else if (!strcmp(name, "show_pad")) g_hudShowPad = v != 0;
		else if (!strcmp(name, "show_stats")) g_hudShowStats = v != 0;
		else if (!strcmp(name, "draw_mode")) g_hudDrawMode = v == 1 ? 1 : 0;
		else if (!strcmp(name, "info_secs")) g_hudInfoSecs = (v == 2 || v == 8) ? v : 4;
		else if (!strcmp(name, "hotkey")) g_hudHotkey = (v & 0xFF) >= 0xE0 && (v & 0xFF) <= 0xE7 ? v & 0xFF : 0xE3;
	}
	StudioPath(path, sizeof(path), "disable_temps.txt");
	g_hudNoTemps = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
	// The hook stays until the next boot once installed: this only stops the drawing.
	StudioPath(path, sizeof(path), "enable_overlay.txt");
	g_hudEnabled = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Boot freezes of 2026-11-05 and 11-06: both frozen boots stopped about 1 s after the
// hook was installed at 14.6 s, while the boot animation was switching to Aurora. So
// the hook now goes in at DllMain, before anything draws, and the files only say
// whether to draw (g_hudEnabled). It costs one call per frame when the overlay is off.
static void InstallVdSwapHook() {
	void* vdSwap = nullptr;
	HANDLE kernel = GetModuleHandleA("xboxkrnl.exe");
	if (!kernel || XexGetProcedureAddress(kernel, 603, &vdSwap) != 0 || !vdSwap) {
		FileLog("VdSwap not found - in-game display off");
		return;
	}
	VdSwapDetour = Detour(vdSwap, (void*)VdSwapHook);
	VdSwapDetour.Install();
	g_overlayProbe = true;
	FileLog("VdSwap hook on at %08x at load (in-game display)", (unsigned int)(uintptr_t)vdSwap);
}

// Black box (2026-11-12, FREEZE_STATIC_ANALYSIS.md). A total freeze may be the kernel's
// own fatal path: VdDisplayFatalError ends by stopping every CPU. These two hooks save
// the error code, the caller and the unflushed log lines to fatal_log.txt, then let the
// kernel go on. Nothing here runs on a healthy console.
extern "C" void* _ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
typedef void (*FatalErrorFn)(DWORD);
typedef void (*BugCheckExFn)(DWORD, DWORD, DWORD, DWORD, DWORD);
static Detour FatalErrorDetour;
static Detour BugCheckExDetour;
static char g_fatalLogPath[128];   // built at install: no path work on the dying path

// The IRQL lives in the processor block (r13 + 0x18), as the kernel itself reads it.
static __declspec(naked) DWORD CurrentIrql() {
	__asm {
		lbz r3, 0x18(r13)
		blr
	}
}

// Stack of the calling thread, from the processor block: base at r13 + 0x70, limit at + 0x74
// (KPCR in xkelib's kePrivateStructs.h).
static __declspec(naked) DWORD ThreadStackBase() {
	__asm {
		lwz r3, 0x70(r13)
		blr
	}
}
static __declspec(naked) DWORD ThreadStackLimit() {
	__asm {
		lwz r3, 0x74(r13)
		blr
	}
}

// How deep the polling thread's stack has ever gone: the unused part is filled with a
// pattern once, at thread start, and the first changed word is the deepest point reached.
static DWORD g_pollStackBase = 0, g_pollStackLimit = 0;
static void PollStackPaint() {
	volatile DWORD here = 0;
	const DWORD sp = (DWORD)(uintptr_t)&here;
	const DWORD base = ThreadStackBase(), limit = ThreadStackLimit();
	g_pollStackBase = base;
	if (limit == 0 || limit >= sp || base <= sp || base - limit > 0x100000) return;   // not what we expect: paint nothing
	for (DWORD* q = (DWORD*)(uintptr_t)limit; (DWORD)(uintptr_t)q + 0x400 < sp; q++) *q = 0xA5A5A5A5;
	g_pollStackLimit = limit;
}
static DWORD PollStackDeepest() {
	if (!g_pollStackLimit) return 0;
	const DWORD* q = (const DWORD*)(uintptr_t)g_pollStackLimit;
	while ((DWORD)(uintptr_t)q < g_pollStackBase && *q == 0xA5A5A5A5) q++;
	return g_pollStackBase - (DWORD)(uintptr_t)q;
}

static void BlackBoxSave(const char* what, DWORD code, DWORD p1, DWORD p2, void* caller) {
	static volatile LONG saved = 0;
	const DWORD irql = CurrentIrql();
	// File I/O is only allowed below DISPATCH level; above it the record is lost.
	if (irql >= 2 || InterlockedExchange(&saved, 1)) return;
	char line[200];
	int n = _snprintf(line, sizeof(line) - 1,
		"[%ums] %hs code=%08x p1=%08x p2=%08x caller=%08x irql=%u title=%08x plugin=%hs\r\n",
		(unsigned int)GetTickCount(), what, (unsigned int)code, (unsigned int)p1, (unsigned int)p2,
		(unsigned int)(uintptr_t)caller, (unsigned int)irql, (unsigned int)g_lastTitleId, X360REMAP_VERSION);
	if (n < 0) n = sizeof(line) - 1;
	HANDLE f = CreateFileA(g_fatalLogPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	SetFilePointer(f, 0, NULL, FILE_END);
	DWORD done = 0;
	WriteFile(f, line, (DWORD)n, &done, NULL);
	int len = g_logBufferLen;   // the lines that never reached the disk
	if (len > 0 && len <= LOG_BUFFER_SIZE) WriteFile(f, g_logBuffer, (DWORD)len, &done, NULL);
	FlushFileBuffers(f);
	CloseHandle(f);
}

static void FatalErrorHook(DWORD code) {
	BlackBoxSave("VdDisplayFatalError", code, 0, 0, _ReturnAddress());
	FatalErrorDetour.GetOriginal<FatalErrorFn>()(code);
}

static void BugCheckExHook(DWORD code, DWORD p1, DWORD p2, DWORD p3, DWORD p4) {
	BlackBoxSave("KeBugCheckEx", code, p1, p2, _ReturnAddress());
	BugCheckExDetour.GetOriginal<BugCheckExFn>()(code, p1, p2, p3, p4);
}

static void InstallBlackBox() {
	StudioPath(g_fatalLogPath, sizeof(g_fatalLogPath), "fatal_log.txt");
	void* fatal = nullptr;
	void* bugCheck = nullptr;
	HANDLE kernel = GetModuleHandleA("xboxkrnl.exe");
	if (!kernel) return;
	if (XexGetProcedureAddress(kernel, 434, &fatal) == 0 && fatal) {
		FatalErrorDetour = Detour(fatal, (void*)FatalErrorHook);
		FatalErrorDetour.Install();
	}
	if (XexGetProcedureAddress(kernel, 83, &bugCheck) == 0 && bugCheck) {
		BugCheckExDetour = Detour(bugCheck, (void*)BugCheckExHook);
		BugCheckExDetour.Install();
	}
	FileLog("Black box on: VdDisplayFatalError %08x, KeBugCheckEx %08x -> fatal_log.txt",
		(unsigned int)(uintptr_t)fatal, (unsigned int)(uintptr_t)bugCheck);
}

static void LoadOverlaySetting() {
	LoadOverlayLook();
	FileLog("In-game display %hs (enable_overlay.txt)", g_hudEnabled ? "on" : "off");
}

static void LogOverlayProbe(uint32_t title) {
	static DWORD s_last = 0;
	const DWORD now = GetTickCount();
	if (!g_overlayProbe || !g_hudEnabled || (now - s_last) < 5000) return;
	const DWORD elapsed = now - s_last;
	s_last = now;
	const LONG frames = InterlockedExchange(&g_swapCount, 0);
	FileLog("Overlay probe: %u frames/s title=%08x front=%08x format=%08x %ux%u fetch %08x %08x %08x %08x %08x %08x",
		(unsigned int)(frames * 1000 / (LONG)elapsed), (unsigned int)title, (unsigned int)g_swapFront,
		(unsigned int)g_swapFormat, (unsigned int)g_swapWidth, (unsigned int)g_swapHeight,
		(unsigned int)g_swapFetch[0], (unsigned int)g_swapFetch[1], (unsigned int)g_swapFetch[2],
		(unsigned int)g_swapFetch[3], (unsigned int)g_swapFetch[4], (unsigned int)g_swapFetch[5]);
}

static void LoadFreezeTestSettings() {
	char path[128];
	StudioPath(path, sizeof(path), "disable_defer_report_free.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_deferReportFree = false; }
	}
	StudioPath(path, sizeof(path), "enable_interrupt_status_check.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_interruptStatusCheck = true; }
	}
	StudioPath(path, sizeof(path), "disable_clean_unplug.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_cleanUnplug = false; FileLog("disable_clean_unplug.txt found - old unplug path"); }
	}
	StudioPath(path, sizeof(path), "enable_stick_release_ramp.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_stickReleaseRamp = true; FileLog("enable_stick_release_ramp.txt found - left stick release ramp ON"); }
	}
	StudioPath(path, sizeof(path), "enable_packet_on_change.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_packetOnChange = true; FileLog("enable_packet_on_change.txt found - packet number only changes with the state"); }
	}
	StudioPath(path, sizeof(path), "disable_stick_rest_noise.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_stickRestNoise = false; FileLog("disable_stick_rest_noise.txt found - left stick can be exactly 0"); }
	}
	StudioPath(path, sizeof(path), "enable_input_trace.txt");
	{
		std::ifstream in(path, std::ios::binary);
		if (in.is_open()) { in.close(); g_inputTrace = true; FileLog("enable_input_trace.txt found - input trace ON"); }
	}
	StudioPath(path, sizeof(path), "players_merged.txt");
	{
		std::ifstream in(path, std::ios::binary);
		g_playersSeparate = !in.is_open();
		if (in.is_open()) in.close();
		FileLog("Players mode: %hs", g_playersSeparate ? "separate, keyboard / mouse = player 1 (default)" : "merged (players_merged.txt)");
	}
	StudioPath(path, sizeof(path), "disable_generic_hid.txt");
	{
		std::ifstream in(path, std::ios::binary);
		g_disableGenericHid = in.is_open();
		if (in.is_open()) { in.close(); FileLog("disable_generic_hid.txt found - non-boot HID devices left to the console"); }
	}
	// Also written to its own small file, straight away: the first lines of the
	// plugin log are often lost at boot (log buffer full before the first flush).
	{
		char sp[128];
		StudioPath(sp, sizeof(sp), "X360Remap_switches.txt");
		std::ofstream o(sp, std::ios::binary | std::ios::trunc);
		if (o.is_open()) {
			o << "X360Remap switches read at boot\r\n";
			o << "deferred report free: " << (g_deferReportFree ? "ON" : "OFF") << "\r\n";
			o << "interrupt status check: " << (g_interruptStatusCheck ? "ON" : "OFF") << "\r\n";
			o << "stick release ramp: " << (g_stickReleaseRamp ? "ON" : "OFF") << "\r\n";
			o << "packet on change: " << (g_packetOnChange ? "ON" : "OFF") << "\r\n";
			o << "stick rest noise: " << (g_stickRestNoise ? "ON" : "OFF") << "\r\n";
			o << "input trace: " << (g_inputTrace ? "ON" : "OFF") << "\r\n";
			o << "clean unplug: " << (g_cleanUnplug ? "ON" : "OFF") << "\r\n";
			o << "players: " << (g_playersSeparate ? "separate" : "merged") << "\r\n";
			o << "generic HID: " << (g_disableGenericHid ? "OFF (disable_generic_hid.txt)" : "ON") << "\r\n";
		}
	}
	FileLog("Freeze test switches: deferred report free %hs, interrupt status check %hs",
		g_deferReportFree ? "ON" : "OFF (disable_defer_report_free.txt)",
		g_interruptStatusCheck ? "ON (enable_interrupt_status_check.txt)" : "OFF");
}

static void LoadNotificationSetting() {
	char path[128];
	StudioPath(path, sizeof(path), "disable_notifications.txt");
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
		return;
	in.close();
	g_notifyUiSafe = false;
	FileLog("disable_notifications.txt found - notifications OFF (user choice)");
}

// Only called from MappingManagerThreadProc (safe thread context) - appends
// the buffered lines to HDD:\X360Remap_plugin.log and resets the buffer.
//
// Troncature au premier flush de chaque demarrage (2026-08-03, demande HB :
// le fichier s'accumulait sur plusieurs redemarrages Aurora, melangeant
// plusieurs sessions de test et rendant le diagnostic impossible a partir
// d'un seul rgh fs cat). g_logTruncatedThisBoot vit uniquement dans ce
// thread (meme contexte que g_logBufferLen), pas de synchronisation requise.
bool g_logTruncatedThisBoot = false;

// INTERRUPTEUR DE DIAGNOSTIC (2026-08-26) : theorie de HB - le gel standalone
// (sans xbdm/jrpc2, confirme sur VID:0461 PID:4E35 malgre notifications,
// SET_PROTOCOL et GET_DESCRIPTOR HID deja innocentes par test) viendrait de
// l'ecriture disque elle-meme, faite ici sur le thread de polling pendant
// que HidAddDeviceHook enumere le peripherique sur un autre thread - une
// collision sur une ressource noyau partagee (disque/USB) que la presence
// de xbdm masquerait simplement par un timing different. A 0 : plus aucune
// ecriture sur HDD:\X360RemapStudio\X360Remap_plugin.log, le tampon memoire
// (g_logBuffer, rempli par FileLog - jamais desactive, toujours sans risque)
// est simplement vide a chaque appel au lieu d'etre ecrit. Diagnostic
// uniquement : si confirme, la vraie fonctionnalite (bascule activer/
// desactiver cote application, avec avertissement "xbdm requis" et envoi
// des logs au developpeur) sera construite proprement plutot que de garder
// ce commutateur brut.
//
// CONCLUSION (2026-08-26) : theorie testee et infirmee sur materiel reel -
// log disque totalement coupe, le gel standalone persiste identique. La
// cause n'est donc PAS ici. Remis a 1 (comportement normal, log actif) :
// utile pour les diagnostics futurs, et sans risque confirme par ce test
// meme. Voir la conclusion generale pres de EN_TEST_CONTROL_REQUESTS_BOOT_MOUSE :
// seule la presence de xbdm.xex (sans meme jrpc2) evite ce gel - prerequis
// noyau confirme par elimination, hors de portee de notre code.
#define EN_TEST_DISABLE_DISK_LOG 0

void FlushLogBuffer() {
	int len = g_logBufferLen;
	if (len <= 0) return;

#if !EN_TEST_DISABLE_DISK_LOG
	std::ios_base::openmode mode = std::ios::binary;
	mode |= g_logTruncatedThisBoot ? std::ios::app : std::ios::trunc;

	// Deplace de HDD:\ vers HDD:\X360RemapStudio\ le 2026-08-03. C'etait le
	// DERNIER fichier du projet reste a la racine du disque : tout le reste
	// (X360Remap.json, X360Remap_studio.log, known_devices.txt, known_titles.txt,
	// input_state.json, X360Remap_default.json) avait ete regroupe dans ce
	// dossier le 2026-08-02, mais ce log-ci avait ete oublie. Consequence
	// concrete signalee par l'utilisateur : en telechargeant le dossier
	// X360RemapStudio pour me l'envoyer, le fichier le plus utile au
	// diagnostic etait justement le seul absent.
	//
	// Le dossier est deja cree au demarrage du plugin (CreateDirectoryA dans
	// le point d'entree, avant tout appel a FileLog), donc aucune creation a
	// faire ici - ce qui est essentiel : cette fonction tourne sur le chemin
	// de flush, elle doit rester la plus simple possible.
	char logPath[128];
	StudioPath(logPath, sizeof(logPath), "X360Remap_plugin.log");

	// Keep the PREVIOUS session (2026-10-11, tester report: after a freeze
	// the only way out is to power off, and the next boot erased the log of
	// the session that froze). Before the first truncation of this boot, the
	// old log (its last 256 KB) is copied to X360Remap_plugin_previous.log.
	if (!g_logTruncatedThisBoot) {
		std::ifstream old(logPath, std::ios::binary);
		if (old.is_open()) {
			old.seekg(0, std::ios::end);
			std::streamoff size = old.tellg();
			const std::streamoff keep = 256 * 1024;
			std::streamoff from = size > keep ? size - keep : 0;
			if (size > 0) {
				char* buf = (char*)malloc((size_t)(size - from));
				if (buf) {
					old.seekg(from, std::ios::beg);
					old.read(buf, size - from);
					const std::streamsize got = old.gcount();
					old.close();
					char prevPath[128];
					StudioPath(prevPath, sizeof(prevPath), "X360Remap_plugin_previous.log");
					std::ofstream prev(prevPath, std::ios::binary | std::ios::trunc);
					if (prev.is_open()) prev.write(buf, got);
					free(buf);
				}
			}
		}
	}

	std::ofstream file(logPath, mode);
	if (file.is_open()) {
		file.write(g_logBuffer, len);
		g_logTruncatedThisBoot = true;
	} else {
		// Disk not ready yet (early boot): KEEP the lines for the next flush
		// (2026-10-14, HB log: the version line and everything before 4.8 s
		// were missing - they were thrown away when this write failed).
		// FileLog drops new lines if the buffer fills up meanwhile.
		return;
	}
#endif
	// Keep the lines written WHILE the disk write was running (2026-10-11,
	// tester log: every "HidAddDevice" line of the re-enumeration was missing -
	// USB callbacks logged during the flush and the old "= 0" erased them).
	{
		const int now = g_logBufferLen;
		const int extra = now > len ? now - len : 0;
		if (extra > 0) memmove(g_logBuffer, g_logBuffer + len, extra);
		g_logBufferLen = extra;
	}
}

// Une etape d'initialisation franchie : ecrite ET poussee sur le disque
// immediatement (2026-08-22). Voir le bloc de commentaire dans DllMain pour
// le raisonnement complet. A n'utiliser QUE depuis DllMain - ailleurs, le
// flush appartient au thread de polling.
static void BootStep(const char* what) {
	FileLog("%hs", what);
	FlushLogBuffer();
}

struct UsbTrb {
	DWORD endpoint;
	DWORD callback;
	DWORD savedEndpoint;
	BYTE  padding[4];
	BYTE  flags;
	BYTE  controllerIndex;   // written by UsbdQueueAsyncTransfer
	BYTE  pad2;
	BYTE  endpointIndex;     // written by UsbdQueueAsyncTransfer
	void* buffer;
	DWORD length;
};

struct UsbPacket {
	BYTE  bmRequestType;
	BYTE  bRequest;
	WORD  wValue;
	WORD  wIndex;
	WORD  wLength;
};

struct UsbControlTrb {
	UsbTrb          trb;          
	BYTE            pad[4];       
	UsbPacket  packet;  
};

struct deviceHandle;
struct __declspec(align(2)) HidControllerExtension
{
	deviceHandle* deviceHandle;
	UsbTrb interruptTrb;
	BYTE gap20[4];
	UsbControlTrb controlTrb;
	BYTE gap4C[4];
	DWORD cleanupHandler;
	BYTE gap54[24];
	DWORD queue;
	BYTE alwaysOne;
	BYTE alwaysOneTwo;
	BYTE unknownFlag;
	BYTE alwaysZero;
	BYTE cleanupDone;
	BYTE initTransferPending;
	BYTE alwaysZeroTwo;
	unsigned __int8 deviceType;
	BYTE alwaysZeroThree;
	BYTE alwaysZeroFour;

	// --- Clean unplug (2026-10-12), appended: only our code uses these ---
	// Recipe from usb-audio-x360 (TimeoutTim, GPL-3.0-or-later,
	// docs/xbox-usb-transport-contract.md, validated on retail 17559): a close
	// request is a SEPARATE object with the transfer-block layout (endpoint,
	// callback, saved endpoint, link...). We used to pass our own interruptTrb /
	// controlTrb to the close functions: the kernel then linked a live
	// transfer block into its close list and called back its old completion
	// routine - the freezes of test 1/3 and the random unplug freezes.
	UsbTrb closeReq[2];             // [0] interrupt endpoint, [1] default endpoint
	DWORD openedDataEp;             // endpoint handles saved right after opening
	DWORD openedDefaultEp;
	volatile LONG closePending;     // close callbacks still expected
	volatile DWORD closeDoneAt;     // tick when the last one arrived (0 = not yet)
};

struct deviceHandle {
	HidControllerExtension* driver;
};

// --- Liberation DIFFEREE des HidControllerExtension (2026-08-24) ---
//
// CAUSE IDENTIFIEE du gel au debranchement/rebranchement (reproduction exacte
// de HB : la souris branchee au demarrage est bien detectee, puis debrancher
// et rebrancher fige la console).
//
// HidAddDeviceHook alloue un HidControllerExtension avec new, puis y range
// DEUX blocs de transfert USB (controlTrb et interruptTrb) dont il donne
// l'adresse a la pile USB via UsbdOpenDefaultEndpoint / UsbdOpenEndpoint, et
// enfin met en file un transfert asynchrone permanent sur interruptTrb
// (UsbdQueueAsyncTransfer). Autrement dit : la pile USB du systeme conserve
// des pointeurs vers l'INTERIEUR de ce bloc memoire, et ecrit dedans a chaque
// paquet recu.
//
// Au retrait, HidRemoveDeviceHook faisait simplement "delete driver". Le bloc
// etait donc rendu a l'allocateur alors que la pile USB pointait toujours
// dessus, avec un transfert encore en file. Au rebranchement, le nouveau
// device reallouait tres probablement le meme bloc - et les deux se
// marchaient dessus. C'est un usage-apres-liberation caracterise, et il
// explique precisement pourquoi le premier branchement passe toujours (rien
// n'a encore ete libere) et pourquoi c'est le CYCLE debranchement +
// rebranchement qui gele.
//
// Deux corrections, complementaires :
//   1. Fermer les endpoints AVANT de rendre la memoire (voir
//      HidRemoveDeviceHook). Les fonctions existent et etaient resolues au
//      demarrage depuis le noyau... mais n'etaient appelees NULLE PART dans
//      tout le fichier.
//   2. Ne jamais liberer immediatement. Ces fermetures sont asynchrones (leur
//      nom commence par "Queue"), donc la pile peut encore toucher le bloc
//      pendant un court instant. Le pointeur est mis en attente ici et n'est
//      reellement libere qu'apres PENDING_FREE_DELAY_MS, depuis le thread de
//      polling. En cas de file pleine, on prefere volontairement fuir un bloc
//      de quelques dizaines d'octets plutot que de reintroduire le bug.
#define PENDING_FREE_SLOTS 8
#define PENDING_FREE_DELAY_MS 5000

struct PendingDriverFree {
	HidControllerExtension* drv;
	DWORD freeAtMs;
};
static PendingDriverFree g_pendingDriverFree[PENDING_FREE_SLOTS];

static void QueueDriverFree(HidControllerExtension* drv) {
	if (!drv) return;
	for (int i = 0; i < PENDING_FREE_SLOTS; i++) {
		if (!g_pendingDriverFree[i].drv) {
			g_pendingDriverFree[i].freeAtMs = GetTickCount() + PENDING_FREE_DELAY_MS;
			// "Ecrit en dernier" n'est garanti sur Xbox 360 qu'avec une
			// barriere (AUDIT_2026-09.md, C3).
			MemoryBarrier();
			g_pendingDriverFree[i].drv = drv;   // ecrit EN DERNIER : tant que ce
			                                    // champ est nul, l'entree est
			                                    // invisible pour le drainage.
			return;
		}
	}
	FileLog("Deferred free: queue full, driver %p deliberately not freed", drv);
}

// Same delay for the report buffer the USB stack writes packets into
// (see g_deferReportFree). Queue full = deliberately leaked, never freed early.
static void* volatile g_pendingBufferFree[PENDING_FREE_SLOTS];
static DWORD g_pendingBufferFreeAt[PENDING_FREE_SLOTS];

static void QueueBufferFree(void* buf) {
	if (!buf) return;
	for (int i = 0; i < PENDING_FREE_SLOTS; i++) {
		if (!g_pendingBufferFree[i]) {
			g_pendingBufferFreeAt[i] = GetTickCount() + PENDING_FREE_DELAY_MS;
			MemoryBarrier();
			g_pendingBufferFree[i] = buf;   // written LAST, same rule as QueueDriverFree
			return;
		}
	}
	FileLog("Deferred free: buffer queue full, report buffer %p deliberately not freed", buf);
}

// Appelee uniquement depuis MappingManagerThreadProc (contexte sur), jamais
// depuis un callback USB.
static void DrainDriverFreeQueue() {
	DWORD now = GetTickCount();
	for (int i = 0; i < PENDING_FREE_SLOTS; i++) {
		HidControllerExtension* drv = g_pendingDriverFree[i].drv;
		if (!drv) continue;
		MemoryBarrier();   // lecture de freeAtMs APRES celle de drv (AUDIT C3)
		// Comparaison signee : resiste au debordement de GetTickCount().
		if ((int)(now - g_pendingDriverFree[i].freeAtMs) < 0) continue;
		// Clean unplug: never free while the kernel still owes us a close
		// callback, and leave it 1 s more after the last one (usb-audio-x360's
		// settle time). No answer after 30 s: leak it on purpose, never free early.
		if (drv->closePending > 0) {
			if ((int)(now - g_pendingDriverFree[i].freeAtMs) > 30000) {
				g_pendingDriverFree[i].drv = nullptr;
				FileLog("Clean unplug: close callbacks missing after 30 s - driver %p kept (not freed)", drv);
			}
			continue;
		}
		if (drv->closeDoneAt && (int)(now - drv->closeDoneAt) < 1000) continue;
		g_pendingDriverFree[i].drv = nullptr;
		if (drv->closeDoneAt) FileLog("Clean unplug: all close callbacks received, driver %p freed", drv);
		delete drv;
	}
	for (int i = 0; i < PENDING_FREE_SLOTS; i++) {
		void* buf = g_pendingBufferFree[i];
		if (!buf) continue;
		MemoryBarrier();
		if ((int)(now - g_pendingBufferFreeAt[i]) < 0) continue;
		g_pendingBufferFree[i] = nullptr;
		free(buf);
	}
}

typedef struct _XINPUT_CAPABILITIESEX
{
	BYTE                                Type;
	BYTE                                SubType;
	WORD                                Flags;
	XINPUT_GAMEPAD                      Gamepad;
	XINPUT_VIBRATION                    Vibration;
	DWORD unk1;
	DWORD unk2;
	DWORD unk3;
} XINPUT_CAPABILITIES_EX, * PXINPUT_CAPABILITIES_EX;

enum InitState
{
	INIT_SET_CONFIGURATION,
	INIT_GET_HID_DESCRIPTOR,
	INIT_GET_REPORT_DESCRIPTOR,
	INIT_DONE,
	INIT_FAILED
};

InitState g_InitState;

// Re-entrancy guard (2026-07-31) - see PROJECT_NOTES.md "Gel au branchement -
// réentrance de l'init USB". g_InitState/c/globalIndex/hidDescriptorBuffer/
// reportDescriptorBuffer are all single global variables, not per-device -
// they assume only one device is ever being initialised at a time. HidAddDeviceHook
// kicks off an async, multi-stage SET_CONFIGURATION/GET_DESCRIPTOR chain
// (setConfigurationComplete) and returns immediately without waiting for it,
// which means the underlying USB stack is free to call HidAddDeviceHook again
// for a second device before the first one's chain has finished - observed on
// real hardware with a composite-interface keyboard exposing two HID
// interfaces 24ms apart: the log shows both "HidAddDevice" lines but neither
// ever reaches "Parsed descriptor", then the console freezes solid. The
// second call was almost certainly clobbering the first device's still
// in-flight g_InitState/hidDescriptorBuffer/reportDescriptorBuffer out from
// under it. This flag makes a second concurrent device defer to the original
// (unmodified) handler instead of touching the shared state while another
// device's init is still in progress - same safe fallback already used for
// "no free index".
volatile bool g_deviceInitBusy = false;
// Timestamp of when g_deviceInitBusy last became true (GetTickCount(), ms
// since boot). Used by a watchdog in MappingManagerThreadProc: if a single
// device's own async chain ever silently stalls (distinct from the
// concurrency race above - e.g. a SendControlRequest that never completes),
// this guard would otherwise stay stuck forever and lock out every future
// device. Not observed on hardware, but this fix should not trade one class
// of permanent lockup for another.
volatile DWORD g_deviceInitBusySince = 0;

// Boot-time USB reset window (2026-07-31) - see PROJECT_NOTES.md "Gel de
// l'animation de boot".
//
// DllMain deliberately resets the whole USB driver
// (UsbdPowerDownNotification + MmFreePhysicalMemory + UsbdDriverEntry) so
// devices already plugged in get re-enumerated without a manual replug. Every
// such device is therefore enumerated from INSIDE UsbdDriverEntry(), i.e.
// while the USB driver is still initialising itself, and our hook responds by
// immediately issuing control transfers (UsbdOpenDefaultEndpoint +
// SendControlRequest) back into that very driver. Claiming a device in that
// window reliably freezes the console at the boot animation, confirmed on
// hardware with a mouse and with a mouse+keyboard.
//
// Note the polling thread (MappingManagerThreadProc) is also only started
// AFTER this reset, so during this window there is no watchdog and no log
// flush either - which is exactly why the file log was always empty for these
// freezes.
//
// While this flag is set, devices are handed straight to the original handler
// untouched. The practical cost is nil: devices connected at boot simply need
// one unplug/replug to be picked up, which is already the established workflow
// for getting a mouse working inside a game anyway.
// IMPORTANT (2026-07-31, révision 2): this must stay set for a DURATION, not
// just for the UsbdDriverEntry() call itself. USB enumeration is asynchronous -
// UsbdDriverEntry() only starts the driver, and the devices are actually
// enumerated slightly later via interrupts/DPCs, typically after DllMain has
// already returned. Clearing the flag right after UsbdDriverEntry() (revision
// 1) closed the window before any device ever arrived, so the guard protected
// nothing and the boot freeze persisted unchanged. The window is now closed by
// MappingManagerThreadProc once BOOT_CLAIM_BLACKOUT_MS have elapsed since the
// plugin loaded.
volatile bool g_bootUsbResetInProgress = false;
DWORD g_bootBlackoutStartedAt = 0;

// Aucune configuration trouvee au demarrage (2026-09-13). Pose dans DllMain,
// consomme par MappingManagerThreadProc une fois le dashboard pret.
//
// POURQUOI PAS NOTIFIER DIRECTEMENT DANS DllMain : a ce moment-la le tableau
// de bord n'est pas encore affiche, la notification serait au mieux invisible.
// Et surtout, XNotifyUI appele depuis un mauvais contexte a DEJA gele cette
// console par le passe (voir Controller::mouseNotified et g_notifyUiSafe) -
// toutes les autres notifications du plugin partent du thread de polling, on
// ne fait pas d'exception ici.
volatile bool g_installIncomplete = false;

// Le fichier de configuration existe mais n'a pas pu etre lu (syntaxe). Cas
// distinct du precedent : inutile - et trompeur - de dire a quelqu'un
// d'installer une application qu'il a deja.
volatile bool g_configUnreadable = false;
// Plugin loads ~4.6s after power-on (observed in logs); devices are manually
// plugged much later (16s+ in every capture so far), so a 10s blackout covers
// boot enumeration without interfering with normal hot-plugging.
#define BOOT_CLAIM_BLACKOUT_MS 10000
// Boot freezes of 2026-11-09 (3 logs, with and without the VdSwap hook, with and
// without NetIsoCapture): every frozen boot stopped within a second of the end of
// the blackout, which re-reads every setting and loaded X360Remap.json a second
// time, while Aurora was making its first XamInput calls through our hooks. The
// end of the blackout now waits until Aurora has been reading the pads for 8 s
// (or 45 s after load if it never does), out of its start-up window.
#define BOOT_SETTLE_AFTER_INPUT_MS 8000
#define BOOT_SETTLE_MAX_MS 45000
// 2026-11-12: the freeze followed that 8 s timer (15 s, then 22 s and 25.8 s), always while
// Aurora was still loading (6 to 14 frames/s). The end of the blackout (a burst of file reads)
// now waits until the dashboard title has been up for 13 s, when Aurora is idle at 59 frames/s,
// and the USB reset follows 3 s later. FREEZE_STATIC_ANALYSIS.md.
#define BOOT_SETTLE_TITLE_MS 13000
extern DWORD g_titleStableSince;
static DWORD g_blackoutEndedAt = 0;
volatile DWORD g_firstXamInputTick = 0;   // set once by the XamInput hooks (a plain store)

#define USB_ENDPOINT_TYPE_CONTROL     0x00
#define USB_ENDPOINT_TYPE_ISOCHRONOUS 0x01
#define USB_ENDPOINT_TYPE_BULK        0x02
#define USB_ENDPOINT_TYPE_INTERRUPT   0x03
#define USB_DIRECTION_IN  1
#define USB_DIRECTION_OUT 0

uint16_t clamp_u16(uint16_t val, uint16_t lo, uint16_t hi) {
	if (val < lo) return lo;
	if (val > hi) return hi;
	return val;
}

// Nintendo specific start
const uint16_t NINTENDO_VENDOR_ID = 0x057E;
const uint16_t SWITCH_PRO_PRODUCT_ID = 0x2009;

const unsigned char nintendo_handshake[2] = { 0x80, 0x02 };
const unsigned char hid_only_mode[2] = { 0x80, 0x04 };

#pragma pack(push, 1)
struct switch_pro_input_report {
	uint8_t  timer;
	uint8_t  battery_conn;   // upper nibble = battery, lower = connection type
	uint8_t  buttons_right;  // Y X B A, R_SR, R_SL, R, ZR
	uint8_t  buttons_mid;    // minus, plus, R_stick, L_stick, home, capture
	uint8_t  buttons_left;   // dpad down/up/right/left, L_SR, L_SL, L, ZL
	uint8_t  left_stick[3];  // 12-bit packed: lx in bits [11:0], ly in bits [23:12]
	uint8_t  right_stick[3]; // same packing for rx, ry
	uint8_t  vibrator;
	uint8_t  imu[48];        // 3 � (accel xyz + gyro xyz), each int16_t
};
#pragma pack(pop)

// buttons1
// Face buttons
#define SWITCH_BTN_Y        (1 << 1)
#define SWITCH_BTN_X        (1 << 0)
#define SWITCH_BTN_B        (1 << 2)
#define SWITCH_BTN_A        (1 << 3)

// Right shoulder cluster
#define SWITCH_BTN_R        (1 << 6)
#define SWITCH_BTN_ZR       (1 << 7)

// System buttons
#define SWITCH_BTN_MINUS    (1 << 8)
#define SWITCH_BTN_PLUS     (1 << 9)

// Sticks
#define SWITCH_BTN_R_STICK  (1 << 10)
#define SWITCH_BTN_L_STICK  (1 << 11)

// System
#define SWITCH_BTN_HOME     (1 << 12)
#define SWITCH_BTN_CAPTURE  (1 << 13)

// buttons2

#define SWITCH_DPAD_DOWN    (1 << 0)
#define SWITCH_DPAD_UP      (1 << 1)
#define SWITCH_DPAD_RIGHT   (1 << 2)
#define SWITCH_DPAD_LEFT    (1 << 3)

#define SWITCH_BTN_L        (1 << 6)
#define SWITCH_BTN_ZL       (1 << 7)

static uint16_t STICK_MIN = 500;
static uint16_t STICK_MAX = 3500;
static uint16_t STICK_CENTER = 2000;

int16_t normalize_stick(uint16_t raw) {
	raw = clamp_u16(raw, STICK_MIN, STICK_MAX);
	if (raw >= STICK_CENTER) {
		return (int16_t)((int32_t)(raw - STICK_CENTER) * 32767 / (STICK_CENTER - STICK_MIN));
	}
	else {
		return (int16_t)((int32_t)(STICK_CENTER - raw) * -32768 / (STICK_CENTER - STICK_MIN));
	}
};

bool NeedsNintendoHandshake(uint16_t vid, uint16_t pid) {
	if (vid != NINTENDO_VENDOR_ID) return false;
	return pid == SWITCH_PRO_PRODUCT_ID;
}


// nintendo specific end

// dualshock 3 specific start
const uint16_t SONY_VENDOR_ID = 0x054C;
const uint16_t DS3_PRODUCT_ID = 0x0268;
const unsigned char DS3_HANDSHAKE[4] = { 0x42, 0x0C, 0x00, 0x00 };

bool NeedsDualshock3Handshake(uint16_t vid, uint16_t pid) {
	if (vid != SONY_VENDOR_ID) return false;
	return pid == DS3_PRODUCT_ID;
}

enum DS3_FEATURE_VALUE
{
	Ds3FeatureDeviceAddress = 0x03F2,
	Ds3FeatureStartDevice = 0x03F4,
	Ds3FeatureHostAddress = 0x03F5

};
// dualshock 3 specific end

typedef usb_device_descriptor* (*usb_device_descriptor_func_t)(deviceHandle* handle);
typedef usb_interface_descriptor* (*usb_interface_descriptor_func_t)(deviceHandle* handle);
typedef int(*usb_add_device_complete_func_t)(deviceHandle* handle, int status_code);
typedef int(*usb_get_device_speed_func_t)(deviceHandle* handle);
typedef int(*usb_queue_async_transfer_func_t)(deviceHandle* handle, void* endpoint);
typedef NTSTATUS(*usb_queue_close_endpoint_func_t)(deviceHandle* handle, void* endpoint);
typedef NTSTATUS(*usb_remove_device_complete_func_t)(deviceHandle* handle);
typedef NTSTATUS(*usb_close_default_endpoint_func_t)(deviceHandle* handle, DWORD* endpoint);
typedef NTSTATUS(*usb_open_default_endpoint_func_t)(deviceHandle* handle, DWORD* endpoint);
typedef NTSTATUS(*usb_open_endpoint_func_t)(deviceHandle* handle, int transfertype, int endpointAddress, int maxPacketLength, int interval, DWORD* endpoint);
typedef usb_endpoint_descriptor* (*usb_endpoint_descriptor_func_t)(deviceHandle* handle, int index, int transfertype, int direction);
typedef int(*xam_user_bind_device_callback_func_t)(unsigned int controllerId, unsigned int context, unsigned __int8 category, bool disconnect, unsigned __int8* userIndex);
typedef int(*usbd_powerdown_notification_func_t)();
typedef void(*mm_free_physical_memory_func_t)(DWORD type, DWORD address);

usb_device_descriptor_func_t UsbdGetDeviceDescriptor = nullptr;
usb_interface_descriptor_func_t UsbdGetInterfaceDescriptor = nullptr;
usb_endpoint_descriptor_func_t UsbdGetEndpointDescriptor = nullptr;
usb_add_device_complete_func_t UsbdAddDeviceComplete = nullptr;
usb_open_default_endpoint_func_t UsbdOpenDefaultEndpoint = nullptr;
usb_open_endpoint_func_t UsbdOpenEndpoint = nullptr;
usb_get_device_speed_func_t UsbdGetDeviceSpeed = nullptr;
usb_queue_async_transfer_func_t UsbdQueueAsyncTransfer = nullptr;
usb_queue_close_endpoint_func_t UsbdQueueCloseEndpoint = nullptr;
usb_close_default_endpoint_func_t UsbdQueueCloseDefaultEndpoint = nullptr;
usb_remove_device_complete_func_t UsbdRemoveDeviceComplete = nullptr;
xam_user_bind_device_callback_func_t XamUserBindDeviceCallback = nullptr;
usbd_powerdown_notification_func_t UsbdPowerDownNotification = nullptr;
usbd_powerdown_notification_func_t UsbdDriverEntry = nullptr;
mm_free_physical_memory_func_t MmFreePhysicalMemory = nullptr;

// Forward declaration - actually defined later in the file alongside
// initFunctionPointers(), but MappingManagerThreadProc (defined earlier) needs
// it for the title-switch USB reset.
extern DWORD UsbPhysicalPage;

enum NINTENDO_HANDSHAKE_STATE {
	INITIAL,
	HANDSHAKE,
	DONE
};
struct Controller {
	deviceHandle* deviceHandle;
	HidControllerExtension* controllerDriver;
	ButtonsReport currentState;
	MacroPlay macroPlay[MAX_MACROS];   // game thread only (XInputdReadStateHook)
	bool padListed;                    // USB pad written to known_devices.txt (polling thread)
	uint8_t userIndex;
	uint32_t deviceContext;
	uint16_t vendorId;
	uint16_t productId;
	uint32_t packetNumber;
	HID_ReportInfo_t* reportInfo;
	uint8_t reportId;
	void* reportData;
	const HidDeviceMapping* map;

	// for nintendo specific handshake
	NINTENDO_HANDSHAKE_STATE nintendo_handshake_state;
	UsbTrb interruptTrb;

	// Relative-motion HID device support (mouse/trackball), see DetectIsMouse().
	// Deltas are accumulated here at the HID interrupt rate and drained once per
	// XInput poll in XInputdReadStateHook, so a poll with no new report reads back
	// zero motion instead of re-sending a stale non-zero delta.
	bool isMouse;
	volatile int32_t mouseAccumX;
	volatile int32_t mouseAccumY;
	// Scroll wheel, same accumulate-then-drain-on-poll pattern as X/Y. Positive
	// = scrolled forward/up, negative = scrolled backward/down (per HID convention).
	volatile int32_t mouseWheelAccum;

	// Etirement de l'impulsion molette (2026-08-03). Un cran de molette
	// n'existait que le temps d'UN SEUL appel XInput : l'accumulateur est
	// draine puis remis a zero dans XInputdReadStateHook, donc le bouton
	// resultant (D-Pad par defaut) n'etait arme que pour ce poll-la. Un
	// dashboard ou un jeu qui echantillonne l'entree moins vite que la
	// cadence de poll pouvait donc rater l'impulsion entierement - cause
	// probable du "la molette ne fait rien dans Aurora" alors que le reglage
	// etait bien lu. Ces deux champs maintiennent le sens capture pendant
	// WHEEL_PULSE_MS millisecondes, quel que soit le nombre de polls qui
	// tombent dans cette fenetre. Ecrits/lus uniquement depuis
	// XInputdReadStateHook (jamais depuis le contexte USB), donc non volatile
	// - contrairement aux accumulateurs juste au-dessus.
	int32_t wheelPulseDir;      // -1 arriere, 0 aucune impulsion en cours, +1 avant
	DWORD   wheelPulseUntilMs;  // GetTickCount() au-dela duquel l'impulsion expire

	// Sens du dernier cran de molette, POUR LA LIAISON RAPIDE uniquement
	// (2026-08-04). Accumulateur separe et volontairement redondant avec
	// mouseWheelAccum : ce dernier est draine par le thread du JEU
	// (XInputdReadStateHook), donc le thread de polling - celui qui gere la
	// liaison rapide - ne voit jamais passer un mouvement de molette. Sans ce
	// champ, impossible d'utiliser la molette comme SOURCE d'une liaison :
	// l'utilisateur armait, tournait, et la liaison expirait faute de source
	// detectee. Ecrit depuis le contexte USB (simple affectation), lu et remis
	// a zero par le thread de polling.
	volatile int32_t bindWheelDir; // +1 avant, -1 arriere, 0 rien depuis la derniere lecture

	// Molette pour le DEFILEMENT de l'interface d'application.xex (2026-08-09,
	// demande utilisateur : "je veux avoir la possibilite d'utiliser la
	// molette pour la side bar de defilement, je vais pas cliquer sur les
	// fleches"). Encore un accumulateur separe et redondant avec
	// mouseWheelAccum/bindWheelDir, pour la meme raison structurelle que
	// bindWheelDir ci-dessus : mouseWheelAccum est draine par le thread du
	// jeu, bindWheelDir par la liaison rapide (thread de polling) - aucun des
	// deux n'est jamais lu par le thread qui ecrit input_state.json (voir la
	// boucle MarkPollPhase(5) plus bas), donc application.xex n'avait
	// litteralement aucune donnee de molette a lire pour faire defiler ses
	// propres listes. Ecrit depuis le contexte USB (simple affectation), lu
	// ET REMIS A ZERO par le thread de polling au moment d'ecrire
	// input_state.json - accumule (pas juste la derniere direction) pour ne
	// perdre aucun cran si plusieurs surviennent entre deux ecritures.
	volatile int32_t uiWheelAccum;

	// --- Descripteur de rapport HID, recupere PAR PERIPHERIQUE (2026-08-03) --
	// Le projet possede deja un parseur complet (USB_ProcessHIDReport, derive
	// de LUFA) et HidFillMouseState sait lire X, Y ET la molette a partir de
	// son resultat, quelle que soit la disposition du paquet. Les souris n'en
	// profitaient pas : elles sont routees vers le chemin rapide "boot", qui
	// met reportInfo a nullptr et ne demande jamais le descripteur.
	//
	// Ce detour existait parce que la chaine d'init d'origine s'appuie sur des
	// GLOBALES partagees (g_InitState, hidDescriptorBuffer,
	// reportDescriptorBuffer) : deux peripheriques s'enumerant a ~30 ms
	// d'intervalle - exactement ce que fait un clavier composite - se
	// marchaient dessus et gelaient la console. Le gel venait donc de l'etat
	// partage, PAS de la requete de controle elle-meme : le SET_PROTOCOL
	// ajoute plus tot dans la journee emet une requete depuis ce meme chemin
	// rapide, avec le controlTrb propre au device, et n'a jamais rien gele.
	//
	// D'ou ces champs : tout est ici, dans le Controller, donc rien a partager
	// et aucune course possible entre deux peripheriques.
	uint8_t  interfaceNumber;          // requis dans wIndex des requetes GET_DESCRIPTOR adressees a l'interface
	usb_hid_descriptor hidDesc;        // etape 1 : donne la longueur du descripteur de rapport
	uint8_t  reportDescBuf[512];       // etape 2 : le descripteur lui-meme
	uint16_t reportDescLen;            // longueur demandee/recue
	volatile bool reportDescReady;     // pose par le callback USB, lu par le thread de polling
	volatile bool reportDescFailed;    // une des deux etapes a echoue -> on reste en mode boot
	volatile bool reportProtocolRequested; // SET_PROTOCOL(report) sent (descriptor uses Report IDs)
	bool     reportDescParsed;         // deja traite par le thread de polling (une seule fois)
	DWORD    reportDescRequestedMs;    // pour le delai de repli

	// Nom lisible du peripherique, declare par lui-meme (descripteur de chaine
	// USB a l'index iProduct). Ajoute le 2026-08-03 sur remarque de
	// l'utilisateur : on interroge deja le peripherique pour savoir lire ses
	// donnees, lui afficher un VID:PID quand il veut juste choisir sa souris
	// n'a aucun sens. Meme mecanique que le descripteur de rapport : requete
	// fire-and-forget, callback qui ne fait que poser un drapeau, conversion
	// depuis le thread de polling.
	uint8_t  productStringIndex;       // 0 = le peripherique n'en declare pas
	uint8_t  nameBuf[128];             // brut UTF-16LE tel que recu
	volatile bool nameReady;
	volatile bool nameParsed;
	char     productName[32];          // resultat converti, chaine vide si indisponible

	// Set once the "mouse detected" notification has been sent, so the polling
	// thread (MappingManagerThreadProc) fires it exactly once per device. Do NOT
	// call XNotifyUI directly from the USB completion-callback chain (e.g.
	// setConfigurationComplete) - that runs in a constrained context that is not
	// safe for it and caused a hard system freeze when tried.
	bool mouseNotified;

	// Keyboard support (2026-07-31, detection-only stage - see PROJECT_NOTES.md
	// "Chantier clavier"). Classified the same way as isMouse: broadly accepted
	// at the USB level (any HID-class interface), then precisely classified from
	// the parsed report descriptor via DetectIsKeyboard(). No key state parsing
	// yet - this stage only identifies the device and keeps it out of the
	// gamepad mapping assistant (which would never find a match on it, same
	// reasoning as for isMouse).
	bool isKeyboard;
	bool keyboardNotified;

	// Boot-protocol devices (mouse/keyboard) take a short, fully synchronous
	// init path and have a fixed packet layout, so reportInfo stays null for
	// them and must never be dereferenced - check this flag first when
	// dispatching a report. See PROJECT_NOTES.md "chemin court".
	bool isBootProtocol;
	uint16_t reportSize;   // interrupt endpoint packet size, to know if a mouse sends a wheel byte

	// Extra keyboard interface of a gaming keyboard / keypad (2026-10-11).
	// Razer (Tartarus V2 1532:022B: interface 0 = boot keyboard, 8 bytes;
	// interface 1 = keyboard, NO boot subclass, 16 bytes; interface 2 = mouse),
	// Corsair, Logitech, SteelSeries... expose a second "keyboard" interface
	// that sends N-key-rollover reports (report ID + one BIT per key). Its
	// layout is NOT the 8-byte boot layout: decoding it as boot gave wrong
	// keys, a phantom Ctrl (the report ID byte read as modifiers) and
	// overwrote the good state of interface 0 on the same player.
	// Fix: the boot interface is put in boot protocol (SET_PROTOCOL, what the
	// Xbox dashboard, a PC BIOS and KVM switches do - the device then sends
	// every key there), and the extra interface is kept claimed but never
	// decoded. Its first reports are logged in hex so a new layout can be
	// studied from a tester's log.
	bool isExtraKeyboardIf;
	uint8_t extraIfLogged;
	// Raw key reports captured in the USB callback, LOGGED BY THE POLLING
	// THREAD (2026-10-12: calling FileLog / _snprintf from the USB completion
	// froze the console after 1-3 key presses - CRT code is not safe there).
	uint8_t keyRaw[8][16];
	volatile uint8_t keyRawCount;     // written by the USB callback only
	uint8_t keyRawLogged;             // read/written by the polling thread only

	// --- Keyboard+mouse fusion (2026-07-31) ---
	// All boot-protocol devices share ONE XAM registration, so the system sees
	// a single player instead of one per device. Each still needs its own
	// Controller entry (own USB endpoint, own report buffer), but they all carry
	// the same deviceContext/userIndex and their states are merged at poll time
	// in XInputdReadStateHook.
	// ownsXamBinding marks the one that actually performed the registration;
	// xamBindIndex is the index the binding was made with, which every member
	// remembers so the binding can still be released if the owner is unplugged
	// first.
	bool ownsXamBinding;
	int  xamBindIndex;

	// --- Live input snapshot for the config assistant (application.xex) ---
	// Plain fields, written from the raw USB completion-callback context
	// exactly like mouseAccumX/mouseWheelAccum above (cheap integer writes
	// only - no file I/O, no XNotifyUI, same rule as everywhere else in this
	// file). 0xFF means "nothing currently pressed". Read and written out to
	// HDD:\input_state.json only from the safe polling thread
	// (MappingManagerThreadProc), never from here directly - see
	// ARCHITECTURE.md for why application.xex needs this instead of reading
	// the USB device itself.
	volatile uint8_t lastKeyCode;
	volatile uint8_t lastModifiers;   // HID modifier byte (Ctrl/Shift/Alt/Win), for the Win+Shift+X switch
	volatile uint8_t lastMouseButtonIdx;
	// Full raw button bitmask (bit0=left/bit1=right/bit2=middle/... per HID
	// boot-mouse convention), added 2026-08-01 for application.xex's mouse
	// cursor (see PROJECT_NOTES.md "Jalon 4") - lastMouseButtonIdx above only
	// reports the single lowest-numbered held button, which is enough for the
	// wizard's "press a button to bind" flow but not for "is left held" while
	// other buttons might also be down. Same write-site as lastMouseButtonIdx.
	volatile uint8_t lastMouseButtonsMask;
	// Cursor-dedicated motion accumulator, separate from mouseAccumX/Y above
	// on purpose: mouseAccumX/Y are drained once per XInput poll in
	// XInputdReadStateHook to feed the right stick emulation - if the config
	// app's JSON writer also drained THAT SAME accumulator, the two consumers
	// would race and steal motion from each other (stick would stutter while
	// the app is open, cursor would stutter otherwise). This pair is filled at
	// the same HID interrupt sites as mouseAccumX/Y but drained independently,
	// only by the input_state.json writer below - same accumulate-then-drain
	// pattern, just a second independent accumulator for a second consumer.
	volatile int32_t cursorAccumX;
	volatile int32_t cursorAccumY;

	// Inertie souris->stick (2026-08-02, demande utilisateur) - lue/ecrite
	// UNIQUEMENT par XInputdReadStateHook (le thread de poll XInput), jamais
	// par un callback USB, donc pas besoin de "volatile" contrairement aux
	// accumulateurs ci-dessus. Voir le commentaire complet a l'endroit ou
	// c'est utilise, plus bas dans ce fichier.
	float smoothedVelX;
	float smoothedVelY;
	DWORD lastVelocityUpdateMs;

	// Report du mouvement qui depasse le stick (2026-09-23, voir
	// ApplyOverflowCarry pres de XInputdReadStateHook). Unites : valeur de
	// stick x lecture XInput. Lu/ecrit uniquement par le thread du jeu.
	int32_t carryX;
	int32_t carryY;

	// Reste fractionnaire de la normalisation de resolution (voir
	// kReferenceCountsPerCm) : sans lui, les petits mouvements d'une souris a
	// haute resolution seraient arrondis a zero. Thread du jeu uniquement.
	float normFracX;
	float normFracY;

	// Lissage temporel (2026-09-26, voir HidDeviceMapping::smoothingMs).
	// Thread du jeu uniquement. Remis a zero avec le reste au retrait.
	SmoothingState smooth;
	SmoothingState smoothAim;   // direct aim smoothing (directAimSmoothingMs), separate from the stick's
	GimxState gimx;             // GIMX method state (1.4.2), game thread only
	DWORD gimxLastMs;           // time of the previous GIMX poll
	float adsFactor;            // current direct-aim speed factor (1 = hip), ramps to adsSensitivity
	DWORD adsLastMs;
} __declspec(align(4));

struct MappingState {
	volatile bool active;
	volatile uint8_t pressedButtonIdx;
	volatile int16_t axisValues[6];
	uint8_t reportId;
	HID_ReportInfo_t* reportInfo;
	int controllerIndex;
	uint8_t availableButtons[256];
	uint8_t availableButtonCount;
	volatile uint8_t previousPressedButtonIdx;
	volatile uint32_t holdCount;
} __declspec(align(4));

Controller connectedControllers[4];
Controller c;

// Title-switch detection (see MappingManagerThreadProc). A device's XAM
// controller binding (XamUserBindDeviceCallback) does not reliably survive a
// title switch even though the driver's hooks stay resident - confirmed on
// real hardware: a mouse bound while under the dashboard stopped affecting
// input once inside a game, until physically unplugged and replugged. 0 means
// "not yet observed" so the very first read after boot doesn't trigger a
// spurious reset.
DWORD g_lastTitleId = 0;

// Debounce du retour a 0 de XamGetCurrentTitleId() (2026-08-08, suite directe
// du fix "profil fige apres sortie du jeu" plus bas dans ce fichier). Ce fix
// a rendu le retour a g_lastTitleId=0 immediat des la premiere lecture a 0 -
// correct pour un vrai retour au dashboard, mais un signalement utilisateur
// ("au lieu de la liaison rapide avec Back+Start, ça declenche la capture
// d'ecran d'Aurora") suggere que XamGetCurrentTitleId() peut repondre 0 de
// facon transitoire en PLEINE PARTIE - vraisemblablement au moment precis ou
// Aurora reagit elle-meme a Back+Start maintenus (son propre raccourci de
// capture d'ecran semble partager ce geste). Un seul sondage a 0 pendant le
// maintien suffisait alors a desarmer notre liaison rapide (garde
// g_lastTitleId != 0, voir plus bas) juste avant le seuil des 1.5s, laissant
// Aurora seule a reagir. On exige donc desormais 0 en continu pendant
// TITLE_ZERO_DEBOUNCE_MS avant de commiter la transition vers "hors jeu" -
// un retour reel au dashboard reste detecte tres vite (l'utilisateur y reste
// largement plus longtemps que ça), mais un blip d'une ou deux iterations de
// poll (~100-200ms) ne desarme plus rien. Les transitions vers un titleId
// NON NUL restent, elles, commitees immediatement (une vraie lecture de jeu
// ne "clignote" pas vers une mauvaise valeur, contrairement a 0).
DWORD g_titleZeroCandidateSince = 0; // 0 = aucun candidat "retour dashboard" en cours
const DWORD TITLE_ZERO_DEBOUNCE_MS = 1500;

// Raccourci "sauvegarder les reglages actuels comme profil du jeu en cours"
// (Jalon 7, suite - demande utilisateur du 2026-08-02 : "pourquoi ne pas se
// servir au mieux" du clavier, eviter l'aller-retour vers l'appli). F9 (code
// HID 0x42) choisi car libre par defaut - aucune touche de la disposition
// par defaut (WASD/IJKL/fleches/Espace/C/R/E/Q/F/V/Maj/Ctrl/Alt/Echap/Tab/
// Retour, revue le 2026-09-13 - voir HidFillBootKeyboardState et README.md)
// ne l'utilise. Verifie/incremente une fois par iteration de
// MappingManagerThreadProc (~100ms/tick, meme cadence que le reste de cette
// boucle) - SAVE_PROFILE_HOLD_TICKS*100ms de maintien avant de declencher,
// meme principe que le hold-to-skip du wizard, pour eviter un declenchement
// accidentel sur un appui rapide en jeu. Lus/ecrits uniquement depuis ce
// thread (comme g_lastTitleId juste au-dessus), pas volatile.
const uint8_t SAVE_PROFILE_HOTKEY_CODE = 0x42; // F9
const int SAVE_PROFILE_HOLD_TICKS = 15;        // ~1.5s a 100ms/tick
int  g_saveProfileHoldTicks = 0;
bool g_saveProfileTriggered = false; // empeche un nouveau declenchement tant que la touche reste enfoncee

// --- REGLAGES EN JEU (2026-09-25, demande HB) --------------------------------
// "Le va-et-vient du jeu a l'application est fastidieux, peu d'utilisateurs
// feront cet effort." On regle donc PENDANT la partie, au clavier :
//   F5 / F6   sensibilite      -/+ 500   (1000..30000)
//   F7 / F8   anti-zone morte  -/+ 500   (0..12000)
//   F10 / F11 zone morte       -/+ 250   (0..10000)
// Touches libres dans la disposition par defaut, a cote de F9. Maintenir =
// repetition. La valeur s'affiche (notification, une seule quand on relache),
// et s'applique TOUT DE SUITE via les surcharges ci-dessous. Elle est
// enregistree dans le PROFIL DU JEU 2,5 s apres le dernier appui - meme
// chemin que F9 (DetachControllerMaps / SetMouseProfile / Save / Rebind),
// jamais depuis un callback USB. Tout se passe dans MappingManagerThreadProc.
//
// Surcharges lues par XInputdReadStateHook (autre thread) : de simples int32
// alignes, ecrits en une instruction - meme tolerance que le reste de ce
// fichier. g_liveTitle est ecrit EN DERNIER (barriere) : tant qu'il vaut 0 ou
// un autre jeu, les surcharges sont ignorees.
#define EN_LIVE_TUNING 1
const uint8_t LIVE_KEY_SENS_DOWN = 0x3E, LIVE_KEY_SENS_UP = 0x3F;   // F5, F6
const uint8_t LIVE_KEY_ADZ_DOWN  = 0x40, LIVE_KEY_ADZ_UP  = 0x41;   // F7, F8
const uint8_t LIVE_KEY_DZ_DOWN   = 0x43, LIVE_KEY_DZ_UP   = 0x44;   // F10, F11
const uint8_t LIVE_KEY_ADZY_DOWN = 0x3C, LIVE_KEY_ADZY_UP = 0x3D;   // F3, F4 : anti-zone morte verticale (2026-09-26)
const uint8_t LIVE_KEY_SM_DOWN   = 0x3A, LIVE_KEY_SM_UP   = 0x3B;   // F1, F2 : lissage (2026-09-26)
const DWORD LIVE_NOTIFY_AFTER_MS = 350;    // notification quand on arrete d'appuyer
const DWORD LIVE_SAVE_AFTER_MS   = 2500;   // enregistrement dans le profil du jeu
volatile int32_t  g_liveSens = 0;          // 0 = pas de surcharge
volatile int32_t  g_liveDz   = -1;         // -1 = pas de surcharge
volatile int32_t  g_liveAdz  = -1;         // -1 = pas de surcharge
volatile int32_t  g_liveAdzY = -1;         // -1 = pas de surcharge
volatile int32_t  g_liveSm   = -1;         // -1 = pas de surcharge
volatile uint32_t g_liveTitle = 0;         // jeu concerne (0 = aucune surcharge active)

// --- On/off switch: Win + Shift + X (2026-10-08, HB) ------------------------
// Pauses X360Remap WITHOUT unplugging anything: keyboard and mouse stop
// driving player 1, and the REAL controller's input is copied into player 1
// instead (the virtual controller keeps its slot, so the game - which only
// listens to player 1 - now hears the pad). Same keys again: back on.
// Read in the polling thread from the keyboard's HID modifier byte.
volatile bool g_remapPaused = false;
const uint8_t PAUSE_KEY_X = 0x1B;           // HID usage of X
// Same combo as Win+Shift+X (rgdshock, issue 7): D = direct aim off / on for the
// running game (back on at the next game), R = reload X360Remap.json and the
// learned cameras, for when the mouse starts acting like the left stick.
const uint8_t AIM_KEY_D = 0x07, RELOAD_KEY_R = 0x15;
static volatile bool g_directAimOff = false;
// Last state of the real controller, refreshed (1) by the game itself each
// time it reads the pad (end of XInputdReadStateHook) and (2) every 100 ms by
// the polling thread. Never read with XInputGetState from inside the hook:
// the game may already hold XInput's lock there.
XINPUT_GAMEPAD    g_realPad;
volatile DWORD    g_realPadTick = 0;
// Separate players: the pad that held player 1 before the keyboard / mouse took
// its place. Written where player 1 is read, played back on the keyboard / mouse
// slot (which becomes that pad's player).
XINPUT_GAMEPAD    g_displacedPad;

// Merged players (see g_playersSeparate). Set while the real-controller
// branch of XInputdReadStateHook reads the keyboard / mouse slot itself.
volatile bool     g_mergeInternalRead = false;
volatile DWORD    g_mergeDeliveredTick = 0; // last time the pad received the keyboard / mouse
// Last time the keyboard / mouse slot (player 1) received the real pad's input
// (merged players, pad on another slot). The pad's own slot then rests, or
// Aurora / games see every pad press twice (2026-10-29, HB: the D-pad skipped
// one tile in Aurora when the Rapoo dongle took player 1 before the pad).
volatile DWORD    g_padIntoKbmTick = 0;
// Guide button asked by a keyboard / mouse key: user index + 1, 0 = none.
// Set in XInputdReadStateHook, sent by the polling thread (XAM call).
volatile LONG     g_guideRequest = 0;
volatile bool     g_pollRealRead = false;   // ReadRealControllerState is reading (polling thread)
const uint8_t     PLAYERS_KEY = 0x47;       // HID usage of Scroll Lock

// Both pads into dst: buttons OR-ed, each trigger the larger, each stick
// the one pushed further (so a resting stick never cancels the other).
static void MergeGamepad(XINPUT_GAMEPAD* dst, const XINPUT_GAMEPAD* src) {
	dst->wButtons |= src->wButtons;
	if (src->bLeftTrigger > dst->bLeftTrigger) dst->bLeftTrigger = src->bLeftTrigger;
	if (src->bRightTrigger > dst->bRightTrigger) dst->bRightTrigger = src->bRightTrigger;
	const uint32_t dl = (uint32_t)((int32_t)dst->sThumbLX * dst->sThumbLX) + (uint32_t)((int32_t)dst->sThumbLY * dst->sThumbLY);
	const uint32_t sl = (uint32_t)((int32_t)src->sThumbLX * src->sThumbLX) + (uint32_t)((int32_t)src->sThumbLY * src->sThumbLY);
	if (sl > dl) { dst->sThumbLX = src->sThumbLX; dst->sThumbLY = src->sThumbLY; }
	const uint32_t dr = (uint32_t)((int32_t)dst->sThumbRX * dst->sThumbRX) + (uint32_t)((int32_t)dst->sThumbRY * dst->sThumbRY);
	const uint32_t sr = (uint32_t)((int32_t)src->sThumbRX * src->sThumbRX) + (uint32_t)((int32_t)src->sThumbRY * src->sThumbRY);
	if (sr > dr) { dst->sThumbRX = src->sThumbRX; dst->sThumbRY = src->sThumbRY; }
}
// Etat du polling uniquement (un seul thread) :
int      g_liveHeldKey = -1, g_liveHeldTicks = 0;
bool     g_liveDirty = false, g_liveNotifyPending = false;
int      g_liveLastKind = 0;               // 0 sens, 1 zone morte, 2 anti-zone morte H, 3 V, 4 lissage
DWORD    g_liveLastChangeMs = 0;
uint16_t g_liveVid = 0, g_livePid = 0;

// ============================================================================
// VISEE DIRECTE "MOUSEHOOK" POUR CALL OF DUTY (2026-09-26, experimental)
//
// Au lieu de simuler un stick (vitesse maximale du jeu, courbe, zone morte,
// assistance a la visee), le mouvement de la souris est ajoute DIRECTEMENT aux
// angles de la camera dans la memoire du jeu : visee 1:1 comme sur PC.
//
// Adresses reprises de Xenia Canary Mousehook (Clippy95 / marinesciencedude,
// fichier src/xenia/hid/winkey/hookables/CallOfDuty.cc), sous licence BSD :
// "Copyright 2023 Ben Vanik. All rights reserved. Released under the BSD
// license". L'emulateur reproduit la memoire de la console a l'identique, les
// memes adresses valent donc sur une vraie Xbox 360 - pour la MEME version du
// jeu (sans mise a jour pour la plupart : TU0). Chaque version est reconnue par
// une signature ("cg_f" a une adresse connue) ; version inconnue = rien ne
// change, la souris reste sur le stick.
//
// Contexte d'execution : XInputdReadStateHook tourne dans le thread du jeu qui
// lit la manette - c'est la que Xenia fait la meme ecriture. Chaque pointeur
// est verifie par MmIsAddressValid avant usage. A NE PAS utiliser en ligne.
// ============================================================================
#define EN_MOUSEHOOK_COD 1

struct CodBuild {
	uint32_t titleId;
	uint32_t sigAddr;       // adresse de la signature de version
	uint32_t sigVal;        // valeur attendue (Xenia : "cg_f" = 0x63675F66 a l'adresse de cg_fov)
	uint32_t xAddr;         // lacet (static) ou decalage depuis la base
	uint32_t yAddr;         // tangage (static), 0 = xAddr - 4 depuis la base
	uint32_t fovScaleAddr;
	uint32_t baseAddr;      // 0 = adresses statiques
	const char* name;
};
static const CodBuild kCodBuilds[] = {
	{ 0x415607E6, 0x82044468, 0x63675F66, 0x824F6BDC, 0x824F6BD8, 0x824F6BC8, 0,          "Call of Duty 4 (solo)" },
	{ 0x415607E6, 0x82050E18, 0x63675F66, 0x25530,    0,          0x823B53A8, 0x82435818, "Call of Duty 4 TU0 (multi)" },
	{ 0x415607E6, 0x82051048, 0x63675F66, 0x25534,    0,          0x84CD7D44, 0x82435AB8, "Call of Duty 4 TU4 (multi)" },
	{ 0x41560817, 0x82020954, 0x63675F66, 0x82648B60, 0x82648B5C, 0x82470AE0, 0,          "Modern Warfare 2 TU0 (solo)" },
	{ 0x41560817, 0x820102D8, 0x63675F66, 0x335C,     0,          0x83AE320C, 0x825A3FAC, "Modern Warfare 2 TU0 (multi)" },
	{ 0x415608CB, 0x8200C558, 0x63675F66, 0x35F4,     0,          0x82599598, 0x826E0A80, "Modern Warfare 3 TU0 (multi)" },
	{ 0x4156081C, 0x82055874, 0x63675F66, 0xEAEC,     0,          0x824DE870, 0x849355D4, "World at War TU7 (solo)" },
	{ 0x4156081C, 0x82012704, 0x63675F66, 0x9D64,     0,          0x85914734, 0x824AEBF0, "World at War TU7 (multi)" },
	{ 0x415607E1, 0x82078F00, 0x63675F66, 0x82A58F68, 0x82A58F64, 0x825CE5F8, 0,          "Call of Duty 3 (solo)" },
	{ 0x415607E1, 0x82078614, 0x63675F66, 0x82C2F378, 0x82C2F374, 0x82C2F350, 0,          "Call of Duty 3 TU0 (multi)" },
	{ 0x415607E1, 0x8206E994, 0x63675F66, 0x82BEF278, 0x82BEF274, 0x82BEF250, 0,          "Call of Duty 3 TU3 (multi)" },
	{ 0x41560914, 0x82014B64, 0x63675F66, 0x82BBD060, 0x82BBD05C, 0x82848844, 0,          "Advanced Warfare TU17 (solo)" },
	{ 0x41560914, 0x82066D7C, 0x63675F66, 0x356C,     0,          0x82928CA4, 0x82B0D308, "Advanced Warfare TU17 (multi)" },
	// --- Versions trouvees par recherche memoire (Xenia + Cheat Engine, ou outil XBDM) ---
	// { titleId, sigAddr, sigVal, lacet, tangage, fovScale (0 = aucun), 0, "nom" },
	// MW3 campagne sans mise a jour (2026-09-26, HB) : lacet trouve sous Xenia +
	// Cheat Engine a 0x82743CBC (valeur qui suit la camera). Tangage suppose a
	// lacet - 4, comme les autres versions statiques. Signature : premier mot
	// de .data (0x824C0928), lu identique sur la console et sous Xenia.
	{ 0x415608CB, 0x82500004, 0x824C0928, 0x82743CBC, 0x82743CB8, 0,          0,          "Modern Warfare 3 TU0 (solo)" },
};

// Etat lu par le thread de polling (notification une fois par jeu).
volatile int  g_mousehookBuild = -1;           // index dans kCodBuilds, -1 = aucun
volatile bool g_mousehookNotifyPending = false;

static bool AddrOk(uint32_t a) { return a != 0 && MmIsAddressValid((PVOID)a) != FALSE; }

static void LogRunningGameVersion(uint32_t titleId);   // defined after GetTitleVersion

// Cherche la version en cours (une fois par titre). -1 = non reconnue.
static int FindCodBuild(uint32_t titleId) {
	static uint32_t s_title = 0xFFFFFFFF;
	static int s_build = -1;
	static DWORD s_checkedAt = 0;
	// Re-verifie de temps en temps : le solo et le multi d'un meme jeu sont
	// deux executables differents sous le meme Title ID.
	const DWORD now = GetTickCount();
	if (titleId == s_title && (now - s_checkedAt) < 3000) return s_build;
	const int before = (titleId == s_title) ? s_build : -2;
	s_title = titleId; s_checkedAt = now; s_build = -1;
	bool known = false;
	for (int i = 0; i < (int)(sizeof(kCodBuilds) / sizeof(kCodBuilds[0])); i++) {
		if (kCodBuilds[i].titleId != titleId) continue;
		known = true;
		if (!AddrOk(kCodBuilds[i].sigAddr)) continue;
		if (*(volatile uint32_t*)kCodBuilds[i].sigAddr == kCodBuilds[i].sigVal) { s_build = i; break; }
	}
	// Journal (2026-09-26, test HB "rien de special") : a chaque changement de
	// resultat pour un Call of Duty, ce qu'on a lu a chaque adresse de
	// signature - pour savoir quelle version tourne et pourquoi elle n'est pas
	// reconnue. FileLog ne fait que remplir un tampon : sans risque ici.
	if (known && s_build != before) {
		if (s_build >= 0) {
			FileLog("Direct aim: recognized build \"%hs\" (title %08x)", kCodBuilds[s_build].name, (unsigned int)titleId);
		} else {
			for (int i = 0; i < (int)(sizeof(kCodBuilds) / sizeof(kCodBuilds[0])); i++) {
				if (kCodBuilds[i].titleId != titleId) continue;
				const bool ok = AddrOk(kCodBuilds[i].sigAddr);
				FileLog("Direct aim: %hs not recognized - [%08x] = %08x%hs", kCodBuilds[i].name,
					(unsigned int)kCodBuilds[i].sigAddr,
					ok ? (unsigned int)*(volatile uint32_t*)kCodBuilds[i].sigAddr : 0u,
					ok ? "" : " (invalid address)");
			}
			LogRunningGameVersion(titleId);
		}
	}
	return s_build;
}


// true = mouvement applique a la camera (ne PAS l'envoyer aussi au stick).
static bool MouseHookCod(uint32_t titleId, int32_t dx, int32_t dy, int32_t sensitivity, bool invertY) {
#if EN_MOUSEHOOK_COD
	if (titleId == 0) return false;
	const int b = FindCodBuild(titleId);
	if (b != g_mousehookBuild) {
		g_mousehookBuild = b;
		if (b >= 0) g_mousehookNotifyPending = true;
	}
	if (b < 0) return false;
	const CodBuild& cb = kCodBuilds[b];
	uint32_t xa, ya;
	if (cb.baseAddr) {
		if (!AddrOk(cb.baseAddr)) return false;
		const uint32_t base = *(volatile uint32_t*)cb.baseAddr;
		static uint32_t s_lastBase = 1;
		if (base != s_lastBase) {                   // entree/sortie de partie, une ligne par changement
			s_lastBase = base;
			FileLog("Direct aim: base [%08x] = %08x (%hs)", (unsigned int)cb.baseAddr, (unsigned int)base,
				base ? "in game" : "menus - mouse on the stick");
		}
		if (base == 0) return false;               // pas en partie (menus) : la souris reste sur le stick
		xa = base + cb.xAddr;
		ya = xa - 4;
	} else {
		xa = cb.xAddr; ya = cb.yAddr;
	}
	if (!AddrOk(xa) || !AddrOk(ya)) return false;
	if (dx == 0 && dy == 0) return true;            // en partie : le stick ne recoit rien de la souris

	float fov = 1.0f;
	if (AddrOk(cb.fovScaleAddr)) fov = *(volatile float*)cb.fovScaleAddr;
	if (!(fov > 0.0f && fov <= 1.0f)) fov = 1.0f;   // meme garde que Xenia
	const float a = 0.9f;                           // fov_sensitivity par defaut de Xenia
	if (fov != 1.0f) fov = (1.0f - a) * fov * fov + a * fov;
	const float divisor = 10.5f / fov;
	const float sens = (sensitivity > 0 ? (float)sensitivity : 10000.0f) / 10000.0f;

	volatile float* yaw = (volatile float*)xa;
	volatile float* pitch = (volatile float*)ya;
	*yaw = *yaw - ((float)dx / divisor) * sens;
	if (!invertY) *pitch = *pitch + ((float)dy / divisor) * sens;
	else          *pitch = *pitch - ((float)dy / divisor) * sens;
	return true;
#else
	(void)titleId; (void)dx; (void)dy; (void)sensitivity; (void)invertY;
	return false;
#endif
}

// ============================================================================
// CAMERA AUTO-DETECTION FOR DIRECT AIM (v1.2.1, 2026-10-03)
//
// Hold F12 in game: the plugin turns the camera by itself through the right
// stick, finds the float(s) that follow it in the game's writable sections,
// lets the player confirm (F12 = keep, Backspace = next candidate), and saves
// the result to X360RemapStudio\camera_addresses.txt - one line per game
// version, easy to share. The pure logic (filters, ranking, file format,
// PE parsing) is in camera_learn.cpp and tested with g++.
//
// Threads:
//   - polling thread (MappingManagerThreadProc): keys, notifications, start;
//   - learning thread (CameraLearnThreadProc): the search itself, Sleep-based;
//   - game thread (XInputdReadStateHook): writes the injected stick, applies
//     learned cameras. Shared state = aligned 32-bit values + MemoryBarrier,
//     same rules as the rest of this file.
//
// Limits (see PLAN_v1.2.1.md): fixed addresses only (cameras inside the
// executable's data). Offline only, like all direct aim.
// ============================================================================
#define EN_CAMERA_LEARN 1

const uint8_t LEARN_KEY_START  = 0x45;   // F12: hold ~1.5 s to start, tap to accept
const uint8_t LEARN_KEY_NEXT   = 0x2A;   // Backspace: next candidate / cancel
const int     LEARN_HOLD_TICKS = 15;     // ~1.5 s at 100 ms per polling tick
const int32_t LEARN_STICK      = 32000;  // push strength

enum LearnState { LEARN_IDLE = 0, LEARN_RUNNING = 1, LEARN_CONFIRM = 2 };
volatile int32_t  g_learnState = LEARN_IDLE;
volatile int32_t  g_learnInject = 0;     // 1 = the hook writes g_learnRX/RY into the right stick
volatile int32_t  g_learnRX = 0, g_learnRY = 0;
volatile uint32_t g_learnHookTick = 0;   // last time the hook applied the injection
volatile int32_t  g_learnAbort = 0;      // set by the polling thread (Backspace, game change)
volatile int32_t  g_learnAnswer = 0;     // CONFIRM: 1 = keep, 2 = next
volatile uint32_t g_learnTitle = 0;
volatile int32_t  g_learnMsgPending = 0; // notification text prepared by the learning thread
wchar_t           g_learnMsg[160];

const int MAX_LEARNED_CAMERAS = 96;   // file lines + the built-in list (camera_known.cpp)
LearnedCamera     g_learned[MAX_LEARNED_CAMERAS];
volatile int32_t  g_learnedCount = 0;
LearnedCamera     g_learnTest;           // candidate being tried while waiting for the answer
volatile int32_t  g_learnTestActive = 0;
volatile int32_t  g_learnedNotifyIdx = -1;      // learned camera recognized: notify once
volatile int32_t  g_learnedActiveIdx = -1;      // learned camera in use (status file), see MouseHookLearned
volatile DWORD    g_learnedActiveTick = 0;
volatile int32_t  g_learnedNotifyPending = 0;

// The running game's executable: base, size, build stamp, PE header.
static bool GetTitleImage(uint32_t* base, uint32_t* size, uint32_t* stamp, uint8_t** nt) {
	__try {
		if (!XexExecutableModuleHandle) return false;
		PLDR_DATA_TABLE_ENTRY e = *XexExecutableModuleHandle;
		if (!e || !MmIsAddressValid(e)) return false;
		*base = (uint32_t)e->ImageBase;
		*size = (uint32_t)e->SizeOfFullImage;
		*stamp = (uint32_t)e->TimeDateStamp;
		*nt = (uint8_t*)e->NtHeadersBase;
		return *base != 0 && *size != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

// Executable version from the XEX header (v3 lines are matched on it, like
// Xenia Mousehook does). 0 if unknown.
static uint32_t GetTitleVersion() {
	__try {
		if (!XexExecutableModuleHandle) return 0;
		PLDR_DATA_TABLE_ENTRY e = *XexExecutableModuleHandle;
		if (!e || !MmIsAddressValid(e) || !e->XexHeaderBase) return 0;
		PXEX_EXECUTION_ID id = (PXEX_EXECUTION_ID)RtlImageXexHeaderField(e->XexHeaderBase, XEX_HEADER_EXECUTION_ID);
		if (!id || !MmIsAddressValid(id)) return 0;
		return (uint32_t)id->Version;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

// Which exact version of the game is running (2026-10-12): needed to add a
// title update to direct aim. The version is the XEX one (major.minor.build.qfe,
// what Xenia shows as "vX.X.X.X"); build stamp + size identify the executable.
static void LogRunningGameVersion(uint32_t titleId) {
	uint32_t base = 0, size = 0, stamp = 0; uint8_t* nt = 0;
	if (!GetTitleImage(&base, &size, &stamp, &nt)) {
		FileLog("Game %08x: executable not readable", (unsigned int)titleId);
		return;
	}
	const uint32_t v = GetTitleVersion();
	FileLog("Game %08x: version %u.%u.%u.%u, build stamp %08x, image %08x size %08x",
		(unsigned int)titleId, (unsigned)(v >> 28), (unsigned)((v >> 24) & 0xF),
		(unsigned)((v >> 8) & 0xFFFF), (unsigned)(v & 0xFF),
		(unsigned int)stamp, (unsigned int)base, (unsigned int)size);
}

static void LearnedCamerasPath(char* out, int outSize) {
	StudioPath(out, outSize, "camera_addresses.txt");
}

// Reads camera_addresses.txt. Called at boot (DllMain) and after a save.
static void LoadLearnedCameras() {
	char path[128];
	LearnedCamerasPath(path, sizeof(path));
	static LearnedCamera tmp[MAX_LEARNED_CAMERAS];
	int n = 0;
	std::ifstream in(path, std::ios::binary);
	if (in.is_open()) {
		std::string line;
		while (n < MAX_LEARNED_CAMERAS && std::getline(in, line)) {
			LearnedCamera e;
			if (CamParseLine(line.c_str(), &e)) tmp[n++] = e;
		}
		in.close();
	}
	const int fromFile = n;
	// Then the built-in list (camera_known.cpp). A line from the file for the
	// same game version comes first and therefore wins.
	for (size_t k = 0; k < kCamKnownCount && n < MAX_LEARNED_CAMERAS; k++) {
		LearnedCamera e;
		if (CamParseLine(kCamKnownLines[k], &e)) tmp[n++] = e;
	}
	g_learnedCount = 0;           // readers see "nothing" while the table is rewritten
	MemoryBarrier();
	for (int i = 0; i < n; i++) g_learned[i] = tmp[i];
	MemoryBarrier();
	g_learnedCount = n;
	FileLog("Learned cameras: %d from camera_addresses.txt, %d built in", fromFile, n - fromFile);
}

// Adds or replaces one entry (same game AND same build), rewrites the file.
static bool SaveLearnedCamera(const LearnedCamera& e) {
	char path[128];
	LearnedCamerasPath(path, sizeof(path));
	std::vector<std::string> keep;
	{
		std::ifstream in(path, std::ios::binary);
		std::string line;
		while (in.is_open() && std::getline(in, line)) {
			LearnedCamera old;
			if (!CamParseLine(line.c_str(), &old)) continue;   // comments are rewritten below
			if (old.titleId == e.titleId && old.timeStamp == e.timeStamp && old.imageSize == e.imageSize) continue;
			keep.push_back(line);
		}
	}
	char newLine[160];
	if (CamFormatLine(e, newLine, sizeof(newLine)) <= 0) return false;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) return false;
	out << "# X360Remap - learned cameras for direct aim (one line per game version).\n";
	out << "# titleId buildStamp imageSize yawAddr pitchAddr signX signY unit name\n";
	out << "# Share this file: another player with the same game version can use it as is.\n";
	for (size_t i = 0; i < keep.size(); i++) out << keep[i] << "\n";
	out << newLine << "\n";
	out.close();
	return true;
}

// Learned camera for the running build, -1 if none (re-checked every 3 s:
// solo and multiplayer are two executables under the same Title ID).
static int FindLearnedCamera(uint32_t titleId) {
	static uint32_t s_title = 0xFFFFFFFF;
	static int s_idx = -1;
	static DWORD s_at = 0;
	static int32_t s_count = -1;
	const DWORD now = GetTickCount();
	const int32_t count = g_learnedCount;
	if (titleId == s_title && count == s_count && (now - s_at) < 3000) return s_idx;
	s_title = titleId; s_at = now; s_count = count; s_idx = -1;
	if (count <= 0) return -1;
	uint32_t base = 0, size = 0, stamp = 0; uint8_t* nt = 0;
	if (!GetTitleImage(&base, &size, &stamp, &nt)) return -1;
	const uint32_t version = GetTitleVersion();
	MemoryBarrier();
	for (int i = 0; i < count && i < MAX_LEARNED_CAMERAS; i++) {
		const LearnedCamera& e = g_learned[i];
		// Build stamp lines (imageSize 0 = any size) or version lines - see CamMatches.
		if (CamMatches(e, titleId, stamp, size, version)) { s_idx = i; break; }
	}
	return s_idx;
}

// Memory reader for CamResolve (game thread and learning thread).
static bool LearnRead32Cb(uint32_t addr, uint32_t* out, void*) {
	if (!AddrOk(addr) || (addr & 3)) return false;
	*out = *(volatile uint32_t*)addr;
	return true;
}

// Direct aim through a learned camera. 0 = not handled, 1 = both axes
// handled, 2 = only horizontal (no pitch learned: vertical stays on the stick).
static int MouseHookLearned(uint32_t titleId, int32_t dx, int32_t dy, int32_t sensitivity, bool invertY) {
#if EN_CAMERA_LEARN
	if (titleId == 0 || g_learnInject) return 0;
	const LearnedCamera* e = 0;
	// 1 = candidate being tried, 2 = found for this session only (no permanent address)
	if (g_learnTestActive && g_learnTitle == titleId) {
		MemoryBarrier();
		e = &g_learnTest;
	} else {
		static int s_prev = -1;
		const int i = FindLearnedCamera(titleId);
		if (i != s_prev) {
			s_prev = i;
			if (i >= 0) { g_learnedNotifyIdx = i; MemoryBarrier(); g_learnedNotifyPending = 1; }
		}
		if (i < 0) return 0;
		e = &g_learned[i];
		g_learnedActiveIdx = i;
		g_learnedActiveTick = GetTickCount();
	}
	// v3: follows the pointers of the learned entry (cameras inside objects
	// allocated by the game). Null pointer = menus / loading: stick as usual.
	uint32_t yawA = 0, pitchA = 0;
	if (!CamResolve(*e, LearnRead32Cb, 0, &yawA, &pitchA)) return 0;
	// Safety (2026-10-12, MW2 TU9 crash): never write through an unaligned or
	// invalid address, and never into something that does not look like an
	// angle - a wrong learned line must leave the mouse on the stick, not
	// crash the game.
	if (!AddrOk(yawA) || (yawA & 3)) return 0;
	const bool hasPitch = pitchA != 0 && !(pitchA & 3) && AddrOk(pitchA);
	if (dx == 0 && dy == 0) return hasPitch ? 1 : 2;
	__try {
		volatile float* yawP = (volatile float*)yawA;
		volatile float* pitchP = hasPitch ? (volatile float*)pitchA : 0;
		float y = *yawP, p = pitchP ? *pitchP : 0.0f;
		// Not a plausible angle (NaN, infinity, or huge): not our camera.
		if (!(y > -100000.0f && y < 100000.0f)) return 0;
		if (pitchP && !(p > -100000.0f && p < 100000.0f)) pitchP = 0;
		CamApplyMouse(&y, pitchP ? &p : 0, dx, dy, sensitivity, invertY, *e);
		*yawP = y;
		if (pitchP) *pitchP = p;
		return pitchP ? 1 : 2;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
#else
	(void)titleId; (void)dx; (void)dy; (void)sensitivity; (void)invertY;
	return 0;
#endif
}

#if EN_CAMERA_LEARN
static void LearnNotify(NotifyStrId id) {
	swprintf(g_learnMsg, 160, L"%s", WTr(id));
	MemoryBarrier();
	g_learnMsgPending = 1;
}
static void LearnNotifyInt(NotifyStrId id, int a, int b) {
	swprintf(g_learnMsg, 160, WTr(id), a, b);
	MemoryBarrier();
	g_learnMsgPending = 1;
}
static void LearnNotifyInt3(NotifyStrId id, int a, int b, int c) {
	swprintf(g_learnMsg, 160, WTr(id), a, b, c);
	MemoryBarrier();
	g_learnMsgPending = 1;
}
static void LearnStick(int32_t rx, int32_t ry) {
	g_learnRX = rx; g_learnRY = ry;
	MemoryBarrier();
}
// false = canceled (Backspace) or the game changed.
static bool LearnWait(DWORD ms) {
	const DWORD start = GetTickCount();
	while ((GetTickCount() - start) < ms) {
		if (g_learnAbort || g_lastTitleId != g_learnTitle) return false;
		Sleep(10);
	}
	return !g_learnAbort && g_lastTitleId == g_learnTitle;
}
// One push: hold the stick, then centre it and let the camera settle.
static bool LearnPush(int32_t rx, int32_t ry) {
	LearnStick(rx, ry);
	if (!LearnWait(220)) { LearnStick(0, 0); return false; }
	LearnStick(0, 0);
	return LearnWait(200);   // games with camera smoothing need a moment to stop
}
static bool LearnReadFloat(uint32_t addr, float* out) {
	__try {
		if (!AddrOk(addr)) return false;
		*out = *(volatile float*)addr;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
static bool LearnWriteFloat(uint32_t addr, float v) {
	__try {
		if (!AddrOk(addr)) return false;
		*(volatile float*)addr = v;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
// Unreadable values become 1e30 - implausible, so the filters drop them.
static void LearnReadCandidates(const CamCandidate* c, size_t n, float* out) {
	for (size_t i = 0; i < n; i++)
		if (!LearnReadFloat(c[i].addr, &out[i])) out[i] = 1e30f;
}
// Bytes readable from addr (whole 4 KB pages), up to maxBytes.
static uint32_t LearnValidPrefix(uint32_t addr, uint32_t maxBytes) {
	uint32_t ok = 0;
	while (ok < maxBytes) {
		if (!MmIsAddressValid((PVOID)(addr + ok))) break;
		const uint32_t pageEnd = ((addr + ok) & ~0xFFFu) + 0x1000u;
		ok = pageEnd - addr;
	}
	return ok > maxBytes ? maxBytes : ok;
}
static bool LearnCopy(void* dst, uint32_t src, uint32_t bytes) {
	__try { memcpy(dst, (const void*)src, bytes); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static size_t LearnCollect(const float* before, uint32_t addr, size_t words, const CamLearnLimits& lim,
                           CamCandidate* out, size_t count, size_t cap) {
	__try { return CamCollect(before, (const float*)addr, words, addr, lim, out, count, cap); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return count; }
}

const size_t LEARN_MAX_PER_BLOCK  = 4096;   // changes kept per block at most (busy blocks are truncated)
const DWORD  LEARN_WINDOW_MS      = 80;     // block compared with itself this long after
const uint32_t LEARN_MAX_HEAP     = 640u * 1024u * 1024u;
const size_t LEARN_MAX_RANGES     = 512;
// Allocated for the search only (see CameraLearnRun), never kept in memory.
static CamCandidate* g_learnCand = 0;
static float*        g_learnNow = 0;
static size_t        LEARN_MAX_CANDIDATES = 0;
static float*        g_learnBefore = 0;      // block buffer
static uint32_t      g_learnBlockBytes = 0;
static CamRange      g_learnRanges[LEARN_MAX_RANGES];
static size_t        g_learnNumRanges = 0;
static size_t        g_learnNumImageRanges = 0;   // the first ones = the executable's own data
static uint32_t      g_learnTotal = 0;
static uint32_t      g_learnImageBase = 0, g_learnImageSize = 0;

// ---------------------------------------------------------------------------
// v3 (2026-10-07) - after "several tests failed on several games" (HB):
//   1. the stick is now injected into EVERY controller the game reads (see the
//      end of XInputdReadStateHook): with the pad as player 1, nothing turned;
//   2. the search covers the game's whole memory (heap), not only the
//      executable: most games (GTA V, MW3 multi...) keep the camera in
//      objects they allocate;
//   3. such a camera moves every session: once confirmed, the plugin looks
//      for the pointers that lead to it (1 or 2 levels) so it can find it
//      again next time; if none, it works for this session only;
//   4. its own log, X360RemapStudio\camera_search.txt, never erased at boot.
// ---------------------------------------------------------------------------

static void LearnLogPath(char* out, int outSize) { StudioPath(out, outSize, "camera_search.txt"); }

static void LearnLog(const char* fmt, ...) {
	char line[300];
	va_list args;
	va_start(args, fmt);
	_vsnprintf(line, sizeof(line) - 2, fmt, args);
	va_end(args);
	line[sizeof(line) - 2] = '\0';
	FileLog("Camera search: %hs", line);
	char path[128];
	LearnLogPath(path, sizeof(path));
	std::ofstream f(path, std::ios::binary | std::ios::app);
	if (f.is_open()) {
		char stamp[24];
		_snprintf(stamp, sizeof(stamp) - 1, "[%lu] ", (unsigned long)GetTickCount());
		stamp[sizeof(stamp) - 1] = '\0';
		f << stamp << line << "\r\n";
	}
}

static void LearnLogStart(uint32_t title) {
	char path[128];
	LearnLogPath(path, sizeof(path));
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (f.is_open()) {
		char head[160];
		_snprintf(head, sizeof(head) - 1, "X360Remap camera search - title %08X - send this file if it fails\r\n",
			(unsigned int)title);
		head[sizeof(head) - 1] = '\0';
		f << head;
	}
}

static bool LearnIsOwnMemory(uint32_t a, uint32_t bytes) {
	struct { uint32_t p; uint32_t n; } own[3] = {
		{ (uint32_t)g_learnBefore, g_learnBlockBytes },
		{ (uint32_t)g_learnCand, (uint32_t)(LEARN_MAX_CANDIDATES * sizeof(CamCandidate)) },
		{ (uint32_t)g_learnNow, (uint32_t)(LEARN_MAX_CANDIDATES * sizeof(float)) },
	};
	for (int i = 0; i < 3; i++)
		if (own[i].p && a < own[i].p + own[i].n && own[i].p < a + bytes) return true;
	return false;
}

static uint32_t LearnQueryProtect(uint32_t a, void*) {
	__try { return (uint32_t)MmQueryAddressProtect((PVOID)a); }
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Values of the candidates become their new reference ("last").
static void LearnRebase(CamCandidate* c, size_t n) {
	LearnReadCandidates(c, n, g_learnNow);
	for (size_t i = 0; i < n; i++) c[i].last = g_learnNow[i];
}

// Hold (rx, ry), take a reference, keep holding a moment, filter.
// Stop (expect 0): half a second to settle (camera smoothing), then 250 ms.
static bool LearnHoldAndFilter(CamCandidate* c, size_t* n, int32_t rx, int32_t ry, int expect, const CamLearnLimits& lim) {
	LearnStick(rx, ry);
	if (!LearnWait(expect == 0 ? 500 : 200)) return false;
	LearnRebase(c, *n);
	if (!LearnWait(expect == 0 ? 250 : LEARN_WINDOW_MS)) return false;
	LearnReadCandidates(c, *n, g_learnNow);
	*n = CamFilter(c, *n, g_learnNow, expect, lim);
	return true;
}

// One round: same direction, opposite direction, stop.
static bool LearnVerifyRound(CamCandidate* c, size_t* n, int32_t rx, int32_t ry, const CamLearnLimits& lim) {
	if (*n == 0) return true;
	if (!LearnHoldAndFilter(c, n, rx, ry, +1, lim)) return false;
	if (!LearnHoldAndFilter(c, n, -rx, -ry, -1, lim)) return false;
	if (!LearnHoldAndFilter(c, n, 0, 0, 0, lim)) return false;
	LearnStick(0, 0);
	return true;
}

// Write test: write +D, see if it stays (persistent) and if OTHER candidates
// follow (the game used our write: this one is the master).
static bool LearnWriteTest(CamCandidate* c, size_t n) {
	// Followers are looked for among the first 64 only; up to 128 are tested.
	static float othersBefore[64];
	const size_t m = n < 64 ? n : 64;
	for (size_t i = 0; i < n; i++) {
		LearnReadCandidates(c, m, othersBefore);
		float v;
		if (!LearnReadFloat(c[i].addr, &v)) continue;
		const float D = 2.0f * c[i].firstDelta;
		const float w = v + D;
		if (!LearnWriteFloat(c[i].addr, w)) continue;
		if (!LearnWait(100)) return false;
		float rb;
		if (!LearnReadFloat(c[i].addr, &rb)) continue;
		c[i].persistent = CamPersistent(w, rb, D) ? 1 : 0;
		LearnReadCandidates(c, m, g_learnNow);
		c[i].followers = 0;
		for (size_t j = 0; j < m; j++)
			if (j != i && CamFollowsWrite(othersBefore[j], g_learnNow[j], D) && c[i].followers < 255) c[i].followers++;
		if (c[i].persistent) LearnWriteFloat(c[i].addr, rb - D);   // turn back
		if (!LearnWait(60)) return false;
	}
	return true;
}

// Block pass over all ranges. Horizontal: stick held right the whole time.
// Vertical: up / down alternate per block (the pitch stops at its limits);
// candidates from "down" blocks get their sign flipped (sign = "stick up").
// When the candidate table is 3/4 full, a verification round prunes it.
static size_t LearnBlockPass(bool vertical, const CamLearnLimits& lim, bool* aborted) {
	*aborted = false;
	size_t n = 0;
	uint32_t done = 0;
	int nextPct = 25, block = 0;
	const int32_t rx = vertical ? 0 : LEARN_STICK, ry = vertical ? LEARN_STICK : 0;
	// v4 (2026-10-08, HB): small back-and-forth movements on BOTH axes - the
	// camera no longer spins in circles (games with turn acceleration or
	// momentum, like Metro, gave unreliable readings).
	for (size_t r = 0; r < g_learnNumRanges; r++) {
		uint32_t off = 0;
		while (off < g_learnRanges[r].size) {
			const uint32_t addr = g_learnRanges[r].start + off;
			uint32_t want = g_learnRanges[r].size - off;
			if (want > g_learnBlockBytes) want = g_learnBlockBytes;
			const uint32_t bytes = LearnValidPrefix(addr, want) & ~3u;
			if (bytes == 0) { const uint32_t skip = 0x1000 - (addr & 0xFFF); off += skip; done += skip; continue; }
			if (LearnIsOwnMemory(addr, bytes)) { off += bytes; done += bytes; continue; }

			if (n > (LEARN_MAX_CANDIDATES * 3) / 4) {
				const size_t before = n;
				LearnStick(0, 0);
				if (!LearnVerifyRound(g_learnCand, &n, rx, ry, lim)) { *aborted = true; return n; }
				LearnLog("table nearly full: %u -> %u candidates after an early check", (unsigned int)before, (unsigned int)n);
			}

			// One block = one short push, alternating right/left (or up/down).
			const int dir = (block & 1) ? -1 : 1;
			LearnStick(dir * rx, dir * ry);
			if (!LearnWait(120)) { *aborted = true; return n; }
			block++;
			if (g_learnAbort || g_lastTitleId != g_learnTitle) { *aborted = true; return n; }
			if (LearnCopy(g_learnBefore, addr, bytes)) {
				if (!LearnWait(LEARN_WINDOW_MS)) { *aborted = true; return n; }
				const size_t start = n;
				size_t cap = start + LEARN_MAX_PER_BLOCK;
				if (cap > LEARN_MAX_CANDIDATES) cap = LEARN_MAX_CANDIDATES;
				n = LearnCollect(g_learnBefore, addr, bytes / 4, lim, g_learnCand, n, cap);
				if (dir < 0)
					for (size_t i = start; i < n; i++) { g_learnCand[i].sign = (int8_t)-g_learnCand[i].sign; g_learnCand[i].firstDelta = -g_learnCand[i].firstDelta; }
				if (n - start == LEARN_MAX_PER_BLOCK && bytes >= 8 * 0x1000) {
					// Busy block (v4, Metro log 2026-10-08: 13 blocks truncated -
					// the camera may well have been in the part that was dropped).
					// Start it again in 8 smaller pieces, each with its own push,
					// pruning the table whenever it fills up. Nothing is dropped
					// unless a single piece is still too busy.
					n = start;
					LearnStick(0, 0);
					const uint32_t piece = (bytes / 8) & ~0xFFFu;
					size_t kept = 0;
					for (int k = 0; k < 8; k++) {
						const uint32_t pa = addr + (uint32_t)k * piece;
						const uint32_t pb = (k == 7) ? bytes - (uint32_t)k * piece : piece;
						if (n > (LEARN_MAX_CANDIDATES * 3) / 4) {
							if (!LearnVerifyRound(g_learnCand, &n, rx, ry, lim)) { *aborted = true; return n; }
						}
						const int pdir = (k & 1) ? -dir : dir;
						LearnStick(pdir * rx, pdir * ry);
						if (!LearnWait(120)) { *aborted = true; return n; }
						if (!LearnCopy(g_learnBefore, pa, pb)) { LearnStick(0, 0); continue; }
						if (!LearnWait(LEARN_WINDOW_MS)) { *aborted = true; return n; }
						const size_t ps = n;
						size_t pc = ps + LEARN_MAX_PER_BLOCK;
						if (pc > LEARN_MAX_CANDIDATES) pc = LEARN_MAX_CANDIDATES;
						n = LearnCollect(g_learnBefore, pa, pb / 4, lim, g_learnCand, n, pc);
						if (pdir < 0)
							for (size_t i = ps; i < n; i++) { g_learnCand[i].sign = (int8_t)-g_learnCand[i].sign; g_learnCand[i].firstDelta = -g_learnCand[i].firstDelta; }
						if (n - ps == LEARN_MAX_PER_BLOCK)
							LearnLog("piece %08x still very busy - first %u changes kept", (unsigned int)pa, (unsigned int)(n - ps));
						kept += n - ps;
						LearnStick(0, 0);
					}
					LearnLog("block %08x busy - scanned again in 8 pieces (%u changes kept)", (unsigned int)addr, (unsigned int)kept);
				}
			}
			off += bytes; done += bytes;
			LearnStick(0, 0);
			if (g_learnTotal > 0) {
				const int pct = (int)(((uint64_t)done * 100u) / g_learnTotal);
				if (pct >= nextPct && pct < 100) { LearnNotifyInt(STR_LEARN_PROGRESS_FMT, nextPct, 0); nextPct += 25; }
			}
		}
	}
	LearnStick(0, 0);
	return n;
}

// Full search for one axis: block pass, 3 verification rounds, write test.
// Counters shown to the player after each search ("values > checked > found").
static uint32_t g_learnStatScan = 0, g_learnStatChecked = 0;

static size_t LearnSearchAxis(bool vertical, const CamLearnLimits& lim, bool* aborted) {
	const char* axis = vertical ? "pitch" : "yaw";
	const DWORD t0 = GetTickCount();
	size_t n = LearnBlockPass(vertical, lim, aborted);
	if (*aborted) return 0;
	g_learnStatScan = (uint32_t)n;
	g_learnStatChecked = 0;
	LearnLog("%hs: %u candidate(s) after the scan (%u ms)", axis, (unsigned int)n, (unsigned int)(GetTickCount() - t0));
	if (!LearnWait(300)) { *aborted = true; return 0; }
	const int32_t rx = vertical ? 0 : LEARN_STICK, ry = vertical ? LEARN_STICK : 0;
	for (int round = 0; round < 3 && n > 0; round++) {
		if (!LearnVerifyRound(g_learnCand, &n, rx, ry, lim)) { *aborted = true; return 0; }
		LearnLog("%hs: %u candidate(s) after check %d/3", axis, (unsigned int)n, round + 1);
		if (round == 0) g_learnStatChecked = (uint32_t)n;
		LearnNotifyInt3(vertical ? STR_LEARN_STATS_V_FMT : STR_LEARN_STATS_H_FMT,
			(int)g_learnStatScan, (int)g_learnStatChecked, (int)n);   // live counters
	}
	if (n == 0) return 0;
	// Lessons from the PC tool (2026-10-08, see camera_learn.h): drop the
	// duplicate views of the same memory, then test the RAREST movements
	// first - the real angle is alone, the copies come in crowds.
	{
		const size_t before = n;
		n = CamDedupeViews(g_learnCand, n);
		if (n != before) LearnLog("%hs: %u duplicate view(s) removed, %u left", axis, (unsigned int)(before - n), (unsigned int)n);
	}
	CamRankByRarity(g_learnCand, n);
	if (n > 128) n = 128;
	if (!LearnWriteTest(g_learnCand, n)) { *aborted = true; return 0; }
	CamRank(g_learnCand, n);
	for (size_t i = 0; i < n && i < 24; i++)
		LearnLog("%hs: #%u %08x%hs delta=%d/1000 hits=%d persistent=%d followers=%d", axis, (unsigned int)i,
			(unsigned int)g_learnCand[i].addr,
			(g_learnCand[i].addr >= g_learnImageBase && g_learnCand[i].addr < g_learnImageBase + g_learnImageSize) ? " (exe)" : " (heap)",
			(int)(g_learnCand[i].firstDelta * 1000.0f), (int)g_learnCand[i].hits,
			(int)g_learnCand[i].persistent, (int)g_learnCand[i].followers);
	LearnLog("%hs: done in %u ms", axis, (unsigned int)(GetTickCount() - t0));
	return n;
}

// Pitch next to the yaw first (most engines keep them side by side).
static uint32_t LearnFindPitchNear(uint32_t yawAddr, int8_t* signY, const CamLearnLimits& lim, bool* aborted) {
	*aborted = false;
	static const int offs[8] = { -4, 4, -8, 8, -12, 12, -16, 16 };
	CamCandidate c[8];
	size_t n = 0;
	for (int i = 0; i < 8; i++) {
		float v;
		if (!LearnReadFloat(yawAddr + offs[i], &v)) continue;
		memset(&c[n], 0, sizeof(c[n]));
		c[n].addr = yawAddr + offs[i];
		c[n].last = v;
		n++;
	}
	LearnStick(0, LEARN_STICK);
	if (!LearnWait(200)) { *aborted = true; return 0; }
	LearnRebase(c, n);
	if (!LearnWait(LEARN_WINDOW_MS)) { *aborted = true; return 0; }
	LearnReadCandidates(c, n, g_learnNow);
	n = CamEstablish(c, n, g_learnNow, lim);
	if (!LearnHoldAndFilter(c, &n, 0, -LEARN_STICK, -1, lim)) { *aborted = true; return 0; }
	if (!LearnHoldAndFilter(c, &n, 0, 0, 0, lim)) { *aborted = true; return 0; }
	for (int r = 0; r < 2 && n > 0; r++)
		if (!LearnVerifyRound(c, &n, 0, LEARN_STICK, lim)) { *aborted = true; return 0; }
	LearnStick(0, 0);
	if (n == 0) return 0;
	CamRank(c, n);
	*signY = c[0].sign;
	return c[0].addr;
}

// A system message box, at the player's pace (HB, 2026-10-30: notifications
// were too fast to read). Learning thread only: never XAM from the input path.
// Returns the button pressed, -1 = closed or game left, -2 = could not open.
// Backspace only cancels once the box is closed: the box owns its statics
// until XAM completes it.
static int LearnBox(const wchar_t* text, int count, NotifyStrId b0, NotifyStrId b1, NotifyStrId b2) {
	static MESSAGEBOX_RESULT s_result;
	static XOVERLAPPED s_overlapped;
	static LPCWSTR s_buttons[3];
	s_buttons[0] = WTr(b0); s_buttons[1] = WTr(b1); s_buttons[2] = WTr(b2);
	g_learnInject = 0;
	LearnStick(0, 0);
	// ERROR_ACCESS_DENIED while the previous box is still closing (log of 2026-10-31).
	HRESULT hr = ERROR_ACCESS_DENIED;
	for (int i = 0; i < 30 && hr == ERROR_ACCESS_DENIED; i++) {
		if (i) Sleep(100);
		memset(&s_result, 0, sizeof(s_result));
		memset(&s_overlapped, 0, sizeof(s_overlapped));
		hr = XamShowMessageBoxUI(XUSER_INDEX_ANY, WTr(STR_LEARN_BOX_TITLE), text, count, s_buttons, 0,
			XMB_ALERTICON, &s_result, &s_overlapped);
	}
	if (hr != ERROR_IO_PENDING && hr != ERROR_SUCCESS) {
		LearnLog("message box refused: %08x", (unsigned int)hr);
		return -2;
	}
	while (!XHasOverlappedIoCompleted(&s_overlapped)) {
		if (g_lastTitleId != g_learnTitle) return -1;
		Sleep(100);
	}
	return s_overlapped.InternalLow == ERROR_SUCCESS ? (int)s_result.dwButtonPressed : -1;
}
// End of a search: a box the player cannot miss, a notification if it fails.
static void LearnResult(NotifyStrId id) {
	if (LearnBox(WTr(id), 1, STR_LEARN_BTN_OK, STR_LEARN_BTN_OK, STR_LEARN_BTN_OK) == -2) LearnNotify(id);
}

// Instruction, 5 s to move the mouse (F12 / Backspace still answer), question.
// 1 = yes, 2 = no, 0 = stopped. Old repeated notification if no box can open.
static int LearnAsk(NotifyStrId fallback, NotifyStrId instruction, NotifyStrId question, int k, int total) {
	static wchar_t text[300];
	swprintf(text, 300, WTr(instruction), k, total);
	const int go = LearnBox(text, 2, STR_LEARN_BTN_OK, STR_LEARN_BTN_STOP, STR_LEARN_BTN_STOP);
	if (go != -2) {
		if (go != 0) return 0;
		const DWORD t = GetTickCount();
		while (g_learnAnswer == 0 && (GetTickCount() - t) < 5000)
			if (!LearnWait(50)) return 0;
		if (g_learnAnswer != 0) return g_learnAnswer;
		const int yes = LearnBox(WTr(question), 3, STR_LEARN_BTN_YES, STR_LEARN_BTN_NO, STR_LEARN_BTN_STOP);
		return yes == 0 ? 1 : yes == 1 ? 2 : 0;
	}
	DWORD lastShown = 0;
	const DWORD t0 = GetTickCount();
	while (g_learnAnswer == 0 && (GetTickCount() - t0) < 60000) {
		if (lastShown == 0 || (GetTickCount() - lastShown) >= 5000) {
			LearnNotifyInt(fallback, k, total);
			lastShown = GetTickCount();
		}
		if (!LearnWait(50)) return 0;
	}
	return g_learnAnswer;
}

// Pointer search for a camera found in the heap. Fills e.levels / base /
// off1 / yawAddr / pitchAddr. false = no pointer found (session only).
static bool LearnFindPointerPath(uint32_t yaw, uint32_t pitch, LearnedCamera* e) {
	static CamPointerHit hits[256];
	static size_t hitTarget[256];
	const uint32_t MAX_OFF = 0x4000;
	// Level 1: a fixed word in the executable pointing just before the yaw.
	size_t nh = 0;
	for (size_t r = 0; r < g_learnNumImageRanges && nh < 256; r++) {
		for (uint32_t off = 0; off < g_learnRanges[r].size; ) {
			const uint32_t addr = g_learnRanges[r].start + off;
			uint32_t want = g_learnRanges[r].size - off;
			if (want > 0x100000) want = 0x100000;
			const uint32_t bytes = LearnValidPrefix(addr, want) & ~3u;
			if (bytes == 0) { off += 0x1000 - (addr & 0xFFF); continue; }
			__try { nh = CamFindPointers((const uint32_t*)addr, bytes / 4, addr, yaw, MAX_OFF, hits, nh, 256); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
			off += bytes;
		}
	}
	if (nh > 0) {
		size_t best = 0;
		for (size_t i = 1; i < nh; i++) if (hits[i].value > hits[best].value) best = i;   // closest object start
		e->levels = 1; e->base = hits[best].loc; e->off1 = 0;
		e->yawAddr = yaw - hits[best].value;
		LearnLog("pointer: 1 level, [%08x] + %x (%u candidate pointer(s))", (unsigned int)e->base,
			(unsigned int)e->yawAddr, (unsigned int)nh);
	} else {
		// Level 2: heap words pointing just before the yaw, then fixed words
		// in the executable pointing just before one of those.
		static CamPointerHit l1[2048];
		size_t n1 = 0;
		for (size_t r = g_learnNumImageRanges; r < g_learnNumRanges && n1 < 2048; r++) {
			for (uint32_t off = 0; off < g_learnRanges[r].size && n1 < 2048; ) {
				const uint32_t addr = g_learnRanges[r].start + off;
				uint32_t want = g_learnRanges[r].size - off;
				if (want > 0x100000) want = 0x100000;
				const uint32_t bytes = LearnValidPrefix(addr, want) & ~3u;
				if (bytes == 0) { off += 0x1000 - (addr & 0xFFF); continue; }
				if (!LearnIsOwnMemory(addr, bytes)) {
					__try { n1 = CamFindPointers((const uint32_t*)addr, bytes / 4, addr, yaw, MAX_OFF, l1, n1, 2048); }
					__except (EXCEPTION_EXECUTE_HANDLER) {}
				}
				off += bytes;
			}
			if (g_learnAbort) return false;
		}
		LearnLog("pointer: %u heap pointer(s) to the camera object", (unsigned int)n1);
		if (n1 == 0) return false;
		// Sort level-1 locations (insertion sort: a few hundred at most).
		for (size_t i = 1; i < n1; i++) {
			CamPointerHit v = l1[i]; size_t j = i;
			while (j > 0 && l1[j - 1].loc > v.loc) { l1[j] = l1[j - 1]; j--; }
			l1[j] = v;
		}
		static uint32_t targets[2048];
		for (size_t i = 0; i < n1; i++) targets[i] = l1[i].loc;
		nh = 0;
		for (size_t r = 0; r < g_learnNumImageRanges && nh < 256; r++) {
			for (uint32_t off = 0; off < g_learnRanges[r].size; ) {
				const uint32_t addr = g_learnRanges[r].start + off;
				uint32_t want = g_learnRanges[r].size - off;
				if (want > 0x100000) want = 0x100000;
				const uint32_t bytes = LearnValidPrefix(addr, want) & ~3u;
				if (bytes == 0) { off += 0x1000 - (addr & 0xFFF); continue; }
				__try { nh = CamFindPointersMulti((const uint32_t*)addr, bytes / 4, addr, targets, n1, 0x1000, hits, hitTarget, nh, 256); }
				__except (EXCEPTION_EXECUTE_HANDLER) {}
				off += bytes;
			}
		}
		if (nh == 0) return false;
		size_t best = 0;
		uint32_t bestCost = 0xFFFFFFFF;
		for (size_t i = 0; i < nh; i++) {
			const CamPointerHit& lv1 = l1[hitTarget[i]];
			const uint32_t cost = (lv1.loc - hits[i].value) + (yaw - lv1.value);
			if (cost < bestCost) { bestCost = cost; best = i; }
		}
		const CamPointerHit& lv1 = l1[hitTarget[best]];
		e->levels = 2; e->base = hits[best].loc;
		e->off1 = lv1.loc - hits[best].value;
		e->yawAddr = yaw - lv1.value;
		LearnLog("pointer: 2 levels, [[%08x] + %x] + %x (%u candidate path(s))", (unsigned int)e->base,
			(unsigned int)e->off1, (unsigned int)e->yawAddr, (unsigned int)nh);
	}
	const int32_t rel = pitch ? CamViewDistance(yaw, pitch) : 0;   // same object through any window
	// 0x4000: Rogue Warrior's pitch is 0x101C below its yaw (2026-11-03).
	e->pitchAddr = (pitch && rel > -0x4000 && rel < 0x4000) ? (uint32_t)rel : 0;
	// Check: the path leads to the camera right now.
	uint32_t ry = 0, rp = 0;
	if (!CamResolve(*e, LearnRead32Cb, 0, &ry, &rp) || ry != yaw) {
		LearnLog("pointer: check failed (%08x instead of %08x)", (unsigned int)ry, (unsigned int)yaw);
		return false;
	}
	return true;
}

static void CameraLearnRunInner() {
	const CamLearnLimits lim = CamDefaultLimits();
	const uint32_t title = g_learnTitle;
	const DWORD tStart = GetTickCount();
	LearnLogStart(title);
	uint32_t base = 0, size = 0, stamp = 0; uint8_t* nt = 0;
	if (!GetTitleImage(&base, &size, &stamp, &nt)) {
		LearnLog("running executable not found");
		LearnNotify(STR_LEARN_FAILED);
		return;
	}
	g_learnImageBase = base; g_learnImageSize = size;
	LearnLog("title %08x image %08x size %08x build %08x", (unsigned int)title,
		(unsigned int)base, (unsigned int)size, (unsigned int)stamp);

	// 1. The executable's writable sections.
	g_learnNumRanges = 0;
	if (nt && MmIsAddressValid(nt)) {
		uint32_t hdrBytes = LearnValidPrefix((uint32_t)nt, 4096);
		static uint8_t hdr[4096];
		if (hdrBytes >= 64 && LearnCopy(hdr, (uint32_t)nt, hdrBytes))
			g_learnNumRanges = CamParseWritableSections(hdr, hdrBytes, base, g_learnRanges, 16);
	}
	if (g_learnNumRanges == 0) {
		LearnLog("section table not understood - using the whole image");
		g_learnRanges[0].start = base; g_learnRanges[0].size = size; g_learnNumRanges = 1;
	}
	g_learnNumImageRanges = g_learnNumRanges;
	uint32_t imageBytes = 0;
	for (size_t i = 0; i < g_learnNumRanges; i++) imageBytes += g_learnRanges[i].size;

	// 2. The game's heap: every writable, cacheable page outside the executables.
	size_t nr = g_learnNumRanges;
	nr = CamBuildRanges(LearnQueryProtect, 0, 0x00010000u, 0x40000000u, 0x1000,  g_learnRanges, nr, LEARN_MAX_RANGES);
	nr = CamBuildRanges(LearnQueryProtect, 0, 0x40000000u, 0x80000000u, 0x10000, g_learnRanges, nr, LEARN_MAX_RANGES);
	nr = CamBuildRanges(LearnQueryProtect, 0, 0xA0000000u, 0xE0000000u, 0x10000, g_learnRanges, nr, LEARN_MAX_RANGES);
	nr = CamBuildRanges(LearnQueryProtect, 0, 0xE0000000u, 0,           0x1000,  g_learnRanges, nr, LEARN_MAX_RANGES);
	g_learnNumRanges = nr;
	uint32_t heapBytes = 0;
	for (size_t i = g_learnNumImageRanges; i < nr; i++) {
		if (heapBytes + g_learnRanges[i].size > LEARN_MAX_HEAP)
			g_learnRanges[i].size = heapBytes < LEARN_MAX_HEAP ? LEARN_MAX_HEAP - heapBytes : 0;
		heapBytes += g_learnRanges[i].size;
	}
	g_learnTotal = imageBytes + heapBytes;
	LearnLog("memory: executable %u KB in %u range(s), heap %u KB in %u range(s)%hs",
		(unsigned int)(imageBytes / 1024), (unsigned int)g_learnNumImageRanges,
		(unsigned int)(heapBytes / 1024), (unsigned int)(nr - g_learnNumImageRanges),
		nr >= LEARN_MAX_RANGES ? " (range list full)" : "");

	// Block buffer: as big as the game leaves us.
	g_learnBlockBytes = 2u * 1024u * 1024u;
	g_learnBefore = 0;
	while (!g_learnBefore && g_learnBlockBytes >= 64u * 1024u) {
		g_learnBefore = (float*)malloc(g_learnBlockBytes);
		if (!g_learnBefore) g_learnBlockBytes /= 2;
	}
	if (!g_learnBefore) {
		LearnLog("no memory for the block buffer");
		LearnNotify(STR_LEARN_FAILED);
		return;
	}
	LearnLog("blocks of %u KB, room for %u candidates", (unsigned int)(g_learnBlockBytes / 1024),
		(unsigned int)LEARN_MAX_CANDIDATES);

	const int go = LearnBox(WTr(STR_LEARN_BOX_START), 2, STR_LEARN_BTN_START, STR_LEARN_BTN_STOP, STR_LEARN_BTN_STOP);
	if (go != 0 && go != -2) { LearnLog("not started"); LearnNotify(STR_LEARN_CANCELED); return; }
	LearnNotify(STR_LEARN_START);
	g_learnInject = 1;
	LearnStick(0, 0);
	if (!LearnWait(1200)) { LearnNotify(STR_LEARN_CANCELED); return; }
	if ((GetTickCount() - g_learnHookTick) > 1000) {
		LearnLog("no controller is being read by the game (not in gameplay?)");
		LearnResult(STR_LEARN_NO_INPUT);
		return;
	}

	bool aborted = false;
	size_t n = LearnSearchAxis(false, lim, &aborted);
	if (aborted) { LearnStick(0, 0); LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
	if (n == 0) {
		LearnLog("no yaw found - %u ms", (unsigned int)(GetTickCount() - tStart));
		LearnStick(0, 0);
		LearnNotifyInt3(STR_LEARN_STATS_H_FMT, (int)g_learnStatScan, (int)g_learnStatChecked, 0);
		LearnWait(3000);                           // let the counters be read first
		LearnResult(STR_LEARN_NOT_FOUND);
		return;
	}
	// --- 1. Horizontal: confirmed with the mouse before going any further ---
	// Only values the game KEEPS when we write them can be driven by the mouse
	// (Metro log 2026-10-08: 42 candidates, none kept - the player was asked
	// 8 times with a frozen camera). Persistent ones only; if there are none,
	// two tries at most, then an honest message.
	static CamCandidate yawCand[8];
	size_t tries = 0, persistentCount = 0;
	for (size_t i = 0; i < n && tries < 8; i++)
		if (g_learnCand[i].persistent) { yawCand[tries++] = g_learnCand[i]; persistentCount++; }
	if (tries == 0)
		for (size_t i = 0; i < n && tries < 2; i++) yawCand[tries++] = g_learnCand[i];
	LearnLog("horizontal: %u persistent candidate(s), %u to try", (unsigned int)persistentCount, (unsigned int)tries);
	g_learnInject = 0;
	LearnStick(0, 0);
	LearnNotifyInt3(STR_LEARN_STATS_H_FMT, (int)g_learnStatScan, (int)g_learnStatChecked, (int)n);
	if (!LearnWait(2500)) { LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }

	LearnedCamera e;
	memset(&e, 0, sizeof(e));
	e.titleId = title; e.timeStamp = stamp; e.imageSize = size;
	e.levels = 0; e.signY = 1;
	_snprintf(e.name, sizeof(e.name) - 1, "Title %08X", (unsigned int)title);
	bool yawOk = false;
	for (size_t k = 0; k < tries && !yawOk; k++) {
		const CamCandidate& c = yawCand[k];
		e.yawAddr = c.addr; e.pitchAddr = 0;           // vertical stays on the stick for now
		e.signX = c.sign;
		e.unit = (uint8_t)CamGuessUnit(c.firstDelta * (1000.0f / (float)LEARN_WINDOW_MS) * 0.22f);
		LearnLog("horizontal: trying %08x sign %d unit %hs", (unsigned int)e.yawAddr, (int)e.signX,
			e.unit == CAM_UNIT_RADIANS ? "rad" : "deg");
		g_learnTest = e;
		MemoryBarrier();
		g_learnAnswer = 0;
		g_learnTestActive = 1;
		g_learnState = LEARN_CONFIRM;
		const int answer = LearnAsk(STR_LEARN_CONFIRM_FMT, STR_LEARN_BOX_TEST_H_FMT, STR_LEARN_BOX_ASK_H, (int)k + 1, (int)tries);
		g_learnState = LEARN_RUNNING;
		g_learnTestActive = 0;
		if (answer == 0) { LearnLog("no answer - canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
		if (answer == 1) { yawOk = true; LearnLog("horizontal confirmed: %08x", (unsigned int)e.yawAddr); }
		else LearnLog("horizontal rejected: %08x", (unsigned int)c.addr);
	}
	if (!yawOk) {
		// No horizontal camera: no point looking for the vertical one.
		LearnLog("no horizontal camera confirmed - stop");
		LearnResult(persistentCount == 0 ? STR_LEARN_RECOMPUTED : STR_LEARN_NOT_FOUND);
		return;
	}

	// --- 2. Vertical, only now ---
	const int goV = LearnBox(WTr(STR_LEARN_BOX_VERTICAL), 2, STR_LEARN_BTN_OK, STR_LEARN_BTN_STOP, STR_LEARN_BTN_STOP);
	if (goV == -2) LearnNotify(STR_LEARN_VERTICAL_START);
	else if (goV != 0) { LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
	if (!LearnWait(1500)) { LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
	g_learnInject = 1;
	static CamCandidate pitchCand[8];
	size_t np = 0;
	int8_t signY = 1;
	const uint32_t nearPitch = LearnFindPitchNear(e.yawAddr, &signY, lim, &aborted);
	if (aborted) { LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
	if (nearPitch) {
		memset(&pitchCand[0], 0, sizeof(pitchCand[0]));
		pitchCand[0].addr = nearPitch; pitchCand[0].sign = signY;
		np = 1;
		g_learnStatScan = 8; g_learnStatChecked = 1;
	} else {
		LearnLog("pitch not next to the yaw - full vertical search");
		const size_t found = LearnSearchAxis(true, lim, &aborted);
		if (aborted) { LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
		np = 0;
		for (size_t i = 0; i < found && np < 8; i++) if (g_learnCand[i].persistent) pitchCand[np++] = g_learnCand[i];
		if (np == 0) for (size_t i = 0; i < found && np < 2; i++) pitchCand[np++] = g_learnCand[i];
	}
	g_learnInject = 0;
	LearnStick(0, 0);
	LearnNotifyInt3(STR_LEARN_STATS_V_FMT, (int)g_learnStatScan, (int)g_learnStatChecked, (int)np);
	if (!LearnWait(2500)) { LearnLog("canceled"); LearnNotify(STR_LEARN_CANCELED); return; }

	bool pitchOk = false;
	for (size_t k = 0; k < np && !pitchOk; k++) {
		e.pitchAddr = pitchCand[k].addr;
		e.signY = pitchCand[k].sign;
		LearnLog("vertical: trying %08x sign %d", (unsigned int)e.pitchAddr, (int)e.signY);
		g_learnTest = e;
		MemoryBarrier();
		g_learnAnswer = 0;
		g_learnTestActive = 1;
		g_learnState = LEARN_CONFIRM;
		const int answer = LearnAsk(STR_LEARN_CONFIRM_V_FMT, STR_LEARN_BOX_TEST_V_FMT, STR_LEARN_BOX_ASK_V, (int)k + 1, (int)np);
		g_learnState = LEARN_RUNNING;
		g_learnTestActive = 0;
		if (answer == 0) { LearnLog("no answer - canceled"); LearnNotify(STR_LEARN_CANCELED); return; }
		if (answer == 1) { pitchOk = true; LearnLog("vertical confirmed: %08x", (unsigned int)e.pitchAddr); }
		else LearnLog("vertical rejected: %08x", (unsigned int)e.pitchAddr);
	}
	if (!pitchOk) {
		e.pitchAddr = 0; e.signY = 1;              // horizontal only: vertical stays on the stick
		LearnLog("no vertical camera confirmed - horizontal only");
	}
	LearnLog("search took %u ms", (unsigned int)(GetTickCount() - tStart));

	// --- 3. Permanent address, then save ---
	const bool inExe = e.yawAddr >= base && e.yawAddr < base + size;
	bool permanent = inExe;
	if (!inExe) {
		LearnNotify(STR_LEARN_SAVING);
		LearnedCamera p = e;
		if (LearnFindPointerPath(e.yawAddr, e.pitchAddr, &p)) { e = p; permanent = true; }
	}
	if (permanent) {
		const bool ok = SaveLearnedCamera(e);
		LearnLog(ok ? "saved (%d pointer level(s))" : "FAILED to save (%d pointer level(s))", (int)e.levels);
		if (ok) LoadLearnedCameras();
		LearnResult(!ok ? STR_LEARN_FAILED : e.pitchAddr ? STR_LEARN_SAVED : STR_LEARN_SAVED_H);
	} else {
		g_learnTest = e;
		MemoryBarrier();
		g_learnTestActive = 2;                      // this session only, until the game changes
		LearnLog("no permanent address found - active for this session only");
		LearnResult(STR_LEARN_SESSION_ONLY);
	}
}

// Memory for the duration of the search only.
static void CameraLearnRun() {
	size_t cap = 65536;
	while (cap >= 4096) {
		g_learnCand = (CamCandidate*)malloc(cap * sizeof(CamCandidate));
		g_learnNow = g_learnCand ? (float*)malloc(cap * sizeof(float)) : 0;
		if (g_learnCand && g_learnNow) break;
		free(g_learnCand); free(g_learnNow);
		g_learnCand = 0; g_learnNow = 0;
		cap /= 2;
	}
	if (!g_learnCand) {
		FileLog("Camera search: no memory for the candidates");
		LearnNotify(STR_LEARN_FAILED);
		return;
	}
	LEARN_MAX_CANDIDATES = cap;
	CameraLearnRunInner();
	free(g_learnBefore); g_learnBefore = 0; g_learnBlockBytes = 0;
	free(g_learnCand); free(g_learnNow);
	g_learnCand = 0; g_learnNow = 0;
	LEARN_MAX_CANDIDATES = 0;
}

DWORD WINAPI CameraLearnThreadProc(LPVOID) {
	__try {
		CameraLearnRun();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		FileLog("Camera search: EXCEPTION %08x - stopped", (unsigned int)GetExceptionCode());
		LearnNotify(STR_LEARN_FAILED);
	}
	if (g_learnCand) { free(g_learnCand); g_learnCand = 0; }   // after an exception inside the search
	if (g_learnNow)  { free(g_learnNow);  g_learnNow = 0; }
	LEARN_MAX_CANDIDATES = 0;
	if (g_learnBefore) { free(g_learnBefore); g_learnBefore = 0; g_learnBlockBytes = 0; }
	LearnStick(0, 0);
	g_learnInject = 0;
	if (g_learnTestActive == 1) g_learnTestActive = 0;   // 2 = session camera, kept until the game changes
	MemoryBarrier();
	g_learnState = LEARN_IDLE;
	return 0;
}

// Polling thread, every tick: keys, start, notifications.
static void CameraLearnPoll(int keyCode) {
	static int s_hold = 0;
	static bool s_triggered = false;
	static int s_prevKey = -1;
	const bool edge = (keyCode != s_prevKey);
	s_prevKey = keyCode;

	if (g_learnMsgPending) {
		g_learnMsgPending = 0;
		MemoryBarrier();
		FlushLogBuffer();   // disk BEFORE XNotifyUI, same precaution as everywhere else
		XNotifyUI(XNOTIFYUI_CUSTOM, g_learnMsg);
	}
	if (g_learnedNotifyPending) {
		g_learnedNotifyPending = 0;
		const int i = g_learnedNotifyIdx;
		if (i >= 0 && i < g_learnedCount) {
			FileLog("Direct aim ready (learned camera): %hs", g_learned[i].name);
			FlushLogBuffer();
			wchar_t msg[160];
			swprintf(msg, 160, WTr(STR_MOUSEHOOK_ON_FMT), g_learned[i].name);
			XNotifyUI(XNOTIFYUI_CUSTOM, msg);
		}
	}

	// Session-only camera (no permanent address): dropped when the game changes.
	if (g_learnTestActive == 2 && g_lastTitleId != g_learnTitle) {
		g_learnTestActive = 0;
		FileLog("Camera search: session camera dropped (game changed)");
	}
	const int st = g_learnState;
	if (st == LEARN_IDLE) {
		if (keyCode == LEARN_KEY_START) {
			if (++s_hold >= LEARN_HOLD_TICKS && !s_triggered) {
				s_triggered = true;
				if (g_lastTitleId == 0) {
					XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_LEARN_NEED_GAME));
				} else {
					FileLog("Camera search: started for title %08x", (unsigned int)g_lastTitleId);
					g_learnTestActive = 0;          // a new search replaces a session camera
					g_learnTitle = g_lastTitleId;
					g_learnAbort = 0;
					g_learnAnswer = 0;
					MemoryBarrier();
					g_learnState = LEARN_RUNNING;
					MakeThread((LPTHREAD_START_ROUTINE)CameraLearnThreadProc, nullptr);
				}
			}
		} else {
			s_hold = 0;
			s_triggered = false;
		}
		return;
	}
	if (g_lastTitleId != g_learnTitle) g_learnAbort = 1;
	if (edge && keyCode == LEARN_KEY_NEXT) {
		if (st == LEARN_CONFIRM) g_learnAnswer = 2;
		else g_learnAbort = 1;
	}
	if (edge && keyCode == LEARN_KEY_START && st == LEARN_CONFIRM) g_learnAnswer = 1;
}
#endif // EN_CAMERA_LEARN

// Index de la souris vid/pid encore branchee, -1 sinon (debranchee entre le
// reglage et l'enregistrement : on n'ecrit rien).
static int FindConnectedMouseIndex(uint16_t vid, uint16_t pid) {
	for (int i = 0; i < 4; i++)
		if (connectedControllers[i].controllerDriver && connectedControllers[i].isMouse &&
			connectedControllers[i].vendorId == vid && connectedControllers[i].productId == pid)
			return i;
	return -1;
}

// Applique les surcharges en cours si elles concernent le jeu affiche.
static void ApplyLiveOverrides(EffectiveMouseSettings& e, uint32_t titleId) {
#if EN_LIVE_TUNING
	const uint32_t t = g_liveTitle;
	if (t == 0 || t != titleId) return;
	MemoryBarrier();
	const int32_t s = g_liveSens, dz = g_liveDz, adz = g_liveAdz, adzY = g_liveAdzY, sm = g_liveSm;
	if (s > 0)    e.mouseSensitivity = s;
	if (dz >= 0)  e.deadzone = dz;
	const bool yFollowsX = (e.antiDeadzoneY == e.antiDeadzone);   // vertical non regle a part
	if (adz >= 0) e.antiDeadzone = adz;
	if (adzY >= 0) e.antiDeadzoneY = adzY;
	else if (adz >= 0 && yFollowsX) e.antiDeadzoneY = adz;
	if (sm >= 0)  e.smoothingMs = sm;
#else
	(void)e; (void)titleId;
#endif
}

// Jalon 7 (suite, 2026-08-02) - "liaison rapide" in-game : associe UN bouton
// de la VRAIE manette physique (branchee a cote de la souris/clavier - PAS
// celle emulee par ce plugin, voir ReadRealControllerState plus bas) a un
// bouton souris ou une touche clavier, SANS sortir du jeu ni afficher de menu
// - retour utilisateur reel (associer RB au clic droit UNIQUEMENT pour un
// jeu donne, sans repasser par l'ecran Profils a chaque partie). Represente
// la cible a la fois en ButtonsReport::* (pour lier une souris, meme
// representation que kMouseButtonTargets dans application.xex) et en
// KeyAction (pour lier une touche clavier, voir HidKeyMapEntry::action dans
// mapping.h) - les deux schemas coexistent deja dans ce projet pour des
// raisons historiques differentes, d'ou cette table qui donne les deux
// representations d'un coup : on ne sait pas encore, au moment de capturer
// la cible, si la SOURCE captures ensuite sera un bouton souris ou une
// touche clavier. Back/Start volontairement ABSENTS de cette liste : ce sont
// les deux boutons qui DECLENCHENT le mode liaison lui-meme (maintenus
// ensemble ~1.5s), les reassigner via ce meme flux serait ambigu et
// dangereux (on ne pourrait plus jamais redeclencher le mode liaison).
struct RealButtonTarget {
    uint16_t xinputMask;                 // bit dans XINPUT_GAMEPAD.wButtons, ignore si gachette (voir isLeftTrigger/isRightTrigger)
    bool isLeftTrigger;
    bool isRightTrigger;
    uint8_t ButtonsReport::* field;
    uint8_t keyAction;                   // KeyAction (mapping.h)
    const wchar_t* label;                // pour la notification XNotifyUI
};

static const RealButtonTarget kRealButtonTargets[] = {
    { XINPUT_GAMEPAD_A,              false, false, &ButtonsReport::a_button, KEYACT_A,  L"A" },
    { XINPUT_GAMEPAD_B,              false, false, &ButtonsReport::b_button, KEYACT_B,  L"B" },
    { XINPUT_GAMEPAD_X,              false, false, &ButtonsReport::x_button, KEYACT_X,  L"X" },
    { XINPUT_GAMEPAD_Y,              false, false, &ButtonsReport::y_button, KEYACT_Y,  L"Y" },
    { XINPUT_GAMEPAD_LEFT_SHOULDER,  false, false, &ButtonsReport::l1,       KEYACT_LB, L"LB" },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, false, false, &ButtonsReport::r1,       KEYACT_RB, L"RB" },
    { XINPUT_GAMEPAD_LEFT_THUMB,     false, false, &ButtonsReport::l3,       KEYACT_L3, L"L3" },
    { XINPUT_GAMEPAD_RIGHT_THUMB,    false, false, &ButtonsReport::r3,       KEYACT_R3, L"R3" },
    { 0,                             true,  false, &ButtonsReport::l2,       KEYACT_LT, L"LT" },
    { 0,                             false, true,  &ButtonsReport::r2,       KEYACT_RT, L"RT" },
    // D-Pad ajoute le 2026-08-03 (demande explicite HB : "j'ai besoin du
    // D-pad en liaison rapide"). Meme mecanisme que les autres cibles (front
    // montant sur le bit XINPUT correspondant) - aucun traitement special
    // requis, XINPUT_GAMEPAD_DPAD_* sont des bits normaux de wButtons.
    { XINPUT_GAMEPAD_DPAD_UP,        false, false, &ButtonsReport::dpad_up,    KEYACT_DPAD_UP,    L"D-Pad Haut" },
    { XINPUT_GAMEPAD_DPAD_DOWN,      false, false, &ButtonsReport::dpad_down,  KEYACT_DPAD_DOWN,  L"D-Pad Bas" },
    { XINPUT_GAMEPAD_DPAD_LEFT,      false, false, &ButtonsReport::dpad_left,  KEYACT_DPAD_LEFT,  L"D-Pad Gauche" },
    { XINPUT_GAMEPAD_DPAD_RIGHT,     false, false, &ButtonsReport::dpad_right, KEYACT_DPAD_RIGHT, L"D-Pad Droite" },
    // Back/Start restent volontairement ABSENTS - voir le commentaire au-dessus
    // de la struct RealButtonTarget. HB a demande "toutes les touches sans
    // exception" (2026-08-03) - point signale explicitement en reponse plutot
    // que tranche unilateralement, voir PROJECT_NOTES.md.
};
static const int kRealButtonTargetCount = sizeof(kRealButtonTargets) / sizeof(kRealButtonTargets[0]);

// Seuil au-dela duquel une gachette analogique (0..255) compte comme
// "pressee" pour ce flux - meme ordre de grandeur que le seuil XInput
// standard (XINPUT_GAMEPAD_TRIGGER_THRESHOLD = 30), un peu plus haut pour
// eviter un appui accidentel/leger pendant qu'on cherche le bon bouton.
static const BYTE REAL_TRIGGER_PRESS_THRESHOLD = 60;

// Index dans kRealButtonTargets du PREMIER bouton dont l'etat passe de
// relache a presse entre previous et current (front montant), ou -1 si
// aucun. L'appelant compare deux lectures successives de la VRAIE manette.
int FindNewlyPressedRealButton(const XINPUT_GAMEPAD* previous, const XINPUT_GAMEPAD* current) {
    for (int i = 0; i < kRealButtonTargetCount; i++) {
        const RealButtonTarget& t = kRealButtonTargets[i];
        if (t.isLeftTrigger) {
            bool wasDown = previous->bLeftTrigger >= REAL_TRIGGER_PRESS_THRESHOLD;
            bool isDown = current->bLeftTrigger >= REAL_TRIGGER_PRESS_THRESHOLD;
            if (isDown && !wasDown) return i;
        } else if (t.isRightTrigger) {
            bool wasDown = previous->bRightTrigger >= REAL_TRIGGER_PRESS_THRESHOLD;
            bool isDown = current->bRightTrigger >= REAL_TRIGGER_PRESS_THRESHOLD;
            if (isDown && !wasDown) return i;
        } else {
            bool wasDown = (previous->wButtons & t.xinputMask) != 0;
            bool isDown = (current->wButtons & t.xinputMask) != 0;
            if (isDown && !wasDown) return i;
        }
    }
    return -1;
}

// Key held on ANY keyboard interface (2026-10-11, Razer Tartarus V2: it has
// two keyboard interfaces and sends its keys on the SECOND one; only the
// first was read for the Studio, the wizard and the shortcuts, so "no key
// does anything" in X360RemapStudio while the same keys worked in a game).
static uint8_t AnyKeyboardKeyCode() {
	for (int i = 0; i < 4; i++)
		if (connectedControllers[i].controllerDriver && connectedControllers[i].isKeyboard &&
		    !connectedControllers[i].isExtraKeyboardIf &&
		    connectedControllers[i].lastKeyCode != 0xFF)
			return connectedControllers[i].lastKeyCode;
	return 0xFF;
}
static uint8_t AnyKeyboardModifiers() {
	uint8_t m = 0;
	for (int i = 0; i < 4; i++)
		if (connectedControllers[i].controllerDriver && connectedControllers[i].isKeyboard &&
		    !connectedControllers[i].isExtraKeyboardIf)
			m |= connectedControllers[i].lastModifiers;
	return m;
}

// Cherche la VRAIE manette physique (pas celle emulee par ce plugin - voir
// Controller::userIndex, deja assigne via XamUserBindDeviceCallback pour nos
// propres peripheriques HID, voir InitStage6 plus haut) parmi les 4 slots
// XInput. Utilise XInputGetState() directement (PAS XInputdReadStateDetour,
// qui n'intercepte que les appels dont dwDeviceContext correspond a un
// device EMULE par ce plugin - un simple user index 0..3 comme celui-ci n'y
// correspond jamais) - meme appel deja utilise avec succes ailleurs dans ce
// fichier pour lire un vrai etat manette (voir la gestion de
// XInputGetCapabilities un peu plus bas dans ce fichier). Renvoie false si
// aucune manette reelle n'est trouvee sur un slot que ce plugin n'occupe pas
// deja lui-meme.
bool ReadRealControllerState(XINPUT_GAMEPAD* out, uint8_t* outUserIndex) {
    for (uint8_t candidate = 0; candidate < 4; candidate++) {
        bool isOurs = false;
        for (int i = 0; i < 4; i++) {
            if (connectedControllers[i].controllerDriver && connectedControllers[i].userIndex == candidate) {
                isOurs = true;
                break;
            }
        }
        if (isOurs)
            continue;

        XINPUT_STATE state;
        memset(&state, 0, sizeof(XINPUT_STATE));
        // Our own read: the merged-players code must not add the keyboard /
        // mouse here (it would use up the mouse movement from this thread).
        g_pollRealRead = true;
        const DWORD gs = XInputGetState(candidate, &state);
        g_pollRealRead = false;
        if (gs == ERROR_SUCCESS) {
            *out = state.Gamepad;
            *outUserIndex = candidate;
            return true;
        }
    }
    return false;
}

// Etats de la state machine de liaison rapide (voir MappingManagerThreadProc
// plus bas pour la logique elle-meme, qui tourne au meme rythme ~100ms/tick
// que SAVE_PROFILE_HOLD_TICKS ci-dessus). PAS de menu/rendu a l'ecran a
// aucune de ces etapes - uniquement des notifications XNotifyUI (comme F9),
// ce qui evite tout le risque deja identifie pour un vrai overlay in-game
// (Jalon 11, PROJECT_NOTES.md - hook D3D9 dans le moteur d'un jeu qu'on ne
// controle pas).
enum QuickBindState {
    QUICKBIND_IDLE = 0,       // rien en cours - on surveille juste Back+Start sur la vraie manette
    QUICKBIND_ARMED,          // Back+Start maintenus ~1.5s - attend le bouton CIBLE sur la vraie manette
    QUICKBIND_WAITING_SOURCE, // cible capturee - attend la SOURCE (touche clavier ou clic souris)
};

const int QUICKBIND_ARM_HOLD_TICKS = 15;  // ~1.5s a 100ms/tick, meme duree que F9
const DWORD QUICKBIND_TIMEOUT_MS = 10000; // annule silencieusement une etape laissee en plan (voir plus bas)
QuickBindState g_quickBindState = QUICKBIND_IDLE;
int g_quickBindArmHoldTicks = 0;
bool g_quickBindArmTriggered = false; // empeche un re-armement tant que Back+Start restent enfonces
int g_quickBindTargetIdx = -1;        // index dans kRealButtonTargets une fois la cible capturee
XINPUT_GAMEPAD g_quickBindPrevRealState = {}; // derniere lecture de la vraie manette, pour detecter un front montant
DWORD g_quickBindDeadlineMs = 0;      // GetTickCount() au-dela duquel on annule (QUICKBIND_TIMEOUT_MS apres le dernier pas franchi)

// Deferred USB re-enumeration - see MappingManagerThreadProc. Set to 0 to
// disable and go back to "unplug/replug after boot".
#define DEFERRED_USB_RESET 0   // 1.4: no USB reset, see docs/USB_RESET.md
DWORD g_titleStableSince = 0;
bool  g_deferredResetDone = false;

// --- USB boot report (1.4, READ ONLY, 2026-11-16) ---------------------------
// Devices plugged in at power-on are enumerated before this plugin loads, so
// HidAddDeviceHook never sees them and the player must replug once. Two fixes
// exist elsewhere and both need facts about those devices first:
//  - Jacckii (Xbox360-Series-Controllers-Native, GPL-3): ordinal 890 re-offers
//    ports parked in the unrecognized table. Tried 2026-11-16: 0 parked ports on
//    HB's console, so a boot keyboard / mouse is not there. Removed.
//  - usb-audio-x360 (TimeoutTim, GPL-3, src/driver.cpp ReenumerateBootDevice):
//    resets ONE root-hub port of a device the kernel rejected, only if it is the
//    only device on that controller.
// This report only READS kernel 17559 tables and logs, per controller and port,
// the device node, its VID:PID and the controller state usb-audio-x360 checks.
// It resets nothing and sends nothing to any device.
#define K_UNRECOGNIZED_PORTS  0x801A7FA0   // {hub node, port mask} x 20, kernel 17559 (Jacckii)
#define K_UNRECOGNIZED_COUNT  20
#define K_USB_DEVICE_STATE    0x801A8220   // per controller state, kernel 17559 (usb-audio-x360)
#define K_USB_HCD_TABLE       0x801A8230   // per controller HCD, kernel 17559 (usb-audio-x360)
#define K_RESET_ROOT_PORT     0x800D7D20   // root-hub port reset routine (checked; called only for ports listed in usb_port_reset.txt)
bool g_dashSinceLoad = false;    // a "back to dashboard" title switch was seen since load
bool g_usbRescanDone = false;    // the report ran this boot
// Port map (2026-11-18, HB: reset only the front ports, never the Wi-Fi): after the
// boot report, the report runs again ~2 s after a device arrives (HidAddDevice), or
// when usb_report.txt is put in the Studio folder (deleted once done). Read only.
volatile bool  g_usbReportWanted = false;
volatile DWORD g_usbReportWantedAt = 0;

// Called once from the polling thread when the dashboard is settled.
static void RunGentleUsbRescan() {
	HANDLE kernel = NULL;
	PVOID rootFn = NULL, portFn = NULL;
	if (XexGetModuleHandle("xboxkrnl.exe", &kernel) < 0 || !kernel) { FileLog("USB report: no kernel handle - skipped"); return; }
	XexGetProcedureAddress(kernel, 880, &rootFn);   // UsbdGetRootHubDeviceNode(controller)
	XexGetProcedureAddress(kernel, 881, &portFn);   // UsbdGetPortDeviceNode(root, port)
	const WORD build = XboxKrnlVersion->Build;
	if (build != 17559 || !rootFn || !portFn || !UsbdGetDeviceDescriptor) {
		FileLog("USB report: kernel %u is not the analysed 17559 layout - skipped", (unsigned)build);
		return;
	}

	int parked = 0;
	for (int i = 0; i < K_UNRECOGNIZED_COUNT; i++)
		if (*(volatile DWORD*)(K_UNRECOGNIZED_PORTS + i * 8)) parked++;
	const volatile DWORD* rr = (const volatile DWORD*)K_RESET_ROOT_PORT;
	const bool resetLayout = rr[0] == 0x7D8802A6 && rr[1] == 0x9181FFF8 && rr[2] == 0xFBE1FFF0 && rr[3] == 0x9421FFA0;
	FileLog("USB report: %d parked port(s), port reset routine %s", parked, resetLayout ? "as expected" : "DIFFERENT");

	typedef deviceHandle* (*RootFn)(DWORD);
	typedef deviceHandle* (*PortFn)(deviceHandle*, DWORD);
	for (DWORD c = 0; c < 4; c++) {
		BYTE* hcd = ((BYTE**)K_USB_HCD_TABLE)[c];
		BYTE* st  = ((BYTE**)K_USB_DEVICE_STATE)[c];
		deviceHandle* root = ((RootFn)rootFn)(c);
		if (!hcd && !root) continue;
		// The fields usb-audio-x360 checks before a port reset: flag, state->hcd link,
		// hcd->root link, the two state words that are 0 when no driver took the
		// device, the cached descriptor of the last device seen there.
		FileLog("USB report: controller %u hcd=%08x flag=%02x pool=%08x root=%08x/%08x state=%08x link=%d w4=%08x w8=%08x cached=%04x:%04x",
			c, (DWORD)hcd, hcd ? hcd[0x96] : 0, hcd ? *(DWORD*)(hcd + 0x40) : 0,
			(DWORD)root, hcd ? *(DWORD*)(hcd + 0x48) : 0, (DWORD)st,
			(st && hcd) ? (*(BYTE**)st == hcd) : 0,
			st ? *(DWORD*)(st + 4) : 0, st ? *(DWORD*)(st + 8) : 0,
			st ? swap_endianness_16(*(WORD*)(st + 0x4C + 8)) : 0, st ? swap_endianness_16(*(WORD*)(st + 0x4C + 10)) : 0);
		if (!root) continue;
		for (DWORD p = 0; p < 8; p++) {
			deviceHandle* dev = ((PortFn)portFn)(root, p);
			if (!dev) continue;
			// shortcut: per Jacckii the descriptor getter may return the last enumerated
			// device instead of this node's; the VID:PID here is a hint, not proof.
			usb_device_descriptor* d = UsbdGetDeviceDescriptor(dev);
			FileLog("USB report:   port %u node=%08x vid:pid=%04x:%04x class=%02x", p, (DWORD)dev,
				d ? swap_endianness_16(d->idVendor) : 0, d ? swap_endianness_16(d->idProduct) : 0, d ? d->bDeviceClass : 0);
		}
	}
	FileLog("USB report: done (read only, nothing was reset) - replug devices plugged at power-on");
	FlushLogBuffer();
}
// --- Port reset at boot, opt-in (2026-11-18, HB) ------------------------------
// A keyboard / mouse plugged at power-on is taken by the console's own driver
// before this plugin loads; resetting ITS root-hub port makes it enumerate again,
// through our HidAddDevice hook. Same call as usb-audio-x360 (TimeoutTim, GPL-3,
// src/driver.cpp ReenumerateBootDevice): ResetRootHubPort(root node, port), from a
// thread, kernel 17559 only, routine prologue checked. Only the ports listed in
// usb_port_reset.txt ("controller:port" per line, e.g. "3:0") are touched, one every
// 3 s, once per boot: the port map differs between consoles (HB's Slim: front =
// 3:0 and 3:1, rear = 2:0 2:2 2:3, Wi-Fi on controller 1). No file = nothing reset.
// An empty port is skipped.
#define K_NODE_POOL_BYTES 0x1000
static DWORD g_portResetList[8][2];
static int   g_portResetCount = -1;      // -1 = file not read yet
static int   g_portResetNext = 0;
static DWORD g_portResetAt = 0;

static bool InUsbNodePool(const BYTE* pool, const void* address) {
	const BYTE* v = (const BYTE*)address;
	return pool && v >= pool && v < pool + K_NODE_POOL_BYTES && (((DWORD)(v - pool) & 0x0F) == 0);
}

static void ReadPortResetList() {
	g_portResetCount = 0;
	char path[128];
	StudioPath(path, sizeof(path), "usb_port_reset.txt");
	std::ifstream f(path);
	if (!f.is_open()) return;
	std::string line;
	while (std::getline(f, line) && g_portResetCount < 8) {
		unsigned c = 9, pt = 9;
		if (sscanf(line.c_str(), "%u:%u", &c, &pt) == 2 && c < 4 && pt < 8) {
			g_portResetList[g_portResetCount][0] = c;
			g_portResetList[g_portResetCount][1] = pt;
			g_portResetCount++;
		}
	}
	FileLog("USB port reset: %d port(s) listed in usb_port_reset.txt", g_portResetCount);
}

// One listed port per call; true while ports remain.
static bool ResetNextListedPort() {
	if (g_portResetNext >= g_portResetCount) return false;
	const DWORD c = g_portResetList[g_portResetNext][0], pt = g_portResetList[g_portResetNext][1];
	g_portResetNext++;
	HANDLE kernel = NULL;
	PVOID rootFn = NULL, portFn = NULL;
	if (XexGetModuleHandle("xboxkrnl.exe", &kernel) < 0 || !kernel) return false;
	XexGetProcedureAddress(kernel, 880, &rootFn);
	XexGetProcedureAddress(kernel, 881, &portFn);
	const volatile DWORD* rr = (const volatile DWORD*)K_RESET_ROOT_PORT;
	if (XboxKrnlVersion->Build != 17559 || !rootFn || !portFn || !UsbdGetDeviceDescriptor ||
		!(rr[0] == 0x7D8802A6 && rr[1] == 0x9181FFF8 && rr[2] == 0xFBE1FFF0 && rr[3] == 0x9421FFA0)) {
		FileLog("USB port reset %u:%u: not the analysed 17559 kernel - skipped", c, pt);
		g_portResetNext = g_portResetCount;
		return false;
	}
	typedef deviceHandle* (*RootFn)(DWORD);
	typedef deviceHandle* (*PortFn)(deviceHandle*, DWORD);
	typedef VOID (*ResetFn)(deviceHandle*, DWORD);
	BYTE* hcd = ((BYTE**)K_USB_HCD_TABLE)[c];
	deviceHandle* root = ((RootFn)rootFn)(c);
	deviceHandle* dev = root ? ((PortFn)portFn)(root, pt) : NULL;
	BYTE* pool = hcd ? *(BYTE**)(hcd + 0x40) : NULL;
	if (!hcd || !root || !dev || *(deviceHandle**)(hcd + 0x48) != root ||
		!InUsbNodePool(pool, root) || !InUsbNodePool(pool, dev)) {
		FileLog("USB port reset %u:%u: no device there (or tables not as expected) - skipped", c, pt);
		return g_portResetNext < g_portResetCount;
	}
	// No VID test: the descriptor read here is the controller's LAST enumerated device
	// (2026-11-18: the Rapoo's port read 045e:02aa, the internal device of port 3), so
	// only the user's list decides. The Wi-Fi port must simply not be listed.
	FileLog("USB port reset %u:%u: resetting node %08x", c, pt, (DWORD)dev);
	FlushLogBuffer();
	((ResetFn)K_RESET_ROOT_PORT)(root, pt);
	FileLog("USB port reset %u:%u: done", c, pt);
	FlushLogBuffer();
	return g_portResetNext < g_portResetCount;
}

// Defined further down (set by initFunctionPointers), but needed by the polling
// thread above it.
extern DWORD UsbPhysicalPage;

// Hot reload of X360Remap.json - see MappingManagerThreadProc.
//
// Change detection uses CreateFile + GetFileTime/GetFileSize rather than the
// CRT's stat(): std::ifstream copes with Xbox device paths like "HDD:\..." but
// stat() is not reliable with them, which is why the first attempt at hot
// reload silently never fired. Size is compared as well as the timestamp, since
// a file pushed over XBDM/FTP may keep a coarse or unchanged timestamp.
FILETIME g_lastJsonWriteTime = {0, 0};
DWORD    g_lastJsonSize = 0;
bool     g_jsonStateKnown = false;
DWORD    g_lastJsonCheck = 0;

// Last write time and size of one file of the Studio folder.
static bool GetStudioFileStamp(const char* fileName, FILETIME* outTime, DWORD* outSize) {
	char jsonPath[128];
	StudioPath(jsonPath, sizeof(jsonPath), fileName);
	HANDLE h = CreateFileA(jsonPath, GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	bool ok = GetFileTime(h, nullptr, nullptr, outTime) != 0;
	*outSize = GetFileSize(h, nullptr);
	CloseHandle(h);
	return ok;
}

bool GetJsonFileStamp(FILETIME* outTime, DWORD* outSize) {
	return GetStudioFileStamp("X360Remap.json", outTime, outSize);
}

// Apres une ecriture faite PAR LE PLUGIN LUI-MEME (F9, liaison rapide) :
// memorise le nouvel horodatage, pour que le rechargement a chaud ne prenne
// pas cette ecriture pour une modification exterieure (AUDIT_2026-09.md, I4).
// Sans ca : un second rechargement inutile 2 s plus tard, avec sa propre
// notification "X360Remap.json recharge" par-dessus "profil enregistre".
static void RememberOwnJsonWrite() {
	FILETIME ft; DWORD sz = 0;
	if (GetJsonFileStamp(&ft, &sz)) {
		g_lastJsonWriteTime = ft;
		g_lastJsonSize = sz;
		g_jsonStateKnown = true;
	}
}

// Echecs de lecture CONSECUTIFS du rechargement a chaud (AUDIT I4). La
// notification "fichier invalide" n'est affichee qu'a partir du deuxieme :
// un premier echec isole est presque toujours une lecture tombee pendant
// que la Studio ecrivait le fichier.
int g_jsonReloadFailures = 0;

// --- input_state.json seulement quand la Studio tourne (AUDIT_2026-09.md, I1)
//
// Avant : ce fichier etait reecrit 10 fois par seconde des qu'un clavier ou
// une souris etait branche, PENDANT TOUTE LA SESSION, jeu compris - soit
// 36 000 ecritures par heure de jeu, alors que seule la Studio le lit. Sur
// une console sans disque dur (BadUpdate : memoire interne ou cle USB),
// c'est de l'usure de memoire flash pour rien.
//
// Mecanisme, sans dependre du Title ID de la Studio (inconnu : son xex.xml
// n'en declare pas) :
//   - la Studio cree studio_active.flag au demarrage, et le recree toutes
//     les 2 s s'il a disparu ;
//   - le plugin SUPPRIME ce fichier a chaque changement de titre : lancer un
//     jeu, c'est forcement que la Studio n'est plus au premier plan ;
//   - le plugin n'ecrit input_state.json que si le fichier est present.
// Pire cas si quelque chose se passe mal : la Studio recree le fichier dans
// les 2 s. Et avec une ANCIENNE Studio (qui ne cree jamais ce fichier), le
// repli garde l'ancien comportement : voir g_studioFlagEverSeen.
//
// 0 = comportement d'avant (ecriture permanente).
#define EN_FIX_INPUT_STATE_ONLY_WITH_STUDIO 1

volatile bool g_studioActive = false;
static volatile uint32_t g_padButtonsHeld = 0;   // see HidFillButtonsReport
// Tant qu'aucune Studio n'a JAMAIS pose ce fichier depuis le demarrage, on
// ecrit comme avant : une Studio ancienne reste donc pleinement fonctionnelle
// avec ce plugin, elle ne profite simplement pas de l'economie d'ecritures.
bool g_studioFlagEverSeen = false;

static void StudioFlagPath(char* out, int outSize) {
	StudioPath(out, outSize, "studio_active.flag");
}

static void ClearStudioFlagOnTitleChange() {
#if EN_FIX_INPUT_STATE_ONLY_WITH_STUDIO
	char path[128];
	StudioFlagPath(path, sizeof(path));
	DeleteFileA(path);
	g_studioActive = false;
#endif
}

usb_hid_descriptor hidDescriptorBuffer;
int globalIndex = -1;
void* reportDescriptorBuffer;
MappingState g_mappingState;

int interruptHandler(DWORD deviceHandle, int32_t a2);
bool DetectIsMouse(HID_ReportInfo_t* info, uint8_t reportId);
bool DetectIsKeyboard(HID_ReportInfo_t* info);
bool DetectIsGamepad(HID_ReportInfo_t* info);

// Keep every IN report item; the driver uses all axes, the hat, and buttons.
bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const CurrentItem) {
	return (CurrentItem->ItemType == HID_REPORT_ITEM_In);
}

HID_ReportItem_t* FindItemByUsage(
	HID_ReportInfo_t* info,
	uint16_t usagePage,
	uint16_t usage,
	uint8_t  reportId) {
	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;
		if (item->Attributes.Usage.Page != usagePage)
			continue;
		if (item->Attributes.Usage.Usage != usage)
			continue;
		// When the device uses report IDs, only match the right report.
		if (info->UsingReportIDs && item->ReportID != reportId)
			continue;
		return item;
	}
	return nullptr;
}

HID_ReportItem_t* FindButtonItem(
	HID_ReportInfo_t* info,
	uint8_t buttonIdx,
	uint8_t reportId) {
	return FindItemByUsage(info, HID_USAGE_PAGE_BUTTON, buttonIdx + 1, reportId);
}

// --- Clavier generique (chemin descripteur, PREMIER JET NON TESTE SUR
// MATERIEL, 2026-09-10) -----------------------------------------------------
//
// Meme principe que FindButtonItem juste au-dessus, applique a la page
// d'usage clavier (0x07) au lieu de la page bouton (0x09). Verifie
// directement dans hid_parser.cpp (HID_RI_INPUT, ligne ~327) : quand un
// champ est declare via Usage Minimum/Maximum (pas une liste Usage()
// explicite - c'est la convention standard pour un tableau de touches ou un
// octet de modificateurs), le parseur assigne a CHAQUE HID_ReportItem_t
// genere une usage qui INCREMENTE a partir de Usage Minimum
// (`NewReportItem.Attributes.Usage.Usage = UsageMinMax.Minimum++`). Pour un
// clavier standard, Usage Minimum vaut 0x00 sur la page 0x07 (0x00 = "aucune
// touche", reserve expres pour cet usage par la USB HID Usage Tables,
// section clavier/pave numerique) : le Nieme emplacement du tableau de
// touches porte donc l'usage N, exactement comme le Nieme bouton porte
// l'usage boutonIdx+1 sur sa propre page. Meme logique pour l'octet de
// modificateurs (LCtrl/LShift/.../RGui), presque toujours declare separement
// avec Usage Minimum 0xE0 : chaque bit genere porte l'usage 0xE0+bit.
//
// Ce n'est PAS garanti a 100% - un clavier qui declare ses touches via une
// liste Usage() explicite plutot qu'un Minimum/Maximum ne suivrait pas cette
// regle - mais c'est la convention very largement dominante (confirmee par
// recherche le 2026-09-10) et c'est la meme methode deja utilisee sans
// probleme depuis longtemps pour les boutons de manette dans ce fichier.
HID_ReportItem_t* FindKeySlotItem(
	HID_ReportInfo_t* info,
	uint8_t slotIdx,
	uint8_t reportId) {
	return FindItemByUsage(info, HID_USAGE_PAGE_KEYBOARD_KEYPAD, slotIdx, reportId);
}

HID_ReportItem_t* FindModifierBitItem(
	HID_ReportInfo_t* info,
	uint8_t bit,
	uint8_t reportId) {
	return FindItemByUsage(info, HID_USAGE_PAGE_KEYBOARD_KEYPAD, 0xE0 + bit, reportId);
}

// Find the report ID that carries gamepad information
// Returns 0 when the device doesn't use report IDs.
uint8_t FindGamepadReportId(HID_ReportInfo_t* info) {
	if (!info->UsingReportIDs)
		return 0;

	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;
		if (item->Attributes.Usage.Page != HID_USAGE_PAGE_GENERIC_DESKTOP)
			continue;
		uint16_t u = item->Attributes.Usage.Usage;
		if (u >= HID_USAGE_AXIS_X && u <= HID_USAGE_AXIS_RZ)
			return item->ReportID;
	}
	return 0;
}

// Meme principe que FindGamepadReportId juste au-dessus, page d'usage
// clavier (0x07) au lieu de bureau generique (0x01). NECESSAIRE (ajoutee
// 2026-09-10, PREMIER JET NON TESTE SUR MATERIEL) : un clavier pur n'a AUCUN
// axe de bureau generique, donc FindGamepadReportId renvoie toujours 0 pour
// lui, meme quand son vrai ReportID est different - ce qui aurait fait
// echouer silencieusement tous les FindItemByUsage/FindKeySlotItem de
// HidFillDescriptorKeyboardState (leur filtre ReportID ne correspondrait
// jamais). Voir son appel juste apres c.isKeyboard dans
// setConfigurationComplete.
uint8_t FindKeyboardReportId(HID_ReportInfo_t* info) {
	if (!info->UsingReportIDs)
		return 0;

	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;
		if (item->Attributes.Usage.Page == HID_USAGE_PAGE_KEYBOARD_KEYPAD)
			return item->ReportID;
	}
	return 0;
}

void SendControlRequest(
	deviceHandle* deviceHandle,
	UsbControlTrb* controlTrb,
	uint8_t bmRequestType,
	uint8_t bRequest,
	uint16_t wValue,
	uint16_t wIndex,
	uint16_t wLength,
	void* data,
	DWORD completionCallback) {
	controlTrb->packet.bmRequestType = bmRequestType;
	controlTrb->packet.bRequest = bRequest;
	controlTrb->packet.wValue = swap_endianness_16(wValue);
	controlTrb->packet.wIndex = swap_endianness_16(wIndex);
	controlTrb->packet.wLength = swap_endianness_16(wLength);
	controlTrb->trb.buffer = data;
	controlTrb->trb.length = wLength;
	controlTrb->trb.flags = 1;
	controlTrb->trb.callback = completionCallback;
	controlTrb->trb.savedEndpoint = controlTrb->trb.endpoint;
	UsbdQueueAsyncTransfer(deviceHandle, controlTrb);
}

void SendInterruptRequest(
	deviceHandle* deviceHandle,
	UsbTrb* interruptTrb,
	void* data,
	uint32_t length,
	DWORD completionCallback) {
	interruptTrb->buffer = data;
	interruptTrb->length = length;
	interruptTrb->flags = 1;
	interruptTrb->callback = completionCallback;
	interruptTrb->savedEndpoint = interruptTrb->endpoint;
	UsbdQueueAsyncTransfer(deviceHandle, interruptTrb);
}

int32_t noopCompleteHandler(DWORD deviceHandle, int32_t status) {
	return 0;
}

// Completion of a close request (clean unplug, 2026-10-12). Runs in the USB
// completion context: only counters here, no log flush, no USB call. The
// argument is the address of the close request itself.
static void CloseRequestDone(HidControllerExtension* drv, int which, int32_t status) {
	if (!drv) return;
	(void)which; (void)status;   // NO FileLog here: USB completion context (see Controller::keyRaw)
	if (InterlockedDecrement(&drv->closePending) <= 0) {
		drv->closeDoneAt = GetTickCount() | 1;   // never 0
	}
}
int32_t CloseDataEpComplete(DWORD request, int32_t status) {
	CloseRequestDone((HidControllerExtension*)(request - offsetof(HidControllerExtension, closeReq)), 0, status);
	return 0;
}
int32_t CloseDefaultEpComplete(DWORD request, int32_t status) {
	CloseRequestDone((HidControllerExtension*)(request - offsetof(HidControllerExtension, closeReq) - sizeof(UsbTrb)), 1, status);
	return 0;
}

// --- Forcage du Boot Protocol : debrayable (2026-08-03) -------------------
// Le SET_PROTOCOL(boot) ajoute plus tot dans la journee a repare le curseur
// d'une souris (VID:046a PID:b092) mais a fait perdre la MOLETTE sur les
// DEUX souris testees. Cause suspectee : le protocole boot du HID ne definit
// officiellement que 3 octets (boutons, X, Y) - la molette est une extension
// de fait, qu'un peripherique n'est pas tenu d'emettre une fois bascule dans
// ce mode. Comme la requete est envoyee a TOUS les peripheriques sans
// exception, elle enleve la molette a tout le monde, y compris a ceux qui
// n'avaient aucun probleme.
//
// Plutot que de trancher a l'aveugle entre "curseur casse sur certaines
// souris" et "molette perdue sur toutes", le forcage devient debrayable a
// chaud : un fichier HDD:\X360RemapStudio\force_boot_protocol.txt contenant
// "0" desactive l'envoi. Absent ou contenant autre chose -> comportement
// actuel conserve (envoi), donc aucune regression pour qui ne touche a rien.
//
// Interet immediat : permet de tester les DEUX etats avec un seul build, en
// deplacant un fichier au lieu de recompiler - le cycle build/deploiement/
// test sur vraie console est le goulot d'etranglement de ce projet.
// A terme, ce reglage a vocation a rejoindre la cascade global -> peripherique
// comme option de compatibilite exposee dans l'application.
// PREUVE (2026-08-03, 425 releves sur une session complete) : avec le boot
// protocol force, le rapport de la souris fait toujours 4 octets mais son 4e
// octet - la molette - reste a ZERO en permanence. Le device continue d'emettre
// la bonne longueur, il n'y met simplement plus la molette. Le mode boot la
// supprime donc reellement, sur les deux souris testees.
//
// D'ou un mode AUTOMATIQUE, qui n'envoie la requete qu'aux peripheriques qui
// en ont besoin, decide sur la taille de paquet de l'endpoint :
//   - paquet <= 4 octets : la disposition native coincide deja avec celle que
//     BootMouseReport suppose (boutons/X/Y/molette). Forcer n'apporte rien et
//     coute la molette -> on n'envoie PAS. C'est le comportement d'avant le
//     2026-08-03, connu comme fonctionnel sur ces souris-la.
//   - paquet > 4 octets : la disposition native est inconnue et l'ancien
//     symptome (curseur qui ne bougeait qu'en vertical sur VID:046a PID:b092,
//     pkt:8) prouve qu'elle ne correspond pas -> on envoie, pour obtenir au
//     moins un curseur et des boutons corrects. Cette souris-la perd la
//     molette, mais elle ne l'avait de toute facon jamais eue exploitable.
//
// Recuperer aussi la molette sur ces devices-la demanderait de lire et
// interpreter leur descripteur de rapport HID (le "chemin lent", a l'origine
// des gels historiques documentes plus haut) - hors sujet ici, a traiter comme
// un chantier a part si le besoin se confirme.
//
// Le fichier reste prioritaire sur l'automatisme, comme echappatoire pour un
// modele exotique : "0" = ne jamais envoyer, "1" = toujours envoyer.
enum BootProtocolMode { BOOTPROTO_AUTO = 0, BOOTPROTO_ALWAYS = 1, BOOTPROTO_NEVER = 2 };
int g_bootProtocolMode = BOOTPROTO_AUTO;

static void LoadBootProtocolSetting() {
	char path[128];
	StudioPath(path, sizeof(path), "force_boot_protocol.txt");
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		FileLog("force_boot_protocol.txt missing - SET_PROTOCOL(boot) in AUTO mode (sent only if packet > 4 bytes)");
		return;
	}
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	for (size_t i = 0; i < content.size(); i++) {
		if (content[i] == '0') { g_bootProtocolMode = BOOTPROTO_NEVER;  break; }
		if (content[i] == '1') { g_bootProtocolMode = BOOTPROTO_ALWAYS; break; }
	}
	FileLog("force_boot_protocol.txt read - SET_PROTOCOL(boot) forced to %hs",
		g_bootProtocolMode == BOOTPROTO_NEVER ? "NEVER" : "ALWAYS");
}

// Decide, pour UN peripherique, s'il faut lui envoyer la requete.
//
// CERCLE VICIEUX CORRIGE (2026-08-03, question de l'utilisateur : "pourquoi la
// molette n'a pas d'empreinte detectee ?"). Le mode AUTO envoyait la requete a
// toute souris a paquet > 4 octets - c'est-a-dire exactement celles que
// l'assistant de calibration est cense sauver. Or le mode boot SUPPRIME la
// molette (prouve sur 425 releves : le 4e octet reste a zero en permanence).
// L'assistant observait donc un flux dont la molette avait deja ete retiree en
// amont : il ne pouvait rien y trouver, et ces souris ne pouvaient jamais etre
// calibrees.
//
// Desormais un peripherique DEJA CALIBRE n'est plus jamais force : sa
// disposition apprise rend le forcage inutile, c'etait tout l'objet de
// l'assistant. Le forcage ne subsiste que comme filet de securite pour les
// souris a paquet long PAS ENCORE calibrees, pour qu'elles aient au moins un
// curseur exploitable en attendant.
//
// Pour la toute PREMIERE calibration d'une telle souris, il faut neanmoins
// l'observer dans son format natif : c'est le role de force_boot_protocol.txt
// a "0" (l'ecran de calibration propose de l'ecrire lui-meme). Le curseur sera
// peut-etre inutilisable pendant ce temps - sans importance, l'assistant se
// pilote a la manette, decision prise precisement pour ce cas de figure.
// REVISE le 2026-08-03 : on ne force plus rien par defaut. Le descripteur de
// rapport, desormais recupere par peripherique (voir Controller::reportDescBuf),
// donne la vraie position de X, Y ET de la molette - forcer le mode boot
// detruirait precisement la molette qu'il permet de retrouver. Le forcage ne
// subsiste donc qu'en mode explicitement demande par l'utilisateur
// (force_boot_protocol.txt = "1"), comme repli si un modele exotique se
// comportait mal avec son propre descripteur.
static bool ShouldForceBootProtocol(uint16_t vendorId, uint16_t productId, uint16_t pktSize) {
	(void)vendorId; (void)productId; (void)pktSize;
	return g_bootProtocolMode == BOOTPROTO_ALWAYS;
}

// Callback purement diagnostique pour la requete SET_PROTOCOL (voir le point
// d'appel dans HidAddDeviceHook, chemin "boot-protocol mice/keyboards",
// 2026-08-03). Envoyee "fire-and-forget" : rien dans le reste du flux
// d'initialisation n'attend cette completion, donc cette fonction ne fait
// jamais planter/bloquer quoi que ce soit - seulement un FileLog en cas
// d'echec, pour garder une trace exploitable si un modele de souris/clavier
// precis pose probleme (des milliers d'utilisateurs potentiels, un nombre
// de modeles jamais testes ici). Ne touche AUCUNE variable globale partagee
// (c/g_InitState/globalIndex/*DescriptorBuffer) - contrairement a l'ancienne
// chaine lente qui causait les gels documentes dans PROJECT_NOTES.md.
// --- Chaine descripteur PAR PERIPHERIQUE (2026-08-03) ---------------------
// Voir les champs reportDescBuf/reportDescReady dans Controller pour le
// pourquoi. Regle absolue respectee ici : ces callbacks tournent dans le
// contexte USB, ils ne font QUE des ecritures de champs simples. Aucun
// parsing, aucune allocation, aucune E/S - le parsing (lourd) est fait par le
// thread de polling, seul contexte sur de ce fichier.
// ATTENTION - piege majeur, cause de l'echec du 2026-08-04 : le premier
// parametre d'un callback de completion N'EST PAS le handle du peripherique.
// C'est un pointeur situe 36 octets a l'interieur de la structure
// HidControllerExtension, et tout le code d'origine le sait deja :
//
//   HidControllerExtension* drv = (HidControllerExtension*)((BYTE*)deviceHandle - 36);
//
// (voir setConfigurationComplete). Ma premiere version comparait ce parametre
// au vrai handle stocke dans Controller::deviceHandle : la comparaison
// echouait TOUJOURS, les callbacks sortaient immediatement sans rien
// enregistrer, et le plugin concluait au bout de 3 s a une "absence de
// reponse" alors que le descripteur etait bel et bien arrive.
//
// On retrouve donc le Controller par son extension, seule correspondance
// fiable - meme convention que le reste du fichier.
static Controller* FindControllerByHandle(DWORD deviceHandle) {
	HidControllerExtension* drv = (HidControllerExtension*)((BYTE*)deviceHandle - 36);
	for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
		if (connectedControllers[i].controllerDriver == drv)
			return &connectedControllers[i];
	}
	return nullptr;
}

// True when a HID report descriptor uses Report IDs (global item 0x85).
// Walks the items properly (a data byte equal to 0x85 is not a match).
// Plain loop, no CRT: safe in a USB completion callback.
static bool DescHasReportId(const uint8_t* d, uint16_t len) {
	uint16_t i = 0;
	while (i < len) {
		uint8_t b = d[i];
		if (b == 0xFE) {                       // long item: FE, size, tag, data
			if (i + 1 >= len) break;
			i = (uint16_t)(i + 3 + d[i + 1]);
			continue;
		}
		uint8_t sz = (uint8_t)(b & 0x03);
		if (sz == 3) sz = 4;
		if ((b & 0xFC) == 0x84) return true;   // Report ID
		i = (uint16_t)(i + 1 + sz);
	}
	return false;
}

int32_t SetBootProtocolComplete(DWORD deviceHandle, int32_t status);

// Step 4 (2026-10-29, HB's Rapoo 24AE:2015 dongle): a mouse read with its own
// descriptor is NOT always in report protocol. The Rapoo mouse interface kept
// sending boot-format reports (buttons, X, Y, wheel - no Report ID byte) while
// its descriptor declares Report IDs, so the native reader took the X byte for
// the buttons (random clicks, no motion). When the descriptor uses Report IDs,
// ask for the report protocol explicitly, so the reports match the descriptor.
// Last link of the chain, same controlTrb, nothing queued after it.
static void RequestReportProtocolIfNeeded(Controller* ctrl) {
	if (!ctrl || !ctrl->reportDescReady || ctrl->reportDescLen == 0)
		return;
	if (g_bootProtocolMode == BOOTPROTO_ALWAYS)
		return;
	if (!DescHasReportId(ctrl->reportDescBuf, ctrl->reportDescLen))
		return;
	ctrl->reportProtocolRequested = true;
	SendControlRequest(ctrl->deviceHandle, &ctrl->controllerDriver->controlTrb,
		0x21 /* Host->Device, Class, Interface */, 0x0B /* SET_PROTOCOL */,
		1 /* wValue: 1 = Report Protocol */, ctrl->interfaceNumber,
		0, nullptr, (DWORD)SetBootProtocolComplete);
}

// Etape 3 : le nom du peripherique est arrive. Simple drapeau, la conversion
// UTF-16 -> ASCII se fait dans le thread de polling.
int32_t BootProductNameComplete(DWORD deviceHandle, int32_t status) {
	Controller* ctrl = FindControllerByHandle(deviceHandle);
	if (!ctrl)
		return 0;
	if (!NT_ERROR(status))
		ctrl->nameReady = true;
	else
		ctrl->nameParsed = true; // echec : on n'essaiera plus, le VID:PID sera affiche
	RequestReportProtocolIfNeeded(ctrl);
	return 0;
}

// Etape 2 : le descripteur de rapport est arrive (ou a echoue). On leve le
// drapeau, puis on enchaine la demande du NOM du peripherique sur le meme
// controlTrb - c'est le dernier maillon de la chaine, rien ne suit.
int32_t BootReportDescriptorComplete(DWORD deviceHandle, int32_t status) {
	Controller* ctrl = FindControllerByHandle(deviceHandle);
	if (!ctrl)
		return 0;
	if (NT_ERROR(status))
		ctrl->reportDescFailed = true;
	else
		ctrl->reportDescReady = true;

	// wValue = 0x0300 | index (type STRING), wIndex = identifiant de langue.
	// 0x0409 (anglais US) est celui que declarent la quasi-totalite des
	// peripheriques ; en demander la liste exacte ajouterait un aller-retour
	// pour un gain nul en pratique. Si le peripherique ne le supporte pas, la
	// requete echoue et on retombe simplement sur le VID:PID.
	if (ctrl->productStringIndex != 0) {
		// ctrl->deviceHandle plutot que le parametre `deviceHandle` : ce
		// dernier est un DWORD (signature imposee par le callback), alors que
		// SendControlRequest attend un vrai pointeur. Le Controller le detient
		// deja typé - inutile de caster, et ca evite le piege de nommage
		// deviceHandle/struct deviceHandle documente plus bas.
		SendControlRequest(ctrl->deviceHandle, &ctrl->controllerDriver->controlTrb,
			0x80 /* Device->Host, Standard, Device */, 0x06 /* GET_DESCRIPTOR */,
			(uint16_t)(0x0300 | ctrl->productStringIndex), 0x0409,
			sizeof(ctrl->nameBuf), ctrl->nameBuf, (DWORD)BootProductNameComplete);
	} else {
		ctrl->nameParsed = true;
		RequestReportProtocolIfNeeded(ctrl);   // no name request: last link here
	}
	return 0;
}

// Etape 1 : le descripteur HID est arrive, il donne la longueur du descripteur
// de rapport. On enchaine la seconde requete sur le MEME controlTrb, propre a
// ce peripherique - c'est ce qui rend la chaine sans danger, contrairement a
// l'originale qui passait par des buffers globaux partages.
int32_t BootHidDescriptorComplete(DWORD deviceHandle, int32_t status) {
	Controller* ctrl = FindControllerByHandle(deviceHandle);
	if (!ctrl)
		return 0;
	if (NT_ERROR(status)) {
		ctrl->reportDescFailed = true;
		return 0;
	}

	uint16_t len = swap_endianness_16(ctrl->hidDesc.wDescriptorLength);
	// Longueur aberrante ou trop grande pour notre tampon fixe : on abandonne
	// proprement et le peripherique reste en mode boot. Pas d'allocation ici -
	// on est dans le contexte USB.
	if (len == 0 || len > sizeof(ctrl->reportDescBuf)) {
		ctrl->reportDescFailed = true;
		return 0;
	}
	ctrl->reportDescLen = len;

	// ctrl->deviceHandle et non le parametre `deviceHandle` : celui-ci est un
	// DWORD impose par la signature du callback, alors que SendControlRequest
	// attend un pointeur. Le Controller le detient deja correctement type.
	// wIndex = NUMERO D'INTERFACE, pas zero. Une requete GET_DESCRIPTOR dont
	// le destinataire est une interface (bmRequestType 0x81) doit dire
	// LAQUELLE. Passer zero ne fonctionne que par chance, pour un peripherique
	// dont l'interface utile porte justement le numero 0 - tous les autres
	// refusent la requete. Cause reelle de l'echec du premier essai sur
	// console (2026-08-04) : aucun descripteur recupere, donc aucun nom et
	// aucune molette.
	SendControlRequest(ctrl->deviceHandle, &ctrl->controllerDriver->controlTrb,
		0x81, 0x06, 0x2200, ctrl->interfaceNumber,
		len, ctrl->reportDescBuf, (DWORD)BootReportDescriptorComplete);
	return 0;
}

int32_t SetBootProtocolComplete(DWORD deviceHandle, int32_t status) {
	// Utilisait la meme comparaison erronee que FindControllerByHandle avant
	// le 2026-08-04 (voir son commentaire) : il ne trouvait donc jamais le
	// peripherique. Comme il ne logue qu'en cas d'echec, le defaut est passe
	// inapercu - un SET_PROTOCOL refuse n'aurait jamais ete signale.
	if (NT_ERROR(status)) {
		Controller* ctrl = FindControllerByHandle(deviceHandle);
		if (ctrl) {
			FileLog("SET_PROTOCOL (boot) failed for VID:%04x PID:%04x (status %x) - this device may send a non-standard packet format (buttons/motion possibly corrupted)",
				ctrl->vendorId, ctrl->productId, (unsigned int)status);
		}
	}
	return 0;
}

// Descriptor path, second half handed to the polling thread (see
// FinishDescriptorInit). One device at a time (g_deviceInitBusy).
volatile LONG g_descReady = 0;
HidControllerExtension* volatile g_descDriver = nullptr;
// Report descriptor buffer: static, so the USB completion callback never calls
// calloc / free (was calloc'ed in INIT_GET_HID_DESCRIPTOR).
static uint8_t g_reportDescStatic[4096];

int32_t setConfigurationComplete(DWORD deviceHandle, int32_t status) {
	HidControllerExtension* controllerDriver = (HidControllerExtension*)((BYTE*)deviceHandle - 36);
	SafeDbgPrint("X360Remap: Control transfer completed.\n");

	// A failed control transfer was never checked: the chain went on with an
	// empty or garbage buffer (2026-10-23).
	if (status < 0 && (g_InitState == InitState::INIT_SET_CONFIGURATION ||
	                   g_InitState == InitState::INIT_GET_HID_DESCRIPTOR ||
	                   g_InitState == InitState::INIT_GET_REPORT_DESCRIPTOR)) {
		FileLog("Init stage %d: control transfer failed (%x) for VID:%04x PID:%04x - giving up",
			(int)g_InitState, (unsigned int)status, c.vendorId, c.productId);
		g_InitState = InitState::INIT_FAILED;
		g_deviceInitBusy = false;
		return 0;
	}
	if (g_InitState == InitState::INIT_SET_CONFIGURATION) {
		g_InitState = InitState::INIT_GET_HID_DESCRIPTOR;
		SafeDbgPrint("X360Remap: Init Stage 1. SET_CONFIGURATION completed successfully\r\n");
		FileLog("InitStage1 SET_CONFIGURATION ok, requesting HID descriptor (interface %d)", (int)c.interfaceNumber);
		// wIndex = NUMERO D'INTERFACE (AUDIT_2026-09.md, C5). Etait 0 en dur :
		// exactement le bug corrige le 2026-08-04 sur le chemin rapide (voir
		// BootHidDescriptorComplete, "passer zero ne fonctionne que par
		// chance"), jamais reporte ici. Un clavier composite dont l'interface
		// utile n'est pas la n°0 - cas courant des claviers gaming - voyait sa
		// requete refusee. Inchange pour une interface n°0.
		SendControlRequest(
			controllerDriver->deviceHandle,
			&controllerDriver->controlTrb,
			0x81,
			0x06,
			0x2100,
			c.interfaceNumber,
			sizeof(usb_hid_descriptor),
			&hidDescriptorBuffer,
			(DWORD)setConfigurationComplete);
	}
	else if (g_InitState == InitState::INIT_GET_HID_DESCRIPTOR) {
		hidDescriptorBuffer.wDescriptorLength = swap_endianness_16(hidDescriptorBuffer.wDescriptorLength);
		SafeDbgPrint("X360Remap: Init Stage 2. Get hid descriptor: %x:%x completed successfully\r\n",
			hidDescriptorBuffer.bLength, hidDescriptorBuffer.wDescriptorLength);
		FileLog("InitStage2 HID descriptor ok, len=%d, requesting report descriptor",
			hidDescriptorBuffer.wDescriptorLength);
		g_InitState = InitState::INIT_GET_REPORT_DESCRIPTOR;

		// Longueur fournie par le PERIPHERIQUE, jamais verifiee jusqu'ici
		// (AUDIT_2026-09.md, C5) : 0 ou une valeur enorme partaient telles
		// quelles dans calloc. Le chemin rapide plafonne a 512 octets ; ici,
		// ou passent aussi des manettes a descripteur long, 4096 laisse une
		// large marge. Hors bornes ou allocation ratee : abandon propre, le
		// peripherique retourne au gestionnaire d'origine au prochain branchement.
		if (hidDescriptorBuffer.wDescriptorLength == 0 || hidDescriptorBuffer.wDescriptorLength > 4096) {
			FileLog("InitStage2 absurd descriptor length (%d) - giving up", (int)hidDescriptorBuffer.wDescriptorLength);
			g_InitState = InitState::INIT_FAILED;
			g_deviceInitBusy = false;
			return 0;
		}
		memset(g_reportDescStatic, 0, sizeof(g_reportDescStatic));
		reportDescriptorBuffer = g_reportDescStatic;   // no heap here (USB completion context)

		SendControlRequest(
			controllerDriver->deviceHandle,
			&controllerDriver->controlTrb,
			0x81,
			0x06,
			0x2200,
			c.interfaceNumber,   // idem Stage 1 (AUDIT C5)
			hidDescriptorBuffer.wDescriptorLength,
			reportDescriptorBuffer,
			(DWORD)setConfigurationComplete);
	}
	else if (g_InitState == InitState::INIT_GET_REPORT_DESCRIPTOR) {
		// Report descriptor received. Everything else (parsing - dozens of
		// heap allocations -, classification, opening the interrupt endpoint,
		// XAM registration, buffer allocation) now runs in the POLLING THREAD
		// (2026-10-23): HB's 0079:0006 pad froze the console only with the
		// plugin, and only through this path (disable_generic_hid.txt = no
		// freeze). This callback runs in the USB completion context, where
		// the C runtime heap must not be used (same lesson as the 2026-10-12
		// key-press freeze) - the boot path already parsed in the polling thread.
		FileLog("InitStage3 report descriptor received - finishing in the polling thread");
		g_descDriver = controllerDriver;
		MemoryBarrier();
		g_descReady = 1;
	}

	return 0;
}

// Second half of the descriptor-path init (see INIT_GET_REPORT_DESCRIPTOR
// above), called from MappingManagerThreadProc only.
static int32_t FinishDescriptorInit(HidControllerExtension* controllerDriver) {
	NTSTATUS status = 0;
	SafeDbgPrint("X360Remap: Init Stage 3. INIT_GET_REPORT_DESCRIPTOR completed successfully %x\r\n",
		*(DWORD*)reportDescriptorBuffer);
	FileLog("InitStage3 report descriptor received, parsing it now");

	// Parse HID descriptor
	HID_ReportInfo_t* reportInfo = nullptr;
	uint8_t parseResult = USB_ProcessHIDReport((const uint8_t*)reportDescriptorBuffer,
		hidDescriptorBuffer.wDescriptorLength,
		&reportInfo);

	if (parseResult != HID_PARSE_Successful || !reportInfo) {
		SafeDbgPrint("X360Remap: Failed to parse HID descriptor: error %d\r\n", parseResult);
		FileLog("Failed to parse HID descriptor: error %d", parseResult);
		g_InitState = InitState::INIT_FAILED;
		g_deviceInitBusy = false;
		return -1;
	}

	SafeDbgPrint("X360Remap: parse done stage 1\r\n");
	g_InitState = InitState::INIT_DONE;

	c.reportInfo = reportInfo;
	c.reportId = FindGamepadReportId(reportInfo);
	c.isMouse = DetectIsMouse(reportInfo, c.reportId);
	c.mouseAccumX = 0;
	c.mouseAccumY = 0;
	c.mouseWheelAccum = 0;
	c.wheelPulseDir = 0;
	c.wheelPulseUntilMs = 0;
	c.cursorAccumX = 0;
	c.cursorAccumY = 0;
	c.smoothedVelX = 0.0f;
	c.smoothedVelY = 0.0f;
	// GetTickCount() maintenant, pas 0 - un premier calcul d'ecart avec 0
	// donnerait un "elapsedMs" enorme (des millions de ms) au tout premier
	// poll, ce qui ferait decroitre l'inertie a neant immediatement au
	// lieu de simplement demarrer a 0 (aucun mouvement accumule encore de
	// toute facon, donc l'effet est cosmetique, mais plus propre ainsi).
	c.lastVelocityUpdateMs = GetTickCount();
	// A keyboard has no relative X axis, so it will never be classified as a
	// mouse - the isMouse check here is just defensive, not a real conflict.
	c.isKeyboard = !c.isMouse && DetectIsKeyboard(reportInfo);

	// CORRECTIF (2026-09-10, PREMIER JET NON TESTE SUR MATERIEL) : c.reportId
	// a ete calcule plus haut par FindGamepadReportId, qui ne regarde QUE
	// la page bureau generique (axes) - toujours 0 pour un clavier pur,
	// meme quand son vrai ReportID est different. Sans cette correction,
	// HidFillDescriptorKeyboardState (voir plus haut, FindKeySlotItem/
	// FindModifierBitItem) filtrerait sur le mauvais ReportID et ne
	// trouverait jamais rien. Voir FindKeyboardReportId juste apres
	// FindGamepadReportId pour le detail.
	if (c.isKeyboard && reportInfo->UsingReportIDs) {
		c.reportId = FindKeyboardReportId(reportInfo);
	}

	SafeDbgPrint("X360Remap: Parsed descriptor. UsingReportIDs: %d, Report ID: %d, IsMouse: %d, IsKeyboard: %d\r\n",
		(int)reportInfo->UsingReportIDs, c.reportId, (int)c.isMouse, (int)c.isKeyboard);
	FileLog("Parsed descriptor. VID:%04x PID:%04x UsingReportIDs:%d ReportID:%d IsMouse:%d IsKeyboard:%d",
		c.vendorId, c.productId, (int)reportInfo->UsingReportIDs, c.reportId, (int)c.isMouse, (int)c.isKeyboard);


	// FILET DE SECURITE (2026-09-10, PREMIER JET NON TESTE SUR MATERIEL) :
	// necessaire depuis que la politique d'acceptation (voir
	// CLAIM_ONLY_SUPPORTED_DEVICES plus bas dans HidAddDeviceHook) laisse
	// passer jusqu'ici toute interface HID-class, plus seulement celles
	// avec un mapping deja connu. Un appareil qui s'avere n'etre ni
	// souris, ni clavier, ni manette a mapping connu n'a rien a faire
	// enregistre comme controleur virtuel - c'est exactement le cout
	// "extra player" que la politique de 2026-07-31 visait a eviter (voir
	// son commentaire). On relache proprement ICI, avant tout appel a
	// UsbdOpenEndpoint/XamUserBindDeviceCallback (donc avant tout effet
	// de bord), plutot que d'enregistrer un joueur fantome inutile.
	// g_deviceInitBusy est remis a false comme sur les autres sorties
	// anticipees de cette fonction (voir le bloc "Failed to parse HID
	// descriptor" juste au-dessus pour le meme motif).
	// Controllers known in advance (SDL Game Controller Database, see
	// known_controllers.h): mapped now, no controller assistant. Only when
	// nothing else maps it - X360Remap.json and the built-in mappings win.
	if (!c.isMouse && !c.isKeyboard && !c.map) {
		uint16_t descAxes[16];
		uint8_t nDescAxes = 0;
		for (HID_ReportItem_t* it = reportInfo->FirstReportItem; it && nDescAxes < 16; it = it->Next) {
			if (it->ItemType != HID_REPORT_ITEM_In) continue;
			if (it->Attributes.Usage.Page != HID_USAGE_PAGE_GENERIC_DESKTOP) continue;
			if (reportInfo->UsingReportIDs && it->ReportID != c.reportId) continue;
			descAxes[nDescAxes++] = it->Attributes.Usage.Usage;
		}
		const char* knownName = nullptr;
		c.map = AttachKnownController(c.vendorId, c.productId, descAxes, nDescAxes, &knownName);
		if (c.map) {
			FileLog("Known controller VID:%04x PID:%04x \"%s\" (SDL GameControllerDB) - %d axes, %d buttons mapped",
				c.vendorId, c.productId, knownName ? knownName : "?",
				(int)c.map->axisMapCount, (int)c.map->buttonMapCount);
		} else if (knownName) {
			FileLog("Known controller VID:%04x PID:%04x \"%s\" but the list is full - the controller assistant will start",
				c.vendorId, c.productId, knownName);
		}
	}

	if (!c.isMouse && !c.isKeyboard && !c.map && DetectIsGamepad(reportInfo)) {
		// Unknown controller: kept, the controller assistant starts from
		// the polling thread (it needs a device with reportInfo and no map).
		FileLog("Unknown controller VID:%04x PID:%04x - kept, the controller assistant will start",
			c.vendorId, c.productId);
	}
	if (!c.isMouse && !c.isKeyboard && !c.map && !DetectIsGamepad(reportInfo)) {
		FileLog("Descriptor parsed but VID:%04x PID:%04x is neither mouse, keyboard nor a controller - released without registering",
			c.vendorId, c.productId);
		// Le descripteur analyse n'etait jamais libere ici (AUDIT M4) :
		// une petite fuite a chaque branchement d'un appareil inconnu.
		USB_FreeReportInfo(reportInfo);
		c.reportInfo = nullptr;
		g_InitState = InitState::INIT_FAILED;
		g_deviceInitBusy = false;
		return 0;
	}

	usb_endpoint_descriptor* endpoint_descriptor = UsbdGetEndpointDescriptor(
		controllerDriver->deviceHandle, 0, USB_ENDPOINT_TYPE_INTERRUPT, USB_DIRECTION_IN);
	// Jamais verifie jusqu'ici (AUDIT M4) - le chemin rapide le fait deja.
	if (!endpoint_descriptor) {
		FileLog("VID:%04x PID:%04x has no interrupt IN endpoint - giving up", c.vendorId, c.productId);
		USB_FreeReportInfo(reportInfo);
		c.reportInfo = nullptr;
		g_InitState = InitState::INIT_FAILED;
		g_deviceInitBusy = false;
		return 0;
	}

	status = UsbdOpenEndpoint(
		controllerDriver->deviceHandle,
		3,
		endpoint_descriptor->bEndpointAddress,
		swap_endianness_16(endpoint_descriptor->wMaxPacketSize) & 0x7FF,
		endpoint_descriptor->bInterval,
		(DWORD*)&controllerDriver->interruptTrb);

	if (NT_ERROR(status)) {
		SafeDbgPrint("X360Remap: Failed to open interrupt endpoint %x!\n", status);
		FileLog("VID:%04x PID:%04x - opening the interrupt endpoint was refused (%x)", c.vendorId, c.productId, (unsigned int)status);
		USB_FreeReportInfo(reportInfo);   // fuite corrigee (AUDIT M4)
		c.reportInfo = nullptr;
		g_deviceInitBusy = false;
		return status;
	}

	controllerDriver->openedDataEp = controllerDriver->interruptTrb.endpoint;   // for the clean unplug
	FileLog("InitStage4 interrupt endpoint opened ok");

	uint16_t pktSize = swap_endianness_16(endpoint_descriptor->wMaxPacketSize) & 0x7FF;
	c.reportData = malloc(pktSize * 2);
	memset(c.reportData, 0, pktSize * 2);

	controllerDriver->interruptTrb.savedEndpoint = controllerDriver->interruptTrb.endpoint; 
	controllerDriver->interruptTrb.length = pktSize;
	controllerDriver->interruptTrb.callback = (DWORD)interruptHandler;
	controllerDriver->interruptTrb.buffer = c.reportData;

	c.controllerDriver = controllerDriver;

	uint8_t  userIndex = -1;
	uint32_t context = 0x0000000010000005 + globalIndex;
	FileLog("InitStage5 calling XamUserBindDeviceCallback (idx=%d)", globalIndex);
	XamUserBindDeviceCallback(0xa7553952 + globalIndex, context, 0, false, &userIndex);
	FileLog("InitStage6 XAM bind returned, slot=%d", (int)userIndex);
	c.userIndex = userIndex;
	c.deviceContext = context;
	connectedControllers[globalIndex] = c;

	SafeDbgPrint("X360Remap: Registered virtual controller inside XAM with index: %d.\n", userIndex);

	if (NeedsDualshock3Handshake(c.vendorId, c.productId)) {
		SafeDbgPrint("X360Remap: Sending dualshock3 handshake!\r\n");
		SendControlRequest(controllerDriver->deviceHandle,
			&controllerDriver->controlTrb,
			0x21,
			0x09, 
			Ds3FeatureStartDevice, 
			0, 
			sizeof(DS3_HANDSHAKE), 
			(void*)DS3_HANDSHAKE, 
			(DWORD)noopCompleteHandler);
	}
	g_deviceInitBusy = false;
	FileLog("InitStage7 init complete, queueing first interrupt transfer");
	return UsbdQueueAsyncTransfer(controllerDriver->deviceHandle, &controllerDriver->interruptTrb);
}

uint8_t NormalizeHat(int32_t v) {
	if (v >= 0 && v <= 7)
		return (uint8_t)v;

	if (v == 0xFF || v > 7)
		return HatSwitch::HAT_NEUTRAL;

	return HatSwitch::HAT_NEUTRAL;
}

HID_ReportItem_t* FindHatItem(HID_ReportInfo_t* info, uint8_t reportId) {
	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;

		if (item->Attributes.Usage.Page != HID_USAGE_PAGE_GENERIC_DESKTOP)
			continue;

		if (item->Attributes.Usage.Usage != HID_USAGE_HAT_SWITCH)
			continue;

		if (info->UsingReportIDs && item->ReportID != reportId)
			continue;

		int32_t min = item->Attributes.Logical.Minimum;
		int32_t max = item->Attributes.Logical.Maximum;

		if (max - min > 16) // hats are never huge ranges
			continue;

		return item;
	}

	return nullptr;
}

void DiscoverAvailableButtons(HID_ReportInfo_t* info, uint8_t reportId,
                              uint8_t* outButtonIndices, uint8_t* outCount) {
	uint8_t count = 0;
	for (HID_ReportItem_t* item = info->FirstReportItem; item && count < 256; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;
		if (item->Attributes.Usage.Page != HID_USAGE_PAGE_BUTTON)
			continue;
		if (info->UsingReportIDs && item->ReportID != reportId)
			continue;

		uint16_t usage = item->Attributes.Usage.Usage;
		if (usage >= 1 && usage <= 256) {
			uint8_t buttonIdx = usage - 1;
			outButtonIndices[count++] = buttonIdx;
		}
	}
	*outCount = count;
}

void DiscoverAvailableAxes(HID_ReportInfo_t* info, uint8_t reportId,
                           uint16_t* outAxisUsages, uint8_t* outCount) {
	uint8_t count = 0;
	for (HID_ReportItem_t* item = info->FirstReportItem; item && count < 6; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;
		if (item->Attributes.Usage.Page != HID_USAGE_PAGE_GENERIC_DESKTOP)
			continue;
		if (info->UsingReportIDs && item->ReportID != reportId)
			continue;

		uint16_t usage = item->Attributes.Usage.Usage;
		if (usage >= HID_USAGE_AXIS_X && usage <= HID_USAGE_AXIS_RZ) {
			outAxisUsages[count++] = usage;
		}
	}
	*outCount = count;
}

void HidFillButtonsReport(
	const uint8_t* payload,
	HID_ReportInfo_t* info,
	ButtonsReport* out,
	uint8_t reportId,
	const HidDeviceMapping* map) {
	// Axes
	const auto* axisMap = map->axisMap;
	uint8_t axisCount = map->axisMapCount;

	for (uint8_t i = 0; i < axisCount; i++) {
		const auto& entry = axisMap[i];

		HID_ReportItem_t* item = FindItemByUsage(
			info,
			HID_USAGE_PAGE_GENERIC_DESKTOP,
			entry.usage,
			reportId
		);

		if (!item || !USB_GetHIDReportItemInfo(reportId, payload, item))
			continue;

		int32_t logMin = (int32_t)item->Attributes.Logical.Minimum;
		int32_t logMax = (int32_t)item->Attributes.Logical.Maximum;
		int32_t raw = (int32_t)item->Value;

		int32_t result = 0;

		if (logMax > logMin) {
			if (raw < logMin) raw = logMin;
			if (raw > logMax) raw = logMax;

			int64_t numerator = (int64_t)(raw - logMin) * 65535;
			int32_t denominator = (logMax - logMin);

			int32_t scaled = (int32_t)((numerator + denominator / 2) / denominator);
			result = scaled - 32768;
		}
		else {
			result = raw;
		}

		// apply inversion
		switch (entry.usage) {
		case HID_USAGE_AXIS_X:
			if (map->invert.invertX) result = -result;
			break;
		case HID_USAGE_AXIS_Y:
			if (map->invert.invertY) result = -result;
			break;
		case HID_USAGE_AXIS_Z:
			if (map->invert.invertZ) result = -result;
			break;
		case HID_USAGE_AXIS_RX:
			if (map->invert.invertRX) result = -result;
			break;
		case HID_USAGE_AXIS_RY:
			if (map->invert.invertRY) result = -result;
			break;
		case HID_USAGE_AXIS_RZ:
			if (map->invert.invertRZ) result = -result;
			break;
		}

		if (result > 32767) result = 32767; if (result < -32768) result = -32768;

		out->*entry.field = (int16_t)result;
	}

	// Hat switch
	HID_ReportItem_t* hatItem = FindHatItem(info, reportId);
	if (hatItem && USB_GetHIDReportItemInfo(reportId, payload, hatItem)) {
		out->has_hat_switch = true;
		out->hatSwitch = NormalizeHat(hatItem->Value);
	} else {
		out->has_hat_switch = false;
	}

	// Buttons - le buttonMap du profil actif (g_lastTitleId) SURCHARGE celui du
	// device entree par entree depuis le 2026-08-03 (avant, il le remplacait
	// integralement et faisait disparaitre tout ce que le profil ne redefinit
	// pas - voir ResolveMergedButtonMap dans mapping.cpp).
	HidButtonMapEntry mergedButtons[MAX_MERGED_BUTTONS];
	uint8_t effButtonCount = ResolveMergedButtonMap(map, g_lastTitleId, mergedButtons, MAX_MERGED_BUTTONS);
	const HidButtonMapEntry* buttonMap = mergedButtons;

	for (uint8_t i = 0; i < effButtonCount; i++) {
		const auto& entry = buttonMap[i];

		HID_ReportItem_t* item = FindButtonItem(info, entry.idx, reportId);
		if (item && USB_GetHIDReportItemInfo(reportId, payload, item)) {
			out->*entry.field = (uint8_t)item->Value;
		}
	}

	// Macros: only which pad buttons are held here; XInputdReadStateHook plays them,
	// it is called at each game read, so the steps follow the game's own timing.
	out->macroHeld = 0;
	for (uint8_t i = 0; i < map->macroCount && i < MAX_MACROS; i++) {
		HID_ReportItem_t* item = FindButtonItem(info, map->macros[i].idx, reportId);
		if (item && USB_GetHIDReportItemInfo(reportId, payload, item) && item->Value) out->macroHeld |= (uint16_t)(1u << i);
	}

	// Held buttons for the Studio's controller page (input_state.json "padBtn").
	// Only while the Studio runs: costs a pass over the report items.
	// shortcut: the last pad to report wins, enough for one USB pad; per pad when the Studio shows two.
	if (g_studioActive) {
		uint32_t held = 0;
		for (HID_ReportItem_t* it = info->FirstReportItem; it; it = it->Next) {
			if (it->ItemType != HID_REPORT_ITEM_In || it->Attributes.Usage.Page != HID_USAGE_PAGE_BUTTON) continue;
			if (info->UsingReportIDs && it->ReportID != reportId) continue;
			const uint16_t usage = it->Attributes.Usage.Usage;
			if (usage >= 1 && usage <= 32 && USB_GetHIDReportItemInfo(reportId, payload, it) && it->Value)
				held |= 1u << (usage - 1);
		}
		g_padButtonsHeld = held;
	}
}

// ---------------------------------------------------------------------------
// Mouse support (relative-motion HID devices)
//
// A USB mouse's X/Y report items live on the same usage page/usage as a
// joystick's (Generic Desktop, 0x30/0x31) - the only structural difference is
// that mouse axes are marked HID_IOF_RELATIVE instead of HID_IOF_ABSOLUTE.
// We use that flag as the classification signal rather than the top level
// collection usage, since it also catches trackballs and similar devices.
// ---------------------------------------------------------------------------

// Report item values are stored as raw, left-shifted bitfields (see
// HID_ALIGN_DATA in hid_parser.h). For relative axes the value is signed, so
// it needs to be sign extended based on the item's actual bit size before use.
int32_t GetSignedAxisDelta(HID_ReportItem_t* item) {
	int32_t aligned = HID_ALIGN_DATA(item, int32_t);
	return aligned >> (32 - item->Attributes.BitSize);
}

bool DetectIsMouse(HID_ReportInfo_t* info, uint8_t reportId) {
	HID_ReportItem_t* xItem = FindItemByUsage(info, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_AXIS_X, reportId);
	if (!xItem)
		return false;

	return (xItem->ItemFlags & HID_IOF_RELATIVE) != 0;
}

// Detection-only for now (see Controller::isKeyboard comment). A standard
// USB keyboard's report descriptor has items on the Keyboard/Keypad usage
// page (0x07) - modifier bits (left/right ctrl/shift/alt/gui) plus either an
// N-key array or a bitmap of individual key usages, depending on the
// device. We don't parse any of that yet, just detect its presence so the
// device can be classified and kept out of the gamepad mapping assistant.
// An unknown controller (2026-10-14, HB's generic USB pad was "not detected"):
// at least 4 buttons on the Button page, or 2 absolute axes on the Generic
// Desktop page. Such a device is kept so the controller assistant can map it.
bool DetectIsGamepad(HID_ReportInfo_t* info) {
	if (!info) return false;
	int buttons = 0, axes = 0;
	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In) continue;
		if (item->Attributes.Usage.Page == HID_USAGE_PAGE_BUTTON) buttons++;
		if (item->Attributes.Usage.Page == HID_USAGE_PAGE_GENERIC_DESKTOP &&
			!(item->ItemFlags & HID_IOF_RELATIVE) &&
			item->Attributes.Usage.Usage >= HID_USAGE_AXIS_X && item->Attributes.Usage.Usage <= HID_USAGE_AXIS_RZ)
			axes++;
	}
	return buttons >= 4 || axes >= 2;
}

bool DetectIsKeyboard(HID_ReportInfo_t* info) {
	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In)
			continue;
		if (item->Attributes.Usage.Page == HID_USAGE_PAGE_KEYBOARD_KEYPAD)
			return true;
	}
	return false;
}

// Clavier generique (chemin descripteur, PREMIER JET NON TESTE SUR MATERIEL,
// 2026-09-10) - comble le trou documente le 2026-08-04 ("CLAVIERS EXCLUS, et
// c'est structurel... il devient totalement muet") : jusqu'ici, un clavier
// classe isKeyboard=true par ce chemin n'avait litteralement aucun code pour
// lire ses touches (seul HidFillBootKeyboardState, reserve au chemin boot,
// le faisait). Meme repli "premiere touche non nulle du tableau, sinon
// premier bit de modificateur actif" que HidFillBootKeyboardState, mais lu
// dynamiquement depuis le VRAI descripteur du peripherique (FindKeySlotItem/
// FindModifierBitItem ci-dessus) au lieu de supposer un format fixe -
// justement pour s'adapter a un clavier qui n'a jamais promis suivre le
// format boot standard. Jusqu'a 8 emplacements verifies (couvre le tableau a
// 6 touches classique et quelques variantes a 8) ; s'arrete au premier non
// vide, meme convention que le chemin boot.
//
// Ne touche NI ctrl->map, NI aucun mapping de boutons manette - lastKeyCode
// est tout ce que lit l'assistant de configuration (application.xex, via
// input_state.json). Le mappage WASD/etc par defaut du clavier reste celui
// deja utilise par le chemin boot (meme fichier X360Remap.json, meme
// convention "keys").
void HidFillDescriptorKeyboardState(const uint8_t* payload, Controller* ctrl) {
	HID_ReportInfo_t* info = ctrl->reportInfo;
	uint8_t reportId = ctrl->reportId;
	if (!info) return;   // defensif - ne devrait jamais arriver ici (voir isBootProtocol dans le dispatcher)

	// Two layouts exist on the keyboard page (2026-10-11):
	//  - ARRAY (boot style, 6KRO): each item is a slot whose VALUE is a key code.
	//  - BITMAP (N-key rollover, gaming keyboards): one 1-bit VARIABLE item per
	//    key, whose USAGE is the key code and value 1 = held.
	// The old code read only array slots 0-7 by usage number, so on a bitmap
	// keyboard it saw just the bits of codes 0-7 and reported "1" as the key.
	uint8_t pressedCode = 0xFF;
	uint8_t mods = 0;
	for (HID_ReportItem_t* item = info->FirstReportItem; item; item = item->Next) {
		if (item->ItemType != HID_REPORT_ITEM_In) continue;
		if (item->Attributes.Usage.Page != HID_USAGE_PAGE_KEYBOARD_KEYPAD) continue;
		if (info->UsingReportIDs && item->ReportID != reportId) continue;
		if (!USB_GetHIDReportItemInfo(reportId, payload, item)) continue;
		const bool bitmap = (item->ItemFlags & HID_IOF_VARIABLE) && item->Attributes.BitSize == 1;
		const uint16_t code = bitmap ? item->Attributes.Usage.Usage : (uint16_t)item->Value;
		if (bitmap && !item->Value) continue;
		if (code >= 0xE0 && code <= 0xE7) { mods |= (uint8_t)(1 << (code - 0xE0)); continue; }
		if (code > 3 && code < 0xE0 && pressedCode == 0xFF) pressedCode = (uint8_t)code;   // 1-3 = rollover/error codes
	}
	if (pressedCode == 0xFF) {
		for (uint8_t bit = 0; bit < 8; bit++)
			if ((mods >> bit) & 1) { pressedCode = 0xE0 + bit; break; }
	}
	ctrl->lastModifiers = mods;
	ctrl->lastKeyCode = pressedCode;
	g_overlayMods = mods;
	g_overlayKeys[0] = pressedCode;   // shortcut: one key at a time on this path, enough for keyboards seen so far
}

// Accumulates relative X/Y motion into the owning Controller (drained in
// XInputdReadStateHook), and fills in mouse button state using the existing
// ButtonsReport/trigger pipeline so no changes are needed downstream.
//
// Button mapping: if the device has a configured mapping (static or loaded
// from X360Remap.json, keyed by VID/PID like any other device - see
// mapping.h/HidDeviceMapping) with a non-empty buttonMap, that mapping is
// used, same as for gamepads. Otherwise falls back to a sensible default:
//   left click  -> r2 (drives bRightTrigger, e.g. "fire")
//   right click -> l2 (drives bLeftTrigger,  e.g. "aim")
//   middle click -> r3 (right stick click)

// Declarees plus bas (avec HidFillBootMouseState, leur autre point
// d'ecriture) mais utilisees ici aussi depuis le 2026-08-08 - voir le
// commentaire dans HidFillMouseState pour le pourquoi. Definitions
// canoniques : chercher "uint8_t  g_rawMouseLatest[8]" plus bas dans ce
// fichier.
extern uint8_t  g_rawMouseLatest[8];
extern uint8_t  g_rawMouseLen;
extern volatile uint32_t g_rawMouseSeq;

void HidFillMouseState(
	const uint8_t* payload,
	HID_ReportInfo_t* info,
	ButtonsReport* out,
	uint8_t reportId,
	Controller* ctrl) {

	HID_ReportItem_t* xItem = FindItemByUsage(info, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_AXIS_X, reportId);
	HID_ReportItem_t* yItem = FindItemByUsage(info, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_AXIS_Y, reportId);

	if (xItem && USB_GetHIDReportItemInfo(reportId, payload, xItem)) {
		int32_t dx = GetSignedAxisDelta(xItem);
		ctrl->mouseAccumX += dx;
		ctrl->cursorAccumX += dx;   // separate accumulator, see Controller::cursorAccumX
	}
	if (yItem && USB_GetHIDReportItemInfo(reportId, payload, yItem)) {
		int32_t dy = GetSignedAxisDelta(yItem);
		ctrl->mouseAccumY += dy;
		ctrl->cursorAccumY += dy;   // separate accumulator, see Controller::cursorAccumX
	}

	HID_ReportItem_t* wheelItem = FindItemByUsage(info, HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_WHEEL, reportId);
	if (wheelItem && USB_GetHIDReportItemInfo(reportId, payload, wheelItem)) {
		int32_t wd = GetSignedAxisDelta(wheelItem);
		ctrl->mouseWheelAccum += wd;
		if (wd != 0) ctrl->bindWheelDir = (wd > 0) ? 1 : -1; // voir Controller::bindWheelDir
		ctrl->uiWheelAccum += wd; // voir Controller::uiWheelAccum
	}

	// Cliche des boutons pour application.xex et la liaison rapide (2026-08-04).
	// Ces deux champs n'etaient renseignes QUE par HidFillBootMouseState : une
	// souris basculee sur le chemin descripteur devenait donc invisible pour le
	// curseur de l'application (qui a besoin de "clic gauche maintenu") et pour
	// la capture de source de la liaison rapide. Constate des le premier
	// basculement reussi - la molette marchait, le curseur non.
	// Les indices suivent la convention HID de la page Boutons (0=gauche,
	// 1=droit, 2=milieu...), la meme que le chemin boot, pour que tout ce qui
	// consomme ces champs reste identique.
	{
		uint8_t mask = 0;
		for (uint8_t b = 0; b < 8; b++) {
			HID_ReportItem_t* item = FindButtonItem(info, b, reportId);
			if (item && USB_GetHIDReportItemInfo(reportId, payload, item) && item->Value)
				mask |= (uint8_t)(1 << b);
		}
		uint8_t pressedIdx = 0xFF;
		for (uint8_t b = 0; b < 8; b++) {
			if ((mask >> b) & 1) { pressedIdx = b; break; }
		}
		ctrl->lastMouseButtonIdx = pressedIdx;
		ctrl->lastMouseButtonsMask = mask;
		g_overlayMouse = mask;
	}

	// Cliche brut du dernier rapport pour l'assistant de calibration
	// (2026-08-08, signale par l'utilisateur : "calibration souris ne marche
	// pas", ecran bloque sur "No mouse data received"). Cause reelle :
	// g_rawMouseLatest/g_rawMouseLen (voir leur declaration, et le meme bloc
	// dans HidFillBootMouseState un peu plus bas) n'etaient jamais renseignes
	// ICI - uniquement dans HidFillBootMouseState, le chemin boot legacy.
	// Depuis le correctif du 2026-08-03 (descripteur HID par peripherique),
	// toute souris dont le descripteur se lit correctement passe par CETTE
	// fonction, pas par le chemin boot - exactement le meme oubli que celui
	// deja corrige plus haut pour lastMouseButtonIdx/lastMouseButtonsMask
	// (voir le commentaire juste au-dessus), pour la meme raison : un champ
	// que seul le chemin boot renseignait, et dont un consommateur (ici
	// l'assistant de calibration, via input_state.json) a silencieusement
	// cesse de recevoir des donnees des qu'une souris a bascule sur le
	// chemin descripteur. Meme regle de callback USB que partout ailleurs
	// dans ce fichier : simple copie d'octets, aucun formatage, aucune E/S -
	// le thread de polling s'occupe du reste (voir plus bas).
	{
		uint16_t copyLen = ctrl->reportSize;
		if (copyLen > 8) copyLen = 8;

		g_rawMouseLen = (uint8_t)copyLen;
		for (uint16_t i = 0; i < copyLen; i++)
			g_rawMouseLatest[i] = payload[i];
		g_rawMouseSeq++;
	}

	// Studio open: the click belongs to the cursor. As a pad button it reaches
	// ImGui first (input_state.json comes later), presses the selected item and
	// the button under the cursor never gets the click (HB, 2026-10-30).
	if (g_studioActive) return;

	// buttonMap du profil actif fusionne par-dessus celui du device (2026-08-03,
	// voir ResolveMergedButtonMap) - une entree de profil surcharge le meme idx,
	// les autres commandes du device restent actives.
	HidButtonMapEntry effButtonMap[MAX_MERGED_BUTTONS];
	const HidDeviceMapping* map = ctrl->map;   // lu une seule fois (AUDIT C4)
	uint8_t effButtonCount = ResolveMergedButtonMap(map, g_lastTitleId, effButtonMap, MAX_MERGED_BUTTONS);
	if (effButtonCount > 0) {
		for (uint8_t i = 0; i < effButtonCount; i++) {
			const auto& entry = effButtonMap[i];
			HID_ReportItem_t* item = FindButtonItem(info, entry.idx, reportId);
			if (item && USB_GetHIDReportItemInfo(reportId, payload, item)) {
				out->*entry.field = (uint8_t)item->Value;
			}
		}
		return;
	}

	HID_ReportItem_t* leftBtn = FindButtonItem(info, 0, reportId);
	HID_ReportItem_t* rightBtn = FindButtonItem(info, 1, reportId);
	HID_ReportItem_t* middleBtn = FindButtonItem(info, 2, reportId);

	if (leftBtn && USB_GetHIDReportItemInfo(reportId, payload, leftBtn))
		out->r2 = (uint8_t)leftBtn->Value;
	if (rightBtn && USB_GetHIDReportItemInfo(reportId, payload, rightBtn))
		out->l2 = (uint8_t)rightBtn->Value;
	if (middleBtn && USB_GetHIDReportItemInfo(reportId, payload, middleBtn))
		out->r3 = (uint8_t)middleBtn->Value;
}

// --- Boot-protocol report handling (fixed layouts, no descriptor needed) ---

// Index of a boot-protocol device already registered with XAM, or -1.
// Used so the keyboard and the mouse end up on the SAME player slot instead of
// claiming one each (which is what made a game see two players and let one
// device override the other).
int FindBootComboMember() {
	for (int i = 0; i < 4; i++) {
		// Critere "clavier ou souris a nous", et non "device boot"
		// (2026-08-04) : une souris deja basculee sur le chemin descripteur a
		// isBootProtocol = false. Sans ce changement, un clavier branche
		// APRES elle ne la reconnaissait plus comme partenaire, prenait son
		// propre emplacement joueur, et on retombait sur le probleme
		// historique "clavier = joueur 1, souris = joueur 2" que la fusion
		// existe justement pour eviter.
		if (connectedControllers[i].controllerDriver &&
			(connectedControllers[i].isMouse || connectedControllers[i].isKeyboard))
			return i;
	}
	return -1;
}

// Merges one device's contribution into the combined report handed to XInput.
// Buttons are OR-ed (either device may press them); an axis is taken from
// whichever device is actually deflecting it, so the keyboard's WASD/IJKL and
// the mouse's motion coexist without one zeroing out the other.
void MergeButtonsReport(ButtonsReport* dst, const ButtonsReport* src) {
	dst->a_button |= src->a_button;
	dst->b_button |= src->b_button;
	dst->x_button |= src->x_button;
	dst->y_button |= src->y_button;
	dst->l1 |= src->l1;
	dst->r1 |= src->r1;
	dst->l2 |= src->l2;
	dst->r2 |= src->r2;
	dst->l3 |= src->l3;
	dst->r3 |= src->r3;
	dst->start |= src->start;
	dst->back |= src->back;
	dst->xbox |= src->xbox;

	dst->dpad_up |= src->dpad_up;
	dst->dpad_down |= src->dpad_down;
	dst->dpad_left |= src->dpad_left;
	dst->dpad_right |= src->dpad_right;

	if (src->x) dst->x = src->x;
	if (src->y) dst->y = src->y;
	if (src->z) dst->z = src->z;
	if (src->rz) dst->rz = src->rz;
	if (src->rx) dst->rx = src->rx;
	if (src->ry) dst->ry = src->ry;

	// A neutral hat must not overwrite a real direction coming from the other device.
	if (src->has_hat_switch && src->hatSwitch != HatSwitch::HAT_NEUTRAL) {
		dst->has_hat_switch = true;
		dst->hatSwitch = src->hatSwitch;
	}
}

// Accepts both regular keys (looked up in the keycode array) and modifier keys,
// which the boot report carries as bits but which have usage codes 0xE0..0xE7.
// This lets a config file refer to any key by a single "code" number.
bool IsKeyPressed(const BootKeyboardReport* r, uint8_t keycode) {
	if (keycode >= 0xE0 && keycode <= 0xE7)
		return (r->modifiers & (1 << (keycode - 0xE0))) != 0;

	for (int i = 0; i < 6; i++) {
		if (r->keycodes[i] == keycode)
			return true;
	}
	return false;
}

// Applies one configured key action to the report being built.
void ApplyKeyAction(ButtonsReport* out, uint8_t action) {
	switch (action) {
	case KEYACT_A:      out->a_button = 1; break;
	case KEYACT_B:      out->b_button = 1; break;
	case KEYACT_X:      out->x_button = 1; break;
	case KEYACT_Y:      out->y_button = 1; break;
	case KEYACT_LB:     out->l1 = 1; break;
	case KEYACT_RB:     out->r1 = 1; break;
	case KEYACT_LT:     out->l2 = 1; break;
	case KEYACT_RT:     out->r2 = 1; break;
	case KEYACT_L3:     out->l3 = 1; break;
	case KEYACT_R3:     out->r3 = 1; break;
	case KEYACT_START:  out->start = 1; break;
	case KEYACT_BACK:   out->back = 1; break;
	case KEYACT_GUIDE:  out->xbox = 1; break;

	case KEYACT_DPAD_UP:    out->has_hat_switch = true; out->hatSwitch = HatSwitch::HAT_UP; break;
	case KEYACT_DPAD_DOWN:  out->has_hat_switch = true; out->hatSwitch = HatSwitch::HAT_DOWN; break;
	case KEYACT_DPAD_LEFT:  out->has_hat_switch = true; out->hatSwitch = HatSwitch::HAT_LEFT; break;
	case KEYACT_DPAD_RIGHT: out->has_hat_switch = true; out->hatSwitch = HatSwitch::HAT_RIGHT; break;

	case KEYACT_LSTICK_UP:    out->y = 32767; break;
	case KEYACT_LSTICK_DOWN:  out->y = -32768; break;
	case KEYACT_LSTICK_LEFT:  out->x = -32768; break;
	case KEYACT_LSTICK_RIGHT: out->x = 32767; break;
	case KEYACT_RSTICK_UP:    out->rz = 32767; break;
	case KEYACT_RSTICK_DOWN:  out->rz = -32768; break;
	case KEYACT_RSTICK_LEFT:  out->z = -32768; break;
	case KEYACT_RSTICK_RIGHT: out->z = 32767; break;
	default: break;
	}
}

// Same behaviour as HidFillMouseState, but reading the fixed boot layout
// instead of a parsed report descriptor. Button indices keep the usual
// meaning (0 = left, 1 = right, 2 = middle) so X360Remap.json entries written
// for the descriptor path keep working unchanged.
// Cliche brut du dernier rapport souris, rempli depuis le callback USB par de
// simples copies d'octets (aucun formatage, aucun log - voir le commentaire
// dans HidFillBootMouseState) et draine par le thread de polling, seul
// endroit autorise a ecrire sur disque. Champs simples, meme regle de course
// acceptee que Controller::lastMouseButtonsMask.
uint8_t  g_rawMouseLatest[8] = {0};
uint8_t  g_rawMouseLen = 0;
// Numero du rapport souris (2026-09-25) : +1 a chaque rapport recu. Permet a
// l'assistant de calibration de distinguer un NOUVEAU rapport d'une simple
// reecriture du meme (input_state.json est reecrit toutes les ~100 ms).
// Simple increment dans le callback, aucune E/S.
volatile uint32_t g_rawMouseSeq = 0;
uint8_t  g_rawMouseBeyond[8] = {0};   // dernier rapport avec un octet non nul au-dela de X/Y
uint8_t  g_rawMouseBeyondLen = 0;
uint32_t g_rawMouseBeyondCount = 0;   // combien de fois c'est arrive (0 = la molette n'emet jamais rien)

void HidFillBootMouseState(const uint8_t* payload, ButtonsReport* out, Controller* ctrl) {
	const BootMouseReport* r = (const BootMouseReport*)payload;

	// --- Disposition apprise par calibration (2026-08-03) -----------------
	// Si l'utilisateur a calibre cette souris via l'assistant, on lit X, Y,
	// la molette et les boutons AUX OFFSETS APPRIS plutot qu'a travers la
	// structure figee BootMouseReport. C'est ce qui permet de gerer une souris
	// dont le paquet ne suit pas la disposition legacy (axes 16 bits, octet
	// d'identifiant en tete, molette ailleurs que sur l'octet 3) SANS forcer
	// le boot protocol - donc sans lui coûter sa molette.
	//
	// Aucun calcul lourd ici : quelques lectures d'octets, comme le reste de
	// cette fonction. On est dans le contexte de callback USB, la regle reste
	// la meme (voir le commentaire sur le gel plus bas).
	// ctrl->map lu UNE SEULE FOIS (AUDIT_2026-09.md, C4) : le thread de
	// polling peut le remettre a nul a tout instant (rechargement de la
	// config). Deux lectures successives pouvaient donner "non nul" puis "nul".
	const HidDeviceMapping* map = ctrl->map;
	const HidReportLayout* layout = (map && map->reportLayout.valid)
		? &map->reportLayout : nullptr;

	if (layout) {
		uint8_t len = (uint8_t)(ctrl->reportSize > 16 ? 16 : ctrl->reportSize);
		int32_t lx = ReadSignedField(payload, len, layout->xOffset, layout->xSize);
		int32_t ly = ReadSignedField(payload, len, layout->yOffset, layout->ySize);

		ctrl->mouseAccumX += lx;
		ctrl->mouseAccumY += ly;
		ctrl->cursorAccumX += lx;
		ctrl->cursorAccumY += ly;

		if (layout->wheelOffset != REPORT_FIELD_ABSENT)
		{
			int32_t wd = ReadSignedField(payload, len, layout->wheelOffset, layout->wheelSize);
			ctrl->mouseWheelAccum += wd;
			if (wd != 0) ctrl->bindWheelDir = (wd > 0) ? 1 : -1; // voir Controller::bindWheelDir
			ctrl->uiWheelAccum += wd; // voir Controller::uiWheelAccum
		}
	} else {
		ctrl->mouseAccumX += r->x;
		ctrl->mouseAccumY += r->y;
		ctrl->cursorAccumX += r->x;   // separate accumulator, see Controller::cursorAccumX
		ctrl->cursorAccumY += r->y;
		// Only trust the wheel byte if the endpoint actually sends one.
		if (ctrl->reportSize >= 4) {
			ctrl->mouseWheelAccum += r->wheel;
			if (r->wheel != 0) ctrl->bindWheelDir = (r->wheel > 0) ? 1 : -1; // voir Controller::bindWheelDir
			ctrl->uiWheelAccum += r->wheel; // voir Controller::uiWheelAccum
		}
	}

	// DIAG molette brute (2026-08-03) - "je scrolle mais ca ne donne rien",
	// et AUCUNE ligne DIAG molette n'apparait dans le log, donc l'accumulateur
	// reste a zero : l'octet payload[3] ne bouge jamais. Hypothese a verifier :
	// le protocole BOOT du HID ne definit officiellement que 3 octets
	// (boutons, X, Y) - la molette est une extension de fait, non garantie. Le
	// SET_PROTOCOL(boot) ajoute le meme jour pour reparer le curseur de cette
	// souris pourrait donc lui avoir fait perdre sa molette.
	//
	// Deux declencheurs pour distinguer les cas :
	//   A. un octet AU-DELA de X/Y est non nul -> des donnees molette arrivent
	//      bien, mais peut-etre pas la ou on les lit (offset different).
	//   B. un cliche periodique quel que soit le contenu -> donne la forme
	//      typique du paquet meme si la molette ne produit jamais rien, ce qui
	//      est justement l'information decisive ici.
	// Limite en frequence : une souris emet a plusieurs centaines de Hz, un log
	// inconditionnel noierait le fichier. A retirer une fois tranche.
	// GEL REEL PROVOQUE ICI (2026-08-03) : la premiere version de ce
	// diagnostic appelait FileLog directement dans cette fonction. Or on est
	// dans le contexte de callback de completion USB, appele a plusieurs
	// centaines de Hz, et FileLog duplique chaque ligne vers DbgPrint - c'est
	// exactement ce que tout ce fichier interdit depuis les gels documentes
	// dans PROJECT_NOTES.md (voir Controller::mouseNotified). Resultat : la
	// console gelait a chaque demarrage.
	//
	// Corrige selon la convention deja etablie partout ailleurs ici : le
	// callback ne fait que des copies memoire bon marche, et c'est le thread
	// de polling (contexte sur) qui formate et ecrit. Meme compromis de
	// course accepte que mouseAccumX/Y : un rapport peut etre manque entre
	// deux relevés, sans importance pour un diagnostic.
	{
		uint16_t copyLen = ctrl->reportSize;
		if (copyLen > 8) copyLen = 8;

		g_rawMouseLen = (uint8_t)copyLen;
		for (uint16_t i = 0; i < copyLen; i++)
			g_rawMouseLatest[i] = payload[i];
		g_rawMouseSeq++;

		// Latch separe : retient le dernier rapport ou un octet AU-DELA de
		// X/Y etait non nul. Sans ca, un cran de molette (quelques rapports
		// sur des centaines) serait presque toujours manque par un simple
		// releve periodique.
		for (uint16_t i = 3; i < copyLen; i++) {
			if (payload[i] != 0) {
				for (uint16_t j = 0; j < copyLen; j++)
					g_rawMouseBeyond[j] = payload[j];
				g_rawMouseBeyondLen = (uint8_t)copyLen;
				g_rawMouseBeyondCount++;
				break;
			}
		}
	}

	// Octet des boutons : celui appris par calibration si disponible, sinon
	// l'octet 0 suppose par la disposition legacy. Sur une souris a paquet
	// long avec identifiant de rapport en tete, l'octet 0 est justement cet
	// identifiant et pas les boutons - d'ou des clics fantomes ou muets tant
	// qu'aucune calibration n'a eu lieu.
	uint8_t buttonsByte = r->buttons;
	if (layout && layout->buttonsOffset != REPORT_FIELD_ABSENT &&
		layout->buttonsOffset < ctrl->reportSize) {
		buttonsByte = payload[layout->buttonsOffset];
	}

	// Live snapshot for the config assistant - first held button bit, HID
	// button-page convention (0=left, 1=right, 2=middle...), 0xFF if none.
	// Plain field write, same safety rule as the accumulators above.
	uint8_t pressedIdx = 0xFF;
	for (uint8_t i = 0; i < 8; i++) {
		if ((buttonsByte >> i) & 1) { pressedIdx = i; break; }
	}
	ctrl->lastMouseButtonIdx = pressedIdx;
	// Full raw mask too (see Controller::lastMouseButtonsMask) - the config
	// app's cursor needs "is left held" as a continuous boolean, which the
	// single-index field above can't give once more than one button is down.
	ctrl->lastMouseButtonsMask = buttonsByte;
	g_overlayMouse = buttonsByte;

	// Studio open: the click belongs to the cursor. As a pad button it reaches
	// ImGui first (input_state.json comes later), presses the selected item and
	// the button under the cursor never gets the click (HB, 2026-10-30).
	if (g_studioActive) return;

	// buttonMap du profil actif fusionne par-dessus celui du device (2026-08-03,
	// voir ResolveMergedButtonMap) - lier un seul bouton via la liaison rapide
	// ne doit plus faire disparaitre tous les autres clics pour ce jeu.
	HidButtonMapEntry effButtonMap[MAX_MERGED_BUTTONS];
	uint8_t effButtonCount = ResolveMergedButtonMap(map, g_lastTitleId, effButtonMap, MAX_MERGED_BUTTONS);
	if (effButtonCount > 0) {
		for (uint8_t i = 0; i < effButtonCount; i++) {
			const auto& entry = effButtonMap[i];
			if (entry.idx < 8)
				out->*entry.field = (buttonsByte >> entry.idx) & 1;
		}
		return;
	}

	// Same defaults as the descriptor path: left -> RT, right -> LT, middle -> R3.
	out->r2 = (buttonsByte >> 0) & 1;
	out->l2 = (buttonsByte >> 1) & 1;
	out->r3 = (buttonsByte >> 2) & 1;
}

// Disposition clavier par defaut.
//
// REVUE LE 2026-09-13 - orientee jeu au lieu d'alphabetique.
//
// L'ancienne disposition venait du fork clavier de reference
// (UncreativeXenon/hiddriver360) : Z/X/C/V = A/B/X/Y, choisis parce qu'ils
// sont cote a cote sur le clavier, pas parce qu'ils correspondent a quoi que
// ce soit dans un jeu. Retour d'un testeur sur Red Dead Redemption : "the
// keyboard worked with the default keybinds (A=z | B=x | X=c | Y=v) which is
// unplayable". C'est exact : aucun joueur PC n'a ces reflexes.
//
// Nouvelle disposition, calquee sur ce que font la quasi-totalite des jeux PC,
// pour que le premier essai soit jouable sans configuration :
//   WASD        = stick gauche        (universel)
//   IJKL        = stick droit         (inchange - la souris le pilote aussi)
//   Fleches     = croix directionnelle (inchange)
//   Espace      = A   (sauter)
//   C           = B   (s'accroupir)
//   R           = X   (recharger)
//   E           = Y   (interagir / utiliser)
//   Q           = LB  (grenade / competence)
//   F           = RB  (corps a corps)
//   Maj gauche  = LT  (INCHANGE)
//   Ctrl gauche = RT  (INCHANGE)
//   Alt gauche  = L3  (INCHANGE - sprint dans beaucoup de jeux)
//   V           = R3  (corps a corps / clic stick droit)
//   Echap       = Start (menu)
//   Tab         = Back  (carte / scores)
//   Retour arr. = Guide (bouton Xbox)
//
// POURQUOI LES GACHETTES NE BOUGENT PAS : il aurait ete tentant de liberer
// Maj/Ctrl (sprint/accroupi sur PC) puisque la souris fournit deja LT/RT. Mais
// un jeu de course se joue au clavier seul, LT/RT = frein/accelerateur : les
// deplacer aurait rendu ces jeux injouables sans souris. Le cout d'un mauvais
// choix n'est pas symetrique, donc on ne touche pas a ce qui marche.
//
// Ceci reste un DEFAUT : toute disposition "keys" dans X360Remap.json le
// remplace integralement, et l'assistant de l'application permet de tout
// reassigner en 21 etapes (ou une seule touche a la fois en mode manuel).
void HidFillBootKeyboardState(const uint8_t* payload, ButtonsReport* out, Controller* ctrl) {
	const BootKeyboardReport* r = (const BootKeyboardReport*)payload;
	const HidDeviceMapping* map = ctrl->map;

	// Live snapshot for the config assistant - first held key, regular keys
	// checked before modifiers only because that matches how a person reads
	// "what am I pressing" (a plain key, not just Shift on its own). 0xFF if
	// nothing is held. Plain field write, same safety rule as elsewhere.
	uint8_t pressedCode = 0xFF;
	for (int i = 0; i < 6 && pressedCode == 0xFF; i++) {
		if (r->keycodes[i] != 0) pressedCode = r->keycodes[i];
	}
	if (pressedCode == 0xFF && r->modifiers != 0) {
		for (uint8_t bit = 0; bit < 8; bit++) {
			if ((r->modifiers >> bit) & 1) { pressedCode = 0xE0 + bit; break; }
		}
	}
	ctrl->lastModifiers = r->modifiers;
	ctrl->lastKeyCode = pressedCode;
	g_overlayMods = r->modifiers;
	for (int i = 0; i < 6; i++) g_overlayKeys[i] = r->keycodes[i];

	// A "keys" layout in X360Remap.json replaces the default entirely. Le keyMap
	// du profil actif, lui, est FUSIONNE par-dessus celui du device depuis le
	// 2026-08-03 (voir ResolveMergedKeyMap) : il ne surcharge que les touches
	// qu'il redefinit. Avant, il remplacait tout - lier une seule touche via la
	// liaison rapide rendait donc TOUT le reste du clavier muet dans ce jeu,
	// ce qui a fait croire a une perte de configuration (elle etait toujours
	// dans le fichier, simplement masquee en jeu).
	HidKeyMapEntry effKeyMap[MAX_MERGED_KEYS];
	uint8_t effKeyCount = ResolveMergedKeyMap(map, g_lastTitleId, effKeyMap, MAX_MERGED_KEYS);
	const bool keypad = IsKnownKeypad(ctrl->vendorId, ctrl->productId);
	if (effKeyCount > 0 || keypad) {
		// Custom keys exist: they go ON TOP of the default layout instead of
		// replacing it (2026-10-11, see ResolveEffectiveKeyMap) - one key
		// assigned in the Studio no longer makes every other key dead.
		effKeyCount = ResolveEffectiveKeyMap(map, g_lastTitleId, effKeyMap, MAX_MERGED_KEYS);
		// Gaming keypad: START / Guide / R3 / RT on keys it really has.
		effKeyCount = AddKeypadPreset(ctrl->vendorId, ctrl->productId, effKeyMap, effKeyCount, MAX_MERGED_KEYS);
		out->has_hat_switch = true;
		out->hatSwitch = HatSwitch::HAT_NEUTRAL;
		bool enterAssigned = false;
		for (uint8_t i = 0; i < effKeyCount; i++) {
			if (effKeyMap[i].code == HID_KEY_ENTER) enterAssigned = true;
			if (IsKeyPressed(r, effKeyMap[i].code))
				ApplyKeyAction(out, effKeyMap[i].action);
		}
		// Enter = A when the player did not use Enter (see the default branch).
		if (!enterAssigned && IsKeyPressed(r, HID_KEY_ENTER)) out->a_button = 1;
		return;
	}

	// Sticks: full deflection, since a key is either down or up.
	if (IsKeyPressed(r, HID_KEY_A))      out->x = -32768;
	else if (IsKeyPressed(r, HID_KEY_D)) out->x = 32767;
	if (IsKeyPressed(r, HID_KEY_W))      out->y = 32767;
	else if (IsKeyPressed(r, HID_KEY_S)) out->y = -32768;

	if (IsKeyPressed(r, HID_KEY_J))      out->z = -32768;
	else if (IsKeyPressed(r, HID_KEY_L)) out->z = 32767;
	if (IsKeyPressed(r, HID_KEY_I))      out->rz = 32767;
	else if (IsKeyPressed(r, HID_KEY_K)) out->rz = -32768;

	// Enter = A as well (2026-10-11): in the Studio and in every menu, a new
	// player presses Enter to confirm. With Space only, the tester's keyboard
	// "did nothing" in the Studio and he thought it had frozen.
	out->a_button = IsKeyPressed(r, HID_KEY_SPACE) || IsKeyPressed(r, HID_KEY_ENTER);   // saut / valider
	out->b_button = IsKeyPressed(r, HID_KEY_C);       // accroupi
	out->x_button = IsKeyPressed(r, HID_KEY_R);       // recharger
	out->y_button = IsKeyPressed(r, HID_KEY_E);       // interagir / utiliser
	out->l1       = IsKeyPressed(r, HID_KEY_Q);       // grenade / competence
	out->r1       = IsKeyPressed(r, HID_KEY_F);       // corps a corps
	// Echap = Start : convention PC universelle pour ouvrir le menu.
	//
	// INTERACTION CONNUE, A SURVEILLER AU TEST : l'assistant de
	// l'application affiche "ESC pour annuler" (STR_WIZARD_HOLD_TO_SKIP et
	// STR_MANUAL_CAPTURE_HINT, quatre langues). Quand l'utilisateur annule,
	// l'application recoit bien la touche (elle la lit dans input_state.json,
	// independamment de l'emulation manette) MAIS le systeme recoit aussi un
	// appui Start. Juge acceptable : Start n'est pas intercepte par le
	// tableau de bord, contrairement a Guide - c'est exactement pour ca que
	// Guide a ete deplace sur Alt droit juste en dessous. A confirmer sur
	// materiel ; si Start s'avere genant dans l'application, le repli est de
	// remettre Start sur Entree.
	out->start    = IsKeyPressed(r, HID_KEY_ESCAPE);
	out->back     = IsKeyPressed(r, HID_KEY_TAB);     // carte / scores

	out->l2 = (r->modifiers & HID_MOD_LSHIFT) ? 1 : 0;   // inchange (LT)
	out->r2 = (r->modifiers & HID_MOD_LCTRL) ? 1 : 0;    // inchange (RT)
	out->l3 = (r->modifiers & HID_MOD_LALT) ? 1 : 0;     // inchange (L3)
	out->r3 = IsKeyPressed(r, HID_KEY_V);                // corps a corps / clic stick droit

	// Guide sur ALT DROIT, et surtout PAS sur Retour arriere (2026-09-13).
	// L'application affiche "Retour arriere pour effacer" dans le champ Title
	// ID (STR_PROFILES_HINT_TITLEID_CHARS, les quatre langues) : mettre Guide
	// sur cette touche ouvrait le menu Xbox par-dessus l'application des que
	// l'utilisateur corrigeait une faute de frappe. Alt droit est libre depuis
	// que R3 est passe sur V, et ne se presse pas par accident.
	out->xbox = (r->modifiers & HID_MOD_RALT) ? 1 : 0;

	out->has_hat_switch = true;
	if (IsKeyPressed(r, HID_KEY_UP))          out->hatSwitch = HatSwitch::HAT_UP;
	else if (IsKeyPressed(r, HID_KEY_RIGHT))  out->hatSwitch = HatSwitch::HAT_RIGHT;
	else if (IsKeyPressed(r, HID_KEY_DOWN))   out->hatSwitch = HatSwitch::HAT_DOWN;
	else if (IsKeyPressed(r, HID_KEY_LEFT))   out->hatSwitch = HatSwitch::HAT_LEFT;
	else                                      out->hatSwitch = HatSwitch::HAT_NEUTRAL;
}

// --- Securisation des pointeurs Controller::map (2026-08-03) --------------
// BUG REEL, trouve en analysant "la liaison rapide ne marche que
// partiellement" : `connectedControllers[i].map` pointe DANS le vector
// g_dynamicMappings (voir FindMapping / mapping.cpp). Toute mutation de ce
// vector invalide donc ces pointeurs :
//   - LoadMappingsFromFile() appelle ClearDynamicMappings() (vector.clear()),
//   - SetMouseProfile() / SetProfileButtonOverride() / SetProfileKeyOverride()
//     font des push_back() qui peuvent reallouer le buffer du vector.
// Le chemin de hot-reload gerait deja ce cas correctement (met les pointeurs
// a nullptr, recharge, puis les re-resout via FindMapping) - mais les deux
// chemins ajoutes au Jalon 7 (raccourci F9 et liaison rapide Back+Start) ne
// le faisaient PAS. Ils laissaient donc des pointeurs pendants, relus juste
// apres par XInputdReadStateHook (thread du JEU, en concurrence avec ce
// thread de polling) : mapping lu dans de la memoire liberee, d'ou un
// comportement erratique apres une liaison rapide - la liaison etait bien
// ecrite sur le disque (et loguee comme reussie), mais l'effet en jeu
// dependait de ce qui se trouvait a cet emplacement memoire. Risque de gel
// ou de crash a la cle, pas seulement un mapping ignore.
//
// Ces deux helpers factorisent le protocole correct, deja valide par le
// hot-reload : detacher AVANT la mutation (le thread du jeu voit alors
// "aucun mapping", comportement par defaut sans danger), re-resoudre APRES.
// La fenetre de course residuelle (thread jeu en plein dereferencement au
// moment precis du detachement) est la meme que celle deja acceptee partout
// ailleurs dans ce fichier pour les accumulateurs souris - non protegeable
// sans verrou, et un verrou dans le chemin USB/XInput est exactement ce qui
// a cause les gels historiques documentes dans PROJECT_NOTES.md.
// Marqueur de progression du thread de polling (2026-08-03) - voir le bloc
// de commentaire dans MappingManagerThreadProc. g_pollPhase est ecrase a
// chaque etape ; g_pollPhaseMax retient la plus avancee atteinte depuis le
// dernier heartbeat, ce qui distingue "il tourne normalement et fait tout le
// tour" de "il boucle mais s'arrete toujours a la meme etape".
volatile int g_pollPhase = 0;
volatile int g_pollPhaseMax = 0;

// Horodatage du dernier passage du thread de polling. Permet a un AUTRE
// thread (celui du jeu, via XInputdReadStateHook) de constater que le thread
// de polling ne repond plus - indispensable pour le cas ou il se BLOQUE au
// lieu de planter : __except ne se declenche pas, aucun heartbeat n'est plus
// ecrit, et le fichier de log ne peut plus rien nous apprendre puisque
// FlushLogBuffer vit precisement dans ce thread bloque.
volatile DWORD g_pollHeartbeatMs = 0;

static void MarkPollPhase(int phase) {
	// A step that took long names itself (2026-11-12: a 3.4 s hole at 21.8 s in a good boot,
	// at the very second where the frozen boots stop).
	const DWORD markNow = GetTickCount();
	if (g_pollHeartbeatMs != 0 && markNow - g_pollHeartbeatMs > 400)
		FileLog("Stall: %u ms from phase %d to phase %d", (unsigned int)(markNow - g_pollHeartbeatMs), (int)g_pollPhase, phase);
	g_pollPhase = phase;
	g_pollHeartbeatMs = markNow;
	if (phase > g_pollPhaseMax)
		g_pollPhaseMax = phase;
}

// Ajoute (ou complete) une entree de known_devices.txt. Appele a DEUX moments
// depuis le thread de polling : a la detection du peripherique, puis a nouveau
// quand son nom arrive - le descripteur de chaine est asynchrone et repond
// souvent apres la notification. AddKnownDevice ne renvoie true que si quelque
// chose a change, donc le fichier n'est reecrit que lorsque c'est utile.
static void UpdateKnownDevicesFile(uint16_t vid, uint16_t pid, bool isMouse, const char* name, bool isPad = false) {
	// Un peripherique reel a toujours un VID non nul. Une entree 0000:0000
	// provient forcement d'un emplacement pas encore renseigne, et polluait la
	// liste de selection d'un choix fantome impossible a identifier - constate
	// par l'utilisateur, qui a vu apparaitre trois entrees "0000:0000" sans
	// savoir laquelle correspondait a sa souris.
	if (vid == 0 && pid == 0)
		return;

	char devPath[128];
	StudioPath(devPath, sizeof(devPath), "known_devices.txt");
	std::ifstream devIn(devPath, std::ios::binary);
	std::string devContent((std::istreambuf_iterator<char>(devIn)), std::istreambuf_iterator<char>());
	devIn.close();

	std::vector<KnownDeviceEntry> knownDevices = ParseKnownDevices(devContent);
	if (!AddKnownDevice(&knownDevices, vid, pid, isMouse, name, isPad))
		return; // rien de neuf

	std::ofstream devOut(devPath, std::ios::binary | std::ios::trunc);
	if (devOut.is_open()) {
		std::string serialized = SerializeKnownDevices(knownDevices);
		devOut.write(serialized.data(), serialized.size());
	} else {
		FileLog("Failed to write known_devices.txt (VID:%04x PID:%04x)", vid, pid);
	}
}

static void DetachControllerMaps() {
	for (int i = 0; i < 4; i++)
		connectedControllers[i].map = nullptr;
	// Les autres coeurs doivent voir les pointeurs nuls AVANT que les tables
	// soient modifiees (AUDIT_2026-09.md, C3). L'ancienne generation n'est
	// de toute facon plus liberee immediatement (C4, ClearDynamicMappings).
	MemoryBarrier();
}

static void RebindControllerMaps() {
	// Les tables fraichement chargees doivent etre visibles AVANT les
	// pointeurs qui y menent (AUDIT_2026-09.md, C3).
	MemoryBarrier();
	for (int i = 0; i < 4; i++) {
		if (connectedControllers[i].controllerDriver) {
			connectedControllers[i].map = FindMapping(
				connectedControllers[i].vendorId,
				connectedControllers[i].productId);
		} else {
			connectedControllers[i].map = nullptr;
		}
	}
}

// --- Curseur de la Studio a 60 releves/s (2026-09-23) -----------------------
//
// Retour HB apres le correctif I2 : plus de clics fantomes, mais un curseur
// "pas tres reactif", avec une sensation de leger a-coup. Normal : la boucle
// ci-dessous ne tourne que toutes les 100 ms, donc le curseur de la Studio ne
// recevait que 10 releves par seconde et avancait par sauts de 100 ms.
//
// Quand - et SEULEMENT quand - la Studio est au premier plan (g_studioActive,
// voir AUDIT I1), l'attente de 100 ms en fin de boucle est decoupee en 5
// attentes de ~17 ms, avec un releve ecrit apres chacune : 6 releves par tour,
// soit ~60 par seconde, la cadence d'affichage de la Studio. La duree d'un
// tour reste ~100 ms : les compteurs F9 et liaison rapide, qui comptent en
// tours de boucle, ne changent pas.
//
// En jeu, rien ne change : ni ecriture supplementaire, ni cadence differente.
#define EN_FIX_STUDIO_FAST_INPUT 1

// Ecrit input_state.json avec l'etat courant. Meme format et memes
// accumulateurs que le bloc complet de MappingManagerThreadProc (phase 5),
// sans la logique F9 / liaison rapide, qui reste dans la boucle principale.
// Peripheriques REELLEMENT branches, pour les points vert/rouge de la Studio
// (2026-09-25, remarque HB : "le clavier n'est pas branche et j'ai toujours du
// vert"). Format : "k:046d:c33a;m:1532:0067" (k = clavier, m = souris), vide
// si rien. Avant, la Studio deduisait "branche" de la simple fraicheur de
// input_state.json - vraie des qu'UN des deux etait branche.
// A USB pad the plugin drives (not a keyboard or a mouse).
static bool AnyUsbPad() {
	for (int i = 0; i < 4; i++) {
		const Controller& c = connectedControllers[i];
		if (c.controllerDriver && !c.isKeyboard && !c.isMouse) return true;
	}
	return false;
}

// While the Studio runs: a pad known only from the built-in or SDL lists gets
// its mapping written to X360Remap.json once, so the Studio can edit it.
// Polling thread only; same detach / load / save / rebind steps as F9.
static void SaveBuiltInPadMappings() {
	static uint32_t s_done[8];
	static int s_doneCount = 0;
	for (int i = 0; i < 4; i++) {
		const Controller& c = connectedControllers[i];
		if (!c.controllerDriver || c.isKeyboard || c.isMouse || !c.map || IsDynamicMapping(c.map)) continue;
		const uint32_t key = ((uint32_t)c.vendorId << 16) | c.productId;
		bool done = false;
		for (int k = 0; k < s_doneCount; k++) done = done || s_done[k] == key;
		if (done || s_doneCount >= 8) continue;
		s_done[s_doneCount++] = key;
		DetachControllerMaps();
		bool ok = LoadMappingsForUpdateStudio();
		const HidDeviceMapping* builtIn = ok ? FindMapping((uint16_t)(key >> 16), (uint16_t)key) : nullptr;
		if (builtIn && !IsDynamicMapping(builtIn)) {
			AddDynamicCopy(*builtIn);
			ok = SaveMappingsToStudioConfig();
			if (ok) RememberOwnJsonWrite();
		}
		RebindControllerMaps();
		FileLog(ok ? "Pad %04x:%04x: built-in mapping written to X360Remap.json for the Studio"
		           : "Pad %04x:%04x: X360Remap.json unreadable or not written - nothing changed",
		        (unsigned int)(key >> 16), (unsigned int)(key & 0xFFFF));
		return;   // one per pass
	}
}

static void FormatConnectedDevices(char* out, int cap) {
	int p = 0;
	out[0] = '\0';
	for (int i = 0; i < 4 && p < cap - 16; i++) {
		const Controller& c = connectedControllers[i];
		if (!c.controllerDriver)
			continue;
		const char kind = c.isKeyboard ? 'k' : c.isMouse ? 'm' : 'c';   // c = USB controller (2026-11-03)
		int w = _snprintf(out + p, cap - p - 1, "%s%c:%04x:%04x", p ? ";" : "",
		                  kind, (unsigned int)c.vendorId, (unsigned int)c.productId);
		if (w <= 0) break;
		p += w;
	}
	out[cap - 1] = '\0';
}

// Logs the raw key reports captured by interruptHandler (see Controller::keyRaw).
// Polling thread only - this is where FileLog is safe.
static void LogCapturedKeyReports() {
	for (int i = 0; i < 4; i++) {
		Controller& c = connectedControllers[i];
		if (!c.controllerDriver) continue;
		while (c.keyRawLogged < c.keyRawCount && c.keyRawLogged < 8) {
			MemoryBarrier();
			const uint8_t* r = c.keyRaw[c.keyRawLogged];
			const int n = c.reportSize < 16 ? (int)c.reportSize : 16;
			char hex[16 * 3 + 1];
			int p = 0;
			for (int b = 0; b < n && p < (int)sizeof(hex) - 3; b++) {
				const char* d = "0123456789ABCDEF";
				hex[p++] = d[r[b] >> 4]; hex[p++] = d[r[b] & 15]; hex[p++] = ' ';
			}
			hex[p] = '\0';
			FileLog("Key report VID:%04x PID:%04x %hs interface: %hs", c.vendorId, c.productId,
				c.isExtraKeyboardIf ? "EXTRA" : "boot", hex);
			c.keyRawLogged++;
		}
	}
}

static void WriteInputStateSnapshot() {
	if (g_studioDriveIsOwnLink) return;   // see g_studioDriveIsOwnLink
	int firstKeyboard = -1, firstMouse = -1;
	for (int i = 0; i < 4; i++) {
		if (!connectedControllers[i].controllerDriver)
			continue;
		if (connectedControllers[i].isKeyboard && !connectedControllers[i].isExtraKeyboardIf && firstKeyboard == -1) firstKeyboard = i;
		if (connectedControllers[i].isMouse && firstMouse == -1)       firstMouse = i;
	}
	if (firstKeyboard == -1 && firstMouse == -1 && !AnyUsbPad())
		return;

	uint8_t keyRaw = AnyKeyboardKeyCode();
	uint8_t mouseRaw = (firstMouse != -1) ? connectedControllers[firstMouse].lastMouseButtonIdx : 0xFF;
	int keyCode = (keyRaw == 0xFF) ? -1 : (int)keyRaw;
	int mouseButton = (mouseRaw == 0xFF) ? -1 : (int)mouseRaw;

	int32_t mouseDX = 0, mouseDY = 0, wheelDelta = 0;
	int mouseButtonsMask = 0;
	if (firstMouse != -1) {
		Controller& m = connectedControllers[firstMouse];
		// Lecture + remise a zero ATOMIQUES : le callback USB ajoute a ces
		// compteurs en parallele, sur un autre coeur. Une lecture suivie d'une
		// remise a zero separee pouvait perdre le mouvement arrive entre les
		// deux - une des causes possibles des a-coups du curseur.
		mouseDX    = (int32_t)InterlockedExchange((volatile LONG*)&m.cursorAccumX, 0);
		mouseDY    = (int32_t)InterlockedExchange((volatile LONG*)&m.cursorAccumY, 0);
		wheelDelta = (int32_t)InterlockedExchange((volatile LONG*)&m.uiWheelAccum, 0);
		mouseButtonsMask = m.lastMouseButtonsMask;
	}

	char rawHex[40];
	rawHex[0] = '\0';
	{
		int p = 0;
		uint8_t n = g_rawMouseLen;
		if (n > 16) n = 16;
		for (uint8_t i = 0; i < n && p < (int)sizeof(rawHex) - 3; i++) {
			int w = _snprintf(rawHex + p, sizeof(rawHex) - p - 1, "%02x", (unsigned int)g_rawMouseLatest[i]);
			if (w <= 0) break;
			p += w;
		}
		rawHex[sizeof(rawHex) - 1] = '\0';
	}

	char devs[80];
	FormatConnectedDevices(devs, sizeof(devs));
	char json[512];
	int len = _snprintf(json, sizeof(json),
		"{ \"keyCode\": %d, \"mouseButton\": %d, \"mouseDX\": %ld, \"mouseDY\": %ld, \"mouseButtons\": %d, \"wheelDelta\": %ld, \"rawLen\": %d, \"raw\": \"%hs\", \"rawSeq\": %lu, \"tick\": %lu, \"devs\": \"%hs\", \"padBtn\": \"0x%04lx\" }",
		keyCode, mouseButton, (long)mouseDX, (long)mouseDY, mouseButtonsMask, (long)wheelDelta,
		(int)g_rawMouseLen, rawHex, (unsigned long)g_rawMouseSeq, GetTickCount(), devs, (unsigned long)g_padButtonsHeld);
	if (len > 0) {
		char statePath[128];
		StudioPath(statePath, sizeof(statePath), "input_state.json");
		std::ofstream f(statePath, std::ios::binary | std::ios::trunc);
		if (f.is_open())
			f.write(json, len);
	}
}

// --- X360Remap_status.json (2026-10-23, CONTRACT.md) -----------------------
// The truth about the console for the Studio and the Windows app: plugin
// version, devices plugged in NOW, game, direct aim, players mode. Written
// every 2 s from the polling thread only, and only while someone reads it:
//   - the Studio runs (studio_active.flag), or
//   - a PC is linked: pc_link.flag whose CONTENT changed in the last 10 s
//     (the Windows app rewrites it every few seconds; content, not the file
//     time, so the console and PC clocks never matter).
// Nothing is written when nobody reads it (flash wear, see AUDIT I1).
static bool PcLinkActive() {
	static char  s_last[64];
	static int   s_lastLen = -1;
	static DWORD s_changedAt = 0;
	char path[128];
	StudioPath(path, sizeof(path), "pc_link.flag");
	char buf[64];
	int n = -1;
	{
		std::ifstream f(path, std::ios::binary);
		if (f.is_open()) {
			f.read(buf, sizeof(buf));
			n = (int)f.gcount();
		}
	}
	const DWORD now = GetTickCount();
	if (n < 0) { s_lastLen = -1; return false; }          // no file: no PC
	if (s_lastLen < 0) {
		// First sight of the file: remember it, but a file left over from an
		// old session is not a link - only a CHANGE afterwards is.
		memcpy(s_last, buf, n); s_lastLen = n;
		return false;
	}
	if (n != s_lastLen || memcmp(buf, s_last, n) != 0) {
		memcpy(s_last, buf, n); s_lastLen = n;
		s_changedAt = now;
	}
	return s_changedAt != 0 && (now - s_changedAt) < 10000;
}

static int JsonEscapeAscii(char* out, int cap, const char* in) {
	int p = 0;
	for (int i = 0; in && in[i] && p < cap - 3; i++) {
		const unsigned char ch = (unsigned char)in[i];
		if (ch == '"' || ch == '\\') { out[p++] = '\\'; out[p++] = (char)ch; }
		else if (ch >= 0x20 && ch < 0x7F) out[p++] = (char)ch;
	}
	out[p] = '\0';
	return p;
}

static void WriteStatusFile() {
	if (g_studioDriveIsOwnLink) return;   // see g_studioDriveIsOwnLink
	char json[2048];
	int p = 0;
	const uint32_t title = g_lastTitleId;
	char tv[24] = "";
	if (title != 0) {
		const uint32_t v = GetTitleVersion();
		if (v) _snprintf(tv, sizeof(tv) - 1, "%u.%u.%u.%u", (unsigned)(v >> 28), (unsigned)((v >> 24) & 0xF),
			(unsigned)((v >> 8) & 0xFFFF), (unsigned)(v & 0xFF));
		tv[sizeof(tv) - 1] = 0;
	}
	const DWORD now = GetTickCount();
	const char* aim = "off";
	char aimName[96] = "";
	const int mb = g_mousehookBuild;
	const int li = g_learnedActiveIdx;
	if (mb >= 0) { aim = "builtin"; JsonEscapeAscii(aimName, sizeof(aimName), kCodBuilds[mb].name); }
	else if (li >= 0 && li < g_learnedCount && (now - g_learnedActiveTick) < 3000) {
		aim = "learned"; JsonEscapeAscii(aimName, sizeof(aimName), g_learned[li].name);
	}
	const bool real = g_realPadTick != 0 && (now - g_realPadTick) < 3000;
	p += _snprintf(json + p, sizeof(json) - p - 1,
		"{\r\n  \"format\": 1,\r\n  \"plugin\": \"%s\",\r\n  \"dashboard\": %d,\r\n  \"uptimeMs\": %lu,\r\n"
		"  \"title\": \"%08x\",\r\n  \"titleVersion\": \"%s\",\r\n  \"directAim\": \"%s\",\r\n  \"directAimName\": \"%s\",\r\n"
		"  \"players\": \"%s\",\r\n  \"paused\": %s,\r\n  \"realController\": %s,\r\n"
		"  \"logBuffer\": \"%08x\",\r\n  \"logBufferLen\": \"%08x\",\r\n  \"devices\": [",
		X360REMAP_VERSION, (int)XboxKrnlVersion->Build, (unsigned long)now,
		(unsigned int)title, tv, aim, aimName,
		g_playersSeparate ? "separate" : "merged", g_remapPaused ? "true" : "false", real ? "true" : "false",
		(unsigned int)(uintptr_t)g_logBuffer, (unsigned int)(uintptr_t)&g_logBufferLen);
	bool first = true;
	for (int i = 0; i < 4 && p < (int)sizeof(json) - 200; i++) {
		const Controller& c = connectedControllers[i];
		if (!c.controllerDriver || c.isExtraKeyboardIf) continue;
		char name[64];
		JsonEscapeAscii(name, sizeof(name), c.productName);
		const char* kind = c.isKeyboard ? "keyboard" : (c.isMouse ? "mouse" : "controller");
		p += _snprintf(json + p, sizeof(json) - p - 1,
			"%s\r\n    { \"vid\": \"%04x\", \"pid\": \"%04x\", \"name\": \"%s\", \"kind\": \"%s\", \"slot\": %d, \"mapped\": %s }",
			first ? "" : ",", (unsigned int)c.vendorId, (unsigned int)c.productId, name, kind,
			(int)(c.userIndex == 0xFF ? -1 : c.userIndex), c.map ? "true" : "false");
		first = false;
	}
	if (p < (int)sizeof(json) - 16) p += _snprintf(json + p, sizeof(json) - p - 1, "%s]\r\n}\r\n", first ? "" : "\r\n  ");
	json[sizeof(json) - 1] = 0;
	if (p <= 0 || p >= (int)sizeof(json)) return;
	char path[128];
	StudioPath(path, sizeof(path), "X360Remap_status.json");
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (f.is_open()) f.write(json, p);
}

// Ctrl + the key left of 1 (HID 0x35, "²" on AZERTY) starts or stops NetISO's clip
// recorder: it polls rec_toggle.txt and deletes it once read (X360_NetISO_Aurora/docs/
// video-capture-design.md, "Recorder contract"). Polling thread only.
static void RecordHotkey() {
	static bool s_down = false;
	static DWORD s_last = 0;
	const bool down = (AnyKeyboardModifiers() & 0x11) != 0 && HudKeyDown(0x35);
	const bool pressed = down && !s_down;
	s_down = down;
	if (!pressed || (GetTickCount() - s_last) < 1000) return;
	s_last = GetTickCount();
	char path[64];
	_snprintf(path, sizeof(path) - 1, "%s\\NetISO", g_studioDrive);
	path[sizeof(path) - 1] = '\0';
	if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;   // no recorder module on this console
	strcat(path, "\\rec_toggle.txt");
	if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return;   // the last one is not read yet
	HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) { FileLog("Record shortcut: could not create %s", path); return; }
	DWORD written = 0;
	WriteFile(f, "toggle", 6, &written, NULL);
	CloseHandle(f);
	FileLog("Record shortcut: Ctrl+0x35, rec_toggle.txt written");
}

static void HudShowToast(const char* text) {
	const int next = !g_hudToastSet;
	strncpy(g_hudToast[next], text, sizeof(g_hudToast[next]) - 1);
	g_hudToast[next][sizeof(g_hudToast[next]) - 1] = '\0';
	MemoryBarrier();
	g_hudToastSet = next;
	g_hudToastUntil = GetTickCount() + 4000;
}

#include "record_toast.h"

// Reads the recorder's state once a second in a game, so a recording started by
// Ctrl + 2 or by the PC is announced on screen. Polling thread only.
static void RecordToast() {
	static DWORD s_last = 0;
	static char s_prev[96] = "";
	static bool s_first = true;
	if (!g_hudGameReady || (GetTickCount() - s_last) < 1000) return;
	s_last = GetTickCount();
	char path[64];
	_snprintf(path, sizeof(path) - 1, "%s\\NetISO\\rec_state.txt", g_studioDrive);
	path[sizeof(path) - 1] = '\0';
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	char cur[96] = "";
	DWORD got = 0;
	ReadFile(f, cur, sizeof(cur) - 1, &got, NULL);
	CloseHandle(f);
	cur[got] = '\0';
	for (char* c = cur; *c; c++) if (*c == '\r' || *c == '\n') { *c = '\0'; break; }
	char text[56];
	RecordToastText(s_prev, cur, text, sizeof(text), g_pluginLang == LANG_FR);
	strcpy(s_prev, cur);
	if (s_first) { s_first = false; return; }   // an old state from before this game
	if (text[0]) HudShowToast(text);
}

// Direct aim settings for the overlay, twice a second. Upper case only: the font has no more.
static void UpdateHudLines() {
	{   // Guide open: no drawing now and for 1.5 s after (see g_hudUiQuietUntil)
		typedef BOOL (*XamIsUIActiveFn)();
		static XamIsUIActiveFn s_uiActive = NULL;
		static bool s_resolved = false;
		if (!s_resolved) {
			s_resolved = true;
			HANDLE xam = GetModuleHandleA("xam.xex");
			PVOID p = NULL;
			if (xam && XexGetProcedureAddress(xam, 748, &p) == 0) s_uiActive = (XamIsUIActiveFn)p;
			FileLog("Overlay: Guide check %s", s_uiActive ? "on" : "unavailable");
		}
		if (s_uiActive && s_uiActive()) g_hudUiQuietUntil = GetTickCount() + 1500;
	}
	static DWORD s_title = 0, s_titleSince = 0;
	if (g_lastTitleId != s_title) { s_title = g_lastTitleId; s_titleSince = GetTickCount(); g_hudGameReady = false; }
	if (IsGameTitle(s_title) && (GetTickCount() - s_titleSince) > 15000) g_hudGameReady = true;

	// The shortcut alone shows or hides the keyboard / controller drawing: for the
	// Windows key, a combo (Win+Shift+X) does not.
	static bool s_keyDown = false, s_keyAlone = false;
	const uint8_t mods = AnyKeyboardModifiers();
	const bool isWin = g_hudHotkey == 0xE3;
	const bool keyDown = isWin ? (mods & 0x88) != 0 : HudKeyDown((uint8_t)g_hudHotkey);
	if (keyDown && !s_keyDown) s_keyAlone = true;
	if (isWin && keyDown && ((mods & ~0x88) != 0 || AnyKeyboardKeyCode() < 0xE0)) s_keyAlone = false;
	if (!keyDown && s_keyDown && s_keyAlone) g_hudInputShown = !g_hudInputShown;
	s_keyDown = keyDown;
	// Shortcut + I shows or hides the console info line (HB, 2026-11-16). Until the
	// next Studio save, which reloads show_stats.
	static bool s_iDown = false;
	const bool iDown = keyDown && HudKeyDown(0x0C);   // HID usage 0x0C = I
	if (iDown && !s_iDown) g_hudShowStats = !g_hudShowStats;
	s_iDown = iDown;

	static DWORD s_last = 0;
	const DWORD now = GetTickCount();
	if ((now - s_last) < 500) return;
	s_last = now;
	const HidDeviceMapping* map = nullptr;
	for (int i = 0; i < 4 && !map; i++)
		if (connectedControllers[i].controllerDriver && connectedControllers[i].isMouse) map = connectedControllers[i].map;
	EffectiveMouseSettings e = ResolveEffectiveMouseSettings(map, g_lastTitleId);
	ApplyLiveOverrides(e, g_lastTitleId);
	const bool learned = g_learnedActiveIdx >= 0 && g_learnedActiveIdx < g_learnedCount && (now - g_learnedActiveTick) < 3000;
	static char s_old[HUD_LINES][HUD_LINE_CHARS];
	// shortcut: French or English only (Portuguese and Spanish get English), the panel font has no accents.
	const bool fr = g_pluginLang == LANG_FR;
	const bool aimOn = g_mousehookBuild >= 0 || learned;
	_snprintf(g_hudLines[0], HUD_LINE_CHARS - 1, fr ? "VISEE: %s" : "DIRECT AIM: %s", aimOn ? (fr ? "ACTIVE" : "ON") : "OFF");
	_snprintf(g_hudLines[1], HUD_LINE_CHARS - 1, "SENS: %d", (int)e.mouseSensitivity);
	_snprintf(g_hudLines[2], HUD_LINE_CHARS - 1, "ADS: %d%%", (int)(e.adsSensitivity ? e.adsSensitivity : 100));
	_snprintf(g_hudLines[3], HUD_LINE_CHARS - 1, fr ? "LISSAGE: %d MS" : "SMOOTHING: %d MS", (int)e.directAimSmoothingMs);
	_snprintf(g_hudLines[4], HUD_LINE_CHARS - 1, fr ? "TRANSITION: %d MS" : "ADS RAMP: %d MS", (int)e.adsRampMs);
	_snprintf(g_hudLines[5], HUD_LINE_CHARS - 1, fr ? "ZONE MORTE: %d" : "DEADZONE: %d", (int)e.deadzone);
	_snprintf(g_hudLines[6], HUD_LINE_CHARS - 1, fr ? "ANTI-ZONE: %d" : "ANTI-DEADZONE: %d", (int)e.antiDeadzone);
	if (memcmp(s_old, g_hudLines, sizeof(s_old)) != 0) {
		memcpy(s_old, g_hudLines, sizeof(s_old));
		g_hudInfoUntil = now + g_hudInfoSecs * 1000;
	}

	static DWORD s_lastFps = 0, s_lastTemps = 0;
	static int s_cpu = -1, s_gpu = -1;
	// In a game only, never at boot: the read came in with the boot hangs of 2026-10-30.
	if (g_hudShowStats && !g_hudNoTemps && g_hudGameReady && (now - s_lastTemps) >= 5000) {
		s_lastTemps = now;
		ReadSmcTemps(&s_cpu, &s_gpu);
	}
	if ((now - s_lastFps) >= 1000) {
		const LONG frames = InterlockedExchange(&g_hudFrames, 0);
		char line[sizeof(g_hudStats)];
		int n = _snprintf(line, sizeof(line) - 1, "FPS %d", (int)(frames * 1000 / (LONG)(now - s_lastFps)));
		if (s_cpu > 0 && n > 0) _snprintf(line + n, sizeof(line) - 1 - n, "   CPU %dC   GPU %dC", s_cpu, s_gpu);
		line[sizeof(line) - 1] = '\0';
		memcpy(g_hudStats, line, sizeof(line));
		s_lastFps = now;
	}

	// The player's own keys: what is saved in the Studio, or the default layout.
	const HidDeviceMapping* keys = nullptr;
	for (int i = 0; i < 4 && !keys; i++)
		if (connectedControllers[i].controllerDriver && connectedControllers[i].isKeyboard &&
			!connectedControllers[i].isExtraKeyboardIf) keys = connectedControllers[i].map;
	static HidKeyMapEntry s_map[MAX_MERGED_KEYS];
	const uint8_t mapped = ResolveMergedKeyMap(keys, g_lastTitleId, s_map, MAX_MERGED_KEYS);
	static const HidKeyMapEntry kDefault[] = {
		{0x1A, KEYACT_LSTICK_UP}, {0x04, KEYACT_LSTICK_LEFT}, {0x16, KEYACT_LSTICK_DOWN}, {0x07, KEYACT_LSTICK_RIGHT},
		{0x2C, KEYACT_A}, {0x06, KEYACT_B}, {0x15, KEYACT_X}, {0x08, KEYACT_Y}, {0x14, KEYACT_LB}, {0x09, KEYACT_RB},
		{0xE1, KEYACT_LT}, {0xE0, KEYACT_RT},
	};
	uint8_t codes[MAX_MERGED_KEYS], actions[MAX_MERGED_KEYS];
	int count = 0;
	if (mapped == 0)
		for (size_t i = 0; i < sizeof(kDefault) / sizeof(kDefault[0]); i++) { codes[count] = kDefault[i].code; actions[count++] = kDefault[i].action; }
	for (int i = 0; i < mapped; i++) { codes[count] = s_map[i].code; actions[count++] = s_map[i].action; }
	const int next = !g_hudCapSet;
	int w = 0, h = 0;
	g_hudCapCount[next] = HudLayout(codes, actions, count, g_hudCaps[next], 420, g_hudStyle ? 60 : 0, g_hudStyle ? 38 : 35, &w, &h);
	g_hudBlockW = (DWORD)w;
	const DWORD blockHeight = h > 112 ? (DWORD)h : 112;
	g_hudBlockY = g_hudKeysY + blockHeight > 712 ? 712 - blockHeight : g_hudKeysY;
	MemoryBarrier();
	g_hudCapSet = next;
}

unsigned int __stdcall MappingThreadProc(void* param);
unsigned int __stdcall MappingManagerThreadProc(void* param){
	PollStackPaint();
	FileLog("Polling thread stack: %u KB (base %08x, limit %08x)%hs",
		(unsigned int)((g_pollStackBase - ThreadStackLimit()) / 1024), (unsigned int)g_pollStackBase,
		(unsigned int)ThreadStackLimit(), g_pollStackLimit ? "" : " - not measured");
	// One-time init: connectedControllers[] starts zero-initialized (global
	// array), which would otherwise make lastKeyCode/lastMouseButtonIdx read
	// as 0 (a real HID code / button index) instead of the "nothing pressed"
	// sentinel until each slot's first real HID report arrives. Matching
	// reset also happens in HidRemoveDeviceHook when a slot is freed.
	for (int i = 0; i < 4; i++) {
		connectedControllers[i].lastKeyCode = 0xFF;
		connectedControllers[i].lastMouseButtonIdx = 0xFF;
	}

	// --- Blindage + auto-diagnostic du thread de polling (2026-08-03) -------
	// CAUSE RACINE confirmee par les donnees : ce thread meurt silencieusement
	// (input_state.json fige a tick=519312 alors que la console tournait
	// encore 13 minutes plus tard). Sa mort emporte le hot-reload, donc le
	// plugin reste fige sur la configuration chargee AU DEMARRAGE - d'ou tous
	// les symptomes rapportes d'un coup : le clic gauche mappe sur A ne fait
	// rien, choisir une direction du D-Pad ne fait rien, la liaison rapide
	// "marche parfois" (elle marchait tant que le thread vivait). Le fichier
	// de config, lui, etait correct depuis le debut.
	//
	// Le debogage live (rgh debug watch) est inutilisable ici : XBDM est coupe
	// des qu'un jeu demarre. Le thread doit donc raconter sa propre mort dans
	// le FICHIER, sans connexion exterieure.
	//
	// Trois mecanismes :
	//   1. g_pollPhase - marqueur mis a jour a chaque etape de la boucle, pour
	//      savoir OU il est mort et pas seulement QUAND.
	//   2. __try/__except autour du corps de boucle - capture l'exception,
	//      logue phase + code, FORCE l'ecriture disque immediatement (sans
	//      attendre le flush de fin de boucle, qui ne viendrait jamais), puis
	//      CONTINUE au lieu de laisser le thread mourir. Le plugin survit donc
	//      a un incident au lieu de se figer definitivement.
	//   3. Heartbeat periodique - date la derniere activite dans le fichier,
	//      pour distinguer "mort" de "bloque" au prochain test.
	DWORD lastHeartbeatMs = GetTickCount();
	DWORD lastRawDumpMs = GetTickCount();

	// This thread monitors for controllers needing mapping and spawns mapping threads
	while (true) {
	__try {
		MarkPollPhase(1);

		// Libere les HidControllerExtension mis en attente au debranchement,
		// une fois le delai de securite ecoule (voir QueueDriverFree). Doit
		// rester dans ce thread : jamais depuis un callback USB.
		DrainDriverFreeQueue();
		LogCapturedKeyReports();

		// Idem pour le flush disque force (voir g_forceFlushRequested) : un
		// callback USB peut le demander, seul ce thread l'execute reellement.
		if (g_forceFlushRequested) {
			g_forceFlushRequested = false;
			FlushLogBuffer();
		}

		// Heartbeat : une ligne toutes les 30s, assez pour dater une mort au
		// fichier sans noyer le log sur une longue session.
		//
		// SAUF PENDANT LA PREMIERE MINUTE (2026-08-23) : le gel reellement
		// constate se produit dans les toutes premieres secondes, entre le
		// lancement d'Aurora (~4.7s) et le premier heartbeat a 30s - donc en
		// plein dans un intervalle ou ce journal n'ecrit strictement rien.
		// Resultat : le fichier s'arrete sur "Title changed" et on ignore a
		// quelle seconde, et a quelle etape de la boucle, tout s'arrete.
		// Pendant 60s on passe donc a une ligne par seconde, poussee sur le
		// disque immediatement. Apres cette minute, retour au rythme normal :
		// aucun cout sur une session longue.
		DWORD sinceBoot = GetTickCount() - g_bootBlackoutStartedAt;
		// Also while the Studio runs (2026-10-30): the "freeze with the Studio
		// open" logs stopped at the last 30 s flush, so they could not tell a
		// frozen console from a Studio that hung while the plugin lived on.
		LogOverlayProbe(g_lastTitleId);
		if (g_overlayProbe && g_hudEnabled) UpdateHudLines();
		RecordHotkey();
		if (g_overlayProbe && g_hudEnabled) RecordToast();
		bool fineGrained = (sinceBoot < 60000) || g_studioActive;
		// Kernel 17559 only: its display mode number (FREEZE_STATIC_ANALYSIS.md). -1 means
		// the HDMI screen changed or a fatal error is being shown; the kernel faults on it.
		if (XboxKrnlVersion->Build == 17559) {
			static DWORD lastVideoMode = 0xFFFFFFFE;
			const DWORD videoMode = *(volatile DWORD*)0x80170D84;
			if (videoMode != lastVideoMode) {
				FileLog("Video mode %d -> %d (EDID sum %08x)", (int)lastVideoMode, (int)videoMode,
					(unsigned int)*(volatile DWORD*)0x801839D4);
				lastVideoMode = videoMode;
			}
		}
		if ((GetTickCount() - lastHeartbeatMs) >= (fineGrained ? 1000 : 30000)) {
			lastHeartbeatMs = GetTickCount();
			// No SMC temperatures here: that read came in with the boot hangs,
			// a console shutdown and a game freeze of 2026-10-30. Off until proven.
			FileLog("Heartbeat polling: alive (phase=%d, max phase=%d, titleId=%08x, USB error stops=%u)",
				(int)g_pollPhase, (int)g_pollPhaseMax, (unsigned int)g_lastTitleId,
				(unsigned int)g_interruptErrorStops);
			{
				static DWORD s_deepest = 0;
				const DWORD deep = PollStackDeepest();
				if (deep > s_deepest + 512) {
					s_deepest = deep;
					FileLog("Stack: deepest %u of %u bytes", (unsigned int)deep, (unsigned int)(g_pollStackBase - g_pollStackLimit));
				}
			}
			if (fineGrained)
				FlushLogBuffer();   // chaque seconde compte : sur le disque tout de suite
			g_pollPhaseMax = 0;
		}

		// DIAG molette brute - formatage et ecriture ICI (thread sur), a
		// partir des octets copies par le callback USB. Voir g_rawMouseLatest
		// et le commentaire dans HidFillBootMouseState expliquant pourquoi ce
		// travail ne doit surtout pas etre fait dans le callback.
		if (g_rawMouseLen > 0 && (GetTickCount() - lastRawDumpMs) >= 3000) {
			lastRawDumpMs = GetTickCount();

			char hexLatest[32]; int p = 0; hexLatest[0] = '\0';
			for (uint8_t i = 0; i < g_rawMouseLen && p < (int)sizeof(hexLatest) - 4; i++) {
				int n = _snprintf(hexLatest + p, sizeof(hexLatest) - p - 1, "%02x", (unsigned int)g_rawMouseLatest[i]);
				if (n <= 0) break;
				p += n;
			}
			hexLatest[sizeof(hexLatest) - 1] = '\0';

			if (g_rawMouseBeyondCount > 0) {
				char hexBeyond[32]; int q = 0; hexBeyond[0] = '\0';
				for (uint8_t i = 0; i < g_rawMouseBeyondLen && q < (int)sizeof(hexBeyond) - 4; i++) {
					int n = _snprintf(hexBeyond + q, sizeof(hexBeyond) - q - 1, "%02x", (unsigned int)g_rawMouseBeyond[i]);
					if (n <= 0) break;
					q += n;
				}
				hexBeyond[sizeof(hexBeyond) - 1] = '\0';
				FileLog("DIAG raw mouse: len=%d current=[%hs] BEYOND_XY=[%hs] (%u times)",
					(int)g_rawMouseLen, hexLatest, hexBeyond, (unsigned int)g_rawMouseBeyondCount);
			} else {
				FileLog("DIAG raw mouse: len=%d current=[%hs] no non-zero byte beyond X/Y since boot",
					(int)g_rawMouseLen, hexLatest);
			}
		}

		for (int i = 0; i < 4; i++) {
			// Mouse-detected notification, sent exactly once per device from this
			// safe polling thread context (see the comment on Controller::mouseNotified
			// for why this must not be called directly from the USB completion
			// callback chain).
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].isMouse &&
				!connectedControllers[i].mouseNotified) {
				connectedControllers[i].mouseNotified = true;

				// Diagnostic: show whether a JSON/static mapping was actually
				// resolved for this device, since we have no DbgPrint visibility
				// yet - this answers "is ctrl->map really set" directly on screen.
				// Also show which XInput user slot XAM actually assigned this
				// device to (c.userIndex, set by XamUserBindDeviceCallback right
				// after the endpoint was opened - see setConfigurationComplete).
				// Needed to check whether the mouse lands on slot 0 (player 1) or
				// gets pushed to another slot when a real controller is already
				// connected - suspected cause of the split-screen/"controller
				// stopped working" behavior seen when both are plugged in at once.
				const HidDeviceMapping* m = connectedControllers[i].map;
				wchar_t msg[160];
				swprintf(msg, 160, WTr(STR_MOUSE_DETECTED_FMT),
					connectedControllers[i].vendorId,
					connectedControllers[i].productId,
					m ? "YES" : "NO",
					m ? m->buttonMapCount : 0,
					m ? m->mouseSensitivity : 0,
					(int)connectedControllers[i].userIndex);
				// ORDRE INVERSE + FLUSH DISQUE le 2026-08-22 : le journal doit
				// atteindre le disque AVANT l'appel a XNotifyUI, pas apres.
				// Auparavant la notification passait en premier, si bien que le
				// gel qu'elle provoquait emportait avec lui la seule ligne qui
				// aurait permis de le situer - le journal s'arretait juste
				// avant, sans jamais dire pourquoi. Meme principe que le flush
				// pratique avant le reset USB differe, plus bas.
				FileLog("Mouse detected. VID:%04x PID:%04x map:%hs btns:%d sens:%d slot:%d",
					connectedControllers[i].vendorId,
					connectedControllers[i].productId,
					m ? "YES" : "NO",
					m ? m->buttonMapCount : 0,
					m ? m->mouseSensitivity : 0,
					(int)connectedControllers[i].userIndex);
				FlushLogBuffer();
				// No "mouse detected" pop-up for the mouse PART of a keyboard /
				// keypad (Tartarus wheel, 2026-10-12): the player saw "mouse
				// detected" twice for one real mouse. Same VID:PID as a keyboard
				// already connected = part of that device.
				bool partOfKeyboard = false;
				for (int k = 0; k < 4; k++)
					if (k != i && connectedControllers[k].controllerDriver && connectedControllers[k].isKeyboard &&
						connectedControllers[k].vendorId == connectedControllers[i].vendorId &&
						connectedControllers[k].productId == connectedControllers[i].productId)
						partOfKeyboard = true;
				if (!partOfKeyboard)
					XNotifyUI(XNOTIFYUI_CUSTOM, msg);

				// Jalon 8 (suite, demande utilisateur 2026-08-03) - persiste ce
				// VID/PID dans known_devices.txt pour que l'ecran de selection
				// d'application.xex le propose SANS saisie manuelle et SANS
				// attendre que l'utilisateur configure quoi que ce soit pour ce
				// device - detection automatique au branchement, exactement
				// comme demande. Meme pattern I/O que known_titles.txt plus bas
				// dans ce fichier (lire, ajouter si nouveau, reecrire seulement
				// si modifie).
				UpdateKnownDevicesFile(connectedControllers[i].vendorId,
					connectedControllers[i].productId, true,
					connectedControllers[i].productName);
			}

			// Keyboard-detected notification, same safe-thread pattern as the
			// mouse one above. Detection-only stage (see Controller::isKeyboard) -
			// no key handling yet, this just confirms on hardware that a given
			// keyboard is being classified correctly before any input logic is
			// built on top of it.
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].isKeyboard &&
				!connectedControllers[i].isExtraKeyboardIf &&
				!connectedControllers[i].keyboardNotified) {
				connectedControllers[i].keyboardNotified = true;

				wchar_t msg[128];
				swprintf(msg, 128, WTr(STR_KEYBOARD_READY_FMT),
					(int)connectedControllers[i].userIndex);
				// Meme inversion que pour la souris juste au-dessus, meme raison.
				FileLog("Keyboard ready. VID:%04x PID:%04x slot:%d",
					connectedControllers[i].vendorId,
					connectedControllers[i].productId,
					(int)connectedControllers[i].userIndex);
				FlushLogBuffer();
				XNotifyUI(XNOTIFYUI_CUSTOM, msg);

				// Jalon 8 (suite) - meme persistance que pour la souris
				// ci-dessus, voir son commentaire pour le detail complet.
				UpdateKnownDevicesFile(connectedControllers[i].vendorId,
					connectedControllers[i].productId, false,
					connectedControllers[i].productName);
			}

			// A USB pad is listed too (type C), with its SDL name until the USB name arrives.
			if (connectedControllers[i].controllerDriver && !connectedControllers[i].isMouse &&
				!connectedControllers[i].isKeyboard && !connectedControllers[i].padListed) {
				connectedControllers[i].padListed = true;
				const KnownControllerEntry* kc = FindKnownController(connectedControllers[i].vendorId, connectedControllers[i].productId);
				const char* name = connectedControllers[i].productName[0] ? connectedControllers[i].productName : kc ? kc->name : "";
				UpdateKnownDevicesFile(connectedControllers[i].vendorId, connectedControllers[i].productId, false, name, true);
			}

			// Check if controller exists, has reportInfo, but no mapping, and mapping not already in progress.
			// Keyboards are excluded for the same reason mice are: the assistant
			// looks for gamepad-style buttons (usage page 0x09) which a keyboard
			// report descriptor doesn't have (its keys live on usage page 0x07),
			// so it would just run forever finding nothing.
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].reportInfo &&
				!connectedControllers[i].map &&
				!connectedControllers[i].isMouse &&
				!connectedControllers[i].isKeyboard &&
				!g_mappingState.active) {

				SafeDbgPrint("X360Remap: Starting mapping for controller %d (VID:%04x PID:%04x)\n",
					i, connectedControllers[i].vendorId, connectedControllers[i].productId);

				// Initialize mapping state
				memset(&g_mappingState, 0, sizeof(MappingState));
				g_mappingState.active = true;
				g_mappingState.reportId = connectedControllers[i].reportId;
				g_mappingState.reportInfo = connectedControllers[i].reportInfo;
				g_mappingState.controllerIndex = i;
				g_mappingState.pressedButtonIdx = 0xFF;

				HANDLE mappingThread = MakeThread((LPTHREAD_START_ROUTINE)MappingThreadProc, &connectedControllers[i]);
				if (mappingThread) {
					CloseHandle(mappingThread);
				} else {
					SafeDbgPrint("X360Remap: Failed to create mapping thread!\n");
					g_mappingState.active = false;
				}
			}
		}

		// --- Automatic title-switch rebind: REVERTED (2026-07-31) ---
		// Two attempts at this (full USB reset, then a lighter XAM-only
		// rebind) both caused unpredictable regressions on real hardware
		// across different games (freezes, wireless controller going
		// undetectable, inputs degrading mid-session) - see PROJECT_NOTES.md.
		// Reverting to the known-good behavior: the mouse works reliably once
		// manually unplugged/replugged after a title launch. Revisit this only
		// once real DbgPrint visibility is available (Xbox 360 Neighborhood /
		// VS remote attach) - iterating blind on this specific problem via
		// hardware test cycles alone has not been reliable.
		// Descriptor path (gamepads and other non-boot HID): second half of the
		// init, handed over by setConfigurationComplete (2026-10-23).
		if (g_descReady) {
			g_descReady = 0;
			MemoryBarrier();
			HidControllerExtension* d = g_descDriver;
			g_descDriver = nullptr;
			if (d) {
				__try { FinishDescriptorInit(d); }
				__except (EXCEPTION_EXECUTE_HANDLER) {
					FileLog("EXCEPTION FinishDescriptorInit: code=%08x - device given up", (unsigned int)GetExceptionCode());
					g_InitState = InitState::INIT_FAILED;
					g_deviceInitBusy = false;
				}
			}
		}

		// --- Parsing du descripteur de rapport (2026-08-03) ----------------
		// Le callback USB s'est contente de poser un drapeau ; le travail lourd
		// (USB_ProcessHIDReport, parseur LUFA) se fait ICI, dans le seul
		// contexte sur de ce fichier. En cas de succes le peripherique bascule
		// du chemin "boot" a disposition figee vers le chemin descripteur, qui
		// sait lire X, Y et la MOLETTE a leur vraie place, quel que soit le
		// modele. En cas d'echec il reste en mode boot : aucune regression.
		MarkPollPhase(8);

		// Conversion du nom du peripherique (UTF-16LE -> ASCII), ici et pas
		// dans le callback USB. Un descripteur de chaine commence par
		// [longueur totale][type 0x03], suivi des caracteres sur 2 octets.
		// On ne garde que l'ASCII imprimable : les rares accents ou ideogrammes
		// deviendraient de toute facon illisibles dans l'atlas de police actuel,
		// mieux vaut un nom tronque proprement qu'une suite de carres.
		for (int i = 0; i < 4; i++) {
			Controller& nc = connectedControllers[i];
			if (!nc.controllerDriver || nc.nameParsed || !nc.nameReady)
				continue;
			nc.nameParsed = true;

			uint8_t total = nc.nameBuf[0];
			if (total < 4 || nc.nameBuf[1] != 0x03)
				continue; // pas un descripteur de chaine exploitable
			if (total > sizeof(nc.nameBuf))
				total = (uint8_t)sizeof(nc.nameBuf);

			size_t out = 0;
			for (uint8_t p = 2; p + 1 < total && out < sizeof(nc.productName) - 1; p += 2) {
				uint16_t ch = (uint16_t)nc.nameBuf[p] | ((uint16_t)nc.nameBuf[p + 1] << 8);
				if (ch >= 0x20 && ch < 0x7F)
					nc.productName[out++] = (char)ch;
				else if (ch == 0)
					break;
			}
			// Espaces de fin : beaucoup de peripheriques en mettent.
			while (out > 0 && nc.productName[out - 1] == ' ')
				out--;
			nc.productName[out] = '\0';

			if (out > 0) {
				FileLog("Device name VID:%04x PID:%04x = \"%hs\"",
					nc.vendorId, nc.productId, nc.productName);
				// Le nom arrive apres la notification de detection, qui a deja
				// ecrit l'entree sans lui : on complete le fichier maintenant.
				// AddKnownDevice ne renvoie true que s'il y a du nouveau, donc
				// pas de reecriture inutile.
				UpdateKnownDevicesFile(nc.vendorId, nc.productId,
					nc.isMouse, nc.productName, !nc.isMouse && !nc.isKeyboard);
			}
		}

		for (int i = 0; i < 4; i++) {
			Controller& dc = connectedControllers[i];
			if (!dc.controllerDriver || !dc.isBootProtocol || dc.reportDescParsed)
				continue;

			// Delai de repli : si la reponse n'arrive jamais (device muet sur
			// l'endpoint de controle), on cesse d'attendre et on garde le mode
			// boot, plutot que de reessayer indefiniment.
			if (!dc.reportDescReady && !dc.reportDescFailed) {
				if (dc.reportDescRequestedMs != 0 &&
					(GetTickCount() - dc.reportDescRequestedMs) > 3000) {
					dc.reportDescParsed = true;
					FileLog("HID descriptor: no answer for VID:%04x PID:%04x - boot mode kept",
						dc.vendorId, dc.productId);
				}
				continue;
			}

			dc.reportDescParsed = true;

			if (dc.reportDescFailed) {
				FileLog("HID descriptor unavailable for VID:%04x PID:%04x - boot mode kept",
					dc.vendorId, dc.productId);
				continue;
			}

			// CLAVIERS EXCLUS, et c'est structurel (2026-08-04). Le chemin
			// descripteur n'a AUCUN gestionnaire clavier : la repartition dans
			// HidInterruptComplete est "boot -> souris descripteur -> gamepad".
			// Un clavier sorti du chemin boot retombe donc sur le chemin
			// GAMEPAD, son keyMap n'est plus jamais applique et il devient
			// totalement muet - exactement ce qu'a constate l'utilisateur au
			// premier test reussi du descripteur.
			// Le chemin boot fonctionne parfaitement pour les claviers depuis
			// le debut : ils n'ont rien a y gagner, et tout a y perdre.
			if (!dc.isMouse) {
				FileLog("HID descriptor received for VID:%04x PID:%04x but not applied (keyboard) - boot path is fine for it",
					dc.vendorId, dc.productId);
				continue;
			}

			HID_ReportInfo_t* info = nullptr;
			uint8_t parseResult = USB_ProcessHIDReport(dc.reportDescBuf, dc.reportDescLen, &info);
			if (parseResult != HID_PARSE_Successful || !info) {
				FileLog("HID descriptor unreadable for VID:%04x PID:%04x (error %d) - boot mode kept",
					dc.vendorId, dc.productId, (int)parseResult);
				continue;
			}

			uint8_t rid = FindGamepadReportId(info);
			bool looksMouse = DetectIsMouse(info, rid);
			bool looksKeyboard = !looksMouse && DetectIsKeyboard(info);

			// ASSOUPLI le 2026-08-04 : la version precedente refusait de
			// basculer quand le descripteur ne confirmait pas la
			// classification USB (DetectIsMouse cherche le drapeau
			// HID_IOF_RELATIVE sur X, que toutes les souris ne presentent pas
			// de la meme facon). C'etait bloquer sur un detail de
			// classification alors qu'on ne se sert du descripteur QUE pour
			// savoir ou lire les champs - et la classification, on l'a deja,
			// fiable, par le protocole d'interface USB (1=clavier, 2=souris).
			// Blocage tres plausible des souris a paquet long, celles-la memes
			// qu'on cherche a reparer. On garde donc la classification USB et
			// on se contente de signaler le desaccord.
			if ((dc.isMouse && !looksMouse) || (dc.isKeyboard && !looksKeyboard)) {
				FileLog("HID descriptor: classification mismatch for VID:%04x PID:%04x (descriptor mouse=%d kbd=%d) - keeping the USB one, fields still read",
					dc.vendorId, dc.productId, (int)looksMouse, (int)looksKeyboard);
			}

			// ORDRE IMPORTANT : reportInfo AVANT isBootProtocol. Le thread des
			// interruptions teste isBootProtocol en premier ; une fois qu'il
			// est faux il dereference reportInfo, qui doit donc deja etre pose.
			dc.reportInfo = info;
			dc.reportId = rid;
			// AUDIT_2026-09.md, C3 : sans barriere, un autre coeur peut voir
			// isBootProtocol = false AVANT reportInfo (Xbox 360 reordonne les
			// ecritures - voir "Lockless Programming Considerations for Xbox
			// 360", Microsoft). MemoryBarrier() = lwsync sur Xbox 360.
			MemoryBarrier();
			dc.isBootProtocol = false;

			FileLog("HID descriptor parsed for VID:%04x PID:%04x (len=%d reportId=%d) - native reading on, wheel included%hs",
				dc.vendorId, dc.productId, (int)dc.reportDescLen, (int)rid,
				dc.reportProtocolRequested ? ", SET_PROTOCOL(report) sent" : "");
		}

		MarkPollPhase(2);
		DWORD currentTitleId = XamGetCurrentTitleId();
		// BUG REEL corrige (2026-08-08, signale par l'utilisateur : la souris
		// branchee avant/pendant le demarrage d'Aurora n'est detectee qu'apres
		// un debranchement/rebranchement manuel, et la notification de
		// detection n'apparait qu'au lancement d'un jeu). Cause : g_titleStableSince
		// demarre a 0 et n'etait mis a jour QUE lors d'un changement de
		// titleId - or rester sur le dashboard (titleId 0 en continu depuis le
		// boot) ne DECLENCHE jamais ce changement, donc g_titleStableSince
		// restait a 0 indefiniment tant qu'aucun jeu n'etait lance. Combine a
		// la garde `g_lastTitleId != 0` qui existait sur le reset USB differe
		// plus bas (destine a "reprendre les peripheriques deja branches une
		// fois le dashboard stabilise"), ce reset ne pouvait donc JAMAIS se
		// declencher tant qu'on etait sur Aurora - seulement ~10s apres avoir
		// lance un vrai jeu, d'ou le symptome observe. Initialise desormais
		// g_titleStableSince des la toute premiere iteration de ce thread,
		// que le titre de depart soit 0 (dashboard) ou non.
		static bool s_titleTrackingInitialized = false;
		if (!s_titleTrackingInitialized) {
			s_titleTrackingInitialized = true;
			g_titleStableSince = GetTickCount();
		}
		// BUG REEL corrige (2026-08-08, signale par l'utilisateur : un profil
		// de jeu reste applique apres avoir quitte le jeu, jusqu'au prochain
		// redemarrage d'Aurora). Cause : cette condition exigeait
		// currentTitleId != 0 pour mettre a jour g_lastTitleId - or
		// XamGetCurrentTitleId() renvoie 0 hors d'un jeu (voir mapping.h),
		// donc le retour au dashboard n'etait JAMAIS detecte : g_lastTitleId
		// restait fige sur le dernier jeu quitte, et ResolveMergedButtonMap /
		// ResolveEffectiveMouseSettings / FindWheelOverride (tous indexes sur
		// g_lastTitleId) continuaient donc a appliquer son profil. Seul un
		// redemarrage complet remettait g_lastTitleId a 0 au chargement du
		// driver, d'ou le symptome observe.
		//
		// La transition VERS 0 (retour dashboard) est desormais bien
		// detectee, mais DEBOUNCEE (voir TITLE_ZERO_DEBOUNCE_MS plus haut) -
		// une lecture 0 isolee en pleine partie ne doit pas etre prise pour un
		// retour au dashboard. Les transitions vers un titleId NON NUL restent
		// immediates comme avant. L'ecriture dans known_titles.txt plus bas
		// reste conditionnee a currentTitleId != 0 pour ne jamais y
		// enregistrer le dashboard lui-meme.
		if (currentTitleId != 0) {
			g_titleZeroCandidateSince = 0; // annule un debounce "retour dashboard" en cours
			if (currentTitleId != g_lastTitleId) {
				FileLog("Title changed: %08x -> %08x (no rebind action taken)", g_lastTitleId, currentTitleId);
			g_directAimOff = false;   // Win+Shift+D lasts one game
				g_lastTitleId = currentTitleId;
				if (currentTitleId != 0xFFFE07D1) LogRunningGameVersion(currentTitleId);   // exact game version, for direct aim
				ClearStudioFlagOnTitleChange();   // AUDIT I1
				g_titleStableSince = GetTickCount();

				// Jalon 7 (suite, 2026-08-02) - historique des jeux vus, pour que
				// l'ecran "Profils" d'application.xex puisse proposer une liste
				// au lieu de faire chercher/taper le Title ID a la main (demande
				// utilisateur). Lecture/ecriture directe (pas de detection de
				// changement externe a faire ici, contrairement au hot-reload de
				// X360Remap.json juste plus bas - on ECRIT, on ne surveille pas)
				// - meme thread deja utilise pour tout I/O disque de ce fichier.
				// AddKnownTitleId renvoie false (donc pas de reecriture) si ce
				// titleId etait deja connu - evite un acces disque a chaque
				// changement de jeu pour un jeu deja vu.
				char titlesPath[128];
					StudioPath(titlesPath, sizeof(titlesPath), "known_titles.txt");
					std::ifstream knownIn(titlesPath, std::ios::binary);
				std::string knownContent((std::istreambuf_iterator<char>(knownIn)), std::istreambuf_iterator<char>());
				knownIn.close();
				std::vector<uint32_t> knownTitles = ParseKnownTitleIds(knownContent);
				if (AddKnownTitleId(&knownTitles, currentTitleId)) {
					std::ofstream knownOut(titlesPath, std::ios::binary | std::ios::trunc);
					if (knownOut.is_open()) {
						std::string serialized = SerializeKnownTitleIds(knownTitles);
						knownOut.write(serialized.data(), serialized.size());
					} else {
						FileLog("Failed to write known_titles.txt for %08x", currentTitleId);
					}
				}
			}
		} else if (g_lastTitleId != 0) {
			// currentTitleId == 0 et on croit encore etre en jeu : candidat au
			// retour dashboard, mais pas encore commite.
			if (g_titleZeroCandidateSince == 0) {
				g_titleZeroCandidateSince = GetTickCount();
			} else if (GetTickCount() - g_titleZeroCandidateSince > TITLE_ZERO_DEBOUNCE_MS) {
				FileLog("Title changed: %08x -> 00000000 (back to dashboard - game profiles off)",
					g_lastTitleId);
				g_lastTitleId = 0;
				g_titleStableSince = GetTickCount();
				g_titleZeroCandidateSince = 0;
				g_dashSinceLoad = true;
				ClearStudioFlagOnTitleChange();   // AUDIT I1
			}
		}

#if DEFERRED_USB_RESET
		// --- Deferred USB re-enumeration (2026-08-01) ---
		//
		// Goal: pick up devices that were already plugged in at power-on, WITHOUT
		// the boot freeze. Doing the reset inside DllMain froze the console every
		// time (see "révision 4"), but that is the only context it was ever tried
		// in - very early, while the system is still coming up. Here it runs from
		// the polling thread instead, once the dashboard has settled, in the same
		// safe context already used for notifications and disk writes.
		//
		// Trigger is state-based rather than a fixed delay: the current title must
		// have stayed unchanged for a while, which means the dashboard has finished
		// loading and is idle. Fires at most once per boot.
		//
		// Also worth noting: if this does freeze, it freezes HERE, with the log
		// thread alive - so unlike the boot freeze, we will actually see how far
		// it got.
		//
		// Known risk (observed in earlier July testing): a full USB reset can make
		// the wireless controller undetectable until it is re-synced. Set
		// DEFERRED_USB_RESET to 0 to disable this entirely.
		//
		// CORRECTIF 2026-08-08 (signale par l'utilisateur : "la souris ne se
		// lance pas au demarrage d'Aurora, j'ai du la debrancher/rebrancher,
		// et la notification de detection n'apparait qu'au lancement du
		// jeu") : la garde `g_lastTitleId != 0` ci-dessous empechait ce reset
		// de se declencher tant qu'aucun jeu n'avait ete lance, alors que le
		// commentaire ci-dessus dit explicitement vouloir le declencher "une
		// fois le dashboard stabilise" - Aurora elle-meme tourne avec
		// titleId 0 (XamGetCurrentTitleId() hors d'un jeu, voir mapping.h),
		// cette garde empechait donc precisement le cas qu'elle pretendait
		// couvrir. Retiree : seule la stabilite du titre compte desormais
		// (0 ou non), voir aussi l'initialisation de g_titleStableSince plus
		// haut, necessaire pour que cette stabilite soit mesurable des le
		// tout premier poll meme si le titre de depart est 0.
		MarkPollPhase(3);
		if (!g_deferredResetDone &&
			(GetTickCount() - g_titleStableSince) > 10000 &&
			(GetTickCount() - g_bootBlackoutStartedAt) > 20000 &&
			!g_bootUsbResetInProgress && (g_blackoutEndedAt == 0 || (GetTickCount() - g_blackoutEndedAt) > 3000)) {

			g_deferredResetDone = true;
			g_bootUsbResetInProgress = false;   // devices may be claimed from now on

			// Test switch (2026-10-26, GitHub issue #5: Wi-Fi networks not found
			// with the plugin loaded). The internal Wi-Fi module of the Slim / E
			// is a USB device, so a full USB reset may leave the network stack
			// without its adapter. disable_usb_reset.txt skips the reset. Read
			// HERE, not in LoadFreezeTestSettings: this runs >20 s after boot in
			// the polling thread, so the hard drive is surely mounted.
			bool skipUsbReset = false;
			{
				char rp[128];
				StudioPath(rp, sizeof(rp), "disable_usb_reset.txt");
				std::ifstream rin(rp, std::ios::binary);
				if (rin.is_open()) { rin.close(); skipUsbReset = true; }
			}

			if (skipUsbReset) {
				FileLog("Deferred USB reset SKIPPED (disable_usb_reset.txt) - devices plugged at power-on may need a replug");
				FlushLogBuffer();
			} else {
				FileLog("Deferred USB reset: title %08x stable, re-enumerating USB now", g_lastTitleId);
				FlushLogBuffer();   // get this on disk BEFORE the risky call, not after

				UsbdPowerDownNotification();
				MmFreePhysicalMemory(0, *(DWORD*)UsbPhysicalPage);
				UsbdDriverEntry();

				FileLog("Deferred USB reset done - devices connected at power-on should appear now");
			}
		}
#else
		// USB boot report (read only, see RunGentleUsbRescan): once, with the
		// dashboard back from the boot title for 10 s and the claim blackout over.
		// shortcut: no 60 s fallback if the boot title never switches back; replug then.
		if (!g_usbRescanDone && g_dashSinceLoad && g_lastTitleId == 0 &&
			(GetTickCount() - g_titleStableSince) > 10000 &&
			!g_bootUsbResetInProgress && g_blackoutEndedAt != 0 &&
			(GetTickCount() - g_blackoutEndedAt) > 3000) {
			g_usbRescanDone = true;
			RunGentleUsbRescan();
		}
		// Listed ports, once per boot, after the boot report: one every 3 s.
		if (g_usbRescanDone && g_portResetCount != 0) {
			if (g_portResetCount < 0) { ReadPortResetList(); g_portResetAt = GetTickCount(); }
			else if (g_portResetNext < g_portResetCount && GetTickCount() - g_portResetAt > 3000) {
				g_portResetAt = GetTickCount();
				ResetNextListedPort();
			}
		}
		if (g_usbRescanDone) {
			static DWORD s_askCheck = 0;
			bool asked = false;
			if (GetTickCount() - s_askCheck > 2000) {
				s_askCheck = GetTickCount();
				char ap[128];
				StudioPath(ap, sizeof(ap), "usb_report.txt");
				if (GetFileAttributesA(ap) != INVALID_FILE_ATTRIBUTES) { DeleteFileA(ap); asked = true; }
			}
			if (asked || (g_usbReportWanted && GetTickCount() - g_usbReportWantedAt > 2000)) {
				g_usbReportWanted = false;
				FileLog("USB report again (%s)", asked ? "usb_report.txt" : "a device arrived");
				RunGentleUsbRescan();
			}
		}
#endif

		// Guide button requested from the input hook (see g_guideRequest).
		{
			const LONG g = InterlockedExchange(&g_guideRequest, 0);
			if (g > 0)
				XamInputSendXenonButtonPress((DWORD)(g - 1));
		}

		// Working folder not found at boot (2026-10-29, see ResolveStudioDrive):
		// retry every 2 s; after 60 s, one FINAL pass that may create the folder
		// on internal memory / USB, then give up. Polling thread = safe context.
		{
			static DWORD lastStorageTryMs = 0;
			static bool storageGaveUp = false;
			if (!g_studioDriveResolved && !storageGaveUp && g_bootBlackoutStartedAt != 0 &&
				(GetTickCount() - lastStorageTryMs) >= 2000) {
				lastStorageTryMs = GetTickCount();
				const bool finalPass = (GetTickCount() - g_bootBlackoutStartedAt) > 60000;
				if (ResolveStudioDrive(finalPass ? STORAGE_PASS_FINAL : STORAGE_PASS_RETRY)) {
					FileLog("Working folder found after boot: %s\\X360RemapStudio (%u ms)%hs",
						g_studioDrive, (unsigned int)(GetTickCount() - g_bootBlackoutStartedAt),
						g_studioDriveIsOwnLink ? " - own link: live Studio files (input_state, status) NOT written" : "");
					// Before the blackout ends, its own re-read below loads
					// everything. After it, load the settings here.
					if (!g_bootUsbResetInProgress) {
						LoadFreezeTestSettings();
						LoadDbgPrintMirrorSetting();
						LoadOverlaySetting();
						LoadBootProtocolSetting();
#if EN_CAMERA_LEARN
						LoadLearnedCameras();
#endif
						if (LoadMappingsFromStudioConfig()) {
							RebindControllerMaps();
							g_installIncomplete = false;
							g_configUnreadable = false;
							FileLog("X360Remap.json loaded from the late working folder");
						}
					}
					FlushLogBuffer();
				} else if (finalPass) {
					storageGaveUp = true;
					FileLog("No writable storage found after 60 s (tried HDD:, Hdd1:, X360R:, OnBoardMU:, INTUSB:, Usb0:, USB sticks Mass0-2) - default layout, no log on disk");
				}
			}
		}

		// Close the boot blackout window once enough time has passed since the
		// USB reset for its asynchronous enumeration to be over - see the
		// g_bootUsbResetInProgress comment for why this is time-based and not
		// tied to the UsbdDriverEntry() call returning.
		if (g_bootUsbResetInProgress && g_bootBlackoutStartedAt != 0 &&
			(GetTickCount() - g_bootBlackoutStartedAt) > BOOT_CLAIM_BLACKOUT_MS &&
			((g_lastTitleId == 0 && (GetTickCount() - g_titleStableSince) > BOOT_SETTLE_TITLE_MS) ||
			 (GetTickCount() - g_bootBlackoutStartedAt) > BOOT_SETTLE_MAX_MS)) {
			g_bootUsbResetInProgress = false;
			g_blackoutEndedAt = GetTickCount();
			FileLog("Boot blackout over - devices can be claimed from now on (replug anything connected at boot)");
			FlushLogBuffer();   // 2026-11-12: every frozen boot stops within a second of this block;
			                    // each step reaches the disk before the next, so a frozen log names it

			// Read AGAIN now that the storage is surely mounted (2026-10-08): at
			// DllMain the hard drive is often not ready yet ("boot read failed:
			// storage not ready yet"), so the enable_*.txt switches and
			// camera_addresses.txt were silently ignored - which is why the
			// Metro switches "changed nothing".
			LoadFreezeTestSettings(); FileLog("Blackout end step 1 done"); FlushLogBuffer();
			// Same for the DbgPrint mirror (2026-10-23): read only at DllMain, it
			// was ALWAYS off - the live capture with XboxFreezeLogger received
			// nothing at all.
			LoadDbgPrintMirrorSetting(); FileLog("Blackout end step 2 done"); FlushLogBuffer();
			LoadOverlaySetting(); FileLog("Blackout end step 3 done"); FlushLogBuffer();
			// Same for force_boot_protocol.txt (2026-10-29, HB's Rapoo log: "No
			// writable storage found" at DllMain, so the file was never seen).
			// Safe here: devices are only claimed after this blackout ends.
			LoadBootProtocolSetting(); FileLog("Blackout end step 4 done"); FlushLogBuffer();
#if EN_CAMERA_LEARN
			LoadLearnedCameras(); FileLog("Blackout end step 5 done"); FlushLogBuffer();
#endif

			// Meme instant que la fin du blackout : le dashboard est affiche
			// depuis un moment, la notification sera donc reellement visible.
			// Une seule fois par demarrage (le drapeau est remis a false).
			//
			// VERIFICATION A NOUVEAU juste avant d'afficher (2026-09-25) :
			// signale par HB, le message "installation incomplete" revenait a
			// CHAQUE demarrage alors que la Studio etait installee et configuree.
			// Le verdict etait pris dans DllMain, tres tot au boot : une lecture
			// qui echoue a ce moment-la (support pas encore pret) suffisait a
			// declencher le message, meme si le fichier est bien la. Ici, le
			// tableau de bord est affiche depuis un moment : si le fichier se lit
			// maintenant, on charge les reglages et on ne dit rien.
			// Contexte sur : thread de polling (meme appel que le rechargement a
			// chaud plus bas).
			if (g_installIncomplete || g_configUnreadable) {
				if (LoadMappingsFromStudioConfig()) {
					RebindControllerMaps();
					FileLog("X360Remap.json readable after boot - settings loaded, no notification (boot read failed: storage not ready yet)");
					g_installIncomplete = false;
					g_configUnreadable = false;
				} else {
					char cfgNow[128];
					StudioPath(cfgNow, sizeof(cfgNow), "X360Remap.json");
					const bool presentNow = GetFileAttributesA(cfgNow) != INVALID_FILE_ATTRIBUTES;
					g_installIncomplete = !presentNow;
					g_configUnreadable  = presentNow;
				}
			}
			if (g_installIncomplete) {
				g_installIncomplete = false;
				FileLog("Notification: incomplete install (X360Remap.json missing - X360RemapStudio never installed/launched, or on storage the plugin does not see)");
				FlushLogBuffer();   // sur le disque AVANT XNotifyUI, meme precaution qu'ailleurs
				// Passage par un tampon local comme TOUS les autres appels a
				// XNotifyUI de ce fichier : le prototype attend un PWCHAR
				// modifiable, WTr() renvoie un const wchar_t*.
				wchar_t msg[192];
				swprintf(msg, 192, L"%s", WTr(STR_INSTALL_INCOMPLETE));
				XNotifyUI(XNOTIFYUI_CUSTOM, msg);
			} else if (g_configUnreadable) {
				// Fichier bien present : l'application est donc installee, le
				// probleme est ailleurs. On reutilise la chaine existante du
				// rechargement a chaud plutot que d'en inventer une deuxieme.
				g_configUnreadable = false;
				FileLog("Notification: X360Remap.json present but unreadable (syntax)");
				FlushLogBuffer();
				wchar_t msg[192];
				swprintf(msg, 192, L"%s", WTr(STR_JSON_INVALID));
				XNotifyUI(XNOTIFYUI_CUSTOM, msg);
			}
		}

		// --- Hot reload of X360Remap.json ---
		// Without this, editing a mapping would require a full console reboot,
		// which makes any configuration tool (on-console or otherwise) painful.
		// Polled here rather than triggered from HidAddDeviceHook because
		// reloading touches the disk, which is only safe from this thread.
		//
		// Pointer hazard: FindMapping() returns a pointer INTO g_dynamicMappings,
		// and reloading clears and refills that vector - so every Controller::map
		// currently held would dangle. They are therefore cleared BEFORE the
		// reload and re-resolved after. During that short window a device falls
		// back to its built-in defaults, which is harmless.
		if ((GetTickCount() - g_lastJsonCheck) > 2000) {
			g_lastJsonCheck = GetTickCount();

#if EN_FIX_INPUT_STATE_ONLY_WITH_STUDIO
			// Presence de la Studio (AUDIT I1) - simple lecture des attributs,
			// aucune ecriture.
			{
				char flagPath[128];
				StudioFlagPath(flagPath, sizeof(flagPath));
				bool present = (GetFileAttributesA(flagPath) != INVALID_FILE_ATTRIBUTES);
				if (present && !g_studioFlagEverSeen)
					FileLog("studio_active.flag seen - input_state.json is now only written while the Studio runs");
				if (present)
					g_studioFlagEverSeen = true;
#if EN_CAMERA_LEARN
				if (!present && g_studioActive) LoadLearnedCameras();   // a camera deleted in the Studio
#endif
				// Settings saved in the Studio apply when it closes. Polling thread: file read is safe here.
				if (!present && g_studioActive && g_overlayProbe) {
					LoadOverlayLook();
					FileLog("Overlay settings reloaded after the Studio: enabled=%d keys=%d info=%d secs=%u hotkey=%02x",
						(int)g_hudEnabled, (int)g_hudShowKeys, (int)g_hudShowInfo, (unsigned int)g_hudInfoSecs, (unsigned int)g_hudHotkey);
				}
				g_studioActive = present;
				if (present) SaveBuiltInPadMappings();
			}
#endif

			// X360Remap_status.json (see WriteStatusFile): every 2 s, only
			// while a PC is linked. Not for the Studio: with this file written
			// while it ran, the console froze within seconds, 4 times out of 4
			// (2026-10-30), even with the Studio not reading it. Cause unknown.
			{
				static DWORD s_lastStatus = 0;
				static bool s_loggedLink = false;
				if (GetTickCount() - s_lastStatus >= 2000) {
					s_lastStatus = GetTickCount();
					const bool pc = PcLinkActive();
					if (pc != s_loggedLink) {
						s_loggedLink = pc;
						FileLog(pc ? "PC link: pc_link.flag is being refreshed - status file on" : "PC link: gone");
					}
					if (pc) {
						__try { WriteStatusFile(); }
						__except (EXCEPTION_EXECUTE_HANDLER) {
							FileLog("EXCEPTION WriteStatusFile: code=%08x - ignored", (unsigned int)GetExceptionCode());
						}
					}
					// In-game display settings apply about 2 s after they are saved (console Studio)
					// or sent (Windows app): the file is watched like X360Remap.json.
					static FILETIME s_ovTime = { 0, 0 };
					static DWORD s_ovSize = 0;
					static bool s_ovKnown = false;
					FILETIME ovTime; DWORD ovSize = 0;
					if (g_overlayProbe && GetStudioFileStamp("overlay_settings.txt", &ovTime, &ovSize)) {
						const bool ovChanged = s_ovKnown && (ovTime.dwLowDateTime != s_ovTime.dwLowDateTime ||
							ovTime.dwHighDateTime != s_ovTime.dwHighDateTime || ovSize != s_ovSize);
						s_ovTime = ovTime; s_ovSize = ovSize; s_ovKnown = true;
						if (ovChanged) {
							LoadOverlayLook();
							FileLog("Overlay settings changed - reloaded: enabled=%d keys=%d pad=%d stats=%d hotkey=%02x draw_mode=%u",
								(int)g_hudEnabled, (int)g_hudShowKeys, (int)g_hudShowPad, (int)g_hudShowStats, (unsigned int)g_hudHotkey,
								(unsigned int)g_hudDrawMode);
						}
					}
				}
			}

			MarkPollPhase(4);
			FILETIME ft; DWORD sz = 0;
			if (!GetJsonFileStamp(&ft, &sz)) {
				// Logged once, not every 2s, so a missing file does not flood.
				if (!g_jsonStateKnown) {
					g_jsonStateKnown = true;
					FileLog("Hot reload: cannot stamp %s\\X360RemapStudio\\X360Remap.json - watching disabled", g_studioDrive);
				}
			} else {
				bool changed = (ft.dwLowDateTime != g_lastJsonWriteTime.dwLowDateTime) ||
				               (ft.dwHighDateTime != g_lastJsonWriteTime.dwHighDateTime) ||
				               (sz != g_lastJsonSize);
				bool firstCheck = !g_jsonStateKnown;

				g_lastJsonWriteTime = ft;
				g_lastJsonSize = sz;

				if (firstCheck) {
					g_jsonStateKnown = true;
					FileLog("Hot reload armed - watching X360Remap.json (size %d)", sz);
				} else if (changed) {
					// Meme protocole detacher/recharger/re-resoudre que les
					// chemins F9 et liaison rapide (voir DetachControllerMaps) -
					// ce chemin-ci etait le seul a le faire correctement, il est
					// desormais factorise pour que les trois soient identiques.
					DetachControllerMaps();

					// Notification SILENCIEUSE pendant le demarrage (2026-08-24,
					// signale par HB : "notification au demarrage X360Remap.json
					// recharge inutile"). Un changement detecte dans les toutes
					// premieres secondes ne vient jamais d'une action de
					// l'utilisateur - il n'a rien eu le temps de modifier quoi que
					// ce soit. C'est un artefact de la premiere comparaison
					// d'horodatage, et afficher "profil recharge" a chaque allumage
					// n'ajoute que du bruit. Le rechargement lui-meme se fait
					// normalement et la ligne de journal est conservee : seule la
					// notification a l'ecran disparait.
					const bool pendantDemarrage =
						(GetTickCount() - g_bootBlackoutStartedAt) < 15000;

					if (LoadMappingsFromStudioConfig()) {
						g_jsonReloadFailures = 0;
						g_installIncomplete = false;
						g_configUnreadable = false;
						RebindControllerMaps();
						FileLog("X360Remap.json changed - mappings reloaded, no reboot needed%hs",
							pendantDemarrage ? " (notification hidden: boot)" : "");
						if (!pendantDemarrage)
							XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_JSON_RELOADED));
					} else {
						// Les mappings precedents restent en memoire (un echec de
						// lecture ne vide rien) : on les re-attache.
						RebindControllerMaps();
						g_jsonReloadFailures++;
						FileLog("X360Remap.json changed but failed to load (syntax error, or read during a write) - previous mappings kept (failure %d)",
							g_jsonReloadFailures);
						if (g_jsonReloadFailures >= 2) {
							XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_JSON_INVALID));
						} else {
							// Premier echec : probablement une lecture tombee
							// pendant une ecriture. On oublie l'horodatage pour
							// forcer une nouvelle tentative au prochain passage
							// (2 s), au lieu d'attendre une AUTRE modification.
							g_lastJsonSize = 0xFFFFFFFF;
						}
					}
				}
			}
		}

		// Watchdog for g_deviceInitBusy (see its declaration comment) - a stuck
		// device init should never lock out every future device forever.
		if (g_deviceInitBusy && (GetTickCount() - g_deviceInitBusySince) > 3000) {
			FileLog("Watchdog: device-init guard stuck for >3s, force-clearing it");
			g_deviceInitBusy = false;
		}

		// Live input snapshot for the config assistant (application.xex) -
		// see Controller::lastKeyCode/lastMouseButtonIdx and ARCHITECTURE.md
		// for why this exists (a second .xex can't safely read the USB
		// device itself). Written every iteration of this loop, same cadence
		// already proven safe on hardware for FlushLogBuffer() just below.
		// Only bothers writing when a boot-protocol device is actually
		// connected, so nothing is written (and no disk access happens) when
		// no keyboard/mouse is plugged in.
		MarkPollPhase(5);
		{
			int firstKeyboard = -1, firstMouse = -1;
			for (int i = 0; i < 4; i++) {
				// Filtre sur "c'est notre clavier/souris", PLUS sur "c'est un
				// device boot" (2026-08-04). Depuis que les souris basculent
				// sur le chemin descripteur, isBootProtocol passe a false et
				// cette boucle les ignorait : input_state.json ne recevait
				// plus rien pour la souris, donc le curseur de application.xex
				// restait fige et la liaison rapide ne pouvait plus capturer
				// de clic comme source. isMouse/isKeyboard restent vrais dans
				// les deux chemins, c'est le bon critere.
				if (!connectedControllers[i].controllerDriver)
					continue;
				if (connectedControllers[i].isKeyboard && !connectedControllers[i].isExtraKeyboardIf && firstKeyboard == -1)
					firstKeyboard = i;
				if (connectedControllers[i].isMouse && firstMouse == -1)
					firstMouse = i;
			}

			if (firstKeyboard != -1 || firstMouse != -1 || AnyUsbPad()) {
				uint8_t keyRaw = AnyKeyboardKeyCode();
				uint8_t mouseRaw = (firstMouse != -1) ? connectedControllers[firstMouse].lastMouseButtonIdx : 0xFF;
				int keyCode = (keyRaw == 0xFF) ? -1 : (int)keyRaw;
				int mouseButton = (mouseRaw == 0xFF) ? -1 : (int)mouseRaw;

				// Cursor motion + full button mask for application.xex's mouse
				// cursor (2026-08-01, see Controller::cursorAccumX and
				// PROJECT_NOTES.md "Jalon 4"). Drained here (own accumulator,
				// doesn't touch mouseAccumX/Y - those stay reserved for the
				// right-stick emulation in XInputdReadStateHook). mouseButtons
				// is the raw bitmask (bit0=left/bit1=right/bit2=middle...), 0
				// when no mouse is connected (0 is already the correct "no
				// button held" value, unlike keyCode/mouseButton above which
				// need a -1 sentinel since 0 is a real HID code).
				int32_t mouseDX = 0, mouseDY = 0;
				int mouseButtonsMask = 0;
				int32_t wheelDelta = 0;
				if (firstMouse != -1) {
					// Lecture + remise a zero atomiques (voir WriteInputStateSnapshot).
					mouseDX = (int32_t)InterlockedExchange((volatile LONG*)&connectedControllers[firstMouse].cursorAccumX, 0);
					mouseDY = (int32_t)InterlockedExchange((volatile LONG*)&connectedControllers[firstMouse].cursorAccumY, 0);
					mouseButtonsMask = connectedControllers[firstMouse].lastMouseButtonsMask;

					// Molette pour le defilement UI d'application.xex
					// (2026-08-09, voir Controller::uiWheelAccum) - meme
					// schema drain-puis-remise-a-zero que mouseDX/mouseDY
					// juste au-dessus.
					wheelDelta = (int32_t)InterlockedExchange((volatile LONG*)&connectedControllers[firstMouse].uiWheelAccum, 0);
				}

				// Octets bruts du dernier rapport souris, en hexadecimal.
				// Indispensables a l'assistant de calibration : c'est en
				// observant QUELS octets changent pendant un geste demande
				// que l'application deduit la disposition du paquet. Meme
				// canal que le reste de ce fichier - l'application ne peut pas
				// lire l'USB elle-meme (voir ARCHITECTURE.md).
				// g_rawMouseLatest est rempli par le callback USB en simples
				// copies d'octets ; le formatage se fait ICI, dans le thread
				// sur, jamais dans le callback (voir le gel du 2026-08-03).
				char rawHex[40];
				rawHex[0] = '\0';
				{
					int p = 0;
					uint8_t n = g_rawMouseLen;
					if (n > 16) n = 16;
					for (uint8_t i = 0; i < n && p < (int)sizeof(rawHex) - 3; i++) {
						int w = _snprintf(rawHex + p, sizeof(rawHex) - p - 1, "%02x", (unsigned int)g_rawMouseLatest[i]);
						if (w <= 0) break;
						p += w;
					}
					rawHex[sizeof(rawHex) - 1] = '\0';
				}

				char devs[80];
				FormatConnectedDevices(devs, sizeof(devs));
				char json[512];
				int len = _snprintf(json, sizeof(json),
					"{ \"keyCode\": %d, \"mouseButton\": %d, \"mouseDX\": %ld, \"mouseDY\": %ld, \"mouseButtons\": %d, \"wheelDelta\": %ld, \"rawLen\": %d, \"raw\": \"%hs\", \"rawSeq\": %lu, \"tick\": %lu, \"devs\": \"%hs\", \"padBtn\": \"0x%04lx\" }",
					keyCode, mouseButton, (long)mouseDX, (long)mouseDY, mouseButtonsMask, (long)wheelDelta,
					(int)g_rawMouseLen, rawHex, (unsigned long)g_rawMouseSeq, GetTickCount(), devs, (unsigned long)g_padButtonsHeld);
#if EN_FIX_INPUT_STATE_ONLY_WITH_STUDIO
				// AUDIT I1 : ecriture seulement si la Studio est presente (ou si
				// aucune Studio recente ne s'est jamais signalee - repli).
				// Only while the Studio runs (2026-11-12). The old fallback "write until a Studio was
				// seen once" meant 10 disk writes a second on every boot where the Studio was not
				// opened, in games too: measured stalls of 0.4 to 0.8 s in this very step, and the
				// last line before the freeze of 2026-11-12. Every Studio since pre12 creates the flag.
				const bool writeInputState = g_studioActive;
#else
				const bool writeInputState = true;
#endif
				if (len > 0 && writeInputState && !g_studioDriveIsOwnLink) {
					char statePath[128];
					StudioPath(statePath, sizeof(statePath), "input_state.json");
					std::ofstream f(statePath, std::ios::binary | std::ios::trunc);
					if (f.is_open()) {
						f.write(json, len);
					}
				}

				// Raccourci "sauvegarder le profil depuis le jeu" (Jalon 7,
				// suite) - reutilise keyCode/firstMouse deja calcules
				// ci-dessus, pas de nouvelle lecture USB. Voir le commentaire
				// de SAVE_PROFILE_HOTKEY_CODE plus haut pour le choix de F9 et
				// la duree de maintien.
				if (keyCode == (int)SAVE_PROFILE_HOTKEY_CODE) {
					g_saveProfileHoldTicks++;
					if (g_saveProfileHoldTicks >= SAVE_PROFILE_HOLD_TICKS && !g_saveProfileTriggered) {
						g_saveProfileTriggered = true; // requiert un relachement avant de re-declencher

						// Un "profil" n'a de sens que DANS un jeu (titleId !=
						// 0, voir FindActiveProfile) et pour LA souris
						// actuellement connectee (ce sont ses reglages qu'on
						// fige) - sinon on log juste pourquoi rien ne s'est
						// passe, pas de notification qui laisserait croire a
						// une sauvegarde reussie.
						if (g_lastTitleId != 0 && firstMouse != -1) {
							const HidDeviceMapping* mouseMap = connectedControllers[firstMouse].map;
							EffectiveMouseSettings effective = ResolveEffectiveMouseSettings(mouseMap, g_lastTitleId);
							ApplyLiveOverrides(effective, g_lastTitleId);   // reglages en jeu F5..F11 (2026-09-25)
							uint16_t vid = connectedControllers[firstMouse].vendorId;
							uint16_t pid = connectedControllers[firstMouse].productId;

							// effective a deja ete resolu ci-dessus a partir de
							// mouseMap - on peut detacher sans rien perdre.
							// Detacher AVANT toute mutation de g_dynamicMappings,
							// re-resoudre APRES : voir DetachControllerMaps.
							DetachControllerMaps();
							// Lecture VERIFIEE avant ecriture (AUDIT I4) : un fichier
							// present mais illisible n'est plus jamais ecrase.
							bool ok = LoadMappingsForUpdateStudio();
							if (!ok)
								FileLog("F9: X360Remap.json unreadable - nothing written, to avoid overwriting it");
							ok = ok && SetMouseProfile(vid, pid, g_lastTitleId,
								effective.mouseSensitivity, effective.invertMouseY, effective.deadzone,
								effective.sensitivityCurveType, effective.sensitivityCurveExponent,
								nullptr, effective.antiDeadzone, -2, effective.antiDeadzoneY, effective.smoothingMs);
							ok = ok && SaveMappingsToStudioConfig();
							if (ok) RememberOwnJsonWrite();
							RebindControllerMaps();
							if (ok && g_liveTitle == g_lastTitleId) {
								// F9 vient d'enregistrer les reglages en jeu en cours.
								g_liveTitle = 0;
								MemoryBarrier();
								g_liveSens = 0; g_liveDz = -1; g_liveAdz = -1; g_liveAdzY = -1; g_liveSm = -1;
								g_liveDirty = false;
							}

							FileLog(ok ? "Profile %08x saved from the F9 shortcut (sensitivity=%d deadzone=%d invertY=%d)"
									   : "FAILED to save profile %08x from the F9 shortcut",
								(unsigned int)g_lastTitleId, effective.mouseSensitivity, effective.deadzone, effective.invertMouseY ? 1 : 0);
							XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)(ok ? WTr(STR_PROFILE_SAVED) : WTr(STR_PROFILE_SAVE_FAILED)));
						} else {
							FileLog("F9 shortcut ignored (no game running or no mouse connected)");
						}
					}
				} else {
					g_saveProfileHoldTicks = 0;
					g_saveProfileTriggered = false;
				}

#if EN_LIVE_TUNING
				// Visee directe Call of Duty reconnue (voir MouseHookCod) : une
				// notification par jeu, ici et pas dans le hook de lecture.
				if (g_mousehookNotifyPending) {
					g_mousehookNotifyPending = false;
					const int mb = g_mousehookBuild;
					if (mb >= 0) {
						FileLog("Direct aim ready: %hs (applies once gameplay starts)", kCodBuilds[mb].name);
						FlushLogBuffer();
						wchar_t msg[160];
						swprintf(msg, 160, WTr(STR_MOUSEHOOK_ON_FMT), kCodBuilds[mb].name);
						XNotifyUI(XNOTIFYUI_CUSTOM, msg);
					}
				}
				// Win + Shift + X: X360Remap on / off (see g_remapPaused).
				{
					static bool s_comboHeld = false;
					const uint8_t mods = AnyKeyboardModifiers();
					// Win (L/R) + Shift (L/R) + X, or Shift + Alt + X for keypads
					// with no Win key (Tartarus: key 16 + thumb button + key 18).
					const bool combo = ((mods & 0x88) || (mods & 0x44)) && (mods & 0x22) && keyCode == (int)PAUSE_KEY_X;
					if (combo && !s_comboHeld) {
						g_remapPaused = !g_remapPaused;
						FileLog(g_remapPaused ? "Win+Shift+X: X360Remap PAUSED - real controller drives player 1"
						                      : "Win+Shift+X: X360Remap ACTIVE again");
						FlushLogBuffer();
						XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(g_remapPaused ? STR_REMAP_PAUSED : STR_REMAP_ACTIVE));
					}
					s_comboHeld = combo;
				}
				// Win + Shift + D: direct aim off / on; Win + Shift + R: reload the settings.
				{
					static int s_heldKey = 0;
					const uint8_t mods = AnyKeyboardModifiers();
					const bool base = ((mods & 0x88) || (mods & 0x44)) && (mods & 0x22);
					const int key = base && (keyCode == (int)AIM_KEY_D || keyCode == (int)RELOAD_KEY_R) ? keyCode : 0;
					if (key && key != s_heldKey) {
						if (key == (int)AIM_KEY_D) {
							g_directAimOff = !g_directAimOff;
							FileLog("Win+Shift+D: direct aim %hs for %08x", g_directAimOff ? "OFF" : "ON", (unsigned int)g_lastTitleId);
							FlushLogBuffer();
							XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(g_directAimOff ? STR_AIM_OFF : STR_AIM_ON));
						} else {
							// Same path as the X360Remap.json hot reload.
							DetachControllerMaps();
							const bool ok = LoadMappingsFromStudioConfig();
							RebindControllerMaps();   // a failed read keeps the previous mappings: attach them again
							LoadLearnedCameras();
							FileLog("Win+Shift+R: settings reloaded (%hs)", ok ? "ok" : "X360Remap.json invalid");
							FlushLogBuffer();
							XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(ok ? STR_CONFIG_RELOADED : STR_JSON_INVALID));
						}
					}
					s_heldKey = key;
				}
				// Scroll Lock: players merged / separate (see g_playersSeparate),
				// kept in players_merged.txt.
				{
					static bool s_playersKeyHeld = false;
					const bool down = keyCode == (int)PLAYERS_KEY;
					if (down && !s_playersKeyHeld) {
						g_playersSeparate = !g_playersSeparate;
						char pp[128];
						StudioPath(pp, sizeof(pp), "players_merged.txt");
						if (!g_playersSeparate) {
							std::ofstream o(pp, std::ios::binary | std::ios::trunc);
							if (o.is_open()) o << "Players: merged (pad + keyboard / mouse = one player). Delete this file (or press Scroll Lock) to separate them again.\r\n";
						} else {
							DeleteFileA(pp);
						}
						FileLog("Scroll Lock: players %hs", g_playersSeparate ? "SEPARATE" : "MERGED");
						FlushLogBuffer();
						XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(g_playersSeparate ? STR_PLAYERS_SEPARATE : STR_PLAYERS_MERGED));
					}
					s_playersKeyHeld = down;
				}
#if EN_CAMERA_LEARN
				CameraLearnPoll(keyCode);   // v1.2.1: F12 camera search, see CameraLearnRun
#endif
				// --- Reglages en jeu (2026-09-25), voir EN_LIVE_TUNING ---------
				{
					const DWORD liveNow = GetTickCount();
					const bool isLiveKey = keyCode == LIVE_KEY_SENS_DOWN || keyCode == LIVE_KEY_SENS_UP ||
						keyCode == LIVE_KEY_ADZ_DOWN || keyCode == LIVE_KEY_ADZ_UP ||
						keyCode == LIVE_KEY_DZ_DOWN || keyCode == LIVE_KEY_DZ_UP ||
						keyCode == LIVE_KEY_ADZY_DOWN || keyCode == LIVE_KEY_ADZY_UP ||
						keyCode == LIVE_KEY_SM_DOWN || keyCode == LIVE_KEY_SM_UP;
					bool step = false;
					if (isLiveKey) {
						if (keyCode != g_liveHeldKey) { g_liveHeldKey = keyCode; g_liveHeldTicks = 0; step = true; }
						else {
							g_liveHeldTicks++;
							// maintenu : repetition apres ~0,5 s, un pas tous les 2 tours (~0,2 s)
							if (g_liveHeldTicks >= 5 && (g_liveHeldTicks % 2) == 1) step = true;
						}
					} else {
						g_liveHeldKey = -1; g_liveHeldTicks = 0;
					}

					if (step) {
						if (g_lastTitleId == 0 || firstMouse == -1) {
							if (g_liveHeldTicks == 0) {
								FileLog("In-game tuning ignored (no game running or no mouse)");
								wchar_t msg[128];
								swprintf(msg, 128, L"%s", WTr(STR_LIVE_NEED_GAME));
								XNotifyUI(XNOTIFYUI_CUSTOM, msg);
							}
						} else {
							// Jeu different de celui des surcharges en cours : on
							// repart des reglages enregistres de CE jeu.
							if (g_liveTitle != 0 && g_liveTitle != g_lastTitleId) {
								g_liveTitle = 0;
								MemoryBarrier();
								g_liveSens = 0; g_liveDz = -1; g_liveAdz = -1; g_liveAdzY = -1; g_liveSm = -1;
								g_liveDirty = false;
							}
							EffectiveMouseSettings cur = ResolveEffectiveMouseSettings(connectedControllers[firstMouse].map, g_lastTitleId);
							ApplyLiveOverrides(cur, g_lastTitleId);
							int32_t sens = cur.mouseSensitivity > 0 ? cur.mouseSensitivity : 10000;  // = DEFAULT_MOUSE_SENSITIVITY
							int32_t dz = cur.deadzone, adz = cur.antiDeadzone, adzY = cur.antiDeadzoneY, sm = cur.smoothingMs;
							// Vertical "identique a l'horizontal" : il le reste tant qu'on ne
							// touche pas F3/F4 (approximation : valeurs egales = suit l'horizontal).
							const bool yFollows = (g_liveAdzY < 0) && (adzY == adz) &&
								keyCode != LIVE_KEY_ADZY_DOWN && keyCode != LIVE_KEY_ADZY_UP;
							if (keyCode == LIVE_KEY_SENS_DOWN) { sens -= 500; g_liveLastKind = 0; }
							if (keyCode == LIVE_KEY_SENS_UP)   { sens += 500; g_liveLastKind = 0; }
							if (keyCode == LIVE_KEY_ADZ_DOWN)  { adz -= 500;  g_liveLastKind = 2; }
							if (keyCode == LIVE_KEY_ADZ_UP)    { adz += 500;  g_liveLastKind = 2; }
							if (keyCode == LIVE_KEY_DZ_DOWN)   { dz -= 250;   g_liveLastKind = 1; }
							if (keyCode == LIVE_KEY_DZ_UP)     { dz += 250;   g_liveLastKind = 1; }
							if (keyCode == LIVE_KEY_ADZY_DOWN) { adzY -= 500; g_liveLastKind = 3; }
							if (keyCode == LIVE_KEY_ADZY_UP)   { adzY += 500; g_liveLastKind = 3; }
							if (keyCode == LIVE_KEY_SM_DOWN)   { sm -= 5;     g_liveLastKind = 4; }
							if (keyCode == LIVE_KEY_SM_UP)     { sm += 5;     g_liveLastKind = 4; }
							if (adzY < 0) adzY = 0;         if (adzY > 12000) adzY = 12000;
							if (sm < 0) sm = 0;             if (sm > 100) sm = 100;
							if (sens < 1000) sens = 1000;   if (sens > 30000) sens = 30000;
							if (adz < 0) adz = 0;           if (adz > 12000) adz = 12000;
							if (dz < 0) dz = 0;             if (dz > 10000) dz = 10000;
							g_liveSens = sens; g_liveDz = dz; g_liveAdz = adz; g_liveAdzY = yFollows ? -1 : adzY; g_liveSm = sm;
							MemoryBarrier();
							g_liveTitle = g_lastTitleId;     // en dernier : active les surcharges
							g_liveVid = connectedControllers[firstMouse].vendorId;
							g_livePid = connectedControllers[firstMouse].productId;
							g_liveDirty = true;
							g_liveNotifyPending = true;
							g_liveLastChangeMs = liveNow;
						}
					}

					// Une seule notification, quand on arrete d'appuyer.
					if (g_liveNotifyPending && !isLiveKey && (liveNow - g_liveLastChangeMs) >= LIVE_NOTIFY_AFTER_MS) {
						g_liveNotifyPending = false;
						const int vals[5] = { (int)g_liveSens, (int)g_liveDz, (int)g_liveAdz, (int)g_liveAdzY, (int)g_liveSm };
						const NotifyStrId ids[5] = { STR_LIVE_SENS_FMT, STR_LIVE_DZ_FMT, STR_LIVE_ADZ_FMT, STR_LIVE_ADZY_FMT, STR_LIVE_SMOOTH_FMT };
						const int kind = (g_liveLastKind >= 0 && g_liveLastKind < 5) ? g_liveLastKind : 0;
						const int v = vals[kind];
						const NotifyStrId sid = ids[kind];
						FileLog("In-game tuning %08x: sens=%d dz=%d adz=%d adzY=%d smoothing=%d", (unsigned int)g_liveTitle,
							(int)g_liveSens, (int)g_liveDz, (int)g_liveAdz, (int)g_liveAdzY, (int)g_liveSm);
						FlushLogBuffer();   // disque AVANT XNotifyUI, meme precaution qu'ailleurs
						wchar_t msg[128];
						swprintf(msg, 128, WTr(sid), v);
						XNotifyUI(XNOTIFYUI_CUSTOM, msg);
					}

					// Enregistrement dans le profil du jeu, meme chemin que F9.
					if (g_liveDirty && !isLiveKey && (liveNow - g_liveLastChangeMs) >= LIVE_SAVE_AFTER_MS) {
						g_liveDirty = false;
						const uint32_t tid = g_liveTitle;
						const int idx = FindConnectedMouseIndex(g_liveVid, g_livePid);
						if (tid != 0 && idx != -1) {
							// Base = reglages ENREGISTRES du jeu (sans surcharge) pour
							// conserver inversion et courbe, puis nos trois valeurs.
							EffectiveMouseSettings base = ResolveEffectiveMouseSettings(connectedControllers[idx].map, tid);
							const int32_t s2 = g_liveSens, dz2 = g_liveDz, adz2 = g_liveAdz, adzY2 = g_liveAdzY, sm2 = g_liveSm;
							DetachControllerMaps();
							bool ok = LoadMappingsForUpdateStudio();
							if (!ok)
								FileLog("In-game tuning: X360Remap.json unreadable - nothing written");
							ok = ok && SetMouseProfile(g_liveVid, g_livePid, tid,
								s2, base.invertMouseY, dz2,
								base.sensitivityCurveType, base.sensitivityCurveExponent,
								nullptr, adz2, -2, adzY2, sm2);
							ok = ok && SaveMappingsToStudioConfig();
							if (ok) RememberOwnJsonWrite();
							RebindControllerMaps();
							FileLog(ok ? "In-game tuning saved to profile %08x (sens=%d dz=%d adz=%d)"
									   : "FAILED to save in-game tuning, profile %08x (sens=%d dz=%d adz=%d)",
								(unsigned int)tid, (int)s2, (int)dz2, (int)adz2);
							if (ok) {
								// Le profil contient maintenant ces valeurs : plus besoin de surcharge.
								g_liveTitle = 0;
								MemoryBarrier();
								g_liveSens = 0; g_liveDz = -1; g_liveAdz = -1; g_liveAdzY = -1; g_liveSm = -1;
							} else {
								// Les surcharges restent actives pour la session ; on n'insiste pas.
								XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_PROFILE_SAVE_FAILED));
							}
						}
					}
				}
#endif
				
				// Jalon 7 (suite, 2026-08-02) - "liaison rapide" in-game
				// (notion 2, voir le commentaire de QuickBindState plus haut).
				// Reutilise keyCode/mouseButton/firstKeyboard/firstMouse deja
				// calcules ci-dessus, pas de nouvelle lecture USB.
				MarkPollPhase(6);
				{
					XINPUT_GAMEPAD real;
					uint8_t realUserIndex = 0xFF;
					bool hasReal = ReadRealControllerState(&real, &realUserIndex);
					if (hasReal && (GetTickCount() - g_realPadTick) > 150) {   // the game is not reading it: keep the cache fresh
						g_realPad = real;
						MemoryBarrier();
						g_realPadTick = GetTickCount();
					}
					// Diagnostic (2026-10-06, HB: "BACK + START does nothing"): one line
					// each time the real controller appears / disappears for the
					// quick bind, with its slot - tells at once whether the plugin
					// sees the controller at all.
					{
						static int s_lastRealSlot = -2;
						const int slot = hasReal ? (int)realUserIndex : -1;
						if (slot != s_lastRealSlot) {
							s_lastRealSlot = slot;
							char ours[32]; int p = 0; ours[0] = '\0';
							for (int k = 0; k < 4 && p < (int)sizeof(ours) - 4; k++)
								if (connectedControllers[k].controllerDriver)
									p += _snprintf(ours + p, sizeof(ours) - p - 1, "%d ", (int)connectedControllers[k].userIndex);
							ours[sizeof(ours) - 1] = '\0';
							if (hasReal) FileLog("Quick bind: real controller seen on slot %d (keyboard/mouse slots: %hs)", slot, ours);
							else         FileLog("Quick bind: no real controller seen (keyboard/mouse slots: %hs)", ours);
						}
					}
					
					// Annule silencieusement une etape laissee en plan (utilisateur qui
					// arme le mode puis abandonne, manette reelle debranchee en cours de
					// route...) - sans ca, un geste effectue bien plus tard pourrait
					// etre pris a tort pour la suite d'un mode arme depuis longtemps.
					if (g_quickBindState != QUICKBIND_IDLE &&
						(!hasReal || GetTickCount() > g_quickBindDeadlineMs)) {
						FileLog(hasReal ? "Quick bind canceled (timed out)"
									: "Quick bind canceled (real controller disconnected)");
						XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_QUICKBIND_CANCELED));
						g_quickBindState = QUICKBIND_IDLE;
						g_quickBindTargetIdx = -1;
					}
					
					if (hasReal) {
						if (g_quickBindState == QUICKBIND_IDLE) {
							bool comboHeld = (real.wButtons & XINPUT_GAMEPAD_BACK) &&
									(real.wButtons & XINPUT_GAMEPAD_START);
							if (comboHeld) {
								if (g_quickBindArmHoldTicks == 0)
									FileLog("Quick bind: BACK + START held on slot %d (hold ~1.5 s)", (int)realUserIndex);
								g_quickBindArmHoldTicks++;
								if (g_quickBindArmHoldTicks >= QUICKBIND_ARM_HOLD_TICKS && !g_quickBindArmTriggered) {
									g_quickBindArmTriggered = true; // requiert un relachement avant de re-declencher
									if (g_lastTitleId != 0) {
										g_quickBindState = QUICKBIND_ARMED;
										g_quickBindPrevRealState = real;
										g_quickBindDeadlineMs = GetTickCount() + QUICKBIND_TIMEOUT_MS;
										FileLog("Quick bind armed (titleId=%08x) - waiting for the target button",
											(unsigned int)g_lastTitleId);
										XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_QUICKBIND_ARMED));
									} else {
										FileLog("Quick bind ignored (no game running)");
										// 2026-10-06: say it on screen too - silently doing nothing looked like a bug.
										XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_LIVE_NEED_GAME));
									}
								}
							} else {
								g_quickBindArmHoldTicks = 0;
								g_quickBindArmTriggered = false;
							}
						} else if (g_quickBindState == QUICKBIND_ARMED) {
							int idx = FindNewlyPressedRealButton(&g_quickBindPrevRealState, &real);
							g_quickBindPrevRealState = real;
							if (idx != -1) {
								g_quickBindTargetIdx = idx;
								g_quickBindState = QUICKBIND_WAITING_SOURCE;
								g_quickBindDeadlineMs = GetTickCount() + QUICKBIND_TIMEOUT_MS;
								wchar_t msg[128];
								swprintf(msg, 128, WTr(STR_QUICKBIND_TARGET_FMT),
									kRealButtonTargets[idx].label);
								XNotifyUI(XNOTIFYUI_CUSTOM, msg);
								FileLog("Quick bind: target captured (index %d)", idx);
							}
						}
					}
					
					// Capture de la SOURCE (touche clavier ou clic souris) - front
					// montant, independant de hasReal (la vraie manette n'a plus
					// besoin d'etre lue une fois la cible capturee).
					static int s_quickBindLastKeyCode = -1;
					static int s_quickBindLastMouseButton = -1;
					bool keyEdge = (keyCode != -1 && keyCode != s_quickBindLastKeyCode);
					bool mouseEdge = (mouseButton != -1 && mouseButton != s_quickBindLastMouseButton);
					s_quickBindLastKeyCode = keyCode;
					s_quickBindLastMouseButton = mouseButton;
					
					// Molette comme SOURCE (2026-08-04). Elle n'etait pas geree :
					// l'utilisateur armait la liaison, choisissait une cible,
					// tournait la molette, et la liaison expirait faute de
					// source reconnue. On lit et on consomme le sens capture
					// depuis le contexte USB (voir Controller::bindWheelDir) -
					// mouseWheelAccum ne convient pas, il est draine par le
					// thread du jeu et n'arrive jamais jusqu'ici.
					int wheelDir = 0;
					if (firstMouse != -1) {
						wheelDir = connectedControllers[firstMouse].bindWheelDir;
						connectedControllers[firstMouse].bindWheelDir = 0;
					}
					bool wheelEdge = (wheelDir != 0);

					if (g_quickBindState == QUICKBIND_WAITING_SOURCE && (keyEdge || mouseEdge || wheelEdge) && g_lastTitleId != 0) {
						const RealButtonTarget& target = kRealButtonTargets[g_quickBindTargetIdx];
						bool ok = false;
						
						// Detacher AVANT toute mutation de g_dynamicMappings
						// (LoadMappingsFromFile vide le vector, les Set*Override
						// peuvent le reallouer), re-resoudre APRES - sans ca les
						// pointeurs Controller::map restaient pendants et le
						// thread du jeu lisait de la memoire liberee juste apres
						// chaque liaison rapide. Voir DetachControllerMaps.
						DetachControllerMaps();
						// Lecture VERIFIEE avant ecriture (AUDIT I4).
						const bool loadedForUpdate = LoadMappingsForUpdateStudio();
						if (!loadedForUpdate) {
							FileLog("Quick bind: X360Remap.json unreadable - nothing written, to avoid overwriting it");
						} else if (keyEdge && firstKeyboard != -1) {
							uint16_t vid = connectedControllers[firstKeyboard].vendorId;
							uint16_t pid = connectedControllers[firstKeyboard].productId;
							ok = SetProfileKeyOverride(vid, pid, g_lastTitleId, (uint8_t)keyCode, target.keyAction);
						} else if (mouseEdge && firstMouse != -1) {
							uint16_t vid = connectedControllers[firstMouse].vendorId;
							uint16_t pid = connectedControllers[firstMouse].productId;
							ok = SetProfileButtonOverride(vid, pid, g_lastTitleId, (uint8_t)mouseButton, target.field);
						} else if (wheelEdge && firstMouse != -1) {
							// idx 3 = molette avant, idx 4 = molette arriere -
							// meme convention reservee que l'ecran "Reglages
							// souris" et FindWheelOverride, pour qu'une liaison
							// faite en jeu et une faite dans l'appli aboutissent
							// exactement au meme enregistrement.
							uint16_t vid = connectedControllers[firstMouse].vendorId;
							uint16_t pid = connectedControllers[firstMouse].productId;
							uint8_t wheelIdx = (wheelDir > 0) ? 3 : 4;
							ok = SetProfileButtonOverride(vid, pid, g_lastTitleId, wheelIdx, target.field);
						}
						ok = ok && SaveMappingsToStudioConfig();
						if (ok) RememberOwnJsonWrite();
						RebindControllerMaps();
						
						wchar_t msg[128];
						if (ok) {
							swprintf(msg, 128, WTr(STR_QUICKBIND_BOUND_FMT), target.label);
							FileLog("Quick bind: target index %d bound (titleId=%08x)",
								g_quickBindTargetIdx, (unsigned int)g_lastTitleId);
						} else {
							swprintf(msg, 128, WTr(STR_QUICKBIND_BIND_FAILED_FMT), target.label);
							FileLog("FAILED quick bind (target index %d, titleId=%08x)",
								g_quickBindTargetIdx, (unsigned int)g_lastTitleId);
						}
						XNotifyUI(XNOTIFYUI_CUSTOM, msg);
						
						g_quickBindState = QUICKBIND_IDLE;
						g_quickBindTargetIdx = -1;
					}
				}
			}
		}

		MarkPollPhase(7);

		// Only place the buffered log lines are actually written to disk -
		// see the FileLog/FlushLogBuffer comment above for why.
		FlushLogBuffer();
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		// Ce thread mourait ici silencieusement, emportant le hot-reload avec
		// lui (voir le bloc de commentaire en tete de boucle). Desormais :
		// on logue OU et POURQUOI, on force l'ecriture disque immediatement
		// (le flush de fin de boucle ci-dessus n'a jamais ete atteint), et on
		// CONTINUE - un incident ponctuel ne doit plus figer definitivement la
		// configuration du plugin jusqu'au prochain redemarrage.
		//
		// FlushLogBuffer est appele directement ici plutot que de compter sur
		// le tour suivant : si l'exception se repete a chaque iteration, on
		// veut quand meme la premiere trace sur le disque.
		FileLog("EXCEPTION polling thread: phase=%d code=%08x - caught, thread keeps running",
			(int)g_pollPhase, (unsigned int)GetExceptionCode());
		FlushLogBuffer();
	}

#if EN_FIX_STUDIO_FAST_INPUT && EN_FIX_INPUT_STATE_ONLY_WITH_STUDIO
		// Studio au premier plan : releves a ~60/s (voir
		// WriteInputStateSnapshot). Sinon, attente habituelle de 100 ms.
		if (g_studioActive) {
			for (int k = 0; k < 5; k++) {
				Sleep(17);
				__try {
					WriteInputStateSnapshot();
				}
				__except (EXCEPTION_EXECUTE_HANDLER) {
					FileLog("EXCEPTION Studio fast sample: code=%08x - ignored", (unsigned int)GetExceptionCode());
				}
			}
			Sleep(15);
		} else {
			Sleep(100);
		}
#else
		Sleep(100);
#endif
	}
	return 0;
}

unsigned int __stdcall MappingThreadProc(void* param) {
	XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_UNKNOWN_CONTROLLER));
	Controller* controller = (Controller*)param;
	HID_ReportInfo_t* info = controller->reportInfo;
	uint8_t reportId = controller->reportId;
	int controllerIndex = -1;

	for (int i = 0; i < 4; i++) {
		if (&connectedControllers[i] == controller) {
			controllerIndex = i;
			break;
		}
	}

	if (controllerIndex == -1)
		return -1;

	// Discover available buttons and axes
	uint8_t availableButtons[256] = {};
	uint8_t buttonCount = 0;
	DiscoverAvailableButtons(info, reportId, availableButtons, &buttonCount);

	uint16_t availableAxes[6] = {};
	uint8_t axisCount = 0;
	DiscoverAvailableAxes(info, reportId, availableAxes, &axisCount);

	// Store discovered buttons in mapping state
	g_mappingState.availableButtonCount = buttonCount;
	for (uint8_t i = 0; i < buttonCount; i++) {
		g_mappingState.availableButtons[i] = availableButtons[i];
	}

	std::vector<HidButtonMapEntry> mappedButtons;
	std::vector<HidAxisMapEntry> mappedAxes;
	HidAxisInvertFlags inverts = {0};

	// Invert vertical axes by default
	inverts.invertY = true;   // Left stick vertical
	inverts.invertRZ = true;  // Right stick vertical

	// Check for hat switch and analog triggers
	bool hasHatSwitch = FindHatItem(info, reportId) != nullptr;

	// If there are more than 4 axes (4 for dual analog sticks), the extra ones are analog triggers
	bool hasAnalogTriggers = axisCount > 4;

	// Track which HID usages we've mapped during this session
	uint16_t mappedUsages[6] = {};
	uint8_t mappedCount = 0;

	// Map buttons in predefined Xbox order
	const struct {
		uint8_t field_idx;
		const char* xbox_name;
		uint8_t ButtonsReport::* field;
	} xbox_buttons[] = {
		{0, "A", &ButtonsReport::a_button},
		{1, "B", &ButtonsReport::b_button},
		{2, "X", &ButtonsReport::x_button},
		{3, "Y", &ButtonsReport::y_button},
		{4, "LB", &ButtonsReport::l1},
		{5, "RB", &ButtonsReport::r1},
		{6, "Back", &ButtonsReport::back},
		{7, "Start", &ButtonsReport::start},
		{8, "Left Stick Click", &ButtonsReport::l3},
		{9, "Right Stick Click", &ButtonsReport::r3},
		{10, "Xbox", &ButtonsReport::xbox},
		{11, "LT", &ButtonsReport::l2},
		{12, "RT", &ButtonsReport::r2},
	};

	// Skip L2/R2 if we have analog triggers
	uint8_t buttonEndIdx = sizeof(xbox_buttons) / sizeof(xbox_buttons[0]);
	if (hasAnalogTriggers) {
		buttonEndIdx-=2;  // Only map up to RB, skip LT and RT
	}

	for (size_t i = 0; i < buttonEndIdx; i++) {
		if (!g_mappingState.active)
			break;

		static wchar_t msg[256];
		swprintf(msg, 256, WTr(STR_PRESS_BUTTON_FMT), xbox_buttons[i].xbox_name);
		XNotifyUI(XNOTIFYUI_CUSTOM, msg);

		uint8_t previousButtonIdx = 0xFF;
		uint8_t foundButtonIdx = 0xFF;
		uint32_t pressWaitCount = 0;
		bool skipped = false;

		while (g_mappingState.active && foundButtonIdx == 0xFF && !skipped) {
			uint8_t currentButtonIdx = g_mappingState.pressedButtonIdx;

			if (previousButtonIdx == 0xFF && currentButtonIdx != 0xFF) {
				// Button just pressed
				pressWaitCount = 0;
				g_mappingState.holdCount = 0;
			} else if (previousButtonIdx != 0xFF && currentButtonIdx == 0xFF) {
				// Button just released
				if (pressWaitCount > 0) {
					foundButtonIdx = previousButtonIdx;
				}
				pressWaitCount = 0;
				g_mappingState.holdCount = 0;
			} else if (currentButtonIdx != 0xFF) {
				pressWaitCount++;
				g_mappingState.holdCount++;
				// 3 second hold = 60 iterations at 50ms each
				if (g_mappingState.holdCount >= 60) {
					skipped = true;
					XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_MAPPING_SKIPPED));
					// Wait for button release
					while (g_mappingState.active && g_mappingState.pressedButtonIdx != 0xFF) {
						Sleep(50);
					}
				}
			}

			previousButtonIdx = currentButtonIdx;
			Sleep(50);
		}

		if (foundButtonIdx != 0xFF) {
			HidButtonMapEntry entry = {foundButtonIdx, xbox_buttons[i].field};
			mappedButtons.push_back(entry);
		}
	}

	// Map D-Pad buttons if no hat switch
	if (!hasHatSwitch && g_mappingState.active) {
		const struct {
			const char* dpad_name;
			uint8_t ButtonsReport::* field;
		} dpad_buttons[] = {
			{"D-Pad Left", &ButtonsReport::dpad_left},
			{"D-Pad Right", &ButtonsReport::dpad_right},
			{"D-Pad Up", &ButtonsReport::dpad_up},
			{"D-Pad Down", &ButtonsReport::dpad_down},
		};

		for (size_t i = 0; i < sizeof(dpad_buttons) / sizeof(dpad_buttons[0]); i++) {
			wchar_t msg[256];
			swprintf(msg, 256, WTr(STR_PRESS_BUTTON_FMT), dpad_buttons[i].dpad_name);
			XNotifyUI(XNOTIFYUI_CUSTOM, msg);

			uint8_t previousButtonIdx = 0xFF;
			uint8_t foundButtonIdx = 0xFF;
			uint32_t pressWaitCount = 0;
			bool skipped = false;

			while (g_mappingState.active && foundButtonIdx == 0xFF && !skipped) {
				uint8_t currentButtonIdx = g_mappingState.pressedButtonIdx;

				if (previousButtonIdx == 0xFF && currentButtonIdx != 0xFF) {
					pressWaitCount = 0;
					g_mappingState.holdCount = 0;
				} else if (previousButtonIdx != 0xFF && currentButtonIdx == 0xFF) {
					if (pressWaitCount > 0) {
						foundButtonIdx = previousButtonIdx;
					}
					pressWaitCount = 0;
					g_mappingState.holdCount = 0;
				} else if (currentButtonIdx != 0xFF) {
					pressWaitCount++;
					g_mappingState.holdCount++;
					if (g_mappingState.holdCount >= 60) {
						skipped = true;
						XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_MAPPING_SKIPPED));
						// Wait for button release
						while (g_mappingState.active && g_mappingState.pressedButtonIdx != 0xFF) {
							Sleep(50);
						}
					}
				}

				previousButtonIdx = currentButtonIdx;
				Sleep(50);
			}

			if (foundButtonIdx != 0xFF) {
				HidButtonMapEntry entry = {foundButtonIdx, dpad_buttons[i].field};
				mappedButtons.push_back(entry);
			}
		}
	}

	// use static axis mapping for now as for gamepads it should be the same for every gamepad
	HidAxisMapEntry entry;

	entry.usage = HID_USAGE_AXIS_X;
	entry.field = &ButtonsReport::x;
	mappedAxes.push_back(entry);

	entry.usage = HID_USAGE_AXIS_Y;
	entry.field = &ButtonsReport::y;
	mappedAxes.push_back(entry);

	entry.usage = HID_USAGE_AXIS_Z;
	entry.field = &ButtonsReport::z;
	mappedAxes.push_back(entry);

	entry.usage = HID_USAGE_AXIS_RX;
	entry.field = &ButtonsReport::rx;
	mappedAxes.push_back(entry);

	entry.usage = HID_USAGE_AXIS_RY;
	entry.field = &ButtonsReport::ry;
	mappedAxes.push_back(entry);

	entry.usage = HID_USAGE_AXIS_RZ;
	entry.field = &ButtonsReport::rz;
	mappedAxes.push_back(entry);

	// Build dynamic mapping
	std::unique_ptr<DynamicMappingData> dynamicData(new DynamicMappingData());
	dynamicData->axisEntries = mappedAxes;
	dynamicData->buttonEntries = mappedButtons;

	HidDeviceMapping newMapping = {0};
	newMapping.vendorId = controller->vendorId;
	newMapping.productId = controller->productId;
	newMapping.axisMapCount = (uint8_t)mappedAxes.size();
	newMapping.buttonMapCount = (uint8_t)mappedButtons.size();
	newMapping.invert = inverts;

	if (!mappedAxes.empty()) {
		newMapping.axisMap = dynamicData->axisEntries.data();
	}
	if (!mappedButtons.empty()) {
		newMapping.buttonMap = dynamicData->buttonEntries.data();
	}

	// Only save if mapping process wasn't interrupted
	if (!g_mappingState.active) {
		SafeDbgPrint("X360Remap: Mapping was interrupted - not saving incomplete mapping\n");
		memset(&g_mappingState, 0, sizeof(MappingState));
		g_mappingState.pressedButtonIdx = 0xFF;
		return 0;
	}

	// Apply mapping to controller
	g_dynamicData.push_back(std::move(dynamicData));
	g_dynamicData.back()->axisEntries = mappedAxes;
	g_dynamicData.back()->buttonEntries = mappedButtons;

	HidDeviceMapping finalMapping = newMapping;
	finalMapping.axisMap = g_dynamicData.back()->axisEntries.data();
	finalMapping.buttonMap = g_dynamicData.back()->buttonEntries.data();

	g_dynamicMappings.push_back(finalMapping);
	connectedControllers[controllerIndex].map = &g_dynamicMappings.back();

	SaveMappingsToStudioConfig();

	XNotifyUI(XNOTIFYUI_CUSTOM, (PWCHAR)WTr(STR_MAPPING_COMPLETE));
	g_mappingState.active = false;

	return 0;
}

// --- AUDIT_2026-09.md, C1 : statut de fin de transfert (INTERRUPTEUR) --------
//
// Le second parametre de ce gestionnaire est le STATUT du transfert qui vient
// de se terminer - meme signature que tous les autres gestionnaires de ce
// fichier, qui testent tous NT_ERROR(status). Celui-ci etait le seul a
// l'ignorer : il remettait un transfert en file quoi qu'il arrive, y compris
// apres un echec du au debranchement du peripherique.
//
// HYPOTHESE A TESTER (non prouvee) : transfert en echec -> remis en file ->
// echoue a nouveau immediatement -> boucle au niveau d'interruption. C'est la
// signature mesuree au debogueur le 2026-09-12 (processeur impossible a
// preempter), et cela expliquerait aussi le gel de l'essai 1/3.
//
// RISQUE CONNU de l'activer : si une souris produit une erreur PASSAGERE en
// fonctionnement normal, elle cesserait de repondre jusqu'au rebranchement.
// D'ou l'interrupteur, et la trace ecrite a chaque arret (compteur d'erreurs
// dans le journal) pour distinguer les deux cas au test.
//
// PROTOCOLE : 1 -> recompiler -> cycles branchement/debranchement comme pour
// l'essai 1/3, puis 10 minutes de jeu normal. Si aucun gel ET aucune souris
// muette : garder a 1, puis REFAIRE l'essai 1/3 (fermeture de l'endpoint
// d'interruption) avec ce correctif en place.
#define EN_TEST_INTERRUPT_STATUS_CHECK 0

// g_interruptErrorStops : declare en tete de fichier (le heartbeat du thread
// de polling, defini plus haut, le journalise).

int interruptHandler(DWORD deviceHandle, int32_t a2) {
	HidControllerExtension* driverExtension = (HidControllerExtension*)((deviceHandle - 4));

	// Verifications AVANT tout dereferencement (l'ancienne version lisait
	// interruptTrb.buffer avant de tester quoi que ce soit).
	if (!driverExtension || !driverExtension->deviceHandle ||
		!driverExtension->deviceHandle->driver ||
		driverExtension->deviceHandle->driver->cleanupDone)
		return 0;

#if EN_TEST_INTERRUPT_STATUS_CHECK
	if (NT_ERROR(a2)) {
		g_interruptErrorStops++;
		return 0;   // ne JAMAIS remettre en file apres une erreur
	}
#else
	// 2026-10-05: same test, switched at boot by a file (see g_interruptStatusCheck).
	if (g_interruptStatusCheck && NT_ERROR(a2)) {
		g_interruptErrorStops++;
		return 0;   // never re-queue after an error
	}
#endif

	Report* report = (Report*)driverExtension->interruptTrb.buffer;

	int index = -1;
	for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
		if (connectedControllers[i].controllerDriver == driverExtension) {
			index = i;
			break;
		}
	}

	// AUDIT_2026-09.md, C1 (toujours actif, pas d'interrupteur : ne change
	// rien au fonctionnement normal). Controleur introuvable = il vient d'etre
	// retire par HidRemoveDeviceHook alors que ce transfert etait encore en
	// vol. L'ancien code continuait avec index = -1 et ECRIVAIT dans
	// connectedControllers[-1], c'est-a-dire dans la memoire qui precede le
	// tableau, puis remettait un transfert en file sur un peripherique en
	// cours de retrait. On s'arrete la.
	if (index < 0)
		return 0;

	// Extra keyboard interface (N-key rollover / media keys, see
	// Controller::isExtraKeyboardIf): never decoded, so it can no longer send
	// wrong keys or overwrite the boot interface's state. Its first 4 non-empty
	// reports go to the log, in hex, to study new layouts from testers' logs.
	// First 8 non-empty reports of EVERY boot-path keyboard interface go to
	// the log in hex (2026-10-11, Tartarus tester: only the thumbpad answers -
	// this shows on which interface each key really arrives).
	if (connectedControllers[index].isKeyboard && connectedControllers[index].isBootProtocol) {
		Controller& xc = connectedControllers[index];
		const uint8_t* raw = (const uint8_t*)report;
		const int n = xc.reportSize < 16 ? (int)xc.reportSize : 16;
		bool any = false;
		for (int i = 0; i < n; i++) if (raw[i]) { any = true; break; }
		// Copy only (no CRT, no log, no lock) - the polling thread logs it.
		const uint8_t k = xc.keyRawCount;
		if (any && k < 8) {
			memset(xc.keyRaw[k], 0, 16);
			for (int i = 0; i < n; i++) xc.keyRaw[k][i] = raw[i];
			MemoryBarrier();
			xc.keyRawCount = (uint8_t)(k + 1);
		}
	}
	if (connectedControllers[index].isExtraKeyboardIf)
		return UsbdQueueAsyncTransfer(driverExtension->deviceHandle, &driverExtension->interruptTrb);

	if (NeedsNintendoHandshake(connectedControllers[index].vendorId, connectedControllers[index].productId) && connectedControllers[index].nintendo_handshake_state != DONE) {
		if (connectedControllers[index].nintendo_handshake_state == INITIAL) {
			SafeDbgPrint("X360Remap: Gotta do nintendo handshake for this one!\r\n");
			usb_endpoint_descriptor* endpoint_descriptor = UsbdGetEndpointDescriptor(
				driverExtension->deviceHandle, 0,
				USB_ENDPOINT_TYPE_INTERRUPT, USB_DIRECTION_OUT);

			if (!endpoint_descriptor) {
				SafeDbgPrint("X360Remap: Failed to find output descriptor\r\n");
				return -1;
			}

			NTSTATUS status = UsbdOpenEndpoint(
				driverExtension->deviceHandle,
				3,
				endpoint_descriptor->bEndpointAddress,
				swap_endianness_16(endpoint_descriptor->wMaxPacketSize) & 0x7FF,
				endpoint_descriptor->bInterval,
				(DWORD*)&connectedControllers[index].interruptTrb);

			if (NT_ERROR(status)) {
				SafeDbgPrint("X360Remap: Failed to open interrupt OUT endpoint %x!\n", status);
				return status;
			}
			connectedControllers[index].nintendo_handshake_state = HANDSHAKE;
			SendInterruptRequest(driverExtension->deviceHandle, &connectedControllers[index].interruptTrb, (void*)nintendo_handshake, sizeof(nintendo_handshake), (DWORD)noopCompleteHandler);
		}

		else if (connectedControllers[index].nintendo_handshake_state == HANDSHAKE) {
			connectedControllers[index].nintendo_handshake_state = DONE;
			SendInterruptRequest(driverExtension->deviceHandle, &connectedControllers[index].interruptTrb, (void*)hid_only_mode, sizeof(hid_only_mode), (DWORD)noopCompleteHandler);
		}
	}

	bool hasReportId = connectedControllers[index].reportInfo &&
		connectedControllers[index].reportInfo->UsingReportIDs; 
	if (report->reportId == connectedControllers[index].reportId || !hasReportId) {
		ButtonsReport buttonReport;
		memset(&buttonReport, 0, sizeof(ButtonsReport));

		const uint8_t* payload = (const uint8_t*)report;
		
		// Check if correct report ID, skip report ID byte for parsing if present
		if (hasReportId) {
			if (payload[0] != connectedControllers[index].reportId)
				return UsbdQueueAsyncTransfer(driverExtension->deviceHandle,
					&driverExtension->interruptTrb);

			payload++;
		}

		// This special case is needed because:
		// https://gbatemp.net/threads/reverse-engineering-the-switch-pro-controller-wired-mode.475226/
		/*
		"WARNING: The HID descriptor does not match the data in the controller payload at all. My guess is it's just the Bluetooth HID descriptor c/p over. Because of that, if you enable the controller on Windows by poking that enable interrupt packet with your favorite USB tool, Windows will go crazy trying to interpret the packets it gets. I now have this on-screen controller keyboard I don't know how to get rid of."
		*/
		if (NeedsNintendoHandshake(connectedControllers[index].vendorId, connectedControllers[index].productId)) {
			switch_pro_input_report* switch_report = (switch_pro_input_report*)payload;
			
			uint16_t lx = switch_report->left_stick[0] | ((switch_report->left_stick[1] & 0x0F) << 8);
			uint16_t ly = (switch_report->left_stick[1] >> 4) | (switch_report->left_stick[2] << 4);
			uint16_t rx = switch_report->right_stick[0] | ((switch_report->right_stick[1] & 0x0F) << 8);
			uint16_t ry = (switch_report->right_stick[1] >> 4) | (switch_report->right_stick[2] << 4);

			// TODO: read the actual calibration values for normalization
			buttonReport.x = normalize_stick(lx);
			buttonReport.y = normalize_stick(ly);
			buttonReport.z = normalize_stick(rx);
			buttonReport.rz = normalize_stick(ry);

			uint16_t b1 = switch_report->buttons_right | ((uint16_t)switch_report->buttons_mid << 8);
			uint8_t  b2 = switch_report->buttons_left;

			buttonReport.a_button = (b1 & SWITCH_BTN_B) ? 1 : 0;
			buttonReport.b_button = (b1 & SWITCH_BTN_A) ? 1 : 0;
			buttonReport.x_button = (b1 & SWITCH_BTN_X) ? 1 : 0;
			buttonReport.y_button = (b1 & SWITCH_BTN_Y) ? 1 : 0;
			buttonReport.r1 = (b1 & SWITCH_BTN_R) ? 1 : 0;
			buttonReport.l1 = (b2 & SWITCH_BTN_L) ? 1 : 0;
			buttonReport.r2 = (b1 & SWITCH_BTN_ZR) ? 1 : 0;
			buttonReport.l2 = (b2 & SWITCH_BTN_ZL) ? 1 : 0;

			buttonReport.r3 = (b1 & SWITCH_BTN_R_STICK) ? 1 : 0;
			buttonReport.l3 = (b1 & SWITCH_BTN_L_STICK) ? 1 : 0;
			buttonReport.start = (b1 & SWITCH_BTN_PLUS) ? 1 : 0;
			buttonReport.back = (b1 & SWITCH_BTN_MINUS) ? 1 : 0;
			buttonReport.xbox = (b1 & SWITCH_BTN_HOME) ? 1 : 0;

			buttonReport.has_hat_switch = false;
			buttonReport.dpad_up = (b2 & SWITCH_DPAD_UP) ? 1 : 0;
			buttonReport.dpad_down = (b2 & SWITCH_DPAD_DOWN) ? 1 : 0;
			buttonReport.dpad_left = (b2 & SWITCH_DPAD_LEFT) ? 1 : 0;
			buttonReport.dpad_right = (b2 & SWITCH_DPAD_RIGHT) ? 1 : 0;
		}

		// Boot-protocol devices must be checked FIRST: they have no parsed
		// reportInfo (it stays null by design on the short init path), so any
		// of the descriptor-based branches below would dereference null.
		else if (connectedControllers[index].isBootProtocol) {
			if (connectedControllers[index].isMouse) {
				HidFillBootMouseState(payload, &buttonReport, &connectedControllers[index]);
			} else if (connectedControllers[index].isKeyboard) {
				HidFillBootKeyboardState(payload, &buttonReport, &connectedControllers[index]);
			}
		}
		// Test de reportInfo ajoute (AUDIT_2026-09.md, C3) : le thread de
		// polling pose reportInfo PUIS isBootProtocol = false, mais sur Xbox 360
		// un autre coeur peut voir ces deux ecritures dans le desordre. Sans ce
		// test, il pouvait arriver ici avec reportInfo encore nul.
		else if (connectedControllers[index].isMouse && connectedControllers[index].reportInfo) {
			HidFillMouseState(
				payload,
				connectedControllers[index].reportInfo,
				&buttonReport,
				connectedControllers[index].reportId,
				&connectedControllers[index]);
		}
		// Branche ajoutee le 2026-09-10 (PREMIER JET NON TESTE SUR MATERIEL) :
		// c'etait le trou documente "CLAVIERS EXCLUS, et c'est structurel"
		// (voir plus haut dans MappingManagerThreadProc) - un clavier non-boot
		// tombait ici avant sans handler dedie, direction la branche manette
		// juste en dessous (son keyMap n'y est jamais applique -> muet).
		// Doit rester AVANT la branche ".map" : un clavier n'a jamais besoin
		// d'un mapping connu pour etre utile (defaut WASD/etc), contrairement
		// a une manette inconnue.
		else if (connectedControllers[index].isKeyboard) {
			HidFillDescriptorKeyboardState(payload, &connectedControllers[index]);
		}
		else if (const HidDeviceMapping* gamepadMap = connectedControllers[index].map) {
			// Pointeur lu une seule fois (AUDIT_2026-09.md, C4) : l'ancien code
			// le relisait pour l'appel, apres l'avoir teste non nul.
			if (connectedControllers[index].reportInfo)
				HidFillButtonsReport(
					payload,
					connectedControllers[index].reportInfo,
					&buttonReport,
					connectedControllers[index].reportId,
					gamepadMap);
		}
		else if (g_mappingState.active && g_mappingState.controllerIndex == index) {
			// Collect raw button states during mapping - only check discovered buttons
			g_mappingState.pressedButtonIdx = 0xFF;
			uint8_t currentPressCount = 0;

			for (uint8_t i = 0; i < g_mappingState.availableButtonCount; i++) {
				uint8_t buttonIdx = g_mappingState.availableButtons[i];
				HID_ReportItem_t* item = FindButtonItem(connectedControllers[index].reportInfo, buttonIdx, connectedControllers[index].reportId);
				if (item && USB_GetHIDReportItemInfo(connectedControllers[index].reportId, payload, item)) {
					if (item->Value) {
						g_mappingState.pressedButtonIdx = buttonIdx;
						currentPressCount++;
					}
				}
			}

			// Clear if multiple buttons pressed (avoid accidental mappings)
			if (currentPressCount != 1) {
				g_mappingState.pressedButtonIdx = 0xFF;
			}

			// Collect raw axis states
			for (uint8_t i = 0; i < 6; i++) {
				static const uint16_t usages[] = {HID_USAGE_AXIS_X, HID_USAGE_AXIS_Y, HID_USAGE_AXIS_Z,
									  HID_USAGE_AXIS_RX, HID_USAGE_AXIS_RY, HID_USAGE_AXIS_RZ};
				HID_ReportItem_t* item = FindItemByUsage(connectedControllers[index].reportInfo,
					HID_USAGE_PAGE_GENERIC_DESKTOP, usages[i], connectedControllers[index].reportId);
				if (item && USB_GetHIDReportItemInfo(connectedControllers[index].reportId, payload, item)) {
					int32_t logMin = (int32_t)item->Attributes.Logical.Minimum;
					int32_t logMax = (int32_t)item->Attributes.Logical.Maximum;
					int32_t raw = (int32_t)item->Value;

					int16_t result = 0;
					if (logMax > logMin) {
						if (raw < logMin) raw = logMin;
						if (raw > logMax) raw = logMax;
						int64_t numerator = (int64_t)(raw - logMin) * 65535;
						int32_t denominator = (logMax - logMin);
						int32_t scaled = (int32_t)((numerator + denominator / 2) / denominator);
						result = (int16_t)(scaled - 32768);
					} else {
						result = (int16_t)raw;
					}
					g_mappingState.axisValues[i] = result;
				}
			}
		}

		connectedControllers[index].currentState = buttonReport;
	}

	return UsbdQueueAsyncTransfer(driverExtension->deviceHandle, &driverExtension->interruptTrb);
}


int HidRemoveDeviceHook(deviceHandle* deviceHandle2) {
	bool found = false;
	int index = 0;
	for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
		if (connectedControllers[i].deviceHandle == deviceHandle2) {
			found = true;
			index = i;
			break;
		}
	}

	if (!found) {
		// Unplugged while its init waited for the polling thread: cancel it,
		// so FinishDescriptorInit never touches a device that is gone.
		HidControllerExtension* pending = g_descDriver;
		if (pending && pending->deviceHandle == deviceHandle2) {
			g_descReady = 0;
			g_descDriver = nullptr;
			g_InitState = InitState::INIT_FAILED;
			g_deviceInitBusy = false;
			FileLog("HidRemoveDevice: handle %p removed during its init - init cancelled", deviceHandle2);
		}
		SafeDbgPrint("X360Remap: Returning original for handle %p\n", deviceHandle2);
		return HidRemoveDeviceDetour.GetOriginal<decltype(&HidRemoveDeviceHook)>()(deviceHandle2);
	}

	SafeDbgPrint("X360Remap: Removing controller with handle %p\n", deviceHandle2);
	// Breadcrumb (2026-10-05): which device, which slot - and ask the polling
	// thread to put it on disk at once, in case this unplug is the one that freezes.
	FileLog("HidRemoveDevice: slot %d VID:%04x PID:%04x handle %p%hs", index,
		connectedControllers[index].vendorId, connectedControllers[index].productId, deviceHandle2,
		connectedControllers[index].isMouse ? " (mouse)" : (connectedControllers[index].isKeyboard ? " (keyboard)" : ""));
	g_forceFlushRequested = true;

	// Check if this controller is currently in mapping process and stop it
	if (g_mappingState.active && g_mappingState.controllerIndex == index) {
		SafeDbgPrint("X360Remap: Stopping mapping process for removed controller %d\n", index);
		g_mappingState.active = false;
		memset(&g_mappingState, 0, sizeof(MappingState));
		g_mappingState.pressedButtonIdx = 0xFF;
	}

	// Garde ajoutee le 2026-08-24 : ce pointeur etait deshereference sans
	// aucune verification. Un handle arrivant ici sans driver - typiquement un
	// peripherique dont l'init a echoue en cours de route dans
	// HidAddDeviceHook, qui retourne alors sans avoir renseigne
	// connectedControllers - suffisait a faire tomber le systeme.
	HidControllerExtension* drv = deviceHandle2->driver;
	if (!drv) {
		FileLog("HidRemoveDevice: handle %p has no driver - handed back to the original handler", deviceHandle2);
		return HidRemoveDeviceDetour.GetOriginal<decltype(&HidRemoveDeviceHook)>()(deviceHandle2);
	}

	if (!drv->cleanupDone) {
		drv->cleanupDone = 1;
		connectedControllers[index].controllerDriver = nullptr;

		// Free HID report info for this controller slot
		if (connectedControllers[index].reportInfo) {
			USB_FreeReportInfo(connectedControllers[index].reportInfo);
			connectedControllers[index].reportInfo = nullptr;
		}
		// Clear mapping data
		connectedControllers[index].map = nullptr;

		// Grab the report buffer BEFORE wiping the struct. This used to be
		// freed after the memset below, which always read back a zeroed
		// pointer - so free(nullptr) did nothing and the real buffer leaked on
		// every single unplug. That matters here because unplugging and
		// replugging is a routine part of using this driver (it is the standard
		// workaround for getting a mouse working after launching a game), so
		// the leak accumulated over a session.
		void* reportDataToFree = connectedControllers[index].reportData;

		// Keyboard+mouse fusion: several boot devices share ONE XAM
		// registration, so it must only be released when the LAST of them is
		// unplugged - otherwise unplugging the keyboard would also kill the
		// mouse's player slot. Remember the binding details before wiping.
		// "Membre de la fusion clavier+souris", et non "device boot"
		// (2026-08-04, meme raison que FindBootComboMember) : une souris
		// basculee sur le chemin descripteur a isBootProtocol = false. Sans ce
		// changement, la debrancher ne liberait pas l'emplacement joueur
		// partage, qui restait occupe jusqu'au redemarrage.
		const bool wasBootDevice = connectedControllers[index].isBootProtocol ||
			connectedControllers[index].isMouse || connectedControllers[index].isKeyboard;
		const int  bindIndex     = connectedControllers[index].xamBindIndex;
		const bool wasBindOwner  = connectedControllers[index].ownsXamBinding;

		memset(&connectedControllers[index], 0, sizeof(Controller));
		// memset leaves lastKeyCode/lastMouseButtonIdx at 0, which is a real
		// HID code (0 = "empty slot" per spec for keycodes, and a valid bit
		// index for mouse buttons) - reset to the actual "nothing pressed"
		// sentinel so a slot reused by the next device doesn't briefly report
		// a phantom press before its first real HID report arrives.
		connectedControllers[index].lastKeyCode = 0xFF;
		connectedControllers[index].lastMouseButtonIdx = 0xFF;
		// --- Fermeture des endpoints AVANT de rendre la memoire (2026-08-24) ---
		//
		// Ces trois fonctions etaient resolues depuis le noyau au demarrage
		// (voir initFunctionPointers) mais n'etaient appelees NULLE PART dans
		// tout le fichier. On rendait donc le bloc contenant controlTrb et
		// interruptTrb alors que la pile USB en detenait encore l'adresse,
		// avec un transfert d'interruption toujours en file - voir le
		// commentaire complet sur QueueDriverFree.
		//
		// UsbdRemoveDeviceComplete repond en outre a UsbdAddDeviceComplete,
		// appele au branchement : sans lui, le systeme n'apprenait jamais que
		// le peripherique etait reellement parti.
		//
		// CONCLUSION DU TEST DE DIAGNOSTIC (2026-08-24) : les trois appels
		// actives EN BLOC (UsbdQueueCloseEndpoint / UsbdQueueCloseDefaultEndpoint
		// / UsbdRemoveDeviceComplete) provoquaient un gel reproductible au
		// debranchement/rebranchement d'une souris precise (VID:046A
		// PID:B092), absent avec une autre souris. Test A/B sur materiel
		// reel : desactives -> deux cycles debranchement/rebranchement de
		// cette souris, aucun gel, plus de 7 minutes de fonctionnement
		// continu ensuite (heartbeats, DIAG, changements de jeu).
		//
		// Ce test n'a PAS isole lequel des trois appels est en cause - il les
		// a testes comme un seul bloc. Le use-after-free d'origine (memoire
		// rendue alors que la pile USB y pointe encore, cf. QueueDriverFree)
		// reste donc reel et non corrige : c'est un choix delibere d'accepter
		// ce risque silencieux plutot qu'un gel certain, en attendant de
		// pouvoir isoler la cause precise sur materiel.
		//
		// 2026-09-11 : trois interrupteurs SEPARES ci-dessous pour tester
		// CHAQUE appel individuellement au prochain acces console, sans jamais
		// en activer plus d'un a la fois (meme methode que le test du
		// 2026-08-24, mais un cran plus fin) :
		//   1. Ne changer QU'UN SEUL des trois EN_TEST_CLOSE_* ci-dessous a 1,
		//      les deux autres a 0.
		//   2. Recompiler, deployer, brancher/debrancher/rebrancher la souris
		//      VID:046A PID:B092 (celle qui a revele le gel) plusieurs fois de
		//      suite.
		//   3. Gel -> cet appel precis est fautif, le noter et repasser a 0.
		//      Pas de gel apres plusieurs cycles -> cet appel est innocent,
		//      passer au suivant.
		//   4. Une fois un appel identifie comme fautif (ou aucun des trois
		//      seul, auquel cas suspecter l'ordre/le delai entre eux), mettre
		//      a jour ce commentaire avec la conclusion avant de repasser a 0.
// ESSAI 1/3 TERMINE (2026-09-12) - EN_TEST_CLOSE_INTERRUPT_ENDPOINT : COUPABLE
// CONFIRME. Gel reproduit sur plusieurs souris, de facon aleatoire (donc
// course, pas defaut d'un peripherique precis). Mesure au debogueur actif
// (outil de debogage XBDM) : sur console gelee, Stop/threads/Go
// echouent tous (0x82DA0100) alors qu'ils fonctionnent parfaitement sur
// console saine avec le meme binaire - le processeur ne peut plus etre
// preempte du tout. Ce n'est donc pas un deadlock entre threads mais un
// blocage a interruptions masquees : coherent avec la fermeture d'un
// endpoint dont le transfert asynchrone est ENCORE EN VOL (mis en file par
// UsbdQueueAsyncTransfer, jamais annule - aucune fonction d'annulation
// n'est resolue dans ce plugin). Repasse a 0 en permanence.
#define EN_TEST_CLOSE_INTERRUPT_ENDPOINT 0   // COUPABLE CONFIRME - ne pas reactiver sans fonction d'annulation de transfert
#define EN_TEST_CLOSE_DEFAULT_ENDPOINT   1   // ESSAI 2/3 EN COURS - UsbdQueueCloseDefaultEndpoint (controlTrb) : pas de transfert permanent en vol dessus, donc a priori sans le probleme ci-dessus - a verifier
#define EN_TEST_REMOVE_DEVICE_COMPLETE   0   // ESSAI 3/3 A VENIR - UsbdRemoveDeviceComplete

		if (g_cleanUnplug) {
			// Clean unplug (2026-10-12) - usb-audio-x360's validated recipe:
			// 1. no more re-queue (cleanupDone is set above, interruptHandler
			//    then returns without re-queueing the cancelled transfer);
			// 2. close EVERY opened endpoint with its OWN close request;
			// 3. the memory is freed only after all close callbacks + 1 s
			//    (DrainDriverFreeQueue). UsbdRemoveDeviceComplete is still not
			//    called (calling it here froze their console too).
			memset(drv->closeReq, 0, sizeof(drv->closeReq));
			LONG n = 0;
			if (drv->openedDataEp) n++;
			if (drv->openedDefaultEp) n++;
			drv->closeDoneAt = 0;
			drv->closePending = n;
			MemoryBarrier();
			FileLog("Clean unplug: closing %d endpoint(s) on handle %p", (int)n, deviceHandle2);
			if (drv->openedDataEp) {
				drv->closeReq[0].endpoint = drv->openedDataEp;
				drv->closeReq[0].savedEndpoint = drv->openedDataEp;
				drv->closeReq[0].callback = (DWORD)CloseDataEpComplete;
				UsbdQueueCloseEndpoint(deviceHandle2, &drv->closeReq[0]);
			}
			if (drv->openedDefaultEp) {
				drv->closeReq[1].endpoint = drv->openedDefaultEp;
				drv->closeReq[1].savedEndpoint = drv->openedDefaultEp;
				drv->closeReq[1].callback = (DWORD)CloseDefaultEpComplete;
				UsbdQueueCloseDefaultEndpoint(deviceHandle2, (DWORD*)&drv->closeReq[1]);
			}
			deviceHandle2->driver = nullptr;
			QueueDriverFree(drv);
		} else {
#if EN_TEST_CLOSE_DEFAULT_ENDPOINT
		FileLog("EN_TEST_CLOSE_DEFAULT_ENDPOINT on: UsbdQueueCloseDefaultEndpoint on handle %p", deviceHandle2);
		g_forceFlushRequested = true;
		UsbdQueueCloseDefaultEndpoint(deviceHandle2, (DWORD*)&drv->controlTrb);
		deviceHandle2->driver = nullptr;
		QueueDriverFree(drv);
#else
		deviceHandle2->driver = nullptr;
		delete drv;
#endif
		}

		if (g_deferReportFree) QueueBufferFree(reportDataToFree);   // see g_deferReportFree
		else free(reportDataToFree);
		FileLog("Device removed (handle %p)", deviceHandle2);
		SafeDbgPrint("X360Remap: Removed controller with handle %p\n", deviceHandle2);

		if (wasBootDevice) {
			int remaining = FindBootComboMember();
			if (remaining < 0) {
				XamUserBindDeviceCallback(0xa7553952 + bindIndex, 0x0000000010000005 + bindIndex, 0, true, 0);
				FileLog("Last boot device removed - released shared player slot");
			} else if (wasBindOwner) {
				// Hand ownership to a surviving member; the registration itself
				// (and therefore bindIndex) is unchanged.
				connectedControllers[remaining].ownsXamBinding = true;
				FileLog("Boot device removed - shared player slot kept alive by remaining device");
			}
		} else {
			XamUserBindDeviceCallback(0xa7553952 + index, 0x0000000010000005 + index, 0, true, 0);
		}
		SafeDbgPrint("X360Remap: Removed virtual controller from XAM.\n");
		return 0;
	}

	// Chemin "nettoyage deja fait" : la fonction n'avait AUCUN return ici, elle
	// tombait donc en fin de corps en renvoyant une valeur indeterminee
	// (2026-08-24). Se produit quand le systeme signale deux fois le retrait du
	// meme peripherique, ce qui arrive avec les peripheriques composites.
	FileLog("HidRemoveDevice: cleanup already done for handle %p", deviceHandle2);
	return 0;
}

int reportData = 0;
int HidAddDeviceHook(deviceHandle* deviceHandle) {
	// DbgPrint brut retire d'ici le 2026-08-21 (suspect principal du gel
	// signale par Cyper - voir g_dbgPrintMirrorEnabled). FileLog() plus bas
	// couvre deja la meme information en toute securite (jamais bloquant),
	// et respecte le nouveau reglage par defaut (miroir DbgPrint desactive).
	FileLog("HidAddDevice called. handle=%p", deviceHandle);
	g_usbReportWantedAt = GetTickCount();   // port map: report again once this device settled
	g_usbReportWanted = true;
	usb_device_descriptor* device_descriptor = UsbdGetDeviceDescriptor(deviceHandle);
	usb_interface_descriptor* interface_descriptor = UsbdGetInterfaceDescriptor(deviceHandle);

	uint16_t vendorId = swap_endianness_16(device_descriptor->idVendor);
	uint16_t productId = swap_endianness_16(device_descriptor->idProduct);

	int speed = UsbdGetDeviceSpeed(deviceHandle);
	bool isOhci = speed == 0;

	// (idem plus haut : DbgPrint brut retire, FileLog couvre l'info ci-dessous)
	FileLog("HidAddDevice. VID:%04x PID:%04x class:%d subclass:%d protocol:%d",
		vendorId, productId, interface_descriptor->bInterfaceClass,
		interface_descriptor->bInterfaceSubClass, interface_descriptor->bInterfaceProtocol);

	// --- Device acceptance policy (2026-07-31, "radical" narrowing;
	// ASSOUPLIE le 2026-09-10 pour les claviers non-boot-protocol) ---
	//
	// Previously we claimed EVERY HID-class interface and sorted it out later,
	// after parsing its report descriptor. That is what made the driver grab
	// things it has no use for - most importantly the keyboard (whose keys are
	// not read at all yet) and the second, generic-HID interface that composite
	// keyboards expose for multimedia keys.
	//
	// Claiming those has zero upside and two real costs:
	//   1. Each claimed device is registered with XAM as its own virtual
	//      controller, i.e. an extra *player* - confirmed on hardware (mouse on
	//      slot 1 while a keyboard held slot 0).
	//   2. It froze Aurora intermittently. The intermittency was never illogical:
	//      a composite keyboard's two interfaces arrive ~30ms apart, so whether
	//      the second one is caught by the g_deviceInitBusy guard or claimed
	//      outright depends purely on whether the first one's async init chain
	//      happened to finish first. A race, hence "freezes twice, works the
	//      third time".
	//
	// PREMIER JET NON TESTE SUR MATERIEL (2026-09-10, voir PROJECT_NOTES.md /
	// session "clavier non-boot-protocol non detecte") : un clavier qui ne
	// declare pas le protocole boot standard (bInterfaceProtocol==1) n'a
	// AUCUN moyen de passer ce filtre aujourd'hui, meme s'il est parfaitement
	// utilisable - il est rejete ici, avant meme d'avoir une chance d'etre
	// classe. Ce n'etait pas grave avant : le chemin descripteur n'avait de
	// toute facon aucun gestionnaire clavier (voir le commentaire "CLAVIERS
	// EXCLUS, et c'est structurel" plus bas dans cette fonction, et
	// HidFillDescriptorKeyboardState ci-dessous qui vient combler ce trou).
	// Maintenant qu'un clavier arrivant par ce chemin est reellement lu, le
	// retenir ici n'a plus de justification - mais chacun des DEUX risques
	// listes ci-dessus reste reel pour un vrai peripherique de jeu inconnu :
	//   1. est couvert par le filet de securite ajoute dans
	//      INIT_GET_REPORT_DESCRIPTOR (relache proprement un device qui
	//      s'avere n'etre ni souris, ni clavier, ni manette connue - voir son
	//      commentaire "FILET DE SECURITE" - au lieu de l'enregistrer comme
	//      joueur fantome).
	//   2. reste couvert par g_deviceInitBusy (inchange, verifie plus bas) et
	//      par sameDeviceAlreadyClaimed (inchange, juste en dessous) - aucun
	//      des deux ne dependait de la presence d'un mapping connu, donc les
	//      retirer d'ici ne les affaiblit pas.
	// Donc : on ne claim plus que ce qu'on sait REELLEMENT piloter aujourd'hui
	//   - boot-protocol mice and keyboards -> SHORT PATH below (fixed layout)
	//   - anything with a known mapping, static (DS3/DS4/Switch Pro...) or
	//     loaded from X360Remap.json -> descriptor path (gamepad)
	//   - toute autre interface HID-class -> descriptor path aussi
	//     desormais, uniquement pour etre CLASSEE (DetectIsMouse/
	//     DetectIsKeyboard) ; si elle ne s'avere ni souris ni clavier ni
	//     manette connue, le filet de securite la relache sans l'enregistrer.
	// A VALIDER SUR MATERIEL REEL avant de considerer ce correctif acquis -
	// suivi interne.
	#define CLAIM_ONLY_SUPPORTED_DEVICES 1

	const uint8_t hidProtocol = interface_descriptor->bInterfaceProtocol;
	const bool isBootMouse    = (interface_descriptor->bInterfaceClass == 0x03 && hidProtocol == USB_HID_PROTOCOL_MOUSE);
	const bool isBootKeyboard = (interface_descriptor->bInterfaceClass == 0x03 && hidProtocol == USB_HID_PROTOCOL_KEYBOARD);

	if (g_disableGenericHid && !isBootMouse && !isBootKeyboard) {
		FileLog("VID:%04x PID:%04x left to the console (disable_generic_hid.txt)", vendorId, productId);
		return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
	}

#if CLAIM_ONLY_SUPPORTED_DEVICES
	if (interface_descriptor->bInterfaceClass == 0x03 && !isBootMouse && !isBootKeyboard) {
		const HidDeviceMapping* knownMapping = FindMapping(vendorId, productId);

		// A composite device exposes several interfaces under the SAME VID/PID:
		// a keyboard typically has its boot interface (protocol 1) plus a
		// generic-HID one (protocol 0) for multimedia keys. Only the boot one is
		// useful to us, and claiming the extra one registers a SECOND virtual
		// controller for the same physical device - i.e. a second player, which
		// is exactly the problem the keyboard+mouse fusion exists to avoid.
		//
		// This bit us as soon as the keyboard got an entry in X360Remap.json:
		// the entry made FindMapping() succeed for the generic interface too, so
		// it passed the "known mapping" test and went down the descriptor path,
		// binding to its own slot. Observed on hardware: keyboard fused on slot
		// 0, then re-registered alone on slot 1.
		//
		// Rule (ASSOUPLIE 2026-09-10) : une interface non-boot est rejetee si
		// elle appartient a un device physique deja pris en charge (protection
		// composite inchangee - voir commentaire ci-dessus), point. Le mapping
		// connu n'est plus une condition d'entree : il sert seulement plus bas
		// (HidFillButtonsReport) a mapper une MANETTE, il n'a jamais ete
		// necessaire pour un clavier (defaut WASD/etc deja utilise par le
		// chemin boot, voir HidFillBootKeyboardState). Le filet de securite
		// dans INIT_GET_REPORT_DESCRIPTOR se charge de relacher tout ce qui
		// n'est ni souris, ni clavier, ni manette a mapping connu, une fois
		// le descripteur reellement lu - PLUS FIABLE qu'un refus a l'aveugle
		// ici, avant meme de savoir ce qu'est l'appareil. Une manette
		// combo sans-fil exposant un vrai clavier ET une vraie souris est
		// inaffectee, puisque ce sont deux interfaces boot qui n'atteignent
		// jamais ce bloc.
		bool sameDeviceAlreadyClaimed = false;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].vendorId == vendorId &&
				connectedControllers[i].productId == productId) {
				sameDeviceAlreadyClaimed = true;
				break;
			}
		}

		if (sameDeviceAlreadyClaimed) {
			SafeDbgPrint("X360Remap: Not a supported device, leaving to original handler\n");
			FileLog("Ignored VID:%04x PID:%04x proto:%d - extra interface of an already-claimed device",
				vendorId, productId, hidProtocol);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}
		// knownMapping (FindMapping ci-dessus) n'est plus utilise pour rejeter
		// ici - il sert seulement plus loin dans le pipeline. Variable
		// conservee pour ne pas toucher au reste de la fonction inutilement;
		// marquee explicitement pour eviter un avertissement "unused" selon
		// le compilateur.
		(void)knownMapping;
	}
#endif

	// --- SHORT PATH for boot-protocol mice and keyboards (2026-07-31) ---
	//
	// Their packet layout is fixed by the USB HID spec (see BootMouseReport /
	// BootKeyboardReport in usb.h), so there is nothing to discover: we can skip
	// the whole SET_CONFIGURATION -> GET_HID_DESCRIPTOR -> GET_REPORT_DESCRIPTOR
	// chain and set the device up in one synchronous pass, exactly like the
	// reference keyboard fork (UncreativeXenon/hiddriver360) does.
	//
	// This matters far beyond saving a few USB round-trips: that asynchronous
	// chain drives itself through setConfigurationComplete using SINGLE GLOBAL
	// variables (c, g_InitState, globalIndex, hidDescriptorBuffer,
	// reportDescriptorBuffer). Any second device enumerating while a chain is in
	// flight corrupts it - which is precisely what a composite keyboard does,
	// its two interfaces arriving ~30ms apart, and precisely why the freezes
	// were intermittent. No chain, no shared state, no race.
	if (isBootMouse || isBootKeyboard) {
		// Blackout applies here too. It was briefly lifted for this path on the
		// theory that only the asynchronous descriptor chain was dangerous, but
		// the boot freeze came back, so the restriction is restored and this is
		// the exact configuration known to be stable.
		if (g_bootUsbResetInProgress) {
			FileLog("Deferred VID:%04x PID:%04x - boot window (replug it once booted)", vendorId, productId);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}

		int index = -1;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (!connectedControllers[i].controllerDriver) { index = i; break; }
		}
		if (index == -1) {
			FileLog("Rejected VID:%04x PID:%04x - no free controller index", vendorId, productId);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}

		usb_endpoint_descriptor* ep = UsbdGetEndpointDescriptor(
			deviceHandle, 0, USB_ENDPOINT_TYPE_INTERRUPT, USB_DIRECTION_IN);
		if (!ep) {
			FileLog("Boot device VID:%04x PID:%04x has no interrupt IN endpoint - ignoring", vendorId, productId);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}

		Controller bc;
		memset(&bc, 0, sizeof(Controller));
		bc.deviceHandle    = deviceHandle;
		bc.vendorId        = vendorId;
		bc.productId       = productId;
		bc.map             = FindMapping(vendorId, productId);
		bc.isBootProtocol  = true;
		bc.isMouse         = isBootMouse;
		bc.isKeyboard      = isBootKeyboard;
		bc.reportInfo      = nullptr;   // never parsed on this path - do not dereference
		bc.nintendo_handshake_state = NINTENDO_HANDSHAKE_STATE::INITIAL;

		HidControllerExtension* drv = new HidControllerExtension();
		drv->deviceType = 0;
		drv->deviceHandle = deviceHandle;
		drv->interruptTrb.flags = 1;
		deviceHandle->driver = drv;

		UsbdAddDeviceComplete(deviceHandle, 0);

		NTSTATUS st = UsbdOpenDefaultEndpoint(deviceHandle, (DWORD*)&drv->controlTrb);
		if (NT_ERROR(st)) {
			FileLog("Boot device VID:%04x PID:%04x - control endpoint failed %x", vendorId, productId, st);
			return st;
		}
		drv->openedDefaultEp = drv->controlTrb.trb.endpoint;   // for the clean unplug

		// Force le "Boot Protocol" HID standard (SET_PROTOCOL, HID 1.11
		// section 7.2.6) sur CE device precis, avant d'ouvrir l'endpoint
		// d'interruption plus bas. Sans ca, un device boot-interface-subclass
		// n'est PAS oblige d'envoyer le format legacy fixe suppose par
		// BootMouseReport/BootKeyboardReport (usb.h, buttons/x/y/wheel sur 4
		// octets) - certains modeles demarrent directement en "Report
		// Protocol", avec un format propre a leur descripteur (jamais lu sur
		// ce chemin rapide), ce qui corrompt silencieusement boutons/
		// mouvement. Constate reellement (2026-08-03, retour utilisateur) :
		// une souris VID:046a PID:b092 rapportait des paquets de 8 octets
		// (pktSize plus bas) au lieu des 4 attendus, curseur casse
		// (uniquement vertical, aucun bouton). Correctif GENERAL - pas un cas
		// particulier pour ce VID/PID : n'importe quel device passant par ce
		// chemin recoit desormais cette requete, souris principale de
		// l'utilisateur (deja fonctionnelle) comprise, sans que ca doive
		// changer son comportement si elle etait deja en boot protocol.
		//
		// Fire-and-forget expres, comme les autres requetes de ce chemin
		// rapide : ne bloque rien, n'attend aucune completion avant de
		// continuer, ne touche AUCUNE variable globale partagee
		// (c/g_InitState/globalIndex/*DescriptorBuffer) - contrairement a
		// l'ancienne chaine lente qui causait les gels documentes dans
		// PROJECT_NOTES.md. SetBootProtocolComplete se contente de logger un
		// echec eventuel, sans jamais bloquer/interrompre l'init.
		//
		// PREMIER JET NON COMPILE SUR CONSOLE - a tester avec prudence :
		// confirmer d'abord qu'aucune regression n'apparait sur la souris/le
		// clavier deja fonctionnels avant de tester un 2e peripherique.
		//
		// DEBRAYABLE depuis le 2026-08-03 (voir LoadBootProtocolSetting) : ce
		// forcage repare le curseur de certaines souris mais fait perdre la
		// molette sur d'autres - tant que l'arbitrage n'est pas tranche par
		// des donnees, il doit pouvoir etre coupe sans recompiler.
		// Taille de paquet calculee AVANT la decision : c'est elle qui arbitre
		// en mode AUTO (voir ShouldForceBootProtocol).
		uint16_t pktSize = swap_endianness_16(ep->wMaxPacketSize) & 0x7FF;

		// Keyboard interfaces (2026-10-11, see Controller::isExtraKeyboardIf):
		//  - boot subclass (1): the real boot keyboard. Always put in boot
		//    protocol (unless force_boot_protocol.txt = "0"): keyboards have no
		//    wheel to lose, and this makes gaming keyboards / keypads send all
		//    their keys here in the standard 8-byte layout.
		//  - no boot subclass: extra interface (N-key rollover, media keys).
		//    Claimed, never decoded.
		const bool extraKeyboardIf = isBootKeyboard && interface_descriptor->bInterfaceSubClass != 1;
		const bool bootKeyboardIf  = isBootKeyboard && !extraKeyboardIf;
		bc.isExtraKeyboardIf = extraKeyboardIf;
		bc.lastKeyCode = 0xFF;   // "no key" until a report says otherwise

		// INTERRUPTEUR DE DIAGNOSTIC (2026-08-26) : gel reproductible au
		// branchement standalone (sans xbdm/jrpc2) confirme sur la souris
		// 0461:4E35 (le composant "normal" du combo clavier+souris, PAS la
		// 046A:B092 a 8 octets - deja traitee separement, voir plus bas dans
		// cette fonction). Notifications deja innocentees par deux tests
		// (flush avant XNotifyUI, puis disable_notifications.txt) : le gel
		// persiste meme sans elles. Reste comme suspects les deux appels
		// SendControlRequest de ce chemin synchrone (SET_PROTOCOL et
		// GET_DESCRIPTOR HID plus bas), jamais exerces en standalone avant
		// ce soir. EN_TEST_CONTROL_REQUESTS_BOOT_MOUSE a 0 saute les DEUX
		// pour isoler s'ils sont en cause, sans toucher a rien d'autre sur
		// ce chemin (UsbdOpenDefaultEndpoint/UsbdOpenEndpoint/
		// UsbdQueueAsyncTransfer restent inchanges).
		//
		// CONCLUSION (2026-08-26) : ces deux appels testes et infirmes sur
		// materiel reel - sautes, le gel standalone persiste identique.
		// Remis a 1 (comportement normal) : ils redonnent le fix curseur/
		// molette, confirmes sans lien avec ce gel. Seule la presence de
		// xbdm.xex (meme sans jrpc2) l'evite - prerequis noyau confirme par
		// elimination sur cinq tests distincts (notifications, ces deux
		// appels, UsbdSecPatch, log disque), hors de portee de notre code.
#define EN_TEST_CONTROL_REQUESTS_BOOT_MOUSE 1
#if EN_TEST_CONTROL_REQUESTS_BOOT_MOUSE
		if (ShouldForceBootProtocol(vendorId, productId, pktSize) ||
			(bootKeyboardIf && g_bootProtocolMode != BOOTPROTO_NEVER)) {
			SendControlRequest(deviceHandle, &drv->controlTrb,
				0x21 /* Host->Device, Class, Interface */, 0x0B /* SET_PROTOCOL */,
				0 /* wValue: 0 = Boot Protocol */, interface_descriptor->bInterfaceNumber,
				0, nullptr, (DWORD)SetBootProtocolComplete);
			FileLog(bootKeyboardIf
				? "SET_PROTOCOL(boot) sent to VID:%04x PID:%04x (pkt=%d) - keyboard, standard key layout"
				: "SET_PROTOCOL(boot) sent to VID:%04x PID:%04x (pkt=%d) - wheel unavailable on this device",
				vendorId, productId, (int)pktSize);
		} else if (extraKeyboardIf) {
			FileLog("Extra keyboard interface VID:%04x PID:%04x if:%d (pkt=%d) - claimed, not decoded (keys come from the boot interface)",
				vendorId, productId, (int)interface_descriptor->bInterfaceNumber, (int)pktSize);
		} else {
			FileLog("SET_PROTOCOL(boot) NOT sent to VID:%04x PID:%04x (pkt=%d) - native layout kept, wheel available",
				vendorId, productId, (int)pktSize);
		}
#else
		FileLog("SET_PROTOCOL(boot) SKIPPED (standalone freeze diagnostic) for VID:%04x PID:%04x (pkt=%d)",
			vendorId, productId, (int)pktSize);
#endif

		st = UsbdOpenEndpoint(deviceHandle, 3, ep->bEndpointAddress, pktSize,
			ep->bInterval, (DWORD*)&drv->interruptTrb);
		if (NT_ERROR(st)) {
			FileLog("Boot device VID:%04x PID:%04x - interrupt endpoint failed %x", vendorId, productId, st);
			return st;
		}
		drv->openedDataEp = drv->interruptTrb.endpoint;   // for the clean unplug

		bc.reportSize = pktSize;
		bc.reportData = malloc(pktSize * 2);
		memset(bc.reportData, 0, pktSize * 2);

		drv->interruptTrb.savedEndpoint = drv->interruptTrb.endpoint;
		drv->interruptTrb.length   = pktSize;
		drv->interruptTrb.callback = (DWORD)interruptHandler;
		drv->interruptTrb.buffer   = bc.reportData;
		bc.controllerDriver = drv;

		// Keyboard+mouse fusion: reuse the registration of whichever boot device
		// got here first, so both end up on the SAME player slot. Registering
		// each one separately is what previously produced "keyboard = slot 0,
		// mouse = slot 1", i.e. two players, with games reading only one of them.
		int comboMember = FindBootComboMember();
		if (comboMember >= 0) {
			bc.userIndex      = connectedControllers[comboMember].userIndex;
			bc.deviceContext  = connectedControllers[comboMember].deviceContext;
			bc.xamBindIndex   = connectedControllers[comboMember].xamBindIndex;
			bc.ownsXamBinding = false;
			FileLog("BootDevice joins existing player. VID:%04x PID:%04x %hs pkt:%d map:%hs slot:%d",
				vendorId, productId, isBootMouse ? "MOUSE" : "KEYBOARD",
				pktSize, bc.map ? "YES" : "NO", (int)bc.userIndex);
		} else {
			uint8_t userIndex = 0xFF;
			uint32_t context = 0x0000000010000005 + index;
			XamUserBindDeviceCallback(0xa7553952 + index, context, 0, false, &userIndex);
			bc.userIndex      = userIndex;
			bc.deviceContext  = context;
			bc.xamBindIndex   = index;
			bc.ownsXamBinding = true;
			FileLog("BootDevice ready. VID:%04x PID:%04x %hs pkt:%d map:%hs slot:%d",
				vendorId, productId, isBootMouse ? "MOUSE" : "KEYBOARD",
				pktSize, bc.map ? "YES" : "NO", (int)userIndex);
		}

		connectedControllers[index] = bc;

		// Demande du descripteur de rapport, APRES l'enregistrement dans
		// connectedControllers : les callbacks retrouvent le peripherique par
		// son handle dans ce tableau (FindControllerByHandle), il doit donc
		// deja y etre. Envoi "fire-and-forget" - la boucle d'interruption
		// demarre juste en dessous sans rien attendre, et le peripherique
		// fonctionne en disposition boot figee tant que la reponse n'est pas
		// arrivee. C'est le repli permanent en cas d'echec.
		//
		// Sautee quand l'utilisateur a explicitement force le mode legacy
		// (force_boot_protocol.txt = "1") : dans ce cas il veut precisement
		// l'ancien comportement.
		// CLAVIERS EXCLUS DE LA REQUETE ELLE-MEME (2026-08-04, gel reel
		// constate : brancher le clavier une fois Aurora demarre figeait la
		// console). Un clavier est souvent COMPOSITE - le sien expose deux
		// interfaces a ~30 ms d'intervalle, exactement le scenario identifie
		// comme cause des gels historiques de ce projet. Y ajouter des
		// transferts de controle pendant l'enumeration rouvrait la porte.
		//
		// Et ca ne coute rien : les claviers etaient DEJA exclus de
		// l'application du descripteur (voir le parsing dans le thread de
		// polling), le chemin boot leur convenant parfaitement. On demandait
		// donc une information qu'on avait decide de ne jamais utiliser, au
		// prix du seul risque serieux qui restait.
#if EN_TEST_CONTROL_REQUESTS_BOOT_MOUSE
		if (g_bootProtocolMode != BOOTPROTO_ALWAYS && isBootMouse) {
			connectedControllers[index].reportDescRequestedMs = GetTickCount();
			// Index de la chaine "produit", lu directement dans le descripteur
			// de device deja disponible en synchrone - sert a l'etape 3 de la
			// chaine (voir BootReportDescriptorComplete).
			connectedControllers[index].productStringIndex =
				device_descriptor ? device_descriptor->iProduct : 0;
			connectedControllers[index].interfaceNumber =
				interface_descriptor->bInterfaceNumber;
			SendControlRequest(deviceHandle, &drv->controlTrb,
				0x81 /* Device->Host, Standard, Interface */, 0x06 /* GET_DESCRIPTOR */,
				0x2100 /* HID descriptor */,
				interface_descriptor->bInterfaceNumber, // wIndex : voir BootHidDescriptorComplete
				sizeof(usb_hid_descriptor), &connectedControllers[index].hidDesc,
				(DWORD)BootHidDescriptorComplete);
			FileLog("HID descriptor requested for VID:%04x PID:%04x", vendorId, productId);
		}
#else
		FileLog("HID descriptor SKIPPED (standalone freeze diagnostic) for VID:%04x PID:%04x - boot layout frozen",
			vendorId, productId);
#endif

		return UsbdQueueAsyncTransfer(deviceHandle, &drv->interruptTrb);
	}

	// Accept any USB HID-class interface, regardless of subclass/protocol.
	// Earlier this was restricted to "generic HID" (subclass 0/protocol 0) plus
	// boot-protocol mice (subclass 1/protocol 2) specifically, but real mice
	// don't reliably stick to those two combinations - two different test mice
	// were silently rejected here before ever reaching DetectIsMouse(), which
	// reads the actual report descriptor and is a much more reliable signal.
	// Classification (mouse vs gamepad vs "nothing usable, e.g. a keyboard")
	// happens downstream once the report descriptor is parsed.
	if (interface_descriptor->bInterfaceClass == 0x03) {
		// See g_bootUsbResetInProgress - claiming a device from inside the
		// boot-time USB driver reset freezes the console at the boot animation.
		if (g_bootUsbResetInProgress) {
			SafeDbgPrint("X360Remap: Boot USB reset in progress, leaving VID:%x PID:%x to original handler\n",
				vendorId, productId);
			FileLog("Deferred VID:%04x PID:%04x - boot USB reset window (replug it once booted)",
				vendorId, productId);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}

		// See g_deviceInitBusy comment - never touch the shared init state
		// (c/g_InitState/globalIndex/*DescriptorBuffer) while another device's
		// async init chain is still in flight. Deferring to the original
		// handler here is far safer than the freeze this used to cause.
		if (g_deviceInitBusy) {
			SafeDbgPrint("X360Remap: Another device init already in progress, deferring VID:%x PID:%x to original handler\n",
				vendorId, productId);
			FileLog("Rejected VID:%04x PID:%04x - another device init already in progress (re-entrancy guard)",
				vendorId, productId);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}

		SafeDbgPrint("X360Remap: HID device detected (subclass %d, protocol %d). Initialising custom handler.\n",
			interface_descriptor->bInterfaceSubClass, interface_descriptor->bInterfaceProtocol);
		int index = -1;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (!connectedControllers[i].controllerDriver) {
				SafeDbgPrint("Assigning controller to index %d\n", i);
				index = i;
				break;
			}
		}

		if (index == -1) {
			SafeDbgPrint("X360Remap: No free index!\n");
			FileLog("Rejected VID:%04x PID:%04x - no free controller index (4 already in use)", vendorId, productId);
			return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
		}
		globalIndex = index;
		g_deviceInitBusy = true;
		g_deviceInitBusySince = GetTickCount();

		c = Controller();
		memset(&c, 0, sizeof(Controller));
		c.packetNumber = 0;
		c.reportInfo = nullptr;   // will be filled in INIT_GET_REPORT_DESCRIPTOR
		c.vendorId = vendorId;
		c.productId = productId;
		// Numero d'interface, utilise dans wIndex des requetes de descripteur
		// (AUDIT_2026-09.md, C5 - voir setConfigurationComplete).
		c.interfaceNumber = interface_descriptor->bInterfaceNumber;
		c.map = FindMapping(vendorId, productId);
		c.nintendo_handshake_state = NINTENDO_HANDSHAKE_STATE::INITIAL;

		HidControllerExtension* controllerDriver = new HidControllerExtension();
		c.deviceHandle = deviceHandle;
		controllerDriver->deviceType = 0;
		deviceHandle->driver = controllerDriver;
		controllerDriver->deviceHandle = deviceHandle;
		controllerDriver->interruptTrb.flags = 1;

		UsbdAddDeviceComplete(deviceHandle, 0);

		NTSTATUS status = UsbdOpenDefaultEndpoint(deviceHandle, (DWORD*)&controllerDriver->controlTrb);
		if (NT_ERROR(status)) {
			SafeDbgPrint("X360Remap: Failed to open control endpoint %x!\n", status);
			g_deviceInitBusy = false;
			return status;
		}
		controllerDriver->openedDefaultEp = controllerDriver->controlTrb.trb.endpoint;   // for the clean unplug

		g_InitState = InitState::INIT_SET_CONFIGURATION;
		SendControlRequest(
			controllerDriver->deviceHandle,
			&controllerDriver->controlTrb,
			0x00,
			0x09,
			1, 0, 0,
			nullptr,
			(DWORD)setConfigurationComplete);

		return 0;
	}

	SafeDbgPrint("X360Remap: Unrelated USB Device. Calling original...\n");
	FileLog("Rejected VID:%04x PID:%04x - not a HID-class interface (class:%d)",
		vendorId, productId, interface_descriptor->bInterfaceClass);
	return HidAddDeviceDetour.GetOriginal<decltype(&HidAddDeviceHook)>()(deviceHandle);
}

DWORD XamInputSetStateHook(DWORD user, DWORD flags, XINPUT_STATE* pInputState, BYTE bAmplitude, BYTE bFrequency, BYTE bOffset) {
	// Trace UNIQUE (2026-08-23) : confirme que le dashboard appelle reellement
	// nos hooks XInput, et date le premier appel. Le gel observe se produit
	// pendant le demarrage d'Aurora, or c'est precisement le moment ou elle
	// commence a interroger les manettes - il faut donc savoir si nos hooks
	// sont dans la boucle a cet instant. Une seule ligne par demarrage : ces
	// fonctions sont appelees des dizaines de fois par seconde. FileLog
	// n'ecrit qu'en memoire, aucun risque depuis ce contexte.
	static bool loggedFirstCall = false;
	if (!loggedFirstCall) {
		loggedFirstCall = true;
		if (!g_firstXamInputTick) g_firstXamInputTick = GetTickCount() | 1;
		FileLog("HOOK: first call XamInputSetState");
	}
	DWORD status = XamInputSetStateDetour.GetOriginal<decltype(&XamInputSetStateHook)>()(user, flags, pInputState, bAmplitude, bFrequency, bOffset);

	if ((user & 0xFF) == 0xFF)
		user = 0;

	if (status == ERROR_DEVICE_NOT_CONNECTED) {
		Controller* c = nullptr;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].userIndex == user) {
				c = &connectedControllers[i];
				break;
			}
		}
		if (!c)
			return status;
		return ERROR_SUCCESS;
	}

	return status;
}

DWORD XamInputGetCapabilitiesExHook(DWORD unk, DWORD user, DWORD flags, XINPUT_CAPABILITIES_EX* capabilities) {
	// Meme trace unique que XamInputSetStateHook - voir son commentaire.
	static bool loggedFirstCall = false;
	if (!loggedFirstCall) {
		loggedFirstCall = true;
		if (!g_firstXamInputTick) g_firstXamInputTick = GetTickCount() | 1;
		FileLog("HOOK: first call XamInputGetCapabilitiesEx");
	}
	DWORD status = XamInputGetCapabilitiesDetour.GetOriginal<decltype(&XamInputGetCapabilitiesExHook)>()(unk, user, flags, capabilities);

	if ((user & 0xFF) == 0xFF)
		user = 0;

	if (!capabilities)
		return status;

	if (status == ERROR_DEVICE_NOT_CONNECTED) {
		Controller* c = nullptr;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].userIndex == user) {
				c = &connectedControllers[i];
				break;
			}
		}
		if (!c)
			return status;

		capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
		capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
		capabilities->Flags = 0;

		XINPUT_STATE state;
		memset(&state, 0, sizeof(XINPUT_STATE));
		XInputGetState(user, &state);
		capabilities->Gamepad = state.Gamepad;
		capabilities->Vibration.wLeftMotorSpeed = 0;
		capabilities->Vibration.wRightMotorSpeed = 0;
		return ERROR_SUCCESS;
	}

	// AUDIT_2026-09.md, C2 : ce return MANQUAIT. Pour toute vraie manette
	// (statut different de ERROR_DEVICE_NOT_CONNECTED), la fonction atteignait
	// l'accolade finale sans rien renvoyer : le jeu recevait ce qui trainait
	// dans un registre. Fonctionnait par chance.
	return status;
}

// Applique un champ ButtonsReport resolu (voir mapping.h/GetButtonFieldPtr)
// directement sur pInputData - utilise UNIQUEMENT pour la molette
// configurable (2026-08-02, voir plus bas) : a ce point du code, `b` (le
// ButtonsReport fusionne de tous les peripheriques) a deja ete traduit vers
// pInputData (sThumbLX/RX, bLeftTrigger/RightTrigger, wButtons...) plus haut
// dans cette fonction - trop tard pour repasser par le pipeline habituel
// (`out->*entry.field = ...`, voir HidFillButtonState un peu plus haut dans
// ce fichier), d'ou ce petit aiguillage direct. Ne couvre que les 12 cibles
// exposees dans application.xex (kMouseButtonTargets, main.cpp) - pas besoin
// de plus puisque rien d'autre ne peut arriver ici.
static void ApplyButtonFieldToXInput(PXINPUT_GAMEPAD pInputData, uint8_t ButtonsReport::* field) {
	if (field == &ButtonsReport::a_button)      pInputData->wButtons |= XINPUT_GAMEPAD_A;
	else if (field == &ButtonsReport::b_button) pInputData->wButtons |= XINPUT_GAMEPAD_B;
	else if (field == &ButtonsReport::x_button) pInputData->wButtons |= XINPUT_GAMEPAD_X;
	else if (field == &ButtonsReport::y_button) pInputData->wButtons |= XINPUT_GAMEPAD_Y;
	else if (field == &ButtonsReport::l1)       pInputData->wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
	else if (field == &ButtonsReport::r1)       pInputData->wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
	else if (field == &ButtonsReport::l3)       pInputData->wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
	else if (field == &ButtonsReport::r3)       pInputData->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
	else if (field == &ButtonsReport::start)    pInputData->wButtons |= XINPUT_GAMEPAD_START;
	else if (field == &ButtonsReport::back)     pInputData->wButtons |= XINPUT_GAMEPAD_BACK;
	// D-Pad ajoute le 2026-08-03 : ces 4 cibles MANQUAIENT, alors que le
	// commentaire au-dessus affirmait couvrir "toutes les cibles exposees
	// dans application.xex". Consequence concrete : toute cible D-Pad
	// resolue ici (molette configuree sur une direction, ou liaison rapide
	// vers le D-Pad) etait silencieusement ignoree - la fonction retombait
	// sur la derniere branche sans rien faire, sans erreur ni log. C'est le
	// bug qui rendait le D-Pad inutilisable comme cible.
	else if (field == &ButtonsReport::dpad_up)    pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	else if (field == &ButtonsReport::dpad_down)  pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	else if (field == &ButtonsReport::dpad_left)  pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	else if (field == &ButtonsReport::dpad_right) pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	// l2/r2 sont des gachettes analogiques (bLeftTrigger/bRightTrigger), pas
	// des bits de wButtons - meme convention que pInputData->bLeftTrigger
	// plus haut dans cette fonction (b.l2 ? 255 : 0).
	else if (field == &ButtonsReport::l2)       pInputData->bLeftTrigger = 255;
	else if (field == &ButtonsReport::r2)       pInputData->bRightTrigger = 255;
}

// idx 3/4 dans le buttonMap d'une souris (au-dela du 0/1/2 gauche/droit/
// milieu deja utilises pour les clics, voir HidFillMouseState/
// HidFillBootMouseState) = convention reservee pour "molette avant"/"molette
// arriere", ecrite par application.xex via ApplyMouseButtonMapping (voir
// config_writer.cpp) quand l'utilisateur configure la molette dans l'onglet
// Reglages souris. nullptr si aucune entree pour cet idx (comportement par
// defaut inchange, voir plus bas).
//
// Passe par ResolveEffectiveButtonMap depuis le 2026-08-03 (au lieu de lire
// map->buttonMap directement) : une liaison molette definie au niveau d'un
// PROFIL de jeu etait auparavant ignoree, seul le reglage device-level etait
// consulte - incoherent avec tout le reste du pipeline boutons, qui resout
// deja profil-puis-device (voir HidFillMouseState/HidFillButtonState).
static uint8_t ButtonsReport::* FindWheelOverride(const HidDeviceMapping* map, uint32_t titleId, uint8_t idx) {
	if (!map)
		return nullptr;
	HidButtonMapEntry entries[MAX_MERGED_BUTTONS];
	uint8_t count = ResolveMergedButtonMap(map, titleId, entries, MAX_MERGED_BUTTONS);
	for (uint8_t i = 0; i < count; i++) {
		if (entries[i].idx == idx)
			return entries[i].field;
	}
	return nullptr;
}

// --- Report du mouvement qui depasse le stick (2026-09-23) -------------------
//
// LE VRAI PROBLEME, mesure par HB sur Tomb Raider (sensibilite au maximum,
// zone morte 0, inertie desactivee) : 20 gestes de souris pour un tour
// complet de camera.
//
// Un stick ne donne pas une POSITION, il donne une VITESSE de rotation, et
// chaque jeu plafonne cette vitesse (stick a fond = vitesse maximale du jeu).
// Un geste de souris rapide dure ~0,2 s : le stick est a fond pendant 0,2 s
// seulement, et tout le mouvement demande AU-DELA du plafond etait
// simplement PERDU (ecretage a 32767). Augmenter la sensibilite n'y change
// rien : on ecrete juste plus tot. C'est pourquoi l'inertie avait ete ajoutee
// le 2026-08-02 - elle prolongeait le geste, mais par une decroissance
// arbitraire qui ajoutait du retard a TOUS les mouvements (le 0,5 s du
// testeur) et un mouvement fantome apres l'arret de la main.
//
// Correctif : ne plus rien perdre. Ce qui depasse le stick est mis en
// reserve et rendu aux lectures suivantes, stick a fond, jusqu'a epuisement.
// Differences decisives avec l'inertie :
//   - un mouvement qui tient dans le stick (visee fine) sort IMMEDIATEMENT et
//     en entier : aucun retard, aucune queue ;
//   - un grand geste produit EXACTEMENT la rotation demandee, simplement
//     etalee a la vitesse maximale du jeu - rien de plus, rien de moins ;
//   - changer de sens annule la reserve dans l'ancien sens : la camera suit
//     la main, elle ne finit pas un mouvement abandonne.
// La vitesse maximale du jeu reste une limite physique : un demi-tour eclair
// prendra le temps que le jeu impose. Mais il ne faudra plus 20 gestes.
//
// PROTOCOLE : meme jeu (Tomb Raider), memes reglages, et dans le jeu la
// sensibilite de visee au MAXIMUM (c'est elle qui fixe la vitesse de
// rotation maximale). Compter les gestes pour un tour complet, et juger le
// ressenti de la visee fine. 0 = comportement du test d'inertie precedent.
// === UN SEUL INTERRUPTEUR POUR LE RESSENTI SOURIS EN JEU (2026-09-23) =====
//
// REGRESSION RECONNUE. Retour HB : "cela marchait avant ton fix", et deux
// souris a sensibilite maximale donnent des resultats differents.
//
// Cause, verifiee par calcul : l'inertie ne faisait pas que lisser. Son
// filtre ADDITIONNE a chaque lecture (v = v*decroissance + mouvement), ce
// qui multiplie tout mouvement par ~7,4 a 60 lectures/s (~4 a 30/s) - c'etait
// deja ecrit ici le 2026-09-14, sans en tirer la consequence. Desactiver
// l'inertie pour le test du retard a donc AUSSI divise la sensibilite
// effective par ~7. Au maximum du curseur, ce qui suffisait avant ne suffit
// plus : 20 gestes pour un tour, et la souris au DPI le plus faible est la
// premiere a manquer de course - d'ou l'ecart entre souris, qui n'apparaissait
// pas avant parce que le gain cache saturait tout.
//
//   0 = EXACTEMENT le comportement d'avant le 2026-09-14 (inertie active,
//       pas de report, diviseur 60). A utiliser pour confirmer que tout est
//       revenu a l'etat "ca marchait".
//   1 = remplacement : pas d'inertie, report du mouvement excedentaire, et
//       diviseur 60 -> 8 pour restituer le gain que l'inertie apportait.
//       Meme deplacement TOTAL qu'avant pour un meme geste (7,4 x), mais
//       livre immediatement au lieu d'etre etale sur ~0,5 s : c'est
//       precisement ce retard que le testeur signalait.
// 2026-09-25 : passe a 1 pour le test. Retour HB : "sur les FPS, pour une
// visee rapide y'a comme un lag" - exactement le retard de ~0,5 s de
// l'inertie decrit ci-dessus. Si la visee est moins bonne : remettre 0.
#define EN_TEST_CARRY_REPLACES_INERTIA 1

#define EN_TEST_MOUSE_OVERFLOW_CARRY EN_TEST_CARRY_REPLACES_INERTIA

// Reserve maximale : ~0,75 s de stick a fond a 60 lectures/s. Au-dela, un
// geste enorme continuerait de faire tourner la camera trop longtemps apres
// l'arret de la main - on prefere perdre l'exces que garder la main.
static const int32_t kOverflowCarryMax = 32767 * 45;

// Resolution de reference des souris MESUREES : 800 DPI = 800 / 2,54 points
// par centimetre. 800 est la resolution par defaut la plus repandue ; une
// souris a 800 DPI mesuree garde donc exactement son ressenti actuel.
// Voir la normalisation dans XInputdReadStateHook.
#define EN_FIX_MOUSE_DPI_NORMALIZATION 1
static const float kReferenceCountsPerCm = 800.0f / 2.54f;

static int32_t ApplyOverflowCarry(int32_t requested, int32_t* carry) {
	int64_t pending = *carry;
	// Changement de sens : la reserve dans l'ancien sens est abandonnee.
	if ((requested > 0 && pending < 0) || (requested < 0 && pending > 0))
		pending = 0;

	int64_t total = pending + (int64_t)requested;
	int32_t out;
	if (total > 32767)       out = 32767;
	else if (total < -32768) out = -32768;
	else                     out = (int32_t)total;

	int64_t rest = total - out;
	if (rest > kOverflowCarryMax)  rest = kOverflowCarryMax;
	if (rest < -kOverflowCarryMax) rest = -kOverflowCarryMax;
	*carry = (int32_t)rest;
	return out;
}

NTSTATUS XInputdReadStateHook(DWORD dwDeviceContext, PDWORD pdwPacketNumber, PXINPUT_GAMEPAD pInputData, PBOOL unk) {
	// Meme trace unique que les deux autres hooks XInput - voir le commentaire
	// sur XamInputSetStateHook. C'est le hook appele le plus souvent (a chaque
	// lecture de manette), donc celui qui confirme le mieux que le chemin
	// d'entree est vivant juste avant un gel.
	static bool loggedFirstCall = false;
	if (!loggedFirstCall) {
		loggedFirstCall = true;
		FileLog("HOOK: first call XInputdReadState (context=%08x)", (unsigned int)dwDeviceContext);
	}
	if (dwDeviceContext >= 0x0000000010000005) {
		if (!pInputData)
			return ERROR_INVALID_PARAMETER;

		static DWORD lastPressTime = 0;
		static const DWORD cooldownDuration = 1000;

		// Several devices can share one deviceContext (keyboard + mouse fused
		// into a single player), so gather ALL of them and merge their states
		// instead of stopping at the first match. With one device this behaves
		// exactly as before.
		ButtonsReport b;
		memset(&b, 0, sizeof(ButtonsReport));
		Controller* c = nullptr;
		Controller* mouseCtrl = nullptr;
		for (int i = 0; i < (sizeof(connectedControllers) / sizeof(Controller)); i++) {
			if (connectedControllers[i].controllerDriver &&
				connectedControllers[i].deviceContext == dwDeviceContext) {
				if (!c)
					c = &connectedControllers[i];   // primary: owns the packet counter
				MergeButtonsReport(&b, &connectedControllers[i].currentState);
				if (connectedControllers[i].isMouse)
					mouseCtrl = &connectedControllers[i];
				Controller& k = connectedControllers[i];
				const HidDeviceMapping* km = k.map;
				if (km && km->macroCount && !k.isMouse && !k.isKeyboard) {
					const DWORD now = GetTickCount();
					for (uint8_t m = 0; m < km->macroCount && m < MAX_MACROS; m++)
						ApplyMacroMask(&b, MacroAdvance(km->macros[m], k.macroPlay[m], now, (k.currentState.macroHeld >> m) & 1));
				}
			}
		}

		if (!c)
			return ERROR_INVALID_PARAMETER;

		// X360Remap paused (Win + Shift + X): player 1 = the real controller.
		if (g_remapPaused) {
			memset(pInputData, 0, sizeof(XINPUT_GAMEPAD));
			if (g_realPadTick != 0 && (GetTickCount() - g_realPadTick) < 500) {
				MemoryBarrier();
				*pInputData = g_realPad;
			}
			if (pdwPacketNumber) *pdwPacketNumber = ++c->packetNumber;
			if (unk) *unk = FALSE;
			return STATUS_SUCCESS;
		}

		// Merged players, the real controller is player 1: this keyboard /
		// mouse slot hands its input to the pad (read from the real-controller
		// branch below, g_mergeInternalRead). The game gets a resting pad here,
		// so the mouse movement is not used up by this read.
		const bool kbmSlot = c->isMouse || c->isKeyboard;
		// Only while the pad really receives it (g_mergeDeliveredTick): if the
		// game stops reading the pad, this slot works normally again.
		// Separate players: the keyboard / mouse took player 1 from the pad, so
		// this slot plays that pad instead (g_displacedPad) - a swap, not a merge.
		if (!g_mergeInternalRead && kbmSlot && c->userIndex != 0 &&
			g_mergeDeliveredTick != 0 && (GetTickCount() - g_mergeDeliveredTick) < 500) {
			if (g_playersSeparate) {
				static XINPUT_GAMEPAD s_lastShown;
				MemoryBarrier();
				*pInputData = g_displacedPad;
				if (memcmp(&s_lastShown, pInputData, sizeof(XINPUT_GAMEPAD)) != 0) {
					s_lastShown = *pInputData;
					++c->packetNumber;   // a new packet only when the pad changes, like a real pad
				}
			} else {
				memset(pInputData, 0, sizeof(XINPUT_GAMEPAD));
			}
			if (pdwPacketNumber) *pdwPacketNumber = c->packetNumber;
			if (unk) *unk = FALSE;
			return STATUS_SUCCESS;
		}

		// Mapping de la souris lu UNE SEULE FOIS pour tout ce poll
		// (AUDIT_2026-09.md, C4) : il etait relu jusqu'a sept fois plus bas,
		// alors que le thread de polling peut le remettre a nul entre deux
		// lectures lors d'un rechargement de la configuration.
		const HidDeviceMapping* mouseMap = mouseCtrl ? mouseCtrl->map : nullptr;

		// Detection "thread de polling ne repond plus", depuis le thread du
		// JEU (2026-08-03). Complete le __except de MappingManagerThreadProc :
		// celui-ci ne rattrape qu'un PLANTAGE, pas un BLOCAGE. Si le thread se
		// fige, plus aucun heartbeat n'est ecrit et FlushLogBuffer - qui vit
		// dans ce meme thread - ne tourne plus, donc le fichier de log ne peut
		// plus rien nous apprendre. Ici on constate le gel depuis l'exterieur,
		// on logue la phase exacte ou il s'est arrete, et on force nous-memes
		// l'ecriture disque.
		//
		// Ecriture fichier depuis le thread du jeu : contraire a la regle
		// habituelle de ce fichier, assume et strictement borne - UNE SEULE
		// fois par demarrage (g_pollStallReported), et uniquement dans un
		// scenario ou le plugin est de toute facon deja hors service. Le
		// diagnostic vaut ce risque, sans quoi ce mode de panne reste
		// invisible et on continue a tourner en rond.
		{
			static bool g_pollStallReported = false;
			if (!g_pollStallReported && g_pollHeartbeatMs != 0 &&
				(GetTickCount() - g_pollHeartbeatMs) > 10000) {
				g_pollStallReported = true;
				FileLog("POLLING THREAD STUCK for %lums - last phase=%d (hot reload, F9 and quick bind are OUT OF SERVICE)",
					(unsigned long)(GetTickCount() - g_pollHeartbeatMs), (int)g_pollPhase);
				FlushLogBuffer();
			}
		}

		// DIAG clic souris (2026-08-03) - "j'ai choisi bouton A pour le clic
		// gauche mais en jeu ca ne marche pas". Relecture complete du chemin
		// (HidFillBootMouseState -> currentState -> MergeButtonsReport -> ici)
		// sans anomalie trouvee : il faut donc les valeurs reelles. Cette
		// unique ligne tranche tout l'arbre de decision d'un coup :
		//   mask toujours 00        -> les clics ne remontent pas de l'USB
		//   map=NO                  -> aucun mapping attache a cette souris
		//   nEntries=0              -> la config n'arrive pas jusqu'au plugin
		//   entries montre 0=a_button mais A=0 -> bug dans HidFillBootMouseState
		//   A=1                     -> le plugin fait son travail, le probleme
		//                              est en aval (jeu/XInput)
		// Ne se declenche qu'au CHANGEMENT du masque de boutons (pas a chaque
		// poll) - une souris envoie ses rapports a plusieurs centaines de Hz,
		// loguer inconditionnellement noierait le fichier et fausserait la
		// mesure. A retirer une fois le probleme identifie.
		if (mouseCtrl) {
			static uint8_t s_lastDiagMask = 0xFF;
			uint8_t diagMask = mouseCtrl->lastMouseButtonsMask;
			if (diagMask != s_lastDiagMask) {
				s_lastDiagMask = diagMask;

				HidButtonMapEntry diagMap[MAX_MERGED_BUTTONS];
				uint8_t diagCount = ResolveMergedButtonMap(mouseMap, g_lastTitleId, diagMap, MAX_MERGED_BUTTONS);

				char entries[160];
				entries[0] = '\0';
				int pos = 0;
				for (uint8_t i = 0; i < diagCount && pos < (int)sizeof(entries) - 24; i++) {
					const char* fname = GetButtonFieldName(diagMap[i].field);
					int n = _snprintf(entries + pos, sizeof(entries) - pos - 1, "%d=%hs ",
						(int)diagMap[i].idx, fname ? fname : "?");
					if (n <= 0) break;
					pos += n;
				}
				entries[sizeof(entries) - 1] = '\0';

				// Origine de la table effective : profil du jeu en cours, ou
				// reglage device. Decisif pour la liaison rapide, qui ecrit
				// TOUJOURS dans le profil - si la table affichee vient du
				// device alors qu'un profil existe, la liaison est enregistree
				// mais jamais appliquee, ce qui correspond exactement au
				// symptome "la notification dit que c'est bon mais en jeu rien
				// ne change" (retour utilisateur 2026-08-03).
				const HidDeviceProfile* diagProfile = FindActiveProfile(mouseMap, g_lastTitleId);
				const char* diagSrc = (diagProfile && diagProfile->buttonMapCount > 0) ? "PROFILE" : "device";

				// Toutes les sorties, plus seulement A/B/RT/LT/R3 : la version
				// precedente n'affichait pas LB/RB ni le D-Pad, donc une
				// liaison vers LB etait invisible dans le diagnostic - angle
				// mort signale par l'utilisateur.
				// rx/ry affiches en plus des bits : la conversion finale est
				// `bLeftTrigger = b.rx ? b.rx : (b.l2 ? 255 : 0)`. Si un
				// peripherique fusionne positionne l'axe analogique rx/ry, il
				// prend le pas sur le clic et ecrase 255 par une valeur
				// tronquee vers un BYTE - gachette molle ou inerte alors que
				// l2/r2 valent bien 1. Seule facon de distinguer ce cas de
				// "la source liee n'est pas le bouton que je croyais".
				FileLog("DIAG click: mask=%02x map=%hs title=%08x n=%d src=%hs [%hs] -> A=%d B=%d X=%d Y=%d LB=%d RB=%d L3=%d R3=%d LT=%d RT=%d DU=%d DD=%d DL=%d DR=%d rx=%d ry=%d",
					(unsigned int)diagMask, mouseMap ? "YES" : "NO",
					(unsigned int)g_lastTitleId, (int)diagCount, diagSrc, entries,
					(int)b.a_button, (int)b.b_button, (int)b.x_button, (int)b.y_button,
					(int)b.l1, (int)b.r1, (int)b.l3, (int)b.r3, (int)b.l2, (int)b.r2,
					(int)b.dpad_up, (int)b.dpad_down, (int)b.dpad_left, (int)b.dpad_right,
					(int)b.rx, (int)b.ry);
			}
		}

		if (b.xbox) {
			DWORD now = GetTickCount();
			if (now - lastPressTime >= cooldownDuration) {
				lastPressTime = now;
				// Never call XAM from inside XInputdReadState (2026-10-30,
				// Jacckii's rule: his Guide press deadlocked the console the
				// same way). The polling thread sends it (see g_guideRequest).
				g_guideRequest = (LONG)c->userIndex + 1;
			}
		}

		if (b.a_button)    pInputData->wButtons |= XINPUT_GAMEPAD_A;
		if (b.b_button)   pInputData->wButtons |= XINPUT_GAMEPAD_B;
		if (b.y_button) pInputData->wButtons |= XINPUT_GAMEPAD_Y;
		if (b.x_button)   pInputData->wButtons |= XINPUT_GAMEPAD_X;
		if (b.start)    pInputData->wButtons |= XINPUT_GAMEPAD_START;
		if (b.back)     pInputData->wButtons |= XINPUT_GAMEPAD_BACK;
		if (b.r3)       pInputData->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
		if (b.l3)       pInputData->wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (b.l1)       pInputData->wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
		if (b.r1)       pInputData->wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

		if (b.has_hat_switch) {
			switch (b.hatSwitch) {
			case HatSwitch::HAT_UP:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
				break;
			case HatSwitch::HAT_UP_RIGHT:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT;
				break;
			case HatSwitch::HAT_RIGHT:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
				break;
			case HatSwitch::HAT_DOWN_RIGHT:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT;
				break;
			case HatSwitch::HAT_DOWN:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
				break;
			case HatSwitch::HAT_DOWN_LEFT:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT;
				break;
			case HatSwitch::HAT_LEFT:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
				break;
			case HatSwitch::HAT_UP_LEFT:
				pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT;
				break;
			case HatSwitch::HAT_NEUTRAL:
				break;
			}
		}

		// Les bits dpad_* sont desormais appliques TOUJOURS, plus seulement
		// dans le "else" du hat switch ci-dessus (correction 2026-08-03).
		// Ancien comportement : des qu'UN periph fusionne envoyait un hat non
		// neutre, MergeButtonsReport mettait has_hat_switch=true sur le
		// rapport COMBINE - et tout le D-Pad venant du clavier (touches
		// flechees mappees dpad_*) ou de la molette etait alors ignore en
		// bloc. Concretement : avec une vraie manette branchee a cote (cas
		// normal ici, elle est requise pour la liaison rapide), appuyer sur
		// sa croix directionnelle faisait disparaitre le D-Pad clavier. Les
		// deux sources sont maintenant OR-ees, ce qui est la regle deja
		// appliquee a tous les autres boutons (voir MergeButtonsReport).
		if (b.dpad_left)
			pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
		if (b.dpad_right)
			pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
		if (b.dpad_up)
			pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
		if (b.dpad_down)
			pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
		
		pInputData->sThumbRX = b.z;
		pInputData->sThumbRY = b.rz;
		pInputData->sThumbLX = b.x;
		pInputData->sThumbLY = b.y;
		// Analog triggers: rx / ry hold -32768 (released) .. 32767 (full). The
		// old code kept only the low byte of that value, which is right for
		// 8-bit triggers only (DualShock 4) and random for 10- or 16-bit ones.
		// (v + 32768) >> 8 gives the same result for 8-bit triggers. A pressed
		// l2 / r2 (key, mouse button, digital trigger) now always means full,
		// even when a released analog trigger is merged in.
		pInputData->bLeftTrigger = b.rx ? (BYTE)(((int32_t)b.rx + 32768) >> 8) : 0;
		pInputData->bRightTrigger = b.ry ? (BYTE)(((int32_t)b.ry + 32768) >> 8) : 0;
		if (b.l2) pInputData->bLeftTrigger = 255;
		if (b.r2) pInputData->bRightTrigger = 255;

		// Use the mouse found above rather than assuming the primary device IS
		// the mouse - with keyboard+mouse fusion the primary may well be the
		// keyboard, and the mouse motion still has to be applied.
		if (mouseCtrl) {
			// Drain the accumulator here (once per XInput poll) rather than in the HID
			// interrupt handler, so motion is reported as a per-poll delta ("velocity"):
			// no new report since the last poll correctly reads back as centered (0,0)
			// instead of repeating a stale non-zero value.
			int32_t rawDx = mouseCtrl->mouseAccumX; mouseCtrl->mouseAccumX = 0;
			int32_t rawDy = mouseCtrl->mouseAccumY; mouseCtrl->mouseAccumY = 0;

#if EN_FIX_MOUSE_DPI_NORMALIZATION
			// --- Normalisation de la resolution (2026-09-23) ---------------
			// Retour HB : deux souris a sensibilite maximale, deux resultats.
			// Pour un meme geste de la main, une souris a 1600 DPI envoie deux
			// fois plus de points qu'une souris a 800 DPI - et aucune souris
			// ne communique son DPI par USB. Une souris MESUREE dans la Studio
			// (feuille A4, HidDeviceMapping::countsPerCm) est donc ramenee ici
			// a une reference commune de 800 DPI : la MEME sensibilite donne
			// alors le MEME ressenti avec n'importe quelle souris mesuree.
			// Souris jamais mesuree (countsPerCm == 0) : rien ne change.
			if (mouseMap && mouseMap->countsPerCm > 0.0f) {
				const float ratio = kReferenceCountsPerCm / mouseMap->countsPerCm;
				float fx = (float)rawDx * ratio + mouseCtrl->normFracX;
				float fy = (float)rawDy * ratio + mouseCtrl->normFracY;
				rawDx = (int32_t)fx;
				rawDy = (int32_t)fy;
				mouseCtrl->normFracX = fx - (float)rawDx;
				mouseCtrl->normFracY = fy - (float)rawDy;
			}
#endif

			// Inertie (2026-08-02, demande utilisateur apres avoir constate
			// qu'un tour de camera complet demandait plusieurs gestes de
			// souris) : rawDx/rawDy bruts retombent a 0 instantanement des que
			// la main s'arrete - un geste de souris ne peut durer que le temps
			// du mouvement physique reel, contrairement a un vrai stick qu'on
			// peut maintenir a fond en continu. Confirme par le log DIAG :
			// meme a sensibilite/deadzone bien reglees, on atteint deja le
			// maximum du stick de facon fiable pendant un swipe rapide - le
			// probleme n'etait pas la vitesse, mais la DUREE pendant laquelle
			// le stick reste a fond par geste physique.
			//
			// Lissage par decroissance exponentielle basee sur le temps reel
			// ecoule (pas sur le nombre de polls, dont la cadence peut varier
			// selon le jeu) : la vitesse precedente est attenuee par une
			// demi-vie de VELOCITY_HALF_LIFE_MS millisecondes, puis le nouveau
			// mouvement s'y ajoute. Un swipe rapide continue donc a faire
			// tourner la camera quelques dizaines de ms apres l'arret de la
			// main au lieu de s'arreter net - reduit le nombre de gestes
			// necessaires pour un tour complet. Compromis explicitement
			// choisi par l'utilisateur (vs reactivite instantanee) - prix a
			// payer : un leger flou/derive juste apres avoir arrete de bouger,
			// surtout genant pour de la visee fine. NON CONFIRME SUR HARDWARE,
			// demi-vie choisie au juge - a ajuster selon le ressenti reel (plus
			// haut = plus d'inertie/de derive, plus bas = plus proche du
			// comportement instantane d'avant ce changement).
			// --- INTERRUPTEUR DE DIAGNOSTIC (2026-09-14) ---
			//
			// HYPOTHESE A TESTER : c'est cette inertie qui provoque le retard
			// signale par un testeur ("the sens is so bad and there's so much
			// delay... i got 0.500Sec delay in mouse", sensibilite au maximum,
			// zone morte a 0, clavier parfaitement reactif chez lui).
			//
			// CE QUI EST CERTAIN, par simple arithmetique sur le code
			// ci-dessous : une demi-vie de 80 ms met 350 a 530 ms a devenir
			// imperceptible (5 a 7 demi-vies). Et comme le filtre ADDITIONNE
			// au lieu de moyenner, un mouvement soutenu converge vers environ
			// huit fois le delta brut a 60 polls/s - le reglage de sensibilite
			// se combine donc a un multiplicateur cache, et sature le stick
			// bien avant son maximum.
			//
			// CE QUI N'EST PAS PROUVE : que ce soit TOUTE l'explication de ce
			// qu'il ressent. Aucune mesure ne vient de sa console, et
			// l'acceleration de camera propre a chaque jeu varie enormement -
			// un titre peut masquer le defaut, un autre l'amplifier.
			//
			// PROTOCOLE : mettre a 1, recompiler, comparer le ressenti souris
			// sur le MEME jeu, avec les MEMES reglages. Retard disparu ->
			// cause confirmee. Retard identique -> chercher ailleurs, et ce
			// resultat negatif vaut autant que l'autre.
// Pilote par EN_TEST_CARRY_REPLACES_INERTIA (voir ApplyOverflowCarry) : ne
// plus changer cette valeur seule - desactiver l'inertie sans compenser son
// gain est exactement ce qui a provoque la regression du 2026-09-23.
#define EN_TEST_DISABLE_MOUSE_INERTIA EN_TEST_CARRY_REPLACES_INERTIA

#if EN_TEST_DISABLE_MOUSE_INERTIA
			// Comportement d'avant le 2026-08-02 : la vitesse retombe a zero
			// des que la main s'arrete, aucun lissage, aucune amplification.
			int32_t dx = rawDx;
			int32_t dy = rawDy;
			mouseCtrl->lastVelocityUpdateMs = GetTickCount();
			mouseCtrl->smoothedVelX = 0.0f;
			mouseCtrl->smoothedVelY = 0.0f;
#else
			const float VELOCITY_HALF_LIFE_MS = 80.0f;
			DWORD nowMs = GetTickCount();
			DWORD elapsedMs = nowMs - mouseCtrl->lastVelocityUpdateMs;
			if (elapsedMs > 200) elapsedMs = 200; // garde-fou si un tres long ecart survient (ex: premier poll)
			mouseCtrl->lastVelocityUpdateMs = nowMs;

			float decayFactor = powf(0.5f, (float)elapsedMs / VELOCITY_HALF_LIFE_MS);
			mouseCtrl->smoothedVelX = mouseCtrl->smoothedVelX * decayFactor + (float)rawDx;
			mouseCtrl->smoothedVelY = mouseCtrl->smoothedVelY * decayFactor + (float)rawDy;

			int32_t dx = (int32_t)mouseCtrl->smoothedVelX;
			int32_t dy = (int32_t)mouseCtrl->smoothedVelY;
#endif

			// Confirmed working on real hardware (Gears of War 3, smooth camera
			// movement) - applies automatically to any mouse, no JSON entry
			// needed. Override per-device via "mouseSensitivity" in
			// X360Remap.json (keyed by vid/pid) only if a specific mouse's DPI
			// needs something different.
			const int32_t DEFAULT_MOUSE_SENSITIVITY = 10000;
			// Jalon 7 (2026-08-02, application/ROADMAP_APPLICATION.md) : les
			// reglages effectifs (sensibilite/invertY/deadzone/courbe)
			// tiennent maintenant compte d'un eventuel profil par jeu
			// (HidDeviceProfile), via ResolveEffectiveMouseSettings
			// (X360Remap/mapping.cpp - logique testee en g++, voir
			// X360Remap/tests/test_mapping_assistant.cpp). g_lastTitleId est
			// deja tenu a jour plus haut dans cette meme fonction (voir
			// "Automatic title-switch rebind" ci-dessus) - 0 hors d'un jeu
			// (dashboard), auquel cas FindActiveProfile ne trouve jamais de
			// profil et les reglages du device s'appliquent tels quels,
			// comportement identique a avant ce jalon.
			//
			// Read the config from the MOUSE, not from the primary device of the
			// fused pair - with keyboard+mouse fusion the primary is often the
			// keyboard (whichever was plugged in first), which has no mouse
			// settings at all.
			EffectiveMouseSettings effective = ResolveEffectiveMouseSettings(mouseMap, g_lastTitleId);
			ApplyLiveOverrides(effective, g_lastTitleId);   // reglages en jeu F5..F11 (2026-09-25)
			// Lissage temporel (2026-09-26) : moyenne des dernieres lectures,
			// avant la mise a l'echelle. 0 = sans effet.
			// Visee directe Call of Duty (voir MouseHookCod) : si la version du
			// jeu est reconnue et qu'on est en partie, le mouvement va dans la
			// camera et plus du tout dans le stick.
			// v1.2.1: learned cameras (camera_addresses.txt) when no built-in
			// build matches - and the candidate being tried takes priority
			// while the player confirms it. Nothing at all while the camera
			// search drives the stick.
			// Direct aim input (2026-10-13): its own smoothing, and a slower speed
			// while aiming down sights (LT held - already decided above in
			// pInputData). Only the direct aim path uses these.
			int32_t aimDx = rawDx, aimDy = rawDy;
			SmoothMouseDelta(&mouseCtrl->smoothAim, GetTickCount(), &aimDx, &aimDy, effective.directAimSmoothingMs);
			int32_t aimSens = (effective.mouseSensitivity != 0) ? effective.mouseSensitivity : DEFAULT_MOUSE_SENSITIVITY;
			// ADS ramp (2026-10-13, tester with years of XIM: "the speed drops
			// instantly when I press right click, my aim feels stuck"): the
			// factor moves from 1 (hip) to adsSensitivity % over adsRampMs, and
			// back the same way when the button is released.
			{
				const float target = (effective.adsSensitivity > 0 && pInputData->bLeftTrigger > 100)
					? (float)effective.adsSensitivity / 100.0f : 1.0f;
				const DWORD nowMs = GetTickCount();
				if (mouseCtrl->adsFactor <= 0.0f) mouseCtrl->adsFactor = 1.0f;   // first use
				DWORD dt = nowMs - mouseCtrl->adsLastMs;
				if (mouseCtrl->adsLastMs == 0 || dt > 1000) dt = 1000;
				mouseCtrl->adsLastMs = nowMs;
				if (effective.adsRampMs <= 0) {
					mouseCtrl->adsFactor = target;
				} else {
					const float full = (float)effective.adsSensitivity / 100.0f;   // whole hip <-> ADS distance
					float span = full > 1.0f ? full - 1.0f : 1.0f - full;
					if (span < 0.01f) span = 0.01f;
					const float step = span * (float)dt / (float)effective.adsRampMs;
					if (mouseCtrl->adsFactor < target) {
						mouseCtrl->adsFactor += step;
						if (mouseCtrl->adsFactor > target) mouseCtrl->adsFactor = target;
					} else if (mouseCtrl->adsFactor > target) {
						mouseCtrl->adsFactor -= step;
						if (mouseCtrl->adsFactor < target) mouseCtrl->adsFactor = target;
					}
				}
				aimSens = (int32_t)((float)aimSens * mouseCtrl->adsFactor);
			}
			if (aimSens < 1) aimSens = 1;
			int hooked = 0;
#if EN_CAMERA_LEARN
			if (!g_learnInject) {
				if (g_learnTestActive)
					hooked = MouseHookLearned(g_lastTitleId, rawDx, rawDy, effective.mouseSensitivity, effective.invertMouseY);
				if (hooked == 0 && !g_directAimOff && MouseHookCod(g_lastTitleId, aimDx, aimDy, aimSens, effective.invertMouseY))
					hooked = 1;
				if (hooked == 0 && !g_directAimOff)
					hooked = MouseHookLearned(g_lastTitleId, aimDx, aimDy, aimSens, effective.invertMouseY);
			}
#else
			if (!g_directAimOff && MouseHookCod(g_lastTitleId, aimDx, aimDy, aimSens, effective.invertMouseY))
				hooked = 1;
#endif
			if (hooked == 1) {
				dx = 0; dy = 0;
				mouseCtrl->carryX = 0; mouseCtrl->carryY = 0;
			} else if (hooked == 2) {          // learned camera without pitch: vertical stays on the stick
				dx = 0;
				mouseCtrl->carryX = 0;
			}
			// GIMX method (1.4.2, docs/PLAN_1.4.2_GIMX.md): computed from the raw motion, before
			// the old chain smooths it; written over the stick at the end of that chain.
			int32_t gimxX = 0, gimxY = 0;
			if (effective.gimxOn) {
				const DWORD gNow = GetTickCount();
				const DWORD gDt = mouseCtrl->gimxLastMs ? gNow - mouseCtrl->gimxLastMs : 16;
				mouseCtrl->gimxLastMs = gNow;
				GimxMouseToStick(effective, &mouseCtrl->gimx, dx, effective.invertMouseY ? dy : -dy, gDt, &gimxX, &gimxY);
			}
			SmoothMouseDelta(&mouseCtrl->smooth, GetTickCount(), &dx, &dy, effective.smoothingMs);
			int32_t sensitivity = (effective.mouseSensitivity != 0) ? effective.mouseSensitivity : DEFAULT_MOUSE_SENSITIVITY;
			bool invertY = effective.invertMouseY;

			// BUG REEL corrige le 2026-08-02 : dx/dy sont de petits deltas HID
			// bruts (single-digit a ~30 en usage normal, voir HidFillBootMouseState
			// - r->x/r->y sont des int8 signes). Multiplier directement par
			// "sensitivity" (echelle 1000..30000, voir le slider dans
			// application.xex) sature quasiment toujours sThumbRX/RY au maximum
			// (+-32767) des qu'un mouvement normal de souris survient : par
			// exemple dx=4 * sensitivity=10000 = 40000, deja au-dela du clamp.
			// Consequence concrete signalee par l'utilisateur : doubler la
			// sensibilite (10000 -> 20000) n'avait AUCUN effet perceptible en jeu,
			// puisque les deux valeurs saturaient deja pour a peu pres tout
			// mouvement reel. Diviseur ajoute pour ramener l'echelle a quelque
			// chose d'utilisable sur toute la plage du slider plutot que de
			// saturer des les premiers pixels de mouvement.
			//
			// RECALIBRE le 2026-08-02 (100 -> 60) a partir de vrais chiffres
			// reels obtenus via le log DIAG ci-dessous (X360Remap_plugin.log,
			// vraie partie de jeu) : avec 100, dx=1/2 (tres frequents lors
			// d'une visee fine) ne donnaient que 300/600 avant deadzone - deja
			// mange par la moindre deadzone non nulle - et la fenetre utile ou
			// le mouvement restait proportionnel (ni ecrete a fond, ni mange
			// par la deadzone) etait etroite (dx ~2 a ~110). Avec 60 : un
			// mouvement moyen (dx~20-50) est nettement plus punchy (10000-25000
			// au lieu de 6000-15000) sans changer fondamentalement le plafond
			// (un swipe rapide, dx>65 environ a sensibilite max, sature toujours
			// - c'est le plafond XInput lui-meme, inevitable, voir discussion
			// avec l'utilisateur). NON CONFIRME SUR HARDWARE - a retester avec
			// la deadzone remise a 0 pour isoler l'effet de ce changement seul.
#if EN_TEST_CARRY_REPLACES_INERTIA
			// 60 / 7,4 ~= 8 : restitue le gain que l'inertie apportait a
			// 60 lectures/s (voir EN_TEST_CARRY_REPLACES_INERTIA).
			const int32_t SENSITIVITY_SCALE_DIVISOR = 8;
#else
			const int32_t SENSITIVITY_SCALE_DIVISOR = 60;
#endif

			int32_t scaledX = (dx * sensitivity) / SENSITIVITY_SCALE_DIVISOR;
			// HID mice report +Y as "down"; stick "up" is positive, hence the
			// negation - "invertMouseY" in the config flips that again on top.
			int32_t scaledY = ((invertY ? dy : -dy) * sensitivity) / SENSITIVITY_SCALE_DIVISOR;

#if EN_TEST_MOUSE_OVERFLOW_CARRY
			// Ce qui depasse le stick est reporte aux lectures suivantes au
			// lieu d'etre perdu - voir ApplyOverflowCarry.
			scaledX = ApplyOverflowCarry(scaledX, &mouseCtrl->carryX);
			scaledY = ApplyOverflowCarry(scaledY, &mouseCtrl->carryY);
#else
			if (scaledX > 32767) scaledX = 32767;
			if (scaledX < -32768) scaledX = -32768;
			if (scaledY > 32767) scaledY = 32767;
			if (scaledY < -32768) scaledY = -32768;
#endif

			// Courbe de sensibilite (Jalon 7) - appliquee APRES la mise a
			// l'echelle lineaire/clamp, AVANT la deadzone : agit sur la valeur
			// finale deja dans la plage -32768..32767, no-op si le device/le
			// profil actif n'a pas configure CURVE_EXPONENTIAL (comportement
			// inchange, exactement l'ancien pipeline lineaire). Voir
			// ApplySensitivityCurve (mapping.cpp) et EffectiveMouseSettings
			// ci-dessus pour la resolution device+profil. NON CONFIRME SUR
			// HARDWARE - premier jet, a tester des qu'un profil avec courbe
			// exponentielle est configure via l'appli.
			scaledX = ApplySensitivityCurve(scaledX, effective.sensitivityCurveType, effective.sensitivityCurveExponent);
			scaledY = ApplySensitivityCurve(scaledY, effective.sensitivityCurveType, effective.sensitivityCurveExponent);

			// Zone morte (deadzone) - schema/UI existaient deja (mapping.h,
			// application.xex) mais n'etaient JAMAIS appliques ici (aucune
			// reference a "deadzone" dans tout X360Remap/main.cpp avant ce fix -
			// le reglage etait sauvegarde dans X360Remap.json sans le moindre
			// effet en jeu). Applique par axe (pas de deadzone radiale
			// X+Y combinee) : un mouvement dont l'amplitude scaledX/scaledY reste
			// sous le seuil est traite comme "rien" plutot que comme un micro-
			// mouvement - utile pour filtrer le bruit du capteur d'une souris
			// tres sensible pres du seuil de detection, moins critique que sur
			// un vrai stick a ressort (une souris immobile donne deja dx=0
			// naturellement, pas de "drift" a corriger). deadzone vient
			// desormais de effective (device OU profil du jeu en cours, voir
			// Jalon 7 plus haut) plutot que directement de mouseMap.
			int32_t deadzone = effective.deadzone;
			if (deadzone > 0) {
				if (scaledX > -deadzone && scaledX < deadzone) scaledX = 0;
				if (scaledY > -deadzone && scaledY < deadzone) scaledY = 0;
			}

			// Anti-zone morte (2026-09-25, voir HidDeviceMapping::antiDeadzone) :
			// APRES notre propre zone morte (un bruit filtre reste a 0), juste
			// avant l'ecriture dans le stick. 0 = sans effet.
			ApplyAntiDeadzone(&scaledX, &scaledY, effective.antiDeadzone, effective.antiDeadzoneY);

			// La souris n'ecrase plus le stick droit quand ELLE NE BOUGE PAS
			// (AUDIT_2026-09.md, I6) : sinon IJKL (stick droit au clavier,
			// ecrit plus haut depuis b.z/b.rz) etait remis a 0 a chaque poll
			// des qu'une souris etait fusionnee - touches inertes. Souris en
			// mouvement : priorite a la souris, exactement comme avant.
			pInputData->sThumbRX = (scaledX != 0) ? (int16_t)scaledX : b.z;
			pInputData->sThumbRY = (scaledY != 0) ? (int16_t)scaledY : b.rz;
			if (effective.gimxOn) {
				pInputData->sThumbRX = (gimxX != 0) ? (int16_t)gimxX : b.z;
				pInputData->sThumbRY = (gimxY != 0) ? (int16_t)gimxY : b.rz;
			}

			// Scroll wheel - drained the same way as X/Y: whatever accumulated
			// since the last poll, then reset, so this naturally acts as a
			// brief pulse per notch rather than a held state.
			//
			// Par defaut : D-Pad Up (avant) / D-Pad Down (arriere). Choix fait
			// le 2026-07-31 - RB/RT (gachettes analogiques) faisaient scroller
			// Aurora trop vite ("fast scroll", plusieurs items d'un coup) car
			// une gachette a fond est lue comme un scroll rapide, alors que
			// D-Pad Up/Down est la navigation standard un-item-a-la-fois des
			// dashboards Xbox 360 dont Aurora.
			//
			// DESORMAIS CONFIGURABLE (2026-08-02, demande utilisateur) : idx 3
			// (avant) / idx 4 (arriere) dans le buttonMap de la souris, voir
			// FindWheelOverride plus haut - reste sur D-Pad par defaut tant
			// que rien n'est configure (comportement inchange pour qui ne
			// touche pas au nouveau reglage), mais quiconque assigne
			// explicitement la molette perd la navigation D-Pad automatique
			// en echange (compromis accepte, voir PROJECT_NOTES.md).
			int32_t wheel = mouseCtrl->mouseWheelAccum; mouseCtrl->mouseWheelAccum = 0;
			// Resolution profil-puis-device depuis le 2026-08-03 (voir
			// FindWheelOverride) - g_lastTitleId vaut 0 hors d'un jeu, auquel
			// cas seul le reglage device-level peut matcher, comme avant.
			uint8_t ButtonsReport::* wheelForward = FindWheelOverride(mouseMap, g_lastTitleId, 3);
			uint8_t ButtonsReport::* wheelBackward = FindWheelOverride(mouseMap, g_lastTitleId, 4);

			// Etirement de l'impulsion (2026-08-03) - voir Controller::
			// wheelPulseDir. Un cran de molette arme le sens capture pendant
			// WHEEL_PULSE_MS ms au lieu d'un unique poll, pour que le
			// dashboard/jeu ait le temps de l'echantillonner. Un nouveau cran
			// pendant une impulsion en cours la relance (et change de sens
			// immediatement si l'utilisateur inverse le sens de rotation).
			const DWORD WHEEL_PULSE_MS = 60;
			DWORD wheelNowMs = GetTickCount();
			if (wheel > 0) {
				mouseCtrl->wheelPulseDir = 1;
				mouseCtrl->wheelPulseUntilMs = wheelNowMs + WHEEL_PULSE_MS;
			} else if (wheel < 0) {
				mouseCtrl->wheelPulseDir = -1;
				mouseCtrl->wheelPulseUntilMs = wheelNowMs + WHEEL_PULSE_MS;
			} else if (mouseCtrl->wheelPulseDir != 0 && wheelNowMs >= mouseCtrl->wheelPulseUntilMs) {
				mouseCtrl->wheelPulseDir = 0; // impulsion expiree - relachement
			}

			// DIAG (2026-08-03, retour utilisateur - "la molette enregistre
			// mais ca ne passe pas") : aucune visibilite jusqu'ici sur ce que
			// cette section voit reellement - wheel accumule non nul ?
			// FindWheelOverride trouve-t-il une entree ? A retirer une fois le
			// probleme identifie, meme convention que "DIAG souris" pour X/Y
			// plus haut dans cette fonction.
			if (wheel != 0) {
				FileLog("DIAG wheel: accum=%d map=%hs fwd=%hs back=%hs",
					(int)wheel, mouseMap ? "YES" : "NO",
					wheelForward ? GetButtonFieldName(wheelForward) : "(none) -> D-Pad Up by default",
					wheelBackward ? GetButtonFieldName(wheelBackward) : "(none) -> D-Pad Down by default");
			}

			if (mouseCtrl->wheelPulseDir > 0) {
				if (wheelForward) ApplyButtonFieldToXInput(pInputData, wheelForward);
				else pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			} else if (mouseCtrl->wheelPulseDir < 0) {
				if (wheelBackward) ApplyButtonFieldToXInput(pInputData, wheelBackward);
				else pInputData->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			}
		}

#if EN_CAMERA_LEARN
		// Camera search (v1.2.1): the plugin drives the right stick itself.
		// Written BEFORE the stick layout below, so a Southpaw / Legacy
		// profile moves it to the stick the game really uses for the camera.
		if (g_learnInject) {
			pInputData->sThumbRX = (int16_t)g_learnRX;
			pInputData->sThumbRY = (int16_t)g_learnRY;
			g_learnHookTick = GetTickCount();
		}
#endif

		// Disposition des sticks du jeu (2026-09-25, voir StickLayout dans
		// mapping.h) : permutation EN DERNIER, une fois les deux sticks
		// entierement calcules (clavier, souris, anti-zone morte).
		if (mouseCtrl) {
			const HidDeviceProfile* sp = FindActiveProfile(mouseCtrl->map, g_lastTitleId);
			if (sp && sp->stickLayout > 0)
				ApplyStickLayout(&pInputData->sThumbLX, &pInputData->sThumbLY,
				                 &pInputData->sThumbRX, &pInputData->sThumbRY, sp->stickLayout);
		}

		if (g_stickReleaseRamp) {
			// One state per virtual controller (contexts 0x10000005 + index).
			struct RampAxis { int16_t from; DWORD since; bool ramping; int16_t last; };
			static RampAxis s_ramp[8][2];
			RampAxis* r = s_ramp[(dwDeviceContext - 0x10000005u) & 7u];
			const DWORD now = GetTickCount();
			int16_t* axis[2] = { &pInputData->sThumbLX, &pInputData->sThumbLY };
			for (int k = 0; k < 2; k++) {
				const int16_t target = *axis[k];
				if (target != 0) {
					r[k].ramping = false;
				} else if (r[k].last != 0 && !r[k].ramping) {
					r[k].ramping = true; r[k].from = r[k].last; r[k].since = now;   // key just released
				}
				if (r[k].ramping) {
					const DWORD t = now - r[k].since;
					if (t >= STICK_RAMP_MS) r[k].ramping = false;
					else *axis[k] = (int16_t)((int32_t)r[k].from * (int32_t)(STICK_RAMP_MS - t) / (int32_t)STICK_RAMP_MS);
				}
				r[k].last = target;
			}
		}

		// Merged players, this slot is player 1: the real controller's input
		// is added (the pad keeps working as player 1 with the mouse too).
		if (!g_playersSeparate && !g_mergeInternalRead && kbmSlot && c->userIndex == 0 &&
			g_realPadTick != 0 && (GetTickCount() - g_realPadTick) < 500) {
			MemoryBarrier();
			XINPUT_GAMEPAD real = g_realPad;
			MergeGamepad(pInputData, &real);
			g_padIntoKbmTick = GetTickCount();
		}

		// OUR USB pad is player 1 (2026-11-18, HB: the Xbox sign-in window only
		// listens to player 1, the keyboard / mouse got player 2 and was ignored).
		// Same as for a real controller (real-controller branch below): the keyboard
		// / mouse slot is read here. Separate: it REPLACES this pad (keyboard / mouse
		// = player 1) and that slot plays the pad. Merged: it is added to this pad
		// and that slot rests.
		if (!g_mergeInternalRead && !kbmSlot && c->userIndex == 0) {
			Controller* v = nullptr;
			for (int i = 0; i < (int)(sizeof(connectedControllers) / sizeof(Controller)); i++) {
				Controller& k = connectedControllers[i];
				if (k.controllerDriver && (k.isMouse || k.isKeyboard) &&
					k.deviceContext != dwDeviceContext && k.userIndex != 0) { v = &k; break; }
			}
			if (v) {
				XINPUT_GAMEPAD virt;
				memset(&virt, 0, sizeof(virt));
				DWORD pkt = 0;
				g_mergeInternalRead = true;
				const NTSTATUS st = XInputdReadStateHook(v->deviceContext, &pkt, &virt, nullptr);
				g_mergeInternalRead = false;
				if (st == STATUS_SUCCESS) {
					if (g_playersSeparate) {
						g_displacedPad = *pInputData;
						MemoryBarrier();
						*pInputData = virt;
					} else {
						MergeGamepad(pInputData, &virt);
					}
					g_mergeDeliveredTick = GetTickCount();
				}
			}
		}

		if (g_stickRestNoise && pInputData->sThumbLX == 0 && pInputData->sThumbLY == 0) {
			static int16_t s_rest = 300;
			s_rest = -s_rest;
			pInputData->sThumbLX = s_rest;
			pInputData->sThumbLY = 300;
		}

		if (pdwPacketNumber) {
			if (g_packetOnChange) {
				static XINPUT_GAMEPAD s_prev[8];
				XINPUT_GAMEPAD& prev = s_prev[(dwDeviceContext - 0x10000005u) & 7u];
				if (memcmp(&prev, pInputData, sizeof(XINPUT_GAMEPAD)) != 0) {
					prev = *pInputData;
					++c->packetNumber;
				}
				*pdwPacketNumber = c->packetNumber;
			} else {
				*pdwPacketNumber = ++c->packetNumber;
			}
		}
		if (unk)
			*unk = FALSE;

		if (g_inputTrace) {
			static int16_t s_lx = 0, s_ly = 0, s_rx = 0, s_ry = 0;
			static WORD s_btn = 0;
			static DWORD s_lastLine = 0, s_calls = 0;
			s_calls++;
			const DWORD now = GetTickCount();
			const bool changed = pInputData->sThumbLX != s_lx || pInputData->sThumbLY != s_ly ||
				pInputData->sThumbRX != s_rx || pInputData->sThumbRY != s_ry || pInputData->wButtons != s_btn;
			const bool held = pInputData->sThumbLX != 0 || pInputData->sThumbLY != 0;
			if (changed || (held && (now - s_lastLine) >= 1000)) {
				FileLog("TRACE ctx %08x pkt %lu LX %d LY %d RX %d RY %d btn %04x title %08x (%lu reads since last line)",
					(unsigned int)dwDeviceContext, pdwPacketNumber ? (unsigned long)*pdwPacketNumber : 0ul,
					(int)pInputData->sThumbLX, (int)pInputData->sThumbLY, (int)pInputData->sThumbRX,
					(int)pInputData->sThumbRY, (unsigned int)pInputData->wButtons, (unsigned int)g_lastTitleId,
					(unsigned long)s_calls);
				s_lx = pInputData->sThumbLX; s_ly = pInputData->sThumbLY;
				s_rx = pInputData->sThumbRX; s_ry = pInputData->sThumbRY; s_btn = pInputData->wButtons;
				s_lastLine = now; s_calls = 0;
			}
		}

		if (!g_mergeInternalRead) HudNotePad(pInputData);
		return STATUS_SUCCESS;
	}
	// Real controller (or any device that is not ours).
	NTSTATUS realStatus = XInputdReadStateDetour.GetOriginal<decltype(&XInputdReadStateHook)>()(dwDeviceContext, pdwPacketNumber, pInputData, unk);
#if EN_CAMERA_LEARN
	// Camera search (v1.2.1 v3, 2026-10-07): the stick is ALSO injected into
	// the real controller. If the keyboard/mouse ended up as player 2 (pad
	// plugged first), the game listens to the pad only - injecting into our
	// virtual controller alone then turned nothing, and every search failed.
	// The packet number is bumped so games that only react to a new packet
	// see the change; the bump only ever grows, so packet numbers stay
	// increasing for that controller.
	if (realStatus == 0 && pInputData) {        // cache for the Win+Shift+X switch
		g_realPad = *pInputData;
		MemoryBarrier();
		g_realPadTick = GetTickCount();
	}
	// The real controller is player 1 and the keyboard / mouse got another slot -
	// read that slot here (same code as when the game reads it). Merged: added to
	// the pad. Separate: it replaces the pad (keyboard / mouse = player 1) and its
	// own slot plays the pad (g_displacedPad). Only ONE pad: the lowest device
	// context seen in the last second.
	{
		static DWORD s_targetCtx = 0, s_targetTick = 0, s_mergeBump = 0;
		static XINPUT_GAMEPAD s_lastVirt;
		const DWORD now = GetTickCount();
		if (s_targetCtx == 0 || dwDeviceContext <= s_targetCtx || (now - s_targetTick) > 1000) {
			s_targetCtx = dwDeviceContext;
		}
		if (dwDeviceContext == s_targetCtx) s_targetTick = now;

		if (realStatus == 0 && pInputData && dwDeviceContext == s_targetCtx &&
			!g_remapPaused && !g_mergeInternalRead && !g_pollRealRead) {
			Controller* v = nullptr;
			for (int i = 0; i < (int)(sizeof(connectedControllers) / sizeof(Controller)); i++) {
				Controller& k = connectedControllers[i];
				if (k.controllerDriver && (k.isMouse || k.isKeyboard) &&
					k.deviceContext >= 0x10000005 && k.userIndex != 0) { v = &k; break; }
			}
			if (v) {
				XINPUT_GAMEPAD virt;
				memset(&virt, 0, sizeof(virt));
				DWORD pkt = 0;
				g_mergeInternalRead = true;
				const NTSTATUS st = XInputdReadStateHook(v->deviceContext, &pkt, &virt, nullptr);
				g_mergeInternalRead = false;
				if (st == STATUS_SUCCESS) {
					if (memcmp(&virt, &s_lastVirt, sizeof(virt)) != 0) { s_lastVirt = virt; s_mergeBump++; }
					if (g_playersSeparate) {
						g_displacedPad = *pInputData;
						MemoryBarrier();
						*pInputData = virt;
					} else {
						MergeGamepad(pInputData, &virt);
					}
					g_mergeDeliveredTick = now;
				}
			}
		}
		// A new packet number when the keyboard / mouse part changes, so
		// games that only react to new packets see it (only ever grows).
		if (realStatus == 0 && pdwPacketNumber && s_mergeBump)
			*pdwPacketNumber += s_mergeBump;
	}
	{
		static volatile uint32_t s_packetBump = 0;
		if (realStatus == 0 && pInputData && g_learnInject) {
			pInputData->sThumbRX = (int16_t)g_learnRX;
			pInputData->sThumbRY = (int16_t)g_learnRY;
			s_packetBump++;
			g_learnHookTick = GetTickCount();
		}
		if (realStatus == 0 && pdwPacketNumber && s_packetBump)
			*pdwPacketNumber += s_packetBump;
	}
#endif
	// Merged players, keyboard / mouse = player 1: the pad's input already
	// reaches player 1 (see g_padIntoKbmTick), so the pad's own slot rests.
	// g_realPad was cached above, before this. Not for our own reads.
	if (realStatus == 0 && pInputData && !g_playersSeparate && !g_remapPaused &&
		!g_pollRealRead && !g_mergeInternalRead &&
		g_padIntoKbmTick != 0 && (GetTickCount() - g_padIntoKbmTick) < 500) {
		memset(pInputData, 0, sizeof(XINPUT_GAMEPAD));
	}
	if (realStatus == 0 && pInputData && !g_pollRealRead && !g_mergeInternalRead) HudNotePad(pInputData);
	return realStatus;
}


void* XamInputSetState = nullptr;
void* XamInputGetCapabilitiesEx = nullptr;
void* XInputdReadStatePtr = nullptr;
uint16_t* XNotifyTimerPtr = nullptr;
bool isDevkit = true;
DWORD UsbPhysicalPage = 0;
void* NotificationPatchPtr = nullptr;
bool initFunctionPointers() {
	isDevkit = *(uint32_t*)(0x8010D334) == 0x00000000;
	HANDLE kernelHandle = GetModuleHandleA("xboxkrnl.exe");

	if (!kernelHandle) {
		SafeDbgPrint("X360Remap: COULDNT GET KERNEL HANDLE!\n");
		return false;
	}

	HANDLE xamHandle = GetModuleHandleA("xam.xex");

	XexGetProcedureAddress(kernelHandle, 759, &UsbdGetDeviceDescriptor);
	XexGetProcedureAddress(kernelHandle, 744, &UsbdGetEndpointDescriptor);
	XexGetProcedureAddress(kernelHandle, 740, &UsbdAddDeviceComplete);
	XexGetProcedureAddress(kernelHandle, 746, &UsbdOpenDefaultEndpoint);
	XexGetProcedureAddress(kernelHandle, 747, &UsbdOpenEndpoint);
	XexGetProcedureAddress(kernelHandle, 742, &UsbdGetDeviceSpeed);
	XexGetProcedureAddress(kernelHandle, 748, &UsbdQueueAsyncTransfer);
	XexGetProcedureAddress(kernelHandle, 750, &UsbdQueueCloseEndpoint);
	XexGetProcedureAddress(kernelHandle, 749, &UsbdQueueCloseDefaultEndpoint);
	XexGetProcedureAddress(kernelHandle, 751, &UsbdRemoveDeviceComplete);
	XexGetProcedureAddress(kernelHandle, 189, &MmFreePhysicalMemory);
	XexGetProcedureAddress(kernelHandle, 486, &XInputdReadStatePtr);

	XexGetProcedureAddress(xamHandle, 685, &XamInputGetCapabilitiesEx);
	XexGetProcedureAddress(xamHandle, 402, &XamInputSetState);
	XexGetProcedureAddress(xamHandle, 1183, &NotificationPatchPtr);

	if (isDevkit) {
		SafeDbgPrint("X360Remap: Running in devkit mode\n");
		FileLog("Running in devkit mode");
		UsbdGetInterfaceDescriptor = (usb_interface_descriptor_func_t)0x8010D2D0; // 89 43 ? ? 3D 60 ? ? 89 2D ? ? 39 6B ? ? 55 4A FF 3A 2B 09 ? ? 7D 6A 58 2E ? ? ? ? ? ? ? ? 89 4D ? ? 2B 0A ? ? ? ? ? ? ? ? ? ? 81 4B ? ? 7F 03 50 40 ? ? ? ? ? ? ? ? A1 4B very bad direct signature. XREF sig: 89 63 ? ? 38 A1
		XamUserBindDeviceCallback = (xam_user_bind_device_callback_func_t)0x817A34B8; // 7C 8B 23 78 7C A4 2B 78 54 CA 06 3F
		UsbdPowerDownNotification = (usbd_powerdown_notification_func_t)0x8010E140; // argument to last function call in UsbdDriverEntry
		UsbdDriverEntry = (usbd_powerdown_notification_func_t)0x8010DE48; // 7D 88 02 A6 ? ? ? ? 94 21 ? ? 3C 80 ? ? 38 A0 

		// Remove two usb related bugchecks to allow reinitialisation of the usb driver
		*(DWORD*)0x80116298 = 0x48000018;
		*(DWORD*)0x801132A4 = 0x48000018;

		// DEVKIT only: Remove assertions(Microsoft did not think that we'd come and reset the usb driver, never let them know your next move typa shit)
		/*
		*(DWORD*)0x80096B84 = 0x60000000;
		*(DWORD*)0x80095F6C = 0x60000000;
		*(DWORD*)0x80116584 = 0x60000000;
		*(DWORD*)0x80116598 = 0x60000000;
		*/

		// Prevent double registration of Usbd handlers because the console wont shutdown cleanly otherwise
		* (DWORD*)0x8010E04C = 0x60000000;
		*(DWORD*)0x8010E05C = 0x60000000;
		UsbPhysicalPage = 0x8020A9B8;

		*(uint16_t*)0x8176A7C6 = 80; // Register custom notification type condition
		XNotifyTimerPtr = (uint16_t*)0x8176a7ca;
	}
	else {
		SafeDbgPrint("X360Remap: Running in retail mode\n");
		FileLog("Running in retail mode");
		UsbdGetInterfaceDescriptor = (usb_interface_descriptor_func_t)0x800D8500; // 89 43 ? ? 3D 60 ? ? 39 6B ? ? 55 4A FF 3A 7D 6A 58 2E A1 4B
		XamUserBindDeviceCallback = (xam_user_bind_device_callback_func_t)0x816D9060; // 7C 8B 23 78 7C A4 2B 78 54 CA 06 3F
		UsbdPowerDownNotification = (usbd_powerdown_notification_func_t)0x800D8FC8; // argument to last function call in UsbdDriverEntry
		UsbdDriverEntry = (usbd_powerdown_notification_func_t)0x800D8D08; // 7D 88 02 A6 ? ? ? ? 94 21 ? ? 3C 80 ? ? 38 A0 

		// Remove two usb related bugchecks to allow reinitialisation of the usb driver
		*(DWORD*)0x800E05E4 = 0x48000018;
		*(DWORD*)0x800DD8E0 = 0x48000018;

		// Prevent double registration of Usbd handlers because the console wont shutdown cleanly otherwise
		*(DWORD*)0x800D8F00 = 0x60000000;
		*(DWORD*)0x800D8EF0 = 0x60000000;
		UsbPhysicalPage = 0x801A8098;

		*(uint16_t*)0x816AB7A6 = 80; // Register custom notification type condition
		XNotifyTimerPtr = (uint16_t*)0x816ab7aa;
	}

	*XNotifyTimerPtr = 1500;

	// Patches notification handling to work without JRPC2, Thanks crow!
	//
	// VERIFIE ET TRACE depuis le 2026-08-22 (voir g_notifyUiSafe tout en haut
	// de ce fichier) : ce patch etait applique "au mieux" et son echec passait
	// totalement inapercu - on continuait ensuite a envoyer des notifications
	// de type personnalise a un xam.xex qui ne savait pas les traiter. C'est
	// le suspect principal du gel au branchement d'un peripherique lorsque
	// jrpc2.xex n'est pas charge.
	if (NotificationPatchPtr == nullptr) {
		g_notifyUiSafe = false;
		FileLog("xam!1183 not found - notifications OFF for safety");
	} else {
		short* patchSite = (short*)((uintptr_t)(NotificationPatchPtr) + 48);
		if (*patchSite == 0x409A) {
			*patchSite = 0x4800;
			g_notifyUiSafe = true;
			FileLog("Notification patch applied (409A -> 4800) - notifications on");
		} else if (*patchSite == 0x4800) {
			// Deja patche : soit par nous lors d'un demarrage precedent (les
			// patchs memoire survivent tant que la console n'est pas eteinte),
			// soit par jrpc2.xex. Dans les deux cas le systeme est pret.
			g_notifyUiSafe = true;
			FileLog("Notification patch already in place - notifications on");
		} else {
			// Cas non prevu : on ignore dans quel etat se trouve xam. On
			// n'ecrit RIEN (patcher a l'aveugle serait pire) et on coupe les
			// notifications - c'est precisement ce scenario qui gelait la
			// console au branchement d'une souris ou d'un clavier.
			g_notifyUiSafe = false;
			FileLog("Notification patch NOT applicable (bytes=%04x) - notifications OFF for safety",
				(unsigned int)(unsigned short)*patchSite);
		}
	}

	return true;
}

BOOL APIENTRY DllMain(HANDLE Handle, DWORD Reason, PVOID Reserved) {
	if (Reason == DLL_PROCESS_ATTACH) {
		if ((XboxKrnlVersion->Build != 17559 && XboxKrnlVersion->Build != 17489) || IsTrayOpen()) {
			SafeDbgPrint("X360Remap: Only 17559 and 17489 dashboards are currently supported or the disk tray is open. Aborting launch...\n");
			return FALSE;
		}

		SafeDbgPrint("X360Remap: HELLO from xbox 360 HID controller driver version 0.6 beta\n");
		// Libelle mis a jour (AUDIT M5) : le journal s'annoncait encore sous le
		// nom et la version du projet d'origine.
		FileLog("--- X360Remap plugin v%hs (fork of hiddriver360) starting, dashboard build %d ---", X360REMAP_VERSION, XboxKrnlVersion->Build);
		// Controllers known in advance: last lookup of FindMapping.
		g_extraMappingLookup = FindKnownControllerMapping;
		FileLog("Known controllers list: %u controllers (SDL GameControllerDB)", kKnownControllerCount);
		// Log where the in-memory log buffer actually lives, so it can be read
		// over the network via XBDM (e.g. xecli: "rgh mem strings --addr <this>
		// --size 8192") EVEN WHEN THE CONSOLE IS FROZEN - XBDM runs in its own
		// context and often still answers after the console hangs. This is the
		// workaround for the fundamental blind spot of FlushLogBuffer(): lines
		// buffered in the last ~100ms before a hard freeze never reach the disk,
		// because the polling thread that writes them never runs again. Reading
		// the buffer directly recovers exactly those missing lines.
		// If the address is unknown/stale, the buffer can also be located by
		// searching for its formatted first line, which only exists in RAM (the
		// .rdata copy is the unformatted "%d" version):
		//   rgh mem search --ascii "beta starting, dashboard build 17559"
		FileLog("Log buffer at %p (size %d) - readable over XBDM while frozen",
			g_logBuffer, LOG_BUFFER_SIZE);

		// X360Remap.json/input_state.json vivent maintenant dans HDD:\X360RemapStudio\
		// plutot qu'a la racine du disque dur (2026-08-02, demande utilisateur).
		// X360Remap.xex tourne dans le process xam.xex du dashboard et a deja
		// "HDD:" mappe par le systeme (pas besoin de MountHdd(), contrairement a
		// application.xex - voir application/main.cpp) mais le sous-dossier lui-
		// meme doit exister avant le premier acces. CreateDirectoryA echoue avec
		// ERROR_ALREADY_EXISTS des le deuxieme lancement - normal, pas une erreur.
		//
		// REMONTE ICI le 2026-08-22 (avant initFunctionPointers) : le dossier
		// doit exister avant la toute premiere ecriture disque du journal, et
		// celle-ci a lieu desormais des le debut de l'initialisation (voir
		// BootStep juste en dessous). Ne depend d'aucun pointeur de fonction,
		// donc rien n'empeche de le faire en premier.
		// Resolution du support de stockage (2026-09-13, voir g_studioDrive) :
		// "HDD:" en premier, donc comportement inchange sur une console avec
		// disque dur. Les suivants ne sont atteints que si le disque dur est
		// absent (Slim 4 Go, console lancee depuis USB...). Validation par une
		// vraie creation de dossier : un prefixe peut exister sans etre
		// inscriptible.
		// Storage resolution moved to ResolveStudioDrive() (2026-10-29). Fallback
		// "c." (create a NEW folder on internal memory / USB) is no longer done
		// here: the hard drive is often not mounted yet at this point, and a
		// USB stick plugged at boot would win for the whole session. The
		// polling thread retries the hard drive for 60 s first.
		if (ResolveStudioDrive(STORAGE_PASS_BOOT)) {
			FileLog("Working folder: %s\\X360RemapStudio", g_studioDrive);
		} else {
			FileLog("Storage not ready at boot (tried HDD:, Hdd1:, existing folders on OnBoardMU:, INTUSB:, Usb0:, USB sticks Mass0-2) - retrying for 60 s, then X360R: as a last resort");
		}

		// --- Traçage pas-a-pas de l'initialisation (2026-08-22) ---
		//
		// POURQUOI : jusqu'ici le journal n'atteignait le disque QUE depuis le
		// thread de polling, demarre a la toute derniere ligne de DllMain. Si
		// l'initialisation mourait ou se bloquait avant cette ligne - ce qui est
		// exactement ce qu'on soupconne pour le gel signale par Cyper puis
		// reproduit par HB - le fichier journal restait VIDE, voire inexistant :
		// aucun moyen de savoir si le plugin n'avait pas demarre du tout, ou
		// s'il etait mort en cours de route, ni a quelle etape.
		//
		// BootStep() ecrit la ligne ET la pousse immediatement sur le disque.
		// Consequence : apres un gel, le journal s'arrete PILE a la derniere
		// etape franchie. L'etape suivante, absente, est la coupable.
		//
		// Sans risque a cet endroit : DllMain n'est pas un callback USB mais un
		// contexte de thread normal, ou l'acces disque est deja pratique juste
		// en dessous (LoadMappingsFromFile, LoadBootProtocolSetting...).
		BootStep("Step 1/7: starting, working folder ready");

		if (!initFunctionPointers())
			return FALSE;
		BootStep("Step 2/7: initFunctionPointers OK (pointers + xam memory patches)");

		SafeDbgPrint("X360Remap: Loading mappings!\r\n");
		// Reparation depuis la copie de secours AVANT la lecture (2026-09-23) :
		// si une sauvegarde a laisse l'original absent ou casse, la derniere
		// configuration valide est dans X360Remap.json.tmp.
		{
			char cfgPathRecover[128];
			StudioPath(cfgPathRecover, sizeof(cfgPathRecover), "X360Remap.json");
			if (RecoverConfigFromTmp(cfgPathRecover))
				FileLog("X360Remap.json rebuilt from its backup copy (X360Remap.json.tmp)");
		}
		if (!LoadMappingsFromStudioConfig()) {
			SafeDbgPrint("X360Remap: Failed to load mappings(JSON either doesn't exist yet or syntax error)!\r\n");
			FileLog("Failed to load %s\\X360RemapStudio\\X360Remap.json (doesn't exist yet, or syntax error)", g_studioDrive);

			// Distinction volontaire (2026-09-13) : LoadMappings echoue aussi
			// bien quand le fichier est ABSENT que quand il est PRESENT mais
			// mal forme. Les deux appellent des conseils opposes, donc on
			// regarde reellement s'il existe plutot que de supposer.
			//
			// Motivation concrete : sur la page des versions publiees, le
			// plugin est telecharge nettement plus souvent que l'application
			// (77 contre 67 au 2026-09-13). Une partie des utilisateurs
			// n'installe donc que la moitie du produit, constate que "ca ne
			// marche pas", et en conclut que le projet n'est pas fonctionnel -
			// alors que leur installation est simplement incomplete.
			char cfgPath[128];
			StudioPath(cfgPath, sizeof(cfgPath), "X360Remap.json");
			if (GetFileAttributesA(cfgPath) == INVALID_FILE_ATTRIBUTES) {
				g_installIncomplete = true;
			} else {
				g_configUnreadable = true;
			}
		} else {
			FileLog("Loaded %s\\X360RemapStudio\\X360Remap.json successfully", g_studioDrive);
		}
		BootStep("Step 3/7: X360Remap.json processed");

		// Lu une seule fois au demarrage, avant l'installation des hooks (donc
		// avant que le moindre device puisse etre enumere et recevoir - ou non
		// - le SET_PROTOCOL). Voir LoadBootProtocolSetting.
		LoadBootProtocolSetting();

		// Meme moment, meme raison : le miroir DbgPrint doit etre soit active
		// soit desactive AVANT que HidAddDeviceHook puisse tourner pour le
		// premier device (voir g_dbgPrintMirrorEnabled, suspect principal du
		// gel signale par Cyper le 2026-08-21).
		LoadDbgPrintMirrorSetting();

		// Apres initFunctionPointers() (qui a deja decide si le systeme de
		// notification est utilisable) : ce reglage explicite prime toujours.
		LoadNotificationSetting();
		LoadFreezeTestSettings();   // 2026-10-05: freeze A/B switches, see g_deferReportFree

#if EN_CAMERA_LEARN
		LoadLearnedCameras();   // v1.2.1: camera_addresses.txt (cameras learned with F12)
#endif

		// Meme principe : lu une seule fois au demarrage, pas a chaque tick du
		// thread de polling (voir lang.h/lang.cpp). Decide dans quelle langue
		// les notifications XNotifyUI de CE binaire s'affichent, a partir du
		// meme fichier que application.xex ecrit via SaveLanguagePref() -
		// aucune memoire partagee entre les deux .xex, seul ce fichier les
		// synchronise.
		char langPath[128];
		StudioPath(langPath, sizeof(langPath), "X360Remap_lang.txt");
		LoadPluginLanguageSetting(langPath);
		BootStep("Step 4/7: settings read (boot protocol, dbgprint, notifications, language)");

		if (isDevkit) {
			HidAddDeviceDetour = Detour((void*)0x8011AE38, (void*)HidAddDeviceHook); // 7D 88 02 A6 ? ? ? ? 94 21 ? ? 7C 7C 1B 78 ? ? ? ? 7C 7F 1B 79
			HidRemoveDeviceDetour = Detour((void*)0x8011ADF8, (void*)HidRemoveDeviceHook); // 81 63 ? ? 39 40 ? ? 39 20 ? ? 99 4B
		}
		else {
			HidAddDeviceDetour = Detour((void*)0x800E4D68, (void*)HidAddDeviceHook); // 7D 88 02 A6 ? ? ? ? 94 21 ? ? 7C 7B 1B 78 ? ? ? ? 7C 7F 1B 79
			HidRemoveDeviceDetour = Detour((void*)0x800E4D28, (void*)HidRemoveDeviceHook); // 81 63 ? ? 39 40 ? ? 39 20 ? ? 99 4B
		}

		BootStep("Step 5/7: USB detours built, installing");

		HidAddDeviceDetour.Install();
		HidRemoveDeviceDetour.Install();

		XamInputGetCapabilitiesDetour = Detour(XamInputGetCapabilitiesEx, (void*)XamInputGetCapabilitiesExHook);
		XamInputSetStateDetour = Detour(XamInputSetState, (void*)XamInputSetStateHook);
		XInputdReadStateDetour = Detour(XInputdReadStatePtr, (void*)XInputdReadStateHook);

		XamInputSetStateDetour.Install();
		XamInputGetCapabilitiesDetour.Install();
		XInputdReadStateDetour.Install();
		InstallVdSwapHook();
		InstallBlackBox();
		SafeDbgPrint("X360Remap: Hooks installed\n");
		FileLog("Hooks installed");
		// A partir d'ici, un peripherique branche peut declencher nos hooks :
		// c'est la premiere etape ou le monde exterieur peut nous appeler.
		BootStep("Step 6/7: all hooks installed (devices can now call us)");

		// --- Boot-time USB driver reset: RE-ENABLED (2026-07-31, révision 4) ---
		//
		// RESULT (2026-08-01): re-enabling this froze the boot again, even
		// though mice and keyboards now take the short synchronous path. That
		// settles a question we had been circling for a long time: the boot
		// freeze comes from the USB driver reset ITSELF when a HID device is
		// physically connected, NOT from how we subsequently handle the device.
		// Every fix aimed at our own handling was therefore aimed at the wrong
		// target. Left at 0 - the cost is one unplug/replug after boot, which
		// is exactly what UsbdSecPatch tells its own users to do.
		#define DO_BOOT_USB_RESET 0

#if DO_BOOT_USB_RESET
		SafeDbgPrint("X360Remap: Resetting USB driver so already-connected devices are picked up.\n");
		FileLog("Boot USB reset starting - devices connected at power-on should work without replug");
		g_bootUsbResetInProgress = true;
		g_bootBlackoutStartedAt = GetTickCount();

		UsbdPowerDownNotification();
		MmFreePhysicalMemory(0, *(DWORD*)UsbPhysicalPage);
		UsbdDriverEntry();

		FileLog("Boot USB reset done");
#else
		// --- Boot-time USB driver reset: DISABLED (2026-07-31) ---
		//
		// Upstream did this (UsbdPowerDownNotification + MmFreePhysicalMemory +
		// UsbdDriverEntry) purely as a convenience: it forces the system to
		// re-enumerate everything already plugged in, so gamepads connected
		// before the console was switched on work without a manual replug.
		//
		// On this setup it is also what freezes the console on the boot
		// animation whenever a HID device (mouse or keyboard) is physically
		// connected at power-on. Evidence it is the reset itself rather than our
		// handling of the device: blocking device claiming for the whole boot
		// window (g_bootUsbResetInProgress, 10s blackout) changed the freeze
		// not at all. It also matches the observation that wired Xbox 360
		// gamepads never trigger it - they are not HID-class (vendor-specific
		// 0xFF interface), so they take a completely different path through the
		// USB stack during this reset.
		//
		// Removing it costs nothing here: a device connected at boot simply
		// needs one unplug/replug once the dashboard is up, which is already the
		// established workflow (a mouse has always needed a replug after
		// launching a game anyway). Hot-plugging after boot is unaffected - it
		// goes through HidAddDeviceHook normally, which is confirmed working.
		//
		// To restore the old behaviour, re-enable the three calls below and
		// keep g_bootUsbResetInProgress set around them.
		SafeDbgPrint("X360Remap: Skipping boot USB driver reset (freeze workaround).\n");
		FileLog("Boot USB reset SKIPPED - replug devices once the dashboard is up");

		// Belt and braces: keep the claim blackout for the first few seconds
		// anyway. Without the reset, anything plugged in at power-on is normally
		// enumerated by the system before this plugin even loads (~4.6s in), so
		// our hook never sees it - but if enumeration happens to land just after
		// we install the hooks, we still do not want to claim a device that
		// early. Costs nothing: such a device just needs the same replug.
		g_bootUsbResetInProgress = true;
		g_bootBlackoutStartedAt = GetTickCount();
#endif // DO_BOOT_USB_RESET

		// Start mapping manager thread
		BootStep("Step 7/7: starting the polling thread");
		MakeThread((LPTHREAD_START_ROUTINE)MappingManagerThreadProc, nullptr);
		// Si cette ligne apparait dans le journal, l'initialisation complete a
		// abouti : tout gel survenant apres est ailleurs (thread de polling,
		// hooks USB), plus dans le demarrage du plugin.
		BootStep("Initialization done - plugin running");
	}
	return TRUE;
}
