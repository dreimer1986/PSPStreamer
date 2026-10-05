// Focused real-browser check of Xbox quality selection, with API fixtures.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
(async()=>{
 const browser=await chromium.launch({headless:true});
 try{
  const page=await browser.newPage(),errors=[],commands=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://psp.test/**',async route=>{
   const r=route.request(),u=new URL(r.url());
   if(u.pathname.startsWith('/api/')){
    let data={};
    if(u.pathname==='/api/session')data={csrf:'test',protected:false};
    if(u.pathname==='/api/library')data={root:0,path:'',parent:null,folders:[],videos:[]};
    if(['/api/radio','/api/offline/jobs'].includes(u.pathname))data=[];
    if(u.pathname==='/api/comfort')data={records:[]};
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
  assert.equal(await page.locator('#xboxVideoQuality option').count(),7);
  for(const size of ['480p-low','360p','480p','576p','720p','1080p']){
   await page.selectOption('#xboxVideoQuality',size);await page.click('#play');
   assert.equal(commands.at(-1).xbox_size,size);assert.equal(commands.at(-1).path,'/api/xbox/command');
  }
  await page.selectOption('#xboxVideoQuality','');await page.click('#play');assert(!('xbox_size' in commands.at(-1)));
  await page.selectOption('#xboxVideoQuality','720p');
  await page.evaluate(()=>choose({id:'music',name:'Music',kind:'audio'}));
  assert(!(await page.locator('#xboxQualityField').isVisible()));await page.click('#play');assert(!('xbox_size' in commands.at(-1)));
  await page.selectOption('#playbackTarget','psp');assert(!(await page.locator('#xboxQualityField').isVisible()));
  await page.click('#play');assert.equal(commands.at(-1).path,'/api/remote/command');assert(!('xbox_size' in commands.at(-1)));
  await page.selectOption('#playbackTarget','browser');assert(!(await page.locator('#xboxQualityField').isVisible()));
  assert.equal(await page.evaluate(()=>localStorage.getItem('xboxVideoQuality')),'720p');
  assert.deepEqual(errors,[]);console.log('PASS: six Xbox sizes, default, persistence, music/browser/PSP isolation');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
