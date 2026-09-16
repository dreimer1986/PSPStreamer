// A queued item is a snapshot of its own track/quality/output selections.
const downloads = document.createElement('section');
downloads.className = 'panel';
downloads.innerHTML = '<h2>Offline downloads</h2><p>Convert here, then open Local storage → Server queue on the PSP to download. No PSP connection is needed while converting.</p><div id="jobs"></div>';
document.querySelector('details').before(downloads);
const target = document.createElement('label');
target.innerHTML = 'Download output <select id="downloadProfile"><option value="normal">LCD 480×272</option><option value="tv">TV 720×480</option><option value="low">LCD low bitrate</option></select>';
$('#play').parentElement.before(target);
const queueButton = document.createElement('button');
queueButton.textContent = 'Convert for download';
$('#play').after(queueButton);
const streamingChoose=choose;
choose=async(v,b)=>{queueButton.hidden=target.hidden=v.kind==='audio';return streamingChoose(v,b);};
queueButton.onclick = async () => {
  if (!selected || selected.kind === 'audio') { $('#status').textContent = 'Select a video for offline conversion.'; return; }
  try {
    await api('/api/offline/jobs', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({
      id:selected.id, audio:+$('#audio').value || 0, subtitle:+$('#subtitle').value,
      audio_quality:$('#audio_quality').value, video_fps:$('#video_fps').value, profile:$('#downloadProfile').value
    })});
    $('#status').textContent = 'Added to conversion queue (complete episode, from the beginning).';
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
      text.textContent=`${j.name} — ${j.state} ${j.progress}% · ${(j.bytes/1048576).toFixed(1)} MiB · ${j.profile} · audio ${j.audio+1}, subtitle ${j.subtitle<0?'off':j.subtitle+1} · ${j.audio_quality} · ${j.video_fps} fps${j.error?' — '+j.error:''}`;
      row.append(text);
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
