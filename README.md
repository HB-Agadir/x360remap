# X360Remap

**Mouse and keyboard on a real Xbox 360** (RGH / JTAG, DashLaunch). No adapter,
no dongle: a DashLaunch plugin turns your USB mouse and keyboard into a
controller, and an on-console app lets you set everything up.

A fork of [hiddriver360](https://github.com/EinTim23/hiddriver360) by EinTim23.

![X360Remap](X360_Remap_cover.jpg)

---

## ⚠️ Read before installing

- **This is a fork of hiddriver360. Do not load both plugins.** They hook the
  same USB functions and will conflict. Remove hiddriver360 from
  `launch.ini` and load **X360Remap alone**.
- **`xbdm.xex` is required.** It must be loaded in DashLaunch as well. Without
  it, the console freezes as soon as a USB input device is connected.
- **Freezes on USB plug / unplug have been greatly reduced but are not fully
  solved yet.** Plug your devices in before starting a game when you can.
- **Offline only.** X360Remap is made for single-player, split-screen and
  private matches — not for playing online against other people.

---

## What's in v1.2

v1.2 is the **third public release** and the biggest one so far:

- **A brand-new Studio** — keyboard & mouse page, game profiles with Aurora
  covers, how-it-works and support pages, 4 languages.
- **Better mouse aim** — smoothing, horizontal / vertical anti-deadzone,
  response curve, stick layouts, no more lag.
- **Tune while you play** — F keys change sensitivity, smoothing and
  anti-deadzone in game, saved per game.
- **Direct aim (first version)** — the mouse drives the game camera directly,
  like on PC. Working on Call of Duty: Modern Warfare 3 (campaign and
  multiplayer, base version).

Full list: [CHANGELOG.md](CHANGELOG.md) · What's next: [ROADMAP.md](ROADMAP.md)

---

## Install

Download the release archive and unzip it. It contains:

```
X360Remap.xex                       -> the plugin, copy to Hdd1:\
X360RemapStudio\X360RemapStudio.xex -> the app, copy the folder to Hdd1:\
README.html                         -> this guide, in 5 languages
cover\                              -> cover picture for Aurora (optional)
```

1. Copy `X360Remap.xex` and the `X360RemapStudio` folder to the root of your
   hard drive (`Hdd1:\`).
2. In `launch.ini`, load the plugin **and xbdm**, and remove hiddriver360 if
   present, for example:
   ```
   plugin1 = Hdd1:\xbdm.xex
   plugin2 = Hdd1:\X360Remap.xex
   ```
   (Keep the paths your own setup already uses for xbdm.)
3. Reboot the console.
4. In Aurora, add `Hdd1:\X360RemapStudio` as an application path and launch
   **X360RemapStudio** once. It creates its settings by itself.
5. Optional: set the cover from the `cover` folder in Aurora (edit artwork).

Both files are required. With the plugin alone, default controls work but the
Studio is what saves your settings and profiles.

---

## Build from source

Visual Studio 2010 toolset with the Xbox 360 SDK. Open `X360Remap.sln`,
configuration **Release Retail | Xbox 360**. It produces:

- `X360Remap\Release Retail\X360Remap.xex`
- `application\Release Retail\X360RemapStudio.xex`

---

## Support the project

X360Remap is a free, solo, spare-time project. Donations go first to the
hardware needed for the next milestones (other controllers, a racing wheel,
an arcade stick, a capture card for demo videos) — see
[ROADMAP.md](ROADMAP.md#community-goals).

- Ko-fi: <https://ko-fi.com/x360remap>
- PayPal: <x360remap@gmail.com>

Bug reports, logs and test results are just as valuable: open an issue.

---

## Credits

- [hiddriver360](https://github.com/EinTim23/hiddriver360) by EinTim23
  (GPL-3.0) — the USB HID groundwork this fork builds on.
- [Xenia Canary Mousehook](https://github.com/Clippy95/xenia-canary-mousehook)
  (BSD) — camera addresses for several Call of Duty builds.
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [RapidJSON](https://github.com/Tencent/rapidjson) (MIT),
  [stb_image](https://github.com/nothings/stb) (public domain / MIT).
- Controller and keyboard icons: [Kenney](https://kenney.nl) (CC0).
- Fonts: Barlow Condensed, Inter (SIL Open Font License).

## License

GPL-3.0 — see [LICENSE](LICENSE).
