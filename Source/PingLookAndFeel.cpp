#include "PingLookAndFeel.h"
#include "PingBinaryData.h"

static const juce::Colour reverseRedFill { 0xffb83030 };

PingLookAndFeel::PingLookAndFeel() {}

void PingLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& bg, bool isOver, bool isDown)
{
    if (button.getComponentID() != "Reverse")
    {
        juce::LookAndFeel_V4::drawButtonBackground (g, button, bg, isOver, isDown);
        return;
    }
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 6.0f;
    bool engaged = button.getToggleState();
    if (engaged)
    {
        g.setColour (reverseRedFill);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (reverseRedFill.darker (0.3f));
        g.drawRoundedRectangle (bounds, corner, 1.8f);
    }
    else
    {
        auto img = juce::ImageCache::getFromMemory (BinaryData::reverse_button_png, BinaryData::reverse_button_pngSize);
        if (img.isValid())
            g.drawImageWithin (img, (int) bounds.getX(), (int) bounds.getY(), (int) bounds.getWidth(), (int) bounds.getHeight(), juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (juce::Colour (0xff1a1a1a));
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (juce::Colour (0xff8b2020));
            g.drawRoundedRectangle (bounds, corner, 1.8f);
        }
    }
}

void PingLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                      bool, bool)
{
    if (button.getComponentID() != "Reverse")
    {
        juce::LookAndFeel_V4::drawButtonText (g, button, false, false);
        return;
    }
    if (! button.getToggleState())
        return;
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
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

    const bool isBigKnob = (width >= 72);
    juce::Colour fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
    juce::Colour thumb = slider.findColour (juce::Slider::thumbColourId);

    if (isBigKnob)
    {
        float outerRadius = radius;
        float innerRadius = radius * 0.52f;
        float lineW = 1.2f;

        g.setColour (trackColour);
        g.drawEllipse (centreX - outerRadius, centreY - outerRadius, outerRadius * 2, outerRadius * 2, lineW);

        g.setColour (fill);
        juce::Path arc;
        arc.addCentredArc (centreX, centreY, outerRadius - lineW * 0.5f, outerRadius - lineW * 0.5f, 0,
                           rotaryStartAngle, angle, true);
        g.strokePath (arc, juce::PathStrokeType (lineW));

        float dotRadius = 4.0f;
        float dotDist = innerRadius + (outerRadius - innerRadius) * 0.5f;
        float dotX = centreX + dotDist * std::sin (angle);
        float dotY = centreY - dotDist * std::cos (angle);
        g.setColour (thumb);
        g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2, dotRadius * 2);

        float centreRadius = innerRadius * 0.85f;
        g.setColour (centreDark);
        g.fillEllipse (centreX - centreRadius, centreY - centreRadius, centreRadius * 2, centreRadius * 2);
        g.setColour (trackColour.withAlpha (0.5f));
        for (int i = 0; i < 24; ++i)
        {
            float a = (float) i * (juce::MathConstants<float>::twoPi / 24.0f);
            float r = centreRadius * (0.7f + 0.15f * (float) (i % 3) / 2.0f);
            float px = centreX + r * std::sin (a);
            float py = centreY - r * std::cos (a);
            g.fillEllipse (px - 0.8f, py - 0.8f, 1.6f, 1.6f);
        }
    }
    else
    {
        float trackRadius = radius - 3.0f;
        float lineW = 1.2f;

        g.setColour (trackColour);
        g.drawEllipse (centreX - trackRadius, centreY - trackRadius, trackRadius * 2, trackRadius * 2, lineW);

        g.setColour (fill);
        juce::Path arc;
        arc.addCentredArc (centreX, centreY, trackRadius - lineW * 0.5f, trackRadius - lineW * 0.5f, 0,
                           rotaryStartAngle, angle, true);
        g.strokePath (arc, juce::PathStrokeType (lineW));

        float dotRadius = 3.0f;
        float dotDist = trackRadius - 2.0f;
        float dotX = centreX + dotDist * std::sin (angle);
        float dotY = centreY - dotDist * std::cos (angle);
        g.setColour (thumb);
        g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2, dotRadius * 2);
    }
}
