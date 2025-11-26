/* script.js
   - Keeps status (including effect + mode) in sync with /status.
   - Uses /wifiInfo to toggle AP vs STA info and show router details.
   - Ensures AI actions force a UI refresh.
*/

/* ---------------------- API helper ---------------------- */
async function api(path, params = {}, options = {}) {
  const method = params.__method || 'GET';
  const url = new URL(path, window.location.origin);

  if (method === 'GET') {
    Object.keys(params).forEach(k => { if (k !== '__method') url.searchParams.set(k, params[k]); });
    const res = await fetch(url.toString(), { cache: 'no-store', ...options });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  } else if (method === 'POST') {
    const body = new URLSearchParams();
    Object.keys(params).forEach(k => { if (k !== '__method') body.set(k, params[k]); });
    const res = await fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString(),
      cache: 'no-store',
      ...options
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }
  throw new Error(`Unsupported method ${method}`);
}

/* ---------------------- Effect list (for UI) ---------------------- */
const EFFECTS = [
  [0,"Static"],[1,"Blink"],[2,"Breath"],[3,"Color Wipe"],[4,"Color Wipe Inv"],[5,"Color Wipe Rev"],[6,"Color Wipe Rev Inv"],
  [7,"Color Wipe Random"],[8,"Random Color"],[9,"Single Dynamic"],[10,"Multi Dynamic"],[11,"Rainbow"],[12,"Rainbow Cycle"],
  [13,"Scan"],[14,"Dual Scan"],[15,"Fade"],[16,"Theater Chase"],[17,"Theater Chase Rainbow"],[18,"Running Lights"],
  [19,"Twinkle"],[20,"Twinkle Random"],[21,"Twinkle Fade"],[22,"Twinkle Fade Random"],[23,"Sparkle"],[24,"Flash Sparkle"],
  [25,"Hyper Sparkle"],[26,"Strobe"],[27,"Strobe Rainbow"],[28,"Multi Strobe"],[29,"Blink Rainbow"],[30,"Chase White"],
  [31,"Chase Color"],[32,"Chase Random"],[33,"Chase Rainbow"],[34,"Chase Flash"],[35,"Chase Flash Random"],
  [36,"Chase Rainbow White"],[37,"Chase Blackout"],[38,"Chase Blackout Rainbow"],[39,"Color Sweep Random"],
  [40,"Running Color"],[41,"Running Red Blue"],[42,"Running Random"],[43,"Larson Scanner"],[44,"Comet"],
  [45,"Fireworks"],[46,"Fireworks Random"],[47,"Merry Christmas"],[48,"Fire Flicker"],[49,"Fire Flicker (Soft)"],
  [50,"Fire Flicker (Intense)"],[51,"Circus Combustus"],[52,"Halloween"],[53,"Bicolor Chase"],[54,"Tricolor Chase"],[55,"ICU"]
];

/* ---------------------- Elements ---------------------- */
const els = {
  // Controls
  color: document.getElementById('colorPicker'),
  brightness: document.getElementById('brightness'),
  brightnessVal: document.getElementById('brightnessVal'),
  power: document.getElementById('powerToggle'),
  mimir: document.getElementById('mimirToggle'),
  effect: document.getElementById('effectSelect'),
  mimirMin: document.getElementById('mimirMin'),
  mimirMax: document.getElementById('mimirMax'),
  mimirMinVal: document.getElementById('mimirMinVal'),
  mimirMaxVal: document.getElementById('mimirMaxVal'),
  // Status
  colorSwatch: document.getElementById('colorSwatch'),
  colorText: document.getElementById('colorText'),
  brightnessBar: document.getElementById('brightnessBar'),
  brightnessText: document.getElementById('brightnessText'),
  luxText: document.getElementById('luxText'),
  luxBar: document.getElementById('luxBar'),
  stateBadge: document.getElementById('stateBadge'),
  wifiBadge: document.getElementById('wifiBadge'),
  effectBadge: document.getElementById('effectBadge'),
  mimirRangeText: document.getElementById('mimirRangeText'),
  mimirRangeBar: document.getElementById('mimirRangeBar'),
  // Wi‑Fi UI
  wifiModeLabel: document.getElementById('wifiModeLabel'),
  btnAP: document.getElementById('btnAP'),
  btnSTA: document.getElementById('btnSTA'),
  staSsid: document.getElementById('staSsid'),
  staPass: document.getElementById('staPass'),
  staInfo: document.getElementById('staInfo'),
  apInfo: document.getElementById('apInfo'),
  staInfoSsid: document.getElementById('staInfoSsid'),
  staInfoIp: document.getElementById('staInfoIp'),
  staInfoGw: document.getElementById('staInfoGw'),
  staInfoSubnet: document.getElementById('staInfoSubnet'),
  staInfoDns: document.getElementById('staInfoDns'),
  staInfoRssi: document.getElementById('staInfoRssi'),
  staInfoCh: document.getElementById('staInfoCh'),
  // AI
  aiPrompt: document.getElementById('aiPrompt'),
  aiAskBtn: document.getElementById('aiAskBtn'),
  aiMicBtn: document.getElementById('aiMicBtn'),
  aiCancelBtn: document.getElementById('aiCancelBtn'),
  aiLog: document.getElementById('aiLog'),
  aiDebugToggle: document.getElementById('aiDebugToggle'),
  aiDebugWrap: document.getElementById('aiDebugWrap'),
  aiDebugPre: document.getElementById('aiDebugPre'),
};

/* ---------------------- Helpers ---------------------- */
function populateEffects() {
  if (!els.effect) return;
  els.effect.innerHTML = '';
  for (const [id, name] of EFFECTS) {
    const o = document.createElement('option');
    o.value = String(id);
    o.textContent = `${id} - ${name}`;
    els.effect.appendChild(o);
  }
}
function pct(val, max) { return Math.max(0, Math.min(100, Math.round((val / max) * 100))); }

function effectNameFromId(id) {
  const match = EFFECTS.find(([eid]) => eid === Number(id));
  return match ? match[1] : 'Unknown';
}

function updateBadges(on, wifiMode, effectId) {
  els.stateBadge.textContent = on ? 'On' : 'Off';
  els.stateBadge.classList.remove('bg-success','bg-secondary');
  els.stateBadge.classList.add(on ? 'bg-success' : 'bg-secondary');

  els.wifiBadge.textContent = wifiMode;

  const name = effectNameFromId(effectId);
  els.effectBadge.textContent = `${effectId} - ${name}`;
}

function updateRangeUI(min, max) {
  els.mimirMin.value = min;
  els.mimirMax.value = max;
  els.mimirMinVal.textContent = String(min);
  els.mimirMaxVal.textContent = String(max);
  els.mimirRangeText.textContent = `${min} – ${max}`;
  els.mimirRangeBar.style.width = `${pct(max - min, 255)}%`;
}

/* ---------------------- Status update ---------------------- */
function updateUIStatus(status) {
  // Color
  els.color.value = `#${status.color}`;
  els.colorSwatch.style.background = `#${status.color}`;
  els.colorText.textContent = `#${status.color}`;

  // Brightness
  els.brightness.value = status.brightness;
  els.brightnessVal.textContent = `${status.brightness}`;
  els.power.checked = !!status.on;

  const p = pct(status.current_brightness, 255);
  els.brightnessBar.style.width = `${p}%`;
  els.brightnessText.textContent = `${status.brightness}/255 (${status.current_brightness})`;

  // Lux
  els.luxText.textContent = Number(status.lux).toFixed(2);
  els.luxBar.style.width = `${pct(Math.min(status.lux, 400), 400)}%`;

  // Mimir range
  const min = Number(status.mimir_min ?? 20);
  const max = Number(status.mimir_max ?? 220);
  updateRangeUI(min, max);

  // Effect + mode badges
  updateBadges(!!status.on, status.wifi_mode, status.effect_id);

  // Effect select
  const opt = [...els.effect.options].find(o => Number(o.value) === Number(status.effect_id));
  if (opt) els.effect.value = String(status.effect_id);
}

async function pollStatus() {
  try {
    const st = await api('/status');
    updateUIStatus(st);
  } catch (e) {
    console.warn('Status poll error', e);
  }
}

/* ---------------------- Wi‑Fi info handling ---------------------- */
function updateWifiInfoUI(info) {
  const mode = (info.mode || 'AP').toUpperCase();

  els.wifiModeLabel.textContent = mode;

  if (mode === 'STA') {
    // Show STA info, hide AP info
    els.staInfo.style.display = 'block';
    els.apInfo.style.display = 'none';

    els.staInfoSsid.textContent   = info.ssid   || '';
    els.staInfoIp.textContent     = info.ip     || '';
    els.staInfoGw.textContent     = info.gw     || '';
    els.staInfoSubnet.textContent = info.subnet || '';
    els.staInfoDns.textContent    = info.dns    || '';
    els.staInfoRssi.textContent   = (info.rssi    !== undefined) ? info.rssi    : '';
    els.staInfoCh.textContent     = (info.channel !== undefined) ? info.channel : '';
  } else {
    // AP or other modes: show AP info, hide STA info
    els.staInfo.style.display = 'none';
    els.apInfo.style.display  = 'block';
  }
}

async function pollWifiInfo() {
  try {
    const info = await api('/wifiInfo');
    updateWifiInfoUI(info);
  } catch (e) {
    console.warn('wifiInfo poll error', e);
  }
}

/* ---------------------- Direct control wrappers ---------------------- */
async function applyColor(hex)       { await api('/setColor',      { hex }); }
async function applyBrightness(val)  { await api('/setBrightness',  { value: val }); }
async function applyEffect(id)       { await api('/setEffect',      { id }); }
async function setMimir(on)          { await api('/setMode',        { mimir: on ? 1 : 0 }); }
async function setPower(on)          { return api('/power',         { on: on ? 1 : 0 }); }
async function setMimirRange(min,max){ return api('/mimirRange',    { min, max }); }
async function setWiFiModeAP()       { return api('/wifi',          { mode: 'AP' }); }
async function setWiFiModeSTA() {
  const ssid = els.staSsid.value.trim();
  const pass = els.staPass.value;
  if (!ssid) { alert('Enter STA SSID'); return; }
  return api('/wifi', { mode: 'STA', ssid, pass });
}

/* ---------------------- AI Control ---------------------- */
let aiPolling = false;

function aiSetUI(stateText, disabled) {
  if (els.aiAskBtn) {
    els.aiAskBtn.disabled = disabled;
    els.aiAskBtn.textContent = disabled ? stateText : 'Ask AI';
  }
  if (els.aiCancelBtn) {
    els.aiCancelBtn.style.display = aiPolling ? '' : 'none';
  }
}

function setDebugVisible(show) {
  els.aiDebugWrap.style.display = show ? 'block' : 'none';
  els.aiDebugToggle.textContent = show ? 'Hide debug' : 'Show debug';
}

function appendDebug(text) {
  if (!text) return;
  const now = new Date().toLocaleTimeString();
  const current = els.aiDebugPre.textContent || '';
  els.aiDebugPre.textContent = `${current}${current ? '\n' : ''}[${now}] ${text}`;
}

async function forceRefreshAfterAI() {
  // Force a couple of polls so status + wifi reflect AI changes
  await pollStatus();
  await pollWifiInfo();
  await new Promise(r => setTimeout(r, 300));
  await pollStatus();
  await pollWifiInfo();
}

async function aiPollLoop(startedAt = Date.now()) {
  aiPolling = true;
  aiSetUI('Running…', true);
  const timeoutMs = 30000;

  while (true) {
    try {
      const st = await api('/aiStatus');
      if (st.running) {
        els.aiLog.textContent = `AI running… Prompt: "${st.prompt}"`;
      } else if (st.idle) {
        els.aiLog.textContent = 'AI idle.';
        break;
      } else if (st.done) {
        if (st.ok) {
          els.aiLog.textContent = `AI OK\nApplied: ${st.applied || ''}`;
        } else {
          els.aiLog.textContent = `AI Error: ${st.error || 'unknown'}`;
        }
        appendDebug(st.model_snippet || '');
        appendDebug(`Duration: ${st.duration_ms ?? 0} ms`);
        try { await forceRefreshAfterAI(); } catch {}
        break;
      } else {
        els.aiLog.textContent = `Unexpected status: ${JSON.stringify(st)}`;
        break;
      }
    } catch (e) {
      els.aiLog.textContent = `Status error: ${e.message}`;
      break;
    }

    if (Date.now() - startedAt > timeoutMs) {
      els.aiLog.textContent = 'AI timed out.';
      try { await forceRefreshAfterAI(); } catch {}
      break;
    }

    await new Promise(r => setTimeout(r, 1000));
  }

  aiPolling = false;
  aiSetUI('Ask AI', false);
}

async function askAI() {
  const prompt = (els.aiPrompt.value || '').trim();
  if (!prompt) { alert('Type a prompt'); return; }
  if (aiPolling) { alert('AI job already running'); return; }
  els.aiLog.textContent = 'Starting AI job…';
  aiSetUI('Starting…', true);

  try {
    const start = await api('/aiCommand', { __method: 'POST', prompt });
    if (!start.ok) {
      els.aiLog.textContent = `Start error: ${start.error || JSON.stringify(start)}`;
      aiSetUI('Ask AI', false);
      return;
    }
    await aiPollLoop();
  } catch (e) {
    els.aiLog.textContent = 'Start HTTP error: ' + e.message;
    aiSetUI('Ask AI', false);
    try { await forceRefreshAfterAI(); } catch {}
  }
}

async function cancelAI() {
  if (!aiPolling) { els.aiLog.textContent = 'No running AI job to cancel.'; return; }
  try {
    const res = await api('/aiCancel', {});
    els.aiLog.textContent = res.ok
      ? 'Cancel requested. Waiting for job to finish…'
      : 'Cancel failed: ' + (res.error || 'unknown');
  } catch (e) {
    els.aiLog.textContent = 'Cancel HTTP error: ' + e.message;
  }
}

/* ---------------------- Speech (optional) ---------------------- */
function setupSpeech() {
  if (!els.aiMicBtn) return;
  const SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SR) {
    els.aiMicBtn.disabled = true;
    els.aiMicBtn.title = 'Speech recognition not supported';
    return;
  }
  const rec = new SR();
  rec.lang = 'en-US';
  rec.interimResults = false;
  rec.maxAlternatives = 1;

  let listening = false;
  rec.addEventListener('result', e => {
    const t = e.results?.[0]?.[0]?.transcript || '';
    if (t) els.aiPrompt.value = t;
  });
  rec.addEventListener('end', () => {
    listening = false;
    els.aiMicBtn.textContent = '🎤 Speak';
  });

  els.aiMicBtn.addEventListener('click', () => {
    if (listening) {
      try { rec.stop(); } catch {}
      listening = false;
      els.aiMicBtn.textContent = '🎤 Speak';
    } else {
      try { rec.start(); listening = true; els.aiMicBtn.textContent = '■ Stop'; } catch {}
    }
  });
}

/* ---------------------- Events + init ---------------------- */
function bindEvents() {
  populateEffects();
  setupSpeech();

  // AI
  els.aiAskBtn.addEventListener('click', askAI);
  if (els.aiCancelBtn) els.aiCancelBtn.addEventListener('click', cancelAI);
  if (els.aiDebugToggle) els.aiDebugToggle.addEventListener('click', () => {
    const show = els.aiDebugWrap.style.display !== 'block';
    setDebugVisible(show);
  });

  // Lighting
  els.color.addEventListener('input', async e => {
    const hex = e.target.value.replace('#', '').toUpperCase();
    try { await applyColor(hex); } catch {}
  });
  els.brightness.addEventListener('input', async e => {
    const v = Number(e.target.value);
    els.brightnessVal.textContent = `${v}`;
    try { await applyBrightness(v); } catch {}
  });
  els.power.addEventListener('change', async e => {
    try { await setPower(!!e.target.checked); await pollStatus(); } catch {}
  });
  els.mimir.addEventListener('change', async e => {
    try { await setMimir(!!e.target.checked); } catch {}
  });

  const sendMimirRange = async () => {
    let min = Number(els.mimirMin.value);
    let max = Number(els.mimirMax.value);
    if (min > max) { max = min; els.mimirMax.value = String(max); }
    updateRangeUI(min, max);
    try { await setMimirRange(min, max); } catch {}
  };
  els.mimirMin.addEventListener('input', sendMimirRange);
  els.mimirMax.addEventListener('input', sendMimirRange);

  els.effect.addEventListener('change', async e => {
    const id = Number(e.target.value);
    try { await applyEffect(id); await pollStatus(); } catch {}
  });

  // Wi‑Fi mode controls
  els.btnAP.addEventListener('click', async () => {
    try {
      const res = await setWiFiModeAP();
      alert(res?.ok ? 'Switched to AP mode' : (res?.error || 'Failed to switch to AP'));
      await pollWifiInfo();
      await pollStatus();
    } catch {
      alert('Failed to switch to AP');
    }
  });

  els.btnSTA.addEventListener('click', async () => {
    try {
      const res = await setWiFiModeSTA();
      alert(res?.ok ? 'Switched to STA mode' : (res?.error || 'Failed to switch to STA'));
      await pollWifiInfo();
      await pollStatus();
    } catch {
      alert('Failed to switch to STA');
    }
  });
}

(async function main() {
  setDebugVisible(false);
  bindEvents();
  await pollStatus();
  await pollWifiInfo();
  setInterval(pollStatus, 2000);
  setInterval(pollWifiInfo, 5000);
})();