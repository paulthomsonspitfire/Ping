#pragma once

#include <cmath>

// ── HP2ndOrder ──────────────────────────────────────────────────────────────
// 2nd-order Butterworth high-pass (RBJ biquad, Q = 1/√2).
//
// Designed for the per-path HP in the multi-mic mixer (MAIN, DIRECT, OUTRIG,
// AMBIENT). Shared by all 4 strips. Pure header — no JUCE dependency — so it
// is linkable from both the plugin and the test binary.
//
// Usage:
//     HP2ndOrder hp;
//     hp.prepare (110.0f, 48000.0);    // cutoff, sample rate
//     hp.enabled = true;
//     float y = hp.process (x, ch);    // ch = 0 (L) or 1 (R)
//
// Design notes:
//   • `enabled` is a hard toggle — no internal smoothing / crossfade. To keep
//     toggles click-free (see DSP_17) the biquad state is ALWAYS clocked
//     through `process()`; `enabled` only selects whether the output sample
//     is the filtered value or the input sample. Re-enabling after a dormant
//     period therefore resumes with the same state the filter would have had
//     if it had been enabled the whole time — no attack transient. This is
//     the D5 ("hard HP toggle") decision from the multi-mic work plan.
//   • Stereo only (2 channels of state). Mono callers can just use ch=0.
//   • `reset()` zeros the state (useful on prepareToPlay / sample-rate
//     changes).
// ───────────────────────────────────────────────────────────────────────────
struct HP2ndOrder
{
    // Public state — callers may flip `enabled` directly between samples.
    bool  enabled = false;

    // Biquad coefficients (computed in prepare()).
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    // Per-channel filter state (direct form I: input + output history).
    float x1 [2] { 0.0f, 0.0f };
    float x2 [2] { 0.0f, 0.0f };
    float y1 [2] { 0.0f, 0.0f };
    float y2 [2] { 0.0f, 0.0f };

    // Recompute coefficients for the requested cutoff + sample rate.
    // Uses the canonical RBJ Audio EQ Cookbook formulas with Q = 1/√2 (= the
    // Butterworth pole placement for a 2nd-order section).
    void prepare (float fc, double sr) noexcept
    {
        const double fcClamped = fc < 1.0f
                                   ? 1.0
                                   : (fc > 0.45 * sr ? 0.45 * sr : (double) fc);
        const double w0    = 2.0 * 3.14159265358979323846 * fcClamped / sr;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        // Q = 1/√2 (Butterworth) → α = sinw0 / √2
        const double alpha = sinw0 / 1.41421356237309504880;

        const double ib0 = (1.0 + cosw0) * 0.5;
        const double ib1 = -(1.0 + cosw0);
        const double ib2 = (1.0 + cosw0) * 0.5;
        const double ia0 =  1.0 + alpha;
        const double ia1 = -2.0 * cosw0;
        const double ia2 =  1.0 - alpha;

        const double inv = 1.0 / ia0;
        b0 = (float) (ib0 * inv);
        b1 = (float) (ib1 * inv);
        b2 = (float) (ib2 * inv);
        a1 = (float) (ia1 * inv);
        a2 = (float) (ia2 * inv);
    }

    void reset() noexcept
    {
        x1[0] = x2[0] = y1[0] = y2[0] = 0.0f;
        x1[1] = x2[1] = y1[1] = y2[1] = 0.0f;
    }

    // Process a single sample on channel `ch` (0 or 1). State is ALWAYS
    // advanced regardless of `enabled` (see class comment).
    inline float process (float x, int ch) noexcept
    {
        const float y = b0 * x + b1 * x1[ch] + b2 * x2[ch]
                      - a1 * y1[ch] - a2 * y2[ch];

        x2[ch] = x1[ch];
        x1[ch] = x;
        y2[ch] = y1[ch];
        y1[ch] = y;

        return enabled ? y : x;
    }
};
