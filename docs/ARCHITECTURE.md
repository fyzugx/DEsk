**English** | [日本語](ARCHITECTURE.ja.md)

# DEsk architecture

This is the technical overview of DEsk v0.1 (BETA): what the programs are,
how the compositor is put together, how the room is drawn, which threads
exist and what they own, and where everything is kept. It is meant for
people reading or changing the code. For what DEsk looks like and how to use
it, read the [README](../README.md); for recipes for common changes, read
[MODDING.md](MODDING.md); for every setting and file, read
[CONFIGURATION.md](CONFIGURATION.md). The full development log, in Japanese,
is [design.md](design.md).

## 1. Principles

These decide most questions about where code goes and what it does.

- **The desk is a place, not a skin.** Windows are sheets of paper, the
  dock is a drawer, the app list is a bookshelf, notifications are letters
  in a post box. A new feature is designed by asking what the thing on the
  desk would do, not by adding a button.
- **Make only what the room is, or what only a compositor can do.** Settings
  panels, applets, file managers and so on are borrowed from existing free
  software (Xfce, MATE, LXQt, GNOME, NetworkManager...). Shell Settings opens
  them. The line is drawn in `src/settings.c` (top comment) and in the
  README's "As a desktop" table.
- **One lamp, one room.** The room (desk, drawer, bookcase, Control Panel,
  post box) is a single ray-traced scene. It is traced once per screen
  shape and quality level, cached on disk, and composited under everything
  else. Everything drawn over it (paper, letters, the clock's light) is
  shaded by the same lamp.
- **Sizes are in points.** One desk unit is 45 points and 1440 x 900 is the
  1x reference. A bigger screen shows more desk, not bigger things
  (`desk_px_per_pt`, `DESK_SCALE`).
- **The compositor thread never waits.** Tracing, disk walks, icon
  conversion, mixer queries, PAM, the weather and sound all run on threads
  of their own and hand results back through the event loop.
- **Light enough for old machines.** The target is a 2010 laptop running Low
  at 60 fps. Every level (Ultra, High, Medium, Low, 2D Fallback) is the same
  code with less work done, never a second implementation.
- **English first, then the world.** UI words are written in English in the
  code and translated through a table (seven languages in v0.1). Seasonal
  touches are ones every culture shares (a birthday, the new year).

## 2. The programs

| Program | Source | What it is |
| --- | --- | --- |
| `desk` | `src/wm.c` + the drawing library | The Wayland compositor: the whole desktop. Also the GPU trial child (`desk-gpu-trial`, the same executable run with `DESK_GPU_PROBE`). |
| `desk-session` | `session/desk-session` (sh) | What a login manager starts. Sets up the session environment (Wayland-first toolkit variables, the input method, DEsk's dconf layer), keeps `~/.local/state/desk/session.log`, and runs `desk` with `DESK_SESSION=1`. |
| `desk-session-helper` | `scripts/desk-session-helper` (sh) | Picks and starts the parts DEsk does not draw (polkit agent, keyring, IBus or fcitx, the network applet, udiskie, battery notices) from DEsk's own autostart entries. |
| `desk-settings` | `src/settings.c` | Shell Settings. A normal Wayland client drawn with the same library, writing `shell.conf`. |
| `desk-index` | `src/index.c` | The DEsk Experience Index (benchmarks scored 1.0 to 9.9). `--text` for a terminal, `--suggest` for the installer's choice of Graphics level. |
| `desk-preview` | `src/preview.c` | Renders the desk, views and parts to a PNG with no compositor: the main tool for checking drawing changes. |
| `desk-gputest` | `src/gputest.c` | Checks the GPU path. `desk-gputest --room` traces every sheet on the GPU and the CPU and compares them. |
| `desk-ctl` | `scripts/desk-ctl` (sh) | `reset` and `logout` for a stuck desk, from a terminal or another VT. |
| `desk-window` | `scripts/desk-window` (sh) | Runs DEsk as a window inside the current session. |
| `desk-screenshot` | `scripts/desk-screenshot` (sh) | Print / Alt+Print: grim, a file in Pictures, and a letter. |

### How a session starts

1. The login manager runs `desk-session` from
   `/usr/share/wayland-sessions/desk.desktop`.
2. `desk-session` sets `XDG_CURRENT_DESKTOP=DEsk`, the toolkit backends, the
   input method choice and the dconf profile, then runs `desk`.
3. `desk` takes an exclusive `flock` on `$XDG_RUNTIME_DIR/desk.lock` (one desk
   per user; `DESK_ALLOW_MULTIPLE=1` to override), starts wlroots, and
   paints the first frame while the room is read from the cache or traced.
4. Once the display exists, `src/session.c` tells D-Bus and systemd where it
   is (`dbus-update-activation-environment --systemd`) and runs the XDG
   autostart entries, leaving out ones that would fight DEsk (another
   notification daemon, an X screen locker, other desktops' polkit agents).
5. DEsk's own autostart entries (`session/autostart/*.desktop`,
   `OnlyShowIn=DEsk`) run `desk-session-helper` for each borrowed part.

Logging out, restarting and powering off ask every window to close first
(`end_begin` in `wm.c`) and then go through logind (`src/power.c`).

## 3. Source map

| File | Lines | Role |
| --- | ---: | --- |
| `src/wm.c` | ~22,500 | The compositor: wlroots setup, outputs, input, windows (xdg-shell and Xwayland), the scene layers, every interactive part of the desk (drawer, views, calendar, memo, post box, tray, clock, lock, Ctrl+Alt+Del), `shell.conf` and `displays.conf`, multi-screen. |
| `src/draw.c` | ~34,900 | The drawing library: the ray-traced room (C reference tracer, GPU driver, cache), and every cairo-drawn thing (paper, frames, folding, crumpling, the book and binders, the Control Panel's live parts, letters, the calendar and memo pad, Shell Settings' form, the front door). |
| `src/room.glsl` | ~2,400 | The room's tracer as a GLES 3.00 fragment shader: a line-for-line port of the C in `draw.c`, which stays the reference. |
| `src/gpu.c` | ~800 | Offscreen EGL/GBM/GLES context for the shader; picks the render node the screen uses. |
| `src/apps.c` | ~2,000 | Scans `.desktop` files on a worker thread, files them on eight shelves, finds icons, picks the standard terminal/browser/files/editor, launches programs. |
| `src/i18n.c` | ~1,700 | The word table (seven languages) and month/day names. `include/i18n.h` explains the scheme. |
| `src/fonts.c` | ~280 | Which typeface each role uses, per CJK region. |
| `src/settings.c` | ~3,200 | `desk-settings` (Shell Settings). |
| `src/index.c` | ~4,500 | `desk-index`. |
| `src/post.c` | ~440 | The notification daemon (`org.freedesktop.Notifications`). |
| `src/tray.c` | ~1,350 | StatusNotifierItem watcher and host (the tray slips). |
| `src/media.c` | ~800 | MPRIS client (the clock's MEDIA page, media keys). |
| `src/ime.c` | ~760 | Which input method is on (IBus, fcitx5). |
| `src/session.c` | ~470 | Autostart and the session environment. |
| `src/lock.c`, `src/user.c` | | PAM password check (on a thread); the user's name and picture for the door. |
| `src/power.c` | | Log out / restart / power off through logind. |
| `src/panel.c` | ~950 | The Control Panel's readings from `/proc`, `/sys` and the mixer. |
| `src/devices.c` | ~700 | What the Control Panel's tuners list and do: Wi-Fi networks (`nmcli`), Bluetooth devices (`bluetoothctl`), sound devices (`pactl`), on a thread of their own. |
| `src/occasion.c` | ~530 | Dates (birthday, the new year...) and the weather at the door (open-meteo.com, opt-out). |
| `src/agenda.c`, `src/memo.c` | | The calendar's notes and the memo pad's sheets, saved under `$XDG_DATA_HOME/desk`. |
| `src/camera.c` | ~260 | Whether a camera or microphone is in use (the clock's two lamps). |
| `src/sfx.c`, `src/sound.c` | ~2,750 | Every sound synthesised at start-up, and a mixer thread that plays them (libpulse, or ALSA loaded at run time). |
| `src/preview.c`, `src/gputest.c` | | Developer tools (see §2). |
| `include/desk.h` | ~1,200 | The drawing library's API, as used by `wm.c`, `settings.c`, `index.c` and the tools. |
| `include/compat.h` | | Carries settings and data over from DEsk's old name (DeskWM). |
| `protocols/` | | wlr-output-management and wlr-output-power-management XML. |
| `scripts/` | | Installers, session scripts, build helpers (`glsl2c.py`, `room-key.py`), `check-words.py` (the translation test). |
| `session/` | | Session entry, PAM file, autostart entries, default applications list, portal configuration. |
| `themes/` | | The DEsk icon theme (generated by `themes/make-icons.py`) and GTK theme. |
| `packaging/` | | A Debian equivs file for the recommended program set. |
| `docs/` | | These documents and the development log. |

Both large files are organised in sections marked with comments of the form
`/* ---- name ---- */` (and `/* ==== */` banners in `draw.c`). Searching for
`---- ` gives a table of contents. Each section begins with a comment saying
what it does and why.

## 4. The compositor (`src/wm.c`)

DEsk is built on **wlroots 0.18**. The backend and renderer are whatever
wlroots picks (DRM and libinput under a login session; the Wayland or X11
backend when nested; headless for tests); the renderer is GLES2 when there
is a GPU and pixman otherwise. One `struct desk_server` holds the state.

### 4.1 The scene graph

Everything on screen is a node in one `wlr_scene`. The layers are trees
created in this order, bottom to top:

| Layer | Contents |
| --- | --- |
| `layer_side` | The plain desk on screens other than the desk's own (§4.6). |
| `layer_bg` | The traced room (one buffer), moved by the camera and the drawer. |
| `layer_desk` | Live parts drawn over the room: the clock, the Control Panel's lamps and needles, the post box's letters, slips, the light under the pointer. |
| `layer_folded` | Folded windows lying on the desk (under every window). |
| `layer_win` | Windows: each a tree with its paper frame, shadow, corner and the client's surfaces. |
| `layer_full` | A fullscreen window. |
| `layer_xover` | X11 override-redirect windows (menus, tooltips) and input-method candidate windows. |
| `layer_drawer` | The drawer, pulled out over the desk, and what lies in it. |
| `layer_parked` | Sheets being folded or opened out (over the windows while they move). |
| `layer_shelf`, `layer_console`, `layer_post` | The three views you turn to: the bookcase, the Control Panel, the post box. |
| `layer_top` | The dark and the counting clock at login, a letter on a first start. |
| `layer_drag` | Whatever is being carried. |
| `layer_lock` | The front door (the screen lock), with black over every screen. |
| `layer_glow` | Light from fireworks on the new year. |
| `layer_dark` | The black of a screen that has gone dark when nested. |

The desk layers (`layer_bg`, `layer_desk`, `layer_drawer`) move together
when the drawer opens and during the start of a camera move; the paper
layers (`layer_folded`, `layer_win`, `layer_xover`, `layer_parked`) move with
them (`desk_follow`).

### 4.2 Buffers and sprites

DEsk draws with **cairo on the CPU** into `struct desk_buf` (a `wlr_buffer`
wrapping a cairo image surface) and hands the buffers to the scene with
`scene_put`. Anything that changes often is a *sprite*: a buffer covering
only what changed, keyed by a hash of what it shows (`struct desk_sprite`,
`sprite_paint`), so an unchanged thing is never drawn again. Over the room,
drawing is done at an overlay resolution that depends on the level
(`desk_overlay_size`) and scaled by the scene, which keeps large screens
cheap. Every source box goes through `scene_source`, which keeps it inside
its picture to the last bit (wlroots asserts on a box that reaches past the
picture, and one worked out in floating point can by a rounding error), and
a node is given a new picture through `scene_swap`, which does the same for
the box it had. A picture's size is kept beside its node (`scene_new`,
`pic_size_keep`, read with `scene_pic_size`): on a GLES2 screen the scene
lets go of the picture (`buffer` becomes NULL) once it is on the GPU, so
nothing may read the size off `buffer`.

### 4.3 A frame

`output_frame` runs once per output frame. On the desk's screen (or any
screen if that one has not had a frame for 12 ms) it calls
`output_frame_work`, which advances every animation by real time: folds,
paper shadows, corners, resizing, throwing, the drawer, the camera moves to
the views, binders, the calendar and memo pad, the lamp at login, the lock,
the clock and the tray slips. Then the scene is committed. Nothing is drawn
on a frame unless something changed; an idle desk commits nothing.

### 4.4 Input

Pointer events are routed in a fixed order (`process_cursor_motion`,
`server_cursor_button`, `server_cursor_axis`):

1. the lock (only the key on the door);
2. something being carried (from the drawer, or a folded sheet);
3. a window being moved or resized, the clock's volume bar, the tray card;
4. on a screen other than the desk's, windows only (§4.6);
5. the open view: the post box, the Control Panel, the bookcase (binders,
   book, calendar, memo pad);
6. the desk itself, when no window covers the point: clock, slips, the ALL
   APPS plate, the Control Panel and post box plates, letters lying on the
   desk, the drawer handle;
7. windows: the turned-down corner, the frame's edges and heading, then the
   client.

Keyboard shortcuts are handled in `handle_keybinding`. Typing into the
desk's own text fields (the book's search line, calendar notes, the memo
pad) and into programs goes through the input method: DEsk implements
`text-input-v3` and bridges it to IBus itself (the section "typing into
programs' windows, through the input method"), so candidates appear under
the text.

### 4.5 Windows

`struct desk_toplevel` represents a window from either xdg-shell or
Xwayland. A window asks for server-side decoration (xdg-decoration or KDE's
server-decoration) and gets DEsk's paper frame; one that draws its own frame
is laid on paper anyway unless `paper_for_all_windows = no`. The top-right
corner is the only control: Fold (minimise into a folded sheet), Unfold or
Refold (maximise and restore), Discard (close, crumpled and thrown). Closing
always asks the client first.

### 4.6 More than one screen

The desk is drawn on one screen: the one at layout position 0,0
(`first_output`). Every other screen gets a plain desk (`side_desks_paint`,
`desk_draw_side_desk`): a band of the desk top cut from the room and laid with
mirrored repeats so the boards continue across the border. Windows can be
placed there. Maximise and fullscreen use the screen the window is on; new
windows open on the screen the pointer is on. Nothing done on the desk's
screen (the drawer, the views) moves windows on the others. When a screen is
removed, its windows move to the nearest remaining screen
(`windows_onto_screens`). The arrangement a display tool sets is remembered
per set of screens in `displays.conf`.

### 4.7 Protocols

wlroots globals created: compositor and subcompositor, xdg-shell,
xdg-decoration and KDE server-decoration, xdg-output, xdg-activation, seat
with data-device and primary selection, data-control, text-input-v3,
viewporter, presentation-time, single-pixel-buffer, linux-dmabuf (with a
GPU renderer), screencopy (for grim), idle-notify and idle-inhibit, output-management and
output-power-management, pointer-constraints and relative-pointer (games).
Not yet provided: layer-shell, session-lock, gamma-control, input-method-v2.

### 4.8 D-Bus

With libsystemd (`-Dpost=enabled`), DEsk uses sd-bus on the event loop's
thread:

- `org.freedesktop.Notifications` server (`post.c`): letters in the post box.
- StatusNotifierWatcher (when no other is on the bus) and host (`tray.c`).
- MPRIS client (`media.c`): what is playing, media keys.
- IBus (its own bus) and fcitx5 (`ime.c`); logind for ending the session.

## 5. The drawing library (`src/draw.c`)

`draw.c` builds into the static library `desk-draw` with `apps.c`,
`panel.c`, `agenda.c`, `fonts.c`, `i18n.c`, `memo.c`, `user.c` and (with
EGL) `gpu.c`. `include/desk.h` is its API. The compositor, Shell Settings,
the Experience Index and the preview all link it, so a thing looks the same
wherever it is drawn.

### 5.1 The room

The room is a **signed distance field** (`scene_map` in `draw.c`) marched
once per sample. A sample goes through the march, the normal, materials,
bump and wear, ambient occlusion, a soft shadow from the one area lamp,
spherical-harmonic ambient light, one bounce of reflection and a clear coat,
and the samples are resolved with coverage, an owner vote (which object a
pixel belongs to, for hit testing) and a glow pass.

It is traced as **layers** (sheets): the room without the drawer, the drawer
alone (pulled out), the bookcase seen from the desk, the Control Panel, and
the post box. Sliding the drawer sheet over the room sheet is what lets the
drawer open at full frame rate. The camera moves to each view are traced as
short films. After the views come two more for the Control Panel's lid: the
console without its lid through a wider lens from the same place (layer 7,
what the view pulls back over), and the lid on its own folding open, a frame
at a time (layer 6), each cut to the lid's part of the frame.

There are two tracers:

- **C** (`draw.c`): the reference, run on all CPU cores in tiles.
- **GLSL** (`room.glsl` via `gpu.c`): a port of the same code, run in an
  offscreen GLES 3.0 context on the render node the screen uses. Every
  named constant is passed from `draw.c` to the shader (`room_glsl_preamble`),
  so the two cannot disagree about geometry. A driver is first tried in a
  child process (`desk-gpu-trial`) so a crashing driver cannot take the desk
  down; if it fails, the CPU traces.

`desk-gputest --room` compares the two tracers sheet by sheet.

### 5.2 Levels and the cache

| Level | What it does |
| --- | --- |
| Ultra | Finer trace, wider on a GPU; nothing drawn coarser while moving. |
| High | As designed (default). |
| Medium, Low | Smaller trace and overlays; paper in motion drawn coarser. |
| 2D Fallback | Traced once, small, one sample a pixel (softened); no shadows, fades, blur or animation. Chosen automatically when the screen is rendered on the CPU. |

Traced sheets are kept in `$XDG_CACHE_HOME/desk` as PNGs named by sheet,
size, desk width, level and `ROOM_KEY`. `ROOM_KEY` is a hash of
`room.glsl` and `gpu.c` made at build time (`scripts/room-key.py`), so a
change to the room's code invalidates old sheets while ordinary updates keep
them.

### 5.3 Flat drawing

Everything else is cairo: window frames and paper (with grain, ageing,
creases), folding (`desk_fold_*`), crumpling, the book and binders, letters,
the calendar and memo pad, Shell Settings' form (`desk_shell_form_*`), the
Control Panel's live parts, the clock's displays, and the front door (a
second, small ray tracer for the door and the walk in). Text goes through
`fonts.c` (a Latin face and a CJK face per role) and `wrap_lines` (word
breaking, CJK breaking and line-start rules).

## 6. Threads

| Thread | Owner | What it does |
| --- | --- | --- |
| Compositor | `wm.c` | The wlroots event loop: input, protocol, drawing sprites, D-Bus. |
| Room worker and tracers | `draw.c` | Read or trace the room for the current screen and level; CPU tracing runs on every core in tiles; the GPU context lives on the room thread. |
| GPU trial | `draw.c` | A child process, not a thread: tries a driver before trusting it. |
| Door tracer | `draw.c` | The lock screen's scene, on every core when it is drawn. |
| App scanner | `apps.c` | Walks the application directories and publishes an immutable snapshot (`desk_apps_acquire`/`release`). |
| Icon jobs | `apps.c`, `tray.c` | Find and convert icons (SVG through rsvg-convert, else ImageMagick, when needed). |
| Texture warm-up | `draw.c` | Makes paper and wood tiles at start-up. |
| Mixer | `sound.c` | Mixes the desk's sounds and feeds libpulse or ALSA. |
| Volume | `panel.c` | Asks and sets the mixer through `wpctl`. |
| Weather | `occasion.c` | One `curl` (or `wget`) to open-meteo.com every half hour, when enabled. |
| Password check | `lock.c` | PAM, which sleeps after a wrong password. |

Shared state is guarded by mutexes (`g_room_lock`, `g_render_lock`, the app
list's reference count) or is atomic (the room's progress counters, its
shape numbers). Workers never call into wlroots or the scene. They publish
a result under a lock, with a serial or generation number (a new room, a
new snapshot of the app list), and the compositor picks it up on its own
thread at its next tick; or they wake the event loop through a pipe or
eventfd it watches (the weather, the tray's icons, the password check).
v0.1 was run under ThreadSanitizer, AddressSanitizer and
UndefinedBehaviorSanitizer (§9).

Signals are caught, never blocked: a handler writes one byte down a pipe the
event loop watches (`cad_signals`), and the loop acts on it. `desk-ctl`'s
USR1 and USR2 reset the desk and end the session; TERM, HUP and INT (logind
ending the session, ^C on a desk in a window) end the desk through the same
teardown as logging out, so what is in hand (the memo, a calendar note) is
kept. A blocked signal would be handed down through exec to every program
the desk starts; a handler is not.

## 7. Files

| What | Where |
| --- | --- |
| Settings | `$XDG_CONFIG_HOME/desk/shell.conf` (the machine's defaults in `/etc/xdg/desk/shell.conf`) |
| Screen arrangements | `$XDG_CONFIG_HOME/desk/displays.conf` |
| Drawer contents | `$XDG_CONFIG_HOME/desk/drawer` |
| Shelf overrides | `$XDG_CONFIG_HOME/desk/shelves.conf` |
| Default programs | `~/.config/desk-mimeapps.list`, then the freedesktop.org lists |
| Calendar notes, memo pad | `$XDG_DATA_HOME/desk/calendar`, `$XDG_DATA_HOME/desk/memo` |
| Traced room, icons, door | `$XDG_CACHE_HOME/desk/` |
| Logs | `$XDG_STATE_HOME/desk/session.log` (and `.old`), `install.log` |
| For Shell Settings | `$XDG_RUNTIME_DIR/<WAYLAND_DISPLAY>.render` (whether the screen is drawn on the CPU) |
| One desk per user | `$XDG_RUNTIME_DIR/desk.lock` (holds the PID; `desk-ctl` reads it) |

Every file DEsk writes is written to a temporary name and renamed into
place. Formats are in [CONFIGURATION.md](CONFIGURATION.md).

## 8. Languages, fonts, sound

- **Words**: `TR("English")` looks the English up in `WORDS` (`src/i18n.c`),
  a table with one column per language (`enum desk_lang`: en, ja, zh, ko,
  es, fr, de). A missing translation shows the English. Keys may carry a
  context, `"clock|DAY"`. `desk-index` keeps its own table in `index.c`.
  `meson test` checks both tables (`scripts/check-words.py`).
- **Fonts**: each role (UI, print, display, engrave, type, screen, hand) has
  a Latin list and a CJK list; the CJK list is chosen again for Chinese,
  Japanese or Korean (Noto CJK SC, JP or KR), because shared characters are
  drawn differently in each.
- **Sound**: nothing is a recording. `sfx.c` synthesises every sound from
  modes, noise, crackle and creaks at start-up; `sound.c` plays variants,
  panned to where the thing is on screen.

## 9. Building and checking

```sh
meson setup build && meson compile -C build
meson test -C build                       # the translation tables
./build/desk-preview -o out.png --shelf   # draw without a compositor
./scripts/desk-window                     # run nested in the current session
```

For v0.1 the following were run:

- four build configurations with no warnings: minimal
  (`-Dpost=disabled -Dsound=disabled -Dxwayland=disabled`), `-Dwerror=true`,
  `-D_FORTIFY_SOURCE=3`, and AddressSanitizer + UndefinedBehaviorSanitizer;
- a strict warning set (`-Wshadow -Wformat=2 -Wnull-dereference
  -Wduplicated-cond -Wlogical-op ...`) and GCC's `-fanalyzer` over every file;
- ThreadSanitizer on the compositor, the preview and the Experience Index;
- ASan/UBSan runs of the nested desk, Shell Settings and the Experience Index,
  including with randomly malformed configuration, data and `.desktop`
  files;
- every view rendered in all seven languages.

`scripts/verify.sh` builds the Debian 13 image in the `Dockerfile` and runs
DEsk in it headless.

## 10. What is stable

v0.1 has **no plugin API**. Everything in `include/` is internal and may
change between releases. The stable interfaces are the standard ones DEsk
implements: Wayland protocols (§4.7), freedesktop.org notifications,
StatusNotifierItem, MPRIS, XDG autostart and `.desktop` files, and the files
in §7, whose keys v0.1 promises to keep reading. Changes to DEsk itself are
made by changing its source; [MODDING.md](MODDING.md) explains how.
