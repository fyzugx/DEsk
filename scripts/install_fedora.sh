#!/bin/sh
# install_fedora.sh -- DEsk on Fedora, in one go.
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
# DEsk is built on wlroots 0.18. Fedora has moved on to newer ones and keeps
# 0.18 beside them as a package of its own (wlroots0.18, in Fedora 43 and
# later), which is what is installed: whichever package the repositories
# have that brings wlroots-0.18.pc. Where none does, wlroots 0.18 is built
# here from its own release into /opt/desk-wlroots, where only DEsk looks
# for it (DESK_WLROOTS_PREFIX=... for another place).
#
# The build directory is build-install in the source folder (BUILD_DIR=...
# for another), and wlroots, when it is built, in build-install-wlroots
# beside it. When it is done, log out and pick "DEsk" on the login screen.
set -eu

yes=0 apps=1 sets=1 prefix=/usr/local
while [ $# -gt 0 ]; do
	case "$1" in
	--yes|-y) yes=1 ;;
	--no-apps) apps=0 ;;
	--bare) apps=0 sets=0 ;;
	--prefix)
		[ $# -ge 2 ] || { echo "install_fedora.sh: --prefix wants a folder" >&2; exit 2; }
		prefix=$2
		shift
		;;
	--prefix=*) prefix=${1#--prefix=} ;;
	-h|--help) sed -n '2,/^set /s/^# \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "install_fedora.sh: what is $1? (--help says what it takes)" >&2; exit 2 ;;
	esac
	shift
done
case "$prefix" in
/?*) prefix=${prefix%/} ;;
*) echo "install_fedora.sh: the prefix has to be a whole path, like /usr/local" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-install}"
deps="$build-wlroots"
wlr="${DESK_WLROOTS_PREFIX:-/opt/desk-wlroots}"

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die() { printf 'install_fedora.sh: %s\n' "$*" >&2; exit 1; }

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
command -v dnf >/dev/null 2>&1 && command -v rpm >/dev/null 2>&1 \
	|| die "there is no dnf here, so this is not Fedora (install_session.sh picks the right one)"
case "$wlr" in
/?*) wlr=${wlr%/} ;;
*) die "DESK_WLROOTS_PREFIX has to be a whole path, like /opt/desk-wlroots" ;;
esac
if [ "$yes" -eq 0 ] && [ ! -t 0 ]; then
	die "there is nobody to answer its questions here; run it with --yes to take the suggested answers"
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Root's work -- dnf, and putting files under the prefix, /etc, /opt and
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

# Which Fedora: Fedora itself, or one built on it (Nobara, Ultramarine...),
# which goes by what its dnf has.
name="this system" like="" ver=""
if [ -r /etc/os-release ]; then
	# shellcheck disable=SC1091
	name=$(. /etc/os-release && printf '%s' "${PRETTY_NAME:-${ID:-this system}}")
	# shellcheck disable=SC1091
	like=$(. /etc/os-release && printf '%s %s' "${ID:-}" "${ID_LIKE:-}")
	# shellcheck disable=SC1091
	ver=$(. /etc/os-release && printf '%s' "${VERSION_ID:-}")
fi

# Installed (rpm keeps the list, so no root and no network for it); and to
# be had from the repositories dnf has. dnf takes a second or more to start,
# so what the repositories have is asked once for every name at the same
# time (has_list), as root, so that the lists read are the system's own and
# not a second copy in somebody's home.
installed() { rpm -q --quiet "$1" 2>/dev/null; }
avail=$tmp/avail
: > "$avail"
known() { grep -qxF -- "$1" "$avail"; }
synced=0
update() {
	[ "$synced" -eq 1 ] && return 0
	say "Reading what the repositories have (dnf makecache)..."
	as_root dnf -q makecache >/dev/null \
		|| say "Note: dnf makecache had trouble (above); going on with the lists it has."
	synced=1
}
has_list() {
	# has_list NAME... -- which of them the repositories have, into $avail
	update
	as_root dnf -q repoquery --available --queryformat '%{name}\n' "$@" 2>/dev/null \
		| grep -v '^$' | sort -u >> "$avail" || true
}

# What is built here when no repository has wlroots 0.18: each line is what
# it is, its pkg-config name, the oldest one wlroots 0.18 takes, the release
# built, where that comes from and its SHA-256, and how it is set up.
# Fedora's Wayland, its protocols and libdrm are new enough, so it is
# wlroots alone. The icons are looked for where the system keeps them, not
# under /opt, or there would be no cursors.
# The wlroots line is the same in install_ubuntu.sh and install_arch.sh: a
# new release goes into all three.
SOURCES='
wlroots|wlroots-0.18|0.18.3|0.18.3|https://gitlab.freedesktop.org/wlroots/wlroots/-/releases/0.18.3/downloads/wlroots-0.18.3.tar.gz|164a7c8bf9f8ae2c1fb00e7bddb6f08cad7e81b3eb35577b48483b1ac265a087|-Dexamples=false -Dwerror=false -Dbackends=drm,libinput,x11 -Drenderers=gles2 -Dallocators=gbm -Dsession=enabled -Dxwayland=enabled -Dlibliftoff=disabled -Dcolor-management=disabled -Dxcb-errors=disabled -Dicon_directory=/usr/share/icons
'

# What it needs, by what for: need|what it is for|Fedora packages, the first
# one installed counting, else the first one the repositories have. "build"
# and "run" are always installed; "dnf" when wlroots 0.18 is a package here
# (the package that brings it is filled in below), "source" when it is
# built; "set" always: what Shell Settings' System pad opens, and what those
# programs need to work (the polkit agent, the keyring, printing, IBus), so
# that every row of it opens something; "app" only without --no-apps. The
# programs are the classic GTK ones of Xfce and MATE, as in the installers
# for Debian, Ubuntu and Arch: they leave their frame to DEsk, so they lie on
# the desk as sheets of paper, and nothing else. What Fedora does not have
# is left out, and said (the memo pad's Klee One, Mugshot, the battery and
# update letters); nwg-look is not packaged, and GNOME's Tweaks sets the
# same things. The theme that dresses Qt programs as the GTK ones is in Qt
# itself (qt6-qtbase-gui, qt5-qtbase-gui), and IBus's setup is a package of
# its own (ibus-setup), which ibus only recommends.
LIST='
build|compiler|gcc
build|compiler: the C library headers|glibc-devel
build|build system (Meson)|meson
build|build system: Ninja|ninja-build
build|build system: finding libraries (pkgconf)|pkgconf-pkg-config
build|the room shader, made into C (Python)|python3
dnf|wlroots 0.18|@WLROOTS@
build|Wayland|wayland-devel
build|Wayland protocols|wayland-protocols-devel
build|keyboards (xkbcommon)|libxkbcommon-devel
build|touchpads and mice (libinput)|libinput-devel
build|drawing (cairo)|cairo-devel
build|text shaping (PangoCairo)|pango-devel
build|drawing: pixels (pixman)|pixman-devel
build|typefaces (fontconfig)|fontconfig-devel
build|the display (libdrm)|libdrm-devel
build|the room on the GPU (EGL, GLES)|libglvnd-devel
build|the room on the GPU (EGL: Mesa'"'"'s part)|mesa-libEGL-devel
build|the room on the GPU (gbm)|mesa-libgbm-devel
build|the post box and the tray (sd-bus)|systemd-devel
build|sounds (libpulse)|pulseaudio-libs-devel
build|the screen lock (PAM)|pam-devel
build|X11 programs (xcb)|libxcb-devel
build|X11 programs: window hints (xcb-icccm, xcb-ewmh)|xcb-util-wm-devel
source|wlroots: fetching its release|curl
source|wlroots: the seat (libseat)|libseat-devel
source|wlroots: monitors (libdisplay-info)|libdisplay-info-devel
source|wlroots: monitor makers'"'"' names|hwdata-devel
source|wlroots: X11 (x11-xcb)|libX11-devel
source|wlroots: X11 (xcb-render-util)|xcb-util-renderutil-devel
source|wlroots: where Xwayland is|xorg-x11-server-Xwayland-devel
run|X11 programs (Xwayland)|xorg-x11-server-Xwayland
run|a session bus for each login|dbus-broker dbus-daemon
run|portals: file choosers, screen sharing|xdg-desktop-portal
run|portals: GTK file chooser|xdg-desktop-portal-gtk
run|portals: screenshots and screen sharing|xdg-desktop-portal-wlr
run|screen sharing: asking which screen (zenity)|zenity
run|screen sharing: the PipeWire session manager|wireplumber
run|typefaces: Latin and more (Noto Sans)|google-noto-sans-fonts
run|typefaces: Latin and more (Noto Serif)|google-noto-serif-fonts
run|typefaces: Chinese, Japanese, Korean (Noto Sans CJK)|google-noto-sans-cjk-vf-fonts google-noto-sans-cjk-fonts
run|typefaces: Chinese, Japanese, Korean (Noto Serif CJK)|google-noto-serif-cjk-vf-fonts google-noto-serif-cjk-fonts
run|typefaces: emoji (Noto Color Emoji)|google-noto-color-emoji-fonts
run|typefaces: the desk'"'"'s own (URW base 35)|urw-base35-fonts
run|screenshots (Print)|grim
run|screenshots: a part of the screen|slurp
run|screenshots: onto the clipboard|wl-clipboard
run|screenshots: the letter saying where it went|libnotify
run|home folders (Documents, Pictures...)|xdg-user-dirs
run|opening files and links|xdg-utils
run|apps'"'"' icons drawn from SVG (rsvg-convert)|librsvg2-tools
run|the weather at the front door (curl)|curl
app|terminal (Xfce Terminal)|xfce4-terminal
app|files (Thunar)|Thunar
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
app|web browser (Firefox)|firefox
set|asking for passwords (MATE polkit agent)|mate-polkit
set|keeping passwords (keyring)|gnome-keyring
set|keyring: unlocked at login|gnome-keyring-pam
app|networks (applet; it brings NetworkManager)|network-manager-applet
set|networks: connection editor|nm-connection-editor
set|Bluetooth (Blueman)|blueman
app|memory sticks: mounted, with a letter (udiskie)|udiskie
app|tray slips for udiskie and other indicators|libayatana-appindicator-gtk3
app|sound (PipeWire, or the PulseAudio you have)|pipewire-pulseaudio pulseaudio
set|sound: devices and levels|pavucontrol
set|printers|system-config-printer
set|printing service|cups
set|disks (GNOME Disks)|gnome-disk-utility
set|disk usage (QDirStat)|qdirstat baobab
set|colour profiles: the service|colord
set|date, time and users (from LXQt)|lxqt-admin
set|passwords and keys (Seahorse)|seahorse
set|adding and removing programs|gnome-packagekit
set|backups (Back In Time)|backintime-qt deja-dup
set|full-colour icons (Back In Time wants them)|adwaita-icon-theme-legacy
set|what the computer is doing|xfce4-taskmanager
set|about this computer|hardinfo2 cpu-x
set|about this computer: the graphics part|glx-utils mesa-demos
set|Qt 6 programs on Wayland|qt6-qtwayland
set|Qt 5 programs on Wayland|qt5-qtwayland
set|displays (wdisplays)|wdisplays
set|default programs (Xfce MIME settings)|xfce4-settings
set|theme, icons and cursor (GNOME Tweaks)|gnome-tweaks
set|Qt 6 programs dressed as the GTK ones|qt6-qtbase-gui
set|Qt 5 programs dressed as the GTK ones|qt5-qtbase-gui
'
# IBus, for every desk: Shell Settings' Input method opens its setup, and
# DEsk hands it the keys itself -- unless fcitx5 is here, which someone has
# chosen (and whose own setup that row opens then).
if ! installed fcitx5; then
	LIST="$LIST
set|input method (IBus)|ibus
set|input method: IBus's own setup|ibus-setup
"
fi
# and for a Japanese desktop, the Japanese converter: IBus, which DEsk
# hands the keys to itself, with Mozc, else Anthy. Someone who has fcitx5
# has chosen it, and desk-session-helper would pick IBus over it once that
# is installed.
case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in
ja*)
	if ! installed fcitx5; then
		LIST="$LIST
app|input method: Japanese (Mozc, else Anthy)|ibus-mozc ibus-anthy
"
	fi
	;;
esac

say "DEsk for Fedora -- $name"
case " $like " in
*" fedora "*) ;;
*) say "Note: this is for Fedora, and $name does not say it is built on Fedora."
	say "      It is tried anyway: what decides it is what dnf has." ;;
esac
case "$ver" in
'' | *[!0-9]*) ;;
*) [ "$ver" -ge 43 ] \
	|| say "Note: this is made for Fedora 43 and later; $ver is tried, going by what dnf has." ;;
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
	step "What DEsk needs, and the programs that go with it (dnf)"
else
	if [ "$sets" -eq 1 ]; then
		step "What DEsk needs, and what its Shell Settings opens (dnf)"
	else
		step "What DEsk needs (dnf)"
	fi
fi

# wlroots 0.18: the package that brings wlroots-0.18.pc where one is
# installed or to be had (wlroots0.18-devel in Fedora 43 and later), built
# here where none is. Built here before, it is used again.
stamp="$wlr/share/desk-wlroots/built"
# (rpm says "no package provides ..." on its output when none does: only
# its exit says which)
if wlrpkg=$(rpm -q --whatprovides 'pkgconfig(wlroots-0.18)' --queryformat '%{name}\n' 2>/dev/null); then
	wlrpkg=$(printf '%s\n' "$wlrpkg" | head -n 1)
else
	wlrpkg=""
fi
if [ -n "$wlrpkg" ]; then
	wlroots=dnf
	say "wlroots 0.18 is installed ($wlrpkg)."
	if [ -f "$stamp" ]; then
		say "  (The one built here before, in $wlr, is used no more: sudo rm -r $wlr"
		say "  takes it away.)"
	fi
elif [ -f "$stamp" ]; then
	wlroots=source
	say "wlroots 0.18 was built here before, into $wlr."
else
	update
	wlrpkg=$(as_root dnf -q repoquery --available --whatprovides 'pkgconfig(wlroots-0.18)' \
		--queryformat '%{name}\n' 2>/dev/null | grep -v '^$' | head -n 1) || wlrpkg=""
	if [ -n "$wlrpkg" ]; then
		wlroots=dnf
		say "wlroots 0.18 is a package here ($wlrpkg)."
	else
		wlroots=source
		say "No repository here has wlroots 0.18, so it is built here (below), into $wlr."
	fi
fi

wanted() {
	printf '%s\n' "$LIST" | while IFS='|' read -r need what pkgs; do
		[ -n "$need" ] || continue
		[ "$need" = app ] && [ "$apps" -eq 0 ] && continue
		[ "$need" = set ] && [ "$sets" -eq 0 ] && continue
		[ "$need" = dnf ] && [ "$wlroots" != dnf ] && continue
		[ "$need" = source ] && [ "$wlroots" != source ] && continue
		[ "$pkgs" = @WLROOTS@ ] && pkgs=$wlrpkg
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

# the repositories are asked only when something is to be installed
wanted > "$tmp/wanted"
missing=0
while IFS='|' read -r need what pkgs; do
	have "$pkgs" >/dev/null || { missing=1; break; }
done < "$tmp/wanted"
if [ "$missing" -eq 1 ]; then
	# shellcheck disable=SC2046
	has_list $(cut -d'|' -f3 "$tmp/wanted" | tr ' ' '\n' | sort -u)
fi
list=$tmp/install
: > "$list"
: > "$tmp/keep"
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
	die "dnf does not have$short, which DEsk cannot do without (are Fedora's own
repositories, fedora and updates, on? dnf repolist says)"
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
	# Without the packages they only recommend (weak dependencies), as the
	# other installers leave out what apt recommends: what DEsk wants is
	# named above. With them, GNOME's Tweaks brought GNOME Shell, and that
	# GDM, which went on to be the login screen.
	# shellcheck disable=SC2086
	if [ "$yes" -eq 1 ]; then
		as_root dnf install -y --setopt=install_weak_deps=False $pkgs \
			|| die "dnf could not install them; what it said is above"
	else
		# dnf says what else comes with them, and how much, and asks
		as_root dnf install --setopt=install_weak_deps=False $pkgs \
			|| die "dnf did not install them; what it said is above"
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
# desktop since taken away): marked as installed for itself, or
# "dnf autoremove" after that something went would take it too. dnf 5
# says "user", dnf 4 "install".
dep=$(as_root dnf -q -C repoquery --installed --queryformat '%{name} %{reason}\n' 2>/dev/null \
	| awk 'tolower($2) ~ /^dep|^weak/ { print $1 }' | grep -Fx -f "$tmp/keep" | tr '\n' ' ' || true)
if [ -n "$dep" ]; then
	say "Kept for DEsk (marked as installed by you):$(printf ' %s' $dep)"
	# shellcheck disable=SC2086
	as_root dnf -q -y mark user $dep >/dev/null 2>&1 \
		|| as_root dnf -q -y mark install $dep >/dev/null 2>&1 \
		|| say "Note: dnf mark could not; dnf autoremove may take them with what needed them."
fi
# what Fedora does not have: said, and done without
if ! fc-list 2>/dev/null | grep -q 'Klee One'; then
	say
	say "Note: the memo pad's handwriting is Klee One, which Fedora does not have;"
	say "      without it the pad is written in Noto Serif CJK."
fi

# -- 2. wlroots 0.18, when there is none to install -------------------------
# Built as the builder in build-install-wlroots and installed under $wlr,
# with its own library folder written into everything built against it
# (the run path), so DEsk finds this wlroots and nothing else on the system
# does: the libraries are not on ld.so's list. (The same in
# install_ubuntu.sh and install_arch.sh.)
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
# SELinux, where it is on: DEsk's files get the labels the policy gives
# their places (bin_t for the programs, etc_t for the PAM file), whatever
# made them -- a copy keeps the label it had where it was made.
if command -v restorecon >/dev/null 2>&1 && command -v selinuxenabled >/dev/null 2>&1 \
	&& selinuxenabled; then
	as_root restorecon -R "$prefix"/bin/desk* "$data/desk" /etc/pam.d/desk 2>/dev/null || true
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
# /usr/share when nothing says otherwise. D-Bus starts them in systemd's
# user environment, where XDG_DATA_DIRS is unset or keeps those two: the
# prefix's own copy is found for /usr and /usr/local, and any other prefix
# gets one in /usr/share.
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

# -- 5. services ------------------------------------------------------------
# Fedora turns most services on as they are installed (its systemd presets);
# printing and Bluetooth (which Shell Settings' Printers and Bluetooth
# devices open, so they are installed --no-apps or not) are looked at all
# the same, and turned on here if they are off (at every start, and now if
# systemd is running: in a chroot it is not).
if command -v systemctl >/dev/null 2>&1; then
	off=""
	for unit in cups.socket bluetooth.service; do
		[ -f "/usr/lib/systemd/system/$unit" ] || continue
		systemctl is-enabled "$unit" >/dev/null 2>&1 || off="$off $unit"
	done
	if [ -n "$off" ]; then
		step "Services the programs above use"
		say "  off:$off"
		if ask "Turn them on (systemctl enable)?"; then
			# shellcheck disable=SC2086
			as_root systemctl enable $off || say "Note: systemctl could not (above)."
			if [ -d /run/systemd/system ]; then
				# shellcheck disable=SC2086
				as_root systemctl start $off || say "Note: they start at the next boot."
			fi
		else
			say "Left off."
		fi
	fi
	if [ -f /usr/lib/systemd/system/NetworkManager.service ] \
		&& ! systemctl is-enabled NetworkManager.service >/dev/null 2>&1; then
		say
		say "Note: the network applet shows NetworkManager's networks, and NetworkManager"
		say "      is off. If nothing else runs your network:"
		say "        sudo systemctl enable --now NetworkManager"
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
# a login screen is one systemd is told to start (display-manager.service);
# Fedora Workstation has GDM, the KDE edition SDDM, Server and the minimal
# installs none
if [ -e /etc/systemd/system/display-manager.service ]; then
	say "Log out, pick \"DEsk\" on the login screen (the session menu: often a gear"
	say "by the password box, or a list at the bottom or top of the screen), and log in."
else
	say "There is no login screen here. Log in on the console and type"
	case "$prefix" in
	/usr|/usr/local) say "  desk-session" ;;
	*) say "  $prefix/bin/desk-session" ;;
	esac
	say "or install a login screen, which lists DEsk by itself, and turn it on --"
	say "LightDM is a light one:"
	say "  sudo dnf install lightdm lightdm-gtk && sudo systemctl enable lightdm"
	say "  sudo systemctl set-default graphical.target"
fi
