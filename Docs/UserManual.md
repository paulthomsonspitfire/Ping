# P!NG User Manual

## 1. Overview

P!NG is an impulse response (IR) reverb plugin for macOS. It convolves your audio with IR files to add reverb, and includes a built-in IR synthesiser to generate new IRs from room geometry, materials, and speaker/mic placement.

---

## 2. Main Interface

*[Insert screenshot: Main P!NG interface showing top row (preset, metres), centre knobs, IR selector, waveform, and EQ]*

### 2.1 Top Row

| Element | Description |
|--------|-------------|
| **Preset menu** | Load or save presets. Presets store all settings including IR choice and IR Synth parameters. |
| **Save preset** | Saves the current settings under the name shown in the preset combo (editable). |
| **Output level metres** | Visual feedback of wet output level. |

### 2.2 IR Selection & Waveform

| Control | Range | Description |
|---------|--------|-------------|
| **IR dropdown** | — | Choose an IR from your folder. **Synthesized IR** appears when you’ve just created an IR with the IR Synth and haven’t saved it yet. |
| **IR Synth** button | — | Opens the IR Synthesiser page. |
| **REVERSE** | Toggle | Time-reverses the IR for preverb-style effects. |
| **Waveform display** | — | Shows the current IR. Drag the trim handles to trim the start/end of the IR (applies to file-loaded IRs). |

### 2.3 Mix & Reverb Controls

*[Insert screenshot: Centre panel with Dry/Wet, Predelay, Damping, Stretch, Width, etc.]*

| Control | Range | Description |
|---------|--------|-------------|
| **Dry / Wet** | 0–100% | Balance between dry (original) and wet (reverb) signal. |
| **Predelay** | 0–500 ms | Delay before the reverb starts. |
| **Damping** | 0–100% | High-frequency damping. Higher = more dull decay. |
| **Stretch** | 0.5–2.0 | Time-stretches the IR. 1.0 = no change. |

### 2.4 Stereo & Modulation

| Control | Range | Description |
|---------|--------|-------------|
| **Width** | 0–2 | Stereo width of the wet signal. 1 = normal, &lt;1 = narrower, &gt;1 = wider. |
| **LFO Depth** | 0–1 | Depth of LFO modulation applied to the tail. |
| **LFO Rate** | 0.01–2 Hz | Speed of the LFO modulation. |
| **Tail Mod** | 0–1 | Amount of modulation on the reverb tail. |
| **Delay Depth** | 0.5–8 ms | Modulated delay depth. |
| **Rate** | 0.05–3 Hz | Rate of tail modulation. |

### 2.5 IR Input

| Control | Range | Description |
|---------|--------|-------------|
| **IR Input Gain** | −24 to +12 dB | Gain applied to the signal before convolution. |
| **IR Input Drive** | 0–1 | Saturation/drive before convolution. Higher values add warmth. |

### 2.6 Early Reflections & Tail Balance

| Control | Range | Description |
|---------|--------|-------------|
| **ER** (Early Reflections) | −48 to +6 dB | Level of the early reflection portion of the IR. |
| **Tail** | −48 to +6 dB | Level of the late reverb tail. |

Use these to rebalance IRs that have strong early or late content.

### 2.7 EQ Section

*[Insert screenshot: EQ graph with three bands]*

A 3-band parametric EQ shapes the wet reverb:

- **Band 1**: Default 400 Hz, variable Q (0.3–10). Cut or boost low-mid.
- **Band 2**: Default 1 kHz, variable Q. Mid control.
- **Band 3**: Default 4 kHz, variable Q. High-mid / presence.

Click band buttons **1**, **2**, or **3** to select a band; drag on the graph to change frequency and gain, or use the sliders when they appear.

---

## 3. IR Synthesiser

Click **IR Synth** to open the synthesiser page.

*[Insert screenshot: IR Synth page with Placement and Character tabs, floor plan, bottom bar]*

### 3.1 Placement Tab

*[Insert screenshot: Placement tab – room shape, dimensions, floor plan]*

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

Drag pucks to move them. Drag the small handle on each puck to rotate (speaker aim, mic direction). Positions and angles affect early reflections and stereo image.

### 3.2 Character Tab

*[Insert screenshot: Character tab – materials, audience, diffusion, vault, etc.]*

#### Surface Materials

14 materials per surface; absorption varies by frequency (6 bands: 125, 250, 500, 1k, 2k, 4k Hz):

| Material | Typical use |
|----------|-------------|
| Concrete / bare brick | Very reflective. |
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
| **Organ case** | 0–1. Absorption from organ case. |
| **Balconies** | 0–1. Absorption and geometry from balconies. |

#### Options

| Control | Options | Description |
|---------|---------|-------------|
| **Mic pattern** | omni, subcardioid, cardioid, figure-8 | Simulated microphone polar pattern. |
| **Early reflections only** | Toggle | Renders only early reflections (no FDN tail). |
| **Sample rate** | 44.1 kHz, 48 kHz | Output sample rate of the synthesised IR. |

### 3.3 RT60 Display

The IR Synth shows estimated RT60 (reverberation time) at six frequencies: 125, 250, 500, 1k, 2k, 4k Hz. These update as you change materials, dimensions, and contents.

### 3.4 Bottom Bar

| Control | Description |
|---------|-------------|
| **IR pick list** | Load an existing 4-channel IR from the IR folder. Leave empty before saving a new IR. |
| **Save** | Saves the current IR to the IR folder under the name you type. |
| **Preview** | Renders the IR and loads it into the plugin. Stays on the IR Synth page. |
| **Done** | Returns to the main UI. If you’ve used Preview, the main UI will show “Synthesized IR”. |

---

## 4. Technical Explanation: How the IR is Synthesised

P!NG’s IR Synthesiser models a room with a geometric acoustic engine and produces a true-stereo, 4-channel IR (Left→Left, Right→Left, Left→Right, Right→Right) for convolution reverb.

### 4.1 Overview

The engine uses:

1. **Image-source method** – Mirror sources for early reflections.
2. **Per-band rendering** – Six octave bands for frequency-dependent absorption.
3. **Allpass diffusion** – Smoothes reflections.
4. **FDN tail** – 16-delay feedback delay network for the late reverb.

### 4.2 Image-Source Method

The room is treated as a rectangular box (possibly with floor-plan and vault modifiers). Virtual sources are placed by mirroring the real source across floor, ceiling, and walls. Each reflection:

- Has a **delay** from path length (distance / speed of sound).
- Has **per-band amplitudes** from:
  - Distance attenuation (1/distance)
  - Floor and ceiling absorption (from material coefficients)
  - Wall absorption (from material coefficients)
  - Air absorption (high-frequency loss)
  - **Mic polar pattern** – gain from arrival angle vs mic orientation
  - **Speaker directivity** – modelled as cardioid
  - **Polarity** – flipped on odd numbers of reflections

Room shape (rectangular, L-shaped, cylindrical, etc.) changes the density and distribution of these reflections.

### 4.3 Per-Band Rendering

Materials are defined by absorption coefficients at 125, 250, 500, 1000, 2000, and 4000 Hz. The engine:

1. Splits reflections into six bands.
2. Applies band-specific absorption for each reflection.
3. Filters each band with a bandpass (around each centre frequency).
4. Sums the bands into a single time series per channel.

This produces frequency-dependent decay (e.g. shorter highs, longer lows).

### 4.4 Diffusion

A 4-stage allpass diffusion network is applied to the summed signal. Higher diffusion:

- Increases delay density and smooths the time response.
- Reduces flutter and specular “pinging”.

### 4.5 Late Reverb (FDN Tail)

For the late tail (after ~85 ms):

1. **Early part** (0–85 ms) is taken from the image-source result.
2. A **16-line feedback delay network (FDN)** is fed with this early energy.
3. Delays are prime-numbered and scaled by room volume/surface (mean free path).
4. **LFO modulation** (±1.2 ms) on the delays reduces metallic resonances.
5. **Hadamard mixing** distributes energy across the 16 lines.
6. **Per-line lowpass** – each line has its own decay (from RT60 curves) so highs decay faster than lows.
7. The early and tail portions are crossfaded around 85 ms.

The “Early reflections only” option skips the FDN and outputs only the image-source portion.

### 4.6 Output

The result is a 4-channel impulse response:

- **iLL**: Left speaker → Left mic  
- **iRL**: Right speaker → Left mic  
- **iLR**: Left speaker → Right mic  
- **iRR**: Right speaker → Right mic  

Each channel is band-limited (20 Hz–18 kHz), normalised for headroom, and written as 24-bit WAV. A `.ping` sidecar file stores the synthesis parameters for recall.

### 4.7 RT60 Calculation

RT60 is estimated with the Eyring equation:

*T = 0.161 × V / (−S × ln(1 − α))*

where *V* is volume, *S* is total surface area, and *α* is the average absorption coefficient. Air absorption is included at high frequencies. Audience, balconies, and organ case add extra absorption terms.

---

## 5. IR File Location & Formats

- **Folder**: `~/Documents/P!NG/IRs`
- **Formats**: `.wav`, `.aiff`, `.aif`
- **Synthesised IRs**: 4-channel WAV (24-bit). The IR Synth list shows only 4-channel files.
- **Sidecar**: `.ping` files store synthesis parameters next to the `.wav` for recall of room, materials, and placement.

---

## 6. Presets

Presets store:

- Main UI parameters (dry/wet, predelay, damping, stretch, width, etc.)
- IR selection (file or Synthesized IR)
- IR Synth parameters (room, materials, placement)
- EQ settings

Presets are stored in `~/Documents/P!NG/Presets/`.
