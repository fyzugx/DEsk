#!/bin/sh
# install_debian.sh -- DEsk on Debian 13 (trixie), in one go.
#
# Run it as yourself from the folder you got DEsk in; it asks for sudo
# when it needs to (run as root -- in a container, say -- it needs none).
# It installs what DEsk is built from and what it runs with, the classic
# programs that go with the desk (unless --no-apps), builds DEsk and
# installs it, puts it on the login screen, and clears away what was
# installed under its old name (DeskWM). Run it again after an update to
# rebuild and reinstall; what is there already is left alone.
#
#   --yes          do not ask; take the answers it would suggest
#   --no-apps      only DEsk, what it needs and the programs its Shell Settings
#                  opens, not the other recommended programs
#   --bare         only DEsk and what it needs: no programs (for a container)
#   --prefix DIR   where DEsk goes (default /usr/local)
#   --help         this
#
# DEsk is built on wlroots 0.18, which Debian has from 13 on, so all of
# it comes from apt.
#
# The build directory is build-install in the source folder (BUILD_DIR=...
# for another). When it is done, log out and pick "DEsk" on the login
# screen.
set -eu

yes=0 apps=1 sets=1 prefix=/usr/local
while [ $# -gt 0 ]; do
	case "$1" in
	--yes|-y) yes=1 ;;
	--no-apps) apps=0 ;;
	--bare) apps=0 sets=0 ;;
	--prefix)
		[ $# -ge 2 ] || { echo "install_debian.sh: --prefix wants a folder" >&2; exit 2; }
		prefix=$2
		shift
		;;
	--prefix=*) prefix=${1#--prefix=} ;;
	-h|--help) sed -n '2,/^set /s/^# \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "install_debian.sh: what is $1? (--help says what it takes)" >&2; exit 2 ;;
	esac
	shift
done
case "$prefix" in
/?*) prefix=${prefix%/} ;;
*) echo "install_debian.sh: the prefix has to be a whole path, like /usr/local" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-install}"

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die() { printf 'install_debian.sh: %s\n' "$*" >&2; exit 1; }

if ! grep -q "^project('desk'" "$root/meson.build" 2>/dev/null; then
	die "$root does not look like DEsk's source folder (no meson.build for it)"
fi
# A copy made for passing round carries SHA256SUMS, and every file is
# checked against it first: a copy damaged on its way (a stick pulled out
# too soon) stops here and says which files, not at some line of the build
# that says nothing of why. install_session.sh checks it before it runs
# this, and says so (DESK_SUMS_CHECKED). A git checkout has none.
if [ -z "${DESK_SUMS_CHECKED:-}" ] && [ -f "$root/SHA256SUMS" ] \
	&& command -v sha256sum >/dev/null 2>&1; then
	if ! damaged=$(cd "$root" && sha256sum --quiet -c SHA256SUMS 2>&1); then
		printf '%s\n' "this copy of DEsk is damaged -- these files are not what they were" \
			"when it was made:" >&2
		printf '%s\n' "$damaged" | sed 's/^/  /' >&2
		die "copy the folder again; if a fresh copy is damaged too, suspect the machine's memory or disk (memtest86+)"
	fi
	say "DEsk: every file of this copy checked against SHA256SUMS."
fi
command -v apt-get >/dev/null 2>&1 && command -v dpkg-query >/dev/null 2>&1 \
	|| die "there is no apt here, so this is not Debian (install_session.sh picks the right one)"
if [ "$yes" -eq 0 ] && [ ! -t 0 ]; then
	die "there is nobody to answer its questions here; run it with --yes to take the suggested answers"
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Root's work -- apt, and putting files under the prefix, /etc and
# /usr/share -- is done through sudo when we are not root already.
if [ "$(id -u)" -eq 0 ]; then
	as_root() { "$@"; }
else
	command -v sudo >/dev/null 2>&1 \
		|| die "installing needs root: run this as root, or install sudo and add yourself to it"
	as_root() { sudo "$@"; }
fi

# The build is done as whoever owns the source folder, even when this is
# run as root: a build directory owned by root in somebody's clone is a
# nuisance ever after.
builder=
if [ "$(id -u)" -eq 0 ]; then
	owner=$(stat -c %u "$root")
	if [ "$owner" -ne 0 ] && id -nu "$owner" >/dev/null 2>&1; then
		builder=$(id -nu "$owner")
	fi
fi
as_builder() {
	if [ -n "$builder" ]; then
		runuser -u "$builder" -- "$@"
	else
		"$@"
	fi
}

ask() {
	# ask "question" -- yes unless the answer is no; --yes answers for us
	[ "$yes" -eq 1 ] && return 0
	printf '%s [Y/n] ' "$1"
	read -r answer || answer=n
	case "$answer" in
	''|y|Y|yes|Yes) return 0 ;;
	*) return 1 ;;
	esac
}

# Which Debian: 13 is the one with wlroots 0.18, and one built on it will
# do. Ubuntu is Debian underneath too, but names things its own way and
# has an installer of its own.
name="this system" like=""
if [ -r /etc/os-release ]; then
	# shellcheck disable=SC1091
	name=$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-${ID:-this system}}")
	# shellcheck disable=SC1091
	like=$(. /etc/os-release && printf '%s %s' "${ID:-}" "${ID_LIKE:-}")
fi
case " $like " in
*" ubuntu "*)
	die "this is $name, which is Ubuntu underneath: scripts/install_ubuntu.sh is for it"
	;;
esac
base=$(cat /etc/debian_version 2>/dev/null || echo unknown)

installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }
# (a name apt has only heard of -- one another package breaks or replaces,
# say -- has nothing to install, though apt-cache does not count it a miss)
known() { apt-cache show --no-all-versions "$1" 2>/dev/null | grep -q '^Package:'; }
updated=0
update() {
	[ "$updated" -eq 1 ] && return 0
	say "Reading what apt has (apt-get update)..."
	as_root apt-get update -qq \
		|| say "Note: apt-get update had trouble (above); going on with the lists it has."
	updated=1
}

# What it needs, by what for: need|what it is for|Debian packages, the
# first one installed counting, else the first one apt knows. "build" and
# "run" are always installed, and so is "set": what Shell Settings' System
# pad opens, and what those programs need to work (the polkit agent, the
# keyring, printing, IBus), so that every row of it opens something. "app"
# only without --no-apps. The programs are the classic GTK ones of Xfce and MATE, as in install_ubuntu.sh and
# install_arch.sh: they leave their frame to DEsk, so they lie on the
# desk as sheets of paper, and nothing else.
LIST='
build|compiler|gcc
build|compiler: the C library headers|libc6-dev
build|build system (Meson)|meson
build|build system: Ninja|ninja-build
build|build system: finding libraries|pkg-config
build|the room shader, made into C (Python)|python3
build|wlroots 0.18|libwlroots-0.18-dev
build|Wayland|libwayland-dev
build|Wayland protocols|wayland-protocols
build|keyboards (xkbcommon)|libxkbcommon-dev
build|touchpads and mice (libinput)|libinput-dev
build|drawing (cairo)|libcairo2-dev
build|text shaping (PangoCairo)|libpango1.0-dev
build|drawing: pixels (pixman)|libpixman-1-dev
build|typefaces (fontconfig)|libfontconfig-dev
build|the display (libdrm)|libdrm-dev
build|the room on the GPU (EGL)|libegl-dev
build|the room on the GPU (GLES)|libgles-dev
build|the room on the GPU (gbm)|libgbm-dev
build|the post box and the tray (sd-bus)|libsystemd-dev
build|sounds (libpulse)|libpulse-dev
build|the screen lock (PAM)|libpam0g-dev
build|X11 programs (xcb)|libxcb1-dev
build|X11 programs: window hints (xcb-icccm)|libxcb-icccm4-dev
build|X11 programs: wlroots wants it (xcb-ewmh)|libxcb-ewmh-dev
run|X11 programs (Xwayland)|xwayland
run|a session bus for each login|dbus-user-session
run|portals: file choosers, screen sharing|xdg-desktop-portal
run|portals: GTK file chooser|xdg-desktop-portal-gtk
run|portals: screenshots and screen sharing|xdg-desktop-portal-wlr
run|screen sharing: asking which screen (zenity)|zenity
run|screen sharing: the PipeWire session manager|wireplumber
run|typefaces: Latin and more (Noto)|fonts-noto-core
run|typefaces: Chinese, Japanese, Korean (Noto CJK)|fonts-noto-cjk
run|typefaces: emoji (Noto Color Emoji)|fonts-noto-color-emoji
run|typefaces: the desk'"'"'s own (URW base 35)|fonts-urw-base35
run|typefaces: handwriting on the memo pad (Klee)|fonts-klee
run|screenshots (Print)|grim
run|screenshots: a part of the screen|slurp
run|screenshots: onto the clipboard|wl-clipboard
run|screenshots: the letter saying where it went|libnotify-bin
run|home folders (Documents, Pictures...)|xdg-user-dirs
run|opening files and links|xdg-utils
run|apps'"'"' icons drawn from SVG (rsvg-convert)|librsvg2-bin
run|the weather at the front door (curl)|curl
app|terminal (Xfce Terminal)|xfce4-terminal
app|files (Thunar)|thunar
app|files: disks, trash and network places|gvfs
app|files: disks and memory sticks|udisks2
app|files: memory sticks opened when put in|thunar-volman
app|files: thumbnails|tumbler
app|text editor (Mousepad)|mousepad
app|pictures (Ristretto)|ristretto
app|PDF and documents (Atril)|atril
app|archives (Engrampa)|engrampa
app|archives: zip|zip
app|archives: unzip|unzip
app|music and video (Parole)|parole
app|web browser (Firefox)|firefox-esr firefox
set|asking for passwords (MATE polkit agent)|mate-polkit
set|keeping passwords (keyring)|gnome-keyring
set|keyring: unlocked at login|libpam-gnome-keyring
app|networks (applet; it brings NetworkManager)|network-manager-applet network-manager-gnome
set|networks: connection editor|nm-connection-editor network-manager-gnome
set|Bluetooth (Blueman)|blueman
app|memory sticks: mounted, with a letter (udiskie)|udiskie
app|tray slips for udiskie and other indicators|gir1.2-ayatanaappindicator3-0.1
app|a letter when the battery runs low|poweralertd
app|a letter when there are updates|package-update-indicator
app|sound (PipeWire, or the PulseAudio you have)|pipewire-pulse pulseaudio
set|sound: devices and levels|pavucontrol
set|printers|system-config-printer
set|printing service|cups
set|disks (GNOME Disks)|gnome-disk-utility
set|disk usage (QDirStat)|qdirstat baobab
set|colour profiles: the service|colord
set|date, time and users (from LXQt)|lxqt-admin
set|your name and picture (Mugshot)|mugshot
set|passwords and keys (Seahorse)|seahorse
set|adding and removing programs|gnome-packagekit
set|backups (Back In Time)|backintime-qt deja-dup
set|full-colour icons (Back In Time wants them)|adwaita-icon-theme-legacy yaru-theme-icon
set|what the computer is doing|xfce4-taskmanager
set|about this computer|hardinfo2 cpu-x
set|about this computer: the graphics part|mesa-utils
set|Qt 6 programs on Wayland|qt6-wayland
set|Qt 5 programs on Wayland|qtwayland5
set|displays (wdisplays)|wdisplays
set|default programs (Xfce MIME settings)|xfce4-settings
set|theme, icons and cursor (nwg-look)|nwg-look gnome-tweaks
set|Qt 6 programs dressed as the GTK ones|qt6-gtk-platformtheme
set|Qt 5 programs dressed as the GTK ones|qt5-gtk-platformtheme
'
# IBus, for every desk: Shell Settings' Input method opens its setup, and
# DEsk hands it the keys itself -- unless fcitx5 is here, which someone has
# chosen (and whose own setup that row opens then).
if ! installed fcitx5; then
	LIST="$LIST
set|input method (IBus)|ibus
"
fi
# and for a Japanese desktop, the Japanese converter: IBus, which DEsk
# hands the keys to itself. Someone who has fcitx5 has chosen it, and
# desk-session-helper would pick IBus over it once that is installed.
case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in
ja*)
	if ! installed fcitx5; then
		LIST="$LIST
app|input method: Japanese (Mozc)|ibus-mozc
"
	fi
	;;
esac

say "DEsk for Debian 13 -- $name"
case "$base" in
13*|trixie*) ;;
*) say "Note: this is for Debian 13 (trixie), and $name is on Debian $base. It is tried"
	say "      anyway: what decides it is whether apt has wlroots 0.18 (libwlroots-0.18-dev)." ;;
esac
say
say "  source    $root"
say "  build     $build"
say "  prefix    $prefix"
if [ "$apps" -eq 1 ]; then
	say "  programs  Shell Settings' and the recommended set (--no-apps: only the former)"
else
	if [ "$sets" -eq 1 ]; then
		say "  programs  only what Shell Settings opens (--no-apps)"
	else
		say "  programs  none (--bare): only DEsk and what it needs"
	fi
fi

# -- 1. packages ------------------------------------------------------------
if [ "$apps" -eq 1 ]; then
	step "What DEsk needs, and the programs that go with it (apt)"
else
	if [ "$sets" -eq 1 ]; then
		step "What DEsk needs, and what its Shell Settings opens (apt)"
	else
		step "What DEsk needs (apt)"
	fi
fi
wanted() {
	printf '%s\n' "$LIST" | while IFS='|' read -r need what pkgs; do
		[ -n "$need" ] || continue
		[ "$need" = app ] && [ "$apps" -eq 0 ] && continue
		[ "$need" = set ] && [ "$sets" -eq 0 ] && continue
		printf '%s|%s|%s\n' "$need" "$what" "$pkgs"
	done
}
# the first of the names that is installed, if one is
have() {
	for p in $1; do
		if installed "$p"; then echo "$p"; return 0; fi
	done
	return 1
}

# the lists are read only when something is to be installed
wanted > "$tmp/wanted"
missing=0
while IFS='|' read -r need what pkgs; do
	have "$pkgs" >/dev/null || { missing=1; break; }
done < "$tmp/wanted"
[ "$missing" -eq 0 ] || update
list=$tmp/install
: > "$list"
short=""
while IFS='|' read -r need what pkgs; do
	if pick=$(have "$pkgs"); then
		printf '  %-50s %s\n' "$what" "$pick (installed)"
		echo "$pick" >> "$tmp/keep"
		continue
	fi
	pick=""
	for p in $pkgs; do
		if known "$p"; then pick=$p; break; fi
	done
	if [ -n "$pick" ]; then
		printf '  %-50s %s\n' "$what" "$pick"
		grep -qx "$pick" "$list" || echo "$pick" >> "$list"
	else
		printf '  %-50s %s\n' "$what" "${pkgs%% *} (not to be had here)"
		case "$need" in app|set) ;; *) short="$short ${pkgs%% *}" ;; esac
	fi
done < "$tmp/wanted"
if [ -n "$short" ]; then
	die "apt does not have$short, which DEsk cannot do without. It needs wlroots 0.18,
which Debian has from 13 (trixie) on; on another Debian, see the README."
fi
if [ -s "$list" ]; then
	pkgs=$(tr '\n' ' ' < "$list")
	n=$(wc -l < "$list")
	say
	if [ "$n" -eq 1 ]; then
		say "To install (1 package, and what it needs in turn):"
	else
		say "To install ($n packages, and what they need in turn):"
	fi
	say "  $pkgs"
	# shellcheck disable=SC2086
	if [ "$yes" -eq 1 ]; then
		as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y \
			-o DPkg::Lock::Timeout=300 --no-install-recommends $pkgs \
			|| die "apt could not install them; what it said is above"
	else
		# apt says what else comes with them, and how much, and asks
		as_root apt-get install -o DPkg::Lock::Timeout=300 --no-install-recommends $pkgs \
			|| die "apt did not install them; what it said is above"
	fi
	# and they are all there before anything is built on them
	for pkg in $pkgs; do
		installed "$pkg" || die "$pkg did not get installed, so DEsk is not installed either"
	done
else
	say
	say "Everything is installed already."
fi
# What DEsk counts on that was installed only for something else (a
# desktop since taken away): marked as installed for itself, or apt
# autoremove would take it away with the rest.
touch "$tmp/keep"
auto=$(apt-mark showauto 2>/dev/null | grep -Fx -f "$tmp/keep" | tr '\n' ' ' || true)
if [ -n "$auto" ]; then
	say "Kept for DEsk (so apt autoremove leaves them):$(printf ' %s' $auto)"
	# shellcheck disable=SC2086
	as_root apt-mark manual $auto >/dev/null || say "Note: apt-mark could not (above)."
fi

# -- 2. build ---------------------------------------------------------------
step "Building DEsk${builder:+ (as $builder, who owns the source)}"
setup() {
	as_builder meson setup "$@" --prefix="$prefix" --buildtype=release "$build" "$root" \
		> "$tmp/setup.log" 2>&1
}
# The last resort, when the build left from an earlier install cannot be set
# up again (another Meson's, or partly root's after a run with sudo): it is
# only a build, so it goes, and is made afresh.
fresh() {
	say "The build left in $build could not be set up again; it is made afresh."
	as_root rm -rf -- "$build" && setup
}
if [ -f "$build/meson-private/coredata.dat" ]; then
	# set up before: the prefix may have changed, or Meson itself, and a
	# directory a different Meson set up cannot be reconfigured, only wiped
	setup --reconfigure || setup --wipe || fresh \
		|| { cat "$tmp/setup.log"; die "Meson could not set up $build"; }
else
	setup || { cat "$tmp/setup.log"; die "Meson could not set up $build"; }
fi
# what Meson found, from its summary
sed -n '/^desk [0-9]/,/^Found ninja/{/^Found ninja/d;p}' "$tmp/setup.log"
as_builder ninja -C "$build" || die "the build failed; what went wrong is above"
[ -x "$build/desk" ] || die "the build left no desk program in $build"

# -- 3. install -------------------------------------------------------------
step "Installing into $prefix"
as_root meson install -C "$build" --no-rebuild --quiet \
	|| die "installing into $prefix failed (above)"
# Meson's note of what it installed belongs with the rest of the build
as_root chown --reference="$build" "$build/meson-logs/install-log.txt" 2>/dev/null || true
data="$prefix/share"
for f in "$prefix/bin/desk" "$prefix/bin/desk-session" /etc/pam.d/desk; do
	[ -e "$f" ] || die "$f is not there after installing"
done
say "  $prefix/bin/desk and the programs that go with it (desk-session, desk-settings...)"
say "  /etc/pam.d/desk (the screen lock checks the password as login does)"

# The login screen. GDM, SDDM and LightDM all read /usr/share/wayland-sessions,
# and LightDM nothing else, so the entry goes there, saying where
# desk-session is. It stays in one place only: a login screen that reads
# both would list DEsk twice.
sessions=/usr/share/wayland-sessions
if [ "$data" != /usr/share ]; then
	sed -e "s|^Exec=desk-session|Exec=$prefix/bin/desk-session|" \
		-e "s|^TryExec=desk-session|TryExec=$prefix/bin/desk-session|" \
		"$data/wayland-sessions/desk.desktop" > "$tmp/desk.desktop"
	as_root install -D -m 644 "$tmp/desk.desktop" "$sessions/desk.desktop"
	as_root rm -f "$data/wayland-sessions/desk.desktop"
	as_root rmdir "$data/wayland-sessions" 2>/dev/null || true
fi
say "  $sessions/desk.desktop (\"DEsk\" on the login screen)"

# The portals look for desk-portals.conf -- which portal does what on this
# desktop -- in /etc, and in XDG_DATA_DIRS, which is /usr/local/share and
# /usr/share when nothing says otherwise (so with xdg-desktop-portal 1.18
# to 1.22, on all the systems the installers are for). D-Bus starts them
# in systemd's user environment, where XDG_DATA_DIRS is unset or keeps
# those two: the prefix's own copy is found for /usr and /usr/local, and
# any other prefix gets one in /usr/share.
case "$prefix" in
/usr|/usr/local) ;;
*) as_root install -D -m 644 "$data/xdg-desktop-portal/desk-portals.conf" \
		/usr/share/xdg-desktop-portal/desk-portals.conf
	say "  /usr/share/xdg-desktop-portal/desk-portals.conf (which portal does what)"
	;;
esac

# A cache already there has to hear about what is new, or it is not found;
# without one, things are looked for as they are, and none is made.
on_path() { command -v "$1" >/dev/null 2>&1; }
if [ -f "$data/icons/hicolor/icon-theme.cache" ] && on_path gtk-update-icon-cache; then
	as_root gtk-update-icon-cache -q -f "$data/icons/hicolor" || true
fi
if [ -f "$data/applications/mimeinfo.cache" ] && on_path update-desktop-database; then
	as_root update-desktop-database -q "$data/applications" || true
fi

# install.sh used to put DEsk in /usr, and the installers put it in
# /usr/local: with one in each, the one first on the PATH is the one that runs
for other in /usr /usr/local; do
	if [ "$other" != "$prefix" ] && [ -x "$other/bin/desk" ]; then
		say
		say "Note: there is another DEsk in $other ($other/bin/desk). With two, the"
		say "      one that runs is the one first on the PATH (usually /usr/local's)."
		say "      \"sudo ninja uninstall\" in the build it came from takes it away."
	fi
done

# desk-session puts its own folder on the PATH and the prefix's share first
# in XDG_DATA_DIRS, so a DEsk session finds everything wherever it is;
# other sessions find the prefix's share only if XDG_DATA_DIRS names it
case "$prefix" in
/usr|/usr/local) ;;
*) say
	say "Note: a DEsk session finds what is in $prefix/share by itself. For other"
	say "      sessions to list DEsk's programs (Shell Settings, the Experience"
	say "      Index) too, add it to XDG_DATA_DIRS -- in /etc/environment, say:"
	say "        XDG_DATA_DIRS=$prefix/share:/usr/local/share:/usr/share"
	;;
esac

# -- 4. the old name --------------------------------------------------------
# DEsk was DeskWM: deskwm, deskwm-session... The old installer put it in
# /usr by default, so /usr and /usr/local are looked in as well as the
# prefix. Only what is named deskwm* is touched; what you kept in
# ~/.config/deskwm and the like is moved over by DEsk itself.
old=$tmp/old
for p in "$prefix" /usr/local /usr; do
	[ -d "$p/bin" ] && find "$p/bin" -maxdepth 1 -name 'deskwm*' 2>/dev/null
	[ -d "$p/share" ] && find "$p/share" -maxdepth 5 -name 'deskwm*' -prune 2>/dev/null
done | sort -u > "$old"
[ -e /etc/pam.d/deskwm ] && echo /etc/pam.d/deskwm >> "$old"
if [ -s "$old" ]; then
	step "Clearing away DeskWM, which is what DEsk was called before"
	sed 's/^/  /' "$old"
	if ask "Remove these?"; then
		while IFS= read -r f; do
			as_root rm -rf -- "$f"
		done < "$old"
		say "Removed."
		if grep -q 'wayland-sessions/deskwm' "$old"; then
			say "\"DeskWM\" is gone from the login screen; pick \"DEsk\" instead."
		fi
	else
		say "Left as they are."
	fi
fi

# -- what Shell Settings opens ---------------------------------------------
# Each row of its System pad, and the program it opens now: a row with none
# is printed faint there, and is said here with the package that brings it.
if [ "$sets" -eq 1 ] && [ -x "$prefix/bin/desk-settings" ]; then
	step "Shell Settings: what each row of the System pad opens"
	as_builder "$prefix/bin/desk-settings" --check > "$tmp/check" 2>&1 || true
	sed 's/^/  /' "$tmp/check"
	if grep -q MISSING "$tmp/check"; then
		say "Note: the rows marked MISSING open nothing yet; the packages named bring them."
	fi
fi

# -- the graphics this machine starts at -----------------------------------
# A quick look -- desk-index --suggest: the graphics (or, with none, the
# processor) for a couple of seconds, the threads and the memory -- chooses
# the Graphics level the desk starts at here, for every user of the machine
# (/etc/xdg/desk/shell.conf, under each user's own). Anyone changes their
# own in Shell Settings; DESK_GRAPHICS=<level> chooses in place of the look.
if [ "$sets" -eq 1 ] && [ -x "$prefix/bin/desk-index" ]; then
	step "The graphics this machine starts at"
	level="${DESK_GRAPHICS:-}"
	if [ -z "$level" ]; then
		level=$(as_builder "$prefix/bin/desk-index" --suggest 2> "$tmp/suggest" | tail -n 1) \
			|| level=""
		sed -n 's/^desk-index: /  /p' "$tmp/suggest"
	fi
	case "$level" in
	2d | low | medium | high | ultra)
		sysconf=/etc/xdg/desk/shell.conf
		if [ -f "$sysconf" ]; then
			grep -v '^[[:space:]]*graphics[[:space:]]*=' "$sysconf" > "$tmp/sysconf" || true
		else
			{
				echo "# DEsk's settings for every user of this machine, under each user's own"
				echo "# (~/.config/desk/shell.conf). The installer chose the graphics from a"
				echo "# quick look at the machine; Shell Settings changes a user's own."
			} > "$tmp/sysconf"
		fi
		echo "graphics = $level" >> "$tmp/sysconf"
		as_root mkdir -p /etc/xdg/desk
		as_root install -m 644 "$tmp/sysconf" "$sysconf"
		say "  graphics = $level, for every user (Shell Settings: Graphics, to change one's own)"
		;;
	*)
		say "  Left as made (High): the look said nothing it could use."
		;;
	esac
fi

step "Done"
say "DEsk is installed in $prefix."
# a login screen is one systemd is told to start (display-manager.service),
# and Debian notes which in /etc/X11/default-display-manager
if [ -e /etc/systemd/system/display-manager.service ] \
	|| [ -s /etc/X11/default-display-manager ]; then
	say "Log out, pick \"DEsk\" on the login screen (the session menu: often a gear"
	say "by the password box, or a list at the bottom or top of the screen), and log in."
else
	say "There is no login screen here. Log in on the console and type"
	case "$prefix" in
	/usr|/usr/local) say "  desk-session" ;;
	*) say "  $prefix/bin/desk-session" ;;
	esac
	say "or install a login screen, which lists DEsk by itself -- LightDM is a light one:"
	say "  sudo apt install lightdm lightdm-gtk-greeter"
fi
