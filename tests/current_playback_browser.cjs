const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE});
 const errors=[],commands=[];
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
   else if(path.startsWith('/api/metadata/'))data={d:1200,a:[{n:0,l:'eng'}],s:[],chapters:[]};
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
  assert.deepEqual(errors,[]);
  console.log('Fresh-session adoption, folder selection, playback switch, paused seek, offline state and no accidental Play: passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
