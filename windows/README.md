# X360Remap for Windows

Direction and rules: [VISION.md](VISION.md) (it wins over the first spec), with
the progress table at its end.

The X360Remap Studio on the PC, with a mouse and a real keyboard. Every setting
(keys, mouse, per-game profiles, direct aim, cameras) is edited on Windows and
sent to the console over Aurora's FTP server. The plugin reloads
`X360Remap.json` by itself; the console Studio shows the same thing.

Single exe (`X360RemapStudio.exe`, x64), nothing to install.

## Using it

**First time: the install wizard.** At the first start (or File > Install
X360Remap on the console) the app walks through the whole install, one button
per step: find the console on the network, download the latest X360Remap from
GitHub and install it (the console settings are copied to the PC first), add
the plugin line to `launch.ini` (shown first, `launch.ini.bak` kept), put the
Studio in Aurora's applications (a small Aurora script you run once in Aurora's
utility scripts), restart the console and check that the plugin started.
The console must be on, in Aurora, on the same network; the remote restart
needs xbdm (otherwise restart it by hand).

1. Start `X360RemapStudio.exe`. Without a console it works on a local file.
2. Click the status text at the bottom left (or **File > Console connection**),
   enter the console's IP address, **Connect**. The defaults are Aurora's
   (port 21, user and password `xboxftp`, folder `/Hdd1/X360RemapStudio/`).
   **Find my console** lists the FTP servers of the local network.
3. Edit. Each change is saved on the PC at once; "Changes not sent" appears in
   the status bar.
4. **Send** puts the settings on the console. **Receive** loads the console's.

To bind a key: click a controller button in the grid, release every key, then
press the key on the PC keyboard. The physical key is what counts: the same key
on the console's keyboard gets the binding, whatever the layout (AZERTY, QWERTY).
With a Razer Tartarus selected, the grid shows the numbers printed on the keypad.

### What is where

| Page | Same as the console, plus on the PC |
|---|---|
| Keyboard & mouse | **Direct aim...** (status bar): sensitivity while aiming, direct-aim smoothing, sensitivity curve |
| Game profiles | **Game tools** (status bar): direct aim of the game (built-in / learned, import a line from Xenia Camera Finder, remove), export / import one game's profile as a `.json`. The game name is typed in a real text field. |
| How it works | **Shortcuts**: the full table of in-game shortcuts |
| Support | **Links**: opens the links in the browser |

**Logs** shows the four console logs and saves a zip to send. **Advanced** shows
and toggles the plugin's switch files (`enable_*.txt` / `disable_*.txt`).
**File** opens a local `X360Remap.json` or saves a copy (sharing a setup).
**File > Install & repair** checks DashLaunch's `launch.ini` (same rules as
the console Studio), installs / updates both `.xex` from a release zip after
copying the console settings to the PC (`backups\<date>\`): the plugin at
the root of the drive (`Hdd:\X360Remap.xex`), the Studio in
`Hdd:\X360RemapStudio\`. It fixes the plugin line only after showing the
exact change and getting a yes (`launch.ini.bak` kept). The plugin file and
`launch.ini` are the only writes outside `X360RemapStudio`.
The same dialog can look for new releases on GitHub (off by default) and
download one; nothing is installed without the same confirmation.

**File > Devices of this PC**: a keyboard or mouse the console does not show
can be plugged into the PC. If it is the same model as on the console, the
user says so once; it is then set up here and its settings are sent under its
VID:PID. Otherwise nothing about it is sent.

The window resizes freely and **F11** switches to full screen: the pages keep
their 16:9 shape, the background continues to the edges of the window.

Measuring and calibrating a mouse stay in the console Studio: both need the raw
reports of the mouse plugged into the console.

### How a file is sent (safe write)

For `X360Remap.json` and `camera_addresses.txt`, every time:

1. the file is checked with the plugin's own code (`mapping.cpp` parser,
   `CamParseLine`): an invalid file is never sent;
2. the console's current file is downloaded. If it changed since the last
   exchange (console Studio, F9 in game...), a dialog asks: keep mine / take
   the console's / cancel;
3. the new content is uploaded as `<name>.new`, downloaded again and compared
   byte for byte;
4. the previous content is kept as `<name>.bak`, then `<name>.new` is renamed
   over the original and the result is compared once more.

A cut before step 4 leaves the console file untouched. The settings are never
written outside the `X360RemapStudio` folder: the remote folder must be named
so, and only plain file names are accepted. No online feature, no telemetry.

### Where the PC keeps its files

`%LOCALAPPDATA%\X360RemapWindows\`: `settings.ini`, `work\` (the working copy
of the console folder), `base\` (the console files at the last exchange, for
the conflict check), `cache\` (Aurora game names and covers), and
`X360RemapWindows.log`. With a file named `portable.txt` next to the exe,
everything goes to a `data` folder next to it instead.

## Building

Open `X360RemapWindows.sln` (Visual Studio 2019, toolset v142, x64; opens in
2022 and later) and build **Release**. The exe lands in `bin\`.
`package.ps1` builds and makes the portable zip in `dist\`.

### What is compiled

Windows only (`src\`):

| File | Role |
|---|---|
| `win_main.cpp` | window, DX11 device, ImGui Win32 + DX11 backends, frame loop, 16:9 canvas and scaling |
| `platform_win.*` | folders, files, clock, toasts, settings, file dialogs, clipboard, zip |
| `console_link.*` | Aurora FTP client, safe write, "Find my console" |
| `sync.*` | receive / send on a worker thread, conflict check |
| `key_capture_win.*` | Windows key events to USB HID usage codes, key labels |
| `model_win.*` | fills the page models from the working copy (the job `application/main.cpp` does on the console) |
| `game_tools_win.*` | camera lines, export / import of a game profile |
| `ui_win.*` | status bar and PC-only dialogs |

Shared, compiled as is from `..\application\` and `..\X360Remap\`: the pages
(`ui_glossy`, `ui_mapping`, `ui_layout`, `theme`, `i18n`, fonts,
`glossy_assets`), `aurora_library`, `config_writer`, `wizard_session`,
`donation_links`, `stb_image`, and the settings engine (`mapping`,
`mapping_assistant`, `mouse_calibration`, `known_titles`, `known_devices`,
`camera_learn`, `camera_known`). The file written is therefore, by
construction, the one the plugin reads. A change to one of these files changes
the console Studio and the plugin too.

Dear ImGui: the **same v1.72b core** as the console Studio
(`..\application\third_party\imgui`), not a second copy: the shared pages
include it by relative path. `third_party\imgui\backends\` holds the official
Win32 and DX11 backends of the v1.72b tag. Textures and fonts are the arrays
already embedded for the console (`glossy_assets.cpp`, `font_data_*.cpp`), so
`assets\` only holds the exe manifest (UTF-8 paths, per-monitor DPI).

The fonts are re-baked at the window's scale, so the text stays sharp when the
window is larger than 1280 x 720.

### Tests

```
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
```

- key table (scan code to HID usage, left / right modifiers, numpad, labels,
  Tartarus numbers);
- settings round trip: what the pages write is re-read and re-written
  identically by `mapping.cpp`;
- FTP client and safe write against `tests\fake_aurora_ftp.py` (Python 3,
  standard library): unchanged file, normal send, conflict, "keep mine", file
  missing on the console, refusal outside `X360RemapStudio`, a server that
  cannot rename over a file, a server without `NLST`, and the network cut in
  the middle of an upload.

### Command line

| Option | Effect |
|---|---|
| `--data-dir=<folder>` | settings and working copy in this folder |
| `--no-connect` | do not connect at start |
| `--tab=0..3`, `--game=<Title ID>`, `--open=connection\|aim\|game\|logs\|advanced\|shortcuts` | start on a page / dialog |
| `--width=<pixels>`, `--height=<pixels>` | client size of the window |
| `--screenshot=<file.bmp>` `--after=<seconds>` | render, save the picture, quit (PC preview of a page) |

## Not in this version

Console screen preview, one-click plugin install, controller tester, macros,
merged / separate player mode, community profile database. Per-game key
overrides are kept as they are in the file but have no editor yet (same as the
console Studio).
