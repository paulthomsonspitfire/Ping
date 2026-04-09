#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "IRManager.h"
#include "IRSynthEngine.h"
#include "LicenceVerifier.h"

class PingProcessor : public juce::AudioProcessor
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

    /** Load IR from buffer (e.g. reversed). Call from message thread.
        If fromSynth is true, marks current IR as synthesized and persists it with plugin state. */
    void loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate, bool fromSynth = false);
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

    /** Load IRSynthParams from a .ping sidecar if it exists. Returns default params if not found. */
    static IRSynthParams loadIRSynthParamsFromSidecar (const juce::File& irFile);

    bool getReverse() const { return reverse; }
    void setReverse (bool v) { reverse = v; }

    float getReverseTrim() const;
    void setReverseTrim (float v);

    juce::File getSelectedIRFile() const  { return selectedIRFile; }
    void       setSelectedIRFile (const juce::File& f) { selectedIRFile = f; }

    /** Last IR Synth parameters (room, materials, placement). Persisted with plugin state. */
    const IRSynthParams& getLastIRSynthParams() const { return lastIRSynthParams; }
    void setLastIRSynthParams (const IRSynthParams& p) { lastIRSynthParams = p; }

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

    /** Pull wet-spectrum samples for GUI (lock-free). Returns num samples copied, or 0 if not ready. */
    int pullSpectrumSamples (float* dest, int maxSamples);

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    IRManager irManager;
    juce::dsp::Convolution tailConvolver;   // combined-tail IR for waveform display; audio uses ts* convolvers
    juce::dsp::Convolution tsErConvLL, tsErConvRL, tsErConvLR, tsErConvRR;
    juce::dsp::Convolution tsTailConvLL, tsTailConvRL, tsTailConvLR, tsTailConvRR;
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

    // Per-block processBlock scratch buffers — pre-allocated in prepareToPlay to avoid
    // heap allocation on the audio thread (eliminates ~8 malloc calls per processBlock).
    // NOTE: convTmp is wrapped in AudioBlock using the explicit-size constructor
    //       (getArrayOfWritePointers, 1, numSamples) so it always processes exactly
    //       numSamples regardless of the allocated capacity.
    juce::AudioBuffer<float> dryBuffer;   // [2 ch × samplesPerBlock]
    juce::AudioBuffer<float> convLIn;     // [1 ch] true-stereo: L input
    juce::AudioBuffer<float> convRIn;     // [1 ch] true-stereo: R input
    juce::AudioBuffer<float> convTmp;     // [1 ch] convolution intermediate (reused 8×)
    juce::AudioBuffer<float> convLEr;     // [1 ch] L early reflections
    juce::AudioBuffer<float> convREr;     // [1 ch] R early reflections
    juce::AudioBuffer<float> convLTail;   // [1 ch] L tail
    juce::AudioBuffer<float> convRTail;   // [1 ch] R tail

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

    juce::AudioBuffer<float> currentIRBuffer;
    juce::AudioBuffer<float> rawSynthBuffer;      // raw (pre-processing) copy of last synth IR
    double rawSynthSampleRate = 48000.0;
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

    // IR load crossfade: prevents partial-swap distortion when switching IRs while audio plays.
    // The 8 convolvers (tsEr* / tsTail*) load asynchronously via JUCE background threads and
    // swap in independently during their own process() calls. Between switches, some convolvers
    // may run the new IR while others still run the old one, producing a wrong-level mixed output
    // that sounds like distortion. Arming this counter before loadImpulseResponse calls causes
    // processBlock to fade the wet bus from silence while the swap-in window passes.
    std::atomic<int> irLoadFadeBlocksRemaining { 0 };
    static constexpr int kIRLoadFadeBlocks = 64; // ~680 ms at 512 samples / 48 kHz — covers
                                                  // worst-case background thread prep for long IRs

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
