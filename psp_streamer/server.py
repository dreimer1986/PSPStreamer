"""A deliberately small, dependency-free HTTP server for PSP transcoding."""

from __future__ import annotations

import base64
import hashlib
import json
import mimetypes
import os
import signal
import subprocess
import tempfile
import threading
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

VIDEO_EXTENSIONS = {".avi", ".m4v", ".mkv", ".mov", ".mp4", ".mpeg", ".mpg", ".ts", ".webm", ".wmv"}


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
        if root not in target.parents or not target.is_file() or target.suffix.lower() not in VIDEO_EXTENSIONS:
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
        for entry in sorted(directory.iterdir(), key=lambda path: (not path.is_dir(), path.name.casefold())):
            if entry.name.startswith("."):
                continue
            child_relative = entry.relative_to(root).as_posix()
            if entry.is_dir():
                folders.append({"name": entry.name, "path": child_relative})
            elif entry.is_file() and entry.suffix.lower() in VIDEO_EXTENSIONS:
                item = MediaItem(root_index, child_relative)
                videos.append({"name": entry.name, "id": self.encode(item), "bytes": entry.stat().st_size})
        parent = None if directory == root else directory.parent.relative_to(root).as_posix()
        return {"root": root_index, "path": relative, "parent": parent, "folders": folders, "videos": videos}


def ffmpeg_command(source: Path, audio_track: int, container: str = "mp4", low_bandwidth: bool = False,
                   subtitle_track: int = -1, audio_bitrate: str = "160k", subtitle_source: Path | None = None,
                   start_seconds: float = 0, bitmap_subtitle: bool = False) -> list[str]:
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
        # Test profile: 20.1 fps matches the PSP presentation clock used by
        # the accompanying 20.1-fps client build.
        video_filter = "fps=201/10,scale=480:272:force_original_aspect_ratio=decrease,pad=480:272:(ow-iw)/2:(oh-ih)/2,format=yuv420p"
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
            # With audio active, 20 fps is the reliable real-time ceiling
            # for software H.264 decoding plus YUV-to-RGBA conversion on a
            # PSP-3000.  It prevents video falling behind the audio clock.
            "-c:v", "libx264", "-profile:v", "baseline", "-level:v", "3.0",
            "-preset", os.environ.get("FFMPEG_PRESET", "veryfast"),
            "-tune", "zerolatency",
            "-b:v", "400k" if low_bandwidth else "600k",
            "-maxrate", "450k" if low_bandwidth else "700k",
            "-bufsize", "600k" if low_bandwidth else "900k",
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
            if parsed.path == "/api/library":
                root = int(query.get("root", ["0"])[0])
                return self.send_json(self.server.library.browse(root, query.get("path", [""])[0]))
            if parsed.path.startswith("/api/metadata/"):
                return self.metadata(parsed.path.rsplit("/", 1)[-1])
            if parsed.path.startswith("/api/transcode/"):
                audio = max(0, int(query.get("audio", ["0"])[0]))
                subtitle = int(query.get("subtitle", ["-1"])[0])
                audio_bitrate = query.get("audio_quality", ["160k"])[0]
                start_seconds = float(query.get("start", ["0"])[0])
                container = query.get("container", ["mp4"])[0]
                profile = query.get("profile", ["normal"])[0]
                if container not in {"mp4", "mpegts", "mjpeg", "h264", "mp3"}:
                    raise ValueError("Unsupported stream container")
                if profile not in {"normal", "low"}:
                    raise ValueError("Unsupported stream profile")
                if subtitle < -1 or subtitle > 31 or audio_bitrate not in {"96k", "128k", "160k"} or not 0 <= start_seconds <= 86400:
                    raise ValueError("Unsupported stream option")
                return self.transcode(parsed.path.rsplit("/", 1)[-1], audio, container, profile == "low", subtitle, audio_bitrate, start_seconds)
            return self.static_file(parsed.path)
        except ValueError as exc:
            self.send_error_json(HTTPStatus.BAD_REQUEST, str(exc))
        except BrokenPipeError:
            pass
        except Exception as exc:  # do not expose filesystem details to clients
            self.log_error("Unhandled error: %r", exc)
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
            language = stream.get("tags", {}).get("language", "und")
            if stream.get("codec_type") == "audio":
                audio.append({"n": str(len(audio)), "l": language})
            elif stream.get("codec_type") == "subtitle":
                subtitles.append({"n": str(len(subtitles)), "l": language})
        duration = json.loads(result.stdout).get("format", {}).get("duration", "0")
        payload = {"a": audio, "s": subtitles, "d": str(duration)}
        # ffprobe can take a few seconds when an SMB share or its disk has
        # just spun up.  Track layouts do not change while a PSP session is
        # active, so retain this tiny response for subsequent openings.
        self.server.metadata_cache[token] = payload
        self.send_json(payload)

    def transcode(self, token: str, audio_track: int, container: str, low_bandwidth: bool = False,
                  subtitle_track: int = -1, audio_bitrate: str = "160k", start_seconds: float = 0) -> None:
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
                bitmap_subtitle = probe.stdout.strip() in {"hdmv_pgs_subtitle", "dvd_subtitle", "dvb_subtitle"}
                alias_dir = Path(tempfile.gettempdir()) / "psp-streamer-subtitles"
                alias_dir.mkdir(mode=0o700, exist_ok=True)
                subtitle_source = alias_dir / hashlib.sha256(str(source).encode()).hexdigest()
                if not subtitle_source.exists():
                    os.symlink(source, subtitle_source)
            process = subprocess.Popen(
                ffmpeg_command(source, audio_track, container, low_bandwidth, subtitle_track, audio_bitrate, subtitle_source, start_seconds, bitmap_subtitle),
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
        # Video and the separate low-bandwidth PCM track each own one FFmpeg
        # process while a PSP is playing.
        # One PSP uses two processes.  Four slots let a reconnect create its
        # fresh audio/video pair while the abandoned pair times out.
        self.transcode_slots = threading.BoundedSemaphore(int(os.environ.get("MAX_TRANSCODES", "4")))


def main() -> None:
    roots = load_roots(os.environ.get("MEDIA_ROOTS"))
    host, port = os.environ.get("BIND", "0.0.0.0"), int(os.environ.get("PORT", "8091"))
    server = AppServer((host, port), Library(roots))
    print(f"PSP Streamer ready at http://{host}:{port} (roots: {', '.join(map(str, roots))})")
    server.serve_forever()


if __name__ == "__main__":
    main()
