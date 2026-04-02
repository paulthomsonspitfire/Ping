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

    int getSelectedIRIndex() const { return selectedIRIndex; }
    void setSelectedIRIndex (int index) { selectedIRIndex = index; }

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
    juce::dsp::Convolution erConvolver;
    juce::dsp::Convolution tailConvolver;
    juce::dsp::Convolution tsErConvLL, tsErConvRL, tsErConvLR, tsErConvRR;
    juce::dsp::Convolution tsTailConvLL, tsTailConvRL, tsTailConvLR, tsTailConvRR;
    bool useTrueStereo = false;
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
    // DENSITY (cloudRate 0.1–4.0) controls grain length and spawn interval:
    //   Low density → long sparse grains (400–1000 ms, 500–1000 ms apart)
    //   High density → short dense grains (25–75 ms, 10–50 ms apart)
    // SIZE (cloudSize 0.25–4.0) is a global multiplier on grain length.
    // WIDTH (cloudDepth 0–1): stereo spread + probability of reverse grains.
    // FEEDBACK (cloudFeedback 0–0.7): grain output mixed back into capture.
    // cloudVolume: added post-dry/wet blend (audible at any wet level).
    static constexpr int   kNumCloudGrains    = 16;     // max simultaneous grain voices
    static constexpr float kCloudCaptureBufMs = 3000.0f; // 3 s for longest grains + headroom

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

    // Same-block bridge: written pre-conv, read post-blend.
    juce::AudioBuffer<float> cloudBuffer;

    // ── Shimmer ───────────────────────────────────────────────────────────────
    // Two signal paths:
    //   shimVoices     — IR Feed: reads pre-conv dry (delayed by shimDelay ms), injects × shimIRFeed
    //   shimVoicesVol  — Feedback: reads post-conv wet, pitch-shifts, stores in shimFeedbackBuf.
    //                    shimFeedbackBuf injected × shimFeedback into convolver next block.
    //                    Loop: wet → pitch-shift (+N st) → convolver → wet (stacks octaves each pass).
    //
    // Grain size (shimSize): 50–500 ms — large grains for smooth, stable shimmer
    // Delay (shimDelay): 0–1000 ms — how far back in the grain buffer to start reading
    //   (controls how long before the shimmer octave appears after a note)
    // kShimBufLen must hold: max grain (500 ms) + max delay (1000 ms) + headroom ≈ 2.5 s = 120000
    static constexpr int kShimGrainLen = 9600;    // default 200 ms at 48 kHz (legacy; not used in DSP)
    static constexpr int kShimBufLen   = 131072;  // 2.73 s at 48 kHz — covers grain + delay range

    struct ShimmerVoice {
        std::vector<float> grainBuf;   // kShimBufLen samples, circular
        int   writePtr    = 0;
        float readPtrA    = 0.f;
        float readPtrB    = 0.f;       // initialised to kShimGrainLen/2 in prepareToPlay
        float grainPhaseA = 0.f;
        float grainPhaseB = 0.5f;
    };

    std::array<ShimmerVoice, 2> shimVoices;          // [ch] — IR Feed path (pre-conv dry)
    std::array<ShimmerVoice, 2> shimVoicesVol;       // [ch] — Feedback path (post-conv wet)
    juce::AudioBuffer<float>    shimBuffer;          // same-block bridge: IR Feed grain output
    juce::AudioBuffer<float>    shimFeedbackBuf;     // cross-block bridge: feedback grain output → next block's convolver input
    int                         shimFeedbackLastBlockSize = 0;
    int                         shimLastBlockSize = 0;

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
    int selectedIRIndex = -1;
    bool irFromSynth = false;
    double synthesizedIRSampleRate = 48000.0;
    double currentIRSampleRate = 48000.0;
    bool reverse = false;
    float lfoPhase = 0.0f;
    float tailLfoPhase = 0.0f;
    juce::File lastLoadedIRFile;
    IRSynthParams lastIRSynthParams;

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
