#ifndef DESK_SHORTCUTS_H
#define DESK_SHORTCUTS_H
#include <stdbool.h>
#include <stdint.h>
enum desk_shortcut_action {
 DESK_SC_TERMINAL, DESK_SC_LOCK, DESK_SC_CLOSE, DESK_SC_CYCLE,
 DESK_SC_MINIMIZE, DESK_SC_DRAWER, DESK_SC_APPS, DESK_SC_CONTROLS,
 DESK_SC_NOTIFICATIONS, DESK_SC_MAXIMIZE, DESK_SC_SETTINGS, DESK_SC_SWITCH, DESK_SC_SWITCH_REVERSE, DESK_SC_COUNT
};
enum { DESK_SC_SUPER = 1, DESK_SC_ALT = 2, DESK_SC_CTRL = 4, DESK_SC_SHIFT = 8 };
struct desk_shortcut { uint32_t sym, mods; };
struct desk_shortcuts { struct desk_shortcut keys[DESK_SC_COUNT]; };
typedef uint32_t (*desk_key_resolver)(const char *name);
void desk_shortcuts_defaults(struct desk_shortcuts *, desk_key_resolver);
/* Invalid values/conflicts leave the previous binding intact. */
const char *desk_shortcut_set(struct desk_shortcuts *, const char *action,
 const char *value, desk_key_resolver);
int desk_shortcut_match(const struct desk_shortcuts *, uint32_t sym, uint32_t mods);
/* Super tap state: any intervening key cancels activation. */
struct desk_super_tap { uint32_t code; bool pending; };
bool desk_super_tap_event(struct desk_super_tap *, uint32_t code,
 bool is_super, bool pressed, bool allowed);
#endif
