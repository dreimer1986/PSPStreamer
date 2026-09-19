// A queued item is a snapshot of its own track/quality/output selections.
const downloads = document.createElement('section');
downloads.className = 'panel';
downloads.innerHTML = '<h2>Offline downloads</h2><p>Recommended: convert here, download the finished Memory Stick ZIP to your PC, extract it there, then merge its PSP folder into the Memory Stick root with PSP Streamer closed. Keep the complete media folder, not just the FLV or MP3. Safely eject before starting Local storage. No SSH or Home Assistant filesystem access needed.</p><p>Without a PC: Local storage → Server queue on the PSP still supports resumable Wi-Fi downloads, but its Wi-Fi is much slower for videos; music files are smaller.</p><div id="jobs"></div>';
document.querySelector('details').before(downloads);
const target = document.createElement('label');
target.innerHTML = 'Download output <select id="downloadProfile"><option value="normal">LCD 480×272</option><option value="tv">TV 720×480</option><option value="low">LCD low bitrate</option></select>';
$('#play').parentElement.before(target);
const queueButton = document.createElement('button');
queueButton.textContent = 'Convert for download';
$('#play').after(queueButton);
const streamingChoose=choose;
let preferences = {}, preferenceWrites = Promise.resolve();
const preferencesReady = api('/api/offline/preferences').then(p=>{
  preferences=p;
  for(const [id,key] of [['audio_quality','audio_quality'],['video_fps','video_fps'],['downloadProfile','profile']])
    if(p[key] && [...$("#"+id).options].some(o=>o.value===p[key])) $("#"+id).value=p[key];
}).catch(e=>{$('#status').textContent='Could not load saved preferences: '+e.message;});
function savePreferences() {
  const body=JSON.stringify(preferences);
  preferenceWrites=preferenceWrites.then(()=>api('/api/offline/preferences',{
    method:'POST',headers:{'Content-Type':'application/json'},body
  })).catch(e=>{$('#status').textContent='Could not save preferences: '+e.message;});
}
for(const [id,key] of [['audio_quality','audio_quality'],['video_fps','video_fps'],['downloadProfile','profile'],['audio','audio'],['subtitle','subtitle']]) {
  $('#'+id).addEventListener('change',()=>{
    const s=$('#'+id);
    preferences[key]=(key==='audio'||key==='subtitle') ? s.selectedOptions[0]?.textContent || '' : s.value;
    savePreferences();
  });
}
function restoreTrack(select, wanted) {
  if(!wanted)return true;
  const options=[...select.options];
  const language=s=>s.trim().split(/\s+/)[0].toLowerCase().replace(/^deu$/, 'ger').replace(/^jpn$/, 'ja');
  const match=options.find(o=>o.textContent===wanted) ||
    (wanted!=='Off' && language(wanted)!=='und' && options.find(o=>language(o.textContent)===language(wanted)));
  if(match){select.value=match.value;return true;}
  return false;
}
choose=async(v,b)=>{
  await preferencesReady;
  queueButton.hidden=String(v.id).startsWith('radio.');
  target.hidden=v.kind==='audio';
  queueButton.disabled=true;
  await streamingChoose(v,b);
  if(selected!==v)return;
  if(v.kind!=='audio') {
    const a=restoreTrack($('#audio'),preferences.audio), s=restoreTrack($('#subtitle'),preferences.subtitle);
    if(!a||!s) $('#details').textContent='Preferred track unavailable: check audio/subtitle selection before converting.';
  }
  queueButton.disabled=false;
};
queueButton.onclick = async () => {
  if (!selected || String(selected.id).startsWith('radio.')) { $('#status').textContent = 'Select a video or music file for offline conversion.'; return; }
  try {
    await api('/api/offline/jobs', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({
      id:selected.id, audio:selected.kind==='audio'?0:+$('#audio').value || 0, subtitle:selected.kind==='audio'?-1:+$('#subtitle').value,
      audio_quality:$('#audio_quality').value, video_fps:$('#video_fps').value, profile:$('#downloadProfile').value
    })});
    $('#status').textContent = 'Added to conversion queue (complete file, from the beginning).';
    await updateDownloads();
  } catch(e) { $('#status').textContent=e.message; }
};
async function updateDownloads() {
  try {
    const jobs = await api('/api/offline/jobs');
    const box=$('#jobs'); box.replaceChildren();
    if (!jobs.length) box.textContent='No conversion jobs.';
    for (const j of jobs) {
      const row=document.createElement('div'); row.className='panel';
      const text=document.createElement('p');
      text.textContent=`${j.name} — ${j.state} ${j.progress}% · ${(j.bytes/1048576).toFixed(1)} MiB · `+
        (j.kind==='audio'?`MP3 · ${j.audio_quality}`:`${j.profile} · audio ${j.audio+1}, subtitle ${j.subtitle<0?'off':j.subtitle+1} · ${j.audio_quality} · ${j.video_fps} fps`)+
        (j.error?' — '+j.error:'');
      row.append(text);
      if(j.state==='ready') {
        const link=document.createElement('a');
        link.href='/api/offline/export/'+encodeURIComponent(j.job);
        link.textContent='Download Memory Stick ZIP (PC / USB — recommended)';
        link.style.color='#8de3ff';
        row.append(link);
      }
      if(j.state==='encoding' && j.eta!=null) {const eta=document.createElement('p');eta.textContent='Estimated conversion time remaining: '+j.eta+' s';row.append(eta);}
      const active=['queued','encoding'].includes(j.state), button=document.createElement('button');
      button.textContent=active?'Cancel conversion':'Delete server copy';
      button.onclick=async()=>{
        if(!confirm(active?'Cancel this conversion?':'Delete this server job and converted files? PSP copies are kept.'))return;
        try { await api('/api/offline/'+(active?'cancel':'delete'),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({job:j.job})}); await updateDownloads(); }
        catch(e){$('#status').textContent=e.message;}
      };
      row.append(button);box.append(row);
    }
  } catch(e) { $('#jobs').textContent=e.message; }
}
async function pollDownloads(){await updateDownloads();setTimeout(pollDownloads,3000);}
pollDownloads();
