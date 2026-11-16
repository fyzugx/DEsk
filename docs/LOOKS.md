# Looks for the drawer

A program kept in DEsk's drawer is a card with its mark printed on it, unless it is one of the
things the drawer has always had. Any of them can be given another look: the other button on it
(a finger held on it) brings up a card of what it could look like, and a touch on one of those is
what it looks like from then on. What there is to choose from:

- its **card**, and its **icon** lying there by itself, printed on nothing;
- the drawer's own things (the pocket computer, the compass, the folder, the notepad, the
  screwdriver) and the desk's (the calculator, the typewriter, the notebook, Print's camera);
- **looks**: a picture of a thing seen from straight above, made by anyone. DEsk comes with a
  few (a radio, a paint box, a game controller, headphones); the rest are yours, or anyone's;
- **Choose a picture…**: any picture, from anywhere, made a look of your own there and then.

Which look a program has is kept in `$XDG_CONFIG_HOME/desk/drawer-looks`, by the program, so it
looks the same taken out of the drawer and put back.

## A look

A look is a folder:

```
teapot/
  look.png   the thing, from straight above, with nothing round it
  look.ini   what it is called, and how big and thick it is
```

`look.ini` (all of it may be left out):

```ini
[Look]
Name=Teapot
Name[ja]=ティーポット
Name[de]=Teekanne
Size=18          # centimetres, its longest side as it lies
Thick=9          # centimetres, how high it stands: how long its shadow is
Order=500        # where it goes among the others where it is, lower first
Author=Someone
License=CC BY 4.0
Comment=Anything
```

- **Name** in English, and `Name[ja]`, `zh`, `ko`, `es`, `fr`, `de` for the desk's other
  languages. Without one it is called what its folder is called.
- **Size** is how long it lies: 2 to 40 cm. Instead, `PixelsPerCm=` says how many of its
  picture's pixels are a centimetre, and it is as big as the picture says. With neither, it is
  14 cm, as big as a card is across.
- **Thick** is only for its shadow; 1 cm if not said.

### The picture

- **Straight above**, the thing lying as it would in a drawer.
- **Lit from the top right and behind**, as the drawer's lamp lights it (in the picture: from
  the upper right). Nothing too bright: the felt is dark.
- **No shadow, and nothing round the thing**: clear (transparent). The drawer gives it the two
  shadows everything in it has, as long as the thing is thick, and the felt's own light where it
  lies.
- 40 pixels to the centimetre is what DEsk's own are; it is read no bigger than 1024 pixels
  across.

A photograph will do, cut out; so will an icon. A rectangle with nothing clear in it lies there as
a rectangle, a photograph on the felt.

## Making one

`desk-look` makes looks, and says what the desk will make of one. A look made among your own is
said as the drawer knows it (`own/Teapot`); `-o FOLDER` makes it anywhere else.

```bash
desk-look add teapot.png --name Teapot --size 18     # from a picture: PNG, JPEG, SVG, anything GTK reads
desk-look bake teapot.glb --name Teapot --name-ja ティーポット     # from a model, in Blender
desk-look check ~/.local/share/desk/looks/Teapot     # what the desk will make of it
desk-look where                                      # where your own are
```

### From a model

`desk-look bake` renders a model the way DEsk's own looks are rendered: in Blender
(<https://www.blender.org>, on the PATH or named by `DESK_BLENDER`), lying flat, seen from
straight above, in the drawer's light, with nothing round it. It takes a `.blend` (all its
objects), `.glb`/`.gltf`, `.obj`, `.fbx`, `.stl` or `.ply`, the thing's top up.

- Its size is its own: metres for glTF, FBX and a `.blend` in metres; for the rest a guess, which
  `--units m|cm|mm` puts right. `--size CM` makes it that long instead.
- `--thick CM` (else how high it is), `--turn DEGREES` (about the upright), `--samples N` (96;
  `--quick` is 16), `--author`, `--license`, `--name-ja` and the other languages.
- A minute or so. `--verbose` shows what Blender says.

What renders it is `art/looks/bake.py` (installed in `share/desk/looks`), which DEsk's own looks
use as well (`art/looks/looks.py`, the radio and the rest, made of plain shapes in a script): a
look made in Blender can be shot with it from a script of your own, `bake.stage()` and
`bake.shoot(objects, folder, names, thick_cm)`.

### From the drawer

**Choose a picture…**, the last on the card of looks, opens a window to choose a picture from
anywhere (the icons there are, too: `/usr/share/icons`, `/usr/share/pixmaps`). It is made a look
of your own, the program looks like it, and the drawer comes out to show it.

## Where they are

| | |
| --- | --- |
| `share/desk/looks/` (where DEsk is installed) | DEsk's own |
| `$XDG_DATA_DIRS/desk/looks/` (`/usr/local/share`, `/usr/share`) | everyone's on this machine |
| `$XDG_DATA_HOME/desk/looks/` (`~/.local/share/desk/looks`) | yours: folders, or only PNGs, each as big as a card |

They are on the card in that order, each place's by `Order` and then by name. A look in a folder of
yours is `own/<folder>`; one of everyone's is its folder's name, and the first of a name is the
one there is.

## Giving one away

A look is its folder: give the folder (zipped, if you like). Put in someone's
`~/.local/share/desk/looks`, it is theirs; in `/usr/local/share/desk/looks`, it is everyone's on
that machine. Nothing else is needed, and nothing runs: a look is a picture and a few words.
