#!/usr/bin/env python3
"""Convert a 120x120 GIF into compact ST7789 animation data."""

from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from PIL import Image

WIDTH = 120
HEIGHT = 120
PIXEL_SCALE = 2
PALETTE_SIZE = 16
FRAME_COUNT = 28
FRAME_INTERVAL_MS = 40
FIRST_FRAME_SIZE = WIDTH * HEIGHT // 2


@dataclass
class DeltaStats:
    """Aggregate encode-time statistics for one or more ST7789 delta streams."""

    delta_bytes: int = 0
    draw_runs: int = 0
    skip_runs: int = 0
    draw_source_pixels: int = 0
    skip_source_pixels: int = 0
    min_draw_run: int = 0
    max_draw_run: int = 0
    small_draw_runs: int = 0

    def add_draw(self, run_length: int, small_run_limit: int) -> None:
        """Record one encoded drawing run."""
        self.draw_runs += 1
        self.draw_source_pixels += run_length
        if self.min_draw_run == 0 or run_length < self.min_draw_run:
            self.min_draw_run = run_length
        if run_length > self.max_draw_run:
            self.max_draw_run = run_length
        if run_length <= small_run_limit:
            self.small_draw_runs += 1

    def add_skip(self, run_length: int) -> None:
        """Record one encoded unchanged run."""
        self.skip_runs += 1
        self.skip_source_pixels += run_length

    def merge(self, other: "DeltaStats") -> None:
        """Merge another statistics block into this aggregate."""
        self.delta_bytes += other.delta_bytes
        self.draw_runs += other.draw_runs
        self.skip_runs += other.skip_runs
        self.draw_source_pixels += other.draw_source_pixels
        self.skip_source_pixels += other.skip_source_pixels
        if other.min_draw_run != 0:
            if self.min_draw_run == 0 or other.min_draw_run < self.min_draw_run:
                self.min_draw_run = other.min_draw_run
        if other.max_draw_run > self.max_draw_run:
            self.max_draw_run = other.max_draw_run
        self.small_draw_runs += other.small_draw_runs

    @property
    def dma_bytes(self) -> int:
        """Return RGB565 bytes sent after ST7789 pixel scaling."""
        return self.draw_source_pixels * PIXEL_SCALE * PIXEL_SCALE * 2


def load_frames(path: Path) -> list[Image.Image]:
    """Load composited GIF frames and retain the configured evenly spaced frames."""
    image = Image.open(path)
    if image.size != (WIDTH, HEIGHT):
        raise ValueError(f"expected {WIDTH}x{HEIGHT}, got {image.size}")
    if image.n_frames < 2:
        raise ValueError("the GIF must contain at least two frames")

    indexes = [
        round(index * (image.n_frames - 1) / (FRAME_COUNT - 1))
        for index in range(FRAME_COUNT)
    ]
    frames = []
    for index in indexes:
        image.seek(index)
        frames.append(image.convert("RGB").copy())
    return frames


def quantize_frames(
    frames: Sequence[Image.Image],
) -> tuple[list[int], list[list[int]]]:
    """Build one global 16-color palette and quantize every frame without dithering."""
    strip = Image.new("RGB", (WIDTH, HEIGHT * len(frames)))
    for index, frame in enumerate(frames):
        strip.paste(frame, (0, index * HEIGHT))

    quantized_strip = strip.quantize(
        colors=PALETTE_SIZE,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    palette_rgb = quantized_strip.getpalette()[: PALETTE_SIZE * 3]
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette(palette_rgb + [0] * (768 - len(palette_rgb)))

    indexes = []
    for frame in frames:
        quantized = frame.quantize(
            palette=palette_image,
            dither=Image.Dither.NONE,
        )
        indexes.append(list(quantized.tobytes()))

    palette_565 = []
    for offset in range(0, len(palette_rgb), 3):
        red, green, blue = palette_rgb[offset : offset + 3]
        palette_565.append(
            ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        )
    return palette_565, indexes


def pack_indexes(indexes: Sequence[int]) -> list[int]:
    """Pack two four-bit palette indexes into each byte."""
    if len(indexes) & 1:
        raise ValueError("four-bit data must contain an even number of pixels")
    return [
        (indexes[offset] << 4) | indexes[offset + 1]
        for offset in range(0, len(indexes), 2)
    ]


def merge_short_gaps(changed: list[bool], merge_gap: int) -> None:
    """Mark short unchanged gaps between changed pixels as changed."""
    if merge_gap <= 0:
        return

    column = 0
    while column < WIDTH:
        while column < WIDTH and not changed[column]:
            column += 1
        while column < WIDTH and changed[column]:
            column += 1

        gap_start = column
        while column < WIDTH and not changed[column]:
            column += 1
        gap_length = column - gap_start

        if (
            gap_length > 0
            and gap_length <= merge_gap
            and gap_start > 0
            and column < WIDTH
        ):
            for gap_column in range(gap_start, column):
                changed[gap_column] = True


def encode_delta(
    current: Sequence[int],
    previous: Sequence[int],
    merge_gap: int,
    small_run_limit: int,
) -> tuple[list[int], DeltaStats]:
    """Encode one frame as row-local runs, optionally bridging short gaps."""
    encoded: list[int] = []
    stats = DeltaStats()
    for row in range(HEIGHT):
        row_start = row * WIDTH
        changed = [
            current[row_start + column] != previous[row_start + column]
            for column in range(WIDTH)
        ]
        merge_short_gaps(changed, merge_gap)

        column = 0
        while column < WIDTH:
            unchanged = not changed[column]
            count = 1
            while (
                column + count < WIDTH
                and count < 128
                and (not changed[column + count]) == unchanged
            ):
                count += 1

            if unchanged:
                encoded.append(0x80 | (count - 1))
                stats.add_skip(count)
            else:
                encoded.append(count - 1)
                stats.add_draw(count, small_run_limit)
                run = current[
                    row_start + column : row_start + column + count
                ]
                if len(run) & 1:
                    run = [*run, 0]
                encoded.extend(pack_indexes(run))
            column += count

    stats.delta_bytes = len(encoded)
    return encoded, stats


def apply_delta(encoded: Sequence[int], previous: Sequence[int]) -> list[int]:
    """Decode one generated delta stream for an exact round-trip check."""
    current = list(previous)
    position = 0
    for row in range(HEIGHT):
        column = 0
        row_start = row * WIDTH
        while column < WIDTH:
            control = encoded[position]
            position += 1
            count = (control & 0x7F) + 1
            if control & 0x80:
                column += count
                continue

            payload_size = (count + 1) // 2
            payload = encoded[position : position + payload_size]
            position += payload_size
            for run_index in range(count):
                packed = payload[run_index // 2]
                palette_index = packed >> 4 if run_index % 2 == 0 else packed & 0x0F
                current[row_start + column + run_index] = palette_index
            column += count

    if position != len(encoded):
        raise ValueError("delta round-trip left unused bytes")
    return current


def format_bytes(data: Iterable[int], indent: str = "    ") -> str:
    """Format bytes as compact ARMCC-compatible C initializers."""
    values = list(data)
    return ",\n".join(
        indent
        + ", ".join(f"0x{value:02X}U" for value in values[start : start + 16])
        for start in range(0, len(values), 16)
    )


def format_words(data: Iterable[int], indent: str = "    ") -> str:
    """Format 16-bit values as C initializers."""
    values = list(data)
    return ",\n".join(
        indent
        + ", ".join(f"0x{value:04X}U" for value in values[start : start + 8])
        for start in range(0, len(values), 8)
    )


def format_dwords(data: Iterable[int], indent: str = "    ") -> str:
    """Format 32-bit values as C initializers."""
    values = list(data)
    return ",\n".join(
        indent
        + ", ".join(f"{value}U" for value in values[start : start + 8])
        for start in range(0, len(values), 8)
    )


def build_header(frame_count: int, delta_size: int, source_hash: str) -> str:
    """Build declarations and format metadata for the generated animation."""
    return f"""#ifndef __ANIM_FRAMES_H
#define __ANIM_FRAMES_H

#include "stm32f10x.h"

#define ANIM_FRAME_WIDTH          {WIDTH}U    // Indexed source width
#define ANIM_FRAME_HEIGHT         {HEIGHT}U    // Indexed source height
#define ANIM_PIXEL_SCALE          {PIXEL_SCALE}U      // Output scale to 240x240
#define ANIM_FRAME_COUNT          {frame_count}U     // Number of retained GIF frames
#define ANIM_FRAME_INTERVAL_MS    {FRAME_INTERVAL_MS}U     // Display time per retained frame
#define ANIM_FIRST_FRAME_SIZE     {FIRST_FRAME_SIZE}U   // Packed first-frame bytes
#define ANIM_DELTA_DATA_SIZE      {delta_size}U  // Encoded bytes for all transitions
#define ANIM_SOURCE_SHA256        "{source_hash}"

extern const uint16_t anim_palette[16];
extern const uint8_t anim_first_frame[ANIM_FIRST_FRAME_SIZE];
extern const uint32_t anim_delta_offsets[ANIM_FRAME_COUNT + 1U];
extern const uint8_t anim_delta_data[ANIM_DELTA_DATA_SIZE];

#endif
"""


def build_source(
    palette: Sequence[int],
    first_frame: Sequence[int],
    offsets: Sequence[int],
    delta_data: Sequence[int],
) -> str:
    """Build constant RGB565 palette, first frame, offsets, and delta stream."""
    return f"""#include "anim_frames.h"

const uint16_t anim_palette[16] =
{{
{format_words(palette)}
}};

const uint8_t anim_first_frame[ANIM_FIRST_FRAME_SIZE] =
{{
{format_bytes(first_frame)}
}};

const uint32_t anim_delta_offsets[ANIM_FRAME_COUNT + 1U] =
{{
{format_dwords(offsets)}
}};

const uint8_t anim_delta_data[ANIM_DELTA_DATA_SIZE] =
{{
{format_bytes(delta_data)}
}};
"""


def build_animation(
    indexes: Sequence[Sequence[int]],
    merge_gap: int,
    small_run_limit: int,
) -> tuple[list[int], list[int], list[int], DeltaStats]:
    """Build first-frame data, delta offsets, delta bytes, and statistics."""
    first_frame = pack_indexes(indexes[0])

    offsets = [0]
    delta_data: list[int] = []
    aggregate = DeltaStats()
    for frame_index in range(len(indexes)):
        next_index = (frame_index + 1) % len(indexes)
        encoded, stats = encode_delta(
            indexes[next_index],
            indexes[frame_index],
            merge_gap,
            small_run_limit,
        )
        if apply_delta(encoded, indexes[frame_index]) != indexes[next_index]:
            raise ValueError(f"delta round-trip failed after frame {frame_index}")
        aggregate.merge(stats)
        delta_data.extend(encoded)
        offsets.append(len(delta_data))

    aggregate.delta_bytes = len(delta_data)
    return first_frame, offsets, delta_data, aggregate


def format_stats(merge_gap: int, stats: DeltaStats) -> str:
    """Format one statistics line for comparing gap-merge settings."""
    return (
        f"merge_gap={merge_gap}: "
        f"delta_bytes={stats.delta_bytes}, "
        f"draw_runs={stats.draw_runs}, "
        f"skip_runs={stats.skip_runs}, "
        f"dma_bytes={stats.dma_bytes}, "
        f"draw_source_pixels={stats.draw_source_pixels}, "
        f"min_run={stats.min_draw_run}, "
        f"max_run={stats.max_draw_run}, "
        f"small_runs={stats.small_draw_runs}"
    )


def parse_gap_list(text: str) -> list[int]:
    """Parse a comma-separated list of non-negative merge gaps."""
    gaps: list[int] = []
    if not text.strip():
        return gaps

    for item in text.split(","):
        gap = int(item.strip())
        if gap < 0:
            raise ValueError("merge gaps must be non-negative")
        if gap not in gaps:
            gaps.append(gap)
    return gaps


def parse_args() -> argparse.Namespace:
    """Parse command-line options for ST7789 GIF generation and analysis."""
    parser = argparse.ArgumentParser(
        description="Convert the local 120x120 cat GIF into ST7789 animation data."
    )
    parser.add_argument(
        "--merge-gap",
        type=int,
        default=1,
        help="unchanged source-pixel gap length to merge into drawing runs",
    )
    parser.add_argument(
        "--compare-gaps",
        default="",
        help="comma-separated merge gaps to report without changing output",
    )
    parser.add_argument(
        "--small-run-limit",
        type=int,
        default=4,
        help="source-pixel run length counted as a small drawing run",
    )
    parser.add_argument(
        "--stats-only",
        action="store_true",
        help="print statistics without writing anim_frames.c/.h",
    )
    return parser.parse_args()


def main() -> int:
    """Generate firmware animation data from the local cat GIF."""
    args = parse_args()
    if args.merge_gap < 0:
        raise ValueError("--merge-gap must be non-negative")
    if args.small_run_limit < 1:
        raise ValueError("--small-run-limit must be at least 1")

    project_root = Path(__file__).resolve().parent.parent
    source = project_root / "小猫图.gif"
    output_c = project_root / "User" / "anim_frames.c"
    output_h = project_root / "User" / "anim_frames.h"

    frames = load_frames(source)
    palette, indexes = quantize_frames(frames)
    first_frame, offsets, delta_data, stats = build_animation(
        indexes,
        args.merge_gap,
        args.small_run_limit,
    )

    report_gaps = parse_gap_list(args.compare_gaps)
    if args.merge_gap not in report_gaps:
        report_gaps.insert(0, args.merge_gap)
    for gap in report_gaps:
        if gap == args.merge_gap:
            report_stats = stats
        else:
            _, _, _, report_stats = build_animation(
                indexes,
                gap,
                args.small_run_limit,
            )
        print(format_stats(gap, report_stats))

    if args.stats_only:
        return 0

    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    output_h.write_text(
        build_header(len(indexes), len(delta_data), source_hash),
        encoding="gbk",
        newline="\n",
    )
    output_c.write_text(
        build_source(palette, first_frame, offsets, delta_data),
        encoding="gbk",
        newline="\n",
    )
    print(
        f"generated {len(indexes)} frames: "
        f"{len(first_frame)} first-frame bytes + "
        f"{len(delta_data)} delta bytes "
        f"(merge_gap={args.merge_gap})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
