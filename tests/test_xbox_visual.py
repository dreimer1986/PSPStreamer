"""Focused Xbox adapter checks; no preset-collection or unrelated server sweep."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class XboxVisualTests(unittest.TestCase):
    def test_fullscreen_skips_hidden_menu(self):
        main=(ROOT/'xbox-client/player_main.c').read_text()
        draw=main.split('static void menu_draw(void){',1)[1].split('static void frame_draw_software',1)[0]
        self.assertEqual(draw.count('menu_render()'),1)
        self.assertIn('}else menu_render();',draw)
        self.assertIn('SDL_UpdateTexture(visual_texture,&changed',
                      (ROOT/'xbox-client/visual_ui.h').read_text())
        teardown=(ROOT/'xbox-client/visual_pbkit.c').read_text().split('void pb_kill(void) {',1)[1]
        self.assertLess(teardown.index('visual_pb_kill_inner();'),teardown.index('XVideoSetFB'))
        self.assertIn('XVideoSetVideoEnable(TRUE)',teardown)

    def test_shared_effect_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'effects'
            shared=['milkdrop_warp','milkdrop_preset','preset_math','milkdrop_signal',
                    'milkdrop_wave','milkdrop_wave_extra','milkdrop_decor','milkdrop_texture','cave_visual','cave_paths']
            subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                '-I'+str(ROOT/'tests/xbox_visual_stubs'),'-I'+str(ROOT/'.toolchain/nxdk/lib/pbkit'),
                '-I'+str(ROOT/'xbox-client'),'-I'+str(ROOT/'psp-client'),
                str(ROOT/'tests/xbox_visual_effects.c'),str(ROOT/'xbox-client/generated/visual_logo.c'),
                *[str(ROOT/'psp-client'/(s+'.c')) for s in shared],
                '-lpng','-ljpeg','-lz','-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary),str(ROOT/'psp-client/presets/active.milk'),
                str(ROOT/'psp-client/presets/custom-wave-demo.milk')],cwd=directory,check=True,timeout=20)

    def test_command_adapter(self):
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'visual'
            subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                '-I'+str(ROOT/'tests/xbox_visual_stubs'),
                '-I'+str(ROOT/'.toolchain/nxdk/lib/pbkit'),
                '-I'+str(ROOT/'xbox-client'),'-I'+str(ROOT/'psp-client'),
                str(ROOT/'tests/xbox_visual_gpu.c'),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=15)

    def test_source_adapter_and_panel_isolation(self):
        subprocess.run(['python3',str(ROOT/'xbox-client/tools/visual_adapter.py')],check=True)
        source=(ROOT/'xbox-client/generated/visual_effects.c').read_text()
        self.assertNotIn('0x44000000',source)
        self.assertNotIn('0x04000000 +',source)
        self.assertNotIn('<pspgu.h>',source)
        self.assertIn('md_live_evaluate',source)
        self.assertIn('cave_draw_combat()',source)
        ui=(ROOT/'xbox-client/visual_ui.h').read_text()
        self.assertIn('PANEL_VISUAL=9,PANEL_EFFECT=10,PANEL_PRESETS=11',ui)
        main=(ROOT/'xbox-client/player_main.c').read_text()
        self.assertIn('else if(panel==8)',main) # Existing library actions.
        self.assertIn('xbox_visual_stop()', (ROOT/'xbox-client/player.h').read_text())

if __name__=='__main__':unittest.main()
