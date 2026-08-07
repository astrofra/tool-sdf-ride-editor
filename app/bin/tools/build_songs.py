import argparse
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:
    raise SystemExit(
        "Pillow is required to build songs. Install it with `python -m pip install Pillow`."
    ) from exc


SCRIPT_PATH = Path(__file__).resolve()
TOOLS_DIR = SCRIPT_PATH.parent
BIN_DIR = TOOLS_DIR.parent
SRC_DIR = BIN_DIR.parent
PROJECT_DIR = SRC_DIR.parent

MANIFEST_PATH = SRC_DIR / "songs_manifest.lua"
OUTPUT_LUA_PATH = SRC_DIR / "songs.lua"
ASSETS_AUDIO_DIR = SRC_DIR / "assets" / "audio"
SOURCE_AUDIO_DIR = PROJECT_DIR / "works" / "audio"
SONGS_TITLES_PATH = SRC_DIR / "assets" / "props" / "walkman_osd" / "songs_titles.png"
FONT_PATH = SRC_DIR / "assets" / "fonts" / "m20.TTF"

TEXTURE_WIDTH = 432
ROW_HEIGHT = 56
TEXT_SIDE_PADDING = 12
FONT_SIZE = 21

RE_SR = re.compile(r"^Sample Rate\s*:\s*(\d+)\b")
RE_SAMPLES = re.compile(r"^Duration\s*:.*=\s*([\d,]+)\s+samples\b")
ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")


def run_cmd(args, timeout=60):
    res = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )
    if res.returncode != 0:
        raise RuntimeError(
            f"Command failed ({res.returncode}): {' '.join(str(arg) for arg in args)}\n{res.stderr.strip()}"
        )
    return res.stdout


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"Missing {label}: {path}")


def find_sox() -> str:
    candidates = [
        BIN_DIR / "sox" / "sox.exe",
        BIN_DIR / "sox" / "sox",
        shutil.which("sox.exe"),
        shutil.which("sox"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    raise FileNotFoundError(
        "Could not find sox. Expected bin\\sox\\sox.exe or a sox executable on PATH."
    )


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


def find_soxi() -> str | None:
    candidates = [
        BIN_DIR / "sox" / "soxi.exe",
        BIN_DIR / "sox" / "soxi",
        shutil.which("soxi.exe"),
        shutil.which("soxi"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    return None


def load_manifest() -> dict:
    lua_cmd = find_lua()
    bootstrap = """
local manifest = dofile(arg[1])

if type(manifest) ~= "table" then
	error("songs_manifest.lua must return a table")
end

if type(manifest.songs) ~= "table" then
	error("songs_manifest.lua must define a `songs` table")
end

if type(manifest.playlist) ~= "table" then
	error("songs_manifest.lua must define a `playlist` array")
end

local function json_escape(value)
	return string.format("%q", value)
end

local parts = {}

table.insert(parts, "{")
table.insert(parts, "\\"songs\\":{")

local first_song = true
for id, song in pairs(manifest.songs) do
	if not first_song then
		table.insert(parts, ",")
	end
	first_song = false
	table.insert(parts, json_escape(id))
	table.insert(parts, ":{")
	table.insert(parts, "\\"title\\":")
	table.insert(parts, json_escape(song.title or ""))
	table.insert(parts, ",\\"artist\\":")
	table.insert(parts, json_escape(song.artist or ""))
	table.insert(parts, "}")
end

table.insert(parts, "},\\"playlist\\":[")

for i, id in ipairs(manifest.playlist) do
	if i > 1 then
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

        output = run_cmd([lua_cmd, str(temp_path), str(MANIFEST_PATH)], timeout=30)
        return json.loads(output)
    except json.JSONDecodeError as exc:
        raise ValueError(f"Could not decode songs manifest JSON bridge output: {exc}") from exc
    finally:
        if temp_path is not None and temp_path.exists():
            temp_path.unlink()


def validate_manifest(manifest):
    if not isinstance(manifest, dict):
        raise ValueError("songs_manifest.lua must return a table with `songs` and `playlist`.")
    if not isinstance(manifest.get("songs"), dict):
        raise ValueError("songs_manifest.lua must define a `songs` table.")
    if not isinstance(manifest.get("playlist"), list) or not manifest["playlist"]:
        raise ValueError("songs_manifest.lua must define a non-empty `playlist` array.")

    validated = []
    playlist_ids = set()
    songs = manifest["songs"]
    playlist = manifest["playlist"]

    for index, entry_id in enumerate(playlist, start=1):
        if not isinstance(entry_id, str) or not entry_id.strip():
            raise ValueError(f"Playlist entry #{index} must be a non-empty string track id.")
        if not ID_RE.fullmatch(entry_id):
            raise ValueError(
                f"Playlist entry `{entry_id}` has an invalid id. Only letters, numbers, `_` and `-` are allowed."
            )
        if entry_id in playlist_ids:
            raise ValueError(f"Duplicate song id in playlist: {entry_id}")
        playlist_ids.add(entry_id)
        if entry_id not in songs:
            raise ValueError(f"Playlist entry `{entry_id}` is missing from the `songs` table.")

        entry = songs[entry_id]
        if not isinstance(entry, dict):
            raise ValueError(f"Song `{entry_id}` must be a table/object.")
        title = entry.get("title")
        artist = entry.get("artist")

        if not isinstance(title, str) or not title.strip():
            raise ValueError(f"Song `{entry_id}` is missing a non-empty string `title`.")
        if not isinstance(artist, str) or not artist.strip():
            raise ValueError(f"Song `{entry_id}` is missing a non-empty string `artist`.")

        wav_path = SOURCE_AUDIO_DIR / f"{entry_id}.wav"
        if not wav_path.is_file():
            raise FileNotFoundError(f"Missing WAV source for `{entry_id}`: {wav_path}")

        validated.append(
            {
                "id": entry_id,
                "title": title.strip(),
                "artist": artist.strip(),
                "label": build_display_label(title, artist),
                "wav_path": wav_path,
                "ogg_path": ASSETS_AUDIO_DIR / f"{entry_id}.ogg",
                "audio_asset": f"audio\\{entry_id}.ogg",
            }
        )

    return validated


def encode_ogg(sox_cmd: str, wav_path: Path, ogg_path: Path) -> None:
    ogg_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        sox_cmd,
        str(wav_path),
        "-t",
        "ogg",
        "-r",
        "44100",
        "-c",
        "2",
        "-C",
        "9",
        str(ogg_path),
    ]
    print("Encoding:", ogg_path.name)
    run_cmd(cmd, timeout=180)


def probe_with_soxi(path: Path, soxi_cmd: str | None):
    if not soxi_cmd:
        return None
    sample_rate = int(run_cmd([soxi_cmd, "-r", str(path)]).strip())
    samples = int(run_cmd([soxi_cmd, "-s", str(path)]).strip().replace(",", ""))
    return sample_rate, samples


def probe_with_sox_i(path: Path, sox_cmd: str):
    output = run_cmd([sox_cmd, "--i", str(path)])
    sample_rate = None
    samples = None
    for line in output.splitlines():
        line = line.strip()
        match = RE_SR.match(line)
        if match:
            sample_rate = int(match.group(1))
            continue
        match = RE_SAMPLES.match(line)
        if match:
            samples = int(match.group(1).replace(",", ""))
    if sample_rate is None or samples is None:
        raise ValueError(f"Could not parse sox --i output for {path}\n{output}")
    return sample_rate, samples


def probe_ogg(path: Path, sox_cmd: str, soxi_cmd: str | None):
    probed = probe_with_soxi(path, soxi_cmd)
    if probed is None:
        probed = probe_with_sox_i(path, sox_cmd)
    sample_rate, samples = probed
    duration_sec = samples / sample_rate
    return duration_sec


def lua_quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def build_display_label(title: str, artist: str) -> str:
    normalized_title = re.sub(r"\s*/\s*", " - ", title.strip())
    normalized_artist = re.sub(r"\s*/\s*", " - ", artist.strip())
    return f"{normalized_title} - {normalized_artist}"


def write_songs_lua(entries) -> None:
    lines = [
        "-- this file is generated, do not edit manually",
        "",
        "local songs = {",
    ]
    for entry in entries:
        lines.extend(
            [
                "\t{",
                f"\t\tid = {lua_quote(entry['id'])},",
                f"\t\ttitle = {lua_quote(entry['title'])},",
                f"\t\tartist = {lua_quote(entry['artist'])},",
                f"\t\tlabel = {lua_quote(entry['label'])},",
                f"\t\taudio_asset = {lua_quote(entry['audio_asset'])},",
                f"\t\tduration_sec = {entry['duration_sec']:.6f},",
                "\t},",
            ]
        )
    lines.extend(["}", "", "return songs", ""])
    OUTPUT_LUA_PATH.write_text("\n".join(lines), encoding="utf-8")
    print("Wrote:", OUTPUT_LUA_PATH)


def render_song_titles(entries) -> None:
    require_file(FONT_PATH, "song titles font")
    image_height = ROW_HEIGHT * len(entries)
    font = ImageFont.truetype(str(FONT_PATH), size=FONT_SIZE)
    image = Image.new("RGB", (TEXTURE_WIDTH, image_height), "black")
    draw = ImageDraw.Draw(image)

    max_width = TEXTURE_WIDTH - (TEXT_SIDE_PADDING * 2)
    for index, entry in enumerate(entries):
        bbox = draw.textbbox((0, 0), entry["label"], font=font)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        if text_width > max_width:
            raise ValueError(
                f"Song label is too long for songs_titles.png at {TEXTURE_WIDTH}px width: {entry['label']}"
            )

        row_top = index * ROW_HEIGHT
        x = ((TEXTURE_WIDTH - text_width) // 2) - bbox[0]
        y = row_top + ((ROW_HEIGHT - text_height) // 2) - bbox[1]
        draw.text((x, y), entry["label"], font=font, fill="white")

    image = image.rotate(90, expand=True)
    image.save(SONGS_TITLES_PATH)
    print("Wrote:", SONGS_TITLES_PATH)


def parse_args():
    parser = argparse.ArgumentParser(description="Encode songs and generate runtime assets.")
    parser.add_argument(
        "--audio-only",
        action="store_true",
        help="Only encode OGG files and validate the manifest.",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    require_file(MANIFEST_PATH, "songs manifest")
    require_file(FONT_PATH, "song titles font")

    manifest = load_manifest()
    entries = validate_manifest(manifest)
    sox_cmd = find_sox()
    soxi_cmd = find_soxi()

    for entry in entries:
        encode_ogg(sox_cmd, entry["wav_path"], entry["ogg_path"])

    if args.audio_only:
        print("Audio-only build complete.")
        return 0

    for entry in entries:
        entry["duration_sec"] = probe_ogg(entry["ogg_path"], sox_cmd, soxi_cmd)

    render_song_titles(entries)
    write_songs_lua(entries)
    print("Song build complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
