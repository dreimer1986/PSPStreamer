// Real Python server + browser login smoke test; no personal server/config is used.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {spawn}=require('node:child_process');
(async()=>{
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'psp-web-login-'));
  fs.mkdirSync(path.join(temporary,'Media'));
  const server=spawn('python3',['-u','-c',
    "from psp_streamer.server import AppServer,Library; import os; from pathlib import Path; s=AppServer(('127.0.0.1',0),Library([Path(os.environ['MEDIA_ROOTS'])])); print(s.server_address[1]); s.serve_forever()"],
    {cwd:path.resolve(__dirname,'..'),env:{...process.env,MEDIA_ROOTS:path.join(temporary,'Media'),
      PSP_STREAMER_PASSWORD:'browser-test-password',PSP_STREAMER_SETTINGS_DIR:temporary,
      PSP_STREAMER_DOWNLOAD_DIR:path.join(temporary,'downloads'),PSP_STREAMER_RADIO_DIR:temporary,
      PSP_STREAMER_TLS_CERT:'',PSP_STREAMER_TLS_KEY:''}});
  let browser;
  try{
    const port=await new Promise((resolve,reject)=>{
      server.stdout.once('data',data=>resolve(Number(data.toString().trim())));
      server.once('error',reject);server.once('exit',code=>reject(Error('Server exited '+code)));
    });
    browser=await chromium.launch({headless:true});
    const page=await browser.newPage({viewport:{width:800,height:850},locale:'de'}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    const base='http://127.0.0.1:'+port;
    await page.goto(base+'/');await page.waitForURL('**/login');
    await page.getByRole('button',{name:'Anmelden',exact:true}).waitFor();
    await page.screenshot({path:'/tmp/psp-web-login.png',fullPage:true});
    await page.fill('#password','wrong');await page.click('#loginForm button');
    await page.getByText('Falsches Passwort',{exact:true}).waitFor();
    await page.fill('#password','browser-test-password');await page.click('#loginForm button');
    await page.getByRole('button',{name:'▸ Dateien',exact:true}).waitFor();
    const cookies=await page.context().cookies();
    assert.equal(cookies.find(c=>c.name==='psp_session').httpOnly,true);
    await page.getByRole('button',{name:'▸ Dateien',exact:true}).click();
    await page.getByRole('button',{name:'▸ Media',exact:true}).click();
    await page.getByText('Hier sind keine Dateien.',{exact:true}).waitFor();
    await page.click('#parent');await page.getByRole('button',{name:'▸ Media',exact:true}).waitFor();
    await page.click('#parent');await page.getByRole('button',{name:'▸ Internet Radio',exact:true}).waitFor();
    await page.click('[data-view=remote]');
    await page.click('[data-action=stop]');await page.getByText('Gesendet: Stopp',{exact:true}).waitFor();
    await page.click('[data-view=settings]');
    await page.fill('#oldPassword','browser-test-password');
    await page.fill('#newPassword','replacement-test-password');await page.fill('#repeatPassword','replacement-test-password');
    page.once('dialog',dialog=>dialog.accept());await page.click('#passwordForm button');
    await page.waitForURL('**/login');await page.fill('#password','replacement-test-password');await page.click('#loginForm button');
    await page.getByRole('button',{name:'▸ Dateien',exact:true}).waitFor();
    await page.click('#logout');await page.waitForURL('**/login');
    const unauthorized=await page.request.get(base+'/api/health',{headers:{'X-PSP-Web':'1'}});
    assert.equal(unauthorized.status(),401);
    const basic=Buffer.from('psp:replacement-test-password').toString('base64');
    const authorized=await page.request.get(base+'/api/health',{headers:{Authorization:'Basic '+basic}});
    assert.equal(authorized.status(),200);
    assert.deepEqual(errors,[]);
    console.log('Real-server browser checks passed: German login, folder round trip, remote command, password change, logout, PSP Basic.');
  }finally{
    if(browser)await browser.close();
    server.kill('SIGTERM');await new Promise(resolve=>server.exitCode!==null?resolve():server.once('exit',resolve));
    fs.rmSync(temporary,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exit(1)});
