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

shell = material('Warm paper enclosure', (0.69, 0.68, 0.64))
ink = material('Graphite', (0.055, 0.055, 0.05))
rubber = material('Feet', (0.16, 0.16, 0.15))
paper = material('E-paper', (0.91, 0.91, 0.86))
for item in json.loads(model.read_text()):
    mesh = bpy.data.meshes.new(item['name'])
    mesh.from_pydata(item['vertices'], [], item['triangles'])
    mesh.update()
    obj = bpy.data.objects.new(item['name'], mesh)
    scene.collection.objects.link(obj)
    obj.data.materials.append(ink if item['name'] == 'Logo' else rubber if item['name'].startswith('Foot') and item['name'][4:].isdigit() else shell)
# The STEP describes the enclosure. This insert is the panel inside its aperture.
mesh = bpy.data.meshes.new('Panel insert')
vertices = [(x, z * 0.267949 - 0.5, z) for x, z in [(10, 19), (101.5, 19), (101.5, 74), (10, 74)]]
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
anchors = {'buttons': [(98.25, y, 19) for y in (54, 66.7, 79.4, 92.1)],
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
