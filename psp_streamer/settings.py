"""Single-password settings. Environment bootstraps; a saved verifier wins."""
import hashlib
import hmac
import json
import os
from pathlib import Path
import tempfile
import threading


class PasswordSettings:
    def __init__(self):
        directory = os.environ.get("PSP_STREAMER_SETTINGS_DIR", "")
        self.path = Path(directory) / "password.json" if directory else None
        self.lock = threading.RLock()
        self.bootstrap = os.environ.get("PSP_STREAMER_PASSWORD", "").encode("utf-8")
        self.record = None
        self.cache_key = os.urandom(32)
        self.cached = None
        if self.path and self.path.exists():
            record = json.loads(self.path.read_text())
            if (record.get("version") != 1 or len(bytes.fromhex(record["salt"])) != 16
                    or len(bytes.fromhex(record["hash"])) != 32):
                raise ValueError("Invalid password settings; refusing to start unprotected")
            self.record = record

    @property
    def protected(self):
        return self.record is not None or bool(self.bootstrap)

    def verify(self, password):
        with self.lock:
            if not self.protected:
                return True
            if len(password) > 128:
                return False
            key = hmac.digest(self.cache_key, password, "sha256")
            if self.cached is not None and hmac.compare_digest(key, self.cached):
                return True
            if self.record is None:
                valid = hmac.compare_digest(password, self.bootstrap)
            else:
                derived = hashlib.pbkdf2_hmac("sha256", password,
                    bytes.fromhex(self.record["salt"]), 200000)
                valid = hmac.compare_digest(derived, bytes.fromhex(self.record["hash"]))
            if valid:
                self.cached = key
            return valid

    def change(self, current, password):
        if self.path is None:
            raise ValueError("Password is managed by the server environment / Home Assistant")
        if not isinstance(current, str) or not isinstance(password, str):
            raise ValueError("Passwords must be text")
        encoded = password.encode("utf-8")
        if not 1 <= len(encoded) <= 128 or any(c in password for c in "\r\n\0"):
            raise ValueError("Use 1–128 UTF-8 bytes without line breaks")
        with self.lock:
            if not self.verify(current.encode("utf-8")):
                raise ValueError("Current password is incorrect")
            salt = os.urandom(16)
            record = {"version": 1, "salt": salt.hex(),
                      "hash": hashlib.pbkdf2_hmac("sha256", encoded, salt, 200000).hex()}
            self.path.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix=".password-", dir=self.path.parent)
            try:
                with os.fdopen(fd, "w") as output:
                    json.dump(record, output)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(temporary, self.path)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
            self.record = record
            self.bootstrap = b""
            self.cached = None
