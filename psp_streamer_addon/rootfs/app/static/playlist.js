'use strict';
let playlistState={revision:0,enabled:false,items:[]},playlistBusy=false;
let playlistDragging=null,playlistDurationBusy=false;
const playlistDurations=new Map();
const playlistPanel=document.createElement('section');playlistPanel.id='view-playlist';playlistPanel.className='view panel';playlistPanel.hidden=true;
const playlistHeading=document.createElement('h2');playlistHeading.textContent=t('Playlist');
const playlistMode=document.createElement('input');playlistMode.type='checkbox';
const playlistLabel=document.createElement('label');playlistLabel.append(playlistMode,t('Use playlist instead of folder order'));
const playlistRows=document.createElement('div'),playlistNote=document.createElement('p');
const playlistSummary=document.createElement('p');playlistSummary.setAttribute('aria-live','polite');
playlistNote.textContent=t('Saved on the server. Add from the library or media details. Each media item appears once.');
playlistPanel.append(playlistHeading,playlistLabel,playlistNote,playlistSummary,playlistRows);$('#view-library').after(playlistPanel);
const playlistNav=button(t('Playlist'),()=>{setView('playlist');loadPlaylist().catch(fail)});playlistNav.dataset.view='playlist';$('nav').append(playlistNav);
async function loadPlaylist(){const state=await api('/api/playlist');if(state.revision>=playlistState.revision&&!playlistDragging&&!playlistRows.contains(document.activeElement)){playlistState=state;renderPlaylist();}}
async function changePlaylist(change){
 if(playlistBusy)throw new Error(t('Playlist update in progress; try again'));playlistBusy=true;
 try{playlistState=await post('/api/playlist',{...change,revision:playlistState.revision});renderPlaylist();}
 catch(error){await loadPlaylist();throw error;}
 finally{playlistBusy=false;}
}
playlistMode.onchange=()=>changePlaylist({action:'enabled',enabled:playlistMode.checked}).catch(fail);
const queueRepeat=document.createElement('select');
for(const [value,label] of [[0,'Repeat off'],[1,'Repeat one'],[2,'Repeat all']])option(queueRepeat,value,t(label));
queueRepeat.setAttribute('aria-label',t('Playlist repeat'));
queueRepeat.onchange=()=>changePlaylist({action:'repeat',repeat:+queueRepeat.value}).catch(fail);
const queueShuffle=document.createElement('input');queueShuffle.type='checkbox';
const queueShuffleLabel=document.createElement('label');queueShuffleLabel.append(queueShuffle,t('Playlist shuffle (independent of folder shuffle)'));
queueShuffle.onchange=()=>changePlaylist({action:'shuffle',shuffle:queueShuffle.checked}).catch(fail);
playlistLabel.after(queueRepeat,queueShuffleLabel);
const playlistTools=document.createElement('div');playlistTools.className='actions';
for(const direction of [-1,1])playlistTools.append(button(t(direction<0?'Previous file':'Next file'),async()=>{
 await loadPlaylist();const index=playlistState.items.findIndex(item=>item.id===playerSample?.id);
 const item=index>=0?await api('/api/media-next/'+encodeURIComponent(playerSample.id)+'?manual=1&direction='+(direction<0?'previous':'next')):null;
 if(item?.id)await changePlaylist({action:'play',id:item.id});
}));
playlistTools.append(button(t('Clear playlist'),()=>{
 if(confirm(t('Clear playlist? Files are kept.')))return changePlaylist({action:'clear'});
}));
playlistTools.append(button(t('Retry unknown durations'),()=>{if(playlistDurationBusy)return;for(const [id,d] of playlistDurations)if(!Number.isFinite(d)||d<=0)playlistDurations.delete(id);return playlistLoadDurations();}));
playlistNote.after(playlistTools);
async function addToPlaylist(items){await loadPlaylist();await changePlaylist({action:'add',items});message(t('Added to playlist'));}
async function playNext(item){await loadPlaylist();await changePlaylist({action:'play_next',items:[item]});message(t('Scheduled next'));}
function playlistTime(seconds){const n=Math.round(seconds);return `${Math.floor(n/3600)}:${String(Math.floor(n/60)%60).padStart(2,'0')}:${String(n%60).padStart(2,'0')}`;}
function playlistSummaryUpdate(){
 let total=0,unknown=0;for(const row of playlistState.items){const d=playlistDurations.get(row.id);if(Number.isFinite(d)&&d>0)total+=d;else unknown++;}
 playlistSummary.textContent=`${t('Total (one pass)')}: ${playlistTime(total)}${unknown?' + '+unknown+' '+t('unknown durations'):''} · ${t('Next')}: ${!playlistState.enabled?t('Folder order'):playlistState.next?.name||t('No queued successor')}`;
}
async function playlistLoadDurations(){
 if(playlistDurationBusy||view!=='playlist')return;playlistDurationBusy=true;
 try{while(view==='playlist'){
  const row=playlistState.items.find(item=>!playlistDurations.has(item.id));if(!row)break;
  playlistDurations.set(row.id,null);
  try{const info=await api('/api/playlist/duration?id='+encodeURIComponent(row.id));playlistDurations.set(row.id,info.duration);}catch(_){/* Unknown duration never prevents playback. */}
  playlistSummaryUpdate();
 }}finally{playlistDurationBusy=false;}
}
function playlistDetailItem(){return {...selected,audio:selected.kind==='audio'?0:(+$('#audio').value||0),subtitle:selected.kind==='audio'||$('#subtitle').value===''?-1:+$('#subtitle').value,audio_quality:$('#audio_quality').value,...(selected.kind==='audio'?{}:{video_fps:$('#video_fps').value})};}
function renderPlaylist(){
 const ids=new Set(playlistState.items.map(item=>item.id));for(const id of playlistDurations.keys())if(!ids.has(id))playlistDurations.delete(id);
 playlistRows.replaceChildren();playlistMode.checked=playlistState.enabled;
 queueRepeat.value=playlistState.repeat||0;queueShuffle.checked=!!playlistState.shuffle;
 for(const [index,item] of playlistState.items.entries()){
  const row=document.createElement('div');row.className='item';
  const handle=document.createElement('span');handle.className='playlist-drag';handle.textContent='⠿';handle.draggable=true;handle.title=t('Drag to reorder');
  handle.addEventListener('dragstart',event=>{playlistDragging=item.id;event.dataTransfer.effectAllowed='move';event.dataTransfer.setData('text/plain',item.id);});
  handle.addEventListener('dragend',()=>{playlistDragging=null;});
  row.addEventListener('dragover',event=>{if(playlistDragging){event.preventDefault();event.dataTransfer.dropEffect='move';}});
  row.addEventListener('drop',event=>{event.preventDefault();const id=playlistDragging;playlistDragging=null;if(id&&id!==item.id)changePlaylist({action:'move',id,position:index}).catch(fail);});
  const label=document.createElement('span');label.className='playlist-title';label.textContent=`${index+1}. ${item.name}${activePlayer(playerSample)&&playerSample.id===item.id?' — '+t('Now playing'):''}`;
  const play=button(t('Play'),()=>changePlaylist({action:'play',id:item.id}));
  const up=button('↑',()=>changePlaylist({action:'move',id:item.id,position:index-1}));up.disabled=index===0;up.title=t('Move up');
  const down=button('↓',()=>changePlaylist({action:'move',id:item.id,position:index+1}));down.disabled=index===playlistState.items.length-1;down.title=t('Move down');
  const quality=document.createElement('select');option(quality,'',t('PSP default'));qualityOptions(quality);quality.value=item.audio_quality||'';quality.setAttribute('aria-label',t('Audio quality'));
  const fps=document.createElement('select');option(fps,'',t('PSP default'));option(fps,'20','20 fps');option(fps,'24000/1001','23.976 fps');fps.value=item.video_fps||'';fps.setAttribute('aria-label',t('Frame rate'));fps.hidden=item.kind==='audio';
  quality.title=t('Audio quality');fps.title=t('Frame rate');
  const saveQuality=()=>changePlaylist({action:'quality',id:item.id,audio_quality:quality.value,video_fps:item.kind==='audio'?'':fps.value}).catch(fail);
  quality.onchange=saveQuality;fps.onchange=saveQuality;
  row.append(handle,label,play,button(t('Play next'),()=>playNext(item)),quality,fps,up,down,button(t('Remove'),()=>changePlaylist({action:'remove',id:item.id})));playlistRows.append(row);
 }
 if(!playlistState.items.length)playlistRows.textContent=t('No entries yet.');
 playlistSummaryUpdate();playlistLoadDurations();
}
const playlistLibraryRender=renderLibrary;
renderLibrary=function(){
 playlistLibraryRender();if(!listing||path===':queue:')return;
 const rows=Array.from($('#library').children).slice(listing.folders.length);
 listing.videos.forEach((item,index)=>{if(!item.live&&!item.id.startsWith('radio.'))rows[index]?.append(button(t('Add to playlist'),()=>addToPlaylist([item])),button(t('Play next'),()=>playNext(item)));});
};
$('#prepareSelected').after(button(t('Add selected to playlist'),()=>addToPlaylist([...checked.values()])));
$('#play').after(button(t('Add to playlist'),()=>{
 if(!selected||!media||selected.live)return;
 return addToPlaylist([playlistDetailItem()]);
}));
setInterval(()=>{if(view==='playlist'&&!playlistBusy&&!playlistDragging)loadPlaylist().catch(fail)},3000);
$('#play').after(button(t('Play next'),()=>{
 if(!selected||!media||selected.live)return;
 return playNext(playlistDetailItem());
}));
