"""The camera Print puts up to the eye (wm.c, snip_*), made in Blender.

A 35 mm reflex camera, black, its top plate satin chrome, seen from behind
and a little above, as it is held up: modelled here, lit as the desk's lamp
lights the desk -- from above and a little to the left -- with a studio's
soft boxes round it for the metal to have something to show, and rendered
with Cycles. What moves on it is rendered again, just where it is, as it
moves: the mode dial's knurling as it turns (the pictures on the dial's top
are drawn by draw.c, turning with it), the power lever going over, the
shutter button going down; the self-timer's knob, with its shadow, alone
(draw.c lays it wherever the slide is); the glass of the frame counter's
window, alone, to go over the figures draw.c puts in it; the eyepiece with
its glass seen through, for the camera coming up to the eye; and the slot
in the plate's back for a tape, its flap pushed in, the little video
cassette that goes into it -- alone, as it is brought up to the slot's
mouth, for draw.c to push in -- and the tape in it, its shadow and all.
All of it is packed into one picture, camera.png, and where each piece is
in it and where it goes on the camera into camera-map.h, for
desk_draw_finder_camera (draw.c) to lay together.

    blender -b --factory-startup -P art/camera/camera.py -- art/camera [--quick]
        [--samples N] [--only base | --only top]

--quick renders with few samples, to look at; --only base renders the
camera whole and nothing else (and says where it put it, not packed);
--only top just the camera as it lies in the drawer (camera-top.png and
camera-top.h), its lens on it. Made with
Blender 4.5. The sizes are in centimetres; the camera is original --
nothing on it is a make's, and nothing on it is a word: the dial has
pictures, the power lever I and O, the self-timer and the film's box their
figures, the slot the eject mark, the tape's label a band and a red dot.
"""

import math
import os
import shutil
import sys
import tempfile

import bmesh
import bpy
import numpy as np
from mathutils import Vector

ARGV = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = os.path.abspath(ARGV[0] if ARGV and not ARGV[0].startswith('--')
                      else os.path.dirname(os.path.abspath(__file__)))
QUICK = '--quick' in ARGV
ONLY = ARGV[ARGV.index('--only') + 1] if '--only' in ARGV else None
SAMPLES = int(ARGV[ARGV.index('--samples') + 1]) if '--samples' in ARGV else (24 if QUICK else 384)

# ---- the view: draw.c's, its camera 600 by 480 units, PX pixels to the unit
PX = 3
UNITS = 40.0                 # units to the centimetre
CW, CH = 600, 480            # draw.c's camera, in its units (the strap hangs on down)
W, H = CW * PX, CH * PX
SIN_T = 0.33                 # seen from 19 degrees up: a circle on the top this flat
COS_T = math.sqrt(1.0 - SIN_T * SIN_T)
V0 = 341.3                   # where the camera's foot is, down the picture, in units


def proj(p):
    """Where a point (x right, y away from the eye, z up) is in the picture,
    in draw.c's units."""
    return 300.0 + UNITS * p[0], V0 - UNITS * (p[2] * COS_T + p[1] * SIN_T)


# ---- the scene, empty
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
SCENE = bpy.context.scene
COLL = SCENE.collection


# ---- materials

def principled(name, base, metallic=0.0, rough=0.5, spec=0.5, coat=0.0, coat_rough=0.05,
               transmission=0.0, ior=1.45, aniso=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*base, 1.0)
    b.inputs['Metallic'].default_value = metallic
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular IOR Level'].default_value = spec
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Coat Roughness'].default_value = coat_rough
    b.inputs['Transmission Weight'].default_value = transmission
    b.inputs['IOR'].default_value = ior
    b.inputs['Anisotropic'].default_value = aniso
    return m


def nodes(m):
    nt = m.node_tree
    return nt, nt.nodes, nt.links, nt.nodes['Principled BSDF']


def bump_from(m, height_socket, strength, distance):
    nt, n, ln, b = nodes(m)
    bump = n.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = strength
    bump.inputs['Distance'].default_value = distance
    ln.new(height_socket, bump.inputs['Height'])
    ln.new(bump.outputs['Normal'], b.inputs['Normal'])
    return bump


def leather():
    """Black leatherette: pebbled, a little sheen on the pebbles' tops."""
    m = principled('leather', (0.024, 0.023, 0.022), rough=0.58, spec=0.45)
    nt, n, ln, b = nodes(m)
    tc = n.new('ShaderNodeTexCoord')
    vor = n.new('ShaderNodeTexVoronoi')
    vor.feature = 'F1'
    vor.inputs['Scale'].default_value = 15.0
    vor.inputs['Randomness'].default_value = 1.0
    ln.new(tc.outputs['Object'], vor.inputs['Vector'])
    # a pebble: high in its middle, falling off to its edges
    sq = n.new('ShaderNodeMath')
    sq.operation = 'POWER'
    sq.inputs[1].default_value = 1.6
    ln.new(vor.outputs['Distance'], sq.inputs[0])
    inv = n.new('ShaderNodeMath')
    inv.operation = 'SUBTRACT'
    inv.inputs[0].default_value = 1.0
    ln.new(sq.outputs[0], inv.inputs[1])
    grain = n.new('ShaderNodeTexNoise')
    grain.inputs['Scale'].default_value = 90.0
    grain.inputs['Detail'].default_value = 4.0
    ln.new(tc.outputs['Object'], grain.inputs['Vector'])
    mix = n.new('ShaderNodeMath')
    mix.operation = 'MULTIPLY_ADD'
    mix.inputs[2].default_value = 0.0
    ln.new(grain.outputs['Fac'], mix.inputs[0])
    mix.inputs[1].default_value = 0.15
    add = n.new('ShaderNodeMath')
    add.operation = 'ADD'
    ln.new(inv.outputs[0], add.inputs[0])
    ln.new(mix.outputs[0], add.inputs[1])
    bump_from(m, add.outputs[0], 0.55, 0.012)
    # the tops worn a little smoother
    rr = n.new('ShaderNodeMapRange')
    rr.inputs['To Min'].default_value = 0.66
    rr.inputs['To Max'].default_value = 0.46
    ln.new(inv.outputs[0], rr.inputs['Value'])
    ln.new(rr.outputs['Result'], b.inputs['Roughness'])
    return m


def satin():
    """Satin chrome, brushed along the plate."""
    m = principled('satin', (0.80, 0.80, 0.79), metallic=1.0, rough=0.20, aniso=0.65)
    nt, n, ln, b = nodes(m)
    along = n.new('ShaderNodeCombineXYZ')
    along.inputs[0].default_value = 1.0
    ln.new(along.outputs[0], b.inputs['Tangent'])
    tc = n.new('ShaderNodeTexCoord')
    mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (0.04, 30.0, 30.0)
    ln.new(tc.outputs['Object'], mp.inputs['Vector'])
    streak = n.new('ShaderNodeTexNoise')
    streak.inputs['Scale'].default_value = 6.0
    streak.inputs['Detail'].default_value = 8.0
    ln.new(mp.outputs['Vector'], streak.inputs['Vector'])
    rr = n.new('ShaderNodeMapRange')
    rr.inputs['To Min'].default_value = 0.15
    rr.inputs['To Max'].default_value = 0.25
    ln.new(streak.outputs['Fac'], rr.inputs['Value'])
    ln.new(rr.outputs['Result'], b.inputs['Roughness'])
    bump_from(m, streak.outputs['Fac'], 0.08, 0.002)
    return m


def rubber():
    m = principled('rubber', (0.020, 0.020, 0.020), rough=0.78, spec=0.4)
    nt, n, ln, b = nodes(m)
    tc = n.new('ShaderNodeTexCoord')
    nz = n.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 140.0
    nz.inputs['Detail'].default_value = 3.0
    ln.new(tc.outputs['Object'], nz.inputs['Vector'])
    bump_from(m, nz.outputs['Fac'], 0.25, 0.004)
    return m


def strap_cloth():
    """The strap: black, woven, a red stripe down its middle."""
    m = principled('strap', (0.028, 0.027, 0.026), rough=0.85, spec=0.35)
    nt, n, ln, b = nodes(m)
    tc = n.new('ShaderNodeTexCoord')
    sep = n.new('ShaderNodeSeparateXYZ')
    ln.new(tc.outputs['UV'], sep.inputs[0])
    # the weave: across it, finely
    wave = n.new('ShaderNodeTexWave')
    wave.wave_type = 'BANDS'
    wave.bands_direction = 'Y'
    wave.inputs['Scale'].default_value = 1.0
    mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (1.0, 260.0, 1.0)
    ln.new(tc.outputs['UV'], mp.inputs['Vector'])
    ln.new(mp.outputs['Vector'], wave.inputs['Vector'])
    bump_from(m, wave.outputs['Fac'], 0.45, 0.01)
    # the stripe: the middle fifth across
    d = n.new('ShaderNodeMath')
    d.operation = 'SUBTRACT'
    d.inputs[1].default_value = 0.5
    ln.new(sep.outputs['X'], d.inputs[0])
    a = n.new('ShaderNodeMath')
    a.operation = 'ABSOLUTE'
    ln.new(d.outputs[0], a.inputs[0])
    st = n.new('ShaderNodeMath')
    st.operation = 'LESS_THAN'
    st.inputs[1].default_value = 0.11
    ln.new(a.outputs[0], st.inputs[0])
    mix = n.new('ShaderNodeMix')
    mix.data_type = 'RGBA'
    mix.inputs['A'].default_value = (0.028, 0.027, 0.026, 1.0)
    mix.inputs['B'].default_value = (0.42, 0.045, 0.035, 1.0)
    ln.new(st.outputs[0], mix.inputs['Factor'])
    ln.new(mix.outputs['Result'], b.inputs['Base Color'])
    return m


def reflection_only(name, rough=0.0, much=1.0):
    """Glass, only its reflection (much of it): what is behind it left to
    show (the picture's transparent there, or as much as the glass lets
    through)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    tr = nt.nodes.new('ShaderNodeBsdfTransparent')
    gl = nt.nodes.new('ShaderNodeBsdfGlossy')
    gl.inputs['Roughness'].default_value = rough
    fr = nt.nodes.new('ShaderNodeFresnel')
    fr.inputs['IOR'].default_value = 1.52
    mix = nt.nodes.new('ShaderNodeMixShader')
    k = nt.nodes.new('ShaderNodeMath')
    k.operation = 'MULTIPLY'
    k.inputs[1].default_value = much
    nt.links.new(fr.outputs['Fac'], k.inputs[0])
    nt.links.new(k.outputs[0], mix.inputs['Fac'])
    nt.links.new(tr.outputs[0], mix.inputs[1])
    nt.links.new(gl.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs['Surface'])
    return m


def holdout():
    m = bpy.data.materials.new('holdout')
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    h = nt.nodes.new('ShaderNodeHoldout')
    nt.links.new(h.outputs[0], out.inputs['Surface'])
    return m


MAT = {
    'leather': leather(),
    'satin': satin(),
    'chrome': principled('chrome', (0.90, 0.90, 0.89), metallic=1.0, rough=0.06),
    'black': principled('black', (0.010, 0.010, 0.010), rough=0.30, coat=0.6, coat_rough=0.12),
    'dial': principled('dial', (0.016, 0.016, 0.016), rough=0.36, spec=0.55),
    'plastic': principled('plastic', (0.018, 0.018, 0.018), rough=0.42),
    'rubber': rubber(),
    'glass': principled('glass', (0.0, 0.0, 0.0), rough=0.0, spec=0.5, coat=1.0, coat_rough=0.0),
    'white': principled('white', (0.80, 0.80, 0.77), rough=0.45),
    'ink': principled('ink', (0.012, 0.012, 0.012), rough=0.5),
    'red': principled('red', (0.66, 0.05, 0.035), rough=0.40),
    'lamp': principled('lamp', (0.50, 0.02, 0.015), rough=0.10, transmission=0.55, ior=1.5,
                       coat=1.0),
    'clear': principled('clear', (1.0, 1.0, 1.0), rough=0.02, transmission=1.0, ior=1.49),
    'brass': principled('brass', (0.86, 0.66, 0.36), metallic=1.0, rough=0.22),
    'card': principled('card', (0.80, 0.74, 0.58), rough=0.62),
    'teal': principled('teal', (0.11, 0.28, 0.36), rough=0.6),
    'wave': principled('wave', (0.50, 0.17, 0.09), rough=0.6),
    'hole': principled('hole', (0.004, 0.004, 0.004), rough=0.9),
    'shell': principled('shell', (0.030, 0.030, 0.034), rough=0.30, coat=0.35, coat_rough=0.10),
    'tape': principled('tape', (0.12, 0.065, 0.030), rough=0.25, spec=0.7),
    'strap': strap_cloth(),
    'see': reflection_only('see', much=0.35),   # (the eye at it: hardly a sheen)
    'dome': reflection_only('dome'),
    'holdout': holdout(),
}


# ---- meshes

def put(name, bm, mat, smooth=True, loc=(0.0, 0.0, 0.0)):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    COLL.objects.link(o)
    o.location = loc
    me.materials.append(MAT[mat])
    if smooth:
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(35.0))
    return o


def bevel(o, width, seg=4, angle=35.0, harden=True):
    m = o.modifiers.new('bevel', 'BEVEL')
    m.width = width
    m.segments = seg
    m.limit_method = 'ANGLE'
    m.angle_limit = math.radians(angle)
    m.harden_normals = harden
    return m


def cut(o, cutter, solver='EXACT'):
    """o less cutter -- its normals weighted by its faces after, so that the
    long faces the cut leaves are flat, not shaded across."""
    m = o.modifiers.new('cut', 'BOOLEAN')
    m.operation = 'DIFFERENCE'
    m.solver = solver
    m.object = cutter
    cutter.hide_render = True
    cutter.hide_viewport = True
    wn = o.modifiers.new('normals', 'WEIGHTED_NORMAL')
    wn.mode = 'FACE_AREA'
    wn.keep_sharp = True
    return m


def rrect(hw, hd, r, n=10, cx=0.0, cy=0.0):
    """A rounded rectangle's outline, anticlockwise, about (cx, cy)."""
    r = min(r, hw, hd)
    pts = []
    for qx, qy, a0 in ((hw - r, hd - r, 0.0), (-(hw - r), hd - r, 90.0),
                       (-(hw - r), -(hd - r), 180.0), (hw - r, -(hd - r), 270.0)):
        for i in range(n + 1):
            a = math.radians(a0 + 90.0 * i / n)
            pts.append((cx + qx + r * math.cos(a), cy + qy + r * math.sin(a)))
    return pts


def circle(r, n=64, cx=0.0, cy=0.0, a0=0.0):
    return [(cx + r * math.cos(a0 + 2 * math.pi * i / n), cy + r * math.sin(a0 + 2 * math.pi * i / n))
            for i in range(n)]


def extrude(name, pts, z0, z1, mat, axis='z', loc=(0.0, 0.0, 0.0)):
    """An outline (anticlockwise) made solid between z0 and z1 -- along z, or
    along y (the outline then in x and z, z0 the nearer the eye)."""
    def at(x, y, z):
        return (x, z, y) if axis == 'y' else (x, y, z)
    bm = bmesh.new()
    lo = [bm.verts.new(at(x, y, z0)) for x, y in pts]
    hi = [bm.verts.new(at(x, y, z1)) for x, y in pts]
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat, loc=loc)


def ring(name, outer, inner, z0, z1, mat, axis='z', loc=(0.0, 0.0, 0.0)):
    """The space between two outlines (the same number of points each, both
    anticlockwise) made solid between z0 and z1."""
    def at(x, y, z):
        return (x, z, y) if axis == 'y' else (x, y, z)
    bm = bmesh.new()
    n = len(outer)
    ol = [bm.verts.new(at(x, y, z0)) for x, y in outer]
    oh = [bm.verts.new(at(x, y, z1)) for x, y in outer]
    il = [bm.verts.new(at(x, y, z0)) for x, y in inner]
    ih = [bm.verts.new(at(x, y, z1)) for x, y in inner]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((ol[i], ol[j], oh[j], oh[i]))
        bm.faces.new((ih[i], ih[j], il[j], il[i]))
        bm.faces.new((oh[i], oh[j], ih[j], ih[i]))
        bm.faces.new((il[i], il[j], ol[j], ol[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat, loc=loc)


def box(name, x0, x1, y0, y1, z0, z1, mat, r=0.0, seg=4, loc=(0.0, 0.0, 0.0)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector(((x0 + x1) * 0.5 + v.co.x * (x1 - x0), (y0 + y1) * 0.5 + v.co.y * (y1 - y0),
                       (z0 + z1) * 0.5 + v.co.z * (z1 - z0)))
    o = put(name, bm, mat, loc=loc)
    if r > 0.0:
        bevel(o, r, seg)
    return o


def hull(name, pts, mat):
    bm = bmesh.new()
    for p in pts:
        bm.verts.new(p)
    res = bmesh.ops.convex_hull(bm, input=bm.verts)
    bmesh.ops.delete(bm, geom=res['geom_interior'] + res['geom_unused'], context='VERTS')
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat)


def cylinder(name, r, z0, z1, mat, n=64, loc=(0.0, 0.0, 0.0), axis='z'):
    return extrude(name, circle(r, n), z0, z1, mat, axis=axis, loc=loc)


def text(name, s, size, loc, rot, mat, font=None, extrude=0.0, align='CENTER'):
    cu = bpy.data.curves.new(name, 'FONT')
    cu.body = s
    cu.size = size
    cu.align_x = align
    cu.align_y = 'CENTER'
    cu.extrude = extrude
    if font:
        cu.font = font
    o = bpy.data.objects.new(name, cu)
    COLL.objects.link(o)
    o.location = loc
    o.rotation_euler = rot
    cu.materials.append(MAT[mat])
    return o


FONT = None
for f in ('/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf',
          '/usr/share/fonts/dejavu-sans-fonts/DejaVuSansCondensed-Bold.ttf'):
    if os.path.exists(f):
        FONT = bpy.data.fonts.load(f)
        break


# ---- the camera
#
# x right, y away from the eye (the lens is at +y), z up; the foot at z 0.

BACK = -1.40        # the leatherette back
PLATE_BACK = -1.46  # the top plate's back face, a little proud of it
PLATE_Z0, PLATE_Z1 = 5.04, 6.10
HW = 6.85           # half the body's width

body = extrude('body', rrect(HW, 1.475, 0.8, cy=0.075), 0.30, PLATE_Z0 + 0.03, 'leather')
bevel(body, 0.04, 2)
# the back door's seam all round it, and its latch
door = ring('door-seam', rrect(5.97, 1.935, 0.27, cy=2.42), rrect(5.935, 1.90, 0.24, cy=2.42),
            BACK - 0.10, BACK + 0.03, 'black', axis='y')
cut(body, door)
box('latch', 6.02, 6.24, BACK - 0.07, BACK + 0.02, 2.22, 3.44, 'chrome', r=0.035)

top = extrude('top-plate', rrect(HW, 1.45, 0.8, cy=-0.01), PLATE_Z0, PLATE_Z1, 'satin')
bevel(top, 0.11, 6)
slot = extrude('timer-slot', rrect(0.77, 0.09, 0.09), PLATE_BACK - 0.2, PLATE_BACK + 0.08,
               'black', axis='y', loc=(2.39, 0.0, 5.47))
cut(top, slot)
bottom = extrude('bottom-plate', rrect(HW, 1.45, 0.8, cy=-0.01), 0.0, 0.32, 'satin')
bevel(bottom, 0.08, 4)
# in the slot, its dark
box('slot-dark', 1.60, 3.18, PLATE_BACK + 0.06, PLATE_BACK + 0.10, 5.37, 5.57, 'hole')

# the prism's housing, its roof and the shoe on it
prism = hull('prism', [
    (-1.86, -1.30, 6.05), (1.86, -1.30, 6.05), (1.86, 1.52, 6.05), (-1.86, 1.52, 6.05),
    (-1.30, -0.42, 8.0), (1.30, -0.42, 8.0), (1.30, 0.60, 8.0), (-1.30, 0.60, 8.0),
], 'black')
bevel(prism, 0.14, 6)
box('shoe', -0.72, 0.72, -0.40, 0.56, 7.99, 8.06, 'chrome', r=0.015)
for s in (-1, 1):
    box('shoe-rail', min(s * 0.72, s * 0.62), max(s * 0.72, s * 0.62), -0.40, 0.56, 8.06, 8.13,
        'chrome', r=0.02)
    box('shoe-lip', min(s * 0.72, s * 0.52), max(s * 0.72, s * 0.52), -0.40, 0.56, 8.12, 8.15,
        'chrome', r=0.012)
cylinder('shoe-insulator', 0.20, 8.06, 8.075, 'plastic', loc=(0.0, 0.06, 0.0))
cylinder('shoe-contact', 0.11, 8.075, 8.09, 'brass', loc=(0.0, 0.06, 0.0))

# the eyepiece: its block, the rubber cup on it, the glass deep in that
EYE_Z = 5.70
box('eye-block', -1.22, 1.22, -1.64, -0.9, 4.62, 6.55, 'black', r=0.10)
cup = ring('eyecup', rrect(1.10, 0.95, 0.42), rrect(0.80, 0.66, 0.24), -2.18, -1.62, 'rubber',
           axis='y', loc=(0.0, 0.0, EYE_Z))
bevel(cup, 0.09, 5, angle=30.0)
GLASS_HW, GLASS_HH = 0.675, 0.58
glass = extrude('eye-glass', rrect(GLASS_HW, GLASS_HH, 0.18), -1.655, -1.645, 'glass', axis='y',
                loc=(0.0, 0.0, EYE_Z))
see_hole = extrude('eye-see-hole', rrect(GLASS_HW, GLASS_HH, 0.18), -1.64, -1.63, 'holdout',
                   axis='y', loc=(0.0, 0.0, EYE_Z))
see_hole.hide_render = True

# ---- the mode dial: a black drum, knurled, on the plate's left; its top
# bare (draw.c puts the pictures on it), a chrome hub in its middle

DIAL_X, DIAL_Y = -4.45, 0.0
DIAL_R, DIAL_Z0, DIAL_Z1 = 1.45, 6.16, 6.66
KNURLS = 60


def knurled(name, r, z0, z1, knurls, depth, chamfer, phase, mat):
    """A drum, its side knurled between a plain foot and a chamfered top."""
    seg = knurls * 4
    rows = [(r, z0), (r, z0 + 0.05), (None, z0 + 0.08), (None, z1 - chamfer - 0.07),
            (r, z1 - chamfer - 0.04), (r, z1 - chamfer), (r - chamfer, z1)]
    bm = bmesh.new()
    loops = []
    for rad, z in rows:
        loop = []
        for j in range(seg):
            a = 2.0 * math.pi * j / seg + phase
            rr = rad
            if rr is None:
                k = j % 4    # a V groove: ridge, side, bottom, side
                rr = r - depth * (0.0, 0.5, 1.0, 0.5)[k]
            loop.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), z)))
        loops.append(loop)
    for a, b in zip(loops, loops[1:]):
        for j in range(seg):
            k = (j + 1) % seg
            bm.faces.new((a[j], a[k], b[k], b[j]))
    bm.faces.new(loops[-1])
    bm.faces.new(list(reversed(loops[0])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = put(name, bm, mat, loc=(DIAL_X, DIAL_Y, 0.0))
    o.data.set_sharp_from_angle(angle=math.radians(50.0))
    return o


cylinder('dial-foot', DIAL_R + 0.03, PLATE_Z1 - 0.01, DIAL_Z0, 'chrome', n=96, loc=(DIAL_X, DIAL_Y, 0))
dial = knurled('dial', DIAL_R, DIAL_Z0, DIAL_Z1, KNURLS, 0.035, 0.06, 0.0, 'dial')
cylinder('dial-hub', 0.26, DIAL_Z1 - 0.01, DIAL_Z1 + 0.015, 'chrome', loc=(DIAL_X, DIAL_Y, 0))
# the index mark, red, on the plate's back face under the dial's front
bm = bmesh.new()
tri = [bm.verts.new((DIAL_X + dx, PLATE_BACK - 0.003, z))
       for dx, z in ((0.0, 5.93), (-0.11, 5.80), (0.11, 5.80))]
bm.faces.new(tri)
put('dial-index', bm, 'red', smooth=False)

# ---- the shutter button in its power collar, its lever's tab turned to
# I (on) or O (off), marked on the plate

SHUT_X, SHUT_Y = 4.20, 0.0


def tab_angle(power):
    """The lever's tab: from the button towards the eye, turned to the right
    (I) or the left (O)."""
    return math.radians(90.0) + (0.5 - power) * 1.1


collar = ring('collar', circle(0.66, 96), circle(0.41, 96), PLATE_Z1 - 0.01, 6.24, 'black',
              loc=(SHUT_X, SHUT_Y, 0.0))
bevel(collar, 0.025, 2)
bm = bmesh.new()
tab_pts = [(0.55, -0.15), (0.94, -0.09), (0.98, 0.0), (0.94, 0.09), (0.55, 0.15)]
lo = [bm.verts.new((x, y, 6.12)) for x, y in tab_pts]
hi = [bm.verts.new((x, y, 6.22)) for x, y in tab_pts]
bm.faces.new(list(reversed(lo)))
bm.faces.new(hi)
for i in range(len(tab_pts) - 1):
    bm.faces.new((lo[i], lo[i + 1], hi[i + 1], hi[i]))
bm.faces.new((lo[-1], lo[0], hi[0], hi[-1]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
tab = put('collar-tab', bm, 'black', loc=(SHUT_X, SHUT_Y, 0.0))
bevel(tab, 0.02, 2)
button = cylinder('button', 0.375, 6.18, 6.44, 'chrome', n=96, loc=(SHUT_X, SHUT_Y, 0.0))
bevel(button, 0.03, 3)
dish = cylinder('button-dish', 0.30, 6.43, 6.50, 'chrome', loc=(SHUT_X, SHUT_Y, 0.0))
cut(button, dish)
cylinder('button-socket', 0.075, 6.40, 6.435, 'hole', loc=(SHUT_X, SHUT_Y, 0.0))
# I and O, white, on the plate where the tab points at each
for power, mark in ((1.0, 'I'), (0.0, 'O')):
    a = tab_angle(power)
    mx, my = SHUT_X + 1.12 * math.cos(a), SHUT_Y - 1.12 * math.sin(a)
    if mark == 'O':
        ring('mark-O', circle(0.085, 32), circle(0.05, 32), PLATE_Z1, PLATE_Z1 + 0.002, 'white',
             loc=(mx, my, 0.0))
    else:
        box('mark-I', mx - 0.018, mx + 0.018, my - 0.09, my + 0.09, PLATE_Z1, PLATE_Z1 + 0.002,
            'white')

# ---- the frame counter: a chrome bezel, the dark under it, its glass

COUNT_X, COUNT_Y = 2.45, 0.0
ring('count-bezel', circle(0.44, 64), circle(0.33, 64), PLATE_Z1 - 0.01, 6.16, 'chrome',
     loc=(COUNT_X, COUNT_Y, 0.0))
cylinder('count-dark', 0.335, PLATE_Z1 - 0.01, 6.105, 'hole', loc=(COUNT_X, COUNT_Y, 0.0))
bm = bmesh.new()
bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=0.62)
bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, 0, 0.53),
                       plane_no=(0, 0, 1), clear_inner=True)
dome = put('count-dome', bm, 'dome', loc=(COUNT_X, COUNT_Y, 6.16 - 0.53))
dome.hide_render = True

# ---- the self-timer: its slide in the plate's back face, the marks over
# it, its knob; its lamp beside it

KNOB_X0, KNOB_STEP, KNOB_Z = 1.80, 0.55, 5.47
knob = box('knob', -0.18, 0.18, PLATE_BACK - 0.13, PLATE_BACK + 0.05, KNOB_Z - 0.16, KNOB_Z + 0.16,
           'chrome', r=0.05, loc=(KNOB_X0 + KNOB_STEP, 0.0, 0.0))
for k in range(-2, 3):
    g = box('knob-ridge', -0.012, 0.012, -0.03, 0.03, KNOB_Z - 0.11, KNOB_Z + 0.11, 'black')
    g.location = (KNOB_X0 + KNOB_STEP + k * 0.065, PLATE_BACK - 0.13, 0.0)
    g.parent = knob
    g.matrix_parent_inverse = knob.matrix_world.inverted()
dot = cylinder('timer-dot', 0.035, PLATE_BACK - 0.003, PLATE_BACK, 'ink', axis='y',
               loc=(KNOB_X0, 0.0, 5.80))
for i, fig in ((1, '2'), (2, '10')):
    text('timer-' + fig, fig, 0.20, (KNOB_X0 + i * KNOB_STEP, PLATE_BACK - 0.003, 5.80),
         (math.radians(90.0), 0.0, 0.0), 'ink', font=FONT)
LAMP_X = 3.62
ring('lamp-bezel', circle(0.20, 48), circle(0.14, 48), PLATE_BACK - 0.06, PLATE_BACK + 0.01,
     'chrome', axis='y', loc=(LAMP_X, 0.0, KNOB_Z))
bm = bmesh.new()
bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=0.15)
put('lamp', bm, 'lamp', loc=(LAMP_X, PLATE_BACK - 0.02, KNOB_Z))

# ---- the tape's slot: in the plate's back face left of the eyepiece, a
# black surround round it and a flap in it, hinged at its top, that a tape
# pushes in; the eject mark beside it

SLOT_X, SLOT_Z = -3.65, 5.40       # its middle
SLOT_HW, SLOT_HH = 1.90, 0.19      # half as wide, half as high
slot_cut = extrude('tape-slot', rrect(SLOT_HW, SLOT_HH, 0.07), PLATE_BACK - 0.2, PLATE_BACK + 1.6,
                   'black', axis='y', loc=(SLOT_X, 0.0, SLOT_Z))
cut(top, slot_cut)
box('slot-back', SLOT_X - SLOT_HW, SLOT_X + SLOT_HW, PLATE_BACK + 1.30, PLATE_BACK + 1.35,
    SLOT_Z - SLOT_HH, SLOT_Z + SLOT_HH, 'hole')
surround = ring('slot-surround', rrect(SLOT_HW + 0.06, SLOT_HH + 0.06, 0.12),
                rrect(SLOT_HW, SLOT_HH, 0.07), PLATE_BACK - 0.035, PLATE_BACK + 0.01, 'plastic',
                axis='y', loc=(SLOT_X, 0.0, SLOT_Z))
bevel(surround, 0.012, 2)
# the flap: drawn from its hinge down, so that it turns on that
FLAP_HH = SLOT_HH - 0.025
bm = bmesh.new()
fl = rrect(SLOT_HW - 0.03, FLAP_HH, 0.05)
lo = [bm.verts.new((x, 0.0, z - FLAP_HH)) for x, z in fl]
hi = [bm.verts.new((x, 0.03, z - FLAP_HH)) for x, z in fl]
bm.faces.new(list(reversed(lo)))
bm.faces.new(hi)
for i in range(len(fl)):
    j = (i + 1) % len(fl)
    bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
flap = put('slot-flap', bm, 'black', loc=(SLOT_X, PLATE_BACK + 0.10, SLOT_Z + FLAP_HH))
# the eject mark: a triangle over a bar
EJECT_X = -1.45
bm = bmesh.new()
tri = [bm.verts.new((EJECT_X + dx, PLATE_BACK - 0.003, SLOT_Z + dz))
       for dx, dz in ((-0.09, -0.005), (0.09, -0.005), (0.0, 0.085))]
bm.faces.new(tri)
put('eject-mark', bm, 'ink', smooth=False)
box('eject-bar', EJECT_X - 0.09, EJECT_X + 0.09, PLATE_BACK - 0.004, PLATE_BACK - 0.003,
    SLOT_Z - 0.065, SLOT_Z - 0.035, 'ink')

# ---- the tape: a little video cassette, its label up and its spine to the
# eye, as it goes into the slot far end first -- its shell smoked, a window
# in its top over the two reels and the tape wound on them, a label (no
# words: a band, and the red dot that says it records) over the rest and
# round its spine. Made about its far edge's middle, at its foot, so that
# it is put where it is by moving that.

TW, TD, TT = 3.56, 2.36, 0.30      # across, front to back, thick
TAPE_Z = SLOT_Z - SLOT_HH + 0.045  # its foot, in the slot
TAPE_OUT = 0.50                    # how much of it stays out, in
tape_parts = []


def tape_part(o):
    tape_parts.append(o)
    return o


shell = tape_part(box('tape-shell', -TW * 0.5, TW * 0.5, -TD, 0.0, 0.0, TT, 'shell', r=0.06, seg=3))
win_cut = extrude('tape-window-cut', rrect(1.38, 0.43, 0.16), 0.07, TT + 0.2, 'black',
                  loc=(0.0, -0.66, 0.0))
cut(shell, win_cut)
for side, pack in ((-1, 0.52), (1, 0.34)):
    cx = side * 0.80
    tape_part(cylinder('tape-pack', pack, 0.07, 0.18, 'tape', n=64, loc=(cx, -0.66, 0.0)))
    tape_part(ring('tape-hub', circle(0.21, 48), circle(0.10, 48), 0.07, 0.19, 'white',
                   loc=(cx, -0.66, 0.0)))
    tape_part(cylinder('tape-spindle', 0.10, 0.07, 0.16, 'hole', n=32, loc=(cx, -0.66, 0.0)))
    for k in range(6):
        a = k * math.pi / 3.0
        tooth = tape_part(box('tape-hub-tooth', -0.018, 0.018, 0.08, 0.115, 0.07, 0.185,
                              'white', loc=(cx, -0.66, 0.0)))
        tooth.rotation_euler = (0.0, 0.0, a)
tape_part(extrude('tape-window', rrect(1.38, 0.43, 0.16), TT - 0.03, TT - 0.012, 'clear',
                  loc=(0.0, -0.66, 0.0)))
# the label over the near part of its top, and round its spine
tape_part(box('tape-label', -1.66, 1.66, -TD + 0.08, -1.24, TT, TT + 0.004, 'card'))
tape_part(box('tape-band', -1.66, 1.66, -1.78, -1.58, TT + 0.004, TT + 0.006, 'teal'))
tape_part(cylinder('tape-dot', 0.10, TT + 0.004, TT + 0.007, 'red', n=32, loc=(1.28, -1.98, 0.0)))
tape_part(box('tape-spine', -1.62, 1.62, -TD - 0.004, -TD + 0.002, 0.05, TT - 0.05, 'card'))
tape_part(box('tape-spine-band', -1.62, 1.62, -TD - 0.007, -TD - 0.004, 0.11, 0.19, 'teal'))
TAPE = bpy.data.objects.new('tape', None)
COLL.objects.link(TAPE)
for o in tape_parts + [win_cut]:   # (its window's cutter goes with it, unseen)
    o.parent = TAPE
# (not in the chrome over the slot: its label there, sharp, looked to be
# seen through the plate)
for o in tape_parts:
    o.visible_glossy = False


def tape_at(inside):
    """The tape at the slot, its far end this far in (0: at the mouth)."""
    TAPE.location = (SLOT_X, PLATE_BACK + inside, TAPE_Z)


def tape_show(on):
    for o in tape_parts:
        o.hide_render = not on


tape_at(TD - TAPE_OUT)
tape_show(False)

# ---- the advance lever, at rest along the plate's right end

WIND_X, WIND_Y, WIND_A = 5.90, 0.10, 0.18 * math.pi
cylinder('wind-boss', 0.26, PLATE_Z1 - 0.01, 6.30, 'chrome', loc=(WIND_X, WIND_Y, 0.0))
bm = bmesh.new()
arm = [(0.0, -0.11), (1.05, -0.085), (1.05, 0.085), (0.0, 0.11)]
lo = [bm.verts.new((x, y, 6.20)) for x, y in arm]
hi = [bm.verts.new((x, y, 6.29)) for x, y in arm]
bm.faces.new(list(reversed(lo)))
bm.faces.new(hi)
for i in range(4):
    j = (i + 1) % 4
    bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
lever = put('wind-arm', bm, 'chrome', loc=(WIND_X, WIND_Y, 0.0))
bevel(lever, 0.02, 2)
lever.rotation_euler = (0.0, 0.0, -WIND_A)
tip = box('wind-tip', 1.0, 1.32, -0.13, 0.13, 6.17, 6.33, 'plastic', r=0.05,
          loc=(WIND_X, WIND_Y, 0.0))
tip.rotation_euler = (0.0, 0.0, -WIND_A)
cylinder('wind-screw', 0.12, 6.30, 6.33, 'chrome', loc=(WIND_X, WIND_Y, 0.0))

# ---- screws: two in the plate's back face, two on its top

for sx in (-6.1, 6.1):
    cylinder('screw', 0.075, PLATE_BACK - 0.025, PLATE_BACK + 0.01, 'chrome', axis='y',
             loc=(sx, 0.0, 5.56))
    box('screw-slot', sx - 0.06, sx + 0.06, PLATE_BACK - 0.03, PLATE_BACK - 0.02, 5.55, 5.57, 'hole')
for sx, sy in ((-2.55, 0.95), (1.30, 0.95)):
    cylinder('screw', 0.07, PLATE_Z1 - 0.01, PLATE_Z1 + 0.025, 'chrome', loc=(sx, sy, 0.0))
    box('screw-slot', sx - 0.055, sx + 0.055, sy - 0.01, sy + 0.01, PLATE_Z1 + 0.02,
        PLATE_Z1 + 0.03, 'hole')

# ---- the memo holder on the door, the end of a film's box in it

MEMO_X, MEMO_Z = 3.95, 2.88
ring('memo-frame', rrect(1.20, 0.77, 0.08), rrect(1.08, 0.65, 0.05), BACK - 0.10, BACK + 0.01,
     'chrome', axis='y', loc=(MEMO_X, 0.0, MEMO_Z))
box('memo-card', MEMO_X - 1.09, MEMO_X + 1.09, BACK - 0.03, BACK - 0.02, MEMO_Z - 0.66,
    MEMO_Z + 0.66, 'card')
box('memo-band', MEMO_X - 1.09, MEMO_X + 1.09, BACK - 0.035, BACK - 0.03, MEMO_Z - 0.40,
    MEMO_Z - 0.12, 'teal')
bm = bmesh.new()
pts = [(MEMO_X - 0.92 + k * 0.08, MEMO_Z + 0.30 + 0.09 * math.sin(k * 0.62)) for k in range(18)]
for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
    nx, nz = -(z1 - z0), (x1 - x0)
    ln_ = math.hypot(nx, nz)
    nx, nz = nx / ln_ * 0.022, nz / ln_ * 0.022
    q = [bm.verts.new((x0 - nx, BACK - 0.033, z0 - nz)), bm.verts.new((x1 - nx, BACK - 0.033, z1 - nz)),
         bm.verts.new((x1 + nx, BACK - 0.033, z1 + nz)), bm.verts.new((x0 + nx, BACK - 0.033, z0 + nz))]
    bm.faces.new(q)
put('memo-wave', bm, 'wave', smooth=False)
text('memo-36', '36', 0.22, (MEMO_X + 0.70, BACK - 0.036, MEMO_Z - 0.26),
     (math.radians(90.0), 0.0, 0.0), 'white', font=FONT)
box('memo-window', MEMO_X - 1.10, MEMO_X + 1.10, BACK - 0.08, BACK - 0.06, MEMO_Z - 0.67,
    MEMO_Z + 0.67, 'clear')

# ---- the lugs, the split rings through them, the strap


def strap(side):
    """A flat band from the ring down and out past the picture's foot, in a
    plane facing the eye; its uv along (y) and across (x) it."""
    s = 1.0 if side > 0 else -1.0
    p = [Vector((s * 7.08, -0.05, 4.18)), Vector((s * 7.30, -0.20, 3.2)),
         Vector((s * 7.42, -0.40, 0.4)), Vector((s * 7.26, -0.60, -5.6))]

    def bez(t):
        u = 1.0 - t
        return p[0] * u ** 3 + p[1] * 3 * u * u * t + p[2] * 3 * u * t * t + p[3] * t ** 3
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new()
    n = 56
    rows = []
    half = 0.48
    for i in range(n + 1):
        t = i / n
        c = bez(t)
        d = (bez(min(1.0, t + 1e-3)) - bez(max(0.0, t - 1e-3))).normalized()
        side_v = Vector((d.z, 0.0, -d.x)).normalized()   # across, in the band's plane
        w = half * min(1.0, 0.45 + t * 2.2)              # narrow where it goes through the ring
        rows.append((bm.verts.new(c - side_v * w), bm.verts.new(c + side_v * w), t))
    for (a0, a1, t0), (b0, b1, t1) in zip(rows, rows[1:]):
        f = bm.faces.new((a0, a1, b1, b0))
        for loop, (x, y) in zip(f.loops, ((0.0, t0), (1.0, t0), (1.0, t1), (0.0, t1))):
            loop[uv].uv = (x, y * 11.0)
    o = put('strap', bm, 'strap')
    sol = o.modifiers.new('thick', 'SOLIDIFY')
    sol.thickness = 0.13
    sol.offset = 0.0
    return o


for s in (-1, 1):
    lug = box('lug', min(s * 6.70, s * 7.12), max(s * 6.70, s * 7.12), -0.22, 0.22, 4.45, 4.98,
              'chrome', r=0.08)
    hole = cylinder('lug-hole', 0.12, -0.5, 0.5, 'hole', axis='y', loc=(s * 6.98, 0.0, 4.70))
    cut(lug, hole)
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=False, radius=0.36, segments=48)
    rg = put('split-ring', bm, 'chrome', loc=(s * 7.05, -0.02, 4.40))
    rg.rotation_euler = (math.radians(75.0), math.radians(18.0 * s), 0.0)
    sk = rg.modifiers.new('wire', 'SKIN')
    for v in rg.data.skin_vertices[0].data:
        v.radius = (0.035, 0.035)
    sub = rg.modifiers.new('round', 'SUBSURF')
    sub.levels = sub.render_levels = 1
    strap(s)


# ---- the lens, on the camera's front: seen only from above, as the camera
# lies in the drawer (render_top) -- in the views from behind it is not
# rendered, the pictures of the camera at the eye having been made without
# it. A standard one: chrome mount, the aperture ring, the rubber focusing
# ring with its ridges, the front of the barrel and its filter ring with a
# white line round it.

LENS_Z = 2.75
lens_parts = []


def lens_part(o):
    lens_parts.append(o)
    o.hide_render = True
    return o


lens_part(cylinder('lens-mount', 2.30, 1.52, 1.80, 'chrome', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))
lens_part(cylinder('lens-aperture', 2.55, 1.80, 2.62, 'black', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))
lens_part(cylinder('lens-focus', 2.78, 2.62, 4.62, 'rubber', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))
for k in range(11):
    y0 = 2.72 + k * 0.175
    lens_part(cylinder('lens-ridge', 2.84, y0, y0 + 0.07, 'rubber', n=96, axis='y',
                       loc=(0.0, 0.0, LENS_Z)))
lens_part(cylinder('lens-front', 2.62, 4.62, 5.58, 'black', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))
lens_part(cylinder('lens-line', 2.635, 5.30, 5.34, 'white', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))
lens_part(cylinder('lens-filter', 2.70, 5.58, 5.76, 'black', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))
lens_part(cylinder('lens-glass', 2.30, 5.70, 5.72, 'glass', n=96, axis='y', loc=(0.0, 0.0, LENS_Z)))


# ---- the light: the desk's lamp from above and a little left, a soft box
# over and in front for the metal's tops, a strip at the back right for
# its edges, and the desk's own light, dim, from behind the eye

def area(name, loc, target, size, size_y, radiance, colour=(1.0, 1.0, 1.0)):
    li = bpy.data.lights.new(name, 'AREA')
    li.shape = 'RECTANGLE'
    li.size = size
    li.size_y = size_y
    li.energy = radiance * math.pi * size * size_y
    li.color = colour
    o = bpy.data.objects.new(name, li)
    COLL.objects.link(o)
    o.location = loc
    d = Vector(target) - Vector(loc)
    o.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return o


MID = (0.0, 0.0, 5.0)
area('lamp', (-16.0, -14.0, 30.0), MID, 34.0, 34.0, 3.4, (1.0, 0.96, 0.90))
area('over', (0.0, 46.0, 26.0), (0.0, 0.0, 6.0), 90.0, 34.0, 1.9, (1.0, 1.0, 1.0))
area('strip', (26.0, 22.0, 30.0), MID, 5.0, 44.0, 6.0, (0.95, 0.97, 1.0))
area('desk', (0.0, -40.0, -6.0), MID, 90.0, 40.0, 0.22, (1.0, 0.92, 0.82))
area('room', (6.0, -46.0, 14.0), MID, 60.0, 30.0, 0.50, (1.0, 0.98, 0.95))

world = bpy.data.worlds.new('room')
SCENE.world = world
world.use_nodes = True
wn = world.node_tree.nodes
wl = world.node_tree.links
bg = wn['Background']
tc = wn.new('ShaderNodeTexCoord')
sep = wn.new('ShaderNodeSeparateXYZ')
wl.new(tc.outputs['Generated'], sep.inputs[0])
ramp = wn.new('ShaderNodeValToRGB')
ramp.color_ramp.elements[0].position = 0.35
ramp.color_ramp.elements[0].color = (0.010, 0.009, 0.008, 1.0)
ramp.color_ramp.elements[1].position = 0.75
ramp.color_ramp.elements[1].color = (0.10, 0.10, 0.105, 1.0)
mr = wn.new('ShaderNodeMapRange')
mr.inputs['From Min'].default_value = -1.0
mr.inputs['From Max'].default_value = 1.0
wl.new(sep.outputs['Z'], mr.inputs['Value'])
wl.new(mr.outputs['Result'], ramp.inputs['Fac'])
wl.new(ramp.outputs['Color'], bg.inputs['Color'])
bg.inputs['Strength'].default_value = 1.0

# ---- the eye: draw.c's view, orthographic

cam_data = bpy.data.cameras.new('eye')
cam_data.type = 'ORTHO'
cam_data.ortho_scale = CW / UNITS
cam_data.sensor_fit = 'HORIZONTAL'
cam = bpy.data.objects.new('eye', cam_data)
COLL.objects.link(cam)
SCENE.camera = cam
up = Vector((0.0, SIN_T, COS_T))
look = Vector((0.0, COS_T, -SIN_T))
cam.location = up * ((V0 - CH * 0.5) / UNITS) - look * 60.0
cam.rotation_euler = (math.pi * 0.5 - math.asin(SIN_T), 0.0, 0.0)
cam_data.clip_start = 1.0
cam_data.clip_end = 200.0

r = SCENE.render
r.engine = 'CYCLES'
r.resolution_x, r.resolution_y, r.resolution_percentage = W, H, 100
r.film_transparent = True
r.image_settings.file_format = 'PNG'
r.image_settings.color_mode = 'RGBA'
r.image_settings.color_depth = '8'
r.use_persistent_data = True
c = SCENE.cycles
c.device = 'CPU'
c.samples = SAMPLES
c.use_adaptive_sampling = False
c.seed = 0
c.max_bounces = 10
c.glossy_bounces = 6
c.transmission_bounces = 8
c.transparent_max_bounces = 16
c.use_denoising = not QUICK and bool(getattr(bpy.app.build_options, 'openimagedenoise', True))
if c.use_denoising:
    c.denoiser = 'OPENIMAGEDENOISE'
SCENE.view_settings.view_transform = 'AgX'
for look_name in ('AgX - Punchy', 'Punchy'):
    try:
        SCENE.view_settings.look = look_name
        break
    except TypeError:
        pass
SCENE.view_settings.exposure = 0.0


# ---- the camera as it is set

def set_state(knurl=0.0, power=1.0, press=0.0, knob_on=False):
    dial.rotation_euler = (0.0, 0.0, knurl * 2.0 * math.pi / KNURLS)
    tab.rotation_euler = (0.0, 0.0, -tab_angle(power))
    button.location.z = -press * 0.12
    for o in bpy.data.objects:
        if o.name.startswith('knob'):
            o.hide_render = not knob_on


def render(path, box_px=None):
    """The picture, whole or (box_px: x0, y0, x1, y1, from its top left) just
    that part of it."""
    r.use_border = box_px is not None
    r.use_crop_to_border = box_px is not None
    if box_px:
        x0, y0, x1, y1 = box_px
        r.border_min_x, r.border_max_x = x0 / W, x1 / W
        r.border_min_y, r.border_max_y = 1.0 - y1 / H, 1.0 - y0 / H
    r.filepath = path
    bpy.ops.render.render(write_still=True)


def load(path):
    """A picture rendered, as rows from the top, 0..255, straight alpha."""
    im = bpy.data.images.load(path, check_existing=False)
    w, h = im.size
    a = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    return np.flipud((a.reshape(h, w, 4) * 255.0 + 0.5).clip(0, 255).astype(np.uint8))


def units_box(points, m=0.0):
    """The box in the picture, in pixels, round some points (and m units more)."""
    us = [proj(p) for p in points]
    x0 = min(u for u, v in us) - m
    x1 = max(u for u, v in us) + m
    y0 = min(v for u, v in us) - m
    y1 = max(v for u, v in us) + m
    return (max(0, int(math.floor(x0 * PX))), max(0, int(math.floor(y0 * PX))),
            min(W, int(math.ceil(x1 * PX))), min(H, int(math.ceil(y1 * PX))))


def ring_pts(cx, cy, rad, z0, z1, n=24):
    return [(cx + rad * math.cos(a), cy + rad * math.sin(a), z)
            for a in (2 * math.pi * i / n for i in range(n)) for z in (z0, z1)]


# where what moves is, in the picture, a margin round it
MARGIN = 10.0
DIAL_BOX = units_box(ring_pts(DIAL_X, DIAL_Y, DIAL_R + 0.1, PLATE_Z1, DIAL_Z1 + 0.05), MARGIN)
SHUT_BOX = units_box(ring_pts(SHUT_X, SHUT_Y, 1.05, PLATE_Z1, 6.50), MARGIN)
KNOB_BOX = units_box([(KNOB_X0 + KNOB_STEP + dx, PLATE_BACK - 0.15, KNOB_Z + dz)
                      for dx in (-0.25, 0.25) for dz in (-0.25, 0.25)], 6.0)
DOME_BOX = units_box(ring_pts(COUNT_X, COUNT_Y, 0.46, PLATE_Z1, 6.30), 3.0)
EYE_BOX = units_box([(dx, -2.2, EYE_Z + dz) for dx in (-1.15, 1.15) for dz in (-1.0, 1.0)]
                    + [(dx, -1.6, EYE_Z + dz) for dx in (-1.15, 1.15) for dz in (-1.0, 1.0)], 2.0)
# the slot, open; the tape in it, and its shadow under it; the tape alone,
# at the slot's mouth
SLOT_BOX = units_box([(SLOT_X + sx * (SLOT_HW + 0.08), y, SLOT_Z + sz * (SLOT_HH + 0.08))
                      for sx in (-1, 1) for sz in (-1, 1) for y in (PLATE_BACK - 0.04, PLATE_BACK + 0.4)],
                     3.0)
# (up to the plate's top: the chrome over the slot has the tape's label in it)
TAPE_IN_BOX = units_box([(SLOT_X + sx, y, z) for sx in (-TW * 0.5 - 0.15, TW * 0.5 + 0.7)
                         for y in (PLATE_BACK - TAPE_OUT - 0.05, PLATE_BACK)
                         for z in (PLATE_Z0 - 0.35, PLATE_Z1 - 0.02)], 3.0)
TAPE_BOX = units_box([(SLOT_X + sx * TW * 0.5, y, z) for sx in (-1, 1)
                      for y in (PLATE_BACK - TD - 0.01, PLATE_BACK) for z in (TAPE_Z, TAPE_Z + TT + 0.01)],
                     3.0)

os.makedirs(OUT, exist_ok=True)

# ---- the camera lying in the drawer, seen from straight above, its lens
# away from the eye: lit as the drawer's things are, by the lamp above and
# to the right of the screen, with no strap -- camera-top.png, and what
# draw.c needs to know of it in camera-top.h

TOP_PX = 40   # pixels to the centimetre


def render_top():
    for o in bpy.data.objects:
        if o.type == 'LIGHT':
            o.hide_render = True
        elif o.name.startswith('strap'):
            o.hide_render = True
    for o in lens_parts:
        o.hide_render = False
    tape_show(False)
    set_state()
    area('top-lamp', (20.0, 24.0, 44.0), (0.0, 1.5, 3.0), 30.0, 30.0, 3.2, (1.0, 0.96, 0.90))
    area('top-fill', (-10.0, -6.0, 60.0), (0.0, 1.5, 3.0), 70.0, 70.0, 0.45)
    cd = bpy.data.cameras.new('above')
    cd.type = 'ORTHO'
    span_x, span_y, mid_y = 15.8, 11.0, 1.8
    cd.ortho_scale = span_x
    cd.sensor_fit = 'HORIZONTAL'
    cd.clip_start = 1.0
    cd.clip_end = 200.0
    above = bpy.data.objects.new('above', cd)
    COLL.objects.link(above)
    above.location = (0.0, mid_y, 60.0)
    above.rotation_euler = (0.0, 0.0, 0.0)
    SCENE.camera = above
    tw, th = int(span_x * TOP_PX), int(span_y * TOP_PX)
    r.resolution_x, r.resolution_y = tw, th
    render(os.path.join(OUT, 'camera-top.png'))
    mid_x, mid_y_px = tw * 0.5, th * 0.5 - (0.075 - mid_y) * TOP_PX
    with open(os.path.join(OUT, 'camera-top.h'), 'w') as f:
        f.write('\n'.join([
            '/* camera-top.h: made by camera.py with camera-top.png -- do not edit, make again.',
            ' * The camera lying in the drawer, seen from straight above, its lens away',
            ' * from the eye: how many of its picture\'s pixels the body is across (lugs',
            ' * not counted), and where the body\'s middle is in it. */',
            '',
            '#define CAMERA_TOP_BODY_PX %.1f' % (2.0 * HW * TOP_PX),
            '#define CAMERA_TOP_MID_X %.1f' % mid_x,
            '#define CAMERA_TOP_MID_Y %.1f' % mid_y_px,
            '',
        ]))
    print('camera.py: camera-top.png %d by %d' % (tw, th))


if ONLY == 'top':
    render_top()
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix='desk-camera-')   # (the renders, before they are packed)

set_state()
render(os.path.join(TMP, 'base.png'))
if ONLY == 'base':
    print('camera.py: the camera whole is in %s' % os.path.join(TMP, 'base.png'))
    sys.exit(0)

pieces = {}
for k in (1, 2, 3):
    set_state(knurl=k / 4.0)
    render(os.path.join(TMP, 'knurl-%d.png' % k), DIAL_BOX)
for i, p in enumerate((0.0, 0.25, 0.5, 0.75)):
    set_state(power=p)
    render(os.path.join(TMP, 'power-%d.png' % i), SHUT_BOX)
for i in (1, 2, 3):
    set_state(press=i / 3.0)
    render(os.path.join(TMP, 'press-%d.png' % i), SHUT_BOX)
set_state()

# the knob, alone, its shadow on the plate as the plate's only picture
hidden = {}
for o in bpy.data.objects:
    if o.type in ('MESH', 'FONT'):
        hidden[o.name] = o.hide_render
        o.hide_render = not (o.name.startswith('knob') or o is top)
top.is_shadow_catcher = True
render(os.path.join(TMP, 'knob.png'), KNOB_BOX)
top.is_shadow_catcher = False
for name, was in hidden.items():
    bpy.data.objects[name].hide_render = was
set_state()

# the counter's glass, alone, as it reflects what is round it
for o in bpy.data.objects:
    if o.type in ('MESH', 'FONT') and o is not dome:
        o.visible_camera = False
dome.hide_render = False
render(os.path.join(TMP, 'dome.png'), DOME_BOX)
dome.hide_render = True
for o in bpy.data.objects:
    if o.type in ('MESH', 'FONT'):
        o.visible_camera = True

# the eyepiece, its glass seen through
glass.data.materials[0] = MAT['see']
see_hole.hide_render = False
render(os.path.join(TMP, 'see.png'), EYE_BOX)
glass.data.materials[0] = MAT['glass']
see_hole.hide_render = True

# the slot with its flap pushed in, nothing in it; and the tape in it, all
# but its spine's end in, its shadow on the plate under it
flap.hide_render = True
render(os.path.join(TMP, 'slot.png'), SLOT_BOX)
tape_show(True)
tape_at(TD - TAPE_OUT)
render(os.path.join(TMP, 'tape-in.png'), TAPE_IN_BOX)
# the tape alone at the slot's mouth, as it is brought to it and pushed in
# (the rest still there for the light, but not seen)
tape_at(0.0)
for o in bpy.data.objects:
    if o.type in ('MESH', 'FONT') and o not in tape_parts:
        o.visible_camera = False
render(os.path.join(TMP, 'tape.png'), TAPE_BOX)
for o in bpy.data.objects:
    if o.type in ('MESH', 'FONT'):
        o.visible_camera = True
tape_show(False)
flap.hide_render = False


# ---- packing: the pieces into one picture, and where they go

def crop_alpha(img, x, y):
    ys, xs = np.nonzero(img[:, :, 3] > 0)
    if len(xs) == 0:
        return img[:1, :1], x, y
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return img[y0:y1, x0:x1], x + x0, y + y0


base = load(os.path.join(TMP, 'base.png'))
base_c, bx, by = crop_alpha(base, 0, 0)
items = [('base', base_c, bx, by)]


def patch(name, path, box_px):
    items.append((name, load(path), box_px[0], box_px[1]))


for k in (1, 2, 3):
    patch('knurl%d' % k, os.path.join(TMP, 'knurl-%d.png' % k), DIAL_BOX)
for i in range(4):
    patch('power%d' % i, os.path.join(TMP, 'power-%d.png' % i), SHUT_BOX)
for i in (1, 2, 3):
    patch('press%d' % i, os.path.join(TMP, 'press-%d.png' % i), SHUT_BOX)
k_img, kx, ky = crop_alpha(load(os.path.join(TMP, 'knob.png')), KNOB_BOX[0], KNOB_BOX[1])
items.append(('knob', k_img, kx, ky))
d_img, dx, dy = crop_alpha(load(os.path.join(TMP, 'dome.png')), DOME_BOX[0], DOME_BOX[1])
items.append(('dome', d_img, dx, dy))
patch('see', os.path.join(TMP, 'see.png'), EYE_BOX)
patch('slot', os.path.join(TMP, 'slot.png'), SLOT_BOX)
patch('tapein', os.path.join(TMP, 'tape-in.png'), TAPE_IN_BOX)
t_img, tx, ty = crop_alpha(load(os.path.join(TMP, 'tape.png')), TAPE_BOX[0], TAPE_BOX[1])
items.append(('tape', t_img, tx, ty))

# shelves, as wide as the camera
aw = max(img.shape[1] for _, img, _, _ in items)
placed, sx, sy, shelf = [], 0, 0, 0
for name, img, x, y in items:
    h, w = img.shape[:2]
    if sx + w > aw:
        sx, sy, shelf = 0, sy + shelf + 2, 0
    placed.append((name, img, x, y, sx, sy))
    sx += w + 2
    shelf = max(shelf, h)
ah = sy + shelf
atlas = np.zeros((ah, aw, 4), dtype=np.uint8)
for name, img, x, y, ax, ay in placed:
    h, w = img.shape[:2]
    atlas[ay:ay + h, ax:ax + w] = img
im = bpy.data.images.new('camera', aw, ah, alpha=True)
im.pixels.foreach_set((np.flipud(atlas).astype(np.float32) / 255.0).ravel())
im.filepath_raw = os.path.join(OUT, 'camera.png')
im.file_format = 'PNG'
im.save()


def u(v):
    return '%.1f' % v


eye = [proj((sx_ * GLASS_HW, -1.65, EYE_Z + sz * GLASS_HH)) for sx_ in (-1, 1) for sz in (-1, 1)]
ex0, ex1 = min(p[0] for p in eye), max(p[0] for p in eye)
ey0, ey1 = min(p[1] for p in eye), max(p[1] for p in eye)
dial_top = proj((DIAL_X, DIAL_Y, DIAL_Z1))
dial_foot = proj((DIAL_X, DIAL_Y, PLATE_Z1))
count_mid = proj((COUNT_X, COUNT_Y, 6.105))
shut_mid = proj((SHUT_X, SHUT_Y, 6.44))
collar_mid = proj((SHUT_X, SHUT_Y, 6.24))
knob0 = proj((KNOB_X0, PLATE_BACK - 0.13, KNOB_Z))
knob_drawn = proj((KNOB_X0 + KNOB_STEP, PLATE_BACK - 0.13, KNOB_Z))
plate_far = proj((0.0, 1.45, PLATE_Z1))
plate_near = proj((0.0, PLATE_BACK, PLATE_Z1))
plate_foot = proj((0.0, PLATE_BACK, PLATE_Z0))
roof = proj((0.0, -0.42, 8.0))
lugs = proj((-7.12, 0.0, 4.98))
slot_top = proj((SLOT_X, PLATE_BACK, SLOT_Z + SLOT_HH))
slot_l = proj((SLOT_X - SLOT_HW - 0.12, PLATE_BACK, SLOT_Z))
slot_r = proj((EJECT_X + 0.16, PLATE_BACK, SLOT_Z))
spine_foot = proj((SLOT_X, PLATE_BACK - TAPE_OUT, TAPE_Z))

lines = [
    '/* camera-map.h: made by camera.py with camera.png -- do not edit, make again.',
    ' * Where each piece of the camera is in camera.png (ax, ay, w by h) and where',
    ' * it goes on the camera, in its pictures\' pixels (x, y): CAMERA_PX to the unit',
    ' * of draw.c\'s camera (DESK_FINDER_CAMERA_W by _H). The base is the camera',
    ' * whole -- the dial\'s knurling at its first turn, on, its button up, no knob in',
    ' * the self-timer\'s slide, the counter\'s window bare, no tape; the rest go over',
    ' * it. And where its parts are, in the camera\'s units. */',
    '',
    '#define CAMERA_PX %d' % PX,
    '',
    'struct camera_piece {',
    '\tint ax, ay, w, h;   /* in camera.png */',
    '\tint x, y;           /* on the camera */',
    '};',
    '',
]
by_name = {name: (ax, ay, img.shape[1], img.shape[0], x, y) for name, img, x, y, ax, ay in placed}


def piece(name):
    return '{ %d, %d, %d, %d, %d, %d }' % by_name[name]


lines.append('static const struct camera_piece CAMERA_BASE = %s;' % piece('base'))
lines.append('/* the dial\'s knurling turned a quarter, a half, three quarters of a knurl on */')
lines.append('static const struct camera_piece CAMERA_KNURL[3] = { %s };'
             % ', '.join(piece('knurl%d' % k) for k in (1, 2, 3)))
lines.append('/* the power lever at 0 (O), a quarter, a half and three quarters of the way to I */')
lines.append('static const struct camera_piece CAMERA_POWER[4] = { %s };'
             % ', '.join(piece('power%d' % i) for i in range(4)))
lines.append('/* the shutter button a third, two thirds and all the way down */')
lines.append('static const struct camera_piece CAMERA_PRESS[3] = { %s };'
             % ', '.join(piece('press%d' % i) for i in (1, 2, 3)))
lines.append('/* the self-timer\'s knob and its shadow, the knob at two seconds */')
lines.append('static const struct camera_piece CAMERA_KNOB = %s;' % piece('knob'))
lines.append('/* the frame counter\'s glass: what it reflects, over its figures */')
lines.append('static const struct camera_piece CAMERA_DOME = %s;' % piece('dome'))
lines.append('/* the eyepiece, its glass seen through */')
lines.append('static const struct camera_piece CAMERA_SEE = %s;' % piece('see'))
lines.append('/* the tape\'s slot, its flap pushed in and nothing in it */')
lines.append('static const struct camera_piece CAMERA_SLOT = %s;' % piece('slot'))
lines.append('/* the tape in it, all but its spine\'s end in, its shadow under it */')
lines.append('static const struct camera_piece CAMERA_TAPE_IN = %s;' % piece('tapein'))
lines.append('/* the tape alone, at the slot\'s mouth, its far end at it */')
lines.append('static const struct camera_piece CAMERA_TAPE = %s;' % piece('tape'))
lines += [
    '',
    '#define CAMERA_KNOB_STEP %s   /* the knob from one setting to the next, units */'
    % u(KNOB_STEP * UNITS),
    '#define CAMERA_KNOB_X %s      /* where it is off */' % u(knob0[0]),
    '#define CAMERA_KNOB_Y %s' % u(knob0[1]),
    '#define CAMERA_DIAL_X %s      /* the dial\'s top: its middle, its round */' % u(dial_top[0]),
    '#define CAMERA_DIAL_Y %s' % u(dial_top[1]),
    '#define CAMERA_DIAL_RX %s' % u((DIAL_R - 0.06) * UNITS),
    '#define CAMERA_DIAL_RY %s' % u((DIAL_R - 0.06) * UNITS * SIN_T),
    '#define CAMERA_DIAL_FOOT %s   /* where its foot is, at its middle */' % u(dial_foot[1]),
    '#define CAMERA_DIAL_HUB %s    /* its hub, across */' % u(0.30 * UNITS),
    '#define CAMERA_DIAL_MARK_R %s /* the pictures, from the middle */' % u(0.88 * UNITS),
    '#define CAMERA_DIAL_MARK %s   /* and across */' % u(0.50 * UNITS),
    '#define CAMERA_DIAL_STEP %s   /* a step of the dial, in radians */' % '0.95',
    '#define CAMERA_KNURLS %d' % KNURLS,
    '#define CAMERA_COUNT_X %s     /* the frame counter\'s window: its middle, its round */'
    % u(count_mid[0]),
    '#define CAMERA_COUNT_Y %s' % u(count_mid[1]),
    '#define CAMERA_COUNT_RX %s' % u(0.33 * UNITS),
    '#define CAMERA_COUNT_RY %s' % u(0.33 * UNITS * SIN_T),
    '#define CAMERA_SHUT_X %s      /* the shutter button\'s top */' % u(shut_mid[0]),
    '#define CAMERA_SHUT_Y %s' % u(shut_mid[1]),
    '#define CAMERA_SHUT_R %s' % u(0.375 * UNITS),
    '#define CAMERA_COLLAR_Y %s    /* the power collar\'s, and its lever\'s reach */'
    % u(collar_mid[1]),
    '#define CAMERA_COLLAR_R %s' % u(1.0 * UNITS),
    '#define CAMERA_EYE_X %s       /* the eyepiece\'s glass */' % u(ex0),
    '#define CAMERA_EYE_Y %s' % u(ey0),
    '#define CAMERA_EYE_W %s' % u(ex1 - ex0),
    '#define CAMERA_EYE_H %s' % u(ey1 - ey0),
    '#define CAMERA_PLATE_FAR %s   /* the top plate: its far edge, its near, its foot */'
    % u(plate_far[1]),
    '#define CAMERA_PLATE_NEAR %s' % u(plate_near[1]),
    '#define CAMERA_PLATE_FOOT %s' % u(plate_foot[1]),
    '#define CAMERA_ROOF %s        /* the prism\'s roof */' % u(roof[1]),
    '#define CAMERA_TAPE_CLIP %s   /* the slot\'s top edge: of the tape going in, none' % u(slot_top[1]),
    '                              * of it over this is seen */',
    '#define CAMERA_TAPE_PUSH %s   /* how far up it goes, pushed in from the mouth */'
    % u((TD - TAPE_OUT) * SIN_T * UNITS),
    '#define CAMERA_SLOT_X0 %s     /* the slot, the tape in it and the eject mark */'
    % u(slot_l[0]),
    '#define CAMERA_SLOT_X1 %s' % u(slot_r[0]),
    '#define CAMERA_SLOT_Y0 %s' % u(slot_top[1] - 3.0),
    '#define CAMERA_SLOT_Y1 %s' % u(spine_foot[1] + 2.0),
    '#define CAMERA_LEFT %s        /* the camera\'s ends, lugs and all */' % u(lugs[0]),
    '#define CAMERA_RIGHT %s' % u(CW - lugs[0]),
    '',
]
with open(os.path.join(OUT, 'camera-map.h'), 'w') as f:
    f.write('\n'.join(lines))
shutil.rmtree(TMP, ignore_errors=True)
print('camera.py: camera.png %d by %d, %d pieces' % (aw, ah, len(placed)))
render_top()
