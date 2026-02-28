#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <array>

/** All parameters for one IR synthesis run. */
struct IRSynthParams
{
    // Room
    std::string shape = "Rectangular";  // "Rectangular"|"L-shaped"|"Fan / Shoebox"|"Cylindrical"|"Cathedral"|"Octagonal"
    double width  = 28.0;
    double depth  = 16.0;
    double height = 12.0;

    // Surfaces
    std::string floor_material   = "Hardwood floor";
    std::string ceiling_material = "Acoustic ceiling tile";
    std::string wall_material    = "Painted plaster";

    // Contents
    double audience  = 0.0;   // 0..1
    double diffusion = 0.4;   // 0..1

    // Architecture
    std::string vault_type = "None (flat)";
    double organ_case = 0.0;  // 0..1
    double balconies  = 0.0;  // 0..1

    // Air
    double temperature = 20.0; // °C  (currently only affects speed-of-sound display, not used in engine in v5)
    double humidity    = 50.0; // %   (same — keep as stored param for display; AIR[] array is hardcoded)

    // Placement — normalised 0..1 within room footprint
    // Speakers: centre (y=0.5), 25%/75% across, facing down. Mics: 1/5 up from bottom (y=0.8), 25%/75% across
    double source_lx   = 0.25; double source_ly   = 0.5;
    double source_rx   = 0.75; double source_ry   = 0.5;
    double receiver_lx = 0.25; double receiver_ly = 0.8;
    double receiver_rx = 0.75; double receiver_ry = 0.8;

    // Angles (radians): 0=right, π/2=down, -π/2=up. Speakers down (π/2), mics up-left (-3π/4) / up-right (-π/4)
    double spkl_angle = 1.57079632679;   // π/2 down
    double spkr_angle = 1.57079632679;
    double micl_angle = -2.35619449019;  // -3π/4 up-left
    double micr_angle = -0.785398163397; // -π/4 up-right

    // Options
    std::string mic_pattern = "cardioid"; // "omni"|"subcardioid"|"cardioid"|"figure8"
    bool        er_only     = false;
    int         sample_rate = 48000;      // 44100 | 48000
};

struct IRSynthResult
{
    std::vector<double> iLL, iRL, iLR, iRR;  // L→L, R→L, L→R, R→R
    std::vector<double> rt60;   // 6 bands: 125 250 500 1k 2k 4k
    int   irLen     = 0;
    int   sampleRate= 0;
    bool  success   = false;
    std::string errorMessage;
};

/**
 * Progress callback: (fraction 0..1, message string)
 * Called from the synthesis thread — must be thread-safe.
 */
using IRSynthProgressFn = std::function<void(double, const std::string&)>;

/**
 * Pure C++ port of the IR Synthesiser v5 acoustic engine.
 * All methods are static — no state, fully re-entrant.
 */
class IRSynthEngine
{
public:
    /** Main entry point. Runs synchronously — call from a background thread. */
    static IRSynthResult synthIR (const IRSynthParams& p, IRSynthProgressFn cb);

    /** Compute RT60 at 6 bands without doing a full synthesis. */
    static std::vector<double> calcRT60 (const IRSynthParams& p);

    /** Encode stereo IR to 24-bit WAV bytes. */
    static std::vector<uint8_t> makeWav (const std::vector<double>& iLL,
                                         const std::vector<double>& iRL,
                                         const std::vector<double>& iLR,
                                         const std::vector<double>& iRR,
                                         int sampleRate);

private:
    // ── constants (match JS verbatim) ──────────────────────────────────────
    static const double SPEED;   // 343.0
    static const int    BANDS[6];// {125,250,500,1000,2000,4000}

    // Material absorption coefficients [14 materials × 6 bands]
    static const std::map<std::string, std::array<double,6>>& getMats();

    // Vault profile table [name → {hm, vs, vHfA}]
    static const std::map<std::string, std::array<double,3>>& getVP();

    // Audience/balcony/organ absorption arrays
    static const double OA[6], BA[6], BSA[6], AIR[6];

    // Mic polar pattern {omni,subcardioid,cardioid,figure8} → {o, d}
    static const std::map<std::string, std::pair<double,double>>& getMIC();

    // ── engine helpers ─────────────────────────────────────────────────────
    static double eyring (double vol, double mAbs, double tS);

    static double micG  (double az, const std::string& pat, double faceAngle);
    static double spkG  (double faceAngle, double azToReceiver);

    // Seeded RNG (matches JS mkRng)
    struct Rng { uint32_t r; double next(); };
    static Rng mkRng (uint32_t seed);
    static double rU  (Rng& rng, double lo, double hi);

    struct Ref { int t; std::array<double,6> amps; double az; };

    static std::vector<Ref> calcRefs (
        double rx, double ry, double rz,
        double sx, double sy, double sz,
        const IRSynthParams& p,
        double He, int mo,
        const std::array<double,6>& rF,
        const std::array<double,6>& rC,
        const std::array<double,6>& rW,
        double oF, double vHfA, double ts,
        bool eo, int ec, int sr,
        uint32_t seed,
        const std::string& micPat,
        double spkFaceAngle, double micFaceAngle);

    static std::vector<double> bpF (const std::vector<double>& buf, double fc, int sr);
    static std::vector<double> lpF (const std::vector<double>& buf, double fc, int sr);
    static std::vector<double> hpF (const std::vector<double>& buf, double fc, int sr);

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

    static std::vector<double> renderCh (
        const std::vector<Ref>& refs,
        int irLen, double den, int sr, double diffusion);

    static std::vector<double> renderFDNTail (
        const std::vector<double>& rt60s,
        int irLen, int erCut,
        const std::vector<double>& erIR,
        double diffusion, int sr, uint32_t seed,
        double roomW, double roomD, double roomH);
};
