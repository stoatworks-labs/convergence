#include "Shaders.h"

namespace convergence::shaders
{

const char* const kVersion = "#version 410 core\n";

/// The full-raster triangle, from gl_VertexID alone.
const char* const kQuadVertex = R"(
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
)";

/// IEC 61966-2-1 both ways.
const char* const kCommon = R"(
vec3 toLinear( vec3 c )
{
	c       = max( c, vec3( 0.0 ) );
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
)";

/// The guns' drives. Output 0, texture A: red light, green light, the blue
/// gun's current I(d) in mA. Output 1, texture B: the blue light d shared
/// between the layers either side of its own spot variance sigma(I(d))^2,
/// in proportion to the distance in variance (tent weights over the layer
/// index), so the mixture's variance is exactly sigma(I(d))^2.
const char* const kPrepFragment = R"(
uniform sampler2D InputTexture;
uniform int Output;
uniform int Premultiply;    // 1 the plugin; 0 the negative control
uniform int ClampOn;        // 1 the plugin; 0 the negative control
uniform int SaturationModel;// 0 the plugin; 1 the current linear in the drive
uniform int LayerModel;     // 0 the plugin; 1 the weights linear in sigma
uniform float Is;
uniform float Iw;
uniform float IsIw;
uniform float SigmaOne2;    // sigma(1 mA)^2, px^2
uniform float TwoP;         // 2 p
uniform float LayerStep;    // V_w / 3, px^2
out vec4 FragColor;
void main()
{
	vec4 src = texelFetch( InputTexture, ivec2( gl_FragCoord.xy ), 0 );
	vec3 lin = toLinear( src.rgb );
	if( Premultiply == 1 )
		lin *= clamp( src.a, 0.0, 1.0 );
	if( ClampOn == 1 )
		lin = clamp( lin, 0.0, 1.0 );
	float d       = lin.b;
	float current = SaturationModel == 1 ? d * Iw : d * IsIw / ( Is + Iw * ( 1.0 - d ) );
	if( Output == 0 )
	{
		FragColor = vec4( lin.r, lin.g, current, 0.0 );
		return;
	}
	float x = clamp( SigmaOne2 * pow( current, TwoP ) / LayerStep, 0.0, 3.0 );
	vec4 w  = max( vec4( 1.0 ) - abs( vec4( x ) - vec4( 0.0, 1.0, 2.0, 3.0 ) ), vec4( 0.0 ) );
	if( LayerModel == 1 )
	{
		float k0 = min( floor( x ), 2.0 );
		float f  = ( sqrt( x ) - sqrt( k0 ) ) / ( sqrt( k0 + 1.0 ) - sqrt( k0 ) );
		w        = vec4( 0.0 );
		w[ int( k0 ) ]     = 1.0 - f;
		w[ int( k0 ) + 1 ] = f;
	}
	FragColor = d * w;
}
)";

/// One axis of the separable blur: every channel by its own kernel, read
/// from row Row of the kernel texture; past the frame's edge is black.
const char* const kBlurFragment = R"(
uniform sampler2D Source;
uniform sampler2D Kernel;
uniform int Row;
uniform int Radius;
uniform ivec2 Direction;
uniform ivec2 Size;
out vec4 FragColor;
void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy );
	vec4 sum = vec4( 0.0 );
	for( int i = -Radius; i <= Radius; ++i )
	{
		ivec2 q = at + Direction * i;
		if( q.x < 0 || q.y < 0 || q.x >= Size.x || q.y >= Size.y )
			continue;
		sum += texelFetch( Kernel, ivec2( i + Radius, Row ), 0 ) * texelFetch( Source, q, 0 );
	}
	FragColor = sum;
}
)";

/// To the host. Green at the pixel; red and blue where their tubes put the
/// raster: S = keystone^-1( P - drift - zone(P) ), the zone field the
/// bilinear interpolation of the grid's set errors at its points (the
/// centres of an N x N partition), constant beyond the outermost. Every
/// displacement is P minus small terms, so with every error zero S is P and
/// a whole-pixel shift lands on texel centres. Output alpha mix( a, 1, Mix ):
/// the projected picture fills the frame. Mix mixes premultiplied, so what
/// sits under a transparent pixel never shows. Mix 0 hands the clip through.
const char* const kCompositeFragment = R"(
uniform sampler2D InputTexture;
uniform sampler2D Tubes; // r red, g green
uniform sampler2D Layers;// the four blue layers
uniform sampler2D Zones; // N x N: red x, red y, blue x, blue y, px
uniform int ZoneCount;
uniform int ZoneModel;   // 0 the plugin; 1 nearest; 2 transposed; 3 blue on red's grid
uniform vec4 Drift;      // red x, red y, blue x, blue y, px
uniform float KeystoneE;
uniform float KeystoneK;
uniform int KeystoneModel;// 0 the plugin; 1 both tubes on one side; 2 affine
uniform ivec2 ViewOrigin;
uniform ivec2 Size;
uniform float MixAmount;
uniform int AlphaModel;  // 0 the plugin; 1 the clip's alpha passed through
out vec4 FragColor;

vec4 zone( ivec2 ij )
{
	return texelFetch( Zones, ZoneModel == 2 ? ij.yx : ij, 0 );
}

vec4 zoneField( vec2 p )
{
	float n = float( ZoneCount );
	vec2 g  = p * n / vec2( Size ) - 0.5;
	if( ZoneModel == 1 )
		return zone( clamp( ivec2( floor( g + 0.5 ) ), ivec2( 0 ), ivec2( ZoneCount - 1 ) ) );
	vec2 b  = clamp( floor( g ), vec2( 0.0 ), vec2( n - 2.0 ) );
	vec2 f  = clamp( g - b, vec2( 0.0 ), vec2( 1.0 ) );
	ivec2 c = ivec2( b );
	vec4 lo = zone( c ) * ( 1.0 - f.x ) + zone( c + ivec2( 1, 0 ) ) * f.x;
	vec4 hi = zone( c + ivec2( 0, 1 ) ) * ( 1.0 - f.x ) + zone( c + ivec2( 1, 1 ) ) * f.x;
	return lo * ( 1.0 - f.y ) + hi * f.y;
}

// keystone^-1( q ) - q for a tube of coefficient e, in px.
vec2 keystone( vec2 q, float e )
{
	vec2 centre = 0.5 * vec2( Size );
	vec2 n    = ( q - centre ) / centre;
	float eu  = e * n.x;
	vec2 d    = KeystoneModel == 2 ? vec2( eu * n.x, eu * n.y ) : vec2( eu * n.x, eu * n.y ) / ( 1.0 - eu );
	return KeystoneK * d * centre;
}

// A texel of the source, black outside the frame.
vec4 fetch( sampler2D source, ivec2 q )
{
	if( q.x < 0 || q.y < 0 || q.x >= Size.x || q.y >= Size.y )
		return vec4( 0.0 );
	return texelFetch( source, q, 0 );
}

// Bilinear by hand: texel centres at integer + 0.5.
vec4 sampleAt( sampler2D source, vec2 s )
{
	vec2 t  = s - 0.5;
	vec2 b  = floor( t );
	vec2 f  = t - b;
	ivec2 c = ivec2( b );
	vec4 lo = fetch( source, c ) * ( 1.0 - f.x ) + fetch( source, c + ivec2( 1, 0 ) ) * f.x;
	vec4 hi = fetch( source, c + ivec2( 0, 1 ) ) * ( 1.0 - f.x ) + fetch( source, c + ivec2( 1, 1 ) ) * f.x;
	return lo * ( 1.0 - f.y ) + hi * f.y;
}

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	vec2 p    = vec2( at ) + 0.5;
	vec4 z    = zoneField( p );
	vec2 zr   = z.xy;
	vec2 zb   = ZoneModel == 3 ? z.xy : z.zw;
	vec2 qr   = p - Drift.xy - zr;
	vec2 qb   = p - Drift.zw - zb;
	vec2 sr   = qr + keystone( qr, KeystoneE );
	vec2 sb   = qb + keystone( qb, KeystoneModel == 1 ? KeystoneE : -KeystoneE );
	vec3 lin;
	lin.r     = sampleAt( Tubes, sr ).r;
	lin.g     = texelFetch( Tubes, at, 0 ).g;
	vec4 blue = sampleAt( Layers, sb );
	lin.b     = ( blue.x + blue.y ) + ( blue.z + blue.w );
	vec3 rgb  = toCode( lin );
	float a   = clamp( src.a, 0.0, 1.0 );
	float outA = AlphaModel == 1 ? a : 1.0;
	float m   = min( MixAmount, 1.0 );
	float mixA = a * ( 1.0 - m ) + outA * m;
	vec3 mixC = ( src.rgb * a * ( 1.0 - m ) + rgb * outA * m ) / max( mixA, 1e-30 );
	FragColor = vec4( mixC, mixA );
}
)";

std::string AssembleVertex( Pass )
{
	return std::string( kVersion ) + kQuadVertex;
}

std::string Assemble( Pass pass )
{
	switch( pass )
	{
	case Pass::Prep: return std::string( kVersion ) + kCommon + kPrepFragment;
	case Pass::Blur: return std::string( kVersion ) + kBlurFragment;
	case Pass::Composite: return std::string( kVersion ) + kCommon + kCompositeFragment;
	}
	return std::string();
}

} // namespace convergence::shaders
