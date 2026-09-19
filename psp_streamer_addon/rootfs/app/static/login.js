fetch('/api/session',{headers:{'X-PSP-Web':'1'},credentials:'same-origin'}).then(r=>{if(r.ok)location.replace('/')}).catch(()=>{});
document.querySelector('#loginForm').onsubmit=async event=>{
  event.preventDefault();const button=event.target.querySelector('button');button.disabled=true;
  try{
    const response=await fetch('/api/login',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json','X-PSP-Web':'1'},body:JSON.stringify({password:document.querySelector('#password').value})});
    const data=await response.json();if(!response.ok)throw Error(t(data.error));
    document.querySelector('#password').value='';location.replace('/');
  }catch(error){document.querySelector('#loginStatus').textContent=error.message;button.disabled=false;}
};
