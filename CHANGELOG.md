# X360Remap v1.2 — what changed since v0.1.1

v1.2 is the **third public release** of X360Remap (after v0.1.0 and v0.1.1,
August 2026) and by far the biggest one.

---

## A brand-new Studio

The on-console app was redrawn from scratch.

- **New "glossy" interface** in four pages: *Keyboard & mouse*, *Game
  profiles*, *How it works*, *Support*.
- **Animated splash screen** at launch (skip it with any key or click).
- **Keyboard & mouse page:** every controller button shown with its key
  (keycap icons), the controller in the middle, the mouse buttons on the
  right. Click a cell to reassign it, or run the 21-step wizard.
- **Device menus with live status:** the keyboard and mouse icons show a
  green dot when plugged in and a red dot when not; the drop-down lists every
  known device with its name and VID:PID.
- **Language flags** (English, Français, Español, Português).
- **Restore defaults / save as default** links.
- **Game profiles page:** your game library with **Aurora covers and exact
  names**, a filter for games that have a profile, rename, delete one profile
  or all of them, and settings that save themselves when you release a
  slider.
- **How it works page:** the four steps, the in-game shortcuts and a short
  troubleshooting guide.
- **Support page:** current version and a QR code to the releases page to
  check for updates (the console cannot reach GitHub by itself).
- **Esc twice returns to Aurora.**

## Better mouse aim

- **Smoothing** (0–100 ms): spreads each mouse movement over a few
  milliseconds. The mouse and the game don't run at the same pace; without
  it, the camera speed jumps from frame to frame.
- **Anti-deadzone, horizontal and vertical separately:** games ignore small
  stick movements, and many (Call of Duty first) ignore more vertically than
  horizontally. Small mouse movements now move the camera right away.
- **Response curve from 0.3 to 3** (1 = linear): below 1, small movements
  count more, to compensate for games that make the start of the stick very
  slow.
- **Stick layout per game** (Default, Southpaw, Legacy, Legacy Southpaw), to
  match the game's own setting.
- **No more mouse lag:** the old "inertia" that spread every gesture over
  about half a second is gone. Movement that goes beyond the stick's limit is
  now carried over to the next frames instead of being lost.
- **Mouse measurement page (A4 sheet):** measure your mouse once, and the same
  sensitivity feels the same with any mouse.

## Direct aim — first working version

On supported games, the mouse writes **directly into the game camera**, like
on PC: no stick speed limit, no stick curve, no stutter.

- **Call of Duty: Modern Warfare 3** — campaign and multiplayer (base version,
  without title update).
- Other builds listed by Xenia Mousehook (Call of Duty 4, MW2, World at War,
  CoD 3, Advanced Warfare — specific versions).
- A notification tells you when direct aim is ready.

**Offline only** (campaign, split-screen, private matches). Never use it
online against other players.

## Tune while you play

- **F5/F6** sensitivity · **F7/F8** horizontal anti-deadzone · **F3/F4**
  vertical anti-deadzone · **F1/F2** smoothing · **F10/F11** dead zone.
- The value is shown on screen, applies right away and is saved **for the
  current game**.
- **F9** (hold) saves the current settings as the game's profile.
- **BACK + START** (hold) on a real controller: quick bind a controller button
  to a key or a mouse click, for the current game.

## Mouse calibration, reworked

- Each step now **moves on by itself** when the gauge is full: no more going
  to click "Validate" with the mouse, which spoiled the recorded gesture.
- Much more tolerant detection (hand drifting during a gesture, 16-bit axes,
  imperfect clicks).

## Languages and logs

- **Spanish and Portuguese (Brazil) added.** *Translated without a native
  speaker — corrections are very welcome.*
- **English is the default language**, for the app and the plugin
  notifications.
- **Plugin and Studio logs are now in English.**

## Stability and install

- **Freezes when plugging or unplugging a USB device: greatly reduced, not
  fully solved yet.** One cause was found and removed (a kernel call that
  closed a USB endpoint while a transfer was still running). Freezes can still
  happen on plug / unplug; work continues.
- **Debug output mirror off by default** (it could hang without `xbdm.xex`).
- **Notifications can be turned off** with an empty file named
  `disable_notifications.txt` in the `X360RemapStudio` folder.
- **Step-by-step startup log**, written straight to disk.
- **"Incomplete install" message fixed:** it no longer shows at every boot
  when the Studio is installed.
- **Works without a hard drive** (internal memory or USB drive).
- **The plugin project was renamed** from `hiddriver` to `X360Remap`.

---

## Known limitations

- **`xbdm.xex` must be loaded** in DashLaunch, or the console freezes when a
  USB input device is connected.
- **Don't run hiddriver360 and X360Remap at the same time.** X360Remap is a
  fork of hiddriver360; both hook the same USB functions and will conflict.
  Load X360Remap alone.
- **Freezes on USB plug / unplug** are rarer but still possible.
- After launching a game, the mouse sometimes needs to be unplugged and
  replugged once.
- While the plugin is loaded, the keyboard can't be used to type text in the
  dashboard.
- Direct aim only works on the exact game versions listed above.
