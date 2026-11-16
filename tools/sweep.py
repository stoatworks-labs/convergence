"""Render every parameter at both ends of its range and fail if any made no
difference. (ferrofluid's script by way of fountain, filings and
intensifier; the table is convergence's.)

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 60 frames (1 s at
60 fps) at 320x180 on the harness's card; nothing here is random, so two
renders differ only by what the control did. Zones and Setup act on the
convergence errors, so they are swept with Convergence up. Warm-up acts on
the cold drift, so it is swept with Cold Drift up; Power On restarts the
warm-up, so it is pressed at frame 30 with a 1 s warm-up and Cold Drift up
(unpressed, the drift has decayed for 1 s; pressed, for half a second). The
focus controls are swept from 0 to 1 with astigmatism set: defocus either
side of best focus blurs the same, so without astigmatism the two ends are
one picture (by design; Sturm's interval puts the line foci on opposite
sides, and astigmatism tells the sides apart). Bloom is swept on a clip of
alternate white and black columns: it widens the blue spot only, by 0.2 px
at 320x180 even at 10 mA (the spec's honest scale), so on the card's broad
shapes it barely shows, while a one-pixel grating, the finest bright detail
there is, loses its blue. An option is set here by name.
A control whose ends differ by less than `--floor` (mean 8-bit difference
per channel) is barely alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, the addendum's honesty check: what
goes dead there is the list of controls that only act with another one set,
which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

CONVERGED_BADLY = ["Convergence=0.5"]
COLD = ["Cold Drift=1"]
ASTIGMATIC = ["Astigmatism=0.5"]

# name -> (low settings, high settings, context settings); a list inside is argv as is.
SWEEP = {
    "Convergence": (["Convergence=0"], ["Convergence=1"], []),
    "Zones": (["Zones=5x5"], ["Zones=9x9"], CONVERGED_BADLY),
    "Setup": (["Setup=1"], ["Setup=1000"], CONVERGED_BADLY),
    "Keystone": (["Keystone=0"], ["Keystone=1"], []),
    "Warm-up": (["Warm-up=0"], ["Warm-up=1"], COLD),
    "Cold Drift": (["Cold Drift=0"], ["Cold Drift=1"], []),
    "Power On": ([], [["--at", "30", "Power On=1"]], ["Warm-up=0"] + COLD),
    "Red Focus": (["Red Focus=0"], ["Red Focus=1"], ASTIGMATIC),
    "Green Focus": (["Green Focus=0"], ["Green Focus=1"], ASTIGMATIC),
    "Blue Focus": (["Blue Focus=0"], ["Blue Focus=1"], ASTIGMATIC),
    "Astigmatism": (["Astigmatism=0"], ["Astigmatism=1"], []),
    "Bloom": (["Bloom=0"], ["Bloom=1"], ["@grating"]),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for cvtest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


WIDTH, HEIGHT = 320, 180


def grating(path):
    """Alternate white and black columns, opaque: raw RGBA, top row first."""
    row = bytes([255, 255, 255, 255, 0, 0, 0, 255]) * (WIDTH // 2)
    path.write_bytes(row * HEIGHT)
    return path


def render(cvtest, out, settings):
    args = [str(cvtest), "--out", str(out), "--size", f"{WIDTH}x{HEIGHT}", "--frames", "60"]
    for setting in settings:
        if setting == "@grating":
            args += ["--clip", str(grating(out.with_suffix(".rgba")))]
        else:
            args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"cvtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(cvtest):
    result = subprocess.run([str(cvtest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"cvtest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(cvtest, scratch, name, bare):
    low, high, context = SWEEP[name]
    context = [] if bare else context
    tag = f"p{abs(hash(name))}"
    before = render(cvtest, scratch / f"{tag}-a.png", context + low)
    after = render(cvtest, scratch / f"{tag}-b.png", context + high)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    cvtest = build / "cvtest"
    if not cvtest.exists():
        print(f"{cvtest} not found", file=sys.stderr)
        return 1

    plugin = "SW Convergence"
    declared = parameters(cvtest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            names = [n for n in declared if n in SWEEP]
            results = list(pool.map(lambda n: sweep_one(cvtest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {plugin}: {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
    print(f"{plugin}: {len(results)} parameters swept{' (bare: no context)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
