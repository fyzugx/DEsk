"""What the pictures of a thing on the desk have in common (include/thing.h).

A thing that lies on the desk is rendered once, by a script of its own in
art/<thing>/, on nothing but its own shadow, in light of its own, through one eye:
this is what such a script uses. (What stands in the room, in the room's light, is
art/case.py's.) The script starts

    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import kit

and is run as

    blender -b --factory-startup -P art/<thing>/<thing>.py -- OUTDIR [--quick] [--samples N]

The sizes are in centimetres; x to the right, y away from the eye, z up, the desk at
z = 0. Pictures are arrays of rows from the top, 0..255, straight alpha. Rendered twice,
a script makes the same files to the byte: that is how a change that should change
nothing is checked.
"""

import math
import os
import subprocess
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

ARGV = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = os.path.abspath(ARGV[0] if ARGV and not ARGV[0].startswith('--') else '.')
QUICK = '--quick' in ARGV
SCRIPT = os.path.basename(sys.argv[sys.argv.index('-P') + 1]) if '-P' in sys.argv else 'kit'
LEG_DIR = os.path.join(OUT, 'legends')


def arg(name, default):
    return ARGV[ARGV.index(name) + 1] if name in ARGV else default


for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
SCENE = bpy.context.scene
COLL = SCENE.collection

MAT = {}                      # materials by name, for put
GROUPS = {}                   # what is shown together: name: its objects (group, show)
_GROUP = [None]


# ---- materials

def principled(name, base, rough=0.5, spec=0.5, metallic=0.0, coat=0.0, coat_rough=0.03,
               sss=0.0, transmission=0.0, ior=1.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*base, 1.0)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Specular IOR Level'].default_value = spec
    b.inputs['Metallic'].default_value = metallic
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Coat Roughness'].default_value = coat_rough
    b.inputs['Transmission Weight'].default_value = transmission
    b.inputs['IOR'].default_value = ior
    if sss > 0.0:
        b.inputs['Subsurface Weight'].default_value = sss
        b.inputs['Subsurface Radius'].default_value = (1.0, 0.55, 0.3)
        b.inputs['Subsurface Scale'].default_value = 0.06
    return m


def grain(m, scale, distance, strength=1.0, detail=4.0, roughness=0.65):
    """A fine texture, in the object's own coordinates: moulding, paper."""
    nt = m.node_tree
    n, ln = nt.nodes, nt.links
    b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord')
    nz = n.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = scale
    nz.inputs['Detail'].default_value = detail
    nz.inputs['Roughness'].default_value = roughness
    ln.new(tc.outputs['Object'], nz.inputs['Vector'])
    bump = n.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = strength
    bump.inputs['Distance'].default_value = distance
    ln.new(nz.outputs[0], bump.inputs['Height'])
    ln.new(bump.outputs['Normal'], b.inputs['Normal'])
    return m


def brushed(name, base, rough, lines=30.0, deep=0.006, along_x=True):
    """Metal drawn across an abrasive: fine lines one way, the shine broken along
    them. Stretched along the lines, it is the same metal."""
    m = principled(name, base, rough=rough, metallic=1.0)
    nt = m.node_tree
    n, ln = nt.nodes, nt.links
    b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord')
    mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (0.12, lines, 1.0) if along_x else (lines, 0.12, 1.0)
    ln.new(tc.outputs['Object'], mp.inputs['Vector'])
    nz = n.new('ShaderNodeTexNoise')
    nz.inputs['Scale'].default_value = 1.0
    nz.inputs['Detail'].default_value = 5.0
    nz.inputs['Roughness'].default_value = 0.62
    ln.new(mp.outputs[0], nz.inputs['Vector'])
    bump = n.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.65
    bump.inputs['Distance'].default_value = deep
    ln.new(nz.outputs[0], bump.inputs['Height'])
    ln.new(bump.outputs['Normal'], b.inputs['Normal'])
    rr = n.new('ShaderNodeMapRange')
    rr.inputs['From Min'].default_value = 0.3
    rr.inputs['From Max'].default_value = 0.7
    rr.inputs['To Min'].default_value = rough - 0.05
    rr.inputs['To Max'].default_value = rough + 0.09
    ln.new(nz.outputs[0], rr.inputs['Value'])
    ln.new(rr.outputs[0], b.inputs['Roughness'])
    return m


def legend_image(s, svg):
    """A key's print, from the text of an SVG, 512 pixels square."""
    os.makedirs(LEG_DIR, exist_ok=True)
    name = ''.join(c if c.isalnum() else f'_{ord(c):x}' for c in s)
    png = os.path.join(LEG_DIR, name + '.png')
    path = png[:-4] + '.svg'
    with open(path, 'w', encoding='utf-8') as f:
        f.write(svg)
    subprocess.run(['rsvg-convert', path, '-o', png], check=True)
    im = bpy.data.images.load(png, check_existing=True)
    im.colorspace_settings.name = 'Non-Color'
    return im


def printed(name, base, ink, rough, coat, image, side, metallic=0.0, ink_metal=0.15, ink_picture=False):
    """A key's top, with its print cut in and filled: where the ink is, it is duller
    than the glaze round it and catches the light as metal does."""
    m = principled(name, base, rough=rough, coat=coat, metallic=metallic)
    nt = m.node_tree
    n, ln = nt.nodes, nt.links
    b = n['Principled BSDF']
    tc = n.new('ShaderNodeTexCoord')
    mp = n.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (1.0 / side, 1.0 / side, 1.0)
    mp.inputs['Location'].default_value = (0.5, 0.5, 0.0)
    ln.new(tc.outputs['Object'], mp.inputs['Vector'])
    tex = n.new('ShaderNodeTexImage')
    tex.image = image
    tex.extension = 'CLIP'
    ln.new(mp.outputs[0], tex.inputs['Vector'])
    mix = n.new('ShaderNodeMix')
    mix.data_type = 'RGBA'
    mix.inputs['A'].default_value = (*base, 1.0)
    mix.inputs['B'].default_value = (*ink, 1.0)
    ln.new(tex.outputs['Alpha'], mix.inputs['Factor'])
    if ink_picture:
        ln.new(tex.outputs['Color'], mix.inputs['B'])
    ln.new(mix.outputs['Result'], b.inputs['Base Color'])
    for sock, a, c in (('Metallic', metallic, ink_metal), ('Roughness', rough, 0.34), ('Coat Weight', coat, 0.0)):
        mm = n.new('ShaderNodeMix')
        mm.data_type = 'FLOAT'
        mm.inputs['A'].default_value = a
        mm.inputs['B'].default_value = c
        ln.new(tex.outputs['Alpha'], mm.inputs['Factor'])
        ln.new(mm.outputs['Result'], b.inputs[sock])
    return m


# ---- shapes

def group(name):
    """What is put from now on goes in this group (show)."""
    GROUPS.setdefault(name, [])
    _GROUP[0] = name


def show(name=None, hide=None):
    """Only that group shows, less hide."""
    for g, objs in GROUPS.items():
        for o in objs:
            o.hide_render = g != name
    for o in (hide or []):
        o.hide_render = True


def put(name, bm, mat, smooth=True, sharp=None, loc=(0.0, 0.0, 0.0)):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    COLL.objects.link(o)
    o.location = loc
    me.materials.append(MAT[mat] if isinstance(mat, str) else mat)
    if smooth:
        me.shade_smooth()
        if sharp is not None:
            me.set_sharp_from_angle(angle=math.radians(sharp))
    if _GROUP[0]:
        GROUPS[_GROUP[0]].append(o)
    return o


def rrect(hw, hh, r, n=12, cx=0.0, cy=0.0):
    """A rounded rectangle's outline, anticlockwise, about (cx, cy)."""
    r = max(min(r, hw, hh), 1e-3)
    pts = []
    for qx, qy, a0 in ((hw - r, hh - r, 0.0), (-(hw - r), hh - r, 90.0),
                       (-(hw - r), -(hh - r), 180.0), (hw - r, -(hh - r), 270.0)):
        for i in range(n + 1):
            a = math.radians(a0 + 90.0 * i / n)
            pts.append((cx + qx + r * math.cos(a), cy + qy + r * math.sin(a)))
    return pts


def loft(name, rings, mat, cap_first=True, cap_last=False, centre_last=None, loc=(0.0, 0.0, 0.0), sharp=None):
    """Rings of the same number of points, bottom to top, made one skin: the ends
    closed flat, or the last on a centre point."""
    bm = bmesh.new()
    vs = [[bm.verts.new(p) for p in ring] for ring in rings]
    n = len(rings[0])
    for a, c in zip(vs, vs[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], c[j], c[i]))
    if cap_first:
        bm.faces.new(list(reversed(vs[0])))
    if centre_last is not None:
        mid = bm.verts.new(centre_last)
        for i in range(n):
            bm.faces.new((vs[-1][i], vs[-1][(i + 1) % n], mid))
    elif cap_last:
        bm.faces.new(vs[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return put(name, bm, mat, loc=loc, sharp=sharp)


def plinth(name, cx, cy, hw, hh, r, z0, z1, mat, bevel=0.0, n=12):
    """A flat solid with round corners, its top edge taken off by bevel."""
    rings = [[(x, y, z0) for x, y in rrect(hw, hh, r, n, cx, cy)]]
    if bevel > 0.0:
        rings.append([(x, y, z1 - bevel) for x, y in rrect(hw, hh, r, n, cx, cy)])
        for k in range(1, 5):
            a = math.radians(90.0 * k / 4)
            d = bevel * (1.0 - math.cos(a))
            z = z1 - bevel + bevel * math.sin(a)
            rings.append([(x, y, z) for x, y in rrect(hw - d, hh - d, max(r - d, 0.01), n, cx, cy)])
    else:
        rings.append([(x, y, z1) for x, y in rrect(hw, hh, r, n, cx, cy)])
    return loft(name, rings, mat, cap_last=True, sharp=70.0 if bevel > 0.0 else 30.0)


def collar(name, cx, cy, hw, hh, r, width, z0, z1, mat, n=12):
    """A raised collar round a hole: hw, hh are the hole's, width how wide it is."""
    inner = rrect(hw, hh, r, n, cx, cy)
    outer = rrect(hw + width, hh + width, r + width, n, cx, cy)
    mid = rrect(hw + width * 0.5, hh + width * 0.5, r + width * 0.5, n, cx, cy)
    rings = [[(x, y, z0) for x, y in inner],
             [(x, y, z1 - (z1 - z0) * 0.2) for x, y in inner],
             [(x, y, z1) for x, y in mid],
             [(x, y, z1 - (z1 - z0) * 0.2) for x, y in outer],
             [(x, y, z0) for x, y in outer]]
    return loft(name, rings, mat, cap_first=False, sharp=80.0)


def cap(name, cx, cy, hw, hh, r, z, top, dish, mat, n=10):
    """A key's cap, standing on z: a shoulder, and the top dished for a finger (or
    domed, if dish is below zero). Its origin is its middle, for its print."""
    sh = min(0.15, top * 0.55)
    rings = [[(x, y, -0.2) for x, y in rrect(hw, hh, r, n)],
             [(x, y, top - sh) for x, y in rrect(hw, hh, r, n)]]
    for k in range(1, 7):
        a = math.radians(90.0 * k / 6)
        d = sh * (1.0 - math.cos(a))
        rings.append([(x, y, top - sh + sh * math.sin(a)) for x, y in rrect(hw - d, hh - d, max(r - d, 0.01), n)])
    edge = rings[-1]
    ex = max(p[0] for p in edge)
    ey = max(p[1] for p in edge)

    def sunk(x, y):
        return top - dish * (1.0 - (x / ex) ** 2) * (1.0 - (y / ey) ** 2)
    steps = 18
    for k in range(1, steps + 1):
        t = 1.0 - k / (steps + 1)
        rings.append([(x * t, y * t, sunk(x * t, y * t)) for x, y, _ in edge])
    o = loft(name, rings, mat, centre_last=(0.0, 0.0, top - dish))
    o.location = (cx, cy, z)
    return o


def decal(name, x, y, hw, hh, z, mat):
    """A flat print. Its origin is its middle, for its picture."""
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in ((-hw, -hh, 0.0), (hw, -hh, 0.0), (hw, hh, 0.0), (-hw, hh, 0.0))]
    bm.faces.new(vs)
    return put(name, bm, mat, smooth=False, loc=(x, y, z))


def desk_plane():
    """The desk under it: only its shadow and what it throws back."""
    bm = bmesh.new()
    for x, y in ((-300, -300), (300, -300), (300, 300), (-300, 300)):
        bm.verts.new((x, y, 0.0))
    bm.faces.new(bm.verts)
    me = bpy.data.meshes.new('desk')
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new('desk', me)
    COLL.objects.link(o)
    me.materials.append(principled('desk', (0.085, 0.052, 0.032), rough=0.55, spec=0.3))
    o.is_shadow_catcher = True
    return o


# ---- the light

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


def room(colour):
    """The light of the room round about."""
    world = bpy.data.worlds.new('room')
    SCENE.world = world
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (*colour, 1.0)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1.0


def seen_only_in_polish(name, y0, y1, z, colour, sees):
    """A sheet of light high over it, which only what shines can see, and of that only
    sees: a polished face is what it has in it. Its Emission node."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.cycles.emission_sampling = 'NONE'
    n = m.node_tree.nodes
    n.remove(n['Principled BSDF'])
    em = n.new('ShaderNodeEmission')
    em.inputs['Color'].default_value = (*colour, 1.0)
    m.node_tree.links.new(em.outputs[0], n['Material Output'].inputs['Surface'])
    bm = bmesh.new()
    vs = [bm.verts.new((x, y, z)) for x, y in ((-200.0, y0), (200.0, y0), (200.0, y1), (-200.0, y1))]
    bm.faces.new(vs)
    o = put(name, bm, m, smooth=False)
    o.visible_camera = o.visible_diffuse = o.visible_shadow = False
    o.visible_transmission = o.visible_volume_scatter = False
    link = bpy.data.collections.new('in ' + name)
    for t in sees:
        link.objects.link(t)
    o.light_linking.receiver_collection = link
    return em


def light_by_height(em, y0, y1, stops):
    """How much of such a sheet there is from y0 to y1 of it: (where, 0..1; how much),
    none at either end. A deck drawn out along x keeps its polish if this is all there is."""
    nt = em.id_data
    tc = nt.nodes.new('ShaderNodeTexCoord')
    xyz = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Object'], xyz.inputs[0])
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.inputs['From Min'].default_value = y0
    mr.inputs['From Max'].default_value = y1
    nt.links.new(xyz.outputs['Y'], mr.inputs['Value'])
    rp = nt.nodes.new('ShaderNodeValToRGB')
    rp.color_ramp.interpolation = 'EASE'
    for e in rp.color_ramp.elements:
        e.color = (0.0, 0.0, 0.0, 1.0)
    for pos, v in stops:
        rp.color_ramp.elements.new(pos).color = (v, v, v, 1.0)
    nt.links.new(mr.outputs[0], rp.inputs['Fac'])
    nt.links.new(rp.outputs['Color'], em.inputs['Strength'])


# ---- the eye and the film

r = SCENE.render
c = SCENE.cycles
TMP = os.path.join(OUT, 'tmp')
SAMPLES = [16]
_EYE = {}


def film(samples, exposure=0.0, filter_size=1.2, bounces=8, glossy=5, transmission=None):
    """Rendered on the processor, to one picture for one window of the screen."""
    SAMPLES[0] = samples
    r.engine = 'CYCLES'
    r.resolution_percentage = 100
    r.film_transparent = True
    if filter_size is not None:
        r.filter_size = filter_size
    r.image_settings.file_format = 'PNG'
    r.image_settings.color_mode = 'RGBA'
    r.image_settings.color_depth = '8'
    c.device = 'CPU'
    c.samples = samples
    c.use_adaptive_sampling = False
    c.seed = 0
    c.max_bounces = bounces
    c.glossy_bounces = glossy
    if transmission is not None:
        c.transmission_bounces = transmission
    c.caustics_reflective = False
    c.caustics_refractive = False
    c.use_denoising = bool(getattr(bpy.app.build_options, 'openimagedenoise', True))
    if c.use_denoising:
        c.denoiser = 'OPENIMAGEDENOISE'
    SCENE.view_settings.view_transform = 'Standard'
    SCENE.view_settings.look = 'None'
    SCENE.view_settings.exposure = exposure
    os.makedirs(TMP, exist_ok=True)


def eye(tilt, px):
    """One eye, far off, tilted forward by tilt: no perspective, px pixels to the
    centimetre across, so that a picture can be stretched and its parts moved."""
    cam_data = bpy.data.cameras.new('eye')
    cam_data.type = 'ORTHO'
    cam_data.clip_start = 1.0
    cam_data.clip_end = 900.0
    cam = bpy.data.objects.new('eye', cam_data)
    COLL.objects.link(cam)
    SCENE.camera = cam
    _EYE.update(cam=cam, data=cam_data, px=px, up=Vector((0.0, math.cos(tilt), math.sin(tilt))),
                back=Vector((0.0, -math.sin(tilt), math.cos(tilt))))


def eye_data():
    return _EYE['data']


def aim(t):
    """The eye on this point, for the middle of the picture."""
    _EYE['cam'].location = t + _EYE['back'] * 400.0
    _EYE['cam'].rotation_euler = (t - _EYE['cam'].location).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.view_layer.update()


def frame(left_px, top_px, w_px, h_px, origin):
    """The eye set so that the picture is w_px by h_px, origin being at (left_px,
    top_px) in it."""
    r.resolution_x, r.resolution_y = w_px, h_px
    _EYE['data'].sensor_fit = 'AUTO'
    _EYE['data'].ortho_scale = max(w_px, h_px) / _EYE['px']
    mx = (w_px * 0.5 - left_px) / _EYE['px']
    my = (h_px * 0.5 - top_px) / _EYE['px']
    aim(Vector(origin) + Vector((mx, 0.0, 0.0)) - _EYE['up'] * my)


def shoot(name, samples=None):
    c.samples = samples or SAMPLES[0]
    path = os.path.join(TMP, name + '.png')
    r.filepath = path
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(path, check_existing=False)
    w, h = im.size
    a = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    return np.flipud((a.reshape(h, w, 4) * 255.0 + 0.5).clip(0, 255).astype(np.uint8))


def save(name, img):
    h, w = img.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=True)
    im.pixels.foreach_set((np.flipud(img).astype(np.float32) / 255.0).ravel())
    im.filepath_raw = os.path.join(OUT, name)
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)
    print(f'{SCRIPT}: wrote', name, w, h, flush=True)


def write_map(name, about, measures):
    """A header for src/: the measures in pixels that the C has to go by, written with
    the pictures so that they are said once. Measures are (NAME, value), or (NAME,
    value, what it is); the header goes in art/<thing>/ with them."""
    lines = [f'/* {name}: made by {SCRIPT} with the pictures -- do not edit, make again.',
             f' * {about} */', '', 'enum {']
    for m in measures:
        lines.append(f'\t{m[0]} = {m[1]},' + (f'  /* {m[2]} */' if len(m) > 2 else ''))
    lines += ['};', '']
    with open(os.path.join(OUT, name), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f'{SCRIPT}: wrote', name, flush=True)


# ---- keys: pieces of the picture, to lay over it

def piece(img, x0, y0, x1, y1, fade=6):
    """Part of a picture, its edge faded into what it will be laid on."""
    p = img[y0:y1, x0:x1].copy()
    h, w = p.shape[:2]
    rx = np.minimum(np.arange(w) + 0.5, w - np.arange(w) - 0.5) / float(fade)
    ry = np.minimum(np.arange(h) + 0.5, h - np.arange(h) - 0.5) / float(fade)
    rx, ry = rx.clip(0.0, 1.0), ry.clip(0.0, 1.0)
    p[:, :, 3] = (p[:, :, 3] * np.outer(ry * ry * (3.0 - 2.0 * ry), rx * rx * (3.0 - 2.0 * rx))).astype(np.uint8)
    return p


def held(p, inset, k=0.76):
    """A key held down is out of the light: its top, in from the edge of the piece."""
    h, w = p.shape[:2]
    rx = np.minimum(np.arange(w) + 0.5, w - np.arange(w) - 0.5) - inset
    ry = np.minimum(np.arange(h) + 0.5, h - np.arange(h) - 0.5) - inset
    m = np.outer((ry / 3.0).clip(0.0, 1.0), (rx / 3.0).clip(0.0, 1.0))
    p = p.copy()
    p[:, :, :3] = (p[:, :, :3] * (1.0 - m[:, :, None] * (1.0 - k))).astype(np.uint8)
    return p


class Sheet:
    """Pieces in a picture of cells, a row up and a row held down. If only part of a
    thing is rendered again, the rest of the sheet stays as it was."""

    def __init__(self, file, cell_w, cell_h, names):
        self.file, self.cell_w, self.cell_h, self.names = file, cell_w, cell_h, names
        self.img = np.zeros((cell_h * 2, cell_w * len(names), 4), dtype=np.uint8)
        path = os.path.join(OUT, file)
        if os.path.exists(path):
            im = bpy.data.images.load(path, check_existing=False)
            if tuple(im.size) == (self.img.shape[1], self.img.shape[0]):
                a = np.empty(self.img.size, dtype=np.float32)
                im.pixels.foreach_get(a)
                self.img = np.flipud((a.reshape(self.img.shape) * 255.0 + 0.5).clip(0, 255).astype(np.uint8))
            bpy.data.images.remove(im)

    def lay(self, name, row, p):
        col = self.names.index(name)
        h, w = p.shape[:2]
        x = col * self.cell_w + (self.cell_w - w) // 2
        y = row * self.cell_h + (self.cell_h - h) // 2
        self.img[y:y + h, x:x + w] = p

    def save(self):
        save(self.file, self.img)


def blur(a, sigma):
    r_ = int(sigma * 3.0) + 1
    k = np.exp(-0.5 * (np.arange(-r_, r_ + 1) / sigma) ** 2)
    k /= k.sum()
    a = np.apply_along_axis(lambda v: np.convolve(v, k, mode='same'), 0, a)
    return np.apply_along_axis(lambda v: np.convolve(v, k, mode='same'), 1, a)


def glyph(name, svg, side, cell):
    """What a light shines through: a print's picture at side pixels, in the middle
    of a square of cell pixels, 0..1."""
    png = os.path.join(LEG_DIR, f'{name}-{side}.png')
    os.makedirs(LEG_DIR, exist_ok=True)
    path = png[:-4] + '.svg'
    with open(path, 'w', encoding='utf-8') as f:
        f.write(svg)
    subprocess.run(['rsvg-convert', '-w', str(side), '-h', str(side), path, '-o', png], check=True)
    im = bpy.data.images.load(png, check_existing=False)
    a = np.empty(side * side * 4, dtype=np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    a = np.flipud(a.reshape(side, side, 4))[:, :, 3]
    out = np.zeros((cell, cell), dtype=np.float32)
    o = (cell - side) // 2
    out[o:o + side, o:o + side] = a
    return out


def glow(base, name, svg, side, cx, cy, half, core, halo, touched, near=1.1, far=2.6, heart=None):
    """A light under glass: laid on the glass by adding to it, as light does, what
    comes straight through the print and the bloom of it round about. If it has a heart,
    that is the colour of the middle of each stroke, where it is hottest."""
    m = glyph(name, svg, side, half * 2)
    k = 1.38 if touched else 1.0
    p = base[cy - half:cy + half, cx - half:cx + half].astype(np.float32)
    close, wide = blur(m, 1.6), blur(m, 6.0)
    inner = ((blur(m, 0.8) - 0.55) / 0.35).clip(0.0, 1.0)
    for i in range(3):
        p[:, :, i] += k * (255.0 * core[i] * m + 255.0 * halo[i] * (near * close + far * wide))
        if heart:
            p[:, :, i] += k * 255.0 * heart[i] * inner
    p = np.clip(p, 0.0, 255.0).astype(np.uint8)
    q = np.zeros((p.shape[0] + 0, p.shape[1] + 0, 4), dtype=np.uint8)
    q[:] = p
    return piece(q, 0, 0, half * 2, half * 2)
