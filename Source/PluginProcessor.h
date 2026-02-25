#pragma once

#include <JuceHeader.h>
#include "IRManager.h"

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

    /** Load IR from buffer (e.g. reversed). Call from message thread. */
    void loadIRFromBuffer (juce::AudioBuffer<float> buffer, double bufferSampleRate);

    /** Current IR buffer for waveform display (read-only). May be empty. */
    const juce::AudioBuffer<float>& getCurrentIRBuffer() const { return currentIRBuffer; }

    bool getReverse() const { return reverse; }
    void setReverse (bool v) { reverse = v; }

    float getReverseTrim() const;
    void setReverseTrim (float v);

    int getSelectedIRIndex() const { return selectedIRIndex; }
    void setSelectedIRIndex (int index) { selectedIRIndex = index; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    IRManager irManager;
    juce::dsp::Convolution convolution;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelayLine;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> chorusDelayLine;
    juce::dsp::Gain<float> dryGain, wetGain;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lowBand, midBand, highBand;

    juce::AudioBuffer<float> currentIRBuffer;
    double currentSampleRate = 48000.0;
    int selectedIRIndex = -1;
    bool reverse = false;
    float lfoPhase = 0.0f;
    float tailLfoPhase = 0.0f;
    juce::File lastLoadedIRFile;

    void updateGains();
    void updatePredelay();
    void updateEQ();
    void applyWidth (juce::AudioBuffer<float>& wet, float width);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PingProcessor)
};
