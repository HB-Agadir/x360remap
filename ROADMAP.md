# X360Remap — Roadmap

**Play your Xbox 360 your way.** Keyboard, mouse, any controller, a wheel or an
arcade stick — on a real console, for free, open source.

This page is where we share where the project is going. Nothing below is a
promise with a date: each milestone ships when it works on real hardware, not
before. If one of these matters to you, tell us — and if you can, help us get
the hardware to test it (see [Community goals](#community-goals)).

> **Offline only.** X360Remap is built for single-player, split-screen and
> private matches. It is not a cheating tool, and we will never support using
> it online against other players.

---

## Where we are — v1.2

v1.2 is the **third public release** (after v0.1.0 and v0.1.1, August 2026)
and the biggest step so far: a new Studio, per-game profiles, in-game tuning
and the first working direct aim.

- **Keyboard + mouse as one player**, detected automatically.
- **X360Remap Studio**, the on-console app: key mapping, mouse settings, game
  profiles with Aurora covers, in-game tuning, 4 languages.
- **Better mouse aim through the stick:** smoothing, separate horizontal /
  vertical anti-deadzone, response curve, stick layouts (Southpaw, Legacy).
- **In-game tuning:** adjust sensitivity, smoothing and anti-deadzone with the
  F keys while you play; saved per game.
- **Direct aim — proof of concept.** The mouse moves the game camera
  directly, like on PC: no stick speed cap, no stick curve, no stutter.
  Working today on **Call of Duty: Modern Warfare 3** (campaign and
  multiplayer, base version), plus the builds listed by Xenia Mousehook.

---

## Milestones

### 1. Controllers, fully supported
- Remap every button of the Xbox 360 controller, per game.
- Layers and macros (hold a button to switch to a second layout).
- Stick tuning for real controllers (deadzone, curve).
- **Use a controller to navigate the Studio.**

### 2. Direct aim for more games
- **Automatic detection:** the plugin finds the camera in memory by itself,
  from the Studio, in about a minute. MW3 is the reference test.
- Aim-down-sight sensitivity, detected automatically.
- **Community database:** share the games you got working and your best
  settings; every game in the database gets a default camera profile.
- Direct aim becomes one option among others: when a game is known, it just
  works; when it is not, the improved stick mode takes over.

### 3. More controllers and brands
- PlayStation (DualShock 3/4, DualSense), Nintendo Switch Pro, 8BitDo and
  generic USB pads.
- **Racing wheels and pedals** (steering, throttle and brake on the right
  axes).
- **Arcade sticks and fight pads** for fighting games.
- Custom and accessibility controllers.

### 4. Mixed play
- **Controller + mouse together:** move with the controller, aim with the
  mouse — the best of both.
- Several devices, one player, any combination.

### 5. Beyond FPS
Profiles and presets for every kind of player:
- **Racing** — wheels, pedals, analog triggers.
- **Fighting** — arcade sticks, precise D-pad.
- **Sports, adventure, platformers and 2D** — the controller you prefer, the
  layout you want.

### 6. Homebrew and emulators
- **A simple interface for homebrew apps:** any Xbox 360 homebrew (emulator
  frontends, ports, tools) could read the real mouse and keyboard from
  X360Remap instead of shipping its own driver — one plugin, no conflicts.
- **Emulators that want a mouse or a real keyboard:** Quake, Doom and DOS
  games; home computers (C64, Atari, MSX) with a real keyboard instead of an
  on-screen one; light-gun games driven by the mouse; mouse-compatible
  PlayStation 1 games.
- **Original Xbox games (backward compatibility):** they read the controller
  through the same system, so keyboard and mouse remapping should already
  apply — to be tested and documented.

### 7. Other consoles — exploratory
Porting the core (mapping engine, profiles, Studio) to **PlayStation 3**
(custom firmware plugins) and later **PlayStation 4** is something we would
love to try. It is a new project for each console, so it only starts once the
Xbox 360 version is mature.

---

## Community goals

Most of these milestones need hardware we do not own yet. Every device has to
be tested on a real console before it is supported.

| Goal | Unlocks |
|---|---|
| HDMI capture card | Clear demo videos and faster bug reports |
| PlayStation and Switch controllers | Milestone 3 — other brands |
| A USB racing wheel + pedals | Racing support |
| An arcade stick | Fighting-game support |
| A second console (different model) | Testing on Slim / E and fixing model-specific issues |
| A PlayStation 3 | Milestone 6 exploration |

You can help by:
- **Testing** and sending your logs and settings;
- **Sharing** what works in your games (the future community database);
- **Supporting the project** (link on the Support page of the Studio and in the
  README) — donations go to this hardware first.

---

## Credits

X360Remap is a fork of [hiddriver360](https://github.com/EinTim23/hiddriver360)
(GPL-3.0). Direct-aim addresses for several Call of Duty builds come from
[Xenia Canary Mousehook](https://github.com/Clippy95/xenia-canary-mousehook)
(BSD). Controller icons: Kenney (CC0).
