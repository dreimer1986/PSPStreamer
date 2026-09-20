import base64
import http.client
import json
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch
from psp_streamer.settings import PasswordSettings
from psp_streamer.server import AppServer, Library


class SettingsTests(unittest.TestCase):
    def test_oc_settings_validated_save_backup_and_failure(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "oc_settings"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                "-I", str(root / "psp-client"), str(root / "tests/oc_settings_harness.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], cwd=temp, check=True, timeout=5)

    def test_psp_settings_cancel_save_failure_long_names_and_utf8_editing(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "settings"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                "-I", str(root / "psp-client"), str(root / "tests/app_settings_harness.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_saved_verifier_survives_restart_and_overrides_bootstrap(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ,
                {"PSP_STREAMER_SETTINGS_DIR": temp, "PSP_STREAMER_PASSWORD": "original"}):
            settings = PasswordSettings()
            self.assertTrue(settings.verify(b"original"))
            with self.assertRaises(ValueError): settings.change("wrong", "new")
            settings.change("original", "ä:new:password")
            self.assertFalse(settings.verify(b"original"))
            self.assertTrue(settings.verify("ä:new:password".encode()))
            saved = (Path(temp) / "password.json").read_text()
            self.assertNotIn("password", saved); self.assertNotIn("original", saved)
            restored = PasswordSettings()
            self.assertTrue(restored.verify("ä:new:password".encode()))
            self.assertFalse(restored.verify(b"original"))
            with patch("psp_streamer.settings.os.replace", side_effect=OSError("disk")):
                with self.assertRaises(OSError): restored.change("ä:new:password", "lost")
            self.assertTrue(restored.verify("ä:new:password".encode()))
            self.assertEqual((Path(temp) / "password.json").read_text(), saved)

    def test_validation_and_environment_managed_mode(self):
        with patch.dict(os.environ, {"PSP_STREAMER_SETTINGS_DIR": "", "PSP_STREAMER_PASSWORD": "ha"}):
            settings = PasswordSettings()
            with self.assertRaises(ValueError): settings.change("ha", "new")
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ,
                {"PSP_STREAMER_SETTINGS_DIR": temp, "PSP_STREAMER_PASSWORD": ""}):
            settings = PasswordSettings()
            for value in ("", "x" * 129, "ä" * 65, "a\nb", None, 123):
                with self.subTest(value=str(value)[:8]), self.assertRaises(ValueError): settings.change("", value)
            settings.change("", "initial")
            self.assertTrue(settings.protected)
            (Path(temp) / "password.json").write_text("{}")
            with self.assertRaises((ValueError, KeyError)): PasswordSettings()

    def test_web_change_auth_csrf_and_no_secret_response(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ,
                {"PSP_STREAMER_SETTINGS_DIR": temp, "PSP_STREAMER_PASSWORD": "", "PSP_STREAMER_TLS_CERT": "", "PSP_STREAMER_TLS_KEY": ""}):
            with AppServer(("127.0.0.1", 0), Library([Path(temp)])) as server:
                thread = threading.Thread(target=server.serve_forever); thread.start()
                client = http.client.HTTPConnection(*server.server_address, timeout=3)
                def request(method, path, payload=None, extra=None):
                    headers = {"Content-Type": "application/json", **(extra or {})}
                    client.request(method, path, json.dumps(payload) if payload is not None else None, headers)
                    response = client.getresponse(); data = response.read()
                    return response.status, json.loads(data) if data else None
                try:
                    status, data = request("GET", "/api/settings")
                    self.assertEqual(data, {"password_editable": True, "password_set": False})
                    status, _ = request("POST", "/api/settings/password", {"password": "first"}, {"Origin": "https://evil.invalid"})
                    self.assertEqual(status, 403)
                    self.assertEqual(request("POST", "/api/settings/password", {"password": "first"})[0], 200)
                    self.assertEqual(request("GET", "/api/health")[0], 401)
                    auth = {"Authorization": "Basic " + base64.b64encode(b"psp:first").decode()}
                    self.assertEqual(request("POST", "/api/settings/password", {"current": "wrong", "password": "second"}, auth)[0], 400)
                    self.assertEqual(request("POST", "/api/settings/password", {"current": "first", "password": "second"}, auth)[0], 200)
                    self.assertEqual(request("GET", "/api/settings", extra=auth)[0], 401)
                finally: client.close(); server.shutdown(); thread.join()

    def test_native_server_https_and_partial_configuration(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp); cert, key = base / "cert.pem", base / "key.pem"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                "-nodes", "-keyout", str(key), "-out", str(cert), "-days", "1", "-subj", "/CN=localhost"], check=True, capture_output=True)
            with patch.dict(os.environ, {"PSP_STREAMER_SETTINGS_DIR": "", "PSP_STREAMER_PASSWORD": "",
                    "PSP_STREAMER_TLS_CERT": str(cert), "PSP_STREAMER_TLS_KEY": str(key)}):
                with AppServer(("127.0.0.1", 0), Library([base])) as server:
                    thread = threading.Thread(target=server.serve_forever); thread.start()
                    client = http.client.HTTPSConnection(*server.server_address, timeout=3, context=ssl._create_unverified_context())
                    try:
                        client.request("GET", "/api/health")
                        reply = client.getresponse(); self.assertEqual(reply.status, 200)
                        self.assertTrue(json.loads(reply.read())["ok"])
                    finally: client.close(); server.shutdown(); thread.join()
                os.environ["PSP_STREAMER_TLS_KEY"] = ""
                with self.assertRaises(ValueError): AppServer(("127.0.0.1", 0), Library([base]))
