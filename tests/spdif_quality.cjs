// Focused visibility test; no browser, network or server required.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('static/browser-player.js','utf8');
const begin=source.indexOf('function xboxQualityVisibility(){');
const end=source.indexOf('\nconst xboxChooseMedia',begin);
const elements={};
const element=id=>elements[id]??=( {value:'',hidden:false,closest(){return this;}} );
const c={browserTarget:{value:'psp'},selected:{kind:'video'},playerSample:null,
    xboxQualityField:{},xboxAudioField:{},downloadAudioLabel:{},downloadAudioSelect:{},$:element};
vm.createContext(c);vm.runInContext(source.slice(begin,end),c);
element('#audio_quality').value='v5';
for(const output of ['spdif_pcm','spdif_auto_pcm','spdif_auto_ac3']) {
    element('#audio_output').value=output;c.xboxQualityVisibility();
    assert(element('#audio_quality').hidden);assert(!c.downloadAudioLabel.hidden);
    assert.equal(c.downloadAudioSelect.value,'v5');
}
element('#audio_output').value='';
c.playerSample={online:true,audio_output:'spdif_auto_ac3'};c.xboxQualityVisibility();
assert(element('#audio_quality').hidden);
c.playerSample.online=false;c.xboxQualityVisibility();assert(!element('#audio_quality').hidden);
element('#audio_output').value='psp';c.xboxQualityVisibility();assert(!element('#audio_quality').hidden);
c.browserTarget.value='browser';element('#audio_output').value='spdif_pcm';
c.xboxQualityVisibility();assert(!element('#audio_quality').hidden);
c.browserTarget.value='xbox';c.xboxQualityVisibility();assert(element('#audio_quality').hidden);
c.browserTarget.value='psp';c.selected.live=true;c.xboxQualityVisibility();assert(c.downloadAudioLabel.hidden);
console.log('PASS: optical quality, PSP-reported default, offline quality, browser/Xbox isolation');
