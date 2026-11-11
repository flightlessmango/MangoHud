# GameSir G7 Pro 8K battery workaround

Some GameSir receivers expose an Xbox-compatible gamepad without exposing a
battery in `/sys/class/power_supply`. `device_battery=gamepad` consequently has
no reading to display. PS4 emulation can expose a battery but report a constant,
incorrect 5%. See [issue #2199](https://github.com/flightlessmango/MangoHud/issues/2199).

`bin/gamesir-battery.py` is an optional external helper for the **G7 Pro 8K PC
receiver `3537:10c6` in 2.4 GHz XInput mode**. Other GameSir models, receiver IDs,
Bluetooth, wired mode and PS4 mode are not supported by this helper. It is not
automatically installed or started by MangoHud.

The helper sends a 64-byte `0x0f, 0xf2` heartbeat every half second to request
GameSir's enhanced reports. It reads the percentage from byte 36 (zero based)
of the 64-byte `0x12` report, and charging from byte 35 bit 0. It does **write**
to the vendor HID interface, but does not save configuration, switch modes,
detach `xpad`, or claim exclusive access. MangoHud itself only runs a command
to read the helper's cached result; the overlay never opens the USB device.

This protocol was identified using the
[GameSir Linux tools project](https://github.com/broroeror/gamesir-linux-tools).
Deadband does not need to be installed or running. The workaround reported
100% on the fully charged controller in #2199; this is not a calibration or
validation of every battery level or the charging flag.

## Setup

Install Python 3 and the `hidapi` Python bindings, which expose `hid.device()`.
On Arch Linux/CachyOS the bindings are packaged as `python-hidapi`. Run the
helper as your desktop user, with read/write permission for the receiver's
vendor `/dev/hidraw*` node. Do not run the game or MangoHud as root. A system
udev rule, if needed, can grant the logged-in user access to this specific
receiver:

```udev
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3537", ATTRS{idProduct}=="10c6", TAG+="uaccess"
```

An administrator can place that rule in
`/etc/udev/rules.d/70-gamesir-g7-pro-8k.rules`, reload the udev rules and
reconnect the receiver. This grants access to the receiver's HID interfaces,
not just its battery data. No rule is needed if your existing device rules
already grant the required access.

From a MangoHud source checkout, first check the hardware reading:

```sh
python3 bin/gamesir-battery.py --once
```

Then leave one copy running outside the game's sandbox:

```sh
python3 bin/gamesir-battery.py
```

It writes atomically to `$XDG_CACHE_HOME/MangoHud/gamesir-battery.txt`, defaulting
to `~/.cache/MangoHud/gamesir-battery.txt`. Reconnecting the receiver is handled
automatically. `--status` returns `Disconnected` for a missing file or a reading
older than ten seconds, even if the helper was killed unexpectedly.

Add this to your MangoHud config, replacing `/absolute/path/to/MangoHud` with
your checkout's location:

```ini
legacy_layout=0
custom_text=G7 Pro
exec=python3 /absolute/path/to/MangoHud/bin/gamesir-battery.py --status
```

`--status` needs only Python, not the HID bindings or device access. Keep the
script and cache accessible inside the game runtime. If the game sees a
different home directory or `XDG_CACHE_HOME`, pass the same explicit
`--output /absolute/shared/path/battery.txt` to the reader and `--status`.
Quote paths containing spaces. This uses a custom text element rather than
the built-in `device_battery` element; keep `device_battery=gamepad` only if you
also want readings for other controllers.

To start the helper at login, install this as
`~/.config/systemd/user/gamesir-battery.service`, replacing the script path:

```ini
[Unit]
Description=GameSir G7 Pro 8K battery reader for MangoHud

[Service]
ExecStart=/usr/bin/python3 /absolute/path/to/MangoHud/bin/gamesir-battery.py
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
```

Stop the foreground reader, then run:

```sh
systemctl --user daemon-reload
systemctl --user enable --now gamesir-battery.service
```

To stop automatic reading, use
`systemctl --user disable --now gamesir-battery.service` and remove the custom
MangoHud config lines. Do not run multiple battery readers for the same receiver.

## Tests

No controller or HID bindings are required for the mocked protocol tests:

```sh
python3 -m unittest discover -s tests -p 'test_gamesir_battery.py' -v
```
