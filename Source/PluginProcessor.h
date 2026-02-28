#pragma once

#include <JuceHeader.h>
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
    juce::dsp::Gain<float> dryGain, wetGain;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lowBand, midBand, highBand;
    juce::SmoothedValue<float> inputGainSmoothed;
    juce::SmoothedValue<float> saturatorDriveSmoothed;
    juce::SmoothedValue<float> erLevelSmoothed;
    juce::SmoothedValue<float> tailLevelSmoothed;

    juce::AudioBuffer<float> currentIRBuffer;
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
