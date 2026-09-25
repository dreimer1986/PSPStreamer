'use strict';
let comfortRecords=[],comfortTab='favorites',searchTimer=0;
const comfortPanel=document.createElement('section');comfortPanel.id='view-comfort';comfortPanel.className='view panel';comfortPanel.hidden=true;
const comfortNav=button(t('Favorites & history'),()=>{setView('comfort');loadComfort().catch(fail)});
comfortNav.dataset.view='comfort';comfortNav.dataset.i18n='Favorites & history';$('nav').append(comfortNav);
const comfortHeading=document.createElement('h2');comfortHeading.textContent=t('Favorites & history');comfortHeading.dataset.i18n='Favorites & history';
const comfortTabs=document.createElement('div');comfortTabs.className='toolbar';
const comfortList=document.createElement('div');
for(const [key,label] of [['favorites','Favorites'],['recent','Recently played'],['continue','Continue watching']]){
  const b=button(t(label),()=>{comfortTab=key;loadComfort().catch(fail)});b.dataset.i18n=label;comfortTabs.append(b);
}
const syncHint=document.createElement('p');syncHint.className='muted';syncHint.dataset.i18n='Open Comfort on the PSP to synchronize favorites and history. Local downloads and server passwords stay on the PSP.';syncHint.textContent=t(syncHint.dataset.i18n);
comfortPanel.append(comfortHeading,comfortTabs,syncHint,comfortList);$('#view-library').after(comfortPanel);
async function loadComfort(){const d=await api('/api/comfort');comfortRecords=d.records;renderComfort();return d;}
function favoriteButton(item){
  const b=button('',async()=>{
    await loadComfort();const current=comfortRecords.find(r=>r.id===item.id);
    await post('/api/comfort',{...item,action:'favorite',value:!current?.favorite});
    await loadComfort();refresh();
  });
  function refresh(){const on=!!comfortRecords.find(r=>r.id===item.id)?.favorite;b.textContent=on?'★':'☆';b.setAttribute('aria-label',t(on?'Remove favorite':'Add favorite'));b.title=b.getAttribute('aria-label');b.setAttribute('aria-pressed',String(on));}
  refresh();return b;
}
async function openShortcut(r,resume=false){
  if(r.folder){root=r.root||0;setView('library');await browse(r.id);return;}
  await choose({id:r.id,name:r.name,kind:r.audio?'audio':'video',live:r.id.startsWith('radio.')});
  if(resume && selected?.id===r.id && media){
    const seconds=media.resume!==undefined?Number(media.resume):r.seconds;
    if(seconds>0)await seekTo(seconds);
  }
}
function renderComfort(){
  comfortList.replaceChildren();
  const rows=comfortRecords.filter(r=>comfortTab==='favorites'?r.favorite:comfortTab==='recent'?r.used&&!r.folder:r.seconds>=5&&!r.audio&&!r.folder);
  for(const r of rows){
    const row=document.createElement('div');row.className='item';
    row.append(button((r.folder?'▸ ':r.audio?'♫ ':'▶ ')+r.name,()=>openShortcut(r,comfortTab==='continue')),favoriteButton(r));
    if(r.seconds>0&&!r.audio&&!r.folder)row.append(button(t('Resume at {time}',{time:timeLabel(r.seconds)}),()=>openShortcut(r,true)));
    if(comfortTab!=='favorites')row.append(button(t('Remove from history'),async()=>{await post('/api/comfort',{id:r.id,action:'forget'});await loadComfort()}));
    comfortList.append(row);
  }
  if(!rows.length)comfortList.textContent=t('No entries yet.');
}
const originalRenderLibrary=renderLibrary;
renderLibrary=function(){
  originalRenderLibrary();if(!listing)return;
  const children=Array.from($('#library').children);
  listing.folders.forEach((f,i)=>{
    if(['Next page','Previous page'].includes(f.name))return;
    const old=children[i];if(!old)return;
    const wrap=document.createElement('div');wrap.className=old.className;old.className='';old.replaceWith(wrap);
    wrap.append(old,favoriteButton({id:f.path,name:f.name,folder:1,audio:0}));
  });
  listing.videos.forEach((v,i)=>children[listing.folders.length+i]?.append(favoriteButton({id:v.id,name:v.name,folder:0,audio:Number(v.kind==='audio')})));
};
const searchForm=document.createElement('form');searchForm.className='toolbar';
const searchInput=document.createElement('input');searchInput.type='search';searchInput.minLength=2;searchInput.maxLength=120;searchInput.required=true;
searchInput.placeholder=t('Search films, series and music');searchInput.setAttribute('aria-label',searchInput.placeholder);
const searchSubmit=document.createElement('button');searchSubmit.textContent=t('Search all sources');searchSubmit.dataset.i18n='Search all sources';
searchForm.append(searchInput,searchSubmit);$('#crumb').before(searchForm);
const searchResults=document.createElement('section');searchResults.hidden=true;$('#library').before(searchResults);
searchForm.onsubmit=async event=>{event.preventDefault();clearTimeout(searchTimer);searchSubmit.disabled=true;searchResults.hidden=false;await runSearch(searchInput.value)};
async function runSearch(query){
  try{
    const d=await api('/api/search?q='+encodeURIComponent(query));searchResults.replaceChildren();
    const info=document.createElement('p');info.textContent=t(d.running?'Searching… {n} folders checked':'Search complete: {n} folders checked',{n:d.visited});searchResults.append(info);
    if(d.limited){const p=document.createElement('p');p.textContent=t('Search limit reached. Results are incomplete; browse the source for more.');searchResults.append(p)}
    for(const error of d.errors){const p=document.createElement('p');p.textContent=error;searchResults.append(p)}
    for(const r of d.results){const row=document.createElement('div');row.className='item';row.append(button(`[${r.source}] ${r.name}`,()=>openShortcut(r)),favoriteButton(r));searchResults.append(row)}
    if(!d.results.length&&!d.running)searchResults.append(document.createTextNode(t('No results.')));
    searchResults.append(button(t('Close search'),()=>{clearTimeout(searchTimer);searchResults.hidden=true;searchSubmit.disabled=false}));
    if(d.running)searchTimer=setTimeout(()=>runSearch(query),1500);else searchSubmit.disabled=false;
  }catch(e){fail(e);searchSubmit.disabled=false;}
}
loadComfort().then(()=>{if(listing)renderLibrary()}).catch(fail);
