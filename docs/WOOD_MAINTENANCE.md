# Wood Maintenance (experimental)

Wood Maintenance is an optional, entirely cosmetic termite screensaver for DEsk.
The animation starts after the configured idle interval. Termites explore the wooden
work surface and exposed paper corners, leave irregular bite marks, and scatter
when the pointer moves. No application content, on-disk documents or wallpaper
files are modified.

## Shell Settings

Under **DEsk**:

- **Wood Maintenance (termites)**: enabled by default; disable to suppress idle visits.
- **Termites start after**: 30 seconds, 1 minute (default), 2 minutes or 5 minutes.
- **Infestation**: One curious guest (1 termite), Small colony (6),
  Call the exterminator (18). Population adjusts on the next compositor tick.
- **Nibble open window corners** and **Nibble folded applications**:
  enable or disable those cosmetic target regions separately.
- **Keep termite damage until restored**: on by default. Saves a bounded
  record of marks in `$XDG_STATE_HOME/desk/wood-damage-v1.txt` (or
  `~/.local/state/desk/wood-damage-v1.txt`). Marks survive logout and login.
- **Restore Desk**: clear the cosmetic damage and remove its saved record.
- **Preview Wood Maintenance**: start immediately; there is no preview timeout.
  Pointer movement begins a brief scatter-and-disappear animation.

The rendered bite marks are a screen-coordinate overlay. They do not yet track
individual windows when those windows move, and restoring does not change
application files. Saved damage is bounded to 2,048 marks.

## Manual test checklist

1. Start Preview, leave the pointer still, and confirm insects reach the chosen
   population size. Watch multiple destinations and varied chewing.
2. Move the pointer. Insects should scatter for roughly a second, leaving the
   chewing marks on screen.
3. Close and restart the DEsk session. Bite marks should still be visible.
4. Choose **Restore Desk** and verify marks disappear and Shell Settings remains usable.
5. Disable persistent damage; confirm a later infestation cleans up on departure.
6. Test full-screen apps and the lock screen. Termites and marks must not overlay them.

## Implementation notes

The renderer lives in `src/wood_maintenance.inc` because it needs the compositor's
window geometry, scene graph and idle state. Its marks are a separate scene layer
that is repainted on changes rather than on every animation frame.

The feature was exercised in a modified DEsk v0.1.2 development session;
the upstream-rebased version of this change still needs a fresh build and
regression test before merging.
