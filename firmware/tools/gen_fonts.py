# SPDX-License-Identifier: AGPL-3.0-or-later
"""Rasterize OFL sources into relocatable LVGL 9 fmt_txt font records."""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/assets_src"
FONT_HEADER = struct.Struct("<4sHHhhbbHIIII")


def static_font(source: Path, output: Path, weight: int) -> None:
    font = TTFont(source)
    if "fvar" in font:
        axes = {axis.axisTag: axis.defaultValue for axis in font["fvar"].axes}
        axes["wght"] = weight
        font = instantiateVariableFont(font, axes, inplace=True)
    # Apply OpenType tabular figures before the converter selects glyphs by cmap.
    gsub = font["GSUB"].table
    for feature in gsub.FeatureList.FeatureRecord:
        if feature.FeatureTag != "tnum":
            continue
        for index in feature.Feature.LookupListIndex:
            for table in gsub.LookupList.Lookup[index].SubTable:
                for cmap in font["cmap"].tables:
                    if cmap.isUnicode():
                        for code in range(ord("0"), ord("9") + 1):
                            cmap.cmap[code] = table.mapping.get(
                                cmap.cmap[code], cmap.cmap[code]
                            )
    font.save(output)


def font_record(text: str) -> bytes:
    bitmap_source = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", text, re.DOTALL)[1]
    codes = [int(c, 16) for c in re.findall(r"/\* U\+([0-9A-F]+) ", bitmap_source)]
    bitmap_source = re.sub(r"/\*.*?\*/", "", bitmap_source, flags=re.DOTALL)
    bitmap = bytes(int(c, 16) for c in re.findall(r"0x([0-9a-f]+)", bitmap_source))
    glyphs = bytearray()
    pattern = (
        r"\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), "
        r"\.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)"
    )
    for fields in re.findall(pattern, text):
        glyphs.extend(struct.pack("<IIHHhh", *(int(v) for v in fields)))
    values = {
        key: int(value)
        for key, value in re.findall(
            r"\.(line_height|base_line|underline_position|underline_thickness) = (-?\d+)",
            text,
        )
    }
    glyph_offset = FONT_HEADER.size
    unicode_offset = glyph_offset + len(glyphs)
    bitmap_offset = (unicode_offset + len(codes) * 2 + 3) & ~3
    header = FONT_HEADER.pack(
        b"NFN1",
        0,
        values["line_height"],
        values["base_line"],
        values["underline_position"],
        values["underline_thickness"],
        4,
        len(codes),
        glyph_offset,
        unicode_offset,
        bitmap_offset,
        len(bitmap),
    )
    return (header + glyphs + struct.pack(f"<{len(codes)}H", *codes)).ljust(
        bitmap_offset, b"\0"
    ) + bitmap


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/assets")
    parser.add_argument(
        "--lang-pack",
        choices=("zh-CN,en", "en-only"),
        default=os.environ.get("NUKA_LANG_PACK", "zh-CN,en"),
    )
    parser.add_argument(
        "--converter",
        type=Path,
        default=Path(__file__).parent / "node_modules/lv_font_conv/lv_font_conv.js",
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    work = args.output / "work"
    work.mkdir(exist_ok=True)
    english = "".join(chr(c) for c in range(32, 127)) + "°℃±×−–—…→←↑↓•"
    chinese = (SOURCE / "common-3500.txt").read_text(encoding="utf-8")
    strings = json.loads((ROOT / "i18n/strings.zh-CN.json").read_text(encoding="utf-8"))
    chinese = "".join(
        sorted(set(chinese + "".join(strings.values()) + "，。！？：；（）【】、％"))
    )
    chinese = "".join(c for c in chinese if ord(c) > 127 and not c.isspace())
    specs = [
        (f"inter-{size}", "Inter", weight, size, english)
        for size, weight in ((22, 500), (28, 400), (32, 600), (40, 600), (56, 600))
    ]
    specs.append(("clock-112", "JetBrainsMonoNerdFont-Latin", 700, 112, "0123456789:"))
    specs.append(("mono-40", "JetBrainsMonoNerdFont-Latin", 700, 40, "0123456789"))
    if args.lang_pack != "en-only":
        specs.extend(
            (f"zh-{size}", "NotoSansSC", 400, size, chinese) for size in (22, 28)
        )
    entries = []
    for name, family, weight, size, symbols in specs:
        static = work / f"{family}-{weight}.ttf"
        static_font(SOURCE / "fonts" / f"{family}.ttf", static, weight)
        c_file = work / f"{name}.c"
        # Decimal codepoints fit Windows' process argument limit without invoking cmd.exe.
        ranges = ",".join(str(code) for code in sorted(set(map(ord, symbols))))
        subprocess.run(
            [
                "node",
                str(args.converter),
                "--size",
                str(size),
                "--bpp",
                "4",
                "--format",
                "lvgl",
                "--font",
                str(static),
                "--range",
                ranges,
                "--no-compress",
                "--no-kerning",
                "--lv-font-name",
                "nuka_generated_font",
                "-o",
                str(c_file),
            ],
            check=True,
        )
        data = font_record(c_file.read_text(encoding="utf-8"))
        output = args.output / f"{name}.nfn"
        output.write_bytes(data)
        entries.append({"name": name, "file": output.name, "type": 1})
        print(f"{name}: {len(data)} bytes")
    for license in (SOURCE / "fonts").glob("*-OFL.txt"):
        shutil.copyfile(license, args.output / license.name)
        entries.append({"name": license.name, "file": license.name, "type": 0})
    (args.output / "assets.json").write_text(
        json.dumps(entries, indent=2) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
