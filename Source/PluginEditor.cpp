#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PingBinaryData.h"

namespace
{
    constexpr int editorW = 1104;
    constexpr int editorH = 786;
    const juce::Colour bgDark      { 0xff141414 };
    const juce::Colour panelBg     { 0xff1e1e1e };
    const juce::Colour panelBorder { 0xff2a2a2a };
    const juce::Colour accent     { 0xff8cd6ef };
    const juce::Colour accentDim   { 0xff5ab0cc };
    const juce::Colour textCol    { 0xffe8e8e8 };
    const juce::Colour textDim    { 0xff909090 };
    const juce::Colour waveFill   { 0x288cd6ef };
    const juce::Colour waveLine   { 0xffd8e8f4 };

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
    setResizable (false, false);
    setOpaque (true);

    // Load brushed-steel background texture from embedded binary data.
    bgTexture = juce::ImageCache::getFromMemory (BinaryData::texture_bg_jpg,
                                                  BinaryData::texture_bg_jpgSize);

    // Pre-process logos: convert to ARGB and zero any pixel whose alpha < 30
    // so near-transparent edge artefacts from the PNG don't show on lighter backgrounds
    {
        auto prepLogo = [] (const void* data, int size) -> juce::Image
        {
            auto raw = juce::ImageCache::getFromMemory (data, size);
            if (! raw.isValid()) return {};
            auto img = raw.convertedToFormat (juce::Image::ARGB);
            for (int y = 0; y < img.getHeight(); ++y)
                for (int x = 0; x < img.getWidth(); ++x)
                    if (img.getPixelAt (x, y).getAlpha() < 30)
                        img.setPixelAt (x, y, juce::Colours::transparentBlack);
            return img;
        };
        spitfireLogoImage = prepLogo (BinaryData::spitfire_logo_png,     BinaryData::spitfire_logo_pngSize);
        pingLogoImage     = prepLogo (BinaryData::ping_logo_blue_png,    BinaryData::ping_logo_blue_pngSize);
    }

    // IR list
    addAndMakeVisible (irCombo);
    irCombo.addListener (this);
    irCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
    irCombo.setColour (juce::ComboBox::textColourId, textDim);
    irCombo.setColour (juce::ComboBox::arrowColourId, accent);

    addAndMakeVisible (irComboLabel);
    irComboLabel.setText ("IR preset", juce::dontSendNotification);
    irComboLabel.setJustificationType (juce::Justification::centredRight);
    irComboLabel.setColour (juce::Label::textColourId, textDim);
    irComboLabel.setFont (juce::FontOptions (11.0f));

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

    addAndMakeVisible (presetLabel);
    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    presetLabel.setColour (juce::Label::textColourId, textDim);
    presetLabel.setFont (juce::FontOptions (14.0f));

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
    makeSlider (erLevelSlider, "ER Level");
    makeSlider (tailLevelSlider, "Tail Level");

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
    // Repaint the editor when either crossfeed toggle changes so the header text colour updates
    erCrossfeedOnButton.onClick  = [this] { repaint(); };
    tailCrossfeedOnButton.onClick = [this] { repaint(); };

    // Plate row knobs (row 3 — same accent style as crossfeed)
    for (auto* s : { &plateDiffusionSlider, &plateColourSlider, &plateSizeSlider, &plateIRFeedSlider })
        makeSlider (*s, "");
    {
        addAndMakeVisible (plateOnButton);
        plateOnButton.setButtonText ("");
        plateOnButton.setComponentID ("PlateSwitch");
        plateOnButton.onClick = [this] { repaint(); };
    }

    // Bloom row knobs (row 4 — same accent style)
    for (auto* s : { &bloomSizeSlider, &bloomFeedbackSlider, &bloomTimeSlider,
                     &bloomIRFeedSlider, &bloomVolumeSlider })
        makeSlider (*s, "");
    {
        addAndMakeVisible (bloomOnButton);
        bloomOnButton.setButtonText ("");
        bloomOnButton.setComponentID ("BloomSwitch");
        bloomOnButton.onClick = [this] { repaint(); };
    }

    // Cloud row knobs (row 5 — same accent style)
    for (auto* s : { &cloudDepthSlider, &cloudRateSlider, &cloudSizeSlider,
                     &cloudIRFeedSlider, &cloudVolumeSlider })
        makeSlider (*s, "");
    {
        addAndMakeVisible (cloudOnButton);
        cloudOnButton.setButtonText ("");
        cloudOnButton.setComponentID ("CloudSwitch");
        cloudOnButton.onClick = [this] { repaint(); };
    }

    // Shimmer row knobs (row 6 — same accent style)
    for (auto* s : { &shimPitchSlider, &shimSizeSlider, &shimColourSlider,
                     &shimIRFeedSlider, &shimVolumeSlider })
        makeSlider (*s, "");
    {
        addAndMakeVisible (shimOnButton);
        shimOnButton.setButtonText ("");
        shimOnButton.setComponentID ("ShimmerSwitch");
        shimOnButton.onClick = [this] { repaint(); };
    }

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
    plateIRFeedSlider.setRange   (0.0,  1.0,  0.01);
    plateDiffusionSlider.setRange (0.30, 0.88, 0.01);
    plateColourSlider.setRange   (0.0,  1.0,  0.01);
    plateSizeSlider.setRange     (0.5,  4.0,  0.01);
    bloomSizeSlider.setRange (0.25, 2.0, 0.01);
    bloomFeedbackSlider.setRange  (0.0,  0.65, 0.01);
    bloomTimeSlider.setRange      (50.0, 500.0, 1.0);
    bloomIRFeedSlider.setRange    (0.0,  1.0,  0.01);
    bloomVolumeSlider.setRange    (0.0,  1.0,  0.01);
    cloudDepthSlider.setRange  (0.0,  1.0,  0.01);   // WIDTH
    cloudRateSlider.setRange   (0.1,  4.0,  0.01);   // DENSITY
    cloudSizeSlider.setRange   (0.25, 4.0,  0.01);   // SIZE multiplier
    cloudIRFeedSlider.setRange (0.0,  0.7,  0.01);   // FEEDBACK
    cloudVolumeSlider.setRange (0.0,  1.0,  0.01);   // IR FEED
    shimPitchSlider.setRange   (-24.0, 24.0, 1.0);
    shimSizeSlider.setRange    (50.0, 500.0,  1.0);
    shimColourSlider.setRange  (0.0, 1000.0,  1.0);   // DELAY
    shimIRFeedSlider.setRange  (0.0,   1.0,  0.01);
    shimVolumeSlider.setRange  (0.0,   0.7,  0.01);  // FEEDBACK

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
    plateIRFeedAttach    = std::make_unique<SliderAttachment> (apvts, "plateIRFeed",    plateIRFeedSlider);
    plateDiffusionAttach = std::make_unique<SliderAttachment> (apvts, "plateDiffusion", plateDiffusionSlider);
    plateColourAttach    = std::make_unique<SliderAttachment> (apvts, "plateColour",    plateColourSlider);
    plateSizeAttach      = std::make_unique<SliderAttachment> (apvts, "plateSize",      plateSizeSlider);
    plateOnAttach        = std::make_unique<ButtonAttachment> (apvts, "plateOn",        plateOnButton);
    bloomSizeAttach = std::make_unique<SliderAttachment> (apvts, "bloomSize", bloomSizeSlider);
    bloomFeedbackAttach  = std::make_unique<SliderAttachment> (apvts, "bloomFeedback",  bloomFeedbackSlider);
    bloomTimeAttach      = std::make_unique<SliderAttachment> (apvts, "bloomTime",      bloomTimeSlider);
    bloomIRFeedAttach    = std::make_unique<SliderAttachment> (apvts, "bloomIRFeed",    bloomIRFeedSlider);
    bloomVolumeAttach    = std::make_unique<SliderAttachment> (apvts, "bloomVolume",    bloomVolumeSlider);
    bloomOnAttach        = std::make_unique<ButtonAttachment> (apvts, "bloomOn",        bloomOnButton);
    cloudDepthAttach  = std::make_unique<SliderAttachment> (apvts, "cloudDepth",  cloudDepthSlider);
    cloudRateAttach   = std::make_unique<SliderAttachment> (apvts, "cloudRate",   cloudRateSlider);
    cloudSizeAttach   = std::make_unique<SliderAttachment> (apvts, "cloudSize",   cloudSizeSlider);
    cloudIRFeedAttach = std::make_unique<SliderAttachment> (apvts, "cloudFeedback", cloudIRFeedSlider);
    cloudVolumeAttach = std::make_unique<SliderAttachment> (apvts, "cloudIRFeed", cloudVolumeSlider);
    cloudOnAttach     = std::make_unique<ButtonAttachment> (apvts, "cloudOn",     cloudOnButton);
    shimPitchAttach   = std::make_unique<SliderAttachment> (apvts, "shimPitch",   shimPitchSlider);
    shimSizeAttach    = std::make_unique<SliderAttachment> (apvts, "shimSize",    shimSizeSlider);
    shimColourAttach  = std::make_unique<SliderAttachment> (apvts, "shimDelay",   shimColourSlider);
    shimIRFeedAttach  = std::make_unique<SliderAttachment> (apvts, "shimIRFeed",  shimIRFeedSlider);
    shimVolumeAttach  = std::make_unique<SliderAttachment> (apvts, "shimFeedback", shimVolumeSlider);
    shimOnAttach      = std::make_unique<ButtonAttachment> (apvts, "shimOn",      shimOnButton);

    setLookAndFeel (&pingLook);

    // Labels
    for (auto* label : { &dryWetLabel, &predelayLabel, &decayLabel, &modDepthLabel,
                        &stretchLabel, &widthLabel, &modRateLabel,
                        &tailModLabel, &delayDepthLabel, &tailRateLabel,
                        &irInputGainLabel, &irInputDriveLabel, &outputGainLabel,
                        &erLevelLabel, &tailLevelLabel,
                        &erCrossfeedDelayLabel, &erCrossfeedAttLabel,
                        &tailCrossfeedDelayLabel, &tailCrossfeedAttLabel,
                        &plateDiffusionLabel, &plateColourLabel, &plateSizeLabel, &plateIRFeedLabel,
                        &bloomSizeLabel, &bloomFeedbackLabel, &bloomTimeLabel,
                        &bloomIRFeedLabel, &bloomVolumeLabel,
                        &cloudDepthLabel, &cloudRateLabel, &cloudSizeLabel,
                        &cloudIRFeedLabel, &cloudVolumeLabel,
                        &shimPitchLabel, &shimSizeLabel, &shimColourLabel,
                        &shimIRFeedLabel, &shimVolumeLabel })
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
    outputGainLabel.setText ("Wet Out trim", juce::dontSendNotification);
    irInputDriveLabel.setText ("DRIVE", juce::dontSendNotification);
    erLevelLabel.setText ("ER", juce::dontSendNotification);
    tailLevelLabel.setText ("TAIL", juce::dontSendNotification);
    erCrossfeedDelayLabel.setText   ("DELAY",   juce::dontSendNotification);
    erCrossfeedAttLabel.setText     ("ATT",     juce::dontSendNotification);
    tailCrossfeedDelayLabel.setText ("DELAY",   juce::dontSendNotification);
    tailCrossfeedAttLabel.setText   ("ATT",     juce::dontSendNotification);
    plateIRFeedLabel.setText    ("IR FEED",   juce::dontSendNotification);
    plateDiffusionLabel.setText ("DIFFUSION", juce::dontSendNotification);
    plateColourLabel.setText    ("COLOUR",    juce::dontSendNotification);
    plateSizeLabel.setText      ("SIZE",      juce::dontSendNotification);
    bloomSizeLabel.setText ("SIZE", juce::dontSendNotification);
    bloomFeedbackLabel.setText  ("FEEDBACK",  juce::dontSendNotification);
    bloomTimeLabel.setText      ("TIME",      juce::dontSendNotification);
    bloomIRFeedLabel.setText    ("IR FEED",   juce::dontSendNotification);
    bloomVolumeLabel.setText    ("VOLUME",    juce::dontSendNotification);
    cloudDepthLabel.setText  ("WIDTH",    juce::dontSendNotification);
    cloudRateLabel.setText   ("DENSITY",  juce::dontSendNotification);
    cloudSizeLabel.setText   ("LENGTH",   juce::dontSendNotification);
    cloudIRFeedLabel.setText ("FEEDBACK", juce::dontSendNotification);
    cloudVolumeLabel.setText ("IR FEED",  juce::dontSendNotification);
    shimPitchLabel.setText   ("PITCH",   juce::dontSendNotification);
    shimSizeLabel.setText    ("LENGTH",  juce::dontSendNotification);
    shimColourLabel.setText  ("DELAY",   juce::dontSendNotification);
    shimIRFeedLabel.setText  ("IR FEED", juce::dontSendNotification);
    shimVolumeLabel.setText  ("FEEDBACK", juce::dontSendNotification);

    for (auto* r : { &dryWetReadout, &predelayReadout, &decayReadout, &modDepthReadout,
                     &stretchReadout, &widthReadout, &modRateReadout,
                     &tailModReadout, &delayDepthReadout, &tailRateReadout,
                     &irInputGainReadout, &irInputDriveReadout, &outputGainReadout,
                     &erLevelReadout, &tailLevelReadout,
                     &erCrossfeedDelayReadout, &erCrossfeedAttReadout,
                     &tailCrossfeedDelayReadout, &tailCrossfeedAttReadout,
                     &plateDiffusionReadout, &plateColourReadout, &plateSizeReadout, &plateIRFeedReadout,
                     &bloomSizeReadout, &bloomFeedbackReadout, &bloomTimeReadout,
                     &bloomIRFeedReadout, &bloomVolumeReadout,
                     &cloudDepthReadout, &cloudRateReadout, &cloudSizeReadout,
                     &cloudIRFeedReadout, &cloudVolumeReadout,
                     &shimPitchReadout, &shimSizeReadout, &shimColourReadout,
                     &shimIRFeedReadout, &shimVolumeReadout })
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
    versionLabel.setJustificationType (juce::Justification::centredRight);
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
    // ── Full-UI background: real brushed-steel texture image ─────────────────
    // Draw the texture scaled to fill the entire editor, then lay a dark
    // semi-transparent overlay over the main body so controls remain readable,
    // leaving the header bar showing the raw texture most prominently.
    if (bgTexture.isValid())
    {
        g.drawImage (bgTexture, getLocalBounds().toFloat(),
                     juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.fillAll (bgDark);
    }

    // Dark overlay on everything below the header bar
    if (headerPanelRect.getWidth() > 0)
    {
        const int bodyY = headerPanelRect.getBottom();
        g.setColour (juce::Colour (0xd8141414));   // ~85% opaque dark — keeps texture hint
        g.fillRect (0, bodyY, getWidth(), getHeight() - bodyY);

        // Subtle darkening on the header itself so logos/text stay legible
        g.setColour (juce::Colour (0x60080a10));
        g.fillRect (headerPanelRect);

        // Bottom separator
        g.setColour (juce::Colour (0xff0a0c12));
        g.fillRect (0, headerPanelRect.getBottom() - 1, getWidth(), 1);
    }
    else
    {
        g.setColour (bgDark.withAlpha (static_cast<juce::uint8> (0xd8)));
        g.fillRect (getLocalBounds());
    }

    auto wfBounds = waveformComponent.getBounds();
    const float corner = 6.0f;
    g.setColour (panelBg);
    g.fillRoundedRectangle (wfBounds.toFloat(), corner);
    g.setColour (panelBorder);
    g.drawRoundedRectangle (wfBounds.toFloat().reduced (0.5f), corner, 0.8f);

    if (spitfireBounds.getWidth() > 0 && spitfireLogoImage.isValid())
        g.drawImageWithin (spitfireLogoImage,
                           spitfireBounds.getX(), spitfireBounds.getY(),
                           spitfireBounds.getWidth(), spitfireBounds.getHeight(),
                           juce::RectanglePlacement::centred);
    if (pingBounds.getWidth() > 0 && pingLogoImage.isValid())
        g.drawImageWithin (pingLogoImage,
                           pingBounds.getX(), pingBounds.getY(),
                           pingBounds.getWidth(), pingBounds.getHeight(),
                           juce::RectanglePlacement::centred);

    // ── Raised bevel panel: left Rows 1+2 (IR Input / IR Controls / ER & Tail Crossfade) ────────
    // True split-edge emboss: top+left edges drawn in a separate Path from bottom+right edges,
    // each stroked with its own colour.  No fill — background texture is fully preserved.
    // Top-left corner belongs to the highlight path; bottom-right corner to the shadow path.
    if (irInputGroupBounds.getWidth() > 0)
    {
        const float padH = 14.f, padV = 12.f;
        const auto panel = juce::Rectangle<float> (
            (float) irInputGainSlider.getX()           - padH,
            (float) irInputGroupBounds.getY()          - padV,
            (float) stretchSlider.getRight()            + padH - ((float) irInputGainSlider.getX() - padH),
            (float) tailCrossfeedAttReadout.getBottom() + padV - ((float) irInputGroupBounds.getY() - padV)
        );

        // Use addRoundedRectangle for a geometrically perfect outline, then draw it twice
        // with diagonal clip regions so the top+left half is highlight and bottom+right is shadow.
        // This avoids all manual arc calculations and curved-join slope artefacts.
        const float lw  = 3.0f;
        const float hlw = lw * 0.5f;
        const float cr  = 9.0f;

        juce::Path outline;
        outline.addRoundedRectangle (panel.reduced (hlw), cr - hlw);

        // Highlight — top and left edges: clip to the upper-left triangle
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getX(),     panel.getY(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33ffffff));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }

        // Shadow — bottom and right edges: clip to the lower-right triangle
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getRight(), panel.getBottom(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33000000));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }
    }

    // ── Raised bevel panel: left Rows 3+4 (Plate pre-diffuser / Bloom hybrid) ──────────────────
    if (plateGroupBounds.getWidth() > 0)
    {
        const float padH = 14.f, padV = 12.f;
        const auto panel = juce::Rectangle<float> (
            (float) plateDiffusionSlider.getX()      - padH,
            (float) plateGroupBounds.getY()          - padV,
            (float) stretchSlider.getRight()           + padH - ((float) plateDiffusionSlider.getX() - padH),
            (float) bloomVolumeReadout.getBottom()   + padV - ((float) plateGroupBounds.getY() - padV)
        );

        const float lw  = 3.0f;
        const float hlw = lw * 0.5f;
        const float cr  = 9.0f;

        juce::Path outline;
        outline.addRoundedRectangle (panel.reduced (hlw), cr - hlw);

        // Highlight — top and left edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getX(),     panel.getY(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33ffffff));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }

        // Shadow — bottom and right edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getRight(), panel.getBottom(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33000000));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }
    }

    // ── Raised bevel panel: right Rows R1+R2 (Clouds / Shimmer) ─────────────────────────────────
    if (cloudGroupBounds.getWidth() > 0)
    {
        const float padH = 14.f, padV = 12.f;
        const auto panel = juce::Rectangle<float> (
            (float) modDepthSlider.getX()            - padH,
            (float) cloudGroupBounds.getY()         - padV,
            (float) cloudVolumeSlider.getRight()     + padH - ((float) cloudDepthSlider.getX() - padH),
            (float) shimVolumeReadout.getBottom()   + padV - ((float) cloudGroupBounds.getY() - padV)
        );

        const float lw  = 3.0f;
        const float hlw = lw * 0.5f;
        const float cr  = 9.0f;

        juce::Path outline;
        outline.addRoundedRectangle (panel.reduced (hlw), cr - hlw);

        // Highlight — top and left edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getX(),     panel.getY(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33ffffff));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }

        // Shadow — bottom and right edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getRight(), panel.getBottom(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33000000));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }
    }

    // ── Raised bevel panel: right Row R3 (Tail AM mod / Tail Frq mod) ───────────────────────────
    if (tailAMModGroupBounds.getWidth() > 0)
    {
        const float padH = 14.f, padV = 12.f;
        const auto panel = juce::Rectangle<float> (
            (float) modDepthSlider.getX()            - padH,
            (float) tailAMModGroupBounds.getY()      - padV,
            (float) tailRateSlider.getRight()         + padH - ((float) modDepthSlider.getX() - padH),
            (float) tailRateReadout.getBottom()      + padV - ((float) tailAMModGroupBounds.getY() - padV)
        );

        const float lw  = 3.0f;
        const float hlw = lw * 0.5f;
        const float cr  = 9.0f;

        juce::Path outline;
        outline.addRoundedRectangle (panel.reduced (hlw), cr - hlw);

        // Highlight — top and left edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getX(),     panel.getY(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33ffffff));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }

        // Shadow — bottom and right edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getRight(), panel.getBottom(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33000000));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }
    }

    // ── Raised bevel panel: EQ control panel ────────────────────────────────────────────────────
    if (eqGraph.getWidth() > 0)
    {
        const float padH = 6.f, padV = 4.f;
        const auto eb = eqGraph.getBounds().toFloat();
        const float panelTop = eb.getY() + (float) eqGraph.bandLabelTopY - padV;
        const auto panel = juce::Rectangle<float> (eb.getX()    - padH,
                                                   panelTop,
                                                   eb.getWidth() + padH * 2.f,
                                                   eb.getBottom() + padV - panelTop);

        const float lw  = 3.0f;
        const float hlw = lw * 0.5f;
        const float cr  = 9.0f;

        juce::Path outline;
        outline.addRoundedRectangle (panel.reduced (hlw), cr - hlw);

        // Highlight — top and left edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getX(),     panel.getY(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33ffffff));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }

        // Shadow — bottom and right edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getRight(), panel.getBottom(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33000000));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }
    }

    // ── Raised bevel panel: central column (ER/Tail/DryWet/WetOut knobs → IR combo → waveform) ──
    if (waveformComponent.getWidth() > 0)
    {
        const float padH = 14.f, padV = 12.f;
        const auto panel = juce::Rectangle<float> (
            (float) erLevelReadout.getX()          - padH,
            (float) erLevelSlider.getY()            - padV,
            (float) outputGainReadout.getRight()    + padH - ((float) erLevelReadout.getX() - padH),
            (float) waveformComponent.getBottom()   + padV - ((float) erLevelSlider.getY() - padV)
        );

        const float lw  = 3.0f;
        const float hlw = lw * 0.5f;
        const float cr  = 9.0f;

        juce::Path outline;
        outline.addRoundedRectangle (panel.reduced (hlw), cr - hlw);

        // Highlight — top and left edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getX(),     panel.getY(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33ffffff));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }

        // Shadow — bottom and right edges
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clipTri;
            clipTri.addTriangle (panel.getRight(), panel.getBottom(),
                                 panel.getRight(), panel.getY(),
                                 panel.getX(),     panel.getBottom());
            g.reduceClipRegion (clipTri);
            g.setColour (juce::Colour (0x33000000));
            g.strokePath (outline, juce::PathStrokeType (lw));
        }
    }

    // Group headers: label text above a horizontal line; active sections glow orange
    g.setFont (juce::FontOptions (10.0f));
    auto drawGroupHeader = [&](const juce::Rectangle<int>& bounds, const juce::String& text, bool active)
    {
        if (bounds.getWidth() <= 0) return;
        const float lineY = (float) bounds.getBottom() - 1.0f;
        if (active)
        {
            // Soft glow: draw text offset in cardinal directions at low alpha
            g.setColour (accent.withAlpha (0.22f));
            const int gOff[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
            for (auto& o : gOff)
                g.drawText (text, bounds.getX() + o[0], bounds.getY() + o[1],
                            bounds.getWidth(), bounds.getHeight() - 3,
                            juce::Justification::centredLeft, false);
            g.setColour (accent);
        }
        else
        {
            g.setColour (textDim);
        }
        g.drawText (text, bounds.getX(), bounds.getY(),
                    bounds.getWidth(), bounds.getHeight() - 3,
                    juce::Justification::centredLeft, false);
        g.drawLine ((float) bounds.getX(), lineY, (float) bounds.getRight(), lineY, 1.0f);
    };
    drawGroupHeader (irInputGroupBounds,    "IR Input",       false);
    drawGroupHeader (irControlsGroupBounds, "IR Controls",    false);
    drawGroupHeader (erCrossfadeGroupBounds,   "ER Crossfade",   erCrossfeedOnButton.getToggleState());
    drawGroupHeader (tailCrossfadeGroupBounds, "Tail Crossfade", tailCrossfeedOnButton.getToggleState());
    drawGroupHeader (plateGroupBounds,         "Plate pre-diffuser", plateOnButton.getToggleState());
    drawGroupHeader (bloomGroupBounds,         "Bloom hybrid",       bloomOnButton.getToggleState());
    drawGroupHeader (cloudGroupBounds,         "Clouds post convolution",    cloudOnButton.getToggleState());
    drawGroupHeader (shimGroupBounds,          "Shimmer",                    shimOnButton.getToggleState());
    drawGroupHeader (tailAMModGroupBounds,     "Tail AM mod",                false);
    drawGroupHeader (tailFrqModGroupBounds,    "Tail Frq mod",               false);

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
    const int dryWetKnobSize = juce::roundToInt (bigKnobSize * 1.05f);  // 70% of original (3/2 × bigKnobSize)
    const int eqMinH = 153;  // matches the level meter height (meterH = 153)
    const int eqHeight = 153;  // fixed to match level meter — eqTotalH = 153 + h/6 gives enough room for the 113 px knob strip
    const int gapV = (int) (0.01f * ch) + (int) (0.008f * ch);
    const int marginV = juce::jmin (12, ch / 38);
    const int availableForMainAndEq = ch - 2 * marginV - topRowH - gapV;
    const int erTailRowH = 0;  // ER/Tail are now rotary knobs in the DRY/WET column, not a separate row
    const int mainHClamped = juce::jmax (3 * sixRowH, availableForMainAndEq - eqHeight - erTailRowH);
    const int mainHeight = juce::jmin (mainHClamped, availableForMainAndEq - eqHeight - erTailRowH - 6);

    // —— Header row: full-width brushed steel panel ——————————————————————————
    auto topRow = b.removeFromTop (topRowH);

    // Store the full-width header rect for paint() — spans x=0, full window width
    headerPanelRect = juce::Rectangle<int> (0, 0, w, topRow.getBottom());

    // Spitfire logo: derive height from header, compute correct width from 474:62 aspect ratio
    {
        const int logoH = juce::jmax (18, (int) (topRowH * 0.48f));  // ~48% of header height
        const int logoW = (int) (logoH * 474.f / 62.f);              // correct width, no arbitrary cap
        spitfireBounds = juce::Rectangle<int> (14, topRow.getY() + (topRowH - logoH) / 2 - 4, logoW, logoH);
    }

    // P!NG logo: centred horizontally, fills the header height (578×182 → ratio 3.176:1)
    {
        const int logoH = topRowH - 6;
        const int logoW = (int) (logoH * 578.f / 182.f);
        pingBounds = juce::Rectangle<int> (w / 2 - logoW / 2,
                                           topRow.getY() + (topRowH - logoH) / 2 - 4,
                                           logoW, logoH);
    }

    // Preset combo + Save button: right-aligned in header
    const int saveButtonW = 48;
    const int presetComboW = 200;
    {
        const int pComboH = 24;
        const int pTopY   = topRow.getY() + (topRowH - pComboH) / 2;
        savePresetButton.setBounds (w - 12 - saveButtonW,                        pTopY, saveButtonW,  pComboH);
        presetCombo.setBounds      (savePresetButton.getX() - 6 - presetComboW,  pTopY, presetComboW, pComboH);
        const int pLabelW = 44;
        presetLabel.setBounds      (presetCombo.getX() - pLabelW - 4,            pTopY, pLabelW,      pComboH);
    }

    const int presetCenterX = presetCombo.getX() + presetCombo.getWidth() / 2;
    juce::ignoreUnused (presetCenterX);

    // Top-bar meter strip (thin horizontal, between spitfire and ping logo)
    const int meterGap = 6;
    const int meterH = 5;
    int meterX = spitfireBounds.getRight() + meterGap;
    int meterW = juce::jmax (0, pingBounds.getX() - meterX - meterGap);
    int meterY = spitfireBounds.getY() + (spitfireBounds.getHeight() - meterH) / 2;
    outputLevelMeter.setBounds (meterX, meterY, meterW, meterH);

    b.removeFromTop ((int) (0.01f * ch));

    // —— Main block: knobs (left), Dry/Wet + IR combo (centre), waveform below IR combo ——
    auto mainArea = b.removeFromTop (mainHeight);

    // Phantom dimensions used only to anchor cy so the DRY/WET knob stays at the same
    // visual position as it had when the waveform lived in the right column.
    const int phantomWavePanelW = juce::jmax (220, (int) (0.36f * cw));
    const int phantomWavePanelH = juce::jmax (72,  (int) (phantomWavePanelW * 0.36f));
    const int phantomReverseH   = juce::jmin (26,  mainHeight / 4);

    // Waveform panel: width = 300 (matches meter), height = meter bar region only.
    // Bar geometry from OutputLevelMeter.h:
    //   totalBarsH  = 4*(2*barH+rowGap)+3*groupGap = 4*(18+2)+3*8 = 104 px
    //   contentH    = totalBarsH + 4 + scaleH = 122 px
    //   barOffset   = (153 - 122) / 2 = 15 px  ← gap from meter panel top to first bar
    static const int kMeterBarsH     = 104;  // INPUT L top → TAIL R bottom
    static const int kMeterBarOffset =  15;  // px from meter panel top to first bar top
    const int wavePanelW = 300;
    const int wavePanelH = kMeterBarsH;

    int irComboH = 24;
    int irComboW = juce::jmin ((int) (0.24f * cw), bigKnobSize + 40);
    // cy anchors the DRY/WET knob at the same visual height as before (where the waveform
    // centre used to be when the waveform lived in the right column).
    const int phantomWaveCentreY = mainArea.getY() + phantomReverseH + phantomWavePanelH / 2;
    int cy = phantomWaveCentreY - (dryWetKnobSize + labelH + readoutH + 4 + irComboH + 6) / 2 + 40;
    const int smallKnobSize = sixKnobSize / 2;
    const int dryWetCenterY = cy + dryWetKnobSize / 2;
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

    const int irSynthW = 64;
    const int irGap = 6;
    int irRowY = cy + 10 + dryWetKnobSize + 2 + readoutH + 6 + labelH;  // just below the DRY/WET readout

    // True vertical centre of the DRY/WET knob (placed at cy+10, so centre is +10+size/2)
    const int dryWetTrueCentreY = cy + 10 + dryWetKnobSize / 2;

    // Wet Output, ER, Tail, Width: all rowKnobSize.
    // ER (left of DRY/WET) and Tail (left, below ER) are shifted down 5 px from the centred position.
    // WET OUTPUT (right of DRY/WET) aligns its top with the ER knob top.
    // WIDTH (right of DRY/WET, further right) aligns its top with the Tail knob top.
    const int outputGainKnobSize = (int)(sixKnobSize * 0.6f);  // = rowKnobSize
    const int erTailCenterX  = w / 2 - (irComboW / 2 + 4 + saveButtonW / 2);
    const int outputGainCenterX  = w / 2 + irComboW / 2 + 4 + saveButtonW / 2;
    const int erTailSlotH    = outputGainKnobSize + labelH + readoutH + 4;
    const int erKnobY        = dryWetTrueCentreY - erTailSlotH - 2 + 5;
    const int tailKnobY      = dryWetTrueCentreY + 2 + 5;

    // ER level — left of DRY/WET, upper position
    erLevelSlider.setBounds  (erTailCenterX - outputGainKnobSize / 2, erKnobY, outputGainKnobSize, outputGainKnobSize);
    erLevelLabel.setBounds   (erTailCenterX - irLabelW / 2, erLevelSlider.getBottom() + 2,    irLabelW, labelH);
    erLevelReadout.setBounds (erTailCenterX - irLabelW / 2, erLevelLabel.getBottom(),          irLabelW, readoutH);

    // Tail level — left of DRY/WET, lower position
    tailLevelSlider.setBounds(erTailCenterX - outputGainKnobSize / 2, tailKnobY, outputGainKnobSize, outputGainKnobSize);
    tailLevelLabel.setBounds (erTailCenterX - irLabelW / 2, tailLevelSlider.getBottom() + 2,  irLabelW, labelH);
    tailLevelReadout.setBounds(erTailCenterX - irLabelW / 2, tailLevelLabel.getBottom(),       irLabelW, readoutH);

    // WET OUTPUT — right of DRY/WET, top aligned with ER knob
    outputGainSlider.setBounds (outputGainCenterX - outputGainKnobSize / 2,
                                erKnobY,
                                outputGainKnobSize, outputGainKnobSize);
    outputGainLabel.setBounds  (outputGainCenterX - irLabelW / 2, outputGainSlider.getBottom() + 2,  irLabelW, labelH);
    outputGainReadout.setBounds(outputGainCenterX - irLabelW / 2, outputGainSlider.getBottom() + labelH + 2, irLabelW, readoutH);

    // WIDTH — right of DRY/WET, same X centre as WET OUTPUT, stacked below it at Tail knob Y

    if (! irSynthComponent.isVisible())
        dryWetSlider.toFront (false);

    // —— Row of 5 controls: Input Gain | IR Input Drive | Predelay | Damping | Stretch ——
    const int rowKnobSize  = (int)(sixKnobSize * 0.6f);
    const int rowLabelW    = juce::jmax (64, rowKnobSize + 14);
    const int rowGap       = juce::jmax (6,  (int)(0.008f * cw));
    const int groupLabelH  = 14;   // height reserved for "IR Input" text + line above knobs 0-1
    const int rowTotalH    = rowKnobSize + labelH + readoutH + 6;
    // Hoist row 2/3/4/5 heights here so they can be used to compute absolute Y anchors below
    const int row2TotalH_  = groupLabelH + rowKnobSize + labelH + readoutH + 6;
    const int row3TotalH_  = row2TotalH_;   // identical formula
    const int row4TotalH_  = row2TotalH_;   // identical formula
    const int row5TotalH_  = row2TotalH_;   // identical formula
    const int row6TotalH_  = row2TotalH_;   // identical formula
    auto topKnobRow = mainArea.removeFromTop (rowTotalH);
    const int rowShiftUp = 30 - rowKnobSize;   // negative = rows shifted down by one knob height relative to original
    const int rowY       = topKnobRow.getY() + groupLabelH - 10 - rowShiftUp;

    // Anchor rows 2 and 3 from topKnobRow.getBottom() rather than from mainArea.getY()
    // after the removeFromTop calls.  When mainHeight < sum of all row heights, JUCE's
    // removeFromTop clamps to the remaining rect height, causing subsequent rows to be
    // placed far too high (they pile up inside row 2 / row 1).  Using absolute offsets
    // from topKnobRow.getBottom() guarantees correct spacing regardless of mainHeight.
    const int row2AbsY = topKnobRow.getBottom() + 4;    // +4 so Row1→Row2 knob spacing matches Row3→Row4 (Plate→Bloom reference)
    const int row3AbsY = row2AbsY + row2TotalH_ + 39;  // 14 base + 25 extra down — separates IR+Crossfade from Plate+Bloom (left) and Cloud+Shimmer from Tail (right)
    const int row4AbsY = row3AbsY + row3TotalH_;
    const int row5AbsY = row4AbsY + row4TotalH_;
    const int row6AbsY = row5AbsY + row5TotalH_;
    const int rowStep    = rowKnobSize + rowGap;
    const int rowStartX  = juce::jmax (8, w / 128) + 10;    // 10 px right of the window edge margin (5 px inset from edge)

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
        topKnobRow.getY() - 10 - rowShiftUp,
        irInputDriveSlider.getRight() - irInputGainSlider.getX(),
        groupLabelH);
    irControlsGroupBounds = juce::Rectangle<int> (
        predelaySlider.getX(),
        topKnobRow.getY() - 10 - rowShiftUp,
        stretchSlider.getRight() - predelaySlider.getX(),
        groupLabelH);

    // Place DRY/WET knob horizontally centred in the window, vertically at cy + 10.
    {
        const int dryWetCenterX = w / 2;
        dryWetSlider.setBounds  (dryWetCenterX - dryWetKnobSize / 2, cy + 10, dryWetKnobSize, dryWetKnobSize);
        dryWetLabel.setBounds   (0, 0, 0, 0);
        dryWetReadout.setBounds (dryWetSlider.getX(), dryWetSlider.getBottom() + 2, dryWetKnobSize, readoutH);
    }

    // Place IR combo + IR Synth button centred under the DRY/WET knob,
    // then waveform + reverse button centred below the combo.
    {
        const int irComboXu = dryWetSlider.getBounds().getCentreX() - irComboW / 2;
        const int irSynthXu = irComboXu + irComboW + irGap;
        irCombo.setBounds       (irComboXu, irRowY, irComboW, irComboH);
        irSynthButton.setBounds (irSynthXu, irRowY, irSynthW, irComboH);
        // "IR preset" label to the left of the combo, matching the Preset label style
        const int irCLabelW = 52;
        irComboLabel.setBounds (irComboXu - irCLabelW - 4, irRowY, irCLabelW, irComboH);

        // Waveform: centred under the IR preset combo (at w/2), reverse button 10 px below the combo.
        {
            const int revBtnW   = juce::jmax (68, (int)(0.075f * cw));
            const int revBtnH   = 22;
            const int revBtnY   = irCombo.getBottom() + 10;
            const int waveformY = revBtnY + revBtnH + 2;
            const int waveCentreX = dryWetSlider.getBounds().getCentreX();   // = w/2
            reverseButton.setBounds (waveCentreX - wavePanelW / 2,
                                     revBtnY, revBtnW, revBtnH);
            waveformComponent.setBounds (waveCentreX - wavePanelW / 2, waveformY,
                                         wavePanelW, wavePanelH);
        }
    }

    // Preset combo and save button are positioned in the top row (right-aligned); no repositioning needed here.

    // —— Row 2: ER Crossfade (delay, att) | Tail Crossfade (delay, att) ——
    // Toggles live in the group header line (LED-style, right-aligned); no extra height needed.
    const int row2TotalH = groupLabelH + rowKnobSize + labelH + readoutH + 6;
    auto row2Area = mainArea.removeFromTop (row2TotalH);
    const int row2KnobY = row2AbsY + groupLabelH - rowShiftUp;

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

    // Store group header bounds for painting
    erCrossfadeGroupBounds = juce::Rectangle<int> (
        erCrossfeedDelaySlider.getX(),
        row2AbsY - rowShiftUp,
        erCrossfeedAttSlider.getRight() - erCrossfeedDelaySlider.getX(),
        groupLabelH);
    tailCrossfadeGroupBounds = juce::Rectangle<int> (
        tailCrossfeedDelaySlider.getX(),
        row2AbsY - rowShiftUp,
        tailCrossfeedAttSlider.getRight() - tailCrossfeedDelaySlider.getX(),
        groupLabelH);

    // Toggles: power-button icon, right-aligned in their group header line (square, same as other rows)
    {
        const int ledH = groupLabelH;        // full header height for power-button icon
        const int ledW = ledH;               // square
        const int ledY = row2AbsY - rowShiftUp + (groupLabelH - ledH) / 2 - 1;
        erCrossfeedOnButton.setBounds   (erCrossfadeGroupBounds.getRight()   - ledW, ledY, ledW, ledH);
        tailCrossfeedOnButton.setBounds (tailCrossfadeGroupBounds.getRight() - ledW, ledY, ledW, ledH);
    }

    // —— Row 3: Plate pre-diffuser (diffusion, colour, size, IR feed) + on/off toggle ——
    // Single group spanning all four knobs; no extra inter-group gap.
    const int row3TotalH = groupLabelH + rowKnobSize + labelH + readoutH + 6;
    auto row3Area = mainArea.removeFromTop (row3TotalH);
    const int row3KnobY = row3AbsY + groupLabelH - rowShiftUp;

    auto placeRow3Knob = [&](juce::Slider& s, juce::Label& lbl, juce::Label& rdout, int idx)
    {
        // No extra gap — all four knobs belong to the single "Plate pre-diffuser" group
        const int cx = rowStartX + rowKnobSize / 2 + idx * rowStep;
        s.setBounds    (cx - rowKnobSize / 2, row3KnobY,                   rowKnobSize, rowKnobSize);
        lbl.setBounds  (cx - rowLabelW / 2,   s.getBottom() + 2,           rowLabelW,   labelH);
        rdout.setBounds(cx - rowLabelW / 2,   s.getBottom() + labelH + 2,  rowLabelW,   readoutH);
    };
    placeRow3Knob (plateDiffusionSlider, plateDiffusionLabel, plateDiffusionReadout, 0);
    placeRow3Knob (plateColourSlider,    plateColourLabel,    plateColourReadout,    1);
    placeRow3Knob (plateSizeSlider,      plateSizeLabel,      plateSizeReadout,      2);
    placeRow3Knob (plateIRFeedSlider,    plateIRFeedLabel,    plateIRFeedReadout,    3);

    // Group header spans all four knobs; toggle pill right-aligned within it
    plateGroupBounds = juce::Rectangle<int> (
        plateDiffusionSlider.getX(),
        row3AbsY - rowShiftUp,
        plateIRFeedSlider.getRight() - plateDiffusionSlider.getX(),
        groupLabelH);
    {
        const int ledH = groupLabelH;   // full header height for power-button icon
        const int ledW = ledH;           // square
        const int ledY = row3AbsY - rowShiftUp + (groupLabelH - ledH) / 2 - 1;
        plateOnButton.setBounds (plateGroupBounds.getRight() - ledW, ledY, ledW, ledH);
    }

    // —— Row 4: Bloom hybrid (diffusion, feedback, time, IR feed, volume) + on/off toggle ——
    // Single group spanning all five knobs; no extra inter-group gap.
    const int row4TotalH = groupLabelH + rowKnobSize + labelH + readoutH + 6;
    auto row4Area = mainArea.removeFromTop (row4TotalH);
    (void) row4Area;  // used only to advance mainArea; actual bounds computed from absolute anchor
    const int row4KnobY = row4AbsY + groupLabelH - rowShiftUp;

    auto placeRow4Knob = [&](juce::Slider& s, juce::Label& lbl, juce::Label& rdout, int idx)
    {
        const int cx = rowStartX + rowKnobSize / 2 + idx * rowStep;
        s.setBounds    (cx - rowKnobSize / 2, row4KnobY,                   rowKnobSize, rowKnobSize);
        lbl.setBounds  (cx - rowLabelW / 2,   s.getBottom() + 2,           rowLabelW,   labelH);
        rdout.setBounds(cx - rowLabelW / 2,   s.getBottom() + labelH + 2,  rowLabelW,   readoutH);
    };
    placeRow4Knob (bloomSizeSlider,     bloomSizeLabel,     bloomSizeReadout,     0);
    placeRow4Knob (bloomFeedbackSlider, bloomFeedbackLabel, bloomFeedbackReadout, 1);
    placeRow4Knob (bloomTimeSlider,     bloomTimeLabel,     bloomTimeReadout,     2);
    placeRow4Knob (bloomIRFeedSlider,   bloomIRFeedLabel,   bloomIRFeedReadout,   3);
    placeRow4Knob (bloomVolumeSlider,   bloomVolumeLabel,   bloomVolumeReadout,   4);

    // Group header spans all five knobs; toggle pill right-aligned within it
    bloomGroupBounds = juce::Rectangle<int> (
        bloomSizeSlider.getX(),
        row4AbsY - rowShiftUp,
        bloomVolumeSlider.getRight() - bloomSizeSlider.getX(),
        groupLabelH);
    {
        const int ledH = groupLabelH;   // full header height for power-button icon
        const int ledW = ledH;           // square
        const int ledY = row4AbsY - rowShiftUp + (groupLabelH - ledH) / 2 - 1;
        bloomOnButton.setBounds (bloomGroupBounds.getRight() - ledW, ledY, ledW, ledH);
    }

    // —— Rows 5 & 6: Cloud and Shimmer — right side, right-justified ——
    // Cloud aligns vertically with Row 1 (IR Input / Controls); rowY is the knob Y for Row 1.
    // Shimmer aligns vertically with Row 2 (ER / Tail Crossfade); row2KnobY is that knob Y.
    // Rightmost knob's right edge aligns with b.getRight().
    const int row5TotalH = groupLabelH + rowKnobSize + labelH + readoutH + 6;
    auto row5Area = mainArea.removeFromTop (row5TotalH);
    (void) row5Area;
    const int row6TotalH = groupLabelH + rowKnobSize + labelH + readoutH + 6;
    auto row6Area = mainArea.removeFromTop (row6TotalH);
    (void) row6Area;

    // Shared lambda: idx 0 = leftmost, idx 4 = rightmost; right edge of idx 4 = b.getRight() - 5 (5 px inset)
    const int rightRowEdge = b.getRight() - 5;
    auto placeRightRowKnob = [&](juce::Slider& s, juce::Label& lbl, juce::Label& rdout,
                                  int idx, int knobY)
    {
        const int cx = rightRowEdge - (4 - idx) * rowStep - rowKnobSize / 2;
        s.setBounds    (cx - rowKnobSize / 2, knobY,                        rowKnobSize, rowKnobSize);
        lbl.setBounds  (cx - rowLabelW / 2,   s.getBottom() + 2,            rowLabelW,   labelH);
        rdout.setBounds(cx - rowLabelW / 2,   s.getBottom() + labelH + 2,   rowLabelW,   readoutH);
    };

    // Cloud — vertically level with Row 1
    placeRightRowKnob (cloudDepthSlider,  cloudDepthLabel,  cloudDepthReadout,  0, rowY);
    placeRightRowKnob (cloudRateSlider,   cloudRateLabel,   cloudRateReadout,   1, rowY);
    placeRightRowKnob (cloudSizeSlider,   cloudSizeLabel,   cloudSizeReadout,   2, rowY);
    placeRightRowKnob (cloudIRFeedSlider, cloudIRFeedLabel, cloudIRFeedReadout, 3, rowY);
    placeRightRowKnob (cloudVolumeSlider, cloudVolumeLabel, cloudVolumeReadout, 4, rowY);

    cloudGroupBounds = juce::Rectangle<int> (
        cloudDepthSlider.getX(),
        topKnobRow.getY() - 10 - rowShiftUp,
        cloudVolumeSlider.getRight() - cloudDepthSlider.getX(),
        groupLabelH);
    {
        const int ledH = groupLabelH;   // full header height for power-button icon
        const int ledW = ledH;           // square
        const int ledY = cloudGroupBounds.getY() + (groupLabelH - ledH) / 2 - 1;
        cloudOnButton.setBounds (cloudGroupBounds.getRight() - ledW, ledY, ledW, ledH);
    }

    // Shimmer — vertically level with Row 2
    placeRightRowKnob (shimPitchSlider,  shimPitchLabel,  shimPitchReadout,  0, row2KnobY);
    placeRightRowKnob (shimSizeSlider,   shimSizeLabel,   shimSizeReadout,   1, row2KnobY);
    placeRightRowKnob (shimColourSlider, shimColourLabel, shimColourReadout, 2, row2KnobY);
    placeRightRowKnob (shimIRFeedSlider, shimIRFeedLabel, shimIRFeedReadout, 3, row2KnobY);
    placeRightRowKnob (shimVolumeSlider, shimVolumeLabel, shimVolumeReadout, 4, row2KnobY);

    shimGroupBounds = juce::Rectangle<int> (
        shimPitchSlider.getX(),
        row2AbsY - rowShiftUp,
        shimVolumeSlider.getRight() - shimPitchSlider.getX(),
        groupLabelH);
    {
        const int ledH = groupLabelH;   // full header height for power-button icon
        const int ledW = ledH;           // square
        const int ledY = shimGroupBounds.getY() + (groupLabelH - ledH) / 2 - 1;
        shimOnButton.setBounds (shimGroupBounds.getRight() - ledW, ledY, ledW, ledH);
    }

    // —— Row R3 (right side): Tail AM mod (LFO Depth, LFO Rate) | Tail Frq mod (Tail Mod, Delay Depth, Rate) ——
    // Vertically aligned with Row 3 (Plate pre-diffuser).  5 px extra gap splits the two groups,
    // mirroring the IR Input / IR Controls split on the left side.
    // idx 0,1 = Tail AM mod;  idx 2,3,4 = Tail Frq mod.  Right edge of idx 4 = b.getRight().
    {
        auto placeR3Knob = [&](juce::Slider& s, juce::Label& lbl, juce::Label& rdout, int idx)
        {
            const int extraGap = (idx < 2) ? 5 : 0;   // extra gap to the left of the AM pair
            const int cx = rightRowEdge - (4 - idx) * rowStep - rowKnobSize / 2 - extraGap;
            s.setBounds    (cx - rowKnobSize / 2, row3KnobY,                     rowKnobSize, rowKnobSize);
            lbl.setBounds  (cx - rowLabelW / 2,   s.getBottom() + 2,             rowLabelW,   labelH);
            rdout.setBounds(cx - rowLabelW / 2,   s.getBottom() + labelH + 2,    rowLabelW,   readoutH);
        };
        placeR3Knob (modDepthSlider,    modDepthLabel,    modDepthReadout,    0);
        placeR3Knob (modRateSlider,     modRateLabel,     modRateReadout,     1);
        placeR3Knob (tailModSlider,     tailModLabel,     tailModReadout,     2);
        placeR3Knob (delayDepthSlider,  delayDepthLabel,  delayDepthReadout,  3);
        placeR3Knob (tailRateSlider,    tailRateLabel,    tailRateReadout,    4);

        const int headerY = row3AbsY - rowShiftUp;
        tailAMModGroupBounds = juce::Rectangle<int> (
            modDepthSlider.getX(), headerY,
            modRateSlider.getRight() - modDepthSlider.getX(), groupLabelH);
        tailFrqModGroupBounds = juce::Rectangle<int> (
            tailModSlider.getX(), headerY,
            tailRateSlider.getRight() - tailModSlider.getX(), groupLabelH);
    }

    // —— Width knob: moved to right of DRY/WET, aligned with Tail knob Y, rowKnobSize ——
    widthSlider.setBounds    (outputGainCenterX - outputGainKnobSize / 2, tailKnobY,                              outputGainKnobSize, outputGainKnobSize);
    widthLabel.setBounds     (outputGainCenterX - irLabelW / 2,           widthSlider.getBottom() + 2,           irLabelW, labelH);
    widthReadout.setBounds   (outputGainCenterX - irLabelW / 2,           widthSlider.getBottom() + labelH + 2,  irLabelW, readoutH);

    // —— EQ: left edge at DRY/WET centre (w/2), right edge at b.getRight().
    // Height = 225 × 1.5 = 337 px (50% taller).  Bottom aligned with the new meter bottom (h − 38).
    // Graph (130 px) at the bottom of the component; ctrl knobs above, restricted to the
    // column that sits under the Tail-mod knobs (ctrlAreaXOffset set before setBounds).
    static constexpr int kEQComponentH = 337;
    const int eqLeft   = w / 2;
    const int eqWidth  = b.getRight() - eqLeft;
    const int eqBottom = h - 34;                  // graph bottom at h−38 (EQGraph reduced(4) internally)
    const int eqTopY   = eqBottom - kEQComponentH;
    eqGraph.ctrlAreaXOffset = 0; // controls fill full EQ width
    eqGraph.setBounds (eqLeft, eqTopY, eqWidth, kEQComponentH);

    licenceLabel.setBounds (12,       h - 18, w / 2 - 12, 16);
    versionLabel.setBounds (w / 2,    h - 18, w / 2 - 12, 16);

    // —— Level meter panel — bottom-left ——
    // Bottom sits 20 px above the licence label (licence at h − 18, so meter bottom = h − 38).
    {
        const int meterW = 300;
        const int meterH = 153;
        const int meterX = rowStartX;
        const int meterY = h - 38 - meterH + 15;   // = h − 176
        outputLevelMeter.setBounds (meterX, meterY, meterW, meterH);
    }
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
    plateIRFeedSlider.setVisible (visible);
    plateIRFeedLabel.setVisible (visible);
    plateIRFeedReadout.setVisible (visible);
    plateDiffusionSlider.setVisible (visible);
    plateDiffusionLabel.setVisible (visible);
    plateDiffusionReadout.setVisible (visible);
    plateColourSlider.setVisible (visible);
    plateColourLabel.setVisible (visible);
    plateColourReadout.setVisible (visible);
    plateSizeSlider.setVisible (visible);
    plateSizeLabel.setVisible (visible);
    plateSizeReadout.setVisible (visible);
    plateOnButton.setVisible (visible);
    bloomSizeSlider.setVisible (visible);
    bloomSizeLabel.setVisible (visible);
    bloomSizeReadout.setVisible (visible);
    bloomFeedbackSlider.setVisible (visible);
    bloomFeedbackLabel.setVisible (visible);
    bloomFeedbackReadout.setVisible (visible);
    bloomTimeSlider.setVisible (visible);
    bloomTimeLabel.setVisible (visible);
    bloomTimeReadout.setVisible (visible);
    bloomIRFeedSlider.setVisible (visible);
    bloomIRFeedLabel.setVisible (visible);
    bloomIRFeedReadout.setVisible (visible);
    bloomVolumeSlider.setVisible (visible);
    bloomVolumeLabel.setVisible (visible);
    bloomVolumeReadout.setVisible (visible);
    bloomOnButton.setVisible (visible);
    cloudDepthSlider.setVisible (visible);
    cloudDepthLabel.setVisible (visible);
    cloudDepthReadout.setVisible (visible);
    cloudRateSlider.setVisible (visible);
    cloudRateLabel.setVisible (visible);
    cloudRateReadout.setVisible (visible);
    cloudSizeSlider.setVisible (visible);
    cloudSizeLabel.setVisible (visible);
    cloudSizeReadout.setVisible (visible);
    cloudIRFeedSlider.setVisible (visible);
    cloudIRFeedLabel.setVisible (visible);
    cloudIRFeedReadout.setVisible (visible);
    cloudVolumeSlider.setVisible (visible);
    cloudVolumeLabel.setVisible (visible);
    cloudVolumeReadout.setVisible (visible);
    cloudOnButton.setVisible (visible);
    shimPitchSlider.setVisible (visible);
    shimPitchLabel.setVisible (visible);
    shimPitchReadout.setVisible (visible);
    shimSizeSlider.setVisible (visible);
    shimSizeLabel.setVisible (visible);
    shimSizeReadout.setVisible (visible);
    shimColourSlider.setVisible (visible);
    shimColourLabel.setVisible (visible);
    shimColourReadout.setVisible (visible);
    shimIRFeedSlider.setVisible (visible);
    shimIRFeedLabel.setVisible (visible);
    shimIRFeedReadout.setVisible (visible);
    shimVolumeSlider.setVisible (visible);
    shimVolumeLabel.setVisible (visible);
    shimVolumeReadout.setVisible (visible);
    shimOnButton.setVisible (visible);
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
    outputLevelMeter.setInputLevels  (pingProcessor.getInputLevelDb  (0), pingProcessor.getInputLevelDb  (1));
    outputLevelMeter.setOutputLevels (pingProcessor.getOutputLevelDb (0), pingProcessor.getOutputLevelDb (1));
    outputLevelMeter.setErLevels     (pingProcessor.getErLevelDb     (0), pingProcessor.getErLevelDb     (1));
    outputLevelMeter.setTailLevels   (pingProcessor.getTailLevelDb   (0), pingProcessor.getTailLevelDb   (1));
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
    plateIRFeedReadout.setText    (juce::String (v ("plateIRFeed"),    2), juce::dontSendNotification);
    plateDiffusionReadout.setText (juce::String (v ("plateDiffusion"), 2), juce::dontSendNotification);
    {
        // Colour → show cutoff frequency: 0→2 kHz, 1→8 kHz
        float colourHz = 2000.f + v ("plateColour") * 6000.f;
        plateColourReadout.setText (juce::String (colourHz / 1000.f, 1) + " kHz", juce::dontSendNotification);
    }
    {
        // Size → show largest allpass delay time: prime 691 × plateSize / 48000 × 1000 ms
        float sizeMs = v ("plateSize") * 691.0f / 48000.0f * 1000.0f;
        plateSizeReadout.setText (juce::String (sizeMs, 1) + " ms", juce::dontSendNotification);
    }
    bloomSizeReadout.setText      (juce::String (v ("bloomSize"), 2) + "\xc3\x97",  juce::dontSendNotification);
    bloomFeedbackReadout.setText  (juce::String (v ("bloomFeedback"),  2),  juce::dontSendNotification);
    bloomTimeReadout.setText      (juce::String (juce::roundToInt (v ("bloomTime"))) + " ms", juce::dontSendNotification);
    bloomIRFeedReadout.setText    (juce::String (v ("bloomIRFeed"),    2),  juce::dontSendNotification);
    bloomVolumeReadout.setText    (juce::String (v ("bloomVolume"),    2),  juce::dontSendNotification);
    cloudDepthReadout.setText  (juce::String (juce::roundToInt (v ("cloudDepth") * 100)) + "%",
                               juce::dontSendNotification);
    cloudRateReadout.setText   (juce::String (v ("cloudRate"),    2), juce::dontSendNotification);
    cloudSizeReadout.setText   (juce::String (juce::roundToInt (v ("cloudSize"))) + " ms", juce::dontSendNotification);
    cloudIRFeedReadout.setText (juce::String (v ("cloudFeedback"), 2), juce::dontSendNotification);
    cloudVolumeReadout.setText (juce::String (v ("cloudIRFeed"),  2), juce::dontSendNotification);
    {
        int st = juce::roundToInt (v ("shimPitch"));
        shimPitchReadout.setText  ((st >= 0 ? "+" : "") + juce::String (st) + " st", juce::dontSendNotification);
    }
    shimSizeReadout.setText   (juce::String (juce::roundToInt (v ("shimSize"))) + " ms",   juce::dontSendNotification);
    shimColourReadout.setText (juce::String (juce::roundToInt (v ("shimDelay")))  + " ms",
                               juce::dontSendNotification);
    shimIRFeedReadout.setText (juce::String (v ("shimIRFeed"), 2),                 juce::dontSendNotification);
    shimVolumeReadout.setText (juce::String (v ("shimFeedback"), 2),               juce::dontSendNotification);
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
