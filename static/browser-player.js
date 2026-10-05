// This tab owns its player. Never post browser controls to the PSP remote queue.
const browserTarget=document.createElement('select');
browserTarget.id='playbackTarget';browserTarget.setAttribute('aria-label',t('Playback target'));
option(browserTarget,'psp','PSP');option(browserTarget,'browser',t('This browser'));option(browserTarget,'xbox','Xbox');
const targetLabel=document.createElement('label');targetLabel.append(t('Playback target'),browserTarget);
$('nav').after(targetLabel);
const browserPanel=document.createElement('section');browserPanel.className='panel';browserPanel.hidden=true;
browserPanel.id='browserPlayer';
const browserTitle=document.createElement('h2'),browserVideo=document.createElement('video');
browserVideo.controls=true;browserVideo.playsInline=true;browserVideo.preload='none';
browserVideo.style.cssText='width:100%;max-height:70vh;background:#000';
const browserSeek=document.createElement('input');browserSeek.type='range';browserSeek.min=0;browserSeek.step=1;
browserSeek.setAttribute('aria-label',t('Position'));
const browserTime=document.createElement('output'),browserNote=document.createElement('p');
browserNote.textContent=t('Seek with this slider. Playback progress is reported to your media server; PSP controls and playlist stay independent.');
const browserStop=button(t('Stop'),()=>stopBrowser());
browserPanel.append(browserTitle,browserVideo,browserSeek,browserTime,browserStop,browserNote);
const browserNext=document.createElement('input');browserNext.type='checkbox';browserNext.checked=true;
const browserNextLabel=document.createElement('label');browserNextLabel.append(browserNext,t('Automatically play next episode / track'));browserPanel.append(browserNextLabel);
const browserPreviousButton=button(t('Previous'),()=>browserAdjacent(true).catch(fail));
const browserNextButton=button(t('Next'),()=>browserAdjacent(false).catch(fail));
browserPanel.append(browserPreviousButton,browserNextButton);
// Stay visible while browsing another folder, so playback is always controllable.
$('nav').after(browserPanel);
let browserItem=null,browserOffset=0,browserGeneration=0,browserPaused=false;
const browserClient='browser-'+(globalThis.crypto?.randomUUID?.()||Date.now().toString(36)+'-'+Math.random().toString(36).slice(2));
let browserReportTime=0,browserReports=Promise.resolve(),browserReportSequence=0;
function reportBrowser(state,unloading=false){
  if(!browserItem)return;
  const body=JSON.stringify({client:browserClient,sequence:++browserReportSequence,id:browserItem.id,name:browserItem.name,
    kind:browserItem.kind,state,position:Math.round(browserPosition()*1000),
    duration:Math.round((browserItem.duration||0)*1000)});
  const send=()=>fetch('/api/client-playback',{method:'POST',credentials:'same-origin',keepalive:true,
    headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},body,
    signal:AbortSignal.timeout(5000)}).catch(()=>{});
  if(unloading)send();else browserReports=browserReports.then(send,send);
  browserReportTime=Date.now();
}
function browserPosition(){return browserOffset+(Number.isFinite(browserVideo.currentTime)?browserVideo.currentTime:0);}
function stopBrowser(unloading=false){
  reportBrowser('stopped',unloading);
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
  if(browserItem&&!browserVideo.paused&&!browserVideo.ended&&browserVideo.readyState>=2&&Date.now()-browserReportTime>=15000)reportBrowser('playing');
};
browserVideo.onplaying=()=>reportBrowser('playing');
browserVideo.onended=()=>{reportBrowser('stopped');if(browserNext.checked&&browserItem&&!browserItem.live)browserAdjacent(false).catch(fail);};
browserVideo.onerror=()=>{if(browserItem){reportBrowser('stopped');message(t('Browser playback failed. Check available transcode slots and the server log.'));}};
// Release FFmpeg on pause instead of holding a blocked socket/process indefinitely.
browserVideo.onpause=()=>{
  if(!browserItem||browserPaused||browserVideo.ended||!browserVideo.paused||browserVideo.readyState<2)return;
  const item=browserItem,position=browserPosition();
  reportBrowser('paused');
  browserGeneration++;browserItem=null;browserVideo.removeAttribute('src');browserVideo.load();
  browserItem=item;browserOffset=position;browserPaused=true;
};
const browserResume=button(t('Resume'),()=>{if(browserItem)return startBrowser(browserItem,browserOffset);});
browserStop.before(browserResume);
browserSeek.onchange=()=>{if(browserItem)startBrowser(browserItem,+browserSeek.value).catch(fail);};
browserTarget.onchange=()=>{
  xboxQualityVisibility();
  $('#play').textContent=t(browserTarget.value==='browser'?'Play in browser':browserTarget.value==='xbox'?'Play on Xbox':'Play on PSP');
  $('#pspController').hidden=browserTarget.value!=='psp';
  if(!selected)$('#title').textContent=browserTarget.value==='xbox'?'Xbox':t('PSP controls');
  playerSample=null;followPlayer=false;pendingSeek=null;remotePlaying=false;
  try{localStorage.setItem('playbackTarget',browserTarget.value);}catch(e){}
  refreshPlayer(true).catch(fail);
};
// Following a PSP that changes episodes must not replace this tab's selection.
const pspRefreshPlayer=refreshPlayer;
let xboxRefresh=null;
refreshPlayer=async function(adopt=false){
  if(browserTarget.value!=='xbox')return pspRefreshPlayer(browserTarget.value==='browser'?false:adopt);
  if(!xboxRefresh)xboxRefresh=api('/api/xbox/status').finally(()=>{xboxRefresh=null;});
  const state=await xboxRefresh;if(browserTarget.value!=='xbox')return;
  return applyPlayerStatus({...state,...state.metadata,title:state.title||state.metadata?.title,position:(state.position||0)/1000,duration:(state.duration||0)/1000},adopt);
};
nowPlayingOpen.onclick=()=>openCurrentPlayback().catch(fail);
const pspCommand=command;
command=async function(action,extra={}){
  if(browserTarget.value==='xbox'){
    let body={action,...extra};
    if(action==='play'){
      if(!selected||!media)return;
      Object.assign(body,{id:selected.id,audio:selected.kind==='audio'?0:(+$('#audio').value||0),
        subtitle:selected.kind==='audio'?-1:($('#subtitle').value===''?-1:+$('#subtitle').value),start:+$('#seek').value||0});
      if(selected.kind!=='audio'&&xboxQuality.value)body.xbox_size=xboxQuality.value;
      if(xboxAudio.value)body.xbox_audio=xboxAudio.value;
    }
    await post('/api/xbox/command',body);
    if(action==='play'&&selected?.id===body.id){seekEditing=false;followPlayer=true;remotePlaying=true;pendingSeek={id:body.id,seconds:body.start,sent:performance.now(),until:performance.now()+15000};}
    if(action==='stop'){remotePlaying=false;playerSample=null;pendingSeek=null;nowPlaying.hidden=true;}
    message(t('Sent: {action}',{action:t(action)}));return;
  }
  if(browserTarget.value!=='browser'){
    if(action==='next'||action==='previous'){
      // PSP's proven wire protocol has no Next/Previous command. Resolve the
      // shared queue/folder just like its HA entity, then use normal Play.
      await refreshPlayer(false);
      if(browserTarget.value!=='psp'||!activePlayer(playerSample)||playerSample.live)return;
      const current=playerSample.id;
      const next=await api('/api/media-next/'+encodeURIComponent(current)+'?manual=1&direction='+action);
      if(browserTarget.value!=='psp'||playerSample.id!==current)return;
      if(!next.id){message(t('No adjacent file.'));return;}
      await choose(next);
      if(browserTarget.value!=='psp'||selected?.id!==next.id)return;
      for(const [key,selector] of [['audio','#audio'],['subtitle','#subtitle'],['audio_quality','#audio_quality'],['video_fps','#video_fps']])
        if(next[key]!==undefined&&[...$(selector).options].some(o=>o.value===String(next[key])))$(selector).value=next[key];
      $('#seek').value=0;
      return pspCommand('play');
    }
    return pspCommand(action,extra);
  }
  if(action==='play'&&selected&&media)return startBrowser({id:selected.id,name:selected.name,
    kind:selected.kind||'video',live:!!selected.live,duration:+media.d||0,
    audio:selected.kind==='audio'?0:(+$('#audio').value||0),
    subtitle:selected.kind==='audio'?-1:($('#subtitle').value===''?-1:+$('#subtitle').value),
    audioPreference:media.a?.[+$('#audio').value||0],subtitlePreference:$('#subtitle').value===''?null:media.s?.[+$('#subtitle').value],
    quality:$('#audio_quality').value},+$('#seek').value||0);
  if(action==='stop')stopBrowser();
  if(action==='pause')browserVideo.pause();
  if(action==='resume'&&browserItem)return startBrowser(browserItem,browserOffset);
  if(action==='seek'&&browserItem)return startBrowser(browserItem,+extra.seconds||0);
  if(action==='previous'||action==='next')return browserAdjacent(action==='previous');
};
const pspSeekTo=seekTo;
seekTo=async function(seconds){
  if(browserTarget.value==='xbox')return pspSeekTo(seconds);
  if(browserTarget.value!=='browser')return pspSeekTo(seconds);
  $('#seek').value=seconds;$('#seekLabel').textContent=timeLabel(seconds);
  if(browserItem&&selected?.id===browserItem.id)return startBrowser(browserItem,seconds);
};
const pspRenderPosition=renderPosition;
renderPosition=function(){if(browserTarget.value!=='browser')pspRenderPosition();};
window.addEventListener('pagehide',()=>stopBrowser(true));
// One common control panel. Only the transport changes with the target.
for(const [label,action] of [['Previous','previous'],['Next','next']])
  $('#view-remote .controls').append(button(t(label),()=>command(action).catch(fail)));
const xboxQualityField=document.createElement('label'),xboxQuality=document.createElement('select');
xboxQualityField.id='xboxQualityField';xboxQuality.id='xboxVideoQuality';
const xboxQualityTitle=document.createElement('span');xboxQualityTitle.textContent=t('Xbox video resolution');
xboxQualityField.append(xboxQualityTitle,xboxQuality);$('#mediaOptions .fields').append(xboxQualityField);
for(const [value,label] of [['',t('Use Xbox setting')],['480p-low','480×272'],['360p','640×360'],['480p','720×480 (16:9)'],['576p','720×576 (16:9)'],['720p','1280×720'],['1080p','1920×1080 (HD)']])option(xboxQuality,value,label);
const xboxQualityHelp=document.createElement('small');xboxQualityHelp.textContent=t('Applies on next Play. TV output mode is set on the Xbox.');xboxQualityField.append(xboxQualityHelp);
try{const saved=localStorage.getItem('xboxVideoQuality');if([...xboxQuality.options].some(o=>o.value===saved))xboxQuality.value=saved;}catch(e){}
xboxQuality.onchange=()=>{try{localStorage.setItem('xboxVideoQuality',xboxQuality.value);}catch(e){}};
const xboxAudioField=document.createElement('label'),xboxAudio=document.createElement('select');
xboxAudioField.id='xboxAudioField';xboxAudio.id='xboxAudioQuality';xboxAudioField.append('Xbox MP2 (48 kHz)',xboxAudio);
$('#mediaOptions .fields').append(xboxAudioField);
for(const value of ['', '128k','192k','256k','320k','384k'])option(xboxAudio,value,value||t('Use Xbox setting'));
try{const saved=localStorage.getItem('xboxAudioQuality');if([...xboxAudio.options].some(o=>o.value===saved))xboxAudio.value=saved;}catch(e){}
xboxAudio.onchange=()=>{try{localStorage.setItem('xboxAudioQuality',xboxAudio.value);}catch(e){}};
function xboxQualityVisibility(){
  const xbox=browserTarget.value==='xbox',audio=selected?.kind==='audio';
  xboxQualityField.hidden=!xbox||!selected||audio;
  xboxAudioField.hidden=!xbox||!selected;
  $('#audio_quality').closest('label').hidden=xbox;
  $('#fpsField').hidden=xbox||audio;
  // Download quality belongs to server preparation, not Xbox playback.
}
const xboxChooseMedia=choose;
choose=async function(...args){try{return await xboxChooseMedia(...args);}finally{xboxQualityVisibility();}};
try{const saved=localStorage.getItem('playbackTarget');if(['psp','browser','xbox'].includes(saved))browserTarget.value=saved;}catch(e){}
browserTarget.onchange();
async function browserAdjacent(previous){
  if(!browserItem||browserItem.live)return;
  const item=browserItem,generation=browserGeneration;
  const next=await api('/api/media-next/'+encodeURIComponent(item.id)+'?folder=1&direction='+(previous?'previous':'next'));
  if(generation!==browserGeneration||browserItem!==item)return;
  if(!next.id){message(t('No adjacent file.'));return;}
  const meta=await api('/api/metadata/'+encodeURIComponent(next.id));
  if(generation!==browserGeneration||browserItem!==item)return;
  // Carry language/title preferences, not an index that means another language
  // in the next episode. Series-specific preferences take precedence.
  const match=(rows,track,fallback)=>{
    if(!track)return fallback;
    const code=s=>({eng:'en',ger:'de',deu:'de',jpn:'ja',fra:'fr',fre:'fr',spa:'es',ita:'it'}[s]||s);
    const choices=(rows||[]).filter(r=>code(r.l)===code(track.l));
    return +(choices.find(r=>r.t===track.t)||choices[0]||{n:fallback}).n;
  };
  const audio=meta.preferred_audio??match(meta.a,item.audioPreference,0);
  const subtitle=item.kind==='audio'?-1:(meta.preferred_subtitle??match(meta.s,item.subtitlePreference,-1));
  await startBrowser({...item,id:next.id,name:meta.name||next.name,kind:next.kind||item.kind,
    duration:+meta.d||0,audio,subtitle,audioPreference:meta.a?.[audio],subtitlePreference:meta.s?.[subtitle]},0);
}
