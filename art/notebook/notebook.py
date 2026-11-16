"""The notebook on the desk, made in Blender.

A wire-bound notebook lying open at a clean page: a stack of sheets on a stiff
cover, twin loops of wire through square holes down the left side, and the top
sheet not quite flat, because it never is. The window of desk-notepad is that
top sheet: DEsk lays the notebook round it (src/notebook.c), and the program
writes on it with nothing of its own under the letters.

    blender -b --factory-startup -P art/notebook/notebook.py -- OUTDIR [--quick]
        [--samples N] [--only page|rings|grain|shut|top|note]

What it renders, in OUTDIR, all through one eye, one pixel of the picture to one
pixel of the screen:

    nb-page.png   the notebook at the size of a B5 page, without its wire: DEsk
                  keeps the corners and stretches the rest to the window's size,
                  which only smooth light and long straight edges have to bear
    nb-rings.png  one turn of the wire down the left side, with its shadows and
                  the hole it goes through, to be laid again and again
    nb-grain.png  the paper's tooth and fibres, repeating, to multiply by
    nb-shut.png   the notebook shut, with a blank label: folding a window shuts it
    nb-top.png    the same with a pencil on it, as it lies in the drawer
    nb-note.png   a sticky note, for finding things
    desk-notepad-icon.png   the program's mark (session/desk-notepad.png)

The sizes are in centimetres; x to the right, y away from the eye, z up, the
desk at z = 0. The notebook is original: nothing on it is a make's.
"""

import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

import kit
from kit import COLL, MAT, OUT, QUICK, SCENE, arg, grain, principled, put

ONLY = arg('--only', '')
SAMPLES = int(arg('--samples', 16 if QUICK else 256))

# ---- the measures. Pixels on the screen first, since the window is in pixels:
# these must agree with include/notebook.h
PX = 30.0                     # pixels to the centimetre, across
SHEET_W_PX, SHEET_H_PX = 546, 771
MARGIN = 30                   # picture past the sheet on every side
RING_P_PX = 24                # from one hole to the next, on the screen
STRIP_PX = 46                 # how far into the sheet the wire's picture goes
PERF_PX = 29                  # the line of perforations, from the sheet's left

TILT = math.radians(13.0)     # the eye is a little to the front, as for the calculator
COS, SIN = math.cos(TILT), math.sin(TILT)
W = SHEET_W_PX / PX
H = SHEET_H_PX / PX / COS
RING_P = RING_P_PX / PX / COS

SHEET_R = 0.33                # the sheets' corners
Z_COVER0, Z_COVER1 = 0.02, 0.15
ZT = 1.02                     # the top sheet
N_SHEETS = 26
DZ = (ZT - Z_COVER1) / N_SHEETS

HOLE_X = 0.47                 # the holes' middle, from the sheet's left
HOLE_W, HOLE_H = 0.37, 0.40
STRAND = 0.115                # each wire of a pair, from the hole's middle
WIRE_R = 0.056
RING_CX, RING_CZ = -0.08, 0.54

random.seed(11)


# ---- materials
MAT.update({
    'paper': principled('paper', (0.86, 0.845, 0.79), rough=0.82, spec=0.28),
    'edge': principled('edge', (0.74, 0.71, 0.64), rough=0.9, spec=0.2),
    'edge2': principled('edge2', (0.56, 0.53, 0.47), rough=0.9, spec=0.2),
    'edge3': principled('edge3', (0.84, 0.81, 0.74), rough=0.9, spec=0.2),
    'cover': grain(principled('cover', (0.018, 0.032, 0.074), rough=0.56, spec=0.45), 70.0, 0.012, 0.7),
    'cover_top': grain(principled('cover top', (0.0085, 0.017, 0.047), rough=0.72, spec=0.22), 110.0, 0.014, 0.9),
    'pit': principled('pit', (0.012, 0.012, 0.016), rough=0.8),
    'steel': principled('steel', (0.80, 0.80, 0.78), rough=0.2, metallic=1.0),
    'perf': principled('perf', (0.44, 0.42, 0.38), rough=0.9),
    'label': principled('label', (0.83, 0.80, 0.72), rough=0.75, spec=0.3),
    'note': principled('note', (0.98, 0.84, 0.27), rough=0.8, spec=0.25),
    'paint': principled('paint', (0.86, 0.50, 0.035), rough=0.28, spec=0.5, coat=0.6),
    'wood': principled('wood', (0.72, 0.52, 0.32), rough=0.7),
    'lead': principled('lead', (0.03, 0.03, 0.035), rough=0.35, spec=0.6),
    'brass': principled('brass', (0.82, 0.70, 0.42), rough=0.26, metallic=1.0),
    'rubber': principled('rubber', (0.72, 0.28, 0.26), rough=0.7),
})


# ---- shapes

def axis(length, r, n, ends=5):
    return ([r * i / ends for i in range(ends)]
            + [r + (length - 2.0 * r) * i / n for i in range(n)]
            + [length - r + r * i / ends for i in range(ends + 1)])


def sheet(name, x0, y0, w, h, r, z, thick, height, nx, ny, mat='paper', side='edge'):
    """A sheet with round corners: its top a grid raised by height(x, y), its
    edge going down from there by thick. x and y of height are measured on the
    top sheet, so that what lies on what bends alike."""
    xs, ys = axis(w, r, nx), axis(h, r, ny)
    cols, rows = len(xs), len(ys)
    bm = bmesh.new()

    def place(x, y):
        cx = r if x < r else (w - r if x > w - r else x)
        cy = r if y < r else (h - r if y > h - r else y)
        dx, dy = x - cx, y - cy
        if (x < r or x > w - r) and (y < r or y > h - r):
            big, long = max(abs(dx), abs(dy)), math.hypot(dx, dy)
            if long > 0.0:
                dx, dy = dx * big / long, dy * big / long
        return x0 + cx + dx, y0 + cy + dy

    top = []
    for y in ys:
        for x in xs:
            px, py = place(x, y)
            top.append(bm.verts.new((px, py, z + height(px, py))))
    for j in range(rows - 1):
        for i in range(cols - 1):
            a = j * cols + i
            bm.faces.new((top[a], top[a + 1], top[a + cols + 1], top[a + cols]))
    ring = ([(i, 0) for i in range(cols)] + [(cols - 1, j) for j in range(1, rows)]
            + [(i, rows - 1) for i in range(cols - 2, -1, -1)] + [(0, j) for j in range(rows - 2, 0, -1)])
    lows = []
    for i, j in ring:
        t = top[j * cols + i]
        lows.append(bm.verts.new((t.co.x, t.co.y, t.co.z - thick)))
    n = len(ring)
    for k in range(n):
        a = top[ring[k][1] * cols + ring[k][0]]
        b = top[ring[(k + 1) % n][1] * cols + ring[(k + 1) % n][0]]
        f = bm.faces.new((a, b, lows[(k + 1) % n], lows[k]))
        f.material_index = 1
    o = put(name, bm, mat, sharp=50.0)
    o.data.materials.append(MAT[side])
    return o


def box(name, x0, y0, x1, y1, z0, z1, r, mat):
    """A flat solid with round corners."""
    n = 6
    cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
    hw, hh = (x1 - x0) * 0.5, (y1 - y0) * 0.5
    pts = []
    for qx, qy, a0 in ((hw - r, hh - r, 0.0), (-(hw - r), hh - r, 90.0),
                       (-(hw - r), -(hh - r), 180.0), (hw - r, -(hh - r), 270.0)):
        for i in range(n + 1):
            a = math.radians(a0 + 90.0 * i / n)
            pts.append((cx + qx + r * math.cos(a), cy + qy + r * math.sin(a)))
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in pts]
    hi = [bm.verts.new((x, y, z1)) for x, y in pts]
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    for i in range(len(pts)):
        j = (i + 1) % len(pts)
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = put(name, bm, mat, sharp=35.0)
    b = o.modifiers.new('edge', 'BEVEL')
    b.width = min(0.03, (z1 - z0) * 0.3)
    b.segments = 2
    b.limit_method = 'ANGLE'
    b.angle_limit = math.radians(40.0)
    return o


# ---- how the top sheet lies: a slow wave through it, the swell where the wire
# holds the sheets together, and the corner that will not stay down
WAVES = [(random.uniform(0.0, 6.28), random.uniform(0.0, 3.14), wl, amp)
         for wl, amp in ((23.0, 0.042), (15.0, 0.030), (10.0, 0.018), (6.5, 0.008))]


def wave(x, y):
    z = 0.0
    for ph, ang, wl, amp in WAVES:
        z += amp * math.sin(6.2832 / wl * (x * math.cos(ang) + y * math.sin(ang)) + ph)
    return z


def smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def lift(x, y):
    corner = 0.46 * smooth(1.0 - math.hypot(W - x, y) / 7.5) ** 1.6
    far = 0.06 * smooth(1.0 - math.hypot(W - x, H - y) / 4.0)
    swell = 0.055 * math.exp(-(x / 1.5) ** 2)
    return corner + far + swell


# ---- the desk under it: only its shadow and what it throws back
kit.desk_plane()


# ---- the light: the desk's lamp, up and to the left, as for the calculator, and
# large soft sources for the wire to have something to show
area = kit.area

MID = (W * 0.5, H * 0.5, 1.0)
area('lamp', (W * 0.5 - 30.0, H * 0.5 + 22.0, 46.0), MID, 26.0, 26.0, 4.9, (1.0, 0.95, 0.88))
area('over', (W * 0.5 - 8.0, H * 0.5 + 10.0, 80.0), MID, 60.0, 30.0, 1.75, (1.0, 0.98, 0.95))
area('back', (W * 0.5 + 6.0, H * 0.5 + 62.0, 60.0), MID, 90.0, 26.0, 0.45, (1.0, 0.98, 0.95))
area('strip', (W * 0.5 + 40.0, H * 0.5 + 10.0, 34.0), MID, 8.0, 50.0, 1.4, (0.96, 0.98, 1.0))
area('room', (W * 0.5, H * 0.5 - 60.0, 50.0), MID, 90.0, 40.0, 0.36, (1.0, 0.97, 0.92))

kit.room((0.05, 0.043, 0.036))

# ---- the eye
kit.eye(TILT, PX)
kit.film(SAMPLES, exposure=float(arg('--exposure', 0.95)))
r, c, TMP = kit.r, kit.c, kit.TMP
shoot, save = kit.shoot, kit.save


def frame(left_px, top_px, w_px, h_px, z_ref):
    """The picture w_px by h_px, its top left corner where the sheet's top left is, if
    that is (left_px, top_px) from it, the sheet lying at height z_ref."""
    kit.frame(left_px, top_px, w_px, h_px, (0.0, H, z_ref))


def smaller(img, k):
    """img k times the size, each new pixel the average of what it covers."""
    f = img.astype(np.float64) / 255.0
    f[:, :, :3] *= f[:, :, 3:4]

    def along(a, n):
        pos = np.linspace(0.0, a.shape[0], n + 1)
        cum = np.concatenate((np.zeros((1,) + a.shape[1:]), np.cumsum(a, axis=0)), axis=0)
        i = np.floor(pos).astype(int).clip(0, a.shape[0] - 1)
        t = (pos - i).reshape((-1,) + (1,) * (a.ndim - 1))
        at = cum[i] * (1.0 - t) + cum[i + 1] * t
        return (at[1:] - at[:-1]) * (float(n) / a.shape[0])

    h, w = f.shape[:2]
    f = along(f, max(1, int(h * k)))
    f = along(f.swapaxes(0, 1), max(1, int(w * k))).swapaxes(0, 1)
    al = f[:, :, 3:4]
    f[:, :, :3] = np.divide(f[:, :, :3], al, out=np.zeros_like(f[:, :, :3]), where=al > 1e-4)
    return (f * 255.0 + 0.5).clip(0, 255).astype(np.uint8)


def faded(img, fade):
    """The far edge of the shadow goes: it must not stop in a straight line on
    the desk. Below a whisper of shadow, nothing, so that the picture has an
    edge at all."""
    img = img.copy()
    a = (img[:, :, 3].astype(np.float32) - 4.0).clip(0.0, 251.0) * 255.0 / 251.0
    h, w = a.shape
    rx = (np.minimum(np.arange(w), np.arange(w)[::-1]) / float(fade)).clip(0.0, 1.0)
    ry = (np.minimum(np.arange(h), np.arange(h)[::-1]) / float(fade)).clip(0.0, 1.0)
    img[:, :, 3] = (a * np.outer(ry * ry * (3.0 - 2.0 * ry), rx * rx * (3.0 - 2.0 * rx)) + 0.5).astype(np.uint8)
    return img


def group(fn):
    """What fn makes, and a way to turn it on and off."""
    before = set(bpy.data.objects)
    fn()
    made = [o for o in bpy.data.objects if o not in before]
    for o in made:
        o.hide_render = True

    def show(on):
        for o in made:
            o.hide_render = not on
    return show


# ---- the notebook, without its wire

def make_page():
    box('cover', -0.015, -0.14, W + 0.14, H + 0.11, Z_COVER0, Z_COVER1, 0.42, 'cover')
    rng = random.Random(4)
    for i in range(N_SHEETS - 1, -1, -1):
        depth = float(i)
        wv, lf = 0.86 ** depth, 0.60 ** depth

        def height(x, y, wv=wv, lf=lf):
            return wave(x, y) * wv + lift(x, y) * lf

        if i == 0:
            sheet('sheet 0', 0.0, 0.0, W, H, SHEET_R, ZT, DZ * 0.9, height, 130, 180)
        else:
            dx = rng.gauss(0.022, 0.018)
            dy = rng.gauss(0.0, 0.016)
            top = rng.gauss(0.018, 0.016)
            nx, ny = (70, 100) if i < 6 else (6, 8)
            sheet(f'sheet {i}', 0.0, dy, W + max(dx, -0.005), H + top - dy, SHEET_R,
                  ZT - DZ * depth, DZ * 0.9, height if i < 6 else (lambda x, y: 0.0), nx, ny,
                  side=rng.choice(('edge', 'edge', 'edge2', 'edge3')))


# ---- the wire: one pair of loops for every hole, a long way up and down, so that
# one turn of it in the middle has its neighbours' shadows

def hole_loop(cy):
    hw, hh, cr = HOLE_W * 0.5, HOLE_H * 0.5, 0.045
    pts = []
    for qx, qy, a0 in ((hw - cr, hh - cr, 0.0), (-(hw - cr), hh - cr, 90.0),
                       (-(hw - cr), -(hh - cr), 180.0), (hw - cr, -(hh - cr), 270.0)):
        for i in range(4):
            a = math.radians(a0 + 90.0 * i / 3.0)
            pts.append((HOLE_X + qx + cr * math.cos(a), cy + qy + cr * math.sin(a)))
    return pts


def wire(name, y, zt=ZT):
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = WIRE_R
    cu.bevel_resolution = 5
    cu.resolution_u = 1
    sp = cu.splines.new('POLY')
    R = math.hypot(HOLE_X - RING_CX, zt - RING_CZ)
    n = 72
    sp.points.add(n)
    for i in range(n + 1):
        a = 6.2832 * i / n
        sp.points[i].co = (RING_CX + R * math.cos(a), y, RING_CZ + R * math.sin(a), 1.0)
    sp.use_cyclic_u = False
    sp.use_smooth = True
    o = bpy.data.objects.new(name, cu)
    COLL.objects.link(o)
    cu.materials.append(MAT['steel'])
    return o


def make_rings():
    n = 9
    ymid = H * 0.5
    ys = [ymid + RING_P * i for i in range(-n, n + 1)]
    x1 = (STRIP_PX + 30.0) / PX
    y0, y1 = ys[0] - RING_P * 2.0, ys[-1] + RING_P * 2.0
    # the top of the page, with the holes in it: only what falls on it shows
    bm = bmesh.new()
    corners = [bm.verts.new((x, y, ZT)) for x, y in ((0.0, y0), (x1, y0), (x1, y1), (0.0, y1))]
    edges = [bm.edges.new((corners[i], corners[(i + 1) % 4])) for i in range(4)]
    loops = []
    for y in ys:
        vs = [bm.verts.new((x, yy, ZT)) for x, yy in hole_loop(y)]
        loops.append(vs)
        for i in range(len(vs)):
            edges.append(bm.edges.new((vs[i], vs[(i + 1) % len(vs)])))
    bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=edges)
    # (the fill covers the holes as well: they go)
    for f in list(bm.faces):
        cx = sum(v.co.x for v in f.verts) / len(f.verts)
        cy = sum(v.co.y for v in f.verts) / len(f.verts)
        if abs(cx - HOLE_X) < HOLE_W * 0.5 and any(abs(cy - y) < HOLE_H * 0.5 for y in ys):
            bm.faces.remove(f)
    top = put('page top', bm, 'paper', smooth=False)
    top.is_shadow_catcher = True
    # the holes' walls, the sheets' edges seen from inside
    bm = bmesh.new()
    for y in ys:
        pts = hole_loop(y)
        hi = [bm.verts.new((x, yy, ZT)) for x, yy in pts]
        lo = [bm.verts.new((x, yy, Z_COVER1)) for x, yy in pts]
        for i in range(len(pts)):
            j = (i + 1) % len(pts)
            bm.faces.new((hi[i], hi[j], lo[j], lo[i]))
    put('walls', bm, 'edge', sharp=35.0)
    # what shows through a hole: the dark under the stack
    bm = bmesh.new()
    for x, y in ((-0.02, y0), (x1, y0), (x1, y1), (-0.02, y1)):
        bm.verts.new((x, y, Z_COVER1))
    bm.faces.new(bm.verts)
    put('under', bm, 'pit', smooth=False)
    # the sheets' left edge, for the wire and the desk to see
    bm = bmesh.new()
    for x, y, z in ((0.0, y0, Z_COVER1), (0.0, y1, Z_COVER1), (0.0, y1, ZT), (0.0, y0, ZT)):
        bm.verts.new((x, y, z))
    bm.faces.new(bm.verts)
    put('left edge', bm, 'edge', smooth=False)
    for y in ys:
        wire(f'wire {y:.2f} a', y - STRAND)
        wire(f'wire {y:.2f} b', y + STRAND)
    # the line of perforations, where a page tears out
    slit = RING_P / 8.0
    bm = bmesh.new()
    px0, px1 = PERF_PX / PX - 0.012, PERF_PX / PX + 0.012
    y = y0
    while y < y1:
        vs = [bm.verts.new((x, yy, ZT + 0.002)) for x, yy in
              ((px0, y), (px1, y), (px1, y + slit * 0.55), (px0, y + slit * 0.55))]
        bm.faces.new(vs)
        y += slit
    put('perforations', bm, 'perf', smooth=False)


# ---- the notebook shut, as it lies in the drawer

def make_shut():
    box('shut under', -0.015, -0.11, W + 0.11, H + 0.11, Z_COVER0, Z_COVER1, 0.42, 'cover')
    rng = random.Random(9)
    for i in range(14):
        z = ZT - (ZT - Z_COVER1) * i / 14.0
        dx = rng.gauss(0.0, 0.012)
        sheet(f'shut sheet {i}', 0.0, rng.gauss(0.0, 0.012), W + dx, H, SHEET_R, z,
              (ZT - Z_COVER1) / 14.0 * 0.9, lambda x, y: 0.0, 4, 6)
    box('shut cover', -0.015, -0.11, W + 0.11, H + 0.11, ZT, ZT + 0.13, 0.42, 'cover_top')
    # a paper label, as on an exercise book
    box('shut label', 4.6, H * 0.62, W - 2.2, H * 0.62 + 4.6, ZT + 0.13, ZT + 0.14, 0.16, 'label')
    n = int(H / (RING_P * 1.0)) - 1
    bm = bmesh.new()
    for k in range(n):
        y = RING_P * (k + 1)
        wire(f'shut wire {k} a', y - STRAND, ZT + 0.13)
        wire(f'shut wire {k} b', y + STRAND, ZT + 0.13)
        vs = [bm.verts.new((x, yy, ZT + 0.132)) for x, yy in hole_loop(y)]
        bm.faces.new(vs)
    put('shut holes', bm, 'pit', smooth=False)


# ---- a sticky note, for the note that finds things

def tube(name, rings, mat, sides):
    """A round thing along its length: rings of (along, radius); the two ends closed."""
    bm = bmesh.new()
    loops = []
    for along, rad in rings:
        loops.append([bm.verts.new((along, rad * math.cos(6.2832 * k / sides), rad * math.sin(6.2832 * k / sides)))
                      for k in range(sides)])
    for i in range(len(loops) - 1):
        for k in range(sides):
            bm.faces.new((loops[i][k], loops[i][(k + 1) % sides], loops[i + 1][(k + 1) % sides], loops[i + 1][k]))
    bm.faces.new(loops[0])
    bm.faces.new(list(reversed(loops[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat, sharp=40.0)


def make_top():
    """The notebook shut, with a pencil on it, as it lies in the drawer."""
    make_shut()
    length, rad = 16.0, 0.42
    parts = [
        tube('pencil body', [(1.9, rad), (length - 1.5, rad)], 'paint', 6),
        tube('pencil wood', [(0.25, 0.09), (1.9, rad * 0.98)], 'wood', 18),
        tube('pencil lead', [(0.0, 0.0), (0.25, 0.09)], 'lead', 12),
        tube('pencil ferrule', [(length - 1.5, rad * 1.04), (length - 0.55, rad * 1.04)], 'brass', 18),
        tube('pencil eraser', [(length - 0.55, rad * 0.97), (length - 0.05, rad * 0.90), (length, rad * 0.55)],
             'rubber', 18),
    ]
    for o in parts:
        o.location = (W * 0.70, H * 0.12, ZT + 0.14 + rad)
        o.rotation_euler = (0.0, 0.0, math.radians(112.0))


NOTE_W, NOTE_H = 12.6, 3.6


def make_note():
    def height(x, y):
        # its top stays stuck; its foot lifts away from the page
        t = smooth((1.0 - y / NOTE_H) * 0.95)
        return (0.06 * t * t + 0.16 * smooth(1.0 - math.hypot(NOTE_W - x, y) / 4.2) ** 2
                + 0.018 * math.sin(x * 0.7 + 1.0) * t)

    sheet('note', 0.0, 0.0, NOTE_W, NOTE_H, 0.03, 0.03, 0.02, height, 80, 30, 'note', 'note')


# ---- the paper's tooth, which repeats without showing where

def make_grain():
    n = 512
    rng = np.random.default_rng(23)
    fx = np.fft.fftfreq(n)
    fr = np.hypot(*np.meshgrid(fx, fx))

    def band(lo, hi):
        spec = np.fft.fft2(rng.normal(size=(n, n)))
        mask = np.exp(-((np.log(fr + 1e-5) - np.log((lo + hi) * 0.5)) / (np.log(hi / lo) * 0.6)) ** 2)
        a = np.fft.ifft2(spec * mask).real
        return a / a.std()

    img = 0.0058 * band(0.25, 0.5) + 0.0042 * band(0.08, 0.2) + 0.0030 * band(0.012, 0.04) + 0.0022 * band(0.004, 0.01)
    # the fibres: short, a little bent, some darker and some lighter than the sheet
    rnd = random.Random(31)
    fib = np.zeros((n, n), dtype=np.float32)
    for _ in range(2600):
        x, y = rnd.uniform(0, n), rnd.uniform(0, n)
        a = rnd.uniform(0.0, 6.2832)
        bend = rnd.uniform(-0.05, 0.05)
        length = rnd.randint(8, 34)
        amp = rnd.choice((-1.0, -1.0, 0.6)) * rnd.uniform(0.005, 0.016)
        for _i in range(length):
            xi, yi = int(x) % n, int(y) % n
            fx_, fy_ = x - int(x), y - int(y)
            fib[yi, xi] += amp * (1.0 - fx_) * (1.0 - fy_)
            fib[yi, (xi + 1) % n] += amp * fx_ * (1.0 - fy_)
            fib[(yi + 1) % n, xi] += amp * (1.0 - fx_) * fy_
            fib[(yi + 1) % n, (xi + 1) % n] += amp * fx_ * fy_
            x += math.cos(a)
            y += math.sin(a)
            a += bend
    img = img + fib
    print('notebook.py: grain sd', float(img.std()), 'range', float(img.min()), float(img.max()), flush=True)
    # to multiply by: the sheet's own brightness is kept in nb-page.png
    v = (0.980 + img).clip(0.0, 1.0)
    g = (v * 255.0 + 0.5).astype(np.uint8)
    im = bpy.data.images.new('grain', n, n, alpha=False)
    rgba = np.dstack((g, g, g, np.full_like(g, 255)))
    im.pixels.foreach_set((np.flipud(rgba).astype(np.float32) / 255.0).ravel())
    im.filepath_raw = os.path.join(OUT, 'nb-grain.png')
    im.file_format = 'PNG'
    SCENE.render.image_settings.color_mode = 'BW'
    im.save()
    SCENE.render.image_settings.color_mode = 'RGBA'
    bpy.data.images.remove(im)
    print('notebook.py: wrote nb-grain.png', flush=True)


page = group(make_page)
rings = group(make_rings)
shut = group(make_shut)
top = group(make_top)
note = group(make_note)

if ONLY in ('', 'grain'):
    make_grain()

if ONLY in ('', 'page'):
    page(True)
    frame(MARGIN, MARGIN, SHEET_W_PX + 2 * MARGIN, SHEET_H_PX + 2 * MARGIN, ZT)
    save('nb-page.png', faded(shoot('page', SAMPLES), 18))
    page(False)

if ONLY in ('', 'rings'):
    rings(True)
    # three turns, and the middle one kept: its top and foot are then the same
    w_px = MARGIN + STRIP_PX
    frame(MARGIN, 0, w_px, RING_P_PX * 3, ZT)
    # (and the eye on the middle of the wire, a hole in the middle of the picture)
    t = Vector((0.0, H * 0.5, ZT)) + Vector(((w_px * 0.5 - MARGIN) / PX, 0.0, 0.0))
    kit.aim(t)
    img = shoot('rings', SAMPLES * 2)
    # its right side goes into the sheet: nothing there to end in a line
    ramp = np.ones(w_px, dtype=np.float32)
    ramp[-12:] = np.linspace(1.0, 0.0, 12)
    img[:, :, 3] = (img[:, :, 3] * ramp + 0.5).astype(np.uint8)
    save('nb-rings.png', img[RING_P_PX:RING_P_PX * 2])
    rings(False)

if ONLY in ('', 'note'):
    note(True)
    pad = 14
    w_px, h_px = int(NOTE_W * PX) + pad * 2, int(NOTE_H * COS * PX) + pad * 2
    r.resolution_x, r.resolution_y = w_px, h_px
    kit.eye_data().ortho_scale = max(w_px, h_px) / PX
    t = Vector((NOTE_W * 0.5, NOTE_H * 0.5, 0.03))
    kit.aim(t)
    save('nb-note.png', faded(shoot('note', SAMPLES), 8))
    note(False)

if ONLY in ('', 'shut'):
    shut(True)
    frame(MARGIN, MARGIN, SHEET_W_PX + 2 * MARGIN, SHEET_H_PX + 2 * MARGIN, ZT + 0.13)
    save('nb-shut.png', faded(shoot('shut', SAMPLES), 12))
    shut(False)

if ONLY in ('', 'top'):
    top(True)
    frame(MARGIN, MARGIN + 40, SHEET_W_PX + 2 * MARGIN, SHEET_H_PX + 2 * MARGIN + 80, ZT + 0.13)
    img = faded(shoot('top', SAMPLES), 18)
    save('nb-top.png', smaller(img, 0.5))
    # the mark: as much of a square as it fills
    sq = smaller(img, 128.0 / max(img.shape[:2]))
    h, w = sq.shape[:2]
    mark = np.zeros((128, 128, 4), dtype=np.uint8)
    mark[(128 - h) // 2:(128 - h) // 2 + h, (128 - w) // 2:(128 - w) // 2 + w] = sq
    save('desk-notepad-icon.png', mark)
    top(False)

print('notebook.py: done', flush=True)
