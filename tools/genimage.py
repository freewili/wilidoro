#!/usr/bin/env python3
"""Shim. wiliOGbsp/bsp/CMakeLists.txt invokes ${CMAKE_SOURCE_DIR}/tools/<name>.py,
and CMAKE_SOURCE_DIR is this repo rather than the submodule. Delegate to the
submodule's copy so nothing is duplicated and submodule bumps stay clean."""
import pathlib, runpy, sys

_target = pathlib.Path(__file__).resolve().parent.parent / "wiliOGbsp" / "tools" / "genimage.py"
sys.argv[0] = str(_target)
runpy.run_path(str(_target), run_name="__main__")
