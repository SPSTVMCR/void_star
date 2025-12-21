const PC_MODEL_HOST = "sleepmodel.local";
const PC_MODEL_PORT = 5055;

const PRESETS_MAX_UI_PER_BUCKET = 6;
const PRESETS_MAX_UI_MANUAL = 12;

const AUTO_SUGGEST_COOLDOWN_MS = 60_000;

// Storage keys
const LS_AUTO_SUGGEST = "autoSuggestEnabled";

// ---------------------- API helpers ----------------------
async function api(path, params = {}, options = {}) {
  const method = params.__method || "GET";
  const url = new URL(path, window.location.origin);

  if (method === "GET") {
    Object.keys(params).forEach((k) => {
      if (k !== "__method") url.searchParams.set(k, params[k]);
    });
    const res = await fetch(url.toString(), { cache: "no-store", ...options });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  } else if (method === "POST") {
    const body = new URLSearchParams();
    Object.keys(params).forEach((k) => {
      if (k !== "__method") body.set(k, params[k]);
    });
    const res = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: body.toString(),
      cache: "no-store",
      ...options,
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }
  throw new Error(`Unsupported method ${method}`);
}

async function apiJson(path, obj, options = {}) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(obj),
    cache: "no-store",
    ...options,
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

// best-effort JSON POST (ignore failures)
function postJsonNoThrow(url, obj) {
  fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(obj),
    cache: "no-store",
  }).catch(() => {});
}

function pcUrl(path) {
  return `http://${PC_MODEL_HOST}:${PC_MODEL_PORT}${path}`;
}

function nowTs() {
  return Math.floor(Date.now() / 1000);
}

// ---------------------- Effect list (for UI) ----------------------
const EFFECTS = [
  [0, "Static"],
  [1, "Blink"],
  [2, "Breath"],
  [3, "Color Wipe"],
  [4, "Color Wipe Inv"],
  [5, "Color Wipe Rev"],
  [6, "Color Wipe Rev Inv"],
  [7, "Color Wipe Random"],
  [8, "Random Color"],
  [9, "Single Dynamic"],
  [10, "Multi Dynamic"],
  [11, "Rainbow"],
  [12, "Rainbow Cycle"],
  [13, "Scan"],
  [14, "Dual Scan"],
  [15, "Fade"],
  [16, "Theater Chase"],
  [17, "Theater Chase Rainbow"],
  [18, "Running Lights"],
  [19, "Twinkle"],
  [20, "Twinkle Random"],
  [21, "Twinkle Fade"],
  [22, "Twinkle Fade Random"],
  [23, "Sparkle"],
  [24, "Flash Sparkle"],
  [25, "Hyper Sparkle"],
  [26, "Strobe"],
  [27, "Strobe Rainbow"],
  [28, "Multi Strobe"],
  [29, "Blink Rainbow"],
  [30, "Chase White"],
  [31, "Chase Color"],
  [32, "Chase Random"],
  [33, "Chase Rainbow"],
  [34, "Chase Flash"],
  [35, "Chase Flash Random"],
  [36, "Chase Rainbow White"],
  [37, "Chase Blackout"],
  [38, "Chase Blackout Rainbow"],
  [39, "Color Sweep Random"],
  [40, "Running Color"],
  [41, "Running Red Blue"],
  [42, "Running Random"],
  [43, "Larson Scanner"],
  [44, "Comet"],
  [45, "Fireworks"],
  [46, "Fireworks Random"],
  [47, "Merry Christmas"],
  [48, "Fire Flicker"],
  [49, "Fire Flicker (Soft)"],
  [50, "Fire Flicker (Intense)"],
  [51, "Circus Combustus"],
  [52, "Halloween"],
  [53, "Bicolor Chase"],
  [54, "Tricolor Chase"],
  [55, "ICU"],
];

function effectNameFromId(id) {
  const match = EFFECTS.find(([eid]) => eid === Number(id));
  return match ? match[1] : "Unknown";
}

function pct(val, max) {
  return Math.max(0, Math.min(100, Math.round((val / max) * 100)));
}

// ---------------------- Elements ----------------------
const els = {
  // Controls
  color: document.getElementById("colorPicker"),
  brightness: document.getElementById("brightness"),
  brightnessVal: document.getElementById("brightnessVal"),
  power: document.getElementById("powerToggle"),
  mimir: document.getElementById("mimirToggle"),
  effect: document.getElementById("effectSelect"),
  mimirMin: document.getElementById("mimirMin"),
  mimirMax: document.getElementById("mimirMax"),
  mimirMinVal: document.getElementById("mimirMinVal"),
  mimirMaxVal: document.getElementById("mimirMaxVal"),

  // Status
  colorSwatch: document.getElementById("colorSwatch"),
  colorText: document.getElementById("colorText"),
  brightnessBar: document.getElementById("brightnessBar"),
  brightnessText: document.getElementById("brightnessText"),
  luxText: document.getElementById("luxText"),
  luxBar: document.getElementById("luxBar"),
  stateBadge: document.getElementById("stateBadge"),
  wifiBadge: document.getElementById("wifiBadge"),
  effectBadge: document.getElementById("effectBadge"),
  mimirRangeText: document.getElementById("mimirRangeText"),
  mimirRangeBar: document.getElementById("mimirRangeBar"),

  // Wi‑Fi UI
  wifiModeLabel: document.getElementById("wifiModeLabel"),
  btnAP: document.getElementById("btnAP"),
  btnSTA: document.getElementById("btnSTA"),
  staSsid: document.getElementById("staSsid"),
  staPass: document.getElementById("staPass"),
  staInfo: document.getElementById("staInfo"),
  apInfo: document.getElementById("apInfo"),
  staInfoSsid: document.getElementById("staInfoSsid"),
  staInfoIp: document.getElementById("staInfoIp"),
  staInfoGw: document.getElementById("staInfoGw"),
  staInfoSubnet: document.getElementById("staInfoSubnet"),
  staInfoDns: document.getElementById("staInfoDns"),
  staInfoRssi: document.getElementById("staInfoRssi"),
  staInfoCh: document.getElementById("staInfoCh"),

  // AI
  aiPrompt: document.getElementById("aiPrompt"),
  aiAskBtn: document.getElementById("aiAskBtn"),
  aiMicBtn: document.getElementById("aiMicBtn"),
  aiCancelBtn: document.getElementById("aiCancelBtn"),
  aiLog: document.getElementById("aiLog"),
  aiDebugToggle: document.getElementById("aiDebugToggle"),
  aiDebugWrap: document.getElementById("aiDebugWrap"),
  aiDebugPre: document.getElementById("aiDebugPre"),

  // Preset controls (bucketed HTML)
  presetModeSelect: document.getElementById("presetModeSelect"),
  autoSuggestToggle: document.getElementById("autoSuggestToggle"),
  applyOnTimeToggle: document.getElementById("applyOnTimeToggle"),
  btnSuggestNow: document.getElementById("btnSuggestNow"),
  themeToggle: document.getElementById("themeToggle"),

  // Preset buckets (lists + empties)
  presetsMorningList: document.getElementById("presetsMorningList"),
  presetsMorningEmpty: document.getElementById("presetsMorningEmpty"),
  presetsNoonList: document.getElementById("presetsNoonList"),
  presetsNoonEmpty: document.getElementById("presetsNoonEmpty"),
  presetsAfternoonList: document.getElementById("presetsAfternoonList"),
  presetsAfternoonEmpty: document.getElementById("presetsAfternoonEmpty"),
  presetsEveningList: document.getElementById("presetsEveningList"),
  presetsEveningEmpty: document.getElementById("presetsEveningEmpty"),
  presetsNightList: document.getElementById("presetsNightList"),
  presetsNightEmpty: document.getElementById("presetsNightEmpty"),
  presetsManualList: document.getElementById("presetsManualList"),
  presetsManualEmpty: document.getElementById("presetsManualEmpty"),
};

// ---------------------- UI helpers ----------------------
function populateEffects() {
  if (!els.effect) return;
  els.effect.innerHTML = "";
  for (const [id, name] of EFFECTS) {
    const o = document.createElement("option");
    o.value = String(id);
    o.textContent = `${id} - ${name}`;
    els.effect.appendChild(o);
  }
}
function setupThemeToggle() {
  const root = document.documentElement;
  const key = "vs-theme";
  const saved = localStorage.getItem(key);
  if (saved === "light" || saved === "dark") {
    root.setAttribute("data-bs-theme", saved);
  }
  const btn = els.themeToggle;
  if (!btn) return;
  function syncLabel() {
    const t = root.getAttribute("data-bs-theme") === "light" ? "Light" : "Dark";
    btn.textContent = t;
  }
  syncLabel();
  btn.addEventListener("click", () => {
    const next =
      root.getAttribute("data-bs-theme") === "dark" ? "light" : "dark";
    root.setAttribute("data-bs-theme", next);
    localStorage.setItem(key, next);
    syncLabel();
  });
}

function updateBadges(on, wifiMode, effectId) {
  els.stateBadge.textContent = on ? "On" : "Off";
  els.stateBadge.classList.remove("bg-success", "bg-secondary");
  els.stateBadge.classList.add(on ? "bg-success" : "bg-secondary");

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

// ---------------------- Status polling ----------------------
function updateUIStatus(status) {
  els.color.value = `#${status.color}`;
  els.colorSwatch.style.background = `#${status.color}`;
  els.colorText.textContent = `#${status.color}`;

  els.brightness.value = status.brightness;
  els.brightnessVal.textContent = `${status.brightness}`;
  els.power.checked = !!status.on;

  els.brightnessBar.style.width = `${pct(status.current_brightness, 255)}%`;
  els.brightnessText.textContent = `${status.brightness}/255 (${status.current_brightness})`;

  els.luxText.textContent = Number(status.lux).toFixed(2);
  els.luxBar.style.width = `${pct(Math.min(status.lux, 400), 400)}%`;

  updateRangeUI(
    Number(status.mimir_min ?? 20),
    Number(status.mimir_max ?? 220)
  );
  updateBadges(!!status.on, status.wifi_mode, status.effect_id);

  const opt = [...els.effect.options].find(
    (o) => Number(o.value) === Number(status.effect_id)
  );
  if (opt) els.effect.value = String(status.effect_id);
}

async function pollStatus() {
  try {
    const st = await api("/status");
    updateUIStatus(st);
  } catch (e) {
    console.warn("Status poll error", e);
  }
}

// ---------------------- Wi‑Fi info ----------------------
function updateWifiInfoUI(info) {
  const mode = (info.mode || "AP").toUpperCase();
  els.wifiModeLabel.textContent = mode;

  if (mode === "STA") {
    els.staInfo.style.display = "block";
    els.apInfo.style.display = "none";
    els.staInfoSsid.textContent = info.ssid || "";
    els.staInfoIp.textContent = info.ip || "";
    els.staInfoGw.textContent = info.gw || "";
    els.staInfoSubnet.textContent = info.subnet || "";
    els.staInfoDns.textContent = info.dns || "";
    els.staInfoRssi.textContent = info.rssi !== undefined ? info.rssi : "";
    els.staInfoCh.textContent = info.channel !== undefined ? info.channel : "";
  } else {
    els.staInfo.style.display = "none";
    els.apInfo.style.display = "block";
  }
}

async function pollWifiInfo() {
  try {
    const info = await api("/wifiInfo");
    updateWifiInfoUI(info);
  } catch (e) {
    console.warn("wifiInfo poll error", e);
  }
}

// ---------------------- Direct control wrappers ----------------------
async function applyColor(hex) {
  await api("/setColor", { hex });
}
async function applyBrightness(val) {
  await api("/setBrightness", { value: val });
}
async function applyEffect(id) {
  await api("/setEffect", { id });
}
async function setMimir(on) {
  await api("/setMode", { mimir: on ? 1 : 0 });
}
async function setPower(on) {
  return api("/power", { on: on ? 1 : 0 });
}
async function setMimirRange(min, max) {
  return api("/mimirRange", { min, max });
}
async function setWiFiModeAP() {
  return api("/wifi", { mode: "AP" });
}
async function setWiFiModeSTA() {
  const ssid = els.staSsid.value.trim();
  const pass = els.staPass.value;
  if (!ssid) {
    alert("Enter STA SSID");
    return;
  }
  return api("/wifi", { mode: "STA", ssid, pass });
}

// ---------------------- Training + auto-suggest ----------------------
async function getStatusSnapshot() {
  return api("/status");
}

function isAutoSuggestEnabled() {
  return localStorage.getItem(LS_AUTO_SUGGEST) === "1";
}
function setAutoSuggestEnabled(on) {
  localStorage.setItem(LS_AUTO_SUGGEST, on ? "1" : "0");
}

let lastAutoSuggestMs = 0;

async function maybeAutoSuggest(note = "auto_suggest") {
  if (!isAutoSuggestEnabled()) return;

  const now = Date.now();
  if (now - lastAutoSuggestMs < AUTO_SUGGEST_COOLDOWN_MS) return;
  lastAutoSuggestMs = now;

  try {
    const status = await getStatusSnapshot();
    await fetch(pcUrl("/suggest"), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      // category defaults to manual on the PC server unless overridden; for UI-auto we still want MANUAL? up to you.
      // Keep it manual so it does not count towards "2 auto per bucket/day".
      body: JSON.stringify({
        ts: nowTs(),
        note,
        source: "ui-auto",
        status,
        category: "manual",
      }),
      cache: "no-store",
    });
    await pollPresets();
  } catch {
    // ignore
  }
}

async function sendTrainingEvent(before, after, note) {
  const payload = { ts: nowTs(), before, after, source: "user", note };
  postJsonNoThrow("/logAction", payload);
  postJsonNoThrow(pcUrl("/train"), payload);
  await maybeAutoSuggest(`after_${note}`);
}

// ---------------------- PC server mode controls ----------------------
let pcHasMode = false;

async function refreshPcMode() {
  if (!els.presetModeSelect || !els.applyOnTimeToggle) return;

  try {
    const res = await fetch(pcUrl("/mode"), { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    if (!data?.ok) throw new Error("no ok");

    pcHasMode = true;
    els.presetModeSelect.disabled = false;
    els.applyOnTimeToggle.disabled = false;

    if (data.mode) els.presetModeSelect.value = data.mode;
    els.applyOnTimeToggle.checked = !!data.apply_on_time_change;
  } catch {
    pcHasMode = false;
    // Keep controls visible but disabled (so user understands feature exists)
    els.presetModeSelect.disabled = true;
    els.applyOnTimeToggle.disabled = true;
  }
}

async function pushPcMode() {
  if (!pcHasMode) return;
  try {
    await fetch(pcUrl("/mode"), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        mode: els.presetModeSelect.value,
        apply_on_time_change: !!els.applyOnTimeToggle.checked,
      }),
      cache: "no-store",
    });
  } catch {
    // ignore
  }
}

// ---------------------- Presets rendering (bucketed) ----------------------
function fmtTimeShort(ts) {
  try {
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  } catch {
    return "";
  }
}

function disposeTooltipsIn(container) {
  if (!container) return;
  if (!window.bootstrap || !bootstrap.Tooltip) return;
  container.querySelectorAll('[data-bs-toggle="tooltip"]').forEach((el) => {
    try {
      const inst = bootstrap.Tooltip.getInstance(el);
      if (inst) inst.dispose();
    } catch {}
  });
}

function clearBucketUI(listEl, emptyEl) {
  if (!listEl || !emptyEl) return;
  disposeTooltipsIn(listEl);
  listEl.innerHTML = "";
  emptyEl.style.display = "";
}

function setBucketHasItems(listEl, emptyEl, hasItems) {
  if (!listEl || !emptyEl) return;
  emptyEl.style.display = hasItems ? "none" : "";
}

function extractSummary(actions) {
  let b = null,
    color = null,
    eff = null,
    mimir = null,
    power = null;
  for (const a of actions || []) {
    if (a.type === "set_brightness") b = a.value;
    if (a.type === "set_color") color = a.hex;
    if (a.type === "set_effect") eff = a.id !== undefined ? a.id : a.name;
    if (a.type === "set_mimir") mimir = a.on;
    if (a.type === "set_power") power = a.on;
  }
  return { b, color, eff, mimir, power };
}

function makeChip(text) {
  const s = document.createElement("span");
  s.className = "chip";
  s.textContent = text;
  return s;
}

function makeColorChip(hex) {
  const wrap = document.createElement("span");
  wrap.className = "chip";
  const dot = document.createElement("span");
  dot.className = "chip-color";
  dot.style.background = hex || "#FFFFFF";
  const txt = document.createElement("span");
  txt.textContent = (hex || "").toUpperCase();
  wrap.appendChild(dot);
  wrap.appendChild(txt);
  return wrap;
}

function createPresetCard(p) {
  const actions = p.actions || [];
  const s = extractSummary(actions);

  const card = document.createElement("button");
  card.type = "button";
  card.className = "btn text-start p-0 preset-card w-100";
  card.style.borderRadius = "0.75rem";

  const body = document.createElement("div");
  body.className = "p-3";

  const header = document.createElement("div");
  header.className = "d-flex justify-content-between align-items-start gap-2";

  const left = document.createElement("div");

  const title = document.createElement("div");
  title.className = "preset-title";
  title.textContent = `${p.category === "manual" ? "Manual" : "Auto"} • ${
    p.bucket || "time"
  }`;

  const sub = document.createElement("div");
  sub.className = "preset-sub";
  sub.textContent = `${fmtTimeShort(p.ts)} • ${p.note || ""}`;

  left.appendChild(title);
  left.appendChild(sub);

  const badge = document.createElement("span");
  badge.className =
    "badge bg-primary-subtle text-primary border border-primary-subtle";
  badge.textContent = "Apply";

  header.appendChild(left);
  header.appendChild(badge);

  const chips = document.createElement("div");
  chips.className = "preset-actions";

  if (s.power !== null)
    chips.appendChild(makeChip(s.power ? "Power: On" : "Power: Off"));
  if (s.b !== null) chips.appendChild(makeChip(`B: ${s.b}`));
  if (s.color) chips.appendChild(makeColorChip(s.color));
  if (s.eff !== null) chips.appendChild(makeChip(`Effect: ${s.eff}`));
  if (s.mimir !== null)
    chips.appendChild(makeChip(`Mimir: ${s.mimir ? "On" : "Off"}`));

  body.appendChild(header);
  body.appendChild(chips);
  card.appendChild(body);

  const tip = [
    `Time: ${new Date((p.ts || 0) * 1000).toLocaleString()}`,
    `Bucket: ${p.bucket || ""}`,
    `Category: ${p.category || ""}`,
    `Source: ${p.source || ""}`,
    `Note: ${p.note || ""}`,
    `Actions: ${JSON.stringify(actions)}`,
  ].join("\n");

  card.setAttribute("data-bs-toggle", "tooltip");
  card.setAttribute("data-bs-placement", "top");
  card.setAttribute("data-bs-title", tip);

  card.addEventListener("click", async () => {
    try {
      await apiJson("/applyPreset", {
        actions,
        source: "ui-click",
        ts: nowTs(),
        note: "apply_clicked",
      });
      await pollStatus();
    } catch (e) {
      console.warn("Apply preset failed", e);
    }
  });

  return card;
}

function bucketKeyFromPreset(p) {
  const cat = (p.category || "").toLowerCase();
  if (cat === "manual") return "manual";

  const b = (p.bucket || "").toLowerCase();
  if (b === "morning") return "morning";
  if (b === "noon") return "noon";
  if (b === "afternoon") return "afternoon";
  if (b === "evening") return "evening";
  if (b === "night") return "night";

  // fallback: treat unknown as manual to avoid losing it
  return "manual";
}

function renderBucketedPresets(presets) {
  // Clear all buckets
  clearBucketUI(els.presetsMorningList, els.presetsMorningEmpty);
  clearBucketUI(els.presetsNoonList, els.presetsNoonEmpty);
  clearBucketUI(els.presetsAfternoonList, els.presetsAfternoonEmpty);
  clearBucketUI(els.presetsEveningList, els.presetsEveningEmpty);
  clearBucketUI(els.presetsNightList, els.presetsNightEmpty);
  clearBucketUI(els.presetsManualList, els.presetsManualEmpty);

  const by = {
    morning: [],
    noon: [],
    afternoon: [],
    evening: [],
    night: [],
    manual: [],
  };

  for (const p of Array.isArray(presets) ? presets : []) {
    const k = bucketKeyFromPreset(p);
    by[k].push(p);
  }

  // Newest first already from server, but ensure
  const limitAndRender = (arr, listEl, emptyEl, limit) => {
    const slice = arr.slice(0, limit);
    if (slice.length === 0) return;
    setBucketHasItems(listEl, emptyEl, true);
    for (const p of slice) listEl.appendChild(createPresetCard(p));
  };

  limitAndRender(
    by.morning,
    els.presetsMorningList,
    els.presetsMorningEmpty,
    PRESETS_MAX_UI_PER_BUCKET
  );
  limitAndRender(
    by.noon,
    els.presetsNoonList,
    els.presetsNoonEmpty,
    PRESETS_MAX_UI_PER_BUCKET
  );
  limitAndRender(
    by.afternoon,
    els.presetsAfternoonList,
    els.presetsAfternoonEmpty,
    PRESETS_MAX_UI_PER_BUCKET
  );
  limitAndRender(
    by.evening,
    els.presetsEveningList,
    els.presetsEveningEmpty,
    PRESETS_MAX_UI_PER_BUCKET
  );
  limitAndRender(
    by.night,
    els.presetsNightList,
    els.presetsNightEmpty,
    PRESETS_MAX_UI_PER_BUCKET
  );
  limitAndRender(
    by.manual,
    els.presetsManualList,
    els.presetsManualEmpty,
    PRESETS_MAX_UI_MANUAL
  );

  // activate tooltips if bootstrap is present
  if (window.bootstrap && bootstrap.Tooltip) {
    document.querySelectorAll('[data-bs-toggle="tooltip"]').forEach((el) => {
      try {
        new bootstrap.Tooltip(el, { trigger: "hover", html: false });
      } catch {}
    });
  }
}

async function pollPresets() {
  try {
    const res = await fetch(pcUrl("/presets"), { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    renderBucketedPresets(data.presets || []);
  } catch {
    // quiet if PC not reachable
  }
}

// Manual suggest -> PC server (category=manual so it's unlimited)
async function manualSuggest() {
  const status = await getStatusSnapshot();
  const res = await fetch(pcUrl("/suggest"), {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      ts: nowTs(),
      note: "manual_suggest",
      source: "ui-manual",
      status,
      category: "manual",
    }),
    cache: "no-store",
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
}

// ---------------------- AI Control (unchanged behavior) ----------------------
let aiPolling = false;

function aiSetUI(stateText, disabled) {
  if (els.aiAskBtn) {
    els.aiAskBtn.disabled = disabled;
    els.aiAskBtn.textContent = disabled ? stateText : "Ask AI";
  }
  if (els.aiCancelBtn) {
    els.aiCancelBtn.style.display = aiPolling ? "" : "none";
  }
}

function setDebugVisible(show) {
  els.aiDebugWrap.style.display = show ? "block" : "none";
  els.aiDebugToggle.textContent = show ? "Hide debug" : "Show debug";
}

function appendDebug(text) {
  if (!text) return;
  const now = new Date().toLocaleTimeString();
  const current = els.aiDebugPre.textContent || "";
  els.aiDebugPre.textContent = `${current}${
    current ? "\n" : ""
  }[${now}] ${text}`;
}

async function forceRefreshAfterAI() {
  await pollStatus();
  await pollWifiInfo();
  await new Promise((r) => setTimeout(r, 300));
  await pollStatus();
  await pollWifiInfo();
}

async function aiPollLoop(startedAt = Date.now()) {
  aiPolling = true;
  aiSetUI("Running…", true);
  const timeoutMs = 30000;

  while (true) {
    try {
      const st = await api("/aiStatus");
      if (st.running) {
        els.aiLog.textContent = `AI running… Prompt: "${st.prompt}"`;
      } else if (st.idle) {
        els.aiLog.textContent = "AI idle.";
        break;
      } else if (st.done) {
        if (st.ok)
          els.aiLog.textContent = `AI OK\nApplied: ${st.applied || ""}`;
        else els.aiLog.textContent = `AI Error: ${st.error || "unknown"}`;
        appendDebug(st.model_snippet || "");
        appendDebug(`Duration: ${st.duration_ms ?? 0} ms`);
        try {
          await forceRefreshAfterAI();
        } catch {}
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
      els.aiLog.textContent = "AI timed out.";
      try {
        await forceRefreshAfterAI();
      } catch {}
      break;
    }

    await new Promise((r) => setTimeout(r, 1000));
  }

  aiPolling = false;
  aiSetUI("Ask AI", false);
}

async function askAI() {
  const prompt = (els.aiPrompt.value || "").trim();
  if (!prompt) {
    alert("Type a prompt");
    return;
  }
  if (aiPolling) {
    alert("AI job already running");
    return;
  }
  els.aiLog.textContent = "Starting AI job…";
  aiSetUI("Starting…", true);

  try {
    const start = await api("/aiCommand", { __method: "POST", prompt });
    if (!start.ok) {
      els.aiLog.textContent = `Start error: ${
        start.error || JSON.stringify(start)
      }`;
      aiSetUI("Ask AI", false);
      return;
    }
    await aiPollLoop();
  } catch (e) {
    els.aiLog.textContent = "Start HTTP error: " + e.message;
    aiSetUI("Ask AI", false);
    try {
      await forceRefreshAfterAI();
    } catch {}
  }
}

async function cancelAI() {
  if (!aiPolling) {
    els.aiLog.textContent = "No running AI job to cancel.";
    return;
  }
  try {
    const res = await api("/aiCancel", {});
    els.aiLog.textContent = res.ok
      ? "Cancel requested. Waiting…"
      : "Cancel failed";
  } catch (e) {
    els.aiLog.textContent = "Cancel HTTP error: " + e.message;
  }
}

// ---------------------- Speech (optional) ----------------------
function setupSpeech() {
  if (!els.aiMicBtn) return;
  const SR = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SR) {
    els.aiMicBtn.disabled = true;
    els.aiMicBtn.title = "Speech recognition not supported";
    return;
  }
  const rec = new SR();
  rec.lang = "en-US";
  rec.interimResults = false;
  rec.maxAlternatives = 1;

  let listening = false;
  rec.addEventListener("result", (e) => {
    const t = e.results?.[0]?.[0]?.transcript || "";
    if (t) els.aiPrompt.value = t;
  });
  rec.addEventListener("end", () => {
    listening = false;
  });

  els.aiMicBtn.addEventListener("click", () => {
    if (listening) {
      try {
        rec.stop();
      } catch {}
      listening = false;
    } else {
      try {
        rec.start();
        listening = true;
      } catch {}
    }
  });
}

// ---------------------- Events + init ----------------------
function bindEvents() {
  populateEffects();
  setupSpeech();

  // Preset controls
  if (els.autoSuggestToggle) {
    els.autoSuggestToggle.checked = isAutoSuggestEnabled();
    els.autoSuggestToggle.addEventListener("change", () => {
      setAutoSuggestEnabled(els.autoSuggestToggle.checked);
      lastAutoSuggestMs = Date.now();
    });
  }

  if (els.btnSuggestNow) {
    els.btnSuggestNow.addEventListener("click", async () => {
      try {
        await manualSuggest();
        await pollPresets();
      } catch (e) {
        alert(`Manual suggest failed: ${e.message}`);
      }
    });
  }

  if (els.presetModeSelect) {
    els.presetModeSelect.addEventListener("change", async () => {
      await pushPcMode();
      await refreshPcMode();
    });
  }

  if (els.applyOnTimeToggle) {
    els.applyOnTimeToggle.addEventListener("change", async () => {
      await pushPcMode();
      await refreshPcMode();
    });
  }

  // Lighting (TRAINING: only on user changes)
  els.color.addEventListener("change", async (e) => {
    const before = await getStatusSnapshot().catch(() => null);
    const hex = e.target.value.replace("#", "").toUpperCase();
    try {
      await applyColor(hex);
    } catch {}
    const after = await getStatusSnapshot().catch(() => null);
    if (before && after) await sendTrainingEvent(before, after, "set_color");
  });

  els.brightness.addEventListener("input", (e) => {
    els.brightnessVal.textContent = `${Number(e.target.value)}`;
  });

  els.brightness.addEventListener("change", async (e) => {
    const before = await getStatusSnapshot().catch(() => null);
    const v = Number(e.target.value);
    try {
      await applyBrightness(v);
    } catch {}
    const after = await getStatusSnapshot().catch(() => null);
    if (before && after)
      await sendTrainingEvent(before, after, "set_brightness");
  });

  els.power.addEventListener("change", async (e) => {
    const before = await getStatusSnapshot().catch(() => null);
    try {
      await setPower(!!e.target.checked);
    } catch {}
    const after = await getStatusSnapshot().catch(() => null);
    if (before && after) await sendTrainingEvent(before, after, "set_power");
    await pollStatus();
  });

  els.mimir.addEventListener("change", async (e) => {
    const before = await getStatusSnapshot().catch(() => null);
    try {
      await setMimir(!!e.target.checked);
    } catch {}
    const after = await getStatusSnapshot().catch(() => null);
    if (before && after) await sendTrainingEvent(before, after, "set_mimir");
  });

  const sendMimirRange = async () => {
    const before = await getStatusSnapshot().catch(() => null);
    let min = Number(els.mimirMin.value);
    let max = Number(els.mimirMax.value);
    if (min > max) {
      max = min;
      els.mimirMax.value = String(max);
    }
    updateRangeUI(min, max);
    try {
      await setMimirRange(min, max);
    } catch {}
    const after = await getStatusSnapshot().catch(() => null);
    if (before && after)
      await sendTrainingEvent(before, after, "set_mimir_range");
  };
  els.mimirMin.addEventListener("change", sendMimirRange);
  els.mimirMax.addEventListener("change", sendMimirRange);

  els.effect.addEventListener("change", async (e) => {
    const before = await getStatusSnapshot().catch(() => null);
    const id = Number(e.target.value);
    try {
      await applyEffect(id);
    } catch {}
    const after = await getStatusSnapshot().catch(() => null);
    if (before && after) await sendTrainingEvent(before, after, "set_effect");
    await pollStatus();
  });

  // Wi‑Fi mode controls
  els.btnAP.addEventListener("click", async () => {
    try {
      const res = await setWiFiModeAP();
      alert(res?.ok ? "Switched to AP" : "Failed");
    } catch {
      alert("Failed to switch to AP");
    }
    await pollWifiInfo();
    await pollStatus();
  });

  els.btnSTA.addEventListener("click", async () => {
    try {
      const res = await setWiFiModeSTA();
      alert(res?.ok ? "Switched to STA" : "Failed");
    } catch {
      alert("Failed to switch to STA");
    }
    await pollWifiInfo();
    await pollStatus();
  });

  // AI
  els.aiAskBtn.addEventListener("click", askAI);
  if (els.aiCancelBtn) els.aiCancelBtn.addEventListener("click", cancelAI);
  if (els.aiDebugToggle)
    els.aiDebugToggle.addEventListener("click", () => {
      const show = els.aiDebugWrap.style.display !== "block";
      setDebugVisible(show);
    });
}

(async function main() {
  setDebugVisible(false);

  // default auto-suggest off unless user enabled
  if (localStorage.getItem(LS_AUTO_SUGGEST) === null)
    setAutoSuggestEnabled(false);
  try {
    setupThemeToggle();
  } catch {}

  bindEvents();

  // Initial polls
  await pollStatus();
  await pollWifiInfo();
  await pollPresets();
  await refreshPcMode();

  // Intervals
  setInterval(pollStatus, 2000);
  setInterval(pollWifiInfo, 5000);
  setInterval(pollPresets, 4000);
  setInterval(refreshPcMode, 15000);
})();
