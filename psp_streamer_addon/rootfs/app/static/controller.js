/* No video mirroring: the PSP/TV remains the screen. Each press/release is
 * ordered; held inputs renew a short server lease and never survive reloads. */
(() => {
  const panel=document.createElement('details');panel.id='pspController';
  panel.innerHTML=`<summary>${t('PSP buttons and text input')}</summary>
    <p>${t('Controls work inside PSPStreamer only. Hold buttons to repeat; touch supports combinations.')}</p>
    <div class="psp-pad" tabindex="0" aria-label="PSP controller">
      <div class="psp-screen"><strong>PSP STREAMER</strong><span id="padStatus" role="status"></span><small>${t('Watch the PSP or TV screen')}</small></div>
      <div class="psp-analog" tabindex="0" role="group" aria-label="Analog stick"><span></span></div>
    </div>
    <div class="controls"><label><input id="padLatchL" type="checkbox">${t('Hold L')}</label><label><input id="padLatchR" type="checkbox">${t('Hold R')}</label><button id="padRelease">${t('Release all buttons')}</button></div>
    <p class="muted">${t('Keyboard: arrows, Z = cross, X = circle, A = square, S = triangle, Q/E = L/R, Enter = START, Shift = SELECT. Focus the PSP first.')}</p>
    <form id="padTextForm"><label>${t('Text for the open PSP field')}<input id="padText" type="text" autocomplete="off" autocapitalize="off" spellcheck="false" disabled></label><button id="padTextSend" disabled>${t('Send text')}</button><p id="padTextStatus" role="status">${t('Open a text field on the PSP first')}</p></form>`;
  $('#view-remote').append(panel);
  const pad=panel.querySelector('.psp-pad'),status=$('#padStatus'),text=$('#padText');
  const keys=[['up','↑',16],['down','↓',64],['left','←',128],['right','→',32],['triangle','△',4096],['circle','○',8192],['cross','×',16384],['square','□',32768],['l','L',256],['r','R',512],['select','SELECT',1],['start','START',8]];
  const owner=crypto.randomUUID?crypto.randomUUID():`pad-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  const held=new Map();let serial=0,chain=Promise.resolve(),pending=0,field=0,client='',capacity=0,online=false,x=128,y=128,stickPointer=null;
  function snapshot(){let mask=0;for(const bit of held.values())mask|=bit;if($('#padLatchL').checked)mask|=256;if($('#padLatchR').checked)mask|=512;return {mask,x,y};}
  function paint(){const s=snapshot();for(const b of pad.querySelectorAll('[data-bit]'))b.setAttribute('aria-pressed',String(!!(s.mask&Number(b.dataset.bit))));pad.querySelector('.psp-analog span').style.transform=`translate(${(x-128)/7}px,${(y-128)/7}px)`;}
  function send(data){
    const body={owner,serial:++serial,...data},queued=performance.now();pending++;
    const task=chain.then(async()=>{if(!('text' in data)&&performance.now()-queued>750&&body.mask)return;const abort=new AbortController(),timeout=setTimeout(()=>abort.abort(),1800);try{return await api('/api/input',{method:'POST',body:JSON.stringify(body),signal:abort.signal});}finally{clearTimeout(timeout);}});
    chain=task.catch(e=>{status.textContent=t(e.name==='AbortError'?'PSP input is offline':e.message);}).finally(()=>pending--);
    return task;
  }
  function update(){paint();send(snapshot()).catch(()=>{});}
  function release(){const before=snapshot();held.clear();x=y=128;stickPointer=null;$('#padLatchL').checked=$('#padLatchR').checked=false;paint();if(before.mask||before.x!==128||before.y!==128||pending)send(snapshot()).catch(()=>{});}
  for(const [name,label,bit] of keys){
    const b=document.createElement('button');b.type='button';b.className='psp-key key-'+name;b.textContent=label;b.dataset.bit=bit;b.setAttribute('aria-label',name.toUpperCase());b.setAttribute('aria-pressed','false');pad.append(b);
    b.onpointerdown=e=>{if(!online)return;e.preventDefault();b.focus({preventScroll:true});b.setPointerCapture(e.pointerId);held.set('p'+e.pointerId,bit);update();};
    const up=e=>{if(held.delete('p'+e.pointerId))update();};b.onpointerup=up;b.onpointercancel=up;b.onlostpointercapture=up;
    b.onclick=e=>{if(e.detail===0&&online){held.set('accessible',bit);update();setTimeout(()=>{held.delete('accessible');update();},140);}};
  }
  const keyboard={ArrowUp:16,ArrowDown:64,ArrowLeft:128,ArrowRight:32,KeyZ:16384,KeyX:8192,KeyA:32768,KeyS:4096,KeyQ:256,KeyE:512,Enter:8,ShiftLeft:1,ShiftRight:1};
  pad.onkeydown=e=>{const bit=keyboard[e.code];if(!bit||!online)return;e.preventDefault();if(!e.repeat){held.set('k'+e.code,bit);update();}};
  pad.onkeyup=e=>{if(keyboard[e.code])e.preventDefault();if(held.delete('k'+e.code))update();};
  document.addEventListener('keyup',e=>{if(held.delete('k'+e.code))update();});
  const stick=pad.querySelector('.psp-analog');
  function move(e){const r=stick.getBoundingClientRect();x=Math.round(Math.max(0,Math.min(255,128+(e.clientX-r.left-r.width/2)*256/r.width)));y=Math.round(Math.max(0,Math.min(255,128+(e.clientY-r.top-r.height/2)*256/r.height)));paint();}
  stick.onpointerdown=e=>{if(!online||stickPointer!==null)return;e.preventDefault();stickPointer=e.pointerId;stick.setPointerCapture(e.pointerId);move(e);update();};
  stick.onpointermove=e=>{if(e.pointerId===stickPointer)move(e);};
  const stickUp=e=>{if(e.pointerId===stickPointer){stickPointer=null;x=y=128;update();}};
  stick.onpointerup=stickUp;stick.onpointercancel=stickUp;stick.onlostpointercapture=stickUp;
  $('#padLatchL').onchange=$('#padLatchR').onchange=update;$('#padRelease').onclick=release;
  function active(){return panel.open&&view==='remote'&&!document.hidden;}
  let wasActive=false,lastHeartbeat=0;
  setInterval(()=>{const a=active();if(wasActive&&!a)release();wasActive=a;if(a&&online&&!pending&&performance.now()-lastHeartbeat>=500){lastHeartbeat=performance.now();send(snapshot()).catch(()=>{});}},100);
  window.addEventListener('blur',release);
  document.addEventListener('visibilitychange',()=>{if(document.hidden){release();text.value='';}});
  window.addEventListener('pagehide',()=>{fetch('/api/input',{method:'POST',credentials:'same-origin',keepalive:true,headers:{'Content-Type':'application/json','X-PSP-Web':'1','X-CSRF-Token':csrf},body:JSON.stringify({owner,serial:++serial,mask:0,x:128,y:128})}).catch(()=>{});});
  async function poll(){
    if(active())try{
      const s=await api('/api/input/status');online=s.online;
      status.textContent=t(online?'PSP connected':'PSP input is offline');
      if(field!==s.dialog||client!==s.client){text.value='';field=s.dialog;client=s.client;$('#padTextStatus').textContent=t(field?'Send replaces this field; confirm with START on the PSP controller.':'Open a text field on the PSP first');}
      capacity=s.capacity;text.type=s.secret?'password':'text';text.disabled=$('#padTextSend').disabled=!online||!field;
      if(capacity)text.maxLength=capacity-1;
    }catch(e){online=false;status.textContent=t(e.message);text.disabled=$('#padTextSend').disabled=true;}
    setTimeout(poll,600);
  }
  $('#padTextForm').onsubmit=async e=>{
    e.preventDefault();if(!online||!field)return;
    if(new TextEncoder().encode(text.value).length>=capacity){$('#padTextStatus').textContent=t('Text exceeds the PSP field capacity');return;}
    const value=text.value; text.value='';
    try{await send({text:value,dialog:field,client});$('#padTextStatus').textContent=t('Text sent. Check the PSP field, then confirm with START.');}catch(e){$('#padTextStatus').textContent=t(e.message);}
  };
  paint();poll();
})();
