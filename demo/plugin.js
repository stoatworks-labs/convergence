/**
 * SW Convergence (CV01, an FFGL effect), in a browser.
 *
 * A three-tube CRT projector throwing the clip onto the screen. Green looks
 * straight at the screen; red and blue sit either side at 7.8 degrees and are
 * converged onto green zone by zone, with a technician's error at every zone
 * point; the side tubes drift as the set warms; each tube has its own focus
 * and astigmatism; the blue gun's spot grows with its current.
 *
 * What runs here, and what is a port:
 *
 * - **The shaders are the plugin's.** `shaders.js` is spliced from
 *   source/Shaders.cpp by demo/tools/check_shaders.py --write, unedited, and
 *   every pass is assembled as Shaders.cpp's Assemble() assembles it (below,
 *   compilePrograms). The kit's port() changes the version line and adds
 *   precision qualifiers; nothing else. tools/verify.sh fails if a character
 *   drifts. The vertex stage is the plugin's gl_VertexID triangle, drawn from
 *   an empty vertex array, not the kit's quad.
 * - **The CPU half is a port**, between the PORT and FRAME markers below:
 *   Controls.cpp (every conversion from the host's 0..1, the table and its
 *   defaults), Projector.cpp (KeystoneE, the spot law, the PCG hash, the zone
 *   table by Box-Muller in double, the drift directions, Compute's optics,
 *   PixelGaussian and BuildKernels), and the CPU half of Convergence.cpp:
 *   SetFloatParameter's Power On edge, UpdateClock (the vote on seconds or
 *   milliseconds and the offline fallback), ProcessOpenGL's frame length
 *   (clamped, rounded to the microsecond), the warm-up s <- s exp(-dt/tau),
 *   the drift, the zone and kernel textures and their caches, and render():
 *   the six draws over three passes, their buffers and formats, and every
 *   uniform. demo/tools/crosscheck/ compares the CPU numbers with a printer
 *   built from the plugin's own sources, and the pictures and the warm-up with
 *   cvtest, the plugin's own harness, frame for frame; past that, only a
 *   reader checks it.
 * - `erf` and `erfc` are the page's own: JavaScript has neither, and
 *   PixelGaussian calls std::erf and std::erfc. The crosscheck compares the
 *   kernels they give with the plugin's.
 * - Not ported: the log (Diag.cpp), the About block, GLState.h's save and
 *   restore of a host's state (the page owns its context), and the harness's
 *   hooks (PluginHooks, OpticsHooks), which are all off as shipped and are
 *   folded in at their shipped values.
 */
import { mountDemo } from './vendor/demo.js';
import { PassBuffer, Program, bindTexture } from './vendor/gl.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import * as S from './shaders.js';

const f32 = Math.fround;

// ==== PORT: Controls.h and Controls.cpp =====================================

const PT = {
  CONVERGENCE: 0,
  ZONES: 1,
  SETUP: 2,
  KEYSTONE: 3,
  WARMUP: 4,
  COLD_DRIFT: 5,
  POWER_ON: 6,
  RED_FOCUS: 7,
  GREEN_FOCUS: 8,
  BLUE_FOCUS: 9,
  ASTIGMATISM: 10,
  BLOOM: 11,
  MIX: 12,
  ABOUT_TEXT: 13, // the About block follows in the plugin and is not here
};

const kRed = 0;
const kGreen = 1;
const kBlue = 2;

const kConvergenceHigh = 0.015;
const kColdDriftHigh = 0.01;
const kWarmupLow = 1.0;
const kWarmupHigh = 1800.0;
const kBloomLow = 0.1;
const kBloomHigh = 10.0;
const kAstigmatismHigh = 0.5;
const kSetupLow = 1;
const kSetupHigh = 1000;

const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));
/** Controls.cpp's clamp01: the host's float, as a double, clamped. */
const clamp01 = (v) => clamp(v, 0.0, 1.0);
/**
 * std::round and std::lround: half away from zero, which Math.round is not
 * (and floor(v + 0.5) is wrong for 0.49999999999999994). v - trunc(v) is exact.
 */
function roundHalfAway(v) {
  const t = Math.trunc(v);
  return Math.abs(v - t) >= 0.5 ? t + Math.sign(v) : t;
}
const lround = roundHalfAway;

function geometric(v, low, high) {
  return low * Math.pow(high / low, clamp01(v));
}
function inverseGeometric(value, low, high) {
  const lo = Math.min(low, high);
  const hi = Math.max(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
function inverseLinear(value, high) {
  return f32(clamp(value / high, 0.0, 1.0));
}

const kZoneNames = ['5x5', '9x9'];
const kZoneCounts = [5, 9];

function OptionIndex(value, count) {
  return clamp(lround(value), 0, count - 1);
}

const ConvergenceFromParam = (v) => kConvergenceHigh * clamp01(v);
const ParamFromConvergence = (fraction) => inverseLinear(fraction, kConvergenceHigh);
const ZonesFromParam = (v) => kZoneCounts[OptionIndex(v, 2)];
function SetupFromParam(v) {
  const rounded = roundHalfAway(v);
  // static_cast< int > of a value already clamped to 1..1000: exact.
  return clamp(rounded === rounded ? rounded : 1.0, kSetupLow, kSetupHigh);
}
const KeystoneFromParam = (v) => clamp01(v);
const WarmupFromParam = (v) => geometric(v, kWarmupLow, kWarmupHigh);
const ParamFromWarmup = (seconds) => inverseGeometric(seconds, kWarmupLow, kWarmupHigh);
const ColdDriftFromParam = (v) => kColdDriftHigh * clamp01(v);
const ParamFromColdDrift = (fraction) => inverseLinear(fraction, kColdDriftHigh);
const FocusFromParam = (v) => 2.0 * clamp01(v) - 1.0;
const ParamFromFocus = (delta) => f32(clamp(0.5 * (delta + 1.0), 0.0, 1.0));
const AstigmatismFromParam = (v) => kAstigmatismHigh * clamp01(v);
const ParamFromAstigmatism = (a) => inverseLinear(a, kAstigmatismHigh);
const BloomFromParam = (v) => geometric(v, kBloomLow, kBloomHigh);
const ParamFromBloom = (milliamps) => inverseGeometric(milliamps, kBloomLow, kBloomHigh);

const kDefaultBloom = 0.32;

/**
 * Controls.cpp's kTable: id, name, group, kind, default (the host's 0..1, the
 * element index, or the integer itself), elements, an integer's range.
 */
const standard = (id, name, group, value) => ({ id, name, group, kind: 'standard', value: f32(value), elements: null, low: 0, high: 1 });
const option = (id, name, group, value, names) => ({ id, name, group, kind: 'option', value: f32(value), elements: names, low: 0, high: 1 });
const integer = (id, name, group, value, low, high) => ({ id, name, group, kind: 'integer', value: f32(value), elements: null, low: f32(low), high: f32(high) });
const event = (id, name, group) => ({ id, name, group, kind: 'event', value: 0, elements: null, low: 0, high: 1 });

const kTable = [
  standard(PT.CONVERGENCE, 'Convergence', 'Convergence', ParamFromConvergence(0.001)),
  option(PT.ZONES, 'Zones', 'Convergence', 0.0, kZoneNames),
  integer(PT.SETUP, 'Setup', 'Convergence', 1.0, kSetupLow, kSetupHigh),
  standard(PT.KEYSTONE, 'Keystone', 'Convergence', 0.0),
  standard(PT.WARMUP, 'Warm-up', 'Warm-up', ParamFromWarmup(400.0)),
  standard(PT.COLD_DRIFT, 'Cold Drift', 'Warm-up', ParamFromColdDrift(0.001)),
  event(PT.POWER_ON, 'Power On', 'Warm-up'),
  standard(PT.RED_FOCUS, 'Red Focus', 'Focus', 0.5),
  standard(PT.GREEN_FOCUS, 'Green Focus', 'Focus', 0.5),
  standard(PT.BLUE_FOCUS, 'Blue Focus', 'Focus', 0.5),
  standard(PT.ASTIGMATISM, 'Astigmatism', 'Focus', 0.0),
  standard(PT.BLOOM, 'Bloom', 'Beam', ParamFromBloom(kDefaultBloom)),
  standard(PT.MIX, 'Mix', 'Look', 1.0),
];
if (kTable.length !== PT.ABOUT_TEXT || kTable.some((row, i) => row.id !== i)) {
  throw new Error('the parameter table is not in index order');
}

// ==== PORT: Projector.h and Projector.cpp ====================================

const kPi = 3.14159265358979323846;

const kTubeAngleDeg = 7.8;
const kHalfFieldTan = 1.0 / 3.0;
const KeystoneE = () => kHalfFieldTan * Math.tan((kTubeAngleDeg * kPi) / 180.0);

const kSpotDiameter1mA = 0.099;
const kSpotDiameter4mA = 0.256;
const SpotExponent = () => Math.log(kSpotDiameter4mA / kSpotDiameter1mA) / Math.log(4.0);
const FivePercentDiameterInSigma = () => 2.0 * Math.sqrt(2.0 * Math.log(20.0));
const SigmaOneMilliampMm = () => kSpotDiameter1mA / FivePercentDiameterInSigma();
const kRasterHeightMm = 5.5 * 0.6 * 25.4;
const kFocusScale = 0.01;

const kSaturationMa = 4.0 / 3.0;
const kLayers = 4;
const kMaxRadius = 256;

const SpotSigmaMm = (milliamps, exponent) => SigmaOneMilliampMm() * Math.pow(milliamps, exponent);

/** PCG's RXS-M-XS output permutation of one LCG step, in uint32 arithmetic. */
function Hash(v) {
  const state = (Math.imul(v >>> 0, 747796405) + 2891336453) >>> 0;
  const word = Math.imul(((state >>> ((state >>> 28) + 4)) ^ state) >>> 0, 277803737) >>> 0;
  return ((word >>> 22) ^ word) >>> 0;
}

const UniformOf = (h) => (h + 0.5) * (1.0 / 4294967296.0);

function key(setup, a, b, c, d) {
  let h = Hash(((setup >>> 0) ^ 0x9e3779b9) >>> 0);
  h = Hash((h ^ a) >>> 0);
  h = Hash((h ^ b) >>> 0);
  h = Hash((h ^ c) >>> 0);
  return Hash((h ^ d) >>> 0);
}

/** A standard normal by Box-Muller from two draws of one key. */
function normal(k) {
  const u1 = UniformOf(Hash((k ^ 0x68e31da4) >>> 0));
  const u2 = UniformOf(Hash((k ^ 0xb5297a4d) >>> 0));
  return Math.sqrt(-2.0 * Math.log(u1)) * Math.cos(2.0 * kPi * u2);
}

/**
 * The set errors for Setup `setup` on an n x n grid, unit normal, texel order
 * (j n + i), i across, j up; per point red x, red y, blue x, blue y.
 */
function ZoneTable(setup, n) {
  const table = new Float64Array(n * n * 4);
  for (let j = 0; j < n; j += 1) {
    for (let i = 0; i < n; i += 1) {
      for (let component = 0; component < 4; component += 1) {
        const k = key(setup, n, i, j, component);
        table[(j * n + i) * 4 + component] = normal(k);
      }
    }
  }
  return table;
}

/** The cold drift's direction for a side tube (0 red, 1 blue): a unit vector. */
function DriftDirection(setup, tube) {
  const phi = 2.0 * kPi * UniformOf(key(setup, 0xd21f7, tube, 0, 0));
  return [Math.cos(phi), Math.sin(phi)];
}

/**
 * One frame's optics, in pixels, in double as in the C++. The harness's
 * OpticsHooks are all off as shipped, so they are folded in.
 */
function Compute(s, width, height) {
  const f = { width, height, sigmaX: [0, 0, 0], sigmaY: [0, 0, 0] };
  const h = height;
  f.exponent = SpotExponent();
  f.sigmaOnePx = (SigmaOneMilliampMm() / kRasterHeightMm) * h;
  f.fullWhiteMa = s.fullWhiteMa;
  const big = (SpotSigmaMm(s.fullWhiteMa, f.exponent) / kRasterHeightMm) * h;
  f.layerStep = (big * big) / (kLayers - 1);
  f.keystoneE = KeystoneE();
  f.keystoneK = s.keystone;

  // Sturm's interval: the horizontal line focus at Delta = -a, the vertical
  // at +a; each axis's blur is S times its distance from its focus, added to
  // the focused spot in quadrature.
  const scale = kFocusScale * h;
  const a = s.astigmatism;
  for (let tube = 0; tube < 3; tube += 1) {
    const delta = s.focus[tube];
    const bx = scale * Math.abs(delta + a);
    const by = scale * Math.abs(delta - a);
    // Red and green at their 1 mA spot; blue's beam is in its layers.
    const beam = tube === 2 ? 0.0 : f.sigmaOnePx;
    f.sigmaX[tube] = Math.sqrt(beam * beam + bx * bx);
    f.sigmaY[tube] = Math.sqrt(beam * beam + by * by);
  }
  // The page's, for the line under the picture: the blue spot at full white.
  f.blueSpotPx = big;
  return f;
}

/**
 * erf and erfc, the page's own (std::erf and std::erfc in the plugin). erf
 * below 2 by its series of positive terms, erf x = 2x/sqrt(pi) e^-x^2
 * sum (2x^2)^n / (2n+1)!!, which does not cancel; above, 1 - erfc. erfc below
 * 0.5 as 1 - erf (at least 0.48, so nothing much cancels); above, by its
 * continued fraction (modified Lentz), so the tail keeps its relative
 * precision. A few parts in 1e-15 relative; the crosscheck compares them, and
 * the kernels they give, with the plugin's.
 */
function erfSeries(x) {
  const x2 = x * x;
  let term = 1.0;
  let sum = 1.0;
  for (let n = 1; n < 400; n += 1) {
    term *= (2.0 * x2) / (2 * n + 1);
    sum += term;
    if (term < 1e-17 * sum) break;
  }
  return ((2.0 * x) / Math.sqrt(kPi)) * Math.exp(-x2) * sum;
}
function erfcFraction(x) {
  // erfc x = exp( -x^2 ) / sqrt(pi) / ( x + (1/2) / ( x + 1 / ( x + (3/2) / ( x + ... ))))
  const tiny = 1e-300;
  let f = x;
  let C = x;
  let D = 0.0;
  for (let n = 1; n < 4000; n += 1) {
    const an = n / 2;
    D = x + an * D;
    if (D === 0) D = tiny;
    C = x + an / C;
    if (C === 0) C = tiny;
    D = 1 / D;
    const delta = C * D;
    f *= delta;
    if (Math.abs(delta - 1) < 1e-16) break;
  }
  return Math.exp(-x * x) / Math.sqrt(kPi) / f;
}
function erf(x) {
  const ax = Math.abs(x);
  if (ax === 0) return x;
  const out = ax < 2.0 ? erfSeries(ax) : 1.0 - erfcFraction(ax);
  return x < 0 ? -out : out;
}
function erfc(x) {
  if (x < 0) return 2.0 - erfc(-x);
  if (x > 27.3) return 0.0; // below the smallest double
  return x < 0.5 ? 1.0 - erfSeries(x) : erfcFraction(x);
}

/** One pixel-integrated Gaussian, cut at ceil(6 sigma) (at most `cap`) and normalised; sigma 0 is the identity. */
function PixelGaussian(sigma, cap) {
  if (!(sigma > 0.0)) return new Float64Array([1.0]);
  const radius = Math.min(cap, Math.trunc(Math.ceil(6.0 * sigma)));
  const w = new Float64Array(2 * radius + 1);
  const k = 1.0 / (sigma * Math.sqrt(2.0));
  let sum = 0.0;
  for (let i = -radius; i <= radius; i += 1) {
    // Phi((i + 1/2)/sigma) - Phi((i - 1/2)/sigma), from the tail that does
    // not cancel.
    const lo = (Math.abs(i) - 0.5) * k;
    const hi = (Math.abs(i) + 0.5) * k;
    const v = i === 0 ? erf(0.5 * k) : 0.5 * (erfc(lo) - erfc(hi));
    w[i + radius] = v;
    sum += v;
  }
  for (let i = 0; i < w.length; i += 1) w[i] /= sum;
  return w;
}

/**
 * The blur passes' kernels, in double, then stored as floats. Rows: 0
 * horizontal over texture A (red, green, the current, the drive), 1
 * horizontal over B (the four blue layers), 2 and 3 the same vertically.
 * Texel (i + radius, row).
 */
function BuildKernels(f) {
  const k = { radius: 0, texels: null, sigma: [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]] };
  for (let axis = 0; axis < 2; axis += 1) {
    const tubes = axis === 0 ? f.sigmaX : f.sigmaY;
    const a = k.sigma[axis * 2];
    const b = k.sigma[axis * 2 + 1];
    a[0] = tubes[0];
    a[1] = tubes[1];
    a[2] = 0.0;
    a[3] = 0.0;
    for (let layer = 0; layer < kLayers; layer += 1) b[layer] = Math.sqrt(tubes[2] * tubes[2] + layer * f.layerStep);
  }
  const kernels = [[], [], [], []];
  for (let row = 0; row < 4; row += 1) {
    for (let c = 0; c < 4; c += 1) {
      kernels[row][c] = PixelGaussian(k.sigma[row][c], kMaxRadius);
      k.radius = Math.max(k.radius, Math.trunc(kernels[row][c].length / 2));
    }
  }
  const width = 2 * k.radius + 1;
  // A Float32Array rounds each double as static_cast< float > does.
  k.texels = new Float32Array(width * 4 * 4);
  for (let row = 0; row < 4; row += 1) {
    for (let c = 0; c < 4; c += 1) {
      const w = kernels[row][c];
      const r = Math.trunc(w.length / 2);
      for (let i = -r; i <= r; i += 1) k.texels[(row * width + (i + k.radius)) * 4 + c] = w[i + r];
    }
  }
  return k;
}

// ==== END PORT ================================================================

// ==== FRAME: Convergence.cpp, the CPU half ====================================

/** Frames that must agree before the host's clock unit is settled. */
const kClockVotes = 4;
/** Frames of advancing host time after which an unsettled vote is decided by the step's size. */
const kClockPatience = 30;
/** Host seconds a single frame may advance by. */
const kMaxFrameDelta = 0.1;

const wallStartMs = performance.now();
const wallSeconds = () => (performance.now() - wallStartMs) / 1000;

/** glUniform2i by name, counted as missing the way the kit's setters count. */
function setIvec2(program, name, a, b) {
  const location = program.location(name);
  if (location === null) {
    program.missing.add(name);
    return;
  }
  program.gl.uniform2i(location, a | 0, b | 0);
}

/** GLState.h's bindUnit and unbindTextureUnits. */
function bindUnit(gl, unit, texture) {
  bindTexture(gl, unit, texture);
}
function unbindTextureUnits(gl, count) {
  for (let u = count - 1; u >= 0; u -= 1) {
    gl.activeTexture(gl.TEXTURE0 + u);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
}

/** Convergence.cpp's makeDataTexture: a float texture for data read texel for texel. */
function makeDataTexture(gl) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return texture;
}

/** Convergence.cpp's uploadData, and WebGL's own two unpack flags put to the plugin's. */
function uploadData(gl, texture, width, height, texels) {
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, texels);
  gl.bindTexture(gl.TEXTURE_2D, null);
}

/**
 * InitGL: AssembleVertex() is kVersion + kQuadVertex for every pass, and each
 * fragment stage is Assemble( pass ), as Shaders.cpp joins it. The triangle
 * comes from gl_VertexID alone, so no attribute is bound.
 */
function compilePrograms(gl) {
  const vertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = {
    prep: S.VERSION + S.COMMON + S.PREP_FRAGMENT,
    blur: S.VERSION + S.BLUR_FRAGMENT,
    composite: S.VERSION + S.COMMON + S.COMPOSITE_FRAGMENT,
  };
  const programs = {};
  for (const [name, fragment] of Object.entries(assemble)) programs[name] = new Program(gl, vertex, fragment, name, { attribs: {} });
  // A core profile draws nothing without a vertex array bound; the triangle
  // needs no attributes.
  programs.vao = gl.createVertexArray();
  return programs;
}

/** One SW Convergence instance. */
class ConvergenceInstance {
  constructor(gl, programs) {
    this.gl = gl;
    this.programs = programs;
    this.vao = programs.vao;
    // The plugin's float params[]: a Float32Array rounds as the host's float does.
    this.params = new Float32Array(PT.ABOUT_TEXT);
    for (let id = 0; id < PT.ABOUT_TEXT; id += 1) this.params[id] = kTable[id].value;
    this.frameIndex = 0;

    this.frame = null;
    this.kernels = { radius: 0, texels: new Float32Array(16), sigma: null };
    this.kernelsUploaded = false;
    this.kernelKey = new Float64Array(7);

    // The installation: the unit-normal grid for (setup, n), cached.
    this.tableSetup = -1;
    this.tableCount = 0;
    this.unitTable = new Float64Array(0);
    this.zoneTexels = new Float32Array(0);
    this.zoneCount = 0;
    this.drift = new Float64Array(4);

    const nearest = () => new PassBuffer(gl, { filter: 'nearest' });
    // tubes: A (red, green, current); layers: B (the blue layers). [0] the
    // result, [1] the horizontal pass's.
    this.tubes = [nearest(), nearest()];
    this.layers = [nearest(), nearest()];
    this.lastWidth = 0;
    this.lastHeight = 0;
    this.zoneTexture = makeDataTexture(gl);
    this.kernelTexture = makeDataTexture(gl);

    // The warm-up: the fraction of the cold drift left, in double.
    this.warm = 1.0;
    this.powerOnHeld = false;
    this.powerOnPending = false;

    // The clock.
    this.hostTime = -1.0;
    this.clockScale = 0.0;
    this.lastRawTime = -1.0;
    this.lastWallTime = -1.0;
    this.wallStart = -1.0;
    this.secondsVotes = 0;
    this.millisVotes = 0;
    this.clockSamples = 0;
    this.settledJump = false;
    this.now = 0.0;
    this.lastNow = -1.0;

    // The page's, for the line under the picture: the last frame's length.
    this.lastDt = 0.0;
  }

  /** The About block's indices are not here: a web page has links of its own. */
  SetFloatParameter(index, value) {
    if (index < 0 || index >= PT.ABOUT_TEXT) return false;
    // Power On acts on the press (an event is 1 on press, 0 on release): the
    // next frame shows the set cold.
    if (index === PT.POWER_ON) {
      const down = f32(value) > 0.5;
      if (down && !this.powerOnHeld) this.powerOnPending = true;
      this.powerOnHeld = down;
    }
    this.params[index] = value;
    return true;
  }

  GetFloatParameter(index) {
    return index >= 0 && index < PT.ABOUT_TEXT ? this.params[index] : 0.0;
  }

  SetTime(time) {
    this.hostTime = time;
  }

  /** The harness's SetClockScaleForTest, for the crosscheck. */
  SetClockScaleForTest(scale) {
    this.clockScale = scale;
  }

  /** The controls in physical units. */
  CurrentSettings() {
    const p = this.params;
    return {
      focus: [FocusFromParam(p[PT.RED_FOCUS]), FocusFromParam(p[PT.GREEN_FOCUS]), FocusFromParam(p[PT.BLUE_FOCUS])],
      astigmatism: AstigmatismFromParam(p[PT.ASTIGMATISM]),
      fullWhiteMa: BloomFromParam(p[PT.BLOOM]),
      keystone: KeystoneFromParam(p[PT.KEYSTONE]),
    };
  }

  UpdateClock() {
    const wallNow = wallSeconds();
    if (this.wallStart < 0.0) this.wallStart = wallNow;
    const raw = this.hostTime;

    if (this.clockScale === 0.0 && raw >= 0.0 && this.lastRawTime >= 0.0 && this.lastWallTime >= 0.0) {
      const hostDelta = raw - this.lastRawTime;
      const wallDelta = wallNow - this.lastWallTime;
      if (hostDelta > 0.0 && wallDelta >= 0.0005) {
        const ratio = hostDelta / wallDelta;
        if (ratio > 0.1 && ratio < 10.0) this.secondsVotes += 1;
        else if (ratio > 100.0 && ratio < 10000.0) this.millisVotes += 1;
        if (this.secondsVotes >= kClockVotes || this.millisVotes >= kClockVotes) {
          this.clockScale = this.millisVotes > this.secondsVotes ? 0.001 : 1.0;
          this.settledJump = true; // wall time to the host's own: no dt across it
        }
      }
      if (hostDelta > 0.0 && this.clockScale === 0.0 && (this.clockSamples += 1) >= kClockPatience) {
        this.clockScale = hostDelta < 1.0 ? 1.0 : 0.001;
        this.settledJump = true;
      }
    }
    if (raw >= 0.0) this.lastRawTime = raw;
    this.lastWallTime = wallNow;

    // Double throughout; only differences are used.
    this.now = raw >= 0.0 && this.clockScale !== 0.0 ? raw * this.clockScale : wallNow - this.wallStart;
  }

  ensureBuffers(width, height) {
    const gl = this.gl;
    for (const b of this.tubes) b.ensure(width, height, gl.RGBA32F);
    for (const b of this.layers) b.ensure(width, height, gl.RGBA32F);
    return true;
  }

  draw(target) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, target.width, target.height);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  uploadZones(width) {
    const setup = SetupFromParam(this.params[PT.SETUP]);
    const n = ZonesFromParam(this.params[PT.ZONES]);
    if (setup !== this.tableSetup || n !== this.tableCount) {
      this.unitTable = ZoneTable(setup, n);
      this.tableSetup = setup;
      this.tableCount = n;
    }
    // The RMS set error, a fraction of the width, in pixels.
    const rms = ConvergenceFromParam(this.params[PT.CONVERGENCE]) * width;
    this.zoneCount = n;
    if (this.zoneTexels.length !== this.unitTable.length) this.zoneTexels = new Float32Array(this.unitTable.length);
    for (let i = 0; i < this.unitTable.length; i += 1) this.zoneTexels[i] = this.unitTable[i] * rms;
    uploadData(this.gl, this.zoneTexture, n, n, this.zoneTexels);
  }

  uploadKernels() {
    // The kernels depend on these alone; rebuilt and uploaded only when one changes.
    const f = this.frame;
    const k = [f.sigmaX[0], f.sigmaX[1], f.sigmaX[2], f.sigmaY[0], f.sigmaY[1], f.sigmaY[2], f.layerStep];
    if (this.kernelsUploaded && k.every((v, i) => Object.is(v, this.kernelKey[i]))) return;
    this.kernelKey.set(k);
    this.kernels = BuildKernels(f);
    uploadData(this.gl, this.kernelTexture, 2 * this.kernels.radius + 1, 4, this.kernels.texels);
    this.kernelsUploaded = true;
  }

  /**
   * ProcessOpenGL. `input` is { texture, width, height }; the composite goes
   * into `hostFBO` (null: the canvas) at a viewport of the input's size from
   * the origin. Returns false where the plugin returns FF_FAIL.
   */
  ProcessOpenGL(input, hostFBO = null) {
    const gl = this.gl;
    if (!input || !input.texture) return false;
    gl.disable(gl.BLEND);
    const width = input.width | 0;
    const height = input.height | 0;
    if (width <= 0 || height <= 0 || !this.vao) return false;

    this.UpdateClock();
    if (this.settledJump) {
      this.lastNow = -1.0;
      this.settledJump = false;
    }
    let dt = this.lastNow >= 0.0 ? clamp(this.now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = this.now;
    // To the microsecond (std::round: dt is never negative here, where
    // Math.round agrees with it).
    dt = Math.round(dt * 1e6) * 1e-6;
    this.lastDt = dt;

    // The warm-up: s <- s exp(-dt/tau). Products of exponentials compose, so
    // the drift at a time is the same at any frame rate; a paused clock holds it.
    const tau = WarmupFromParam(this.params[PT.WARMUP]);
    this.warm *= Math.exp(-dt / tau);
    if (this.powerOnPending) this.warm = 1.0;
    this.powerOnPending = false;
    this.lastWidth = width;
    this.lastHeight = height;

    const setup = SetupFromParam(this.params[PT.SETUP]);
    const cold = ColdDriftFromParam(this.params[PT.COLD_DRIFT]) * width;
    const amount = this.warm * cold;
    for (let tube = 0; tube < 2; tube += 1) {
      const dir = DriftDirection(setup, tube);
      this.drift[tube * 2] = amount * dir[0];
      this.drift[tube * 2 + 1] = amount * dir[1];
    }

    this.frame = Compute(this.CurrentSettings(), width, height);
    this.uploadKernels();
    this.uploadZones(width);

    this.render(input, [0, 0, width, height], hostFBO, width, height);
    this.frameIndex = (this.frameIndex + 1) >>> 0;
    return true;
  }

  render(input, hostViewport, hostFBO, width, height) {
    const gl = this.gl;
    const P = this.programs;
    if (!this.ensureBuffers(width, height)) return;
    gl.bindVertexArray(this.vao);
    const f = this.frame;

    // 1. prep: A (red, green, the blue current), then B (the blue layers)
    P.prep.use();
    bindUnit(gl, 0, input.texture);
    P.prep.setSampler('InputTexture', 0);
    P.prep.setInt('Premultiply', 1);
    P.prep.setInt('ClampOn', 1);
    P.prep.setInt('SaturationModel', 0);
    P.prep.setInt('LayerModel', 0);
    P.prep.set('Is', kSaturationMa);
    P.prep.set('Iw', f.fullWhiteMa);
    P.prep.set('IsIw', kSaturationMa * f.fullWhiteMa);
    P.prep.set('SigmaOne2', f.sigmaOnePx * f.sigmaOnePx);
    P.prep.set('TwoP', 2.0 * f.exponent);
    P.prep.set('LayerStep', f.layerStep);
    P.prep.setInt('Output', 0);
    this.draw(this.tubes[0]);
    P.prep.setInt('Output', 1);
    this.draw(this.layers[0]);

    // 2. blur: along x into [1], along y back into [0]
    P.blur.use();
    bindUnit(gl, 1, this.kernelTexture);
    P.blur.setSampler('Source', 0);
    P.blur.setSampler('Kernel', 1);
    P.blur.setInt('Radius', this.kernels.radius);
    setIvec2(P.blur, 'Size', width, height);
    const blurs = [
      { from: this.tubes[0], to: this.tubes[1], row: 0, dx: 1, dy: 0 },
      { from: this.layers[0], to: this.layers[1], row: 1, dx: 1, dy: 0 },
      { from: this.tubes[1], to: this.tubes[0], row: 2, dx: 0, dy: 1 },
      { from: this.layers[1], to: this.layers[0], row: 3, dx: 0, dy: 1 },
    ];
    for (const b of blurs) {
      bindUnit(gl, 0, b.from.texture);
      P.blur.setInt('Row', b.row);
      setIvec2(P.blur, 'Direction', b.dx, b.dy);
      this.draw(b.to);
    }

    // 3. composite, into the host's framebuffer and viewport
    gl.bindFramebuffer(gl.FRAMEBUFFER, hostFBO);
    gl.viewport(hostViewport[0], hostViewport[1], hostViewport[2], hostViewport[3]);
    P.composite.use();
    bindUnit(gl, 0, input.texture);
    bindUnit(gl, 1, this.tubes[0].texture);
    bindUnit(gl, 2, this.layers[0].texture);
    bindUnit(gl, 3, this.zoneTexture);
    P.composite.setSampler('InputTexture', 0);
    P.composite.setSampler('Tubes', 1);
    P.composite.setSampler('Layers', 2);
    P.composite.setSampler('Zones', 3);
    P.composite.setInt('ZoneCount', this.zoneCount);
    P.composite.setInt('ZoneModel', 0);
    P.composite.set('Drift', f32(this.drift[0]), f32(this.drift[1]), f32(this.drift[2]), f32(this.drift[3]));
    P.composite.set('KeystoneE', f.keystoneE);
    P.composite.set('KeystoneK', f.keystoneK);
    P.composite.setInt('KeystoneModel', 0);
    setIvec2(P.composite, 'ViewOrigin', hostViewport[0], hostViewport[1]);
    setIvec2(P.composite, 'Size', width, height);
    P.composite.set('MixAmount', clamp(this.params[PT.MIX], 0.0, 1.0));
    P.composite.setInt('AlphaModel', 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    unbindTextureUnits(gl, 4);
    gl.activeTexture(gl.TEXTURE0);
    gl.useProgram(null);
    // ScopedGLState's part that matters to the page: no vertex array left bound.
    gl.bindVertexArray(null);
  }

  /** The frame's mean blue-gun current, mA, read from texture A (cvtest's MeanCurrent). */
  readMeanCurrent() {
    const gl = this.gl;
    const t = this.tubes[0];
    const raw = new Float32Array(t.width * t.height * 4);
    gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
    gl.readPixels(0, 0, t.width, t.height, gl.RGBA, gl.FLOAT, raw);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    let sum = 0.0;
    for (let p = 0; p < raw.length / 4; p += 1) sum += raw[4 * p + 2];
    return sum / (raw.length / 4);
  }

  /** DeInitGL. */
  dispose() {
    const gl = this.gl;
    for (const t of [this.zoneTexture, this.kernelTexture]) if (t) gl.deleteTexture(t);
    this.zoneTexture = null;
    this.kernelTexture = null;
    for (const b of [...this.tubes, ...this.layers]) b.dispose();
  }
}

// ==== END FRAME ===============================================================

//===========================================================================
// For demo/tools/crosscheck/ and the flash measurement: a hook a headless
// Chrome drives. Nothing on the page calls it.
//===========================================================================

/** RGBA8 given top row first, uploaded as cvtest uploads a frame: float, value / 255, RGBA32F, LINEAR. */
function uploadFloatFrame(gl, rgba, width, height, texture = null) {
  const t = texture ?? gl.createTexture();
  const row = width * 4;
  const pixels = new Float32Array(row * height);
  for (let y = 0; y < height; y += 1) {
    const from = y * row;
    const to = (height - 1 - y) * row;
    for (let x = 0; x < row; x += 1) pixels[to + x] = rgba[from + x] / 255;
  }
  gl.bindTexture(gl.TEXTURE_2D, t);
  if (!texture) {
    // Every pass reads it by texelFetch, so the filter never acts; but a float
    // texture set to LINEAR without OES_texture_float_linear is incomplete,
    // and an incomplete texture fetches ( 0, 0, 0, 1 ).
    const filter = gl.getExtension('OES_texture_float_linear') ? gl.LINEAR : gl.NEAREST;
    gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA32F, width, height);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  }
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.FLOAT, pixels);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return t;
}

/** An RGBA32F target's pixels, quantised as cvtest --pipe quantises: lround( clamp( v ) x 255 ), top row first. */
function readQuantised(gl, target) {
  const { width, height } = target;
  gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
  const raw = new Float32Array(width * height * 4);
  gl.readPixels(0, 0, width, height, gl.RGBA, gl.FLOAT, raw);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  const row = width * 4;
  const out = new Uint8Array(row * height);
  for (let y = 0; y < height; y += 1) {
    const from = (height - 1 - y) * row;
    const to = y * row;
    // lround( clamp( v, 0.0f, 1.0f ) * 255.0f ): the product rounded to a float first.
    for (let x = 0; x < row; x += 1) out[to + x] = lround(f32(clamp(raw[from + x], 0, 1) * 255));
  }
  return out;
}

/** The uniforms a program declares that nothing has set: each would sit at 0. */
function unsetUniforms(gl, program) {
  const set = new Set([...program.locations].filter(([, loc]) => loc !== null).map(([name]) => name));
  const out = [];
  const count = gl.getProgramParameter(program.program, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < count; i += 1) {
    const name = gl.getActiveUniform(program.program, i).name;
    if (!set.has(name) && !set.has(name.replace(/\[0\]$/, ''))) out.push(name);
  }
  return out;
}

const LINEAR = new Float64Array(256).map((_, i) => {
  const s = i / 255;
  return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
});

/**
 * flashcheck.py's levels, for one frame of RGBA8 (top row first) as the
 * browser shows it over a backdrop of `bg` (0 black, 1 white): the canvas is
 * premultiplied, so what shows is the colour plus the backdrop times (1 - a),
 * in code values. Then the whole frame's mean linear luminance, then each of
 * the 5 x 5 windows a third of the frame each way, stepped by a sixth,
 * row-major.
 */
function flashLevels(pixels, width, height, bg) {
  const ww = Math.floor(width / 3);
  const wh = Math.floor(height / 3);
  const sx = Math.floor(width / 6);
  const sy = Math.floor(height / 6);
  const y = new Float64Array(width * height);
  let total = 0;
  const shown = (c, a) => Math.min(255, Math.round(c + bg * (255 - a)));
  for (let p = 0; p < y.length; p += 1) {
    const a = pixels[4 * p + 3];
    const v = 0.2126 * LINEAR[shown(pixels[4 * p], a)] + 0.7152 * LINEAR[shown(pixels[4 * p + 1], a)]
      + 0.0722 * LINEAR[shown(pixels[4 * p + 2], a)];
    y[p] = v;
    total += v;
  }
  const row = [total / y.length];
  for (let j = 0; j < 5; j += 1) {
    for (let i = 0; i < 5; i += 1) {
      let sum = 0;
      for (let yy = j * sy; yy < j * sy + wh; yy += 1) for (let xx = i * sx; xx < i * sx + ww; xx += 1) sum += y[yy * width + xx];
      row.push(sum / (ww * wh));
    }
  }
  return row;
}

/** cvtest's applySetting: a number (strtof, a float), or an option by its element's name. */
function applySettings(run, settings) {
  for (const [name, value] of Object.entries(settings)) {
    const info = kTable.find((row) => row.name === name);
    if (!info) throw new Error(`no parameter called '${name}'`);
    const index = info.elements ? info.elements.indexOf(value) : -1;
    const number = index >= 0 ? index : Number(value);
    if (!Number.isFinite(number)) throw new Error(`'${value}' is not a number or an option of ${name}`);
    run.SetFloatParameter(info.id, f32(number));
  }
}

function installHook(gl, programs) {
  window.convergenceDemo = {
    table: kTable.map((row) => ({ name: row.name, kind: row.kind, value: row.value, elements: row.elements ?? [],
      low: row.low, high: row.high })),

    /**
     * The port's CPU numbers for a raster and settings (host values by name,
     * as cvtest --set takes them), for cvdump's: the settings, Compute's
     * frame, the kernels, the zone table and its texels, the drift
     * directions, and the uniforms as the floats the GPU receives.
     */
    cpu({ settings = {}, width, height }) {
      const run = new ConvergenceInstance(gl, programs);
      try {
        applySettings(run, settings);
        const p = run.params;
        const s = run.CurrentSettings();
        const fr = Compute(s, width, height);
        const k = BuildKernels(fr);
        const setup = SetupFromParam(p[PT.SETUP]);
        const n = ZonesFromParam(p[PT.ZONES]);
        const unit = ZoneTable(setup, n);
        const rms = ConvergenceFromParam(p[PT.CONVERGENCE]) * width;
        const cold = ColdDriftFromParam(p[PT.COLD_DRIFT]) * width;
        const dirs = [DriftDirection(setup, 0), DriftDirection(setup, 1)];
        return {
          params: [...p],
          conversions: {
            convergence: ConvergenceFromParam(p[PT.CONVERGENCE]), zones: n, setup,
            keystone: KeystoneFromParam(p[PT.KEYSTONE]), warmup: WarmupFromParam(p[PT.WARMUP]),
            coldDrift: ColdDriftFromParam(p[PT.COLD_DRIFT]), redFocus: s.focus[0], greenFocus: s.focus[1],
            blueFocus: s.focus[2], astigmatism: s.astigmatism, bloom: s.fullWhiteMa,
          },
          frame: { sigmaOnePx: fr.sigmaOnePx, exponent: fr.exponent, sigmaX: fr.sigmaX, sigmaY: fr.sigmaY,
            layerStep: fr.layerStep, fullWhiteMa: fr.fullWhiteMa, keystoneE: fr.keystoneE, keystoneK: fr.keystoneK },
          kernels: { radius: k.radius, sigma: k.sigma, texels: [...k.texels] },
          zones: { n, setup, unit: [...unit], texels: [...unit].map((v) => f32(v * rms)) },
          drift: { dirs, uniform: [f32(cold * dirs[0][0]), f32(cold * dirs[0][1]), f32(cold * dirs[1][0]), f32(cold * dirs[1][1])] },
          uniforms: [kSaturationMa, fr.fullWhiteMa, kSaturationMa * fr.fullWhiteMa, fr.sigmaOnePx * fr.sigmaOnePx,
            2.0 * fr.exponent, fr.layerStep, fr.keystoneE, fr.keystoneK, clamp(p[PT.MIX], 0, 1)].map(f32),
        };
      } finally {
        run.dispose();
      }
    },

    /** The page's erf and erfc at each x, for cvdump's std::erf and std::erfc. */
    erf(xs) {
      return xs.map((x) => [erf(x), erfc(x)]);
    },

    /** PixelGaussian( sigma, kMaxRadius ) for each sigma, in double, for cvdump's. */
    gauss(sigmas) {
      return sigmas.map((sigma) => [...PixelGaussian(sigma, kMaxRadius)]);
    },

    /**
     * cvtest --pipe's run, in the page: a FRESH instance on a seconds clock
     * (the rig's SetClockScaleForTest( 1.0 )), the settings by display name,
     * `changes` ([ [ frame, { settings } ], ... ], cvtest's --script cues),
     * then one frame per picture (RGBA8, top row first) at SetTime( f / fps ),
     * each composited into an RGBA32F target and read back quantised as cvtest
     * writes it. With each frame, --stats's two numbers: the mean blue-gun
     * current and the warm-up's state.
     */
    renderRun({ settings = {}, changes = [], width, height, fps = 60, frames, negative = null }) {
      const run = new ConvergenceInstance(gl, programs);
      run.SetClockScaleForTest(1.0);
      const host = new PassBuffer(gl, { filter: 'nearest' });
      let texture = null;
      try {
        applySettings(run, settings);
        host.ensure(width, height, gl.RGBA32F);
        const pictures = [];
        const stats = [];
        let ok = true;
        frames.forEach((rgba, f) => {
          for (const [at, change] of changes) {
            if (at !== f) continue;
            // A negative control for the crosscheck: the page never sees the press.
            if (negative === 'power-ignored' && 'Power On' in change) continue;
            applySettings(run, change);
          }
          texture = uploadFloatFrame(gl, rgba, width, height, texture);
          run.SetTime(f / fps);
          host.clearTo(0, 0, 0, 0);
          ok = run.ProcessOpenGL({ texture, width, height }, host.fbo) && ok;
          pictures.push(readQuantised(gl, host));
          stats.push([run.readMeanCurrent(), run.warm]);
        });
        const unset = Object.entries(programs).filter(([k]) => k !== 'vao').flatMap(([k, p]) => unsetUniforms(gl, p).map((u) => `${k}: ${u}`));
        const missing = Object.entries(programs).filter(([k]) => k !== 'vao').flatMap(([k, p]) => [...p.missing].map((u) => `${k}: ${u}`));
        return { ok, pictures, stats, missing, unset, error: gl.getError(), radius: run.kernels.radius, zones: run.zoneCount };
      } finally {
        if (texture) gl.deleteTexture(texture);
        host.dispose();
        run.dispose();
      }
    },

    /**
     * The flash measurement: the page's own clip and a fresh instance on the
     * page's clock arithmetic (t = f / fps), `seconds` long, read back from an
     * RGBA8 target as the canvas would hold it, shown over a backdrop `bg` (0
     * black, 1 white), reduced to flashcheck.py's levels. `changes` is
     * [ [ frame, { settings } ], ... ] applied as a visitor would apply them.
     */
    flashRun({ clip = 'scene', settings = {}, changes = [], width = 960, height = 540, fps = 60, seconds = 10, bg = 0 }) {
      const run = new ConvergenceInstance(gl, programs);
      const sources = new SourceRenderer(gl);
      const host = new PassBuffer(gl, { filter: 'nearest' });
      const source = SOURCES.find((s) => s.id === clip);
      if (!source) throw new Error(`no clip ${clip}`);
      try {
        applySettings(run, settings);
        host.ensure(width, height, gl.RGBA8);
        const levels = [];
        const n = Math.round(seconds * fps);
        const row = width * 4;
        const read = new Uint8Array(row * height);
        const top = new Uint8Array(row * height);
        for (let f = 0; f < n; f += 1) {
          for (const [at, change] of changes) if (at === f) applySettings(run, change);
          const input = sources.render(source, width, height, f / fps);
          run.SetTime(f / fps);
          host.clearTo(0, 0, 0, 0);
          run.ProcessOpenGL(input, host.fbo);
          gl.bindFramebuffer(gl.FRAMEBUFFER, host.fbo);
          gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, read);
          for (let y = 0; y < height; y += 1) top.set(read.subarray((height - 1 - y) * row, (height - y) * row), y * row);
          levels.push(flashLevels(top, width, height, bg));
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return { levels, error: gl.getError() };
      } finally {
        sources.dispose();
        host.dispose();
        run.dispose();
      }
    },
  };
}

//===========================================================================
// The parameters: the plugin's own, in its declaration's order and groups,
// with its names, element lists and defaults. Hints from the README and the
// spec.
//===========================================================================

const V = (v) => f32(v);

/** Setup is FF_TYPE_INTEGER 1..1000; the kit has no integer control, so it is a dropdown of all 1000, slot s holding s + 1. */
const kSetupElements = Array.from({ length: kSetupHigh - kSetupLow + 1 }, (_, i) => String(i + kSetupLow));
const setupSlot = (value) => value - kSetupLow;

function secondsText(s) {
  if (s < 60) return `τ ${s < 10 ? s.toFixed(1) : s.toFixed(0)} s`;
  return `τ ${(s / 60).toFixed(s < 600 ? 1 : 0)} min`;
}
function focusText(v) {
  const delta = FocusFromParam(V(v));
  if (Math.abs(delta) < 0.0005) return 'sharp';
  return `Δ ${delta > 0 ? '+' : '−'}${Math.abs(delta).toFixed(2)}`;
}

const SPEC = [
  ['convergence', PT.CONVERGENCE, (v) => `${(ConvergenceFromParam(V(v)) * 100).toFixed(2)}% width`,
    'The technician’s error at each zone point, RMS, 0 to 1.5% of the picture width. The default, 0.1%, is about one crosshatch line of a 1024-line raster (the NEC setup guide’s “one or two test pattern line thicknesses”): a converged set. The errors are a fixed installation, not noise: they do not move.'],
  ['zones', PT.ZONES, null,
    'How many zone points a side: 5x5 like a Barco Cine 8 or a Sony VPH-G90, 9x9 like a Sony KP-43T70. Between the points the board interpolates bilinearly. The grid’s size is part of the installation: 9x9 is not 5x5 with more points.'],
  ['setup', PT.SETUP, null,
    'Which installation, 1 to 1000: the same number is the same grid of errors everywhere, at any raster. FF_TYPE_INTEGER in the plugin; the kit has no integer control, so it is a dropdown of all 1000.'],
  ['keystone', PT.KEYSTONE, (v) => `${(KeystoneFromParam(V(v)) * 100).toFixed(0)}% uncorrected`,
    'How much of the side tubes’ trapezoid the set leaves uncorrected, 0 to 1. Each side tube sees the screen from 7.8°, so its picture is a trapezoid; red and blue lean opposite ways, so at 1 a line at the right-hand edge has its red about 2% of the width to one side of green and its blue about 2% to the other: some 4.5% apart.'],
  ['warmup', PT.WARMUP, (v) => secondsText(WarmupFromParam(V(v))),
    'The warm-up’s time constant, 1 s to 30 minutes, geometric. The default 400 s makes Barco’s 20-minute warm-up three of them. The drift left falls as exp(−t/τ) of the host’s time: it closes up slowly, so press Power On with a short Warm-up to watch it.'],
  ['cold', PT.COLD_DRIFT, (v) => `${(ColdDriftFromParam(V(v)) * 100).toFixed(2)}% width`,
    'How far off the side tubes start when the set is cold, 0 to 1% of the width, each in its own direction (from Setup). It decays as the set warms.'],
  ['power', PT.POWER_ON, null,
    'The set is cold again: the drift goes back to Cold Drift and starts closing up. FF_TYPE_EVENT in the plugin, a button in the host; the kit has no event control, so it is a toggle the page releases after one frame — one press, one rising edge, which is what the plugin acts on.'],
  ['redFocus', PT.RED_FOCUS, focusText,
    'The red tube’s focus: 0.5 is sharp, either side softens by the same amount until Astigmatism tells the two sides apart. Δ = 2v − 1; the blur is 1% of the picture height per unit of Δ.'],
  ['greenFocus', PT.GREEN_FOCUS, focusText, 'The green tube’s focus, as Red Focus.'],
  ['blueFocus', PT.BLUE_FOCUS, focusText, 'The blue tube’s focus, as Red Focus.'],
  ['astigmatism', PT.ASTIGMATISM, (v) => `a ${AstigmatismFromParam(V(v)).toFixed(3)}`,
    'Each gun’s astigmatism, a = 0 to 0.5 of the focus range: the horizontal line focus sits at Δ = −a and the vertical one at +a (Sturm’s interval). So with Astigmatism up, a tube focused below 0.5 has sharp vertical lines and soft horizontal ones, and above 0.5 the other way round. At 0.5 exactly both are soft.'],
  ['bloom', PT.BLOOM, (v) => `${BloomFromParam(V(v)).toPrecision(2)} mA`,
    'The blue gun’s current at full white, 0.1 to 10 mA, geometric; the default 0.32 mA is a 1989 patent’s blue current for white. The blue phosphor saturates, so a calibrated set draws disproportionate current for bright blue, and the blue spot grows with its current. It is small, because the real thing is: at 10 mA the blue spot’s sigma is 1.26 px at 1080p. Look for it on fine, bright blue detail, at 1920 × 1080, with the page zoomed in.'],
  ['mix', PT.MIX, (v) => `${(clamp01(V(v)) * 100).toFixed(0)}%`,
    'The projector against the clip, premultiplied. At 0 the clip comes through untouched; at 1 the output is opaque: the projected picture fills the screen, black where no gun is driven, even over a transparent part of the clip. So moving Mix between 0 and 1 three or more times a second over the transparent clip with White behind it flashes that part between white and black (the warning at the top of the page).'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const row = kTable[slot];
  let p;
  if (row.kind === 'integer') {
    p = { id, slot, name: row.name, type: 'option', elements: kSetupElements, default: setupSlot(row.value), group: row.group, hint };
  } else if (row.kind === 'event') {
    p = { id, slot, name: row.name, type: 'boolean', default: 0, group: row.group, hint };
  } else {
    p = { id, slot, name: row.name, type: row.kind, default: row.value, group: row.group, hint };
    if (row.elements) p.elements = row.elements;
  }
  if (display) p.display = display;
  return p;
});

/** The page's value for a control, as the host would send it: Setup's slot back to its integer. */
function hostValue(p, value) {
  return p.slot === PT.SETUP ? f32(value + kSetupLow) : f32(value);
}

//===========================================================================
// The page.
//===========================================================================

const page = { instance: null, stats: '' };

function pxText(v) {
  const a = Math.abs(v);
  return `${a < 10 ? a.toFixed(2) : a.toFixed(1)} px`;
}

function createRenderer(gl) {
  const programs = compilePrograms(gl);
  const instance = new ConvergenceInstance(gl, programs);
  page.instance = instance;
  installHook(gl, programs);

  return {
    render({ input, params, time }) {
      // The host restates every parameter each frame; Power On is an event,
      // pressed here when the page's toggle is on and released below.
      let pressed = false;
      for (const p of PARAMS) {
        if (p.slot === PT.POWER_ON) {
          pressed = params.get(p.id) > 0.5;
          instance.SetFloatParameter(p.slot, pressed ? 1 : 0);
        } else {
          instance.SetFloatParameter(p.slot, hostValue(p, params.get(p.id)));
        }
      }
      instance.SetTime(time);
      instance.ProcessOpenGL(input, null);
      if (pressed) params.set('power', 0);

      // The line under the picture: the port's numbers for this frame.
      const fr = instance.frame;
      if (!fr) return;
      const pr = instance.params;
      const w = input.width;
      const cold = ColdDriftFromParam(pr[PT.COLD_DRIFT]) * w;
      const k = instance.kernels;
      const parts = [
        `${input.width} × ${input.height}`,
        `Setup ${SetupFromParam(pr[PT.SETUP])}: zone error ${pxText(ConvergenceFromParam(pr[PT.CONVERGENCE]) * w)} RMS on ${instance.zoneCount} × ${instance.zoneCount}`,
        `cold drift left ${(instance.warm * 100).toFixed(instance.warm < 0.01 ? 2 : 1)}% (${pxText(instance.warm * cold)})`,
      ];
      if (fr.keystoneK > 0) parts.push(`keystone: red and blue ${pxText(keystoneEdge(fr, w))} apart at the side edges`);
      parts.push(`blue spot at full white σ ${pxText(fr.blueSpotPx)}`);
      parts.push(`blur kernels ${2 * k.radius + 1} taps`);
      parts.push(`frame ${(instance.lastDt * 1e3).toFixed(1)} ms`);
      page.stats = parts.join(' · ');
    },
  };
}

/**
 * The page's, for the line under the picture: how far apart the red and blue
 * pictures are at the middle of the right-hand edge, from the composite's
 * keystone() with the drift and zones left out: the distance between the
 * points of the two tubes' pictures the edge pixel reads.
 */
function keystoneEdge(fr, width) {
  const cx = 0.5 * width;
  const q = width - 0.5;
  const n = (q - cx) / cx;
  const shift = (e) => {
    const eu = e * n;
    return ((fr.keystoneK * (eu * n)) / (1.0 - eu)) * cx;
  };
  return shift(fr.keystoneE) - shift(-fr.keystoneE);
}

const COMMON_GAPS = {
  es: 'GLSL ES 3.00 in WebGL2, not desktop GL 4.1 core. The shader text is the plugin’s; only the version line and precision qualifiers are changed by the kit. The sRGB curves are the driver’s pow, and the blur sums floats in the driver’s order, so another GPU can round a last bit differently.',
  presets: 'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters (one of them presses Power On).',
  about: 'The About block (a text line and link buttons for the host’s panel) is absent: a web page has links of its own. So are the plugin’s log file and its harness’s wrong-model hooks, all off here as in a shipped instance. The plugin has no audio path, and neither has the page.',
};

const mounted = mountDemo({
  name: 'SW Convergence',
  kind: 'effect',
  pluginId: 'CV01',
  tagline:
    'A three-tube CRT projector throwing your clip onto the screen. Green looks straight at it; red and blue sit either side at 7.8° and are bent onto green zone by zone, by a technician who never got every zone exactly right — so a white line is clean in one part of the screen, has a red edge somewhere else and a blue one in a corner. Switch the set on cold and the side tubes sit off as a whole, closing up as it warms. Each tube focuses on its own, an astigmatic gun sharpens horizontal lines at one end of its focus and vertical lines at the other, and a hard-driven blue gun’s spot grows. None of it is drawn.',
  repo: 'https://github.com/stoatworks-labs/convergence',
  blurb:
    'It is SW Convergence’s own GLSL, its three passes ported from the repository to WebGL2 unedited, run by a JavaScript port of the plugin’s CPU half (the installation’s zone errors, the warm-up, the spot’s blur kernels, the clock and the six draws) that only a reader and a crosscheck against its harness check, on generated clips in this page — no install.',
  needFloat: true,
  showBackdrop: true,
  // The geometry card first: a crosshatch is what a convergence technician looks at.
  sources: ['grid', 'bars', 'detail', 'scene', 'spot', 'ramp', 'alpha'],
  params: PARAMS,
  // The plugin declares no presets. These are the page's: combinations of
  // the plugin's own parameters and nothing else.
  presets: {
    'A badly converged set (Convergence 1% of the width)': { convergence: ParamFromConvergence(0.01) },
    'Nine zones a side (Zones 9x9, Convergence 0.5%)': { zones: 1, convergence: ParamFromConvergence(0.005) },
    'Another installation (Setup 42, Convergence 0.5%)': { setup: setupSlot(42), convergence: ParamFromConvergence(0.005) },
    'Keystone left uncorrected (Keystone 1)': { keystone: 1 },
    'Cold start, 10 s warm-up from 1% off (presses Power On)': { warmup: ParamFromWarmup(10), cold: ParamFromColdDrift(0.01), power: 1 },
    'Soft blue tube (Blue Focus 0.2)': { blueFocus: ParamFromFocus(-0.6) },
    'Astigmatic guns, vertical lines sharp (Astigmatism 0.5, every Focus 0.25)': {
      astigmatism: ParamFromAstigmatism(0.5), redFocus: ParamFromFocus(-0.5), greenFocus: ParamFromFocus(-0.5), blueFocus: ParamFromFocus(-0.5) },
    'Astigmatic guns, horizontal lines sharp (Astigmatism 0.5, every Focus 0.75)': {
      astigmatism: ParamFromAstigmatism(0.5), redFocus: ParamFromFocus(0.5), greenFocus: ParamFromFocus(0.5), blueFocus: ParamFromFocus(0.5) },
    'Blue gun pushed (Bloom 10 mA)': { bloom: 1 },
    'Half projector, half clip (Mix 0.5)': { mix: 0.5 },
  },
  differences: [
    'The CPU half is a port. The three shaders are the plugin’s, unedited. What runs them is translated to JavaScript by hand in demo/plugin.js: Controls.cpp’s conversions and defaults; Projector.cpp’s keystone coefficient, spot law, PCG hash, zone table (Box–Muller in double), drift directions, frame optics and blur kernels (pixel-integrated Gaussians); and Convergence.cpp’s Power On edge, clock, frame length, warm-up, drift and pass orchestration — prep drawn twice, the separable blur in four draws, the composite — with every uniform. demo/tools/crosscheck/ compares the CPU numbers with a printer built from the plugin’s own sources, and this page’s pictures and warm-up with the plugin’s harness frame for frame (AGENTS.md has the figures); outside those cases, only a reader checks it. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders drifts.',
    'The installation is the plugin’s, not a lookalike: the zone errors are the same PCG hash and Box–Muller of (Setup, grid, point, tube, axis) in double, so Setup 42 here is Setup 42 in Resolume, at any raster. The logarithm, cosine, exponential and power are the browser’s, not the C library’s, and erf and erfc are the page’s own (JavaScript has neither): the crosscheck measures how far apart that leaves the doubles and how often a float handed to the GPU differs.',
    'A frame here is one of the display’s refreshes, not Resolume’s output frame, and the clock is the page’s, in seconds; the plugin’s vote on whether its host sends seconds or milliseconds is ported and settles on seconds. The warm-up steps by each frame’s own length, clamped to a tenth of a second and rounded to the microsecond, as the plugin does, so it is the same at any refresh rate. Paused, the clock stands still and the warm-up holds; Restart is the clock going backwards, which the plugin takes as no time passing. A tab in the background draws nothing and its clock stops, so the set does not warm while you are away, where in Resolume it warms as long as the host’s clock runs.',
    'Power On is a toggle the page releases after one frame, and Setup is a dropdown of all 1000 installations: the kit has no event or integer control. Each reaches the plugin’s code as the host would send it — a press then a release, and the integer itself.',
    'The clips are the kit’s, generated in the page, not Resolume’s demo footage, and they are premultiplied, as is a picture of your own here. The plugin weights the clip’s light by its alpha, which is right for straight alpha (Resolume’s DXV demo clips decode to straight alpha); on a premultiplied clip a soft edge is weighted twice. Only the “Shape on transparency” clip has an edge to show it. Bloom’s default was decided on Resolume’s 33 demo clips, which are not here.',
    'Bloom is drawn at its real size, which is small: the blue spot at full white has a sigma of 0.63 px at 960 × 540 at 10 mA and 0.06 px at the default 0.32 mA (1.26 px and 0.12 px at 1080p). The page does not exaggerate it. It shows on fine, bright blue detail — the Fine detail clip, the blue bar’s edges — at 1920 × 1080 with the page zoomed in; Blue Focus and Astigmatism soften far more.',
    'The zone errors and the drift are fractions of the picture width and the spots and the focus blur fractions of its height, in the plugin and here, so the picture looks the same at any Composition size. A picture or video of your own runs at its own size; changing Composition afterwards does not resize it.',
    'Float render targets: the blur’s intermediates are RGBA32F, as in the plugin, so the page needs WebGL2’s EXT_color_buffer_float and stops with an error without it rather than falling back to a narrower format. Every texture is read texel by texel (the composite’s bilinear is done by hand in the shader), so no float filtering is needed.',
    'The output is the page’s 8-bit canvas, as the plugin’s is a host’s buffer. At Mix 1 it is opaque and the two agree. Below 1, where the output is partly transparent, the plugin writes straight (unpremultiplied) colour, which is what a host is handed, and the browser composites the canvas as premultiplied over the backdrop, so there a partly transparent pixel shows brighter than a host compositing straight alpha would show it.',
    COMMON_GAPS.es,
    'The line under the picture is not the plugin. It reports what the port computed for the frame — the zone error in pixels, the cold drift left, the keystone’s spread, the blue spot at full white, the kernels’ width and the frame’s length. The plugin draws no such thing.',
    COMMON_GAPS.presets,
    COMMON_GAPS.about,
    'Nothing here is measured. The plugin’s numerical proof — every zone point’s fringe against the installation’s set errors, the keystone against a ray-plane intersection, the warm-up’s time constant fitted at three frame rates, the spot’s variance, 23 wrong models and 46 one-character mutants caught — is tools/cvtest in the repository, and that harness, not this page, is the reason to believe the projector. What Resolume itself has shown is in the README’s Status. This page has been compared with the harness on one Mac (an Apple M4 Max, Chrome through ANGLE on Metal) and on no other GPU.',
  ],
  createRenderer,
});

// The line under the picture.
const statLine = document.createElement('p');
statLine.className = 'stage__status';
statLine.setAttribute('aria-live', 'off');
statLine.dataset.role = 'convergence-stats';
document.querySelector('.stage')?.append(statLine);
if (statLine.isConnected) {
  const tick = () => {
    if (statLine.textContent !== page.stats) statLine.textContent = page.stats;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// For a headless check: the mounted page.
page.mounted = mounted ?? null;
window.convergencePage = page;
