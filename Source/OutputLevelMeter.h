#pragma once

#include <JuceHeader.h>

class OutputLevelMeter : public juce::Component
{
public:
    OutputLevelMeter() = default;

    void setLevelsDb (float leftDb, float rightDb)
    {
        if (std::abs (levelDbL - leftDb) > 0.01f || std::abs (levelDbR - rightDb) > 0.01f)
        {
            levelDbL = leftDb;
            levelDbR = rightDb;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
            return;

        constexpr int barCount = 2;
        const float barH = (bounds.getHeight() - (barCount - 1) * gap) / (float) barCount;
        if (barH <= 0.0f)
            return;

        const float barWidth = bounds.getWidth();
        juce::Colour greenColour  { 0xff4ade80 };
        juce::Colour yellowColour { 0xfffacc15 };
        juce::Colour redColour    { 0xfff87171 };
        const float greenEnd = 0.90f;
        const float yellowEnd = 0.95f;

        juce::ColourGradient gradient (
            greenColour, 0, 0,
            redColour, barWidth, 0,
            false);
        gradient.addColour (greenEnd, greenColour);
        gradient.addColour (yellowEnd, yellowColour);

        for (int ch = 0; ch < barCount; ++ch)
        {
            float db = (ch == 0) ? levelDbL : levelDbR;
            float norm = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            norm = std::pow (norm, sensitivityExponent);
            float fillW = barWidth * norm;

            float y = bounds.getY() + ch * (barH + gap);
            auto barBounds = juce::Rectangle<float> (bounds.getX(), y, barWidth, barH);

            g.setColour (juce::Colour (0xff1a1a1a));
            g.fillRoundedRectangle (barBounds, 1.0f);

            if (fillW > 0.5f)
            {
                auto fillRect = barBounds.withWidth (fillW);
                g.setGradientFill (gradient);
                g.fillRoundedRectangle (fillRect, 1.0f);
            }
        }
    }

private:
    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 0.0f;
    static constexpr float sensitivityExponent = 2.0f;  // Power curve: quieter signals fill less
    static constexpr float gap = 1.0f;

    float levelDbL = -60.0f;
    float levelDbR = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputLevelMeter)
};
