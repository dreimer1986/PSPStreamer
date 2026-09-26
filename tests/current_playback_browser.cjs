const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE});
 const errors=[],commands=[];
 let queue={revision:0,enabled:false,items:[]};
 let current={online:true,age:0,id:'episode1',title:'Episode One',kind:'video',state:'playing',position:123,duration:1200};
 const files=[{id:'episode1',name:'Episode One',kind:'video'},{id:'song',name:'Song',kind:'audio'},{id:'other',name:'Other',kind:'video'}];
 try{
  const context=await browser.newContext({locale:'en'});
  await context.route('**/api/**',async route=>{
   const path=new URL(route.request().url()).pathname;let data={};
   if(path==='/api/session')data={csrf:'test',protected:false};
   else if(path==='/api/library')data={root:0,path:'',parent:null,folders:[],videos:files};
   else if(path==='/api/plex')data={mappings:[]};
   else if(path==='/api/dlna')data={devices:[]};
   else if(path==='/api/radio'||path==='/api/offline/jobs')data=[];
   else if(path==='/api/comfort')data={records:[]};
   else if(path==='/api/player')data=current;
   else if(path.startsWith('/api/metadata/'))data={d:1200,a:[{n:0,l:'eng'}],s:[{n:0,l:'deu'}],chapters:[]};
   else if(path==='/api/playlist'){
    if(route.request().method()==='POST'){
     const change=route.request().postDataJSON();assert.equal(change.revision,queue.revision);
     if(change.action==='add')for(const item of change.items)if(!queue.items.some(r=>r.id===item.id))queue.items.push(item);
     if(change.action==='move'){const index=queue.items.findIndex(r=>r.id===change.id);queue.items.splice(change.position,0,...queue.items.splice(index,1))}
     if(change.action==='remove')queue.items=queue.items.filter(r=>r.id!==change.id);
     if(change.action==='enabled')queue.enabled=change.enabled;
     if(change.action==='play'){queue.enabled=true;commands.push({action:'play',id:change.id})}
     if(change.action==='clear')queue.items=[];
     queue.revision++;
    }data=queue;
   }
   else if(path==='/api/remote/command')commands.push(route.request().postDataJSON());
   await route.fulfill({json:data});
  });
  const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
  await page.goto(process.argv[2]);
  await page.getByRole('button',{name:'Control current playback',exact:true}).waitFor();
  await page.locator('nav [data-view="remote"]').click();
  await page.waitForFunction(()=>document.querySelector('#title').textContent==='Episode One'&&+document.querySelector('#seek').value>=123);
  assert.equal(commands.length,0); // Adoption must never restart the PSP.
  await page.locator('[data-action="pause"]').click();
  await page.waitForTimeout(100);assert.equal(commands.at(-1).action,'pause');
  current={...current,id:'song',title:'Song',kind:'audio',state:'paused',position:42};
  await page.evaluate(()=>refreshPlayer(true));
  assert.equal(await page.locator('#title').textContent(),'Song');
  assert.equal(await page.locator('#audioField').isVisible(),false);
  assert.equal(await page.locator('#seek').inputValue(),'42');
  await page.locator('nav [data-view="library"]').click();
  await page.getByRole('button',{name:'▶ Other',exact:true}).click();
  await page.evaluate(()=>refreshPlayer(true));
  assert.equal(await page.locator('#title').textContent(),'Other');
  await page.getByRole('button',{name:'Control current playback',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#title').textContent==='Song'&&!document.querySelector('#mediaOptions').hidden);
  await page.reload();
  await page.getByRole('button',{name:'Control current playback',exact:true}).waitFor();
  await page.getByRole('button',{name:'♫ Song — Now playing',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#seek').value==='42');
  assert.equal(commands.length,1);
  current={online:false,age:60,state:'idle'};await page.evaluate(()=>refreshPlayer(true));
  assert.equal(await page.getByRole('button',{name:'Control current playback',exact:true}).isVisible(),false);
  await page.locator('nav [data-view="library"]').click();
  await page.getByRole('button',{name:'▶ Other',exact:true}).click();
  await page.locator('#subtitle').selectOption('0');
  await page.locator('#view-remote').getByRole('button',{name:'Add to playlist',exact:true}).click();
  await page.waitForFunction(()=>playlistState.items.length===1);
  assert.equal(queue.items[0].subtitle,0);
  await page.locator('nav [data-view="library"]').click();
  await page.locator('#library').getByRole('button',{name:'Add to playlist',exact:true}).nth(1).click();
  await page.waitForFunction(()=>playlistState.items.length===2);
  await page.locator('nav [data-view="playlist"]').click();
  await page.locator('#view-playlist button[title="Move up"]').nth(1).click();
  await page.waitForFunction(()=>playlistState.items[0].id==='song');
  await page.locator('#view-playlist').getByRole('button',{name:'Play',exact:true}).first().click();
  await page.waitForFunction(()=>playlistState.enabled);
  assert.equal(commands.at(-1).id,'song');
  await page.reload();await page.locator('nav [data-view="playlist"]').click();
  await page.locator('#view-playlist').getByText('1. Song',{exact:true}).waitFor();
  await page.locator('#view-playlist').getByRole('button',{name:'Remove',exact:true}).first().click();
  await page.waitForFunction(()=>playlistState.items.length===1);
  assert.deepEqual(errors,[]);
  console.log('Current playback and playlist add, subtitle zero, mixed entries, move, play, reload and remove: passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
