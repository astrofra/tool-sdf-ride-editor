from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. Install it with `python -m pip install pillow`."
    ) from exc


DEFAULT_COMMON_ROOT = Path(__file__).resolve().parent.parent / "assets" / "common"
SUPPORTED_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}
CHANNEL_ORDER = ("ao", "roughness", "metalness")
CHANNEL_FALLBACKS = {
    "ao": 255,
    "roughness": 255,
    "metalness": 0,
}

try:
    RESAMPLE = Image.Resampling.LANCZOS
except AttributeError:
    RESAMPLE = Image.LANCZOS


@dataclass(frozen=True)
class ChannelMatch:
    channel: str
    path: Path
    score: int
    reason: str


def tokenize_stem(path: Path) -> list[str]:
    return [token for token in re.split(r"[^a-z0-9]+", path.stem.lower()) if token]


def compact_stem(path: Path) -> str:
    return re.sub(r"[^a-z0-9]+", "", path.stem.lower())


def is_supported_image(path: Path) -> bool:
    return path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS


def should_ignore_as_output(path: Path) -> bool:
    tokens = set(tokenize_stem(path))
    compact = compact_stem(path)
    return (
        "orm" in tokens
        or compact.endswith("orm")
        or compact.endswith("occlusionroughnessmetalness")
        or compact.endswith("occlusionroughnessmetallic")
    )


def score_channel(path: Path, channel: str) -> ChannelMatch | None:
    tokens = tokenize_stem(path)
    token_set = set(tokens)
    compact = compact_stem(path)
    score = 0
    reason = ""

    if should_ignore_as_output(path):
        return None

    if channel == "ao":
        if "ambientocclusion" in compact:
            score = 120
            reason = "ambientocclusion"
        elif "ambient" in token_set and "occlusion" in token_set:
            score = 115
            reason = "ambient+occlusion"
        elif "ao" in token_set:
            score = 80
            reason = "ao"
        elif "occlusion" in token_set:
            score = 60
            reason = "occlusion"
    elif channel == "roughness":
        if "roughness" in compact:
            score = 120
            reason = "roughness"
        elif "rough" in token_set:
            score = 70
            reason = "rough"
    elif channel == "metalness":
        if "metalness" in compact:
            score = 120
            reason = "metalness"
        elif "metallic" in compact:
            score = 110
            reason = "metallic"
        elif "metal" in token_set:
            score = 70
            reason = "metal"
    else:
        raise ValueError(f"Unsupported channel: {channel}")

    if score <= 0:
        return None

    if tokens:
        last_token = tokens[-1]
        if channel == "ao" and last_token in {"ambientocclusion", "ao", "occlusion"}:
            score += 10
        elif channel == "roughness" and last_token in {"roughness", "rough"}:
            score += 10
        elif channel == "metalness" and last_token in {"metalness", "metallic", "metal"}:
            score += 10

    return ChannelMatch(channel=channel, path=path, score=score, reason=reason)


def choose_best_match(paths: Iterable[Path], channel: str) -> ChannelMatch | None:
    matches = [match for path in paths if (match := score_channel(path, channel)) is not None]
    if not matches:
        return None
    matches.sort(key=lambda match: (-match.score, len(match.path.name), match.path.name.lower()))
    return matches[0]


def read_image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def load_scalar_channel(path: Path, size: tuple[int, int]) -> Image.Image:
    with Image.open(path) as image:
        scalar = image.convert("L")
    if scalar.size != size:
        scalar = scalar.resize(size, RESAMPLE)
    return scalar


def display_path(path: Path) -> str:
    try:
        return str(path.relative_to(Path.cwd()))
    except ValueError:
        return str(path)


def resolve_output_path(material_dir: Path, args: argparse.Namespace) -> Path:
    try:
        file_name = args.output_name.format(folder=material_dir.name)
    except KeyError as exc:
        raise SystemExit(
            f"Unsupported placeholder in --output-name: {exc}. Only {{folder}} is allowed."
        ) from exc

    output_dir = args.output_dir if args.output_dir is not None else material_dir
    return output_dir / file_name


def build_material_orm(material_dir: Path, args: argparse.Namespace) -> tuple[str, bool]:
    images = [path for path in sorted(material_dir.iterdir()) if is_supported_image(path)]
    matches = {channel: choose_best_match(images, channel) for channel in CHANNEL_ORDER}

    if not any(matches.values()):
        return (f"[skip] {material_dir.name}: no AO / roughness / metalness source found", False)

    readable_matches: list[ChannelMatch] = []
    for match in matches.values():
        if match is None:
            continue
        try:
            read_image_size(match.path)
        except OSError as exc:
            return (f"[error] {material_dir.name}: cannot read {display_path(match.path)} ({exc})", False)
        readable_matches.append(match)

    if not readable_matches:
        return (f"[skip] {material_dir.name}: no readable source map found", False)

    size_candidates = []
    for match in readable_matches:
        width, height = read_image_size(match.path)
        size_candidates.append((width * height, width, height))
    _, width, height = max(size_candidates)
    target_size = (width, height)

    output_path = resolve_output_path(material_dir, args)
    source_paths = {match.path.resolve() for match in readable_matches}
    if output_path.resolve() in source_paths:
        return (
            f"[error] {material_dir.name}: output collides with an input map ({display_path(output_path)})",
            False,
        )

    if output_path.exists() and not args.overwrite:
        return (
            f"[skip] {material_dir.name}: {display_path(output_path)} already exists (use --overwrite)",
            False,
        )

    channels = []
    notes = []
    for channel in CHANNEL_ORDER:
        match = matches[channel]
        if match is None:
            channels.append(Image.new("L", target_size, CHANNEL_FALLBACKS[channel]))
            notes.append(f"{channel}=fallback({CHANNEL_FALLBACKS[channel]})")
            continue
        try:
            channels.append(load_scalar_channel(match.path, target_size))
        except OSError as exc:
            return (f"[error] {material_dir.name}: cannot load {display_path(match.path)} ({exc})", False)
        notes.append(f"{channel}={match.path.name}")

    if args.dry_run:
        return (
            f"[dry-run] {material_dir.name}: would write {display_path(output_path)}"
            f" [{', '.join(notes)}]",
            True,
        )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    orm_image = Image.merge("RGB", tuple(channels))
    orm_image.save(output_path)
    return (
        f"[ok] {material_dir.name}: wrote {display_path(output_path)}"
        f" [{', '.join(notes)}]",
        True,
    )


def iter_material_dirs(common_root: Path, folders: list[str]) -> list[Path]:
    if folders:
        material_dirs = []
        for folder in folders:
            material_dir = common_root / folder
            if material_dir.is_dir():
                material_dirs.append(material_dir)
            else:
                print(f"[warn] Missing material folder: {display_path(material_dir)}")
        return material_dirs
    return sorted(path for path in common_root.iterdir() if path.is_dir())


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Pack AO, roughness and metalness textures into RGB ORM maps."
    )
    parser.add_argument(
        "--common-root",
        type=Path,
        default=DEFAULT_COMMON_ROOT,
        help=f"Root folder containing material subfolders (default: {DEFAULT_COMMON_ROOT})",
    )
    parser.add_argument(
        "--folder",
        dest="folders",
        action="append",
        default=[],
        help="Material folder name to process. Repeat the flag to target more than one folder.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Optional output directory. Defaults to each source material folder.",
    )
    parser.add_argument(
        "--output-name",
        default="{folder}_orm.png",
        help="Output file name template. Only {folder} is supported. Default: {folder}_orm.png",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing outputs.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned outputs without writing files.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    common_root = args.common_root.resolve()
    args.common_root = common_root
    if args.output_dir is not None:
        args.output_dir = args.output_dir.resolve()

    if not common_root.is_dir():
        print(f"[error] Missing common root: {display_path(common_root)}")
        return 1

    material_dirs = iter_material_dirs(common_root, args.folders)
    if not material_dirs:
        print("[error] No material folders to process.")
        return 1
    if len(material_dirs) > 1 and args.output_dir is not None and "{folder}" not in args.output_name:
        print("[error] --output-name must contain {folder} when --output-dir is shared by multiple folders.")
        return 1

    generated_count = 0
    for material_dir in material_dirs:
        message, generated = build_material_orm(material_dir, args)
        print(message)
        if generated:
            generated_count += 1

    print(
        f"Processed {len(material_dirs)} material folder(s); "
        f"{generated_count} ORM texture(s) {'planned' if args.dry_run else 'written'}."
    )
    return 0 if generated_count > 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
