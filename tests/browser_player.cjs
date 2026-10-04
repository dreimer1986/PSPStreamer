const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true});
 try {
  const page=await browser.newPage();const errors=[],commands=[],reports=[];
  page.on('pageerror',e=>errors.push(e.message));
  page.on('request',r=>{if(r.url().includes('/api/remote/command'))commands.push(r);if(r.url().includes('/api/client-playback'))reports.push(r.postDataJSON());});
  await page.goto(process.env.BROWSER_TEST_URL);
  await page.waitForFunction(()=>csrf!==undefined&&document.querySelector('#playbackTarget'));
  await page.evaluate(async id=>{await choose({id,name:'Browser fixture',kind:'video'});},process.env.BROWSER_TEST_TOKEN);
  await page.selectOption('#playbackTarget','browser');
  await page.click('#play');
  console.log('browser play requested');
  await page.waitForFunction(()=>browserVideo.currentTime>.2);
  console.log('video advanced');
  assert.equal(await page.evaluate(()=>browserVideo.videoWidth),720);
  await page.evaluate(()=>browserVideo.pause());
  await page.waitForFunction(()=>browserPaused&&!browserVideo.getAttribute('src'));
  console.log('paused and detached');
  const paused=await page.evaluate(()=>browserOffset);
  assert(paused>.1);
  await page.locator('#browserPlayer').getByRole('button',{name:'Resume',exact:true}).click();
  console.log('resume requested');
  await page.waitForFunction(()=>browserVideo.currentTime>.1);
  await page.evaluate(()=>{browserSeek.value=3;browserSeek.onchange();});
  await page.waitForFunction(()=>browserOffset===3&&browserVideo.currentTime>.1);
  await page.locator('#browserPlayer').getByRole('button',{name:'Stop',exact:true}).click();
  assert.equal(await page.locator('#browserPlayer').isVisible(),false);
  assert.equal(commands.length,0);
  // Audio shares the browser controls but must use an audio stream.
  await page.evaluate(async id=>{await choose({id,name:'Audio fixture',kind:'audio'});},process.env.BROWSER_TEST_TOKEN);
  await page.click('#play');
  await page.waitForFunction(()=>browserVideo.currentTime>.1);
  assert.equal(await page.evaluate(()=>browserVideo.videoWidth),0);
  await page.locator('#browserPlayer').getByRole('button',{name:'Stop',exact:true}).click();
  if(process.env.BROWSER_SEASON_TOKEN){
    await page.evaluate(async id=>{await choose({id,name:'Season fixture',kind:'video'});},process.env.BROWSER_SEASON_TOKEN);
    await page.click('#play');
    await page.waitForFunction(id=>browserItem?.id===id&&browserVideo.currentTime>.1,process.env.BROWSER_NEXT_TOKEN);
    console.log('Browser clean EOF continued into next season');
    await page.locator('#browserPlayer').getByRole('button',{name:'Stop',exact:true}).click();
  }
  await page.evaluate(()=>browserReports);
  assert(reports.some(r=>r.state==='playing'));assert(reports.some(r=>r.state==='paused'));assert(reports.some(r=>r.state==='stopped'));
  assert(reports.every(r=>r.client.startsWith('browser-')&&r.sequence>0));
  await page.selectOption('#playbackTarget','xbox');await page.click('#play');
  const xboxPlay=await page.evaluate(()=>api('/api/xbox/remote?after=0'));
  assert.equal(xboxPlay.action,'play');assert(xboxPlay.id);
  assert.equal(commands.length,0);
  await page.evaluate(()=>command('seek',{seconds:3}));
  const xboxSeek=await page.evaluate(()=>api('/api/xbox/remote?after=0'));
  assert.equal(xboxSeek.action,'seek');assert.equal(xboxSeek.seconds,3);
  assert.deepEqual(await page.evaluate(seq=>api('/api/xbox/remote?after='+seq),xboxSeek.sequence),{});
  await page.evaluate(async token=>{
    await post('/api/client-playback',{client:'xbox-webtest',sequence:1,id:token,name:'Xbox active item',kind:'video',state:'playing',position:2000,duration:6000});
    await api('/api/xbox/remote?after=0');clearMedia();setView('remote');await refreshPlayer(true);
  },process.env.BROWSER_TEST_TOKEN);
  assert.equal(await page.locator('#playbackTarget').isVisible(),true);
  assert.equal(await page.evaluate(()=>selected?.id),process.env.BROWSER_TEST_TOKEN);
  assert.equal(await page.evaluate(()=>playerSample.position),2);
  assert.equal(await page.locator('#pspController').isVisible(),false);
  assert.equal(commands.length,0);
  await page.selectOption('#playbackTarget','psp');await page.click('#play');
  assert.equal(commands.length,1);
  assert.deepEqual(errors,[]);
  console.log('Actual browser video/audio, pause/resume/seek/stop and PSP target isolation: passed');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
