# X360Remap Studio for Windows - plan for 1.4 (2026-11-17, HB)

HB: "the Windows app is a copy of the console app; it must be more advanced and much
prettier, and spare the player the console setup so he can focus on playing. Do not stop
at my suggestions." The console Studio stays as it is; everything below is Windows only,
on the same files (`X360Remap.json`, `overlay_settings.txt`) and the same shared code.

## Navigation (own tab bar, an icon per page)

Keyboard & mouse | **Controller** | **Macros** | Game profiles | In-game display | **Console** | Help (+ Support)

## Pages

| Page | What the PC adds | Shared code used |
|---|---|---|
| Controller (new) | big controller picture lit LIVE by the pad plugged into the PC; click an Xbox button then press the pad button; presets (swap bumpers/triggers, southpaw sticks); stick circles with the dead zone drawn and the real stick dot; drift check; "works on the console" verdict | `ReadPadConfig` / `ApplyPadButtons` / `ApplyPadStick` (config_writer), SDL list (`known_controllers_db.cpp`), Windows joystick API + XInput |
| Macros (new) | list of the pad's macros (max 16); timeline of steps with durations; **record a combo live from the PC pad**; templates (quarter circle, dragon punch...); play it back on the picture before sending | `PadMacro` / `PadStep` in `ApplyPadButtons`, CONTRACT.md "macros" |
| In-game display | done 2026-11-17: live preview, own picture, drag, colour picker | `overlay_preview.cpp` |
| Console (new) | one place for: connection, install & repair (exists as a dialog), **X360Remap cover in Aurora** (the console Studio cannot do it), logs, switches | sync.cpp, install_win.cpp, Aurora FTP |

## The Aurora tile from Windows

Aurora keeps each title's pictures in `Data\GameData\<TitleId>_<content id>\` (`GC<TitleId>.asset`
= the cover). The community tool AuroraAssetEditor (Swizzy, github.com/Swizzy/AuroraAssetEditor,
`Classes/FTPOperations.cs`) uploads an `.asset` into that folder over Aurora's FTP; we do the same:
find the Studio's content id in `content.db` (the app already reads it), upload the cover asset
HB made (`GC00000000.asset`, 900x600, taken from his console), keep a copy of what was there.
Aurora shows it after its next start. Only on "Yes" from the player.

## Rules kept

- Nothing fake on screen (VISION.md 2): no sample device, no made-up game.
- No turbo / rapid fire (VISION.md 5f): macros are combos played once per press.
- A pad entry without axes is never written (config_writer): the page says to plug the pad
  into the console once instead.

## Order

1. Tab bar with icons, Controller page. 2. Macros page. 3. Console page with the tile.
4. Ideas after that: per-game controller profiles, console screenshot as the In-game display
   background, controller assistant for unknown pads (VISION.md 5d).
