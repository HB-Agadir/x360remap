# X360Remap — mouse & keyboard for the Xbox 360

Play Xbox 360 games with a USB mouse and keyboard. Works out of the box with
any mouse, and remembers different settings for each game.

A fork of **hiddriver360** by EinTim23.

---

## READ THIS FIRST — the package contains TWO files, both required

| File | What it does |
|------|--------------|
| `X360Remap.xex` | The **plugin** — goes in `launch.ini`. Reads your mouse/keyboard, feeds it to the game as controller input. |
| `X360RemapStudio.xex` | The **application** — launched from Aurora, *never* from `launch.ini`. Creates your settings, calibrates your mouse, manages per-game profiles. |

Install only the plugin and nothing will happen in-game — it will look like the
project is broken. It isn't; the install is just half done. The plugin now says
so on screen when that happens.

---

## What makes it different from a USB adapter

**Per-game profiles, applied automatically.** The plugin reads the ID of the
game you launch and switches to that game's sensitivity, deadzone and button
layout on its own. A hardware adapter sits on the other end of the cable — it
has no idea what you're playing. This is the one thing it structurally cannot
do.

**It learns your actual mouse.** Cheap and generic mice rarely report their
buttons and axes in the standard layout, which is why they often behave oddly.
Instead of guessing, the calibration wizard asks you to move the mouse and
click, and reads the real layout from your device.

**Remap without leaving the game** — see Quick bind below.

---

## Works immediately, with no configuration

These defaults apply to **any** mouse and keyboard, before you configure
anything.

**Mouse**

| Input | Controller |
|---|---|
| Movement | Right stick |
| Left click | RT |
| Right click | LT |
| Middle click | Right stick click |
| Wheel | D-Pad up/down |

**Keyboard**

| Keys | Controller | Typical use |
|---|---|---|
| WASD | Left stick | Move |
| Arrow keys | D-Pad | |
| Space | A | Jump |
| C | B | Crouch |
| R | X | Reload |
| E | Y | Interact / use |
| Q | LB | Grenade / ability |
| F | RB | Melee |
| Left Shift | LT | (aim, brake…) |
| Left Ctrl | RT | (fire, accelerate…) |
| Left Alt | L3 | Sprint |
| V | R3 | Melee |
| Esc | Start | Menu |
| Tab | Back | Map / scoreboard |
| Right Alt | Guide | Xbox menu |
| IJKL | Right stick | (the mouse drives it too) |

> **Note:** while the plugin is loaded, the keyboard can no longer be used to
> type text (in the dashboard search box, for instance). This is inherent to
> how the keyboard is captured, and is the same behaviour as the keyboard fork
> this one builds on.

---

## Two things you can do in-game, without opening the app

**Quick bind — hold BACK + START**

Rebind a button in the middle of a game, without quitting it:

1. Hold **Back + Start**. A message confirms quick bind is armed.
2. Press the **controller button** you want to change.
3. Press the **key or mouse click** you want to assign to it.

The binding is saved for that game only.

**Save mouse feel — hold F9**

Hold **F9** for a few seconds during a game: the current sensitivity, deadzone,
inversion and curve are stored as that game's profile, and applied
automatically next time you launch it.

(F9 is free in the default layout. Avoid using it if you build your own
keyboard mapping.)

---

## Requirements

- An Xbox 360 with RGH / JTAG, or BadUpdate.
- **DashLaunch**, to load the plugin.
- **`xbdm.xex` loaded as a plugin — not optional.** Without it, the console
  freezes when a USB mouse or keyboard is connected. This was established by
  elimination testing on real hardware: the cause is in the console's kernel,
  below anything this plugin can reach. Most RGH setups already load it, in
  which case there is nothing to change.
- Storage: a hard drive, internal memory, or a USB drive.

---

## Installation

**1.** Copy `X360Remap.xex` onto the console, for example `Hdd:\X360Remap.xex`.

**2.** Add it to `launch.ini` on a free plugin slot:

```
plugin1 = Hdd:\X360Remap.xex
```

If `plugin1` is taken, use `plugin2`, `plugin3`... Make sure `xbdm.xex` is
loaded on one of the slots as well.

**3.** Copy `X360RemapStudio.xex` wherever you keep homebrew, and add it to
Aurora (or your launcher) like any other app.

> ⚠️ **Do NOT put `X360RemapStudio.xex` in `launch.ini`.** Only the plugin
> (`X360Remap.xex`) goes there. The Studio is an application you launch from
> Aurora, like a game. Loading it as a DashLaunch plugin makes it start inside
> the dashboard process, where it captures controller input — the usual
> symptom is a controller that stops responding to everything except the Xbox
> Guide button.

**4.** Reboot the console.

**5.** Run X360RemapStudio once. This creates its folder and your
configuration. Until then, the plugin has nothing to load.

Then plug in your mouse and/or keyboard — before or after powering on, either
works. A notification confirms detection. Devices already connected at boot are
picked up automatically about 25 seconds after power-on, once the dashboard has
loaded; you don't need to unplug them.

**Known limitation:** after launching a game, the mouse sometimes needs to be
unplugged and replugged once. The system-level attachment doesn't always
survive a title change.

---

## In the configuration app

- **Keyboard wizard** — assign all 21 keys, guided step by step.
- **Mouse settings** — sensitivity, deadzone, Y inversion, movement inertia,
  and a live view of what the plugin is receiving from your device.
- **Per-game profiles** — override buttons, keys and mouse feel per game, from
  a list of the games you've actually launched.
- **Device selection** — pick which mouse or keyboard to configure when several
  are connected.

Available in English, French and Portuguese.

---

## Where your settings live

In a folder named `X360RemapStudio`, created automatically on your storage
device — normally the hard drive.

**If your console has no hard drive** (a 4 GB Slim used with BadUpdate, for
example), X360Remap now looks for internal memory or a USB drive instead, and
uses the first one it can write to. This is recent and has had very little
testing on real hardware: if it works — or doesn't — on a console without a
hard drive, please say so. The plugin writes which device it picked into its
log file, which makes that easy to report.

---

## If something doesn't work

**The console freezes when you plug in a mouse or keyboard**
Check that `xbdm.xex` is loaded. This is by far the most common cause.

**Nothing happens at all, no notification**
Check the plugin is really listed in `launch.ini`, and that you rebooted after
editing it.

**"run X360RemapStudio once to finish setup" on screen**
The plugin works with its default controls, but the app has never been run
on this console. Launch X360RemapStudio once: it creates the configuration
and the message stops.

**"X360Remap.json invalid - check syntax"**
Your configuration file exists but is damaged. Delete it and run the app again
to recreate it.

**The mouse is detected but moves strangely, or buttons are wrong**
Run the calibration wizard in the app. Generic mice frequently don't follow the
standard layout, and the wizard reads yours directly instead of assuming.

**The keyboard is detected but does nothing**
Some keyboards — often full-NKRO gaming models — don't declare the simple
"boot" mode the console expects. Support for those was added recently and is
still being validated; reports are welcome.

When reporting anything, the `.log` files in the `X360RemapStudio` folder are
the single most useful thing you can attach.

---

## Credits

X360Remap is a fork of **hiddriver360** by **EinTim23**, which did the hard
work of getting HID devices talking to the console in the first place. The
default keyboard layout comes from the keyboard fork by
**UncreativeXenon**. This fork adds mouse support, the configuration app,
per-game profiles, the calibration wizard and quick bind.

Thanks to everyone who tested, reported freezes, sent logs and questioned
assumptions — several fixes in this build exist only because someone described
precisely what happened on their console.

Licensed under GPL v3, like the project it derives from.
