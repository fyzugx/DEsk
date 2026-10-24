# Choosing networks, Bluetooth devices and sound devices on the Control Panel

Branch: `control-panel-cards` (against upstream v0.1.3, `ec16818`).

## What was missing

The Control Panel could turn Wi-Fi and Bluetooth on and off and set the
volume, but not choose anything: no network to join, no Bluetooth device to
connect, no speaker or microphone to use. The way to do that existed --
the other mouse button on a control opens `nm-connection-editor`,
`blueman-manager` or `pavucontrol` -- but nothing on the panel said so.

## What it does now

- **CHOOSE buttons.** A brass push button beside the wireless lever, the
  Bluetooth lever and the volume knob. Pushed, it stays down while its tuner
  is up; another one swaps the tuner; the same one again puts it away. The
  word under each control, and the network meter, do the same.
- **The lid.** A walnut lid lies in a recess in the top of the console, its
  back level with the top and its ends in cuts in the cheeks. On CHOOSE the
  view eases back, then the lid folds up on brass hinges along the back of
  the top to stand at the instrument face's slant, the tuner in a brass
  bezel on its inside. Esc, or the same CHOOSE, shuts it; a click off the
  machine shuts it and turns back to the desk.
- **The tuner.** A radio tuning dial: names round a cream scale under a
  fixed red pointer, a knob that rolls the scale (wheel, drag, arrow keys)
  and is pressed (or Return) to take the one under the pointer. A readout
  on the left names that one and how it stands; the one on the right always
  shows what is in use now. RESCAN (Wi-Fi), SEARCH (Bluetooth) or an
  Output/Input band switch (sound) on the left, Settings on the right.
- **The work is borrowed.** `src/devices.c` runs `nmcli`, `bluetoothctl` and
  `pactl` on a thread of its own, never through a shell, in the C locale. A
  Wi-Fi password is asked by nm-applet's dialog (NetworkManager's agent in a
  DEsk session); pairing is confirmed by blueman's.

## How it is made

- **Geometry, in both tracers** (`draw.c` and `room.glsl`): the walnut top
  of the tower, the recess and the cheek cuts, and the lid (`map_lid`), shut
  in every sheet but two.
- **Two more traced layers**, after the views and before the films, kept on
  the disk like them: the console without its lid through a lens wider from
  the same camera position (layer 7) -- the view's own sheet is exactly its
  middle, so pulling back is the picture growing smaller round its middle;
  and the lid on its own, open by `g_cc_lid`, 16 frames of its folding
  (layer 6), each traced only through the lid's part of the frame
  (`room_cam_window`) and stacked in one strip.
- **The pull-back** is in `console_xform`, so every piece drawn on the set
  and every point picked on it follows it. In the compositor, every picture
  is drawn once and moved by the scene graph while the view moves; only the
  tuner is drawn again, when what is on it changes.
- **The tuner** is laid on the lid's window in thin bands following the
  window's sides, so its corners land on the bezel's at any angle.

## Bugs found on the way

In DEsk as it was (they show up with the panel up while the screen changes,
or while anything moves on it):

1. **The panel's details vanished after a resize.** The traced base was
   destroyed and made again on top of the live layer, covering it. It is now
   put back under it (`view_base_under`); the post box had the same fault.
2. **The panel's details were left offset after a resize.** They were drawn
   for the old room the moment the size changed and not again once the new
   room landed. They are drawn again when the room itself changes
   (`desk_console_room`).
3. **Reading the meters stopped the screen.** `desk_meters_read` waited on
   a graphics card's sensors for about 22 ms, several times a second, on the
   compositor's thread. The tick now takes the last reading and asks for the
   next on a thread (`desk_meters_latest`).

In this work itself, found in testing and fixed:

4. A crash on a resize with the tuner up: a source box past the edge of a
   picture made for the old size (wlroots asserts). Cuts are now within the
   picture actually put up, and cleared whenever it is replaced.
5. The pulled-back view ran onto the screen beside the desk's: its pictures
   are cut to the desk's own screen (`node_show_within`).
6. The opening's length was scaled by the Motion setting twice.
7. A click off the machine with the tuner up only put the tuner away; it now
   turns back to the desk as well.
8. The tuners opened empty ("...") until a Wi-Fi scan came back: the lists
   are read as the panel is turned to, from NetworkManager's last scan, with
   a new one asked for behind it.

## Testing

- `desk-gputest --room`: the GPU and the C tracer agree on every layer,
  including the two new ones.
- A scripted hand (`DESK_HAND`, see `docs/MODDING.md`) through every tuner,
  turning, switching, the band switch, Esc and a click off -- on the GPU,
  in 2D, and in a build with AddressSanitizer and UBSan: no reports.
- Frame times (`DESK_PROFILE=1`): opening and shutting hold 60 fps, the
  slowest frame under 8 ms (from 40-66 ms before the fixes above).
- Two headless screens: nothing of the pulled-back view on the second.
- Not tried: joining a new password-protected network, and pairing a new
  Bluetooth device, which rely on nm-applet's and blueman's agents.

Tracing the lid's 16 frames takes about 13 s on a recent GPU at 1440x900,
once for each size of screen; until then CHOOSE does nothing.
