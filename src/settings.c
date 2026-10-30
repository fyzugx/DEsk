/* desk-settings: Shell Settings.
 *
 * The shell's own settings, on a sheet of the shell's paper. This is a
 * program like any other on the desk: it asks DEsk for its frame, so it
 * is a sheet with a heading and a turned-down corner, folds, and is thrown
 * away like the rest -- and on that paper, its own surface clear wherever
 * nothing is drawn, it draws a folder with a pad in each of its pockets,
 * and prints its form on the pads' sheets. What is chosen goes into
 * $XDG_CONFIG_HOME/desk/shell.conf, which DEsk reads again as soon as
 * it changes; the lines it does not know about are kept as they were.
 *
 * It is also where the rest of the machine is set from, and this is where
 * the line is drawn between what DEsk makes and what it borrows (README.md
 * and docs/design.md have the same line as a table):
 *
 *   DEsk makes      what is drawn as the room and handled as its things --
 *                     windows as paper, the drawer, the shelves, the clock,
 *                     the Control Panel, the post box, the front door --
 *                     and what only the compositor can do: the keyboard
 *                     and pointer and their keys, the screens and their
 *                     power, going idle, the lock. Its own settings are on
 *                     this form: windows, the size of things, the language,
 *                     the clock, your birthday and the weather, how long
 *                     the screen stays on and whether the desk locks then.
 *   both              DEsk draws the thing, a program or service does the
 *                     work: the volume knob and mute (PipeWire or
 *                     PulseAudio), the brightness (logind), the Wi-Fi and
 *                     Bluetooth levers, the post box (the desk is the
 *                     notification daemon), the tray's slips (the applets'
 *                     own items), the clock's MEDIA page (MPRIS),
 *                     screenshots (grim).
 *   other programs    network connections, Bluetooth devices, sound devices
 *                     and each program's volume, where the screens stand
 *                     and their resolution, printers, the look of other
 *                     programs, default programs, disks -- and the programs
 *                     themselves, the classic GTK ones of Xfce and MATE
 *                     first; and the DEsk Experience Index, desk-index,
 *                     which is a program of its own.
 *
 * Each job on the third line is a row on the System pad (the other tab), with
 * the programs that do it in the order they are preferred: Xfce's, MATE's and ones that
 * stand alone, as plain as the desk is -- never KDE's System Settings or
 * kcmshell, which do not work outside Plasma. The first that is installed
 * is started, let go of, with the session's environment; with none, the row
 * is printed faint and, if a package here brings one, names it by the name
 * it goes by on this system (Debian's, Ubuntu's or Arch's).
 *
 * What does not work here is not offered, as tried in DEsk with a GTK
 * program beside it. MATE's appearance, display, keyboard and mouse
 * dialogs are X11 programs that crash on a Wayland display, and what they
 * write is put into effect only by MATE's settings daemon, on X. Xfce's
 * keyboard and mouse dialogs say they work on X11 only: under Wayland the
 * compositor has the keyboard and the pointer. nwg-displays speaks to sway
 * and Hyprland only.
 *
 * Only programs that need nothing running beside them are offered.
 * wdisplays puts where the screens stand into effect through
 * wlr-output-management itself, and DEsk keeps what is put into effect
 * that way (desk/displays.conf), so the screens stand the same at the next
 * login. nwg-look writes the look of programs itself where GTK on Wayland
 * reads it: GSettings' org.gnome.desktop.interface, and GTK's settings.ini;
 * where it is not packaged (Ubuntu 24.04), GNOME's Tweaks writes the same
 * GSettings. Xfce's display and appearance dialogs are not offered: they
 * put nothing into effect (4.20 and on) without xfsettingsd running, and
 * xfsettingsd, as it starts, copies Xfce's own defaults into
 * org.gnome.desktop.interface over what was set there -- Xfce's theme, the
 * Tango icons, Sans 10 -- so every GTK program would look different though
 * nothing was changed. */

#define _GNU_SOURCE
#include "agenda.h"
#include "apps.h"
#include "compat.h"
#include "desk.h"
#include "fonts.h"
#include "i18n.h"
#include "sound.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#ifdef DESK_HAVE_WL_CURSOR
#include <wayland-cursor.h>
#endif

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

/* The window: the pad, and the notes standing out of it to the right. */
#define FORM_W 694
#define FORM_H_MAX 660
/* Two pads, each in a pocket of a folder under an index tab along the
 * top: the shell's own settings, and the rest of the machine (the
 * programs that set it). The other pad comes up out of the folder from
 * the bottom, as a sheet pulled out from under a pile, in PULL_S -- and so
 * does the pad in front when the window first opens, PULL_WAIT after. */
#define TAB_H 42.0
#define PULL_S 0.50
#define PULL_WAIT 0.12
static const char *const TAB_WORDS[2] = { "Shell", "System" };
/* A pad has a sheet to each part of the form, hung from a wire along the
 * top as the desk calendar's pages are, and each sheet has a sticky note
 * stuck to its right-hand edge at a height of its own, standing out of the
 * pad. Taking hold of a note turns the sheets over the wire to its sheet;
 * those turned over hang behind the pad, their notes still to be seen. */
#define PAD_X 10.0          /* the pad's left edge */
#define NOTE_OUT 96.0       /* the room the notes stand out into, right of it */
#define PAD_WIRE 14.0       /* the wire, under the foot of the tabs */
#define PAD_INK 22.0        /* from the wire to where the print starts */
#define PAD_FOOT 12.0       /* under the pad: the edges of the sheets under */
#define NOTE_TOP 34.0       /* the first note, down from the wire */
#define NOTE_H 34.0
#define NOTE_STEP 42.0
#define NOTE_STUCK 16.0     /* how much of a note is stuck on its sheet */
#define FLIP_S 0.50         /* a sheet over the wire; a few together take longer */
#define FLIP_EYE 0.30       /* the eye a little above the pad */
#define FLIP_END (M_PI / 2.0 + FLIP_EYE + 0.35)   /* over, and out of sight */
#define PAGES_MAX 12
/* What is written on each part's note: as short as on a sticky note. */
static const char *const NOTE_WORDS[][2] = {
	{ "WINDOWS", "Windows" },
	{ "DESK", "Desk" },
	{ "SCREEN", "Screen" },
	{ "KEYBOARD AND POINTER", "Keyboard,\nmouse" },
	{ "DAYS AND WEATHER", "Days,\nweather" },
	{ "DRAWER AND ALL APPS", "Drawer" },
	{ "SOUND", "Sound" },
	{ "NETWORK AND DEVICES", "Network,\ndevices" },
	{ "LOOK AND LANGUAGE", "Look,\nlanguage" },
	{ "THIS COMPUTER", "Computer" },
};
/* DESK_SLOW=n, as the shell has it: what moves takes n times as long */
static double g_slow = 1.0;

/* A program part of the machine is set in: its name on PATH. */
struct prog {
	const char *bin;
};

/* Which names packages go by here (os_read), as the package lists of Debian
 * 13, Ubuntu 24.04 and 26.04, Arch and Fedora have them: Debian's and
 * Ubuntu's as they are from Debian 13 and Ubuntu 26.04 on; as they were
 * before those, when nm-connection-editor came in network-manager-gnome and
 * nwg-look was not packaged; Arch's; and Fedora's. */
enum { PKG_NOW, PKG_BEFORE, PKG_ARCH, PKG_FEDORA, PKG_KINDS };

/* What an action opens: the programs that do it, in the order they are
 * preferred; a package that brings the first of them that is packaged, by
 * the name it goes by in each kind of system -- where none is given, the
 * one Debian and Ubuntu give it now, and where it is "", nothing packaged
 * there does it; and what of the compositor's, if anything, all of them
 * need (a Wayland global's name). */
struct opens {
	const struct prog *progs;
	const char *pkg[PKG_KINDS];
	const char *needs;
};

/* What is on the form, in order: the key in shell.conf, what is printed,
 * for a choice its words and the values they are written as, and for an
 * action what it opens. */
enum { S_SECTION, S_CHECK, S_CHOICE, S_DATE, S_ACTION, S_PREVIEW };
struct setting {
	int kind;
	const char *key;
	const char *label;
	const char *note;
	const char *const *words;
	const char *const *vals;
	int def;
	const struct opens *opens;
};

/* and a sixth, when shell.conf holds a size none of these is -- a display
 * tool (wdisplays, Xfce's Display settings) writes the scale it was given
 * there -- so the form shows the size the desk is at (val_choice) */
static char g_scale_word[16], g_scale_val[16];
static const char *SCALE_WORDS[] = { "Automatic", "100%", "125%", "150%", "200%", NULL, NULL };
static const char *SCALE_VALS[] = { "auto", "1", "1.25", "1.5", "2", NULL };
/* how much is drawn, the most first (shell.conf graphics; desk.h's enum
 * desk_gfx the other way round) */
static const char *const GFX_WORDS[] = { "Ultra", "High", "Medium", "Low", "2D Fallback", NULL };
static const char *const GFX_VALS[] = { "ultra", "high", "medium", "low", "2d" };
#define GFX_2D 4
static const char *const MOTION_WORDS[] = { "Brisk", "Normal", "Gentle", NULL };
static const char *const MOTION_VALS[] = { "brisk", "normal", "gentle" };
static const char *const BOUNCE_WORDS[] = { "Normal", "Low", "None", NULL };
static const char *const BOUNCE_VALS[] = { "normal", "low", "off" };
static const char *const CLOCK_WORDS[] = { "24-hour", "12-hour", NULL };
static const char *const CLOCK_VALS[] = { "24h", "12h" };
/* DEsk Wood Maintenance settings v0.1 */
static const char *const TERM_WAIT_WORDS[] = { "30 sec", "1 min", "2 min", "5 min", NULL };
static const char *const TERM_WAIT_VALS[] = { "30", "60", "120", "300" };
static const char *const TERM_LEVEL_WORDS[] = { "One curious guest", "Small colony", "Call the exterminator", NULL };
static const char *const TERM_LEVEL_VALS[] = { "curious", "colony", "infestation" };
static const char *const SOUND_WORDS[] = { "Off", "Quiet", "Normal", NULL };
static const char *const SOUND_VALS[] = { "off", "quiet", "normal" };
/* each language in its own words, as a language is always offered */
static const char *const LANG_WORDS[] = { "Automatic", "English",
	"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",   /* 日本語 */
	"\xe4\xb8\xad\xe6\x96\x87\xef\xbc\x88\xe7\xae\x80\xe4\xbd\x93\xef\xbc\x89",   /* 中文（简体） */
	"\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4",   /* 한국어 */
	"Espa\xc3\xb1ol", "Fran\xc3\xa7" "ais", "Deutsch", NULL };
static const char *const LANG_VALS[] = { "auto", "en", "ja", "zh", "ko", "es", "fr", "de" };
static const char *const SCREEN_OFF_WORDS[] = { "Never", "1 min", "2 min", "5 min", "10 min",
	"15 min", "30 min", NULL };
static const char *const SCREEN_OFF_VALS[] = { "never", "1", "2", "5", "10", "15", "30" };
static const char *const SPEED_WORDS[] = { "Slow", "Normal", "Fast", NULL };
static const char *const SPEED_VALS[] = { "slow", "normal", "fast" };
/* the layouts most keyboards are; shell.conf takes any that xkb knows */
static const char *const LAYOUT_WORDS[] = { "The machine's", "English (US)", "English (UK)",
	"Japanese", "German", "French", "Spanish", "Italian", "Portuguese", "Portuguese (Brazil)",
	"Russian", "Korean", "Swedish", "Norwegian", "Danish", "Finnish", NULL };
static const char *const LAYOUT_VALS[] = { "system", "us", "gb", "jp", "de", "fr", "es", "it",
	"pt", "br", "ru", "kr", "se", "no", "dk", "fi" };
/* and the second: those, and the scripts a second layout is most often for */
static const char *const SECOND_WORDS[] = { "None", "English (US)", "English (UK)", "Japanese",
	"German", "French", "Spanish", "Italian", "Portuguese", "Portuguese (Brazil)", "Russian",
	"Korean", "Swedish", "Norwegian", "Danish", "Finnish", "Arabic", "Persian", "Hebrew", "Greek",
	"Ukrainian", "Hindi", "Thai", NULL };
static const char *const SECOND_VALS[] = { "none", "us", "gb", "jp", "de", "fr", "es", "it", "pt",
	"br", "ru", "kr", "se", "no", "dk", "fi", "ara", "ir", "il", "gr", "ua", "in", "th" };
static const char *const SWITCH_WORDS[] = { "Alt+Shift", "Super+Space", "Ctrl+Shift", "Caps Lock",
	NULL };
static const char *const SWITCH_VALS[] = { "alt_shift", "super_space", "ctrl_shift", "caps_lock" };

/* The programs the rest of the machine is set in (see the top). */
static const struct prog P_NETWORK[] = { { "nm-connection-editor" }, { NULL } };
static const struct prog P_BLUETOOTH[] = { { "blueman-manager" }, { NULL } };
static const struct prog P_SOUND[] = { { "pavucontrol" }, { "pavucontrol-qt" },
	{ "mate-volume-control" }, { NULL } };
static const struct prog P_DISPLAYS[] = { { "wdisplays" }, { NULL } };
static const struct prog P_PRINTERS[] = { { "system-config-printer" }, { NULL } };
static const struct prog P_LOOK[] = { { "nwg-look" }, { "gnome-tweaks" }, { NULL } };
static const struct prog P_DEFAULTS[] = { { "xfce4-mime-settings" },
	{ "mate-default-applications-properties" }, { NULL } };
static const struct prog P_DISKS[] = { { "gnome-disks" }, { NULL } };
static const struct prog P_INDEX[] = { { "desk-index" }, { NULL } };
static const struct prog P_COLOUR[] = { { "xfce4-color-settings" }, { NULL } };
static const struct prog P_USAGE[] = { { "qdirstat" }, { "baobab" }, { NULL } };
static const struct prog P_INPUT[] = { { "ibus-setup" }, { "fcitx5-config-qt" }, { NULL } };
static const struct prog P_TIME[] = { { "lxqt-admin-time" }, { NULL } };
static const struct prog P_FACE[] = { { "mugshot" }, { NULL } };
static const struct prog P_USERS[] = { { "lxqt-admin-user" }, { NULL } };
static const struct prog P_KEYS[] = { { "seahorse" }, { NULL } };
static const struct prog P_SOFTWARE[] = { { "gpk-application" }, { "gnome-software" }, { NULL } };
static const struct prog P_BACKUP[] = { { "backintime-qt" }, { "deja-dup" }, { NULL } };
static const struct prog P_TASKS[] = { { "xfce4-taskmanager" }, { "gnome-system-monitor" },
	{ NULL } };
static const struct prog P_ABOUT[] = { { "hardinfo2" }, { "cpu-x" }, { NULL } };

/* network-manager-gnome is only a transitional package now, which brings
 * nm-connection-editor and the panel applet */
static const struct opens O_NETWORK = { P_NETWORK,
	{ "nm-connection-editor", "network-manager-gnome" }, NULL };
static const struct opens O_BLUETOOTH = { P_BLUETOOTH, { "blueman" }, NULL };
static const struct opens O_SOUND = { P_SOUND, { "pavucontrol" }, NULL };
/* screens are arranged through the compositor, which has to offer it */
static const struct opens O_DISPLAYS = { P_DISPLAYS, { "wdisplays" }, "zwlr_output_manager_v1" };
static const struct opens O_PRINTERS = { P_PRINTERS, { "system-config-printer" }, NULL };
/* before, there was no nwg-look, and Fedora has none: GNOME's Tweaks writes
 * the same GSettings */
static const struct opens O_LOOK = { P_LOOK, { "nwg-look", "gnome-tweaks", NULL, "gnome-tweaks" },
	NULL };
static const struct opens O_DEFAULTS = { P_DEFAULTS, { "xfce4-settings" }, NULL };
static const struct opens O_DISKS = { P_DISKS, { "gnome-disk-utility" }, NULL };
static const struct opens O_INDEX = { P_INDEX, { NULL }, NULL };
/* the classic-looking ones first (QDirStat, Back In Time); Arch has them
 * only in the AUR, and GNOME's for them in its own repositories */
static const struct opens O_COLOUR = { P_COLOUR, { "xfce4-settings" }, NULL };
static const struct opens O_USAGE = { P_USAGE, { "qdirstat", "qdirstat", "baobab" }, NULL };
/* Fedora keeps IBus's setup apart from IBus */
static const struct opens O_INPUT = { P_INPUT, { "ibus", NULL, NULL, "ibus-setup" }, NULL };
static const struct opens O_TIME = { P_TIME, { "lxqt-admin" }, NULL };
/* Fedora has no Mugshot */
static const struct opens O_FACE = { P_FACE, { "mugshot", NULL, NULL, "" }, NULL };
static const struct opens O_USERS = { P_USERS, { "lxqt-admin" }, NULL };
static const struct opens O_KEYS = { P_KEYS, { "seahorse" }, NULL };
static const struct opens O_SOFTWARE = { P_SOFTWARE, { "gnome-packagekit" }, NULL };
static const struct opens O_BACKUP = { P_BACKUP, { "backintime-qt", "backintime-qt", "deja-dup" },
	NULL };
static const struct opens O_TASKS = { P_TASKS, { "xfce4-taskmanager" }, NULL };
/* hardinfo2 is not in Ubuntu 24.04 */
static const struct opens O_ABOUT = { P_ABOUT, { "hardinfo2", "cpu-x" }, NULL };

static const struct setting SETTINGS[] = {
	{ S_SECTION, NULL, "WINDOWS", NULL, NULL, NULL, 0, NULL },
	{ S_CHECK, "paper_for_all_windows", "Lay every window on paper",
		"Programs that draw their own frame and buttons, such as Firefox, get the shell's "
		"paper too: a margin round them, the turned-down corner, folding, the shadow.",
		NULL, NULL, 1, NULL },
	{ S_CHECK, "throw_closed_windows", "Screw up a closed window and throw it away",
		"Off, a window that is closed simply goes.", NULL, NULL, 1, NULL },
	{ S_CHECK, "corner_on_hover", "Turn a corner up when the pointer rests on it",
		"Off, it turns up only when it is clicked. For windows, letters and binder sheets.",
		NULL, NULL, 1, NULL },
	{ S_SECTION, NULL, "DESK", NULL, NULL, NULL, 0, NULL },
	{ S_CHECK, "termites_enabled", "Wood Maintenance (termites)",
		"Visitors appear on the desk when it is left alone. Visual only.",
		NULL, NULL, 1, NULL },
	{ S_CHOICE, "termites_wait", "Termites start after",
		"Time without activity before the first termite arrives.",
		TERM_WAIT_WORDS, TERM_WAIT_VALS, 1, NULL },
	{ S_CHOICE, "termites_level", "Infestation",
		"Choose one termite, a small colony or a busy infestation.",
		TERM_LEVEL_WORDS, TERM_LEVEL_VALS, 1, NULL },
	{ S_CHECK, "termites_chew_windows", "Nibble open window corners",
		"Pretend to chew paper edges; applications and files stay untouched.",
		NULL, NULL, 1, NULL },
	{ S_CHECK, "termites_chew_folded", "Nibble folded applications",
		"Folded paper packets can be inspected too.", NULL, NULL, 1, NULL },
	{ S_CHECK, "termites_damage_persistent", "Keep termite damage until restored",
		"On by default. Chewed marks stay between idle visits and logins; turn off to "
		"clear them automatically when the insects leave.", NULL, NULL, 1, NULL },
	{ S_PREVIEW, "termites_restore", "Restore Desk",
		"Repair all cosmetic termite damage now, including marks from earlier sessions.",
		NULL, NULL, 1, NULL },
	{ S_PREVIEW, NULL, "Preview Wood Maintenance",
		"Preview runs until the mouse moves, without waiting for idle.",
		NULL, NULL, 1, NULL },
	{ S_CHOICE, "scale", "Size of things on the desk",
		"Automatic follows the screen. Bigger things leave less desk; the room is traced "
		"again for it, which takes a moment.", SCALE_WORDS, SCALE_VALS, 0, NULL },
	{ S_CHOICE, "graphics", "Graphics",
		"How much the desk draws. High is the desk as it is made; Ultra traces the room "
		"finer, and wider on a screen with a GPU to do it; Medium and Low smaller and "
		"plainer. 2D Fallback is for a machine without a GPU, or a virtual machine: no "
		"shadows, nothing fading, blurred or moving, and the room traced once, small and "
		"plain. The room is traced again for it, which takes a moment.",
		GFX_WORDS, GFX_VALS, 1, NULL },
	{ S_CHOICE, "motion", "Motion", "How long everything that moves takes.",
		MOTION_WORDS, MOTION_VALS, 1, NULL },
	{ S_CHOICE, "camera_bounce", "Camera bounce",
		"How far the view swings past what you turn to look at and settles back, and how "
		"the head bobs walking to the front door. Lower it, or turn it off, if it makes you "
		"feel unwell.", BOUNCE_WORDS, BOUNCE_VALS, 0, NULL },
	{ S_CHOICE, "language", "Language",
		"The words on the desk, and the names programs are filed under.",
		LANG_WORDS, LANG_VALS, 0, NULL },
	{ S_CHOICE, "clock", "Clock", "The clock on the desk, and the times on letters.",
		CLOCK_WORDS, CLOCK_VALS, 0, NULL },
	{ S_CHECK, "lights_at_login", "Bring the lamp up slowly at login",
		"The room starts dark and the lamp comes up as it is made; the clock counts how far. "
		"Off, it is lit at once.", NULL, NULL, 1, NULL },
	{ S_SECTION, NULL, "SCREEN", NULL, NULL, NULL, 0, NULL },
	{ S_CHOICE, "screen_off", "Turn the screen off when the desk is left",
		"After this long without a key or the pointer, the screen goes dark; either brings "
		"it back.", SCREEN_OFF_WORDS, SCREEN_OFF_VALS, 4, NULL },
	{ S_CHECK, "lock_on_screen_off", "Lock the desk when the screen goes off",
		"The desk is then behind the front door, and your password lets you back in. Off, a "
		"key or the pointer is enough.", NULL, NULL, 1, NULL },
	{ S_SECTION, NULL, "KEYBOARD AND POINTER", NULL, NULL, NULL, 0, NULL },
	{ S_CHECK, "tap_to_click", "Tap the touchpad to click",
		"Off, only its buttons click.", NULL, NULL, 1, NULL },
	{ S_CHECK, "natural_scrolling", "Natural scrolling on the touchpad",
		"The page goes the way the fingers do, as on a phone.", NULL, NULL, 0, NULL },
	{ S_CHOICE, "pointer_speed", "Pointer speed", "For mice and touchpads alike.",
		SPEED_WORDS, SPEED_VALS, 1, NULL },
	{ S_CHECK, "left_handed", "Buttons for the left hand",
		"The main button on the right, the other on the left.", NULL, NULL, 0, NULL },
	{ S_CHOICE, "keyboard_layout", "Keyboard layout",
		"The machine's is the one set for the whole computer. Any other layout xkb knows "
		"can be written into shell.conf.", LAYOUT_WORDS, LAYOUT_VALS, 0, NULL },
	{ S_CHOICE, "keyboard_second", "Second layout",
		"Changed to and back with the keys below.", SECOND_WORDS, SECOND_VALS, 0, NULL },
	{ S_CHOICE, "keyboard_switch", "Change layouts with", NULL, SWITCH_WORDS, SWITCH_VALS, 0,
		NULL },
	{ S_CHOICE, "key_repeat", "Key repeat",
		"How soon a key held down starts repeating, and how fast.", SPEED_WORDS, SPEED_VALS, 1,
		NULL },
	{ S_SECTION, NULL, "DAYS AND WEATHER", NULL, NULL, NULL, 0, NULL },
	/* a date, and nothing said of what comes of it: the days the desk keeps
	 * (this one, Christmas, the New Year) are left to be found */
	{ S_DATE, "birthday", "Your birthday", NULL, NULL, NULL, 0, NULL },
	{ S_CHECK, "weather", "The real weather at the front door",
		"Rain, snow and fog at the front door as they are where you live: the city your time "
		"zone is named after is looked up on open-meteo.com every half hour, and nothing else "
		"is sent. Off, it is always fine.", NULL, NULL, 1, NULL },
	{ S_SECTION, NULL, "DRAWER AND ALL APPS", NULL, NULL, NULL, 0, NULL },
	{ S_CHECK, "drawer_closes", "Put the drawer back after something in it is used",
		"Off, it stays out until it is pushed back in.", NULL, NULL, 1, NULL },
	{ S_CHECK, "fold_into_drawer", "Fold windows away into the drawer",
		"Off, a folded window is put down along the front of the desk.", NULL, NULL, 0, NULL },
	{ S_CHECK, "take_sheet", "Carry a program's sheet down to the desk",
		"A program taken out of ALL APPS has its sheet put down where its window will be, "
		"and the window comes onto it.", NULL, NULL, 1, NULL },
	{ S_SECTION, NULL, "SOUND", NULL, NULL, NULL, 0, NULL },
	{ S_CHOICE, "sound", "Sounds", "Paper, the drawer, the post box, keys and switches: "
		"what each thing on the desk sounds like.", SOUND_WORDS, SOUND_VALS, 2, NULL },
	{ S_CHECK, "letter_sound", "Hear a letter arrive",
		"A letter coming through the slot of the post box.", NULL, NULL, 1, NULL },
	{ S_SECTION, NULL, "NETWORK AND DEVICES",
		"Volume, brightness, the wireless and the lock are on the Control Panel. The rest of "
		"the machine is set in programs made for it, each of which opens as a window on the "
		"desk.", NULL, NULL, 0, NULL },
	{ S_ACTION, NULL, "Network connections",
		"Wired, Wi-Fi and VPN connections, and their passwords.", NULL, NULL, 0, &O_NETWORK },
	{ S_ACTION, NULL, "Bluetooth devices",
		"Pairing headphones, keyboards and phones. The radio itself is switched on the "
		"Control Panel.", NULL, NULL, 0, &O_BLUETOOTH },
	{ S_ACTION, NULL, "Sound devices",
		"Which speakers and microphone, and how loud each program is. The volume of "
		"everything is on the Control Panel.", NULL, NULL, 0, &O_SOUND },
	{ S_ACTION, NULL, "Displays",
		"Where each screen stands, its resolution and its refresh rate.", NULL, NULL, 0,
		&O_DISPLAYS },
	{ S_ACTION, NULL, "Colour profiles", "How each screen and printer shows colour.", NULL,
		NULL, 0, &O_COLOUR },
	{ S_ACTION, NULL, "Printers", "Adding a printer, and what is waiting to be printed.",
		NULL, NULL, 0, &O_PRINTERS },
	{ S_ACTION, NULL, "Disks", "Formatting, partitioning and checking disks and memory sticks.",
		NULL, NULL, 0, &O_DISKS },
	{ S_ACTION, NULL, "Disk usage", "What is taking up the room on a disk, folder by folder.",
		NULL, NULL, 0, &O_USAGE },
	{ S_SECTION, NULL, "LOOK AND LANGUAGE", NULL, NULL, NULL, 0, NULL },
	{ S_ACTION, NULL, "Look of other programs",
		"The theme, icons and fonts of other programs' windows. The desk keeps its own look.",
		NULL, NULL, 0, &O_LOOK },
	{ S_ACTION, NULL, "Input method",
		"Typing Japanese, Chinese, Korean and the like: which converters, and the keys that "
		"turn them on.", NULL, NULL, 0, &O_INPUT },
	{ S_ACTION, NULL, "Date and time",
		"The time zone, and setting the clock by the network.", NULL, NULL, 0, &O_TIME },
	{ S_ACTION, NULL, "Default programs",
		"Which program opens web links, mail and each kind of file. The drawer's web "
		"browser, file manager and text editor are the ones chosen here.", NULL, NULL, 0,
		&O_DEFAULTS },
	{ S_SECTION, NULL, "THIS COMPUTER", NULL, NULL, NULL, 0, NULL },
	{ S_ACTION, NULL, "Your name and picture",
		"As the sign on the front door shows them, and the login screen.", NULL, NULL, 0,
		&O_FACE },
	{ S_ACTION, NULL, "Users", "Who else can log in to this computer.", NULL, NULL, 0,
		&O_USERS },
	{ S_ACTION, NULL, "Passwords and keys",
		"The passwords programs keep for you, and your SSH and PGP keys.", NULL, NULL, 0,
		&O_KEYS },
	{ S_ACTION, NULL, "Adding and removing programs",
		"Finding programs to put on the shelves, and updating the ones there.", NULL, NULL, 0,
		&O_SOFTWARE },
	{ S_ACTION, NULL, "Backups", "Copies of your files, kept on another disk now and then.",
		NULL, NULL, 0, &O_BACKUP },
	{ S_ACTION, NULL, "What the computer is doing",
		"The programs running, and what each is using.", NULL, NULL, 0, &O_TASKS },
	{ S_ACTION, NULL, "About this computer",
		"What the computer is made of, part by part.", NULL, NULL, 0, &O_ABOUT },
	{ S_ACTION, NULL, "DEsk Experience Index",
		"How this machine does at what the desk asks of it: the processor, the memory, the "
		"graphics and the disk.", NULL, NULL, 0, &O_INDEX },
};
#define N_SETTINGS ((int)(sizeof SETTINGS / sizeof SETTINGS[0]))

struct buffer {
	struct wl_buffer *wl;
	cairo_surface_t *surface;
	void *data;
	size_t size;
	int w, h;
	bool busy;
};

struct app {
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct wl_compositor *comp;
	struct wl_shm *shm;
	struct xdg_wm_base *wm;
	struct zxdg_decoration_manager_v1 *decoman;
	struct wl_seat *seat;
	struct wl_pointer *ptr;
	struct wl_keyboard *kbd;
	struct wl_surface *surf;
	struct xdg_surface *xsurf;
	struct xdg_toplevel *top;
	struct zxdg_toplevel_decoration_v1 *deco;
#ifdef DESK_HAVE_WL_CURSOR
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surf;
#endif
	struct buffer buf[2];
	int w, h;
	int pending_w, pending_h;
	int pending_mode;         /* 0 its own size, 1 maximised or full screen, 2 resized */
	int own_w, own_h;         /* its own size: the form's, or as the hand last left it */
	bool configured_once, closed, dirty;
	double px, py;
	int pressed, pressed_opt;
	struct desk_form_row rows[N_SETTINGS];
	const char *opt[N_SETTINGS][DESK_FORM_OPTS];   /* a choice's words, in the language */
	int found[N_SETTINGS];            /* an action: which of its programs is there, or -1 */
	bool offered[N_SETTINGS];         /* and the compositor has what they need */
	char aside[N_SETTINGS][96];       /* what is written when there is nothing to open */
	char named[N_SETTINGS][192];      /* an action's label, with what it opens by name */
	int opening;                      /* the action just used, until its window is up */
	struct timespec opened_at;
	struct desk_shell_form form;   /* the sheet on top: its part's rows */
	double total;            /* the form's height, all of it */
	int tab;                 /* the pad in front: 0 the shell's, 1 the machine's */
	int page;                /* its sheet on top */
	int npages;              /* a sheet to each part of the form, the pads one after the other */
	int pg_first[PAGES_MAX], pg_n[PAGES_MAX], pg_tab[PAGES_MAX];   /* its rows; its pad */
	double pg_scroll[PAGES_MAX];   /* how far each was slid, kept while it is not on top */
	int tab_pg0[2], tab_npg[2];    /* each pad's sheets */
	int tab_open[2];               /* and the one each is open at */
	double tab_x[2], tab_w[2];    /* where the tabs were drawn, for the hand */
	int tab_hover, tab_pressed;   /* -1: none */
	int note_hover, note_pressed; /* the sheet whose note is under the hand; -1: none */
	bool flip_on;            /* sheets going over the wire */
	struct timespec flip_at;
	double flip_s;           /* how long they take */
	int flip_from, flip_to;
	int flip_w, flip_h;      /* the window's size they were printed for */
	cairo_surface_t *flip_img[PAGES_MAX];   /* the sheets turned, and the one under, printed */
	bool anim_on;            /* a pad coming up out of the folder */
	struct timespec anim_at;
	double anim_wait;        /* how long after anim_at it starts */
	cairo_surface_t *anim_old, *anim_new;   /* the pads as they were, and are */
	bool icons_done;         /* the programs' icons looked up */
	uint32_t mods;           /* the modifier keys held */
	int date_row;            /* a date being typed in, digit by digit; -1: none */
	char date_typed[5];
	double wheel;            /* the wheel turned over a date's word, not yet a step */
	char path[512];
	struct timespec seen;    /* the file as it was last read */
	off_t seen_size;
	bool seen_there;
};

/* ---- shell.conf ---- */

static void conf_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/shell.conf", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/shell.conf", home);
	} else {
		out[0] = '\0';
	}
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'
			|| s[n - 1] == '\r')) {
		s[--n] = '\0';
	}
	return s;
}

/* Is this line "key = ..."? Then where its value starts in it. */
static bool line_is(const char *line, const char *key, char *val, size_t n)
{
	char buf[512];
	snprintf(buf, sizeof buf, "%s", line);
	char *s = trim(buf);
	if (*s == '#') {
		return false;
	}
	char *eq = strchr(s, '=');
	if (!eq) {
		return false;
	}
	*eq = '\0';
	if (strcmp(trim(s), key) != 0) {
		return false;
	}
	if (val) {
		snprintf(val, n, "%s", trim(eq + 1));
	}
	return true;
}

/* A date as a row holds it: month * 32 + day, 0 for none. The month and the
 * day only: the year a birthday is in is nobody's business, and is not
 * asked for. */
#define DATE_M(v) ((v) / 32)
#define DATE_D(v) ((v) % 32)
/* shell.conf had a year in it (from a Shell Settings that asked for one):
 * taken out again as it is read (main) */
static bool g_date_had_year;

/* MM-DD, as shell.conf has it -- or YYYY-MM-DD, its year let go */
static int date_read(const char *val)
{
	int y = 0, m = 0, d = 0;
	if (sscanf(val, "%d-%d-%d", &y, &m, &d) == 3 && y >= 1000) {
		g_date_had_year = true;
	} else if (sscanf(val, "%d-%d", &m, &d) != 2) {
		return 0;
	}
	return m >= 1 && m <= 12 && d >= 1 && d <= 31 ? m * 32 + d : 0;
}

static bool val_yes(const char *v, bool def)
{
	if (!strcasecmp(v, "no") || !strcasecmp(v, "false") || !strcasecmp(v, "off")
		|| !strcmp(v, "0")) {
		return false;
	}
	if (!strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on")
		|| !strcmp(v, "1")) {
		return true;
	}
	return def;
}

/* Which of a choice's words a value is: -1 for one not on the form (a
 * scale of 1.75, written by hand), which leaves none of them ringed. */
static int val_choice(const struct setting *st, const char *v)
{
	for (int k = 0; st->words[k]; k++) {
		if (!strcasecmp(v, st->vals[k])) {
			return k;
		}
	}
	if (!strcmp(st->key, "scale")) {
		char *end = NULL;
		double x = strtod(v, &end);
		if (end != v && x > 0.0) {
			x = end && *end == '%' ? x / 100.0 : x;
			int k = 1;
			for (; st->words[k]; k++) {
				if (fabs(atof(st->vals[k]) - x) < 1e-3) {
					return k;
				}
			}
			if (k == 5 && x >= 0.5 && x <= 4.0) {
				snprintf(g_scale_word, sizeof g_scale_word, "%.0f%%", x * 100.0);
				snprintf(g_scale_val, sizeof g_scale_val, "%g", x);
				SCALE_WORDS[5] = g_scale_word;
				SCALE_VALS[5] = g_scale_val;
				return 5;
			}
		}
	}
	if (!strcmp(st->key, "clock") && !strcmp(v, "12")) {
		return 1;
	}
	if (!strcmp(st->key, "clock") && !strcmp(v, "24")) {
		return 0;
	}
	return -1;
}

/* Every value as the file has it, defaults for what it has not. (An
 * action's is whether it has something to open: actions_find.) */
static void conf_read_file(const char *path, struct desk_form_row *rows);

/* Whether the user's own file says key, as the desk reads it (line_is). */
static bool conf_has_key(const char *path, const char *key)
{
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return false;
	}
	char line[512];
	bool has = false;
	while (!has && fgets(line, sizeof line, f)) {
		has = line_is(line, key, NULL, 0);
	}
	fclose(f);
	return has;
}

/* What the desk that is running says of how the screen is drawn
 * ($XDG_RUNTIME_DIR/<its Wayland display>.render, wm.c render_publish):
 * on the CPU -- pixman, or a GL that is a rasterizer on the processor --
 * and by what. Read with the settings, so a desk started since is heard. */
static bool g_on_cpu;
static char g_render_name[256];
static char g_cpu_warn[512];

static void render_read(void)
{
	g_on_cpu = false;
	g_render_name[0] = '\0';
	const char *run = getenv("XDG_RUNTIME_DIR"), *wd = getenv("WAYLAND_DISPLAY");
	char path[512];
	if (!run || !*run || !wd || !*wd || strchr(wd, '/')
		|| (size_t)snprintf(path, sizeof path, "%s/%s.render", run, wd) >= sizeof path) {
		return;
	}
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	char line[512], val[256];
	while (fgets(line, sizeof line, f)) {
		if (line_is(line, "on_cpu", val, sizeof val)) {
			g_on_cpu = !strcmp(val, "yes");
		} else if (line_is(line, "renderer", val, sizeof val)) {
			snprintf(g_render_name, sizeof g_render_name, "%s", val);
		}
	}
	fclose(f);
}

/* The settings as the desk has them: its own defaults, then the machine's
 * (desk/shell.conf in the first of $XDG_CONFIG_DIRS that has one, where the
 * installer puts the graphics it chose), then the user's own file. And on
 * the CPU, Graphics is 2D Fallback unless the user's own file chose -- as
 * the desk has it (wm.c gfx_wanted). */
static void conf_read(const char *path, struct desk_form_row *rows)
{
	render_read();
	for (int i = 0; i < N_SETTINGS; i++) {
		if (SETTINGS[i].key) {
			rows[i].value = SETTINGS[i].def;
		}
	}
	const char *dirs = getenv("XDG_CONFIG_DIRS");
	if (!dirs || !*dirs) {
		dirs = "/etc/xdg";
	}
	while (*dirs) {
		size_t len = strcspn(dirs, ":");
		char sys[512];
		if (len > 0 && (size_t)snprintf(sys, sizeof sys, "%.*s/desk/shell.conf", (int)len,
			dirs) < sizeof sys && access(sys, R_OK) == 0) {
			conf_read_file(sys, rows);
			break;
		}
		dirs += len + (dirs[len] == ':');
	}
	if (path[0]) {
		conf_read_file(path, rows);
	}
	if (g_on_cpu && !conf_has_key(path, "graphics")) {
		for (int i = 0; i < N_SETTINGS; i++) {
			if (SETTINGS[i].key && !strcmp(SETTINGS[i].key, "graphics")) {
				rows[i].value = GFX_2D;
			}
		}
	}
}

static void conf_read_file(const char *path, struct desk_form_row *rows)
{
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	char line[512], val[256];
	while (fgets(line, sizeof line, f)) {
		for (int i = 0; i < N_SETTINGS; i++) {
			const struct setting *st = &SETTINGS[i];
			if (!st->key || !line_is(line, st->key, val, sizeof val)) {
				continue;
			}
			if (st->kind == S_DATE) {
				rows[i].value = date_read(val);
				continue;
			}
			/* Restore Desk is a one-shot command, not a choice. */
			if (st->kind == S_PREVIEW) continue;
			rows[i].value = st->kind == S_CHECK ? val_yes(val, st->def) : val_choice(st, val);
		}
	}
	fclose(f);
}

/* A date's words, as the form lays them out (struct desk_form_row): the
 * month and the day in the language, each between the marks to go back and
 * on, and none. typed: the digits being typed in, if they are (MMDD so
 * far), shown as far as they go. */
static char g_date_words[3][40];

static void date_words(int value, const char *typed, const char *opt[DESK_FORM_OPTS])
{
	static const char *const MONTH[12] = { "January", "February", "March", "April", "May",
		"June", "July", "August", "September", "October", "November", "December" };
	int m = DATE_M(value), d = DATE_D(value);
	if (typed) {
		char pad[5] = "____";
		memcpy(pad, typed, strnlen(typed, 4));
		snprintf(g_date_words[0], sizeof g_date_words[0], "%.2s", pad);
		snprintf(g_date_words[1], sizeof g_date_words[1], "%.2s", pad + 2);
	} else if (m >= 1 && m <= 12 && d >= 1) {
		snprintf(g_date_words[0], sizeof g_date_words[0], "%s", TR(MONTH[m - 1]));
		snprintf(g_date_words[1], sizeof g_date_words[1], TR("day|%d"), d);
	} else {
		snprintf(g_date_words[0], sizeof g_date_words[0], "%s", TR("month|\xe2\x80\x94"));
		snprintf(g_date_words[1], sizeof g_date_words[1], "%s", TR("day|\xe2\x80\x94"));
	}
	snprintf(g_date_words[2], sizeof g_date_words[2], "%s", TR("Not set"));
	for (int k = 0; k < 2; k++) {
		opt[3 * k] = "\xe2\x80\xb9";
		opt[3 * k + 1] = g_date_words[k];
		opt[3 * k + 2] = "\xe2\x80\xba";
	}
	opt[6] = g_date_words[2];
	opt[7] = NULL;
}

/* how many days a month has, the 29th of February counted */
static int month_days(int m)
{
	return m == 2 ? 29 : m == 4 || m == 6 || m == 9 || m == 11 ? 30 : 31;
}

/* Which names packages go by here (PKG_*). */
static int g_pkg = PKG_NOW;

/* The Ubuntu releases from before the names of now. Ubuntu, and what is made
 * from it, says which it is or is made from in UBUNTU_CODENAME -- Linux
 * Mint's numbers are its own. Those between 24.04 and 26.04 are counted in:
 * the names from before install there too. */
static const char *const UBUNTU_BEFORE[] = { "bionic", "focal", "jammy", "noble", "oracular",
	"plucky", "questing", NULL };

/* The Debian releases from before the names of now. What is made from
 * Debian is numbered its own way -- LMDE 7 and Devuan 6 are Debian 13 --
 * and may say which Debian it is made from in DEBIAN_CODENAME (LMDE does). */
static const char *const DEBIAN_BEFORE[] = { "stretch", "buster", "bullseye", "bookworm", NULL };

/* Whether name is one of names (NULL at the end). */
static bool named_in(const char *const *names, const char *name)
{
	for (int k = 0; names[k]; k++) {
		if (!strcmp(name, names[k])) {
			return true;
		}
	}
	return false;
}

/* An os-release line's value, if the line is key's: KEY=value or KEY="value". */
static bool os_value(const char *line, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);
	if (strncmp(line, key, k) != 0 || line[k] != '=') {
		return false;
	}
	const char *v = line + k + 1;
	v += *v == '"' || *v == '\'';
	snprintf(out, n, "%.*s", (int)strcspn(v, "\"'\r\n"), v);
	return true;
}

/* Which names packages go by, as os-release f says: Arch's for Arch and
 * what is made from it, Fedora's for Fedora and what is made from it (which
 * says so in ID_LIKE); for Ubuntu and what is made from it, by the Ubuntu
 * it is; for what else is made from Debian, by the Debian it says it is
 * made from; for Debian itself (and Raspberry Pi OS, numbered as Debian is),
 * by VERSION_ID, which its testing and unstable do not have; the names of now
 * for the rest. */
static int pkg_kind(FILE *f)
{
	char line[512], id[64] = "", like[128] = "", ver[32] = "", ubuntu[32] = "", debian[32] = "";
	while (fgets(line, sizeof line, f)) {
		if (!os_value(line, "ID", id, sizeof id)
			&& !os_value(line, "ID_LIKE", like, sizeof like)
			&& !os_value(line, "VERSION_ID", ver, sizeof ver)
			&& !os_value(line, "UBUNTU_CODENAME", ubuntu, sizeof ubuntu)) {
			os_value(line, "DEBIAN_CODENAME", debian, sizeof debian);
		}
	}
	if (strstr(id, "arch") || strstr(like, "arch")) {
		return PKG_ARCH;
	}
	if (!strcmp(id, "fedora") || strstr(like, "fedora")) {
		return PKG_FEDORA;
	}
	if (ubuntu[0]) {
		return named_in(UBUNTU_BEFORE, ubuntu) ? PKG_BEFORE : PKG_NOW;
	}
	if (debian[0]) {
		return named_in(DEBIAN_BEFORE, debian) ? PKG_BEFORE : PKG_NOW;
	}
	if ((!strcmp(id, "debian") || !strcmp(id, "raspbian")) && ver[0] && atoi(ver) < 13) {
		return PKG_BEFORE;
	}
	return PKG_NOW;
}

static void os_read(void)
{
	FILE *f = fopen("/etc/os-release", "re");
	if (!f) {
		f = fopen("/usr/lib/os-release", "re");
	}
	if (!f) {
		return;
	}
	g_pkg = pkg_kind(f);
	fclose(f);
}

/* An action's words: its label with the program it opens -- or, none of
 * them there, the one it would -- by name; "Open ›", "Opening…" while
 * what it opens is on its way, and what is written instead when there is
 * nothing to open. */
static void action_words(struct app *a, int i)
{
	const struct opens *o = SETTINGS[i].opens;
	const char *pkg = o->pkg[g_pkg] ? o->pkg[g_pkg] : o->pkg[PKG_NOW];
	const struct prog *p = &o->progs[a->found[i] >= 0 ? a->found[i] : 0];
	snprintf(a->named[i], sizeof a->named[i], TR("label|%s (%s)"), TR(SETTINGS[i].label),
		p->bin);
	a->rows[i].label = a->named[i];
	if (o->needs && !a->offered[i]) {
		snprintf(a->aside[i], sizeof a->aside[i], "%s", TR("DEsk cannot do this yet"));
	} else if (pkg && *pkg) {
		snprintf(a->aside[i], sizeof a->aside[i], TR("not installed \xe2\x80\x94 e.g. %s"), pkg);
	} else {
		snprintf(a->aside[i], sizeof a->aside[i], "%s", TR("not installed"));
	}
	a->opt[i][0] = TR(a->opening == i ? "Opening\xe2\x80\xa6" : "Open \xe2\x80\xba");
	a->opt[i][1] = a->aside[i];
	a->opt[i][2] = NULL;
	a->rows[i].options = a->opt[i];
}

/* The form in the language it says: what the language row is set to,
 * and every word on the form put into it. */
static void words_apply(struct app *a)
{
	struct desk_form_row *rows = a->rows;
	for (int i = 0; i < N_SETTINGS; i++) {
		const struct setting *st = &SETTINGS[i];
		if (st->key && !strcmp(st->key, "language")) {
			int v = rows[i].value;
			desk_lang_set(v > 0 ? desk_lang_parse(LANG_VALS[v]) : desk_lang_from_locale());
		}
	}
	for (int i = 0; i < N_SETTINGS; i++) {
		const struct setting *st = &SETTINGS[i];
		rows[i].label = TR(st->label);
		rows[i].note = st->note ? TR(st->note) : NULL;
		rows[i].warn = NULL;
		if (g_on_cpu && st->key && !strcmp(st->key, "graphics")) {
			/* said where the level is chosen, as the letter says it */
			snprintf(g_cpu_warn, sizeof g_cpu_warn, "%s%s%s%s",
				TR("Running on the CPU now: check the graphics drivers, or in a virtual "
				"machine its settings. Until then the desk draws in 2D Fallback."),
				g_render_name[0] ? " (" : "", g_render_name, g_render_name[0] ? ")" : "");
			rows[i].warn = g_cpu_warn;
		}
		if (st->words) {
			int n = 0;
			for (; st->words[n] && n < DESK_FORM_OPTS - 1; n++) {
				a->opt[i][n] = TR(st->words[n]);
			}
			a->opt[i][n] = NULL;
			rows[i].options = a->opt[i];
		}
		if (st->kind == S_PREVIEW) {
			a->opt[i][0] = TR(st->key ? "Restore now ›" : "Preview now ›");
			a->opt[i][1] = NULL;
			rows[i].options = a->opt[i];
		}
		if (st->kind == S_DATE) {
			date_words(rows[i].value, a->date_row == i ? a->date_typed : NULL, a->opt[i]);
			rows[i].options = a->opt[i];
		}
		if (st->kind == S_ACTION) {
			action_words(a, i);
		}
	}
	if (a->top) {
		xdg_toplevel_set_title(a->top, TR("Shell Settings"));
	}
}

static void mkdirs_for(const char *path)
{
	char dir[512];
	snprintf(dir, sizeof dir, "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
}

/* The one line changed, or added; everything else as it was. */
static bool conf_write(const char *path, const char *key, const char *value)
{
	if (!path[0]) {
		return false;
	}
	mkdirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *in = fopen(path, "re");
	FILE *out = fopen(tmp, "we");
	if (!out) {
		if (in) {
			fclose(in);
		}
		fprintf(stderr, "desk-settings: cannot write %s: %s\n", tmp, strerror(errno));
		return false;
	}
	bool done = false;
	if (in) {
		char line[512];
		while (fgets(line, sizeof line, in)) {
			if (line_is(line, key, NULL, 0)) {
				if (!done) {
					fprintf(out, "%s = %s\n", key, value);
					done = true;
				}
				continue;
			}
			fputs(line, out);
		}
		fclose(in);
	} else {
		fputs("# DEsk's shell settings. Shell Settings writes this; so may you.\n"
			"#\n"
			"#   paper_for_all_windows = yes | no   programs with frames of their own on paper\n"
			"#   paper.<program> = yes | no         one program the other way (desktop id,\n"
			"#                                      app id or X class)\n"
			"#   throw_closed_windows = yes | no    closed windows screwed up and thrown away\n"
			"#   corner_on_hover = yes | no         corners turn up when the pointer rests\n"
			"#   scale = auto | 1 | 1.25 | 1.5 | 2  how big things on the desk are\n"
			"#   graphics = ultra | high | medium | low | 2d\n"
			"#                                      how much the desk draws (2d: no shadows,\n"
			"#                                      nothing fading or moving; for no GPU)\n"
			"#   motion = brisk | normal | gentle   how long things that move take\n"
			"#   camera_bounce = normal | low | off the view swinging past and settling\n"
			"#   clock = 24h | 12h\n"
			"#   language = auto | en | ja | zh | ko | es | fr | de\n"
			"#                                      the desk's words; auto: the locale's\n"
			"#   lights_at_login = yes | no         the room starts dark, the lamp comes up\n"
			"#   screen_off = never | 1 | 2 | 5 | 10 | 15 | 30\n"
			"#                                      minutes left alone before it goes dark\n"
			"#   lock_on_screen_off = yes | no      and the desk locks when it does\n"
			"#   tap_to_click = yes | no            a touchpad: a tap is a click\n"
			"#   natural_scrolling = yes | no       a touchpad: the page follows the fingers\n"
			"#   pointer_speed = slow | normal | fast\n"
			"#   left_handed = yes | no             the buttons the other way round\n"
			"#   keyboard_layout = system | <xkb>   us, jp, de...; system: the machine's\n"
			"#   keyboard_second = none | <xkb>     a second layout to change to\n"
			"#   keyboard_switch = alt_shift | super_space | ctrl_shift | caps_lock\n"
			"#   key_repeat = slow | normal | fast\n"
			"#   birthday = MM-DD                   your birthday (no year)\n"
			"#   weather = yes | no                 the real weather at the front door\n"
			"#   drawer_closes = yes | no           the drawer goes back after use\n"
			"#   fold_into_drawer = yes | no        folded windows go into the drawer\n"
			"#   take_sheet = yes | no              ALL APPS carries the sheet down\n"
			"#   sound = off | quiet | normal       the sounds of things on the desk\n"
			"#   letter_sound = yes | no            a letter heard arriving\n", out);
	}
	if (!done) {
		fprintf(out, "%s = %s\n", key, value);
	}
	bool ok = fflush(out) == 0 && !ferror(out);
	ok = fclose(out) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		fprintf(stderr, "desk-settings: cannot write %s\n", path);
		unlink(tmp);
		return false;
	}
	return true;
}

/* Read again if the file has changed since it was last read -- by hand,
 * or by another copy of this. */
static bool conf_changed(const char *path, struct timespec *seen, off_t *size, bool *there)
{
	struct stat st;
	bool now = path[0] && stat(path, &st) == 0;
	if (now == *there && (!now || (st.st_mtim.tv_sec == seen->tv_sec
			&& st.st_mtim.tv_nsec == seen->tv_nsec && st.st_size == *size))) {
		return false;
	}
	*there = now;
	if (now) {
		*seen = st.st_mtim;
		*size = st.st_size;
	}
	return true;
}

/* ---- the rest of the machine ---- */

/* the directory this program is in: a DEsk run from where it was built
 * keeps its own programs there, desk-index among them, off the PATH */
static char g_exe_dir[512];

static void exe_dir_read(void)
{
	ssize_t k = readlink("/proc/self/exe", g_exe_dir, sizeof g_exe_dir - 1);
	g_exe_dir[k > 0 ? k : 0] = '\0';
	char *slash = strrchr(g_exe_dir, '/');
	if (slash) {
		*slash = '\0';
	} else {
		g_exe_dir[0] = '\0';
	}
}

static bool is_program(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

/* Where a program is: beside this one, then along PATH. */
static bool find_program(const char *name, char *out, size_t n)
{
	if (g_exe_dir[0] && snprintf(out, n, "%s/%s", g_exe_dir, name) < (int)n
		&& is_program(out)) {
		return true;
	}
	const char *path = getenv("PATH");
	for (const char *p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin"; *p;) {
		const char *e = strchr(p, ':');
		int len = e ? (int)(e - p) : (int)strlen(p);
		if (len > 0 && snprintf(out, n, "%.*s/%s", len, p, name) < (int)n && is_program(out)) {
			return true;
		}
		p += len + (e ? 1 : 0);
	}
	out[0] = '\0';
	return false;
}

/* Which of each action's programs is there to open, and whether that is
 * now different: programs are installed and taken away while the form is
 * up. A program that needs another beside it counts only with both, and
 * none does without what the compositor has to offer for it. */
static bool actions_find(struct app *a)
{
	bool changed = false;
	char path[600];
	for (int i = 0; i < N_SETTINGS; i++) {
		const struct opens *o = SETTINGS[i].opens;
		if (SETTINGS[i].kind != S_ACTION || !o) {
			continue;
		}
		int found = -1;
		for (int k = 0; o->progs[k].bin && found < 0 && (!o->needs || a->offered[i]); k++) {
			const struct prog *p = &o->progs[k];
			if (find_program(p->bin, path, sizeof path)) {
				found = k;
			}
		}
		changed = changed || found != a->found[i];
		a->found[i] = found;
		a->rows[i].value = found >= 0;
	}
	return changed;
}

/* ---- the icons of what is opened ---- */

/* Of a desktop entry's Exec, the program: its first word, past an "env" and
 * its settings, without its directory. */
static void exec_program(const char *exec, char *out, size_t n)
{
	char word[256];
	const char *p = exec;
	out[0] = '\0';
	for (;;) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		size_t k = 0;
		while (*p && *p != ' ' && *p != '\t' && k + 1 < sizeof word) {
			word[k++] = *p == '"' ? (p++, *p ? *p : '"') : *p;
			if (*p) {
				p++;
			}
		}
		word[k] = '\0';
		if (!k) {
			return;
		}
		if (!strcmp(word, "env") || strchr(word, '=')) {
			continue;
		}
		const char *base = strrchr(word, '/');
		if (snprintf(out, n, "%s", base ? base + 1 : word) >= (int)n) {
			out[0] = '\0';   /* a name too long to be one of these */
		}
		return;
	}
}

/* In one applications folder: the Icon= of the entry that runs bin. */
static bool icon_name_in(const char *dir, const char *bin, char *out, size_t n)
{
	DIR *d = opendir(dir);
	if (!d) {
		return false;
	}
	bool found = false;
	struct dirent *de;
	while (!found && (de = readdir(d))) {
		size_t l = strlen(de->d_name);
		if (l < 9 || strcmp(de->d_name + l - 8, ".desktop")) {
			continue;
		}
		char path[1024];
		int pl = snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
		FILE *f = pl > 0 && (size_t)pl < sizeof path ? fopen(path, "re") : NULL;
		if (!f) {
			continue;
		}
		char line[1024], icon[256] = "", prog[128] = "";
		bool entry = false;
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (line[0] == '[') {
				if (entry) {
					break;   /* the actions' sections run other things */
				}
				entry = !strcmp(line, "[Desktop Entry]");
			} else if (entry && !strncmp(line, "Icon=", 5)) {
				if (snprintf(icon, sizeof icon, "%s", line + 5) >= (int)sizeof icon) {
					icon[0] = '\0';
				}
			} else if (entry && !strncmp(line, "Exec=", 5) && !prog[0]) {
				exec_program(line + 5, prog, sizeof prog);
			} else if (entry && !strncmp(line, "TryExec=", 8)) {
				exec_program(line + 8, prog, sizeof prog);
			}
		}
		fclose(f);
		if (icon[0] && !strcmp(prog, bin)) {
			snprintf(out, n, "%s", icon);
			found = true;
		}
	}
	closedir(d);
	return found;
}

/* The icon of the program bin, as its desktop entry names it, found where
 * the desk finds the shelves' icons; kept, as the programs are the same
 * few. NULL if it has none. */
static cairo_surface_t *prog_icon(const char *bin)
{
	static struct {
		char bin[64];
		cairo_surface_t *s;
	} kept[48];
	static int n_kept;
	for (int i = 0; i < n_kept; i++) {
		if (!strcmp(kept[i].bin, bin)) {
			return kept[i].s;
		}
	}
	char dirs[2048] = "", name[256] = "";
	const char *home = getenv("HOME");
	const char *dh = getenv("XDG_DATA_HOME");
	const char *dd = getenv("XDG_DATA_DIRS");
	snprintf(dirs, sizeof dirs, "%s%s%s:%s%s%s", dh && *dh ? dh : home ? home : "",
		dh && *dh ? "" : "/.local/share", "", dd && *dd ? dd : "/usr/local/share:/usr/share",
		g_exe_dir[0] ? ":" : "", g_exe_dir);
	for (char *dir = strtok(dirs, ":"); dir && !name[0]; dir = strtok(NULL, ":")) {
		char apps[1024];
		/* the build directory's own entries are in applications/ beside it */
		snprintf(apps, sizeof apps, "%s/applications", dir);
		icon_name_in(apps, bin, name, sizeof name);
	}
	cairo_surface_t *s = NULL;
	char png[1024];
	if (name[0] == '/' ? snprintf(png, sizeof png, "%s", name) > 0
		: desk_icon_path(name, NULL, png, sizeof png)) {
		size_t l = strlen(png);
		if (l > 4 && !strcmp(png + l - 4, ".png")) {
			s = cairo_image_surface_create_from_png(png);
			if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
				cairo_surface_destroy(s);
				s = NULL;
			}
		}
	}
	if (n_kept < (int)(sizeof kept / sizeof kept[0])) {
		snprintf(kept[n_kept].bin, sizeof kept[0].bin, "%s", bin);
		kept[n_kept++].s = s;
	}
	return s;
}

/* Each action's row given the icon of what it would open. */
static void icons_apply(struct app *a)
{
	for (int i = 0; i < N_SETTINGS; i++) {
		const struct opens *o = SETTINGS[i].opens;
		int k = a->found[i];
		a->rows[i].icon = SETTINGS[i].kind == S_ACTION && o && k >= 0 ? prog_icon(o->progs[k].bin)
			: NULL;
	}
}

/* Started and let go of: forked twice, so that it is nobody's child to
 * wait for, in a session of its own, in the home directory, with nothing of
 * this program's -- not the display's socket, not the sound's -- but the
 * environment, which is the session's: DEsk's, as it started this. */
static bool launch(const char *path)
{
	const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	const char *home = getenv("HOME");
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		struct sigaction dfl;
		memset(&dfl, 0, sizeof dfl);
		dfl.sa_handler = SIG_DFL;
		sigaction(SIGPIPE, &dfl, NULL);
		sigaction(SIGCHLD, &dfl, NULL);
		pid_t run = fork();
		if (run != 0) {
			_exit(run < 0 ? 1 : 0);
		}
		int null = open("/dev/null", O_RDONLY);
		if (null > 0) {
			dup2(null, 0);
			close(null);
		}
#ifdef SYS_close_range
		if (syscall(SYS_close_range, 3u, ~0u, 0u) != 0)
#endif
		{
			for (int fd = 3; fd < 1024; fd++) {
				close(fd);
			}
		}
		if (home && *home && chdir(home) != 0) {
			/* where it runs from matters little; it runs */
		}
		execl(path, name, (char *)NULL);
		_exit(127);
	}
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
		;
	}
	return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

/* ---- buffers ---- */

static int shm_file(size_t size)
{
	int fd = -1;
#ifdef MFD_CLOEXEC
	fd = memfd_create("desk-settings", MFD_CLOEXEC);
#endif
	if (fd < 0) {
		char name[64];
		snprintf(name, sizeof name, "/desk-settings-%ld-%ld", (long)getpid(),
			(long)time(NULL));
		fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
		}
	}
	if (fd < 0) {
		return -1;
	}
	if (ftruncate(fd, (off_t)size) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl)
{
	(void)wl;
	struct buffer *b = data;
	b->busy = false;
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static void buffer_free(struct buffer *b)
{
	if (b->wl) {
		wl_buffer_destroy(b->wl);
	}
	if (b->surface) {
		cairo_surface_destroy(b->surface);
	}
	if (b->data) {
		munmap(b->data, b->size);
	}
	memset(b, 0, sizeof *b);
}

static bool buffer_make(struct app *a, struct buffer *b, int w, int h)
{
	buffer_free(b);
	int stride = w * 4;
	size_t size = (size_t)stride * h;
	int fd = shm_file(size);
	if (fd < 0) {
		return false;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return false;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(a->shm, fd, (int32_t)size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	b->data = data;
	b->size = size;
	b->w = w;
	b->h = h;
	b->surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, w, h, stride);
	return true;
}

/* ---- the pads ---- */

/* The desk set to draw in 2D (Graphics): then nothing here moves either --
 * no pad pulled out, no sheet turned over, the other one is simply there --
 * and nothing has a shadow. */
static bool app_flat(const struct app *a)
{
	static int row = -2;
	if (row == -2) {
		row = -1;
		for (int i = 0; i < N_SETTINGS; i++) {
			if (SETTINGS[i].key && !strcmp(SETTINGS[i].key, "graphics")) {
				row = i;
			}
		}
	}
	return row >= 0 && a->rows[row].value == GFX_2D;
}

/* Where the sheets of the pad in front are: hung from the wire at ay, x
 * from the left, w wide and h long; the notes stand out right of x + w. */
struct pad {
	double x, w, ay, h;
};

static void pad_at(const struct app *a, struct pad *p)
{
	p->x = PAD_X;
	p->w = fmax(a->w - PAD_X - NOTE_OUT - 4.0, 240.0);
	p->ay = TAB_H + PAD_WIRE;
	p->h = fmax(a->h - p->ay - PAD_FOOT, 80.0);
}

/* How wide what is printed on a sheet is, and how tall the room for it. */
static double ink_w(const struct app *a)
{
	struct pad p;
	pad_at(a, &p);
	return p.w;
}

static double page_h(const struct app *a)
{
	struct pad p;
	pad_at(a, &p);
	return fmax(p.h - PAD_INK - 6.0, 1.0);
}

static void scroll_clamp(struct app *a);

/* The form on the sheet on top measured again, after its words changed. */
static void form_measure(struct app *a)
{
	a->total = desk_shell_form_height(ink_w(a), &a->form);
	scroll_clamp(a);
}

/* Sheet pg's form as it stands, not under the hand. */
static struct desk_shell_form page_form(const struct app *a, int pg)
{
	struct desk_shell_form f = { 0 };
	f.rows = a->rows + a->pg_first[pg];
	f.n = a->pg_n[pg];
	f.hover = f.hover_opt = -1;
	f.scroll = pg == a->page ? a->form.scroll : a->pg_scroll[pg];
	/* shell.conf, at the foot of the sheets whose settings are kept there */
	f.kept_in = a->pg_tab[pg] == 0 ? a->path : NULL;
	f.on_paper = true;
	return f;
}

/* Sheet pg on top -- of the pad it is in, which is then in front: its rows
 * on the form, slid as far as they were. */
static void page_show(struct app *a, int pg)
{
	a->pg_scroll[a->page] = a->form.scroll;
	a->form = page_form(a, pg);
	a->page = pg;
	a->tab = a->pg_tab[pg];
	a->tab_open[a->tab] = pg;
	a->pressed = a->pressed_opt = -1;
	a->note_hover = a->note_pressed = -1;
	form_measure(a);
}

/* Sheet pg's note: how far down from the wire, by its place in the pad. */
static double note_y(const struct app *a, int pg)
{
	return NOTE_TOP + (pg - a->tab_pg0[a->pg_tab[pg]]) * NOTE_STEP;
}

static const char *note_word(const struct app *a, int pg)
{
	const char *label = SETTINGS[a->pg_first[pg]].label;
	for (size_t i = 0; i < sizeof NOTE_WORDS / sizeof NOTE_WORDS[0]; i++) {
		if (!strcmp(NOTE_WORDS[i][0], label)) {
			return TR(NOTE_WORDS[i][1]);
		}
	}
	return TR(label);
}

/* What is written on sheet pg's note, in its lines (one or two). */
static int note_lines(const struct app *a, int pg, char line[2][96])
{
	const char *word = note_word(a, pg);
	const char *br = strchr(word, '\n');
	if (br && (size_t)(br - word) < 96) {
		snprintf(line[0], 96, "%.*s", (int)(br - word), word);
		snprintf(line[1], 96, "%s", br + 1);
		return 2;
	}
	snprintf(line[0], 96, "%s", word);
	return 1;
}

/* How big the hand on a pad's notes is: the same on all of them, as small
 * as it takes for the longest to fit. */
static double note_size(cairo_t *cr, const struct app *a, int t)
{
	double room = NOTE_OUT - 28.0, size = 14.5;
	cairo_save(cr);
	for (int pg = a->tab_pg0[t]; pg < a->tab_pg0[t] + a->tab_npg[t]; pg++) {
		char line[2][96];
		int nl = note_lines(a, pg, line);
		desk_font_select(cr, DESK_FONT_HAND, line[0], false, false);
		for (;;) {
			cairo_set_font_size(cr, size);
			double widest = 0.0;
			for (int i = 0; i < nl; i++) {
				cairo_text_extents_t te;
				cairo_text_extents(cr, line[i], &te);
				widest = fmax(widest, te.x_advance);
			}
			if (widest <= room || size <= 9.0) {
				break;
			}
			size -= 0.5;
		}
	}
	cairo_restore(cr);
	return size;
}

/* A note's outline: square where it is stuck at x0, the corners of its
 * free end at x1 a little round. */
static void note_path(cairo_t *cr, double x0, double y, double x1, double h)
{
	double r = 3.0;
	cairo_new_path(cr);
	cairo_move_to(cr, x0, y);
	cairo_line_to(cr, x1 - r, y);
	cairo_arc(cr, x1 - r, y + r, r, -M_PI / 2.0, 0.0);
	cairo_line_to(cr, x1, y + h - r);
	cairo_arc(cr, x1 - r, y + h - r, r, 0.0, M_PI / 2.0);
	cairo_line_to(cr, x0, y + h);
	cairo_close_path(cr);
}

/* Sheet pg's sticky note, the sheet's right-hand edge at x and the note's
 * top at y. out: how much further out it is drawn (the hand on it); shade:
 * how much darker, hanging behind the pad; whole: with the part stuck on
 * the sheet, which only the sheet on top shows; alpha: how much of it is
 * seen. Its colour is one of the pads the desk's sticky notes come in. */
static void note_paint(cairo_t *cr, const struct app *a, int pg, double x, double y,
	double out, double shade, bool whole, double alpha)
{
	if (alpha <= 0.002) {
		return;
	}
	int t = a->pg_tab[pg], k = pg - a->tab_pg0[t];
	double r, g, b;
	desk_stick_colour((k + 3 * t) % DESK_STICK_COLOURS, &r, &g, &b);
	double x0 = whole ? x - NOTE_STUCK : x, x1 = x + NOTE_OUT - 14.0 + out;
	cairo_save(cr);
	if (alpha < 1.0) {
		cairo_push_group(cr);
	}
	/* its shadow, on the folder or on the sheet under it */
	if (!app_flat(a)) {
		note_path(cr, x0, y + 1.8, x1 + 1.2, NOTE_H);
		cairo_set_source_rgba(cr, 0.12, 0.08, 0.04, 0.20 * (1.0 - shade));
		cairo_fill(cr);
	}
	/* the paper: where it is stuck the sheet shows through a little; out
	 * in the air it is a shade darker by the edge and lighter at its end */
	note_path(cr, x0, y, x1, NOTE_H);
	cairo_pattern_t *pat = cairo_pattern_create_linear(x0, 0, x1, 0);
	double e = (x - x0) / (x1 - x0);
	cairo_pattern_add_color_stop_rgba(pat, 0.0, r, g, b, whole ? 0.80 : 1.0);
	cairo_pattern_add_color_stop_rgba(pat, e, r * 0.98, g * 0.98, b * 0.98, whole ? 0.88 : 1.0);
	cairo_pattern_add_color_stop_rgba(pat, fmin(e + 0.02, 1.0), r * 0.91, g * 0.91, b * 0.91,
		1.0);
	cairo_pattern_add_color_stop_rgba(pat, 1.0, r, g, b, 1.0);
	cairo_set_source(cr, pat);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(pat);
	if (shade > 0.0) {
		cairo_set_source_rgba(cr, 0.10, 0.07, 0.04, shade);
		cairo_fill_preserve(cr);
	}
	cairo_set_source_rgba(cr, r * 0.5, g * 0.5, b * 0.5, 0.45);
	cairo_set_line_width(cr, 0.8);
	cairo_stroke(cr);
	/* the part's name, written on it by hand: a line or two, as small as
	 * the pad's longest takes */
	char line[2][96];
	int nl = note_lines(a, pg, line);
	double size = note_size(cr, a, t);
	desk_font_select(cr, DESK_FONT_HAND, line[0], false, false);
	cairo_set_font_size(cr, size);
	double lh = size * 1.02;
	double base = y + NOTE_H * 0.5 + size * 0.34 - (nl - 1) * lh * 0.5;
	cairo_set_source_rgba(cr, 0.20, 0.17, 0.15, 0.88 * (1.0 - 1.4 * shade));
	for (int i = 0; i < nl; i++) {
		cairo_move_to(cr, x + 7.0, base + i * lh);
		cairo_show_text(cr, line[i]);
	}
	if (alpha < 1.0) {
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, alpha);
	}
	cairo_restore(cr);
}

/* The wire's loops along the top of a pad w wide, a pair at a time, as
 * the desk calendar's: where they are, and how many pairs. */
#define RING_PAIR 30.0
static int ring_pairs(double w)
{
	int n = (int)floor((w - 48.0) / RING_PAIR) + 1;
	return n < 2 ? 2 : n;
}

static double ring_x(double w, int i, int n)
{
	return (w - (n - 1) * RING_PAIR) * 0.5 + i * RING_PAIR;
}

/* A sheet w by h, its top-left corner at the origin: the paper, its edge,
 * the holes the wire goes through, and f printed on it. */
static void sheet_paint(cairo_t *cr, const struct desk_shell_form *f, double w, double h)
{
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_save(cr);
	cairo_clip(cr);
	desk_draw_pad_paper(cr);
	cairo_restore(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.30, 0.25, 0.19, 0.30);
	cairo_rectangle(cr, 0.5, 0.5, w - 1.0, h - 1.0);
	cairo_stroke(cr);
	int n = ring_pairs(w);
	cairo_set_source_rgba(cr, 0.33, 0.28, 0.23, 0.85);
	for (int i = 0; i < n; i++) {
		for (int j = -1; j <= 1; j += 2) {
			/* a slot with round ends */
			double x = ring_x(w, i, n) + j * 2.6;
			cairo_new_path(cr);
			cairo_arc(cr, x, 6.4, 1.4, M_PI, 2.0 * M_PI);
			cairo_arc(cr, x, 9.6, 1.4, 0.0, M_PI);
			cairo_close_path(cr);
			cairo_fill(cr);
		}
	}
	cairo_translate(cr, 0, PAD_INK);
	desk_draw_shell_form(cr, w, h - PAD_INK - 6.0, f);
	cairo_restore(cr);
}

/* The wire over all of it: the front half of each loop, from its hole up
 * over the top of the pad. */
static void rings_paint(cairo_t *cr, double w)
{
	int n = ring_pairs(w);
	double lw = 2.0;
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	for (int i = 0; i < n; i++) {
		for (int j = -1; j <= 1; j += 2) {
			double x = ring_x(w, i, n) + j * 2.6;
			cairo_set_line_width(cr, lw * 1.15);
			cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
			cairo_move_to(cr, x + 1.1, 9.3);
			cairo_line_to(cr, x + 1.1, -6.7);
			cairo_stroke(cr);
			cairo_set_line_width(cr, lw);
			cairo_set_source_rgb(cr, 0.075, 0.075, 0.085);
			cairo_move_to(cr, x, 8.0);
			cairo_line_to(cr, x, -8.0);
			cairo_stroke(cr);
			cairo_set_line_width(cr, lw * 0.3);
			cairo_set_source_rgba(cr, 0.66, 0.66, 0.70, 0.70);
			cairo_move_to(cr, x - 0.45, 7.5);
			cairo_line_to(cr, x - 0.45, -8.0);
			cairo_stroke(cr);
		}
	}
	cairo_restore(cr);
}

/* Sheet pg drawn on its own as it stands, its note with it: for while it
 * is going over the wire. */
static cairo_surface_t *page_picture(const struct app *a, int pg)
{
	struct pad p;
	pad_at(a, &p);
	int w = (int)ceil(p.w + NOTE_OUT), h = (int)ceil(p.h);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return NULL;
	}
	struct desk_shell_form f = page_form(a, pg);
	cairo_t *cr = cairo_create(s);
	sheet_paint(cr, &f, p.w, p.h);
	note_paint(cr, a, pg, p.w, note_y(a, pg), 0.0, 0.0, true, 1.0);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	return s;
}

static void flip_end(struct app *a)
{
	for (int i = 0; i < PAGES_MAX; i++) {
		if (a->flip_img[i]) {
			cairo_surface_destroy(a->flip_img[i]);
			a->flip_img[i] = NULL;
		}
	}
	a->flip_on = false;
}

static double since(const struct timespec *t);

/* How far the turning is, 0 to 1, eased in and out. */
static double flip_eased(const struct app *a)
{
	double t = since(&a->flip_at) / a->flip_s;
	t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	return t * t * (3.0 - 2.0 * t);
}

/* Sheet pg's angle over the wire (0 lying on the pad, FLIP_END over and
 * out of sight) at st of the turning. Going over together, the sheets fan
 * a little apart, the top one leading; coming back it lands last. */
static double flip_angle(const struct app *a, int pg, double st)
{
	int lo = a->flip_from < a->flip_to ? a->flip_from : a->flip_to;
	int n = abs(a->flip_to - a->flip_from);
	double base = a->flip_to > a->flip_from ? FLIP_END * st : FLIP_END * (1.0 - st);
	double ang = base;
	if (n > 1) {
		ang += (0.25 + 0.06 * n) * sin(M_PI * st) * (0.5 - (double)(pg - lo) / (n - 1));
	}
	return ang < 0.0 ? 0.0 : ang > FLIP_END ? FLIP_END : ang;
}

/* How long a sheet at angle ang reaches down from the wire, on the page's
 * own scale: the eye a little above the pad, as at the calendar. */
static double flip_reach(double ang)
{
	return cos(ang - FLIP_EYE) / cos(FLIP_EYE);
}

/* A sheet in the air, img as it was printed (its note with it), at angle
 * ang over the wire, its top-left corner at the origin. In strips, as the
 * calendar's pages are: a picture can only be turned and stretched, and a
 * sheet coming up at you is wider at its free edge. */
static void sheet_turning(cairo_t *cr, cairo_surface_t *img, double pw, double ph, double ang)
{
	double s = flip_reach(ang);
	if (s * ph < 0.75) {
		return;   /* over the wire, out of sight */
	}
	bool face = cos(ang + FLIP_EYE) > 0.0;   /* the printed side towards you */
	double bulge = 0.10 * sin(ang), cx = pw * 0.5;
	int iw = cairo_image_surface_get_width(img), ih = cairo_image_surface_get_height(img);
	/* the sheet put together from its strips first, and then its back or
	 * its shade laid over it whole: laid over strip by strip, the seams
	 * between them would show */
	cairo_push_group(cr);
	const int N = 28;
	for (int i = 0; i < N; i++) {
		double u0 = (double)i / N, u1 = (double)(i + 1) / N;
		double y0 = s * ph * u0, y1 = s * ph * u1;
		double k0 = 1.0 + bulge * u0, k1 = 1.0 + bulge * u1, km = 0.5 * (k0 + k1);
		double sy = (y1 - y0) / ((u1 - u0) * ih);
		/* a hair of overlap, or the strips show as lines */
		double o = i + 1 < N ? 0.6 : 0.0;
		cairo_save(cr);
		cairo_move_to(cr, cx - cx * k0, y0);
		cairo_line_to(cr, cx + (iw - cx) * k0, y0);
		cairo_line_to(cr, cx + (iw - cx) * k1, y1 + o);
		cairo_line_to(cr, cx - cx * k1, y1 + o);
		cairo_close_path(cr);
		cairo_clip(cr);
		cairo_translate(cr, cx - cx * km, y0 - u0 * ih * sy);
		cairo_scale(cr, km, sy);
		cairo_set_source_surface(cr, img, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
		cairo_paint(cr);
		cairo_restore(cr);
	}
	cairo_pattern_t *sheet = cairo_pop_group(cr);
	cairo_set_source(cr, sheet);
	cairo_paint(cr);
	if (!face) {
		/* its back: plain, the print showing through a little */
		cairo_set_source_rgba(cr, 0.950, 0.940, 0.915, 0.86);
		cairo_mask(cr, sheet);
	}
	/* darker as it turns from the lamp -- the face most at its free edge,
	 * which is coming up at you, the back all over */
	cairo_pattern_t *shade = cairo_pattern_create_linear(0, 0, 0, s * ph);
	double dark = face ? 0.10 * sin(ang) : 0.05 + 0.07 * fabs(sin(ang));
	cairo_pattern_add_color_stop_rgba(shade, 0.0, 0, 0, 0, face ? 0.0 : dark);
	cairo_pattern_add_color_stop_rgba(shade, 1.0, 0, 0, 0, dark);
	cairo_set_source(cr, shade);
	cairo_mask(cr, sheet);
	cairo_pattern_destroy(shade);
	cairo_pattern_destroy(sheet);
}

/* The shadow a sheet at angle ang throws on the one it is lifting off. */
static void turning_shadow(cairo_t *cr, double pw, double ph, double ang)
{
	double s = flip_reach(ang);
	if (s <= 0.0) {
		return;
	}
	double ye = s * ph, lift = sin(fmin(ang, M_PI / 2.0));
	double hg = 8.0 + 34.0 * lift, al = 0.24 * fmin(1.0, lift * 2.5);
	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, pw, ph);
	cairo_clip(cr);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, ye, 0, ye + hg);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.10, 0.07, 0.04, al);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.10, 0.07, 0.04, 0.0);
	cairo_rectangle(cr, 0, ye, pw, hg);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	cairo_restore(cr);
}

/* The pad in front, in the window's own place for it: the sheets under
 * the top one and their notes, the notes of those turned over hanging
 * behind, the top sheet and its note, the sheets in the air, the wire.
 * hand: the hand's row and note shown. */
static void pad_paint(cairo_t *cr, const struct app *a, bool hand)
{
	struct pad p;
	pad_at(a, &p);
	int t = a->tab, p0 = a->tab_pg0[t], np = a->tab_npg[t];
	bool flip = a->flip_on;
	double st = flip ? flip_eased(a) : 1.0;
	/* in the air, lo to hi - 1; lying on top, flat */
	int lo = flip ? (a->flip_from < a->flip_to ? a->flip_from : a->flip_to) : a->page;
	int hi = flip ? (a->flip_from < a->flip_to ? a->flip_to : a->flip_from) : a->page;
	int flat = flip ? hi : a->page;
	int under = p0 + np - flat - 1;
	under = under > 3 ? 3 : under;
	cairo_save(cr);
	cairo_translate(cr, p.x, p.ay);
	/* the pad's shadow on the folder */
	if (!app_flat(a)) {
		cairo_set_source_rgba(cr, 0.12, 0.08, 0.04, 0.10);
		cairo_rectangle(cr, 1.0, 2.5, p.w, p.h + 1.6 * under);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 0.12, 0.08, 0.04, 0.08);
		cairo_rectangle(cr, 0.5, 1.0, p.w, p.h + 1.6 * under);
		cairo_fill(cr);
	}
	/* the sheets under the top one: their edges, along the foot */
	for (int k = under; k >= 1; k--) {
		cairo_rectangle(cr, 0, 1.6 * k, p.w, p.h);
		cairo_set_source_rgb(cr, 0.935 - 0.012 * k, 0.925 - 0.012 * k, 0.895 - 0.012 * k);
		cairo_fill(cr);
		cairo_set_line_width(cr, 1.0);
		cairo_set_source_rgba(cr, 0.30, 0.25, 0.19, 0.35);
		cairo_move_to(cr, 0, 1.6 * k + p.h - 0.5);
		cairo_line_to(cr, p.w, 1.6 * k + p.h - 0.5);
		cairo_stroke(cr);
	}
	/* the notes but the top sheet's: those of the sheets turned over hang
	 * behind the pad, a shade darker and not so far out; a sheet going over
	 * hands its note on to there as it goes out of sight */
	double hidden = M_PI / 2.0 + FLIP_EYE;
	for (int pg = p0; pg < p0 + np; pg++) {
		if (pg == flat) {
			continue;
		}
		double out = hand && pg == a->note_hover ? 7.0 : 0.0;
		if (flip && pg >= lo && pg < hi) {
			double bv = (flip_angle(a, pg, st) - hidden) / (FLIP_END - hidden);
			note_paint(cr, a, pg, p.w, note_y(a, pg), out - 8.0, 0.24, false,
				bv < 0.0 ? 0.0 : bv > 1.0 ? 1.0 : bv);
			continue;
		}
		bool behind = pg < (flip ? lo : flat);
		note_paint(cr, a, pg, p.w, note_y(a, pg), out + (behind ? -8.0 : 0.0),
			behind ? 0.24 : 0.0, false, 1.0);
	}
	/* the sheet on top, and its note */
	if (flip && a->flip_img[flat]) {
		cairo_set_source_surface(cr, a->flip_img[flat], 0, 0);
		cairo_paint(cr);
	} else {
		struct desk_shell_form f = a->form;
		if (!hand) {
			f.hover = f.hover_opt = -1;
		}
		sheet_paint(cr, &f, p.w, p.h);
		note_paint(cr, a, flat, p.w, note_y(a, flat),
			hand && flat == a->note_hover ? 7.0 : 0.0, 0.0, true, 1.0);
	}
	/* the sheets in the air: the shadow the lowest throws on the top one,
	 * then each, the farthest from the eye first */
	if (flip) {
		int order[PAGES_MAX], n = 0;
		double ang[PAGES_MAX];
		for (int pg = lo; pg < hi && n < PAGES_MAX; pg++) {
			ang[pg] = flip_angle(a, pg, st);
			int j = n++;
			while (j > 0 && sin(ang[order[j - 1]]) > sin(ang[pg])) {
				order[j] = order[j - 1];
				j--;
			}
			order[j] = pg;
		}
		double low = FLIP_END;
		for (int i = 0; i < n; i++) {
			low = fmin(low, ang[order[i]]);
		}
		turning_shadow(cr, p.w, p.h, low);
		for (int i = 0; i < n; i++) {
			if (a->flip_img[order[i]]) {
				sheet_turning(cr, a->flip_img[order[i]], p.w, p.h, ang[order[i]]);
			}
		}
	}
	rings_paint(cr, p.w);
	cairo_restore(cr);
}

/* The pad in front drawn on its own as it stands, from the foot of the
 * tabs down: for while it is being pulled out. */
static cairo_surface_t *pad_picture(const struct app *a)
{
	int h = a->h - (int)TAB_H;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, a->w, h > 1 ? h : 1);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return NULL;
	}
	cairo_t *cr = cairo_create(s);
	cairo_translate(cr, 0, -TAB_H);
	pad_paint(cr, a, false);
	cairo_destroy(cr);
	cairo_surface_flush(s);
	return s;
}

static void anim_end(struct app *a)
{
	if (a->anim_old) {
		cairo_surface_destroy(a->anim_old);
	}
	if (a->anim_new) {
		cairo_surface_destroy(a->anim_new);
	}
	a->anim_old = a->anim_new = NULL;
	a->anim_on = false;
}

/* The pad in front pulled up out of the folder from the bottom, wait
 * seconds from now, over old (the pad that was in front, or NULL). */
static void pull_start(struct app *a, cairo_surface_t *old, double wait)
{
	anim_end(a);
	if (app_flat(a)) {
		if (old) {
			cairo_surface_destroy(old);
		}
		return;   /* 2D: the pad is simply there */
	}
	a->anim_old = old;
	a->anim_new = pad_picture(a);
	a->anim_on = a->anim_new != NULL;
	if (!a->anim_on) {
		anim_end(a);
		return;
	}
	a->anim_wait = wait;
	clock_gettime(CLOCK_MONOTONIC, &a->anim_at);
	desk_sound_play_in(DESK_SND_SHEET_TAKE, (a->px / (a->w > 0 ? a->w : 1)) * 0.6 - 0.3, 1.0,
		0.0, wait);
}

/* The index tabs along the top, as a folder has them: the one in front open
 * into its page, the other behind it, a shade darker. */
static void draw_tabs(cairo_t *cr, struct app *a)
{
	double base = TAB_H - 4.0, top = 9.0, x = 24.0;
	cairo_save(cr);
	for (int t = 0; t < 2; t++) {
		const char *word = TR(TAB_WORDS[t]);
		desk_font_select(cr, DESK_FONT_UI, word, t == a->tab, false);
		cairo_set_font_size(cr, 14.5);
		cairo_text_extents_t te;
		cairo_text_extents(cr, word, &te);
		a->tab_x[t] = x;
		a->tab_w[t] = te.x_advance + 44.0;
		x += a->tab_w[t] + 4.0;
	}
	/* the page's top edge along the foot of the tabs, open under the one in
	 * front */
	double fx = a->tab_x[a->tab], fw = a->tab_w[a->tab];
	cairo_set_line_width(cr, 1.2);
	cairo_set_source_rgba(cr, 0.27, 0.21, 0.15, 0.55);
	cairo_move_to(cr, 12.0, base + 0.5);
	cairo_line_to(cr, fx, base + 0.5);
	cairo_move_to(cr, fx + fw, base + 0.5);
	cairo_line_to(cr, a->w - 12.0, base + 0.5);
	cairo_stroke(cr);
	for (int t = 0; t < 2; t++) {
		bool front = t == a->tab;
		double tx = a->tab_x[t], tw = a->tab_w[t], r = 6.0, lean = 5.0;
		/* sides leaning in a little to a rounded top */
		cairo_new_path(cr);
		cairo_move_to(cr, tx, base + 0.5);
		cairo_line_to(cr, tx + lean, top + r);
		cairo_arc(cr, tx + lean + r, top + r, r, M_PI, 1.5 * M_PI);
		cairo_arc(cr, tx + tw - lean - r, top + r, r, 1.5 * M_PI, 2.0 * M_PI);
		cairo_line_to(cr, tx + tw, base + 0.5);
		if (front) {
			cairo_set_source_rgba(cr, 1.0, 0.97, 0.88, 0.45);
		} else if (t == a->tab_hover) {
			cairo_set_source_rgba(cr, 1.0, 0.96, 0.84, 0.30);
		} else {
			cairo_set_source_rgba(cr, 0.30, 0.22, 0.14, 0.07);
		}
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.27, 0.21, 0.15, front ? 0.85 : 0.45);
		cairo_stroke(cr);
		const char *word = TR(TAB_WORDS[t]);
		desk_font_select(cr, DESK_FONT_UI, word, front, false);
		cairo_set_font_size(cr, 14.5);
		cairo_set_source_rgba(cr, 0.18, 0.14, 0.10, front ? 0.95 : 0.62);
		cairo_move_to(cr, tx + 22.0, base - 9.0);
		cairo_show_text(cr, word);
	}
	cairo_restore(cr);
}

static void draw(struct app *a);

/* The other pad brought to the front: pulled up out of the folder over
 * the one there, as a sheet from the bottom of a pile. */
static void tab_switch(struct app *a, int t)
{
	if (t == a->tab || t < 0 || t > 1) {
		return;
	}
	flip_end(a);
	anim_end(a);
	cairo_surface_t *was = a->configured_once ? pad_picture(a) : NULL;
	page_show(a, a->tab_open[t]);
	if (was) {
		pull_start(a, was, 0.0);
	}
	draw(a);
}

/* The pad turned to sheet pg, over the wire: forwards, the sheets on it
 * going over together; back, the ones between coming back down. */
static void page_turn(struct app *a, int pg)
{
	if (pg == a->page || pg < 0 || pg >= a->npages || a->pg_tab[pg] != a->tab) {
		return;
	}
	flip_end(a);
	anim_end(a);
	double pan = (a->px / (a->w > 0 ? a->w : 1)) * 0.6 - 0.3;
	if (app_flat(a)) {
		/* 2D: the sheet is simply on top */
		page_show(a, pg);
		desk_sound_play(DESK_SND_PAGE_TURN, pan, 1.0, 0.0);
		draw(a);
		return;
	}
	int from = a->page;
	int lo = from < pg ? from : pg, hi = from < pg ? pg : from, n = hi - lo;
	bool ok = a->configured_once;
	/* each as it is now, the one on top slid as it is */
	for (int k = lo; k <= hi && ok; k++) {
		a->flip_img[k] = page_picture(a, k);
		ok = a->flip_img[k] != NULL;
	}
	page_show(a, pg);
	if (!ok) {
		flip_end(a);
		draw(a);
		return;
	}
	a->flip_from = from;
	a->flip_to = pg;
	a->flip_s = FLIP_S * (1.0 + 0.12 * (n - 1)) * g_slow;
	a->flip_w = a->w;
	a->flip_h = a->h;
	a->flip_on = true;
	clock_gettime(CLOCK_MONOTONIC, &a->flip_at);
	if (n == 1) {
		desk_sound_play(DESK_SND_PAGE_TURN, pan, 1.0, 0.0);
	} else {
		/* a few together: a thumb run along their edges, and over they go */
		desk_sound_play(DESK_SND_LETTER_FLICK, pan, fmin(0.8 + 0.15 * n, 1.4), 1.0);
		desk_sound_play(DESK_SND_PAGE_TURN, pan, 1.1, fmax(0.80 - 0.05 * n, 0.55));
	}
	draw(a);
}

static void draw(struct app *a)
{
	if (!a->configured_once || a->closed) {
		return;
	}
	struct buffer *b = !a->buf[0].busy ? &a->buf[0] : !a->buf[1].busy ? &a->buf[1] : NULL;
	if (!b) {
		a->dirty = true;   /* both with the compositor: when one comes back */
		return;
	}
	if (b->w != a->w || b->h != a->h || !b->wl) {
		if (!buffer_make(a, b, a->w, a->h)) {
			return;
		}
	}
	cairo_t *cr = cairo_create(b->surface);
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);
	double area = a->h - TAB_H;
	double t = a->anim_on ? (since(&a->anim_at) - a->anim_wait) / (PULL_S * g_slow) : 1.0;
	if (a->anim_on && (t >= 1.0 || cairo_image_surface_get_width(a->anim_new) != a->w
		|| cairo_image_surface_get_height(a->anim_new) != a->h - (int)TAB_H)) {
		anim_end(a);
	}
	if (a->flip_on && (since(&a->flip_at) >= a->flip_s || a->flip_w != a->w
		|| a->flip_h != a->h)) {
		flip_end(a);
	}
	cairo_save(cr);
	cairo_translate(cr, 0, TAB_H);
	cairo_rectangle(cr, 0, 0, a->w, area);
	cairo_clip(cr);
	if (a->anim_on) {
		double p = t <= 0.0 ? 0.0 : 1.0 - pow(1.0 - t, 3.0);
		double off = (1.0 - p) * (area + 8.0);
		double edge = off + PAD_WIRE;   /* the top of its sheets */
		if (a->anim_old) {
			/* the pad that was in front, where the new one has not covered it */
			cairo_save(cr);
			cairo_rectangle(cr, 0, 0, a->w, edge);
			cairo_clip(cr);
			cairo_set_source_surface(cr, a->anim_old, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
			/* the shadow its edge throws on it */
			cairo_pattern_t *g = cairo_pattern_create_linear(0, edge - 14.0, 0, edge);
			cairo_pattern_add_color_stop_rgba(g, 0.0, 0.15, 0.10, 0.05, 0.0);
			cairo_pattern_add_color_stop_rgba(g, 1.0, 0.15, 0.10, 0.05, 0.22);
			cairo_rectangle(cr, 0, edge - 14.0, a->w, 14.0);
			cairo_set_source(cr, g);
			cairo_fill(cr);
			cairo_pattern_destroy(g);
		}
		cairo_set_source_surface(cr, a->anim_new, 0, off);
		cairo_paint(cr);
	} else {
		cairo_translate(cr, 0, -TAB_H);
		pad_paint(cr, a, true);
	}
	cairo_restore(cr);
	draw_tabs(cr, a);
	cairo_destroy(cr);
	cairo_surface_flush(b->surface);
	wl_surface_attach(a->surf, b->wl, 0, 0);
	wl_surface_damage(a->surf, 0, 0, a->w, a->h);
	wl_surface_commit(a->surf);
	b->busy = true;
	a->dirty = false;
}

/* ---- the sheet ---- */

static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void xsurf_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
	struct app *a = data;
	xdg_surface_ack_configure(xs, serial);
	/* Its own size, unless it is maximised, has the whole screen or is
	 * being made bigger or smaller by hand: the size every new window is
	 * offered (and offered again with every configure) is too big for one
	 * form. */
	if (a->pending_mode && a->pending_w > 0 && a->pending_h > 0) {
		a->w = a->pending_w < 320 ? 320 : a->pending_w;
		a->h = a->pending_h < 180 ? 180 : a->pending_h;
		if (a->pending_mode == 2) {
			a->own_w = a->w;
			a->own_h = a->h;
		}
	} else {
		a->w = a->own_w;
		a->h = a->own_h;
	}
	bool first = !a->configured_once;
	a->configured_once = true;
	form_measure(a);
	if (first) {
		/* the folder put down, then the pad pulled up out of it */
		pull_start(a, NULL, PULL_WAIT * g_slow);
	}
	draw(a);
}

static const struct xdg_surface_listener xsurf_listener = { .configure = xsurf_configure };

static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
	struct wl_array *states)
{
	(void)t;
	struct app *a = data;
	a->pending_w = w;
	a->pending_h = h;
	a->pending_mode = 0;
	uint32_t *st;
	wl_array_for_each(st, states) {
		if (*st == XDG_TOPLEVEL_STATE_MAXIMIZED || *st == XDG_TOPLEVEL_STATE_FULLSCREEN) {
			a->pending_mode = 1;
		} else if (*st == XDG_TOPLEVEL_STATE_RESIZING && a->pending_mode == 0) {
			a->pending_mode = 2;
		}
	}
}

static void top_close(void *data, struct xdg_toplevel *t)
{
	(void)t;
	struct app *a = data;
	a->closed = true;
}

static const struct xdg_toplevel_listener top_listener = {
	.configure = top_configure,
	.close = top_close,
};

/* ---- the hand ---- */

static void scroll_clamp(struct app *a)
{
	double most = a->total - page_h(a);
	a->form.scroll = a->form.scroll > most ? most : a->form.scroll;
	a->form.scroll = a->form.scroll < 0.0 ? 0.0 : a->form.scroll;
}

/* The tab under the hand, or -1. */
static int tab_at(const struct app *a)
{
	if (a->py < 4.0 || a->py > TAB_H - 3.0) {
		return -1;
	}
	for (int t = 0; t < 2; t++) {
		if (a->px >= a->tab_x[t] && a->px <= a->tab_x[t] + a->tab_w[t]) {
			return t;
		}
	}
	return -1;
}

/* The sheet whose note is under the hand, of the pad in front, or -1. */
static int note_at(const struct app *a)
{
	struct pad p;
	pad_at(a, &p);
	double x = a->px - p.x - p.w, y = a->py - p.ay;
	int t = a->tab;
	for (int pg = a->tab_pg0[t]; pg < a->tab_pg0[t] + a->tab_npg[t]; pg++) {
		double ny = note_y(a, pg);
		double x0 = pg == a->page ? -NOTE_STUCK : 0.0;
		if (y >= ny && y <= ny + NOTE_H && x >= x0 && x <= NOTE_OUT - 6.0) {
			return pg;
		}
	}
	return -1;
}

/* The row of the sheet on top under the hand (its place on the sheet), or
 * -1; opt, the word on it. Nothing while sheets are on the move. */
static int row_at(const struct app *a, int *opt)
{
	*opt = -1;
	struct pad p;
	pad_at(a, &p);
	double top = p.ay + PAD_INK;
	if (a->py < top || a->flip_on || a->anim_on) {
		return -1;
	}
	return desk_shell_form_hit(p.w, page_h(a), &a->form, a->px - p.x, a->py - top, opt);
}

static void date_untype(struct app *a);

static void track(struct app *a)
{
	int opt;
	int at = row_at(a, &opt);
	int tab = tab_at(a);
	int note = note_at(a);
	if (a->date_row >= 0 && (at < 0 || a->pg_first[a->page] + at != a->date_row)) {
		date_untype(a);
	}
	if (at != a->form.hover || opt != a->form.hover_opt || tab != a->tab_hover
		|| note != a->note_hover) {
		a->form.hover = at;
		a->form.hover_opt = opt;
		a->tab_hover = tab;
		a->note_hover = note;
		draw(a);
	}
}

/* How loud the desk is, as the form says. */
static double sound_level(const struct app *a)
{
	for (int i = 0; i < N_SETTINGS; i++) {
		if (SETTINGS[i].key && !strcmp(SETTINGS[i].key, "sound")) {
			int v = a->rows[i].value;
			return v == 0 ? 0.0 : (v == 1 ? 0.45 : 1.0);
		}
	}
	return 1.0;
}

/* Row i set to v, and written down. */
static void set_value(struct app *a, int i, int v)
{
	const struct setting *st = &SETTINGS[i];
	if (!st->key || v == a->rows[i].value) {
		return;
	}
	char date[24] = "";
	if (st->kind == S_DATE && v > 0) {
		snprintf(date, sizeof date, "%02d-%02d", DATE_M(v), DATE_D(v));
	}
	const char *word = st->kind == S_DATE ? date : st->kind == S_CHECK ? (v ? "yes" : "no")
		: st->vals[v];
	if (conf_write(a->path, st->key, word)) {
		a->rows[i].value = v;
		if (st->kind == S_DATE) {
			date_words(v, NULL, a->opt[i]);
		}
		if (!strcmp(st->key, "language")) {
			words_apply(a);
		}
		form_measure(a);
		/* ticked, or ringed, in pencil -- as loud as the desk now is */
		desk_sound_set_level(sound_level(a));
		desk_sound_play(st->kind != S_CHOICE ? DESK_SND_PENCIL_TICK : DESK_SND_PENCIL_RING,
			(a->px / (a->w > 0 ? a->w : 1)) * 0.6 - 0.3, 1.0, 0.0);
		draw(a);
	}
}

/* A date moved on (or back) a month or a day -- a day past the end of its
 * month into the next -- or set to none (see date_words for which word is
 * which: 0 to 2 the month, 3 to 5 the day, 6 none). With none set, the
 * first step puts in today. */
static void date_step(struct app *a, int i, int opt)
{
	int v = a->rows[i].value, m = DATE_M(v), d = DATE_D(v);
	if (opt == 6) {
		set_value(a, i, 0);
		return;
	}
	if (opt < 0 && m >= 1 && d >= 1) {
		return;   /* Space or Enter on it: only what puts in a first date */
	}
	if (m < 1 || d < 1) {
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		set_value(a, i, (tm.tm_mon + 1) * 32 + tm.tm_mday);
		return;
	}
	if (opt <= 2) {
		m += opt == 0 ? -1 : 1;
		m = m < 1 ? 12 : m > 12 ? 1 : m;
	} else {
		d += opt == 3 ? -1 : 1;
		if (d < 1) {
			m = m == 1 ? 12 : m - 1;
			d = month_days(m);
		} else if (d > month_days(m)) {
			m = m == 12 ? 1 : m + 1;
			d = 1;
		}
	}
	d = d > month_days(m) ? month_days(m) : d;
	set_value(a, i, m * 32 + d);
}

/* The date on row i typed in, a digit at a time, MMDD: shown as it comes,
 * and put in when it is whole and a real day. */
static void date_type(struct app *a, int i, int digit)
{
	if (a->date_row != i) {
		a->date_row = i;
		a->date_typed[0] = '\0';
	}
	size_t n = strlen(a->date_typed);
	if (n < 4) {
		a->date_typed[n] = (char)('0' + digit);
		a->date_typed[n + 1] = '\0';
		n++;
	}
	if (n == 4) {
		int v = 0, m = 0, d = 0;
		if (sscanf(a->date_typed, "%2d%2d", &m, &d) == 2 && m >= 1 && m <= 12 && d >= 1
			&& d <= month_days(m)) {
			v = m * 32 + d;
		}
		a->date_row = -1;
		a->date_typed[0] = '\0';
		if (v > 0 && v != a->rows[i].value) {
			set_value(a, i, v);
			return;
		}
	}
	words_apply(a);
	form_measure(a);
	draw(a);
}

/* What was being typed into a date, given up (the hand gone from its row,
 * Esc). */
static void date_untype(struct app *a)
{
	if (a->date_row >= 0) {
		a->date_row = -1;
		a->date_typed[0] = '\0';
		words_apply(a);
		form_measure(a);
		draw(a);
	}
}

/* How long an action's "Opening…" stays: about as long as the slowest of
 * the programs takes to put its window up. Not used again in that time. */
#define OPENING_S 2.5

static double since(const struct timespec *t)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - t->tv_sec) + (now.tv_nsec - t->tv_nsec) / 1e9;
}

/* Action i used: the first of its programs that is there started; a page
 * turned, as for another form. */
static void open_row(struct app *a, int i)
{
	if (a->opening == i) {
		return;
	}
	if (actions_find(a)) {
		words_apply(a);
	}
	int k = a->found[i];
	char path[600];
	if (k >= 0) {
		const struct prog *p = &SETTINGS[i].opens->progs[k];
		if (find_program(p->bin, path, sizeof path) && launch(path)) {
			fprintf(stderr, "desk-settings: opened %s\n", path);
			/* one "Opening…" at a time: the last one used */
			int was = a->opening;
			a->opening = i;
			clock_gettime(CLOCK_MONOTONIC, &a->opened_at);
			action_words(a, i);
			if (was >= 0) {
				action_words(a, was);
			}
			desk_sound_play(DESK_SND_PAGE_TURN, (a->px / (a->w > 0 ? a->w : 1)) * 0.6 - 0.3,
				1.0, 0.0);
		}
	}
	form_measure(a);
	draw(a);
}

/* "Opening…" back to "Open ›" when its time is up. */
static void opening_done(struct app *a)
{
	if (a->opening >= 0 && since(&a->opened_at) >= OPENING_S) {
		int i = a->opening;
		a->opening = -1;
		action_words(a, i);
		form_measure(a);
		draw(a);
	}
}

/* What a click, or Space, on row i does: a box is ticked or not; a choice
 * goes to the word under the hand, or the next one; an action opens what
 * it opens. */
static void use_row(struct app *a, int i, int opt)
{
	if (i < 0 || i >= N_SETTINGS) {
		return;
	}
	const struct setting *st = &SETTINGS[i];
	if (st->kind == S_PREVIEW && st->key && !strcmp(st->key, "termites_restore")) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		char stamp[32];
		snprintf(stamp, sizeof stamp, "%lld",
			(long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
		if (conf_write(a->path, "termites_restore", stamp)) {
			desk_sound_play(DESK_SND_PENCIL_TICK, 0.0, 1.0, 0.0);
			draw(a);
		}
		return;
	}
	if (st->kind == S_CHECK) {
		set_value(a, i, !a->rows[i].value);
	} else if (st->kind == S_CHOICE) {
		int n = 0;
		while (st->words[n]) {
			n++;
		}
		set_value(a, i, opt >= 0 ? opt : (a->rows[i].value + 1 + n) % n);
	} else if (st->kind == S_DATE) {
		date_step(a, i, opt);
	} else if (st->kind == S_PREVIEW) {
		char stamp[48];
		snprintf(stamp, sizeof stamp, "%lld", (long long)time(NULL));
		if (conf_write(a->path, "termites_preview", stamp)) {
			desk_sound_play(DESK_SND_PENCIL_TICK, 0.0, 1.0, 0.0);
			draw(a);
		}
	} else if (st->kind == S_ACTION) {
		open_row(a, i);
	}
}

/* The keyboard's row on the page in front: the next one that is not a
 * heading, either way. */
static int next_row(const struct app *a, int from, int dir)
{
	const struct setting *page = SETTINGS + a->pg_first[a->page];
	for (int i = from + dir; i >= 0 && i < a->pg_n[a->page]; i += dir) {
		if (page[i].kind != S_SECTION) {
			return i;
		}
	}
	return from;
}

/* The sheet slid as far as it takes for the keyboard's row to be in the
 * window -- and the heading over it, when it is the first in its part. */
static void show_row(struct app *a, int i)
{
	double top, h, ht, hh;
	desk_shell_form_row_span(ink_w(a), &a->form, i, &top, &h);
	if (i > 0 && SETTINGS[a->pg_first[a->page] + i - 1].kind == S_SECTION) {
		desk_shell_form_row_span(ink_w(a), &a->form, i - 1, &ht, &hh);
		h += top - ht;
		top = ht;
	}
	if (top < a->form.scroll) {
		a->form.scroll = top;
	} else if (top + h > a->form.scroll + page_h(a)) {
		a->form.scroll = top + h - page_h(a);
	}
	scroll_clamp(a);
}

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
	wl_fixed_t x, wl_fixed_t y)
{
	(void)s;
	struct app *a = data;
#ifdef DESK_HAVE_WL_CURSOR
	struct wl_cursor *c = a->cursor_theme
		? wl_cursor_theme_get_cursor(a->cursor_theme, "left_ptr") : NULL;
	if (c && c->image_count > 0) {
		struct wl_cursor_image *img = c->images[0];
		wl_pointer_set_cursor(p, serial, a->cursor_surf, (int32_t)img->hotspot_x,
			(int32_t)img->hotspot_y);
		wl_surface_attach(a->cursor_surf, wl_cursor_image_get_buffer(img), 0, 0);
		wl_surface_damage(a->cursor_surf, 0, 0, (int32_t)img->width, (int32_t)img->height);
		wl_surface_commit(a->cursor_surf);
	}
#else
	(void)p;
	(void)serial;
#endif
	/* someone may have changed it in the meantime, by hand, or installed
	 * something */
	conf_read(a->path, a->rows);
	actions_find(a);
	if (a->icons_done) {
		icons_apply(a);
	}
	words_apply(a);
	form_measure(a);
	a->px = wl_fixed_to_double(x);
	a->py = wl_fixed_to_double(y);
	a->form.hover = -2;
	track(a);
}

static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)p;
	(void)serial;
	(void)s;
	struct app *a = data;
	a->pressed = -1;
	a->form.hover = -1;
	a->form.hover_opt = -1;
	a->tab_hover = a->note_hover = -1;
	draw(a);
}

static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
	wl_fixed_t y)
{
	(void)p;
	(void)time;
	struct app *a = data;
	a->px = wl_fixed_to_double(x);
	a->py = wl_fixed_to_double(y);
	track(a);
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state)
{
	(void)p;
	(void)serial;
	(void)time;
	struct app *a = data;
	if (button != BTN_LEFT) {
		return;
	}
	int opt;
	int at = row_at(a, &opt);
	int tab = tab_at(a);
	int note = note_at(a);
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		a->pressed = at;
		a->pressed_opt = opt;
		a->tab_pressed = tab;
		a->note_pressed = note;
	} else {
		/* let go where it was taken hold of: a tab; a note; a box or an
		 * action anywhere on its row, a choice on the same word */
		if (tab >= 0 && tab == a->tab_pressed) {
			tab_switch(a, tab);
		} else if (note >= 0 && note == a->note_pressed) {
			page_turn(a, note);
		} else if (at >= 0 && at == a->pressed) {
			int i = a->pg_first[a->page] + at;
			int kind = SETTINGS[i].kind;
			if (kind == S_CHECK || kind == S_ACTION || kind == S_PREVIEW) {
				use_row(a, i, -1);
			} else if (opt >= 0 && opt == a->pressed_opt) {
				use_row(a, i, opt);
			}
		}
		a->pressed = -1;
		a->tab_pressed = -1;
		a->note_pressed = -1;
	}
}

/* The wheel slides the sheet up and down, when it is longer than the
 * window it is in. */
static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
	wl_fixed_t value)
{
	(void)p;
	(void)time;
	struct app *a = data;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
		return;
	}
	int at = a->form.hover, opt = a->form.hover_opt;
	int i = at >= 0 ? a->pg_first[a->page] + at : -1;
	if (i >= 0 && SETTINGS[i].kind == S_DATE && opt >= 0 && opt < 6) {
		/* over a date's month or day: the wheel turns it, up on and
		 * down back, a notch a step */
		a->wheel -= wl_fixed_to_double(value);
		while (a->wheel >= 10.0 || a->wheel <= -10.0) {
			bool on = a->wheel > 0.0;
			a->wheel += on ? -10.0 : 10.0;
			date_step(a, i, opt / 3 * 3 + (on ? 2 : 0));
		}
		return;
	}
	a->wheel = 0.0;
	double before = a->form.scroll;
	a->form.scroll += wl_fixed_to_double(value) * 2.4;
	scroll_clamp(a);
	if (a->form.scroll != before) {
		track(a);
		draw(a);
	}
}

static const struct wl_pointer_listener ptr_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
};

static void kbd_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
	uint32_t size)
{
	(void)data;
	(void)k;
	(void)format;
	(void)size;
	close(fd);
}

static void kbd_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s,
	struct wl_array *keys)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)s;
	(void)keys;
}

static void kbd_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s)
{
	(void)data;
	(void)k;
	(void)serial;
	(void)s;
}

static void kbd_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
	uint32_t key, uint32_t state)
{
	(void)k;
	(void)serial;
	(void)time;
	struct app *a = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}
	/* Ctrl+Tab, Ctrl+Page Up and Down: the other page */
	if ((a->mods & 4) && (key == KEY_TAB || key == KEY_PAGEUP || key == KEY_PAGEDOWN)) {
		tab_switch(a, key == KEY_PAGEUP ? 0 : key == KEY_PAGEDOWN ? 1 : 1 - a->tab);
		return;
	}
	int row = a->form.hover;
	/* the row as the whole form has it */
	int i = row >= 0 ? a->pg_first[a->page] + row : -1;
	int pad_end = a->tab_pg0[a->tab] + a->tab_npg[a->tab];
	/* a date typed in: the digits along the top of the keyboard or on the
	 * keypad, as they are on the keys */
	static const uint32_t DIGIT_KEY[10][2] = { { KEY_0, KEY_KP0 }, { KEY_1, KEY_KP1 },
		{ KEY_2, KEY_KP2 }, { KEY_3, KEY_KP3 }, { KEY_4, KEY_KP4 }, { KEY_5, KEY_KP5 },
		{ KEY_6, KEY_KP6 }, { KEY_7, KEY_KP7 }, { KEY_8, KEY_KP8 }, { KEY_9, KEY_KP9 } };
	if (i >= 0 && SETTINGS[i].kind == S_DATE && !(a->mods & 4)) {
		for (int dg = 0; dg < 10; dg++) {
			if (key == DIGIT_KEY[dg][0] || key == DIGIT_KEY[dg][1]) {
				date_type(a, i, dg);
				return;
			}
		}
		if (key == KEY_BACKSPACE && a->date_row == i) {
			size_t n = strlen(a->date_typed);
			if (n <= 1) {
				date_untype(a);
			} else {
				a->date_typed[n - 1] = '\0';
				words_apply(a);
				draw(a);
			}
			return;
		}
	}
	if (a->date_row >= 0 && (key == KEY_ESC || i != a->date_row || key == KEY_DOWN
		|| key == KEY_UP || key == KEY_TAB)) {
		/* Esc gives up what was being typed (and closes nothing) */
		bool esc = key == KEY_ESC;
		date_untype(a);
		if (esc) {
			return;
		}
	}
	if (key == KEY_DOWN || key == KEY_TAB || key == KEY_UP) {
		bool up = key == KEY_UP;
		int to = up ? next_row(a, row < 0 ? a->pg_n[a->page] : row, -1)
			: next_row(a, row < 0 ? -1 : row, 1);
		if (row >= 0 && to == row && (up ? a->page > a->tab_pg0[a->tab] : a->page + 1 < pad_end)) {
			/* past the last row on the sheet, or the first: on to the next
			 * sheet, or back */
			page_turn(a, a->page + (up ? -1 : 1));
			to = up ? next_row(a, a->pg_n[a->page], -1) : next_row(a, -1, 1);
		}
		a->form.hover = to;
		a->form.hover_opt = -1;
		show_row(a, a->form.hover);
		draw(a);
	} else if ((key == KEY_LEFT || key == KEY_RIGHT || key == KEY_PAGEUP || key == KEY_PAGEDOWN)
		&& i >= 0 && SETTINGS[i].kind == S_DATE) {
		/* a day either way; a month, Page Up and Down */
		date_step(a, i, key == KEY_LEFT ? 3 : key == KEY_RIGHT ? 5 : key == KEY_PAGEUP ? 0 : 2);
	} else if (key == KEY_PAGEUP || key == KEY_PAGEDOWN) {
		/* the sheet before, or the next */
		int pg = a->page + (key == KEY_PAGEUP ? -1 : 1);
		if (pg >= a->tab_pg0[a->tab] && pg < pad_end) {
			page_turn(a, pg);
		}
	} else if ((key == KEY_LEFT || key == KEY_RIGHT) && i >= 0 && SETTINGS[i].kind == S_CHOICE) {
		int n = 0;
		while (SETTINGS[i].words[n]) {
			n++;
		}
		int v = a->rows[i].value + (key == KEY_RIGHT ? 1 : -1);
		set_value(a, i, v < 0 ? 0 : (v >= n ? n - 1 : v));
	} else if (key == KEY_SPACE || key == KEY_ENTER) {
		use_row(a, i, -1);
	} else if (key == KEY_ESC) {
		a->closed = true;
	}
}

static void kbd_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t dep,
	uint32_t lat, uint32_t lock, uint32_t group)
{
	struct app *a = data;
	(void)k;
	(void)serial;
	a->mods = dep;
	(void)lat;
	(void)lock;
	(void)group;
}

static void kbd_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	(void)data;
	(void)k;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener kbd_listener = {
	.keymap = kbd_keymap,
	.enter = kbd_enter,
	.leave = kbd_leave,
	.key = kbd_key,
	.modifiers = kbd_modifiers,
	.repeat_info = kbd_repeat,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
	struct app *a = data;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !a->ptr) {
		a->ptr = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(a->ptr, &ptr_listener, a);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !a->kbd) {
		a->kbd = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(a->kbd, &kbd_listener, a);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface,
	uint32_t version)
{
	struct app *a = data;
	for (int i = 0; i < N_SETTINGS; i++) {
		if (SETTINGS[i].opens && SETTINGS[i].opens->needs
			&& !strcmp(iface, SETTINGS[i].opens->needs)) {
			a->offered[i] = true;
		}
	}
	if (!strcmp(iface, wl_compositor_interface.name)) {
		a->comp = wl_registry_bind(reg, name, &wl_compositor_interface,
			version < 4 ? version : 4);
	} else if (!strcmp(iface, wl_shm_interface.name)) {
		a->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (!strcmp(iface, xdg_wm_base_interface.name)) {
		a->wm = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(a->wm, &wm_listener, a);
	} else if (!strcmp(iface, zxdg_decoration_manager_v1_interface.name)) {
		a->decoman = wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
	} else if (!strcmp(iface, wl_seat_interface.name) && !a->seat) {
		a->seat = wl_registry_bind(reg, name, &wl_seat_interface, version < 4 ? version : 4);
		wl_seat_add_listener(a->seat, &seat_listener, a);
	}
}

static void reg_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data;
	(void)reg;
	(void)name;
}

static const struct wl_registry_listener reg_listener = {
	.global = reg_global,
	.global_remove = reg_remove,
};

/* desk-settings --check: the rows of the System pad, each with the program
 * it opens here -- or, with none, the package that would bring one, by
 * this system's name -- for the installers to show at the end, and for
 * anyone who wants to know, without a display. The compositor's part of a
 * row (Displays needs wlr-output-management) is not asked after. Returns
 * how many rows open nothing. */
static int check_rows(struct app *a)
{
	int missing = 0;
	for (int i = 0; i < N_SETTINGS; i++) {
		if (SETTINGS[i].opens) {
			a->offered[i] = true;
		}
	}
	actions_find(a);
	for (int i = 0; i < N_SETTINGS; i++) {
		const struct setting *st = &SETTINGS[i];
		if (st->kind == S_SECTION && i + 1 < N_SETTINGS && SETTINGS[i + 1].kind == S_ACTION) {
			printf("%s\n", st->label);
		}
		if (st->kind != S_ACTION || !st->opens) {
			continue;
		}
		const struct opens *o = st->opens;
		char path[600];
		if (a->found[i] >= 0 && find_program(o->progs[a->found[i]].bin, path, sizeof path)) {
			printf("  %-32s %s\n", st->label, path);
		} else {
			const char *pkg = o->pkg[g_pkg] ? o->pkg[g_pkg] : o->pkg[PKG_NOW];
			printf("  %-32s MISSING -- %s%s\n", st->label,
				pkg && *pkg ? "install " : "nothing packaged here does it", pkg ? pkg : "");
			missing++;
		}
	}
	if (missing) {
		printf("%d row%s open%s nothing.\n", missing, missing == 1 ? "" : "s",
			missing == 1 ? "s" : "");
	} else {
		printf("Every row opens something.\n");
	}
	return missing;
}

int main(int argc, char **argv)
{
	desk_compat();
	bool check = argc > 1 && !strcmp(argv[1], "--check");
	if (argc > 1 && !strcmp(argv[1], "--version")) {
		printf("%s\n", DESK_RELEASE);
		return 0;
	}
	if (argc > 1 && !check) {
		fprintf(stderr, "usage: desk-settings [--check | --version]\n");
		return strcmp(argv[1], "--help") && strcmp(argv[1], "-h") ? 2 : 0;
	}
	const char *slow = getenv("DESK_SLOW");
	if (slow && atof(slow) > 0.0) {
		g_slow = atof(slow);
	}
	struct app a;
	memset(&a, 0, sizeof a);
	a.w = a.own_w = FORM_W;
	a.pressed = a.pressed_opt = -1;
	a.opening = -1;
	a.date_row = -1;
	for (int i = 0; i < N_SETTINGS; i++) {
		const struct setting *st = &SETTINGS[i];
		a.rows[i] = (struct desk_form_row){
			st->kind == S_SECTION ? DESK_FORM_SECTION
				: st->kind == S_CHECK ? DESK_FORM_CHECK
				: st->kind == S_DATE ? DESK_FORM_DATE
				: (st->kind == S_ACTION || st->kind == S_PREVIEW)
					? DESK_FORM_ACTION : DESK_FORM_CHOICE,
			st->label, st->note, st->words, st->def, NULL, NULL };
		a.found[i] = -2;
	}
	/* the machine's page: from the heading of the part whose rows open
	 * programs to the end; the shell's, all before it */
	int sys = N_SETTINGS;
	for (int i = 0; i < N_SETTINGS; i++) {
		if (SETTINGS[i].kind == S_ACTION) {
			sys = i;
			break;
		}
	}
	while (sys > 0 && sys < N_SETTINGS && SETTINGS[sys].kind != S_SECTION) {
		sys--;
	}
	/* a sheet to each part: from its heading to the next */
	for (int i = 0; i < N_SETTINGS && a.npages < PAGES_MAX; i++) {
		if (i == 0 || SETTINGS[i].kind == S_SECTION) {
			a.pg_first[a.npages] = i;
			a.pg_tab[a.npages] = i >= sys;
			a.npages++;
		}
	}
	for (int pg = 0; pg < a.npages; pg++) {
		a.pg_n[pg] = (pg + 1 < a.npages ? a.pg_first[pg + 1] : N_SETTINGS) - a.pg_first[pg];
		a.tab_npg[a.pg_tab[pg]]++;
	}
	a.tab_pg0[1] = a.tab_npg[0];
	a.tab_open[0] = 0;
	a.tab_open[1] = a.tab_pg0[1];
	a.tab_hover = a.tab_pressed = -1;
	conf_path(a.path, sizeof a.path);
	conf_read(a.path, a.rows);
	if (g_date_had_year && !check) {
		/* a birthday kept with its year: the month and the day only, from
		 * now on, and the year gone from the file */
		for (int i = 0; i < N_SETTINGS; i++) {
			if (SETTINGS[i].kind == S_DATE && SETTINGS[i].key && a.rows[i].value > 0) {
				char md[8];
				snprintf(md, sizeof md, "%02d-%02d", DATE_M(a.rows[i].value) % 100,
					DATE_D(a.rows[i].value));
				conf_write(a.path, SETTINGS[i].key, md);
			}
		}
		g_date_had_year = false;
	}
	exe_dir_read();
	os_read();
	words_apply(&a);
	page_show(&a, 0);
	if (check) {
		return check_rows(&a) ? 1 : 0;
	}
	desk_sound_set_level(sound_level(&a));
	desk_sound_init("Shell Settings");

	a.dpy = wl_display_connect(NULL);
	if (!a.dpy) {
		fprintf(stderr, "desk-settings: no Wayland display to open on\n");
		return 1;
	}
	a.reg = wl_display_get_registry(a.dpy);
	wl_registry_add_listener(a.reg, &reg_listener, &a);
	wl_display_roundtrip(a.dpy);
	if (!a.comp || !a.shm || !a.wm) {
		fprintf(stderr, "desk-settings: the compositor lacks what a window needs\n");
		return 1;
	}
	/* what there is to open, now that it is known what the compositor offers */
	actions_find(&a);
	words_apply(&a);
	/* as tall as the tallest sheet, and the notes, as long as that fits on
	 * a desk */
	double tallest = NOTE_TOP + NOTE_STEP * (a.tab_npg[0] > a.tab_npg[1] ? a.tab_npg[0]
		: a.tab_npg[1]) + 12.0;
	for (int pg = 0; pg < a.npages; pg++) {
		struct desk_shell_form f = page_form(&a, pg);
		tallest = fmax(tallest, PAD_INK + desk_shell_form_height(ink_w(&a), &f) + 6.0);
	}
	tallest += TAB_H + PAD_WIRE + PAD_FOOT;
	a.h = a.own_h = tallest < FORM_H_MAX ? (int)ceil(tallest) : FORM_H_MAX;
	form_measure(&a);
#ifdef DESK_HAVE_WL_CURSOR
	a.cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"),
		getenv("XCURSOR_SIZE") && atoi(getenv("XCURSOR_SIZE")) > 0
			? atoi(getenv("XCURSOR_SIZE")) : 24, a.shm);
	a.cursor_surf = wl_compositor_create_surface(a.comp);
#endif
	a.surf = wl_compositor_create_surface(a.comp);
	a.xsurf = xdg_wm_base_get_xdg_surface(a.wm, a.surf);
	xdg_surface_add_listener(a.xsurf, &xsurf_listener, &a);
	a.top = xdg_surface_get_toplevel(a.xsurf);
	xdg_toplevel_add_listener(a.top, &top_listener, &a);
	xdg_toplevel_set_title(a.top, TR("Shell Settings"));
	xdg_toplevel_set_app_id(a.top, "desk-settings");
	xdg_toplevel_set_min_size(a.top, 320, 180);
	if (a.decoman) {
		/* the shell's frame: this is one of its sheets */
		a.deco = zxdg_decoration_manager_v1_get_toplevel_decoration(a.decoman, a.top);
		zxdg_toplevel_decoration_v1_set_mode(a.deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	wl_surface_commit(a.surf);

	conf_changed(a.path, &a.seen, &a.seen_size, &a.seen_there);
	/* the display's events, and once a second a look at the file */
	int fd = wl_display_get_fd(a.dpy);
	while (!a.closed) {
		while (wl_display_prepare_read(a.dpy) != 0) {
			if (wl_display_dispatch_pending(a.dpy) < 0) {
				a.closed = true;
				break;
			}
		}
		if (a.closed) {
			break;
		}
		wl_display_flush(a.dpy);
		struct pollfd pfd = { fd, POLLIN, 0 };
		/* sooner, when an action's "Opening…" is to go */
		int ms = 1000;
		if (a.anim_on || a.flip_on) {
			ms = 16;   /* a pad coming up, sheets going over */
		} else if (a.opening >= 0) {
			double left = OPENING_S - since(&a.opened_at);
			ms = left <= 0.0 ? 0 : left < 1.0 ? (int)ceil(left * 1000.0) : 1000;
		}
		int r = poll(&pfd, 1, ms);
		if (r > 0 && (pfd.revents & POLLIN)) {
			if (wl_display_read_events(a.dpy) < 0) {
				break;
			}
		} else {
			wl_display_cancel_read(a.dpy);
			if (r > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
				break;
			}
		}
		if (wl_display_dispatch_pending(a.dpy) < 0) {
			break;
		}
		if (conf_changed(a.path, &a.seen, &a.seen_size, &a.seen_there)) {
			conf_read(a.path, a.rows);
			words_apply(&a);
			form_measure(&a);
			desk_sound_set_level(sound_level(&a));
			draw(&a);
		}
		opening_done(&a);
		/* once the sheet is up, the programs' icons: looking them up can
		 * take a moment the first time */
		if (a.configured_once && !a.icons_done) {
			a.icons_done = true;
			icons_apply(&a);
			draw(&a);
		}
		if (a.dirty || a.anim_on || a.flip_on) {
			draw(&a);
		}
	}

	anim_end(&a);
	flip_end(&a);
	buffer_free(&a.buf[0]);
	buffer_free(&a.buf[1]);
	if (a.deco) {
		zxdg_toplevel_decoration_v1_destroy(a.deco);
	}
	xdg_toplevel_destroy(a.top);
	xdg_surface_destroy(a.xsurf);
	wl_surface_destroy(a.surf);
#ifdef DESK_HAVE_WL_CURSOR
	if (a.cursor_theme) {
		wl_cursor_theme_destroy(a.cursor_theme);
	}
	if (a.cursor_surf) {
		wl_surface_destroy(a.cursor_surf);
	}
#endif
	if (a.ptr) {
		wl_pointer_destroy(a.ptr);
	}
	if (a.kbd) {
		wl_keyboard_destroy(a.kbd);
	}
	if (a.seat) {
		wl_seat_destroy(a.seat);
	}
	if (a.decoman) {
		zxdg_decoration_manager_v1_destroy(a.decoman);
	}
	xdg_wm_base_destroy(a.wm);
	wl_shm_destroy(a.shm);
	wl_compositor_destroy(a.comp);
	wl_registry_destroy(a.reg);
	wl_display_disconnect(a.dpy);
	desk_sound_shutdown();
	return 0;
}
