"""Low-volume transport evidence; never log media URLs, credentials or content."""
import time


class StreamTrace:
    def __init__(self, pid, emit):
        self.pid, self.emit = pid, emit
        self.start = self.last_read = self.last_send = time.monotonic()
        self.next_report = self.start
        self.read = self.sent = 0

    def report(self, phase, force=False):
        now = time.monotonic()
        if not force and now < self.next_report:
            return
        self.next_report = now + 30
        self.emit('stream transport pid=%d phase=%s elapsed=%.1f read=%d sent=%d '
                  'read_age=%.1f send_age=%.1f', self.pid, phase, now-self.start,
                  self.read, self.sent, now-self.last_read, now-self.last_send)

    def received(self, size):
        self.read += size
        self.last_read = time.monotonic()

    def delivered(self, size):
        self.sent += size
        if size:
            self.last_send = time.monotonic()
        self.report('sending' if size else 'client backpressure')
