#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import tempfile
from pathlib import Path

import librosa
import numpy as np


SCRIPT_PATH = Path(__file__).resolve()
TOOLS_DIR = SCRIPT_PATH.parent
BIN_DIR = TOOLS_DIR.parent
SRC_DIR = BIN_DIR.parent
PROJECT_DIR = SRC_DIR.parent

MANIFEST_PATH = SRC_DIR / "songs_manifest.lua"
SOURCE_AUDIO_DIR = PROJECT_DIR / "works" / "audio"
ASSETS_AUDIO_DIR = SRC_DIR / "assets" / "audio"
OUTPUT_LUA_PATH = SRC_DIR / "music_analysis.lua"

FEATURES = [
    "energy",
    "onset",
    "brightness",
    "bass",
    "mid",
    "treble",
    "flatness",
    "zcr",
    "tempo_phase",
    "chroma_flux",
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


def find_lua() -> str:
    candidates = [
        BIN_DIR / "hg_lua-win-x64" / "lua.exe",
        BIN_DIR / "hg_lua-win-x64" / "lua",
        shutil.which("lua.exe"),
        shutil.which("lua"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    raise FileNotFoundError(
        "Could not find lua. Expected bin\\hg_lua-win-x64\\lua.exe or a lua executable on PATH."
    )


def load_manifest() -> dict:
    lua_cmd = find_lua()
    bootstrap = r'''
local manifest = dofile(arg[1])

if type(manifest) ~= "table" then
    error("songs_manifest.lua must return a table")
end
if type(manifest.songs) ~= "table" then
    error("songs_manifest.lua must define a songs table")
end
if type(manifest.playlist) ~= "table" then
    error("songs_manifest.lua must define a playlist array")
end

local function json_escape(value)
    return string.format("%q", value)
end

local parts = {}
table.insert(parts, "{")
table.insert(parts, "\"songs\":{")

local first_song = true
for id, song in pairs(manifest.songs) do
    if not first_song then
        table.insert(parts, ",")
    end
    first_song = false
    table.insert(parts, json_escape(id))
    table.insert(parts, ":{")
    table.insert(parts, "\"title\":")
    table.insert(parts, json_escape(song.title or ""))
    table.insert(parts, ",\"artist\":")
    table.insert(parts, json_escape(song.artist or ""))
    table.insert(parts, "}")
end

table.insert(parts, "},\"playlist\":[")
for i, id in ipairs(manifest.playlist) do
    if i > 1 then
        table.insert(parts, ",")
    end
    table.insert(parts, json_escape(id))
end
table.insert(parts, "]}")
io.write(table.concat(parts))
'''

    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", suffix=".lua", delete=False
        ) as handle:
            handle.write(bootstrap)
            temp_path = Path(handle.name)

        return json.loads(run_cmd([lua_cmd, str(temp_path), str(MANIFEST_PATH)], timeout=30))
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()


def find_audio_file(track_id: str, prefer_source_wav: bool) -> Path:
    candidates = []
    if prefer_source_wav:
        candidates.extend(
            [
                SOURCE_AUDIO_DIR / f"{track_id}.wav",
                ASSETS_AUDIO_DIR / f"{track_id}.ogg",
            ]
        )
    else:
        candidates.extend(
            [
                ASSETS_AUDIO_DIR / f"{track_id}.ogg",
                SOURCE_AUDIO_DIR / f"{track_id}.wav",
            ]
        )

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Missing audio source for `{track_id}`.")


def normalize_feature(values: np.ndarray, low_percentile: float = 5.0, high_percentile: float = 95.0) -> np.ndarray:
    values = np.asarray(values, dtype=np.float64)
    values = np.nan_to_num(values, nan=0.0, posinf=0.0, neginf=0.0)
    lo = float(np.percentile(values, low_percentile))
    hi = float(np.percentile(values, high_percentile))
    if not math.isfinite(lo) or not math.isfinite(hi) or abs(hi - lo) < 1e-9:
        return np.zeros_like(values)
    return np.clip((values - lo) / (hi - lo), 0.0, 1.0)


def resample_feature(values: np.ndarray, target_len: int) -> np.ndarray:
    values = np.asarray(values, dtype=np.float64).reshape(-1)
    if values.size == 0:
        return np.zeros(target_len, dtype=np.float64)
    if values.size == target_len:
        return values
    if values.size == 1:
        return np.full(target_len, float(values[0]), dtype=np.float64)

    old_x = np.linspace(0.0, 1.0, values.size)
    new_x = np.linspace(0.0, 1.0, target_len)
    return np.interp(new_x, old_x, values)


def band_energy(power: np.ndarray, freqs: np.ndarray, min_hz: float, max_hz: float) -> np.ndarray:
    mask = (freqs >= min_hz) & (freqs < max_hz)
    if not np.any(mask):
        return np.zeros(power.shape[1], dtype=np.float64)
    return np.mean(power[mask, :], axis=0)


def beat_phase(frame_count: int, beat_frames: np.ndarray) -> np.ndarray:
    phase = np.zeros(frame_count, dtype=np.float64)
    beat_frames = np.asarray(beat_frames, dtype=np.int64)
    beat_frames = beat_frames[(beat_frames >= 0) & (beat_frames < frame_count)]
    if beat_frames.size < 2:
        return phase

    for index in range(beat_frames.size - 1):
        start = int(beat_frames[index])
        end = int(beat_frames[index + 1])
        if end <= start:
            continue
        phase[start:end] = np.linspace(0.0, 1.0, end - start, endpoint=False)
    phase[int(beat_frames[-1]) :] = 0.0
    return phase


def extract_track_frames(path: Path, sample_rate_hz: int, analysis_sr: int) -> tuple[float, np.ndarray]:
    y, sr = librosa.load(path, sr=analysis_sr, mono=True)
    if y.size == 0:
        raise ValueError(f"Empty audio file: {path}")

    duration_sec = float(librosa.get_duration(y=y, sr=sr))
    target_len = max(1, int(math.ceil(duration_sec * sample_rate_hz)) + 1)
    hop_length = max(1, int(round(sr / sample_rate_hz)))
    n_fft = 2048

    magnitude = np.abs(librosa.stft(y, n_fft=n_fft, hop_length=hop_length))
    power = magnitude * magnitude
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)

    energy = librosa.feature.rms(y=y, frame_length=n_fft, hop_length=hop_length)[0]
    onset = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop_length)
    centroid = librosa.feature.spectral_centroid(S=magnitude, sr=sr)[0] / max(1.0, sr * 0.5)
    flatness = librosa.feature.spectral_flatness(S=magnitude)[0]
    zcr = librosa.feature.zero_crossing_rate(y, frame_length=n_fft, hop_length=hop_length)[0]
    chroma = librosa.feature.chroma_stft(S=magnitude, sr=sr)
    chroma_flux = np.concatenate(
        [np.zeros(1, dtype=np.float64), np.linalg.norm(np.diff(chroma, axis=1), axis=0)]
    )

    _, beats = librosa.beat.beat_track(onset_envelope=onset, sr=sr, hop_length=hop_length)

    raw_features = {
        "energy": normalize_feature(energy),
        "onset": normalize_feature(onset, 25.0, 98.0),
        "brightness": normalize_feature(centroid),
        "bass": normalize_feature(np.log1p(band_energy(power, freqs, 20.0, 250.0))),
        "mid": normalize_feature(np.log1p(band_energy(power, freqs, 250.0, 4000.0))),
        "treble": normalize_feature(np.log1p(band_energy(power, freqs, 4000.0, 10000.0))),
        "flatness": normalize_feature(flatness),
        "zcr": normalize_feature(zcr),
        "tempo_phase": beat_phase(max(energy.size, onset.size), beats),
        "chroma_flux": normalize_feature(chroma_flux, 10.0, 95.0),
    }

    frames = np.zeros((target_len, len(FEATURES)), dtype=np.float64)
    for feature_index, feature_name in enumerate(FEATURES):
        frames[:, feature_index] = resample_feature(raw_features[feature_name], target_len)

    frames = np.nan_to_num(frames, nan=0.0, posinf=0.0, neginf=0.0)
    frames = np.clip(frames, 0.0, 1.0)
    return duration_sec, frames


def lua_quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def write_lua(output_path: Path, tracks: list[dict], sample_rate_hz: int) -> None:
    lines = [
        "-- this file is generated, do not edit manually",
        "",
        "local music_analysis = {",
        f"\tsample_rate_hz = {sample_rate_hz},",
        "\tfeatures = {",
    ]

    for feature in FEATURES:
        lines.append(f"\t\t{lua_quote(feature)},")
    lines.extend(["\t},", "\tsongs = {"])

    for track in tracks:
        lines.extend(
            [
                f"\t\t[{lua_quote(track['id'])}] = {{",
                f"\t\t\tduration_sec = {track['duration_sec']:.6f},",
                "\t\t\tframes = {",
            ]
        )
        for frame in track["frames"]:
            values = ", ".join(f"{float(value):.3f}" for value in frame)
            lines.append(f"\t\t\t\t{{{values}}},")
        lines.extend(["\t\t\t},", "\t\t},"])

    lines.extend(["\t},", "}", "", "return music_analysis", ""])
    output_path.write_text("\n".join(lines), encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate per-song music analysis frames as a Lua table."
    )
    parser.add_argument(
        "--sample-rate-hz",
        type=int,
        default=20,
        help="Runtime analysis frame rate (default: 20).",
    )
    parser.add_argument(
        "--analysis-sr",
        type=int,
        default=22050,
        help="Sample rate used by librosa while extracting features (default: 22050).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=OUTPUT_LUA_PATH,
        help=f"Output Lua file (default: {OUTPUT_LUA_PATH}).",
    )
    parser.add_argument(
        "--prefer-ogg",
        action="store_true",
        help="Analyze generated OGG assets before source WAV files.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.sample_rate_hz <= 0:
        raise SystemExit("--sample-rate-hz must be positive.")

    manifest = load_manifest()
    playlist = manifest.get("playlist", [])
    if not playlist:
        raise SystemExit("songs_manifest.lua playlist is empty.")

    tracks = []
    for track_id in playlist:
        path = find_audio_file(track_id, prefer_source_wav=not args.prefer_ogg)
        print(f"analyze {track_id}: {path.name}")
        duration_sec, frames = extract_track_frames(path, args.sample_rate_hz, args.analysis_sr)
        tracks.append(
            {
                "id": track_id,
                "duration_sec": duration_sec,
                "frames": frames,
            }
        )

    write_lua(args.output.resolve(), tracks, args.sample_rate_hz)
    print(f"Wrote: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
