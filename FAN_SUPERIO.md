# MangoHud — Super I/O fan support (fork changes)

This is a fork of [MangoHud](https://github.com/flightlessmango/MangoHud) that adds
the ability to read **chassis / CPU / system fan RPM from Super I/O chips** via the
Linux `hwmon` interface, and to display them in the overlay.

This file documents *only* what differs from upstream MangoHud. For everything else,
see the main [`README.md`](README.md).

---

## Why

Stock MangoHud can only show two kinds of fan:

- `gpu_fan` — the GPU's own fan (AMD RPM, or NVIDIA percentage)
- `fan` — the **Steam Deck** APU fan only

It has no way to read the **motherboard fan headers** (CPU fan, case fans, etc.).
On a desktop those are wired to a Super I/O chip (Nuvoton, ITE, Fintek, …) which the
kernel exposes through `hwmon`. This fork adds a dedicated **`cfan`** element that reads
those chips, with optional manual sensor selection for multi-fan boards, while leaving
the original **`fan`** element for the Steam Deck fan.

---

## What changed

| Area | Upstream | This fork |
|------|----------|-----------|
| `fan` element | Steam Deck only | Steam Deck only (unchanged) |
| `cfan` element | n/a | **New** — auto-detected Super I/O chip fan |
| Sensor selection | n/a | New `cfan_custom_sensor` option to pick exact chip + input(s) for `cfan` |
| Multiple fans | n/a | Multiple labelled fans on `cfan` via `cfan_custom_sensor` |

### Touched source files
- `src/overlay.cpp` — `find_active_fan_input()` helper and the rewritten `update_fan()`,
  which now fills two lists: `fan_sensors` (Steam Deck) and `cfan_sensors`
  (custom / Super I/O).
- `src/overlay_params.cpp` / `.h` — the `cfan` boolean element plus the
  `fan_custom_sensor` / `cfan_custom_sensor` parameters and their shared parser
  (`parse_fan_custom_sensor`).
- `src/hud_elements.cpp` / `.h`, `src/overlay.h` — `HudElements::cfan()` render function
  and overlay plumbing for the resolved fan(s).

---

## Usage

Config lives in `~/.config/MangoHud/MangoHud.conf`.

Two independent elements — enable either or both:

| Element | Source | Label | Label color |
|---------|--------|-------|-------------|
| `fan`   | Steam Deck APU fan (`steamdeck_hwmon` → `fan1_input`) | `FAN` | `fan_color` |
| `cfan`  | Custom / Super I/O chip fan(s) | `CFAN` (or your label) | `cfan_color` |

Each element has its own label color, e.g. `cfan_color=00ff00` (defaults to the same
`eb5b5b` as `fan_color`).

### 1. The `fan` element

```ini
fan
```

By default reads the Steam Deck's `steamdeck_hwmon` sensor. On other hardware you can
point it at any hwmon sensor with `fan_custom_sensor` (same format as
`cfan_custom_sensor`, described below):

```ini
fan
fan_custom_sensor=nct6799,fan7_input,CPU
```

### 2. Super I/O auto-detect (desktops)

```ini
cfan
```

`update_fan()` resolves the sensor **once** on first frame, then refreshes its RPM
every update. `cfan` auto-detect picks the first hwmon whose `name` starts with a known
Super I/O prefix:

| Vendor   | Prefixes matched                        |
|----------|-----------------------------------------|
| Nuvoton  | `nct61`, `nct67`, `nct77` (incl. nct6799) |
| ITE      | `it86`, `it87`                          |
| Fintek   | `f7188`, `f8000`                        |
| Winbond  | `w836`, `w8362`, `w8377`                |
| SMSC     | `smsc`, `sch311`                        |

Within that chip it picks the **first spinning fan** — the lowest-numbered
`fanN_input` reporting a nonzero RPM (`find_active_fan_input()`).

> **Caveat:** auto-detect only finds one fan, and only one that is *already spinning*
> when the overlay starts. If the fan you want is idle at launch, or you have several
> fans, use `cfan_custom_sensor` below.

### 3. Manual selection for `cfan` (recommended for desktops / multi-fan)

```ini
cfan
# single fan
cfan_custom_sensor=nct6799,fan2_input

# multiple fans, with labels
cfan_custom_sensor=nct6799,fan2_input,CPU;nct6799,fan7_input,GPU
```

Format — one or more entries separated by `;`:

```
chip,input[,label]
```

- **chip** — matched against the hwmon `name` file (e.g. `nct6799`).
- **input** — the raw sysfs filename (e.g. `fan2_input`).
- **label** — optional display label. Defaults to `FAN` for a single sensor, or
  `FAN1`, `FAN2`, … when several are configured.

When `cfan_custom_sensor` is set, auto-detection is skipped and one overlay entry is
created per configured sensor.

---

## Finding your chip name and inputs

List the hwmon chips and their live fan readings:

```sh
for d in /sys/class/hwmon/hwmon*; do
  n=$(cat "$d/name" 2>/dev/null)
  echo "== $n  ($d)"
  for f in "$d"/fan*_input; do
    [ -e "$f" ] && echo "   $(basename "$f") -> $(cat "$f") rpm"
  done
done
```

Use the printed `name` as **chip** and the `fanN_input` filename as **input** in
`cfan_custom_sensor`.

If no Super I/O chip shows up, load the right kernel module (e.g.
`sudo modprobe nct6775` for Nuvoton) — and run `sudo sensors-detect` from
`lm_sensors` once if you haven't.

---

## Troubleshooting

- **Nothing shows:** run with `MANGOHUD_CONFIG=cfan` (or `fan`) and check the debug log —
  `update_fan()` emits `fan: using ...` / `cfan: using ...` / `fan: no fan sensor found`
  via `SPDLOG_DEBUG` (build/run with debug logging enabled).
- **Wrong fan picked:** auto-detect grabbed the first spinning fan — switch to an
  explicit `cfan_custom_sensor`.
- **RPM reads `-1`:** the sysfs path returned empty or non-numeric; double-check the
  exact `fanN_input` filename for that chip.
