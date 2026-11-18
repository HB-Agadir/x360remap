# Why the plugin no longer resets the USB driver (1.4)

Since 1.4 the plugin does not call UsbdPowerDownNotification, MmFreePhysicalMemory or
UsbdDriverEntry at any time. Reasons:

- The project's own note at the boot reset (2026-08-01, main.cpp) already found that the
  reset itself froze the console when a USB device was connected.
- The deferred reset (about 35 s after boot, from the polling thread) made the same calls
  from outside Usbd's own DPC. The Jacckii project (Xbox360-Series-Controllers-Native,
  docs/INTERNALS.md) states that Usbd takes no locks and must only be called from its DPC.
- Freezes in the 2026-11-12/13 tests came at or just after that reset.

Cost: after boot, a keyboard or mouse plugged in at power-on must be unplugged and plugged
back once. The plugin's own message says so.

## Update 2026-11-16 evening: rescan removed, read-only USB report instead

First boot with the rescan on: "0 parked port(s)" on both boots, so the rescan never ran and
cannot help: a keyboard / mouse plugged at power-on is not in the unrecognized table. That boot
froze at ~66 s in Aurora, ~10 s after a mouse replug, with the rescan idle (not its cause).

`RunGentleUsbRescan` now only logs ("USB report: ...") each controller, its port devices and the
fields usb-audio-x360 (TimeoutTim, src/driver.cpp `ReenumerateBootDevice`) checks before resetting
ONE root-hub port. Nothing is reset. The log decides whether that per-port method can apply.

## (historical) Gentle rescan, on by default (added 2026-11-16, HB's choice)

`RunGentleUsbRescan` in `X360Remap/main.cpp` calls `UsbdTitleDriverResetAllUnrecognizedPorts`
(ordinal 890) on Usbd's own processor through a KDPC of ours, as Jacckii's project does. It only
re-offers ports no driver took (the table at 0x801A7FA0); it does not reset the USB driver.

- Runs once, 10 s after the boot title hands back to the dashboard, 3 s after the claim blackout.
- Only on the exact 17559 code (address and prologue checked); otherwise it does nothing.
- Always logs the parked ports ("USB rescan before: ..."), read only.
- The rescan runs unless `disable_usb_rescan.txt` exists in the Studio folder (the off switch
  if it ever freezes a console).

Not known yet: whether a keyboard or mouse plugged at power-on sits in that table at all (the
console's own HID driver may have taken it). The first boot's log answers that.

## 2026-11-18: per-port reset, opt-in, first success (HB's Slim)

Port map of HB's Slim (plugin re-runs the USB report 2 s after each device arrives, or when
`usb_report.txt` is put in the Studio folder): front right = 3:0, front left = 3:1, rear =
2:2, 2:3, 2:0; controller 3 port 3 = an internal device (always there); controller 1 ports 2
and 4 = Microsoft 045e:0765 (the Wi-Fi card on its internal port). Controllers 1 and 3 share
one node pool; controllers 0 and 2 the other.

`usb_port_reset.txt` (one `controller:port` per line) lists the ports to reset once per boot,
one every 3 s, after the boot report (~38 s). Same call as usb-audio-x360
(`ResetRootHubPort(root, port)`, kernel 17559, prologue checked). No file = nothing reset.
The VID read on a port is the controller's last enumerated device, not that port's: it is not
used to decide anything.

First result with `3:0` and `3:1`: 3:0 reset at 38.6 s, the Rapoo keyboard and mouse plugged at
power-on came back through HidAddDevice 0.13 s later, no freeze, network still up. 3:1 was empty.
Next: 3:1 with a device, then the rear ports one by one; then a port-map step in the Studios.
