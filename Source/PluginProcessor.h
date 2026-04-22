#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "IRManager.h"
#include "IRSynthEngine.h"
#include "LicenceVerifier.h"
#include "HP2ndOrder.h"

class PingProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorParameter::Listener
{
public:
    PingProcessor();
    ~PingProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "P!NG"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const { return apvts; }

    IRManager& getIRManager() { return irManager; }
    const IRManager& getIRManager() const { return irManager; }

    /** Load the given IR file. Call from message thread. */
    void loadIRFromFile (const juce::File& file);
    /** Last loaded IR file (for reload when stretch/decay change). */
    const juce::File& getLastLoadedIRFile() const { return lastLoadedIRFile; }

    /** Which microphone path a buffer is being loaded into.
        Main: existing behaviour — drives currentIRBuffer, waveform, combined-tail IR.
        Direct: short-circuit load — no transforms, no ER/Tail split. 4 mono convolvers.
        Outrig / Ambient: full pipeline (reverse/stretch/decay/trim/ER-Tail split) but into
        their own 8-convolver sets; do NOT touch main state (currentIRBuffer, selectedIRFile). */
    enum class MicPath { Main, Direct, Outrig, Ambient };

    /** Load IR from buffer (e.g. reversed). Call from message thread.
        If fromSynth is true, marks current IR as synthesized and persists it with plugin state.
        If deferConvolverLoad is true, saves the raw buffer (fromSynth only) but does NOT call
        loadImpulseResponse. Use this in setStateInformation before prepareToPlay has run, to
        avoid a data race between loadImpulseResponse background threads and reset().
        The path argument selects which convolver set and raw buffer slot to target. */
    void loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate,
                           bool fromSynth = false, bool deferConvolverLoad = false,
                           MicPath path = MicPath::Main);

    /** Load the suffix-derived sibling file for the given mic path (_direct / _outrig / _ambient).
        Returns silently if the sibling file does not exist (old presets / factory IRs with
        only the MAIN WAV, or paths the user has not synthesised). */
    void loadMicPathFromFile (const juce::File& baseIRFile, MicPath path);

    void reloadSynthIR();

    /** True when current IR came from IR Synth (not from file list). */
    bool isIRFromSynth() const { return irFromSynth; }

    /** Current IR buffer for waveform display (read-only). May be empty. */
    const juce::AudioBuffer<float>& getCurrentIRBuffer() const { return currentIRBuffer; }
    /** Sample rate of the current IR (for saving). */
    double getCurrentIRSampleRate() const { return currentIRSampleRate; }

    /** Save current IR to the IR folder with the given name (no extension). Returns the saved file.
        Also saves IRSynthParams sidecar (.ping) for recall when loading from IR Synth list. */
    juce::File saveCurrentIRToFile (const juce::String& name);

    /** Write the current synth IR set (MAIN from currentIRBuffer + any non-empty
        rawSynth{Direct,Outrig,Ambient}Buffer) into destDir using <stem>.wav as the
        MAIN and <stem>_direct.wav / _outrig.wav / _ambient.wav as aux siblings.
        Also writes <stem>.ping alongside the MAIN. Creates destDir if missing.
        Returns the MAIN File on success, or juce::File() on failure / no IR loaded. */
    juce::File writeSynthIRSetToDirectory (const juce::File& destDir, const juce::String& stem);

    /** Load IRSynthParams from a .ping sidecar if it exists. Returns default params if not found. */
    static IRSynthParams loadIRSynthParamsFromSidecar (const juce::File& irFile);

    /** Fix permissions (0644) and strip macOS quarantine on an imported file.
        Ensures the plugin can read files received via AirDrop, email, etc. */
    static void fixImportedFilePermissions (const juce::File& f);

    /** Write a .ping sidecar file alongside a WAV, containing the IRSynthParams used to generate it. */
    static void writeIRSynthSidecar (const juce::File& wavFile, const IRSynthParams& p);

    bool getReverse() const { return reverse; }
    void setReverse (bool v) { reverse = v; }

    float getReverseTrim() const;
    void setReverseTrim (float v);

    juce::File getSelectedIRFile() const  { return selectedIRFile; }
    void       setSelectedIRFile (const juce::File& f) { selectedIRFile = f; }

    /** Last IR Synth parameters (room, materials, placement). Persisted with plugin state. */
    const IRSynthParams& getLastIRSynthParams() const { return lastIRSynthParams; }
    void setLastIRSynthParams (const IRSynthParams& p) { lastIRSynthParams = p; }

    /** Last loaded preset name — survives editor destroy/recreate and session save/load. */
    juce::String getLastPresetName() const { return lastPresetName; }
    void setLastPresetName (const juce::String& n) { lastPresetName = n; }

    /** Preset dirty flag — set when any APVTS parameter changes after a preset load/save. */
    bool isPresetDirty() const noexcept { return presetDirty.load(); }
    void setPresetDirty (bool d) noexcept { presetDirty.store (d); }

    /** Snapshot the current APVTS state as the "clean" reference for dirty comparison. */
    void snapshotCleanState();
    /** Compare current APVTS parameter values against the clean snapshot. */
    bool hasParameterChangedSinceSnapshot() const;

    /** IR Synth dirty flag — set when any synth parameter changes after an IR load/save/calculate. */
    bool isIRSynthDirty() const noexcept { return irSynthDirty.load(); }
    void setIRSynthDirty (bool d) noexcept { irSynthDirty.store (d); }

    bool isLicensed() const;
    void setLicence (const LicenceResult& result, const juce::String& serial, const juce::String& displayName = {});
    juce::String getLicenceName() const;
    /** Display name from verified payload only - use this for UI, ignores any stored state. */
    juce::String getLicenceNameFromPayload() const;

    /** Decode legacy ASCII-decimal display names (e.g. "8097117108..." -> "Paul Thomson"). */
    static juce::String decodeLicenceDisplayName (const juce::String& raw);

    float getOutputLevelDb (int channel) const;  // 0 = L, 1 = R
    float getInputLevelDb  (int channel) const;  // 0 = L, 1 = R
    float getErLevelDb     (int channel) const;  // 0 = L, 1 = R
    float getTailLevelDb   (int channel) const;  // 0 = L, 1 = R

    /** Per-path post-mixer peak values (0..1+). Channel 0 = L, 1 = R.
        Updated every processBlock from the per-path mix (post gain, post pan,
        post HP, post mute/solo gating). Used by MicMixerComponent meters. */
    float getPathPeak (MicPath path, int channel) const noexcept;

    /** True iff a real IR has been loaded for the given path (i.e. the path's
        convolver is no longer at unity pass-through). Used by MicMixerComponent
        to disable a strip's power switch when no IR has been calculated for
        that path — preventing the user from enabling a silent path. */
    bool isPathIRLoaded (MicPath path) const noexcept
    {
        switch (path)
        {
            case MicPath::Main:    return mainIRLoaded   .load();
            case MicPath::Direct:  return directIRLoaded .load();
            case MicPath::Outrig:  return outrigIRLoaded .load();
            case MicPath::Ambient: return ambientIRLoaded.load();
        }
        return false;
    }

    /** Pull wet-spectrum samples for GUI (lock-free). Returns num samples copied, or 0 if not ready. */
    int pullSpectrumSamples (float* dest, int maxSamples);

    /** True while preset/session state is being restored (skip redundant IR reloads from APVTS callbacks). */
    bool getIsRestoringState() const noexcept { return isRestoringState.load(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    IRManager irManager;
    juce::dsp::Convolution tailConvolver;   // combined-tail IR for waveform display; audio uses ts* convolvers
    juce::dsp::Convolution tsErConvLL, tsErConvRL, tsErConvLR, tsErConvRR;
    juce::dsp::Convolution tsTailConvLL, tsTailConvRL, tsTailConvLR, tsTailConvRR;

    // ── Multi-mic path convolvers (feature/multi-mic-paths) ──────────────────
    // DIRECT: 4 mono convolvers, no ER/Tail split (IR is too short to split).
    juce::dsp::Convolution tsDirectConvLL, tsDirectConvRL, tsDirectConvLR, tsDirectConvRR;
    // OUTRIG: 8 mono convolvers (ER + Tail × true stereo), full pipeline.
    juce::dsp::Convolution tsOutrigErConvLL,   tsOutrigErConvRL,   tsOutrigErConvLR,   tsOutrigErConvRR;
    juce::dsp::Convolution tsOutrigTailConvLL, tsOutrigTailConvRL, tsOutrigTailConvLR, tsOutrigTailConvRR;
    // AMBIENT: 8 mono convolvers (ER + Tail × true stereo), full pipeline.
    juce::dsp::Convolution tsAmbErConvLL,      tsAmbErConvRL,      tsAmbErConvLR,      tsAmbErConvRR;
    juce::dsp::Convolution tsAmbTailConvLL,    tsAmbTailConvRL,    tsAmbTailConvLR,    tsAmbTailConvRR;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelayLine;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> chorusDelayLine;
    // Stereo decorrelation: 2-stage allpass on R only (7.13 ms, 14.27 ms), incommensurate with FDN
    std::array<int, 2> decorrDelays {};
    std::array<std::vector<float>, 2> decorrBufs;
    std::array<int, 2> decorrPtrs {};
    float decorrG = 0.5f;

    // Post-convolution crossfeed (ER and Tail): L↔R delayed, attenuated copy; on/off per path
    std::vector<float> crossfeedErBufRtoL, crossfeedErBufLtoR, crossfeedTailBufRtoL, crossfeedTailBufLtoR;
    int crossfeedErWriteRtoL = 0, crossfeedErWriteLtoR = 0, crossfeedTailWriteRtoL = 0, crossfeedTailWriteLtoR = 0;
    int crossfeedMaxSamples = 0;

    // Plate onset: pre-convolution allpass diffuser cascade
    // SimpleAllpass: Schroeder allpass section with variable effective delay length.
    // effLen = 0 means use buf.size() as the wrap length.
    struct SimpleAllpass {
        std::vector<float> buf;
        int   ptr    = 0;
        int   effLen = 0;   // effective delay length (≤ buf.size()); set each block for plateSize support
        float g      = 0.7f;
        float process (float x) noexcept {
            int len = (effLen > 0 && effLen <= (int)buf.size()) ? effLen : (int)buf.size();
            float d = buf[(size_t)ptr];
            float w = x + g * d;
            buf[(size_t)ptr] = w;
            ptr = (ptr + 1) % len;
            return d - g * w;
        }
    };
    static constexpr int kNumPlateStages = 6;
    std::array<std::array<SimpleAllpass, kNumPlateStages>, 2> plateAPs; // [ch][stage]
    // 1-pole lowpass state for plateColour (one value per channel)
    std::array<float, 2> plateShelfState { 0.f, 0.f };
    // Pre-allocated buffer for the processed plate signal (used across pre/post-convolution injection points)
    juce::AudioBuffer<float> plateBuffer;

    // ── Bloom hybrid ──────────────────────────────────────────────────────────
    // 6-stage allpass cascade (reuses SimpleAllpass defined above).
    // Separate L/R prime sets for genuine stereo independence:
    //   L: {241, 383, 577, 863, 1297, 1913}  (~5–40 ms at 48 kHz)
    //   R: {263, 431, 673, 1049, 1531, 2111} (~5.5–44 ms at 48 kHz)
    // g = 0.35f hardcoded (transparent scatter). Buffers allocated at 2× base primes
    // so bloomSize 0.25–2.0 needs no reallocation. effLen set each block via bloomSize.
    static constexpr int kNumBloomStages     = 6;
    static constexpr int kBloomFeedbackMaxMs = 500;
    std::array<std::array<SimpleAllpass, kNumBloomStages>, 2> bloomAPs; // [ch][stage]
    // Circular buffer holds post-EQ wet signal for feedback re-injection
    std::array<std::vector<float>, 2> bloomFbBufs;
    std::array<int, 2>                bloomFbWritePtrs { 0, 0 };
    // Per-block intermediate buffer: cascade output stored here so both the IR-feed
    // injection (pre-conv) and the volume injection (post-conv) read the same values.
    juce::AudioBuffer<float> bloomBuffer;

    // ── Cloud Granular Delay ──────────────────────────────────────────────────
    // Pre-convolution granular delay engine. Reads Hann-windowed grains at
    // random positions in a 3-second circular capture buffer.
    //
    // DENSITY (cloudRate 0.1–4.0) controls spawn interval via exponential curve:
    //   t=0  → 205–410 ms apart (~0 grains overlapping at 200 ms LENGTH)
    //   t=0.5 → 33–67 ms apart (~4 grains at 200 ms LENGTH)
    //   t=1.0 →  9–18 ms apart (~14 grains at 200 ms LENGTH)
    // LENGTH (cloudSize 25–1000 ms) sets grain length directly.
    // Grains scatter across the FULL 3-second buffer (not just 1–2 grain lengths back)
    //   so concurrent grains draw from different time positions → cloud texture, not delay.
    // WIDTH (cloudDepth 0–1): stereo spread + probability of reverse grains.
    // FEEDBACK (cloudFeedback 0–0.7): grain output mixed back into capture.
    // 4-stage all-pass diffusion applied to grain sum (Clouds-style TEXTURE smearing).
    // cloudVolume: added post-dry/wet blend (audible at any wet level).
    static constexpr int   kNumCloudGrains       = 40;     // max simultaneous grain voices (Clouds-style density)
    static constexpr float kCloudCaptureBufMs    = 3000.0f; // 3 s for longest grains + headroom
    static constexpr int   kNumCloudDiffuseStages = 4;    // all-pass diffusion cascade (Clouds TEXTURE-style)

    struct CloudGrain {
        float readPos  = 0.f;   // fractional read position in capture buffer
        int   grainLen = 0;     // grain length in samples
        float phase    = 1.f;   // 0..1 through grain; ≥ 1.0 = inactive
        bool  reverse  = false; // play grain backwards
        int   srcCh    = -1;    // -1 = normal stereo, 0 = L-only, 1 = R-only
    };

    std::array<std::vector<float>, 2>       cloudCaptureBufs;         // [ch] circular capture
    std::array<int, 2>                      cloudCaptureWritePtrs {};  // per-channel write heads
    std::array<CloudGrain, kNumCloudGrains> cloudGrains;
    float                                   cloudSpawnPhase    = 0.f;
    float                                   cloudCurrentSpawnIntervalSamps = 24000.f;
    int                                     cloudNextGrainSlot = 0;    // round-robin index
    uint32_t                                cloudSpawnSeed     = 12345u;
    std::array<float, 2>                    cloudFbSamples     { 0.f, 0.f }; // feedback state

    // 4-stage all-pass diffusion cascade applied to grain output (Clouds-style TEXTURE smearing).
    // Delays: 13.7 / 7.3 / 4.1 / 1.7 ms (prime-spaced, sub-15 ms to avoid echo perception).
    // g = 0.65f on all stages. effLen = 0 (uses buf.size()). Allocated in prepareToPlay.
    std::array<std::array<SimpleAllpass, kNumCloudDiffuseStages>, 2> cloudDiffuseAPs; // [ch][stage]

    // Same-block bridge: written pre-conv, read post-blend.
    juce::AudioBuffer<float> cloudBuffer;

    // ── Shimmer ───────────────────────────────────────────────────────────────
    // 8-voice harmonic shimmer cloud. Every voice reads the CLEAN pre-conv dry
    // signal and injects a pitch-shifted copy into the convolver input (× shimIRFeed).
    // There is NO feedback path — the IR provides all smearing and repetition.
    //
    // Voice pitch layout (shimPitch = user interval N semitones):
    //   Voice 0:  0 st               — unshifted (body / unison reverb tail)
    //   Voice 1: +N st               — fundamental shimmer interval
    //   Voice 2: +2N st              — 2nd harmonic upward
    //   Voice 3: −N st               — 1st harmonic downward
    //   Voice 4: +3N st              — 3rd harmonic upward
    //   Voice 5: −2N st              — 2nd harmonic downward
    //   Voice 6:  0 st + 3 cents     — chorus double of voice 0  (fixed detune)
    //   Voice 7: +N st + 6 cents     — chorus double of voice 1  (fixed wider detune)
    //
    // shimFeedback (0–0.7) = LFO modulation depth on per-voice grain delay:
    //   at 0   → all voices use 300 ms fixed delay
    //   at 0.7 → per-voice LFO sweeps delay 300–750 ms at 0.5 Hz
    // Per-voice LFO phases are spread 45° apart (2π/8 per voice) for de-correlation.
    //
    // 2-stage per-voice allpass (base delays 7 ms / 14 ms) slowly modulated at
    // ~0.2 Hz (phases spread independently) for additional spectral smearing.
    //
    // ±25% per-grain LCG jitter on delay at each grain boundary gives organic timing.
    // kShimBufLen holds max grain (500 ms) + max voice delay (1.6 × 1000 ms = 1600 ms) + headroom.
    static constexpr int kNumShimVoices = 8;
    static constexpr int kShimGrainLen  = 9600;   // 200 ms at 48 kHz (legacy constant)
    static constexpr int kShimBufLen    = 131072;  // 2.73 s at 48 kHz

    struct ShimmerVoice {
        std::vector<float> grainBuf;   // kShimBufLen samples, circular
        int   writePtr    = 0;
        float readPtrA    = 0.f;
        float readPtrB    = 0.f;       // initialised to kShimGrainLen/2 in prepareToPlay
        float grainPhaseA = 0.f;
        float grainPhaseB = 0.5f;
    };

    // [voice][ch] — 8 harmonic voices × 2 channels
    std::array<std::array<ShimmerVoice, 2>, kNumShimVoices> shimVoicesHarm;
    juce::AudioBuffer<float> shimBuffer;    // per-block scratch: sum of all voice outputs
    uint32_t                 shimRng = 0x92d68ca2u; // LCG for per-grain delay jitter

    // Per-voice LFO state (main delay LFO at 0.5 Hz; allpass LFO at 0.2 Hz).
    // Phases initialised with 45° (2π/8) offsets so voices are de-correlated.
    std::array<float, kNumShimVoices> shimLfoPhase   {};   // main delay LFO phases
    std::array<float, kNumShimVoices> shimApLfoPhase {};   // allpass modulation LFO phases

    // 2-stage allpass per voice × 2 channels: shimAPs[voice][stage][ch]
    // Stage 0: base 7 ms (allocated 2× = 14 ms); stage 1: base 14 ms (allocated 2× = 28 ms).
    // g = 0.5f. effLen set each block from allpass LFO.
    std::array<std::array<std::array<SimpleAllpass, 2>, 2>, kNumShimVoices> shimAPs;

    // Staggered onset: voice vi waits (vi+1) × shimDelay ms before contributing output.
    // Counters are armed (in samples) when shimOn transitions false→true, then count down.
    // The grain buffer still fills during the silent period so voices have real content
    // at onset. Counts down at block granularity; unlock is sample-accurate within a block.
    std::array<int,  kNumShimVoices> shimOnsetCounters {};
    bool shimWasEnabled = false;

    // Per-voice delay lines for sustain feedback (8 voices × 2 channels).
    // Each voice has its own period (kShimVoiceMultiplier, 0.4–1.6× the DELAY knob)
    // so echoes are widely spread — no audible pulse repeats at any DELAY setting.
    std::array<std::array<std::vector<float>, 2>, kNumShimVoices> shimDelayBufs;
    std::array<std::array<int, 2>, kNumShimVoices> shimDelayPtrs {};

    juce::dsp::Gain<float> dryGain, wetGain;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lowBand, midBand, highBand, lowShelfBand, highShelfBand;
    juce::SmoothedValue<float> inputGainSmoothed;
    juce::SmoothedValue<float> outputGainSmoothed;
    juce::SmoothedValue<float> saturatorDriveSmoothed;
    juce::SmoothedValue<float> erLevelSmoothed;
    juce::SmoothedValue<float> tailLevelSmoothed;

    // Per-mic-path mixer strips (MAIN / DIRECT / OUTRIG / AMBIENT).
    // Each strip has a smoothed linear gain and a 2nd-order HP. Pan is sampled once per block
    // (it changes slowly and small per-block steps are inaudible). Mute / Solo / On flags are
    // folded into the target gain: a strip contributes zero when gated off, knob value otherwise.
    // Defaults (mainGain = 0 dB, pan = 0, HP off, On=true, Mute=Solo=false, all extras Off)
    // collapse the mixer to a pure ×1.0 passthrough, so MAIN output stays bit-identical
    // to the pre-C9 single-path processBlock.
    juce::SmoothedValue<float> mainGainSmoothed,   directGainSmoothed,   outrigGainSmoothed,   ambientGainSmoothed;
    HP2ndOrder                 mainHP,             directHP,             outrigHP,             ambientHP;
    // Atomics for future per-strip level meters (populated in processBlock; read from GUI).
    std::atomic<float> mainPeakL { 0.f },    mainPeakR { 0.f };
    std::atomic<float> directPeakL { 0.f },  directPeakR { 0.f };
    std::atomic<float> outrigPeakL { 0.f },  outrigPeakR { 0.f };
    std::atomic<float> ambientPeakL { 0.f }, ambientPeakR { 0.f };

    juce::AudioBuffer<float> currentIRBuffer;
    juce::AudioBuffer<float> rawSynthBuffer;      // raw (pre-processing) copy of last synth IR (MAIN)
    juce::AudioBuffer<float> rawSynthDirectBuffer;   // raw copy of last DIRECT synth IR
    juce::AudioBuffer<float> rawSynthOutrigBuffer;   // raw copy of last OUTRIG synth IR
    juce::AudioBuffer<float> rawSynthAmbientBuffer;  // raw copy of last AMBIENT synth IR

    // Per-path "IR loaded" flags. Required because juce::dsp::Convolution defaults to
    // a unity (pass-through) impulse response until loadImpulseResponse() is called.
    // Without these flags, enabling a DIRECT/OUTRIG/AMBIENT mixer strip before that path
    // has ever been synthesised produces a dry pass-through of the post-predelay signal
    // instead of silence. processBlock skips the per-path mixer contribution when the
    // corresponding flag is false; setFromSynth / loadIRFromBuffer flip it true when a
    // real IR is loaded (or a synth path is explicitly cleared).
    std::atomic<bool> mainIRLoaded    { false };
    std::atomic<bool> directIRLoaded  { false };
    std::atomic<bool> outrigIRLoaded  { false };
    std::atomic<bool> ambientIRLoaded { false };

    // Per-path "convolvers fully ready last block" tracker. Used by processBlock to detect
    // the transition not-ready → ready (when all convolvers in a path have published their
    // real IR after loadImpulseResponse's background NUPC threads finish), so a fresh
    // wet-bus fade can be armed at that moment. Without this, a path's convolvers can
    // become ready after the irLoadFadeSamplesRemaining fade has already expired, producing
    // an audible click when the wet path suddenly unmutes at full gain. Processed only on
    // the audio thread; atomic<bool> is defensive — not strictly required.
    std::atomic<bool> mainConvPrevReady    { false };
    std::atomic<bool> directConvPrevReady  { false };
    std::atomic<bool> outrigConvPrevReady  { false };
    std::atomic<bool> ambientConvPrevReady { false };
    double rawSynthSampleRate = 48000.0;           // shared — all four paths share the same SR
    double currentSampleRate = 48000.0;
    juce::File selectedIRFile;   // empty = synth IR or nothing loaded
    bool irFromSynth = false;
    double synthesizedIRSampleRate = 48000.0;
    double currentIRSampleRate = 48000.0;
    bool reverse = false;
    float lfoPhase = 0.0f;
    float tailLfoPhase = 0.0f;
    juce::File lastLoadedIRFile;
    IRSynthParams lastIRSynthParams;
    juce::String lastPresetName { "Default" };

    // IR load crossfade: prevents partial-swap distortion when switching IRs while audio plays.
    // The 8 convolvers (tsEr* / tsTail*) load asynchronously via JUCE background threads and
    // swap in independently during their own process() calls. Between switches, some convolvers
    // may run the new IR while others still run the old one, producing a wrong-level mixed output
    // that sounds like distortion. Arming this counter before loadImpulseResponse calls causes
    // processBlock to fade the wet bus from silence while the swap-in window passes.
    std::atomic<int> irLoadFadeSamplesRemaining { 0 };
    static constexpr int kIRLoadFadeSamples = 48000; // 1 s at 48 kHz — sample-based fade is
                                                      // consistent across buffer sizes (block-count
                                                      // fades collapse to ~170 ms at 128-sample buffers)

    // Set to true at the start of setStateInformation, cleared asynchronously (via
    // MessageManager::callAsync) AFTER all queued parameterChanged notifications have fired.
    // PingEditor::parameterChanged checks this flag and skips loadSelectedIR() while set,
    // preventing the extra IR reloads that apvts.replaceState() triggers for "stretch"/"decay".
    // Without this, a preset load causes 3 × 8 = 24 loadImpulseResponse calls in milliseconds,
    // swamping the NUPC background thread and causing persistent crackling.
    std::atomic<bool> isRestoringState { false };
    std::atomic<bool> stateWasRestored { false };
    std::atomic<bool> presetDirty { false };
    std::atomic<bool> irSynthDirty { false };

    std::vector<float> cleanParamSnapshot;

    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    // Set to true the first time prepareToPlay completes.  Used in setStateInformation to
    // distinguish an initial session load (prepareToPlay not yet run) from a live preset switch.
    // During initial load, loadImpulseResponse must NOT be called before prepareToPlay resets and
    // prepares the convolvers: doing so spawns JUCE background threads that race with reset(),
    // corrupting NUPC internal state and causing permanent distortion / escalating crackling.
    // When false: setStateInformation saves rawSynthBuffer / selectedIRFile but defers the
    //   actual loadImpulseResponse calls to a callAsync posted at the END of prepareToPlay.
    // When true (live preset switch): setStateInformation calls loadIRFromFile immediately;
    //   no prepareToPlay follows so there is nothing to race against.
    std::atomic<bool> audioEnginePrepared { false };

    std::atomic<float> outputLevelPeakL { 0.0f };
    std::atomic<float> outputLevelPeakR { 0.0f };
    std::atomic<float> inputLevelPeakL  { 0.0f };
    std::atomic<float> inputLevelPeakR  { 0.0f };
    std::atomic<float> erLevelPeakL     { 0.0f };
    std::atomic<float> erLevelPeakR     { 0.0f };
    std::atomic<float> tailLevelPeakL   { 0.0f };
    std::atomic<float> tailLevelPeakR   { 0.0f };

    static constexpr int spectrumFftSize = 2048;
    float spectrumFifo[spectrumFftSize];
    int spectrumFifoIndex = 0;
    float spectrumFftData[spectrumFftSize * 2];
    std::atomic<bool> spectrumBlockReady { false };
    void pushWetSampleForSpectrum (float s) noexcept;

    LicenceResult currentLicence;
    juce::String savedLicenceSerial;
    juce::String licenceDisplayName;

    void loadStoredLicence();

    void updateGains();
    void updatePredelay();
    void updateEQ();
    void applyWidth (juce::AudioBuffer<float>& wet, float width);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingProcessor)
};
