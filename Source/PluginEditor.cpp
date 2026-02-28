#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PingBinaryData.h"

namespace
{
    constexpr int editorW = 920;
    constexpr int editorH = 440;
    constexpr int minW = 720;
    constexpr int minH = 440;
    constexpr int maxW = 1600;
    constexpr int maxH = 900;
    const juce::Colour bgDark      { 0xff141414 };
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour accent     { 0xffe8a84a };
    const juce::Colour accentDim   { 0xffc48938 };
    const juce::Colour textCol    { 0xffe8e8e8 };
    const juce::Colour textDim    { 0xff909090 };
    const juce::Colour waveFill   { 0x28e8a84a };
    const juce::Colour waveLine   { 0xffe8e8e8 };

    juce::String toTitleCase (const juce::String& s)
    {
        if (s.isEmpty()) return {};
        juce::String result;
        bool atWordStart = true;
        for (juce::juce_wchar c : s)
        {
            if (c == ' ' || c == '\t' || c == '-')
            {
                atWordStart = true;
                result += c;
            }
            else
            {
                result += atWordStart ? (juce::String) (juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) c)
                                     : (juce::String) (juce::juce_wchar) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c);
                atWordStart = false;
            }
        }
        return result;
    }
}

PingEditor::PingEditor (PingProcessor& p)
    : AudioProcessorEditor (&p),
      pingProcessor (p),
      apvts (p.getAPVTS()),
      eqGraph (p.getAPVTS()),
      waveformComponent (p)
{
    setSize (editorW, editorH);
    setResizable (true, true);
    setResizeLimits (minW, minH, maxW, maxH);
    setOpaque (true);

    // IR list
    addAndMakeVisible (irCombo);
    irCombo.addListener (this);
    irCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
    irCombo.setColour (juce::ComboBox::textColourId, textDim);
    irCombo.setColour (juce::ComboBox::arrowColourId, accent);

    addAndMakeVisible (presetCombo);
    presetCombo.addListener (this);
    presetCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
    presetCombo.setColour (juce::ComboBox::textColourId, textCol);
    presetCombo.setColour (juce::ComboBox::arrowColourId, accent);
    presetCombo.addItem ("Default", 1);
    presetCombo.setSelectedId (1, juce::dontSendNotification);
    presetCombo.setEditableText (true);

    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this]
    {
        juce::String name = presetCombo.getText().trim();
        if (name.isNotEmpty())
        {
            savePreset (name);
            refreshPresetList();
        }
    };
    savePresetButton.setColour (juce::TextButton::buttonColourId, panelBg);
    savePresetButton.setColour (juce::TextButton::textColourOffId, textDim);

    addAndMakeVisible (reverseButton);
    reverseButton.setComponentID ("Reverse");
    reverseButton.setClickingTogglesState (true);
    reverseButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1a1a1a));
    reverseButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    reverseButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    reverseButton.onClick = [this]
    {
        pingProcessor.setReverse (reverseButton.getToggleState());
        loadSelectedIR();
    };

    addAndMakeVisible (irSynthButton);
    irSynthButton.setColour (juce::TextButton::buttonColourId, panelBg);
    irSynthButton.setColour (juce::TextButton::textColourOffId, textDim);
    irSynthButton.onClick = [this]
    {
        setMainPanelControlsVisible (false);
        irSynthComponent.setVisible (true);
        irSynthComponent.toFront (true);
    };

    addChildComponent (irSynthComponent);
    irSynthComponent.setVisible (false);
    irSynthComponent.setOnComplete ([this] (const IRSynthResult& result)
    {
        if (! result.success || result.iLL.empty() || result.iRL.empty() || result.iLR.empty() || result.iRR.empty())
            return;
        const size_t N = result.iLL.size();
        juce::AudioBuffer<float> buf (4, (int) N);
        for (size_t i = 0; i < N; ++i)
        {
            buf.setSample (0, (int) i, (float) result.iLL[i]);
            buf.setSample (1, (int) i, (float) result.iRL[i]);
            buf.setSample (2, (int) i, (float) result.iLR[i]);
            buf.setSample (3, (int) i, (float) result.iRR[i]);
        }
        pingProcessor.loadIRFromBuffer (std::move (buf), (double) result.sampleRate);
        updateWaveform();
        irSynthComponent.setVisible (false);
        setMainPanelControlsVisible (true);
    });
    irSynthComponent.setOnCancel ([this]
    {
        irSynthComponent.setVisible (false);
        setMainPanelControlsVisible (true);
    });

    // Sliders - rotary style
    auto makeSlider = [this] (juce::Slider& s, const juce::String& name)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setColour (juce::Slider::rotarySliderFillColourId, accent);
        s.setColour (juce::Slider::thumbColourId, accent);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, panelBorder);
        s.setColour (juce::Slider::trackColourId, panelBorder);
    };

    auto makeGreySlider = [this] (juce::Slider& s)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        const juce::Colour greyAccent { 0xff909090 };
        s.setColour (juce::Slider::rotarySliderFillColourId, greyAccent);
        s.setColour (juce::Slider::thumbColourId, greyAccent);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, panelBorder);
        s.setColour (juce::Slider::trackColourId, panelBorder);
    };

    makeSlider (dryWetSlider, "Dry/Wet");
    makeSlider (predelaySlider, "Predelay");
    makeSlider (decaySlider, "Decay");
    makeSlider (modDepthSlider, "Mod Depth");
    makeSlider (stretchSlider, "Stretch");
    makeSlider (widthSlider, "Width");
    makeSlider (modRateSlider, "Mod Rate");
    makeGreySlider (tailModSlider);
    makeGreySlider (delayDepthSlider);
    makeGreySlider (tailRateSlider);
    makeSlider (irInputGainSlider, "IR Input Gain");
    irInputGainSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4caf50));
    irInputGainSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff4caf50));
    makeSlider (irInputDriveSlider, "IR Input Drive");
    irInputDriveSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe53935));
    irInputDriveSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe53935));

    auto makeHorizontalSlider = [this] (juce::Slider& s)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setColour (juce::Slider::thumbColourId, accent);
        s.setColour (juce::Slider::trackColourId, panelBorder);
    };
    makeHorizontalSlider (erLevelSlider);
    makeHorizontalSlider (tailLevelSlider);

    dryWetSlider.setRange (0.0, 1.0, 0.01);
    predelaySlider.setRange (0.0, 500.0, 1.0);
    decaySlider.setRange (0.0, 1.0, 0.01);
    modDepthSlider.setRange (0.0, 1.0, 0.01);
    stretchSlider.setRange (0.5, 2.0, 0.01);
    widthSlider.setRange (0.0, 2.0, 0.01);
    modRateSlider.setRange (0.01, 2.0, 0.01);
    tailModSlider.setRange (0.0, 1.0, 0.01);
    delayDepthSlider.setRange (0.5f, 8.0f, 0.01f);
    tailRateSlider.setRange (0.05f, 3.0f, 0.01f);
    irInputGainSlider.setRange (-24.0, 12.0, 0.5);
    irInputDriveSlider.setRange (0.0, 1.0, 0.01);
    erLevelSlider.setRange (-48.0, 6.0, 0.1);
    tailLevelSlider.setRange (-48.0, 6.0, 0.1);

    dryWetAttach    = std::make_unique<SliderAttachment> (apvts, "drywet",   dryWetSlider);
    predelayAttach  = std::make_unique<SliderAttachment> (apvts, "predelay", predelaySlider);
    decayAttach     = std::make_unique<SliderAttachment> (apvts, "decay",    decaySlider);
    stretchAttach   = std::make_unique<SliderAttachment> (apvts, "stretch",  stretchSlider);
    widthAttach     = std::make_unique<SliderAttachment> (apvts, "width",   widthSlider);
    modDepthAttach  = std::make_unique<SliderAttachment> (apvts, "moddepth", modDepthSlider);
    modRateAttach   = std::make_unique<SliderAttachment> (apvts, "modrate",  modRateSlider);
    tailModAttach   = std::make_unique<SliderAttachment> (apvts, "tailmod",  tailModSlider);
    delayDepthAttach = std::make_unique<SliderAttachment> (apvts, "delaydepth", delayDepthSlider);
    tailRateAttach  = std::make_unique<SliderAttachment> (apvts, "tailrate", tailRateSlider);
    irInputGainAttach  = std::make_unique<SliderAttachment> (apvts, "inputGain", irInputGainSlider);
    irInputDriveAttach = std::make_unique<SliderAttachment> (apvts, "irInputDrive", irInputDriveSlider);
    erLevelAttach   = std::make_unique<SliderAttachment> (apvts, "erLevel", erLevelSlider);
    tailLevelAttach = std::make_unique<SliderAttachment> (apvts, "tailLevel", tailLevelSlider);

    setLookAndFeel (&pingLook);

    // Labels
    for (auto* label : { &dryWetLabel, &predelayLabel, &decayLabel, &modDepthLabel,
                        &stretchLabel, &widthLabel, &modRateLabel,
                        &tailModLabel, &delayDepthLabel, &tailRateLabel,
                        &irInputGainLabel, &irInputDriveLabel,
                        &erLevelLabel, &tailLevelLabel })
    {
        addAndMakeVisible (label);
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, textCol);
        label->setFont (12.0f);
    }
    dryWetLabel.setText ("", juce::dontSendNotification);
    dryWetLabel.setVisible (false);
    predelayLabel.setText ("PREDELAY", juce::dontSendNotification);
    decayLabel.setText ("DAMPING", juce::dontSendNotification);
    modDepthLabel.setText ("LFO DEPTH", juce::dontSendNotification);
    stretchLabel.setText ("STRETCH", juce::dontSendNotification);
    widthLabel.setText ("WIDTH", juce::dontSendNotification);
    modRateLabel.setText ("LFO RATE", juce::dontSendNotification);
    tailModLabel.setText ("TAIL MOD", juce::dontSendNotification);
    delayDepthLabel.setText ("DELAY DEPTH", juce::dontSendNotification);
    tailRateLabel.setText ("RATE", juce::dontSendNotification);
    irInputGainLabel.setText ("IR INPUT GAIN", juce::dontSendNotification);
    irInputGainLabel.setFont (juce::FontOptions (9.0f));
    irInputDriveLabel.setText ("IR INPUT DRIVE", juce::dontSendNotification);
    irInputDriveLabel.setFont (juce::FontOptions (9.0f));
    erLevelLabel.setText ("ER", juce::dontSendNotification);
    tailLevelLabel.setText ("Tail", juce::dontSendNotification);

    for (auto* r : { &dryWetReadout, &predelayReadout, &decayReadout, &modDepthReadout,
                     &stretchReadout, &widthReadout, &modRateReadout,
                     &tailModReadout, &delayDepthReadout, &tailRateReadout,
                     &irInputGainReadout, &irInputDriveReadout,
                     &erLevelReadout, &tailLevelReadout })
    {
        addAndMakeVisible (r);
        r->setJustificationType (juce::Justification::centred);
        r->setColour (juce::Label::textColourId, textDim);
        r->setFont (11.0f);
    }

    waveformComponent.setOnTrimChanged ([this] { loadSelectedIR(); });
    addAndMakeVisible (waveformComponent);
    addAndMakeVisible (eqGraph);
    addAndMakeVisible (outputLevelMeter);

    addAndMakeVisible (licenceLabel);
    licenceLabel.setJustificationType (juce::Justification::centredLeft);
    licenceLabel.setColour (juce::Label::textColourId, textDim);
    licenceLabel.setFont (juce::FontOptions (11.0f));

    addChildComponent (licenceScreen);
    licenceScreen.onActivationSuccess = [this] (LicenceResult r, juce::String serial, juce::String displayName)
    {
        pingProcessor.setLicence (r, serial, displayName);
        licenceScreen.setVisible (false);
    };
    if (pingProcessor.isLicensed())
        licenceScreen.setVisible (false);
    else
        licenceScreen.setVisible (true);

    apvts.addParameterListener ("stretch", this);
    apvts.addParameterListener ("decay", this);

    refreshIRList();
    refreshPresetList();
    reverseButton.setToggleState (pingProcessor.getReverse(), juce::dontSendNotification);
    int savedIdx = pingProcessor.getSelectedIRIndex();
    if (savedIdx >= 0 && savedIdx < irCombo.getNumItems())
        irCombo.setSelectedId (savedIdx + 1, juce::dontSendNotification);
    loadSelectedIR();
    startTimerHz (8);
}

PingEditor::~PingEditor()
{
    setLookAndFeel (nullptr);
    apvts.removeParameterListener ("stretch", this);
    apvts.removeParameterListener ("decay", this);
    irCombo.removeListener (this);
}

void PingEditor::parameterChanged (const juce::String& parameterID, float)
{
    if ((parameterID == "stretch" || parameterID == "decay") && pingProcessor.getLastLoadedIRFile().existsAsFile())
        pingProcessor.loadIRFromFile (pingProcessor.getLastLoadedIRFile());
}

void PingEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgDark);
    g.setGradientFill (juce::ColourGradient::vertical (bgDark.brighter (0.03f), 0, bgDark.darker (0.04f), (float) getHeight()));
    g.fillRect (getLocalBounds());

    auto wfBounds = waveformComponent.getBounds();
    const float corner = 6.0f;
    g.setColour (panelBg);
    g.fillRoundedRectangle (wfBounds.toFloat(), corner);
    g.setColour (panelBorder);
    g.drawRoundedRectangle (wfBounds.toFloat().reduced (0.5f), corner, 0.8f);

    if (spitfireBounds.getWidth() > 0 && spitfireBounds.getHeight() > 0)
    {
        auto spitfireImg = juce::ImageCache::getFromMemory (BinaryData::spitfire_logo_png, BinaryData::spitfire_logo_pngSize);
        if (spitfireImg.isValid())
            g.drawImageWithin (spitfireImg, spitfireBounds.getX(), spitfireBounds.getY(), spitfireBounds.getWidth(), spitfireBounds.getHeight(),
                              juce::RectanglePlacement (juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid));
    }
    if (pingBounds.getWidth() > 0 && pingBounds.getHeight() > 0)
    {
        auto pingImg = juce::ImageCache::getFromMemory (BinaryData::ping_logo_png, BinaryData::ping_logo_pngSize);
        if (pingImg.isValid())
            g.drawImageWithin (pingImg, pingBounds.getX(), pingBounds.getY(), pingBounds.getWidth(), pingBounds.getHeight(),
                              juce::RectanglePlacement (juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid));
    }

    auto dryWetImg = juce::ImageCache::getFromMemory (BinaryData::drywet_legend_png, BinaryData::drywet_legend_pngSize);
    if (dryWetImg.isValid())
    {
        auto r = dryWetLabel.getBounds();
        if (r.getWidth() > 0 && r.getHeight() > 0)
            g.drawImageWithin (dryWetImg, r.getX(), r.getY(), r.getWidth(), r.getHeight(), juce::RectanglePlacement::centred);
    }

}

void PingEditor::resized()
{
    licenceScreen.setBounds (getLocalBounds());
    if (licenceScreen.isVisible())
        licenceScreen.toFront (true);

    irSynthComponent.setBounds (getLocalBounds());
    if (irSynthComponent.isVisible())
        irSynthComponent.toFront (true);

    int w = getWidth();
    int h = getHeight();
    auto b = getLocalBounds().reduced (juce::jmin (12, w / 76), juce::jmin (12, h / 38));

    const int topRowH = juce::jmax (38, (int) (0.09f * h));
    const int knobSize = juce::jmax (40, juce::jmin (72, (int) (0.078f * w)));
    const int sixKnobSize = (int) (knobSize * 1.2f);
    const int labelH = juce::jmax (10, (int) (0.024f * h));
    const int readoutH = juce::jmax (10, (int) (0.022f * h));
    const int sixRowH = sixKnobSize + labelH + readoutH + (int) (0.01f * h);
    const int bigKnobSize = juce::jmax (80, juce::jmin (128, (int) (0.15f * w)));
    const int eqMinH = 175;
    const int eqHeight = juce::jmax (eqMinH, (int) (0.36f * h));
    const int gapV = (int) (0.01f * h) + (int) (0.008f * h);
    const int marginV = juce::jmin (12, h / 38);
    const int availableForMainAndEq = h - 2 * marginV - topRowH - gapV;
    const int erTailRowH = 28;
    const int mainHClamped = juce::jmax (3 * sixRowH, availableForMainAndEq - eqHeight - erTailRowH);
    const int mainHeight = juce::jmin (mainHClamped, availableForMainAndEq - eqHeight - erTailRowH - 6);

    // —— Top row: Spitfire (left) | Preset menu (center, greyed - key menu) | P!NG (right) ——
    auto topRow = b.removeFromTop (topRowH);
    const int leftLogoW = juce::jmin (150, (int) (0.17f * w));
    const int rightLogoW = juce::jmin (90, (int) (0.10f * w));
    spitfireBounds = topRow.removeFromLeft (leftLogoW).reduced (2);
    pingBounds = topRow.removeFromRight (rightLogoW).reduced (2);
    auto presetArea = topRow.reduced (4);
    const int saveButtonW = 48;
    int presetComboW = juce::jmin ((int) (0.38f * w), presetArea.getWidth() - saveButtonW - 6);
    presetCombo.setBounds (presetArea.getX() + (presetArea.getWidth() - (presetComboW + 6 + saveButtonW)) / 2,
                          presetArea.getY() + 2, presetComboW, presetArea.getHeight() - 4);
    savePresetButton.setBounds (presetCombo.getRight() + 6, presetArea.getY() + 2, saveButtonW, presetArea.getHeight() - 4);

    const int presetCenterX = presetCombo.getX() + presetCombo.getWidth() / 2;

    const int meterGap = 6;
    const int meterH = 5;
    int meterX = spitfireBounds.getRight() + meterGap;
    int meterW = juce::jmax (0, presetCombo.getX() - meterX - meterGap);
    int meterY = spitfireBounds.getY() + (spitfireBounds.getHeight() - meterH) / 2;
    outputLevelMeter.setBounds (meterX, meterY, meterW, meterH);

    b.removeFromTop ((int) (0.01f * h));

    // —— Main block: knobs (left), Dry/Wet + IR combo (centre under preset), Reverse + waveform (right) ——
    auto mainArea = b.removeFromTop (mainHeight);
    int wavePanelW = juce::jmax (220, (int) (0.36f * w));
    int wavePanelH = juce::jmax (72, (int) (wavePanelW * 0.36f));
    auto rightCol = mainArea.removeFromRight (wavePanelW);
    auto reverseStrip = rightCol.removeFromTop (juce::jmin (26, rightCol.getHeight() / 4));
    reverseButton.setBounds (reverseStrip.removeFromRight (juce::jmax (68, (int)(0.075f * w))).reduced (2));
    waveformComponent.setBounds (rightCol.getX(), rightCol.getY(), rightCol.getWidth(),
                                 juce::jmin (wavePanelH, rightCol.getHeight()));

    int irComboH = 24;
    int irComboW = juce::jmin ((int) (0.24f * w), bigKnobSize + 40);
    int cy = waveformComponent.getBounds().getCentreY() - (bigKnobSize + labelH + readoutH + 4 + irComboH + 6) / 2;
    const int smallKnobSize = sixKnobSize / 2;
    const int dryWetCenterY = cy + bigKnobSize / 2;
    const int irKnobGap = 6;

    const int sixColGap = juce::jmax (8, (int) (0.01f * w));
    const int sixColW = sixKnobSize + sixColGap;
    int x1 = mainArea.getX();
    int x2 = mainArea.getX() + sixColW;
    int x3 = mainArea.getX() + 2 * sixColW;
    const int tailModCenterX = x3 + sixKnobSize / 2;
    const int irKnobsCenterX = (tailModCenterX + presetCenterX) / 2;

    const int irLabelW = juce::jmax (90, smallKnobSize * 2);

    irInputGainSlider.setBounds (irKnobsCenterX - smallKnobSize / 2, dryWetCenterY - smallKnobSize - irKnobGap, smallKnobSize, smallKnobSize);
    irInputGainLabel.setBounds (irKnobsCenterX - irLabelW / 2, irInputGainSlider.getBottom() + 2, irLabelW, labelH);
    irInputGainReadout.setBounds (irKnobsCenterX - irLabelW / 2, irInputGainSlider.getBottom() + labelH + 2, irLabelW, readoutH);

    irInputDriveSlider.setBounds (irKnobsCenterX - smallKnobSize / 2, irInputGainReadout.getBottom() + 4, smallKnobSize, smallKnobSize);
    irInputDriveLabel.setBounds (irKnobsCenterX - irLabelW / 2, irInputDriveSlider.getBottom() + 2, irLabelW, labelH);
    irInputDriveReadout.setBounds (irKnobsCenterX - irLabelW / 2, irInputDriveSlider.getBottom() + labelH + 2, irLabelW, readoutH);

    dryWetSlider.setBounds (presetCenterX - bigKnobSize / 2, cy, bigKnobSize, bigKnobSize);
    dryWetLabel.setBounds (dryWetSlider.getX(), dryWetSlider.getBottom() + 2, bigKnobSize, labelH);
    dryWetReadout.setBounds (dryWetSlider.getX(), dryWetSlider.getBottom() + labelH + 2, bigKnobSize, readoutH);

    // IR combo + IR Synth: align IR Synth right edge with waveform left edge
    const int irSynthW = 64;
    const int irGap = 6;
    int irSynthX = waveformComponent.getX() - irSynthW;
    int irComboX = irSynthX - irGap - irComboW;
    int irRowY = dryWetReadout.getBottom() + 6;
    irCombo.setBounds (irComboX, irRowY, irComboW, irComboH);
    irSynthButton.setBounds (irSynthX, irRowY, irSynthW, irComboH);
    if (! irSynthComponent.isVisible())
        dryWetSlider.toFront (false);
    int y = mainArea.getY();

    decaySlider.setBounds (x1, y, sixKnobSize, sixKnobSize);
    decayLabel.setBounds (x1, y + sixKnobSize + 2, sixKnobSize, labelH);
    decayReadout.setBounds (x1, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    predelaySlider.setBounds (x1, y, sixKnobSize, sixKnobSize);
    predelayLabel.setBounds (x1, y + sixKnobSize + 2, sixKnobSize, labelH);
    predelayReadout.setBounds (x1, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    modDepthSlider.setBounds (x1, y, sixKnobSize, sixKnobSize);
    modDepthLabel.setBounds (x1, y + sixKnobSize + 2, sixKnobSize, labelH);
    modDepthReadout.setBounds (x1, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);

    y = mainArea.getY();
    stretchSlider.setBounds (x2, y, sixKnobSize, sixKnobSize);
    stretchLabel.setBounds (x2, y + sixKnobSize + 2, sixKnobSize, labelH);
    stretchReadout.setBounds (x2, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    widthSlider.setBounds (x2, y, sixKnobSize, sixKnobSize);
    widthLabel.setBounds (x2, y + sixKnobSize + 2, sixKnobSize, labelH);
    widthReadout.setBounds (x2, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    modRateSlider.setBounds (x2, y, sixKnobSize, sixKnobSize);
    modRateLabel.setBounds (x2, y + sixKnobSize + 2, sixKnobSize, labelH);
    modRateReadout.setBounds (x2, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);

    y = mainArea.getY();
    tailModSlider.setBounds (x3, y, sixKnobSize, sixKnobSize);
    tailModLabel.setBounds (x3, y + sixKnobSize + 2, sixKnobSize, labelH);
    tailModReadout.setBounds (x3, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    delayDepthSlider.setBounds (x3, y, sixKnobSize, sixKnobSize);
    delayDepthLabel.setBounds (x3, y + sixKnobSize + 2, sixKnobSize, labelH);
    delayDepthReadout.setBounds (x3, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    tailRateSlider.setBounds (x3, y, sixKnobSize, sixKnobSize);
    tailRateLabel.setBounds (x3, y + sixKnobSize + 2, sixKnobSize, labelH);
    tailRateReadout.setBounds (x3, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);

    b.removeFromTop ((int) (0.008f * h));

    // —— ER/Tail sliders: between waveform and EQ, within waveform width, side by side ——
    auto erTailRow = b.removeFromTop (erTailRowH);
    int sliderAreaX = erTailRow.getX() + erTailRow.getWidth() - wavePanelW;  // align with waveform
    int sliderAreaW = wavePanelW;
    int sliderH = 18;
    int labelW = 28;
    int readoutW = 38;
    int gap = 8;
    int halfW = (sliderAreaW - gap) / 2;
    int eachSliderW = halfW - labelW - readoutW - gap;

    erLevelLabel.setBounds (sliderAreaX, erTailRow.getY(), labelW, sliderH);
    erLevelSlider.setBounds (erLevelLabel.getRight() + gap, erTailRow.getY(), eachSliderW, sliderH);
    erLevelReadout.setBounds (erLevelSlider.getRight() + gap, erTailRow.getY(), readoutW, sliderH);

    int tailX = sliderAreaX + halfW + gap;
    tailLevelLabel.setBounds (tailX, erTailRow.getY(), labelW, sliderH);
    tailLevelSlider.setBounds (tailLevelLabel.getRight() + gap, erTailRow.getY(), eachSliderW, sliderH);
    tailLevelReadout.setBounds (tailLevelSlider.getRight() + gap, erTailRow.getY(), readoutW, sliderH);

    // —— Bottom: EQ (right) ——
    int eqWidth = juce::jmax (420, (int) (0.62f * w));
    auto bottomRow = b.removeFromBottom (eqHeight);
    auto eqRect = bottomRow.removeFromRight (eqWidth);
    eqGraph.setBounds (eqRect);

    licenceLabel.setBounds (12, getHeight() - 20, getWidth() - 24, 16);
}

void PingEditor::comboBoxChanged (juce::ComboBox* combo)
{
    if (combo == &irCombo)
    {
        int idx = irCombo.getSelectedId() - 1;
        if (idx >= 0)
        {
            pingProcessor.setSelectedIRIndex (idx);
            auto file = pingProcessor.getIRManager().getIRFileAt (idx);
            if (file.existsAsFile())
            {
                pingProcessor.loadIRFromFile (file);
                updateWaveform();
            }
        }
    }
    else if (combo == &presetCombo)
    {
        int id = presetCombo.getSelectedId();
        if (id > 1)
            loadPreset (presetCombo.getText());
    }
}

void PingEditor::refreshPresetList()
{
    juce::String current = presetCombo.getText();
    presetCombo.clear (juce::dontSendNotification);
    presetCombo.addItem ("Default", 1);
    auto names = PresetManager::getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presetCombo.addItem (names[i], i + 2);
    if (current == "Default" || current.isEmpty())
        presetCombo.setSelectedId (1, juce::dontSendNotification);
    else
        presetCombo.setText (current, juce::dontSendNotification);
}

void PingEditor::loadPreset (const juce::String& name)
{
    auto file = PresetManager::getPresetFile (name);
    if (file.existsAsFile())
    {
        juce::MemoryBlock data;
        if (file.loadFileAsData (data))
        {
            pingProcessor.setStateInformation (data.getData(), (int) data.getSize());
            reverseButton.setToggleState (pingProcessor.getReverse(), juce::dontSendNotification);
            int idx = pingProcessor.getSelectedIRIndex();
            irCombo.setSelectedId (idx >= 0 ? idx + 1 : 1, juce::sendNotificationSync);
            updateWaveform();
        }
    }
}

void PingEditor::setMainPanelControlsVisible (bool visible)
{
    dryWetSlider.setVisible (visible);
    dryWetLabel.setVisible (visible);
    dryWetReadout.setVisible (visible);
}

void PingEditor::savePreset (const juce::String& name)
{
    juce::MemoryBlock data;
    pingProcessor.getStateInformation (data);
    auto file = PresetManager::getPresetFile (name);
    if (file.replaceWithData (data.getData(), data.getSize()))
        refreshPresetList();
}

void PingEditor::timerCallback()
{
    updateWaveform();
    updateAllReadouts();
    outputLevelMeter.setLevelsDb (pingProcessor.getOutputLevelDb (0), pingProcessor.getOutputLevelDb (1));
    // Minimal: payload only, like LicenceScreen - no toTitleCase, no file, no stored state
    juce::String name = pingProcessor.getLicenceNameFromPayload();
    licenceLabel.setText (name.isNotEmpty() ? ("Licensed to: " + name) : juce::String ("Licensed"), juce::dontSendNotification);
}

void PingEditor::updateAllReadouts()
{
    auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

    dryWetReadout.setText (juce::String (juce::roundToInt (v ("drywet") * 100)) + "%", juce::dontSendNotification);
    predelayReadout.setText (juce::String (juce::roundToInt (v ("predelay"))) + " ms", juce::dontSendNotification);
    decayReadout.setText (juce::String (juce::roundToInt ((1.0f - v ("decay")) * 100)) + "%", juce::dontSendNotification);
    modDepthReadout.setText (juce::String (juce::roundToInt (v ("moddepth") * 100)) + "%", juce::dontSendNotification);

    float stretchVal = v ("stretch");
    stretchReadout.setText (juce::String (stretchVal, 2) + "x", juce::dontSendNotification);

    float widthVal = v ("width");
    if (widthVal < 0.05f)
        widthReadout.setText ("Mono", juce::dontSendNotification);
    else if (widthVal > 1.95f)
        widthReadout.setText ("200%", juce::dontSendNotification);
    else
        widthReadout.setText (juce::String (juce::roundToInt (widthVal * 100)) + "%", juce::dontSendNotification);

    float periodSec = 2.01f - v ("modrate");
    if (periodSec >= 1.0f)
        modRateReadout.setText (juce::String (periodSec, 2) + " s", juce::dontSendNotification);
    else
        modRateReadout.setText (juce::String (1.0 / periodSec, 1) + " Hz", juce::dontSendNotification);

    tailModReadout.setText (juce::String (juce::roundToInt (v ("tailmod") * 100)) + "%", juce::dontSendNotification);
    delayDepthReadout.setText (juce::String (v ("delaydepth"), 1) + " ms", juce::dontSendNotification);
    tailRateReadout.setText (juce::String (v ("tailrate"), 2) + " Hz", juce::dontSendNotification);
    irInputGainReadout.setText (juce::String (juce::roundToInt (v ("inputGain"))) + " dB", juce::dontSendNotification);
    irInputDriveReadout.setText (juce::String (juce::roundToInt (v ("irInputDrive") * 100)) + "%", juce::dontSendNotification);

    float erDb = v ("erLevel");
    float tailDb = v ("tailLevel");
    erLevelReadout.setText (erDb <= -47.0f ? "-inf dB" : juce::String (juce::roundToInt (erDb)) + " dB", juce::dontSendNotification);
    tailLevelReadout.setText (tailDb <= -47.0f ? "-inf dB" : juce::String (juce::roundToInt (tailDb)) + " dB", juce::dontSendNotification);
}

void PingEditor::refreshIRList()
{
    pingProcessor.getIRManager().refresh();
    irCombo.clear();
    auto names = pingProcessor.getIRManager().getDisplayNames();
    for (int i = 0; i < names.size(); ++i)
        irCombo.addItem (names[i], i + 1);
    if (irCombo.getNumItems() > 0 && irCombo.getSelectedId() <= 0)
        irCombo.setSelectedId (1, juce::sendNotificationSync);
    loadSelectedIR();
}

void PingEditor::loadSelectedIR()
{
    int idx = irCombo.getSelectedId() - 1;
    if (idx < 0) return;
    auto file = pingProcessor.getIRManager().getIRFileAt (idx);
    if (file.existsAsFile())
        pingProcessor.loadIRFromFile (file);
}

void PingEditor::updateWaveform()
{
    waveformComponent.repaint();
}
