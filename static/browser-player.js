// This tab owns its player. Never post browser controls to the PSP remote queue.
const browserTarget=document.createElement('select');
browserTarget.id='playbackTarget';browserTarget.setAttribute('aria-label',t('Playback target'));
option(browserTarget,'psp','PSP');option(browserTarget,'browser',t('This browser'));
const targetLabel=document.createElement('label');targetLabel.append(t('Playback target'),browserTarget);
$('#play').before(targetLabel);
const browserPanel=document.createElement('section');browserPanel.className='panel';browserPanel.hidden=true;
browserPanel.id='browserPlayer';
const browserTitle=document.createElement('h2'),browserVideo=document.createElement('video');
browserVideo.controls=true;browserVideo.playsInline=true;browserVideo.preload='none';
browserVideo.style.cssText='width:100%;max-height:70vh;background:#000';
const browserSeek=document.createElement('input');browserSeek.type='range';browserSeek.min=0;browserSeek.step=1;
browserSeek.setAttribute('aria-label',t('Position'));
const browserTime=document.createElement('output'),browserNote=document.createElement('p');
browserNote.textContent=t('Seek with this slider. Browser playback is independent of the PSP; playlists and watched status stay unchanged.');
const browserStop=button(t('Stop'),()=>stopBrowser());
browserPanel.append(browserTitle,browserVideo,browserSeek,browserTime,browserStop,browserNote);
// Stay visible while browsing another folder, so playback is always controllable.
$('nav').after(browserPanel);
let browserItem=null,browserOffset=0,browserGeneration=0,browserPaused=false;
function browserPosition(){return browserOffset+(Number.isFinite(browserVideo.currentTime)?browserVideo.currentTime:0);}
function stopBrowser(){
  browserGeneration++;browserItem=null;browserVideo.pause();browserVideo.removeAttribute('src');browserVideo.load();
  browserPanel.hidden=true;browserPaused=false;
}
async function startBrowser(item,position=0){
  stopTheme();stopBrowser();browserItem=item;
  const generation=browserGeneration;
  browserOffset=item.live?0:Math.max(0,Math.min(position,item.duration||0));
  browserTitle.textContent=item.name;browserPanel.hidden=false;
  browserSeek.hidden=!!item.live;browserSeek.max=item.duration||0;browserSeek.value=browserOffset;
  const query=new URLSearchParams({kind:item.kind,audio:item.audio,subtitle:item.subtitle,
    audio_quality:item.quality,profile:'tv',start:browserOffset});
  browserVideo.src='/api/browser-stream/'+encodeURIComponent(item.id)+'?'+query;
  try{await browserVideo.play();}catch(error){if(generation===browserGeneration&&error.name!=='AbortError')fail(error);}
}
browserVideo.ontimeupdate=()=>{
  const position=browserPosition();browserTime.textContent=timeLabel(position)+' / '+timeLabel(browserItem?.duration||0);
  if(document.activeElement!==browserSeek)browserSeek.value=position;
};
browserVideo.onerror=()=>{if(browserItem)message(t('Browser playback failed. Check available transcode slots and the server log.'));};
// Release FFmpeg on pause instead of holding a blocked socket/process indefinitely.
browserVideo.onpause=()=>{
  if(!browserItem||browserPaused||browserVideo.ended||!browserVideo.paused||browserVideo.readyState<2)return;
  const item=browserItem,position=browserPosition();
  browserGeneration++;browserItem=null;browserVideo.removeAttribute('src');browserVideo.load();
  browserItem=item;browserOffset=position;browserPaused=true;
};
const browserResume=button(t('Resume'),()=>{if(browserItem)return startBrowser(browserItem,browserOffset);});
browserStop.before(browserResume);
browserSeek.onchange=()=>{if(browserItem)startBrowser(browserItem,+browserSeek.value).catch(fail);};
browserTarget.onchange=()=>{$('#play').textContent=t(browserTarget.value==='browser'?'Play in browser':'Play on PSP');};
// Following a PSP that changes episodes must not replace this tab's selection.
const pspRefreshPlayer=refreshPlayer;
refreshPlayer=async function(adopt=false){return pspRefreshPlayer(browserTarget.value==='browser'?false:adopt);};
nowPlayingOpen.onclick=()=>{browserTarget.value='psp';browserTarget.onchange();openCurrentPlayback().catch(fail);};
const pspCommand=command;
command=async function(action,extra={}){
  if(browserTarget.value!=='browser')return pspCommand(action,extra);
  if(action==='play'&&selected&&media)return startBrowser({id:selected.id,name:selected.name,
    kind:selected.kind||'video',live:!!selected.live,duration:+media.d||0,
    audio:selected.kind==='audio'?0:(+$('#audio').value||0),
    subtitle:selected.kind==='audio'?-1:($('#subtitle').value===''?-1:+$('#subtitle').value),
    quality:$('#audio_quality').value},+$('#seek').value||0);
  if(action==='stop')stopBrowser();
  if(action==='pause')browserVideo.pause();
  if(action==='resume'&&browserItem)return startBrowser(browserItem,browserOffset);
  if(action==='seek'&&browserItem)return startBrowser(browserItem,+extra.seconds||0);
};
const pspSeekTo=seekTo;
seekTo=async function(seconds){
  if(browserTarget.value!=='browser')return pspSeekTo(seconds);
  $('#seek').value=seconds;$('#seekLabel').textContent=timeLabel(seconds);
  if(browserItem&&selected?.id===browserItem.id)return startBrowser(browserItem,seconds);
};
const pspRenderPosition=renderPosition;
renderPosition=function(){if(browserTarget.value!=='browser')pspRenderPosition();};
window.addEventListener('pagehide',stopBrowser);
