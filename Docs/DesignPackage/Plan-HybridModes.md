# P!NG — Hybrid Mode Implementation Plan

**Target version:** 1.8.0
**Status:** Pre-implementation research plan
**Scope:** Four optional, independently switchable "hybrid" processing modes that extend P!NG beyond physically-accurate IR convolution into algorithmic reverb territory. All four operate entirely in `PluginProcessor.h/.cpp` (`processBlock`), are non-destructive to the existing signal path, and work identically with both file-loaded IRs and synthesised IRs.

---

## Guiding architecture principles

1. **No changes to `IRSynthEngine`** — all four features are real-time processors applied to the wet convolution output (or input), not baked into the IR.
2. **Optional by toggle** — each feature has a bool APVTS parameter (`plateOn`, `bloomOn`, `cloudOn`, `shimmerOn`). When off, zero DSP overhead and identical output to current behaviour.
3. **Non-destructive insertion** — features slot into the existing signal chain without removing or reordering any current stages.
4. **Consistent parameter naming** — IDs follow the existing `IDs::` namespace pattern in `PluginProcessor.cpp`.

---

## Current signal chain recap (post-convolution)

```
[Convolution: ER × erLevel + Tail × tailLevel]
  → 3-band Parametric EQ
  → Stereo decorrelation allpass (R ch only)
  → LFO Modulation (wet gain × sin, existing)
  → Stereo Width (M/S)
  → Tail Chorus Modulation (existing chorusDelayLine)
  → Output Gain
  → Dry/Wet blend
```

New features will insert at two points:
- **Pre-convolution:** Plate, Bloom (as input diffusers and/or feedback inject)
- **Post-convolution, after Width/Tail Chorus:** Cloud, Shimmer

---

## New signal chain (with all four active)

```
Input (stereo)
  │
  ├──────────────────────────────── dryBuffer copy
  │
  ▼
Predelay → Input Gain → Saturator
  │
  │◄── [Shimmer feedback: attenuated, diffused shimmer output]  (if shimmerOn)
  │◄── [Bloom feedback: attenuated, diffused wet tail output]   (if bloomOn)
  │
  ▼
[1. PLATE: allpass diffuser cascade on input]    (if plateOn)
[2. BLOOM: allpass diffuser cascade on input]    (if bloomOn)
  │                                               (shares allpass type, separate delay sizes)
  ▼
Convolution (ER + Tail, unchanged)
  │
  ▼
3-band EQ → Stereo decorrelation → LFO mod → Width → Tail Chorus
  │
  ▼
[3. CLOUD: 8-line parallel LFO-modulated delay bank]   (if cloudOn)
  │
  ▼
[4. SHIMMER: two-voice granular pitch shift + feedback] (if shimmerOn)
  │
  ▼
Output Gain → Dry/Wet blend → Output
```

---

---

## Feature 1 — Plate Onset

### What it does

Replaces the "room geometry in the first 50 ms" feel with a dense, immediate diffuse wash — the defining acoustic property of a physical plate reverb. Rather than discrete early reflections that give away the room shape, the onset blooms instantly into an undifferentiated density.

### How it works

A cascade of allpass filters applied to the **input signal before convolution** acts as a dense pre-diffuser. Every incoming transient is turned from a clean impulse into a brief smeared cloud. When that cloud passes through the convolution IR, the early reflections (which are sharp and room-specific in the IR) are blurred together into something that sounds and feels like the fast-building onset of a plate.

This is acoustically equivalent to what happens inside a real plate reverb: the metal surface disperses sound in all directions simultaneously, removing directionality and geometry from the response. In the DSP domain, the allpass cascade achieves the same effect by scattering each input sample across its neighbouring samples before the convolver sees it.

The cascade uses 6 allpass stages with incommensurately prime delay lengths spanning approximately 3 ms to 20 ms total accumulation, with g = 0.65–0.75. At `plateDensity = 0` the cascade is bypassed. At `plateDensity = 1` the input is fully diffused before convolution.

### Controls

| Parameter ID | Name | Range | Default | Effect |
|---|---|---|---|---|
| `plateOn` | Plate On | bool | false | Enables the cascade |
| `plateDensity` | Plate Density | 0–1 | 0.5 | Mix between raw and diffused input into convolver |
| `plateColour` | Plate Colour | 0–1 | 0.5 | HF shelf cutoff: maps 0 → 2 kHz, 1 → 8 kHz (brighter at high end) |
| `plateSize` | Plate Size | 0.5–2.0 | 1.0 | Scale factor applied to all 6 allpass delay times |

`plateColour` uses a 1-pole high-shelf applied to the diffused path before mixing. Lower values produce the warm, dark quality of large metal plates (EMT 140 character); higher values produce the bright, clear quality of smaller plates (AMS RMX16 plate simulation).

`plateSize` scales all six allpass delay times uniformly. At 0.5 the total cascade spans ~1.5–10 ms — tight, punchy, modern plate character. At 2.0 it spans ~6–40 ms — slower, vintage EMT-style density build. Allpass buffers are allocated at 2× the base delays in `prepareToPlay` so the full 0.5–2.0 range is covered without reallocation. The `SimpleAllpass` struct's effective wrap length is set per-sample to `(int)(basePrime * plateSize)` rather than `buf.size()`.

### New state in `PluginProcessor.h`

```cpp
// Plate onset: pre-convolution allpass diffuser cascade
struct SimpleAllpass {
    std::vector<float> buf;
    int   ptr    = 0;
    int   effLen = 0;  // effective delay length (≤ buf.size()); set each block for plateSize support
    float g      = 0.7f;
    float process (float x) noexcept {
        int len = (effLen > 0 && effLen <= (int)buf.size()) ? effLen : (int)buf.size();
        float d = buf[(size_t)ptr];
        float w = x + g * d;
        buf[(size_t)ptr] = w;
        ptr = (ptr + 1) % len;
        return d - g * w;
    }
};

static constexpr int kNumPlateStages = 6;
std::array<std::array<SimpleAllpass, kNumPlateStages>, 2> plateAPs; // [ch][stage]

// 1-pole HF shelf state for plateColour
std::array<float, 2> plateShelfState { 0.f, 0.f };
```

### `prepareToPlay` additions

```cpp
// Plate: 6 allpass stages, prime delay times in samples
// Chosen to be incommensurate with FDN delays and with each other.
// At 48 kHz base primes span ~0.5 ms to ~7 ms; total max group delay ~20 ms.
// Buffers are allocated at 2× the base primes so plateSize can reach 2.0 without reallocation.
const int platePrimes[6] = { 24, 71, 157, 293, 431, 691 }; // at 48 kHz
for (int ch = 0; ch < 2; ++ch)
    for (int s = 0; s < kNumPlateStages; ++s) {
        int d = (int)std::round(platePrimes[s] * sampleRate / 48000.0 * 2.0); // 2× for plateSize headroom
        plateAPs[ch][s].buf.assign((size_t)d, 0.f);
        plateAPs[ch][s].ptr = 0;
        plateAPs[ch][s].g   = 0.70f;
    }
plateShelfState = { 0.f, 0.f };
```

### `processBlock` insertion point

After the saturator, **before** the convolution block, add:

```cpp
bool plateOn = apvts.getRawParameterValue (IDs::plateOn)->load() > 0.5f;
if (plateOn)
{
    float density  = apvts.getRawParameterValue (IDs::plateDensity)->load();
    float colour   = apvts.getRawParameterValue (IDs::plateColour)->load();
    float plateSize = juce::jlimit (0.5f, 2.0f, apvts.getRawParameterValue (IDs::plateSize)->load());
    // colour → 1-pole shelf alpha: maps 0→1 to cutoff 2kHz→8kHz
    float cutHz    = 2000.f + colour * 6000.f;
    float shelfAlpha = 1.f - std::exp (-2.f * juce::MathConstants<float>::pi * cutHz / (float)currentSampleRate);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float in  = data[i];
            float diff = in;
            // plateSize scales each stage's effective delay length within the 2× allocated buffer
            const int platePrimes[6] = { 24, 71, 157, 293, 431, 691 };
            for (int s = 0; s < kNumPlateStages; ++s) {
                int effLen = (int)std::round (platePrimes[s] * currentSampleRate / 48000.0 * (double)plateSize);
                plateAPs[ch][s].effLen = effLen; // SimpleAllpass wraps ptr at effLen, not buf.size()
                diff = plateAPs[ch][s].process (diff);
            }
            // 1-pole HF shelf: attenuates HF above cutHz in diffused path
            plateShelfState[ch] = plateShelfState[ch] + shelfAlpha * (diff - plateShelfState[ch]);
            float shaped = plateShelfState[ch]; // low-passed (warmer)
            data[i] = in * (1.f - density) + shaped * density;
        }
    }
}
```

### Why this is safe

The allpass cascade is all-pass in frequency — it does not boost or cut any frequency, only scatters in time. It cannot cause gain increases or instability. The mix parameter means `density = 0` leaves the signal completely unmodified. The shelf is a simple 1-pole and cannot overshoot. The existing convolver receives the slightly-smeared input and processes it exactly as before.

---

---

## Feature 2 — Bloom Pre-Diffusion

### What it does

Creates a progressively-building reverb onset where the reverb appears to grow out of silence and swell into the room, rather than the reflections arriving all at once. Adds a feedback path from the wet output back to the pre-convolution input, causing the reverb to continuously self-seed and expand.

Two elements work together:
- **Pre-convolution allpass cascade** (longer delays than Plate, emphasising a gradual smear rather than an immediate dense wash)
- **Wet output feedback** (a fraction of the wet convolution output is fed back through the allpass cascade and added to the convolver input, causing the reverb to grow with each pass)

### How it works

The allpass cascade for Bloom uses 4 stages with longer delays (40–300 ms range), creating the "bloom" character where dense diffusion builds over multiple feedback cycles. The feedback signal passes through the same cascade before entering the convolver, so each recirculation becomes progressively more diffuse — the bloom expands rather than repeating.

Feedback stability: the convolver has gain < 1 for any physical IR. The total loop gain is `bloomFeedback × convolver_gain`, which must remain < 1 to be stable. At `bloomFeedback = 0.6` this is reliable for all P!NG IRs since convolver gain at steady state approaches 0. A safety clamp ensures `bloomFeedback ≤ 0.65`.

### Controls

| Parameter ID | Name | Range | Default | Effect |
|---|---|---|---|---|
| `bloomOn` | Bloom On | bool | false | Enables bloom path |
| `bloomDiffusion` | Bloom Diffusion | 0–1 | 0.5 | Amount of allpass cascade applied to input |
| `bloomFeedback` | Bloom Feedback | 0–0.65 | 0.25 | Wet-to-input feedback amount; higher = more self-sustaining swell |
| `bloomTime` | Bloom Time | 50–500 ms | 200 ms | Delay of the feedback loop — how far back in the wet signal the feedback tap reads |

At `bloomFeedback = 0` the effect is pure pre-diffusion with a more gradual onset than Plate (longer allpass delays, 4 stages). At higher feedback the reverb blooms continuously — the longer the IR's RT60, the more pronounced this becomes.

`bloomTime` controls the feedback cycle length. Short values (50–100 ms) produce a fast, rhythmic swell that resolves quickly. Long values (300–500 ms) give a slow, expansive bloom that hangs and sustains. The feedback buffer is allocated at the maximum (500 ms); `bloomTime` sets the read-offset into it: `rdPtr = (bloomFbWritePtrs[ch] - timeInSamples + fbLen) % fbLen`. At `bloomFeedback = 0`, this parameter has no audible effect.

### New state in `PluginProcessor.h`

```cpp
// Bloom: pre-convolution allpass cascade (longer delays than Plate)
static constexpr int kNumBloomStages = 4;
std::array<std::array<SimpleAllpass, kNumBloomStages>, 2> bloomAPs; // [ch][stage]

// Bloom feedback: circular buffer holds last ~500 ms of wet signal
// The wet signal is tapped after convolution, attenuated, and fed back here.
// bloomTime (50–500 ms) sets how far back the feedback tap reads into this buffer.
static constexpr int kBloomFeedbackMaxMs = 500;
std::array<std::vector<float>, 2> bloomFbBufs;    // [ch]
std::array<int, 2>               bloomFbWritePtrs { 0, 0 };
```

### `prepareToPlay` additions

```cpp
// Bloom: 4 allpass stages with longer prime delays (40–300 ms range at 48 kHz)
// These are much longer than Plate stages, creating a slower-building character.
const int bloomPrimes[4] = { 1871, 3541, 7211, 14387 }; // ~39, ~74, ~150, ~300 ms at 48k
for (int ch = 0; ch < 2; ++ch)
    for (int s = 0; s < kNumBloomStages; ++s) {
        int d = (int)std::round (bloomPrimes[s] * sampleRate / 48000.0);
        bloomAPs[ch][s].buf.assign ((size_t)d, 0.f);
        bloomAPs[ch][s].ptr = 0;
        bloomAPs[ch][s].g   = 0.60f;
    }

int fbSamps = (int)std::ceil (kBloomFeedbackMaxMs * sampleRate / 1000.0);
for (int ch = 0; ch < 2; ++ch) {
    bloomFbBufs[ch].assign ((size_t)fbSamps, 0.f);
    bloomFbWritePtrs[ch] = 0;
}
```

### `processBlock` insertion points (two locations)

**Location A — after the saturator, before convolution (feedback inject + diffusion):**

```cpp
bool bloomOn = apvts.getRawParameterValue (IDs::bloomOn)->load() > 0.5f;
if (bloomOn)
{
    float diffAmt  = apvts.getRawParameterValue (IDs::bloomDiffusion)->load();
    float fbAmt    = std::min (0.65f, apvts.getRawParameterValue (IDs::bloomFeedback)->load());
    float bloomTimeMs = juce::jlimit (50.f, 500.f, apvts.getRawParameterValue (IDs::bloomTime)->load());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        int fbLen   = (int)bloomFbBufs[ch].size();
        int timeInSamples = juce::jlimit (1, fbLen - 1, (int)std::round (bloomTimeMs * currentSampleRate / 1000.0));
        for (int i = 0; i < numSamples; ++i)
        {
            // Read feedback from bloomTime ms ago (not just 1 sample — drives the swell cycle length)
            int rdPtr = (bloomFbWritePtrs[ch] - timeInSamples + fbLen) % fbLen;
            float fb  = bloomFbBufs[ch][(size_t)rdPtr] * fbAmt;
            // Diffuse feedback + input through bloom allpass cascade
            float in   = data[i] + fb;
            float diff = in;
            for (int s = 0; s < kNumBloomStages; ++s)
                diff = bloomAPs[ch][s].process (diff);
            data[i] = data[i] * (1.f - diffAmt) + diff * diffAmt;
        }
    }
}
```

**Location B — after the convolution / EQ / decorrelation chain, before LFO mod (feedback tap):**

```cpp
if (bloomOn)
{
    // Tap the wet signal here and store in bloom feedback buffer for use next block.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        int fbLen = (int)bloomFbBufs[ch].size();
        for (int i = 0; i < numSamples; ++i)
        {
            bloomFbBufs[ch][(size_t)bloomFbWritePtrs[ch]] = data[i];
            bloomFbWritePtrs[ch] = (bloomFbWritePtrs[ch] + 1) % fbLen;
        }
    }
}
```

### Why this is safe

The allpass stages are all-pass — no gain, no instability risk from the cascade itself. The feedback loop is bounded: the safety clamp at 0.65 prevents the feedback gain from approaching 1.0. The 1-frame latency (reading from the previous block's last written sample) breaks any instantaneous feedback loop that could cause numerical blow-up in the same block. The feedback path uses the already-diffused wet output, not the dry signal, so it cannot amplify transients.

---

---

## Feature 3 — Cloud Multi-LFO

### What it does

Applies a bank of 8 simultaneously running, independently-frequency LFO-modulated delay lines to the full wet signal. Unlike the existing single `chorusDelayLine` (one LFO, one depth), Cloud creates a sense of "living air" in the reverb tail — multiple simultaneous non-beating modulations that animate the wet signal without any individual modulation being obviously audible. The effect is the dense, atmospheric shimmer of the BigSky Cloud algorithm's multi-point modulation.

### How it works

8 delay lines (per channel), each with its own LFO frequency chosen on a geometric scale so that no two rates have a rational beat frequency (same principle as the FDN LFO spacing in `renderFDNTail`). Each line reads back from a short circular buffer at a position modulated by `±depthMs × sin(lfoPhase[i])`. The 8 outputs are averaged and blended with the unprocessed wet signal at a depth controlled by `cloudDepth`.

The key difference from the existing `chorusDelayLine`:
- **8 simultaneous LFOs vs 1** — each line at a different frequency, so the beating is aperiodic and never produces an obvious cyclic effect
- **Lower base depth per line** — since 8 lines sum, the total depth effect is comparable to the existing chorus at lower per-line depths, but without the single-LFO audible sweep
- **Rate scaling via `cloudRate`** — the entire geometric spread is scaled as a unit, preserving the spacing relationship between lines while allowing the user to move from "imperceptibly slow air movement" to "obvious chorus"

### Controls

| Parameter ID | Name | Range | Default | Effect |
|---|---|---|---|---|
| `cloudOn` | Cloud On | bool | false | Enables the Cloud bank |
| `cloudDepth` | Cloud Depth | 0–1 | 0.3 | Per-line modulation depth scale (0 = no mod, 1 = ±3 ms per line) |
| `cloudRate` | Cloud Rate | 0.1–4.0 | 1.0 | Multiplier on all 8 LFO rates (maintains geometric spacing) |
| `cloudSize` | Cloud Size | 1–40 ms | 5 ms | Base (unmodulated) delay of each line — sets the spatial character |

Base LFO rates: geometric series from 0.04 Hz to 0.35 Hz across 8 lines, i.e. `r_i = 0.04 × k^i` where `k = (0.35/0.04)^(1/7) ≈ 1.366`. At `cloudRate = 1.0` the fastest LFO completes one cycle every ~3 seconds — well below audible pitch modulation at normal reverb wet levels. At `cloudRate = 4.0` the fastest cycles at ~0.7 Hz, beginning to introduce an obvious pulsing quality.

`cloudSize` sets the centre delay around which each line is modulated. Short values (1–5 ms) produce a dense, intimate chorus texture — the lines sit in the early tail and their output blends closely with the source. Long values (20–40 ms) create a wide spatial smear — the lines form independent "rooms within the room" and the reverb tail takes on a cavernous, atmospheric quality. The delay buffer is allocated at `kCloudBufMs = cloudSize_max + kCloudBaseDepthMs + margin = 45 ms` to cover the full range without reallocation. The read position centre is `cloudSizeSamps + maxDepthSamps` instead of the previous fixed `maxDepthSamps + 1`.

### New state in `PluginProcessor.h`

```cpp
// Cloud: 8 parallel LFO-modulated delay lines per channel
static constexpr int kNumCloudLines = 8;
static constexpr float kCloudBaseDepthMs = 3.0f;   // max per-line modulation depth at cloudDepth=1
static constexpr float kCloudSizeMaxMs   = 40.0f;  // max cloudSize (UI range 1–40 ms)
static constexpr float kCloudBufMs       = 45.0f;  // buffer = cloudSizeMax + depthMax + margin

// Per-line buffers
std::array<std::array<std::vector<float>, kNumCloudLines>, 2> cloudBufs;   // [ch][line]
std::array<std::array<int,               kNumCloudLines>, 2> cloudWritePtrs;

// LFO state (global, not per-channel — L and R use same LFO phases, π offset for R)
std::array<float, kNumCloudLines> cloudLfoPhases {};   // 0..2π
std::array<float, kNumCloudLines> cloudLfoBaseRates {}; // radians/sample at 1× rate
```

### `prepareToPlay` additions

```cpp
// Cloud: 8 lines, geometric LFO rate spacing 0.04–0.35 Hz
const double rBase = 0.04, rTop = 0.35;
const double k = std::pow (rTop / rBase, 1.0 / (kNumCloudLines - 1));
int cloudBufSamps = (int)std::ceil (kCloudBufMs * sampleRate / 1000.0);
for (int i = 0; i < kNumCloudLines; ++i)
{
    double rHz = rBase * std::pow (k, i);
    cloudLfoBaseRates[i]  = (float)(2.0 * M_PI * rHz / sampleRate);
    cloudLfoPhases[i]     = (float)(i * 0.7854); // stagger initial phases by π/4
    for (int ch = 0; ch < 2; ++ch) {
        cloudBufs[ch][i].assign ((size_t)cloudBufSamps, 0.f);
        cloudWritePtrs[ch][i] = 0;
    }
}
```

### `processBlock` insertion — after Tail Chorus, before Output Gain

```cpp
bool cloudOn = apvts.getRawParameterValue (IDs::cloudOn)->load() > 0.5f;
if (cloudOn)
{
    float cloudDepth = apvts.getRawParameterValue (IDs::cloudDepth)->load();
    float cloudRate  = apvts.getRawParameterValue (IDs::cloudRate)->load();
    float cloudSizeMs = juce::jlimit (1.f, kCloudSizeMaxMs, apvts.getRawParameterValue (IDs::cloudSize)->load());

    float maxDepthSamps  = kCloudBaseDepthMs * cloudDepth * (float)currentSampleRate / 1000.f;
    float cloudSizeSamps = cloudSizeMs * (float)currentSampleRate / 1000.f;
    int   bufSamps       = (int)cloudBufs[0][0].size();

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance all LFO phases once per sample
        for (int line = 0; line < kNumCloudLines; ++line)
        {
            cloudLfoPhases[line] += cloudLfoBaseRates[line] * cloudRate;
            if (cloudLfoPhases[line] > 2.f * juce::MathConstants<float>::pi)
                cloudLfoPhases[line] -= 2.f * juce::MathConstants<float>::pi;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float input = buffer.getSample (ch, i);
            // R channel: π phase offset on all LFOs for stereo decorrelation
            float phaseOffset = (ch == 1) ? juce::MathConstants<float>::pi : 0.f;

            float lineSum = 0.f;
            for (int line = 0; line < kNumCloudLines; ++line)
            {
                auto& buf = cloudBufs[ch][line];
                int   wp  = cloudWritePtrs[ch][line];

                // Write current input sample
                buf[(size_t)wp] = input;

                // Modulated read position: centre is cloudSizeSamps, ± maxDepthSamps modulation
                float mod       = maxDepthSamps * std::sin (cloudLfoPhases[line] + phaseOffset);
                float baseDelay = cloudSizeSamps + maxDepthSamps + 1.f; // centre + mod headroom
                float readPos   = (float)wp - baseDelay - mod;
                int   rInt    = (int)std::floor (readPos);
                float rFrac   = readPos - (float)rInt;
                rInt          = ((rInt % bufSamps) + bufSamps) % bufSamps;
                int   rInt1   = (rInt + 1) % bufSamps;
                float sample  = buf[(size_t)rInt] * (1.f - rFrac) + buf[(size_t)rInt1] * rFrac;

                lineSum += sample;
                cloudWritePtrs[ch][line] = (wp + 1) % bufSamps;
            }
            // Average and blend: cloudDepth=0 → pass-through, =1 → fully modulated
            float cloudOut = lineSum / (float)kNumCloudLines;
            buffer.setSample (ch, i, input * (1.f - cloudDepth * 0.7f) + cloudOut * cloudDepth * 0.7f);
            // Note: the 0.7 scale prevents unity-gain summation of N lines from boosting the signal.
        }
    }
}
```

### Why this is safe

Each line reads from and writes to independent circular buffers — no feedback, no chance of instability. The delay buffers have no effect on DC or sub-audio content. The R-channel π phase offset on all LFOs provides stereo decorrelation of the Cloud effect without introducing asymmetric gain. The blend clamp means `cloudDepth = 0` is bit-identical to bypass.

---

---

## Feature 4 — Shimmer

### What it does

Adds one or two pitch-shifted harmonic voices derived from the wet reverb tail, with optional feedback routing that causes them to re-enter the reverb and build into a self-sustaining shifted harmonic shimmer. The classic Brian Eno/Daniel Lanois technique — a reverb that seems to grow upward in harmonic register.

### How it works

A **granular pitch shifter** reads from a continuously-written circular buffer at a rate proportional to the target pitch ratio (`2^(semitones/12)`). A 512-sample grain with a Hann window crossfade between two overlapping grain readers eliminates clicks at grain boundaries. Two independent voices (V1 and V2) can be set to different intervals.

The pitch-shifted output is then:
1. Blended into the wet signal at `shimmerAmount`
2. Attenuated by `shimmerFeedback` and injected into the Bloom feedback buffer (if Bloom is on) or directly into the input buffer before convolution (if Bloom is off)

The self-referential feedback — where the shifted output re-enters the reverb and is shifted again on the next pass — creates the characteristic angelic climbing quality. Each pass the shifted component becomes slightly more diffused by the convolver, smoothing out grain artifacts and creating a natural-feeling rise.

### Controls

| Parameter ID | Name | Range | Default | Effect |
|---|---|---|---|---|
| `shimmerOn` | Shimmer On | bool | false | Enables shimmer voices |
| `shimmerVoice1` | Voice 1 | 0–24 semitones | 12 (octave) | Pitch shift for voice 1 |
| `shimmerVoice2` | Voice 2 | 0–24 semitones | 19 (octave+5th) | Pitch shift for voice 2; set = Voice 1 for mono shimmer |
| `shimmerAmount` | Amount | 0–1 | 0.3 | Blend of shimmer voices into wet signal |
| `shimmerFeedback` | Feedback | 0–0.7 | 0.3 | How much shifts feeds back into reverb input |

`shimmerVoice1 = 12` (one octave up) is the standard. `shimmerVoice2 = 19` (octave + 5th) gives the bi-tonal shimmer. Setting both to `12` gives a single-voice thicker shimmer. Setting `shimmerVoice2 = 0` effectively makes it a single-voice shimmer (v2 produces unshifted signal). Setting them to `24` gives double-octave shimmer.

### New state in `PluginProcessor.h`

```cpp
// Shimmer: two-voice granular pitch shifter
static constexpr int kShimmerGrainLen   = 512;   // samples per grain
static constexpr int kShimmerBufSamples = 8192;  // ~170 ms at 48 kHz, must be >> kShimmerGrainLen

struct ShimmerVoice {
    std::vector<float> buf;         // circular write buffer
    int   writePtr   = 0;
    float readPtr    = 0.f;         // fractional — advances at pitchRatio per sample
    float grainPhase = 0.f;         // 0..1, drives Hann crossfade window
    float pitchRatio = 2.f;         // 2.0 = octave up
    // Two grain readers (A/B) for crossfade
    float readPtrB   = 0.f;
    float grainPhaseB= 0.5f;        // offset by half grain for seamless crossfade
};

std::array<std::array<ShimmerVoice, 2>, 2> shimmerVoices; // [voice][ch]

// Shimmer feedback buffer (feeds back into pre-convolution path)
std::array<std::vector<float>, 2> shimmerFbBufs;
std::array<int, 2>               shimmerFbWritePtrs { 0, 0 };
static constexpr int kShimmerFbMaxMs = 500;
```

### `prepareToPlay` additions

```cpp
// Shimmer: initialise grain buffers for 2 voices × 2 channels
for (int v = 0; v < 2; ++v)
    for (int ch = 0; ch < 2; ++ch) {
        shimmerVoices[v][ch].buf.assign (kShimmerBufSamples, 0.f);
        shimmerVoices[v][ch].writePtr    = 0;
        shimmerVoices[v][ch].readPtr     = 0.f;
        shimmerVoices[v][ch].grainPhase  = 0.f;
        shimmerVoices[v][ch].readPtrB    = (float)(kShimmerGrainLen / 2);
        shimmerVoices[v][ch].grainPhaseB = 0.5f;
        shimmerVoices[v][ch].pitchRatio  = 2.f;
    }

int fbSamps = (int)std::ceil (kShimmerFbMaxMs * sampleRate / 1000.0);
for (int ch = 0; ch < 2; ++ch) {
    shimmerFbBufs[ch].assign ((size_t)fbSamps, 0.f);
    shimmerFbWritePtrs[ch] = 0;
}
```

### `processBlock` — Shimmer has two insertion points

**Location A — before convolution (feedback inject):**

```cpp
bool shimmerOn = apvts.getRawParameterValue (IDs::shimmerOn)->load() > 0.5f;
if (shimmerOn)
{
    float fbAmt = apvts.getRawParameterValue (IDs::shimmerFeedback)->load();
    fbAmt = std::min (0.7f, fbAmt);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        int fbLen   = (int)shimmerFbBufs[ch].size();
        for (int i = 0; i < numSamples; ++i)
        {
            int rdPtr = (shimmerFbWritePtrs[ch] - 1 + fbLen) % fbLen;
            data[i] += shimmerFbBufs[ch][(size_t)rdPtr] * fbAmt;
        }
    }
}
```

**Location B — after Cloud, before Output Gain (grain processing + feedback store):**

```cpp
if (shimmerOn)
{
    float shimAmt = apvts.getRawParameterValue (IDs::shimmerAmount)->load();
    float fbAmt   = std::min (0.7f, apvts.getRawParameterValue (IDs::shimmerFeedback)->load());

    float v1Semi  = apvts.getRawParameterValue (IDs::shimmerVoice1)->load();
    float v2Semi  = apvts.getRawParameterValue (IDs::shimmerVoice2)->load();
    float ratio[2] = {
        std::pow (2.f, v1Semi / 12.f),
        std::pow (2.f, v2Semi / 12.f)
    };

    auto grainRead = [](ShimmerVoice& v, int bufLen) -> float
    {
        // Grain A
        int   r0A = ((int)v.readPtr % bufLen + bufLen) % bufLen;
        float fA  = v.readPtr - std::floor (v.readPtr);
        float sA  = v.buf[(size_t)r0A] * (1.f - fA) + v.buf[(size_t)((r0A + 1) % bufLen)] * fA;
        float wA  = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * v.grainPhase);

        // Grain B (offset by half grain)
        int   r0B = ((int)v.readPtrB % bufLen + bufLen) % bufLen;
        float fB  = v.readPtrB - std::floor (v.readPtrB);
        float sB  = v.buf[(size_t)r0B] * (1.f - fB) + v.buf[(size_t)((r0B + 1) % bufLen)] * fB;
        float wB  = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * v.grainPhaseB);

        return sA * wA + sB * wB;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float wet = buffer.getSample (ch, i);
            float shimOut = 0.f;

            for (int v = 0; v < 2; ++v)
            {
                ShimmerVoice& voice = shimmerVoices[v][ch];
                int bufLen = (int)voice.buf.size();

                // Write current wet sample
                voice.buf[(size_t)voice.writePtr] = wet;

                // Read grain-pitched output
                shimOut += grainRead (voice, bufLen) * 0.5f; // average 2 voices

                // Advance read pointers at pitch ratio
                voice.readPtr  += ratio[v];
                voice.readPtrB += ratio[v];

                // Wrap read pointers
                while (voice.readPtr  >= (float)bufLen) voice.readPtr  -= (float)bufLen;
                while (voice.readPtrB >= (float)bufLen) voice.readPtrB -= (float)bufLen;

                // Advance grain phases
                voice.grainPhase  += ratio[v] / (float)kShimmerGrainLen;
                voice.grainPhaseB += ratio[v] / (float)kShimmerGrainLen;
                if (voice.grainPhase  >= 1.f) { voice.grainPhase  -= 1.f; voice.readPtr  = (float)voice.writePtr; }
                if (voice.grainPhaseB >= 1.f) { voice.grainPhaseB -= 1.f; voice.readPtrB = (float)voice.writePtr; }

                voice.writePtr = (voice.writePtr + 1) % bufLen;
            }

            // Mix shimmer into wet
            float out = wet * (1.f - shimAmt * 0.5f) + shimOut * shimAmt;
            buffer.setSample (ch, i, out);

            // Store in feedback buffer for next block injection
            shimmerFbBufs[ch][(size_t)shimmerFbWritePtrs[ch]] = shimOut * fbAmt;
            shimmerFbWritePtrs[ch] = (shimmerFbWritePtrs[ch] + 1) % (int)shimmerFbBufs[ch].size();
        }
    }
}
```

### Notes on grain quality

The two-grain crossfade approach (Grain A and Grain B offset by half a grain length) produces clean output for octave shifts (ratio 2.0) and approximate output for non-octave intervals. Grain length of 512 samples at 48 kHz = ~10.7 ms. This is long enough to suppress grain click artifacts on full chord material. For cleaner non-octave shifting (e.g. major 3rds), the grain length could be extended to 1024 or a more sophisticated phase-vocoder approach used in a later version.

### Why the feedback is safe

The feedback amount is clamped at 0.70. The shimmer output that feeds back has already been through the convolver (which attenuates it), been grain-averaged across two windows (which further reduces peak energy), and then attenuated by `shimmerFeedback`. The total loop gain is well below 1.0 for all practical reverb IRs. If IR length → 0 (degenerate case with no IR loaded), both the convolver and shimmer output will be silent, so feedback is self-limiting.

---

---

## Summary: parameters to add to `createParameterLayout()`

```cpp
// Plate
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::plateOn,       "Plate On",        false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateDensity,  "Plate Density",   0.f, 1.f, 0.5f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateColour,   "Plate Colour",    0.f, 1.f, 0.5f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::plateSize,     "Plate Size",      0.5f, 2.f, 1.f));

// Bloom
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::bloomOn,       "Bloom On",        false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomDiffusion,"Bloom Diffusion", 0.f, 1.f, 0.5f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomFeedback, "Bloom Feedback",  0.f, 0.65f, 0.25f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomTime,     "Bloom Time",      50.f, 500.f, 200.f));

// Cloud
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::cloudOn,       "Cloud On",        false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudDepth,    "Cloud Depth",     0.f, 1.f, 0.3f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudRate,     "Cloud Rate",      0.1f, 4.f, 1.f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudSize,     "Cloud Size",      1.f, 40.f, 5.f));

// Shimmer
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::shimmerOn,     "Shimmer On",      false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimmerVoice1, "Shimmer Voice 1", 0.f, 24.f, 12.f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimmerVoice2, "Shimmer Voice 2", 0.f, 24.f, 19.f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimmerAmount, "Shimmer Amount",  0.f, 1.f, 0.3f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimmerFeedback,"Shimmer Feedback",0.f, 0.7f, 0.3f));
```

And in `namespace IDs`:
```cpp
static const juce::String plateOn       { "plateOn" };
static const juce::String plateDensity  { "plateDensity" };
static const juce::String plateColour   { "plateColour" };
static const juce::String plateSize     { "plateSize" };
static const juce::String bloomOn       { "bloomOn" };
static const juce::String bloomDiffusion{ "bloomDiffusion" };
static const juce::String bloomFeedback { "bloomFeedback" };
static const juce::String bloomTime     { "bloomTime" };
static const juce::String cloudOn       { "cloudOn" };
static const juce::String cloudDepth    { "cloudDepth" };
static const juce::String cloudRate     { "cloudRate" };
static const juce::String cloudSize     { "cloudSize" };
static const juce::String shimmerOn     { "shimmerOn" };
static const juce::String shimmerVoice1 { "shimmerVoice1" };
static const juce::String shimmerVoice2 { "shimmerVoice2" };
static const juce::String shimmerAmount { "shimmerAmount" };
static const juce::String shimmerFeedback{ "shimmerFeedback" };
```

---

## Summary: `prepareToPlay` changes

All new state needs to be reset in `prepareToPlay`. The reset order should be:
1. Resize and zero all new allpass buffers (Plate stages, Bloom stages)
2. Reset all new circular delay buffers (Bloom feedback, Cloud lines, Shimmer grain buffers, Shimmer feedback)
3. Reset all LFO phase accumulators (Cloud line phases)
4. Reset all pointer variables to 0
5. Recompute any rate/coefficient values that depend on `sampleRate`

---

## Memory footprint at 48 kHz

| Feature | State | Approx. bytes (stereo) |
|---|---|---|
| Plate | 6 allpass stages × 2 ch, buffers at 2× base delays (48–1382 samps) | ~28 KB |
| Bloom | 4 allpass stages × 2 ch (1871–14387 samps) + 500 ms fb buf | ~350 KB |
| Cloud | 8 lines × 2 ch × 45 ms buf | ~138 KB |
| Shimmer | 2 voices × 2 ch × 8192 samps + 500 ms fb | ~100 KB |
| **Total** | | **~420 KB** |

All well within acceptable limits for a JUCE plugin.

---

## CPU overhead (all four on, worst case)

- **Plate:** 6 multiply-adds per sample per channel — negligible
- **Bloom:** 4 multiply-adds per sample (cascade) + 1 circular read/write (feedback) — negligible
- **Cloud:** 8 lines × (1 interpolated read + 1 write + 1 sin) per sample × 2 ch — ~160 multiply-adds/sample. The `sin()` call is the expensive part; could be replaced with a Bhaskara approximation or pre-computed LUT if CPU headroom is tight.
- **Shimmer:** 2 voices × 2 grains × (interpolated read + window multiply) per sample × 2 ch — ~32 multiply-adds/sample. The `std::cos` for the Hann window runs once per grain boundary, not every sample.
- **Total overhead:** Dominated by Cloud's 8×sin calls. On a modern Mac this is comfortably under 1% CPU at typical block sizes (128–512 samples).

---

## Implementation order (suggested)

1. **Cloud first** — the most self-contained, purely post-convolution, no feedback risk. Quickest to hear results.
2. **Plate** — straightforward pre-convolution input diffuser, no feedback. Low risk.
3. **Shimmer** — feedback but bounded, grain pitch is well-understood. Needs careful testing of feedback stability across IRs.
4. **Bloom** — the most architecturally complex (two insertion points, interaction with Shimmer's feedback if both are on). Implement last once the others are working.

---

## Interactions between features

All four can be active simultaneously. Significant interaction cases:

- **Bloom + Shimmer both on:** The Shimmer feedback bypasses the Bloom feedback path and injects directly before convolution. Both paths add to the pre-convolution input independently. This is intentional — the shimmer feeds back harmonically, the bloom feeds back diffusely. Net effect: shimmer grows harmonically while bloom adds to the ambient swell.
- **Plate + Bloom both on:** Both pre-diffuse the input. Their allpass cascades run in series on the input signal (Plate runs first, then Bloom). This doubles the allpass diffusion depth, which at high settings can create a very smooth and fast onset. A useful combination for ambient work.
- **Cloud + Shimmer both on:** Cloud animates the wet tail before Shimmer pitch-shifts it. The pitch shifter therefore shifts an already-modulated tail, producing a shimmer that has built-in movement. This creates a lush, complex shimmer character. No conflict — both are purely post-convolution, non-feeding-back at their interaction point.
- **All four on:** Valid and stable. The feedback paths (Bloom and Shimmer) are both bounded and independent. At extreme settings (bloom feedback 0.6 + shimmer feedback 0.6 + long IR RT60 > 5 s) the combined loop can produce very long sustained tones — this is musically intentional but should be noted in the user manual as a "drone/sustain" use case.
