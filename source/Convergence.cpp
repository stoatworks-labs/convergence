#include "Convergence.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace convergence
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (UpdateClock).
constexpr int kClockPatience = 30;
/// Host seconds a single frame may advance by.
constexpr double kMaxFrameDelta = 0.1;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// A float texture for data read texel for texel.
GLuint makeDataTexture()
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

void uploadData( GLuint texture, int width, int height, const float* texels )
{
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
	glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, texels );
	glBindTexture( GL_TEXTURE_2D, 0 );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
ConvergencePlugin::ConvergencePlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The warm-up runs in the host's seconds.
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Integer:
			//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an
			//integer is declared with its real default and range (boreal's).
			SetParamInfo( id, info.name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( id, info.low, info.high );
			break;
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint ConvergencePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult ConvergencePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &prepShader, shaders::Pass::Prep, "prep" },
		{ &blurShader, shaders::Pass::Blur, "blur" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Convergence: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//A core profile draws nothing without a vertex array bound; the
	//triangle needs no attributes.
	glGenVertexArrays( 1, &vao );
	zoneTexture     = makeDataTexture();
	kernelTexture   = makeDataTexture();
	kernelsUploaded = false;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
projector::Settings ConvergencePlugin::CurrentSettings() const
{
	projector::Settings s;
	s.focus[ kRed ]   = FocusFromParam( params[ PT_RED_FOCUS ] );
	s.focus[ kGreen ] = FocusFromParam( params[ PT_GREEN_FOCUS ] );
	s.focus[ kBlue ]  = FocusFromParam( params[ PT_BLUE_FOCUS ] );
	s.astigmatism     = AstigmatismFromParam( params[ PT_ASTIGMATISM ] );
	s.fullWhiteMa     = BloomFromParam( params[ PT_BLOOM ] );
	s.keystone        = KeystoneFromParam( params[ PT_KEYSTONE ] );
	return s;
}

//---------------------------------------------------------------------------
bool ConvergencePlugin::ensureBuffers( int width, int height )
{
	using S = PassBuffer::Sampling;
	bool ok = true;
	for( PassBuffer& b : tubes )
		ok = ok && b.Ensure( width, height, GL_RGBA32F, S::Nearest );
	for( PassBuffer& b : layers )
		ok = ok && b.Ensure( width, height, GL_RGBA32F, S::Nearest );
	return ok;
}

void ConvergencePlugin::draw( PassBuffer& target )
{
	glBindFramebuffer( GL_FRAMEBUFFER, target.GetGLID() );
	glViewport( 0, 0, static_cast< GLsizei >( target.GetWidth() ), static_cast< GLsizei >( target.GetHeight() ) );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

//---------------------------------------------------------------------------
void ConvergencePlugin::uploadZones( int width )
{
	const int setup = SetupFromParam( params[ PT_SETUP ] );
	const int n     = ZonesFromParam( params[ PT_ZONES ] );
	if( setup != tableSetup || n != tableCount )
	{
		unitTable  = projector::ZoneTable( setup, n );
		tableSetup = setup;
		tableCount = n;
	}
	//The RMS set error, a fraction of the width, in pixels.
	const double rms = ConvergenceFromParam( params[ PT_CONVERGENCE ] ) * width;
	zoneCount        = n;
	zoneTexels.resize( unitTable.size() );
	for( size_t i = 0; i < unitTable.size(); ++i )
	{
		double v = unitTable[ i ] * rms;
		if( hooks.uniformZones )
		{
			const size_t c = i % 4;
			v              = c < 2 ? hooks.redShift[ c ] : hooks.blueShift[ c - 2 ];
		}
		zoneTexels[ i ] = static_cast< float >( v );
	}
	uploadData( zoneTexture, n, n, zoneTexels.data() );
}

void ConvergencePlugin::uploadKernels()
{
	//The kernels depend on these alone; rebuilt (a few thousand erfc at the
	//widest) and uploaded only when one changes.
	const projector::Frame& f = frame;
	const double key[ 7 ]     = { f.sigmaX[ 0 ], f.sigmaX[ 1 ], f.sigmaX[ 2 ], f.sigmaY[ 0 ], f.sigmaY[ 1 ], f.sigmaY[ 2 ], f.layerStep };
	if( kernelsUploaded && std::memcmp( key, kernelKey, sizeof( key ) ) == 0 )
		return;
	std::memcpy( kernelKey, key, sizeof( key ) );
	kernels = projector::BuildKernels( f );
	uploadData( kernelTexture, 2 * kernels.radius + 1, 4, kernels.texels.data() );
	kernelsUploaded = true;
}

//---------------------------------------------------------------------------
void ConvergencePlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the passes' buffers" );
		return;
	}
	glBindVertexArray( vao );
	const projector::Frame& f = frame;

	//1. prep: A (red, green, the blue current), then B (the blue layers)
	glUseProgram( prepShader.GetGLID() );
	bindUnit( 0, input->Handle );
	glUniform1i( where( prepShader, "InputTexture" ), 0 );
	glUniform1i( where( prepShader, "Premultiply" ), hooks.premultiply ? 1 : 0 );
	glUniform1i( where( prepShader, "ClampOn" ), hooks.clampOn ? 1 : 0 );
	glUniform1i( where( prepShader, "SaturationModel" ), hooks.saturationModel );
	glUniform1i( where( prepShader, "LayerModel" ), hooks.layerModel );
	glUniform1f( where( prepShader, "Is" ), static_cast< float >( projector::kSaturationMa ) );
	glUniform1f( where( prepShader, "Iw" ), static_cast< float >( f.fullWhiteMa ) );
	glUniform1f( where( prepShader, "IsIw" ), static_cast< float >( projector::kSaturationMa * f.fullWhiteMa ) );
	glUniform1f( where( prepShader, "SigmaOne2" ), static_cast< float >( f.sigmaOnePx * f.sigmaOnePx ) );
	glUniform1f( where( prepShader, "TwoP" ), static_cast< float >( 2.0 * f.exponent ) );
	glUniform1f( where( prepShader, "LayerStep" ), static_cast< float >( f.layerStep ) );
	glUniform1i( where( prepShader, "Output" ), 0 );
	draw( tubes[ 0 ] );
	glUniform1i( where( prepShader, "Output" ), 1 );
	draw( layers[ 0 ] );

	//2. blur: along x into [1], along y back into [0]
	glUseProgram( blurShader.GetGLID() );
	bindUnit( 1, kernelTexture );
	glUniform1i( where( blurShader, "Source" ), 0 );
	glUniform1i( where( blurShader, "Kernel" ), 1 );
	glUniform1i( where( blurShader, "Radius" ), kernels.radius );
	glUniform2i( where( blurShader, "Size" ), width, height );
	struct Blur
	{
		PassBuffer* from;
		PassBuffer* to;
		int row;
		int dx, dy;
	};
	const Blur blurs[] = {
		{ &tubes[ 0 ], &tubes[ 1 ], 0, 1, 0 },
		{ &layers[ 0 ], &layers[ 1 ], 1, 1, 0 },
		{ &tubes[ 1 ], &tubes[ 0 ], 2, 0, 1 },
		{ &layers[ 1 ], &layers[ 0 ], 3, 0, 1 },
	};
	for( const Blur& b : blurs )
	{
		bindUnit( 0, b.from->TextureID() );
		glUniform1i( where( blurShader, "Row" ), b.row );
		glUniform2i( where( blurShader, "Direction" ), b.dx, b.dy );
		draw( *b.to );
	}

	//3. composite, into the host's framebuffer and viewport
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( 0, input->Handle );
	bindUnit( 1, tubes[ 0 ].TextureID() );
	bindUnit( 2, layers[ 0 ].TextureID() );
	bindUnit( 3, zoneTexture );
	glUniform1i( where( compositeShader, "InputTexture" ), 0 );
	glUniform1i( where( compositeShader, "Tubes" ), 1 );
	glUniform1i( where( compositeShader, "Layers" ), 2 );
	glUniform1i( where( compositeShader, "Zones" ), 3 );
	glUniform1i( where( compositeShader, "ZoneCount" ), zoneCount );
	glUniform1i( where( compositeShader, "ZoneModel" ), hooks.zoneModel );
	glUniform4f( where( compositeShader, "Drift" ), static_cast< float >( drift[ 0 ] ), static_cast< float >( drift[ 1 ] ),
	             static_cast< float >( drift[ 2 ] ), static_cast< float >( drift[ 3 ] ) );
	glUniform1f( where( compositeShader, "KeystoneE" ), static_cast< float >( f.keystoneE ) );
	glUniform1f( where( compositeShader, "KeystoneK" ), static_cast< float >( f.keystoneK ) );
	glUniform1i( where( compositeShader, "KeystoneModel" ), hooks.keystoneModel );
	glUniform2i( where( compositeShader, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform2i( where( compositeShader, "Size" ), width, height );
	glUniform1f( where( compositeShader, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	glUniform1i( where( compositeShader, "AlphaModel" ), hooks.alphaModel );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	unbindTextureUnits( 4 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void ConvergencePlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (ferrofluid's, by way of
	//rosette, millpond and intensifier).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
		//A host that renders much faster than real time -- an offline render,
		//oxbow's selftest, cvtest -- never wins the vote. After kClockPatience
		//frames the step's size decides: a frame is well under a second and
		//well over a millisecond (fountain's).
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s. Only differences are used.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( hooks.floatClock )
		now = static_cast< double >( static_cast< float >( now ) );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
FFResult ConvergencePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	restore.restoreOnExit     = !hooks.leakGLState;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 || vao == 0 )
		return FF_FAIL;

	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow   = now;
	//To the microsecond: a clock at 4.99e8 ms and one from 0 then give the
	//same drift to the bit (a frame is never near a half microsecond at any
	//common rate).
	dt = std::round( dt * 1e6 ) * 1e-6;

	//The warm-up: s <- s exp(-dt/tau). Products of exponentials compose, so
	//the drift at a time is the same at any frame rate; a paused clock holds it.
	const double tau = WarmupFromParam( params[ PT_WARMUP ] );
	warm *= std::exp( -( hooks.warmPerFrame ? 1.0 / 60.0 : dt ) / tau );
	if( lastWidth > 0 && ( width != lastWidth || height != lastHeight ) && hooks.resizeResets )
		warm = 1.0;
	if( powerOnPending && !hooks.powerOnIgnored )
		warm = 1.0;
	powerOnPending = false;
	lastWidth      = width;
	lastHeight     = height;

	const int setup     = SetupFromParam( params[ PT_SETUP ] );
	const double cold   = ColdDriftFromParam( params[ PT_COLD_DRIFT ] ) * width;
	const double amount = ( hooks.driftGrows ? 1.0 - warm : warm ) * cold;
	for( int tube = 0; tube < 2; ++tube )
	{
		double dir[ 2 ];
		projector::DriftDirection( setup, tube, dir );
		drift[ tube * 2 ]     = amount * dir[ 0 ];
		drift[ tube * 2 + 1 ] = amount * dir[ 1 ];
	}

	frame = projector::Compute( CurrentSettings(), width, height, hooks.optics );
	uploadKernels();
	uploadZones( width );

	render( input, hostViewport, pgl->HostFBO, width, height );
	++frameIndex;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult ConvergencePlugin::DeInitGL()
{
	uniformCache.clear();
	for( FFGLShader* s : { &prepShader, &blurShader, &compositeShader } )
		s->FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
	for( GLuint* t : { &zoneTexture, &kernelTexture } )
	{
		if( *t != 0 )
			glDeleteTextures( 1, t );
		*t = 0;
	}
	kernelsUploaded = false;
	for( PassBuffer& b : tubes )
		b.Destroy();
	for( PassBuffer& b : layers )
		b.Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult ConvergencePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* ConvergencePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult ConvergencePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult ConvergencePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Power On acts on the press (an event is 1 on press, 0 on release): the
	//next frame shows the set cold.
	if( index == PT_POWER_ON )
	{
		const bool down = value > 0.5f;
		if( down && !powerOnHeld )
			powerOnPending = true;
		powerOnHeld = down;
	}
	params[ index ] = value;
	return FF_SUCCESS;
}

float ConvergencePlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace convergence
