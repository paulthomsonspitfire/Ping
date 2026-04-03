#include "PingLookAndFeel.h"
#include "PingBinaryData.h"

static const juce::Colour reverseRedFill   { 0xffb83030 };
static const juce::Colour reverseRedGlow   { 0xffe04040 };
static const juce::Colour buttonBgDark     { 0xff1a1a1a };
static const juce::Colour buttonBorder     { 0xff2a2a2a };
static const juce::Colour accentIce        { 0xff8cd6ef };   // icy blue-white
static const juce::Colour accentLed        { 0xffc4ecf8 };   // near-white for pill LED centre

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
        // Use full local bounds (no reduction) so height matches the adjacent combo box visually
        auto fullBounds = button.getLocalBounds().toFloat();
        // Semi-transparent white fill — lets the background texture show through at the same
        // brightness level as the header bar. Slightly more opaque on hover/down.
        const juce::uint8 alpha = isDown ? 0x58 : (isOver ? 0x48 : 0x38);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillRoundedRectangle (fullBounds, corner);
        g.setColour ((isOver || isDown) ? accentIce : buttonBorder);
        g.drawRoundedRectangle (fullBounds.reduced (0.5f), corner, 0.8f);
        return;
    }

    juce::LookAndFeel_V4::drawButtonBackground (g, button, bg, isOver, isDown);
}

void PingLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::String id = button.getComponentID();
    if (id == "ERCrossfeedSwitch" || id == "TailCrossfeedSwitch" || id == "PlateSwitch"
        || id == "BloomSwitch" || id == "CloudSwitch" || id == "ShimmerSwitch")
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        bool on      = button.getToggleState();
        bool hovered = shouldDrawButtonAsHighlighted;

        const float cx  = bounds.getCentreX();
        const float cy  = bounds.getCentreY();
        const float dim = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const float bgR = dim * 0.48f;

        // ── Circular body ─────────────────────────────────────────────────
        // Soft drop-shadow
        g.setColour (juce::Colour (0x28000000));
        g.fillEllipse (cx - bgR + 1.0f, cy - bgR + 1.5f, bgR * 2.0f, bgR * 2.0f);

        // Body — same radial gradient as knob face
        juce::ColourGradient bodyGrad (knobBodyLight,
                                       cx - bgR * 0.3f, cy - bgR * 0.3f,
                                       knobBodyDark,
                                       cx + bgR,        cy + bgR, true);
        g.setGradientFill (bodyGrad);
        g.fillEllipse (cx - bgR, cy - bgR, bgR * 2.0f, bgR * 2.0f);

        // Subtle inner-edge darkening
        g.setColour (juce::Colour (0x18000000));
        g.drawEllipse (cx - bgR, cy - bgR, bgR * 2.0f, bgR * 2.0f, 1.0f);

        // LED radial glow when on
        if (on)
        {
            juce::ColourGradient ledGlow (accentLed.withAlpha (0.85f), cx, cy,
                                          accentIce.withAlpha (0.0f),
                                          cx + bgR, cy, true);
            g.setGradientFill (ledGlow);
            g.fillEllipse (cx - bgR, cy - bgR, bgR * 2.0f, bgR * 2.0f);
        }

        // Border — ice-blue when on, dim grey when off
        juce::Colour borderCol = on ? (hovered ? accentIce.brighter (0.1f) : accentIce)
                                    : (hovered ? juce::Colour (0xff606060) : juce::Colour (0xff404040));
        g.setColour (borderCol);
        g.drawEllipse (cx - bgR, cy - bgR, bgR * 2.0f, bgR * 2.0f, on ? 1.5f : 1.0f);

        // ── Power symbol ──────────────────────────────────────────────────
        const float iconR   = bgR * 0.58f;   // ring radius
        const float gapHalf = 0.60f;          // ~34° gap half-angle at 12 o'clock
        const float strokeW = dim * 0.13f;    // stroke width scaled to button size
        juce::Colour iconCol = on ? accentIce
                                  : (hovered ? juce::Colour (0xff888888) : juce::Colour (0xff555555));
        g.setColour (iconCol);

        // Arc: clockwise from +gapHalf to 2π − gapHalf (gap left open at top)
        juce::Path arc;
        arc.addArc (cx - iconR, cy - iconR, iconR * 2.0f, iconR * 2.0f,
                    gapHalf, juce::MathConstants<float>::twoPi - gapHalf, true);
        g.strokePath (arc, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Vertical line: from just below centre up through the gap to past the ring top
        juce::Path line;
        line.startNewSubPath (cx, cy + iconR * 0.18f);
        line.lineTo          (cx, cy - iconR - strokeW * 0.4f);
        g.strokePath (line, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        return;
    }
    juce::LookAndFeel_V4::drawToggleButton (g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
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
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
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

void PingLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                    int buttonX, int /*buttonY*/, int buttonW, int /*buttonH*/,
                                    juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.f, 0.f, (float) width, (float) height);

    // Use the combo's backgroundColourId so each combo can control its own transparency.
    // Combos explicitly set 0x30ffffff (19 % opaque white) so the glassy look reads against
    // both the lighter header background and the darker IR Synth body background.
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.f);

    // Soft border — low-alpha, no hard edge
    g.setColour (juce::Colour (0x44ffffff));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.f, 0.8f);

    // Dropdown arrow — centred in button area, subtle
    const float arrowCX = (float) buttonX + (float) buttonW * 0.5f;
    const float arrowCY = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowCX - 4.f, arrowCY - 2.f,
                       arrowCX + 4.f, arrowCY - 2.f,
                       arrowCX,       arrowCY + 3.f);
    g.setColour (juce::Colour (0x99ffffff));
    g.fillPath (arrow);
}

void PingLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // Leave room for the arrow on the right; slight left inset for readability
    label.setBounds (6, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (juce::FontOptions (12.f));
}
