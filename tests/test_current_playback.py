import functools
import http.server
import os
from pathlib import Path
import subprocess
import threading
import unittest


class CurrentPlaybackTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('PLAYWRIGHT_MODULE'), 'requires Playwright')
    def test_actual_browser(self):
        root=Path(__file__).resolve().parents[1]
        handler=functools.partial(http.server.SimpleHTTPRequestHandler,directory=str(root/'static'))
        with http.server.ThreadingHTTPServer(('127.0.0.1',0),handler) as server:
            worker=threading.Thread(target=server.serve_forever);worker.start()
            try:
                subprocess.run(['node',str(root/'tests/current_playback_browser.cjs'),
                    f'http://127.0.0.1:{server.server_port}/'],check=True,timeout=35)
            finally:
                server.shutdown();worker.join()
