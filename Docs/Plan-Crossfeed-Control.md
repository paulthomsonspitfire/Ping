# Plan: Post-convolution crossfeed control (ER and Tail)

Add a post-convolution crossfeed block with **six controls**: for each path (ER and Tail), an **on/off switch**, **delay** (5–15 ms), and **attenuation**. L→R and R→L each get a delayed, attenuated copy of the opposite channel when that path is on. Controls are placed on the **Placement** tab (IR Synth dialog) below the Height control: a block of 4 knobs (2×2) plus 2 on/off switches, matching the main UI’s left-side knob design. **No changes to IR Synth engine code.**

---

## 1. Behaviour

- **Crossfeed:** For each channel, add a **delayed** and **attenuated** copy of the **opposite** channel.
  - L_out = L_wet + att_LtoR × delay(R_wet)  (R→L crossfeed)
  - R_out = R_wet + att_RtoL × delay(L_wet)  (L→R crossfeed)
  - Same formula for both ER and Tail, with separate delay and attenuation.
- **Separate ER and Tail:** ER crossfeed is applied to the early-reflection buffer(s); tail crossfeed to the tail buffer(s). Then ER and tail are mixed with the existing erLevel/tailLevel as they are now.
- **True-stereo path:** We have `lEr`, `rEr`, `lTail`, `rTail`. Apply ER crossfeed to `lEr`/`rEr`, tail crossfeed to `lTail`/`rTail`, then sum with gains.
- **Stereo (2ch) path:** We have `buffer` (ER) and `tailBuffer` (tail). Apply ER crossfeed to `buffer`, tail crossfeed to `tailBuffer`, then mix into `buffer` with erG/tailG.
- **Parameters:** All six are global plugin parameters (saved with session). Each path (ER, Tail) has an **on/off switch**; when off, that path’s crossfeed is skipped entirely (no reliance on delay or attenuation extremes).

---

## 2. Parameters (APVTS)

| ID | Name (display) | Type | Range / Default | Notes |
|----|-----------------|------|------------------|--------|
| `erCrossfeedOn`   | ER Crossfeed On   | bool | default **false** | When false, ER crossfeed is skipped. |
| `erCrossfeedDelayMs`  | ER Crossfeed Delay  | float | 5–15 ms, default 10   | Linear or 1 ms steps |
| `erCrossfeedAttDb`    | ER Crossfeed Att    | float | -24–0 dB, default -6   | dB, 0 = no attenuation |
| `tailCrossfeedOn`  | Tail Crossfeed On  | bool | default **false** | When false, tail crossfeed is skipped. |
| `tailCrossfeedDelayMs` | Tail Crossfeed Delay| float | 5–15 ms, default 10   | Same as ER |
| `tailCrossfeedAttDb`  | Tail Crossfeed Att  | float | -24–0 dB, default -6   | Same as ER |

- **On/off:** Each path has an explicit switch. Processing for that path is applied only when the switch is on; delay/att knobs have no “off” position.
- Default 10 ms / -6 dB (when on) matches the “~10 ms, ~6 dB” crossfeed described for Cinematic Rooms.
- Attenuation in dB: gain = `juce::Decibels::decibelsToGain(attDb)`. At -24 dB the crossfeed is very low; at 0 dB it’s full level (may be very wet).

---

## 3. Processor (PluginProcessor)

### 3.1 Parameter layout

- In `createParameterLayout()` add the six parameters: two `AudioParameterBool` (erCrossfeedOn, tailCrossfeedOn) and four `AudioParameterFloat` (delay and att for each path).
- No new parameter struct; they live only in APVTS.

### 3.2 State (PluginProcessor.h)

- **Delay lines:** Four mono delay lines (circular buffers), two for ER, two for Tail:
  - `crossfeedErDelayL`  — delay line for R→L (so we delay R and add to L)
  - `crossfeedErDelayR`  — delay line for L→R
  - `crossfeedTailDelayL` — tail R→L
  - `crossfeedTailDelayR` — tail L→R
- **Length:** Each buffer length = `maxDelaySamples = (int)ceil(0.015 * sampleRate) + 1` (15 ms at max).
- **Indices:** Write index per line (single int or size_t per line); read index = (write - delaySamples) mod length. So we need:
  - `std::vector<float> crossfeedErBufL`, `crossfeedErBufR`, `crossfeedTailBufL`, `crossfeedTailBufR`;
  - `int crossfeedErWriteL`, `crossfeedErWriteR`, `crossfeedTailWriteL`, `crossfeedTailWriteR`;
- Or one struct per “pair” (L and R) and four such structs. Keep implementation simple (e.g. four buffers, four write pointers).

### 3.3 prepareToPlay

- Compute `maxDelaySamples = (int)std::ceil(0.015 * sampleRate) + 1`.
- Resize the four buffers to `maxDelaySamples`, clear to zero, reset write indices to 0.

### 3.4 processBlock — True-stereo

- After building `lEr`, `rEr`, `lTail`, `rTail` (and before applying erLevel/tailLevel and summing into `buffer`):
  1. If **ER crossfeed on:** read `erCrossfeedOn`, `erDelayMs`, `erAttDb` → `erDelaySamps`, `erGain`; apply ER crossfeed in place to `lEr`/`rEr` (push L into delayR, R into delayL; L += gain×read(delayR), R += gain×read(delayL)). If ER crossfeed off, skip this step (but still advance/write delay lines with dry signal so state stays in sync, or skip writing when off — see 3.6).
  2. If **Tail crossfeed on:** read tail params; apply same crossfeed to `lTail`/`rTail`. If tail crossfeed off, skip.
  3. Sum into buffer as now: `buffer.L = (lEr*erG + lTail*tailG) * trueStereoWetGain`, same for R.

- **Detail:** For “R→L” we add delayed R into L. So we need to **delay the R channel** and add it to L. So:
  - Delay line “for L” (delayR) holds **R** samples; we read it and add to L.
  - Delay line “for R” (delayL) holds **L** samples; we read it and add to R.
  - Each line is written with the **opposite** channel. So: write rEr[i] into erBufL, write lEr[i] into erBufR; lEr[i] += gain * readFrom(erBufL), rEr[i] += gain * readFrom(erBufR). Naming: erBufL = buffer that stores R (for feeding into L). So naming like `erDelayRtoL` (buffer holding R, we read and add to L) and `erDelayLtoR` (buffer holding L, we read and add to R). Implementation: two buffers, two write indices. Buffer 0: we write R, read and add to L. Buffer 1: we write L, read and add to R.

### 3.5 processBlock — Stereo (2ch)

- After `erConvolver.process(erBlock)` and `tailConvolver.process(tailBlock)` (so `buffer` = ER, `tailBuffer` = tail):
  1. If **ER crossfeed on:** apply ER crossfeed to `buffer` (stereo): L += gain × delayed(R), R += gain × delayed(L), using ER delay/att and the ER delay lines.
  2. If **Tail crossfeed on:** apply tail crossfeed to `tailBuffer` (stereo) with tail delay/att and tail delay lines.
  3. Mix: `buffer.setSample(ch, i, buffer.getSample(ch,i)*erG + tailBuffer.getSample(ch,i)*tailG)` as now, then `buffer.applyGain(0.5f)`.

### 3.6 When crossfeed is “off”

- **Off is controlled only by the on/off switch.** When `erCrossfeedOn` is false, do not apply ER crossfeed (skip the ER crossfeed step). When `tailCrossfeedOn` is false, do not apply tail crossfeed. Delay and attenuation knobs have no “off” meaning; they only apply when the switch is on.
- **Delay lines when off:** When a path is off, either (a) skip reading/writing that path’s delay lines entirely, or (b) continue writing input into the delay lines so that when the user turns the path on, the lines don’t contain stale silence. Option (b) is smoother for toggling; option (a) is simpler. Prefer (a): when off, skip that path’s crossfeed block; delay-line state will “catch up” when turned on (brief transient acceptable).

---

## 4. UI: Placement tab (IR Synth dialog)

### 4.1 Where

- **Tab:** Placement (in IRSynthComponent).
- **Position:** Below the **Height** row (below `heightSlider` / `heightValueLabel`), still in the left column.
- **Block:** Two rows (ER, Tail). Each row has **three** controls: **on/off switch** (left), **Delay** knob, **Attenuation** knob. So 3 columns × 2 rows.
- **Design:** Knobs match the 9 knobs on the left of the main UI: **rotary** sliders, label under knob, readout under label, **48 px** knob size. Switches are simple **on/off toggles** (e.g. `juce::ToggleButton`) with a short label (e.g. “ER”, “Tail” or “On”).

### 4.2 Controls to add (IRSynthComponent)

- **Placement tab, left column, below Height:**
  - **Row 1 (ER):** ER Crossfeed On (toggle), ER Crossfeed Delay (knob), ER Crossfeed Att (knob).
  - **Row 2 (Tail):** Tail Crossfeed On (toggle), Tail Crossfeed Delay (knob), Tail Crossfeed Att (knob).
- **Switches:** Two `juce::ToggleButton`s (e.g. “On” or just a checkbox style). Attached to `erCrossfeedOn` and `tailCrossfeedOn` via `ButtonAttachment`. Placed in the first column of each row so each path has its switch beside its two knobs.
- **Knobs:** Rotary, no text box; label under knob (“ER Dly”, “ER Att”, “Tail Dly”, “Tail Att”); readout under label (“10 ms”, “-6 dB”).
- **Sizes (match main left 9 knobs):**
  - Knob size: **48** px. Label height: 12 px, readout height: 10 px.
  - Switch: compact (e.g. 24–28 px wide, 20–24 px high, or same height as knob row). Row height: max(switch height, 48 + 2 + labelH + 2 + readoutH) so about **74 px** per row, two rows ≈ 148 px + gap.

### 4.3 Layout (layoutPlacementTab)

- After the three dimension rows (Width, Depth, Height), leave a small gap, then:
  - **Crossfeed block:** 3 columns × 2 rows.
  - Column 0 (switch): width e.g. 28 px. Column 1 (delay knob): 48 px. Column 2 (att knob): 48 px. Gaps between columns (e.g. 6–8 px).
  - leftColX = same as existing left column.
  - Col0: x = leftColX (switch).
  - Col1: x = leftColX + switchW + gap (delay knob).
  - Col2: x = leftColX + switchW + gap + 48 + gap (att knob).
  - Row 0 (ER): y = current y after height row + gap. Place ER switch, ER delay knob, ER att knob; labels and readouts under the two knobs.
  - Row 1 (Tail): y += rowHeight + gap. Place Tail switch, Tail delay knob, Tail att knob.
  - Then `floorPlanComponent` stays to the right; left column (including crossfeed block) can scroll in the viewport if needed.

### 4.4 Wiring to APVTS

- All six controls are **plugin** parameters (not IR Synth params). They must be attached to `juce::AudioProcessorValueTreeState` in the **main** plugin.
- **IRSynthComponent** does not own the APVTS. The **editor** (PingEditor) owns the APVTS and the IRSynthComponent.
- **Approach:** Pass the APVTS from the editor to the IR Synth component so it can create the attachments and update readouts.
  - Add `void setApvts (juce::AudioProcessorValueTreeState* apvts)` to IRSynthComponent.
  - Store `juce::AudioProcessorValueTreeState* apvts = nullptr;`.
  - In `setApvts`, store the pointer and create **two** `ButtonAttachment` objects (for the two toggles) and **four** `SliderAttachment` objects (for the four knobs). The switches and sliders must already exist (created in IRSynthComponent ctor and added to `placementContent`).
  - In **PingEditor** constructor, after `addChildComponent(irSynthComponent)`, call `irSynthComponent.setApvts(&apvts)`.
- **Readouts:** Update the four readout labels when values change in `timerCallback` (read the four float params from `apvts` and set the readout text). No readout for the switches (on/off is visible on the button).

### 4.5 New members (IRSynthComponent.h)

- **Switches:** `juce::ToggleButton erCrossfeedOnButton`, `tailCrossfeedOnButton`.
- **Sliders:** `juce::Slider erCrossfeedDelaySlider`, `erCrossfeedAttSlider`, `tailCrossfeedDelaySlider`, `tailCrossfeedAttSlider`.
- **Labels:** `juce::Label erCrossfeedDelayLabel`, `erCrossfeedAttLabel`, `tailCrossfeedDelayLabel`, `tailCrossfeedAttLabel`.
- **Readouts:** `juce::Label erCrossfeedDelayReadout`, `erCrossfeedAttReadout`, `tailCrossfeedDelayReadout`, `tailCrossfeedAttReadout`.
- **APVTS:** `juce::AudioProcessorValueTreeState* apvts = nullptr;`
- **Attachments:** `std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> erCrossfeedOnAttach`, `tailCrossfeedOnAttach`; and `std::unique_ptr<SliderAttachment> erCrossfeedDelayAttach`, `erCrossfeedAttAttach`, `tailCrossfeedDelayAttach`, `tailCrossfeedAttAttach` (six attachments total).

### 4.6 Styling

- Use the same look as the main UI knobs: `PingLookAndFeel` if the IR Synth dialog uses it, or the same colours (accent, panel border, text dim) so the crossfeed block matches the “9 knobs” style. Slider style: `RotaryHorizontalVerticalDrag`, no text box. Toggle buttons: compact, same colour scheme (e.g. accent when on, panel border when off); label “On” or a short label so the switch meaning is clear.

---

## 5. File checklist

| File | Changes |
|------|--------|
| **PluginProcessor.h** | Add 4 delay buffers + 4 write indices (or 2 per “path”); no new public API beyond params. |
| **PluginProcessor.cpp** | Register 6 params in `createParameterLayout` (2 bool, 4 float). In `prepareToPlay`, allocate/reset delay lines. In `processBlock`, after building ER/tail buffers (true-stereo and stereo), apply ER crossfeed only if `erCrossfeedOn`, tail crossfeed only if `tailCrossfeedOn`, then sum. |
| **IRSynthComponent.h** | Add 2 toggle buttons, 4 sliders, 4 labels, 4 readouts, `setApvts`, `apvts` pointer, 2 ButtonAttachments, 4 SliderAttachments. |
| **IRSynthComponent.cpp** | In ctor: create 2 toggles (“On” or similar), 4 sliders (rotary, ranges 5–15 ms and -24–0 dB), 4 labels, 4 readouts; add to `placementContent`. In `setApvts`: create 2 ButtonAttachments and 4 SliderAttachments. In `layoutPlacementTab`: layout the 3×2 block (switch, delay, att per row) below Height. In `timerCallback`: update the 4 readouts from apvts when apvts is set. |
| **PluginEditor.cpp** | After constructing and adding `irSynthComponent`, call `irSynthComponent.setApvts(&apvts)`. |
| **CLAUDE.md** (optional) | Add crossfeed to signal flow and a short design bullet. |

---

## 6. Signal flow (after implementation)

- Convolution (ER + Tail, separate or combined) → **ER crossfeed** (on ER buffer(s)) → **Tail crossfeed** (on tail buffer(s)) → mix with erLevel/tailLevel → (rest unchanged: EQ, decorrelation allpass, LFO, width, tail chorus, output gain, dry/wet).

---

## 7. Testing

- True-stereo: ER crossfeed (when on) affects early portion; tail crossfeed (when on) affects tail. With switch off, that path has no crossfeed.
- Stereo: Same: ER/Tail crossfeed applied only when respective switch is on.
- Switches off by default: no crossfeed until user enables ER and/or Tail.
- Switches on, 10 ms / -6 dB: natural L↔R bleed without collapsing image.
- Preset/session: All six params (two bool, four float) save and load with the plugin state.

---

## 8. Out of scope (per user)

- **IR Synth code:** No changes in IRSynthEngine, IRSynthComponent (except UI and APVTS wiring above), or any synthesis/convolution loading logic.
- **Extra features:** No filtering on the crossfeed path (e.g. “echo rolloff”) in this plan; can be added later if desired.
