"""Capture and convert the real LVGL RGB565 framebuffer."""
from pathlib import Path
import struct
import subprocess
import tempfile
from PIL import Image, ImageDraw

preview = Path(__file__).resolve().parent
screens = preview / "screenshots"
screens.mkdir(exist_ok=True)
names = ("home", "lights", "fans", "climate", "energy", "automation", "settings", "devices")
for variant in ("normal", "offline-long", "pending-long", "vacant", "radar-down", "stale", "clock", "sleep-clock"):
    with tempfile.TemporaryDirectory() as temporary:
        subprocess.run([preview.parent / ".pio/preview/room-panel.exe", temporary, variant], check=True)
        sheet = Image.new("RGB", (4*480, 2*504), "#121722")
        labels = ImageDraw.Draw(sheet)
        for index, name in enumerate(names):
            targeted={"vacant":"climate","radar-down":"climate","stale":"climate","clock":"settings","sleep-clock":"home"}
            if variant in targeted and name != targeted[variant]: continue
            raw = (Path(temporary) / f"{index}.rgb565").read_bytes()
            assert len(raw) == 480*480*2
            rgb = bytearray()
            for (pixel,) in struct.iter_unpack("<H", raw):
                rgb.extend(((pixel>>11)*255//31, ((pixel>>5)&63)*255//63, (pixel&31)*255//31))
            image = Image.frombytes("RGB", (480,480), bytes(rgb))
            suffix = "" if variant == "normal" else "-" + variant
            image.save(screens / f"{name}{suffix}.png")
            x, y = (index%4)*480, (index//4)*504
            sheet.paste(image,(x,y))
            labels.text((x+10,y+484),name.title(),fill="white")
        if variant not in targeted: sheet.save(screens / f"contact-sheet{suffix}.png")
