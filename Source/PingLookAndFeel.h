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
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                        bool isMouseOver, bool isButtonDown) override;
    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

private:
    juce::Colour trackColour    { 0xff3a3a3a };
    juce::Colour fillColour    { 0xffe0e0e0 };
    juce::Colour thumbColour   { 0xffffffff };
    juce::Colour centreDark    { 0xff1a1a1a };
    juce::Colour knobBodyDark  { 0xff1e1e1e };
    juce::Colour knobBodyLight { 0xff353535 };
};
