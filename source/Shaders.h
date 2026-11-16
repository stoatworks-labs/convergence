#pragma once

#include <string>

/**
    The passes, in the order a frame runs them. Every one is a full-raster
    triangle into a buffer of its own; nothing is blended.

    1. **prep** -- per pixel, the clip decoded to linear, weighted by its
       alpha (straight alpha over black: a transparent pixel drives no gun),
       clamped to the signal's range. Drawn twice: texture A gets red, green,
       the blue gun's current I(d) and the blue drive d; texture B the blue
       light split between its four variance layers.
    2. **blur** -- separable, along x then along y, each channel by its own
       kernel from a small float texture (Projector.h, BuildKernels). Past
       the frame's edge is black. The current and the drive ride through on
       identity kernels. Four draws: A and B, each way.
    3. **composite** -- to the host: green read at the pixel; red and blue
       read where the zone field, the warm-up drift and the keystone send
       them, by bilinear interpolation done by hand in float; the four blue
       layers summed; encoded to sRGB; mixed with the clip.

    No time and no randomness reach a shader: the drift arrives as a
    displacement in pixels, the zone grid as a small float texture, both
    computed in double on the CPU. tools/glslc.sh's ASSEMBLED table mirrors
    Assemble().
*/
namespace convergence::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kPrepFragment;
extern const char* const kBlurFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Prep,
	Blur,
	Composite
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

} // namespace convergence::shaders
