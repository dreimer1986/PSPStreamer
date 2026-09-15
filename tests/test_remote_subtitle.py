import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RemoteSubtitleTests(unittest.TestCase):
    def test_browser_hides_music_tracks_and_restores_video_tracks(self):
        html=(ROOT / "static/index.html").read_text()
        choose=next(line for line in html.splitlines() if line.startswith("async function choose("))
        addon=(ROOT / "psp_streamer_addon/rootfs/app/static/index.html").read_text()
        self.assertIn(choose,addon)
        script="""
const assert=require('assert'); let selected;
const elements={};
function $(key){return elements[key]||(elements[key]={parentElement:{style:{}}})}
const document={querySelectorAll:()=>[]};
const button={classList:{add:()=>{}}};
async function api(){return {a:[],s:[],d:60}}
function option(){}
"""+choose+"""
(async()=>{
 await choose({id:'music',name:'Music',kind:'audio'},button);
 assert.strictEqual($('#audio').parentElement.style.display,'none');
 assert.strictEqual($('#subtitle').parentElement.style.display,'none');
 await choose({id:'video',name:'Video',kind:'video'},button);
 assert.strictEqual($('#audio').parentElement.style.display,'');
 assert.strictEqual($('#subtitle').parentElement.style.display,'');
})().catch(e=>{console.error(e);process.exit(1)});
"""
        subprocess.run(["node","-e",script],check=True,timeout=5)

    def test_actual_browser_command_preserves_first_subtitle(self):
        html = (ROOT / "static/index.html").read_text()
        command = next(line for line in html.splitlines() if line.startswith("async function command("))
        addon=(ROOT / "psp_streamer_addon/rootfs/app/static/index.html").read_text()
        self.assertEqual(command,next(line for line in addon.splitlines() if line.startswith("async function command(")))
        script = """
const assert=require('assert');
let selected={id:'example'}, sent;
const elements={'#subtitle':{value:'0'},'#audio':{value:'0'},'#seek':{value:'0'},'#status':{},'#audio_quality':{value:'v5'},'#video_fps':{value:'24000/1001'}};
function $(id){return elements[id]}
async function api(path,options){sent=JSON.parse(options.body)}
""" + command + """
(async()=>{
 for(const value of ['-1','0','1','7','']){
  elements['#subtitle'].value=value;
  await command('play');
  assert.strictEqual(sent.subtitle,value===''?-1:Number(value));
  assert.strictEqual(sent.audio_quality,'v5');
  assert.strictEqual(sent.video_fps,'24000/1001');
 }
 selected.kind='audio'; elements['#audio'].value='3'; elements['#subtitle'].value='7';
 await command('play');
 assert.strictEqual(sent.audio,0); assert.strictEqual(sent.subtitle,-1);
})().catch(e=>{console.error(e);process.exit(1)});
"""
        subprocess.run(["node","-e",script],check=True,timeout=10)

    def test_watchdog_does_not_change_playback_timeouts_or_codecs(self):
        watch=(ROOT / "psp-client/video_watchdog.h").read_text()
        for forbidden in ("sceMpeg", "sceAudiocodec", "sceAudioOutput", "TerminateThread", "http_get"):
            self.assertNotIn(forbidden,watch)
        self.assertIn("now-heartbeat>=8000",watch)
        self.assertLess(watch.index("unsigned int heartbeat=video_watch_tick;"),
                        watch.index("unsigned int now=video_watch_now();"))
        self.assertIn("reports<4",watch)
        source=(ROOT / "psp-client/main.c").read_text()
        self.assertIn('video_watch_ping("stop: join remote HTTP")',source)
        self.assertIn('video_watch_ping("AVC decode/CSC")',source)
        self.assertIn("video_watch_stop();",source)


if __name__ == "__main__":
    unittest.main()
