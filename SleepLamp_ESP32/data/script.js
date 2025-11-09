// Helper
async function api(path, params = {}) {
  const url = new URL(path, window.location.origin);
  Object.keys(params).forEach((k) => url.searchParams.set(k, params[k]));
  const res = await fetch(url.toString(), { cache: "no-store" });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

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

const els = {
  color: document.getElementById("colorPicker"),
  brightness: document.getElementById("brightness"),
  brightnessVal: document.getElementById("brightnessVal"),
  power: document.getElementById("powerToggle"),
  mimir: document.getElementById("mimirToggle"),
  effect: document.getElementById("effectSelect"),
  // Mimir range
  mimirMin: document.getElementById("mimirMin"),
  mimirMax: document.getElementById("mimirMax"),
  mimirMinVal: document.getElementById("mimirMinVal"),
  mimirMaxVal: document.getElementById("mimirMaxVal"),
  // Status UI
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
  // Wi-Fi controls
  btnAP: document.getElementById("btnAP"),
  btnSTA: document.getElementById("btnSTA"),
  staSsid: document.getElementById("staSsid"),
  staPass: document.getElementById("staPass"),
  // Router info
  routerInfo: document.getElementById("routerInfo"),
  riSsid: document.getElementById("riSsid"),
  riRssi: document.getElementById("riRssi"),
  riChan: document.getElementById("riChan"),
  riIP: document.getElementById("riIP"),
  riGW: document.getElementById("riGW"),
  riSN: document.getElementById("riSN"),
  riDNS: document.getElementById("riDNS"),
};

function populateEffects() {
  els.effect.innerHTML = "";
  for (const [id, name] of EFFECTS) {
    const o = document.createElement("option");
    o.value = String(id);
    o.textContent = `${id} - ${name}`;
    els.effect.appendChild(o);
  }
}

async function applyColor(hex) {
  await api("/setColor", { hex });
}
async function applyBrightness(value) {
  await api("/setBrightness", { value });
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

function pct(val, max) {
  return Math.max(0, Math.min(100, Math.round((val / max) * 100)));
}

function updateBadges(on, wifi, effectId, effectName) {
  els.stateBadge.textContent = on ? "On" : "Off";
  els.stateBadge.classList.remove("bg-success", "bg-secondary");
  els.stateBadge.classList.add(on ? "bg-success" : "bg-secondary");
  els.wifiBadge.textContent = wifi;
  els.effectBadge.textContent = `${effectId} - ${effectName}`;
}

function updateRangeUI(min, max) {
  els.mimirMin.value = min;
  els.mimirMax.value = max;
  els.mimirMinVal.textContent = String(min);
  els.mimirMaxVal.textContent = String(max);
  els.mimirRangeText.textContent = `${min} – ${max}`;
  els.mimirRangeBar.style.width = `${pct(max - min, 255)}%`;
}

function updateRouterInfoVisible(isSTA) {
  els.routerInfo.style.display = isSTA ? "" : "none";
}

async function pollWifiInfoIfSTA(isSTA) {
  if (!isSTA) {
    updateRouterInfoVisible(false);
    return;
  }
  try {
    const wi = await api("/wifiInfo");
    if (wi.mode !== "STA") {
      updateRouterInfoVisible(false);
      return;
    }
    updateRouterInfoVisible(true);
    els.riSsid.textContent = wi.ssid ?? "-";
    els.riRssi.textContent = (wi.rssi ?? 0) + " dBm";
    els.riChan.textContent = wi.channel ?? "-";
    els.riIP.textContent = wi.ip ?? "-";
    els.riGW.textContent = wi.gw ?? "-";
    els.riSN.textContent = wi.subnet ?? "-";
    els.riDNS.textContent = wi.dns ?? "-";
  } catch (e) {
    updateRouterInfoVisible(false);
  }
}

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
  els.brightnessBar.ariaValueNow = String(p);
  els.brightnessText.textContent = `${status.brightness}/255 (${status.current_brightness})`;

  els.luxText.textContent = Number(status.lux).toFixed(2);
  els.luxBar.style.width = `${pct(Math.min(status.lux, 400), 400)}%`;

  updateBadges(
    !!status.on,
    status.wifi_mode,
    status.effect_id,
    status.effect_name
  );

  // Effect
  const opt = [...els.effect.options].find(
    (o) => Number(o.value) === Number(status.effect_id)
  );
  if (opt) els.effect.value = String(status.effect_id);

  // Mimir range
  const min = Number(status.mimir_min ?? 20);
  const max = Number(status.mimir_max ?? 220);
  updateRangeUI(min, max);

  pollWifiInfoIfSTA(status.wifi_mode === "STA");
}

async function poll() {
  try {
    const st = await api("/status");
    updateUIStatus(st);
  } catch (e) {
    console.warn("Status poll error", e);
  }
}

function bindEvents() {
  els.color.addEventListener("input", async (e) => {
    const hex = e.target.value.replace("#", "").toUpperCase();
    try {
      await applyColor(hex);
    } catch {}
  });

  els.brightness.addEventListener("input", async (e) => {
    const v = Number(e.target.value);
    els.brightnessVal.textContent = `${v}`;
    try {
      await applyBrightness(v);
    } catch {}
  });

  els.power.addEventListener("change", async (e) => {
    try {
      await setPower(!!e.target.checked);
      await poll();
    } catch {}
  });

  els.mimir.addEventListener("change", async (e) => {
    try {
      await setMimir(!!e.target.checked);
    } catch {}
  });

  const sendMimirRange = async () => {
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
  };
  els.mimirMin.addEventListener("input", sendMimirRange);
  els.mimirMax.addEventListener("input", sendMimirRange);

  els.effect.addEventListener("change", async (e) => {
    const id = Number(e.target.value);
    try {
      await applyEffect(id);
    } catch {}
  });

  els.btnAP.addEventListener("click", async () => {
    try {
      alert("Switching to AP mode");
      await setWiFiModeAP();
    } catch {
      alert("Failed to switch to AP");
    }
  });
  els.btnSTA.addEventListener("click", async () => {
    try {
      alert(
        "Switching to STA mode. Connect to your router's Wi-Fi network, then find the new IP address of the lamp. This page will be unavailable shortly."
      );
      const res = await setWiFiModeSTA();
      if (res && res.ok) alert("Switched to STA mode");
      else alert("Failed to switch to STA");
    } catch {
      alert("Failed to switch to STA");
    }
  });
}

(async function main() {
  populateEffects();
  bindEvents();
  await poll();
  setInterval(poll, 2000);
})();
