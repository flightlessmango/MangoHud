# MangoHud Next Configuration

MangoHud Next reads its configuration from:

```text
$XDG_CONFIG_HOME/MangoHud/MangoHud.yml
```

If `XDG_CONFIG_HOME` is not set, the fallback path is:

```text
~/.config/MangoHud/MangoHud.yml
```

If the file does not exist, or if the HUD table is invalid, MangoHud Next falls back to the built-in default table in `server/config.h`.

## Configuration Format

The config file is YAML. `options` is optional. A layout can be defined as `hud.windows`, or as `hud_table` for a single default window.

```yaml
options:
  font_size: 24
  fps_limit: 0.0

hud:
  windows:
    - position: [10, 10]
      padding: 8
      background: true
      table:
        rows:
          - [ {text: GPU}, {value: [GPU, 0, LOAD]} ]
```

`hud_table.rows` is required when using the single-window form. `hud.windows[].table.rows` is required when using the window form.

The config file is watched while MangoHud Next is running. Changes are reloaded automatically when the file timestamp or size changes.

## Options

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `font_size` | integer | `24` | HUD font size. Currently parsed by the config system for renderer use. |
| `fps_limit` | number | `0` | FPS limit sent to connected clients. `0` disables the limiter. |

## Hud Windows

Each entry in `hud.windows` defines one hud window.

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `position` | `[x, y]` | `[10, 10]` | Window position in pixels. |
| `padding` | number | `8` | Padding around the table content. |
| `background` | boolean | `true` | Draw the window background. |
| `table` | map | required | Table layout for this window. |

## Hud Table

`rows` is a list of rows. Each row is a YAML list of cells. A cell can be `null` or one of these maps:

| Cell | Required field | Optional fields | Description |
| --- | --- | --- | --- |
| Text | `text` | `color`, `font_size`, `font_scale`, `truncate` | Static text. |
| Value | `value` | `unit`, `color`, `precision`, `font_size`, `font_scale`, `truncate` | Metric value. If `unit` is omitted, the metric's default unit is used. Float values use one decimal by default. |
| Graph | `graph` | none currently used | Graph data. Currently only supports `FRAMETIMES`. |
| Exec | `exec` | `unit`, `color`, `font_size`, `font_scale`, `truncate` | Output of a shell command. |

Color values are `RRGGBB` or `RRGGBBAA` hex strings, with an optional leading `#`. When `color` is omitted, the default is white.

`font_size` sets an exact size for a cell. `font_scale` scales the HUD default font size for that cell. If both are set, `font_size` is used.

`truncate` limits displayed text to a maximum character count. Truncated text ends with `...` when there is room.

## Metric References

Metric references can be written in three forms:

```yaml
value: FPS
value: [CPU, LOAD]
value: [GPU, 0, LOAD]
```

Single-name references, such as `FPS`, use metrics from the current application. Two-item references use a metric group and key, such as `[CPU, LOAD]`. GPU metrics are indexed, so use the three-item form, such as `[GPU, 0, LOAD]`.

Available metrics currently include:

| Reference | Keys |
| --- | --- |
| `[GPU, index, KEY]` | `LOAD`, `VRAM_USED`, `GTT_USED`, `VRAM_TOTAL`, `VRAM_CLOCK`, `VRAM_TEMP`, `TEMP`, `JUNCTION_TEMP`, `CORE_CLOCK`, `VOLTAGE`, `POWER`, `POWER_LIMIT`, `FAN_SPEED` |
| `[CPU, KEY]` | `LOAD`, `FREQ`, `TEMP`, `POWER` |
| `[RAM, KEY]` | `USED`, `TOTAL`, `SWAP_USED` |
| `KEY` | `ENGINE_NAME`, `VULKAN_DRIVER`, `RESOLUTION`, `FPS`, `FRAMETIME`, `FRAMETIMES` |

Some single-name references are backed by small shell commands:

| Reference | Description |
| --- | --- |
| `ARCH` | Client executable architecture, using the Wine/Proton game executable when possible. |
| `CLIENT_EXE` | Client executable name. |
| `CPU_GOVERNOR` | Current CPU frequency governor. |
| `CPU_NAME` | CPU model name. |
| `DESKTOP_SESSION` | Current desktop session from `XDG_CURRENT_DESKTOP`. |
| `GAMEMODE` | `ON` when GameMode appears in the client process maps, otherwise `OFF`. |
| `KERNEL` | Kernel release. |
| `MEDIA_ALBUM` | Current MPRIS media album from `playerctl`. |
| `MEDIA_ARTIST` | Current MPRIS media artist from `playerctl`. |
| `MEDIA_TITLE` | Current MPRIS media title from `playerctl`. |
| `OS_NAME` | Pretty OS name from `/etc/os-release`. |
| `VKBASALT` | `ON` when vkBasalt appears in the client process maps, otherwise `OFF`. |
| `WINESYNC` | Wine sync method, such as `NTsync`, `Fsync`, `Esync`, `Wserver`, or `NONE`. |

## Example

```yaml
options:
  font_size: 24
  fps_limit: 0.0

hud_table:
  rows:
    - [ {text: GPU, color: "2e9762FF", font_scale: 0.75},
        {value: [GPU, 0, LOAD], unit: "%"},
        {value: [GPU, 0, TEMP]}
      ]

    - [ null,
        {value: [GPU, 0, CORE_CLOCK]},
        {value: [GPU, 0, POWER]}
      ]

    - [ {text: CPU, color: "2e97cbFF"},
        {value: [CPU, LOAD], unit: "%"},
        {value: [CPU, TEMP]}
      ]

    - [ {text: VULKAN, color: "eb5b5bFF"},
        {value: FPS},
        {value: FRAMETIME, precision: 2}
      ]

    - [ {graph: FRAMETIMES} ]

    - [ {text: Kernel},
        {exec: "uname -r"}
      ]
```
