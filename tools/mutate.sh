#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the physics, would pass every mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), built arm64-only once. Each mutant edits its file in the copy,
# deletes that file's object -- every object, for a header -- and the binary
# (make compares mtimes to the second, so an edit within the same second as
# the last build would otherwise be missed, and a relink skipped would run the
# last mutant's cvtest), builds cvtest, requires a GLSL mutant to be in the
# binary verbatim, runs the named check at 320x180 (or the raster it names:
# '--keystone@1280x720') under a time limit (a
# mutant can hang: honeydew's lesson; a timeout counts as caught), then puts
# the file back. --resize always runs 1280x720 -> 320x180 and the offline
# --model ignores --size. (ferrofluid's script by way of fountain, filings
# and intensifier; convergence's mutants.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
#
# Two first-draft mutants were equivalent and are gone (AGENTS.md): the blur's
# sum negated (the two passes' signs cancel) and the layer step V_w / 2 (the
# layer mixture's variance is exact for any step >= V_w / 3; only its shape
# changes, which nothing claims). A third, a geometric range high x low, is
# equivalent for Warm-up (whose low end is 1 s) and is run against --bloom.
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap): no GLSL || here.
MUTANTS=(
	"source/Shaders.cpp|/ 1.055, vec3( 2.4 ) );|/ 1.055, vec3( 2.5 ) );|--bloom|GLSL: the sRGB decode's exponent 2.5, 4 -> 5"
	"source/Shaders.cpp|vec3( 1.0 / 2.4 ) ) - 0.055;|vec3( 1.0 / 2.4 ) ) - 0.065;|--focus|GLSL: the sRGB encode's offset 0.065, 5 -> 6"
	"source/Shaders.cpp|lin *= clamp( src.a, 0.0, 1.0 );|lin /= clamp( src.a, 0.0, 1.0 );|--alpha|GLSL: the clip's light divided by its alpha, * -> /"
	"source/Shaders.cpp|if( ClampOn == 1 )|if( ClampOn == 2 )|--robust|GLSL: the prep pass's clamp never on, 1 -> 2"
	"source/Shaders.cpp|d * IsIw / ( Is + Iw * ( 1.0 - d ) );|d * IsIw / ( Is + Iw * ( 2.0 - d ) );|--bloom|GLSL: the calibrated current's denominator, 1 -> 2"
	"source/Shaders.cpp|/ LayerStep, 0.0, 3.0 );|/ LayerStep, 0.0, 2.0 );|--bloom|GLSL: the layer index clamped at 2, 3 -> 2"
	"source/Shaders.cpp|vec4( 0.0, 1.0, 2.0, 3.0 ) )|vec4( 0.0, 1.0, 2.0, 4.0 ) )|--bloom|GLSL: the fourth layer's tent at 4, 3 -> 4"
	"source/Shaders.cpp|vec4( lin.r, lin.g, current, d );|vec4( lin.g, lin.g, current, d );|--shift|GLSL: the red tube fed the green signal, r -> g"
	"source/Shaders.cpp|for( int i = -Radius;|for( int i = +Radius;|--focus|GLSL: the blur's loop from +R, its last tap alone, - -> +"
	"source/Shaders.cpp|vec4 sum = vec4( 0.0 );|vec4 sum = vec4( 1.0 );|--focus|GLSL: the blur's sum starting at 1, 0 -> 1"
	"source/Shaders.cpp|p * n / vec2( Size ) - 0.5;|p * n / vec2( Size ) - 0.4;|--zones|GLSL: the zone points a tenth of a cell off the cell centres, 5 -> 4"
	"source/Shaders.cpp|vec2( n - 2.0 ) );|vec2( n - 1.0 ) );|--zones|GLSL: the last cell reads past the grid, 2 -> 1"
	"source/Shaders.cpp|zone( c + ivec2( 0, 1 ) )|zone( c + ivec2( 0, 2 ) )|--zones|GLSL: the cell's upper corners two rows up, 1 -> 2"
	"source/Shaders.cpp|ZoneModel == 3 ? z.xy : z.zw;|ZoneModel == 3 ? z.xy : z.zz;|--zones|GLSL: blue's y error read from its x, w -> z"
	"source/Shaders.cpp|vec2 centre = 0.5 * vec2( Size );|vec2 centre = 0.6 * vec2( Size );|--keystone|GLSL: the keystone's centre off the raster's, 5 -> 6"
	"source/Shaders.cpp|/ ( 1.0 - eu );|/ ( 1.0 + eu );|--keystone|GLSL: the homography's denominator the wrong way, - -> +"
	"source/Shaders.cpp|KeystoneE : -KeystoneE );|KeystoneE : +KeystoneE );|--keystone|GLSL: the blue tube keystoned to red's side, - -> +"
	"source/Shaders.cpp|vec2 qr   = p - Drift.xy - zr;|vec2 qr   = p + Drift.xy - zr;|--warmup|GLSL: red's warm-up drift reversed, - -> +"
	"source/Shaders.cpp|p - Drift.zw - zb;|p - Drift.zz - zb;|--warmup|GLSL: blue's drift y read from its x, w -> z"
	"source/Shaders.cpp|vec2 t  = s - 0.5;|vec2 t  = s - 0.6;|--shift|GLSL: the resampler's texel centres off by a tenth, 5 -> 6"
	"source/Shaders.cpp|fetch( source, c ) * ( 1.0 - f.x )|fetch( source, c ) * ( 1.0 + f.x )|--shift|GLSL: the resampler's left weight 1 + f, - -> +"
	"source/Shaders.cpp|		return vec4( 0.0 );|		return vec4( 1.0 );|--shift|GLSL: past the frame's edge is white, 0 -> 1"
	"source/Shaders.cpp|vec2 p    = vec2( at ) + 0.5;|vec2 p    = vec2( at ) + 0.6;|--shift|GLSL: the pixel's centre off by a tenth, 5 -> 6"
	"source/Shaders.cpp|texelFetch( Tubes, at, 0 ).g;|texelFetch( Tubes, at, 0 ).b;|--shift|GLSL: green shows the blue current, g -> b"
	"source/Shaders.cpp|( blue.x + blue.y ) + ( blue.z + blue.w );|( blue.x + blue.y ) + ( blue.z - blue.w );|--bloom|GLSL: the fourth blue layer subtracted, + -> -"
	"source/Shaders.cpp|AlphaModel == 1 ? a : 1.0;|AlphaModel == 1 ? a : 0.0;|--alpha|GLSL: the projected picture transparent, 1 -> 0"
	"source/Shaders.cpp|src.rgb * a * ( 1.0 - m )|src.rgb * a * ( 2.0 - m )|--alpha|GLSL: the clip's share at Mix m 2 - m, 1 -> 2"
	"source/Shaders.cpp|if( MixAmount <= 0.0 )|if( MixAmount <= 0.5 )|--alpha|GLSL: the clip handed through up to Mix 0.5, 0 -> 5"
	"source/Projector.h|constexpr double kTubeAngleDeg = 7.8;|constexpr double kTubeAngleDeg = 7.9;|--keystone@1280x720|C++: the side tubes at 7.9 degrees, 8 -> 9"
	"source/Projector.h|constexpr double kSaturationMa = 4.0 / 3.0;|constexpr double kSaturationMa = 4.0 / 2.0;|--bloom|C++: I_s 2 mA, 3 -> 2"
	"source/Projector.cpp|const double bx    = scale * std::fabs( delta + a );|const double bx    = scale * std::fabs( delta - a );|--focus|C++: the horizontal line focus on the wrong side, + -> -"
	"source/Projector.cpp|f.sigmaX[ tube ] = std::sqrt( beam * beam + bx * bx );|f.sigmaX[ tube ] = std::sqrt( beam * beam * bx * bx );|--focus|C++: the spot and the defocus multiplied, + -> *"
	"source/Projector.cpp|static_cast< double >( kLayers - 1 );|static_cast< double >( kLayers + 1 );|--bloom|C++: the layer step V_w / 5, the full-white spot past the last layer, - -> +"
	"source/Projector.cpp|std::sqrt( -2.0 * std::log( u1 ) )|std::sqrt( -3.0 * std::log( u1 ) )|--model|C++: Box-Muller's radius with -3 ln u, 2 -> 3"
	"source/Projector.cpp|const double v  = i == 0 ? std::erf( 0.5 * k )|const double v  = i == 0 ? std::erf( 0.6 * k )|--focus|C++: the kernel's centre pixel 1.2 wide, 5 -> 6"
	"source/Controls.cpp|return low * std::pow( high / low, clamp01( v ) );|return low * std::pow( high * low, clamp01( v ) );|--bloom|C++: a geometric control's range high x low, / -> *"
	"source/Controls.h|constexpr double kConvergenceHigh = 0.015;|constexpr double kConvergenceHigh = 0.016;|--zones|C++: Convergence's top 1.6% of the width, 5 -> 6"
	"source/Convergence.cpp|double v = unitTable[ i ] * rms;|double v = unitTable[ i ] / rms;|--zones|C++: the zone errors divided by the RMS, * -> /"
	"source/Convergence.cpp|warm *= std::exp( -( hooks|warm *= std::exp( +( hooks|--warmup|C++: the drift growing, - -> +"
	"source/Convergence.cpp|dt = std::round( dt * 1e6 ) * 1e-6;|dt = std::round( dt * 1e5 ) * 1e-6;|--warmup|C++: the frame counted a tenth as long, 6 -> 5"
	"source/Convergence.cpp|drift[ tube * 2 + 1 ] = amount * dir[ 1 ];|drift[ tube * 2 + 1 ] = amount * dir[ 0 ];|--warmup|C++: the drift's y along its x, 1 -> 0"
	"source/Convergence.cpp|clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;|clockScale  = hostDelta > 1.0 ? 1.0 : 0.001;|--clock|C++: the clock-unit fallback upside down, < -> >"
	"source/Convergence.cpp|const bool down = value > 0.5f;|const bool down = value > 1.5f;|--warmup|C++: Power On never pressed, 0 -> 1"
	"source/Convergence.cpp|projector::kSaturationMa * f.fullWhiteMa ) );|projector::kSaturationMa / f.fullWhiteMa ) );|--bloom|C++: the shader's I_s I_w as I_s / I_w, * -> /"
	"source/Convergence.cpp|{ &tubes[ 1 ], &tubes[ 0 ], 2, 0, 1 },|{ &tubes[ 1 ], &tubes[ 0 ], 0, 0, 1 },|--focus|C++: red and green's vertical pass reads the horizontal kernels, 2 -> 0"
	"source/GLState.h|viewport[ 2 ], viewport[ 3 ] );|viewport[ 2 ], viewport[ 2 ] );|--glstate|C++: the host's viewport put back square, 3 -> 2"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCONVERGENCE_BUILD_TOOLS=ON >/dev/null
cmake --build "$tree/build" --target cvtest -j8 >/dev/null 2>&1 || { echo "the unmutated copy does not build"; exit 1; }

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && "$ONLY" != "$index" ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$REPO/$file" "$tree/$file"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	# A header's mutant reaches every object that includes it: delete them all.
	object="$( basename "$file" ).o"
	[[ "$file" == *.h ]] && object="*.o"
	find "$tree/build" -name "$object" -delete
	# The binary too: a relink skipped would run the last mutant's cvtest.
	rm -f "$tree/build/cvtest"
	printf '\n== mutant %d: %s\n' "$index" "$what"
	status=0
	if ! cmake --build "$tree/build" --target cvtest -j8 >"$WORK/build.log" 2>&1; then
		# A mutant that does not compile proves nothing about the checks.
		printf '   FAIL  the mutant does not build -- choose another\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
	elif [[ "$file" == source/Shaders.cpp ]] && ! LC_ALL=C grep -qF -- "$mutant" "$tree/build/cvtest"; then
		# A GLSL mutant is a string literal: it must be in the binary, verbatim.
		printf '   FAIL  the mutant is not in the built binary -- the build did not pick it up\n'
	else
		size=320x180
		if [[ "$check" == *@* ]]; then
			size="${check#*@}"
			check="${check%@*}"
		fi
		limit 600 "$tree/build/cvtest" "$check" --size "$size" >"$WORK/log" 2>&1 || status=$?
		if [[ "$status" -eq 0 ]]; then
			printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
		else
			printf '   ok    %s fails against the mutant (exit %d%s):\n' "$check" "$status" "$( [[ "$status" -eq 142 ]] && echo ', the time limit' )"
			# A mutant that timed out printed no FAIL: grep finding none must not end the run.
			{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
			caught=$(( caught + 1 ))
		fi
	fi
	cp "$REPO/$file" "$tree/$file"
	find "$tree/build" -name "$object" -delete
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
