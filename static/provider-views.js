'use strict';
const providerPanel=document.createElement('section');providerPanel.id='view-provider';providerPanel.className='view panel';providerPanel.hidden=true;
const providerHeading=document.createElement('h2');providerHeading.textContent=t('Provider library');
const providerSelect=document.createElement('select'),providerShelf=document.createElement('select'),providerSection=document.createElement('select');
for(const name of ['plex','jellyfin'])option(providerSelect,name,name==='plex'?'Plex':'Jellyfin');
for(const [key,label] of [['continue','Continue watching'],['recent','Recently added'],['unwatched','Unwatched'],['collections','Collections'],['watchlist','Plex Watchlist']])option(providerShelf,key,t(label));
providerSelect.setAttribute('aria-label',t('Provider'));providerShelf.setAttribute('aria-label',t('View'));providerSection.setAttribute('aria-label',t('Library'));
const providerRows=document.createElement('div'),providerPages=document.createElement('div');
let providerPage=0,providerGeneration=0,providerListing=null;
const providerLoad=button(t('Refresh'),()=>loadProvider(0));
const providerCoverToggle=button(t('Cover view'),toggleCoverView);
providerCoverToggle.dataset.i18n='Cover view';providerCoverToggle.setAttribute('aria-pressed',String(coverView));
document.addEventListener('cover-view-changed',()=>{
 providerCoverToggle.setAttribute('aria-pressed',String(coverView));
 if(providerListing)renderProvider();
});
providerPanel.append(providerHeading,providerSelect,providerShelf,providerSection,providerLoad,providerCoverToggle,providerRows,providerPages);$('#view-library').after(providerPanel);
const providerNav=button(t('Provider library'),()=>{setView('provider');loadProviderSections().catch(fail)});providerNav.dataset.view='provider';$('nav').append(providerNav);
async function loadProviderSections(){
 const watch=providerShelf.querySelector('option[value="watchlist"]');watch.hidden=providerSelect.value!=='plex';watch.disabled=watch.hidden;
 if(watch.hidden&&providerShelf.value==='watchlist')providerShelf.value='continue';
 const generation=++providerGeneration;providerListing=null;providerRows.textContent=t('Loading tracks…');
 try{
  const data=await api('/api/provider-view?view=sections&provider='+providerSelect.value);
  if(generation!==providerGeneration)return;
  providerSection.replaceChildren();option(providerSection,'',t('All libraries'));
  for(const section of data.sections)option(providerSection,section.id,section.name);
  await loadProvider(0);
 }catch(error){if(generation===providerGeneration){providerRows.textContent=error.message;providerPages.replaceChildren()}throw error}
}
async function loadProvider(offset=0){
 const generation=++providerGeneration;providerPage=offset;providerListing=null;providerRows.textContent=t('Loading tracks…');providerPages.replaceChildren();
 const globalShelf=['continue','watchlist'].includes(providerShelf.value);
 providerSection.disabled=globalShelf;
 if(providerSelect.value==='plex'&&!globalShelf&&!providerSection.value){providerRows.textContent=t('Select a provider library');return}
 const query=new URLSearchParams({provider:providerSelect.value,view:providerShelf.value,offset,section:globalShelf?'':providerSection.value});
 try{
  const data=await api('/api/provider-view?'+query);if(generation!==providerGeneration)return;
  providerListing=data;renderProvider();
 }catch(error){if(generation===providerGeneration)providerRows.textContent=error.message;throw error}
}
function renderProvider(){
  const data=providerListing,offset=providerPage;
  providerPages.replaceChildren();
  providerRows.replaceChildren();providerRows.className=coverView?'cover-grid':'';
  for(const folder of data.folders){const b=button('▸ '+folder.name,()=>browse(folder.path));b.className='item';addCover(b,folder.artwork);providerRows.append(b)}
  for(const item of data.videos){
   const row=document.createElement('div');row.className='item';const open=button(item.name,async()=>{
    await choose(item);
    if(providerShelf.value==='continue'&&selected?.id===item.id&&media){const seconds=Number(media.resume??item.resume);if(seconds>0)await seekTo(seconds)}
   });addCover(open,item.artwork);row.append(open,button(t('Add to playlist'),()=>addToPlaylist([item])));providerRows.append(row);
  }
  if(!data.folders.length&&!data.videos.length)providerRows.textContent=t('No entries yet.');
  for(const item of data.unavailable||[]){const row=document.createElement('p');row.textContent=item.name+' — '+t(item.reason);providerRows.append(row);}
  if(providerShelf.value==='watchlist'){const note=document.createElement('p');note.textContent=t('Films and series from your Plex account, not the episode playlist.');providerRows.prepend(note);}
  if(offset)providerPages.append(button(t('Previous page'),()=>loadProvider(Math.max(0,offset-(data.page_size||50)))));
  if(data.next!==null)providerPages.append(button(t('Next page'),()=>loadProvider(data.next)));
}
providerSelect.onchange=()=>loadProviderSections().catch(fail);
providerShelf.onchange=providerSection.onchange=()=>loadProvider(0).catch(fail);
