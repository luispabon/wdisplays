# wdisplays

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://spdx.org/licenses/MIT.html)

wdisplays is a graphical application for configuring displays in Wayland
compositors. It borrows some code from [kanshi]. It should work in any
compositor that implements the wlr-output-management-unstable-v1 protocol,
including [sway]. The goal of this project is to allow precise adjustment of
display settings in kiosks, digital signage, and other elaborate multi-monitor
setups.

![Screenshot](wdisplays.png)

# Building

Build requirements are:

- meson
- GTK+3
- epoxy
- wayland-client

```sh
meson build
ninja -C build
sudo ninja -C build install
```

Binaries are not available. Only building from source is supported, and only
if you're using wlroots compiled from master.

# Usage

Displays can be moved around the virtual screen space by clicking and dragging
them in the preview on the left panel. By default, they will snap to one
another. Hold Shift while dragging to disable snapping. You can click and drag
with the middle mouse button to pan. Zoom in and out either with the buttons on
the top left, or by holding Ctrl and scrolling the mouse wheel. Fine tune your
adjustments in the right panel, then click apply.

There are some options available by clicking the menu button on the top left:

- Automatically Apply Changes: Makes it so you don't have to hit apply. Disable
  this for making minor adjustments, but be careful, you may end up with an
  unusable setup.
- Show Screen Contents: Shows a live preview of the screens in the left panel.
  Turn off to reduce energy usage.
- Overlay Screen Names: Shows big names in the corner of all screens for easy
  identification. Disable if they get in the way.

# FAQ (Fervently Anticpiated Quandaries)

### What is this?

It's intended to be the Wayland equivalent of an xrandr GUI, like [ARandR].

### Help, I get errors and/or crashes!

Make sure your wlroots and sway are up-to-date. Particularly, you need a git
revision of wlroots from [this commit](https://github.com/swaywm/wlroots/commit/724b5e1b8d742a8429f4431ae1a55d7d26cb92ae)
(or later) or your compositor may crash when adding/removing displays.
Alternatively, you can try to disable the "Show Screen Contents" option.

### I'm using Sway, why aren't my display settings saved when I log out?

Sway, like i3, doesn't save any settings unless you put them in the config
file. See man `sway-output`. If you want to have multiple configurations
depending on the monitors connected, you'll need to use an external program
like [kanshi].

[kanshi]: https://github.com/emersion/kanshi
[sway]: https://github.com/swaywm/sway
[ARandR]: https://christian.amsuess.com/tools/arandr/
