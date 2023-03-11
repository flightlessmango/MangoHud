# MangoHud

A Vulkan and OpenGL overlay for monitoring FPS, temperatures, CPU/GPU load and more.

![Example gif showing a standard performance readout with frametimes](assets/overlay_example.gif)

---

- [MangoHud](#mangohud)
  - [Installation - Build From Source](#installation---build-from-source)
    - [Dependencies](#dependencies)
    - [Building with build script](#building-with-build-script)
  - [Installation - Pre-packaged Binaries](#installation---pre-packaged-binaries)
    - [GitHub releases](#github-releases)
    - [Arch-based distributions](#arch-based-distributions)
    - [Debian, Ubuntu](#debian-ubuntu)
    - [Fedora](#fedora)
    - [Solus](#solus)
    - [openSUSE](#opensuse)
    - [Flatpak](#flatpak)
  - [Normal usage](#normal-usage)
  - [OpenGL](#opengl)
  - [Hud configuration](#hud-configuration)
    - [Environment Variables: **`MANGOHUD_CONFIG`** and **`MANGOHUD_CONFIGFILE`**](#environment-variables-mangohud_config-and-mangohud_configfile)
  - [Vsync](#vsync)
    - [OpenGL Vsync](#opengl-vsync)
    - [Vulkan Vsync](#vulkan-vsync)
  - [Keybindings](#keybindings)
  - [Workarounds](#workarounds)
  - [MangoHud FPS logging](#mangohud-fps-logging)
    - [Multiple log files](#multiple-log-files)
    - [Log uploading walkthrough](#log-uploading-walkthrough)

## Installation - Build From Source

---

If you wish to compile MangoHud to keep up to date with any changes - first clone this repository and cd into it:

```
git clone --recurse-submodules https://github.com/flightlessmango/MangoHud.git
cd MangoHud
```

Using `meson` to install "manually":

```
meson build
ninja -C build install
```

By default, meson should install MangoHud to `/usr/local`. Specify install prefix with `--prefix=/usr` if desired.
Add `-Dappend_libdir_mangohud=false` option to meson to not append `mangohud` to libdir if desired (e.g. /usr/local/lib/mangohud).

To install 32-bit build on 64-bit distro, specify proper `libdir`: `lib32` for Arch, `lib/i386-linux-gnu` on Debian-based distros. RPM-based distros usually install 32-bit libraries to `/usr/lib` and 64-bit to `/usr/lib64`.
You may have to change `PKG_CONFIG_PATH` to point to correct folders for your distro.

```
CC="gcc -m32" \
CXX="g++ -m32" \
PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig" \
meson build32 --libdir lib32
ninja -C build32 install
```

### Dependencies

Install necessary development packages.

- gcc, g++
- or gcc-multilib, g++-multilib for 32-bit support
- meson >=0.54
- ninja (ninja-build)
- glslang
- vulkan headers if using `-Duse_system_vulkan=enabled` option with `meson`
- libGL/libEGL (libglvnd, mesa-common-dev, mesa-libGL-devel etc)
- X11 (libx11-dev)
- XNVCtrl (libxnvctrl-dev), optional, use `-Dwith_xnvctrl=disabled` option with `meson` to disable
- D-Bus (libdbus-1-dev), optional, use `-Dwith_dbus=disabled` option with `meson` to disable

Python 3 libraries:

- Mako (python3-mako or install with `pip`)

If distro's packaged `meson` is too old and gives build errors, install newer version with `pip` (`python3-pip`).

### Building with build script

You can also use `build.sh` script to do some things automatically like install dependencies, if distro is supported but it usually assumes you are running on x86_64 architecture.

To just build it, execute:

```
./build.sh build
```

You can also pass arguments to meson:

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

__NOTE: If you are running an Ubuntu-based, Arch-based, Fedora-based, or openSUSE-based distro, the build script will automatically detect and prompt you to install missing build dependencies. If you run into any issues with this please report them!__

## Installation - Pre-packaged Binaries

---

### GitHub releases

If you do not wish to compile anything, simply download the file under [Releases](https://github.com/flightlessmango/MangoHud/releases), extract it, and from within the extracted folder in terminal, execute:

```
./mangohud-setup.sh install
```

### Arch-based distributions

If you are using an Arch-based distribution, install [`mangohud`](https://aur.archlinux.org/packages/mangohud/) and [`lib32-mangohud`](https://aur.archlinux.org/packages/lib32-mangohud/) with your favourite AUR helper. [`mangohud-git`](https://aur.archlinux.org/packages/mangohud-git/) and [`lib32-mangohud-git`](https://aur.archlinux.org/packages/lib32-mangohud-git/) are also available on the AUR if you want the up-to-date version of MangoHud. These can help fix issues with the hud not activating when using older releases from pacman!

If you are building it by yourself, you need to enable multilib repository, by editing pacman config:

```
sudo nano /etc/pacman.conf
```

and uncomment:

```txt
#[multilib]
#Include = /etc/pacman.d/mirrorlist
```

then save the file and execute:

```
sudo pacman -Syy
```

### Debian, Ubuntu

If you are using Debian 11 (Bullseye) or later, Ubuntu 21.10 (Impish) or later, or distro derived from them, to install the [MangoHud](https://tracker.debian.org/pkg/mangohud) package, execute:

```
sudo apt install mangohud
```

Optionally, if you also need MangoHud for 32-bit applications, execute:

```
sudo apt install mangohud:i386
```

### Fedora

If you are using Fedora, to install the [MangoHud](https://src.fedoraproject.org/rpms/mangohud) package, execute:

```
sudo dnf install mangohud
```

### Solus

If you are using Solus, to install [MangoHud](https://dev.getsol.us/source/mangohud/) simply execute:

```
sudo eopkg it mangohud
```

### openSUSE

If you run openSUSE Leap or Tumbleweed you can get Mangohud from the official repositories.
There are two packages, [mangohud](https://software.opensuse.org/package/mangohud) for 64bit and [mangohud-32bit](https://software.opensuse.org/package/mangohud-32bit) for 32bit application support.
To have Mangohud working for both 32bit and 64bit applications you need to install both packages even on a 64bit operating system.

```
sudo zypper in mangohud mangohud-32bit
```

Leap doesn't seem to have the 32bit package.

Leap 15.2

```
sudo zypper addrepo -f https://download.opensuse.org/repositories/games:tools/openSUSE_Leap_15.2/games:tools.repo
sudo zypper install mangohud
```

Leap 15.3

```
sudo zypper addrepo -f https://download.opensuse.org/repositories/games:tools/openSUSE_Leap_15.3/games:tools.repo
sudo zypper install mangohud
```

### Flatpak

If you are using Flatpaks, you will have to add the [Flathub repository](https://flatpak.org/setup/) for your specific distribution, and then, to install it, execute:

For flatpak:

```
flatpak install org.freedesktop.Platform.VulkanLayer.MangoHud
```

To enable MangoHud for all Steam games:

```
flatpak override --user --env=MANGOHUD=1 com.valvesoftware.Steam
```

## Normal usage

---

To enable the MangoHud overlay layer for Vulkan and OpenGL, run :

`mangohud /path/to/app`

For Lutris games, go to the System options in Lutris (make sure that advanced options are enabled) and add this to the `Command prefix` setting:

`mangohud`

For Steam games, you can add this as a launch option:

`mangohud %command%`

Or alternatively, add `MANGOHUD=1` to your shell profile (Vulkan only).

## OpenGL

OpenGL games may also need `dlsym` hooking. Add `--dlsym` or `MANGOHUD_DLSYM=1` env var to your command like `mangohud --dlsym %command%` for Steam.

Some Linux native OpenGL games overrides LD_PRELOAD and stops MangoHud from working. You can sometimes fix this by editing LD_PRELOAD in the start script
`LD_PRELOAD=/path/to/mangohud/lib/`

## Hud configuration

MangoHud comes with a config file which can be used to set configuration options globally or per application. Usually it is installed as `/usr/share/doc/mangohud/MangoHud.conf.example` or [get a copy from here](https://raw.githubusercontent.com/flightlessmango/MangoHud/master/data/MangoHud.conf).

The priorities of different config files are:

1. `/path/to/application/dir/MangoHud.conf`
2. Per-application configuration in ~/.config/MangoHud:
    1. `~/.config/MangoHud/<application_name>.conf` for native applications, where `<application_name>` is the case sensitive name of the executable
    2. `~/.config/MangoHud/wine-<application_name>.conf` for wine/proton apps, where `<application_name>` is the case sensitive name of the executable without the `.exe` ending
3. `~/.config/MangoHud/MangoHud.conf`

Example: For Overwatch, this would be `wine-Overwatch.conf` (even though the executable you run from Lutris is `Battle.net.exe`, the actual game executable name is `Overwatch.exe`).

If you start the game from the terminal with MangoHud enabled (for example by starting Lutris from the terminal), MangoHud will print the config file names it is looking for.

You can find an example config in /usr/share/doc/mangohud

[GOverlay](https://github.com/benjamimgois/goverlay) is a GUI application that can be used to manage the config

---

### Environment Variables: **`MANGOHUD_CONFIG`** and **`MANGOHUD_CONFIGFILE`**

You can also customize the hud by using the `MANGOHUD_CONFIG` environment variable while separating different options with a comma. This takes priority over any config file.

You can also specify configuration file with `MANGOHUD_CONFIGFILE=/path/to/config` for applications whose names are hard to guess (java, python etc).

A partial list of parameters are below. See the config file for a complete list.
Parameters that are enabled by default have to be explicitly disabled. These (currently) are `fps`, `frame_timing`, `cpu_stats` (cpu load), `gpu_stats` (gpu load), and each can be disabled by setting the corresponding variable to 0 (e.g., fps=0).

| Variable                           | Description                                                                           |
|------------------------------------|---------------------------------------------------------------------------------------|
| `alpha`                            | Set the opacity of all text and frametime graph `0.0-1.0`                             |
| `arch`                             | Show if the application is 32- or 64-bit                                              |
| `background_alpha`                 | Set the opacity of the background `0.0-1.0`                                           |
| `battery_color`                    | Change the battery text color                                                         |
| `battery_icon`                     | Display battery icon instead of percent                                               |
| `battery`                          | Display current battery percent and energy consumption                                |
| `benchmark_percentiles`            | Configure which framerate percentiles are shown in the logging summary. Default is `97,AVG,1,0.1`      |
| `blacklist`                        | Add a program to the blacklist. e.g `blacklist=vkcube,WatchDogs2.exe`                 |
| `cellpadding_y`                    | Set the vertical cellpadding, default is `-0.085` |
| `core_load_change`                 | Changes the colors of cpu core loads, uses the same data from `cpu_load_value` and `cpu_load_change`   |
| `core_load`                        | Displays load & frequency per core                                                    |
| `cpu_load_change`                  | Changes the color of the CPU load depending on load                                   |
| `cpu_load_color`                   | Set the colors for the gpu load change low, medium and high. e.g `cpu_load_color=0000FF,00FFFF,FF00FF` |
| `cpu_load_value`                   | Set the values for medium and high load e.g `cpu_load_value=50,90`                    |
| `cpu_mhz`                          | Shows the CPUs current MHz                                                            |
| `cpu_power`<br>`gpu_power`         | Display CPU/GPU draw in watts                                                         |
| `cpu_temp`<br>`gpu_temp`           | Displays current CPU/GPU temperature                                                  |
| `cpu_text`<br>`gpu_text`           | Override CPU and GPU text                                                             |
| `custom_text_center`               | Display a custom text centered useful for a header e.g `custom_text_center=FlightLessMango Benchmarks` |
| `custom_text`                      | Display a custom text e.g `custom_text=Fsync enabled`                                 |
| `engine_version`                   | Display OpenGL or vulkan and vulkan-based render engine's version                     |
| `exec`                             | Display output of bash command in next column, e.g `custom_text=/home` , `exec=df -h /home \| tail -n 1`. Only works with legacy_layout=false  |
| `font_file_text`                   | Change text font. Otherwise `font_file` is used                                       |
| `font_file`                        | Change default font (set location to .TTF/.OTF file )                                 |
| `font_glyph_ranges`                | Specify extra font glyph ranges, comma separated: `korean`, `chinese`, `chinese_simplified`, `japanese`, `cyrillic`, `thai`, `vietnamese`, `latin_ext_a`, `latin_ext_b`. If you experience crashes or text is just squares, reduce font size or glyph ranges. |
| `font_scale=`                      | Set global font scale (default=1.0)                                                   |
| `font_scale_media_player`          | Change size of media player text relative to font_size                                |
| `font_size=`                       | Customizeable font size (default=24)                                                  |
| `font_size_text=`                  | Customizeable font size for other text like media metadata (default=24)               |
| `fps_limit`                        | Limit the apps framerate. Comma-separated list of one or more FPS values. `0` means unlimited. |
| `fps_only`                         | Show FPS only. ***Not meant to be used with other display params***                   |
| `frame_count`                      | Display frame count                                                                   |
| `frametime`                        | Display frametime next to FPS text                                                    |
| `full`                             | Enables most of the toggleable parameters (currently excludes `histogram`)            |
| `gamemode`                         | Shows if gamemode is on                                                               |
| `gamepad_battery_icon`             | Display gamepad battery percent with icon. *Enabled by default*                       |
| `gamepad_battery`                  | Display battey of wireless gamepads (xone/xpadneo/ds4)                                |
| `gpu_color`<br>`cpu_color`<br>`vram_color`<br>`ram_color`<br>`io_color`<br>`engine_color`<br>`frametime_color`<br>`background_color`<br>`text_color`<br>`media_player_color`         | Change default colors: `gpu_color=RRGGBB`|
| `gpu_core_clock`<br>`gpu_mem_clock`| Displays GPU core/memory frequency                                                    |
| `gpu_load_change`                  | Changes the color of the GPU load depending on load                                   |
| `gpu_load_color`                   | Set the colors for the gpu load change low,medium and high. e.g `gpu_load_color=0000FF,00FFFF,FF00FF`  |
| `gpu_load_value`                   | Set the values for medium and high load e.g `gpu_load_value=50,90`                    |
| `gpu_name`                         | Displays GPU name from pci.ids                                                        |
| `histogram`                        | Change FPS graph to histogram                                                         |
| `io_read`<br> `io_write`           | Show non-cached IO read/write, in MiB/s                                               |
| `log_duration`                     | Set amount of time the logging will run for (in seconds)                              |
| `log_interval`                     | Change the default log interval, `100` is default                                     |
| `media_player_format`              | Format media player metadata. Add extra text etc. Semi-colon breaks to new line. Defaults to `{title};{artist};{album}`. |
| `media_player_name`                | Force media player DBus service name without the `org.mpris.MediaPlayer2` part, like `spotify`, `vlc`, `audacious` or `cantata`. If none is set, MangoHud tries to switch between currently playing players. |
| `media_player`                     | Show media player metadata                                                            |
| `no_display`                       | Hide the HUD by default                                                               |
| `no_small_font`                    | Use primary font size for smaller text like units                                     |
| `offset_x` `offset_y`              | HUD position offsets                                                                  |
| `output_folder`                    | Set location of the output files (Required for logging)                               |
| `pci_dev`                          | Select GPU device in multi-gpu setups                                                 |
| `permit_upload`                    | Allow uploading of logs to Flightlessmango.com                                        |
| `position=`                        | Location of the HUD: `top-left` (default), `top-right`, `middle-left`, `middle-right`, `bottom-left`, `bottom-right`, `top-center` |
| `procmem`<br>`procmem_shared`, `procmem_virt`| Displays process' memory usage: resident, shared and/or virtual. `procmem` (resident) also toggles others off if disabled. |
| `ram`<br>`vram`                    | Displays system RAM/VRAM usage                                                        |
| `read_cfg`                         | Add to MANGOHUD_CONFIG as first parameter to also load config file. Otherwise only MANGOHUD_CONFIG parameters are used. |
| `reload_cfg=`                      | Change keybind for reloading the config. Default = `Shift_L+F4`                       |
| `resolution`                       | Display the current resolution                                                        |
| `round_corners`                    | Change the amount of roundness of the corners have e.g `round_corners=10.0`           |
| `show_fps_limit`                   | Display the current FPS limit                                                         |
| `swap`                             | Displays swap space usage next to system RAM usage                                    |
| `table_columns`                    | Set the number of table columns for ImGui, defaults to 3                              |
| `time`<br>`time_format=%T`         | Displays local time. See [std::put_time](https://en.cppreference.com/w/cpp/io/manip/put_time) for formatting help. NOTE: Sometimes apps may set `TZ` (timezone) environment variable to UTC/GMT |
| `toggle_fps_limit`                 | Cycle between FPS limits. Defaults to `Shift_L+F1`.                                   |
| `toggle_hud=`<br>`toggle_logging=` | Modifiable toggle hotkeys. Default are `Shift_R+F12` and `Shift_L+F2`, respectively.  |
| `upload_log`                       | Change keybind for uploading log                                                      |
| `version`                          | Shows current MangoHud version                                                        |
| `vkbasalt`                         | Shows if vkbasalt is on                                                               |
| `vsync`<br> `gl_vsync`             | Set vsync for OpenGL or Vulkan                                                        |
| `vulkan_driver`                    | Displays used vulkan driver (radv/amdgpu-pro/amdvlk)                                  |
| `width=`<br>`height=`              | Customizeable HUD dimensions (in pixels)                                              |
| `wine_color`                       | Change color of the wine/proton text                                                  |
| `wine`                             | Shows current Wine or Proton version in use                                           |

Example: `MANGOHUD_CONFIG=cpu_temp,gpu_temp,position=top-right,height=500,font_size=32`
Because comma is also used as option delimiter and needs to be escaped for values with a backslash, you can use `+` like `MANGOHUD_CONFIG=fps_limit=60+30+0` instead.

*Note: Width and Height are set automatically based on the font_size, but can be overridden.*

*Note: RAPL is currently used for Intel CPUs to show power draw with `cpu_power` which may be unreadable for non-root users due to [vulnerability](https://platypusattack.com/). The corresponding `energy_uj` file has to be readable by corresponding user, e.g. by running `chmod o+r /sys/class/powercap/intel-rapl\:0/energy_uj` as root, else the power shown will be **0 W**, though having the file readable may potentially be a security vulnerability persisting until system reboots.*

## Vsync

### OpenGL Vsync

- `-1` = Adaptive sync
- `0`  = Off
- `1`  = On
- `n`  = Sync to refresh rate / n.

### Vulkan Vsync

- `0` = Adaptive VSync (FIFO_RELAXED_KHR)
- `1` = Off (IMMEDIATE_KHR)
- `2` = Mailbox (VSync with uncapped FPS) (MAILBOX_KHR)
- `3` = On (FIFO_KHR)

Not all vulkan vsync options may be supported on your device, you can check what your device supports here [vulkan.gpuinfo.org](https://vulkan.gpuinfo.org/listsurfacepresentmodes.php?platform=linux)

## Keybindings

- `Shift_L+F2` : Toggle Logging
- `Shift_L+F4` : Reload Config
- `Shift_R+F12` : Toggle Hud

## Workarounds

Options starting with "gl_*" are for OpenGL.

- `gl_size_query = viewport` : Specify what to use for getting display size. Options are "viewport", "scissorbox" or disabled. Defaults to using glXQueryDrawable.
- `gl_bind_framebuffer = 0..N` : (Re)bind given framebuffer before MangoHud gets drawn. Helps with Crusader Kings III.
- `gl_dont_flip = 1` : Don't swap origin if using GL_UPPER_LEFT. Helps with Ryujinx.
- `libdrm_sampling` : Use libdrm_amdgpu to calculate GPU utilization. Helps with some problematic Vega GPUs.

## MangoHud FPS logging

You must set a valid path for `output_folder` in your configuration to store logs in.

When you toggle logging (using the keybind `Shift_L+F2`), a file is created with the game name plus a date & timestamp in your `output_folder`.

Log files can be uploaded to [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks) to create graphs automatically.

You can share the created page with others, just link it.

### Multiple log files

It's possible to upload multiple files when using [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks). You can rename them to your preferred names and upload them in a batch.
These filenames will be used as the legend in the graph.

### Log uploading walkthrough

![Gif illustrating the log uploading process](assets/log_upload_example.gif)
