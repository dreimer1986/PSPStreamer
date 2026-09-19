// Account passwords are entered only on plex.tv. This UI never sees tokens.
const plexPanel = document.createElement('details');
plexPanel.className = 'panel';
plexPanel.innerHTML = `<summary>Media sources / Plex</summary>
<p>Link Plex, choose a reachable server connection and enable Plex below. Originals are read directly over HTTP(S), including across networks. Plex does not transcode them.</p>
<button id="plexLink">Link Plex account</button> <a id="plexAuth" hidden target="_blank" rel="noopener noreferrer">Open Plex sign-in</a>
<button id="plexServers">Refresh servers</button> <button id="plexDisconnect">Disconnect Plex</button>
<label>Server connection <select id="plexConnection"></select></label><button id="plexSelect">Use connection</button>
<p id="plexStatus" role="status"></p>
<form id="plexSources">
<label><input type="checkbox" id="sourceFiles"> Filesystem library</label>
<label><input type="checkbox" id="sourcePlex"> Plex library</label>
<label><input type="checkbox" id="sourceRadio"> Internet radio</label>
<details><summary>Optional: use existing local media mounts</summary><p>No mapping is needed. If the same originals are already mounted here, a mapping can avoid the HTTP transfer. Otherwise Plex HTTP(S) is used automatically.</p>
<div id="plexMappings"></div><button type="button" id="plexAddMapping">Add path mapping</button>
</details><button type="submit">Save sources</button></form>`;
document.querySelector('details').before(plexPanel);
let plexPollTimer, plexGeneration = 0;
function plexPost(action, data={}) {
  return api('/api/plex/'+action,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
}
function plexError(e) { $('#plexStatus').textContent=e.message; }
function plexMapping(plex='',local='') {
  const row=document.createElement('div');
  row.className='plex-mapping';
  const from=document.createElement('input'), to=document.createElement('input'), remove=document.createElement('button');
  from.placeholder='Plex directory'; from.setAttribute('aria-label','Plex directory'); from.value=plex;
  to.placeholder='Mounted directory'; to.setAttribute('aria-label','Mounted directory'); to.value=local;
  remove.type='button';remove.textContent='Remove';remove.onclick=()=>row.remove();
  row.append(from,to,remove);$('#plexMappings').append(row);
}
async function plexRefresh() {
  const settings=await api('/api/plex');
  $('#sourceFiles').checked=settings.files;$('#sourcePlex').checked=settings.enabled;$('#sourceRadio').checked=settings.radio;
  $('#plexMappings').replaceChildren();settings.mappings.forEach(m=>plexMapping(m.plex,m.local));
  $('#plexStatus').textContent=(settings.selected?'Selected: '+settings.url:settings.linked?'Account linked; select a server.':'Plex not linked.')+(settings.report_error?' Playback reporting: '+settings.report_error:'');
}
$('#plexAddMapping').onclick=()=>plexMapping();
$('#plexLink').onclick=async()=>{
  try {
    clearTimeout(plexPollTimer);const generation=++plexGeneration;
    const data=await plexPost('link');
    $('#plexAuth').href=data.url;$('#plexAuth').hidden=false;
    $('#plexStatus').textContent='Open Plex sign-in, authorize PSPStreamer, then return here.';
    const poll=async()=>{
      if(generation!==plexGeneration)return;
      try {
        const result=await plexPost('poll');
        if(result.linked){$('#plexAuth').hidden=true;await plexRefresh();await plexServers();}
        else plexPollTimer=setTimeout(poll,2000);
      }catch(e){plexError(e);}
    };
    plexPollTimer=setTimeout(poll,2000);
  }catch(e){plexError(e);}
};
async function plexServers() {
  const servers=await api('/api/plex/servers');const select=$('#plexConnection');select.replaceChildren();
  for(const server of servers)for(const url of server.connections){
    const item=document.createElement('option');item.value=url;item.dataset.server=server.id;
    item.textContent=server.name+' — '+url;select.append(item);
  }
  if(!select.options.length)$('#plexStatus').textContent='No Plex Media Server available for this account.';
}
$('#plexServers').onclick=()=>plexServers().catch(plexError);
$('#plexSelect').onclick=async()=>{
  try {
    const item=$('#plexConnection').selectedOptions[0];if(!item)throw Error('Refresh servers and select a connection first.');
    await plexPost('select',{server:item.dataset.server,url:item.value});await plexRefresh();
  }catch(e){plexError(e);}
};
$('#plexDisconnect').onclick=async()=>{
  if(!confirm('Disconnect Plex and restore the filesystem library?'))return;
  try{++plexGeneration;clearTimeout(plexPollTimer);$('#plexAuth').hidden=true;await plexPost('disconnect');await plexRefresh();await browse('');}catch(e){plexError(e);}
};
$('#plexSources').onsubmit=async event=>{
  event.preventDefault();
  try{
    const mappings=[...document.querySelectorAll('.plex-mapping')].map(row=>{
      const fields=row.querySelectorAll('input');return {plex:fields[0].value,local:fields[1].value};
    });
    await plexPost('settings',{files:$('#sourceFiles').checked,enabled:$('#sourcePlex').checked,radio:$('#sourceRadio').checked,mappings});
    await plexRefresh();await browse('');
  }catch(e){plexError(e);}
};
const plexDetails=document.createElement('div');$('#details').after(plexDetails);
const plexBaseChoose=choose;
choose=async(v,b)=>{
  plexDetails.replaceChildren();
  try{await plexBaseChoose(v,b);}catch(e){
    if(selected===v){selected=null;$('#audio').replaceChildren();$('#subtitle').replaceChildren();$('#details').textContent=e.message;$('#status').textContent='Could not load media information.';}
    return;
  }
  if(selected!==v || !String(v.id).startsWith('plex.'))return;
  try{
    const data=await api('/api/plex/details/'+encodeURIComponent(v.id));if(selected!==v)return;
    const title=document.createElement('p'),summary=document.createElement('p');
    title.textContent=[data.artist,data.album,data.title,data.year,data.watched?'Watched':'Unwatched'].filter(Boolean).join(' · ');
    summary.textContent=data.summary;plexDetails.append(title,summary);
    if(data.resume>0){
      const resume=document.createElement('button');resume.textContent='Resume at '+Math.floor(data.resume/60)+':'+String(data.resume%60).padStart(2,'0');
      resume.onclick=()=>{$('#seek').value=data.resume;$('#seek').dispatchEvent(new Event('input'));};plexDetails.append(resume);
    }
  }catch(e){plexDetails.textContent=e.message;}
};
plexRefresh().catch(plexError);
