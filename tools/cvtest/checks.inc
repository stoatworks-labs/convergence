//===========================================================================
// The harness's own physics: SPEC-convergence.md restated with the harness's
// own constants. None of these is read from the plugin; the zone grid and the
// drift directions are (an installation is data, not physics).
//===========================================================================
constexpr double hPi        = 3.14159265358979323846;
constexpr double hAngleDeg  = 7.8;                      ///< US 4,672,458's preferred embodiment
constexpr double hHalfField = 1.0 / 3.0;                ///< t: a throw of 1.5 widths (the spec's choice)
constexpr double hD1        = 0.099;                    ///< mm, the 5% spot at 1 mA (US 7,005,801)
constexpr double hD4        = 0.256;                    ///< mm, at 4 mA (US 7,005,801)
constexpr double hRasterMm  = 5.5 * 25.4 * 3.0 / 5.0;   ///< a 5.5-inch 4:3 raster's height (Sony RVP-411DS)
constexpr double hFocusS    = 0.01;                     ///< S, a fraction of the height (the spec's choice)

/// I_s from Philips' "0.5 mA to 2 mA increases the light output by a factor
/// of 2.2" under L = I/(1 + I/I_s): 4 (1 + 0.5/I_s) = 2.2 (1 + 2/I_s), so
/// I_s = (4.4 - 2) / (4 - 2.2).
double hSaturation()
{
	return ( 2.2 * 2.0 - 4.0 * 0.5 ) / ( 4.0 - 2.2 );
}
double hE()
{
	return hHalfField * std::tan( hAngleDeg * hPi / 180.0 );
}
double hP()
{
	return std::log( hD4 / hD1 ) / std::log( 4.0 );
}
double hSigma1Px( int height )
{
	//The 5% diameter of exp(-r^2 / 2 sigma^2) is 2 sigma sqrt(2 ln 20).
	return hD1 / ( 2.0 * std::sqrt( 2.0 * std::log( 20.0 ) ) ) / hRasterMm * height;
}
double hSpotSigmaPx( double milliamps, int height, double p )
{
	return hSigma1Px( height ) * std::pow( milliamps, p );
}
/// The spec's form: q = d I_w / (I_s + I_w), I = I_s q / (1 - q).
double hCurrent( double d, double iw )
{
	const double q = d * iw / ( hSaturation() + iw );
	return hSaturation() * q / ( 1.0 - q );
}
double hLight( double milliamps )
{
	return milliamps / ( 1.0 + milliamps / hSaturation() );
}

double hClamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double hConvergence( float v )
{
	return 0.015 * hClamp01( v );
}
double hColdDrift( float v )
{
	return 0.01 * hClamp01( v );
}
double hTau( float v )
{
	return 1.0 * std::pow( 1800.0, hClamp01( v ) );
}
double hDelta( float v )
{
	return 2.0 * hClamp01( v ) - 1.0;
}
double hAstig( float v )
{
	return 0.5 * hClamp01( v );
}
double hBloom( float v )
{
	return 0.1 * std::pow( 100.0, hClamp01( v ) );
}

/// The discrete variance of a pixel-integrated Gaussian (Phi differences),
/// cut at ceil(6 sigma) and normalised: what an isolated dot's light spreads
/// to. Restated, not called from Projector.cpp.
double hDiscreteVar( double sigma )
{
	if( !( sigma > 0.0 ) )
		return 0.0;
	const int r    = static_cast< int >( std::ceil( 6.0 * sigma ) );
	const double k = 1.0 / ( sigma * std::sqrt( 2.0 ) );
	double s0 = 0.0, s2 = 0.0;
	for( int i = -r; i <= r; ++i )
	{
		//Phi((|i| + 1/2)/sigma) - Phi((|i| - 1/2)/sigma), from the upper tail.
		const double a = std::fabs( static_cast< double >( i ) );
		const double w = 0.5 * ( std::erfc( ( a - 0.5 ) * k ) - std::erfc( ( a + 0.5 ) * k ) );
		s0 += w;
		s2 += w * i * i;
	}
	return s2 / s0;
}

/// One float's spacing at x.
double ulpOf( double x )
{
	const float f = static_cast< float >( std::fabs( x ) );
	return static_cast< double >( std::nextafter( f, std::numeric_limits< float >::infinity() ) - f );
}

/// The larger of two errors, NaN winning: std::max( worst, NaN ) keeps
/// worst, so a model that produced NaN would pass every "worst <= tolerance"
/// (a mutant that divides by alpha did exactly that).
double worse( double a, double b )
{
	return ( a != a || b != b ) ? std::numeric_limits< double >::quiet_NaN() : std::max( a, b );
}
template< typename... T >
double worse( double a, double b, T... rest )
{
	return worse( worse( a, b ), rest... );
}

//===========================================================================
// Rigs and pictures.
//===========================================================================
/// Every check sets every control it depends on (honeydew's trap). These
/// are the quiet ones: a converged set, warm, focused, Bloom at 1 mA.
void defaults( Rig& rig )
{
	rig.Set( PT_CONVERGENCE, 0.0f );
	rig.Set( PT_ZONES, 0 );
	rig.Set( PT_SETUP, 1.0f );
	rig.Set( PT_KEYSTONE, 0.0f );
	rig.Set( PT_WARMUP, ParamFromWarmup( 400.0 ) );
	rig.Set( PT_COLD_DRIFT, 0.0f );
	rig.Set( PT_RED_FOCUS, 0.5f );
	rig.Set( PT_GREEN_FOCUS, 0.5f );
	rig.Set( PT_BLUE_FOCUS, 0.5f );
	rig.Set( PT_ASTIGMATISM, 0.0f );
	rig.Set( PT_BLOOM, ParamFromBloom( 1.0 ) );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr, double fps = 60.0 )
{
	rig.fps = fps;
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.HooksForTest() = perturb;
	defaults( rig );
	return true;
}

/// The Warm-up parameter for tau, by the harness's own inverse.
float warmupParam( double tau )
{
	return static_cast< float >( std::log( tau ) / std::log( 1800.0 ) );
}

Floats grey( int w, int h, double code, double alpha = 1.0 )
{
	return flatCard( w, h, static_cast< float >( code ), static_cast< float >( code ), static_cast< float >( code ), static_cast< float >( alpha ) );
}

/// Soft lines on black, grey (the three guns alike): vertical lines at xs,
/// horizontal at ys, each a Gaussian of sigma px in LINEAR light, the
/// brighter where two cross; encoded to sRGB in double.
constexpr double kLineSigma = 1.5;
Floats linesCard( int w, int h, const std::vector< double >& xs, const std::vector< double >& ys, double sigma = kLineSigma )
{
	Floats card( static_cast< size_t >( w ) * h * 4 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			double v = 0.0;
			for( const double cx : xs )
				v = std::max( v, std::exp( -( x + 0.5 - cx ) * ( x + 0.5 - cx ) / ( 2.0 * sigma * sigma ) ) );
			for( const double cy : ys )
				v = std::max( v, std::exp( -( y + 0.5 - cy ) * ( y + 0.5 - cy ) / ( 2.0 * sigma * sigma ) ) );
			const float code = static_cast< float >( codeOf( v ) );
			float* o         = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			o[ 0 ] = o[ 1 ] = o[ 2 ] = code;
			o[ 3 ]                   = 1.0f;
		}
	return card;
}

/// One channel of the output in linear light, rows bottom first.
std::vector< double > channel( const Floats& out, int c )
{
	std::vector< double > v( out.size() / 4 );
	for( size_t p = 0; p < v.size(); ++p )
		v[ p ] = linearOf( out[ 4 * p + c ] );
	return v;
}

/// The centroid of a line's light along one row (vertical line) or one
/// column (horizontal line), in a window about `centre`; also its mean
/// absolute deviation.
struct Centroid
{
	double at    = 0.0;
	double mad   = 0.0;
	double sum   = 0.0;
	bool clipped = false;///< the window ran off the frame
};
Centroid centroidAlong( const std::vector< double >& ch, int w, int h, bool vertical, int index, double centre, double half )
{
	const int n  = vertical ? w : h;
	const int lo = std::max( 0, static_cast< int >( std::floor( centre - half ) ) );
	const int hi = std::min( n - 1, static_cast< int >( std::ceil( centre + half ) ) );
	auto value   = [ & ]( int k ) { return vertical ? ch[ static_cast< size_t >( index ) * w + k ] : ch[ static_cast< size_t >( k ) * w + index ]; };
	Centroid c;
	c.clipped = centre - half < 0.0 || centre + half > n;
	double s1 = 0.0;
	for( int k = lo; k <= hi; ++k )
	{
		c.sum += value( k );
		s1 += ( k + 0.5 ) * value( k );
	}
	c.at = s1 / c.sum;
	for( int k = lo; k <= hi; ++k )
		c.mad += std::fabs( k + 0.5 - c.at ) * value( k );
	c.mad /= c.sum;
	return c;
}

/// The window's half-width for a line of the card's sigma: 6 sigma and a
/// pixel, so about 1e-9 of its light falls outside.
double windowHalf( double sigmaTotal )
{
	return 6.0 * sigmaTotal + 1.0;
}

//===========================================================================
// --model (offline): the constants back to their sources, the tables'
// statistics, the conversions.
//===========================================================================
int runModel( const Perturb& perturb )
{
	std::printf( "\n=== model: the projector's constants against their sources, the installation's statistics, the controls' conversions\n" );
	namespace pj = projector;

	//The spot law through Hitachi's two points.
	const double k5   = 2.0 * std::sqrt( 2.0 * std::log( 20.0 ) );
	const double d1   = pj::SpotSigmaMm( 1.0, pj::SpotExponent() ) * k5;
	const double d4   = pj::SpotSigmaMm( 4.0, pj::SpotExponent() ) * k5;
	const double zen  = pj::SpotSigmaMm( 2.0, 0.5 ) / pj::SpotSigmaMm( 1.0, 0.5 );
	Check( std::fabs( d1 / hD1 - 1.0 ) < 1e-12 && std::fabs( d4 / hD4 - 1.0 ) < 1e-12 && std::fabs( pj::SpotExponent() - hP() ) < 1e-12,
	       fmt( "spot: the 5%% diameter %.6f mm at 1 mA and %.6f mm at 4 mA (US 7,005,801: 0.099, 0.256); p = %.7f; Zenith's p = 1/2 would give "
	            "x%.4f from 1 to 2 mA (US 4,481,445: 'about forty percent')",
	            d1, d4, pj::SpotExponent(), zen ) );
	const double r5 = std::sqrt( -2.0 * std::log( 0.05 ) );//exp(-r^2/2) = 0.05
	Check( std::fabs( pj::FivePercentDiameterInSigma() - 2.0 * r5 ) < 1e-12,
	       fmt( "a Gaussian's 5%% diameter: %.7f sigma, where exp(-r^2/2 sigma^2) = 0.05 puts it (%.7f)", pj::FivePercentDiameterInSigma(), 2.0 * r5 ) );
	Check( std::fabs( pj::kRasterHeightMm - hRasterMm ) < 1e-12,
	       fmt( "the raster: %.4f mm high (a 5.5-inch 4:3 raster, Sony RVP-411DS); sigma(1 mA) = %.4e of the height, %.3f px at 1080", pj::kRasterHeightMm,
	            pj::SigmaOneMilliampMm() / pj::kRasterHeightMm, pj::SigmaOneMilliampMm() / pj::kRasterHeightMm * 1080.0 ) );

	//The blue phosphor.
	const double ratio = pj::SaturatedLight( 2.0 ) / pj::SaturatedLight( 0.5 );
	const double slope = 1.0 / ( 1.0 + 0.85 / pj::kSaturationMa );
	Check( std::fabs( pj::kSaturationMa - hSaturation() ) < 1e-12 && std::fabs( ratio - 2.2 ) < 1e-12 && std::fabs( slope - 0.61 ) < 0.005,
	       fmt( "saturation: I_s %.6f mA; L(2 mA)/L(0.5 mA) = %.6f (US 4,807,014: 2.2); d ln L / d ln I at 0.85 mA = %.4f (US 4,845,403's gamma 0.61, "
	            "to its two digits); efficiency down x%.2f at 2.7 mA and x%.2f at 6.7 mA (Philips' '3 to 6')",
	            pj::kSaturationMa, ratio, slope, 1.0 + 2.7 / pj::kSaturationMa, 1.0 + 6.7 / pj::kSaturationMa ) );
	double worstCurrent = 0.0, worstLinear = 0.0;
	for( const double iw : { 0.1, 0.32, 1.0, 1.93, 4.0, 10.0 } )
	{
		worstCurrent = worse( worstCurrent, std::fabs( pj::BlueCurrent( 1.0, iw ) / iw - 1.0 ) );
		for( int i = 0; i <= 100; ++i )
		{
			const double d = i / 100.0;
			worstCurrent   = worse( worstCurrent, std::fabs( pj::BlueCurrent( d, iw ) - hCurrent( d, iw ) ) / iw );
			worstLinear    = worse( worstLinear, std::fabs( hLight( pj::BlueCurrent( d, iw ) ) / hLight( iw ) - d ) );
		}
	}
	Check( worstCurrent < 1e-12 && worstLinear < 1e-12,
	       fmt( "the calibrated blue gun, I_w 0.1 to 10 mA: I(d) against the spec's q-form off by %.1e of I_w, I(1) = I_w; the light L(I(d))/L(I_w) "
	            "off d by %.1e (tolerance 1e-12)",
	            worstCurrent, worstLinear ) );

	//The geometry: the homography from a ray-plane intersection, done here
	//in 3D. The right tube's lens at (b, 0, 0), aimed at the screen centre
	//(0, 0, D), so tan(theta) = b / D; a raster point leaves along
	//(u t, v t, 1) in the tube's own frame, rotated about y by -theta.
	const double e    = hE();
	double worstHomog = 0.0, worstTrip = 0.0;
	{
		const double theta = hAngleDeg * hPi / 180.0, D = 10.0, b = D * std::tan( theta ), t = hHalfField;
		auto land          = [ & ]( double u, double v, double& X, double& Y ) {
            const double dx = u * t, dy = v * t, dz = 1.0;
            //rotate by -theta about y: aiming the lens from +b back at the centre
            const double rx = dx * std::cos( theta ) - dz * std::sin( theta );
            const double rz = dx * std::sin( theta ) + dz * std::cos( theta );
            const double s  = D / rz;
            X               = b + s * rx;
            Y               = s * dy;
		};
		double x0, y0, xu, yu, xv, yv, xu2, yu2, xv2, yv2;
		const double h = 1e-5;
		land( 0.0, 0.0, x0, y0 );
		land( h, 0.0, xu, yu );
		land( -h, 0.0, xu2, yu2 );
		land( 0.0, h, xv, yv );
		land( 0.0, -h, xv2, yv2 );
		const double mx = ( xu - xu2 ) / ( 2.0 * h ), my = ( yv - yv2 ) / ( 2.0 * h );//the centre's magnifications
		for( int i = -10; i <= 10; ++i )
			for( int j = -10; j <= 10; ++j )
			{
				const double u = i / 10.0, v = j / 10.0;
				double X, Y;
				land( u, v, X, Y );
				const double U = ( X - x0 ) / mx, V = ( Y - y0 ) / my;
				worstHomog     = worse( worstHomog, std::fabs( U - u / ( 1.0 + e * u ) ), std::fabs( V - v / ( 1.0 + e * u ) ) );
				//H^-1 ( H(u, v) ) = (u, v)
				const double ui = U / ( 1.0 - e * U ), vi = V / ( 1.0 - e * U );
				worstTrip       = worse( worstTrip, std::fabs( ui - u ), std::fabs( vi - v ) );
			}
	}
	Check( std::fabs( pj::KeystoneE() - e ) < 1e-15 && worstHomog < 1e-9 && worstTrip < 1e-9,
	       fmt( "geometry: e = t tan(7.8 deg) = %.6f; a 3D ray-plane intersection lands within %.1e of U = u/(1 + e u), V = v/(1 + e u) (tolerance "
	            "1e-9: the central-difference magnifications); H^-1 round trip %.1e; the uncorrected corner %.2f%% of the height off green's "
	            "(the spec: 2.2%%); third-order residual e^2 = %.3f%% of the half-width (0.21%%); a 5x5 grid's bilinear residual %.2e (2.5e-4)",
	            pj::KeystoneE(), worstHomog, worstTrip, 100.0 * 0.5 * e / ( 1.0 + e ), 100.0 * e * e, 0.16 / 8.0 * 6.0 * e * e ) );

	//The installation's statistics: unit normal, independent per tube, the
	//same every time.
	for( const int n : { 5, 9 } )
	{
		const int setups = 2000;
		double s[ 4 ] = {}, s2[ 4 ] = {}, srb = 0.0;
		size_t count  = 0;
		bool same     = true;
		for( int setup = 1; setup <= setups; ++setup )
		{
			const std::vector< double > t = pj::ZoneTable( setup, n );
			if( setup <= 3 )
				same = same && t == pj::ZoneTable( setup, n );
			for( int p = 0; p < n * n; ++p )
			{
				for( int c = 0; c < 4; ++c )
				{
					s[ c ] += t[ 4 * p + c ];
					s2[ c ] += t[ 4 * p + c ] * t[ 4 * p + c ];
				}
				srb += t[ 4 * p ] * t[ 4 * p + 2 ];
				++count;
			}
		}
		double worstMean = 0.0, worstVar = 0.0;
		for( int c = 0; c < 4; ++c )
		{
			worstMean = worse( worstMean, std::fabs( s[ c ] / count ) );
			worstVar  = worse( worstVar, std::fabs( s2[ c ] / count - 1.0 ) );
		}
		const double corr = srb / count;
		//5 standard errors: the mean's 1/sqrt(N), the variance's sqrt(2/N), a
		//correlation's 1/sqrt(N).
		const double N = static_cast< double >( count );
		Check( worstMean < 5.0 / std::sqrt( N ) && worstVar < 5.0 * std::sqrt( 2.0 / N ) && std::fabs( corr ) < 5.0 / std::sqrt( N ) && same,
		       fmt( "%dx%d zones over %d Setups (%zu points a component): worst |mean| %.4f (5 s.e. %.4f), |variance - 1| %.4f (%.4f), red-blue "
		            "correlation %+.4f (%.4f); the same table twice: %s",
		            n, n, setups, count, worstMean, 5.0 / std::sqrt( N ), worstVar, 5.0 * std::sqrt( 2.0 / N ), corr, 5.0 / std::sqrt( N ),
		            same ? "yes" : "NO" ) );
	}
	{
		double sc = 0.0, ss = 0.0, worstNorm = 0.0;
		const int setups = 2000;
		for( int setup = 1; setup <= setups; ++setup )
			for( int tube = 0; tube < 2; ++tube )
			{
				double d[ 2 ];
				pj::DriftDirection( setup, tube, d );
				sc += d[ 0 ];
				ss += d[ 1 ];
				worstNorm = worse( worstNorm, std::fabs( std::hypot( d[ 0 ], d[ 1 ] ) - 1.0 ) );
			}
		const double N = 2.0 * setups;
		//cos and sin of a uniform angle: variance 1/2 each.
		Check( worstNorm < 1e-15 && std::fabs( sc / N ) < 5.0 * std::sqrt( 0.5 / N ) && std::fabs( ss / N ) < 5.0 * std::sqrt( 0.5 / N ),
		       fmt( "drift directions over %d Setups: unit (worst %.1e), mean (%+.4f, %+.4f) (5 s.e. %.4f)", setups, worstNorm, sc / N, ss / N,
		            5.0 * std::sqrt( 0.5 / N ) ) );
	}

	//The frame's optics against the harness's restatement.
	{
		pj::Settings s;
		s.focus[ 0 ]     = -0.4;
		s.focus[ 1 ]     = 0.2;
		s.focus[ 2 ]     = 0.6;
		s.astigmatism    = 0.3;
		s.fullWhiteMa    = 4.0;
		const int H      = 720;
		const pj::Frame f = pj::Compute( s, 1280, H, perturb.optics );
		double worst     = 0.0;
		for( int tube = 0; tube < 3; ++tube )
		{
			const double beam = tube == 2 ? 0.0 : hSigma1Px( H );
			const double bx   = hFocusS * H * ( s.focus[ tube ] + s.astigmatism );
			const double by   = hFocusS * H * ( s.focus[ tube ] - s.astigmatism );
			worst             = worse( worst, std::fabs( f.sigmaX[ tube ] - std::sqrt( beam * beam + bx * bx ) ), std::fabs( f.sigmaY[ tube ] - std::sqrt( beam * beam + by * by ) ) );
		}
		const double vw = hSpotSigmaPx( 4.0, H, hP() ) * hSpotSigmaPx( 4.0, H, hP() );
		worst           = worse( worst, std::fabs( f.layerStep * 3.0 - vw ), std::fabs( f.sigmaOnePx - hSigma1Px( H ) ) );
		Check( worst < 1e-12, fmt( "a frame at 720 (focus -0.4, 0.2, 0.6; astigmatism 0.3; I_w 4 mA): Sturm's sigmas and the layer step against the "
		                           "harness's own, worst %.1e px (tolerance 1e-12)",
		                           worst ) );
	}

	//The kernels: normalised, cut at ceil(6 sigma), and above sigma 1 the
	//discrete variance is sigma^2 + 1/12 (what --bloom leans on).
	{
		double worstSum = 0.0, worstVar = 0.0, worstTwelfth = 0.0;
		int wrongRadius = 0;
		for( const double sigma : { 0.05, 0.17, 0.5, 1.0, 1.7, 3.1, 7.9, 20.0 } )
		{
			const std::vector< double > w = pj::PixelGaussian( sigma, pj::kMaxRadius );
			const int r                   = static_cast< int >( w.size() / 2 );
			wrongRadius += r != static_cast< int >( std::ceil( 6.0 * sigma ) );
			double s0 = 0.0, s2 = 0.0;
			for( int i = -r; i <= r; ++i )
			{
				s0 += w[ static_cast< size_t >( i + r ) ];
				s2 += w[ static_cast< size_t >( i + r ) ] * i * i;
			}
			worstSum = worse( worstSum, std::fabs( s0 - 1.0 ) );
			worstVar = worse( worstVar, std::fabs( s2 - hDiscreteVar( sigma ) ) / hDiscreteVar( sigma ) );
			//The cut at 6 sigma takes 2e-9 of the light from 36 sigma^2 away
			//(7e-8 sigma^2); the sampling aliases below 2e-8 from sigma 1.
			if( sigma >= 1.0 )
				worstTwelfth = worse( worstTwelfth, std::fabs( hDiscreteVar( sigma ) - sigma * sigma - 1.0 / 12.0 ) / ( 1e-7 * ( sigma * sigma + 1.0 ) ) );
		}
		Check( worstSum < 1e-14 && worstVar < 1e-12 && wrongRadius == 0 && worstTwelfth <= 1.0,
		       fmt( "kernels: sums off 1 by %.1e, radius ceil(6 sigma) (%d wrong), discrete variance against the harness's %.1e relative; at sigma >= 1 the "
		            "discrete variance is sigma^2 + 1/12 to %.3f of 1e-7 (sigma^2 + 1): the 6-sigma cut and the sampling's aliasing",
		            worstSum, wrongRadius, worstVar, worstTwelfth ) );
	}

	//The conversions: the ranges the spec gives, the defaults it gives, and
	//each inverse a round trip.
	{
		double worst = 0.0;
		for( int i = 0; i <= 20; ++i )
		{
			const float v = i / 20.0f;
			worst         = worse( worst, std::fabs( ConvergenceFromParam( v ) - hConvergence( v ) ), std::fabs( ColdDriftFromParam( v ) - hColdDrift( v ) ),
                           std::fabs( WarmupFromParam( v ) / hTau( v ) - 1.0 ), std::fabs( FocusFromParam( v ) - hDelta( v ) ),
                           std::fabs( AstigmatismFromParam( v ) - hAstig( v ) ), std::fabs( BloomFromParam( v ) / hBloom( v ) - 1.0 ) );
			worst = worse( worst, std::fabs( ParamFromWarmup( WarmupFromParam( v ) ) - v ), std::fabs( ParamFromBloom( BloomFromParam( v ) ) - v ),
			               std::fabs( ParamFromConvergence( ConvergenceFromParam( v ) ) - v ), std::fabs( ParamFromColdDrift( ColdDriftFromParam( v ) ) - v ),
			               std::fabs( ParamFromFocus( FocusFromParam( v ) ) - v ), std::fabs( ParamFromAstigmatism( AstigmatismFromParam( v ) ) - v ) );
		}
		const bool zones = ZonesFromParam( 0.0f ) == 5 && ZonesFromParam( 1.0f ) == 9 && SetupFromParam( 0.0f ) == 1 && SetupFromParam( 7.4f ) == 7
		                   && SetupFromParam( 5000.0f ) == 1000 && SetupFromParam( std::numeric_limits< float >::quiet_NaN() ) == 1;
		const double defConv = ConvergenceFromParam( InfoOf( PT_CONVERGENCE ).defaultValue );
		const double defTau  = WarmupFromParam( InfoOf( PT_WARMUP ).defaultValue );
		const double defCold = ColdDriftFromParam( InfoOf( PT_COLD_DRIFT ).defaultValue );
		Check( worst < 1e-5 && zones && std::fabs( defConv - 0.001 ) < 1e-9 && std::fabs( defTau - 400.0 ) < 1e-3 && std::fabs( defCold - 0.001 ) < 1e-9,
		       fmt( "controls: every conversion against the spec's ranges and its inverse, worst %.1e (tolerance 1e-5, a float parameter); Zones 5x5/9x9, "
		            "Setup 1..1000 and NaN-safe: %s; defaults Convergence %.4f%% of the width, Warm-up %.2f s, Cold Drift %.4f%%, Bloom %.3f mA",
		            worst, zones ? "yes" : "NO", 100.0 * defConv, defTau, 100.0 * defCold, BloomFromParam( InfoOf( PT_BLOOM ).defaultValue ) ) );
	}
	return Verdict();
}

//===========================================================================
// --shift: a uniform zone field (the test seam), whole pixels and fractions
//===========================================================================
/// The crosshatch the uniform checks share: vertical lines at W (k + 1/2)/8,
/// horizontal at H (k + 1/2)/4. Vertical lines are measured in the rows
/// midway between the horizontal ones and the other way about, so no window
/// sees a crossing.
struct Hatch
{
	std::vector< double > xs, ys;
	std::vector< int > rows, columns;
};
Hatch hatchFor( int w, int h )
{
	Hatch g;
	for( int k = 0; k < 8; ++k )
		g.xs.push_back( w * ( k + 0.5 ) / 8.0 );
	for( int k = 0; k < 4; ++k )
		g.ys.push_back( h * ( k + 0.5 ) / 4.0 );
	for( int k = 1; k < 4; ++k )
		for( int d = -2; d <= 2; ++d )
			g.rows.push_back( h * k / 4 + d );
	for( int k = 1; k < 8; ++k )
		for( int d = -2; d <= 2; ++d )
			g.columns.push_back( w * k / 8 + d );
	return g;
}

/// The red and blue fringes off the hatch, the green line the reference:
/// the mean over every line and every row (column) measured, and the worst
/// departure of any one measurement from `want` (px: red x, red y, blue x,
/// blue y). The window follows `want`.
struct Fringe
{
	double mean[ 4 ] = {};
	double worst     = 0.0;
	double mad       = 0.0;
};
Fringe measureHatch( const Floats& out, int w, int h, const Hatch& g, const double want[ 4 ], double sigmaTotal )
{
	const std::vector< double > r = channel( out, 0 ), gr = channel( out, 1 ), b = channel( out, 2 );
	const double half             = windowHalf( sigmaTotal );
	Fringe f;
	int n[ 4 ] = {};
	for( int axis = 0; axis < 2; ++axis )
	{
		const bool vertical            = axis == 0;
		const std::vector< double >& at = vertical ? g.xs : g.ys;
		const std::vector< int >& along = vertical ? g.rows : g.columns;
		for( const double c : at )
			for( const int index : along )
			{
				const Centroid cg = centroidAlong( gr, w, h, vertical, index, c, half );
				const Centroid cr = centroidAlong( r, w, h, vertical, index, c + want[ axis ], half );
				const Centroid cb = centroidAlong( b, w, h, vertical, index, c + want[ 2 + axis ], half );
				const double fr = cr.at - cg.at, fb = cb.at - cg.at;
				f.mean[ axis ] += fr;
				f.mean[ 2 + axis ] += fb;
				++n[ axis ];
				++n[ 2 + axis ];
				f.worst = worse( f.worst, std::fabs( fr - want[ axis ] ), std::fabs( fb - want[ 2 + axis ] ) );
				f.mad   = std::max( f.mad, cg.mad );
			}
	}
	for( int c = 0; c < 4; ++c )
		f.mean[ c ] /= std::max( n[ c ], 1 );
	return f;
}

/// What a uniform shift's centroid may be off by: the sampling position
/// S = P - shift is rounded to a float at the pixel's coordinate (8 ulps of
/// the larger dimension, generously), and the decode, encode and the
/// bilinear's products are float (a few parts in 1e6 of a value, times the
/// line's mean absolute deviation).
double uniformTolerance( int w, int h, double mad )
{
	return 8.0 * ulpOf( std::max( w, h ) ) + 4e-6 * mad;
}

int runShift( const Perturb& perturb )
{
	std::printf( "\n=== shift: a uniform zone field set through the test seam. Whole pixels, on colour noise: red and blue are the unshifted red and\n"
	             "    blue translated, bit for bit, and black where the shift uncovers the frame's edge; the unshifted red is the green of the card\n"
	             "    with red and green swapped (the red tube shows the red signal, through green's spot). Fractions: the fringe off a hatch of\n"
	             "    soft lines, red and blue against green\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//Whole pixels, on colour noise: every pixel and every channel different.
		const Floats noise = noiseCard( w, h );
		Floats swapped     = noise;
		for( size_t p = 0; p < swapped.size(); p += 4 )
			std::swap( swapped[ p ], swapped[ p + 1 ] );
		Rig shifted, plain, swap;
		if( !startRig( shifted, w, h, perturb, &noise ) || !startRig( plain, w, h, perturb, &noise ) || !startRig( swap, w, h, perturb, &swapped ) )
			return 1;
		for( Rig* r : { &shifted, &plain, &swap } )
			r->plugin.HooksForTest().uniformZones = true;
		auto& hs        = shifted.plugin.HooksForTest();
		hs.redShift[ 0 ]  = 3.0;
		hs.redShift[ 1 ]  = -2.0;
		hs.blueShift[ 0 ] = -5.0;
		hs.blueShift[ 1 ] = 4.0;
		if( !shifted.Render( 2 ) || !plain.Render( 2 ) || !swap.Render( 2 ) )
			return 1;
		const Floats a = shifted.Output(), p = plain.Output(), s = swap.Output();
		size_t redDiffer = 0, blueDiffer = 0, compared = 0, greenDiffer = 0, plainRed = 0, uncovered = 0, lit = 0;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
			{
				const size_t at = static_cast< size_t >( y ) * w + x;
				greenDiffer += a[ 4 * at + 1 ] != p[ 4 * at + 1 ];
				plainRed += p[ 4 * at ] != s[ 4 * at + 1 ];
				//Where a shifted tube's raster does not reach, the screen is black:
				//those pixels sample past the frame's edge.
				const int xr = x - 3, yr = y + 2, xb = x + 5, yb = y - 4;
				const bool redOff = xr < 0 || yr >= h, blueOff = xb >= w || yb < 0;
				uncovered += redOff + blueOff;
				lit += ( redOff && a[ 4 * at ] != 0.0f ) + ( blueOff && a[ 4 * at + 2 ] != 0.0f );
				if( redOff || blueOff )
					continue;
				++compared;
				redDiffer += a[ 4 * at ] != p[ 4 * ( static_cast< size_t >( yr ) * w + xr ) ];
				blueDiffer += a[ 4 * at + 2 ] != p[ 4 * ( static_cast< size_t >( yb ) * w + xb ) + 2 ];
			}
		Check( redDiffer == 0 && blueDiffer == 0 && greenDiffer == 0 && plainRed == 0 && lit == 0,
		       fmt( "%4dx%-4d whole pixels on colour noise, red (3, -2), blue (-5, 4): of %zu pixels, red differs from the unshifted red translated at %zu, "
		            "blue from the unshifted blue translated at %zu, green moved at %zu; of %zu uncovered by the shift, %zu not black; the unshifted red "
		            "differs from the swapped card's green at %zu (all bit-exact)",
		            w, h, compared, redDiffer, blueDiffer, greenDiffer, uncovered, lit, plainRed ) );

		//Fractions, on the hatch.
		const Hatch g    = hatchFor( w, h );
		const Floats card = linesCard( w, h, g.xs, g.ys );
		Rig frac;
		if( !startRig( frac, w, h, perturb, &card ) )
			return 1;
		auto& hf          = frac.plugin.HooksForTest();
		hf.uniformZones   = true;
		const double want[ 4 ] = { 0.37, -0.81, -0.43, 0.29 };
		hf.redShift[ 0 ]  = want[ 0 ];
		hf.redShift[ 1 ]  = want[ 1 ];
		hf.blueShift[ 0 ] = want[ 2 ];
		hf.blueShift[ 1 ] = want[ 3 ];
		if( !frac.Render( 2 ) )
			return 1;
		const Fringe f         = measureHatch( frac.Output(), w, h, g, want, kLineSigma );
		const double tolerance = uniformTolerance( w, h, f.mad );
		Check( f.worst <= tolerance,
		       fmt( "%4dx%-4d fractions: red (%+.6f, %+.6f) px for (+0.37, -0.81), blue (%+.6f, %+.6f) for (-0.43, +0.29); worst single line off by "
		            "%.2e px, tolerance %.2e (8 ulps of %d, and 4e-6 of the line's %.2f px deviation)",
		            w, h, f.mean[ 0 ], f.mean[ 1 ], f.mean[ 2 ], f.mean[ 3 ], f.worst, tolerance, std::max( w, h ), f.mad ) );
	}
	return Verdict();
}

//===========================================================================
// --zones: the fringe against the installation's set errors
//===========================================================================
/// The harness's own zone field: bilinear between the points at the centres
/// of an n x n partition, constant beyond the outermost, from the plugin's
/// unit table (data) scaled by the harness's own RMS. Component c: red x,
/// red y, blue x, blue y. Also the field's x and y slopes.
struct ZoneField
{
	int n = 5, w = 0, h = 0;
	std::vector< double > table;//px
	double at( int c, double x, double y ) const
	{
		const double gx = std::clamp( x * n / w - 0.5, 0.0, n - 1.0 ), gy = std::clamp( y * n / h - 0.5, 0.0, n - 1.0 );
		const int i = std::min( static_cast< int >( gx ), n - 2 ), j = std::min( static_cast< int >( gy ), n - 2 );
		const double fx = gx - i, fy = gy - j;
		auto z          = [ & ]( int a, int b ) { return table[ ( static_cast< size_t >( b ) * n + a ) * 4 + c ]; };
		return ( z( i, j ) * ( 1 - fx ) + z( i + 1, j ) * fx ) * ( 1 - fy ) + ( z( i, j + 1 ) * ( 1 - fx ) + z( i + 1, j + 1 ) * fx ) * fy;
	}
	/// The slope of component c along x (vertical = true) or y, just left
	/// (side -1) or right (+1) of position s on the line at the other
	/// coordinate `o`.
	double slope( int c, bool alongX, double s, double o, int side ) const
	{
		const double d = 1e-4;
		const double a = alongX ? at( c, s + side * d, o ) : at( c, o, s + side * d );
		const double b = alongX ? at( c, s + side * 2 * d, o ) : at( c, o, s + side * 2 * d );
		return ( b - a ) / ( side * d );
	}
};

int runZones( const Perturb& perturb )
{
	std::printf( "\n=== zones: Convergence 0.5%% of the width, Setups 7 and 11, 5x5 and 9x9. Every zone point and every cell centre: red and blue lines\n"
	             "    against the harness's bilinear field from the set errors (the value at a point is that point's error; at a cell centre, the mean\n"
	             "    of its corners). Tolerance per line: float, the bilinear resampler's centroid bias where the field has slope g (|g|(1 + MAD)),\n"
	             "    and at a zone point the kink |dg| x MAD\n" );
	const float convergence = static_cast< float >( 0.005 / 0.015 );
	for( const Raster& raster : kRasters )
		for( const int n : { 5, 9 } )
			for( const int setup : { 7, 11 } )
			{
				const int w = raster.w, h = raster.h;
				ZoneField field;
				field.n            = n;
				field.w            = w;
				field.h            = h;
				const double rms   = hConvergence( convergence ) * w;
				field.table        = projector::ZoneTable( setup, n );
				for( double& v : field.table )
					v *= rms;
				double worstRatio = 0.0, worstErr = 0.0, worstTol = 0.0, biggest = 0.0;
				int measured = 0, skipped = 0;
				Rig rig;
				if( !startRig( rig, w, h, perturb ) )
					return 1;
				rig.Set( PT_CONVERGENCE, convergence );
				rig.Set( PT_ZONES, n == 5 ? 0 : 1 );
				rig.Set( PT_SETUP, static_cast< float >( setup ) );
				//Lines at the points and at the cell centres, every other one
				//per card so no window reaches a neighbour.
				for( int kind = 0; kind < 2; ++kind )//0 points, 1 centres
					for( int parity = 0; parity < 2; ++parity )
						for( int axis = 0; axis < 2; ++axis )//0 vertical lines (x fringes), 1 horizontal
						{
							const bool vertical = axis == 0;
							const int across    = vertical ? w : h;//the line's position runs along this
							const int along     = vertical ? h : w;
							std::vector< double > lines, at;
							for( int i = 0; i < ( kind == 0 ? n : n - 1 ); ++i )
								if( i % 2 == parity )
									lines.push_back( across * ( i + 0.5 + 0.5 * kind ) / n );
							for( int j = 0; j < ( kind == 0 ? n : n - 1 ); ++j )
								at.push_back( along * ( j + 0.5 + 0.5 * kind ) / n );
							const Floats card = vertical ? linesCard( w, h, lines, {} ) : linesCard( w, h, {}, lines );
							rig.Upload( card );
							if( !rig.Render( 1 ) )
								return 1;
							const Floats out              = rig.Output();
							const std::vector< double > g = channel( out, 1 );
							for( int tube = 0; tube < 2; ++tube )
							{
								const std::vector< double > ch = channel( out, tube == 0 ? 0 : 2 );
								const int c                    = tube * 2 + axis;
								for( const double x0 : lines )
									for( const double o : at )
										//The two pixel rows (columns) whose centres straddle o.
										for( const int index : { static_cast< int >( std::floor( o - 0.5 ) ), static_cast< int >( std::floor( o - 0.5 ) ) + 1 } )
										{
											if( index < 0 || index >= along )
												continue;
											const double oc = index + 0.5;
											//The red line's screen position: P - z(P) = x0.
											double P = x0;
											for( int it = 0; it < 60; ++it )
												P = x0 + ( vertical ? field.at( c, P, oc ) : field.at( c, oc, P ) );
											const double fringe = P - x0;
											const double half   = windowHalf( kLineSigma );
											const Centroid cg   = centroidAlong( g, w, h, vertical, index, x0, half );
											const Centroid cr   = centroidAlong( ch, w, h, vertical, index, P, half );
											if( cg.clipped || cr.clipped )
											{
												++skipped;
												continue;
											}
											const double got    = cr.at - cg.at;
											//The slopes either side of the line's position.
											const double gl = field.slope( c, vertical, P, oc, -1 ), gr = field.slope( c, vertical, P, oc, +1 );
											const double gm = std::max( std::fabs( gl ), std::fabs( gr ) );
											//A kink within the line's reach: the slope's change over the window.
											const double kl = field.slope( c, vertical, P - half, oc, -1 ), kr = field.slope( c, vertical, P + half, oc, +1 );
											const double dg = std::max( { std::fabs( kr - kl ), std::fabs( gr - gl ) } );
											const double gmax = std::max( { gm, std::fabs( kl ), std::fabs( kr ) } );
											const double mad  = cg.mad / ( 1.0 - gmax );
											const double tol  = uniformTolerance( w, h, mad ) + 4.0 * ulpOf( fringe ) + gmax * ( 1.0 + mad ) / ( 1.0 - gmax )
											                   + dg * mad / ( ( 1.0 - gmax ) * ( 1.0 - gmax ) );
											const double err  = std::fabs( got - fringe );
											if( err / tol > worstRatio || err != err )
											{
												worstRatio = err != err ? err : err / tol;
												worstErr   = err;
												worstTol   = tol;
											}
											biggest = std::max( biggest, std::fabs( fringe ) );
											++measured;
										}
							}
						}
				Check( worstRatio <= 1.0 && measured > 0,
				       fmt( "%4dx%-4d %dx%d Setup %2d: %d line measurements (fringes to %.2f px; %d skipped, the line's window off the frame); worst error / "
				            "tolerance %.3f (%.4f px against %.4f)",
				            w, h, n, n, setup, measured, biggest, skipped, worstRatio, worstErr, worstTol ) );
			}
	return Verdict();
}

//===========================================================================
// --keystone: the side tubes' uncorrected homography
//===========================================================================
int runKeystone( const Perturb& perturb )
{
	std::printf( "\n=== keystone: Keystone 1 and 0.5, everything else converged. Vertical lines' red and blue positions against the harness's own\n"
	             "    solution of P + k (H^-1(P) - P) = line; horizontal lines' heights at both ends (the trapezoid); red and blue opposite\n" );
	const double e = hE();
	for( const Raster& raster : kRasters )
		for( const float k : { 1.0f, 0.5f } )
		{
			const int w = raster.w, h = raster.h;
			const double cx = 0.5 * w, cy = 0.5 * h;
			Rig rig;
			if( !startRig( rig, w, h, perturb ) )
				return 1;
			rig.Set( PT_KEYSTONE, k );
			//The sampling map's displacement for a tube of coefficient ee, px.
			auto dx = [ & ]( double P, double ee ) {
				const double U = ( P - cx ) / cx;
				return k * cx * ee * U * U / ( 1.0 - ee * U );
			};
			double worstRatio = 0.0, worstErr = 0.0, worstTol = 0.0, edge[ 2 ] = {};
			int measured      = 0;
			for( int axis = 0; axis < 2; ++axis )
			{
				const bool vertical = axis == 0;
				std::vector< double > lines;
				if( vertical )
					for( int i = 1; i < 12; ++i )
						lines.push_back( w * i / 12.0 );
				else
					for( int i = 1; i < 6; ++i )
						lines.push_back( h * i / 6.0 );
				const Floats card = vertical ? linesCard( w, h, lines, {} ) : linesCard( w, h, {}, lines );
				rig.Upload( card );
				if( !rig.Render( 1 ) )
					return 1;
				const Floats out              = rig.Output();
				const std::vector< double > g = channel( out, 1 );
				for( int tube = 0; tube < 2; ++tube )
				{
					const double ee                = tube == 0 ? e : -e;
					const std::vector< double > ch = channel( out, tube == 0 ? 0 : 2 );
					for( const double l : lines )
					{
						std::vector< int > along;
						if( vertical )
							along = { h / 2 - 20 * h / 180, h / 2, h / 2 + 20 * h / 180 };
						else
							along = { w / 16, w / 8, w - 1 - w / 8, w - 1 - w / 16 };
						for( const int index : along )
						{
							double P = l, g1 = 0.0, curv = 0.0;
							if( vertical )
							{
								//Newton on P + dx(P) = l.
								for( int it = 0; it < 50; ++it )
								{
									const double hh = 1e-4;
									const double f  = P + dx( P, ee ) - l;
									const double fp = 1.0 + ( dx( P + hh, ee ) - dx( P - hh, ee ) ) / ( 2.0 * hh );
									P -= f / fp;
								}
								const double hh = 1e-2;
								g1              = -( dx( P + hh, ee ) - dx( P - hh, ee ) ) / ( 2.0 * hh );
								//x''(S) of the inverse map: -S''/S'^3.
								const double s2 = ( dx( P + hh, ee ) - 2.0 * dx( P, ee ) + dx( P - hh, ee ) ) / ( hh * hh );
								curv            = std::fabs( s2 ) / std::pow( 1.0 - g1, 3.0 );
							}
							else
							{
								//S_y - cy = (P_y - cy)(1 + k e U / (1 - e U)), U at this column.
								const double U = ( index + 0.5 - cx ) / cx;
								const double m = 1.0 + k * ee * U / ( 1.0 - ee * U );
								P              = cy + ( l - cy ) / m;
								g1             = 1.0 - m;
							}
							const double half = windowHalf( kLineSigma ) / ( 1.0 - std::fabs( g1 ) );
							const Centroid cg = centroidAlong( g, w, h, vertical, index, l, half );
							const Centroid cr = centroidAlong( ch, w, h, vertical, index, P, half );
							const double got  = cr.at - cg.at;
							const double want = P - l;
							const double gm   = std::fabs( g1 );
							const double mad  = cg.mad / ( 1.0 - gm );
							const double var  = cg.mad * cg.mad * hPi / 2.0;//a Gaussian's variance from its MAD
							const double tol  = uniformTolerance( w, h, mad ) + 4.0 * ulpOf( want ) + gm * ( 1.0 + mad ) / ( 1.0 - gm ) + 2.0 * curv * var;
							const double err  = std::fabs( got - want );
							if( err / tol > worstRatio || err != err )
							{
								worstRatio = err != err ? err : err / tol;
								worstErr   = err;
								worstTol   = tol;
							}
							if( vertical && l == lines.back() )
								edge[ tube ] = got;
							++measured;
						}
					}
				}
			}
			Check( worstRatio <= 1.0 && edge[ 0 ] * edge[ 1 ] < 0.0,
			       fmt( "%4dx%-4d Keystone %.1f: %d line measurements; worst error / tolerance %.3f (%.4f px against %.4f); the rightmost line's red %+.3f px, "
			            "blue %+.3f (opposite sides)",
			            w, h, k, measured, worstRatio, worstErr, worstTol, edge[ 0 ], edge[ 1 ] ) );
		}
	return Verdict();
}

//===========================================================================
// --warmup: the drift against D e^(-t/tau), at three frame rates
//===========================================================================
int runWarmup( const Perturb& perturb )
{
	std::printf( "\n=== warmup: Cold Drift 0.5%% of the width, tau 2 s, Setup 1, at 30, 60 and 240 fps. The fringe (a uniform shift) at 0 to 4 s\n"
	             "    against D e^(-t/tau) in the Setup's direction; tau fitted on ln|offset|; the same drift at the times all three rates share;\n"
	             "    Power On at 3 s starts it again\n" );
	const float coldParam = 0.5f, tauParam = warmupParam( 2.0 );
	const double tau      = hTau( tauParam );
	const double cold     = hColdDrift( coldParam );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Hatch g     = hatchFor( w, h );
		const Floats card = linesCard( w, h, g.xs, g.ys );
		double dir[ 2 ][ 2 ];
		projector::DriftDirection( 1, 0, dir[ 0 ] );
		projector::DriftDirection( 1, 1, dir[ 1 ] );
		const double rates[ 3 ] = { 30.0, 60.0, 240.0 };
		std::vector< std::array< double, 4 > > at[ 3 ];//per rate, per half second
		double worstRatio = 0.0, worstErr = 0.0, worstTol = 0.0;
		double fittedTau[ 3 ] = {}, fitTol[ 3 ] = {};
		for( int r = 0; r < 3; ++r )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card, rates[ r ] ) )
				return 1;
			rig.Set( PT_COLD_DRIFT, coldParam );
			rig.Set( PT_WARMUP, tauParam );
			const int step = static_cast< int >( rates[ r ] / 2.0 );
			std::vector< double > ts, lns, sig;
			for( int f = 0; f <= 4 * static_cast< int >( rates[ r ] ); ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				if( f % step != 0 )
					continue;
				const double t  = f / rates[ r ];
				const double s  = std::exp( -t / tau );
				const double D  = cold * w * s;
				const double want[ 4 ] = { D * dir[ 0 ][ 0 ], D * dir[ 0 ][ 1 ], D * dir[ 1 ][ 0 ], D * dir[ 1 ][ 1 ] };
				const Fringe fr = measureHatch( rig.Output(), w, h, g, want, kLineSigma );
				//The microsecond rounding of every frame's dt: up to 0.5 us each.
				const double tol = uniformTolerance( w, h, fr.mad ) + D * ( f * 0.5e-6 / tau + 1e-6 );
				if( fr.worst / tol > worstRatio || fr.worst != fr.worst )
				{
					worstRatio = fr.worst != fr.worst ? fr.worst : fr.worst / tol;
					worstErr   = fr.worst;
					worstTol   = tol;
				}
				at[ r ].push_back( { fr.mean[ 0 ], fr.mean[ 1 ], fr.mean[ 2 ], fr.mean[ 3 ] } );
				const double mag = std::hypot( fr.mean[ 0 ], fr.mean[ 1 ] );
				ts.push_back( t );
				lns.push_back( std::log( mag ) );
				sig.push_back( tol / mag );
			}
			//Least squares of ln|offset| on t: the slope is -1/tau. Its error
			//bound: each point's ln error at most tol/|offset|, through the
			//slope's linear weights.
			double st = 0.0, sl = 0.0;
			for( size_t i = 0; i < ts.size(); ++i )
			{
				st += ts[ i ];
				sl += lns[ i ];
			}
			st /= ts.size();
			sl /= ts.size();
			double stt = 0.0, stl = 0.0;
			for( size_t i = 0; i < ts.size(); ++i )
			{
				stt += ( ts[ i ] - st ) * ( ts[ i ] - st );
				stl += ( ts[ i ] - st ) * ( lns[ i ] - sl );
			}
			const double slope = stl / stt;
			double slopeErr    = 0.0;
			for( size_t i = 0; i < ts.size(); ++i )
				slopeErr += std::fabs( ts[ i ] - st ) / stt * sig[ i ];
			fittedTau[ r ] = -1.0 / slope;
			fitTol[ r ]    = tau * tau * slopeErr;
		}
		//The times all three share: the same offset at every rate.
		double worstAcross = 0.0, acrossTol = 0.0;
		for( size_t i = 0; i < at[ 0 ].size(); ++i )
			for( int c = 0; c < 4; ++c )
			{
				const double a = at[ 0 ][ i ][ c ], b = at[ 1 ][ i ][ c ], d = at[ 2 ][ i ][ c ];
				worstAcross    = worse( worstAcross, std::fabs( a - b ), std::fabs( a - d ), std::fabs( b - d ) );
			}
		{
			const double D = cold * w;
			acrossTol      = 2.0 * uniformTolerance( w, h, 1.3 ) + D * ( 4.0 * 240.0 * 0.5e-6 / tau + 4.0 * 30.0 * 0.5e-6 / tau + 2e-6 );
		}
		bool fits = true;
		for( int r = 0; r < 3; ++r )
			fits = fits && std::fabs( fittedTau[ r ] - tau ) <= fitTol[ r ];
		Check( worstRatio <= 1.0 && fits && worstAcross <= acrossTol,
		       fmt( "%4dx%-4d at 0..4 s every half second: worst offset error / tolerance %.3f (%.2e px against %.2e); tau fitted %.6f, %.6f, %.6f s at "
		            "30, 60, 240 fps (%.6f; tolerance +-%.1e); the rates agree to %.2e px (tolerance %.2e)",
		            w, h, worstRatio, worstErr, worstTol, fittedTau[ 0 ], fittedTau[ 1 ], fittedTau[ 2 ], tau, std::max( { fitTol[ 0 ], fitTol[ 1 ], fitTol[ 2 ] } ),
		            worstAcross, acrossTol ) );

		//Power On at 3 s.
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card, 60.0 ) )
			return 1;
		rig.Set( PT_COLD_DRIFT, coldParam );
		rig.Set( PT_WARMUP, tauParam );
		rig.beforeFrame = [ & ]( int f ) {
			if( f == 180 )
				rig.Set( PT_POWER_ON, 1.0f );
			if( f == 181 )
				rig.Set( PT_POWER_ON, 0.0f );
		};
		double worstPower = 0.0, powerTol = 0.0;
		for( int f = 0; f <= 240; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			if( f != 120 && f != 180 && f != 210 && f != 240 )
				continue;
			const double t  = f < 180 ? f / 60.0 : ( f - 180 ) / 60.0;
			const double D  = cold * w * std::exp( -t / tau );
			const double want[ 4 ] = { D * dir[ 0 ][ 0 ], D * dir[ 0 ][ 1 ], D * dir[ 1 ][ 0 ], D * dir[ 1 ][ 1 ] };
			const Fringe fr = measureHatch( rig.Output(), w, h, g, want, kLineSigma );
			const double tol = uniformTolerance( w, h, fr.mad ) + D * ( f * 0.5e-6 / tau + 1e-6 );
			worstPower       = worse( worstPower, fr.worst / tol );
			powerTol         = tol;
		}
		Check( worstPower <= 1.0, fmt( "%4dx%-4d Power On pressed at 3 s: the drift at 2, 3, 3.5 and 4 s against D e^(-t/tau) from the press, worst error / "
		                               "tolerance %.3f (tolerance about %.1e px)",
		                               w, h, worstPower, powerTol ) );
	}
	return Verdict();
}

//===========================================================================
// --focus: each tube's spot against Sturm's interval
//===========================================================================
/// A white dot every `spacing` px, away from the edges; their centres.
Floats dotsCard( int w, int h, int spacing, std::vector< std::pair< int, int > >& dots, float r = 1.0f, float g = 1.0f, float b = 1.0f )
{
	Floats card = flatCard( w, h, 0.0f, 0.0f, 0.0f, 1.0f );
	dots.clear();
	for( int y = spacing / 2; y + spacing / 2 < h; y += spacing )
		for( int x = spacing / 2; x + spacing / 2 < w; x += spacing )
		{
			float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			o[ 0 ]   = r;
			o[ 1 ]   = g;
			o[ 2 ]   = b;
			dots.emplace_back( x, y );
		}
	return card;
}

/// A dot's light in one channel, within +-half of (x, y): its sum and its
/// variances along x and y.
struct Spread
{
	double sum = 0.0, vx = 0.0, vy = 0.0;
};
Spread spreadOf( const std::vector< double >& ch, int w, int h, int x, int y, int half )
{
	Spread s;
	double mx = 0.0, my = 0.0;
	for( int j = std::max( 0, y - half ); j <= std::min( h - 1, y + half ); ++j )
		for( int i = std::max( 0, x - half ); i <= std::min( w - 1, x + half ); ++i )
		{
			const double v = ch[ static_cast< size_t >( j ) * w + i ];
			s.sum += v;
			mx += v * ( i - x );
			my += v * ( j - y );
		}
	mx /= s.sum;
	my /= s.sum;
	for( int j = std::max( 0, y - half ); j <= std::min( h - 1, y + half ); ++j )
		for( int i = std::max( 0, x - half ); i <= std::min( w - 1, x + half ); ++i )
		{
			const double v = ch[ static_cast< size_t >( j ) * w + i ];
			s.vx += v * ( i - x - mx ) * ( i - x - mx );
			s.vy += v * ( j - y - my ) * ( j - y - my );
		}
	s.vx /= s.sum;
	s.vy /= s.sum;
	return s;
}

int runFocus( const Perturb& perturb )
{
	std::printf( "\n=== focus: isolated white dots. Each tube's x and y variance against the discrete variance of a pixel-integrated Gaussian of\n"
	             "    sigma^2 = sigma_beam^2 + (S (Delta +- a))^2 (red and green at their 1 mA spot, blue at its full-white current); tubes independent\n" );
	struct Setting
	{
		float red, green, blue, astig;
	};
	const Setting settings[] = {
		{ 0.5f, 0.5f, 0.5f, 0.0f }, { 0.3f, 0.6f, 0.8f, 0.0f }, { 0.45f, 0.55f, 0.5f, 0.6f }, { 0.2f, 0.5f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.15f, 1.0f }, { 0.7f, 0.35f, 0.6f, 0.3f },
	};
	const float bloomParam = ParamFromBloom( 2.0 );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		double worstRel = 0.0;
		int measured    = 0;
		for( const Setting& s : settings )
		{
			const float tubeParam[ 3 ] = { s.red, s.green, s.blue };
			double sx[ 3 ], sy[ 3 ];
			const double a  = hAstig( s.astig );
			const double vw = std::pow( hSpotSigmaPx( hBloom( bloomParam ), h, hP() ), 2.0 );
			for( int t = 0; t < 3; ++t )
			{
				const double d    = hDelta( tubeParam[ t ] );
				const double beam = t == 2 ? std::sqrt( vw ) : hSigma1Px( h );
				sx[ t ]           = std::sqrt( beam * beam + std::pow( hFocusS * h * ( d + a ), 2.0 ) );
				sy[ t ]           = std::sqrt( beam * beam + std::pow( hFocusS * h * ( d - a ), 2.0 ) );
			}
			double biggest = 0.0;
			for( int t = 0; t < 3; ++t )
				biggest = std::max( { biggest, sx[ t ], sy[ t ] } );
			const int half    = static_cast< int >( std::ceil( 6.0 * biggest ) ) + 2;
			const int spacing = 2 * half + 2;
			std::vector< std::pair< int, int > > dots;
			const Floats card = dotsCard( w, h, spacing, dots );
			if( dots.empty() )
				continue;
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			rig.Set( PT_RED_FOCUS, s.red );
			rig.Set( PT_GREEN_FOCUS, s.green );
			rig.Set( PT_BLUE_FOCUS, s.blue );
			rig.Set( PT_ASTIGMATISM, s.astig );
			rig.Set( PT_BLOOM, bloomParam );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats out = rig.Output();
			for( int t = 0; t < 3; ++t )
			{
				const std::vector< double > ch = channel( out, t );
				const double wantX = hDiscreteVar( sx[ t ] ), wantY = hDiscreteVar( sy[ t ] );
				for( size_t k = 0; k < std::min< size_t >( dots.size(), 6 ); ++k )
				{
					const Spread sp = spreadOf( ch, w, h, dots[ k ].first, dots[ k ].second, half );
					//Float: 1e-4 of the variance, or 1e-7 px^2 where the spot is
					//a fraction of a pixel and its tails are a few ulps.
					worstRel = worse( worstRel, std::fabs( sp.vx - wantX ) / std::max( 1e-4 * wantX, 1e-7 ),
					                  std::fabs( sp.vy - wantY ) / std::max( 1e-4 * wantY, 1e-7 ) );
					++measured;
				}
			}
		}
		Check( worstRel <= 1.0, fmt( "%4dx%-4d %zu settings of the three Focus controls and Astigmatism, %d dots measured: worst variance error / tolerance %.3f "
		                             "(tolerance 1e-4 of the variance, at least 1e-7 px^2)",
		                             w, h, sizeof( settings ) / sizeof( settings[ 0 ] ), measured, worstRel ) );
	}
	return Verdict();
}

//===========================================================================
// --bloom: the blue gun's current and its spot
//===========================================================================
int runBloom( const Perturb& perturb )
{
	std::printf( "\n=== bloom: grey dots at blue drives 0.05 to 1, Bloom 0.5, 2 and 8 mA, Blue Focus defocused so every sigma >= 1 px. The plugin's\n"
	             "    current against I(d) for the d it decoded; the blue light's sum against d; the blue variance's excess over the focus terms\n"
	             "    against sigma(I(d))^2; red and green unchanged by Bloom, bit for bit\n" );
	const double drives[] = { 0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 0.85, 1.0 };
	const float blueFocus = 0.8f;//Delta 0.6: S 0.6 = 1.08 px at 180
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double focusSigma = hFocusS * h * hDelta( blueFocus );
		const int half          = static_cast< int >( std::ceil( 6.0 * std::sqrt( focusSigma * focusSigma + std::pow( hSpotSigmaPx( 8.0, h, hP() ), 2.0 ) ) ) ) + 2;
		const int spacing       = 2 * half + 2;
		//One dot per drive, in a row (repeated in further rows as room allows).
		Floats card = flatCard( w, h, 0.0f, 0.0f, 0.0f, 1.0f );
		std::vector< std::pair< int, int > > dots;
		std::vector< int > which;
		int k = 0;
		for( int y = spacing / 2; y + spacing / 2 < h; y += spacing )
			for( int x = spacing / 2; x + spacing / 2 < w; x += spacing, ++k )
			{
				const int i     = k % 8;
				const float c   = static_cast< float >( codeOf( drives[ i ] ) );
				float* o        = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = c;
				dots.emplace_back( x, y );
				which.push_back( i );
			}
		double worstCurrent = 0.0, worstSum = 0.0, worstVar = 0.0, biggestExcess = 0.0, worstDrive = 0.0;
		Floats firstOut;
		size_t rgDiffer = 0;
		for( const double bloom : { 0.5, 2.0, 8.0 } )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			const float bp = ParamFromBloom( bloom );
			rig.Set( PT_BLOOM, bp );
			rig.Set( PT_BLUE_FOCUS, blueFocus );
			if( !rig.Render( 1 ) )
				return 1;
			const double iw                = hBloom( bp );
			const Floats out               = rig.Output();
			const Floats tubes             = rig.Tubes();
			const std::vector< double > ch = channel( out, 2 );
			for( size_t n = 0; n < dots.size(); ++n )
			{
				const size_t p   = static_cast< size_t >( dots[ n ].second ) * w + dots[ n ].first;
				const double d   = tubes[ 4 * p + 3 ];//the drive the plugin decoded
				const double cur = tubes[ 4 * p + 2 ];
				//...against the drive the harness encoded: the float code's rounding
				//and the shader's pow, 16 float ulps times the decode's condition
				//number d ln L / d ln c <= 2.4.
				worstDrive = worse( worstDrive, std::fabs( d / drives[ which[ n ] ] - 1.0 ) / ( 16.0 * 6e-8 * 2.4 ) );
				const double I   = hCurrent( d, iw );
				//The current's float evaluation: a few ulps, times the formula's
				//condition number d ln I / d ln d = 1 + I_w d / (I_s + I_w (1 - d)).
				const double kappa = 1.0 + iw * d / ( hSaturation() + iw * ( 1.0 - d ) );
				worstCurrent       = worse( worstCurrent, std::fabs( cur / I - 1.0 ) / ( 8.0 * 6e-8 * kappa ) );
				const Spread sp    = spreadOf( ch, w, h, dots[ n ].first, dots[ n ].second, half );
				worstSum           = worse( worstSum, std::fabs( sp.sum / d - 1.0 ) / 1e-5 );
				const double v     = std::pow( hSpotSigmaPx( I, h, hP() ), 2.0 );
				const double total = focusSigma * focusSigma + v + 1.0 / 12.0;
				//1e-4 of the variance (float sums of the dot's light, as --focus),
				//and the discrete variance's departure from sigma^2 + 1/12 (--model:
				//1e-7 (sigma^2 + 1)).
				const double tol = 1e-4 * total + 1e-7 * ( total + 1.0 );
				worstVar         = worse( worstVar, std::fabs( sp.vx - total ) / tol, std::fabs( sp.vy - total ) / tol );
				biggestExcess    = std::max( biggestExcess, v );
			}
			if( firstOut.empty() )
				firstOut = out;
			else
				for( size_t p = 0; p < out.size(); p += 4 )
					rgDiffer += ( out[ p ] != firstOut[ p ] ) + ( out[ p + 1 ] != firstOut[ p + 1 ] );
		}
		Check( worstDrive <= 1.0 && worstCurrent <= 1.0 && worstSum <= 1.0 && worstVar <= 1.0 && rgDiffer == 0,
		       fmt( "%4dx%-4d %zu dots at three Bloom settings: the decoded drive against the harness's, error / tolerance %.3f (16 float ulps x 2.4); "
		            "current error / tolerance %.3f (8 float ulps x the condition number); blue light's sum / d "
		            "error / 1e-5 %.3f; blue variance error / tolerance %.3f (excess up to %.4f px^2; tolerance 1e-4 of the variance + 1e-7 (it + 1)); red and "
		            "green values changed by Bloom: %zu (bit-exact)",
		            w, h, dots.size(), worstDrive, worstCurrent, worstSum, worstVar, biggestExcess, rgDiffer ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: output alpha = mix( a, 1, Mix ); white under alpha 0 changes nothing (at Mix 1 and 0.5); Mix 0 hands the clip through;\n"
	             "    Mix 0.5 mixes premultiplied, (clip a (1 - m) + projected m) / alpha, against the Mix 1 and Mix 0 renders; a flat colour under\n"
	             "    alpha 0.25, 0.5, 0.75 and 1 drives the guns with a times its light\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//Left half: white (or black) at alpha 0. Right half: the look card
		//under an alpha ramp in 8-bit steps.
		const Floats look = buildCard( w, h );
		Floats white = look, black = look;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
			{
				const size_t p = ( static_cast< size_t >( y ) * w + x ) * 4;
				if( x < w / 2 )
				{
					white[ p ] = white[ p + 1 ] = white[ p + 2 ] = 1.0f;
					black[ p ] = black[ p + 1 ] = black[ p + 2 ] = 0.0f;
					white[ p + 3 ] = black[ p + 3 ] = 0.0f;
				}
				else
					white[ p + 3 ] = black[ p + 3 ] = std::round( 255.0f * ( 2.0f * x / w - 1.0f ) ) / 255.0f;
			}
		Rig rw, rb;
		if( !startRig( rw, w, h, perturb, &white ) || !startRig( rb, w, h, perturb, &black ) )
			return 1;
		for( Rig* r : { &rw, &rb } )
		{
			r->Set( PT_CONVERGENCE, 0.2f );
			r->Set( PT_COLD_DRIFT, 0.3f );
			r->Set( PT_BLUE_FOCUS, 0.7f );
			r->Set( PT_BLOOM, ParamFromBloom( 4.0 ) );
		}
		double worst[ 2 ]      = {};
		size_t shows[ 2 ]      = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		for( int k = 0; k < 2; ++k )
		{
			for( Rig* r : { &rw, &rb } )
			{
				r->Set( PT_MIX, mixes[ k ] );
				if( !r->Render( 2 ) )
					return 1;
			}
			const Floats ow = rw.Output(), ob = rb.Output();
			for( size_t p = 0; p < ow.size() / 4; ++p )
			{
				const double a = white[ 4 * p + 3 ];
				worst[ k ]     = worse( worst[ k ], std::fabs( ow[ 4 * p + 3 ] - ( a + ( 1.0 - a ) * mixes[ k ] ) ) );
			}
			shows[ k ] = differing( ow, ob );
		}
		rw.Set( PT_MIX, 0.0f );
		if( !rw.Render( 1 ) )
			return 1;
		const size_t through = differing( rw.Output(), white );
		//Mix 0.5's colour from the clip and the Mix 1 picture (opaque), in code
		//values: a few float operations on numbers <= 1. Two fresh instances
		//rendered to the same instant, so the warm-up drift is the same in both.
		Floats mixed[ 2 ];
		for( int k = 0; k < 2; ++k )
		{
			Rig rm;
			if( !startRig( rm, w, h, perturb, &white ) )
				return 1;
			rm.Set( PT_CONVERGENCE, 0.2f );
			rm.Set( PT_COLD_DRIFT, 0.3f );
			rm.Set( PT_BLUE_FOCUS, 0.7f );
			rm.Set( PT_BLOOM, ParamFromBloom( 4.0 ) );
			rm.Set( PT_MIX, mixes[ k ] );
			if( !rm.Render( 2 ) )
				return 1;
			mixed[ k ] = rm.Output();
		}
		double worstMix = 0.0;
		for( size_t p = 0; p < white.size() / 4; ++p )
		{
			const double a = white[ 4 * p + 3 ];
			for( int c = 0; c < 3; ++c )
			{
				const double want = ( white[ 4 * p + c ] * a * 0.5 + mixed[ 0 ][ 4 * p + c ] * 0.5 ) / ( a * 0.5 + 0.5 );
				worstMix          = worse( worstMix, std::fabs( mixed[ 1 ][ 4 * p + c ] - want ) );
			}
		}
		Check( worst[ 0 ] <= 1e-6 && worst[ 1 ] <= 1e-6 && shows[ 0 ] == 0 && shows[ 1 ] == 0 && through == 0 && worstMix <= 1e-6,
		       fmt( "%4dx%-4d alpha off mix( a, 1, Mix ) by %.2g at Mix 1 and %.2g at Mix 0.5 (tolerance 1e-6); white rather than black under alpha 0 "
		            "changes %zu values at Mix 1 and %zu at Mix 0.5 (bit-exact); Mix 0: %zu values differ from the clip (bit-exact); Mix 0.5's colour off "
		            "the premultiplied mix by %.2g (tolerance 1e-6)",
		            w, h, worst[ 0 ], worst[ 1 ], shows[ 0 ], shows[ 1 ], through, worstMix ) );

		//A flat colour in four bands of alpha, the set converged and focused:
		//inside a band, each channel's light is a times the colour's (straight
		//alpha over black). Bands of a quarter of the width; 8 px either side
		//of a boundary left out (the spots are under a pixel here).
		const double code[ 3 ]  = { 0.8, 0.5, 0.3 };
		const double alphas[ 4 ] = { 0.25, 0.5, 0.75, 1.0 };
		Floats bands             = flatCard( w, h, static_cast< float >( code[ 0 ] ), static_cast< float >( code[ 1 ] ), static_cast< float >( code[ 2 ] ) );
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
				bands[ ( static_cast< size_t >( y ) * w + x ) * 4 + 3 ] = static_cast< float >( alphas[ std::min( 3, 4 * x / w ) ] );
		Rig rf;
		if( !startRig( rf, w, h, perturb, &bands ) || !rf.Render( 1 ) )
			return 1;
		const Floats of   = rf.Output();
		double worstLight = 0.0;
		int inside        = 0;
		for( int y = 8; y < h - 8; ++y )
			for( int x = 0; x < w; ++x )
			{
				const int band = std::min( 3, 4 * x / w );
				if( x < band * w / 4 + 8 || x >= ( band + 1 ) * w / 4 - 8 )
					continue;
				++inside;
				const size_t p = static_cast< size_t >( y ) * w + x;
				for( int c = 0; c < 3; ++c )
				{
					const double want = alphas[ band ] * linearOf( static_cast< float >( code[ c ] ) );
					worstLight        = worse( worstLight, std::fabs( linearOf( of[ 4 * p + c ] ) / want - 1.0 ) );
				}
			}
		//The decode, the blur's float sum and the encode, a few ulps each
		//through the transfer's condition number 2.4; and the 8-bit-free
		//float readback decoded in double.
		Check( worstLight <= 1e-5, fmt( "%4dx%-4d a flat colour at alpha 0.25 / 0.5 / 0.75 / 1, %d pixels inside the bands: each channel's light off a x "
		                                "its own by %.2e of it (tolerance 1e-5: decode, blur and encode in float, a few ulps each times 2.4)",
		                                w, h, inside, worstLight ) );
	}
	return Verdict();
}

//===========================================================================
// --resize
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: 1280x720 -> 320x180 a second into a warm-up (Cold Drift 0.5%%, tau 2 s): the fringe, as a fraction of the width,\n"
	             "    and the drift's state follow a run never resized\n" );
	const float coldParam = 0.5f, tauParam = warmupParam( 2.0 );
	const Hatch big = hatchFor( 1280, 720 ), small = hatchFor( 320, 180 );
	const Floats cardBig = linesCard( 1280, 720, big.xs, big.ys ), cardSmall = linesCard( 320, 180, small.xs, small.ys );
	Rig moved, kept;
	if( !startRig( moved, 1280, 720, perturb, &cardBig ) || !startRig( kept, 1280, 720, perturb, &cardBig ) )
		return 1;
	for( Rig* r : { &moved, &kept } )
	{
		r->Set( PT_COLD_DRIFT, coldParam );
		r->Set( PT_WARMUP, tauParam );
	}
	if( !moved.Render( 60 ) || !kept.Render( 60 ) )
		return 1;
	if( !moved.Resize( 320, 180, &cardSmall ) )
		return 1;
	if( !moved.Render( 30 ) || !kept.Render( 30 ) )
		return 1;
	const double tau = hTau( tauParam ), cold = hColdDrift( coldParam );
	double dir[ 2 ][ 2 ];
	projector::DriftDirection( 1, 0, dir[ 0 ] );
	projector::DriftDirection( 1, 1, dir[ 1 ] );
	const double s = std::exp( -( 89.0 / 60.0 ) / tau );
	double worst   = 0.0;
	Fringe fm, fk;
	for( int which = 0; which < 2; ++which )
	{
		const int w            = which == 0 ? 320 : 1280;
		const double D         = cold * w * s;
		const double want[ 4 ] = { D * dir[ 0 ][ 0 ], D * dir[ 0 ][ 1 ], D * dir[ 1 ][ 0 ], D * dir[ 1 ][ 1 ] };
		const Fringe f         = measureHatch( ( which == 0 ? moved : kept ).Output(), w, which == 0 ? 180 : 720, which == 0 ? small : big, want, kLineSigma );
		( which == 0 ? fm : fk ) = f;
		const double tol       = uniformTolerance( w, which == 0 ? 180 : 720, f.mad ) + D * ( 90 * 0.5e-6 / tau + 1e-6 );
		worst                  = worse( worst, f.worst / tol );
	}
	double fracDiff = 0.0;
	for( int c = 0; c < 4; ++c )
		fracDiff = worse( fracDiff, std::fabs( fm.mean[ c ] / 320.0 - fk.mean[ c ] / 1280.0 ) );
	const double fracTol = uniformTolerance( 320, 180, fm.mad ) / 320.0 + uniformTolerance( 1280, 720, fk.mad ) / 1280.0;
	Check( worst <= 1.0 && fracDiff <= fracTol && moved.plugin.WarmState() == kept.plugin.WarmState(),
	       fmt( "half a second after the resize: red fringe (%+.5f, %+.5f) of the width resized, (%+.5f, %+.5f) kept -- worst difference %.2e of the width "
	            "(tolerance %.2e); each against D e^(-t/tau): worst error / tolerance %.3f; the drift's state %.12f and %.12f (bit-exact)",
	            fm.mean[ 0 ] / 320.0, fm.mean[ 1 ] / 320.0, fk.mean[ 0 ] / 1280.0, fk.mean[ 1 ] / 1280.0, fracDiff, fracTol, worst, moved.plugin.WarmState(),
	            kept.plugin.WarmState() ) );
	return Verdict();
}

//===========================================================================
// --clock
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: a host clock at 4.99e8 ms gives the pictures a clock from 0 s does, bit for bit, through a warm-up with Power On; a clock\n"
	             "    left to settle its own unit settles on the right one\n" );
	for( const Raster& raster : kRasters )
	{
		const Hatch g     = hatchFor( raster.w, raster.h );
		const Floats card = linesCard( raster.w, raster.h, g.xs, g.ys );
		Rig zero, late;
		if( !startRig( zero, raster.w, raster.h, perturb, &card ) || !startRig( late, raster.w, raster.h, perturb, &card ) )
			return 1;
		late.plugin.SetClockScaleForTest( 0.001 );
		late.timeOffset = 4.99e8;
		late.timeScale  = 1000.0;
		for( Rig* r : { &zero, &late } )
		{
			r->Set( PT_CONVERGENCE, 0.1f );
			r->Set( PT_COLD_DRIFT, 0.5f );
			r->Set( PT_WARMUP, warmupParam( 2.0 ) );
		}
		size_t differ = 0, values = 0;
		for( int f = 0; f < 120; ++f )
		{
			if( f == 40 || f == 41 )
				for( Rig* r : { &zero, &late } )
					r->Set( PT_POWER_ON, f == 40 ? 1.0f : 0.0f );
			if( !zero.Render( 1 ) || !late.Render( 1 ) )
				return 1;
			if( f % 30 == 29 )
			{
				const Floats a = zero.Output(), b = late.Output();
				differ += differing( a, b );
				values += a.size();
			}
		}
		Check( differ == 0 && zero.plugin.WarmState() == late.plugin.WarmState() && zero.plugin.WarmState() < 1.0,
		       fmt( "%4dx%-4d 120 frames, Power On at frame 40: %zu of %zu values differ at frames 30, 60, 90, 120 (bit-exact); the drift's state %.12f "
		            "and %.12f",
		            raster.w, raster.h, differ, values, zero.plugin.WarmState(), late.plugin.WarmState() ) );

		for( const double unit : { 1.0, 1000.0 } )
		{
			Rig r;
			if( !startRig( r, raster.w, raster.h, perturb ) )
				return 1;
			r.plugin.SetClockScaleForTest( 0.0 );
			r.timeOffset = unit == 1.0 ? 12.5 : 4.99e8;
			r.timeScale  = unit;
			if( !r.Render( 60 ) )
				return 1;
			const double want = 1.0 / unit;
			const double got  = r.plugin.ClockScale();
			const double host = ( r.timeOffset + r.timeScale * r.Seconds( r.frame - 1 ) ) * want;
			Check( got == want && std::fabs( r.plugin.HostNow() - host ) < 1e-6,
			       fmt( "%4dx%-4d a host clock in %s at 60 fps, rendered faster than real time: settled on %s by frame 60, reads %.6f s (host %.6f s)",
			            raster.w, raster.h, unit == 1.0 ? "seconds" : "milliseconds", got == 1.0 ? "seconds" : ( got == 0.001 ? "milliseconds" : "nothing" ),
			            r.plugin.HostNow(), host ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_CONVERGENCE, 0.2f );
		GLuint vao = 0, buffer = 0, unpack = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		glGenBuffers( 1, &unpack );
		glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack );
		glBufferData( GL_PIXEL_UNPACK_BUFFER, 64, nullptr, GL_STREAM_DRAW );
		glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
		int wrong = 0;
		Floats reference;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
			glPixelStorei( GL_UNPACK_ROW_LENGTH, 7 );
			glActiveTexture( GL_TEXTURE3 );
			rig.plugin.SetTime( rig.Seconds( frame ) );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0, ub = 0, ua = 0, url = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &ub );
			glGetIntegerv( GL_UNPACK_ALIGNMENT, &ua );
			glGetIntegerv( GL_UNPACK_ROW_LENGTH, &url );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += ub != static_cast< GLint >( unpack ) || ua != 1 || url != 7;
			wrong += glGetError() != GL_NO_ERROR;
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
			glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
		}
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		glDeleteBuffers( 1, &unpack );
		Check( wrong == 0, fmt( "%4dx%-4d three frames with a host viewport, blend, clear colour, vertex array, buffer, texture unit and pixel-unpack "
		                        "buffer and store of its own: %d things not put back, GL errors included",
		                        raster.w, raster.h, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// --robust
//===========================================================================
int runRobust( const Perturb& perturb )
{
	std::printf( "\n=== robust: a clip out of range (-1, 2 and 1e30 in a float texture, alpha too) is the clip clamped, bit for bit at Mix 1; every control\n"
	             "    at its extremes gives finite output everywhere\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats wild = buildCard( w, h ), tame;
		const float values[] = { -1.0f, 2.0f, 1e30f, 0.5f, 0.0f, 1.0f };
		for( size_t p = 0; p < wild.size(); p += 4 )
		{
			const uint32_t k = static_cast< uint32_t >( p / 4 );
			if( hash01( k, 3 ) < 0.3 )
				for( int c = 0; c < 4; ++c )
					wild[ p + c ] = values[ pcg( k * 4 + c ) % 6 ];
		}
		tame = wild;
		for( float& v : tame )
			v = std::clamp( v, 0.0f, 1.0f );
		Rig a, b;
		if( !startRig( a, w, h, perturb, &wild ) || !startRig( b, w, h, perturb, &tame ) )
			return 1;
		for( Rig* r : { &a, &b } )
		{
			r->Set( PT_CONVERGENCE, 0.3f );
			r->Set( PT_RED_FOCUS, 0.6f );
			r->Set( PT_BLOOM, ParamFromBloom( 6.0 ) );
		}
		if( !a.Render( 2 ) || !b.Render( 2 ) )
			return 1;
		const size_t differ = differing( a.Output(), b.Output() );
		double worst        = 0.0;
		for( const float v : a.Output() )
			worst = worse( worst, std::isfinite( v ) ? 0.0 : std::numeric_limits< double >::quiet_NaN() );

		//Every control at both ends, one at a time, on the wild clip.
		int nonFinite = 0, tried = 0;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			if( id == PT_POWER_ON )
				continue;
			const ControlInfo& info = InfoOf( id );
			for( const float end : { info.kind == Kind::Integer ? info.low : 0.0f, info.kind == Kind::Integer ? info.high : ( info.kind == Kind::Option ? static_cast< float >( info.optionCount - 1 ) : 1.0f ) } )
			{
				Rig r;
				if( !startRig( r, w, h, perturb, &wild ) )
					return 1;
				r.Set( PT_COLD_DRIFT, 1.0f );
				r.Set( PT_WARMUP, 0.0f );
				r.Set( id, end );
				if( !r.Render( 3 ) )
					return 1;
				for( const float v : r.Output() )
					if( !std::isfinite( v ) )
					{
						++nonFinite;
						break;
					}
				++tried;
			}
		}
		Check( differ == 0 && worst == 0.0 && nonFinite == 0,
		       fmt( "%4dx%-4d the wild clip against the clamped one: %zu values differ (bit-exact), non-finite output: %s; %d renders with one control at an "
		            "end: %d with any non-finite value",
		            w, h, differ, worst == 0.0 ? "none" : "SOME", tried, nonFinite ) );
	}
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "model", runModel, true },
		{ "shift", runShift, false },
		{ "zones", runZones, false },
		{ "keystone", runKeystone, false },
		{ "warmup", runWarmup, false },
		{ "focus", runFocus, false },
		{ "bloom", runBloom, false },
		{ "alpha", runAlpha, false },
		{ "resize", runResize, false },
		{ "clock", runClock, false },
		{ "glstate", runGLState, false },
		{ "robust", runRobust, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "model", runModel, "Zenith's square-root spot law for Hitachi's power law", []( Perturb& p ) { p.optics.spotExponentHalf = true; } );
	add( "model", runModel, "defocus added to the spot, not in quadrature", []( Perturb& p ) { p.optics.defocusLinear = true; } );
	add( "zones", runZones, "the nearest zone's error, not interpolated", []( Perturb& p ) { p.zoneModel = 1; } );
	add( "zones", runZones, "the grid transposed", []( Perturb& p ) { p.zoneModel = 2; } );
	add( "zones", runZones, "blue reading red's grid", []( Perturb& p ) { p.zoneModel = 3; } );
	add( "keystone", runKeystone, "both side tubes on the same side", []( Perturb& p ) { p.keystoneModel = 1; } );
	add( "keystone", runKeystone, "an affine keystone (the e^2 u^3 term dropped)", []( Perturb& p ) { p.keystoneModel = 2; } );
	add( "warmup", runWarmup, "the decay stepped per frame (60 fps factors at every rate)", []( Perturb& p ) { p.warmPerFrame = true; } );
	add( "warmup", runWarmup, "Power On ignored", []( Perturb& p ) { p.powerOnIgnored = true; } );
	add( "warmup", runWarmup, "the drift growing from zero", []( Perturb& p ) { p.driftGrows = true; } );
	add( "focus", runFocus, "one focus for all three tubes", []( Perturb& p ) { p.optics.focusShared = true; } );
	add( "focus", runFocus, "astigmatism's line foci on the same side", []( Perturb& p ) { p.optics.astigSameSide = true; } );
	add( "focus", runFocus, "defocus added to the spot, not in quadrature", []( Perturb& p ) { p.optics.defocusLinear = true; } );
	add( "bloom", runBloom, "no saturation (the current linear in the drive)", []( Perturb& p ) { p.saturationModel = 1; } );
	add( "bloom", runBloom, "Zenith's square-root spot law", []( Perturb& p ) { p.optics.spotExponentHalf = true; } );
	add( "bloom", runBloom, "layer weights linear in sigma, not sigma^2", []( Perturb& p ) { p.layerModel = 1; } );
	add( "bloom", runBloom, "the bloom on red too", []( Perturb& p ) { p.optics.bloomOnRed = true; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.alphaModel = 1; } );
	add( "alpha", runAlpha, "the clip's light not weighted by its alpha", []( Perturb& p ) { p.premultiply = false; } );
	add( "resize", runResize, "a new raster restarts the warm-up", []( Perturb& p ) { p.resizeResets = true; } );
	add( "clock", runClock, "the host's clock read in single precision", []( Perturb& p ) { p.floatClock = true; } );
	add( "glstate", runGLState, "the host's GL state not put back", []( Perturb& p ) { p.leakGLState = true; } );
	add( "robust", runRobust, "the prep pass's clamp removed", []( Perturb& p ) { p.clampOn = false; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
		int warm, timed;
		bool widest;
	};
	const Size sizes[] = {
		{ 1280, 720, "720p", 60, 120, false }, { 1920, 1080, "1080p", 60, 120, false }, { 3840, 2160, "4K", 20, 40, false },
		{ 1280, 720, "720p", 30, 60, true },   { 1920, 1080, "1080p", 30, 60, true },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) on the card: the defaults, then the widest kernels (every Focus at 0, Astigmatism 1)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Size& size : sizes )
	{
		Rig rig;
		if( !rig.Init( size.w, size.h ) )
			return 1;
		if( size.widest )
		{
			for( const unsigned int id : { PT_RED_FOCUS, PT_GREEN_FOCUS, PT_BLUE_FOCUS } )
				rig.Set( id, 0.0f );
			rig.Set( PT_ASTIGMATISM, 1.0f );
		}
		if( !rig.Render( size.warm ) )
			return 1;
		std::vector< double > gpu;
		for( int f = 0; f < size.timed; ++f )
		{
			glBeginQuery( GL_TIME_ELAPSED, query );
			if( !rig.Render( 1 ) )
				return 1;
			glEndQuery( GL_TIME_ELAPSED );
			GLuint64 ns = 0;
			glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
			gpu.push_back( static_cast< double >( ns ) * 1e-6 );
		}
		std::sort( gpu.begin(), gpu.end() );
		std::printf( "  %-6s %-8s kernel radius %3d px  GPU median %6.2f ms, worst %6.2f (%5.1f%% of a 60 fps frame)\n", size.name,
		             size.widest ? "widest" : "defaults", rig.plugin.LastKernels().radius, gpu[ gpu.size() / 2 ], gpu.back(),
		             100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ) );
	}
	glDeleteQueries( 1, &query );
	return 0;
}
