"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL pieces of `source/Shaders.cpp`: kVersion,
kQuadVertex, kCommon (the sRGB pair) and the three passes -- prep, blur and
composite. That is two copies of the same text, and two copies drift --
quietly, because a fringe drawn by a *plausible* resampler looks exactly like
one drawn by the right one. The whole claim of the page is that it runs the
plugin's own shaders, so the claim needs something enforcing it. `cvtest`
drives the real plugin class and has never heard of this page, and
`tools/glslc.sh` compiles the C++ copies and never looks at the JS one.

------------------------------------------------------------------- what it does

Pulls each piece out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. `kVersion`, a plain string, is compared too.
A piece may be several adjacent raw strings (MSVC refuses one literal over
about 16 KB, C2026); this joins them as the compiler does, with nothing
between. None of convergence's pieces needs that today.

It fails when Shaders.cpp grows a piece this script does not list (a pass the
page would silently lack), and it checks the JOIN as well as the pieces:
AssembleVertex() is kVersion + kQuadVertex for every pass, Assemble() joins
each fragment stage from its pieces, Convergence.cpp's InitGL compiles those
for all three passes, and plugin.js must assemble them the same way.

The page is more than the shaders: the orchestration around them -- the
buffers and their formats, the six draws and their order, the zone and kernel
textures, the clock, the frame length and the warm-up -- is Convergence.cpp's
ProcessOpenGL and render() translated by hand, the zone table, the drift
directions, the frame's optics and the kernels are Projector.cpp's, the
conversions Controls.cpp's. This cannot check a translation, but it can trip
when its source moves: ORCHESTRATION lists the C++ lines the port was made
from, and a change to any of them fails here and needs a reader to carry it
across.

And it fails when a piece declares a uniform that plugin.js never names (a
uniform the page does not set sits at 0, a control quietly dead on the page and
live in the plugin), or when plugin.js hands an integer uniform -- int, ivec,
uint or a sampler -- to the kit's `.set()`, which always uploads a float and so
is refused by GL without a word. Every piece is checked, not only the
fragments.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write would escape one as \\`.
Shaders.cpp has none today. This undoes that escape and REJECTS any other
backslash or any `${` on the JS side; there are none in the C++ bodies, so
either could only be somebody hiding a difference.

`--write` produces shaders.js from the C++ by exactly that escape, so the copy
is spliced by this script and never typed.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half: Controls.cpp (every conversion and
default), Projector.cpp (Hash, the zone table, the drift directions, Compute,
PixelGaussian and BuildKernels) and the CPU half of Convergence.cpp (the
clock, the frame length, the warm-up, the drift, the passes and every
uniform) in demo/plugin.js are a hand translation. demo/tools/crosscheck/
compares the CPU numbers with a printer built from the plugin's own sources,
and renders cvtest --pipe and the page from the same frames and compares the
pictures and the warm-up; it needs Chrome and a GPU, so it is run by hand, not
by verify.sh. When you change one of those, change the port too.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion, a plain string, is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("PREP_FRAGMENT", "kPrepFragment"),
    ("BLUR_FRAGMENT", "kBlurFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]

# The pieces whose uniforms plugin.js must set (by name, quoted).
UNIFORM_PIECES = [symbol for _, symbol in SHADERS]

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugin's assembly; the JS lines
# are plugin.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/Shaders.cpp",
     "std::string AssembleVertex( Pass )\n{\n"
     "\treturn std::string( kVersion ) + kQuadVertex;\n}"),
    ("source/Shaders.cpp",
     "std::string Assemble( Pass pass )\n{\n"
     "\tswitch( pass )\n"
     "\t{\n"
     "\tcase Pass::Prep: return std::string( kVersion ) + kCommon + kPrepFragment;\n"
     "\tcase Pass::Blur: return std::string( kVersion ) + kBlurFragment;\n"
     "\tcase Pass::Composite: return std::string( kVersion ) + kCommon + kCompositeFragment;\n"
     "\t}\n"
     "\treturn std::string();\n}"),
    ("source/Convergence.cpp", "const std::string vertex   = shaders::AssembleVertex( stage.pass );"),
    ("source/Convergence.cpp", "const std::string fragment = shaders::Assemble( stage.pass );"),
    ("source/Convergence.cpp", "if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )"),
    ("source/Convergence.cpp", '{ &prepShader, shaders::Pass::Prep, "prep" },'),
    ("source/Convergence.cpp", '{ &blurShader, shaders::Pass::Blur, "blur" },'),
    ("source/Convergence.cpp", '{ &compositeShader, shaders::Pass::Composite, "composite" },'),
    ("demo/plugin.js", "const vertex = S.VERSION + S.QUAD_VERTEX;"),
    ("demo/plugin.js", "prep: S.VERSION + S.COMMON + S.PREP_FRAGMENT,"),
    ("demo/plugin.js", "blur: S.VERSION + S.BLUR_FRAGMENT,"),
    ("demo/plugin.js", "composite: S.VERSION + S.COMMON + S.COMPOSITE_FRAGMENT,"),
    ("demo/plugin.js", "programs[name] = new Program(gl, vertex, fragment, name, { attribs: {} });"),
]

# The C++ the page's CPU half (plugin.js, between its PORT and FRAME markers)
# was ported from, line by line. Not a proof of the port: a tripwire for its
# source.
ORCHESTRATION = [
    # Controls: the ranges, the table and the conversions.
    ("source/Controls.h", "constexpr double kConvergenceHigh = 0.015;"),
    ("source/Controls.h", "constexpr double kColdDriftHigh   = 0.01;"),
    ("source/Controls.h", "constexpr double kWarmupLow = 1.0, kWarmupHigh = 1800.0;"),
    ("source/Controls.h", "constexpr double kBloomLow = 0.1, kBloomHigh = 10.0;"),
    ("source/Controls.h", "constexpr double kAstigmatismHigh = 0.5;"),
    ("source/Controls.h", "constexpr int kSetupLow = 1, kSetupHigh = 1000;"),
    ("source/Controls.cpp", "return std::clamp( static_cast< double >( v ), 0.0, 1.0 );"),
    ("source/Controls.cpp", "return low * std::pow( high / low, clamp01( v ) );"),
    ("source/Controls.cpp",
     "return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );"),
    ("source/Controls.cpp", "return static_cast< float >( std::clamp( value / high, 0.0, 1.0 ) );"),
    ("source/Controls.cpp", 'const char* const kZoneNames[] = { "5x5", "9x9" };'),
    ("source/Controls.cpp", "constexpr int kZoneCounts[]    = { 5, 9 };"),
    ("source/Controls.cpp", "constexpr double kDefaultBloom = 0.32;"),
    ("source/Controls.cpp",
     'standard( PT_CONVERGENCE, "Convergence", "Convergence", ParamFromConvergence( 0.001 ) ),'),
    ("source/Controls.cpp", 'option( PT_ZONES, "Zones", "Convergence", 0.0f, kZoneNames, 2 ),'),
    ("source/Controls.cpp",
     'integer( PT_SETUP, "Setup", "Convergence", 1.0f, static_cast< float >( kSetupLow ), static_cast< float >( kSetupHigh ) ),'),
    ("source/Controls.cpp", 'standard( PT_KEYSTONE, "Keystone", "Convergence", 0.0f ),'),
    ("source/Controls.cpp", 'standard( PT_WARMUP, "Warm-up", "Warm-up", ParamFromWarmup( 400.0 ) ),'),
    ("source/Controls.cpp", 'standard( PT_COLD_DRIFT, "Cold Drift", "Warm-up", ParamFromColdDrift( 0.001 ) ),'),
    ("source/Controls.cpp", 'event( PT_POWER_ON, "Power On", "Warm-up" ),'),
    ("source/Controls.cpp", 'standard( PT_RED_FOCUS, "Red Focus", "Focus", 0.5f ),'),
    ("source/Controls.cpp", 'standard( PT_GREEN_FOCUS, "Green Focus", "Focus", 0.5f ),'),
    ("source/Controls.cpp", 'standard( PT_BLUE_FOCUS, "Blue Focus", "Focus", 0.5f ),'),
    ("source/Controls.cpp", 'standard( PT_ASTIGMATISM, "Astigmatism", "Focus", 0.0f ),'),
    ("source/Controls.cpp", 'standard( PT_BLOOM, "Bloom", "Beam", ParamFromBloom( kDefaultBloom ) ),'),
    ("source/Controls.cpp", 'standard( PT_MIX, "Mix", "Look", 1.0f ),'),
    ("source/Controls.cpp", "const long i = std::lround( static_cast< double >( value ) );"),
    ("source/Controls.cpp", "return kConvergenceHigh * clamp01( v );"),
    ("source/Controls.cpp", "return kZoneCounts[ OptionIndex( v, 2 ) ];"),
    ("source/Controls.cpp", "const double rounded = std::round( static_cast< double >( v ) );"),
    ("source/Controls.cpp",
     "return static_cast< int >( std::clamp( rounded == rounded ? rounded : 1.0, static_cast< double >( kSetupLow ), static_cast< double >( kSetupHigh ) ) );"),
    ("source/Controls.cpp", "return geometric( v, kWarmupLow, kWarmupHigh );"),
    ("source/Controls.cpp", "return kColdDriftHigh * clamp01( v );"),
    ("source/Controls.cpp", "return 2.0 * clamp01( v ) - 1.0;"),
    ("source/Controls.cpp", "return kAstigmatismHigh * clamp01( v );"),
    ("source/Controls.cpp", "return geometric( v, kBloomLow, kBloomHigh );"),
    # Projector: the constants, the hash, the table, the optics, the kernels.
    ("source/Projector.h", "constexpr double kTubeAngleDeg = 7.8;"),
    ("source/Projector.h", "constexpr double kHalfFieldTan = 1.0 / 3.0;"),
    ("source/Projector.h", "constexpr double kSpotDiameter1mA = 0.099;"),
    ("source/Projector.h", "constexpr double kSpotDiameter4mA = 0.256;"),
    ("source/Projector.h", "constexpr double kRasterHeightMm = 5.5 * 0.6 * 25.4;"),
    ("source/Projector.h", "constexpr double kFocusScale = 0.01;"),
    ("source/Projector.h", "constexpr double kSaturationMa = 4.0 / 3.0;"),
    ("source/Projector.h", "constexpr int kLayers = 4;"),
    ("source/Projector.h", "constexpr int kMaxRadius = 256;"),
    ("source/Projector.cpp", "return kHalfFieldTan * std::tan( kTubeAngleDeg * kPi / 180.0 );"),
    ("source/Projector.cpp", "return std::log( kSpotDiameter4mA / kSpotDiameter1mA ) / std::log( 4.0 );"),
    ("source/Projector.cpp", "return 2.0 * std::sqrt( 2.0 * std::log( 20.0 ) );"),
    ("source/Projector.cpp", "return kSpotDiameter1mA / FivePercentDiameterInSigma();"),
    ("source/Projector.cpp", "return SigmaOneMilliampMm() * std::pow( milliamps, exponent );"),
    ("source/Projector.cpp", "const uint32_t state = v * 747796405u + 2891336453u;"),
    ("source/Projector.cpp", "const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;"),
    ("source/Projector.cpp", "return ( word >> 22u ) ^ word;"),
    ("source/Projector.cpp", "return ( static_cast< double >( h ) + 0.5 ) * ( 1.0 / 4294967296.0 );"),
    ("source/Projector.cpp", "uint32_t h = Hash( static_cast< uint32_t >( setup ) ^ 0x9e3779b9u );"),
    ("source/Projector.cpp", "const double u1 = UniformOf( Hash( k ^ 0x68e31da4u ) );"),
    ("source/Projector.cpp", "const double u2 = UniformOf( Hash( k ^ 0xb5297a4du ) );"),
    ("source/Projector.cpp", "return std::sqrt( -2.0 * std::log( u1 ) ) * std::cos( 2.0 * kPi * u2 );"),
    ("source/Projector.cpp",
     "const uint32_t k = key( setup, static_cast< uint32_t >( n ), static_cast< uint32_t >( i ), static_cast< uint32_t >( j ), component );"),
    ("source/Projector.cpp", "table[ ( static_cast< size_t >( j ) * n + i ) * 4 + component ] = normal( k );"),
    ("source/Projector.cpp",
     "const double phi = 2.0 * kPi * UniformOf( key( setup, 0xd21f7u, static_cast< uint32_t >( tube ), 0u, 0u ) );"),
    ("source/Projector.cpp", "f.sigmaOnePx     = SigmaOneMilliampMm() / kRasterHeightMm * h;"),
    ("source/Projector.cpp", "const double big = SpotSigmaMm( s.fullWhiteMa, f.exponent ) / kRasterHeightMm * h;"),
    ("source/Projector.cpp", "f.layerStep      = big * big / static_cast< double >( kLayers - 1 );"),
    ("source/Projector.cpp", "const double scale = kFocusScale * h;"),
    ("source/Projector.cpp", "const double bx    = scale * std::fabs( delta + a );"),
    ("source/Projector.cpp", "const double by    = scale * std::fabs( hooks.astigSameSide ? delta + a : delta - a );"),
    ("source/Projector.cpp", "double beam = tube == 2 ? 0.0 : f.sigmaOnePx;"),
    ("source/Projector.cpp", "f.sigmaX[ tube ] = std::sqrt( beam * beam + bx * bx );"),
    ("source/Projector.cpp", "f.sigmaY[ tube ] = std::sqrt( beam * beam + by * by );"),
    ("source/Projector.cpp", "const int radius = std::min( cap, static_cast< int >( std::ceil( 6.0 * sigma ) ) );"),
    ("source/Projector.cpp", "const double k = 1.0 / ( sigma * std::sqrt( 2.0 ) );"),
    ("source/Projector.cpp", "const double lo = ( std::fabs( static_cast< double >( i ) ) - 0.5 ) * k;"),
    ("source/Projector.cpp", "const double hi = ( std::fabs( static_cast< double >( i ) ) + 0.5 ) * k;"),
    ("source/Projector.cpp",
     "const double v  = i == 0 ? std::erf( 0.5 * k ) : 0.5 * ( std::erfc( lo ) - std::erfc( hi ) );"),
    ("source/Projector.cpp", "b[ layer ] = std::sqrt( tubes[ 2 ] * tubes[ 2 ] + layer * f.layerStep );"),
    ("source/Projector.cpp",
     "k.texels[ ( static_cast< size_t >( row ) * width + ( i + k.radius ) ) * 4 + c ] = static_cast< float >( w[ static_cast< size_t >( i + r ) ] );"),
    # Convergence: the clock, the frame length, the warm-up, the drift, the passes.
    ("source/Convergence.cpp", "constexpr int kClockVotes = 4;"),
    ("source/Convergence.cpp", "constexpr int kClockPatience = 30;"),
    ("source/Convergence.cpp", "constexpr double kMaxFrameDelta = 0.1;"),
    ("source/Convergence.cpp", "if( ratio > 0.1 && ratio < 10.0 )"),
    ("source/Convergence.cpp", "else if( ratio > 100.0 && ratio < 10000.0 )"),
    ("source/Convergence.cpp", "if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )"),
    ("source/Convergence.cpp", "clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;"),
    ("source/Convergence.cpp", "now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;"),
    ("source/Convergence.cpp", "double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;"),
    ("source/Convergence.cpp", "dt = std::round( dt * 1e6 ) * 1e-6;"),
    ("source/Convergence.cpp", "warm *= std::exp( -( hooks.warmPerFrame ? 1.0 / 60.0 : dt ) / tau );"),
    ("source/Convergence.cpp", "if( powerOnPending && !hooks.powerOnIgnored )"),
    ("source/Convergence.cpp", "const double cold   = ColdDriftFromParam( params[ PT_COLD_DRIFT ] ) * width;"),
    ("source/Convergence.cpp", "const double amount = ( hooks.driftGrows ? 1.0 - warm : warm ) * cold;"),
    ("source/Convergence.cpp", "drift[ tube * 2 ]     = amount * dir[ 0 ];"),
    ("source/Convergence.cpp", "drift[ tube * 2 + 1 ] = amount * dir[ 1 ];"),
    ("source/Convergence.cpp", "if( down && !powerOnHeld )"),
    ("source/Convergence.cpp", "const double rms = ConvergenceFromParam( params[ PT_CONVERGENCE ] ) * width;"),
    ("source/Convergence.cpp", "zoneTexels[ i ] = static_cast< float >( v );"),
    ("source/Convergence.cpp", "uploadData( zoneTexture, n, n, zoneTexels.data() );"),
    ("source/Convergence.cpp",
     "const double key[ 7 ]     = { f.sigmaX[ 0 ], f.sigmaX[ 1 ], f.sigmaX[ 2 ], f.sigmaY[ 0 ], f.sigmaY[ 1 ], f.sigmaY[ 2 ], f.layerStep };"),
    ("source/Convergence.cpp", "uploadData( kernelTexture, 2 * kernels.radius + 1, 4, kernels.texels.data() );"),
    ("source/Convergence.cpp", "ok = ok && b.Ensure( width, height, GL_RGBA32F, S::Nearest );"),
    ("source/Convergence.cpp", 'glUniform1f( where( prepShader, "Is" ), static_cast< float >( projector::kSaturationMa ) );'),
    ("source/Convergence.cpp",
     'glUniform1f( where( prepShader, "IsIw" ), static_cast< float >( projector::kSaturationMa * f.fullWhiteMa ) );'),
    ("source/Convergence.cpp",
     'glUniform1f( where( prepShader, "SigmaOne2" ), static_cast< float >( f.sigmaOnePx * f.sigmaOnePx ) );'),
    ("source/Convergence.cpp", 'glUniform1f( where( prepShader, "TwoP" ), static_cast< float >( 2.0 * f.exponent ) );'),
    ("source/Convergence.cpp", "{ &tubes[ 0 ], &tubes[ 1 ], 0, 1, 0 },"),
    ("source/Convergence.cpp", "{ &layers[ 0 ], &layers[ 1 ], 1, 1, 0 },"),
    ("source/Convergence.cpp", "{ &tubes[ 1 ], &tubes[ 0 ], 2, 0, 1 },"),
    ("source/Convergence.cpp", "{ &layers[ 1 ], &layers[ 0 ], 3, 0, 1 },"),
    ("source/Convergence.cpp",
     'glUniform1f( where( compositeShader, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );'),
    ("source/Convergence.cpp", "++frameIndex;"),
]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them.

    Returns (text, count of raw strings), or (None, 0) if the symbol is not
    there or what follows its `=` is not a run of adjacent raw strings ending
    in `;`.
    """
    match = re.search(r'const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    """Every raw-string piece Shaders.cpp defines, by symbol."""
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def uniforms(glsl):
    """Every uniform a piece declares, as (type, name), comments stripped
    first. An array (`uniform float Weights[ 169 ];`) counts by its name."""
    code = re.sub(r"//[^\n]*", "", glsl)
    return re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(?:\[\s*\d+\s*\])?\s*;", code, re.M)


INTEGER_TYPES = re.compile(r"^(int|uint|ivec[234]|uvec[234]|bool|bvec[234]|[iu]?sampler\w+)$")


def write(cpp):
    out = [HEADER]
    version = cpp_version(cpp)
    if version is None:
        print("FAIL  kVersion not found in source/Shaders.cpp")
        return 1
    # kVersion is "#version 410 core\n" in the C++, the \n an escape there and
    # in the JS string literal alike.
    out.append(f"\n// kVersion, source/Shaders.cpp\nexport const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def verbatim(entries, what):
    problems = 0
    for path, text in entries:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    {what}: {path} still says {text.splitlines()[0][:64]}")
        else:
            print(f"FAIL  {what}: {path} no longer says: {text.splitlines()[0]}")
            problems += 1
    return problems


def check(cpp, js):
    problems = 0
    version_cpp = cpp_version(cpp)
    version_js = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<20} matches kVersion")

    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    problems += verbatim(ASSEMBLY, "assembly")
    problems += verbatim(ORCHESTRATION, "orchestration")

    try:
        plugin = read("demo", "plugin.js")
    except FileNotFoundError:
        plugin = ""

    for symbol in UNIFORM_PIECES:
        text, _ = from_cpp(cpp, symbol)
        if text is None:
            continue
        declared = uniforms(text)
        if not declared:
            continue
        # Named as 'Name' or, for an array, element by element as 'Name[0]'.
        unset = [n for _, n in declared if f"'{n}'" not in plugin and f"'{n}[" not in plugin]
        floated = [n for t, n in declared
                   if INTEGER_TYPES.match(t) and re.search(r"\.set\(\s*'" + n + r"[\[']", plugin)]
        if unset:
            print(f"FAIL  {symbol} declares {', '.join(unset)}, which demo/plugin.js never names -- "
                  "the page would leave it at 0")
            problems += 1
        elif floated:
            print(f"FAIL  {symbol}'s integer uniform(s) {', '.join(floated)} go through .set(), which uploads a "
                  "float that GL refuses -- use setInt, setUint, setSampler or gl.uniform2i")
            problems += 1
        else:
            print(f"ok    uniforms: all {len(declared)} of {symbol}'s are named in demo/plugin.js, integers as integers")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp)
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js by hand); an assembly, orchestration, uniform or unlisted-piece "
              "failure needs a reader")
        return 1
    print(f"kVersion and all {len(SHADERS)} shader pieces, their assembly, the port's C++ and the uniforms "
          "are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
