"""Optional focused browser test: run with Playwright installed, not in full suites."""
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from psp_streamer.server import AppServer, Library


@unittest.skipUnless(os.environ.get('PSP_ARTWORK_BROWSER_TEST'), 'opt-in browser smoke test')
class ArtworkBrowserTests(unittest.TestCase):
    def test_grid_details_and_no_playback_commands(self):
        from playwright.sync_api import sync_playwright
        with tempfile.TemporaryDirectory() as directory, patch.dict('os.environ', {
                'PSP_STREAMER_SETTINGS_DIR':directory,'PSP_STREAMER_DOWNLOAD_DIR':directory+'/downloads',
                'PSP_STREAMER_PASSWORD':''}):
            server=AppServer(('127.0.0.1',0),Library([Path(directory)]))
            thread=threading.Thread(target=server.serve_forever);thread.start()
            try:
                with sync_playwright() as browser:
                    chromium=browser.chromium.launch(executable_path='/usr/bin/google-chrome-stable',headless=True)
                    page=chromium.new_page(viewport={'width':1100,'height':800})
                    errors=[];commands=[]
                    page.on('pageerror',lambda e:errors.append(str(e)))
                    page.on('request',lambda r:commands.append(r.url) if '/api/remote/command' in r.url else None)
                    art={'cover':'/api/artwork/plex.42.0123456789ab/cover','backdrop':'/api/artwork/plex.42.0123456789ab/backdrop'}
                    page.route('**/api/artwork/**',lambda route:route.fulfill(path=str(Path('static/logo.png').resolve()),content_type='image/png'))
                    page.route('**/api/library?*',lambda route:route.fulfill(json={
                        'root':0,'path':':plex:s1','parent':'', 'folders':[],
                        'videos':[{'id':'plex.42.0123456789ab','name':'S01E02 Example','kind':'video','artwork':art}]}))
                    page.route('**/api/metadata/*',lambda route:route.fulfill(json={
                        'provider':'plex','d':1200,'name':'Example','summary':'Series description',
                        'a':[{'n':0,'l':'jpn'}],'s':[],'artwork':art}))
                    page.goto(f'http://127.0.0.1:{server.server_port}/')
                    page.get_by_role('button',name='Cover view',exact=True).click()
                    page.locator('#library img').wait_for(state='visible')
                    page.locator('#library button').click()
                    page.locator('.media-cover').wait_for(state='visible')
                    page.locator('.media-backdrop').wait_for(state='visible')
                    self.assertIn('Series description',page.locator('#details').inner_text())
                    self.assertEqual(errors,[]);self.assertEqual(commands,[])
                    page.screenshot(path='/tmp/pspstreamer-artwork-web.png')
                    page.set_viewport_size({'width':390,'height':844})
                    self.assertFalse(page.evaluate('document.documentElement.scrollWidth>innerWidth'))
                    chromium.close()
            finally:
                server.shutdown();thread.join();server.server_close()
