"""The keyboard on the desk, made in Blender.

For a desk with no keyboard on it: a typewriter, seen straight from above like all that is on the
desk. It is a heavy thing in black enamel, with its keys round, in nickel rings, standing over the
dark of the machine where the levers run back; and behind them the fan of the type bars, the ribbon
on its two spools, the platen and its knobs. It is not one picture. What keys there are, and what is
printed on them, depends on the language of the keyboard, so these are the machine with nothing in
its well, and a key of each kind, up and held down, two keys wide so that a key of any width can be
cut from it: DEsk lays them out and prints them itself (src/keyboardart.c).

    blender -b --factory-startup -P art/keyboard/keyboard.py -- OUTDIR [--quick]

Through one eye, near enough that what is further off is smaller and goes in towards the middle: the
walls of the well show, and the levers draw together down in it. A row goes across at one size, and
DEsk has that size for each row from keyboard-map.h. A key is rendered in the middle of the rows,
with the shadow it throws on what is under it, and scaled to the row it goes in. The sizes are in
centimetres; x to the right, y away from the eye, z up, the desk at z = 0.
"""

import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

import kit
from kit import MAT, QUICK, SCENE, arg, loft, put, rrect

SAMPLES = int(arg('--samples', 16 if QUICK else 192))
KEYS_ONLY = '--keys-only' in kit.ARGV   # (to look at a key: nothing else is rendered, and then it stops)

# ---- the measures
U = 2.08                      # from a key to the next, across
V = 1.80                      # and from a row to the next, away
CLIMB = 0.10                  # and how much higher that row is
COLS, ROWS = 15, 5            # keys wide; full rows
SLIM = 0.66                   # the top row, of a row
D = 1.60                      # a key, across
RING = 0.135                  # a key's ring, how wide it looks from above
RING_DOWN = 0.26              # how far its side comes down
PRESS = 0.50                  # how far a key goes down
ABOVE_TOP = 0.22              # how far the rings stand over the case's top, beside them
WELL_DEEP = 1.55              # the floor of the well, under the case's top
KEY_OVER = 0.72               # what a key throws its shadow on is this far under its ring
MARGIN = 0.42                 # between the keys and the wall of the well
RIM = 0.95                    # the case round the well, at the sides
FRONT = 0.92                  # and in front of it
Z_FRONT = 2.15                # the case's top at its front edge
BRIDGE = 0.62                 # the bar across behind the keys
BASKET = 1.42                 # the opening for the type bars behind that
REAR = 1.95                   # the case behind that: the platen stands on it
R_OUT = 1.5                   # the case's corners
BEVEL = 0.85                  # and how much its edges are rounded
PLATEN_R = 0.72               # the platen
KNOB_L = 1.35
TILT = 0.0                    # the eye: straight above
DISTANCE = 42.0               # and how far from the middle of the machine
PX = 132.0                    # pixels to the centimetre, at the row the keys are rendered in
PAD = 150                     # pixels round the machine, for its shadow on the desk
PAD_RIGHT = 320               # (it falls this way further)
TILE_MARGIN = 62              # pixels of a tile past what a key has of it, for its shadow
TILE_TOP = 30                 # pixels of a tile above its row
TILE_BELOW = 70               # and below

KINDS = {
    # the top of a key: what it is, how much of a sheen
    'letter': dict(base=(0.0045, 0.0045, 0.0055), rough=0.06, coat=0.7, spec=0.32),
    'function': dict(base=(0.024, 0.023, 0.023), rough=0.22, coat=0.5, spec=0.5),
    'return': dict(base=(0.20, 0.018, 0.012), rough=0.08, coat=1.0, spec=0.5),
}


def principled(name, base, **more):
    return kit.principled(name, base, **more)


MAT.update({
    'enamel': kit.grain(principled('enamel', (0.0105, 0.0105, 0.0115), rough=0.17, coat=1.0, coat_rough=0.05),
                        60.0, 0.0024, 0.35, detail=3.0, roughness=0.5),
    'floor': kit.grain(principled('floor', (0.062, 0.058, 0.052), rough=0.78), 90.0, 0.012, 0.6),
    'inner': principled('inner', (0.030, 0.029, 0.028), rough=0.55),
    'lever': principled('lever', (0.26, 0.255, 0.24), rough=0.46, metallic=1.0),
    'stem': principled('stem', (0.62, 0.61, 0.58), rough=0.30, metallic=1.0),
    'bar': principled('bar', (0.70, 0.69, 0.66), rough=0.26, metallic=1.0),
    'slug': principled('slug', (0.45, 0.44, 0.42), rough=0.32, metallic=1.0),
    'nickel': principled('nickel', (0.88, 0.85, 0.79), rough=0.12, metallic=1.0),
    'satin': principled('satin', (0.92, 0.90, 0.85), rough=0.46, metallic=0.95),
    'gold': principled('gold', (0.92, 0.66, 0.24), rough=0.34, metallic=0.55),
    'rubber': kit.grain(principled('rubber', (0.016, 0.016, 0.016), rough=0.36), 300.0, 0.002, 0.3),
    'knob': principled('knob', (0.010, 0.010, 0.010), rough=0.20, coat=0.6, coat_rough=0.12),
    'ribbon black': principled('ribbon black', (0.012, 0.012, 0.014), rough=0.62),
    'ribbon red': principled('ribbon red', (0.26, 0.018, 0.016), rough=0.58),
    'lens': principled('lens', (0.14, 0.012, 0.008), rough=0.08, coat=1.0, coat_rough=0.02),
})
for kind, top in KINDS.items():
    MAT['top ' + kind] = principled('top ' + kind, top['base'], rough=top['rough'], coat=top['coat'],
                                    coat_rough=0.02, spec=top['spec'])


# ---- where things are
W_FIELD = COLS * U
# (from the front: the keys, the bar, the type bars, the back of the case)
Y_FRONT = -9.4                       # the case's front edge
Y_WELL0 = Y_FRONT + FRONT            # the front of the well
RY = [0.0] * 6                       # the middle of each row: 0 the slim one at the back, 5 at the front
RY[5] = Y_WELL0 + MARGIN + V * 0.5
for r_ in range(4, 0, -1):
    RY[r_] = RY[r_ + 1] + V
RY[0] = RY[1] + V * 0.5 + V * SLIM * 0.5 + 0.06
Y_WELL1 = RY[0] + V * SLIM * 0.5 + MARGIN * 0.6
Y_BRIDGE1 = Y_WELL1 + BRIDGE
Y_BASKET1 = Y_BRIDGE1 + BASKET
Y_BACK = Y_BASKET1 + REAR
W_WELL = W_FIELD + MARGIN * 2.0
W_CASE = W_WELL + RIM * 2.0
SLOPE = CLIMB / V


def z_top(y):
    """The top of the case, climbing to the back."""
    return Z_FRONT + (y - Y_FRONT) * SLOPE


RZ = [z_top(y) + ABOVE_TOP for y in RY]
Y_REF, Z_REF = RY[3], RZ[3]          # the row the keys are rendered in


# ---- shapes

def capsule(hw, hh, inset=0.0):
    """A key's outline: round at its ends, however wide."""
    h = max(hh - inset, 1e-3)
    straight = max(hw - hh, 0.0)
    pts = []
    for side, a0 in ((1.0, -90.0), (-1.0, 90.0)):
        for i in range(37):
            a = math.radians(a0 + 180.0 * i / 36)
            pts.append((side * straight + h * math.cos(a), h * math.sin(a)))
    return pts


def key_parts(name, hw, hh, kind, ring=RING, dome=0.11):
    """A key, its ring's top at z = 0: a stamped nickel ring, rounded over all its width, with a good
    deep side, and the disc let into it, domed high under its sheen."""
    prof = [(0.0, -RING_DOWN), (0.0, -0.09)]
    for i in range(1, 13):
        a = math.radians(180.0 * i / 12)
        prof.append((ring * 0.5 * (1.0 - math.cos(a)), -0.056 + 0.056 * math.sin(a)))
    rim = loft(name + ' ring', [[(x, y, z) for x, y in capsule(hw, hh, i)] for i, z in prof], 'nickel',
               cap_first=True)
    lip = capsule(hw, hh, ring)
    ex = max(p[0] for p in lip)
    ey = max(p[1] for p in lip)
    rings = [[(x, y, -0.070) for x, y in lip]]
    steps = 26

    def over(x, y):
        # a dome across its height, and flat along what it is drawn out by
        straight = ex - ey
        dx = max(abs(x) - straight, 0.0)
        return 1.0 - min((dx * dx + y * y) / (ey * ey), 1.0)
    for s_ in range(1, steps + 1):
        t = 1.0 - s_ / (steps + 1)
        ring_pts = []
        for x, y in capsule(hw, hh, ring + (ey * (1.0 - t))):
            ring_pts.append((x, y, -0.070 + dome * over(x, y)))
        rings.append(ring_pts)
    top = loft(name + ' top', rings, 'top ' + kind, cap_first=False, cap_last=True)
    return [rim, top]


def bar_parts(name, hw, hh):
    """The space bar: a long nickel bar, round over."""
    rings = [[(x, y, -0.34) for x, y in capsule(hw, hh)]]
    for s in range(0, 13):
        a = math.radians(90.0 * s / 12)
        i = hh * 0.86 * (1.0 - math.cos(a))
        rings.append([(x, y, -0.30 + 0.30 * math.sin(a)) for x, y in capsule(hw, hh, i)])
    return [loft(name, rings, 'nickel', cap_last=True)]


def flat(name, hw, hh, r, z, mat, cx=0.0, cy=0.0, slope=0.0):
    """Something flat with round corners, lying at z, going up with the case's top."""
    bm = bmesh.new()
    vs = [bm.verts.new((x, y, z + (y - cy) * slope)) for x, y in rrect(hw, hh, r, 12, cx, cy)]
    bm.faces.new(vs)
    return put(name, bm, mat, smooth=False)


def line_round(name, hw, hh, r, width, mat, cx=0.0, cy=0.0):
    """A line painted round something, on the top of the case."""
    a = rrect(hw, hh, r, 14, cx, cy)
    b = rrect(hw + width, hh + width, r + width, 14, cx, cy)
    bm = bmesh.new()
    va = [bm.verts.new((x, y, z_top(y) + 0.003)) for x, y in a]
    vb = [bm.verts.new((x, y, z_top(y) + 0.003)) for x, y in b]
    n = len(va)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((va[i], va[j], vb[j], vb[i]))
    return put(name, bm, mat, smooth=False)


def turned(name, prof, mat, cy, cz, n=48, flutes=0, flute=0.0):
    """Turned about a line along x: prof is (x, radius) from one end to the other."""
    rings = []
    for x, rad in prof:
        ring = []
        for i in range(n):
            a = 2.0 * math.pi * i / n
            q = rad + (flute * math.cos(a * flutes) if flutes and rad > flute * 3.0 else 0.0)
            ring.append((x, cy + q * math.cos(a), cz + q * math.sin(a)))
        rings.append(ring)
    return loft(name, rings, mat, cap_last=True)


def spun(name, prof, mat, cx, cy, cz, n=48):
    """Turned about a line straight up: prof is (radius, height), from the bottom."""
    rings = [[(cx + r * math.cos(2.0 * math.pi * i / n), cy + r * math.sin(2.0 * math.pi * i / n), cz + h)
              for i in range(n)] for r, h in prof]
    return loft(name, rings, mat, cap_last=True)


def tube(name, pts, rad, mat, flatten=1.0, n=14):
    """A rod along a line of points."""
    rings = []
    for i, p in enumerate(pts):
        a = Vector(pts[max(i - 1, 0)])
        b = Vector(pts[min(i + 1, len(pts) - 1)])
        t = (b - a).normalized()
        side = t.cross(Vector((0.0, 0.0, 1.0)))
        if side.length < 1e-3:
            side = Vector((1.0, 0.0, 0.0))
        side.normalize()
        up = side.cross(t).normalized()
        ring = []
        for k in range(n):
            q = 2.0 * math.pi * k / n
            ring.append(tuple(Vector(p) + side * (rad * math.cos(q)) + up * (rad * flatten * math.sin(q))))
        rings.append(ring)
    return loft(name, rings, mat, cap_last=True)


def block(name, pts_lo, height, mat):
    """A solid on an outline."""
    rings = [[(x, y, z) for x, y, z in pts_lo], [(x, y, z + height) for x, y, z in pts_lo]]
    return loft(name, rings, mat, cap_last=True, sharp=30.0)


def ink_only(name, ink, image, side, metallic=0.0, rough=0.45):
    """A print with nothing but its ink: the case shows through round it."""
    m = principled(name, ink, rough=rough, metallic=metallic)
    m.blend_method = 'CLIP'
    nt = m.node_tree
    n, ln = nt.nodes, nt.links
    out = n['Material Output']
    tc = n.new('ShaderNodeTexCoord')
    mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (1.0 / side, 1.0 / side, 1.0)
    mp.inputs['Location'].default_value = (0.5, 0.5, 0.0)
    ln.new(tc.outputs['Object'], mp.inputs['Vector'])
    tex = n.new('ShaderNodeTexImage')
    tex.image = image
    tex.extension = 'CLIP'
    ln.new(mp.outputs[0], tex.inputs['Vector'])
    mix = n.new('ShaderNodeMixShader')
    clear = n.new('ShaderNodeBsdfTransparent')
    ln.new(tex.outputs['Alpha'], mix.inputs[0])
    ln.new(clear.outputs[0], mix.inputs[1])
    ln.new(n['Principled BSDF'].outputs[0], mix.inputs[2])
    ln.new(mix.outputs[0], out.inputs[0])
    return m


# ---- the machine
desk = kit.desk_plane()
CX_OPEN = 0.0
CY_OPEN = (Y_WELL0 + Y_BASKET1) * 0.5
HH_OPEN = (Y_BASKET1 - Y_WELL0) * 0.5


def make_case():
    parts = []
    cy_case = (Y_FRONT + Y_BACK) * 0.5
    hh_case = (Y_BACK - Y_FRONT) * 0.5
    hw_case = W_CASE * 0.5
    # the shell: its sides straight down to the desk, its edges rounded well over, its top going up
    # towards the back; and the opening let into that, with its own walls going down into the dark
    rings = [[(x, y, 0.04) for x, y in rrect(hw_case, hh_case, R_OUT, 24, 0.0, cy_case)]]
    rings.append([(x, y, 0.5) for x, y in rrect(hw_case, hh_case, R_OUT, 24, 0.0, cy_case)])
    rings.append([(x, y, z_top(y) - BEVEL) for x, y in rrect(hw_case, hh_case, R_OUT, 24, 0.0, cy_case)])
    for s in range(1, 17):
        a = math.radians(90.0 * s / 16)
        d = BEVEL * (1.0 - math.cos(a))
        rings.append([(x, y, z_top(y) - BEVEL + BEVEL * math.sin(a))
                      for x, y in rrect(hw_case - d, hh_case - d, max(R_OUT - d, 0.2), 24, 0.0, cy_case)])
    open_r = 0.46
    lip = 0.10
    for s in range(0, 9):
        a = math.radians(90.0 * s / 8)
        d = lip * (1.0 - math.sin(a))
        z = -lip + lip * math.cos(a)
        rings.append([(x, y, z_top(y) + z)
                      for x, y in rrect(W_WELL * 0.5 + d, HH_OPEN + d, open_r + d, 24, 0.0, CY_OPEN)])
    for dz in (WELL_DEEP * 0.5, WELL_DEEP):
        rings.append([(x, y, z_top(y) - dz) for x, y in rrect(W_WELL * 0.5, HH_OPEN, open_r, 24, 0.0, CY_OPEN)])
    o = loft('case', rings, 'enamel', cap_last=True, sharp=75.0)
    parts.append(o)

    # the floor of the well, and the levers running back over it into the dark, a level or two of
    # them, as they do, each going to its own key
    bm = bmesh.new()
    vs = [bm.verts.new((x, y, z_top(y) - WELL_DEEP + 0.01)) for x, y in rrect(W_WELL * 0.5, HH_OPEN, open_r, 18, 0.0, CY_OPEN)]
    bm.faces.new(vs)
    parts.append(put('floor', bm, 'floor', smooth=False))
    n = 48
    for i in range(n + 1):
        x = -W_FIELD * 0.5 + W_FIELD * i / n
        lift = 0.20 if i % 2 else 0.42
        y0 = Y_WELL0 + 0.08
        y1 = Y_WELL1 + 0.9
        parts.append(tube(f'lever {i}', [(x, y0, z_top(y0) - WELL_DEEP + lift), (x, y1, z_top(y1) - WELL_DEEP + lift + 0.15)],
                          0.052, 'lever', 1.5, 8))
    # the comb that keeps them apart, across the front of the well
    for k, yy in enumerate((Y_WELL0 + 1.6, RY[2], RY[1] + 0.6)):
        z = z_top(yy) - WELL_DEEP + 0.62
        parts.append(turned(f'cross rod {k}', [(-W_WELL * 0.5, 0.075), (W_WELL * 0.5, 0.075)], 'lever', yy, z, 10))

    # the gold line round all of that, and its name, in gold, on the front
    parts.append(line_round('line', W_WELL * 0.5 + 0.26, HH_OPEN + 0.26, open_r + 0.26, 0.026, 'gold', 0.0, CY_OPEN))
    im = kit.legend_image('DEsk', ("<svg xmlns='http://www.w3.org/2000/svg' width='512' height='512'>"
                                   "<text x='256' y='292' font-family='Nimbus Roman' font-weight='700' "
                                   "font-size='112' text-anchor='middle' fill='#fff' "
                                   "letter-spacing='6'>DEsk</text></svg>"))
    m = ink_only('name', (0.95, 0.70, 0.27), im, 3.0, metallic=0.45, rough=0.32)
    y_name = Y_WELL0 - 0.30
    d = kit.decal('name', 0.0, y_name, 1.5, 1.5, z_top(y_name) + 0.004, m)
    d.rotation_euler = (math.atan(SLOPE), 0.0, 0.0)
    parts.append(d)
    return parts


case_parts = make_case()


def make_inside():
    """Behind the keys: the bar across, the fan of the type bars behind it, the ribbon."""
    parts = []
    # the bar across, with the small jewel of a lamp in it
    cy = (Y_WELL1 + Y_BRIDGE1) * 0.5
    hh = BRIDGE * 0.5
    z_b = z_top(cy)
    rings = [[(x, y, z_top(y) - 1.2) for x, y in rrect(W_WELL * 0.5 + 0.02, hh, 0.06, 6, 0.0, cy)]]
    rings.append([(x, y, z_top(y) - 0.16) for x, y in rrect(W_WELL * 0.5 + 0.02, hh, 0.06, 6, 0.0, cy)])
    for s in range(1, 7):
        a = math.radians(90.0 * s / 6)
        d = 0.16 * (1.0 - math.cos(a))
        rings.append([(x, y, z_top(y) - 0.16 + 0.16 * math.sin(a))
                      for x, y in rrect(W_WELL * 0.5 + 0.02 - d, hh - d, max(0.06 - d, 0.01), 6, 0.0, cy)])
    parts.append(loft('bridge', rings, 'enamel', cap_last=True, sharp=70.0))
    for sign in (-1.0, 1.0):
        parts.append(flat(f'stroke {sign}', 6.2, 0.012, 0.01, z_b + 0.004, 'gold', sign * 8.0, cy, SLOPE))
    parts.append(flat('jewel seat', 0.13, 0.13, 0.125, z_b + 0.004, 'nickel', W_CASE * 0.5 - 3.0, cy, SLOPE))
    parts.append(flat('jewel', 0.095, 0.095, 0.09, z_b + 0.008, 'lens', W_CASE * 0.5 - 3.0, cy, SLOPE))
    y_jewel = cy
    # the type bars: from a curved segment under the printing point out to their types, lying back
    # on a felt, in a fan a good deal wider than it is deep, as from here it looks
    yc = Y_BASKET1 + 0.15
    zc = z_top(yc) - 0.55
    half_w, deep = W_WELL * 0.5 - 0.5, BASKET + 0.02
    floor_z = z_top(Y_BRIDGE1) - 1.45
    parts.append(flat('basket floor', W_WELL * 0.5 + 0.02, BASKET * 0.5 + 0.1, 0.1, floor_z - 0.12, 'floor', 0.0,
                      (Y_BRIDGE1 + Y_BASKET1) * 0.5, SLOPE))
    count = 43
    for i in range(count):
        t = (i + 0.5) / count
        ang = math.radians(-82.0 + 164.0 * t)
        # outer end: the type; inner end: at the segment
        ox, oy = math.sin(ang) * half_w, yc - math.cos(ang) * deep
        ix, iy = math.sin(ang) * 2.6, yc - math.cos(ang) * 0.42
        oz = floor_z + 0.18 + 0.05 * math.cos(ang * 2.0)
        iz = zc - 0.26
        parts.append(tube(f'type bar {i}', [(ox, oy, oz), ((ox + ix) * 0.5, (oy + iy) * 0.5, (oz + iz) * 0.5 + 0.08),
                                            (ix, iy, iz)], 0.052, 'bar', 0.55, 8))
        # its type, at the end
        dirx, diry = math.sin(ang), -math.cos(ang) * deep / half_w
        ln = math.hypot(dirx, diry)
        dirx, diry = dirx / ln, diry / ln
        parts.append(tube(f'type {i}', [(ox - dirx * 0.14, oy - diry * 0.14, oz), (ox + dirx * 0.24, oy + diry * 0.24, oz + 0.02)],
                          0.095, 'slug', 0.8, 8))
    # the segment: a curved plate over their inner ends
    pts = []
    for i in range(41):
        ang = math.radians(-96.0 + 192.0 * i / 40)
        pts.append((math.sin(ang) * 2.75, yc - math.cos(ang) * 0.52, zc - 0.20))
    parts.append(tube('segment', pts, 0.15, 'nickel', 0.8, 10))
    # the ribbon, off one spool and through the guide to the other, black over red
    y_rib = Y_BASKET1 - 0.28
    z_rib = zc + 0.34
    for sign in (-1.0, 1.0):
        x = sign * (W_WELL * 0.5 - 2.25)
        z0 = z_top(y_rib) - 0.52
        # a reel: its flange, the ribbon wound on it, a nickel cap with its slots
        parts.append(spun(f'spool {sign}', [(0.0, 0.0), (1.30, 0.0), (1.32, 0.03), (1.32, 0.06), (1.28, 0.08), (0.0, 0.08)],
                          'knob', x, y_rib - 0.05, z0, 56))
        parts.append(spun(f'spool ribbon {sign}', [(0.0, 0.08), (1.12, 0.08), (1.14, 0.12), (1.14, 0.30), (1.10, 0.33), (0.0, 0.33)],
                          'ribbon black', x, y_rib - 0.05, z0, 56))
        parts.append(spun(f'spool cap {sign}', [(0.0, 0.33), (0.36, 0.33), (0.38, 0.35), (0.36, 0.38), (0.15, 0.39), (0.12, 0.44),
                                               (0.0, 0.45)], 'satin', x, y_rib - 0.05, z0, 40))
    for side, mat in ((-0.11, 'ribbon black'), (0.11, 'ribbon red')):
        bm = bmesh.new()
        xs = W_WELL * 0.5 - 2.25 - 1.15
        hw = 0.11
        y = y_rib + side * 0.25
        z = z_rib + side
        vs = [bm.verts.new(p) for p in ((-xs, y - 0.03, z - hw), (xs, y - 0.03, z - hw), (xs, y + 0.03, z + hw), (-xs, y + 0.03, z + hw))]
        bm.faces.new(vs)
        parts.append(put(f'ribbon {side}', bm, mat, smooth=False))
    # the guide it goes through, at the printing point
    parts.append(tube('ribbon guide', [(-0.46, y_rib - 0.30, z_rib - 0.34), (-0.42, y_rib - 0.08, z_rib + 0.20),
                                       (0.0, y_rib - 0.08, z_rib + 0.27), (0.42, y_rib - 0.08, z_rib + 0.20),
                                       (0.46, y_rib - 0.30, z_rib - 0.34)], 0.042, 'nickel'))
    return parts, y_jewel


inside_parts, Y_JEWEL = make_inside()

Y_PLATEN = Y_BASKET1 + 0.95
Z_PLATEN = z_top(Y_PLATEN) + 1.02
PLATEN_L = W_CASE * 0.5 - 3.1        # the roller's half length


def make_platen():
    """On the back of the machine: the carriage, with the platen in it."""
    parts = []
    over = []
    # what it runs on: two rails across the back of the case, on blocks
    z_deck = z_top(Y_PLATEN)
    for k, (dy, dz) in enumerate(((-0.72, 0.13), (0.72, 0.13))):
        y = Y_PLATEN + dy
        parts.append(turned(f'rail {k}', [(-W_CASE * 0.5 + 1.0, 0.10), (W_CASE * 0.5 - 1.0, 0.10)], 'nickel', y,
                            z_top(y) + dz, 16))
    # the carriage's two ends, standing on the rails
    for sign in (-1.0, 1.0):
        x = sign * (PLATEN_L + 0.18)
        pts = [(x - 0.13, Y_PLATEN - 0.9, z_deck + 0.1), (x + 0.13, Y_PLATEN - 0.9, z_deck + 0.1),
               (x + 0.13, Y_PLATEN + 0.9, z_deck + 0.1), (x - 0.13, Y_PLATEN + 0.9, z_deck + 0.1)]
        parts.append(block(f'carriage end {sign}', pts, Z_PLATEN - z_deck + 0.20, 'enamel'))
    # the platen, black rubber
    parts.append(turned('platen', [(-PLATEN_L, PLATEN_R - 0.08), (-PLATEN_L + 0.06, PLATEN_R), (PLATEN_L - 0.06, PLATEN_R),
                                   (PLATEN_L, PLATEN_R - 0.08)], 'rubber', Y_PLATEN, Z_PLATEN, 72))
    # its axle, and a knob on each end
    for sign in (-1.0, 1.0):
        x0 = sign * PLATEN_L
        parts.append(turned(f'axle {sign}', [(x0, 0.15), (x0 + sign * 0.62, 0.15)] if sign > 0
                            else [(x0 + sign * 0.62, 0.15), (x0, 0.15)], 'nickel', Y_PLATEN, Z_PLATEN, 16))
        x1 = x0 + sign * 0.48
        prof = [(x1, 0.30), (x1 + sign * 0.05, 0.76), (x1 + sign * 0.14, 0.84), (x1 + sign * KNOB_L * 0.84, 0.84),
                (x1 + sign * KNOB_L * 0.94, 0.76), (x1 + sign * KNOB_L, 0.34)]
        if sign < 0:
            prof = list(reversed(prof))
        parts.append(turned(f'knob {sign}', prof, 'knob', Y_PLATEN, Z_PLATEN, 112, flutes=30, flute=0.02))
        cap = x1 + sign * KNOB_L
        parts.append(turned(f'knob cap {sign}', [(cap, 0.34), (cap + sign * 0.03, 0.26), (cap + sign * 0.04, 0.0)] if sign > 0
                            else [(cap + sign * 0.04, 0.0), (cap + sign * 0.03, 0.26), (cap, 0.34)], 'nickel', Y_PLATEN,
                            Z_PLATEN, 32))
    # the bail that holds the paper to the platen, with its small rollers: over the paper
    bail_y = Y_PLATEN - 0.62
    bail_z = Z_PLATEN + 0.46
    over.append(turned('bail', [(-PLATEN_L + 0.2, 0.06), (PLATEN_L - 0.2, 0.06)], 'nickel', bail_y, bail_z, 12))
    for x in (-8.4, -2.8, 2.8, 8.4):
        over.append(turned(f'bail roller {x}', [(x - 0.42, 0.085), (x - 0.40, 0.125), (x + 0.40, 0.125), (x + 0.42, 0.085)],
                           'rubber', bail_y, bail_z, 16))
    for sign in (-1.0, 1.0):
        over.append(tube(f'bail arm {sign}', [(sign * (PLATEN_L - 0.2), bail_y, bail_z),
                                              (sign * (PLATEN_L + 0.08), Y_PLATEN - 0.38, Z_PLATEN + 0.07),
                                              (sign * (PLATEN_L + 0.12), Y_PLATEN - 0.05, Z_PLATEN)], 0.04, 'nickel'))
    # the plate the type strikes through: a nickel strip with its scale, close against the platen
    plate_y = Y_PLATEN - PLATEN_R - 0.02
    plate_z = Z_PLATEN - 0.04
    hw, hh = 5.6, 0.10
    bm = bmesh.new()
    vs = [bm.verts.new((x, plate_y + y * 0.3, plate_z + y)) for x, y in rrect(hw, hh, 0.05, 6)]
    bm.faces.new(vs)
    over.append(put('scale', bm, 'satin', smooth=False))
    for i in range(-42, 43):
        tall = 0.09 if i % 10 == 0 else 0.05
        bm = bmesh.new()
        x = i * 0.127
        vs = [bm.verts.new((x + dx, plate_y + (hh * 0.2) * 0.3 - 0.006, plate_z + hh * 0.7 + dz))
              for dx, dz in ((-0.008, -tall), (0.008, -tall), (0.008, 0.0), (-0.008, 0.0))]
        bm.faces.new(vs)
        over.append(put(f'tick {i}', bm, 'floor', smooth=False))
    # the lever that throws the carriage back, up from the left knob
    x = -PLATEN_L - 0.2
    y = Y_PLATEN - 0.15
    lev = [(x, y, Z_PLATEN + 0.3), (x + 0.12, y + 0.05, Z_PLATEN + 0.95), (x + 0.50, y + 0.26, Z_PLATEN + 1.35),
           (x + 1.30, y + 0.90, Z_PLATEN + 1.62), (x + 2.30, y + 1.65, Z_PLATEN + 1.75)]
    parts.append(tube('lever arm', lev, 0.085, 'nickel', 0.7))
    tip = Vector(lev[-1])
    parts.append(tube('lever paddle', [tuple(tip + Vector((-0.08, -0.06, 0.0))), tuple(tip + Vector((0.30, 0.24, 0.02))),
                                       tuple(tip + Vector((0.76, 0.58, 0.03)))], 0.20, 'nickel', 0.42))
    return parts, over, (bail_y, bail_z, plate_y, plate_z + hh)


platen_parts, over_paper, (BAIL_Y, BAIL_Z, PLATE_Y, PLATE_Z) = make_platen()
machine = case_parts + inside_parts + platen_parts

# ---- the light: a lamp up to the left, with an edge to what it throws, as everywhere on the desk; a
# soft fill; and what is only seen in what shines
area = kit.area
MID = (0.0, -1.0, 2.0)
lamp = area('lamp', (-24.0, 22.0, 46.0), MID, 15.0, 15.0, 22.0, (1.0, 0.92, 0.80))
lamp.data.shape = 'DISK'
# (a big soft light, that does not show as itself in anything polished)
area('over', (6.0, 4.0, 70.0), MID, 44.0, 22.0, 1.15, (1.0, 0.98, 0.95)).visible_glossy = False
kit.room((0.060, 0.052, 0.044))

kit.film(SAMPLES, exposure=0.55, filter_size=None, bounces=10, glossy=6, transmission=4)
SCENE.view_settings.view_transform = 'AgX'
for look_name in ('AgX - Punchy', 'Punchy'):
    try:
        SCENE.view_settings.look = look_name
        break
    except TypeError:
        pass

# the eye
cam_data = bpy.data.cameras.new('eye')
cam_data.type = 'PERSP'
cam_data.clip_start = 1.0
cam_data.clip_end = 400.0
cam = bpy.data.objects.new('eye', cam_data)
kit.COLL.objects.link(cam)
SCENE.camera = cam
AIM = Vector((0.0, (Y_FRONT + Y_BACK) * 0.5, (z_top(Y_FRONT) + z_top(Y_BACK)) * 0.5))
BACKWARD = Vector((0.0, -math.sin(TILT), math.cos(TILT)))
cam.location = AIM + BACKWARD * DISTANCE
cam.rotation_euler = Matrix(((1.0, 0.0, 0.0), (0.0, math.cos(TILT), -math.sin(TILT)),
                             (0.0, math.sin(TILT), math.cos(TILT)))).to_euler()
bpy.context.view_layer.update()


def depth_of(p):
    return (cam.location - Vector(p)).dot(BACKWARD)


# one whole picture, its scale set so that the row the keys are rendered in has PX to the centimetre
DEPTH_REF = depth_of((0.0, Y_REF, Z_REF))
kit.r.resolution_percentage = 100


def frame_all():
    """The size of the whole picture and of the eye's film, from what must be in it."""
    corners = []
    for x in (-W_CASE * 0.5 - KNOB_L, W_CASE * 0.5 + KNOB_L):
        for y in (Y_FRONT, Y_BACK + 1.8):
            for z in (0.0, z_top(y) + 3.2):
                corners.append((x, y, z))
    up = Vector((0.0, math.cos(TILT), math.sin(TILT)))
    xs, ys = [], []
    for p in corners:
        d = depth_of(p)
        rel = Vector(p) - cam.location
        k = PX * DEPTH_REF / d
        xs.append(rel.x * k)
        ys.append(rel.dot(up) * k)
    half_w = max(abs(v) for v in xs) + PAD_RIGHT
    y_top, y_bottom = max(ys) + PAD, min(ys) - PAD
    return half_w, y_top, y_bottom, up


HALF_W, Y_TOP, Y_BOTTOM, UP = frame_all()
# (the middle of the picture on the eye's axis, up and down too: shifted, not turned)
w_px = int(math.ceil(HALF_W)) * 2
h_px = int(math.ceil(Y_TOP - Y_BOTTOM))
kit.r.resolution_x, kit.r.resolution_y = w_px, h_px
cam_data.sensor_fit = 'HORIZONTAL'
cam_data.sensor_width = 36.0
cam_data.lens = 36.0 * (PX * DEPTH_REF) / w_px
cam_data.shift_x = 0.0
cam_data.shift_y = ((Y_TOP + Y_BOTTOM) * 0.5) / w_px
bpy.context.view_layer.update()


def to_px(p):
    """Where a point comes in the whole picture."""
    d = depth_of(p)
    rel = Vector(p) - cam.location
    k = PX * DEPTH_REF / d
    return (w_px * 0.5 + rel.x * k, Y_TOP - rel.dot(UP) * k)


def scale_at(p):
    return PX * DEPTH_REF / depth_of(p)


def polish(name, sees, strength=1.0, middle=0.24, glow=0.0):
    """A wide sheet of light high over it, that only what shines can see, dark in the middle: what is
    flat, a dome too, looks straight up into the dark, and what is rounded over, a ring's roll, an
    edge, has the rest in it, brighter to the left, where the lamp is."""
    em = kit.seen_only_in_polish(name, -300.0, 300.0, 100.0, (1.0, 0.97, 0.92), sees)
    nt = em.id_data
    nd, ln = nt.nodes, nt.links
    tc = nd.new('ShaderNodeTexCoord')
    xyz = nd.new('ShaderNodeSeparateXYZ')
    ln.new(tc.outputs['Object'], xyz.inputs[0])
    flat_ = nd.new('ShaderNodeCombineXYZ')
    ln.new(xyz.outputs['X'], flat_.inputs['X'])
    ln.new(xyz.outputs['Y'], flat_.inputs['Y'])
    length = nd.new('ShaderNodeVectorMath')
    length.operation = 'LENGTH'
    ln.new(flat_.outputs[0], length.inputs[0])
    mr = nd.new('ShaderNodeMapRange')
    mr.inputs['From Min'].default_value = 0.0
    mr.inputs['From Max'].default_value = 300.0
    ln.new(length.outputs['Value'], mr.inputs['Value'])
    rp = nd.new('ShaderNodeValToRGB')
    rp.color_ramp.interpolation = 'EASE'
    rp.color_ramp.elements[0].position = 0.0
    rp.color_ramp.elements[0].color = (0.0, 0.0, 0.0, 1.0)
    rp.color_ramp.elements[1].position = 1.0
    rp.color_ramp.elements[1].color = (0.45, 0.45, 0.45, 1.0)
    rp.color_ramp.elements[0].color = (middle, middle, middle, 1.0)
    for pos, v in ((0.16, middle), (0.36, 1.0)):
        rp.color_ramp.elements.new(pos).color = (v, v, v, 1.0)
    ln.new(mr.outputs[0], rp.inputs['Fac'])
    side = nd.new('ShaderNodeMapRange')
    side.inputs['From Min'].default_value = -200.0
    side.inputs['From Max'].default_value = 200.0
    side.inputs['To Min'].default_value = 1.35 * strength
    side.inputs['To Max'].default_value = 0.55 * strength
    ln.new(xyz.outputs['X'], side.inputs['Value'])
    mul = nd.new('ShaderNodeMath')
    mul.operation = 'MULTIPLY'
    ln.new(rp.outputs['Color'], mul.inputs[0])
    ln.new(side.outputs[0], mul.inputs[1])
    if not glow:
        ln.new(mul.outputs[0], em.inputs['Strength'])
        return em
    # and where a dome's upper left looks to, the glow of the lamp
    gx, gy, rx, ry = -24.0, Y_REF + 22.0, 34.0, 30.0
    mp = nd.new('ShaderNodeMapping')
    mp.inputs['Location'].default_value = (-gx / rx, -gy / ry, 0.0)
    mp.inputs['Scale'].default_value = (1.0 / rx, 1.0 / ry, 0.0)
    ln.new(tc.outputs['Object'], mp.inputs['Vector'])
    near = nd.new('ShaderNodeVectorMath')
    near.operation = 'LENGTH'
    ln.new(mp.outputs[0], near.inputs[0])
    rg = nd.new('ShaderNodeValToRGB')
    rg.color_ramp.interpolation = 'EASE'
    rg.color_ramp.elements[0].color = (glow, glow, glow, 1.0)
    rg.color_ramp.elements[1].position = 1.0
    rg.color_ramp.elements[1].color = (0.0, 0.0, 0.0, 1.0)
    ln.new(near.outputs['Value'], rg.inputs['Fac'])
    add = nd.new('ShaderNodeMath')
    add.operation = 'ADD'
    ln.new(mul.outputs[0], add.inputs[0])
    ln.new(rg.outputs['Color'], add.inputs[1])
    ln.new(add.outputs[0], em.inputs['Strength'])
    return em


polish('sheet', machine + over_paper)


def shoot_region(name, x0, y0, x1, y1):
    """Part of the whole picture, in its pixels: rendered as it would be there."""
    x0, y0 = int(math.floor(x0)), int(math.floor(y0))
    x1, y1 = int(math.ceil(x1)), int(math.ceil(y1))
    kit.r.use_border = True
    kit.r.use_crop_to_border = True
    kit.r.border_min_x = x0 / w_px
    kit.r.border_max_x = x1 / w_px
    kit.r.border_max_y = 1.0 - y0 / h_px
    kit.r.border_min_y = 1.0 - y1 / h_px
    img = kit.shoot(name)
    kit.r.use_border = False
    kit.r.use_crop_to_border = False
    return img, x0, y0


# the machine alone, on its shadow, at half the size of the keys
if not KEYS_ONLY:
    kit.r.resolution_percentage = 50
    case_with = kit.shoot('case with')
    for o in machine:
        o.hide_render = True
    desk.hide_render = True
    bail = kit.shoot('bail')
    over_h = int(math.ceil(to_px((0.0, PLATE_Y - 0.25, PLATE_Z - 0.3))[1])) // 2
    kit.save('kb-bail.png', bail[:over_h])
    for o in machine:
        o.hide_render = False
    desk.hide_render = False
    for o in over_paper:
        o.hide_render = True
    case = kit.shoot('case')
    kit.r.resolution_percentage = 100
    # its shadow goes before the picture does (it is laid over whatever is there)
    _fade = 40.0
    _h, _w = case.shape[:2]
    _rx = (np.minimum(np.arange(_w), np.arange(_w)[::-1]) / _fade).clip(0.0, 1.0)
    _ry = (np.minimum(np.arange(_h), np.arange(_h)[::-1]) / _fade).clip(0.0, 1.0)
    case, case_with = case.copy(), case_with.copy()
    for c_ in (case, case_with):
        c_[:, :, 3] = (c_[:, :, 3] * np.outer(_ry * _ry * (3.0 - 2.0 * _ry), _rx * _rx * (3.0 - 2.0 * _rx)) + 0.5).astype(np.uint8)
    kit.save('kb-case.png', case)

# each row: where its middle comes in the picture, and how big things are there
ROW_MID = [to_px((0.0, RY[r], RZ[r]))[1] for r in range(6)]
ROW_K = [scale_at((0.0, RY[r], RZ[r])) for r in range(6)]
# (a row goes from half way to the row behind to half way to the row in front)
ROW_TOPS = [0.0] * 7
for r in range(1, 6):
    ROW_TOPS[r] = (ROW_MID[r - 1] + ROW_MID[r]) * 0.5
ROW_TOPS[0] = ROW_MID[0] - (ROW_TOPS[1] - ROW_MID[0])
ROW_TOPS[6] = ROW_MID[5] + (ROW_MID[5] - ROW_TOPS[5])

# ---- a key of each kind, on nothing, in the row they are rendered in, in the middle of it. With the
# shadow it throws on the floor of the well.
for o in machine + over_paper:
    o.hide_render = True
desk.hide_render = True
bpy.data.objects.remove(bpy.data.objects['sheet'], do_unlink=True)

bm = bmesh.new()
vs = [bm.verts.new((x, Y_REF + y, Z_REF - KEY_OVER)) for x, y in ((-30, -30), (30, -30), (30, 30), (-30, 30))]
bm.faces.new(vs)
catcher = put('tile floor', bm, principled('tile floor', (0.05, 0.05, 0.05), rough=0.8), smooth=False)
catcher.is_shadow_catcher = True

K_REF = scale_at((0.0, Y_REF, Z_REF))
U_REF = U * K_REF
REF_MID = to_px((0.0, Y_REF, Z_REF))[1]
PRESS_PX = abs(to_px((0.0, Y_REF, Z_REF - PRESS))[1] - REF_MID)


def tile_of(name, row_h, down, make):
    """A key: its middle where a key's middle goes, two keys wide with a margin."""
    parts = make()
    for o in parts:
        o.location = (0.0, Y_REF, Z_REF - (PRESS if down else 0.0))
        # (it goes away from the eye: a good deal further than it would seem to from here)
        if down:
            o.scale = (0.965, 0.965, 1.0)
    polish('glaze', parts, 1.5, glow=3.0)
    x0 = w_px * 0.5 - U_REF - TILE_MARGIN
    x1 = w_px * 0.5 + U_REF + TILE_MARGIN
    y0 = REF_MID - row_h * 0.5 - TILE_TOP
    y1 = REF_MID + row_h * 0.5 + TILE_BELOW + PRESS_PX
    img, ix, iy = shoot_region(name, x0, y0, x1, y1)
    if down:
        # out of the light, down there
        img = img.copy()
        img[:, :, :3] = (img[:, :, :3] * 0.80).astype(np.uint8)
    for o in parts:
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.objects.remove(bpy.data.objects['glaze'], do_unlink=True)
    return img, REF_MID - row_h * 0.5 - iy, w_px * 0.5 - U_REF - ix


def round_key(kind):
    def make():
        hw = U - (U - D) * 0.5
        return key_parts('key', hw, D * 0.5, kind)
    return make


def oblong_key(kind, h):
    def make():
        hw = U - (U - D) * 0.5 + 0.02
        return key_parts('key', hw, h * 0.5, kind, ring=0.075, dome=0.025)
    return make


def bar():
    return bar_parts('bar', U - 0.12, 0.42)


ROW_H_REF = ROW_TOPS[4] - ROW_TOPS[3]
SLIM_H_REF = (ROW_TOPS[1] - ROW_TOPS[0]) * K_REF / ROW_K[0]
TILES = {}
for kind, make, rh in (('letter', round_key('letter'), ROW_H_REF), ('function', round_key('function'), ROW_H_REF),
                       ('return', round_key('return'), ROW_H_REF), ('space', bar, ROW_H_REF),
                       ('slim', oblong_key('letter', 0.80), SLIM_H_REF)):
    for down in (False, True):
        name = f'kb-{kind}-{"down" if down else "up"}.png'
        TILES[name], top_px, left_px = tile_of(name[:-4], rh, down, make)
        kit.save(name, TILES[name])
        TILES[name + ' at'] = (top_px, left_px)
        if KEYS_ONLY:
            sys.exit(0)

# ---- as it lies in the drawer: all of it, small, with nothing printed on it. The keys of the
# commonest of the keyboards, cut from the one key as DEsk cuts them.


def lay(big, img, x0, y0):
    h, w = img.shape[:2]
    if x0 < 0 or y0 < 0:
        return
    x1, y1 = min(x0 + w, big.shape[1]), min(y0 + h, big.shape[0])
    p = img[:y1 - y0, :x1 - x0].astype(np.float32)
    a = p[:, :, 3:4] / 255.0
    big[y0:y1, x0:x1, :3] = p[:, :, :3] * a + big[y0:y1, x0:x1, :3] * (1.0 - a)
    big[y0:y1, x0:x1, 3:4] = p[:, :, 3:4] + big[y0:y1, x0:x1, 3:4] * (1.0 - a)


def scaled(img, k):
    """A picture made smaller or bigger, near enough for a picture that is to be made small."""
    h, w = img.shape[:2]
    nh, nw = max(int(round(h * k)), 1), max(int(round(w * k)), 1)
    ys = (np.arange(nh) / k).astype(int).clip(0, h - 1)
    xs = (np.arange(nw) / k).astype(int).clip(0, w - 1)
    return img[ys][:, xs]


def laid_out():
    rows = [[1.0] * 15, [1.0] * 13 + [2.0], [1.5] + [1.0] * 12 + [1.5], [1.75] + [1.0] * 11 + [2.25],
            [2.25] + [1.0] * 10 + [2.75], [1.0] * 5 + [4.75, 1.25] + [1.0] * 4]
    big = np.zeros((h_px, w_px, 4), dtype=np.float32)
    # (the machine, back up to the keys' size)
    c = np.repeat(np.repeat(case_with, 2, axis=0), 2, axis=1)[:h_px, :w_px].astype(np.float32)
    big[:c.shape[0], :c.shape[1]] = c
    for ri, row in enumerate(rows):
        slim = ri == 0
        k = ROW_K[ri] / K_REF
        u_px = U * ROW_K[ri]
        x = w_px * 0.5 - 7.5 * u_px
        for cell in row:
            width = int(round(cell * u_px))
            enter = ri == 3 and cell == 2.25
            space = cell == 4.75
            letter = ri >= 1 and cell == 1.0 and ri != 5
            kind = 'kb-slim-up.png' if slim else 'kb-return-up.png' if enter else (
                'kb-space-up.png' if space else 'kb-letter-up.png' if letter else 'kb-function-up.png')
            t = scaled(TILES[kind], k)
            top_px, left_px = TILES[kind + ' at']
            half = int(round(U_REF * 0.5 * k))
            m = int(round(left_px * k))
            tw = t.shape[1]
            cols = np.concatenate((np.arange(m + half), np.full(max(width - half * 2, 0), tw // 2),
                                   np.arange(tw - m - half, tw)))
            piece = t[:, cols]
            x0, y0 = int(round(x)) - m, int(round(ROW_MID[ri] - (ROW_H_REF if not slim else SLIM_H_REF) * k * 0.5 - TILE_TOP * k))
            lay(big, piece, x0, y0)
            x += cell * u_px
    # (what is in the well stays in it: the front of the case comes over it)
    cut = np.repeat(np.repeat(case_with, 2, axis=0), 2, axis=1)[:h_px, :w_px].astype(np.float32)
    y_cut = int(to_px((0.0, Y_WELL0, z_top(Y_WELL0)))[1])
    big[y_cut:cut.shape[0]] = cut[y_cut:]
    return big


whole = laid_out()
kit.save('kb-whole.png', (whole + 0.5).clip(0, 255).astype(np.uint8))
# small: four to one, and then again
f = 8
hh, ww = whole.shape[0] // f * f, whole.shape[1] // f * f
small = whole[:hh, :ww].reshape(hh // f, f, ww // f, f, 4).mean(axis=(1, 3))
kit.save('kb-top.png', (small + 0.5).clip(0, 255).astype(np.uint8))

body_left = to_px((-W_CASE * 0.5, Y_FRONT, 0.0))[0]
body_foot = to_px((0.0, Y_FRONT, 0.0))[1]
platen_top = to_px((0.0, Y_PLATEN, Z_PLATEN + PLATEN_R))[1]
tile_top, tile_left = TILES['kb-letter-up.png at']
slim_top, _ = TILES['kb-slim-up.png at']

measures = [
    ('KB_CASE_W', w_px),
    ('KB_CASE_H', h_px),
    ('KB_BODY_X', int(round(body_left)), 'its body, in the pictures: the left of its front'),
    ('KB_BODY_FOOT', int(round(body_foot)), 'where its front meets the desk'),
    ('KB_PLATEN_TOP', int(round(platen_top)), 'the top of the platen, where paper comes out'),
    ('KB_PLATEN_X', int(round(to_px((-PLATEN_L, Y_PLATEN, Z_PLATEN))[0])), 'and its left end'),
    ('KB_PAPER_W', int(round(U * 12.5 * scale_at((0.0, Y_PLATEN, Z_PLATEN)))), 'a sheet in it'),
    ('KB_PLATE_Y', int(round(to_px((0.0, PLATE_Y, PLATE_Z))[1])), 'the plate paper goes in behind'),
    ('KB_BAIL_Y', int(round(to_px((0.0, BAIL_Y, BAIL_Z))[1])), 'and the bail over it'),
    ('KB_BODY_TOP', int(round(to_px((0.0, Y_BACK, z_top(Y_BACK)))[1])), 'the back of its body'),
    ('KB_LEVER_X0', int(round(to_px((-PLATEN_L - 0.6, Y_PLATEN, Z_PLATEN + 1.0))[0])), 'the lever, in the pictures'),
    ('KB_LEVER_X1', int(round(to_px((-PLATEN_L + 3.2, Y_PLATEN + 1.5, Z_PLATEN + 1.8))[0]))),
    ('KB_LEVER_Y0', int(round(to_px((-PLATEN_L + 2.0, Y_PLATEN + 2.2, Z_PLATEN + 1.85))[1])) - 30),
    ('KB_COLS', COLS),
    ('KB_K_REF', int(round(K_REF * 100.0)), 'pixels to the centimetre where the keys are rendered, in hundredths'),
    ('KB_U_REF', int(round(U_REF)), 'a key across, there'),
    ('KB_ROW_REF', int(round(ROW_H_REF)), 'a full row, there'),
    ('KB_SLIM_REF', int(round(SLIM_H_REF)), 'the top row, as it would be there'),
    ('KB_TILE_TOP', int(round(tile_top)), 'how far above its row a tile begins'),
    ('KB_SLIM_TOP', int(round(slim_top))),
    ('KB_TILE_LEFT', int(round(tile_left)), 'and how far to the left of a key'),
    ('KB_LIGHT_X', int(round(to_px((W_CASE * 0.5 - 3.0, Y_JEWEL, z_top(Y_JEWEL)))[0]))),
    ('KB_LIGHT_Y', int(round(to_px((W_CASE * 0.5 - 3.0, Y_JEWEL, z_top(Y_JEWEL)))[1]))),
]
measures += [
    ('KB_SHOW_FOOT', int(round(to_px((0.0, Y_FRONT, z_top(Y_FRONT) - 0.6))[1])), 'lying at the front of the desk, as much as shows'),
    ('KB_KNOB_X', int(round(to_px((-PLATEN_L - 0.48 - KNOB_L, Y_PLATEN, Z_PLATEN))[0])), 'the left of its knob'),
    ('KB_WELL_FX', int(round(to_px((-W_WELL * 0.5, Y_WELL0, z_top(Y_WELL0)))[0])), 'the well: its front left corner'),
    ('KB_WELL_FY', int(round(to_px((-W_WELL * 0.5, Y_WELL0, z_top(Y_WELL0)))[1]))),
    ('KB_WELL_BX', int(round(to_px((-W_WELL * 0.5, Y_WELL1, z_top(Y_WELL1)))[0])), 'and its back left corner'),
    ('KB_WELL_BY', int(round(to_px((-W_WELL * 0.5, Y_WELL1, z_top(Y_WELL1)))[1]))),
]
for r in range(6):
    measures.append((f'KB_ROW_MID_{r}', int(round(ROW_MID[r])), 'the middle of each row') if r == 0
                    else (f'KB_ROW_MID_{r}', int(round(ROW_MID[r]))))
for r in range(7):
    measures.append((f'KB_ROW_TOP_{r}', int(round(ROW_TOPS[r])), 'each row, down the picture') if r == 0
                    else (f'KB_ROW_TOP_{r}', int(round(ROW_TOPS[r]))))
for r in range(6):
    measures.append((f'KB_ROW_U_{r}', int(round(U * ROW_K[r] * 10.0)), 'a key across in each, in tenths of a pixel') if r == 0
                    else (f'KB_ROW_U_{r}', int(round(U * ROW_K[r] * 10.0))))
kit.write_map('keyboard-map.h', 'the keyboard, as DEsk lays it out (src/keyboardart.c)', measures)
