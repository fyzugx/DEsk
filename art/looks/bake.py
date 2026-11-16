"""A look for DEsk's drawer (docs/LOOKS.md), from a model of a thing: anyone's, made in Blender
or anything that writes what Blender reads. It is rendered as the desk's own looks are
(art/looks/looks.py, which uses this): lying flat and seen from straight above, lit as the
drawer is -- the lamp up behind the desk to the right -- with nothing round it, for the drawer
gives it its shadows. Out comes a look's folder: look.png, 40 pixels to the centimetre, and
look.ini, what it is called and how big and thick it is.

    blender -b --factory-startup -P bake.py -- FOLDER MODEL [--name NAME] [--name-ja NAME] ...
        [--size CM] [--thick CM] [--turn DEGREES] [--units m|cm|mm] [--samples N] [--quick]
        [--author WHO] [--license WHICH] [--order N]

MODEL is a .blend (all its objects), .glb or .gltf, .obj, .fbx, .stl or .ply, lying as it
would on a table, its top up. Its size is taken from it -- metres for glTF and FBX and a
.blend in metres, and for the rest a guess, unless --units says -- or made --size
centimetres along its longest side. desk-look runs this (desk-look bake).
"""

import math
import os
import shutil
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.dirname(HERE)]
import kit  # noqa: E402

TOP_PX = 40                   # pixels to the centimetre
MARGIN = 0.6                  # centimetres of nothing round it
LANGS = ('ja', 'zh', 'ko', 'es', 'fr', 'de')
_EYE = {}


def stage(samples):
    """The drawer's light, and an eye straight above: once, before any is shot."""
    kit.film(samples)
    kit.area('lamp', (20.0, 24.0, 44.0), (0.0, 1.5, 3.0), 30.0, 30.0, 3.2, (1.0, 0.96, 0.90))
    kit.area('fill', (-10.0, -6.0, 60.0), (0.0, 1.5, 3.0), 70.0, 70.0, 0.45)
    kit.room((0.035, 0.035, 0.04))
    cd = bpy.data.cameras.new('above')
    cd.type = 'ORTHO'
    cd.clip_start = 1.0
    cd.clip_end = 2000.0
    above = bpy.data.objects.new('above', cd)
    kit.COLL.objects.link(above)
    above.rotation_euler = (0.0, 0.0, 0.0)
    kit.SCENE.camera = above
    _EYE.update(cam=above, data=cd)


def footprint(objs):
    xs, ys, zs = [], [], []
    for o in objs:
        if o.type not in ('MESH', 'CURVE', 'SURFACE', 'META', 'FONT'):
            continue
        for v in o.bound_box:
            w = o.matrix_world @ Vector(v)
            xs.append(w.x)
            ys.append(w.y)
            zs.append(w.z)
    if not xs:
        raise SystemExit('bake.py: there is nothing to see in it')
    return min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)


def shoot(objs, folder, names, thick_cm, order=None, author=None, licence=None, comment=None):
    """objs, and nothing else, into folder: look.png and look.ini. names: {'en': ..., 'ja': ...}."""
    keep = set(objs)
    for o in bpy.data.objects:
        if o.type in ('LIGHT', 'CAMERA'):
            continue
        o.hide_render = o not in keep
    bpy.context.view_layer.update()
    x0, y0, _, x1, y1, _ = footprint(objs)
    x0, y0, x1, y1 = x0 - MARGIN, y0 - MARGIN, x1 + MARGIN, y1 + MARGIN
    # (as many pixels as it is wide, and the middle of the picture its middle)
    w, h = int(math.ceil((x1 - x0) * TOP_PX)), int(math.ceil((y1 - y0) * TOP_PX))
    r = kit.SCENE.render
    r.resolution_x, r.resolution_y = w, h
    cd = _EYE['data']
    cd.ortho_scale = (w if w >= h else h) / TOP_PX
    cd.sensor_fit = 'HORIZONTAL' if w >= h else 'VERTICAL'
    _EYE['cam'].location = ((x0 + x1) * 0.5, (y0 + y1) * 0.5, 600.0)
    os.makedirs(folder, exist_ok=True)
    r.filepath = os.path.join(folder, 'look.png')
    bpy.ops.render.render(write_still=True)
    lines = ['[Look]', 'Name=%s' % names.get('en', os.path.basename(folder))]
    lines += ['Name[%s]=%s' % (k, names[k]) for k in LANGS if names.get(k)]
    lines += ['PixelsPerCm=%d' % TOP_PX, 'Thick=%.1f' % thick_cm]
    if order is not None:
        lines.append('Order=%d' % order)
    for k, v in (('Author', author), ('License', licence), ('Comment', comment)):
        if v:
            lines.append('%s=%s' % (k, v))
    with open(os.path.join(folder, 'look.ini'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('bake.py: %s, %d by %d, %.1f by %.1f cm' % (folder, w, h, x1 - x0 - 2 * MARGIN, y1 - y0 - 2 * MARGIN))


def tidy():
    """(what kit.py keeps its renders in, gone)"""
    shutil.rmtree(kit.TMP, ignore_errors=True)


# ---- a model of anyone's

def load(path):
    """What is in the file, put in the scene: its objects."""
    before = set(bpy.data.objects)
    ext = os.path.splitext(path)[1].lower()
    if ext == '.blend':
        with bpy.data.libraries.load(path, link=False) as (src, dst):
            dst.objects = list(src.objects)
        for o in dst.objects:
            if o is not None and o.type not in ('LIGHT', 'CAMERA'):
                kit.COLL.objects.link(o)
    elif ext in ('.glb', '.gltf'):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == '.obj':
        bpy.ops.wm.obj_import(filepath=path)
    elif ext == '.fbx':
        bpy.ops.import_scene.fbx(filepath=path)
    elif ext == '.stl':
        bpy.ops.wm.stl_import(filepath=path)
    elif ext == '.ply':
        bpy.ops.wm.ply_import(filepath=path)
    else:
        raise SystemExit('bake.py: %s is not something Blender reads (.blend .glb .gltf .obj .fbx .stl .ply)' % path)
    new = [o for o in bpy.data.objects if o not in before]
    # (its own lights and eyes are not the drawer's)
    for o in new:
        if o.type in ('LIGHT', 'CAMERA'):
            o.hide_render = True
    return [o for o in new if o.type not in ('LIGHT', 'CAMERA')], ext


def units_to_cm(ext, units, objs):
    if units:
        return {'m': 100.0, 'cm': 1.0, 'mm': 0.1}[units]
    if ext in ('.glb', '.gltf', '.fbx'):
        return 100.0
    if ext == '.blend':
        return 100.0 * (kit.SCENE.unit_settings.scale_length or 1.0)
    # (a guess: a thing for a drawer is a few centimetres to a few tens of them)
    x0, y0, z0, x1, y1, z1 = footprint(objs)
    most = max(x1 - x0, y1 - y0, z1 - z0)
    return 100.0 if most < 1.5 else 0.1 if most > 80.0 else 1.0


def main():
    args = kit.ARGV
    if len(args) < 2:
        raise SystemExit(__doc__)
    folder, model = os.path.abspath(args[0]), os.path.abspath(args[1])
    objs, ext = load(model)
    if not objs:
        raise SystemExit('bake.py: there is nothing in %s' % model)
    # all of it on one empty, made centimetres, turned, its middle over the eye and its foot at 0
    root = bpy.data.objects.new('look-root', None)
    kit.COLL.objects.link(root)
    for o in objs:
        if o.parent is None:
            o.parent = root
    k = units_to_cm(ext, kit.arg('--units', None), objs)
    root.scale = (k, k, k)
    root.rotation_euler = (0.0, 0.0, math.radians(float(kit.arg('--turn', '0'))))
    bpy.context.view_layer.update()
    x0, y0, z0, x1, y1, z1 = footprint(objs)
    size = kit.arg('--size', None)
    if size:
        s = float(size) / max(x1 - x0, y1 - y0, 1e-6)
        root.scale = (k * s, k * s, k * s)
        bpy.context.view_layer.update()
        x0, y0, z0, x1, y1, z1 = footprint(objs)
    root.location = (-(x0 + x1) * 0.5, -(y0 + y1) * 0.5, -z0)
    bpy.context.view_layer.update()
    x0, y0, z0, x1, y1, z1 = footprint(objs)
    names = {'en': kit.arg('--name', os.path.splitext(os.path.basename(model))[0])}
    for lang in LANGS:
        names[lang] = kit.arg('--name-' + lang, None)
    thick = float(kit.arg('--thick', '%.2f' % (z1 - z0)))
    stage(16 if kit.QUICK else int(kit.arg('--samples', 96)))
    order = kit.arg('--order', None)
    shoot(objs, folder, names, min(thick, 12.0), order=int(order) if order else None,
          author=kit.arg('--author', None), licence=kit.arg('--license', None),
          comment='Baked by desk-look from %s' % os.path.basename(model))
    tidy()


if __name__ == '__main__':
    main()
