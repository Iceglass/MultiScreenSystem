/* clock */
function pad2(n) { return n < 10 ? '0' + n : '' + n }
function fmtDate(d) { return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate()) }
function tickClock() {
    var d = new Date();
    var c = document.getElementById('clock'),
        dt = document.getElementById('date');
    if (c) c.textContent = pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
    if (dt) dt.textContent = fmtDate(d);
}
setInterval(tickClock, 1000);
addEventListener('load', tickClock);

/* api */
function postJSON(u, b) {
    return fetch(u, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(b || {})
    }).then(r => r.json())
}
function apiAddOld(n, u, d) { return postJSON('/api/streams', { name: n, url: u, decoder: d }).then(j => !!(j && j.ok)) }
function apiDelOld(n) { return fetch('/api/streams/' + encodeURIComponent(n), { method: 'DELETE' }).then(r => r.json()).then(j => !!(j && j.ok)) }
function apiStartOld(n) { return fetch('/api/streams/' + encodeURIComponent(n) + '/start', { method: 'POST' }).then(r => r.json()).then(j => !!(j && j.ok)) }
function apiStopOld(n) { return fetch('/api/streams/' + encodeURIComponent(n) + '/stop', { method: 'POST' }).then(r => r.json()).then(j => !!(j && j.ok)) }
function apiRestartOld(n) { return fetch('/api/streams/' + encodeURIComponent(n) + '/restart', { method: 'POST' }).then(r => r.json()).then(j => !!(j && j.ok)) }
function apiAdd(n, u, d) { return postJSON('/api/stream/add', { name: n, url: u }).then(j => !!(j && j.ok)) }
function apiDel(n) { return postJSON('/api/stream/delete', { name: n }).then(j => !!(j && j.ok)) }
function apiStart(n) { return postJSON('/api/stream/start', { name: n }).then(j => !!(j && j.ok)) }
function apiStop(n) { return postJSON('/api/stream/stop', { name: n }).then(j => !!(j && j.ok)) }
function apiRestart(n) { return postJSON('/api/stream/restart', { name: n }).then(j => !!(j && j.ok)) }
var USE_OLD = false;
function pickApi() {
    return fetch('/api/settings', { cache: 'no-store' })
        .then(r => { USE_OLD = !r.ok })
        .catch(() => { USE_OLD = true })
}

/* table render */
var THRESH = { fps: { warn_ratio: 0.70, crit_ratio: 0.40 }, bitrate: { warn_kbps: 300, crit_kbps: 100 } };
var ROWS = new Map(), STATE = new Map(), RESTART_UNTIL = new Map();
function td(t, c) { var e = document.createElement('td'); if (c) e.className = c; e.textContent = (t == null ? '' : t); return e }
function num(x) { x = Number(x || 0); return x.toFixed(1) }
function int0(x) { x = Number(x || 0); return (x | 0) }
function actionsCell(s) {
    var tdA = document.createElement('td'), wrap = document.createElement('div'); wrap.className = 'actions';
    function b(t, c, a) { var x = document.createElement('button'); x.type = 'button'; x.textContent = t; x.className = 'btn ' + (c || ''); if (a) x.dataset.act = a; return x }
    wrap.appendChild(b('INFO', 'secondary', 'info'));
    wrap.appendChild(b('Start/Stop', '', 'toggle'));
    wrap.appendChild(b('Restart', '', 'restart'));
    wrap.appendChild(b('Edit', 'secondary', 'edit'));
    wrap.appendChild(b('Delete', 'danger', 'delete'));
    tdA.appendChild(wrap); return tdA;
}
function wireRowActions(tr, s) {
    var q = a => tr.querySelector('[data-act="' + a + '"]');
    var i = q('info'), t = q('toggle'), r = q('restart'), e = q('edit'), d = q('delete');
    if (i) i.onclick = () => showInfo(s.name);
    if (t) { t.textContent = s.running ? 'Stop' : 'Start'; t.onclick = () => onToggle(s.name, s.running) }
    if (r) r.onclick = () => onRestart(s.name);
    if (e) e.onclick = () => onEdit(s.name, s.url, s.decoder);
    if (d) d.onclick = () => onDelete(s.name);
}
function ensureRow(s, idx) {
    STATE.set(s.name, s);
    var tr = ROWS.get(s.name);
    if (!tr) {
        tr = document.createElement('tr'); tr.dataset.name = s.name;
        tr.appendChild(td(idx)); tr.appendChild(td(s.name, 'name')); tr.appendChild(td(s.running ? 'true' : 'false'));
        tr.appendChild(td(num(s.input_fps), 'num')); tr.appendChild(td(num(s.decode_fps), 'num')); tr.appendChild(td(num(s.render_fps), 'num'));
        tr.appendChild(td(int0(s.bitrate_kbps))); tr.appendChild(td(int0(s.video_kbps))); tr.appendChild(td(int0(s.audio_kbps)));
        tr.appendChild(td(s.rate_mode || '')); var cc = td(int0(s.cc_errors)); if ((s.cc_errors | 0) > 0) cc.classList.add('cell-cc-bad'); tr.appendChild(cc);
        tr.appendChild(td(s.decoder || '')); tr.appendChild(td(s.last_error || '')); tr.appendChild(actionsCell(s));
        document.getElementById('tb').appendChild(tr); ROWS.set(s.name, tr);
    } else {
        var t = tr.children; t[0].textContent = idx; t[1].textContent = s.name; t[2].textContent = s.running ? 'true' : 'false';
        t[3].textContent = num(s.input_fps); t[4].textContent = num(s.decode_fps); t[5].textContent = num(s.render_fps);
        t[6].textContent = int0(s.bitrate_kbps); t[7].textContent = int0(s.video_kbps); t[8].textContent = int0(s.audio_kbps);
        t[9].textContent = s.rate_mode || ''; t[10].textContent = int0(s.cc_errors);
        if ((s.cc_errors | 0) > 0) t[10].classList.add('cell-cc-bad'); else t[10].classList.remove('cell-cc-bad');
        t[11].textContent = s.decoder || ''; t[12].textContent = s.last_error || ''; wireRowActions(tr, s);
    }
    // state logic:
    var inF = Number(s.input_fps || 0), deF = Number(s.decode_fps || 0), br = Number(s.bitrate_kbps || 0);
    var ratio = inF > 0 ? (deF / inF) : 1.0;
    var dead = (s.running === true) && inF <= 0 && deF <= 0 && br <= 0;
    var isCrit = (ratio <= (THRESH.fps.crit_ratio || 0.40)) || (br <= (THRESH.bitrate.crit_kbps || 100)) || ((s.status || '') === 'crit');
    var isWarn = !isCrit && ((ratio <= (THRESH.fps.warn_ratio || 0.70)) || (br <= (THRESH.bitrate.warn_kbps || 300)) || ((s.status || '') === 'warn'));
    tr.className = tr.className.replace(/\brow-(running|restarting|warn|crit|dead)\b/g, '').trim();
    if (dead) { tr.classList.add('row-dead'); }
    else if ((RESTART_UNTIL.get(s.name) || 0) > Date.now()) { tr.classList.add('row-restarting'); }
    else if (isCrit) { tr.classList.add('row-crit'); }
    else if (isWarn) { tr.classList.add('row-warn'); }
    else if (s.running) { tr.classList.add('row-running'); }
}
/* actions */
function askDecoder(cur) { var v = prompt('Decoder (auto/cpu/cuda/dxva2):', cur || 'auto'); return v || 'auto' }
async function onAdd() { var n = prompt('Channel name:'); if (!n) return; var u = prompt('Channel URL:'); if (!u) return; var d = askDecoder('auto'); var ok = USE_OLD ? await apiAddOld(n.trim(), u.trim(), d) : await apiAdd(n.trim(), u.trim(), d); if (!ok) alert('Add failed') }
async function onEdit(name, url, dec) { var u = prompt('New URL for "' + name + '":', url || ''); if (!u) return; var d = askDecoder(dec || 'auto'); var ok = USE_OLD ? await apiAddOld(name, u.trim(), d) : await apiAdd(name, u.trim(), d); if (!ok) alert('Edit failed') }
async function onDelete(name) { if (!confirm('Delete "' + name + '"?')) return; var ok = USE_OLD ? await apiDelOld(name) : await apiDel(name); if (!ok) { alert('Delete failed'); return } var tr = ROWS.get(name); if (tr) { tr.remove(); ROWS.delete(name); STATE.delete(name) } }
async function onToggle(name, run) {
    var ok = run ? (USE_OLD ? await apiStopOld(name) : await apiStop(name))
        : (USE_OLD ? await apiStartOld(name) : await apiStart(name)); if (!ok) alert((run ? 'Stop' : 'Start') + ' failed')
}
async function onRestart(name) { var ok = USE_OLD ? await apiRestartOld(name) : await apiRestart(name); if (!ok) { alert('Restart failed'); return } RESTART_UNTIL.set(name, Date.now() + 4000) }
/* modal */
function openModal() { var o = document.getElementById('modal'); o.style.display = 'flex'; o.setAttribute('aria-hidden', 'false') }
function closeModal() { var o = document.getElementById('modal'); o.style.display = 'none'; o.setAttribute('aria-hidden', 'true') }
function addRow(tb, k, v) { var tr = document.createElement('tr'); var th = document.createElement('th'); th.textContent = k; var td = document.createElement('td'); td.textContent = (v == null || v == '') ? '-' : String(v); tr.appendChild(th); tr.appendChild(td); tb.appendChild(tr) }
function showInfo(name) {
    var s = STATE.get(name); if (!s) return;
    document.getElementById('modal-title').textContent = 'Stream: ' + name;
    var tb = document.getElementById('info-body'); tb.innerHTML = '';
    var ratio = (s.input_fps > 0) ? (s.decode_fps / s.input_fps) : 1.0;
    addRow(tb, 'Name', s.name); addRow(tb, 'Service', s.service_name); addRow(tb, 'URL', s.url);
    addRow(tb, 'Running', s.running ? 'true' : 'false'); addRow(tb, 'Decoder', s.decoder);
    addRow(tb, 'Input FPS', num(s.input_fps)); addRow(tb, 'Decode FPS', num(s.decode_fps)); addRow(tb, 'FPS ratio', ratio.toFixed(2));
    addRow(tb, 'Render FPS', num(s.render_fps)); addRow(tb, 'Bitrate kbps', int0(s.bitrate_kbps)); addRow(tb, 'Video kbps', int0(s.video_kbps)); addRow(tb, 'Audio kbps', int0(s.audio_kbps));
    addRow(tb, 'Rate mode', s.rate_mode); addRow(tb, 'CC errors', int0(s.cc_errors));
    addRow(tb, 'SID', int0(s.sid)); addRow(tb, 'PMT', int0(s.pmt_pid)); addRow(tb, 'PCR', int0(s.pcr_pid)); addRow(tb, 'Video PID', int0(s.video_pid)); addRow(tb, 'Audio PIDs', (s.audio_pids == null || s.audio_pids == '') ? '-' : s.audio_pids);
    addRow(tb, 'Status', s.status || 'ok'); addRow(tb, 'Last error', s.last_error || '-'); openModal();
}
/* reload & init */
async function reload() {
    try {
        var r = await fetch('/api/streams', { cache: 'no-store' }); if (!r.ok) { console.error('HTTP ' + r.status); return }
        var arr = await r.json(), tb = document.getElementById('tb');
        if (!Array.isArray(arr)) { tb.innerHTML = '<tr><td colspan="14">Bad JSON</td></tr>'; return }
        if (tb.children.length === 0) { tb.innerHTML = '' }
        arr.forEach((s, i) => ensureRow(s, i + 1));
        arr.forEach(s => { var tr = ROWS.get(s.name); if (tr) wireRowActions(tr, s) })
    } catch (e) { console.error(e) }
}
addEventListener('load', function () {
    document.getElementById('btnAdd').addEventListener('click', function () { onAdd().then(reload) });
    document.getElementById('modal-close').addEventListener('click', closeModal);
    document.getElementById('modal-x').addEventListener('click', closeModal);
    document.getElementById('modal').addEventListener('click', e => { if (e.target === e.currentTarget) closeModal() });
    pickApi().then(function () { reload(); setInterval(reload, 1000); });
});
