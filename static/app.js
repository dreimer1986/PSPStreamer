'use strict';
const $=s=>document.querySelector(s);
let root=0,path='',selected=null,media=null,listing=null,csrf='',remotePlaying=false,preferences={},view='library';
let browseGeneration=0,chooseGeneration=0,preferenceWrites=Promise.resolve(),plexTimer,plexGeneration=0;
const checked=new Map();
let coverView=localStorage.getItem('psp-cover-view')==='1';
const coverToggle=button(t('Cover view'),()=>{coverView=!coverView;localStorage.setItem('psp-cover-view',coverView?'1':'0');renderLibrary();});
$('#refresh').after(coverToggle);
coverToggle.dataset.i18n='Cover view';
const backdrop=document.createElement('img');backdrop.className='media-backdrop';backdrop.alt='';backdrop.hidden=true;
const cover=document.createElement('img');cover.className='media-cover';cover.alt='';cover.hidden=true;
$('#view-remote').prepend(backdrop);$('#title').before(cover);
const themeAudio=document.createElement('audio');themeAudio.controls=true;themeAudio.preload='none';themeAudio.hidden=true;
const themeButton=button(t('Preview theme song'),async()=>{
  if(!selected)return;themeAudio.src='/api/theme/'+encodeURIComponent(selected.id);themeAudio.hidden=false;
  try{await themeAudio.play();}catch(e){if(e.name!=='AbortError')message(t('No playable theme song available.'));}
});
themeButton.dataset.i18n='Preview theme song';themeButton.hidden=true;$('#details').after(themeButton,themeAudio);
themeAudio.onerror=()=>{themeAudio.hidden=true;message(t('No playable theme song available.'));};
function stopTheme(){themeAudio.pause();themeAudio.removeAttribute('src');themeAudio.load();themeAudio.hidden=true;themeButton.hidden=true;}
function setArtwork(art={}){
  for(const [img,url] of [[cover,art.cover],[backdrop,art.backdrop]]){
    img.hidden=true;img.removeAttribute('src');
    if(typeof url==='string'&&url.startsWith('/api/artwork/')){
      img.onload=()=>img.hidden=false;img.onerror=()=>img.hidden=true;img.src=url;
    }
  }
}
function addCover(element,art){
  if(!coverView||!art?.cover?.startsWith('/api/artwork/'))return;
  const img=document.createElement('img');img.alt='';img.loading='lazy';img.decoding='async';img.src=art.cover;
  img.onerror=()=>img.remove();element.prepend(img);
}
function message(value){$('#status').textContent=value;}
function fail(error){if(error.name!=='AbortError')message(t(error.message));}
function action(selector,fn){$(selector).onclick=()=>Promise.resolve().then(fn).catch(fail);}
async function api(url,options={}){
  const response=await fetch(url,{...options,credentials:'same-origin',headers:{'X-PSP-Web':'1',...(options.method?{'Content-Type':'application/json','X-CSRF-Token':csrf}:{}),...options.headers}});
  if(response.status===401){location.assign('/login');throw Error(t('Please sign in'));}
  const data=await response.json();if(!response.ok)throw Error(t(data.error||String(response.status)));return data;
}
const post=(url,data={})=>api(url,{method:'POST',body:JSON.stringify(data)});
function option(select,value,text){const o=document.createElement('option');o.value=value;o.textContent=text;select.append(o);return o;}
function button(text,fn){const b=document.createElement('button');b.type='button';b.textContent=text;b.onclick=()=>Promise.resolve().then(fn).catch(fail);return b;}
function setView(name){view=name;if(name!=='remote')themeAudio.pause();document.querySelectorAll('.view').forEach(e=>e.hidden=e.id!=='view-'+name);document.querySelectorAll('[data-view]').forEach(e=>e.setAttribute('aria-current',String(e.dataset.view===name)));if(name==='downloads')updateDownloads().catch(fail);}
document.querySelectorAll('[data-view]').forEach(b=>b.onclick=()=>setView(b.dataset.view));
function timeLabel(seconds){return Math.floor(seconds/60)+':'+String(Math.floor(seconds%60)).padStart(2,'0');}
function selectionCount(){ $('#selectionCount').textContent=t('{n} selected',{n:checked.size});$('#prepareSelected').disabled=!checked.size;}
function folderLabel(folder){
  const system=[':files:',':plex:',':jellyfin:',':radio:',':plex:playlists',':jellyfin:playlists'].includes(folder.path)||
    ((folder.path.startsWith(':plex:')||folder.path.startsWith(':jellyfin:'))&&folder.path.includes('@')&&['Previous page','Next page'].includes(folder.name));
  return system?t(folder.name):folder.name;
}
function clearMedia(){chooseGeneration++;selected=media=null;remotePlaying=false;stopTheme();setArtwork();$('#title').textContent=t('PSP controls');$('#details').replaceChildren();$('#mediaOptions').hidden=true;$('#play').disabled=$('#queue').disabled=true;}
async function browse(next=''){
  const generation=++browseGeneration;message(t('Loading…'));
  const d=await api(`/api/library?root=${root}&path=${encodeURIComponent(next)}`);if(generation!==browseGeneration)return;
  root=d.root;path=d.path;listing=d;checked.clear();clearMedia();selectionCount();
  $('#crumb').textContent=path||t('Sources');$('#parent').disabled=d.parent===null;
  $('#batchTools').hidden=['',':files:',':plex:',':jellyfin:',':jellyfin:playlists',':radio:'].includes(path);
  renderLibrary();message('');
}
function renderLibrary(){
  if(!listing)return;const d=listing;
  const box=$('#library');box.replaceChildren();box.classList.toggle('cover-grid',coverView);coverToggle.setAttribute('aria-pressed',String(coverView));
  for(const f of d.folders){const b=button('▸ '+folderLabel(f),()=>browse(f.path));b.className='item folder';addCover(b,f.artwork);box.append(b);}
  for(const v of d.videos){const row=document.createElement('div');row.className='item';
    if(!v.live&&!v.id.startsWith('radio.')){const check=document.createElement('input');check.type='checkbox';check.checked=checked.has(v.id);check.setAttribute('aria-label',t('Select')+' '+v.name);check.onchange=()=>{if(check.checked)checked.set(v.id,v);else checked.delete(v.id);selectionCount()};row.append(check);}
    const b=button((v.kind==='audio'?'♫ ':'▶ ')+v.name,()=>choose(v));addCover(b,v.artwork);row.append(b);box.append(row);
  }
  if(!d.folders.length&&!d.videos.length)box.textContent=t('No files here.');message('');
}
function qualityOptions(select){for(const q of ['160k','128k','96k','v6','v5','v4','v3'])option(select,q,q.startsWith('v')?'VBR '+q.toUpperCase():'CBR '+parseInt(q)+' kbit/s');}
qualityOptions($('#audio_quality'));qualityOptions($('#batchQuality'));
function savePreferences(){const data={...preferences};preferenceWrites=preferenceWrites.then(()=>post('/api/offline/preferences',data)).catch(fail);}
for(const [id,key] of [['audio_quality','audio_quality'],['video_fps','video_fps'],['downloadProfile','profile'],['audio','audio'],['subtitle','subtitle']]){
  $('#'+id).onchange=()=>{const s=$('#'+id);preferences[key]=['audio','subtitle'].includes(key)?s.selectedOptions[0]?.textContent||'':s.value;savePreferences();};
}
async function choose(v){
  const generation=++chooseGeneration;selected=v;media=null;remotePlaying=false;setView('remote');
  stopTheme();
  setArtwork(v.artwork);
  $('#title').textContent=v.name;$('#details').textContent=t('Loading tracks…');$('#mediaOptions').hidden=true;
  $('#play').disabled=$('#queue').disabled=true;
  try{
    const d=await api('/api/metadata/'+encodeURIComponent(v.id));if(generation!==chooseGeneration)return;media=d;
    setArtwork(d.artwork);
    const audio=v.kind==='audio',live=!!v.live||v.id.startsWith('radio.');selected={...v,live};
    for(const id of ['audioField','subtitleField','fpsField','profileField'])$('#'+id).hidden=audio;
    $('#seekField').hidden=live;$('#queue').hidden=live;
    $('#audio').replaceChildren();$('#subtitle').replaceChildren();option($('#subtitle'),'-1',t('Off'));
    for(const a of d.a||[])option($('#audio'),a.n,`${a.l||'und'} ${a.t||''}`.trim());
    for(const s of d.s||[])option($('#subtitle'),s.n,`${s.l||'und'} ${s.t||''}`.trim());
    const a=restoreTrack($('#audio'),preferences.audio),s=restoreTrack($('#subtitle'),preferences.subtitle);
    $('#seek').max=Math.floor(+d.d||0);$('#seek').value=0;$('#seekLabel').textContent='0:00';
    $('#queue').textContent=t(audio?'Prepare MP3 download':'Convert for download');
    $('#details').textContent=[d.artist,d.album,d.title,d.year,+d.d>0?timeLabel(+d.d):'',d.summary].filter(Boolean).join(' · ')||t(live?'Live radio':audio?'Music stream':'Ready to play');
    if(d.provider==='plex'||d.provider==='jellyfin'){
      themeButton.hidden=audio;
      const state=document.createElement('p');state.textContent=t(d.watched?'Watched':'Unwatched');$('#details').append(state);
      if(d.resume>0)$('#details').append(button(t('Resume at {time}',{time:timeLabel(d.resume)}),()=>{$('#seek').value=d.resume;$('#seek').dispatchEvent(new Event('input'));}));
    }
    $('#mediaOptions').hidden=false;$('#play').disabled=$('#queue').disabled=false;
    message(!audio&&(!a||!s)?t('Preferred track unavailable; check the selection.'):'');
  }catch(error){if(generation===chooseGeneration){clearMedia();throw error;}}
}
async function command(action,extra={}){
  themeAudio.pause();
  if(action==='play'&&(!selected||!media))return;
  if(action==='seek'&&selected?.live)return;
  let body={action,...extra};
  if(action==='play')Object.assign(body,{id:selected.id,audio_quality:$('#audio_quality').value,video_fps:$('#video_fps').value,audio:selected.kind==='audio'?0:(+$('#audio').value||0),subtitle:selected.kind==='audio'?-1:($('#subtitle').value===''?-1:+$('#subtitle').value),start:selected.live?0:+$('#seek').value||0});
  await post('/api/remote/command',body);if(action==='play')remotePlaying=true;if(action==='stop')remotePlaying=false;
  message(t('Sent: {action}',{action:t(action)}));
}
action('#home',()=>browse(''));action('#parent',()=>listing?.parent!==null&&browse(listing.parent));action('#refresh',()=>browse(path));
action('#clearMedia',clearMedia);action('#play',()=>command('play'));
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>command(b.dataset.action).catch(fail));
$('#seek').oninput=()=>$('#seekLabel').textContent=timeLabel(+$('#seek').value);
$('#seek').onchange=()=>{if(remotePlaying)command('seek',{seconds:+$('#seek').value}).catch(fail)};
action('#logout',async()=>{await post('/api/logout');location.assign('/login')});
action('#queue',async()=>{if(!selected||!media||selected.live)return;
  const generation=chooseGeneration;$('#queue').disabled=true;
  try{await post('/api/offline/jobs',{id:selected.id,audio:selected.kind==='audio'?0:+$('#audio').value||0,subtitle:selected.kind==='audio'?-1:+$('#subtitle').value,audio_quality:$('#audio_quality').value,video_fps:$('#video_fps').value,profile:$('#downloadProfile').value});
    message(t('{n} jobs queued',{n:1}));setView('downloads');await updateDownloads();
  }finally{if(generation===chooseGeneration)$('#queue').disabled=false;}
});
action('#selectAll',()=>{$('#library').querySelectorAll('input[type=checkbox]').forEach(e=>{e.checked=true;e.dispatchEvent(new Event('change'))})});
action('#selectNone',()=>{$('#library').querySelectorAll('input[type=checkbox]').forEach(e=>{e.checked=false;e.dispatchEvent(new Event('change'))})});

let batchItems=[],batchRows=[],batchAbort=null,batchGeneration=0;
function invalidateBatch(){batchRows=[];$('#previewBatch').disabled=false;$('#commitBatch').disabled=true;$('#batchRows').replaceChildren();}
for(const id of ['batchAudio','batchSubtitle','batchQuality','batchFps','batchProfile'])$('#'+id).onchange=()=>{batchGeneration++;batchAbort?.abort();invalidateBatch();};
function trackChoice(select,tracks,subtitle){select.replaceChildren();option(select,subtitle?'off':'first',t(subtitle?'Off':'First audio track'));
  for(const track of tracks||[]){const wanted={language:trackLanguage(track.l),title:track.t||''};option(select,JSON.stringify(wanted),`${track.l||'und'} ${track.t||''}`.trim());}
}
async function beginBatch(items){
  if(!items.length)throw Error(t('Select one or more files first.'));if(items.length>128)throw Error(t('At most 128 files per batch.'));
  batchGeneration++;batchAbort?.abort();batchItems=items;invalidateBatch();$('#batch').hidden=false;setView('downloads');
  const musicOnly=items.every(item=>item.kind==='audio');
  for(const id of ['batchAudio','batchSubtitle','batchFps','batchProfile'])$('#'+id).parentElement.hidden=musicOnly;
  $('#batchStatus').textContent=t('Loading tracks…');$('#previewBatch').disabled=true;
  const generation=batchGeneration,controls=[...$('#batch').querySelectorAll('select')];let ready=false;
  controls.forEach(control=>control.disabled=true);
  try{const sample=items.find(x=>x.kind!=='audio')||items[0],d=await api('/api/metadata/'+encodeURIComponent(sample.id));if(generation!==batchGeneration)return;
    trackChoice($('#batchAudio'),d.a,false);trackChoice($('#batchSubtitle'),d.s,true);
    let restored=true;
    for(const [id,wanted] of [['batchAudio',preferences.audio],['batchSubtitle',preferences.subtitle]])if(!restoreTrack($('#'+id),wanted))restored=false;
    $('#batchQuality').value=$('#audio_quality').value;$('#batchFps').value=$('#video_fps').value;$('#batchProfile').value=$('#downloadProfile').value;
    $('#batchStatus').textContent=t('{n} selected',{n:items.length})+(!musicOnly&&!restored?' — '+t('Preferred track unavailable; check the selection.'):'');
    ready=true;
  }finally{if(generation===batchGeneration){$('#previewBatch').disabled=!ready;controls.forEach(control=>control.disabled=!ready);}}
}
action('#prepareSelected',()=>beginBatch([...checked.values()]));
action('#prepareFolder',async()=>{message(t('Loading…'));const d=await api(`/api/library/files?root=${root}&path=${encodeURIComponent(path)}&recursive=${$('#recursive').checked?1:0}`);await beginBatch(d.files);message('');});
function batchSelection(){return batchRows.filter(row=>row.check.checked&&row.options).map(row=>row.options);}
action('#previewBatch',async()=>{
  batchAbort?.abort();batchAbort=new AbortController();const signal=batchAbort.signal,generation=++batchGeneration;
  invalidateBatch();$('#previewBatch').disabled=true;
  const choice=id=>{const v=$('#'+id).value;return v.startsWith('{')?JSON.parse(v):v;};
  const audio=choice('batchAudio'),subtitle=choice('batchSubtitle');
  try{
    for(let i=0;i<batchItems.length;i++){
      if(signal.aborted||generation!==batchGeneration)return;
      $('#batchStatus').textContent=t('Checking {n}/{total}',{n:i+1,total:batchItems.length});
      const item=batchItems[i],row={item};
      try{const d=await api('/api/metadata/'+encodeURIComponent(item.id),{signal});if(signal.aborted||generation!==batchGeneration)return;
        const a=item.kind==='audio'?0:preferredTrack(d.a||[],audio),s=item.kind==='audio'?-1:preferredTrack(d.s||[],subtitle,true);
        if(a===null||s===null)throw Error(t('Missing or ambiguous preferred track'));
        row.options={id:item.id,audio:a,subtitle:s,audio_quality:$('#batchQuality').value,video_fps:$('#batchFps').value,profile:$('#batchProfile').value};
        const label=(tracks,n)=>n<0?t('Off'):(tracks.find(x=>+x.n===n)?.l||'und')+' '+(tracks.find(x=>+x.n===n)?.t||'');
        row.description=item.kind==='audio'?'MP3':`${t('Audio track')}: ${label(d.a||[],a)} · ${t('Subtitles')}: ${label(d.s||[],s)}`;
      }catch(e){if(e.name==='AbortError')return;row.error=e.message;}
      const label=document.createElement('label');row.check=document.createElement('input');row.check.type='checkbox';row.check.checked=!!row.options;row.check.disabled=!row.options;
      row.check.onchange=()=>$('#commitBatch').disabled=!batchSelection().length;
      label.append(row.check,document.createTextNode(' '+item.name+' — '+(row.error||row.description)));if(row.error)label.className='bad';$('#batchRows').append(label);batchRows.push(row);
    }
    const ready=batchSelection().length;$('#batchStatus').textContent=t('{n} ready; {errors} need attention',{n:ready,errors:batchRows.length-ready});$('#commitBatch').disabled=!ready;
  }finally{if(generation===batchGeneration)$('#previewBatch').disabled=false;}
});
action('#cancelBatch',()=>{batchGeneration++;batchAbort?.abort();invalidateBatch();$('#batch').hidden=true;});
action('#commitBatch',async()=>{const items=batchSelection();if(!items.length)return;$('#commitBatch').disabled=true;
  const saved={audio:$('#batchAudio').selectedOptions[0].textContent,subtitle:$('#batchSubtitle').selectedOptions[0].textContent,audio_quality:$('#batchQuality').value,video_fps:$('#batchFps').value,profile:$('#batchProfile').value};
  if(batchItems.every(item=>item.kind==='audio'))for(const key of ['audio','subtitle','video_fps','profile'])delete saved[key];
  const controls=[...$('#batch').querySelectorAll('button,select,input')];controls.forEach(control=>control.disabled=true);
  try{const d=await post('/api/offline/batch',{items});
    Object.assign(preferences,saved);savePreferences();
    for(const [id,key] of [['audio_quality','audio_quality'],['video_fps','video_fps'],['downloadProfile','profile']])if(saved[key])$('#'+id).value=saved[key];
    $('#batch').hidden=true;invalidateBatch();message(t('{n} jobs queued',{n:d.jobs.length}));await updateDownloads();
  }finally{controls.forEach(control=>control.disabled=false);batchRows.forEach(row=>row.check.disabled=!row.options);$('#commitBatch').disabled=!batchSelection().length;}
});
async function updateDownloads(){const jobs=await api('/api/offline/jobs'),box=$('#jobs');box.replaceChildren();if(!jobs.length)box.textContent=t('No conversion jobs.');
  for(const j of jobs){const row=document.createElement('article');row.className='job';const title=document.createElement('h3');title.textContent=j.name;const desc=document.createElement('p');desc.textContent=`${t(j.state)} · ${j.progress}% · ${(j.bytes/1048576).toFixed(1)} MiB · ${j.kind==='audio'?'MP3':j.profile+' / '+j.video_fps+' fps'} · ${j.audio_quality}`;row.append(title,desc);
    if(j.error){const error=document.createElement('p');error.className='bad';error.textContent=t(j.error);row.append(error);}
    if(j.state==='ready'){const link=document.createElement('a');link.href='/api/offline/export/'+encodeURIComponent(j.job);link.textContent=t('Download Memory Stick ZIP');row.append(link);}
    if(j.state==='encoding'&&j.eta!=null){const eta=document.createElement('p');eta.textContent=t('Remaining: {n} s',{n:j.eta});row.append(eta);}
    const active=['queued','encoding'].includes(j.state);row.append(button(t(active?'Cancel conversion':'Delete server copy'),async()=>{if(!confirm(t(active?'Cancel this conversion?':'Delete this server copy? PSP copies are kept.')))return;await post('/api/offline/'+(active?'cancel':'delete'),{job:j.job});await updateDownloads();}));box.append(row);
  }
}

function plexMapping(plex='',local=''){const row=document.createElement('div');row.className='mapping';for(const [label,value] of [['Plex directory',plex],['Mounted directory',local]]){const input=document.createElement('input');input.placeholder=t(label);input.setAttribute('aria-label',t(label));input.value=value;row.append(input);}row.append(button(t('Remove'),()=>row.remove()));$('#plexMappings').append(row);}
async function plexRefresh(){const s=await api('/api/plex');$('#sourceFiles').checked=s.files;$('#sourcePlex').checked=s.enabled;$('#sourceRadio').checked=s.radio;
  $('#plexMappings').replaceChildren();s.mappings.forEach(m=>plexMapping(m.plex,m.local));$('#plexStatus').textContent=(s.selected?t('Selected connection: {url}',{url:s.url}):t(s.linked?'Account linked; select a server.':'Plex not linked.'))+(s.report_error?' — '+s.report_error:'');}
async function plexServers(){const servers=await api('/api/plex/servers');$('#plexConnection').replaceChildren();for(const s of servers)for(const url of s.connections){const o=option($('#plexConnection'),url,s.name+' — '+url);o.dataset.server=s.id;}if(!$('#plexConnection').options.length)$('#plexStatus').textContent=t('No servers found.');}
action('#plexAddMapping',()=>plexMapping());action('#plexServers',plexServers);
action('#plexSelect',async()=>{const o=$('#plexConnection').selectedOptions[0];if(!o)throw Error(t('Choose a connection first.'));await post('/api/plex/select',{server:o.dataset.server,url:o.value});await plexRefresh();});
action('#plexDisconnect',async()=>{if(!confirm(t('Disconnect Plex and restore Files?')))return;plexGeneration++;clearTimeout(plexTimer);$('#plexAuth').hidden=true;await post('/api/plex/disconnect');await plexRefresh();await browse('');});
action('#plexLink',async()=>{clearTimeout(plexTimer);const generation=++plexGeneration,d=await post('/api/plex/link');$('#plexAuth').href=d.url;$('#plexAuth').hidden=false;$('#plexStatus').textContent=t('Authorize on Plex, then return here.');
  const poll=async()=>{if(generation!==plexGeneration)return;try{const d=await post('/api/plex/poll');if(d.linked){$('#plexAuth').hidden=true;await plexRefresh();await plexServers();}else plexTimer=setTimeout(poll,2000);}catch(e){fail(e)}};plexTimer=setTimeout(poll,2000);
});
async function jellyfinRefresh(){const s=await api('/api/jellyfin');$('#sourceJellyfin').checked=s.enabled;$('#jellyfinUrl').value=s.url;$('#jellyfinUser').value=s.username;$('#jellyfinStatus').textContent=(s.selected?t('Connected as {user}',{user:s.username}):t('Jellyfin not connected.'))+(s.report_error?' — '+s.report_error:'');}
$('#jellyfinForm').onsubmit=async event=>{event.preventDefault();try{await post('/api/jellyfin/login',{url:$('#jellyfinUrl').value,username:$('#jellyfinUser').value,password:$('#jellyfinPassword').value});$('#jellyfinPassword').value='';await jellyfinRefresh();await browse('');}catch(e){$('#jellyfinPassword').value='';fail(e)}};
action('#jellyfinDisconnect',async()=>{await post('/api/jellyfin/disconnect');await Promise.all([jellyfinRefresh(),plexRefresh()]);await browse('');});
$('#sourceForm').onsubmit=async event=>{event.preventDefault();try{const jf=$('#sourceJellyfin').checked;if(!jf&&!$('#sourceFiles').checked&&!$('#sourcePlex').checked&&!$('#sourceRadio').checked)throw Error(t('Keep at least one source enabled'));const mappings=[...document.querySelectorAll('.mapping')].map(row=>{const fields=row.querySelectorAll('input');return {plex:fields[0].value,local:fields[1].value};});if(jf)await post('/api/jellyfin/settings',{enabled:true});await post('/api/plex/settings',{files:$('#sourceFiles').checked,enabled:$('#sourcePlex').checked,radio:$('#sourceRadio').checked,mappings});if(!jf)await post('/api/jellyfin/settings',{enabled:false});await Promise.all([plexRefresh(),jellyfinRefresh()]);await browse('');message(t('Saved. Refresh the PSP library with Square.'));}catch(e){fail(e)}};
async function radioRefresh(){const stations=await api('/api/radio');$('#radioStations').replaceChildren();for(const s of stations){const row=document.createElement('div');row.className='job';const name=document.createElement('strong');name.textContent=s.name;row.append(name,button(t('Select'),()=>choose({...s,kind:'audio',live:true})),button(t('Edit'),()=>{for(const [id,key] of [['radioId','id'],['radioName','name'],['radioUrl','url']])$('#'+id).value=s[key];}),button(t('Delete'),async()=>{if(!confirm(t('Remove station?')))return;await post('/api/radio',{id:s.id,delete:true});if(selected?.id===s.id)clearMedia();await radioRefresh();}));$('#radioStations').append(row);}}
$('#radioForm').onsubmit=async event=>{event.preventDefault();try{await post('/api/radio',{id:$('#radioId').value,name:$('#radioName').value.trim(),url:$('#radioUrl').value.trim()});$('#radioForm').reset();await radioRefresh();message(t('Saved. Refresh the PSP library with Square.'));}catch(e){fail(e)}};
action('#radioClear',()=>$('#radioForm').reset());
async function passwordSettings(){const s=await api('/api/settings');$('#passwordForm').hidden=!s.password_editable;$('#oldPassword').required=s.password_set;$('#passwordInfo').textContent=t(s.password_editable?(s.password_set?'Password enabled.':'No password set; server is open.'):'Password is managed in Home Assistant / the server environment.');}
$('#passwordForm').onsubmit=async event=>{event.preventDefault();if($('#newPassword').value!==$('#repeatPassword').value){message(t('Passwords do not match.'));return;}
  try{await post('/api/settings/password',{current:$('#oldPassword').value,password:$('#newPassword').value});$('#passwordForm').reset();alert(t('Password changed. Sign in again and update the PSP.'));location.assign('/login');}catch(e){fail(e)}
};
async function poll(){try{if(view==='downloads')await updateDownloads();if(selected?.live&&view==='remote'){const item=selected,d=await api('/api/radio/status/'+encodeURIComponent(item.id));if(selected===item)$('#details').textContent=d.radio_active?[d.radio_station,d.radio_title||t('No title supplied')].join(' · '):t('Stopped / paused / reconnecting');}}catch(e){fail(e)}setTimeout(poll,3000);}
async function init(){const session=await api('/api/session');csrf=session.csrf;$('#logout').hidden=!session.protected;
  preferences=await api('/api/offline/preferences');for(const [id,key] of [['audio_quality','audio_quality'],['video_fps','video_fps'],['downloadProfile','profile']])if(preferences[key])$('#'+id).value=preferences[key];
  await browse('');await Promise.all([plexRefresh(),jellyfinRefresh(),radioRefresh(),passwordSettings()]);setView('library');poll();
}
init().catch(fail);
