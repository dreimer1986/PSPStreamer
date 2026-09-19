import http.client
import json
import os
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from psp_streamer.server import AppServer, Library
from psp_streamer.web_session import WebSessions


class SessionTests(unittest.TestCase):
    def test_expiry_limits_and_throttling(self):
        sessions = WebSessions()
        with patch('psp_streamer.web_session.time.monotonic', return_value=100):
            token, csrf = sessions.create()
            self.assertEqual(sessions.get('psp_session='+token), csrf)
            self.assertIsNone(sessions.get('psp_session=unknown'))
            for _ in range(10):
                self.assertTrue(sessions.allow_login('client'))
            self.assertFalse(sessions.allow_login('client'))
        with patch('psp_streamer.web_session.time.monotonic', return_value=100+sessions.lifetime):
            self.assertIsNone(sessions.get('psp_session='+token))
            self.assertTrue(sessions.allow_login('client'))
        for _ in range(70):
            sessions.create()
        self.assertEqual(len(sessions.sessions), 64)
        sessions.clear()
        self.assertFalse(sessions.sessions)

    def test_login_csrf_logout_and_password_revocation(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {
                'PSP_STREAMER_PASSWORD': 'test-password',
                'PSP_STREAMER_SETTINGS_DIR': temp,
                'PSP_STREAMER_DOWNLOAD_DIR': temp+'/downloads'}):
            with AppServer(('127.0.0.1', 0), Library([Path(temp)])) as server:
                worker = threading.Thread(target=server.serve_forever)
                worker.start()
                def request(method, path, data=None, headers=None):
                    conn = http.client.HTTPConnection(*server.server_address, timeout=5)
                    conn.request(method, path, json.dumps(data) if data is not None else None,
                                 {'Content-Type': 'application/json', 'X-PSP-Web': '1', **(headers or {})})
                    reply = conn.getresponse()
                    result = reply.status, dict(reply.getheaders()), reply.read()
                    conn.close()
                    return result
                try:
                    self.assertEqual(request('GET', '/login')[0], 200)
                    self.assertEqual(request('GET', '/logo.png')[0], 200)
                    code, headers, _ = request('GET', '/api/session')
                    self.assertEqual(code, 401)
                    self.assertNotIn('WWW-Authenticate', headers)
                    code, headers, _ = request('GET', '/api/offline/export/expired',
                        headers={'Sec-Fetch-Mode': 'navigate', 'Accept': 'text/html'})
                    self.assertEqual(code, 303)
                    self.assertEqual(headers['Location'], '/login')
                    self.assertNotIn('WWW-Authenticate', headers)
                    self.assertEqual(request('POST', '/api/login', {'password': 'wrong'})[0], 401)
                    self.assertEqual(request('POST', '/api/login', {'password': 'test-password'},
                        {'Origin': 'https://evil.invalid'})[0], 403)
                    origin = 'https://%s:%d' % server.server_address
                    code, headers, _ = request('POST', '/api/login', {'password': 'test-password'}, {'Origin': origin})
                    self.assertEqual(code, 200)
                    for attribute in ('HttpOnly', 'SameSite=Strict', 'Secure', 'Max-Age=43200'):
                        self.assertIn(attribute, headers['Set-Cookie'])
                    cookie = headers['Set-Cookie'].split(';')[0]
                    auth = {'Cookie': cookie}
                    code, _, body = request('GET', '/api/session', headers=auth)
                    self.assertEqual(code, 200)
                    csrf = json.loads(body)['csrf']
                    self.assertEqual(request('POST', '/api/remote/command', {'action': 'stop'}, auth)[0], 403)
                    auth['X-CSRF-Token'] = csrf
                    self.assertEqual(request('POST', '/api/remote/command', {'action': 'stop'}, auth)[0], 200)
                    self.assertEqual(request('POST', '/api/logout', {}, auth)[0], 200)
                    self.assertEqual(request('GET', '/api/session', headers=auth)[0], 401)
                    code, headers, _ = request('POST', '/api/login', {'password': 'test-password'})
                    auth = {'Cookie': headers['Set-Cookie'].split(';')[0]}
                    auth['X-CSRF-Token'] = json.loads(request('GET', '/api/session', headers=auth)[2])['csrf']
                    self.assertEqual(request('POST', '/api/settings/password',
                        {'current': 'test-password', 'password': 'new-password'}, auth)[0], 200)
                    self.assertEqual(request('GET', '/api/session', headers=auth)[0], 401)
                    self.assertEqual(request('POST', '/api/login', {'password': 'test-password'})[0], 401)
                    self.assertEqual(request('POST', '/api/login', {'password': 'new-password'})[0], 200)
                finally:
                    server.shutdown()
                    worker.join()
