import argparse
import json
import math
import os
import random
import time
from typing import Dict, Any, Optional, Tuple

import numpy as np
import tensorflow as tf

EFFECT_MAX = 55
BRIGHT_MAX = 255
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


def one_hot(idx: int, n: int) -> np.ndarray:
    v = np.zeros((n,), dtype=np.float32)
    if 0 <= idx < n:
        v[idx] = 1.0
    return v


def clamp01(x: float) -> float:
    return max(0.0, min(1.0, float(x)))


def rgb01_to_hex(rgb: Tuple[float, float, float]) -> str:
    r = int(round(clamp01(rgb[0]) * 255))
    g = int(round(clamp01(rgb[1]) * 255))
    b = int(round(clamp01(rgb[2]) * 255))
    return f"{r:02X}{g:02X}{b:02X}"


def hex_to_rgb01(hexstr: str) -> Tuple[float, float, float]:
    s = (hexstr or "").strip()
    if s.startswith("#"):
        s = s[1:]
    if len(s) != 6:
        return (1.0, 1.0, 1.0)
    return (int(s[0:2], 16) / 255.0, int(s[2:4], 16) / 255.0, int(s[4:6], 16) / 255.0)


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


SAFE_EFFECTS = [
    0,   # Static
    2,   # Breath
    11,  # Rainbow
    12,  # Rainbow Cycle
    18,  # Running Lights
    19,  # Twinkle
    21,  # Twinkle Fade
    23,  # Sparkle
    24,  # Flash Sparkle
    40,  # Running Color
    44,  # Comet
    48, 49,  # Fire Flicker
    15,  # Fade (kept, but will be down-weighted)
]

BUCKET_COLOR_ANCHORS = {
    "morning": ["#FFD27D", "#FFA500", "#FFB347", "#FFE4A1"],
    "noon": ["#3FA9F5", "#00BFFF", "#2ECCFF", "#00FFFF"],
    "afternoon": ["#00FF7F", "#2ECC71", "#7CFC00", "#32CD32"],
    "evening": ["#FF5A5F", "#FF00AA", "#C77DFF", "#7B2CBF"],
    "night": ["#FF0000", "#FF3B30", "#8B0000", "#4B0082"],
}


def rand_ts_in_bucket(bucket: str) -> int:
    hours = {
        "morning": (7, 10),
        "noon": (11, 13),
        "afternoon": (14, 17),
        "evening": (18, 22),
        "night": (23, 5),
    }
    if bucket != "night":
        h = random.randint(hours[bucket][0], hours[bucket][1])
    else:
        h = random.choice([23, 0, 1, 2, 3, 4, 5])
    m = random.randint(0, 59)
    s = random.randint(0, 59)
    lt = time.localtime()
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, h, m, s, 0, 0, -1)))


def jitter_rgb(rgb: Tuple[float, float, float], amt: float = 0.05) -> Tuple[float, float, float]:
    return (
        clamp01(rgb[0] + random.uniform(-amt, amt)),
        clamp01(rgb[1] + random.uniform(-amt, amt)),
        clamp01(rgb[2] + random.uniform(-amt, amt)),
    )


def choose_brightness(bucket: str) -> int:
    base = {
        "morning": 120,
        "noon": 165,
        "afternoon": 150,
        "evening": 105,
        "night": 55,
    }[bucket]
    v = int(round(random.gauss(base, 16)))
    return max(5, min(255, v))


def choose_effect(bucket: str) -> int:
    weights = []
    for e in SAFE_EFFECTS:
        w = 1.0
        if e == 15:  # Fade
            w = 0.55  # de-emphasize
        if bucket == "night" and e in (0, 2, 21, 48, 49):
            w *= 1.8
        if bucket == "noon" and e in (11, 12, 40):
            w *= 1.5
        if bucket == "morning" and e in (0, 2):
            w *= 1.3
        if bucket == "evening" and e in (44, 19, 21):
            w *= 1.2
        weights.append(w)
    return random.choices(SAFE_EFFECTS, weights=weights, k=1)[0]


def gen_one_sample() -> Tuple[np.ndarray, Dict[str, np.ndarray]]:
    bucket = random.choice(BUCKETS)
    ts = rand_ts_in_bucket(bucket)

    before = {
        "brightness": random.randint(0, 255),
        "on": random.random() > 0.02,
        "mimir": random.random() < 0.12,
        "lux": random.uniform(0, 400),
        "color": rgb01_to_hex((random.random(), random.random(), random.random())),
        "effect_id": random.randint(0, EFFECT_MAX),
    }

    color_hex = random.choice(BUCKET_COLOR_ANCHORS[bucket])
    rgb = jitter_rgb(hex_to_rgb01(color_hex), 0.06)

    after = {
        "brightness": choose_brightness(bucket),
        "on": True,
        "mimir": False,
        "lux": before["lux"],
        "color": rgb01_to_hex(rgb),
        "effect_id": choose_effect(bucket),
    }

    x = status_to_features(before, ts=ts)
    y = after_to_targets(after)
    return x, y


def build_dataset(n: int, seed: int) -> Tuple[np.ndarray, Dict[str, np.ndarray]]:
    random.seed(seed)
    np.random.seed(seed)

    xs = []
    y_ctrl = []
    y_rgb = []
    y_eff = []
    for _ in range(n):
        x, y = gen_one_sample()
        xs.append(x)
        y_ctrl.append(y["y_ctrl"])
        y_rgb.append(y["y_rgb"])
        y_eff.append(y["y_eff"])

    X = np.stack(xs, axis=0).astype(np.float32)
    Y = {
        "y_ctrl": np.stack(y_ctrl, axis=0).astype(np.float32),
        "y_rgb": np.stack(y_rgb, axis=0).astype(np.float32),
        "y_eff": np.stack(y_eff, axis=0).astype(np.float32),
    }
    return X, Y


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="lamp_preset_model.keras")
    ap.add_argument("--samples", type=int, default=10000)
    ap.add_argument("--epochs", type=int, default=7)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    dummy = {"brightness": 64, "on": True, "mimir": False, "lux": 0.0, "color": "FFA500", "effect_id": 0}
    input_dim = status_to_features(dummy, ts=int(time.time())).shape[0]

    X, Y = build_dataset(args.samples, args.seed)
    model = build_model(input_dim)
    model.fit(X, Y, epochs=args.epochs, batch_size=args.batch, verbose=2, validation_split=0.05, shuffle=True)
    model.save(args.out)

    meta = {
        "created_at": int(time.time()),
        "samples": args.samples,
        "epochs": args.epochs,
        "batch": args.batch,
        "seed": args.seed,
        "safe_effects": SAFE_EFFECTS,
        "bucket_color_anchors": BUCKET_COLOR_ANCHORS,
    }
    meta_path = os.path.splitext(args.out)[0] + "_pretrain_config.json"
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, sort_keys=True)

    print(f"[pretrain] saved model: {args.out}")
    print(f"[pretrain] saved config: {meta_path}")


if __name__ == "__main__":
    os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
    main()