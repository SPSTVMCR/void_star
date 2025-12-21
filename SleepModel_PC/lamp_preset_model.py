import argparse
import json
import math
import os
import socket
import threading
import time
import traceback
from dataclasses import dataclass, asdict
from typing import Dict, Any, List, Optional, Tuple

import numpy as np
import requests
import tensorflow as tf
from flask import Flask, request, jsonify
from zeroconf import Zeroconf, ServiceInfo

EFFECT_MAX = 55
BRIGHT_MAX = 255

PC_MDNS_HOSTNAME = "sleepmodel"
PC_MDNS_SERVICE_NAME = "SleepLamp Model._http._tcp.local."
PC_MDNS_TYPE = "_http._tcp.local."

BUCKETS = ["morning", "noon", "afternoon", "evening", "night"]

SERVER_VERSION = {"name": "sleepmodel", "api": 4, "date": "2025-12-20"}


def bucket_from_hour(h: int) -> str:
    if 6 <= h <= 10:
        return "morning"
    if 11 <= h <= 13:
        return "noon"
    if 14 <= h <= 17:
        return "afternoon"
    if 18 <= h <= 22:
        return "evening"
    return "night"


def resolve_host(host: str) -> str:
    try:
        return socket.gethostbyname(host)
    except Exception:
        return host


def get_local_ip_for_mdns() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


class LampClient:
    def __init__(self, host: str, port: int = 80, timeout: float = 4.0):
        self.host = host
        self.port = port
        self.timeout = timeout

    @property
    def base_url(self) -> str:
        h = resolve_host(self.host)
        return f"http://{h}:{self.port}"

    def get_status(self) -> Dict[str, Any]:
        r = requests.get(f"{self.base_url}/status", timeout=self.timeout)
        r.raise_for_status()
        return r.json()

    def apply_actions(self, actions: List[Dict[str, Any]], source: str, note: str, ts: Optional[int] = None) -> Dict[str, Any]:
        if ts is None:
            ts = int(time.time())
        payload = {"ts": int(ts), "source": source, "note": note, "actions": actions}
        r = requests.post(f"{self.base_url}/applyPreset", json=payload, timeout=self.timeout)
        r.raise_for_status()
        return r.json()


def clamp01(x: float) -> float:
    return max(0.0, min(1.0, float(x)))


def hex_to_rgb01(hexstr: str) -> Tuple[float, float, float]:
    s = (hexstr or "").strip()
    if s.startswith("#"):
        s = s[1:]
    if len(s) != 6:
        return (1.0, 1.0, 1.0)
    return (int(s[0:2], 16) / 255.0, int(s[2:4], 16) / 255.0, int(s[4:6], 16) / 255.0)


def one_hot(idx: int, n: int) -> np.ndarray:
    v = np.zeros((n,), dtype=np.float32)
    if 0 <= idx < n:
        v[idx] = 1.0
    return v


def time_features(ts: Optional[int] = None) -> np.ndarray:
    if ts is None:
        ts = int(time.time())
    lt = time.localtime(ts)
    h = lt.tm_hour
    ang = 2.0 * math.pi * (h / 24.0)
    hs = math.sin(ang)
    hc = math.cos(ang)
    b = bucket_from_hour(h)
    oh = np.zeros((len(BUCKETS),), dtype=np.float32)
    oh[BUCKETS.index(b)] = 1.0
    return np.concatenate([np.array([hs, hc], dtype=np.float32), oh], axis=0)


def status_to_features(st: Dict[str, Any], ts: Optional[int] = None) -> np.ndarray:
    brightness = float(st.get("brightness", 0)) / BRIGHT_MAX
    on = 1.0 if st.get("on", True) else 0.0
    mimir = 1.0 if st.get("mimir", False) else 0.0
    lux = clamp01(float(st.get("lux", 0.0)) / 400.0)
    r, g, b = hex_to_rgb01(st.get("color", "FFFFFF"))
    eff = int(st.get("effect_id", 0))
    eff_oh = one_hot(eff, EFFECT_MAX + 1)
    tfv = time_features(ts)
    return np.concatenate([np.array([brightness, on, mimir, lux, r, g, b], dtype=np.float32), eff_oh, tfv], axis=0)


def after_to_targets(after: Dict[str, Any]) -> Dict[str, np.ndarray]:
    b01 = clamp01(float(after.get("brightness", 0)) / BRIGHT_MAX)
    on = 1.0 if after.get("on", True) else 0.0
    mimir = 1.0 if after.get("mimir", False) else 0.0
    y_ctrl = np.array([b01, on, mimir], dtype=np.float32)

    r, g, b = hex_to_rgb01(after.get("color", "FFFFFF"))
    y_rgb = np.array([r, g, b], dtype=np.float32)

    eff = int(after.get("effect_id", 0))
    y_eff = one_hot(eff, EFFECT_MAX + 1)
    return {"y_ctrl": y_ctrl, "y_rgb": y_rgb, "y_eff": y_eff}


def outputs_to_actions(y_ctrl: np.ndarray, y_rgb: np.ndarray, y_eff: np.ndarray) -> List[Dict[str, Any]]:
    brightness = int(round(clamp01(float(y_ctrl[0])) * BRIGHT_MAX))
    on = bool(y_ctrl[1] > 0.5)
    mimir = bool(y_ctrl[2] > 0.5)

    r = int(round(clamp01(float(y_rgb[0])) * 255))
    g = int(round(clamp01(float(y_rgb[1])) * 255))
    b = int(round(clamp01(float(y_rgb[2])) * 255))
    hex_color = f"#{r:02X}{g:02X}{b:02X}"

    eff_id = int(np.argmax(y_eff))

    return [
        {"type": "set_power", "on": on},
        {"type": "set_mimir", "on": mimir},
        {"type": "set_brightness", "value": brightness},
        {"type": "set_color", "hex": hex_color},
        {"type": "set_effect", "id": int(np.clip(eff_id, 0, EFFECT_MAX))},
    ]


def build_model(input_dim: int) -> tf.keras.Model:
    inp = tf.keras.Input(shape=(input_dim,), name="x")
    x = tf.keras.layers.Dense(192, activation="relu")(inp)
    x = tf.keras.layers.Dense(192, activation="relu")(x)
    x = tf.keras.layers.Dropout(0.15)(x)

    y_ctrl = tf.keras.layers.Dense(3, activation="sigmoid", name="y_ctrl")(x)
    y_rgb = tf.keras.layers.Dense(3, activation="sigmoid", name="y_rgb")(x)
    y_eff = tf.keras.layers.Dense(EFFECT_MAX + 1, activation="softmax", name="y_eff")(x)

    model = tf.keras.Model(inputs=inp, outputs={"y_ctrl": y_ctrl, "y_rgb": y_rgb, "y_eff": y_eff})
    model.compile(
        optimizer=tf.keras.optimizers.Adam(1e-3),
        loss={"y_ctrl": "mse", "y_rgb": "mse", "y_eff": "categorical_crossentropy"},
        loss_weights={"y_ctrl": 1.0, "y_rgb": 2.0, "y_eff": 3.0},
    )
    return model


class ReplayBuffer:
    def __init__(self, maxlen: int = 8000):
        self.maxlen = maxlen
        self.X: List[np.ndarray] = []
        self.Y_ctrl: List[np.ndarray] = []
        self.Y_rgb: List[np.ndarray] = []
        self.Y_eff: List[np.ndarray] = []
        self.ts: List[int] = []

    def add(self, x: np.ndarray, ydict: Dict[str, np.ndarray], ts: int):
        self.X.append(x.astype(np.float32))
        self.Y_ctrl.append(ydict["y_ctrl"].astype(np.float32))
        self.Y_rgb.append(ydict["y_rgb"].astype(np.float32))
        self.Y_eff.append(ydict["y_eff"].astype(np.float32))
        self.ts.append(int(ts))

        if len(self.X) > self.maxlen:
            self.X = self.X[-self.maxlen:]
            self.Y_ctrl = self.Y_ctrl[-self.maxlen:]
            self.Y_rgb = self.Y_rgb[-self.maxlen:]
            self.Y_eff = self.Y_eff[-self.maxlen:]
            self.ts = self.ts[-self.maxlen:]

    def sample(self, n: int) -> Tuple[np.ndarray, Dict[str, np.ndarray], np.ndarray]:
        n = min(n, len(self.X))
        if n <= 0:
            return np.zeros((0,)), {"y_ctrl": np.zeros((0,)), "y_rgb": np.zeros((0,)), "y_eff": np.zeros((0,))}, np.zeros((0,))

        idx = np.random.choice(len(self.X), size=n, replace=False)
        X = np.stack([self.X[i] for i in idx], axis=0)
        Y = {
            "y_ctrl": np.stack([self.Y_ctrl[i] for i in idx], axis=0),
            "y_rgb": np.stack([self.Y_rgb[i] for i in idx], axis=0),
            "y_eff": np.stack([self.Y_eff[i] for i in idx], axis=0),
        }

        now = int(time.time())
        age_s = np.array([max(0, now - self.ts[i]) for i in idx], dtype=np.float32)
        w = np.power(0.5, age_s / 21600.0).astype(np.float32)
        w = np.clip(w, 0.2, 2.0)
        return X, Y, w

    def __len__(self):
        return len(self.X)


@dataclass
class PresetRecord:
    ts: int
    bucket: str
    source: str
    note: str
    actions: List[Dict[str, Any]]
    category: str  # auto | manual


def start_mdns_advertisement(port: int) -> Tuple[Zeroconf, ServiceInfo]:
    ip = get_local_ip_for_mdns()
    zc = Zeroconf()
    props = {b"path": b"/", b"service": b"SleepLampPresetModel", b"hostname": (PC_MDNS_HOSTNAME + ".local").encode("utf-8")}
    info = ServiceInfo(
        type_=PC_MDNS_TYPE,
        name=PC_MDNS_SERVICE_NAME,
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties=props,
        server=(PC_MDNS_HOSTNAME + ".local."),
    )
    zc.register_service(info)
    return zc, info


def safe_read_json(path: str) -> Optional[Dict[str, Any]]:
    try:
        if not os.path.exists(path):
            return None
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def safe_write_json(path: str, obj: Dict[str, Any]) -> None:
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, sort_keys=True)
    os.replace(tmp, path)


def date_key_local(ts: Optional[int] = None) -> str:
    if ts is None:
        ts = int(time.time())
    lt = time.localtime(ts)
    return f"{lt.tm_year:04d}-{lt.tm_mon:02d}-{lt.tm_mday:02d}"


def normalize_after_state(after: Dict[str, Any]) -> Dict[str, Any]:
    on = bool(after.get("on", True))
    mimir = bool(after.get("mimir", False))

    b = int(after.get("brightness", 0))
    bq = int(round(b / 5.0) * 5)
    bq = max(0, min(255, bq))

    color = str(after.get("color", "FFFFFF")).strip()
    if not color.startswith("#"):
        color = "#" + color
    color = color.upper()
    if len(color) != 7:
        color = "#FFFFFF"

    eff = int(after.get("effect_id", 0))
    eff = int(max(0, min(EFFECT_MAX, eff)))

    return {"on": on, "mimir": mimir, "brightness": bq, "color": color, "effect_id": eff}


def signature_from_norm(n: Dict[str, Any]) -> str:
    return f"p={1 if n['on'] else 0};m={1 if n['mimir'] else 0};b={n['brightness']};c={n['color']};e={n['effect_id']}"


def actions_from_signature(sig: str) -> List[Dict[str, Any]]:
    parts = dict(p.split("=", 1) for p in sig.split(";") if "=" in p)
    norm = {
        "on": parts.get("p", "1") == "1",
        "mimir": parts.get("m", "0") == "1",
        "brightness": int(parts.get("b", "0")),
        "color": parts.get("c", "#FFFFFF"),
        "effect_id": int(parts.get("e", "0")),
    }
    return [
        {"type": "set_power", "on": bool(norm["on"])},
        {"type": "set_mimir", "on": bool(norm["mimir"])},
        {"type": "set_brightness", "value": int(norm["brightness"])},
        {"type": "set_color", "hex": str(norm["color"])},
        {"type": "set_effect", "id": int(norm["effect_id"])},
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lamp", default="voidstar.local")
    ap.add_argument("--lamp-port", type=int, default=80)
    ap.add_argument("--listen", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5055)
    ap.add_argument("--model", default="lamp_preset_model.keras")

    ap.add_argument("--train-batch", type=int, default=256)
    ap.add_argument("--online-steps", type=int, default=2)
    ap.add_argument("--min-buffer", type=int, default=30)

    ap.add_argument("--preset-max", type=int, default=240)
    ap.add_argument("--presets-file", default="presets.json")

    ap.add_argument("--mode-file", default="mode.json")
    ap.add_argument("--usage-file", default="usage_counts.json")

    ap.add_argument("--sched-check-s", type=int, default=30)
    ap.add_argument("--sched-cooldown-s", type=int, default=60)

    ap.add_argument("--auto-per-bucket-per-day", type=int, default=2)
    ap.add_argument("--manual-per-day", type=int, default=5)
    ap.add_argument("--seed-on-startup", action="store_true", default=True)
    ap.add_argument("--seed-on-rollover", action="store_true", default=True)

    ap.add_argument("--waitress", action="store_true", default=False)
    args = ap.parse_args()

    lamp = LampClient(args.lamp, args.lamp_port)

    dummy = {"brightness": 64, "on": True, "mimir": False, "lux": 0.0, "color": "FFA500", "effect_id": 0}
    input_dim = status_to_features(dummy).shape[0]

    model = None
    if os.path.exists(args.model):
        try:
            model = tf.keras.models.load_model(args.model)
            out_keys = list(getattr(model, "output_names", []))
            if set(out_keys) != {"y_ctrl", "y_rgb", "y_eff"}:
                raise ValueError("incompatible model outputs")
        except Exception:
            model = None

    if model is None:
        model = build_model(input_dim)
        model.save(args.model)

    rb = ReplayBuffer(maxlen=8000)

    presets_lock = threading.Lock()
    presets: List[PresetRecord] = []

    mode_lock = threading.Lock()
    mode_state = {"mode": "off", "apply_on_time_change": False}

    usage_lock = threading.Lock()
    usage: Dict[str, Dict[str, int]] = {b: {} for b in BUCKETS}

    daily_lock = threading.Lock()
    daily_state = {"date": date_key_local(), "auto_counts": {b: 0 for b in BUCKETS}, "manual_count": 0}

    def load_mode():
        j = safe_read_json(args.mode_file)
        if isinstance(j, dict):
            mode_state["mode"] = str(j.get("mode", mode_state["mode"]))
            mode_state["apply_on_time_change"] = bool(j.get("apply_on_time_change", mode_state["apply_on_time_change"]))
        if mode_state["mode"] not in ("off", "suggest", "schedule_top"):
            mode_state["mode"] = "off"

    def save_mode():
        safe_write_json(args.mode_file, mode_state)

    def load_usage():
        j = safe_read_json(args.usage_file)
        if not isinstance(j, dict):
            return
        with usage_lock:
            for b in BUCKETS:
                d = j.get(b, {})
                if isinstance(d, dict):
                    usage[b] = {str(k): int(v) for k, v in d.items() if isinstance(v, (int, float))}

    def save_usage():
        with usage_lock:
            safe_write_json(args.usage_file, usage)

    def inc_usage(bucket: str, sig: str):
        with usage_lock:
            d = usage.setdefault(bucket, {})
            d[sig] = int(d.get(sig, 0)) + 1

    def top_signature(bucket: str) -> Optional[Tuple[str, int]]:
        with usage_lock:
            d = usage.get(bucket, {})
            if not d:
                return None
            sig = max(d.keys(), key=lambda k: d[k])
            return sig, int(d[sig])

    def load_presets():
        j = safe_read_json(args.presets_file)
        if not isinstance(j, dict):
            return
        arr = j.get("presets", [])
        if not isinstance(arr, list):
            return
        restored: List[PresetRecord] = []
        for it in arr:
            if not isinstance(it, dict):
                continue
            try:
                restored.append(
                    PresetRecord(
                        ts=int(it.get("ts", 0)),
                        bucket=str(it.get("bucket", "")),
                        source=str(it.get("source", "")),
                        note=str(it.get("note", "")),
                        actions=list(it.get("actions", [])),
                        category=str(it.get("category", "manual")).lower(),
                    )
                )
            except Exception:
                continue
        with presets_lock:
            presets.clear()
            presets.extend(restored[-args.preset_max:])

    def save_presets():
        with presets_lock:
            safe_write_json(args.presets_file, {"presets": [asdict(p) for p in presets]})

    def _predict_actions_for_status(st: Dict[str, Any], ts: int) -> List[Dict[str, Any]]:
        x = status_to_features(st, ts=ts)[None, :]
        pred = model.predict(x, verbose=0)
        return outputs_to_actions(pred["y_ctrl"][0], pred["y_rgb"][0], pred["y_eff"][0])

    def _representative_ts_for_bucket(bucket: str, day_ts: Optional[int] = None) -> int:
        if day_ts is None:
            day_ts = int(time.time())
        lt = time.localtime(day_ts)
        hour = {"morning": 8, "noon": 12, "afternoon": 16, "evening": 20, "night": 1}[bucket]
        return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, hour, 0, 0, 0, 0, -1)))

    def recount_today_counts(today: str):
        auto_counts = {b: 0 for b in BUCKETS}
        manual_count = 0
        with presets_lock:
            for p in presets:
                if date_key_local(p.ts) != today:
                    continue
                if p.category == "auto" and p.bucket in BUCKETS:
                    auto_counts[p.bucket] += 1
                elif p.category != "auto":
                    manual_count += 1
        with daily_lock:
            daily_state["date"] = today
            daily_state["auto_counts"] = auto_counts
            daily_state["manual_count"] = manual_count

    def prune_to_today(today: str):
        with presets_lock:
            presets[:] = [p for p in presets if date_key_local(p.ts) == today]
            if len(presets) > args.preset_max:
                presets[:] = presets[-args.preset_max:]

    def prune_autos_to_caps(today: str):
        with presets_lock:
            autos_today = [p for p in presets if p.category == "auto" and p.bucket in BUCKETS and date_key_local(p.ts) == today]
            autos_today.sort(key=lambda p: p.ts, reverse=True)
            kept_auto_by_bucket: Dict[str, List[PresetRecord]] = {b: [] for b in BUCKETS}
            for p in autos_today:
                if len(kept_auto_by_bucket[p.bucket]) < args.auto_per_bucket_per_day:
                    kept_auto_by_bucket[p.bucket].append(p)
            kept_autos = []
            for b in BUCKETS:
                kept_autos.extend(kept_auto_by_bucket[b])

            manuals_today = [p for p in presets if p.category != "auto" and date_key_local(p.ts) == today]
            presets[:] = manuals_today + kept_autos
            presets.sort(key=lambda p: p.ts)
            if len(presets) > args.preset_max:
                presets[:] = presets[-args.preset_max:]

        recount_today_counts(today)

    def replace_oldest_manual_today(today: str, new_rec: PresetRecord):
        with presets_lock:
            manuals = [(i, p) for i, p in enumerate(presets) if p.category != "auto" and date_key_local(p.ts) == today]
            if len(manuals) < args.manual_per_day:
                presets.append(new_rec)
                return
            oldest_i, _ = min(manuals, key=lambda t: t[1].ts)
            presets[oldest_i] = new_rec

    def replace_oldest_auto_in_bucket_today(today: str, bucket: str, new_rec: PresetRecord):
        with presets_lock:
            autos = [(i, p) for i, p in enumerate(presets) if p.category == "auto" and p.bucket == bucket and date_key_local(p.ts) == today]
            if len(autos) < args.auto_per_bucket_per_day:
                presets.append(new_rec)
                return
            oldest_i, _ = min(autos, key=lambda t: t[1].ts)
            presets[oldest_i] = new_rec

    def can_add_auto(today: str, bucket: str) -> bool:
        with daily_lock:
            if daily_state["date"] != today:
                return True
            return int(daily_state["auto_counts"].get(bucket, 0)) < args.auto_per_bucket_per_day

    def seed_day(reason: str, now_ts: Optional[int] = None):
        if now_ts is None:
            now_ts = int(time.time())
        today = date_key_local(now_ts)
        try:
            st = lamp.get_status()
        except Exception:
            return

        for b in BUCKETS:
            while can_add_auto(today, b):
                ts_b = _representative_ts_for_bucket(b, day_ts=now_ts)
                actions = _predict_actions_for_status(st, ts=ts_b)
                rec = PresetRecord(ts=int(ts_b), bucket=b, source="pc-seed", note=f"seed:{reason}", actions=actions, category="auto")
                with presets_lock:
                    presets.append(rec)
                recount_today_counts(today)
                if not can_add_auto(today, b):
                    break

        prune_autos_to_caps(today)
        save_presets()

    def rollover_if_needed(now_ts: Optional[int] = None):
        if now_ts is None:
            now_ts = int(time.time())
        today = date_key_local(now_ts)
        with daily_lock:
            if today == daily_state["date"]:
                return
            daily_state["date"] = today
            daily_state["auto_counts"] = {b: 0 for b in BUCKETS}
            daily_state["manual_count"] = 0

        with presets_lock:
            presets.clear()

        save_presets()
        if args.seed_on_rollover:
            seed_day("rollover", now_ts=now_ts)

    def load_state():
        load_mode()
        load_usage()
        load_presets()
        today = date_key_local()
        prune_to_today(today)
        prune_autos_to_caps(today)
        recount_today_counts(today)
        save_presets()

    load_state()

    if args.seed_on_startup:
        seed_day("startup", now_ts=int(time.time()))

    app = Flask(__name__)

    @app.after_request
    def add_cors_headers(resp):
        resp.headers["Access-Control-Allow-Origin"] = "*"
        resp.headers["Access-Control-Allow-Methods"] = "GET,POST,OPTIONS"
        resp.headers["Access-Control-Allow-Headers"] = "Content-Type"
        resp.headers["Access-Control-Max-Age"] = "600"
        return resp

    @app.get("/health")
    def http_health():
        return jsonify({"ok": True})

    @app.get("/version")
    def http_version():
        return jsonify({"ok": True, **SERVER_VERSION})

    @app.get("/presets")
    def http_presets():
        rollover_if_needed()
        with presets_lock:
            data = [asdict(p) for p in presets][::-1]
        return jsonify({"ok": True, "presets": data})

    @app.get("/mode")
    def http_get_mode():
        with mode_lock:
            return jsonify({"ok": True, **mode_state})

    @app.post("/mode")
    def http_set_mode():
        j = request.get_json(force=True, silent=True) or {}
        mode = str(j.get("mode", mode_state["mode"])).strip()
        apply_flag = bool(j.get("apply_on_time_change", mode_state["apply_on_time_change"]))

        if mode not in ("off", "suggest", "schedule_top"):
            return jsonify({"ok": False, "error": "invalid mode"}), 400

        with mode_lock:
            mode_state["mode"] = mode
            mode_state["apply_on_time_change"] = apply_flag
            save_mode()

        return jsonify({"ok": True, **mode_state})

    @app.get("/stats")
    def http_stats():
        rollover_if_needed()
        today = date_key_local()
        recount_today_counts(today)
        with daily_lock:
            daily = {
                "date": daily_state["date"],
                "auto_counts": dict(daily_state["auto_counts"]),
                "auto_limit": args.auto_per_bucket_per_day,
                "manual_count": int(daily_state["manual_count"]),
                "manual_limit": args.manual_per_day,
            }
        with mode_lock:
            mode = dict(mode_state)
        return jsonify({"ok": True, **mode, **daily, "presets_cached": len(presets)})

    @app.post("/suggest")
    def http_suggest():
        try:
            j = request.get_json(force=True, silent=True) or {}
            ts = int(j.get("ts", int(time.time())))
            rollover_if_needed(ts)
            today = date_key_local(ts)

            st = j.get("status")
            if not isinstance(st, dict):
                st = lamp.get_status()

            actions = _predict_actions_for_status(st, ts=ts)
            bucket = bucket_from_hour(time.localtime(ts).tm_hour)

            category = str(j.get("category", "manual")).lower()
            if category not in ("manual", "auto"):
                category = "manual"

            rec = PresetRecord(
                ts=ts,
                bucket=bucket,
                source=str(j.get("source", "pc-model")),
                note=str(j.get("note", "manual_suggest")),
                actions=actions,
                category=category,
            )

            if category == "manual":
                replace_oldest_manual_today(today, rec)
                prune_autos_to_caps(today)
                recount_today_counts(today)
                save_presets()
                return jsonify({"ok": True, "preset": asdict(rec)})

            if category == "auto":
                replace_oldest_auto_in_bucket_today(today, bucket, rec)
                prune_autos_to_caps(today)
                recount_today_counts(today)
                save_presets()
                return jsonify({"ok": True, "preset": asdict(rec)})

            return jsonify({"ok": False, "error": "invalid category"}), 400

        except Exception as e:
            traceback.print_exc()
            return jsonify({"ok": False, "error": str(e)}), 500

    @app.post("/train")
    def http_train():
        try:
            j = request.get_json(force=True, silent=False) or {}
            ts = int(j.get("ts", int(time.time())))
            before = j.get("before", {}) or {}
            after = j.get("after", {}) or {}
            note = j.get("note", "user_action")

            rollover_if_needed(ts)
            today = date_key_local(ts)

            x = status_to_features(before, ts=ts)
            y = after_to_targets(after)
            rb.add(x, y, ts=ts)

            b = bucket_from_hour(time.localtime(ts).tm_hour)
            sig = signature_from_norm(normalize_after_state(after))
            inc_usage(b, sig)
            if (len(rb) % 10) == 0:
                try:
                    save_usage()
                except Exception:
                    pass

            buffer_n = len(rb)
            trained = False
            loss = None
            reason = ""

            if buffer_n < args.min_buffer:
                reason = "buffer<min"
            else:
                batch_n = min(args.train_batch, buffer_n)
                Xb, Yb, wb = rb.sample(batch_n)
                if not hasattr(Xb, "shape") or Xb.shape[0] == 0:
                    reason = "sample_empty"
                else:
                    sw = {"y_ctrl": wb, "y_rgb": wb, "y_eff": wb}
                    out = None
                    for _ in range(max(1, args.online_steps)):
                        out = model.train_on_batch(Xb, Yb, sample_weight=sw, return_dict=True)
                    loss = float(out.get("loss")) if out and out.get("loss") is not None else None
                    model.save(args.model)
                    trained = True

            prune_autos_to_caps(today)
            recount_today_counts(today)
            save_presets()

            return jsonify({"ok": True, "trained": trained, "loss": loss, "buffer": buffer_n, "note": note, "reason": reason})

        except Exception as e:
            traceback.print_exc()
            return jsonify({"ok": False, "error": str(e)}), 500

    sched_state = {"last_bucket": None, "last_action_ts": 0}

    def scheduler_loop():
        while True:
            try:
                now = int(time.time())
                rollover_if_needed(now)
                today = date_key_local(now)

                bucket = bucket_from_hour(time.localtime(now).tm_hour)
                if sched_state["last_bucket"] is None:
                    sched_state["last_bucket"] = bucket

                if bucket != sched_state["last_bucket"]:
                    prev = sched_state["last_bucket"]
                    sched_state["last_bucket"] = bucket

                    if now - sched_state["last_action_ts"] < args.sched_cooldown_s:
                        time.sleep(args.sched_check_s)
                        continue

                    with mode_lock:
                        mode = mode_state["mode"]
                        apply_flag = bool(mode_state["apply_on_time_change"])

                    if mode == "off":
                        sched_state["last_action_ts"] = now
                        time.sleep(args.sched_check_s)
                        continue

                    top = top_signature(bucket)
                    if mode == "schedule_top" and top:
                        sig, count = top
                        actions = actions_from_signature(sig)
                        note_txt = f"time_bucket:{prev}->{bucket} mode=schedule_top picked={count}"
                    else:
                        st = lamp.get_status()
                        actions = _predict_actions_for_status(st, ts=now)
                        note_txt = f"time_bucket:{prev}->{bucket} mode={mode} model"

                    if apply_flag:
                        try:
                            lamp.apply_actions(actions, source="pc-scheduler", note=note_txt, ts=now)
                        except Exception:
                            pass

                    rec = PresetRecord(ts=now, bucket=bucket, source="pc-scheduler", note=note_txt, actions=actions, category="auto")
                    replace_oldest_auto_in_bucket_today(today, bucket, rec)

                    prune_autos_to_caps(today)
                    recount_today_counts(today)
                    save_presets()

                    sched_state["last_action_ts"] = now
                    time.sleep(args.sched_check_s)
                    continue

            except Exception:
                traceback.print_exc()

            time.sleep(args.sched_check_s)

    threading.Thread(target=scheduler_loop, name="Scheduler", daemon=True).start()

    zc, info = start_mdns_advertisement(args.port)
    try:
        if args.waitress:
            from waitress import serve
            serve(app, host=args.listen, port=args.port)
        else:
            app.run(host=args.listen, port=args.port, debug=False)
    finally:
        try:
            zc.unregister_service(info)
            zc.close()
        except Exception:
            pass


if __name__ == "__main__":
    os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
    main()