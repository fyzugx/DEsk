# A drawer on every screen, and a false bottom

Branch: `drawer-per-screen-pr`, on top of the folded-windows fix
(`multimonitor-folded-sheets`), which it needs. Changes in `src/wm.c`, `src/draw.c`,
`include/desk.h`, `src/settings.c`, `src/i18n.c` and the docs.

## What was asked

- The drawer could only be opened on the desk's own screen. With more than
  one monitor it should open on any of them, with a setting to choose
  between **one drawer shared by every screen** and **a drawer for each
  screen**.
- A **false bottom**: a hidden compartment under the felt, opened from a
  click point in the drawer, which can be on in one drawer and off in
  another when there is a drawer for each screen.

## How it works

### Which drawer, on which screen

There is still one drawer animation and one set of scene nodes (the traced
drawer sheet and the layer of things lying in it). Only one drawer is out at
a time. What changes is where it is laid and what is in it:

- `drw_at` is the screen it comes out on (`""` for the desk's). On a screen
  beside the desk's, `drawer_side_frame` puts a frame the size of the desk's
  screen at that screen's foot, centred, and everything about the drawer
  (its slide, the felt, the row of things, hit tests) is measured in that
  frame. `drawer_local` turns a pointer position into it.
- `drw_open` is how far out the drawer is, wherever it is. `drawer_t`, which
  the room and everything else on the desk's screen reads, is now the desk
  screen's share of it (`drawer_share`): `drw_open` when the drawer is under
  the desk, `0` when it is beside it. The desk stays put while another
  screen's drawer is out.
- Asked for on another screen while it is out (Super+D, or a click on that
  screen's pull), it goes home first and then comes out there
  (`drw_next`, `drawer_home`).
- `drw_key`/`drw_floor` say whose drawer and which floor `drw[]` holds. The
  others wait in `drw_keep` and `drawer_switch` swaps them in and out. With
  one shared drawer the key is always `""`.

### The side screen

- `desk_draw_side_desk` takes a `pull` flag and lays the desk's front edge
  along the foot of the side screen, so there is a pull to click.
- The drawer coming out lifts the paper on that screen (windows, folded
  sheets and the bare boards) by as much as `drawer_lift` lifts the desk's
  (`drawer_side_lift`, `side_lift` in `desk_follow`), and shades the boards
  above it (`side_shade` in `overlay_place`).
- Under the lifted boards, `desk_draw_side_foot` shows the room below the
  desk's front edge, the way the desk's own screen does.
- A side screen's work area keeps clear of its pull, as the desk's does.

### The false bottom

- `desk_drawer_paint_hollow` paints a walnut compartment over the felt;
  `desk_drawer_paint_board` paints the felt as a board (cut from the traced
  drawer sheet) slid back by `fb_t`, with a brass ring in its front right
  corner. With the board down only the ring is drawn, over the traced felt.
- Pulling the ring (`fb_toggle`) slides the board back over 0.46 s and makes
  the hollow's things the ones the hand reaches; pulling it again brings the
  felt back. The board stops short of the desk's front edge, which covers
  the back third of the felt, so its edge and the ring stay in sight.
- Dropping anything you carry on the ring slips it through to the other
  floor.
- Going home always leaves the board down.

### Settings and files

| Key | Values | Default |
|---|---|---|
| `drawers` | `shared`, `each` | `shared` |
| `false_bottom` | `off`, `all`, `desk`, `side`, or screen names (`DP-1,HDMI-A-1`) | `off` |

With a shared drawer, any `false_bottom` other than `off` gives it one. Both
settings are on Shell Settings under DRAWER AND ALL APPS, in all seven
languages.

Each floor is kept in its own file: `drawer` (as before), `drawer@SCREEN`
for another screen's drawer, and the same names with `.hidden` for what is
under a false bottom. A new screen's drawer comes with what a new desk
drawer does; a hollow starts empty.

## Bugs found while testing

| Bug | Fix |
|---|---|
| `drawer_switch(server, server->drw_key, …)` copied the key onto itself with `snprintf`. Overlapping `snprintf` is undefined, and glibc emptied the string, so pulling the ring made a side screen's drawer forget whose it was. | Copy the name before writing it. |
| The board slid back under the desk's front edge, which hides the back of the felt with the drawer out, so neither it nor the ring could be seen. | Slide it back less (44% of the felt instead of 78%). |
| Black either side of a side screen's drawer, where the lifted boards had uncovered nothing. | `desk_draw_side_foot`, laid under the boards as they lift. |
| Changing `drawers` in Shell Settings did nothing the first time, because the setting was only remembered from the first re-read. | Remember it on the first load too. |
| The pull strip along a side screen's foot was much lighter than the boards above it. The boards there are a band of the desk repeated in mirrored copies, so the row just above the strip was whichever came round, often the shaded back of the desk, while the strip is the desk's well-lit front edge. | When the screen has a pull, the band is laid so it ends right where the strip begins, as it does on the desk. |
| The new settings didn't appear in Shell Settings on a desk run from the build folder: the screwdriver opened the installed `desk-settings` (found by its desktop file or on the PATH), which is older and doesn't know them. | `settings_open` tries the `desk-settings` next to the running desk first. An installed desk is unaffected, since its neighbour is the installed one. |
| A drawer snapped home from a side screen (setting changed, screen unplugged) left that screen's paper lifted, because `desk_follow` looked at where the drawer is now rather than where it had lifted. | Keep the lifted screen in `side_lift_on`. |

## Tested

Headless, two 1280x720 screens, with scripted `DESK_HAND` runs (the scripted hand from the Control Panel
branch) and grim
screenshots:

- each drawer coming out on its own screen with its own things
- Super+D on the other screen while it is out (home, then out there)
- clicking the desk while a side screen's drawer is out, and clicking a
  side screen's pull
- dragging a thing onto the ring, then pulling the ring both ways
- switching `drawers` from each to shared while a side drawer was out with
  its false bottom open

The same runs were repeated in the ASan + UBSan build with no errors. The
only leaks LeakSanitizer reports are caches the desk keeps for its whole
life (item pictures, fonts, the shelf), none of them in the new code. Not
tried: screens of different sizes, or more than two screens.
