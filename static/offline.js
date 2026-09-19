function trackLanguage(value){const s=String(value||'und').toLowerCase();return ({de:'deu',ger:'deu',ja:'jpn',en:'eng',fr:'fra',fre:'fra',es:'spa'})[s]||s;}
function preferredTrack(tracks,wanted,subtitle=false){
  if(wanted==='off')return -1;
  if(!wanted||wanted==='first')return tracks.length?Number(tracks[0].n):(subtitle?-1:null);
  const matches=tracks.filter(x=>trackLanguage(x.l)===wanted.language);
  const exact=matches.filter(x=>(x.t||'')===(wanted.title||''));
  if(exact.length===1)return Number(exact[0].n);
  return matches.length===1?Number(matches[0].n):null;
}
function restoreTrack(select,wanted){
  if(!wanted)return true;
  const options=[...select.options],lang=s=>trackLanguage(s.trim().split(/\s+/)[0]);
  if(wanted==='First audio track'||wanted==='Erste Tonspur'){
    if(options.length){select.value=options[0].value;return true;}return false;
  }
  const match=options.find(o=>o.textContent===wanted)||((wanted==='Off'||wanted==='Aus')?options.find(o=>o.value==='-1'||o.value==='off'):
    lang(wanted)!=='und'&&options.find(o=>lang(o.textContent)===lang(wanted)));
  if(match){select.value=match.value;return true;}return false;
}
