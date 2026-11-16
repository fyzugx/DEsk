"""The pencil case that lies under the notebook, made in Blender.

An open case of brown leather, stitched round its rim and lined with suede, and in
it what the notebook is written in with: a pen of four refills in the front, turned
to the one that is out, a highlighter and a rubber behind. DEsk lays it under the
notebook's window and turns the pen with the hand (src/thing-notebook.c).

    blender -b --factory-startup -P art/notebook/pencilcase.py -- OUTDIR [--quick]
        [--samples N] [--only case|pen|marker|rubber]

What it renders, in OUTDIR, through the notebook's eye (art/notebook/notebook.py: 13
degrees to the front, 30 pixels to the centimetre, its lamp):

    nb-case.png         the case, empty, on its own shadow on the desk
    nb-pen-roll.png     the pen in its place, alone, its refills in, turned through a
                        whole turn in ROLL_FRAMES steps, one under another
    nb-pen-rest.png     the same at rest on each refill, that refill out
    nb-pen-shadow.png   what the pen throws on the case, without the pen
    nb-marker.png       the highlighter in its place, alone; nb-marker-shadow.png
    nb-rubber.png       the rubber in its place, alone; nb-rubber-shadow.png
    case-map.h          where each of them goes in nb-case.png, for src/

Taken out of the case, a thing is its own picture again, higher, its shadow further off
and softer: the desk draws that (it is the same light).

The sizes are in centimetres; x to the right, y away from the eye, z up, the desk at
z = 0. Nothing on any of it is a make's.
"""

import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

import kit
from kit import MAT, QUICK, arg, grain, principled, put, rrect

ONLY = arg('--only', '')
SAMPLES = int(arg('--samples', 16 if QUICK else 256))
ROLL_SAMPLES = max(16, SAMPLES // 2)

PX = 30.0                     # as the notebook's
TILT = math.radians(13.0)
COS, SIN = math.cos(TILT), math.sin(TILT)
MARGIN = 26                   # picture past the case, for its shadow
ROLL_FRAMES = 32              # a whole turn of the pen; four of them a refill apart

# ---- the case
L, D, H = 15.6, 3.3, 1.45     # outside
WALL, FLOOR = 0.30, 0.22
R = 0.75                      # its corners, outside
# ---- what is in it
PEN_Y, PEN_R, PEN_X0, PEN_LEN = 0.96, 0.56, 1.0, 13.6
MARK_Y, MARK_R, MARK_X0, MARK_LEN = 2.36, 0.62, 0.85, 10.5
RUB_X0, RUB_X1, RUB_Y0, RUB_Y1, RUB_Z1 = 11.75, 14.95, 1.74, 2.96, 1.30

INKS = [('black', (0.012, 0.012, 0.014)), ('blue', (0.012, 0.045, 0.32)),
        ('red', (0.42, 0.012, 0.010)), ('green', (0.006, 0.17, 0.045))]

MAT.update({
    'leather': grain(principled('leather', (0.255, 0.105, 0.040), rough=0.48, spec=0.38, coat=0.12,
                                coat_rough=0.25), 55.0, 0.010, 0.8),
    'leather_edge': grain(principled('leather edge', (0.16, 0.062, 0.022), rough=0.55, spec=0.3),
                          70.0, 0.008, 0.6),
    'suede': grain(principled('suede', (0.50, 0.36, 0.22), rough=0.95, spec=0.12, sss=0.0), 140.0, 0.016, 1.0),
    'thread': principled('thread', (0.74, 0.62, 0.42), rough=0.7, spec=0.25),
    'lacquer': principled('lacquer', (0.010, 0.018, 0.055), rough=0.16, spec=0.6, coat=0.9, coat_rough=0.04),
    'grip': grain(principled('grip', (0.022, 0.022, 0.026), rough=0.62, spec=0.3), 160.0, 0.006, 0.6),
    'chrome': principled('chrome', (0.86, 0.86, 0.84), rough=0.12, metallic=1.0),
    'satin': principled('satin', (0.80, 0.80, 0.79), rough=0.32, metallic=1.0),
    'tip': principled('tip', (0.70, 0.70, 0.68), rough=0.2, metallic=1.0),
    'yellow': principled('yellow', (1.0, 0.76, 0.0), rough=0.34, spec=0.45, coat=0.2),
    'cap': principled('cap', (0.20, 0.21, 0.23), rough=0.36, spec=0.5, coat=0.3),
    'rubber_white': grain(principled('rubber white', (0.80, 0.80, 0.78), rough=0.85, spec=0.18,
                                     sss=0.15), 90.0, 0.004, 0.5),
    'sleeve': grain(principled('sleeve', (0.23, 0.40, 0.62), rough=0.6, spec=0.3), 120.0, 0.004, 0.4),
})
for name, colour in INKS:
    MAT['ink ' + name] = principled('ink ' + name, colour, rough=0.18, spec=0.6, coat=0.8, coat_rough=0.05)


# ---- shapes

def ring(hw, hh, r, z, n=10, inset=0.0):
    return [(x, y, z) for x, y in rrect(hw - inset, hh - inset, r - inset, n, L * 0.5, D * 0.5)]


def make_case():
    hw, hh = L * 0.5, D * 0.5
    # the leather outside and over the rim, its bottom edge rounded under
    outside = [ring(hw, hh, R, 0.0, inset=0.10), ring(hw, hh, R, 0.04, inset=0.025), ring(hw, hh, R, 0.12),
               ring(hw, hh, R, H - 0.10), ring(hw, hh, R, H - 0.025, inset=0.02),
               ring(hw, hh, R, H, inset=0.08), ring(hw, hh, R, H, inset=WALL - 0.08),
               ring(hw, hh, R, H - 0.03, inset=WALL - 0.02), ring(hw, hh, R, H - 0.10, inset=WALL)]
    kit.loft('case', outside, 'leather', cap_first=True, sharp=50.0)
    # the suede inside, down from under the rim to the floor
    inside = [ring(hw, hh, R, H - 0.10, inset=WALL), ring(hw, hh, R, FLOOR + 0.10, inset=WALL),
              ring(hw, hh, R, FLOOR + 0.015, inset=WALL + 0.06), ring(hw, hh, R, FLOOR, inset=WALL + 0.16)]
    kit.loft('lining', inside, 'suede', cap_first=False, centre_last=(L * 0.5, D * 0.5, FLOOR), sharp=50.0)
    # its seam: stitches along the middle of the rim
    pts = ring(hw, hh, R, H + 0.004, n=24, inset=WALL * 0.5)
    seg = [(pts[i], pts[(i + 1) % len(pts)]) for i in range(len(pts))]
    total = sum((Vector(b) - Vector(a)).length for a, b in seg)
    step, along, k = 0.32, 0.0, 0
    bm = bmesh.new()
    while along < total - step * 0.5:
        # where along the seam, and which way it goes there
        t, s = along, 0
        while t > (Vector(seg[s][1]) - Vector(seg[s][0])).length:
            t -= (Vector(seg[s][1]) - Vector(seg[s][0])).length
            s += 1
        a, b = Vector(seg[s][0]), Vector(seg[s][1])
        d = (b - a).normalized()
        c = a + d * t
        half = 0.105
        side = Vector((-d.y, d.x, 0.0)) * 0.034
        p0, p1 = c - d * half, c + d * half
        v = [bm.verts.new(p) for p in (p0 - side, p1 - side, p1 + side, p0 + side)]
        top = [bm.verts.new(p + Vector((0.0, 0.0, 0.028))) for p in (p0 - side * 0.6, p1 - side * 0.6,
                                                                  p1 + side * 0.6, p0 + side * 0.6)]
        bm.faces.new(top)
        for i in range(4):
            bm.faces.new((v[i], v[(i + 1) % 4], top[(i + 1) % 4], top[i]))
        along += step
        k += 1
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    put('stitches', bm, 'thread', sharp=30.0)


def tube(name, rings_, mat, sides=48):
    """A round thing along x: rings of (along, radius); both ends closed."""
    bm = bmesh.new()
    loops = []
    for along, rad in rings_:
        loops.append([bm.verts.new((along, rad * math.cos(6.2832 * k / sides), rad * math.sin(6.2832 * k / sides)))
                      for k in range(sides)])
    for i in range(len(loops) - 1):
        for k in range(sides):
            bm.faces.new((loops[i][k], loops[i][(k + 1) % sides], loops[i + 1][(k + 1) % sides], loops[i + 1][k]))
    bm.faces.new(list(reversed(loops[0])))
    bm.faces.new(loops[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat, sharp=40.0)


def inlay(name, x0, x1, angle, rad, half, mat):
    """A coloured lozenge let into a barrel, round its side at angle, half wide in radians."""
    bm = bmesh.new()
    nx, na = 14, 8
    rows = []
    for i in range(nx + 1):
        x = x0 + (x1 - x0) * i / nx
        # its ends rounded: narrower towards them
        e = min(1.0, min(x - x0, x1 - x) / ((x1 - x0) * 0.22))
        w = half * math.sqrt(max(0.0, 1.0 - (1.0 - e) ** 2))
        rows.append([bm.verts.new((x, (rad + 0.004) * math.cos(angle + w * (2.0 * j / na - 1.0)),
                                   (rad + 0.004) * math.sin(angle + w * (2.0 * j / na - 1.0))))
                     for j in range(na + 1)])
    for i in range(nx):
        for j in range(na):
            bm.faces.new((rows[i][j], rows[i + 1][j], rows[i + 1][j + 1], rows[i][j + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat)


PEN_PARTS = []
TIPS = {}


def make_pen():
    """Lying along x from its point at 0; turned about its own length. Its point is a cone of
    chrome; the grip; a ring; the lacquered barrel with a refill's colour let into it four times
    round; an end of chrome."""
    n = PEN_LEN
    parts = [
        tube('pen nose', [(0.0, 0.10), (0.25, 0.17), (0.9, 0.40), (1.35, 0.50), (1.42, 0.505)], 'chrome'),
        tube('pen grip', [(1.42, 0.50), (1.5, 0.53), (4.2, 0.53), (4.28, 0.51)], 'grip'),
        tube('pen ring', [(4.28, 0.53), (4.33, 0.565), (4.55, 0.565), (4.6, 0.55)], 'chrome'),
        tube('pen barrel', [(4.6, 0.56), (n - 3.55, 0.558)], 'lacquer'),
        # the band the refills' colours are let into, in satin metal: black shows on it too
        tube('pen band', [(n - 3.55, 0.558), (n - 3.5, 0.562), (n - 1.3, 0.562), (n - 1.25, 0.556)], 'satin'),
        tube('pen barrel end', [(n - 1.25, 0.556), (n - 1.15, 0.555)], 'lacquer'),
        tube('pen end', [(n - 1.15, 0.555), (n - 1.05, 0.565), (n - 0.35, 0.55), (n - 0.12, 0.46), (n, 0.18)],
             'chrome'),
    ]
    for k, (name, _c) in enumerate(INKS):
        parts.append(inlay('pen ' + name, n - 3.25, n - 1.55, math.pi * 0.5 * (1 + k), 0.562, 0.40, 'ink ' + name))
    PEN_PARTS.extend(parts)
    # the refill out: a short sleeve of its colour, and the ball at its point
    for name, _c in INKS:
        sleeve = tube('refill ' + name, [(-0.30, 0.055), (0.05, 0.075)], 'ink ' + name, 24)
        ball = tube('ball ' + name, [(-0.40, 0.0), (-0.38, 0.026), (-0.33, 0.034), (-0.30, 0.040)], 'tip', 24)
        TIPS[name] = [sleeve, ball]


def pen_pose(s, out):
    """The pen in its place, turned so that refill s (0 black .. 3 green, in between too) is up,
    that refill out if out."""
    theta = -math.pi * 0.5 * s
    for o in PEN_PARTS + [p for parts in TIPS.values() for p in parts]:
        o.location = (PEN_X0, PEN_Y, FLOOR + PEN_R + 0.012)
        o.rotation_euler = (theta, 0.0, 0.0)
    for k, (name, _c) in enumerate(INKS):
        for p in TIPS[name]:
            p.hide_render = not (out and k == int(round(s)) % 4)


MARK_PARTS = []


def make_marker():
    """A fat highlighter: its body, the cap over its chisel, a clip along the top of the cap."""
    n = MARK_LEN
    parts = [
        tube('marker end', [(0.0, 0.30), (0.08, 0.50), (0.25, 0.60), (0.5, 0.62)], 'yellow'),
        tube('marker body', [(0.5, 0.62), (n - 3.6, 0.62), (n - 3.5, 0.60)], 'yellow'),
        tube('marker cap', [(n - 3.55, 0.645), (n - 3.45, 0.66), (n - 0.5, 0.66), (n - 0.15, 0.56), (n, 0.30)], 'cap'),
    ]
    bm = bmesh.new()
    x0, x1 = n - 3.2, n - 0.7
    pts = [(x0, -0.13), (x1, -0.12), (x1, 0.12), (x0, 0.13)]
    lo = [bm.verts.new((x, y, 0.655)) for x, y in pts]
    hi = [bm.verts.new((x, y * 0.9, 0.73)) for x, y in pts]
    bm.faces.new(hi)
    bm.faces.new(list(reversed(lo)))
    for i in range(4):
        bm.faces.new((lo[i], lo[(i + 1) % 4], hi[(i + 1) % 4], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    parts.append(put('marker clip', bm, 'cap', sharp=30.0))
    for o in parts:
        o.location = (MARK_X0 + n, MARK_Y, FLOOR + MARK_R + 0.012)
        o.rotation_euler = (math.radians(14.0), 0.0, math.pi)
    MARK_PARTS.extend(parts)


RUB_PARTS = []


def rounded_box(name, x0, x1, y0, y1, z0, z1, r, mat):
    hw, hh = (x1 - x0) * 0.5, (y1 - y0) * 0.5
    cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
    rings_ = [[(x, y, z0) for x, y in rrect(hw - r, hh - r, r * 0.3, 6, cx, cy)],
              [(x, y, z0 + r) for x, y in rrect(hw, hh, r, 6, cx, cy)],
              [(x, y, z1 - r) for x, y in rrect(hw, hh, r, 6, cx, cy)],
              [(x, y, z1) for x, y in rrect(hw - r, hh - r, r * 0.3, 6, cx, cy)]]
    return kit.loft(name, rings_, mat, cap_first=True, cap_last=True, sharp=60.0)


def make_rubber():
    z0 = FLOOR + 0.005
    RUB_PARTS.append(rounded_box('rubber', RUB_X0, RUB_X1, RUB_Y0, RUB_Y1, z0, RUB_Z1, 0.12, 'rubber_white'))
    # its paper sleeve, round the middle of it, the ends of it bare
    sx0, sx1 = RUB_X0 + 0.55, RUB_X1 - 0.85
    RUB_PARTS.append(rounded_box('sleeve', sx0, sx1, RUB_Y0 - 0.025, RUB_Y1 + 0.025, z0 - 0.002, RUB_Z1 + 0.025,
                                 0.13, 'sleeve'))


def group(fn):
    before = set(bpy.data.objects)
    fn()
    made = [o for o in bpy.data.objects if o not in before]
    for o in made:
        o.hide_render = True

    def show(on):
        for o in made:
            o.hide_render = not on
    return show, made


# ---- the desk under it: only its shadow and what it throws back
desk = kit.desk_plane()

# ---- the light: the notebook's (art/notebook/notebook.py), the notebook lying behind the case
W_NB, H_NB = 546 / PX, 771 / PX / COS
CX, CY = L * 0.5, D * 0.5
NB_O = Vector((CX - W_NB * 0.5, D + 0.45, 0.0))   # the front left corner of its sheet


def nb(x, y, z):
    return Vector((x, y, z)) + NB_O


MID = nb(W_NB * 0.5, H_NB * 0.5, 1.0)
area = kit.area
area('lamp', nb(W_NB * 0.5 - 30.0, H_NB * 0.5 + 22.0, 46.0), MID, 26.0, 26.0, 4.9, (1.0, 0.95, 0.88))
area('over', nb(W_NB * 0.5 - 8.0, H_NB * 0.5 + 10.0, 80.0), MID, 60.0, 30.0, 1.75, (1.0, 0.98, 0.95))
area('back', nb(W_NB * 0.5 + 6.0, H_NB * 0.5 + 62.0, 60.0), MID, 90.0, 26.0, 0.45, (1.0, 0.98, 0.95))
area('strip', nb(W_NB * 0.5 + 40.0, H_NB * 0.5 + 10.0, 34.0), MID, 8.0, 50.0, 1.4, (0.96, 0.98, 1.0))
area('room', nb(W_NB * 0.5, H_NB * 0.5 - 60.0, 50.0), MID, 90.0, 40.0, 0.36, (1.0, 0.97, 0.92))
kit.room((0.05, 0.043, 0.036))

kit.eye(TILT, PX)
kit.film(SAMPLES, exposure=float(arg('--exposure', 0.95)))
shoot, save = kit.shoot, kit.save

# the picture: the case's back left corner, at the top of its rim, MARGIN in from its top left
TOP = MARGIN
W_PX = int(round(L * PX)) + 2 * MARGIN
H_PX = int(math.ceil((D * COS + H * SIN) * PX)) + 2 * MARGIN + 8


def frame():
    kit.frame(MARGIN, TOP, W_PX, H_PX, (0.0, D, H))


case_show, case_objs = group(make_case)
pen_show, _p = group(make_pen)
mark_show, _m = group(make_marker)
rub_show, _r = group(make_rubber)


def catching(on):
    """The case only for the shadows on it, and for hiding what it is in front of; or as it is."""
    for o in case_objs:
        o.is_shadow_catcher = on
    desk.hide_render = on


def clean(img):
    """Below a whisper, nothing: what a shadow catcher leaves where there is no shadow."""
    img = img.copy()
    a = (img[:, :, 3].astype(np.float32) - 5.0).clip(0.0, 250.0) * 255.0 / 250.0
    img[:, :, 3] = (a + 0.5).astype(np.uint8)
    return img


def bbox(imgs):
    a = np.zeros(imgs[0].shape[:2], dtype=bool)
    for img in imgs:
        a |= img[:, :, 3] > 0
    ys, xs = np.nonzero(a)
    pad = 2
    return (max(0, xs.min() - pad), max(0, ys.min() - pad), min(a.shape[1], xs.max() + 1 + pad),
            min(a.shape[0], ys.max() + 1 + pad))


def crop(img, b):
    return img[b[1]:b[3], b[0]:b[2]]


def faded(img, fade):
    """The far edge of the shadow goes: it must not stop in a straight line on the desk."""
    img = img.copy()
    a = (img[:, :, 3].astype(np.float32) - 4.0).clip(0.0, 251.0) * 255.0 / 251.0
    h, w = a.shape
    rx = (np.minimum(np.arange(w), np.arange(w)[::-1]) / float(fade)).clip(0.0, 1.0)
    ry = (np.minimum(np.arange(h), np.arange(h)[::-1]) / float(fade)).clip(0.0, 1.0)
    img[:, :, 3] = (a * np.outer(ry * ry * (3.0 - 2.0 * ry), rx * rx * (3.0 - 2.0 * rx)) + 0.5).astype(np.uint8)
    return img


measures = [('CASE_W', W_PX, 'nb-case.png'), ('CASE_H', H_PX),
            ('CASE_LEFT', MARGIN, 'the case\'s outside, from the picture\'s left'),
            ('CASE_TOP', TOP, 'and the top of its rim at the back'),
            ('CASE_RIGHT', MARGIN + int(round(L * PX))),
            ('CASE_FOOT', TOP + int(math.ceil((D * COS + H * SIN) * PX)), 'its foot at the front'),
            ('PEN_FRAMES', ROLL_FRAMES, 'in a whole turn of the pen')]
frame()

if ONLY in ('', 'case'):
    case_show(True)
    catching(False)
    save('nb-case.png', faded(shoot('case', SAMPLES), 10))
    case_show(False)


def pieces(name, show, poses, samples):
    """A thing alone, in each pose, cropped alike: its pictures one under another."""
    show(True)
    desk.hide_render = True
    imgs = []
    for i, pose in enumerate(poses):
        pose()
        imgs.append(clean(shoot(f'{name}-{i:02d}', samples)))
    desk.hide_render = False
    show(False)
    b = bbox(imgs)
    save(name + '.png', np.concatenate([crop(img, b) for img in imgs], axis=0))
    return b


def shadow(name, objs, pose, samples):
    """Only what a thing throws on the case, the thing itself not seen."""
    for o in objs:
        o.hide_render = False
        o.visible_camera = False
    pose()
    case_show(True)
    catching(True)
    img = clean(shoot(name, samples))
    catching(False)
    case_show(False)
    for o in objs:
        o.hide_render = True
        o.visible_camera = True
    b = bbox([img])
    save(name + '.png', crop(img, b))
    return b


def where(prefix, b):
    return [(prefix + '_X', b[0]), (prefix + '_Y', b[1]), (prefix + '_W', b[2] - b[0]), (prefix + '_H', b[3] - b[1])]


if ONLY in ('', 'pen'):
    tips = [p for parts in TIPS.values() for p in parts]
    measures += where('PEN_SHADOW', shadow('nb-pen-shadow', PEN_PARTS + tips, lambda: pen_pose(1, True), SAMPLES))
    measures += where('PEN', pieces('nb-pen-roll', pen_show, [lambda s=f: pen_pose(s * 4.0 / ROLL_FRAMES, False)
                                                             for f in range(ROLL_FRAMES)], ROLL_SAMPLES))
    measures += where('PEN_REST', pieces('nb-pen-rest', pen_show, [lambda k=k: pen_pose(k, True) for k in range(4)],
                                         SAMPLES))
    # how far, on the screen, the pen's top goes round when it turns once: its radius
    measures += [('PEN_RADIUS', int(round(PEN_R * PX)), 'pixels, as the pen lies')]

if ONLY in ('', 'marker'):
    measures += where('MARKER_SHADOW', shadow('nb-marker-shadow', _m, lambda: None, SAMPLES))
    measures += where('MARKER', pieces('nb-marker', mark_show, [lambda: None], SAMPLES))

if ONLY in ('', 'rubber'):
    measures += where('RUBBER_SHADOW', shadow('nb-rubber-shadow', _r, lambda: None, SAMPLES))
    measures += where('RUBBER', pieces('nb-rubber', rub_show, [lambda: None], SAMPLES))

if ONLY == '':
    kit.write_map('case-map.h', 'The pencil case under the notebook (art/notebook/pencilcase.py), in pixels: '
                  'nb-case.png, and where each of what is in it goes on it.', measures)
print('pencilcase.py: done', flush=True)
