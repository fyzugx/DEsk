"""The calculator on the desk, made in Blender.

A pocket-book of a calculator in dark moulded plastic: two halves of a size,
hinged along the middle like a notebook. On the right half are a display of
dots, which says what is being worked out as well as the answer, and the
keys everyone needs; the left half is its lid, and opened out it has a pad of
paper (what has been worked out) and the keys hardly anyone needs. Shut, the
lid lies over the keys, which is why the keys of both halves stand in trays:
they meet each other's gaps, not each other. And shut it is still a
calculator: the lid's outside is one, a display of figures and the keys for
a sum, in a shallower tray of its own. Each face has two small keys at its
head: the one shuts it or opens it, the other is it put away.

The keys are the point of it: each stands in a well of its own in the tray's
floor, taller than the floor by a good bit, its sides sloping in, its corners
round, its top dished for a finger, and printed.

    blender -b --factory-startup -P art/calc/calc.py -- OUTDIR [--quick]
        [--samples N] [--state shut|half|open] [--view desk|keys|disp|lid]
    blender -b --factory-startup -P art/calc/calc.py -- OUTDIR --bake [--quick]

The first renders one picture, to look at. --bake renders what DEsk lays the
calculator from (src/calcart.c), all from the one eye, in OUTDIR: the
calculator open and shut (calc-open.png, calc-shut.png) with nothing on its
display or its paper, which DEsk writes itself; every key held down
(calc-keys.png, a sheet of small pieces to lay over the open one); the lid on
its way, a third of the size, as it goes by too fast to read
(calc-lid-NN.png, and where they go, calc-film.h); the calculator as it lies
in the drawer (calc-top.png); and where all that is (calc-map.h).
--bake --only film renders the lid on its way and calc-film.h, and nothing else.

The sizes are in centimetres; x to the right, y away from the eye, z up, the
desk at z = 0. The calculator is original: nothing on it is a make's, and the
words on it are only what its keys do.
"""

import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

import kit
from kit import ARGV, COLL, MAT, OUT, QUICK, SCENE, arg, loft, put, rrect

BAKE = '--bake' in ARGV

SAMPLES = int(arg('--samples', 12 if QUICK else 256))
STATE = arg('--state', 'open')
VIEW = arg('--view', 'desk')
ANGLE = {'open': 0.0, 'half': 112.0, 'shut': 180.0}[STATE]

# ---- the calculator's measures
W, H = 10.4, 17.6             # one half, seen from above
GAP = 0.26                    # from the hinge's axis to each half's edge
Z0 = 0.12                     # the shell's underside (it stands on feet)
ZB = 1.50                     # the rims' tops, where the halves meet: the hinge's height
ZP = 1.00                     # the tray's floor
RIM = 0.55                    # how wide the rim is
R_OUT = 0.78                  # the shell's corners

PITCH_X, PITCH_Y = 2.15, 2.02
KEY_HW, KEY_HH = 0.89, 0.78   # a key's foot, at the floor
WELL = 0.06                   # the gap round a key
RX = GAP + W * 0.5            # the right half's centre
LX = -RX                      # the left half's centre
ROW0 = -H * 0.5 + RIM + 0.62 + KEY_HH   # the lowest row's y

random.seed(5)


# ---- materials

def principled(name, base, coat_rough=0.1, **more):
    return kit.principled(name, base, coat_rough=coat_rough, **more)


def grain(m, scale, distance, strength=1.0):
    """A moulding's fine texture, in the object's own coordinates."""
    return kit.grain(m, scale, distance, strength, detail=3.0, roughness=0.6)


def emission(name, colour, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n = m.node_tree.nodes
    b = n['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.0, 0.0, 0.0, 1.0)
    b.inputs['Emission Color'].default_value = (*colour, 1.0)
    b.inputs['Emission Strength'].default_value = strength
    return m


MAT.update({
    'shell': grain(principled('shell', (0.052, 0.048, 0.044), rough=0.60, spec=0.42), 90.0, 0.006, 0.6),
    'floor': grain(principled('floor', (0.028, 0.026, 0.024), rough=0.64, spec=0.40), 140.0, 0.004, 0.6),
    'pit': principled('pit', (0.004, 0.004, 0.004), rough=0.8),
    'rubber': principled('rubber', (0.010, 0.010, 0.010), rough=0.85),
    'steel': principled('steel', (0.62, 0.62, 0.60), rough=0.28, metallic=1.0),
    'glass': principled('glass', (0.62, 0.11, 0.08), rough=0.05, transmission=1.0),
    'solar': grain(principled('solar', (0.035, 0.022, 0.030), rough=0.22, spec=0.45), 300.0, 0.002, 0.4),
    'paper': grain(principled('paper', (0.98, 0.90, 0.72), rough=0.88, spec=0.3), 260.0, 0.004, 0.35),
    'rule': principled('rule', (0.34, 0.50, 0.62), rough=0.85),
    'ink': principled('ink', (0.018, 0.035, 0.12), rough=0.6),
    'lit': emission('lit', (1.0, 0.05, 0.025), 3.0),
    'unlit': emission('unlit', (1.0, 0.055, 0.03), 0.035),
})


# (paper is what is read: lit a little more than the light it is in would)
_pb = MAT['paper'].node_tree.nodes['Principled BSDF']
_pb.inputs['Emission Color'].default_value = (1.0, 0.93, 0.78, 1.0)
_pb.inputs['Emission Strength'].default_value = 0.10

# Legends: printed on the keys, so a picture on the key's colour, not a shape.
KEY_KINDS = {
    'ivory': ((0.74, 0.66, 0.47), (0.016, 0.015, 0.016)),
    'dark': ((0.050, 0.047, 0.043), (0.85, 0.80, 0.64)),
    'amber': ((0.72, 0.235, 0.028), (0.95, 0.90, 0.76)),
}
LEG_SIZE = 1.5                # the picture's side on the key, in cm
SANS = "font-family='Nimbus Sans' font-weight='700'"
RADICAL = ("<path d='M150 268 L188 250 L238 366 L300 134 L392 134' fill='none' stroke='#fff' "
           "stroke-width='24' stroke-linejoin='round' stroke-linecap='round'/>")


def legend_svg(s):
    """A key's legend, white on nothing, in a 512-square picture."""
    if s == 'sqrt':
        body = RADICAL
    elif s == 'pi':
        body = "<text x='256' y='326' text-anchor='middle' font-family='DejaVu Sans' font-weight='700' font-size='176'>\u03c0</text>"
    elif s == 'x2':
        body = (f"<text x='226' y='332' text-anchor='middle' {SANS} font-size='190'>x</text>"
                f"<text x='320' y='236' text-anchor='middle' {SANS} font-size='112'>2</text>")
    elif s == 'xy':
        body = (f"<text x='226' y='332' text-anchor='middle' {SANS} font-size='190'>x</text>"
                f"<text x='320' y='236' text-anchor='middle' {SANS} font-size='112'>y</text>")
    elif s == '1/x':
        body = f"<text x='256' y='318' text-anchor='middle' {SANS} font-size='150'>1/x</text>"
    elif s in ('sin', 'cos', 'tan', 'log', 'ln', 'n!'):
        body = f"<text x='256' y='312' text-anchor='middle' {SANS} font-size='146'>{s}</text>"
    elif s in ('FOLD', 'OPEN', 'OFF'):
        body = f"<text x='256' y='290' text-anchor='middle' {SANS} font-size='88' letter-spacing='6'>{s}</text>"
    elif s in ('MC', 'MR', 'M-', 'M+', 'AC'):
        t = s.replace('-', '−')
        body = f"<text x='256' y='310' text-anchor='middle' {SANS} font-size='132'>{t}</text>"
    elif s in ('+', '-', 'x', '/', '=', 'pm', '%'):
        t = {'-': '−', 'x': '×', '/': '÷', 'pm': '±'}.get(s, s)
        family = "font-family='DejaVu Sans' font-weight='700'" if s != '%' else SANS
        size = 196 if s != '%' else 168
        body = f"<text x='256' y='{326 if s != '%' else 318}' text-anchor='middle' {family} font-size='{size}'>{t}</text>"
    else:
        body = f"<text x='256' y='336' text-anchor='middle' {SANS} font-size='{204 if s != '00' else 168}'>{s}</text>"
    return ("<svg xmlns='http://www.w3.org/2000/svg' width='512' height='512' viewBox='0 0 512 512' "
            f"fill='#fff'>{body}</svg>")


def legend_image(s):
    return kit.legend_image(s, legend_svg(s))


def key_material(kind, s):
    """A key's plastic: a little of the light goes into it, its top more
    polished where the finger goes, the legend printed on."""
    base, ink = KEY_KINDS[kind]
    sss = 0.3 if kind != 'dark' else 0.0
    m = principled(f'key {kind} {s}', base, rough=0.42, coat=0.12, coat_rough=0.32, sss=sss)
    nt = m.node_tree
    n, ln = nt.nodes, nt.links
    b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord')
    obj = tc.outputs['Object']

    mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (1.0 / LEG_SIZE, 1.0 / LEG_SIZE, 1.0)
    mp.inputs['Location'].default_value = (0.5, 0.5, 0.0)
    ln.new(obj, mp.inputs['Vector'])
    tex = n.new('ShaderNodeTexImage')
    tex.image = legend_image(s)
    tex.extension = 'CLIP'
    ln.new(mp.outputs[0], tex.inputs['Vector'])
    mix = n.new('ShaderNodeMix')
    mix.data_type = 'RGBA'
    mix.inputs['A'].default_value = (*base, 1.0)
    mix.inputs['B'].default_value = (*ink, 1.0)
    ln.new(tex.outputs['Alpha'], mix.inputs['Factor'])
    ln.new(mix.outputs['Result'], b.inputs['Base Color'])
    # the ink lies on the plastic: duller, with no shine of the plastic's over it
    bare = n.new('ShaderNodeMath')
    bare.operation = 'SUBTRACT'
    bare.inputs[0].default_value = 1.0
    ln.new(tex.outputs['Alpha'], bare.inputs[1])
    for sock, much in (('Coat Weight', 0.12), ('Specular IOR Level', 0.42)):
        by = n.new('ShaderNodeMath')
        by.operation = 'MULTIPLY_ADD'
        by.inputs[1].default_value = much * 0.8
        by.inputs[2].default_value = much * 0.2
        ln.new(bare.outputs[0], by.inputs[0])
        ln.new(by.outputs[0], b.inputs[sock])
    if sss > 0.0:
        keep = n.new('ShaderNodeMath')
        keep.operation = 'SUBTRACT'
        keep.use_clamp = True
        keep.inputs[0].default_value = sss
        ln.new(tex.outputs['Alpha'], keep.inputs[1])
        ln.new(keep.outputs[0], b.inputs['Subsurface Weight'])

    # rubbed by use: smoother in the middle of the top, a little cloudy
    sep = n.new('ShaderNodeSeparateXYZ')
    ln.new(obj, sep.inputs[0])
    ln2 = n.new('ShaderNodeVectorMath')
    ln2.operation = 'LENGTH'
    comb = n.new('ShaderNodeCombineXYZ')
    ln.new(sep.outputs['X'], comb.inputs['X'])
    ln.new(sep.outputs['Y'], comb.inputs['Y'])
    ln.new(comb.outputs[0], ln2.inputs[0])
    rough = n.new('ShaderNodeMapRange')
    rough.inputs['From Min'].default_value = 0.1
    rough.inputs['From Max'].default_value = 0.72
    rough.inputs['To Min'].default_value = 0.34
    rough.inputs['To Max'].default_value = 0.50
    ln.new(ln2.outputs['Value'], rough.inputs['Value'])
    cloud = n.new('ShaderNodeTexNoise')
    cloud.inputs['Scale'].default_value = 3.0
    ln.new(obj, cloud.inputs['Vector'])
    add = n.new('ShaderNodeMath')
    add.operation = 'MULTIPLY_ADD'
    add.inputs[1].default_value = 0.10
    add.inputs[2].default_value = -0.05
    ln.new(cloud.outputs[0], add.inputs[0])
    tot = n.new('ShaderNodeMath')
    tot.operation = 'ADD'
    ln.new(rough.outputs[0], tot.inputs[0])
    ln.new(add.outputs[0], tot.inputs[1])
    ln.new(tot.outputs[0], b.inputs['Roughness'])

    fine = n.new('ShaderNodeTexNoise')
    fine.inputs['Scale'].default_value = 240.0
    fine.inputs['Detail'].default_value = 3.0
    ln.new(obj, fine.inputs['Vector'])
    bump = n.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.25
    bump.inputs['Distance'].default_value = 0.0025
    ln.new(fine.outputs[0], bump.inputs['Height'])
    ln.new(bump.outputs['Normal'], b.inputs['Normal'])
    ln.new(bump.outputs['Normal'], b.inputs['Coat Normal'])
    return m


# ---- shapes

def arc(ci, cz, r, a0, a1, n=8, sign=1.0):
    """Points (inset, z) on a circle about (ci, cz), degrees; angle 0 is
    towards the inside of the outline when sign is 1."""
    out = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        out.append((ci + sign * r * math.cos(a), cz + r * math.sin(a)))
    return out


def shell_rings(cx, profile):
    return [[(x, y, z) for x, y in rrect(W * 0.5 - i, H * 0.5 - i, R_OUT - i * 0.62, 12, cx, 0.0)]
            for i, z in profile]


def shell(name, cx, z0=Z0, zb=ZB, zp=ZP):
    """A half: underside, the wall round it, the rim, down into the tray."""
    rb, rt, ri, rf = 0.10, 0.14, 0.09, 0.10
    p = [(rb + 0.02, z0)]
    p += arc(rb, z0 + rb, rb, -90.0, 0.0, sign=-1.0)
    p += [(0.02, (z0 + zb) * 0.5)]
    p += arc(rt + 0.03, zb - rt, rt, 0.0, 90.0, sign=-1.0)
    p += [(RIM - ri, zb)]
    p += arc(RIM - ri, zb - ri, ri, 90.0, 0.0)
    p += [(RIM + 0.04, zp + rf)]
    p += arc(RIM + 0.04 + rf, zp + rf, rf, 180.0, 270.0)
    p += [(RIM + 0.04 + rf + 0.04, zp + 0.0006)]
    return loft(name, shell_rings(cx, p), 'shell')


def extruded(name, outlines, z0, z1, mat):
    """Outlines (not touching) made solid between z0 and z1, one object."""
    bm = bmesh.new()
    for pts in outlines:
        lo = [bm.verts.new((x, y, z0)) for x, y in pts]
        hi = [bm.verts.new((x, y, z1)) for x, y in pts]
        bm.faces.new(list(reversed(lo)))
        bm.faces.new(hi)
        n = len(pts)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat, sharp=35.0)


def tray_floor(name, cx, holes, zp=ZP, deep=0.52):
    """The tray's floor with its wells cut, their lips softly chamfered."""
    hw = W * 0.5 - RIM - 0.10
    hh = H * 0.5 - RIM - 0.10
    o = extruded(name, [rrect(hw, hh, 0.3, 8, cx, 0.0)], zp - deep, zp, 'floor')
    cutter = extruded(name + ' wells', holes, zp - deep - 0.08, zp + 0.1, 'pit')
    cutter.hide_render = True
    cutter.hide_viewport = True
    m = o.modifiers.new('wells', 'BOOLEAN')
    m.operation = 'DIFFERENCE'
    m.solver = 'EXACT'
    m.object = cutter
    b = o.modifiers.new('lips', 'BEVEL')
    b.width = 0.028
    b.segments = 3
    b.limit_method = 'ANGLE'
    b.angle_limit = math.radians(40.0)
    b.harden_normals = True
    w = o.modifiers.new('normals', 'WEIGHTED_NORMAL')
    w.keep_sharp = True
    return o


def key(name, x, y, kind, legend, hw=KEY_HW, hh=KEY_HH, top=0.46, z=ZP, corner=0.31):
    """A key: its foot down in the well, sides sloping in to a round shoulder,
    the top dished. The origin is where the key's middle meets the floor.
    top is how far the top stands over the floor."""
    sh = min(0.17, top * 0.4)  # the shoulder's radius
    prof = [(0.0, -0.40), (0.0, -0.04), (0.012, 0.05), (0.078, top - sh * 0.95)]
    prof += arc(0.078 + sh * 0.97, top - sh, sh, 172.0, 96.0, n=14, sign=1.0)
    rings = []
    last_i = 0.0
    for i, zz in prof:
        last_i = max(i, last_i)
        r = max(corner - last_i * 0.30, 0.02)
        rings.append([(px, py, zz) for px, py in rrect(hw - last_i, hh - last_i, r, 8)])
    # the dish: the top ring drawn in to the middle, falling a little
    edge = rings[-1]
    dish = min(0.055, top * 0.2)
    steps = 24
    ex = max(p[0] for p in edge)
    ey = max(p[1] for p in edge)
    def sunk(px, py):
        return top - dish * (1.0 - (px / ex) ** 2) * (1.0 - (py / ey) ** 2)
    for k in range(1, steps + 1):
        t = 1.0 - k / (steps + 1)
        rings.append([(px * t, py * t, sunk(px * t, py * t)) for px, py, _ in edge])
    o = loft(name, rings, key_material(kind, legend), centre_last=(0.0, 0.0, top - dish))
    # no two keys were ever set quite alike
    o.location = (x + random.uniform(-0.006, 0.006), y + random.uniform(-0.006, 0.006),
                  z + random.uniform(-0.006, 0.006))
    o['top'] = top
    o.rotation_euler = (math.radians(random.uniform(-0.35, 0.35)),
                        math.radians(random.uniform(-0.35, 0.35)),
                        math.radians(random.uniform(-0.25, 0.25)))
    return o


def key_field(prefix, cx, rows, y0, pitch_y=PITCH_Y, z=ZP, top=0.46):
    """Keys by rows, the lowest first; their wells' outlines."""
    holes = []
    keys = []
    for r, row in enumerate(rows):
        for c, (legend, kind) in enumerate(row):
            x = cx + (c - (len(row) - 1) * 0.5) * PITCH_X
            y = y0 + r * pitch_y
            keys.append(key(f'{prefix} {legend}', x, y, kind, legend, top=top, z=z))
            holes.append(rrect(KEY_HW + WELL, KEY_HH + WELL, 0.31, 8, x, y))
    return keys, holes


# the two small keys at the head of each face: the one shuts or opens it, the
# other puts it away
PILL_HW, PILL_HH, PILL_Y = 0.62, 0.25, 7.54
PILL_X = (1.90, 3.45)


def pills(prefix, cx, words, z):
    keys, holes = [], []
    for dx, word in zip(PILL_X, words):
        keys.append(key(f'{prefix} {word}', cx + dx, PILL_Y, 'dark', word, hw=PILL_HW, hh=PILL_HH,
                        top=0.24, z=z, corner=0.25))
        holes.append(rrect(PILL_HW + WELL, PILL_HH + WELL, 0.25 + WELL, 8, cx + dx, PILL_Y))
    return keys, holes


MAIN = [
    [('0', 'ivory'), ('00', 'ivory'), ('.', 'ivory'), ('=', 'amber')],
    [('1', 'ivory'), ('2', 'ivory'), ('3', 'ivory'), ('+', 'amber')],
    [('4', 'ivory'), ('5', 'ivory'), ('6', 'ivory'), ('-', 'amber')],
    [('7', 'ivory'), ('8', 'ivory'), ('9', 'ivory'), ('x', 'amber')],
    [('AC', 'dark'), ('pm', 'dark'), ('%', 'dark'), ('/', 'amber')],
    [('MC', 'dark'), ('MR', 'dark'), ('M-', 'dark'), ('M+', 'dark')],
]
SPECIAL = [
    [('sin', 'dark'), ('cos', 'dark'), ('tan', 'dark'), ('n!', 'dark')],
    [('pi', 'dark'), ('e', 'dark'), ('ln', 'dark'), ('log', 'dark')],
    [('sqrt', 'dark'), ('x2', 'dark'), ('xy', 'dark'), ('1/x', 'dark')],
]

# ---- the right half: the display and the keys, and at its head the two small keys
parts_right = [shell('right shell', RX)]
keys, holes = key_field('key', RX, MAIN, ROW0)
pill_keys, pill_holes = pills('key', RX, ('FOLD', 'OFF'), ZP)
keys += pill_keys
holes += pill_holes

DISP_Y = 5.62
DISP_HW, DISP_HH = 4.10, 1.30
holes.append(rrect(DISP_HW, DISP_HH, 0.14, 6, RX, DISP_Y))
parts_right.append(tray_floor('right floor', RX, holes))
parts_right.append(extruded('right pit', [rrect(W * 0.5 - RIM, H * 0.5 - RIM, 0.3, 8, RX, 0.0)],
                            Z0 + 0.05, ZP - 0.42, 'pit'))
# the display's glass, just under the floor, and what is under the glass
glass = extruded('glass', [rrect(DISP_HW - 0.01, DISP_HH - 0.01, 0.13, 6, RX, DISP_Y)],
                 ZP - 0.07, ZP - 0.035, 'glass')

SEGS = {
    '0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc', '5': 'afgcd',
    '6': 'afgedc', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg', '-': 'g', ' ': '',
}


def segment(which, x, y, w, h, t):
    """A figure's segment, as a six-sided plate. (x, y) the figure's lower left."""
    g = t * 0.12              # the gap between segments
    s = t * 0.5
    def horiz(y1):
        return [(x + g, y1), (x + g + s, y1 + s), (x + w - g - s, y1 + s), (x + w - g, y1),
                (x + w - g - s, y1 - s), (x + g + s, y1 - s)]
    def vert(x1, y0, y2):
        return [(x1, y0 + g), (x1 + s, y0 + g + s), (x1 + s, y2 - g - s), (x1, y2 - g),
                (x1 - s, y2 - g - s), (x1 - s, y0 + g + s)]
    m = h * 0.5
    return {
        'a': horiz(y + h), 'g': horiz(y + m), 'd': horiz(y),
        'f': vert(x, y + m, y + h), 'e': vert(x, y, y + m),
        'b': vert(x + w, y + m, y + h), 'c': vert(x + w, y, y + m),
    }[which]


FIG_W, FIG_H, FIG_T, FIG_PITCH, FIG_SLANT = 0.41, 1.40, 0.108, 0.66, 0.06


def figures(text, name, z, cx, cy, mat_on, mat_off):
    """Twelve figures, the text to the right, the off segments faintly there."""
    DIGITS = 12
    w, h, t, pitch = FIG_W, FIG_H, FIG_T, FIG_PITCH
    slant = FIG_SLANT
    chars = []
    for ch in text:
        if ch == '.' and chars:
            chars[-1] = (chars[-1][0], True)
        else:
            chars.append((ch, False))
    chars = [(' ', False)] * (DIGITS - len(chars)) + chars
    on, off = bmesh.new(), bmesh.new()
    x0 = cx - (DIGITS - 1) * pitch * 0.5 - w * 0.5
    y0 = cy - h * 0.5
    for i, (ch, dot) in enumerate(chars):
        x = x0 + i * pitch
        for sg in 'abcdefg':
            pts = segment(sg, x, y0, w, h, t)
            bm = on if sg in SEGS[ch] else off
            bm.faces.new([bm.verts.new((px + (py - y0) * slant, py, z)) for px, py in pts])
        bm = on if dot else off
        px, py, s = x + w + 0.125, y0 + 0.05, 0.065
        bm.faces.new([bm.verts.new((px + dx, py + dy, z))
                      for dx, dy in ((-s, -s), (s, -s), (s, s), (-s, s))])
    put(name + ' on', on, mat_on, smooth=False)
    put(name + ' off', off, mat_off, smooth=False)


# (its dots are DEsk's to draw, lit and unlit: only the glass and the dark under it here)
extruded('display floor', [rrect(DISP_HW + 0.05, DISP_HH + 0.05, 0.1, 6, RX, DISP_Y)],
         ZP - 0.6, ZP - 0.37, 'pit')

# the feet the right half stands on
for sx in (-1, 1):
    for sy in (-1, 1):
        extruded('foot', [[(RX + sx * 4.2 + 0.5 * math.cos(a * math.pi / 12),
                            sy * 7.6 + 0.5 * math.sin(a * math.pi / 12)) for a in range(24)]],
                 0.0, Z0 + 0.03, 'rubber')

# ---- the left half, the lid: built lying open, hung on the hinge afterwards
lid_parts = [shell('lid shell', LX)]
lid_keys, lid_holes = key_field('lid key', LX, SPECIAL, ROW0)

PAD_Y0 = ROW0 + 3 * PITCH_Y - 0.48
PAD_Y1 = H * 0.5 - RIM - 0.52
PAD_CY = (PAD_Y0 + PAD_Y1) * 0.5
PAD_HW, PAD_HH = 4.14, (PAD_Y1 - PAD_Y0) * 0.5
lid_holes.append(rrect(PAD_HW + 0.04, PAD_HH + 0.04, 0.10, 6, LX, PAD_CY))
lid_parts.append(tray_floor('lid floor', LX, lid_holes))
lid_parts.append(extruded('lid pit', [rrect(W * 0.5 - RIM, H * 0.5 - RIM, 0.3, 8, LX, 0.0)],
                          Z0 + 0.05, ZP - 0.42, 'pit'))

# the pad of paper in its recess: leaves not quite square on each other
for i in range(7):
    dx, dy, tw = random.uniform(-0.012, 0.012), random.uniform(-0.012, 0.012), random.uniform(-0.05, 0.05)
    z = ZP - 0.34 + i * 0.038
    o = extruded(f'leaf {i}', [rrect(PAD_HW - 0.03, PAD_HH - 0.03, 0.025, 2, 0.0, 0.0)], z - 0.03, z, 'paper')
    o.location = (LX + dx, PAD_CY + dy, 0.0)
    o.rotation_euler = (0.0, 0.0, math.radians(tw))
    lid_parts.append(o)
PAPER_Z = ZP - 0.34 + 6 * 0.038 + 0.0015

# the corners that hold the pad in
for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
    s = 0.42
    cx, cy = LX + sx * (PAD_HW + 0.05), PAD_CY + sy * (PAD_HH + 0.05)
    tri = [(cx, cy), (cx - sx * s, cy), (cx, cy - sy * s)]
    if sx * sy < 0:
        tri.reverse()
    o = extruded('corner', [tri], PAPER_Z - 0.02, ZP + 0.03, 'floor')
    b = o.modifiers.new('soft', 'BEVEL')
    b.width = 0.022
    b.segments = 3
    b.limit_method = 'ANGLE'
    lid_parts.append(o)

# what has been worked out (draw.c writes this; here only to see it)
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'
font = bpy.data.fonts.load(MONO)
RULE_P = 0.92
top = PAD_CY + PAD_HH - 0.72
rows = [('1,250 + 380', '1,630'), ('1,630 × 2', '3,260'), ('3,260 − 60', '3,200'),
        ('√ 3,200', '56.568542'), ('56.568542 × 8', '452.54834'), ('sin 30', '0.5')]
n_rules = int((PAD_HH * 2.0 - 0.4) / RULE_P)
PAPER_TOP = top
for i in range(n_rules):
    y = top - i * RULE_P - 0.16
    me = extruded('rule', [[(LX - PAD_HW + 0.02, y), (LX + PAD_HW - 0.02, y),
                            (LX + PAD_HW - 0.02, y + 0.018), (LX - PAD_HW + 0.02, y + 0.018)]],
                  PAPER_Z, PAPER_Z + 0.0008, 'rule')
    lid_parts.append(me)


def write(name, s, x, y, size, align):
    cu = bpy.data.curves.new(name, 'FONT')
    cu.body = s
    cu.size = size
    cu.font = font
    cu.align_x = align
    cu.materials.append(MAT['ink'])
    o = bpy.data.objects.new(name, cu)
    COLL.objects.link(o)
    o.location = (x, y, PAPER_Z + 0.0015)
    return o


for i, (a, b) in enumerate(() if BAKE else rows):
    y = top - i * RULE_P
    lid_parts.append(write('sum', a, LX - PAD_HW + 0.34, y, 0.50, 'LEFT'))
    lid_parts.append(write('answer', '= ' + b, LX + PAD_HW - 0.34, y, 0.50, 'RIGHT'))

# ---- the hinge: knuckles along the middle, by turns the lid's and the body's
pivot = bpy.data.objects.new('pivot', None)
COLL.objects.link(pivot)
pivot.location = (0.0, 0.0, ZB)
KN, KN_GAP = 7, 0.05
KN_R = 0.235
span = H - 2.2
kn_len = (span - (KN - 1) * KN_GAP) / KN


def barrel(name, y0, y1, mat, r=KN_R):
    pts = [(r * math.cos(2 * math.pi * i / 40), r * math.sin(2 * math.pi * i / 40)) for i in range(40)]
    bm = bmesh.new()
    lo = [bm.verts.new((x, y0, ZB + z)) for x, z in pts]
    hi = [bm.verts.new((x, y1, ZB + z)) for x, z in pts]
    bm.faces.new(lo)
    bm.faces.new(list(reversed(hi)))
    for i in range(40):
        j = (i + 1) % 40
        bm.faces.new((lo[i], hi[i], hi[j], lo[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = put(name, bm, mat, sharp=35.0)
    b = o.modifiers.new('soft', 'BEVEL')
    b.width = 0.03
    b.segments = 3
    b.limit_method = 'ANGLE'
    b.angle_limit = math.radians(50.0)
    return o


for i in range(KN):
    y0 = -span * 0.5 + i * (kn_len + KN_GAP)
    kn = barrel(f'knuckle {i}', y0, y0 + kn_len, 'shell')
    # the web from the knuckle to its half
    side = 1 if i % 2 == 0 else -1
    x0, x1 = (0.0, GAP + 0.15) if side > 0 else (-GAP - 0.15, 0.0)
    web = extruded('web', [[(x0, y0), (x1, y0), (x1, y0 + kn_len), (x0, y0 + kn_len)]],
                   Z0 + 0.25, ZB - 0.02, 'shell')
    if side < 0:
        lid_parts += [kn, web]
for sy in (-1, 1):
    barrel('pin', sy * (span * 0.5 + 0.0) - 0.05, sy * (span * 0.5 + 0.0) + 0.05, 'steel', 0.09)

# (what cuts the wells in the lid's floor goes with it, or they stay behind)
lid_cutters = [o.modifiers['wells'].object for o in lid_parts if 'wells' in o.modifiers]
for o in lid_parts + lid_keys + lid_cutters:
    o.parent = pivot
    o.matrix_parent_inverse = Matrix.Translation(-pivot.location)

# ---- the lid's outside, a calculator of its own: built as it lies shut over
# the right half, a shallower tray on the lid's back with a display of figures,
# the keys for a sum, the two small keys and a strip of solar cells
ZS = 2.0 * ZB - Z0            # the lid's back, shut
OZB, OZP = ZS + 0.80, ZS + 0.42
OUTER = [
    [('0', 'ivory'), ('00', 'ivory'), ('.', 'ivory'), ('=', 'amber')],
    [('1', 'ivory'), ('2', 'ivory'), ('3', 'ivory'), ('+', 'amber')],
    [('4', 'ivory'), ('5', 'ivory'), ('6', 'ivory'), ('-', 'amber')],
    [('7', 'ivory'), ('8', 'ivory'), ('9', 'ivory'), ('x', 'amber')],
    [('AC', 'dark'), ('pm', 'dark'), ('%', 'dark'), ('/', 'amber')],
]
OUT_ROW0, OUT_PITCH_Y = -6.75, 2.30
ODISP_Y, ODISP_HW, ODISP_HH = 4.90, 4.10, 1.15
SOLAR_X, SOLAR_HW, SOLAR_HH = -2.45, 1.85, 0.30
outer_parts = [shell('lid outer shell', RX, ZS, OZB, OZP)]
outer_keys, outer_holes = key_field('outer key', RX, OUTER, OUT_ROW0, OUT_PITCH_Y, OZP, 0.34)
opills, opill_holes = pills('outer key', RX, ('OPEN', 'OFF'), OZP)
outer_keys += opills
outer_holes += opill_holes
outer_holes.append(rrect(ODISP_HW, ODISP_HH, 0.14, 6, RX, ODISP_Y))
outer_holes.append(rrect(SOLAR_HW + 0.04, SOLAR_HH + 0.04, 0.06, 4, RX + SOLAR_X, PILL_Y))
outer_parts.append(tray_floor('lid outer floor', RX, outer_holes, OZP, 0.30))
outer_parts.append(extruded('lid outer pit', [rrect(W * 0.5 - RIM, H * 0.5 - RIM, 0.3, 8, RX, 0.0)],
                            ZS + 0.02, OZP - 0.31, 'pit'))
outer_parts.append(extruded('outer glass', [rrect(ODISP_HW - 0.01, ODISP_HH - 0.01, 0.13, 6, RX, ODISP_Y)],
                            OZP - 0.07, OZP - 0.035, 'glass'))
figures('' if BAKE else arg('--show', '3200.'), 'outer figures', OZP - 0.27, RX, ODISP_Y, 'lit', 'unlit')
outer_parts += [o for o in bpy.data.objects if o.name.startswith('outer figures')]
outer_parts.append(extruded('outer display floor', [rrect(ODISP_HW + 0.05, ODISP_HH + 0.05, 0.1, 6, RX, ODISP_Y)],
                            OZP - 0.33, OZP - 0.28, 'pit'))
# the solar cells: dark glass, a little purple in the light, in strips
outer_parts.append(extruded('solar', [rrect(SOLAR_HW, SOLAR_HH, 0.04, 4, RX + SOLAR_X, PILL_Y)],
                            OZP - 0.08, OZP - 0.03, 'solar'))
for i in range(1, 5):
    sx = RX + SOLAR_X - SOLAR_HW + i * SOLAR_HW * 2.0 / 5.0
    outer_parts.append(extruded('solar line', [[(sx - 0.012, PILL_Y - SOLAR_HH + 0.03), (sx + 0.012, PILL_Y - SOLAR_HH + 0.03),
                                                 (sx + 0.012, PILL_Y + SOLAR_HH - 0.03), (sx - 0.012, PILL_Y + SOLAR_HH - 0.03)]],
                                OZP - 0.03, OZP - 0.028, 'steel'))
outer_cutters = [o.modifiers['wells'].object for o in outer_parts if 'wells' in o.modifiers]
pivot.rotation_euler = (0.0, math.pi, 0.0)
bpy.context.view_layer.update()
to_open = pivot.matrix_world.inverted()
for o in outer_parts + outer_keys + outer_cutters:
    o.parent = pivot
    o.matrix_parent_inverse = to_open
pivot.rotation_euler = (0.0, math.radians(ANGLE), 0.0)

# ---- the desk under it: only its shadow and what it throws back
bm = bmesh.new()
for x, y in ((-200, -200), (200, -200), (200, 200), (-200, 200)):
    bm.verts.new((x, y, 0.0))
bm.faces.new(bm.verts)
desk = put('desk', bm, principled('desk', (0.20, 0.105, 0.052), rough=0.45, spec=0.35), smooth=False)
desk.is_shadow_catcher = True


# ---- the light: the desk's lamp, up and to the left, and soft boxes for the
# plastic to have something to show
area = kit.area

MID = (0.0, 0.0, 1.0)
area('lamp', (-26.0, 18.0, 40.0), MID, 22.0, 22.0, 11.0, (1.0, 0.93, 0.82))
area('over', (-8.0, 10.0, 80.0), MID, 46.0, 20.0, 1.0, (1.0, 0.98, 0.95))
s = area('streak', (5.5, 25.0, 80.0), MID, 2.2, 40.0, 0.05, (1.0, 0.98, 0.95))
s.rotation_euler = (s.rotation_euler.to_matrix() @ Matrix.Rotation(math.radians(32.0), 3, 'Z')).to_euler()
area('back', (6.0, 62.0, 60.0), MID, 90.0, 26.0, 0.55, (1.0, 0.98, 0.95))
area('strip', (40.0, 10.0, 34.0), MID, 8.0, 50.0, 1.4, (0.96, 0.98, 1.0))
area('room', (0.0, -60.0, 50.0), MID, 90.0, 40.0, 0.42, (1.0, 0.97, 0.92))

kit.room((0.05, 0.043, 0.036))

# ---- the eye: a long way off, a little to the front, so that the keys' near
# sides show
TILT = math.radians(13.0)
DIST = 140.0
PX_PER_CM, TARGET, RES = {
    'desk': (26.0, (0.0 if STATE != 'shut' else RX, 0.0, 1.0), (1440, 900)),
    'keys': (118.0, (RX, -1.8, 1.0), (1400, 1000)),
    'disp': (118.0, (RX, 4.6, 1.0), (1400, 1000)),
    'lid': (118.0, (LX, 0.6, 1.0), (1400, 1000)),
}[VIEW]
cam_data = bpy.data.cameras.new('eye')
cam_data.sensor_fit = 'HORIZONTAL'
cam_data.sensor_width = 36.0
cam_data.lens = 36.0 * DIST / (RES[0] / PX_PER_CM)
cam_data.clip_start = 1.0
cam_data.clip_end = 600.0
cam = bpy.data.objects.new('eye', cam_data)
COLL.objects.link(cam)
SCENE.camera = cam
t = Vector(TARGET)
cam.location = t + Vector((0.0, -math.sin(TILT), math.cos(TILT))) * DIST
cam.rotation_euler = (t - cam.location).to_track_quat('-Z', 'Y').to_euler()

kit.film(SAMPLES, exposure=0.85, filter_size=None, bounces=10, glossy=6, transmission=8)
r, c = kit.r, kit.c
r.resolution_x, r.resolution_y = RES[0], RES[1]
r.filepath = os.path.join(OUT, f'calc-{STATE}-{VIEW}.png')
SCENE.view_settings.view_transform = 'AgX'
for look_name in ('AgX - Punchy', 'Punchy'):
    try:
        SCENE.view_settings.look = look_name
        break
    except TypeError:
        pass
if not BAKE:
    bpy.ops.render.render(write_still=True)
    print('wrote', r.filepath)
    sys.exit(0)

# ---- what DEsk lays it from: all through one eye, BAKE_PX pixels to the
# centimetre where the calculator is, with room over it for the lid on its way
BAKE_PX = 72
CANVAS = 1860
FILM_N = 15                   # the pictures of the lid between shut and open
FILM_PERCENT = 33             # and how big they are: as fine as the screen shows it as it comes
PRESS = 0.32                  # how far a key goes down: most of the way into its well
PRESS_SHADE = 0.78            # how much of its colour it has then
FEATHER = 4                   # a piece's edge, faded into the picture under it

t = Vector((0.35, 1.0, 1.0))
cam_data.lens = 36.0 * DIST / (CANVAS / BAKE_PX)
cam.location = t + Vector((0.0, -math.sin(TILT), math.cos(TILT))) * DIST
cam.rotation_euler = (t - cam.location).to_track_quat('-Z', 'Y').to_euler()
r.resolution_x = r.resolution_y = CANVAS
r.resolution_percentage = 100
r.use_persistent_data = True
bpy.context.view_layer.update()
TMP = os.path.join(OUT, 'tmp')
os.makedirs(TMP, exist_ok=True)


def px(x, y, z):
    """Where a point is in the canvas, in its pixels from the top left."""
    u = world_to_camera_view(SCENE, cam, Vector((x, y, z)))
    return u.x * CANVAS, (1.0 - u.y) * CANVAS


def bounds(points):
    xs, ys = zip(*(px(*p) for p in points))
    return [int(math.floor(min(xs))), int(math.floor(min(ys))),
            int(math.ceil(max(xs))), int(math.ceil(max(ys)))]


def lid_to(angle):
    pivot.rotation_euler = (0.0, math.radians(angle), 0.0)
    bpy.context.view_layer.update()


def shoot(name, samples, box=None, percent=100):
    """A picture (or just box of it, x0, y0, x1, y1, in the canvas), as rows
    from the top, 0..255, straight alpha."""
    c.samples = samples
    r.resolution_percentage = percent
    r.use_border = box is not None
    r.use_crop_to_border = box is not None
    if box:
        r.border_min_x, r.border_max_x = box[0] / CANVAS, box[2] / CANVAS
        r.border_min_y, r.border_max_y = 1.0 - box[3] / CANVAS, 1.0 - box[1] / CANVAS
    path = os.path.join(TMP, name + '.png')
    r.filepath = path
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(path, check_existing=False)
    w, h = im.size
    a = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    return np.flipud((a.reshape(h, w, 4) * 255.0 + 0.5).clip(0, 255).astype(np.uint8))


save = kit.save


def trimmed(img, fade=64):
    """img without its empty edges, and where what is left was. The far edge of
    the shadow goes: the large soft lights reach further than the canvas does,
    and it must not stop in a straight line on the desk."""
    img = img.copy()
    a = (img[:, :, 3].astype(np.float32) - 6.0).clip(0.0, 249.0) * 255.0 / 249.0
    h, w = a.shape
    rx = np.minimum(np.arange(w), np.arange(w)[::-1]) / float(fade)
    ry = np.minimum(np.arange(h), np.arange(h)[::-1]) / float(fade)
    rx, ry = rx.clip(0.0, 1.0), ry.clip(0.0, 1.0)
    img[:, :, 3] = (a * np.outer(ry * ry * (3.0 - 2.0 * ry), rx * rx * (3.0 - 2.0 * rx)) + 0.5).astype(np.uint8)
    ys, xs = np.nonzero(img[:, :, 3] > 0)
    x0, x1, y0, y1 = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
    return img[y0:y1, x0:x1], x0, y0


samples_rest = 12 if QUICK else 192
samples_key = 12 if QUICK else 192
samples_film = 8 if QUICK else 64

# the lid on its way, a third of the size, and where those go (calc-film.h)
def film():
    lines = []
    for i in range(FILM_N):
        angle = 180.0 * (FILM_N - i) / (FILM_N + 1)
        lid_to(angle)
        img, x, y = trimmed(shoot('film', samples_film, None, FILM_PERCENT), 32)
        save('calc-lid-%02d.png' % i, img)
        lines.append('\t{ %d, %d, %d, %d },' % (x, y, img.shape[1], img.shape[0]))
        print('calc.py: lid', i, flush=True)
    film_px = CANVAS * FILM_PERCENT // 100
    with open(os.path.join(OUT, 'calc-film.h'), 'w') as f:
        f.write('\n'.join([
            '/* calc-film.h: made by calc.py with the pictures -- do not edit, make again.',
            ' * The lid on its way, from nearly shut to nearly open (calc-lid-NN.png): how many of',
            ' * the canvas\'s pixels are one of theirs, and where each picture goes in theirs. */',
            '',
            '#define CALC_FILM_N %d' % FILM_N,
            '#define CALC_FILM_SCALE %.5f' % (CANVAS / film_px),
            'static const short CALC_FILM[CALC_FILM_N][4] = {',
            *lines,
            '};',
            '',
        ]))


if arg('--only', None) == 'film':
    film()
    print('calc.py: done', flush=True)
    sys.exit(0)


lid_to(0.0)
img, open_x, open_y = trimmed(shoot('open', samples_rest))
open_h, open_w = img.shape[:2]
save('calc-open.png', img)
print('calc.py: open', open_w, open_h, flush=True)
base = None

# every key held down: a piece of the picture round it
KEY_ENUM = {'.': 'DOT', '=': 'EQ', '+': 'ADD', '-': 'SUB', 'x': 'MUL', '/': 'DIV', 'pm': 'NEG', '%': 'PCT',
            'M-': 'MMINUS', 'M+': 'MPLUS', '1/x': 'INV', 'n!': 'FACT', 'x2': 'SQ', 'xy': 'POW', 'e': 'EUL',
            '00': 'DBLZERO', 'FOLD': 'FOLD', 'OPEN': 'FOLD', 'OFF': 'OFF'}
pieces = []
# the keys of the inside, open, and of the outside, shut (face 0 and 1)
all_keys = [(o.name, o, 0) for o in keys + lid_keys] + [(o.name, o, 1) for o in outer_keys]
for name, o, face in all_keys:
    legend = name.split(' ')[-1]
    lid_to(180.0 if face else 0.0)
    wx, wy, wz = (o.matrix_world @ Vector((0.0, 0.0, 0.0)))[:]
    small = legend in ('FOLD', 'OPEN', 'OFF')
    hw, hh = (PILL_HW + WELL, PILL_HH + WELL) if small else (KEY_HW + WELL, KEY_HH + WELL)
    top = o['top']
    hit = bounds([(wx + sx * hw, wy + sy * hh, wz + z) for sx in (-1, 1) for sy in (-1, 1) for z in (0.0, top)])
    m = FEATHER + 4
    box = [hit[0] - m, hit[1] - m, hit[2] + m, hit[3] + m]
    z0 = o.location.z
    # (down most of the way into its well; the outside's are the other way up)
    press = min(PRESS, top * 0.7)
    o.location.z = z0 - press
    # (and in the shade of what is pressing it: from nearly straight above, how
    # far it goes down hardly shows)
    mix = next(n for n in o.data.materials[0].node_tree.nodes if n.type == 'MIX')
    was = tuple(mix.inputs['A'].default_value)
    mix.inputs['A'].default_value = (was[0] * PRESS_SHADE, was[1] * PRESS_SHADE, was[2] * PRESS_SHADE, 1.0)
    piece = shoot('key', samples_key, box)
    mix.inputs['A'].default_value = was
    o.location.z = z0
    h, w = piece.shape[:2]
    # faded in from its edges, so that it cannot be seen where it stops
    ramp = np.minimum.outer(np.minimum(np.arange(h), np.arange(h)[::-1]),
                            np.minimum(np.arange(w), np.arange(w)[::-1]))
    piece = piece.copy()
    piece[:, :, 3] = (np.clip(ramp / FEATHER, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    pieces.append((legend, hit, box, piece, face))
    print('calc.py: key', legend, face, flush=True)
lid_to(0.0)

cols = 8
pw = max(p[3].shape[1] for p in pieces) + 2
ph = max(p[3].shape[0] for p in pieces) + 2
rows_n = (len(pieces) + cols - 1) // cols
sheet = np.zeros((rows_n * ph, cols * pw, 4), dtype=np.uint8)
key_lines = []
for i, (legend, hit, box, piece, face) in enumerate(pieces):
    ax, ay = (i % cols) * pw, (i // cols) * ph
    h, w = piece.shape[:2]
    sheet[ay:ay + h, ax:ax + w] = piece
    en = KEY_ENUM.get(legend, legend.upper())
    key_lines.append('\t{ DESK_CK_%s, %d, { %d, %d, %d, %d }, { %d, %d, %d, %d }, %d, %d },'
                     % (en, face, *hit, box[0], box[1], w, h, ax, ay))
save('calc-keys.png', sheet)

lid_to(180.0)
img, shut_x, shut_y = trimmed(shoot('shut', samples_rest))
shut_h, shut_w = img.shape[:2]
save('calc-shut.png', img)
print('calc.py: shut', shut_w, shut_h, flush=True)

# the lid on its way
film()

# and where it all is
lid_to(0.0)
xr0, xr1 = GAP, GAP + W
xl0, xl1 = -GAP - W, -GAP
yy0, yy1 = -H * 0.5, H * 0.5
open_box = bounds([(x, y, z) for x in (xl0, xr1) for y in (yy0, yy1) for z in (Z0, ZB)])
shut_box = bounds([(x, y, z) for x in (xr0, xr1) for y in (yy0, yy1) for z in (Z0, OZB)])
right_box = bounds([(x, y, z) for x in (xr0, xr1) for y in (yy0, yy1) for z in (Z0, ZB)])
lid_box = bounds([(x, y, z) for x in (xl0, xl1) for y in (yy0, yy1) for z in (Z0, ZB)])
tray_box = bounds([(x, y, ZP) for x in (xl0 + RIM, xl1 - RIM) for y in (yy0 + RIM, yy1 - RIM)])
hinge_box = bounds([(x, y, z) for x in (-GAP, GAP) for y in (yy0, yy1) for z in (Z0, ZB + KN_R)])
disp_box = bounds([(RX + sx * DISP_HW, DISP_Y + sy * DISP_HH, ZP) for sx in (-1, 1) for sy in (-1, 1)])
paper_box = bounds([(LX + sx * PAD_HW, PAD_CY + sy * PAD_HH, PAPER_Z) for sx in (-1, 1) for sy in (-1, 1)])
# (the outside's figures, as it lies shut: they were made where they are then)
z = OZP - 0.27
fx0 = RX - 11 * FIG_PITCH * 0.5 - FIG_W * 0.5
p00, p01 = px(fx0, ODISP_Y - FIG_H * 0.5, z), px(fx0, ODISP_Y + FIG_H * 0.5, z)
p10 = px(fx0 + 11 * FIG_PITCH, ODISP_Y - FIG_H * 0.5, z)
odisp_box = bounds([(RX + sx * ODISP_HW, ODISP_Y + sy * ODISP_HH, OZP) for sx in (-1, 1) for sy in (-1, 1)])
rules = [px(LX, PAPER_TOP - i * RULE_P - 0.16, PAPER_Z)[1] for i in range(n_rules)]
text_x0 = px(LX - PAD_HW + 0.34, PAD_CY, PAPER_Z)[0]
text_x1 = px(LX + PAD_HW - 0.34, PAD_CY, PAPER_Z)[0]
cm = (px(RX + 1.0, DISP_Y, z)[0] - px(RX, DISP_Y, z)[0])


def define(name, v):
    return '#define %s %s' % (name, ('%.2f' % v) if isinstance(v, float) else str(v))


def box_def(name, b):
    return '#define %s { %d, %d, %d, %d }' % (name, *b)


with open(os.path.join(OUT, 'calc-map.h'), 'w') as f:
    f.write('\n'.join([
        '/* calc-map.h: made by calc.py with the pictures -- do not edit, make again.',
        ' * Where everything is on the calculator, in pixels of its canvas (the',
        ' * pictures\' whole, from its top left), CALC_BAKE_PX to the centimetre. A box',
        ' * is x0, y0, x1, y1. */',
        '',
        define('CALC_BAKE_PX', BAKE_PX),
        define('CALC_CANVAS', CANVAS),
        define('CALC_OPEN_X', open_x), define('CALC_OPEN_Y', open_y),
        define('CALC_SHUT_X', shut_x), define('CALC_SHUT_Y', shut_y),
        box_def('CALC_OPEN_BOX', open_box), box_def('CALC_SHUT_BOX', shut_box),
        box_def('CALC_RIGHT_BOX', right_box), box_def('CALC_LID_BOX', lid_box),
        box_def('CALC_LID_TRAY', tray_box), box_def('CALC_HINGE_BOX', hinge_box),
        box_def('CALC_DISP_BOX', disp_box), box_def('CALC_PAPER_BOX', paper_box),
        '/* the outside\'s display, shut, and its figures: the first one\'s lower left, how far',
        ' * to the next and up it (the inside\'s display, CALC_DISP_BOX, is a matrix of dots',
        ' * DEsk draws) */',
        box_def('CALC_ODISP_BOX', odisp_box),
        define('CALC_FIG_X', p00[0]), define('CALC_FIG_Y', p00[1]),
        define('CALC_FIG_PITCH', (p10[0] - p00[0]) / 11.0),
        define('CALC_FIG_RISE', p00[1] - p01[1]),
        define('CALC_FIG_W', FIG_W * cm), define('CALC_FIG_T', FIG_T * cm),
        define('CALC_FIG_SLANT', FIG_SLANT),
        define('CALC_FIGURES', 12),
        '/* the paper: its rules, down it, and where the sums start and the answers end */',
        define('CALC_RULES', n_rules),
        'static const double CALC_RULE_Y[CALC_RULES] = { %s };' % ', '.join('%.2f' % v for v in rules),
        define('CALC_TEXT_X0', text_x0), define('CALC_TEXT_X1', text_x1),
        define('CALC_TEXT_SIZE', 0.56 * cm),
        '',
        '/* the keys: which, on which face, where a hand finds it, and its piece held',
        ' * down: where that goes, how big it is, and where it is on the sheet */',
        'static const struct calc_key_map {',
        '\tenum desk_calc_key key;',
        '\tunsigned char face;     /* 0 open, the inside; 1 shut, the outside */',
        '\tshort hit[4];',
        '\tshort at[4];',
        '\tshort sx, sy;',
        '} CALC_KEYS[] = {',
        *key_lines,
        '};',
        '',
        '/* the lid on its way (calc-lid-NN.png): where each goes */',
        '#include "calc-film.h"',
        '',
        '/* the calculator lying in the drawer (calc-top.png), on its side: how many',
        ' * pixels it is along, and its middle */',
        '',
    ]))

# as it lies in the drawer: from straight above, on its side, its own light
for o in bpy.data.objects:
    if o.type == 'LIGHT':
        o.hide_render = True
desk.hide_render = True
lid_to(180.0)
area('top-lamp', (RX + 20.0, 24.0, 44.0), (RX, 0.0, 2.0), 30.0, 30.0, 6.0, (1.0, 0.96, 0.90))
area('top-fill', (RX - 10.0, -6.0, 60.0), (RX, 0.0, 2.0), 70.0, 70.0, 0.8)
cd = bpy.data.cameras.new('above')
cd.type = 'ORTHO'
TOP_PX = 22
span_along, span_across = H + 1.0, W + 1.8
cd.ortho_scale = span_along
cd.sensor_fit = 'HORIZONTAL'
cd.clip_start = 1.0
cd.clip_end = 200.0
above = bpy.data.objects.new('above', cd)
COLL.objects.link(above)
above.location = (RX, 0.0, 60.0)
above.rotation_euler = (0.0, 0.0, math.pi * 0.5)
SCENE.camera = above
r.resolution_percentage = 100
r.resolution_x, r.resolution_y = int(span_along * TOP_PX), int(span_across * TOP_PX)
r.use_border = False
r.use_crop_to_border = False
c.samples = 12 if QUICK else 128
r.filepath = os.path.join(OUT, 'calc-top.png')
bpy.ops.render.render(write_still=True)
with open(os.path.join(OUT, 'calc-map.h'), 'a') as f:
    f.write('\n'.join([
        define('CALC_TOP_ALONG_PX', float(H * TOP_PX)),
        define('CALC_TOP_MID_X', r.resolution_x * 0.5),
        define('CALC_TOP_MID_Y', r.resolution_y * 0.5),
        '',
    ]))
print('calc.py: done', flush=True)
