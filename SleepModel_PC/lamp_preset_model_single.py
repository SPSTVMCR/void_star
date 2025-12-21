"""
lamp_preset_model.py (drop-in replacement - STABLE TRAINING)

Fixes the Keras/TF "OptionalFromValue Toutput_types length 0" crash by:
- using a single-output model (one vector)
- avoiding multi-output + sample_weight dict edge cases
- validating/rebuilding the saved model if incompatible

Endpoints:
  GET  /presets
  POST /suggest
  POST /train
"""

import argparse
import math
import os
import socket
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


# Single output vector:
# [brightness(1), on(1), mimir(1), r(1), g(1), b(1), effect_onehot(56)]
Y_DIM = 6 + (EFFECT_MAX + 1)


def after_to_y(after: Dict[str, Any]) -> np.ndarray:
    b01 = clamp01(float(after.get("brightness", 0)) / BRIGHT_MAX)
    on = 1.0 if after.get("on", True) else 0.0
    mimir = 1.0 if after.get("mimir", False) else 0.0
    r, g, b = hex_to_rgb01(after.get("color", "FFFFFF"))
    eff = int(after.get("effect_id", 0))
    eff_oh = one_hot(eff, EFFECT_MAX + 1)
    return np.concatenate([np.array([b01, on, mimir, r, g, b], dtype=np.float32), eff_oh], axis=0)


def y_to_actions(y: np.ndarray) -> List[Dict[str, Any]]:
    b01 = float(y[0])
    on = bool(y[1] > 0.5)
    mimir = bool(y[2] > 0.5)

    r = int(round(clamp01(float(y[3])) * 255))
    g = int(round(clamp01(float(y[4])) * 255))
    b = int(round(clamp01(float(y[5])) * 255))
    hex_color = f"#{r:02X}{g:02X}{b:02X}"

    eff_id = int(np.argmax(y[6:]))

    brightness = int(round(clamp01(b01) * BRIGHT_MAX))

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
    out = tf.keras.layers.Dense(Y_DIM, activation="sigmoid", name="y")(x)
    model = tf.keras.Model(inp, out)
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3), loss="mse")
    return model


class ReplayBuffer:
    def __init__(self, maxlen: int = 8000):
        self.maxlen = maxlen
        self.X: List[np.ndarray] = []
        self.Y: List[np.ndarray] = []
        self.ts: List[int] = []

    def add(self, x: np.ndarray, y: np.ndarray, ts: int):
        self.X.append(x.astype(np.float32))
        self.Y.append(y.astype(np.float32))
        self.ts.append(int(ts))
        if len(self.X) > self.maxlen:
            self.X = self.X[-self.maxlen:]
            self.Y = self.Y[-self.maxlen:]
            self.ts = self.ts[-self.maxlen:]

    def sample(self, n: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        n = min(n, len(self.X))
        if n <= 0:
            return np.zeros((0,)), np.zeros((0,)), np.zeros((0,))
        idx = np.random.choice(len(self.X), size=n, replace=False)
        X = np.stack([self.X[i] for i in idx], axis=0)
        Y = np.stack([self.Y[i] for i in idx], axis=0)

        # recency weighting
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
    print(f"[mDNS] advertised {PC_MDNS_SERVICE_NAME} at {ip}:{port} (use http://{PC_MDNS_HOSTNAME}.local:{port})")
    return zc, info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lamp", default="voidstar.local")
    ap.add_argument("--lamp-port", type=int, default=80)
    ap.add_argument("--listen", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5055)
    ap.add_argument("--model", default="lamp_preset_model.keras")
    ap.add_argument("--preset-max", type=int, default=12)
    ap.add_argument("--train-batch", type=int, default=256)
    ap.add_argument("--online-epochs", type=int, default=2)
    ap.add_argument("--min-buffer", type=int, default=30)
    ap.add_argument("--rebuild-if-incompatible", action="store_true", default=True)
    args = ap.parse_args()

    lamp = LampClient(args.lamp, args.lamp_port)

    dummy = {"brightness": 64, "on": True, "mimir": False, "lux": 0.0, "color": "FFA500", "effect_id": 0}
    input_dim = status_to_features(dummy).shape[0]

    model = None
    if os.path.exists(args.model):
        try:
            m = tf.keras.models.load_model(args.model)
            # Validate output shape
            out_shape = tuple(m.output_shape)  # (None, Y_DIM)
            if len(out_shape) != 2 or out_shape[1] != Y_DIM:
                raise ValueError(f"incompatible saved model output_shape={out_shape}, expected (None,{Y_DIM})")
            model = m
            print(f"[MODEL] loaded {args.model} output_shape={out_shape}")
        except Exception as e:
            print(f"[MODEL] load failed or incompatible: {e}")
            if not args.rebuild_if_incompatible:
                raise

    if model is None:
        model = build_model(input_dim)
        model.save(args.model)
        print(f"[MODEL] built new model and saved {args.model} (Y_DIM={Y_DIM})")

    rb = ReplayBuffer(maxlen=8000)
    presets: List[PresetRecord] = []

    def add_preset(rec: PresetRecord):
        nonlocal presets
        presets.append(rec)
        presets = presets[-args.preset_max:]

    app = Flask(__name__)

    @app.after_request
    def add_cors_headers(resp):
        resp.headers["Access-Control-Allow-Origin"] = "*"
        resp.headers["Access-Control-Allow-Methods"] = "GET,POST,OPTIONS"
        resp.headers["Access-Control-Allow-Headers"] = "Content-Type"
        resp.headers["Access-Control-Max-Age"] = "600"
        return resp

    @app.get("/presets")
    def http_presets():
        data = [asdict(p) for p in presets][::-1]
        return jsonify({"ok": True, "presets": data})

    @app.post("/suggest")
    def http_suggest():
        j = request.get_json(force=True, silent=True) or {}
        ts = int(j.get("ts", int(time.time())))
        note = j.get("note", "manual_suggest")
        source = j.get("source", "pc-model")

        st = j.get("status")
        if not isinstance(st, dict):
            st = lamp.get_status()

        x = status_to_features(st, ts=ts)[None, :]
        y = model.predict(x, verbose=0)[0]
        actions = y_to_actions(y)

        rec = PresetRecord(
            ts=ts,
            bucket=bucket_from_hour(time.localtime(ts).tm_hour),
            source=source,
            note=note,
            actions=actions,
        )
        add_preset(rec)

        print(f"[suggest] ts={ts} bucket={rec.bucket} source={source} note={note} presets={len(presets)}/{args.preset_max}")
        return jsonify({"ok": True, "preset": asdict(rec)})

    @app.post("/train")
    def http_train():
        try:
            j = request.get_json(force=True, silent=False) or {}
            ts = int(j.get("ts", int(time.time())))
            before = j.get("before", {}) or {}
            after = j.get("after", {}) or {}
            note = j.get("note", "user_action")

            x = status_to_features(before, ts=ts)
            y = after_to_y(after)

            rb.add(x, y, ts=ts)
            buffer_n = len(rb)

            eff_before = before.get("effect_id", None)
            eff_after = after.get("effect_id", None)
            col_before = before.get("color", None)
            col_after = after.get("color", None)
            print(f"[train] note={note} ts={ts} buffer={buffer_n} eff {eff_before}->{eff_after} color {col_before}->{col_after}")

            if buffer_n < args.min_buffer:
                print(f"[train] skip: buffer<{args.min_buffer} (buffer={buffer_n})")
                return jsonify({"ok": True, "trained": False, "loss": None, "buffer": buffer_n, "note": note, "reason": "buffer<min"})

            batch_n = min(args.train_batch, buffer_n)
            Xb, Yb, wb = rb.sample(batch_n)

            if not hasattr(Xb, "shape") or Xb.shape[0] == 0:
                print("[train] skip: sampled empty batch")
                return jsonify({"ok": True, "trained": False, "loss": None, "buffer": buffer_n, "note": note, "reason": "sample_empty"})

            print(f"[train] fit: batch={Xb.shape[0]} epochs={max(1,args.online_epochs)} X={tuple(Xb.shape)} Y={tuple(Yb.shape)}")

            hist = model.fit(
                Xb,
                Yb,
                sample_weight=wb,
                epochs=max(1, args.online_epochs),
                batch_size=32,
                verbose=0,
            )

            loss = float(hist.history["loss"][-1]) if "loss" in hist.history else None
            model.save(args.model)

            print(f"[train] ok: loss={loss} saved={args.model}")
            return jsonify({"ok": True, "trained": True, "loss": loss, "buffer": buffer_n, "note": note})

        except Exception as e:
            print("[train] EXCEPTION:", repr(e))
            traceback.print_exc()
            return jsonify({"ok": False, "error": str(e)}), 500

    zc, info = start_mdns_advertisement(args.port)
    try:
        print(f"[HTTP] serving on {args.listen}:{args.port}")
        print(f"[HTTP] lamp base={lamp.base_url}")
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