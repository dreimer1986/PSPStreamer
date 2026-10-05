// Focused real-browser check of Xbox quality selection, with API fixtures.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
(async()=>{
 const browser=await chromium.launch({headless:true});
 try{
  const page=await browser.newPage(),errors=[],commands=[];let xboxState={},pspState={};
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://psp.test/**',async route=>{
   const r=route.request(),u=new URL(r.url());
   if(u.pathname.startsWith('/api/')){
    let data={};
    if(u.pathname==='/api/session')data={csrf:'test',protected:false};
    if(u.pathname==='/api/library')data={root:0,path:'',parent:null,folders:[],videos:[]};
    if(['/api/radio','/api/offline/jobs'].includes(u.pathname))data=[];
    if(u.pathname==='/api/comfort')data={records:[]};
    if(u.pathname==='/api/xbox/status')data=xboxState;
    if(u.pathname==='/api/player')data=pspState;
    if(u.pathname.startsWith('/api/media-next/'))data={id:'next',name:'Next',kind:'video',audio:0,subtitle:-1};
    if(u.pathname.startsWith('/api/metadata/'))data={d:120,a:[{n:0,l:'eng'}],s:[]};
    if(u.pathname.endsWith('/command')){commands.push({path:u.pathname,...r.postDataJSON()});data={ok:true};}
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   }
   const name=u.pathname==='/'?'index.html':u.pathname.slice(1);
   return route.fulfill({contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html',body:fs.readFileSync(path.join(__dirname,'../static',name))});
  });
  await page.goto('http://psp.test/');await page.waitForFunction(()=>document.querySelector('#xboxVideoQuality'));
  await page.evaluate(()=>choose({id:'video',name:'Video',kind:'video'}));
  await page.selectOption('#playbackTarget','xbox');
  assert(await page.locator('#xboxQualityField').isVisible());
  assert(await page.locator('#xboxAudioField').isVisible());
  assert(!(await page.locator('#audio_quality').isVisible()));
  await page.selectOption('#xboxAudioQuality','320k');
  assert.equal(await page.locator('#xboxVideoQuality option').count(),7);
  for(const size of ['480p-low','360p','480p','576p','720p','1080p']){
   await page.selectOption('#xboxVideoQuality',size);await page.click('#play');
   assert.equal(commands.at(-1).xbox_size,size);assert.equal(commands.at(-1).path,'/api/xbox/command');
   assert.equal(commands.at(-1).xbox_audio,'320k');
  }
  await page.selectOption('#xboxVideoQuality','');await page.click('#play');assert(!('xbox_size' in commands.at(-1)));
  await page.selectOption('#xboxVideoQuality','720p');
  await page.evaluate(()=>choose({id:'music',name:'Music',kind:'audio'}));
  assert(!(await page.locator('#xboxQualityField').isVisible()));await page.click('#play');assert(!('xbox_size' in commands.at(-1)));
  await page.selectOption('#playbackTarget','psp');assert(!(await page.locator('#xboxQualityField').isVisible()));
  assert(await page.locator('#audio_quality').isVisible());
  await page.click('#play');assert.equal(commands.at(-1).path,'/api/remote/command');assert(!('xbox_size' in commands.at(-1)));
  await page.selectOption('#playbackTarget','browser');assert(!(await page.locator('#xboxQualityField').isVisible()));
  assert.equal(await page.evaluate(()=>localStorage.getItem('xboxVideoQuality')),'720p');
  xboxState={online:true,state:'playing',age:0,id:'episode',title:'Current Xbox episode',kind:'video',position:42000,duration:120000,metadata:{}};
  await page.evaluate(()=>{clearMedia();setView('remote');});
  await page.selectOption('#playbackTarget','xbox');
  await page.waitForFunction(()=>selected?.id==='episode'&&remotePlaying);
  assert.equal(await page.locator('#view-remote').count(),1);
  assert(await page.locator('#seek').isVisible());
  assert.equal(await page.locator('#seek').inputValue(),'42');
  await page.click('[data-action="pause"]');assert.equal(commands.at(-1).path,'/api/xbox/command');
  assert.equal(commands.at(-1).action,'pause');
  await page.evaluate(()=>seekTo(65));assert.equal(commands.at(-1).seconds,65);
  await page.selectOption('#playbackTarget','psp');
  await page.click('[data-action="pause"]');assert.equal(commands.at(-1).path,'/api/remote/command');
  pspState={online:true,state:'playing',age:0,id:'first',title:'PSP first',kind:'video',position:10,duration:120};
  await page.evaluate(()=>command('next'));
  assert.equal(commands.at(-1).path,'/api/remote/command');assert.equal(commands.at(-1).action,'play');assert.equal(commands.at(-1).id,'next');
  assert.deepEqual(errors,[]);console.log('PASS: six Xbox sizes, default, persistence, music/browser/PSP isolation');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
