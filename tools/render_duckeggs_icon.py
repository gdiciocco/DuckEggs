#!/usr/bin/env python3
"""Render DuckEggs.ico and its preview from the checked-in SVG."""

from __future__ import annotations

import ctypes
import os
import struct
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SVG_PATH = ROOT / "resources" / "duckeggs-logo.svg"
ICO_PATH = ROOT / "resources" / "DuckEggs.ico"
PREVIEW_PATH = ROOT / "resources" / "duckeggs-logo-512.png"
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
PREVIEW_SIZE = 512
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class _Rectangle(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("width", ctypes.c_double),
        ("height", ctypes.c_double),
    ]


class Renderer:
    def __init__(self) -> None:
        self.cairo = ctypes.CDLL("libcairo.so.2")
        self.rsvg = ctypes.CDLL("librsvg-2.so.2")

        self.cairo.cairo_image_surface_create.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
        ]
        self.cairo.cairo_image_surface_create.restype = ctypes.c_void_p
        self.cairo.cairo_create.argtypes = [ctypes.c_void_p]
        self.cairo.cairo_create.restype = ctypes.c_void_p
        self.cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
        self.cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
        self.cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]
        self.cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
        self.cairo.cairo_image_surface_get_data.restype = ctypes.POINTER(
            ctypes.c_ubyte
        )
        self.cairo.cairo_image_surface_get_stride.argtypes = [ctypes.c_void_p]
        self.cairo.cairo_image_surface_get_stride.restype = ctypes.c_int
        self.cairo.cairo_surface_write_to_png.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
        ]
        self.cairo.cairo_surface_write_to_png.restype = ctypes.c_int

        self.rsvg.rsvg_handle_new_from_file.argtypes = [
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        self.rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
        self.rsvg.rsvg_handle_render_document.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(_Rectangle),
            ctypes.POINTER(ctypes.c_void_p),
        ]
        self.rsvg.rsvg_handle_render_document.restype = ctypes.c_int
        self.rsvg.rsvg_handle_free.argtypes = [ctypes.c_void_p]

    def render(self, size: int) -> tuple[bytes, bytes]:
        """Return a PNG and a bottom-up 32-bit DIB for one square size."""
        surface = self.cairo.cairo_image_surface_create(
            0, size, size
        )  # CAIRO_FORMAT_ARGB32
        if not surface:
            raise RuntimeError("cairo could not create the icon surface")
        context = self.cairo.cairo_create(surface)
        handle = self.rsvg.rsvg_handle_new_from_file(
            os.fsencode(SVG_PATH), None
        )
        if not handle:
            self.cairo.cairo_destroy(context)
            self.cairo.cairo_surface_destroy(surface)
            raise RuntimeError(f"librsvg could not open {SVG_PATH}")
        try:
            rectangle = _Rectangle(0.0, 0.0, float(size), float(size))
            error = ctypes.c_void_p()
            rendered = self.rsvg.rsvg_handle_render_document(
                handle,
                context,
                ctypes.byref(rectangle),
                ctypes.byref(error),
            )
            if not rendered:
                raise RuntimeError("librsvg could not render the SVG")

            with tempfile.NamedTemporaryFile(
                suffix=".png", delete=False
            ) as temporary:
                png_path = Path(temporary.name)
            try:
                status = self.cairo.cairo_surface_write_to_png(
                    surface, os.fsencode(png_path)
                )
                if status:
                    raise RuntimeError(
                        f"cairo PNG write failed with status {status}"
                    )
                png = png_path.read_bytes()
            finally:
                png_path.unlink(missing_ok=True)

            self.cairo.cairo_surface_flush(surface)
            data = self.cairo.cairo_image_surface_get_data(surface)
            stride = self.cairo.cairo_image_surface_get_stride(surface)
            dib = _make_dib(data, stride, size)
            return png, dib
        finally:
            self.rsvg.rsvg_handle_free(handle)
            self.cairo.cairo_destroy(context)
            self.cairo.cairo_surface_destroy(surface)


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
            png_width, png_height = struct.unpack_from(">II", image, 16)
            if (png_width, png_height) != (actual_width, actual_height):
                raise RuntimeError("PNG entry dimensions do not match")
        else:
            dib_width, dib_height = struct.unpack_from("<ii", image, 4)
            if (dib_width, dib_height) != (actual_width, actual_height * 2):
                raise RuntimeError("DIB entry dimensions do not match")
        found_sizes.append(actual_width)
    if tuple(found_sizes) != ICON_SIZES:
        raise RuntimeError(f"unexpected ICO sizes: {found_sizes}")


def main() -> None:
    renderer = Renderer()
    rendered = {size: renderer.render(size) for size in ICON_SIZES}
    ico_entries = []
    for size in ICON_SIZES:
        png, dib = rendered[size]
        ico_entries.append((size, png if size == 256 else dib))

    ICO_PATH.write_bytes(_build_ico(ico_entries))
    _validate_ico(ICO_PATH)

    preview_png, _ = renderer.render(PREVIEW_SIZE)
    PREVIEW_PATH.write_bytes(preview_png)
    print(
        f"wrote {ICO_PATH.relative_to(ROOT)} "
        f"({', '.join(map(str, ICON_SIZES))} px) and "
        f"{PREVIEW_PATH.relative_to(ROOT)} ({PREVIEW_SIZE} px)"
    )


if __name__ == "__main__":
    main()
