import unittest
from psp_streamer.timeline import chapters, plex_markers


class TimelineTests(unittest.TestCase):
    def test_container_chapters_sorted_and_bounded(self):
        rows = [{'start_time': '60.25', 'tags': {'title': 'Änderung'}},
                {'start_time': '0'}, {'start_time': '-1'}, {'start_time': 'nan'}, None]
        self.assertEqual(chapters(rows), [{'start': 0, 'title': ''}, {'start': 60.25, 'title': 'Änderung'}])
        self.assertEqual(chapters(None), [])

    def test_plex_millisecond_markers(self):
        rows = [{'type': 'intro', 'startTimeOffset': 10000, 'endTimeOffset': 30500},
                {'type': 'credits', 'startTimeOffset': 90000, 'endTimeOffset': 100000},
                {'type': 'intro', 'startTimeOffset': 20, 'endTimeOffset': 10},
                {'type': 'advertisement', 'startTimeOffset': 0, 'endTimeOffset': 1000},
                {'type': 'intro', 'startTimeOffset': 'nan', 'endTimeOffset': 1000}]
        self.assertEqual(plex_markers(rows), [{'type': 'intro', 'start': 10, 'end': 30.5},
                                            {'type': 'credits', 'start': 90, 'end': 100}])
        self.assertEqual(chapters([{'startTimeOffset': 1250, 'title': 'Start'}], 'startTimeOffset', 1000),
                         [{'start': 1.25, 'title': 'Start'}])
