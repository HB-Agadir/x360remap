# X360Remap — the shared contract (plugin · Studio · Windows app)

Every file in `HDD:\X360RemapStudio\` (FTP: `/Hdd1/X360RemapStudio/`): who
writes it, who reads it, its format and its rules. **Read this before changing
how any of the three programs reads or writes one of these files**, and add a
line to the change log at the end when you do.

Programs:
- **Plugin**: `X360Remap.xex` (`X360Remap/main.cpp`), runs all the time.
- **Studio**: `X360RemapStudio.xex` (`application/`), on the console.
- **Windows**: `X360RemapStudio.exe` (`windows/`), over Aurora's FTP.

Shared code is the first rule of the contract: the settings file is parsed
and written by `X360Remap/mapping.cpp` in all three programs, camera lines by
`X360Remap/camera_learn.*`, the launch.ini diagnosis by
`application/plugin_diag.h`. Change the format there, never in a copy.

## Version

All three programs carry the same version (`1.3-pre13`): `X360REMAP_VERSION`
in `X360Remap/main.cpp`, `kAppVersion` in `application/main.cpp`, and the
Windows app's own. They are released in one zip. The plugin writes its version
in the first line of `X360Remap_plugin.log` and in `X360Remap_status.json`.

## Settings (all three read; Studio, Windows and the plugin write)

| File | Format | Writers | Readers | Rules |
|---|---|---|---|---|
| `X360Remap.json` | JSON, `mapping.cpp` (devices by VID:PID, keys, mouse, profiles per game) | Studio; Windows (safe write); plugin (F9, quick bind, live F-keys) | all three | The plugin reloads it within ~1 s when its time/size changes. Writers keep a `X360Remap.json.tmp` copy and never delete the original (`SaveMappingsToFile`). Windows: `.new` → read back → `.bak` → rename, conflict dialog if the console's copy changed. |
| `X360Remap_default.json` | same as above | Studio ("save as default") | Studio ("restore defaults") | the plugin never reads it. Windows keeps its own copy on the PC (working folder) and never sends it. |
| `camera_addresses.txt` | one camera line per row, `CamParseLine` (v1 / v2 / v3) | plugin (F12 search); Windows (safe write) | plugin (boot + after a save), Windows | Lines in this file win over the built-in list (`camera_known.cpp`). |
| `X360Remap_lang.txt` | first byte: `0` FR, `1` EN, `2` PT-BR, `3` ES (anything else = EN) | Studio | plugin (notifications), Studio | — |
| `players_merged.txt` | present = players merged (pad + keyboard / mouse = one player); absent = separate, keyboard / mouse = player 1 (default since 1.4.2) | plugin (Scroll Lock) | plugin at boot | Windows may create / delete it as a setting. |

## Written by the Windows app only

| File | When | Notes |
|---|---|---|
| `<name>.new` | every safe write (`X360Remap.json`, `camera_addresses.txt`, the two `.xex`, `launch.ini`) | upload, read back, then renamed over `<name>`; removed if the read-back differs. A leftover `.new` means a cut before the rename: the original is untouched. |
| `<name>.bak` | same | the previous content, written before the rename. |
| `X360Remap.xex`, `X360RemapStudio.xex` | Install & repair, after a yes | settings copied to the PC first (`backups\<date>\` on the PC). |
| `overlay_settings.txt`, `enable_overlay.txt` | "Send", when the In-game display page was saved on the PC | same safe write as the settings; the switch file is created or deleted to match what the settings show. |
| the folder itself | first write on a console without it | FTP MKD `/Hdd1/X360RemapStudio/`. |

Windows reads `force_boot_protocol.txt` nowhere and does not toggle it: its
Switches list only handles empty `enable_*` / `disable_*` files
(`force_boot_protocol.txt` holds `0` / `1`, so it is not a plain switch).

## Lists the plugin keeps (plugin writes, the others read)

| File | Format | Readers |
|---|---|---|
| `known_devices.txt` | one line per device AND type: `VVVV:PPPP:K`, `:M` or `:C` (C = USB pad, since 2026-11-05, named from the SDL list or its USB name; older readers skip C lines; before this a named pad was wrongly written as K) (a combo receiver such as Rapoo 24AE:2015 has BOTH K and M lines, since 1.3-pre14) (hex upper case), then `:name` if known (`:` in names replaced by spaces), `known_devices.h` | Studio, Windows |
| `known_titles.txt` | one 8-digit hex Title ID per line, `known_titles.h` | Studio, Windows |
| `camera_search.txt` | F12 search log, never erased at boot | people (support) |

## Live state

| File | Writer | When | Readers | Format |
|---|---|---|---|---|
| `studio_active.flag` | Studio (created at start, re-created every 2 s) | while the Studio runs | plugin | empty. The plugin deletes it at every title change. |
| `input_state.json` | plugin | ~60/s, only while `studio_active.flag` exists, and only with a keyboard or mouse plugged in | Studio | one line: `keyCode`, `mouseButton`, `mouseDX/DY`, `mouseButtons`, `wheelDelta`, `rawLen`, `raw`, `rawSeq`, `tick`, `devs` (`k:046d:c33a;m:1532:0067`) |
| `pc_link.flag` | **Windows** | rewrite with NEW content (e.g. a counter or the PC time) at least every 5 s while connected | plugin | any text up to 64 bytes. The plugin compares the content, not the file time: a change in the last 10 s = PC linked. A file left from an old session does not count. |
| `X360Remap_status.json` | plugin | every 2 s, only while a PC is linked (2026-10-30: no longer for the Studio) | Windows (step 4) | see below |

### `X360Remap_status.json` (format 1, since 1.3-pre13)

```json
{
  "format": 1,
  "plugin": "1.3-pre13",
  "dashboard": 17559,
  "uptimeMs": 123456,
  "title": "41560817",
  "titleVersion": "0.0.9.21",
  "directAim": "learned",
  "directAimName": "Modern Warfare 2 TU9",
  "players": "merged",
  "paused": false,
  "realController": true,
  "devices": [
    { "vid": "1532", "pid": "022b", "name": "Razer Tartarus V2", "kind": "keyboard", "slot": 0, "mapped": true }
  ]
}
```

- `uptimeMs`: console tick count. **A new value = the plugin is alive** (the
  Studio uses this; after the first minute the log only gets a heartbeat every
  30 s, so the log is not a reliable sign of life).
- `title`: `00000000` on the dashboard. `titleVersion` empty when unknown.
- `directAim`: `off`, `builtin` (a built-in Call of Duty build) or `learned`
  (a camera line in use in the last 3 s).
- `players`: `merged` or `separate`. `paused`: Win+Shift+X.
- `realController`: a real Xbox 360 controller was read in the last 3 s.
- `logBuffer` / `logBufferLen`: addresses (hex) of the plugin's 8 KB in-memory
  log and of its fill length (an `int`). After a freeze, read them over XBDM
  (`getmem`): the lines that never reached the disk are there. Same addresses
  for one build of the plugin.
- `devices`: what is plugged in **now**. `kind`: `keyboard`, `mouse` or
  `controller`. `slot`: the player slot (-1 = none). `mapped`: the device has
  a mapping (X360Remap.json, built-in or the SDL controller list).
- Readers must accept unknown fields (new ones are added, never renamed) and
  retry if a read fails mid-write. `format` changes only when a field changes
  meaning.

## Logs and switches

| File | Writer | Notes |
|---|---|---|
| `X360Remap_plugin.log` | plugin | first line: version + dashboard build. Truncated at each boot after copying the old one to `X360Remap_plugin_previous.log` (last 256 KB). |
| `X360Remap_studio.log` / `_previous.log` | Studio | same idea |
| `X360Remap_switches.txt` | plugin, at boot | which `enable_*` / `disable_*` switches were found |
| `enable_*.txt`, `disable_*.txt` | people / Windows (Switches tab) | empty files, read at plugin boot only. List: `enable_dbgprint`, `enable_stick_release_ramp`, `enable_input_trace`, `enable_packet_on_change`, `disable_stick_rest_noise` (lets the left stick be exactly 0 again; by default it never is, for every game), `enable_interrupt_status_check`, `disable_defer_report_free`, `disable_clean_unplug`, `disable_generic_hid` (non-boot HID devices left to the console, freeze test), `disable_own_link` (do not use the plugin's own `X360R:` hard-drive link, pre13 behaviour; freeze test), `disable_usb_reset` (skips the USB reset done ~20 s after boot; read at that moment, not at boot; Wi-Fi test, issue #5), `disable_notifications`, `force_boot_protocol`, `enable_overlay` (in-game overlay work, off by default; step 2 draws one test rectangle in 1280x720 games). Test switches: never needed by players. |

Controller page (2026-11-03): `input_state.json` is also written when only a USB pad is plugged in; `devs` adds `c:vid:pid` for each USB pad; new field `padBtn` = hex mask of the HID buttons held on a USB pad (bit n = HID button idx n, as in `buttons`), filled only while the Studio runs. A pad known only from the built-in or SDL lists gets its mapping written once to `X360Remap.json` when the Studio opens (axes, buttons, invert, deadzone): the Studio must re-read `X360Remap.json` when a new `c:` device appears. Device entries may carry `"macros"` (at most 16 per device; the plugin keeps them when it saves the file). Sequence form (2026-11-05, HB: one pad button = a fighting-game combo): `{ "idx": 6, "steps": [ { "fields": ["dpad_down"], "ms": 50 }, { "fields": ["dpad_down", "dpad_right"], "ms": 33 }, { "fields": ["dpad_right", "x_button"], "ms": 80 } ] }`. One press of HID button idx plays the steps once, in order, even if the button is released; presses while it plays are ignored. Each step holds its fields (names as in `buttons`, any number, D-pad for directions) for `ms` milliseconds (16 to 5000, clamped); `"fields": []` = a pause; `"ms": 0` = as long as the pad button stays held (useful as the last step). At most 64 steps; every step reaches at least one game read. Old form, still read and written for a single held combo: `{ "idx": 4, "fields": ["a_button", "b_button"] }` = while idx is held, all fields are pressed (fewer than 2 valid names = ignored). The Studio removes a macro idx from `buttons`. Shared struct: `HidButtonMacro` in `X360Remap/mapping.h` (steps[], stepCount, mask bit i = kButtonFieldNames[i] via MacroFieldBit).

NetISO recorder (other project, 2026-11-03): Ctrl + HID 0x35 makes the plugin create `HDD:\NetISO\rec_toggle.txt` ("toggle") from its polling thread, only if `HDD:\NetISO` exists, at most once a second and never while the previous one is unread; NetISO's module deletes it.

`camera_addresses.txt` is also edited by the Studio: deleting a game's camera removes its lines (old file kept as `camera_addresses.bak`); the plugin reloads it when the Studio closes.

`overlay_settings.txt` (people / Studio, read at boot and when the Studio closes; the Studio creates `enable_overlay.txt` when something is shown and deletes it when nothing is; since 2026-11-06 the VdSwap hook is always installed at plugin load and this file only switches the drawing, re-read when the Studio closes): one `name value` per line, values in hexadecimal. `color_on`, `color_off` = RRGGBB; `keys_x`, `keys_y`, `info_x`, `info_y` = position, limited to the screen; `info_always` 1 = aim settings always shown (default: 4 s after a change); `style` 1 = Kenney key images (default), 0 = drawn keys; `show_keys` 1 = draw the keyboard and mouse (default 0, a learning aid); `show_info` 0 = hide the aim panel (default 1); `show_stats` 0 = hide the top-right line FPS / CPU / GPU temperatures (default 1; `disable_temps.txt` in the Studio folder stops the SMC read, the FPS stays); `show_pad` 1 = draw the two-view controller instead (default 0; it shows what the game receives, at `keys_x`/`keys_y`; the Studio offers ONE choice, and the keys win if both are 1); while a game runs the plugin also reads `Hdd:\NetISO\rec_state.txt` once a second and shows the recorder's start / end as a 4 s message; `info_secs` 2, 4 (default) or 8 = aim panel duration; `hotkey` = HID usage of the key that shows / hides the keyboard or controller drawing (default E3 = either Windows key; since 2026-11-09 only modifier keys E0-E7 are taken, anything else means Windows, because a typing key also reaches the game). Since 2026-11-09 (HB): the drawing chosen by `show_keys` / `show_pad` starts hidden at every game and only the hotkey shows it; `show_info`, `info_always`, `info_secs`, `info_x`, `info_y` are ignored (no aim panel on the picture, F12 and the tuning keys are Xbox notifications); colours follow the frame layout from the VdSwap fetch constant (endian + swizzle, `hud_color.h`). Missing file or line = default.

## Outside the folder

- **`launch.ini`** (`HDD:\launch.ini` or `HDD:\DashLaunch\launch.ini`): read by
  the Studio and Windows (`plugin_diag.h`). Only Windows may write it, only
  after a clear yes from the user, one line, `launch.ini.bak` first.
- **Aurora (Windows install wizard, HB 2026-10-24)**: Windows READS
  `/Game/Data/Databases/settings.db` over FTP (is `\X360RemapStudio` a scan
  path?) and, when the user asks, WRITES one file:
  `/Game/User/Scripts/Utility/X360Remap Setup/Main.lua` (Aurora's FTP root
  `/Game` = Aurora's folder; same mechanism as NetISO Manager). The user runs
  it in Aurora; it adds `\X360RemapStudio` (Hdd1, depth 2, "Applications") to
  `Config.ScanPaths` if missing, then offers to restart Aurora. Aurora's
  databases are never written from the PC.
- **Reboot**: Windows may send `magicboot cold` to xbdm (TCP 730), only after
  the user's yes; then it waits for a new `X360Remap_plugin.log`.
- **Plugin file (HB 2026-10-26: plugin at the root, Studio in the folder)**:
  Windows install writes `X360Remap.xex` at the root of the drive
  (`/Hdd1/X360Remap.xex` = `Hdd:\X360Remap.xex`, safe write, a fixed name)
  and proposes the launch.ini line `Hdd:\X360Remap.xex`.
  `X360RemapStudio.xex` stays in `X360RemapStudio\`.
- Nothing else outside `X360RemapStudio\` is ever written.

## Change log

| Date | Change | By |
|---|---|---|
| 2026-11-12 | `overlay_settings.txt`: the Windows app now edits it too (its "In-game display" page), sends it with the safe write like `X360Remap.json`, and creates or deletes `enable_overlay.txt` to match. The plugin watches the file like `X360Remap.json` and re-reads it (and `enable_overlay.txt`) within 2 s of any change, so writers set the switch file first, then the settings. `show_stats` is really saved now (it was missing from the text the Studio wrote, which also made `hotkey` come out as 4); readers take any `hotkey` outside E0-E7 as E3. `enable_overlay.txt` is wanted when keys, pad or stats are shown (`show_info` no longer counts). | Claude Code |
| 2026-11-12 | `input_state.json`: written only while `studio_active.flag` exists, as this file always said. The plugin's fallback "write until a Studio was seen once" is gone. | Claude Code |
| 2026-10-29 | `known_devices.txt`: key is now VID:PID + type (combo receivers get a K and an M line). Plugin working folder may be `X360R:` (own link to `\Device\Harddisk0\Partition1`, same files). New switch `disable_own_link.txt`. | plugin / Studio conversation |
| 2026-10-26 | New test switch `disable_usb_reset.txt` (pre14). | plugin / Studio conversation |
| 2026-10-26 | Windows: plugin installed at the root of the drive (`Hdd:\X360Remap.xex`) and launch.ini line proposed there, per HB; the console Studio's launch.ini hints and the guide still say `Hdd:\X360RemapStudio\X360Remap.xex` (see MESSAGES.md). | Claude Code |
| 2026-10-24 | Windows install wizard: reads Aurora `settings.db`, writes the "X360Remap Setup" Aurora script on request, cold reboot over xbdm on request (see "Outside the folder"). | Claude Code |
| 2026-10-23 | Windows: added "Written by the Windows app only" (.new / .bak / .xex / folder); `X360Remap_default.json` is not written on the console by Windows; `force_boot_protocol.txt` is not in its Switches list. | Claude Code |
| 2026-10-23 | Status file: `logBuffer` and `logBufferLen` added (freeze diagnosis over XBDM). | plugin / Studio conversation |
| 2026-10-23 | First version of this file. New: `X360Remap_status.json` (format 1), `pc_link.flag` (content-based). Studio uses `uptimeMs` as its sign of life. | plugin / Studio conversation |
