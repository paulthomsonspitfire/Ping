#include "PingLookAndFeel.h"
#include "PingBinaryData.h"

static const juce::Colour reverseRedFill   { 0xffb83030 };
static const juce::Colour reverseRedGlow   { 0xffe04040 };
static const juce::Colour buttonBgDark     { 0xff1a1a1a };
static const juce::Colour buttonBorder     { 0xff2a2a2a };
static const juce::Colour accentOrange     { 0xffe8a84a };

PingLookAndFeel::PingLookAndFeel() {}

void PingLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& bg, bool isOver, bool isDown)
{
    juce::String id = button.getComponentID();
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 6.0f;

    if (id == "Reverse")
    {
        bool engaged = button.getToggleState();
        if (engaged)
        {
            // Red glow (soft shadow) for active state — red (not orange)
            juce::DropShadow redGlow (reverseRedGlow, 14, { 0, 1 });
            juce::Path p;
            p.addRoundedRectangle (bounds, corner);
            redGlow.drawForPath (g, p);

            g.setColour (reverseRedFill);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (reverseRedFill.darker (0.2f));
            g.drawRoundedRectangle (bounds, corner, 1.5f);

            // Glowing version of Reverse icon for active state
            auto img = juce::ImageCache::getFromMemory (BinaryData::reverse_button_glow_png, BinaryData::reverse_button_glow_pngSize);
            if (img.isValid())
                g.drawImageWithin (img, (int) bounds.getX(), (int) bounds.getY(),
                                   (int) bounds.getWidth(), (int) bounds.getHeight(),
                                   juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (buttonBgDark);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (buttonBorder);
            g.drawRoundedRectangle (bounds, corner, 1.2f);

            auto img = juce::ImageCache::getFromMemory (BinaryData::reverse_button_png, BinaryData::reverse_button_pngSize);
            if (img.isValid())
                g.drawImageWithin (img, (int) bounds.getX(), (int) bounds.getY(),
                                   (int) bounds.getWidth(), (int) bounds.getHeight(),
                                   juce::RectanglePlacement::stretchToFit);
            else
            {
                g.setColour (juce::Colour (0xff8b2020));
                g.drawRoundedRectangle (bounds, corner, 1.8f);
            }
        }
        return;
    }

    if (id == "SavePreset")
    {
        juce::Colour fill = button.findColour (juce::TextButton::buttonColourId);
        if (isOver || isDown) fill = fill.brighter (isDown ? 0.1f : 0.05f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (buttonBorder);
        g.drawRoundedRectangle (bounds, corner, 1.2f);
        return;
    }

    if (id == "IRSynth")
    {
        juce::Colour fill = button.findColour (juce::TextButton::buttonColourId);
        if (isOver || isDown) fill = fill.brighter (isDown ? 0.1f : 0.05f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour ((isOver || isDown) ? accentOrange : buttonBorder);
        g.drawRoundedRectangle (bounds, corner, 1.2f);
        return;
    }

    if (id == "ERCrossfeedSwitch" || id == "TailCrossfeedSwitch")
    {
        bool on = button.getToggleState();
        float pillRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        g.setColour (buttonBgDark);
        g.fillRoundedRectangle (bounds, pillRadius);
        g.setColour (buttonBorder);
        g.drawRoundedRectangle (bounds, pillRadius, 1.0f);
        float thumbRadius = pillRadius - 2.0f;
        float cx = on ? (bounds.getRight() - 1.0f - thumbRadius) : (bounds.getX() + 1.0f + thumbRadius);
        float cy = bounds.getCentreY();
        g.setColour (on ? accentOrange : juce::Colour (0xff505050));
        g.fillEllipse (cx - thumbRadius, cy - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f);
        g.setColour (on ? accentOrange.brighter (0.2f) : buttonBorder);
        g.drawEllipse (cx - thumbRadius, cy - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f, 1.0f);
        return;
    }

    juce::LookAndFeel_V4::drawButtonBackground (g, button, bg, isOver, isDown);
}

void PingLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                      bool isOver, bool isDown)
{
    juce::String id = button.getComponentID();

    if (id == "Reverse")
    {
        // Both inactive and active states use full button images (reverse_button / reverse_button_glow)
        return;
    }

    if (id == "SavePreset")
    {
        g.setColour (button.findColour (juce::TextButton::textColourOffId));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    if (id == "IRSynth")
    {
        g.setColour (button.findColour (juce::TextButton::textColourOffId));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    juce::LookAndFeel_V4::drawButtonText (g, button, isOver, isDown);
}

void PingLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                        juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2);
    float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    float centreX = bounds.getCentreX();
    float centreY = bounds.getCentreY();
    float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Colour fill = slider.findColour (juce::Slider::rotarySliderFillColourId);

    const float knobRadius = radius - 2.0f;
    const float shadowOffset = 2.5f;

    // Soft shadow (below and to the right)
    g.setColour (juce::Colour (0x28000000));
    g.fillEllipse (centreX - knobRadius + shadowOffset, centreY - knobRadius + shadowOffset,
                   knobRadius * 2.1f, knobRadius * 2.1f);

    // Knob body — radial gradient for convex 3D gloss (lighter centre, darker edge)
    juce::ColourGradient grad (knobBodyLight, centreX - knobRadius * 0.3f, centreY - knobRadius * 0.3f,
                               knobBodyDark, centreX + knobRadius, centreY + knobRadius, true);
    g.setGradientFill (grad);
    g.fillEllipse (centreX - knobRadius, centreY - knobRadius, knobRadius * 2, knobRadius * 2);

    // Subtle edge darkening for depth
    g.setColour (juce::Colour (0x18000000));
    g.drawEllipse (centreX - knobRadius, centreY - knobRadius, knobRadius * 2, knobRadius * 2, 1.0f);

    // Dotted ring around perimeter — orange for active range, grey for inactive
    const int numDots = (width >= 72) ? 36 : 28;
    const float dotRadius = (width >= 72) ? 1.8f : 1.4f;
    const float dotDist = knobRadius - 4.0f;

    for (int i = 0; i < numDots; ++i)
    {
        float a = rotaryStartAngle + (float) i / (float) numDots * (rotaryEndAngle - rotaryStartAngle);
        bool inActiveRange = (rotaryStartAngle <= rotaryEndAngle) ? (a >= rotaryStartAngle && a <= angle)
                                                                 : (a <= rotaryStartAngle && a >= angle);

        g.setColour (inActiveRange ? fill : trackColour.withAlpha (0.6f));
        float px = centreX + dotDist * std::sin (a);
        float py = centreY - dotDist * std::cos (a);
        g.fillEllipse (px - dotRadius, py - dotRadius, dotRadius * 2, dotRadius * 2);
    }

    // Dry/Wet knob: centre label text (same style as other knob labels)
    if (slider.getComponentID() == "DryWet")
    {
        g.setColour (juce::Colour (0xffe8e8e8));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("DRY/WET", bounds.toNearestInt(), juce::Justification::centred, true);
    }
}
