#pragma once

#include <JuceHeader.h>

/** Manages user presets in ~/Library/Audio/Presets/Ping/P!NG/ */
class PresetManager
{
public:
    static juce::File getPresetDirectory();
    static juce::StringArray getPresetNames();
    static juce::File getPresetFile (const juce::String& name);
};
