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
        || id == "BloomSwitch" || id == "CloudSwitch" || id == "ShimmerSwitch"
        || id == "PathPowerToggle")
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        bool on      = button.getToggleState();
        bool hovered = shouldDrawButtonAsHighlighted;

        // Per-button accent override: mic-mixer path toggles stash their
        // strip colour in button properties so each column can glow in its
        // own accent (MAIN cyan, DIRECT orange, OUTRIG violet, AMBIENT green).
        // Effect switches (Plate/Bloom/…) don't set this, so they fall back
        // to the plugin-wide ice-blue accent.
        juce::Colour activeAccent = accentIce;
        juce::Colour activeLed    = accentLed;
        if (button.getProperties().contains ("pathAccent"))
        {
            activeAccent = juce::Colour::fromString (button.getProperties().getWithDefault ("pathAccent", accentIce.toString()).toString());
            activeLed    = activeAccent.brighter (0.25f);
        }

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
            juce::ColourGradient ledGlow (activeLed.withAlpha (0.85f), cx, cy,
                                          activeAccent.withAlpha (0.0f),
                                          cx + bgR, cy, true);
            g.setGradientFill (ledGlow);
            g.fillEllipse (cx - bgR, cy - bgR, bgR * 2.0f, bgR * 2.0f);
        }

        // Border — accent when on, dim grey when off
        juce::Colour borderCol = on ? (hovered ? activeAccent.brighter (0.1f) : activeAccent)
                                    : (hovered ? juce::Colour (0xff606060) : juce::Colour (0xff404040));
        g.setColour (borderCol);
        g.drawEllipse (cx - bgR, cy - bgR, bgR * 2.0f, bgR * 2.0f, on ? 1.5f : 1.0f);

        // ── Power symbol ──────────────────────────────────────────────────
        const float iconR   = bgR * 0.58f;   // ring radius
        const float gapHalf = 0.60f;          // ~34° gap half-angle at 12 o'clock
        const float strokeW = dim * 0.13f;    // stroke width scaled to button size
        juce::Colour iconCol = on ? activeAccent
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

    if (id == "HPFButton")
    {
        // Draw the standard HPF symbol: a horizontal line on the right with
        // a diagonal slope on the left side (cuts down toward low frequencies).
        const bool on = button.getToggleState();
        const juce::Colour iconCol = on ? button.findColour (juce::TextButton::textColourOnId)
                                        : button.findColour (juce::TextButton::textColourOffId);
        auto b = button.getLocalBounds().toFloat().reduced (4.0f);
        if (b.getWidth() < 6.f || b.getHeight() < 4.f) return;

        // The symbol occupies the central horizontal band of the button.
        const float cy       = b.getCentreY() + 1.0f; // sits slightly below centre for balance
        const float x0       = b.getX();
        const float x1       = b.getRight();
        const float kneeFrac = 0.45f;                 // where the diagonal meets the flat top
        const float kneeX    = x0 + (x1 - x0) * kneeFrac;
        const float topY     = b.getY() + b.getHeight() * 0.12f;
        const float strokeW  = juce::jmax (1.2f, b.getHeight() * 0.09f);

        juce::Path p;
        p.startNewSubPath (x0,    cy);       // lower-left starting point (the "down" end)
        p.lineTo          (kneeX, topY);     // diagonal up to the knee
        p.lineTo          (x1,    topY);     // flat top out to the right (the pass band)
        g.setColour (iconCol);
        g.strokePath (p, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        return;
    }

    juce::LookAndFeel_V4::drawButtonText (g, button, isOver, isDown);
}

void PingLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minSliderPos, float maxSliderPos,
                                        const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const juce::String id = slider.getComponentID();

    // Custom column-of-dots fader used by the mic mixer strips. Matches the
    // styling of the rotary knobs (small dots on a dark track) and lights
    // from the bottom up to the current level in the strip accent colour.
    if (id == "MixerFader" && style == juce::Slider::LinearVertical)
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        if (bounds.getHeight() < 8.0f) return;

        // Level from the slider's own value, so the visual scale matches the dB
        // mapping exactly (including skew) regardless of the raw pixel sliderPos.
        const float level = juce::jlimit (0.0f, 1.0f,
            (float) slider.valueToProportionOfLength (slider.getValue()));

        const juce::Colour activeCol = slider.findColour (juce::Slider::thumbColourId);
        const juce::Colour dimCol    = trackColour.withAlpha (0.55f);

        // Finer granularity than the previous column: smaller dots and more
        // of them so the level reads as a smooth lit bar rather than big pills.
        const float dotR = 1.5f;
        // Aim for ~3.0 px vertical spacing between dot centres — that's 2x the
        // dot diameter, giving a crisp rhythm without the dots merging.
        const int numDots = juce::jlimit (20, 56, (int) std::round (bounds.getHeight() / 3.0f));

        const float cx   = bounds.getCentreX();
        // Bottom dot sits flush against the fader/meter bottom edge (the two
        // share the same Y bounds in MicMixerComponent::layoutStrip), so the
        // bottom of the lit column aligns pixel-exactly with the meter base.
        const float botY = bounds.getBottom() - dotR;
        const float topY = bounds.getY()      + dotR + 1.0f;
        const float run  = botY - topY;
        if (run <= 0.f) return;

        int activeCount = (int) std::round (level * (float) numDots);
        if (level > 0.001f) activeCount = juce::jmax (activeCount, 1);

        for (int i = 0; i < numDots; ++i)
        {
            // i=0 at top (max), i=numDots-1 at bottom (min).
            const float frac = (float) i / (float) (numDots - 1);
            const float py   = topY + frac * run;
            const int   fromBottomIndex = numDots - 1 - i;
            const bool  active = fromBottomIndex < activeCount;
            g.setColour (active ? activeCol : dimCol);
            g.fillEllipse (cx - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
        }
        return;
    }

    juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height,
                                            sliderPos, minSliderPos, maxSliderPos, style, slider);
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

    // Dotted ring around perimeter — accent for active range, grey for inactive
    const int numDots = (width >= 72) ? 36 : 28;
    const float dotRadius = (width >= 72) ? 1.8f : 1.4f;
    const float dotDist = knobRadius - 4.0f;

    // Bipolar knobs (e.g. the mic mixer pan controls) light up from the
    // 12 o'clock midpoint outward toward the current position, so centre
    // reads as zero and both sides can be distinguished visually.
    const bool bipolar = slider.getComponentID() == "PanKnob";
    const float midAngle = 0.5f * (rotaryStartAngle + rotaryEndAngle);
    // Tolerance for "exactly at centre" — about half a dot spacing.
    const float centreTol = 0.5f * ((rotaryEndAngle - rotaryStartAngle) / (float) numDots);
    const bool atCentre = bipolar && std::abs (angle - midAngle) <= centreTol;

    for (int i = 0; i < numDots; ++i)
    {
        float a = rotaryStartAngle + (float) i / (float) numDots * (rotaryEndAngle - rotaryStartAngle);

        bool inActiveRange;
        if (bipolar)
        {
            if (atCentre)
            {
                // Light only the dot closest to 12 o'clock (midpoint).
                inActiveRange = std::abs (a - midAngle) <= centreTol;
            }
            else
            {
                // Light dots between midpoint and current angle, in whichever direction.
                const float lo = std::min (midAngle, angle);
                const float hi = std::max (midAngle, angle);
                inActiveRange = (a >= lo && a <= hi);
            }
        }
        else
        {
            inActiveRange = (rotaryStartAngle <= rotaryEndAngle) ? (a >= rotaryStartAngle && a <= angle)
                                                                  : (a <= rotaryStartAngle && a >= angle);
        }

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
