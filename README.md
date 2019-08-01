# wdisplays

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://spdx.org/licenses/MIT.html)

wdisplays is a graphical application for configuring displays in Wayland
compositors. It borrows some code from [kanshi]. It should work in any
compositor that implements the wlr-output-management-unstable-v1 protocol, such
as [sway].

![Screenshot](wdisplays.png)

# Building

Build requirements are:

- GTK+3
- epoxy
- wayland-client

```sh
meson build
ninja -C build
sudo ninja -C build install
```

[kanshi]: https://github.com/emersion/kanshi
[sway]: https://github.com/swaywm/sway
