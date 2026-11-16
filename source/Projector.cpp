#include "Projector.h"

#include <algorithm>
#include <cmath>

namespace convergence::projector
{

double KeystoneE()
{
	return kHalfFieldTan * std::tan( kTubeAngleDeg * kPi / 180.0 );
}

double SpotExponent()
{
	return std::log( kSpotDiameter4mA / kSpotDiameter1mA ) / std::log( 4.0 );
}

double FivePercentDiameterInSigma()
{
	return 2.0 * std::sqrt( 2.0 * std::log( 20.0 ) );
}

double SigmaOneMilliampMm()
{
	return kSpotDiameter1mA / FivePercentDiameterInSigma();
}

double BlueCurrent( double drive, double fullWhiteMa )
{
	//q = d I_w / (I_s + I_w) and I = I_s q / (1 - q), rearranged so nothing
	//is a difference of near-equal numbers: (1 - d) is exact near d = 1.
	return drive * kSaturationMa * fullWhiteMa / ( kSaturationMa + fullWhiteMa * ( 1.0 - drive ) );
}

double SaturatedLight( double milliamps )
{
	return milliamps / ( 1.0 + milliamps / kSaturationMa );
}

double SpotSigmaMm( double milliamps, double exponent )
{
	return SigmaOneMilliampMm() * std::pow( milliamps, exponent );
}

uint32_t Hash( uint32_t v )
{
	//PCG's RXS-M-XS output permutation of one LCG step (O'Neill; the
	//"pcg_hash" of Jarzynski & Olano, JCGT 9(3), 2020).
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

double UniformOf( uint32_t h )
{
	return ( static_cast< double >( h ) + 0.5 ) * ( 1.0 / 4294967296.0 );
}

namespace
{
uint32_t key( int setup, uint32_t a, uint32_t b, uint32_t c, uint32_t d )
{
	uint32_t h = Hash( static_cast< uint32_t >( setup ) ^ 0x9e3779b9u );
	h          = Hash( h ^ a );
	h          = Hash( h ^ b );
	h          = Hash( h ^ c );
	return Hash( h ^ d );
}

/// A standard normal by Box-Muller from two draws of one key.
double normal( uint32_t k )
{
	const double u1 = UniformOf( Hash( k ^ 0x68e31da4u ) );
	const double u2 = UniformOf( Hash( k ^ 0xb5297a4du ) );
	return std::sqrt( -2.0 * std::log( u1 ) ) * std::cos( 2.0 * kPi * u2 );
}
} // namespace

std::vector< double > ZoneTable( int setup, int n )
{
	std::vector< double > table( static_cast< size_t >( n ) * n * 4 );
	for( int j = 0; j < n; ++j )
		for( int i = 0; i < n; ++i )
			for( uint32_t component = 0; component < 4; ++component )
			{
				//component: tube (red 0, blue 1) x 2 + axis (x 0, y 1); the
				//grid's size is in the key, so 9 x 9 is not 5 x 5 extended.
				const uint32_t k = key( setup, static_cast< uint32_t >( n ), static_cast< uint32_t >( i ), static_cast< uint32_t >( j ), component );
				table[ ( static_cast< size_t >( j ) * n + i ) * 4 + component ] = normal( k );
			}
	return table;
}

void DriftDirection( int setup, int tube, double out[ 2 ] )
{
	const double phi = 2.0 * kPi * UniformOf( key( setup, 0xd21f7u, static_cast< uint32_t >( tube ), 0u, 0u ) );
	out[ 0 ]         = std::cos( phi );
	out[ 1 ]         = std::sin( phi );
}

//---------------------------------------------------------------------------
Frame Compute( const Settings& s, int width, int height, const OpticsHooks& hooks )
{
	Frame f;
	f.width          = width;
	f.height         = height;
	const double h   = static_cast< double >( height );
	f.exponent       = hooks.spotExponentHalf ? 0.5 : SpotExponent();
	f.sigmaOnePx     = SigmaOneMilliampMm() / kRasterHeightMm * h;
	f.fullWhiteMa    = s.fullWhiteMa;
	const double big = SpotSigmaMm( s.fullWhiteMa, f.exponent ) / kRasterHeightMm * h;
	f.layerStep      = big * big / static_cast< double >( kLayers - 1 );
	f.keystoneE      = KeystoneE();
	f.keystoneK      = s.keystone;

	//Sturm's interval: the horizontal line focus at Delta = -a, the vertical
	//at +a; each axis's blur is S times its distance from its focus, added to
	//the focused spot in quadrature.
	const double scale = kFocusScale * h;
	const double a     = s.astigmatism;
	for( int tube = 0; tube < 3; ++tube )
	{
		const double delta = hooks.focusShared ? s.focus[ 1 ] : s.focus[ tube ];
		const double bx    = scale * std::fabs( delta + a );
		const double by    = scale * std::fabs( hooks.astigSameSide ? delta + a : delta - a );
		//Red and green at their 1 mA spot; blue's beam is in its layers.
		double beam = tube == 2 ? 0.0 : f.sigmaOnePx;
		if( tube == 0 && hooks.bloomOnRed )
			beam = big;
		if( hooks.defocusLinear )
		{
			f.sigmaX[ tube ] = beam + bx;
			f.sigmaY[ tube ] = beam + by;
		}
		else
		{
			f.sigmaX[ tube ] = std::sqrt( beam * beam + bx * bx );
			f.sigmaY[ tube ] = std::sqrt( beam * beam + by * by );
		}
	}
	return f;
}

//---------------------------------------------------------------------------
std::vector< double > PixelGaussian( double sigma, int cap )
{
	if( !( sigma > 0.0 ) )
		return { 1.0 };
	const int radius = std::min( cap, static_cast< int >( std::ceil( 6.0 * sigma ) ) );
	std::vector< double > w( static_cast< size_t >( 2 * radius + 1 ) );
	const double k = 1.0 / ( sigma * std::sqrt( 2.0 ) );
	double sum     = 0.0;
	for( int i = -radius; i <= radius; ++i )
	{
		//Phi((i + 1/2)/sigma) - Phi((i - 1/2)/sigma), from the tail that does
		//not cancel.
		const double lo = ( std::fabs( static_cast< double >( i ) ) - 0.5 ) * k;
		const double hi = ( std::fabs( static_cast< double >( i ) ) + 0.5 ) * k;
		const double v  = i == 0 ? std::erf( 0.5 * k ) : 0.5 * ( std::erfc( lo ) - std::erfc( hi ) );
		w[ static_cast< size_t >( i + radius ) ] = v;
		sum += v;
	}
	for( double& v : w )
		v /= sum;
	return w;
}

Kernels BuildKernels( const Frame& f )
{
	Kernels k;
	//Per row, the four channels' sigmas (0 is the identity).
	for( int axis = 0; axis < 2; ++axis )
	{
		const double* tubes = axis == 0 ? f.sigmaX : f.sigmaY;
		double* a           = k.sigma[ axis * 2 ];
		double* b           = k.sigma[ axis * 2 + 1 ];
		a[ 0 ]              = tubes[ 0 ];
		a[ 1 ]              = tubes[ 1 ];
		a[ 2 ] = a[ 3 ] = 0.0;
		for( int layer = 0; layer < kLayers; ++layer )
			b[ layer ] = std::sqrt( tubes[ 2 ] * tubes[ 2 ] + layer * f.layerStep );
	}
	std::vector< double > kernels[ 4 ][ 4 ];
	for( int row = 0; row < 4; ++row )
		for( int c = 0; c < 4; ++c )
		{
			kernels[ row ][ c ] = PixelGaussian( k.sigma[ row ][ c ], kMaxRadius );
			k.radius            = std::max( k.radius, static_cast< int >( kernels[ row ][ c ].size() / 2 ) );
		}
	const int width = 2 * k.radius + 1;
	k.texels.assign( static_cast< size_t >( width ) * 4 * 4, 0.0f );
	for( int row = 0; row < 4; ++row )
		for( int c = 0; c < 4; ++c )
		{
			const std::vector< double >& w = kernels[ row ][ c ];
			const int r                    = static_cast< int >( w.size() / 2 );
			for( int i = -r; i <= r; ++i )
				k.texels[ ( static_cast< size_t >( row ) * width + ( i + k.radius ) ) * 4 + c ] = static_cast< float >( w[ static_cast< size_t >( i + r ) ] );
		}
	return k;
}

} // namespace convergence::projector
