#pragma once

#include <JuceHeader.h>

/**
 * Multi-channel level meter panel.
 * Displays 8 bars in 4 labelled groups: Input L/R, Output L/R, ER L/R, Tail L/R.
 * Each bar has a visible background rail (so the range reads even at silence)
 * and a colour-coded fill that turns amber near 0 dB and red above 0 dB.
 *
 * Label area is split into two columns:
 *   [GROUP NAME] [L/R]  [bar ...]
 * so the group name and the L/R tag never overlap.
 */
class OutputLevelMeter : public juce::Component
{
public:
    OutputLevelMeter()
    {
        levels.fill (kMinDb);
    }

    // ── Setters called from timerCallback ──────────────────────────────────
    void setInputLevels  (float l, float r) { setLevel (0, l); setLevel (1, r); }
    void setOutputLevels (float l, float r) { setLevel (2, l); setLevel (3, r); }
    void setErLevels     (float l, float r) { setLevel (4, l); setLevel (5, r); }
    void setTailLevels   (float l, float r) { setLevel (6, l); setLevel (7, r); }

    // ── Legacy 2-channel setter kept for compatibility ──────────────────────
    void setLevelsDb (float leftDb, float rightDb)
    {
        setOutputLevels (leftDb, rightDb);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (b.getWidth() < 4.f || b.getHeight() < 4.f) return;

        // ── Palette: black background, grey bars ────────────────────────────
        const juce::Colour bgColour    { 0xff0d0d0d };   // near-black panel
        const juce::Colour railColour  { 0xff242424 };   // dark grey rail
        const juce::Colour fillNormal  { 0xff606060 };   // mid grey signal fill
        const juce::Colour fillHot     { 0xffe8a628 };   // amber  (above −6 dB)
        const juce::Colour fillClip    { 0xffdc2626 };   // red    (above  0 dB)
        const juce::Colour groupColour { 0xff505050 };   // group name text
        const juce::Colour labelColour { 0xff808080 };   // L / R tag text
        const juce::Colour scaleColour { 0xff404040 };   // dB scale text + ticks
        const juce::Colour gridColour  { 0xff1c1c1c };   // subtle vertical grid

        // ── Geometry ───────────────────────────────────────────────────────
        // Label area: [groupNameW] [lrSep] [lrW]  |  bar
        const float groupNameW = 34.f;   // column for "INPUT", "OUT", "ER", "TAIL"
        const float lrSep      =  3.f;   // gap between group name and L/R tag
        const float lrW        = 12.f;   // column for "L" / "R"
        const float labelW     = groupNameW + lrSep + lrW;  // total left margin

        const float scaleH  = 14.f;
        const float barH    =  9.f;
        const float rowGap  =  2.f;   // gap between L and R bars within a group
        const float groupGap=  8.f;   // gap between groups
        const float cornerR =  1.5f;

        static const char* groupNames[] = { "INPUT", "OUT", "ER", "TAIL" };
        static const char* barLabels[]  = { "L", "R", "L", "R", "L", "R", "L", "R" };

        const float totalBarsH = 4.f * (2.f * barH + rowGap) + 3.f * groupGap;
        const float contentH   = totalBarsH + 4.f + scaleH;

        // Vertically centre the content in the component
        const float topY = b.getY() + (b.getHeight() - contentH) * 0.5f;
        const float barX = b.getX() + labelW;
        const float barW = b.getWidth() - labelW;
        if (barW < 4.f) return;

        // ── Background: transparent (no fill) ─────────────────────────────

        // ── Bars ───────────────────────────────────────────────────────────
        float y = topY;
        int ch = 0;

        for (int grp = 0; grp < 4; ++grp)
        {
            for (int row = 0; row < 2; ++row, ++ch)
            {
                const float barY = y + (float)row * (barH + rowGap);

                // Rail (full-width background — visible at all signal levels)
                g.setColour (railColour);
                g.fillRoundedRectangle (barX, barY, barW, barH, cornerR);

                // Subtle vertical grid lines at dB marker positions
                static const float markerDbs[] = { -48.f, -24.f, -12.f, -6.f, 0.f };
                for (float mDb : markerDbs)
                {
                    float gx = barX + dbToNorm (mDb) * barW;
                    g.setColour (gridColour);
                    g.drawVerticalLine (juce::roundToInt (gx), barY, barY + barH);
                }

                // Signal fill
                float fillWidth = barW * dbToNorm (levels[(size_t)ch]);
                if (fillWidth > 0.5f)
                {
                    float db = levels[(size_t)ch];
                    juce::Colour fillCol = (db >= 0.f)
                                        ? fillClip
                                        : (db >= -6.f)
                                            ? fillNormal.interpolatedWith (fillHot, (db + 6.f) / 6.f)
                                            : fillNormal;
                    g.setColour (fillCol);
                    g.fillRoundedRectangle (barX, barY, fillWidth, barH, cornerR);
                }

                // ── Labels ─────────────────────────────────────────────────
                const float lx = b.getX();

                // Group name column — only on the first (L) row of each group
                if (row == 0)
                {
                    g.setColour (groupColour);
                    g.setFont (juce::FontOptions (8.f).withStyle ("Bold"));
                    g.drawText (groupNames[grp],
                                lx, barY, groupNameW, barH,
                                juce::Justification::centredRight, false);
                }

                // L / R tag column — on both rows
                g.setColour (labelColour);
                g.setFont (juce::FontOptions (8.f));
                g.drawText (barLabels[ch],
                            lx + groupNameW + lrSep, barY, lrW, barH,
                            juce::Justification::centredLeft, false);
            }

            y += 2.f * barH + rowGap + groupGap;
        }

        // ── dB Scale ───────────────────────────────────────────────────────
        const float scaleY = topY + totalBarsH + 3.f;
        static const float scaleDbs[]    = { -60.f, -48.f, -24.f, -12.f, -6.f, 0.f, 6.f };
        static const char* scaleLabels[] = { "-60", "-48", "-24", "-12", "-6", "0", "+6" };

        g.setFont (juce::FontOptions (7.5f));
        for (int i = 0; i < 7; ++i)
        {
            float lbX = barX + dbToNorm (scaleDbs[i]) * barW;
            g.setColour (scaleColour);
            g.drawVerticalLine (juce::roundToInt (lbX), scaleY, scaleY + 3.f);
            juce::Rectangle<float> lr (lbX - 12.f, scaleY + 3.f, 24.f, 10.f);
            g.drawText (scaleLabels[i], lr, juce::Justification::centred, false);
        }
    }

private:
    static constexpr float kMinDb = -60.f;
    static constexpr float kMaxDb =   6.f;
    std::array<float, 8> levels;

    void setLevel (int ch, float db)
    {
        float clamped = juce::jlimit (kMinDb, kMaxDb, db);
        if (std::abs (levels[(size_t)ch] - clamped) > 0.05f)
        {
            levels[(size_t)ch] = clamped;
            repaint();
        }
    }

    float dbToNorm (float db) const noexcept
    {
        return juce::jlimit (0.f, 1.f, (db - kMinDb) / (kMaxDb - kMinDb));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputLevelMeter)
};
