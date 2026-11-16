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
| `$XDG_CONFIG_HOME/desk/drawer` | DEsk | The programs in the drawer, in order. |
| `$XDG_CONFIG_HOME/desk/drawer-looks` | DEsk | What a program in the drawer looks like when it has been given another look (the other button on it): `<look> <what the drawer's file calls it>`, the look one of `card`, `icon`, `terminal`, `compass`, `folder`, `notepad`, `screwdriver`, `calculator`, `typewriter`, `notebook`, `camera`, a look's folder's name (`radio`, `paintbox`, `gamepad`, `headphones` are DEsk's), or `own/<folder or file>`. |
| `$XDG_CONFIG_HOME/desk/desk-mat` | DEsk, `desk-mat` | The prints under the clear mat, the bottom one first: `paper = <print> <x> <y> <width> <turn>`, its middle x, y points from the middle of the mat (right, and down the screen), width points across, turned that many degrees clockwise. DEsk reads it again when it changes. |
| `$XDG_DATA_HOME/desk/mat/` | `desk-mat` | The prints themselves: each picture made a PNG no larger than 2048 pixels across and turned as its camera was held, so the picture can go from where it was and the print stays. One put back in its envelope on the desk is deleted. |
| `$XDG_DATA_HOME/desk/looks/` | you, `desk-look` | Your own looks for a program in the drawer (docs/LOOKS.md): folders, each `look.png` (a thing seen from straight above, nothing round it) and `look.ini` (`Name`, `Name[ja]` and the rest, `Size` and `Thick` in centimetres, `Order`), or only a PNG, as long as a card is. `$XDG_DATA_DIRS/desk/looks/` holds everyone's on the machine. |
| `$XDG_CONFIG_HOME/desk/shelves.conf` | you | Programs filed on a shelf other than their own. |
| `$XDG_CONFIG_HOME/desk/favorites` | DEsk | The favourites: the programs whose sheets have a flag stuck on them, a desktop file id a line, in the order they were flagged. They are the slim binder at the left end of the bookcase's row. DEsk reads it at login and writes it whole when a flag goes on or comes off. |
| `~/.config/desk-mimeapps.list` | you | Default programs inside DEsk only, before `mimeapps.list`. |
| `$XDG_DATA_HOME/desk/calendar` | DEsk | Notes stuck on the calendar. |
| `$XDG_DATA_HOME/desk/memo` | DEsk | The memo pad's sheets. |
| `$XDG_DATA_HOME/desk/calc` | DEsk | What the calculator has worked out, and its memory. |
| `$XDG_STATE_HOME/desk/calculator` | DEsk | How much bigger the calculator has been made (`scale`, 0.6 to 2.2) and whether it was open (`open 1`), for the next time it comes out. |
| `$XDG_DATA_HOME/desk/notebook/` | desk-notepad | The notebook's pages, two files to a page: `page-00001.txt`, what is typed on it, and `page-00001.ink`, what is written on it with a pen (each line one stroke: its ink, the character of the line it hangs from, its points and how hard they were pressed) and in its heading: lines `title`, `year`, `month` and `day`, what is typed there, and lines beginning `h`, what is written there with a pen (its ink, 1 the title or 2 the date, and its points in 24-pixel units from the sheet's top left, or for the date its top right). A blank page has none. `over/` keeps what is written over text files, by the file's name, when the file is saved. The journal's pages (`desk/journal/`) are brought here once, if the notebook has none. |
| `$XDG_STATE_HOME/desk/pencil-case` | DEsk (written), desk-notepad (read) | What is out of the notebook's pencil case: `pen black\|blue\|red\|green`, the refill that is out, and `hand none\|pen\|marker\|rubber`, what has been taken out into the hand; and `by X Y`, where the case lay by the notebook, from its sheet's top left, when they last went from the desk (it comes out there with the next). |
| `$XDG_CONFIG_HOME/desk/notepad.conf` | desk-notepad | The notebook's letter size, wrapping, typewriter letters and the ruling of a file's page; the notebook's page it was open at; and, for when there is no pencil case (not on DEsk, or with the whole screen), the pen chosen on the page under a turned-up one. |
| `$XDG_STATE_HOME/desk/notepad/` | desk-notepad | Copies of what has not been saved, until it is. |
| `$XDG_CACHE_HOME/desk/` | DEsk | The traced room, icons converted from SVG, your picture for the door, the table handwriting is read with (`handwriting-*.table`, made from your fonts). Safe to delete; it is made again. |
| `$XDG_STATE_HOME/desk/session.log`, `session.log.old` | `desk-session` | What DEsk said this session and the one before. |
| `$XDG_STATE_HOME/desk/install.log` | `install_session.sh` | What the last install run through it said (one run straight from `install_<system>.sh` says it only on the terminal). |
| `$XDG_STATE_HOME/desk/cpu-letter` | DEsk | When the "running on the CPU" letter was last sent. |
| `$XDG_STATE_HOME/desk/night` | DEsk | There while NIGHT on the Control Panel's lid is on: every screen keeps its warm colours from one login to the next. |

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
| `binder_ink` | `faded`, `normal`, `dark`, `darkest`, or `0` to `1` | `normal` | How dark the names on the bookcase's binders are typed. |
| `camera_bounce` | `normal`, `low`, `off` | `normal` | How far the view swings past and settles. |
| `language` | `auto`, `en`, `ja`, `zh`, `ko`, `es`, `fr`, `de` | `auto` | The desk's words and the names programs are filed under. `auto` follows `LC_ALL`, `LC_MESSAGES`, `LANG`. |
| `clock` | `24h`, `12h` | `24h` | |
| `desk_wood` | `walnut`, `oak`, `cherry`, `maple`, `ebony`, `leather`, `linoleum` | `walnut` | What the desk is made of: its top, sides and drawer. `leather` (green, tooled in gold) and `linoleum` (grey-green) are let into a walnut rim. The bookcase and the console stay walnut. The room is traced again for another, which takes a moment. |
| `desk_stain` | `natural`, `honey`, `brown`, `dark`, `grey` | `natural` | Over the desk's wood, the grain showing through. Ebony is not stained `brown` or `dark` (on black they are one colour): either is taken as `natural` on it. |
| `desk_mat` | `none`, `green`, `navy`, `burgundy`, `charcoal`, `cream`, `#rrggbb`, `photo`, `clear` | `none` | A mat on the desk, under the windows. A colour is felt; `photo` lays the picture in `desk_mat_photo`; `clear` is a clear sheet over all the open desk, with pictures under it (`desk_mat_under`, [The clear mat](#the-clear-mat)). |
| `desk_mat_photo` | an absolute path | (none) | The picture for `desk_mat = photo`, and for a clear mat with `desk_mat_under = whole`; Shell Settings' "The mat's picture" chooses one (`desk-mat pick --whole`). A PNG of up to 4 megapixels is read by DEsk itself; a larger one, and JPEG, WebP and the rest, by gdk-pixbuf's thumbnailer (or ImageMagick for `.png`, `.jpg`, `.webp`, `.gif`, `.bmp` and `.tif`), each in a process of its own. It must be a regular file of up to 64 MB (a PNG up to 48 megapixels). Until it is read, or if it can't be (the log says), the mat is charcoal cloth; it is tried again when the settings are applied again. |
| `desk_mat_size` | `small`, `medium`, `large`, `full`, or a percentage (`60%`) | `medium` | How big the mat is, in points like the windows, so a larger screen shows more desk rather than a larger mat: about 600 by 380 points, 900 by 530 and 1160 by 650; `full` is all the open desk. A percentage is a place on the way from the least to `full`; no mat is bigger than the open desk. A clear mat is always all the open desk. |
| `desk_mat_under` | `free`, `whole` | `free` | What lies under a clear mat: `free`, prints laid where you like ("Free" in Shell Settings); `whole`, the picture in `desk_mat_photo` all over it ("Full screen"). |
| `lights_at_login` | `yes`, `no` | `yes` | The room starts dark and the lamp comes up as it is made. |
| `celebrations` | `yes`, `no` | `yes` | The door and clock on certain days (not in Shell Settings). |

#### The clear mat

`desk_mat = clear` lays a sheet of clear vinyl over all the open desk, as an
office desk has under its glass: the boards are seen through it, the lamp lies
in it, and under it lie pictures.

- **Free** (`desk_mat_under = free`): prints, each where it was laid. Shell
  Settings' "Pictures under the clear mat" chooses pictures (`desk-mat pick`) and
  lays them near the middle, or, where a window is over the middle, in the nearest
  place on the mat that is clear of windows; if the mat was not a clear one with
  its pictures laid where you like, it is made one. To move them, rest the pointer on a corner of the
  mat, where it comes up a little, and press: the mat is lifted at that corner.
  Then a print taken up goes where the pointer goes and stays where it is let go,
  on top of the rest; the wheel over one turns it, and with Ctrl makes it larger
  or smaller. Beside the lifted corner lies the envelope the prints came in:
  pressed, it chooses more; a print put into it is gone. A press anywhere else on
  the desk, on a window, or Escape lays the mat down again.
- **Full screen** (`desk_mat_under = whole`): the picture in `desk_mat_photo`
  under all of it. Lifted, the envelope chooses another.

`desk-mat` does the same from a terminal:

```bash
desk-mat add PICTURE... [--width POINTS]   # prints made of pictures, laid under the mat (made clear, free)
desk-mat whole PICTURE                     # that picture all over the mat (made clear, full screen)
desk-mat pick [--whole]                    # chosen from anywhere (what Shell Settings runs)
desk-mat list                              # what lies under it, and where
desk-mat where                             # where the prints and their file are
```

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

### Wood Maintenance

Termites that come out onto the desk when it is left alone, and nibble at it.
Their marks are only a picture. The ones in the wood are kept in
`$XDG_STATE_HOME/desk/wood-damage-v1.txt`; Restore the desk in Shell Settings
takes them all away.

| Key | Values | Default | |
| --- | --- | --- | --- |
| `termites_enabled` | `yes`, `no` | `no` | Termites come when the desk is left alone. |
| `termites_wait` | `30`, `60`, `120`, `300` | `60` | Seconds left alone before the first comes. |
| `termites_level` | `curious`, `colony`, `infestation` | `colony` | One, a few, or a lot. |
| `termites_chew_windows` | `yes`, `no` | `yes` | Nibble the edges of windows (the marks go with the window). |
| `termites_chew_folded` | `yes`, `no` | `yes` | Nibble folded windows. |
| `termites_damage_persistent` | `yes`, `no` | `yes` | Keep their marks in the wood until restored. |

### Keyboard and pointer

| Key | Values | Default | |
| --- | --- | --- | --- |
| `tap_to_click` | `yes`, `no` | `yes` | |
| `onscreen_keyboard` | `auto`, `always`, `never` | `auto` | When the keyboard on the screen comes up by itself: with fingers and no keyboard to type on, always, or never. It is in the drawer as well. |
| `touch_screens_swapped` | `yes`, `no` | `no` | Two touch screens (and their pens) on two screens alike, each working the other. |
| `natural_scrolling` | `yes`, `no` | `no` | A touchpad. |
| `natural_scrolling_mouse` | `yes`, `no` | `no` | A mouse's wheel. |
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
| `take_sheet` | `yes`, `no` | `yes` | A program taken from ALL APPS has its sheet carried down first. |
| `app.terminal`, `app.browser`, `app.files`, `app.editor` | a desktop file id (`xfce4-terminal.desktop`) | | The standard programs, instead of DEsk's choice. Shell Settings sets `app.terminal` (System, Look and language: Terminal); the others follow Default programs. |

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

## favorites

```
# desktop file id, a line
firefox-esr.desktop
gimp.desktop
```

A flag stuck on a program's sheet (in any binder, or in the margin of its
line in the index book) puts it here; peeled off, it goes. The favourites
binder files them by name.

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
| `DESK_BLENDER` | For `desk-look bake`: the Blender to render a model with, if `blender` is not on the PATH. |

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
| `DESK_POWER_DRY` | Say in the log what the Control Panel would ask logind for (`PowerOff`, `Reboot`, `Suspend`) and what it would set the power profile to, and do neither. |

`DESK_SESSION`, `DESK_IM` and `DESK_NESTED` are set by `desk-session` and
`desk-session-helper` for their own use. Old `DESKWM_*` names still work.

## Starting over

- Settings: delete `~/.config/desk/` (or one file in it).
- The traced room: delete `~/.cache/desk/` and it is traced again at the next
  start.
- Notes and memo: `~/.local/share/desk/`.
