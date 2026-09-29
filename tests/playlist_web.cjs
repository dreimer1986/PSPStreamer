// Focused DOM contract harness; no server, sockets or unrelated UI suites.
const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
class Element {
 constructor(){this.children=[];this.events={};this.value='';this.dataset={};}
 append(...items){this.children.push(...items);}
 after(){}
 replaceChildren(...items){this.children=items;}
 setAttribute(){}
 addEventListener(name,fn){this.events[name]=fn;}
 contains(item){return this===item||this.children.some(child=>child instanceof Element&&child.contains(item));}
}
const elements=new Map(),requests=[];
let state={revision:7,enabled:true,repeat:0,shuffle:false,items:[{id:'a',name:'Episode',kind:'video'},{id:'b',name:'Music',kind:'audio'}],next:{id:'b',name:'Music'}};
const context=vm.createContext({console,Map,Set,Number,String,document:{createElement:()=>new Element(),activeElement:null},
 $:key=>{if(!elements.has(key))elements.set(key,new Element());return elements.get(key);},
 t:s=>s,button:(title,fn)=>{const e=new Element();e.textContent=title;e.click=fn;return e;},
 option:(e,value,title)=>e.append({value,title}),qualityOptions:e=>e.append({value:'v5'}),
 setView(){},setInterval(){},renderLibrary(){},view:'library',listing:null,path:'',checked:new Map(),
 selected:{id:'a',kind:'video'},media:{},playerSample:{id:'a'},activePlayer:()=>true,
 message(){},fail:e=>{throw e;},confirm:()=>true,
 api:async url=>{requests.push(url);return url.includes('/duration?')?{duration:url.endsWith('a')?120:null}:structuredClone(state);},
 post:async(url,body)=>{requests.push(body);state={...state,revision:state.revision+1};return structuredClone(state);}
});
vm.runInContext(fs.readFileSync(require('node:path').join(__dirname,'../static/playlist.js'),'utf8'),context);
const run=code=>vm.runInContext(code,context);
(async()=>{
 await run('loadPlaylist()');
 assert.match(run('playlistSummary.textContent'),/0:00:00 \+ 2 unknown durations/);
 assert.match(run('playlistSummary.textContent'),/Next: Music/);
 run('view="playlist"');await run('playlistLoadDurations()');
 assert.match(run('playlistSummary.textContent'),/0:02:00 \+ 1 unknown durations/);
 assert.equal(requests.filter(x=>typeof x==='string'&&x.includes('/duration?')).length,2);
 await run('playlistLoadDurations()'); // Unknown does not cause endless probes.
 assert.equal(requests.filter(x=>typeof x==='string'&&x.includes('/duration?')).length,2);
 assert.equal(run('playlistRows.children[0].children[5].hidden'),false);
 assert.equal(run('playlistRows.children[1].children[5].hidden'),true);
 run('playlistRows.children[0].children[4].value="v5";playlistRows.children[0].children[5].value="24000/1001"');
 await run('playlistRows.children[0].children[4].onchange()');
 assert.deepEqual(JSON.parse(JSON.stringify(requests.at(-1))),{action:'quality',id:'a',audio_quality:'v5',video_fps:'24000/1001',revision:7});
 run('playlistDragging="b";playlistRows.children[0].events.drop({preventDefault(){}})');
 await new Promise(resolve=>setImmediate(resolve));
 assert.equal(requests.at(-1).action,'move');assert.equal(requests.at(-1).position,0);assert.equal(requests.at(-1).id,'b');
 const revision=run('playlistState.revision');state={...state,revision:revision+1};
 run('playlistDragging="a"');await run('loadPlaylist()');assert.equal(run('playlistState.revision'),revision);
 run('playlistDragging=null;document.activeElement=playlistRows.children[0].children[4]');await run('loadPlaylist()');assert.equal(run('playlistState.revision'),revision);
 run('document.activeElement=null');await run('loadPlaylist()');assert.equal(run('playlistState.revision'),revision+1);
 console.log('Playlist web: duration, next preview, quality, audio-only, drag revision and focus protection passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
