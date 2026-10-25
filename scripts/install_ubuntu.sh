#!/bin/sh
# install_ubuntu.sh -- DEsk on Ubuntu 24.04 LTS and 26.04 LTS, in one go.
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
# DEsk is built on wlroots 0.18, which neither LTS has: 24.04 has 0.17
# and 26.04 has 0.19. So wlroots 0.18 is built here from its own release,
# with the newer Wayland it needs on 24.04, into /opt/desk-wlroots, where
# only DEsk looks for it (DESK_WLROOTS_PREFIX=... for another place).
# An Ubuntu whose apt has wlroots 0.18 (24.10 to 25.10) gets that instead.
#
# The build directory is build-install in the source folder (BUILD_DIR=...
# for another), and wlroots is built in build-install-wlroots beside it.
# When it is done, log out and pick "DEsk" on the login screen.
set -eu

yes=0 apps=1 sets=1 prefix=/usr/local
while [ $# -gt 0 ]; do
	case "$1" in
	--yes|-y) yes=1 ;;
	--no-apps) apps=0 ;;
	--bare) apps=0 sets=0 ;;
	--prefix)
		[ $# -ge 2 ] || { echo "install_ubuntu.sh: --prefix wants a folder" >&2; exit 2; }
		prefix=$2
		shift
		;;
	--prefix=*) prefix=${1#--prefix=} ;;
	-h|--help) sed -n '2,/^set /s/^# \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "install_ubuntu.sh: what is $1? (--help says what it takes)" >&2; exit 2 ;;
	esac
	shift
done
case "$prefix" in
/?*) prefix=${prefix%/} ;;
*) echo "install_ubuntu.sh: the prefix has to be a whole path, like /usr/local" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-install}"
deps="$build-wlroots"
wlr="${DESK_WLROOTS_PREFIX:-/opt/desk-wlroots}"

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die() { printf 'install_ubuntu.sh: %s\n' "$*" >&2; exit 1; }

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
	|| die "there is no apt here, so this is not Ubuntu (install_session.sh picks the right one)"
case "$wlr" in
/?*) wlr=${wlr%/} ;;
*) die "DESK_WLROOTS_PREFIX has to be a whole path, like /opt/desk-wlroots" ;;
esac
if [ "$yes" -eq 0 ] && [ ! -t 0 ]; then
	die "there is nobody to answer its questions here; run it with --yes to take the suggested answers"
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Root's work -- apt, and putting files under the prefix, /etc, /opt and
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

# Which Ubuntu: 24.04 and 26.04 are the ones this is made for; one built on
# Ubuntu (Mint, Pop!_OS...) goes by the Ubuntu it is built on. Debian has
# apt too, but names things its own way, has no "universe" to turn on, and
# has an installer of its own.
name="this system" release=unknown like=""
if [ -r /etc/os-release ]; then
	# shellcheck disable=SC1091
	name=$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-${ID:-this system}}")
	# shellcheck disable=SC1091
	release=$(. /etc/os-release && printf '%s' "${UBUNTU_CODENAME:-${VERSION_CODENAME:-unknown}}")
	# shellcheck disable=SC1091
	like=$(. /etc/os-release && printf '%s %s' "${ID:-}" "${ID_LIKE:-}")
fi
case " $like " in
*" ubuntu "*) ;;
*" debian "*)
	die "this is $name, which is Debian and not Ubuntu: scripts/install_debian.sh is for it"
	;;
esac

installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }
# (a name apt has only heard of -- one another package breaks or replaces,
# say -- has nothing to install, though apt-cache does not count it a miss)
known() { apt-cache show --no-all-versions "$1" 2>/dev/null | grep -q '^Package:'; }
candidate() { apt-cache policy "$1" 2>/dev/null | sed -n 's/^ *Candidate: //p'; }
updated=0
update() {
	[ "$updated" -eq 1 ] && return 0
	say "Reading what apt has (apt-get update)..."
	as_root apt-get update -qq \
		|| say "Note: apt-get update had trouble (above); going on with the lists it has."
	updated=1
}

# What is built here when there is no wlroots 0.18 to install: each line is
# what it is, its pkg-config name, the oldest one wlroots 0.18 takes, the
# release built, where that comes from and its SHA-256, and how it is set
# up. wlroots itself is always built; the others only when the system's is
# too old (24.04's Wayland is 1.22). The icons are looked for where the
# system keeps them, not under /opt, or there would be no cursors.
# The wlroots line is the same in install_arch.sh: a new release goes into
# both.
SOURCES='
wayland|wayland-server|1.23|1.23.1|https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.23.1/downloads/wayland-1.23.1.tar.xz|864fb2a8399e2d0ec39d56e9d9b753c093775beadc6022ce81f441929a81e5ed|-Ddocumentation=false -Dtests=false -Ddtd_validation=false -Dicon_directory=/usr/share/icons
wayland-protocols|wayland-protocols|1.35|1.45|https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.45/downloads/wayland-protocols-1.45.tar.xz|4d2b2a9e3e099d017dc8107bf1c334d27bb87d9e4aff19a0c8d856d17cd41ef0|-Dtests=false
libdrm|libdrm|2.4.122|2.4.125|https://dri.freedesktop.org/libdrm/libdrm-2.4.125.tar.xz|d4bae92797a50f81a93524762e0410a49cd84cfa0f997795bc0172ac8fb1d96a|-Dauto_features=disabled -Dtests=false
wlroots|wlroots-0.18|0.18.3|0.18.3|https://gitlab.freedesktop.org/wlroots/wlroots/-/releases/0.18.3/downloads/wlroots-0.18.3.tar.gz|164a7c8bf9f8ae2c1fb00e7bddb6f08cad7e81b3eb35577b48483b1ac265a087|-Dexamples=false -Dwerror=false -Dbackends=drm,libinput,x11 -Drenderers=gles2 -Dallocators=gbm -Dsession=enabled -Dxwayland=enabled -Dlibliftoff=disabled -Dcolor-management=disabled -Dxcb-errors=disabled -Dicon_directory=/usr/share/icons
'

# What it needs, by what for: need|what it is for|Ubuntu packages, the
# first one installed counting, else the first one apt knows. "build" and
# "run" are always installed; "apt" when apt has wlroots 0.18, "source"
# when it is built here; "set" always: what Shell Settings' System pad
# opens, and what those programs need to work (the polkit agent, the
# keyring, printing, IBus), so that every row of it opens something; "app"
# only without --no-apps. The programs are the classic GTK ones of Xfce and MATE, as in install_debian.sh and
# install_arch.sh: they leave their frame to DEsk, so they lie on the
# desk as sheets of paper, and nothing else.
LIST='
build|compiler|gcc
build|compiler: the C library headers|libc6-dev
build|build system (Meson)|meson
build|build system: Ninja|ninja-build
build|build system: finding libraries|pkg-config
build|the room shader, made into C (Python)|python3
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
apt|wlroots 0.18|libwlroots-0.18-dev
source|wlroots: fetching its release|curl
source|wlroots: fetching its release (certificates)|ca-certificates
source|wlroots: unpacking Wayland|xz-utils
source|wlroots: the seat (libseat)|libseat-dev
source|wlroots: devices (udev)|libudev-dev
source|wlroots: monitors (libdisplay-info)|libdisplay-info-dev
source|wlroots: monitor makers'"'"' names|pnp.ids hwdata
source|wlroots: monitor makers'"'"' names (pkg-config)|hwdata
source|wlroots: Xwayland (where it is)|xwayland
source|wlroots: X11 (xcb-composite)|libxcb-composite0-dev
source|wlroots: X11 (xcb-res)|libxcb-res0-dev
source|wlroots: X11 (xcb-xfixes)|libxcb-xfixes0-dev
source|wlroots: X11 (xcb-render)|libxcb-render0-dev
source|wlroots: X11 (xcb-render-util)|libxcb-render-util0-dev
source|wlroots: X11 (xcb-dri3)|libxcb-dri3-dev
source|wlroots: X11 (xcb-present)|libxcb-present-dev
source|wlroots: X11 (xcb-shm)|libxcb-shm0-dev
source|wlroots: X11 (xcb-xinput)|libxcb-xinput-dev
source|Wayland, if it is built here (libffi)|libffi-dev
source|Wayland, if it is built here (expat)|libexpat1-dev
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

say "DEsk for Ubuntu -- $name"
case "$release" in
noble|resolute) ;;
*) say "Note: this is for Ubuntu 24.04 (noble) and 26.04 (resolute), and $name is"
	say "      \"$release\". It is tried anyway: what decides it is what apt has." ;;
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
		[ "$need" = apt ] && [ "$wlroots" != apt ] && continue
		[ "$need" = source ] && [ "$wlroots" != source ] && continue
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

# wlroots 0.18: from apt where it has it, built here where it has not.
# Built here before, it is used again. Whether apt has it is for the lists
# to say, so they are read first if anything at all is to be installed.
stamp="$wlr/share/desk-wlroots/built"
wlroots=unknown
if installed libwlroots-0.18-dev; then
	wlroots=apt
elif [ -f "$stamp" ]; then
	wlroots=source
fi
missing=0
[ "$wlroots" = unknown ] && missing=1
wanted > "$tmp/wanted"
while IFS='|' read -r need what pkgs; do
	have "$pkgs" >/dev/null || { missing=1; break; }
done < "$tmp/wanted"
if [ "$missing" -eq 1 ]; then
	update
	# Ubuntu keeps most of these in "universe": the Xfce and MATE programs,
	# libseat, wdisplays. A desktop install has it on; a server or a
	# container may not.
	if ! apt-cache policy 2>/dev/null | grep -q '/universe '; then
		say "Most of what DEsk uses is in Ubuntu's \"universe\" component, which is off here."
		ask "Turn universe on?" || die "without universe, apt has not got what DEsk needs"
		ubuntu_sources=/etc/apt/sources.list.d/ubuntu.sources
		if [ -f "$ubuntu_sources" ]; then
			# Since 24.04 Ubuntu's own sources are in this file, which
			# add-apt-repository does not change (it says it has, and
			# changes only sources.list): universe is put on its
			# Components lines here, the old file kept as .save beside it
			# -- the name add-apt-repository keeps one under, which apt
			# passes over.
			as_root cp "$ubuntu_sources" "$ubuntu_sources.save"
			as_root sed -i '/^Components:/{/[[:space:]]universe\([[:space:]]\|$\)/!s/$/ universe/}' \
				"$ubuntu_sources"
			say "  universe is on in $ubuntu_sources (it was as $ubuntu_sources.save)"
		else
			if ! command -v add-apt-repository >/dev/null 2>&1; then
				as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
					--no-install-recommends software-properties-common
			fi
			as_root add-apt-repository -y -n universe
		fi
		updated=0
		update
		apt-cache policy 2>/dev/null | grep -q '/universe ' \
			|| die "universe is still not on; turn it on in Software & Updates and run this again"
	fi
fi
if [ "$wlroots" = unknown ]; then
	if known libwlroots-0.18-dev; then wlroots=apt; else wlroots=source; fi
fi
if [ "$wlroots" = apt ]; then
	say "wlroots 0.18 is to be had from apt."
elif [ -f "$stamp" ]; then
	say "wlroots 0.18 was built here before, into $wlr."
else
	say "apt has no wlroots 0.18, so it is built here (below), into $wlr."
fi

# Ubuntu's Firefox is a snap: apt's "firefox" only installs it from the
# Snap Store, which is done by itself further on. One built on Ubuntu that
# has Firefox as it is (Linux Mint) gets it from apt with the rest.
firefox_from=none
if [ "$apps" -eq 1 ]; then
	if command -v snap >/dev/null 2>&1 && snap list firefox >/dev/null 2>&1; then
		firefox_from=installed
	elif installed firefox || installed firefox-esr; then
		case "$(dpkg-query -W -f='${Version}' firefox 2>/dev/null)" in
		*snap*) firefox_from=snap ;;
		*) firefox_from=installed ;;
		esac
	else
		case "$(candidate firefox)" in
		''|'(none)') ;;
		*snap*) firefox_from=snap ;;
		*) firefox_from=apt
			LIST="$LIST
app|web browser (Firefox)|firefox
" ;;
		esac
	fi
fi

list=$tmp/install
: > "$list"
short=""
wanted > "$tmp/wanted"
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
case "$firefox_from" in
installed) printf '  %-50s %s\n' "web browser (Firefox)" "firefox (installed)" ;;
snap) printf '  %-50s %s\n' "web browser (Firefox)" "firefox (a snap: further on)" ;;
esac
if [ -n "$short" ]; then
	die "apt does not have$short, which DEsk cannot do without (is this Ubuntu,
with its \"universe\" and \"-updates\" on?)"
fi
if [ -s "$list" ]; then
	# (the lists are read already, unless Firefox alone is to come from apt)
	update
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

# -- 2. wlroots 0.18, when there is none to install -------------------------
# Built as the builder in build-install-wlroots and installed under $wlr,
# with its own library folder written into everything built against it
# (the run path), so DEsk finds this wlroots -- and this Wayland, when
# that is built too -- and nothing else on the system does: the libraries
# are not on ld.so's list. (The same in install_arch.sh.)
pcpath="$wlr/lib/pkgconfig:$wlr/share/pkgconfig"
wlrver=$(printf '%s\n' "$SOURCES" | awk -F'|' '$1 == "wlroots" { print $4 }')
fetch() {
	# fetch URL SHA256 -- the release, kept in $deps, checked every time
	f="$deps/${1##*/}"
	if [ ! -f "$f" ] || ! printf '%s  %s\n' "$2" "$f" | sha256sum -c --status; then
		say "  fetching ${1##*/}"
		as_builder curl -fsSL --retry 3 -o "$f.part" "$1" || die "could not fetch $1"
		as_builder mv "$f.part" "$f"
		printf '%s  %s\n' "$2" "$f" | sha256sum -c --status \
			|| die "${1##*/} is not the file it should be (its SHA-256 differs); try again later"
	fi
}
if [ "$wlroots" = source ]; then
	step "wlroots $wlrver, built into $wlr"
	# what is built: wlroots, and whatever the system has too old a one of
	: > "$tmp/sources"
	printf '%s\n' "$SOURCES" > "$tmp/table"
	while IFS='|' read -r what pc need ver url sum opts; do
		[ -n "$what" ] || continue
		if [ "$what" != wlroots ] && pkg-config --atleast-version="$need" "$pc" 2>/dev/null; then
			say "  $what $(pkg-config --modversion "$pc"): the system's will do"
			continue
		fi
		printf '%s|%s|%s|%s|%s\n' "$what" "$ver" "$url" "$sum" "$opts" >> "$tmp/sources"
	done < "$tmp/table"
	todo=$(cut -d'|' -f1,2 "$tmp/sources" | tr '|' ' ')
	if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$todo" ] \
		&& PKG_CONFIG_PATH=$pcpath pkg-config --exact-version="$wlrver" wlroots-0.18 2>/dev/null; then
		say "  $(printf '%s' "$todo" | tr '\n' ',' | sed 's/,/, /g'): built before, kept"
	else
		as_builder mkdir -p "$deps"
		# a build folder of its own, so git leaves it alone as it does Meson's
		[ -f "$deps/.gitignore" ] || printf '*\n' | as_builder tee "$deps/.gitignore" >/dev/null
		while IFS='|' read -r what ver url sum opts; do
			start=$(date +%s)
			fetch "$url" "$sum"
			src="$deps/$what-$ver"
			as_builder rm -rf "$src"
			as_builder tar -xf "$deps/${url##*/}" -C "$deps"
			say "  building $what $ver"
			# the run path, for what has libraries (the protocols are only XML)
			[ "$what" = wayland-protocols ] || opts="$opts -Dc_link_args=-Wl,-rpath,$wlr/lib"
			# shellcheck disable=SC2086
			as_builder env PATH="$wlr/bin:$PATH" meson setup "$src/_build" "$src" \
				--prefix="$wlr" --libdir=lib --buildtype=release \
				-Dpkg_config_path="$wlr/lib/pkgconfig,$wlr/share/pkgconfig" \
				$opts > "$tmp/$what.log" 2>&1 \
				|| { cat "$tmp/$what.log"; die "Meson could not set up $what $ver"; }
			as_builder meson compile -C "$src/_build" > "$tmp/$what.log" 2>&1 \
				|| { tail -n 40 "$tmp/$what.log"; die "$what $ver did not build"; }
			as_root meson install -C "$src/_build" --no-rebuild --quiet > "$tmp/$what.log" 2>&1 \
				|| { cat "$tmp/$what.log"; die "$what $ver did not install into $wlr"; }
			say "  $what $ver: built and installed in $(( $(date +%s) - start )) s"
		done < "$tmp/sources"
		PKG_CONFIG_PATH=$pcpath pkg-config --exact-version="$wlrver" wlroots-0.18 \
			|| die "wlroots $wlrver is not to be found in $wlr after building it"
		printf '%s\n' "$todo" > "$tmp/built"
		as_root install -D -m 644 "$tmp/built" "$stamp"
	fi
	[ "$(PKG_CONFIG_PATH=$pcpath pkg-config --variable=have_xwayland wlroots-0.18)" = true ] \
		|| die "the wlroots in $wlr was built without Xwayland"
fi

# -- 3. build ---------------------------------------------------------------
step "Building DEsk${builder:+ (as $builder, who owns the source)}"
# With wlroots built here, Meson looks there first and writes its folder
# into DEsk's programs as their run path; with a package's, both are
# emptied, which matters to a build directory set up the other way before.
# ("[]" is an empty list to Meson; an empty pkg_config_path would be ".".)
if [ "$wlroots" = source ]; then
	findwlr="-Dpkg_config_path=$wlr/lib/pkgconfig,$wlr/share/pkgconfig"
	linkwlr="-Dc_link_args=-Wl,-rpath,$wlr/lib"
	path="$wlr/bin:$PATH"
else
	findwlr="-Dpkg_config_path=[]" linkwlr="-Dc_link_args=" path=$PATH
fi
setup() {
	as_builder env PATH="$path" meson setup "$@" --prefix="$prefix" --buildtype=release \
		"$findwlr" "$linkwlr" "$build" "$root" > "$tmp/setup.log" 2>&1
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
	# directory a different Meson set up cannot be reconfigured, only wiped.
	# The options are set with "meson configure" first, as a reconfigure
	# alone keeps the link arguments the directory was set up with.
	{ as_builder env PATH="$path" meson configure "$build" -Dprefix="$prefix" -Dbuildtype=release \
		"$findwlr" "$linkwlr" > "$tmp/setup.log" 2>&1 && setup --reconfigure; } \
		|| setup --wipe || fresh || { cat "$tmp/setup.log"; die "Meson could not set up $build"; }
else
	setup || { cat "$tmp/setup.log"; die "Meson could not set up $build"; }
fi
# what Meson found, from its summary
sed -n '/^desk [0-9]/,/^Found ninja/{/^Found ninja/d;p}' "$tmp/setup.log"
as_builder env PATH="$path" ninja -C "$build" || die "the build failed; what went wrong is above"
[ -x "$build/desk" ] || die "the build left no desk program in $build"

# -- 4. install -------------------------------------------------------------
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
if [ "$wlroots" = source ]; then
	# the run path is what finds wlroots; a program that cannot is no use
	# (the helpers that are scripts have none to find). DEsk's own only:
	# desk* would take in DeskWM's deskwm, cleared away further on, and in
	# /usr desktop-file-install and the like.
	for f in "$prefix/bin/desk" "$prefix"/bin/desk-*; do
		if LC_ALL=C ldd "$f" 2>/dev/null | grep -q 'not found'; then
			LC_ALL=C ldd "$f" | grep 'not found' >&2
			die "$f cannot find all its libraries"
		fi
	done
	say "  wlroots $wlrver for them in $wlr"
fi

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

# -- 5. Firefox, as Ubuntu has it -------------------------------------------
# From the Snap Store, as on any Ubuntu desktop -- which needs snapd running,
# and it is not in a container or a chroot; then it is left for later.
if [ "$firefox_from" = snap ]; then
	step "The web browser: Firefox, from the Snap Store as Ubuntu has it"
	snapd=""
	if command -v snap >/dev/null 2>&1; then
		snapd=$(snap version 2>/dev/null | awk '$1 == "snapd" { print $2 }')
	fi
	if [ -n "$snapd" ] && [ "$snapd" != unavailable ]; then
		if ask "Install Firefox (snap install firefox)?"; then
			as_root snap install firefox || say "Note: the Snap Store said no (above); Firefox is left out."
		else
			say "Left out."
		fi
	else
		say "  snapd is not running here, so it is left out; once it is: sudo snap install firefox"
	fi
fi

# -- 6. the old name --------------------------------------------------------
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
# and Ubuntu notes which in /etc/X11/default-display-manager
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
