#include "MicMixerComponent.h"

namespace
{
    // Visual palette — matches PingLookAndFeel conventions.
    const juce::Colour kRailColour   { 0xff1e1f24 };
    const juce::Colour kFrameColour  { 0x30ffffff };
    const juce::Colour kTextDim      { 0xffbcbcbc };
    const juce::Colour kTextBright   { 0xffe8e8e8 };
    const juce::Colour kMeterGreen   { 0xff2db32d };
    const juce::Colour kMeterYellow  { 0xffe8dc28 };
    const juce::Colour kMeterOrange  { 0xffe87828 };
    const juce::Colour kMeterRed     { 0xffdc2626 };
    const juce::Colour kMeterBack    { 0xff151515 };

    // Strip accent colours matching the four mic path colours shown on the floor plan.
    // OUTRIG pucks on the floor plan are violet (0xffb09aff / 0xffc8a6ff) and
    // AMBIENT pucks are green (0xff6fc26f / 0xff9ee89e), so the mixer accents
    // mirror that so the key, pucks, and mixer strip all read as one colour.
    const juce::Colour kAccentMain    { 0xff8cd6ef }; // icy blue (existing plugin accent)
    const juce::Colour kAccentDirect  { 0xffe87a2d }; // warm orange
    const juce::Colour kAccentOutrig  { 0xffc987e8 }; // soft violet (matches violet pucks)
    const juce::Colour kAccentAmbient { 0xff7bd67b }; // fresh green (matches green pucks)

    constexpr float kMeterMinDb = -60.0f;
    constexpr float kMeterMaxDb =   6.0f;

    // Convert linear peak to 0..1 bar fill with a dB-scaled curve.
    float peakToNorm (float peak) noexcept
    {
        if (peak < 1e-6f) return 0.f;
        const float db = juce::Decibels::gainToDecibels (peak);
        return juce::jlimit (0.f, 1.f, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
    }
}

// ── Construction ────────────────────────────────────────────────────────────
MicMixerComponent::MicMixerComponent (PingProcessor& proc)
    : processor (proc)
{
    stripIDs = { {
        { "MAIN",   "mainOn",    "mainGain",    "mainPan",    "mainMute",    "mainSolo",    "mainHPOn",    PingProcessor::MicPath::Main,    kAccentMain    },
        { "DIRECT", "directOn",  "directGain",  "directPan",  "directMute",  "directSolo",  "directHPOn",  PingProcessor::MicPath::Direct,  kAccentDirect  },
        { "OUTRIG", "outrigOn",  "outrigGain",  "outrigPan",  "outrigMute",  "outrigSolo",  "outrigHPOn",  PingProcessor::MicPath::Outrig,  kAccentOutrig  },
        { "AMB",    "ambientOn", "ambientGain", "ambientPan", "ambientMute", "ambientSolo", "ambientHPOn", PingProcessor::MicPath::Ambient, kAccentAmbient }
    } };

    for (int i = 0; i < 4; ++i)
        initStrip (i, stripIDs[(size_t) i]);

    startTimerHz (30);
}

MicMixerComponent::~MicMixerComponent()
{
    stopTimer();
}

void MicMixerComponent::initStrip (int idx, const StripIDs& ids)
{
    auto& s   = strips[(size_t) idx];
    auto& vts = processor.getAPVTS();

    // ── Name label ────────────────────────────────────────────────────────
    // Channel name is drawn as accent-coloured text — the old filled header box
    // has been removed so the strip reads as a cleaner glance-row of labels.
    s.nameLabel.setText (ids.label, juce::dontSendNotification);
    s.nameLabel.setJustificationType (juce::Justification::centred);
    s.nameLabel.setColour (juce::Label::textColourId, ids.accent);
    s.nameLabel.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    s.nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (s.nameLabel);

    // ── Power toggle ──────────────────────────────────────────────────────
    // Matches Plate / Bloom / Cloud / Shimmer power-button styling via
    // PingLookAndFeel::drawToggleButton, but uses per-strip accent via a
    // "pathAccent" property so each column glows in its own colour.
    s.powerBtn.setClickingTogglesState (true);
    s.powerBtn.setComponentID ("PathPowerToggle");
    s.powerBtn.getProperties().set ("pathAccent", ids.accent.toString());
    s.powerBtn.setTooltip ("Path on/off");
    addAndMakeVisible (s.powerBtn);
    s.onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (vts, ids.onID, s.powerBtn);

    // ── Pan knob ──────────────────────────────────────────────────────────
    // ComponentID "PanKnob" triggers bipolar dot-lighting in drawRotarySlider:
    // dots light from the 12 o'clock midpoint outward to the current position,
    // so centre reads as the zero reference.
    s.panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.panKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f,
                                   true);
    s.panKnob.setComponentID ("PanKnob");
    s.panKnob.setColour (juce::Slider::rotarySliderFillColourId, ids.accent);
    s.panKnob.setTooltip ("Pan (L ↔ R)");
    addAndMakeVisible (s.panKnob);
    s.panAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (vts, ids.panID, s.panKnob);

    // Pan readout label — "C" at centre, "L<n>" / "R<n>" on the MIDI-style
    // -64..+63 scale. Placed directly below the pan knob, above the fader
    // column so it never overlaps the fader's top dot.
    s.panReadout.setJustificationType (juce::Justification::centred);
    s.panReadout.setColour (juce::Label::textColourId, ids.accent);
    s.panReadout.setFont (juce::FontOptions (8.5f));
    s.panReadout.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (s.panReadout);

    auto refreshPan = [this, idx]
    {
        auto& strip = strips[(size_t) idx];
        const float raw = (float) strip.panKnob.getValue();          // -1..+1
        juce::String txt;
        // 0.5/64 ≈ 0.0078 — anything below this rounds to 0 on either side.
        if (std::abs (raw) < 0.008f)                    txt = "C";
        else if (raw < 0.f)  txt = "L" + juce::String (juce::jlimit (1, 64, (int) std::round (-raw * 64.f)));
        else                 txt = "R" + juce::String (juce::jlimit (1, 63, (int) std::round ( raw * 63.f)));
        strip.panReadout.setText (txt, juce::dontSendNotification);
    };
    s.panKnob.onValueChange = refreshPan;
    refreshPan();

    // ── Gain fader ────────────────────────────────────────────────────────
    // ComponentID "MixerFader" triggers the column-of-dots rendering in
    // PingLookAndFeel::drawLinearSlider. `thumbColourId` is used as the
    // lit-dot colour, matching the strip accent.
    s.gainFader.setSliderStyle (juce::Slider::LinearVertical);
    s.gainFader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.gainFader.setComponentID ("MixerFader");
    s.gainFader.setColour (juce::Slider::trackColourId,           juce::Colour (0x00000000));
    s.gainFader.setColour (juce::Slider::backgroundColourId,      juce::Colour (0x00000000));
    s.gainFader.setColour (juce::Slider::thumbColourId,           ids.accent);
    s.gainFader.setTooltip ("Gain (dB)");
    addAndMakeVisible (s.gainFader);
    s.gainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (vts, ids.gainID, s.gainFader);

    // Gain readout label — live dB value (driven by onValueChange).
    s.gainReadout.setJustificationType (juce::Justification::centred);
    s.gainReadout.setColour (juce::Label::textColourId, ids.accent);
    s.gainReadout.setFont (juce::FontOptions (8.5f));
    s.gainReadout.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (s.gainReadout);

    auto refreshReadout = [this, idx]
    {
        auto& strip = strips[(size_t) idx];
        const float db = (float) strip.gainFader.getValue();
        juce::String txt = (std::abs (db) < 0.05f) ? juce::String ("0.0") : juce::String (db, 1);
        strip.gainReadout.setText (txt + " dB", juce::dontSendNotification);
    };
    s.gainFader.onValueChange = refreshReadout;
    refreshReadout();

    // ── Mute / Solo / HP mini buttons ────────────────────────────────────
    auto initTinyToggle = [this] (juce::TextButton& btn, juce::Colour onColour)
    {
        btn.setClickingTogglesState (true);
        btn.setConnectedEdges (0);
        btn.setColour (juce::TextButton::buttonColourId,      juce::Colour (0xff1a1c20));
        btn.setColour (juce::TextButton::buttonOnColourId,    onColour);
        btn.setColour (juce::TextButton::textColourOffId,     kTextDim);
        btn.setColour (juce::TextButton::textColourOnId,      juce::Colours::black);
        addAndMakeVisible (btn);
    };

    initTinyToggle (s.muteBtn,  juce::Colour (0xffdc2626)); // red when muted
    initTinyToggle (s.soloBtn,  juce::Colour (0xffe8dc28)); // yellow when soloed
    initTinyToggle (s.hpBtn,    ids.accent);                 // accent when HP engaged
    // HPFButton componentID swaps the "HP" text for the standard HPF icon
    // (horizontal line with a down-left diagonal) in PingLookAndFeel.
    s.hpBtn.setComponentID ("HPFButton");

    s.muteBtn.setTooltip ("Mute");
    s.soloBtn.setTooltip ("Solo");
    s.hpBtn  .setTooltip ("110 Hz high-pass");

    s.muteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (vts, ids.muteID, s.muteBtn);
    s.soloAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (vts, ids.soloID, s.soloBtn);
    s.hpAttach   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (vts, ids.hpID,   s.hpBtn);
}

// ── Layout ──────────────────────────────────────────────────────────────────
void MicMixerComponent::resized()
{
    auto area = getLocalBounds();
    const int stripW = area.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto col = area.removeFromLeft (i == 3 ? area.getWidth() : stripW);
        strips[(size_t) i].stripBounds = col;
        layoutStrip (strips[(size_t) i], col.reduced (2));
    }
}

void MicMixerComponent::layoutStrip (Strip& s, juce::Rectangle<int> area)
{
    // Vertical breakdown (approximate Y budget within 208-4 = 204 px):
    //   12  name
    //   14  power toggle
    //    2  gap
    //   32  pan knob   (larger — ~half the size of a typical UI knob)
    //   10  pan readout ("C" / "L23" / "R17")
    //    2  gap
    //  ~106 fader + meters column   (bottom-aligned)
    //    2  gap
    //   10  gain readout
    //    2  gap
    //   14  MSH buttons row
    auto col = area;

    s.nameLabel.setBounds (col.removeFromTop (12));

    // Power toggle — 14 px square, centred on the column centre.
    auto powerRow = col.removeFromTop (14);
    const int pSize = 14;
    s.powerBtn.setBounds (powerRow.withSizeKeepingCentre (pSize, pSize));

    col.removeFromTop (2);

    // Pan knob — 32 px, left-offset within the strip so it sits over the
    // fader column rather than centred. Readout sits directly beneath it.
    const int panSize     = 32;
    const int panLeftPad  = 6;       // inset from strip left edge
    auto panRow = col.removeFromTop (panSize);
    s.panKnob.setBounds (panRow.getX() + panLeftPad, panRow.getY(), panSize, panSize);
    // Readout sits under the knob, same width — small text, won't crash fader top.
    auto panReadoutRow = col.removeFromTop (10);
    s.panReadout.setBounds (panReadoutRow.getX() + panLeftPad - 4,
                            panReadoutRow.getY(),
                            panSize + 8,            // slight extension so "L64" fits
                            panReadoutRow.getHeight());

    col.removeFromTop (2);

    // Bottom-up: reserve button row and gain readout first, so the fader
    // block below is the only thing that stretches with strip height.
    auto btnRow     = col.removeFromBottom (14);
    col.removeFromBottom (2);
    auto readoutRow = col.removeFromBottom (10);
    col.removeFromBottom (2);

    // Remaining = fader + meter area. Fader takes ~60% of the width, meters
    // ~30% (halved vs. previous ~45%) so bars read as fine ticks rather
    // than wide blocks. Both share the same top/bottom Y so the fader's
    // bottom dot lines up exactly with the meter's bottom edge.
    auto faderBlock = col;
    const int totalW     = faderBlock.getWidth();
    const int meterAreaW = juce::jmax (10, (int) std::round (totalW * 0.28f));
    const int gapPx      = 6;
    const int faderAreaW = juce::jmax (16, totalW - meterAreaW - gapPx);
    auto faderArea = faderBlock.removeFromLeft (faderAreaW);
    faderBlock.removeFromLeft (gapPx);
    s.meterBounds = faderBlock.withWidth (meterAreaW);

    const int actualFaderW = juce::jmax (10, faderAreaW - 2);
    s.faderBounds = faderArea.withSizeKeepingCentre (actualFaderW, faderArea.getHeight());
    s.gainFader.setBounds (s.faderBounds);

    s.gainReadout.setBounds (readoutRow);

    // Mute / Solo / HP — 3 buttons share btnRow
    const int bgap  = 2;
    const int btnW  = (btnRow.getWidth() - 2 * bgap) / 3;
    auto b1 = btnRow.removeFromLeft (btnW); btnRow.removeFromLeft (bgap);
    auto b2 = btnRow.removeFromLeft (btnW); btnRow.removeFromLeft (bgap);
    auto b3 = btnRow;
    s.muteBtn.setBounds (b1);
    s.soloBtn.setBounds (b2);
    s.hpBtn  .setBounds (b3);
}

// ── Timer ───────────────────────────────────────────────────────────────────
void MicMixerComponent::timerCallback()
{
    bool anyChange = false;
    for (int i = 0; i < 4; ++i)
    {
        auto& s  = strips[(size_t) i];
        const auto path = stripIDs[(size_t) i].path;
        s.peakL = processor.getPathPeak (path, 0);
        s.peakR = processor.getPathPeak (path, 1);

        // Peak-hold style decay: immediate rise, slow fall.
        const float decay = 0.75f;
        s.displayL = (s.peakL > s.displayL) ? s.peakL : (s.displayL * decay + s.peakL * (1.f - decay));
        s.displayR = (s.peakR > s.displayR) ? s.peakR : (s.displayR * decay + s.peakR * (1.f - decay));

        // Gate UI on per-path IR load state — the user can't enable a path
        // whose IR hasn't been synthesised (Calculate IR must run with the
        // path's "Enabled" checkbox ticked on the IR Synth page first).
        const bool loaded = processor.isPathIRLoaded (path);
        if (s.powerBtn.isEnabled() != loaded)
        {
            s.powerBtn.setEnabled (loaded);
            // Dim the rest of the strip while no IR is available so the whole
            // column reads as "not active" at a glance.
            const float alpha = loaded ? 1.0f : 0.35f;
            s.nameLabel  .setAlpha (alpha);
            s.panKnob    .setAlpha (alpha);
            s.panReadout .setAlpha (alpha);
            s.gainFader  .setAlpha (alpha);
            s.gainReadout.setAlpha (alpha);
            s.muteBtn    .setAlpha (alpha);
            s.soloBtn    .setAlpha (alpha);
            s.hpBtn      .setAlpha (alpha);
            s.muteBtn    .setEnabled (loaded);
            s.soloBtn    .setEnabled (loaded);
            s.hpBtn      .setEnabled (loaded);
            s.panKnob    .setEnabled (loaded);
            s.gainFader  .setEnabled (loaded);
            // Power button — dim it too (the custom LookAndFeel draw override
            // doesn't auto-dim on isEnabled() alone; setAlpha handles both).
            s.powerBtn   .setAlpha (alpha);
            // Tooltip hints at the reason so a new user knows where to enable it.
            s.powerBtn.setTooltip (loaded ? juce::String ("Path on/off")
                                          : juce::String ("IR not calculated — enable this path on the IR Synth page and press Calculate IR"));
        }

        anyChange = true;
    }
    if (anyChange) repaint();
}

// ── Painting ────────────────────────────────────────────────────────────────
void MicMixerComponent::paint (juce::Graphics& g)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& s = strips[(size_t) i];

        // Strip backing panel — unchanged, sets the dark glass substrate.
        // The old filled accent header stripe has been removed: the channel
        // name itself is now drawn in the strip accent colour (via nameLabel)
        // so the column reads as colour-at-a-glance without the heavy block.
        auto b = s.stripBounds.toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0x40000000));
        g.fillRoundedRectangle (b, 3.f);

        // Hairline frame — kept for column separation against the brushed
        // steel background; too subtle to feel like a "box".
        g.setColour (kFrameColour);
        g.drawRoundedRectangle (b, 3.f, 1.0f);

        paintMeters (g, s);
    }
}

void MicMixerComponent::paintMeters (juce::Graphics& g, const Strip& s)
{
    auto m = s.meterBounds.toFloat();
    if (m.getWidth() < 4.f || m.getHeight() < 4.f) return;

    // Two thin vertical meters (L, R), left-justified within m so a small gap
    // also appears between the R meter and the strip's right edge — mirroring
    // the L↔R gap so the pair reads as a matched pair of ticks, not a block.
    // `shrinkPerBar` sheds pixels from the right side of each bar.
    const float gap          = 3.f;
    const float shrinkPerBar = 2.f;
    const float barW = juce::jmax (2.f, (m.getWidth() - gap) * 0.5f - shrinkPerBar);
    juce::Rectangle<float> bL (m.getX(),                  m.getY(), barW, m.getHeight());
    juce::Rectangle<float> bR (m.getX() + barW + gap,     m.getY(), barW, m.getHeight());

    auto paintBar = [&] (juce::Rectangle<float> rail, float peakNorm)
    {
        // Rail (always visible)
        g.setColour (kMeterBack);
        g.fillRect (rail);

        // Gradient fill from bottom up — green/yellow/orange/red
        if (peakNorm > 0.001f)
        {
            const float fillH = rail.getHeight() * peakNorm;
            juce::Rectangle<float> fill (rail.getX(),
                                         rail.getBottom() - fillH,
                                         rail.getWidth(),
                                         fillH);

            juce::ColourGradient grad (kMeterRed,   rail.getX(), rail.getY(),
                                       kMeterGreen, rail.getX(), rail.getBottom(),
                                       false);
            const float n12 = juce::jlimit (0.f, 1.f, (-12.f - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
            const float n6  = juce::jlimit (0.f, 1.f, ( -6.f - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
            const float n3  = juce::jlimit (0.f, 1.f, ( -3.f - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
            // Stops are measured top-to-bottom for the gradient; invert.
            grad.addColour (1.f - n12, kMeterGreen);
            grad.addColour (1.f - n6,  kMeterYellow);
            grad.addColour (1.f - n3,  kMeterOrange);
            g.setGradientFill (grad);
            g.fillRect (fill);
        }

        // Hairline frame — now ~¼ of the strip's frame alpha so the meter
        // surround reads as a ghost outline rather than a visible box.
        g.setColour (kFrameColour.withMultipliedAlpha (0.22f));
        g.drawRect (rail, 1.0f);
    };

    paintBar (bL, peakToNorm (s.displayL));
    paintBar (bR, peakToNorm (s.displayR));
}

// Unused helper kept for symmetry with existing components; never called.
void MicMixerComponent::paintStripChrome (juce::Graphics&, const Strip&, const StripIDs&) {}
