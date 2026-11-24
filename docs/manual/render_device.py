# SPDX-License-Identifier: MIT
"""Render the STEP assembly with Blender; write projected anchors for flat callouts."""
import json
from pathlib import Path
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector

arguments = sys.argv[sys.argv.index('--') + 1:]
model, output = Path(arguments[0]), Path(arguments[1])
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 48
scene.cycles.use_denoising = True
scene.render.resolution_x = 1400
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.world.color = (0.8, 0.8, 0.8)
scene.view_settings.view_transform = 'Standard'
scene.render.film_transparent = False

def material(name, color, roughness=0.7):
    result = bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1)
    result.use_nodes = True
    node = result.node_tree.nodes.get('Principled BSDF')
    node.inputs['Base Color'].default_value = (*color, 1)
    node.inputs['Roughness'].default_value = roughness
    return result

def srgb(red, green, blue):
    return tuple(((channel / 255 + 0.055) / 1.055) ** 2.4 for channel in (red, green, blue))

# Match the production case and caps under the manual's neutral lighting.
shell = material('Copper PLA', srgb(126, 83, 60), roughness=0.48)
ink = material('Black PLA', srgb(21, 21, 21), roughness=0.45)
rubber = material('Feet', srgb(27, 27, 27), roughness=0.85)
paper = material('E-paper', srgb(173, 173, 168), roughness=0.8)
buttons = {}
for item in json.loads(model.read_text()):
    name = item['name']
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(item['vertices'], [], item['triangles'])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    if name.startswith('Cap'):
        top = max(vertex[2] for vertex in item['vertices'])
        center = [(min(vertex[axis] for vertex in item['vertices']) +
                   max(vertex[axis] for vertex in item['vertices'])) / 2 for axis in (0, 1)]
        buttons[name] = (*center, top)
        mesh.materials.append(ink)
    elif name == 'Logo':
        mesh.materials.append(ink)
    elif name.startswith('Foot') and name[4:].isdigit():
        mesh.materials.append(rubber)
    else:
        mesh.materials.append(shell)
# The STEP describes the enclosure. This insert is the panel inside its aperture.
mesh = bpy.data.meshes.new('Panel insert')
panel_up = Vector((0, 0.25882, 0.96593))
panel_normal = Vector((0, -0.96593, 0.25882))
vertices = [Vector((x, 0, 0)) + panel_up * height + panel_normal * (0.518 - 2.25)
            for x, height in [(10.25, 6.7), (101.25, 6.7), (101.25, 83.7), (10.25, 83.7)]]
mesh.from_pydata(vertices, [], [(0, 1, 2, 3)])
obj = bpy.data.objects.new('Panel insert', mesh)
scene.collection.objects.link(obj)
obj.data.materials.append(paper)

floor = material('Background', (0.94, 0.93, 0.90))
bpy.ops.mesh.primitive_plane_add(size=2000, location=(55, 45, -1.6))
bpy.context.object.data.materials.append(floor)
for name, position, power, size in [('Key', (-60, -80, 230), 1100000, 170), ('Fill', (200, 140, 190), 700000, 140)]:
    data = bpy.data.lights.new(name, 'AREA')
    data.energy, data.shape, data.size = power, 'DISK', size
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = position
    obj.rotation_euler = (Vector((55, 45, 25)) - obj.location).to_track_quat('-Z', 'Y').to_euler()

camera_data = bpy.data.cameras.new('Camera')
camera = bpy.data.objects.new('Camera', camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.type = 'ORTHO'
# The USB opening is the capsule on the Foot's right wall:
# x=111.5, y=28.53..38.53, z=4.53..9.13 in assembly coordinates.
anchors = {'buttons': [buttons[f'Cap{number}'] for number in range(1, 5)],
           'port': [(111.51, 33.53, 6.83)]}
result = {}
for name, position, target, scale in [
    ('device-base', (188, 193, 179), (98.25, 73.05, 19), 84),
    ('device-port', (235, -40, 65), (111.5, 33.53, 9), 64),
]:
    camera.location = position
    camera.rotation_euler = (Vector(target) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.ortho_scale = scale
    bpy.context.view_layer.update()
    scene.render.filepath = str(output / (name + '.png'))
    bpy.ops.render.render(write_still=True)
    result[name] = {}
    for key, points in anchors.items():
        projected = [world_to_camera_view(scene, camera, Vector(point)) for point in points]
        result[name][key] = [[round(point.x * 1400, 2), round((1 - point.y) * 1000, 2)] for point in projected]
(output / 'anchors.json').write_text(json.dumps(result, indent=2))
