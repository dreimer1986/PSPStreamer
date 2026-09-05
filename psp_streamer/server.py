"""A deliberately small, dependency-free HTTP server for PSP transcoding."""

from __future__ import annotations

import base64
from collections import OrderedDict
import hashlib
import json
import mimetypes
import os
import signal
import subprocess
import tempfile
import threading
import re
import unicodedata
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from .pgs import PgsCue, parse_pgs

VIDEO_EXTENSIONS = {".avi", ".m4v", ".mkv", ".mov", ".mp4", ".mpeg", ".mpg", ".ts", ".webm", ".wmv"}
AUDIO_EXTENSIONS = {".aac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wma"}
MEDIA_EXTENSIONS = VIDEO_EXTENSIONS | AUDIO_EXTENSIONS
TEXT_SUBTITLE_CODECS = {"ass", "mov_text", "srt", "ssa", "subrip", "text", "webvtt"}
BITMAP_SUBTITLE_CODECS = {"dvb_subtitle", "dvd_subtitle", "hdmv_pgs_subtitle", "xsub"}
PSP_SUBTITLE_FPS = 20.1
MAX_SUBTITLE_CUES = 1800
PGS_CACHE_TRACKS = max(1, int(os.environ.get("PGS_CACHE_TRACKS", "1")))


def track_label(value: object) -> str:
    """Keep optional MKV track titles safe for the PSP's tiny JSON parser."""
    text = str(value or "").replace('"', "'").replace("\\", "/")
    text = " ".join(text.split())
    return text[:40]


def natural_name_key(value: str) -> list[object]:
    """Sort episode names naturally: E02 precedes E10."""
    return [int(part) if part.isdigit() else part.casefold() for part in re.split(r"(\d+)", value)]


def psp_subtitle_text(value: str) -> str:
    """Make an intentionally small, safe payload for the PSP bitmap font.

    The native client has no general JSON or UTF-8 renderer on its real-time
    playback path.  Keep cue text printable and transport line breaks as a
    pipe; the client turns those into separately centred lines.
    """
    value = re.sub(r"<[^>]*>", "", value)
    value = re.sub(r"\{[^}]*\}", "", value)  # ASS styling tags
    value = value.replace("\\N", "|").replace("\\n", "|").replace("\n", "|")
    # Preserve Latin-1 directly (notably German umlauts).  Characters beyond
    # the compact client atlas degrade to an ASCII approximation instead of
    # making the JSON cue parser carry an arbitrary Unicode font stack.
    value = "".join(
        character if ord(character) <= 255 else unicodedata.normalize("NFKD", character).encode("ascii", "ignore").decode("ascii")
        for character in value
    )
    value = value.replace('"', "'").replace("\\", "/")
    value = re.sub(r"[^ -ÿ|]", " ", value)
    value = re.sub(r"[ \t]+", " ", value).strip(" |")
    return value[:180]


def parse_srt_cues(value: str, fps: float = PSP_SUBTITLE_FPS) -> list[list[object]]:
    """Convert FFmpeg's canonical SRT output into PSP presentation frames."""
    cues: list[list[object]] = []
    blocks = re.split(r"\r?\n\r?\n+", value.strip())
    timing = re.compile(
        r"(\d+):(\d+):(\d+)[,.](\d+)\s*-->\s*"
        r"(\d+):(\d+):(\d+)[,.](\d+)"
    )
    for block in blocks:
        lines = block.splitlines()
        line_index = next((index for index, line in enumerate(lines) if "-->" in line), -1)
        if line_index < 0:
            continue
        match = timing.search(lines[line_index])
        if not match:
            continue
        parts = [int(part) for part in match.groups()]
        start = ((parts[0] * 60 + parts[1]) * 60 + parts[2]) * 1000 + parts[3]
        end = ((parts[4] * 60 + parts[5]) * 60 + parts[6]) * 1000 + parts[7]
        text = psp_subtitle_text("|".join(lines[line_index + 1:]))
        if text and end > start:
            cues.append([round(start * fps / 1000), round(end * fps / 1000), text])
        if len(cues) >= MAX_SUBTITLE_CUES:
            break
    return cues


def load_roots(value: str | None) -> list[Path]:
    """Load and validate the colon-separated paths from MEDIA_ROOTS."""
    paths = [Path(part).expanduser().resolve() for part in (value or "/media").split(":") if part]
    existing = [path for path in paths if path.is_dir()]
    if not existing:
        raise ValueError("No readable media root found. Set MEDIA_ROOTS to an existing directory.")
    return existing


@dataclass(frozen=True)
class MediaItem:
    root: int
    relative: str


class Library:
    def __init__(self, roots: list[Path]):
        self.roots = roots

    def encode(self, item: MediaItem) -> str:
        raw = json.dumps({"r": item.root, "p": item.relative}, separators=(",", ":")).encode()
        return base64.urlsafe_b64encode(raw).decode().rstrip("=")

    def decode(self, token: str) -> tuple[MediaItem, Path]:
        try:
            raw = base64.urlsafe_b64decode(token + "=" * (-len(token) % 4))
            data = json.loads(raw)
            item = MediaItem(root=int(data["r"]), relative=str(data["p"]))
            root = self.roots[item.root]
            target = (root / item.relative).resolve()
        except (ValueError, KeyError, IndexError, json.JSONDecodeError) as exc:
            raise ValueError("Invalid media identifier") from exc
        if root not in target.parents or not target.is_file() or target.suffix.lower() not in MEDIA_EXTENSIONS:
            raise ValueError("Media item is unavailable")
        return item, target

    def browse(self, root_index: int, relative: str = "") -> dict:
        if root_index < 0 or root_index >= len(self.roots):
            raise ValueError("Unknown media root")
        root = self.roots[root_index]
        directory = (root / relative).resolve()
        if directory != root and root not in directory.parents:
            raise ValueError("Path escapes media root")
        if not directory.is_dir():
            raise ValueError("Folder is unavailable")

        folders, videos = [], []
        for entry in sorted(directory.iterdir(), key=lambda path: (not path.is_dir(), natural_name_key(path.name))):
            if entry.name.startswith("."):
                continue
            child_relative = entry.relative_to(root).as_posix()
            if entry.is_dir():
                folders.append({"name": entry.name, "path": child_relative})
            elif entry.is_file() and entry.suffix.lower() in MEDIA_EXTENSIONS:
                item = MediaItem(root_index, child_relative)
                videos.append({"name": entry.name, "id": self.encode(item), "bytes": entry.stat().st_size,
                               "kind": "audio" if entry.suffix.lower() in AUDIO_EXTENSIONS else "video"})
        parent = None if directory == root else directory.parent.relative_to(root).as_posix()
        return {"root": root_index, "path": relative, "parent": parent, "folders": folders, "videos": videos}


def ffmpeg_command(source: Path, audio_track: int, container: str = "mp4", low_bandwidth: bool = False,
                   subtitle_track: int = -1, audio_bitrate: str = "160k", subtitle_source: Path | None = None,
                   start_seconds: float = 0, bitmap_subtitle: bool = False,
                   tv_output: bool = False) -> list[str]:
    """Conservative AVC/AAC profile for a PSP-3000 over an 802.11b LAN."""
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(source),
        "-map", "0:v:0", "-map", f"0:a:{audio_track}?",
        "-vf", "scale=480:272:force_original_aspect_ratio=decrease,pad=480:272:(ow-iw)/2:(oh-ih)/2,format=yuv420p",
        "-r", "30", "-c:v", "libx264", "-profile:v", "baseline", "-level:v", "3.0",
        "-preset", os.environ.get("FFMPEG_PRESET", "veryfast"),
        "-tune", "zerolatency", "-x264-params", "keyint=60:min-keyint=60:scenecut=0",
        "-b:v", "700k", "-maxrate", "900k", "-bufsize", "1800k",
        "-c:a", "aac", "-ac", "2", "-ar", "48000", "-b:a", "96k",
    ]
    if container == "mjpeg":
        # The first native PSP playback path uses the firmware JPEG decoder.
        # A concatenated MJPEG stream is deliberately simple to parse over a
        # raw HTTP socket: every JPEG is framed by SOI (FFD8) and EOI (FFD9).
        # Six fps at this quantizer stays around 150--250 KB/s for typical
        # animation, which is realistic even through an internet hotspot.
        return [
            # -re is essential here: without it ffmpeg runs faster than real
            # time and would fill the PSP socket/RAM faster than playback.
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-re", "-i", str(source),
            "-map", "0:v:0",
            "-vf", "scale=480:272:force_original_aspect_ratio=decrease,pad=480:272:(ow-iw)/2:(oh-ih)/2,format=yuvj420p",
            "-r", "6", "-c:v", "mjpeg", "-q:v", "24", "-an",
            "-f", "mjpeg", "pipe:1",
        ]
    if container == "h264":
        # Annex-B is the native input expected by the PSP OpenH264 playback
        # path. AUD makes access-unit boundaries unambiguous on a raw socket;
        # repeated headers let a client recover at each IDR.
        # LCD playback retains its validated 20.1-fps cadence.  Native
        # component output alone uses the 20.2-fps calibration.
        target_width, target_height = (720, 480) if tv_output else (480, 272)
        frame_rate = "101/5" if tv_output else "201/10"
        video_filter = (f"fps={frame_rate},scale={target_width}:{target_height}:force_original_aspect_ratio=decrease,"
                        f"pad={target_width}:{target_height}:(ow-iw)/2:(oh-ih)/2,format=yuv420p")
        bitmap_filter = None
        if subtitle_track >= 0 and bitmap_subtitle:
            bitmap_filter = f"[0:v:0][0:s:{subtitle_track}]overlay,{video_filter}[v]"
        elif subtitle_track >= 0:
            # ffmpeg's subtitles filter burns the chosen embedded subtitle
            # into the small PSP frame, avoiding any client-side renderer.
            # libavfilter has a separate quoting language.  A source file
            # with an apostrophe cannot be represented reliably in its
            # filename option, so transcode() supplies a safe symlink name.
            subtitle_path = subtitle_source or source
            escaped = str(subtitle_path).replace("\\", "\\\\").replace(":", "\\:").replace("'", "\\'")
            # Restrict libass to a small known font directory.  Letting it
            # crawl the complete host font catalogue made PSP startup appear
            # frozen for tens of seconds.
            fonts_dir = os.environ.get("FFMPEG_FONTS_DIR", "/usr/share/fonts/truetype/dejavu")
            video_filter = f"subtitles='{escaped}':si={subtitle_track}:fontsdir='{fonts_dir}',{video_filter}"
        command = [
            # Deliver the opening seconds at disk speed.  This hides libass
            # setup and creates a small TCP runway; after two seconds -re
            # resumes the normal real-time rate.
            "ffmpeg", "-hide_banner", "-loglevel", "error", *( ["-ss", f"{start_seconds:.3f}"] if start_seconds else [] ), "-re", "-readrate_initial_burst", "2", "-i", str(source),
            "-map", "[v]" if bitmap_filter else "0:v:0",
            # The selected profile's calibrated frame rate is the real-time ceiling
            # for software H.264 decoding plus YUV-to-RGBA conversion on a
            # PSP-3000.  It prevents video falling behind the audio clock.
            "-c:v", "libx264", "-profile:v", "baseline", "-level:v", "3.0",
            "-preset", os.environ.get("FFMPEG_PRESET", "veryfast"),
            "-tune", "zerolatency",
            "-b:v", "400k" if low_bandwidth else ("850k" if tv_output else "600k"),
            "-maxrate", "450k" if low_bandwidth else ("950k" if tv_output else "700k"),
            "-bufsize", "600k" if low_bandwidth else ("1200k" if tv_output else "900k"),
            # Each repeated SPS/PPS marks a safe firmware-AVC reset point.
            # 3.2 seconds is below the ME deadlock window but makes the reset
            # much less noticeable than the earlier 2.4-second GOP.
            "-x264-params", "aud=1:repeat-headers=1:keyint=64:min-keyint=64:scenecut=0:bframes=0",
            "-an", "-f", "h264", "pipe:1",
        ]
        if bitmap_filter:
            command[command.index("-map"):command.index("-map")] = ["-filter_complex", bitmap_filter]
        else:
            command[command.index("-c:v"):command.index("-c:v")] = ["-vf", video_filter]
        return command
    if container == "mp3":
        # MP3 keeps the audio stream below 8 KB/s at the wire while the PSP's
        # dedicated decoder turns it into 44.1-kHz PCM locally.  Suppressing
        # Xing/ID3 data means the HTTP body begins with an MPEG audio frame.
        return [
            # The PSP DAC and TCP back-pressure clock this stream.  Letting
            # ffmpeg prime it immediately prevents an audio/video start skew.
            "ffmpeg", "-hide_banner", "-loglevel", "error", *( ["-ss", f"{start_seconds:.3f}"] if start_seconds else [] ), "-i", str(source),
            "-map", f"0:a:{audio_track}?", "-vn", "-ac", "2",
            # 160 kbit/s is still only 20 KiB/s and removes the remaining
            # metallic artefacts from music.  Do not add gain here: TV/anime music already reaches
            # full scale and extra gain produces audible clipping on PSP.
            "-ar", "44100",
            "-c:a", "libmp3lame", "-b:a", audio_bitrate,
            "-write_xing", "0", "-id3v2_version", "0", "-f", "mp3", "pipe:1",
        ]
    if container == "mpegts":
        # The PSP client consumes TS progressively; no end-of-file MP4 index needed.
        command.extend(["-mpegts_flags", "+resend_headers", "-f", "mpegts", "pipe:1"])
    else:
        # Fragmented MP4 can be emitted on a pipe immediately; ordinary +faststart cannot.
        command.extend(["-movflags", "+frag_keyframe+empty_moov+default_base_moof", "-f", "mp4", "pipe:1"])
    return command


class AppHandler(BaseHTTPRequestHandler):
    server: "AppServer"

    def log_message(self, fmt: str, *args: object) -> None:
        if os.environ.get("ACCESS_LOG") == "1":
            super().log_message(fmt, *args)

    def send_json(self, body: object, status: HTTPStatus = HTTPStatus.OK) -> None:
        # Compact output is friendlier to the PSP's small response buffer and parser.
        data = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_error_json(self, status: HTTPStatus, message: str) -> None:
        self.send_json({"error": message}, status)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        try:
            if parsed.path == "/api/health":
                return self.send_json({"ok": True, "roots": len(self.server.library.roots)})
            if parsed.path == "/api/remote/next":
                after = max(0, int(query.get("after", ["0"])[0]))
                return self.send_json(self.server.remote_after(after))
            if parsed.path == "/api/library":
                root = int(query.get("root", ["0"])[0])
                return self.send_json(self.server.library.browse(root, query.get("path", [""])[0]))
            if parsed.path.startswith("/api/metadata/"):
                return self.metadata(parsed.path.rsplit("/", 1)[-1])
            if parsed.path.startswith("/api/subtitles/"):
                track = int(query.get("track", ["-1"])[0])
                if not 0 <= track <= 31:
                    raise ValueError("Unsupported subtitle track")
                return self.subtitles(parsed.path.rsplit("/", 1)[-1], track, query.get("tv", ["0"])[0] == "1")
            if parsed.path.startswith("/api/bitmap-subtitles/"):
                track = int(query.get("track", ["-1"])[0])
                if not 0 <= track <= 31:
                    raise ValueError("Unsupported subtitle track")
                return self.bitmap_subtitles(parsed.path.rsplit("/", 1)[-1], track, query.get("tv", ["0"])[0] == "1")
            if parsed.path.startswith("/api/bitmap-sprite/"):
                track = int(query.get("track", ["-1"])[0])
                cue = int(query.get("cue", ["-1"])[0])
                if not 0 <= track <= 31 or cue < 0:
                    raise ValueError("Unsupported bitmap subtitle")
                return self.bitmap_sprite(parsed.path.rsplit("/", 1)[-1], track, cue)
            if parsed.path.startswith("/api/transcode/"):
                audio = max(0, int(query.get("audio", ["0"])[0]))
                subtitle = int(query.get("subtitle", ["-1"])[0])
                audio_bitrate = query.get("audio_quality", ["160k"])[0]
                start_seconds = float(query.get("start", ["0"])[0])
                container = query.get("container", ["mp4"])[0]
                profile = query.get("profile", ["normal"])[0]
                if container not in {"mp4", "mpegts", "mjpeg", "h264", "mp3"}:
                    raise ValueError("Unsupported stream container")
                if profile not in {"normal", "low", "tv"}:
                    raise ValueError("Unsupported stream profile")
                if subtitle < -1 or subtitle > 31 or audio_bitrate not in {"96k", "128k", "160k"} or not 0 <= start_seconds <= 86400:
                    raise ValueError("Unsupported stream option")
                return self.transcode(parsed.path.rsplit("/", 1)[-1], audio, container, profile == "low", subtitle, audio_bitrate, start_seconds, profile == "tv")
            return self.static_file(parsed.path)
        except ValueError as exc:
            self.send_error_json(HTTPStatus.BAD_REQUEST, str(exc))
        except BrokenPipeError:
            pass
        except Exception as exc:  # do not expose filesystem details to clients
            self.log_error("Unhandled error: %r", exc)
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, "Internal server error")

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        try:
            if parsed.path != "/api/remote/command":
                return self.send_error_json(HTTPStatus.NOT_FOUND, "Not found")
            length = int(self.headers.get("Content-Length", "0"))
            if not 2 <= length <= 8192:
                raise ValueError("Invalid command length")
            command = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(command, dict):
                raise ValueError("Invalid command")
            action = command.get("action")
            if action not in {"play", "pause", "resume", "stop", "seek"}:
                raise ValueError("Unsupported remote action")
            clean: dict[str, object] = {"action": action}
            if action == "play":
                token = command.get("id")
                if not isinstance(token, str) or len(token) > 1024:
                    raise ValueError("Invalid media id")
                _, source = self.server.library.decode(token)  # validates root confinement
                clean["id"] = token
                clean["kind"] = "audio" if source.suffix.lower() in AUDIO_EXTENSIONS else "video"
                clean["audio"] = max(0, min(7, int(command.get("audio", 0))))
                clean["subtitle"] = max(-1, min(31, int(command.get("subtitle", -1))))
            elif action == "seek":
                clean["seconds"] = max(0, min(86400, int(command.get("seconds", 0))))
            return self.send_json(self.server.set_remote_command(clean))
        except (ValueError, json.JSONDecodeError, UnicodeDecodeError) as exc:
            return self.send_error_json(HTTPStatus.BAD_REQUEST, str(exc))
        except BrokenPipeError:
            pass
        except Exception as exc:
            self.log_error("Remote command failed: %r", exc)
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, "Internal server error")

    def static_file(self, path: str) -> None:
        wanted = "/index.html" if path == "/" else path
        base = Path(__file__).parent.parent / "static"
        file_path = (base / wanted.lstrip("/")).resolve()
        if base not in file_path.parents or not file_path.is_file():
            return self.send_error_json(HTTPStatus.NOT_FOUND, "Not found")
        data = file_path.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", mimetypes.guess_type(file_path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def metadata(self, token: str) -> None:
        cached = self.server.metadata_cache.get(token)
        if cached is not None:
            return self.send_json(cached)
        _, source = self.server.library.decode(token)
        result = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration:stream=index,codec_type,codec_name:stream_tags=language,title", "-of", "json", str(source)],
            capture_output=True, text=True, timeout=20, check=False,
        )
        if result.returncode:
            raise ValueError("Could not inspect media file")
        streams = json.loads(result.stdout).get("streams", [])
        audio, subtitles = [], []
        for stream in streams:
            tags = stream.get("tags", {})
            language = str(tags.get("language", "und"))[:15]
            title = track_label(tags.get("title"))
            if stream.get("codec_type") == "audio":
                audio.append({"n": str(len(audio)), "l": language, "t": title})
            elif stream.get("codec_type") == "subtitle":
                subtitles.append({"n": str(len(subtitles)), "l": language, "t": title})
        duration = json.loads(result.stdout).get("format", {}).get("duration", "0")
        payload = {"a": audio, "s": subtitles, "d": str(duration)}
        # ffprobe can take a few seconds when an SMB share or its disk has
        # just spun up.  Track layouts do not change while a PSP session is
        # active, so retain this tiny response for subsequent openings.
        self.server.metadata_cache[token] = payload
        self.send_json(payload)

    def subtitles(self, token: str, track: int, tv_profile: bool = False) -> None:
        """Return a compact cue list without involving the video transcode.

        Text tracks are converted by FFmpeg to its canonical SRT form.  This
        conversion is a short, finite extraction rather than libass/font
        initialisation in the latency-sensitive H.264 process.  Bitmap tracks
        deliberately report their kind now; the PSP client can retain the
        proven burn-in fallback until its sprite overlay transport lands.
        """
        fps = 20.2 if tv_profile else PSP_SUBTITLE_FPS
        cache_key = (token, track, fps)
        with self.server.subtitle_cache_lock:
            cached = self.server.subtitle_cache.get(cache_key)
        if cached is not None:
            return self.send_json(cached)
        _, source = self.server.library.decode(token)
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", f"s:{track}",
             "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1", str(source)],
            capture_output=True, text=True, timeout=20, check=False,
        )
        codec = probe.stdout.strip()
        if codec in BITMAP_SUBTITLE_CODECS:
            payload = {"t": "bitmap", "c": []}
        elif codec not in TEXT_SUBTITLE_CODECS:
            payload = {"t": "unsupported", "c": []}
        else:
            extracted = subprocess.run(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(source),
                 "-map", f"0:s:{track}", "-f", "srt", "pipe:1"],
                capture_output=True, text=True, timeout=180, check=False,
            )
            if extracted.returncode:
                raise ValueError("Could not extract subtitle track")
            payload = {"t": "text", "c": parse_srt_cues(extracted.stdout, fps)}
        with self.server.subtitle_cache_lock:
            self.server.subtitle_cache[cache_key] = payload
        self.send_json(payload)

    def pgs_cues(self, token: str, track: int) -> list[PgsCue]:
        cache_key = (token, track)
        with self.server.pgs_cache_lock:
            cached = self.server.pgs_cache.pop(cache_key, None)
            if cached is not None:
                self.server.pgs_cache[cache_key] = cached
        if cached is not None:
            return cached
        _, source = self.server.library.decode(token)
        tracks = json.loads(subprocess.run(["mkvmerge", "-J", str(source)], capture_output=True, text=True, timeout=30, check=True).stdout)["tracks"]
        subtitle_tracks = [entry for entry in tracks if entry.get("type") == "subtitles"]
        if track >= len(subtitle_tracks) or subtitle_tracks[track].get("codec") != "HDMV PGS":
            return []
        cache_dir = Path(tempfile.gettempdir()) / "psp-streamer-pgs"
        cache_dir.mkdir(mode=0o700, exist_ok=True)
        suffix = hashlib.sha256(f"{source}:{track}:{source.stat().st_mtime_ns}".encode()).hexdigest()
        sup = cache_dir / f"{suffix}.sup"
        if not sup.exists():
            extracted = subprocess.run(["mkvextract", "tracks", str(source), f"{subtitle_tracks[track]['id']}:{sup}"],
                                       capture_output=True, text=True, timeout=600, check=False)
            if extracted.returncode:
                raise ValueError("Could not extract PGS subtitle track")
        parsed = parse_pgs(sup.read_bytes())
        with self.server.pgs_cache_lock:
            self.server.pgs_cache[cache_key] = parsed
            while len(self.server.pgs_cache) > PGS_CACHE_TRACKS:
                self.server.pgs_cache.popitem(last=False)
        return parsed

    def bitmap_subtitles(self, token: str, track: int, tv_profile: bool = False) -> None:
        cues = self.pgs_cues(token, track)
        # Frames share the video presentation clock; positions are scaled by
        # the client from the original PGS canvas into 480x272.
        fps = 20.2 if tv_profile else PSP_SUBTITLE_FPS
        payload = {"t": "pgs", "c": [[round(cue.start * fps), round(cue.end * fps),
                                         cue.x, cue.y, cue.width, cue.height, cue.canvas_width, cue.canvas_height]
                                       for cue in cues]}
        self.send_json(payload)

    def bitmap_sprite(self, token: str, track: int, cue: int) -> None:
        cues = self.pgs_cues(token, track)
        if cue >= len(cues):
            raise ValueError("Bitmap subtitle cue is unavailable")
        selected = cues[cue]
        # Palette-indexed payload is much smaller than RGBA and lets the PSP
        # blend it into the AVC framebuffer without a PNG/zlib dependency.
        data = selected.palette + selected.pixels
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def transcode(self, token: str, audio_track: int, container: str, low_bandwidth: bool = False,
                  subtitle_track: int = -1, audio_bitrate: str = "160k", start_seconds: float = 0,
                  tv_output: bool = False) -> None:
        _, source = self.server.library.decode(token)
        if not self.server.transcode_slots.acquire(blocking=False):
            return self.send_error_json(HTTPStatus.TOO_MANY_REQUESTS, "A transcode is already running")
        process = None
        try:
            # A PSP that crashes or suspends mid-stream can otherwise leave a
            # blocking socket write and monopolise the sole transcode slot.
            # Subtitle font initialisation can delay the first H.264 frame;
            # the PSP intentionally holds audio muted until that point.  Keep
            # its full queue alive instead of timing it out after 12 seconds.
            # A disconnected PSP/hotspot can leave TCP half-open.  Free the
            # two playback slots promptly; font/subtitle preparation happens
            # before the first write and is unaffected by this timeout.
            # Audio deliberately waits while a subtitle-enabled video stream
            # prepares its first frame.  Its queue must survive that wait;
            # video sockets can be reclaimed quickly after a disconnect.
            timeout_default = "180" if container == "mp3" else "5"
            self.connection.settimeout(float(os.environ.get("CLIENT_WRITE_TIMEOUT", timeout_default)))
            subtitle_source = None
            bitmap_subtitle = False
            if container == "h264" and subtitle_track >= 0:
                probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", f"s:{subtitle_track}", "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1", str(source)], capture_output=True, text=True, check=False)
                bitmap_subtitle = probe.stdout.strip() in BITMAP_SUBTITLE_CODECS
                alias_dir = Path(tempfile.gettempdir()) / "psp-streamer-subtitles"
                alias_dir.mkdir(mode=0o700, exist_ok=True)
                subtitle_source = alias_dir / hashlib.sha256(str(source).encode()).hexdigest()
                if not subtitle_source.exists():
                    os.symlink(source, subtitle_source)
            process = subprocess.Popen(
                ffmpeg_command(source, audio_track, container, low_bandwidth, subtitle_track, audio_bitrate, subtitle_source, start_seconds, bitmap_subtitle, tv_output),
                # Use the host's already-populated fontconfig cache.  The
                # earlier private cache avoided a directory scan but made
                # libass rebuild its font database for every transcode on
                # some installations, causing the long subtitle startup.
                # stderr was never consumed.  A libass warning repeated for
                # each subtitle event can fill that pipe and consequently
                # stop ffmpeg's video writer after only a few seconds.
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            )
            self.send_response(HTTPStatus.OK)
            content_type = {"mpegts": "video/mp2t", "mjpeg": "image/jpeg", "h264": "video/h264", "mp3": "audio/mpeg", "mp4": "video/mp4"}[container]
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            assert process.stdout is not None
            # read() waits until its complete request is filled. For low-rate
            # MJPEG that used to batch several frames into a 64-KB burst, so
            # the PSP paused and then caught up. read1() returns what ffmpeg
            # has produced now; small blocks keep frame cadence intact.
            chunk_size = 4 * 1024 if container in {"mjpeg", "h264"} else 64 * 1024
            while chunk := process.stdout.read1(chunk_size):
                self.wfile.write(chunk)
                self.wfile.flush()
            process.wait(timeout=15)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass
        finally:
            if process and process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
            self.server.transcode_slots.release()


class AppServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], library: Library):
        super().__init__(address, AppHandler)
        self.library = library
        self.metadata_cache: dict[str, object] = {}
        self.subtitle_cache: dict[tuple[str, int], object] = {}
        self.subtitle_cache_lock = threading.Lock()
        # PGS tracks contain every decoded bitmap of an episode.  Retaining
        # them indefinitely makes a long browsing session consume host RAM.
        self.pgs_cache: OrderedDict[tuple[str, int], list[PgsCue]] = OrderedDict()
        self.pgs_cache_lock = threading.Lock()
        # Video and the separate low-bandwidth PCM track each own one FFmpeg
        # process while a PSP is playing.
        # One PSP uses two processes.  Four slots let a reconnect create its
        # fresh audio/video pair while the abandoned pair times out.
        self.transcode_slots = threading.BoundedSemaphore(int(os.environ.get("MAX_TRANSCODES", "4")))
        self.remote_lock = threading.Lock()
        self.remote_sequence = 0
        self.remote_command: dict[str, object] = {"seq": 0, "action": "idle"}

    def set_remote_command(self, command: dict[str, object]) -> dict[str, object]:
        with self.remote_lock:
            self.remote_sequence += 1
            self.remote_command = {"seq": self.remote_sequence, **command}
            return dict(self.remote_command)

    def remote_after(self, sequence: int) -> dict[str, object]:
        with self.remote_lock:
            if self.remote_sequence > sequence:
                return dict(self.remote_command)
            return {"seq": self.remote_sequence, "action": "idle"}


def main() -> None:
    roots = load_roots(os.environ.get("MEDIA_ROOTS"))
    host, port = os.environ.get("BIND", "0.0.0.0"), int(os.environ.get("PORT", "8091"))
    server = AppServer((host, port), Library(roots))
    print(f"PSP Streamer ready at http://{host}:{port} (roots: {', '.join(map(str, roots))})")
    server.serve_forever()


if __name__ == "__main__":
    main()
