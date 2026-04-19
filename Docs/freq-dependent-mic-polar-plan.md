# P!NG — Frequency-Dependent Mic Polar Patterns: Implementation Plan

## Background

The IR Synth currently applies a single polar pattern gain to all 8 octave bands. Real microphones are
more omnidirectional at low frequencies and increasingly directional at high frequencies. A
large-diaphragm cardioid is nearly omni at 125 Hz and close to figure-8 directivity at 16 kHz. This
change makes each octave band receive its physically correct directional weighting, producing more
realistic reverb character: more omnidirectional low-frequency diffusion, tighter high-frequency pickup.

A new **Cardioid (SDC)** option is added alongside the existing **Cardioid** (renamed **Cardioid (LDC)**).
Small-diaphragm condensers maintain more consistent directivity across frequency.

**On-axis constraint:** `o + d = 1.0` is maintained at every band for every pattern. On-axis gain is
frequency-flat. Only off-axis rejection varies with frequency.

**Mic direction is fully preserved.** `p.micl_angle` / `p.micr_angle` are passed as `micFaceAngle` into
all four `calcRefs` calls and the polar formula uses `cos(az - faceAngle)`. The new per-band
implementation doesn't change `az` or `faceAngle` — only `o` and `d` become band-indexed.

---

## Files to modify

| File | Nature of change |
|---|---|
| `Source/IRSynthEngine.h` | Update `micG()` static declaration — add `int band` param |
| `Source/IRSynthEngine.cpp` | Update `s_mic` type, `initMIC()`, `getMIC()`, `micG()`, `calcRefs()` |
| `Source/IRSynthComponent.cpp` | Update `micOptions[]` (5 entries), `n` counts, legacy migration in `setParams()` |
| `Tests/PingEngineTests.cpp` | Update IR_11 golden values only (after running capture tool) |

**Do NOT touch:** `IRSynthEngine.h` (IRSynthParams struct — no field changes), `PluginProcessor`,
`PluginEditor`, `IRSynthComponent.h`, `FloorPlanComponent`, `PingDSPTests.cpp`, all other source files.

---

## Step 1 — `Source/IRSynthEngine.h`: Update `micG()` declaration

Find:
```cpp
static double micG  (double az, const std::string& pat, double faceAngle);
```

Replace with:
```cpp
static double micG  (int band, double az, const std::string& pat, double faceAngle);
```

No other changes to this file.

---

## Step 2 — `Source/IRSynthEngine.cpp`: Update `s_mic` type

Find the static map declaration (near the top of the file, before `initMIC()`):
```cpp
static std::map<std::string, std::pair<double,double>> s_mic;
```

Replace with:
```cpp
static std::map<std::string, std::array<std::pair<double,double>, 8>> s_mic;
```

---

## Step 3 — `Source/IRSynthEngine.cpp`: Replace `initMIC()` entirely

Find the entire `initMIC()` function and replace it with the following.

The new type is `std::array<std::pair<double,double>, 8>` — one `{omni, directional}` pair per octave
band, ordered `[125, 250, 500, 1k, 2k, 4k, 8k, 16k]` Hz.

```cpp
static const std::map<std::string, std::array<std::pair<double,double>, 8>>& initMIC()
{
    if (s_mic.empty())
    {
        // Each entry: array of {omni, directional} pairs for bands [125,250,500,1k,2k,4k,8k,16k] Hz.
        // Constraint: o+d = 1.0 at every band (on-axis gain = 1.0; only off-axis rejection varies).
        // Values derived from published polar pattern measurements for each mic family.

        // Pure pressure transducer — omnidirectional at all frequencies
        s_mic["omni"] = {{
            {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00},
            {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}, {1.00, 0.00}
        }};

        // Wide pickup, broadens further at low end
        s_mic["subcardioid"] = {{
            {0.85, 0.15}, {0.82, 0.18}, {0.78, 0.22}, {0.70, 0.30},
            {0.65, 0.35}, {0.60, 0.40}, {0.55, 0.45}, {0.50, 0.50}
        }};

        // Large-diaphragm condenser (~1" capsule) — significant narrowing above 1 kHz.
        // "cardioid" kept as backward-compat alias so old saved presets continue to work.
        std::array<std::pair<double,double>, 8> ldcData = {{
            {0.78, 0.22}, {0.68, 0.32}, {0.57, 0.43}, {0.50, 0.50},
            {0.40, 0.60}, {0.28, 0.72}, {0.16, 0.84}, {0.06, 0.94}
        }};
        s_mic["cardioid (LDC)"] = ldcData;
        s_mic["cardioid"]       = ldcData;  // backward-compat for saved presets

        // Small-diaphragm condenser (~12-16mm capsule) — more consistent directivity across frequency
        s_mic["cardioid (SDC)"] = {{
            {0.65, 0.35}, {0.58, 0.42}, {0.53, 0.47}, {0.50, 0.50},
            {0.44, 0.56}, {0.36, 0.64}, {0.28, 0.72}, {0.18, 0.82}
        }};

        // Ribbon figure-8 — fairly consistent but slight omni component at very low end
        // due to cabinet diffraction effects below ~200 Hz
        s_mic["figure8"] = {{
            {0.12, 0.88}, {0.06, 0.94}, {0.02, 0.98}, {0.00, 1.00},
            {0.00, 1.00}, {0.00, 1.00}, {0.00, 1.00}, {0.00, 1.00}
        }};
    }
    return s_mic;
}
```

---

## Step 4 — `Source/IRSynthEngine.cpp`: Update `getMIC()` return type

Find:
```cpp
static const std::map<std::string, std::pair<double,double>>& getMIC() { return initMIC(); }
```

Replace with:
```cpp
static const std::map<std::string, std::array<std::pair<double,double>, 8>>& getMIC() { return initMIC(); }
```

---

## Step 5 — `Source/IRSynthEngine.cpp`: Update `micG()` implementation

Find the existing `micG()` function:
```cpp
double IRSynthEngine::micG (double az, const std::string& pat, double faceAngle)
{
    auto it = getMIC().find(pat);
    if (it == getMIC().end()) return 1.0;
    double o = it->second.first, d = it->second.second;
    return std::max(0.0, o + d * std::cos(az - faceAngle));
}
```

Replace with:
```cpp
double IRSynthEngine::micG (int band, double az, const std::string& pat, double faceAngle)
{
    const auto& mic = getMIC();
    auto it = mic.find(pat);
    if (it == mic.end()) return 1.0;
    const double o = it->second[band].first;
    const double d = it->second[band].second;
    return std::max(0.0, o + d * std::cos(az - faceAngle));
}
```

`band` is always in `[0, 7]` — it comes from the `for (int b = 0; b < N_BANDS; ++b)` loop in `calcRefs`
where `N_BANDS = 8`.

---

## Step 6 — `Source/IRSynthEngine.cpp`: Update `calcRefs()` amplitude computation

Find this block inside `calcRefs()`:

```cpp
double az = std::atan2(iy - ry, ix - rx);
double mg = micG(az, micPat, micFaceAngle);

std::array<double,8> amps;
for (int b = 0; b < N_BANDS; ++b)
{
    double a = 1.0 / std::max(dist, 0.5);
    a *= std::pow(rF[b], std::ceil(std::abs(nz) / 2.0));
    a *= std::pow(rC[b], std::floor(std::abs(nz) / 2.0));
    a *= std::pow(rW[b], std::abs(nx) + std::abs(ny));
    if (std::abs(ny) > 0) a *= std::pow(oF, std::abs(ny));
    if (std::abs(nz) > 0) a *= std::pow(1.0 - vHfA * std::min(b / 3.0, 1.0), std::abs(nz));
    amps[b] = a * std::pow(10.0, -AIR[b] * dist / 20.0) * mg * sg * polarity;
}
```

Replace with (remove the single `mg` scalar; call `micG` per band):

```cpp
double az = std::atan2(iy - ry, ix - rx);
// mg moved inside loop — now computed per frequency band

std::array<double,8> amps;
for (int b = 0; b < N_BANDS; ++b)
{
    double a = 1.0 / std::max(dist, 0.5);
    a *= std::pow(rF[b], std::ceil(std::abs(nz) / 2.0));
    a *= std::pow(rC[b], std::floor(std::abs(nz) / 2.0));
    a *= std::pow(rW[b], std::abs(nx) + std::abs(ny));
    if (std::abs(ny) > 0) a *= std::pow(oF, std::abs(ny));
    if (std::abs(nz) > 0) a *= std::pow(1.0 - vHfA * std::min(b / 3.0, 1.0), std::abs(nz));
    amps[b] = a * std::pow(10.0, -AIR[b] * dist / 20.0) * micG(b, az, micPat, micFaceAngle) * sg * polarity;
}
```

This is the entirety of the DSP change. Everything else in `calcRefs` — the `Ref` struct, scatter logic,
Lambert scatter block — is untouched. `renderCh`, `renderFDNTail`, and the blend are untouched.

---

## Step 7 — `Source/IRSynthComponent.cpp`: Update mic pattern combo (4 sub-changes)

### 7a — Update `micOptions[]` array

Find:
```cpp
const char* const micOptions[] = { "omni", "subcardioid", "cardioid", "figure8" };
```

Replace with:
```cpp
const char* const micOptions[] = { "omni", "subcardioid", "cardioid (LDC)", "cardioid (SDC)", "figure8" };
```

### 7b — Update `addOptions` call count

Find every occurrence of:
```cpp
addOptions (micPatternCombo, micOptions, 4);
```

Replace each one with:
```cpp
addOptions (micPatternCombo, micOptions, 5);
```

### 7c — Update `setComboTo` call in `setParams()` with legacy migration

Find:
```cpp
setComboTo (micPatternCombo, p.mic_pattern, micOptions, 4);
```

Replace with:
```cpp
// Migrate legacy "cardioid" (written by older versions) to the new "cardioid (LDC)" display key
juce::String micPat = juce::String (p.mic_pattern);
if (micPat.trim().equalsIgnoreCase ("cardioid"))
    micPat = "cardioid (LDC)";
setComboTo (micPatternCombo, micPat, micOptions, 5);
```

### 7d — Update `comboSelection` call count in `getParams()`

Find:
```cpp
p.mic_pattern = comboSelection (micPatternCombo, micOptions, 4).toStdString();
```

Replace with:
```cpp
p.mic_pattern = comboSelection (micPatternCombo, micOptions, 5).toStdString();
```

### 7e — Verify default selection comment

Find the default selection line (no code change needed, just confirm the comment is clear):
```cpp
micPatternCombo.setSelectedId (3, juce::dontSendNotification); // cardioid (LDC) default
```

ID 3 now correctly points to "cardioid (LDC)" in the updated 5-item array. No code change needed.

---

## Step 8 — Build and run the test suite

```bash
cd "/Users/paulthomson/Cursor wip/Ping"
cmake -B build -S .
cmake --build build --target PingTests
cd build && ctest --output-on-failure
```

**Expected result:**
- **IR_11 FAILS** — golden values changed. This is expected and correct.
- **All other tests pass** — IR_01 through IR_10, IR_12, IR_13, DSP_01 through DSP_14.

If any test other than IR_11 fails, stop and investigate before proceeding. Do not suppress other failures.

---

## Step 9 — Capture new IR_11 golden values

```bash
cd build
./PingTests "[capture]" -s
```

This prints the new onset index and 30 sample values for `iLL`.

Open `Tests/PingEngineTests.cpp` and find the IR_11 test. Make these changes:

1. Set `goldenCaptured = true`
2. Update `onset_offset` to the printed value
3. Replace the `golden_iLL[30]` array with all 30 printed values
4. Update the comment to read:
   ```
   // Updated vX.Y.Z: frequency-dependent mic polar patterns
   // (o+d=1 per band; LDC cardioid: {0.5,0.5} at 1kHz, narrows to {0.06,0.94} at 16kHz)
   ```

---

## Step 10 — Run full test suite to confirm clean pass

```bash
cd build && ctest --output-on-failure
```

All 27 tests (IR_01–IR_13, DSP_01–DSP_14) must pass.

---

## Step 11 — Build and install for listening test

```bash
cmake --build build --config Release
cp -R build/Ping_artefacts/Release/AU/P!NG.component /Library/Audio/Plug-Ins/Components/
```

In Logic Pro, open a fresh P!NG instance, go to the IR Synth, synthesise a new IR with the default
large room. Cycle through all five patterns and verify the following character differences:

- **Omni:** Maximum low-frequency diffusion, broadest pickup — most "room-filling" low end
- **Subcardioid:** Slightly tighter than omni, moderate HF focus
- **Cardioid (LDC):** Noticeably tighter top end; low end still comes through openly; sounds like a
  classic "big mic on a classical recording"
- **Cardioid (SDC):** More consistent directivity — tonal balance shifts less between low and high
  frequencies than LDC; feels more uniform across the spectrum
- **Figure-8:** Clear side null; front and rear pickup; low end slightly more diffuse than mid/high

Useful A/B: switch between LDC and SDC on a reverb-heavy string or piano recording. LDC should give
more "open" and enveloping low-frequency reverb; SDC should feel tighter and more precise.

---

## Step 12 — Update `CLAUDE.md`

Add to the key design decisions section:

> **Frequency-dependent mic polar patterns** — `micG()` now takes a `band` parameter (0–7,
> corresponding to octave bands 125–16 kHz). The `s_mic` map stores
> `std::array<std::pair<double,double>, 8>` per pattern. The constraint `o + d = 1.0` is maintained
> at every band so on-axis gain is frequency-flat; only off-axis rejection varies with frequency.
> The "cardioid" key is kept as a backward-compatible alias for "cardioid (LDC)" data. The five
> patterns are: omni (flat), subcardioid (0.85/0.15 at 125 Hz → 0.50/0.50 at 16 kHz), cardioid LDC
> (0.78/0.22 at 125 Hz → 0.06/0.94 at 16 kHz), cardioid SDC (0.65/0.35 at 125 Hz → 0.18/0.82 at
> 16 kHz), figure-8 (0.12/0.88 at 125 Hz → 0.00/1.00 from 1 kHz up). `micFaceAngle` (from
> `p.micl_angle` / `p.micr_angle`) is unchanged — mic direction from the FloorPlan UI continues to
> rotate the pattern correctly.

Also update the `IRSynthParams` comment for `mic_pattern` to read:
```
// "omni"|"subcardioid"|"cardioid (LDC)"|"cardioid (SDC)"|"figure8"
// "cardioid" is a legacy alias for "cardioid (LDC)"
```

---

## Factory IR regeneration (separate task — not blocking)

After the above is merged and confirmed working, regenerate the 27 factory IRs:

```bash
g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
    Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
    -o build/generate_factory_irs -lm
./build/generate_factory_irs Installer/factory_irs Installer/factory_presets
python3 Tools/trim_factory_irs.py Installer/factory_irs
find Installer -name '.DS_Store' -delete
```

Existing factory `.wav` files still load and play correctly — they are baked audio and are unaffected
by the engine change. Regeneration improves them for the next installer release but is not required
immediately.

---

## Risk summary

| Risk | Likelihood | Mitigation |
|---|---|---|
| Test other than IR_11 fails | Low | Other tests measure properties (plausibility, monotonic decay, NaN), not absolute values. On-axis is unchanged. |
| Level shift in synthesised IRs | Very low | `o + d = 1.0` constraint keeps on-axis gain at 1.0 across all bands |
| Backward compat for old presets | None | `"cardioid"` key kept in `s_mic`; `setParams()` migrates it to "cardioid (LDC)" for display |
| Existing saved IR `.wav` files | None | Baked audio — independent of engine |
| Build failure | Very low | Change is ~15 lines across two files; no new dependencies |

---

## Per-band polar pattern values reference

All values satisfy `o + d = 1.0`. Bands are `[125, 250, 500, 1k, 2k, 4k, 8k, 16k]` Hz.

| Band | Omni | Subcardioid | Cardioid LDC | Cardioid SDC | Figure-8 |
|------|------|-------------|--------------|--------------|----------|
| 125 Hz  | 1.00 / 0.00 | 0.85 / 0.15 | 0.78 / 0.22 | 0.65 / 0.35 | 0.12 / 0.88 |
| 250 Hz  | 1.00 / 0.00 | 0.82 / 0.18 | 0.68 / 0.32 | 0.58 / 0.42 | 0.06 / 0.94 |
| 500 Hz  | 1.00 / 0.00 | 0.78 / 0.22 | 0.57 / 0.43 | 0.53 / 0.47 | 0.02 / 0.98 |
| 1 kHz   | 1.00 / 0.00 | 0.70 / 0.30 | 0.50 / 0.50 | 0.50 / 0.50 | 0.00 / 1.00 |
| 2 kHz   | 1.00 / 0.00 | 0.65 / 0.35 | 0.40 / 0.60 | 0.44 / 0.56 | 0.00 / 1.00 |
| 4 kHz   | 1.00 / 0.00 | 0.60 / 0.40 | 0.28 / 0.72 | 0.36 / 0.64 | 0.00 / 1.00 |
| 8 kHz   | 1.00 / 0.00 | 0.55 / 0.45 | 0.16 / 0.84 | 0.28 / 0.72 | 0.00 / 1.00 |
| 16 kHz  | 1.00 / 0.00 | 0.50 / 0.50 | 0.06 / 0.94 | 0.18 / 0.82 | 0.00 / 1.00 |
