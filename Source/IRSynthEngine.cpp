#include "IRSynthEngine.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// ── constants (match JS verbatim) ──────────────────────────────────────────
const double IRSynthEngine::SPEED = 343.0;
const int    IRSynthEngine::BANDS[6] = { 125, 250, 500, 1000, 2000, 4000 };

// Material absorption coefficients [14 materials × 6 bands] — verbatim from JS MATS
static std::map<std::string, std::array<double,6>> s_mats;
static const std::map<std::string, std::array<double,6>>& initMats()
{
    if (s_mats.empty())
    {
        s_mats["Concrete / bare brick"]     = {{0.02, 0.03, 0.03, 0.04, 0.05, 0.07}};
        s_mats["Painted plaster"]           = {{0.01, 0.02, 0.02, 0.03, 0.04, 0.05}};
        s_mats["Hardwood floor"]            = {{0.04, 0.04, 0.07, 0.06, 0.06, 0.07}};
        s_mats["Carpet (thin)"]              = {{0.03, 0.05, 0.10, 0.20, 0.30, 0.35}};
        s_mats["Carpet (thick)"]             = {{0.08, 0.24, 0.57, 0.69, 0.71, 0.73}};
        s_mats["Glass (large pane)"]         = {{0.18, 0.06, 0.04, 0.03, 0.02, 0.02}};
        s_mats["Heavy curtains"]             = {{0.07, 0.31, 0.49, 0.75, 0.70, 0.60}};
        s_mats["Acoustic ceiling tile"]     = {{0.25, 0.45, 0.78, 0.92, 0.89, 0.87}};
        s_mats["Plywood panel"]             = {{0.28, 0.22, 0.17, 0.09, 0.10, 0.11}};
        s_mats["Upholstered seats"]         = {{0.49, 0.66, 0.80, 0.88, 0.82, 0.70}};
        s_mats["Bare wooden seats"]          = {{0.02, 0.03, 0.03, 0.06, 0.06, 0.05}};
        s_mats["Water / pool surface"]      = {{0.01, 0.01, 0.01, 0.02, 0.02, 0.03}};
        s_mats["Rough stone / rock"]         = {{0.02, 0.03, 0.03, 0.04, 0.04, 0.05}};
        s_mats["Exposed brick (rough)"]      = {{0.03, 0.03, 0.03, 0.04, 0.05, 0.07}};
    }
    return s_mats;
}
const std::map<std::string, std::array<double,6>>& IRSynthEngine::getMats() { return initMats(); }

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

// Audience, balcony, organ, air — verbatim from JS
const double IRSynthEngine::OA[6]  = { 0.06, 0.10, 0.14, 0.18, 0.22, 0.28 };
const double IRSynthEngine::BA[6]   = { 0.03, 0.04, 0.05, 0.06, 0.07, 0.08 };
const double IRSynthEngine::BSA[6]  = { 0.30, 0.45, 0.60, 0.72, 0.68, 0.58 };
const double IRSynthEngine::AIR[6] = { 0.0003, 0.001, 0.002, 0.005, 0.011, 0.026 };

// Mic polar pattern — verbatim from JS MIC
static std::map<std::string, std::pair<double,double>> s_mic;
static const std::map<std::string, std::pair<double,double>>& initMIC()
{
    if (s_mic.empty())
    {
        s_mic["omni"]       = {1.0, 0.0};
        s_mic["subcardioid"]= {0.7, 0.3};
        s_mic["cardioid"]   = {0.5, 0.5};
        s_mic["figure8"]    = {0.0, 1.0};
    }
    return s_mic;
}
const std::map<std::string, std::pair<double,double>>& IRSynthEngine::getMIC() { return initMIC(); }

// ── eyring — verbatim from JS ──────────────────────────────────────────────
double IRSynthEngine::eyring (double vol, double mAbs, double tS)
{
    if (mAbs >= 1.0) return 0.0;
    double l = std::log(1.0 - mAbs);
    if (std::abs(l) < 1e-9) return 99.0;
    return 0.161 * vol / (-tS * l);
}

// ── micG — verbatim from JS ───────────────────────────────────────────────
double IRSynthEngine::micG (double az, const std::string& pat, double faceAngle)
{
    auto it = getMIC().find(pat);
    if (it == getMIC().end()) return 1.0;
    double o = it->second.first, d = it->second.second;
    return std::max(0.0, o + d * std::cos(az - faceAngle));
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
    const auto& mw = mwIt != mats.end() ? mwIt->second : mats.find("Painted plaster")->second;

    std::vector<double> rt60(6);
    for (int i = 0; i < 6; ++i)
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
    const std::array<double,6>& rF,
    const std::array<double,6>& rC,
    const std::array<double,6>& rW,
    double oF, double vHfA, double ts,
    bool eo, int ec, int sr,
    uint32_t seed,
    const std::string& micPat,
    double spkFaceAngle, double micFaceAngle)
{
    double W = p.width, D = p.depth;
    Rng rng = mkRng(seed);
    std::vector<Ref> refs;

    for (int nx = -mo; nx <= mo; ++nx)
        for (int ny = -mo; ny <= mo; ++ny)
            for (int nz = -mo; nz <= mo; ++nz)
            {
                double ix = nx * W + (nx % 2 ? W - sx : sx);
                double iy = ny * D + (ny % 2 ? D - sy : sy);
                double iz = nz * He + (nz % 2 ? He - sz : sz);
                double dist = std::sqrt((ix - rx) * (ix - rx) + (iy - ry) * (iy - ry) + (iz - rz) * (iz - rz));
                if (dist < 1e-6) continue;

                int t = (int)std::floor(dist / SPEED * sr);
                if (eo && t > ec) continue;
                if (ts > 0.05) t = std::max(1, t + (int)std::floor(rU(rng, -ts * 4.0, ts * 4.0) * sr / 1000.0));

                double az = std::atan2(ix - rx, iy - ry);
                double mg = micG(az, micPat, micFaceAngle);
                double spkAz = std::atan2(ix - sx, iy - sy);
                double sg = spkG(spkFaceAngle, spkAz);

                int totalBounces = std::abs(nx) + std::abs(ny) + std::abs(nz);
                double polarity = (totalBounces % 2 == 0) ? 1.0 : -1.0;

                std::array<double,6> amps;
                for (int b = 0; b < 6; ++b)
                {
                    double a = 1.0 / std::max(dist, 0.5);
                    a *= std::pow(rF[b], std::ceil(std::abs(nz) / 2.0));
                    a *= std::pow(rC[b], std::floor(std::abs(nz) / 2.0));
                    a *= std::pow(rW[b], std::abs(nx) + std::abs(ny));
                    if (std::abs(ny) > 0) a *= std::pow(oF, std::abs(ny));
                    if (std::abs(nz) > 0) a *= std::pow(1.0 - vHfA * std::min(b / 3.0, 1.0), std::abs(nz));
                    amps[b] = a * std::pow(10.0, -AIR[b] * dist / 20.0) * mg * sg * polarity;
                }
                refs.push_back({ t, amps, az });
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
    int irLen, double den, int sr, double diffusion)
{
    std::vector<std::vector<double>> bi(6);
    for (int b = 0; b < 6; ++b)
        bi[b].resize((size_t)irLen, 0.0);

    for (const auto& r : refs)
    {
        if (r.t >= irLen) continue;
        double lat = std::abs(std::sin(r.az));
        for (int b = 0; b < 6; ++b)
            bi[b][(size_t)r.t] += r.amps[b] * den * (1.0 - lat * (b / 5.0) * 0.5);
    }

    std::vector<double> raw((size_t)irLen, 0.0);
    for (int b = 0; b < 6; ++b)
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

    if (diffusion > 0.02)
    {
        AllpassDiffuser diff = makeAllpassDiffuser(sr, diffusion);
        for (int i = 0; i < irLen; ++i)
            raw[(size_t)i] = diff.process(raw[(size_t)i]);
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
    double roomW, double roomD, double roomH)
{
    (void)diffusion;
    const int N = 16;
    double vol = roomW * roomD * roomH;
    double surf = 2.0 * (roomW * roomD + roomD * roomH + roomW * roomH);
    double mfp = 4.0 * vol / surf;
    double mfpMs = mfp / SPEED * 1000.0;
    double minMs = std::max(20.0, mfpMs * 0.5);
    double maxMs = std::max(150.0, mfpMs * 3.0);

    std::vector<int> delays(N);
    for (int i = 0; i < N; ++i)
    {
        double t = minMs + (maxMs - minMs) * i / (N - 1.0);
        delays[i] = nearestPrime((int)std::round(t * sr / 1000.0));
    }
    for (int i = 1; i < N; ++i)
        if (delays[i] <= delays[i - 1])
            delays[i] = nearestPrime(delays[i - 1] + 2);

    const double LFO_DEPTH_MS = 1.2;
    int lfoDepthSamp = (int)std::round(LFO_DEPTH_MS * sr / 1000.0);

    std::vector<std::vector<double>> bufs(N);
    for (int i = 0; i < N; ++i)
        bufs[i].resize((size_t)(delays[i] + lfoDepthSamp * 2 + 4), 0.0);

    std::vector<int> writePtr(N, 0);
    std::vector<double> lfoRates(N), lfoPhaseAcc(N);
    for (int i = 0; i < N; ++i)
    {
        lfoRates[i] = (0.07 + i * 0.025) * 2.0 * 3.141592653589793 / sr;
        lfoPhaseAcc[i] = (seed == 101 ? 3.141592653589793 : 0.0) + i * 0.7853;
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
        double gHF = std::pow(10.0, -3.0 * d / (rt60s[5] * sr));
        double alpha = std::min(0.9995, std::max(0.05, gHF / std::max(gLF, 1e-9)));
        lpAlpha[i] = { alpha, gLF };
    }

    std::vector<double> tmpMix(N);

    auto fdnStep = [&](double inject, int sampleIdx) -> double
    {
        (void)sampleIdx;
        for (int ch = 0; ch < N; ++ch)
        {
            lfoPhaseAcc[ch] += lfoRates[ch];
            double lfoMod = lfoDepthSamp * std::sin(lfoPhaseAcc[ch]);
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

    int fadeLen = (int)std::floor(0.040 * sr);
    int fadeStart = std::max(0, erCut - fadeLen);
    for (int i = 0; i < erCut; ++i)
    {
        double env = i < fadeStart ? 0.0 : (double)(i - fadeStart) / fadeLen;
        fdnStep(erIR[(size_t)i] * env, i);
    }

    std::vector<double> out((size_t)irLen, 0.0);
    for (int i = erCut; i < irLen; ++i)
        out[(size_t)i] = fdnStep(0.0, i);

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

// ── synthIR — verbatim from JS ─────────────────────────────────────────────
IRSynthResult IRSynthEngine::synthIR (const IRSynthParams& p, IRSynthProgressFn cb)
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
    double rm = rt[2];
    bool eo = p.er_only;

    int irLen = (int)std::floor((eo ? 0.15 : std::max(0.3, std::min(rm * 1.5 + 0.1, 30.0))) * sr);
    int ec = (int)std::floor(0.085 * sr);

    double den = shapeDen(p.shape);
    int mo = eo ? 3 : std::min(60, std::max(3, (int)std::floor(rm * SPEED / std::min(std::min(p.width, p.depth), He) / 2.0)));
    double ts = std::min(0.95, vs + p.organ_case * 0.35 + p.balconies * 0.25 + p.diffusion * 0.3);
    double oF = 1.0 - p.organ_case * 0.4;

    auto& mats = getMats();
    auto rc = [&](const std::string& m) -> std::array<double,6>
    {
        auto it = mats.find(m);
        if (it == mats.end()) it = mats.find("Painted plaster");
        std::array<double,6> r;
        for (int i = 0; i < 6; ++i) r[i] = std::sqrt(1.0 - it->second[i]);
        return r;
    };
    auto rF = rc(p.floor_material), rC = rc(p.ceiling_material), rW = rc(p.wall_material);

    report(0.05, "Computing image sources…");

    double sz = He * 0.55, rz = He * 0.55;
    double slx = p.width * p.source_lx, sly = p.depth * p.source_ly;
    double srx = p.width * p.source_rx, sry = p.depth * p.source_ry;
    double rlx = p.width * p.receiver_lx, rly = p.depth * p.receiver_ly;
    double rrx = p.width * p.receiver_rx, rry = p.depth * p.receiver_ry;

    std::vector<Ref> rLL = calcRefs(rlx, rly, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 42, p.mic_pattern, p.spkl_angle, p.micl_angle);
    std::vector<Ref> rRL = calcRefs(rlx, rly, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 43, p.mic_pattern, p.spkr_angle, p.micl_angle);
    std::vector<Ref> rLR = calcRefs(rrx, rry, rz, slx, sly, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 44, p.mic_pattern, p.spkl_angle, p.micr_angle);
    std::vector<Ref> rRR = calcRefs(rrx, rry, rz, srx, sry, sz, p, He, mo, rF, rC, rW, oF, vHfA, ts, eo, ec, sr, 45, p.mic_pattern, p.spkr_angle, p.micr_angle);

    report(0.30, "Rendering " + std::to_string(rLL.size() + rRL.size() + rLR.size() + rRR.size()) + " reflections…");

    double diff = p.diffusion;
    std::vector<double> eLL = renderCh(rLL, irLen, den, sr, diff);
    std::vector<double> eRL = renderCh(rRL, irLen, den, sr, diff);
    std::vector<double> eLR = renderCh(rLR, irLen, den, sr, diff);
    std::vector<double> eRR = renderCh(rRR, irLen, den, sr, diff);

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
        std::vector<double> tL = renderFDNTail(rt, irLen, ec, eL, diff, sr, 100, p.width, p.depth, He);
        std::vector<double> tR = renderFDNTail(rt, irLen, ec, eR, diff, sr, 101, p.width, p.depth, He);

        iLL.resize((size_t)irLen);
        iRL.resize((size_t)irLen);
        iLR.resize((size_t)irLen);
        iRR.resize((size_t)irLen);
        int xfade = (int)std::floor(0.020 * sr);
        for (int i = 0; i < irLen; ++i)
        {
            double ef = (i < ec - xfade) ? 1.0 : (i < ec) ? (double)(ec - i) / xfade : 0.0;
            double tf = (i < ec - xfade) ? 0.0 : (i < ec) ? (double)(i - (ec - xfade)) / xfade : 1.0;
            double tailL = tL[(size_t)i] * tf * 0.5;
            double tailR = tR[(size_t)i] * tf * 0.5;
            iLL[(size_t)i] = eLL[(size_t)i] * ef + tailL;
            iRL[(size_t)i] = eRL[(size_t)i] * ef + tailL;
            iLR[(size_t)i] = eLR[(size_t)i] * ef + tailR;
            iRR[(size_t)i] = eRR[(size_t)i] * ef + tailR;
        }
    }
    else
    {
        iLL = eLL;
        iRL = eRL;
        iLR = eLR;
        iRR = eRR;
    }

    report(0.85, "Finishing…");

    iLL = hpF(lpF(iLL, 18000.0, sr), 20.0, sr);
    iRL = hpF(lpF(iRL, 18000.0, sr), 20.0, sr);
    iLR = hpF(lpF(iLR, 18000.0, sr), 20.0, sr);
    iRR = hpF(lpF(iRR, 18000.0, sr), 20.0, sr);

    // Normalize against combined channels so mono input sounds identical to before
    double pk = 0.0;
    for (size_t i = 0; i < iLL.size(); ++i)
    {
        pk = std::max(pk, std::abs(iLL[i] + iRL[i]));  // combined L — mono input safety
        pk = std::max(pk, std::abs(iLR[i] + iRR[i]));  // combined R — mono input safety
        pk = std::max(pk, std::abs(iLL[i]));             // individual — fully-left input safety
        pk = std::max(pk, std::abs(iRL[i]));             // individual — fully-right input safety
        pk = std::max(pk, std::abs(iLR[i]));             // individual — fully-left input safety
        pk = std::max(pk, std::abs(iRR[i]));             // individual — fully-right input safety
    }
    if (pk > 1e-9)
    {
        double gv = 0.708 / pk;
        for (size_t i = 0; i < iLL.size(); ++i)
        {
            iLL[i] *= gv;
            iRL[i] *= gv;
            iLR[i] *= gv;
            iRR[i] *= gv;
        }
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

// ── makeWav — 24-bit quad (iLL,iRL,iLR,iRR), little-endian ────────────────
std::vector<uint8_t> IRSynthEngine::makeWav (const std::vector<double>& iLL,
                                              const std::vector<double>& iRL,
                                              const std::vector<double>& iLR,
                                              const std::vector<double>& iRR,
                                              int sampleRate)
{
    size_t N = std::min({iLL.size(), iRL.size(), iLR.size(), iRR.size()});
    size_t ds = N * 4 * 3;  // 4 channels, 24-bit
    std::vector<uint8_t> buf(44 + ds);
    uint8_t* p = buf.data();

    memcpy(p, "RIFF", 4); p += 4;
    uint32_t v32 = (uint32_t)(36 + ds);
    memcpy(p, &v32, 4); p += 4;
    memcpy(p, "WAVE", 4); p += 4;
    memcpy(p, "fmt ", 4); p += 4;
    v32 = 16; memcpy(p, &v32, 4); p += 4;
    uint16_t v16 = 1; memcpy(p, &v16, 2); p += 2;
    v16 = 4; memcpy(p, &v16, 2); p += 2;  // 4 channels
    v32 = (uint32_t)sampleRate; memcpy(p, &v32, 4); p += 4;
    v32 = (uint32_t)(sampleRate * 4 * 3); memcpy(p, &v32, 4); p += 4;  // byte rate
    v16 = 12; memcpy(p, &v16, 2); p += 2;  // block align
    v16 = 24; memcpy(p, &v16, 2); p += 2;
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
