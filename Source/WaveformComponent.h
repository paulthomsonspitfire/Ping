#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/** Waveform display with optional reverse-trim line when Reverse is engaged. */
class WaveformComponent : public juce::Component
{
public:
    explicit WaveformComponent (PingProcessor& processor);

    void paint (juce::Graphics& g) override;
    void mouseDown (juce::MouseEvent const& e) override;
    void mouseDrag (juce::MouseEvent const& e) override;
    void mouseUp (juce::MouseEvent const& e) override;

    void setOnTrimChanged (std::function<void()> fn) { onTrimChanged = std::move (fn); }

private:
    PingProcessor& processor;
    std::function<void()> onTrimChanged;
    bool draggingTrim = false;

    juce::Rectangle<float> getWaveformInnerBounds() const;
    float trimPositionToFraction (float x) const;
};
