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

The config file is YAML with two top-level sections:

```yaml
options:
  font_size: 24
  fps_limit: 0.0

hud_table:
  rows:
    - [ {text: GPU}, {value: [GPU, 0, LOAD]} ]
```

`options` is optional. `hud_table.rows` is required when a config file exists.

The config file is watched while MangoHud Next is running. Changes are reloaded automatically when the file timestamp or size changes.

## Options

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `font_size` | integer | `24` | HUD font size. Currently parsed by the config system for renderer use. |
| `fps_limit` | number | `0` | FPS limit sent to connected clients. `0` disables the limiter. |

## HUD Table

`hud_table.rows` is a list of rows. Each row is a YAML list of cells. A cell can be `null` or one of these maps:

| Cell | Required field | Optional fields | Description |
| --- | --- | --- | --- |
| Text | `text` | `color`, `font_size`, `font_scale` | Static text. |
| Value | `value` | `unit`, `color`, `precision`, `font_size`, `font_scale` | Metric value. If `unit` is omitted, the metric's default unit is used. Float values use one decimal by default. |
| Graph | `graph` | none currently used | Graph data. Currently only supports `FRAMETIMES`. |
| Exec | `exec` | `unit`, `color`, `font_size`, `font_scale` | Output of a shell command. |

Color values are `RRGGBB` or `RRGGBBAA` hex strings, with an optional leading `#`. When `color` is omitted, the default is white.

`font_size` sets an exact size for a cell. `font_scale` scales the HUD default font size for that cell. If both are set, `font_size` is used.

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
| `KEY` | `ENGINE_NAME`, `FPS`, `FRAMETIME`, `FRAMETIMES` |

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
