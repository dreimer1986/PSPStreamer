import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch
from psp_streamer.xbox_remote import XboxRemote


class XboxRemoteTests(unittest.TestCase):
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
