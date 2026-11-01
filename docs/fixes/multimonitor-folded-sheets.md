# Folded windows on a second monitor

Branch: `multimonitor-folded-sheets` (against upstream v0.1.3, `ec16818`).
All changes are in `src/wm.c`.

## The bug

With more than one monitor, DEsk lets windows lie on any screen, but a
window folded into a sheet (Fold, minimize) could not stay on a screen
beside the desk's. A sheet folded there flew back to the desk's own screen,
and a sheet dragged onto another screen jumped back to the desk when it was
let go.

## Where it was

Everything about folded sheets used `work_area(server)`, which is only ever
the desk's own screen (`first_output`), where windows use
`screen_area(server, out)`, which is right for every screen:

| Where | What it did |
|---|---|
| `drawer_drop` | A sheet let go of was clamped to `work_area`: dropped on another screen, it snapped back to the nearest edge of the desk's. |
| `sheet_slot_xy`, `sheet_begin_fold` | A sheet being folded was always put in the row along the front of the desk's screen, whatever screen its window was on. |
| `desk_follow` | When the drawer opens or the camera turns to the bookcase, Control Panel or post box, every layer of paper moves with the desk. Windows on a side screen are moved back by as much so they stay put; sheets were not. |
| `windows_onto_screens` | A screen taken away puts its windows on one that is left; sheets on it were not moved. |

The last two did not show only because the first two kept every sheet on
the desk's screen.

## What was done

- **`sheet_screen`** (new): the screen a sheet lies on, or is on its way to,
  from the middle of where it comes to rest (`x1`, `y1`).
- **`sheet_alloc_slot`, `sheet_slot_xy`** take the screen. Each screen has
  its own row of places along its front, laid out in `screen_area` for that
  screen; on the desk's this is the same as before.
- **`sheet_begin_fold`** takes the screen the window is on (`view_fold_toggle`
  passes `view_screen(view)`), so a window folded on a side screen lies
  along the front of that screen. Folding straight into the drawer
  (`fold_into_drawer = yes`) still goes to the desk, where the drawer is.
- **`drawer_drop`** clamps to the screen under the pointer, and gives the
  sheet a place in that screen's row if it changed screens.
- **`desk_follow`** moves sheets on a side screen back by as much as the
  desk moved them, as it does for windows (not the one in the hand, which
  follows the pointer, nor one on its way into the drawer).
- **`windows_onto_screens`** puts a sheet left on a screen that has gone in
  the row of the nearest screen that is left.
- `drawer_lift_sheet` and `drawer_put_in` (a sheet taken out of the drawer,
  or the drawer full) keep using the desk's screen.

### Fixed along the way

- **The drawer sprang open from another screen.** Carrying a sheet low on
  a side screen opened the desk's drawer, because `drawer_carry_move`
  checked only the pointer's height. It now checks that the pointer is on
  the desk's screen (`pointer_on_side`).
- **A folded window on a side screen opened out to the wrong place** after
  the drawer or camera had moved. `desk_follow` moved the hidden window back
  but not the sheet's `x0`, `y0` (where it opens out to); it now moves both.
  `windows_onto_screens` likewise updates them when it moves a folded
  window.

## Bugs found in testing

Tested nested in a running session, with two outputs (below).

1. **A sheet on a side screen could not be picked up or opened.** Sheets now
   folded and stayed on the second screen, but clicking one did nothing,
   and it could not be dragged. In `server_cursor_button`, a press on a side
   screen went straight to `paper_press` (windows) and never reached
   `drawer_press`, which is what takes hold of a sheet. Upstream had the
   same gap; it never showed because no sheet could get there. The press
   now tries `drawer_press` first, as on the desk's screen. (Moving and
   letting go were already handled before the side-screen check.)
2. **Sheets on a side screen were out of reach while the desk's screen was
   turned to the bookcase, the Control Panel or the post box.** Found while
   fixing the first: `drawer_press` refused everything while one of those
   was up. The README says a screen beside the desk's stays in reach
   whatever the desk's own is turned to, so the check now applies only on
   the desk's screen.

## Testing

Built against the wlroots 0.18 that DEsk's Ubuntu installer puts in
`/opt/desk-wlroots`, and run nested with two screens and settings of its
own, so the session it runs in is not touched:

```sh
PKG_CONFIG_PATH=/opt/desk-wlroots/lib/pkgconfig meson setup build-dev
ninja -C build-dev
D=$PWD/build-dev/testhome; mkdir -p $D/config $D/data $D/state
DESK_ALLOW_MULTIPLE=1 WLR_BACKENDS=wayland WLR_WL_OUTPUTS=2 \
	XDG_CONFIG_HOME=$D/config XDG_DATA_HOME=$D/data XDG_STATE_HOME=$D/state \
	./build-dev/desk --size 1280x800
```

WL-1 is the desk's screen, WL-2 the one beside it; programs started with
`WAYLAND_DISPLAY=desk-0` open on the screen the pointer is on.

- Seen: a window folded on WL-2 stays on WL-2.
- Not yet seen since the fixes for the testing bugs: opening and dragging
  a sheet on WL-2, dragging one between screens, the drawer and the
  bookcase with a sheet on WL-2, and a screen taken away.

`src/wm.c` builds with no warnings at the project's warning level
(`-Wall -Wextra`).
