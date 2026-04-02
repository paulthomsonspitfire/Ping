# P!NG — Cloud Multi-LFO Implementation Plan

**Target version:** 1.8.7 (follows Bloom at 1.8.6)
**Status:** Pre-implementation plan
**Scope:** Cloud Multi-LFO — Feature 3 from Plan-HybridModes.md, with two additional output controls (`cloudVolume`, `cloudIRFeed`) added for consistency with the Plate and Bloom implementations.

---

## What it does

Cloud applies a bank of 8 simultaneously-running, independently-frequency LFO-modulated delay lines to the wet reverb tail. The 8 LFOs span a geometric rate range (0.04–0.35 Hz) so no two share a rational beat frequency — the result is aperiodic, continuously-shifting animation of the tail that never settles into an audible cyclic sweep. The overall character is the dense, living-air shimmer of high-end algorithmic reverbs.

---

## Architecture — Cloud follows the Plate / Bloom model

The original `Plan-HybridModes.md` had Cloud modifying the wet signal **in-place** (`out = input*(1-depth*0.7) + avg*depth*0.7`). This plan updates that to match the Plate/Bloom pattern:

- Cloud runs on the wet signal (post-Tail Chorus) and stores its output in a bridge buffer `cloudBuffer`
- The main wet signal is **not modified** by the Cloud bank itself
- **`cloudVolume`** injects `cloudBuffer × volume` additively into the wet signal (default 0 = off)
- **`cloudIRFeed`** injects `cloudBuffer × irFeed` back into the convolver input (1-block deferred, default 0 = off)
- At both defaults = 0, Cloud has no audible effect when switched on — consistent with Plate (`plateIRFeed = 0`) and Bloom (`bloomVolume = bloomIRFeed = 0`)

This means `cloudDepth` becomes a **pure modulation-depth control** (how far the LFOs swing the delay times) rather than a wet/dry blend. The output routing is now fully independent, controlled by `cloudVolume` and `cloudIRFeed`.

### IR feed: 1-block deferred feedback

Because Cloud runs **post-convolution** (after Tail Chorus), its IR feed must be deferred by one block. `cloudBuffer` persists as a member variable between `processBlock` calls:

- Start of block N+1 (pre-convolution phase): read `cloudBuffer` (holds block N's Cloud output), inject `× cloudIRFeed` into the main signal before the convolver.
- Post-Tail Chorus in block N+1: overwrite `cloudBuffer` with block N+1's Cloud output.

This is safe because Insertion 1 (read) runs before Insertion 2 (write) within each block. No separate feedback circular buffer is needed — the 1-block latency is inherent to the post→pre ordering.

Stability: `cloudIRFeed × convolver_gain < 1` for any physical IR (convolver gain ≪ 1 for real room IRs), so the feedback loop is unconditionally stable. No additional safety clamp is needed beyond the 0–1 parameter range.

---

## Parameters

| ID | Name | Range | Default | Effect |
|---|---|---|---|---|
| `cloudOn` | Cloud On | bool | false | Enables Cloud; zero overhead when off |
| `cloudDepth` | Cloud Depth | 0–1 | 0.3 | LFO modulation depth scale: 0 = no modulation, 1 = ±3 ms per line |
| `cloudRate` | Cloud Rate | 0.1–4.0 | 1.0 | Rate multiplier applied to all 8 LFOs; preserves geometric spacing |
| `cloudSize` | Cloud Size | 1–40 ms | 5 ms | Base (unmodulated) delay of each line — sets spatial character. Short: intimate chorus. Long: cavernous spatial smear |
| `cloudVolume` | Cloud Volume | 0–1 | 0.0 | How much Cloud output is added to the wet signal (additive injection) |
| `cloudIRFeed` | Cloud IR Feed | 0–1 | 0.0 | How much Cloud output feeds back to the convolver input (1-block deferred) |

---

## New state in `PluginProcessor.h`

Add after the Bloom section:

```cpp
// ── Cloud Multi-LFO ──────────────────────────────────────────────────────────
static constexpr int   kNumCloudLines      = 8;
static constexpr float kCloudBaseDepthMs   = 3.0f;   // max per-line LFO swing at cloudDepth=1
static constexpr float kCloudSizeMaxMs     = 40.0f;  // UI cloudSize upper bound
static constexpr float kCloudBufMs         = 45.0f;  // = sizeMax + depthMax + 2 ms margin

// Per-line, per-channel delay buffers
std::array<std::array<std::vector<float>, kNumCloudLines>, 2> cloudBufs;    // [ch][line]
std::array<std::array<int,               kNumCloudLines>, 2> cloudWritePtrs {};

// LFO state — phases are global (not per-channel); R channel applies a π phase offset
std::array<float, kNumCloudLines> cloudLfoPhases     {};   // 0 .. 2π
std::array<float, kNumCloudLines> cloudLfoBaseRates  {};   // radians/sample at rate=1×

// Bridge buffer: Cloud output from current block (Insertion 2 writes, Insertion 1 reads next block)
juce::AudioBuffer<float> cloudBuffer;
int                      cloudLastBlockSize = 0;   // valid sample count in cloudBuffer
```

### Why no separate feedback circular buffer

Bloom needs `bloomFbBufs` (a circular buffer with up to 500 ms depth) because `bloomTime` controls how far back the feedback reads. Cloud's IR feed is a fixed 1-block round-trip — there is no variable "cloud time" parameter — so `cloudBuffer` itself is the feedback store. Persisting between `processBlock` calls is all that's needed.

---

## `prepareToPlay` additions

After the Bloom section:

```cpp
// Cloud: 8-line LFO-modulated delay bank
{
    const double rBase       = 0.04;  // Hz
    const double rTop        = 0.35;  // Hz
    const double k           = std::pow (rTop / rBase, 1.0 / (kNumCloudLines - 1));
    const int    cloudBufSamps = (int)std::ceil (kCloudBufMs * sampleRate / 1000.0);

    for (int i = 0; i < kNumCloudLines; ++i)
    {
        const double rHz        = rBase * std::pow (k, i);
        cloudLfoBaseRates[i]    = (float)(2.0 * M_PI * rHz / sampleRate);
        cloudLfoPhases[i]       = (float)(i * 0.7854);   // π/4 stagger to distribute initial positions
        for (int ch = 0; ch < 2; ++ch)
        {
            cloudBufs[ch][i].assign ((size_t)cloudBufSamps, 0.f);
            cloudWritePtrs[ch][i] = 0;
        }
    }

    cloudBuffer.setSize (2, samplesPerBlock);
    cloudBuffer.clear();
    cloudLastBlockSize = 0;
}
```

---

## `processBlock` — two insertion points

### Insertion 1 — before convolution (IR feed from previous block)

Position: immediately after Bloom Insertion 1 (the Bloom cascade block), before the `erDb` convolution setup.

```cpp
// ── Cloud Insertion 1: IR feed — inject previous block's Cloud output into convolver input
if (apvts.getRawParameterValue (IDs::cloudOn)->load() > 0.5f)
{
    const float irFeed = apvts.getRawParameterValue (IDs::cloudIRFeed)->load();
    if (irFeed > 0.f && cloudLastBlockSize > 0)
    {
        const int injectN = std::min (numSamples, cloudLastBlockSize);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float*       data = buffer.getWritePointer (ch);
            const float* prev = cloudBuffer.getReadPointer (ch);
            for (int i = 0; i < injectN; ++i)
                data[i] += prev[i] * irFeed;
        }
    }
}
```

**Key:** `cloudBuffer` still holds block N's output at this point in block N+1. It is overwritten later in the same block by Insertion 2.

### Insertion 2 — after Tail Chorus, before Output Gain

Position: immediately after the tail chorus modulation block, before the output gain smoothing loop.

```cpp
// ── Cloud Insertion 2: run wet signal through 8-line LFO delay bank
if (apvts.getRawParameterValue (IDs::cloudOn)->load() > 0.5f)
{
    const float depth   = apvts.getRawParameterValue (IDs::cloudDepth)->load();
    const float rate    = juce::jlimit (0.1f, 4.0f,
                              apvts.getRawParameterValue (IDs::cloudRate)->load());
    const float sizeMs  = juce::jlimit (1.f, kCloudSizeMaxMs,
                              apvts.getRawParameterValue (IDs::cloudSize)->load());
    const float volume  = apvts.getRawParameterValue (IDs::cloudVolume)->load();

    // Reallocate bridge buffer if host delivers larger-than-expected block
    if (numSamples > cloudBuffer.getNumSamples())
        cloudBuffer.setSize (2, numSamples, false, true, true);
    cloudBuffer.clear();

    const float maxDepthSamps = kCloudBaseDepthMs * depth * (float)currentSampleRate / 1000.f;
    const float sizeSamps     = sizeMs * (float)currentSampleRate / 1000.f;
    const int   bufSamps      = (int)cloudBufs[0][0].size();

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance all 8 LFO phases once per sample (before the per-channel loop)
        for (int line = 0; line < kNumCloudLines; ++line)
        {
            cloudLfoPhases[line] += cloudLfoBaseRates[line] * rate;
            if (cloudLfoPhases[line] > 2.f * juce::MathConstants<float>::pi)
                cloudLfoPhases[line] -= 2.f * juce::MathConstants<float>::pi;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // R channel uses π phase offset on all LFOs for stereo decorrelation
            const float phaseOff = (ch == 1) ? juce::MathConstants<float>::pi : 0.f;
            const float input    = buffer.getSample (ch, i);

            float lineSum = 0.f;
            for (int line = 0; line < kNumCloudLines; ++line)
            {
                auto& buf = cloudBufs[ch][line];
                const int wp = cloudWritePtrs[ch][line];

                buf[(size_t)wp] = input;

                const float mod       = maxDepthSamps * std::sin (cloudLfoPhases[line] + phaseOff);
                const float baseDelay = sizeSamps + maxDepthSamps + 1.f;  // centre + headroom
                float       readPos   = (float)wp - baseDelay - mod;
                int         rInt      = (int)std::floor (readPos);
                const float rFrac     = readPos - (float)rInt;
                rInt    = ((rInt % bufSamps) + bufSamps) % bufSamps;
                const int rInt1 = (rInt + 1) % bufSamps;

                lineSum += buf[(size_t)rInt] * (1.f - rFrac) + buf[(size_t)rInt1] * rFrac;

                cloudWritePtrs[ch][line] = (wp + 1) % bufSamps;
            }

            const float cloudOut = lineSum / (float)kNumCloudLines;
            cloudBuffer.setSample (ch, i, cloudOut);  // store for IR feed (next block) and volume

            if (volume > 0.f)
                buffer.setSample (ch, i, input + cloudOut * volume);
        }
    }
    cloudLastBlockSize = numSamples;
}
else
{
    // cloudOn = false: clear buffer so IR feed injection in the next block is silent
    cloudBuffer.clear();
    cloudLastBlockSize = 0;
}
```

**No Insertion 3:** Unlike Bloom, Cloud does not write a separate feedback circular buffer after EQ. The bridge buffer `cloudBuffer` IS the feedback store; it persists between blocks automatically.

---

## `PluginProcessor.h` — IDs namespace

After `bloomVolume`:

```cpp
// Cloud Multi-LFO
static const juce::String cloudOn       { "cloudOn" };
static const juce::String cloudDepth    { "cloudDepth" };
static const juce::String cloudRate     { "cloudRate" };
static const juce::String cloudSize     { "cloudSize" };
static const juce::String cloudVolume   { "cloudVolume" };
static const juce::String cloudIRFeed   { "cloudIRFeed" };
```

## `createParameterLayout()` additions

After the Bloom parameters:

```cpp
// Cloud Multi-LFO
layout.add (std::make_unique<juce::AudioParameterBool>  (IDs::cloudOn,     "Cloud On",      false));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudDepth,  "Cloud Depth",   0.0f, 1.0f,   0.3f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudRate,   "Cloud Rate",    0.1f, 4.0f,   1.0f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudSize,   "Cloud Size",    1.0f, 40.0f,  5.0f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudVolume, "Cloud Volume",  0.0f, 1.0f,   0.0f));
layout.add (std::make_unique<juce::AudioParameterFloat> (IDs::cloudIRFeed, "Cloud IR Feed", 0.0f, 1.0f,   0.0f));
```

---

## UI — Row 5 "Cloud multi-LFO"

### `PluginEditor.h` additions

After the Bloom controls:

```cpp
// Cloud Multi-LFO (Row 5)
juce::Slider       cloudDepthSlider, cloudRateSlider, cloudSizeSlider,
                   cloudIRFeedSlider, cloudVolumeSlider;
juce::ToggleButton cloudOnButton;
juce::Label        cloudDepthLabel,  cloudRateLabel,  cloudSizeLabel,
                   cloudIRFeedLabel, cloudVolumeLabel;
juce::Label        cloudDepthReadout, cloudRateReadout, cloudSizeReadout,
                   cloudIRFeedReadout, cloudVolumeReadout;
std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
    cloudDepthAttach, cloudRateAttach, cloudSizeAttach,
    cloudIRFeedAttach, cloudVolumeAttach;
std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
    cloudOnAttach;
juce::Rectangle<int> cloudGroupBounds;
```

### `PluginEditor.cpp` constructor

Pattern identical to the Bloom row:

```cpp
// Cloud controls
makeSlider (cloudDepthSlider);
cloudDepthSlider.setRange (0.0, 1.0, 0.0);
cloudDepthAttach = std::make_unique<APVTS::SliderAttachment> (
    pingProcessor.apvts, IDs::cloudDepth, cloudDepthSlider);
cloudDepthLabel.setText ("DEPTH", juce::dontSendNotification);

makeSlider (cloudRateSlider);
cloudRateSlider.setRange (0.1, 4.0, 0.0);
cloudRateAttach = std::make_unique<APVTS::SliderAttachment> (
    pingProcessor.apvts, IDs::cloudRate, cloudRateSlider);
cloudRateLabel.setText ("RATE", juce::dontSendNotification);

makeSlider (cloudSizeSlider);
cloudSizeSlider.setRange (1.0, 40.0, 0.1);
cloudSizeAttach = std::make_unique<APVTS::SliderAttachment> (
    pingProcessor.apvts, IDs::cloudSize, cloudSizeSlider);
cloudSizeLabel.setText ("SIZE", juce::dontSendNotification);

makeSlider (cloudIRFeedSlider);
cloudIRFeedSlider.setRange (0.0, 1.0, 0.0);
cloudIRFeedAttach = std::make_unique<APVTS::SliderAttachment> (
    pingProcessor.apvts, IDs::cloudIRFeed, cloudIRFeedSlider);
cloudIRFeedLabel.setText ("IR FEED", juce::dontSendNotification);

makeSlider (cloudVolumeSlider);
cloudVolumeSlider.setRange (0.0, 1.0, 0.0);
cloudVolumeAttach = std::make_unique<APVTS::SliderAttachment> (
    pingProcessor.apvts, IDs::cloudVolume, cloudVolumeSlider);
cloudVolumeLabel.setText ("VOLUME", juce::dontSendNotification);

cloudOnButton.setButtonText ("");
cloudOnButton.setComponentID ("CloudSwitch");
cloudOnButton.onClick = [this] { repaint(); };
cloudOnAttach = std::make_unique<APVTS::ButtonAttachment> (
    pingProcessor.apvts, IDs::cloudOn, cloudOnButton);
addAndMakeVisible (cloudOnButton);
```

Apply `makeLabel` / `makeReadout` for all 5 labels and readouts, and `addAndMakeVisible` for all controls.

### `paint()`

```cpp
drawGroupHeader (cloudGroupBounds, "Cloud multi-LFO", cloudOnButton.getToggleState());
```

### `resized()`

```cpp
const int row5TotalH_  = row2TotalH_;   // identical formula
const int row5AbsY     = row4AbsY + row4TotalH_;

// Place Row 5 knobs — same pattern as rows 3/4
auto placeRow5Knob = [&](juce::Slider& sl, juce::Label& lb, juce::Label& rd, int idx, int extraGap = 0)
{
    const int x = rowStartX + idx * rowStep + extraGap;
    const int y = row5AbsY  + groupLabelH;
    sl.setBounds (x, y, rowKnobSize, rowKnobSize);
    rd.setBounds (x, y + rowKnobSize + 1, rowKnobSize, readoutH);
    lb.setBounds (x, y + rowKnobSize + readoutH + 2, rowKnobSize, labelH);
};

placeRow5Knob (cloudDepthSlider,  cloudDepthLabel,  cloudDepthReadout,  0);
placeRow5Knob (cloudRateSlider,   cloudRateLabel,   cloudRateReadout,   1);
placeRow5Knob (cloudSizeSlider,   cloudSizeLabel,   cloudSizeReadout,   2);
placeRow5Knob (cloudIRFeedSlider, cloudIRFeedLabel, cloudIRFeedReadout, 3);
placeRow5Knob (cloudVolumeSlider, cloudVolumeLabel, cloudVolumeReadout, 4);

auto row5Area = mainArea.removeFromTop (row5TotalH_);
(void) row5Area;

// Group header spans knobs 0..4
cloudGroupBounds = juce::Rectangle<int> (
    cloudDepthSlider.getX(),   row5AbsY,
    cloudVolumeSlider.getRight() - cloudDepthSlider.getX(),   groupLabelH);

// Pill switch: right-aligned inside the group header
const int swW = 44, swH = 20;
cloudOnButton.setBounds (cloudGroupBounds.getRight() - swW - 4,
                         cloudGroupBounds.getCentreY() - swH / 2, swW, swH);
```

**Update 6-knob grid Y anchor** — change the two occurrences from `row4AbsY + row4TotalH_ + 70` to `row5AbsY + row5TotalH_ + 70`.

**Editor height** — bump from `672` → `744` (`editorH = 744`).

### `setMainPanelControlsVisible()`

Add `setVisible(visible)` calls for all Cloud slider/label/readout/button members (17 calls total, same count as Bloom).

### `updateAllReadouts()`

```cpp
cloudDepthReadout.setText  (juce::String (v ("cloudDepth"),  2),         juce::dontSendNotification);
cloudRateReadout.setText   (juce::String (v ("cloudRate"),   2) + "×",   juce::dontSendNotification);
cloudSizeReadout.setText   (juce::String (v ("cloudSize"),   1) + " ms", juce::dontSendNotification);
cloudIRFeedReadout.setText (juce::String (v ("cloudIRFeed"), 2),         juce::dontSendNotification);
cloudVolumeReadout.setText (juce::String (v ("cloudVolume"), 2),         juce::dontSendNotification);
```

---

## Tests — DSP_05 and DSP_06

Both existing Cloud tests are **self-contained reference specs** that define their own local Cloud implementation; they do not test production code directly.

- **DSP_05** (LFO rate spacing) — unchanged. Tests the geometric rate formula, which is identical in the new plan.
- **DSP_06** (does not amplify) — unchanged. Tests the delay bank's mathematical output using the original in-place blend formula (not the new additive-injection approach). The test remains valid as a check of the underlying delay-bank algorithm and will continue to pass.

No new tests are required for the Cloud output paths (`cloudVolume`, `cloudIRFeed`) — these follow the same additive-injection pattern already validated for Bloom and Plate, and don't introduce novel stability risks.

---

## Differences from original `Plan-HybridModes.md`

| Aspect | Original plan | This plan |
|---|---|---|
| Output path | In-place wet blend: `input*(1-depth*0.7) + avg*depth*0.7` | Additive injection via `cloudVolume`; main signal unchanged |
| `cloudDepth` role | Controls both modulation depth AND blend amount | Controls modulation depth only |
| Default state | On = always modulates wet signal | On = no effect (both cloudVolume=0 and cloudIRFeed=0) |
| IR feed | Not in original plan | New: 1-block deferred feedback to convolver input |
| Feedback buffer | Not needed | `cloudBuffer` persists between blocks (no separate circular buffer) |
| Insertion count | 1 (post-Tail Chorus) | 2 (pre-conv IR feed inject + post-Tail Chorus Cloud run) |

---

## Signal chain position (updated)

```
...
Tail Chorus Modulation
  │
  ▼
[Cloud Insertion 2: run wet through 8-line LFO bank → cloudBuffer;
 add cloudBuffer × cloudVolume to wet signal]   (if cloudOn)
  │
  ▼
Output Gain
  │
  ...
  ▼
[Next block pre-convolution — Cloud Insertion 1:
 inject cloudBuffer × cloudIRFeed into main signal]   (if cloudOn && cloudIRFeed > 0)
```

---

## CLAUDE.md updates required

- Parameters table: add 6 Cloud rows after `bloomVolume`
- Signal flow diagram: add Cloud Insertion 1 and Insertion 2 annotations
- New section: `## Cloud multi-LFO (Feature 3 — implemented)`
- Design decisions: update `editorH` to 744, update 6-knob grid to row5, add Cloud-specific design decisions:
  - Cloud bridge buffer persists between blocks (no separate feedback circular buffer)
  - cloudDepth is modulation depth only, not a blend
  - IR feed is 1-block deferred (post→pre ordering)
  - DSP_05 / DSP_06 remain valid self-contained reference tests
  - R channel π-offset for stereo decorrelation of Cloud LFOs
