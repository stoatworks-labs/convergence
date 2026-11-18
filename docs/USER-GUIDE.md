# Convergence user guide

Convergence is a three-tube CRT projector for [Resolume](https://resolume.com) Arena and
Avenue, as one FFGL **effect**, **SW Convergence**: your clip thrown onto the screen by three
tubes, one red, one green and one blue, each with its own lens.

The green tube looks straight at the screen. The red and blue tubes sit either side of it and
look at the screen from 7.8°, so each draws a slightly trapezoidal picture, and the set's
convergence electronics bend each side tube's picture back onto the green one, **zone by zone**.
A technician set those zones by eye and never got every one exactly right. So a white line is
clean in one part of the screen, has a red edge on its left somewhere else and a blue edge above
it in a corner; white text grows red and blue edges; and magenta shows wherever red and blue land
together without green. Switch the set on cold and the side tubes sit off as a whole, closing up
as it warms. Each tube focuses on its own, and an astigmatic gun sharpens horizontal lines at one
end of its focus control and vertical lines at the other. Push the blue gun hard and its spot
grows, because the blue phosphor saturates.

Nothing is drawn. Every fringe comes out of where each tube's picture lands and how big each
tube's spot is.

![White lines of a wireframe corridor on black, each with a thin red edge on one side and a blue
edge on the other, the edges changing side and width across the picture](convergence.png)

*SW Convergence on its defaults over Resolume's demo clip Enter5_12: a 960 × 540 crop from the
middle of a 1920 × 1080 frame, at full size, rendered by the offline harness rather than captured
from Resolume. At the defaults the fringes are a pixel or two; they are meant to be.*

> **Before you rely on this:** this is **v0.1.0**, the first release, and early days. The
> geometry is measured, not just asserted. An offline harness drives the plugin's own code, at
> two sizes, on an Apple Silicon Mac's GPU and again on Apple's software renderer, and reads
> the picture back: the colour fringes at **every zone point** of 5 × 5 and 9 × 9 grids match
> the installation's set errors to within half of a derived tolerance, whole-pixel shifts are
> exact, the keystone puts red and blue on opposite sides by the right amounts, and the warm-up's
> time constant comes back to within **0.01 %** at 30, 60 and 240 fps. Twenty-three deliberately
> wrong versions of the plugin and 46 one-character sabotages of its code are all caught, and all
> 13 controls change the picture. In Resolume Arena 7.29.0 on a Mac every control was there and
> did what this guide says, and the warm-up ran on Arena's clock with the time constant set;
> in Arena 7.27.1 on Windows, with no GPU, every control was there and changed the picture.
> That was over still pictures, never in a show. And several of the projector's numbers are
> **chosen**, not sourced
> (see [Known limits](#known-limits)). **Try it on a spare layer first**, and shout if anything
> misbehaves.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Installing

Download the build for your platform. For macOS there's a universal `.dmg` or `.zip`
(Apple silicon and Intel), **Developer ID-signed and notarised** so the bundle just loads
(the signing happens on my Mac shortly after each release is published, so a download made
in the first few minutes may need **Open** from the context menu once), and for Windows an
x64 installer or `.zip`. Put `Convergence.bundle` (macOS) or `Convergence.dll` (Windows) in
Resolume's FFGL folder, then restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout in its own folder.

The Windows installer puts the DLL there for you, per user, with no admin rights needed. It
defaults to Arena's folder (Avenue's if only Avenue is installed) and lets you change it.
The Windows builds aren't code-signed. Plugin files aren't gated the way `.exe` files are,
so Resolume loads them as normal; only the installer trips SmartScreen, once: **More info**
→ **Run anyway**.

It's in the Effects browser as **SW Convergence**. Drop it on a clip, a layer or the
composition.

---

## Start here

1. Put SW Convergence on a clip with fine bright lines or white text. At the defaults the set
   is well converged: the fringes are a pixel or two, as the NEC setup guide asks of a real
   one, and you may need to look closely.
2. Raise **Convergence**. The red and blue pictures wander off the green one, differently in
   each part of the screen. That pattern is one installation; **Setup** picks another, and
   **Zones** 9x9 gives a finer grid of zones.
3. Press **Power On**, then raise **Cold Drift** and lower **Warm-up**: the side tubes start
   off as a whole and close up as the set warms.
4. Pull **Blue Focus** away from 0.5, then raise **Astigmatism** and move the Focus controls
   again: one direction of line sharpens while the other softens.

---

## How it works, in one paragraph

The plugin decodes your clip to linear light and gives each colour to its own tube. The green
tube's picture lands where the clip is. The red and blue pictures land where the convergence
electronics put them: each side tube's raster is the trapezoid a tube 7.8° off-axis draws (by
how much the set leaves uncorrected), moved at each zone point by that zone's setting error,
bilinear between the zone points, plus the warm-up's drift, which is one shift per side tube
that dies away exponentially. Each tube's spot is a Gaussian: the focused beam, plus the blur of
being out of focus, horizontally and vertically on their own when the gun is astigmatic. The
blue phosphor saturates, so the set's calibrated greyscale drives bright blue with
disproportionate current, and the blue spot grows with the current, pixel by pixel. The three
pictures are added on the screen and encoded back to sRGB.

---

## The Convergence group

- **Convergence** is how carefully the set was converged: the setting error at each zone point,
  as an RMS over the zones, from 0 (perfect) to 1.5 % of the picture's width. The default,
  0.1 %, is about two pixels at 1080p, "one or two test pattern line thicknesses" in the NEC
  setup guide's words. Each zone point's error is drawn once per installation, separately for
  red and blue, horizontally and vertically.
- **Zones** is the grid the set converges on: **5x5**, like a Barco Cine 8 or a Sony VPH-G90, or
  **9x9**, like a Sony KP-43T70. Between zone points the correction is bilinear; beyond the
  outermost it holds.
- **Setup** (1 to 1000) is which installation. The same number gives the same errors in every
  instance, on every machine and at every resolution, so a "projector" can be recalled. 5x5 and
  9x9 at the same Setup are different installations.
- **Keystone** is how much of the side tubes' trapezoid the set leaves uncorrected, from 0 (a
  converged set: none) to 1 (all of it). The red tube's picture narrows towards one side of the
  screen and the blue tube's towards the other, so at 1 the fringes grow towards the left and
  right edges, red and blue on opposite sides of each line (about 20 px at the edge at 720p).

---

## The Warm-up group

- **Warm-up** is the warm-up's time constant, from 1 second to 30 minutes. The default, 400 s,
  makes Barco's 20-minute warm-up three time constants; after one time constant the drift is
  down to 37 %, after three to 5 %.
- **Cold Drift** is how far each side tube's picture sits off when the set is cold, as a share
  of the picture's width, 0 to 1 %. Each side tube drifts in its own direction, which Setup
  decides.
- **Power On** makes the set cold again: the drift jumps back to Cold Drift and starts closing
  up. It acts on the press.

**When the warm-up starts again.** A new instance starts cold: adding the effect, or opening a
composition that has it, switches the set on. After that only **Power On** makes it cold. In
Resolume Arena 7.29 on a Mac, retriggering the clip, stopping it and starting it again,
bypassing the layer for several seconds and changing the composition's resolution (1920 × 1080
to 1280 × 720 and back) each left the warm-up where it was, and it carried on closing up through
all of them, by as much as the time that had passed. After a resolution change the drift is the
same share of the picture's width as before. (The plugin steps the warm-up by Resolume's clock
for each frame it draws, at most a tenth of a second at a time, so if a host stopped drawing the
effect altogether, the warm-up would wait for it; Arena went on drawing it through every one of
those.)

---

## The Focus group

- **Red Focus**, **Green Focus** and **Blue Focus** are each tube's focus control. 0.5 is sharp;
  either side of it softens the same amount, because a beam out of focus by the same distance
  either way blurs alike. So with no astigmatism, 0.2 and 0.8 look the same.
- **Astigmatism** is a gun astigmatism common to all three tubes. The spot then has two line
  foci either side of best focus: below 0.5 on a tube's focus control its spot is narrow
  horizontally, so vertical lines are sharp and horizontal ones soft; above 0.5 the other way
  round. At 0.5 the spot is round and soft, the circle of least confusion between them.

The focused spot itself is small (the tubes' measured spot at their own scale is a fraction of a
pixel at 1080p), so all the softness you see is defocus.

---

## The Beam group

- **Bloom** is the blue gun's current at full white, from 0.1 to 10 mA. The blue phosphor
  saturates, so the set's calibrated greyscale drives bright blue with disproportionate current,
  and the blue spot grows with it, pixel by pixel. The default, 0.32 mA, is the blue tube's
  current for white in a 1989 Kasei Optonix and Hitachi patent.

**Bloom is small, because the real thing is.** At 10 mA the blue spot at full white is
1.26 pixels across at 1080p, and at the default a tenth of that. It shows as a slightly soft
blue edge on fine bright detail, never as a glow over broad shapes. Blue Focus and Astigmatism
soften far more.

---

## The Look group

- **Mix** blends the projected picture with the clip as it came in, premultiplied.

The projected picture fills the screen: over a transparent part of the clip its fringes still
put light there, and a transparent pixel drives no gun, so at Mix 1 the output is opaque, black
where the clip was transparent. Nothing that was under a transparent pixel ever shows.

---

## Recipes

- **A set just switched on:** Cold Drift 0.5 to 1 %, Warm-up 30 s to a few minutes, and press
  Power On on a cue. Map Power On to a key or a MIDI note to "switch on" at a moment in the show.
- **A rental that was never converged properly:** Convergence 0.3 to 0.6 with Zones 9x9, and
  step Setup until the pattern suits the content.
- **A set with its keystone correction off:** Keystone 1, Convergence low: the fringes grow
  towards the left and right edges, red one way, blue the other.
- **A tired blue tube:** Blue Focus 0.2, Bloom 3 to 10 mA, Astigmatism 0.3.
- **Behind other effects.** It reads the clip's colour and alpha, so colour effects before it
  are thrown by the tubes like the clip; anything after it (a glow, a scanline effect such as
  SW Old Cathode) treats the fringes as part of the picture.

---

## Flashing

Convergence doesn't make light that wasn't in the clip, and nothing in it pulses: it moves and
softens each colour's picture. Measured over all 33 of Resolume's demo clips (each 8 seconds,
through the plugin, counting changes of brightness over the whole frame and over every
third-of-the-frame window, the way the fleet's videos are checked):

- at the **defaults**, no clip flashes more often through the plugin than on its own, and none
  goes over three flashes a second (the worst, Beat 002, flashes three times a second on its
  own and the same through it);
- with **every control at an extreme at once** (Convergence and Keystone at 1, Bloom at 10 mA,
  Cold Drift 1 % with the shortest Warm-up from a cold start, every Focus at an end and
  Astigmatism at 1), the same, and the same with Bloom alone at 10 mA;
- pressing **Power On** 2, 6 and 10 times a second (Cold Drift 1 %, the shortest Warm-up) never
  gave more than one change of brightness in any second, in the browser demo's measurement of
  the same code.

The one way found to make it flash is **toggling Mix** (or switching the effect off and on) three
or more times a second over a clip with transparent areas and something bright behind it:
because the projected picture is opaque, that alternates the bright background with black. Move
Mix smoothly, or not at all, over transparent clips.

A clip that flashes on its own still flashes through it.

---

## Performance

On the Mac it was measured on (an Apple M4 Max, shared with other work), at the defaults over
the harness's test card: about **0.52 ms** of GPU time a frame at 720p, **1.13 ms** at 1080p
(7 % of a 60 fps frame) and **4.7 ms** at 4K. The blur costs more as the spots grow: with every
Focus at 0 and Astigmatism at 1 (a 98-pixel kernel at 1080p) it is about **2.5 ms** at 720p and
**8 ms** at 1080p. Another GPU will differ, and nothing has been timed inside Resolume.

---

## If it looks wrong

- **I can't see anything happening.** At the defaults the set is converged, and the fringes are
  a pixel or two. Use a clip with fine bright lines or text, or raise Convergence.
- **The Focus controls do the same thing at both ends.** They do: defocus either way blurs
  alike. Raise Astigmatism and the two ends differ.
- **Bloom does nothing.** It's real-size: at most about a pixel at 1080p, at 10 mA, and only on
  bright blue detail. See [the Beam group](#the-beam-group).
- **The fringes sit still on one clip and move on another.** The installation is fixed (Setup);
  only the warm-up moves it, and that's slow at the default 400 s time constant.
- **The transparent parts of my clip went black.** At Mix 1 the projected picture is opaque:
  a transparent pixel drives no gun. Lower Mix, or put it on a layer above.
- **The colours of my clip shifted at the edges.** That's the fringes: red and blue landing
  apart from green.

---

## Known limits

- **It has barely met Resolume.** It ran in Arena 7.29.0 on an Apple M4 Max, mounted over
  Arena's API on a still picture: all 13 controls were there with their defaults, each did what
  this guide says, and the warm-up ran on Arena's clock (a 10 s time constant came back as 10.0
  and 9.8 s). In Arena 7.27.1 on Windows (software rendering, no GPU) the same 13 were there
  and every control the fleet's test can set changed the picture (it can't press Power On, and
  Warm-up only shows after a press). It hasn't run on a moving clip in Resolume, or in a show,
  and its cost inside Arena wasn't measured.
- **Bloom is nearly invisible at real physics.** The blue spot at 10 mA is 1.26 pixels at
  1080p, and the default is a tenth of a pixel. A "spot scale" beyond the physics may come later
  if Bloom is wanted as a visible look.
- **Bloom's default (0.32 mA)** reads one patent table's current as a white raster's; Resolume's
  demo clips ruled out the other reading, nothing confirmed this one. A projectionist's real
  full-white current is probably higher (Philips speaks of 0.5 to 2 mA); a higher default would
  show more bloom on bright clips. It may change.
- **No zone can be set by hand.** The installation comes from Setup, so every control can be
  automated, but you can't pick one zone and move it. A per-zone mode needs Resolume to redraw a
  slider when the plugin changes it, which hasn't been tried.
- **The Focus controls are symmetric about 0.5**, faithfully; you might expect 0 to mean sharp
  and 1 soft. A later version may offer that.
- **Red is on the right and blue on the left.** That's a choice; real sets differ. A mirror
  option may come later.
- **Chosen, not sourced:** the throw (1.5 picture widths), red on the right and blue on the
  left, where the zone points sit and how the correction is interpolated between them, the
  setting errors being independent and normal, the warm-up's exponential shape, its time
  constant and its size, the focus blur's scale (1 % of the picture's height per unit of
  defocus), and the calibrated greyscale. No source for any of them was reached; a real set's
  figures would replace them.
- **The spot is the same all over the screen** (the set's dynamic focus taken as perfect), red
  and green never bloom, and the blue spot is a mixture of two Gaussians with the right width,
  not one Gaussian.
- **Straight or premultiplied alpha?** The plugin treats your clip's colour as straight alpha,
  which is how Resolume's own demo clips decode. If Resolume hands effects premultiplied colour,
  the edges of transparent areas come out darker than they should. Nobody has checked in
  Resolume yet.
- **No scanlines.** Layer SW Old Cathode for those.
- **Other GPUs.** The geometry is measured on one Apple GPU and Apple's software renderer
  and has run in Arena on that Mac and on Windows with Mesa's llvmpipe; the browser demo, a port of it, matched the harness to within one level on 219
  bytes in 473 million on the same Mac.

---

## About

The last group, **About**, has the plugin's name, version, licence and maker, and buttons that
open this user guide, the project page, the source on GitHub and the support page in your
browser.

## Links

- Project page: [stoatworks-labs.com/software/convergence](https://stoatworks-labs.com/software/convergence/)
- Source: [github.com/stoatworks-labs/convergence](https://github.com/stoatworks-labs/convergence)
- User guide: [stoatworks-labs.com/software/convergence/guide](https://stoatworks-labs.com/software/convergence/guide/)
- Browser demo: [convergence-demo.stoatworks-labs.com](https://convergence-demo.stoatworks-labs.com/)

## Reporting something

Something wrong, odd or missing? Open an issue at
[github.com/stoatworks-labs/convergence/issues](https://github.com/stoatworks-labs/convergence/issues).
The plugin keeps a short log of what the host did (the GPU it was given, whether its shaders
compiled, and how the host's clock counts time), which helps a lot:

```
macOS    ~/Library/Logs/convergence/
Windows  %LOCALAPPDATA%\convergence\logs\
```
