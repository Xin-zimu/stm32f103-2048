#!/usr/bin/env python3
"""Convert an animated GIF to ST7789 4-bit indexed C arrays."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable, Sequence

WIDTH = 120
HEIGHT = 120
PIXEL_SCALE = 2
PALETTE_SIZE = 16
FRAME_SIZE = WIDTH * HEIGHT // 2


def parse_args() -> argparse.Namespace:
    """Parse converter paths and firmware playback limits."""
    parser = argparse.ArgumentParser(
        description="Convert GIF frames to ST7789 120x120 indexed C arrays."
    )
    parser.add_argument("input_gif", type=Path, help="input GIF path")
    parser.add_argument("output_c", type=Path, help="output C source path")
    parser.add_argument(
        "--max-frames",
        type=int,
        default=3,
        help="maximum number of frames to export (default: 3)",
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=100,
        help="firmware frame interval in milliseconds (default: 100)",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    """Reject values that cannot produce valid firmware source files."""
    if args.max_frames <= 0:
        raise ValueError("--max-frames must be greater than zero")
    if args.interval <= 0:
        raise ValueError("--interval must be greater than zero")
    if args.output_c.suffix.lower() != ".c":
        raise ValueError("output path must end with .c")


def fit_rgb_frame(frame: object) -> object:
    """Fit one Pillow image into a black 120x120 canvas without cropping."""
    from PIL import Image, ImageOps

    rgba = frame.convert("RGBA")
    background = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    background.alpha_composite(rgba)
    fitted = ImageOps.contain(
        background.convert("RGB"),
        (WIDTH, HEIGHT),
        method=Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
    canvas.paste(
        fitted,
        ((WIDTH - fitted.width) // 2, (HEIGHT - fitted.height) // 2),
    )
    return canvas


def pack_indexes(indexes: Sequence[int]) -> list[int]:
    """Pack two four-bit palette indexes into each output byte."""
    if len(indexes) != WIDTH * HEIGHT:
        raise ValueError(f"expected {WIDTH * HEIGHT} indexes, got {len(indexes)}")

    packed = []
    for offset in range(0, len(indexes), 2):
        packed.append((indexes[offset] << 4) | indexes[offset + 1])
    return packed


def rgb888_to_rgb565(red: int, green: int, blue: int) -> int:
    """Convert one RGB888 color to the ST7789 RGB565 wire format."""
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def load_gif_frames(
    input_path: Path, max_frames: int
) -> tuple[list[int], list[list[int]]]:
    """Build one shared 16-color palette and packed indexes for all GIF frames."""
    try:
        from PIL import Image, ImageSequence
    except ImportError as exc:
        raise RuntimeError(
            "Pillow is required. Install it with: py -m pip install pillow"
        ) from exc

    rgb_frames = []
    with Image.open(input_path) as gif:
        for index, frame in enumerate(ImageSequence.Iterator(gif)):
            if index >= max_frames:
                break
            rgb_frames.append(fit_rgb_frame(frame))

    if not rgb_frames:
        raise ValueError("the input GIF contains no frames")

    atlas = Image.new("RGB", (WIDTH, HEIGHT * len(rgb_frames)))
    for index, frame in enumerate(rgb_frames):
        atlas.paste(frame, (0, index * HEIGHT))

    quantized = atlas.quantize(
        colors=PALETTE_SIZE,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.FLOYDSTEINBERG,
    )
    raw_palette = quantized.getpalette()[: PALETTE_SIZE * 3]
    palette = []
    for index in range(PALETTE_SIZE):
        red, green, blue = raw_palette[index * 3 : index * 3 + 3]
        palette.append(rgb888_to_rgb565(red, green, blue))

    frames = []
    for index in range(len(rgb_frames)):
        frame = quantized.crop((0, index * HEIGHT, WIDTH, (index + 1) * HEIGHT))
        frames.append(pack_indexes(list(frame.getdata())))

    return palette, frames


def format_bytes(data: Iterable[int], indent: str = "        ") -> str:
    """Format byte values in compact ARMCC-compatible C source lines."""
    values = list(data)
    lines = []
    for start in range(0, len(values), 16):
        chunk = values[start : start + 16]
        lines.append(indent + ", ".join(f"0x{value:02X}U" for value in chunk))
    return ",\n".join(lines)


def format_words(data: Iterable[int], indent: str = "    ") -> str:
    """Format RGB565 palette values in ARMCC-compatible C source lines."""
    values = list(data)
    lines = []
    for start in range(0, len(values), 8):
        chunk = values[start : start + 8]
        lines.append(indent + ", ".join(f"0x{value:04X}U" for value in chunk))
    return ",\n".join(lines)


def build_header(frame_count: int, interval: int) -> str:
    """Create the generated animation declarations and size constants."""
    return f"""#ifndef __ANIM_FRAMES_H
#define __ANIM_FRAMES_H

#include "stm32f10x.h"

#define ANIM_FRAME_WIDTH          {WIDTH}U    // Indexed source width
#define ANIM_FRAME_HEIGHT         {HEIGHT}U    // Indexed source height
#define ANIM_PIXEL_SCALE          {PIXEL_SCALE}U      // Output scale to 240x240
#define ANIM_FRAME_SIZE           {FRAME_SIZE}U   // Packed four-bit bytes per frame
#define ANIM_FRAME_COUNT          {frame_count}U      // Exported GIF frame count
#define ANIM_FRAME_INTERVAL_MS    {interval}U    // Playback interval in milliseconds

extern const uint16_t anim_palette[16];
extern const uint8_t anim_frames[ANIM_FRAME_COUNT][ANIM_FRAME_SIZE];

#endif
"""


def build_source(
    header_name: str, palette: Sequence[int], frames: Sequence[Sequence[int]]
) -> str:
    """Create constant RGB565 palette and indexed frame definitions."""
    frame_blocks = []
    for index, frame in enumerate(frames):
        frame_blocks.append(
            "    {\n"
            f"        /* Animation frame {index} */\n"
            f"{format_bytes(frame)}\n"
            "    }"
        )

    return (
        f'#include "{header_name}"\n\n'
        "const uint16_t anim_palette[16] =\n"
        "{\n"
        f"{format_words(palette)}\n"
        "};\n\n"
        "const uint8_t anim_frames[ANIM_FRAME_COUNT][ANIM_FRAME_SIZE] =\n"
        "{\n"
        + ",\n".join(frame_blocks)
        + "\n};\n"
    )


def write_outputs(
    output_c: Path,
    palette: Sequence[int],
    frames: Sequence[Sequence[int]],
    interval: int,
) -> tuple[Path, Path]:
    """Write GB2312-compatible generated C and header files without a BOM."""
    output_h = output_c.with_suffix(".h")
    output_c.parent.mkdir(parents=True, exist_ok=True)
    output_h.write_text(
        build_header(len(frames), interval), encoding="gbk", newline="\n"
    )
    output_c.write_text(
        build_source(output_h.name, palette, frames),
        encoding="gbk",
        newline="\n",
    )
    return output_c, output_h


def main() -> int:
    """Convert the requested GIF and report generated firmware paths."""
    args = parse_args()
    try:
        validate_args(args)
        palette, frames = load_gif_frames(args.input_gif, args.max_frames)
        output_c, output_h = write_outputs(
            args.output_c, palette, frames, args.interval
        )
    except (OSError, RuntimeError, ValueError) as exc:
        raise SystemExit(f"error: {exc}") from exc

    print(f"exported {len(frames)} frame(s), {FRAME_SIZE} bytes per frame")
    print(f"C source: {output_c}")
    print(f"header:   {output_h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
