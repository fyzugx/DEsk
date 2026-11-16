"""The phone on the desk, made in Blender.

What a picture-in-picture window is on DEsk: the screen of a slider phone of
the old kind, made as if it had been made to last. A slab of black glass in a
band of polished steel with well rounded corners, held on its side. Down its right
side, as such phones had along their foot: a red cross and an arrow, which are not
keys but lights under the glass that answer a touch, and between them a real button
that stops and starts the picture. On its left, the earpiece and the name cut into
the glass. From under the slab, on rails, comes a deck of deep blue under a polish,
with what else a player needs. DEsk lays it round the window (src/phone.c).

    blender -b --factory-startup -P art/phone/phone.py -- OUTDIR [--quick]
        [--samples N] [--only slab|deck]

What it renders, in OUTDIR, all through one eye, one pixel of the picture to one
pixel of the screen:

    ph-slab.png   the slab with nothing on it: DEsk keeps the corners and stretches
                  the rest to the window's size, and the program's picture lies in
                  the middle of it
    ph-ear.png    the earpiece, the lens and the name, for the middle of its left side
    ph-deck.png   the deck with no keys: DEsk keeps both ends and stretches the
                  middle, which changes only up and down
    ph-keys.png   the keys, a row of them up and a row of them held down, each in
                  a cell of CELL_W by CELL_H, its edge faded into what is under it:
                  the lights too, lit and touched, and the slider's knob

    phone-map.h   the measures in pixels, for src/phone.c (it goes in art/phone with them)

The sizes are in centimetres; x to the right, y away from the eye, z up, the desk
at z = 0. The phone is original: nothing on it is a make's, and the words on it
are only what its keys do.
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

import kit
from kit import GROUPS, MAT, arg, brushed, cap, collar, decal, loft, plinth, principled, rrect

QUICK = kit.QUICK
ONLY = arg('--only', '')
SAMPLES = int(arg('--samples', 16 if QUICK else 384))

# ---- the measures, pixels on the screen first: src/phone.c has them from phone-map.h
PX = 30.0
SLAB_W_PX, SLAB_H_PX = 580, 322
INSET_X_PX, INSET_Y_PX = 52, 26    # the window, from the slab's edge
PAD = 44                           # picture past the slab on every side
KEY_X_PX = 26                      # the keys' middles, from the slab's right
KEY_Y_PX = 44                      # and the lights', from its top and its foot
HOME_W_PX, HOME_H_PX = 28, 60      # the button, in the middle of that side
EAR_X0, EAR_X1 = 4, 50             # ph-ear.png, from the slab's left
EAR_ABOVE, EAR_BELOW = 101, 75     # and from its middle
EARPIECE_X, NAME_X = 15, 34        # the earpiece's middle and the name's

DECK_W_PX, DECK_H_PX = 560, 186    # the deck's plate
DECK_SIDE, DECK_FOOT = 30, 40      # picture past it, at each side and under it
SLIDER_Y = 62                      # the slider's middle, from the plate's top
SLIDER_X0, SLIDER_X1 = 102, 458
TIME_W = 62                        # the windows the times are in
TIME_L = 32
KEY_Y0, KEY_Y1 = 90, 158           # the row of keys
# x0, x1 of each, from the plate's left
KEYS_PX = {'back': (32, 190), 'fwd': (201, 359), 'open': (370, 528)}

CELL_W, CELL_H = 176, 92           # ph-keys.png's cells
FADE = 6                           # how far outside a key its piece goes on
CELLS = ['back', 'fwd', 'open', 'close', 'down', 'up', 'knob', 'home']

TILT = math.radians(13.0)
COS, SIN = math.cos(TILT), math.sin(TILT)
W = SLAB_W_PX / PX
H = SLAB_H_PX / PX / COS
DW = DECK_W_PX / PX
DH = DECK_H_PX / PX / COS

Z_DECK = 0.44                      # the deck's top
Z_SLAB0, ZT = 0.46, 1.42           # the slab, from its underside to its glass

# ---- materials
MAT.update({
    'glass': principled('glass', (0.003, 0.003, 0.004), rough=0.06, spec=0.6),
    'screen': principled('screen', (0.001, 0.001, 0.001), rough=0.3, spec=0.2),
    'bright': principled('bright', (0.82, 0.82, 0.81), rough=0.09, metallic=1.0),
    'dark steel': principled('dark steel', (0.20, 0.20, 0.21), rough=0.16, metallic=1.0),
    'plate': principled('plate', (0.014, 0.031, 0.082), rough=0.30, metallic=0.35, coat=1.0),
    'rim': principled('rim', (0.22, 0.27, 0.36), rough=0.10, metallic=1.0),
    'pit': principled('pit', (0.003, 0.003, 0.003), rough=0.7),
    'slot': principled('slot', (0.006, 0.006, 0.007), rough=0.34, spec=0.5),
    'black glass': principled('black glass', (0.002, 0.002, 0.002), rough=0.05, spec=0.6, coat=1.0),
    'knob': principled('knob', (0.50, 0.56, 0.66), rough=0.20, metallic=1.0),
    'mesh': principled('mesh', (0.012, 0.012, 0.013), rough=0.42),
    'lens': principled('lens', (0.002, 0.003, 0.012), rough=0.03, spec=0.9, coat=1.0),
})


SANS = "font-family='Nimbus Sans' font-weight='700'"


def legend_svg(s):
    """A key's print, white on nothing, in a 512-square picture."""
    def arrows(x, sign):
        # two wedges, nose to tail
        a = ''
        for k in (0, 1):
            x0 = x + sign * k * 58
            a += f"<path d='M{x0} 256 L{x0 - sign * 62} 214 L{x0 - sign * 62} 298 Z'/>"
        return a
    if s == 'back':
        body = arrows(120, -1) + f"<text x='292' y='284' text-anchor='middle' {SANS} font-size='78'>10</text>"
    elif s == 'fwd':
        body = f"<text x='220' y='284' text-anchor='middle' {SANS} font-size='78'>10</text>" + arrows(392, 1)
    elif s == 'play':
        body = ("<path d='M150 186 L276 256 L150 326 Z'/>"
                "<rect x='304' y='186' width='32' height='140' rx='4'/>"
                "<rect x='362' y='186' width='32' height='140' rx='4'/>")
    elif s == 'name':
        # cut into the glass: its far side in shadow, its near side catching the light
        word = "y='42' text-anchor='middle' font-family='Nimbus Sans' font-size='120' letter-spacing='7'>DEsk</text>"
        body = (f"<text transform='translate(261 261) rotate(-90)' fill='#c8c8c8' {word}"
                f"<text transform='translate(256 256) rotate(-90)' fill='#4a4a4a' {word}")
    elif s == 'open':
        body = ("<g fill='none' stroke='#fff' stroke-width='11' stroke-linejoin='round' stroke-linecap='round'>"
                "<path d='M172 232 H116 V314 H198 V258'/><path d='M154 276 L216 214 M174 212 H218 V256'/></g>"
                f"<text x='352' y='284' text-anchor='middle' {SANS} font-size='58' letter-spacing='3'>OPEN</text>")
    elif s == 'close':
        body = ("<g fill='none' stroke='#fff' stroke-width='50' stroke-linecap='round'>"
                "<path d='M120 120 L392 392 M392 120 L120 392'/></g>")
    elif s == 'down':
        body = ("<g fill='none' stroke='#fff' stroke-width='54' stroke-linecap='round' stroke-linejoin='round'>"
                "<path d='M130 204 L256 330 L382 204'/></g>")
    else:
        body = ("<g fill='none' stroke='#fff' stroke-width='54' stroke-linecap='round' stroke-linejoin='round'>"
                "<path d='M130 308 L256 182 L382 308'/></g>")
    return ("<svg xmlns='http://www.w3.org/2000/svg' width='512' height='512' viewBox='0 0 512 512' "
            f"fill='#fff'>{body}</svg>")


def printed(name, base, ink, rough, coat, legend, side, **more):
    return kit.printed(name, base, ink, rough, coat, kit.legend_image(legend, legend_svg(legend)), side, **more)


KEYS = {}                     # name: the key's cap, to press and to hide


# from pixels on the screen, on the slab's glass or on the deck's plate
def sx(px):
    return px / PX


def sy(py):
    return H - py / PX / COS


def dy(py):
    return DH - py / PX / COS


# ---- the slab
kit.group('slab')
R_OUT = 38.0 / PX
BAND = 0.11                   # the polished edge round the glass
mid_x, mid_y = W * 0.5, H * 0.5
rings = [[(x, y, Z_SLAB0) for x, y in rrect(W * 0.5 - 0.05, H * 0.5 - 0.05, R_OUT - 0.05, 16, mid_x, mid_y)],
         [(x, y, Z_SLAB0 + 0.06) for x, y in rrect(W * 0.5, H * 0.5, R_OUT, 16, mid_x, mid_y)],
         [(x, y, ZT - 0.22) for x, y in rrect(W * 0.5, H * 0.5, R_OUT, 16, mid_x, mid_y)]]
loft('band', rings, brushed('band', (0.72, 0.72, 0.71), 0.24, lines=44.0, deep=0.002), cap_last=True, sharp=60.0)
# its edge rounded over to the glass: polished, so it has the lights in it
rings = []
for k in range(0, 8):
    a = math.radians(90.0 * k / 7)
    d = 0.20 * (1.0 - math.cos(a))
    z = ZT - 0.22 + 0.20 * math.sin(a)
    rings.append([(x, y, z) for x, y in rrect(W * 0.5 - d, H * 0.5 - d, R_OUT - d, 16, mid_x, mid_y)])
inset = 0.20
rings.append([(x, y, ZT - 0.012) for x, y in rrect(W * 0.5 - inset - 0.03, H * 0.5 - inset - 0.03, R_OUT - inset - 0.03, 16, mid_x, mid_y)])
loft('edge', rings, 'bright', sharp=None, cap_first=False, cap_last=False)
plinth('glass', mid_x, mid_y, W * 0.5 - inset - 0.02, H * 0.5 - inset - 0.02, R_OUT - inset - 0.02, ZT - 0.2, ZT, 'glass', bevel=0.012)
# where the window lies: a hair of dark steel round a well
ww = (SLAB_W_PX - INSET_X_PX * 2) / PX / 2.0
wh = (SLAB_H_PX - INSET_Y_PX * 2) / PX / COS / 2.0
plinth('screen', mid_x, mid_y, ww + 0.01, wh + 0.01, 0.05, ZT - 0.02, ZT + 0.004, 'screen')
collar('trim', mid_x, mid_y, ww, wh, 0.05, 0.05, ZT - 0.01, ZT + 0.016, 'dark steel', n=4)


KX, KY_MID = sx(SLAB_W_PX - KEY_X_PX), mid_y
KEY_OBJ_START = len(GROUPS['slab'])
INK = (0.92, 0.91, 0.88)
# the button in the middle: a polished collar, a dark gap, and a cap of dark steel you
# can feel go down. (The lights either side are nothing yet: see glow, below.)
home_hw, home_hh = HOME_W_PX / PX / 2.0, HOME_H_PX / PX / COS / 2.0
plinth('home pit', KX, KY_MID, home_hw + 0.02, home_hh + 0.02, home_hw + 0.02, ZT - 0.05, ZT + 0.003, 'pit', n=14)
collar('home collar', KX, KY_MID, home_hw - 0.02, home_hh - 0.02, home_hw - 0.02, 0.07, ZT - 0.02, ZT + 0.05, 'bright', n=14)
KEYS['home'] = cap('home', KX, KY_MID, home_hw - 0.05, home_hh - 0.05, home_hw - 0.05, ZT - 0.02, 0.15, -0.035,
                   printed('home', (0.26, 0.26, 0.27), INK, 0.26, 0.0, 'play', 1.42, metallic=1.0), n=14)
SLAB_KEY_OBJS = GROUPS['slab'][KEY_OBJ_START:]



# the earpiece and a lens, on the left, in the slab's middle; and the name, cut into the
# glass and filled, read with the phone held the other way up
EAR_OBJ_START = len(GROUPS['slab'])
EX = sx(EARPIECE_X)
plinth('ear', EX, mid_y, 0.115, 1.25, 0.115, ZT - 0.03, ZT + 0.002, 'mesh', n=8)
for i in range(30):
    plinth(f'ear hole {i}', EX, mid_y - 1.16 + i * 0.08, 0.075, 0.018, 0.018, ZT, ZT + 0.0035, 'pit', n=2)
collar('ear collar', EX, mid_y, 0.115, 1.25, 0.115, 0.04, ZT - 0.01, ZT + 0.018, 'bright', n=8)
plinth('lens', EX, mid_y + 1.86, 0.15, 0.15, 0.15, ZT - 0.01, ZT + 0.016, 'lens', bevel=0.012, n=10)
collar('lens collar', EX, mid_y + 1.86, 0.15, 0.15, 0.15, 0.035, ZT - 0.01, ZT + 0.02, 'dark steel', n=10)
decal('name', sx(NAME_X), mid_y, 0.36, 1.25, ZT + 0.0012,
      printed('name', (0.003, 0.003, 0.004), (0.70, 0.70, 0.69), 0.06, 0.0, 'name', 2.4, ink_metal=0.85, ink_picture=True))
EAR_OBJS = GROUPS['slab'][EAR_OBJ_START:]

# ---- the deck
kit.group('deck')
dx0, dy0 = DW * 0.5, DH * 0.5
o = plinth('plate', dx0, dy0, DW * 0.5, DH * 0.5, 0.46, 0.02, Z_DECK, 'plate', bevel=0.05, n=12)
o.data.polygons[-1].use_smooth = False   # truly flat, for what it has in its polish
# the rails it runs on, in slots down each side
for name, x in (('left', sx(15.0)), ('right', sx(DECK_W_PX - 15.0))):
    y0, y1 = dy(0.0) - 0.02, dy(150.0)
    plinth('rail slot ' + name, x, (y0 + y1) * 0.5, 0.135, (y0 - y1) * 0.5, 0.135, Z_DECK - 0.02, Z_DECK + 0.002, 'pit', n=6)
    plinth('rail ' + name, x, (y0 + y1) * 0.5 - 0.02, 0.085, (y0 - y1) * 0.5 - 0.05, 0.085, Z_DECK - 0.02, Z_DECK + 0.05, 'rim', bevel=0.05, n=6)

# the slider's slot, and the two windows for the times
sy0 = dy(SLIDER_Y)
sw = (SLIDER_X1 - SLIDER_X0) / PX / 2.0
scx = sx((SLIDER_X0 + SLIDER_X1) * 0.5)
plinth('slider slot', scx, sy0, sw, 0.105, 0.105, Z_DECK - 0.02, Z_DECK + 0.002, 'slot', n=8)
collar('slider collar', scx, sy0, sw, 0.105, 0.105, 0.042, Z_DECK - 0.01, Z_DECK + 0.024, 'rim', n=8)
for cx in (sx(TIME_L + TIME_W * 0.5), sx(DECK_W_PX - TIME_L - TIME_W * 0.5)):
    plinth('time', cx, sy0, TIME_W / PX / 2.0, 0.40, 0.07, Z_DECK - 0.01, Z_DECK + 0.003, 'black glass', n=6)
    collar('time collar', cx, sy0, TIME_W / PX / 2.0, 0.40, 0.07, 0.05, Z_DECK - 0.01, Z_DECK + 0.03, 'rim', n=6)

CERAMIC = (0.009, 0.009, 0.010)
KEY_OBJ_START = len(GROUPS['deck'])
ky = dy((KEY_Y0 + KEY_Y1) * 0.5)
khh = (KEY_Y1 - KEY_Y0) / PX / COS / 2.0
for name, (x0, x1) in KEYS_PX.items():
    kx = sx((x0 + x1) * 0.5)
    khw = (x1 - x0) / PX / 2.0
    plinth(name + ' pit', kx, ky, khw - 0.02, khh - 0.02, 0.30, Z_DECK - 0.03, Z_DECK + 0.002, 'pit', n=8)
    collar(name + ' collar', kx, ky, khw - 0.06, khh - 0.06, 0.26, 0.06, Z_DECK - 0.01, Z_DECK + 0.05, 'rim', n=8)
    m = printed(name, CERAMIC, INK, 0.14, 0.5, name, 3.6)
    KEYS[name] = cap(name, kx, ky, khw - 0.10, khh - 0.10, 0.23, Z_DECK - 0.02, 0.27, 0.022, m, n=8)
DECK_KEY_OBJS = GROUPS['deck'][KEY_OBJ_START:]

# the slider's knob: a polished block with its edges rounded, a line cut across it
KNOB_OBJS = []
kn_x = sx(SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) * 0.25)
KEYS['knob'] = plinth('knob', kn_x, sy0, 0.34, 0.50, 0.11, Z_DECK - 0.01, Z_DECK + 0.30, 'knob', bevel=0.09, n=8)
KNOB_OBJS.append(KEYS['knob'])
KNOB_OBJS.append(plinth('knob line', kn_x, sy0, 0.028, 0.44, 0.01, Z_DECK + 0.27, Z_DECK + 0.3035, 'pit', n=1))


kit.desk_plane()


# ---- the light: the desk's lamp up to the left as for everything on it, and long
# soft sources for the polished steel and the glass to have something in them
area = kit.area
MID = (W * 0.5, H * 0.5, 1.0)
area('lamp', (W * 0.5 - 30.0, H * 0.5 + 22.0, 46.0), MID, 26.0, 26.0, 5.2, (1.0, 0.95, 0.88))
area('over', (W * 0.5 - 14.0, H * 0.5 - 40.0, 70.0), MID, 60.0, 30.0, 1.6, (1.0, 0.98, 0.95))
area('back', (W * 0.5 + 6.0, H * 0.5 + 62.0, 60.0), MID, 90.0, 26.0, 1.1, (1.0, 0.98, 0.95))
area('strip', (W * 0.5 + 40.0, H * 0.5 + 10.0, 34.0), MID, 8.0, 50.0, 1.7, (0.96, 0.98, 1.0))
area('room', (W * 0.5, H * 0.5 - 60.0, 50.0), MID, 90.0, 40.0, 0.9, (1.0, 0.97, 0.92))
# what the glass has in it, seen from the eye: faint, and high up behind
area('sky', (W * 0.5, H * 0.5 + 16.0, 72.0), MID, 140.0, 60.0, 0.13, (0.92, 0.96, 1.0))



SHEET_HIGH = 40.0
# how much of the window is in the deck's polish at each height of it, its foot at 0: it
# changes only up and down, since DEsk draws the deck out along its length and the keys
# move along it
WINDOW_LIGHT = ((0.04, 0.0), (0.30, 0.3), (0.565, 0.8), (0.585, 2.1), (0.85, 3.0), (0.95, 1.0))
trim = [t for t in GROUPS['deck'] if t.name == 'plate' or t.name.startswith('rail ') or t.name.endswith(' collar')]
# what it has in it: a long window, straight over it from the eye, of which only the plate
# and its rims have anything, so that the keys and the black glass stay black
z_sheet = Z_DECK + SHEET_HIGH
y_up = SHEET_HIGH * SIN / COS
em = kit.seen_only_in_polish('window', y_up - 40.0, y_up + DH + 40.0, z_sheet, (0.44, 0.58, 1.0), trim + [KEYS['knob']])
kit.light_by_height(em, y_up, y_up + DH, WINDOW_LIGHT)
# and low down in front, bright, only ever seen in the rounded edges: the polish's thin lines
kit.seen_only_in_polish('edge light', -90.0, -14.0, z_sheet, (0.70, 0.80, 1.0), trim).inputs['Strength'].default_value = 16.0

kit.room((0.09, 0.085, 0.08))

# ---- the eye
kit.eye(TILT, PX)
kit.film(SAMPLES, exposure=float(arg('--exposure', 0.0)))


def press(names, down):
    for n in names:
        o = KEYS[n]
        o.location.z = o['z0'] - down


for k, o in KEYS.items():
    o['z0'] = o.location.z

sheet = kit.Sheet('ph-keys.png', CELL_W, CELL_H, CELLS)
PRESS_ROUND, PRESS_DECK = 0.12, 0.20


RED_CORE, RED_HALO, RED_HEART = (1.0, 0.05, 0.03), (1.0, 0.04, 0.02), (0.0, 0.15, 0.09)
PALE_CORE, PALE_HALO = (0.88, 0.95, 1.0), (0.42, 0.68, 1.0)

if ONLY in ('', 'slab'):
    kit.frame(PAD, PAD, SLAB_W_PX + PAD * 2, SLAB_H_PX + PAD * 2, (0.0, H, ZT))
    kit.show('slab', hide=SLAB_KEY_OBJS + EAR_OBJS)
    base = kit.shoot('slab')
    kit.save('ph-slab.png', base)
    # the earpiece and the name, by themselves on the glass
    kit.show('slab', hide=SLAB_KEY_OBJS)
    img = kit.shoot('ear')
    ey = PAD + SLAB_H_PX // 2
    kit.save('ph-ear.png', kit.piece(img, PAD + EAR_X0, ey - EAR_ABOVE, PAD + EAR_X1, ey + EAR_BELOW))
    # the lights, lit, and brighter when they are touched
    kx = PAD + SLAB_W_PX - KEY_X_PX
    half = 26
    for row in (0, 1):
        sheet.lay('close', row, kit.glow(base, 'close', legend_svg('close'), 30, kx, PAD + KEY_Y_PX, 20, RED_CORE, RED_HALO, row == 1, near=0.7, far=0.55, heart=RED_HEART))
        sheet.lay('down', row, kit.glow(base, 'down', legend_svg('down'), 20, kx, PAD + SLAB_H_PX - KEY_Y_PX, half, PALE_CORE, PALE_HALO, row == 1))
        sheet.lay('up', row, kit.glow(base, 'up', legend_svg('up'), 20, kx, PAD + SLAB_H_PX - KEY_Y_PX, half, PALE_CORE, PALE_HALO, row == 1))
    # and the button, up and held down
    for row, down in ((0, 0.0), (1, PRESS_ROUND)):
        press(['home'], down)
        kit.show('slab', hide=EAR_OBJS)
        img = kit.shoot(f'home {row}')
        q = kit.piece(img, kx - 26, ey - 42, kx + 26, ey + 42)
        sheet.lay('home', row, kit.held(q, 14) if row else q)
    press(['home'], 0.0)

if ONLY in ('', 'deck'):
    kit.frame(DECK_SIDE, 0, DECK_W_PX + DECK_SIDE * 2, DECK_H_PX + DECK_FOOT, (0.0, DH, Z_DECK))
    kit.show('deck', hide=DECK_KEY_OBJS + KNOB_OBJS)
    kit.save('ph-deck.png', kit.shoot('deck'))
    for row, down in ((0, 0.0), (1, PRESS_DECK)):
        press(list(KEYS_PX), down)
        kit.show('deck', hide=KNOB_OBJS)
        img = kit.shoot(f'deck keys {row}')
        for name, (x0, x1) in KEYS_PX.items():
            q = kit.piece(img, DECK_SIDE + x0 - FADE, KEY_Y0 - FADE, DECK_SIDE + x1 + FADE, KEY_Y1 + FADE)
            sheet.lay(name, row, kit.held(q, FADE + 7) if row else q)
    press(list(KEYS_PX), 0.0)
    kit.show('deck', hide=DECK_KEY_OBJS)
    img = kit.shoot('knob')
    kx = DECK_SIDE + SLIDER_X0 + (SLIDER_X1 - SLIDER_X0) // 4
    p = kit.piece(img, kx - 26, SLIDER_Y - 30, kx + 26, SLIDER_Y + 30)
    sheet.lay('knob', 0, p)
    sheet.lay('knob', 1, p)

sheet.save()

kit.write_map('phone-map.h', 'What src/phone.c lays the pictures by, in pixels on the screen.', (
    ('PH_PAD', PAD, 'how far the slab\'s picture goes past it, for its shadow'),
    ('PH_L', INSET_X_PX, 'the window, from the slab\'s edge'), ('PH_R', INSET_X_PX),
    ('PH_T', INSET_Y_PX), ('PH_B', INSET_Y_PX),
    ('PH_LIGHT_X', KEY_X_PX, 'the lights\' middles, from the slab\'s right'),
    ('PH_LIGHT_Y', KEY_Y_PX, 'and from its top and its foot'),
    ('PH_EAR_X0', EAR_X0, 'ph-ear.png, from the slab\'s left'),
    ('PH_EAR_ABOVE', EAR_ABOVE, 'and from its middle'),
    ('PH_DECK_W', DECK_W_PX, 'the deck\'s plate'), ('PH_DECK_H', DECK_H_PX),
    ('PH_DECK_SIDE', DECK_SIDE, 'picture past it, at each side and under it'), ('PH_DECK_FOOT', DECK_FOOT),
    ('PH_SLIDER_Y', SLIDER_Y, 'the slider\'s middle, from the plate\'s top'),
    ('PH_SLIDER_END', SLIDER_X0, 'and how far in from each end'),
    ('PH_TIME_L', TIME_L, 'a window for a time, from the plate\'s end'), ('PH_TIME_W', TIME_W),
    ('PH_ROW_Y', KEY_Y0, 'the row of keys'), ('PH_ROW_H', KEY_Y1 - KEY_Y0),
    ('PH_ROW_LEAD', KEYS_PX['back'][0], 'the first, from the plate\'s left, and how wide each is'),
    ('PH_ROW_WIDE', KEYS_PX['back'][1] - KEYS_PX['back'][0]),
    ('PH_CELL_W', CELL_W, 'the cells of ph-keys.png'), ('PH_CELL_H', CELL_H),
    ('PH_CELL_CLOSE', CELLS.index('close')), ('PH_CELL_DOWN', CELLS.index('down')),
    ('PH_CELL_UP', CELLS.index('up')), ('PH_CELL_KNOB', CELLS.index('knob')),
    ('PH_CELL_HOME', CELLS.index('home')),
))
print('phone.py: done', flush=True)
