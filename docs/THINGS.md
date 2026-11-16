**English** | [日本語](THINGS.ja.md)

# Things: windows that are not paper

On DEsk a window is a sheet of paper. It folds, opens out, has a corner to turn, and is crumpled up when it goes. That is the rule for every program that knows nothing of the desk.

A **thing** is the exception: something on the desk that has the rules that suit what it is. A picture-in-picture window is the screen of a phone, which does not fold. The text editor is a notebook, typed in and written in with a pen, which shuts, and is closed by being taken to the edge of the desk. The calculator is no window at all.

A thing is **one C file**, its name in a list, and usually a folder of pictures. `include/thing.h` is all it needs to know: what it fills in, and what it can ask of the desk.

Like everything in `include/`, `thing.h` is internal and may change between releases ([ARCHITECTURE.md](ARCHITECTURE.md) §10): a thing is a change to DEsk's source, built with it ([MODDING.md](MODDING.md)). Read this page with the three that are there.

## Three shapes

| Shape | The window is | Example | Read |
| --- | --- | --- | --- |
| Round a program that knows nothing of it | whoever's it is, as the thing's screen | the phone | `src/thing-phone.c` |
| Round a program of ours | a program drawing on the thing | the notebook, and its pencil case | `src/thing-notebook.c`, `src/notepad.c` |
| No window | none: a tree of its own among the windows | the calculator | `src/thing-calc.c` |

The first is the cheapest way to add to the desk: nothing has to be written but the thing. A terminal can have a screen round it, a music player a tape deck, and the programs need not know.

## What every thing keeps

A thing makes its own rules, but people still have to find their way. These do not change:

1. How to close it can be seen.
2. It can be picked up and moved.
3. If it has a window, it is in Alt+Tab.
4. With the whole screen, the thing goes away and only the window is left. (`view_thing` returns NULL then, and the desk does the rest.)
5. If a picture cannot be read, the window is plain paper, and nothing breaks. (`ready`.)
6. What is read, letters and figures, is drawn at the screen's own pixels, at every level of graphics. Pictures can be soft; words cannot.

The notebook shows a few of the less obvious things a thing can have: something of its own on the desk beside it, anywhere it is put, handled by the hand before the windows are (`hand_down`, `drag`, `release`, `wheel`, a node of its own in `desk_windows` found with `wlr_scene_node_at`: its pencil case); its own way with the edge of the desk (`at_edge`) and with going (`goes`, from `view_picture`), instead of half the screen and the crumpled ball; room kept under it when it is opened out (`open_out_below`); fingers on its window left to its program though it takes presses itself (`program_fingers`); and no keyboard brought up for a pen that is writing on it (`pen_writes`). It talks to its program through a small file the program watches, which is all two processes on one desk need.

## The smallest thing

A wooden frame round every xterm, with no pictures at all:

```c
/* src/thing-frame.c */
#include "thing.h"

#include <string.h>

enum { FRAME = 26 };

static bool frame_claims(const struct desk_toplevel *view)
{
	const char *id = view_program_id(view);
	return id && !strcmp(id, "XTerm");
}

static void frame_paint(cairo_t *cr, struct desk_toplevel *view, double x, double y, double w,
	double h, const char *title, bool focused, bool resting)
{
	(void)view;
	(void)title;
	(void)focused;
	(void)resting;
	cairo_set_source_rgb(cr, 0.30, 0.18, 0.09);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
}

const struct desk_thing desk_thing_frame = {
	.name = "frame",
	.claims = frame_claims,
	.no = THING_NO_FOLD | THING_NO_CORNER | THING_NO_CRUMPLE,
	.inset_l = FRAME, .inset_t = FRAME, .inset_r = FRAME, .inset_b = FRAME,
	.pad = 8,
	.paint = frame_paint,
};
```

Then add it to the list in `src/things.c` (its `extern` and its name) and to `wm_sources` in `meson.build`. That is all: it is moved by its frame, pulled by its edges, kept in Alt+Tab, and given up for the whole screen, with nothing else said.

Whatever a thing leaves out is as paper has it, or nothing. `thing.h` says what each field is. Those for a thing round a window come first; `drag`, `release`, `tick` and the rest are for anything that wants the hand or the frames whatever is going on; the last are for a thing kept in the drawer.

The state of a thing round a window goes in `view_thing_data`: it is zeros the first time, and the desk frees it with the window. A thing with no window keeps a `static` struct.

**Fingers come free.** A finger on the window in a thing is the program's. On the thing itself it is the hand: `hand_down`, `drag` and `release`, or your `press`, or picked up and pulled as paper is. Only a thing with no window needs `hand_here`, so that a finger going across the desk knows whether it is carrying the thing or going somewhere.

A thing whose fingers each do something of their own has them to itself with `finger_down`, `finger_move` and `finger_up`: the keyboard on the screen (`src/thing-keyboard.c`) is one. With `over_all` it is also asked before anything else, the door included, and `text_wanted` tells it when something has come up to type in.

## Pictures

Things on the desk are rendered ahead in Blender, one script to a thing in `art/<thing>/`, and read when they are wanted (`desk_art_find`). `art/kit.py` has what the scripts share: materials, rounded shapes and keys, lights, one eye, the film, pieces of a picture faded at their edges, sheets of keys up and held down, lights under glass, and polish.

```python
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import kit
```

`art/phone/phone.py` is the one to start from. Some things that have cost time:

- **Light in the mirror direction turns black glass grey.** Flat faces seen by one far eye reflect a single direction: `(0, sin tilt, cos tilt)`. Keep lamps out of it.
- **A polished face is what it has in it.** `seen_only_in_polish` is a sheet of light only glossy rays can see, and light linking keeps it off the keys. A white reflection washes colour out and brings back the look of metal, so make it the colour of the face, and weak.
- **Whatever is stretched must not change along the stretch.** A picture that will be drawn out sideways can change only up and down (`light_by_height`), and keys laid on it at new places then still match.
- **A flat face must be flat.** A smooth-shaded n-gon bends what it reflects along its triangles: `o.data.polygons[-1].use_smooth = False`.
- **Say measures once.** `kit.write_map` writes the pixels the C needs into a header beside the pictures (`art/phone/phone-map.h`).

The same script makes the same files, to the byte. A change that should change nothing is checked by rendering before and after with `--quick` and comparing.

Add the pictures to `meson.build` with `install_data`, into `share/desk/<thing>`, as the others are.

## Before it goes in

Try every one of these, on a nested desk (`./scripts/desk-window`), with a sanitizer build (MODDING.md §10):

- The window at its smallest, and very large.
- The whole screen, and back.
- Picked up, carried, pulled by each edge.
- Alt+Tab.
- Two screens (`WLR_X11_OUTPUTS=2` nested in X).
- The pictures taken away (`DESK_ART_DIR` pointing at an empty folder): it must be paper.
- Installed, not run from the build (`meson install --destdir`): the pictures must be found.
- `WLR_RENDERER=gles2` and `pixman`, if it does anything to the scene but move what it has drawn.
- The Low level of graphics, and an old machine's speed.

Words go through `TR()`, and in the table in `src/i18n.c`, in all the languages (`meson test words` looks).
