#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SUPPORTED_EXTENSIONS = {
    ".aiff",
    ".aif",
    ".flac",
    ".m4a",
    ".mp3",
    ".ogg",
    ".wav",
}

SCRIPT_PATH = Path(__file__).resolve()
TOOLS_DIR = SCRIPT_PATH.parent
BIN_DIR = TOOLS_DIR.parent
SRC_DIR = BIN_DIR.parent
PROJECT_DIR = SRC_DIR.parent
MANIFEST_PATH = SRC_DIR / "songs_manifest.lua"
FONT_CANDIDATES = [
    SRC_DIR / "assets" / "fonts" / "WDXLLubrifontTC-Regular.ttf",
    SRC_DIR / "assets" / "fonts" / "m20.TTF",
]


def run_cmd(args: list[str], timeout: int = 60) -> str:
    result = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(str(arg) for arg in args)}\n"
            f"{result.stderr.strip()}"
        )
    return result.stdout


def find_lua() -> str | None:
    candidates = [
        BIN_DIR / "hg_lua-win-x64" / "lua.exe",
        BIN_DIR / "hg_lua-win-x64" / "lua",
        shutil.which("lua.exe"),
        shutil.which("lua"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return None


def find_font(font_override: Path | None) -> Path | None:
    candidates = []
    if font_override is not None:
        candidates.append(font_override.resolve())
    candidates.extend(FONT_CANDIDATES)

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def escape_drawtext(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace(":", r"\:")
        .replace("'", r"\'")
        .replace("%", r"\%")
        .replace(",", r"\,")
        .replace("[", r"\[")
        .replace("]", r"\]")
    )


def fallback_label(audio_file: Path) -> tuple[str, str]:
    return audio_file.stem.replace("_", " "), ""


def load_song_labels() -> dict[str, tuple[str, str]]:
    if not MANIFEST_PATH.is_file():
        print(f"warn  manifest not found: {MANIFEST_PATH}", file=sys.stderr)
        return {}

    lua_cmd = find_lua()
    if lua_cmd is None:
        print("warn  lua not found, using filenames instead of song metadata", file=sys.stderr)
        return {}

    bootstrap = """
local manifest = dofile(arg[1])

if type(manifest) ~= "table" or type(manifest.songs) ~= "table" then
    error("songs_manifest.lua must return a table with a songs field")
end

local function json_escape(value)
    return string.format("%q", value)
end

local ids = {}
for id in pairs(manifest.songs) do
    table.insert(ids, id)
end
table.sort(ids)

local parts = {}
table.insert(parts, "{")

for index, id in ipairs(ids) do
    local song = manifest.songs[id]
    if index > 1 then
        table.insert(parts, ",")
    end
    table.insert(parts, json_escape(id))
    table.insert(parts, ":{")
    table.insert(parts, "\\"title\\":")
    table.insert(parts, json_escape(song.title or ""))
    table.insert(parts, ",\\"artist\\":")
    table.insert(parts, json_escape(song.artist or ""))
    table.insert(parts, "}")
end

table.insert(parts, "}")
io.write(table.concat(parts))
"""

    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", suffix=".lua", delete=False
        ) as handle:
            handle.write(bootstrap)
            temp_path = Path(handle.name)

        payload = run_cmd([lua_cmd, str(temp_path), str(MANIFEST_PATH)], timeout=30)
        parsed = json.loads(payload)
    except (OSError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"warn  could not load songs manifest: {exc}", file=sys.stderr)
        return {}
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()

    labels: dict[str, tuple[str, str]] = {}
    for song_id, song in parsed.items():
        if not isinstance(song, dict):
            continue
        title = str(song.get("title", "")).strip()
        artist = str(song.get("artist", "")).strip()
        labels[song_id] = (title, artist)
    return labels


def build_parser() -> argparse.ArgumentParser:
    default_input = PROJECT_DIR / "works" / "audio"
    default_output = default_input / "spectrograms"

    parser = argparse.ArgumentParser(
        description=(
            "Scan an audio folder and generate readable spectrogram PNGs "
            "for each supported file by calling ffmpeg."
        )
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=default_input,
        help=f"Audio folder to scan recursively (default: {default_input})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"Output folder for generated PNGs (default: {default_output})",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=2048,
        help="Spectrogram width in pixels (default: 2048)",
    )
    parser.add_argument(
        "--height",
        type=int,
        default=1024,
        help="Spectrogram height in pixels (default: 1024)",
    )
    parser.add_argument(
        "--color",
        default="magma",
        choices=[
            "channel",
            "intensity",
            "rainbow",
            "moreland",
            "nebulae",
            "fire",
            "fiery",
            "fruit",
            "cool",
            "magma",
            "green",
            "viridis",
            "plasma",
            "cividis",
            "terrain",
        ],
        help="Color map used by ffmpeg showspectrumpic (default: magma)",
    )
    parser.add_argument(
        "--mode",
        default="combined",
        choices=["combined", "separate"],
        help="How stereo channels are rendered (default: combined)",
    )
    parser.add_argument(
        "--ffmpeg",
        default="ffmpeg",
        help="ffmpeg executable to use (default: ffmpeg)",
    )
    parser.add_argument(
        "--font",
        type=Path,
        help="Optional TTF font file for title and artist overlay",
    )
    parser.add_argument(
        "--no-labels",
        action="store_true",
        help="Disable the title/artist banner on generated spectrograms",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Regenerate PNGs even when the output file already exists",
    )
    return parser


def find_audio_files(audio_root: Path) -> list[Path]:
    return sorted(
        path
        for path in audio_root.rglob("*")
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS
    )


def output_path_for(audio_file: Path, input_root: Path, output_root: Path) -> Path:
    relative_audio = audio_file.relative_to(input_root)
    return (output_root / relative_audio).with_suffix(".png")


def render_spectrogram(
    ffmpeg: str,
    audio_file: Path,
    image_file: Path,
    width: int,
    height: int,
    color: str,
    mode: str,
    font_path: Path | None,
    title: str,
    artist: str,
    show_labels: bool,
) -> None:
    filter_parts = [
        f"showspectrumpic="
        f"s={width}x{height}:"
        f"mode={mode}:"
        f"color={color}:"
        f"scale=log:"
        f"fscale=log:"
        f"win_func=bharris:"
        f"legend=1:"
        f"gain=2:"
        f"start=20:"
        f"stop=20000:"
        f"drange=120,"
        f"scale={width}:-2"
    ]

    if show_labels and font_path is not None:
        header_height = max(140, width // 12)
        title_size = max(38, width // 30)
        artist_size = max(24, width // 52)
        title_text = escape_drawtext(title)
        artist_text = escape_drawtext(artist)
        font = escape_drawtext(str(font_path))

        filter_parts.extend(
            [
                f"pad=iw:ih+{header_height}:0:{header_height}:color=black",
                f"drawbox=x=0:y=0:w=iw:h={header_height}:color=black@0.92:t=fill",
                (
                    "drawtext="
                    f"fontfile='{font}':"
                    f"text='{title_text}':"
                    f"fontcolor=white:"
                    f"fontsize={title_size}:"
                    "x=48:"
                    "y=26:"
                    "fix_bounds=1"
                ),
            ]
        )
        if artist:
            filter_parts.append(
                (
                    "drawtext="
                    f"fontfile='{font}':"
                    f"text='{artist_text}':"
                    "fontcolor=#f8d9a6:"
                    f"fontsize={artist_size}:"
                    "x=52:"
                    f"y={26 + title_size + 10}:"
                    "fix_bounds=1"
                )
            )

    filter_args = ",".join(filter_parts)

    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        str(audio_file),
        "-lavfi",
        filter_args,
        "-frames:v",
        "1",
        str(image_file),
    ]
    subprocess.run(command, check=True)


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    input_root = args.input.resolve()
    output_root = args.output.resolve()
    labels = load_song_labels()
    font_path = None if args.no_labels else find_font(args.font)

    if args.width <= 0 or args.height <= 0:
        parser.error("--width and --height must be positive integers")

    if not input_root.exists():
        parser.error(f"Input folder does not exist: {input_root}")

    if not input_root.is_dir():
        parser.error(f"Input path is not a folder: {input_root}")

    if shutil.which(args.ffmpeg) is None:
        parser.error(
            f"Unable to find ffmpeg executable '{args.ffmpeg}' in PATH. "
            "Install ffmpeg or pass --ffmpeg with an explicit path."
        )
    if not args.no_labels and font_path is None:
        print("warn  no font found, generating spectrograms without text overlay", file=sys.stderr)

    audio_files = find_audio_files(input_root)
    if not audio_files:
        print(f"No supported audio files found in {input_root}")
        return 0

    output_root.mkdir(parents=True, exist_ok=True)

    generated = 0
    skipped = 0
    failed = 0

    for audio_file in audio_files:
        image_file = output_path_for(audio_file, input_root, output_root)
        image_file.parent.mkdir(parents=True, exist_ok=True)

        if image_file.exists() and not args.overwrite:
            skipped += 1
            print(f"skip  {audio_file.name} -> {image_file.name}")
            continue

        print(f"build {audio_file.name} -> {image_file.name}")
        title, artist = labels.get(audio_file.stem, fallback_label(audio_file))

        try:
            render_spectrogram(
                ffmpeg=args.ffmpeg,
                audio_file=audio_file,
                image_file=image_file,
                width=args.width,
                height=args.height,
                color=args.color,
                mode=args.mode,
                font_path=font_path,
                title=title,
                artist=artist,
                show_labels=not args.no_labels,
            )
        except subprocess.CalledProcessError as exc:
            failed += 1
            print(
                f"fail  {audio_file.name}: ffmpeg exited with code {exc.returncode}",
                file=sys.stderr,
            )
            continue

        generated += 1

    print(
        f"Done. generated={generated} skipped={skipped} failed={failed} "
        f"output={output_root}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
