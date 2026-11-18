# 1.4.2: mouse to stick, the GIMX way (plan, 2026-11-18)

Branch `v1.4.2-gimx`, from the backup commit `09768d4` (tag `backup-1.4-pre1-20261118`).

Why: direct aim (writing the game's camera) only works game by game and failed on Rogue
Warrior (the values found are copies the game rewrites every frame). GIMX (GPL-3, same as
X360Remap) drives real consoles with a mouse only through the stick, smoothly. Source read:
`matlo/GIMX` `core/config.c` (`mouse2axis1d`, motion buffer and filter).

## What GIMX does, per poll

1. Motion = weighted average of the last `buffer_size` mouse samples, weights 1, f, f², ...
   (`filter` f in 0..1). Plus the residue of the previous poll.
2. `z = multiplier * |v|^exponent` (sign kept).
3. Aiming mode: `axis = dead_zone + z` (the game's own stick dead zone is jumped over), clamped.
4. Residue = the motion not turned into stick movement (clamp and integer rounding), exact
   inverse of step 2: `|v| - (|axis - dz| / multiplier)^(1/exponent)`; carried to the next poll,
   dropped when the mouse stops. This is what makes it feel 1:1.
5. The dead zone is measured per game: raise it until the camera starts to turn (DZX / DZY).

## What X360Remap already has (X360Remap/main.cpp, mouse path)

| GIMX | X360Remap today |
|---|---|
| buffer + filter | `smoothingMs` (time-based smoothing) |
| exponent on the motion | `ApplySensitivityCurve` on the output (CURVE_EXPONENTIAL, rarely set) |
| dead zone jump | `antiDeadzone` / `antiDeadzoneY` (F7/F8, F3/F4 in game) |
| residue | `ApplyOverflowCarry`: only what goes past full tilt, not the rounding |
| per-game dead zone calibration | none (F-key tuning by hand) |
| ready settings for 11 X360 games | none |

## Steps

1. Quick check with what exists, Rogue Warrior: anti-deadzone up with F7 (H) and F3 (V) until
   the camera starts to move with a tiny mouse motion, smoothing F1/F2. Tells how much of the
   stiffness is the game's dead zone.
2. `GimxMouseToStick` in mapping.cpp: pure function (state: last samples, residue), unit test
   in X360Remap/tests. Used when the profile says `"mouseMode": "gimx"`; old path unchanged
   otherwise.
3. JSON fields per device / game profile: `gimxDeadZone`, `gimxMultiplierX/Y`, `gimxExponent`,
   `gimxBuffer`, `gimxFilter` (config_writer + mapping.cpp parser, CONTRACT.md).
4. In-game dead zone calibration (like GIMX DZX/DZY): a shortcut raises the dead zone step by
   step until the player confirms the camera moved.
5. Presets from `matlo/GIMX-configurations` (Windows/X360_*.xml, Halo 3/4/Reach/CEA, Gears 1-3,
   Battlefield 2 MC, Black, GoldenEye Reloaded, Perfect Dark) as game profiles, credited.
6. Studios: mode switch and the new settings on the Keyboard & mouse page.

Each step is tested on HB's console before the next.
