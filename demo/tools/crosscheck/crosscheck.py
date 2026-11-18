"""The browser demo against the plugin: the same frames, the same clock, two renderers.

    python3 demo/tools/crosscheck/crosscheck.py [--cvtest build/cvtest] [--size 480x270] [--frames 12] [--keep DIR]

Each run below is rendered twice:

  - by the PLUGIN: `cvtest --pipe --stats` (the real ConvergencePlugin class in
    a GL 4.1 context, on the harness's seconds clock at --fps), fed the frames
    on stdin, with `--script` for a cue (Power On's press and release).
  - by the PAGE: demo/ served from here and loaded in headless Chrome,
    through window.convergenceDemo.renderRun, which makes a fresh instance,
    puts it on the same clock (SetClockScaleForTest( 1.0 ), SetTime( f / fps )),
    applies the same cues on the same frames, uploads each frame as cvtest does
    (value / 255 into RGBA32F) and reads the composite back from an RGBA32F
    target, quantised as cvtest quantises it.

The frames are generated here, straight alpha: a dim graded ground, a white
crosshatch of one- and two-pixel lines, red, green and magenta bars, a field of
bright blue patches and one-pixel blue stripes (Bloom's case), fine vertical
and horizontal stripes (Astigmatism's), a disc that moves, and a card whose
alpha ramps from 0 to 1.

Nothing in the plugin is random: the zone errors and the drift's direction
are a hash of Setup, and the warm-up is a function of the clock. So the two
renderers should agree to the bit wherever the GPUs compute the same floats,
and this counts the bytes that are identical, frame by frame. Where a pow, a
division or the blur's sum rounds a last bit differently between GL 4.1 and
WebGL2 through ANGLE on Metal, a byte can differ by a level. --stats's two
numbers, the mean blue-gun current (from the plugin's own texture A) and the
warm-up's state, are compared with the page's, frame by frame.

The CPU half is compared without a GPU too. cvdump.cpp is built here from
source/Projector.cpp and source/Controls.cpp, twice: with -ffp-contract=off,
so that neither side fuses a multiply-add (JavaScript never does) and what is
left is the C library's log, pow, exp, cos, erf and erfc against the
browser's and the page's own erf/erfc; and with the plugin's own Release
flags (-O3, Apple clang's default -ffp-contract=on, which fuses on arm64), to
say how far the plugin as shipped is from the page. Each prints the
conversions, Compute's frame, the blur kernels, the zone table and its
texels, the drift and every float uniform; the page's port
(window.convergenceDemo.cpu) works out the same. erf and erfc are compared
directly over 0..6, and PixelGaussian over a range of sigmas.

Before any picture, the page's table of controls (names, kinds, defaults) is
compared with `cvtest --list`; the option elements are compared by passing
them to cvtest by name in the runs below (it refuses a name it does not have).

A run passes when at least --exact % of its bytes are identical, no byte is
more than --max levels apart, no frame's mean is more than --mean levels of
255 apart and the warm-up state agrees to --warm relative on every frame.
Negative controls -- the page with one control one slider step (0.001) off,
Setup one off, a press of Power On it never sees -- are compared with the
plugin's run as it should be, and each must fail that test, so the comparison
is shown to see a slip of that size.

Not run by tools/verify.sh: it needs a GPU and Chrome. Exit 1 if a table
differs, a uniform is missing or unset, a run or a CPU number fails, or a
negative control passes.
"""
import argparse
import functools
import http.server
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True

POWER = [(6, {"Power On": "1"}), (7, {"Power On": "0"})]
# name, settings as `cvtest --set` takes them (host values; options by element
# name; Setup the integer), fps, cues [ (frame, {name: value}) ].
RUNS = [
    ("defaults", {}, 60, []),
    ("convergence-1pct", {"Convergence": "0.6667"}, 60, []),
    ("zones-9x9", {"Zones": "9x9", "Convergence": "0.5"}, 60, []),
    ("setup-42", {"Setup": "42", "Convergence": "0.5"}, 60, []),
    ("setup-1000", {"Setup": "1000", "Convergence": "1", "Zones": "9x9"}, 60, []),
    ("keystone-1", {"Keystone": "1"}, 60, []),
    ("keystone-half", {"Keystone": "0.5", "Convergence": "0.3"}, 60, []),
    ("warm-fast", {"Warm-up": "0", "Cold Drift": "1"}, 60, []),
    ("warm-fast-30fps", {"Warm-up": "0", "Cold Drift": "1"}, 30, []),
    ("power-on", {"Warm-up": "0.2", "Cold Drift": "1"}, 60, POWER),
    ("red-focus-0", {"Red Focus": "0"}, 60, []),
    ("blue-focus-1", {"Blue Focus": "1"}, 60, []),
    ("astig-vertical-sharp", {"Astigmatism": "1", "Red Focus": "0.25", "Green Focus": "0.25", "Blue Focus": "0.25"}, 60, []),
    ("astig-half", {"Astigmatism": "0.5", "Green Focus": "0.75"}, 60, []),
    ("bloom-10mA", {"Bloom": "1"}, 60, []),
    ("bloom-0.1mA", {"Bloom": "0"}, 60, []),
    ("mix-half", {"Mix": "0.5"}, 60, []),
    ("mix-0", {"Mix": "0"}, 60, []),
    ("extremes", {"Convergence": "1", "Zones": "9x9", "Setup": "1000", "Keystone": "1", "Warm-up": "0",
                  "Cold Drift": "1", "Red Focus": "1", "Green Focus": "0", "Blue Focus": "0", "Astigmatism": "1",
                  "Bloom": "1"}, 60, POWER),
]
# The negative controls: a run rendered by the page with one thing slightly
# off, compared with the plugin's run as it should be.
NEGATIVES = [
    ("defaults", "convergence-1-step", {"Convergence": "0.0676667"}, None, "Convergence one slider step (0.001) off"),
    ("keystone-half", "keystone-1-step", {"Keystone": "0.501"}, None, "Keystone one step off"),
    ("warm-fast", "warmup-1-step", {"Warm-up": "0.001"}, None, "Warm-up one step off"),
    ("warm-fast", "cold-drift-1-step", {"Cold Drift": "0.999"}, None, "Cold Drift one step off"),
    ("setup-42", "setup-43", {"Setup": "43"}, None, "Setup one off"),
    ("red-focus-0", "red-focus-1-step", {"Red Focus": "0.001"}, None, "Red Focus one step off"),
    ("blue-focus-1", "blue-focus-1-step", {"Blue Focus": "0.999"}, None, "Blue Focus one step off"),
    ("astig-half", "astigmatism-1-step", {"Astigmatism": "0.501"}, None, "Astigmatism one step off"),
    ("bloom-10mA", "bloom-1-step", {"Bloom": "0.999"}, None, "Bloom one step off (at 10 mA)"),
    ("mix-half", "mix-1-step", {"Mix": "0.501"}, None, "Mix one step off"),
    ("power-on", "power-ignored", {}, "power-ignored", "a press of Power On the page never sees"),
]
# The CPU numbers: settings by name, raster.
EXTREMES = {"Convergence": "1", "Zones": "9x9", "Setup": "1000", "Keystone": "1", "Warm-up": "1", "Cold Drift": "1",
            "Red Focus": "1", "Green Focus": "0", "Blue Focus": "0", "Astigmatism": "1", "Bloom": "1", "Mix": "0.5"}
CPU = [
    ({}, 480, 270),
    ({}, 960, 540),
    ({}, 1920, 1080),
    ({}, 3840, 2160),
    ({}, 1, 1),
    (EXTREMES, 1920, 1080),
    (EXTREMES, 5760, 3240),
    ({"Astigmatism": "0.5", "Red Focus": "0.25", "Green Focus": "0.25", "Blue Focus": "0.25"}, 1280, 720),
    ({"Bloom": "0"}, 720, 576),
    ({"Setup": "42", "Zones": "9x9", "Convergence": "0.5"}, 1024, 768),
    ({"Setup": "500.5", "Zones": "0.5", "Convergence": "0.123456", "Keystone": "0.3333"}, 1366, 768),
    ({"Setup": "0.4999", "Warm-up": "0.5", "Cold Drift": "0.77"}, 640, 360),
    ({"Setup": "1000.6", "Red Focus": "0.3333", "Green Focus": "0.6667", "Blue Focus": "0.9", "Bloom": "0.77"}, 2048, 858),
    ({"Setup": "-5", "Astigmatism": "0.123", "Mix": "1.5"}, 333, 777),
]
ORDER = ["Convergence", "Zones", "Setup", "Keystone", "Warm-up", "Cold Drift", "Power On", "Red Focus", "Green Focus",
         "Blue Focus", "Astigmatism", "Bloom", "Mix"]
OPTIONS = {"Zones": ["5x5", "9x9"]}
ERF_X = [round(i * 0.01, 2) for i in range(601)] + [0.5 / (s * math.sqrt(2.0)) for s in (0.017, 0.065, 0.2, 0.63)]
SIGMAS = [0.0, 0.017225, 0.065141, 0.1, 0.25, 0.3, 0.5, 0.63, 1.0, 2.5, 7.0, 20.0, 42.7, 100.0]


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def scene(width, height, count):
    """The frames, RGBA8 straight alpha, top row first."""
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    u, v = (x + 0.5) / width, (y + 0.5) / height
    out = []
    for f in range(count):
        t = f / 60.0
        grey = 0.05 + 0.30 * u + 0.04 * np.sin(2 * math.pi * (u * 5 + v * 2))
        rgb = np.stack([grey * 0.9, grey, grey * 1.1], axis=-1)
        alpha = np.ones((height, width))
        # The crosshatch: one-pixel lines every 24 px, two-pixel every 72.
        thin = (x % 24 == 11) | (y % 24 == 11)
        thick = (x % 72 < 2) | (y % 72 < 2)
        rgb[thin | thick] = 1.0
        # Bars: red, green, magenta.
        for i, colour in enumerate(((1, 0, 0), (0, 1, 0), (1, 0, 1))):
            bar = (u > 0.05 + 0.1 * i) & (u < 0.12 + 0.1 * i) & (v > 0.05) & (v < 0.25)
            rgb[bar] = colour
        # Bright blue: patches and one-pixel stripes.
        patch = (u > 0.40) & (u < 0.55) & (v > 0.06) & (v < 0.22)
        rgb[patch] = (0.0, 0.0, 1.0)
        stripes = (u > 0.58) & (u < 0.72) & (v > 0.06) & (v < 0.22) & (x % 2 == 0)
        rgb[stripes] = (0.1, 0.2, 1.0)
        # Fine detail: vertical stripes above horizontal ones.
        fine = (u > 0.05) & (u < 0.30) & (v > 0.35) & (v < 0.5)
        rgb[fine & (x % 3 == 0)] = 0.95
        fine = (u > 0.05) & (u < 0.30) & (v > 0.52) & (v < 0.67)
        rgb[fine & (y % 3 == 0)] = 0.95
        # A disc that moves.
        d = np.hypot((u - (0.45 + 0.4 * t)) * width / height, v - 0.55)
        rgb[d < 0.06] = (1.0, 0.95, 0.85)
        # A card whose alpha ramps.
        card = (u > 0.62) & (u < 0.95) & (v > 0.55) & (v < 0.9)
        rgb[card] = [0.85, 0.7, 0.5]
        alpha[card] = (u[card] - 0.62) / 0.33
        rgba = np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], axis=-1)
        out.append(np.round(rgba * 255).astype(np.uint8).tobytes())
    return out


def run_cvtest(cvtest, args, env, stdin=b""):
    done = subprocess.run([cvtest, *args], input=stdin, capture_output=True, check=False, env=env)
    if done.returncode != 0:
        sys.exit(f"cvtest {' '.join(args)} failed ({done.returncode}): {done.stderr.decode(errors='replace').strip()}")
    return done.stdout, done.stderr.decode(errors="replace")


STATS = re.compile(r"^frame (\d+) current (\S+) mA warm (\S+)$")
f32 = lambda value: struct.unpack("f", struct.pack("f", value))[0]


# ---------------------------------------------------------------- the server
class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job, the frames and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        if self.path.startswith("/xc/frame/"):
            return self.reply(self.state["frames"][int(self.path[len("/xc/frame/"):])], "application/octet-stream")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/xc/out/"):
            self.state["page"][self.path[len("/xc/out/"):]] = body
        elif self.path.startswith("/xc/stats/"):
            self.state["stats"][self.path[len("/xc/stats/"):]] = json.loads(body)
        elif self.path in ("/xc/table", "/xc/cpu", "/xc/erf", "/xc/gauss", "/xc/missing"):
            self.state[self.path[len("/xc/"):]] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------- the judging
def compare(a, b, width, height, frames):
    x = np.frombuffer(a, np.uint8).reshape(frames, height, width, 4).astype(np.int16)
    y = np.frombuffer(b, np.uint8).reshape(frames, height, width, 4).astype(np.int16)
    diff = np.abs(x - y)
    worst = diff.max(axis=3)
    means = np.abs(x[..., :3].mean(axis=(1, 2, 3)) - y[..., :3].mean(axis=(1, 2, 3)))
    return {
        "bytes": float((diff == 0).mean() * 100.0),
        "differ": int((diff != 0).sum()),
        "pixels": float((worst == 0).mean() * 100.0),
        "max": int(worst.max()),
        "mean": float(means.max()),
    }


def warm_agreement(want, got):
    """The warm-up's state, frame for frame: the worst relative difference."""
    if len(want) != len(got):
        return math.inf
    return max((abs(a[1] - b[1]) / max(abs(a[1]), 1e-300) for a, b in zip(want, got)), default=0.0)


def passes(s, warm, args):
    return s["bytes"] >= args.exact and s["max"] <= args.max and s["mean"] <= args.mean and warm <= args.warm


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(standard|option|integer|text|event)\s+(\S+)\s*$")


def compare_table(cvtest, page_table, env):
    """`cvtest --list` against the page's table: name, kind, default (4 dp), in order, up to the About block."""
    rows = []
    out, _ = run_cvtest(cvtest, ["--list"], env)
    for line in out.decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None:
            continue
        index, name, kind, value = m.groups()
        if kind == "text":
            break
        rows.append((int(index), name, kind, float(value)))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"cvtest lists {len(rows)} controls before the About block, the page {len(page_table)}")
    for (index, name, kind, value), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005:
            problems.append(f"#{index}: cvtest says {name} {kind} {value:.4f}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f}")
    return len(rows), problems


def host_values(settings):
    """The 13 host values cvdump takes, as strings: options by index, and "-"
    for a control left at the plugin's constructor default."""
    return [str(OPTIONS[n].index(settings[n])) if settings.get(n) in OPTIONS.get(n, []) else settings.get(n, "-")
            for n in ORDER]


def ulps(a, b):
    if a == b:
        return 0
    if math.copysign(1, a) != math.copysign(1, b):
        return ulps(abs(a), 0.0) + ulps(abs(b), 0.0)
    ia = struct.unpack("<q", struct.pack("<d", abs(a)))[0]
    ib = struct.unpack("<q", struct.pack("<d", abs(b)))[0]
    return abs(ia - ib)


class Tally:
    """Doubles counted by ulps apart, floats and ints exactly."""

    def __init__(self):
        self.doubles = self.exact = self.worst = 0
        self.where = ""
        self.floats = self.float_exact = 0
        self.float_worst = 0.0
        self.float_where = []
        self.ints_ok = True
        self.int_where = []

    def dbl(self, where, a, b):
        self.doubles += 1
        u = ulps(float(a), float(b))
        self.exact += u == 0
        if u > self.worst:
            self.worst, self.where = u, where
        return u

    def flt(self, where, a, b):
        self.floats += 1
        same = f32(a) == f32(b)
        self.float_exact += same
        if not same:
            self.float_worst = max(self.float_worst, abs(f32(a) - f32(b)) / max(abs(f32(a)), 1e-38))
            if len(self.float_where) < 4:
                self.float_where.append(where)

    def int(self, where, a, b):
        if a != b:
            self.ints_ok = False
            self.int_where.append(f"{where}: {a} vs {b}")


def compare_cpu(plugin, page):
    t = Tally()
    for i, (a, b) in enumerate(zip(plugin["params"], page["params"])):
        t.flt(f"params[{i}]", a, b)
    for key, value in plugin["conversions"].items():
        if key in ("zones", "setup"):
            t.int(f"conversions.{key}", value, page["conversions"][key])
        else:
            t.dbl(f"conversions.{key}", value, page["conversions"][key])
    for key, value in plugin["frame"].items():
        if isinstance(value, list):
            for i, (a, b) in enumerate(zip(value, page["frame"][key])):
                t.dbl(f"frame.{key}[{i}]", a, b)
        else:
            t.dbl(f"frame.{key}", value, page["frame"][key])
    t.int("kernels.radius", plugin["kernels"]["radius"], page["kernels"]["radius"])
    for r in range(4):
        for c in range(4):
            t.dbl(f"kernels.sigma[{r}][{c}]", plugin["kernels"]["sigma"][r][c], page["kernels"]["sigma"][r][c])
    t.int("kernels.texels", len(plugin["kernels"]["texels"]), len(page["kernels"]["texels"]))
    for i, (a, b) in enumerate(zip(plugin["kernels"]["texels"], page["kernels"]["texels"])):
        t.flt(f"kernels.texels[{i}]", a, b)
    for key in ("n", "setup"):
        t.int(f"zones.{key}", plugin["zones"][key], page["zones"][key])
    for i, (a, b) in enumerate(zip(plugin["zones"]["unit"], page["zones"]["unit"])):
        t.dbl(f"zones.unit[{i}]", a, b)
    for i, (a, b) in enumerate(zip(plugin["zones"]["texels"], page["zones"]["texels"])):
        t.flt(f"zones.texels[{i}]", a, b)
    for i in range(2):
        for j in range(2):
            t.dbl(f"drift.dirs[{i}][{j}]", plugin["drift"]["dirs"][i][j], page["drift"]["dirs"][i][j])
    for i, (a, b) in enumerate(zip(plugin["drift"]["uniform"], page["drift"]["uniform"])):
        t.flt(f"drift.uniform[{i}]", a, b)
    names = ["Is", "Iw", "IsIw", "SigmaOne2", "TwoP", "LayerStep", "KeystoneE", "KeystoneK", "MixAmount"]
    for i, (a, b) in enumerate(zip(plugin["uniforms"], page["uniforms"])):
        t.flt(f"uniform {names[i]}", a, b)
    return t


def build_dump(cxx, out, flags):
    built = subprocess.run([cxx, "-std=c++17", "-O3", "-DNDEBUG", *flags, "-I", os.path.join(REPO, "source"),
                            os.path.join(HERE, "cvdump.cpp"), os.path.join(REPO, "source", "Projector.cpp"),
                            os.path.join(REPO, "source", "Controls.cpp"), "-o", out],
                           capture_output=True, check=False)
    if built.returncode != 0:
        sys.exit(f"cvdump did not build:\n{built.stderr.decode(errors='replace')}")


def dump(binary, *args):
    return json.loads(subprocess.run([binary, *map(str, args)], capture_output=True, check=True).stdout)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--cvtest", default=os.path.join(REPO, "build", "cvtest"))
    parser.add_argument("--size", default="480x270")
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--keep", help="write each run's last frame, both renderers, here as PNGs")
    parser.add_argument("--exact", type=float, default=99.99,
                        help="share of bytes, %%, that must be identical over a run (default 99.99)")
    parser.add_argument("--max", type=int, default=1,
                        help="the most levels of 255 any byte may differ by (default 1)")
    parser.add_argument("--mean", type=float, default=0.01,
                        help="the largest difference of a frame's mean, levels of 255 (default 0.01)")
    parser.add_argument("--warm", type=float, default=1e-8,
                        help="the largest relative difference of the warm-up state on any frame (default 1e-8: "
                             "cvtest prints it to 9 digits)")
    parser.add_argument("--ulps", type=int, default=8,
                        help="the furthest apart a CPU double may be with -ffp-contract=off, in units in the last "
                             "place (default 8): the browser's log, pow, exp and cos and the page's erf and erfc "
                             "against the C library's")
    parser.add_argument("--gauss", type=float, default=1e-11,
                        help="the largest relative difference of a PixelGaussian weight, unfused (default 1e-11)")
    parser.add_argument("--erf", type=float, default=1e-14,
                        help="the largest relative difference of the page's erf or erfc from the C library's "
                             "(default 1e-14)")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--cxx", default="/usr/bin/clang++")
    parser.add_argument("--port", type=int, default=0, help="the local server's port (default: any free one)")
    args = parser.parse_args(argv)
    width, height = parse_size(args.size)
    if not os.access(args.cvtest, os.X_OK):
        sys.exit(f"no cvtest at {args.cvtest}: build it (cmake --build build --target cvtest)")

    work = tempfile.mkdtemp(prefix="convergence-xc-")
    # The harness writes the plugin's log; keep it out of ~/Library/Logs.
    env = {**os.environ, "CONVERGENCE_LOG_DIR": os.path.join(work, "log")}
    size = f"{width}x{height}"
    frame_bytes = width * height * 4

    # cvdump: the plugin's CPU half alone, unfused and as the plugin is built.
    unfused, shipped = os.path.join(work, "cvdump-unfused"), os.path.join(work, "cvdump-shipped")
    build_dump(args.cxx, unfused, ["-ffp-contract=off"])
    build_dump(args.cxx, shipped, [])
    plugin_cpu = {kind: [dump(b, w, h, *host_values(s)) for s, w, h in CPU] for kind, b in
                  (("unfused", unfused), ("shipped", shipped))}
    plugin_erf = dump(unfused, "erf", *(repr(x) for x in ERF_X))
    plugin_gauss = {kind: dump(b, "gauss", *(repr(s) for s in SIGMAS)) for kind, b in
                    (("unfused", unfused), ("shipped", shipped))}

    frames = scene(width, height, args.frames)
    stream = b"".join(frames)
    plugin, stats = {}, {}
    for name, settings, fps, cues in RUNS:
        sets = [x for k, v in settings.items() for x in ("--set", f"{k}={v}")]
        script = []
        if cues:
            path = os.path.join(work, f"{name}.cues")
            with open(path, "w", encoding="utf-8") as fh:
                for at, change in cues:
                    for k, v in change.items():
                        fh.write(f"{at} {k} {v}\n")
            script = ["--script", path]
        out, err = run_cvtest(args.cvtest, ["--pipe", "--size", size, "--fps", str(fps), "--stats", *script, *sets],
                              env, stream)
        if len(out) != frame_bytes * args.frames:
            sys.exit(f"cvtest --pipe for {name} wrote {len(out)} bytes, not {frame_bytes * args.frames}")
        plugin[name] = out
        stats[name] = [tuple(float(g) for g in m.groups()[1:]) for m in map(STATS.match, err.splitlines()) if m]

    runs = [{"name": n, "settings": s, "fps": fps, "changes": c, "frames": args.frames} for n, s, fps, c in RUNS]
    by_name = {r["name"]: r for r in runs}
    for base, name, change, negative, _ in NEGATIVES:
        r = {**by_name[base], "name": name, "settings": {**by_name[base]["settings"], **change}}
        if negative:
            r["negative"] = negative
        runs.append(r)
    cpu = [{"settings": s, "width": w, "height": h} for s, w, h in CPU]
    job = {"width": width, "height": height, "frames": args.frames, "runs": runs, "cpu": cpu, "erf": ERF_X,
           "sigmas": SIGMAS}

    state = {"job": job, "frames": frames, "page": {}, "stats": {}, "table": None, "cpu": None, "erf": None,
             "gauss": None, "missing": None, "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, state=state))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="convergence-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(900):
            sys.exit("the page never finished (900 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    failures = 0
    count, table_problems = compare_table(args.cvtest, state["table"] or [], env)
    if table_problems:
        failures += len(table_problems)
        print("the page's controls differ from cvtest --list:")
        for line in table_problems:
            print(f"  {line}")
    else:
        print(f"controls: all {count} of cvtest --list's before the About block, same names, kinds and defaults, "
              "in order; Zones' elements by name through cvtest --set")
    gaps = state["missing"] or {}
    for label, values in (("uniforms the page set that a program lacks", gaps.get("missing")),
                          ("uniforms a program has that the page never set", gaps.get("unset")),
                          ("GL errors", gaps.get("errors")), ("frames the instance refused", gaps.get("refused"))):
        if values:
            failures += 1
            print(f"{label}: {', '.join(sorted(set(values)))}")
    if not any(gaps.get(k) for k in ("missing", "unset", "errors", "refused")):
        print("every uniform of all three programs set, none missing, no GL error, no frame refused")

    # erf and erfc: the page's own against the C library's.
    worst = {"erf": (0.0, 0.0), "erfc": (0.0, 0.0)}
    for x, (pe, pc), (qe, qc) in zip(ERF_X, plugin_erf, state["erf"] or []):
        for key, a, b in (("erf", pe, qe), ("erfc", pc, qc)):
            rel = abs(a - b) / abs(a) if a else abs(b)
            if rel > worst[key][0]:
                worst[key] = (rel, x)
    erf_ok = worst["erf"][0] <= args.erf and worst["erfc"][0] <= args.erf and len(state["erf"] or []) == len(ERF_X)
    failures += 0 if erf_ok else 1
    print(f"\nerf and erfc at {len(ERF_X)} points over 0..6: the page's own against the C library's: worst relative "
          f"{worst['erf'][0]:.2g} (erf, x = {worst['erf'][1]:.4g}), {worst['erfc'][0]:.2g} (erfc, x = "
          f"{worst['erfc'][1]:.4g})  {'ok' if erf_ok else 'FAIL'} (pass: {args.erf:g})")

    # A tap far out on a wide kernel is the difference of two nearly equal
    # erfc values, so a last-bit difference in erfc comes out a relative one
    # of up to sigma times that in the weight: judged relative, not by ulps.
    for kind in ("unfused", "shipped"):
        t = Tally()
        rel, rel_where = 0.0, ""
        for s, a, b in zip(SIGMAS, plugin_gauss[kind], state["gauss"] or []):
            t.int(f"len sigma {s}", len(a), len(b))
            for i, (p, q) in enumerate(zip(a, b)):
                t.dbl(f"sigma {s} [{i}]", p, q)
                r = abs(p - q) / abs(p) if p else abs(q)
                if r > rel:
                    rel, rel_where = r, f"sigma {s} tap {i - len(a) // 2}"
        ok = t.ints_ok and (kind == "shipped" or rel <= args.gauss)
        failures += 0 if ok else 1
        print(f"PixelGaussian at {len(SIGMAS)} sigmas ({kind}): weights {t.exact}/{t.doubles} exact, worst {rel:.2g} "
              f"relative ({t.worst} ulps){' at ' + rel_where if rel_where else ''}; lengths "
              f"{'same' if t.ints_ok else 'DIFFER'}  {'ok' if ok else 'FAIL'}"
              f"{' (pass: ' + format(args.gauss, 'g') + ' relative)' if kind == 'unfused' else ' (reported)'}")

    for kind, label in (("unfused", "-O3 -ffp-contract=off"), ("shipped", "-O3, the plugin's own flags (fused on arm64)")):
        print(f"\nthe CPU half: cvdump (source/Projector.cpp, Controls.cpp at {label}) against the page's port")
        for (settings, w, h), mine, theirs in zip(CPU, state["cpu"] or [], plugin_cpu[kind]):
            c = compare_cpu(theirs, mine)
            if kind == "unfused":
                ok = c.ints_ok and c.float_exact == c.floats and c.worst <= args.ulps
            else:
                ok = c.ints_ok  # reported, not judged: the page cannot fuse
            failures += 0 if ok else 1
            label2 = f"{w}x{h} " + (",".join(f"{k}={v}" for k, v in settings.items()) or "defaults")
            print(f"  {label2}\n      doubles {c.exact}/{c.doubles} exact, worst {c.worst} ulps"
                  f"{' (' + c.where + ')' if c.where else ''}; floats {c.float_exact}/{c.floats} exact"
                  f"{' (worst ' + format(c.float_worst, '.2g') + ' rel: ' + ', '.join(c.float_where) + ')' if c.float_where else ''}"
                  f"; ints {'same' if c.ints_ok else 'DIFFER: ' + '; '.join(c.int_where)}  {'ok' if ok else 'FAIL'}")

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)

    print(f"\n{size}, {args.frames} frames a run\n")
    print(f"{'run':<22} {'bytes==':>9} {'bytes!=':>8} {'pix==':>9} {'max':>4} {'|dmean|':>8}  warm-up, blue current")
    for name, settings, fps, _ in RUNS:
        got = state["page"].get(name)
        if got is None or len(got) != frame_bytes * args.frames:
            print(f"{name:<22} no pictures from the page")
            failures += 1
            continue
        s = compare(plugin[name], got, width, height, args.frames)
        want, mine = stats[name], state["stats"].get(name, [])
        warm = warm_agreement(want, mine)
        same_current = sum(1 for a, b in zip(want, mine) if f"{a[0]:.9g}" == f"{b[0]:.9g}")
        cur_rel = max((abs(a[0] - b[0]) / max(abs(a[0]), 1e-300) for a, b in zip(want, mine)), default=0.0)
        ok = passes(s, warm, args) and len(want) == args.frames
        failures += 0 if ok else 1
        print(f"{name:<22} {s['bytes']:8.4f}% {s['differ']:>8} {s['pixels']:8.4f}% {s['max']:>4} {s['mean']:8.5f}  "
              f"warm worst {warm:.2g} rel (last {want[-1][1]:.9g}); current {same_current}/{len(want)} equal to 9 digits, "
              f"worst {cur_rel:.2g} rel  {'ok' if ok else 'FAIL'}")
        if args.keep:
            last = slice(frame_bytes * (args.frames - 1), frame_bytes * args.frames)
            for label, data in (("plugin", plugin[name][last]), ("page", got[last])):
                Image.frombytes("RGBA", (width, height), data).save(os.path.join(args.keep, f"{name}-{label}.png"))

    print("\nnegative controls: the page a little off, against the plugin as it should be")
    for base, name, _, _, label in NEGATIVES:
        late = state["page"].get(name)
        if late is None:
            print(f"  {label:<44} no pictures")
            failures += 1
            continue
        s = compare(plugin[base], late, width, height, args.frames)
        warm = warm_agreement(stats[base], state["stats"].get(name, []))
        caught = not passes(s, warm, args)
        failures += 0 if caught else 1
        print(f"  {label:<44} {s['bytes']:8.4f}% {s['max']:>4} {s['mean']:8.5f}  warm {warm:.2g}  "
              f"{'caught' if caught else 'NOT CAUGHT'}")
    print(f"\n{'every run passes, every CPU number agrees, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: {args.exact}% of bytes identical over the run, none more than {args.max} level apart, no frame's "
          f"mean more than {args.mean} levels apart, the warm-up within {args.warm:g} relative; CPU doubles within "
          f"{args.ulps} ulps unfused and every float handed to the GPU identical)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
