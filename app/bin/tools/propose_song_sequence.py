#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import subprocess
import sys
import tempfile
import warnings
from dataclasses import dataclass
from pathlib import Path

import librosa
import numpy as np
from sklearn.decomposition import PCA
from sklearn.preprocessing import StandardScaler


SUPPORTED_EXTENSIONS = {".wav", ".flac", ".aif", ".aiff", ".ogg", ".mp3", ".m4a"}

SCRIPT_PATH = Path(__file__).resolve()
TOOLS_DIR = SCRIPT_PATH.parent
BIN_DIR = TOOLS_DIR.parent
SRC_DIR = BIN_DIR.parent
PROJECT_DIR = SRC_DIR.parent

DEFAULT_AUDIO_DIR = PROJECT_DIR / "works" / "audio"
DEFAULT_OUTPUT_DIR = DEFAULT_AUDIO_DIR / "analysis"
MANIFEST_PATH = SRC_DIR / "songs_manifest.lua"

warnings.filterwarnings(
    "ignore",
    message="Trying to estimate tuning from empty frequency set.",
    category=UserWarning,
)


@dataclass
class TrackInfo:
    id: str
    title: str
    artist: str
    path: Path
    current_rank: int | None


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


def load_manifest() -> dict:
    if not MANIFEST_PATH.is_file():
        return {"songs": {}, "playlist": []}

    lua_cmd = find_lua()
    if lua_cmd is None:
        print("warn  lua not found, metadata will fall back to filenames", file=sys.stderr)
        return {"songs": {}, "playlist": []}

    bootstrap = """
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

local ids = {}
for id in pairs(manifest.songs) do
    table.insert(ids, id)
end
table.sort(ids)

local parts = {}
table.insert(parts, "{")
table.insert(parts, "\\"songs\\":{")

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

table.insert(parts, "},\\"playlist\\":[")

for index, id in ipairs(manifest.playlist) do
    if index > 1 then
        table.insert(parts, ",")
    end
    table.insert(parts, json_escape(id))
end

table.insert(parts, "]}")
io.write(table.concat(parts))
"""

    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", suffix=".lua", delete=False
        ) as handle:
            handle.write(bootstrap)
            temp_path = Path(handle.name)
        return json.loads(run_cmd([lua_cmd, str(temp_path), str(MANIFEST_PATH)], timeout=30))
    except (OSError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"warn  could not load manifest metadata: {exc}", file=sys.stderr)
        return {"songs": {}, "playlist": []}
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Generate audio embeddings and propose a one-axis song order "
            "that keeps neighboring tracks musically close."
        )
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_AUDIO_DIR,
        help=f"Audio folder to scan recursively (default: {DEFAULT_AUDIO_DIR})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"Output folder for embeddings and playlist proposal (default: {DEFAULT_OUTPUT_DIR})",
    )
    parser.add_argument(
        "--sr",
        type=int,
        default=22050,
        help="Resample rate used for feature extraction (default: 22050)",
    )
    parser.add_argument(
        "--segment-seconds",
        type=float,
        default=24.0,
        help="Intro/outro section length in seconds (default: 24)",
    )
    parser.add_argument(
        "--first-track",
        help="Optional track id to force as the first item of the proposed playlist",
    )
    parser.add_argument(
        "--overwrite",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Overwrite existing analysis files (default: enabled)",
    )
    return parser


def find_audio_files(audio_root: Path) -> list[Path]:
    return sorted(
        path
        for path in audio_root.rglob("*")
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS
    )


def fallback_title(track_id: str) -> str:
    return track_id.replace("_", " ")


def collect_tracks(audio_root: Path, manifest: dict) -> list[TrackInfo]:
    songs = manifest.get("songs", {})
    playlist = manifest.get("playlist", [])
    current_ranks = {track_id: index + 1 for index, track_id in enumerate(playlist)}

    tracks: list[TrackInfo] = []
    for path in find_audio_files(audio_root):
        track_id = path.stem
        song = songs.get(track_id, {})
        title = str(song.get("title") or fallback_title(track_id)).strip()
        artist = str(song.get("artist") or "").strip()
        tracks.append(
            TrackInfo(
                id=track_id,
                title=title,
                artist=artist,
                path=path,
                current_rank=current_ranks.get(track_id),
            )
        )
    return tracks


def summarize_feature(feature: np.ndarray) -> np.ndarray:
    feature = np.asarray(feature, dtype=np.float64)
    if feature.ndim == 1:
        feature = feature[np.newaxis, :]
    mean = np.mean(feature, axis=1)
    std = np.std(feature, axis=1)
    return np.concatenate([mean, std])


def safe_section(y: np.ndarray, sr: int, seconds: float, from_end: bool) -> np.ndarray:
    samples = max(1, int(seconds * sr))
    if y.size <= samples:
        return y
    return y[-samples:] if from_end else y[:samples]


def extract_section_features(y: np.ndarray, sr: int) -> tuple[np.ndarray, dict[str, float]]:
    hop_length = 2048
    n_fft = 4096
    y = np.asarray(y, dtype=np.float32)

    harmonic = librosa.effects.harmonic(y)
    mfcc = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=16, n_fft=n_fft, hop_length=hop_length)
    chroma = librosa.feature.chroma_stft(
        y=harmonic, sr=sr, n_fft=n_fft, hop_length=hop_length
    )
    contrast = librosa.feature.spectral_contrast(
        y=y, sr=sr, n_fft=n_fft, hop_length=hop_length
    )
    tonnetz = librosa.feature.tonnetz(chroma=chroma, sr=sr)

    centroid = librosa.feature.spectral_centroid(y=y, sr=sr, n_fft=n_fft, hop_length=hop_length)
    bandwidth = librosa.feature.spectral_bandwidth(
        y=y, sr=sr, n_fft=n_fft, hop_length=hop_length
    )
    rolloff = librosa.feature.spectral_rolloff(y=y, sr=sr, n_fft=n_fft, hop_length=hop_length)
    flatness = librosa.feature.spectral_flatness(y=y, n_fft=n_fft, hop_length=hop_length)
    zcr = librosa.feature.zero_crossing_rate(y, hop_length=hop_length)
    rms = librosa.feature.rms(y=y, frame_length=n_fft, hop_length=hop_length)
    tempo, _ = librosa.beat.beat_track(y=y, sr=sr, hop_length=hop_length)
    tempo_array = np.asarray(tempo, dtype=np.float64).reshape(-1)
    tempo_value = float(tempo_array[0]) if tempo_array.size else 0.0

    spectral_stack = np.vstack([centroid, bandwidth, rolloff, flatness, zcr, rms])
    embedding = np.concatenate(
        [
            summarize_feature(mfcc),
            summarize_feature(chroma),
            summarize_feature(contrast),
            summarize_feature(tonnetz),
            summarize_feature(spectral_stack),
            np.array([tempo_value, librosa.get_duration(y=y, sr=sr)], dtype=np.float64),
        ]
    )
    embedding = np.nan_to_num(embedding, nan=0.0, posinf=0.0, neginf=0.0)

    metrics = {
        "tempo_bpm": tempo_value,
        "rms_mean": float(np.mean(rms)),
        "centroid_mean": float(np.mean(centroid)),
        "rolloff_mean": float(np.mean(rolloff)),
        "flatness_mean": float(np.mean(flatness)),
        "zcr_mean": float(np.mean(zcr)),
    }
    return embedding, metrics


def extract_track_embedding(
    path: Path, sr: int, segment_seconds: float
) -> tuple[np.ndarray, dict[str, float]]:
    y, loaded_sr = librosa.load(path, sr=sr, mono=True)
    if y.size == 0:
        raise ValueError(f"Empty audio file: {path}")

    full_embedding, metrics = extract_section_features(y, loaded_sr)
    intro_embedding, _ = extract_section_features(
        safe_section(y, loaded_sr, segment_seconds, from_end=False), loaded_sr
    )
    outro_embedding, _ = extract_section_features(
        safe_section(y, loaded_sr, segment_seconds, from_end=True), loaded_sr
    )

    embedding = np.concatenate([full_embedding, intro_embedding, outro_embedding])
    embedding = np.nan_to_num(embedding, nan=0.0, posinf=0.0, neginf=0.0)
    metrics["duration_sec"] = float(librosa.get_duration(y=y, sr=loaded_sr))
    return embedding, metrics


def compute_similarity_axis(reduced_embeddings: np.ndarray, energy_reference: np.ndarray) -> np.ndarray:
    if reduced_embeddings.shape[0] <= 2:
        axis = reduced_embeddings[:, 0]
    else:
        distances = np.linalg.norm(
            reduced_embeddings[:, np.newaxis, :] - reduced_embeddings[np.newaxis, :, :],
            axis=2,
        )
        positive = distances[distances > 0]
        sigma = float(np.median(positive)) if positive.size else 1.0
        sigma = max(sigma, 1e-6)

        affinity = np.exp(-((distances**2) / (2.0 * sigma**2)))
        np.fill_diagonal(affinity, 0.0)
        degree = np.sum(affinity, axis=1)
        degree = np.where(degree <= 1e-9, 1e-9, degree)
        inv_sqrt_degree = np.diag(1.0 / np.sqrt(degree))
        laplacian = np.eye(affinity.shape[0]) - inv_sqrt_degree @ affinity @ inv_sqrt_degree
        _, eigenvectors = np.linalg.eigh(laplacian)
        axis = eigenvectors[:, 1]

    if axis.shape[0] > 1:
        corr = np.corrcoef(axis, energy_reference)[0, 1]
        if not np.isnan(corr) and corr < 0:
            axis = -axis
    return axis


def mean_adjacent_distance(order: list[int], reduced_embeddings: np.ndarray) -> float:
    if len(order) < 2:
        return 0.0
    distances = []
    for left, right in zip(order, order[1:]):
        distance = np.linalg.norm(reduced_embeddings[left] - reduced_embeddings[right])
        distances.append(float(distance))
    return float(sum(distances) / len(distances))


def build_axis_order(
    axis_scores: np.ndarray,
    reduced_embeddings: np.ndarray,
    first_track_index: int | None,
) -> list[int]:
    sorted_indices = list(np.argsort(axis_scores))
    if first_track_index is None:
        return sorted_indices

    anchor_position = sorted_indices.index(first_track_index)
    left_first = list(reversed(sorted_indices[: anchor_position + 1])) + sorted_indices[
        anchor_position + 1 :
    ]
    right_first = sorted_indices[anchor_position:] + list(
        reversed(sorted_indices[:anchor_position])
    )

    return left_first if len(left_first) <= 1 else min(
        [left_first, right_first],
        key=lambda order: (
            mean_adjacent_distance(order, reduced_embeddings),
            order.index(first_track_index),
        ),
    )


def write_embeddings_json(
    output_path: Path,
    tracks: list[TrackInfo],
    embeddings: np.ndarray,
    reduced: np.ndarray,
    axis_scores: np.ndarray,
    metrics_list: list[dict[str, float]],
    proposed_order: list[int],
) -> None:
    proposal_rank = {track_index: rank + 1 for rank, track_index in enumerate(proposed_order)}
    payload = []
    for index, track in enumerate(tracks):
        item = {
            "id": track.id,
            "title": track.title,
            "artist": track.artist,
            "path": str(track.path),
            "current_rank": track.current_rank,
            "proposed_rank": proposal_rank[index],
            "axis_score": float(axis_scores[index]),
            "metrics": metrics_list[index],
            "embedding": embeddings[index].round(8).tolist(),
            "reduced_embedding": reduced[index].round(8).tolist(),
        }
        payload.append(item)
    output_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def write_projection_csv(
    output_path: Path,
    tracks: list[TrackInfo],
    axis_scores: np.ndarray,
    metrics_list: list[dict[str, float]],
    proposed_order: list[int],
) -> None:
    proposal_rank = {track_index: rank + 1 for rank, track_index in enumerate(proposed_order)}
    with output_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "proposed_rank",
                "current_rank",
                "axis_score",
                "id",
                "title",
                "artist",
                "duration_sec",
                "tempo_bpm",
                "rms_mean",
                "centroid_mean",
                "rolloff_mean",
                "flatness_mean",
            ]
        )
        for index in proposed_order:
            track = tracks[index]
            metrics = metrics_list[index]
            writer.writerow(
                [
                    proposal_rank[index],
                    track.current_rank or "",
                    f"{axis_scores[index]:.6f}",
                    track.id,
                    track.title,
                    track.artist,
                    f"{metrics['duration_sec']:.2f}",
                    f"{metrics['tempo_bpm']:.2f}",
                    f"{metrics['rms_mean']:.6f}",
                    f"{metrics['centroid_mean']:.2f}",
                    f"{metrics['rolloff_mean']:.2f}",
                    f"{metrics['flatness_mean']:.6f}",
                ]
            )


def write_playlist_snippet(output_path: Path, tracks: list[TrackInfo], proposed_order: list[int]) -> None:
    lines = ["local playlist = {"]
    for index in proposed_order:
        lines.append(f'\t"{tracks[index].id}",')
    lines.extend(["}", ""])
    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_report(
    output_path: Path,
    tracks: list[TrackInfo],
    proposed_order: list[int],
    reduced_embeddings: np.ndarray,
    current_playlist_order: list[int],
    axis_scores: np.ndarray,
    forced_first_track: str | None,
) -> None:
    proposed_distance = mean_adjacent_distance(proposed_order, reduced_embeddings)
    current_distance = (
        mean_adjacent_distance(current_playlist_order, reduced_embeddings)
        if len(current_playlist_order) >= 2
        else math.nan
    )

    lines = [
        "Harmonious Song Sequence Proposal",
        "",
        "Method",
        "- Embedding built from global, intro and outro audio descriptors.",
        "- Features include MFCC, chroma, tonnetz, spectral contrast and low-level energy/brightness metrics.",
        "- Tracks are projected on one axis with a spectral-seriation approach using the Fiedler vector of the similarity graph.",
        "- Axis direction is oriented from lower to higher perceived energy.",
    ]
    if forced_first_track:
        lines.append(f"- First track constrained to: {forced_first_track}")
    lines.extend(["", "Proposed order"])

    for rank, index in enumerate(proposed_order, start=1):
        track = tracks[index]
        lines.append(
            f"{rank:02d}. {track.id} | {track.title} | {track.artist} | axis={axis_scores[index]:.4f}"
        )

    lines.extend(["", "Transition score"])
    lines.append(f"- Proposed average adjacent distance: {proposed_distance:.4f}")
    if not math.isnan(current_distance):
        lines.append(f"- Current manifest average adjacent distance: {current_distance:.4f}")
        delta = current_distance - proposed_distance
        lines.append(f"- Improvement vs current order: {delta:.4f}")

    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = build_parser().parse_args()
    audio_root = args.input.resolve()
    output_root = args.output.resolve()

    if args.sr <= 0:
        raise SystemExit("--sr must be a positive integer")
    if args.segment_seconds <= 0:
        raise SystemExit("--segment-seconds must be positive")
    if not audio_root.is_dir():
        raise SystemExit(f"Input folder does not exist or is not a directory: {audio_root}")

    output_root.mkdir(parents=True, exist_ok=True)

    outputs = [
        output_root / "audio_embeddings.json",
        output_root / "embedding_axis.csv",
        output_root / "playlist_proposal.lua",
        output_root / "playlist_report.txt",
    ]
    if not args.overwrite:
        existing = [path for path in outputs if path.exists()]
        if existing:
            raise SystemExit(
                "Output files already exist. Re-run with --overwrite to regenerate:\n"
                + "\n".join(str(path) for path in existing)
            )

    manifest = load_manifest()
    tracks = collect_tracks(audio_root, manifest)
    if not tracks:
        print(f"No supported audio files found in {audio_root}")
        return 0

    embeddings = []
    metrics_list = []

    for track in tracks:
        print(f"embed {track.path.name}")
        embedding, metrics = extract_track_embedding(track.path, args.sr, args.segment_seconds)
        embeddings.append(embedding)
        metrics_list.append(metrics)

    embedding_matrix = np.vstack(embeddings)
    scaled_embeddings = StandardScaler().fit_transform(embedding_matrix)

    max_components = min(8, scaled_embeddings.shape[0] - 1, scaled_embeddings.shape[1])
    if max_components <= 0:
        reduced_embeddings = scaled_embeddings
    else:
        reduced_embeddings = PCA(n_components=max_components, random_state=0).fit_transform(
            scaled_embeddings
        )

    energy_reference = np.array(
        [
            metrics["tempo_bpm"] * 0.20
            + metrics["rms_mean"] * 1000.0 * 0.55
            + metrics["centroid_mean"] / 10000.0 * 0.25
            for metrics in metrics_list
        ],
        dtype=np.float64,
    )
    energy_reference = StandardScaler().fit_transform(energy_reference.reshape(-1, 1)).ravel()

    axis_scores = compute_similarity_axis(reduced_embeddings, energy_reference)
    track_index_by_id = {track.id: index for index, track in enumerate(tracks)}
    first_track_index = None
    if args.first_track:
        if args.first_track not in track_index_by_id:
            available = ", ".join(track.id for track in tracks)
            raise SystemExit(
                f"Unknown --first-track '{args.first_track}'. Available ids: {available}"
            )
        first_track_index = track_index_by_id[args.first_track]
    proposed_order = build_axis_order(axis_scores, reduced_embeddings, first_track_index)

    current_playlist_order = [
        track_index_by_id[track_id]
        for track_id in manifest.get("playlist", [])
        if track_id in track_index_by_id
    ]

    write_embeddings_json(
        output_root / "audio_embeddings.json",
        tracks,
        embedding_matrix,
        reduced_embeddings,
        axis_scores,
        metrics_list,
        proposed_order,
    )
    write_projection_csv(
        output_root / "embedding_axis.csv",
        tracks,
        axis_scores,
        metrics_list,
        proposed_order,
    )
    write_playlist_snippet(output_root / "playlist_proposal.lua", tracks, proposed_order)
    write_report(
        output_root / "playlist_report.txt",
        tracks,
        proposed_order,
        reduced_embeddings,
        current_playlist_order,
        axis_scores,
        args.first_track,
    )

    print("")
    print("Suggested order:")
    for rank, index in enumerate(proposed_order, start=1):
        track = tracks[index]
        print(f"{rank:02d}. {track.id} ({track.title} / {track.artist})")

    print("")
    print(f"Wrote: {output_root / 'audio_embeddings.json'}")
    print(f"Wrote: {output_root / 'embedding_axis.csv'}")
    print(f"Wrote: {output_root / 'playlist_proposal.lua'}")
    print(f"Wrote: {output_root / 'playlist_report.txt'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
