'use strict';
const reservePanel=document.createElement('section');reservePanel.className='panel';
const reserveTitle=document.createElement('h2');reserveTitle.textContent=t('Episode reserve');
const reserveEnabled=document.createElement('input');reserveEnabled.type='checkbox';
const reserveLimit=document.createElement('input');reserveLimit.type='number';reserveLimit.min=128;reserveLimit.max=1048576;reserveLimit.value=4096;
const reserveLabel=document.createElement('label');reserveLabel.append(reserveEnabled,t('Automatically prepare unwatched episodes'));
const reserveLimitLabel=document.createElement('label');reserveLimitLabel.append(t('Server cache limit (MiB)'),reserveLimit);
const reserveNote=document.createElement('p');reserveNote.textContent=t('Server packages only. Download via ZIP or PSP Server queue. Only automatic cache copies are cleaned; manual jobs and PSP files are kept.');
const reserveStatus=document.createElement('p'),reserveRules=document.createElement('div');
reservePanel.append(reserveTitle,reserveLabel,reserveLimitLabel,
 button(t('Save'),()=>changeReserve({action:'settings',enabled:reserveEnabled.checked,limit_mib:Number(reserveLimit.value)})),
 button(t('Refresh'),()=>changeReserve({action:'refresh'})),reserveNote,reserveStatus,reserveRules);
$('#view-downloads').prepend(reservePanel);
async function loadReserve(){renderReserve(await api('/api/offline/reserve'));}
async function changeReserve(data){renderReserve(await post('/api/offline/reserve',data));}
function renderReserve(data){
 reserveEnabled.checked=data.enabled;reserveLimit.value=data.limit_mib;
 reserveStatus.textContent=(data.bytes/1048576).toFixed(1)+' / '+data.limit_mib+' MiB';reserveRules.replaceChildren();
 for(const rule of data.rules){const row=document.createElement('p');row.append(rule.name+' · '+rule.count+' · '+rule.profile+' · '+rule.audio_quality,
  button(t('Remove'),()=>changeReserve({action:'remove',key:rule.key})));
  if(data.errors[rule.key]){const error=document.createElement('p');error.className='bad';error.textContent=t(data.errors[rule.key]);row.append(error);}reserveRules.append(row);}
 if(data.errors.worker)reserveStatus.textContent+=' — '+t(data.errors.worker);
}
function addReserveControls(item){
 const label=document.createElement('label'),count=document.createElement('input');count.type='number';count.min=1;count.max=20;count.value=3;
 label.append(t('Unwatched episodes to keep ready'),count);
 const add=button(t('Reserve this series/folder'),async()=>{
  const request={action:'add',id:item.id,count:Number(count.value),audio:Number($('#audio').value),subtitle:Number($('#subtitle').value),
   audio_quality:$('#audio_quality').value,video_fps:$('#video_fps').value,profile:$('#downloadProfile').value};
  add.disabled=true;
  try{await changeReserve(request);setView('downloads');message(t('Rule saved. Enable the episode reserve in Downloads.'));}
  finally{add.disabled=false;}
 });$('#details').append(label,add);
}
const watchlistLabel=document.createElement('label'),watchlistEnabled=document.createElement('input');watchlistEnabled.type='checkbox';
watchlistLabel.append(watchlistEnabled,t('Enable Plex Watchlist'));
$('#plexStatus').after(watchlistLabel);
watchlistEnabled.onchange=async()=>{try{const state=await post('/api/plex/watchlist',{enabled:watchlistEnabled.checked});watchlistEnabled.checked=state.watchlist;}catch(error){watchlistEnabled.checked=!watchlistEnabled.checked;fail(error);}};
document.addEventListener('click',event=>{if(event.target.closest('[data-view="downloads"]'))loadReserve().catch(fail);});
// Follow the existing authenticated startup; no extra playback polling.
api('/api/plex').then(data=>watchlistEnabled.checked=data.watchlist).catch(fail);
loadReserve().catch(fail);
