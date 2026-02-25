#pragma once

#include <JuceHeader.h>

/** Thin rotary knobs and concept-style big Dry/Wet knob. */
class PingLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PingLookAndFeel();
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool isMouseOver, bool isButtonDown) override;
    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                        bool isMouseOver, bool isButtonDown) override;

private:
    juce::Colour trackColour    { 0xff3a3a3a };
    juce::Colour fillColour    { 0xffe0e0e0 };
    juce::Colour thumbColour   { 0xffffffff };
    juce::Colour centreDark   { 0xff1a1a1a };
};
