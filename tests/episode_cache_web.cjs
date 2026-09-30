const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{})});
 try{
  const page=await browser.newPage({locale:'en'}),errors=[];
  page.setDefaultTimeout(5000);
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(process.argv[2]);
  await page.locator('#library .item').first().waitFor();
  await page.evaluate(()=>browse(':files:/0'));
  await page.locator('#library').getByRole('button',{name:/Episode 1/}).click();
  await page.getByLabel('Unwatched episodes to keep ready').fill('2');
  await page.getByRole('button',{name:'Reserve this series/folder',exact:true}).click();
  await page.getByRole('heading',{name:'Episode reserve',exact:true}).waitFor();
  await page.getByLabel('Server cache limit (MiB)').fill('256');
  await page.getByLabel('Automatically prepare unwatched episodes').check();
  await page.getByRole('button',{name:'Save',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#view-downloads').textContent.includes('/ 256 MiB'));
  const settings=await page.evaluate(()=>api('/api/offline/reserve'));
  assert.equal(settings.enabled,true);assert.equal(settings.rules[0].count,2);
  await page.route('**/api/provider-view?*',route=>{
   const url=new URL(route.request().url());
   return route.fulfill({contentType:'application/json',body:JSON.stringify(url.searchParams.get('view')==='sections'?{sections:[]}:
    {folders:[],videos:[],unavailable:[{name:'Unavailable film',reason:'Not available on the selected Plex server'}],next:null,page_size:8})});
  });
  await page.getByRole('button',{name:'Provider library',exact:true}).click();
  await page.getByLabel('View',{exact:true}).selectOption('watchlist');
  await page.getByText('Unavailable film — Not available on the selected Plex server',{exact:true}).waitFor();
  assert.equal(await page.getByLabel('Library',{exact:true}).isDisabled(),true);
  await page.getByLabel('Provider',{exact:true}).selectOption('jellyfin');
  assert.equal(await page.getByLabel('View',{exact:true}).inputValue(),'continue');
  assert.deepEqual(errors,[]);
  console.log('Episode reserve editor and separate Watchlist browser checks passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
