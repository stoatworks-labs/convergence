#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace convergence
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
float inverseLinear( double value, double high )
{
	return static_cast< float >( std::clamp( value / high, 0.0, 1.0 ) );
}

const char* const kZoneNames[] = { "5x5", "9x9" };
constexpr int kZoneCounts[]    = { 5, 9 };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0, 0.0f, 1.0f };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count, 0.0f, 1.0f };
}
ControlInfo integer( unsigned int id, const char* name, const char* group, float value, float low, float high )
{
	return { id, name, group, Kind::Integer, value, nullptr, 0, low, high };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0, 0.0f, 1.0f };
}

//---------------------------------------------------------------------------
// The defaults (SPEC-convergence.md, Controls). Convergence 0.1% of the
// width, about one crosshatch line of a 1024-line raster (the NEC setup
// guide's "one or two test pattern line thicknesses"); a 5x5 grid (Barco
// Cine 8, Sony VPH-G90); keystone corrected; a 400 s warm-up (Barco's 20
// minutes = 3 tau, chosen) from a cold drift of 0.1% (chosen); best focus, no
// astigmatism; Bloom from Resolume's demo clips (tools/clips.py defaults: the
// median clip's mean blue current at 320 uA, US 4,845,403's Table 1).
//---------------------------------------------------------------------------
constexpr double kDefaultBloom = 1.93;

const ControlInfo kTable[] = {
	standard( PT_CONVERGENCE, "Convergence", "Convergence", ParamFromConvergence( 0.001 ) ),
	option( PT_ZONES, "Zones", "Convergence", 0.0f, kZoneNames, 2 ),
	integer( PT_SETUP, "Setup", "Convergence", 1.0f, static_cast< float >( kSetupLow ), static_cast< float >( kSetupHigh ) ),
	standard( PT_KEYSTONE, "Keystone", "Convergence", 0.0f ),
	standard( PT_WARMUP, "Warm-up", "Warm-up", ParamFromWarmup( 400.0 ) ),
	standard( PT_COLD_DRIFT, "Cold Drift", "Warm-up", ParamFromColdDrift( 0.001 ) ),
	event( PT_POWER_ON, "Power On", "Warm-up" ),
	standard( PT_RED_FOCUS, "Red Focus", "Focus", 0.5f ),
	standard( PT_GREEN_FOCUS, "Green Focus", "Focus", 0.5f ),
	standard( PT_BLUE_FOCUS, "Blue Focus", "Focus", 0.5f ),
	standard( PT_ASTIGMATISM, "Astigmatism", "Focus", 0.0f ),
	standard( PT_BLOOM, "Bloom", "Beam", ParamFromBloom( kDefaultBloom ) ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const long i = std::lround( static_cast< double >( value ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double ConvergenceFromParam( float v )
{
	return kConvergenceHigh * clamp01( v );
}
float ParamFromConvergence( double fraction )
{
	return inverseLinear( fraction, kConvergenceHigh );
}
int ZonesFromParam( float v )
{
	return kZoneCounts[ OptionIndex( v, 2 ) ];
}
int SetupFromParam( float v )
{
	const double rounded = std::round( static_cast< double >( v ) );
	return static_cast< int >( std::clamp( rounded == rounded ? rounded : 1.0, static_cast< double >( kSetupLow ), static_cast< double >( kSetupHigh ) ) );
}
double KeystoneFromParam( float v )
{
	return clamp01( v );
}
double WarmupFromParam( float v )
{
	return geometric( v, kWarmupLow, kWarmupHigh );
}
float ParamFromWarmup( double seconds )
{
	return inverseGeometric( seconds, kWarmupLow, kWarmupHigh );
}
double ColdDriftFromParam( float v )
{
	return kColdDriftHigh * clamp01( v );
}
float ParamFromColdDrift( double fraction )
{
	return inverseLinear( fraction, kColdDriftHigh );
}
double FocusFromParam( float v )
{
	return 2.0 * clamp01( v ) - 1.0;
}
float ParamFromFocus( double delta )
{
	return static_cast< float >( std::clamp( 0.5 * ( delta + 1.0 ), 0.0, 1.0 ) );
}
double AstigmatismFromParam( float v )
{
	return kAstigmatismHigh * clamp01( v );
}
float ParamFromAstigmatism( double a )
{
	return inverseLinear( a, kAstigmatismHigh );
}
double BloomFromParam( float v )
{
	return geometric( v, kBloomLow, kBloomHigh );
}
float ParamFromBloom( double milliamps )
{
	return inverseGeometric( milliamps, kBloomLow, kBloomHigh );
}

} // namespace convergence
