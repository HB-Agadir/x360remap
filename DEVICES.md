# Supported devices — and the ones we want to support next

X360Remap turns USB devices into an Xbox 360 controller. Every device has to be
tested **on a real console** before we call it supported, so this list is
honest about what has really been tested.

> **Offline only.** X360Remap is for single-player, split-screen and private
> matches. It is not a cheating tool.

| Status | Meaning |
|---|---|
| ✅ Works | Tested on a real console |
| 🧪 Testing | Works in the current pre-release, being tested by players |
| 🟡 Should work | Already in the code, **nobody has tested it yet** — we need you |
| 🔧 Planned | We know how to do it; we need the hardware to test it |
| 🔍 Research | Possible, but needs more work first |

---

## Keyboards, mice, gaming keypads

| Device | Status | Notes |
|---|---|---|
| Standard USB keyboards | ✅ Works | Keyboard + mouse = one player, default WASD layout |
| Standard USB mice | ✅ Works | Several models tested; wheel and extra buttons depend on the model |
| Gaming keyboards with N-key rollover (Razer, Corsair, Logitech, SteelSeries…) | 🧪 Testing | Fixed in 1.3 pre-release |
| Razer Tartarus V2 / Tartarus Pro | 🧪 Testing | Ready-made layout: key 01 = START, 05 = Guide, Shift + Alt + X = on/off |
| Mice with many side buttons (Logitech G502…) | 🔧 Planned | |
| Other gaming keypads (Razer Orbweaver, Azeron…) | 🟡 Should work | They act as keyboards — tell us how it goes |

## Controllers

| Device | Status | Notes |
|---|---|---|
| Xbox 360 controllers | ✅ Works | Native, X360Remap leaves them alone |
| PlayStation 3 — DualShock 3 | 🟡 Should work | In the code, never tested |
| PlayStation 4 — DualShock 4 (v1, v2, wireless adapter) | 🟡 Should work | In the code, never tested |
| PlayStation 5 — DualSense / DualSense Edge | 🟡 Should work | In the code, probably needs fixes |
| Nintendo Switch Pro Controller | 🟡 Should work | In the code, never tested |
| PC USB controllers in the SDL Game Controller Database (Logitech F310/F510/F710 in DirectInput mode, Dual Action, Betop, Thrustmaster, PS2/PS3 adapters… about 860 models) | 🟡 Should work | Mapped the moment they are plugged in, no wizard. The log says `Known controller VID:… "name"`. Tell us if a button is wrong |
| Other generic USB controllers | 🧪 Testing | Through the controller wizard |
| "USB Joystick" `0079:0006` (Microntek / DragonRise, many cheap PC pads) | ✅ Works | Tested on a real console (1.3-pre13): detected, all buttons and sticks |
| Xbox One / Series controllers | 🔍 Research | Not a standard USB HID device: needs its own driver |
| PC "XInput" controllers (8BitDo, GameSir…) | 🔍 Research | The 360 refuses them on its own; we may be able to fix that |

## Racing wheels, arcade sticks, others

| Device | Status | Notes |
|---|---|---|
| Logitech wheels (Driving Force, G25, G27, G29) | 🔧 Planned | Basic mode should work through the wizard; full wheel range and separate pedals need the real wheel |
| Thrustmaster wheels | 🔧 Planned | |
| USB arcade sticks (PC / PS3 type) | 🟡 Should work | Through the controller wizard; fighting-game shortcuts planned |
| Flight sticks, dance pads, guitars | 🔍 Research | |

---

## Your device is not listed?

1. Plug it in, then send us `X360Remap_plugin.log` from `HDD:\X360RemapStudio\`
   (open an issue on GitHub or post on the Se7enSins thread).
2. Tell us what works and what doesn't. Even "nothing works" is useful: the log
   shows what the console sees.

---

## How you can help

Most of the "Planned" lines above are waiting for one thing: the hardware.

- **Test.** Got one of the 🟡 devices? You are the tester we need. A log and two
  lines of feedback are enough.
- **Support the hardware fund.** Donations go first to the devices on this page
  (a racing wheel, an arcade stick, PlayStation controllers, an Xbox One
  controller). Every device bought gets tested and added here.
  Ko-fi: <https://ko-fi.com/x360remap>
- **Code with your own AI assistant.** X360Remap is largely built with Claude
  (Anthropic). If you have a Claude account (or another AI coding assistant) and
  a modded console, you can take one device from this list, work on it in your
  fork and open a pull request. Start with [ARCHITECTURE.md](ARCHITECTURE.md);
  tell us which device you pick so two people don't work on the same one.

Thank you — every log, test and contribution makes this list longer.

---

The list of known PC controllers comes from the
[SDL Game Controller Database](https://github.com/mdqinc/SDL_GameControllerDB)
(zlib license), converted for X360Remap by `X360Remap/tools/gen_known_controllers.py`.
