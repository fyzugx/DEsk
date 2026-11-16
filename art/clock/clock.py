"""The clock's case on the desk (ROOM_GADGETS[DESK_GADGET_CLOCK], draw.c),
made in Blender: the room's tracer leaves it out of the desk's sheet, and
this picture of it, rendered once, is what is on the desk instead.

The case is the tracer's to the hundredth: a block of Bakelite 10.32 by 1.86
by 0.95 room units, its edges rounded 0.14, a well cut into its top for the
display and the keys, and a slab of smoked glass in the well. It stands in
the room the pictures of the desk's things share (art/case.py): lit by its
lamp, seen through its camera, put through its tone curve, with the shadow
it throws on the desk and what of the room it keeps off the desk round its
foot.

What the tracer cannot afford and Blender can: the Bakelite marbled right
through, its arrises rubbed back to brown where hands have been, a little
dust in the well's corners, the lamp itself in the glass, the light the
desk throws back up into the case's front, the lip of the well rounded.

It stands in the middle of the desk's far edge, so a wider desk moves only
the lamp, out and up about the middle of the near edge, as far as the far
edge has gone back: the same light, from the same way. It is rendered for
the twelve shapes of screen and one desk, and laid by its top, its front
and the desk at its foot (clock_case_pieces in draw.c).

    blender -b --factory-startup -P art/clock/clock.py -- art/clock
        [--quick] [--samples N] [--only A[,A...]] [--ppu N] [--dump DIR]

gives clock-case.bin and clock-case.h. Made with Blender 4.5.
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import case  # noqa: E402

# the clock: ROOM_GADGETS[DESK_GADGET_CLOCK], room_gadgets
CX, CZ_IN, CHX, CHZ, CH, INSET = 0.50, 0.68, 5.16, 0.93, 0.95, 0.20
EDGE_R = 0.14
# how far round it the desk is rendered for its shadow -- to the left,
# away from the lamp, to the right, and in front of its foot
SPILL = (2.0, 1.0, 1.2)


def cz(room):
    """gadget_z: as far back as leaves its top's far edge in view."""
    return room.hidden_z(CH) + CZ_IN


def bakelite():
    """MID_GADGET_BODY, as the tracer would have it with time to spare:
    nearly black, marbled with brown right through, a dull shell; its
    arrises rubbed back to brown, and dust settled where it is out of the
    duster's way."""
    m = case.principled('bakelite', (0.05, 0.04, 0.035))
    nt = m.node_tree
    g = case.Graph(nt)
    b = nt.nodes['Principled BSDF']
    geo = g.geometry()
    pos = geo.outputs['Position']
    # the marbling: room_fbm(x 9, z 9 + y 4), with the swirl of a moulding
    t = g.smooth(g.noise(pos, 5.0, detail=6.0, rough=0.58, distort=0.9), 0.30, 0.70,
                 interp='LINEAR')
    base = g.mix(g.math('MULTIPLY', t, 0.8), (0.036, 0.030, 0.028), (0.082, 0.062, 0.050))
    # its arrises, and not worn evenly: where a hand goes, the back hardly
    # at all
    edge = g.arris(geo)
    hands = g.smooth(g.noise(pos, 1.3, detail=3.0), 0.38, 0.66)
    rub = g.math('MULTIPLY', edge, g.math('ADD', g.math('MULTIPLY', hands, 0.8), 0.2))
    base = g.mix(g.math('MULTIPLY', rub, 0.55), base, (0.15, 0.095, 0.060))
    settle = g.settle(geo, 1.0)
    base = g.mix(g.math('MULTIPLY', settle, 0.16), base, case.DUST)
    g.link(base, b.inputs['Base Color'])
    # the rubbed arrises smoother, the burr gone; the dust rougher
    rough = g.math('ADD', g.math('ADD', 0.26, g.math('MULTIPLY', t, 0.10)),
                   g.math('SUBTRACT', g.math('MULTIPLY', settle, 0.22),
                          g.math('MULTIPLY', rub, 0.12)))
    g.link(rough, b.inputs['Roughness'])
    # what is left of the shine it came out of the mould with
    b.inputs['Coat Weight'].default_value = 0.34
    b.inputs['Coat Roughness'].default_value = 0.185
    # the skin of a moulding: not quite flat, and fine pits in it
    fine = g.math('ADD', g.noise(pos, 140.0, detail=2.0),
                  g.math('MULTIPLY', g.noise(pos, 7.0, detail=1.0), 0.6))
    n = g.bump(fine, 0.18, 0.004)
    g.link(n, b.inputs['Normal'])
    g.link(n, b.inputs['Coat Normal'])
    return m


MAT = {
    'bakelite': bakelite(),
    # MID_GADGET_GLASS: smoked, polished; the face is printed over it
    'glass': case.principled('glass', (0.024, 0.024, 0.027), rough=0.05, coat=1.0,
                             coat_rough=0.03, ior=1.49),
}


def build(room):
    z = cz(room)
    body = case.rbox('case', (CX, CH * 0.5, z), (CHX, CH * 0.5, CHZ), EDGE_R, MAT['bakelite'])
    fx, fz = CHX - INSET, CHZ - INSET
    well = case.rbox('well', (CX, CH, z), (fx, 0.10, fz), 0.05, MAT['bakelite'], seg=6)
    case.cut(body, well)
    # the lip of the well, which the tracer leaves a knife edge
    lip = body.modifiers.new('lip', 'BEVEL')
    lip.width = 0.022
    lip.segments = 4
    lip.limit_method = 'ANGLE'
    lip.angle_limit = math.radians(40.0)
    lip.harden_normals = True
    wn = body.modifiers.new('normals', 'WEIGHTED_NORMAL')
    wn.mode = 'FACE_AREA'
    wn.keep_sharp = True
    glass = case.rbox('glass', (CX, CH - 0.12, z), (fx, 0.05, fz), 0.04, MAT['glass'], seg=4)
    return [body, glass]


def reach(room):
    """The case, and the desk round it as far as its shadow could reach --
    off to the left, away from the lamp, and a little in front of its foot."""
    z = cz(room)
    x0, x1, zb, zf = CX - CHX, CX + CHX, z - CHZ, z + CHZ
    pts = [(x, y, zz) for x in (x0, x1) for zz in (zb, zf) for y in (0.0, CH)]
    pts += [(x, 0.0, zz) for x in (x0 - SPILL[0], x1 + SPILL[1]) for zz in (zb, zf + SPILL[2])]
    return pts


def centre(room):
    return (CX, CH * 0.5, cz(room))


case.run('clock', 'The clock\'s case', [case.RM_HX_MIN], build, reach, centre)
