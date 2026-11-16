#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged STANDARD parameter the host sees is 0..1, because
    `SetParamInfo` clamps an `FF_TYPE_STANDARD` default into 0..1 before
    `SetParamRange` could widen it. The conversions live in Controls.cpp, one
    function per control, each with its inverse where a default or the harness
    needs it. Option parameters hold the element index (an option reads back
    0..1 whatever its count: map by index). Setup is an `FF_TYPE_INTEGER`,
    declared with its real default and range (boreal's way).

    Units: fractions of the picture width for the zone error and the cold
    drift (so an installation looks the same at every raster), seconds for
    Warm-up, the defocus in units of the focus range (Delta = 2v - 1), mA for
    Bloom (the blue gun's current at full white).
*/
namespace convergence
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Convergence -------------------------------------------------------------
	PT_CONVERGENCE = 0,
	PT_ZONES,
	PT_SETUP,
	PT_KEYSTONE,

	// -- Warm-up -----------------------------------------------------------------
	PT_WARMUP,
	PT_COLD_DRIFT,
	PT_POWER_ON,

	// -- Focus -------------------------------------------------------------------
	PT_RED_FOCUS,
	PT_GREEN_FOCUS,
	PT_BLUE_FOCUS,
	PT_ASTIGMATISM,

	// -- Beam --------------------------------------------------------------------
	PT_BLOOM,

	// -- Look --------------------------------------------------------------------
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Integer,
	Event
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, the element index, or the integer itself
	const char* const* options;
	int optionCount;
	float low, high;///< an integer's range
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

/// The tubes, in the order the shaders and the tables use.
enum Tube
{
	kRed   = 0,
	kGreen = 1,
	kBlue  = 2
};

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double ConvergenceFromParam( float v );///< RMS zone error, fraction of the width, 0..0.015, linear
float ParamFromConvergence( double fraction );
int ZonesFromParam( float v );///< 5 or 9 (points a side)
int SetupFromParam( float v );///< 1..1000
double KeystoneFromParam( float v );///< the uncorrected fraction, 0..1
double WarmupFromParam( float v );///< tau, s, 1..1800, geometric
float ParamFromWarmup( double seconds );
double ColdDriftFromParam( float v );///< fraction of the width, 0..0.01, linear
float ParamFromColdDrift( double fraction );
double FocusFromParam( float v );///< Delta, -1..1; 0.5 is best focus
float ParamFromFocus( double delta );
double AstigmatismFromParam( float v );///< a, 0..0.5
float ParamFromAstigmatism( double a );
double BloomFromParam( float v );///< the blue gun's full-white current, mA, 0.1..10, geometric
float ParamFromBloom( double milliamps );

/// The ranges, for the harness and the docs.
constexpr double kConvergenceHigh = 0.015;
constexpr double kColdDriftHigh   = 0.01;
constexpr double kWarmupLow = 1.0, kWarmupHigh = 1800.0;
constexpr double kBloomLow = 0.1, kBloomHigh = 10.0;
constexpr double kAstigmatismHigh = 0.5;
constexpr int kSetupLow = 1, kSetupHigh = 1000;

} // namespace convergence
