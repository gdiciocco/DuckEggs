#!/usr/bin/env python3
"""Render deterministic DuckEggs raster assets from the approved PNG master.

The checked-in master is the only artwork input. This helper never parses or
reconstructs shapes: previews and RCDATA resample its complete RGBA canvas;
ICO entries use a square viewport around its meaningful alpha bounds.
"""

from __future__ import annotations

import ctypes
import hashlib
import os
import struct
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"
MASTER_PATH = RESOURCES / "duckeggs-logo-master.png"
ICO_PATH = RESOURCES / "DuckEggs.ico"
PREVIEW_PATH = RESOURCES / "duckeggs-logo-512.png"
SMALL_PREVIEW_PATH = RESOURCES / "duckeggs-logo-small-48.png"
EMBEDDED_256_PATH = RESOURCES / "duckeggs-logo-256.png"
EMBEDDED_128_PATH = RESOURCES / "duckeggs-logo-128.png"
CONTACT_SHEET_PATH = ROOT / "build" / "duckeggs-logo-contact-sheet.png"

MASTER_SIZE = 1254
MEANINGFUL_ALPHA = 16
ICO_SOURCE_PADDING = 8
MASTER_SHA256 = (
    "baabe140bf9398af64402b64300ce2b6a85d244273f328b9cb9b003e101271f5"
)
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 80, 96, 128, 256)
CONTACT_SIZES = ICON_SIZES
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CAIRO_FORMAT_ARGB32 = 0
CAIRO_OPERATOR_SOURCE = 1
CAIRO_FILTER_NEAREST = 3
CAIRO_FILTER_BEST = 2


class Cairo:
    def __init__(self) -> None:
        self.lib = ctypes.CDLL("libcairo.so.2")
        self._declare()

    def _declare(self) -> None:
        lib = self.lib
        lib.cairo_image_surface_create_from_png.argtypes = [ctypes.c_char_p]
        lib.cairo_image_surface_create_from_png.restype = ctypes.c_void_p
        lib.cairo_image_surface_create.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
        ]
        lib.cairo_image_surface_create.restype = ctypes.c_void_p
        lib.cairo_surface_status.argtypes = [ctypes.c_void_p]
        lib.cairo_surface_status.restype = ctypes.c_int
        lib.cairo_image_surface_get_width.argtypes = [ctypes.c_void_p]
        lib.cairo_image_surface_get_width.restype = ctypes.c_int
        lib.cairo_image_surface_get_height.argtypes = [ctypes.c_void_p]
        lib.cairo_image_surface_get_height.restype = ctypes.c_int
        lib.cairo_image_surface_get_stride.argtypes = [ctypes.c_void_p]
        lib.cairo_image_surface_get_stride.restype = ctypes.c_int
        lib.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
        lib.cairo_image_surface_get_data.restype = ctypes.POINTER(
            ctypes.c_ubyte
        )
        lib.cairo_surface_flush.argtypes = [ctypes.c_void_p]
        lib.cairo_surface_write_to_png.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
        ]
        lib.cairo_surface_write_to_png.restype = ctypes.c_int
        lib.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
        lib.cairo_create.argtypes = [ctypes.c_void_p]
        lib.cairo_create.restype = ctypes.c_void_p
        lib.cairo_destroy.argtypes = [ctypes.c_void_p]
        lib.cairo_set_operator.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.cairo_set_source_rgba.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.cairo_set_source_rgb.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.cairo_scale.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.cairo_set_source_surface.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.cairo_get_source.argtypes = [ctypes.c_void_p]
        lib.cairo_get_source.restype = ctypes.c_void_p
        lib.cairo_pattern_set_filter.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.cairo_paint.argtypes = [ctypes.c_void_p]
        lib.cairo_rectangle.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.cairo_fill.argtypes = [ctypes.c_void_p]
        lib.cairo_select_font_face.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
        ]
        lib.cairo_set_font_size.argtypes = [ctypes.c_void_p, ctypes.c_double]
        lib.cairo_move_to.argtypes = [
            ctypes.c_void_p,
            ctypes.c_double,
            ctypes.c_double,
        ]
        lib.cairo_show_text.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

    def open_png(self, path: Path) -> ctypes.c_void_p:
        surface = self.lib.cairo_image_surface_create_from_png(
            os.fsencode(path)
        )
        if not surface or self.lib.cairo_surface_status(surface) != 0:
            if surface:
                self.lib.cairo_surface_destroy(surface)
            raise RuntimeError(f"cairo could not open {path}")
        return surface

    def new_surface(self, width: int, height: int) -> ctypes.c_void_p:
        surface = self.lib.cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, width, height
        )
        if not surface or self.lib.cairo_surface_status(surface) != 0:
            if surface:
                self.lib.cairo_surface_destroy(surface)
            raise RuntimeError("cairo could not create an image surface")
        return surface

    def resize(
        self,
        source: ctypes.c_void_p,
        width: int,
        height: int,
        crop: tuple[int, int, int, int] | None = None,
    ) -> ctypes.c_void_p:
        destination = self.new_surface(width, height)
        context = self.lib.cairo_create(destination)
        if not context:
            self.lib.cairo_surface_destroy(destination)
            raise RuntimeError("cairo could not create a rendering context")
        try:
            self.lib.cairo_set_operator(context, CAIRO_OPERATOR_SOURCE)
            self.lib.cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 0.0)
            self.lib.cairo_paint(context)
            left, top, crop_width, crop_height = crop or (
                0, 0, self.width(source), self.height(source)
            )
            self.lib.cairo_scale(
                context, width / crop_width, height / crop_height
            )
            self.lib.cairo_set_source_surface(
                context, source, -float(left), -float(top)
            )
            self.lib.cairo_pattern_set_filter(
                self.lib.cairo_get_source(context), CAIRO_FILTER_BEST
            )
            self.lib.cairo_paint(context)
        finally:
            self.lib.cairo_destroy(context)
        self.lib.cairo_surface_flush(destination)
        return destination

    def write_png(self, surface: ctypes.c_void_p, path: Path) -> bytes:
        path.parent.mkdir(parents=True, exist_ok=True)
        status = self.lib.cairo_surface_write_to_png(
            surface, os.fsencode(path)
        )
        if status != 0:
            raise RuntimeError(f"cairo PNG write failed with status {status}")
        return path.read_bytes()

    def width(self, surface: ctypes.c_void_p) -> int:
        return self.lib.cairo_image_surface_get_width(surface)

    def height(self, surface: ctypes.c_void_p) -> int:
        return self.lib.cairo_image_surface_get_height(surface)

    def destroy(self, surface: ctypes.c_void_p) -> None:
        self.lib.cairo_surface_destroy(surface)

    def make_contact_sheet(
        self,
        surfaces: dict[int, ctypes.c_void_p],
        path: Path,
    ) -> None:
        label_width = 112
        column_widths = {
            size: max(56, size + 16) for size in CONTACT_SIZES
        }
        header_height = 32
        row_height = max(CONTACT_SIZES) + 40
        sheet_width = label_width + sum(column_widths.values())
        sheet_height = header_height + row_height * 2
        sheet = self.new_surface(sheet_width, sheet_height)
        context = self.lib.cairo_create(sheet)
        if not context:
            self.destroy(sheet)
            raise RuntimeError("cairo could not create contact-sheet context")
        try:
            self.lib.cairo_set_source_rgb(context, 1.0, 1.0, 1.0)
            self.lib.cairo_paint(context)
            self.lib.cairo_set_source_rgb(context, 0.11, 0.13, 0.15)
            self.lib.cairo_rectangle(
                context,
                0.0,
                0.0,
                float(sheet_width),
                float(header_height + row_height),
            )
            self.lib.cairo_fill(context)

            for row, dark in enumerate((True, False)):
                row_top = header_height + row * row_height
                if not dark:
                    self.lib.cairo_set_source_rgb(
                        context, 1.0, 1.0, 1.0
                    )
                    self.lib.cairo_rectangle(
                        context,
                        0.0,
                        float(row_top),
                        float(sheet_width),
                        float(row_height),
                    )
                    self.lib.cairo_fill(context)

                panel_text = (
                    (0.86, 0.89, 0.91) if dark else (0.10, 0.13, 0.16)
                )
                self.lib.cairo_set_source_rgb(context, *panel_text)
                self.lib.cairo_select_font_face(
                    context, b"DejaVu Sans", 0, 1
                )
                self.lib.cairo_set_font_size(context, 12.0)
                row_label = (
                    b"Dark background" if dark else b"White background"
                )
                self.lib.cairo_move_to(
                    context, 12.0, row_top + 132.0
                )
                self.lib.cairo_show_text(context, row_label)

                column_left = label_width
                for size in CONTACT_SIZES:
                    column_width = column_widths[size]
                    image = surfaces[size]
                    x = column_left + (column_width - size) // 2
                    y = row_top + 8
                    self.lib.cairo_set_source_surface(
                        context, image, float(x), float(y)
                    )
                    self.lib.cairo_pattern_set_filter(
                        self.lib.cairo_get_source(context),
                        CAIRO_FILTER_NEAREST,
                    )
                    self.lib.cairo_paint(context)

                    self.lib.cairo_set_source_rgb(context, *panel_text)
                    self.lib.cairo_select_font_face(
                        context, b"DejaVu Sans", 0, 0
                    )
                    self.lib.cairo_set_font_size(context, 11.0)
                    self.lib.cairo_move_to(
                        context,
                        float(column_left + 4),
                        float(row_top + max(CONTACT_SIZES) + 17),
                    )
                    self.lib.cairo_show_text(
                        context, f"{size}px".encode("ascii")
                    )
                    column_left += column_width
        finally:
            self.lib.cairo_destroy(context)
        self.write_png(sheet, path)
        self.destroy(sheet)


def _png_size(contents: bytes) -> tuple[int, int]:
    if not contents.startswith(PNG_SIGNATURE) or contents[12:16] != b"IHDR":
        raise RuntimeError("invalid PNG signature or IHDR")
    return struct.unpack_from(">II", contents, 16)


def _alpha_summary(
    data: ctypes.POINTER(ctypes.c_ubyte),
    stride: int,
    width: int,
    height: int,
) -> tuple[int, int, int]:
    transparent = 0
    partial = 0
    opaque = 0
    for y in range(height):
        row = y * stride
        for x in range(width):
            alpha = data[row + x * 4 + 3]
            if alpha == 0:
                transparent += 1
            elif alpha == 255:
                opaque += 1
            else:
                partial += 1
    return transparent, partial, opaque


def _meaningful_alpha_bounds(
    data: ctypes.POINTER(ctypes.c_ubyte),
    stride: int,
    width: int,
    height: int,
    threshold: int = MEANINGFUL_ALPHA,
) -> tuple[int, int, int, int]:
    left, top, right, bottom = width, height, -1, -1
    for y in range(height):
        for x in range(width):
            if data[y * stride + x * 4 + 3] >= threshold:
                left, top = min(left, x), min(top, y)
                right, bottom = max(right, x), max(bottom, y)
    if right < left or bottom < top:
        raise RuntimeError("approved master has no meaningful alpha bounds")
    return left, top, right + 1, bottom + 1


def _ico_viewport(
    bounds: tuple[int, int, int, int], width: int, height: int
) -> tuple[int, int, int, int]:
    left, top, right, bottom = bounds
    square = max(right - left, bottom - top) + 2 * ICO_SOURCE_PADDING
    center_x = (left + right) / 2
    center_y = (top + bottom) / 2
    x = round(center_x - square / 2)
    y = round(center_y - square / 2)
    if x < 0 or y < 0 or x + square > width or y + square > height:
        raise RuntimeError("ICO viewport would clip meaningful master alpha")
    if not (left >= x and top >= y and right <= x + square and bottom <= y + square):
        raise RuntimeError("ICO viewport clips meaningful master alpha")
    return x, y, square, square


def _make_dib(
    data: ctypes.POINTER(ctypes.c_ubyte),
    stride: int,
    size: int,
) -> bytes:
    pixels = bytearray(size * size * 4)
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)

    for output_y, source_y in enumerate(range(size - 1, -1, -1)):
        source_row = source_y * stride
        output_row = output_y * size * 4
        mask_row = output_y * mask_stride
        for x in range(size):
            offset = source_row + x * 4
            blue = data[offset]
            green = data[offset + 1]
            red = data[offset + 2]
            alpha = data[offset + 3]
            pixel_offset = output_row + x * 4
            if alpha:
                red = min(255, (red * 255 + alpha // 2) // alpha)
                green = min(255, (green * 255 + alpha // 2) // alpha)
                blue = min(255, (blue * 255 + alpha // 2) // alpha)
            else:
                red = green = blue = 0
                mask[mask_row + x // 8] |= 0x80 >> (x % 8)
            pixels[pixel_offset : pixel_offset + 4] = bytes(
                (blue, green, red, alpha)
            )

    header = struct.pack(
        "<IiiHHIIiiII",
        40,
        size,
        size * 2,
        1,
        32,
        0,
        len(pixels),
        0,
        0,
        0,
        0,
    )
    return header + bytes(pixels) + bytes(mask)


def _build_ico(images: list[tuple[int, bytes]]) -> bytes:
    header = struct.pack("<HHH", 0, 1, len(images))
    directory = []
    payload_offset = len(header) + len(images) * 16
    payloads = bytearray()
    for size, image in images:
        encoded_size = 0 if size == 256 else size
        directory.append(
            struct.pack(
                "<BBBBHHII",
                encoded_size,
                encoded_size,
                0,
                0,
                1,
                32,
                len(image),
                payload_offset,
            )
        )
        payloads.extend(image)
        payload_offset += len(image)
    return header + b"".join(directory) + bytes(payloads)


def _validate_ico(path: Path) -> None:
    contents = path.read_bytes()
    reserved, image_type, count = struct.unpack_from("<HHH", contents)
    if (reserved, image_type, count) != (0, 1, len(ICON_SIZES)):
        raise RuntimeError("invalid ICO directory header")
    found_sizes = []
    for index in range(count):
        entry = struct.unpack_from("<BBBBHHII", contents, 6 + index * 16)
        width, height, _, _, _, bits, length, offset = entry
        actual_width = 256 if width == 0 else width
        actual_height = 256 if height == 0 else height
        image = contents[offset : offset + length]
        if bits != 32 or actual_width != actual_height:
            raise RuntimeError("invalid ICO image entry")
        if image.startswith(PNG_SIGNATURE):
            if _png_size(image) != (actual_width, actual_height):
                raise RuntimeError("ICO PNG dimensions do not match")
        else:
            header_size, dib_width, dib_height = struct.unpack_from(
                "<Iii", image, 0
            )
            if (dib_width, dib_height) != (actual_width, actual_height * 2):
                raise RuntimeError("ICO DIB dimensions do not match")
            mask_stride = ((actual_width + 31) // 32) * 4
            expected_length = (
                header_size
                + actual_width * actual_height * 4
                + mask_stride * actual_height
            )
            if header_size != 40 or len(image) != expected_length:
                raise RuntimeError("ICO DIB or AND mask has invalid length")
        found_sizes.append(actual_width)
    if tuple(found_sizes) != ICON_SIZES:
        raise RuntimeError(f"unexpected ICO sizes: {found_sizes}")


def main() -> None:
    master_bytes = MASTER_PATH.read_bytes()
    actual_hash = hashlib.sha256(master_bytes).hexdigest()
    if actual_hash != MASTER_SHA256:
        raise RuntimeError(
            f"approved master hash changed: {actual_hash}; refusing to render"
        )
    if _png_size(master_bytes) != (MASTER_SIZE, MASTER_SIZE):
        raise RuntimeError("approved master is not 1254 by 1254")

    cairo = Cairo()
    master = cairo.open_png(MASTER_PATH)
    surfaces: dict[int, ctypes.c_void_p] = {}
    ico_surfaces: dict[int, ctypes.c_void_p] = {}
    try:
        if (cairo.width(master), cairo.height(master)) != (
            MASTER_SIZE,
            MASTER_SIZE,
        ):
            raise RuntimeError("cairo read different master dimensions")
        cairo.lib.cairo_surface_flush(master)
        master_alpha = _alpha_summary(
            cairo.lib.cairo_image_surface_get_data(master),
            cairo.lib.cairo_image_surface_get_stride(master),
            MASTER_SIZE,
            MASTER_SIZE,
        )
        if master_alpha[0] == 0 or master_alpha[1] + master_alpha[2] == 0:
            raise RuntimeError("approved master has no valid transparent artwork")
        alpha_data = cairo.lib.cairo_image_surface_get_data(master)
        alpha_stride = cairo.lib.cairo_image_surface_get_stride(master)
        viewport = _ico_viewport(
            _meaningful_alpha_bounds(
                alpha_data, alpha_stride, MASTER_SIZE, MASTER_SIZE
            ),
            MASTER_SIZE,
            MASTER_SIZE,
        )
        for size in sorted(set(ICON_SIZES) | set(CONTACT_SIZES) | {48, 512}):
            surfaces[size] = cairo.resize(master, size, size)
        ico_surfaces = {
            size: cairo.resize(master, size, size, viewport)
            for size in sorted(set(ICON_SIZES) | {48})
        }

        embedded_256 = cairo.write_png(surfaces[256], EMBEDDED_256_PATH)
        embedded_128 = cairo.write_png(surfaces[128], EMBEDDED_128_PATH)
        cairo.write_png(surfaces[512], PREVIEW_PATH)
        cairo.write_png(ico_surfaces[48], SMALL_PREVIEW_PATH)
        for path, expected in (
            (EMBEDDED_256_PATH, (256, 256)),
            (EMBEDDED_128_PATH, (128, 128)),
            (PREVIEW_PATH, (512, 512)),
            (SMALL_PREVIEW_PATH, (48, 48)),
        ):
            if _png_size(path.read_bytes()) != expected:
                raise RuntimeError(f"unexpected PNG dimensions in {path}")

        alpha_sets = []
        ico_entries = []
        for size in ICON_SIZES:
            surface = ico_surfaces[size]
            cairo.lib.cairo_surface_flush(surface)
            alpha_sets.append(
                _alpha_summary(
                    cairo.lib.cairo_image_surface_get_data(surface),
                    cairo.lib.cairo_image_surface_get_stride(surface),
                    size,
                    size,
                )
            )
            if size == 256:
                with tempfile.TemporaryDirectory() as temporary_directory:
                    normalized_256 = Path(temporary_directory) / "icon.png"
                    cairo.write_png(surface, normalized_256)
                    image = normalized_256.read_bytes()
            else:
                image = _make_dib(
                    cairo.lib.cairo_image_surface_get_data(surface),
                    cairo.lib.cairo_image_surface_get_stride(surface),
                    size,
                )
            ico_entries.append((size, image))

        if len(embedded_128) >= len(embedded_256):
            raise RuntimeError("128 px RCDATA should be smaller than 256 px")
        if not all(partial + opaque for _, partial, opaque in alpha_sets):
            raise RuntimeError("resampled icon contains no artwork")

        ICO_PATH.write_bytes(_build_ico(ico_entries))
        _validate_ico(ICO_PATH)
        cairo.make_contact_sheet(ico_surfaces, CONTACT_SHEET_PATH)
    finally:
        for surface in surfaces.values():
            cairo.destroy(surface)
        for surface in ico_surfaces.values():
            cairo.destroy(surface)
        cairo.destroy(master)

    print(
        f"wrote {ICO_PATH.relative_to(ROOT)} "
        f"({', '.join(map(str, ICON_SIZES))} px), PNG previews/resources, "
        f"and {CONTACT_SHEET_PATH.relative_to(ROOT)}"
    )


if __name__ == "__main__":
    main()
