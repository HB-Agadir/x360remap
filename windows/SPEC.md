# X360Remap for Windows — specification (for Claude Code)

**One sentence:** the X360Remap Studio, on the PC, with a mouse and a real
keyboard: every setting (keys, mouse, per-game profiles, direct aim, cameras)
is edited comfortably on Windows and sent to the console over Aurora's FTP,
where the plugin picks it up live and the console Studio sees the same thing.

Author: HB (HB-Agadir). Spec written 2026-10-13. Code: Claude Code.

---

## 0. Decisions (already taken — do not reopen)

| Question | Decision | Why |
|---|---|---|
| Stack | **C++ / Visual Studio, Dear ImGui v1.90.9 + official Win32 + DirectX 11 backends** | The console Studio is already Dear ImGui. Its screens (`ui_mapping.cpp`, `ui_glossy.cpp`, `ui_layout.cpp`, `theme.cpp`, fonts, `glossy_assets`) and the settings engine (`X360Remap/mapping.cpp`, `application/config_writer.cpp`) are reused **as is**: same look (or better), and the file written is *by construction* the one the plugin reads. `preview/` already proves the UI compiles off-console. |
| Transfer | **Aurora's FTP server** (same as NetISO Manager) | No plugin change; the plugin already reloads `X360Remap.json` by itself within a second. |
| Single exe | yes, x64, no runtime to install | like Xenia Camera Finder |

Do **not** rewrite the settings format, the mapping logic or the screens in
another language. If a shared file must change, it changes in
`X360Remap-v1.3\` for everybody (console Studio + plugin + Windows), and the g++
tests (`X360Remap/tests`, `application/tests`) must still pass.

## 1. Where it lives

New folder `X360Remap-v1.3\windows\` in the same project (same repo, same
version number as the plugin):

```
windows/
  X360RemapWindows.vcxproj / .sln     x64, v142 (VS2019) - opens in 2022/2026
  src/
    win_main.cpp        window, DX11 device, ImGui Win32+DX11 backends, frame loop
    platform_win.*      what the console Studio gets from Xbox APIs:
                        paths, notifications (-> toasts in the window), clock
    console_link.*      Aurora FTP: connect, list, download, upload, backup
    sync.*              pull -> local working copy -> push, conflict check
    key_capture_win.*   Windows key / mouse events -> USB HID usage codes
    model_win.*         fills UiMappingModel from the working copy (the job
                        application/main.cpp does on the console)
  assets/               glossy textures + fonts (copied from the console Studio)
  third_party/imgui/backends/  imgui_impl_win32.*, imgui_impl_dx11.* (v1.90.9)
```

Reused from `application/` and `X360Remap/` (compiled into the Windows exe):
`ui_mapping.*`, `ui_glossy.*`, `ui_layout.*`, `glossy_layout.h`, `theme.*`,
`i18n.*`, fonts, `glossy_assets.*`, `aurora_library.*`, `config_writer.*`,
`wizard_session.*`, `calibration_session.*` (if needed), `mapping.*`,
`mapping_assistant.*`, `known_titles.*`, `known_devices.*`, `camera_learn.*`,
`camera_known.cpp`, stb_image. Anything Xbox-only goes behind `platform_win`.

## 2. Connection to the console (Aurora FTP)

- Settings: console IP (remembered), port 21, user/password = Aurora's defaults
  (the ones NetISO Manager uses), editable. A **[ Find my console ]** button that
  scans the local /24 for an FTP server answering like Aurora (optional, v1.1).
- Status bar: `Console 192.168.1.20 - connected - plugin 1.3-pre9` (version read
  from the first line of `X360Remap_plugin.log`).
- Files used, all in `HDD:\X360RemapStudio\` (Aurora FTP path `/Hdd1/X360RemapStudio/`
  — verify on a real Aurora, keep it configurable):

| File | Read | Write |
|---|---|---|
| `X360Remap.json` | yes | yes — the main settings |
| `camera_addresses.txt` | yes | yes — learned cameras |
| `known_devices.txt` | yes (devices seen by the plugin: VID:PID + name) | no |
| `known_titles.txt` | yes (games seen) | no |
| `X360Remap_plugin.log`, `X360Remap_studio.log` (+ `_previous`) | yes (Logs page, "Send logs" zip) | no |
| `X360Remap_switches.txt` | yes (shown in Advanced) | no |
| `disable_*.txt` / `enable_*.txt` switches | yes | yes (Advanced page, on/off) |

- **Safe write**, every time:
  1. download the current console file;
  2. if it changed since our last download (size or content) → show "changed on
     the console" with *keep mine / take console's / cancel*;
  3. upload as `X360Remap.json.new`, re-download and compare byte for byte,
     then rename over the original (keep `X360Remap.json.bak` = previous one);
  4. validate the JSON **with `mapping.cpp`'s own parser** before uploading —
     never upload a file the plugin would reject.
- Works offline too: open / save a local `X360Remap.json` (File menu). Useful
  without the console and for sharing a setup.

## 3. Screens (v1)

Same 4 pages as the console Studio — **Keyboard & mouse · Game profiles · How it
works · Support** — same components, same rules (no new page; one setting in
one place; advanced settings hidden until useful). On the PC they get bigger,
easier versions:

### Keyboard & mouse
- The controller picture in the centre (as on the console): click a controller
  button → press the key on the PC keyboard → assigned. Release-first rule (like
  pre7): the key is taken only after all keys are released.
- **Keys are captured on the PC** (`key_capture_win`): VK / scan code → USB HID
  usage (table for the 104/105 keys, AZERTY and QWERTY, numpad, F keys,
  modifiers left/right). Show the key names in the user's layout.
- Device list from `known_devices.txt` (so the user picks the console's
  keyboard / keypad / mouse by name, e.g. "Razer Tartarus V2").
- Keypads (Tartarus): show the numbered keys (01–20, thumb, thumbpad) using the
  factory table from OpenTartarus (already in `PLAN_v1.3.md` / `mapping.cpp`
  `AddKeypadPreset`).
- Mouse: sensitivity, dead zone, anti-dead zone H/V, smoothing, curve, invert Y,
  buttons (0–4+), **direct aim: ADS sensitivity, direct-aim smoothing**. Sliders
  with the real units and a one-line explanation each.
- "Default settings" (as on the console).

### Game profiles
- Games from `known_titles.txt` (+ Aurora covers if reachable via FTP, else name
  only). Per game: overrides of the mouse settings, keys, stick layout.
- **Direct aim** block per game: built-in / learned / none (from
  `camera_known.cpp` + `camera_addresses.txt`), with *Import a line* (paste from
  Xenia Camera Finder) and *Remove*. Validate pasted lines with `CamParseLine`.
- Export / import a profile as a small `.json` file (sharing, first step of the
  community database).

### How it works
Same content as the console page + the full shortcut table (`PLAN_v1.3.md`
section 3): F1–F12, Win/Shift/Alt+X, BACK+START, Esc×2…

### Support
Ko-fi link and QR (same as console), GitHub links (X360Remap, Xenia Camera Finder).

### Logs (inside How it works, or a small button in the status bar)
Download the 4 logs, show them, **[ Save a zip to send ]**.

## 4. Look

Same theme, fonts and glossy assets as the console Studio, at the window's
resolution (scale by DPI, minimum 1280×720, resizable). Mouse hover states,
tooltips, smooth scrolling — the things the console cannot do. Languages:
the 4 of the console Studio (FR, EN, ES, PT) from `i18n.cpp`.

## 5. What it must NOT do

- Never write anything outside `HDD:\X360RemapStudio\` (except nothing — no
  `launch.ini` edit in v1).
- Never upload an invalid or partial file (see safe write).
- No online features, no telemetry.
- No change to the plugin or the console Studio behaviour.

## 6. Milestones (each one usable)

| # | Result |
|---|---|
| W1 | Window + DX11 + the real theme/fonts; the Keyboard & mouse page shows a local `X360Remap.json` read with `mapping.cpp` |
| W2 | Editing on the PC (keys with PC capture, mouse sliders) + save locally; file re-read by the g++ tests = identical |
| W3 | Aurora FTP: connect, pull, safe push, status bar; the plugin log shows "X360Remap.json changed - mappings reloaded" |
| W4 | Game profiles + direct aim lines + import/export |
| W5 | Logs, switches, installer or portable zip, README |

## 7. Acceptance tests

1. Open the console's `X360Remap.json`, change nothing, push: the file on the
   console is byte-identical (or only reformatted) and the plugin still loads it.
2. Assign key 02 of the Tartarus to Y on the PC, push: in MW2, key 02 = Y, no
   restart.
3. Set ADS sensitivity 25 % for the Xtrfy, push: aiming in MW2 is 4× slower.
4. Edit on the console Studio, then push from the PC without pulling: the
   conflict dialog appears; nothing is lost.
5. Pull the cut on purpose (unplug the network mid-upload): the console file is
   untouched (`.new` never renamed).
6. Paste the MW3 line from Xenia Camera Finder into Game profiles, push:
   direct aim recognized on the console ("Direct aim ready" in the log).
7. Start with no console reachable: everything works on a local file.

## 8. Later (not v1)

Console screen preview (as in NetISO Manager), one-click plugin install
(`launch.ini` with backup), controller tester, macros / fighting-game
shortcuts, merged/separate player mode UI, community profile database.
