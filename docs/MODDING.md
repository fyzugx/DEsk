**English** | [日本語](MODDING.ja.md)

# Changing DEsk

DEsk v0.1 has no plugin API: a mod is a change to the source, built and
installed like DEsk itself. This page tells you how to set up for that,
what the house rules are, and walks through the changes people make most
often. Read [ARCHITECTURE.md](ARCHITECTURE.md) first for the map of the
code.

DEsk is MIT-licensed. Keep the `LICENSE` file with your copy and say in your
README what you changed.

## 1. Setting up

```sh
meson setup build                  # debug-optimised build in ./build
meson compile -C build
meson test -C build                # checks the translation tables
```

What the build needs is in the README ("Requirements"); on Debian, Ubuntu or
Arch, `scripts/install_session.sh --bare` installs exactly that and builds
once.

Three ways to see a change, from cheapest to closest to real:

1. **The preview** draws without a compositor, into a PNG:

   ```sh
   ./build/desk-preview -o out.png -s 1440x900            # the desk
   ./build/desk-preview -o out.png --shelf --lang de       # the bookcase, in German
   ./build/desk-preview --help                             # every view it can draw
   ```

2. **Nested**: `scripts/desk-window` runs DEsk as a window in the session you
   are in (`DESK_SIZE=1600x900` for another size). It shares your real
   settings; to keep them apart, set `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
   `XDG_STATE_HOME` and `XDG_CACHE_HOME` to directories of their own.
   `DESK_ALLOW_MULTIPLE=1` lets it start while another DEsk is running.
   `WLR_X11_OUTPUTS=2` (nested in X11) gives it two screens.
3. **Installed**: `scripts/install_session.sh` again replaces the installed
   copy; log out and in.

What DEsk says as it runs is in `~/.local/state/desk/session.log` (or on
stderr when nested). `DESK_PROFILE=1` adds frame timings.

`DESK_HAND=file` drives the desk from a script, for trying a change the
same way every time (nested or `--headless`): a line at a time, `move X Y`,
`glide X Y MS`, `press`, `release`, `click`, `key NAME` (`super+NAME` with
Super), `choose wifi|bt|vol`, `wait MS`, `shot PATH` (the desk's screen),
`shotall PATH` (every screen) and `quit`; see `hand_cb` in `wm.c`. With
`DESK_SLOW=4` the screenshots catch what moves part way.

## 2. House rules

- **C11**, tabs, the brace style already used, `-Wall -Wextra` clean.
  Comments are full sentences that say *why*; read a few sections of
  `wm.c` and `draw.c` and write like them.
- **No new libraries** without a strong reason. The required set is
  wlroots, wayland, xkbcommon, cairo, pixman, fontconfig, libdrm and PAM;
  libsystemd, libpulse, libinput, EGL/GLES/gbm and Xwayland are optional and
  must stay optional (`#ifdef DESK_HAVE_POST`, `DESK_HAVE_GPU` and so on).
- **The compositor thread never blocks**: no disk walks, process waits or
  network on it. Use a thread and hand the result back (see
  ARCHITECTURE.md §6).
- **Sizes in points.** Multiply by `desk_px_per_pt(screen_w)` rather than
  scaling with the screen.
- **Words in English, through `TR()`**, with a row in the table for every
  language (§3).
- **Borrow before building.** If an existing free program already does the
  job, add it to the installers and open it from Shell Settings rather than
  writing a panel (§5).
- **Every level, every language.** Check Ultra and 2D Fallback, and look at
  the longest language (usually German) and a CJK one.

## 3. Words and languages

### A new word

1. Write it in English in the code: `TR("Put it away")`. A word that is two
   different words in another language takes a context: `TR("verb|Open")`.
2. Add a row to `WORDS` in `src/i18n.c`, one column per language in the
   order of `enum desk_lang` (en, ja, zh, ko, es, fr, de). Put `NULL` where
   you have no translation; the English is shown there.

   ```c
   { "Put it away", "片付ける", "收起来", "치우기", "Guardar", "Ranger", "Wegräumen" },
   ```

3. If it is a printf format, every translation must take the same
   conversions in the same order (`%d` where the English has `%d`).
4. `meson test -C build` checks every row (column count, duplicate keys,
   printf conversions).

Words used only by `desk-index` go in the table at the top of `src/index.c`
instead, looked up with `W()`.

### A new language

1. Add it to `enum desk_lang` in `include/i18n.h`, before
   `DESK_LANG_COUNT`.
2. `src/i18n.c`: add its code and English name to `CODES`; add a column to
   every row of `WORDS` (the table is regular; `scripts/check-words.py`
   shows how to read it) and a row to `MONTH_WHOLE`, `MONTH_SHORT`,
   `DAY_WHOLE`, `DAY_SHORT` and `DAY_LETTER`. If it is written without spaces
   between words, make `desk_lang_wide` return true for it.
3. `src/index.c`: a column in its `WORDS`, and its AM/PM words in
   `when_words` (`HALF`).
4. Where the date is ordered differently: `cal_year_word`,
   `cal_month_year`, `cal_week_range` and `clk_page_day` in `src/draw.c`;
   `when_words` and `stamp_date` in `src/index.c`.
5. `src/settings.c`: its name, in itself, in `LANG_WORDS`, and its code in
   `LANG_VALS`. `src/wm.c`: its code in the list `shell.conf`'s `language`
   accepts.
6. `src/apps.c`: its usual locale in `REGION` (`locale_build`), for the names
   programs are filed under.
7. If it uses CJK characters, the region's fonts in `src/fonts.c` (`CUTS`,
   `ONLY_FOR`, `desk_font_set_region`).
8. Check: `meson test`, then `desk-preview --lang xx` with `--shelf`,
   `--console`, `--post`, `--calendar`, `--memo`, `--agenda`, `--lock`.
   Long words are shrunk to fit in a few places; anything that still runs
   over needs a shorter word.

A row with no translation for your language shows English, and a name table
with no row for it shows the English names, so you can add a language a
piece at a time.

## 4. A new setting

A setting is a line in `shell.conf` that DEsk reads, and usually a row in
Shell Settings.

1. **`src/wm.c`**: add a field to `struct shell_conf`, its default in
   `shell_conf_defaults`, and read it: a yes/no key goes in the chain in
   `shell_conf_read_file`; anything else in `conf_other` (look at `clock` or
   `motion`). Report a bad value with `wlr_log(WLR_ERROR, ...)` and keep the
   default. Act on it in `shell_apply_all`, which runs at start-up and every
   time the file changes.
2. **`src/settings.c`**: add a row to `SETTINGS` (`S_CHECK` for yes/no,
   `S_CHOICE` with `*_WORDS` and `*_VALS` arrays for a choice), under the
   right section, with its default. Add the key to the template comment in
   `conf_write`.
3. **Words**: the label, the note under it, and each choice's word, in
   `WORDS` (§3).
4. **Documentation**: the key in [CONFIGURATION.md](CONFIGURATION.md) and in
   the Japanese README's Shell Settings table.

Keys DEsk does not know are reported in the log and kept in the file, so
older and newer versions can share one `shell.conf`.

## 5. Borrowed programs

### A row on Shell Settings' System tab

Each row opens a program that does the job. In `src/settings.c`:

1. A `struct prog` list of the programs that can do it, preferred first
   (`P_DISPLAYS` and so on), and a `struct opens` saying which package brings
   the first one on Debian/Ubuntu now, before Debian 13 / Ubuntu 26.04, on
   Arch and on Fedora (`O_DISPLAYS`; `""` where nothing packaged there does
   it). Name a Wayland global in `needs` if the program cannot work without
   one.
2. An `S_ACTION` row in `SETTINGS` with its label and note.
3. The package in `scripts/install_debian.sh`, `install_ubuntu.sh`,
   `install_arch.sh`, `install_fedora.sh` and `install-desktop.sh`, so the
   installers bring it. On Fedora, check that the program is not in a
   package of its own that the main one only recommends (`ibus-setup`, say):
   the Fedora installer leaves weak dependencies out.

The row shows the program's name ("Displays (wdisplays)"), opens the first
one installed, and says which package to install when none is.

### The standard programs

The drawer's terminal, browser, file manager and text editor are chosen in
`src/apps.c` ("the standard programs"): `shell.conf`'s `app.terminal` etc.,
then the default-applications lists, then `STD_TERMINAL`, `STD_BROWSER`,
`STD_FILES` and `STD_EDITOR` in order. To change DEsk's own preference,
reorder those lists; a user can override them with `app.*` in `shell.conf`
without rebuilding.

### Which shelf a program is filed on

Users can do this without code, in `~/.config/desk/shelves.conf`:

```
org.kde.kate.desktop = Development
```

The rules themselves are in `src/apps.c`: the Categories table, and
`SHELF_FIX` for programs whose own Categories mislead.

## 6. Sounds

All sounds are synthesised (`src/sfx.c`). To add one:

1. A name in `enum desk_sound` in `include/sound.h`, before
   `DESK_SND_COUNT`.
2. A function `s_<name>(struct buf *b, struct rng *r, int v)` in `sfx.c`
   built from the pieces at the top of the file (modes, bursts, friction,
   crackle, swish, creak), and its entry in `SFX`: name, length in seconds,
   number of variants.
3. Play it: `desk_sound_play(DESK_SND_YOURS, pan, gain, 0.0)`, with `pan`
   from -1 (left) to 1 (right), usually where the thing is on the screen.

`DESK_SOUND=file:/tmp/desk-%p.wav` writes the mix to a file instead of the
speakers, which is how sounds are checked without disturbing anyone.

## 7. The room

The room is traced twice over: by the C in `src/draw.c` (the reference) and
by `src/room.glsl` on the GPU. **Change both, the same way.**

- The scene is `scene_map(vec3 p, int kind)` in `draw.c` and `scene_map` in
  `room.glsl`; materials and shading follow it. Every named constant
  (`RM_*`) reaches the shader through `room_glsl_preamble`, so add new
  numbers as named constants in `draw.c` rather than writing them twice.
- The build hashes `room.glsl` and `gpu.c` into `ROOM_KEY`, so cached rooms
  made by old code are not used. A change to `draw.c` alone does not change
  the key: delete `~/.cache/desk/room-*` (or set `XDG_CACHE_HOME` to an
  empty directory) to see it.
- Check that the two agree: `./build/desk-gputest --room`. It traces every
  sheet both ways and reports coverage, owner and colour differences.
- Check the cost on the CPU: `DESK_NO_GPU=1` traces on the processor, and
  the log's `desk: room:` lines say how long each sheet took. The 2D
  Fallback level (`graphics = 2d`) must stay cheap.
- Hit testing (what is under the pointer on the bookcase, the Control
  Panel, the post box) uses the owner ids the trace records, so a new
  clickable object needs an id (`room_owner`, the `*_owner` functions).

## 8. Things on the desk

Live parts over the room (the clock's display, the Control Panel's lamps,
letters, slips) are sprites (`struct desk_sprite`, `sprite_paint` in
`wm.c`) drawn by functions in `draw.c` that take a cairo context and a
screen size. Their positions come from the room's camera (`room_project`)
so they stay attached to the traced objects at every screen shape. To add
one:

1. A drawing function in `draw.c` and its declaration in `include/desk.h`.
2. Its box (where on the screen it is) from the room, the way
   `desk_overlay_box` does for the existing overlays.
3. A sprite in `struct desk_server`, drawn when what it shows changes (key
   it by a hash of that), and placed on the right layer (ARCHITECTURE.md
   §4.1).
4. If it is clickable, a hit test in `draw.c` and a branch in
   `server_cursor_button` in the right place in the order (ARCHITECTURE.md
   §4.4).
5. Draw it in `desk-preview` too, so it can be checked without a compositor.

## 9. Icons and theme

- The DEsk icon theme is generated: `python3 themes/make-icons.py` writes
  `themes/icons/DEsk`. Shapes and light are gradients; texture is SVG
  filters, and each icon must still read without filters (Qt ignores them).
  `themes/contact-sheet.py` makes a sheet of all of them to look at.
- The GTK theme is `themes/gtk/DEsk/gtk-3.0/gtk.css`, on top of Adwaita.
- Both are installed into DEsk's own dconf layer, so they never change
  another desktop's look.

## 10. Before you share a change

```sh
meson test -C build
# the build configurations DEsk is checked in (each in its own directory)
meson setup b-min -Dpost=disabled -Dsound=disabled -Dxwayland=disabled
meson setup b-werror -Dbuildtype=debug -Dwerror=true
meson setup b-asan -Dbuildtype=debug -Db_sanitize=address,undefined
meson setup b-tsan -Dbuildtype=debug -Db_sanitize=thread
```

Build each (`meson compile -C b-...`), run the sanitizer builds nested
through what you changed, look at your change in the preview at Ultra and
2D and in a long language and a CJK one, and check the log for
`[ERROR]` lines. The development log ([design.md](design.md), in Japanese)
shows how earlier changes were checked.
