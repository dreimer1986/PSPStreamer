import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch
from psp_streamer.xbox_remote import XboxRemote


class XboxRemoteTests(unittest.TestCase):
    def test_menu_buttons_text_ack_and_psp_isolation(self):
        remote=XboxRemote();server=SimpleNamespace(remote_sequence=91)
        for key in ('a','up','help','back'):
            reply=remote.send(server,dict(action='button',button=key))
            command=remote.poll(0,dialog=123,secret=True)
            self.assertEqual(command['button'],key)
            self.assertEqual(remote.poll(reply['sequence'],123,True),{})
            self.assertEqual(remote.snapshot()['acknowledged'],reply['sequence'])
        self.assertTrue(remote.snapshot()['secret'])
        self.assertEqual(remote.snapshot()['dialog'],123)
        remote.send(server,dict(action='text',text='Grüße',dialog=123))
        self.assertEqual(remote.poll(0)['text'],'Grüße')
        self.assertNotIn('text',remote.snapshot())
        for data in (dict(action='button',button='shell'),dict(action='text',text='x',dialog=0),dict(action='text',text='ü'*65,dialog=1),dict(action='text',text='x\ny',dialog=1)):
            with self.assertRaises(ValueError):remote.send(server,data)
        self.assertEqual(server.remote_sequence,91)

    def test_video_sizes_optional_validated_and_forwarded(self):
        remote=XboxRemote();server=SimpleNamespace(library=Mock(),radio=Mock())
        for size in ('480p-low','360p','480p','576p','720p','1080p'):
            remote.send(server,dict(action='play',id='test',xbox_size=size))
            self.assertEqual(remote.poll(0)['xbox_size'],size)
        for size in ('1080i','4k','',123,[]):
            with self.assertRaises(ValueError):remote.send(server,dict(action='play',id='test',xbox_size=size))
        remote.send(server,dict(action='play',id='test'))
        self.assertNotIn('xbox_size',remote.poll(0))

    def test_expiry_ack_latest_and_isolation(self):
        server=SimpleNamespace(library=Mock(),radio=Mock(),remote_sequence=12)
        remote=XboxRemote()
        with patch('psp_streamer.xbox_remote.time.monotonic',return_value=100):
            self.assertFalse(remote.snapshot()['online'])
            remote.send(server,dict(action='play',id='test',start=45,audio=1,subtitle=2))
            first=remote.poll(0)
            self.assertEqual(first['start'],45)
            self.assertEqual(first['subtitle'],2)
            self.assertEqual(remote.poll(first['sequence']),{})
            self.assertTrue(remote.snapshot()['online'])
            remote.send(server,dict(action='stop'))
            self.assertEqual(remote.poll(0)['action'],'stop')
        with patch('psp_streamer.xbox_remote.time.monotonic',return_value=116):
            self.assertEqual(remote.poll(0),{})
        self.assertEqual(server.remote_sequence,12)

    def test_validation_and_status(self):
        remote=XboxRemote();server=SimpleNamespace(library=Mock(),radio=Mock())
        for data in ([],{},dict(action='arbitrary'),dict(action='play',id=None),dict(action='seek',seconds='oops')):
            with self.assertRaises(ValueError):remote.send(server,data)
        remote.report(dict(state='paused',position=1000,duration=6000,kind='video'),'test','Title')
        self.assertEqual(remote.snapshot()['title'],'Title')
        self.assertEqual(remote.snapshot()['position'],1000)
