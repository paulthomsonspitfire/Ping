#pragma once

#include <JuceHeader.h>

/**
 * Multi-channel level meter panel.
 * Displays 8 bars in 4 labelled groups: Input L/R, Output L/R, ER L/R, Tail L/R.
 * Each bar has a visible background rail (so the range reads even at silence)
 * and a colour-coded fill that turns amber near 0 dB and red above 0 dB.
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

        // ── Palette ────────────────────────────────────────────────────────
        const juce::Colour bgColour    { 0xff1a1824 };   // panel background
        const juce::Colour railColour  { 0xff2e2b3e };   // empty bar rail
        const juce::Colour fillNormal  { 0xff4f46e5 };   // indigo
        const juce::Colour fillHot     { 0xffe8a628 };   // amber  (above −6 dB)
        const juce::Colour fillClip    { 0xffdc2626 };   // red    (above  0 dB)
        const juce::Colour titleColour { 0xff6b7280 };   // grey-500
        const juce::Colour labelColour { 0xff9ca3af };   // grey-400
        const juce::Colour scaleColour { 0xff4b5563 };   // grey-600
        const juce::Colour gridColour  { 0xff2d2b3c };   // subtle grid line

        // ── Geometry ───────────────────────────────────────────────────────
        const float titleH  = 13.f;
        const float scaleH  = 14.f;
        const float labelW  = 38.f;
        const float barH    = 7.f;
        const float rowGap  = 2.f;    // gap between the two bars in a group
        const float groupGap= 6.f;    // gap between groups
        const float cornerR = 1.5f;

        // groups: { "INPUT", "OUTPUT", "ER", "TAIL" }, each with 2 bars
        static const char* groupNames[] = { "INPUT", "OUTPUT", "ER", "TAIL" };
        static const char* barLabels[]  = { "L", "R", "L", "R", "L", "R", "L", "R" };

        const float totalBarsH = 4.f * (2.f * barH + rowGap) + 3.f * groupGap;
        const float contentH   = titleH + 4.f + totalBarsH + 4.f + scaleH;

        // Vertically centre the content in the component
        const float topY  = b.getY() + (b.getHeight() - contentH) * 0.5f;
        const float barX  = b.getX() + labelW;
        const float barW  = b.getWidth() - labelW;
        if (barW < 4.f) return;

        // ── Background ─────────────────────────────────────────────────────
        g.setColour (bgColour);
        g.fillRoundedRectangle (b, 3.f);

        // ── Title ──────────────────────────────────────────────────────────
        g.setColour (titleColour);
        g.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));
        g.drawText ("LEVELS", b.getX(), topY, b.getWidth(), titleH,
                    juce::Justification::centred, false);

        // ── Bars ───────────────────────────────────────────────────────────
        float y = topY + titleH + 4.f;
        int ch = 0;

        for (int grp = 0; grp < 4; ++grp)
        {
            for (int row = 0; row < 2; ++row, ++ch)
            {
                const float barY = y + (float)row * (barH + rowGap);

                // Rail (background)
                auto rail = juce::Rectangle<float> (barX, barY, barW, barH);
                g.setColour (railColour);
                g.fillRoundedRectangle (rail, cornerR);

                // Grid lines at dB markers
                static const float markerDbs[] = { -48.f, -24.f, -12.f, -6.f, 0.f };
                for (float mDb : markerDbs)
                {
                    float nx = dbToNorm (mDb);
                    float gx = barX + nx * barW;
                    g.setColour (gridColour);
                    g.drawVerticalLine (juce::roundToInt (gx), barY, barY + barH);
                }

                // Signal fill
                float norm = dbToNorm (levels[(size_t)ch]);
                float fillWidth = barW * norm;
                if (fillWidth > 0.5f)
                {
                    float db = levels[(size_t)ch];
                    juce::Colour fillCol = (db >= 0.f)   ? fillClip
                                        : (db >= -6.f)  ? fillNormal.interpolatedWith (fillHot, (db + 6.f) / 6.f)
                                                        : fillNormal;
                    g.setColour (fillCol);
                    g.fillRoundedRectangle (barX, barY, fillWidth, barH, cornerR);
                }

                // Bar label (L / R)
                g.setColour (labelColour);
                g.setFont (juce::FontOptions (8.5f));
                // Group name on the first row, L/R on both
                float lx = b.getX();
                if (row == 0)
                {
                    // Group name, small, above-left of the pair
                    g.setColour (titleColour);
                    g.setFont (juce::FontOptions (7.5f).withStyle ("Bold"));
                    g.drawText (groupNames[grp], lx, barY - 0.f, labelW - 4.f, barH,
                                juce::Justification::centredRight, false);
                }
                g.setColour (labelColour);
                g.setFont (juce::FontOptions (8.0f));
                g.drawText (barLabels[ch], lx, barY + (barH - 9.f) * 0.5f, labelW - 4.f, 9.f,
                            juce::Justification::centredRight, false);
            }

            y += 2.f * barH + rowGap + groupGap;
        }

        // ── dB Scale ───────────────────────────────────────────────────────
        const float scaleY = topY + titleH + 4.f + totalBarsH + 3.f;
        static const float scaleDbs[] = { -60.f, -48.f, -24.f, -12.f, -6.f, 0.f, 6.f };
        static const char* scaleLabels[] = { "-60", "-48", "-24", "-12", "-6", "0", "+6" };

        g.setFont (juce::FontOptions (7.5f));
        for (int i = 0; i < 7; ++i)
        {
            float nx = dbToNorm (scaleDbs[i]);
            float lbX = barX + nx * barW;
            g.setColour (scaleColour);
            g.drawVerticalLine (juce::roundToInt (lbX), scaleY, scaleY + 3.f);

            g.setColour (scaleColour);
            juce::Rectangle<float> labelRect (lbX - 12.f, scaleY + 3.f, 24.f, 10.f);
            g.drawText (scaleLabels[i], labelRect, juce::Justification::centred, false);
        }

        // "dB" unit label at right
        g.setColour (scaleColour);
        g.setFont (juce::FontOptions (7.5f));
        g.drawText ("dB", barX + barW - 14.f, scaleY + 3.f, 14.f, 10.f,
                    juce::Justification::centredRight, false);
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
