# P!NG — Bloom Implementation Plan

**Target version:** 1.9.0
**Status:** Pre-implementation
**Scope:** Feature 2 from Plan-HybridModes.md — Bloom pre-diffusion + feedback, extended with `bloomVolume` (direct output) and `bloomIRFeed` (convolver injection) to match the Plate architecture.

---

## 1. What Bloom does

Bloom creates a progressively building reverb onset where the reverb grows out of silence rather than arriving all at once. Two elements work together:

- **Pre-convolution allpass cascade** — 4 stages with long delays (40–300 ms), creating a gradual smear rather than the immediate dense wash of Plate.
- **Wet-output feedback** — a fraction of the post-convolution wet signal is fed back through the cascade and re-enters the convolver, causing each pass to become progressively more diffuse.

The new `bloomVolume` and `bloomIRFeed` controls follow the Plate pattern: `bloomIRFeed` injects the Bloom-processed signal additively into the IR convolver alongside the main signal; `bloomVolume` sends it directly to the wet output (bypassing the convolver). Both default to 0 — consistent with `plateIRFeed = 0` — so Bloom has zero effect at defaults until the user raises at least one output control.

---

## 2. Architecture decision — Bloom follows the Plate model

The original plan's `bloomDiffusion` was a **mix parameter** (blend between raw and cascade-output before convolution). This is replaced by the Plate model:

| Control | Role |
|---|---|
| `bloomDiffusion` | Allpass g coefficient (0.30–0.88) — how intensely the cascade diffuses each pass. Applied to all 4 stages each block before the sample loop. |
| `bloomFeedback` | Wet→input feedback amount (0–0.65, safety-clamped). |
| `bloomTime` | Feedback tap delay into the feedback buffer (50–500 ms). |
| `bloomIRFeed` | How much of `bloomBuffer` is added to the convolver input (additive, on top of the main signal). Range 0–1, default 0. |
| `bloomVolume` | How much of `bloomBuffer` is added directly to the wet signal after convolution, before EQ. Range 0–1, default 0. |

The cascade processes `(mainInput + feedbackTap)` → stores result in `bloomBuffer`. The main signal path into the convolver is **not modified** by the cascade itself — only additively by `bloomIRFeed`. This gives clean, independent control of both output paths.

---

## 3. Parameters

### `IDs` namespace additions (`PluginProcessor.cpp`)

```cpp
static const juce::String bloomOn        { "bloomOn" };
static const juce::String bloomDiffusion { "bloomDiffusion" };
static const juce::String bloomFeedback  { "bloomFeedback" };
static const juce::String bloomTime      { "bloomTime" };
static const juce::String bloomIRFeed    { "bloomIRFeed" };
static const juce::String bloomVolume    { "bloomVolume" };
```

### `createParameterLayout()` additions

```cpp
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::bloomOn,        "Bloom On",         false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomDiffusion, "Bloom Diffusion",  0.30f, 0.88f, 0.60f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomFeedback,  "Bloom Feedback",   0.f,   0.65f, 0.25f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomTime,      "Bloom Time",       50.f,  500.f, 200.f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomIRFeed,    "Bloom IR Feed",    0.f,   1.f,   0.f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::bloomVolume,    "Bloom Volume",     0.f,   1.f,   0.f));
```

**CLAUDE.md parameters table entry to add:**

| ID | Name | Range | Default |
|----|------|-------|---------|
| `bloomOn` | Bloom On | bool | false |
| `bloomDiffusion` | Bloom Diffusion | 0.30–0.88 | 0.60 |
| `bloomFeedback` | Bloom Feedback | 0–0.65 | 0.25 |
| `bloomTime` | Bloom Time (ms) | 50–500 | 200 |
| `bloomIRFeed` | Bloom IR Feed | 0–1 | 0 |
| `bloomVolume` | Bloom Volume | 0–1 | 0 |

---

## 4. New state in `PluginProcessor.h`

Add after the Plate state block:

```cpp
// ── Bloom ────────────────────────────────────────────────────────────────────
// Bloom reuses SimpleAllpass (defined for Plate — must remain in sync with PingDSPTests.cpp)
static constexpr int kNumBloomStages     = 4;
static constexpr int kBloomFeedbackMaxMs = 500;

// 4-stage allpass cascade, per channel
// Base prime delays (samples at 48 kHz): ~39 ms, ~74 ms, ~150 ms, ~300 ms
std::array<std::array<SimpleAllpass, kNumBloomStages>, 2> bloomAPs; // [ch][stage]

// Circular feedback buffer — stores post-convolution wet signal for re-injection
std::array<std::vector<float>, 2> bloomFbBufs;
std::array<int, 2>                bloomFbWritePtrs { 0, 0 };

// Per-block intermediate buffer: stores cascade output for use at both
// the pre-convolution IR-feed injection and the post-convolution volume output.
// Sized to max block size in prepareToPlay.
juce::AudioBuffer<float> bloomBuffer;
```

**Key design note:** `bloomBuffer` bridges two processBlock insertion points within the same block. It must be allocated in `prepareToPlay` to `(2, maxBlockSize)` and zeroed at the start of each Bloom processing block. It is NOT a feedback buffer — `bloomFbBufs` is the feedback buffer.

---

## 5. `prepareToPlay` additions

Add after Plate `prepareToPlay` block:

```cpp
// Bloom: 4 allpass stages with long prime delays
// At 48 kHz: 1871 → ~39 ms, 3541 → ~74 ms, 7211 → ~150 ms, 14387 → ~300 ms
// Bloom has no size-scaling (unlike Plate's plateSize), so no allocation headroom needed.
const int bloomPrimes[kNumBloomStages] = { 1871, 3541, 7211, 14387 };
for (int ch = 0; ch < 2; ++ch)
    for (int s = 0; s < kNumBloomStages; ++s) {
        int d = (int)std::round (bloomPrimes[s] * sampleRate / 48000.0);
        bloomAPs[ch][s].buf.assign ((size_t)d, 0.f);
        bloomAPs[ch][s].ptr    = 0;
        bloomAPs[ch][s].effLen = 0;   // Bloom stages use full buf.size() (no size scaling)
        bloomAPs[ch][s].g      = 0.60f;
    }

// Bloom feedback buffer (max 500 ms of wet signal, per channel)
int bloomFbSamps = (int)std::ceil (kBloomFeedbackMaxMs * sampleRate / 1000.0);
for (int ch = 0; ch < 2; ++ch) {
    bloomFbBufs[ch].assign ((size_t)bloomFbSamps, 0.f);
    bloomFbWritePtrs[ch] = 0;
}

// Bloom intermediate buffer (per-block output of cascade, used for IR feed + volume inject)
bloomBuffer.setSize (2, expectedBlockSize, false, true, true);
```

**Note on `expectedBlockSize`:** use the `maximumExpectedSamplesPerBlock` argument passed to `prepareToPlay`. If Bloom blocks are used beyond `maximumExpectedSamplesPerBlock`, reallocate on the fly with a guard (`if (numSamples > bloomBuffer.getNumSamples()) bloomBuffer.setSize(...)`).

---

## 6. `processBlock` — insertion points

Bloom has **four** insertion points in `processBlock`:

### Insertion 1 — Pre-convolution: cascade processing + IR feed inject
**Position:** After the saturator block (and after Plate block), **before** the convolution block.

```cpp
bool bloomOn = apvts.getRawParameterValue (IDs::bloomOn)->load() > 0.5f;
if (bloomOn)
{
    float bloomDiff   = apvts.getRawParameterValue (IDs::bloomDiffusion)->load();
    float fbAmt       = std::min (0.65f, apvts.getRawParameterValue (IDs::bloomFeedback)->load());
    float bloomTimeMs = juce::jlimit (50.f, 500.f, apvts.getRawParameterValue (IDs::bloomTime)->load());
    float irFeed      = apvts.getRawParameterValue (IDs::bloomIRFeed)->load();

    // Set g on all stages once per block (not per sample)
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < kNumBloomStages; ++s)
            bloomAPs[ch][s].g = bloomDiff;

    bloomBuffer.clear();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data  = buffer.getWritePointer (ch);
        float* bloom = bloomBuffer.getWritePointer (ch);
        int    fbLen = (int)bloomFbBufs[ch].size();
        int    timeInSamples = juce::jlimit (1, fbLen - 1,
                                   (int)std::round (bloomTimeMs * currentSampleRate / 1000.0));

        for (int i = 0; i < numSamples; ++i)
        {
            // Read feedback tap: bloomTime ms back in the wet-output buffer
            int rdPtr = (bloomFbWritePtrs[ch] - timeInSamples + fbLen) % fbLen;
            float fb  = bloomFbBufs[ch][(size_t)rdPtr] * fbAmt;

            // Run (input + feedback) through the 4-stage allpass cascade
            float in   = data[i] + fb;
            float diff = in;
            for (int s = 0; s < kNumBloomStages; ++s)
                diff = bloomAPs[ch][s].process (diff);

            // Store cascade output — used for IR feed (here) and volume output (Insertion 3)
            bloom[i] = diff;

            // IR feed: add bloom cascade output into the convolver input additively
            data[i] += diff * irFeed;
        }
    }
}
```

### Insertion 2 — Post-convolution: direct volume output
**Position:** After the ER + Tail blend (convolution output ready), **before** the 5-band EQ block.

The Bloom direct output runs through EQ, decorrelation, LFO mod, Width, Tail Chorus, Output Gain, and Dry/Wet — giving it the same shaping as the convolved reverb. This is intentional: the EQ sculpts Bloom's diffuse texture the same way it shapes the reverb tail.

```cpp
if (bloomOn)
{
    float bloomVol = apvts.getRawParameterValue (IDs::bloomVolume)->load();
    if (bloomVol > 0.f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float*       data  = buffer.getWritePointer (ch);
            const float* bloom = bloomBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] += bloom[i] * bloomVol;
        }
    }
}
```

### Insertion 3 — Feedback tap: write wet signal for next block
**Position:** After the 5-band EQ + stereo decorrelation allpass, **before** LFO mod. (Matches the plan's Location B — "after the convolution / EQ / decorrelation chain".)

The feedback captures the EQ-shaped wet signal, so the bloom grows with the room's filtered character on each recirculation.

```cpp
if (bloomOn)
{
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

---

## 7. Signal chain diagram (with Bloom active)

```
Input (stereo)
  │
  ├──── dryBuffer copy
  │
  ▼
Predelay → Input Gain → Saturator
  │
  ▼ [Plate block — if plateOn]
  │
  ▼ [BLOOM INSERTION 1: cascade processing]
  │   bloomBuffer[ch][i] = allpassCascade(data[i] + feedbackTap)
  │   data[i] += bloomBuffer[ch][i] * bloomIRFeed        ← IR Feed path
  │
  ▼
Convolution (ER + Tail)
  │
  ▼
[ER × erLevel + Tail × tailLevel summed]
  │
  ▼ [BLOOM INSERTION 2: direct volume output]
  │   data[i] += bloomBuffer[ch][i] * bloomVolume        ← Volume path
  │
  ▼
5-band EQ (low shelf → 3 peaks → high shelf)
  │
  ▼
Stereo decorrelation allpass (R ch only)
  │
  ▼ [BLOOM INSERTION 3: feedback tap]
  │   bloomFbBufs[ch][wp] = data[i]  (EQ-shaped wet captured here)
  │
  ▼
LFO Modulation → Stereo Width → Tail Chorus
  │
  ▼
Output Gain → Dry/Wet blend → Output
```

---

## 8. Stability and safety notes

**Allpass cascade:** `SimpleAllpass` is all-pass in frequency — no gain, no instability. The 4 stages use g in the range 0.30–0.88, well within the unit-circle stability limit. At `g = 0` the cascade is a pass-through.

**Feedback loop:** Total loop gain = `bloomFeedback × convolver_gain`. The safety clamp at 0.65 prevents the feedback coefficient from approaching 1.0. The convolver has gain < 1 for any physical IR (IR energy is finite and decays). The 1-frame latency (reading the previous block's written samples) breaks any same-block feedback loop.

**`bloomVolume` additive output:** Since the cascade output is bounded by the input energy (all-pass, no amplification), adding `bloom * bloomVolume` to the wet signal can at most double the wet level at `bloomVolume = 1.0`. This is equivalent in gain risk to turning up a wet knob, not a feedback loop.

**`bloomIRFeed` additive inject:** Adds cascade-processed signal into the convolver input on top of the main signal. At `bloomIRFeed = 1.0` the convolver sees twice the energy. For physical IRs this doubles the wet output, same bounded risk as above.

**`bloomBuffer` reuse across insertions:** `bloomBuffer` is populated at Insertion 1 and read at Insertion 2. Both insertions are in the same `processBlock` call — no threading concern. `bloomBuffer.clear()` at the top of Insertion 1 ensures no stale data if `numSamples` changes between blocks.

---

## 9. `SimpleAllpass` sync requirement

The `SimpleAllpass` struct definition in `PluginProcessor.h` must remain **exactly in sync** with the one defined locally in `PingDSPTests.cpp`. This is an existing requirement for Plate. Bloom reuses the same struct — no changes needed to the struct itself.

The DSP tests that are already written and must continue to pass:

| Test | What it checks |
|---|---|
| DSP_03 | Bloom allpass is causal — no output before first delay drains |
| DSP_04 | Bloom feedback stable at max setting (0.65), bloomTime = 300 ms |
| DSP_11 | Bloom feedback stable at minimum bloomTime (50 ms) — worst-case stress test |

These tests define their own local `SimpleAllpass` and Bloom struct. They are reference specs and do not test the UI parameters or `bloomBuffer`/`bloomVolume`/`bloomIRFeed` — those are production-only concerns.

---

## 10. UI — Row 4 (Bloom)

**Position:** Immediately after Row 3 (Plate), using the same `rowKnobSize`/`rowStep`/`rowStartX` constants.

**Layout:** Single group "Bloom" with 5 knobs + 1 pill switch right-aligned in the group header. No inter-group gap.

| Knob index | Parameter ID | Label |
|---|---|---|
| 0 | `bloomDiffusion` | DIFFUSION |
| 1 | `bloomFeedback` | FEEDBACK |
| 2 | `bloomTime` | TIME |
| 3 | `bloomIRFeed` | IR FEED |
| 4 | `bloomVolume` | VOLUME |
| — | `bloomOn` | pill switch, right-aligned in "Bloom" header |

**Readout formats:**
- DIFFUSION: display `bloomDiffusion` to 2 decimal places (e.g. `"0.60"`)
- FEEDBACK: display to 2 decimal places (e.g. `"0.25"`)
- TIME: display in ms to 0 decimal places (e.g. `"200 ms"`)
- IR FEED: display to 2 decimal places (e.g. `"0.50"`)
- VOLUME: display to 2 decimal places (e.g. `"0.30"`)

**Editor height:** bump `editorH` from `600` → `672` px to accommodate Row 4. `minH` remains at 528.

**Row 4 Y anchor:** use `row4AbsY = row3AbsY + row3TotalH_` (absolute, not `mainArea.removeFromTop`) — consistent with the Row 2/3 absolute-anchor pattern to avoid `removeFromTop` clamping (see CLAUDE.md design decision on absolute row anchors).

**Switch component ID:** `"BloomSwitch"` — used by `PingLookAndFeel` pill drawing and `repaint()` trigger so the header text glows orange when active.

**Group header bounds:** stored as `bloomGroupBounds` (set in `resized()`, drawn in `paint()` via `drawGroupHeader`).

---

## 11. CLAUDE.md key design decisions to add

After the Plate section, add:

> **Bloom has two output paths: IR Feed and Volume, both defaulting to 0** — consistent with `plateIRFeed = 0`. `bloomIRFeed` injects the cascade output additively into the convolver input; `bloomVolume` adds it directly to the wet signal before EQ. The main signal is not modified by the cascade itself. At both defaults = 0, Bloom has no audible effect until the user raises at least one output control.
>
> **`bloomBuffer` bridges pre-conv and post-conv insertions within a single block** — populated at Insertion 1 (cascade output), read at Insertion 2 (volume output). It is not a feedback buffer. Sized to `maximumExpectedSamplesPerBlock` in `prepareToPlay` and cleared at the start of each Bloom block.
>
> **Bloom feedback tap position: after EQ + decorrelation, before LFO mod** — the EQ-shaped wet signal is what gets fed back, so each recirculation inherits the current EQ curve. Changing the EQ while Bloom feedback is active will gradually shift the bloom character over several feedback cycles.
>
> **Bloom g coefficient is set per block, not per sample** — `bloomDiffusion` is read once from APVTS and written to all `bloomAPs[ch][s].g` before the sample loop, consistent with the Plate implementation pattern.

---

## 12. Implementation order

1. **Parameters + IDs** — add all 6 entries to `IDs` namespace and `createParameterLayout()`. Build and verify preset saving/loading still works (no existing presets affected — new keys default gracefully).
2. **State + prepareToPlay** — add `bloomAPs`, `bloomFbBufs`, `bloomBuffer`, reset logic. Verify `prepareToPlay` compiles and the plugin loads without crash.
3. **Insertion 1 (cascade + IR feed)** — implement pre-convolution block. Verify: with `bloomIRFeed = 0.5` and a short IR loaded, toggling `bloomOn` produces an audible difference in the convolved wet signal. Verify `bloomIRFeed = 0` is bit-identical to bypass.
4. **Insertion 3 (feedback tap)** — implement feedback buffer write. Verify with `bloomFeedback > 0` that the reverb tail lengthens and DSP_04/DSP_11 tests still pass.
5. **Insertion 2 (volume output)** — implement direct wet output. Verify `bloomVolume = 0` is still bit-identical to bypass; `bloomVolume = 1` adds a diffuse texture on top of the convolved wet.
6. **UI Row 4** — add knobs, switch, group header. Verify layout at both 672 px and minimum 528 px window sizes.
7. **Tests** — run full test suite (`ctest --output-on-failure`). DSP_03/DSP_04/DSP_11 must pass unchanged; IR_01–IR_11 must be unaffected (Bloom does not touch `IRSynthEngine`).

---

## 13. Open questions / risks

- **`bloomBuffer` size vs actual block size:** if the host calls `processBlock` with `numSamples > maximumExpectedSamplesPerBlock`, `bloomBuffer` will be too small. Add a guard at the top of Insertion 1: `if (numSamples > bloomBuffer.getNumSamples()) bloomBuffer.setSize(2, numSamples, false, true, false);`
- **Bloom + Shimmer interaction (for later):** per Plan-HybridModes.md, when both are on, Shimmer feedback bypasses Bloom's feedback path and injects directly before convolution. These are independent paths. No action needed now but document it when Shimmer is implemented.
- **`row4TotalH_` constant:** needs to be defined in `PluginEditor.h` alongside `row3TotalH_`. Set it to the same value as `row3TotalH_` since the layouts are identical.
- **Readout for TIME knob:** the value is in milliseconds (50–500). Display with no decimal places and a "ms" suffix. Edge cases: at 50 ms display `"50 ms"`, at 500 ms display `"500 ms"`.
