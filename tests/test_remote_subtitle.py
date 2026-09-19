import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class RemoteSubtitleTests(unittest.TestCase):
    def test_browser_hides_music_tracks_and_restores_video_tracks(self):
        html=(ROOT / "static/web.css").read_text()
        # Author label/display rules otherwise override the browser's hidden
        # attribute, even though the JavaScript correctly sets hidden=true.
        self.assertIn('[hidden]{display:none!important}', html)
        source=(ROOT / 'static/app.js').read_text()
        choose=source[source.index('async function choose('):source.index('async function command(')]
        addon=(ROOT / "psp_streamer_addon/rootfs/app/static/app.js").read_text()
        self.assertIn(choose,addon)
        script="""
const assert=require('assert'); let selected,media,chooseGeneration=0,preferences={};
const elements={};
function $(key){return elements[key]||(elements[key]={replaceChildren:()=>{},append:()=>{}})}
const document={querySelectorAll:()=>[]};
const button={classList:{add:()=>{}}};
async function api(){return {a:[],s:[],d:60}}
function option(){}
function setView(){} function message(){} function restoreTrack(){return true} function t(s){return s}
function timeLabel(s){return String(s)}
"""+choose+"""
(async()=>{
 await choose({id:'music',name:'Music',kind:'audio'},button);
 assert.strictEqual($('#audioField').hidden,true);
 assert.strictEqual($('#subtitleField').hidden,true);
 assert.strictEqual($('#fpsField').hidden,true);
 await choose({id:'video',name:'Video',kind:'video'},button);
 assert.strictEqual($('#audioField').hidden,false);
 assert.strictEqual($('#subtitleField').hidden,false);
 assert.strictEqual($('#fpsField').hidden,false);
})().catch(e=>{console.error(e);process.exit(1)});
"""
        subprocess.run(["node","-e",script],check=True,timeout=5)

    def test_actual_browser_command_preserves_first_subtitle(self):
        html = (ROOT / "static/app.js").read_text()
        command = html[html.index('async function command('):html.index("action('#home'")]
        addon=(ROOT / "psp_streamer_addon/rootfs/app/static/app.js").read_text()
        self.assertIn(command,addon)
        script = """
const assert=require('assert');
let selected={id:'example'}, sent,media={},remotePlaying=false;
const elements={'#subtitle':{value:'0'},'#audio':{value:'0'},'#seek':{value:'0'},'#status':{},'#audio_quality':{value:'v5'},'#video_fps':{value:'24000/1001'}};
function $(id){return elements[id]}
async function post(path,options){sent=options}
function message(){} function t(s){return s}
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
