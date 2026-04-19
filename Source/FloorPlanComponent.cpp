#include "FloorPlanComponent.h"

namespace
{
    const double kPi = 3.14159265358979323846;
    // Speakers (0/1) — warm orange/amber (unchanged from v2.5.0).
    const juce::Colour colSpkL  { 0xffd4714a };
    const juce::Colour colSpkR  { 0xffd4a040 };
    // MAIN mics (2/3) — icy blue accent.
    const juce::Colour colMicL  { 0xff4a9ed4 };
    const juce::Colour colMicR  { 0xff40c8d4 };
    // OUTRIG mics (4/5) — soft purple pair (L slightly cooler than R).
    const juce::Colour colOutrigL { 0xffb09aff };
    const juce::Colour colOutrigR { 0xffc8a6ff };
    // AMBIENT mics (6/7) — fresh green pair (L slightly darker than R).
    // Mixer strip uses the same green accent so key ↔ puck ↔ mixer all match.
    const juce::Colour colAmbientL { 0xff6fc26f };
    const juce::Colour colAmbientR { 0xff9ee89e };
    // Decca Tree (index 8, only when deccaVisible). Reuses the MAIN mic icy
    // blue so Decca reads as a "super-MAIN" in the same visual family.
    const juce::Colour colDecca { 0xff4a9ed4 };
    // Classical Decca geometry (must match the file-static constants in
    // IRSynthEngine::synthMainPath — see CLAUDE.md Decca section).
    const double kDeccaOuterM   = 2.0;
    const double kDeccaAdvanceM = 1.2;
    // Sentinel index for the Decca tree puck in hit-testing / drag state.
    // Outside the 0..7 range used by TransducerState's cx[]/cy[]/angle[].
    const int kDeccaIdx = 8;
    const juce::Colour colWall  { 0xffc8a96e };
    const juce::Colour colFill1 { 0xf214142a };
    const juce::Colour colFill2 { 0xf20c0c1e };
    const juce::Colour colGrid  { 0x0ac8a96e };
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
        auto tr = juce::AffineTransform::scale (scale).rotated (angle).translated (cx, cy);
        g.setColour (col);
        g.fillPath (iconPath, tr);
    }

    // Group classification for a transducer index.
    enum class Group { Speaker, Main, Outrig, Ambient };
    Group groupFor (int index)
    {
        if (index <= 1) return Group::Speaker;
        if (index <= 3) return Group::Main;
        if (index <= 5) return Group::Outrig;
        return Group::Ambient;
    }

    juce::Colour colourFor (int index)
    {
        switch (index)
        {
            case 0: return colSpkL;   case 1: return colSpkR;
            case 2: return colMicL;   case 3: return colMicR;
            case 4: return colOutrigL;case 5: return colOutrigR;
            case 6: return colAmbientL; default: return colAmbientR;
        }
    }

    const char* labelForMic (int index)
    {
        switch (groupFor (index))
        {
            case Group::Main:    return "M";
            case Group::Outrig:  return "O";
            case Group::Ambient: return "A";
            default:             return "";
        }
    }
}

FloorPlanComponent::FloorPlanComponent()
{
    setOpaque (false);
    mirrorCursor = makeMirrorCursor();
}

juce::MouseCursor FloorPlanComponent::makeMirrorCursor()
{
    // Procedurally build a 32×32 cursor glyph: a vertical axis line with
    // two triangular arrows pointing outward (mirror-across-axis icon).
    // Drawn at 2× resolution for Retina, then handed to MouseCursor with
    // scaleFactor = 2.0 so macOS picks the right physical size.
    const int sz = 32;
    juce::Image img (juce::Image::ARGB, sz, sz, true);
    juce::Graphics g (img);

    const float cx = sz * 0.5f;
    const float cy = sz * 0.5f;

    // Soft dark outline, then bright fill on top — readable on any background.
    auto drawWithOutline = [&] (const juce::Path& p)
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff8cd6ef)); // accentIce
        g.fillPath (p);
    };

    // Central vertical axis (the "mirror line").
    juce::Path axis;
    axis.startNewSubPath (cx, cy - 9.0f);
    axis.lineTo          (cx, cy + 9.0f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.strokePath (axis, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff8cd6ef));
    g.strokePath (axis, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Left arrow: points left, tail toward axis.
    juce::Path leftArrow;
    leftArrow.startNewSubPath (cx - 3.5f,  cy);
    leftArrow.lineTo          (cx - 10.5f, cy - 4.5f);
    leftArrow.lineTo          (cx - 10.5f, cy + 4.5f);
    leftArrow.closeSubPath();
    drawWithOutline (leftArrow);

    // Right arrow: mirror of the left.
    juce::Path rightArrow;
    rightArrow.startNewSubPath (cx + 3.5f,  cy);
    rightArrow.lineTo          (cx + 10.5f, cy - 4.5f);
    rightArrow.lineTo          (cx + 10.5f, cy + 4.5f);
    rightArrow.closeSubPath();
    drawWithOutline (rightArrow);

    // Hotspot at the glyph centre (sits on top of the puck being dragged).
    return juce::MouseCursor (img, sz / 2, sz / 2);
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

bool FloorPlanComponent::transducerVisible (int index) const
{
    if (index < 0 || index > 7) return false;
    switch (groupFor (index))
    {
        case Group::Speaker: return true;
        // MAIN pucks are hidden when the Decca Tree puck replaces them.
        case Group::Main:    return ! deccaVisible;
        case Group::Outrig:  return outrigVisible;
        case Group::Ambient: return ambientVisible;
    }
    return false;
}

juce::Point<float> FloorPlanComponent::transducerCanvasPos (int index) const
{
    if (! getParams || index < 0 || index > 7) return {};
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
    // Decca Tree puck takes priority when visible — sits where the MAIN mics
    // normally live and uses the same ring/core hit targets.
    if (deccaVisible && getParams)
    {
        auto p = getParams();
        const int W = getWidth(), H = getHeight();
        if (W >= 20 && H >= 20)
        {
            const double rw = std::max (0.5, p.width), rd = std::max (0.5, p.depth);
            auto poly = roomPoly (p.shape);
            double minX = 1, maxX = 0, minY = 1, maxY = 0;
            for (const auto& pt : poly)
            { minX = std::min (minX, pt.first); maxX = std::max (maxX, pt.first);
              minY = std::min (minY, pt.second); maxY = std::max (maxY, pt.second); }
            const double pw = maxX - minX, ph = maxY - minY;
            const int margin = 18;
            const double pxPerMx = (W - margin * 2) / (rw * std::max (0.01, pw));
            const double pxPerMy = (H - margin * 2) / (rd * std::max (0.01, ph));
            const double ppm = std::min (pxPerMx, pxPerMy);
            const double scaleX = rw * ppm, scaleY = rd * ppm;
            const double ox = (W - pw * scaleX) / 2 - minX * scaleX;
            const double oy = (H - ph * scaleY) / 2 - minY * scaleY;
            const float cx = (float) (transducers.deccaCx * scaleX + ox);
            const float cy = (float) (transducers.deccaCy * scaleY + oy);
            const float dist = std::hypot (mx - cx, my - cy);
            if (dist < CORE_R + 2)
                return { kDeccaIdx, false };
            if (dist >= RING_R - RING_W / 2 - 2 && dist <= RING_R + RING_W / 2 + 2)
                return { kDeccaIdx, true };
        }
    }

    for (int i = 0; i < 8; ++i)
    {
        if (! transducerVisible (i)) continue;
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
        // Option-mirror does not apply to the Decca Tree puck (it is a single
        // rigid super-puck — there is no partner to mirror it against).
        // Only enable if the partner puck is currently visible.
        mirrorDrag = hit.index != kDeccaIdx
                      && e.mods.isAltDown()
                      && transducerVisible (hit.index ^ 1);
        if (dragRotate)
        {
            if (hit.index == kDeccaIdx)
            {
                // Ring drag rotates the whole tree; anchor on deccaCx/Cy.
                auto pos = transducerCanvasPos (0); // placeholder for scale
                // Compute Decca puck canvas position via transducerCanvasPos-like math.
                // Simpler: reuse hit-test scaling by calling paint-side helpers.
                // Instead, compute atan2 between cursor and the Decca puck centre
                // using the same transform used in the hit test.
                if (getParams)
                {
                    auto p = getParams();
                    const int W = getWidth(), H = getHeight();
                    const double rw = std::max (0.5, p.width), rd = std::max (0.5, p.depth);
                    auto poly = roomPoly (p.shape);
                    double minX = 1, maxX = 0, minY = 1, maxY = 0;
                    for (const auto& pt : poly)
                    { minX = std::min (minX, pt.first); maxX = std::max (maxX, pt.first);
                      minY = std::min (minY, pt.second); maxY = std::max (maxY, pt.second); }
                    const double pw = maxX - minX, ph = maxY - minY;
                    const int margin = 18;
                    const double pxPerMx = (W - margin * 2) / (rw * std::max (0.01, pw));
                    const double pxPerMy = (H - margin * 2) / (rd * std::max (0.01, ph));
                    const double ppm = std::min (pxPerMx, pxPerMy);
                    const double scaleX = rw * ppm, scaleY = rd * ppm;
                    const double ox = (W - pw * scaleX) / 2 - minX * scaleX;
                    const double oy = (H - ph * scaleY) / 2 - minY * scaleY;
                    const float cx = (float) (transducers.deccaCx * scaleX + ox);
                    const float cy = (float) (transducers.deccaCy * scaleY + oy);
                    dragStartAngle = std::atan2 (e.y - cy, e.x - cx) - transducers.deccaAngle;
                    juce::ignoreUnused (pos);
                }
            }
            else
            {
                auto pt = transducerCanvasPos (hit.index);
                dragStartAngle = std::atan2 (e.y - pt.y, e.x - pt.x) - transducers.angle[hit.index];
            }
        }
        if (mirrorDrag)
        {
            setMouseCursor (mirrorCursor);
            repaint(); // redraw to show the centre guide line
        }
    }
}

void FloorPlanComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0) return;

    // Decca Tree rigid drag: position/angle live on TransducerState.deccaCx/Cy/Angle.
    if (dragIndex == kDeccaIdx)
    {
        if (dragRotate)
        {
            // Rotate the whole array around the tree centre.
            if (! getParams) { repaint(); return; }
            auto p = getParams();
            const int W = getWidth(), H = getHeight();
            const double rw = std::max (0.5, p.width), rd = std::max (0.5, p.depth);
            auto poly = roomPoly (p.shape);
            double minX = 1, maxX = 0, minY = 1, maxY = 0;
            for (const auto& pt : poly)
            { minX = std::min (minX, pt.first); maxX = std::max (maxX, pt.first);
              minY = std::min (minY, pt.second); maxY = std::max (maxY, pt.second); }
            const double pw = maxX - minX, ph = maxY - minY;
            const int margin = 18;
            const double pxPerMx = (W - margin * 2) / (rw * std::max (0.01, pw));
            const double pxPerMy = (H - margin * 2) / (rd * std::max (0.01, ph));
            const double ppm = std::min (pxPerMx, pxPerMy);
            const double scaleX = rw * ppm, scaleY = rd * ppm;
            const double ox = (W - pw * scaleX) / 2 - minX * scaleX;
            const double oy = (H - ph * scaleY) / 2 - minY * scaleY;
            const float cx = (float) (transducers.deccaCx * scaleX + ox);
            const float cy = (float) (transducers.deccaCy * scaleY + oy);
            transducers.deccaAngle = std::atan2 (e.y - cy, e.x - cx) - dragStartAngle;
        }
        else
        {
            double nx, ny;
            canvasToNorm ((float) e.x, (float) e.y, nx, ny);
            if (isInsideRoom (nx, ny))
            {
                transducers.deccaCx = juce::jlimit (0.0, 1.0, nx);
                transducers.deccaCy = juce::jlimit (0.0, 1.0, ny);
            }
        }
        repaint();
        return;
    }

    const int partner = dragIndex ^ 1;
    if (dragRotate)
    {
        auto pt = transducerCanvasPos (dragIndex);
        transducers.angle[dragIndex] = std::atan2 (e.y - pt.y, e.x - pt.x) - dragStartAngle;
        if (mirrorDrag)
        {
            // Horizontal mirror of an angle around the vertical axis: a' = π - a.
            // Verified against the default pair (Mic L = -2.356, Mic R = -0.785):
            //   π - (-2.356) = -0.785  ✓
            transducers.angle[partner] = kPi - transducers.angle[dragIndex];
        }
    }
    else
    {
        double nx, ny;
        canvasToNorm ((float) e.x, (float) e.y, nx, ny);
        if (isInsideRoom (nx, ny))
        {
            transducers.cx[dragIndex] = juce::jlimit (0.0, 1.0, nx);
            transducers.cy[dragIndex] = juce::jlimit (0.0, 1.0, ny);

            if (mirrorDrag)
            {
                // Mirror across the vertical centre line (x = 0.5), same y.
                // Skip if the mirrored point is outside the room (relevant
                // for L-shaped rooms) — dragged puck keeps moving, partner
                // holds its last valid position until symmetry is restorable.
                const double mx = 1.0 - transducers.cx[dragIndex];
                const double my = transducers.cy[dragIndex];
                if (isInsideRoom (mx, my))
                {
                    transducers.cx[partner] = juce::jlimit (0.0, 1.0, mx);
                    transducers.cy[partner] = juce::jlimit (0.0, 1.0, my);
                }
            }
        }
    }
    repaint();
}

void FloorPlanComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex >= 0)
    {
        // Decca tree drag maps to the MAIN placement group (it replaces the
        // MAIN L/R pair on the floor plan when deccaVisible is true).
        if (dragIndex == kDeccaIdx)
        {
            if (onMainPlacementChanged) onMainPlacementChanged();
        }
        else
        {
            // Fire per-group callback first, then the generic catch-all. The
            // partner puck is in the same group as the dragged puck, so a single
            // group callback covers both when mirroring.
            switch (groupFor (dragIndex))
            {
                case Group::Speaker:
                case Group::Main:
                    if (onMainPlacementChanged) onMainPlacementChanged();
                    break;
                case Group::Outrig:
                    if (onOutrigPlacementChanged) onOutrigPlacementChanged();
                    break;
                case Group::Ambient:
                    if (onAmbientPlacementChanged) onAmbientPlacementChanged();
                    break;
            }
        }
        if (onPlacementChanged) onPlacementChanged();
    }
    dragIndex = -1;
    if (mirrorDrag)
    {
        mirrorDrag = false;
        // Restore the default hover cursor — next mouseMove will upgrade it
        // to drag-hand or crosshair if the cursor is still over a puck.
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint(); // clear the centre guide line
    }
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

    // Option-mirror: draw a faint dashed vertical guide at x = 0.5 while the
    // user is Option-dragging a puck, visualising the horizontal mirror axis.
    if (mirrorDrag)
    {
        auto guideTop    = toCanvas (0.5, 0.0);
        auto guideBottom = toCanvas (0.5, 1.0);
        g.setColour (juce::Colour (0xff8cd6ef).withAlpha (0.45f)); // accentIce, 45%
        const float dashes[] = { 6.0f, 4.0f };
        g.drawDashedLine (juce::Line<float> (guideTop.x, guideTop.y, guideBottom.x, guideBottom.y),
                          dashes, 2, 1.0f);
    }
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

    const float beamLen = RING_R * 3.2f;

    auto isSpeakerIndex = [] (int i) { return i <= 1; };

    // Pick the pattern used for mic beam arc. MAIN uses p.mic_pattern; OUTRIG/
    // AMBIENT use their own pattern strings from IRSynthParams.
    auto patternForMicIndex = [&p] (int i) -> std::string
    {
        switch (groupFor (i))
        {
            case Group::Main:    return p.mic_pattern;
            case Group::Outrig:  return p.outrig_pattern;
            case Group::Ambient: return p.ambient_pattern;
            default:             return {};
        }
    };

    // Beam passes — iterate all 8 indices, skip hidden ones.
    for (int i = 0; i < 8; ++i)
    {
        if (! transducerVisible (i)) continue;

        const float cx = toCanvas (transducers.cx[i], transducers.cy[i]).x;
        const float cy = toCanvas (transducers.cx[i], transducers.cy[i]).y;
        const float ang = (float) transducers.angle[i];

        double arcHalf = isSpeakerIndex (i) ? (0.6 * kPi) : (kPi * 0.65 * 0.5);
        if (! isSpeakerIndex (i))
        {
            const auto pat = patternForMicIndex (i);
            if      (pat == "omni")                   arcHalf = kPi * 0.5;
            else if (pat == "omni (MK2H)")            arcHalf = kPi * 0.5;
            else if (pat == "subcardioid")            arcHalf = kPi * 0.75 * 0.5;
            else if (pat == "wide cardioid (MK21)")   arcHalf = kPi * 0.70 * 0.5;
            else if (pat == "figure8")                arcHalf = kPi * 0.25;
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
        g.setColour (colourFor (i).withAlpha (isSpeakerIndex (i) ? 0.18f : 0.16f));
        g.fillPath (beam);
    }

    juce::Path spkPath = makeSpeakerIconPath();
    juce::Path micPath = makeMicIconPath();

    for (int i = 0; i < 8; ++i)
    {
        if (! transducerVisible (i)) continue;

        auto pt = toCanvas (transducers.cx[i], transducers.cy[i]);
        auto col = colourFor (i);
        g.setColour (col.withAlpha (0.22f));
        g.drawEllipse (pt.x - RING_R, pt.y - RING_R, RING_R * 2, RING_R * 2, RING_W);

        float tickX = pt.x + std::cos ((float) transducers.angle[i]) * RING_R;
        float tickY = pt.y + std::sin ((float) transducers.angle[i]) * RING_R;
        g.setColour (col);
        g.fillEllipse (tickX - 3.5f, tickY - 3.5f, 7, 7);

        const juce::Path& iconPath = isSpeakerIndex (i) ? spkPath : micPath;
        drawTransducerIcon (g, iconPath, pt.x, pt.y, (float) transducers.angle[i], CORE_R, col);

        // Small M/O/A label next to the ring for non-speaker mics. Placed
        // to the upper-right of the ring so it does not overlap the beam.
        const char* letter = labelForMic (i);
        if (letter[0] != 0)
        {
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.setColour (col.withAlpha (0.95f));
            g.drawText (letter,
                        (int) (pt.x + RING_R * 0.8f),
                        (int) (pt.y - RING_R * 1.1f),
                        12, 12,
                        juce::Justification::centredLeft, false);
        }
    }

    // Decca Tree puck (when visible). The tree is a rigid array of 3 mics
    // (L, C, R) rotated by transducers.deccaAngle around the tree centre at
    // (deccaCx, deccaCy). Classical geometry — see kDeccaOuterM / kDeccaAdvanceM.
    if (deccaVisible)
    {
        const double ang = transducers.deccaAngle;
        const double fx = std::cos (ang), fy = std::sin (ang);     // forward unit vector
        const double rx = -std::sin (ang), ry = std::cos (ang);    // right unit vector
        // Metre offsets → normalised (divide by room extents).
        const double halfA_nx = (rx * (kDeccaOuterM * 0.5)) / rw;
        const double halfA_ny = (ry * (kDeccaOuterM * 0.5)) / rd;
        const double adv_nx   = (fx * kDeccaAdvanceM) / rw;
        const double adv_ny   = (fy * kDeccaAdvanceM) / rd;

        const double Lnx = transducers.deccaCx - halfA_nx;
        const double Lny = transducers.deccaCy - halfA_ny;
        const double Rnx = transducers.deccaCx + halfA_nx;
        const double Rny = transducers.deccaCy + halfA_ny;
        const double Cnx = transducers.deccaCx + adv_nx;
        const double Cny = transducers.deccaCy + adv_ny;

        auto centrePt = toCanvas (transducers.deccaCx, transducers.deccaCy);
        auto Lpt      = toCanvas (Lnx, Lny);
        auto Rpt      = toCanvas (Rnx, Rny);
        auto Cpt      = toCanvas (Cnx, Cny);

        // Forward-facing beam on the whole tree (wide, omni-ish) so users see
        // where the array is pointing. Same arc shape as a MAIN omni mic.
        {
            const float beamLenD = RING_R * 3.2f;
            const double arcHalf = kPi * 0.5;
            juce::Path beam;
            beam.startNewSubPath (centrePt.x, centrePt.y);
            const int nSeg = 16;
            for (int s = 0; s <= nSeg; ++s)
            {
                const float t = (float) s / (float) nSeg;
                const float theta = (float) (ang - arcHalf + t * 2 * arcHalf);
                beam.lineTo (centrePt.x + beamLenD * std::cos (theta),
                             centrePt.y + beamLenD * std::sin (theta));
            }
            beam.closeSubPath();
            g.setColour (colDecca.withAlpha (0.14f));
            g.fillPath (beam);
        }

        // Light connecting line to show it's a rigid array (L—C—R).
        g.setColour (colDecca.withAlpha (0.35f));
        g.drawLine (Lpt.x, Lpt.y, Cpt.x, Cpt.y, 1.2f);
        g.drawLine (Cpt.x, Cpt.y, Rpt.x, Rpt.y, 1.2f);

        // Per-mic face ticks (toe-out) — L/R mics rotate ±toe from the tree's
        // forward axis; C always looks straight forward. Mirrors the engine
        // logic in synthMainPath / synthDirectPath so the display matches what
        // the convolution is actually doing. Pulled from IRSynthParams so the
        // tick live-updates when the user drags the Toe-out slider.
        double toe = kPi * 0.5;   // fallback default (±90°) when getParams is not wired
        if (getParams)
        {
            const auto p = getParams();
            toe = juce::jlimit (0.0, kPi * 0.5, p.decca_toe_out);
        }
        const double faceLAng = ang - toe;
        const double faceRAng = ang + toe;
        const double faceCAng = ang;

        // L/C/R dots with an outward-pointing face tick (2–3 px tick from the
        // dot edge) to make the individual mic orientations unmistakable.
        auto drawDotWithFace = [&] (juce::Point<float> pt, const char* lbl, double faceAng)
        {
            g.setColour (colDecca);
            g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7.0f, 7.0f);

            // Face tick — short line from dot edge outwards along faceAng.
            const float tickStart = 3.5f;
            const float tickLen   = 10.0f;
            const float fx2 = (float) std::cos (faceAng);
            const float fy2 = (float) std::sin (faceAng);
            g.setColour (colDecca.withAlpha (0.85f));
            g.drawLine (pt.x + fx2 * tickStart, pt.y + fy2 * tickStart,
                        pt.x + fx2 * (tickStart + tickLen), pt.y + fy2 * (tickStart + tickLen),
                        1.4f);
            // Small arrow-head cap at the end of the tick.
            const float headAng = 0.45f;   // radians half-spread
            const float headLen = 3.2f;
            const float hx = pt.x + fx2 * (tickStart + tickLen);
            const float hy = pt.y + fy2 * (tickStart + tickLen);
            const float h1x = hx - headLen * (float) std::cos (faceAng - headAng);
            const float h1y = hy - headLen * (float) std::sin (faceAng - headAng);
            const float h2x = hx - headLen * (float) std::cos (faceAng + headAng);
            const float h2y = hy - headLen * (float) std::sin (faceAng + headAng);
            g.drawLine (hx, hy, h1x, h1y, 1.2f);
            g.drawLine (hx, hy, h2x, h2y, 1.2f);

            g.setColour (colDecca.withAlpha (0.95f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText (lbl, (int) (pt.x - 10), (int) (pt.y + 4), 20, 10,
                        juce::Justification::centred, false);
        };
        drawDotWithFace (Lpt, "L", faceLAng);
        drawDotWithFace (Cpt, "C", faceCAng);
        drawDotWithFace (Rpt, "R", faceRAng);

        // Ring + rotation tick + mic icon at the tree centre.
        g.setColour (colDecca.withAlpha (0.22f));
        g.drawEllipse (centrePt.x - RING_R, centrePt.y - RING_R,
                       RING_R * 2, RING_R * 2, RING_W);
        const float tickX = centrePt.x + std::cos ((float) ang) * RING_R;
        const float tickY = centrePt.y + std::sin ((float) ang) * RING_R;
        g.setColour (colDecca);
        g.fillEllipse (tickX - 3.5f, tickY - 3.5f, 7, 7);
        drawTransducerIcon (g, micPath, centrePt.x, centrePt.y, (float) ang, CORE_R, colDecca);

        // "D" label at upper-right of the ring.
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.setColour (colDecca.withAlpha (0.95f));
        g.drawText ("D",
                    (int) (centrePt.x + RING_R * 0.8f),
                    (int) (centrePt.y - RING_R * 1.1f),
                    12, 12,
                    juce::Justification::centredLeft, false);
    }

    // Legend: colour guide. Base rows (Spk L/R, Mic L/R) are always shown.
    // OUTRIG and AMBIENT rows are appended only when visible.
    struct LegRow { const char* label; int index; };
    std::vector<LegRow> rows = {
        { "Spk L", 0 }, { "Spk R", 1 },
    };
    // When Decca is visible, show a single "Decca" legend row in place of the
    // Mic L/R rows (MAIN mic pucks are hidden).
    if (deccaVisible)
        rows.push_back ({ "Decca", 2 }); // index 2 used only for icon positioning
    else
    {
        rows.push_back ({ "Mic L", 2 });
        rows.push_back ({ "Mic R", 3 });
    }
    if (outrigVisible)  { rows.push_back ({ "Out L", 4 }); rows.push_back ({ "Out R", 5 }); }
    if (ambientVisible) { rows.push_back ({ "Amb L", 6 }); rows.push_back ({ "Amb R", 7 }); }

    const float legX = 8.0f;
    const float legY = 8.0f;
    const float legIconSz = 10.0f;
    const float legRow = 14.0f;
    const juce::Colour legText { 0xc0ffffff };
    g.setFont (juce::FontOptions (9.0f));

    for (size_t r = 0; r < rows.size(); ++r)
    {
        const auto& row = rows[r];
        const int i = row.index;
        const float iconCx = legX + legIconSz * 0.5f;
        const float yy = legY + ((float) r + 0.5f) * legRow;
        const juce::Path& legIcon = isSpeakerIndex (i) ? spkPath : micPath;
        drawTransducerIcon (g, legIcon, iconCx, yy, 0.0f, legIconSz * 0.5f, colourFor (i));

        g.setColour (legText);
        g.drawText (row.label, (int) (legX + legIconSz + 6), (int) (legY + (float) r * legRow), 30, (int) legRow,
                    juce::Justification::centredLeft, false);

        // Angle readout — 0° = north (up), +CW, −CCW, ±180° = south (audience).
        // When Decca is visible, the row labelled "Decca" (index 2) uses the
        // tree angle rather than the (hidden) MAIN L mic angle.
        const bool isDeccaRow = deccaVisible && juce::String (row.label) == "Decca";
        const double angSource = isDeccaRow ? transducers.deccaAngle : transducers.angle[i];
        double displayDeg = (angSource + kPi / 2.0) * 180.0 / kPi;
        while (displayDeg >  180.0) displayDeg -= 360.0;
        while (displayDeg <= -180.0) displayDeg += 360.0;
        juce::String degStr = juce::String ((int) std::round (displayDeg))
                              + juce::String::fromUTF8 (u8"\u00b0");
        g.setColour (colourFor (i).withAlpha (0.85f));
        // "Decca" label is wider than "Mic L" etc., so shift the angle readout.
        const int angOffset = isDeccaRow ? 58 : 50;
        g.drawText (degStr, (int) (legX + legIconSz + angOffset), (int) (legY + (float) r * legRow), 36, (int) legRow,
                    juce::Justification::centredLeft, false);
    }
}
