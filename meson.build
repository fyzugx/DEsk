project('desk', 'c',
  version: '0.1.3',
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
add_project_arguments('-DDESK_RELEASE="DEsk v0.1.3 (BETA)"',
  '-DDESK_VERSION="@0@"'.format(meson.project_version()), language: 'c')
# a variable initialised from itself (x = x) is uninitialised, and in C
# -Wall does not say so: a keysym once read as garbage that way
add_project_arguments(meson.get_compiler('c').get_supported_arguments('-Winit-self'),
  language: 'c')

math = meson.get_compiler('c').find_library('m', required: true)
threads = dependency('threads')
cairo = dependency('cairo')
# the faces text is set in are chosen from what is installed (src/fonts.c)
fontconfig = dependency('fontconfig')
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
  'src/memo.c', 'src/user.c']
draw_deps = [cairo, fontconfig, math, threads]
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

# The desk's sounds: made in sfx.c, played by sound.c to the sound server
# through libpulse (PulseAudio, or PipeWire answering in its language) when
# it is there to build against, else to ALSA loaded at run time, else
# nowhere. Nothing about sound is required to build or to run.
libpulse = dependency('libpulse', required: get_option('sound'))
dl = meson.get_compiler('c').find_library('dl', required: false)
sound_deps = [math, threads, dl]
sound_args = []
if libpulse.found()
  sound_deps += libpulse
  sound_args += '-DDESK_HAVE_PULSE=1'
endif
sound_lib = static_library('desk-sound',
  ['src/sfx.c', 'src/sound.c'],
  c_args: sound_args,
  include_directories: include_directories('include'),
  dependencies: sound_deps,
)
summary({'Sound to the sound server (libpulse)': libpulse.found()})

draw_lib = static_library('desk-draw',
  draw_sources,
  include_directories: include_directories('include'),
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

wm_sources = proto_headers + ['src/wm.c', 'src/power.c', 'src/session.c', 'src/lock.c',
  'src/camera.c', 'src/occasion.c', 'src/devices.c']
wm_deps = [wlroots, wayland_server, xkbcommon, cairo, fontconfig, pixman, libdrm, math, threads]
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
  # one), and what is playing (MPRIS), for the clock's MEDIA page
  wm_sources += ['src/tray.c', 'src/ime.c', 'src/media.c']
  wm_deps += sdbus
endif

executable('desk',
  wm_sources,
  c_args: wm_args,
  include_directories: include_directories('include'),
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
install_data('scripts/desk-session-helper', 'scripts/desk-screenshot', 'scripts/desk-ctl',
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
test('words', import('python').find_installation(),
  args: [files('scripts/check-words.py'), meson.project_source_root()],
)
