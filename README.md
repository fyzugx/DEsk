**English** | [日本語](README.ja.md)

# DEsk

**v0.1.3 (BETA)** -- v0.1, the first public release, with fixes, an installer for Fedora, and games on the whole screen. See [RELEASE-NOTES.md](RELEASE-NOTES.md).

I've created a Discord server!
You can also report bugs and submit ideas for new features here!
link : https://discord.gg/xR5f7Xs49

> **DEsk** is **D**esktop **E**nvironment and **desk** in one word -- a
> name that came from the Reddit community. It used to be called DeskWM:
> the commands are now `desk`, `desk-session`, `desk-settings` and so on, and
> what was set up for DeskWM (its settings, its data, `DESKWM_*` variables)
> carries over by itself.

A Wayland desktop for Linux in realistic skeuomorphism -- the kind iOS 6
had, where everything on the screen is drawn to look like the real thing.
Here the desktop is a desk.

Windows are sheets of paper on a walnut desk. The dock is a drawer, your
apps are filed in binders on a bookshelf behind the desk, notifications
arrive in a post box, and the system controls are a panel built into the
back of the desk. The room is ray-traced, and one desk lamp lights all of it.

![The desk: two programs on paper, the clock, the Control Panel at the back left and the post box at the back right](docs/screenshots/desk.jpg)

> **Status: beta.** DEsk is used day to day on its author's machines and has
> been checked carefully (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
> section 9), but this is its first release: expect rough edges, and expect
> things to change. Your settings and data will be carried forward.

## Install

Download the latest release from the
[Releases page](https://github.com/fyzugx/DEsk/releases) and unpack it (or
clone this repository). Then, in its folder, run the line for your system:

**Debian 13**

```sh
sudo sh scripts/install_debian.sh
```

**Ubuntu 24.04 / 26.04** (and Linux Mint, Pop!_OS)

```sh
sudo sh scripts/install_ubuntu.sh
```

**Arch Linux** (and EndeavourOS, Manjaro)

```sh
sudo sh scripts/install_arch.sh
```

**Fedora 43 / 44** (and Nobara)

```sh
sudo sh scripts/install_fedora.sh
```

Then log out and choose **DEsk** on the login screen.

- It shows what it will install and asks first. `--yes` takes the suggested
  answers; `--no-apps` installs DEsk without the recommended programs.
- The first login draws the room once: a few seconds on a GPU, longer
  without one. After that it starts at once.
- To update, get the new release and run the same line again. Your settings
  are kept.
- Something went wrong? See [Reporting a problem](#reporting-a-problem).

### Uninstall

Log out of DEsk, and from another desktop (or a text console), in the
folder you installed it from:

```sh
sudo sh scripts/uninstall.sh
```

It shows what it will remove and asks first: DEsk's programs and files,
"DEsk" on the login screen, its settings for the whole machine
(`/etc/xdg/desk`), and the wlroots the installer built for it
(`/opt/desk-wlroots`, on Ubuntu, Arch and some Fedoras; `--keep-wlroots`
leaves it). The packages the installer brought from your distribution stay,
since other programs may use them, and so does everything of yours: to
remove that too, `rm -rf ~/.config/desk ~/.local/share/desk
~/.local/state/desk ~/.cache/desk`.

More options, building by hand and running DEsk in a window are under
[Build](#build) and [Run](#run).

## What's on the desk

Nothing here is a symbol for a thing on a desk. As in the realistic
skeuomorphism of iOS 6, each is drawn to look like the real thing set down
there -- its material, the light on it, its shadow and the wear of use.

- **Windows are paper.** Each window sits on a cream sheet with real fibre,
  thickness and a dog-eared corner. Hold the pointer on the corner and it
  lifts to show what is printed on the back: **Fold** (minimize: folded
  like a letter and left on the desk, under the windows as paper put down
  is), **Unfold / Refold** (maximize and restore), and **Discard** (close:
  crumpled into a ball and tossed).
  Dragging an edge pulls more paper out from behind the sheet.
- **The drawer is the dock.** Pull the brass handle (or press `Super+D`).
  It holds the things you put in it: any app, or a folded window. Out of
  the box it holds DEsk's standard programs (below) and a screwdriver that
  opens Shell Settings.
- **The bookshelf holds your apps** (`Super+A`, or the ALL APPS plate).
  Each category is a ring binder whose sheets fan from a single ring.
  Take a sheet and it is carried down to the desk, where the app's window
  opens on it. At the end of the row stands an **ALL APPS index book**:
  every program from A to Z, with thumb tabs and a line at the top of the
  page to type into (input methods via IBus work).
- **The post box is the notification centre** (`Super+N`). Letters show the
  sender and subject, and a letter taken out lies on the desk, where you
  can carry it anywhere, or click it to put it away.
- **The Control Panel** (`Super+C`) is a valve-era machine built into the
  back of the desk: real meters, a volume knob, network and Bluetooth
  levers, a fader for the room's lamp and one for the screen's own
  backlight (on a laptop, through logind; the brightness keys work too),
  a key to unlock the power buttons, and the system tray as paper slips
  under it (more than fit, and the wheel slides the row along). A brass
  **CHOOSE** button beside the wireless lever, the Bluetooth lever and the
  knob folds up a walnut lid from the top of the set with a radio tuning
  dial in it: the networks in range, the Bluetooth devices, or the speakers
  and microphones. Turn its knob (wheel, drag or arrow keys) and press it
  (or Return) to join, connect or switch; its other readout always shows
  what is in use now. NetworkManager, BlueZ and PipeWire do the work.
- **A desk calendar, a desk memo pad and a clock** stand on the shelf. The
  calendar has a notebook of plans and to-dos, and notes are peeled off and
  stuck onto the day. The memo pad is taken down the same way and written
  on (IBus works there too); its sheets turn over the wire. The clock
  shows the seconds small beside the minutes, under two lamps that light
  only while a camera or a microphone is in use.
- **The front door is out in the real world.** The lock screen's door has
  the hour's light -- the sun worked out from your time zone's city, the
  porch lamp coming on at dusk -- and, if you let it ask open-meteo.com for
  that city's weather (only its latitude and longitude are sent), the
  weather: rain darkening the brick and streaking past the lamp, snow
  settling on what faces up, fog, lightning. And on certain days of the
  year the door and the clock have something more for you -- there is no
  setting for it; what it is, you will see on the day (give Shell Settings
  your birthday, and that is one of them). Esc stops it.
- **Light.** The room (desk, shelf, console, post box) is ray-traced on the
  GPU (GLES 3), with a CPU tracer as the fallback, and cached per screen
  size. Everything drawn over it is lit by the same lamp. At login the room
  starts dark and the lamp comes up as the room is made, while the clock
  counts how far in glowing red.
- **Sizes are in points.** A bigger screen is a bigger desk, not bigger
  things.
- **More than one screen.** The desk is on one screen; every other shows
  more of the desk's boards, running on from it, for windows to lie on. A
  new window comes onto the screen the pointer is on, Unfold and full
  screen fill the screen the window is on, and nothing the desk's own
  screen does -- the drawer, turning to the bookshelf -- moves what lies on
  the others or takes it out of reach. A screen taken away puts its
  windows on one that is left.
- **X11 apps** run through Xwayland, and windows that draw their own frames
  sit on paper too.
- **The screen lock is the front door** (`Super+L`, or logind): you walk
  up to it at night, close enough to touch, traced in 3D with its own
  lamps and soft shadows. Your picture and name are on the board hung on
  it, the password goes in at the brass plate, the key beside it; on
  Return (or a click on the key) you lean in and turn the key. Let in, it
  goes round, the door swings open, you walk through into the room and sit
  down at the desk; told no, it will not turn. Locking is the same run
  backwards -- out of the room, the door shut, the key turned -- from
  `Super+L` or the blue LOCK button on the Control Panel. Checked with
  PAM, as at login.
- **Seven languages.** Everything DEsk prints itself, and the names of
  programs, are in English, Japanese, Chinese (simplified), Korean,
  Spanish, French or German: the locale's, or `language = en | ja | zh |
  ko | es | fr | de` in `~/.config/desk/shell.conf` (or the Shell Settings
  app), and switch live. The calendar is printed as one sold there is,
  and Chinese and Korean are set in their own cut of the CJK faces.

## Standard programs

DEsk is meant to be installed as a whole desktop, like GNOME or KDE, so it
picks a standard program for each job the desk has: a terminal, a web
browser, a file manager and a text editor. The drawer's things and
`Super+Enter` open them. For each job, the first of these that is there:

1. `app.terminal`, `app.browser`, `app.files`, `app.editor` in `shell.conf`
   (a `.desktop` id),
2. the default-applications lists in the freedesktop.org order:
   `~/.config/desk-mimeapps.list` (DEsk only), `~/.config/mimeapps.list`
   (what Shell Settings' Default programs, xfce4-mime-settings, writes), and
   the same two in the system's folders, ending with the
   `desk-mimeapps.list` installed with DEsk -- the same answers xdg-open
   and GIO give,
3. the standard set, classic GTK programs first: Xfce Terminal, Firefox
   ESR, Thunar, Mousepad (then MATE Terminal, Caja, Pluma, and so on),
4. any program filed under the right category.

## A tour

| | |
| --- | --- |
| ![The turned-down corner of a window, with Fold, Unfold and Discard on its back](docs/screenshots/corner.jpg) | ![The drawer pulled out under the desk, with its five things on green felt](docs/screenshots/drawer.jpg) |
| The corner of every window: Fold, Unfold, Discard. | The drawer: the dock. |
| ![A window folded into a packet and left on the desk](docs/screenshots/folded.jpg) | ![The bookcase: eight binders, the ALL APPS book, the calendar, memo pad, clock and post box](docs/screenshots/bookcase.jpg) |
| A folded window, lying on the desk under the others. | The bookcase behind the desk. |
| ![The ALL APPS book open at A to D](docs/screenshots/book.jpg) | ![The ALL APPS book searching for "term"](docs/screenshots/book-search.jpg) |
| ALL APPS, every program from A to Z ... | ... and a line to type into. |
| ![The Control Panel: meters, levers, the volume knob and the power keys](docs/screenshots/control-panel.jpg) | ![The post box, open, with letters in it](docs/screenshots/post-box.jpg) |
| The Control Panel. | The post box: notifications. |
| ![The desk calendar at October, beside its binder of sticky notes](docs/screenshots/calendar.jpg) | ![Shell Settings, System tab: each row names the program it opens](docs/screenshots/shell-settings.jpg) |
| The desk calendar and its notes. | Shell Settings. |

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) -- how DEsk is built: the
  programs, the compositor, the ray-traced room, threads, files.
- [docs/MODDING.md](docs/MODDING.md) -- changing DEsk: words and languages,
  settings, borrowed programs, sounds, the room, things on the desk.
- [docs/CONFIGURATION.md](docs/CONFIGURATION.md) -- every setting, file and
  environment variable.
- [RELEASE-NOTES.md](RELEASE-NOTES.md) -- what is in v0.1.3 and what is known
  not to work yet.
- [docs/design.md](docs/design.md) -- the full development log (in Japanese).

## As a desktop

DEsk draws the desk; the work done on it is done with free software that
already exists. It reaches completeness by borrowing, and keeps a firm line
between what it makes and what it borrows:

| | DEsk makes | both: DEsk draws it, something else does the work | borrowed |
| --- | --- | --- | --- |
| what | what is drawn as the room and handled as its things; what only the compositor can do | | programs, system settings panels, background helpers |
| examples | windows as paper, the drawer, the bookcase, the clock, the Control Panel, the post box, the front door, the calendar and memo pad; keyboard, pointer and their keys (volume and media keys too), the screens and their power, remembering their arrangement, going idle, the lock; Shell Settings | volume knob and mute (PipeWire/PulseAudio), brightness (logind), Wi-Fi and Bluetooth levers, the post box (the desk is the notification daemon), the tray's slips (the applets' own items), the clock's MEDIA page (MPRIS), screenshots (grim), the input method (a bridge to IBus) | network connections, Bluetooth devices, sound devices, display arrangement, printers, the look of other programs, default programs, disks, polkit agent, keyring, portals -- and the programs themselves |

The rule for new cases: make only what is drawn as part of the room or can
only be done inside the compositor; otherwise find an existing program that
works as it is under a wlroots compositor, and make something only as a
last resort. KDE's System Settings and kcmshell (which do not work outside
Plasma), Xfce's and MATE's X11-only dialogs, and Xfce's display and
appearance dialogs (which need xfsettingsd, and xfsettingsd overwrites
GSettings with Xfce's defaults as it starts) are not used.

```sh
sudo sh scripts/install_debian.sh     # DEsk and the whole set (install_ubuntu.sh, install_arch.sh, install_fedora.sh)
sh scripts/install-desktop.sh         # only the programs (shows them, then asks for sudo)
sh scripts/install-desktop.sh --extras   # and the office suite, mail...
```

`install-desktop.sh` works with apt, dnf, pacman and zypper. On Debian and
Ubuntu the same set is also one package:
`equivs-build packaging/debian/desk-desktop.equivs`.

| For | What comes |
| --- | --- |
| terminal, files, text | Xfce Terminal, Thunar (with tumbler and thunar-volman), Mousepad |
| the web | Firefox (Firefox ESR on Debian, the snap on Ubuntu) |
| pictures, documents, music and video | Ristretto, Atril, Parole |
| tools | Engrampa, GNOME Disks |
| networks, Bluetooth, sound | NetworkManager with its applet and editor, Blueman, PipeWire, pavucontrol |
| displays, looks, defaults | wdisplays, nwg-look (GNOME Tweaks on Ubuntu 24.04 and Fedora), Xfce's MIME settings |
| printing | system-config-printer, CUPS |
| notices | udiskie (memory sticks mounted, with a letter), poweralertd (low battery; batsignal on Arch; none on Fedora), package-update-indicator (updates; Debian and Ubuntu) |
| looking after the machine | QDirStat (disk usage), LXQt's date and time and users, Mugshot (your name and picture), Seahorse (passwords and keys), gnome-packagekit (adding and removing programs), Back In Time (backups), Xfce Task Manager, hardinfo2 (about this computer); baobab and Deja Dup on Arch; no Mugshot on Fedora |
| passwords | MATE's polkit agent, GNOME Keyring |
| plumbing | xdg-desktop-portal (wlr, gtk), Xwayland, xdg-user-dirs, xdg-utils, wl-clipboard, grim, curl |
| looks | Qt's GTK platform theme, so Qt programs match the GTK ones |
| typing and fonts | IBus (and Mozc for Japanese), Noto (CJK, emoji), the URW base 35, Klee One (not on Arch or Fedora) |
| `--extras` | LibreOffice (drawn with GTK), Thunderbird, Galculator, Xfce Task Manager, Simple Scan |

The classic GTK programs of Xfce and MATE come first because they are the
kind of window the desk's paper is cut for -- a menu bar, plain tools and
the document -- and they are light, start quickly and run on Wayland as
they are. GTK 3 asks the compositor for its frame through KDE's
server-decoration protocol, so they lie on DEsk's paper with its printed
heading and no GTK title bar. Windows with a header bar (GNOME's programs,
Firefox) keep their own frame, inside a paper margin.

Shell Settings (`desk-settings`) holds DEsk's own settings -- windows, the
size of things, how much is drawn, language, clock, your birthday and the
weather, when the screen goes
off and whether the desk locks then -- on its Shell tab, and on its System
tab rows that open the borrowed programs (network, Bluetooth, sound, displays, printers, the look
of other programs, default programs, disks, disk usage, colour profiles, the
input method, date and time, your name and picture, users, passwords and keys,
adding and removing programs, backups, what the computer is doing, and the
Experience Index), classic-looking programs first, each with the program's
own icon and its name -- "Displays (wdisplays)" -- so you can see what does
the setting. A row whose program is not installed is printed faint with the
package to install, by this distribution's name. The two tabs sit on the
sheet like a folder's index tabs, and in each pocket is a wire-bound pad
with a sheet to each part of the form and a sticky note on each sheet's
edge; taking hold of a note turns the sheets over the wire to it, as on the
desk calendar (Page Up and Down too). Changing tab (a click, Ctrl+Tab or
Ctrl+Page Up/Down) draws the other pad up out of the folder from below, and
the pad comes up the same way when the window opens.

Started as a session (`desk-session`, from a login screen), DEsk tells
D-Bus and systemd where its display is, runs the XDG autostart entries
(leaving out the ones that would fight it, such as another notification
daemon or an X screen locker), and starts an authentication agent, the
keyring, the input method and the network applet with whichever free
program for each is installed. The input method's candidates come up under
the text being written -- in a program's window as on the desk's own pages --
and leave the keys where they are. It turns the screen off when nobody is at the
desk (locking first, by default), lets video players keep it on, and
speaks wlr-output-management (wdisplays, wlr-randr, kanshi), remembering a
display tool's arrangement for each set of screens. Logging out, restarting
and powering off ask every window to close first, so a program with unsaved
work can ask about it; a window that does not close holds it up and is put in
front.

Shell Settings also sets what only a Wayland compositor can: tap to click,
natural scrolling, pointer speed and left-handed buttons (through libinput),
the keyboard layout, a second layout and the keys that change to it, and key
repeat -- applied at once to what is plugged in.

How much the desk draws is a setting too, Graphics: Ultra (the room traced
finer, and wider where a GPU can, and nothing drawn coarser while it moves),
High (as made, the default), Medium, Low,
and 2D Fallback -- for a machine without a GPU or a virtual machine rendering
on its CPU: no shadows, nothing fading, blurred or moving, nothing ray-traced
as it moves, and the room traced once, small and plain. On two CPU cores
without a GPU the first start goes from nearly ten minutes to about a quarter
of a minute, and a frame from 2 ms to 0.5 ms. The installer takes a quick look
at the machine (`desk-index --suggest`: the graphics, or the processor, for a
couple of seconds, the threads and the memory, and how long the processor takes
to draw a sheet of paper folding) and picks the level for it, as the machine's
default (`/etc/xdg/desk/shell.conf`); each user's own choice wins.

Where the screen is drawn by the processor -- no GPU, a virtual machine without
3D, a GPU without its driver -- the desk runs in 2D Fallback unless you choose
a level yourself, and says so: a letter in the post box (check the graphics
drivers, or the virtual machine's settings) and a line in red beside Graphics
in Shell Settings. 2D Fallback is the same desk with its effects off, not a
second one, its room traced at a sample a pixel and softened; what is read on
it -- text, figures, buttons, needles -- is drawn at the screen's own
resolution at every level, the room alone as small as the level. And a first
start, which traces the room from nothing, leaves a letter in the middle of
the desk saying it may take a minute or two, more on some machines, and that
the desk can be used meanwhile.

It is meant to stay light on old machines. Measured on a machine held to
about a 2010 laptop's pace (a quarter of a core each on two cores, 2 GB, no
GPU for the room): a later login shows the desk in under half a second and
has the light on in 1.4 s; the very first one, which has to trace the room
on the processor, shows a soft first look and the light within seven
seconds instead of a minute and a half in the dark; folding a sheet costs
5 ms a frame at Low. The traced room is kept across updates, and paper in
motion is drawn finer or coarser by the level (whole at High, half at Low).

Programs on the desk are dressed for it too: DEsk's own icons, drawn to look
like the real things set down on the desk, the way iOS 6 drew them -- manila
card folders with papers in them, a typed page with its corner turned down, a
letter signed in blue ink, a ledger, slides in their mounts, instant photos,
a record sliding out of its sleeve, a film slate, a taped carton for an
archive, a deck of punched cards for a program, a block of wood type for a
font, a wire wastebasket you see through, a brass globe for the network, a
drive in its aluminium case -- with the grain of paper, card, wood and
brushed metal and the shadows they cast; and a GTK theme of paper, ink and
brass. They are set in a dconf
layer of DEsk's own over the user's, so another desktop on the same machine
keeps its look.

When something on the desk gets stuck: Ctrl+Alt+Del twice puts everything
of the desk's own away and draws it all again (the windows stay as they are);
three times logs out. `desk-ctl reset` and `desk-ctl logout` do the same from
a terminal or another VT.

### The DEsk Experience Index

`desk-index` rates the machine at what the desk asks of it, the way the
Windows Experience Index did: processor, memory, 2D graphics (the desk's
own drawing), 3D graphics (the room's shader, checked against the CPU) and
disk, each 1.0-9.9, on brass-rimmed meters; the base score is the lowest.
`desk-index --text` prints the same in a terminal.

## Requirements

The simplest way is the installer for your distribution (below), which
brings everything. To build it yourself:

- wlroots 0.18 (Debian 13 has it, and Fedora 43 and later as `wlroots0.18`;
  Ubuntu 24.04 has 0.17, Ubuntu 26.04 only 0.19 and Arch only 0.20, so the
  installers build 0.18.3 from its release into `/opt/desk-wlroots`, used by
  DEsk alone through its run path)
- wayland, wayland-protocols, xkbcommon, cairo, pixman, fontconfig, libdrm, PAM
- libsystemd (optional): the notification daemon and the system tray.
  Build with `-Dpost=disabled` to leave them out.
- libpulse (optional): the desk's sounds (`-Dsound=disabled`)
- libinput (optional): touchpad and mouse settings
- EGL, GLES, gbm (optional): ray-trace the room on the GPU. Without them
  the CPU does it.
- Xwayland and xcb (optional): X11-only apps.
- Fonts: the URW base 35 (`fonts-urw-base35`) and Noto CJK
  (`fonts-noto-cjk`); `fonts-klee` for the memo pad.
- At run time: `grim` for screenshots, `curl` for the weather at the door.

### Minimum and recommended

Measured with the CPU, GPU and memory cut down (`systemd-run` CPUQuota and
MemoryMax, `taskset`; nested, 1440x900, the same hand moves every run).
Once the room is traced, even one core without a GPU dropped no frames; a
frame costs about 2 ms. Only the first login traces the room: 6-9 s on a
GPU, about 10 minutes on two cores without one (then it is cached).

| | minimum | recommended |
| --- | --- | --- |
| CPU | 2 x86-64 cores (1 works) | 4 cores or more |
| memory | 4 GB (DEsk itself 200-450 MB) | 8 GB or more |
| GPU | none needed (a long first login) | OpenGL ES 3.0 with GBM (Mesa Intel, AMD, Nouveau; VMware's SVGA3D) |
| screen | 1366x768 | 1440x900 or more |
| disk | about 1 GB (`--bare`; `--no-apps` adds what Shell Settings opens) | 2-2.7 GB with the whole set |
| system | Debian 13, Ubuntu 24.04 / 26.04 LTS, Arch Linux, Fedora 43 / 44 | the same |

## Build

```sh
meson setup build
meson compile -C build
```

## Run

Inside your current session, as a window:

```sh
./build/desk -s foot
```

As a session of its own, picked from the login screen (GDM, SDDM, LightDM),
use the installer for your system ([Install](#install)). All four take the
same options:

```sh
sudo sh scripts/install_debian.sh              # or install_ubuntu.sh, install_arch.sh, install_fedora.sh
sudo sh scripts/install_debian.sh --no-apps    # DEsk, what it needs and what Shell Settings opens
sudo sh scripts/install_debian.sh --bare       # DEsk and what it needs, no programs (containers)
sudo sh scripts/install_debian.sh --prefix /opt/desk
```

`sh scripts/install_session.sh` picks the right one by itself and keeps what
it says in `~/.local/state/desk/install.log`. Each installer does the same
(derivatives such as Mint, Pop!_OS, EndeavourOS, Manjaro and Nobara are recognised):
packages, a release build in `build-install`, the PAM file, the login entry,
and the removal of what the old name, DeskWM, left behind (after asking).
Every program Shell Settings' System tab opens is installed, `--no-apps` or
not, and at the end `desk-settings --check` lists what each row opens (or
which package would bring it).
On other distributions, install the requirements and run `scripts/install.sh`.
Or from a TTY: `desk-session`.

To update, get the new copy and run the installer again the same
way ("this updates it", it says): what is installed is replaced, the last build
is reused (or made afresh when it cannot be), and your settings and the traced
room stay. All the installer says is also kept in
`~/.local/state/desk/install.log` (the last run's) -- the thing to look at, or
send, if it stops part way. A copy made for passing round carries SHA256SUMS,
and the installer checks every file against it before building: a copy damaged
on its way says which files are wrong and stops (copy it again; if a fresh copy
is damaged too, suspect the machine's memory or disk -- memtest86+). A Japanese
locale gets the Mozc converter among the suggested programs; IBus itself is
installed for everyone but only runs for languages typed through an input
method, or when one is set up.

The first start at a new screen size spends a little while ray-tracing the
room, on the GPU the screen is drawn with: the desk first, then the bookcase,
the Control Panel and the post box, each there to turn to as soon as its
picture is, and last the films of the camera moves to them (until a film is
done, the move to its view is a cut). Each part is cached as it is done, so a
first start cut short picks up where it stopped. What DEsk says as it runs is
kept in `~/.local/state/desk/session.log` (the session before in
`session.log.old`); its `desk: room:` lines say what the room was traced on
and how long each part took -- the thing to send if a start is slow. A GPU
is tried once, in a child process, before the room is traced on it; one that
fails says why (`failed its trial: ...`), and the processor traces the room
instead -- at High's quality even where Ultra is chosen, Ultra's being a
GPU's to trace.

## Keys

| Key | |
| --- | --- |
| `Super+Enter` | terminal |
| `Super+Q` / `Alt+F4` | close the focused window |
| `Super+M` | fold / unfold the focused sheet |
| `Super+D` | open / close the drawer |
| `Super+A` | look up at the bookshelf |
| `Super+C` | the Control Panel |
| `Super+N` | the post box |
| `Super+F` | the focused window on the whole screen, and back (games) |
| `Alt+Tab` / `Super+Tab` / `Alt+F1` | cycle windows (one on the whole screen goes back to its sheet first) |
| `Esc` | back to the desk |
| brightness keys | the screen's backlight (or, with none, the lamp) |
| volume, mute and media keys | the mixer and the player playing (as the clock's MEDIA page) |
| `Print` / `Alt+Print` | screenshot of the screen / of the window in front |
| `Super+L` | lock the screen: the front door; type the password, Enter (or click the key) |
| `Super+Esc` / `Alt+Esc` | log out (asking the windows to close first) |

## Reporting a problem

The quickest way: run this in a terminal (any desktop, or a console -- DEsk
need not start), and attach the file it writes, `~/desk-report.txt`:

```sh
desk-report
```

It gathers DEsk's version, your Linux, your graphics card and driver, the
last session's log and whether DEsk crashed, with your user name, home
folder, machine name and network addresses taken out. Read it through before
sending it. Say too what you did and what happened.

Without it (an older DEsk, or one not installed), attach by hand:

- `~/.local/state/desk/session.log` (and `session.log.old` if the problem was
  in the session before) -- what DEsk said as it ran;
- if the installer stopped, its last lines from the terminal (or
  `~/.local/state/desk/install.log` when it was run through
  `install_session.sh`).

Both are plain text; look them over before sending them. If the desk is stuck,
`desk-ctl reset` from a terminal (or another VT) draws everything again
without closing your windows.

## How it was made

DEsk was built with the help of AI. Much of the code and the documentation
was written by Claude (Anthropic's AI model) under the author's direction:
the author decided what DEsk should be and how it should look, tested it on
real machines, and reviewed what went in. Reports and patches are welcome
all the same.

## License

[MIT](LICENSE)
