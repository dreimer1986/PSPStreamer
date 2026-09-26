'use strict';
let playlistState={revision:0,enabled:false,items:[]},playlistBusy=false;
const playlistPanel=document.createElement('section');playlistPanel.id='view-playlist';playlistPanel.className='view panel';playlistPanel.hidden=true;
const playlistHeading=document.createElement('h2');playlistHeading.textContent=t('Playlist');
const playlistMode=document.createElement('input');playlistMode.type='checkbox';
const playlistLabel=document.createElement('label');playlistLabel.append(playlistMode,t('Use playlist instead of folder order'));
const playlistRows=document.createElement('div'),playlistNote=document.createElement('p');
playlistNote.textContent=t('Saved on the server. Add from the library or media details. Each media item appears once.');
playlistPanel.append(playlistHeading,playlistLabel,playlistNote,playlistRows);$('#view-library').after(playlistPanel);
const playlistNav=button(t('Playlist'),()=>{setView('playlist');loadPlaylist().catch(fail)});playlistNav.dataset.view='playlist';$('nav').append(playlistNav);
async function loadPlaylist(){const state=await api('/api/playlist');if(state.revision>=playlistState.revision){playlistState=state;renderPlaylist();}}
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
}));playlistNote.after(playlistTools);
async function addToPlaylist(items){await loadPlaylist();await changePlaylist({action:'add',items});message(t('Added to playlist'));}
function renderPlaylist(){
 playlistRows.replaceChildren();playlistMode.checked=playlistState.enabled;
 queueRepeat.value=playlistState.repeat||0;queueShuffle.checked=!!playlistState.shuffle;
 for(const [index,item] of playlistState.items.entries()){
  const row=document.createElement('div');row.className='item';
  const label=document.createElement('span');label.textContent=`${index+1}. ${item.name}${activePlayer(playerSample)&&playerSample.id===item.id?' — '+t('Now playing'):''}`;
  const play=button(t('Play'),()=>changePlaylist({action:'play',id:item.id}));
  const up=button('↑',()=>changePlaylist({action:'move',id:item.id,position:index-1}));up.disabled=index===0;up.title=t('Move up');
  const down=button('↓',()=>changePlaylist({action:'move',id:item.id,position:index+1}));down.disabled=index===playlistState.items.length-1;down.title=t('Move down');
  row.append(label,play,up,down,button(t('Remove'),()=>changePlaylist({action:'remove',id:item.id})));playlistRows.append(row);
 }
 if(!playlistState.items.length)playlistRows.textContent=t('No entries yet.');
}
const playlistLibraryRender=renderLibrary;
renderLibrary=function(){
 playlistLibraryRender();if(!listing||path===':queue:')return;
 const rows=Array.from($('#library').children).slice(listing.folders.length);
 listing.videos.forEach((item,index)=>{if(!item.live&&!item.id.startsWith('radio.'))rows[index]?.append(button(t('Add to playlist'),()=>addToPlaylist([item])));});
};
$('#prepareSelected').after(button(t('Add selected to playlist'),()=>addToPlaylist([...checked.values()])));
$('#play').after(button(t('Add to playlist'),()=>{
 if(!selected||!media||selected.live)return;
 return addToPlaylist([{...selected,audio:selected.kind==='audio'?0:(+$('#audio').value||0),subtitle:selected.kind==='audio'||$('#subtitle').value===''?-1:+$('#subtitle').value}]);
}));
setInterval(()=>{if(view==='playlist'&&!playlistBusy)loadPlaylist().catch(fail)},3000);
