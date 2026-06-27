from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, List, Tuple

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS_ROOT = ROOT / "data" / "assets"
MANIFEST_PATH = ASSETS_ROOT / "manifest.json"
OUT_HEADER = ROOT / "include" / "generated_assets.h"
OUT_SOURCE = ROOT / "src" / "generated_assets.cpp"


def iter_assets() -> List[Path]:
    top_level = sorted(p for p in ASSETS_ROOT.glob("*.png") if p.is_file())
    if top_level:
        return top_level
    return sorted(p for p in ASSETS_ROOT.rglob("*.png") if p.is_file())


def load_manifest_entries() -> Dict[str, dict]:
    if not MANIFEST_PATH.exists():
        return {}

    data = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    out: Dict[str, dict] = {}
    for entry in data.get("assets", []):
        project_path = entry.get("project_path")
        if not project_path:
            continue
        path = (ROOT / project_path).resolve()
        out[str(path)] = entry
    return out


def make_symbol(rel_path: Path) -> str:
    raw = "_".join(rel_path.with_suffix("").parts)
    return "asset_" + "".join(ch if ch.isalnum() else "_" for ch in raw)


def has_transparency(alpha_band: Image.Image) -> bool:
    a_min, a_max = alpha_band.getextrema()
    return a_min < 255 or a_max < 255


def encode_lvgl_bytes(image: Image.Image, alpha: bool) -> bytes:
    rgba = image.convert("RGBA")
    pixels = rgba.getdata()
    out = bytearray()
    for r, g, b, a in pixels:
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out.append(rgb565 & 0xFF)
        out.append((rgb565 >> 8) & 0xFF)
        if alpha:
            out.append(a)
    return bytes(out)


def format_bytes(data: bytes) -> str:
    items = [f"0x{value:02X}" for value in data]
    lines = []
    for i in range(0, len(items), 16):
        lines.append("    " + ", ".join(items[i : i + 16]))
    return ",\n".join(lines)


def render() -> Tuple[str, str]:
    assets = iter_assets()
    if not assets:
        raise RuntimeError("No PNG assets found in data/assets")

    manifest_by_path = load_manifest_entries()
    declarations: List[str] = []
    data_blocks: List[str] = []
    table_rows: List[str] = []
    seen_runtime_paths: set[str] = set()

    for path in assets:
        rel = path.relative_to(ASSETS_ROOT)
        rel_posix = rel.as_posix()
        symbol = make_symbol(rel)
        data_symbol = f"{symbol}_data"

        with Image.open(path) as img:
            rgba = img.convert("RGBA")
            alpha = has_transparency(rgba.getchannel("A"))
            encoded = encode_lvgl_bytes(rgba, alpha)
            w, h = rgba.size

        cf = "LV_IMG_CF_TRUE_COLOR_ALPHA" if alpha else "LV_IMG_CF_TRUE_COLOR"
        declarations.append(f"extern const lv_img_dsc_t {symbol};")
        data_blocks.append(
            f"static const uint8_t {data_symbol}[] = {{\n{format_bytes(encoded)}\n}};\n\n"
            f"const lv_img_dsc_t {symbol} = {{\n"
            f"    .header = {{\n"
            f"        .cf = {cf},\n"
            f"        .always_zero = 0,\n"
            f"        .reserved = 0,\n"
            f"        .w = {w},\n"
            f"        .h = {h},\n"
            f"    }},\n"
            f"    .data_size = sizeof({data_symbol}),\n"
            f"    .data = {data_symbol},\n"
            f"}};\n"
        )

        manifest_entry = manifest_by_path.get(str(path.resolve()))
        runtime_paths: List[str] = []
        if manifest_entry is not None:
            canonical = manifest_entry.get("path")
            legacy = manifest_entry.get("legacy_path")
            if canonical:
                runtime_paths.extend([f"S:{canonical}", canonical])
            if legacy:
                runtime_paths.extend([f"S:{legacy}", legacy])
        else:
            fallback = f"/assets/{rel_posix}"
            runtime_paths.extend([f"S:{fallback}", fallback])

        for runtime_path in runtime_paths:
            if runtime_path in seen_runtime_paths:
                continue
            seen_runtime_paths.add(runtime_path)
            table_rows.append(f'    {{"{runtime_path}", &{symbol}}},')

    header = (
        "#ifndef GENERATED_ASSETS_H\n"
        "#define GENERATED_ASSETS_H\n\n"
        "#include <lvgl.h>\n\n"
        + "\n".join(declarations)
        + "\n\n"
        "const lv_img_dsc_t * embedded_asset_for_path(const char * path);\n"
        "bool embedded_asset_has_path(const char * path);\n\n"
        "#endif  // GENERATED_ASSETS_H\n"
    )

    source = (
        '#include "generated_assets.h"\n\n'
        "#include <cstring>\n\n"
        + "\n\n".join(data_blocks)
        + "\n\n"
        "struct EmbeddedAssetMap {\n"
        "    const char * path;\n"
        "    const lv_img_dsc_t * image;\n"
        "};\n\n"
        "static const EmbeddedAssetMap kEmbeddedAssetMap[] = {\n"
        + "\n".join(table_rows)
        + "\n};\n\n"
        "const lv_img_dsc_t * embedded_asset_for_path(const char * path) {\n"
        "    if (path == nullptr) {\n"
        "        return nullptr;\n"
        "    }\n"
        "    for (const auto & item : kEmbeddedAssetMap) {\n"
        "        if (std::strcmp(item.path, path) == 0) {\n"
        "            return item.image;\n"
        "        }\n"
        "    }\n"
        "    return nullptr;\n"
        "}\n\n"
        "bool embedded_asset_has_path(const char * path) {\n"
        "    return embedded_asset_for_path(path) != nullptr;\n"
        "}\n"
    )

    return header, source


def main() -> None:
    header, source = render()
    OUT_HEADER.write_text(header, encoding="utf-8")
    OUT_SOURCE.write_text(source, encoding="utf-8")
    print(f"Generated: {OUT_HEADER}")
    print(f"Generated: {OUT_SOURCE}")


if __name__ == "__main__":
    main()
