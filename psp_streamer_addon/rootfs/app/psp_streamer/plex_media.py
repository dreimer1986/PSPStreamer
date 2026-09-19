"""Seekable loopback bridge for original Plex media, never a Plex transcode.

Only signed, server-bound part paths are accepted. Plex credentials stay in
HTTP headers inside this process, not FFmpeg arguments or public API replies.
"""
import base64
from dataclasses import dataclass
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from http.client import HTTPException
import json
from pathlib import PurePosixPath
import re
import secrets
import threading
from urllib.error import HTTPError, URLError
from urllib.request import Request, build_opener


@dataclass(frozen=True)
class RemoteSource:
    url: str
    name: str
    cache_key: str

    @property
    def suffix(self):
        return PurePosixPath(self.name).suffix

    @property
    def stem(self):
        return PurePosixPath(self.name).stem

    def __str__(self):
        return self.url


class PlexMediaBridge(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self, plex):
        self.plex = plex
        self.secret = secrets.token_bytes(32)
        self.slots = threading.BoundedSemaphore(16)
        self.closed = threading.Event()
        super().__init__(('127.0.0.1', 0), PlexMediaHandler)
        self.worker = threading.Thread(target=self.serve_forever, name='PlexOriginal', daemon=True)
        self.worker.start()

    def source(self, part, name, revision):
        key = part.get('key', '')
        # A Part key is an API path, not an arbitrary URL or redirect target.
        if not isinstance(key, str) or not re.fullmatch(r'/library/parts/[0-9]+/[A-Za-z0-9_./%~-]+', key):
            raise ValueError('Plex did not provide a valid original media endpoint')
        if any(piece in ('.', '..') for piece in key.split('/')) or '%2e' in key.lower() or '%2f' in key.lower():
            raise ValueError('Invalid Plex original media endpoint')
        payload = base64.urlsafe_b64encode(json.dumps([self.plex.namespace(), key], separators=(',', ':')).encode()).decode().rstrip('=')
        signature = hmac.new(self.secret, payload.encode(), hashlib.sha256).hexdigest()
        url = f'http://127.0.0.1:{self.server_port}/{payload}.{signature}'
        return RemoteSource(url, name, hashlib.sha256(f'{payload}:{revision}:{part.get("size", 0)}'.encode()).hexdigest())

    def resolve(self, path):
        if len(path) > 4096:
            raise ValueError('Invalid capability')
        payload, signature = path.lstrip('/').split('.')
        expected = hmac.new(self.secret, payload.encode(), hashlib.sha256).hexdigest()
        if not hmac.compare_digest(signature, expected):
            raise ValueError('Invalid capability')
        namespace, key = json.loads(base64.urlsafe_b64decode(payload + '=' * (-len(payload) % 4)))
        with self.plex.lock:
            self.plex.require()
            if namespace != self.plex.namespace():
                raise ValueError('Plex server changed')
            return self.plex.config['url'] + key, self.plex.config['token'], self.plex.config['client']

    def close(self):
        if self.closed.is_set():
            return
        self.closed.set()
        self.shutdown()
        self.server_close()
        self.worker.join(timeout=2)


class PlexMediaHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # Capability paths and upstream credentials are never logged.

    def do_HEAD(self):
        self.transfer(head=True)

    def do_GET(self):
        self.transfer(head=False)

    def transfer(self, head):
        from .plex import NoRedirect
        try:
            url, token, client = self.server.resolve(self.path)
        except (ValueError, TypeError, KeyError):
            self.send_error(404)
            return
        if not self.server.slots.acquire(blocking=False):
            self.send_error(503)
            return
        started = False
        self.connection.settimeout(20)
        try:
            headers = {'X-Plex-Token': token, 'X-Plex-Client-Identifier': client,
                       'Accept-Encoding': 'identity'}
            requested_range = self.headers.get('Range')
            if requested_range:
                if not re.fullmatch(r'bytes=(?:[0-9]{1,20}-[0-9]{0,20}|-[0-9]{1,20})', requested_range):
                    self.send_error(416)
                    return
                headers['Range'] = requested_range
            request = Request(url, headers=headers, method='HEAD' if head else 'GET')
            with build_opener(NoRedirect()).open(request, timeout=20) as upstream:
                if upstream.status not in (200, 206):
                    raise ValueError('Unexpected Plex response')
                self.send_response(upstream.status)
                for key in ('Content-Type', 'Content-Length', 'Content-Range', 'Accept-Ranges'):
                    value = upstream.headers.get(key)
                    if value is not None:
                        self.send_header(key, value)
                self.send_header('Connection', 'close')
                self.end_headers()
                started = True
                if not head:
                    while not self.server.closed.is_set():
                        block = upstream.read(64 * 1024)
                        if not block:
                            break
                        self.wfile.write(block)
        except HTTPError as exc:
            if not started:
                self.send_error(416 if exc.code == 416 else 502)
            exc.close()
        except (OSError, URLError, ValueError, HTTPException):
            if not started:
                try:
                    self.send_error(502)
                except OSError:
                    pass
        finally:
            self.close_connection = True
            self.server.slots.release()
