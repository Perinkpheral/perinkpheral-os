# SPDX-License-Identifier: MIT
"""Tessellate the named STEP bodies in assembly coordinates for Blender."""
import importlib.util
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("case_export", root / "tools/case/export_3mf.py")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
bodies = module.load_step_bodies(root / "hardware/step/rev1/perinkpheral.step")
meshes = []
for name, shapes in bodies.items():
    mesh = module.tessellate_body(name, shapes)
    meshes.append({"name": name, "vertices": mesh.vertices, "triangles": mesh.triangles})
    bounds = [[round(f(point[axis] for point in mesh.vertices), 2) for axis in range(3)] for f in (min, max)]
    print(name, bounds)
Path(sys.argv[1]).write_text(json.dumps(meshes))
