#!/usr/bin/env python3
"""Builds the OctaviOS sprite sources for app_maze_cube from assets_src/ (build.ps1 runs it, then packs).

Writes app_maze_cube/art_src/art/*.png, the pack manifest app_maze_cube/maze_assets.json,
app_maze_cube/sound/assets/success.mp3 and copies the template label fonts into app_maze_cube/art.
The Classic and Ancient looks reproduce the shapes and colours the SDK 6.3 renderer drew in code.
Then pack with tools/build-octavios.ps1.
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

APP = Path(__file__).resolve().parent.parent  # the app folder (C:\wowcube\maze_cube)
SRC = APP / "assets_src" / "images"
ART = APP / "art_src" / "art"
PLUGIN_TEMPLATE = Path.home() / ".claude/plugins/cache/wowcube-skills/wowcube/2.13.0/templates/app_ai_template"
FFMPEG = Path(r"C:\wowcube\octavios\utils\ffmpeg.exe")

CELL, WALL = 60, 14
sprites = []


def rgb(hexstr):
    return tuple(int(hexstr[i:i + 2], 16) for i in (0, 2, 4))


def add(name, img, *, color="palette", fullsize=True, background="key", group=None, desc="", anim=None, frame=None):
    img.save(ART / f"{name}.png")
    s = {"name": name, "size": list(img.size), "color": color, "background": background,
         "group": group or name, "description": desc,
         "gen_prompt": f"Existing Maze Cube art ({desc}); exported by tools/export-octavios-assets.py, never generated."}
    flags = {"fullsize": fullsize}
    if color == "full":
        flags["alpha"] = False
    s["flags"] = flags
    if anim:
        s["anim"], s["frame"] = anim, frame
    sprites.append(s)


def smooth_disc(size, layers):
    """Antialiased concentric discs: layers = [(radius, colour, ring_width or 0)] drawn in order, 4x supersampled."""
    k = 4
    im = Image.new("RGBA", (size * k, size * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = size * k / 2
    for radius, col, width in layers:
        r = radius * k
        box = (c - r, c - r, c + r, c + r)
        if width:
            d.ellipse(box, outline=rgb(col) + (255,), width=width * k)
        else:
            d.ellipse(box, fill=rgb(col) + (255,))
    return im.resize((size, size), Image.LANCZOS)


def floors():
    add("floor_classic", Image.new("RGB", (240, 240), rgb("F2F4F7")), color="full", background="opaque",
        desc="Classic maze floor, one 240 px screen")
    wood = Image.new("RGB", (240, 240))
    tile = Image.open(SRC / "wood_floor.png").convert("RGB")
    for row in range(4):
        for col in range(4):
            t = ImageOps.mirror(tile) if col % 2 else tile
            t = ImageOps.flip(t) if row % 2 else t
            wood.paste(t, (col * 60, row * 60))  # mirrored grid keeps adjacent grain edges matched
    add("floor_wood", wood, color="full", background="opaque", desc="Wood maze floor, oak tiles in a mirrored 4x4 grid")
    anc = Image.new("RGB", (240, 240), rgb("B4A58B"))
    d = ImageDraw.Draw(anc)
    for row in range(6):
        for col in range(6):
            x, y = col * 40, row * 40
            d.rectangle((x + 1, y + 1, x + 39, y + 39), fill=rgb("DBCFB6" if (row + col) % 2 else "E2D7C0"))
            d.rectangle((x + 2, y + 2, x + 38, y + 2), fill=rgb("EDE4D2"))
            if (row * 3 + col) % 4 == 0:
                d.rectangle((x + 27, y + 29, x + 31, y + 29), fill=rgb("C6B99F"))
                d.rectangle((x + 31, y + 30, x + 31, y + 32), fill=rgb("C6B99F"))
    add("floor_ancient", anc, color="full", background="opaque", desc="Ancient maze floor, sandstone slabs")


def walls():
    # The texture's bevelled caps have transparent corners: the SDK 6.3 renderer drew it over a walnut rectangle.
    wood_rgba = Image.open(SRC / "wood_wall.png").convert("RGBA")
    wood_src = Image.alpha_composite(Image.new("RGBA", wood_rgba.size, rgb("67442C") + (255,)), wood_rgba).convert("RGB")
    cap = 8
    for n in (1, 2, 3):
        length = n * CELL + WALL
        add(f"wall_c{n}", Image.new("RGB", (length, WALL), rgb("35465C")), color="full", background="opaque",
            group="walls_c", desc=f"Classic wall over {n} cell(s)")

        w = Image.new("RGB", (length, WALL))
        w.paste(wood_src.crop((0, 0, cap, WALL)), (0, 0))
        x = cap
        middle = wood_src.crop((cap, 0, wood_src.width - cap, WALL))
        while x < length - cap:  # repeat only the beam's middle between both end bevels
            w.paste(middle.crop((0, 0, min(middle.width, length - cap - x), WALL)), (x, 0))
            x += middle.width
        w.paste(wood_src.crop((wood_src.width - cap, 0, wood_src.width, WALL)), (length - cap, 0))
        add(f"wall_w{n}", w, color="full", background="opaque", group="walls_w", desc=f"Walnut wall over {n} cell(s)")

        a = Image.new("RGB", (length, WALL), rgb("665D4C"))
        d = ImageDraw.Draw(a)
        d.rectangle((2, 2, length - 3, WALL - 3), fill=rgb("928773"))
        d.rectangle((1, 1, length - 2, 2), fill=rgb("B8AA8D"))
        d.rectangle((1, 1, 2, WALL - 2), fill=rgb("B8AA8D"))
        for off in range(28, length - 4, 30):
            d.rectangle((off, 2, off, WALL - 3), fill=rgb("5B5447"))
        add(f"wall_a{n}", a, color="full", background="opaque", group="walls_a", desc=f"Masonry wall over {n} cell(s)")


def pieces():
    add("ball_classic", Image.alpha_composite(smooth_disc(35, [(17.5, "FF655F", 0)]),
                                              _offset_disc(35, 4, -5, -5, "FFB4AD")),
        desc="Classic coral ball, 35 px")
    # metal_ball.png is an opaque square master; the SDK 6.3 renderer clipped it to the ball's circle.
    metal = Image.open(SRC / "metal_ball.png").convert("RGBA")
    metal.putalpha(smooth_disc(35, [(17.5, "FFFFFF", 0)]).getchannel("A"))
    add("ball_metal", metal, desc="Steel ball, 35 px")
    add("goal_classic", smooth_disc(40, [(20, "19A99A", 0), (16, "153B40", 0), (19, "58DDC7", 1)]), desc="Classic goal hole")
    add("goal_wood", Image.open(SRC / "brass_goal.png").convert("RGBA"), desc="Brass goal hole")
    add("goal_ancient", smooth_disc(40, [(20, "997344", 0), (16, "29271F", 0), (19, "D0B379", 1)]), desc="Bronze goal hole")


def _offset_disc(size, radius, dx, dy, col):
    k = 4
    im = Image.new("RGBA", (size * k, size * k), (0, 0, 0, 0))
    c = size * k / 2
    cx, cy, r = c + dx * k, c + dy * k, radius * k
    ImageDraw.Draw(im).ellipse((cx - r, cy - r, cx + r, cy + r), fill=rgb(col) + (255,))
    return im.resize((size, size), Image.LANCZOS)


def menu():
    add("menu_bg", Image.open(SRC / "bg_menu.png").convert("RGB").resize((120, 120), Image.LANCZOS),
        color="full", fullsize=False, background="opaque", desc="Shared wooden menu background, drawn x2")
    for theme in ("classic", "wood", "ancient"):
        add(f"board_{theme}", Image.open(SRC / f"ms_{theme}.png").convert("RGBA").resize((204, 204), Image.LANCZOS),
            desc=f"{theme} theme preview board (85 %)")
    add("icon_pat", Image.open(SRC / "ss_pat.png").convert("RGBA"), desc="PAT gesture icon")
    add("twist_00", Image.open(SRC / "ss_twist_1.png").convert("RGBA"), group="twist", anim="twist", frame=0,
        desc="TWIST gesture icon, frame 0")
    add("twist_01", Image.open(SRC / "ss_twist_2.png").convert("RGBA"), group="twist", anim="twist", frame=1,
        desc="TWIST gesture icon, frame 1")
    add("logo", Image.open(SRC / "menu_icon.png").convert("RGBA"), desc="Maze Cube logo")
    add("qr_code", Image.open(SRC / "ss_QR_code.png").convert("RGBA"), desc="QR code panel")


def main():
    if ART.exists():
        shutil.rmtree(ART)
    ART.mkdir(parents=True)
    floors(); walls(); pieces(); menu()

    manifest = {
        "game": "maze_cube", "schema_version": 1,
        "_style": "The approved Maze Cube art: oak and walnut wood, steel ball, brass goal; Classic and Ancient themes.",
        "icon": {"color": "palette", "side": 160},
        "sprites": sprites,
        "sounds": [{"name": "success", "duration_ms": 2500, "event_type": "win", "group": "play",
                    "description": "existing success.wav, played on reaching the goal"}],
    }
    (APP / "maze_assets.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")

    snd = APP / "sound" / "assets"
    snd.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(FFMPEG), "-loglevel", "error", "-y", "-i", str(APP / "assets_src" / "success.wav"),
                    "-ac", "1", "-ar", "22050", "-sample_fmt", "s16p", "-b:a", "32k", "-codec:a", "libmp3lame",
                    "-write_xing", "0", "-map_metadata", "-1", "-id3v2_version", "0", str(snd / "success.mp3")],
                   check=True)

    fonts = APP / "art"
    fonts.mkdir(exist_ok=True)
    for f in (PLUGIN_TEMPLATE / "art").glob("font_*"):
        shutil.copy2(f, fonts / f.name)
    print(f"{len(sprites)} sprites -> {ART}")


if __name__ == "__main__":
    sys.exit(main())
