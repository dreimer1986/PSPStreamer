// Radio is managed on the server; the PSP receives IDs, never station URLs.
const radioPanel=document.createElement('details');
radioPanel.className='panel';
radioPanel.innerHTML=`<summary>Internet radio — manage stations</summary>
<p>Direct HTTP(S) audio streams or .m3u/.pls links (first entry). No website URLs or HLS.
Pause disconnects; Resume returns to the live programme. No seeking or offline conversion.</p>
<button id="radioBrowse">Browse radio on PSP / remote</button>
<form id="radioForm">
<input id="radioId" type="hidden">
<label>Station name <input id="radioName" maxlength="100" required></label>
<label>Stream URL <input id="radioUrl" type="url" maxlength="2048" required size="40"></label>
<button type="submit">Save station</button><button id="radioCancel" type="button">Clear / add new</button>
</form><p id="radioStatus" role="status"></p><div id="radioStations"></div>`;
document.querySelector('.grid').after(radioPanel);
const radioPost=data=>api('/api/radio',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
async function radioRefresh(){
  const stations=await api('/api/radio'), box=$('#radioStations');box.replaceChildren();
  for(const station of stations){
    const row=document.createElement('div'), label=document.createElement('span');
    label.textContent=station.name+' ';row.append(label);
    const play=document.createElement('button');play.textContent='Select';
    play.onclick=()=>choose({...station,kind:'audio',live:true},play).catch(e=>$('#radioStatus').textContent=e.message);
    const edit=document.createElement('button');edit.textContent='Edit';
    edit.onclick=()=>{for(const [id,key] of [['radioId','id'],['radioName','name'],['radioUrl','url']])$('#'+id).value=station[key]};
    const remove=document.createElement('button');remove.textContent='Delete';
    remove.onclick=async()=>{
      if(!confirm('Remove station '+station.name+'?'))return;
      try{await radioPost({id:station.id,delete:true});if(selected?.id===station.id)selected=null;await radioRefresh();if(path===':radio:')await browse(path)}
      catch(e){$('#radioStatus').textContent=e.message}
    };
    row.append(play,edit,remove);box.append(row);
  }
}
$('#radioForm').onsubmit=async e=>{
  e.preventDefault();
  try{
    await radioPost({id:$('#radioId').value,name:$('#radioName').value.trim(),url:$('#radioUrl').value.trim()});
    $('#radioForm').reset();$('#radioStatus').textContent='Saved on server. Refresh the PSP library with Square.';
    await radioRefresh();if(path===':radio:')await browse(path);
  }catch(error){$('#radioStatus').textContent=error.message}
};
$('#radioCancel').onclick=()=>$('#radioForm').reset();
$('#radioBrowse').onclick=()=>browse(':radio:').catch(e=>$('#radioStatus').textContent=e.message);
const radioBaseChoose=choose;
let radioStatusGeneration=0;
choose=async(v,b)=>{
  await radioBaseChoose(v,b);
  if(selected!==v)return;
  $('#seek').parentElement.hidden=!!v.live;
  $('#seek').disabled=!!v.live;
  if(v.live){$('#seek').value=0;$('#details').textContent='Live radio · Pause disconnects; Resume rejoins live. No seek.'}
  else if(v.kind==='audio'){
    const d=await api('/api/metadata/'+encodeURIComponent(v.id));
    if(selected===v)$('#details').textContent=[d.artist,d.title,d.album].filter(Boolean).join(' · ')||'Music stream';
  }
};
const radioBaseCommand=command;
command=async(action,extra={})=>{
  if(selected?.live && action==='seek')return;
  if(selected?.live && action==='play')$('#seek').value=0;
  return radioBaseCommand(action,extra);
};
radioRefresh().catch(e=>$('#radioStatus').textContent=e.message);
async function radioNowPlaying(){
  const item=selected, generation=++radioStatusGeneration;
  try{
    if(item?.live){
      const state=await api('/api/radio/status/'+encodeURIComponent(item.id));
      if(selected===item && generation===radioStatusGeneration){
        $('#details').textContent=state.radio_active?[state.radio_station,state.radio_title||'Live — no title supplied'].join(' · '):'Radio stopped / paused / reconnecting';
      }
    }
  }catch(error){if(selected===item)$('#details').textContent=error.message}
  setTimeout(radioNowPlaying,3000);
}
radioNowPlaying();
