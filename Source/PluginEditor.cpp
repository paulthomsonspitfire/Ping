#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PingBinaryData.h"

namespace
{
    constexpr int editorW = 1104;
    constexpr int editorH = 528;
    constexpr int minW = 864;
    constexpr int minH = 528;
    constexpr int maxW = 1920;
    constexpr int maxH = 1080;
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
      eqGraph (p.getAPVTS(), &p),
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
    savePresetButton.setComponentID ("SavePreset");
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
    irSynthButton.setComponentID ("IRSynth");
    irSynthButton.setButtonText ("IR SYNTH");
    irSynthButton.setColour (juce::TextButton::buttonColourId, panelBg);
    irSynthButton.setColour (juce::TextButton::textColourOffId, textDim);
    irSynthButton.onClick = [this]
    {
        irSynthComponent.setParams (pingProcessor.getLastIRSynthParams());
        irSynthComponent.setIRList (pingProcessor.getIRManager().getDisplayNames4Channel());
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
        pingProcessor.setLastIRSynthParams (irSynthComponent.getLastRenderParams());
        pingProcessor.loadIRFromBuffer (std::move (buf), (double) result.sampleRate, true);
        updateWaveform();
    });
    irSynthComponent.setOnDone ([this]
    {
        irSynthComponent.setVisible (false);
        setMainPanelControlsVisible (true);
        refreshIRList();
        updateWaveform();
    });
    irSynthComponent.setOnSaveIR ([this] (const juce::String& name)
    {
        saveSynthIR (name);
    });
    irSynthComponent.setOnLoadIR ([this] (int index)
    {
        auto file = pingProcessor.getIRManager().getIRFileAt4Channel (index);
        if (file.existsAsFile())
        {
            int fullIdx = -1;
            auto files = pingProcessor.getIRManager().getIRFiles();
            for (int i = 0; i < files.size(); ++i)
            {
                if (files[i] == file) { fullIdx = i; break; }
            }
            if (fullIdx >= 0)
                pingProcessor.setSelectedIRIndex (fullIdx);
            pingProcessor.loadIRFromFile (file);
            auto sidecar = file.getSiblingFile (file.getFileNameWithoutExtension() + ".ping");
            if (sidecar.existsAsFile())
            {
                auto params = PingProcessor::loadIRSynthParamsFromSidecar (file);
                pingProcessor.setLastIRSynthParams (params);
                irSynthComponent.setParams (params);
            }
            irSynthComponent.setSelectedIRDisplayName (file.getFileNameWithoutExtension());
            refreshIRList();
            updateWaveform();
        }
    });
    irSynthComponent.setBakeLevelsGetter ([this]
    {
        auto* erParam = apvts.getRawParameterValue ("erLevel");
        auto* tailParam = apvts.getRawParameterValue ("tailLevel");
        float erDb = erParam != nullptr ? erParam->load() : 0.0f;
        float tailDb = tailParam != nullptr ? tailParam->load() : 0.0f;
        return std::pair<float, float> { erDb, tailDb };
    });
    irSynthComponent.setParams (pingProcessor.getLastIRSynthParams());

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

    dryWetSlider.setComponentID ("DryWet");
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
    makeSlider (outputGainSlider, "Wet Output");
    outputGainSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4caf50));
    outputGainSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff4caf50));

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

    // Crossfeed row knobs (accent colour, same rotary style)
    for (auto* s : { &erCrossfeedDelaySlider, &erCrossfeedAttSlider,
                     &tailCrossfeedDelaySlider, &tailCrossfeedAttSlider })
        makeSlider (*s, "");
    // Buttons
    for (auto* b : { &erCrossfeedOnButton, &tailCrossfeedOnButton })
    {
        addAndMakeVisible (b);
        b->setButtonText ("");
    }
    erCrossfeedOnButton.setComponentID ("ERCrossfeedSwitch");
    tailCrossfeedOnButton.setComponentID ("TailCrossfeedSwitch");

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
    outputGainSlider.setRange (-24.0, 12.0, 0.5);
    irInputDriveSlider.setRange (0.0, 1.0, 0.01);
    erLevelSlider.setRange (-48.0, 6.0, 0.1);
    tailLevelSlider.setRange (-48.0, 6.0, 0.1);
    erCrossfeedDelaySlider.setRange (5.0, 15.0, 0.5);
    erCrossfeedAttSlider.setRange (-24.0, 0.0, 0.5);
    tailCrossfeedDelaySlider.setRange (5.0, 15.0, 0.5);
    tailCrossfeedAttSlider.setRange (-24.0, 0.0, 0.5);

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
    outputGainAttach   = std::make_unique<SliderAttachment> (apvts, "outputGain", outputGainSlider);
    irInputDriveAttach = std::make_unique<SliderAttachment> (apvts, "irInputDrive", irInputDriveSlider);
    erLevelAttach   = std::make_unique<SliderAttachment> (apvts, "erLevel", erLevelSlider);
    tailLevelAttach = std::make_unique<SliderAttachment> (apvts, "tailLevel", tailLevelSlider);
    erCrossfeedDelayAttach   = std::make_unique<SliderAttachment> (apvts, "erCrossfeedDelayMs",   erCrossfeedDelaySlider);
    erCrossfeedAttAttach     = std::make_unique<SliderAttachment> (apvts, "erCrossfeedAttDb",     erCrossfeedAttSlider);
    tailCrossfeedDelayAttach = std::make_unique<SliderAttachment> (apvts, "tailCrossfeedDelayMs", tailCrossfeedDelaySlider);
    tailCrossfeedAttAttach   = std::make_unique<SliderAttachment> (apvts, "tailCrossfeedAttDb",   tailCrossfeedAttSlider);
    erCrossfeedOnAttach      = std::make_unique<ButtonAttachment> (apvts, "erCrossfeedOn",        erCrossfeedOnButton);
    tailCrossfeedOnAttach    = std::make_unique<ButtonAttachment> (apvts, "tailCrossfeedOn",      tailCrossfeedOnButton);

    setLookAndFeel (&pingLook);

    // Labels
    for (auto* label : { &dryWetLabel, &predelayLabel, &decayLabel, &modDepthLabel,
                        &stretchLabel, &widthLabel, &modRateLabel,
                        &tailModLabel, &delayDepthLabel, &tailRateLabel,
                        &irInputGainLabel, &irInputDriveLabel, &outputGainLabel,
                        &erLevelLabel, &tailLevelLabel,
                        &erCrossfeedDelayLabel, &erCrossfeedAttLabel,
                        &tailCrossfeedDelayLabel, &tailCrossfeedAttLabel })
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
    irInputGainLabel.setText ("GAIN", juce::dontSendNotification);
    outputGainLabel.setText ("WET OUTPUT", juce::dontSendNotification);
    outputGainLabel.setFont (juce::FontOptions (9.0f));
    irInputDriveLabel.setText ("DRIVE", juce::dontSendNotification);
    erLevelLabel.setText ("ER", juce::dontSendNotification);
    tailLevelLabel.setText ("TAIL", juce::dontSendNotification);
    erCrossfeedDelayLabel.setText   ("DELAY", juce::dontSendNotification);
    erCrossfeedAttLabel.setText     ("ATT",   juce::dontSendNotification);
    tailCrossfeedDelayLabel.setText ("DELAY", juce::dontSendNotification);
    tailCrossfeedAttLabel.setText   ("ATT",   juce::dontSendNotification);

    for (auto* r : { &dryWetReadout, &predelayReadout, &decayReadout, &modDepthReadout,
                     &stretchReadout, &widthReadout, &modRateReadout,
                     &tailModReadout, &delayDepthReadout, &tailRateReadout,
                     &irInputGainReadout, &irInputDriveReadout, &outputGainReadout,
                     &erLevelReadout, &tailLevelReadout,
                     &erCrossfeedDelayReadout, &erCrossfeedAttReadout,
                     &tailCrossfeedDelayReadout, &tailCrossfeedAttReadout })
    {
        addAndMakeVisible (r);
        r->setJustificationType (juce::Justification::centred);
        r->setColour (juce::Label::textColourId, accent);
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

    addAndMakeVisible (versionLabel);
    versionLabel.setJustificationType (juce::Justification::centred);
    versionLabel.setColour (juce::Label::textColourId, textDim);
    versionLabel.setFont (juce::FontOptions (11.0f));
    versionLabel.setText (juce::String ("v") + ProjectInfo::versionString, juce::dontSendNotification);

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
    if (savedIdx >= 0 && savedIdx < irCombo.getNumItems() - 1)  // -1 for Synthesized item
        irCombo.setSelectedId (savedIdx + 2, juce::dontSendNotification);
    loadSelectedIR();
    startTimerHz (8);
}

PingEditor::~PingEditor()
{
    pingProcessor.setLastIRSynthParams (irSynthComponent.getParams());
    setLookAndFeel (nullptr);
    apvts.removeParameterListener ("stretch", this);
    apvts.removeParameterListener ("decay", this);
    irCombo.removeListener (this);
}

void PingEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "stretch" || parameterID == "decay")
        loadSelectedIR();
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
        {
            const int pw = pingBounds.getWidth() * 2;
            const int ph = pingBounds.getHeight() * 2;
            auto drawRect = juce::Rectangle<int> (pingBounds.getRight() - pw, pingBounds.getCentreY() - ph / 2, pw, ph);
            g.drawImageWithin (pingImg, drawRect.getX(), drawRect.getY(), drawRect.getWidth(), drawRect.getHeight(),
                              juce::RectanglePlacement (juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid));
        }
    }

    // Group headers: label text above a horizontal line
    g.setColour (textDim);
    g.setFont (juce::FontOptions (9.0f));
    auto drawGroupHeader = [&](const juce::Rectangle<int>& bounds, const juce::String& text)
    {
        if (bounds.getWidth() <= 0) return;
        const float lineY = (float) bounds.getBottom() - 1.0f;
        g.drawText (text, bounds.getX(), bounds.getY(),
                    bounds.getWidth(), bounds.getHeight() - 3,
                    juce::Justification::centredLeft, false);
        g.drawLine ((float) bounds.getX(), lineY, (float) bounds.getRight(), lineY, 1.0f);
    };
    drawGroupHeader (irInputGroupBounds,    "IR Input");
    drawGroupHeader (irControlsGroupBounds, "IR Controls");
    drawGroupHeader (erCrossfadeGroupBounds,   "ER Crossfade");
    drawGroupHeader (tailCrossfadeGroupBounds, "Tail Crossfade");

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
    // Content occupies the right 5/6 of width and top 5/6 of height,
    // leaving ~20% empty space on the left and at the bottom.
    const int leftPad   = w / 6;
    const int cw = w - leftPad;
    const int ch = h - h / 6;
    auto b = juce::Rectangle<int> (leftPad, 0, cw, ch)
             .reduced (juce::jmin (12, cw / 76), juce::jmin (12, ch / 38));

    const int topRowH = juce::jmax (38, (int) (0.09f * ch));
    const int knobSize = juce::jmax (40, juce::jmin (72, (int) (0.078f * cw)));
    const int sixKnobSize = (int) (knobSize * 1.2f);
    const int labelH = juce::jmax (10, (int) (0.024f * ch));
    const int readoutH = juce::jmax (10, (int) (0.022f * ch));
    const int sixRowH = sixKnobSize + labelH + readoutH + (int) (0.01f * ch);
    const int bigKnobSize = juce::jmax (80, juce::jmin (128, (int) (0.15f * cw)));
    const int eqMinH = 175;
    const int eqHeight = juce::jmax (eqMinH, (int) (0.36f * ch));
    const int gapV = (int) (0.01f * ch) + (int) (0.008f * ch);
    const int marginV = juce::jmin (12, ch / 38);
    const int availableForMainAndEq = ch - 2 * marginV - topRowH - gapV;
    const int erTailRowH = 28;
    const int mainHClamped = juce::jmax (3 * sixRowH, availableForMainAndEq - eqHeight - erTailRowH);
    const int mainHeight = juce::jmin (mainHClamped, availableForMainAndEq - eqHeight - erTailRowH - 6);

    // —— Top row: Spitfire (left) | Preset menu (center, greyed - key menu) | P!NG (right) ——
    auto topRow = b.removeFromTop (topRowH);
    const int leftLogoW = juce::jmin (150, (int) (0.17f * cw));
    const int rightLogoW = juce::jmin (90, (int) (0.10f * cw));
    spitfireBounds = topRow.removeFromLeft (leftLogoW).reduced (2);
    pingBounds = topRow.removeFromRight (rightLogoW).reduced (2);
    auto presetArea = topRow.reduced (4);
    const int saveButtonW = 48;
    int presetComboW = juce::jmin ((int) (0.38f * cw), presetArea.getWidth() - saveButtonW - 6);
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

    b.removeFromTop ((int) (0.01f * ch));

    // —— Main block: knobs (left), Dry/Wet + IR combo (centre under preset), Reverse + waveform (right) ——
    auto mainArea = b.removeFromTop (mainHeight);
    int wavePanelW = juce::jmax (220, (int) (0.36f * cw));
    int wavePanelH = juce::jmax (72, (int) (wavePanelW * 0.36f));
    auto rightCol = mainArea.removeFromRight (wavePanelW);
    auto reverseStrip = rightCol.removeFromTop (juce::jmin (26, rightCol.getHeight() / 4));
    reverseButton.setBounds (reverseStrip.removeFromRight (juce::jmax (68, (int)(0.075f * cw))).reduced (2));
    waveformComponent.setBounds (rightCol.getX(), rightCol.getY(), rightCol.getWidth(),
                                 juce::jmin (wavePanelH, rightCol.getHeight()));

    int irComboH = 24;
    int irComboW = juce::jmin ((int) (0.24f * cw), bigKnobSize + 40);
    int cy = waveformComponent.getBounds().getCentreY() - (bigKnobSize + labelH + readoutH + 4 + irComboH + 6) / 2;
    const int smallKnobSize = sixKnobSize / 2;
    const int dryWetCenterY = cy + bigKnobSize / 2;
    const int irKnobGap = 6;

    const int sixColGap = juce::jmax (8, (int) (0.01f * cw));
    const int sixColW = sixKnobSize + sixColGap;
    int x1 = mainArea.getX();
    int x2 = mainArea.getX() + sixColW;
    int x3 = mainArea.getX() + 2 * sixColW;
    const int tailModCenterX = x3 + sixKnobSize / 2;
    const int irKnobsCenterX = (tailModCenterX + presetCenterX) / 2;

    const int irLabelW = juce::jmax (90, smallKnobSize * 2);

    // irInputGain and irInputDrive are now placed in the top row below the main area start — see row placement below

    dryWetSlider.setBounds (presetCenterX - bigKnobSize / 2, cy, bigKnobSize, bigKnobSize);
    dryWetLabel.setBounds (0, 0, 0, 0);  // unused: "dry/wet" text is drawn in knob centre
    dryWetReadout.setBounds (dryWetSlider.getX(), dryWetSlider.getBottom() + 2, bigKnobSize, readoutH);

    // IR combo + IR Synth: align IR Synth right edge with waveform left edge (needed for output gain placement)
    const int irSynthW = 64;
    const int irGap = 6;
    int irSynthX = waveformComponent.getX() - irSynthW;
    int irComboX = irSynthX - irGap - irComboW;
    int irRowY = dryWetReadout.getBottom() + 6 + labelH;  // +labelH restores space from removed dry/wet image

    // Wet Output: centred to the right of irKnobsCenterX (irInputGain/Drive now live in the top row)
    const int outputGainCenterX = irKnobsCenterX + smallKnobSize / 2;
    const int outputGainY = dryWetCenterY - smallKnobSize - irKnobGap;
    outputGainSlider.setBounds (outputGainCenterX - smallKnobSize / 2, outputGainY, smallKnobSize, smallKnobSize);
    outputGainLabel.setBounds (outputGainCenterX - irLabelW / 2, outputGainSlider.getBottom() + 2, irLabelW, labelH);
    outputGainReadout.setBounds (outputGainCenterX - irLabelW / 2, outputGainSlider.getBottom() + labelH + 2, irLabelW, readoutH);

    irCombo.setBounds (irComboX, irRowY, irComboW, irComboH);
    irSynthButton.setBounds (irSynthX, irRowY, irSynthW, irComboH);
    if (! irSynthComponent.isVisible())
        dryWetSlider.toFront (false);

    // —— Row of 5 controls: Input Gain | IR Input Drive | Predelay | Damping | Stretch ——
    const int rowKnobSize  = (int)(sixKnobSize * 0.6f);
    const int rowLabelW    = juce::jmax (64, rowKnobSize + 14);
    const int rowGap       = juce::jmax (6,  (int)(0.008f * cw));
    const int groupLabelH  = 14;   // height reserved for "IR Input" text + line above knobs 0-1
    const int rowTotalH    = rowKnobSize + labelH + readoutH + 6;
    auto topKnobRow = mainArea.removeFromTop (rowTotalH + 4 + groupLabelH);
    const int rowY       = topKnobRow.getY() + groupLabelH;  // knobs sit below the group header
    const int rowStep    = rowKnobSize + rowGap;
    const int rowStartX  = juce::jmax (8, w / 128) + 5;     // 5 px right of the window edge margin

    auto placeRowKnob = [&](juce::Slider& s, juce::Label& lbl, juce::Label& rdout, int idx)
    {
        // Add 5 px extra gap before knob index 2 (separates the IR-Input pair from the rest)
        const int extraGap = (idx >= 2) ? 5 : 0;
        const int cx = rowStartX + rowKnobSize / 2 + idx * rowStep + extraGap;
        s.setBounds    (cx - rowKnobSize / 2, rowY,                       rowKnobSize, rowKnobSize);
        lbl.setBounds  (cx - rowLabelW / 2,   s.getBottom() + 2,          rowLabelW,   labelH);
        rdout.setBounds(cx - rowLabelW / 2,   s.getBottom() + labelH + 2, rowLabelW,   readoutH);
    };
    placeRowKnob (irInputGainSlider,  irInputGainLabel,  irInputGainReadout,  0);
    placeRowKnob (irInputDriveSlider, irInputDriveLabel, irInputDriveReadout, 1);
    placeRowKnob (predelaySlider,     predelayLabel,     predelayReadout,     2);
    placeRowKnob (decaySlider,        decayLabel,        decayReadout,        3);
    placeRowKnob (stretchSlider,      stretchLabel,      stretchReadout,      4);

    // Store group bounds for painting (text + line above each pair/triple)
    irInputGroupBounds = juce::Rectangle<int> (
        irInputGainSlider.getX(),
        topKnobRow.getY(),
        irInputDriveSlider.getRight() - irInputGainSlider.getX(),
        groupLabelH);
    irControlsGroupBounds = juce::Rectangle<int> (
        predelaySlider.getX(),
        topKnobRow.getY(),
        stretchSlider.getRight() - predelaySlider.getX(),
        groupLabelH);

    // —— Row 2: ER Crossfade (delay, att, switch) | Tail Crossfade (delay, att, switch) ——
    const int switchH = 18;
    const int switchW = 36;
    const int row2TotalH = groupLabelH + rowKnobSize + labelH + readoutH + 6 + switchH + 4;
    auto row2Area = mainArea.removeFromTop (row2TotalH);
    const int row2KnobY = row2Area.getY() + groupLabelH;

    auto placeRow2Knob = [&](juce::Slider& s, juce::Label& lbl, juce::Label& rdout, int idx)
    {
        const int extraGap = (idx >= 2) ? 5 : 0;
        const int cx = rowStartX + rowKnobSize / 2 + idx * rowStep + extraGap;
        s.setBounds    (cx - rowKnobSize / 2, row2KnobY,                    rowKnobSize, rowKnobSize);
        lbl.setBounds  (cx - rowLabelW / 2,   s.getBottom() + 2,            rowLabelW,   labelH);
        rdout.setBounds(cx - rowLabelW / 2,   s.getBottom() + labelH + 2,   rowLabelW,   readoutH);
    };
    placeRow2Knob (erCrossfeedDelaySlider,   erCrossfeedDelayLabel,   erCrossfeedDelayReadout,   0);
    placeRow2Knob (erCrossfeedAttSlider,     erCrossfeedAttLabel,     erCrossfeedAttReadout,     1);
    placeRow2Knob (tailCrossfeedDelaySlider, tailCrossfeedDelayLabel, tailCrossfeedDelayReadout, 2);
    placeRow2Knob (tailCrossfeedAttSlider,   tailCrossfeedAttLabel,   tailCrossfeedAttReadout,   3);

    // Switches: centred under each 2-knob pair
    {
        const int erCentreX  = (erCrossfeedDelaySlider.getX()   + erCrossfeedAttSlider.getRight())   / 2;
        const int tailCentreX = (tailCrossfeedDelaySlider.getX() + tailCrossfeedAttSlider.getRight()) / 2;
        const int switchY = erCrossfeedAttReadout.getBottom() + 4;
        erCrossfeedOnButton.setBounds   (erCentreX   - switchW / 2, switchY, switchW, switchH);
        tailCrossfeedOnButton.setBounds (tailCentreX - switchW / 2, switchY, switchW, switchH);
    }

    // Store group header bounds for painting
    erCrossfadeGroupBounds = juce::Rectangle<int> (
        erCrossfeedDelaySlider.getX(),
        row2Area.getY(),
        erCrossfeedAttSlider.getRight() - erCrossfeedDelaySlider.getX(),
        groupLabelH);
    tailCrossfadeGroupBounds = juce::Rectangle<int> (
        tailCrossfeedDelaySlider.getX(),
        row2Area.getY(),
        tailCrossfeedAttSlider.getRight() - tailCrossfeedDelaySlider.getX(),
        groupLabelH);

    // —— Remaining 6 knobs (grid): LFO Depth | Width | LFO Rate | Tail Mod | Delay Depth | Rate ——
    int y = mainArea.getY();   // below the row

    modDepthSlider.setBounds (x1, y, sixKnobSize, sixKnobSize);
    modDepthLabel.setBounds  (x1, y + sixKnobSize + 2,          sixKnobSize, labelH);
    modDepthReadout.setBounds(x1, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);

    widthSlider.setBounds    (x2, y, sixKnobSize, sixKnobSize);
    widthLabel.setBounds     (x2, y + sixKnobSize + 2,          sixKnobSize, labelH);
    widthReadout.setBounds   (x2, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    modRateSlider.setBounds  (x2, y, sixKnobSize, sixKnobSize);
    modRateLabel.setBounds   (x2, y + sixKnobSize + 2,          sixKnobSize, labelH);
    modRateReadout.setBounds (x2, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);

    y = mainArea.getY();
    tailModSlider.setBounds  (x3, y, sixKnobSize, sixKnobSize);
    tailModLabel.setBounds   (x3, y + sixKnobSize + 2,          sixKnobSize, labelH);
    tailModReadout.setBounds (x3, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    delayDepthSlider.setBounds  (x3, y, sixKnobSize, sixKnobSize);
    delayDepthLabel.setBounds   (x3, y + sixKnobSize + 2,          sixKnobSize, labelH);
    delayDepthReadout.setBounds (x3, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);
    y += sixRowH;

    tailRateSlider.setBounds  (x3, y, sixKnobSize, sixKnobSize);
    tailRateLabel.setBounds   (x3, y + sixKnobSize + 2,          sixKnobSize, labelH);
    tailRateReadout.setBounds (x3, y + sixKnobSize + labelH + 2, sixKnobSize, readoutH);

    b.removeFromTop ((int) (0.008f * ch));

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
    int eqWidth = juce::jmax (420, (int) (0.62f * cw));
    auto bottomRow = b.removeFromBottom (eqHeight);
    auto eqRect = bottomRow.removeFromRight (eqWidth);
    eqGraph.setBounds (eqRect);

    licenceLabel.setBounds (leftPad + 12, ch - 20, cw - 24, 16);
    versionLabel.setBounds (tailRateSlider.getX(), ch - 20, tailRateSlider.getWidth(), 16);
}

void PingEditor::comboBoxChanged (juce::ComboBox* combo)
{
    if (combo == &irCombo)
    {
        int idx = irCombo.getSelectedId() - 2;  // id 1=Synth, 2+=files
        if (idx >= 0)
        {
            pingProcessor.setSelectedIRIndex (idx);
            auto file = pingProcessor.getIRManager().getIRFileAt (idx);
            if (file.existsAsFile())
            {
                pingProcessor.loadIRFromFile (file);
                auto sidecar = file.getSiblingFile (file.getFileNameWithoutExtension() + ".ping");
                if (sidecar.existsAsFile())
                {
                    auto params = PingProcessor::loadIRSynthParamsFromSidecar (file);
                    pingProcessor.setLastIRSynthParams (params);
                    irSynthComponent.setParams (params);
                }
                irSynthComponent.setSelectedIRDisplayName (file.getFileNameWithoutExtension());
                updateWaveform();
            }
        }
        else
        {
            pingProcessor.setSelectedIRIndex (-1);  // Synthesized IR selected
            irSynthComponent.setSelectedIRDisplayName ("");
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
    {
        // If a file exists with the current typed name, select it so overwrites
        // target the exact preset currently shown as selected in the dropdown.
        int matchedIndex = -1;
        for (int i = 0; i < names.size(); ++i)
        {
            if (names[i] == current)
            {
                matchedIndex = i;
                break;
            }
        }

        if (matchedIndex >= 0)
            presetCombo.setSelectedId (matchedIndex + 2, juce::dontSendNotification);
        else
            presetCombo.setText (current, juce::dontSendNotification);
    }
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
            irSynthComponent.setParams (pingProcessor.getLastIRSynthParams());
            int idx = pingProcessor.getSelectedIRIndex();
            irCombo.setSelectedId (idx >= 0 ? idx + 2 : 1, juce::sendNotificationSync);
            updateWaveform();
        }
    }
}

void PingEditor::setMainPanelControlsVisible (bool visible)
{
    dryWetSlider.setVisible (visible);
    dryWetLabel.setVisible (visible);
    dryWetReadout.setVisible (visible);
    erCrossfeedDelaySlider.setVisible (visible);
    erCrossfeedDelayLabel.setVisible (visible);
    erCrossfeedDelayReadout.setVisible (visible);
    erCrossfeedAttSlider.setVisible (visible);
    erCrossfeedAttLabel.setVisible (visible);
    erCrossfeedAttReadout.setVisible (visible);
    erCrossfeedOnButton.setVisible (visible);
    tailCrossfeedDelaySlider.setVisible (visible);
    tailCrossfeedDelayLabel.setVisible (visible);
    tailCrossfeedDelayReadout.setVisible (visible);
    tailCrossfeedAttSlider.setVisible (visible);
    tailCrossfeedAttLabel.setVisible (visible);
    tailCrossfeedAttReadout.setVisible (visible);
    tailCrossfeedOnButton.setVisible (visible);
}

void PingEditor::savePreset (const juce::String& name)
{
    if (irSynthComponent.isVisible())
        pingProcessor.setLastIRSynthParams (irSynthComponent.getParams());
    juce::MemoryBlock data;
    pingProcessor.getStateInformation (data);
    juce::String trimmedName = name.trim();
    auto file = PresetManager::getPresetFile (trimmedName);

    const bool targetExists = file.existsAsFile();
    const int selectedId = presetCombo.getSelectedId();
    const juce::String selectedName = (selectedId > 0 ? presetCombo.getItemText (selectedId) : juce::String());
    const bool sameAsCurrentSelection = (trimmedName == selectedName);
    if (targetExists && sameAsCurrentSelection && selectedId > 1)
    {
        // Don't block with modal loops inside a plugin. Use an async message box and
        // only write/refresh the preset if the user confirms.
        juce::MemoryBlock payload = data;
        auto fileToWrite = file;
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon,
            "Overwrite preset?",
            "Preset \"" + trimmedName + "\" already exists.\nOverwrite it?",
            "Overwrite",
            "Cancel",
            this);

        juce::AlertWindow::showAsync (options, [this, fileToWrite, payload] (int result) mutable
        {
            if (result != 1) // 1 == first button clicked ("Overwrite")
                return;

            if (fileToWrite.replaceWithData (payload.getData(), payload.getSize()))
                refreshPresetList();
        });
        return;
    }

    if (file.replaceWithData (data.getData(), data.getSize()))
    {
        refreshPresetList();
    }
}

void PingEditor::saveSynthIR (const juce::String& name)
{
    juce::String trimmedName = name.trim();
    if (trimmedName.isEmpty())
        return;

    juce::String safeName = trimmedName.replaceCharacters ("/\\:*?\"<>|", "----------");
    auto targetFile = IRManager::getIRFolder().getChildFile (safeName + ".wav");

    const bool targetExists = targetFile.existsAsFile();
    const int selectedId = irSynthComponent.getSelectedIRId();
    const juce::String selectedName = irSynthComponent.getSelectedIRName().trim();
    const bool sameAsCurrentSelection = (trimmedName == selectedName);

    if (targetExists && sameAsCurrentSelection && selectedId >= 1)
    {
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon,
            "Overwrite IR?",
            "IR \"" + trimmedName + "\" already exists.\nOverwrite it?",
            "Overwrite",
            "Cancel",
            this);

        juce::AlertWindow::showAsync (options, [this, trimmedName] (int result)
        {
            if (result != 1) // 1 == first button clicked ("Overwrite")
                return;

            finishSaveSynthIR (trimmedName);
        });
        return;
    }

    finishSaveSynthIR (trimmedName);
}

void PingEditor::finishSaveSynthIR (const juce::String& name)
{
    auto file = pingProcessor.saveCurrentIRToFile (name);
    if (file.existsAsFile())
    {
        pingProcessor.getIRManager().refresh();
        int idx = -1;
        auto files = pingProcessor.getIRManager().getIRFiles();
        for (int i = 0; i < files.size(); ++i)
        {
            if (files[i] == file) { idx = i; break; }
        }
        if (idx >= 0)
        {
            pingProcessor.setSelectedIRIndex (idx);
            pingProcessor.loadIRFromFile (file);
        }
        refreshIRList();
        refreshPresetList();
        irSynthComponent.setIRList (pingProcessor.getIRManager().getDisplayNames4Channel());
        updateWaveform();
    }
}

void PingEditor::timerCallback()
{
    if (irSynthComponent.isVisible())
        pingProcessor.setLastIRSynthParams (irSynthComponent.getParams());
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
    outputGainReadout.setText (juce::String (juce::roundToInt (v ("outputGain"))) + " dB", juce::dontSendNotification);
    irInputDriveReadout.setText (juce::String (juce::roundToInt (v ("irInputDrive") * 100)) + "%", juce::dontSendNotification);

    float erDb = v ("erLevel");
    float tailDb = v ("tailLevel");
    erLevelReadout.setText (erDb <= -47.0f ? "-inf dB" : juce::String (juce::roundToInt (erDb)) + " dB", juce::dontSendNotification);
    tailLevelReadout.setText (tailDb <= -47.0f ? "-inf dB" : juce::String (juce::roundToInt (tailDb)) + " dB", juce::dontSendNotification);

    erCrossfeedDelayReadout.setText  (juce::String (v ("erCrossfeedDelayMs"),   1) + " ms", juce::dontSendNotification);
    float erAttDb = v ("erCrossfeedAttDb");
    erCrossfeedAttReadout.setText    (erAttDb <= -23.5f ? "-inf dB" : juce::String (juce::roundToInt (erAttDb)) + " dB", juce::dontSendNotification);
    tailCrossfeedDelayReadout.setText(juce::String (v ("tailCrossfeedDelayMs"), 1) + " ms", juce::dontSendNotification);
    float tailAttDb = v ("tailCrossfeedAttDb");
    tailCrossfeedAttReadout.setText  (tailAttDb <= -23.5f ? "-inf dB" : juce::String (juce::roundToInt (tailAttDb)) + " dB", juce::dontSendNotification);
}

void PingEditor::refreshIRList()
{
    pingProcessor.getIRManager().refresh();
    irCombo.clear();
    irCombo.addItem ("Synthesized IR", 1);
    auto names = pingProcessor.getIRManager().getDisplayNames();
    for (int i = 0; i < names.size(); ++i)
        irCombo.addItem (names[i], i + 2);  // id 1=Synth, 2+=files
    if (pingProcessor.isIRFromSynth())
    {
        irCombo.setSelectedId (1, juce::dontSendNotification);
    }
    else
    {
        int idx = pingProcessor.getSelectedIRIndex();
        if (idx >= 0 && idx < (int) names.size())
            irCombo.setSelectedId (idx + 2, juce::dontSendNotification);
        else if (irCombo.getNumItems() > 1)
            irCombo.setSelectedId (2, juce::sendNotificationSync);  // first file
        loadSelectedIR();
    }
}

void PingEditor::loadSelectedIR()
{
    int idx = irCombo.getSelectedId() - 2;  // id 1=Synth (idx -1), id 2+=file
    if (idx < 0)
    {
        // Synth IR: re-process raw buffer with current reverse/trim/stretch/decay settings
        pingProcessor.reloadSynthIR();
        return;
    }
    auto file = pingProcessor.getIRManager().getIRFileAt (idx);
    if (file.existsAsFile())
        pingProcessor.loadIRFromFile (file);
}

void PingEditor::updateWaveform()
{
    waveformComponent.repaint();
}
