#include "FloorPlanComponent.h"

namespace
{
    const double kPi = 3.14159265358979323846;
    const juce::Colour colSpkL { 0xffd4714a };
    const juce::Colour colSpkR { 0xffd4a040 };
    const juce::Colour colMicL { 0xff4a9ed4 };
    const juce::Colour colMicR { 0xff40c8d4 };
    const juce::Colour colWall { 0xffc8a96e };
    const juce::Colour colFill1 { 0xf214142a };
    const juce::Colour colFill2 { 0xf20c0c1e };
    const juce::Colour colGrid { 0x0ac8a96e };
    const float CORE_R = 10.0f;
    const float RING_R = 16.0f;
    const float RING_W = 6.0f;

    juce::Path makeSpeakerIconPath()
    {
        juce::Path p;
        p.addRoundedRectangle (-0.7f, -0.6f, 0.55f, 1.2f, 0.12f);
        p.addEllipse (0.25f, -0.3f, 0.45f, 0.6f);
        return p;
    }

    juce::Path makeMicIconPath()
    {
        juce::Path p;
        p.addEllipse (-0.3f, -0.65f, 0.6f, 0.5f);
        p.addRoundedRectangle (-0.12f, -0.05f, 0.24f, 0.55f, 0.06f);
        p.addRoundedRectangle (-0.32f, 0.48f, 0.64f, 0.16f, 0.06f);
        return p;
    }

    void drawTransducerIcon (juce::Graphics& g, const juce::Path& iconPath, float cx, float cy,
                            float angle, float scale, juce::Colour col)
    {
        // Path is in -1..1, so: scale at origin, rotate at origin, translate to (cx,cy)
        auto tr = juce::AffineTransform::scale (scale).rotated (angle).translated (cx, cy);
        g.setColour (col);
        g.fillPath (iconPath, tr);
    }
}

FloorPlanComponent::FloorPlanComponent()
{
    setOpaque (false);
}

std::vector<std::pair<double, double>> FloorPlanComponent::roomPoly (const std::string& shape)
{
    std::vector<std::pair<double, double>> poly;
    if (shape == "L-shaped")
        poly = {{0,0},{1,0},{1,.55},{.55,.55},{.55,1},{0,1}};
    else if (shape == "Fan / Shoebox")
        poly = {{.15,0},{.85,0},{1,1},{0,1}};
    else if (shape == "Cylindrical")
    {
        for (int i = 0; i < 64; ++i)
        {
            double a = (i / 64.0) * 2 * kPi;
            poly.push_back ({ 0.5 + 0.48 * std::cos (a), 0.5 + 0.48 * std::sin (a) });
        }
    }
    else if (shape == "Cathedral")
    {
        const double cx = 0.5, cy = 0.5, nw = 0.18, nd = 0.48, tw = 0.48, th = 0.18;
        poly = {{cx-nw,cy-nd},{cx+nw,cy-nd},{cx+nw,cy-th},{cx+tw,cy-th},
                {cx+tw,cy+th},{cx+nw,cy+th},{cx+nw,cy+nd},{cx-nw,cy+nd},
                {cx-nw,cy+th},{cx-tw,cy+th},{cx-tw,cy-th},{cx-nw,cy-th}};
    }
    else if (shape == "Octagonal")
    {
        for (int i = 0; i < 8; ++i)
        {
            double a = (i / 8.0) * 2 * kPi + kPi / 8;
            poly.push_back ({ 0.5 + 0.47 * std::cos (a), 0.5 + 0.47 * std::sin (a) });
        }
    }
    else
        poly = {{0,0},{1,0},{1,1},{0,1}};
    return poly;
}

juce::Point<float> FloorPlanComponent::transducerCanvasPos (int index) const
{
    if (! getParams || index < 0 || index > 3) return {};
    auto p = getParams();
    const int W = getWidth(), H = getHeight();
    if (W < 20 || H < 20) return {};
    const double rw = std::max (0.5, p.width), rd = std::max (0.5, p.depth);
    auto poly = roomPoly (p.shape);
    double minX = 1, maxX = 0, minY = 1, maxY = 0;
    for (const auto& pt : poly)
    {
        minX = std::min (minX, pt.first);
        maxX = std::max (maxX, pt.first);
        minY = std::min (minY, pt.second);
        maxY = std::max (maxY, pt.second);
    }
    const double pw = maxX - minX, ph = maxY - minY;
    const int margin = 18;
    const double pxPerMx = (W - margin * 2) / (rw * std::max (0.01, pw));
    const double pxPerMy = (H - margin * 2) / (rd * std::max (0.01, ph));
    const double ppm = std::min (pxPerMx, pxPerMy);
    const double scaleX = rw * ppm, scaleY = rd * ppm;
    const double ox = (W - pw * scaleX) / 2 - minX * scaleX;
    const double oy = (H - ph * scaleY) / 2 - minY * scaleY;
    return { (float) (transducers.cx[index] * scaleX + ox),
             (float) (transducers.cy[index] * scaleY + oy) };
}

void FloorPlanComponent::canvasToNorm (float cx, float cy, double& nx, double& ny) const
{
    if (! getParams) return;
    auto p = getParams();
    const int W = getWidth(), H = getHeight();
    if (W < 20 || H < 20) return;
    const double rw = std::max (0.5, p.width), rd = std::max (0.5, p.depth);
    auto poly = roomPoly (p.shape);
    double minX = 1, maxX = 0, minY = 1, maxY = 0;
    for (const auto& pt : poly)
    {
        minX = std::min (minX, pt.first);
        maxX = std::max (maxX, pt.first);
        minY = std::min (minY, pt.second);
        maxY = std::max (maxY, pt.second);
    }
    const double pw = maxX - minX, ph = maxY - minY;
    const int margin = 18;
    const double pxPerMx = (W - margin * 2) / (rw * std::max (0.01, pw));
    const double pxPerMy = (H - margin * 2) / (rd * std::max (0.01, ph));
    const double ppm = std::min (pxPerMx, pxPerMy);
    const double scaleX = rw * ppm, scaleY = rd * ppm;
    const double ox = (W - pw * scaleX) / 2 - minX * scaleX;
    const double oy = (H - ph * scaleY) / 2 - minY * scaleY;
    nx = (cx - ox) / scaleX;
    ny = (cy - oy) / scaleY;
}

bool FloorPlanComponent::isInsideRoom (double nx, double ny) const
{
    if (! getParams) return false;
    auto poly = roomPoly (getParams().shape);
    int n = (int) poly.size();
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        double xi = poly[i].first, yi = poly[i].second;
        double xj = poly[j].first, yj = poly[j].second;
        if (((yi > ny) != (yj > ny)) && (nx < (xj - xi) * (ny - yi) / (yj - yi) + xi))
            inside = ! inside;
    }
    return inside;
}

FloorPlanComponent::HitResult FloorPlanComponent::transducerHitTest (float mx, float my)
{
    for (int i = 0; i < 4; ++i)
    {
        auto pt = transducerCanvasPos (i);
        float dist = std::hypot (mx - pt.x, my - pt.y);
        if (dist < CORE_R + 2)
            return { i, false };
        if (dist >= RING_R - RING_W / 2 - 2 && dist <= RING_R + RING_W / 2 + 2)
            return { i, true };
    }
    return { -1, false };
}

void FloorPlanComponent::mouseDown (const juce::MouseEvent& e)
{
    auto hit = transducerHitTest ((float) e.x, (float) e.y);
    if (hit.index >= 0)
    {
        dragIndex = hit.index;
        dragRotate = hit.rotate;
        if (dragRotate)
        {
            auto pt = transducerCanvasPos (hit.index);
            dragStartAngle = std::atan2 (e.y - pt.y, e.x - pt.x) - transducers.angle[hit.index];
        }
    }
}

void FloorPlanComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0) return;
    if (dragRotate)
    {
        auto pt = transducerCanvasPos (dragIndex);
        transducers.angle[dragIndex] = std::atan2 (e.y - pt.y, e.x - pt.x) - dragStartAngle;
    }
    else
    {
        double nx, ny;
        canvasToNorm ((float) e.x, (float) e.y, nx, ny);
        if (isInsideRoom (nx, ny))
        {
            transducers.cx[dragIndex] = juce::jlimit (0.0, 1.0, nx);
            transducers.cy[dragIndex] = juce::jlimit (0.0, 1.0, ny);
        }
    }
    repaint();
}

void FloorPlanComponent::mouseUp (const juce::MouseEvent&)
{
    dragIndex = -1;
}

void FloorPlanComponent::mouseMove (const juce::MouseEvent& e)
{
    auto hit = transducerHitTest ((float) e.x, (float) e.y);
    if (hit.index >= 0)
        setMouseCursor (hit.rotate ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void FloorPlanComponent::paint (juce::Graphics& g)
{
    if (! getParams)
        return;
    g.reduceClipRegion (getLocalBounds());
    auto p = getParams();
    const int W = getWidth();
    const int H = getHeight();
    if (W < 20 || H < 20)
        return;

    const double rw = std::max (0.5, p.width);
    const double rd = std::max (0.5, p.depth);
    auto poly = roomPoly (p.shape);

    double minX = 1, maxX = 0, minY = 1, maxY = 0;
    for (const auto& pt : poly)
    {
        minX = std::min (minX, pt.first);
        maxX = std::max (maxX, pt.first);
        minY = std::min (minY, pt.second);
        maxY = std::max (maxY, pt.second);
    }
    const double pw = maxX - minX, ph = maxY - minY;
    const int margin = 18;
    const double pxPerMx = (W - margin * 2) / (rw * std::max (0.01, pw));
    const double pxPerMy = (H - margin * 2) / (rd * std::max (0.01, ph));
    const double ppm = std::min (pxPerMx, pxPerMy);
    const double scaleX = rw * ppm;
    const double scaleY = rd * ppm;
    const double ox = (W - pw * scaleX) / 2 - minX * scaleX;
    const double oy = (H - ph * scaleY) / 2 - minY * scaleY;

    auto toCanvas = [&] (double nx, double ny) -> juce::Point<float>
    {
        return { (float) (nx * scaleX + ox), (float) (ny * scaleY + oy) };
    };

    juce::Path roomPath;
    for (size_t i = 0; i < poly.size(); ++i)
    {
        auto pt = toCanvas (poly[i].first, poly[i].second);
        if (i == 0)
            roomPath.startNewSubPath (pt);
        else
            roomPath.lineTo (pt);
    }
    roomPath.closeSubPath();

    g.setFillType (juce::ColourGradient::vertical (colFill1, 0.f, colFill2, (float) H));
    g.fillPath (roomPath);
    g.setColour (colWall.withAlpha (0.55f));
    g.strokePath (roomPath, juce::PathStrokeType (1.5f));

    g.saveState();
    g.reduceClipRegion (roomPath);
    const float gridStep = (float) std::max (20.0, std::min (scaleX, scaleY) * 0.08);
    g.setColour (colGrid.withAlpha (0.08f));
    for (float gx = 0; gx < (float) W; gx += gridStep)
        g.drawLine (gx, 0, gx, (float) H, 0.5f);
    for (float gy = 0; gy < (float) H; gy += gridStep)
        g.drawLine (0, gy, (float) W, gy, 0.5f);
    g.restoreState();

    if (p.shape == "Rectangular" || p.shape == "Fan / Shoebox" || p.shape == "L-shaped")
    {
        auto pt00 = toCanvas (0, 0);
        auto pt10 = toCanvas (1, 0);
        auto pt01 = toCanvas (0, 1);
        g.setColour (colWall.withAlpha (0.9f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (rw, 1) + " m", juce::Rectangle<float> ((pt00.x + pt10.x) / 2 - 30, pt00.y - 8, 60, 12),
                    juce::Justification::centred, false);
        g.drawText (juce::String (rd, 1) + " m", juce::Rectangle<float> (pt00.x - 28, (pt00.y + pt01.y) / 2 - 6, 56, 12),
                    juce::Justification::centred, false);
    }

    const juce::Colour cols[] = { colSpkL, colSpkR, colMicL, colMicR };
    const float beamLen = RING_R * 3.2f;
    const bool isSpeaker[] = { true, true, false, false };

    for (int i = 0; i < 4; ++i)
    {
        const float cx = toCanvas (transducers.cx[i], transducers.cy[i]).x;
        const float cy = toCanvas (transducers.cx[i], transducers.cy[i]).y;
        const float ang = (float) transducers.angle[i];
        double arcHalf = isSpeaker[i] ? (0.6 * kPi) : (kPi * 0.65 * 0.5);
        if (! isSpeaker[i])
        {
            if (p.mic_pattern == "omni") arcHalf = kPi * 0.5;
            else if (p.mic_pattern == "subcardioid") arcHalf = kPi * 0.75 * 0.5;
            else if (p.mic_pattern == "figure8") arcHalf = kPi * 0.25;
        }
        juce::Path beam;
        beam.startNewSubPath (cx, cy);
        const int nSeg = 16;
        for (int s = 0; s <= nSeg; ++s)
        {
            const float t = (float) s / (float) nSeg;
            const float theta = (float) (ang - arcHalf + t * 2 * arcHalf);
            beam.lineTo (cx + beamLen * std::cos (theta), cy + beamLen * std::sin (theta));
        }
        beam.closeSubPath();
        g.setColour (cols[i].withAlpha (isSpeaker[i] ? 0.18f : 0.16f));
        g.fillPath (beam);
    }

    juce::Path spkPath = makeSpeakerIconPath();
    juce::Path micPath = makeMicIconPath();

    for (int i = 0; i < 4; ++i)
    {
        auto pt = toCanvas (transducers.cx[i], transducers.cy[i]);
        g.setColour (cols[i].withAlpha (0.22f));
        g.drawEllipse (pt.x - RING_R, pt.y - RING_R, RING_R * 2, RING_R * 2, RING_W);

        float tickX = pt.x + std::cos ((float) transducers.angle[i]) * RING_R;
        float tickY = pt.y + std::sin ((float) transducers.angle[i]) * RING_R;
        g.setColour (cols[i]);
        g.fillEllipse (tickX - 3.5f, tickY - 3.5f, 7, 7);

        const juce::Path& iconPath = isSpeaker[i] ? spkPath : micPath;
        drawTransducerIcon (g, iconPath, pt.x, pt.y, (float) transducers.angle[i], CORE_R, cols[i]);
    }

    // Legend: colour guide for L/R speakers and mics (top-left so never scrolled out of view)
    const float legX = 8.0f;
    const float legY = 8.0f;
    const float legIconSz = 10.0f;
    const float legRow = 14.0f;
    const juce::Colour legText { 0xc0ffffff };
    g.setFont (juce::FontOptions (9.0f));
    const char* legLabels[] = { "Spk L", "Spk R", "Mic L", "Mic R" };
    for (int i = 0; i < 4; ++i)
    {
        float iconCx = legX + legIconSz * 0.5f;
        float yy = legY + (i + 0.5f) * legRow;  // center icon vertically in row
        const juce::Path& legIcon = (i < 2) ? spkPath : micPath;
        drawTransducerIcon (g, legIcon, iconCx, yy, 0.0f, legIconSz * 0.5f, cols[i]);
        g.setColour (legText);
        g.drawText (legLabels[i], (int) (legX + legIconSz + 6), (int) (legY + i * legRow), 52, (int) legRow,
                    juce::Justification::centredLeft, false);
    }
}
