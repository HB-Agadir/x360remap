# X360Remap for Windows — vision and rules (v2, 2026-10-13)

Read this BEFORE `SPEC.md`. Where they disagree, this file wins.

Direction confirmed by HB on 2026-10-13: the Windows app is in charge of the
console application **and** uses what Windows offers that the console cannot:
a real link with the hardware, real information gathered and then transferred
- not only the mapping settings, also what our other tools already produce.
It is the upgrade path of the console version.

## 1. Who it is for

**Every X360Remap user, and first of all HB, the author.** Not one tester, not
one device. The Razer Tartarus is one known device among others: nothing in the
interface, the defaults or the help text may be written around it. Device-
specific information appears **only when that device is actually present**.

## 2. The golden rule: nothing fake on screen

Everything displayed must come from a real source: the console, the PC, a
file, or the user. No sample data, no example device, no invented game, no
"preview" values left in the build.

| Instead of | Show |
|---|---|
| a placeholder device list | "No keyboard seen by the console yet - plug one into the console" |
| a made-up game list | "No game played with X360Remap yet" + how the list fills up |
| a guessed plugin version | "unknown (no log received)" |
| a Tartarus line in the shortcut table | that line only if a keypad with that VID:PID is known |

Every empty state says **why** it is empty and **what to do**. When a value is
unknown, say "unknown"; never fill it in. Audit the shared pages too
(`ui_mapping`, `ui_glossy`, preview leftovers) for hard-coded display data used
by the Windows build.

## 3. Detect, don't ask

### The console
- **Find it automatically** at start: scan the local network (port 21), keep
  only servers whose FTP answers like Aurora **and** that have
  `/Hdd1/X360RemapStudio/`. One found = connect. Several = list them by name /
  IP. None = clear help (console on, Aurora open, same network).
- Show **real** console facts: plugin version and dashboard build (first lines
  of `X360Remap_plugin.log`), last boot time, game running, direct aim status.

### The devices
- The devices are the ones **plugged into the console**, by name and VID:PID,
  from the console itself — never a built-in list.
- `known_devices.txt` gives the devices ever seen. To know what is connected
  **now**, add a small status file written by the plugin (see 6).
- Known special devices (keypads, mice with many buttons...) get their extra
  layout only when detected.

## 4. Use what Windows gives us (the Xbox 360 cannot)

- **Full-size 16:9 layout.** The window fills a 16:9 screen without black bars
  or a small fixed canvas: panels grow with the window (1280×720 up to
  3840×2160), text stays sharp at any DPI, and windowed / maximised / full
  screen all look designed, not scaled.
- Real mouse and keyboard: hover, tooltips, drag and drop (profile files onto
  the window), right-click menus, search / filter in the game list, copy-paste.
- **Key test panel**: press a key on the PC and the matching controller button
  lights up, using the current layout (same HID codes as the console keyboard).
- Notifications that stay readable (no 3-second toasts only).
- File dialogs, opening folders and web links, zips for logs.
- Optional, off by default: check GitHub for a new release (no telemetry,
  nothing sent besides the version request).

## 5. One home for the four tools

| Tool | In the Windows app |
|---|---|
| X360Remap Studio | the main pages (keys, mouse, game profiles, help, support) |
| **Xenia Camera Finder** | built in, on its own **Direct aim** page (see 5e, which replaces the "button in Game profiles" idea): same engine (`src/shared` of Xenia-Camera-Finder = `X360Remap/camera_learn.*`), tried in Xenia first, then sent to the console in one click. The standalone tool keeps existing. |
| **NetISO Manager** | not merged (Python app): detected if installed, launched from a button, and it shares the console address so the user types it once. |

### 5b. The debugger (XboxFreezeLogger) — one "Diagnostics" area

`XboxFreezeLogger\` (C#) proved useful: it connects to the console's debug
monitor and writes every event to disk at once, so the last thing before a
freeze is never lost. Bring its job into the Windows app, **with the same look
as the rest** (same theme, panels, buttons, fonts) — no console window, no
separate program.

One **Diagnostics** area (where "Logs" and "Advanced" already are), with tabs:

| Tab | Content |
|---|---|
| Logs | today's Logs dialog: the 4 console logs over FTP, "Save a zip to send" |
| **Live** | the debugger: plugin lines and debug events in real time, colour per kind (plugin / exception / RIP / state change), filter and search, pause scroll; every line also written **immediately** to a file on the PC (survives a console freeze) |
| Freeze report | after a freeze: the last lines received, the time, the plugin version, the switches on — one click "save a report" (zip with logs + live capture) |
| Switches | today's Advanced: `enable_*` / `disable_*` files, plus "DbgPrint mirror" (`enable_dbgprint.txt`) needed for Live |
| Screen | optional: the console screen (debug monitor screenshot), like NetISO Manager |

**Hard rule:** `xdevkit.dll` and `JRPC.dll` are Microsoft / third-party binaries
that must **never** be shipped or published. The Windows app talks to the debug
monitor (`xbdm.xex`, TCP port 730) **directly, with its own small client** for
the text protocol (connect, notification channel for debug strings and events,
`getmem` for the plugin's log buffer, `modules`, `screenshot`). If `xbdm.xex`
is not loaded on the console, the Live tab says so in plain words and the rest
of the app works normally.

Keep from XboxFreezeLogger: immediate write to disk, the "guardian" behaviour
(the capture keeps running even if the window is closed by mistake: ask before
stopping), automatic reconnection after the console comes back.
Leave aside for now: `ViewAngleFinder.cs` (camera search over the debug
monitor) — the plugin's F12 search and Xenia Camera Finder cover it.

Order of work: insert after step 5 below ("Diagnostics" = step 5b).

### 5c. Install & repair (added 2026-10-20, HB)

Real case: a tester saw "Plugin silent" and "no keyboard or mouse", even after
going back to 1.2. His `launch.ini` pointed to a plugin file that no longer
existed. The Windows app should make this impossible to get stuck on:

- **Check:** over FTP, read `HDD:\launch.ini` (or `HDD:\DashLaunch\launch.ini`)
  and say in plain words what is wrong - same rules as
  `application/plugin_diag.h` (reuse it, it is header-only and tested): no
  X360Remap line in `[Plugins]`, a line pointing to a missing file,
  hiddriver360 loaded too. Also show the plugin version and dashboard build
  from the first line of `X360Remap_plugin.log`, and whether the log is
  growing (plugin alive).
- **Install / update:** one button puts the latest `X360Remap.xex` and
  `X360RemapStudio.xex` in `/Hdd1/X360RemapStudio/` (safe write, same as the
  settings). The two files come from the release zip the user picks (no
  download without being asked; optional GitHub check stays off by default).
- **Fix the plugin line:** only after showing the exact change and getting a
  clear yes. Keep `launch.ini.bak` first, change only the one line (or add
  `pluginN = Hdd:\X360RemapStudio\X360Remap.xex` in a free slot), never touch
  any other line. Then ask the user to restart the console.
- **Before any update:** save the console's `X360Remap.json`,
  `camera_addresses.txt` and the other settings files to the PC (dated
  folder), so nothing is ever lost.
- Never write outside `X360RemapStudio\` except this one confirmed
  `launch.ini` change.

The console Studio already shows the cause in a scrolling band at the bottom
when the plugin is not running (1.3-pre13).

### 5d. Controller assistant on the PC (added 2026-10-23, HB)

HB: "the main job of the Windows app is to make installing, setting up,
detecting devices and adding direct aim simple - if something is not clear on
the console, the user finds it on Windows." First case: his Chinese-brand
controller that the console does not recognise.

- **Detect:** the controller plugged into the PC (Raw Input / HID, already
  listed by the "PC devices" work): name, VID:PID, and its HID description
  (`HidP_GetCaps`, button caps = Button page usages, value caps = Generic
  Desktop axes X Y Z Rx Ry Rz, hat).
- **Known already?** Look it up the same way the plugin does: an entry in
  `X360Remap.json`, the built-in mappings, or the SDL list
  (`X360Remap/known_controllers_db.cpp` + `known_controllers.cpp`, shared
  code). If known: say so ("works as soon as it is plugged into the
  console"), nothing to do.
- **Unknown: a guided assistant.** "Press A", "Press B"... "Move the left
  stick right", "Pull LT"... each answer read live from the PC (which HID
  button usage or which axis moved, and in which direction), with Skip and
  Back. The controller picture lights up as you go, same look as the console
  pages.
- **Result = a normal device entry in `X360Remap.json`** (format of
  `mapping.cpp`, never a new one): `buttons` [{ idx = HID button usage - 1,
  field }], `axes` [{ usage, field }] with x/y = left stick, z/rz = right
  stick, rx/ry = LT/RT, and `invert` for axes that go the wrong way (Xbox: up
  = positive). Then the usual "Send" (safe write); the plugin reloads it
  within a second.
- **Check on the console:** with the PC linked, `X360Remap_status.json`
  (`../CONTRACT.md`) lists the controller with `"mapped": true`. If the
  console never lists it, say so plainly: "the console does not see this
  controller as a USB HID device - some cheap controllers switch to another
  mode when plugged into an Xbox; it cannot work this way". HB's Chinese
  controller may be such a case: plugged into the console, the plugin log
  showed no `HidAddDevice` at all (its PC VID:PID is still unknown -
  `30FA:0302` turned out to be HB's mouse, corrected 2026-10-23). So the
  assistant must not promise more than the console allows.
- **Share it (later):** the result can be exported and sent to us, so the
  controller can be added to the list for everyone.

### 5e. Direct aim made simple - Xenia Camera Finder inside the app (with step 6)

HB (2026-10-23): use what Windows has and the console has not. Direct aim is
found on the PC with Xenia, the user must understand it works **both in Xenia
and on the console**, and moving the result to the console must be one
obvious path - **no second program, no confusion**.

One page, **"Direct aim"**, in the main tabs (not a menu item), built around
the game:

1. **Pick the game** from the library (Aurora names + covers, the console's
   current `title` / `titleVersion` from the status file pre-selected).
   The page says at once where that game stands:
   - **Built in** - works on the console already, nothing to do;
   - **Found** - a line exists in `camera_addresses.txt`;
   - **Not yet** - the steps below.
2. **Find it with Xenia** (the Xenia Camera Finder engine, built in - the
   standalone tool keeps existing, but the user never needs it):
   - checklist with live ticks: Xenia found, game running in Xenia, the
     right game / version, "start a level and stand still";
   - one big **Find the camera** button, a progress bar with plain words
     ("turning the camera for you...", "checking 42 candidates...");
   - result: **"Found - try it now in Xenia"**: the app moves the camera
     with the PC mouse inside Xenia (XCF `--play`), so the user SEES direct
     aim work before touching the console.
3. **Send to the console**: one button - adds the line to
   `camera_addresses.txt` with the safe write, then "Start the game on the
   console: you will see the notification 'Direct aim ready'". The status
   file (`directAim`: `learned`) confirms it from the PC.
4. **Share**: optional button that copies the line + a ready text for the
   GitHub issue, so the game can be built in for everyone.

**Short tutorial** (first visit of the page, can be replayed; 4-5 screens
with pictures, EN / FR / PT / ES like the rest):
- what direct aim is (the mouse moves the camera itself, like on PC, instead
  of pushing a stick);
- why a game needs to be "found" once (each game keeps its camera somewhere
  else in memory);
- it works in Xenia **and** on the console: the same line serves both;
- two ways: on the PC with Xenia (easiest), or on the console with F12 (no
  PC needed) - both end in the same `camera_addresses.txt`;
- offline only.

**Console F12 path explained too**: the page shows the F12 steps in pictures
for users without Xenia, and reads `camera_search.txt` from the console to say
how the last search went.

**Honest limits shown on the page** (same text as the README):
- some games keep their camera in a way not supported yet (Gears of War,
  Halo...);
- **no recoil change, on purpose**: X360Remap will not touch recoil, because
  removing recoil is the classic online cheat and would get the project seen
  as a cheat tool. Only comfort options (view / weapon bob, lowering aim
  assist offline) may come later.

### 5f. The Controller page - "your controller, tuned like a pro pad" (added 2026-10-24, HB)

HB was not convinced by a "device management" approach. A player who loves
his controller does not want to manage a device: he wants it to react exactly
the way he likes, in every game, without thinking about it again. One
**Controller** page in the main tabs, in three steps. 5d (the assistant) is
only the fallback of step 1.

**1. I plug it in: it works, and I can see it**
- Recognised at once when it is in the known list (SDL list, 867 pads):
  nothing to do.
- The page shows **a picture of that controller**; every button pressed
  lights up live, with the Xbox button it gives ("your triangle = Y").
- Unknown pad: the 5d assistant ("Press A...") starts on its own.
- A plain verdict: "Works on the console" (status file: listed, `mapped`) or
  "The console does not see it: here is why".

**2. My buttons: one click, one press - same gesture as the keyboard**
- Click a button on the picture, press the one that must replace it.
- **For every game / for this game only**: one switch; the game's profile is
  applied by Title ID at launch, like the mouse profiles.
- **Presets**: swap triggers and bumpers, left-handed, jump on RB
  ("bumper jumper"). One click, then adjust.
- **Extra buttons** (back paddles, PS button, touchpad click): each can be
  any Xbox button - what players pay an Elite controller for.

**3. My sticks and triggers - where the PC makes the difference**
The same pad plugged into the PC gives its real values live.
- **Live circle** for each stick: the dot moves, dead zone and curve drawn.
- **Drift check**: "let go of the sticks for 3 seconds" - measures the drift
  and proposes the smallest dead zone that hides it (worn pads).
- **Per stick**: inner dead zone, anti-dead zone, response curve, horizontal /
  vertical sensitivity - same words as the mouse page.
- **Triggers**: activation point (hair trigger: fire at 10 % of the travel)
  and usable range (triggers that never reach 100 %).
- Tuned and seen on the circle first, then "Send" like everything else.

**Not done, on purpose** (same line as recoil): no turbo / rapid fire, no
combat macros - that would make X360Remap look like a cheat tool. Comfort and
accessibility only: **hold or toggle** for aim / sprint; later a **second
layer** (hold one button = second layout).

| Where | Role |
|---|---|
| Windows | the full page: live picture, stick circles, drift, presets, per game |
| Console Studio | the short version: change a button, pick a preset, game profile on / off |
| In game | quick bind BACK + START (exists) |

Order inside 5f: (1) live picture + "works on the console" verdict;
(2) button remap, all games / per game, presets; (3) sticks and triggers with
circles and drift check; (4) extra buttons, then hold / toggle.

**Needs the plugin side** (ask in MESSAGES.md, nothing decided yet): dead zone /
curve / trigger threshold applied to HID pads AND to the real Xbox
controller; button remap of the real Xbox controller; new fields in
`X360Remap.json` (decided with the plugin side, written in `CONTRACT.md`).

## 6. Plugin side - DONE in 1.3-pre13 (2026-10-23)

The plugin writes `HDD:\X360RemapStudio\X360Remap_status.json` every 2 s
while a PC is linked (or the Studio runs). **The exact format and rules are in
`../CONTRACT.md`** - read it, it is the reference for every shared file.

What the Windows app must do for it:
- while connected, rewrite `pc_link.flag` with NEW content at least every 5 s
  (a counter or the PC time - the plugin compares the content, not the file
  time, so clocks never matter);
- read `X360Remap_status.json` every 2-3 s; accept unknown fields; retry if a
  read fails mid-write; a new `uptimeMs` = plugin alive;
- stop rewriting `pc_link.flag` when disconnecting (the plugin stops writing
  the status file 10 s later).

## 7. Order of work

1. Remove every hard-coded display data; empty states everywhere (rule 2).
2. 16:9 full-size layout (rule 4).
3. Automatic console detection + real console facts (rule 3).
4. Plugin status file + live device list (rule 6).
5. Key test panel.
5b. Diagnostics area with the built-in debugger (section 5b).
5c. Install & repair (section 5c).
5d. Controller assistant on the PC (section 5d) - after step 4 (needs the status file).
5f. Controller page (section 5f) - with / after 5d; its plugin part is decided with the plugin side first.
6. Xenia Camera Finder built in.
7. NetISO Manager detection and launch.

Each step builds and passes `tests\run_tests.ps1`; steps 3, 4 and 6 are also
checked on HB's real console.

## 8. Progress

| Step | State (2026-10-13) |
|---|---|
| 1 | Done. No keyboard / no mouse: empty-state panels saying why and what to do, no default layout shown. Plugin version "unknown (no log received)" when no log; dashboard build shown from the log. Keypad lines of the shortcut table only when a known keypad is among the console's devices. First language = language of Windows. Shared pages checked: no sample device or game in `ui_glossy` / `ui_mapping` (the samples live in `preview/` only, not compiled here). **Open:** the green dot of a device means "console connected", not "plugged in now" - needs step 4. |
| 2 | Partly. Any window shape: the page background continues to the window edges and the status bar spans the width (no black band), free resize, F11 full screen, text re-baked sharp at the window's scale. **Open:** the pages still scale as one 16:9 block; panels that reflow and grow need a Windows layout of their own (same textures and components). |
| 5c | Done (2026-10-20), checked against the fake FTP server, **not yet on a real console**. File > Install & repair: the check runs on its own (launch.ini with `plugin_diag.h`, plugin version, plugin log changing or not, the two .xex in X360RemapStudio); install / update from a release zip (unzipped in the app since pre16; tar.exe before), settings backed up to `backups\<date>\` first; the launch.ini change is shown line by line and written only on "Yes" (launch.ini.bak, one line, refused if launch.ini changed since the check); "Copy the console settings to the PC" on its own. Tests: 5 launch.ini cases + install / fix / conflict / backup over FTP. |
| 5c+ | 2026-10-22, fake server + real GitHub (read only), **not yet on a real console**. Studio folder created on the console when missing (FTP MKD). launch.ini: a new line only in plugin1..plugin5; all five used by other plugins = nothing written, the user frees one. GitHub (off by default, previews optional): newest release by id, notice in the status bar, "Download this version" (zip or the two .xex) then the same install confirmation; tested live on pre12 (2.3 MB zip). |
| PC devices | 2026-10-22. Keyboards, mice and controllers plugged into the PC (Raw Input, VID:PID, product name). For each one unknown to the console: "Is it the same as on the console?" Yes = listed with the console's devices, settings saved under its VID:PID and sent on "Send"; No = nothing about it reaches the console. Answers kept on the PC (pc_devices.txt). Controllers: detected and shown, no settings editor yet. Rescans when a device is plugged in. |
| 3, 4, 5, 5b, 6, 7 | Not started. |
