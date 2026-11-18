# convergence

A three-tube CRT projector throwing the clip onto the screen, for Resolume Arena/Avenue as
one FFGL effect. Green looks straight at the screen; red and blue sit either side at 7.8° and
are converged onto green zone by zone, with a technician's error at every zone point; the side
tubes drift as the set warms; each tube has its own focus and astigmatism; the blue gun's spot
grows with its current, and a calibrated tube draws disproportionate current for bright blue.
C++/GLSL, CMake MODULE → universal `.bundle` (macOS). MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `CV01` | `SW Convergence` | `Convergence.bundle`, `com.stoatworks.ffgl.convergence` | effect |

Read `AGENTS.md` before touching the composite's sampling map (`Shaders.cpp`: zone field,
keystone, hand-done bilinear), the blue layers (prep's tent weights and `BuildKernels`), the
zone table (`Projector.cpp`, `ZoneTable`), the clock or the warm-up. The spec is
`~/Projects/resolume/specs/SPEC-convergence.md` and is kept in step with this repo.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DCONVERGENCE_BUILD_TOOLS=ON`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/cvtest --out /tmp/c.png` (the defaults over the harness's card,
  120 frames in at 60 fps)
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/cvtest --list`
- Set anything by name: `./build/cvtest --set "Convergence=0.5" --set "Zones=9x9" --set "Setup=42"`
  (a standard control takes the host's 0..1; an option takes its name; Setup takes 1..1000;
  anything unparseable is refused with exit 2)
- Change something later: `./build/cvtest --out /tmp/c.png --at 60 "Power On=1"`
- Film: `./build/cvtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/cvtest --pipe --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
- `--stats` prints each frame's mean blue-gun current (mA) and the cold drift left on stderr.
  Nothing is random: there is no `--seed`.
- Bloom's default, re-measured: `python3 tools/clips.py defaults` (all 33 of Arena's demo clips;
  about a minute). The alpha check on two of them: `python3 tools/clips.py alpha`.

## Verify
- Everything: `TMPDIR=<a writable dir> OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved
  words, `glslc` on all 4 shaders, the submodule pin, a fresh universal build with no warnings
  outside external/ffgl, lipo, plist, ad-hoc signature, `oxbow probe` and `selftest` (required,
  by name: there is no default path, and no oxbow is a failure); every check at 1280x720 and
  320x180; every check again on Apple's software renderer at 320x180; `--offline`; the alpha
  check and Bloom's default on Arena's clips; `--pipe`; the negative controls; the mutants; the
  sweep; the bench). `KEEP_BUILD=1` reuses `build-verify/`. It needs Resolume Arena's demo
  clips in `/Applications/Resolume Arena/media`.
- Every check at once: `./build/cvtest --all`. One at a time (`--size WxH`: that raster alone):
  - **No GL**: `--model` (the constants back to their sources, the homography against a 3D
    ray-plane intersection, the zone table's statistics over 2000 Setups, the kernels, the
    conversions).
  - **Geometry**: `--shift` (whole-pixel shifts bit-exact; fractional to float), `--zones` (every
    zone point and cell centre against the set errors), `--keystone` (the trapezoid against the
    harness's own inverse homography).
  - **Time**: `--warmup` (τ fitted at 30, 60 and 240 fps; Power On), `--resize` (mid-warm-up),
    `--clock` (4.99e8 ms against 0, bit for bit; the unit settles).
  - **Spot**: `--focus` (each tube's x and y variance; Sturm's interval), `--bloom` (the
    decoded drive, the current law, the light kept, the variance's excess = σ(I(d))²).
  - **Host**: `--alpha`, `--glstate`, `--robust` (a wild clip renders as its clamped copy;
    every control at its ends is finite).
- The checks can fail: `./build/cvtest --negative` (23 wrong models), `tools/mutate.sh`
  (46 mutants, arm64, 320x180 unless a mutant names its raster, perl alarm at 600 s; `ONLY=n`
  runs one; set TMPDIR first).
- No dead controls: `python3 tools/sweep.py` (each control in a context where it acts;
  `--bare` drops the context).
- Render cost: `./build/cvtest --bench` (720p, 1080p, one 4K run).
- `CVTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **Three passes, six draws, one full-raster triangle** (`Shaders.h`, four shaders for
  `glslc`): prep (drawn twice: texture A red, green, the blue current, the blue drive; texture
  B the blue light in four variance layers), blur (separable, four draws, each channel by its
  own kernel from a float texture), composite (green at the pixel; red and blue through the
  zone field, the drift and the keystone by hand-done bilinear; sRGB; the premultiplied Mix).
  Nothing is blended.
- **No time and no randomness reach a shader.** The CPU keeps the clock in double, rounds Δt
  to the microsecond, steps the warm-up as s ← s·exp(−Δt/τ) and sends the drift as pixels.
  The zone grid is a PCG hash and Box–Muller of (Setup, n, point, tube, axis) in double.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index;
  Setup is an `FF_TYPE_INTEGER` with its real range.
- GLSL 4.10 core. Reserved words (`common partition active … packed sample patch layout flat`,
  `noise1`–`noise4`, `any far near fixed half`) must not be identifiers (`half` bit this repo:
  the keystone's centre is `centre`), and nothing writes `M_PI`; `verify.sh` greps for both.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `convergence_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Generated, never edit here:** `source/StoatworksAbout.h`, `ATTRIBUTIONS.md` (and the
  README's attributions block), the issue forms and `.github/FUNDING.yml` come from
  stoatworks-backend's sync scripts (registered 2026-11-18). Edit them there and re-sync.
  sync-about added the User guide button, so the plugin declares 18 parameters (the 13
  controls, the About text and four link buttons). `StoatworksAboutLinks.h` and
  `StoatworksAboutParams.h` are the FFGL renderer, this repo's own.
- **Workflows** (the repo is public, `stoatworks-labs/convergence`, so Actions are allowed):
  `ci.yml` builds on macOS (arm64, `cvtest --list` and `--offline`, `glslc`) and Windows on
  every push and pull request; `release.yml` builds the universal bundle and the x64 DLL
  (checked by `tools/wincheck.ps1`), with a .dmg, zips and an NSIS installer, and publishes on
  a `v*` tag. A `workflow_dispatch` of `release.yml` builds without publishing. The pixel
  checks still run only in `tools/verify.sh`, on a Mac.
- `~/Projects/resolume/convergence`, public at `github.com/stoatworks-labs/convergence`.

## Browser demo
- `demo/` is <https://convergence-demo.stoatworks-labs.com>; `.github/workflows/deploy.yml`
  deploys it on a push to main (a route plus a proxied AAAA 100:: record, not a custom
  domain: `wrangler.toml`).
- After a shader edit: `python3 demo/tools/check_shaders.py --write` (verify.sh fails on
  drift). It also fails if a C++ line the page's CPU half was ported from changes (the
  control table and conversions, Projector's constants, hash, zone table, Compute and
  kernels, the clock, the warm-up, the buffers, the uniforms): change the port, then the
  anchor.
- After a change to Controls, Projector or Convergence.cpp's CPU half: change the port in
  `demo/plugin.js` (between the PORT and FRAME markers) too.
- Locally: `python3 -m http.server -d demo`, or `npx wrangler dev` for the real `_headers`.
- Against the plugin: `python3 demo/tools/crosscheck/crosscheck.py --cvtest build/cvtest`
  (Chrome and a GPU; builds its own `cvdump` from Projector.cpp and Controls.cpp). Flashes:
  `python3 demo/tools/crosscheck/flash.py`, which also fails if the page's photosensitivity
  note stops being true. AGENTS.md has the gaps and the last figures.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/convergence/convergence.YYYY-MM-DD.log
    %LOCALAPPDATA%\convergence\logs\convergence.YYYY-MM-DD.log

`CONVERGENCE_LOG_DIR` overrides the folder.
