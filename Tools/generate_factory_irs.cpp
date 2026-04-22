/**
 * generate_factory_irs.cpp
 *
 * Batch generator for P!NG factory IR WAV files, sidecar .ping XMLs,
 * and binary preset .xml files (JUCE "VC2!" format).
 *
 * Build:
 *   cd "/Users/paulthomson/Cursor wip/Ping"
 *   g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
 *       Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
 *       -o build/generate_factory_irs -lm
 *
 * Run:
 *   ./build/generate_factory_irs \
 *       Installer/factory_irs \
 *       Installer/factory_presets
 *
 * Each venue produces three files:
 *   <ir_outdir>/<Category>/<Name>.wav
 *   <ir_outdir>/<Category>/<Name>.ping      (IR Synth sidecar XML)
 *   <preset_outdir>/<Category>/<Name>.xml   (JUCE binary preset)
 *
 * Placement convention (Y axis = depth, 0=stage/source end, 1=far end):
 *   Halls          : sources y=0.20 (stage), mics y=0.70 (mid-audience)
 *   Large Spaces   : sources y=0.25,          mics y=0.75
 *   Scoring Stages : sources y=0.30,          mics y=0.65
 *   Rooms          : sources y=0.35,          mics y=0.65
 *   Tight Spaces   : sources y=0.25,          mics y=0.75
 */

#include "IRSynthEngine.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <sys/stat.h>

namespace fs = std::filesystem;

// ── EQ override for a preset ────────────────────────────────────────────────
struct EQPreset
{
    // Low shelf (b3)
    double b3freq = 0, b3gain = 0, b3q = 0;
    // Peak 1 (b0)
    double b0freq = 0, b0gain = 0, b0q = 0;
    // Peak 2 (b1)
    double b1freq = 0, b1gain = 0, b1q = 0;
    // Peak 3 (b2)
    double b2freq = 0, b2gain = 0, b2q = 0;
    // High shelf (b4)
    double b4freq = 0, b4gain = 0, b4q = 0;
};

// ── Venue definition ─────────────────────────────────────────────────────────
struct VenueDef
{
    const char*  category;   // "Halls" | "Large Spaces" | "Scoring Stages" | "Rooms" | "Tight Spaces"
    const char*  name;       // Display / file name
    IRSynthParams params;
    EQPreset      eq;        // Optional EQ for preset (zero fields = use default/flat)
    double        dryWet = 0.35;
    double        inputGainDb = 0.0; // IR input gain in dB (raw APVTS value, range -24..12)
};

// ── All venues ───────────────────────────────────────────────────────────────
// Angles (radians): π/2 ≈ 1.5708 (down), -3π/4 ≈ -2.3562 (up-left), -π/4 ≈ -0.7854 (up-right)
static const double PI_2   =  1.57079632679;
static const double NEG_3PI4 = -2.35619449019;
static const double NEG_PI4  = -0.785398163397;
static const double NEG_PI_6 = -0.5235987755982988; // -30° default mic tilt

// Outrigger / ambient face angles: straight up (-π/2) rotated 20° "in toward the source"
// For L mic (at low x), rotating toward +x = adding 20° → -π/2 + π/9
// For R mic (at high x), rotating toward -x = subtracting 20° → -π/2 - π/9
static const double OUT_L_ANG = -1.57079632679 +  0.349065850398866; // -π/2 + π/9 ≈ -70°
static const double OUT_R_ANG = -1.57079632679 -  0.349065850398866; // -π/2 - π/9 ≈ -110°

// Helper: build IRSynthParams with common angles applied
static IRSynthParams makeP(
    const char* shape,
    double W, double D, double H,
    const char* floor_, const char* ceil_, const char* wall_,
    double win, double aud, double diff,
    const char* vault, double organ, double balc,
    // placement (x = width%, y = depth%  -- both normalised 0..1)
    double slx, double sly, double srx, double sry,
    double rlx, double rly, double rrx, double rry,
    const char* micPat = "cardioid",
    double spklAngle = 1e99, double spkrAngle = 1e99,
    double miclAngle = 1e99, double micrAngle = 1e99)
{
    IRSynthParams p;
    p.shape            = shape;
    p.width  = W; p.depth  = D; p.height = H;
    p.floor_material   = floor_;
    p.ceiling_material = ceil_;
    p.wall_material    = wall_;
    p.window_fraction  = win;
    p.audience         = aud;
    p.diffusion        = diff;
    p.vault_type       = vault;
    p.organ_case       = organ;
    p.balconies        = balc;
    p.source_lx = slx;  p.source_ly = sly;
    p.source_rx = srx;  p.source_ry = sry;
    p.receiver_lx = rlx; p.receiver_ly = rly;
    p.receiver_rx = rrx; p.receiver_ry = rry;
    p.spkl_angle = (spklAngle < 1e90) ? spklAngle : PI_2;
    p.spkr_angle = (spkrAngle < 1e90) ? spkrAngle : PI_2;
    p.micl_angle = (miclAngle < 1e90) ? miclAngle : NEG_3PI4;
    p.micr_angle = (micrAngle < 1e90) ? micrAngle : NEG_PI4;
    p.mic_pattern = micPat;
    return p;
}

// Warm-hall EQ: gentle low-frequency warmth, slight high-frequency softening
static EQPreset warmHallEQ()
{
    EQPreset eq;
    eq.b3freq = 200;  eq.b3gain =  1.5; eq.b3q = 0.707;   // low shelf +1.5 dB
    eq.b4freq = 8000; eq.b4gain = -1.5; eq.b4q = 0.707;   // high shelf -1.5 dB
    return eq;
}

// Bright-chamber EQ: clarity boost for tiled / concrete echo chambers
static EQPreset brightChamberEQ()
{
    EQPreset eq;
    eq.b4freq = 6000; eq.b4gain = 2.0; eq.b4q = 0.707;    // high shelf +2 dB
    return eq;
}

// ── Standard factory mic setup ──────────────────────────────────────────────
// Applied to every venue before synthesis. Configures:
//   • MAIN  = Decca tree (MK21), centred on x=0.5 at existing mic-pair y
//   • OUTRIG = MK21 spaced pair at (0.25, decca_y) and (0.75, decca_y),
//              faces pointed 20° in toward the source from straight-up
//   • AMBIENT = omni pair at (0.10, 0.90) and (0.90, 0.90), same face angles
//   • DIRECT  = order-1 near-field tap, shares MAIN (Decca) mic pattern
//   • All mics tilted -30° (factory default)
//   • Lambert scatter OFF, speaker-directivity fade OFF (full cardioid)
// Speaker positions are preserved as authored per venue.
static void applyStandardMicSetup (IRSynthParams& p)
{
    // Use existing receiver pair's average y as the Decca / outrig row depth.
    const double deccaY = (p.receiver_ly + p.receiver_ry) * 0.5;

    // MAIN path — Decca tree (all three mics MK21, -30° tilt)
    p.mic_pattern         = "wide cardioid (MK21)";
    p.micl_tilt           = NEG_PI_6;
    p.micr_tilt           = NEG_PI_6;
    p.main_decca_enabled  = true;
    p.decca_cx            = 0.5;
    p.decca_cy            = deccaY;
    p.decca_angle         = -PI_2;                 // forward (toward low-y, i.e. stage)
    p.decca_centre_gain   = 0.5;                   // struct default
    p.decca_toe_out       = 1.5707963267948966;    // π/2 (struct default)
    p.decca_tilt          = NEG_PI_6;

    // OUTRIG path
    p.outrig_enabled      = true;
    p.outrig_lx           = 0.25;
    p.outrig_ly           = deccaY;
    p.outrig_rx           = 0.75;
    p.outrig_ry           = deccaY;
    p.outrig_langle       = OUT_L_ANG;
    p.outrig_rangle       = OUT_R_ANG;
    p.outrig_height       = 3.0;
    p.outrig_pattern      = "wide cardioid (MK21)";
    p.outrig_ltilt        = NEG_PI_6;
    p.outrig_rtilt        = NEG_PI_6;

    // AMBIENT path
    p.ambient_enabled     = true;
    p.ambient_lx          = 0.10;
    p.ambient_ly          = 0.90;
    p.ambient_rx          = 0.90;
    p.ambient_ry          = 0.90;
    p.ambient_langle      = OUT_L_ANG;
    p.ambient_rangle      = OUT_R_ANG;
    p.ambient_height      = 6.0;
    p.ambient_pattern     = "omni";
    p.ambient_ltilt       = NEG_PI_6;
    p.ambient_rtilt       = NEG_PI_6;

    // DIRECT — shares MAIN mic pattern + angles; direct_max_order=1 = first-order bounces
    p.direct_enabled      = true;
    p.direct_max_order    = 1;

    // Experimental early-reflection toggles
    p.lambert_scatter_enabled = false;
    p.spk_directivity_full    = true;
}

// ── Venue table ───────────────────────────────────────────────────────────────
static const std::vector<VenueDef> VENUES = {

    // ── HALLS ──────────────────────────────────────────────────────────────

    // Vienna Musikverein "Golden Hall"
    // 49 × 19 × 18 m. Classic rectangular shoebox. Verified dimensions.
    // Ornate plaster walls, hardwood floor, wooden balconies; horseshoe balconies.
    { "Halls", "Vienna Musikverein Golden Hall",
      makeP("Rectangular", 49, 19, 18,
            "Hardwood floor", "Painted plaster", "Plywood panel",
            /*win*/0.08, /*aud*/0.30, /*diff*/0.55,
            "None (flat)", /*organ*/0.40, /*balc*/0.60,
            0.1769230769230769, 0.662672064777328,  0.1707692307692308, 0.3135222672064777,
            0.4338461538461538, 0.6547368421052632,  0.4292307692307692, 0.377004048582996,
            "cardioid", 0.468828947339202, -0.6253684004209985,
            2.427501922850161, -2.595937647421048),
      warmHallEQ() },

    // Concertgebouw Amsterdam (Grote Zaal)
    // 27.5 × 27.75 × 18 m per the hall's own rental spec (floor area 763 m²
    // matches 27.5 × 27.75 exactly). Older "44 × 28 × 17" figure was wrong —
    // likely confused with an overall building length. RT60 ~2.2 s with audience.
    // Wooden panelling walls, plaster ceiling, balconies on three sides.
    { "Halls", "Concertgebouw Amsterdam",
      makeP("Rectangular", 28, 28, 18,
            "Hardwood floor", "Painted plaster", "Plywood panel",
            0.05, 0.45, 0.50,
            "None (flat)", 0.35, 0.55,
            0.25, 0.20,  0.75, 0.20,
            0.35, 0.70,  0.65, 0.70),
      warmHallEQ() },

    // Carnegie Hall — Isaac Stern Auditorium, New York
    // ~30 × 21 × 18 m. Painted plaster walls and ceiling; upholstered seats,
    // three tiers of balconies. Real RT60 ~1.7-2.0 s with full audience.
    { "Halls", "Carnegie Hall New York",
      makeP("Rectangular", 30, 21, 18,
            "Carpet (thin)", "Painted plaster", "Painted plaster",
            0.00, 0.65, 0.45,
            "None (flat)", 0.00, 0.55,
            0.25, 0.20,  0.75, 0.20,
            0.35, 0.72,  0.65, 0.72),
      warmHallEQ() },

    // Church of St Jude-on-the-Hill, Hampstead Garden Suburb
    // 37 × 15 × 12 m. Edwin Lutyens design. Barrel-vaulted/domed ceiling.
    // Stone walls, painted plaster barrel vault; stained glass windows.
    { "Halls", "St Jude-on-the-Hill Hampstead",
      makeP("Cathedral", 37, 15, 12,
            "Rough stone / rock", "Painted plaster", "Rough stone / rock",
            0.15, 0.10, 0.35,
            "Shallow barrel vault", 0.50, 0.10,
            0.4335384615384615, 0.2304123076923077,  0.5723692307692307, 0.219483076923077,
            0.4586461538461539, 0.7768738461538461,  0.539876923076923, 0.7768738461538461,
            "cardioid", 2.255505160484458, 0.7654008428193579) },

    // King's College Chapel, Cambridge
    // 88.4 × 12.2 × 24.4 m (289 ft × 40 ft × 80 ft interior height). Famous fan vault.
    // Previous 29 m height was the exterior (to top of pinnacles); interior to the
    // crown of the fan vault is 80 ft ≈ 24 m. Stone floor and walls; stained-glass
    // windows in lower bays only (~15% wall area). Target RT60: 4-6 s with partial audience.
    { "Halls", "King's College Chapel Cambridge",
      makeP("Cathedral", 88, 12, 24,
            "Rough stone / rock", "Rough stone / rock", "Rough stone / rock",
            0.15, 0.10, 0.25,
            "Fan vault  (King's College)", 0.60, 0.05,
            0.2149538461538461, 0.58,  0.2105230769230769, 0.4076923076923077,
            0.4054769230769231, 0.6538461538461539,  0.4054769230769231, 0.4015384615384615,
            "cardioid", 0.4237575093842993, -0.3923401077650537,
            2.5425772726579, -2.530494608480618) },

    // Sage Gateshead — Hall One
    // ~26 × 20 × 12 m (adjustable ceiling 10–21 m; mid position).
    // American Ash wood panelling throughout; designed as a high-diffusion shoebox.
    { "Halls", "Sage Gateshead Hall One",
      makeP("Rectangular", 26, 20, 12,
            "Hardwood floor", "Plywood panel", "Plywood panel",
            0.00, 0.40, 0.65,
            "None (flat)", 0.00, 0.30,
            0.25, 0.20,  0.75, 0.20,
            0.35, 0.72,  0.65, 0.72) },

    // ── LARGE SPACES ───────────────────────────────────────────────────────

    // Royal Albert Hall, London
    // 67 × 56 × 41 m elliptical / cylindrical (219 × 185 × 135 ft interior).
    // Height was previously 30 m; the real interior rises to 135 ft ≈ 41 m at the
    // dome peak. Post-1969: 3,000 acoustic mushrooms suspended from ceiling
    // (modelled as Acoustic ceiling tile); velvet box draping (Heavy curtains for
    // walls); Proms audience ~ maximum absorption. Target RT60: ~2.5-3.0 s.
    { "Large Spaces", "Royal Albert Hall London",
      makeP("Cylindrical", 67, 56, 41,
            "Carpet (thin)", "Painted plaster", "Painted plaster",
            0.00, 0.67, 0.55,
            "Coffered dome  (circular hall)", 0.56, 0.50,
            0.3995692307692307, 0.2666461538461538,  0.6063384615384616, 0.2696,
            0.4424, 0.6432615384615384,  0.5738461538461539, 0.6417846153846154,
            "cardioid", 2.056057171020652, 1.110441760215903),
      warmHallEQ() },

    // Elbphilharmonie Hamburg — Großer Saal
    // ~35 × 28 × 20 m, vineyard-style. 10,000 uniquely shaped diffusion panels.
    // Maximum diffusion by design; audience surrounds stage.
    { "Large Spaces", "Elbphilharmonie Hamburg",
      makeP("Rectangular", 35, 28, 20,
            "Hardwood floor", "Plywood panel", "Plywood panel",
            0.00, 0.55, 0.80,
            "None (flat)", 0.00, 0.70,
            0.30, 0.25,  0.70, 0.25,  // vineyard-style — wider speaker spread, standard depth convention
            0.35, 0.75,  0.65, 0.75),
      warmHallEQ() },

    // Sydney Opera House — Concert Hall
    // ~35 × 24 × 14 m, fan-shaped. Australian brush box timber throughout.
    // Suspended acrylic acoustic rings; brush box veneer ceiling panels.
    { "Large Spaces", "Sydney Opera House Concert Hall",
      makeP("Fan / Shoebox", 35, 24, 14,
            "Hardwood floor", "Plywood panel", "Plywood panel",
            0.00, 0.55, 0.50,
            "Shallow barrel vault", 0.00, 0.45,
            0.25, 0.20,  0.75, 0.20,
            0.35, 0.72,  0.65, 0.72),
      warmHallEQ() },

    // Hansa Tonstudio — Meistersaal (Großer Saal), Berlin
    // ~26 × 18 × 7 m (published floor area 463 m², wall height 7 m, volume
    // ~3,000 m³). Previous 26 × 25 gave 650 m² / 4,550 m³ which didn't match
    // either the stated area or volume. Grand ornate ballroom from 1913.
    // Large windows, wood floors, plaster walls. Recorded Bowie, U2, Depeche Mode.
    { "Large Spaces", "Hansa Meistersaal Berlin",
      makeP("Rectangular", 26, 18, 7,
            "Hardwood floor", "Painted plaster", "Painted plaster",
            0.20, 0.15, 0.45,
            "None (flat)", 0.00, 0.00,
            0.3676923076923077, 0.2296,  0.6169230769230769, 0.2296,
            0.4507692307692308, 0.5752,  0.5553846153846154, 0.5736,
            "cardioid", 2.313847021255637, 0.8960553566552649) },

    // Usher Hall, Edinburgh
    // ~32 × 22 × 16 m. Elliptical/horseshoe hall; decorative dome ceiling.
    // Classic early 20th-century concert hall. Harrison & Harrison organ.
    { "Large Spaces", "Usher Hall Edinburgh",
      makeP("Rectangular", 32, 22, 16,
            "Carpet (thin)", "Painted plaster", "Painted plaster",
            0.05, 0.45, 0.40,
            "Coffered dome  (circular hall)", 0.35, 0.60,
            0.25, 0.20,  0.75, 0.20,
            0.35, 0.72,  0.65, 0.72),
      warmHallEQ() },

    // ── SCORING STAGES ─────────────────────────────────────────────────────

    // Abbey Road — Studio One
    // 28 × 16 × 12 m (92 × 52 × 40 ft, published by Abbey Road / Mix magazine).
    // Previous 22 × 20 × 10 figures were wrong — the room is a long shoebox, not
    // square, and is 40 ft high. Floor area ~450 m² matches 28 × 16. Largest
    // purpose-built recording studio. Parquet floor; adjustable acoustic panels;
    // purpose-built for orchestral recording.
    { "Scoring Stages", "Abbey Road Studio One",
      makeP("Rectangular", 28, 16, 12,
            "Hardwood floor", "Painted plaster", "Plywood panel",
            0.05, 0.20, 0.55,
            "None (flat)", 0.00, 0.00,
            0.3846153846153846, 0.230923076923077,  0.6461538461538462, 0.230923076923077,
            0.4769230769230769, 0.7233846153846154,  0.556923076923077, 0.7233846153846154,
            "cardioid", 2.137525633964683, 1.012965695057059) },

    // Ocean Way Nashville — Studio A
    // 15.2 × 11.6 × 9.1 m (50 × 38 × 30 ft). Exact published dimensions.
    // Natural-sounding wood reflectors throughout; 4 isolation booths.
    { "Scoring Stages", "Ocean Way Nashville Studio A",
      makeP("Rectangular", 15.2, 11.6, 9.1,
            "Hardwood floor", "Plywood panel", "Plywood panel",
            0.08, 0.10, 0.60,
            "None (flat)", 0.00, 0.00,
            0.25, 0.30,  0.75, 0.30,
            0.35, 0.65,  0.65, 0.65) },

    // Capitol Studios Hollywood — Studio A
    // 19 × 14 × 6 m (62 × 46 × 20 ft, published; ~57,000 cu ft ≈ 1,600 m³ volume).
    // Previous 14 × 10 × 7 figures were undersized. Live concrete/plaster walls.
    // Famous echo chambers in the basement.
    { "Scoring Stages", "Capitol Studios Hollywood A",
      makeP("Rectangular", 19, 14, 6,
            "Hardwood floor", "Painted plaster", "Hardwood floor",
            0.05, 0.31, 0.40,
            "None (flat)", 0.00, 0.00,
            0.25, 0.35,  0.75, 0.35,
            0.35, 0.70,  0.65, 0.70) },

    // Warner Bros. Eastwood Scoring Stage, Burbank
    // ~28 × 22 × 9 m. Large Hollywood orchestral scoring stage.
    // Carpeted floor, acoustic tile ceiling, diffusion panels on walls.
    { "Scoring Stages", "Warner Bros Eastwood Scoring Stage",
      makeP("Rectangular", 28, 22, 9,
            "Carpet (thin)", "Acoustic ceiling tile", "Plywood panel",
            0.00, 0.12, 0.55,
            "None (flat)", 0.00, 0.00,
            0.3261538461538461, 0.2395804195804196,  0.7092307692307692, 0.2317482517482518,
            0.4630769230769231, 0.6879720279720281,  0.5415384615384615, 0.68993006993007,
            "cardioid", 2.32035835186019, 0.8663022080995093) },

    // ── ROOMS ──────────────────────────────────────────────────────────────

    // Abbey Road — Studio Two
    // 18.3 × 11.7 × 8.5 m (60 × 38 × 28 ft, published). Previous 7.31 m height
    // was an underestimate — the room is 28 ft to the ceiling. RT60 ~1.2 s.
    // Where the Beatles recorded. Parquet floor, adjustable LEDE panels.
    { "Rooms", "Abbey Road Studio Two",
      makeP("Rectangular", 18.3, 11.7, 8.5,
            "Hardwood floor", "Acoustic ceiling tile", "Plywood panel",
            0.05, 0.15, 0.55,
            "None (flat)", 0.00, 0.00,
            0.3030769230769231, 0.2450836120401338,  0.6123076923076923, 0.2401337792642141,
            0.3830769230769231, 0.7450167224080267,  0.5476923076923077, 0.7425418060200669,
            "cardioid", 1.972910241279746, 1.147038892898704) },

    // British Grove Studios — Studio 1 Main Room, London
    // ~16 × 12 × 6 m (approx). Munro Acoustics design. Slanted wood reflectors.
    { "Rooms", "British Grove Studios London",
      makeP("Rectangular", 16, 12, 6,
            "Hardwood floor", "Plywood panel", "Plywood panel",
            0.05, 0.20, 0.60,
            "None (flat)", 0.00, 0.00,
            0.3615384615384615, 0.2312820512820513,  0.5430769230769231, 0.2046153846153846,
            0.3661538461538462, 0.7317948717948718,  0.6092307692307692, 0.7317948717948718,
            "cardioid", 2.242508606109763, 1.007679657135154) },

    // Sunset Sound — Studio 2, Hollywood
    // ~11 × 9 × 4.5 m. Classic early rock/pop room. Concrete and plywood walls.
    // Van Halen, The Doors, Led Zeppelin, Prince.
    { "Rooms", "Sunset Sound Studio 2 Hollywood",
      makeP("Rectangular", 11, 9, 4.5,
            "Hardwood floor", "Plywood panel", "Concrete / bare brick",
            0.05, 0.10, 0.45,
            "None (flat)", 0.00, 0.00,
            0.3107692307692307, 0.2705982905982906,  0.6984615384615385, 0.2611965811965812,
            0.3707692307692308, 0.7143589743589743,  0.6169230769230769, 0.7181196581196581) },

    // Electric Lady Studios — Studio A, New York
    // ~11.6 × 10.7 × 4.5 m (Live Room listed as 35 × 38 ft). Previous 12 × 9 was
    // slightly under on depth. Jimi Hendrix's studio. Curved plaster walls,
    // carpeted floor.
    { "Rooms", "Electric Lady Studios New York",
      makeP("Rectangular", 11.6, 10.7, 4.5,
            "Carpet (thin)", "Plywood panel", "Painted plaster",
            0.00, 0.20, 0.50,
            "None (flat)", 0.00, 0.00,
            0.4184615384615384, 0.2169230769230769,  0.5738461538461539, 0.2148717948717949,
            0.4753846153846154, 0.7215384615384616,  0.5461538461538461, 0.7194871794871794,
            "cardioid", 1.981860832367087, 1.083897070083762) },

    // RAK Studios — Studio 1, London
    // ~12 × 9 × 5 m. North London boutique studio. Hardwood floors, wood panels.
    { "Rooms", "RAK Studios London",
      makeP("Rectangular", 12, 9, 5,
            "Hardwood floor", "Plywood panel", "Plywood panel",
            0.05, 0.15, 0.55,
            "None (flat)", 0.00, 0.00,
            0.4353846153846154, 0.2928205128205128,  0.5907692307692308, 0.2928205128205128,
            0.4738461538461539, 0.7625641025641026,  0.5584615384615385, 0.7625641025641026,
            "cardioid", 2.022697166595603, 0.8760579983331214) },

    // Metropolis Studios — Studio A, London
    // ~10 × 8 × 6 m (Live Room published 80 m² floor, 6 m ceiling).
    // Previous 14 × 10 × 5 didn't match the published floor area or the
    // 6 m ceiling. One of Europe's largest studio complexes.
    { "Rooms", "Metropolis Studios London",
      makeP("Rectangular", 10, 8, 6,
            "Hardwood floor", "Acoustic ceiling tile", "Plywood panel",
            0.05, 0.20, 0.55,
            "None (flat)", 0.00, 0.00,
            0.28, 0.35,  0.72, 0.35,
            0.37, 0.65,  0.63, 0.65) },

    // ── TIGHT SPACES ───────────────────────────────────────────────────────

    // Abbey Road — Echo Chamber
    // ~10 × 5 × 3 m. Tiled underground chamber below Studio Three.
    // Bare concrete and tile walls — extremely live.
    { "Tight Spaces", "Abbey Road Echo Chamber",
      makeP("Rectangular", 10, 5, 3,
            "Concrete / bare brick", "Concrete / bare brick", "Concrete / bare brick",
            0.00, 0.00, 0.20,
            "None (flat)", 0.00, 0.00,
            0.30, 0.25,  0.70, 0.25,
            0.38, 0.75,  0.62, 0.75),
      brightChamberEQ(), 0.35, -20.0 },

    // Capitol Studios — Underground Echo Chambers, Hollywood
    // ~8 × 4 × 2.5 m. Eight custom-built chambers beneath Capitol Tower.
    // Raw concrete throughout; among the most famous echo chambers in history.
    { "Tight Spaces", "Capitol Studios Echo Chamber",
      makeP("Rectangular", 8, 4, 2.5,
            "Concrete / bare brick", "Concrete / bare brick", "Concrete / bare brick",
            0.00, 0.00, 0.15,
            "None (flat)", 0.00, 0.00,
            0.30, 0.25,  0.70, 0.25,
            0.38, 0.75,  0.62, 0.75),
      brightChamberEQ(), 0.35, -20.0 },

    // Small Stone Recital Room
    // ~8 × 6 × 4 m. Modelled on Oxford college chapels and small vaulted stone rooms.
    // Stone walls and floor, barrel-vaulted plaster ceiling, stained glass.
    { "Tight Spaces", "Stone Recital Room",
      makeP("Cathedral", 8, 6, 4,
            "Rough stone / rock", "Painted plaster", "Rough stone / rock",
            0.15, 0.10, 0.35,
            "Shallow barrel vault", 0.20, 0.00,
            0.4187692307692308, 0.2853538461538462,  0.5812307692307692, 0.2833846153846154,
            0.4187692307692308, 0.7540307692307693,  0.5856615384615385, 0.7500923076923077,
            "cardioid", 2.186781481895591, 0.8884797851182471),
      {}, 0.35, -20.0 },

    // Concrete Stairwell
    // 4 × 4 × 12 m. Tall narrow concrete box — long flutter echoes, bright character.
    // Based on the distinctive stairwell reverb heard in countless recordings.
    { "Tight Spaces", "Concrete Stairwell",
      makeP("Rectangular", 4, 4, 12,
            "Concrete / bare brick", "Concrete / bare brick", "Concrete / bare brick",
            0.05, 0.00, 0.15,
            "None (flat)", 0.00, 0.00,
            0.30, 0.20,  0.70, 0.20,
            0.38, 0.80,  0.62, 0.80),
      brightChamberEQ(), 0.35, -20.0 },

    // Damped Studio Booth
    // 5 × 4 × 2.8 m. Small treated vocal/instrument booth.
    // Thick carpet floor, acoustic panels on walls and ceiling — near-anechoic character.
    { "Tight Spaces", "Damped Studio Booth",
      makeP("Rectangular", 5, 4, 2.8,
            "Carpet (thick)", "Acoustic ceiling tile", "Acoustic ceiling tile",
            0.08, 0.00, 0.20,
            "None (flat)", 0.00, 0.00,
            0.28, 0.30,  0.72, 0.30,
            0.37, 0.70,  0.63, 0.70),
      {}, 0.35, -20.0 },

    // Tiled Shower / Bathroom
    // 3 × 2.5 × 2.5 m. Domestic hard-surfaced room with classic short bright reverb.
    // All hard reflective surfaces — the sound every vocalist knows.
    { "Tight Spaces", "Tiled Shower Room",
      makeP("Rectangular", 3, 2.5, 2.5,
            "Concrete / bare brick", "Concrete / bare brick", "Concrete / bare brick",
            0.05, 0.00, 0.15,
            "None (flat)", 0.00, 0.00,
            0.28, 0.25,  0.72, 0.25,
            0.37, 0.75,  0.63, 0.75),
      brightChamberEQ(), 0.35, -20.0 },
};

// ── File / string helpers ────────────────────────────────────────────────────

static bool writeBytes(const fs::path& path, const uint8_t* data, size_t sz)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), (std::streamsize)sz);
    return f.good();
}

static bool writeText(const fs::path& path, const std::string& text)
{
    std::ofstream f(path);
    if (!f) return false;
    f << text;
    return f.good();
}

// ── Sidecar XML ──────────────────────────────────────────────────────────────
static std::string makeSidecarXML(const IRSynthParams& p)
{
    auto d = [](double v, int prec = 6) {
        std::ostringstream s;
        s << std::setprecision(prec) << v;
        return s.str();
    };
    auto b = [](bool v) { return v ? "1" : "0"; };

    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PingIRSynth>\n  <irSynthParams";
    x += " shape=\"" + p.shape + "\"";
    x += " width=\""  + d(p.width)  + "\"";
    x += " depth=\""  + d(p.depth)  + "\"";
    x += " height=\"" + d(p.height) + "\"";
    x += " floor=\""    + p.floor_material   + "\"";
    x += " ceiling=\""  + p.ceiling_material + "\"";
    x += " wall=\""     + p.wall_material    + "\"";
    x += " windows=\""  + d(p.window_fraction) + "\"";
    x += " audience=\"" + d(p.audience)  + "\"";
    x += " diffusion=\""+ d(p.diffusion) + "\"";
    x += " vault=\""    + p.vault_type   + "\"";
    x += " organ=\""    + d(p.organ_case) + "\"";
    x += " balconies=\""+ d(p.balconies) + "\"";
    x += " temp=\""     + d(p.temperature) + "\"";
    x += " humidity=\"" + d(p.humidity)   + "\"";
    x += " slx=\""  + d(p.source_lx,12)  + "\"";
    x += " sly=\""  + d(p.source_ly,12)  + "\"";
    x += " srx=\""  + d(p.source_rx,12)  + "\"";
    x += " sry=\""  + d(p.source_ry,12)  + "\"";
    x += " rlx=\""  + d(p.receiver_lx,12)+ "\"";
    x += " rly=\""  + d(p.receiver_ly,12)+ "\"";
    x += " rrx=\""  + d(p.receiver_rx,12)+ "\"";
    x += " rry=\""  + d(p.receiver_ry,12)+ "\"";
    x += " spkl=\"" + d(p.spkl_angle,12) + "\"";
    x += " spkr=\"" + d(p.spkr_angle,12) + "\"";
    x += " micl=\"" + d(p.micl_angle,12) + "\"";
    x += " micr=\"" + d(p.micr_angle,12) + "\"";
    x += " micPat=\""  + p.mic_pattern + "\"";
    x += " erOnly=\""  + std::string(b(p.er_only)) + "\"";
    x += " sr=\""      + std::to_string(p.sample_rate) + "\"";
    x += " bakeERTail=\""  + std::string(b(p.bake_er_tail_balance)) + "\"";
    x += " bakedERGain=\"" + d(p.baked_er_gain)   + "\"";
    x += " bakedTailGain=\"" + d(p.baked_tail_gain) + "\"";

    // Multi-mic path enables + experimental toggles
    x += " directOn=\""       + std::string(b(p.direct_enabled))   + "\"";
    x += " outrigOn=\""       + std::string(b(p.outrig_enabled))   + "\"";
    x += " ambientOn=\""      + std::string(b(p.ambient_enabled))  + "\"";
    x += " directMaxOrder=\"" + std::to_string(p.direct_max_order) + "\"";
    x += " lambertScatter=\"" + std::string(b(p.lambert_scatter_enabled)) + "\"";
    x += " spkDirFull=\""     + std::string(b(p.spk_directivity_full))    + "\"";

    // Outrigger pair
    x += " outrigLx=\""     + d(p.outrig_lx,12) + "\"";
    x += " outrigLy=\""     + d(p.outrig_ly,12) + "\"";
    x += " outrigRx=\""     + d(p.outrig_rx,12) + "\"";
    x += " outrigRy=\""     + d(p.outrig_ry,12) + "\"";
    x += " outrigLang=\""   + d(p.outrig_langle,12) + "\"";
    x += " outrigRang=\""   + d(p.outrig_rangle,12) + "\"";
    x += " outrigHeight=\"" + d(p.outrig_height) + "\"";
    x += " outrigPat=\""    + p.outrig_pattern + "\"";
    x += " outrigLtilt=\""  + d(p.outrig_ltilt,12) + "\"";
    x += " outrigRtilt=\""  + d(p.outrig_rtilt,12) + "\"";

    // Ambient pair
    x += " ambientLx=\""     + d(p.ambient_lx,12) + "\"";
    x += " ambientLy=\""     + d(p.ambient_ly,12) + "\"";
    x += " ambientRx=\""     + d(p.ambient_rx,12) + "\"";
    x += " ambientRy=\""     + d(p.ambient_ry,12) + "\"";
    x += " ambientLang=\""   + d(p.ambient_langle,12) + "\"";
    x += " ambientRang=\""   + d(p.ambient_rangle,12) + "\"";
    x += " ambientHeight=\"" + d(p.ambient_height) + "\"";
    x += " ambientPat=\""    + p.ambient_pattern + "\"";
    x += " ambientLtilt=\""  + d(p.ambient_ltilt,12) + "\"";
    x += " ambientRtilt=\""  + d(p.ambient_rtilt,12) + "\"";

    // MAIN tilt (elevation component of mic-facing axis)
    x += " miclTilt=\"" + d(p.micl_tilt,12) + "\"";
    x += " micrTilt=\"" + d(p.micr_tilt,12) + "\"";

    // Decca tree
    x += " deccaOn=\""      + std::string(b(p.main_decca_enabled)) + "\"";
    x += " deccaCx=\""      + d(p.decca_cx,12) + "\"";
    x += " deccaCy=\""      + d(p.decca_cy,12) + "\"";
    x += " deccaAng=\""     + d(p.decca_angle,12) + "\"";
    x += " deccaCtrGain=\"" + d(p.decca_centre_gain,12) + "\"";
    x += " deccaToeOut=\""  + d(p.decca_toe_out,12) + "\"";
    x += " deccaTilt=\""    + d(p.decca_tilt,12) + "\"";

    x += "/>\n</PingIRSynth>\n";
    return x;
}

// ── Preset XML (JUCE "VC2!" binary format) ───────────────────────────────────
static std::string makePresetXML(
    const std::string& irFilePath,
    const IRSynthParams& irp,
    const EQPreset& eq,
    double dryWet,
    double inputGainDb = 0.0)
{
    auto dv = [](double v) {
        std::ostringstream s;
        s << std::setprecision(10) << v;
        return s.str();
    };
    auto b = [](bool v) { return v ? "1" : "0"; };

    // Build irSynthParams attribute string (same content as sidecar, single-line)
    auto d12 = [](double v) {
        std::ostringstream s; s << std::setprecision(12) << v; return s.str();
    };
    auto d6 = [](double v) {
        std::ostringstream s; s << std::setprecision(6) << v; return s.str();
    };

    std::string isp;
    isp += " shape=\"" + irp.shape + "\"";
    isp += " width=\""  + d6(irp.width)  + "\"";
    isp += " depth=\""  + d6(irp.depth)  + "\"";
    isp += " height=\"" + d6(irp.height) + "\"";
    isp += " floor=\""    + irp.floor_material   + "\"";
    isp += " ceiling=\""  + irp.ceiling_material + "\"";
    isp += " wall=\""     + irp.wall_material    + "\"";
    isp += " windows=\""  + d6(irp.window_fraction) + "\"";
    isp += " audience=\"" + d6(irp.audience)  + "\"";
    isp += " diffusion=\""+ d6(irp.diffusion) + "\"";
    isp += " vault=\""    + irp.vault_type   + "\"";
    isp += " organ=\""    + d6(irp.organ_case) + "\"";
    isp += " balconies=\""+ d6(irp.balconies) + "\"";
    isp += " temp=\""     + d6(irp.temperature) + "\"";
    isp += " humidity=\"" + d6(irp.humidity)   + "\"";
    isp += " slx=\""  + d12(irp.source_lx)   + "\" sly=\""  + d12(irp.source_ly)   + "\"";
    isp += " srx=\""  + d12(irp.source_rx)   + "\" sry=\""  + d12(irp.source_ry)   + "\"";
    isp += " rlx=\""  + d12(irp.receiver_lx) + "\" rly=\""  + d12(irp.receiver_ly) + "\"";
    isp += " rrx=\""  + d12(irp.receiver_rx) + "\" rry=\""  + d12(irp.receiver_ry) + "\"";
    isp += " spkl=\"" + d12(irp.spkl_angle)  + "\" spkr=\"" + d12(irp.spkr_angle)  + "\"";
    isp += " micl=\"" + d12(irp.micl_angle)  + "\" micr=\"" + d12(irp.micr_angle)  + "\"";
    isp += " micPat=\""  + irp.mic_pattern + "\"";
    isp += " erOnly=\""  + std::string(b(irp.er_only)) + "\"";
    isp += " sr=\""      + std::to_string(irp.sample_rate) + "\"";
    isp += " bakeERTail=\""    + std::string(b(irp.bake_er_tail_balance)) + "\"";
    isp += " bakedERGain=\""   + d6(irp.baked_er_gain)    + "\"";
    isp += " bakedTailGain=\"" + d6(irp.baked_tail_gain)  + "\"";

    // Multi-mic path enables + experimental toggles
    isp += " directOn=\""       + std::string(b(irp.direct_enabled))   + "\"";
    isp += " outrigOn=\""       + std::string(b(irp.outrig_enabled))   + "\"";
    isp += " ambientOn=\""      + std::string(b(irp.ambient_enabled))  + "\"";
    isp += " directMaxOrder=\"" + std::to_string(irp.direct_max_order) + "\"";
    isp += " lambertScatter=\"" + std::string(b(irp.lambert_scatter_enabled)) + "\"";
    isp += " spkDirFull=\""     + std::string(b(irp.spk_directivity_full))    + "\"";

    // Outrigger pair
    isp += " outrigLx=\""     + d12(irp.outrig_lx)     + "\" outrigLy=\"" + d12(irp.outrig_ly) + "\"";
    isp += " outrigRx=\""     + d12(irp.outrig_rx)     + "\" outrigRy=\"" + d12(irp.outrig_ry) + "\"";
    isp += " outrigLang=\""   + d12(irp.outrig_langle) + "\" outrigRang=\"" + d12(irp.outrig_rangle) + "\"";
    isp += " outrigHeight=\"" + d6(irp.outrig_height)  + "\"";
    isp += " outrigPat=\""    + irp.outrig_pattern     + "\"";
    isp += " outrigLtilt=\""  + d12(irp.outrig_ltilt)  + "\" outrigRtilt=\"" + d12(irp.outrig_rtilt) + "\"";

    // Ambient pair
    isp += " ambientLx=\""     + d12(irp.ambient_lx)     + "\" ambientLy=\"" + d12(irp.ambient_ly) + "\"";
    isp += " ambientRx=\""     + d12(irp.ambient_rx)     + "\" ambientRy=\"" + d12(irp.ambient_ry) + "\"";
    isp += " ambientLang=\""   + d12(irp.ambient_langle) + "\" ambientRang=\"" + d12(irp.ambient_rangle) + "\"";
    isp += " ambientHeight=\"" + d6(irp.ambient_height)  + "\"";
    isp += " ambientPat=\""    + irp.ambient_pattern     + "\"";
    isp += " ambientLtilt=\""  + d12(irp.ambient_ltilt)  + "\" ambientRtilt=\"" + d12(irp.ambient_rtilt) + "\"";

    // MAIN tilt + Decca tree
    isp += " miclTilt=\""     + d12(irp.micl_tilt)        + "\" micrTilt=\""  + d12(irp.micr_tilt) + "\"";
    isp += " deccaOn=\""      + std::string(b(irp.main_decca_enabled)) + "\"";
    isp += " deccaCx=\""      + d12(irp.decca_cx)          + "\" deccaCy=\""   + d12(irp.decca_cy)    + "\"";
    isp += " deccaAng=\""     + d12(irp.decca_angle)       + "\"";
    isp += " deccaCtrGain=\"" + d12(irp.decca_centre_gain) + "\"";
    isp += " deccaToeOut=\""  + d12(irp.decca_toe_out)     + "\"";
    isp += " deccaTilt=\""    + d12(irp.decca_tilt)        + "\"";

    // Build parameter list — only non-default values are given a value= attribute.
    // Everything else gets an empty tag so JUCE uses the registered default.
    auto param = [&](const std::string& id, const std::string& val) -> std::string {
        if (val.empty()) return "<PARAM id=\"" + id + "\"/>";
        return "<PARAM id=\"" + id + "\" value=\"" + val + "\"/>";
    };
    auto eqV = [&](double v) -> std::string {
        if (v == 0.0) return "";
        std::ostringstream s; s << std::setprecision(10) << v; return s.str();
    };

    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?> ";
    x += "<Parameters irFilePath=\"" + irFilePath + "\"";
    x += " reverse=\"0\" licenceName=\"\" licenceSerial=\"\" licenceDisplayName=\"\">";

    // EQ (b3=low shelf, b0/b1/b2=peaks, b4=high shelf)
    x += param("b3freq", eqV(eq.b3freq));
    x += param("b3gain", eqV(eq.b3gain));
    x += param("b3q",    eqV(eq.b3q));
    x += param("b0freq", eqV(eq.b0freq));
    x += param("b0gain", eqV(eq.b0gain));
    x += param("b0q",    eqV(eq.b0q));
    x += param("b1freq", eqV(eq.b1freq));
    x += param("b1gain", eqV(eq.b1gain));
    x += param("b1q",    eqV(eq.b1q));
    x += param("b2freq", eqV(eq.b2freq));
    x += param("b2gain", eqV(eq.b2gain));
    x += param("b2q",    eqV(eq.b2q));
    x += param("b4freq", eqV(eq.b4freq));
    x += param("b4gain", eqV(eq.b4gain));
    x += param("b4q",    eqV(eq.b4q));

    // Core reverb params — drywet only, everything else at default
    x += param("drywet", dv(dryWet));
    x += param("decay",      "");
    x += param("delaydepth", "");
    x += param("erCrossfeedAttDb",  "");
    x += param("erCrossfeedDelayMs","");
    x += param("erCrossfeedOn",     "");
    x += param("erLevel",    "");
    x += param("inputGain",  inputGainDb != 0.0 ? dv(inputGainDb) : std::string(""));
    x += param("irInputDrive","");
    x += param("moddepth",   "");
    x += param("modrate",    "");
    x += param("outputGain", "");
    x += param("predelay",   "");
    x += param("reversetrim","");
    x += param("stretch",    "");
    x += param("tailCrossfeedAttDb",  "");
    x += param("tailCrossfeedDelayMs","");
    x += param("tailCrossfeedOn",     "");
    x += param("tailLevel",  "");
    x += param("tailmod",    "");
    x += param("tailrate",   "");
    x += param("width",      "");

    // Effects — all off by default
    x += param("bloomFeedback",""); x += param("bloomIRFeed","");
    x += param("bloomOn","");       x += param("bloomSize","");
    x += param("bloomTime","");     x += param("bloomVolume","");
    x += param("cloudDepth","");    x += param("cloudFeedback","");
    x += param("cloudIRFeed","");   x += param("cloudOn","");
    x += param("cloudRate","");     x += param("cloudSize","");
    x += param("cloudVolume","");
    x += param("plateColour","");   x += param("plateDiffusion","");
    x += param("plateIRFeed","");   x += param("plateOn","");
    x += param("plateSize","");
    x += param("shimDelay","");     x += param("shimFeedback","");
    x += param("shimIRFeed","");    x += param("shimOn","");
    x += param("shimPitch","");     x += param("shimSize","");
    x += param("shimVolume","");

    // Multi-mic mixer strips — only MAIN is audible by default; aux paths are
    // calculated (so the user can switch them in from the mic mixer) but
    // muted at the strip level.
    x += param("mainOn",    "1");
    x += param("directOn",  "0");
    x += param("outrigOn",  "0");
    x += param("ambientOn", "0");
    x += param("mainGain",   "");  x += param("directGain",  "");
    x += param("outrigGain", "");  x += param("ambientGain", "");
    x += param("mainPan",    "");  x += param("directPan",   "");
    x += param("outrigPan",  "");  x += param("ambientPan",  "");
    x += param("mainMute",   "");  x += param("directMute",  "");
    x += param("outrigMute", "");  x += param("ambientMute", "");
    x += param("mainSolo",   "");  x += param("directSolo",  "");
    x += param("outrigSolo", "");  x += param("ambientSolo", "");
    x += param("mainHPOn",   "");  x += param("directHPOn",  "");
    x += param("outrigHPOn", "");  x += param("ambientHPOn", "");

    // Embed IR synth params so the IR Synth panel loads correctly when this preset is used
    x += "<irSynthParams" + isp + "/>";

    x += "</Parameters>";
    return x;
}

// Write preset in JUCE "VC2!" binary format: [magic 4B][size 4B][XML][NUL]
static bool writePreset(const fs::path& path, const std::string& xmlStr)
{
    const uint8_t magic[4] = { 'V', 'C', '2', '!' };
    uint32_t sz = (uint32_t)xmlStr.size();

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(magic), 4);
    f.write(reinterpret_cast<const char*>(&sz), 4);
    f.write(xmlStr.c_str(), xmlStr.size());
    f.write("\0", 1);
    return f.good();
}

// ── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    umask(022);

    if (argc < 3)
    {
        std::cerr << "Usage: generate_factory_irs <ir_outdir> <preset_outdir>\n"
                  << "  e.g. generate_factory_irs Installer/factory_irs Installer/factory_presets\n";
        return 1;
    }

    fs::path irBase     = argv[1];
    fs::path presetBase = argv[2];

    int total   = (int)VENUES.size();
    int done    = 0;
    int failed  = 0;

    std::cout << "=== P!NG Factory IR Generator ===\n"
              << "Venues: " << total << "\n"
              << "IR output:     " << irBase     << "\n"
              << "Preset output: " << presetBase << "\n\n";

    for (const auto& venueConst : VENUES)
    {
        // Take a mutable copy so we can apply the standard multi-mic setup
        // (Decca + outriggers + ambients) on top of the authored venue parameters.
        VenueDef venue = venueConst;
        applyStandardMicSetup (venue.params);

        std::cout << "[" << (done + failed + 1) << "/" << total << "] "
                  << venue.category << " / " << venue.name << "\n";
        std::cout.flush();

        // Create output directories
        fs::path irDir     = irBase     / venue.category;
        fs::path presetDir = presetBase / venue.category;
        try {
            fs::create_directories(irDir);
            fs::create_directories(presetDir);
        } catch (const std::exception& e) {
            std::cerr << "  ERROR creating directories: " << e.what() << "\n";
            ++failed;
            continue;
        }

        fs::path wavPath    = irDir    / (std::string(venue.name) + ".wav");
        fs::path sidecarPath= irDir    / (std::string(venue.name) + ".ping");
        fs::path presetPath = presetDir/ (std::string(venue.name) + ".xml");
        fs::path directPath = irDir    / (std::string(venue.name) + "_direct.wav");
        fs::path outrigPath = irDir    / (std::string(venue.name) + "_outrig.wav");
        fs::path ambientPath= irDir    / (std::string(venue.name) + "_ambient.wav");

        // Generate IR
        auto t0 = std::chrono::steady_clock::now();

        IRSynthResult result = IRSynthEngine::synthIR(
            venue.params,
            [](double frac, const std::string& msg) {
                std::cout << "    " << std::fixed << std::setprecision(0)
                          << (frac * 100.0) << "% " << msg << "         \r";
                std::cout.flush();
            });

        auto t1  = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        std::cout << "    Done in " << std::fixed << std::setprecision(1) << elapsed
                  << " s  (" << (result.irLen / result.sampleRate) << " s IR, "
                  << result.iLL.size() << " samples)\n";

        if (!result.success)
        {
            std::cerr << "  FAILED: " << result.errorMessage << "\n";
            ++failed;
            continue;
        }

        // Write MAIN WAV
        auto wavBytes = IRSynthEngine::makeWav(
            result.iLL, result.iRL, result.iLR, result.iRR, result.sampleRate);
        if (!writeBytes(wavPath, wavBytes.data(), wavBytes.size()))
        {
            std::cerr << "  ERROR writing WAV: " << wavPath << "\n";
            ++failed;
            continue;
        }
        std::cout << "    WAV:     " << wavPath.filename() << " ("
                  << (wavBytes.size() / 1024) << " KB)\n";

        // Write DIRECT / OUTRIG / AMBIENT sibling WAVs (auto-loaded by the
        // plugin when the MAIN file is selected — see IRManager sibling rules).
        auto writeAux = [&](const MicIRChannels& ch, const fs::path& p, const char* label) {
            if (!ch.synthesised || ch.LL.empty()) return;
            auto bytes = IRSynthEngine::makeWav (ch.LL, ch.RL, ch.LR, ch.RR, result.sampleRate);
            if (!writeBytes (p, bytes.data(), bytes.size()))
            {
                std::cerr << "  ERROR writing " << label << ": " << p << "\n";
                return;
            }
            std::cout << "    " << label << ":  " << p.filename() << " ("
                      << (bytes.size() / 1024) << " KB)\n";
        };
        writeAux (result.direct,  directPath,  "Direct ");
        writeAux (result.outrig,  outrigPath,  "Outrig ");
        writeAux (result.ambient, ambientPath, "Ambient");

        // Write sidecar .ping XML
        auto sidecarXML = makeSidecarXML(venue.params);
        if (!writeText(sidecarPath, sidecarXML))
        {
            std::cerr << "  ERROR writing sidecar: " << sidecarPath << "\n";
            ++failed;
            continue;
        }
        std::cout << "    Sidecar: " << sidecarPath.filename() << "\n";

        // Write preset binary
        std::string irFilePath = "/Library/Application Support/Ping/Factory IRs/"
            + std::string(venue.category) + "/" + std::string(venue.name) + ".wav";
        auto presetXML = makePresetXML(irFilePath, venue.params, venue.eq, venue.dryWet, venue.inputGainDb);
        if (!writePreset(presetPath, presetXML))
        {
            std::cerr << "  ERROR writing preset: " << presetPath << "\n";
            ++failed;
            continue;
        }
        std::cout << "    Preset:  " << presetPath.filename() << "\n";

        ++done;
    }

    std::cout << "\n=== Complete: " << done << " succeeded, " << failed << " failed ===\n";
    return (failed > 0) ? 1 : 0;
}
