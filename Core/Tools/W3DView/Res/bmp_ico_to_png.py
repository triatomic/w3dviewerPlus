#!/usr/bin/env python3
r"""
Convert .bmp and .ico files to .png with automatic transparency detection.

For .ico files: each contained frame is exported as <name>_<WxH>.png. ICO already
carries an alpha channel or an AND-mask, so we just pass through ImageMagick.

For .bmp files: BMPs are opaque. We auto-detect the mask color by sampling the
four corner pixels - if at least 3 of them share the same RGB, that color is
treated as the transparency key and replaced with alpha. Override with --key.

Requires ImageMagick (the `magick` CLI on PATH, v7+).

Usage:
    python bmp_ico_to_png.py file_or_dir [file_or_dir ...]
                            [-o OUTDIR] [-k R,G,B] [--no-auto] [--fuzz PCT]
                            [-top] [--size N]

Examples:
    python bmp_ico_to_png.py Toolbar.bmp
    python bmp_ico_to_png.py icons\dark -o icons\dark\png
    python bmp_ico_to_png.py icons\dark\Toolbar.bmp -k 192,192,192
    python bmp_ico_to_png.py icons\dark -top --size 256
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path


def find_magick() -> str:
    """Return the ImageMagick CLI to invoke. Prefers v7 `magick`."""
    for candidate in ("magick", "convert"):
        if shutil.which(candidate):
            return candidate
    sys.exit("ImageMagick not found on PATH. Install from https://imagemagick.org/")


def sample_bmp_corner_colors(bmp_path: Path) -> list[tuple[int, int, int]] | None:
    """Read a BMP header + the 4 corner pixels. Returns None if format is unsupported."""
    data = bmp_path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        return None
    pixel_off = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]

    if compression != 0:
        return None  # we only handle BI_RGB
    if width <= 0 or height == 0:
        return None
    top_down = height < 0
    h = abs(height)

    def pixel_at(col: int, row_from_top: int) -> tuple[int, int, int] | None:
        row = row_from_top if top_down else (h - 1 - row_from_top)
        if bpp == 24:
            stride = ((width * 3 + 3) // 4) * 4
            off = pixel_off + row * stride + col * 3
            if off + 3 > len(data):
                return None
            b, g, r = data[off], data[off + 1], data[off + 2]
            return (r, g, b)
        if bpp == 32:
            stride = width * 4
            off = pixel_off + row * stride + col * 4
            if off + 4 > len(data):
                return None
            b, g, r = data[off], data[off + 1], data[off + 2]
            return (r, g, b)
        if bpp in (1, 4, 8):
            # Palette indexed. Palette starts at 14 + DIB header size.
            dib_size = struct.unpack_from("<I", data, 14)[0]
            pal_off = 14 + dib_size
            stride = (((width * bpp) + 31) // 32) * 4
            row_off = pixel_off + row * stride
            if bpp == 8:
                idx = data[row_off + col]
            elif bpp == 4:
                byte = data[row_off + (col >> 1)]
                idx = (byte >> 4) if (col & 1) == 0 else (byte & 0x0F)
            else:  # 1
                byte = data[row_off + (col >> 3)]
                idx = (byte >> (7 - (col & 7))) & 1
            ent = pal_off + idx * 4
            if ent + 3 > len(data):
                return None
            b, g, r = data[ent], data[ent + 1], data[ent + 2]
            return (r, g, b)
        return None

    corners = [
        pixel_at(0, 0),
        pixel_at(width - 1, 0),
        pixel_at(0, h - 1),
        pixel_at(width - 1, h - 1),
    ]
    return [c for c in corners if c is not None]


def detect_transparency_key(bmp_path: Path) -> tuple[int, int, int] | None:
    """Pick the most common corner color when >=3 of 4 corners agree."""
    corners = sample_bmp_corner_colors(bmp_path)
    if not corners:
        return None
    top, count = Counter(corners).most_common(1)[0]
    return top if count >= 3 else None


def convert_bmp(magick: str, src: Path, dst_dir: Path, key: tuple[int, int, int] | None,
                fuzz_pct: float, size: int | None, filt: str) -> Path:
    dst = dst_dir / f"{src.stem}.png"
    cmd: list[str] = [magick, str(src)]
    if key is not None:
        r, g, b = key
        if fuzz_pct > 0:
            cmd += ["-fuzz", f"{fuzz_pct}%"]
        cmd += ["-transparent", f"rgb({r},{g},{b})"]
    if size is not None:
        # `!` forces exact dimensions even if aspect would normally be preserved.
        cmd += ["-filter", filt, "-resize", f"{size}x{size}!"]
    # Force true-color RGBA output. png:format=png32 = 8-bit RGBA, color type 6;
    # ImageMagick's PNG encoder would otherwise downgrade to Grayscale/Palette
    # when the image fits those modes.
    cmd += ["-alpha", "on", "-colorspace", "sRGB",
            "-type", "TrueColorAlpha", "-depth", "8",
            "-define", "png:format=png32",
            "-define", "png:color-type=6",
            "-define", "png:bit-depth=8"]
    cmd.append(str(dst))
    note = (f"  (key=rgb{key})" if key else "  (opaque)")
    if size is not None:
        note += f"  resized to {size}x{size} ({filt})"
    print(f"  bmp  -> {dst.name}" + note)
    subprocess.run(cmd, check=True)
    return dst


def convert_ico(magick: str, src: Path, dst_dir: Path,
                top_only: bool = False, size: int | None = None,
                filt: str = "Lanczos") -> list[Path]:
    """Export frames in the .ico as PNGs suffixed with their dimensions.

    With top_only=True, only the largest frame is written and the file is named
    <stem>.png (no size suffix).
    """
    tmp_pattern = dst_dir / f"{src.stem}_frame_%d.png"
    cmd = [magick, str(src), str(tmp_pattern)]
    subprocess.run(cmd, check=True)

    # Collect (frame_path, width, height) for every emitted frame.
    frames: list[tuple[Path, int, int]] = []
    for frame in sorted(dst_dir.glob(f"{src.stem}_frame_*.png")):
        ident = subprocess.run(
            [magick, "identify", "-format", "%w %h", str(frame)],
            check=True, capture_output=True, text=True,
        ).stdout.strip().split()
        frames.append((frame, int(ident[0]), int(ident[1])))

    def normalize_in_place(p: Path) -> None:
        # Always force true-color RGBA (PNG color type 6) so downstream tools
        # don't have to deal with indexed/grayscale/palette PNGs. Resize at the
        # same time if a target size was requested.
        cmd = [magick, str(p)]
        if size is not None:
            cmd += ["-filter", filt, "-resize", f"{size}x{size}!"]
        cmd += ["-alpha", "on", "-colorspace", "sRGB",
                "-type", "TrueColorAlpha", "-depth", "8",
                "-define", "png:format=png32",
                "-define", "png:color-type=6",
                "-define", "png:bit-depth=8",
                str(p)]
        subprocess.run(cmd, check=True)

    out: list[Path] = []
    if top_only and frames:
        # Some .icos use a large outer frame as transparent padding with a tiny
        # graphic in the middle, while the small frame holds the actual art.
        # Pick by non-transparent pixel count (real painted area). Use a soft
        # alpha threshold so anti-aliased edges still count.
        # ImageMagick: -alpha extract -threshold 1% -format "%[fx:mean*w*h]"
        # gives the number of pixels with alpha > 1%.
        def opaque_pixels(frame_path: Path) -> float:
            r = subprocess.run(
                [magick, str(frame_path),
                 "-alpha", "extract", "-threshold", "1%",
                 "-format", "%[fx:mean*w*h]", "info:"],
                check=True, capture_output=True, text=True,
            )
            try:
                return float(r.stdout.strip())
            except ValueError:
                return 0.0

        scored = [(opaque_pixels(f), w * h, i, f, w, h)
                  for i, (f, w, h) in enumerate(frames)]
        # Sort: most opaque pixels first; then largest dimensions; then later
        # frame index (typically the higher bit-depth variant in ICO ordering).
        scored.sort(key=lambda t: (t[0], t[1], t[2]), reverse=True)
        _, _, _, best_frame, w, h = scored[0]

        final = dst_dir / f"{src.stem}.png"
        if final.exists():
            final.unlink()
        best_frame.rename(final)
        normalize_in_place(final)
        out.append(final)
        opaque = int(scored[0][0])
        note = f"  (top {w}x{h}, {opaque} opaque px)"
        if size is not None:
            note += f", resized to {size}x{size} ({filt})"
        print(f"  ico  -> {final.name}" + note)
        # Discard the rest.
        for frame, _, _ in frames:
            if frame.exists():
                frame.unlink()
        return out

    for frame, w, h in frames:
        final = dst_dir / f"{src.stem}_{w}x{h}.png"
        if final.exists():
            i = 2
            while (cand := dst_dir / f"{src.stem}_{w}x{h}_{i}.png").exists():
                i += 1
            final = cand
        frame.rename(final)
        normalize_in_place(final)
        out.append(final)
        note = f"  (resized to {size}x{size} ({filt}))" if size is not None else ""
        print(f"  ico  -> {final.name}" + note)
    return out


def iter_targets(paths: list[Path]) -> list[Path]:
    # Use a set keyed on the resolved path so case-insensitive filesystems
    # (Windows) don't yield each file once per glob casing.
    seen: set[Path] = set()
    out: list[Path] = []

    def add(p: Path) -> None:
        key = p.resolve()
        if key in seen:
            return
        seen.add(key)
        out.append(p)

    for p in paths:
        if p.is_file():
            add(p)
        elif p.is_dir():
            for ext in ("*.bmp", "*.BMP", "*.ico", "*.ICO"):
                for hit in sorted(p.rglob(ext)):
                    add(hit)
        else:
            print(f"skip (not found): {p}", file=sys.stderr)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", type=Path,
                    help="Files and/or directories. Directories are searched recursively.")
    ap.add_argument("-o", "--outdir", type=Path, default=None,
                    help="Output directory. Defaults to the source file's directory.")
    ap.add_argument("-k", "--key", default=None,
                    help='Transparency color for BMPs as "R,G,B" (e.g. 192,192,192). '
                         "Disables auto-detection.")
    ap.add_argument("--no-auto", action="store_true",
                    help="Disable BMP corner-color auto-detection (write opaque PNGs).")
    ap.add_argument("--fuzz", type=float, default=0.0,
                    help="ImageMagick -fuzz percentage when applying -transparent. "
                         "Useful for anti-aliased BMP edges. Default 0.")
    ap.add_argument("-top", "--top", action="store_true",
                    help="For .ico inputs, only export the highest-resolution frame. "
                         "Output filename is <stem>.png with no size suffix.")
    ap.add_argument("--size", type=int, default=None, metavar="N",
                    help="Resize every exported PNG to NxN pixels (e.g. --size 256). "
                         "Default: preserve native frame size. Useful with -top to "
                         "produce a consistent 256x256 set for AI-driven restyling.")
    ap.add_argument("--filter", default="Lanczos", metavar="NAME",
                    help="ImageMagick resize filter when --size is used. "
                         "Common: Lanczos (default, sharp downscale), Mitchell, "
                         "Triangle (bilinear), Point (nearest-neighbor, no "
                         "interpolation - blocky upscale, pixel-art preserving). "
                         "Aliases: 'none', 'nearest', 'box' map to Point.")
    args = ap.parse_args()

    if args.size is not None and args.size <= 0:
        sys.exit("--size must be a positive integer")

    # User-friendly aliases for "no interpolation" -> ImageMagick's Point filter.
    filter_aliases = {
        "none": "Point", "nearest": "Point", "nn": "Point",
        "box": "Point", "point": "Point",
    }
    filt = filter_aliases.get(args.filter.lower(), args.filter)

    forced_key: tuple[int, int, int] | None = None
    if args.key:
        try:
            parts = [int(x) for x in args.key.split(",")]
            if len(parts) != 3 or not all(0 <= v <= 255 for v in parts):
                raise ValueError
            forced_key = (parts[0], parts[1], parts[2])
        except ValueError:
            sys.exit('--key must be three ints 0-255, e.g. "192,192,192"')

    magick = find_magick()
    targets = iter_targets(args.inputs)
    if not targets:
        sys.exit("No .bmp or .ico files found.")

    for src in targets:
        dst_dir = args.outdir or src.parent
        dst_dir.mkdir(parents=True, exist_ok=True)
        ext = src.suffix.lower()
        try:
            if ext == ".bmp":
                key = forced_key
                if key is None and not args.no_auto:
                    key = detect_transparency_key(src)
                convert_bmp(magick, src, dst_dir, key, args.fuzz, args.size, filt)
            elif ext == ".ico":
                convert_ico(magick, src, dst_dir, top_only=args.top,
                            size=args.size, filt=filt)
        except subprocess.CalledProcessError as e:
            print(f"  ! failed: {src} ({e})", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
