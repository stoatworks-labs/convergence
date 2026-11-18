# AGENTS.md: Convergence

Read this before changing the composite's sampling map (the zone field, the keystone, the
hand-done bilinear), the blue layers (prep's tent weights and `BuildKernels`), the zone table,
the clock or the warm-up. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-convergence.md`, written before any code and kept in step with
this repo (no As-built section: where the build departed from the first draft, the spec was
changed, and the changes are listed below).

## The idea

**Three pictures, made by three tubes through three lenses, never quite coincide.** Green looks
straight at the screen; red and blue sit either side and are bent onto green zone by zone by a
technician who never gets every zone exactly right. Zone-by-zone colour fringes, white text with
red and blue edges, drift as the set warms, a soft blue on bright scenes, and per-tube focus
and astigmatism all fall out of where each tube's raster lands and how big each tube's spot is;
none of them is drawn. Neighbours not duplicated: `old-cathode` (one direct-view tube, a
hand-shaped luma bloom for all three channels) and `regauss` (magnetic interference on a
shadow mask).

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Convergence`, `CV01` (checked free against every `"CV01"` in
  `~/Projects/*/*/source` and `~/dev/*/source`), an effect, `com.stoatworks.ffgl.convergence`,
  harness `cvtest`. One class, `ConvergencePlugin`; host indices are the ParamIds, the About
  block last.
- **Green is the frame.** Every service procedure converges red and blue onto green, so green is
  sampled at the pixel and only red and blue move. Red is the right-hand tube and blue the left
  (**chosen**; real sets differ).
- **The keystone is the side tube's plane-to-plane homography**, U = u/(1 + e u),
  V = v/(1 + e u), e = t tan θ, θ = 7.8° (US 4,672,458) and t = 1/3 (**chosen**: a throw of 1.5
  widths). `--model` checks it against a 3D ray-plane intersection. A converged set has no
  visible geometric residual (the spec computes a quarter of a pixel at 1920), so the keystone
  is only there as the Keystone control: the fraction of the homography left uncorrected.
- **The per-zone grid through FFGL's flat parameter list: a seeded installation.** A 5 × 5 grid
  is 100 values and 9 × 9 is 324. One parameter per value buries the dozen controls that matter
  and nobody can set 324 sliders by eye. A zone selector with four sliders (the projector's own
  remote) needs `SetParamVisibility` over hundreds of hidden parameters and an effect's value
  event redrawing Resolume's sliders, which cannot be checked without Arena, and automation
  would reach only the four visible sliders. So the grid is generated from **Setup** (an
  integer, which installation) and scaled by **Convergence** (how carefully it was converged),
  with **Zones** picking the density: every control automatable, a Setup number the same grid
  in every instance on every machine (PCG hash and Box–Muller in double on the CPU), and the
  harness reads the grid back to measure the picture against it. Lost: setting one chosen zone
  by hand (an open question below).
- **Zone points at the centres of an N × N partition, bilinear between, constant beyond the
  outermost** (**chosen**; no source gives a board's interpolation). Errors independent normal,
  RMS Convergence per component in fractions of the width, so an installation looks the same
  at every raster.
- **The warm-up is a static shift per side tube decaying as e^(−t/τ)** (Matsushita's CA
  1,294,702: heat drift is corrected by static convergence). τ 400 s by default so Barco's
  20-minute warm-up is 3τ (**chosen**); Cold Drift 0.1% of the width (**chosen**, under a forum
  anecdote). The state is one double, stepped s ← s·exp(−Δt/τ): products of exponentials
  compose, so the drift at a time is the same at any frame rate.
- **The spot is a separable Gaussian per tube, the same over the screen**: Hitachi's two
  measured 5% diameters (0.099 mm at 1 mA, 0.256 at 4) through a power law, p = 0.6853, on a
  5.5-inch raster (Sony RVP-411DS) taken as the frame's height. Focus and astigmatism by Sturm's
  interval, the blur S = 1% of the height per unit of defocus (**chosen**) in quadrature with the
  beam. Red and green stay at their 1 mA spot.
- **The blue phosphor saturates, L ∝ I/(1 + I/I_s), I_s = 4/3 mA** (fitted to Philips' one
  pair, 0.5 → 2 mA gives 2.2×; Kasei's γ 0.61 at 0.85 mA is a cross-check). **The greyscale is
  calibrated** (**chosen**): the drive is pre-distorted so blue light is linear in the signal,
  and saturation costs current, I(d) = d I_s I_w / (I_s + I_w (1 − d)).
- **Bloom is per pixel and exact in its second moment.** The blue light is split between four
  layers at variances k V_w/3, each pixel's light shared between the two either side of its own
  σ(I(d))² in proportion to the distance in variance, each layer blurred by its own Gaussian. A
  mixture whose weights are linear in variance has exactly the interpolated variance; its shape
  is a two-Gaussian mixture, not one Gaussian (stated, never claimed otherwise).
- **Output alpha = mix(α, 1, Mix), and Mix mixes premultiplied.** The projected picture fills
  the screen and its fringes put light where the clip was transparent. The clip's light drives
  the guns weighted by α (straight alpha over black, which Arena's DXV clips decode to); the
  Mix colour is (clip α (1 − m) + projected m)/alpha_out, so nothing under a transparent pixel
  shows at any Mix. Checked on two of Arena's alpha clips with white painted under every
  transparent pixel.
- **The prep pass clamps before it weights by alpha**, so an out-of-range clip (a float
  texture with −1, 2 or 10³⁰) renders exactly as its clamped copy.
- **The clock** is intensifier's: the host's time in double, a vote on seconds or
  milliseconds, Δt clamped to [0, 0.1] s and rounded to the microsecond, so a clock at
  4.99e8 ms and one from 0 give the same picture to the bit. A paused host holds the drift.
- **A resize loses nothing**: nothing on the GPU carries between frames; the warm-up is a CPU
  double and the drift is a fraction of the width.
- **Bloom's default is 0.32 mA**, US 4,845,403's Table 1 current for white, read as a white
  raster's current, because the demo clips ruled the other reading out (below).
- **No OpenFX port and no presets.** (At the build there was also no browser demo, no user
  guide, no Windows build and no workflows; the release added them once the repo went public
  as `stoatworks-labs/convergence`, where Actions are allowed: `ci.yml` and `release.yml`
  are intensifier's (xenon's), `tools/wincheck.ps1` penumbra's.)

## Changes from the spec's first draft

Each was made in the spec as well.

- **Bloom's default.** The draft read Table 1's 320 µA as the average a projector draws on a
  picture and planned to bisect per clip for the I_w that gives it. Measured through
  `--pipe --stats`: at I_w = 10 mA, the top of the range (the mean only grows with I_w), the 33
  demo clips draw 0.00 to 210 µA, median 13.9. None reaches 320 µA; the clips are dark in blue
  (median mean linear drive 0.0055). Table 1 is now read as its own words say, a white raster's
  current, I_w itself: 0.32 mA. `tools/clips.py defaults` re-measures this.
- **The prep pass clamps before the alpha weighting** (the draft had it after), so a wild value
  under a fractional alpha cannot leak through; the first order would have broken
  `--robust`'s claim that a wild clip renders as its clamped copy.
- **Texture A carries the blue drive d in A.a** beside the current, so `--bloom` can compare the
  decoded drive with the harness's own.
- **Mix mixes premultiplied** (the draft said `mix` with the clip). A plain mix would show what
  sits under a transparent pixel at Mix 0.5; Arena's clips are black there, so only the
  white-paint check sees the difference.
- **The geometry cards.** The draft's crosshatch put crossing lines inside measuring windows.
  `--zones` and `--keystone` use vertical-only and horizontal-only cards, alternate lines on
  separate cards; `--shift` and `--warmup` measure a hatch only between its crossings.
- **`--zones` measures the two rows (columns) straddling each point** against the harness's own
  field at that row, instead of fitting centroids either side of a crossing. Its tolerance
  gained the resampler's bias term, |g|(1 + MAD/(1 − |g|))/(1 − |g|) where the field has slope
  g (derived; a Python simulation's worst case reached 0.45 of it), and the kink term
  |Δg|·MAD/(1 − |g|)². At 9 × 9 on 320 × 180 measurements whose window runs off the frame are
  skipped and counted (45 and 32).
- **The discrete variance's tolerance** is 10⁻⁷ (σ² + 1), not 10⁻⁸: the 6σ cut alone leaves
  2.5e-5 at σ = 20 (`--model` measures 0.629 of the new bound).
- **`--shift`, `--bloom` and `--alpha` gained checks** after the first mutation run (below).
- **`--keystone`'s angle mutant runs at 1280 × 720**: at 320 × 180 a 1.3% error in e (7.9° for
  7.8°) moves the rightmost red line 0.05 px, inside the resampler's bias term there, and
  passes; at 1280 × 720 it moves 0.27 px and fails (worst ratio 1.6).

## The traps actually hit

- **`half` is a GLSL reserved word**: the keystone's centre is `centre`. verify.sh's grep has it.
- **Equivalent mutants.** Three of the first 45 could not be caught by any check of what is
  claimed: the blur's sum negated (the horizontal and vertical passes' signs cancel), the layer
  step V_w/2 (the mixture's variance is exact for any step ≥ V_w/3; only its shape moves), and a
  geometric range high × low run against Warm-up (whose low end is 1 s, so high × low = high /
  low). Replaced: the blur's sum starting at 1, the step V_w/5 (the full-white spot past the
  last layer), and the range mutant run against Bloom (low 0.1 mA).
- **Six real holes the mutants found**: red fed the green signal passed `--shift` (its cards
  were grey, so red and green were the same signal: now colour noise, and the unshifted red
  must equal the r/g-swapped card's green); past the frame's edge white passed (now every pixel
  a shift uncovers must be black); the sRGB decode's exponent 2.5 passed `--bloom` (it compared
  everything with the drive the plugin decoded: now that drive is checked against the
  harness's); the clip's light divided by alpha and the clip's share at Mix m as 2 − m passed
  `--alpha` (now a flat colour under four alphas, and Mix 0.5's colour against the
  premultiplied mix); the side tubes at 7.9° passed `--keystone` at 320 × 180 (above).
- **The Mix check's first version compared renders at different instants**: Mix 1 at frame 2
  and Mix 0.5 at frame 4 of one instance, with the cold drift moving between them (0.0013 off).
  Two fresh instances rendered to the same instant agree to 1.5e-7.
- **The bilinear resampler's centroid bias** under a stretch is not float-sized: where the
  zone field or the keystone has slope g, a line's centroid moves by up to about |g| times its
  spread. The tolerance carries a derived bound for it; a Python simulation's worst case
  reached 0.45 of the bound. It is also why `--keystone` at 320 × 180 cannot resolve e better
  than a couple of per cent.
- **The discrete variance in double cancelled in the tails**: `hDiscreteVar` uses erfc from the
  tail that does not cancel.
- **A forward difference on the homography** was too coarse for a 1e-9 check: central
  differences.
- **Defocus is symmetric about best focus**: Focus 0 and 1 blur the same, so a sweep of either
  end with no astigmatism is a dead control by design. The sweep sets Astigmatism.
- **Bloom at its honest scale barely moves the card** (a mean 0.023 of a code between its ends
  at 1280 × 720): the sweep runs it on a one-pixel white grating instead (0.85 at 320 × 180).
- **ffmpeg reads the terminal** unless `-nostdin`; **zsh has no PIPESTATUS** (the pipe checks
  run under bash); **macOS has no timeout** (perl's alarm); **a heredoc hangs here unless
  TMPDIR is set** (mutate.sh has one); **make's mtimes are 1 s** (mutate.sh deletes the objects
  and the binary, and checks a GLSL mutant is in the binary).
- **The software renderer's `--all` takes about two minutes** at 320 × 180; verify.sh runs it
  check by check.

## Verified, and assumed

Verified, by `OXBOW=... tools/verify.sh` on 16 November 2026 (numbers in README's Status):
every check at 1280 × 720 and 320 × 180 on this Mac's GPU and again on Apple's software
renderer at 320 × 180; the alpha decision and Bloom's default on Arena's clips; the pipe; the
negative controls and mutants; the sweep; the bundle universal, signed ad hoc, read and
rendered by oxbow.

Every claim, its number at 1280 × 720 (320 × 180) and its tolerance's source:

- **Whole-pixel shifts** (`--shift`): 0 of 908,208 (54,288) pixels differ, 0 of 13,414 (3,334)
  uncovered pixels lit, the red tube's signal its own: bit-exact, because with every error zero
  the sampling position is P exactly and a whole-pixel shift lands on texel centres.
- **Fractional shifts** (`--shift`): worst line off by 5.37e-5 (7.29e-6) px against 9.81e-4
  (2.49e-4): 8 float ulps of the larger frame dimension (the sampling position's rounding) plus
  4e-6 of the line's MAD (the window's sums).
- **Zone fringes equal the set errors** (`--zones`, fringes up to 22.4 px): worst ratio 0.381 to
  0.475 (0.440 to 0.514) of float + the resampler's bias + the kink term (derived; above).
- **The keystone's trapezoid** (`--keystone`): worst ratio 0.333 / 0.355 at Keystone 1 / 0.5
  (0.758 / 0.696) of float + the bias term + 2|x″|Var (the centroid's second-order bias under a
  smooth map); the rightmost line's red −19.559 px and blue +21.105 at 1280 (opposite sides).
- **Warm-up drift against its time constant** (`--warmup`, τ = 2 s, Cold Drift 0.5%): every
  half second from 0 to 4 s, worst ratio 0.167 (0.165) of the shift check's float bound plus
  the microsecond rounding of every frame's Δt (0.5 µs each); τ fitted 2.000020, 1.999957,
  1.999839 s at 30, 60 and 240 fps against ±1.8e-3 (each point's error bound through the least
  squares slope's weights); the three rates agree to 2.21e-4 (5.54e-5) px against 3.70e-3
  (9.34e-4); Power On at 3 s ratio 0.049.
- **Focus and astigmatism** (`--focus`, 6 settings, 108 dots): worst ratio 0.005 (0.003) of 1e-4
  of the variance, at least 1e-7 px² (float sums of a dot's light; the floor where the spot is
  a fraction of a pixel).
- **Bloom** (`--bloom`, 231 (120) dots at 0.5, 2 and 8 mA): the decoded drive 0.103 of 16 float
  ulps × 2.4 (the decode's condition number); the current 0.175 of 8 ulps × the formula's
  condition number; the blue light's sum / d 0.019 (0.034) of 1e-5; the variance's excess
  (up to 0.5217 (0.0326) px²) 0.001 of 1e-4 of the variance + 1e-7 (it + 1) (the discrete
  variance's measured departure from σ² + 1/12); red and green bit-exact across Bloom.
- **Alpha** (`--alpha`): alpha off mix(α, 1, Mix) by 0 at Mix 1 and 4.5e-8 at Mix 0.5 (1e-6, a
  few float roundings); white under α = 0 changes 0 values; Mix 0 passes the clip bit for bit;
  Mix 0.5's colour off the premultiplied mix by 1.5e-7 (1.4e-7) (1e-6); a flat colour under
  α = 0.25 … 1 off α × its light by 3.87e-7 of it (1e-5: decode, blur and encode in float, a few
  ulps each × 2.4). On Arena's clips (`tools/clips.py alpha`): alpha 255 at Mix 1, within half a
  code of a/2 + 127.5 at Mix 0.5, 0 bytes changed by white under every transparent pixel, and
  the negative control (white on ~200 opaque pixels) shows on both clips.
- **Resize mid-warm-up** (`--resize`, 1280 × 720 → 320 × 180): the fringe as a fraction of the
  width off a run never resized by 1.42e-10 against 1.54e-6 (twice the shift check's float
  bound, as a fraction of the width); the drift's state bit-equal.
- **The clock** (`--clock`): 0 of 14,745,600 (921,600) values differ between a clock at
  4.99e8 ms and one from 0 through a Power On; seconds and milliseconds settle. Exact.
- **The host's GL state** (`--glstate`): 0 things not put back. Exact.
- **Robustness** (`--robust`): the wild clip renders as its clamped copy, 0 values differ; 24
  renders with one control at an end, 0 with a non-finite value. Exact.
- **The constants** (`--model`, no GL): Hitachi's 0.099 and 0.256 mm back from the spot law;
  Philips' 2.2 from I_s; Kasei's 0.61 to its two digits; the homography within 3.6e-12 of a 3D
  ray-plane intersection (1e-9, the central differences); the zone table's mean, variance and
  red-blue correlation over 2000 Setups within 5 standard errors; the controls' conversions
  exact.

Assumed or chosen, not measured: the throw (t = 1/3); red on the right; the zone points'
positions and bilinear interpolation; the errors normal and independent; the warm-up's
exponential shape, τ and Cold Drift; the focus blur's scale S; the calibrated greyscale; a
5.5-inch raster filling the frame's height; red and green spots held at 1 mA; Table 1's 320 µA
as a white raster's current.

Not done: loading it into Resolume on any platform; a Windows build; a user guide, presets, an
OpenFX port, a browser demo; looking at the demo clips in a show rather than as numbers.

## Would this hold on another rasteriser, at another raster?

One line per check. Apple's software renderer at 320 × 180 passes every one of them.

- `--model`: no GL; double.
- `--shift`: whole pixels are texelFetch and weights of exactly 1 and 0, so bit-exact on any
  driver that does float multiply-add by IEEE rules; the fractional tolerance is in ulps of the
  raster's own dimension.
- `--zones`, `--keystone`: the float term is in ulps of the raster; the bias and kink terms are
  in the line's own MAD and the field's slope, so they hold at any raster. At 320 × 180 the
  outermost 9 × 9 lines' windows run off the frame and are skipped, and the keystone's bias term
  hides a 1.3% error in e that 1280 × 720 catches.
- `--warmup`, `--resize`: the drift is computed in double on the CPU; only its sampling is float.
- `--focus`, `--bloom`: kernels in double on the CPU; the sums are float over at most a few
  hundred taps, inside 1e-4.
- `--alpha`: 1e-6 is a few float roundings; Mix 0 is a texelFetch and a copy.
- `--clock`: compares the plugin with itself under two clocks in one context, so it holds
  wherever one driver is deterministic run to run.
- `--glstate`: whatever the driver reports.
- `--robust`: relies on the clamp coming before anything multiplies, on the sRGB decode's pow
  of 10³⁰ overflowing to +inf and on clamp taking +inf to 1. Both renderers here do; GLSL does
  not promise the overflow.

## The negative controls and mutants

`cvtest --negative`, each wrong model required to fail (checks failed of those run), all 23
caught:

| check | wrong model | failed |
| --- | --- | --- |
| model | Zenith's square-root spot law for Hitachi's power law | 1 |
| model | defocus added to the spot, not in quadrature | 1 |
| zones | the nearest zone's error, not interpolated | 8 |
| zones | the grid transposed | 8 |
| zones | blue reading red's grid | 8 |
| keystone | both side tubes on the same side | 4 |
| keystone | an affine keystone (the e²u³ term dropped) | 4 |
| warmup | the decay stepped per frame (60 fps factors at every rate) | 4 |
| warmup | Power On ignored | 2 |
| warmup | the drift growing from zero | 4 |
| focus | one focus for all three tubes | 2 |
| focus | astigmatism's line foci on the same side | 2 |
| focus | defocus added to the spot, not in quadrature | 2 |
| bloom | no saturation (the current linear in the drive) | 2 |
| bloom | Zenith's square-root spot law | 2 |
| bloom | layer weights linear in σ, not σ² | 2 |
| bloom | the bloom on red too | 2 |
| alpha | the clip's alpha passed through | 2 |
| alpha | the clip's light not weighted by its alpha | 2 |
| resize | a new raster restarts the warm-up | 1 |
| clock | the host's clock read in single precision | 4 |
| glstate | the host's GL state not put back | 2 |
| robust | the prep pass's clamp removed | 2 |

`tools/mutate.sh`, 46 one-character mutants, each built arm64 and run at 320 × 180 (the keystone
angle's at 1280 × 720) under a 600 s alarm, all caught. GLSL: the sRGB decode's exponent, the
calibrated current's denominator, the layer clamp at 2, the fourth tent at 4, the fourth layer
subtracted (`--bloom`); the sRGB encode's offset, the blur's loop from +R, its sum from 1
(`--focus`); the clip's light divided by α, the picture transparent, the clip's share 2 − m,
the clip handed through to Mix 0.5 (`--alpha`); the clamp never on (`--robust`); red fed green,
the resampler's centres and left weight, past the edge white, the pixel's centre, green showing
the current (`--shift`); the zone points off their centres, the last cell past the grid, the
upper corners two rows up, blue's y from its x (`--zones`); the keystone's centre, the
homography's denominator, blue keystoned to red's side (`--keystone`); red's drift reversed,
blue's drift y from x (`--warmup`). C++: the side tubes at 7.9° (`--keystone` at 1280 × 720);
I_s 2 mA, the layer step V_w/5, the shader's I_s I_w as I_s / I_w, a geometric range high × low
(`--bloom`); the horizontal line focus on the wrong side, spot and defocus multiplied, the
kernel's centre pixel 1.2 wide, red and green's vertical pass on the horizontal kernels
(`--focus`); Box–Muller's radius (`--model`); Convergence's top 1.6%, the zone errors divided by
the RMS (`--zones`); the drift growing, the frame a tenth as long, the drift's y along x, Power
On never pressed (`--warmup`); the clock-unit fallback (`--clock`); the host's viewport put back
square (`--glstate`).

## The sweep's honest list

`python3 tools/sweep.py`: all 13 controls change the picture, each in a context where it acts
(mean 8-bit difference per channel at 320 × 180): Convergence 13.5, Zones 10.3 and Setup 12.1
(Convergence 0.5), Keystone 8.7, Warm-up 9.4 (Cold Drift 1), Cold Drift 13.2, Power On 3.7
(pressed half a second into a 1 s warm-up, Cold Drift 1), Red / Green / Blue Focus 1.9 / 1.8 /
1.2 (Astigmatism 0.5), Astigmatism 8.9, Bloom 0.85 (a one-pixel white grating), Mix 2.7.

`--bare` (the defaults, no context): **dead** — Red, Green and Blue Focus, whose ends are the
same picture with no astigmatism (defocus either side of best focus blurs alike, by design);
**barely alive** — Power On (0.0018: at the default 400 s warm-up, half a second of decay is
nothing to restart) and Bloom (under 0.0001 on the card at 320 × 180: the blue spot at 10 mA is
0.2 px there). Zones, Setup and Warm-up stay alive bare, through the default 0.1% convergence
error and cold drift.

## The browser demo (2026-11-18)

`demo/` is <https://convergence-demo.stoatworks-labs.com>, built to the fleet's
`resolume-demo` kit rules (`~/Projects/resolume/specs/DEMO-BRIEF.md`), from intensifier's. What
a reader of it must know:

- **The shaders are the plugin's**: kVersion and all five raw-string pieces of
  `source/Shaders.cpp` (kQuadVertex, kCommon and the three passes), spliced by
  `demo/tools/check_shaders.py --write` into `demo/shaders.js` and compared character for
  character, with the assembly on both sides (AssembleVertex, Assemble, InitGL's stages,
  plugin.js's copy) and every uniform any pass declares named in plugin.js. The integers go
  through `setInt` (Output, Premultiply, ClampOn, the five models, Radius, Row, ZoneCount),
  `setSampler`, or `gl.uniform2i` (Size, Direction, ViewOrigin); a `.set()` on one fails the
  check. The vertex stage is the plugin's gl_VertexID triangle, drawn from an empty VAO, not
  the kit's quad. `tools/verify.sh` runs the check.
- **The CPU half is a port**, in double, between the PORT and FRAME markers of
  `demo/plugin.js`: Controls.cpp (the table, geometric and its inverse, OptionIndex and
  SetupFromParam with `std::round`/`std::lround`'s half away from zero), Projector.cpp
  (KeystoneE, the spot law, the PCG hash in uint32 arithmetic, ZoneTable's Box-Muller,
  DriftDirection, Compute with the shipped OpticsHooks folded in, PixelGaussian, BuildKernels
  with its texels rounded to float), and the CPU half of Convergence.cpp: Power On's edge,
  UpdateClock's vote and patience, the frame length (clamped to 0.1 s, rounded to the
  microsecond), the warm-up, the drift, the kernel and zone caches and uploads, ensureBuffers,
  render()'s six draws over three passes and every uniform in order. `erf` and `erfc` are the
  page's own (JavaScript has neither): a positive-term series and a continued fraction, no
  third-party code. Not ported: the log, the About block, ScopedGLState, and the harness's
  hooks (all off as shipped). The check holds the port to the C++ lines it came from
  (ORCHESTRATION in check_shaders.py): change Controls, Projector or Convergence.cpp and change
  the port, then the anchor.
- **Two controls the kit cannot draw.** Setup (FF_TYPE_INTEGER) is a dropdown of all 1000
  values, slot s sending s + 1; Power On (FF_TYPE_EVENT) is a toggle the renderer releases
  after the frame it fires on, so the plugin's code sees a press then a release, one edge.
  `?power=1` in the URL presses it on load.
- **The composite's viewport is the input's size**, from the origin. A visitor's picture keeps
  its own size, so changing Composition after loading one leaves it in the corner.
- **Premultiplied clips.** The kit's clips and a visitor's picture are premultiplied; the
  plugin weights light by alpha as for straight alpha (`Premultiply` 1), so a soft edge is
  weighted twice. Only the "Shape on transparency" clip shows it. And the plugin's output is
  straight below Mix 1 where the browser composites the canvas as premultiplied, so a partly
  transparent pixel shows brighter there than in a straight-alpha host. Both on the page.
- **A frame is the display's refresh**, and the clock is the page's, in seconds (the vote
  settles on seconds within four frames). The warm-up steps by each frame's own length, so it
  is the same at any refresh rate; paused, a control change draws a frame with no time passed,
  so the warm-up holds; Restart is the clock going back, which the plugin takes as no time.
  A background tab stops the kit's clock, so the set does not warm while nobody watches.
- **Bloom is drawn at its real size**: sigma 0.06 px at 960 × 540 at the default, 0.63 px at
  10 mA. The page says where to look (fine bright blue detail, 1920 × 1080, zoomed in) and
  does not exaggerate it.
- **The page's own `differences` list says all of this on the page.** Keep it true. The line
  under the picture (the zone error in pixels, the cold drift left, the keystone's spread at
  the edges, the blue spot, the kernels' width, the frame's length) is the page's, and says
  so. The presets are the page's (the plugin declares none; one presses Power On).

How it was verified, and how to again (it needs this Mac's GPU and Chrome, so verify.sh does
not run it):

- `python3 demo/tools/crosscheck/crosscheck.py --cvtest build/cvtest [--size 960x540]`. The
  table of controls first, against `cvtest --list` (names, kinds including integer and event,
  defaults; Zones' elements by name through `--set`). Then 19 runs of 12 generated frames
  (straight alpha: a graded ground, a white crosshatch of one- and two-pixel lines, red, green
  and magenta bars, bright blue patches and one-pixel blue stripes, fine vertical and
  horizontal stripes, a moving disc, a card whose alpha ramps): the defaults, Convergence 1%,
  9x9 zones, Setup 42 and 1000, Keystone 1 and 0.5, the fastest warm-up from the most cold
  drift at 60 and 30 fps, Power On pressed on frame 6 and released on 7, Red Focus 0, Blue
  Focus 1, Astigmatism 1 with every Focus 0.25, Astigmatism 0.5 with Green Focus 0.75, Bloom
  10 and 0.1 mA, Mix 0.5 and 0, and every control at an extreme with Power On. Each through
  `cvtest --pipe --stats [--script]` and through the page's `renderRun` (a fresh instance on
  the harness's seconds clock, the same cues on the same frames, the frames uploaded as cvtest
  uploads them, the composite read back from an RGBA32F target and quantised as cvtest
  quantises). 18 November 2026, M4 Max, Chrome 157.0.8103.79 through ANGLE on Metal, against
  cvtest on GL 4.1 (an arm64 build of f526c10): at 960 × 540, **219 bytes of 472.8 million
  differ, each by one level** (124 in Keystone 1, 68 in Keystone 0.5, 22 in the extremes, 5 in
  Mix 0.5; the other 15 runs identical to the byte); at 480 × 270, 32 of 118.2 million. The
  warm-up's state equals `--stats`'s nine digits on every frame (worst 5.6e-10 relative, the
  printing), and so does the mean blue-gun current read back from texture A. Every uniform of
  all three programs set, none missing, no GL error, no frame refused.
- The CPU half without a GPU: `cvdump.cpp`, built from Projector.cpp and Controls.cpp, against
  the page's port at 14 rasters and settings (1×1 to 5760×3240, Setup's rounding at 0.4999,
  500.5, 1000.6 and -5, Zones at 0.5, Mix 1.5): every float the GPU receives (the 13 params,
  every kernel texel, every zone texel, the drift, the nine float uniforms; 34,440 in all)
  identical, every int identical, every double within 2 ulps (the zone table's Box-Muller: the
  browser's log and cos against the C library's). Built twice, with `-ffp-contract=off` (the
  tolerance: 8 ulps) and with the plugin's own Release flags (clang fuses multiply-adds on
  arm64): the same figures both ways. The page's erf and erfc against the C library's at 605
  points over 0..6: worst 7.8e-16 and 5.4e-15 relative (tolerance 1e-14). PixelGaussian at 14
  sigmas from 0 to 100: worst 4.1e-13 relative (tolerance 1e-11), at a far tap of the widest
  kernel, which is the difference of two nearly equal erfc values.
- Negative controls, each caught (pass: 99.99% of bytes identical over the run, no byte more
  than one level apart, no frame's mean 0.01 levels apart, the warm-up within 1e-8 relative),
  at 960 × 540: Convergence one slider step (0.001) off 91.4% of bytes identical; Keystone
  one step off 94.1%; Warm-up one step off 95.2% (and the warm-up 1.4e-3 apart); Cold Drift
  one step off 93.2%; Setup one off 59.7%; Red Focus one step off 99.29%; Blue Focus one step
  off (at Blue Focus 1) 99.32%; Astigmatism one step off 97.0%; Bloom one step off (at 10 mA)
  98.7%; Mix one step off 97.0%; a press of Power On the page never sees 93.8%.
- Flash, by xenon's rule (`demo/tools/crosscheck/flash.py`, flashcheck.py's transitions: an
  opposing change of 0.1 of white whose darker end is below 0.8; more than 6 in any second
  fails; the whole frame and 5 × 5 windows a third of the frame each way), on the page's own
  instance and clips read back at 960 × 540, 60 fps, 5 s a case, over the backdrop as the
  canvas composites: every clip at the defaults; every control at an extreme at once from a
  cold start; Power On pressed 2, 6 and 10 times a second at the most Cold Drift and the
  shortest Warm-up; every control flipped between its extremes 10 times a second, on the bars
  and on the transparent clip over white. **At most one transition in any second in any
  window**, none over the whole frame: the projector moves edges by pixels, and no move
  changes an area's light. The one exception is Mix over a transparent clip: at 0 the backdrop
  shows through, at 1 the projected picture fills it, black where no gun is driven. Over
  White, flipping Mix 0↔1 at 1, 2, 3, 4 and 6 times a second gives 2, 4, 7, 8 and 13
  transitions a second (a window); over the kit's checkerboard, at most 1. No gate: nothing
  flashes without the visitor's hand, and it is the plugin's documented Mix. The page carries
  a photosensitivity note naming that move (and Mix's hint says so); flash.py fails if the
  note stops being true either way. No held setting measured (the defaults, every extreme)
  crosses the line.

The thumb (`stoatworks-labs.com/thumbs/convergence.png`) and the project page did not exist
when the demo went live, so `index.html` has no `project-page` meta and its no-script block
links only the repository.

## Open questions

- **Bloom is honest and nearly invisible.** At its real scale the blue spot at 10 mA is 1.26 px
  at 1080p, and the default 0.32 mA is a tenth of a pixel. The IDEAS bullet's "soft blue channel
  on bright scenes" shows on fine bright detail and little else. Keep the physics, or add a
  "spot scale" beyond it?
- **Bloom's default.** 0.32 mA reads Table 1 as a white raster's current; the demo clips ruled
  out the average reading. A projectionist's real full-white current is likely higher (Philips
  talks of 0.5–2 mA); a higher default would show more bloom on bright clips.
- **Setting one zone by hand.** The seeded installation makes every control automatable but
  cannot set a chosen zone. A zone selector with four sliders would, if Resolume redraws a
  slider on an effect's value event: needs Arena to test.
- **Focus symmetric about best focus** means half of each Focus slider repeats the other half
  with no astigmatism. Faithful; a VJ may expect 0 to 1 to mean sharp to soft.
- **Red on the right, blue on the left** is a choice; real sets differ. A mirror option?
- **Straight or premultiplied alpha in Resolume's pipeline?** As intensifier: the plugin weights
  the clip's light by α, right for straight alpha, which Arena's DXV clips decode to through
  ffmpeg. Needs a look in Arena.
- **Scanlines** are left out (old-cathode has them); layering the two is the route if wanted.
