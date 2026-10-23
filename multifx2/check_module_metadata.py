#!/usr/bin/env python3
"""Compare native JFX control ABI with browser manifests: exact key order, defaults, sizes.
Runs on ARM64 and x86_64, catching silent missing controls before releasing to Pi.
"""
import ctypes
import json
import math
import sys
from pathlib import Path

class Param(ctypes.Structure):
    _fields_ = [
        ("key", ctypes.c_char_p), ("label", ctypes.c_char_p),
        ("unit", ctypes.c_char_p), ("default_value", ctypes.c_float)
    ]

class Descriptor(ctypes.Structure):
    _fields_ = [
        ("abi_version", ctypes.c_uint32), ("key", ctypes.c_char_p),
        ("name", ctypes.c_char_p), ("parameter_count", ctypes.c_uint32),
        ("parameters", ctypes.POINTER(Param)),
        ("create", ctypes.c_void_p), ("destroy", ctypes.c_void_p),
        ("process", ctypes.c_void_p)
    ]

def validate(path):
    modules = list(path.glob("*.so"))
    assert len(modules) >= 13, f"Expected 13 modules, got {len(modules)}"
    issues = []
    for file in modules:
        manifest = file.with_suffix(".json")
        if not manifest.exists():
            issues.append(f"Missing manifest: {file.name}")
            continue
        meta = json.loads(manifest.read_text(encoding="utf-8"))
        library = ctypes.CDLL(str(file.resolve()))
        entry = library.jfx_plugin
        entry.restype = ctypes.POINTER(Descriptor)
        desc = entry().contents
        count = int(desc.parameter_count)
        name = desc.key.decode("utf-8")
        if desc.abi_version != 2 or name != meta["key"] or name != file.stem:
            issues.append(f"{file.name}: incompatible key/ABI")
            continue
        if len(meta["params"]) != count:
            issues.append(f"{name}: UI controls {len(meta['params'])}, DSP controls {count}")
            continue
        for i, p in enumerate(meta["params"]):
            native = desc.parameters[i]
            real_key = native.key.decode("utf-8")
            if real_key != p["key"]:
                issues.append(f"{name} parameter {i}: C++ {real_key} != UI {p['key']}")
            if not math.isclose(native.default_value, float(p["def"]), abs_tol=1e-4):
                issues.append(f"{name} default {i}: C++ {native.default_value:g} != UI {p['def']:g}")
        print(f"{name}: validated {count} controls")
    if issues:
        raise SystemExit("Native-to-panel control metadata mismatches:\n" + "\n".join(issues))
    print(f"OK: {len(modules)} FX .so modules have matching, complete panel descriptors.")

if __name__ == "__main__":
    validate(Path(sys.argv[1]))
