const state = { root: 0, path: "", selected: null };
const $ = (selector) => document.querySelector(selector);

async function api(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error((await response.json()).error || response.statusText);
  return response.json();
}
function esc(value) {
  return String(value).replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
}
async function loadLibrary(root = state.root, path = state.path) {
  state.root = root; state.path = path; $('#player').hidden = true;
  const data = await api(`/api/library?root=${root}&path=${encodeURIComponent(path)}`);
  $('#location').textContent = `Quelle ${root + 1}${data.path ? ` / ${data.path}` : ''}`;
  const entries = [];
  if (data.parent !== null) entries.push(`<button class="folder" data-path="${esc(data.parent)}">← Zurück</button>`);
  data.folders.forEach(folder => entries.push(`<button class="folder" data-path="${esc(folder.path)}">📁 ${esc(folder.name)}</button>`));
  data.videos.forEach(video => entries.push(`<button class="video" data-id="${esc(video.id)}" data-name="${esc(video.name)}">▶ ${esc(video.name)} <small>${Math.round(video.bytes / 1048576)} MB</small></button>`));
  $('#library').innerHTML = entries.join('') || '<p>Keine Videos in diesem Ordner.</p>';
  document.querySelectorAll('.folder').forEach(button => button.onclick = () => loadLibrary(root, button.dataset.path));
  document.querySelectorAll('.video').forEach(button => choose(button.dataset.id, button.dataset.name));
}
async function choose(id, name) {
  state.selected = id; $('#title').textContent = name; $('#player').hidden = false;
  const data = await api(`/api/metadata/${id}`);
  const audio = data.a || [];
  const subtitles = data.s || [];
  $('#audio').innerHTML = audio.map((stream, index) => `<option value="${stream.n}">Spur ${index + 1} (${esc(stream.l || 'und')})</option>`).join('') || '<option value="0">Standard</option>';
  $('#subtitle').innerHTML = '<option value="-1">Aus</option>' + subtitles.map((stream, index) => `<option value="${stream.n}">Untertitel ${index + 1} (${esc(stream.l || 'und')})</option>`).join('');
  $('#player').scrollIntoView({behavior: 'smooth'});
}
$('#start').onclick = () => {
  const video = $('#video');
  video.src = `/api/transcode/${state.selected}?audio=${$('#audio').value}&subtitle=${$('#subtitle').value}&_=${Date.now()}`;
  video.play().catch(() => {});
};
loadLibrary().catch(error => $('#library').textContent = `Fehler: ${error.message}`);
