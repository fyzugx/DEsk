#ifndef DESK_DEVICES_H
#define DESK_DEVICES_H

#include <stdbool.h>

/* What is behind the Control Panel's levers and its knob, to be picked
 * from a card: the wireless networks in range, the Bluetooth devices, and
 * the sound devices to play to and record from.
 *
 * DEsk only draws the card. The work is done by the programs that already
 * do it -- nmcli (NetworkManager), bluetoothctl (BlueZ) and pactl
 * (PipeWire or PulseAudio) -- run on a thread of their own, because a
 * wireless scan takes seconds and the compositor cannot stop for it. A
 * password a network asks for is asked by the network applet's own dialog
 * (nm-applet is NetworkManager's agent in a DEsk session), and pairing is
 * confirmed by blueman's. */

enum desk_dev_kind {
	DESK_DEV_WIFI,
	DESK_DEV_BT,
	DESK_DEV_SINK,     /* where sound is played */
	DESK_DEV_SOURCE,   /* where it is recorded from */
	DESK_DEV_KINDS,
};

enum desk_dev_action {
	DESK_DEV_CONNECT,     /* Wi-Fi: join; Bluetooth: connect (pairing first if need be) */
	DESK_DEV_DISCONNECT,
	DESK_DEV_DEFAULT,     /* sound: make it the one used */
	DESK_DEV_SCAN,        /* look again for what is in range */
};

#define DESK_DEV_MAX 40

struct desk_dev {
	char id[160];      /* what it is acted on by: SSID, address, node name */
	char label[128];   /* what it is called */
	char conn[128];    /* Wi-Fi: its saved connection's name, if it has one */
	int signal;        /* Wi-Fi: 0..100; otherwise -1 */
	bool secure;       /* Wi-Fi: it wants a password */
	bool known;        /* Wi-Fi: saved; Bluetooth: paired */
	bool active;       /* connected; for sound, the one in use */
};

struct desk_dev_list {
	int n;
	struct desk_dev d[DESK_DEV_MAX];
	bool have;         /* the program it is read with is on this machine */
	bool read;         /* it has been read at least once */
	bool busy;         /* being read or acted on now */
	bool off;          /* Wi-Fi or Bluetooth: its radio is off */
	bool failed;       /* the last thing asked of it did not happen */
};

struct wl_event_loop;
/* Once. changed is called on the compositor's thread whenever a list has
 * been read again, or something asked of one is done. */
void desk_devices_init(struct wl_event_loop *loop, void (*changed)(void *data), void *data);
/* Read a list again (the two sound lists are read together). */
void desk_devices_refresh(enum desk_dev_kind kind);
/* Do something to one (id from its list; NULL for a scan), and read the
 * list again after. */
void desk_devices_act(enum desk_dev_kind kind, enum desk_dev_action action, const char *id);
/* The list as last read. */
void desk_devices_get(enum desk_dev_kind kind, struct desk_dev_list *out);

#endif
