#!/usr/bin/env python3
"""Generate known_controllers_db.cpp from gamecontrollerdb_windows.txt.

    python tools/gen_known_controllers.py

Only Windows DirectInput entries of USB devices are kept: their GUID is
03000000VVVV0000PPPP000000000000 (bus 0x0003, VID and PID little-endian), and
their numbering is the one the plugin can reproduce from the HID descriptor:

  bN  = HID button usage N+1 (DirectInput lists buttons in usage order)
  aN  = Nth axis present, in DirectInput order: X Y Z Rx Ry Rz, then
        sliders (slider / dial / wheel). Resolved at plug-in time from the
        device's own descriptor (see AttachKnownController).
  h0.1/2/4/8 = the hat switch; the plugin always reads the first hat, so a
        standard hat D-pad needs nothing in the table.

Skipped (no equivalent in the plugin): half axes (+a2 / -a2), axes or hats
used as buttons, D-pad on axes, a second hat, buttons used as axes.
The first entry wins when a VID:PID appears more than once.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# The full community file first when it is there (newest data wins), then the
# saved Windows extract.
SRCS = [os.path.join(HERE, "gamecontrollerdb.txt"),
        os.path.join(HERE, "gamecontrollerdb_windows.txt")]
OUT = os.path.join(os.path.dirname(HERE), "known_controllers_db.cpp")

# Order of KnownControllerEntry::btn[] (= kButtonFieldNames in mapping.cpp).
BUTTONS = ["a", "b", "x", "y", "leftshoulder", "rightshoulder",
           "lefttrigger", "righttrigger", "back", "start",
           "leftstick", "rightstick", "guide",
           "dpleft", "dpright", "dpup", "dpdown"]
# Order of KnownControllerEntry::axis[].
AXES = ["leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"]
HAT_STD = {"dpup": "h0.1", "dpright": "h0.2", "dpdown": "h0.4", "dpleft": "h0.8"}

# Bytes 2-3 may hold SDL's name CRC (newer database lines): any value accepted.
GUID_RE = re.compile(r"^0300[0-9a-f]{4}([0-9a-f]{4})0000([0-9a-f]{4})000000000000$")


def le16(h):
    return int(h[2:4] + h[0:2], 16)


def c_string(s):
    s = s.encode("ascii", "replace").decode("ascii")
    s = s.replace("\\", "/").replace('"', "'")
    return '"' + s[:40] + '"'


def parse(line):
    parts = [p for p in line.strip().split(",") if p != ""]
    if len(parts) < 3:
        return None
    guid, name = parts[0].lower(), parts[1].strip()
    m = GUID_RE.match(guid)
    if not m:
        return None
    vid, pid = le16(m.group(1)), le16(m.group(2))
    if vid == 0 or pid == 0:
        return None
    fields = {}
    for p in parts[2:]:
        if ":" not in p:
            continue
        k, v = p.split(":", 1)
        fields[k.strip()] = v.strip()
    if fields.get("platform") != "Windows":
        return None

    btn = [-1] * len(BUTTONS)
    axis = [-1] * len(AXES)
    invert = 0
    hat_ok = True
    for i, role in enumerate(BUTTONS):
        v = fields.get(role)
        if v is None:
            continue
        mb = re.fullmatch(r"b(\d+)", v)
        if mb and int(mb.group(1)) < 64:
            btn[i] = int(mb.group(1))
        elif role in HAT_STD and v != HAT_STD[role]:
            hat_ok = False
    for i, role in enumerate(AXES):
        v = fields.get(role)
        if v is None:
            continue
        ma = re.fullmatch(r"a(\d+)(~?)", v)
        if ma and int(ma.group(1)) < 8:
            if role in ("lefttrigger", "righttrigger") and btn[BUTTONS.index(role)] >= 0:
                continue
            axis[i] = int(ma.group(1))
            if ma.group(2):
                invert |= 1 << i
    usable = sum(1 for b in btn if b >= 0) >= 4 or sum(1 for a in axis if a >= 0) >= 2
    if not usable:
        return None
    return dict(vid=vid, pid=pid, name=name, btn=btn, axis=axis, invert=invert,
                hat_ok=hat_ok)


def merge(group):
    """Several lines for one VID:PID (many cheap pads share a chip): keep, for
    each button / axis, the value most lines agree on (ties: the first line).
    One odd line no longer decides for everyone (e.g. 0079:0006)."""
    from collections import Counter
    first = group[0]
    if len(group) == 1:
        return first
    out = dict(first)
    out["btn"] = []
    for i in range(len(BUTTONS)):
        c = Counter(e["btn"][i] for e in group)
        best = max(c.values())
        out["btn"].append(next(e["btn"][i] for e in group if c[e["btn"][i]] == best))
    out["axis"] = []
    inv = 0
    for i in range(len(AXES)):
        vals = [(e["axis"][i], (e["invert"] >> i) & 1) for e in group]
        c = Counter(vals)
        best = max(c.values())
        a, b = next(v for v in vals if c[v] == best)
        out["axis"].append(a)
        inv |= b << i
    out["invert"] = inv
    out["name"] = first["name"] + (" (+%d similar)" % (len(group) - 1))
    return out


def main():
    groups, order, dup, skipped = {}, [], 0, 0
    lines = []
    for src in SRCS:
        if os.path.exists(src):
            lines += open(src, encoding="utf-8", errors="replace").read().splitlines()
            print("read", os.path.basename(src))
    seen_guid_lines = set()
    for line in lines:
        if not line.strip() or line.startswith("#"):
            continue
        e = parse(line)
        if e is None:
            skipped += 1
            continue
        sig = line.strip().lower()
        if sig in seen_guid_lines:      # same line in both files
            dup += 1
            continue
        seen_guid_lines.add(sig)
        key = (e["vid"], e["pid"])
        if key not in groups:
            groups[key] = []
            order.append(key)
        else:
            dup += 1
        groups[key].append(e)
    entries = [merge(groups[k]) for k in order]
    entries.sort(key=lambda e: (e["vid"], e["pid"]))

    out = []
    out.append("// GENERATED by tools/gen_known_controllers.py - do not edit by hand.")
    out.append("// Source: tools/gamecontrollerdb_windows.txt (SDL Game Controller Database,")
    out.append("// zlib license - see that file). Converted for X360Remap: altered from the")
    out.append("// original format.")
    out.append("//")
    out.append("// %d controllers (%d duplicate VID:PID and %d unusable lines skipped)." %
               (len(entries), dup, skipped))
    out.append('#include "known_controllers.h"')
    out.append("")
    out.append("const KnownControllerEntry kKnownControllers[] = {")
    out.append("    // vid, pid, buttons (a b x y lb rb lt rt back start l3 r3 guide dl dr du dd),")
    out.append("    // axes (lx ly rx ry lt rt), inverted axes, name")
    for e in entries:
        out.append("    { 0x%04X, 0x%04X, {%s}, {%s}, 0x%02X, %s }," % (
            e["vid"], e["pid"],
            ",".join(str(b) for b in e["btn"]),
            ",".join(str(a) for a in e["axis"]),
            e["invert"], c_string(e["name"])))
    out.append("};")
    out.append("const unsigned kKnownControllerCount = sizeof(kKnownControllers) / sizeof(kKnownControllers[0]);")
    out.append("")
    with open(OUT, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out))
    print("%d controllers written to %s (dup %d, skipped %d)" % (len(entries), OUT, dup, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
