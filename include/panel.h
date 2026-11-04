#ifndef DESK_METERS_H
#define DESK_METERS_H

/* What the instruments on the console are reading.
 *
 * Everything here comes off the machine itself. Where the machine has no
 * such thing -- this one has no battery and no backlight -- the instrument
 * says so rather than inventing a number, because a meter that reads a
 * number nobody measured is worse than a meter that reads LINE. */

#include <stdbool.h>
#include <stddef.h>

struct desk_meters {
	double cpu;        /* 0..1, busy fraction since the last read */
	double mem;        /* 0..1 */
	double temp_c;     /* degrees; < -100 when nothing measures it */
	double load;       /* one minute load average, per core */

	bool net_up;       /* a carrier, and a route to somewhere */
	bool net_routed;
	double net_rate;   /* 0..1, recent traffic against a rough ceiling */
	char net_if[32];
	bool net_wireless; /* the route goes out of a radio, not a cable */
	double net_signal; /* 0..1 link quality; only means anything on a radio */

	bool have_battery;
	double battery;    /* 0..1 */
	bool on_mains;

	bool have_volume;
	double volume;     /* 0..1 */
	bool muted;

	bool have_radio;   /* any rfkill switch at all */
	bool wifi_blocked;
	bool bt_blocked;
};

/* A handful of small reads under /proc and /sys, plus the volume no more
 * than a few times a second. Small, but not always quick: a graphics
 * card's sensors are asked through its driver, which can take twenty
 * milliseconds to answer. */
void desk_meters_read(struct desk_meters *out);
/* The same without the wait, for a tick: the last reading, at once, and
 * a new one asked for on a thread of its own if that one is older than
 * a tick of the meters. */
void desk_meters_latest(struct desk_meters *out);

/* Returns false when the machine has no mixer to set. Both return at
 * once; the mixer is set on a thread of its own, the latest value first.
 * how: 0 unmute, 1 mute, 2 the other way from what it is. */
bool desk_meters_set_volume(double v);
bool desk_meters_set_muted(int how);
/* Only the mixer: a second behind it at worst, never a wait. */
void desk_meters_read_volume(struct desk_meters *out);

/* What the machine is running on, and what it draws. Anything unknown is
 * negative: a desktop has no battery, and nothing that measures what it
 * draws off the wall, so draw_w is usually the processor (RAPL, if it is
 * readable at all) and a graphics card that reports itself -- and named
 * in source, so nobody takes it for the whole machine. */
struct desk_supply {
	bool have_battery;
	double battery;        /* 0..1 */
	bool charging, discharging, full;
	bool on_mains;
	double batt_w;         /* into or out of the battery; < 0 unknown */
	double hours;          /* to empty, or to full; < 0 unknown */
	double draw_w;         /* what the machine draws; < 0 nothing measures it */
	char source[16];       /* what draw_w is: BATTERY, CPU, GPU, CPU+GPU */
	bool have_backlight;   /* a screen whose light the machine sets: a laptop's */
	double backlight;      /* 0..1 */
};
void desk_supply_read(struct desk_supply *out);
/* The backlight that reading is of: its name under class/backlight, its
 * full scale and where it is now. False when there is none. */
bool desk_backlight_find(char *name, size_t n, double *max, double *now);
/* where sysfs is: /sys, or DESK_SYSFS's tree */
const char *desk_sys_root(void);

/* Throw a radio's soft block. False when there is no such radio, or when
 * the machine will not let us touch it -- in which case the lever on the
 * panel is a lever and nothing more. */
bool desk_meters_set_radio(bool wifi, bool on);

#endif
