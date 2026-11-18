# convergence

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume.** What's established is established by an offline harness, `cvtest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context on a synthetic clock, on this Mac's GPU at two rasters and on Apple's software
> renderer. Read off the picture, the colour fringes at **every zone point** of 5 × 5 and 9 × 9
> grids (fringes up to 22 px) match the installation's set errors to within **0.514** of a
> derived tolerance; whole-pixel shifts are **bit-exact** and fractional ones good to
> **5.4e-5 px**; the warm-up drift's time constant comes back as **2.000020, 1.999957 and
> 1.999839 s** for 2 s at 30, 60 and 240 fps. **23** deliberately wrong models and **46**
> one-character mutants of the shipped code are all caught. The throw, the warm-up's time
> constant and size, the focus blur's scale and the zone layout are **chosen**, not measured:
> see [Status](#status).

A three-tube CRT projector throwing your clip onto the screen, for
[Resolume](https://resolume.com) Arena and Avenue: an FFGL effect, **SW Convergence**.

The green tube looks straight at the screen; the red and blue tubes sit either side and look
at it from 7.8°. The set's convergence electronics bend each side tube's picture to land on
the green one, **zone by zone**, and a technician set those zones by eye, never quite exactly.
So a white line is clean in one part of the screen, has a red edge on the left somewhere else
and a blue one above it in a corner; white text has red and blue edges; magenta shows where red
and blue land together without green. Switch the set on cold and the side tubes sit off as a
whole, closing up as it warms. Each tube focuses on its own, and an astigmatic gun sharpens
horizontal lines at one end of its focus control and vertical lines at the other. Push the blue
gun hard and its spot grows: the blue phosphor saturates, so bright blue costs it
disproportionate current. Nothing is drawn: it all comes out of where each tube's picture
lands and how big each tube's spot is.

## Controls

| Group | |
| --- | --- |
| **Convergence** | Convergence (the technician's error at each zone point, RMS 0–1.5% of the picture width), Zones (5x5 like a Barco Cine 8 or Sony VPH-G90, 9x9 like a Sony KP-43T70), Setup (1–1000: which installation; the same number is the same grid everywhere), Keystone (how much of the side tubes' trapezoid the set leaves uncorrected, 0–1). |
| **Warm-up** | Warm-up (the time constant, 1 s to 30 minutes), Cold Drift (how far off the side tubes start, 0–1% of the width), Power On (the set is cold again). |
| **Focus** | Red Focus, Green Focus, Blue Focus (0.5 is sharp; either side softens), Astigmatism. |
| **Beam** | Bloom (the blue gun's current at full white, 0.1–10 mA). |
| **Look** | Mix. |

The defaults are a converged set with a 0.1% error at each of 25 zones (about two pixels at
1080p: the NEC setup guide's "one or two test pattern line thicknesses"), keystone corrected,
warming up with a 400 s time constant (Barco's 20-minute warm-up is three of them) from 0.1%
off, every tube in focus, and Bloom at **0.32 mA**, the blue tube's current for white in a 1989
Kasei Optonix and Hitachi patent. That default was decided on Resolume's own demo clips: read
as the average current a picture draws, 320 µA is out of reach for every one of the 33 clips
anywhere in Bloom's range (the brightest draws 210 µA at 10 mA), so it is a white raster's.

**Bloom is small, because the real thing is.** The blue spot at 10 mA is 1.26 pixels at 1080p
and a tenth of that at the default, so it shows on fine bright detail, not on broad shapes.
Blue Focus and Astigmatism soften far more.

**The focus controls are symmetric** about 0.5: defocus either way blurs the same, until
Astigmatism tells the two sides apart.

Over a transparent part of a clip the projected picture still fills the screen (the fringes
put light there); a transparent pixel drives no gun. The output is opaque at Mix 1, and Mix
blends premultiplied, so nothing under a transparent pixel ever shows.

## Status

**v0.1.0, local only, 16 November 2026. Never loaded into Resolume.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). Every picture check runs at **two rasters**, 1280×720 and
320×180, and again on **Apple's software renderer** at 320×180. The numbers below are from
`cvtest --all` on the same commit (an arm64 build, the same code); 320×180 in brackets.

| check | result |
| --- | --- |
| `--model` | Hitachi's 0.099 and 0.256 mm spots back from the spot law; Philips' 2.2 from I_s; Kasei's γ 0.61 at 0.85 mA to its two digits; the homography within **3.6e-12** of a 3D ray-plane intersection; the zone table's mean, variance and red-blue correlation over 2000 Setups within 5 standard errors; the controls' conversions exact |
| `--shift` | whole-pixel shifts: **0** of 908,208 pixels differ, **0** of 13,414 uncovered pixels lit, the red tube showing the red signal, bit for bit; fractional shifts off by **5.37e-5 px** (7.29e-6) against 9.81e-4 (2.49e-4) |
| `--zones` | every zone point and cell centre of 5 × 5 and 9 × 9 grids, Setups 7 and 11, fringes up to 22.4 px (5.6): worst error **0.381–0.475** (0.440–0.514) of the tolerance (float, the resampler's centroid bias where the field slopes, the kink at a zone point) |
| `--keystone` | the rightmost line's red **−19.559 px** and blue **+21.105** (opposite sides) at Keystone 1; worst error 0.333 and 0.355 (0.758 and 0.696) of the tolerance at Keystone 1 and 0.5, against the harness's own inverse homography |
| `--warmup` | τ = 2 s fitted as **2.000020**, **1.999957** and **1.999839 s** at 30, 60 and 240 fps (±1.8e-3); the drift every half second within 0.167 (0.165) of its tolerance; the three rates agree to 2.2e-4 px (5.5e-5); Power On restarts it, within 0.049 |
| `--focus` | each tube's x and y spread at six settings of Focus and Astigmatism, 108 dots: worst **0.005** (0.003) of a tolerance of 1e-4 of the variance |
| `--bloom` | 231 (120) dots at 0.5, 2 and 8 mA: the decoded drive within 0.103 and the current within 0.175 of their float tolerances; the blue light kept to **1.9e-7** (3.4e-7) of the drive; the spot's growth (up to 0.52 px²) within 0.001 of its tolerance; red and green untouched by Bloom, bit for bit |
| `--alpha` | alpha = mix(α, 1, Mix) to **4.5e-8**; white under α = 0 changes **0** values; Mix 0 the clip bit for bit; Mix 0.5 the premultiplied mix to 1.5e-7; a colour under α = 0.25–1 drives the guns with α × its light to 3.9e-7. On two of Arena's own alpha clips, white painted under every transparent pixel changes **0** output bytes |
| `--resize` | 1280×720 → 320×180 mid-warm-up: the fringe, as a fraction of the width, off a run never resized by **1.4e-10**; the warm-up's state bit-equal |
| `--clock` | a host clock at 4.99e8 ms against one from 0, through a Power On: **0** of 14,745,600 values differ; seconds and milliseconds clocks settle on the right unit |
| `--glstate` | the host's GL state comes back, **0** things changed |
| `--robust` | a clip with −1, 2 and 10³⁰ in it renders exactly as its clamped copy; every control at either end gives finite output (24 renders) |
| `--pipe` | 2.5 frames in, exactly 2 out; bad cues and settings refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **23** deliberately wrong models, **23** caught |
| mutation | **46** one-character mutants of the shipped GLSL and C++, **46** caught |
| `tools/sweep.py` | all **13** controls measurably change the picture |
| shaders | all 4 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Convergence` / `CV01` / `effect` and renders through it |

Render cost, from verify.sh's final run on 16 November 2026 on a shared machine, over the
harness's card: at the defaults, GPU time **0.52 ms** a frame at 720p (worst
0.95), **1.13 ms** at 1080p (worst 1.19) and **4.67 ms** at 4K (one run; worst 5.07). The blur's
cost grows with the spot: with every Focus at 0 and Astigmatism at 1 (a 98 px kernel at
1080p) it is **2.48 ms** at 720p and **7.99 ms** at 1080p. What part of that each pass takes,
and the CPU's time a frame, were not measured. macOS only.

### Not established

- **No host has loaded it.** Not Arena, not Avenue, on any platform. `oxbow` (the fleet's test
  host) reads the bundle and renders through it, which is all.
- **Windows** has not been built yet: `release.yml` (GitHub Actions) is the first place it will be.
- **Chosen, not measured:** the throw (1.5 picture widths), red on the right and blue on the
  left, where the zone points sit and how the board interpolates between them, the setting
  errors being independent and normal, the warm-up's exponential shape, its time constant and
  its size, the focus blur's scale, and the calibrated greyscale. No source for any of them was
  reached.
- **Bloom's default** rests on one reading of a patent table's 320 µA, as a white raster's
  current; the clips ruled out the other reading, nothing confirmed this one.
- **The spot is the same all over the screen** (the dynamic focus taken as perfect), red and
  green never bloom, and the blue spot is a two-Gaussian mixture with the right variance, not
  one Gaussian.
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's DXV demo clips decode to; whether Resolume hands an effect premultiplied colour has
  not been checked.
- No scanlines (layer old-cathode for those), no user guide, no presets, no OpenFX port and no
  browser demo.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Convergence.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's under Effects as **SW Convergence**.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and again on the software renderer, the alpha check and
Bloom's default on Arena's demo clips (required), the pipe, the negative controls, the mutants,
the sweep for dead controls and the render cost. CI (`ci.yml`) builds on macOS and Windows and
runs the checks that need no GL; the pixel checks need a GPU, so this script is where they run.
`CLAUDE.md` lists the checks one by one; `./build/cvtest --all` runs them all.

Render a frame of your own:

```bash
./build/cvtest --out /tmp/convergence.png --set "Convergence=0.5" --set "Zones=9x9" --set "Keystone=0.6"
```

Run a clip through it:

```bash
ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/cvtest --pipe --fps 30 --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mov
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
