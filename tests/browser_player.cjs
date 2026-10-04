const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true});
 try {
  const page=await browser.newPage();const errors=[],commands=[];
  page.on('pageerror',e=>errors.push(e.message));
  page.on('request',r=>{if(r.url().includes('/api/remote/command'))commands.push(r);});
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
  await page.selectOption('#playbackTarget','psp');await page.click('#play');
  assert.equal(commands.length,1);
  assert.deepEqual(errors,[]);
  console.log('Actual browser video/audio, pause/resume/seek/stop and PSP target isolation: passed');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
