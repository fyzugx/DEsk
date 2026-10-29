# DEsk configuration reference

Everything DEsk reads and writes, for v0.1 (BETA). Shell Settings
(`desk-settings`) edits `shell.conf` for you; everything here can also be
edited by hand, and DEsk picks up a changed `shell.conf` within a second.
Unknown keys and bad values are reported in the session log
(`~/.local/state/desk/session.log`) and otherwise ignored.

`$XDG_CONFIG_HOME` is `~/.config`, `$XDG_DATA_HOME` is `~/.local/share`,
`$XDG_STATE_HOME` is `~/.local/state` and `$XDG_CACHE_HOME` is `~/.cache`
unless set otherwise.

## Files

| File | Written by | What it holds |
| --- | --- | --- |
| `$XDG_CONFIG_HOME/desk/shell.conf` | Shell Settings, you | DEsk's settings (below). It is not there until you first change a setting in Shell Settings, which writes it; you can also create it yourself. Until then the machine's defaults (the next row) and the defaults below hold. |
| `/etc/xdg/desk/shell.conf` | the installer | The machine's defaults, under each user's own file; the installer writes the Graphics level it measured. |
| `$XDG_CONFIG_HOME/desk/displays.conf` | DEsk | How a display tool last arranged each set of screens. |
| `$XDG_CONFIG_HOME/desk/drawer` | DEsk | The programs in the drawer, in order. With a drawer for each screen, another screen's is `drawer@SCREEN` (`drawer@HDMI-A-1`); what is under a false bottom is the same name with `.hidden` after it. |
| `$XDG_CONFIG_HOME/desk/shelves.conf` | you | Programs filed on a shelf other than their own. |
| `~/.config/desk-mimeapps.list` | you | Default programs inside DEsk only, before `mimeapps.list`. |
| `$XDG_DATA_HOME/desk/calendar` | DEsk | Notes stuck on the calendar. |
| `$XDG_DATA_HOME/desk/memo` | DEsk | The memo pad's sheets. |
| `$XDG_CACHE_HOME/desk/` | DEsk | The traced room, icons converted from SVG, your picture for the door. Safe to delete; it is made again. |
| `$XDG_STATE_HOME/desk/session.log`, `session.log.old` | `desk-session` | What DEsk said this session and the one before. |
| `$XDG_STATE_HOME/desk/install.log` | `install_session.sh` | What the last install run through it said (one run straight from `install_<system>.sh` says it only on the terminal). |
| `$XDG_STATE_HOME/desk/cpu-letter` | DEsk | When the "running on the CPU" letter was last sent. |

DEsk writes every file to a new name and renames it into place, so a crash
never leaves half a file.

## shell.conf

One `key = value` per line; `#` starts a comment.

### Windows

| Key | Values | Default | |
| --- | --- | --- | --- |
| `paper_for_all_windows` | `yes`, `no` | `yes` | Lay programs that draw their own frame (Firefox, GNOME programs) on paper too. |
| `paper.<program>` | `yes`, `no` | | One program the other way. `<program>` is its desktop file id, app id or X class. |
| `throw_closed_windows` | `yes`, `no` | `yes` | Crumple a closed window and throw it away. |
| `corner_on_hover` | `yes`, `no` | `yes` | Turn a corner up when the pointer rests on it (otherwise only on a click). |

### The desk

| Key | Values | Default | |
| --- | --- | --- | --- |
| `scale` | `auto`, a number from `0.5` to `4`, or a percentage (`125%`) | `auto` | Pixels per point. `auto` follows the screen's density. |
| `graphics` | `ultra`, `high`, `medium`, `low`, `2d` | `high` (`2d` when the screen is drawn on the CPU) | How much is drawn. |
| `motion` | `brisk`, `normal`, `gentle` | `normal` | How long movements take. |
| `camera_bounce` | `normal`, `low`, `off` | `normal` | How far the view swings past and settles. |
| `language` | `auto`, `en`, `ja`, `zh`, `ko`, `es`, `fr`, `de` | `auto` | The desk's words and the names programs are filed under. `auto` follows `LC_ALL`, `LC_MESSAGES`, `LANG`. |
| `clock` | `24h`, `12h` | `24h` | |
| `lights_at_login` | `yes`, `no` | `yes` | The room starts dark and the lamp comes up as it is made. |
| `celebrations` | `yes`, `no` | `yes` | The door and clock on certain days (not in Shell Settings). |

### Screen

| Key | Values | Default | |
| --- | --- | --- | --- |
| `screen_off` | `never`, or minutes (`1` to `1440`) | `10` | Minutes untouched before the screen goes dark. |
| `lock_on_screen_off` | `yes`, `no` | `yes` | Lock (the front door) when it does. |

A key or the mouse lights the screen again. Some monitors say they have been
unplugged when they go to sleep (an HDMI or DisplayPort monitor dropping its
hot-plug signal); DEsk then waits for the screen to come back, and a screen
that comes back while the desk is dark stays dark until a key or the mouse.
While there is no screen at all, keys and clicks do nothing. A monitor that
stays asleep like this needs its own power button; turning off its deep-sleep
or power-saving setting, or `screen_off = never`, avoids it.

### Keyboard and pointer

| Key | Values | Default | |
| --- | --- | --- | --- |
| `tap_to_click` | `yes`, `no` | `yes` | |
| `natural_scrolling` | `yes`, `no` | `no` | |
| `pointer_speed` | `slow`, `normal`, `fast`, or `-1` to `1` | `normal` | |
| `left_handed` | `yes`, `no` | `no` | |
| `keyboard_layout` | `system`, or an xkb layout (`us`, `jp`, `de`, `us(intl)`) | `system` | |
| `keyboard_second` | `none`, or an xkb layout (`ara`, `ru`, `gr`) | `none` | A second layout to switch to. DEsk's shortcuts go by the first layout's keys while it is on. |
| `keyboard_switch` | `alt_shift`, `super_space`, `ctrl_shift`, `caps_lock` | `alt_shift` | |
| `key_repeat` | `slow`, `normal`, `fast` | `normal` | |

### Days and weather

| Key | Values | Default | |
| --- | --- | --- | --- |
| `birthday` | `MM-DD` | none | |
| `weather` | `yes`, `no` | `yes` | Ask open-meteo.com every half hour for the weather at your time zone's city (only its latitude and longitude are sent). |

### Drawer and ALL APPS

| Key | Values | Default | |
| --- | --- | --- | --- |
| `drawer_closes` | `yes`, `no` | `yes` | Close the drawer after something in it is used. |
| `fold_into_drawer` | `yes`, `no` | `no` | Folded windows go into the drawer instead of lying on the desk. |
| `drawers` | `shared`, `each` | `shared` | With more than one screen: one drawer that comes out on the screen it is opened from, or a drawer for each screen with its own things in it. |
| `false_bottom` | `off`, `all`, `desk`, `side`, or screen names (`DP-1,HDMI-A-1`) | `off` | Drawers whose felt slides back, by a brass ring in it, to a hidden compartment with things of its own. With one shared drawer, anything but `off` gives it one. |
| `take_sheet` | `yes`, `no` | `yes` | A program taken from ALL APPS has its sheet carried down first. |
| `app.terminal`, `app.browser`, `app.files`, `app.editor` | a desktop file id (`xfce4-terminal.desktop`) | | The standard programs, instead of DEsk's choice. |

### Sound

| Key | Values | Default | |
| --- | --- | --- | --- |
| `sound` | `off`, `quiet`, `normal` | `normal` | |
| `letter_sound` | `yes`, `no` | `yes` | Hear a letter arrive. |

## displays.conf

Written when a display tool (wdisplays, wlr-randr, kanshi) changes the
screens, and applied again whenever the same set of screens is connected.
The size of a point is not kept here; it is `shell.conf`'s `scale`.

```
set = <screen> + <screen> ...
screen = <make> / <model> / <serial>    (or the connector name, DP-1, if there is no serial)
enabled = yes | no
mode = <width>x<height>@<Hz> [custom]
position = <x>,<y>                      (pixels; the desk's screen is at 0,0)
transform = normal | 90 | 180 | 270 | flipped | flipped-90 | flipped-180 | flipped-270
```

The screen at position 0,0 is the one the desk is drawn on; any other shows
a plain desk for windows.

## shelves.conf

```
# desktop file id = shelf
org.kde.kate.desktop = Development
```

Shelves: `Development`, `Internet`, `Office`, `Graphics`, `Media`, `Games`,
`Utilities`, `System`.

## Environment variables

For users:

| Variable | |
| --- | --- |
| `DESK_SCALE` | Pixels per point (overrides `scale`). |
| `DESK_ROOM_W` | Trace the room at this width (320 to 3840), whatever the GPU. |
| `DESK_NO_GPU` | Trace the room on the CPU. |
| `DESK_GPU_NODE` | Trace the room on this render node (`/dev/dri/renderD129`). |
| `DESK_ON_CPU` | `1` or `0`: say whether the screen is drawn on the CPU instead of detecting it. |
| `DESK_NO_XWAYLAND` | Do not offer X11. |
| `DESK_SLOW` | Make every movement this many times slower. |
| `DESK_SOUND` | `auto`, `pulse`, `alsa`, `off`, or `file:PATH` (`%p` is the process id). |
| `DESK_LOOK` | `0`: do not install DEsk's icons and GTK theme into its dconf layer. |
| `DESK_LOG` | `-`: `desk-session` leaves the log on stderr instead of `session.log`. |
| `DESK_ALLOW_MULTIPLE` | `1`: start even though a DEsk is already running for this user. |
| `DESK_SIZE`, `DESK_RENDERER` | For `desk-window`: the window's size, and `pixman` or `gles2`. |

For development and testing:

| Variable | |
| --- | --- |
| `DESK_PROFILE`, `DESK_PROFILE_FRAMES` | Frame timings in the log; a CSV of every frame. |
| `DESK_TODAY` | Pretend it is this date (`2026-12-24T18:00`). |
| `DESK_WEATHER` | Pretend the weather is `clear`, `clouds`, `overcast`, `fog`, `rain`, `snow` or `storm` (`rain:0.8` for how heavy). |
| `DESK_LOCK_PAM_DIR` | Check the lock's password against this PAM directory (only when not the real session). |
| `DESK_ROOM_GLSL` | Load the room's shader from this file. |
| `DESK_GPU_DUMP` | Write the shader as the driver receives it. |
| `DESK_SOUND_DEBUG` | Log each sound's latency. |
| `DESK_SYSFS` | Read the Control Panel's instruments from this directory instead of `/sys`. |

`DESK_SESSION`, `DESK_IM` and `DESK_NESTED` are set by `desk-session` and
`desk-session-helper` for their own use. Old `DESKWM_*` names still work.

## Starting over

- Settings: delete `~/.config/desk/` (or one file in it).
- The traced room: delete `~/.cache/desk/` and it is traced again at the next
  start.
- Notes and memo: `~/.local/share/desk/`.
