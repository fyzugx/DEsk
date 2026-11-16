project('desk', 'c',
  version: '0.2.0',
  default_options: [
    'buildtype=debugoptimized',
    'c_std=c11',
    'warning_level=2',
    'werror=false',
  ],
)

add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  '-D_POSIX_C_SOURCE=200809L',
  language: 'c',
)
# what this release is called, as --version and the session log say it,
# and its number, as the notification server tells programs that ask
add_project_arguments('-DDESK_RELEASE="DEsk v0.2 (BETA)"',
  '-DDESK_VERSION="@0@"'.format(meson.project_version()), language: 'c')
# a variable initialised from itself (x = x) is uninitialised, and in C
# -Wall does not say so: a keysym once read as garbage that way
add_project_arguments(meson.get_compiler('c').get_supported_arguments('-Winit-self'),
  language: 'c')

math = meson.get_compiler('c').find_library('m', required: true)
threads = dependency('threads')
cairo = dependency('cairo')
# a letter's text, emoji and every script in it (src/draw.c, note_layout)
pangocairo = dependency('pangocairo')
# the faces text is set in are chosen from what is installed (src/fonts.c)
fontconfig = dependency('fontconfig')
# and read for handwriting, on a thread of their own, by a FreeType of its own (src/handwriting.c)
freetype = dependency('freetype2')
pixman = dependency('pixman-1')
egl = dependency('egl', required: false)
glesv2 = dependency('glesv2', required: false)
gbm_dep = dependency('gbm', required: false)
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
libdrm = dependency('libdrm')
wayland_protos = dependency('wayland-protocols')
# The post box is the machine's notification daemon, so it needs a bus.
# -Dpost=disabled leaves it out even where libsystemd is installed.
sdbus = dependency('libsystemd', required: get_option('post'))
if sdbus.found()
  add_project_arguments('-DDESK_HAVE_POST=1', language: 'c')
endif
wayland_scanner = find_program('wayland-scanner')
wlroots = dependency('wlroots-0.18', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots', version: '>=0.18.0')
endif

wp = wayland_protos.get_variable('pkgdatadir')

proto_headers = []
foreach spec : [
  ['xdg-shell-protocol.h', wp / 'stable/xdg-shell/xdg-shell.xml'],
  ['xdg-decoration-unstable-v1-protocol.h', wp / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
  # wlroots' pointer-constraint header includes a scanner-generated enum header.
  ['pointer-constraints-unstable-v1-protocol.h',
    wp / 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
  ['relative-pointer-unstable-v1-protocol.h',
    wp / 'unstable/relative-pointer/relative-pointer-unstable-v1.xml'],
  # and its tablet header, one for what it says to a pen
  ['tablet-v2-protocol.h', wp / 'stable/tablet/tablet-v2.xml'],
  # the screens for display tools (wlr-randr, kanshi, wdisplays) and their
  # power (wlopm): wlr-protocols, which wayland-protocols does not carry,
  # kept in protocols/ -- wlroots' own header for the power one includes it
  ['wlr-output-management-unstable-v1-protocol.h', 'protocols/wlr-output-management-unstable-v1.xml'],
  ['wlr-output-power-management-unstable-v1-protocol.h',
    'protocols/wlr-output-power-management-unstable-v1.xml'],
]
  proto_headers += custom_target(
    spec[0],
    input: spec[1],
    output: spec[0],
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

draw_sources = ['src/draw.c', 'src/apps.c', 'src/panel.c', 'src/agenda.c', 'src/fonts.c', 'src/i18n.c',
  'src/memo.c', 'src/user.c', 'src/calc.c', 'src/calcart.c', 'src/keyboardart.c', 'src/notebook.c', 'src/nbink.c', 'src/phone.c',
  'src/looks.c', 'src/lookfile.c', 'src/clipart.c']
draw_deps = [cairo, pangocairo, fontconfig, math, threads]
# The room is a distance field marched once per pixel, which is what
# graphics hardware is for. Without EGL we simply do not build that path
# and the C tracer -- which stays the reference either way -- is all there
# is.
have_gpu = egl.found() and glesv2.found() and gbm_dep.found()
# What the room looks like, as a number: a room traced once is kept on disk
# until that changes (scripts/room-key.py), not thrown away with each build.
room_key_h = custom_target('room_key.h',
  input: ['src/room.glsl', 'src/gpu.c'],
  output: 'room_key.h',
  command: [import('python').find_installation(), files('scripts/room-key.py'),
    '@OUTPUT@', '@INPUT0@', '@INPUT1@'],
)
draw_sources += [room_key_h]
# The camera Print puts up to the eye, rendered in Blender
# (art/camera/camera.py) into one picture, and as it lies in the drawer,
# seen from above; and the things on the desk, rendered in Blender
# (art/case.py, and the script for each): pictures of each through the
# room's camera for the shapes of screen and widths of desk, one PNG after
# another. Where the pieces are in them, and where each goes in the room's
# frame, is built in (art/camera/camera-map.h, camera-top.h,
# art/clock/clock-case.h, art/post/post-case.h, art/console/console-case.h);
# the pictures are DEsk's data, read when they are wanted (draw.c
# desk_art_find): built in, the four programs that link the drawing each
# carried all 17 MB of them. Blender is not needed to build; only to make
# them again.
install_data('art/camera/camera.png', 'art/camera/camera-top.png',
  install_dir: get_option('datadir') / 'desk' / 'camera')
# The calculator, rendered in Blender (art/calc/calc.py): open, shut, its keys
# held down, the lid on its way, and as it lies in the drawer. Where it all is
# is built in (art/calc/calc-map.h).
calc_art = ['art/calc/calc-open.png', 'art/calc/calc-shut.png', 'art/calc/calc-keys.png',
  'art/calc/calc-top.png']
foreach i : range(15)
  calc_art += 'art/calc/calc-lid-@0@@1@.png'.format(i < 10 ? '0' : '', i)
endforeach
install_data(calc_art, install_dir: get_option('datadir') / 'desk' / 'calc')
# The keyboard, a typewriter rendered in Blender (art/keyboard/keyboard.py): its case, the paper
# bail, the top of the case, and a key of each kind, up and down, that DEsk lays out for the keymap
# (src/keyboardart.c).
install_data('art/keyboard/kb-case.png', 'art/keyboard/kb-bail.png',
  'art/keyboard/kb-top.png',
  'art/keyboard/kb-letter-up.png', 'art/keyboard/kb-letter-down.png',
  'art/keyboard/kb-function-up.png', 'art/keyboard/kb-function-down.png',
  'art/keyboard/kb-return-up.png', 'art/keyboard/kb-return-down.png',
  'art/keyboard/kb-slim-up.png', 'art/keyboard/kb-slim-down.png',
  'art/keyboard/kb-space-up.png', 'art/keyboard/kb-space-down.png',
  install_dir: get_option('datadir') / 'desk' / 'keyboard')
# The notebook desk-notepad's window is the top sheet of, rendered in Blender
# (art/notebook/notebook.py): the page, a turn of its wire, its paper's tooth,
# the notebook shut, the note for finding things; and the pencil case under it
# (art/notebook/pencilcase.py: the case, its pen turned through a whole turn and
# at rest on each refill, the highlighter, the rubber, and what each throws on
# the case; where they go is built in, art/notebook/case-map.h).
install_data('art/notebook/nb-page.png', 'art/notebook/nb-rings.png', 'art/notebook/nb-grain.png',
  'art/notebook/nb-shut.png', 'art/notebook/nb-note.png', 'art/notebook/nb-top.png',
  'art/notebook/nb-case.png', 'art/notebook/nb-pen-roll.png', 'art/notebook/nb-pen-rest.png',
  'art/notebook/nb-pen-shadow.png', 'art/notebook/nb-marker.png', 'art/notebook/nb-marker-shadow.png',
  'art/notebook/nb-rubber.png', 'art/notebook/nb-rubber-shadow.png',
  install_dir: get_option('datadir') / 'desk' / 'notebook')
# What a program kept in the drawer can be given to look like (docs/LOOKS.md): the desk's own
# looks, each a folder, rendered in Blender for nothing but that (art/looks/looks.py), and what
# renders anyone's the same way (art/looks/bake.py, with art/kit.py; desk-look bake runs it).
foreach look : ['radio', 'paintbox', 'gamepad', 'headphones']
  install_data('art/looks' / look / 'look.png', 'art/looks' / look / 'look.ini',
    install_dir: get_option('datadir') / 'desk' / 'looks' / look)
endforeach
install_data('art/looks/bake.py', 'art/kit.py', install_dir: get_option('datadir') / 'desk' / 'looks')
# The slider phone round a picture-in-picture window, rendered in Blender
# (art/phone/phone.py).
install_data('art/phone/ph-slab.png', 'art/phone/ph-ear.png', 'art/phone/ph-deck.png', 'art/phone/ph-keys.png',
  install_dir: get_option('datadir') / 'desk' / 'phone')
foreach thing : ['clock', 'post', 'console']
  install_data('art' / thing / thing + '-case.bin',
    install_dir: get_option('datadir') / 'desk' / thing)
endforeach
# The views' faces, pictured once for every shape of room
# (desk-gputest --faces-bake): what the desk's own view and its drawer, the
# bookcase's, the console's and the post box's views and the moves to them
# are laid from on a machine that would trace them on its processor -- the
# console's and the post box's for three widths of desk. With DEsk's data,
# read as the room is made (draw.c rf_pack_load), not built in: the four
# programs that link the drawing each carried all 30 MB of them.
room_faces = ['art/room/plan-faces.bin', 'art/room/shelf-faces.bin']
foreach thing : ['console', 'post']
  foreach w : ['16', '21', '28']
    room_faces += 'art/room/' + thing + '-faces-' + w + '.bin'
  endforeach
endforeach
install_data(room_faces, install_dir: get_option('datadir') / 'desk' / 'room')
draw_includes = include_directories('include', 'art/keyboard', 'art/calc', 'art/camera', 'art/clock', 'art/post', 'art/phone',
  'art/console', 'art/notebook')
if have_gpu
  # The room's shader, as a string for draw.c to compile at run time.
  room_glsl_h = custom_target('room_glsl.h',
    input: 'src/room.glsl',
    output: 'room_glsl.h',
    command: [import('python').find_installation(), files('scripts/glsl2c.py'),
      'ROOM_GLSL', '@INPUT@', '@OUTPUT@'],
  )
  draw_sources += ['src/gpu.c', room_glsl_h]
  draw_deps += [egl, glesv2, gbm_dep]
  add_project_arguments('-DDESK_HAVE_GPU=1', language: 'c')
endif

# The desk's sounds: made in sfx.c, or made in Faust and baked
# (art/sound/bake.py, read as the sounds are made: sound.c baked_take),
# played by sound.c to the sound server
# through libpulse (PulseAudio, or PipeWire answering in its language) when
# it is there to build against, else to ALSA loaded at run time, else
# nowhere. Nothing about sound is required to build or to run.
libpulse = dependency('libpulse', required: get_option('sound'))
dl = meson.get_compiler('c').find_library('dl', required: false)
sound_deps = [math, threads, dl]
sound_args = ['-DDESK_DATADIR="' + (get_option('prefix') / get_option('datadir')) + '"']
if libpulse.found()
  sound_deps += libpulse
  sound_args += '-DDESK_HAVE_PULSE=1'
endif
# (the baked takes, not what bakes them)
install_subdir('art/sound', install_dir: get_option('datadir') / 'desk',
  exclude_files: ['bake.py', 'render.c'], exclude_directories: ['dsp'])
sound_lib = static_library('desk-sound',
  ['src/sfx.c', 'src/sound.c'],
  c_args: sound_args,
  include_directories: include_directories('include'),
  dependencies: sound_deps,
)
summary({'Sound to the sound server (libpulse)': libpulse.found()})

draw_lib = static_library('desk-draw',
  draw_sources,
  c_args: ['-DDESK_DATADIR="' + (get_option('prefix') / get_option('datadir')) + '"'],
  include_directories: draw_includes,
  dependencies: draw_deps,
)

if have_gpu
  executable('desk-gputest',
    'src/gputest.c',
    include_directories: include_directories('include'),
    link_with: draw_lib,
    dependencies: draw_deps,
  )
endif

executable('desk-preview',
  'src/preview.c',
  include_directories: include_directories('include'),
  link_with: [draw_lib, sound_lib],
  dependencies: [cairo, fontconfig, math, threads] + sound_deps,
  install: true,
)

wm_sources = proto_headers + ['src/wm.c', 'src/touch.c', 'src/things.c', 'src/thing-phone.c', 'src/thing-notebook.c', 'src/thing-calc.c', 'src/thing-keyboard.c', 'src/thing-mat.c', 'src/thing-clipboard.c', 'src/clips.c', 'src/handwriting.c', 'src/power.c', 'src/session.c', 'src/lock.c', 'src/orient.c',
  'src/camera.c', 'src/occasion.c', 'src/cards.c', 'src/rec.c']
wm_deps = [wlroots, wayland_server, xkbcommon, cairo, fontconfig, freetype, pixman, libdrm, math, threads]
# the screen lock checks the password as login does
pam = dependency('pam', required: false)
if not pam.found()
  pam = meson.get_compiler('c').find_library('pam')
endif
wm_deps += pam
# DEsk's own autostart entries live with its data, wherever the prefix
# put that, and are looked for there after the XDG directories
wm_args = ['-DDESK_DATADIR="' + (get_option('prefix') / get_option('datadir')) + '"']

# X11 programs, through Xwayland. wlroots has to have been built with it --
# a distribution's usually is, one built for a kiosk may not be -- and the
# window manager's half of it speaks xcb. Without either, DEsk still
# builds and runs; it simply opens Wayland programs only.
have_xwayland = false
xwayland_opt = get_option('xwayland')
if not xwayland_opt.disabled()
  xcb = dependency('xcb', required: xwayland_opt)
  xcb_icccm = dependency('xcb-icccm', required: xwayland_opt)
  # not called, but wlroots' Xwayland header includes it
  xcb_ewmh = dependency('xcb-ewmh', required: xwayland_opt)
  wlr_xwayland = wlroots.get_variable('have_xwayland', default_value: 'false') == 'true'
  if xwayland_opt.enabled() and not wlr_xwayland
    error('xwayland: this wlroots was built without Xwayland')
  endif
  have_xwayland = wlr_xwayland and xcb.found() and xcb_icccm.found() and xcb_ewmh.found()
  if have_xwayland
    wm_deps += [xcb, xcb_icccm]
    wm_args += '-DDESK_HAVE_XWAYLAND=1'
  endif
endif
# Touchpads and mice set up as Shell Settings says (tap to click, natural
# scrolling, speed, left-handed): through libinput, which wlroots opens the
# devices with. Under Wayland nothing but the compositor can set these, so
# without it they simply keep libinput's defaults.
libinput = dependency('libinput', required: false)
have_libinput = libinput.found() \
  and wlroots.get_variable('have_libinput_backend', default_value: 'false') == 'true'
if have_libinput
  wm_deps += libinput
  wm_args += '-DDESK_HAVE_LIBINPUT=1'
endif
summary({'Xwayland (X11 programs)': have_xwayland, 'GPU room tracer': have_gpu,
  'Post box and tray (session bus)': sdbus.found(),
  'Touchpad and mouse settings (libinput)': have_libinput})

if sdbus.found()
  # With a bus, DEsk IS the notification daemon and the post box fills
  # with what the machine actually has to say. Without one it still runs;
  # the box is simply always empty.
  wm_sources += 'src/post.c'
  # and the tray: the watcher programs put their tray icons with, what
  # input method is on (IBus keeps a bus of its own; fcitx5 is on this
  # one), and what is playing (MPRIS), for the clock's MEDIA page; and
  # programs asking the screen saver's name for the screen to stay lit
  wm_sources += ['src/tray.c', 'src/ime.c', 'src/media.c', 'src/saver.c']
  wm_deps += sdbus
endif

executable('desk',
  wm_sources,
  c_args: wm_args,
  include_directories: include_directories('include', 'art/phone', 'art/keyboard', 'art/notebook'),
  link_with: [draw_lib, sound_lib],
  dependencies: wm_deps + sound_deps,
  install: true,
)

# Shell Settings: a program of its own, on a sheet of the shell's paper.
# It needs only libwayland's client half; without it, there is no Shell
# Settings and shell.conf is edited by hand.
wayland_client = dependency('wayland-client', required: false)
wayland_cursor = dependency('wayland-cursor', required: false)
if wayland_client.found()
  client_protos = []
  foreach spec : [
    ['xdg-shell', wp / 'stable/xdg-shell/xdg-shell.xml'],
    ['xdg-decoration-unstable-v1', wp / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'],
  ]
    client_protos += custom_target(spec[0] + '-client-protocol.h',
      input: spec[1],
      output: spec[0] + '-client-protocol.h',
      command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    client_protos += custom_target(spec[0] + '-protocol.c',
      input: spec[1],
      output: spec[0] + '-protocol.c',
      command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
  endforeach
  settings_deps = draw_deps + [wayland_client]
  settings_args = []
  if wayland_cursor.found()
    settings_deps += wayland_cursor
    settings_args += '-DDESK_HAVE_WL_CURSOR=1'
  endif
  executable('desk-settings',
    ['src/settings.c'] + client_protos,
    c_args: settings_args,
    include_directories: include_directories('include'),
    link_with: [draw_lib, sound_lib],
    dependencies: settings_deps + sound_deps,
    install: true,
  )
  configure_file(
    input: 'session/desk-settings.desktop.in',
    output: 'desk-settings.desktop',
    configuration: { 'EXEC': get_option('prefix') / get_option('bindir') / 'desk-settings',
      'ICON': 'desk-settings' },
    install_dir: get_option('datadir') / 'applications',
  )
  install_data('session/desk-settings.png',
    install_dir: get_option('datadir') / 'icons/hicolor/128x128/apps',
  )
  # the DEsk Experience Index: how this machine does at what the desk asks
  # of it (src/index.c)
  executable('desk-index',
    ['src/index.c'] + client_protos,
    c_args: settings_args,
    include_directories: include_directories('include'),
    link_with: [draw_lib, sound_lib],
    dependencies: settings_deps + sound_deps,
    install: true,
  )
  configure_file(
    input: 'session/desk-index.desktop.in',
    output: 'desk-index.desktop',
    configuration: { 'EXEC': get_option('prefix') / get_option('bindir') / 'desk-index',
      'ICON': 'desk-settings' },
    install_dir: get_option('datadir') / 'applications',
  )
  subdir('applications')
endif
summary({'Shell Settings (wayland-client)': wayland_client.found()})

# The calculator on the shelves as well as in the drawer (2026-11-12): DEsk takes it out of the
# drawer when its line is taken, and desk-ctl does from anywhere else
configure_file(
  input: 'session/desk-calculator.desktop.in',
  output: 'desk-calculator.desktop',
  configuration: { 'EXEC': get_option('prefix') / get_option('bindir') / 'desk-ctl',
    'ICON': 'desk-calculator' },
  install_dir: get_option('datadir') / 'applications',
)
install_data('session/desk-calculator.png',
  install_dir: get_option('datadir') / 'icons/hicolor/128x128/apps',
)

install_data('session/desk-session',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)
install_data('session/desk.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions',
)
install_data('session/desk-portals.conf',
  install_dir: get_option('datadir') / 'xdg-desktop-portal',
)
# xdg-desktop-portal-wlr reads its settings for a desktop only from
# /etc/xdg (or the user's own), by the desktop's name
# (with where its chooser of a screen to share is)
xdpw_conf = configure_file(
  input: 'session/desk-xdpw.conf',
  output: 'desk-xdpw.conf',
  configuration: {
    'CHOOSER': get_option('prefix') / get_option('bindir') / 'desk-screen-chooser',
  },
)
install_data(xdpw_conf,
  install_dir: '/etc/xdg/xdg-desktop-portal-wlr',
  rename: 'DEsk',
)
# DEsk's standard programs: the default program for each kind of file in
# its session, and what the drawer's things open (apps.c)
install_data('session/desk-mimeapps.list',
  install_dir: get_option('datadir') / 'applications',
)

# The look of other programs in a DEsk session: its icons (themes/icons,
# drawn by themes/make-icons.py) and its GTK theme (themes/gtk), given to
# the session's own layer of settings by desk-session (desk-dconf-profile)
install_subdir('themes/icons/DEsk', install_dir: get_option('datadir') / 'icons')
install_subdir('themes/gtk/DEsk', install_dir: get_option('datadir') / 'themes')
install_data('session/desk-dconf-profile',
  install_dir: get_option('datadir') / 'desk',
)
gtk_update_icon_cache = find_program('gtk-update-icon-cache', required: false)
if gtk_update_icon_cache.found()
  meson.add_install_script(gtk_update_icon_cache, '-q', '-t', '-f',
    get_option('prefix') / get_option('datadir') / 'icons' / 'DEsk',
    skip_if_destdir: true)
endif

# the desktop session: DEsk's autostart entries, and the helper they run
install_data(
  'session/autostart/desk-polkit.desktop',
  'session/autostart/desk-keyring.desktop',
  'session/autostart/desk-ime.desktop',
  'session/autostart/desk-network.desktop',
  'session/autostart/desk-media.desktop',
  'session/autostart/desk-power.desktop',
  install_dir: get_option('datadir') / 'desk' / 'autostart',
)
# the screen lock's PAM service (it includes "login"); PAM reads only /etc
install_data('session/pam/desk', install_dir: '/etc/pam.d')
install_data('scripts/desk-session-helper', 'scripts/desk-screenshot', 'scripts/desk-record',
  'scripts/desk-ctl',
  'scripts/desk-report', 'scripts/desk-screen-chooser',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

# DEsk nested in the session you are already in, and something to double
# click to get it.
install_data('scripts/desk-window',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)
install_data('session/desk-window.desktop',
  install_dir: get_option('datadir') / 'applications',
)
install_data('session/desk.png',
  install_dir: get_option('datadir') / 'icons/hicolor/512x512/apps',
)

# meson test: the word tables (src/i18n.c, src/index.c) have a column for
# each language, and every translation takes the printf conversions its
# English takes, in the same order (scripts/check-words.py)
# DEsk's notebook, to type in and to write in with a pen, and its text editor,
# if GTK 3 is there to build it with: it is what lets it have input methods,
# right-to-left text and the clipboard without writing them again
# (src/notepad.c). Without it the drawer's notebook opens another editor, as it
# does for anyone who chooses one.
gtk3 = dependency('gtk+-3.0', required: false)
summary({'Text editor (GTK 3)': gtk3.found()})
if gtk3.found()
  executable('desk-notepad',
    ['src/notepad.c', 'src/i18n.c', 'src/fonts.c', 'src/nbink.c'],
    include_directories: include_directories('include'),
    dependencies: [gtk3, fontconfig, cairo, math],
    c_args: ['-DDESK_DATADIR="' + (get_option('prefix') / get_option('datadir')) + '"'],
    install: true,
  )
  configure_file(
    input: 'session/desk-notepad.desktop.in',
    output: 'desk-notepad.desktop',
    configuration: { 'EXEC': get_option('prefix') / get_option('bindir') / 'desk-notepad',
      'ICON': 'desk-notepad' },
    install_dir: get_option('datadir') / 'applications',
  )
  install_data('session/desk-notepad.png',
    install_dir: get_option('datadir') / 'icons/hicolor/128x128/apps',
  )
  # Looks for the drawer, made: from a picture chosen from anywhere (what the drawer's card of
  # looks runs), from a picture named, from a model in Blender; and one checked (src/look.c,
  # docs/LOOKS.md)
  executable('desk-look',
    ['src/look.c', 'src/lookfile.c', 'src/i18n.c', 'src/fonts.c'],
    include_directories: include_directories('include'),
    dependencies: [gtk3, fontconfig, cairo],
    c_args: ['-DDESK_DATADIR="' + (get_option('prefix') / get_option('datadir')) + '"'],
    install: true,
  )
  # Pictures for the clear mat: chosen from anywhere (what the mat's envelope and Shell Settings
  # run), made prints of and laid under it, or one laid all over it (src/mat.c)
  executable('desk-mat',
    ['src/mat.c', 'src/i18n.c', 'src/fonts.c'],
    include_directories: include_directories('include'),
    dependencies: [gtk3, fontconfig, cairo, math],
    c_args: ['-DDESK_DATADIR="' + (get_option('prefix') / get_option('datadir')) + '"'],
    install: true,
  )
endif

test('words', import('python').find_installation(),
  args: [files('scripts/check-words.py'), meson.project_source_root()],
)
# and fingers are taken to have done what they did (tests/touch-test.c)
test('touch', executable('touch-test', ['tests/touch-test.c', 'src/touch.c'],
  include_directories: include_directories('include'),
  dependencies: math,
  install: false,
))
# and what is written with a pen is read as what it is (tests/handwriting-test.c)
test('handwriting', executable('handwriting-test', ['tests/handwriting-test.c', 'src/handwriting.c'],
  include_directories: include_directories('include'),
  dependencies: [cairo, fontconfig, freetype, math, threads],
  install: false,
), timeout: 120)
# and the calculator works out what it should (tests/calc-test.c)
test('calc', executable('calc-test', ['tests/calc-test.c', 'src/calc.c'],
  include_directories: include_directories('include'),
  dependencies: math,
  install: false,
))
