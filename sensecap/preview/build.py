"""Build the production LVGL UI on Windows using Zig 0.13 and cached PIO libraries."""
import concurrent.futures
import os
from pathlib import Path
import subprocess
import sys

preview = Path(__file__).resolve().parent
panel = preview.parent
deps = panel / ".pio/libdeps/d1"
lvgl = deps / "lvgl"
out = panel / ".pio/preview"
out.mkdir(parents=True, exist_ok=True)
common = ["-O1", "-DLV_CONF_INCLUDE_SIMPLE", "-DROOM_PANEL_PREVIEW"]
for path in (panel / "include", preview / "compat", lvgl, deps / "ArduinoJson/src"):
    common.extend(("-I", str(path)))

def zig(*args):
    subprocess.run([sys.executable, "-m", "ziglang", *map(str,args)], check=True)

cpp_headers=max(p.stat().st_mtime for root in (panel / "include", preview / "compat") for p in root.rglob("*.h"))

def compile_source(source):
    relative = str(source.relative_to(panel))
    obj = out / (relative.replace("\\", "_").replace("/", "_") + ".o")
    if not obj.exists() or obj.stat().st_mtime < max(source.stat().st_mtime, cpp_headers if source.suffix == ".cpp" else 0, (panel / "include/lv_conf.h").stat().st_mtime):
        cpp = source.suffix == ".cpp"
        zig("c++" if cpp else "cc", *common, *(["-std=c++17"] if cpp else []), "-c", source, "-o", obj)
    return obj

sources = list((lvgl / "src").rglob("*.c"))
sources += [preview / "main.cpp", panel / "src/Ui.cpp"]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objects = list(pool.map(compile_source, sources))
response = out / "link.rsp"
response.write_text("\n".join(f'"{obj.as_posix()}"' for obj in objects))
zig("c++", "@" + str(response), "-luser32", "-lgdi32", "-o", out / "room-panel.exe")
