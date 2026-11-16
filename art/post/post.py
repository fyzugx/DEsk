"""The post box at the back right of the desk (map_post, draw.c), made in
Blender: the room's tracer leaves it out of the desk's sheet, and this
picture of it, rendered once, is what is on the desk instead. Its door is
shut on the desk; it opens only in the box's own view, which is still
traced.

The box is the tracer's to the hundredth: cast iron under red coach enamel,
3.8 by 1.6 room units on a dark foot, 4.05 to the shoulder, its door the
front of it with a slot through its head in a brass frame under a brass
hood, a brass escutcheon below, and a cap 0.62 thick on top, tilted so the
weather runs off the front. It stands as far back as leaves the plate on
its cap in view (RM_PB_CZ), at the desk's back right corner, which moves
out with a wider desk. It stands in the room the pictures of the desk's
things share (art/case.py).

What the tracer cannot afford and Blender can: the enamel's orange peel and
the chips knocked out of it down to the iron, its arrises worn, the brass
tarnished where nobody touches it, the light the desk and the box throw
into each other, and the slot dark because there is a box behind it.

What is printed on it -- the plate, the hood's shadow down the door, the
post that has not gone all the way in -- is drawn over it by draw.c
(desk_draw_post_idle), as it was over the traced box.

How much of its front and its left side shows, and how the lamp falls on
them, follow the shape of the screen and the width of the desk, so it is
rendered for the twelve shapes and three desks, and laid by its cap, its
front, its side and the desk at its foot (post_case_pieces in draw.c).

    blender -b --factory-startup -P art/post/post.py -- art/post
        [--quick] [--samples N] [--only A[,A...]] [--hx H[,H...]] [--ppu N]
        [--dump DIR]

gives post-case.bin and post-case.h. Made with Blender 4.5.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import case  # noqa: E402

# ---- the box: map_post and RM_PB_* in draw.c
PB_X0 = 9.60          # RM_PB_CX on the desk the room was designed on
PB_HX = 1.90
PB_HZ = 0.80
PB_PLINTH = 0.34
PB_SHOULDER = 4.05
PB_CAP_T = 0.62
PB_TILT = 0.185
PB_SLOT_HS = 0.17
PB_SLOT_S = -0.34
PB_DOOR_Y0 = PB_PLINTH + 0.16
PB_FS_Y = 2.75
PB_FS_HU = 1.25
PB_FS_HH = 0.11
PB_FS_FR = 0.09
PB_HOOD_OUT = 0.18
PB_HOOD_TILT = 0.30
PB_PLATE_S0 = PB_SLOT_S + PB_SLOT_HS + 0.30
PB_PLATE_S1 = PB_HZ - 0.10
PB_TOP_F = 0.30
PB_TOP_S = PB_PLATE_S0 + (PB_PLATE_S1 - PB_PLATE_S0) * PB_TOP_F
PB_TOP_N = PB_CAP_T * 0.5 + 0.012
PB_TOP_Y = (PB_SHOULDER + PB_CAP_T * 0.5 + PB_TOP_N * math.cos(PB_TILT)
            - PB_TOP_S * math.sin(PB_TILT))
PB_TOP_DZ = -(PB_TOP_N * math.sin(PB_TILT) + PB_TOP_S * math.cos(PB_TILT))
PB_TOP_GAP = 0.02
# how far round it the desk is rendered for its shadow -- to the left, to
# the right, away from the lamp, and in front of its foot
SPILL = (1.2, 2.6, 1.4)


def place(room):
    """RM_PB_CX and RM_PB_CZ: at the back right corner, and as far back as
    leaves the lettering on the plate in view."""
    cx = PB_X0 + room.grow
    cz = room.zf + PB_TOP_DZ + PB_TOP_GAP + (case.RM_FOV_X * 0.5 / room.aspect) * PB_TOP_Y
    return cx, cz


def rot_x(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def cap_to_world(room, u, n, s):
    """pb_cap_to_world: a point in the cap's own frame -- across it, out of
    its top, towards its front -- in the room."""
    cx, cz = place(room)
    q = rot_x((u, n, s), PB_TILT)
    return (q[0] + cx, q[1] + PB_SHOULDER + PB_CAP_T * 0.5, q[2] + cz)


# ---- materials. Their patterns are fixed to the box (an empty at its
# middle), not to the room, so that every picture of it has the same chips
# in the same places and two of them mixed are one box.

TEX = []


def coords(g):
    tc = g.n.new('ShaderNodeTexCoord')
    TEX.append(tc)
    return tc.outputs['Object']


def finish(g, b, base, rough, geo, dust, edge, metal=False):
    """shade_surface's wear and dust over a base: arrises paint has thinned
    on and metal polished, and dust where it faces up."""
    wear = g.math('MULTIPLY', g.arris(geo), min(edge * 0.4, 1.0))
    if metal:
        base = g.mix(g.math('MULTIPLY', wear, 0.45), base, (0.95, 0.80, 0.42))
        rough = g.math('SUBTRACT', rough, g.math('MULTIPLY', wear, 0.11))
    else:
        base = g.mix(g.math('MULTIPLY', wear, 0.55), base, g.vmath('SCALE', base, scale=1.30))
        rough = g.math('SUBTRACT', rough, g.math('MULTIPLY', wear, 0.14))
    settle = g.settle(geo, dust)
    base = g.mix(g.math('MULTIPLY', settle, 0.16), base, case.DUST)
    rough = g.math('ADD', rough, g.math('MULTIPLY', settle, 0.22), clamp=True)
    g.link(base, b.inputs['Base Color'])
    g.link(rough, b.inputs['Roughness'])


def enamel():
    """MID_POST_RED: cast iron under coach enamel, repainted more than once
    and not always well; chipped to the iron, not worn thin."""
    m = case.principled('enamel', (0.27, 0.035, 0.02))
    g = case.Graph(m.node_tree)
    b = m.node_tree.nodes['Principled BSDF']
    geo = g.geometry()
    p = coords(g)
    t = g.smooth(g.noise(p, 6.0, detail=3.0), 0.30, 0.70, interp='LINEAR')
    base = g.mix(g.math('MULTIPLY', t, 0.85), (0.232, 0.024, 0.016), (0.318, 0.048, 0.026))
    orange = g.smooth(g.noise(p, 1.3, detail=2.0), 0.30, 0.70, interp='LINEAR')
    base = g.mix(g.math('MULTIPLY', orange, 0.32), base, (0.186, 0.038, 0.024))
    # chips (nick_at): the top few per cent of a noise, down to the iron
    chip = g.smooth(g.noise(p, 5.2, detail=1.0), 0.705, 0.745)
    base = g.mix(g.math('MULTIPLY', chip, 0.85), base, (0.088, 0.064, 0.052))
    rough = g.math('ADD', g.math('ADD', 0.34, g.math('MULTIPLY', t, 0.14)),
                   g.math('MULTIPLY', chip, 0.42))
    coat = g.math('MULTIPLY', 0.46, g.math('SUBTRACT', 1.0, g.math('MULTIPLY', chip, 0.9)))
    g.link(coat, b.inputs['Coat Weight'])
    crough = g.math('ADD', g.math('ADD', 0.12, g.math('MULTIPLY', t, 0.09)),
                    g.math('MULTIPLY', chip, 0.30))
    g.link(crough, b.inputs['Coat Roughness'])
    # orange peel: coach enamel was brushed on, and it shows
    n = g.bump(g.noise(p, 34.0, detail=2.0), 0.10, 0.004)
    g.link(n, b.inputs['Normal'])
    g.link(n, b.inputs['Coat Normal'])
    finish(g, b, base, rough, geo, 1.0, 2.4)
    return m


def dark():
    """MID_POST_DARK: the foot, and the liner at the back of the box."""
    m = case.principled('dark', (0.05, 0.04, 0.033), coat=0.14, coat_rough=0.34)
    g = case.Graph(m.node_tree)
    b = m.node_tree.nodes['Principled BSDF']
    geo = g.geometry()
    p = coords(g)
    t = g.smooth(g.noise(p, 14.0, detail=3.0), 0.30, 0.70, interp='LINEAR')
    base = g.mix(t, (0.036, 0.026, 0.022), (0.074, 0.054, 0.044))
    rough = g.math('ADD', 0.62, g.math('MULTIPLY', t, 0.14))
    n = g.bump(g.noise(p, 120.0, detail=2.0), 0.08, 0.002)
    g.link(n, b.inputs['Normal'])
    finish(g, b, base, rough, geo, 1.0, 2.0)
    return m


def brass():
    """MID_CC_BRASS: turned, lacquered once, handled since; gone further
    over where nobody touches it."""
    m = case.principled('brass', (0.70, 0.55, 0.24), metal=1.0)
    g = case.Graph(m.node_tree)
    b = m.node_tree.nodes['Principled BSDF']
    geo = g.geometry()
    p = coords(g)
    brush = g.smooth(g.noise(p, 40.0, detail=1.0), 0.30, 0.70, interp='LINEAR')
    tarn = g.math('MULTIPLY', g.smooth(g.noise(p, 4.2, detail=3.0), 0.40, 0.68), 0.95)
    base = g.mix(g.math('MULTIPLY', tarn, 0.62), (0.800, 0.620, 0.266), (0.352, 0.286, 0.150))
    base = g.vmath('SCALE', base, scale=g.math('ADD', 0.90, g.math('MULTIPLY', brush, 0.18)))
    rough = g.math('ADD', g.math('ADD', 0.245, g.math('MULTIPLY', brush, 0.13)),
                   g.math('MULTIPLY', tarn, 0.30))
    finish(g, b, base, rough, geo, 0.35, 1.0, metal=True)
    return m


MAT = {'enamel': enamel(), 'dark': dark(), 'brass': brass()}


def build(room):
    cx, cz = place(room)
    ref = bpy.data.objects.new('box', None)
    case.COLL.objects.link(ref)
    ref.location = case.B(cx, 2.3, cz)
    for tc in TEX:
        tc.object = ref
    parts = []

    # the foot
    parts.append(case.rbox('foot', (cx, PB_PLINTH * 0.5, cz),
                           (PB_HX + 0.16, PB_PLINTH * 0.5, PB_HZ + 0.16), 0.07, MAT['dark']))

    # the body: a shell, with the room inside it that a slot needs behind it
    y0, y1 = PB_PLINTH, PB_SHOULDER
    m0, m1 = y0 + 0.14, y1 - 0.26
    shell = case.rbox('body', (cx, (y0 + y1) * 0.5, cz), (PB_HX, (y1 - y0) * 0.5, PB_HZ), 0.16,
                      MAT['enamel'])
    cav = case.rbox('cavity', (cx, (m0 + m1) * 0.5, cz + 0.08),
                    (PB_HX - 0.26, (m1 - m0) * 0.5, PB_HZ - 0.18), 0.10, MAT['enamel'], seg=4)
    mouth = case.rbox('mouth', (cx, (m0 + m1) * 0.5, cz + PB_HZ),
                      (PB_HX - 0.26, (m1 - m0) * 0.5, 0.50), 0.0, MAT['enamel'])
    case.cut(shell, cav)
    case.cut(shell, mouth)
    parts.append(shell)
    parts.append(case.rbox('liner', (cx, (m0 + m1) * 0.5, cz - PB_HZ + 0.30),
                           (PB_HX - 0.30, (m1 - m0) * 0.5 - 0.04, 0.05), 0.04, MAT['dark']))

    # the door, shut, with the slot through its head
    hy, hz = y0 + 0.16, cz + PB_HZ
    dh = m1 + 0.16 - hy
    door = case.rbox('door', (cx, hy + dh * 0.5, hz), (PB_HX - 0.06, dh * 0.5, 0.10), 0.07,
                     MAT['enamel'])
    slot = case.rbox('slot', (cx, hy + PB_FS_Y, hz), (PB_FS_HU, PB_FS_HH, 0.30), 0.0,
                     MAT['enamel'])
    case.cut(door, slot)
    parts.append(door)
    # its brass frame, just proud of the door
    frame = case.rbox('frame', (cx, hy + PB_FS_Y, hz + 0.12),
                      (PB_FS_HU + PB_FS_FR, PB_FS_HH + PB_FS_FR, 0.035), 0.03, MAT['brass'],
                      seg=4)
    inner = case.rbox('inner', (cx, hy + PB_FS_Y, hz + 0.12), (PB_FS_HU, PB_FS_HH, 0.30), 0.0,
                      MAT['brass'])
    case.cut(frame, inner)
    parts.append(frame)
    # the hood over it, sloping down and out
    py, pz = hy + PB_FS_Y + PB_FS_HH + PB_FS_FR - 0.01, hz + 0.14
    off = rot_x((0.0, 0.0, PB_HOOD_OUT * 0.5), PB_HOOD_TILT)
    parts.append(case.rbox('hood', (cx, py + off[1], pz + off[2]),
                           (PB_FS_HU + PB_FS_FR + 0.04, 0.022, PB_HOOD_OUT * 0.5), 0.015,
                           MAT['brass'], seg=3, rot_x=PB_HOOD_TILT))
    # the escutcheon
    parts.append(case.rbox('key', (cx, hy + dh * 0.42, hz + 0.07), (0.24, 0.32, 0.05), 0.09,
                           MAT['brass'], seg=6))

    # the cap, tilted so the weather runs off the front of it
    parts.append(case.rbox('cap', (cx, PB_SHOULDER + PB_CAP_T * 0.5, cz),
                           (PB_HX + 0.14, PB_CAP_T * 0.5, PB_HZ + 0.14), 0.13, MAT['enamel'],
                           rot_x=PB_TILT))

    # and none of it past the top board's far edge
    case.behind_desk(room, parts)
    return parts


def reach(room):
    """The box, and the desk round it as far as its shadow could reach."""
    cx, cz = place(room)
    x0, x1 = cx - PB_HX - 0.30, cx + PB_HX + 0.30
    pts = [(x, y, z) for x in (x0, x1) for y in (0.0, PB_SHOULDER + PB_CAP_T + 0.2)
           for z in (cz - PB_HZ - 0.4, cz + PB_HZ + 0.5)]
    pts += [(x, 0.0, z) for x in (x0 - SPILL[0], x1 + SPILL[1])
            for z in (cz - PB_HZ - 0.4, cz + PB_HZ + 0.5 + SPILL[2])]
    return pts


def centre(room):
    cx, cz = place(room)
    return (cx, 2.3, cz)


case.run('post', 'The post box', [16.0, 24.0, 36.0], build, reach, centre)
