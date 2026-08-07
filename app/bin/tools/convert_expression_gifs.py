from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "Missing dependency: Pillow. Install it with `python -m pip install pillow`."
    ) from exc


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_INPUT_DIR = REPO_ROOT / "works" / "expressions" / "gifs"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "src" / "assets" / "common" / "expressions"


def parse_rgb(value: str) -> tuple[int, int, int]:
    parts = value.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("Expected RGB as R,G,B, for example 0,0,0.")

    try:
        rgb = tuple(int(part.strip()) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("RGB values must be integers.") from exc

    if any(channel < 0 or channel > 255 for channel in rgb):
        raise argparse.ArgumentTypeError("RGB values must be between 0 and 255.")

    return rgb  # type: ignore[return-value]


def frame_to_rgb(frame: Image.Image, background: tuple[int, int, int]) -> Image.Image:
    rgba = frame.convert("RGBA")
    rgb = Image.new("RGB", rgba.size, background)
    rgb.paste(rgba, mask=rgba.getchannel("A"))
    return rgb


def convert_gif_to_strip(
    gif_path: Path,
    output_dir: Path,
    background: tuple[int, int, int],
    overwrite: bool,
) -> Path:
    output_path = output_dir / f"{gif_path.stem}.png"
    if output_path.exists() and not overwrite:
        raise FileExistsError(f"{output_path} already exists. Use --overwrite to replace it.")

    with Image.open(gif_path) as gif:
        frame_count = getattr(gif, "n_frames", 1)
        frames: list[Image.Image] = []

        for frame_index in range(frame_count):
            gif.seek(frame_index)
            frames.append(frame_to_rgb(gif.copy(), background))

    if not frames:
        raise ValueError(f"No frame found in {gif_path}.")

    frame_width, frame_height = frames[0].size
    if any(frame.size != (frame_width, frame_height) for frame in frames):
        raise ValueError(f"All frames must have the same size in {gif_path}.")

    strip = Image.new("RGB", (frame_width * len(frames), frame_height), background)
    for frame_index, frame in enumerate(frames):
        strip.paste(frame, (frame_index * frame_width, 0))

    output_dir.mkdir(parents=True, exist_ok=True)
    strip.save(output_path, "PNG")
    return output_path


def convert_all_gifs(
    input_dir: Path,
    output_dir: Path,
    background: tuple[int, int, int],
    overwrite: bool,
) -> int:
    if not input_dir.exists():
        raise FileNotFoundError(f"Input directory does not exist: {input_dir}")

    gif_paths = sorted(input_dir.glob("*.gif"))
    if not gif_paths:
        print(f"No GIF found in {input_dir}")
        return 0

    converted_count = 0
    for gif_path in gif_paths:
        output_path = convert_gif_to_strip(gif_path, output_dir, background, overwrite)
        print(f"{gif_path.name} -> {output_path.relative_to(REPO_ROOT)}")
        converted_count += 1

    print(f"Converted {converted_count} GIF(s).")
    return converted_count


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert expression GIFs into horizontal RGB PNG strips."
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=DEFAULT_INPUT_DIR,
        help=f"GIF source directory. Default: {DEFAULT_INPUT_DIR}",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"PNG output directory. Default: {DEFAULT_OUTPUT_DIR}",
    )
    parser.add_argument(
        "--background",
        type=parse_rgb,
        default=(0, 0, 0),
        help="RGB background used when GIF frames contain transparency. Default: 0,0,0",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace existing PNG files.",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        convert_all_gifs(
            input_dir=args.input_dir.resolve(),
            output_dir=args.output_dir.resolve(),
            background=args.background,
            overwrite=args.overwrite,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
