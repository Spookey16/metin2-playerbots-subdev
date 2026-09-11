#!/usr/bin/env python3
"""Train a tiny MLP from Shinsoo ML replay and write pbml_policy.txt.

Keep OBS_DIM / N_ACTIONS in sync with overlays/playerbot-ml/src/game/src/pbml_types.h.
The game core is 32-bit C++; it loads this text file with gemv+ReLU+argmax.
No ONNX.

Replay layout (little-endian), written by pbml_replay.cpp when
PLAYERBOT_ML_REPLAY=1 (file lives in the channel cwd, next to playerbot_status.tsv):

  header: 8s magic='PBMLRP01', u16 obs_dim, u16 n_actions, u16 rec_size, u16 version=1
  record: obs_dim floats, u8 action, u8 done, u16 pad, f32 reward

Usage:
  python train_policy.py pbml_replay.bin -o pbml_policy.txt
  Copy pbml_policy.txt into the channel cwd (or set PLAYERBOT_ML_WEIGHTS).
"""
from __future__ import print_function

import argparse
import os
import struct
import sys

# Mirror pbml_types.h
OBS_DIM = 21
N_ACTIONS = 5
DEFAULT_HIDDEN = 32
HEADER_FMT = "<8sHHHH"
MAGIC = b"PBMLRP01"

ACTIONS = ("WAIT", "GO_HUNT", "APPROACH", "ATTACK", "WANDER")


def rec_fmt(obs_dim):
    return "<{}fBBHf".format(obs_dim)


def rec_size(obs_dim):
    return struct.calcsize(rec_fmt(obs_dim))


def load_replay(path):
    with open(path, "rb") as fp:
        raw = fp.read(struct.calcsize(HEADER_FMT))
        if len(raw) != struct.calcsize(HEADER_FMT):
            raise SystemExit("replay file too small: {}".format(path))
        magic, obs_dim, n_actions, stored_size, version = struct.unpack(HEADER_FMT, raw)
        if magic != MAGIC:
            raise SystemExit("bad magic {!r} (want PBMLRP01)".format(magic))
        if version != 1:
            raise SystemExit("unsupported replay version {}".format(version))
        if obs_dim != OBS_DIM or n_actions != N_ACTIONS:
            raise SystemExit(
                "replay contract mismatch: obs={} actions={} (trainer expects {} / {})".format(
                    obs_dim, n_actions, OBS_DIM, N_ACTIONS
                )
            )
        expected = rec_size(obs_dim)
        if stored_size != expected:
            raise SystemExit(
                "record size {} != expected {} (obs_dim={})".format(
                    stored_size, expected, obs_dim
                )
            )
        fmt = rec_fmt(obs_dim)
        obs_list = []
        act_list = []
        while True:
            chunk = fp.read(expected)
            if not chunk:
                break
            if len(chunk) != expected:
                break
            fields = struct.unpack(fmt, chunk)
            obs_list.append(fields[:obs_dim])
            act_list.append(int(fields[obs_dim]))
    return obs_list, act_list


def softmax(logits):
    import numpy as np

    shifted = logits - logits.max(axis=1, keepdims=True)
    exp = np.exp(shifted)
    return exp / exp.sum(axis=1, keepdims=True)


def train(obs, actions, hidden, epochs, lr, batch, seed):
    import numpy as np

    rng = np.random.RandomState(seed)
    x = np.asarray(obs, dtype=np.float32)
    y = np.asarray(actions, dtype=np.int64)
    n = x.shape[0]
    if n == 0:
        raise SystemExit("replay has no records")

    valid = (y >= 0) & (y < N_ACTIONS)
    x = x[valid]
    y = y[valid]
    n = x.shape[0]
    if n == 0:
        raise SystemExit("replay has no valid actions")

    w1 = rng.randn(hidden, OBS_DIM).astype(np.float32) * (1.0 / np.sqrt(OBS_DIM))
    b1 = np.zeros(hidden, dtype=np.float32)
    w2 = rng.randn(N_ACTIONS, hidden).astype(np.float32) * (1.0 / np.sqrt(hidden))
    b2 = np.zeros(N_ACTIONS, dtype=np.float32)

    counts = np.bincount(y, minlength=N_ACTIONS).astype(np.float32)
    print("records={} action_counts={}".format(n, counts.astype(int).tolist()))

    def forward(batch_x):
        h = np.maximum(0.0, batch_x.dot(w1.T) + b1)
        logits = h.dot(w2.T) + b2
        return h, logits

    order = np.arange(n)
    for epoch in range(1, epochs + 1):
        rng.shuffle(order)
        total_loss = 0.0
        seen = 0
        correct = 0
        for start in range(0, n, batch):
            idx = order[start : start + batch]
            bx = x[idx]
            by = y[idx]
            h, logits = forward(bx)
            p = softmax(logits)
            logp = np.log(np.clip(p[np.arange(len(by)), by], 1e-8, 1.0))
            loss = -logp.mean()
            total_loss += float(loss) * len(by)
            seen += len(by)
            correct += int((p.argmax(axis=1) == by).sum())

            dlogits = p
            dlogits[np.arange(len(by)), by] -= 1.0
            dlogits /= float(len(by))

            dw2 = dlogits.T.dot(h)
            db2 = dlogits.sum(axis=0)
            dh = dlogits.dot(w2)
            dh[h <= 0] = 0.0
            dw1 = dh.T.dot(bx)
            db1 = dh.sum(axis=0)

            w2 -= lr * dw2
            b2 -= lr * db2
            w1 -= lr * dw1
            b1 -= lr * db1

        acc = 100.0 * correct / float(seen)
        print("epoch {:3d}  loss={:.4f}  acc={:.1f}%".format(epoch, total_loss / seen, acc))

    return w1, b1, w2, b2


def write_policy(path, w1, b1, w2, b2):
    hidden = w1.shape[0]
    with open(path, "w") as fp:
        fp.write("PBMLMLP 1\n")
        fp.write("{} {} {}\n".format(OBS_DIM, hidden, N_ACTIONS))

        def dump(vec):
            flat = vec.reshape(-1)
            for i, value in enumerate(flat):
                fp.write("{:.8g}{}".format(float(value), "\n" if (i + 1) % 8 == 0 else " "))
            if len(flat) % 8 != 0:
                fp.write("\n")

        dump(w1)
        dump(b1)
        dump(w2)
        dump(b2)
    print("wrote {}  hidden={}".format(path, hidden))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("replay", nargs="?", default="pbml_replay.bin")
    parser.add_argument("-o", "--output", default="pbml_policy.txt")
    parser.add_argument("--hidden", type=int, default=DEFAULT_HIDDEN)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--lr", type=float, default=0.02)
    parser.add_argument("--batch", type=int, default=64)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    if args.hidden < 1 or args.hidden > 256:
        raise SystemExit("--hidden must be 1..256")
    if not os.path.isfile(args.replay):
        raise SystemExit("replay not found: {}".format(args.replay))

    try:
        import numpy as np  # noqa: F401
    except ImportError:
        raise SystemExit("numpy is required: pip install numpy")

    obs, actions = load_replay(args.replay)
    print("loaded {}  records={}".format(args.replay, len(obs)))
    if not obs:
        raise SystemExit("empty replay")

    illegal = [a for a in actions if a < 0 or a >= N_ACTIONS]
    if illegal:
        raise SystemExit("replay contains out-of-range actions")

    w1, b1, w2, b2 = train(obs, actions, args.hidden, args.epochs, args.lr, args.batch, args.seed)
    write_policy(args.output, w1, b1, w2, b2)
    print("copy {} next to the game core (or set PLAYERBOT_ML_WEIGHTS).".format(args.output))
    print("actions: {}".format(", ".join("{}={}".format(i, n) for i, n in enumerate(ACTIONS))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
