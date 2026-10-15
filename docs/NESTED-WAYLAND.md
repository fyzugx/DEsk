# Nested Wayland compatibility

When DEsk runs as a window inside another Wayland compositor, keyboard events
use the parent's keycodes. The parent may replace its XKB map, for example when
an accessibility or automation keyboard sends text. wlroots 0.18.3's Wayland
backend discards that map. The result is different characters arriving in DEsk
and its clients, even though the same input works in the parent desktop.

`patches/wlroots-0.18-parent-keymap.patch` makes that backend apply valid parent
maps, including later replacements. Invalid or truncated maps retain the previous
map, and the map's file descriptor is closed on every path. The patch changes
only the Wayland backend; direct libinput keyboards keep their existing layout
handling.

The Arch and Ubuntu installers apply it when they build their private wlroots
copy. Its fingerprint is part of the build stamp, so an existing private copy is
rebuilt after the patch changes. Distribution-provided wlroots packages are not
modified. When building against such a package, nested text injection still
needs this fix in that dependency.

For a private manual build, apply it to a fresh wlroots 0.18.3 source tree before
Meson setup:

```sh
sh scripts/wlroots-keymap.sh /path/to/wlroots-0.18.3
```

After building and installing that library, the regression check compiles its
actual callback against real wlroots and xkbcommon objects:

```sh
python3 scripts/check-wlroots-keymap.py \
  --source /path/to/wlroots-0.18.3 --prefix /opt/desk-wlroots
```

The check covers ASCII and Unicode map replacement, rejected unsupported,
zero-length, truncated and malformed maps, and descriptor cleanup. Omitting
`--prefix` uses the normal pkg-config and library search paths. It needs a C
compiler and wlroots, Wayland client and xkbcommon development files.

DEsk also draws cursors in the rendered window for nested Wayland outputs.
This avoids depending on the parent displaying a separate cursor surface.
Direct desktop outputs are unchanged.

For a live check, start `desk-window`, open a terminal in it, and enter mixed-case
text, punctuation and Unicode. Check Backspace and Enter after a second text
entry, and move the pointer over both the desk and the terminal. The arrow and
text cursor should remain visible. A successful software-rendered nested test
establishes input and cursor behavior; GPU rendering needs separate validation
on a machine with an accessible render device.
