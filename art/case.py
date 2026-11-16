"""What the pictures made in Blender of the things standing on the desk have
in common: the room they stand in, as the tracer has it (draw.c) -- its
camera for a screen of a given shape over a desk of a given width, its
lamps, the desk under them and the wall behind, the light of the room round
them and its tone curve -- and how a picture of one is made, with the shadow
it throws on the desk, and put out for draw.c to lay over the desk's sheet
(room_cases_paint).

Imported by the script that makes each thing (art/clock/clock.py,
art/post/post.py, art/console/console.py), which says what the thing is
and where, and is run as

    blender -b --factory-startup -P art/THING/THING.py -- art/THING
        [--quick] [--samples N] [--only A[,A...]] [--hx H[,H...]] [--ppu N]
        [--keep DIR] [--dump DIR]

--quick renders with few samples, to look at; --only renders just those
shapes of screen (width over height), --hx just those desks (half widths);
--ppu is the picture's pixels to a room unit (100: the desk's 45 points to
one, at up to twice that); --keep keeps each picture in DIR as it is made,
and takes any already there instead of making it again, so that a run cut
short goes on where it stopped; --dump keeps each picture's three renders
(the thing, the desk without it and with it) in DIR as passes-<shape>.npz.

How much of a thing's front and side shows, how the lamp falls on it and
where its shadow goes follow the shape of the screen -- the desk is as deep
as the screen is tall -- and, for a thing out at a corner, the width of the
desk -- the desk is as wide as the screen is in points, the things at the
back corners move out with the corners and the lamp out and up with the
desk -- so each is rendered for a dozen shapes, and for some widths, and
draw.c mixes the ones either side of the screen it is on. Each picture is
laid piece by piece, a piece to a plane of the thing (its top, its front,
its side, the desk at its foot), through where four corners of that plane
were seen when it was rendered (through the camera of the room it was
rendered for) and are seen now; THING-case.h has what each picture is of
and where in that room's frame it is, and THING-case.bin the pictures, one
PNG after another.

Made with Blender 4.5. Sizes in room units.
"""

import json
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
OUT = os.path.abspath(ARGV[0] if ARGV and not ARGV[0].startswith('--') else '.')
QUICK = '--quick' in ARGV


def arg(name, default):
    return ARGV[ARGV.index(name) + 1] if name in ARGV else default


SAMPLES = int(arg('--samples', 32 if QUICK else 512))
DESK_SAMPLES = max(16, SAMPLES // 2)
PPU = float(arg('--ppu', 100.0))
KEEP = arg('--keep', '')
# the shapes of screen: from a tall one to an ultrawide, closer together
# where screens are (5:4, 4:3, 16:10, 16:9, 21:9)
ASPECTS = [0.30, 0.42, 0.5625, 0.75, 1.00, 1.333, 1.60, 1.778, 2.10, 2.50, 3.00, 4.00]

# ---- the room's numbers (draw.c)
RM_ZN = 7.2
RM_FOV_X = 0.78
RM_DR_PULL = 3.30
RM_HX_MIN = 16.0
ICON_EXPOSURE = 0.62
LIT_KEY_IRRADIANCE = 5.6
LIT_KEY_COL = (1.00, 0.89, 0.69)
LIT_KEY_DIR = (0.05, 0.90, 0.43)
LIT_FILL_DIR = (-0.66, 0.30, 0.69)
LIT_FILL_COL = (0.36, 0.45, 0.60)
LIT_RIM_DIR = (-0.10, 0.16, -0.98)
LIT_RIM_COL = (0.96, 0.90, 0.80)
ROOM_LAMP_POS0 = (8.0, 12.5, 3.5)
ROOM_LAMP_AIM = (-0.16, -0.97, -0.18)
ROOM_LAMP_REF0 = 16.4
ROOM_LAMP_RADIUS0 = 2.30
ROOM_LAMP_CONE_IN = 0.22
ROOM_LAMP_CONE_OUT = 0.80
SHELF_LAMP_COL = (1.00, 0.86, 0.62)
SHELF_LAMP_AIM = (0.0, -0.94, 0.34)
SHELF_LAMP_Y = 7.85
SHELF_LAMP_IRR = 26.0
RM_FLOOR = -19.0


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


class Room:
    """The room for a screen of this shape over a desk hx wide (half, in
    room units): room_set_aspect, room_cam_build, and the lamp moved out
    with the desk (ROOM_LAMP_K)."""

    def __init__(self, aspect, hx=RM_HX_MIN):
        self.aspect = aspect
        self.hx = hx
        self.grow = hx - RM_HX_MIN
        self.k = hx / RM_HX_MIN
        self.zf = RM_ZN - 2.0 * hx / aspect
        z0, z1 = self.zf, RM_ZN + RM_DR_PULL + 0.90
        cam_hx = max((z1 - z0) * 0.5 * aspect, hx)
        self.eye = (0.0, 2.0 * cam_hx / RM_FOV_X, (z0 + z1) * 0.5)
        self.fov_y = RM_FOV_X / aspect
        # RM_SH_ZF, where the lamps under the bookcase's board are
        self.sh_zf = self.zf + (RM_FOV_X * 0.5 / aspect) * RM_FLOOR + 0.60 - 1.10
        # the frame, as many pixels to a unit of the desk top as asked
        self.w = int(round(PPU * 2.0 * cam_hx))
        self.h = int(round(self.w / aspect))

    def hidden_z(self, y):
        """room_hidden_z: the line of sight along the top of the frame."""
        return self.zf + (RM_FOV_X * 0.5 / self.aspect) * y

    def project(self, x, y, z):
        """room_project: where a point lands in the frame, 0..1 from its top left."""
        dw = self.eye[1] - y
        return (0.5 + (x - self.eye[0]) / dw / RM_FOV_X,
                0.5 + (z - self.eye[2]) / dw / self.fov_y)


def B(x, y, z):
    """A point in the room (x right, y up, z towards the eye) in Blender's
    (x right, y away, z up)."""
    return Vector((x, -z, y))


SCENE = bpy.context.scene
COLL = SCENE.collection
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)


# ---- materials

def principled(name, base, rough=0.5, spec=0.5, coat=0.0, coat_rough=0.05, ior=1.5,
               metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*base, 1.0)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular IOR Level'].default_value = spec
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Coat Roughness'].default_value = coat_rough
    b.inputs['IOR'].default_value = ior
    b.inputs['Metallic'].default_value = metal
    return m


def sock(node, ident):
    """A socket by its identifier: the Mix node has an A and a B for each
    type it mixes."""
    for s in list(node.inputs) + list(node.outputs):
        if s.identifier == ident:
            return s
    raise KeyError(ident)


class Graph:
    """A few node-graph sums, so that a material reads as what it computes."""

    def __init__(self, nt):
        self.nt = nt
        self.n = nt.nodes
        self.ln = nt.links

    def link(self, a, b):
        self.ln.new(a, b)

    def math(self, op, a, b=None, clamp=False):
        m = self.n.new('ShaderNodeMath')
        m.operation = op
        m.use_clamp = clamp
        for i, v in enumerate((a, b)):
            if v is None:
                continue
            if isinstance(v, (int, float)):
                m.inputs[i].default_value = v
            else:
                self.link(v, m.inputs[i])
        return m.outputs[0]

    def vmath(self, op, a, b=None, scale=None):
        m = self.n.new('ShaderNodeVectorMath')
        m.operation = op
        for i, v in enumerate((a, b)):
            if v is None:
                continue
            if isinstance(v, tuple):
                m.inputs[i].default_value = v
            else:
                self.link(v, m.inputs[i])
        if scale is not None:
            if isinstance(scale, (int, float)):
                m.inputs['Scale'].default_value = scale
            else:
                self.link(scale, m.inputs['Scale'])
        return m.outputs['Value' if op in ('DOT_PRODUCT', 'LENGTH', 'DISTANCE') else 'Vector']

    def smooth(self, v, lo, hi, clamp=True, interp='SMOOTHSTEP'):
        """0 at lo, 1 at hi (either way round), smoothly."""
        m = self.n.new('ShaderNodeMapRange')
        m.interpolation_type = interp
        m.clamp = clamp
        self.link(v, m.inputs['Value'])
        m.inputs['From Min'].default_value = lo
        m.inputs['From Max'].default_value = hi
        return m.outputs['Result']

    def mix(self, t, a, b):
        m = self.n.new('ShaderNodeMix')
        m.data_type = 'RGBA'
        for s, v in ((sock(m, 'Factor_Float'), t), (sock(m, 'A_Color'), a), (sock(m, 'B_Color'), b)):
            if isinstance(v, (int, float)):
                s.default_value = v
            elif isinstance(v, tuple):
                s.default_value = (*v, 1.0)
            else:
                self.link(v, s)
        return sock(m, 'Result_Color')

    def noise(self, vec, scale, detail=2.0, rough=0.5, distort=0.0):
        t = self.n.new('ShaderNodeTexNoise')
        t.inputs['Scale'].default_value = scale
        t.inputs['Detail'].default_value = detail
        t.inputs['Roughness'].default_value = rough
        t.inputs['Distortion'].default_value = distort
        self.link(vec, t.inputs['Vector'])
        return t.outputs['Fac']

    def geometry(self):
        return self.n.new('ShaderNodeNewGeometry')

    def arris(self, geo, radius=0.05):
        """How much the surface here is the rounded-over edge of something:
        where the true normal turns away from one rounded over its arrises
        (the tracer's curvature, scene_curvature)."""
        bev = self.n.new('ShaderNodeBevel')
        bev.samples = 8
        bev.inputs['Radius'].default_value = radius
        turn = self.vmath('DOT_PRODUCT', bev.outputs['Normal'], geo.outputs['Normal'])
        return self.smooth(turn, 0.995, 0.93)

    def settle(self, geo, dust):
        """shade_surface's dust: a film of it on whatever faces up, and more
        in the corners the duster does not get into."""
        ao = self.n.new('ShaderNodeAmbientOcclusion')
        ao.only_local = True
        ao.samples = 8
        ao.inputs['Distance'].default_value = 0.12
        nz = self.n.new('ShaderNodeSeparateXYZ')
        self.link(geo.outputs['Normal'], nz.inputs[0])
        up = self.math('POWER', self.math('MAXIMUM', nz.outputs['Z'], 0.0), 2.0)
        corner = self.math('MULTIPLY', self.math('SUBTRACT', 1.0, ao.outputs['AO']), 0.9)
        return self.math('MULTIPLY', self.math('MULTIPLY', self.math('ADD', 0.38, corner), up),
                         dust, clamp=True)

    def bump(self, height, strength, distance):
        b = self.n.new('ShaderNodeBump')
        b.inputs['Strength'].default_value = strength
        b.inputs['Distance'].default_value = distance
        self.link(height, b.inputs['Height'])
        return b.outputs['Normal']


DUST = (0.42, 0.40, 0.37)


# ---- shapes

def rbox(name, centre, half, r, mat, seg=10, rot_x=0.0):
    """sd_round_box: a box half-extents `half` (room x, y, z), every edge
    rounded r -- and never thinner than r, which is what the tracer's is
    where r is more than half of it; turned rot_x about its own x (rot_x in
    the tracer's sense: the room point is the box's turned by it)."""
    half = tuple(max(h, r) for h in half)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * 2.0 * half[0], v.co.y * 2.0 * half[2], v.co.z * 2.0 * half[1]))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    COLL.objects.link(o)
    o.location = B(*centre)
    # a turn about the room's x is one about Blender's x, the other way
    # round (room z is Blender's -y)
    o.rotation_euler = (rot_x, 0.0, 0.0)
    me.materials.append(mat)
    if r > 0.0:
        # (smooth only where it is rounded: a box shaded smooth bends its
        # faces' normals to its corners, and the desk turned from the lamp)
        me.shade_smooth()
        bv = o.modifiers.new('round', 'BEVEL')
        bv.width = min(r, 0.999 * min(half))
        bv.segments = seg
        bv.limit_method = 'NONE'
        bv.profile = 0.5
        bv.harden_normals = True
    return o


def cylinder(name, centre, axis, radius, half_len, mat, seg=48, r=0.0):
    """sd_cyl_y: a cylinder radius `radius` about `axis` (a direction in the
    room) through centre, half_len either way along it; its rims rounded r."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg,
                          radius1=radius, radius2=radius, depth=2.0 * half_len)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    COLL.objects.link(o)
    o.location = B(*centre)
    o.rotation_euler = B(*axis).normalized().to_track_quat('Z', 'Y').to_euler()
    me.materials.append(mat)
    # (smooth round the side, and the caps kept flat by the bevel's
    # hardened normals, or by the edge split when there is no bevel)
    me.shade_smooth()
    if r > 0.0:
        bv = o.modifiers.new('round', 'BEVEL')
        bv.width = min(r, 0.999 * min(radius, half_len))
        bv.segments = 4
        bv.limit_method = 'ANGLE'
        bv.angle_limit = math.radians(40.0)
        bv.harden_normals = True
    else:
        es = o.modifiers.new('edges', 'EDGE_SPLIT')
        es.split_angle = math.radians(40.0)
    return o


def sphere(name, centre, radius, mat):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=radius)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    COLL.objects.link(o)
    o.location = B(*centre)
    me.materials.append(mat)
    me.shade_smooth()
    return o


def cut(o, cutter, kind='DIFFERENCE'):
    """A boolean of o with cutter, which is not itself rendered."""
    cutter.hide_render = True
    cutter.hide_viewport = True
    m = o.modifiers.new('cut', 'BOOLEAN')
    m.operation = kind
    m.solver = 'EXACT'
    m.object = cutter
    return m


def behind_desk(room, objects):
    """What the tracer leaves of a thing at the back of the desk: nothing
    past the top board's own far edge (map_post, map_console)."""
    c = rbox('behind', (0.0, 0.0, room.zf + 0.02 - 50.0), (200.0, 200.0, 50.0), 0.0,
             MAT_HIDDEN)
    for o in objects:
        cut(o, c)
    return c


MAT_HIDDEN = principled('hidden', (0.5, 0.5, 0.5))


# ---- the light: the room's lamp, the two under the bookcase's upper
# board, and the room round them

def lamp(name, kind, at, power, colour, radius=0.0, shadow=True, spec=1.0):
    li = bpy.data.lights.new(name, kind)
    li.energy = power
    li.color = colour
    li.shadow_soft_size = radius
    li.use_shadow = shadow
    li.specular_factor = spec
    o = bpy.data.objects.new(name, li)
    COLL.objects.link(o)
    o.location = B(*at)
    return o, li


# env_ambient, as the world: felt below, walnut round about, a dim ceiling,
# and the three lobes
world = bpy.data.worlds.new('room')
SCENE.world = world
world.use_nodes = True
wg = Graph(world.node_tree)
tc = wg.n.new('ShaderNodeTexCoord')
d = tc.outputs['Generated']
dz = wg.n.new('ShaderNodeSeparateXYZ')
wg.link(d, dz.inputs[0])
dy = dz.outputs['Z']
c = wg.mix(wg.smooth(dy, -0.80, -0.04), (0.026, 0.044, 0.024), (0.150, 0.098, 0.052))
c = wg.mix(wg.smooth(dy, 0.02, 0.88), c, (0.128, 0.116, 0.106))
for direction, colour, kk, p in ((LIT_KEY_DIR, LIT_KEY_COL, 0.30, 3.0),
                                 (LIT_FILL_DIR, LIT_FILL_COL, 0.24, 2.5),
                                 (LIT_RIM_DIR, LIT_RIM_COL, 0.17, 7.0)):
    n = B(*direction).normalized()
    f = wg.math('POWER', wg.math('MAXIMUM', wg.vmath('DOT_PRODUCT', d, tuple(n)), 0.0), p)
    c = wg.vmath('ADD', c, wg.vmath('SCALE', colour, scale=wg.math('MULTIPLY', f, kk)))
wg.link(c, wg.n['Background'].inputs['Color'])
wg.n['Background'].inputs['Strength'].default_value = 1.0

MAT_DESK = principled('desk', (0.16, 0.085, 0.040), rough=0.42, coat=0.6, coat_rough=0.16)
MAT_WALL = principled('wall', (0.10, 0.09, 0.08), rough=0.8)


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    for li in list(bpy.data.lights):
        bpy.data.lights.remove(li)
    for ca in list(bpy.data.cameras):
        bpy.data.cameras.remove(ca)


def room_scene(room, centre):
    """The desk and the wall, the lamps and the eye, where they are in a
    room of this shape; the lamps under the bookcase as bright as they are
    at `centre`, the middle of the thing. Returns the desk."""
    # the desk under it and the wall behind, for it to see and (but for the
    # desk when its shadow is taken) the eye not to
    desk = rbox('desk', (0.0, -0.30, (room.zf + RM_ZN) * 0.5),
                (max(60.0, room.hx + 30.0), 0.30, (RM_ZN - room.zf) * 0.5), 0.0, MAT_DESK)
    wall = rbox('wall', (0.0, 10.0, room.zf - 1.60 - 0.30),
                (max(90.0, room.hx + 60.0), 30.0, 0.30), 0.0, MAT_WALL)
    desk.visible_camera = False
    wall.visible_camera = False

    # key_at(ROOM_KIND): E = LIT_KEY_IRRADIANCE (ref / d)^2 on a surface
    # square to it, which a lamp of power 4 pi E ref^2 gives; in a cone,
    # full inside CONE_OUT and none outside CONE_IN; moved out and up with
    # a wider desk, about the middle of its near edge, and as much brighter
    k = room.k
    ref = ROOM_LAMP_REF0 * k
    at = tuple(RM_ZN * (i == 2) + (ROOM_LAMP_POS0[i] - RM_ZN * (i == 2)) * k for i in range(3))
    key, kl = lamp('lamp', 'SPOT', at, 4.0 * math.pi * LIT_KEY_IRRADIANCE * ref * ref,
                   LIT_KEY_COL, radius=ROOM_LAMP_RADIUS0 * k)
    kl.spot_size = 2.0 * math.acos(ROOM_LAMP_CONE_IN)
    kl.spot_blend = (ROOM_LAMP_CONE_OUT - ROOM_LAMP_CONE_IN) / (1.0 - ROOM_LAMP_CONE_IN)
    key.rotation_euler = B(*ROOM_LAMP_AIM).to_track_quat('-Z', 'Y').to_euler()
    # shelf_lamp_at: diffuse only, and no shadows; each as much of its cone
    # as reaches the thing
    aim = Vector(SHELF_LAMP_AIM).normalized()
    for sx in (-8.2, 8.2):
        lp = Vector((sx, SHELF_LAMP_Y, room.sh_zf - 0.15))
        cone = smoothstep(-0.05, 0.62, (Vector(centre) - lp).normalized().dot(aim))
        if cone > 0.0:
            lamp('shelf', 'POINT', tuple(lp), 4.0 * math.pi * SHELF_LAMP_IRR * cone,
                 SHELF_LAMP_COL, radius=0.3, shadow=False, spec=0.0)

    # the eye: room_cam_build's, straight down
    cam_data = bpy.data.cameras.new('eye')
    cam_data.type = 'PERSP'
    cam_data.sensor_fit = 'HORIZONTAL'
    cam_data.sensor_width = 36.0
    cam_data.lens = 36.0 / RM_FOV_X
    cam_data.clip_start = 1.0
    cam_data.clip_end = 600.0
    cam = bpy.data.objects.new('eye', cam_data)
    COLL.objects.link(cam)
    SCENE.camera = cam
    cam.location = B(*room.eye)
    cam.rotation_euler = (0.0, 0.0, 0.0)
    SCENE.render.resolution_x, SCENE.render.resolution_y = room.w, room.h
    return desk


r = SCENE.render
r.engine = 'CYCLES'
r.resolution_percentage = 100
r.film_transparent = True
r.image_settings.file_format = 'OPEN_EXR'
r.image_settings.color_mode = 'RGBA'
r.image_settings.color_depth = '32'
r.use_persistent_data = False
cy = SCENE.cycles
cy.device = 'CPU'
cy.use_adaptive_sampling = False
cy.seed = 0
cy.max_bounces = 8
cy.glossy_bounces = 4
cy.filter_width = 1.0
cy.use_denoising = not QUICK and bool(getattr(bpy.app.build_options, 'openimagedenoise', True))
if cy.use_denoising:
    cy.denoiser = 'OPENIMAGEDENOISE'
SCENE.view_settings.view_transform = 'Standard'
TMP = tempfile.mkdtemp(prefix='case-')


def render(room, box, samples):
    """The frame's pixels in box (x0, y0, x1, y1 from its top left), as
    linear light, premultiplied, rows from the top."""
    x0, y0, x1, y1 = box
    r.use_border = True
    r.use_crop_to_border = True
    # (a quarter pixel in, so that Blender's rounding lands on these pixels)
    r.border_min_x, r.border_max_x = (x0 + 0.25) / room.w, (x1 + 0.25) / room.w
    r.border_min_y, r.border_max_y = (room.h - y1 + 0.25) / room.h, (room.h - y0 + 0.25) / room.h
    cy.samples = samples
    exr = os.path.join(TMP, 'pass.exr')
    r.filepath = exr
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(exr, check_existing=False)
    w, h = im.size
    if (w, h) != (x1 - x0, y1 - y0):
        sys.exit('case.py: rendered %dx%d, wanted %dx%d' % (w, h, x1 - x0, y1 - y0))
    px = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(px)
    bpy.data.images.remove(im)
    return np.flipud(px.reshape(h, w, 4)).astype(np.float64)


# ---- the room's tone curve (room_resolve, tonemap)

def aces(x):
    a, b, c_, d_, e = 2.51, 0.03, 2.43, 0.59, 0.14
    x = np.maximum(x, 0.0)
    return np.clip((x * (a * x + b)) / (x * (c_ * x + d_) + e), 0.0, 1.0)


def lum(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def soften(a):
    """A three by three box over a: what is left of the noise in a ratio of
    two renders."""
    p = np.pad(a, 1, mode='edge')
    return sum(p[1 + dy:p.shape[0] - 1 + dy, 1 + dx:p.shape[1] - 1 + dx]
               for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9.0


FADE = 0.35


def picture(room, parts, desk, reach, name):
    """The thing and its shadow, for this room: the picture (rows from the
    top, 0..255, straight alpha) and where in the frame it is (0..1).
    `reach` is the points, in the room, the part of the frame rendered has
    to take in: the thing, and the desk round it as far as its shadow could
    go."""
    pts = [room.project(*p) for p in reach]
    bx0 = max(0, int(math.floor(min(u for u, v in pts) * room.w)) - 4)
    bx1 = min(room.w, int(math.ceil(max(u for u, v in pts) * room.w)) + 4)
    by0 = max(0, int(math.floor(min(v for u, v in pts) * room.h)) - 4)
    by1 = min(room.h, int(math.ceil(max(v for u, v in pts) * room.h)) + 4)
    box = (bx0, by0, bx1, by1)

    # the thing, the desk and the wall unseen
    lit = render(room, box, SAMPLES)
    # the desk without it, and with it there unseen: the one over the other
    # is how much of the room's light it keeps off each part of the desk
    for o in parts:
        o.hide_render = True
    desk.visible_camera = True
    bare = render(room, box, DESK_SAMPLES)
    for o in parts:
        o.hide_render = False
        o.visible_camera = False
    shaded = render(room, box, DESK_SAMPLES)
    for o in parts:
        o.visible_camera = True
    desk.visible_camera = False
    if '--dump' in ARGV:
        np.savez(os.path.join(arg('--dump', '.'), 'passes-%s-%.4f-%.1f.npz' % (
            name, room.aspect, room.hx)), lit=lit, bare=bare, shaded=shaded)

    alpha = np.clip(lit[..., 3], 0.0, 1.0)
    straight = lit[..., :3] / np.maximum(alpha, 1e-6)[..., None] * ICON_EXPOSURE
    colour = aces(straight) ** (1.0 / 2.2)
    on_desk = np.clip(bare[..., 3], 0.0, 1.0)
    kept = np.where(on_desk > 0.5, lum(shaded[..., :3]) / np.maximum(lum(bare[..., :3]), 1e-6),
                    1.0)
    kept = np.clip(soften(np.clip(kept, 0.0, 1.0)), 0.0, 1.0)
    # what multiplying the desk's light by `kept` does to its picture, after
    # the room's curve at the desk's own brightness (low on the curve, where
    # it is steeper than its gamma): darkening it by `shadow`
    desk_l = lum(bare[..., :3]) / np.maximum(on_desk, 1e-6) * ICON_EXPOSURE
    shadow = (1.0 - (aces(desk_l * kept) / np.maximum(aces(desk_l), 1e-9)) ** (1.0 / 2.2)) * on_desk
    # and nothing where it is no more than the renders' noise
    shadow = np.clip((shadow - 1.0 / 255.0) / (1.0 - 1.0 / 255.0), 0.0, 1.0)
    # what is left of it at the sides and the foot of the part rendered
    # (the room keeps its light past them) faded out, not cut off; but not
    # at an edge that is the frame's own
    fade = PPU * FADE
    hh, ww = shadow.shape
    xs, ys = np.arange(ww, dtype=np.float64), np.arange(hh, dtype=np.float64)
    fx = np.ones(ww)
    if bx0 > 0:
        fx = np.minimum(fx, xs / fade)
    if bx1 < room.w:
        fx = np.minimum(fx, (ww - 1 - xs) / fade)
    fy = np.ones(hh)
    if by0 > 0:
        fy = np.minimum(fy, ys / fade)
    if by1 < room.h:
        fy = np.minimum(fy, (hh - 1 - ys) / fade)
    f = np.clip(fy[:, None] * fx[None, :], 0.0, 1.0)
    shadow *= f * f * (3.0 - 2.0 * f)
    # the thing over its shadow
    out_a = alpha + (1.0 - alpha) * shadow
    out_c = colour * (alpha / np.maximum(out_a, 1e-6))[..., None]
    out_c[out_a <= 0.0] = 0.0

    # cut to what is there
    there = np.argwhere(out_a >= 0.5 / 255.0)
    ty0, tx0 = np.maximum(there.min(axis=0) - 2, 0)
    ty1, tx1 = np.minimum(there.max(axis=0) + 3, out_a.shape)
    out_c, out_a = out_c[ty0:ty1, tx0:tx1], out_a[ty0:ty1, tx0:tx1]
    h, w = out_a.shape
    pic = np.empty((h, w, 4), dtype=np.uint8)
    pic[..., :3] = (out_c * 255.0 + 0.5).clip(0, 255).astype(np.uint8)
    pic[..., 3] = (out_a * 255.0 + 0.5).clip(0, 255).astype(np.uint8)
    rect = ((bx0 + tx0) / room.w, (by0 + ty0) / room.h,
            (bx0 + tx1) / room.w, (by0 + ty1) / room.h)
    print('case.py: %s %.4f/%.1f: %dx%d at (%d, %d) in a frame %dx%d' % (
        name, room.aspect, room.hx, w, h, bx0 + tx0, by0 + ty0, room.w, room.h))
    sys.stdout.flush()
    return pic, rect


HEADER = (
    '/* %s-case.h: made by %s.py with %s-case.bin -- do not edit, make again.\n'
    ' * %s and its shadow, rendered through the room\'s camera for each\n'
    ' * of these shapes of screen (width over height) and desks (half their\n'
    ' * width, in room units): where its picture is in %s-case.bin (a PNG),\n'
    ' * and where that goes in the frame of the room it was rendered in\n'
    ' * (room_cam_plan_at; 0..1 from its top left). In rows of\n'
    ' * %s_CASE_ASPECTS shapes, a row to a desk; room_cases_paint in draw.c. */\n\n')


def png_bytes(pic):
    """pic (rows from the top, RGBA bytes) as a PNG."""
    h, w = pic.shape[:2]
    im = bpy.data.images.new('case-piece', w, h, alpha=True)
    im.pixels.foreach_set((np.flipud(pic).astype(np.float32) / 255.0).ravel())
    path = os.path.join(TMP, 'piece.png')
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)
    with open(path, 'rb') as f:
        return f.read()


def write(name, what, shapes, n_aspects, n_hx):
    """NAME-case.bin, the pictures one after another, and NAME-case.h, where
    each is and what it is of. shapes: (aspect, hx, png, (w, h), rect) in
    rows of n_aspects, a row to a width."""
    up = name.upper()
    blob = bytearray()
    rows = []
    for aspect, hx, png, (w, h), rect in shapes:
        rows.append((aspect, hx, len(blob), len(png), w, h, rect))
        blob += png
    with open(os.path.join(OUT, '%s-case.bin' % name), 'wb') as f:
        f.write(blob)
    with open(os.path.join(OUT, '%s-case.h' % name), 'w') as f:
        f.write(HEADER % (name, name, name, what, name, up))
        f.write('#define %s_CASE_ASPECTS %d\n' % (up, n_aspects))
        f.write('#define %s_CASE_HXS %d\n' % (up, n_hx))
        f.write('#define %s_CASE_BIN_SIZE %du\n\n' % (up, len(blob)))
        f.write('static const struct room_case_bake %s_CASE[%s_CASE_ASPECTS * %s_CASE_HXS] = {\n'
                % (up, up, up))
        for aspect, hx, off, size, w, h, rect in rows:
            f.write('\t{ %.6f, %.4f, %du, %du, %d, %d, %.8f, %.8f, %.8f, %.8f },\n' % (
                aspect, hx, off, size, w, h, *rect))
        f.write('};\n')
    print('case.py: %d pictures, %s-case.bin %d bytes' % (len(rows), name, len(blob)))


def run(name, what, hxs, build, reach, centre):
    """Render the thing for each shape of screen and width of desk asked for
    and put out what draw.c needs. build(room) puts the thing in the room
    and returns its objects; reach(room) the points the part of the frame
    rendered has to take in; centre(room) its middle."""
    aspects = ASPECTS
    if '--only' in ARGV:
        aspects = [float(a) for a in arg('--only', '').split(',')]
    if '--hx' in ARGV:
        hxs = [float(h) for h in arg('--hx', '').split(',')]
    shapes = []
    for hx in hxs:
        for aspect in aspects:
            kept = os.path.join(KEEP, '%s-%.4f-%.4f' % (name, aspect, hx)) if KEEP else ''
            if kept and os.path.exists(kept + '.json') and os.path.exists(kept + '.png'):
                with open(kept + '.json') as f:
                    k = json.load(f)
                with open(kept + '.png', 'rb') as f:
                    shapes.append((aspect, hx, f.read(), tuple(k['size']), tuple(k['rect'])))
                print('case.py: %s %.4f/%.1f: kept' % (name, aspect, hx))
                continue
            room = Room(aspect, hx)
            clear()
            parts = build(room)
            desk = room_scene(room, centre(room))
            pic, rect = picture(room, parts, desk, reach(room), name)
            shape = (aspect, hx, png_bytes(pic), (pic.shape[1], pic.shape[0]), rect)
            if kept:
                os.makedirs(KEEP, exist_ok=True)
                with open(kept + '.png', 'wb') as f:
                    f.write(shape[2])
                # (the picture first: a .json is the sign it is all there)
                with open(kept + '.json', 'w') as f:
                    json.dump({'size': shape[3], 'rect': shape[4]}, f)
            shapes.append(shape)
    write(name, what, shapes, len(aspects), len(hxs))
    shutil.rmtree(TMP, ignore_errors=True)
