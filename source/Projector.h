#pragma once

#include <cstdint>
#include <vector>

/**
    The projector's physics, on the CPU, in double. Every constant here is
    one of SPEC-convergence.md's, with its source beside it; the harness
    restates the ones it checks with its own copies, so a slip here is a
    failed check and not a silently agreed one.

    Coordinates are the effect's frame in pixels, GL's way up: pixel (x, y)
    is centred at (x + 0.5, y + 0.5) and row 0 is the bottom row. Normalised
    U = (X - W/2)/(W/2), V = (Y - H/2)/(H/2).
*/
namespace convergence::projector
{

constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// The geometry
//---------------------------------------------------------------------------
/// The side tubes' angle to the screen normal. US 4,672,458 (Philips): "a
/// range from 5 deg to 11 deg"; its preferred embodiment, 7.8 deg.
constexpr double kTubeAngleDeg = 7.8;
/// The tangent of the half-field: 1/3, a throw of 1.5 picture widths
/// (chosen; no source checked for a typical throw ratio).
constexpr double kHalfFieldTan = 1.0 / 3.0;
/// e = t tan(theta): the homography's one coefficient for the right-hand
/// tube; the left-hand tube is -e.
double KeystoneE();

//---------------------------------------------------------------------------
// The spot
//---------------------------------------------------------------------------
/// US 7,005,801 (Hitachi): the 5%-beam spot diameter of a projection gun,
/// 0.099 mm at 1 mA and 0.256 mm at 4 mA.
constexpr double kSpotDiameter1mA = 0.099;
constexpr double kSpotDiameter4mA = 0.256;
/// p = ln(0.256/0.099)/ln 4: the power law through Hitachi's two points.
double SpotExponent();
/// A Gaussian's 5% diameter in sigmas: 2 sqrt(2 ln 20).
double FivePercentDiameterInSigma();
/// sigma at 1 mA, mm.
double SigmaOneMilliampMm();
/// Sony RVP-411DS: a 5.5-inch raster on a 7-inch tube, taken as 4:3: the
/// raster's height, mm.
constexpr double kRasterHeightMm = 5.5 * 0.6 * 25.4;
/// S: the blur of a unit defocus, as a fraction of the picture height (chosen).
constexpr double kFocusScale = 0.01;

//---------------------------------------------------------------------------
// The blue phosphor
//---------------------------------------------------------------------------
/// I_s in L = I / (1 + I/I_s), fitted to US 4,807,014's "0.5 mA to 2 mA
/// increases the light output by a factor of 2.2": 4/3 mA.
constexpr double kSaturationMa = 4.0 / 3.0;
/// The blue light's variance layers (SPEC: the bloom).
constexpr int kLayers = 4;

/// The blue gun's current for a drive d in [0, 1] when the set is
/// calibrated so the light is linear in d and full white draws I_w:
/// I(d) = d I_s I_w / (I_s + I_w (1 - d)).
double BlueCurrent( double drive, double fullWhiteMa );
/// The saturating light law, unnormalised: I / (1 + I/I_s).
double SaturatedLight( double milliamps );
/// sigma of the focused spot at a beam current, mm.
double SpotSigmaMm( double milliamps, double exponent );

//---------------------------------------------------------------------------
// The installation (seeded): the zone grid and the drift directions
//---------------------------------------------------------------------------
/// The set errors for Setup `setup` on an n x n grid, unit normal (RMS 1),
/// texel order: index (j n + i), i across, j up; per point red x, red y,
/// blue x, blue y. Scale by the RMS in pixels.
std::vector< double > ZoneTable( int setup, int n );
/// The cold drift's direction for a side tube (0 red, 1 blue): a unit vector.
void DriftDirection( int setup, int tube, double out[ 2 ] );

/// The hash behind both (PCG's output permutation) and a uniform in (0, 1).
uint32_t Hash( uint32_t v );
double UniformOf( uint32_t h );

//---------------------------------------------------------------------------
// One frame's optics, from the controls
//---------------------------------------------------------------------------
/// The controls in physical units.
struct Settings
{
	double focus[ 3 ]   = { 0.0, 0.0, 0.0 };///< Delta per tube (0 best)
	double astigmatism  = 0.0;              ///< a
	double fullWhiteMa  = 1.0;              ///< I_w, the blue gun at full white
	double keystone     = 0.0;              ///< k, the uncorrected fraction
};

/// The harness's wrong models of the optics. All off in the plugin.
struct OpticsHooks
{
	bool focusShared      = false;///< every tube takes green's focus
	bool astigSameSide    = false;///< both line foci on the same side of Delta
	bool defocusLinear    = false;///< blur added to the spot, not in quadrature
	bool spotExponentHalf = false;///< Zenith's square root for Hitachi's power law
	bool bloomOnRed       = false;///< the red spot at the blue gun's full-white current
};

/// What the passes are given, in pixels.
struct Frame
{
	int width = 0, height = 0;
	double sigmaOnePx    = 0.0;///< the focused spot's sigma at 1 mA
	double exponent      = 0.0;///< p
	double sigmaX[ 3 ]   = {}; ///< red, green: the whole spot; blue: the focus terms only
	double sigmaY[ 3 ]   = {};
	double layerStep     = 0.0;///< V_w / 3, px^2: layer k's beam variance is k x this
	double fullWhiteMa   = 1.0;
	double keystoneE     = 0.0;
	double keystoneK     = 0.0;
};
Frame Compute( const Settings& s, int width, int height, const OpticsHooks& hooks );

/// The blur passes' kernels: pixel-integrated Gaussians, cut at ceil(6 sigma)
/// and normalised, in double, then stored as floats. Rows: 0 horizontal over
/// texture A (red, green, the current, the drive), 1 horizontal over B (the
/// four blue layers), 2 and 3 the same vertically. Texel (i + radius, row).
struct Kernels
{
	int radius = 0;
	std::vector< float > texels;///< (2 radius + 1) x 4 rows x RGBA
	double sigma[ 4 ][ 4 ] = {};///< what each channel of each row was built for
};
/// The widest kernel the blur passes take: past it a kernel is cut and
/// renormalised. ceil(6 sigma) reaches it only on a raster taller than
/// 2844 px with a tube's focus and the astigmatism both at an extreme.
constexpr int kMaxRadius = 256;
Kernels BuildKernels( const Frame& f );
/// One pixel-integrated Gaussian, cut at ceil(6 sigma) (at most `cap`) and
/// normalised; sigma 0 is the identity.
std::vector< double > PixelGaussian( double sigma, int cap );

} // namespace convergence::projector
