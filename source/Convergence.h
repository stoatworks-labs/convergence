#pragma once

#include "Controls.h"
#include "PassBuffer.h"
#include "Projector.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace convergence
{

/// The harness's negative controls and test seams that reach the shaders or
/// the plugin's own bookkeeping. All off in the plugin.
struct PluginHooks
{
	bool uniformZones   = false;///< every zone set to the shifts below (the --shift seam)
	double redShift[ 2 ]  = {}; ///< px
	double blueShift[ 2 ] = {}; ///< px
	int zoneModel       = 0;    ///< 1 nearest zone; 2 the grid transposed; 3 blue on red's grid
	int keystoneModel   = 0;    ///< 1 both side tubes on one side; 2 affine (the e^2 u^3 term dropped)
	bool warmPerFrame   = false;///< the decay stepped by a 60 fps frame whatever the clock says
	bool powerOnIgnored = false;
	bool driftGrows     = false;///< the drift grows from zero instead of decaying from Cold Drift
	bool resizeResets   = false;///< a new raster restarts the warm-up
	bool floatClock     = false;///< the host's clock read in single precision
	bool leakGLState    = false;///< the host's GL state not put back
	int saturationModel = 0;    ///< 1 the blue current linear in the drive
	int layerModel      = 0;    ///< 1 the layer weights linear in sigma
	int alphaModel      = 0;    ///< 1 the clip's alpha passed through
	bool premultiply    = true; ///< false: the clip's light not weighted by its alpha
	bool clampOn        = true; ///< false: the prep pass's clamp removed
	projector::OpticsHooks optics;
};

/**
    The plugin, `SW Convergence`: the clip projected by a three-tube CRT
    projector. Every frame: the clock and the warm-up on the CPU, the zone
    grid and the kernels, then the passes of Shaders.h -- prep, blur (four
    draws), composite.

    Host indices are the ParamIds, About last.
*/
class ConvergencePlugin : public CFFGLPlugin
{
public:
	ConvergencePlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// The controls in physical units.
	projector::Settings CurrentSettings() const;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	PluginHooks& HooksForTest()
	{
		return hooks;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	const projector::Frame& LastFrame() const
	{
		return frame;
	}
	const projector::Kernels& LastKernels() const
	{
		return kernels;
	}
	/// The zone grid the last frame used, px: (j n + i) x (red x, red y, blue x, blue y).
	const std::vector< float >& LastZones() const
	{
		return zoneTexels;
	}
	int LastZoneCount() const
	{
		return zoneCount;
	}
	/// The warm-up drift the last frame used, px: red x, red y, blue x, blue y.
	const double* LastDrift() const
	{
		return drift;
	}
	/// The fraction of the cold drift left.
	double WarmState() const
	{
		return warm;
	}
	/// The last frame's texture A after the blur: r red, g green (blurred),
	/// b the blue gun's current in mA, a the blue drive d (both unblurred:
	/// they ride identity kernels).
	GLuint TubesTextureID() const
	{
		return tubes[ 0 ].TextureID();
	}
	double HostNow() const
	{
		return now;
	}
	double ClockScale() const
	{
		return clockScale;
	}
	uint32_t FrameIndex() const
	{
		return frameIndex;
	}

private:
	void UpdateClock();
	bool ensureBuffers( int width, int height );
	void draw( PassBuffer& target );
	void uploadZones( int width );
	void uploadKernels();
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};
	PluginHooks hooks;
	uint32_t frameIndex = 0;

	projector::Frame frame;
	projector::Kernels kernels;
	bool kernelsUploaded = false;
	double kernelKey[ 7 ] = {};

	// The installation: the unit-normal grid for (setup, n), cached.
	int tableSetup = -1, tableCount = 0;
	std::vector< double > unitTable;
	std::vector< float > zoneTexels;
	int zoneCount = 0;
	double drift[ 4 ] = {};

	ffglex::FFGLShader prepShader, blurShader, compositeShader;
	GLuint vao           = 0;
	GLuint zoneTexture   = 0;
	GLuint kernelTexture = 0;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	// tubes: A (red, green, current); layers: B (the blue layers). [0] the
	// result, [1] the horizontal pass's.
	PassBuffer tubes[ 2 ];
	PassBuffer layers[ 2 ];
	int lastWidth = 0, lastHeight = 0;

	// The warm-up: the fraction of the cold drift left, in double.
	double warm        = 1.0;
	bool powerOnHeld   = false;
	bool powerOnPending = false;

	// The clock (ferrofluid's vote on seconds or milliseconds, fountain's
	// offline fallback; intensifier's).
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockSamples    = 0;
	int clockFrames     = 0;
	bool settledJump    = false;
	double now          = 0.0;
	double lastNow      = -1.0;
};

} // namespace convergence
