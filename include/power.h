#ifndef DESK_POWER_H
#define DESK_POWER_H

#include <stdbool.h>

/* What the buttons under the lid do.
 *
 * Logging out is not here: it is the compositor stopping, which it can do
 * by itself and does not need to ask anybody about. */
enum desk_power {
	DESK_POWER_OFF,
	DESK_POWER_REBOOT,
	DESK_POWER_SUSPEND,   /* the machine to sleep, in its memory */
};

/* Ask logind. Returns false if it would not, or could not be reached --
 * the caller is then holding a dark screen and had better say so. With
 * DESK_POWER_DRY in the environment (a desk being tried out) it is only
 * said, in the log, and done by nobody. */
bool desk_power_request(enum desk_power what);

/* Power saving, as the power-profiles daemon has it: 1 its power-saver
 * profile is on, 0 another, -1 there is no daemon to ask (or no such
 * profile). Set it on, or back to balanced: false if it would not. */
int desk_power_saver(void);
bool desk_power_saver_set(bool on);

/* Turn the screen's backlight (panel.c's choice of device) to v, 0..1 of
 * its scale -- never quite to nothing, which is a black screen with no way
 * to see to turn it back up. Through logind, which lets the user at the
 * seat set it; or else straight into sysfs, where that file is writable
 * (a udev rule, or DESK_SYSFS's tree when trying it). False if neither
 * would have it. */
bool desk_backlight_set(double v);

#endif
