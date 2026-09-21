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
  let match=options.find(o=>o.textContent===wanted);
  if(!match && (wanted==='Off'||wanted==='Aus'))match=options.find(o=>o.value==='-1'||o.value==='off');
  if(!match && lang(wanted)!=='und'){
    const same=options.filter(o=>lang(o.textContent)===lang(wanted));
    // Preserve old plain titles only when the enriched match is unambiguous.
    const title=wanted.trim().split(/\s+/).slice(1).join(' ');
    const legacy=same.filter(o=>title && o.textContent.split(' | ').slice(1).join(' | ')===title);
    if(legacy.length===1)match=legacy[0];else if(same.length===1)match=same[0];
  }
  if(match){select.value=match.value;return true;}return false;
}
