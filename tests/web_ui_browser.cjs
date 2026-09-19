// Run with PLAYWRIGHT_MODULE=/path/to/playwright node tests/web_ui_browser.cjs
// Browser behavior uses deterministic API fixtures; Python tests cover the real HTTP API.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'../static');
(async()=>{
  const browser=await chromium.launch({headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1100,height:850},locale:'en'});
    const errors=[],commands=[],batches=[];
    page.on('pageerror',error=>errors.push(error.message));
    const files=[{id:'video1',name:'Episode 1.mkv',kind:'video'},
      {id:'video2',name:'Episode 2.mkv',kind:'video'},
      {id:'video3',name:'Episode 3.mkv',kind:'video'},
      {id:'music',name:'Musik Grüße.flac',kind:'audio'}];
    let preferences={},recursive=false,delayMetadata=false;
    await page.route('http://psp.test/**',async route=>{
      const request=route.request(),url=new URL(request.url());
      if(url.pathname.startsWith('/api/')){
        let data={},status=200;
        const body=request.postDataJSON();
        switch(url.pathname){
          case '/api/session':data={csrf:'test-csrf',protected:true};break;
          case '/api/offline/preferences':if(body)preferences=body;data=preferences;break;
          case '/api/offline/jobs':data=[];break;
          case '/api/plex':data={files:true,enabled:true,radio:true,mappings:[],linked:false};break;
          case '/api/radio':data=[];break;
          case '/api/settings':data={password_editable:true,password_set:true};break;
          case '/api/library':{
            const p=url.searchParams.get('path')||'';
            data={root:0,path:p,parent:p===''?null:p===':files:'?'':p===':files:0'?':files:':':files:0',folders:[],videos:[]};
            if(!p)data.folders=[{name:'Files',path:':files:'},{name:'Plex',path:':plex:'},{name:'Internet Radio',path:':radio:'}];
            else if(p===':files:')data.folders=[{name:'Media',path:':files:0'}];
            else if(p===':files:0'){data.videos=files;data.folders=[{name:'Album',path:':files:0/Album'}]}
            else if(p===':files:0/Album')data.videos=[files[3]];
            break;
          }
          case '/api/library/files':recursive=url.searchParams.get('recursive')==='1';data={files};break;
          case '/api/metadata/video1':data={d:'120',a:[{n:'0',l:'jpn',t:'Japanese'},{n:'1',l:'deu',t:'German'}],s:[{n:'0',l:'deu',t:'German'}]};break;
          case '/api/metadata/video2':data={d:'120',a:[{n:'0',l:'ger',t:'German'}],s:[{n:'3',l:'ger',t:'German'}]};break;
          case '/api/metadata/video3':data={d:'120',a:[{n:'0',l:'jpn',t:'Japanese'}],s:[]};break;
          case '/api/metadata/music':data={d:'90',a:[{n:'0',l:'und'}],s:[],artist:'Künstler',title:'Grüße'};break;
          case '/api/remote/command':commands.push(body);data=body;break;
          case '/api/offline/batch':batches.push(body.items);data={jobs:body.items};break;
          case '/api/logout':data={ok:true};break;
          default:status=404;data={error:'Unknown fixture '+url.pathname};
        }
        if(body)assert.equal(request.headers()['x-csrf-token'],'test-csrf');
        if(delayMetadata&&url.pathname.startsWith('/api/metadata/'))await new Promise(resolve=>setTimeout(resolve,150));
        return route.fulfill({status,contentType:'application/json',body:JSON.stringify(data)});
      }
      const name=url.pathname==='/'?'index.html':url.pathname==='/login'?'login.html':url.pathname.slice(1);
      const contentType=name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':name.endsWith('.png')?'image/png':'text/html';
      return route.fulfill({contentType,body:fs.readFileSync(path.join(root,name))});
    });
    const click=selector=>page.locator(selector).click();
    const visible=async(selector,expected)=>assert.equal(await page.locator(selector).isVisible(),expected,selector);
    await page.goto('http://psp.test/');
    await page.getByRole('button',{name:'▸ Files',exact:true}).waitFor();
    await click('[data-view=remote]');await visible('#mediaOptions',false);await visible('[data-action=stop]',true);
    await click('[data-view=library]');
    await page.getByRole('button',{name:'▸ Files',exact:true}).click();
    await page.getByRole('button',{name:'▸ Media',exact:true}).click();
    await page.getByRole('button',{name:'♫ Musik Grüße.flac',exact:true}).click();
    await page.locator('#mediaOptions').waitFor();
    await visible('#audioField',false);await visible('#subtitleField',false);await visible('#fpsField',false);
    await click('#play');await page.waitForFunction(()=>document.querySelector('#status').textContent.startsWith('Sent:'));
    assert.equal(commands.at(-1).subtitle,-1);
    await click('[data-view=library]');await page.getByRole('button',{name:'▶ Episode 1.mkv',exact:true}).click();
    await page.locator('#mediaOptions').waitFor();await visible('#subtitleField',true);
    await page.selectOption('#subtitle','0');await click('#play');
    await page.waitForTimeout(40);assert.equal(commands.at(-1).subtitle,0);
    await page.selectOption('#audio','1');
    await click('[data-view=library]');await click('#selectAll');await click('#prepareSelected');
    await page.waitForFunction(()=>!document.querySelector('#previewBatch').disabled);
    await click('#previewBatch');await page.waitForFunction(()=>document.querySelector('#batchStatus').textContent.includes('ready;'));
    assert.equal(await page.locator('#batchRows input:checked').count(),3);
    assert.equal(await page.locator('#batchRows input:disabled').count(),1);
    assert.equal(batches.length,0);
    await page.screenshot({path:'/tmp/psp-web-batch.png',fullPage:true});
    await click('#commitBatch');await page.waitForFunction(()=>document.querySelector('#batch').hidden);
    assert.equal(batches.length,1);assert.equal(batches[0].length,3);
    assert.deepEqual(batches[0].map(x=>[x.id,x.audio,x.subtitle]),[['video1',1,0],['video2',0,3],['music',0,-1]]);
    await click('[data-view=library]');await page.getByRole('button',{name:'▸ Album',exact:true}).click();
    await click('#selectAll');await click('#prepareSelected');await page.waitForFunction(()=>!document.querySelector('#previewBatch').disabled);
    await visible('#batchAudio',false);await visible('#batchSubtitle',false);await visible('#batchFps',false);await visible('#batchQuality',true);
    await click('#cancelBatch');await click('[data-view=library]');await click('#home');
    await page.getByRole('button',{name:'▸ Plex',exact:true}).waitFor();
    await page.getByRole('button',{name:'▸ Files',exact:true}).click();await click('#parent');
    await page.getByRole('button',{name:'▸ Internet Radio',exact:true}).waitFor();
    await page.selectOption('.language','de');
    await page.getByRole('button',{name:'Bibliothek',exact:true}).waitFor();
    await page.setViewportSize({width:390,height:844});
    await click('[data-view=settings]');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:'/tmp/psp-web-mobile.png',fullPage:true});
    assert.deepEqual(errors,[]);
    // Translation coverage includes every static UI label.
    const missing=await page.evaluate(()=>[...document.querySelectorAll('[data-i18n]')].map(e=>e.dataset.i18n).filter(key=>!webLanguages.de[key]));
    assert.deepEqual(missing,[]);
    console.log('Web browser checks passed: navigation, music/video, subtitle 0, batch language matching, German, mobile.');
  }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exit(1)});
