#include "IRSynthEngine.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <future>
#include <atomic>
#include <mutex>

// ── constants ──────────────────────────────────────────────────────────────
const double IRSynthEngine::SPEED   = 343.0;
const int    IRSynthEngine::N_BANDS = 8;
const int    IRSynthEngine::BANDS[8] = { 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

// Material absorption coefficients [14 materials × 8 bands]
// Columns: 125, 250, 500, 1k, 2k, 4k Hz (from JS MATS verbatim) + 8k, 16k Hz (ISO 354).
// Hard surfaces plateau at 0.02–0.10 by 8 kHz; fibrous materials peak ~4–8 kHz then roll off.
static std::map<std::string, std::array<double,8>> s_mats;
static const std::map<std::string, std::array<double,8>>& initMats()
{
    if (s_mats.empty())
    {
        //                                              125    250    500     1k     2k     4k     8k    16k
        s_mats["Concrete / bare brick"]     = {{0.02, 0.03, 0.03, 0.04, 0.05, 0.07, 0.09, 0.10}};
        s_mats["Painted plaster"]           = {{0.01, 0.02, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07}};
        s_mats["Hardwood floor"]            = {{0.04, 0.04, 0.07, 0.06, 0.06, 0.07, 0.10, 0.12}};
        s_mats["Carpet (thin)"]              = {{0.03, 0.05, 0.10, 0.20, 0.30, 0.35, 0.50, 0.60}};
        s_mats["Carpet (thick)"]             = {{0.08, 0.24, 0.57, 0.69, 0.71, 0.73, 0.78, 0.75}};
        s_mats["Glass (large pane)"]         = {{0.18, 0.06, 0.04, 0.03, 0.02, 0.02, 0.02, 0.02}};
        s_mats["Heavy curtains"]             = {{0.07, 0.31, 0.49, 0.75, 0.70, 0.60, 0.65, 0.62}};
        s_mats["Acoustic ceiling tile"]     = {{0.25, 0.45, 0.78, 0.92, 0.89, 0.87, 0.88, 0.85}};
        s_mats["Plywood panel"]             = {{0.28, 0.22, 0.17, 0.09, 0.10, 0.11, 0.12, 0.13}};
        s_mats["Upholstered seats"]         = {{0.49, 0.66, 0.80, 0.88, 0.82, 0.70, 0.75, 0.72}};
        s_mats["Bare wooden seats"]          = {{0.02, 0.03, 0.03, 0.06, 0.06, 0.05, 0.07, 0.08}};
        s_mats["Water / pool surface"]      = {{0.01, 0.01, 0.01, 0.02, 0.02, 0.03, 0.03, 0.04}};
        s_mats["Rough stone / rock"]         = {{0.02, 0.03, 0.03, 0.04, 0.04, 0.05, 0.07, 0.08}};
        s_mats["Exposed brick (rough)"]      = {{0.03, 0.03, 0.03, 0.04, 0.05, 0.07, 0.09, 0.10}};
    }
    return s_mats;
}
const std::map<std::string, std::array<double,8>>& IRSynthEngine::getMats() { return initMats(); }

// Vault profile [name → {hm, vs, vHfA}] — verbatim from JS VP (note: vault names with 2 spaces)
static std::map<std::string, std::array<double,3>> s_vp;
static const std::map<std::string, std::array<double,3>>& initVP()
{
    if (s_vp.empty())
    {
        s_vp["None (flat)"]                              = {{1.00, 0.00, 0.00}};
        s_vp["Shallow barrel vault"]                     = {{1.12, 0.08, 0.01}};
        s_vp["Deep pointed vault (gothic)"]               = {{1.40, 0.18, 0.02}};  // JS: 'Deep pointed vault  (gothic)'
        s_vp["Deep pointed vault  (gothic)"]             = {{1.40, 0.18, 0.02}};
        s_vp["Groin / cross vault (Lyndhurst Hall)"]      = {{1.25, 0.28, 0.02}};  // JS: 2 spaces
        s_vp["Groin / cross vault  (Lyndhurst Hall)"]    = {{1.25, 0.28, 0.02}};
        s_vp["Fan vault (King's College)"]                = {{1.30, 0.38, 0.03}};
        s_vp["Fan vault  (King's College)"]               = {{1.30, 0.38, 0.03}};
        s_vp["Coffered dome (circular hall)"]             = {{1.20, 0.22, 0.01}};
        s_vp["Coffered dome  (circular hall)"]            = {{1.20, 0.22, 0.01}};
    }
    return s_vp;
}
const std::map<std::string, std::array<double,3>>& IRSynthEngine::getVP() { return initVP(); }

// Audience, balcony, organ, air — 8 bands [125 250 500 1k 2k 4k 8k 16k]
// OA/BA/BSA: 125–4k verbatim from JS; 8k/16k extended from ISO 354 / acoustic reference data.
// AIR: 125–4k verbatim from JS; 8k/16k from ISO 9613-1 (20°C, 50% RH).
// Usage in calcRefs: pow(10, -AIR[b] * dist_metres / 20) — amplitude attenuation dB/m.
const double IRSynthEngine::OA[8]  = { 0.06, 0.10, 0.14, 0.18, 0.22, 0.28, 0.32, 0.35 };
const double IRSynthEngine::BA[8]  = { 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.10 };
const double IRSynthEngine::BSA[8] = { 0.30, 0.45, 0.60, 0.72, 0.68, 0.58, 0.48, 0.40 };
const double IRSynthEngine::AIR[8] = { 0.0003, 0.001, 0.002, 0.005, 0.011, 0.026, 0.066, 0.200 };

// Mic polar pattern — per-octave-band {o, d} pairs.
// Bands: [125, 250, 500, 1k, 2k, 4k, 8k, 16k] Hz.
// Convention: for most patterns o + d = 1.0 at every band — on-axis gain is
// frequency-flat and only off-axis rejection varies with frequency.
// Exception: "omni (MK2H)" models the Schoeps MK 2H's narrow-inlet HF shelf
// (gold-ring acoustic elevation above ~6 kHz) by letting o > 1 at HF; since
// d = 0 this affects on-axis colour only, not polar pattern.
static std::map<std::string, std::array<std::pair<double,double>, 8>> s_mic;
static const std::map<std::string, std::array<std::pair<double,double>, 8>>& initMIC()
{
    if (s_mic.empty())
    {
        // Values derived from published polar pattern measurements for each mic family.

        // Pure pressure transducer — omnidirectional at all frequencies
        s_mic["omni"] = {{
            {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00},
            {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}
        }};

        // Schoeps MK 2H-style: omnidirectional with a mild on-axis HF elevation
        // from its narrow sound inlet (gold-ring acoustic shelf above ~6 kHz).
        // Published MK 2H free-field response: flat to ~4 kHz, gentle rise
        // starting ~5 kHz, peaking ~+3 to +4 dB around 10–12 kHz. Since d = 0
        // the mic is still omni at every band — only on-axis gain varies.
        //   4 kHz: +0.4 dB    8 kHz: +2.6 dB    16 kHz: +3.8 dB
        s_mic["omni (MK2H)"] = {{
            {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00},
            {1.00, 0.00}, {1.05, 0.00}, {1.35, 0.00}, {1.55, 0.00}
        }};

        // Wide pickup, broadens further at low end
        s_mic["subcardioid"] = {{
            {0.85, 0.15}, {0.82, 0.18}, {0.78, 0.22}, {0.70, 0.30},
            {0.65, 0.35}, {0.60, 0.40}, {0.55, 0.45}, {0.50, 0.50}
        }};

        // Schoeps MK 21-style: frequency-independent wide cardioid (α ≈ 0.70 mid,
        // slight broadening below 250 Hz, slight narrowing above 8 kHz).
        // Rear rejection holds ~−8 to −12 dB across the full audio band — the MK 21's
        // defining "extremely consistent polar response".
        s_mic["wide cardioid (MK21)"] = {{
            {0.77, 0.23}, {0.75, 0.25}, {0.73, 0.27}, {0.70, 0.30},
            {0.70, 0.30}, {0.68, 0.32}, {0.65, 0.35}, {0.62, 0.38}
        }};

        // Large-diaphragm condenser (~1" capsule) — significant narrowing above 1 kHz.
        // "cardioid" kept as backward-compat alias so older saved presets continue to work.
        std::array<std::pair<double,double>, 8> ldcData = {{
            {0.78, 0.22}, {0.68, 0.32}, {0.57, 0.43}, {0.50, 0.50},
            {0.40, 0.60}, {0.28, 0.72}, {0.16, 0.84}, {0.06, 0.94}
        }};
        s_mic["cardioid (LDC)"] = ldcData;
        s_mic["cardioid"]       = ldcData;  // backward-compat for saved presets

        // Small-diaphragm condenser (~12-16mm capsule) — more consistent directivity across frequency
        s_mic["cardioid (SDC)"] = {{
            {0.65, 0.35}, {0.58, 0.42}, {0.53, 0.47}, {0.50, 0.50},
            {0.44, 0.56}, {0.36, 0.64}, {0.28, 0.72}, {0.18, 0.82}
        }};

        // Ribbon figure-8 — fairly consistent but slight omni component at very low end
        // due to cabinet diffraction effects below ~200 Hz
        s_mic["figure8"] = {{
            {0.12, 0.88}, {0.06, 0.94}, {0.02, 0.98}, {0.00, 1.00},
            {0.00, 1.00}, {0.00, 1.00}, {0.00, 1.00}, {0.00, 1.00}
        }};

        // Neumann M50-like sphere-mounted pressure transducer. Behaves as a
        // pure omni below ~1 kHz and progressively narrows above, approaching a
        // wide-cardioid shape (α≈0.7) by 4 kHz and beyond. On-axis gain stays
        // frequency-flat (o+d=1) so the existing micG formula is unchanged.
        // Used by the Decca Tree capture mode (Docs/deep-research-report.md §"Canonical geometry").
        s_mic["M50-like"] = {{
            {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00},
            {0.95, 0.05}, {0.85, 0.15}, {0.75, 0.25}, {0.70, 0.30}
        }};
    }
    return s_mic;
}
const std::map<std::string, std::array<std::pair<double,double>, 8>>& IRSynthEngine::getMIC() { return initMIC(); }

// ── eyring — verbatim from JS ──────────────────────────────────────────────
double IRSynthEngine::eyring (double vol, double mAbs, double tS)
{
    if (mAbs >= 1.0) return 0.0;
    double l = std::log(1.0 - mAbs);
    if (std::abs(l) < 1e-9) return 99.0;
    return 0.161 * vol / (-tS * l);
}

// ── directivityCos — 3D source-to-mic-axis cosine ─────────────────────────
// Spherical law of cosines:  cos(theta) = sin(el)·sin(faceEl)
//                                       + cos(el)·cos(faceEl)·cos(az - faceAz)
// where (az, el) is the direction the sound arrives from (source direction
// in the receiver's frame) and (faceAz, faceEl) is the mic's pointing axis.
// At faceEl = 0 (horizontal) this reduces to cos(el)·cos(az - faceAz) — the
// old 2D formula multiplied by a cos(el) elevation factor. Sources directly
// overhead (el = ±π/2) therefore correctly map to the mic's vertical-axis
// rejection (cos(theta) = sin(el)·sin(faceEl)).
//
// This is the single source of truth for 3D mic directivity in the engine.
// Tests/PingDSPTests.cpp duplicates the formula as directivityCosLocal for
// DSP_21 to keep the DSP test layer free of IRSynthEngine.h coupling.
double IRSynthEngine::directivityCos (double az, double el,
                                      double faceAzimuth, double faceElevation) noexcept
{
    return std::sin(el) * std::sin(faceElevation)
         + std::cos(el) * std::cos(faceElevation) * std::cos(az - faceAzimuth);
}

// ── micG — per-octave-band polar pattern gain ─────────────────────────────
// band ∈ [0, 7] indexing octave bands [125 Hz .. 16 kHz]. Called once per
// (reflection, band) from calcRefs so each frequency band sees its own
// off-axis rejection while on-axis (o + d = 1) remains frequency-flat.
//
// cosTheta is precomputed by the caller via directivityCos, hoisted out of
// the per-band loop so the spherical-law-of-cosines call (4 trig ops) runs
// once per reflection rather than once per (reflection × band).
double IRSynthEngine::micG (int band, const std::string& pat, double cosTheta)
{
    const auto& mic = getMIC();
    auto it = mic.find(pat);
    if (it == mic.end()) return 1.0;
    const double o = it->second[band].first;
    const double d = it->second[band].second;
    return std::max(0.0, o + d * cosTheta);
}

// ── spkG — verbatim from JS (pure cardioid) ────────────────────────────────
double IRSynthEngine::spkG (double faceAngle, double azToReceiver)
{
    return std::max(0.0, 0.5 + 0.5 * std::cos(azToReceiver - faceAngle));
}

// ── RNG — verbatim from JS mkRng ───────────────────────────────────────────
IRSynthEngine::Rng IRSynthEngine::mkRng (uint32_t seed) { return { seed & 0xFFFFFFFFu }; }
double IRSynthEngine::Rng::next()
{
    r += 0x6D2B79F5u;
    uint32_t t = r;
    t = (uint32_t)((int32_t)(t ^ (t >> 15)) * (int32_t)(t | 1));
    t ^= t + (uint32_t)((int32_t)(t ^ (t >> 7)) * (int32_t)(t | 61));
    return (double)((t ^ (t >> 14)) & 0xFFFFFFFFu) / 4294967296.0;
}
double IRSynthEngine::rU (Rng& rng, double lo, double hi) { return lo + rng.next() * (hi - lo); }

// ── calcRT60 — verbatim from JS ────────────────────────────────────────────
std::vector<double> IRSynthEngine::calcRT60 (const IRSynthParams& p)
{
    auto& mats = getMats();
    auto vpIt = getVP().find(p.vault_type);
    if (vpIt == getVP().end()) vpIt = getVP().find("None (flat)");
    double hm = vpIt != getVP().end() ? vpIt->second[0] : 1.0;
    double H = p.height * hm;

    double fA = p.width * p.depth;
    double cA = fA * (1.0 + (hm - 1.0) * 1.6);
    double sA = p.depth * H * 2.0;
    double eA = p.width * H * 2.0;
    double tS = fA + cA + sA + eA;

    double vol = p.width * p.depth * H;
    double bA = fA * 0.25 * p.balconies;
    double oA = p.width * H * 0.15 * p.organ_case;

    auto mfIt = mats.find(p.floor_material);
    auto mcIt = mats.find(p.ceiling_material);
    auto mwIt = mats.find(p.wall_material);
    const auto& mf = mfIt != mats.end() ? mfIt->second : mats.find("Hardwood floor")->second;
    const auto& mc = mcIt != mats.end() ? mcIt->second : mats.find("Acoustic ceiling tile")->second;
    const auto& mw_base = mwIt != mats.end() ? mwIt->second : mats.find("Painted plaster")->second;
    const auto& mw_glass = mats.find("Glass (large pane)")->second;
    std::array<double,8> mw;
    double wf = std::max(0.0, std::min(1.0, p.window_fraction));
    for (int i = 0; i < N_BANDS; ++i)
        mw[i] = (1.0 - wf) * mw_base[i] + wf * mw_glass[i];

    std::vector<double> rt60(N_BANDS);
    for (int i = 0; i < N_BANDS; ++i)
    {
        double a = fA * mf[i] + cA * mc[i] + (sA + eA) * mw[i]
                 + p.audience * fA * 0.5 + bA * BA[i] + bA * 0.6 * BSA[i] + oA * OA[i];
        double rt = eyring(vol, a / tS, tS);
        rt = std::max(0.05, std::min(rt, 30.0));
        rt60[i] = 1.0 / (1.0 / rt + AIR[i] * SPEED / 60.0);
    }
    return rt60;
}

// ── calcRefs — verbatim from JS image-source loop ───────────────────────────
std::vector<IRSynthEngine::Ref> IRSynthEngine::calcRefs (
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
    double minJitterMs,
    double highOrderJitterMs,
    double micFaceTilt)
{
    double W = p.width, D = p.depth;
    Rng rng = mkRng(seed);
    std::vector<Ref> refs;

    for (int nx = -mo; nx <= mo; ++nx)
        for (int ny = -mo; ny <= mo; ++ny)
            for (int nz = -mo; nz <= mo; ++nz)
            {
                int totalBounces = std::abs(nx) + std::abs(ny) + std::abs(nz);
                double ix = nx * W + (nx % 2 ? W - sx : sx);
                double iy = ny * D + (ny % 2 ? D - sy : sy);
                double iz = nz * He + (nz % 2 ? He - sz : sz);
                double dist = std::sqrt((ix - rx) * (ix - rx) + (iy - ry) * (iy - ry) + (iz - rz) * (iz - rz));
                if (dist < 1e-6) continue;
                // Skip image sources beyond the target window — the FDN tail covers
                // everything further out.  This bounds the reflection count to the
                // sources that are actually distinct from diffuse reverberation.
                if (dist > maxRefDist) continue;

                int t = (int)std::floor(dist / SPEED * sr);
                if (ts > 0.05) t = std::max(1, t + (int)std::floor(rU(rng, -ts * 4.0, ts * 4.0) * sr / 1000.0));
                // Order-dependent jitter when sources are close: keep direct and first-order
                // tight (small jitter); scramble order 2+ to break the periodic decaying echo
                // that comes from repeated bounces (e.g. floor-ceiling at 2*H/c).
                double jitterMs = (totalBounces >= 2) ? highOrderJitterMs : minJitterMs;
                if (jitterMs > 0.0) t = std::max(1, t + (int)std::floor(rU(rng, -jitterMs, jitterMs) * sr / 1000.0));
                // In ER-only mode, skip image sources that arrive at or beyond the ER
                // window boundary.  In full-reverb mode keep all sources — late ones seed
                // the FDN (ef=0 after ecFdn prevents them appearing in the ER output).
                if (eo && t >= ec) continue;
                // Screen-style room coordinates: 0 = right, +pi/2 = down.
                double az = std::atan2(iy - ry, ix - rx);
                // 3D mic directivity: compute the source elevation in the receiver's
                // frame and the spherical-law-of-cosines projection onto the mic axis
                // ONCE per reflection, then reuse across all 8 bands. The cos(el)
                // factor implicit in directivityCos correctly attenuates sources that
                // are well above or below the mic plane (e.g. ceiling-bounce image
                // sources arriving from steep elevations).
                const double hDist   = std::sqrt((ix - rx) * (ix - rx) + (iy - ry) * (iy - ry));
                const double el      = std::atan2(iz - rz, std::max(hDist, 1e-9));
                const double cosTh3D = directivityCos(az, el, micFaceAngle, micFaceTilt);
                // micG is now called per-band inside the loop below so each octave
                // band sees its own off-axis rejection (frequency-dependent polar pattern).
                // Source directivity: by default, direct (0) and first-order (1)
                // use the full speaker pattern; order 2 is a 50/50 blend with
                // omni; order 3+ is fully omnidirectional. This fade avoids
                // over-attenuating late reflections that in reality arrive from
                // all directions.
                //
                // When IRSynthParams::spk_directivity_full is true, the fade is
                // disabled and every reflection order keeps the full cardioid —
                // an A/B knob for testing whether early-reflection directional
                // cues are being lost to the fade.
                double spkAz = std::atan2(ry - sy, rx - sx);
                double sgDir = spkG(spkFaceAngle, spkAz);
                double sg;
                if (p.spk_directivity_full)
                    sg = sgDir;
                else if (totalBounces <= 1)
                    sg = sgDir;
                else if (totalBounces == 2)
                    sg = 0.5 * sgDir + 0.5;
                else
                    sg = 1.0;
                double polarity = (totalBounces % 2 == 0) ? 1.0 : -1.0;

                std::array<double,8> amps;
                for (int b = 0; b < N_BANDS; ++b)
                {
                    double a = 1.0 / std::max(dist, 0.5);
                    a *= std::pow(rF[b], std::ceil(std::abs(nz) / 2.0));
                    a *= std::pow(rC[b], std::floor(std::abs(nz) / 2.0));
                    a *= std::pow(rW[b], std::abs(nx) + std::abs(ny));
                    if (std::abs(ny) > 0) a *= std::pow(oF, std::abs(ny));
                    if (std::abs(nz) > 0) a *= std::pow(1.0 - vHfA * std::min(b / 3.0, 1.0), std::abs(nz));
                    amps[b] = a * std::pow(10.0, -AIR[b] * dist / 20.0) * micG(b, micPat, cosTh3D) * sg * polarity;
                }
                refs.push_back({ t, amps, az });

                // Feature A — Lambert diffuse scattering: add N_SCATTER secondary refs per
                // bounce at orders 1–3 so the space between specular spikes is filled.
                // Runtime-toggleable via IRSynthParams::lambert_scatter_enabled (default true
                // preserves bit-identity with pre-experiment builds).
                const int N_SCATTER = 2;
                if (p.lambert_scatter_enabled && ts > 0.05 && totalBounces >= 1 && totalBounces <= 3)
                {
                    double scatterWeight = (ts * 0.08) / (double)N_SCATTER * std::pow(0.6, totalBounces - 1);
                    for (int s = 0; s < N_SCATTER; ++s)
                    {
                        double scatterAz = rU(rng, -3.141592653589793, 3.141592653589793);
                        int scatterT = t + (int)std::round(rU(rng, 0.0, 4.0) * sr / 1000.0);
                        scatterT = std::max(1, scatterT);
                        // Gate scatter refs beyond the ER window in ER-only mode.
                        // Parent t < ec, but the +0–4 ms scatter offset could push it over.
                        if (eo && scatterT >= ec) continue;
                        std::array<double,8> scatterAmps;
                        for (int b = 0; b < N_BANDS; ++b)
                            scatterAmps[b] = amps[b] * scatterWeight;
                        refs.push_back({ scatterT, scatterAmps, scatterAz });
                    }
                }
            }
    return refs;
}

// ── bpF — verbatim from JS (bandpass) ──────────────────────────────────────
std::vector<double> IRSynthEngine::bpF (const std::vector<double>& buf, double fc, int sr)
{
    double wl = std::max(fc / 1.414, 20.0) / (sr / 2.0);
    double wh = std::min(fc * 1.414, sr / 2.0 - 1.0) / (sr / 2.0);
    double wc = (wl + wh) / 2.0;
    double Q = wc / (wh - wl);
    double K = std::tan(wc * 3.141592653589793 * 0.5);
    double n = 1.0 / (1.0 + K / Q + K * K);
    double b0 = K / Q * n, b2 = -b0, a1 = 2.0 * (K * K - 1.0) * n, a2 = (1.0 - K / Q + K * K) * n;

    std::vector<double> out(buf.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < buf.size(); ++i)
    {
        double x = buf[i];
        double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        out[i] = y;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
    }
    return out;
}

// ── lpF — verbatim from JS (lowpass) ───────────────────────────────────────
std::vector<double> IRSynthEngine::lpF (const std::vector<double>& buf, double fc, int sr)
{
    double K = std::tan(3.141592653589793 * fc / sr);
    double n = 1.0 / (1.0 + 1.414213562373095 * K + K * K);
    double b0 = K * K * n, b1 = 2.0 * b0, b2 = b0;
    double a1 = 2.0 * (K * K - 1.0) * n, a2 = (1.0 - 1.414213562373095 * K + K * K) * n;

    std::vector<double> out(buf.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < buf.size(); ++i)
    {
        double x = buf[i];
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        out[i] = y;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
    }
    return out;
}

// ── hpF — verbatim from JS (highpass) ───────────────────────────────────────
std::vector<double> IRSynthEngine::hpF (const std::vector<double>& buf, double fc, int sr)
{
    double K = std::tan(3.141592653589793 * fc / sr);
    double n = 1.0 / (1.0 + 1.414213562373095 * K + K * K);
    double b0 = n, b1 = -2.0 * n, b2 = n;
    double a1 = 2.0 * (K * K - 1.0) * n, a2 = (1.0 - 1.414213562373095 * K + K * K) * n;

    std::vector<double> out(buf.size());
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < buf.size(); ++i)
    {
        double x = buf[i];
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        out[i] = y;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
    }
    return out;
}

// ── bpFQ — high-Q bandpass biquad for modal resonators ─────────────────────
// Same design as bpF but with explicit Q instead of the fixed octave-band Q.
std::vector<double> IRSynthEngine::bpFQ (const std::vector<double>& buf, double fc, double Q, int sr)
{
    double K = std::tan(3.141592653589793 * fc / (double)sr);
    double n = 1.0 / (1.0 + K / Q + K * K);
    double b0 = K / Q * n, b2 = -b0;
    double a1 = 2.0 * (K * K - 1.0) * n;
    double a2 = (1.0 - K / Q + K * K) * n;
    std::vector<double> out(buf.size());
    double x1=0, x2=0, y1=0, y2=0;
    for (size_t i = 0; i < buf.size(); ++i)
    {
        double x = buf[i];
        double y = b0*x + b2*x2 - a1*y1 - a2*y2;
        out[i] = y;  x2=x1; x1=x; y2=y1; y1=y;
    }
    return out;
}

// ── applyModalBank — add axial room-mode resonances below ~250 Hz ───────────
// The image-source method is ray-based and under-represents low-frequency
// standing-wave energy.  A parallel bank of IIR resonators tuned to the axial
// modes of the room adds the modal ringing characteristic of large enclosed spaces.
// Each resonator's Q is derived from the 125 Hz RT60 so modes decay correctly.
std::vector<double> IRSynthEngine::applyModalBank (
    const std::vector<double>& buf,
    double W, double D, double He,
    double rt60_125, double gain, int sr)
{
    // Axial mode frequencies: f_n = c*n / (2*L), n = 1..4 per dimension
    std::vector<double> modes;
    for (int n = 1; n <= 4; ++n)
    {
        double fx = SPEED * n / (2.0 * W);
        double fy = SPEED * n / (2.0 * D);
        double fz = SPEED * n / (2.0 * He);
        if (fx > 10.0 && fx < 250.0) modes.push_back(fx);
        if (fy > 10.0 && fy < 250.0) modes.push_back(fy);
        if (fz > 10.0 && fz < 250.0) modes.push_back(fz);
    }
    if (modes.empty()) return buf;

    std::vector<double> modal(buf.size(), 0.0);
    for (double fc : modes)
    {
        // Q = π*fc*RT60 / ln(1000) — standard decay-rate / bandwidth relationship
        double Q = std::clamp(3.141592653589793 * fc * rt60_125 / 6.908, 8.0, 80.0);
        // Lower modes carry more energy; weight ∝ 60/fc, clamped to avoid extreme boosts
        double modeGain = gain * std::min(60.0 / fc, 2.0);
        std::vector<double> res = bpFQ(buf, fc, Q, sr);
        for (size_t i = 0; i < modal.size(); ++i)
            modal[i] += res[i] * modeGain;
    }
    // Normalise by mode count so total level is stable regardless of room dimensions
    const double norm = 1.0 / (double)modes.size();
    std::vector<double> out(buf.size());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = buf[i] + modal[i] * norm;
    return out;
}

// ── makeAllpassDiffuser — verbatim from JS ─────────────────────────────────
IRSynthEngine::AllpassDiffuser IRSynthEngine::makeAllpassDiffuser (int sr, double diffusion)
{
    AllpassDiffuser ap;
    ap.g = 0.5 + diffusion * 0.25;
    ap.delays[0] = (int)std::round(sr * 0.0171);
    ap.delays[1] = (int)std::round(sr * 0.0063);
    ap.delays[2] = (int)std::round(sr * 0.0023);
    ap.delays[3] = (int)std::round(sr * 0.0008);
    for (int i = 0; i < 4; ++i)
    {
        ap.bufs[i].resize((size_t)ap.delays[i], 0.0);
        ap.ptrs[i] = 0;
    }
    return ap;
}

// ── makeAllpassDiffuserForER — ER diffusion without dominant 17 ms repeat ─
// The default allpass (17.1, 6.3, 2.3, 0.8 ms) creates a clear repeating delay
// at 17.1 ms when used on the early reflections. Use shorter incommensurate
// delays so no single period dominates.
IRSynthEngine::AllpassDiffuser IRSynthEngine::makeAllpassDiffuserForER (int sr, double diffusion)
{
    AllpassDiffuser ap;
    ap.g = 0.5 + diffusion * 0.25;
    ap.delays[0] = (int)std::round(sr * 0.0081);
    ap.delays[1] = (int)std::round(sr * 0.0047);
    ap.delays[2] = (int)std::round(sr * 0.0023);
    ap.delays[3] = (int)std::round(sr * 0.0007);
    for (int i = 0; i < 4; ++i)
    {
        ap.bufs[i].resize((size_t)ap.delays[i], 0.0);
        ap.ptrs[i] = 0;
    }
    return ap;
}

double IRSynthEngine::AllpassDiffuser::process (double x)
{
    double s = x;
    for (int i = 0; i < 4; ++i)
    {
        int d = delays[i];
        int p = ptrs[i];
        double delayed = bufs[i][(size_t)p];
        double w = s + g * delayed;
        bufs[i][(size_t)p] = w;
        ptrs[i] = (p + 1) % d;
        s = -g * w + delayed;
    }
    return s;
}

// ── renderCh — verbatim from JS (per-band buffers, sum filtered, then diffuser) ─
std::vector<double> IRSynthEngine::renderCh (
    const std::vector<Ref>& refs,
    int irLen, double den, int sr, double diffusion,
    double reflectionSpreadMs,
    double freqScatterMs)
{
    std::vector<std::vector<double>> bi(N_BANDS);
    for (int b = 0; b < N_BANDS; ++b)
        bi[b].resize((size_t)irLen, 0.0);

    const int spreadHalf = (reflectionSpreadMs > 0.0)
        ? std::max(1, (int)std::round(reflectionSpreadMs * 0.001 * sr * 0.5))
        : 0;

    for (const auto& r : refs)
    {
        double lat = std::abs(std::sin(r.az));
        if (spreadHalf <= 0)
        {
            if (r.t >= irLen) continue;
            for (int b = 0; b < N_BANDS; ++b)
            {
                int bt = r.t;
                if (freqScatterMs > 0.0 && b > 0)
                {
                    // Frequency-dependent scattering: higher frequency bands scatter more
                    // because shorter wavelengths interact with surface micro-structure.
                    // A deterministic hash of (arrival_sample, band) gives reproducible,
                    // per-band uncorrelated offsets — no extra RNG state needed.
                    uint32_t h = ((uint32_t)(r.t + 1) * 2654435769u) ^ ((uint32_t)b * 1234567891u);
                    double frac = (double)(h & 0xFFFF) / 65535.0 - 0.5;  // −0.5 … +0.5
                    double scale = (double)b / (double)(N_BANDS - 1);     // 0 @ 125Hz → 1 @ 16kHz
                    bt += (int)std::round(frac * 2.0 * freqScatterMs * scale * sr / 1000.0);
                    bt = std::clamp(bt, 0, irLen - 1);
                }
                bi[b][(size_t)bt] += r.amps[b] * den * (1.0 - lat * ((double)b / (double)(N_BANDS - 1)) * 0.5);
            }
        }
        else
        {
            // Spread this reflection over ±spreadHalf samples with triangular envelope
            // (weights sum to 1 so total energy is preserved).
            double weightSum = 0.0;
            for (int d = -spreadHalf; d <= spreadHalf; ++d)
                weightSum += 1.0 - (double)std::abs(d) / (double)(spreadHalf + 1);
            const double invSum = (weightSum > 1e-12) ? 1.0 / weightSum : 1.0;
            for (int d = -spreadHalf; d <= spreadHalf; ++d)
            {
                int i = r.t + d;
                if (i < 0 || i >= irLen) continue;
                double w = (1.0 - (double)std::abs(d) / (double)(spreadHalf + 1)) * invSum;
                for (int b = 0; b < N_BANDS; ++b)
                    bi[b][(size_t)i] += r.amps[b] * den * (1.0 - lat * ((double)b / (double)(N_BANDS - 1)) * 0.5) * w;
            }
        }
    }

    std::vector<double> raw((size_t)irLen, 0.0);
    for (int b = 0; b < N_BANDS; ++b)
    {
        double m = 0.0;
        for (size_t i = 0; i < bi[b].size(); ++i)
            if (std::abs(bi[b][i]) > m) m = std::abs(bi[b][i]);
        if (m > 1e-12)
        {
            std::vector<double> filt = bpF(bi[b], (double)BANDS[b], sr);
            for (int i = 0; i < irLen; ++i)
                raw[(size_t)i] += filt[(size_t)i];
        }
    }

    // Temporal smoothing DISABLED: a 5 ms moving average replaces each sample with
    // the window mean, which divides each reflection peak by ~(window length).
    // That attenuated content after 60 ms by ~200× and caused "extremely quiet
    // reverb". To soften periodic peaks without killing level, use a gentler
    // method (e.g. very short window, or envelope-based smoothing).
    // {
    //     const int smoothStart = (int)std::round(0.060 * sr);
    //     const int halfWin    = (int)std::round(0.0025 * sr);
    //     ...
    // }

    if (diffusion > 0.02)
    {
        // Deferred allpass diffusion: keep discrete early reflections sharp,
        // then blend into diffused output from 65 ms with a 20 ms crossfade.
        //
        // IMPORTANT: start the allpass loop at dryEnd, NOT at sample 0.
        // If we process from sample 0, strong early spikes create allpass
        // echoes that bleed into the wet region.  Starting at dryEnd means
        // the allpass state is cold (zero) when diffusion kicks in.
        // Use ER-specific allpass (shorter incommensurate delays) so we don't
        // get a dominant 17.1 ms repeating delay from the default allpass.
        const int dryEnd   = (int)std::round(0.065 * sr);  // pure-dry up to here
        const int fadeLen  = (int)std::round(0.020 * sr);  // crossfade window
        const int wetStart = dryEnd + fadeLen;              // fully-wet from here

        std::vector<double> wet((size_t)irLen, 0.0);
        AllpassDiffuser diff = makeAllpassDiffuserForER(sr, diffusion);
        for (int i = dryEnd; i < irLen; ++i)
            wet[(size_t)i] = diff.process(raw[(size_t)i]);

        // 0 … dryEnd-1  →  raw unchanged (dry)
        // dryEnd … wetStart-1  →  linear crossfade dry→wet
        for (int i = dryEnd; i < wetStart && i < irLen; ++i)
        {
            const double t  = (double)(i - dryEnd) / (double)fadeLen;
            raw[(size_t)i]  = raw[(size_t)i] * (1.0 - t) + wet[(size_t)i] * t;
        }
        // wetStart … irLen-1  →  fully wet
        for (int i = wetStart; i < irLen; ++i)
            raw[(size_t)i] = wet[(size_t)i];
    }
    return raw;
}

// ── nearestPrime — verbatim from JS ───────────────────────────────────────
static int nearestPrime (int n)
{
    n = std::max(2, (int)std::round((double)n));
    auto isPrime = [](int x) {
        if (x < 2) return false;
        for (int i = 2; i * i <= x; ++i)
            if (x % i == 0) return false;
        return true;
    };
    for (int d = 0; ; ++d)
    {
        if (isPrime(n + d)) return n + d;
        if (d > 0 && isPrime(n - d)) return n - d;
    }
}

// ── hadamard16 — verbatim from JS ─────────────────────────────────────────
static void hadamard16 (std::vector<double>& a)
{
    const double s = 0.25;
    for (int step = 1; step < 16; step <<= 1)
        for (int i = 0; i < 16; i += step * 2)
            for (int j = i; j < i + step; ++j)
            {
                double u = a[j], w = a[j + step];
                a[j] = u + w;
                a[j + step] = u - w;
            }
    for (int i = 0; i < 16; ++i)
        a[i] *= s;
}

// ── renderFDNTail — verbatim from JS (N=16 FDN, Hadamard, LFO modulation) ──
std::vector<double> IRSynthEngine::renderFDNTail (
    const std::vector<double>& rt60s,
    int irLen, int erCut,
    const std::vector<double>& erIR,
    double diffusion, int sr, uint32_t seed,
    double roomW, double roomD, double roomH,
    int maxRefCut)
{
    const int N = 16;
    double vol = roomW * roomD * roomH;
    double surf = 2.0 * (roomW * roomD + roomD * roomH + roomW * roomH);
    double mfp = 4.0 * vol / surf;
    double mfpMs = mfp / SPEED * 1000.0;
    double minMs = std::max(20.0, mfpMs * 0.5);
    // Avoid a fixed 150 ms longest line (was max(150, ...)) which caused an audible
    // repeating delay when speakers were coincident or close to one mic.
    double maxMs = std::max(130.0, mfpMs * 3.0);

    // Power-law spacing so the longest delay lines are spread (not clustered at maxMs).
    // Reduces the level of a single dominant recurrence; exponent 1.4 spreads the top.
    std::vector<int> delays(N);
    const double power = 1.4;
    for (int i = 0; i < N; ++i)
    {
        double frac = (N > 1) ? std::pow((double)i / (double)(N - 1), power) : 1.0;
        double t = minMs + (maxMs - minMs) * frac;
        delays[i] = nearestPrime((int)std::round(t * sr / 1000.0));
    }
    for (int i = 1; i < N; ++i)
        if (delays[i] <= delays[i - 1])
            delays[i] = nearestPrime(delays[i - 1] + 2);

    // LFO modulation depth varies per delay line depending on how HF-attenuated
    // that line is.  A line with alpha ≈ 1.0 (LF-dominated, little HF content)
    // can tolerate ±1.2 ms without audible chorusing.  A line with low alpha
    // (fast HF decay, significant HF content) must use a much shallower depth
    // (≈ ±0.3 ms) to avoid pitch-modulation artefacts on high-frequency transients.
    // Linear interpolation on alpha: depth = MIN + (MAX – MIN) * alpha.
    const double LFO_DEPTH_MAX_MS = 1.2;   // for LF-dominated lines (alpha → 1.0)
    const double LFO_DEPTH_MIN_MS = 0.3;   // for HF-attenuated lines (alpha → 0.0)
    // Per-line depths are populated after the lpAlpha loop below.
    std::vector<int> lfoDepthSamp(N, 0);

    // Buffer size is determined by the maximum possible modulation depth (LFO_DEPTH_MAX_MS).
    int maxLfoDepthSamp = (int)std::round(LFO_DEPTH_MAX_MS * sr / 1000.0);

    std::vector<std::vector<double>> bufs(N);
    for (int i = 0; i < N; ++i)
        bufs[i].resize((size_t)(delays[i] + maxLfoDepthSamp * 2 + 4), 0.0);

    std::vector<int> writePtr(N, 0);
    std::vector<double> lfoRates(N), lfoPhaseAcc(N);
    {
        // Use geometric (not linear/arithmetic) spacing for LFO rates so that no
        // two delay lines share a rational beat frequency.  Linear spacing
        // (0.07 + i*0.025 Hz) creates beats at exact multiples of 0.025 Hz; the
        // worst pair (line 0 vs 15) beats at 0.375 Hz (~2.7 s period), which is
        // audible as a periodic density fluctuation in a long tail.
        //
        // Geometric spacing: rate_i = r_base * k^i  where k is derived so the
        // series spans 0.07 – 0.45 Hz over N=16 lines.  Because k is irrational,
        // no two rates have a rational ratio, so their beats are aperiodic.
        const double r_base_hz = 0.07;
        const double r_top_hz  = 0.45;
        const double k = std::pow(r_top_hz / r_base_hz, 1.0 / (N - 1)); // ≈ 1.1321
        const double twoPiOverSr = 2.0 * 3.141592653589793 / sr;
        const double channelPhaseOffset = (seed == 101 ? 3.141592653589793 : 0.0);
        for (int i = 0; i < N; ++i)
        {
            lfoRates[i]    = r_base_hz * std::pow(k, i) * twoPiOverSr;
            lfoPhaseAcc[i] = channelPhaseOffset + i * 0.7853;
        }
    }

    auto readFrac = [&](int ch, int delaySamp) -> double
    {
        int len = (int)bufs[ch].size();
        double readF = ((writePtr[ch] - delaySamp) % len + len) % len;
        int r0 = (int)std::floor(readF);
        double frac = readF - r0;
        int r1 = (r0 + 1) % len;
        return bufs[ch][r0] * (1.0 - frac) + bufs[ch][r1] * frac;
    };

    std::vector<double> lpState(N, 0.0);
    struct LpAlpha { double alpha; double gain; };
    std::vector<LpAlpha> lpAlpha(N);
    for (int i = 0; i < N; ++i)
    {
        int d = delays[i];
        double gLF = std::pow(10.0, -3.0 * d / (rt60s[0] * sr));
        // Use 16 kHz RT60 (rt60s[7]) as HF reference — more aggressive than the previous
        // 4 kHz reference (rt60s[5]).  Air absorption above 8 kHz is substantial, so the
        // 1-pole LP correctly darkens large-room tails in proportion to their size.
        double gHF = std::pow(10.0, -3.0 * d / (rt60s[7] * sr));
        double alpha = std::min(0.9995, std::max(0.05, gHF / std::max(gLF, 1e-9)));
        lpAlpha[i] = { alpha, gLF };
    }

    // Compute per-line LFO modulation depth from each line's filter coefficient.
    // alpha is already clamped to [0.05, 0.9995] above; the extra clamp here is
    // defensive against any future floating-point edge cases.
    for (int i = 0; i < N; ++i)
    {
        double a = std::max(0.0, std::min(1.0, lpAlpha[i].alpha));
        double depthMs = LFO_DEPTH_MIN_MS + (LFO_DEPTH_MAX_MS - LFO_DEPTH_MIN_MS) * a;
        lfoDepthSamp[i] = (int)std::round(depthMs * sr / 1000.0);
    }

    std::vector<double> tmpMix(N);

    auto fdnStep = [&](double inject, int sampleIdx) -> double
    {
        (void)sampleIdx;
        for (int ch = 0; ch < N; ++ch)
        {
            lfoPhaseAcc[ch] += lfoRates[ch];
            double lfoMod = lfoDepthSamp[ch] * std::sin(lfoPhaseAcc[ch]);
            tmpMix[ch] = readFrac(ch, delays[ch] + (int)lfoMod);
        }
        hadamard16(tmpMix);

        double sum = 0.0;
        for (int ch = 0; ch < N; ++ch)
        {
            double alpha = lpAlpha[ch].alpha, gain = lpAlpha[ch].gain;
            lpState[ch] = alpha * tmpMix[ch] * gain + (1.0 - alpha) * lpState[ch];
            double inSig = lpState[ch] + inject * (ch % 2 == 0 ? 1.0 : -1.0) * (1.0 / N);
            bufs[ch][(size_t)writePtr[ch]] = inSig;
            writePtr[ch] = (writePtr[ch] + 1) % (int)bufs[ch].size();
            sum += tmpMix[ch];
        }
        return sum / N;
    };

    // If maxRefCut was not supplied (or defaulted to -1) fall back to the old
    // behaviour where seeding stops at erCut.
    if (maxRefCut < 0) maxRefCut = erCut;
    // Never extend beyond the buffer we were given.
    maxRefCut = std::min(maxRefCut, (int)erIR.size());
    maxRefCut = std::min(maxRefCut, irLen);

    int seedRampLen = std::max(1, (int)std::floor(0.020 * sr));

    // Phase 1 — seed-only warmup (0 … erCut-1), output not captured.
    // This matches the original JS: the FDN is loaded up by the early-reflection
    // signal before it starts contributing to the IR.
    for (int i = 0; i < erCut; ++i)
    {
        double env = std::min(1.0, (double)i / (double)seedRampLen);
        fdnStep(erIR[(size_t)i] * env, i);
    }

    std::vector<double> out((size_t)irLen, 0.0);

    // Phase 2 — seed AND capture (erCut … maxRefCut-1).
    // The image-source signal continues to feed the FDN while its output is
    // simultaneously collected.  This accomplishes two things:
    //   • It adds a smooth, diffuse background to the sparse late reflections in
    //     eLL/eRL (filling the density gap that appears ~100 ms after the ER).
    //   • It loads far more energy into the FDN delay lines than the original
    //     85 ms warmup alone could provide.  Without this, the FDN level at 1 s
    //     is much quieter than the image-source field it must replace, causing
    //     the audible reverb cutoff at exactly maxRefDist.
    for (int i = erCut; i < maxRefCut; ++i)
        out[(size_t)i] = fdnStep(erIR[(size_t)i], i);

    // Phase 3 — free-running (maxRefCut … irLen-1).
    // Image sources are silent beyond maxRefDist; the FDN now carries the full
    // reverb tail on its own, decaying naturally at the room's RT60.
    for (int i = maxRefCut; i < irLen; ++i)
        out[(size_t)i] = fdnStep(0.0, i);

    if (diffusion > 0.02)
    {
        // The tail starts after the ER crossover, so it can be fully diffused
        // without blurring the initial discrete reflections.
        AllpassDiffuser diff = makeAllpassDiffuser(sr, diffusion);
        for (int i = 0; i < irLen; ++i)
            out[(size_t)i] = diff.process(out[(size_t)i]);
    }

    return out;
}

// Shape density factor — verbatim from JS
static double shapeDen (const std::string& shape)
{
    if (shape == "Rectangular") return 1.0;
    if (shape == "Fan / Shoebox") return 0.8;
    if (shape == "L-shaped") return 0.7;
    if (shape == "Cylindrical") return 1.2;
    if (shape == "Cathedral") return 0.5;
    if (shape == "Octagonal") return 0.9;
    return 1.0;
}

// ── synthIR — public entry point, parallel mic-path dispatcher ─────────────
// When no extras are enabled (outrig/ambient/direct all false — the default,
// matching all existing presets and factory IRs) this is a direct forward to
// synthMainPath with no threading at all, preserving bit-identity of the
// MAIN output (guarded by IR_14).
//
// When one or more extras are enabled the dispatcher runs MAIN and each
// enabled extra concurrently via std::async (launch::async policy).
// Synchronisation notes:
//   • IRSynthEngine's static material / vault / mic-pattern maps use lazy
//     `if (empty()) fill(); ` initialisation which is NOT thread-safe. We
//     warm them up on the calling thread here so all async threads only ever
//     *read* from the populated maps (which is safe for std::map).
//   • The progress callback is serialised behind cbMutex so hosts that assume
//     single-threaded GUI callbacks (the common case) don't see a race.
//   • Per-path progress is tracked in atomics; the reported progress is the
//     MIN across active paths (i.e. "we're waiting for the slowest path").
//   • MAIN is authoritative for res.rt60 / res.irLen / res.sampleRate /
//     res.success. Extras only populate res.direct / res.outrig / res.ambient.
IRSynthResult IRSynthEngine::synthIR (const IRSynthParams& p, IRSynthProgressFn cb)
{
    const bool anyExtra = p.outrig_enabled || p.ambient_enabled || p.direct_enabled;

    // Fast path: no extras → straight synchronous call. Guarantees bit-identity
    // with the pre-C5 behaviour for every existing session, preset and test.
    if (! anyExtra)
        return synthMainPath (p, cb);

    // Warm up static maps on this thread before fanning out — their lazy init
    // is not thread-safe. After this call the maps are fully populated and
    // concurrent reads from them are safe.
    (void) getMats();
    (void) getVP();
    (void) getMIC();

    std::atomic<double> mainProg { 0.0 }, outrigProg { 0.0 }, ambientProg { 0.0 };
    std::mutex cbMutex;

    auto reportAggregate = [&](const std::string& msg)
    {
        if (! cb) return;
        double minP = mainProg.load();
        if (p.outrig_enabled)  minP = std::min (minP, outrigProg.load());
        if (p.ambient_enabled) minP = std::min (minP, ambientProg.load());
        // DIRECT is fast enough that a dedicated progress channel is overkill.
        std::lock_guard<std::mutex> lk (cbMutex);
        cb (minP, msg);
    };

    auto mainCb = [&](double f, const std::string& m) { mainProg.store (f);    reportAggregate (m); };
    auto outrigCb = [&](double f, const std::string& m) { outrigProg.store (f);  reportAggregate (m); };
    auto ambientCb = [&](double f, const std::string& m) { ambientProg.store (f); reportAggregate (m); };

    auto mainFut = std::async (std::launch::async,
        [&]{ return synthMainPath (p, mainCb); });

    std::future<MicIRChannels> outrigFut, ambientFut, directFut;

    if (p.outrig_enabled)
        outrigFut = std::async (std::launch::async,
            [&]{ return synthExtraPath (p,
                                        p.outrig_lx, p.outrig_ly,
                                        p.outrig_rx, p.outrig_ry,
                                        p.outrig_height,
                                        p.outrig_langle, p.outrig_rangle,
                                        p.outrig_pattern,
                                        /*seedBase*/ 52,
                                        outrigCb,
                                        p.outrig_ltilt, p.outrig_rtilt); });

    if (p.ambient_enabled)
        ambientFut = std::async (std::launch::async,
            [&]{ return synthExtraPath (p,
                                        p.ambient_lx, p.ambient_ly,
                                        p.ambient_rx, p.ambient_ry,
                                        p.ambient_height,
                                        p.ambient_langle, p.ambient_rangle,
                                        p.ambient_pattern,
                                        /*seedBase*/ 62,
                                        ambientCb,
                                        p.ambient_ltilt, p.ambient_rtilt); });

    if (p.direct_enabled)
        directFut = std::async (std::launch::async,
            [&]{ return synthDirectPath (p); });

    // Collect results — sequential to guarantee a deterministic assignment
    // order for the returned IRSynthResult.
    IRSynthResult res = mainFut.get();
    if (outrigFut.valid())  res.outrig  = outrigFut.get();
    if (ambientFut.valid()) res.ambient = ambientFut.get();
    if (directFut.valid())  res.direct  = directFut.get();

    if (cb) cb (1.0, "Done.");
    return res;
}

// ── synthMainPath — verbatim from JS (MAIN mic pair) ──────────────────────
// This is the historical body of synthIR, unchanged. The feature/multi-mic-paths
// branch introduces sibling synthExtraPath / synthDirectPath helpers (C4) and a
// parallel dispatcher in synthIR (C5). Bit-identity of MAIN output is locked by
// IR_14 — do not rearrange floating-point expressions here.
IRSynthResult IRSynthEngine::synthMainPath (const IRSynthParams& p, IRSynthProgressFn cb)
{
    IRSynthResult res;
    res.sampleRate = p.sample_rate;
    int sr = p.sample_rate;

    auto report = [&](double frac, const std::string& msg) { if (cb) cb(frac, msg); };

    auto& vp = getVP();
    auto vpIt = vp.find(p.vault_type);
    if (vpIt == vp.end()) vpIt = vp.find("None (flat)");
    double hm = 1.0, vs = 0.0, vHfA = 0.0;
    if (vpIt != vp.end()) { hm = vpIt->second[0]; vs = vpIt->second[1]; vHfA = vpIt->second[2]; }
    double He = p.height * hm;

    std::vector<double> rt = calcRT60(p);
    double rm = rt[2];  // MF (500 Hz) RT60 — used for reflection order
    // irLen must be long enough for the slowest-decaying (usually LF) band.
    // Using only the MF RT60 truncates the LF tail mid-decay, giving a gated sound.
    // 8× RT60 gives a long natural decay before the end fade; peak normalisation keeps level safe.
    double rmMax = *std::max_element(rt.begin(), rt.end());
    bool eo = p.er_only;

    int irLen = (int)std::floor(std::max(0.3, std::min(8.0 * rmMax, 30.0)) * sr);
    int ec = (int)std::floor(0.085 * sr);

    double den = shapeDen(p.shape);

    // Image-source order: sized so that every source within the room's full
    // reverberant lifetime is visited.  Sources die away naturally through
    // cumulative material absorption — no artificial distance gate is applied.
    // Formula from the original JS source, capped at 60.
    const double minDim    = std::min({ p.width, p.depth, He });
    const double maxRefDist = 1e9;  // no gate — all sources within mo are used
    int mo = std::min(60, std::max(3, (int)std::floor(rm * SPEED / minDim / 2.0)));

    double ts = std::min(0.95, vs + p.organ_case * 0.35 + p.balconies * 0.25 + p.diffusion * 0.3);
    double oF = 1.0 - p.organ_case * 0.4;
    double bakedErGain = p.bake_er_tail_balance ? p.baked_er_gain : 1.0;
    double bakedTailGain = p.bake_er_tail_balance ? p.baked_tail_gain : 1.0;

    auto& mats = getMats();
    auto rc = [&](const std::string& m) -> std::array<double,8>
    {
        auto it = mats.find(m);
        if (it == mats.end()) it = mats.find("Painted plaster");
        std::array<double,8> r;
        for (int i = 0; i < N_BANDS; ++i) r[i] = std::sqrt(1.0 - it->second[i]);
        return r;
    };
    auto rF = rc(p.floor_material), rC = rc(p.ceiling_material), rW = rc(p.wall_material);
    if (p.window_fraction > 1e-9)
    {
        auto mwIt = mats.find(p.wall_material);
        auto mgIt = mats.find("Glass (large pane)");
        const auto& aw = mwIt != mats.end() ? mwIt->second : mats.find("Painted plaster")->second;
        const auto& ag = mgIt != mats.end() ? mgIt->second : aw;
        double wf = std::max(0.0, std::min(1.0, p.window_fraction));
        for (int i = 0; i < N_BANDS; ++i)
        {
            double a_blend = (1.0 - wf) * aw[i] + wf * ag[i];
            rW[i] = std::sqrt(1.0 - a_blend);
        }
    }

    report(0.05, "Computing image sources…");

    // Physical placement heights:
    //   sz  = source (speaker/instrument) at 1 m off the floor, clamped to 90 % of He.
    //   rz  = receiver (mic) at 3 m (Decca tree / outrigger height), clamped to 90 % of He.
    double sz = std::min(1.0, He * 0.9);
    double rz = std::min(3.0, He * 0.9);
    double slx = p.width * p.source_lx, sly = p.depth * p.source_ly;
    double srx = p.width * p.source_rx, sry = p.depth * p.source_ry;
    double rlx = p.width * p.receiver_lx, rly = p.depth * p.receiver_ly;
    double rrx = p.width * p.receiver_rx, rry = p.depth * p.receiver_ry;

    // ── Decca Tree capture mode (MAIN path) ──────────────────────────────────
    // When p.main_decca_enabled is true, override the L/R mic positions and
    // face angles with the tree-derived geometry and compute a third (centre)
    // mic path later in this function, combining all three into the standard
    // iLL/iRL/iLR/iRR 4-channel layout. Non-Decca mode leaves rlx..rry etc.
    // unchanged so IR_11 and IR_14 bit-identity are preserved.
    //
    // Classical tree defaults per Docs/deep-research-report.md §"Canonical geometry":
    //   a  = outer spacing  = 2.0 m
    //   b  = centre advance = 1.2 m
    //   h  = height         = 3.0 m  (matches rz above in typical rooms)
    //   gC = centre gain    = 1/√2 (-3 dB, constant-power)
    //   HP = centre HPF cut = 110 Hz (1-pole, avoids LF doubling)
    //   mic pattern         = M50-like (spherical pressure, narrows above 1 kHz)
    // These are kept file-static here so they are easy to re-tune engine-side
    // but impossible to fat-finger from a preset/sidecar.
    static constexpr double kDeccaOuterM   = 2.0;
    static constexpr double kDeccaAdvanceM = 1.2;
    static constexpr double kDeccaHeightM  = 3.0;
    static constexpr double kDeccaGC       = 0.70710678118654752;
    static constexpr double kDeccaHpHz     = 110.0;

    // Centre-mic position (Decca only; left uninitialised in normal mode).
    double rcx = 0.0, rcy = 0.0;
    // Mic face angles — non-Decca uses the configured micl/micr angles; Decca
    // mounts all three mics rigidly to the tree so they rotate together.
    double faceL = p.micl_angle;
    double faceR = p.micr_angle;
    double faceC = p.micl_angle;   // unused in non-Decca mode
    // Mic elevation tilt (radians) — same convention as faceL/faceR but in
    // the elevation plane. 0 = horizontal. Used as the faceElevation arg to
    // directivityCos (3D spherical-law-of-cosines mic directivity). In Decca
    // mode all three mics share one tilt (the rigid array rotates together).
    double tiltL = p.micl_tilt;
    double tiltR = p.micr_tilt;
    double tiltC = p.micl_tilt;    // unused in non-Decca mode
    std::string mainMicPattern = p.mic_pattern;

    if (p.main_decca_enabled)
    {
        const double cxC = p.width * p.decca_cx;
        const double cyC = p.depth * p.decca_cy;
        const double ux  = std::cos(p.decca_angle);
        const double uy  = std::sin(p.decca_angle);
        // Right-axis v is u rotated +90° CCW, so with the default
        // decca_angle = -π/2 (forward = -y) v points to +x, placing L at
        // lower-x and R at higher-x — matches the existing L/R convention
        // (receiver_lx < receiver_rx).
        const double vx = -uy;
        const double vy =  ux;

        rlx = cxC - (kDeccaOuterM * 0.5) * vx;
        rly = cyC - (kDeccaOuterM * 0.5) * vy;
        rrx = cxC + (kDeccaOuterM * 0.5) * vx;
        rry = cyC + (kDeccaOuterM * 0.5) * vy;
        rcx = cxC + kDeccaAdvanceM * ux;
        rcy = cyC + kDeccaAdvanceM * uy;

        // Toe-out: L and R outer mics rotate ±toe from the forward axis;
        // centre mic always looks straight forward. Default π/2 (±90°) is fully
        // side-firing; π/4 (±45°) matches the classic main pair; 0 collapses
        // the tree back to three coincident forward-facing mics. Clamped to
        // [0, π/2] so the outer mics never face more than 90° off-forward.
        const double toe = std::max(0.0, std::min((double) M_PI_2, p.decca_toe_out));
        faceL = p.decca_angle - toe;
        faceR = p.decca_angle + toe;
        faceC = p.decca_angle;
        // Decca tree is a rigid 3-mic array — all three mics share decca_tilt.
        tiltL = p.decca_tilt;
        tiltR = p.decca_tilt;
        tiltC = p.decca_tilt;
        // Decca uses the user-selected MAIN mic pattern (p.mic_pattern already
        // assigned into mainMicPattern above). The previous hardcoded override
        // to "cardioid (LDC)" was a diagnostic: M50-like is effectively omni
        // below 2 kHz, which collapses toe-out to no-op for most musical
        // content. With the pattern now user-selectable, the user can pick
        // cardioid (LDC) / wide cardioid (MK21) / figure-8 etc. for tight
        // imaging, or omni / omni (MK2H) for the classical spaced-omni Decca
        // feel.
        //
        // Override rz so the tree height matches the classical default even in
        // rooms where the existing rz clamp would otherwise trim it below 3 m.
        // In sufficiently tall rooms (He > 3.33 m) this is a no-op.
        rz = std::min(kDeccaHeightM, He * 0.9);
    }

    const double srcDist = std::sqrt((slx - srx) * (slx - srx) + (sly - sry) * (sly - sry));
    const double roomMin = std::min(p.width, p.depth);
    // Two tiers: "close" (break periodic delay with jitter only) vs "coincident" (soft FDN seed scale).
    // We do NOT use reflection spread — it smears the early response and causes the sound to collapse.
    const double closeThreshold = roomMin * 0.30;   // close: time jitter to break periodic delay
    const double coincidentThreshold = roomMin * 0.10;  // very close: gentle FDN seed scale only
    const bool close = (srcDist < closeThreshold);
    const bool coincident = (srcDist < coincidentThreshold);
    // When close: small jitter on direct/first-order (keep them tight), larger jitter on
    // order 2+ to break the periodic echo from repeated wall/floor/ceiling bounces.
    const double jitterOrder01Ms = close ? 0.25 : 0.0;
    const double jitterOrder2PlusMs = close ? 2.5 : 0.0;
    const double reflectionSpreadMs = 0.0;  // disabled — was causing collapse when close

    std::vector<Ref> rLL = calcRefs(rlx, rly, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 42, mainMicPattern, p.spkl_angle, faceL, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, tiltL);
    std::vector<Ref> rRL = calcRefs(rlx, rly, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 43, mainMicPattern, p.spkr_angle, faceL, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, tiltL);
    std::vector<Ref> rLR = calcRefs(rrx, rry, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 44, mainMicPattern, p.spkl_angle, faceR, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, tiltR);
    std::vector<Ref> rRR = calcRefs(rrx, rry, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 45, mainMicPattern, p.spkr_angle, faceR, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, tiltR);

    // Centre-mic rays (Decca only). Seed offsets 46/47 are unique across the
    // dispatcher (MAIN=42+, OUTRIG=52+, AMBIENT=62+, DIRECT=72+), so parallel
    // aux-path synthesis never collides with these.
    std::vector<Ref> rLC, rRC;
    if (p.main_decca_enabled)
    {
        rLC = calcRefs(rcx, rcy, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 46, mainMicPattern, p.spkl_angle, faceC, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, tiltC);
        rRC = calcRefs(rcx, rcy, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 47, mainMicPattern, p.spkr_angle, faceC, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, tiltC);
    }

    report(0.30, "Rendering " + std::to_string(rLL.size() + rRL.size() + rLR.size() + rRR.size() + rLC.size() + rRC.size()) + " reflections…");

    double diff = p.diffusion;
    // When "early reflections only" is on we normally use no diffusion (sharp ER).
    // If speakers are close, moderate diffusion helps break the periodic delay (jitter alone often isn't enough).
    double earlyDiff = eo ? 0.0 : diff;
    if (eo && close)
        earlyDiff = 0.50;
    // Frequency-dependent scatter: scales with diffusion; 0 when diffusion is off.
    // Scale is kept to ~one 4 kHz filter time-constant (≈0.4 ms) so the scatter
    // is perceptible but does not diffuse the high-frequency reverb into a noise floor.
    // At default settings (ts≈0.74), freqScatterMs ≈ 0.37 ms → ±18 samples at 4 kHz.
    const double freqScatterMs = ts * 0.5;  // Feature C — frequency-dependent scatter (0 = off)
    std::vector<double> eLL = renderCh(rLL, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> eRL = renderCh(rRL, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> eLR = renderCh(rLR, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> eRR = renderCh(rRR, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);

    // ── Decca Tree combine (additive) ────────────────────────────────────────
    // H_L_out = H_L_mic + gC·H_C_mic   (speaker L → centre mic contributes to L-out too)
    // H_R_out = H_R_mic + gC·H_C_mic
    //
    // In 4-channel form this means the centre mic contribution is mixed into
    // BOTH eLL/eLR (same speaker L path) and eRL/eRR (speaker R path). The
    // centre contribution is high-pass-filtered at 110 Hz first to avoid LF
    // doubling from the near-coincident LF omni response of the three mics
    // (see Docs/deep-research-report.md §"Centre channel HPF").
    if (p.main_decca_enabled)
    {
        std::vector<double> eLC = renderCh(rLC, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
        std::vector<double> eRC = renderCh(rRC, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);

        // 1-pole HPF on the centre-mic contributions only.
        // y[n] = α·(y[n-1] + x[n] - x[n-1]),  α = exp(-2π·fc/sr).
        auto hp1pole = [sr](std::vector<double>& v, double fcHz)
        {
            if (v.empty()) return;
            const double a = std::exp(-2.0 * M_PI * fcHz / (double)sr);
            double xPrev = 0.0, yPrev = 0.0;
            for (double& s : v)
            {
                double x = s;
                double y = a * (yPrev + x - xPrev);
                s = y;
                xPrev = x;
                yPrev = y;
            }
        };
        hp1pole(eLC, kDeccaHpHz);
        hp1pole(eRC, kDeccaHpHz);

        // Centre-fill gain: user-controllable via p.decca_centre_gain. Legacy
        // kDeccaGC (0.707) is the documented upper bound and is retained as
        // the constexpr for reference; 0.5 is the new default (see IRSynthParams).
        const double gC = std::max(0.0, std::min((double) kDeccaGC, p.decca_centre_gain));
        for (int i = 0; i < irLen; ++i)
        {
            const double lc = gC * eLC[(size_t)i];
            const double rc = gC * eRC[(size_t)i];
            eLL[(size_t)i] += lc;   // speaker L → output L: L-mic + gC·C-mic
            eRL[(size_t)i] += rc;   // speaker R → output L: L-mic + gC·C-mic (speaker R path)
            eLR[(size_t)i] += lc;   // speaker L → output R: R-mic + gC·C-mic
            eRR[(size_t)i] += rc;   // speaker R → output R: R-mic + gC·C-mic
        }
    }

    report(0.60, "Synthesising FDN reverb tail…");

    std::vector<double> iLL, iRL, iLR, iRR;
    if (!eo)
    {
        // Paired paths share FDN tail 50/50: LL+RL → left mic tail, LR+RR → right mic tail
        std::vector<double> eL(irLen), eR(irLen);
        for (int i = 0; i < irLen; ++i)
        {
            eL[(size_t)i] = eLL[(size_t)i] + eRL[(size_t)i];
            eR[(size_t)i] = eLR[(size_t)i] + eRR[(size_t)i];
        }

        // When sources are very close (coincident), eL/eR have nearly identical direct
        // paths so the FDN seed spike is 2x and recirculates as a repeat. Scale down
        // gently (0.8) so we don't collapse the tail; 0.5 was too aggressive.
        if (coincident)
        {
            for (int i = 0; i < irLen; ++i)
            {
                eL[(size_t)i] *= 0.8;
                eR[(size_t)i] *= 0.8;
            }
        }

        // Pre-diffuse the FDN seed to prevent coherent echoes when speakers are
        // coincident or close to one mic.  A sharp spike recirculates in the FDN
        // and re-emerges at the longest delay as a repeating ping.  Use three
        // stages: short (17 ms spread) -> long (35 ms spread) -> short again, so
        // the seed is smeared over 50+ ms and no single FDN line gets a clean repeat.
        {
            AllpassDiffuser dL = makeAllpassDiffuser(sr, 0.80);
            AllpassDiffuser dR = makeAllpassDiffuser(sr, 0.80);
            for (int i = 0; i < irLen; ++i)
            {
                eL[(size_t)i] = dL.process(eL[(size_t)i]);
                eR[(size_t)i] = dR.process(eR[(size_t)i]);
            }
            // Long allpass (31.7, 17.3, 11.2, 5.7 ms) spreads seed over ~35 ms.
            {
                AllpassDiffuser longL, longR;
                longL.g = longR.g = 0.62;
                for (int i = 0; i < 4; ++i)
                {
                    int d = (int)std::round(sr * (i == 0 ? 0.0317 : i == 1 ? 0.0173 : i == 2 ? 0.0112 : 0.0057));
                    longL.delays[i] = longR.delays[i] = d;
                    longL.bufs[i].resize((size_t)d, 0.0);
                    longR.bufs[i].resize((size_t)d, 0.0);
                    longL.ptrs[i] = longR.ptrs[i] = 0;
                }
                for (int i = 0; i < irLen; ++i)
                {
                    eL[(size_t)i] = longL.process(eL[(size_t)i]);
                    eR[(size_t)i] = longR.process(eR[(size_t)i]);
                }
            }
            dL = makeAllpassDiffuser(sr, 0.80);
            dR = makeAllpassDiffuser(sr, 0.80);
            for (int i = 0; i < irLen; ++i)
            {
                eL[(size_t)i] = dL.process(eL[(size_t)i]);
                eR[(size_t)i] = dR.process(eR[(size_t)i]);
            }
        }

        // Start FDN seeding at the first reflection arrival (t_first), not at
        // sample 0.  Before t_first the eL/eR signals are silent — seeding silence
        // into the FDN wastes the ec warmup window.  By starting at t_first we
        // guarantee the full ec (85 ms) is spent on real reverberant signal, so
        // the FDN delay lines are properly loaded before output begins.
        //
        // ecFdn = t_first + ec  (new warmup-end / output-start sample)
        //
        // Echo absorption: any spike at T in eL echoes at T + fdnMaxMs.  For this
        // to be absorbed inside Phase 2 (seed+output window) we need:
        //   fdnMaxRefCut > T_spike + fdnMaxMs
        //
        // The worst-case spike is the direct-path at t_first, so:
        //   fdnMaxRefCut > t_first + fdnMaxMs = (ecFdn - ec) + fdnMaxMs
        //
        // In practice we use (ecFdn + fdnMaxMs) × 1.1, which always satisfies
        // the constraint regardless of speaker–mic distance.

        // longest FDN delay line (same formula as inside renderFDNTail)
        const double fdnVol  = p.width * p.depth * He;
        const double fdnSurf = 2.0 * (p.width * p.depth + p.depth * He + p.width * He);
        const double fdnMfp  = 4.0 * fdnVol / fdnSurf;
        const double fdnMaxMs = std::max(130.0, fdnMfp / SPEED * 1000.0 * 3.0);

        auto dist3d = [](double ax, double ay, double az,
                         double bx, double by, double bz) -> double {
            double dx = ax-bx, dy = ay-by, dz = az-bz;
            return std::sqrt(dx*dx + dy*dy + dz*dz);
        };
        // t_first: first sample where eL/eR carry real signal (min direct-path).
        // Subtract one sample as a small safety margin against flooring errors.
        // In Decca mode the centre mic (1.2 m forward of L/R) typically has a
        // shorter direct path than the outer mics, so include it in the min.
        double minDirectDistM = std::min({
            dist3d(rlx, rly, rz, slx, sly, sz),
            dist3d(rlx, rly, rz, srx, sry, sz),
            dist3d(rrx, rry, rz, slx, sly, sz),
            dist3d(rrx, rry, rz, srx, sry, sz)
        });
        if (p.main_decca_enabled)
        {
            minDirectDistM = std::min(minDirectDistM, std::min(
                dist3d(rcx, rcy, rz, slx, sly, sz),
                dist3d(rcx, rcy, rz, srx, sry, sz)));
        }
        const int t_first      = std::max(0, (int)std::floor(minDirectDistM / SPEED * sr) - 1);
        const int ecFdn        = std::min(irLen, t_first + ec);
        const int fdnMaxRefCut = std::min(irLen,
            (int)std::ceil((ecFdn + fdnMaxMs * sr / 1000.0) * 1.1));

        std::vector<double> tL = renderFDNTail(rt, irLen, ecFdn, eL, diff, sr, 100, p.width, p.depth, He, fdnMaxRefCut);
        std::vector<double> tR = renderFDNTail(rt, irLen, ecFdn, eR, diff, sr, 101, p.width, p.depth, He, fdnMaxRefCut);

        iLL.resize((size_t)irLen);
        iRL.resize((size_t)irLen);
        iLR.resize((size_t)irLen);
        iRR.resize((size_t)irLen);
        int xfade = (int)std::floor(0.020 * sr);

        // ── FDN level-match + ef residual floor ──────────────────────────────
        // Problem: the ER boundary fix (ef=0 after ecFdn) removed late-ISM energy
        // that was previously masking the FDN's low starting level.  The FDN IS
        // seeded by the full ER, but the three-stage pre-diffusion cascade spreads
        // its energy over time and the delay lines haven't all completed a full cycle
        // by ecFdn.  Fix: two complementary techniques applied together.
        //
        // A — ef residual floor (erFloor): instead of ef → 0, decay to a small
        //     constant so the already-fully-diffuse late-ISM contributes a natural
        //     undertone.  eLL/eRL themselves decay at the room RT60, so no extra
        //     envelope is needed — the contribution decays automatically.
        //
        // B — dynamic FDN gain (fdnGainL/R): windowed-RMS comparison at the
        //     crossfade boundary produces a per-channel multiplier so that
        //     tailL fills the (1 − erFloor) portion of the ER level, ensuring
        //     iLL+iRL is continuous at ecFdn.
        //
        // Math: target at ecFdn  →  (eLL+eRL)·bakedErGain·erFloor  +  tL·bakedTailGain·fdnGainL
        //                        =  (eLL+eRL)·bakedErGain
        //     ∴  fdnGainL = RMS(eLL+eRL, ref)·bakedErGain·(1−erFloor) / (RMS(tL, fdn)·bakedTailGain)
        const double erFloor  = 0.05;   // 5 % ER residual amplitude — fully diffuse past ecFdn
        const double kMaxGain = 16.0;           // hard cap: +24 dB
        const double kMinGain = 1.0 / kMaxGain; // floor: −24 dB — allows tail attenuation to match ER at distance
        const double kMinRms  = 1e-7;           // silence guard

        auto wRMS = [&](const std::vector<double>& v, int a, int b) -> double {
            a = std::max(a, 0); b = std::min(b, irLen);
            if (b <= a) return 0.0;
            double s = 0.0;
            for (int i = a; i < b; ++i) s += v[(size_t)i] * v[(size_t)i];
            return std::sqrt(s / (b - a));
        };

        double fdnGainL = 1.0, fdnGainR = 1.0;
        {
            // ER reference window: [ecFdn-xfade, ecFdn] — the last 20 ms before the
            // crossfade starts, where ef=1.  The previous window ([ecFdn-2·xfade,
            // ecFdn-xfade] ≈ [55–75 ms]) straddled the sparse-ISM / allpass-onset
            // boundary, giving near-zero windowed RMS. This window sits further into
            // the allpass diffusion zone (renderCh is fully diffused past 85 ms;
            // ecFdn-xfade ≈ 79–95 ms for typical placements) and is therefore denser.
            const int rA = ecFdn - xfade, rB = ecFdn;
            int a = std::max(rA, 0), b = std::min(rB, irLen);
            double sL = 0.0, sR = 0.0;
            int n = std::max(b - a, 1);
            for (int i = a; i < b; ++i)
            {
                double l = eLL[(size_t)i] + eRL[(size_t)i];
                double r = eLR[(size_t)i] + eRR[(size_t)i];
                sL += l * l; sR += r * r;
            }
            double erRmsL = std::sqrt(sL / n) * bakedErGain;
            double erRmsR = std::sqrt(sR / n) * bakedErGain;

            // FDN measurement window: [ecFdn+fdnMaxMs, ecFdn+fdnMaxMs+2·xfade].
            // The previous window ([ecFdn, ecFdn+xfade]) captured the FDN's initial
            // energy burst (16 delay lines releasing 99 ms of accumulated warmup), which
            // can be much larger than the sparse ER reference, giving fdnGain < 1 and
            // silencing the tail.  By waiting until the longest delay line has completed
            // its first full recirculation cycle (fdnMaxMs ≈ 130 ms), the FDN is at a
            // quasi-steady-state level that correctly represents the ongoing tail.
            //
            // The 0.5 blend factor is included in the denominator because the actual
            // tail contribution is   tL * 0.5 * bakedTailGain * fdnGainL.
            // Omitting it made fdnGain 2× too small in the previous version.
            //
            // fdnGain is clamped to [kMinGain, kMaxGain] (±24 dB): the tail can be
            // attenuated as well as boosted so that ER/tail balance stays consistent
            // as speaker-to-mic distance changes.  kMinRms silence guard keeps the
            // fallback at 1.0 if the FDN measurement window is silent.
            const int fdnMaxSamp = (int)std::ceil(fdnMaxMs * sr / 1000.0);
            double fdnRmsL = wRMS(tL, ecFdn + fdnMaxSamp, ecFdn + fdnMaxSamp + 2 * xfade) * 0.5 * bakedTailGain;
            double fdnRmsR = wRMS(tR, ecFdn + fdnMaxSamp, ecFdn + fdnMaxSamp + 2 * xfade) * 0.5 * bakedTailGain;

            fdnGainL = (fdnRmsL > kMinRms)
                ? std::min(std::max(erRmsL * (1.0 - erFloor) / fdnRmsL, kMinGain), kMaxGain) : 1.0;
            fdnGainR = (fdnRmsR > kMinRms)
                ? std::min(std::max(erRmsR * (1.0 - erFloor) / fdnRmsR, kMinGain), kMaxGain) : 1.0;
        }
        // ─────────────────────────────────────────────────────────────────────

        for (int i = 0; i < irLen; ++i)
        {
            // FDN fade-in: 0→1 linear over [ecFdn-xfade, ecFdn] (unchanged).
            // For centred speakers ecFdn ≈ ec so behaviour is unchanged.
            // For far speakers ecFdn is later; tL/tR are zero before ecFdn anyway.
            double tf = (i < ecFdn - xfade) ? 0.0
                      : (i < ecFdn)         ? (double)(i - (ecFdn - xfade)) / xfade
                      : 1.0;

            // ER fade-out: 1→erFloor cosine over [ecFdn-xfade, ecFdn], then constant
            // erFloor thereafter.  Fading to erFloor (not 0) avoids a hard silence just
            // before the FDN onset; the residual decays naturally at the room RT60
            // because eLL/eRL are already decaying image-source signals.
            double ef;
            if      (i < ecFdn - xfade) ef = 1.0;
            else if (i < ecFdn)         ef = erFloor + (1.0 - erFloor) * 0.5 * (1.0 + std::cos(M_PI * (double)(i - (ecFdn - xfade)) / xfade));
            else                        ef = erFloor;

            double tailL = tL[(size_t)i] * tf * 0.5 * bakedTailGain * fdnGainL;
            double tailR = tR[(size_t)i] * tf * 0.5 * bakedTailGain * fdnGainR;
            iLL[(size_t)i] = eLL[(size_t)i] * bakedErGain * ef + tailL;
            iRL[(size_t)i] = eRL[(size_t)i] * bakedErGain * ef + tailL;
            iLR[(size_t)i] = eLR[(size_t)i] * bakedErGain * ef + tailR;
            iRR[(size_t)i] = eRR[(size_t)i] * bakedErGain * ef + tailR;
        }
    }
    else
    {
        iLL.resize((size_t)irLen);
        iRL.resize((size_t)irLen);
        iLR.resize((size_t)irLen);
        iRR.resize((size_t)irLen);
        // Gentle 10 ms cosine taper approaching ec: avoids an abrupt silence
        // boundary and prevents any sharp ISM spike right at the cut point from
        // aliasing through the convolution engine.
        const int erTaperLen   = (int)std::round(0.010 * sr);
        const int erTaperStart = ec - erTaperLen;
        for (int i = 0; i < irLen; ++i)
        {
            double erFade;
            if      (i >= ec)           erFade = 0.0;
            else if (i >= erTaperStart) erFade = 0.5 * (1.0 + std::cos(M_PI * (double)(i - erTaperStart) / erTaperLen));
            else                        erFade = 1.0;

            iLL[(size_t)i] = eLL[(size_t)i] * bakedErGain * erFade;
            iRL[(size_t)i] = eRL[(size_t)i] * bakedErGain * erFade;
            iLR[(size_t)i] = eLR[(size_t)i] * bakedErGain * erFade;
            iRR[(size_t)i] = eRR[(size_t)i] * bakedErGain * erFade;
        }
    }

    // Feature B — modal resonance boost (axial room modes 10–250 Hz)
    {
        const double modalGain = 0.18;
        iLL = applyModalBank(iLL, p.width, p.depth, He, rt[0], modalGain, sr);
        iRL = applyModalBank(iRL, p.width, p.depth, He, rt[0], modalGain, sr);
        iLR = applyModalBank(iLR, p.width, p.depth, He, rt[0], modalGain, sr);
        iRR = applyModalBank(iRR, p.width, p.depth, He, rt[0], modalGain, sr);
    }

    report(0.85, "Finishing…");

    iLL = hpF(lpF(iLL, 18000.0, sr), 20.0, sr);
    iRL = hpF(lpF(iRL, 18000.0, sr), 20.0, sr);
    iLR = hpF(lpF(iLR, 18000.0, sr), 20.0, sr);
    iRR = hpF(lpF(iRR, 18000.0, sr), 20.0, sr);

    // Cosine fade-out over the last 500 ms so the tail eases to silence
    // without a noticeable abrupt end (longer fade = smoother transition).
    {
        const int endFade = (int)std::round(0.500 * sr);
        const int fadeStart = std::max(0, irLen - endFade);
        for (auto* v : { &iLL, &iRL, &iLR, &iRR })
            for (int i = fadeStart; i < irLen; ++i)
            {
                double t = (double)(i - fadeStart) / (double)endFade;
                (*v)[(size_t)i] *= 0.5 * (1.0 + std::cos(M_PI * t));
            }
    }

    // Peak normalisation intentionally removed.
    // IR amplitude now scales naturally with speaker-to-mic distance (1/r law),
    // preserving the proximity effect: speakers placed close to mics produce a
    // louder wet signal; speakers placed far away produce a quieter one.
    // Clipping only occurs below ~85 cm (cardioid default), which is an
    // implausible placement in any real room.  The host wet-level control
    // gives the user full range to compensate for overall level.

    // Output level trim: +15 dB applied to all four channels at the very end,
    // after all processing is complete.  This corrects for the observed level
    // shortfall without touching any of the synthesis calculations.
    {
        const double gain15dB = std::pow(10.0, 15.0 / 20.0); // ≈ 5.6234
        for (auto* v : { &iLL, &iRL, &iLR, &iRR })
            for (double& s : *v)
                s *= gain15dB;
    }

    res.iLL = std::move(iLL);
    res.iRL = std::move(iRL);
    res.iLR = std::move(iLR);
    res.iRR = std::move(iRR);
    res.rt60 = rt;
    res.irLen = irLen;
    res.success = true;
    report(1.0, "Done.");
    return res;
}

// ── synthExtraPath — OUTRIG / AMBIENT ─────────────────────────────────────
// Near-verbatim duplicate of synthMainPath with the MAIN-specific identifiers
// parameterised (receiver positions, receiver height, mic pattern, mic face
// angles, seed base). Kept as an explicit duplicate so the MAIN body is frozen
// for IR_14 bit-identity — do not factor out the shared body until a future
// commit updates the golden digests.
MicIRChannels IRSynthEngine::synthExtraPath (const IRSynthParams& p,
                                             double rlxNorm, double rlyNorm,
                                             double rrxNorm, double rryNorm,
                                             double rzMetres,
                                             double langle, double rangle,
                                             const std::string& pattern,
                                             uint32_t seedBase,
                                             IRSynthProgressFn cb,
                                             double ltilt,
                                             double rtilt)
{
    MicIRChannels out;
    int sr = p.sample_rate;

    auto report = [&](double frac, const std::string& msg) { if (cb) cb(frac, msg); };

    auto& vp = getVP();
    auto vpIt = vp.find(p.vault_type);
    if (vpIt == vp.end()) vpIt = vp.find("None (flat)");
    double hm = 1.0, vs = 0.0, vHfA = 0.0;
    if (vpIt != vp.end()) { hm = vpIt->second[0]; vs = vpIt->second[1]; vHfA = vpIt->second[2]; }
    double He = p.height * hm;

    std::vector<double> rt = calcRT60(p);
    double rm = rt[2];
    double rmMax = *std::max_element(rt.begin(), rt.end());
    bool eo = p.er_only;

    int irLen = (int)std::floor(std::max(0.3, std::min(8.0 * rmMax, 30.0)) * sr);
    int ec = (int)std::floor(0.085 * sr);

    double den = shapeDen(p.shape);

    const double minDim    = std::min({ p.width, p.depth, He });
    const double maxRefDist = 1e9;
    int mo = std::min(60, std::max(3, (int)std::floor(rm * SPEED / minDim / 2.0)));

    double ts = std::min(0.95, vs + p.organ_case * 0.35 + p.balconies * 0.25 + p.diffusion * 0.3);
    double oF = 1.0 - p.organ_case * 0.4;
    double bakedErGain = p.bake_er_tail_balance ? p.baked_er_gain : 1.0;
    double bakedTailGain = p.bake_er_tail_balance ? p.baked_tail_gain : 1.0;

    auto& mats = getMats();
    auto rc = [&](const std::string& m) -> std::array<double,8>
    {
        auto it = mats.find(m);
        if (it == mats.end()) it = mats.find("Painted plaster");
        std::array<double,8> r;
        for (int i = 0; i < N_BANDS; ++i) r[i] = std::sqrt(1.0 - it->second[i]);
        return r;
    };
    auto rF = rc(p.floor_material), rC = rc(p.ceiling_material), rW = rc(p.wall_material);
    if (p.window_fraction > 1e-9)
    {
        auto mwIt = mats.find(p.wall_material);
        auto mgIt = mats.find("Glass (large pane)");
        const auto& aw = mwIt != mats.end() ? mwIt->second : mats.find("Painted plaster")->second;
        const auto& ag = mgIt != mats.end() ? mgIt->second : aw;
        double wf = std::max(0.0, std::min(1.0, p.window_fraction));
        for (int i = 0; i < N_BANDS; ++i)
        {
            double a_blend = (1.0 - wf) * aw[i] + wf * ag[i];
            rW[i] = std::sqrt(1.0 - a_blend);
        }
    }

    report(0.05, "Computing image sources…");

    // Speaker height same as MAIN (1 m); mic height from caller (OUTRIG: 3 m, AMBIENT: 6 m),
    // clamped to 90 % of He so low-ceiling rooms don't place mics through the ceiling.
    double sz = std::min(1.0, He * 0.9);
    double rz = std::min(rzMetres, He * 0.9);
    double slx = p.width * p.source_lx, sly = p.depth * p.source_ly;
    double srx = p.width * p.source_rx, sry = p.depth * p.source_ry;
    double rlx = p.width * rlxNorm, rly = p.depth * rlyNorm;
    double rrx = p.width * rrxNorm, rry = p.depth * rryNorm;

    const double srcDist = std::sqrt((slx - srx) * (slx - srx) + (sly - sry) * (sly - sry));
    const double roomMin = std::min(p.width, p.depth);
    const double closeThreshold = roomMin * 0.30;
    const double coincidentThreshold = roomMin * 0.10;
    const bool close = (srcDist < closeThreshold);
    const bool coincident = (srcDist < coincidentThreshold);
    const double jitterOrder01Ms = close ? 0.25 : 0.0;
    const double jitterOrder2PlusMs = close ? 2.5 : 0.0;
    const double reflectionSpreadMs = 0.0;

    std::vector<Ref> rLL = calcRefs(rlx, rly, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, seedBase + 0, pattern, p.spkl_angle, langle, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, ltilt);
    std::vector<Ref> rRL = calcRefs(rlx, rly, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, seedBase + 1, pattern, p.spkr_angle, langle, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, ltilt);
    std::vector<Ref> rLR = calcRefs(rrx, rry, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, seedBase + 2, pattern, p.spkl_angle, rangle, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, rtilt);
    std::vector<Ref> rRR = calcRefs(rrx, rry, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, seedBase + 3, pattern, p.spkr_angle, rangle, maxRefDist, jitterOrder01Ms, jitterOrder2PlusMs, rtilt);

    report(0.30, "Rendering " + std::to_string(rLL.size() + rRL.size() + rLR.size() + rRR.size()) + " reflections…");

    double diff = p.diffusion;
    double earlyDiff = eo ? 0.0 : diff;
    if (eo && close)
        earlyDiff = 0.50;
    const double freqScatterMs = ts * 0.5;
    std::vector<double> eLL = renderCh(rLL, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> eRL = renderCh(rRL, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> eLR = renderCh(rLR, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> eRR = renderCh(rRR, irLen, den, sr, earlyDiff, reflectionSpreadMs, freqScatterMs);

    report(0.60, "Synthesising FDN reverb tail…");

    std::vector<double> iLL, iRL, iLR, iRR;
    if (!eo)
    {
        std::vector<double> eL(irLen), eR(irLen);
        for (int i = 0; i < irLen; ++i)
        {
            eL[(size_t)i] = eLL[(size_t)i] + eRL[(size_t)i];
            eR[(size_t)i] = eLR[(size_t)i] + eRR[(size_t)i];
        }

        if (coincident)
        {
            for (int i = 0; i < irLen; ++i)
            {
                eL[(size_t)i] *= 0.8;
                eR[(size_t)i] *= 0.8;
            }
        }

        {
            AllpassDiffuser dL = makeAllpassDiffuser(sr, 0.80);
            AllpassDiffuser dR = makeAllpassDiffuser(sr, 0.80);
            for (int i = 0; i < irLen; ++i)
            {
                eL[(size_t)i] = dL.process(eL[(size_t)i]);
                eR[(size_t)i] = dR.process(eR[(size_t)i]);
            }
            {
                AllpassDiffuser longL, longR;
                longL.g = longR.g = 0.62;
                for (int i = 0; i < 4; ++i)
                {
                    int d = (int)std::round(sr * (i == 0 ? 0.0317 : i == 1 ? 0.0173 : i == 2 ? 0.0112 : 0.0057));
                    longL.delays[i] = longR.delays[i] = d;
                    longL.bufs[i].resize((size_t)d, 0.0);
                    longR.bufs[i].resize((size_t)d, 0.0);
                    longL.ptrs[i] = longR.ptrs[i] = 0;
                }
                for (int i = 0; i < irLen; ++i)
                {
                    eL[(size_t)i] = longL.process(eL[(size_t)i]);
                    eR[(size_t)i] = longR.process(eR[(size_t)i]);
                }
            }
            dL = makeAllpassDiffuser(sr, 0.80);
            dR = makeAllpassDiffuser(sr, 0.80);
            for (int i = 0; i < irLen; ++i)
            {
                eL[(size_t)i] = dL.process(eL[(size_t)i]);
                eR[(size_t)i] = dR.process(eR[(size_t)i]);
            }
        }

        const double fdnVol  = p.width * p.depth * He;
        const double fdnSurf = 2.0 * (p.width * p.depth + p.depth * He + p.width * He);
        const double fdnMfp  = 4.0 * fdnVol / fdnSurf;
        const double fdnMaxMs = std::max(130.0, fdnMfp / SPEED * 1000.0 * 3.0);

        auto dist3d = [](double ax, double ay, double az,
                         double bx, double by, double bz) -> double {
            double dx = ax-bx, dy = ay-by, dz = az-bz;
            return std::sqrt(dx*dx + dy*dy + dz*dz);
        };
        const double minDirectDistM = std::min({
            dist3d(rlx, rly, rz, slx, sly, sz),
            dist3d(rlx, rly, rz, srx, sry, sz),
            dist3d(rrx, rry, rz, slx, sly, sz),
            dist3d(rrx, rry, rz, srx, sry, sz)
        });
        const int t_first      = std::max(0, (int)std::floor(minDirectDistM / SPEED * sr) - 1);
        const int ecFdn        = std::min(irLen, t_first + ec);
        const int fdnMaxRefCut = std::min(irLen,
            (int)std::ceil((ecFdn + fdnMaxMs * sr / 1000.0) * 1.1));

        // FDN seed derived from seedBase so OUTRIG (52 → 110/111) and AMBIENT (62 → 120/121)
        // have distinct diffuse fields from MAIN (100/101) and from each other.
        std::vector<double> tL = renderFDNTail(rt, irLen, ecFdn, eL, diff, sr, seedBase + 58, p.width, p.depth, He, fdnMaxRefCut);
        std::vector<double> tR = renderFDNTail(rt, irLen, ecFdn, eR, diff, sr, seedBase + 59, p.width, p.depth, He, fdnMaxRefCut);

        iLL.resize((size_t)irLen);
        iRL.resize((size_t)irLen);
        iLR.resize((size_t)irLen);
        iRR.resize((size_t)irLen);
        int xfade = (int)std::floor(0.020 * sr);

        const double erFloor  = 0.05;
        const double kMaxGain = 16.0;
        const double kMinGain = 1.0 / kMaxGain;
        const double kMinRms  = 1e-7;

        auto wRMS = [&](const std::vector<double>& v, int a, int b) -> double {
            a = std::max(a, 0); b = std::min(b, irLen);
            if (b <= a) return 0.0;
            double s = 0.0;
            for (int i = a; i < b; ++i) s += v[(size_t)i] * v[(size_t)i];
            return std::sqrt(s / (b - a));
        };

        double fdnGainL = 1.0, fdnGainR = 1.0;
        {
            const int rA = ecFdn - xfade, rB = ecFdn;
            int a = std::max(rA, 0), b = std::min(rB, irLen);
            double sL = 0.0, sR = 0.0;
            int n = std::max(b - a, 1);
            for (int i = a; i < b; ++i)
            {
                double l = eLL[(size_t)i] + eRL[(size_t)i];
                double r = eLR[(size_t)i] + eRR[(size_t)i];
                sL += l * l; sR += r * r;
            }
            double erRmsL = std::sqrt(sL / n) * bakedErGain;
            double erRmsR = std::sqrt(sR / n) * bakedErGain;

            const int fdnMaxSamp = (int)std::ceil(fdnMaxMs * sr / 1000.0);
            double fdnRmsL = wRMS(tL, ecFdn + fdnMaxSamp, ecFdn + fdnMaxSamp + 2 * xfade) * 0.5 * bakedTailGain;
            double fdnRmsR = wRMS(tR, ecFdn + fdnMaxSamp, ecFdn + fdnMaxSamp + 2 * xfade) * 0.5 * bakedTailGain;

            fdnGainL = (fdnRmsL > kMinRms)
                ? std::min(std::max(erRmsL * (1.0 - erFloor) / fdnRmsL, kMinGain), kMaxGain) : 1.0;
            fdnGainR = (fdnRmsR > kMinRms)
                ? std::min(std::max(erRmsR * (1.0 - erFloor) / fdnRmsR, kMinGain), kMaxGain) : 1.0;
        }

        for (int i = 0; i < irLen; ++i)
        {
            double tf = (i < ecFdn - xfade) ? 0.0
                      : (i < ecFdn)         ? (double)(i - (ecFdn - xfade)) / xfade
                      : 1.0;

            double ef;
            if      (i < ecFdn - xfade) ef = 1.0;
            else if (i < ecFdn)         ef = erFloor + (1.0 - erFloor) * 0.5 * (1.0 + std::cos(M_PI * (double)(i - (ecFdn - xfade)) / xfade));
            else                        ef = erFloor;

            double tailL = tL[(size_t)i] * tf * 0.5 * bakedTailGain * fdnGainL;
            double tailR = tR[(size_t)i] * tf * 0.5 * bakedTailGain * fdnGainR;
            iLL[(size_t)i] = eLL[(size_t)i] * bakedErGain * ef + tailL;
            iRL[(size_t)i] = eRL[(size_t)i] * bakedErGain * ef + tailL;
            iLR[(size_t)i] = eLR[(size_t)i] * bakedErGain * ef + tailR;
            iRR[(size_t)i] = eRR[(size_t)i] * bakedErGain * ef + tailR;
        }
    }
    else
    {
        iLL.resize((size_t)irLen);
        iRL.resize((size_t)irLen);
        iLR.resize((size_t)irLen);
        iRR.resize((size_t)irLen);
        const int erTaperLen   = (int)std::round(0.010 * sr);
        const int erTaperStart = ec - erTaperLen;
        for (int i = 0; i < irLen; ++i)
        {
            double erFade;
            if      (i >= ec)           erFade = 0.0;
            else if (i >= erTaperStart) erFade = 0.5 * (1.0 + std::cos(M_PI * (double)(i - erTaperStart) / erTaperLen));
            else                        erFade = 1.0;

            iLL[(size_t)i] = eLL[(size_t)i] * bakedErGain * erFade;
            iRL[(size_t)i] = eRL[(size_t)i] * bakedErGain * erFade;
            iLR[(size_t)i] = eLR[(size_t)i] * bakedErGain * erFade;
            iRR[(size_t)i] = eRR[(size_t)i] * bakedErGain * erFade;
        }
    }

    {
        const double modalGain = 0.18;
        iLL = applyModalBank(iLL, p.width, p.depth, He, rt[0], modalGain, sr);
        iRL = applyModalBank(iRL, p.width, p.depth, He, rt[0], modalGain, sr);
        iLR = applyModalBank(iLR, p.width, p.depth, He, rt[0], modalGain, sr);
        iRR = applyModalBank(iRR, p.width, p.depth, He, rt[0], modalGain, sr);
    }

    report(0.85, "Finishing…");

    iLL = hpF(lpF(iLL, 18000.0, sr), 20.0, sr);
    iRL = hpF(lpF(iRL, 18000.0, sr), 20.0, sr);
    iLR = hpF(lpF(iLR, 18000.0, sr), 20.0, sr);
    iRR = hpF(lpF(iRR, 18000.0, sr), 20.0, sr);

    {
        const int endFade = (int)std::round(0.500 * sr);
        const int fadeStart = std::max(0, irLen - endFade);
        for (auto* v : { &iLL, &iRL, &iLR, &iRR })
            for (int i = fadeStart; i < irLen; ++i)
            {
                double t = (double)(i - fadeStart) / (double)endFade;
                (*v)[(size_t)i] *= 0.5 * (1.0 + std::cos(M_PI * t));
            }
    }

    {
        const double gain15dB = std::pow(10.0, 15.0 / 20.0);
        for (auto* v : { &iLL, &iRL, &iLR, &iRR })
            for (double& s : *v)
                s *= gain15dB;
    }

    out.LL = std::move(iLL);
    out.RL = std::move(iRL);
    out.LR = std::move(iLR);
    out.RR = std::move(iRR);
    out.irLen = irLen;
    out.synthesised = true;
    report(1.0, "Done.");
    return out;
}

// ── synthDirectPath — order-0 only, shares MAIN mic pattern + angles ──────
// Synthesises a very short IR containing only the direct arrivals (one ray
// per speaker-mic pair). No reflections, no diffusion, no FDN tail, no modal
// bank, no 500 ms end fade — just the 8-band bandpass-filter impulse-response
// tail plus a +15 dB output trim to match MAIN's level convention.
//
// D2: uses p.mic_pattern and p.micl_angle / p.micr_angle (inherits from MAIN).
// er_only is implicitly honoured — order-0 arrivals are always within ec.
MicIRChannels IRSynthEngine::synthDirectPath (const IRSynthParams& p)
{
    MicIRChannels out;
    int sr = p.sample_rate;

    auto& vp = getVP();
    auto vpIt = vp.find(p.vault_type);
    if (vpIt == vp.end()) vpIt = vp.find("None (flat)");
    double hm = 1.0;
    if (vpIt != vp.end()) hm = vpIt->second[0];
    double He = p.height * hm;

    // Speaker/mic heights identical to MAIN path for consistent direct-path timing.
    double sz = std::min(1.0, He * 0.9);
    double rz = std::min(3.0, He * 0.9);
    double slx = p.width * p.source_lx, sly = p.depth * p.source_ly;
    double srx = p.width * p.source_rx, sry = p.depth * p.source_ry;
    double rlx = p.width * p.receiver_lx, rly = p.depth * p.receiver_ly;
    double rrx = p.width * p.receiver_rx, rry = p.depth * p.receiver_ry;

    // Decca geometry (must match synthMainPath exactly so DIRECT stays
    // acoustically aligned with MAIN).  Defaults kept file-static locally
    // rather than shared with synthMainPath because PING_TESTING_BUILD
    // produces two independent TUs.
    static constexpr double kDeccaOuterM   = 2.0;
    static constexpr double kDeccaAdvanceM = 1.2;
    static constexpr double kDeccaHeightM  = 3.0;
    static constexpr double kDeccaGC       = 0.70710678118654752;
    static constexpr double kDeccaHpHz     = 110.0;

    double rcx = 0.0, rcy = 0.0;
    double faceL = p.micl_angle;
    double faceR = p.micr_angle;
    double faceC = p.micl_angle;
    // Mic elevation tilt (radians) — see synthMainPath for the convention.
    // DIRECT shares MAIN's mic angles + tilt so the direct-arrival cue stays
    // acoustically aligned with the MAIN path.
    double tiltL = p.micl_tilt;
    double tiltR = p.micr_tilt;
    double tiltC = p.micl_tilt;
    std::string mainMicPattern = p.mic_pattern;

    if (p.main_decca_enabled)
    {
        const double cxC = p.width * p.decca_cx;
        const double cyC = p.depth * p.decca_cy;
        const double ux  = std::cos(p.decca_angle);
        const double uy  = std::sin(p.decca_angle);
        const double vx = -uy;
        const double vy =  ux;
        rlx = cxC - (kDeccaOuterM * 0.5) * vx;
        rly = cyC - (kDeccaOuterM * 0.5) * vy;
        rrx = cxC + (kDeccaOuterM * 0.5) * vx;
        rry = cyC + (kDeccaOuterM * 0.5) * vy;
        rcx = cxC + kDeccaAdvanceM * ux;
        rcy = cyC + kDeccaAdvanceM * uy;
        // Toe-out: L and R outer mics rotate ±toe from the forward axis;
        // centre mic always looks straight forward. Default π/2 (±90°) is fully
        // side-firing; π/4 (±45°) matches the classic main pair; 0 collapses
        // the tree back to three coincident forward-facing mics. Clamped to
        // [0, π/2] so the outer mics never face more than 90° off-forward.
        const double toe = std::max(0.0, std::min((double) M_PI_2, p.decca_toe_out));
        faceL = p.decca_angle - toe;
        faceR = p.decca_angle + toe;
        faceC = p.decca_angle;
        // Decca tree is a rigid 3-mic array — all three mics share decca_tilt.
        tiltL = p.decca_tilt;
        tiltR = p.decca_tilt;
        tiltC = p.decca_tilt;
        // DIRECT uses the same user-selected MAIN mic pattern as the main
        // path — mainMicPattern = p.mic_pattern above is already correct.
        // Keep the rz override in sync with synthMainPath so the tree height
        // matches the classical 3 m default in sufficiently tall rooms.
        rz = std::min(kDeccaHeightM, He * 0.9);
    }

    auto dist3d = [](double ax, double ay, double az,
                     double bx, double by, double bz) -> double {
        double dx = ax-bx, dy = ay-by, dz = az-bz;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    };
    double maxDirectDistM = std::max({
        dist3d(rlx, rly, rz, slx, sly, sz),
        dist3d(rlx, rly, rz, srx, sry, sz),
        dist3d(rrx, rry, rz, slx, sly, sz),
        dist3d(rrx, rry, rz, srx, sry, sz)
    });
    if (p.main_decca_enabled)
    {
        maxDirectDistM = std::max(maxDirectDistM, std::max(
            dist3d(rcx, rcy, rz, slx, sly, sz),
            dist3d(rcx, rcy, rz, srx, sry, sz)));
    }

    int ec = (int)std::floor(0.085 * sr);

    // mo = p.direct_max_order (clamped 0..2). Needed here so irLen can size the
    // buffer to cover early reflections when the user has opted into them.
    const int mo = std::max(0, std::min(2, p.direct_max_order));

    // irLen needs to cover the farthest arrival we'll actually render plus the
    // bandpass-filter settling tail (512 samples ≈ 10 ms at 48 kHz).
    //
    // direct_max_order = 0: only the direct arrival per pair, so sizing to the
    //   longest direct path is sufficient. Keeps order-0 IRs tight (~40 ms) —
    //   preserves IR_17's regression lock.
    //
    // direct_max_order > 0: calcRefs emits first- and higher-order reflections
    //   up to the ec (85 ms) gate that `eo = true` enforces, so the buffer has
    //   to be long enough for renderCh to lay them down. Without this extension
    //   any reflection arriving past the short direct-window irLen is silently
    //   truncated inside renderCh — the user hears almost no room spatialisation
    //   at order 1/2 even though calcRefs actually generated the reflections.
    int irLen = (int)std::ceil(maxDirectDistM / SPEED * sr) + 512;
    if (mo > 0)
        irLen = std::max(irLen, ec + 512);
    // Guarantee a minimum usable length even in tiny rooms so filter tails have room.
    irLen = std::max(irLen, 1024);
    double den = shapeDen(p.shape);

    // Material reflection coefficients are unused for order-0 (no bounces), but
    // calcRefs signature requires them — compute cheaply.
    auto& mats = getMats();
    auto rc = [&](const std::string& m) -> std::array<double,8>
    {
        auto it = mats.find(m);
        if (it == mats.end()) it = mats.find("Painted plaster");
        std::array<double,8> r;
        for (int i = 0; i < N_BANDS; ++i) r[i] = std::sqrt(1.0 - it->second[i]);
        return r;
    };
    auto rF = rc(p.floor_material), rC = rc(p.ceiling_material), rW = rc(p.wall_material);

    double bakedErGain = p.bake_er_tail_balance ? p.baked_er_gain : 1.0;

    // `mo` was declared above with irLen sizing. Recap on its meaning:
    //   0 → calcRefs iterates only (0,0,0): a single direct arrival per pair
    //       (totalBounces = 0, no wall bounces) — the historical behaviour.
    //   1 → + first-order bounces (floor, ceiling, each of 4 walls singly).
    //   2 → + second-order bounces.
    // eo = true gates any reflection whose arrival is ≥ ec (85 ms), so DIRECT
    // can never leak content into what should be tail territory regardless of
    // the order limit or room size. Lambert scatter offsets that push past ec
    // are also skipped by the same gate.
    const bool eo = true;
    const double maxRefDist = 1e9;
    const double ts = 0.0;  // no scatter / temporal jitter
    const double oF = 1.0, vHfA = 0.0;
    // No jitter at all for DIRECT: close-speaker jitter would misalign the direct pulse.
    const double jitter01 = 0.0, jitter2 = 0.0;

    std::vector<Ref> rLL = calcRefs(rlx, rly, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 72, mainMicPattern, p.spkl_angle, faceL, maxRefDist, jitter01, jitter2, tiltL);
    std::vector<Ref> rRL = calcRefs(rlx, rly, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 73, mainMicPattern, p.spkr_angle, faceL, maxRefDist, jitter01, jitter2, tiltL);
    std::vector<Ref> rLR = calcRefs(rrx, rry, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 74, mainMicPattern, p.spkl_angle, faceR, maxRefDist, jitter01, jitter2, tiltR);
    std::vector<Ref> rRR = calcRefs(rrx, rry, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 75, mainMicPattern, p.spkr_angle, faceR, maxRefDist, jitter01, jitter2, tiltR);

    std::vector<Ref> rLC, rRC;
    if (p.main_decca_enabled)
    {
        rLC = calcRefs(rcx, rcy, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 76, mainMicPattern, p.spkl_angle, faceC, maxRefDist, jitter01, jitter2, tiltC);
        rRC = calcRefs(rcx, rcy, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 77, mainMicPattern, p.spkr_angle, faceC, maxRefDist, jitter01, jitter2, tiltC);
    }

    // No diffusion, no frequency scatter, no reflection spread — just the bpF cascade.
    const double diff = 0.0;
    const double reflectionSpreadMs = 0.0;
    const double freqScatterMs = 0.0;

    std::vector<double> iLL = renderCh(rLL, irLen, den, sr, diff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> iRL = renderCh(rRL, irLen, den, sr, diff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> iLR = renderCh(rLR, irLen, den, sr, diff, reflectionSpreadMs, freqScatterMs);
    std::vector<double> iRR = renderCh(rRR, irLen, den, sr, diff, reflectionSpreadMs, freqScatterMs);

    // Decca combine (same formula as synthMainPath — see that function for the
    // derivation). The DIRECT path is order-0 only, so the combine applies to
    // the short direct-arrival impulse responses before they are band-limited.
    if (p.main_decca_enabled)
    {
        std::vector<double> eLC = renderCh(rLC, irLen, den, sr, diff, reflectionSpreadMs, freqScatterMs);
        std::vector<double> eRC = renderCh(rRC, irLen, den, sr, diff, reflectionSpreadMs, freqScatterMs);
        auto hp1pole = [sr](std::vector<double>& v, double fcHz)
        {
            if (v.empty()) return;
            const double a = std::exp(-2.0 * M_PI * fcHz / (double)sr);
            double xPrev = 0.0, yPrev = 0.0;
            for (double& s : v)
            {
                double x = s;
                double y = a * (yPrev + x - xPrev);
                s = y;
                xPrev = x;
                yPrev = y;
            }
        };
        hp1pole(eLC, kDeccaHpHz);
        hp1pole(eRC, kDeccaHpHz);

        // Centre-fill gain: same mechanism as synthMainPath (MAIN). See the
        // companion decca_centre_gain block there for the derivation.
        const double gC = std::max(0.0, std::min((double) kDeccaGC, p.decca_centre_gain));
        for (int i = 0; i < irLen; ++i)
        {
            const double lc = gC * eLC[(size_t)i];
            const double rc = gC * eRC[(size_t)i];
            iLL[(size_t)i] += lc;
            iRL[(size_t)i] += rc;
            iLR[(size_t)i] += lc;
            iRR[(size_t)i] += rc;
        }
    }

    // Match MAIN's bakedErGain convention (applied to the ER portion in MAIN).
    for (auto* v : { &iLL, &iRL, &iLR, &iRR })
        for (double& s : *v) s *= bakedErGain;

    // Light band-limiting to match MAIN's post-processing profile.
    iLL = hpF(lpF(iLL, 18000.0, sr), 20.0, sr);
    iRL = hpF(lpF(iRL, 18000.0, sr), 20.0, sr);
    iLR = hpF(lpF(iLR, 18000.0, sr), 20.0, sr);
    iRR = hpF(lpF(iRR, 18000.0, sr), 20.0, sr);

    // +15 dB output trim (same convention as MAIN).
    {
        const double gain15dB = std::pow(10.0, 15.0 / 20.0);
        for (auto* v : { &iLL, &iRL, &iLR, &iRR })
            for (double& s : *v)
                s *= gain15dB;
    }

    out.LL = std::move(iLL);
    out.RL = std::move(iRL);
    out.LR = std::move(iLR);
    out.RR = std::move(iRR);
    out.irLen = irLen;
    out.synthesised = true;
    return out;
}

// ── makeWav — 24-bit quad (iLL,iRL,iLR,iRR), little-endian ────────────────
// Writes WAVE_FORMAT_EXTENSIBLE (tag 0xFFFE) with a 40-byte fmt chunk.
// Plain PCM (tag 0x0001) is rejected by JUCE's WavAudioFormat for 4-channel files.
std::vector<uint8_t> IRSynthEngine::makeWav (const std::vector<double>& iLL,
                                              const std::vector<double>& iRL,
                                              const std::vector<double>& iLR,
                                              const std::vector<double>& iRR,
                                              int sampleRate)
{
    size_t N = std::min({iLL.size(), iRL.size(), iLR.size(), iRR.size()});
    size_t ds = N * 4 * 3;  // 4 channels, 24-bit = 12 bytes/frame

    // EXTENSIBLE fmt chunk is 40 bytes; total header = 12 (RIFF+WAVE) + 48 (fmt) + 8 (data hdr)
    const size_t kFmtSize   = 40;
    const size_t kHeaderSize = 12 + (8 + kFmtSize) + 8;  // = 68
    std::vector<uint8_t> buf(kHeaderSize + ds);
    uint8_t* p = buf.data();

    // ── RIFF header ──────────────────────────────────────────────────────────
    memcpy(p, "RIFF", 4); p += 4;
    uint32_t v32 = (uint32_t)(4 + (8 + kFmtSize) + 8 + ds);  // WAVE + fmt chunk + data chunk
    memcpy(p, &v32, 4); p += 4;
    memcpy(p, "WAVE", 4); p += 4;

    // ── fmt chunk (WAVE_FORMAT_EXTENSIBLE, 40-byte body) ────────────────────
    memcpy(p, "fmt ", 4); p += 4;
    v32 = (uint32_t)kFmtSize; memcpy(p, &v32, 4); p += 4;

    uint16_t v16 = 0xFFFE; memcpy(p, &v16, 2); p += 2;  // wFormatTag = EXTENSIBLE
    v16 = 4;               memcpy(p, &v16, 2); p += 2;  // nChannels
    v32 = (uint32_t)sampleRate; memcpy(p, &v32, 4); p += 4;
    v32 = (uint32_t)(sampleRate * 4 * 3); memcpy(p, &v32, 4); p += 4;  // nAvgBytesPerSec
    v16 = 12; memcpy(p, &v16, 2); p += 2;  // nBlockAlign
    v16 = 24; memcpy(p, &v16, 2); p += 2;  // wBitsPerSample
    v16 = 22; memcpy(p, &v16, 2); p += 2;  // cbSize (size of extension = 22)
    v16 = 24; memcpy(p, &v16, 2); p += 2;  // wValidBitsPerSample
    v32 = 0x33; memcpy(p, &v32, 4); p += 4; // dwChannelMask (0x33 = FL+FR+BL+BR, matches JUCE writer)
    // SubFormat GUID for PCM: {00000001-0000-0010-8000-00AA00389B71}
    static const uint8_t kPCMGuid[16] = {
        0x01,0x00,0x00,0x00, 0x00,0x00, 0x10,0x00,
        0x80,0x00, 0x00,0xAA,0x00,0x38,0x9B,0x71
    };
    memcpy(p, kPCMGuid, 16); p += 16;

    // ── data chunk ──────────────────────────────────────────────────────────
    memcpy(p, "data", 4); p += 4;
    v32 = (uint32_t)ds; memcpy(p, &v32, 4); p += 4;

    for (size_t i = 0; i < N; ++i)
    {
        for (const double* s : { &iLL[i], &iRL[i], &iLR[i], &iRR[i] })
        {
            int32_t x = (int32_t)std::round(std::max(-1.0, std::min(1.0, *s)) * 8388607.0);
            p[0] = (uint8_t)(x & 0xff);
            p[1] = (uint8_t)((x >> 8) & 0xff);
            p[2] = (uint8_t)((x >> 16) & 0xff);
            p += 3;
        }
    }
    return buf;
}
