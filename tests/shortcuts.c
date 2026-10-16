#include "shortcuts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
/* Independent symbolic keys: tests need neither a display nor xkbcommon. */
static uint32_t resolve(const char *name)
{
 const char *names[] = {"Return", "l", "q", "Tab", "m", "d", "a", "c", "n", "Up", "comma", "F9", "ISO_Left_Tab"};
 for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
  if (!strcasecmp(name, names[i])) return i + 1;
 return 0;
}
int main(void)
{
 struct desk_shortcuts s;
 desk_shortcuts_defaults(&s, resolve);
 assert(desk_shortcut_match(&s, resolve("Return"), DESK_SC_SUPER) == DESK_SC_TERMINAL);
 assert(desk_shortcut_match(&s, resolve("Return"), DESK_SC_SUPER | DESK_SC_CTRL) == -1);
 assert(desk_shortcut_match(&s, 0, 0) == -1);
 assert(!desk_shortcut_set(&s, "terminal", "Ctrl+Alt+F9", resolve));
 assert(desk_shortcut_match(&s, resolve("Return"), DESK_SC_SUPER) == -1);
 assert(desk_shortcut_match(&s, resolve("F9"), DESK_SC_CTRL | DESK_SC_ALT) == DESK_SC_TERMINAL);
 assert(desk_shortcut_set(&s, "lock", "Ctrl+Alt+F9", resolve));
 assert(desk_shortcut_match(&s, resolve("l"), DESK_SC_SUPER) == DESK_SC_LOCK);
 const char *bad[] = {"F9", "Shift+F9", "Super+", "Super+Super+F9", "Meta+F9", "Super+unknown", "+F9", ""};
 for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
  assert(desk_shortcut_set(&s, "lock", bad[i], resolve));
  assert(desk_shortcut_match(&s, resolve("l"), DESK_SC_SUPER) == DESK_SC_LOCK);
 }
 assert(desk_shortcut_set(&s, "bogus", "Super+F9", resolve));
 char long_value[200]; memset(long_value, 'x', sizeof long_value); long_value[199] = 0;
 assert(desk_shortcut_set(&s, "lock", long_value, resolve));
 assert(!desk_shortcut_set(&s, "terminal", "none", resolve));
 assert(!desk_shortcut_set(&s, "lock", "ctrl+alt+f9", resolve));
 assert(desk_shortcut_match(&s, resolve("F9"), DESK_SC_CTRL | DESK_SC_ALT) == DESK_SC_LOCK);
 desk_shortcuts_defaults(&s, resolve);
 assert(desk_shortcut_match(&s, resolve("Up"), DESK_SC_SUPER) == DESK_SC_MAXIMIZE);
 assert(desk_shortcut_match(&s, resolve("Tab"), DESK_SC_ALT) == DESK_SC_SWITCH);
 assert(desk_shortcut_match(&s, resolve("ISO_Left_Tab"), DESK_SC_ALT | DESK_SC_SHIFT) == DESK_SC_SWITCH_REVERSE);
 assert(!desk_shortcut_set(&s, "switch_reverse", "Alt+Shift+Tab", resolve));
 assert(desk_shortcut_match(&s, resolve("ISO_Left_Tab"), DESK_SC_ALT | DESK_SC_SHIFT) == DESK_SC_SWITCH_REVERSE);
 assert(!desk_shortcut_set(&s, "switch", "none", resolve));
 assert(desk_shortcut_match(&s, resolve("Tab"), DESK_SC_ALT) == -1);
 struct desk_super_tap tap = {0};
 assert(!desk_super_tap_event(&tap, 133, true, true, true));
 assert(desk_super_tap_event(&tap, 133, true, false, true));
 assert(!desk_super_tap_event(&tap, 133, true, false, true));
 /* Super+another key, repeats, and lock/disabled states cannot open apps. */
 assert(!desk_super_tap_event(&tap, 133, true, true, true));
 assert(!desk_super_tap_event(&tap, 24, false, true, true));
 assert(!desk_super_tap_event(&tap, 24, false, false, true));
 assert(!desk_super_tap_event(&tap, 133, true, false, true));
 assert(!desk_super_tap_event(&tap, 133, true, true, true));
 assert(!desk_super_tap_event(&tap, 133, true, true, true));
 assert(!desk_super_tap_event(&tap, 133, true, false, true));
 assert(!desk_super_tap_event(&tap, 134, true, true, true));
 assert(!desk_super_tap_event(&tap, 134, true, false, false));
 assert(!desk_super_tap_event(&tap, 133, true, true, false));
 assert(!desk_super_tap_event(&tap, 133, true, false, true));
 /* Two Super keys held together cancel the tap. */
 assert(!desk_super_tap_event(&tap, 133, true, true, true));
 assert(!desk_super_tap_event(&tap, 134, true, true, true));
 assert(!desk_super_tap_event(&tap, 134, true, false, true));
 assert(!desk_super_tap_event(&tap, 133, true, false, true));
 puts("shortcuts: passed");
 return 0;
}
