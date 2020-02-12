# MangoHud

A modification of the Mesa Vulkan overlay. Including GUI improvements, temperature reporting, and logging capabilities.

#### Example:
![](assets/overlay_example.gif)

# Installation

If you do not wish to compile anything, simply download the file under Releases, extract it, and run `./install.sh` from within the extracted folder.

If you wish to compile MangoHud to keep up to date with any changes - first clone this repository and cd into it:

```
git clone --recurse-submodules https://github.com/flightlessmango/MangoHud.git
cd MangoHud
```

Then simply run the following command:

`./build.sh install`

This will build and copy `libMangoHud.so` & `libMangoHud32.so` to `$HOME/.local/share/MangoHud`, as well as copying the required Vulkan layer configuration files.

---

If you are running an Ubuntu-like distrobution, Fedora, or Arch, the build script will automatically detect and prompt you to install missing build dependencies. If you run in to any issues with this please report them!

# Normal usage

To enable the MangoHud Vulkan overlay layer, run :

`MANGOHUD=1 /path/to/my_vulkan_app`

Or alternatively, add `MANGOHUD=1` to your shell profile.

## Hud configuration

MangoHud comes with a config file which can be used to set configuration options globally or per application. The priorities of different config files are:

1. `/path/to/application/dir/MangoHud.conf`
2. `$HOME/.config/MangoHud/{application_name}.conf`
3. `$HOME/.config/MangoHud/MangoHud.conf`
4. `$HOME/.local/share/MangoHud/MangoHud.conf`

The default config file is installed to `$HOME/.config/MangoHud/MangoHud.conf` and will not be overwritten by the script.

---

### `MANGOHUD_CONFIG` environment varianble

You can also customize the hud by using the `MANGOHUD_CONFIG` environment variable while separating different options with a comma. This takes priority over any config file.

A partial list of parameters are below. See the config file for a complete list.

| Variable                           | Description                                                                           |
|------------------------------------|---------------------------------------------------------------------------------------|
| `cpu_temp`<br>`gpu_temp`           | Displays current CPU/GPU temperature                                                  |
| `core_load`                        | Displays load & frequency per core                                                    |
| `ram`<br>`vram`                    | Displays system RAM/VRAM usage                                                        |
| `full`                             | Enables all of the above config options                                               |
| `crosshair`                        | Adds a crosshair overlay at the centre of the screen                                  |
| `font_size=`                       | Customizeable font size (default=24)                                                  |
| `width=`<br>`height=`              | Customizeable hud dimensions (in pixels)                                              |
| `position=`                        | Location of the hud: `top-left` (default), `top-right`, `bottom-left`, `bottom-right` |
| `no_display`                       | Hide the hud by default                                                               |
| `toggle_hud=`<br>`toggle_logging=` | Modifiable toggle hotkeys. Default are F12 and F2, respectively.                      |

Note: Width and Height are set automatically based on the font_size, but can be overridden.

Example: `MANGOHUD_CONFIG=cpu_temp,gpu_temp,position=top-right,height=500,font_size=32`

## Environment Variables

- `MANGOHUD_OUTPUT` : Define name and location of the output file (Required for logging)
- `MANGOHUD_FONT`: Change default font (set location to .TTF/.OTF file )

## Keybindings

- `F2` : Toggle Logging
- `F12`: Toggle Hud

## MangoHud FPS logging

When you toggle logging (using the keybind `F2`), a file is created with your chosen name (using `MANGOHUD_OUTPUT`) plus a date & timestamp.

This file can be uploaded to [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks) to create graphs automatically.
you can share the created page with others, just link it.

#### Multiple log files

It's possible to upload multiple files when using [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks). You can rename them to your preferred names and upload them in a batch.
These filenames will be used as the legend in the graph.

#### Log uploading walkthrough

![](assets/log_upload_example.gif)
