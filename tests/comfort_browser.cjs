const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true});
 try{
  const page=await browser.newPage({viewport:{width:430,height:900},locale:'en'}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(process.argv[2]);
  await page.locator('#library .item').first().waitFor();
  await page.locator('#library button[aria-label="Add favorite"]').first().click();
  await page.getByRole('button',{name:'Favorites & history',exact:true}).click();
  await page.locator('#view-comfort .item').first().waitFor();
  assert((await page.locator('#view-comfort').innerText()).includes('Files'));
  await page.locator('#view-comfort button[aria-label="Remove favorite"]').first().click();
  await page.getByText('No entries yet.',{exact:true}).waitFor();
  await page.locator('nav [data-view="library"]').click();
  await page.getByRole('searchbox').fill('grüsse');
  await page.getByRole('button',{name:'Search all sources',exact:true}).click();
  await page.getByRole('button',{name:'[Files] Grüße Anime',exact:true}).waitFor();
  await page.getByRole('button',{name:'[Files] Grüße Anime',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#crumb').textContent.includes('Grüße Anime'));
  assert.deepEqual(errors,[]);
  console.log('Actual web UI: favorite add/remove, cross-source search and folder navigation passed');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
