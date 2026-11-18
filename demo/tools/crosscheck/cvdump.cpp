// The plugin's CPU numbers, as JSON, for crosscheck.py to hold the page's port
// of Controls.cpp and Projector.cpp to.
//
//   cvdump W H V0 .. V12     the frame for 13 host values (- for the default)
//   cvdump erf X ...         [ std::erf( x ), std::erfc( x ) ] for each x
//   cvdump gauss S ...       PixelGaussian( s, kMaxRadius ) for each s
//
// A host value is the host's 0..1 float (an option its element index, Setup
// the integer itself), parsed with strtof as cvtest --set parses it. Built by
// crosscheck.py from source/Projector.cpp and source/Controls.cpp alone, with
// no GL: nothing here is the page's. The frame part is
// ConvergencePlugin::CurrentSettings, the start of ProcessOpenGL and render()'s
// float uniforms, restated, at a warm-up state of 1 (a set just switched on).
#include "Controls.h"
#include "Projector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace convergence;

namespace
{
void list( const char* name, const double* v, size_t n, const char* tail )
{
	std::printf( "\"%s\":[", name );
	for( size_t i = 0; i < n; ++i )
		std::printf( "%s%.17g", i ? "," : "", v[ i ] );
	std::printf( "]%s", tail );
}
void floats( const char* name, const float* v, size_t n, const char* tail )
{
	std::printf( "\"%s\":[", name );
	for( size_t i = 0; i < n; ++i )
		std::printf( "%s%.9g", i ? "," : "", static_cast< double >( v[ i ] ) );
	std::printf( "]%s", tail );
}
} // namespace

int main( int argc, char** argv )
{
	if( argc >= 2 && std::strcmp( argv[ 1 ], "erf" ) == 0 )
	{
		std::printf( "[" );
		for( int i = 2; i < argc; ++i )
		{
			const double x = std::strtod( argv[ i ], nullptr );
			std::printf( "%s[%.17g,%.17g]", i > 2 ? "," : "", std::erf( x ), std::erfc( x ) );
		}
		std::printf( "]\n" );
		return 0;
	}
	if( argc >= 2 && std::strcmp( argv[ 1 ], "gauss" ) == 0 )
	{
		std::printf( "[" );
		for( int i = 2; i < argc; ++i )
		{
			const std::vector< double > w = projector::PixelGaussian( std::strtod( argv[ i ], nullptr ), projector::kMaxRadius );
			std::printf( "%s[", i > 2 ? "," : "" );
			for( size_t k = 0; k < w.size(); ++k )
				std::printf( "%s%.17g", k ? "," : "", w[ k ] );
			std::printf( "]" );
		}
		std::printf( "]\n" );
		return 0;
	}
	if( argc != 3 + PT_ABOUT_TEXT )
	{
		std::fprintf( stderr, "cvdump W H V0 .. V12 | cvdump erf X ... | cvdump gauss SIGMA ...\n" );
		return 2;
	}
	const int width  = std::atoi( argv[ 1 ] );
	const int height = std::atoi( argv[ 2 ] );
	// "-" leaves a control at the plugin's constructor default.
	float params[ PT_ABOUT_TEXT ];
	for( unsigned int i = 0; i < PT_ABOUT_TEXT; ++i )
	{
		const char* a = argv[ 3 + i ];
		params[ i ]   = a[ 0 ] == '-' && a[ 1 ] == '\0' ? InfoOf( i ).defaultValue : std::strtof( a, nullptr );
	}

	// ConvergencePlugin::CurrentSettings.
	projector::Settings s;
	s.focus[ kRed ]   = FocusFromParam( params[ PT_RED_FOCUS ] );
	s.focus[ kGreen ] = FocusFromParam( params[ PT_GREEN_FOCUS ] );
	s.focus[ kBlue ]  = FocusFromParam( params[ PT_BLUE_FOCUS ] );
	s.astigmatism     = AstigmatismFromParam( params[ PT_ASTIGMATISM ] );
	s.fullWhiteMa     = BloomFromParam( params[ PT_BLOOM ] );
	s.keystone        = KeystoneFromParam( params[ PT_KEYSTONE ] );

	const projector::Frame f     = projector::Compute( s, width, height, projector::OpticsHooks{} );
	const projector::Kernels k   = projector::BuildKernels( f );
	const int setup              = SetupFromParam( params[ PT_SETUP ] );
	const int n                  = ZonesFromParam( params[ PT_ZONES ] );
	const std::vector< double > u = projector::ZoneTable( setup, n );
	const double rms             = ConvergenceFromParam( params[ PT_CONVERGENCE ] ) * width;
	std::vector< float > zt( u.size() );
	for( size_t i = 0; i < u.size(); ++i )
		zt[ i ] = static_cast< float >( u[ i ] * rms );
	const double cold = ColdDriftFromParam( params[ PT_COLD_DRIFT ] ) * width;
	double dirs[ 2 ][ 2 ];
	projector::DriftDirection( setup, 0, dirs[ 0 ] );
	projector::DriftDirection( setup, 1, dirs[ 1 ] );
	// ProcessOpenGL's drift at warm = 1, then the glUniform4f's floats.
	const double warm   = 1.0;
	const double amount = warm * cold;
	const float drift[ 4 ] = { static_cast< float >( amount * dirs[ 0 ][ 0 ] ), static_cast< float >( amount * dirs[ 0 ][ 1 ] ),
		                       static_cast< float >( amount * dirs[ 1 ][ 0 ] ), static_cast< float >( amount * dirs[ 1 ][ 1 ] ) };
	// render()'s glUniform1f arguments, in its order.
	const float uniforms[ 9 ] = { static_cast< float >( projector::kSaturationMa ), static_cast< float >( f.fullWhiteMa ),
		                          static_cast< float >( projector::kSaturationMa * f.fullWhiteMa ),
		                          static_cast< float >( f.sigmaOnePx * f.sigmaOnePx ), static_cast< float >( 2.0 * f.exponent ),
		                          static_cast< float >( f.layerStep ), static_cast< float >( f.keystoneE ),
		                          static_cast< float >( f.keystoneK ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) };

	std::printf( "{" );
	floats( "params", params, PT_ABOUT_TEXT, ",\n" );
	std::printf( "\"conversions\":{\"convergence\":%.17g,\"zones\":%d,\"setup\":%d,\"keystone\":%.17g,\"warmup\":%.17g,"
	             "\"coldDrift\":%.17g,\"redFocus\":%.17g,\"greenFocus\":%.17g,\"blueFocus\":%.17g,\"astigmatism\":%.17g,"
	             "\"bloom\":%.17g},\n",
	             ConvergenceFromParam( params[ PT_CONVERGENCE ] ), n, setup, KeystoneFromParam( params[ PT_KEYSTONE ] ),
	             WarmupFromParam( params[ PT_WARMUP ] ), ColdDriftFromParam( params[ PT_COLD_DRIFT ] ), s.focus[ kRed ],
	             s.focus[ kGreen ], s.focus[ kBlue ], s.astigmatism, s.fullWhiteMa );
	std::printf( "\"frame\":{\"sigmaOnePx\":%.17g,\"exponent\":%.17g,", f.sigmaOnePx, f.exponent );
	list( "sigmaX", f.sigmaX, 3, "," );
	list( "sigmaY", f.sigmaY, 3, "," );
	std::printf( "\"layerStep\":%.17g,\"fullWhiteMa\":%.17g,\"keystoneE\":%.17g,\"keystoneK\":%.17g},\n", f.layerStep,
	             f.fullWhiteMa, f.keystoneE, f.keystoneK );
	std::printf( "\"kernels\":{\"radius\":%d,\"sigma\":[", k.radius );
	for( int row = 0; row < 4; ++row )
	{
		std::printf( "%s", row ? "," : "" );
		std::printf( "[%.17g,%.17g,%.17g,%.17g]", k.sigma[ row ][ 0 ], k.sigma[ row ][ 1 ], k.sigma[ row ][ 2 ], k.sigma[ row ][ 3 ] );
	}
	std::printf( "]," );
	floats( "texels", k.texels.data(), k.texels.size(), "},\n" );
	std::printf( "\"zones\":{\"n\":%d,\"setup\":%d,", n, setup );
	list( "unit", u.data(), u.size(), "," );
	floats( "texels", zt.data(), zt.size(), "},\n" );
	std::printf( "\"drift\":{\"dirs\":[[%.17g,%.17g],[%.17g,%.17g]],", dirs[ 0 ][ 0 ], dirs[ 0 ][ 1 ], dirs[ 1 ][ 0 ], dirs[ 1 ][ 1 ] );
	floats( "uniform", drift, 4, "},\n" );
	floats( "uniforms", uniforms, 9, "}\n" );
	return 0;
}
