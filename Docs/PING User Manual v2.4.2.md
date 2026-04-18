# P!NG — User Manual

**IR Convolution Reverb · macOS Audio Unit + VST3**

Version 2.4.2

---

## Contents

1. [Overview](#1-overview)
2. [Installation & Activation](#2-installation--activation)
3. [Main Interface](#3-main-interface)
   - 3.1 [Header — Preset & Level Meters](#31-header--preset--level-meters)
   - 3.2 [Centre Column — DRY/WET, IR Selection & Waveform](#32-centre-column--drywet-ir-selection--waveform)
   - 3.3 [IR Input & IR Controls (Row 1)](#33-ir-input--ir-controls-row-1)
   - 3.4 [ER Crossfade & Tail Crossfade (Row 2)](#34-er-crossfade--tail-crossfade-row-2)
   - 3.5 [Plate Pre-Diffuser (Row 3)](#35-plate-pre-diffuser-row-3)
   - 3.6 [Bloom Hybrid (Row 4)](#36-bloom-hybrid-row-4)
   - 3.7 [Clouds Post Convolution (Row R1)](#37-clouds-post-convolution-row-r1)
   - 3.8 [Shimmer (Row R2)](#38-shimmer-row-r2)
   - 3.9 [Tail AM Mod & Tail Frq Mod (Row R3)](#39-tail-am-mod--tail-frq-mod-row-r3)
   - 3.10 [Output Controls](#310-output-controls)
   - 3.11 [EQ Section](#311-eq-section)
   - 3.12 [Level Meter](#312-level-meter)
4. [IR Synthesiser](#4-ir-synthesiser)
   - 4.1 [Surface Materials](#41-surface-materials)
   - 4.2 [Contents](#42-contents)
   - 4.3 [Interior Architecture](#43-interior-architecture)
   - 4.4 [Options](#44-options)
   - 4.5 [Room Shape & Dimensions](#45-room-shape--dimensions)
   - 4.6 [Floor Plan — Speaker & Mic Placement](#46-floor-plan--speaker--mic-placement)
   - 4.7 [RT60 Display](#47-rt60-display)
   - 4.8 [Bottom Bar](#48-bottom-bar)
5. [Technical Overview: How the IR Is Synthesised](#5-technical-overview-how-the-ir-is-synthesised)
6. [IR File Location & Formats](#6-ir-file-location--formats)
7. [Presets](#7-presets)
   - 7.1 [Factory Presets & IRs](#71-factory-presets--irs)
8. [Licence](#8-licence)

---

## 1. Overview

P!NG is an impulse response (IR) reverb plugin for macOS, available as an Audio Unit (AU) for Logic Pro and as a VST3 for compatible DAWs. It convolves your audio with IR files to add reverb, and includes a built-in IR Synthesiser to generate new IRs from room geometry, materials, and speaker/mic placement from first principles.

Version 2.2 added four hybrid processing modules — Plate, Bloom, Cloud, and Shimmer — that work in conjunction with the convolution engine to extend the sonic palette from natural room simulation to creative, ethereal textures. A 5-band parametric EQ, stereo crossfeed controls, and waveform display complete the feature set.

---

## 2. Installation & Activation

Run the supplied .pkg installer. It deploys P!NG system-wide to:

- `/Library/Audio/Plug-Ins/Components/P!NG.component` (AU)
- `/Library/Audio/Plug-Ins/VST3/P!NG.vst3` (VST3)

The installer requires administrator credentials. After installation, rescan plugins in your DAW to make P!NG available.

### Activation

P!NG requires a licence to process audio. On first launch a licence entry screen appears. Enter the serial number provided at purchase exactly as shown (case-insensitive). The licence is stored locally — no internet connection is needed after activation.

The licence file is written to `~/Library/Audio/Ping/licence.xml`. A system-wide licence can also be placed at `/Library/Application Support/Audio/Ping/licence.xml` to cover all user accounts on the machine. If upgrading from an earlier version, existing licence files are migrated to the new location automatically.

> *An unlicensed plugin passes silence — all other controls remain functional for evaluation purposes.*

---

## 3. Main Interface

The P!NG window is fixed at 1104 × 786 px. Controls are arranged in labelled groups across three areas: a header strip at the top, a left column of processing rows (Rows 1–4), a right column of processing rows (Rows R1–R3), and a centre column with the DRY/WET knob, IR selector, waveform, and EQ.

### 3.1 Header — Preset & Level Meters

| Element | Description |
|---|---|
| P!NG logo | Centre of the header. Click to open the licence entry screen if needed. |
| Spitfire Audio logo | Left of the header. |
| PRESET menu | Right of the header. Browse and load presets. Factory presets appear first under a Factory heading; your own presets appear below under Your Presets. Select a preset to load it immediately. |
| Save button | Saves the current plugin state under the name shown in the PRESET box. |

### 3.2 Centre Column — DRY/WET, IR Selection & Waveform

| Control | Range | Description |
|---|---|---|
| DRY / WET | 0–100 % | Balance between dry (unprocessed) and wet (reverb) signal. Uses a constant-power crossfade. |
| IR dropdown | — | Browse and select an IR. The list is divided into two sections: **Factory** — IRs installed with P!NG, grouped into categories (Halls, Large Spaces, Rooms, Scoring Stages, Tight Spaces); **Your IRs** — files from `~/Library/Audio/Impulse Responses/Ping/`. "Synthesized IR" appears at the top when the IR Synth has generated a custom IR. |
| IR Synth button | — | Opens the IR Synthesiser page. |
| REVERSE | Toggle | Time-reverses the IR for preverb effects. When engaged, a trim handle appears on the waveform — drag it to set the start point. |
| Waveform display | — | Shows the loaded IR on a logarithmic dB scale (−60 dB floor). The full decay envelope is visible for any room size. Drag the trim handle in Reverse mode to set the reverb start. |

### 3.3 IR Input & IR Controls (Row 1)

Two groups of knobs immediately below the header. These control the signal going into the convolution engine.

**IR Input group**

| Control | Range | Description |
|---|---|---|
| GAIN | −24 to +12 dB | Gain applied to the signal before convolution. Use this to drive the input harder or softer without affecting the dry signal. |
| DRIVE | 0–1 | Applies harmonic saturation (cubic soft-clip) before convolution. Higher values add warmth and odd-order harmonics to the reverb colour. |

**IR Controls group**

| Control | Range | Description |
|---|---|---|
| PREDELAY | 0–500 ms | Silence before the reverb onset. Useful for creating separation between the dry signal and the reverb. |
| DAMPING | 0–1 | Applies an exponential decay envelope to the IR. Higher values shorten and darken the tail. |
| STRETCH | 0.5–2.0 | Time-stretches the IR without changing pitch. 1.0 = original length. Values above 1 lengthen; below 1 shorten. |

> *DAMPING and STRETCH re-render the IR when changed. A short processing pause is normal on synthesised IRs.*

### 3.4 ER Crossfade & Tail Crossfade (Row 2)

Stereo crossfeed for the early reflection and tail portions of the convolution output. When enabled, a delayed and attenuated copy of the opposite channel is added to each side (L→R and R→L), widening the stereo image and improving pan-tracking. Each path has its own independent on/off switch.

**ER Crossfade group** (acts on early reflections only)

| Control | Range | Description |
|---|---|---|
| DELAY | 5–15 ms | Time before the crossfeed copy arrives. Longer delays add more spatial depth to the early image. |
| ATT | −24 to 0 dB | Attenuation of the crossfeed copy. More attenuation = subtler widening. |
| On/Off switch | On / Off | Enables ER stereo crossfeed. Off by default. |

**Tail Crossfade group** (acts on the reverb tail only)

| Control | Range | Description |
|---|---|---|
| DELAY | 5–15 ms | Crossfeed delay for the tail path. |
| ATT | −24 to 0 dB | Attenuation of the tail crossfeed copy. |
| On/Off switch | On / Off | Enables Tail stereo crossfeed. Off by default. |

> *Crossfeed controls are live — they do not require an IR reload.*

### 3.5 Plate Pre-Diffuser (Row 3)

Plate simulates the fast-building, dense onset of a physical plate reverb. A cascade of six allpass filters is applied to the input before convolution, scattering transients into an undifferentiated cloud. When that cloud passes through the convolution IR, early reflections blur together into the instantly-dense, room-free onset characteristic of a classic plate unit.

IR FEED controls how much of the processed plate signal is injected into the convolver — at 0 the Plate has no audible effect. Use DIFFUSION to dial in the density and COLOUR to shift between a dark EMT 140 character and a brighter AMS RMX16 feel.

| Control | Range | Description |
|---|---|---|
| DIFFUSION | 0.30–0.88 | Allpass feedback coefficient. Higher = denser, more metallic diffusion. Lower = gentler, more transparent scatter. |
| COLOUR | 0–1 | Lowpass cutoff applied to the diffused signal: 0 → 2 kHz (warm, dark); 1 → 8 kHz (bright). Readout shows the cutoff in kHz. |
| SIZE | 0.5–14.0 | Scales all six allpass delay times. Readout shows the largest delay in ms. At 1.0 the maximum delay is ~14.4 ms; at 14.0 it is ~202 ms. |
| IR FEED | 0–1 | How much of the diffused plate signal is added to the convolver input. At 0, Plate has no effect. |
| On/Off switch | On / Off | Enables the Plate cascade. Off = zero processing overhead. |

### 3.6 Bloom Hybrid (Row 4)

Bloom is a self-diffusing feedback processor that sits upstream of the convolution engine — think of it as a guitar pedal plugged into the reverb's input. A six-stage allpass cascade with an internal feedback loop produces a progressively building, textured swell. The left and right channels use different prime delay times, so the stereo field naturally fills without any explicit width processing.

Two independent output paths give Bloom its character:

- **IR FEED** — injects the Bloom output into the convolver input. The reverb then adds its own decay on top of the bloom texture. Default: 0.4 (audible as soon as Bloom is enabled).
- **VOLUME** — adds the Bloom output directly to the final mix, after the dry/wet blend. This path is audible at any dry/wet setting, including fully dry. Default: 0.

| Control | Range | Description |
|---|---|---|
| SIZE | 0.25–2.0 | Scales all six allpass delay times. At 1.0 the delays span ~5–40 ms (dense, textured); at 2.0 ~10–80 ms (spacious). |
| FEEDBACK | 0–0.65 | Amount of self-feedback. Higher values produce a more self-sustaining, swelling bloom. |
| TIME | 50–500 ms | How far back the feedback tap reads. Short = fast rhythmic build; long = slow, expansive sustain. |
| IR FEED | 0–1 | Injects Bloom output into the convolver input. Default 0.4. |
| VOLUME | 0–1 | Adds Bloom output directly to the final mix (independent of dry/wet). Default 0. |
| On/Off switch | On / Off | Enables Bloom. Off = zero processing overhead. |

### 3.7 Clouds Post Convolution (Row R1)

Cloud is a granular processor inspired by Mutable Instruments' Clouds hardware module. It captures a three-second window of the input signal and plays back randomised grains drawn from across the full history. Because concurrent grains come from very different moments in time, the result is a rich spectral cloud rather than a delay-like repetition. A four-stage allpass diffusion cascade smooths grain boundaries.

DENSITY controls how many grains are active simultaneously via an exponential curve — the full knob range is musically useful. LENGTH sets grain duration directly in milliseconds, independently of DENSITY. IR FEED routes the grain output into the convolution engine so the reverb tail wraps around the cloud texture.

| Control | Range | Description |
|---|---|---|
| WIDTH | 0–1 | Stereo spread. Higher values increase cross-channel grain routing and the probability of reversed grains. |
| DENSITY | 0.1–4.0 | Grain spawn rate (exponential curve). At 50% (~2.05) approximately four grains are active simultaneously at 200 ms LENGTH. |
| LENGTH | 25–1000 ms | Individual grain duration in ms. Longer grains = smoother, more sustained texture; shorter = glitchy, staccato. |
| FEEDBACK | 0–0.7 | Mixes the grain output back into the capture buffer, creating a self-reinforcing, accumulating cloud texture. |
| IR FEED | 0–1 | Routes the diffused grain output into the convolver input, so the reverb tail wraps around the cloud. |
| On/Off switch | On / Off | Enables Cloud. Off = zero processing overhead. |

> *Cloud captures the pre-convolution dry signal. The reverb tail and any other effects do not feed back into the grain buffer.*

### 3.8 Shimmer (Row R2)

Shimmer is an eight-voice harmonic grain cloud. Each voice pitch-shifts the dry signal by a fixed harmonic interval relative to PITCH, runs it through allpass smearing for spectral width, and injects the result into the convolution engine. The reverb IR then adds the decay — there is no post-convolution feedback loop, so pitch never stacks uncontrollably. Two chorus voices (voices 6 and 7) are tuned 3 and 6 cents above voices 0 and 1 respectively for gentle beating.

**Voice layout**

| Voice | Pitch | Role |
|---|---|---|
| 0 | 0 st | Unshifted — body / unison reverb tail |
| 1 | +N st | Fundamental shimmer interval |
| 2 | +2N st | 2nd harmonic up |
| 3 | −N st | 1st harmonic down |
| 4 | +3N st | 3rd harmonic up |
| 5 | −2N st | 2nd harmonic down |
| 6 | 0 st + 3 ¢ | Chorus double of voice 0 |
| 7 | +N st + 6 ¢ | Chorus double of voice 1 |

N = the PITCH semitone setting. At +12 st (one octave), voices 1/2/3/4/5 produce +12, +24, −12, +36, −24 semitones.

| Control | Range | Description |
|---|---|---|
| PITCH | −24 to +24 st | Harmonic interval N applied to the shimmer voices (integer semitone steps). +12 = one octave. |
| LENGTH | 50–500 ms | Grain duration in ms. Longer = smoother, more sustained pitch; shorter = more smeared, atmospheric. |
| DELAY | 0–1000 ms | Dual role: (1) onset stagger — each voice waits (voice number + 1) × DELAY ms before contributing, building the shimmer progressively; (2) sets the per-voice internal delay period, staggered across voices to avoid pulsing. |
| IR FEED | 0–1 | Injects the shimmer cloud into the convolver input. Default 0.5 — audible immediately on enable. |
| FEEDBACK | 0–0.7 | Controls the decay time of per-voice internal delay lines (longer sustain / ring-out). Higher = up to ~15 s decay. |
| On/Off switch | On / Off | Enables Shimmer. Off = zero processing overhead. |

> *At DELAY = 0, all eight voices start simultaneously. At DELAY = 500 ms (default), voice 0 starts at 500 ms, voice 7 at 4,000 ms after enabling.*

### 3.9 Tail AM Mod & Tail Frq Mod (Row R3)

These five knobs add LFO-based amplitude and pitch/chorus modulation to the wet signal. They are always active — there is no on/off switch.

**Tail AM Mod group**

| Control | Range | Description |
|---|---|---|
| LFO DEPTH | 0–1 | Depth of amplitude modulation applied to the reverb tail. At 0 there is no modulation. |
| LFO RATE | 0.01–2 Hz (inverted) | LFO speed. The knob is display-inverted: turn left for slower modulation, right for faster. |

**Tail Frq Mod group**

| Control | Range | Description |
|---|---|---|
| TAIL MOD | 0–1 | Amount of variable-delay chorus/pitch modulation on the reverb tail. |
| DELAY DEPTH | 0.5–8 ms | Depth of the modulated delay sweep. |
| RATE | 0.05–3 Hz | Rate of the tail chorus/pitch modulation. |

### 3.10 Output Controls

Four output-level knobs are arranged in the centre of the window, flanking the DRY/WET knob.

| Control | Range | Description |
|---|---|---|
| ER | −48 to +6 dB | Level of the early reflection portion of the IR (< 85 ms). Use this to emphasise or suppress the room 'snap'. |
| Tail | −48 to +6 dB | Level of the reverb tail (> 85 ms). Reduce to shorten perceived decay without altering the IR. |
| WET OUT TRIM | −24 to +12 dB | Overall gain on the wet output, applied after convolution and EQ. |
| Width | 0–2 | Stereo width of the wet signal via mid/side processing. 1 = normal; below 1 = narrower (mono at 0); above 1 = wider. |

### 3.11 EQ Section

A five-band parametric EQ shapes the wet signal. The EQ occupies the bottom-right of the window and shows a live spectrum analyser behind the response curve. Drag the coloured band handles on the graph to adjust frequency and gain, or use the knobs above the graph.

| Band | Type | Default Freq | Freq Range | Gain Range | Q / Slope Range |
|---|---|---|---|---|---|
| LOW | Low shelf | 200 Hz | 20–1,200 Hz | ±12 dB | Slope 0.3–2.0 |
| MID 1 | Peak | 400 Hz | 50–16,000 Hz | ±12 dB | Q 0.3–10 |
| MID 2 | Peak | 1,000 Hz | 50–16,000 Hz | ±12 dB | Q 0.3–10 |
| MID 3 | Peak | 4,000 Hz | 50–16,000 Hz | ±12 dB | Q 0.3–10 |
| HIGH | High shelf | 8,000 Hz | 2,000–20,000 Hz | ±12 dB | Slope 0.3–2.0 |

Each band has three knobs: FREQ (frequency), GAIN (cut/boost), and Q or SLOPE. A live readout above each knob shows its current value. All bands default to 0 dB gain.

### 3.12 Level Meter

A stereo peak meter in the bottom-left of the window shows the wet output level across four paths: Input L, Input R, Tail L, and Tail R. The meter displays peak levels with a fast attack and slow decay.

---

## 4. IR Synthesiser

Click the IR Synth button to open the synthesiser page. All controls are visible simultaneously — there are no tabs. The left column contains acoustic character controls; the right side shows an interactive floor plan. Changes to any control take effect when you click Calculate IR.

### 4.1 Surface Materials

Choose materials for the floor, ceiling, and walls from the following options. Each material has absorption coefficients defined across eight octave bands (125 Hz – 16 kHz). A Windows slider (0–100 %) blends the chosen wall material with glass — useful for halls with glazed panels.

| Material | Typical Character |
|---|---|
| Concrete / bare brick | Very reflective — long, bright tail. |
| Painted plaster | Moderately reflective. |
| Hardwood floor | Reflective floor surface. |
| Carpet (thin) | Mildly absorptive. |
| Carpet (thick) | Highly absorptive, shortened HF decay. |
| Glass (large pane) | Reflective above 500 Hz; absorptive at 125 Hz. |
| Heavy curtains | Highly absorptive — very short, muted decay. |
| Acoustic ceiling tile | Absorptive ceiling — short RT60. |
| Plywood panel | Moderately absorptive with some resonance. |
| Upholstered seats | Very absorptive — suitable for occupied audiences. |
| Bare wooden seats | Moderately reflective seating. |
| Water / pool surface | Highly reflective at most frequencies. |
| Rough stone / rock | Moderately reflective with some scatter. |
| Exposed brick (rough) | Reflective with textured, diffuse character. |

| Control | Range | Description |
|---|---|---|
| Floor | 14 materials | Floor surface material. |
| Ceiling | 14 materials | Ceiling surface material. |
| Walls | 14 materials | Wall surface material (blended with glass via the Windows slider). |
| Windows | 0–100 % | Fraction of wall area that is glazing. 0 % = solid wall; 100 % = fully glazed. |

> *Default values: Walls = Concrete / bare brick, Ceiling = Painted plaster, Floor = Hardwood floor, Windows = 27 %.*

### 4.2 Contents

| Control | Range | Description |
|---|---|---|
| Audience | 0–1 | Simulates an occupied audience. Adds frequency-dependent absorption across the floor area — mainly reduces HF decay. Default: 0.45. |
| Diffusion | 0–1 | Controls allpass feedback and Lambert scatter density. Higher values produce a smoother, denser early reflection field and suppress specular 'pinging' artefacts. Default: 0.40. |

### 4.3 Interior Architecture

| Control | Range / Options | Description |
|---|---|---|
| Vault type | None (flat), Shallow barrel, Deep pointed (gothic), Groin / cross vault, Fan vault, Coffered dome | Ceiling geometry. Adds height-based HF scatter and raises the effective ceiling height. Default: Groin / cross vault. |
| Organ case | 0–1 | Absorption and scattering from an organ case — opens one wall face and adds internal surface area. Default: 0.59. |
| Balconies | 0–1 | Adds balcony absorption area — shortens RT60 and changes reflection density. Default: 0.54. |

### 4.4 Options

| Control | Options | Description |
|---|---|---|
| Mic pattern | omni, subcardioid, cardioid, figure-8 | Simulated microphone polar pattern. Figure-8 attenuates lateral arrivals; omni captures all angles equally. |
| Early reflections only | Toggle | Renders only image-source reflections — no FDN reverb tail. The IR fades out with a 10 ms cosine taper at ~85 ms. |
| Sample rate | 44.1 kHz, 48 kHz | Output sample rate of the synthesised IR. Match your DAW's project sample rate. |

### 4.5 Room Shape & Dimensions

| Shape | Description |
|---|---|
| Rectangular | Standard box room — highest reflection density. |
| L-shaped | L-shaped floor plan — lower density, irregular ER pattern. |
| Fan / Shoebox | Tapered (fan) or parallel-sided (shoebox) hall profile. |
| Cylindrical | Circular cross-section — elevated high-frequency density. |
| Cathedral | Large cathedral-like space — sparser early reflections. |
| Octagonal | Octagonal floor plan — intermediate density. |

| Dimension | Range | Description |
|---|---|---|
| Width | 0.5–50 m | Room width. |
| Depth | 0.5–50 m | Room depth. |
| Height | 1–30 m | Room ceiling height. |

Click a number label next to a slider to type in an exact value.

### 4.6 Floor Plan — Speaker & Mic Placement

The floor plan shows the room from above. Two pairs of pucks represent the signal chain:

- **Orange pucks** — Left and Right speakers (sources).
- **Blue pucks** — Left and Right microphones (receivers).

Drag a puck to reposition it. Drag the small handle extending from a puck to rotate its aim direction. Speaker and microphone positions and angles affect early reflection timing, the stereo image, and cross-channel path lengths. Placement also shifts the crossfade point between early reflections and the FDN tail.

> *Default placement: speakers at 25 % and 75 % of the room width (centred in depth); microphones at 35 % and 65 % of the room width, 80 % from the front wall.*

### 4.7 RT60 Display

The IR Synth page shows estimated RT60 values (reverberation time in seconds) at eight octave bands: 125, 250, 500, 1k, 2k, 4k, 8k, and 16k Hz. These update in real time as you change materials, dimensions, and room contents. Typical rooms show longer RT60 at low frequencies and shorter RT60 at high frequencies.

### 4.8 Bottom Bar

| Control | Description |
|---|---|
| IR preset list | Load an existing 4-channel IR from the IR folder, or type a new name here before saving. |
| Save | Saves the current IR to `~/Library/Audio/Impulse Responses/Ping/` under the name shown in the preset list. If the name matches an existing file, a confirmation prompt appears. |
| Calculate IR | Renders the IR from the current parameters and loads it into the plugin. The plugin remains on the IR Synth page. Rendering may take a few seconds for large rooms. |
| Main Menu | Returns to the main plugin interface. The main UI will display 'Synthesized IR' if you have used Calculate IR. |

---

## 5. Technical Overview: How the IR Is Synthesised

P!NG's IR Synthesiser models a room as a mathematical acoustic space and produces a four-channel, 24-bit WAV impulse response (channels: Left→Left, Right→Left, Left→Right, Right→Right). The engine uses four main stages: image-source method, per-band rendering, allpass diffusion, and an FDN tail.

### 5.1 Image-Source Method

The room is treated as a box (with floor-plan and vault modifiers). Virtual source positions are created by mirroring the real speaker across each wall. Each reflection carries a frequency-dependent amplitude based on: distance attenuation (1/distance law), per-surface absorption coefficients at each of the eight frequency bands, air absorption (ISO 9613-1 values, increasing sharply above 8 kHz), speaker directivity (cardioid at low orders, blending to omnidirectional above order 2), microphone polar pattern (applied per arrival angle), and phase polarity (inverted on odd-order reflections).

Lambert diffuse scattering generates two secondary 'scatter' arrivals from each first- to third-order reflection, filling in the gaps between specular spikes and making the early reflection field sound more like a real, irregular room.

### 5.2 Per-Band Rendering

Materials are defined by absorption at eight octave bands (125 Hz – 16 kHz). The engine splits reflections into eight per-band buffers, applies bandpass filters, and sums them into a single waveform per channel. This gives each room its characteristic tonal decay curve — for example, concrete rooms have a longer, brighter decay than carpeted ones.

### 5.3 Allpass Diffusion

Deferred allpass diffusion starts at 65 ms (not from the beginning of the IR), preserving discrete early reflections while smoothing the denser later field. The FDN seed is pre-diffused with a three-stage allpass cascade to prevent any single spike from recirculating through the delay network as a distinct echo.

### 5.4 Late Reverb (FDN Tail)

After approximately 85 ms, the image-source field gives way to a 16-line feedback delay network (FDN) seeded with the early reflection energy. Key properties:

- Prime-length delay lines prevent harmonic resonances.
- Hadamard mixing distributes energy evenly and keeps channels decorrelated.
- Per-line lowpass filters give each line its own frequency-dependent decay rate, using the 16 kHz RT60 as the high-frequency reference for accurate air-absorption modelling.
- LFO modulation with geometric frequency spacing (0.07–0.45 Hz, irrational ratios) prevents periodic density fluctuations. Modulation depth is proportional to each line's low-frequency dominance, avoiding pitch artefacts on high-frequency content.
- Automatic level matching adjusts the FDN onset to be consistent with the ER, regardless of speaker–mic distance.

### 5.5 Output

The final IR is band-limited (20 Hz – 18 kHz), processed through a low-frequency modal resonator bank (sub-200 Hz standing-wave enhancement), faded out over the final 500 ms, and given a fixed +15 dB output trim. A `.ping` sidecar file alongside each synthesised WAV stores the synthesis parameters for recall.

---

## 6. IR File Location & Formats

| Item | Details |
|---|---|
| Factory IR folder | `/Library/Application Support/Ping/Factory IRs/` — installed by the P!NG installer, shared across all users, read-only. Organised into subfolders: Halls, Large Spaces, Rooms, Scoring Stages, Tight Spaces. These appear in the Factory section of the IR dropdown. |
| User IR folder | `~/Library/Audio/Impulse Responses/Ping/` — place any compatible .wav or .aiff file here and it appears in the Your IRs section of the IR dropdown. |
| Supported formats | .wav, .aiff, .aif (mono, stereo, or 4-channel) |
| Synthesised IRs | 4-channel WAV (24-bit, 48 kHz or 44.1 kHz as selected). Only 4-channel files appear in the IR Synth pick list. |
| Sidecar files | .ping (JSON) — stored alongside each synthesised WAV. Contains the IRSynthParams used to generate it, allowing full recall of room, materials, and placement. |

Place any compatible .wav or .aiff file in the IR folder and it will appear in the main IR dropdown after a rescan. Mono and stereo IRs are expanded internally to four channels for the true-stereo convolution path.

---

## 7. Presets

Presets capture the complete plugin state and are stored in `~/Library/Audio/Presets/Ping/`. A preset includes:

- All main UI parameters (DRY/WET, PREDELAY, DAMPING, STRETCH, WIDTH, all modulation and output knobs).
- IR selection — the loaded IR file path, or a reference to the active synthesised IR.
- All IR Synth parameters (room shape, dimensions, surface materials, placement, options).
- All hybrid effect settings (Plate, Bloom, Cloud, Shimmer).
- Crossfeed settings (ER and Tail).
- EQ settings (all five bands).

Factory presets are installed by the P!NG installer at `/Library/Application Support/Ping/Factory Presets/` — shared across all users on the machine and read-only. They appear first in the PRESET dropdown under a Factory heading, organised by category (Halls, Large Spaces, Rooms, Scoring Stages, Tight Spaces). Your own saved presets appear below under a Your Presets heading.

Factory presets cannot be overwritten. To save your own preset, type a name in the PRESET box and click Save. If the name matches an existing preset, a confirmation prompt appears before overwriting. To load a preset, open the PRESET dropdown and select it.

### 7.1 Factory Presets & IRs

P!NG ships with 43 factory presets across five categories, each paired with a synthesised impulse response. Real-world venue models use published room dimensions and surface materials; synthetic presets are purpose-designed acoustic spaces.

#### Halls

| Preset | Description |
|---|---|
| Carnegie Hall New York | Isaac Stern Auditorium. Painted plaster walls, upholstered seats, three tiers of balconies. Warm, rich reverb (RT60 ~1.7–2.0 s). |
| Cello Epic Hall | Synthetic large concert hall tuned for solo cello and strings. Wide, enveloping decay. |
| Concertgebouw Amsterdam | Grote Zaal. Wooden panelling walls, plaster ceiling, balconies on three sides. Classic European concert hall sound (RT60 ~2.2 s). |
| Epic Piano Hall | Synthetic concert hall tuned for grand piano. Long, clear decay with balanced frequency response. |
| King's College Chapel Cambridge | Iconic Gothic chapel with famous fan vault. Stone floor and walls, stained-glass windows. Very long reverb (RT60 ~4–6 s). |
| Large Concert | Synthetic large concert hall with a broad, full-bodied reverb. |
| Med Orch Hall | Synthetic medium orchestral hall. Balanced reverb suited to orchestral sessions. |
| Percussion Hall centre | Synthetic hall with centre mic placement, tuned for percussion clarity. |
| Sage Gateshead Hall One | American Ash wood panelling throughout. High-diffusion shoebox design with adjustable ceiling. |
| St Jude-on-the-Hill Hampstead | Edwin Lutyens church in Hampstead. Stone walls, painted plaster barrel vault, stained glass. Rich, characterful mid-length reverb. |
| Vienna Musikverein Golden Hall | Classic rectangular shoebox hall. Ornate plaster walls, hardwood floor, horseshoe balconies. The gold standard of concert-hall acoustics. |

#### Large Spaces

| Preset | Description |
|---|---|
| Elbphilharmonie Hamburg | Großer Saal. Vineyard-style seating with 10,000 uniquely shaped diffusion panels. Maximum diffusion, audience-surround layout. |
| Epic Room | Synthetic large reverberant space. Full-bodied, expansive character. |
| Hall of Plenty | Synthetic large hall with generous, warm reverb tail. |
| Hansa Meistersaal Berlin | Grand ornate ballroom from 1913. Large windows, wood floors, plaster walls. Recorded Bowie, U2, Depeche Mode. |
| Large Beauty Space | Synthetic spacious environment with a wide, smooth decay. |
| Royal Albert Hall London | Elliptical/cylindrical hall with suspended acoustic mushrooms. Post-1969 treatment (RT60 ~2.5–3.0 s). |
| Sydney Opera House Concert Hall | Fan-shaped hall with Australian brush box timber throughout. Suspended acrylic acoustic rings. |
| Usher Hall Edinburgh | Early 20th-century horseshoe hall with decorative dome ceiling and Harrison & Harrison organ. |
| Warm Long Church | Synthetic church-like space with a warm, extended reverb tail. |

#### Rooms

| Preset | Description |
|---|---|
| Abbey Road Studio Two | Where the Beatles recorded. Parquet floor, adjustable LEDE panels (RT60 ~1.2 s). |
| Brick Room | Synthetic room with reflective brick character. Short, lively ambience. |
| British Grove Studios London | Munro Acoustics design. Slanted wood reflectors, hardwood floors. |
| Drum Circle | Synthetic room tuned for drum kit recording. Tight, punchy ambience. |
| Electric Lady Studios New York | Jimi Hendrix's studio. Curved plaster walls, carpeted floor. |
| Metropolis Studios London | One of Europe's largest studio complexes. Clean, controlled room sound. |
| Perc Room Med Centre | Synthetic medium room with centre mic, tuned for percussion. |
| RAK Studios London | North London boutique studio. Hardwood floors, wood panels. Natural, musical room tone. |
| Sunset Sound Studio 2 Hollywood | Classic rock/pop room. Concrete and plywood walls. Van Halen, The Doors, Led Zeppelin, Prince. |

#### Scoring Stages

| Preset | Description |
|---|---|
| Abbey Road Studio One | Largest purpose-built recording studio. Parquet floor, adjustable acoustic panels. Home of countless film scores. |
| Capitol Studios Hollywood A | Live concrete/plaster walls. The studio above Capitol's famous underground echo chambers. |
| Medium Studio | Synthetic medium scoring stage. Balanced, versatile room for ensemble recording. |
| Medium wide Space | Synthetic medium-wide scoring environment with broader stereo image. |
| Ocean Way Nashville Studio A | Natural-sounding wood reflectors throughout. Four isolation booths. |
| Warner Bros Eastwood Scoring Stage | Large Hollywood orchestral scoring stage. Carpeted floor, acoustic tile ceiling, diffusion panels. |
| Wide Scoring Stage WW left | Synthetic wide scoring stage with left-weighted mic placement. |

#### Tight Spaces

| Preset | Description |
|---|---|
| Abbey Road Echo Chamber | Tiled underground chamber below Studio Three. Bare concrete and tile — extremely live. |
| Capitol Studios Echo Chamber | Raw concrete chambers beneath Capitol Tower. Among the most famous echo chambers in history. |
| Concrete Stairwell | Tall narrow concrete box. Long flutter echoes, bright character — the classic stairwell reverb. |
| Damped Studio Booth | Small treated vocal/instrument booth. Thick carpet, acoustic panels — near-anechoic character. |
| Drum space | Synthetic tight room tuned for close-miked drum recording. |
| Stone Recital Room | Small vaulted stone room modelled on Oxford college chapels. Stone walls, barrel-vaulted plaster ceiling. |
| Tiled Shower Room | Domestic hard-surfaced room. The classic short, bright reverb every vocalist knows. |

---

## 8. Licence

P!NG uses Ed25519 digital signature verification for licence validation. No internet connection is required after the initial activation.

| Tier | Description |
|---|---|
| demo | Evaluation licence — audio output is silenced after a period. |
| standard | Full licence. |
| pro | Full licence with extended privileges (future use). |

The licence is stored in `~/Library/Audio/Ping/licence.xml`. A system-wide licence placed in `/Library/Application Support/Audio/Ping/licence.xml` will activate the plugin for all users on the machine. If upgrading from an earlier version, the plugin migrates existing licence files to the new location automatically.

> *Contact support if you need to move your licence to a different machine.*
