#pragma once

#include <JuceHeader.h>

class EQGraphComponent : public juce::Component,
                         private juce::Timer
{
public:
    explicit EQGraphComponent (juce::AudioProcessorValueTreeState& apvts);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> getGraphArea() const;
    int hitTestBand (juce::Point<float> pos) const;
    void freqGainToXY (float freqHz, float gainDb, float& x, float& y) const;
    void xyToFreqGain (float x, float y, float& freqHz, float& gainDb) const;
    float getResponseAt (float freqHz) const;
    void syncSlidersFromParams();

    juce::AudioProcessorValueTreeState& apvts;
    juce::Slider freqSlider;
    juce::Slider gainSlider;
    juce::Slider qSlider;
    juce::Label freqLabel;
    juce::Label gainLabel;
    juce::Label qLabel;
    juce::TextButton band1Button { "1" };
    juce::TextButton band2Button { "2" };
    juce::TextButton band3Button { "3" };
    int selectedBand = 0;
    int draggingBand = -1;
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minGain = -12.0f;
    static constexpr float maxGain = 12.0f;
    static constexpr int numBands = 3;
    static const char* bandIds[3][3];
};
