#pragma once

#ifdef PING_TESTING_BUILD
  // When compiling under the test harness, IRSynthEngine.cpp only uses STL.
  // JuceHeader.h is not available (and not needed) in the standalone test target.
  #include <cstdint>
  #include <functional>
  #include <vector>
  #include <string>
  #include <map>
  #include <array>
#else
  #include <JuceHeader.h>
  #include <functional>
  #include <vector>
  #include <string>
  #include <map>
  #include <array>
#endif

/**
 * Source radiation pattern (instrument directivity) — per-octave-band model.
 *
 * Replaces the original frequency-flat pure-cardioid `spkG` formula with a
 * frequency-dependent pattern that better models real acoustic instruments:
 * nearly omni at low frequencies (where instrument size ≪ wavelength), and
 * progressively narrower toward HF — with a non-zero rear floor (real
 * instruments still radiate ~ −15 to −25 dB behind themselves; a true
 * cardioid's hard zero at 180° is a measurement artefact, not a real
 * instrument property).
 *
 * Three kinds, in order of complexity:
 *
 *   LegacyCardioid (default, 0)
 *     Frequency-flat 0.5 + 0.5·cos(θ). Bit-identical to the pre-v2.11
 *     engine; the `bandExp` and `bandFloor` arrays are ignored. Selected
 *     so existing presets and the IR_11/IR_14 regression locks are
 *     preserved exactly.
 *
 *   Parametric (1)
 *     Per-band gain = floor[b] + (1 − floor[b]) · max(0, 0.5 + 0.5·cos(θ))^exp[b].
 *     `exp[b] = 1, floor[b] = 0` reproduces a pure cardioid (verified by
 *     IR_34). `exp[b]` increasing with frequency narrows the HF lobe;
 *     `floor[b]` raises the rear gain toward published instrument data.
 *
 *   Omni (2)
 *     Flat 1.0 everywhere. Equivalent to Parametric with exp=0, floor=1
 *     but kept as a separate Kind so the UI/preset semantics are explicit.
 *
 * The order-dependent fade-to-omni from `spk_directivity_full` applies
 * to every Kind identically (full=true → no fade; full=false → pattern at
 * orders 0–1, 50/50 with omni at order 2, omni at order 3+).
 *
 * This struct is intentionally extensible: v2.12 added per-source
 * vertical tilt (see `IRSynthParams::spkl_tilt` / `spkr_tilt` and
 * `defaultTiltDeg` below). Future work can append asymmetric rear-lobe
 * coefficients or full spherical-harmonic coefficients without a further
 * IRSynthParams refactor — the existing fields stay layout-compatible
 * because new optional members are appended at the end with harmless
 * defaults.
 */
struct SourceRadiation
{
    enum class Kind : int { LegacyCardioid = 0, Parametric = 1, Omni = 2 };

    Kind kind = Kind::LegacyCardioid;

    // Per-octave-band cardioid exponent. Bands [125, 250, 500, 1k, 2k, 4k, 8k, 16k].
    // Used only when kind == Parametric.
    std::array<double, 8> bandExp { { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 } };

    // Per-octave-band rear floor in [0, 1]. Same band layout as bandExp.
    // 0 = hard zero behind (cardioid-like); 0.10 ≈ −20 dB rear (typical for
    // measured instruments); 1.0 = omni.
    std::array<double, 8> bandFloor { { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 } };

    // Identifier used to round-trip the choice through .ping XML and to
    // present a human-readable label in the UI. Empty for ad-hoc/default.
    // The plugin reads this back and re-resolves it through byPreset() at
    // load time, so coefficient drift in a preset's tuning does NOT
    // invalidate older sidecars.
    std::string presetName;

    // Suggested vertical tilt in DEGREES for this preset (0 = horizontal
    // forward, +90 = straight up, −90 = straight down). Purely advisory:
    // the actual per-source tilt lives on IRSynthParams::spkl_tilt /
    // spkr_tilt and is always under user control. The UI uses this value
    // to auto-populate the tilt slider when a preset is picked (e.g.
    // strings/voice default to a slight upward tilt; brass defaults to 0
    // because the bell is usually pointed at the listener). Set to 0 for
    // all built-in v2.11 presets so loading an existing scene does not
    // change behaviour.
    double defaultTiltDeg = 0.0;

    // ── Built-in preset factories ────────────────────────────────────────
    // Each returns a fully populated SourceRadiation. byPreset() does a
    // case-insensitive lookup against presetNames(); unknown names map to
    // legacyCardioid() so unknown XML tags from the future degrade safely.
    static SourceRadiation legacyCardioid();
    static SourceRadiation cardioidExperimental();   // Parametric equivalent of legacy (for IR_34)
    static SourceRadiation omni();
    static SourceRadiation supercardioidWithFloor();
    static SourceRadiation genericInstrument();
    static SourceRadiation brassForward();
    static SourceRadiation voice();
    static SourceRadiation stringsBroad();
    static SourceRadiation windReed();

    // Lookup the canonical built-in / loaded preset by name. Returns
    // legacyCardioid() if not found. Used both by the UI dropdown and by
    // .ping preset XML loading.
    static SourceRadiation byPreset (const std::string& name);

    // Names of all built-in presets, in the order they should appear in
    // the UI dropdown. JSON-loaded measured-instrument presets are appended
    // to this list at runtime via registerLoadedPreset().
    static const std::vector<std::string>& presetNames();

    // Append a runtime-loaded preset (e.g. parsed from
    // Resources/instrument-radiation.json) to the registry. Idempotent on
    // name. Called once at plugin startup.
    static void registerLoadedPreset (const SourceRadiation& r);
};

/** All parameters for one IR synthesis run. */
struct IRSynthParams
{
    // Room
    // Shape strings (v2.8.0+): "Rectangular"|"Fan / Shoebox"|"Octagonal"|"Circular Hall"|"Cathedral".
    // Old strings handled by PluginProcessor migration: "L-shaped" -> "Rectangular",
    // "Cylindrical" -> "Circular Hall".
    std::string shape = "Rectangular";
    double width  = 28.0;
    double depth  = 16.0;
    double height = 12.0;

    // ── Shape proportion parameters (only used when shape != "Rectangular") ──
    // These shape footprint via the polygon image-source method (calcRefsPolygon).
    // Defaults are middle-of-range values that produce visually plausible shapes
    // for typical room dimensions. Each is also clamped to its documented range
    // by makeWalls2D before being applied to vertex math.
    //
    // Cathedral cruciform proportions:
    //   shapeNavePct: nave width / total width, range [0.15, 0.50].
    //                 Below 0.15 the nave becomes a thin slot; above 0.50 the
    //                 transept arms vanish.
    //   shapeTrptPct: transept arm depth / total depth, range [0.20, 0.45].
    //                 Below 0.20 the transepts are vestigial; above 0.45 they
    //                 would extend past the bounding box.
    //
    // Fan/Shoebox:
    //   shapeTaper:   stage-end narrowing factor [0.0, 0.70]. 0 = rectangle,
    //                 0.70 = stage end is 30% of back-wall width.
    //
    // Octagonal / Circular Hall:
    //   shapeCornerCut: chamfer depth [0.0, 1.0]. For Octagonal, 0.414 = regular
    //                   octagon when W=D. For Circular Hall, 0 = bounding rectangle,
    //                   1 = inscribed ellipse via 16-gon interpolation.
    double shapeNavePct   = 0.30;
    double shapeTrptPct   = 0.35;
    double shapeTaper     = 0.30;
    double shapeCornerCut = 0.414;

    // Surfaces
    std::string floor_material   = "Hardwood floor";
    std::string ceiling_material = "Painted plaster";
    std::string wall_material    = "Concrete / bare brick";
    double window_fraction       = 0.27;   // 0..1 fraction of wall area that is glazing (brick + glass blend)

    // Contents
    double audience  = 0.45;   // 0..1
    double diffusion = 0.40;   // 0..1

    // Architecture
    std::string vault_type = "Groin / cross vault  (Lyndhurst Hall)";
    double organ_case = 0.59;  // 0..1
    double balconies  = 0.54;  // 0..1

    // Air
    double temperature = 20.0; // °C  (currently only affects speed-of-sound display, not used in engine in v5)
    double humidity    = 50.0; // %   (same — keep as stored param for display; AIR[] array is hardcoded)

    // Placement — normalised 0..1 within room footprint
    // Speakers: centre (y=0.5), 25%/75% across, facing down. Mics: 1/5 up from bottom (y=0.8), 35%/65% across
    double source_lx   = 0.25; double source_ly   = 0.5;
    double source_rx   = 0.75; double source_ry   = 0.5;
    double receiver_lx = 0.35; double receiver_ly = 0.8;
    double receiver_rx = 0.65; double receiver_ry = 0.8;

    // Mono speaker source mode (off by default — preserves bit-identity with
    // pre-feature builds). When true, every synthesis path (MAIN, OUTRIG,
    // AMBIENT, DIRECT) uses a single speaker positioned at (source_lx,
    // source_ly) for both the L-source and R-source slots in the 4-channel
    // IR layout (rRL := rLL, rRR := rLR). By linearity of convolution the
    // existing 4-convolver runFour mixer then produces
    //   outL = IR_A ⊛ (inL + inR), outR = IR_B ⊛ (inL + inR)
    // which is exactly equivalent to summing the input to mono and
    // convolving with a single-speaker IR. Eliminates inter-speaker comb
    // filtering when the two speakers were placed close together.
    // The R speaker puck is hidden in the floor plan UI when this is on.
    bool        mono_source = false;

    // Angles (radians): 0=right, π/2=down, -π/2=up. Speakers down (π/2), mics up-left (-3π/4) / up-right (-π/4)
    double spkl_angle = 1.57079632679;   // π/2 down
    double spkr_angle = 1.57079632679;
    double micl_angle = -2.35619449019;  // -3π/4 up-left
    double micr_angle = -0.785398163397; // -π/4 up-right

    // Source elevation tilt (radians, v2.12): 0 = horizontal forward,
    // +π/2 = aimed straight up (e.g. violin bell upward), −π/2 = down.
    // When the source_radiation kind is non-Legacy, calcRefs and
    // calcRefsPolygon use the full 3D directivityCos (azimuth + elevation
    // + tilt) for the speaker pattern, so a tilted violin/voice now
    // correctly attenuates listeners behind/below the instrument.
    //
    // Bit-identity: when both tilts are 0 *and* the radiation kind is
    // LegacyCardioid, the inline scalar `sg` path is unchanged and the
    // engine output remains bit-identical to v2.11 (IR_11/IR_14/IR_32/
    // IR_33 stay green). LegacyCardioid intentionally does NOT consume
    // tilt — the UI greys the tilt slider out for that preset and the
    // documented promise is "Cardioid (legacy) is bit-identical to the
    // pre-v2.11 engine, including ignoring tilt".
    double spkl_tilt = 0.0;
    double spkr_tilt = 0.0;

    // Mic elevation tilt (radians): 0 = horizontal, +π/2 = straight up,
    // -π/2 = straight down. Acts as the elevation component of each mic's
    // facing axis in the spherical-law-of-cosines polar-pattern formula
    // (see IRSynthEngine::directivityCos). Default -30° = mic tilted down,
    // matching real-world orchestral practice (mic at 3 m on a stand looks
    // down at the source roughly 1 m off the floor a few metres away).
    //
    // ── Backward-compatibility note for preset/sidecar loading ────────────
    // OLD presets / sidecars saved before this feature have no tilt
    // attributes. PluginProcessor / IRSynthComponent must explicitly fall
    // back to 0.0 (horizontal) when the XML attribute is missing — NOT to
    // these struct defaults — so existing user content sounds bit-exactly
    // as it did before. New presets created after this feature use these
    // -30° defaults via fresh-instance IRSynthParams. See CLAUDE.md.
    double micl_tilt = -0.5235987755982988;   // -π/6  (-30°)
    double micr_tilt = -0.5235987755982988;

    // Options
    std::string mic_pattern = "cardioid (LDC)";
    // "omni"|"omni (MK2H)"|"subcardioid"|"wide cardioid (MK21)"|"cardioid (LDC)"|"cardioid (SDC)"|"figure8"
    // "cardioid" is a legacy alias for "cardioid (LDC)"
    bool        er_only     = false;
    int         sample_rate = 48000;      // 44100 | 48000
    bool        bake_er_tail_balance = false;
    double      baked_er_gain = 1.0;
    double      baked_tail_gain = 1.0;

    // ── Outrigger mics (independent mic pair, full ER + Tail) ────────────────
    // outrig_height is the physical mic height in metres (unlike receiver_*
    // which derives height from He * 0.9). Defaults match MAIN mic height (3 m).
    bool        outrig_enabled  = false;
    double      outrig_lx       = 0.15;
    double      outrig_ly       = 0.80;
    double      outrig_rx       = 0.85;
    double      outrig_ry       = 0.80;
    double      outrig_langle   = -2.35619449019;   // -3 pi/4 up-left (same default as MAIN)
    double      outrig_rangle   = -0.785398163397;  // -pi/4 up-right
    double      outrig_height   = 3.0;              // metres above floor
    std::string outrig_pattern  = "cardioid (LDC)";
    // Per-mic elevation tilt (radians); 0 = horizontal. Same convention as
    // micl_tilt / micr_tilt. Default -30°. Legacy 0° fallback applies on
    // load when the attribute is missing.
    double      outrig_ltilt    = -0.5235987755982988;
    double      outrig_rtilt    = -0.5235987755982988;

    // ── Ambient mics (higher, further-back pair, full ER + Tail) ─────────────
    bool        ambient_enabled = false;
    double      ambient_lx      = 0.20;
    double      ambient_ly      = 0.95;
    double      ambient_rx      = 0.80;
    double      ambient_ry      = 0.95;
    double      ambient_langle  = -2.35619449019;
    double      ambient_rangle  = -0.785398163397;
    double      ambient_height  = 6.0;              // metres above floor
    std::string ambient_pattern = "omni";
    // Per-mic elevation tilt (radians); 0 = horizontal. Same convention as
    // micl_tilt / micr_tilt. Default -30°. Legacy 0° fallback on load.
    double      ambient_ltilt   = -0.5235987755982988;
    double      ambient_rtilt   = -0.5235987755982988;

    // ── Direct path (shares MAIN mic pattern + angles) ───────────────────────
    // No extra geometry fields: DIRECT uses receiver_lx/ly/rx/ry, micl_angle,
    // micr_angle and mic_pattern from the MAIN pair (Decision D2 in
    // Docs/Multi-Mic-Work-Plan.md).
    //
    // direct_max_order: reflection order limit for the DIRECT path. 0 = pure
    // line-of-sight (the historical behaviour); 1 = direct + first-order wall
    // bounces (floor / ceiling / near walls — strengthens localisation via the
    // precedence-effect fusion window); 2 = + second-order. ER crossover gate
    // (eo=true, ec=85 ms) still applies, so raising this cannot leak content
    // into the tail region.
    bool        direct_enabled   = false;
    int         direct_max_order = 1;

    // ── Experimental toggles (A/B knobs for the early-reflection experiment)
    // Both default to the historical behaviour so MAIN/OUTRIG/AMBIENT output
    // is bit-identical to pre-experiment builds when left at defaults.
    //
    // lambert_scatter_enabled: Feature A in calcRefs. When true (default), each
    // specular reflection of order 1–3 spawns N_SCATTER=2 secondary rays with
    // 0–4 ms random delay at ~3% amplitude. Softens the ER comb and fills the
    // gaps between specular spikes. Can be turned off to test whether it is
    // contributing to perceived image softness in the first 30 ms.
    //
    // spk_directivity_full: speaker directivity fade-to-omni override. When
    // false (default), order 0–1 use full cardioid, order 2 is a 50/50 blend
    // with omni, order 3+ is fully omni. When true, the fade is disabled and
    // all reflection orders use the full cardioid speaker pattern — tests
    // whether early-reflection directional cues are being lost to the fade.
    bool        lambert_scatter_enabled = true;
    bool        spk_directivity_full    = false;

    // ── Source radiation model (instrument directivity) ───────────────────
    // See struct SourceRadiation defined above. Default value reproduces
    // the legacy frequency-flat pure cardioid bit-exactly, so existing
    // presets and IR_11/IR_14/IR_32/IR_33 regression locks are unaffected.
    // Selecting a non-default preset via the UI changes the per-octave-band
    // directivity used by calcRefs / calcRefsPolygon.
    SourceRadiation source_radiation {};

    // ── Decca Tree capture mode (MAIN + DIRECT paths only) ───────────────────
    // When enabled, MAIN and DIRECT synthesis use a 3-mic L/C/R array rigidly
    // mounted at (decca_cx, decca_cy, h) and rotated by decca_angle. Outer
    // spacing a, centre advance b, height h, centre gain gC and centre HPF
    // cutoff are fixed classical defaults in the engine (file-static constants).
    // The 3-mic L/C/R render is combined into the same iLL/iRL/iLR/iRR
    // 4-channel layout used by the non-Decca path, so the convolver and mixer
    // downstream are unchanged. OUTRIG/AMBIENT are unaffected.
    bool        main_decca_enabled = false;
    double      decca_cx    = 0.5;
    double      decca_cy    = 0.65;
    double      decca_angle = -1.5707963267948966;  // -π/2 pointing towards low-y (source stage)

    // Centre-mic gain in the Decca combine, i.e. the scalar applied to the
    // centre-mic's captured signal before it is summed identically into both
    // the L and R output channels: L_out = L_raw + g·HPF(C);
    // R_out = R_raw + g·HPF(C). Acts as a "centre fill" control.
    //
    // Default 0.5 (−6 dB) approximates typical real-world Decca mixing practice
    // (centre 6 dB below outers). Setting 0.0 disables the centre-fill entirely
    // and leaves the tree as a bare L/R spaced pair at the outer positions.
    // Higher values reinforce the phantom centre at the cost of stereo width
    // and reflection definition (the centre-mic's reflections arrive at
    // different times than L/R and smear the early field when summed strongly).
    // 0.707 was the previous fixed value and remains the upper bound.
    double      decca_centre_gain = 0.5;

    // Toe-out angle (radians) applied to the L and R outer mic face directions
    // relative to the Decca tree's forward axis. L face = decca_angle − toe_out,
    // R face = decca_angle + toe_out. The centre mic always looks straight
    // forward (decca_angle). Default π/2 (±90°) places the outer mics fully
    // side-firing for maximum stereo separation; π/4 (±45°) matches the
    // classic main pair default; 0 collapses the tree back to three forward-
    // facing mics (the pre-experiment behaviour).
    double      decca_toe_out     = 1.5707963267948966;  // π/2 (90°)

    // Single elevation tilt applied to all three mics in the Decca tree
    // (L, C, R move rigidly together). 0 = horizontal, -π/6 = -30° down.
    // Legacy 0° fallback on load.
    double      decca_tilt        = -0.5235987755982988;  // -π/6 (-30°)

    // Floor-plan Option-mirror axis preference (UI-only — not consumed by
    // the synthesis engine). 0 = vertical (mirror across x = 0.5, the
    // historical default), 1 = horizontal (mirror across y = 0.5). Stored
    // here so it round-trips through the .ping sidecar with the rest of the
    // floor-plan UI state.
    int         mirror_axis       = 0;
};

/** Per-path 4-channel IR (LL/RL/LR/RR) used for DIRECT/OUTRIG/AMBIENT results. */
struct MicIRChannels
{
    std::vector<double> LL, RL, LR, RR;
    int  irLen       = 0;
    bool synthesised = false;   // false = path was disabled, empty vectors
};

struct IRSynthResult
{
    // MAIN path — existing fields, unchanged layout.
    std::vector<double> iLL, iRL, iLR, iRR;  // L->L, R->L, L->R, R->R
    std::vector<double> rt60;                // 8 bands: 125 250 500 1k 2k 4k 8k 16k
    int   irLen      = 0;
    int   sampleRate = 0;
    bool  success    = false;
    std::string errorMessage;

    // Additional mic paths (feature/multi-mic-paths).
    // synthesised == false for any path that was not requested in IRSynthParams;
    // the vectors stay empty in that case.
    MicIRChannels direct;
    MicIRChannels outrig;
    MicIRChannels ambient;
};

/**
 * Progress callback: (fraction 0..1, message string)
 * Called from the synthesis thread — must be thread-safe.
 */
using IRSynthProgressFn = std::function<void(double, const std::string&)>;

/**
 * 2D polygon wall used by the polygon image-source method (calcRefsPolygon).
 *
 * Convention: walls are listed counter-clockwise when viewed from above
 * (standard polygon winding). The inward unit normal therefore points to the
 * RIGHT of the direction of travel (x2-x1, y2-y1):
 *   nx =  (y2 - y1) / length
 *   ny = -(x2 - x1) / length
 *
 * rAbs[b] is the per-band reflection coefficient (= 1 - absorption). In the
 * initial implementation makeWalls2D sets rAbs[b] = rW[b] (the same blended
 * wall+window absorption used by the rectangular calcRefs) for every wall.
 * Per-wall material assignment is a future extension.
 *
 * The fixed array size of 8 must match IRSynthEngine::N_BANDS exactly (verified
 * by static_assert inside IRSynthEngine.cpp). Defined at namespace scope so
 * unit tests can construct Wall2D values directly without depending on the
 * IRSynthEngine class internals.
 */
struct Wall2D
{
    double x1 = 0.0, y1 = 0.0;          // wall start point (metres)
    double x2 = 0.0, y2 = 0.0;          // wall end point   (metres)
    double nx = 0.0, ny = 0.0;          // inward unit normal
    std::array<double, 8> rAbs{};       // reflection coefficient per band
};

/**
 * Pure C++ port of the IR Synthesiser v5 acoustic engine.
 * All methods are static — no state, fully re-entrant.
 */
class IRSynthEngine
{
public:
    /** Main entry point. Runs synchronously — call from a background thread. */
    static IRSynthResult synthIR (const IRSynthParams& p, IRSynthProgressFn cb);

    /** Compute RT60 at 8 bands without doing a full synthesis. */
    static std::vector<double> calcRT60 (const IRSynthParams& p);

    /** Encode stereo IR to 24-bit WAV bytes. */
    static std::vector<uint8_t> makeWav (const std::vector<double>& iLL,
                                         const std::vector<double>& iRL,
                                         const std::vector<double>& iLR,
                                         const std::vector<double>& iRR,
                                         int sampleRate);

    // ── 2D polygon geometry utilities (v2.8.0) ────────────────────────────
    // Public for testability (DSP_22 in PingPolygonTests.cpp). They are pure
    // functions of their inputs with no engine-internal state, so exposing
    // them is harmless. See Docs/Polygon-Room-Geometry-Plan.md sections 6–8.

    /** Reflect a 2D point through the infinite line containing wall w. */
    static std::pair<double, double> reflect2D (double px, double py,
                                                const Wall2D& w) noexcept;

    /** True if the directed segment (ax,ay)->(bx,by) crosses the finite wall
        segment. t_out in [0,1] along the ray, s_out in [0,1] along the wall.
        Uses EPS = 1e-9 for grazing-corner robustness. */
    static bool rayIntersectsSegment (double ax, double ay,
                                      double bx, double by,
                                      const Wall2D& w,
                                      double& t_out, double& s_out) noexcept;

    /** Shoelace formula. Works for any simple (non-self-intersecting) polygon
        whose walls are wound consistently (CW or CCW). Returns absolute area. */
    static double polygonArea      (const std::vector<Wall2D>& walls) noexcept;

    /** Sum of wall segment lengths. */
    static double polygonPerimeter (const std::vector<Wall2D>& walls) noexcept;

    /** Construct the 2D polygon wall list for the given shape and dimensions.
        rWPerBand is broadcast to every wall's rAbs[]. Zero-length walls (e.g.
        Octagonal at shapeCornerCut=0) are excluded. For shape=="Rectangular"
        this returns the 4-wall rectangle for completeness, but the engine
        never calls it on the Rectangular path — calcRefs is hard-routed. */
    static std::vector<Wall2D> makeWalls2D (const IRSynthParams& p,
                                            double W, double D,
                                            const std::array<double, 8>& rWPerBand);

private:
    // ── constants ──────────────────────────────────────────────────────────
    static const double SPEED;      // 343.0
    static const int    N_BANDS;    // 8
    static const int    BANDS[8];   // {125,250,500,1000,2000,4000,8000,16000}

    // Material absorption coefficients [14 materials × 8 bands]
    static const std::map<std::string, std::array<double,8>>& getMats();

    // Vault profile table [name → {hm, vs, vHfA}]
    static const std::map<std::string, std::array<double,3>>& getVP();

    // Audience/balcony/organ/air absorption arrays [8 bands]
    static const double OA[8], BA[8], BSA[8], AIR[8];

    // Mic polar pattern — per-band {o, d} pairs for 8 octave bands [125..16k Hz].
    // Patterns: {omni, omni (MK2H), subcardioid, wide cardioid (MK21), cardioid (LDC), cardioid (SDC), figure8};
    // "cardioid" kept as a backward-compat alias for "cardioid (LDC)".
    // Constraint: o + d = 1.0 at every band (on-axis gain is frequency-flat).
    static const std::map<std::string, std::array<std::pair<double,double>, 8>>& getMIC();

    // ── engine helpers ─────────────────────────────────────────────────────
    static double eyring (double vol, double mAbs, double tS);

    // 3D mic directivity: cos(theta) between source direction (az, el) and the
    // mic's facing axis (faceAzimuth, faceElevation). Spherical law of cosines.
    // Single source of truth — duplicated locally as directivityCosLocal in
    // PingDSPTests.cpp to keep the DSP test suite self-contained (DSP_21).
    static double directivityCos (double az, double el,
                                  double faceAzimuth, double faceElevation) noexcept;

    // Per-octave-band polar pattern gain. cosTheta is the precomputed
    // dot product of the source direction with the mic's facing axis,
    // produced by directivityCos. Hoisted out of the band loop so the
    // 3D math runs once per reflection rather than once per (reflection × band).
    static double micG  (int band, const std::string& pat, double cosTheta);
    static double spkG  (double faceAngle, double azToReceiver);

    // Seeded RNG (matches JS mkRng)
    struct Rng { uint32_t r; double next(); };
    static Rng mkRng (uint32_t seed);
    static double rU  (Rng& rng, double lo, double hi);

    struct Ref { int t; std::array<double,8> amps; double az; };

    static std::vector<Ref> calcRefs (
        double rx, double ry, double rz,
        double sx, double sy, double sz,
        const IRSynthParams& p,
        double He, int mo,
        const std::array<double,8>& rF,
        const std::array<double,8>& rC,
        const std::array<double,8>& rW,
        double oF, double vHfA, double ts,
        bool eo, int ec, int sr,
        uint32_t seed,
        const std::string& micPat,
        double spkFaceAngle, double micFaceAngle,
        double maxRefDist,
        double minJitterMs = 0.0,
        double highOrderJitterMs = 0.0,   // jitter for order 2+ (when close, breaks periodic echo)
        double micFaceTilt = 0.0,         // mic elevation tilt in radians (0 = horizontal); see directivityCos
        double spkFaceTilt = 0.0);        // source elevation tilt (v2.12); only consumed when source_radiation kind != LegacyCardioid

    // calcRefsPolygon — polygon image-source method for non-rectangular shapes.
    // Structurally mirrors calcRefs parameter-for-parameter so the two can be
    // swapped through a dispatch lambda at each call site. Horizontal
    // reflections are computed by a recursive 2D Borish tree with full chain
    // validation; vertical reflections use the same nz loop as calcRefs.
    // The Rectangular path NEVER routes here — this function is only invoked
    // when p.shape is one of Fan / Shoebox, Octagonal, Circular Hall, Cathedral.
    // Reflection order is capped per-shape by orderLimitForShape (internal).
    static std::vector<Ref> calcRefsPolygon (
        double rx, double ry, double rz,
        double sx, double sy, double sz,
        const IRSynthParams& p,
        double He, int mo,
        const std::array<double,8>& rF,
        const std::array<double,8>& rC,
        const std::array<double,8>& rW,
        double oF, double vHfA, double ts,
        bool eo, int ec, int sr,
        uint32_t seed,
        const std::string& micPat,
        double spkFaceAngle, double micFaceAngle,
        double maxRefDist,
        double minJitterMs = 0.0,
        double highOrderJitterMs = 0.0,
        double micFaceTilt = 0.0,
        double spkFaceTilt = 0.0);

    static std::vector<double> bpF  (const std::vector<double>& buf, double fc, int sr);
    static std::vector<double> bpFQ (const std::vector<double>& buf, double fc, double Q, int sr);
    static std::vector<double> lpF  (const std::vector<double>& buf, double fc, int sr);
    static std::vector<double> hpF  (const std::vector<double>& buf, double fc, int sr);

    // Modal resonance boost: parallel IIR resonators tuned to axial modes below ~250 Hz.
    // Axial modes (f_n = c·n/(2L)) are physically meaningful only for rectangular rooms.
    //
    // For non-rectangular shapes the function behaviour depends on the build:
    //   - PING_POLYGON_MODAL_BANK undefined (v2.8.0 behaviour): early-returns unchanged.
    //   - PING_POLYGON_MODAL_BANK defined (default v2.9.0): computes an
    //     equivalent-box approximation — a rectangle with dimensions
    //     Wₑ = √polygonAreaM2, Dₑ = polygonAreaM2 / Wₑ, Hₑ = He — and runs the
    //     same axial-mode formula on those equivalent dimensions. The exact
    //     frequencies are not physical but the psychoacoustic LF solidity is.
    //     Caller must supply polygonAreaM2 > 0 to activate the branch.
    //
    // Default `polygonAreaM2 = 0.0` preserves the v2.8.0 skip-for-polygon
    // behaviour for any caller that hasn't updated.
    static std::vector<double> applyModalBank (const std::vector<double>& buf,
                                               double W, double D, double He,
                                               double rt60_125, double gain, int sr,
                                               const std::string& shape = "Rectangular",
                                               double polygonAreaM2 = 0.0);

    // Allpass diffuser (matches JS makeAllpassDiffuser + inline processing)
    struct AllpassDiffuser
    {
        double g = 0.0;
        std::array<int,4>               delays{};
        std::array<std::vector<double>,4> bufs;
        std::array<int,4>               ptrs{};
        double process (double x);
    };
    static AllpassDiffuser makeAllpassDiffuser (int sr, double diffusion);
    /** ER diffusion: shorter incommensurate delays to avoid the 17.1 ms repeat from the default allpass. */
    static AllpassDiffuser makeAllpassDiffuserForER (int sr, double diffusion);

    static std::vector<double> renderCh (
        const std::vector<Ref>& refs,
        int irLen, double den, int sr, double diffusion,
        double reflectionSpreadMs = 0.0,
        double freqScatterMs = 0.0);  // per-band time scatter (0 = off); higher bands scatter more

    // Mean free path uses room volume / surface area. For rectangular rooms the
    // formulas vol = W·D·H and surf = 2(WD + DH + WH) apply verbatim. For polygon
    // shapes pass `paramsForShape != nullptr` so the function can compute
    // vol = polyArea·H and surf = 2·polyArea + polyPerim·H from makeWalls2D.
    // When the pointer is null OR the shape is "Rectangular" the original
    // rectangular formula is used unchanged (preserves IR_11 / IR_14 bit-identity).
    static std::vector<double> renderFDNTail (
        const std::vector<double>& rt60s,
        int irLen, int erCut,
        const std::vector<double>& erIR,
        double diffusion, int sr, uint32_t seed,
        double roomW, double roomD, double roomH,
        int maxRefCut = -1,                              // -1 → same as erCut (old behaviour)
        const IRSynthParams* paramsForShape = nullptr);  // null → rectangular formula

    // ── Multi-mic path synthesis (feature/multi-mic-paths, Phase 1.3) ──────
    // synthMainPath is the historical body of synthIR, unchanged (bit-identity
    // guarded by IR_14). synthExtraPath / synthDirectPath are sibling helpers
    // used for OUTRIG, AMBIENT and DIRECT mic pairs. The parallel dispatcher
    // (C5) fans synthIR out across these helpers using std::async.
    static IRSynthResult synthMainPath (const IRSynthParams& p, IRSynthProgressFn cb);

    // synthExtraPath — OUTRIG / AMBIENT. Identical engine to synthMainPath but
    // reads an independent mic pair (normalised 0–1 receiver positions,
    // absolute-metres height), independent mic pattern + angles, and an
    // independent seed base so the diffuse field and FDN seed are distinct
    // from MAIN. Speaker positions, room geometry, rt60, diffusion and all
    // other acoustic parameters are unchanged (same room, different ears).
    //   rlxNorm/rlyNorm/rrxNorm/rryNorm: normalised 0–1 mic positions (multiplied by width/depth internally).
    //   rzMetres: mic height in metres (clamped to He * 0.9 as MAIN clamps to min(3.0, He*0.9)).
    //   langle/rangle: mic face angles (same convention as p.micl_angle/p.micr_angle).
    //   pattern: mic polar pattern string (same keys as p.mic_pattern).
    //   seedBase: base seed for calcRefs (4 consecutive seeds consumed) and FDN (+58, +59).
    static MicIRChannels synthExtraPath (const IRSynthParams& p,
                                         double rlxNorm, double rlyNorm,
                                         double rrxNorm, double rryNorm,
                                         double rzMetres,
                                         double langle, double rangle,
                                         const std::string& pattern,
                                         uint32_t seedBase,
                                         IRSynthProgressFn cb,
                                         double ltilt = 0.0,
                                         double rtilt = 0.0);

    // synthDirectPath — order-0-only IR (direct arrivals only, no reflections,
    // no diffusion, no FDN tail, no modal bank, no end fade). Shares MAIN's
    // mic pattern + angles + receiver positions (D2). Returns a very short IR
    // (~2–60 ms depending on room size) sufficient for the direct ray plus
    // the 8-band bandpass-filter impulse-response tail.
    static MicIRChannels synthDirectPath (const IRSynthParams& p);
};
