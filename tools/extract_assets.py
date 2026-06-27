from __future__ import annotations

import argparse
import json
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from PIL import Image, ImageChops, ImageOps


ROOT = Path(__file__).resolve().parents[1]
MASTER_SHEET = ROOT / "reference" / "master_assets_sheet.png"
ASSETS_ROOT = ROOT / "data" / "assets"
RGB565_ROOT = ROOT / "data" / "assets_rgb565"
MANIFEST_PATH = ASSETS_ROOT / "manifest.json"
RESAMPLE = Image.Resampling.LANCZOS if hasattr(Image, "Resampling") else Image.LANCZOS


@dataclass(frozen=True)
class AssetSpec:
    filename: str
    size: tuple[int, int]
    group: str
    transparent: bool = False

    @property
    def canonical_path(self) -> Path:
        return ASSETS_ROOT / self.filename

    @property
    def runtime_path(self) -> str:
        return f"/assets/{self.filename}"

    @property
    def legacy_project_path(self) -> Path:
        return ASSETS_ROOT / self.group / self.filename

    @property
    def legacy_runtime_path(self) -> str:
        return f"/assets/{self.group}/{self.filename}"


@dataclass(frozen=True)
class Component:
    area: int
    left: int
    top: int
    right: int
    bottom: int

    @property
    def width(self) -> int:
        return self.right - self.left + 1

    @property
    def height(self) -> int:
        return self.bottom - self.top + 1

    @property
    def center(self) -> tuple[float, float]:
        return ((self.left + self.right) / 2.0, (self.top + self.bottom) / 2.0)


HEADER_SPECS: Sequence[AssetSpec] = (
    AssetSpec("logo_main_240x120.png", (240, 120), "logo"),
    AssetSpec("splash_screen_320x240.png", (320, 240), "backgrounds"),
    AssetSpec("background_main_320x240.png", (320, 240), "backgrounds"),
)

MENU_SPECS: Sequence[AssetSpec] = (
    AssetSpec("menu_stages_64.png", (64, 64), "menu", True),
    AssetSpec("menu_live_64.png", (64, 64), "menu", True),
    AssetSpec("menu_calibration_64.png", (64, 64), "menu", True),
    AssetSpec("menu_settings_64.png", (64, 64), "menu", True),
    AssetSpec("menu_alarms_64.png", (64, 64), "menu", True),
    AssetSpec("menu_maintenance_64.png", (64, 64), "menu", True),
)

STAGE_SPECS: Sequence[AssetSpec] = tuple(
    AssetSpec(f"stage_{index}_64.png", (64, 64), "stages", True) for index in range(1, 6)
)

SENSOR_SPECS: Sequence[AssetSpec] = (
    AssetSpec("icon_ph_32.png", (32, 32), "icons", True),
    AssetSpec("icon_ec_32.png", (32, 32), "icons", True),
    AssetSpec("icon_temp_water_32.png", (32, 32), "icons", True),
    AssetSpec("icon_temp_air_32.png", (32, 32), "icons", True),
    AssetSpec("icon_humidity_32.png", (32, 32), "icons", True),
    AssetSpec("icon_level_32.png", (32, 32), "icons", True),
)

ACTUATOR_SPECS: Sequence[AssetSpec] = (
    AssetSpec("icon_pump_32.png", (32, 32), "icons", True),
    AssetSpec("icon_recirculation_32.png", (32, 32), "icons", True),
    AssetSpec("icon_fill_32.png", (32, 32), "icons", True),
    AssetSpec("icon_light_32.png", (32, 32), "icons", True),
    AssetSpec("icon_fan_in_32.png", (32, 32), "icons", True),
    AssetSpec("icon_fan_out_32.png", (32, 32), "icons", True),
)

STATUS_SPECS: Sequence[AssetSpec] = (
    AssetSpec("status_on_32.png", (32, 32), "status", True),
    AssetSpec("status_off_32.png", (32, 32), "status", True),
    AssetSpec("status_alert_32.png", (32, 32), "status", True),
)

NAV_SPECS: Sequence[AssetSpec] = (
    AssetSpec("nav_back_32.png", (32, 32), "navigation", True),
    AssetSpec("nav_home_32.png", (32, 32), "navigation", True),
    AssetSpec("nav_next_32.png", (32, 32), "navigation", True),
)


def pixel_is_foreground(rgb: tuple[int, int, int], threshold: int = 45) -> bool:
    r, g, b = rgb
    return (r + g + b) > threshold and max(rgb) > 20


def alpha_for_pixel(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    peak = max(rgb)
    delta = peak - min(rgb)

    if peak <= 18 and delta <= 10:
        return 0
    if peak <= 36 and delta < 20:
        return 0
    if peak < 80:
        return int(max(0.0, min(1.0, (peak - 24) / 56.0)) * 255)
    return 255


def find_components(image: Image.Image) -> list[Component]:
    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    mask = [[False] * width for _ in range(height)]

    for y in range(height):
        for x in range(width):
            mask[y][x] = pixel_is_foreground(pixels[x, y])

    visited = [[False] * width for _ in range(height)]
    components: list[Component] = []

    for y in range(height):
        for x in range(width):
            if not mask[y][x] or visited[y][x]:
                continue

            queue = deque([(x, y)])
            visited[y][x] = True
            left = right = x
            top = bottom = y
            area = 0

            while queue:
                cx, cy = queue.popleft()
                area += 1
                left = min(left, cx)
                right = max(right, cx)
                top = min(top, cy)
                bottom = max(bottom, cy)

                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < width and 0 <= ny < height and mask[ny][nx] and not visited[ny][nx]:
                        visited[ny][nx] = True
                        queue.append((nx, ny))

            if area > 500:
                components.append(Component(area, left, top, right, bottom))

    return components


def local_mask(image: Image.Image, threshold: int = 45) -> list[list[bool]]:
    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    return [[pixel_is_foreground(pixels[x, y], threshold) for x in range(width)] for y in range(height)]


def occupancy_bounds(mask: Sequence[Sequence[bool]], row_ratio: float, col_ratio: float) -> tuple[int, int, int, int]:
    height = len(mask)
    width = len(mask[0])

    row_counts = [sum(row) for row in mask]
    col_counts = [sum(mask[y][x] for y in range(height)) for x in range(width)]

    row_threshold = max(1, int(width * row_ratio))
    col_threshold = max(1, int(height * col_ratio))

    top = next((idx for idx, count in enumerate(row_counts) if count >= row_threshold), 0)
    bottom = next((idx for idx in range(height - 1, -1, -1) if row_counts[idx] >= row_threshold), height - 1)
    left = next((idx for idx, count in enumerate(col_counts) if count >= col_threshold), 0)
    right = next((idx for idx in range(width - 1, -1, -1) if col_counts[idx] >= col_threshold), width - 1)

    return left, top, right, bottom


def expand_box(box: tuple[int, int, int, int], limit: tuple[int, int], pad: int) -> tuple[int, int, int, int]:
    left, top, right, bottom = box
    max_width, max_height = limit
    return (
        max(0, left - pad),
        max(0, top - pad),
        min(max_width - 1, right + pad),
        min(max_height - 1, bottom + pad),
    )


def crop_box(image: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    left, top, right, bottom = box
    return image.crop((left, top, right + 1, bottom + 1))


def fit_exact(image: Image.Image, size: tuple[int, int], background: tuple[int, int, int, int] | None = None) -> Image.Image:
    if background is None:
        return ImageOps.fit(image, size, method=RESAMPLE, centering=(0.5, 0.5))

    canvas = Image.new("RGBA", size, background)
    thumb = image.copy()
    thumb.thumbnail(size, RESAMPLE)
    x = (size[0] - thumb.width) // 2
    y = (size[1] - thumb.height) // 2
    canvas.paste(thumb, (x, y), thumb if thumb.mode == "RGBA" else None)
    return canvas


def strip_caption_from_panel(image: Image.Image) -> Image.Image:
    mask = local_mask(image)
    left, top, right, bottom = occupancy_bounds(mask, row_ratio=0.55, col_ratio=0.20)
    box = expand_box((left, top, right, bottom), image.size, 4)
    return crop_box(image, box)


def strip_caption_from_icon(image: Image.Image) -> Image.Image:
    mask = local_mask(image)
    width, height = image.size
    row_counts = [sum(row) for row in mask]
    split = int(height * 0.72)

    for idx in range(int(height * 0.55), int(height * 0.90)):
        if row_counts[idx] <= max(2, int(width * 0.06)):
            split = idx
            break

    cropped = image.crop((0, 0, width, split))
    trimmed = ImageChops.difference(cropped, Image.new(cropped.mode, cropped.size, (0, 0, 0)))
    bbox = trimmed.getbbox()
    return cropped.crop(bbox) if bbox else cropped


def make_icon_transparent(image: Image.Image, target_size: tuple[int, int]) -> Image.Image:
    rgba = image.convert("RGBA")
    width, height = rgba.size
    pixels = rgba.load()

    for y in range(height):
        for x in range(width):
            r, g, b, _ = pixels[x, y]
            pixels[x, y] = (r, g, b, alpha_for_pixel((r, g, b)))

    alpha = rgba.getchannel("A")
    bbox = alpha.getbbox()
    if bbox:
        rgba = rgba.crop(bbox)

    return fit_exact(rgba, target_size, background=(0, 0, 0, 0))


def save_png(image: Image.Image, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination, format="PNG", optimize=True, compress_level=9)


def rgb565_bytes(image: Image.Image) -> bytes:
    rgb = image.convert("RGB")
    data = bytearray()
    for r, g, b in rgb.getdata():
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data.extend(((value >> 8) & 0xFF, value & 0xFF))
    return bytes(data)


def classify_components(components: Iterable[Component]) -> dict[str, list[Component]]:
    filtered = [
        comp
        for comp in components
        if comp.top < 700 and not (comp.area > 100_000 and comp.height > 250)
    ]

    header = sorted([comp for comp in filtered if comp.top < 320 and comp.width > 200], key=lambda comp: comp.left)
    menu = sorted([comp for comp in filtered if comp.top < 330 and comp.left > 1000], key=lambda comp: (comp.top, comp.left))
    stages = sorted(
        [comp for comp in filtered if 330 <= comp.top < 530 and comp.width >= 145 and comp.left < 860],
        key=lambda comp: comp.left,
    )
    sensors = sorted(
        [comp for comp in filtered if 360 <= comp.top < 520 and 850 <= comp.left < 1530 and comp.width <= 130],
        key=lambda comp: comp.left,
    )
    actuators = sorted(
        [comp for comp in filtered if 540 <= comp.top < 700 and comp.left < 860 and comp.width <= 145],
        key=lambda comp: comp.left,
    )
    status = sorted(
        [comp for comp in filtered if 540 <= comp.top < 700 and 860 <= comp.left < 1160 and comp.width <= 120],
        key=lambda comp: comp.left,
    )
    navigation = sorted(
        [comp for comp in filtered if 540 <= comp.top < 700 and comp.left >= 1160 and comp.width <= 140],
        key=lambda comp: comp.left,
    )
    if len(navigation) != len(NAV_SPECS):
        nav_cluster = [comp for comp in filtered if 540 <= comp.top < 700 and comp.left >= 1160]
        if not nav_cluster:
            raise RuntimeError("Navigation cluster could not be detected")

        left = min(comp.left for comp in nav_cluster)
        top = min(comp.top for comp in nav_cluster)
        right = max(comp.right for comp in nav_cluster)
        bottom = max(comp.bottom for comp in nav_cluster)

        trimmed_top = top + int((bottom - top + 1) * 0.18)
        total_width = right - left + 1
        slice_width = total_width // len(NAV_SPECS)
        navigation = []

        for index in range(len(NAV_SPECS)):
            slice_left = left + (index * slice_width)
            slice_right = right if index == len(NAV_SPECS) - 1 else (slice_left + slice_width - 1)
            navigation.append(
                Component(
                    area=(slice_right - slice_left + 1) * (bottom - trimmed_top + 1),
                    left=slice_left,
                    top=trimmed_top,
                    right=slice_right,
                    bottom=bottom,
                )
            )

    groups = {
        "header": header,
        "menu": menu,
        "stages": stages,
        "sensors": sensors,
        "actuators": actuators,
        "status": status,
        "navigation": navigation,
    }

    expected_counts = {
        "header": len(HEADER_SPECS),
        "menu": len(MENU_SPECS),
        "stages": len(STAGE_SPECS),
        "sensors": len(SENSOR_SPECS),
        "actuators": len(ACTUATOR_SPECS),
        "status": len(STATUS_SPECS),
        "navigation": len(NAV_SPECS),
    }

    for name, items in groups.items():
        if len(items) != expected_counts[name]:
            raise RuntimeError(f"Detected {len(items)} assets for '{name}', expected {expected_counts[name]}")

    return groups


def export_group(
    sheet: Image.Image,
    components: Sequence[Component],
    specs: Sequence[AssetSpec],
    panel_mode: bool,
    export_rgb565: bool,
) -> list[dict[str, object]]:
    exported: list[dict[str, object]] = []

    for component, spec in zip(components, specs):
        component_image = sheet.crop((component.left, component.top, component.right + 1, component.bottom + 1))

        if panel_mode:
            prepared = strip_caption_from_panel(component_image)
            output = fit_exact(prepared, spec.size)
        elif spec.transparent:
            prepared = strip_caption_from_icon(component_image)
            output = make_icon_transparent(prepared, spec.size)
        else:
            prepared = strip_caption_from_panel(component_image)
            output = fit_exact(prepared, spec.size)

        destination = spec.canonical_path
        save_png(output, destination)

        # Keep grouped duplicates for compatibility with any existing UI build
        # that still references /assets/<group>/<name>.png paths.
        legacy_destination = spec.legacy_project_path
        save_png(output, legacy_destination)

        rgb565_path = None
        rgb565_blob = rgb565_bytes(output)
        if export_rgb565:
            rgb565_destination = RGB565_ROOT / spec.filename.replace(".png", ".rgb565.bin")
            rgb565_destination.parent.mkdir(parents=True, exist_ok=True)
            rgb565_destination.write_bytes(rgb565_blob)
            rgb565_path = str(rgb565_destination.relative_to(ROOT)).replace("\\", "/")

            legacy_rgb565_destination = RGB565_ROOT / spec.group / spec.filename.replace(".png", ".rgb565.bin")
            legacy_rgb565_destination.parent.mkdir(parents=True, exist_ok=True)
            legacy_rgb565_destination.write_bytes(rgb565_blob)

        exported.append(
            {
                "name": spec.filename,
                "group": spec.group,
                "path": spec.runtime_path,
                "project_path": str(destination.relative_to(ROOT)).replace("\\", "/"),
                "legacy_path": spec.legacy_runtime_path,
                "legacy_project_path": str(legacy_destination.relative_to(ROOT)).replace("\\", "/"),
                "size": {"width": spec.size[0], "height": spec.size[1]},
                "rgb565_bytes": len(rgb565_blob),
                "rgb565_path": rgb565_path,
            }
        )

    return exported


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract and optimize e-Weed assets from the master sprite sheet.")
    parser.add_argument(
        "--source",
        type=Path,
        default=MASTER_SHEET,
        help="Source sprite sheet path (absolute or relative to project root).",
    )
    parser.add_argument(
        "--export-rgb565",
        action="store_true",
        help="Also export raw RGB565 binaries into data/assets_rgb565 for TFT/LVGL performance experiments.",
    )
    args = parser.parse_args()

    source = args.source
    if not source.is_absolute():
        source = (ROOT / source).resolve()

    if not source.exists():
        raise FileNotFoundError(f"Master sheet not found at {source}")

    sheet = Image.open(source).convert("RGBA")
    components = find_components(sheet)
    groups = classify_components(components)

    manifest_entries: list[dict[str, object]] = []
    manifest_entries.extend(export_group(sheet, groups["header"], HEADER_SPECS, panel_mode=True, export_rgb565=args.export_rgb565))
    manifest_entries.extend(export_group(sheet, groups["menu"], MENU_SPECS, panel_mode=False, export_rgb565=args.export_rgb565))
    manifest_entries.extend(export_group(sheet, groups["stages"], STAGE_SPECS, panel_mode=False, export_rgb565=args.export_rgb565))
    manifest_entries.extend(export_group(sheet, groups["sensors"], SENSOR_SPECS, panel_mode=False, export_rgb565=args.export_rgb565))
    manifest_entries.extend(export_group(sheet, groups["actuators"], ACTUATOR_SPECS, panel_mode=False, export_rgb565=args.export_rgb565))
    manifest_entries.extend(export_group(sheet, groups["status"], STATUS_SPECS, panel_mode=False, export_rgb565=args.export_rgb565))
    manifest_entries.extend(export_group(sheet, groups["navigation"], NAV_SPECS, panel_mode=False, export_rgb565=args.export_rgb565))

    try:
        source_rel = str(source.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        source_rel = str(source)

    MANIFEST_PATH.write_text(
        json.dumps(
            {
                "source": source_rel,
                "asset_count": len(manifest_entries),
                "runtime_root": "/assets",
                "rgb565_root": "data/assets_rgb565" if args.export_rgb565 else None,
                "assets": manifest_entries,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    print(f"Generated {len(manifest_entries)} assets from {source.name}")


if __name__ == "__main__":
    main()
