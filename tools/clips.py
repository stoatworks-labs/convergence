"""Resolume's own demo clips through `cvtest --pipe`: the alpha check, and
the measurement Bloom's default was set from.

    tools/clips.py alpha [--build DIR]      the output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   each clip's mean blue current at the top of
                                            Bloom's range and at its default: the
                                            measurement Bloom's default was decided by

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV
with alpha, decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal
otherwise, and a script running it in a loop loses its input). Nothing here
is a second copy of the plugin's formulas: every number is read off the
pipe's frames or its `--stats` lines, which read the blue gun's current back
from the plugin's own prep texture.

**alpha.** The plugin's decision: output alpha = mix( a, 1, Mix ) -- the
projected picture fills the frame, so at Mix 1 the output is opaque, and the
clip's light drives the guns weighted by its alpha (straight alpha over
black: a transparent pixel drives nothing). Checked on BattleWeapon_Tank_09
and Ethnik2, the two clips with real transparency: at Mix 1 every output
alpha is 255; at Mix 0.5 every output alpha is within half a code of
a / 2 + 127.5; and white painted under every fully transparent pixel changes
no output byte, at Mix 1 and at Mix 0.5 (the clips are straight alpha over
black, so without the paint a plugin that ignored alpha would pass). The
negative control paints ~200 opaque pixels instead, which must show.

**defaults.** Bloom is I_w, the blue gun's current at full white. US 4,845,403's
Table 1 gives the conventional blue tube's "exciting current for obtaining white color" as
320 uA. The spec first read that as the average a projector draws on a picture and meant to
set I_w so the median demo clip draws it. Measured here: for each of the 33 demo clips
(`media/AV`, `media/Shop74`), at 640x360, 30 frames from 1 s in, the median frame's mean blue
current at I_w = 10 mA, the top of the range (the mean only grows with I_w:
dI/dI_w = d I_s^2 / (I_s + I_w (1 - d))^2 > 0, so no I_w in range gives more). No clip reaches
320 uA -- the brightest draws about 210 uA, the median about 14 -- because the clips are dark
in blue (median mean linear drive 0.0055) and a calibrated tube's current at small drives is
about d I_s I_w / (I_s + I_w) < d I_s. So the average reading cannot be met, and Table 1 is
read as its own words say: the current of a white raster, every pixel at full drive, which is
I_w itself. The default is 0.32 mA, and this prints what the clips draw there; it fails if the
binary's default is anything else.
"""

import argparse
import math
import pathlib
import statistics
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]
TARGET_MA = 0.320


def bloom_param(milliamps):
    """Bloom is 0.1 .. 10 mA, geometric (the spec)."""
    return math.log(milliamps / 0.1) / math.log(100.0)


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(cvtest, raw, width, height, settings, stats=False, fps=30):
    args = [str(cvtest), "--pipe", "--size", f"{width}x{height}", "--fps", str(fps)]
    for s in settings:
        args += ["--set", s]
    if stats:
        args += ["--stats"]
    result = subprocess.run(args, input=raw, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"cvtest --pipe failed ({result.returncode}): {result.stderr.decode()[-400:]}")
    frames = np.frombuffer(result.stdout, dtype=np.uint8).reshape(-1, height, width, 4)
    lines = [ln.split() for ln in result.stderr.decode().splitlines() if ln.startswith("frame ")]
    currents = [float(ln[3]) for ln in lines]
    return frames, currents


def alpha_check(cvtest):
    width, height, count = 320, 180, 20
    failures, negative_caught = 0, 0
    for name in ALPHA_CLIPS:
        path = MEDIA / name
        if not path.exists():
            print(f"  FAIL  {path} is missing: the alpha check needs Resolume Arena's demo clips")
            return 1
        raw, frames = decode(path, width, height, count)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        a = clip[..., 3].astype(np.float64)
        full, _ = pipe(cvtest, raw, width, height, ["Mix=1"])
        half, _ = pipe(cvtest, raw, width, height, ["Mix=0.5"])
        opaque_alpha = int(full[..., 3].min())
        off_half = float(np.abs(half[..., 3].astype(np.float64) - (a / 2.0 + 127.5)).max())
        # The clips are straight alpha over black: where a = 0 the colour is
        # (almost always) black already, so a plugin that ignored alpha would
        # look the same. Paint every fully transparent pixel white: the output
        # must not change by a single byte.
        white = clip.copy()
        hidden = white[..., 3] == 0
        white[..., :3][hidden] = 255
        changed = 0
        for mix, reference in (("Mix=1", full), ("Mix=0.5", half)):
            painted, _ = pipe(cvtest, white.tobytes(), width, height, [mix])
            changed += int(np.count_nonzero(painted != reference))
        # The negative control: the same paint on a few opaque pixels must show.
        shown = clip.copy()
        opaque = np.argwhere(shown[..., 3] == 255)
        pick = opaque[:: max(1, len(opaque) // 200)]
        shown[pick[:, 0], pick[:, 1], pick[:, 2], :3] = 255
        control, _ = pipe(cvtest, shown.tobytes(), width, height, ["Mix=1"])
        negative_caught += int(np.count_nonzero(control != full)) > 0
        ok = opaque_alpha == 255 and off_half <= 0.5 + 1e-9 and changed == 0
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'}  {path.name}: {frames} frames at {width}x{height}, {hidden.mean():.0%} fully "
              f"transparent; Mix 1: alpha >= {opaque_alpha} (want 255); Mix 0.5: alpha within {off_half:.2f} codes of "
              f"a/2 + 127.5 (tolerance 0.5, the byte's rounding); white painted under every transparent pixel changes "
              f"{changed} output bytes at Mix 1 and 0.5 (want 0)")
    print(f"  {'ok  ' if negative_caught == len(ALPHA_CLIPS) else 'FAIL'}  negative control: white painted on ~200 "
          f"opaque pixels shows in the output on {negative_caught} of {len(ALPHA_CLIPS)} clips")
    failures += negative_caught != len(ALPHA_CLIPS)
    print(f"\n  {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def mean_current(cvtest, raw, width, height, milliamps):
    _, currents = pipe(cvtest, raw, width, height, [f"Bloom={bloom_param(milliamps):.7f}"], stats=True)
    return statistics.median(currents)


def bloom_default(cvtest):
    """Bloom's default in mA, read from the binary's own --list."""
    listing = subprocess.run([str(cvtest), "--list"], capture_output=True, text=True, check=True).stdout
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[1] == "Bloom":
            return 0.1 * 100.0 ** float(parts[3])
    raise RuntimeError("no Bloom in cvtest --list")


def defaults(cvtest):
    clips = sorted(p for p in (MEDIA / "AV").glob("*.mov")) + sorted(p for p in (MEDIA / "Shop74").glob("*.mov"))
    if not clips:
        print(f"no demo clips under {MEDIA}")
        return 1
    width, height = 640, 360
    default = bloom_default(cvtest)
    top, at_default = [], []
    for path in clips:
        raw, _ = decode(path, width, height, 30)
        hi = mean_current(cvtest, raw, width, height, 10.0)
        lo = mean_current(cvtest, raw, width, height, default)
        top.append(hi)
        at_default.append(lo)
        print(f"  {path.parent.name + '/' + path.name:38s} mean blue current {hi * 1000:7.2f} uA at I_w = 10 mA, "
              f"{lo * 1000:6.2f} uA at the default")
    reach = sum(i >= TARGET_MA for i in top)
    verdict = ("so no I_w in range makes the median clip draw it" if reach <= len(clips) // 2 else
               "so the average reading can be met: bisect for it instead")
    print(f"\n{len(clips)} clips: at I_w = 10 mA the mean blue current runs {min(top) * 1000:.2f} to {max(top) * 1000:.1f} uA "
          f"(median {statistics.median(top) * 1000:.1f}); {reach} reach 320 uA, {verdict}")
    print(f"Bloom's default in the binary: {default:.4f} mA (want 0.32: US 4,845,403 Table 1 read as a white raster's current); "
          f"there the clips draw a median {statistics.median(at_default) * 1000:.2f} uA, "
          f"{statistics.median(at_default) / default:.2%} of the white raster's")
    # --list prints the parameter to four decimals: ln(100) x 5e-5 = 2.3e-4 of the current.
    ok = reach <= len(clips) // 2 and abs(default - 0.32) <= 0.32 * math.log(100.0) * 5e-5
    print(f"\n  {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    build = args.build if args.build.is_absolute() else REPO / args.build
    cvtest = build / "cvtest"
    if not cvtest.exists():
        print(f"{cvtest} not found", file=sys.stderr)
        return 1
    if args.what == "alpha":
        print("\n=== alpha on a real clip: Resolume's DXV alpha clips through --pipe")
        return alpha_check(cvtest)
    return defaults(cvtest)


if __name__ == "__main__":
    sys.exit(main())
