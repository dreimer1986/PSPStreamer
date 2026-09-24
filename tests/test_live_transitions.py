"""Only the live-transition renderer paths; no preset-collection sweep."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LiveTransitionTests(unittest.TestCase):
    def test_live_render_and_owned_lifetimes(self):
        adapter = (ROOT / 'psp-client/milkdrop_gu.c').read_text()
        adapter = re.sub(r'#include <psp\w+\.h>\n', '', adapter)
        harness = (ROOT / 'tests/milkdrop_harness.c').read_text().replace('/* GU_ADAPTER */', adapter)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder / 'live.c'
            source.write_text(harness)
            presets = []
            for index in range(2):
                preset = folder / f'live-{index}.milk'
                preset.write_text('[preset00]\nnWaveMode=0\nfWaveAlpha=.7\nfDecay=.95\n'
                                  f'per_frame_1=counter=counter+1;rot={index * .02};zoom=1+.01*sin(time);\n'
                                  'per_pixel_1=dx=.001*sin(rad+time);\n'
                                  'shapecode_0_enabled=1\nshapecode_0_sides=8\n'
                                  'shapecode_0_a=.5\nshapecode_0_border_a=0\n'
                                  'wavecode_0_enabled=1\nwavecode_0_samples=64\n'
                                  'wavecode_0_a=.6\nwavecode_0_bDrawThick=1\n'
                                  'wave_0_per_point1=x=sample;y=.5+.1*sin(sample*6+time);\n')
                presets.append(preset)
            units = ['milkdrop_warp', 'milkdrop_preset', 'preset_math', 'milkdrop_signal',
                     'milkdrop_wave', 'milkdrop_wave_extra', 'milkdrop_decor',
                     'milkdrop_texture', 'cave_visual', 'cave_paths']
            binary = folder / 'live'
            subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=undefined,float-cast-overflow', '-I', str(ROOT / 'psp-client'),
                            str(source), *[str(ROOT / 'psp-client' / (s + '.c')) for s in units],
                            '-lpng', '-ljpeg', '-lz', '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary), '--live', *map(str, presets)], check=True, timeout=20)
            # Static spectrum-only + PCM, dynamic primary-wave mode and custom
            # stereo-spectrum waves must still request the union of both inputs.
            for variant in ('nWaveMode=8\n',
                            'nWaveMode=0\nper_frame_2=wave_mode=if(above(sin(time),0),8,0);\n',
                            'nWaveMode=0\nwavecode_0_enabled=1\nwavecode_0_bSpectrum=1\n'):
                presets[0].write_text('[preset00]\nper_frame_1=counter=counter+1;\n' + variant)
                presets[1].write_text('[preset00]\nnWaveMode=0\nper_frame_1=counter=counter+1;\n')
                subprocess.run([str(binary), '--live', *map(str, presets)], check=True, timeout=20)
            demos = ROOT / 'psp-client/presets/live-transition-test'
            subprocess.run([str(binary), '--live', str(demos / '01 - Amber orbit.milk'),
                            str(demos / '02 - Cyan wave.milk')], check=True, timeout=20)
