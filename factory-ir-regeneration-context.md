# Factory IR & Preset Regeneration — Context for Cursor

## What changed

**`Source/IRSynthEngine.cpp` — two fixes:**

1. **`sz` / `rz` speaker/mic heights** — changed from `He * 0.55` (both) to:
   ```cpp
   double sz = std::min(1.0, He * 0.9);   // speaker at 1 m off floor
   double rz = std::min(3.0, He * 0.9);   // mic at 3 m (Decca tree height)
   ```
   Models a real orchestral session instead of a symmetric geometric approximation.

2. **`makeWav` — WAVE_FORMAT_EXTENSIBLE with correct channel mask:**
   ```cpp
   v32 = 0x33; memcpy(p, &v32, 4); p += 4; // dwChannelMask = FL+FR+BL+BR
   ```
   Previously `dwChannelMask=0` — macOS CoreAudio/QuickTime rejects EXTENSIBLE WAV files with unspecified channel mask. Must be `0x33` to match what JUCE's own `WavAudioFormat` writer produces. Files with `dwChannelMask=0` appear in the IR list but silently fail to load (blank waveform, no audio).

**`Tests/PingEngineTests.cpp` — golden values updated:**
- IR_11 onset index: `371 → 482` (height change shifted direct-path arrival geometry)
- IR_11 `golden_iLL[30]` array: new values for the new geometry
- IR_10: added onset detection (`onsetIdx` forward scan) so the RT60 decay measurement doesn't start before the direct path arrives

---

## How factory content was regenerated

All 27 venue IRs + presets live in `Installer/factory_irs/` and `Installer/factory_presets/`. They are pre-generated static files committed to the repo — the plugin ships them, it doesn't generate them at runtime.

**Regeneration steps (run from repo root on macOS):**

```bash
# 1. Compile the batch generator (standalone, no JUCE dependency)
g++ -std=c++17 -O2 -DPING_TESTING_BUILD=1 -ISource \
    Tools/generate_factory_irs.cpp Source/IRSynthEngine.cpp \
    -o build/generate_factory_irs -lm

# 2. Generate all 27 venues → Installer/factory_irs/ + Installer/factory_presets/
./build/generate_factory_irs Installer/factory_irs Installer/factory_presets

# 3. Trim trailing silence (synthIR allocates 8×RT60; actual signal is much shorter)
python3 Tools/trim_factory_irs.py Installer/factory_irs

# 4. Build installer .pkg
bash Installer/build_installer.sh
```

**Any change to `IRSynthEngine.cpp` that affects audio output requires re-running all four steps** before cutting a release.

---

## Sanity checks

- Verify WAV format: `python3 -c "import struct; d=open('file.wav','rb').read(80); [print(f'tag=0x{struct.unpack(chr(60)+chr(72),d[p+8:p+10])[0]:04x} mask=0x{struct.unpack(chr(60)+chr(73),d[p+28:p+32])[0]:08x}') for p in range(12,60) if d[p:p+4]==b'fmt ']"` — must be `tag=0xfffe mask=0x00000033`
- Open any new `.wav` in QuickTime Player — if it won't play, the channel mask or format is wrong
- All 28 tests must pass: `cd build && ctest --output-on-failure`
- IR_11 golden lock: if engine geometry changes again, run `./PingTests "[capture]" -s` and update onset + 30-sample array in `Tests/PingEngineTests.cpp`

---

## Factory IR file format

4-channel 24-bit 48 kHz WAV, WAVE_FORMAT_EXTENSIBLE (`tag=0xFFFE`, `dwChannelMask=0x33`), channels ordered: **iLL, iRL, iLR, iRR** (true-stereo cross-channel impulse responses). The plugin's `loadIRFromFile` reads these as 4-channel and routes them directly to the true-stereo convolution path — no mono/stereo expansion is applied (that only happens for 2-channel user IRs).
