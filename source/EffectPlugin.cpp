#include "Convergence.h"

/**
    The registration. `convergence_core` is an OBJECT library so this
    file-scope constructor, which nothing references by name, is linked into
    the bundle (an archive would let the linker drop it, and the bundle would
    load and report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< convergence::ConvergencePlugin >,// Create method
	"CV01",                                         // Plugin unique ID of maximum length 4
	"SW Convergence",                               // Plugin name
	2,                                              // API major version number
	1,                                              // API minor version number
	0,                                              // Plugin major version number
	1,                                              // Plugin minor version number
	FF_EFFECT,                                      // Plugin type
	"The clip through a three-tube CRT projector. Red, green and blue each have their own tube, lens and angle to the "
	"screen, converged on green zone by zone with a technician's errors; the side tubes drift as the set warms up; each "
	"tube has its own focus and the guns share an astigmatism; the blue phosphor saturates, so the blue gun draws more "
	"current on bright scenes and its spot grows: fringes that differ across the screen, white text with red and blue "
	"edges, and a soft blue channel.",
	"Convergence FFGL effect"// About
);

extern "C" const char* ConvergenceBuildStamp()
{
	return "convergence " CONVERGENCE_VERSION ", built " __DATE__ " " __TIME__;
}
