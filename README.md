# MangoHud

A Vulkan and OpenGL overlay for monitoring FPS, temperatures, CPU/GPU load and more.

#### Example:
![](assets/overlay_example.gif)

# Installation

## Build

If you wish to compile MangoHud to keep up to date with any changes - first clone this repository and cd into it:

```
git clone --recurse-submodules https://github.com/flightlessmango/MangoHud.git
cd MangoHud
```

To build it, execute:

```
./build.sh build
./build.sh package
```

**NOTE: If you are running an Ubuntu-based, Arch-based, Fedora-based, or openSUSE-based distro, the build script will automatically detect and prompt you to install missing build dependencies. If you run into any issues with this please report them!**

Once done, proceed to the [installation](#source).

## Install

### Source

If you have compiled MangoHud from source, to install it, execute:

```
./build.sh install
```

You can then subsequently uninstall MangoHud via the following command

```
./build.sh uninstall
```

### Pre-packaged binaries

#### GitHub releases

If you do not wish to compile anything, simply download the file under [Releases](https://github.com/flightlessmango/MangoHud/releases), extract it, and from within the extracted folder in terminal, execute:

```
./mangohud-setup.sh install
```

#### Arch-based distributions

If you are using an Arch-based distribution, install [`mangohud`](https://aur.archlinux.org/packages/mangohud/) and [`lib32-mangohud`](https://aur.archlinux.org/packages/lib32-mangohud/) with your favourite AUR helper. [`mangohud-git`](https://aur.archlinux.org/packages/mangohud-git/) and [`lib32-mangohud-git`](https://aur.archlinux.org/packages/lib32-mangohud-git/) are also available on the AUR if you want the up-to-date version of MangoHud.

#### Fedora

If you are using Fedora, to install the [MangoHud](https://src.fedoraproject.org/rpms/mangohud) package, execute:

```
sudo dnf install mangohud
```

#### Flatpak

If you are using Flatpaks, you will have to add the [Flathub repository](https://flatpak.org/setup/) for your specific distribution, and then, to install it, execute:

##### For Steam flatpak
```
flatpak install com.valvesoftware.Steam.Utility.MangoHud
```
To enable MangoHud for all Steam games:
```
flatpak override --user --env=MANGOHUD=1 com.valvesoftware.Steam
```

# Normal usage

To enable the MangoHud overlay layer for 64bit Vulkan and OpenGL, run :

`mangohud /path/to/app`

Or

`mangohud.x86 /path/to/app` for 32bit OpenGL

For Lutris games, go to the System options in Lutris (make sure that advanced options are enabled) and add this to the `Command prefix` setting:

`mangohud`

For Steam games, you can add this as a launch option:

`mangohud %command%`

Or alternatively, add `MANGOHUD=1` to your shell profile (Vulkan only).

## OpenGL

OpenGL games may also need `dlsym` hooking. Add `MANGOHUD_DLSYM=1` to your command like `MANGOHUD_DLSYM=1 mangohud %command%` for Steam.

Some Linux native OpenGL games overrides LD_PRELOAD and stops MangoHud from working. You can sometimes fix this by editing LD_PRELOAD in the start script
`LD_PRELOAD=/path/to/mangohud/lib/`

## Hud configuration

MangoHud comes with a config file which can be used to set configuration options globally or per application. The priorities of different config files are:

1. `/path/to/application/dir/MangoHud.conf`
2. `$HOME/.config/MangoHud/{application_name}.conf`
3. `$HOME/.config/MangoHud/MangoHud.conf`

You can find an example config in /usr/share/doc/mangohud

[GOverlay](https://github.com/benjamimgois/goverlay) is a GUI application that can be used to manage the config

---

### `MANGOHUD_CONFIG` and `MANGOHUD_CONFIGFILE` environment variables

You can also customize the hud by using the `MANGOHUD_CONFIG` environment variable while separating different options with a comma. This takes priority over any config file.

You can also specify configuration file with `MANGOHUD_CONFIGFILE=/path/to/config` for applications whose names are hard to guess (java, python etc).

A partial list of parameters are below. See the config file for a complete list.

| Variable                           | Description                                                                           |
|------------------------------------|---------------------------------------------------------------------------------------|
| `cpu_temp`<br>`gpu_temp`           | Displays current CPU/GPU temperature                                                  |
| `core_load`                        | Displays load & frequency per core                                                    |
| `gpu_core_clock`<br>`gpu_mem_clock`| Displays GPU core/memory frequency                                                    |
| `ram`<br>`vram`                    | Displays system RAM/VRAM usage                                                        |
| `full`                             | Enables all of the above config options                                               |
| `font_size=`                       | Customizeable font size (default=24)                                                  |
| `width=`<br>`height=`              | Customizeable hud dimensions (in pixels)                                              |
| `position=`                        | Location of the hud: `top-left` (default), `top-right`, `bottom-left`, `bottom-right`, `top-center` |
| `offset_x` `offset_y`              | Hud position offsets                                                                  |
| `no_display`                       | Hide the hud by default                                                               |
| `toggle_hud=`<br>`toggle_logging=` | Modifiable toggle hotkeys. Default are `Shift_R+F12` and `Shift_L+F2`, respectively.      |
| `reload_cfg=`                      | Change keybind for reloading the config. Default = `Shift_L+F4`                       |
| `time`<br>`time_format=%T`         | Displays local time. See [std::put_time](https://en.cppreference.com/w/cpp/io/manip/put_time) for formatting help. NOTE: Sometimes apps (or AMDVLK) may set `TZ` environment variable to `GMT+0` |
| `gpu_color`<br>`gpu_color`<br>`vram_color`<br>`ram_color`<br>`io_color`<br>`engine_color`<br>`frametime_color`<br>`background_color`<br>`text_color`<br>`media_player_color`         | Change default colors: `gpu_color=RRGGBB`|
| `alpha`                            | Set the opacity of all text and frametime graph `0.0-1.0`                             |
| `background_alpha`                 | Set the opacity of the background `0.0-1.0`                                           |
| `read_cfg`                         | Add to MANGOHUD_CONFIG as first parameter to also load config file. Otherwise only MANGOHUD_CONFIG parameters are used. |
| `output_file`                      | Define name and location of the output file (Required for logging)                    |
| `font_file`                        | Change default font (set location to .TTF/.OTF file )                                 |
| `log_duration`                     | Set amount of time the logging will run for (in seconds)                              |
| `vsync`<br> `gl_vsync`             | Set vsync for OpenGL or Vulkan                                                        |
| `media_player`                     | Show media player metadata                                                            |
| `media_player_name`                | Set main media player DBus service name without the `org.mpris.MediaPlayer2` part, like `spotify`, `vlc`, `audacious` or `cantata`. Defaults to `spotify`. |
| `media_player_order`               | Media player metadata field order. Defaults to `title,artist,album`.                  |
| `font_scale_media_player`          | Change size of media player text relative to font_size                                |
| `io_read`<br> `io_write`           | Show non-cached IO read/write, in MiB/s                                               |
| `pci_dev`                          | Select GPU device in multi-gpu setups                                                 |
| `version`                          | Shows current mangohud version                                                        |
| `fps_limit`                        | Limit the apps framerate                                                              |
| `arch`                             | Show if the application is 32 or 64 bit                                               |
| `histogram`                        | Change fps graph to histogram                                                         |
| `cpu_text`<br>`gpu_text`           | Override CPU and GPU text                                                             |
| `log_interval`                     | Change the default log interval, `100` is default                                     |
| `vulkan_driver`                    | Displays used vulkan driver, radv/amdgpu-pro/amdvlk                                   |
| `gpu_name`                         | Displays GPU name from pci.ids                                                        |
| `gpu_power`                        | Display GPU draw in watts                                                             |
| `engine_version`                   | Display OpenGL or vulkan and vulkan-based render engine's version                     |
| `permit_upload`                    | Allow uploading of logs to Flightlessmango.com                                        |
| `upload_log`                       | Change keybind for uploading log                                                      |

Example: `MANGOHUD_CONFIG=cpu_temp,gpu_temp,position=top-right,height=500,font_size=32`

Note: Width and Height are set automatically based on the font_size, but can be overridden.
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

All vulkan vsync options might not be supported on your device, you can check what your device supports here [vulkan.gpuinfo.org](https://vulkan.gpuinfo.org/listsurfacepresentmodes.php?platform=linux)

## Keybindings

- `Shift_L+F2` : Toggle Logging
- `Shift_L+F4` : Reload Config
- `Shift_R+F12`: Toggle Hud

## MangoHud FPS logging

When you toggle logging (using the keybind `Shift_L+F2`), a file is created with your chosen name (using `output_file`) plus a date & timestamp.

This file can be uploaded to [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks) to create graphs automatically.
you can share the created page with others, just link it.

#### Multiple log files

It's possible to upload multiple files when using [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks). You can rename them to your preferred names and upload them in a batch.
These filenames will be used as the legend in the graph.

#### Log uploading walkthrough

![](assets/log_upload_example.gif)
