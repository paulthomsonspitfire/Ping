# P!NG User Manual

## 1. Overview

P!NG is an impulse response (IR) reverb plugin for macOS. It convolves your audio with IR files to add reverb, and includes a built-in IR synthesiser to generate new IRs from room geometry, materials, and speaker/mic placement.

---

## 2. Main Interface

*[Insert screenshot: Main P!NG interface showing header bar (preset, metres), centre knobs, IR selector, waveform, EQ, and hybrid effect rows]*

### 2.1 Top Row

| Element | Description |
|--------|-------------|
| **Preset folder** | Choose a subfolder to save into (editable — type a new name to create one). |
| **Preset menu** | Browse and load presets. Factory presets appear first, followed by your own. |
| **Save preset** | Saves the current settings under the name shown in the preset combo (editable). |
| **Output level metres** | Visual feedback of wet output level. |

Presets are organised into two sections in the dropdown:

- **Factory** — presets installed with P!NG, grouped by category (Halls, Large Spaces, Rooms, Scoring Stages, Tight Spaces). These are read-only.
- **Your Presets** — presets you have saved, optionally organised into subfolders.

To save into a subfolder, type or select a folder name in the **Preset folder** combo before clicking Save. Leave it blank (or set to `(no folder)`) to save at the top level of your preset directory.

### 2.2 IR Selection & Waveform

| Control | Range | Description |
|---------|--------|-------------|
| **IR dropdown** | — | Browse and load an IR. **Synthesized IR** appears when you have generated an IR with the IR Synth. |
| **IR Synth** button | — | Opens the IR Synthesiser page. |
| **REVERSE** | Toggle | Time-reverses the IR for preverb-style effects. Also works with synthesised IRs. |
| **Waveform display** | — | Shows the current IR on a dB scale. Drag the trim handle when Reverse is engaged to trim from the start of the reversed IR. |

The IR dropdown is organised into two sections:

- **Factory** — IRs installed with P!NG, grouped by category (Halls, Large Spaces, Rooms, Scoring Stages, Tight Spaces). These are read-only and shared across all users on the machine.
- **Your IRs** — any `.wav` or `.aiff` files you have placed in `~/Documents/P!NG/IRs/`.

### 2.3 Mix & Reverb Controls

*[Insert screenshot: Centre panel with Dry/Wet, Predelay, Damping, Stretch, Width, etc.]*

| Control | Range | Description |
|---------|--------|-------------|
| **Dry / Wet** | 0–100% | Balance between dry (original) and wet (reverb) signal. |
| **Predelay** | 0–500 ms | Delay before the reverb starts. |
| **Damping** | 0–100% | High-frequency damping. Higher = more dull decay. |
| **Stretch** | 0.5–2.0 | Time-stretches the IR. 1.0 = no change. |
| **WET OUT TRIM** | −24 to +12 dB | Output level trim for the wet signal only. |

### 2.4 Stereo & Modulation

| Control | Range | Description |
|---------|--------|-------------|
| **Width** | 0–2 | Stereo width of the wet signal. 1 = normal, <1 = narrower, >1 = wider. |
| **ER** (Early Reflections) | −48 to +6 dB | Level of the early reflection portion of the IR. |
| **Tail** | −48 to +6 dB | Level of the late reverb tail. |

#### Tail AM mod

| Control | Range | Description |
|---------|--------|-------------|
| **LFO Depth** | 0–1 | Depth of LFO amplitude modulation applied to the tail. |
| **LFO Rate** | 0.01–2 Hz | Speed of the LFO modulation (display-inverted: left = slow, right = fast). |

#### Tail Frq mod

| Control | Range | Description |
|---------|--------|-------------|
| **Tail Mod** | 0–1 | Amount of chorus-style modulation on the reverb tail. |
| **Delay Depth** | 0.5–8 ms | Modulated delay depth. |
| **Rate** | 0.05–3 Hz | Rate of tail modulation. |

### 2.5 IR Input

| Control | Range | Description |
|---------|--------|-------------|
| **IR Input Gain** | −24 to +12 dB | Gain applied to the signal before convolution. |
| **IR Input Drive** | 0–1 | Soft-clip saturation before convolution. Adds harmonic warmth at higher settings. |

### 2.6 Early Reflections & Tail Balance

| Control | Range | Description |
|---------|--------|-------------|
| **ER** (Early Reflections) | −48 to +6 dB | Level of the early reflection portion of the IR. |
| **Tail** | −48 to +6 dB | Level of the late reverb tail. |

Use these to rebalance IRs that have strong early or late content.

#### ER Crossfade

Adds a delayed, attenuated copy of the opposite channel into the early reflections, improving stereo imaging for the attack of the reverb.

| Control | Range | Description |
|---------|--------|-------------|
| **On/Off** | Toggle | Enables ER crossfeed. |
| **Delay** | 5–15 ms | Delay of the cross-channel copy. |
| **Att** | −24–0 dB | Attenuation of the cross-channel copy. |

#### Tail Crossfade

The same crossfeed concept applied independently to the reverb tail.

| Control | Range | Description |
|---------|--------|-------------|
| **On/Off** | Toggle | Enables Tail crossfeed. |
| **Delay** | 5–15 ms | Delay of the cross-channel copy. |
| **Att** | −24–0 dB | Attenuation of the cross-channel copy. |

Crossfeed controls are live — they do not trigger a new IR render.

### 2.7 EQ Section

*[Insert screenshot: EQ graph with five bands]*

A 5-band parametric EQ shapes the wet reverb signal. Bands are arranged left to right:

| Band | Type | Default Freq | Range |
|------|------|-------------|-------|
| **LOW** | Low shelf | 200 Hz | 20–1200 Hz |
| **MID 1** | Peak | 400 Hz | 50–16k Hz |
| **MID 2** | Peak | 1000 Hz | 50–16k Hz |
| **MID 3** | Peak | 4000 Hz | 50–16k Hz |
| **HIGH** | High shelf | 8000 Hz | 2000–20k Hz |

All bands offer ±12 dB of gain and adjustable Q (0.3–10 for peaks; 0.3–2.0 slope for shelves). Drag handles on the EQ graph or use the per-band FREQ/GAIN/Q knobs below the graph.

---

## 3. Hybrid Effects

P!NG includes four hybrid effects that run before the convolution engine, adding textural layers on top of (or feeding into) the reverb. Each has its own power button and an **IR Feed** knob that controls how much of its output is sent into the convolver.

### 3.1 Plate Pre-Diffuser

*[Insert screenshot: Plate row]*

A six-stage allpass cascade applied to the input signal before convolution. It smears transients into a dense, immediate diffuse wash — the characteristic onset of a physical plate reverb — before the IR shapes the resulting sound.

| Control | Range | Description |
|---------|--------|-------------|
| **On/Off** | Toggle | Enables the Plate pre-diffuser. Zero overhead when off. |
| **Diffusion** | 0.30–0.88 | Allpass feedback coefficient. Higher = denser, more metallic diffusion. |
| **Colour** | 0–1 | Tonal character: 0 → warm/dark (2 kHz rolloff, EMT 140 style); 1 → bright (8 kHz, AMS RMX16 style). |
| **Size** | 0.5–14.0 | Scales all six allpass delay times. Larger = longer, more spacious pre-diffusion. |
| **IR Feed** | 0–1 | How much of the diffused plate signal is mixed into the convolver input. |

### 3.2 Bloom Hybrid

*[Insert screenshot: Bloom row]*

A self-reinforcing allpass feedback network that builds a dense, swelling texture from the input. Think of it as a feedback pedal sitting upstream of the reverb: it has its own internal loop that builds independently of the IR, and two output paths — one into the convolver and one directly to the final output.

| Control | Range | Description |
|---------|--------|-------------|
| **On/Off** | Toggle | Enables Bloom. Zero overhead when off. |
| **Size** | 0.25–2.0 | Scales the six allpass delay times (0.25 = very dense; 2.0 = more spacious). |
| **Feedback** | 0–0.65 | Amount of the cascade output fed back into its own input. Higher = more self-sustaining swell. |
| **Time** | 50–500 ms | Feedback tap delay — how far back the feedback reads. Short = fast rhythmic bloom; long = slow expansive sustain. |
| **IR Feed** | 0–1 | How much bloom output feeds the convolver input. Default 0.4 — audible immediately on enable. |
| **Volume** | 0–1 | How much bloom output is added directly to the final mix (after dry/wet blend, independent of the wet level). |

### 3.3 Clouds

*[Insert screenshot: Cloud row]*

A Mutable Instruments Clouds-style granular processor. It captures a 3-second window of audio and plays back randomised grains from it, scattering them across the full buffer so overlapping grains draw from different moments in time.

| Control | Range | Description |
|---------|--------|-------------|
| **On/Off** | Toggle | Enables Cloud. Zero overhead when off. |
| **Width** | 0–1 | Stereo grain scatter and reverse-grain probability. |
| **Density** | 0.1–4.0 | Controls grain spawn rate via an exponential curve. At the default (2.0) roughly 4 grains overlap at a 200 ms grain length. |
| **Length** | 25–1000 ms | Grain length in milliseconds, independent of Density. |
| **Feedback** | 0–0.7 | Mixes previous grain output back into the capture buffer. Creates accumulating, self-reinforcing texture. |
| **IR Feed** | 0–1 | Sends the diffused grain output into the convolver input. |

### 3.4 Shimmer

*[Insert screenshot: Shimmer row]*

An 8-voice harmonic shimmer cloud. All voices read the clean pre-convolution dry signal, pitch-shift it by a configurable interval (and its harmonics), and inject the result into the convolver. Voices build progressively using a staggered onset.

| Control | Range | Description |
|---------|--------|-------------|
| **On/Off** | Toggle | Enables Shimmer. Zero overhead when off. |
| **Pitch** | −24–+24 semitones | The base interval (integer semitones). +12 = one octave. |
| **Length** | 50–500 ms | Grain length in ms. Longer = smoother, more sustained pitch. |
| **Delay** | 0–1000 ms | Dual role: (1) sets the stagger interval between each voice's onset; (2) controls the per-voice delay line period (spread across voices for a denser wash). |
| **IR Feed** | 0–1 | How much shimmer output feeds the convolver. Default 0.5 — audible immediately on enable. |
| **Feedback** | 0–0.7 | Decay time of the per-voice delay lines. Higher = longer ring-out after input stops. |

---

## 4. IR Synthesiser

Click **IR Synth** to open the synthesiser page.

*[Insert screenshot: IR Synth page — left column (room, materials), right column (floor plan), bottom bar]*

The IR Synth page is a single layout with all controls visible simultaneously: room geometry and acoustic character controls on the left, and the interactive floor plan on the right.

### 4.1 Room Geometry

#### Room Shape

| Option | Description |
|--------|-------------|
| Rectangular | Standard box room. |
| L-shaped | L-shaped floor plan. |
| Fan / Shoebox | Tapered or shoebox theatre style. |
| Cylindrical | Circular/cylindrical room. |
| Cathedral | Large cathedral-like space. |
| Octagonal | Octagonal floor plan. |

#### Dimensions (metres)

| Control | Range | Description |
|---------|--------|-------------|
| **Width** | 0.5–50 m | Room width. |
| **Depth** | 0.5–50 m | Room depth. |
| **Height** | 1–30 m | Room height. |

Values can be typed into the number labels next to each slider.

#### Floor Plan

The floor plan shows:

- **Left/Right speakers** (orange pucks): Source positions.
- **Left/Right mics** (blue pucks): Receiver positions.

Drag pucks to move them. Drag the small handle on each puck to rotate its direction. Speaker aim and mic polar orientation affect early reflections and stereo image.

### 4.2 Acoustic Character

#### Surface Materials

Materials are defined by absorption coefficients across eight octave bands (125 Hz to 16 kHz). Separate selectors are provided for floor, ceiling, and walls. A **Windows** slider blends the wall material with glass absorption.

| Material | Typical use |
|----------|-------------|
| Concrete / bare brick | Very reflective. Good for large reverberant spaces. |
| Painted plaster | Moderately reflective. |
| Hardwood floor | Reflective floor. |
| Carpet (thin/thick) | Absorptive floor. |
| Glass (large pane) | Reflective, frequency-dependent. |
| Heavy curtains | Highly absorptive. |
| Acoustic ceiling tile | Absorptive ceiling. |
| Plywood panel | Moderately absorptive. |
| Upholstered seats | Very absorptive (audience). |
| Bare wooden seats | Reflective seating. |
| Water / pool surface | Special case. |
| Rough stone / rock | Moderately reflective. |
| Exposed brick (rough) | Reflective. |

#### Contents

| Control | Range | Description |
|---------|--------|-------------|
| **Audience** | 0–1 | Amount of audience absorption on the floor. |
| **Diffusion** | 0–1 | Scattered/diffuse reflections. Higher = smoother, less specular. |

#### Interior Architecture

| Control | Description |
|---------|-------------|
| **Vault type** | Ceiling geometry: None (flat), Shallow barrel, Deep pointed (gothic), Groin/cross vault, Fan vault, Coffered dome. |
| **Organ case** | 0–1. Absorption and lateral scattering from an organ case. |
| **Balconies** | 0–1. Absorption and geometry from balconies. |

#### Options

| Control | Options | Description |
|---------|---------|-------------|
| **Mic pattern** | omni, subcardioid, cardioid, figure-8 | Simulated microphone polar pattern. |
| **Early reflections only** | Toggle | Renders only early reflections (no FDN tail). Useful for very tight, controlled spaces. |
| **Sample rate** | 44.1 kHz, 48 kHz | Output sample rate of the synthesised IR. |

### 4.3 RT60 Display

The IR Synth shows estimated RT60 (reverberation time) at eight frequencies: 125, 250, 500, 1k, 2k, 4k, 8k, and 16k Hz. These update as you change materials, dimensions, and contents. High-frequency RT60 values reflect air absorption — longer high-frequency times indicate a more "live" sounding space.

### 4.4 Bottom Bar

| Control | Description |
|---------|-------------|
| **IR preset combo** | Load an existing 4-channel IR from your IR folder. Selecting a saved IR recalls its room settings if a `.ping` sidecar is present. |
| **Save** | Saves the current synthesised IR as a 4-channel WAV file (with `.ping` sidecar) under the name you type. |
| **Calculate IR** | Renders the IR and loads it into the plugin. Stays on the IR Synth page so you can continue adjusting. |
| **Main Menu** | Returns to the main plugin UI. |

---

## 5. Technical Explanation: How the IR is Synthesised

P!NG's IR Synthesiser models a room with a geometric acoustic engine and produces a true-stereo, 4-channel IR (Left→Left, Right→Left, Left→Right, Right→Right) for convolution reverb.

### 5.1 Overview

The engine uses:

1. **Image-source method** – Mirror sources for early reflections.
2. **Per-band rendering** – Eight octave bands for frequency-dependent absorption (125 Hz to 16 kHz).
3. **Allpass diffusion** – Smoothes reflections progressively from 65 ms onward.
4. **FDN tail** – 16-delay feedback delay network for the late reverb.

### 5.2 Image-Source Method

The room is treated as a rectangular box (possibly with floor-plan and vault modifiers). Virtual sources are placed by mirroring the real source across floor, ceiling, and walls. Each reflection has a delay from path length, per-band amplitude from surface absorption and air absorption, speaker directivity (cardioid, fading to omnidirectional at higher reflection orders), mic polar pattern gain from arrival angle, and distance attenuation.

### 5.3 Per-Band Rendering

Materials are defined by absorption coefficients at eight octave bands (125–16,000 Hz). The engine splits reflections into these bands, applies band-specific absorption, filters each band with a bandpass around each centre frequency, and sums them into a single time series per channel. This produces frequency-dependent decay (e.g. shorter highs, longer lows).

### 5.4 Diffusion

A 4-stage allpass diffusion network is applied to the summed signal, fading in gradually from 65 ms to 85 ms. This keeps early reflections discrete and punchy while preventing late reflections from becoming spiky or periodic.

### 5.5 Late Reverb (FDN Tail)

For the late tail (after ~85 ms):

1. **Early part** (0–85 ms) is taken from the image-source result.
2. A **16-line feedback delay network (FDN)** is fed with this early energy after a three-stage allpass pre-diffusion cascade.
3. Delays are prime-numbered and scaled by room volume/surface (mean free path).
4. **LFO modulation** with per-line depth (±0.3 to ±1.2 ms, proportional to each line's low-frequency content) reduces metallic resonances. LFO rates follow a geometric sequence (0.07–0.45 Hz) to avoid audible beating between lines.
5. **Hadamard mixing** distributes energy across all 16 lines.
6. **Per-line 1-pole lowpass** – each line decays faster at high frequencies, following the room's RT60 curve. The HF reference uses the 16 kHz RT60 for correct air-absorption modelling.
7. The early and tail portions are crossfaded around 85 ms with dynamic level matching so the tail always follows naturally from the early reflections.

The **Early reflections only** option skips the FDN and outputs only the image-source portion (with a short cosine fade-out at the crossover point).

### 5.6 Output

The result is a 4-channel impulse response:

- **iLL**: Left speaker → Left mic
- **iRL**: Right speaker → Left mic
- **iLR**: Left speaker → Right mic
- **iRR**: Right speaker → Right mic

Each channel is band-limited (20 Hz–18 kHz) and written as a 24-bit WAV. A `.ping` sidecar file stores the synthesis parameters for recall. **Note:** synthesised IRs are not peak-normalised — amplitude follows the physical 1/r law, so moving speakers closer to mics produces a louder wet signal.

### 5.7 RT60 Calculation

RT60 is estimated with the Eyring equation:

*T = 0.161 × V / (−S × ln(1 − α))*

where *V* is volume, *S* is total surface area, and *α* is the average absorption coefficient. Air absorption is included at high frequencies. Audience, balconies, and organ case add extra absorption terms.

---

## 6. IR File Locations & Formats

### User IRs

- **Folder**: `~/Documents/P!NG/IRs/`
- **Formats**: `.wav`, `.aiff`, `.aif`
- Files placed here appear in the **Your IRs** section of the IR dropdown.

### Factory IRs

- **Folder**: `/Library/Application Support/Ping/P!NG/Factory IRs/`
- Installed by the P!NG installer; shared across all users on the machine; read-only.
- Organised into subfolders: **Halls**, **Large Spaces**, **Rooms**, **Scoring Stages**, **Tight Spaces**. Each subfolder appears as a section heading in the Factory part of the IR dropdown.

### Synthesised IRs

Saved from the IR Synth page into `~/Documents/P!NG/IRs/` as 4-channel 24-bit WAV files. A `.ping` sidecar file (same name, `.wav.ping` extension) stores the synthesis parameters and is automatically loaded when that IR is selected, restoring the IR Synth panel to the settings used to generate it. The IR Synth list shows only 4-channel files.

---

## 7. Presets

Presets store all current settings including:

- Main UI parameters (dry/wet, predelay, damping, stretch, width, hybrid effects, crossfeed, EQ, etc.)
- IR selection (file path or Synthesized IR)
- IR Synth parameters (room geometry, materials, placement)

### Factory Presets

- **Location**: `/Library/Application Support/Ping/P!NG/Factory Presets/`
- Installed with P!NG; read-only and shared across all users.
- Organised into subfolders matching the factory IR categories: **Halls**, **Large Spaces**, **Rooms**, **Scoring Stages**, **Tight Spaces**.
- Appear first in the preset dropdown under the **Factory** section heading.

### Your Presets

- **Location**: `~/Library/Audio/Presets/Ping/P!NG/`
- Presets you save appear in the **Your Presets** section of the dropdown.
- You can organise your presets into subfolders using the **Preset folder** combo in the header:
  - Select an existing subfolder from the dropdown, or type a new name to create one automatically on save.
  - Leave it blank (or `(no folder)`) to save at the top level.
  - Subfolders appear as section headings within the Your Presets area of the dropdown.

### Saving a Preset

1. Type a name in the preset combo (or leave the current name to overwrite).
2. Optionally select or type a folder name in the folder combo.
3. Click **Save**. If the file already exists and the name matches the currently selected preset, a confirmation prompt will appear before overwriting.

### Loading a Preset

Click the preset combo to open the list, navigate to the desired preset, and select it. The plugin will load all stored parameters and — if the preset references a specific IR file — reload that IR automatically.
