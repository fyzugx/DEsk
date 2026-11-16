"""What a program kept in DEsk's drawer can be given to look like (include/looks.h), for
nothing but that: things a person might keep in a desk drawer, lying flat on the felt and
seen from straight above, in the drawer's light -- the lamp up behind the desk to the
right -- with nothing round them. The drawer gives each its shadows, as long as the thing
is thick.

    radio/        a little transistor radio on its back, its aerial folded along it
    paintbox/     a tin of watercolours, open, a brush across it
    gamepad/      a game's controller, its pad and its buttons
    headphones/   a pair of headphones put down flat

each a look's folder (look.png and look.ini, docs/LOOKS.md), shot as anyone's look is
(bake.py). Each is made up here, in plain shapes: none is anyone's in particular. Made
with

    blender -b --factory-startup -P art/looks/looks.py -- art/looks [--quick] [--only NAME]
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path[:0] = [os.path.dirname(os.path.abspath(__file__)), os.path.dirname(os.path.dirname(os.path.abspath(__file__)))]
import kit  # noqa: E402
from kit import MAT, OUT, QUICK, cap, collar, group, loft, plinth, principled, put  # noqa: E402

ONLY = kit.arg('--only', None)


# ---- shapes kit.py has not got

def disc(name, cx, cy, r, z0, z1, mat, n=48, bevel=0.0):
    """A round flat solid: a knob, a button, a cup."""
    return plinth(name, cx, cy, r, r, r, z0, z1, mat, bevel=bevel, n=max(n // 4, 3))


def ring(name, cx, cy, r, minor, z, mat, n=64, m=12, flat=1.0):
    """A ring of round section lying flat: a bezel, a cushion."""
    bm = bmesh.new()
    rows = []
    for i in range(n):
        a = 2.0 * math.pi * i / n
        row = []
        for j in range(m):
            b = 2.0 * math.pi * j / m
            rr = r + minor * math.cos(b)
            row.append(bm.verts.new((cx + rr * math.cos(a), cy + rr * math.sin(a), z + minor * flat * math.sin(b))))
        rows.append(row)
    for i in range(n):
        for j in range(m):
            a, b = rows[i], rows[(i + 1) % n]
            bm.faces.new((a[j], b[j], b[(j + 1) % m], a[(j + 1) % m]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat)


def tube(name, pts, radius, mat, m=14, flat=1.0, caps=True):
    """A round tube along points (x, y, z), squashed to flat of its height."""
    bm = bmesh.new()
    rows = []
    for i, p in enumerate(pts):
        p = Vector(p)
        a = Vector(pts[max(i - 1, 0)])
        b = Vector(pts[min(i + 1, len(pts) - 1)])
        t = (b - a).normalized()
        side = t.cross(Vector((0.0, 0.0, 1.0)))
        if side.length < 1e-6:
            side = Vector((1.0, 0.0, 0.0))
        side.normalize()
        up = side.cross(t).normalized()
        rows.append([bm.verts.new(p + side * radius * math.cos(2.0 * math.pi * j / m)
                                  + up * radius * flat * math.sin(2.0 * math.pi * j / m)) for j in range(m)])
    for a, b in zip(rows, rows[1:]):
        for j in range(m):
            bm.faces.new((a[j], a[(j + 1) % m], b[(j + 1) % m], b[j]))
    if caps:
        bm.faces.new(list(reversed(rows[0])))
        bm.faces.new(rows[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat)


def outline_offset(pts, d):
    """An outline (anticlockwise) moved in by d along its normals."""
    out = []
    n = len(pts)
    for i in range(n):
        ax, ay = pts[i - 1]
        bx, by = pts[(i + 1) % n]
        tx, ty = bx - ax, by - ay
        ln = math.hypot(tx, ty) or 1.0
        out.append((pts[i][0] - ty / ln * d, pts[i][1] + tx / ln * d))
    return out


def slab(name, outline, z0, z1, mat, bevel=0.0):
    """A flat solid of any outline (anticlockwise), its top edge rounded off by bevel."""
    rings = [[(x, y, z0) for x, y in outline]]
    if bevel > 0.0:
        rings.append([(x, y, z1 - bevel) for x, y in outline])
        for k in range(1, 6):
            a = math.radians(90.0 * k / 5)
            d = bevel * (1.0 - math.cos(a))
            rings.append([(x, y, z1 - bevel + bevel * math.sin(a)) for x, y in outline_offset(outline, d)])
    else:
        rings.append([(x, y, z1) for x, y in outline])
    return loft(name, rings, mat, cap_last=True, sharp=60.0)


def mat(name, *a, **k):
    MAT[name] = principled(name, *a, **k)
    return MAT[name]


# ---- the radio: on its back, its face up -- a round grille on the left, the
# dial on the right, the aerial folded along its top edge

RADIO_HW, RADIO_HH = 7.5, 4.6
group('radio')
mat('radio-case', (0.42, 0.07, 0.05), rough=0.32, coat=0.6, coat_rough=0.08)
mat('radio-face', (0.80, 0.74, 0.60), rough=0.45)
kit.grain(MAT['radio-face'], 120.0, 0.004, 0.4)
mat('chrome', (0.80, 0.80, 0.82), rough=0.12, metallic=1.0)
mat('grille', (0.62, 0.62, 0.63), rough=0.28, metallic=1.0)
mat('hole', (0.03, 0.03, 0.03), rough=0.9)
mat('dial', (0.93, 0.89, 0.76), rough=0.4)
mat('ink', (0.10, 0.08, 0.07), rough=0.6)
mat('needle', (0.75, 0.10, 0.06), rough=0.4)
plinth('radio-body', 0.0, 0.0, RADIO_HW, RADIO_HH, 1.6, 0.0, 3.4, 'radio-case', bevel=0.7)
plinth('radio-plate', 0.0, -0.1, RADIO_HW - 0.7, RADIO_HH - 0.8, 1.0, 3.2, 3.55, 'radio-face', bevel=0.12)
# the grille: perforated, in a chrome ring
GX, GY, GR = -3.2, -0.1, 3.0
disc('radio-grille', GX, GY, GR, 3.5, 3.62, 'grille', n=96)
ring('radio-grille-ring', GX, GY, GR + 0.12, 0.17, 3.62, 'chrome', n=96)
k = 0
step = 0.42
y = -GR
while y <= GR:
    x = -GR
    row = int(round((y + GR) / step))
    x += step * 0.5 if row % 2 else 0.0
    while x <= GR:
        if math.hypot(x, y) < GR - 0.3:
            disc('radio-hole-%d' % k, GX + x, GY + y, 0.11, 3.6, 3.64, 'hole', n=12)
            k += 1
        x += step
    y += step * 0.87
# the dial: its face, its scale, the needle, a chrome bezel; the knob below it
DX, DY, DR = 3.9, 0.55, 2.25
disc('radio-dial', DX, DY, DR, 3.5, 3.62, 'dial', n=96)
ring('radio-dial-bezel', DX, DY, DR + 0.08, 0.2, 3.64, 'chrome', n=96)
for i in range(21):
    a = math.radians(200.0 - 220.0 * i / 20)
    long_ = i % 5 == 0
    r0, r1 = DR - (0.75 if long_ else 0.5), DR - 0.25
    bm = bmesh.new()
    w = 0.035 if not long_ else 0.05
    c, s = math.cos(a), math.sin(a)
    vs = [bm.verts.new((DX + c * rr + -s * ww, DY + s * rr + c * ww, 3.635))
          for rr, ww in ((r0, -w), (r1, -w), (r1, w), (r0, w))]
    bm.faces.new(vs)
    put('radio-tick-%d' % i, bm, 'ink', smooth=False)
na = math.radians(118.0)
bm = bmesh.new()
vs = [bm.verts.new((DX + math.cos(na) * rr - math.sin(na) * ww, DY + math.sin(na) * rr + math.cos(na) * ww, 3.645))
      for rr, ww in ((-0.3, -0.04), (DR - 0.3, -0.02), (DR - 0.3, 0.02), (-0.3, 0.04))]
bm.faces.new(vs)
put('radio-needle', bm, 'needle', smooth=False)
disc('radio-hub', DX, DY, 0.32, 3.6, 3.75, 'chrome', n=32, bevel=0.08)
cap('radio-knob', DX + 0.4, -2.9, 0.75, 0.75, 0.75, 3.5, 0.55, -0.12, 'chrome', n=12)
# the aerial, folded down along the top edge, and its hinge
tube('radio-aerial', [(-6.6 + 0.5 * i, RADIO_HH - 0.45, 3.95) for i in range(27)], 0.17, 'chrome')
disc('radio-aerial-tip', -6.75, RADIO_HH - 0.45, 0.24, 3.75, 4.15, 'chrome', n=16, bevel=0.1)
disc('radio-hinge', 6.55, RADIO_HH - 0.45, 0.42, 3.4, 4.0, 'chrome', n=24, bevel=0.15)

# ---- the paint box: an open tin, black outside and white enamel in, twelve
# pans in two rows, and a brush laid across it

group('paintbox')
mat('tin', (0.035, 0.035, 0.04), rough=0.25, coat=0.5)
mat('enamel', (0.90, 0.90, 0.87), rough=0.22, coat=0.3)
PANS = [(0.93, 0.83, 0.15), (0.95, 0.62, 0.08), (0.90, 0.36, 0.06), (0.80, 0.14, 0.08),
        (0.62, 0.05, 0.12), (0.55, 0.10, 0.40), (0.30, 0.12, 0.45), (0.10, 0.16, 0.55),
        (0.08, 0.40, 0.62), (0.05, 0.40, 0.30), (0.30, 0.48, 0.12), (0.48, 0.24, 0.10)]
TIN_HW, TIN_HH = 7.6, 4.3
plinth('paint-tin', 0.0, 0.0, TIN_HW, TIN_HH, 0.7, 0.0, 1.5, 'tin', bevel=0.25)
plinth('paint-tray', 0.0, 0.0, TIN_HW - 0.35, TIN_HH - 0.35, 0.5, 1.3, 1.42, 'enamel')
for i, colour in enumerate(PANS):
    col, row = i % 6, i // 6
    cx = -TIN_HW + 1.05 + col * 2.5 + 0.2
    cy = 1.55 - row * 3.1
    collar('paint-pan-%d' % i, cx, cy, 1.0, 1.2, 0.18, 0.12, 1.4, 1.75, 'enamel')
    m = mat('paint-%d' % i, colour, rough=0.55, coat=0.25)
    kit.grain(m, 40.0, 0.01, 0.6)
    used = 0.04 + 0.045 * ((i * 7) % 5)
    cap('paint-cake-%d' % i, cx, cy, 1.0, 1.2, 0.18, 1.4, 0.3, used, 'paint-%d' % i, n=8)
mat('brush-handle', (0.55, 0.08, 0.05), rough=0.25, coat=0.7)
mat('ferrule', (0.78, 0.76, 0.72), rough=0.2, metallic=1.0)
mat('hair', (0.20, 0.12, 0.07), rough=0.7)
a = math.radians(-14.0)
ax, ay = math.cos(a), math.sin(a)


def along(t, lift):
    return (-6.4 * ax + t * ax, -6.4 * ay + t * ay + 0.9, 1.9 + lift)


hs = [along(t * 0.25, 0.0) for t in range(0, 41)]
tube('brush-handle', hs, 0.28, 'brush-handle')
disc('brush-end', hs[0][0], hs[0][1], 0.22, 1.65, 2.05, 'brush-handle', n=16, bevel=0.1)
tube('brush-ferrule', [along(10.0 + t * 0.1, 0.0) for t in range(19)], 0.31, 'ferrule')
hair = [along(11.8 + t * 0.12, 0.0) for t in range(16)]
bm = bmesh.new()
rows = []
for i, p in enumerate(hair):
    t = i / (len(hair) - 1)
    rr = 0.3 * (1.0 + 0.25 * math.sin(math.pi * t * 0.8)) * (1.0 - t ** 2.2) + 0.01
    rows.append([bm.verts.new((p[0] - ay * rr * math.cos(2 * math.pi * j / 12),
                               p[1] + ax * rr * math.cos(2 * math.pi * j / 12),
                               p[2] + rr * math.sin(2 * math.pi * j / 12))) for j in range(12)])
for r0, r1 in zip(rows, rows[1:]):
    for j in range(12):
        bm.faces.new((r0[j], r0[(j + 1) % 12], r1[(j + 1) % 12], r1[j]))
bm.faces.new(list(reversed(rows[0])))
bm.faces.new(rows[-1])
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
put('brush-hair', bm, 'hair')

# ---- the controller: a pebble with a grip each side, a cross of a pad on the
# left, two round buttons on the right, two small ones between

group('gamepad')
mat('pad-shell', (0.56, 0.57, 0.58), rough=0.42)
kit.grain(MAT['pad-shell'], 60.0, 0.006, 0.5)
mat('pad-dark', (0.10, 0.10, 0.11), rough=0.35, coat=0.2)
mat('pad-red', (0.58, 0.09, 0.08), rough=0.3, coat=0.4)
mat('pad-well', (0.34, 0.35, 0.36), rough=0.5)


def pebble():
    """Its outline: a body and two grips, smoothly one."""
    def sd(x, y):
        def box(px, py, hw, hh, r):
            qx, qy = abs(px) - hw + r, abs(py) - hh + r
            return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r
        a = box(x, y - 0.5, 4.8, 2.9, 2.4)
        b = math.hypot(x - 4.9, y + 0.9) - 3.2
        c = math.hypot(x + 4.9, y + 0.9) - 3.2
        k = 1.4
        for o in (b, c):
            h = max(k - abs(a - o), 0.0) / k
            a = min(a, o) - h * h * k * 0.25
        return a
    pts = []
    for i in range(160):
        th = 2.0 * math.pi * i / 160
        dx, dy = math.cos(th), math.sin(th)
        r = 0.0
        while sd(dx * (r + 0.02), dy * (r + 0.02)) < 0.0 and r < 12.0:
            r += 0.02
        pts.append((dx * r, dy * r))
    return pts


shell = pebble()
slab('pad-body', shell, 0.0, 2.2, 'pad-shell', bevel=0.9)
# the cross: in a shallow well
disc('pad-cross-well', -4.6, 0.05, 1.75, 1.9, 2.24, 'pad-well', n=64)
for hw, hh in ((1.45, 0.48), (0.48, 1.45)):
    plinth('pad-cross-%s' % ('across' if hw > hh else 'down'), -4.6, 0.05, hw, hh, 0.12, 2.1, 2.62, 'pad-dark', bevel=0.12)
disc('pad-cross-dimple', -4.6, 0.05, 0.28, 2.6, 2.64, 'pad-shell', n=24)
# the two round buttons, in their wells
for i, (bx, by) in enumerate(((3.6, -0.35), (5.4, 0.85))):
    disc('pad-btn-well-%d' % i, bx, by, 0.95, 1.95, 2.25, 'pad-well', n=48)
    cap('pad-btn-%d' % i, bx, by, 0.72, 0.72, 0.72, 2.0, 0.6, -0.12, 'pad-red', n=12)
# and the two small ones between
for i, bx in enumerate((-0.85, 0.85)):
    plinth('pad-pill-well-%d' % i, bx, -1.3, 0.62, 0.26, 0.26, 2.0, 2.24, 'pad-well')
    plinth('pad-pill-%d' % i, bx, -1.3, 0.5, 0.17, 0.17, 2.0, 2.36, 'pad-dark', bevel=0.08)

# ---- the headphones: put down flat, the band in a bow over the two cups

group('headphones')
mat('cup', (0.07, 0.07, 0.075), rough=0.38, coat=0.3)
mat('cushion', (0.12, 0.08, 0.06), rough=0.55)
kit.grain(MAT['cushion'], 90.0, 0.006, 0.7)
mat('steel', (0.70, 0.70, 0.72), rough=0.22, metallic=1.0)
mat('band-pad', (0.16, 0.10, 0.07), rough=0.55)
kit.grain(MAT['band-pad'], 90.0, 0.006, 0.7)
CUPX, CUPY, CUPR = 6.0, -3.2, 3.6
for i, sx in enumerate((-1.0, 1.0)):
    cx = sx * CUPX
    ring('hp-cushion-%d' % i, cx, CUPY, CUPR - 0.4, 0.85, 0.85, 'cushion', flat=0.9)
    disc('hp-cup-%d' % i, cx, CUPY, CUPR - 0.3, 0.5, 2.6, 'cup', n=96, bevel=0.8)
    ring('hp-trim-%d' % i, cx, CUPY, CUPR - 1.15, 0.09, 2.62, 'steel', n=96)
    disc('hp-cap-%d' % i, cx, CUPY, 1.4, 2.45, 2.7, 'steel', n=64, bevel=0.15)
    # the yoke, from the band down to the cup's edge
    tube('hp-yoke-%d' % i, [(cx + sx * 0.2, CUPY + CUPR - 0.6 + t * 0.12, 2.2) for t in range(14)], 0.22, 'steel')
# the band: steel, in a bow from one yoke to the other, and the pad over its top
BOW_W, BOW_H, BOW_Y = CUPX + 0.2, 7.6, CUPY + CUPR + 0.95


def bow(t, inset=0.0):
    a = math.pi * t
    return (-math.cos(a) * (BOW_W - inset), BOW_Y + math.sin(a) * (BOW_H - inset), 2.2)


tube('hp-band', [bow(i / 60.0) for i in range(61)], 0.32, 'steel', flat=0.6)
tube('hp-band-pad', [bow(0.2 + 0.6 * i / 40.0, 0.25) for i in range(41)], 0.85, 'band-pad', flat=0.45)

# ---- shot as anyone's look is (bake.py), each into a look's folder of its own

import bake  # noqa: E402

# what each is called, how thick it stands for its shadow (cm), where it goes among the looks
LOOKS = {
    'radio': ({'en': 'Radio', 'ja': 'ラジオ', 'zh': '收音机', 'ko': '라디오', 'es': 'Radio', 'fr': 'Radio',
               'de': 'Radio'}, 2.2, 10),
    'paintbox': ({'en': 'Paint box', 'ja': '絵の具箱', 'zh': '颜料盒', 'ko': '물감 상자', 'es': 'Caja de pinturas',
                  'fr': 'Boîte de peinture', 'de': 'Malkasten'}, 1.0, 20),
    'gamepad': ({'en': 'Game controller', 'ja': 'ゲームのコントローラー', 'zh': '游戏手柄', 'ko': '게임 컨트롤러',
                 'es': 'Mando de juego', 'fr': 'Manette', 'de': 'Gamecontroller'}, 1.7, 30),
    'headphones': ({'en': 'Headphones', 'ja': 'ヘッドホン', 'zh': '耳机', 'ko': '헤드폰', 'es': 'Auriculares',
                    'fr': 'Casque', 'de': 'Kopfhörer'}, 1.9, 40),
}

bake.stage(16 if QUICK else int(kit.arg('--samples', 96)))
for name, (names, thick, order) in LOOKS.items():
    if ONLY and ONLY != name:
        continue
    bake.shoot(kit.GROUPS[name], os.path.join(OUT, name), names, thick, order=order, author='DEsk',
               comment='Made up in plain shapes by art/looks/looks.py')
bake.tidy()
