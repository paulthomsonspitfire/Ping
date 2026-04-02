# Plan: Shimmer (Feature 4)

**Target:** Add a pitch-shifted feedback reverb effect ("shimmer") as Row 6 in the main editor panel.

---

## What it does

Shimmer takes the wet convolver output, pitch-shifts it up by a user-selectable interval (default: +12 semitones = one octave), and feeds it back into the convolver input — producing the characteristic shimmering, harmonically-rising wash associated with the Eventide H3000 and Eno/Lanois-style ambient reverb.

The pitch shifter is a two-grain granular pitch shifter (grain length 512 samples, Hann windowed, two grains offset by half a grain length for continuous output). This matches the reference implementation already specified and verified in DSP_07–DSP_09.

---

## Architecture

### Insertion points

Shimmer uses a **1-block deferred IR feed**, identical to the Cloud architecture:

- **Shimmer Insertion 1** (pre-convolution, after Cloud Insertion 1): Reads `shimBuffer` from the previous block and injects it × `shimIRFeed` into the main signal before convolution. This is the pitch-shifted, delayed shimmer signal re-entering the IR.
- **Shimmer Insertion 2** (post-Tail Chorus, after Cloud Insertion 2): Runs the two-grain pitch shifter on the current wet signal, stores result in `shimBuffer`. Optionally adds `shimBuffer × shimVolume` directly to the wet signal.

Like Cloud, `shimBuffer` is a member variable that persists between blocks. Do not clear it at the start of each block.

### Feedback loop

The shimmer loop is: wet signal → grain shifter → `shimBuffer` → (next block) Insertion 1 → convolver → wet signal. The convolver's inherent decay keeps loop gain below 1 for any real room IR. `shimFeedback` is a separate parameter that scales the shimmer output before it's added back to the convolver input — this is a multiplier on `shimIRFeed`, controlling how much the shimmer "builds" on itself.

Actually — keeping it simple and consistent with Cloud: **`shimIRFeed`** is the knob that injects shimBuffer into the convolver input (the "pitch-shifted signal re-entering the reverb" path). **`shimVolume`** adds shimBuffer directly to the final output independently. No separate "shimFeedback" parameter — the feedback amount is simply `shimIRFeed`.

---

## DSP state (`PluginProcessor.h`)

```cpp
// ── Shimmer ────────────────────────────────────────────────────────────────
static constexpr int kShimGrainLen  = 512;
static constexpr int kShimBufLen    = 8192;   // grain buffer — matches DSP_07 spec

struct ShimmerVoice {
    std::vector<float> grainBuf;   // kShimBufLen samples, circular
    int   writePtr    = 0;
    float readPtrA    = 0.f;
    float readPtrB    = 0.f;       // initialised to kShimGrainLen/2 in prepareToPlay
    float grainPhaseA = 0.f;
    float grainPhaseB = 0.5f;
};

std::array<ShimmerVoice, 2> shimVoices;   // [ch]
juce::AudioBuffer<float>    shimBuffer;   // bridge: Insertion 2 → Insertion 1 (next block)
int                         shimLastBlockSize = 0;
```

The `ShimmerVoice` struct is channel-local state. Both channels share the same `pitchRatio` (derived from `shimPitch` in semitones) but have independent grain buffers and read pointers, giving natural L/R independence as the reverb tail varies between channels.

---

## Parameters

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| `shimOn` | Shimmer On | bool | false | Zero overhead when off |
| `shimPitch` | Shimmer Pitch | −24–+24 semitones | +12 | Readout: show semitone value with sign, e.g. "+12 st". Integer steps (0.01 is fine, user snaps with Ctrl). Default +12 = 1 octave up. |
| `shimSize` | Shimmer Size | 0.5–4.0 | 1.0 | Scales the grain length: `effGrainLen = round(kShimGrainLen * shimSize)`. Longer grains = smoother pitch at cost of more latency/smear. Readout: grain length in ms, e.g. "10.7 ms". |
| `shimIRFeed` | Shimmer IR Feed | 0–1 | 0.5 | Injects shimBuffer × shimIRFeed into the convolver input. This is the primary shimmer feedback path. Default 0.5 (audible shimmer at default settings). |
| `shimVolume` | Shimmer Volume | 0–1 | 0 | Adds shimBuffer × shimVolume directly to the wet signal after Insertion 2 (before output gain), independent of dry/wet. Default 0 = only the IR feed path is audible. |

**5 controls total** (matches the Bloom/Cloud row width). No separate "feedback" knob — `shimIRFeed` is the feedback amount.

Note on `shimOn` default: unlike Bloom and Cloud (where both outputs default to 0 so the effect is silent at defaults), Shimmer sets `shimIRFeed` default to 0.5. This means the shimmer effect is **audible as soon as `shimOn` is enabled** — the user doesn't need to turn up a separate control. This matches expected shimmer plugin UX (you enable shimmer and it shimmers).

---

## `prepareToPlay`

```cpp
// Shimmer voices
for (int ch = 0; ch < 2; ++ch)
{
    auto& v = shimVoices[ch];
    v.grainBuf.assign(kShimBufLen, 0.f);
    v.writePtr    = 0;
    v.readPtrA    = 0.f;
    v.readPtrB    = (float)(kShimGrainLen / 2);
    v.grainPhaseA = 0.f;
    v.grainPhaseB = 0.5f;
}
shimBuffer.setSize (2, samplesPerBlock, false, true, true);
shimBuffer.clear();
shimLastBlockSize = 0;
```

No reallocation needed at runtime — `kShimBufLen = 8192` is fixed. The `kShimGrainLen * shimSize` scaling is implemented by the read pointer advancing at `pitchRatio * shimSize` per sample rather than `pitchRatio` alone — **no**, wait. Let's think this through:

The grain pitch shift works because the read pointer advances faster than the write pointer (by `pitchRatio` samples per input sample). `shimSize` scales the *grain length* — a longer grain means smoother transitions (grain phase advances more slowly), but the **read pointer advance rate** is what determines pitch. The read pointer must advance at `pitchRatio` samples per input sample regardless of grain size. `shimSize` only affects grain reset frequency. The grain phase advances at `pitchRatio / effGrainLen` per sample, so at `shimSize = 2.0` grain phase advances at half the rate and each grain lasts twice as long. This is correct.

On grain reset: `readPtr = (writePtr - effGrainLen + kShimBufLen) % kShimBufLen` — same formula as DSP_07, but using `effGrainLen` instead of `kShimGrainLen`.

---

## `processBlock` Insertion 1

After Cloud Insertion 1, before convolution:

```cpp
if (shimOn && shimIRFeed > 0.001f && shimLastBlockSize > 0)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* shim = shimBuffer.getReadPointer(ch);
        float* buf = buffer.getWritePointer(ch);
        for (int i = 0; i < shimLastBlockSize; ++i)
            buf[i] += shim[i] * shimIRFeed;
    }
}
```

---

## `processBlock` Insertion 2

After Cloud Insertion 2, before Output Gain. Per-sample loop:

```cpp
if (shimOn)
{
    const float pitchRatio = std::pow(2.f, shimPitch / 12.f);
    const int   effGrainLen = juce::roundToInt(kShimGrainLen * shimSize);
    const float hannNorm    = 0.5f;   // 1/numGrains to normalise

    auto hannW = [](float p) {
        return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * p);
    };

    shimBuffer.setSize(2, numSamples, false, true, true);  // realloc guard (usually no-op)

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& v = shimVoices[ch];
        const float* wet = buffer.getReadPointer(ch);
        float* shim      = shimBuffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            // Write current wet sample to grain buffer
            v.grainBuf[(size_t)v.writePtr] = wet[i];

            // Read two grains with Hann window
            auto readAt = [&](float rp) -> float {
                int   ri   = ((int)std::floor(rp) % kShimBufLen + kShimBufLen) % kShimBufLen;
                float frac = rp - std::floor(rp);
                return v.grainBuf[(size_t)ri]           * (1.f - frac)
                     + v.grainBuf[(size_t)((ri+1) % kShimBufLen)] * frac;
            };

            float out = (readAt(v.readPtrA) * hannW(v.grainPhaseA)
                       + readAt(v.readPtrB) * hannW(v.grainPhaseB)) * hannNorm;

            shim[i] = out;

            // Advance read pointers and phases
            v.readPtrA    += pitchRatio;
            v.readPtrB    += pitchRatio;
            v.grainPhaseA += pitchRatio / (float)effGrainLen;
            v.grainPhaseB += pitchRatio / (float)effGrainLen;

            if (v.readPtrA >= (float)kShimBufLen) v.readPtrA -= (float)kShimBufLen;
            if (v.readPtrB >= (float)kShimBufLen) v.readPtrB -= (float)kShimBufLen;

            // On grain reset: snap read head to one effGrainLen behind write head
            if (v.grainPhaseA >= 1.f)
            {
                v.grainPhaseA -= 1.f;
                v.readPtrA = (float)((v.writePtr - effGrainLen + kShimBufLen) % kShimBufLen);
            }
            if (v.grainPhaseB >= 1.f)
            {
                v.grainPhaseB -= 1.f;
                v.readPtrB = (float)((v.writePtr - effGrainLen + kShimBufLen) % kShimBufLen);
            }

            v.writePtr = (v.writePtr + 1) % kShimBufLen;

            // Direct output path
            if (shimVolume > 0.001f)
                buffer.getWritePointer(ch)[i] += out * shimVolume;
        }
    }
    shimLastBlockSize = numSamples;
}
else
{
    shimBuffer.clear();
    shimLastBlockSize = 0;
}
```

**Important:** The `buffer` write for `shimVolume` above reads back from `buffer` then writes to it in the same loop. Since we're adding `out` (computed from the grain buffer, not `buffer[i]`), this is safe — no read-after-write hazard.

---

## IDs and parameter layout

In the `IDs` namespace in `PluginProcessor.cpp`:

```cpp
static const juce::String shimOn      { "shimOn" };
static const juce::String shimPitch   { "shimPitch" };
static const juce::String shimSize    { "shimSize" };
static const juce::String shimIRFeed  { "shimIRFeed" };
static const juce::String shimVolume  { "shimVolume" };
```

In `createParameterLayout()`:

```cpp
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::shimOn,     "Shimmer On",      false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimPitch,  "Shimmer Pitch",  juce::NormalisableRange<float>(-24.f, 24.f, 1.f), 12.f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimSize,   "Shimmer Size",    0.5f, 4.0f, 1.0f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimIRFeed, "Shimmer IR Feed", 0.0f, 1.0f, 0.5f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimVolume, "Shimmer Volume",  0.0f, 1.0f, 0.0f));
```

Note `shimPitch` uses a stepped `NormalisableRange` with step = 1.0 (integer semitones only). This prevents fractional-semitone values that would produce detuned shimmer not aligned to any musical interval.

---

## UI — Row 6

Follows the identical pattern to Rows 3–5.

### PluginEditor.h additions

```cpp
// Shimmer controls (row 6)
juce::Slider shimPitchSlider, shimSizeSlider, shimIRFeedSlider, shimVolumeSlider;
// (shimOn has no slider — it's a toggle; we have 4 knobs for a 5-knob row)
// Actually 4 knobs: PITCH, SIZE, IR FEED, VOLUME — leave knob slot 4 empty or...
```

Wait — we have 5 parameter knobs (PITCH, SIZE, IR FEED, VOLUME) plus the on/off toggle. That's 4 knobs in the row. We could either:
a) Use all 5 slots: PITCH, SIZE, [gap], IR FEED, VOLUME
b) Use 4 knobs packed left, toggle right-aligned
c) Add a 5th meaningful knob

The current rows all use 5 knobs (Bloom: SIZE, FEEDBACK, TIME, IR FEED, VOLUME; Cloud: DEPTH, RATE, SIZE, IR FEED, VOLUME). Shimmer with only 4 would look sparse. **Better to add a BLEND knob** — a dry/wet blend that mixes the original wet signal with the shimmer output within the Shimmer block. This gives the user a way to dial in "how much shimmer character" without changing the overall dry/wet of the plugin.

Actually, on reflection: the `shimVolume` already controls the direct shimmer level to the output, and `shimIRFeed` controls how much feeds back into the IR. A BLEND knob would be redundant. Instead, let's add a **MIX knob** that is the wet/dry of the shimmer output within its own block — but this is essentially the same as `shimVolume`.

**Simpler solution:** Keep 4 knobs and accept the visual gap, OR use the 5th slot for a parameter that genuinely adds value. Looking at the Shimmer effect's character, a useful 5th knob would be **SHIMMER COLOUR** — a 1-pole LP on the shimmer output, similar to Plate's COLOUR parameter, letting the user control whether the octave-up is bright (full-range) or warm (low-passed). Range 0–1: 0 → 2 kHz cutoff (dark shimmer, emphasises the harmonic structure), 1 → 20 kHz (no filtering, full brightness).

**Final 5-knob layout:** PITCH | SIZE | COLOUR | IR FEED | VOLUME

```cpp
// Shimmer controls (row 6)
juce::Slider shimPitchSlider, shimSizeSlider, shimColourSlider, shimIRFeedSlider, shimVolumeSlider;
juce::ToggleButton shimOnButton;
```

Add `shimColour` to IDs and layout:
```cpp
static const juce::String shimColour  { "shimColour" };
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::shimColour, "Shimmer Colour", 0.0f, 1.0f, 0.7f));
```

Default 0.7 = moderately bright, cutting some of the harshest HF from the pitch-shifted signal while keeping the shimmer sparkle.

### DSP state for colour LP

```cpp
std::array<float, 2> shimColourState { 0.f, 0.f };  // 1-pole LP state per channel
```

Applied in Insertion 2 after computing `out`, before storing to `shimBuffer`:
```cpp
// 1-pole LP: colour 0 → 2 kHz, colour 1 → 20 kHz
float cutoffHz  = 2000.f + shimColour * 18000.f;
float lpAlpha   = 1.f - std::exp(-juce::MathConstants<float>::twoPi * cutoffHz / sampleRate);
shimColourState[ch] += lpAlpha * (out - shimColourState[ch]);
out = shimColourState[ch];
shim[i] = out;
```

### PluginEditor.h additions (final)

```cpp
// Shimmer controls (row 6)
juce::Slider shimPitchSlider, shimSizeSlider, shimColourSlider, shimIRFeedSlider, shimVolumeSlider;
juce::ToggleButton shimOnButton;

// Attachments
std::unique_ptr<SliderAttachment> shimPitchAttach, shimSizeAttach, shimColourAttach, shimIRFeedAttach, shimVolumeAttach;
std::unique_ptr<ButtonAttachment> shimOnAttach;

// Labels & readouts
juce::Label shimPitchLabel, shimSizeLabel, shimColourLabel, shimIRFeedLabel, shimVolumeLabel;
juce::Label shimPitchReadout, shimSizeReadout, shimColourReadout, shimIRFeedReadout, shimVolumeReadout;

// Group bounds
juce::Rectangle<int> shimGroupBounds;
```

### PluginEditor.cpp changes

- `editorH`: 744 → **816** (+ 72 px for Row 6)
- Constructor: `makeSlider` for 5 sliders; `shimOnButton` setup with ID `"ShimmerSwitch"` and `onClick = repaint()`
- Ranges: `shimPitchSlider (-24.0, 24.0, 1.0)`, `shimSizeSlider (0.5, 4.0, 0.01)`, `shimColourSlider (0.0, 1.0, 0.01)`, `shimIRFeedSlider (0.0, 1.0, 0.01)`, `shimVolumeSlider (0.0, 1.0, 0.01)`
- Attachments: 5 sliders + button attached to APVTS
- Labels: PITCH, SIZE, COLOUR, IR FEED, VOLUME
- `paint()`: `drawGroupHeader (shimGroupBounds, "Shimmer", shimOnButton.getToggleState())`
- `resized()`: Add `row6TotalH_ = row2TotalH_`, `row6AbsY = row5AbsY + row5TotalH_`; full Row 6 layout block with `placeRow6Knob` lambda; shimGroupBounds and shimOnButton bounds (same LED pattern); update 6-knob grid Y anchor to `row6AbsY + row6TotalH_ + 70`
- `setMainPanelControlsVisible()`: 18 `setVisible(visible)` calls for all shimmer controls
- `updateAllReadouts()`: 5 readout setText calls:
  - PITCH: `juce::String((int)v("shimPitch") >= 0 ? "+" : "") + juce::String((int)v("shimPitch")) + " st"`
  - SIZE: `juce::String(v("shimSize"), 2) + "×"`
  - COLOUR: `juce::String(juce::roundToInt(2000.f + v("shimColour") * 18000.f)) + " Hz"`
  - IR FEED: `juce::String(v("shimIRFeed"), 2)`
  - VOLUME: `juce::String(v("shimVolume"), 2)`
- `PingLookAndFeel.cpp`: Add `"ShimmerSwitch"` to the pill-switch condition

---

## Signal flow position

```
[Cloud Insertion 2: 8-line LFO delay bank]
  │
  ▼
[Shimmer Insertion 2: 2-grain pitch shifter on wet signal]  (if shimOn)
  │   shimBuffer ← grain output; wet signal += shimBuffer × shimVolume (if shimVolume > 0)
  │   (next block's Shimmer Insertion 1 reads shimBuffer and injects × shimIRFeed pre-convolver)
  │
  ▼
Output Gain
```

And pre-convolution:

```
[Cloud Insertion 1: prev-block cloudBuffer × cloudIRFeed → main signal]
  │
  ▼
[Shimmer Insertion 1: prev-block shimBuffer × shimIRFeed → main signal]  (if shimOn && shimIRFeed > 0)
  │
  ▼
Convolution
```

---

## Test coverage

DSP_07, DSP_08, and DSP_09 already fully specify and test the grain engine. No new tests are needed for the shimmer grain DSP — it is already verified. The production implementation must match the test spec exactly (same grain reset formula, same Hann window, same `readPtrB` initialisation at `grainLen/2`).

---

## CLAUDE.md updates needed after implementation

1. Parameters table: 5 new rows (shimOn, shimPitch, shimSize, shimColour, shimIRFeed, shimVolume — 6 rows)
2. Signal flow: Cloud Insertion 2 → Shimmer Insertion 2 annotation; Cloud Insertion 1 → Shimmer Insertion 1 annotation
3. New "Shimmer (Feature 4)" section
4. Key design decisions: bridge buffer pattern (same as Cloud), grain reset formula (must match DSP_07), shimColour LP, ShimmerSwitch pill style, editorH 816, 6-knob grid anchor row6, shimPitch integer steps, shimIRFeed defaults to 0.5 (audible at enable unlike other features)

---

## Implementation order

1. `PluginProcessor.h` — add `ShimmerVoice`, member vars, constants
2. `PluginProcessor.cpp` — IDs, layout, prepareToPlay, Insertion 1 (pre-conv), Insertion 2 (post-Cloud)
3. `PluginEditor.h` — sliders, button, attachments, labels, readouts, bounds
4. `PluginEditor.cpp` — editorH, constructor setup, ranges, attachments, labels, resized Row 6, paint, setMainPanelControlsVisible, updateAllReadouts, PingLookAndFeel pill-switch condition
5. Run test suite — all 23 existing tests must still pass
6. Update CLAUDE.md
