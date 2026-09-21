"""Bounded streaming writes with client-confirmed pause heartbeats."""
import threading
import time


class StreamPauses:
    def __init__(self):
        self.lock = threading.Lock()
        self.active = {}

    def begin(self, client, media):
        lease = object()
        with self.lock:
            self.active[lease] = (client, media, False, 0.0)
        return lease

    def report(self, client, media, paused):
        with self.lock:
            for lease, (owner, token, _, _) in list(self.active.items()):
                if (owner, token) == (client, media):
                    self.active[lease] = (owner, token, paused, time.monotonic())

    def paused(self, lease):
        with self.lock:
            entry = self.active.get(lease)
            return bool(entry and entry[2] and time.monotonic() - entry[3] < 45)

    def end(self, lease):
        with self.lock:
            self.active.pop(lease, None)


def write_stream(connection, chunk, timeout, paused):
    # Unlike retrying sendall()/wfile.write(), send() exposes how much was
    # transmitted. Retrying cannot duplicate a partially delivered FLV tag.
    pending = memoryview(chunk)
    last_progress = time.monotonic()
    while pending:
        if paused():
            last_progress = time.monotonic()
        try:
            sent = connection.send(pending)
        except TimeoutError:
            if time.monotonic() - last_progress >= timeout:
                raise
            continue
        if sent <= 0:
            raise BrokenPipeError("Streaming client disconnected")
        pending = pending[sent:]
        last_progress = time.monotonic()
