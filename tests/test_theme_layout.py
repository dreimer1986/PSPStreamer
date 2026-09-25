"""Focused measured-skin and compact translated sidebar checks."""
import ast
import re
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]/'psp-client'

class ThemeLayoutTests(unittest.TestCase):
    def test_measured_edges_are_exclusive(self):
        text=(ROOT/'theme_layout.h').read_text()
        values=dict((k,int(v)) for k,v in re.findall(r'((?:LCD|TV)_(?:LEFT|RIGHT)_[XYRB])=(\d+)',text))
        for name,expected in [('LCD_LEFT',(31,27,351,155)),('LCD_RIGHT',(372,27,450,157)),
                              ('TV_LEFT',(25,59,535,293)),('TV_RIGHT',(557,59,693,294))]:
            self.assertEqual(tuple(values[name+'_'+c] for c in 'XYRB'),expected)

    def test_fixed_sidebar_messages_fit_ten_lcd_cells(self):
        keys='PLEASE_WAIT NO_VIDEO STREAM_HAS STARTED SELECTED WAITING FOLDER MUSIC VIDEO AVC_READY AVC_ERROR STREAMS NO_TRACKS QUALITY SAVED_FOR NEXT_MUSIC STREAM_BANG AUDIO_SUB NEXT_PLAY BACK PRESET_LIMIT'.split()
        for lang in ['en','de']:
            strings={k:ast.literal_eval(v) for k,v in re.findall(r'\[(TXT_\w+)\]\s*=\s*("(?:[^"\\]|\\.)*")',(ROOT/f'lang_{lang}.h').read_text())}
            for key in keys:
                self.assertLessEqual(len(strings['TXT_'+key]),10,(lang,key,strings['TXT_'+key]))
            self.assertLessEqual(len(strings['TXT_ENTRIES']%9999),10)
