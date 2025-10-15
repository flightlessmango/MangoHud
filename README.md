# MangoHud

A Vulkan and OpenGL overlay for monitoring FPS, temperatures, CPU/GPU load and more.

![Example GIF showing a standard performance readout with frametimes](assets/overlay_example.gif)

---

- [MangoHud](#mangohud)
  - [Installation by building from source](#installation-by-building-from-source)
    - [Dependencies](#dependencies)
    - [Building with a build script](#building-with-a-build-script)
  - [Installation with pre-packaged binaries](#installation-with-pre-packaged-binaries)
    - [Arch-based distributions](#arch-based-distributions)
    - [Debian-based or Ubuntu-based distributions](#debian-based-or-ubuntu-based-distributions)
    - [Flatpak](#flatpak)
    - [Fedora-based distributions](#fedora-based-distributions)
    - [GitHub Releases](#github-releases)
    - [Solus-based distributions](#solus-based-distributions)
    - [openSUSE-based distributions](#opensuse-based-distributions)
  - [Normal usage](#normal-usage)
  - [OpenGL](#opengl)
  - [HUD configuration](#hud-configuration)
    - [Environment variables: **`MANGOHUD_CONFIG`**, **`MANGOHUD_CONFIGFILE`**, and **`MANGOHUD_PRESETSFILE`**](#environment-variables)
  - [Vsync](#vsync)
    - [Vulkan Vsync](#vulkan-vsync)
    - [OpenGL Vsync](#opengl-vsync)
  - [Keybindings](#keybindings)
  - [Workarounds](#workarounds)
  - [FPS logging](#fps-logging)
    - [Online visualization: flightlessmango.com](#online-visualization-flightlessmangocom)
    - [Local visualization: `mangoplot`](#local-visualization-mangoplot)
  - [Metrics support by GPU vendor/driver](#metrics-support-by-gpu-vendordriver)

## Installation by building from source

Clone this repository and `cd` into it:

```
git clone --recurse-submodules https://github.com/flightlessmango/MangoHud.git
cd MangoHud
```

Using Meson to install manually:

```
meson build
ninja -C build install
```

By default, Meson should install MangoHud to `/usr/local`. Specify install prefix with `--prefix=/usr` if desired.
Add `-Dappend_libdir_mangohud=false` option to Meson to not append `mangohud` to libdir if desired.

To install a 32-bit build on 64-bit distribution, specify proper `libdir`: `lib32` for Arch-based distributions, `lib/i386-linux-gnu` on Debian-based distributions. RPM-based distributions usually install 32-bit libraries to `/usr/lib` and 64-bit to `/usr/lib64`.
You may have to change `PKG_CONFIG_PATH` to point to correct folders for your distribution.

```
CC="gcc -m32" \
CXX="g++ -m32" \
PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig" \
meson build32 --libdir lib32
ninja -C build32 install
```

### Dependencies

Install the necessary development packages.

- D-Bus (`libdbus-1-dev`), optional, use `-Dwith_dbus=disabled` option with Meson to disable
- `gcc`, `g++` for 64-bit support or `gcc-multilib`, `g++-multilib` for 32-bit support
- `glslang`
- `meson` >= 0.54
- `ninja` (`ninja-build`)
- `wayland-client`
- X11 (`libx11-dev`)
- `xcbcommon`
- XNVCtrl (`libxnvctrl-dev`), optional, use `-Dwith_xnvctrl=disabled` option with Meson to disable

Python 3 libraries:

- Mako (python3-mako or install with `pip`)

Package names may be different depending on the distribution.

If distribution's packaged Meson is too old and gives build errors, install a newer version with `pip` (usually a package named `python3-pip`).

### Meson options

| Option        | Default | Description
| --------      | ------- | -
| with_nvml     | enabled    | Required for NVIDIA GPU metrics on Wayland
| with_xnvctrl  | enabled    | Required for NVIDIA GPU metrics on older GPUs
| with_x11      | enabled    | Required for keybinds on X11
| with_wayland  | enabled    | Required for keybinds on Wayland
| with_dbus     | enabled    | Required for using the media features
| mangoapp      | false      | Includes mangoapp
| mangohudctl   | false      | Includes mangohudctl
| tests         | auto       | Includes tests
| mangoplot     | true       | Includes mangoplot

### Building with a build script

You can also use `build.sh` script to do some things automatically like install dependencies if your distribution is supported but it usually assumes you are running on the x86_64 architecture.

To build it, execute:

```
./build.sh build
```

You can also pass arguments to Meson:

```
./build.sh build -Dwith_xnvctrl=disabled
```

Resulting files will be install to `./build/release` folder.

If you have compiled MangoHud from source, to install it, execute:

```
./build.sh install
```

You can then subsequently uninstall MangoHud via the following command

```
./build.sh uninstall
```

To tar up the resulting binaries into a package and create a release tar with installer script, execute:

```
./build.sh package release
```

or combine the commands, although `package` should also call `build` if it doesn't find the built libs:

```
./build.sh build package release
```

If you have built MangoHud before and suddenly it fails, you can try cleaning the `build` folder, execute:

```
./build.sh clean
```

Currently it just does `rm -fr build` and clears subprojects.

__Note: If you are running an Arch-based, Fedora-based, openSUSE-based, or Ubuntu-based distribution, the build script will automatically detect and prompt you to install missing build dependencies. If you run into any issues with this please report them!__

## Installation with pre-packaged binaries

### Arch-based distributions

If you are using Arch Linux or a distribution derived from it, to install the [`mangohud`](https://archlinux.org/packages/extra/x86_64/mangohud/) package from the `extra` repository, execute:

```
sudo pacman -S mangohud
```

If you are building it by yourself or want to install the [`lib32-mangohud`](https://archlinux.org/packages/multilib/x86_64/lib32-mangohud/) package for 32-bit app support, you need to enable and sync the `multilib` repository by editing the `pacman` configuration:

```
sudo nano /etc/pacman.conf
```

Uncomment:

```txt
#[multilib]
#Include = /etc/pacman.d/mirrorlist
```

then save the file and execute:

```
sudo pacman -Syy
```

After enabling and syncing the `multilib` repository, to install the 32-bit package, execute:

```
sudo pacman -S lib32-mangohud
```

### Debian-based or Ubuntu-based distributions

If you are using Debian 11 (Bullseye) or later, Ubuntu 21.10 (Impish) or later, or distribution derived from them, to install the [MangoHud](https://tracker.debian.org/pkg/mangohud) package, execute:

```
sudo apt install mangohud
```

On Debian, to install the 32-bit MangoHud package for 32-bit app support, execute:

```
sudo apt install mangohud:i386
```

Ubuntu does not seem to have the 32-bit package.

### Fedora-based distributions

If you are using Fedora or a distribution derived from it, to install the [MangoHud](https://src.fedoraproject.org/rpms/mangohud) package, execute:

```
sudo dnf install mangohud
```

### Flatpak

If you are using Flatpaks, you will have to add the Flathub repository for your specific distribution, and then, to install it, execute:

```
flatpak install org.freedesktop.Platform.VulkanLayer.MangoHud
```

An environment variable override with the value `MANGOHUD=1` has to be set for each app you want to use the MangoHud Flatpak with. To set the the environment variable override for an app, execute:

```
flatpak override --env=MANGOHUD=1 --user [app ID]
```

To list the names and IDs of installed Flatpak apps, execute:

```
flatpak list --app --columns=name,application
```

### GitHub Releases

If you do not wish to compile anything, simply download the file under [Releases](https://github.com/flightlessmango/MangoHud/releases), extract it, and from within the extracted folder in terminal, execute:

```
./mangohud-setup.sh install
```

### Solus-based distributions

If you are using Fedora or a distribution derived from it, to install the [MangoHud](https://src.fedoraproject.org/rpms/mangohud) package, execute:

```
sudo eopkg it mangohud
```

### openSUSE-based distributions

If you are using OpenSUSE Leap or Tumbleweed or a distribution derived from them, to install the [MangoHud](https://software.opensuse.org/package/mangohud) package, execute:

```
sudo zypper in mangohud
````

To install the [32-bit MangoHud](https://software.opensuse.org/package/mangohud-32bit) package for 32-bit app support, execute:

```
sudo zypper in mangohud-32bit
````

Leap does not seem to have the 32-bit MangoHud package.

To install the [MangoHud](https://software.opensuse.org/package/mangohud) package on Leap 15.2, execute:

```
sudo zypper addrepo -f https://download.opensuse.org/repositories/games:tools/openSUSE_Leap_15.2/games:tools.repo
sudo zypper install mangohud
```

To install the [MangoHud](https://software.opensuse.org/package/mangohud) package on Leap 15.3, execute:

```
sudo zypper addrepo -f https://download.opensuse.org/repositories/games:tools/openSUSE_Leap_15.3/games:tools.repo
sudo zypper install mangohud
```

## Normal usage

---

To enable the MangoHud overlay layer for Vulkan and OpenGL, run:

`mangohud /path/to/app`

For Lutris games, go to the System options in Lutris (make sure that advanced options are enabled) and add this to the `Command prefix` setting:

`mangohud`

For Steam games, you can add this as a launch option:

`mangohud %command%`

Or alternatively, add `MANGOHUD=1` to your shell profile (Vulkan only).

## OpenGL

OpenGL games may also need `dlsym` hooking, which is now enabled by default. Set the `MANGOHUD_DLSYM` environment variable to `0` to disable it.

Some Linux native OpenGL games override LD_PRELOAD which stops MangoHud from working with them. You can sometimes fix this by editing LD_PRELOAD in the start script
`LD_PRELOAD=/path/to/mangohud/lib/`.

## Gamescope

To enable MangoHud with Gamescope, you need to install mangoapp.
`gamescope --mangoapp -- %command%`

Using the normal MangoHud with Gamescope is not supported.

## Hud configuration

MangoHud comes with a configuration file which can be used to set configuration options globally or per application. Usually it is installed as `/usr/share/doc/mangohud/MangoHud.conf.example`. You can also [get a copy from here](https://raw.githubusercontent.com/flightlessmango/MangoHud/master/data/MangoHud.conf).

The priorities of different configuration files are:

1. `/path/to/application/dir/MangoHud.conf`
2. Per-application configuration in ~/.config/MangoHud:
    1. `~/.config/MangoHud/<application_name>.conf` for native applications, where `<application_name>` is the case sensitive name of the executable
    2. `~/.config/MangoHud/wine-<application_name>.conf` for Wine/Proton applications, where `<application_name>` is the case sensitive name of the executable without the `.exe` extension
3. `~/.config/MangoHud/MangoHud.conf`

If you start the game from the terminal with MangoHud enabled, MangoHud will print the configuration file names it is looking for.

You can find an example config in `/usr/share/doc/mangohud`.

[GOverlay](https://github.com/benjamimgois/goverlay) is a graphical user interface (GUI) application that can be used to manage the configuration.

---

### Environment variables

You can customize the HUD by using the `MANGOHUD_CONFIG` environment variable while separating different options with a comma. This takes priority over any config file.

You can specify a configuration file with `MANGOHUD_CONFIGFILE=/path/to/config`.

You can specify a presets file with `MANGOHUD_PRESETSFILE=/path/to/config`.

You can specify custom HUD libraries for OpenGL using `MANGOHUD_OPENGL_LIBS=/path/to/libMangoHud_opengl.so`.

A partial list of parameters are below. See the config file for a complete list.
Parameters that are enabled by default have to be explicitly disabled. These (currently) are `fps`, `frame_timing`, `cpu_stats` (CPU load), `gpu_stats` (GPU load), and each can be disabled by setting the corresponding variable to 0.

| Variable                           | Description                                                                           |
|------------------------------------|---------------------------------------------------------------------------------------|
| `af`                               | Anisotropic filtering level. Improves sharpness of textures viewed at an angle `0`-`16` |
| `alpha`                            | Set the opacity of all text and frametime graph `0.0`-`1.0`                           |
| `arch`                             | Show the bitness of the application                                              |
| `autostart_log=`                   | Starts the log after X seconds from MangoHud initialization                                     |
| `background_alpha`                 | Set the opacity of the background `0.0`-`1.0`                                         |
| `battery_color`                    | Change the battery text color                                                         |
| `battery_icon`                     | Display battery icon instead of percent                                               |
| `battery_watt`                     | Display wattage for the battery option                                                |
| `battery_time`                     | Display remaining time for battery option                                             |
| `battery`                          | Display current battery percent and energy consumption                                |
| `benchmark_percentiles`            | Configure which framerate percentiles are shown in the logging summary |
| `bicubic`                          | Force bicubic filtering                                                               |
| `blacklist`                        | Add an application to the blacklist.`                 |
| `cellpadding_y`                    | Set the vertical cellpadding |
| `control=`                         | Sets up a Unix socket with a specific name that can be connected to with mangohud-control    |
| `core_load_change`                 | Change the colors of CPU core loads, uses the same data from `cpu_load_value` and `cpu_load_change` |
| `core_load`                        | Display load & frequency per core                                                     |
| `core_type`                        | Display CPU core type per core. For Intel CPUs, it shows which cores are performance and efficiency cores, for ARM it shows core codenames                                                     |
| `core_bars`                        | Change the display of `core_load` from numbers to vertical bars                       |
| `cpu_load_change`                  | Change the color of the CPU load depending on load                                    |
| `cpu_load_color`                   | Set the colors for the GPU load change low, medium and high |
| `cpu_load_value`                   | Set the values for medium and high load                  |
| `cpu_mhz`                          | Show the CPU's current MHz                                                             |
| `cpu_power`<br>`gpu_power`         | Display CPU/GPU draw in watts                                                         |
| `cpu_temp`<br>`gpu_temp`<br>`gpu_junction_temp`<br>`gpu_mem_temp`           | Display current CPU/GPU temperature                                                  |
| `cpu_text`<br>`gpu_text`           | Override CPU and GPU text. `gpu_text` is a list in case of multiple GPUs              |
| `cpu_efficiency`                   | Display CPU efficiency in frames per joule                                            |
| `custom_text_center`               | Display a custom text centered |
| `custom_text`                      | Display a custom text                                |
| `debug`                            | Shows the graph of Gamescope application frametimes and latency (only works on Gamescope) |
| `device_battery_icon`              | Display wireless device battery icon                                                  |
| `device_battery`                   | Display wireless device battery percent. Currently supported arguments are `gamepad` and `mouse` |
| `display_server`                   | Display the current display session                             |
| `dynamic_frame_timing`             | This changes frame_timing y-axis to correspond with the current maximum and minimum frametime instead of being a static 0-50 |
| `engine_short_names`               | Display a short version of the used engine name          |
| `engine_version`                   | Display OpenGL's or Vulkan's and the Vulkan rendering engine's version                     |
| `exec`                             | Display output of bash command in next column. Only works with `legacy_layout=0` |
| `exec_name`                        | Display current exec name                                                             |
| `fan`                              | Shows the Steam Deck fan RPM                                                          |
| `fcat`                             | Enables frame capture analysis                                                        |
| `fcat_overlay_width=`              | Sets the width of fcat. Default is `24`                                               |
| `fcat_screen_edge=`                | Decides the edge fcat is displayed on. A value between `1` and `4`                    |
| `font_file_text`                   | Change the text font. Otherwise `font_file` is used                                       |
| `font_file`                        | Change the default font (set location to .TTF/.OTF file)                                  |
| `font_glyph_ranges`                | Specify extra font glyph ranges, comma separated: `korean`, `chinese`, `chinese_simplified`, `japanese`, `cyrillic`, `thai`, `vietnamese`, `latin_ext_a`, `latin_ext_b`. If you experience crashes or text is just squares, reduce the font size or glyph ranges |
| `font_scale=`                      | Set the global font scale.                                               |
| `font_scale_media_player`          | Change size of media player text relative to `font_size`                              |
| `font_size=`                       | Customize the font size                                               |
| `font_size_secondary=`             | Customize the font size for secondary metrics          |
| `font_size_text=`                  | Customizable font size for other text like media metadata            |
| `fps_color_change`                 | Change the FPS text color depending on the FPS value                                |
| `fps_color=`                       | Choose the colors that the FPS changes to when `fps_color_change` is enabled. Corresponds with fps_value   |
| `fps_limit_method`                 | If the FPS limiter should wait before or after presenting a frame. Choose `late` (default) for the lowest latency or `early` for the smoothest frametimes |
| `fps_limit`                        | Limit the app's framerate. Comma-separated list of one or more FPS values. `0` means unlimited |
| `fps_only`                         | Show FPS only. Not meant to be used with other display parameters                   |
| `fps_sampling_period=`             | Time interval between two sampling points for gathering the FPS in milliseconds.   |
| `fps_value`                        | Choose the break points where `fps_color_change` changes colors between |
| `fps_metrics`                      | Takes a list of decimal values or the value average                      |
| `reset_fps_metrics`                | Reset the FPS metrics keybind                                    |
| `fps_text`                         | Display custom text for the engine name in front of the FPS                                   |
| `frame_count`                      | Display the frame count                                                                   |
| `frametime`                        | Display the frametime next to the FPS text                                                    |
| `frame_timing_detailed`            | Display the frame timing in a more detailed chart                                         |
| `fsr`                              | Display the the status of FSR (only works in Gamescope)                                   |
| `hdr`                              | Display the status of HDR (only works in Gamescope)                                   |
| `refresh_rate`                     | Display the current refresh rate (only works in Gamescope)                            |
| `full`                             | Enable most of the toggleable parameters (currently excludes `histogram`)             |
| `gamemode`                         | Show if GameMode is on                                                                |
| `gpu_color`<br>`cpu_color`<br>`vram_color`<br>`ram_color`<br>`io_color`<br>`engine_color`<br>`frametime_color`<br>`background_color`<br>`text_color`<br>`media_player_color`<br>`network_color`         | Change default colors: `gpu_color=RRGGBB` |
| `gpu_core_clock`<br>`gpu_mem_clock`| Display GPU core/memory frequency                                                     |
| `gpu_fan`                          | GPU fan in RPM, except for NVIDIA GPUs where it is a percentage |
| `gpu_load_change`                  | Change the color of the GPU load depending on load                                    |
| `gpu_load_color`                   | Set the colors for the GPU load change low,medium and high |
| `gpu_load_value`                   | Set the values for medium and high load                    |
| `gpu_name`                         | Display GPU name from pci.ids                                                         |
| `gpu_voltage`                      | Display GPU voltage                                                                   |
| `gpu_list`                         | List GPUs to display `gpu_list=0,1`                                                   |
| `gpu_efficiency`                   | Display GPU efficiency in frames per joule                                            |
| `gpu_power_limit`                  | Display GPU power limit                                                               |
| `hide_fsr_sharpness`               | Hides the sharpness info for the `fsr` option (only available in gamescope)           |
| `histogram`                        | Change the FPS graph to a histogram                                                         |
| `horizontal`                       | Display MangoHud in a horizontal position                                             |
| `horizontal_separator_color`       | Set the colors for the horizontal separators (horizontal layout only)                 |
| `horizontal_stretch`               | Stretches the background to the screens width (horizontal layout only)                    |
| `hud_compact`                      | Display a compact version of MangoHud                                                   |
| `hud_no_margin`                    | Remove the margins around MangoHud                                                        |
| `io_read`<br> `io_write`           | Show non-cached IO read/write, in MiB/s                                               |
| `log_duration`                     | Set amount of time the logging will run for in seconds                              |
| `log_interval`                     | Change the default log interval in milliseconds                       |
| `log_versioning`                   | Adds more headers and information such as versioning to the log. This format is not supported on flightlessmango.com (yet)    |
| `media_player_format`              | Format media player metadata. Add extra text etc. Semi-colon breaks to new line. Defaults to `{title};{artist};{album}` |
| `media_player_name`                | Force media player DBus service name without the `org.mpris.MediaPlayer2` part. If none is set, MangoHud tries to switch between currently playing players |
| `media_player`                     | Show media player metadata                                                            |
| `no_display`                       | Hide the HUD by default                                                               |
| `no_small_font`                    | Use primary font size for smaller text like units                                     |
| `offset_x` `offset_y`              | HUD position offsets                                                                  |
| `output_file`                      | Set location and name of the log file                                                 |
| `output_folder`                    | Set location of the output files (Required for logging)                               |
| `pci_dev`                          | Select GPU device in multi-GPU setups                                                 |
| `permit_upload`                    | Allow uploading of logs to flightlessmango.com                                        |
| `picmip`                           | Mip-map LoD bias. Negative values will increase texture sharpness (and aliasing). Positive values will increase texture blurriness. Supported values are `-16`-`16` |
| `position=`                        | Location of the HUD: `top-left`, `top-right`, `middle-left`, `middle-right`, `bottom-left`, `bottom-right`, `top-center`, `bottom-center` |
| `preset=`                          | Comma separated list of one or more presets. Default is `-1,0,1,2,3,4`. Available presets:<br>`0` (no HUD)<br> `1` (FPS only)<br> `2` (horizontal)<br> `3` (extended)<br> `4` (detailed)<br>User defined presets can be created by using a [presets.conf](data/presets.conf) file in `~/.config/MangoHud/`.                      |
| `procmem`<br>`procmem_shared`, `procmem_virt`| Displays a process' memory usage: resident, shared and/or virtual. `procmem` (resident) also toggles others off if disabled |
| `proc_vram`                        | Display a process' VRAM usage                                                           |
| `ram`<br>`vram`                    | Display the system RAM/VRAM usage                                                         |
| `read_cfg`                         | Add to `MANGOHUD_CONFIG` as the first parameter to also load a configuration file. Otherwise only `MANGOHUD_CONFIG` parameters are used |
| `reload_cfg=`                      | Change keybind for reloading the config. Default = `Shift_L+F4`                       |
| `resolution`                       | Display the current resolution                                                        |
| `retro`                            | Disable linear texture filtering                          |
| `round_corners`                    | Change the amount of roundness of the corners have e.g `round_corners=10.0`           |
| `show_fps_limit`                   | Display the current FPS limit                                                         |
| `swap`                             | Display swap space usage next to system RAM usage                                     |
| `table_columns`                    | Set the number of table columns for ImGui                              |
| `temp_fahrenheit`                  | Show temperature in Fahrenheit                                                        |
| `text_outline`                     | Draw an outline around text for better readability               |
| `text_outline_color=`              | Set the color of `text_outline`                                   |
| `text_outline_thickness=`          | Set the thickness of `text_outline`                                  |
| `throttling_status`                | Show if GPU is throttling based on the power, current, temperature or other things. Only shows if throttling is currently happening. Currently disabled by default for NVIDIA as it may cause lag on 3000 series GPUs |
| `throttling_status_graph`          | Same as `throttling_status` but displays throttling in the frametime graph and only power and temp throttling |
| `time`<br>`time_format=%T`         | Display local time. See [std::put_time](https://en.cppreference.com/w/cpp/io/manip/put_time) for formatting help. Note that sometimes apps may set `TZ` (timezone) environment variable to UTC/GMT |
| `time_no_label`                    | Remove the label before time                                                          |
| `toggle_fps_limit`                 | Cycle between FPS limits (needs at least two values set with `fps_limit`)                                    |
| `toggle_preset`                    | Cycle between presets.                                      |
| `toggle_hud=`<br>`toggle_logging=` | Modifiable toggle hotkeys   |
| `toggle_hud_position`              | Toggle MangoHud position                                     |
| `trilinear`                        | Force trilinear filtering                                                             |
| `upload_log`                       | Change keybind for uploading log                                                      |
| `upload_logs`                      | Enables automatic uploads of logs to flightlessmango.com                              |
| `version`                          | Show current MangoHud version                                                         |
| `vkbasalt`                         | Show if vkBasalt is on                                                                |
| `vsync`<br> `gl_vsync`             | Set Vsync for Vulkan or OpenGL                                                        |
| `vulkan_driver`                    | Display the used Vulkan driver's name (radv/amdgpu-pro/amdvlk)                                   |
| `width=`<br>`height=`              | Customizable HUD dimensions (in pixels)                                              |
| `wine_color`                       | Change color of the Wine/Proton text                                                  |
| `wine`                             | Show the current Wine or Proton version in use                                            |
| `winesync`                         | Show which Wine sync method is in use                                                          |
| `present_mode`                     | Shows current Vulkan [present mode](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPresentModeKHR.html) or Vsync status in OpenGL  |
| `network`                          | Show network interfaces tx and rx kb/s. You can specify the interface with by setting the interface as the value |
| `fex_stats`                        | Show FEX-Emu statistics |
| `ftrace`                           | Display information about trace events reported through ftrace                        |
| `flip_efficiency`                  | Flips CPU and GPU efficiency to joules per frame                                      |

Comma is also used as option delimiter and needs to be escaped for values with a backslash. For those you can use `+` instead.

*Note that height and width are set automatically based on the font_size, but can be overridden.*

*Note: RAPL is currently used for AMD Zen and Intel CPUs to show power draw with `cpu_power` which may be unreadable for non-root users due to a [vulnerability](https://platypusattack.com/). The corresponding `energy_uj` file has to be readable by corresponding user, else the power shown will be **0 W**, though having the file readable may potentially be a security vulnerability persisting until system reboots.*

*Note: The [zenergy](https://github.com/boukehaarsma23/zenergy) or [zenpower3](https://git.exozy.me/a/zenpower3) kernel driver must be installed to show the power draw of Ryzen CPUs.*

## Vsync

### Vulkan Vsync

- `0` = Adaptive sync (FIFO_RELAXED_KHR)
- `1` = Off (IMMEDIATE_KHR)
- `2` = Mailbox (VSync with uncapped FPS) (MAILBOX_KHR)
- `3` = On (FIFO_KHR)

### OpenGL Vsync

- `-1` = Adaptive sync
- `0`  = Off
- `1`  = On
- `n`  = Sync to refresh rate

Not all Vulkan Vsync options may be supported on your device, you can check what your device supports here [vulkan.gpuinfo.org](https://vulkan.gpuinfo.org/listsurfacepresentmodes.php?platform=linux)

## Keybindings

- `Shift_L+F2` : Toggle logging
- `Shift_L+F4` : Reload configuration
- `Shift_R+F12` : Toggle HUD
- `Shift_R+F9` : Reset FPS metrics

## Workarounds

Options starting with "gl_*" are for OpenGL.

- `gl_size_query = viewport` : Specify what to use for getting display size. Options are "viewport", "scissorbox" or disabled. Uses glXQueryDrawable.
- `gl_bind_framebuffer = 0..N` : (Re)bind given framebuffer before MangoHud gets drawn. Helps with Crusader Kings III.
- `gl_dont_flip = 1` : Don't swap origin if using GL_UPPER_LEFT. Helps with Ryujinx.

## FPS logging

You must set a valid path for `output_folder` in your configuration to store logs in.

When you toggle logging (default keybind is `Shift_L+F2`), a file is created with the game name plus a date and timestamp in your `output_folder`.

Log files can be visualized locally or online.

### Local visualization: `mangoplot`
`mangoplot` is a plotting script that is shipped with `MangoHud`: on a given folder, it takes each log file, makes a 1D heatmap of its framerates, then stacks the heats maps vertically to form a 2D graph for easy visual comparison between benchmarks.

Example output:

![Overwatch 2 Windows 11 vs Linux](assets/Overwatch2-w11-vs-linux.svg)

<sub><sup>Overwatch 2, 5950X + 5700XT, low graphics preset, FHD, 50% render scale</sup></sub>

### Online visualization: flightlessmango.com
Log files can be (batch) uploaded to [flightlessmango.com](https://flightlessmango.com/games/user_benchmarks), which will then take care of creating a frametime graph and a summary with 1% min or 97th percentile or average framerate in a horizontal bar chart and table form.

Notes:
- Uploaded benchmarks are public: you can share them with anyone by simply giving them the link.
- Benchmark filenames are used as legend in the produced tables and graphs, they can be renamed after the upload.

![GIF illustrating the log uploading process](assets/log_upload_example.gif)

## Metrics support by GPU vendor/driver
<table>
	<tr>
		<th></th>
		<th>NVIDIA</th>
		<th>AMD</th>
		<th colspan="2">Intel Discrete</th>
		<th>Intel Integrated</th>
		<th>Panfrost driver</th>
	</tr>
	<tr>
		<th></th>
		<th></th>
		<th></th>
		<th>i915</th>
		<th>xe</th>
		<th>i915/xe</th>
		<th></th>
	</tr>
	<tr>
		<td>Usage%</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
	</tr>
	<tr>
		<td>Temperature</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🟢</td>
	</tr>
	<tr>
		<td>Junction temperature</td>
		<td>🔴</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Memory temperature</td>
		<td>🔴</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Process VRAM</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
	</tr>
	<tr>
		<td>System VRAM</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Total VRAM</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Memory clock</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Core clock</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
	</tr>
	<tr>
		<td>Power usage</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Throttling status</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Fan speed</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
	<tr>
		<td>Voltage</td>
		<td>🔴</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🟢</td>
		<td>🔴</td>
		<td>🔴</td>
	</tr>
</table>

#### Intel notes
- GPU temperature for `i915` requires Linux 6.13+.
- Fan speed for `i915` requires Linux 6.12+.
- GPU temperature and vram temperature for `xe` requires Linux 6.15+.
- Fan speed for `xe` requires Linux 6.16+.
- GPU usage and memory usage shows usage of current process, not total system usage (it's an issue on Intel's side).
- Integrated Intel GPUs are limited due to lack of the `hwmon` interface (it's an issue on Intel's side, [i915 source](https://github.com/torvalds/linux/blob/5fc31936081919a8572a3d644f3fbb258038f337/drivers/gpu/drm/i915/i915_hwmon.c#L914-L916), [xe source](https://github.com/torvalds/linux/blob/5fc31936081919a8572a3d644f3fbb258038f337/drivers/gpu/drm/xe/xe_hwmon.c#L824-L826)).

#### Panfrost notes
- GPU usage requires `echo 1 | sudo tee /sys/class/drm/renderD*/device/profiling`.
