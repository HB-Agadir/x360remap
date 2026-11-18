# X360Remap 1.4.1 Preview

> **This is a preview, not the final 1.4.** It is the first public 1.4 build and the most stable one so far, thanks to the USB changes below (in our last full test round: 10 boots, 5 game launches and 5 exits with no freeze). Some parts were tested on one console only. Your reports decide what goes into the final version.

> **Testers wanted:** this preview is open to your comments. Share your logs, feedback and videos in the **[1.4.1 testers discussion](https://github.com/HB-Agadir/X360Remap/discussions/10)**.

> **Offline only.** Taking a modified console onto Xbox Live can get it banned by Microsoft.

## Downloads

- **`X360Remap_1.4.1-pre_console.zip`** - the console files: `X360Remap.xex` (plugin), the `X360RemapStudio` folder with `X360RemapStudio.xex`, the optional Slim file for the front USB ports, and `X360Remap_Guide.html` (full guide in **English and Arabic**).
- **`X360RemapStudio-1.4.1-pre-win64.zip`** - X360Remap Studio for Windows.

**Version check:** the Studio's *Support* page and the first line of `X360Remap_plugin.log` must both say **1.4.1-pre**. Always install the plugin and the Studio **together**.

## Highlights

### Keyboard and mouse are always player 1
- **New default: players separate, keyboard and mouse = player 1**, a controller = player 2, **whatever the order you plug them in**. The console gives player numbers in plug order, so when a controller took player 1 first, X360Remap now swaps the two when the game reads them. Good for split screen or one against the other.
- **Fixed: the Xbox sign-in window ("Sign in", profile list) ignored the keyboard and mouse** when a USB controller was plugged in first. Games that ask for a profile (Rogue Warrior, Metro Last Light...) looked frozen on that window. The window listens to player 1, which is now always the keyboard and mouse.
- **Merged mode is now a choice:** controller + keyboard and mouse = one player (fast mouse aim with the controller's buttons). **Scroll Lock** switches, a notification shows the mode, the choice is kept in `players_merged.txt`. Merged mode now also works with USB controllers handled by X360Remap (before: only with a real Xbox 360 controller).

### Much more stable: no more USB system restart
- **The plugin no longer restarts the console's whole USB system at boot.** That restart froze some boots and could cut the Wi-Fi. Cost: a keyboard or mouse plugged in at power-on must be unplugged and plugged back once Aurora is on screen. Devices plugged in later work at once.
- **New, optional: restart only chosen USB ports** (`usb_port_reset.txt`, one `controller:port` per line). For the Slim, the zip has a ready file for the **two front ports** (`3:0` and `3:1`): about 40 s after power-on, they are restarted one after the other, the devices there come back by themselves, and **the Wi-Fi card, on its own internal port, is not touched**. The rear ports are not restarted yet and still need a replug. Tested on one Slim (Trinity) so far.
- **Boot freeze fixes:** the in-game display hook is installed before anything draws; the end of the boot "blackout" waits until the dashboard is really up; plugin threads get a 64 KB stack (the polling thread went past the old 16 KB one).
- **Black box:** if the kernel hits a fatal error, the plugin writes `fatal_log.txt` first. Any polling step slower than 400 ms is logged. The log reaches the disk after each boot step.

### In-game display (new)
- **Console info** at the top right: FPS, CPU and GPU temperature. **Win + I** shows or hides it in a game.
- **Input help** over the game: none (default), keyboard + mouse, or controller (front and top views). The keys you press light up. **Win** shows or hides it; the key can be changed.
- Theme, colours, position; works from 640x360 to 1920x1080. Plugin messages and Xbox notifications are shown in it. Nothing is drawn while the Xbox Guide is open.

### Controller page and macros (new)
- **Controller page** in the Studio, in the same style as the Keyboard & mouse page, with the controller's name in the device bar.
- **Combo macros:** one button plays a sequence of presses with their timing, up to 64 steps. Record it from the controller, test it, then play.

### Direct aim
- **12 more built-in games**, found by rgdshock with the F12 search (issue #7). With the imported Xenia Mousehook cameras and the Call of Duty games, direct aim now covers the CoD games plus 29 camera entries (each for a specific game version).
- **Win + Shift + D:** direct aim off / on for the running game. **Win + Shift + R:** reload the settings and the learned cameras.

### Studio (console)
- **Fixed (issue #8): "Plugin not loaded" and "no writable storage found"** on consoles where the hard drive link looked fine but could not be written: the Studio now checks that its folder can really be created before using the drive.
- **The device lists show only what is plugged in now** (green). Before, a mouse that was not plugged could stay selected (red) and you configured the wrong mouse without knowing. The selection follows the device by its USB ID.
- **"How it works" is now a paged guide** with pictures: one tutorial for the console, one for the Windows Studio.
- The console info shortcut is shown on the In-game display page. Fixed: the shortcut was saved as the letter A.

### Windows Studio
- **Stays in the console Studio's look**, with the same pages.
- **Live device status:** the keyboard, mouse and controller plugged into the console show in green, refreshed every 3 s while connected.
- **Controller page** with all the settings, like on the console; live buttons when the same controller is plugged into the PC.
- **Game profiles** filled from the console; **In-game display** page with a preview over a game picture (its tabs were one page off: fixed).
- **Find the camera (Xenia):** the Xenia Camera Finder is built in; a camera found goes straight into the game's direct aim settings.
- Finds the console again when its IP address changed; reads DashLaunch's `launch.ini` in both usual places.
- **How it works:** two 8-step tutorials (console and Windows), in four languages.

### Under the hood (for 1.4.2)
- The **GIMX mouse-to-stick engine** (method from the GIMX project, GPL-3) is in the plugin but **off**: there is no setting for it yet. It will come in 1.4.2 as a per-game option for games without direct aim.

## Install

**Easiest:** the Windows Studio (*File > Install X360Remap on the console*). It opens by itself the first time.

**By hand:**
1. Copy `X360Remap.xex` to `HDD:\X360Remap.xex` and the `X360RemapStudio` folder to `HDD:\X360RemapStudio\` (replace the old files).
2. In DashLaunch's `launch.ini`, `[Plugins]` section: `plugin2 = Hdd:\X360Remap.xex` (any free number). Keep `xbdm.xex` loaded. Do not load hiddriver360 at the same time.
3. Add `X360RemapStudio.xex` to Aurora.
4. Slim, optional: copy `optional\Slim_front_USB_ports\usb_port_reset.txt` to `HDD:\X360RemapStudio\`.
5. Restart the console. Replug the keyboard and mouse once if they were plugged in at power-on.

Coming from 1.3: your settings are kept. If you had `players_separate.txt`, it is no longer used (separate is the default now).

## Known issues

- Devices plugged in at power-on need one replug (except the Slim front ports with the optional file). The per-port restart was tested on one Slim only; Phat and E consoles are untested: do not use the Slim file on them yet.
- The in-game drawing can shake in some games.
- Quitting some games through the Guide (Aurora Home) can hang the picture (seen with Metro 2033). Power the console off and on.
- When a controller is moved to player 2, it does not vibrate and its light still shows player 1.
- Wi-Fi networks not found on some consoles (#5) and mice that do not move (#6): still open, logs welcome.
- `xbdm.xex` must be loaded.
- The console Studio's Support page still shows a YouTube link: that channel no longer exists.

## Coming in 1.4.2

- **Direct aim first:** more games with their camera found in memory, so the mouse works like on PC. Rogue Warrior and other games without a usable camera are the next targets.
- **GIMX method per game:** a game profile can use GIMX's mouse-to-stick method, for games where direct aim is not possible, with presets from the GIMX community configs (credited).
- **Players page** in both Studios: each plugged device with its port and player number, the merged choice, a notification when a device arrives ("Rapoo = Player 1").
- **Several keyboard and mouse players:** a keyboard and mouse on the same receiver are linked automatically, others can be paired in the Studio; room for more devices (two receivers plus controllers).
- **USB:** the rear ports one by one, then a port-map step in the Studios so every console model can use it.
- **In-game display:** size of your choice (with a limit so games do not stutter), HD preview on Windows, shake fix, a shortcut to switch the drawing between keyboard + mouse and controller, every shortcut configurable.
- The controller moved to player 2 keeps its vibration and light.

## Testers wanted: share your logs, feedback and videos

**This preview is open to your comments.** Post in the **[1.4.1 testers discussion](https://github.com/HB-Agadir/X360Remap/discussions/10)**: every report helps, even a short "works on my console".

- **Logs:** attach them to your comment. Windows Studio: *Logs > Save a zip to send*. By hand: `X360Remap_plugin.log` and `X360Remap_plugin_previous.log` from `HDD:\X360RemapStudio\`, copied **before** the next restart.
- **Feedback:** your console (Phat, Slim, E), the game, your keyboard / mouse / controller, what worked and what did not.
- **Videos:** a short clip of X360Remap in a game (phone on the TV is fine) helps more than a long text. Attach it to your comment, or post a link to it. With your permission, the best ones will be shared to show the project.

Bugs can also go to [Issues](https://github.com/HB-Agadir/X360Remap/issues), or to the [Se7enSins thread](https://www.se7ensins.com/forums/threads/need-your-feedback-on-my-plugin-who-let-you-play-with-keyboard-and-mouse-%E2%80%93-x360remap.1927902/). Ideas and requests are welcome too.

X360Remap is free. If it helps you play: [Ko-fi](https://ko-fi.com/x360remap). A fork of [hiddriver360](https://github.com/EinTim23/hiddriver360) by EinTim23, GPL v3.
