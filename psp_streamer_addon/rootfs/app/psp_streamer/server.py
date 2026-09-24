"""A deliberately small, dependency-free HTTP server for PSP transcoding."""

from __future__ import annotations

import base64
from collections import OrderedDict
import hashlib
import hmac
import binascii
import json
import mimetypes
import os
import random
import signal
import ssl
import subprocess
import tempfile
import threading
import time
import re
import select
import unicodedata
import zipfile
from contextlib import ExitStack
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from .pgs import PgsCue, parse_pgs
from .subtitle_pages import display_timeline, subtitle_page
from .track_labels import subtitle_labels
from .probe_cache import probe as cached_probe, file_identity
from .settings import PasswordSettings
from .state_storage import state_directory
from .radio import RadioDirectory, RADIO_PATH, resolve_playlist, radio_command, IcyLogReader, display_text
from .plex import Plex
from .jellyfin import Jellyfin
from .plex_media import RemoteSource
from .web_session import WebSessions
from .stream_pause import StreamPauses, write_stream
from .catalogue import browse as browse_catalogue, folder_media

VIDEO_EXTENSIONS = {".avi", ".m4v", ".mkv", ".mov", ".mp4", ".mpeg", ".mpg", ".ts", ".webm", ".wmv"}
AUDIO_EXTENSIONS = {".aac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wma"}
MEDIA_EXTENSIONS = VIDEO_EXTENSIONS | AUDIO_EXTENSIONS
TEXT_SUBTITLE_CODECS = {"ass", "mov_text", "srt", "ssa", "subrip", "text", "webvtt"}
BITMAP_SUBTITLE_CODECS = {"dvb_subtitle", "dvd_subtitle", "hdmv_pgs_subtitle", "xsub"}
# Wire-format compatibility only for old clients without timebase=ms.
# Current LCD AND TV playback use container PTS and millisecond cues, not FPS.
LEGACY_SUBTITLE_FPS_LCD = 20.1
LEGACY_SUBTITLE_FPS_TV = 20.2
MAX_SUBTITLE_CUES = 1800  # compatibility response for older, unpaged clients
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


def parse_srt_cues(value: str, fps: float = LEGACY_SUBTITLE_FPS_LCD) -> list[list[object]]:
    """Convert SRT timestamps to ticks (1000 = milliseconds; old clients use frames)."""
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
    return cues


def load_roots(value: str | None) -> list[Path]:
    """Load and validate the colon-separated paths from MEDIA_ROOTS."""
    paths = [Path(part).expanduser().resolve() for part in ("/media" if value is None else value).split(":") if part]
    existing = [path for path in paths if path.is_dir()]
    # Plex/radio-only servers need no filesystem mount. A missing root never
    # broadens filesystem access; it simply contributes no files.
    return existing


@dataclass(frozen=True)
class MediaItem:
    root: int
    relative: str


class Library:
    def __init__(self, roots: list[Path]):
        self.roots = roots
        self.plex = None
        self.jellyfin = None
        self.dlna = None

    def encode(self, item: MediaItem) -> str:
        raw = json.dumps({"r": item.root, "p": item.relative}, separators=(",", ":")).encode()
        return base64.urlsafe_b64encode(raw).decode().rstrip("=")

    def decode(self, token: str) -> tuple[MediaItem, Path | RemoteSource]:
        if token.startswith('dlna.') and self.dlna:
            source = self.dlna.source(token)
            return MediaItem(-1,source.name),source
        if token.startswith('jellyfin.') and self.jellyfin:
            source = self.jellyfin.source(token)
            if source.suffix.lower() not in MEDIA_EXTENSIONS:
                raise ValueError('Unsupported Jellyfin original format')
            return MediaItem(-1, source.name), source
        if token.startswith('plex.') and self.plex:
            source = self.plex.source(token)
            if source.suffix.lower() not in MEDIA_EXTENSIONS:
                raise ValueError('Unsupported Plex original format')
            if isinstance(source, RemoteSource):
                return MediaItem(-1, source.name), source
            for index, root in enumerate(self.roots):
                if root in source.parents:
                    return MediaItem(index, source.relative_to(root).as_posix()), source
            raise ValueError('Plex original is outside MEDIA_ROOTS')
        if self.plex and not self.plex.config['files']:
            raise ValueError('Filesystem source is disabled')
        try:
            raw = base64.urlsafe_b64decode(token + "=" * (-len(token) % 4))
            data = json.loads(raw)
            item = MediaItem(root=int(data["r"]), relative=str(data["p"]))
            if not 0 <= item.root < len(self.roots):
                raise ValueError('Invalid media root')
            root = self.roots[item.root]
            target = (root / item.relative).resolve()
        except (ValueError, TypeError, KeyError, IndexError) as exc:
            raise ValueError("Invalid media identifier") from exc
        if root not in target.parents or not target.is_file() or target.suffix.lower() not in MEDIA_EXTENSIONS:
            raise ValueError("Media item is unavailable")
        return item, target

    def next_media(self, token: str, shuffle: bool = False, previous: bool = False) -> dict:
        """Resolve a same-folder successor independently of PSP menu state.

        Video stops at the folder's end. Music may shuffle, excluding the
        current file.
        """
        if token.startswith('dlna.') and self.dlna:
            return self.dlna.next_media(token,shuffle,previous)
        if token.startswith('plex.') and self.plex:
            return self.plex.next_media(token, shuffle, previous)
        if token.startswith('jellyfin.') and self.jellyfin:
            return self.jellyfin.next_media(token, shuffle, previous)
        item, source = self.decode(token)
        root = self.roots[item.root]
        directory = (root / item.relative).parent.resolve()
        if directory != root and root not in directory.parents:
            raise ValueError("Path escapes media root")
        is_audio = source.suffix.lower() in AUDIO_EXTENSIONS
        candidates = []
        for entry in directory.iterdir():
            if entry.name.startswith(".") or not entry.is_file():
                continue
            if entry.suffix.lower() not in MEDIA_EXTENSIONS:
                continue
            if (entry.suffix.lower() in AUDIO_EXTENSIONS) != is_audio:
                continue
            if root not in entry.resolve().parents:
                continue
            candidates.append(entry)
        candidates.sort(key=lambda entry: (natural_name_key(entry.name), entry.name))
        current = next((i for i, entry in enumerate(candidates)
                        if entry.name == Path(item.relative).name), None)
        if current is None:
            return {}
        if previous:
            if current == 0:
                return {}
            following = candidates[current - 1]
        elif is_audio and shuffle:
            choices = candidates[:current] + candidates[current + 1:]
            if not choices:
                return {}
            following = random.choice(choices)
        elif current + 1 < len(candidates):
            following = candidates[current + 1]
        else:
            return {}
        return {"id": self.encode(MediaItem(item.root, following.relative_to(root).as_posix())),
                "kind": "audio" if is_audio else "video"}

    def browse(self, root_index: int, relative: str = "") -> dict:
        if not self.roots and root_index == 0 and not relative:
            return {'root': 0, 'path': '', 'parent': None, 'folders': [], 'videos': []}
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


def ffmpeg_command(source: Path | RemoteSource | str, audio_track: int, container: str = "mp4", low_bandwidth: bool = False,
                   subtitle_track: int = -1, audio_bitrate: str = "160k", subtitle_source: Path | None = None,
                   start_seconds: float = 0, bitmap_subtitle: bool = False,
                   tv_output: bool = False, video_fps: str = "20", external_subtitle: bool = False) -> list[str]:
    """Conservative AVC/AAC profile for a PSP-3000 over an 802.11b LAN."""
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(source),
        "-map", "0:v:0", "-map", f"0:a:{audio_track}?",
        "-vf", "scale=480:272:force_original_aspect_ratio=decrease,pad=480:272:(ow-iw)/2:(oh-ih)/2,format=yuv420p",
        "-r", "30", "-c:v", "libx264", "-profile:v", "main", "-level:v", "3.0",
        "-preset", os.environ.get("FFMPEG_PRESET", "veryfast"),
        "-tune", "zerolatency", "-x264-params", "keyint=60:min-keyint=60:scenecut=0:bframes=0:cabac=1:weightp=0",
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
    if container in {"h264", "flv"}:
        # New clients demux H.264/MP3 from one FLV timeline. The 20 fps filter
        # limits decoder workload; playback is paced by container timestamps.
        # Keep the raw H.264 endpoint's old cadence for older clients only.
        target_width, target_height = (720, 480) if tv_output else (480, 272)
        frame_rate = video_fps if container == "flv" else ("101/5" if tv_output else "201/10")
        video_filter = (f"fps={frame_rate},scale={target_width}:{target_height}:force_original_aspect_ratio=decrease,"
                        f"pad={target_width}:{target_height}:(ow-iw)/2:(oh-ih)/2,format=yuv420p")
        bitmap_filter = None
        if subtitle_track >= 0 and bitmap_subtitle:
            # Subtitle-to-video can end far beyond the last real video frame.
            # Do not let that secondary timeline extend the main picture.
            # shortest=1 would instead cut off episodes with short sub tracks.
            bitmap_filter = f"[0:v:0][{'1:s:0' if external_subtitle else '0:s:'+str(subtitle_track)}]overlay=eof_action=pass:repeatlast=0,{video_filter}[v]"
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
            video_filter = f"subtitles='{escaped}':si={0 if external_subtitle else subtitle_track}:fontsdir='{fonts_dir}',{video_filter}"
        command = [
            # Deliver the opening seconds at disk speed.  This hides libass
            # setup and creates a small TCP runway; after two seconds -re
            # resumes the normal real-time rate.
            "ffmpeg", "-hide_banner", "-loglevel", "error", *( ["-ss", f"{start_seconds:.3f}"] if start_seconds else [] ), "-re", "-readrate_initial_burst", "2", "-i", str(source),
            *([*(["-ss", f"{start_seconds:.3f}"] if start_seconds else []), "-i",str(subtitle_source)] if external_subtitle and bitmap_filter else []),
            "-map", "[v]" if bitmap_filter else "0:v:0",
            # Main/CABAC compatibility test: keep packet PTS in display order
            # with no B-frames, and retain unweighted P prediction.
            "-c:v", "libx264", "-profile:v", "main", "-level:v", "3.0",
            "-preset", os.environ.get("FFMPEG_PRESET", "veryfast"),
            "-tune", "zerolatency",
            "-b:v", "400k" if low_bandwidth else ("850k" if tv_output else "600k"),
            "-maxrate", "450k" if low_bandwidth else ("950k" if tv_output else "700k"),
            "-bufsize", "600k" if low_bandwidth else ("1200k" if tv_output else "900k"),
            # Retain established GOP size and headers. They are not a clock
            # and the client does not reset the decoder at each keyframe.
            "-x264-params", "aud=1:repeat-headers=1:keyint=64:min-keyint=64:scenecut=0:bframes=0:cabac=1:weightp=0",
            * (["-an", "-f", "h264", "pipe:1"] if container == "h264" else
               ["-map", f"0:a:{audio_track}?", "-c:a", "libmp3lame", "-ar", "44100",
                # Normalize delayed audio to the same zero-based PTS. Do not
                # use an infinite apad filter: on a short track it can keep a
                # live FLV process running after video EOF.
                "-af", "aresample=44100:first_pts=0",
                "-ac", "2", *( ["-q:a", audio_bitrate[1:]] if audio_bitrate.startswith("v") else ["-b:a", audio_bitrate] ), "-flvflags", "no_duration_filesize",
                "-f", "flv", "pipe:1"]),
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
            "-c:a", "libmp3lame", *( ["-q:a", audio_bitrate[1:]] if audio_bitrate.startswith("v") else ["-b:a", audio_bitrate] ),
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

    def authorized(self) -> bool:
        self.web_csrf = self.server.web_sessions.get(self.headers.get('Cookie'))
        if self.web_csrf:
            return True
        if not self.server.settings.protected:
            return True
        supplied = b""
        try:
            scheme, token = self.headers.get("Authorization", "").split(" ", 1)
            if scheme.lower() == "basic":
                credentials = base64.b64decode(token, validate=True)
                user, separator, password = credentials.partition(b":")
                if separator and user == b"psp":
                    supplied = password
        except (ValueError, binascii.Error):
            pass
        if self.server.settings.verify(supplied):
            return True
        self.close_connection = True  # Do not interpret an unread POST body as another request.
        if (urlparse(self.path).path in ('/', '/index.html') or
                self.headers.get('Sec-Fetch-Mode') == 'navigate' or
                'text/html' in self.headers.get('Accept', '')):
            self.send_response(HTTPStatus.SEE_OTHER)
            self.send_header('Location', '/login')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return False
        if self.headers.get('X-PSP-Web') == '1' or self.headers.get('Sec-Fetch-Mode'):
            self.send_error_json(HTTPStatus.UNAUTHORIZED, 'Please sign in')
            return False
        self.send_response(HTTPStatus.UNAUTHORIZED)
        self.send_header("WWW-Authenticate", 'Basic realm="PSP Streamer", charset="UTF-8"')
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()
        return False

    def log_message(self, fmt: str, *args: object) -> None:
        if os.environ.get("ACCESS_LOG") == "1":
            super().log_message(fmt, *args)

    def end_headers(self) -> None:
        if getattr(self, 'session_cookie', None) is not None:
            self.send_header('Set-Cookie', self.session_cookie)
            self.session_cookie = None
        if (self.server.settings.protected or urlparse(self.path).path in ('/api/session', '/api/login', '/api/logout')) and not any(
                header.lower().startswith(b'cache-control:') for header in getattr(self, '_headers_buffer', [])):
            self.send_header("Cache-Control", "private, no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header('X-Frame-Options', 'DENY')
        self.send_header('Referrer-Policy', 'same-origin')
        super().end_headers()

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
        public = urlparse(self.path).path
        if public in ('/login', '/login.html', '/login.js', '/web.css', '/i18n.js', '/logo.png'):
            return self.static_file('/login.html' if public == '/login' else public)
        if not self.authorized():
            return
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        try:
            if parsed.path == '/api/psp-artwork':
                selector = query.get('item', [''])[0]
                provider = self.server.dlna if selector.startswith(('dlna.', ':dlna:')) else self.server.plex if selector.startswith(('plex.', ':plex:')) else self.server.jellyfin
                folder = re.fullmatch(r':(plex|jellyfin):m([0-9a-f]+)', selector)
                token = provider.token(folder[2]) if folder else selector
                if provider is self.server.dlna:token=provider.artwork_token(selector)
                try:
                    if query.get('v') == ['2']:
                        known = query.get('known', [''])[0]
                        if known and not re.fullmatch('[0-9a-f]{64}', known):
                            return self.send_error_json(400, 'Invalid artwork identity')
                        data = provider.artwork.psp(token, known)
                    else:
                        data = provider.artwork.psp(token)
                except ValueError:
                    return self.send_error_json(404, 'Artwork unavailable')
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Cache-Control', 'private, no-store')
                self.end_headers()
                self.wfile.write(data)
                return
            if parsed.path.startswith('/api/theme/'):
                token = parsed.path.rsplit('/', 1)[-1]
                provider = self.server.plex if token.startswith('plex.') else self.server.jellyfin
                try:
                    data, mime = provider.artwork.theme(token)
                except ValueError:
                    return self.send_error_json(404, 'No theme song available')
                self.send_response(200)
                self.send_header('Content-Type', mime)
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Cache-Control', 'private, no-store')
                self.end_headers()
                self.wfile.write(data)
                return
            if parsed.path.startswith('/api/artwork/'):
                parts = parsed.path.split('/')
                if len(parts) != 5:
                    return self.send_error_json(404, 'Artwork unavailable')
                token, kind = parts[3:]
                provider = self.server.dlna if token.startswith('dlna.') else self.server.plex if token.startswith('plex.') else self.server.jellyfin
                try:
                    data, mime = provider.artwork.get(token, kind)
                except ValueError:
                    return self.send_error_json(404, 'Artwork unavailable')
                self.send_response(200)
                self.send_header('Content-Type', mime)
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Cache-Control', 'private, no-store')
                self.end_headers()
                self.wfile.write(data)
                return
            if parsed.path == '/api/session':
                return self.send_json({'csrf': self.web_csrf or '', 'protected': self.server.settings.protected})
            if parsed.path == '/api/library/files':
                return self.send_json({'files': folder_media(self.server,
                    int(query.get('root', ['0'])[0]), query.get('path', [''])[0],
                    query.get('recursive', ['0'])[0] == '1')})
            if parsed.path == '/api/plex':
                return self.send_json(self.server.plex.public())
            if parsed.path == '/api/jellyfin':
                return self.send_json(self.server.jellyfin.public())
            if parsed.path == '/api/plex/servers':
                return self.send_json(self.server.plex.servers())
            if parsed.path.startswith('/api/plex/details/'):
                return self.send_json(self.server.plex.details(parsed.path.rsplit('/', 1)[-1]))
            if parsed.path == '/api/radio':
                return self.send_json(self.server.radio.list())
            if parsed.path == '/api/dlna':
                return self.send_json(self.server.dlna.public())
            if parsed.path.startswith('/api/radio/status/'):
                return self.send_json(self.server.radio.status(parsed.path.rsplit('/', 1)[-1]))
            if parsed.path == '/api/offline/preferences':
                return self.send_json(self.server.offline.preferences())
            if parsed.path == "/api/offline/jobs":
                return self.send_json(self.server.offline.list())
            if parsed.path == "/api/offline/catalog":
                jobs = self.server.offline.list()
                data = ''.join(f"{j['job']}\t{j['state']}\t{j['progress']}\t{j['name']}\n" for j in jobs).encode('utf-8')
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Content-Type', 'text/plain; charset=utf-8')
                self.end_headers()
                self.wfile.write(data)
                return
            if parsed.path.startswith('/api/offline/job/'):
                return self.send_json(self.server.offline.get(parsed.path.rsplit('/', 1)[-1]))
            if parsed.path.startswith('/api/offline/export/'):
                return self.offline_export(parsed.path.rsplit('/', 1)[-1])
            if parsed.path.startswith('/api/offline/file/'):
                key, number = parsed.path.split('/')[-2:]
                return self.offline_file(key, int(number))
            if parsed.path == "/api/settings":
                return self.send_json({"password_editable": self.server.settings.path is not None,
                                       "password_set": self.server.settings.protected})
            if parsed.path == "/api/health":
                return self.send_json({"ok": True, "roots": len(self.server.library.roots)})
            if parsed.path == '/api/player':
                return self.send_json(self.server.player_status.snapshot())
            if parsed.path == "/api/remote/next":
                self.server.player_status.report(query)
                media = query.get('media') or query.get('plex')
                if media and query.get('state', [''])[0] in {'paused', 'playing', 'stopped'}:
                    self.server.stream_pauses.report(self.client_address[0], media[0],
                        query['state'][0] == 'paused')
                    if media[0].startswith('dlna.') and self.server.dlna.bridge:
                        self.server.dlna.bridge.report_pause(media[0],query['state'][0]=='paused')
                    provider = (self.server.jellyfin if media[0].startswith('jellyfin.') else
                                self.server.plex if media[0].startswith('plex.') else None)
                    if provider and provider.media_bridge:
                        try:
                            provider.media_bridge.report_pause(provider.split(media[0])[0], query['state'][0]=='paused')
                        except ValueError:
                            pass
                if query.get('plex'):
                    try:
                        provider = self.server.jellyfin if query['plex'][0].startswith('jellyfin.') else self.server.plex
                        provider.report(query['plex'][0], query.get('state', ['playing'])[0],
                            query.get('position', ['0'])[0], query.get('duration', ['0'])[0])
                    except ValueError:
                        pass  # Reporting must never block or break the remote control.
                after = max(0, int(query.get("after", ["0"])[0]))
                reply = self.server.remote_after(after)
                if query.get('radio'):
                    try:
                        reply.update(self.server.radio.status(query['radio'][0]))
                        self.server.player_status.remember(query['radio'][0],
                            {'name': reply.get('radio_title') or reply.get('radio_station', '')}, 'audio')
                    except ValueError:
                        pass  # A removed sender must not block Stop/Play commands.
                return self.send_json(reply)
            if parsed.path == "/api/library":
                root = int(query.get("root", ["0"])[0])
                path = query.get("path", [""])[0]
                listing = browse_catalogue(self.server, root, path)
                if not self.headers.get('X-PSP-Web') and query.get('artwork', ['0'])[0] != '1':
                    # Keep the PSP's bounded catalogue JSON exactly as small as
                    # before. Web clients opt in; image bytes stay server-side.
                    listing = {**listing, **{key: [{k:v for k,v in row.items() if k != 'artwork'}
                               for row in listing[key]] for key in ('folders','videos')}}
                return self.send_json(listing)
            if parsed.path.startswith("/api/media-next/"):
                if parsed.path.rsplit('/', 1)[-1].startswith('radio.'):
                    self.server.radio.get(parsed.path.rsplit('/', 1)[-1])
                    return self.send_json({})
                return self.send_json(self.server.library.next_media(
                    parsed.path.rsplit("/", 1)[-1], query.get("shuffle", ["0"])[0] == "1",
                    query.get("direction", ["next"])[0] == "previous"))
            if parsed.path.startswith("/api/metadata/"):
                return self.metadata(parsed.path.rsplit("/", 1)[-1])
            if parsed.path.startswith("/api/subtitles/"):
                track = int(query.get("track", ["-1"])[0])
                if not 0 <= track <= 31:
                    raise ValueError("Unsupported subtitle track")
                paged = query.get('page', ['0'])[0] == '1'
                offset = int(query['offset'][0]) if 'offset' in query else None
                at_ms = int(query.get('at_ms', ['0'])[0])
                if (offset is not None and offset < 0) or not 0 <= at_ms <= 2147483647:
                    raise ValueError('Invalid subtitle page position')
                return self.subtitles(parsed.path.rsplit("/", 1)[-1], track, query.get("tv", ["0"])[0] == "1", query.get("timebase", [""])[0] == "ms", paged, offset, at_ms)
            if parsed.path.startswith("/api/bitmap-subtitles/"):
                track = int(query.get("track", ["-1"])[0])
                if not 0 <= track <= 31:
                    raise ValueError("Unsupported subtitle track")
                return self.bitmap_subtitles(parsed.path.rsplit("/", 1)[-1], track, query.get("tv", ["0"])[0] == "1", query.get("timebase", [""])[0] == "ms")
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
                video_fps = query.get("video_fps", ["20"])[0]
                start_seconds = float(query.get("start", ["0"])[0])
                container = query.get("container", ["mp4"])[0]
                profile = query.get("profile", ["normal"])[0]
                if container not in {"mp4", "mpegts", "mjpeg", "h264", "mp3", "flv"}:
                    raise ValueError("Unsupported stream container")
                if profile not in {"normal", "low", "tv"}:
                    raise ValueError("Unsupported stream profile")
                if subtitle < -1 or subtitle > 31 or audio_bitrate not in {"96k", "128k", "160k", "v6", "v5", "v4", "v3"} or video_fps not in {"20", "24000/1001"} or not 0 <= start_seconds <= 86400:
                    raise ValueError("Unsupported stream option")
                return self.transcode(parsed.path.rsplit("/", 1)[-1], audio, container, profile == "low", subtitle, audio_bitrate, start_seconds, profile == "tv", video_fps)
            return self.static_file(parsed.path)
        except ValueError as exc:
            self.send_error_json(HTTPStatus.BAD_REQUEST, str(exc))
        except BrokenPipeError:
            pass
        except Exception as exc:  # do not expose filesystem details to clients
            self.log_error("Unhandled error: %r", exc)
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, "Internal server error")

    def do_POST(self) -> None:  # noqa: N802
        login = urlparse(self.path).path == '/api/login'
        if not login and not self.authorized():
            return
        # Native PSP clients only GET. Browser commands must be same-origin
        # JSON; reject form POSTs that could reuse cached Basic credentials.
        origin = self.headers.get("Origin")
        if (self.headers.get_content_type() != "application/json" or
                (origin is not None and urlparse(origin).netloc != self.headers.get("Host"))):
            self.close_connection = True
            return self.send_error_json(HTTPStatus.FORBIDDEN, "Same-origin JSON required")
        parsed = urlparse(self.path)
        try:
            if login:
                if not self.server.web_sessions.allow_login(self.client_address[0]):
                    self.close_connection = True
                    return self.send_error_json(HTTPStatus.TOO_MANY_REQUESTS, 'Too many attempts; wait one minute')
                length = int(self.headers.get('Content-Length', '0'))
                if not 2 <= length <= 2048:
                    self.close_connection = True
                    raise ValueError('Invalid login request')
                data = json.loads(self.rfile.read(length))
                password = data.get('password') if isinstance(data, dict) else None
                if not isinstance(password, str) or not self.server.settings.verify(password.encode('utf-8')):
                    return self.send_error_json(HTTPStatus.UNAUTHORIZED, 'Incorrect password')
                with self.server.settings.lock:
                    # Recheck under the same lock as password changes.
                    if not self.server.settings.verify(password.encode('utf-8')):
                        return self.send_error_json(HTTPStatus.UNAUTHORIZED, 'Incorrect password')
                    self.server.web_sessions.revoke(self.headers.get('Cookie'))
                    token, _ = self.server.web_sessions.create()
                self.session_cookie = self.cookie_value(token, WebSessions.lifetime)
                return self.send_json({'ok': True})
            if self.web_csrf and not hmac.compare_digest(self.web_csrf.encode(), self.headers.get('X-CSRF-Token', '').encode()):
                self.close_connection = True
                return self.send_error_json(HTTPStatus.FORBIDDEN, 'Invalid session token')
            if parsed.path == '/api/logout':
                self.close_connection = True
                self.server.web_sessions.revoke(self.headers.get('Cookie'))
                self.session_cookie = self.cookie_value('', 0)
                return self.send_json({'ok': True})
            if parsed.path.startswith('/api/dlna/'):
                length=int(self.headers.get('Content-Length','0'))
                if not 2<=length<=8192:
                    self.close_connection=True
                    raise ValueError('Invalid DLNA settings length')
                data=json.loads(self.rfile.read(length))
                if not isinstance(data,dict):raise ValueError('Invalid DLNA settings')
                action=parsed.path.rsplit('/',1)[-1]
                if action=='discover':return self.send_json(self.server.dlna.discover())
                if action=='add':return self.send_json(self.server.dlna.add(data.get('url')))
                if action=='settings':return self.send_json(self.server.dlna.configure(data))
                return self.send_error_json(HTTPStatus.NOT_FOUND,'Not found')
            if parsed.path.startswith('/api/jellyfin/'):
                length = int(self.headers.get('Content-Length', '0'))
                if not 2 <= length <= 8192:
                    self.close_connection = True
                    raise ValueError('Invalid Jellyfin settings length')
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise ValueError('Invalid Jellyfin settings')
                action = parsed.path.rsplit('/', 1)[-1]
                if action == 'login':
                    return self.send_json(self.server.jellyfin.login(data))
                if action == 'settings':
                    return self.send_json(self.server.jellyfin.configure(data))
                if action == 'disconnect':
                    result = self.server.jellyfin.disconnect()
                    if not self.server.dlna.config['enabled'] and not any(self.server.plex.config[k] for k in ('enabled', 'files', 'radio')):
                        self.server.plex.config['files'] = True
                        self.server.plex.save()
                    return self.send_json(result)
                return self.send_error_json(HTTPStatus.NOT_FOUND, 'Not found')
            if parsed.path.startswith('/api/plex/'):
                length = int(self.headers.get('Content-Length', '0'))
                if not 2 <= length <= 8192:
                    self.close_connection = True
                    raise ValueError('Invalid Plex settings length')
                data = json.loads(self.rfile.read(length))
                if not isinstance(data, dict):
                    raise ValueError('Invalid Plex settings')
                action = parsed.path.rsplit('/', 1)[-1]
                if action == 'link':
                    return self.send_json(self.server.plex.link())
                if action == 'poll':
                    return self.send_json(self.server.plex.poll())
                if action == 'select':
                    return self.send_json(self.server.plex.select(data.get('server'), data.get('url')))
                if action == 'settings':
                    return self.send_json(self.server.plex.configure(data))
                if action == 'disconnect':
                    return self.send_json(self.server.plex.disconnect())
                return self.send_error_json(HTTPStatus.NOT_FOUND, 'Not found')
            if parsed.path.startswith('/api/offline/'):
                length = int(self.headers.get('Content-Length', '0'))
                if not 2 <= length <= (131072 if parsed.path == '/api/offline/batch' else 8192):
                    self.close_connection = True
                    raise ValueError('Invalid request length')
                data = json.loads(self.rfile.read(length).decode('utf-8'))
                if not isinstance(data, dict):
                    raise ValueError('Invalid request')
                if parsed.path == '/api/offline/preferences':
                    return self.send_json(self.server.offline.preferences(data))
                if parsed.path == '/api/offline/jobs':
                    return self.send_json(self.server.offline.add(data))
                if parsed.path == '/api/offline/batch':
                    return self.send_json({'jobs': self.server.offline.add_many(data.get('items'))})
                if parsed.path == '/api/offline/cancel':
                    return self.send_json(self.server.offline.cancel(str(data.get('job', ''))))
                if parsed.path == '/api/offline/delete':
                    self.server.offline.delete(str(data.get('job', '')))
                    return self.send_json({'ok': True})
                return self.send_error_json(HTTPStatus.NOT_FOUND, 'Not found')
            if parsed.path == "/api/settings/password":
                length = int(self.headers.get("Content-Length", "0"))
                if not 2 <= length <= 2048:
                    self.close_connection = True
                    raise ValueError("Invalid settings length")
                settings = json.loads(self.rfile.read(length).decode("utf-8"))
                if not isinstance(settings, dict):
                    raise ValueError("Invalid settings")
                with self.server.settings.lock:
                    self.server.settings.change(settings.get("current", ""), settings.get("password"))
                    self.server.web_sessions.clear()
                self.session_cookie = self.cookie_value('', 0)
                return self.send_json({"ok": True})
            if parsed.path != "/api/remote/command":
                if parsed.path == '/api/radio':
                    length = int(self.headers.get('Content-Length', '0'))
                    if not 2 <= length <= 8192:
                        self.close_connection = True
                        raise ValueError('Invalid station settings length')
                    return self.send_json(self.server.radio.change(json.loads(self.rfile.read(length))))
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
                live = token.startswith('radio.')
                source = None if live else self.server.library.decode(token)[1]
                station = self.server.radio.get(token) if live else None
                clean["id"] = token
                clean["kind"] = "audio" if live or source.suffix.lower() in AUDIO_EXTENSIONS else "video"
                clean["audio"] = max(0, min(7, int(command.get("audio", 0))))
                clean["subtitle"] = max(-1, min(31, int(command.get("subtitle", -1))))
                clean["start"] = max(0, min(86400, int(command.get("start", 0))))
                if live:
                    clean.update(live=True, name=display_text(station['name']), audio=0, subtitle=-1, start=0)
                for name, allowed in (("audio_quality", {"96k", "128k", "160k", "v6", "v5", "v4", "v3"}),
                                      ("video_fps", {"20", "24000/1001"})):
                    if name in command:
                        if not isinstance(command[name], str) or command[name] not in allowed:
                            raise ValueError("Invalid " + name)
                        clean[name] = command[name]
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

    def offline_export(self, key: str) -> None:
        # Pin open descriptors under the queue lock, then release it before
        # streaming. A concurrent deletion cannot mix or truncate the bundle.
        with ExitStack() as opened:
            with self.server.offline.lock:
                job = self.server.offline.get(key)
                if job['state'] != 'ready':
                    raise ValueError('Conversion is not ready for export')
                files = []
                for number in range(len(job['files'])):
                    path, entry = self.server.offline.file(key, number)
                    source = opened.enter_context(path.open('rb'))
                    if os.fstat(source.fileno()).st_size != entry['size']:
                        raise ValueError('Converted file size changed; create a new job')
                    files.append((source, entry))
            self.connection.settimeout(30)
            self.close_connection = True
            self.send_response(200)
            self.send_header('Content-Type', 'application/zip')
            self.send_header('Content-Disposition', f'attachment; filename="PSPStreamer-{key}.zip"')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Connection', 'close')
            self.end_headers()
            prefix = f'PSP/VIDEO/PSPStreamer/{key}/'
            try:
                # No second video-sized file on HA storage; no recompression
                # of H.264/MP3. ZIP's data descriptors support a socket sink.
                with zipfile.ZipFile(self.wfile, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as archive:
                    for source, entry in files:
                        digest = hashlib.sha256()
                        with archive.open(prefix + entry['name'], 'w', force_zip64=True) as target:
                            while block := source.read(256 * 1024):
                                digest.update(block)
                                target.write(block)
                        if digest.hexdigest() != entry['sha256']:
                            raise ValueError('Converted file checksum changed')
                    # Disk job.json is pretty-printed. PSP's narrow parser
                    # requires the same compact form as its HTTP manifest.
                    archive.writestr(prefix + 'job.json', json.dumps(job, ensure_ascii=False, separators=(',', ':')).encode('utf-8'))
                    archive.writestr(prefix + 'ready', b'1')
            except (OSError, ValueError) as exc:
                # Headers already went out: never append JSON to a ZIP body.
                # In particular, a failed integrity check must not emit ready.
                self.log_error('Offline export interrupted: %r', exc)

    def offline_file(self, key: str, number: int) -> None:
        path, entry = self.server.offline.file(key, number)
        # Open before headers: a concurrent deletion cannot produce a partial
        # error response; an already-open download can finish on POSIX.
        with path.open('rb') as data:
            size = os.fstat(data.fileno()).st_size
            start = 0
            partial = self.headers.get('Range')
            if partial:
                match = re.fullmatch(r'bytes=(\d+)-', partial)
                if not match or int(match[1]) >= size:
                    self.send_response(416)
                    self.send_header('Content-Range', f'bytes */{size}')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                start = int(match[1])
            self.send_response(206 if partial else 200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(size-start))
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('ETag', '"' + entry['sha256'] + '"')
            if partial:
                self.send_header('Content-Range', f'bytes {start}-{size-1}/{size}')
            self.end_headers()
            data.seek(start)
            self.connection.settimeout(15)
            while block := data.read(65536):
                self.wfile.write(block)

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

    def cookie_value(self, token, age):
        # A same-origin HTTPS Origin also covers TLS-terminating reverse proxies;
        # arbitrary X-Forwarded-* headers are deliberately not trusted.
        secure = bool(self.server.tls_context) or self.headers.get('Origin', '').startswith('https://')
        return f'psp_session={token}; Path=/; HttpOnly; SameSite=Strict; Max-Age={age}' + ('; Secure' if secure else '')

    def metadata(self, token: str) -> None:
        if token.startswith('radio.'):
            station = self.server.radio.get(token)
            return self.send_json({'a': [], 's': [], 'd': '0', 'live': True, 'name': display_text(station['name'])})
        _, source = self.server.library.decode(token)
        identity = file_identity(source)
        cache_key = (token, identity)
        cached = self.server.metadata_cache.get(cache_key) if identity is not None else None
        if cached is not None and not token.startswith(('plex.', 'jellyfin.')):
            self.server.player_status.remember(token, cached, cached.get('kind', 'video'))
            return self.send_json(cached if self.headers.get('X-PSP-Web') else
                                  {k:v for k,v in cached.items() if k not in ('artwork', 'chapters', 'markers')})
        result = cached_probe(source,
            ["-show_entries", "chapter=start_time,end_time:chapter_tags=title:format=duration:format_tags=title,artist,album,album_artist:stream=index,codec_type,codec_name:stream_disposition=forced,hearing_impaired,default:stream_tags=language,title,artist,album,album_artist,NUMBER_OF_BYTES,NUMBER_OF_BYTES-eng,NUMBER_OF_FRAMES,NUMBER_OF_FRAMES-eng", "-of", "json"])
        if result.returncode:
            raise ValueError("Could not inspect media file")
        probe_data = json.loads(result.stdout)
        streams = probe_data.get("streams", [])
        from .external_subtitles import tracks
        streams = streams + tracks(self.server.library,token)
        audio, subtitles = [], subtitle_labels(streams)
        for stream in streams:
            tags = stream.get("tags", {})
            language = str(tags.get("language", "und"))[:15]
            title = track_label(tags.get("title"))
            if stream.get("codec_type") == "audio":
                audio.append({"n": str(len(audio)), "l": language, "t": title})
        duration = probe_data.get("format", {}).get("duration", "0")
        payload = {"a": audio, "s": subtitles, "d": str(duration)}
        from .timeline import chapters
        file_chapters = chapters(probe_data.get('chapters'))
        payload['chapters'] = file_chapters
        payload['kind'] = 'audio' if source.suffix.lower() in AUDIO_EXTENSIONS else 'video'
        payload['name'] = display_text(source.name, 126)
        if source.suffix.lower() in AUDIO_EXTENSIONS:
            tags = {}
            for stream in streams:
                if stream.get('codec_type') == 'audio':
                    tags = {k.lower(): v for k, v in stream.get('tags', {}).items()}
                    break
            tags.update({k.lower(): v for k, v in probe_data.get('format', {}).get('tags', {}).items() if v})
            title = display_text(tags.get('title') or source.name)
            artist = display_text(tags.get('artist') or tags.get('album_artist'))
            payload.update(title=title, artist=artist, album=display_text(tags.get('album')),
                           name=display_text(f'{artist} - {title}' if artist else title, 126))
        # ffprobe can take a few seconds when an SMB share or its disk has
        # just spun up. Reuse local responses only while file identity matches.
        if token.startswith('plex.'):
            payload.update(self.server.plex.details(token))
        elif token.startswith('jellyfin.'):
            payload.update(self.server.jellyfin.details(token))
            if self.headers.get('X-PSP-Web'):
                payload['markers'] = self.server.jellyfin.web_markers(token)
        elif token.startswith('dlna.'):
            row=self.server.dlna.metadata(token)
            payload.update(name=row['name'],artwork=self.server.dlna.artwork.links(row,token))
        elif identity is not None:
            if len(self.server.metadata_cache) >= 128:
                self.server.metadata_cache.clear()
            self.server.metadata_cache[cache_key] = payload
        if not payload.get('chapters'):
            payload['chapters'] = file_chapters
        self.server.player_status.remember(token, payload, payload['kind'])
        self.send_json(payload if self.headers.get('X-PSP-Web') else
                       {k:v for k,v in payload.items() if k not in ('artwork', 'chapters', 'markers')})

    def subtitles(self, token: str, track: int, tv_profile: bool = False, milliseconds: bool = False,
                  paged: bool = False, offset: int | None = None, at_ms: int = 0) -> None:
        """Return a compact cue list without involving the video transcode.

        Text tracks are converted by FFmpeg to its canonical SRT form.  This
        conversion is a short, finite extraction rather than libass/font
        initialisation in the latency-sensitive H.264 process.  Bitmap tracks
        deliberately report their kind now; the PSP client can retain the
        proven burn-in fallback until its sprite overlay transport lands.
        """
        fps = 1000 if milliseconds or paged else (LEGACY_SUBTITLE_FPS_TV if tv_profile else LEGACY_SUBTITLE_FPS_LCD)
        cache_key = (token, track, fps)
        with self.server.subtitle_cache_lock:
            cached = self.server.subtitle_cache.get(cache_key)
        def respond(payload):
            if paged:
                return self.send_json(subtitle_page(payload, offset, at_ms))
            return self.send_json(dict(payload, c=payload['c'][:MAX_SUBTITLE_CUES]))
        if cached is not None:
            return respond(cached)
        from .external_subtitles import payload as external_payload
        external = external_payload(self.server.library,token,track)
        if external:
            codec,body=external
            payload={'t':'bitmap','c':[]} if codec=='hdmv_pgs_subtitle' else {'t':'text','c':display_timeline(parse_srt_cues(body.decode('utf-8-sig',errors='replace'),fps))}
            return respond(payload)
        if token.startswith('jellyfin.'):
            text = self.server.jellyfin.text_subtitle(token, track)
            if text is not None:
                payload = {'t': 'text', 'c': display_timeline(parse_srt_cues(text, fps))}
                with self.server.subtitle_cache_lock:
                    self.server.subtitle_cache[cache_key] = payload
                return respond(payload)
        _, source = self.server.library.decode(token)
        probe = cached_probe(source, ["-select_streams", f"s:{track}",
             "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1"])
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
            payload = {"t": "text", "c": display_timeline(parse_srt_cues(extracted.stdout, fps))}
        with self.server.subtitle_cache_lock:
            self.server.subtitle_cache[cache_key] = payload
        respond(payload)

    def pgs_cues(self, token: str, track: int) -> list[PgsCue]:
        cache_key = (token, track)
        with self.server.pgs_cache_lock:
            cached = self.server.pgs_cache.pop(cache_key, None)
            if cached is not None:
                self.server.pgs_cache[cache_key] = cached
        if cached is not None:
            return cached
        from .external_subtitles import payload as external_payload
        external=external_payload(self.server.library,token,track)
        if external:
            if external[0]!='hdmv_pgs_subtitle':raise ValueError('Selected external subtitle is not PGS')
            cues=parse_pgs(external[1])
            with self.server.pgs_cache_lock:
                self.server.pgs_cache[cache_key]=cues
                while len(self.server.pgs_cache)>PGS_CACHE_TRACKS:self.server.pgs_cache.popitem(last=False)
            return cues
        _, source = self.server.library.decode(token)
        remote = isinstance(source, RemoteSource)
        if remote:
            probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', f's:{track}',
                '-show_entries', 'stream=codec_name', '-of', 'default=nw=1:nk=1', str(source)],
                capture_output=True, text=True, timeout=30, check=False)
            if probe.returncode:
                raise ValueError('Could not inspect Plex subtitle track')
            if probe.stdout.strip() != 'hdmv_pgs_subtitle':
                return []
        else:
            tracks = json.loads(subprocess.run(["mkvmerge", "-J", str(source)], capture_output=True, text=True, timeout=30, check=True).stdout)["tracks"]
            subtitle_tracks = [entry for entry in tracks if entry.get("type") == "subtitles"]
            if track >= len(subtitle_tracks) or subtitle_tracks[track].get("codec") != "HDMV PGS":
                return []
        cache_dir = Path(tempfile.gettempdir()) / "psp-streamer-pgs"
        cache_dir.mkdir(mode=0o700, exist_ok=True)
        identity = source.cache_key if remote else f'{source}:{source.stat().st_mtime_ns}'
        suffix = hashlib.sha256(f"{identity}:{track}".encode()).hexdigest()
        sup = cache_dir / f"{suffix}.sup"
        if not sup.exists():
            with tempfile.TemporaryDirectory(dir=cache_dir) as work:
                target = Path(work) / 'track.sup'
                command = (['ffmpeg', '-v', 'error', '-i', str(source), '-map', f'0:s:{track}',
                            '-c:s', 'copy', '-f', 'sup', str(target)] if remote else
                           ["mkvextract", "tracks", str(source), f"{subtitle_tracks[track]['id']}:{target}"])
                extracted = subprocess.run(command, capture_output=True, text=True, timeout=600, check=False)
                if extracted.returncode:
                    raise ValueError("Could not extract PGS subtitle track")
                target.replace(sup)
        parsed = parse_pgs(sup.read_bytes())
        with self.server.pgs_cache_lock:
            self.server.pgs_cache[cache_key] = parsed
            while len(self.server.pgs_cache) > PGS_CACHE_TRACKS:
                self.server.pgs_cache.popitem(last=False)
        return parsed

    def bitmap_subtitles(self, token: str, track: int, tv_profile: bool = False, milliseconds: bool = False) -> None:
        cues = self.pgs_cues(token, track)
        # Frames share the video presentation clock; positions are scaled by
        # the client from the original PGS canvas into 480x272.
        fps = 1000 if milliseconds else (LEGACY_SUBTITLE_FPS_TV if tv_profile else LEGACY_SUBTITLE_FPS_LCD)
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
                  tv_output: bool = False, video_fps: str = "20") -> None:
        live = token.startswith('radio.')
        if live and (container != 'mp3' or start_seconds or subtitle_track != -1):
            raise ValueError('Radio supports live MP3 playback only (no seek or subtitles)')
        source = self.server.radio.get(token)['url'] if live else self.server.library.decode(token)[1]
        if not self.server.transcode_slots.acquire(blocking=False):
            return self.send_error_json(HTTPStatus.TOO_MANY_REQUESTS, "A transcode is already running")
        process = None
        radio_lease = None
        pause_lease = None
        external_folder = None
        try:
            # Keep the existing startup/subtitle inactivity allowance. During
            # blocked body writes only, fresh client-confirmed pause reports
            # suspend that allowance. Lost clients still expire; font setup
            # is before body writes and retains its existing behavior.
            timeout_default = "180" if container in {"mp3", "flv"} else "5"
            write_timeout = float(os.environ.get("CLIENT_WRITE_TIMEOUT", timeout_default))
            self.connection.settimeout(write_timeout)
            pause_lease = self.server.stream_pauses.begin(self.client_address[0], token)
            subtitle_source = None
            bitmap_subtitle = False
            external = None
            if source is not None and container in {"h264", "flv"} and subtitle_track >= 0:
                from .external_subtitles import payload as external_payload
                external = external_payload(self.server.library,token,subtitle_track,source)
                if not external:
                    probe = cached_probe(source, ["-select_streams", f"s:{subtitle_track}", "-show_entries", "stream=codec_name", "-of", "default=nw=1:nk=1"])
                    bitmap_subtitle = probe.stdout.strip() in BITMAP_SUBTITLE_CODECS
                if external:
                    external_folder=tempfile.TemporaryDirectory(prefix='psp-external-')
                    subtitle_source=Path(external_folder.name)/('subtitle.sup' if external[0]=='hdmv_pgs_subtitle' else 'subtitle.srt')
                    subtitle_source.write_bytes(external[1])
                    bitmap_subtitle=external[0]=='hdmv_pgs_subtitle'
                elif not isinstance(source, RemoteSource):
                    alias_dir = Path(tempfile.gettempdir()) / "psp-streamer-subtitles"
                    alias_dir.mkdir(mode=0o700, exist_ok=True)
                    subtitle_source = alias_dir / hashlib.sha256(str(source).encode()).hexdigest()
                    if not subtitle_source.exists():
                        os.symlink(source, subtitle_source)
            command = radio_command(resolve_playlist(source), audio_bitrate, ffmpeg_command) if live else ffmpeg_command(source, audio_track, container, low_bandwidth, subtitle_track, audio_bitrate, subtitle_source, start_seconds, bitmap_subtitle, tv_output, video_fps, external_subtitle=external is not None)
            if live:
                radio_lease = self.server.radio.begin(token)
            process = subprocess.Popen(
                command,
                # Use the host's already-populated fontconfig cache.  The
                # earlier private cache avoided a directory scan but made
                # libass rebuild its font database for every transcode on
                # some installations, causing the long subtitle startup.
                # stderr was never consumed.  A libass warning repeated for
                # each subtitle event can fill that pipe and consequently
                # stop ffmpeg's video writer after only a few seconds.
                stdout=subprocess.PIPE, stderr=subprocess.PIPE if live else subprocess.DEVNULL,
            )
            self.send_response(HTTPStatus.OK)
            content_type = {"flv": "video/x-flv", "mpegts": "video/mp2t", "mjpeg": "image/jpeg", "h264": "video/h264", "mp3": "audio/mpeg", "mp4": "video/mp4"}[container]
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            assert process.stdout is not None
            if live:
                # Poll the client as well: Stop during an upstream stall must
                # release this transcode slot without waiting for more audio.
                self.close_connection = True
                self.connection.settimeout(10)
                last_data = time.monotonic()
                icy = IcyLogReader(self.server.radio, token, radio_lease)
                watched = [process.stdout, process.stderr, self.connection]
                while time.monotonic() - last_data < 25:
                    ready, _, _ = select.select(watched, [], [], 1)
                    if self.connection in ready:
                        break
                    if process.stderr in ready:
                        data = os.read(process.stderr.fileno(), 4096)
                        if data:
                            icy.feed(data)
                        else:
                            watched.remove(process.stderr)
                    if process.stdout in ready:
                        chunk = os.read(process.stdout.fileno(), 4096)
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        self.wfile.flush()
                        last_data = time.monotonic()
                return
            # read() waits until its complete request is filled. For low-rate
            # MJPEG that used to batch several frames into a 64-KB burst, so
            # the PSP paused and then caught up. read1() returns what ffmpeg
            # has produced now; small blocks keep frame cadence intact.
            chunk_size = 4 * 1024 if container in {"mjpeg", "h264", "flv"} else 64 * 1024
            self.wfile.flush()
            self.connection.settimeout(min(1.0, write_timeout))
            while chunk := process.stdout.read1(chunk_size):
                write_stream(self.connection, chunk, write_timeout,
                             lambda: self.server.stream_pauses.paused(pause_lease))
            process.wait(timeout=15)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            pass
        finally:
            self.server.stream_pauses.end(pause_lease)
            if radio_lease:
                self.server.radio.end(token, radio_lease)
            if process and process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if process and process.stdout:
                process.stdout.close()
            if process and process.stderr:
                process.stderr.close()
            if external_folder:
                external_folder.cleanup()
            self.server.transcode_slots.release()


class AppServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], library: Library):
        state_root = state_directory()
        self.settings = PasswordSettings()
        self.web_sessions = WebSessions()
        self.stream_pauses = StreamPauses()
        cert = os.environ.get("PSP_STREAMER_TLS_CERT", "")
        key = os.environ.get("PSP_STREAMER_TLS_KEY", "")
        self.tls_context = None
        if cert or key:
            if not cert or not key:
                raise ValueError("Set both PSP_STREAMER_TLS_CERT and PSP_STREAMER_TLS_KEY")
            self.tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            self.tls_context.minimum_version = ssl.TLSVersion.TLSv1_2
            self.tls_context.load_cert_chain(cert, key)
        super().__init__(address, AppHandler)
        self.library = library
        self.plex = Plex(state_root, library.roots)
        library.plex = self.plex
        self.jellyfin = Jellyfin(state_root, library.roots)
        library.jellyfin = self.jellyfin
        from .dlna import Dlna
        self.dlna = Dlna(state_root)
        library.dlna = self.dlna
        self.plex.additional_source = lambda: self.jellyfin.config['enabled'] or self.dlna.config['enabled']
        self.jellyfin.additional_source = lambda: self.dlna.config['enabled'] or any(self.plex.config[k] for k in ('enabled','files','radio'))
        self.dlna.additional_source = lambda: self.jellyfin.config['enabled'] or any(self.plex.config[k] for k in ('enabled','files','radio'))
        self.radio = RadioDirectory(os.environ.get('PSP_STREAMER_RADIO_DIR') or
            state_root)
        self.metadata_cache: dict[tuple, object] = {}
        self.subtitle_cache: dict[tuple[str, int], object] = {}
        self.subtitle_cache_lock = threading.Lock()
        # PGS tracks contain every decoded bitmap of an episode.  Retaining
        # them indefinitely makes a long browsing session consume host RAM.
        self.pgs_cache: OrderedDict[tuple[str, int], list[PgsCue]] = OrderedDict()
        self.pgs_cache_lock = threading.Lock()
        # New video clients use one muxed FLV process; music uses one MP3
        # process. Spare slots allow reconnects and legacy two-stream clients.
        self.transcode_slots = threading.BoundedSemaphore(int(os.environ.get("MAX_TRANSCODES", "4")))
        self.remote_lock = threading.Lock()
        self.remote_sequence = 0
        self.remote_deadline = 0.0
        self.remote_session = os.urandom(16).hex()
        self.remote_command: dict[str, object] = {"seq": 0, "action": "idle"}
        from .player_status import PlayerStatus
        self.player_status = PlayerStatus(self.plex.path.parent)
        from .offline import OfflineQueue
        cache_root = os.environ.get('PSP_STREAMER_DOWNLOAD_DIR') or str(state_root / 'downloads')
        self.offline = OfflineQueue(cache_root, library, ffmpeg_command, parse_srt_cues, self.transcode_slots)
        if self.offline.jobs:
            self.offline.start()

    def server_close(self):
        if hasattr(self,'dlna'):
            self.dlna.close()
        if hasattr(self, 'offline'):
            self.offline.close()
        if hasattr(self, 'plex'):
            self.plex.close()
        if hasattr(self, 'jellyfin'):
            self.jellyfin.close()
        super().server_close()

    def get_request(self):
        connection, address = super().get_request()
        if self.tls_context:
            connection.settimeout(15)
            try:
                connection = self.tls_context.wrap_socket(connection, server_side=True,
                                                          do_handshake_on_connect=False)
            except Exception:
                connection.close()
                raise
        return connection, address

    def set_remote_command(self, command: dict[str, object]) -> dict[str, object]:
        with self.remote_lock:
            self.remote_sequence += 1
            self.remote_deadline = time.monotonic() + 15.0
            self.remote_command = {**command, "seq": self.remote_sequence, "session": self.remote_session}
            return dict(self.remote_command)

    def remote_after(self, sequence: int) -> dict[str, object]:
        with self.remote_lock:
            if self.remote_sequence > sequence and time.monotonic() < self.remote_deadline:
                return dict(self.remote_command)
            return {"seq": self.remote_sequence, "session": self.remote_session, "action": "idle"}


def main() -> None:
    roots = load_roots(os.environ.get("MEDIA_ROOTS"))
    host, port = os.environ.get("BIND", "0.0.0.0"), int(os.environ.get("PORT", "8091"))
    server = AppServer((host, port), Library(roots))
    scheme = "https" if server.tls_context else "http"
    print(f"PSP Streamer ready at {scheme}://{host}:{port} (roots: {', '.join(map(str, roots))})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
