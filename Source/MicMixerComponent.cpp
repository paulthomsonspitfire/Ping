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
    const juce::Colour kAccentMain    { 0xff8cd6ef }; // icy blue (existing plugin accent)
    const juce::Colour kAccentDirect  { 0xffe87a2d }; // warm orange
    const juce::Colour kAccentOutrig  { 0xff7bd67b }; // fresh green
    const juce::Colour kAccentAmbient { 0xffc987e8 }; // soft violet

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
    s.nameLabel.setText (ids.label, juce::dontSendNotification);
    s.nameLabel.setJustificationType (juce::Justification::centred);
    s.nameLabel.setColour (juce::Label::textColourId, kTextBright);
    s.nameLabel.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    s.nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (s.nameLabel);

    // ── Power toggle ──────────────────────────────────────────────────────
    s.powerBtn.setClickingTogglesState (true);
    s.powerBtn.setComponentID ("PathPowerToggle");
    s.powerBtn.setTooltip ("Path on/off");
    addAndMakeVisible (s.powerBtn);
    s.onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (vts, ids.onID, s.powerBtn);

    // ── Pan knob ──────────────────────────────────────────────────────────
    s.panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.panKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f,
                                   true);
    s.panKnob.setColour (juce::Slider::rotarySliderFillColourId, ids.accent);
    s.panKnob.setTooltip ("Pan (L ↔ R)");
    addAndMakeVisible (s.panKnob);
    s.panAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (vts, ids.panID, s.panKnob);

    // ── Gain fader ────────────────────────────────────────────────────────
    s.gainFader.setSliderStyle (juce::Slider::LinearVertical);
    s.gainFader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.gainFader.setColour (juce::Slider::trackColourId,           juce::Colour (0xff2a2d33));
    s.gainFader.setColour (juce::Slider::backgroundColourId,      juce::Colour (0xff16181c));
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
    // Vertical breakdown (approximate Y budget within 153-4 = 149 px):
    //   12  name
    //   14  power toggle
    //    2  gap
    //   22  pan knob
    //    2  gap
    //   72  fader + meters column
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

    // Pan knob — small rotary, 22 px tall centred.
    auto panRow = col.removeFromTop (22);
    const int panSize = 22;
    s.panKnob.setBounds (panRow.withSizeKeepingCentre (panSize, panSize));

    col.removeFromTop (2);

    // Fader block: fader on left ~55% of column width, meters on right ~45%.
    auto btnRow     = col.removeFromBottom (14);
    col.removeFromBottom (2);
    auto readoutRow = col.removeFromBottom (10);
    col.removeFromBottom (2);

    const int faderBlockH = col.getHeight();
    auto faderBlock = col; // all remaining vertical space

    const int faderW = juce::jmax (16, (int) (faderBlock.getWidth() * 0.45f));
    const int meterPad = 4;
    auto faderArea = faderBlock.removeFromLeft (faderW + 8);   // 8 px breathing room around fader
    // Meters occupy the remaining right side
    faderBlock.removeFromLeft (meterPad);
    s.meterBounds = faderBlock;

    // Centre the actual slider widget within faderArea (narrow track)
    const int actualFaderW = juce::jmax (10, faderW - 2);
    s.faderBounds = faderArea.withSizeKeepingCentre (actualFaderW, faderBlockH);
    s.gainFader.setBounds (s.faderBounds);

    // Readout
    s.gainReadout.setBounds (readoutRow);

    // Mute / Solo / HP — 3 buttons share btnRow
    const int gap   = 2;
    const int btnW  = (btnRow.getWidth() - 2 * gap) / 3;
    auto b1 = btnRow.removeFromLeft (btnW); btnRow.removeFromLeft (gap);
    auto b2 = btnRow.removeFromLeft (btnW); btnRow.removeFromLeft (gap);
    auto b3 = btnRow; // remainder
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

        anyChange = true;
    }
    if (anyChange) repaint();
}

// ── Painting ────────────────────────────────────────────────────────────────
void MicMixerComponent::paint (juce::Graphics& g)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& s   = strips[(size_t) i];
        auto& ids = stripIDs[(size_t) i];

        // Strip backing panel
        auto b = s.stripBounds.toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0x40000000));
        g.fillRoundedRectangle (b, 3.f);

        // Top header stripe in the path accent
        const float headerH = 14.f;
        juce::Rectangle<float> header (b.getX(), b.getY(), b.getWidth(), headerH);
        g.setColour (ids.accent.withAlpha (0.22f));
        g.fillRoundedRectangle (header, 3.f);

        // Accent underline below header
        g.setColour (ids.accent.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (b.getX() + 4.f, b.getY() + headerH - 1.f,
                                            b.getWidth() - 8.f, 1.f));

        // Border
        g.setColour (kFrameColour);
        g.drawRoundedRectangle (b, 3.f, 1.0f);

        paintMeters (g, s);
    }
}

void MicMixerComponent::paintMeters (juce::Graphics& g, const Strip& s)
{
    auto m = s.meterBounds.toFloat();
    if (m.getWidth() < 4.f || m.getHeight() < 4.f) return;

    // Two thin vertical meters (L, R) filling m. Small internal gap between them.
    const float gap = 2.f;
    const float barW = juce::jmax (3.f, (m.getWidth() - gap) * 0.5f);
    juce::Rectangle<float> bL (m.getX(),                    m.getY(), barW, m.getHeight());
    juce::Rectangle<float> bR (m.getRight() - barW,         m.getY(), barW, m.getHeight());

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

        // Hairline frame
        g.setColour (kFrameColour);
        g.drawRect (rail, 1.0f);
    };

    paintBar (bL, peakToNorm (s.displayL));
    paintBar (bR, peakToNorm (s.displayR));
}

// Unused helper kept for symmetry with existing components; never called.
void MicMixerComponent::paintStripChrome (juce::Graphics&, const Strip&, const StripIDs&) {}
