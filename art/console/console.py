"""The Control Panel at the back left of the desk (map_console, draw.c),
stowed, made in Blender: the room's tracer leaves it out of the desk's
sheet, and this picture of it, rendered once, is what is on the desk
instead. It slides out only in its own view, which is still traced.

Stowed, it has gone back into the desk's far edge as far as leaves the
front of it in view (cc_stow_at) -- the nameplate's ledge on the plinth,
the walnut cheeks either end, the row of pilot lenses along the panel's
lip, LOCK and SLEEP at the front of its left half, and at the right the
front of the walnut lid over the guarded set, with the two at the front of
it (NIGHT and DISPLAY) -- and nothing of it past the far edge exists. All of it is the tracer's to the hundredth: the
case in crinkle-black steel, the fittings in brass, glass lenses in brass
bezels. It stands in the room the pictures of the desk's things share
(art/case.py).

What the tracer cannot afford and Blender can: the crinkle of the paint,
the walnut's grain, the brass gone over where nobody touches it, the glass
of the lenses and the caps, and the light the desk and the machine throw
into each other.

What is printed on it -- the nameplate and its legend, the lamps lit
behind the lenses -- is drawn over it by draw.c (console_idle), as it was
over the traced set.

How much of it shows, and how the lamp falls on it, follow the shape of the
screen and the width of the desk, so it is rendered for the twelve shapes
and three desks, and laid by its cheeks (front, top, the right one's side
and the left one's inside), its panel, its plinth and the desk at its foot
(console_case_pieces in draw.c).

    blender -b --factory-startup -P art/console/console.py -- art/console
        [--quick] [--samples N] [--only A[,A...]] [--hx H[,H...]] [--ppu N]
        [--dump DIR]

gives console-case.bin and console-case.h. Made with Blender 4.5.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import case  # noqa: E402

# ---- the set: map_console and RM_CC_* in draw.c, on the desk the room was
# designed on (it moves out with the corner of a wider one)
CC_X0 = -15.20
CC_X1 = -5.10
CC_PLATE_X0 = -15.05
CC_CHEEK = 0.72
CC_PLINTH = 0.50
CC_LEDGE = 0.95
CC_LEDGE_X1 = -11.50
CC_PY0, CC_PZ0, CC_PY1, CC_PZ1 = 0.95, -5.70, 3.30, -8.20
CC_TY0, CC_TZ0, CC_TY1, CC_TZ1 = 3.30, -8.20, 5.918, -9.447
CC_ZB = -10.60
CC_BODY_Z0 = -8.95        # the carcass's front, behind the tower's back
CC_SKIN = 0.17
CC_PLINTH_Z0 = CC_PZ0 - 0.05
CC_PW_R = 0.285
CC_PW_RING = 0.395
CC_PW_DOME = 0.50
CC_BTN_S = -1.00          # CC[CC_LOCK].s and CC[CC_SLEEP].s: the row at the lip
CC_LOCK_U = -3.05         # CC[CC_LOCK].u
CC_SLEEP_U = -1.15        # CC[CC_SLEEP].u
# the lid over the guarded set (CC_LID_*), and the four on it (CC_LB_*, CC[CC_AIR..])
CC_LID_U0, CC_LID_U1 = 0.42, 3.36
CC_LID_S0, CC_LID_S1 = -1.30, 1.57
CC_LID_N0, CC_LID_N1 = 0.40, 0.54
CC_LID_HINGE_S = 1.64
CC_LB_R, CC_LB_RING, CC_LB_DOME = 0.325, 0.45, 0.637
CC_ON_LID = ((1.155, 1.07, 'amber'), (2.625, 1.07, 'green'), (1.155, -0.80, 'blue'),
             (2.625, -0.80, 'white'))
# how far round it the desk is rendered for its shadow -- to the left, away
# from the lamp (what its cheeks throw goes on back past the far edge), to
# the right, and in front of its ledge
SPILL = (3.0, 1.5, 1.5)


def place(room):
    """The set's left and right ends, out with the corner of a wider desk,
    and the z it carries stowed (cc_dz(0, aspect): back far enough for a
    fifth of the way up its panel, just above the lamps, to be on the line
    of the frame's top)."""
    g = room.grow
    k = 0.15
    y = CC_PY0 + (CC_PY1 - CC_PY0) * k
    z = CC_PZ0 + (CC_PZ1 - CC_PZ0) * k
    dz = -(z - room.hidden_z(y))
    return CC_X0 - g, CC_X1 - g, dz


class Face:
    """cc_face_build: a sloped face of the set -- its middle, and across it,
    up it towards the back, and out of it."""

    def __init__(self, x0, x1, y0, z0, y1, z1):
        dy, dz = y1 - y0, z1 - z0
        ln = math.hypot(dy, dz)
        self.o = ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5)
        self.s = (0.0, dy / ln, dz / ln)
        self.n = (0.0, -self.s[2], self.s[1])
        self.half = ln * 0.5
        # the turn about x that puts a box's y on n (and its z on -s)
        self.turn = math.atan2(self.n[2], self.n[1])

    def at(self, u, n, s, dz=0.0):
        """cc_world: a point on the face, in the room."""
        return (self.o[0] + u,
                self.o[1] + self.n[1] * n + self.s[1] * s,
                self.o[2] + self.n[2] * n + self.s[2] * s + dz)


def faces(room):
    x0, x1, dz = place(room)
    return (Face(x0, x1, CC_PY0, CC_PZ0, CC_PY1, CC_PZ1),
            Face(x0, x1, CC_TY0, CC_TZ0, CC_TY1, CC_TZ1), dz)


# ---- materials, their patterns fixed to the set (an empty at its middle)

TEX = []


def coords(g):
    tc = g.n.new('ShaderNodeTexCoord')
    TEX.append(tc)
    return tc.outputs['Object']


def finish(g, b, base, rough, geo, dust, edge, metal=False):
    """shade_surface's wear and dust over a base."""
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


def crinkle(name, rough):
    """MID_CC_STEEL and MID_CC_PANEL: crinkle-finish black, a hard shell
    over a surface that is not quite flat -- the finish put on a panel so
    that it will not shine."""
    m = case.principled(name, (0.04, 0.04, 0.045), coat=0.12, coat_rough=0.40)
    g = case.Graph(m.node_tree)
    b = m.node_tree.nodes['Principled BSDF']
    geo = g.geometry()
    p = coords(g)
    t = g.smooth(g.noise(p, 30.0, detail=3.0), 0.30, 0.70, interp='LINEAR')
    crin = g.smooth(g.noise(p, 90.0, detail=2.0), 0.25, 0.75, interp='LINEAR')
    base = g.mix(t, (0.026, 0.027, 0.030), (0.062, 0.063, 0.069))
    base = g.vmath('SCALE', base, scale=g.math('ADD', 0.88, g.math('MULTIPLY', crin, 0.26)))
    n = g.bump(g.noise(p, 170.0, detail=3.0, rough=0.6), 0.12, 0.003)
    g.link(n, b.inputs['Normal'])
    g.link(n, b.inputs['Coat Normal'])
    finish(g, b, base, rough, geo, 0.65, 1.0)
    return m


def brass():
    """MID_CC_BRASS: turned, lacquered once, handled since."""
    m = case.principled('brass', (0.70, 0.55, 0.24), metal=1.0)
    g = case.Graph(m.node_tree)
    b = m.node_tree.nodes['Principled BSDF']
    geo = g.geometry()
    p = coords(g)
    brush = g.smooth(g.noise(p, 40.0, detail=1.0), 0.30, 0.70, interp='LINEAR')
    # less handled the higher it is (lac), and the lip of the panel is low
    tarn = g.math('MULTIPLY', g.smooth(g.noise(p, 4.2, detail=3.0), 0.40, 0.68), 0.62)
    base = g.mix(g.math('MULTIPLY', tarn, 0.62), (0.800, 0.620, 0.266), (0.352, 0.286, 0.150))
    base = g.vmath('SCALE', base, scale=g.math('ADD', 0.90, g.math('MULTIPLY', brush, 0.18)))
    rough = g.math('ADD', g.math('ADD', 0.245, g.math('MULTIPLY', brush, 0.13)),
                   g.math('MULTIPLY', tarn, 0.30))
    finish(g, b, base, rough, geo, 0.35, 1.0, metal=True)
    return m


def walnut():
    """MID_CC_CHEEK: walnut_at, its rings across the depth of the cheek, a
    satin coat, and the set's been taken hold of by them."""
    m = case.principled('walnut', (0.15, 0.07, 0.03), coat=0.24, coat_rough=0.23)
    g = case.Graph(m.node_tree)
    b = m.node_tree.nodes['Principled BSDF']
    geo = g.geometry()
    p = coords(g)
    # rings across the depth of the cheek (walnut_at's u is the room's z),
    # seven to ten to a unit, wandering a little; latewood a narrow band
    wave = g.n.new('ShaderNodeTexWave')
    wave.wave_type = 'BANDS'
    wave.bands_direction = 'Y'          # Blender's y: the room's depth
    wave.wave_profile = 'SIN'
    wave.inputs['Scale'].default_value = 8.5
    wave.inputs['Distortion'].default_value = 1.6
    wave.inputs['Detail'].default_value = 2.0
    wave.inputs['Detail Scale'].default_value = 0.8
    g.link(p, wave.inputs['Vector'])
    ring = g.math('POWER', wave.outputs['Fac'], 2.7)
    tone = g.smooth(g.noise(p, 0.6, detail=2.0), 0.35, 0.65, interp='LINEAR')
    light = g.vmath('SCALE', (0.234, 0.114, 0.049),
                    scale=g.math('ADD', 0.80, g.math('MULTIPLY', tone, 0.44)))
    base = g.mix(ring, light, (0.052, 0.021, 0.009))
    pore = g.math('POWER', g.noise(p, 60.0, detail=1.0), 4.0)
    base = g.vmath('SCALE', base, scale=g.math('SUBTRACT', 0.92, g.math('MULTIPLY', pore, 0.3)))
    rough = g.math('ADD', 0.26, g.math('MULTIPLY', ring, 0.13))
    n = g.bump(g.noise(p, 26.0, detail=2.0), 0.06, 0.003)
    g.link(n, b.inputs['Normal'])
    finish(g, b, base, rough, geo, 1.0, 1.5)
    return m


def lens(name, tint):
    """The cast glass of a lens or a cap: lit by the compositor over the
    top, so here it has only to read as glass with a cold filament behind
    it."""
    m = case.principled(name, tuple(c * 0.34 for c in tint), rough=0.075, coat=1.0,
                        coat_rough=0.035)
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Emission Color'].default_value = (*tint, 1.0)
    b.inputs['Emission Strength'].default_value = 0.10
    return m


MAT = {
    'steel': crinkle('steel', 0.68),
    'panel': crinkle('panel', 0.78),
    'brass': brass(),
    'walnut': walnut(),
    'amber': lens('amber', (0.620, 0.310, 0.060)),
    'green': lens('green', (0.110, 0.420, 0.160)),
    'blue': lens('blue', (0.110, 0.230, 0.560)),
    'white': lens('white', (0.720, 0.700, 0.640)),
}


def on_face(name, f, u, n, s, dz, half, r, mat, seg=6):
    """A box on a face, in its frame: half across, out and up it."""
    return case.rbox(name, f.at(u, n, s, dz), half, r, mat, seg=seg, rot_x=f.turn)


def cyl_on(name, f, u, n, s, dz, radius, half, mat, r=0.0):
    """sd_cyl_y in a face's frame: about the face's normal."""
    return case.cylinder(name, f.at(u, n, s, dz), f.n, radius, half, mat, r=r)


def button(f, u, s, dz, mat, parts, base=CC_SKIN, ring=CC_PW_RING, cap_r=CC_PW_R,
           dome=CC_PW_DOME):
    """A button (cc_button): the wide ring that carries its word, the skirt,
    and the cap -- a shallow dome, a big sphere with all but its crown cut
    away, the crown 0.20 off its base whatever its width."""
    parts.append(cyl_on('ring', f, u, base + 0.045, s, dz, ring, 0.045, MAT['brass'], r=0.012))
    parts.append(cyl_on('skirt', f, u, base + 0.075, s, dz, cap_r + 0.045, 0.075, MAT['brass'],
                        r=0.012))
    cap = case.sphere('cap', f.at(u, base + 0.20 - dome, s, dz), dome, mat)
    keep = cyl_on('crown', f, u, base + 0.05, s, dz, cap_r, 0.16, mat)
    case.cut(cap, keep, 'INTERSECT')
    parts.append(cap)


def build(room):
    x0, x1, dz = place(room)
    panel, tower, _ = faces(room)
    cx, chx = (x0 + x1) * 0.5, (x1 - x0) * 0.5
    inner = chx - CC_CHEEK
    ref = bpy.data.objects.new('set', None)
    case.COLL.objects.link(ref)
    ref.location = case.B(cx, 1.5, CC_PZ0 + dz)
    for tc in TEX:
        tc.object = ref
    parts = []

    # the plinth, set back at the sides and the front, and run out at the
    # front as the nameplate's ledge as far across as the plate goes
    plz0, plz1 = CC_PLINTH_Z0, CC_ZB + 0.30
    parts.append(case.rbox('plinth', (cx, CC_PLINTH * 0.5, (plz0 + plz1) * 0.5 + dz),
                           (chx - 0.10, CC_PLINTH * 0.5, (plz0 - plz1) * 0.5), 0.06,
                           MAT['steel'], seg=4))
    lx0, lx1 = CC_PLATE_X0 - room.grow + 0.10, CC_LEDGE_X1 - room.grow
    lz0, lz1 = CC_PZ0 + CC_LEDGE, CC_PLINTH_Z0 - 0.20
    parts.append(case.rbox('ledge', ((lx0 + lx1) * 0.5, CC_PLINTH * 0.5, (lz0 + lz1) * 0.5 + dz),
                           ((lx1 - lx0) * 0.5, CC_PLINTH * 0.5, (lz0 - lz1) * 0.5), 0.06,
                           MAT['steel'], seg=4))
    # the carcass under both faces, and the faces
    parts.append(case.rbox('carcass', (cx, (CC_PLINTH + CC_TY1 * 0.72) * 0.5,
                                       (CC_BODY_Z0 + CC_ZB) * 0.5 + dz),
                           (inner, (CC_TY1 * 0.72 - CC_PLINTH) * 0.5,
                            (CC_BODY_Z0 - CC_ZB) * 0.5),
                           0.10, MAT['steel'], seg=4))
    parts.append(on_face('panel', panel, 0.0, 0.0, 0.0, dz, (inner, 0.17, panel.half), 0.06,
                         MAT['panel']))
    parts.append(on_face('tower', tower, 0.0, 0.0, 0.0, dz, (inner, 0.17, tower.half), 0.06,
                         MAT['panel']))
    # walnut cheeks either end, tall at the back, low at the front
    for sx in (x0 + CC_CHEEK * 0.5, x1 - CC_CHEEK * 0.5):
        prof = case.rbox('cheek', (sx, (CC_PLINTH + CC_TY1) * 0.5, (CC_PZ1 + CC_ZB) * 0.5 + dz),
                         (CC_CHEEK * 0.5, (CC_TY1 - CC_PLINTH) * 0.5, (CC_PZ1 - CC_ZB) * 0.5),
                         0.10, MAT['walnut'], seg=6)
        front = case.rbox('cheek', (sx, (CC_PLINTH + CC_PY1) * 0.5, (CC_PZ0 + CC_PZ1) * 0.5 + dz),
                          (CC_CHEEK * 0.5, (CC_PY1 - CC_PLINTH) * 0.5, (CC_PZ0 - CC_PZ1) * 0.5),
                          0.10, MAT['walnut'], seg=6)
        parts += [prof, front]

    # LOCK and SLEEP at the lip, under the two switches: what else is on the
    # left of the panel -- the switches, the brass that parts it -- is past the
    # far edge
    button(panel, CC_LOCK_U, CC_BTN_S, dz, MAT['blue'], parts)
    button(panel, CC_SLEEP_U, CC_BTN_S, dz, MAT['white'], parts)

    # and at the right the guarded set's walnut lid, shut, on its sub-plate, with
    # the brass lip along its free edge and the four on it: from the desk, its
    # front and the two at the front of it show
    lu, ls = (CC_LID_U0 + CC_LID_U1) * 0.5, (CC_LID_S0 + CC_LID_S1) * 0.5
    hn = (CC_LID_N0 + CC_LID_N1) * 0.5
    parts.append(on_face('subplate', panel, lu, CC_SKIN + 0.035,
                         (CC_LID_S0 + CC_LID_HINGE_S + 0.04) * 0.5, dz,
                         ((CC_LID_U1 - CC_LID_U0) * 0.5 + 0.04, 0.035,
                          (CC_LID_HINGE_S - CC_LID_S0 + 0.08) * 0.5), 0.05, MAT['panel']))
    parts.append(on_face('lid', panel, lu, hn, ls, dz,
                         ((CC_LID_U1 - CC_LID_U0) * 0.5, (CC_LID_N1 - CC_LID_N0) * 0.5,
                          (CC_LID_S1 - CC_LID_S0) * 0.5), 0.035, MAT['walnut']))
    parts.append(on_face('lip', panel, lu, CC_LID_N1 - 0.015, CC_LID_S0 + 0.07, dz,
                         (0.25, 0.03, 0.045), 0.02, MAT['brass']))
    for u, s, mat in CC_ON_LID:
        button(panel, u, s, dz, MAT[mat], parts, base=CC_LID_N1, ring=CC_LB_RING,
               cap_r=CC_LB_R, dome=CC_LB_DOME)

    # the row of pilot lenses along the lip
    for i in range(6):
        u = -inner + 0.70 + i * (2.0 * (inner - 0.70) / 5.0)
        s = -panel.half + 0.136
        parts.append(cyl_on('bezel', panel, u, CC_SKIN + 0.035, s, dz, 0.13, 0.035,
                            MAT['brass'], r=0.01))
        parts.append(case.sphere('lens', panel.at(u, CC_SKIN + 0.095, s, dz), 0.095,
                                 MAT['green' if i & 1 else 'amber']))

    # and nothing of it past the desk's far edge
    case.behind_desk(room, parts)
    return parts


def reach(room):
    """The front of the set that shows, and the desk round it as far as its
    shadow could reach."""
    x0, x1, dz = place(room)
    z0 = CC_PZ0 + CC_LEDGE + dz
    pts = [(x, y, z) for x in (x0, x1) for y in (0.0, CC_PY1)
           for z in (room.zf, z0)]
    pts += [(x, 0.0, z) for x in (x0 - SPILL[0], x1 + SPILL[1])
            for z in (room.zf, z0 + SPILL[2])]
    return pts


def centre(room):
    x0, x1, dz = place(room)
    return ((x0 + x1) * 0.5, 1.2, CC_PZ0 + dz)


case.run('console', 'The Control Panel, stowed,', [16.0, 24.0, 36.0], build, reach,
         centre)
