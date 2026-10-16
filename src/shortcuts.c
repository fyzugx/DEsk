#include "shortcuts.h"
#include <string.h>
#include <strings.h>
static const char *const names[DESK_SC_COUNT] = {
 "terminal", "lock", "close", "cycle", "minimize", "drawer", "apps",
 "controls", "notifications", "maximize", "settings", "switch", "switch_reverse"
};
static const char *const defaults[DESK_SC_COUNT] = {
 "Return", "l", "q", "Tab", "m", "d", "a", "c", "n", "Up", "comma", "Tab", "ISO_Left_Tab"
};
void desk_shortcuts_defaults(struct desk_shortcuts *s, desk_key_resolver resolve)
{
 for (int i = 0; i < DESK_SC_COUNT; i++)
  s->keys[i] = (struct desk_shortcut){resolve(defaults[i]), i == DESK_SC_SWITCH ? DESK_SC_ALT
   : i == DESK_SC_SWITCH_REVERSE ? DESK_SC_ALT | DESK_SC_SHIFT : DESK_SC_SUPER};
}
const char *desk_shortcut_set(struct desk_shortcuts *s, const char *action,
 const char *value, desk_key_resolver resolve)
{
 int index = -1;
 for (int i = 0; i < DESK_SC_COUNT; i++)
  if (!strcmp(action, names[i])) index = i;
 if (index < 0) return "unknown action";
 struct desk_shortcut key = {0};
 if (strcasecmp(value, "none")) {
  if (strlen(value) >= 128) return "binding too long";
  char buf[128];
  strcpy(buf, value);
  char *part = buf, *next;
  while ((next = strchr(part, '+'))) {
   *next = 0;
   uint32_t mod = !strcasecmp(part, "Super") ? DESK_SC_SUPER
    : !strcasecmp(part, "Alt") ? DESK_SC_ALT
    : !strcasecmp(part, "Ctrl") ? DESK_SC_CTRL
    : !strcasecmp(part, "Shift") ? DESK_SC_SHIFT : 0;
   if (!mod || (key.mods & mod)) return "invalid or repeated modifier";
   key.mods |= mod;
   part = next + 1;
  }
  if (!(key.mods & (DESK_SC_SUPER | DESK_SC_ALT | DESK_SC_CTRL)))
   return "Super, Alt or Ctrl required";
  key.sym = *part ? resolve(part) : 0;
  if (!key.sym) return "unknown or missing key";
  if ((key.mods & DESK_SC_SHIFT) && key.sym == resolve("Tab"))
   key.sym = resolve("ISO_Left_Tab");
  for (int i = 0; i < DESK_SC_COUNT; i++)
   if (i != index && key.sym == s->keys[i].sym && key.mods == s->keys[i].mods)
    return "binding already assigned";
 }
 s->keys[index] = key;
 return NULL;
}
int desk_shortcut_match(const struct desk_shortcuts *s, uint32_t sym, uint32_t mods)
{
 for (int i = 0; i < DESK_SC_COUNT; i++)
  if (sym && s->keys[i].sym == sym && s->keys[i].mods == mods) return i;
 return -1;
}

bool desk_super_tap_event(struct desk_super_tap *tap, uint32_t code,
 bool is_super, bool pressed, bool allowed)
{
 if (!allowed) tap->pending = false;
 if (pressed) {
  if (is_super && !tap->code) {
   tap->code = code;
   tap->pending = allowed;
  } else {
   tap->pending = false;
  }
  return false;
 }
 if (tap->code && code == tap->code) {
  bool activate = tap->pending && allowed;
  tap->code = 0;
  tap->pending = false;
  return activate;
 }
 return false;
}
