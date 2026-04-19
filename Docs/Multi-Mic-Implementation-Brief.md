# P!NG — Multi-Mic IR Synthesis: Implementation Brief

**Branch:** `feature/multi-mic-paths` (branched from `main` after v2.5.0)  
**Codebase:** JUCE AU+VST3 plugin, macOS. See `CLAUDE.md` for full project context.

---

## What This Feature Is

The IR Synth currently produces one true-stereo (4-channel) IR from a single main mic pair. This feature adds three additional independent mic paths — **DIRECT**, **OUTRIG**, and **AMBIENT** — each with its own synthesised 4-channel IR, feeding a small summing mixer before the existing post-convolution chain. The result is an Altiverb-style multi-mic setup: users can blend a dry direct signal, a main stereo pair, outrigger mics, and ambient room mics, each independently faded, panned, muted, and soloed.

### The four paths

| # | UI Label | Nature | Switchable | IR type |
|---|---|---|---|---|
| 1 | DIRECT | Order-0 image-source only (no wall bounces). Uses same mic positions as MAIN. Frequency-dependent polar colouration applied. Additive on top of the MAIN full IR. | Yes | ~100 sample spike per speaker-mic pair. Negligible convolution cost. |
| 2 | MAIN | The existing main stereo mic pair. Full ER + Tail synthesis. Currently the only path. | Yes | Existing 4-channel full-length IR — unchanged. |
| 3 | OUTRIG | Wider stereo outrigger pair, independently placeable on the FloorPlan, same height as MAIN by default. Full ER + Tail. | Yes | New 4-channel full-length IR. |
| 4 | AMBIENT | Higher, further-back ambient pair, independently placeable. Full ER + Tail. | Yes | New 4-channel full-length IR. |

**Everything downstream of the convolver is unchanged.** EQ, stereo decorrelation, LFO mod, Width, Tail Chorus, Cloud, Shimmer, Bloom Volume, Dry/Wet blend — all operate on the summed wet bus as they do today.

---

## Resolved Design Decisions

Read these before touching any code:

1. **Single combined fader per strip.** No separate ER/Tail faders on OUTRIG or AMBIENT. The MAIN path's existing `erLevel`/`tailLevel` params continue to work internally and are applied before MAIN's strip fader.

2. **All four paths can be switched off independently**, including MAIN. No always-on paths.

3. **DIRECT is tied to MAIN mic positions.** Outrig and Ambient full IRs already contain their own direct arrivals naturally. DIRECT is additive: turning it up adds an extra controllable dose of the direct arrival above what the MAIN full IR already provides (like Altiverb's direct slider).

4. **Per-path direct for OUTRIG/AMBIENT is a future enhancement**, not part of this implementation.

5. **High-pass per strip:** Fixed 110 Hz 2nd-order Butterworth toggle per strip. Default off on DIRECT and MAIN; default on for OUTRIG and AMBIENT.

6. **Parallel synthesis:** All enabled paths synthesise in parallel threads. Target: same wall-clock time as current single-path synthesis.

7. **Four separate WAV files** per synthesised IR: `<name>.wav` (MAIN, unchanged), `<name>_direct.wav`, `<name>_outrig.wav`, `<name>_ambient.wav`. One `.ping` sidecar covers all.

8. **Strip naming: DIRECT / MAIN / OUTRIG / AMBIENT.** Not "TREE". A Decca Tree synthesis mode is a future feature. Parameter IDs use this same naming (`mainGain`, `outrigOn`, etc.).

9. **Default positions for OUTRIG:** `lx=0.15, ly=0.80, rx=0.85, ry=0.80`, same height as MAIN (3 m). Default OUTRIG pattern: `"cardioid (LDC)"`. **Default positions for AMBIENT:** `lx=0.20, ly=0.95, rx=0.80, ry=0.95`, height 6 m. Default AMBIENT pattern: `"omni"`.

10. **Backward compatibility:** Old presets/factory IRs have only the MAIN `.wav`. Missing `_outrig.wav` / `_ambient.wav` / `_direct.wav` → those paths default to off, unloaded. Old `.ping` sidecars missing new fields → defaults apply.

---

## Existing Architecture — What You Need to Know

### Convolver setup (PluginProcessor.h)

Currently 8 JUCE `juce::dsp::Convolution` objects for the MAIN true-stereo path:
```cpp
juce::dsp::Convolution tsErConvLL, tsErConvRL, tsErConvLR, tsErConvRR;    // ER
juce::dsp::Convolution tsTailConvLL, tsTailConvRL, tsTailConvLR, tsTailConvRR; // Tail
```
Plus a `tailConvolver` (stereo, used only for the WaveformComponent display).

The true-stereo convolution works as: `lOut = LL(lIn) + RL(rIn)`, `rOut = LR(lIn) + RR(rIn)`.

### IR loading

`loadIRFromBuffer(buffer, sampleRate, fromSynth, deferConvolverLoad)` in `PluginProcessor.cpp`:
- Applies Reverse, Stretch, Decay envelope, silence trim, 4-channel expansion
- Splits at 80 ms into ER and Tail halves
- Calls `loadImpulseResponse` on all 8 convolvers

`loadIRFromFile()` → `loadIRFromBuffer()`. `reloadSynthIR()` re-calls `loadIRFromBuffer` with the stored `rawSynthBuffer`.

### IR load fade guard

```cpp
std::atomic<int> irLoadFadeBlocksRemaining { 0 };
static constexpr int kIRLoadFadeBlocks = 64;
```
Armed in `loadIRFromBuffer` to prevent crossfade audible glitch while JUCE background threads swap in new IRs. **This must cover all convolvers.** With up to 28 convolvers total, increase to a sample-based counter (see Phase 2 risk note).

### IRSynthParams / IRSynthResult (IRSynthEngine.h)

`IRSynthParams` holds all room + placement params. `IRSynthResult` holds `iLL, iRL, iLR, iRR` vectors plus `rt60[]`, `irLen`, `sampleRate`.

`IRSynthEngine::synthIR(p, progressCallback)` is the main entry point. It is fully stateless/re-entrant — safe to call from multiple threads simultaneously with different params.

### FloorPlanComponent

`TransducerState` holds `cx[4], cy[4], angle[4]` — indices 0/1 are speakers, 2/3 are MAIN mics. The FloorPlan has a single `onPlacementChanged` callback.

### Peak meters

```cpp
std::atomic<float> outputLevelPeakL, outputLevelPeakR;
std::atomic<float> inputLevelPeakL,  inputLevelPeakR;
std::atomic<float> erLevelPeakL,     erLevelPeakR;
std::atomic<float> tailLevelPeakL,   tailLevelPeakR;
```
Updated in `processBlock`, read by the GUI timer in `OutputLevelMeter`.

### Key invariants to preserve

- `loadIRFromBuffer` / `loadIRFromFile` are **message-thread only**. Never call from the audio thread.
- `setStateInformation` uses the `isRestoringState` + `callAsync` pattern to prevent triple IR loading.
- `audioEnginePrepared` guards against `setStateInformation → prepareToPlay` data races.
- `getStateInformation` must strip old `irSynthParams` / `synthIR` children before adding fresh ones (prevents exponential growth).
- `irCombo.clear()` must always use `juce::dontSendNotification`.
- All level-scaling parameters use `SmoothedValue` (20 ms ramp).

---

## Implementation Phases

### Phase 1 — IRSynthEngine: extended params, result, and parallel synthesis

**Files:** `Source/IRSynthEngine.h`, `Source/IRSynthEngine.cpp`

#### 1a. Extend `IRSynthParams`

Add after the existing placement fields:

```cpp
// ── Outrigger mics ───────────────────────────────────────────────────────
bool   outrig_enabled  = false;
double outrig_lx       = 0.15;  double outrig_ly = 0.80;
double outrig_rx       = 0.85;  double outrig_ry = 0.80;
double outrig_langle   = -2.35619449019;  // same default facing as MAIN
double outrig_rangle   = -0.785398163397;
double outrig_height   = 3.0;             // metres above floor
std::string outrig_pattern = "cardioid (LDC)";

// ── Ambient mics ─────────────────────────────────────────────────────────
bool   ambient_enabled  = false;
double ambient_lx       = 0.20;  double ambient_ly = 0.95;
double ambient_rx       = 0.80;  double ambient_ry = 0.95;
double ambient_langle   = -2.35619449019;
double ambient_rangle   = -0.785398163397;
double ambient_height   = 6.0;
std::string ambient_pattern = "omni";

// ── Direct path ───────────────────────────────────────────────────────────
// Uses same mic positions/pattern as the MAIN pair (receiver_lx/ly etc. + mic_pattern).
// No extra position fields needed.
bool direct_enabled = false;
```

#### 1b. Extend `IRSynthResult`

Add a `MicIRChannels` struct and extra path results. Keep existing `iLL/iRL/iLR/iRR` intact for backward compat:

```cpp
struct MicIRChannels
{
    std::vector<double> LL, RL, LR, RR;
    int irLen = 0;
    bool synthesised = false;
};

struct IRSynthResult
{
    // MAIN path — existing fields unchanged
    std::vector<double> iLL, iRL, iLR, iRR;
    std::vector<double> rt60;  // 8 bands
    int   irLen      = 0;
    int   sampleRate = 0;
    bool  success    = false;
    std::string errorMessage;

    // Additional paths — empty/synthesised=false if not requested
    MicIRChannels direct;
    MicIRChannels outrig;
    MicIRChannels ambient;
};
```

#### 1c. Refactor `synthIR()` to support parallel synthesis

The existing `synthIR` runs the full pipeline for the MAIN path. Refactor by extracting a `synthOnePath()` helper that takes the receiver geometry as explicit parameters (rather than always reading from `p.receiver_lx` etc.), then call it from parallel threads.

The direct path uses `calcRefs` with `maxOrder = 0` only — add a `maxOrder` parameter to `calcRefs` (or a separate `calcDirectRefs` that filters to order 0). The resulting Ref list has at most 4 entries (one per speaker-mic pair). `renderFDNTail` is skipped entirely for the direct path.

**Parallel dispatch pattern:**

```cpp
IRSynthResult IRSynthEngine::synthIR(const IRSynthParams& p, IRSynthProgressFn cb)
{
    // Shared progress: each path reports into its own atomic fraction.
    // MAIN gets ~60% of the bar (longest), OUTRIG/AMBIENT ~18% each, DIRECT negligible.
    std::atomic<double> mainProg{0}, outrigProg{0}, ambientProg{0};

    auto updateProgress = [&](const std::string& msg) {
        double total = mainProg.load() * 0.60
                     + outrigProg.load() * 0.18
                     + ambientProg.load() * 0.18
                     + 0.04; // constant for direct
        cb(std::min(total, 1.0), msg);
    };

    // Launch MAIN (always runs)
    auto mainFuture = std::async(std::launch::async, [&]{
        return synthMainPath(p, [&](double f, const std::string& m){
            mainProg.store(f); updateProgress(m);
        });
    });

    // Launch DIRECT (very fast — order 0 only, no FDN)
    std::future<MicIRChannels> directFuture;
    if (p.direct_enabled)
        directFuture = std::async(std::launch::async, [&]{
            return synthDirectPath(p);
        });

    // Launch OUTRIG
    std::future<MicIRChannels> outrigFuture;
    if (p.outrig_enabled)
        outrigFuture = std::async(std::launch::async, [&]{
            return synthExtraPath(p, OutrigGeom{p}, [&](double f, const std::string& m){
                outrigProg.store(f); updateProgress(m);
            });
        });

    // Launch AMBIENT
    std::future<MicIRChannels> ambientFuture;
    if (p.ambient_enabled)
        ambientFuture = std::async(std::launch::async, [&]{
            return synthExtraPath(p, AmbientGeom{p}, [&](double f, const std::string& m){
                ambientProg.store(f); updateProgress(m);
            });
        });

    // Collect
    IRSynthResult result = mainFuture.get();  // populates iLL/iRL/iLR/iRR + rt60 etc.
    if (directFuture.valid())  result.direct  = directFuture.get();
    if (outrigFuture.valid())  result.outrig  = outrigFuture.get();
    if (ambientFuture.valid()) result.ambient = ambientFuture.get();

    cb(1.0, "Done");
    return result;
}
```

**`synthDirectPath`** runs `calcRefs` with `maxOrder=0`, then `renderCh` for the 4 cross-paths (LL/RL/LR/RR), skips `renderFDNTail` and the ER/FDN blend. The resulting vectors are very short (~direct arrival sample index + a few samples of impulse response). No normalisation, no silence trim needed — they are inherently short.

**`synthExtraPath`** runs the full pipeline (same as `synthMainPath`) but substitutes the given receiver geometry (lx/ly/rx/ry/langle/rangle/height/pattern) from the outrig or ambient fields.

#### 1d. `makeWav` — call once per synthesised path

`makeWav` is unchanged. It will be called up to 4 times in `IRSynthComponent` after synthesis, with the appropriate channel vectors and filename suffix.

---

### Phase 2 — PluginProcessor: new convolvers, APVTS params, and processBlock mixer

**Files:** `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`

#### 2a. New APVTS parameters

Add to `createParameterLayout()`. All gain params use `SmoothedValue` (20 ms). All booleans are `AudioParameterBool`.

```
// MAIN path mixer additions (gain/pan/mute/solo added to existing path)
"mainGain"      NormalisableRange -48..+6 dB, default 0.0f
"mainPan"       NormalisableRange -1..+1, default 0.0f
"mainMute"      bool, default false
"mainSolo"      bool, default false
"mainHPOn"      bool, default false   // 110 Hz high-pass

// DIRECT path
"directOn"      bool, default false
"directGain"    NormalisableRange -48..+6 dB, default 0.0f
"directPan"     NormalisableRange -1..+1, default 0.0f
"directMute"    bool, default false
"directSolo"    bool, default false
"directHPOn"    bool, default false

// OUTRIG path
"outrigOn"      bool, default false
"outrigGain"    NormalisableRange -48..+6 dB, default 0.0f
"outrigPan"     NormalisableRange -1..+1, default 0.0f
"outrigMute"    bool, default false
"outrigSolo"    bool, default false
"outrigHPOn"    bool, default true    // 110 Hz HP on by default

// AMBIENT path
"ambientOn"     bool, default false
"ambientGain"   NormalisableRange -48..+6 dB, default 0.0f
"ambientPan"    NormalisableRange -1..+1, default 0.0f
"ambientMute"   bool, default false
"ambientSolo"   bool, default false
"ambientHPOn"   bool, default true    // 110 Hz HP on by default
```

#### 2b. New convolvers (PluginProcessor.h)

```cpp
// DIRECT path — 4 convolvers, no ER/Tail split (IR is too short to split)
juce::dsp::Convolution tsDirectConvLL, tsDirectConvRL, tsDirectConvLR, tsDirectConvRR;

// OUTRIG path — 8 convolvers (ER + Tail × true stereo)
juce::dsp::Convolution tsOutrigErConvLL,   tsOutrigErConvRL,   tsOutrigErConvLR,   tsOutrigErConvRR;
juce::dsp::Convolution tsOutrigTailConvLL, tsOutrigTailConvRL, tsOutrigTailConvLR, tsOutrigTailConvRR;

// AMBIENT path — 8 convolvers (ER + Tail × true stereo)
juce::dsp::Convolution tsAmbErConvLL,   tsAmbErConvRL,   tsAmbErConvLR,   tsAmbErConvRR;
juce::dsp::Convolution tsAmbTailConvLL, tsAmbTailConvRL, tsAmbTailConvLR, tsAmbTailConvRR;
```

Add to `prepareToPlay()` and `releaseResources()` — same prepare/reset pattern as the existing `tsEr*` / `tsTail*` convolvers.

#### 2c. Per-path SmoothedValues (PluginProcessor.h)

```cpp
// Mixer gain smoothing (20 ms ramp)
juce::SmoothedValue<float> mainGainSmooth, directGainSmooth, outrigGainSmooth, ambientGainSmooth;
// Pan smoothing
juce::SmoothedValue<float> mainPanSmooth, directPanSmooth, outrigPanSmooth, ambientPanSmooth;
```

Initialised in `prepareToPlay()` with `reset(sampleRate, 0.02)`.

#### 2d. Per-path peak meters (PluginProcessor.h)

```cpp
std::atomic<float> directPeakL  { 0.f }, directPeakR  { 0.f };
std::atomic<float> outrigPeakL  { 0.f }, outrigPeakR  { 0.f };
std::atomic<float> ambientPeakL { 0.f }, ambientPeakR { 0.f };
// MAIN path reuses existing erLevelPeakL/R and tailLevelPeakL/R for its ER/Tail sub-meters.
// Add a combined post-fader main peak:
std::atomic<float> mainPeakL    { 0.f }, mainPeakR    { 0.f };
```

#### 2e. Per-path high-pass filter state (PluginProcessor.h)

110 Hz 2nd-order Butterworth high-pass, 2 channels per path:

```cpp
// HP filter coefficients (computed once in prepareToPlay when SR is known)
struct HP2ndOrder {
    float b0=1,b1=0,b2=0,a1=0,a2=0;
    float x1[2]={},x2[2]={},y1[2]={},y2[2]={};
    void prepare(double fc, double sr);
    float process(float x, int ch);
};
HP2ndOrder mainHP, directHP, outrigHP, ambientHP;
```

`prepare()` computes standard Butterworth 2nd-order HP coefficients for 110 Hz at the current sample rate. Called from `prepareToPlay()`. `process()` runs the biquad per sample.

#### 2f. IR loading for extra paths

Add a `MicPath` enum and extend `loadIRFromBuffer`:

```cpp
enum class MicPath { Main, Direct, Outrig, Ambient };

void loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate,
                       bool fromSynth = false,
                       bool deferConvolverLoad = false,
                       MicPath path = MicPath::Main);
```

For `MicPath::Main` — existing logic unchanged.  
For `MicPath::Direct` — load the (very short) buffer into `tsDirectConvLL/RL/LR/RR`. No ER/Tail split — the entire buffer goes into all four. No silence trim, no decay envelope (direct path IRs are too short for any of that to be meaningful).  
For `MicPath::Outrig` / `MicPath::Ambient` — run the full existing pipeline (Reverse, Stretch, Decay, silence trim, 4-ch expansion, 80 ms ER/Tail split) against the respective convolver sets.

Add convenience wrapper:
```cpp
void loadMicPathFromFile (const juce::File& baseIRFile, MicPath path);
// Derives the suffix-appended filename from baseIRFile and calls loadIRFromFile → loadIRFromBuffer.
// Returns silently if the file does not exist.
```

Filename derivation:
```cpp
auto stem = baseIRFile.getFileNameWithoutExtension();
auto ext  = baseIRFile.getFileExtension();
auto dir  = baseIRFile.getParentDirectory();
// MicPath::Direct  → stem + "_direct" + ext
// MicPath::Outrig  → stem + "_outrig" + ext
// MicPath::Ambient → stem + "_ambient" + ext
```

#### 2g. `loadSelectedIR()` update

After loading the MAIN IR (existing logic), call:
```cpp
loadMicPathFromFile (selectedIRFile, MicPath::Direct);
loadMicPathFromFile (selectedIRFile, MicPath::Outrig);
loadMicPathFromFile (selectedIRFile, MicPath::Ambient);
```
These return silently if the files don't exist (old presets / factory IRs with only the MAIN WAV).

#### 2h. `irLoadFadeBlocksRemaining` — convert to sample-based

The current block-count approach (64 blocks) is too short for 28 convolvers at small buffer sizes. Replace with sample-based:

```cpp
// Replace:
std::atomic<int> irLoadFadeBlocksRemaining { 0 };
static constexpr int kIRLoadFadeBlocks = 64;

// With:
std::atomic<int> irLoadFadeSamplesRemaining { 0 };
static constexpr int kIRLoadFadeSamples = 96000; // 2 seconds at 48 kHz
```

Update `processBlock` to decrement by `numSamples` and compute `fadeIn = 1.f - (float)remaining / (float)kIRLoadFadeSamples`. Set to `kIRLoadFadeSamples` in `loadIRFromBuffer` before the first `loadImpulseResponse` call (regardless of which path is loading).

#### 2i. processBlock — mixer insertion

After the existing MAIN convolution block (where `lEr`, `rEr`, `lTail`, `rTail` are computed and `erLevel`/`tailLevel` applied), add the multi-path mixer. The insertion point is **after existing ER/Tail crossfeed** and **before EQ**.

```cpp
// ── MAIN path post-fader ─────────────────────────────────────────────────
// Apply mainHP (if mainHPOn), then mainGainSmooth + constant-power pan.
// Compute mainPeakL/R from post-fader samples.

// ── DIRECT path ───────────────────────────────────────────────────────────
if (directOn)
{
    // Run tsDirectConvLL/RL/LR/RR (same 4-convolver pattern as tsEr* block)
    // → directBufL, directBufR
    // Apply directHP (if directHPOn), directGainSmooth, directPan.
    // Compute directPeakL/R.
}

// ── OUTRIG path ───────────────────────────────────────────────────────────
if (outrigOn)
{
    // Run tsOutrigErConvLL/RL/LR/RR → erL, erR
    // Run tsOutrigTailConvLL/RL/LR/RR → tailL, tailR
    // Sum with outrigErLevel (TODO: decide if per-path ER/Tail params needed;
    //   current decision: no — single combined fader, so just sum directly)
    // Apply outrigHP (if outrigHPOn), outrigGainSmooth, outrigPan.
    // Compute outrigPeakL/R.
    // Note: no crossfeed on outrig/ambient — their stereo comes from placement geometry.
}

// ── AMBIENT path ──────────────────────────────────────────────────────────
// Same pattern as OUTRIG → ambientBufL/R → ambientHP → ambientGain → pan.

// ── Solo logic ────────────────────────────────────────────────────────────
bool anySolo = mainSolo || directSolo || outrigSolo || ambientSolo;
bool mainContributes   = !mainMute   && (!anySolo || mainSolo);
bool directContributes = directOn  && !directMute  && (!anySolo || directSolo);
bool outrigContributes = outrigOn  && !outrigMute  && (!anySolo || outrigSolo);
bool ambientContributes= ambientOn && !ambientMute && (!anySolo || ambientSolo);

// ── Sum to wet bus ────────────────────────────────────────────────────────
// buffer = sum of contributing paths (replace existing buffer content,
//          since MAIN currently writes directly to buffer)
// Then continue with EQ, decorrelation, LFO, Width, Tail Chorus — UNCHANGED.
```

**Pan law:** constant-power panning. For pan value `p` in [-1, +1]:
```cpp
float panL = std::cos((p + 1.f) * juce::MathConstants<float>::pi / 4.f);
float panR = std::sin((p + 1.f) * juce::MathConstants<float>::pi / 4.f);
```

**Note on MAIN path:** Currently the MAIN convolution writes directly into `buffer`. After this change, MAIN writes into a temporary buffer (`mainBufL/R`), and the final sum writes into `buffer`. This is the most significant restructure in processBlock — be careful to preserve the existing ER/Tail crossfeed, `erLevel`/`tailLevel` SmoothedValues, and `trueStereoWetGain` behaviour exactly.

#### 2j. `rawSynthBuffer` — per-path variants

```cpp
juce::AudioBuffer<float> rawSynthBuffer;        // existing MAIN path (unchanged)
juce::AudioBuffer<float> rawSynthDirectBuffer;
juce::AudioBuffer<float> rawSynthOutrigBuffer;
juce::AudioBuffer<float> rawSynthAmbientBuffer;

double rawSynthSampleRate = 48000.0;  // shared — all paths have the same SR
```

`reloadSynthIR()` calls `loadIRFromBuffer` for each non-empty raw buffer:
```cpp
void PingProcessor::reloadSynthIR()
{
    if (rawSynthBuffer.getNumSamples() > 0)
        loadIRFromBuffer(rawSynthBuffer, rawSynthSampleRate, true,  false, MicPath::Main);
    if (rawSynthDirectBuffer.getNumSamples() > 0)
        loadIRFromBuffer(rawSynthDirectBuffer, rawSynthSampleRate, true, false, MicPath::Direct);
    if (rawSynthOutrigBuffer.getNumSamples() > 0)
        loadIRFromBuffer(rawSynthOutrigBuffer, rawSynthSampleRate, true, false, MicPath::Outrig);
    if (rawSynthAmbientBuffer.getNumSamples() > 0)
        loadIRFromBuffer(rawSynthAmbientBuffer, rawSynthSampleRate, true, false, MicPath::Ambient);
}
```

#### 2k. State persistence

**`getStateInformation`:** Only embed `rawSynthBuffer` (MAIN) as base64 in state — same as now. The other three paths' WAV files are always on disk (saved alongside the MAIN WAV) and re-loaded from file on restore. Embedding all four large buffers in state risks exceeding DAW size limits.

Add the new IRSynthParams fields (outrig/ambient positions and enable flags) to the `irSynthParams` XML child element. Strip and re-add as always (the existing duplicate-strip pattern — do not skip this).

**`setStateInformation`:** Read back new XML attributes with defaults matching `IRSynthParams` defaults. After loading the MAIN IR (existing logic), call `loadMicPathFromFile` for the other three paths.

---

### Phase 3 — FloorPlanComponent: additional mic drag points

**Files:** `Source/FloorPlanComponent.h`, `Source/FloorPlanComponent.cpp`

#### 3a. Extend `TransducerState`

```cpp
struct TransducerState
{
    // indices 0/1 = speakers, 2/3 = MAIN mics (existing)
    double cx[8]    = { 0.25, 0.75, 0.35, 0.65,  0.15, 0.85,  0.20, 0.80 };
    double cy[8]    = { 0.50, 0.50, 0.80, 0.80,  0.80, 0.80,  0.95, 0.95 };
    double angle[8] = {
        1.57079632679, 1.57079632679,        // speakers: down
        -2.35619449019, -0.785398163397,     // MAIN mics: up-left, up-right
        -2.35619449019, -0.785398163397,     // OUTRIG mics
        -2.35619449019, -0.785398163397      // AMBIENT mics
    };
    // indices 4/5 = OUTRIG, 6/7 = AMBIENT
};
```

`dragIndex` range extended to 0..7. Indices 4/5 are active only when `outrig_enabled`, indices 6/7 only when `ambient_enabled`.

#### 3b. Callbacks

Replace single `onPlacementChanged` with per-group callbacks:
```cpp
std::function<void()> onMainPlacementChanged;
std::function<void()> onOutrigPlacementChanged;
std::function<void()> onAmbientPlacementChanged;
// Keep onPlacementChanged as a catch-all that calls all three, for existing wiring in IRSynthComponent.
```

Add enable flags so the FloorPlan knows which extra points to draw:
```cpp
bool outrigVisible  = false;
bool ambientVisible = false;
```

#### 3c. Drawing

- **Speakers (0/1):** existing white/grey style.
- **MAIN mics (2/3):** existing icy blue `0xff8cd6ef`.
- **OUTRIG mics (4/5):** soft purple `0xffb09aff`. When `outrigVisible = false`, draw as hollow/dim.
- **AMBIENT mics (6/7):** amber `0xffcfa95e`. When `ambientVisible = false`, draw as hollow/dim.

Label each non-speaker pair with a small letter: "M", "O", "A" next to the dot.

Height is not shown in the 2D FloorPlan — it is set via sliders in IRSynthComponent (see Phase 4).

---

### Phase 4 — IRSynthComponent: outrig/ambient controls

**Files:** `Source/IRSynthComponent.h`, `Source/IRSynthComponent.cpp`

#### 4a. New controls in left column

Below the existing mic pattern and angle readout section, add three collapsible sections:

**Direct path section:**
- Header label "Direct path" + power-button toggle (`directEnabledToggle` → sets `p.direct_enabled`)
- No position params (uses MAIN mic positions)
- No additional controls

**Outrigger section:**
- Header "Outrigger mics" + power-button toggle (`outrigEnabledToggle` → sets `p.outrig_enabled`)
- When enabled: `outrig_pattern` combo (same options as main mic combo)
- `outrig_height` slider (0.5 – 8.0 m, default 3.0, label "HEIGHT (m)")
- The outrig L/R x/y positions come from the FloorPlan — no numeric sliders needed
- Pattern combo and height slider grey out when toggle is off

**Ambient section:**
- Header "Ambient mics" + power-button toggle (`ambientEnabledToggle` → sets `p.ambient_enabled`)
- `ambient_pattern` combo
- `ambient_height` slider (1.0 – 12.0 m, default 6.0)
- Same greying behaviour

All three toggles call `onParamModifiedFn()` when changed (dirty the IR, same as all other synth params).

#### 4b. FloorPlan sync

When `outrig_enabled` or `ambient_enabled` changes, call `floorPlan.outrigVisible = outrig_enabled` (etc.) and `floorPlan.repaint()`. The FloorPlan callbacks `onOutrigPlacementChanged` / `onAmbientPlacementChanged` write back to `p.outrig_lx` etc. and call `onParamModifiedFn()`.

#### 4c. `getParams()` / `setParams()` 

Extend `getParams` to fill the new fields from the controls. Extend `setParams` to restore them. Apply the same `suppressingParamNotifications` guard + 3-layer async guard as the existing implementation (see CLAUDE.md "IRSynthComponent::setParams suppresses dirty notifications").

#### 4d. WAV saving after synthesis

In `IRSynthComponent`'s synthesis completion handler, call `makeWav` for each synthesised path and write the files:

```cpp
// After synthIR() returns result:
auto wavMain    = IRSynthEngine::makeWav(result.iLL, result.iRL, result.iLR, result.iRR, sr);
// write to <name>.wav

if (result.direct.synthesised)
{
    auto wavDirect = IRSynthEngine::makeWav(result.direct.LL, result.direct.RL,
                                            result.direct.LR, result.direct.RR, sr);
    // write to <name>_direct.wav
}
// Same for outrig and ambient
```

Also update `rawSynthDirectBuffer`, `rawSynthOutrigBuffer`, `rawSynthAmbientBuffer` in `PingProcessor` from the synthesis result, so `reloadSynthIR` works correctly for the next Reverse/Stretch/Decay change.

#### 4e. `.ping` sidecar

Extend the JSON serialisation to include the new fields. Example additions:
```json
{
  "direct_enabled": false,
  "outrig_enabled": false,
  "outrig_lx": 0.15, "outrig_ly": 0.80,
  "outrig_rx": 0.85, "outrig_ry": 0.80,
  "outrig_langle": -2.35619, "outrig_rangle": -0.78540,
  "outrig_height": 3.0,
  "outrig_pattern": "cardioid (LDC)",
  "ambient_enabled": false,
  "ambient_lx": 0.20, "ambient_ly": 0.95,
  "ambient_rx": 0.80, "ambient_ry": 0.95,
  "ambient_langle": -2.35619, "ambient_rangle": -0.78540,
  "ambient_height": 6.0,
  "ambient_pattern": "omni"
}
```

Missing fields on load use `IRSynthParams` defaults (all extras disabled). Existing sidecars load without errors.

---

### Phase 5 — MicMixerComponent (new component)

**Files:** `Source/MicMixerComponent.h`, `Source/MicMixerComponent.cpp` (new files)

This component replaces `OutputLevelMeter` in `PluginEditor`. It contains four vertical channel strips.

#### 5a. Strip layout (per strip, top to bottom)

1. **Fader readout label** — small, shows current gain in dB (e.g. "0.0", "−6.2"). Update in timer.
2. **Fader + meter column** — the fader thumb is a `juce::Slider` (JUCE `LinearBarVertical` style or custom draw). The coloured level bar fills behind the fader. Dragging sets the APVTS gain param.
3. **Label** — "DIRECT" / "MAIN" / "OUTRIG" / "AMBIENT" in 10 pt small-caps.
4. **Power toggle** — circular power-button icon drawn by `PingLookAndFeel::drawToggleButton`. Sets `directOn` / `outrigOn` / `ambientOn`. The MAIN toggle controls `mainOn` (MAIN can be turned off).
5. **M / S buttons** — small `juce::ToggleButton` instances styled as labelled pill buttons. M → mute param, S → solo param. When any solo is active, non-soloed strips visually grey their fader.
6. **Pan knob** — small rotary, `outputGainKnobSize * 0.7f`. Centred below M/S. Centre detent. Tooltip shows "C", "L50", "R100" etc.
7. **HP toggle** — very small toggle or pill below pan, labelled "HP". Toggles the 110 Hz high-pass for this path.

#### 5b. Colours

| Strip | Meter bar colour |
|---|---|
| DIRECT | `0xff7ab8d4` (pale blue) |
| MAIN   | `0xff8cd6ef` (existing icy accent) |
| OUTRIG | `0xffb09aff` (soft purple) |
| AMBIENT| `0xffcfa95e` (amber) |

Muted or disabled strips: bar colour at 40% opacity. Label greyed to `textDim`.

#### 5c. APVTS attachment

Each strip's fader, pan, mute, solo, on/off, and HP toggle are wired to the corresponding APVTS params via `AudioProcessorValueTreeState::SliderAttachment`, `ButtonAttachment` etc. in the `MicMixerComponent` constructor.

#### 5d. Peak meter update

`MicMixerComponent` has a `startTimerHz(24)` timer. In `timerCallback()`, it reads the per-path atomic peak values from `PingProcessor` and updates the bar heights. The existing `OutputLevelMeter` timer is removed.

#### 5e. Position in PluginEditor

```cpp
// Replace:
outputLevelMeter.setBounds(meterX, meterY, meterW, meterH);

// With:
const int mixerW = 310;
const int mixerH = 160;
const int mixerX = rowStartX;
const int mixerY = h - 38 - mixerH + 15;  // same anchor as old meter (bottom at h − 23)
micMixer.setBounds(mixerX, mixerY, mixerW, mixerH);
```

Remove `OutputLevelMeter` from `PluginEditor` entirely. The licence label and version label remain at `(12, h-18)` and `(w/2, h-18)` — unchanged.

---

### Phase 6 — Factory IR Regeneration

**Files:** `Tools/generate_factory_irs.cpp`

For each of the 27 venue definitions, add default outrigger and ambient positions (following the orchestral convention: outriggers 10–15% further out than main mics at same depth; ambients at 95% room depth, 6–8 m high) and set `outrig_enabled = true`, `ambient_enabled = true`, `direct_enabled = true`.

The tool will now emit 4 WAV files per venue instead of 1. The total factory IR file count grows from 27 to 108.

`Tools/trim_factory_irs.py` requires no changes — it trims any `.wav` files it finds, regardless of suffix.

**Rebuild command** (unchanged):
```bash
g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
    Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
    -o build/generate_factory_irs -lm
./build/generate_factory_irs Installer/factory_irs Installer/factory_presets
python3 Tools/trim_factory_irs.py Installer/factory_irs
```

---

## Key Risks and Critical Notes

### R1: processBlock restructure
The biggest risk. Currently the MAIN convolution writes directly into `buffer`. After this change, MAIN writes into a temporary per-block buffer (`mainBuf`), and a final sum step writes into `buffer`. Be very careful to preserve:
- The exact `trueStereoWetGain = 2.0f` scaling
- The existing ER/Tail `erLevel`/`tailLevel` SmoothedValue application
- The ER/Tail crossfeed (applied to MAIN only, before MAIN's strip fader)
- The `×0.5` stereo gain compensation (only applies to the 2-channel stereo path, not true-stereo)

Write unit tests or reference golden values before touching processBlock.

### R2: convTmp AudioBlock length
The existing code uses the explicit 3-argument `AudioBlock` constructor: `juce::dsp::AudioBlock<float>(convTmp.getArrayOfWritePointers(), 1, (size_t)numSamples)`. **Do not use the single-argument form** `AudioBlock<float>(convTmp)` for any of the new convolvers — it captures `getNumSamples()` (= `samplesPerBlock`) not `numSamples`, causing overlap-add corruption on short blocks. See CLAUDE.md "processBlock scratch buffers" for the full history.

### R3: isRestoringState / triple-load guard
The `isRestoringState` + `callAsync` pattern guards against `parameterChanged` triggering redundant IR reloads during `setStateInformation`. Ensure the new `*On` params (`outrigOn`, `ambientOn`, `directOn`) do NOT trigger `loadSelectedIR()` from `parameterChanged`. These on/off states are mixer controls only — they enable/disable DSP in processBlock, they do not trigger IR loading. Only `stretch` and `decay` trigger `loadSelectedIR()` (existing behaviour, unchanged).

### R4: audioEnginePrepared race with 28 convolvers
With 28 convolvers total, the window for a `setStateInformation → prepareToPlay` race is wider. The existing `audioEnginePrepared` / `deferConvolverLoad` guard already handles this — ensure all 20 new convolvers are in the deferred path (not loaded until after `prepareToPlay` completes and `callAsync` fires). Do not add `loadImpulseResponse` calls for new convolvers anywhere that runs before `audioEnginePrepared = true`.

### R5: getStateInformation state size
Only embed `rawSynthBuffer` (MAIN) as base64 in the plugin state. The three extra raw synth buffers (`rawSynthDirectBuffer` etc.) are NOT embedded — they are always reconstructed from the on-disk WAV files. If the WAV files are absent (e.g. user deleted them), those paths default to off. Embedding all four buffers risks exceeding DAW state size limits.

### R6: Progress callback thread safety
The `IRSynthProgressFn` lambda is called from up to 4 threads simultaneously. The `updateProgress` aggregation (atomic fractions → single progress value → callback) must be thread-safe. Use `std::atomic<double>` for per-thread progress values and ensure the outer callback itself is thread-safe (it typically posts to the message thread via `callAsync` in IRSynthComponent).

### R7: Existing tests
IR_01–IR_13 and DSP_01–DSP_14 test the existing engine and DSP building blocks. The new parallel synthesis code must not change the output of the MAIN path. After Phase 1, run all existing tests and verify they still pass. IR_11 (golden regression) is particularly sensitive.

---

## Files Modified Summary

| File | Change |
|---|---|
| `Source/IRSynthEngine.h` | Extend `IRSynthParams`, add `MicIRChannels`, extend `IRSynthResult`, add `synthDirectPath`/`synthExtraPath` private declarations |
| `Source/IRSynthEngine.cpp` | Parallel `synthIR()`, `synthDirectPath()`, `synthExtraPath()`, `calcRefs` maxOrder param |
| `Source/PluginProcessor.h` | 20 new convolvers, new APVTS params, SmoothedValues, HP filter state, per-path peaks, `rawSynth*Buffer` variants, `MicPath` enum, `irLoadFadeSamplesRemaining` |
| `Source/PluginProcessor.cpp` | Extend `createParameterLayout`, `prepareToPlay`, `processBlock` (mixer insertion), `loadIRFromBuffer`, `loadMicPathFromFile`, `loadSelectedIR`, `reloadSynthIR`, `getStateInformation`, `setStateInformation` |
| `Source/FloorPlanComponent.h` | Extend `TransducerState` to 8 points, add `outrigVisible`/`ambientVisible`, per-group callbacks |
| `Source/FloorPlanComponent.cpp` | Extended hit-test, drawing for 3 mic pairs, colour coding, hollow/dim for disabled paths |
| `Source/IRSynthComponent.h` | New toggle/slider/combo members for outrig/ambient/direct sections |
| `Source/IRSynthComponent.cpp` | New UI sections, extended `getParams`/`setParams`, WAV writing for extra paths, sidecar extension |
| `Source/MicMixerComponent.h` | New file — 4-strip mixer component declaration |
| `Source/MicMixerComponent.cpp` | New file — full implementation |
| `Source/PluginEditor.h` | Replace `OutputLevelMeter` with `MicMixerComponent` |
| `Source/PluginEditor.cpp` | Wire up `MicMixerComponent`, remove `outputLevelMeter`, update `resized()` |
| `Tools/generate_factory_irs.cpp` | Add outrig/ambient positions, emit 4 WAVs per venue |
| `CLAUDE.md` | Document multi-mic architecture in new section |

---

## Implementation Order Recommendation

1. **Phase 1 first** — IRSynthEngine changes only. Verify with existing tests (`PingTests`) that MAIN IR output is bit-identical. The parallel-threaded synthIR still needs to produce the same MAIN result.
2. **Phase 2 (processor) before Phase 3/4/5** — Get the DSP and loading right before building the UI against it.
3. **Phase 3 + Phase 4 in parallel** — FloorPlan and IRSynthComponent changes are independent of each other.
4. **Phase 5 last** — The mixer UI is a clean layer on top of the working DSP.
5. **Phase 6 separately** — Factory IR regeneration is gated only on Phase 1 being stable. Can be done any time after that.
