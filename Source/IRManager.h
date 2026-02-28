#pragma once

#include <JuceHeader.h>

/**
 * Scans the fixed IR folder (~/Documents/P!NG/IRs) and provides
 * a list of impulse response files for the plugin pick list.
 */
class IRManager
{
public:
    IRManager() = default;

    /** Returns the fixed folder where the plugin looks for IR files. */
    static juce::File getIRFolder();

    /** Returns display names (filename without extension) in order. */
    juce::StringArray getDisplayNames() const;

    /** Returns full File for each IR; indices match getDisplayNames(). */
    juce::Array<juce::File> getIRFiles() const;

    /** Re-scans the folder. Call when you want to refresh the list. */
    void refresh();

    /** Number of IRs currently found. */
    int getNumIRs() const { return irFiles.size(); }

    /** Get the File for a given index (0-based). Returns invalid File if out of range. */
    juce::File getIRFileAt (int index) const;

    /** Display names for 4-channel IRs only (synthesized). Indices match getIRFiles4Channel(). */
    juce::StringArray getDisplayNames4Channel() const;
    /** Files that have 4 channels (synthesized IRs). */
    juce::Array<juce::File> getIRFiles4Channel() const;
    /** Get file at index into the 4-channel subset. */
    juce::File getIRFileAt4Channel (int index) const;

private:
    juce::Array<juce::File> irFiles;

    void scanFolder();
};
