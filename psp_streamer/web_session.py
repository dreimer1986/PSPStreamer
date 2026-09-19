"""Bounded, process-local browser sessions; PSP Basic authentication is separate."""
from collections import OrderedDict
from http.cookies import SimpleCookie, CookieError
import secrets
import threading
import time


class WebSessions:
    lifetime = 12 * 3600

    def __init__(self):
        self.sessions = OrderedDict()
        self.attempts = OrderedDict()
        self.lock = threading.Lock()

    def allow_login(self, address):
        with self.lock:
            now = time.monotonic()
            recent = [t for t in self.attempts.pop(address, []) if now-t < 60]
            allowed = len(recent) < 10
            if allowed:
                recent.append(now)
            self.attempts[address] = recent
            while len(self.attempts) > 1024:
                self.attempts.popitem(last=False)
            return allowed

    @staticmethod
    def token(header):
        try:
            cookie = SimpleCookie()
            cookie.load(header or '')
            return cookie['psp_session'].value if 'psp_session' in cookie else ''
        except CookieError:
            return ''

    def create(self):
        token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
        with self.lock:
            self.sessions[token] = (time.monotonic()+self.lifetime, csrf)
            while len(self.sessions) > 64:
                self.sessions.popitem(last=False)
        return token, csrf

    def get(self, header):
        token = self.token(header)
        with self.lock:
            record = self.sessions.get(token)
            if record and record[0] > time.monotonic():
                return record[1]
            self.sessions.pop(token, None)
        return None

    def revoke(self, header):
        with self.lock:
            self.sessions.pop(self.token(header), None)

    def clear(self):
        with self.lock:
            self.sessions.clear()
