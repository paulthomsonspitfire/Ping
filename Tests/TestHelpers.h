#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>
#include <algorithm>

// ── Minimal deterministic RNG (mirrors IRSynthEngine::Rng) ──────────────────
// Used to generate reproducible noise sequences in tests without depending
// on std::rand or platform-specific random facilities.
struct TestRng
{
    uint32_t r;
    explicit TestRng (uint32_t seed) : r(seed) {}

    double next()
    {
        r += 0x6D2B79F5u;
        uint32_t t = r;
        t = (uint32_t)((int32_t)(t ^ (t >> 15)) * (int32_t)(t | 1));
        t ^= t + (uint32_t)((int32_t)(t ^ (t >> 7)) * (int32_t)(t | 61));
        return (double)((t ^ (t >> 14)) & 0xFFFFFFFFu) / 4294967296.0;
    }

    // Returns a sample in [-1, 1]
    float nextFloat() { return (float)(next() * 2.0 - 1.0); }
};

// ── Energy / RMS helpers ────────────────────────────────────────────────────

inline double totalEnergy (const std::vector<double>& v, int start = 0, int end = -1)
{
    if (end < 0) end = (int)v.size();
    double e = 0.0;
    for (int i = start; i < end; ++i) e += v[i] * v[i];
    return e;
}

inline double totalEnergyF (const std::vector<float>& v, int start = 0, int end = -1)
{
    if (end < 0) end = (int)v.size();
    double e = 0.0;
    for (int i = start; i < end; ++i) e += (double)v[i] * v[i];
    return e;
}

inline double windowRMS (const std::vector<double>& v, int start, int len)
{
    int end = std::min(start + len, (int)v.size());
    if (end <= start) return 0.0;
    return std::sqrt(totalEnergy(v, start, end) / (end - start));
}

inline double windowRMSF (const std::vector<float>& v, int start, int len)
{
    int end = std::min(start + len, (int)v.size());
    if (end <= start) return 0.0;
    return std::sqrt(totalEnergyF(v, start, end) / (end - start));
}

// ── NaN / Inf scanner ───────────────────────────────────────────────────────

inline bool hasNaNorInf (const std::vector<double>& v)
{
    for (double x : v)
        if (std::isnan(x) || std::isinf(x)) return true;
    return false;
}

inline bool hasNaNorInfF (const std::vector<float>& v)
{
    for (float x : v)
        if (std::isnan(x) || std::isinf(x)) return true;
    return false;
}

// ── L2 distance between two vectors ────────────────────────────────────────

inline double l2diff (const std::vector<double>& a, const std::vector<double>& b)
{
    double sum = 0.0;
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) { double d = a[i] - b[i]; sum += d * d; }
    return sum;
}

// ── Peak magnitude in a frequency range via DFT ────────────────────────────
// Searches [freqLo, freqHi] Hz in integer steps. Returns the frequency (Hz)
// with the largest magnitude. Uses only the second half of the buffer to
// skip any transient. O(N * (freqHi - freqLo)) — keep ranges narrow.

inline int peakFrequencyHz (const std::vector<float>& signal, int sampleRate,
                             int freqLo, int freqHi)
{
    int N    = (int)signal.size();
    int half = N / 2;          // skip first half (transient)
    int len  = N - half;

    float bestMag = 0.f;
    int   bestHz  = freqLo;

    for (int f = freqLo; f <= freqHi; ++f)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < len; ++n)
        {
            double angle = 2.0 * M_PI * f * (half + n) / sampleRate;
            re += signal[(size_t)(half + n)] * std::cos(angle);
            im += signal[(size_t)(half + n)] * std::sin(angle);
        }
        float mag = (float)(re * re + im * im);
        if (mag > bestMag) { bestMag = mag; bestHz = f; }
    }
    return bestHz;
}
