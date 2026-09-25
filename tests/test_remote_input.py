import unittest
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch
from psp_streamer.remote_input import RemoteInput


class InputTests(unittest.TestCase):
    def test_native_psp_mailbox(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            binary=str(Path(tmp)/'input')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                '-I',str(root/'psp-client'),str(root/'tests/input_native.c'),'-o',binary],check=True)
            subprocess.run([binary],check=True,timeout=5)

    def setUp(self):
        self.clock = patch('psp_streamer.remote_input.time.monotonic', return_value=100)
        self.now = self.clock.start()
        self.addCleanup(self.clock.stop)
        self.r = RemoteInput()
        self.q = dict(client=['psp-test-1234'], ack=['0'], dialog=['1'], capacity=['64'], secret=['1'])
        self.wait = patch.object(self.r.lock, 'wait')
        self.wait.start()
        self.addCleanup(self.wait.stop)
        self.r.poll(self.q)

    def send(self, serial, **kw):
        return self.r.submit(dict(owner='browser-1234', serial=serial, client=self.r.client, **kw))

    def packet(self):
        return self.r.poll(self.q).split()

    def test_press_release_ack_repeat_and_expiry(self):
        self.send(1, mask=0x300)
        self.send(2, mask=0)
        p = self.packet()
        self.assertEqual(p[1:3], ['1', '768'])
        self.assertEqual(self.packet(), p)  # retry until consumed
        self.q['ack'] = [p[0]]
        p = self.packet()
        self.assertEqual(p[1:3], ['1', '0'])
        self.q['ack'] = [p[0]]
        self.send(3, mask=16)
        self.now.return_value = 103
        self.assertEqual(self.packet()[2:6], ['0', '128', '128', '0'])
        self.assertFalse(self.r.queue)

    def test_text_cannot_cross_app_restart(self):
        stale=dict(owner='browser-1234',serial=1,client=self.r.client,text='secret',dialog=1)
        self.q['client']=['psp-new-instance']
        self.r.poll(self.q)
        with self.assertRaises(ValueError): self.r.submit(stale)

    def test_tap_is_not_revived_and_real_hold_is_renewed(self):
        self.send(1, mask=16)
        p=self.packet()
        self.assertEqual(p[1], '1')
        self.q['ack']=[p[0]]
        self.now.return_value=100.1
        self.assertEqual(self.packet()[1:3], ['0', '0'])
        self.now.return_value=100.4
        self.assertEqual(self.packet()[1:3], ['3', '16'])
        self.send(2, mask=0)
        p=self.packet();self.q['ack']=[p[0]]
        self.assertEqual(self.packet()[1:3], ['0', '0'])

    def test_local_start_uses_normal_main_cleanup(self):
        root=Path(__file__).resolve().parents[1]
        local=(root/'psp-client/offline_ui.h').read_text()
        main=(root/'psp-client/main.c').read_text()
        self.assertIn('if(pressed&PSP_CTRL_START){app_exit_requested=1;return;}',local)
        loop=main[main.index('int main(void)'):]
        self.assertIn('if(app_exit_requested)break;',loop)
        self.assertLess(loop.index('if(app_exit_requested)break;'),loop.index('int browser_stopped=exit_join_worker'))

    def test_restart_and_ordering(self):
        self.send(5, mask=16)
        self.send(4, mask=32)
        self.assertEqual(self.r.keys[0], 16)
        self.q['client'] = ['psp-restarted']
        self.assertEqual(self.packet()[2], '0')
        self.assertFalse(self.r.queue)

    def test_text_scope_utf8_and_limits(self):
        self.send(1, text='Grüße', dialog=1)
        p = self.packet()
        self.assertEqual(bytes.fromhex(p[7]).decode(), 'Grüße')
        self.assertNotIn('Grüße', str(self.r.status()))
        self.q['dialog'] = ['2']
        self.assertEqual(self.packet()[1], '0')
        with self.assertRaises(ValueError): self.send(2, text='late', dialog=1)
        with self.assertRaises(ValueError): self.send(3, text='ä'*32, dialog=2)
        with self.assertRaises(ValueError): self.send(4, text='line\nbreak', dialog=2)
        with self.assertRaises(ValueError): self.send(4, text='\ud800', dialog=2)
        self.send(5, text='', dialog=2)
        self.assertEqual(self.packet()[7], '-')

    def test_validation_and_browser_ownership(self):
        with self.assertRaises(ValueError): self.send(1, mask=0x10000)  # HOME excluded
        with self.assertRaises(ValueError): self.send(2, x=256)
        self.send(3, mask=1)
        with self.assertRaises(ValueError): self.r.submit(dict(owner='other-browser', serial=1, mask=8))
        self.now.return_value = 102.1
        self.r.submit(dict(owner='other-browser', serial=1, mask=8))
        self.now.return_value = 110
        with self.assertRaises(ValueError): self.send(4, mask=1)
