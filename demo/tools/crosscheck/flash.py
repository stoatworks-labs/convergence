"""Count the flashes the browser demo can make, by xenon's rule.

    python3 demo/tools/crosscheck/flash.py [--size 960x540] [--seconds 5]

The rule is the fleet's (stoatworks-backend video/projects/xenon/flashcheck.py,
from ramdac's render.py): a transition is a change of at least 0.1 of white in
mean relative luminance, accumulated in one direction, whose darker end is
below 0.8; two opposing transitions are one flash; more than three flashes
(six transitions) in any one second fails. Counted over the whole frame and a
5 x 5 grid of overlapping windows, each a third of the frame each way, stepped
by a sixth, in linear light (sRGB transfer, Rec. 709 weights), every frame.

Measured on the page itself: window.convergenceDemo.flashRun renders the
page's own clips through a fresh instance on the page's clock, reads each
frame back as the canvas holds it (8-bit, premultiplied) and works out what
shows over the backdrop (black, or white for the transparent clip), then the
levels. The cases:

  - every clip at the defaults (what a visitor sees on arrival);
  - every control at an extreme at once, from a cold start, on four clips;
  - Power On pressed again and again (2, 6 and 10 a second) on a set with
    the most Cold Drift and the shortest Warm-up, so each press throws the
    side tubes back as far as they go;
  - Mix flipped between 0 and 1 at 1, 2, 3, 4 and 6 times a second on the
    transparent clip, over white and over the kit's checkerboard;
  - each control flipped between its two extremes 10 times a second (faster
    than a hand moves a slider) on the colour bars and on the transparent
    clip over white.

Not run by tools/verify.sh: it needs a GPU and Chrome. The page's
photosensitivity note (index.html) names the one move that crosses the line;
exit 1 if a case it does not name crosses, or one it names no longer does.
"""
import argparse
import functools
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

STEP, DARK, MAX_TRANSITIONS = 0.1, 0.8, 6

CLIPS = ["scene", "bars", "bars100", "ramp", "grid", "detail", "spot", "alpha"]
EXTREMES = {"Convergence": "1", "Zones": "9x9", "Setup": "1000", "Keystone": "1", "Warm-up": "0", "Cold Drift": "1",
            "Red Focus": "1", "Green Focus": "0", "Blue Focus": "0", "Astigmatism": "1", "Bloom": "1"}
# control, its two extremes, anything it needs to show.
FLIPS = [
    ("Convergence", "0", "1", {}),
    ("Zones", "5x5", "9x9", {"Convergence": "1"}),
    ("Setup", "1", "1000", {"Convergence": "1"}),
    ("Keystone", "0", "1", {}),
    ("Cold Drift", "0", "1", {"Warm-up": "1"}),
    ("Red Focus", "0", "1", {}),
    ("Green Focus", "0", "1", {}),
    ("Blue Focus", "0", "1", {}),
    ("Astigmatism", "0", "1", {}),
    ("Bloom", "0", "1", {}),
    ("Mix", "0", "1", {}),
]


def transitions(series):
    """Opposing changes of at least STEP of white, accumulated in one direction,
    whose darker end is below DARK: the frames they ended on (flashcheck.py's)."""
    out, direction = [], 0
    lo = hi = series[0] if len(series) else 0.0
    for i, v in enumerate(series):
        hi, lo = max(hi, v), min(lo, v)
        if direction >= 0 and hi - v >= STEP and v < DARK:
            out.append(i)
            direction, lo, hi = -1, v, v
        elif direction <= 0 and v - lo >= STEP and lo < DARK:
            out.append(i)
            direction, lo, hi = 1, v, v
    return out


def worst_second(ends, fps):
    n = int(round(fps))
    return max((sum(1 for e in ends[k:] if e - s < n) for k, s in enumerate(ends)), default=0)


def cases(fps, seconds):
    frames = int(round(fps * seconds))
    out = []
    for clip in CLIPS:
        out.append({"name": f"defaults {clip}", "clip": clip, "settings": {}, "changes": [], "bg": 0})
    out.append({"name": "defaults alpha over white", "clip": "alpha", "settings": {}, "changes": [], "bg": 1})
    for clip, bg in (("scene", 0), ("bars", 0), ("spot", 0), ("alpha", 1)):
        out.append({"name": f"every extreme, cold start, {clip}{' over white' if bg else ''}", "clip": clip,
                    "settings": EXTREMES, "changes": [[0, {"Power On": "1"}], [1, {"Power On": "0"}]], "bg": bg})
    for rate in (2, 6, 10):
        period = int(round(fps / rate))
        presses = []
        for f in range(0, frames, period):
            presses += [[f, {"Power On": "1"}], [f + 1, {"Power On": "0"}]]
        for clip in ("grid", "bars", "scene"):
            out.append({"name": f"Power On {rate}/s, Cold Drift 1, Warm-up 1 s, {clip}", "clip": clip,
                        "settings": {"Cold Drift": "1", "Warm-up": "0"}, "changes": presses, "bg": 0})
    # Mix over a transparent clip is the one move that swings a large area:
    # at 0 the backdrop shows through, at 1 the projected picture (black where
    # no gun is driven) fills it. At the rates a hand can move a slider, over
    # white and over the kit's checkerboard (0.118 of white on average).
    for rate in (1, 2, 3, 4, 6):
        step = fps / (2 * rate)
        flips = [[int(round(k * step)), {"Mix": "1" if k % 2 else "0"}] for k in range(int(frames / step) + 1)]
        for bg, label in ((1, "white"), (0.118, "the checkerboard")):
            out.append({"name": f"Mix 0<->1 {rate}/s, alpha over {label}", "clip": "alpha", "settings": {},
                        "changes": flips, "bg": bg, "warned": bg == 1 and rate >= 3})
    half = int(round(fps / 20))  # 10 a second: a change every half period
    for name, a, b, extra in FLIPS:
        flips = [[f, {name: b if (f // half) % 2 else a}] for f in range(0, frames, half)]
        for clip, bg in (("bars", 0), ("alpha", 1)):
            out.append({"name": f"{name} {a}<->{b} 10/s, {clip}{' over white' if bg else ''}", "clip": clip,
                        "settings": extra, "changes": flips, "bg": bg, "warned": name == "Mix" and bg == 1})
    return out


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/fc/job.json":
            body = json.dumps(self.state["job"]).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return None
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/fc/levels/"):
            self.state["levels"][urllib.parse.unquote(self.path[len("/fc/levels/"):])] = json.loads(body)
        elif self.path == "/fc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/fc/done":
            self.state["finished"].set()
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--size", default="960x540")
    parser.add_argument("--seconds", type=float, default=5.0)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--json", help="write every case's counts here")
    parser.add_argument("--match", help="run only the cases whose name contains this")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    args = parser.parse_args(argv)
    width, height = (int(v) for v in args.size.lower().split("x"))
    job = {"width": width, "height": height, "fps": args.fps, "seconds": args.seconds,
           "cases": [c for c in cases(args.fps, args.seconds) if not args.match or args.match in c["name"]]}
    state = {"job": job, "levels": {}, "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, state=state))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="convergence-flash-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{server.server_address[1]}/tools/crosscheck/flash.html",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(1800):
            sys.exit("the page never finished (1800 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        sys.exit(f"the page failed:\n{state['error']}")

    names = ["frame"] + [f"window r{j}c{i}" for j in range(5) for i in range(5)]
    over, results = [], []
    print(f"{width}x{height} at {args.fps:g} fps, {args.seconds:g} s a case; worst second, transitions "
          f"(the line: more than {MAX_TRANSITIONS})\n")
    print(f"{'case':<62} {'frame':>5} {'window':>6}  biggest swing")
    for c in job["cases"]:
        rows = state["levels"].get(c["name"])
        if rows is None:
            print(f"{c['name']:<62} no levels")
            over.append(c["name"])
            continue
        per = []
        for k, name in enumerate(names):
            series = [r[k] for r in rows]
            per.append((worst_second(transitions(series), args.fps), max(series) - min(series), name))
        frame = per[0][0]
        window = max(per[1:])
        swing = max(per, key=lambda p: p[1])
        bad = frame > MAX_TRANSITIONS or window[0] > MAX_TRANSITIONS
        # The page's photosensitivity note names exactly the cases marked
        # warned: one that stops crossing, or one not named that does, makes
        # the note wrong, and fails this.
        if bad != bool(c.get("warned")):
            over.append(c["name"] + (" (the page warns of it, and it no longer crosses)" if not bad else ""))
        results.append({"case": c["name"], "frame": frame, "worst_window": window[2], "window": window[0],
                        "swing": swing[1], "swing_at": swing[2], "warned": bool(c.get("warned"))})
        mark = ("  OVER (the page warns of it)" if c.get("warned") else "  OVER") if bad else ""
        print(f"{c['name']:<62} {frame:>5} {window[0]:>6}  {swing[1]:.3f} ({swing[2]}){mark}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(results, fh, indent=1)
            fh.write("\n")
    if over:
        print(f"\nthe page's photosensitivity note is wrong about {len(over)} case(s): " + "; ".join(over))
        return 1
    warned = [r["case"] for r in results if r["warned"]]
    print(f"\nevery case at or under three flashes a second, whole frame and every window, except the {len(warned)} "
          "the page's photosensitivity note names (Mix moved between 0 and 1 three or more times a second on the "
          "transparent clip over white)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
