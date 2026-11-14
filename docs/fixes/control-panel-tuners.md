# Choosing networks, Bluetooth devices and sound devices on the Control Panel

Branch: `control-panel-card`, against upstream v0.1.5.

## What was missing

The Control Panel could turn Wi-Fi and Bluetooth on and off and set the
volume, but not choose anything: no network to join, no Bluetooth device to
connect, no speaker or microphone to use. The way to do that existed --
the other mouse button on a control opens `nm-connection-editor`,
`blueman-manager` or `pavucontrol` -- but nothing on the panel said so.

## What it does now

- **CHOOSE buttons.** A brass push button beside the wireless lever, the
  Bluetooth lever and the volume knob. Pushed, it stays down while its tuner
  is out; another one swaps the tuner; the same one again puts it away. The
  word under each control, and the network meter, do the same.
- **The card.** A walnut card, as thick as a finger, rises out of a brass
  slot along the top of the set and stands above it, a little past its
  stop and back onto it with a click; the set sinks down the screen as it
  rises to make the room, so nothing on the set is covered. Esc, or the
  same CHOOSE, sinks it back into the slot and the set comes back up; a
  click off the machine turns back to the desk.
- **The tuner.** A switchboard: a jack for each one in the list, its name
  on an ivory strip under it and a lamp over it (green in use, amber saved
  or paired, blinking while it is being put through, red when it did not
  go), with the signal and a padlock for a network. The one in use, then
  the saved or paired ones, come first; the order is taken when the card
  comes out, when a scan is back and when the band is changed, and then
  kept, so a jack never runs along the row because it was just connected.
  A braided cord comes up through the shelf to a brass plug in the jack of
  the one in use. Pull the plug out and put it in another jack to take
  that one; put it in the hole on the shelf to let go (Wi-Fi and
  Bluetooth) or, on the sound card, to mute the output or the microphone
  -- sound always has a device in use, so its "nothing" is silence -- and
  the plug stays in the hole, the readout saying Muted, until it is put
  in a jack again, which unmutes (Return does the same both ways); let it
  go anywhere else and it swings back. Ten jacks are on
  the board at once: the arrows, the wheel and the arrow keys move a jack
  at a time, the stops under the row (1-10, 11-20...) and Page Up/Down a
  board at a time, and a plug held at an end of the row slides it along.
  Return takes the jack the keys are on, so the plug is never the only
  way. The readout on the left names that jack; the one on the right
  always shows what is in use now. RESCAN (Wi-Fi), SEARCH (Bluetooth) or
  an Output/Input band switch (sound) on the left, Settings on the right.
- **The work is borrowed.** `src/devices.c` runs `nmcli`, `bluetoothctl` and
  `pactl` on a thread of its own, never through a shell, in the C locale. A
  Wi-Fi password is asked by nm-applet's dialog (NetworkManager's agent in a
  DEsk session); pairing is confirmed by blueman's.

## How it is made

Nothing about the room changes: no new geometry in either tracer, no new
traced layer, no change to `room.glsl`, so nothing more is traced on any
screen size and the CHOOSE buttons and the card work as soon as the set
does.

- The CHOOSE buttons are drawn pieces of the set, as the needles and lamps
  are (`DESK_CCP_CHOOSE`), placed on the panel by the same projection.
- The card is drawn by `desk_draw_tuner` into one picture and put up in the
  console's layer above everything live on the set (`ccd_paint`), drawn
  again only when what is on it changes -- the list, the jack the keys are
  on, the row sliding, a lamp. It stands in the slot along the top of the
  instrument tower (`desk_console_card_place`: the tower's top edge,
  projected through the set's camera), and the slot is drawn on the set's
  sheet. There is seldom room above the set, so the console's whole layer
  is moved down while the card is out (`cc_pan_set`, as far as
  `pl->pan`, never so far that the bottom row of controls and its lamps
  leave the screen; the card is made smaller only if that is not
  enough). The set's hit tests look that much higher up it
  (`desk_console_set_pan`); the strip this bares at the top of the screen
  is covered with the wall, made once from the sheet's top rows
  (`cc_wall_make`). The tube's brass bezel stands proud of the tower and
  comes up past its top edge, so the slot is cut in a low walnut rail on
  top of the tower that stands clear of the bezel (the bezel left in front
  of the rail's foot): the card never comes up out of the bezel. The
  output's and the microphone's mute come from `wpctl`
  (`desk_meters_set_muted`, `desk_meters_set_mic_muted`); an answer from
  the mixer that may predate a change made here is not taken. On the way
  (`ccd_pose`) the card is only moved and cut
  off at the slot's lip, never drawn again; the hand has it only once it
  has reached its stop. When the card goes at once (the set turned away
  from, the desk locked), the set eases back up rather than jumping.
- The cord is a picture of its own over the card's, only as big as the
  cord and its plug (`ccd_cord_put`, `desk_draw_cord`: one path, stroked
  twice). While it moves, only that picture is drawn again and sent, never
  the board. It is a rope of 14 points (`ccd_cord_step`): Verlet, gravity,
  damping, three passes of the lengths a step, at a fixed 1/120 s step and
  no more than four steps a frame, drawn at no more than 60 frames a
  second, or 30 while drawing it costs more than half a frame. What it
  does not need is taken up under the shelf, so it hangs a little slack at
  any reach. It is stepped only while something moves it; still for a
  quarter of a second, it is left be, and nothing ticks.
- The cord and the busy lamps keep real time (`real_sec`). In 2D -- which
  the desk picks by itself when the screen is drawn on the processor --
  the desk's own clock runs a hundred times fast so that everything that
  eases gets there at once, and a cord swung by the hand cannot. The lamps
  blink four times a second on a timer of their own, which stops when
  nothing is being put through; the whole card used to be drawn again on
  every frame while anything was busy.
- Both pictures, and the timer, are let go of when the card is put away.
- Input goes to the card first (`ccd_press`, `ccd_motion`, `ccd_release`,
  `ccd_axis`, `ccd_keys`); anything off it is the set's, all still there
  under it. The plug in the hand goes back where it came from when the
  card goes, the desk locks or is set right, another button is pressed, or
  the list is read again with other jacks on it (`ccd_let_go`). It acts
  only when it is let go, once.

## Testing

- Headless, drawn on the processor (pixman, so in 2D), at 1600x1000,
  1280x720 and 2560x1440 with `DESK_SCALE=1.5`, with stand-ins for
  `nmcli`, `bluetoothctl` and `pactl` first on `PATH` (30 networks that
  grow by three when a scan is back, three paired devices, four outputs;
  one network, one device and one output that refuse). The scripted hand
  (`DESK_HAND`, with `plug`, `jack`, `hole` and `by`, see
  `docs/MODDING.md`) opens each tuner and pulls the plug over several
  jacks, to the end of the row (which slides), into a jack, onto nothing,
  into one that refuses, into the hole; uses the arrows, Page Up/Down and
  Return; flips the left-hand button; presses again with the plug in
  hand; shuts the card, and locks the desk, with the plug in hand; and the
  list changes under a held plug. The screen was photographed at each
  step. Each seat, Return or hole called the backend once.
- What it costs, on the processor at 1600x1000 (Ryzen 7 9800X3D, one core
  of it): with the card out and nothing moving, the desk makes the same 4
  frames a second as with no card out (the set's own meters); dragging
  the plug, 58 frames a second at about 1 ms a frame, 7% of a core;
  settling, 2%. Held to a quarter of one core and 1 GB (`systemd-run
  -p CPUQuota=25% -p MemoryMax=1G`, `taskset`) at 1280x720: the same 60 a
  second, 6% of that core. Not tried on a Raspberry Pi.
- Under AddressSanitizer, a whole open, drag, seat, close: no errors; the
  only reports are cairo's font cache, as everywhere else in the desk.
  Both pictures and the timer are gone after the card is put away.
- Not tried: joining a new password-protected network, and pairing a new
  Bluetooth device, which rely on nm-applet's and blueman's agents.
