const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
(async()=>{
 const browser=await chromium.launch({headless:true});
 try{
  for(const width of [430,1100]){
   const page=await browser.newPage({viewport:{width,height:900},locale:'en'}),commands=[],errors=[];
   let dialog=0;
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('**/*',async route=>{
    const u=new URL(route.request().url());
    if(u.pathname==='/api/input/status')return route.fulfill({json:{online:true,client:'psp-browser-test',dialog,capacity:32,secret:dialog===2}});
    if(u.pathname==='/api/input'){commands.push(route.request().postDataJSON());return route.fulfill({json:{ok:true}});}
    if(u.pathname==='/')return route.fulfill({contentType:'text/html',body:`<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><link rel="stylesheet" href="/web.css"><section id="view-remote"></section><script>const $=s=>document.querySelector(s);let view='remote',csrf='test';function t(s){return s}async function api(u,o={}){return (await fetch(u,o)).json()}</script><link rel="stylesheet" href="/controller.css"><script src="/controller.js"></script>`});
    return route.fulfill({contentType:u.pathname.endsWith('.js')?'text/javascript':'text/css',body:fs.readFileSync(path.join(__dirname,'../static',u.pathname))});
   });
   await page.goto('http://psp.test/');await page.locator('summary').click();
   await page.getByText('PSP connected',{exact:true}).waitFor();
   const cross=page.locator('.key-cross'),r=await cross.boundingBox();
   await page.mouse.move(r.x+r.width/2,r.y+r.height/2);await page.mouse.down();await page.waitForTimeout(350);await page.mouse.up();
   await page.waitForTimeout(100);assert(commands.some(x=>x.mask===16384));assert.equal(commands.at(-1).mask,0);
   await page.locator('#padLatchL').check();await cross.click();await page.waitForTimeout(100);
   assert(commands.some(x=>x.mask===(256|16384)));await page.locator('#padRelease').click();
   dialog=1;await page.locator('#padText:not([disabled])').waitFor();await page.locator('#padText').fill('Grüße');await page.locator('#padTextSend').click();
   await page.waitForTimeout(100);assert(commands.some(x=>x.text==='Grüße'&&x.dialog===1));
   dialog=2;await page.waitForFunction(()=>document.querySelector('#padText').type==='password');
   assert.equal(await page.locator('#padText').inputValue(),'');
   await page.locator('.psp-pad').focus();await page.keyboard.down('ArrowDown');await page.waitForTimeout(100);
   await page.evaluate(()=>{window.dispatchEvent(new Event('blur'))});await page.waitForTimeout(100);
   assert.equal(commands.at(-1).mask,0);await page.keyboard.up('ArrowDown');
   assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
   assert.deepEqual(errors,[]);await page.screenshot({path:`/tmp/psp-controller-${width}.png`,fullPage:true});await page.close();
  }
  console.log('Controller desktop/mobile, hold/combo/release and UTF-8/password fields: OK');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
