"""Ephemeral, acknowledged PSP input. Never persist keys or field contents."""
from collections import deque
import threading
import time
import re


class RemoteInput:
    MASK = 0xF3F9  # SELECT, START, directions, L/R, triangle/circle/cross/square
    ONLINE_SECONDS = 12  # Slow PSP polling plus bounded TLS handshake.
    EVENT_SECONDS = 15   # Tap/text delivery, separate from the short hold lease.

    def __init__(self):
        self.lock = threading.Condition()
        self.client = ''
        self.seen = 0.0
        self.dialog = self.capacity = self.secret = self.sequence = 0
        self.owner = ''
        self.serial = -1
        self.lease = 0.0
        self.keys = (0, 128, 128)
        self.keys_since = 0.0
        self.queue = deque()

    def _expire(self, now):
        if now >= self.lease:
            self.keys = (0, 128, 128)
        while self.queue and self.queue[0][1] <= now:
            self.queue.popleft()

    def status(self):
        with self.lock:
            online = bool(self.client) and time.monotonic() - self.seen < self.ONLINE_SECONDS
            return {'online': online, 'client': self.client if online else '', 'dialog': self.dialog if online else 0,
                    'capacity': self.capacity if online else 0, 'secret': bool(self.secret)}

    def submit(self, data):
        if not isinstance(data, dict):
            raise ValueError('Invalid input')
        owner = data.get('owner', '')
        serial = data.get('serial')
        if not isinstance(owner, str) or not re.fullmatch(r'[a-zA-Z0-9-]{8,64}', owner):
            raise ValueError('Invalid controller ID')
        if type(serial) is not int or not 0 <= serial < 2**53:
            raise ValueError('Invalid input sequence')
        with self.lock:
            now = time.monotonic()
            self._expire(now)
            if not self.client or now - self.seen >= self.ONLINE_SECONDS:
                raise ValueError('PSP input is offline')
            if self.owner == owner and serial <= self.serial:
                return {'ok': True}  # Delayed duplicate/reordered browser request.
            if self.owner != owner and now < self.lease:
                raise ValueError('Another browser is controlling the PSP')
            if self.owner != owner:
                self.queue.clear()  # Never deliver the previous owner's backlog.
            if len(self.queue) >= 32:
                raise ValueError('PSP input queue is full')
            if 'text' in data:
                text = data['text']
                if not self.dialog or data.get('dialog') != self.dialog or data.get('client') != self.client:
                    raise ValueError('Open a text field on the PSP first')
                if not isinstance(text, str) or any(ord(c) < 32 or ord(c) == 127 for c in text):
                    raise ValueError('Use single-line text without control characters')
                try:
                    encoded = text.encode('utf-8')
                except UnicodeEncodeError:
                    raise ValueError('Invalid UTF-8 text') from None
                if len(encoded) >= self.capacity:
                    raise ValueError('Text exceeds the PSP field capacity')
                event = (2, 0, 128, 128, self.dialog, encoded.hex() or '-')
            else:
                values = tuple(data.get(k, default) for k, default in [('mask', 0), ('x', 128), ('y', 128)])
                if any(type(v) is not int for v in values) or values[0] < 0 or values[0] & ~self.MASK or not all(0 <= v <= 255 for v in values[1:]):
                    raise ValueError('Invalid PSP buttons or analog position')
                event = (1, *values, 0, '-') if values != self.keys else None
                if values[0] != self.keys[0]:
                    self.keys_since = now
                self.keys = values
            self.owner, self.serial, self.lease = owner, serial, now + 2
            if event:
                self.sequence += 1
                self.queue.append((self.sequence, now + self.EVENT_SECONDS, event))
            self.lock.notify_all()
            return {'ok': True}

    def poll(self, query):
        client = query.get('client', [''])[0]
        if not re.fullmatch(r'[a-zA-Z0-9-]{8,64}', client):
            raise ValueError('Invalid PSP input client')
        ack, dialog, capacity, secret = (int(query.get(k, ['0'])[0]) for k in ('ack', 'dialog', 'capacity', 'secret'))
        if not 0 <= ack <= 0x7fffffff or not 0 <= dialog <= 0x7fffffff or not 0 <= capacity <= 256 or secret not in (0, 1):
            raise ValueError('Invalid PSP input state')
        with self.lock:
            now = time.monotonic()
            if self.client != client:
                self.queue.clear()
                self.keys = (0, 128, 128)
                self.owner, self.serial, self.lease = '', -1, 0
                self.client = client
            self.sequence = max(self.sequence, ack)
            self.seen = now
            if dialog != self.dialog:
                self.queue = deque(e for e in self.queue if e[2][0] != 2)
            self.dialog, self.capacity, self.secret = dialog, capacity, secret
            while self.queue and self.queue[0][0] <= ack:
                self.queue.popleft()
            self._expire(now)
            if not self.queue:
                self.lock.wait(0.35)
            now = time.monotonic()
            self._expire(now)
            if self.queue:
                seq, event_until, event = self.queue[0]
                kind, mask, x, y, field, text = event
            else:
                seq, kind, field, text = ack, 0, 0, '-'
                mask, x, y = self.keys
                # A queued press is a short impulse, not a one-second hold.
                # Only a still-down key, after the initial hold delay, may
                # renew a hold. Quick taps cannot be revived by idle polls.
                if mask and now - self.keys_since >= 0.35:
                    kind = 3
                else:
                    mask = 0
            ttl = max(0, min(1000, int(((event_until if self.queue else self.lease) - now) * 1000)))
            return f'{seq} {kind} {mask} {x} {y} {ttl} {field} {text}\n'
