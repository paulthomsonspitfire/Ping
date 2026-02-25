#pragma once

#include <JuceHeader.h>
#include "LicenceVerifier.h"

/** Floating activation overlay — blocks plugin until name + serial are verified. */
class LicenceScreen : public juce::Component
{
public:
    std::function<void(LicenceResult, juce::String)> onActivationSuccess;

    LicenceScreen()
    {
        setOpaque (true);

        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Activate P!NG", juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (20.0f).withStyle ("Bold"));
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8e8e8));

        addAndMakeVisible (nameLabel);
        nameLabel.setText ("Your Name (as provided at purchase):", juce::dontSendNotification);
        nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xff909090));

        addAndMakeVisible (nameField);
        nameField.setTextToShowWhenEmpty ("e.g. Paul Hartnoll", juce::Colour (0xff606060));
        nameField.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a2a));
        nameField.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe8e8e8));
        nameField.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (serialLabel);
        serialLabel.setText ("Serial Number:", juce::dontSendNotification);
        serialLabel.setColour (juce::Label::textColourId, juce::Colour (0xff909090));

        addAndMakeVisible (serialField);
        serialField.setTextToShowWhenEmpty ("e.g. ABCDE-FGHIJ-KLMNO-PQRST-...", juce::Colour (0xff606060));
        serialField.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a2a));
        serialField.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe8e8e8));
        serialField.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (activateButton);
        activateButton.setButtonText ("Activate");
        activateButton.onClick = [this] { attemptActivation(); };
        activateButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffe8a84a));
        activateButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff141414));

        addAndMakeVisible (statusLabel);
        statusLabel.setJustificationType (juce::Justification::centred);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe08080));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xdd141414));

        auto card = getLocalBounds().reduced (40).toFloat();
        float corner = 12.0f;
        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (card, corner);
        g.setColour (juce::Colour (0xff2a2a2a));
        g.drawRoundedRectangle (card.reduced (0.5f), corner - 0.5f, 0.8f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (60);

        titleLabel.setBounds (area.removeFromTop (50));
        area.removeFromTop (25);

        nameLabel.setBounds (area.removeFromTop (22));
        nameField.setBounds (area.removeFromTop (32));
        area.removeFromTop (18);

        serialLabel.setBounds (area.removeFromTop (22));
        serialField.setBounds (area.removeFromTop (32));
        area.removeFromTop (25);

        activateButton.setBounds (area.removeFromTop (40).withSizeKeepingCentre (140, 38));
        area.removeFromTop (20);

        statusLabel.setBounds (area.removeFromTop (50));
    }

private:
    void attemptActivation()
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8a84a));
        statusLabel.setText ("Checking...", juce::dontSendNotification);

        LicenceVerifier verifier;
        auto result = verifier.activate (nameField.getText().toStdString(),
                                         serialField.getText().toStdString());

        if (result.valid && ! result.expired)
        {
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff80c080));
            statusLabel.setText ("Activated! " + juce::String (LicenceVerifier::licenceDisplayString (result)),
                                juce::dontSendNotification);

            if (onActivationSuccess)
                onActivationSuccess (result, serialField.getText());
        }
        else if (result.valid && result.expired)
        {
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8a84a));
            statusLabel.setText (result.errorMessage, juce::dontSendNotification);
        }
        else
        {
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe08080));
            statusLabel.setText (result.errorMessage, juce::dontSendNotification);
        }
    }

    juce::Label titleLabel, nameLabel, serialLabel, statusLabel;
    juce::TextEditor nameField, serialField;
    juce::TextButton activateButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenceScreen)
};
