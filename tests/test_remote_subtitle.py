import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RemoteSubtitleTests(unittest.TestCase):
    def test_actual_browser_command_preserves_first_subtitle(self):
        html = (ROOT / "static/index.html").read_text()
        command = next(line for line in html.splitlines() if line.startswith("async function command("))
        addon=(ROOT / "psp_streamer_addon/rootfs/app/static/index.html").read_text()
        self.assertEqual(command,next(line for line in addon.splitlines() if line.startswith("async function command(")))
        script = """
const assert=require('assert');
let selected={id:'example'}, sent;
const elements={'#subtitle':{value:'0'},'#audio':{value:'0'},'#seek':{value:'0'},'#status':{}};
function $(id){return elements[id]}
async function api(path,options){sent=JSON.parse(options.body)}
""" + command + """
(async()=>{
 for(const value of ['-1','0','1','7','']){
  elements['#subtitle'].value=value;
  await command('play');
  assert.strictEqual(sent.subtitle,value===''?-1:Number(value));
 }
})().catch(e=>{console.error(e);process.exit(1)});
"""
        subprocess.run(["node","-e",script],check=True,timeout=10)

    def test_watchdog_does_not_change_playback_timeouts_or_codecs(self):
        watch=(ROOT / "psp-client/video_watchdog.h").read_text()
        for forbidden in ("sceMpeg", "sceAudiocodec", "sceAudioOutput", "TerminateThread", "http_get"):
            self.assertNotIn(forbidden,watch)
        self.assertIn("now-video_watch_tick>=8000",watch)
        self.assertIn("reports<4",watch)
        source=(ROOT / "psp-client/main.c").read_text()
        self.assertIn('video_watch_ping("stop: join remote HTTP")',source)
        self.assertIn('video_watch_ping("AVC decode/CSC")',source)
        self.assertIn("video_watch_stop();",source)


if __name__ == "__main__":
    unittest.main()
