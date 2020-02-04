# MangoHud

A modification of the Mesa Vulkan overlay. Including GUI improvements, temperature reporting, and logging capabilities.

#### Comparison (outdated)
![](assets/overlay_comparison.gif)

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

## MANGOHUD_CONFIG parameters

You can customize the hud by using the MANGOHUD_CONFIG environment variable while separating different options with a comma.

- `cpu_temp`  :  Displays current CPU temperature
- `gpu_temp`  :  Displays current GPU temperature
- `core_load` :  Displays current CPU load per core
- `font_size` :  Changes the default font size (default is 24)
- `width`     :  Set custom hud width
- `height`    :  Set custom hud height
- `position=x`:  Available values for `x` include `top-left`, `top-right`, `bottom-left`, and `bottom-right`

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
