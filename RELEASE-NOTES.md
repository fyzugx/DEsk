**English** | [日本語](RELEASE-NOTES.ja.md)

# DEsk v0.2 (BETA)

## What changed since v0.1.5

- **New: the Control Panel in four.** The tube and the meters at the top left, the volume
  knob and the two faders at the top right, the wireless and Bluetooth switches with LOCK
  and a new SLEEP (the screens to sleep) at the bottom left, and at the bottom right a
  walnut lid on a brass hinge. On the lid, for anybody: AIRPLANE (both radios off),
  POWER SAVE (the power profile), NIGHT (warm colours on every screen, kept from one login
  to the next) and DISPLAY. Under it, once the key is turned and the lid lifted (it swings
  up past upright): SHUT DOWN, RESTART, LOG OUT and a new SUSPEND. Every button has what it
  does etched lightly into its glass cap, lit from behind when its lamp is.
- **New: DISPLAY, and Super+P.** Each press the next way of using the screens, as
  Windows+P has them: extend (the desk goes on onto the others), duplicate, this screen
  only, the other screen only. The button lights the colour of the way, and the tube says
  it a moment; right-click it for the display settings program.
- **New: a calculator on the desk.** In the drawer, beside the camera: click it
  and it comes up onto the desk and opens like a notebook. It lies among the
  windows, to be used beside them. Its deep plastic keys go down as they are
  pressed, by the mouse or the keyboard (click it, and what you type is keyed
  on it). The lid holds a pad with what has been worked out, to scroll back
  through and bring answers back from, and the keys hardly anyone needs: root,
  powers, logs, sines. Sums go in the order they are keyed, as on any desk
  calculator, and in decimals: 0.1 + 0.2 is 0.3.
- **New: a notepad of DEsk's own**, for the drawer's notebook and pencil and for
  text files: the window is the top page of a wire-bound notebook, with its wire,
  holes, perforations, stack of sheets and cover, ruled and with a red margin, and
  you write on it in blue-black ink. Folding it shuts the notebook, with the file's
  name on its label. No menu bar: the keys, a right click, and a sticky note for
  finding things.
  Files are saved in the encoding, with the line endings, they came in; what has
  not been saved is kept, and offered back if the machine goes down. Input methods
  and right-to-left text are GTK's, which building DEsk now needs (the installers
  add it). Mousepad and the rest can still be chosen in Default programs.
- **New: one notebook, typed in and written in.** The drawer's notebook and the
  notepad are one program now: type on a page, or write on it with a pen, and
  the lines you write stay with the lines of text they were written by. A leather
  pencil case lies beside it, with a four-colour pen you roll to change colour, a
  highlighter and an eraser; with one in hand a mouse or a finger writes too.
  Write a title and the date at the head of a page. Lift the bottom right corner
  for the card of what the notebook can do, turn on through it for a new page,
  and let go of the notebook at the edge of the screen to close it.
- **New: Print is a camera.** Print lifts a camera up to your eye and holds
  the screen still as it was, a menu or a tooltip and all. As it nears
  your eye the room goes dark round it, and what is left is the
  viewfinder: the screen a little smaller in the dark, its edge thick
  glass, and under it the viewfinder's green display (what the dial is
  set to, the size of the picture, the self-timer, the frame). Drag out
  the picture you want and the light falls on it; let go and the shutter
  goes off, with its click and a flash. The camera is rendered
  in Blender and looks like the real thing; the one at the foot of the
  screen works like a real one, too: its mode dial takes an
  area, a window or a screen (point at one and click), its self-timer
  counts 2 or 10 seconds with the screen yours meanwhile, its power lever
  puts it down. `Enter` or the space bar is its shutter button; `Esc` or
  the right button gives up. `Ctrl+Print` is the whole screen now, and
  `Alt+Print` the window in front, as before.
  From barenakedchicken (dentarthurdent on Discord) and Ash's region
  screenshot.
- **New: films, with a tape in the camera.** Put a tape in Print's camera
  -- click the slot on its back, or press `Shift+Print` -- and its shutter
  records instead: the area, the window or the screen, with the sound your
  speakers are playing; the clock on the desk shows how long it has run,
  and Print again, or a click on the clock's face, stops it. It goes into
  Videos/Screencasts as an .mp4 (made by ffmpeg, which has to be
  installed), and a letter in the post box says where. The camera is in
  the drawer too (a click on it puts it up to your eye once the drawer is
  in), and `Super+Shift+S` works as Print does.
- **New: touch.** The desk and the windows take fingers. In a window a finger
  goes to the program as it is (scrolling, pinching); on the desk the drawer,
  the bookcase, the Control Panel and the post box, the sheets in a binder and
  the corner of a sheet follow the finger and stay under it. Let go and they go
  on at the finger's speed: a sheet slides over the desk and stops, a drawer
  bounces a little at its stop. Three fingers pick, fold, carry, spread and
  crumple windows, following the fingers too.
- **New: a typewriter for a keyboard.** Used with fingers or a pen and no
  keyboard, touching a place to type brings a typewriter up to the front of the
  desk: glass keys in nickel rings, the fan of typebars and the ribbon, paper on
  the platen, the clack and the bell. Its layout and letters come from the
  machine's keymap (English, European, Japanese, Korean). It types the password
  at the front door too, and can be taken out of the drawer. Shell Settings has
  it under "On-screen keyboard".
- **New: a pen.** Programs that know pens get one, with pressure and tilt;
  others get clicks. The palm of the hand holding the pen is not taken for a
  finger.
- **New: handwriting.** Write a letter in the squares on the typewriter's paper
  and it is read and typed (Latin letters, Japanese, Chinese, Korean; read on the
  machine only).
- **New: the screen turns with the machine.** On a tablet or a 2-in-1 with an
  accelerometer (through iio-sensor-proxy), turning it turns the screen, the
  room is put together again the new way, and windows are kept on the screen.
  `Auto-rotate` on the Screen page of Shell Settings turns it off.
- **New: picture-in-picture is an old slider phone.** A browser's video in its own
  small window becomes the screen of a phone, black glass in a polished steel band,
  put down in the corner of the desk and kept in front. On its right side: a red light
  to close it, a real button to play and pause, and a pale light that slides a deck out
  from under it, with the times, a slider, ten seconds back, ten seconds on, and Open,
  which brings up the window it came from. The keys speak MPRIS: where a page or a
  program cannot be sent somewhere, the slider and the ten seconds do nothing.
- **New: a mat on the desk** (thanks to DaeTheDruid, PR #61). Felt in a colour, green
  leather tooled in gold, linoleum, a photo printed on it, or a clear sheet over all of the
  desk with your pictures under it (a mat is unrolled across the desk, and rolled up to take
  it away): one
  picture all over it (Full screen), or prints laid wherever you like (Free). To move them,
  lift the mat by a corner: carry a print, turn it with the wheel, make it larger or smaller
  with Ctrl and the wheel (on a touch screen, a second finger by the one holding it: apart
  or together for the size, turned to turn it), and put one back in its envelope to take it
  away. Shell Settings
  → Mat (choosing pictures there makes the mat a clear one, and they are slipped under it
  where no window covers them); `desk-mat` does the same from a terminal.
- **New: what the desk is made of.** Walnut as it has always been, oak, cherry, maple or
  ebony, and a stain over the wood: honey, brown, dark brown or a grey wash, the grain
  showing through (ebony takes only the lighter ones). Shell Settings → Material, an order
  form: tick a wood and a stain on fans of samples of the desk in them, as at a joiner's, and
  sign it. The lamp goes down while the room is traced again in it (the clock counts how
  far), and the light comes back as eyes opening on the new desk.
- **New: what a program in the drawer looks like.** Right-click one in the
  drawer (or hold a finger on it) for a card of looks: paper, its icon as it is,
  a picture you choose, and things made for it. A look is a folder anyone can
  make, a picture from straight above and a short `look.ini`;
  `desk-look` adds one from any picture or bakes one from a 3D model in Blender
  ([docs/LOOKS.md](docs/LOOKS.md)).
- **New: the clock shows the machine.** Its POWER page is now PC: the battery (or the
  line), the watts, the processor, the memory and the network in and out, read every second
  while it shows. And the clock stays on the page you leave it at; it no longer goes back
  to the time by itself.
- **New: favourites on the bookcase.** A slim mustard binder at the left end of the row.
  Stick a tab on the edge of a program's sheet in any binder, or by its line in the index
  book, and it is in Favourites too; take the tab off to take it out.
- **New: a clipboard in the drawer.** A hardboard board with a steel clip, and what you copy
  clipped to it as slips, the newest in front and the foot of each one before it showing
  below: words and pictures, the last twenty, kept only while you are logged in (nothing is
  written to disk, and what a password manager marks secret is not kept at all). Click a
  slip to copy it again; right-click to tear it off, or the clip to take them all off.
- **Richer sounds**, chosen by ear: the drawer opens and shuts with weight,
  a sheet put down on a leather or linoleum mat sounds soft, a window slid over
  the desk rustles at the speed of the hand, and the Control Panel's buttons,
  levers, knob, key and lid each have their own.
- **New: Alt+Tab spreads the windows out over the desk.** Keep Alt held and
  every window, folded ones too, slides apart across the desk so you can
  see them all, each with a label showing its program's icon and name, and
  the chosen one picked up over the rest. `Tab` and `Shift+Tab` go round them last used first, the arrows
  go to the next one that way, or just point at one. Let go of Alt (or press
  `Enter`, or click) to go there; `Esc` or a click on the desk puts them all
  back. A quick `Alt+Tab` now goes to the window you used just before, as on
  other desktops. `Super+Tab` and `Alt+F1` still go straight round. From
  barenakedchicken's AiO too (dentarthurdent on Discord).
- **New: Alt+Tab goes round folded windows too.** A folded window, lying on
  the desk or put away in the drawer, now has its turn in `Alt+Tab` (and
  `Super+Tab`, `Alt+F1`): it is unfolded, and has the keys straight away.
  Also from barenakedchicken's AiO (dentarthurdent on Discord).
- **New: a window on half of the screen.** `Super+Left` or `Super+Right` lays
  the window in front on the left or right half of its screen, top to bottom,
  for two side by side; the same key again puts it back as it was. Letting go
  of a window at the left or right edge of the screen does the same (not at an
  edge where another screen carries on), and picking it up again brings it back
  to its own size. It comes from the AiO collection by barenakedchicken
  (dentarthurdent on Discord): thanks! It is also the answer to #39.
- **A window slides to its half.** With `Super+Left`/`Super+Right`, or let go
  at the edge, the window no longer jumps to its half: its edges are drawn
  across in a third of a second, as if pulled by hand.
- **New: a window lifted off the drawer's handle.** A window down to the front
  of the desk (opened out, on a half, or pulled down there) covers the
  drawer's handle. Click its bottom edge and the edge lifts off the handle,
  so the drawer can be opened with the mouse; click it again to lay it back
  down. Dragging the edge still resizes it. Also from barenakedchicken's AiO
  (dentarthurdent on Discord), and the answer to #40.
- **Screens one above the other** (a ZenBook Duo): the clipboard and the calculator carried
  down onto the lower screen stay there instead of going into the drawer, a window flicked
  downwards stops at the bottom of the lowest screen instead of sliding off it, and a
  finger turns the index book a spread at a time.
- **The level the desk starts at is chosen with more care.** With graphics built into the
  processor, or a virtual machine's, it starts at Medium (the installer's look lets it go
  higher only for a machine well over what High needs), and Ultra is chosen only with
  graphics of its own. With nothing chosen anywhere (a live USB, a build of your own) it
  goes by the same. Shell Settings → Graphics changes it as before.
- **Tried on a Surface Pro 7**, and what it showed was put right: Shell
  Settings works with fingers; the Control Panel's lid, the tray's slips, the
  post box's letters and the things along the top come down under the finger;
  a binder's sheets and the ALL APPS book follow it as it goes; opening the
  bookcase, the Control Panel, the memo pad and the calendar on a dense screen
  is lighter; `Super+V` brings the clipboard out and puts it away; the
  calculator is in the bookcase too; Night colours have a strength; and new
  windows are kept on a small screen (down to 800x600).
- **New: Steam games are known by name.** Many games call their window by
  their executable (hh.x86_64, wine…), so a folded one showed just a letter.
  DEsk now finds the game Steam started it for, and uses its name and icon.
  Also from barenakedchicken's AiO (dentarthurdent on Discord).
- **New: Wood Maintenance.** Turn it on in Shell Settings (Termites) and,
  when the desk is left alone a while, termites come out and nibble at it,
  the wood and the edges of windows, and run off when you move the mouse.
  Only a picture: the marks in the wood stay until you restore the desk, and
  a window's go with it. Off unless you turn it on. By barenakedchicken
  (dentarthurdent on Discord), PR #14.
- **Darker names on the bookcase's binders** (#32), and a Shell Settings row,
  Binder labels, for how dark: Faded (as before), Normal, Dark, Darkest.
  Thanks to DaeTheDruid (PR #24).
- **The clock, the post box and the Control Panel on the desk are pictures
  now, and the desk much lighter to make.** Their cases are rendered in
  Blender with their shadows on the desk, for every shape of screen and
  width of desk: the clock's marbled Bakelite with its edges worn, the post
  box's red enamel on cast iron with its brass, the Control Panel's crinkle
  black, brass and walnut. They are laid over the desk, and the room's
  tracer leaves them out, so tracing the desk on a processor takes about
  30% less time. In 2D they are as sharp as the screen. Opening the Control
  Panel or the post box, and the moves to the bookcase and back, still show
  them in 3D.
- **On a machine without a GPU, the room is laid from pictures, not traced.**
  The desk and its drawer, the views of the bookcase, the Control Panel and
  the post box, and the moves to them were traced on the processor at every
  first start: on a machine of about 2010, at Low, close to half an hour.
  They are now laid from pictures of each surface made in advance (installed
  with DEsk's data in `share/desk/room`, about 30 MB), and only what those
  cannot show is traced: about two minutes on the same machine, the desk
  itself in under ten seconds, and no soft first look while it is made.
  At 2D, Low and Medium the room is now as wide as the screen, so the desk is
  no longer blurred (it was traced 640, 960 and 1280 wide and stretched).
- **Smaller programs.** The pictures DEsk is drawn from -- the room's
  surfaces, the clock, the post box, the Control Panel and Print's camera --
  were built into each of its four programs; they are now installed once,
  with its data (`share/desk`). `desk` is about 7 MB rather than 56, and DEsk
  as installed about 150 MB smaller.
- **The front door moves as sharp as it stands.** At Medium and up, walking
  up to the door, leaning in to the key, being told no, going in and going
  out of the room are filmed ahead, on a thread at the back of the queue,
  for the door as it is (its weather and hour, its board), and played as
  you go: the name on the board can be read all the way, and the door is
  sharp the moment you stop, where it used to be coarse while moving and
  sharp only seconds later. It also keeps the frame rate better.
- **For people changing DEsk: things.** A window that is not paper, or a tool lying on
  the desk, is now one file of its own (`include/thing.h`, `src/thing-*.c`), and what
  the Blender scripts share is `art/kit.py`. How to make one is in `docs/THINGS.md`.

## What changed in v0.1.5

- **Fixed: the screen could stop for half a second every second and a half**
  on a laptop. The clock reads the battery every second, and a laptop's
  battery is its firmware's to answer for, which on some laptops takes half
  a second; DEsk waited for the answer with everything else waiting behind
  it. The battery (and the temperature on the Control Panel) is now read on
  a thread of its own, and what is shown is the last answer. Should the
  once-a-second round ever take long enough to see, the log says which part
  of it was slow, and so does desk-report. Thanks to Riennas (on Discord),
  whose video showed the rhythm of it.
- **Fixed: a window folded on a screen beside the desk's stays there.** With
  more than one screen, a window folded on another screen flew to the front
  of the desk's screen, and a folded sheet carried to another screen went
  back when let go. Now it lies along the front of the screen it was folded
  on, stays where it is while the drawer or the camera moves the desk, can
  be opened or carried about there even while the desk's screen is turned to
  the bookcase, and goes to a screen that is left when its screen is taken
  away. Thanks to v (on Discord) for the report, and to DaeTheDruid, whose
  PR #23 found and fixed it.
- **Fixed: a screen turned off with a display tool could not be turned on
  again.** wdisplays and the like could turn a screen off, but turning it
  back on was refused every time, and as DEsk remembers how the screens were
  left, it stayed off after a restart too. A real screen will not be tried
  without a picture to show, and DEsk gave the one being turned on none; it
  now gives each screen its first picture before the change is tried. A
  screen turned back on without a mode comes on in the one it was turned off
  in, and one asked to go where another screen already is (tools send 0,0,
  knowing no place for a screen that is off) is put beside the others, to the
  right, so the desk stays where it is. Thanks to Mr_Duckling (on Discord),
  whose desk-report showed it.
- **Fixed: a game made on the whole screen could open as a sheet on the
  desk.** A program can ask for the whole screen, or to be opened out,
  before its window is up: Wine does so for a game made on the whole screen
  (Proton's games among them), and so do many Wayland programs. DEsk only
  listened once the window was up, so such a game came onto the desk as a
  big sheet fitted to the desk, like an opened-out window. It now gets what
  it asked for as it opens, the whole screen or opened out; one that asked
  for both is opened out again when it leaves the whole screen. A program
  that remembers being opened out (many do) now opens that way again. The
  log, and so desk-report, now says how each X11 window came onto the desk
  and when it asks for the whole screen or to leave it. Thanks to Nightshade
  (on Discord), whose report about a game led to it.
- **Fixed: a file dragged from one X11 program to another was dropped
  short.** Dragging between X11 programs (Wine ones among them) ended as
  soon as the pointer left the first window: the picture being dragged is
  a window of its own under the pointer, DEsk handed the pointer to it, and
  Xwayland takes that as the button let go. A button held down in a program
  now keeps the pointer with that program until it is let go, as on other
  desktops: a drag between X11 programs reaches the other one, and a slider
  or a selection pulled out of a window goes on following the mouse. A
  Wayland program is told, too, when the button comes up outside its window
  (it used to go on thinking it was held). Dragging from an X11 program onto
  a Wayland one still does not work (wlroots 0.18 cannot); such a drag now
  just ends where it is let go. Thanks to YZ10 (on Discord), whose report
  led to it.
- **X11 and Flatpak programs can keep the screen lit too.** A film in
  Spotify, VLC or a game used to let the screen go dark, as they ask on
  the session bus (`org.freedesktop.ScreenSaver`), which DEsk didn't
  answer; only Wayland programs were heard. Now both are, for as long as
  the program asks, and one that quits without letting go lets go as it
  leaves. A screen saver of your own that has the name keeps it. Thanks
  to DaddyBeer (on Discord).
- **Fixed: a program installed while you were logged in was not in the
  bookcase** until you logged in again. DEsk now looks at the folders the
  programs' entries are in every few seconds, and when one changes (a
  program installed or taken away, Flatpak or a package) it reads them
  again, as at login. It waits while you're at the bookcase. Thanks to
  PasqualePerilli (#34) and JamesStallings (#50).
- **Fixed: Flatpak apps had no icon** in the bookcase, the drawer, on their
  folded sheets and in the tray. DEsk now looks for icons wherever the
  icon theme spec says, Flatpak's own folders included. Thanks to
  barenakedchicken (dentarthurdent on Discord), whose AiO found it.
- **Fixed: emoji in notifications were boxes with an X.** A letter in the post
  box now shows them in colour, and Arabic in it is joined up. Thanks to
  barenakedchicken (dentarthurdent on Discord), who wrote the fix (PR #15).
  Building DEsk now needs Pango's headers; the installers add them.
- **Fixed: the Control Panel could stay out of place after the screen
  changed shape.** Opened just after a resize, before the room for the new
  size was ready, it kept the old room's picture under its needles and
  lamps. It's now drawn again once the new room is there. Thanks to
  DaeTheDruid (PR #58).
- **Volume up unmutes.** While the sound is muted, the key that turns the
  volume up now unmutes it too, as on other desktops; turning it down
  leaves it muted. Thanks to what (on Discord).
- **Natural scrolling for a mouse's wheel.** Shell Settings has a new row
  under the touchpad's, "Natural scrolling on the mouse"
  (`natural_scrolling_mouse` in shell.conf): the page goes the way the
  wheel turns. Before, natural scrolling was only for touchpads, and a
  mouse could not be turned round at all. Thanks to AlanGraham0 (#27).
- **New: a Terminal row in Shell Settings** (System, Look and language,
  under Default programs): the terminal the drawer and `Super+Enter` open,
  chosen by name from the ones installed. Default programs can't choose
  one, as a terminal has no kind of file of its own, which is why changing
  it there did nothing. Thanks to JamesStallings (#47).
- **The pointer shows when DEsk runs as a window on another Wayland
  desktop.** Some desktops don't show the pointer a window asks for over
  it, so DEsk now draws the pointer into its own window there. Thanks to
  Gberfield (PR #29).
- **NixOS: a flake**, in `packaging/nix`, with a package and a NixOS module
  for the login session. It's kept by the community, not built or tested
  with each release. Thanks to picarica (PR #48, #22).

## What changed in v0.1.4

- **Fixed: after the screen turned off, it never came back** on some
  monitors (GitHub #18). Many HDMI and DisplayPort monitors say they have
  been unplugged when they go to sleep, and DEsk ended the whole session when
  its last screen went away: nothing could wake it, and every program in it
  was closed. DEsk now keeps the session and waits for the screen. A screen
  that comes back while the desk is dark stays dark until you press a key or
  move the mouse, so a sleeping monitor is left asleep. If your monitor stays
  asleep this way, press its power button (turning off its deep-sleep or
  power-saving setting avoids it). Thanks to JamesStallings on GitHub for the
  report and the `desk-report` that showed what happened.
- **Fixed: on a laptop whose firmware screen stayed beside its graphics
  card, the desk was drawn in 2D on a screen nobody sees.** Some drivers
  (NVIDIA's, unless `nvidia_drm.fbdev=1` is set) leave the screen the
  firmware started with (simpledrm) beside the real card, and DEsk took that
  one as its main screen: drawn on the processor, in 2D Fallback, with the
  laptop's own panel showing only the wood. DEsk now leaves the firmware's
  screen out when a graphics card with screens of its own is there, and says
  so in its log ("screens: ... left out"). If you set `WLR_DRM_DEVICES`
  yourself, it is left as you set it. Thanks to Darkhawk on Discord for the
  report and the `desk-report`.
- **Arabic and more in Shell Settings' "Second layout".** Arabic, Persian,
  Hebrew, Greek, Ukrainian, Hindi and Thai can now be chosen there (any
  layout xkb knows could already be written into `shell.conf`). And DEsk's
  own shortcuts, such as Super+A for ALL APPS and Super+L to lock, now work
  while a second layout like Arabic or Russian is on; before, they did
  nothing until you changed back. Thanks to YZ10 on Discord for asking.
- **GTK 4 programs on paper too.** DEsk's GTK theme now has a GTK 4 part, so
  GTK 4 programs that follow the theme (pavucontrol, for one) get the same
  paper, ink and brass as GTK 3 ones in a DEsk session. Programs made with
  libadwaita (most of GNOME's) do not follow themes; the README has the one
  line that puts them on paper too, if you want it in every desktop. Thanks
  to VKOWinHecker201 on GitHub, who made the GTK 4 stylesheet (#17).
- **Fixed: on a screen put together on the GPU, a sheet's turned-up corner
  showed the bare desk** instead of the sheet beneath it. It now shows what
  is underneath, as on the processor. (Known issue since v0.1.)
- **Fixed: after changing Graphics, the Control Panel and the post box were
  drawn wrong until you logged in again** (GitHub #12). The Control Panel
  lost its needles, lamps and labels, and the post box its letters: the
  traced picture under them was made again on top of them. Thanks to
  Myttenzzz on GitHub for the report.
- **Fixed: an unfolded (maximized) browser came back small after a
  full-screen video.** Chrome, Brave and other Chromium browsers ask to be
  maximized again when they leave full screen, and DEsk took any such request
  as "the other way round". It now does what the window asks for. Thanks to
  what on Discord for the report.
- **Fixed: on a slower processor, the front door (the lock screen) stood
  still for a second or more**, sometimes for several, so that typing the
  password and going in were late. The door is drawn on the processor even
  with a GPU. Its fine picture is now drawn behind everything else and
  given up as soon as it is no longer needed, and the moving picture gets
  coarser at once when the machine cannot keep up. On a machine like
  2010's, going in after typing the password takes as long as on a fast
  one.
- **Fixed: "DEsk" in the bookcase did nothing inside DEsk.** That entry opens
  DEsk in a window from another desktop; it is now left out inside DEsk.
- **Fixed: an old X program folded into a blank sheet.** A window whose
  title is in Latin-1 rather than UTF-8 left its folded sheet without its
  letter and name; it now gets a "?" for its letter, and its name. Program
  names that are not proper UTF-8 (a desktop file in another encoding, or a
  long name cut off inside a letter) are cleaned up as they are read.
- **`desk-report`** now lists each graphics device with its driver and the
  screens plugged into it, NVIDIA's `fbdev` setting, and the kernel options
  that choose them. A laptop whose firmware screen sat beside its NVIDIA
  card (only the desk's wood showing) is then plain from its first lines.
- **Docs: where `shell.conf` is.** Your own `~/.config/desk/shell.conf` is
  written the first time you change a setting in Shell Settings; until then
  the machine's `/etc/xdg/desk/shell.conf` and the defaults hold (README,
  docs/CONFIGURATION.md).

## What changed in v0.1.3

- **New: games on the whole screen, with the mouse caught.** `Super+F` puts
  the window in front on the whole screen (and back). A game can now catch
  the pointer to look around, and gets the mouse's movement as it should.
  `Alt+Tab` (and `Super+Tab`, `Alt+F1`) moves to the next window, putting a
  game on the whole screen back on its sheet first. `Super+M` folds the
  window in front. The bookcase, the Control Panel, the post box and the
  drawer still come up in front of a game, and the mouse is yours while they
  are up. Alt+Tab takes a game alone on the whole screen back to the desk.
  In a virtual machine, or with a tablet, a game now gets the movement too.
  This is the community's work: thanks to barenakedchicken
  (dentarthurdent on Discord), who wrote it, and to Array, who tried it with
  Overwatch.
- **Fixed: a folded Python program was called "py"** (Linux Mint's Software
  Manager, for one). Its sheet now has the program's own name and icon.
  Thanks to barenakedchicken (dentarthurdent on Discord), who found it and
  sent a fix.
- **Fixed: the clock went on counting from the last track** when Spotify
  moved to the next one. A new track now starts at 0 on the clock's MEDIA
  page. Thanks to Array on Discord for the report and the video.
- **Fixed: a picture-in-picture video went behind other windows**
  (Firefox, Chromium). It now stays in front. Thanks to Yusuf0Zain for
  the report on GitHub.
- **Fixed: apps whose icon is only an SVG got a printed letter** on the
  bookcase. The installers now bring `rsvg-convert`, a small tool that
  draws SVG; ImageMagick still works when it is there instead.
- **New: `scripts/uninstall.sh`** takes DEsk off the machine again: its
  programs and files, "DEsk" on the login screen, its settings for the whole
  machine, and the wlroots the installer built. Your own settings and the
  packages from your distribution are left. See "Uninstall" in the README.
- **Fixed: Arabic (and Hebrew, Thai, Devanagari...) came out as boxes** on
  the bookcase, in ALL APPS and in window titles. They are now set in a
  typeface that has them. The letters are not yet joined up or set right to
  left. Thanks to robot5477 on Discord for the report.
- **Fixed: on 4K screens, ALL APPS, the memo pad and the calendar were
  slow and soft.** Writing in the book, turning its pages and taking up the
  memo pad no longer stall (it was about a third of a second a letter, and
  over a second for the memo's first); the same went for any screen at Low,
  Medium and 2D. The grain of the paper was shrunk through cairo's finest
  filter at every pixel; it is now shrunk once. And at Ultra, the book, the
  papers, the calendar and the clock's figures are now drawn at the screen's
  own resolution (up to a 4K screen's worth of pixels) instead of 2048
  pixels wide and enlarged. That takes up to about 500 MB more on a 4K
  screen, and turning a page costs about two and a half times as much; High
  and below draw them as before. Thanks to Fendergluid on Discord for the
  report.
- **Sharper at Low, Medium and 2D.** The book, the papers, the calendar, the
  clock and its buttons, and the console's lamps and needles are now drawn at
  the screen's own resolution (up to 2048 pixels wide) at every level, rather
  than at the room's -- a third of it at 2D on a 1920 screen, which left the
  clock's buttons unreadable. The room, the door and the weather at it stay
  as they were. On an old machine the book costs the most: writing in it and
  turning its pages can take a few tenths of a second at 2D, and the desk
  takes 50 to 130 MB more.
- **New: a letter on the desk can be carried.** Take hold of it and move
  it wherever you like; it stays there. A click without moving still puts it
  away.
- **New: screen sharing asks which screen** (with two or more), in a
  small window, instead of always sharing the first. Sharing a single window
  is not possible yet. Thanks to Nightshade on Discord for the report.
- **`desk-report`** now also takes what the system's journal says of DEsk and
  the login screen since this boot, for when the login goes straight back to
  the login screen; and it says whether the screen was put together on the
  processor, and which screen-sharing portal (xdg-desktop-portal-wlr) is
  installed.
- **Fixed: "Clock skew detected"** when building from the ZIP west of Japan.
  The files in the release now carry a time in the past everywhere.

## What changed in v0.1.2

- **Fedora**: an installer for Fedora 43 and 44 (and what is built on them,
  such as Nobara), `sudo sh scripts/install_fedora.sh`. Everything comes from
  Fedora's own repositories, wlroots 0.18 included (`wlroots0.18`); it is
  built from its release only on a Fedora that has none. Packages that are
  only recommended are left out, as on Debian and Ubuntu. Not in Fedora, so
  done without: the memo pad's Klee One (Noto Serif CJK instead), Mugshot,
  and the letters for a low battery and for updates.
  `install_session.sh` recognises Fedora, and Shell Settings names Fedora's
  packages when a row has nothing to open.
- **Fixed: the Arch installer stopped while building wlroots** (on an Arch
  without `wlroots0.18` from the AUR), with "mkdir: cannot create directory".
  Thanks to Axenity on Discord, who found the cause and the fix.
- **Fixed: a black screen in the next desktop** (Plasma, for one) after
  logging out of DEsk. DEsk's session now takes back what it told the
  user's systemd, and stops the portals it started, once it is over.
- **Fixed: a letter taken out of the post box could not be thrown away.**
  Click it on the desk.
- **New: `desk-report`**, which gathers what a bug report needs into one
  file (`~/desk-report.txt`), with personal things taken out. Run it in a
  terminal and attach the file.
- **Fixed: a blank sheet waited about 45 seconds** for programs that run in
  a terminal (btop) or call their window by another name (KDE's System
  Settings). The window now finds its sheet by the program it came from, and
  a program that quits without a window has its sheet cleared in seconds.

## What changed in v0.1.1

- **Fixed: opening the calendar or the memo pad stopped the desk** on
  screens drawn by the GPU. On a GPU screen the desk read the size of a
  picture it had already handed over, and laid it for the whole screen.
- **Simpler install**: one line per system, `sudo sh scripts/install_debian.sh`
  (or `install_ubuntu.sh`, `install_arch.sh`), at the top of the README. Each
  installer now checks the copy against `SHA256SUMS` itself.

To update, unpack the new release and run the same line again; your
settings are kept.

## About DEsk

DEsk is a Wayland desktop for Linux in realistic
skeuomorphism -- the kind iOS 6 had -- in which the desktop is a desk:
windows are sheets of paper, the dock is a drawer, programs are filed in
binders on a bookcase, notifications arrive in a post box, and one desk lamp
lights a ray-traced room.

## What is in it

- **A whole desktop**, not only a window manager: a Wayland compositor
  (wlroots 0.18) with its own notification daemon, system tray, media
  controls, screen lock, idle and screen-off handling, display arrangement
  memory, and a session that starts the polkit agent, keyring, input method
  and network applet for you.
- **Windows on paper**, folded away, opened out to the desk, crumpled and
  thrown when closed. Folded windows lie on the desk under the others, or go
  into the drawer.
- **The drawer** (the dock), **the bookcase** with eight binders and an ALL
  APPS book you can search, **the Control Panel** (meters, Wi-Fi and
  Bluetooth levers, volume, brightness, the tray), **the post box**, a
  **desk calendar** with sticky notes, a **memo pad** and a **clock** with a
  media player page.
- **The front door** as the screen lock, in the real time of day and,
  optionally, the real weather.
- **Shell Settings**, which also opens the free programs DEsk borrows for the
  rest of the machine, each row naming the program it uses.
- **Seven languages**: English, Japanese, Chinese (simplified), Korean,
  Spanish, French and German, with calendars printed the way each country
  prints them and Chinese and Korean set in their own cut of the CJK fonts.
- **More than one screen**: the desk on one, a plain desk to put windows on
  on the others.
- **Five quality levels**, from Ultra down to a 2D Fallback that DEsk picks by
  itself when the screen is drawn on the CPU (no GPU, or a virtual machine
  without 3D).
- **Installers** for Debian 13, Ubuntu 24.04 and 26.04, Arch Linux and
  Fedora 43 and 44 (and their derivatives), which install DEsk, what it
  needs and a classic set of GTK programs, and check the copy against
  `SHA256SUMS` first.

## Installing

Unpack the release (`DEsk-v0.2-BETA.zip` or `.tar.gz`), go into its folder
and run the line for your system:

```sh
sudo sh scripts/install_debian.sh    # Debian 13
sudo sh scripts/install_ubuntu.sh    # Ubuntu 24.04 / 26.04, Linux Mint, Pop!_OS
sudo sh scripts/install_arch.sh      # Arch Linux, EndeavourOS, Manjaro
sudo sh scripts/install_fedora.sh    # Fedora 43 / 44, Nobara
```

Then log out and choose **DEsk** on the login screen. The README has the
details, the options (`--yes`, `--no-apps`, `--bare`, `--prefix`) and how to
build it yourself. To update later, run the same line from the new copy;
your settings and the traced room are kept.

The first start at a screen size traces the room: a few seconds on a GPU,
and longer on the processor (a letter on the desk says so). It is kept on
disk afterwards.

## Known limitations

- **Not yet supported protocols**: layer-shell (panels such as waybar, and
  slurp for picking an area), session-lock (external screen lockers),
  gamma-control (outside night-light tools; DEsk has its own Night colours),
  input-method-v2 (outside on-screen keyboards; DEsk has its own),
  fractional-scale.
- **High-density screens**: DEsk sizes its own things in points, but does
  not yet tell programs a scale, so their windows come out small on a HiDPI
  screen.
- **More than one screen**: screens other than the desk's show a plain desk
  for windows; the bookcase, Control Panel and post box are on the desk's
  screen only.
- **X11 programs** run through Xwayland. Dragging from an X11 program onto a
  Wayland one does not work (wlroots 0.18 cannot); between X11 programs, and
  from Wayland ones onto X11 ones, it does.
- **Screen sharing** shares whole screens, not single windows, and does not
  start when the screen is put together on the processor (wlroots' software
  renderer, as on a machine without a GPU driver): the screen-sharing portal
  (xdg-desktop-portal-wlr) needs GPU buffers from a wlroots as old as the
  0.18 DEsk is built on. `desk-report` says which it is.
- **Right-to-left scripts** (Arabic, Hebrew) show their letters, but not
  joined up or in right-to-left order, in what DEsk draws itself (letters in
  the post box excepted, where Arabic is joined up).
- **Translations** other than English and Japanese have not yet been
  reviewed by native speakers.
- **No plugin API**: changing DEsk means changing its source
  ([docs/MODDING.md](docs/MODDING.md)); things on the desk have a small kit for
  that ([docs/THINGS.md](docs/THINGS.md)).
- Real hardware tested so far: VMware virtual machines (SVGA3D), an ASUS
  ZenBook Duo (two GPUs, two screens) and a Microsoft Surface Pro 7 (touch and
  pen, from a live USB). Reports from other machines are very
  welcome.
- **Fedora** was tried in a clean Fedora 44 container: the installer from
  scratch and again as an update, the wlroots build for a Fedora without it,
  and the installed desk run headless. Logging in to it on a Fedora machine
  (GDM, SDDM) has not been tried yet.

## How it was checked

Built with no warnings in four configurations (minimal, `-Werror`,
`_FORTIFY_SOURCE=3`, AddressSanitizer + UndefinedBehaviorSanitizer), with a
strict warning set and GCC's static analyzer over every file; run under
ThreadSanitizer, AddressSanitizer and UndefinedBehaviorSanitizer, including
with deliberately malformed settings, data and `.desktop` files; every view
rendered in all seven languages; the translation tables checked by
`meson test`. Details in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
For v0.2, each new part was tried in a desk run inside another (with touch
fed in for the fingers, and a real kernel's DRM in QEMU where it needed one),
many under AddressSanitizer and UndefinedBehaviorSanitizer; touch, pen and the
small screen on a Surface Pro 7; the release build has no warnings and
`meson test` passes.

## Checking your copy

The folder carries `SHA256SUMS`, which the installer checks before it builds.
The archive comes with `DEsk-v0.2-BETA.tar.gz.sha256`:

```sh
sha256sum -c DEsk-v0.2-BETA.tar.gz.sha256
```

## License

MIT. See [LICENSE](LICENSE).
