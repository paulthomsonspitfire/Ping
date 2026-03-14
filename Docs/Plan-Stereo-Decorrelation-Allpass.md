# Plan: Stereo Decorrelation Allpass Network on FDN Output

## Goal

Add a short allpass filter on the **right** wet channel only to create time–frequency decorrelation between L and R. This reduces stereo collapse at strong FDN modes while preserving the mono sum (allpass has unity gain) and avoiding audible comb-filtering.

---

## Current Behaviour

- **Width** is applied as M/S gain in `PluginProcessor::applyWidth()`: `m = (L+R)/2`, `s = (L-R)/2`, `s *= width`, then `L = m+s`, `R = m-s`. This only rescales existing L/R difference; it does not create new uncorrelated content.
- **FDN:** The synth runs two 16-line FDNs (seeds 100 and 101) to produce `tL` and `tR`; the tail is already L/R. Nevertheless, both channels share the same resonant structure, so at certain frequencies the stereo image can still collapse.
- **Wet path order (processBlock):** Convolution → Input gain → Saturator → (ER+Tail mix) → EQ → (optional LFO mod) → **applyWidth** → (optional tail chorus) → Output gain → Dry/wet mix.

---

## Proposed Change

Insert a **stereo decorrelation allpass** that processes **only the R channel** of the wet buffer. Place it **after EQ and before applyWidth** so that:
1. The reverb (including FDN tail) is fully formed and equalised.
2. R is decorrelated from L.
3. Width then scales the now-more-uncorrelated M/S content.

### Why R only?

- Allpass has unity gain, so `L + R` (mono sum) is unchanged.
- Phase/time differences on R alone are enough to create a wider, more stable stereo image (see CCRMA-style decorrelation).

### Why these delays?

- **7.13 ms** and **14.27 ms** (and optionally a third, e.g. **~21.4 ms**) are incommensurate with typical FDN prime-based delays (e.g. 17.1 ms, 31.7 ms in the existing diffusers), so no single repeat dominates.
- Short enough that comb-filtering is in the low end (~140 Hz, ~70 Hz) and not musically objectionable; above ~200 Hz the effect is mainly phase decorrelation.

---

## Implementation Outline

### 1. Decorrelation allpass in the plugin (no IR Synth change)

- **Location:** `Source/PluginProcessor.cpp` / `PluginProcessor.h`.
- **New member:** A small allpass state for the **R channel only**:
  - 2 or 3 stages (recommend **2** for simplicity: 7.13 ms, 14.27 ms).
  - Delay lengths in samples: `round(7.13 * sr / 1000)`, `round(14.27 * sr / 1000)`.
  - Coefficient `g` in the range **0.4–0.6** (e.g. 0.5) so decorrelation is audible but not metallic.
- **Processing:** In `processBlock()`, after the EQ and before `applyWidth()`:
  - If `numChannels >= 2`, run each sample of the **R** channel (channel 1) through the allpass in sequence; leave L unchanged.
- **Reset on sample-rate or buffer change:** In `prepareToPlay()`, clear the delay buffers and reset write pointers so there’s no click when SR or block size changes.

### 2. Allpass structure (match existing pattern)

Reuse the same topology as `IRSynthEngine::AllpassDiffuser::process()`:

- Per stage: `w = x + g * delayed`, `buf[ptr] = w`, `ptr = (ptr+1) % delay`, `y = -g*w + delayed`.
- Cascade: output of stage 1 → input of stage 2 (and optionally stage 3).

So you need:

- 2 (or 3) delay lengths in samples.
- 2 (or 3) circular buffers and read/write pointers.
- One `g` (shared or per-stage; shared is fine to start).

### 3. Suggested constants

- Delay 1: **7.13 ms**
- Delay 2: **14.27 ms**
- `g`: **0.5** (tweak later if needed)

Sample-rate dependent:

```cpp
int d1 = (int)std::round(7.13f * sampleRate / 1000.0f);
int d2 = (int)std::round(14.27f * sampleRate / 1000.0f);
```

Ensure `d1`, `d2` are at least 1 to avoid division-by-zero.

### 4. Where in the signal flow

```
... → EQ (low/mid/high) → [NEW: R-channel decorrelation allpass] → (LFO mod) → applyWidth → (tail chorus) → output gain → dry/wet ...
```

So: **immediately before** the existing `applyWidth(buffer, widthVal)` call, run the R channel through the decorrelation allpass.

### 5. Edge cases

- **Mono (numChannels < 2):** Skip the decorrelation block; nothing to do.
- **prepareToPlay:** Reallocate delay buffers for the current sample rate and reset pointers (and clear buffer contents) so no old tail bleeds across a SR change.
- **Bypass:** If you ever add a “stereo width off” or “mono reverb” mode, you can skip the allpass when that mode is on; for the default, always run it.

---

## Files to Touch

| File | Change |
|------|--------|
| `Source/PluginProcessor.h` | Add a small struct or member state for the R-only allpass (delays, buffers, pointers, `g`). Declare something like `void processDecorrelateR (juce::AudioBuffer<float>& wet);` if you want a helper. |
| `Source/PluginProcessor.cpp` | In `prepareToPlay()`, initialise or resize the decorrelation allpass state. In `processBlock()`, after EQ and before `applyWidth()`, call the R-channel allpass (or inline the loop over the R channel). |

No changes to the IR Synth engine or to the installer/version.

---

## Testing

- **Mono sum:** Feed a stereo test signal; sum L+R to mono. With decorrelation on, the mono sum should match the sum when the allpass is bypassed (within numerical precision).
- **Stereo:** Compare with Width at 1.0: the image should feel wider and less “collapsed” on resonant tail frequencies than without the allpass.
- **SR change:** Switch project sample rate; no clicks or stuck echoes.

---

## Reference

- Stanford CCRMA (2016) and similar work on allpass-based decorrelation for reverb: short, incommensurate allpass delays on one channel provide inter-channel phase decorrelation without adding energy or changing the mono sum.
