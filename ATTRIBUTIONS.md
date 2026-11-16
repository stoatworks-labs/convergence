# Attributions

Convergence is built on other people's work. This file lists what that work is, who did it,
and what it is doing here.

**A provisional hand copy (2026-11-16).** Convergence is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness and the tooling — Stoatworks intensifier, and ferrofluid before it

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, `--set` by display name, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout an exit of 1), `--negative` with its Perturb, and `--offline` (tools/cvtest is intensifier's ittest, which is filings' fgtest, fountain's fttest and ferrofluid's fetest, under new checks); tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh, tools/sweep.py and tools/clips.py; the clock's vote on seconds or milliseconds (ferrofluid's, by way of rosette and millpond) with fountain's offline fallback (source/Convergence.cpp). All adapted. intensifier lives at ~/dev/intensifier and is not published yet.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of intensifier. source/GLState.h is radar's, which is vectrix's, from resolume-scopes; convergence adds the pixel-unpack buffer and store to what it puts back, and lets the harness's negative control skip the restore.

### The About block — Stoatworks zoetrope

<https://github.com/stoatworks-labs/zoetrope>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout*.h, a provisional hand copy of intensifier's, itself from zoetrope's first header (no user guide yet).

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in cvtest and the picture under `--alpha` and `--robust`.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something this plugin calls directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud. Each was read on 2026-11-16; the spec (`SPEC-convergence.md`) quotes them.

### US patents, read as text on Google Patents

US 4,672,458 (North American Philips): in-line side tubes at 5–11° off the normal, 7.8° preferred — the keystone's angle. US 4,807,014 (U.S. Philips): the blue ZnS:Ag phosphor saturates, a 2.2× light gain from 0.5 to 2 mA, the blue tube needs more current, and the spot grows with it — I_s and the bloom. US 4,845,403 (Kasei Optonix, Hitachi): ZnS:Ag's γ of 0.61, and Table 1's 320 µA blue current for white — the cross-check on I_s and Bloom's default. US 7,005,801 (Hitachi Displays): the 5% spot, 0.099 mm at 1 mA and 0.256 mm at 4 mA — the spot law. US 4,481,445 (Zenith): spot size ∝ √I "in general" and +40% from 1 to 2 mA — the negative control. US 5,512,804 (Pioneer) and US 6,525,459 (Sony): dynamic focus and deliberate gun astigmatism. US 5,045,930 (Pioneer): liquid coupling and the Scheimpflug tilt (left out, said why). CA 1,294,702 (Matsushita): heat drift is mostly a static misconvergence — the warm-up's shape.

### Sony manuals

The VPH-G90 service manual (registration modes and order, green first; 25 zone locations per colour), the KP-43T70 manual (81 points at crosshatch intersections; KEY, SKEW, LIN, PIN), the VPH-D50Q install manual (the CRT conversion angle) and the RVP-411DS specifications (7-inch tubes, a 5.5-inch raster — the spot's scale on the picture).

### Barco owner's manuals

The Cine 8 (convergence zones 1–25 and the menu; per-set gamma corrections), the BARCODATA 708 and the Ciné 7 (the 20-minute warm-up — the default time constant's anchor).

### NEC CRT setup guide and the CurtPalme forum

"nor should the convergence be out more than one or two test pattern line thicknesses" (the NEC guide, as archived on the CurtPalme forum) — the default convergence error; the forum's "BG808 convergence question" thread — the warm-up drift's anecdotal size.

### Eizo T57S user's manual and Wikipedia — "Astigmatism (optical systems)"

A cold CRT's colour (left out, said why); Sturm's interval, the line foci and the circle of least confusion — the focus and astigmatism model.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — the clip's decoding and the output's encoding (source/Shaders.cpp; the harness's own copy in tools/cvtest).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
