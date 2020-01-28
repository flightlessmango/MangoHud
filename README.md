# MangoHud

A modification of the Mesa vulkan overlay. Personal improvements including temperature reporting and logging capabilities.

#### Comparison (outdated)
![](assets/overlay_comparison.gif)

# Installation
- Arch linux: [PKGBUILD](https://github.com/flightlessmango/PKGBUILDS/blob/master/mangohud/PKGBUILD)

# Normal usage

To enable the MangoHud vulkan overlay layer, run :

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

## MangoHud fps logging

When you toggle logging (using the keybind `F2`), a file is created with your chosen name (using `MANGOHUD_OUTPUT`) plus a date & timestamp.

This file can be uploaded to [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks) to create graphs automatically.
you can share the created page with others, just link it.

#### Multiple log files

It's possible to upload multiple files when using [Flightlessmango.com](https://flightlessmango.com/games/user_benchmarks). You can rename them to your preferred names and upload them in a batch.
These filenames will be used as the legend in the graph.

#### Log uploading walkthrough

![](assets/log_upload_example.gif)

# Notable changes
- Removed hud decoration [90a2212](https://github.com/flightlessmango/mesa/commit/90a2212055a8047d46d0220d5fdc30a76900aaed)
- Changed frametime graph to Lines instead of Histogram [e40533b](https://github.com/flightlessmango/mesa/commit/e40533b7f46858e5b9f08829e789277b2364d5d1)
- Set static min/max ms on frametime graph to act like Afterburners graph [df5238f](https://github.com/flightlessmango/mesa/commit/df5238f990218f5d6e698d572b05ddd19e52b108)
- Added CPU/GPU usage (Only Nvidia and AMD)
- Changed font to UbuntuMono-Bold [73f0aa9](https://github.com/flightlessmango/mesa/commit/73f0aa94d382365205a4a4128d82208315b0b190)
- Increased hud font size [b7d238b](https://github.com/flightlessmango/mesa/commit/b7d238b07eb82153f272d34bf7d1353b701f32e0)
