# Prompts to redesign X360Remap Studio for Windows (all pages)

Use them in a design tool (Claude Design, Google Stitch, Figma AI...). Paste the
**shared brief** first, then one page prompt at a time. Each prompt only describes
what the app really does today (no invented feature), so the result can be built.

---

## 0. Shared brief (paste first, every time)

```
Design a desktop app for Windows called "X360Remap Studio". It sets up a USB keyboard,
mouse and USB controller for a modded Xbox 360 (RGH/JTAG): the player configures
everything on the PC, then clicks "Send" and the console receives it over the network.

Canvas: 1280x720 desktop window (must also scale to 1920x1080). Dark theme.
Mood: premium gaming software (like Razer Synapse, Logitech G HUB, Steam Big Picture),
clean, not cluttered, big readable controls.
Colours: background deep navy #070D19 to #0A1324 with a subtle diagonal light streak;
cards #0A1324 at 95% with 1px border #22375A and 12px radius; accent gold #F5D35A for
the active tab, primary buttons and selected items; text #E9EEF4, secondary #A9B8C9,
hints #7F90A4; success #5BE06A, warning #F0B429, error #F0605A. Xbox face colours:
A #6FBF3A, B #E5413B, X #2F8DE8, Y #F0B429.
Fonts: Barlow (titles, tabs, buttons), Inter (body).
Layout shared by all pages:
- Top left: the X360Remap logo (green Xbox-style badge + "X360 REMAP / STUDIO").
- Top: a tab bar with an icon + label per page: Keyboard, Controller, Macros, Games,
  Display, Console, Help, and a heart icon button for Support on the far right.
- Under the tabs: one line of help text for the page.
- Bottom status bar (full width, 36px): left = console link state (dot + "Connected to
  192.168.1.102" / "Offline - local file") and "Changes not sent" in amber when needed;
  right = buttons "Receive", "Send" (gold when changes wait), "File", "Logs", "Advanced".
- Languages: French, English, Spanish, Portuguese (BR); design with French text length.
Rules: never show fake data. Empty states explain what to do ("Plug a keyboard into the
console", "No game played with X360Remap yet"). Every control has a hover and a pressed state.
```

---

## 1. Keyboard & mouse

```
Page "Keyboard": two columns.
Left card "Keyboard settings": a device chip (keyboard name + VID:PID + green dot when
plugged into the console, red when not, dropdown to pick another known keyboard).
A big "Wizard - 21 steps" button (press each key in turn). Then a grid of the 21 Xbox
controls (A B X Y, LB RB LT RT, L3 R3, Start, Back, Guide, D-pad 4 ways, left stick 4 ways),
each cell = Xbox button icon -> the keyboard key bound to it shown as a keycap; click a cell
then press a key to rebind; modifier tags L/R for Ctrl/Shift/Alt.
Right card "Mouse settings": device chip like the keyboard; mouse buttons list (left,
right, middle, side 1, side 2, wheel up/down) each -> Xbox button, "+ Add"; sliders with
value: Sensitivity (1000-30000), Dead zone, Anti-dead zone (horizontal and vertical),
Smoothing (0-100 ms, "off" at 0); checkbox Invert Y; "Mouse resolution: not measured /
1600 DPI" with a "Measure" link; buttons "Mouse advanced..." (aim speed while aiming,
response curve).
Footer links: "Default settings" and "Save as default".
Make it feel like a gaming peripheral app: keycaps rendered as real keys, a faint
keyboard illustration behind the left card.
```

## 2. Controller

```
Page "Controller": edits a USB controller (not Xbox) used on the console.
Left large card: header = controller name ("Defender Joystick Cobra R4") + VID:PID,
"Next" button when several controllers; status pills: green "Ready for the console" or
amber "Plug it into the console once, open the Studio, then Receive"; blue "LIVE on this PC"
when the same controller is plugged into the PC.
Centre: a large, beautiful Xbox 360-style controller illustration (top view, front
triggers visible above). Every button is a hot spot: hover = gold outline, selected =
pulsing gold ring, pressed live = filled gold. Under each button a small tag with the
physical button number of the controller ("B3"). Sticks show the live position.
Bottom line: "Click an Xbox button, then press the controller button that must do it."
Right card "Settings": PRESETS (pills: "Original layout", "Triggers <> bumpers");
STICKS: two live circles (left, right) with crosshair, the dead zone drawn as a red disc,
the live dot gold (red when inside the dead zone); slider "Dead zone 0-40 %"; switch
"Invert the right stick (up / down)"; bottom buttons "Revert" (ghost) and "Save" (gold).
```

## 3. Macros

```
Page "Macros": one controller button plays a combo once per press (fighting games).
Left card: title "Macros 2 / 16", controller name; list rows "Button B13 - 3 steps - 163 ms"
with a play-sequence icon, selected row gold bar; buttons "+ New macro", "Delete macro".
Right card: title "Played by button B13" + "Change" (press a controller button) and -/+.
A horizontal TIMELINE: one block per step, width = duration, each block shows coloured
chips of the buttons held (A green, X blue, arrows for the D-pad, grey for LB/RB...),
"50 ms" under it, "pause" when empty; the selected block has a gold outline; a gold play
head sweeps across during playback.
Under it: buttons "Play", "Record" (red dot, blinking while recording + text "Do your combo
on the controller..."), "+ Step", "- Step".
STEP editor: 17 toggle chips (A B X Y LB RB LT RT L3 R3 ST BK G up down left right),
"Duration 50 ms" slider (16-1000) and a switch "Held" (as long as the button stays held).
TEMPLATES: 4 pills ("Quarter circle forward + X", "Dragon punch + X", "Quarter circle back + Y",
"Double forward (dash)"). Bottom right "Save". No turbo / rapid fire anywhere.
```

## 4. Games (game profiles)

```
Page "Games": the player's game library from the Aurora dashboard.
Top: filter pills "All games" / "With a profile", a search box.
Main area: grid of game covers (portrait 3:4, real Aurora covers, exact names, a small gold
badge on games that have a profile); scrollable; empty state "No game played with
X360Remap yet: launch a game on the console, then Receive".
Right panel for the selected game: big cover, name, Title ID; per-game mouse settings
(Sensitivity, Dead zone, Anti-dead zone H and V, Smoothing, Response curve 0.3-3, Stick
layout: Default / Southpaw / Legacy / Legacy Southpaw, Invert Y); "Direct aim" status
(Supported / Not supported / Learn with Xenia Camera Finder); buttons "Rename",
"Delete profile", "Save".
```

## 5. Display (in-game overlay)

```
Page "Display": what the console draws over the game.
Left big card "Preview": a 16:9 game picture (default: a dusk city scene with a crosshair,
or the player's own screenshot - segmented control "Scene / My picture...") with the
overlay drawn on it at real scale: top-right "FPS 60  CPU 45C  GPU 52C" in green, bottom-left
either keyboard keycaps with small Xbox button discs + a mouse outline, or a two-view
controller outline. A pill top-left "LIVE: W A S D, Space, Shift, Ctrl, clicks" with a green dot:
keys pressed on the PC light up in the preview. The keyboard/controller drawing can be
dragged; while dragging show a gold frame and "X 20  Y 590".
Right card "Settings": CONSOLE INFO switch (+ hint "In game: shortcut key + I"); INPUT HELP
segmented None / Keyboard / Controller; COLOURS: 5 two-tone theme dots + two swatches
"Pressed" and "Resting" opening a colour wheel; KEY STYLE segmented Pictures / Drawn;
IN-GAME SHORTCUT (SHOW / HIDE): key shown ("Windows") + "Change"; buttons "Defaults", "Save".
```

## 6. Console (new)

```
Page "Console": everything about the Xbox 360 itself, so the player never touches it.
Card "Connection": console address, FTP user, state (connected, last exchange time),
buttons "Find my console" (shows consoles found on the network, one click to pick),
"Connect".
Card "Install & repair": plugin version on the console vs latest on GitHub, checks list with
green/red ticks (plugin loaded in launch.ini, xbdm loaded, X360RemapStudio folder present,
plugin log alive), buttons "Install / update", "Repair launch.ini" (shows the exact line
change before writing), "Back up console settings".
Card "Aurora": "X360Remap Studio cover in Aurora": preview of the 900x600 cover (back |
front), status "Not installed / Installed", button "Install the cover" (needs Aurora restart).
Card "Diagnostics": live plugin log with colour per kind, search, pause, "Save a report (zip)";
switches list (enable_* / disable_* files) as toggles with one-line explanations.
```

## 7. Help

```
Page "Help": "How it works" in 4 illustrated steps (1 copy the plugin, 2 load it in DashLaunch
with xbdm, 3 add the Studio in Aurora, 4 replug keyboard and mouse once Aurora is shown),
then "In-game shortcuts" as a keycap table (F5/F6 sensitivity, F7/F8 horizontal anti-dead
zone, F3/F4 vertical, F1/F2 smoothing, F10/F11 dead zone, hold F9 save game profile, hold
BACK+START quick bind, F12 camera search, Windows show/hide drawing, Windows+I console info),
then "Troubleshooting" accordions (console freezes at boot, keyboard not detected, plugin
silent, game not supported for direct aim). Offline only notice.
```

## 8. Support (heart)

```
Page "Support": app version and console plugin version, "Check for updates" (GitHub
releases), a warm thank-you message from the developer H.B_Agadir, buttons Ko-fi, PayPal,
GitHub (star the project), and a QR code to the releases page. Credits: hiddriver360
(EinTim23), Kenney input prompts, SDL GameControllerDB, Xenia Mousehook.
```

## 9. Dialogs and first start

```
Design 4 dialogs in the same style (modal card centred, dimmed background):
1) "Console connection": IP, port 21, user, password, folder "/Hdd1/X360RemapStudio/",
   "Connect at start" checkbox, buttons Connect / Find my console, found list, error line in red.
2) "Devices plugged into this PC": list of keyboards / mice / controllers with VID:PID and
   "Is it the same as the one plugged into the console?" Yes / No, or "The console already knows it".
3) "Changed on the console": conflict between PC and console copies, choose which to keep.
4) First start splash: animated logo, version "v1.4", then a 3-step onboarding
   (find the console, receive settings, plug your devices).
```
