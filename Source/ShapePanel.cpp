#include "ShapePanel.h"
#include "TimePresets.h"

using namespace triad::ui;
namespace mod = triad::mod;

triad::mod::Shape ShapeEditor::clipboard;

namespace
{
constexpr float pointRadius = 4.5f;

// A preset shape in the palette under the grid, drawn as a small picture. Right-click: onMenu (USER shapes).
class ShapeButton final : public juce::Button
{
public:
    ShapeButton (const juce::String& name, mod::Shape s, juce::Colour c) : Button (name), shape (std::move (s)), colour (c)
    {
        setTooltip (name + " (replaces the shape; the heart: FAVORITES)");
    }
    std::function<void()> onMenu;
    std::function<void()> onHeart;   // the heart in the lower left corner (FAVORITES)
    bool favourite = false;
    bool showName = false;           // the browser's tiles (0.16.1): the name under the shape, the heart beside it
    bool current = false;            // the shape on the grid: outlined
    const mod::Shape& getShape() const noexcept { return shape; }
    juce::Rectangle<float> heartArea() const
    {
        return showName ? juce::Rectangle<float> (9.0f, (float) getHeight() - 20.0f, 13.0f, 12.0f)
                        : juce::Rectangle<float> (3.0f, (float) getHeight() - 15.0f, 12.0f, 12.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu) { onMenu(); return; }
        if (onHeart && heartArea().expanded (3.0f).contains (e.position)) { onHeart(); return; }   // only the heart: the shape stays
        Button::mouseDown (e);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool over = onHeart && heartArea().expanded (3.0f).contains (e.position);
        if (over != overHeart) { overHeart = over; repaint(); }
        Button::mouseMove (e);
    }
    void mouseExit (const juce::MouseEvent& e) override { overHeart = false; Button::mouseExit (e); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto fill = showName ? (down ? currentPalette.controlsOn : over ? currentPalette.controls.brighter (0.08f) : currentPalette.controls)
                                   : (down ? currentPalette.controlsOn : over ? slotRowHover : currentPalette.paletteButton);
        g.setColour (fill);
        g.fillRoundedRectangle (r, showName ? 8.0f : 4.0f);
        const auto area = showName ? r.reduced (12.0f, 10.0f).withTrimmedBottom (18.0f) : r.reduced (6.0f, 5.0f);
        // Drawn from the shape's own points (steps exactly, curves finely), so dense shapes stay crisp.
        juce::Path p;
        auto at = [&area] (float x, float y) { return juce::Point<float> (area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y); };
        for (size_t i = 0; i < shape.size(); ++i)
        {
            const auto& b = shape[i];
            if (i == 0) { p.startNewSubPath (at (b.x, b.y)); continue; }
            const auto& a = shape[i - 1];
            const float width = (b.x - a.x) * area.getWidth();
            if (std::abs (a.curve) > 1.0e-3f && width > 1.0f)
                for (int k = 1; k < juce::jmax (2, (int) (width / 1.5f)); ++k)
                {
                    const float t = (float) k / (float) juce::jmax (2, (int) (width / 1.5f));
                    p.lineTo (at (a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * mod::curveFraction (t, a.curve)));
                }
            p.lineTo (at (b.x, b.y));
        }
        g.setColour (over || current ? colour : textColour.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (shape.size() > 40 ? 1.1f : (showName ? 1.8f : 1.6f), juce::PathStrokeType::beveled, juce::PathStrokeType::butt));
        if (showName)
        {
            g.setColour (current ? textColour : dimTextColour);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawFittedText (getName(), juce::Rectangle<float> (heartArea().getRight() + 6.0f, (float) getHeight() - 24.0f,
                                                                  r.getWidth() - heartArea().getRight() - 12.0f, 20.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
        if (onHeart)
        {
            // A small patch of the button's own colour under the heart, so the shape's line does not run through it.
            g.setColour (fill.withAlpha (0.9f));
            g.fillRoundedRectangle (heartArea().expanded (1.5f), 3.0f);
            drawHeart (g, heartArea(), favourite, overHeart, dimTextColour.withAlpha (over ? 0.8f : 0.5f));
        }
        if (current && showName)
        {
            g.setColour (triad::ui::menuColours().chosen);
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);
        }
    }
private:
    mod::Shape shape;
    juce::Colour colour;
    bool overHeart = false;
};

// The generator's big button: a dice and NEW SHAPE.
class GenerateButton final : public juce::Button
{
public:
    GenerateButton() : Button ("New shape")
    {
        setTooltip ("Draw a new shape in this style, on the grid (Undo brings the old one back)");
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (accent.withAlpha (down ? 0.55f : over ? 0.42f : 0.3f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        auto area = r.reduced (10.0f, 6.0f);
        const auto label = juce::String ("NEW SHAPE");
        const juce::Font font (juce::FontOptions (13.0f, juce::Font::bold));
        const float textWidth = juce::GlyphArrangement::getStringWidth (font, label);
        const float iconSize = area.getHeight();
        auto content = area.withSizeKeepingCentre (iconSize + 8.0f + textWidth, area.getHeight());
        triad::ui::IconButton::drawIcon (g, triad::ui::IconButton::Icon::dice, content.removeFromLeft (iconSize), textColour);
        content.removeFromLeft (8.0f);
        g.setColour (textColour);
        g.setFont (font);
        g.drawText (label, content, juce::Justification::centredLeft, false);
    }
};
}

namespace
{
// The green check (like) and the red X (dislike) under the dice, with how many shapes you rated so.
class RateButton final : public juce::Button
{
public:
    explicit RateButton (bool likeIt) : Button (likeIt ? "Like" : "Dislike"), like (likeIt) {}
    void setCount (int n) { if (n != count) { count = n; repaint(); } }
    void flash() { lit = juce::Time::getMillisecondCounterHiRes(); repaint(); juce::Timer::callAfterDelay (600, [safe = juce::Component::SafePointer<RateButton> (this)] { if (safe != nullptr) safe->repaint(); }); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto colour = like ? juce::Colour (0xff5fd068) : juce::Colour (0xffe8594f);
        const bool glowing = juce::Time::getMillisecondCounterHiRes() - lit < 550.0;
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colour.withAlpha (! isEnabled() ? 0.06f : down || glowing ? 0.5f : over ? 0.28f : 0.16f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colour.withAlpha (isEnabled() ? 0.85f : 0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        // The mark, drawn (no font needed), then the count.
        const float size = r.getHeight() * 0.46f;
        auto content = r.withSizeKeepingCentre (size + (count > 0 ? 30.0f : 0.0f), size);
        const auto mark = content.removeFromLeft (size);
        juce::Path p;
        if (like)
        {
            p.startNewSubPath (mark.getX(), mark.getCentreY());
            p.lineTo (mark.getX() + size * 0.38f, mark.getBottom());
            p.lineTo (mark.getRight(), mark.getY());
        }
        else
        {
            p.startNewSubPath (mark.getTopLeft()); p.lineTo (mark.getBottomRight());
            p.startNewSubPath (mark.getTopRight()); p.lineTo (mark.getBottomLeft());
        }
        g.setColour (isEnabled() ? colour : colour.withAlpha (0.35f));
        g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (count > 0)
        {
            g.setColour (textColour.withAlpha (isEnabled() ? 0.9f : 0.4f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String (count), content.withTrimmedLeft (6.0f).expanded (0.0f, 4.0f), juce::Justification::centredLeft);
        }
    }
private:
    const bool like;
    int count = 0;
    double lit = 0.0;
};
}

// ---- Shape editor -----------------------------------------------------------------------------------
void ShapeEditor::setHost (ShapeHost* h)
{
    host = h;
    drag = Drag::none;
    reload();
}
juce::Colour ShapeEditor::shapeColour() const { return host != nullptr ? host->getColour() : accent; }
bool ShapeEditor::undo()
{
    if (host == nullptr || ! host->undo()) return false;
    reload();
    if (onChange) onChange();
    return true;
}
bool ShapeEditor::redo()
{
    if (host == nullptr || ! host->redo()) return false;
    reload();
    if (onChange) onChange();
    return true;
}
void ShapeEditor::reload()
{
    if (drag != Drag::none) return;
    shape = host != nullptr ? host->getShape() : mod::flat();
    if (shape.size() < 2) shape = mod::flat();
    selected.assign (shape.size(), false);
    repaint();
}
void ShapeEditor::commit (bool undoable)
{
    if (host != nullptr) host->setShape (shape, undoable);
    if (selected.size() != shape.size()) selected.assign (shape.size(), false);
    repaint();
    if (onChange) onChange();
}
void ShapeEditor::apply (const std::function<mod::Shape (const mod::Shape&)>& operation)
{
    auto next = operation (shape);
    mod::normalise (next);
    shape = std::move (next);
    selected.assign (shape.size(), false);
    commit (true);
}
juce::Rectangle<float> ShapeEditor::plot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedTop (20.0f).withTrimmedRight (10.0f).withTrimmedBottom (10.0f);
}
float ShapeEditor::plotAspect() const
{
    const auto r = plot();
    return r.getWidth() > 1.0f && r.getHeight() > 1.0f ? r.getWidth() / r.getHeight() : mod::plotAspect;
}
int ShapeEditor::columns() const
{
    const double beats = host != nullptr ? mod::lengthInBeats (host->getLength(), 4.0) : 4.0;
    if (beats <= 0.0) return 16;
    const double columnBeats = 4.0 / gridDivision * (triplet ? 2.0 / 3.0 : 1.0);
    return juce::jlimit (8, 128, (int) std::round (beats / columnBeats));
}
bool ShapeEditor::isSquareGrid() const { return host != nullptr && host->squareGrid(); }
int ShapeEditor::rows() const { return isSquareGrid() ? 16 : 8; }
int ShapeEditor::snapRows() const { return 16; }
juce::Point<float> ShapeEditor::toScreen (const mod::Point& p) const
{
    const auto r = plot();
    return { r.getX() + r.getWidth() * p.x, r.getBottom() - r.getHeight() * p.y };
}
mod::Point ShapeEditor::snapPoint (mod::Point p, bool snapping) const
{
    p.x = juce::jlimit (0.0f, 1.0f, p.x);
    p.y = juce::jlimit (0.0f, 1.0f, p.y);
    if (snapping)
    {
        const auto c = (float) columns();
        p.x = std::round (p.x * c) / c;
        const auto r = (float) snapRows();
        p.y = std::round (p.y * r) / r;
    }
    return p;
}
juce::Point<float> ShapeEditor::curveHandle (int i) const
{
    const auto& a = shape[(size_t) i];
    const auto& b = shape[(size_t) i + 1];
    const float y = a.y + (b.y - a.y) * mod::curveFraction (0.5f, a.curve);
    return toScreen ({ 0.5f * (a.x + b.x), y, 0.0f });
}
int ShapeEditor::hitPoint (juce::Point<float> pos) const
{
    int best = -1;
    float distance = 9.0f;
    for (size_t i = 0; i < shape.size(); ++i)
    {
        const auto d = toScreen (shape[i]).getDistanceFrom (pos);
        if (d <= distance) { distance = d; best = (int) i; }
    }
    return best;
}
int ShapeEditor::hitCurve (juce::Point<float> pos) const
{
    for (size_t i = 0; i + 1 < shape.size(); ++i)
    {
        const auto a = toScreen (shape[i]), b = toScreen (shape[i + 1]);
        if (b.x - a.x < 14.0f || std::abs (b.y - a.y) < 6.0f) continue;
        if (curveHandle ((int) i).getDistanceFrom (pos) <= 7.0f) return (int) i;
    }
    return -1;
}

void ShapeEditor::frame (double time)
{
    // Every frame of the screen, up to 120 a second (ProMotion): the playing position, carried on smoothly between
    // the audio's updates.
    if (! pacer->due (lastFrame, time)) return;
    const auto next = host != nullptr ? host->getPhaseNow() : -1.0f;
    if (host != nullptr && (host->hasBackdrop() || hadBackdrop))
    {
        // The sound behind the grid moves: the whole grid, at up to 60 frames a second.
        hadBackdrop = host->hasBackdrop();
        phase = next;
        if (time - lastBackdrop >= 1.0 / 61.0) { lastBackdrop = time; repaint(); }
        return;
    }
    if (std::abs (next - phase) <= 1.0e-4f) return;
    // Repaint only the strips under the old and the new playing position.
    const auto strip = [this] (float p)
    {
        if (p < 0.0f) return juce::Rectangle<int>();
        const int x = juce::roundToInt (toScreen ({ p, 0.0f, 0.0f }).x);
        return juce::Rectangle<int> (x - 7, 0, 14, getHeight());
    };
    repaint (strip (phase));
    phase = next;
    repaint (strip (phase));
}
void ShapeEditor::paint (juce::Graphics& g)
{
    const triad::ui::FramePacer::Timed timed (*pacer);
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    auto key = (juce::uint64) (juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (scale) + "/" + juce::String ((juce::int64) shapeColour().getARGB())
                               + "/" + juce::String (columns()) + "/" + juce::String (hoverPoint) + "/" + juce::String (hoverCurve)
                               + "/t" + juce::String (themeGeneration)).hashCode64();
    auto mix = [&key] (int v) { key = key * 1099511628211ull + (juce::uint64) (juce::uint32) v; };
    for (const auto& p : shape) { mix (juce::roundToInt (p.x * 1.0e6f)); mix (juce::roundToInt (p.y * 1.0e6f)); mix (juce::roundToInt (p.curve * 1.0e4f)); }
    for (bool b : selected) mix (b ? 7 : 3);
    const int labelMode = host != nullptr ? host->segmentLabelMode() : 0;
    mix (labelMode);
    const bool backdrop = host != nullptr && host->hasBackdrop();
    mix (backdrop ? 11 : 13);
    if (labelMode == 2) { mix (drag == Drag::points ? dragIndex : -2); }
    if ((juce::int64) key != layerKey || ! layer.isValid())
    {
        layerKey = (juce::int64) key;
        ++layerRenders;
        layer = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                             juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), true);
        juce::Graphics lg (layer);
        lg.addTransform (juce::AffineTransform::scale (scale));
        paintLayer (lg);
    }
    if (host != nullptr && host->hasBackdrop())
    {
        // The sound coming in, under the grid (the picture over it is see-through where it shows the grid's background).
        g.setColour (gridBackground);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot().toNearestInt());
        host->paintBackdrop (g, plot());
    }
    triad::ui::drawPixelExact (g, layer, scale);
    const auto colour = shapeColour();
    if (drag == Drag::select)
    {
        g.setColour (colour.withAlpha (0.12f)); g.fillRect (selection);
        g.setColour (colour.withAlpha (0.7f)); g.drawRect (selection, 1.0f);
    }
    // While a point is dragged: what the straight lines either side of it do (Triad Time's TIME: their pitch).
    if (drag == Drag::points && host != nullptr && host->segmentLabelMode() > 0 && dragIndex >= 0 && dragIndex < (int) shape.size())
        for (int i : { dragIndex - 1, dragIndex })
            drawSegmentLabel (g, i, false);
    // Playing position.
    if (phase >= 0.0f)
    {
        const auto r = plot();
        const auto p = toScreen ({ phase, mod::evaluate (shape, phase), 0.0f });
        g.setColour (currentPalette.marker.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (p.x - 0.75f, r.getY(), 1.5f, r.getHeight()));   // at sub-pixel positions: glides
        g.setColour (currentPalette.marker);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    }
}
void ShapeEditor::paintLayer (juce::Graphics& g)
{
    const auto r = plot();
    const auto colour = shapeColour();
    if (host == nullptr || ! host->hasBackdrop())   // with a backdrop, paint() fills it first and draws the sound on it
    {
        g.setColour (gridBackground);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }
    const int cols = columns();
    if (isSquareGrid())
    {
        // Triad Time, laid out as ShaperBox's grid: the quarters of both directions make 4 x 4 big squares. Across, each
        // quarter is divided in 4 by bright lines and each of those in 4 again by faint ones (64 columns at GRID 1/64 for a
        // 1-bar pattern); down, each quarter in 4 by faint lines (16 rows).
        for (int c = 0; c <= cols; ++c)
        {
            const float alpha = (c * 4) % cols == 0 ? 0.40f : (c * 16) % cols == 0 ? 0.26f : 0.13f;
            g.setColour (currentPalette.gridLines.withAlpha (alpha));
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) c / (float) cols), r.getY(), r.getBottom());
        }
        for (int row = 0; row <= 16; ++row)
        {
            g.setColour (currentPalette.gridLines.withAlpha (row % 4 == 0 ? 0.40f : 0.13f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * (float) row / 16.0f), r.getX(), r.getRight());
        }
    }
    // Grid: columns, beats and quarters of the cycle; eight rows.
    else for (int c = 0; c <= cols; ++c)
    {
        const float x = r.getX() + r.getWidth() * (float) c / (float) cols;
        const bool quarter = (c * 4) % cols == 0;
        g.setColour (currentPalette.gridLines.withAlpha (quarter ? 0.22f : 0.07f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
    }
    if (! isSquareGrid())
        for (int row = 0; row <= 8; ++row)
        {
            const float y = r.getY() + r.getHeight() * (float) row / 8.0f;
            g.setColour (currentPalette.gridLines.withAlpha (row % 4 == 0 ? 0.2f : 0.07f));
            g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
        }
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (dimTextColour);
    if (isSquareGrid())
    {
        // As ShaperBox: 0, 1/4 ... 4/4 across the top; 0, -2/4 and -4/4 down the side.
        for (int q = 0; q <= 4; ++q)
            g.drawText (juce::String (q) + (q == 0 ? "" : "/4"),
                        juce::Rectangle<float> (r.getX() + r.getWidth() * (float) q / 4.0f - (q == 0 ? 0.0f : q == 4 ? 40.0f : 20.0f), 2.0f, 40.0f, 16.0f),
                        q == 0 ? juce::Justification::centredLeft : q == 4 ? juce::Justification::centredRight : juce::Justification::centred);
        for (int v : { 100, 50, 0 })
            g.drawText (host->valueLabel (v), juce::Rectangle<float> (0.0f, r.getBottom() - r.getHeight() * (float) v / 100.0f - 8.0f, 34.0f, 16.0f),
                        juce::Justification::centredRight);
    }
    else
    {
        for (int q = 1; q < 4; ++q)
            g.drawText (juce::String (q) + "/4", juce::Rectangle<float> (r.getX() + r.getWidth() * (float) q / 4.0f - 20.0f, 2.0f, 40.0f, 16.0f),
                        juce::Justification::centred);
        for (int v : { 100, 75, 50, 25, 0 })
            g.drawText (host != nullptr ? host->valueLabel (v) : juce::String (v) + "%", juce::Rectangle<float> (0.0f, r.getBottom() - r.getHeight() * (float) v / 100.0f - 8.0f, 34.0f, 16.0f),
                        juce::Justification::centredRight);
    }
    // The shape: hatched fill under a solid line (Triad Time's TIME: the triangle below the diagonal, as ShaperBox).
    juce::Path line, fill;
    fill.startNewSubPath (r.getX(), r.getBottom());
    const int steps = juce::jmax (64, (int) r.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps;
        const auto p = toScreen ({ x, mod::evaluate (shape, x), 0.0f });
        if (i == 0) line.startNewSubPath (p); else line.lineTo (p);
        fill.lineTo (p);
    }
    // Vertical steps are drawn exactly.
    for (size_t i = 0; i + 1 < shape.size(); ++i)
        if (juce::exactlyEqual (shape[i].x, shape[i + 1].x))
        {
            line.startNewSubPath (toScreen (shape[i]));
            line.lineTo (toScreen (shape[i + 1]));
        }
    fill.lineTo (r.getRight(), r.getBottom());
    fill.closeSubPath();
    if (host != nullptr && host->shadeBelowDiagonal())
    {
        // ShaperBox's Time view: broad faint stripes in the triangle under the corner-to-corner diagonal (further back than
        // the cycle has played), its edge drawn faintly; nothing under the line itself.
        fill.clear();
        fill.addTriangle (r.getTopLeft(), r.getBottomRight(), r.getBottomLeft());
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (fill);
        g.setColour (currentPalette.gridLines.withAlpha (0.045f));
        for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 8.0f)
            g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 3.5f);
    }
    else
    {
        g.setColour (colour.withAlpha (0.12f));
        g.fillPath (fill);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (fill);
        g.setColour (colour.withAlpha (0.1f));
        for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 9.0f)
            g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
    }
    if (host != nullptr && host->shadeBelowDiagonal())
    {
        g.setColour (currentPalette.gridLines.withAlpha (0.12f));
        g.drawLine ({ r.getTopLeft(), r.getBottomRight() }, 1.2f);
    }
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Labels on every line that has room for one (the lines either side of a point being dragged get theirs on top).
    if (host != nullptr && host->segmentLabelMode() == 2)
        for (int i = 0; i + 1 < (int) shape.size(); ++i)
            if (drag != Drag::points || (i != dragIndex - 1 && i != dragIndex))
                drawSegmentLabel (g, i, true);
    // Curve handles and points.
    for (size_t i = 0; i + 1 < shape.size(); ++i)
    {
        const auto a = toScreen (shape[i]), b = toScreen (shape[i + 1]);
        if (b.x - a.x < 14.0f || std::abs (b.y - a.y) < 6.0f) continue;
        const auto h = curveHandle ((int) i);
        g.setColour (gridBackground); g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (h));
        g.setColour (colour.withAlpha (hoverCurve == (int) i ? 1.0f : 0.7f));
        g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (h), 1.4f);
    }
    for (size_t i = 0; i < shape.size(); ++i)
    {
        const auto p = toScreen (shape[i]);
        const bool sel = i < selected.size() && selected[i];
        const float radius = pointRadius + (hoverPoint == (int) i ? 1.5f : 0.0f);
        g.setColour (sel ? colour : currentPalette.marker);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
        if (sel) { g.setColour (currentPalette.marker); g.drawEllipse (juce::Rectangle<float> (radius * 2.0f + 3.0f, radius * 2.0f + 3.0f).withCentre (p), 1.2f); }
    }
}

bool ShapeEditor::drawSegmentLabel (juce::Graphics& g, int i, bool onlyIfRoom) const
{
    if (host == nullptr || i < 0 || i + 1 >= (int) shape.size() || ! juce::exactlyEqual (shape[(size_t) i].curve, 0.0f)) return false;
    const auto text = host->segmentLabel (shape[(size_t) i], shape[(size_t) i + 1]);
    if (text.isEmpty()) return false;
    const auto r = plot();
    const auto a = toScreen (shape[(size_t) i]), b = toScreen (shape[(size_t) i + 1]);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 10.0f;
    if (onlyIfRoom && b.x - a.x < w + 6.0f) return false;
    // Above the middle of the line, or under it when the line runs along the top.
    const auto middle = (a + b) * 0.5f;
    const bool above = middle.y - 24.0f >= r.getY();
    const auto box = juce::Rectangle<float> (w, 17.0f).withCentre (middle.translated (0.0f, above ? -14.0f : 14.0f)).constrainedWithin (r);
    const auto colour = shapeColour();
    g.setColour (gridBackground.withAlpha (0.9f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
    g.drawText (text, box, juce::Justification::centred);
    return true;
}

void ShapeEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = hitPoint (e.position), c = p < 0 ? hitCurve (e.position) : -1;
    if (p != hoverPoint || c != hoverCurve) { hoverPoint = p; hoverCurve = c; repaint(); }
    setMouseCursor (p >= 0 ? juce::MouseCursor::DraggingHandCursor : c >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                                                         : juce::MouseCursor::CrosshairCursor);
}
void ShapeEditor::mouseDown (const juce::MouseEvent& e)
{
    if (isShowing()) grabKeyboardFocus();
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (selected.size() != shape.size()) selected.assign (shape.size(), false);
    mouseStart = e.position;
    dragStart = shape;
    if (const int p = hitPoint (e.position); p >= 0)
    {
        if (e.mods.isShiftDown()) selected[(size_t) p] = ! selected[(size_t) p];
        else if (! selected[(size_t) p]) { selected.assign (shape.size(), false); selected[(size_t) p] = true; }
        drag = Drag::points;
        dragIndex = p;
        if (host != nullptr) host->beginEdit();
    }
    else if (const int c = hitCurve (e.position); c >= 0)
    {
        drag = Drag::curve;
        dragIndex = c;
        if (host != nullptr) host->beginEdit();
    }
    else
    {
        drag = Drag::select;
        selectionStart = e.mods.isShiftDown() ? selected : std::vector<bool> (shape.size(), false);
        selection = {};
    }
    repaint();
}
void ShapeEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto r = plot();
    if (drag == Drag::points && dragIndex >= 0 && dragIndex < (int) dragStart.size())
    {
        const bool snapping = snap && ! e.mods.isAltDown() && ! e.mods.isCommandDown();
        const auto& anchor = dragStart[(size_t) dragIndex];
        auto target = snapPoint ({ anchor.x + (e.position.x - mouseStart.x) / r.getWidth(),
                                   anchor.y - (e.position.y - mouseStart.y) / r.getHeight(), 0.0f }, snapping);
        float dx = target.x - anchor.x, dy = target.y - anchor.y;
        // Keep the order: moved points cannot pass a point that stays put, and the ends stay at 0 and 1.
        const int last = (int) dragStart.size() - 1;
        for (int i = 0; i <= last; ++i)
        {
            if (! selected[(size_t) i]) continue;
            if (i == 0 || i == last) continue;
            const bool leftFixed = ! selected[(size_t) i - 1] || i - 1 == 0;
            const bool rightFixed = ! selected[(size_t) i + 1] || i + 1 == last;
            if (leftFixed) dx = juce::jmax (dx, dragStart[(size_t) i - 1].x - dragStart[(size_t) i].x);
            if (rightFixed) dx = juce::jmin (dx, dragStart[(size_t) i + 1].x - dragStart[(size_t) i].x);
        }
        for (int i = 0; i <= last; ++i)
            if (selected[(size_t) i])
            {
                dy = juce::jlimit (-dragStart[(size_t) i].y, 1.0f - dragStart[(size_t) i].y, dy);
            }
        shape = dragStart;
        for (int i = 0; i <= last; ++i)
            if (selected[(size_t) i])
            {
                auto& p = shape[(size_t) i];
                if (i != 0 && i != last) p.x = juce::jlimit (0.0f, 1.0f, dragStart[(size_t) i].x + dx);
                p.y = juce::jlimit (0.0f, 1.0f, dragStart[(size_t) i].y + dy);
            }
        if (host != nullptr) host->setShape (shape, false);
        repaint();
    }
    else if (drag == Drag::curve && dragIndex >= 0 && dragIndex + 1 < (int) shape.size())
    {
        auto& a = shape[(size_t) dragIndex];
        const auto& b = shape[(size_t) dragIndex + 1];
        const float value = (r.getBottom() - e.position.y) / r.getHeight();
        if (! juce::exactlyEqual (a.y, b.y))
        {
            a.curve = e.mods.isPopupMenu() ? 0.0f : mod::curveForMidpoint ((value - a.y) / (b.y - a.y));
            if (host != nullptr) host->setShape (shape, false);
            repaint();
        }
    }
    else if (drag == Drag::select)
    {
        selection = juce::Rectangle<float> (mouseStart, e.position);
        for (size_t i = 0; i < shape.size(); ++i)
            selected[i] = selectionStart[i] || selection.contains (toScreen (shape[i]));
        repaint();
    }
}
void ShapeEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::points || drag == Drag::curve)
    {
        // Points dragged on top of each other become one.
        auto cleaned = mod::removeRedundant (shape);
        if (cleaned.size() != shape.size()) { shape = cleaned; selected.assign (shape.size(), false); }
        if (host != nullptr) host->setShape (shape, false);
        if (onChange) onChange();
    }
    drag = Drag::none;
    repaint();
}
void ShapeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    drag = Drag::none;
    if (const int p = hitPoint (e.position); p >= 0)
    {
        if (p == 0 || p == (int) shape.size() - 1) return;   // the ends stay
        if (host != nullptr) host->beginEdit();
        shape.erase (shape.begin() + p);
    }
    else if (const int c = hitCurve (e.position); c >= 0)
    {
        if (host != nullptr) host->beginEdit();
        shape[(size_t) c].curve = 0.0f;
    }
    else
    {
        const auto r = plot();
        const auto p2 = snapPoint ({ (e.position.x - r.getX()) / r.getWidth(), (r.getBottom() - e.position.y) / r.getHeight(), 0.0f },
                                   snap && ! e.mods.isAltDown() && ! e.mods.isCommandDown());
        if (p2.x <= 0.0f || p2.x >= 1.0f) return;
        if (host != nullptr) host->beginEdit();
        auto at = std::upper_bound (shape.begin(), shape.end(), p2.x, [] (float x, const mod::Point& q) { return x < q.x; });
        const auto index = (size_t) std::distance (shape.begin(), at);
        mod::Point point = p2;
        if (index > 0) point.curve = shape[index - 1].curve;
        shape.insert (at, point);
    }
    selected.assign (shape.size(), false);
    commit (false);
}
void ShapeEditor::selectAll() { selected.assign (shape.size(), true); repaint(); }
void ShapeEditor::deleteSelected()
{
    if (selected.size() != shape.size()) return;
    mod::Shape kept;
    for (size_t i = 0; i < shape.size(); ++i)
        if (! selected[i] || i == 0 || i + 1 == shape.size()) kept.push_back (shape[i]);
    if (kept.size() == shape.size()) return;
    if (host != nullptr) host->beginEdit();
    shape = kept;
    selected.assign (shape.size(), false);
    commit (false);
}
int ShapeEditor::numSelected() const
{
    return selected.size() == shape.size() ? (int) std::count (selected.begin(), selected.end(), true) : 0;
}
void ShapeEditor::moveWave (int direction, bool fine)
{
    const float step = (fine ? 0.125f : 1.0f) / (float) juce::jmax (1, columns()) * (float) (direction < 0 ? -1 : 1);
    if (numSelected() == 0)
    {
        apply ([step] (const mod::Shape& s) { return mod::shifted (s, step); });
        return;
    }
    // The selected points: each run of them moves together, but not past the points either side of it (the first and
    // last points of the cycle stay at its ends).
    if (host != nullptr) host->beginEdit();
    const int n = (int) shape.size();
    int i = 1;
    while (i < n - 1)
    {
        if (! selected[(size_t) i]) { ++i; continue; }
        int j = i;
        while (j + 1 < n - 1 && selected[(size_t) j + 1]) ++j;
        const float room = step < 0.0f ? juce::jmax (step, shape[(size_t) i - 1].x - shape[(size_t) i].x)
                                       : juce::jmin (step, shape[(size_t) j + 1].x - shape[(size_t) j].x);
        for (int k = i; k <= j; ++k) shape[(size_t) k].x = juce::jlimit (0.0f, 1.0f, shape[(size_t) k].x + room);
        i = j + 1;
    }
    commit (false);
}
void ShapeEditor::copyShape()
{
    ShapeEditor::clipboard = shape;
    juce::SystemClipboard::copyTextToClipboard (mod::toText (shape));
}
void ShapeEditor::pasteShape()
{
    auto pasted = mod::fromText (juce::SystemClipboard::getTextFromClipboard());
    if (pasted.size() < 2) pasted = ShapeEditor::clipboard;
    if (pasted.size() < 2) return;
    apply ([pasted] (const mod::Shape&) { return pasted; });
}
bool ShapeEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (mods.isCommandDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) key.getKeyCode());
        if (c == 'a') { selectAll(); return true; }
        if (c == 'c') { copyShape(); return true; }
        if (c == 'v') { pasteShape(); return true; }
        if (c == 'z' && ! mods.isShiftDown()) { undo(); return true; }
        if ((c == 'z' && mods.isShiftDown()) || c == 'y') { redo(); return true; }
    }
    return false;
}
void ShapeEditor::showMenu()
{
    enum { selectAllId = 1, toGridId, flipHId, flipVId, copyId, pasteId, deleteId, tripletId, snapId, gridBase = 100 };
    juce::PopupMenu menu;
    menu.addSectionHeader ("WAVE");
    menu.addItem (selectAllId, "Select all points");
    menu.addItem (toGridId, "Move points to grid");
    menu.addItem (flipHId, "Flip horizontal");
    menu.addItem (flipVId, "Flip vertical");
    menu.addItem (copyId, "Copy wave");
    menu.addItem (pasteId, "Paste wave", ShapeEditor::clipboard.size() >= 2 || juce::SystemClipboard::getTextFromClipboard().contains ("TriadLfoShape"));
    menu.addItem (deleteId, "Delete selected points", std::count (selected.begin(), selected.end(), true) > 0);
    menu.addSectionHeader ("GRID");
    menu.addItem (tripletId, "Triplet", true, triplet);
    menu.addItem (snapId, "Snap to grid", true, snap);
    for (int d : { 4, 8, 16, 32, 64 }) menu.addItem (gridBase + d, "Grid 1/" + juce::String (d), true, gridDivision == d);
    juce::Component::SafePointer<ShapeEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto& self = *safe;
        const auto cols = self.columns();
        switch (choice)
        {
            case selectAllId: self.selectAll(); break;
            case toGridId: self.apply ([cols] (const mod::Shape& s) { return mod::snapped (s, cols, 16); }); break;
            case flipHId: self.apply (mod::reversed); break;
            case flipVId: self.apply (mod::flippedVertical); break;
            case copyId: self.copyShape(); break;
            case pasteId: self.pasteShape(); break;
            case deleteId: self.deleteSelected(); break;
            case tripletId: self.triplet = ! self.triplet; break;
            case snapId: self.snap = ! self.snap; break;
            default: if (choice > gridBase) self.gridDivision = choice - gridBase; break;
        }
        self.repaint();
        if (self.onChange) self.onChange();
    });
}

// ---- USER folder ------------------------------------------------------------------------------------------
namespace triad::userShapes
{
static constexpr const char* extension = ".triadlfo";
std::vector<Entry> load()
{
    std::vector<Entry> list;
    for (const auto& f : TriadSettings::lfoShapeFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
    {
        auto shape = mod::fromText (f.loadFileAsString());
        if (shape.size() >= 2) list.push_back ({ f.getFileNameWithoutExtension(), std::move (shape), f });
    }
    std::sort (list.begin(), list.end(), [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
    return list;
}
static juce::File freeFile (const juce::String& wanted, const juce::File& except = {})
{
    const auto folder = TriadSettings::lfoShapeFolder();
    auto base = juce::File::createLegalFileName (wanted.trim());
    if (base.isEmpty()) base = "My shape";
    auto f = folder.getChildFile (base + extension);
    for (int n = 2; f.exists() && f != except; ++n) f = folder.getChildFile (base + " " + juce::String (n) + extension);
    return f;
}
juce::File save (const juce::String& name, const mod::Shape& shape)
{
    if (shape.size() < 2 || ! TriadSettings::lfoShapeFolder().createDirectory()) return {};
    const auto f = freeFile (name);
    return f.replaceWithText (mod::toText (shape)) ? f : juce::File();
}
bool rename (const juce::File& f, const juce::String& newName)
{
    return f.existsAsFile() && f.moveFileTo (freeFile (newName, f));
}
bool remove (const juce::File& f)
{
    return f.existsAsFile() && (f.moveToTrash() || f.deleteFile());
}
}

// ---- The sound behind the grid -----------------------------------------------------------------------------------
namespace triad::ui
{
float waveHeight (float level)
{
    // As a clip in the playlist (its amplitude), lifted a little so tails and quiet sounds still show.
    return juce::jlimit (0.0f, 1.0f, std::pow (juce::jmax (0.0f, level), 0.6f));
}
void drawWaveTrace (juce::Graphics& g, juce::Rectangle<float> plot, const std::array<float, triad::WaveTrace::bins>& level,
                    const std::array<int, triad::WaveTrace::bins>& age, juce::Colour colour, float thresholdDb)
{
    const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f;
    const float step = plot.getWidth() / (float) triad::WaveTrace::bins;
    // Two passes: what is left of the cycle before (faint), then this cycle (brighter), as filled columns.
    for (int pass = 1; pass >= 0; --pass)
    {
        juce::Path wave;
        bool open = false;
        for (int i = 0; i < triad::WaveTrace::bins; ++i)
        {
            const bool shown = age[(size_t) i] == pass;
            const float h = shown ? waveHeight (level[(size_t) i]) * half : 0.0f;
            if (h < 0.5f) { open = false; continue; }
            const float x0 = plot.getX() + step * (float) i, x1 = x0 + step + 0.35f;
            wave.addRectangle (x0, mid - h, x1 - x0, 2.0f * h);
            open = true;
        }
        juce::ignoreUnused (open);
        g.setColour (colour.withAlpha (pass == 0 ? 0.30f : 0.11f));
        g.fillPath (wave);
    }
    if (! std::isnan (thresholdDb))
    {
        const float h = waveHeight (juce::Decibels::decibelsToGain (thresholdDb)) * half;
        g.setColour (colour.withAlpha (0.55f));
        const float dashes[] { 4.0f, 4.0f };
        for (float y : { mid - h, mid + h })
            g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.0f);
    }
}
}

namespace triad::ui
{
SpectrumBackdrop::SpectrumBackdrop()
    : samples ((size_t) fftSize), window ((size_t) fftSize), levels ((size_t) fftSize / 2 + 1, -200.0f), bins ((size_t) fftSize)
{
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
        windowSum += window[(size_t) i];
    }
}
void SpectrumBackdrop::paint (juce::Graphics& g, juce::Rectangle<float> plot, triad::SpectrumTap& tap, double sampleRate, float low, float high,
                              juce::Colour colour)
{
    tap.active.store (true);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float decay = (float) (36.0 * juce::jlimit (0.0, 0.25, now - lastTime));
    lastTime = now;
    const auto w = tap.getWriteIndex();
    if (w != lastWrite && tap.read (samples.data(), fftSize))
    {
        lastWrite = w;
        for (size_t i = 0; i < (size_t) fftSize; ++i) bins[i] = { samples[i] * window[i], 0.0f };
        triad::fft (bins);
        for (size_t k = 1; k < levels.size(); ++k)
        {
            const auto db = 20.0f * std::log10 (std::abs (bins[k]) * 2.0f / windowSum + 1.0e-9f);
            levels[k] = juce::jmax (db, levels[k] - decay);
        }
    }
    else for (auto& l : levels) l -= decay;
    const float binHz = (float) juce::jmax (8000.0, sampleRate) / (float) fftSize;
    const auto xFor = [&plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto hzFor = [&plot] (float x) { return 20.0f * std::pow (1000.0f, (x - plot.getX()) / plot.getWidth()); };
    const auto yFor = [&plot] (float db) { return juce::jmap (juce::jlimit (-84.0f, 6.0f, db), -84.0f, 6.0f, plot.getBottom(), plot.getY()); };
    juce::Path fill, line;
    fill.startNewSubPath (plot.getX(), plot.getBottom());
    for (float x = plot.getX(); x <= plot.getRight() + 0.5f; x += 2.0f)
    {
        const float k0 = hzFor (x - 1.0f) / binHz, k1 = hzFor (x + 1.0f) / binHz;
        float db = -200.0f;
        for (auto k = (size_t) juce::jmax (1.0f, k0); k <= (size_t) juce::jmax (1.0f, k1) && k < levels.size(); ++k)
            db = juce::jmax (db, levels[k] + 3.0f * std::log2 (juce::jmax (1.0f, (float) k * binHz) / 1000.0f));   // +3 dB/oct tilt, as the BANDS tab
        const float y = yFor (db);
        fill.lineTo (x, y);
        if (line.isEmpty()) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setColour (textColour.withAlpha (0.10f));
    g.fillPath (fill);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (juce::Rectangle<float> (xFor (low), plot.getY(), xFor (high) - xFor (low), plot.getHeight()).toNearestInt());
    g.setColour (colour.withAlpha (0.30f));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.65f));
    g.strokePath (line, juce::PathStrokeType (1.2f));
}
}

// ---- FAVORITES: the shapes marked with the heart ---------------------------------------------------------------
namespace triad::favouriteShapes
{
namespace
{
struct Cache { juce::Time modified; juce::int64 size = -1; std::vector<Entry> entries; juce::int64 version = 0; };
Cache& cache()
{
    static Cache c;
    return c;
}
std::vector<Entry>& current()
{
    // Read again only when the file changed (another window or plug-in may have marked a heart).
    auto& c = cache();
    const auto file = TriadSettings::favouriteShapesFile();
    const auto modified = file.getLastModificationTime();
    const auto size = file.existsAsFile() ? file.getSize() : 0;
    if (modified != c.modified || size != c.size)
    {
        c.modified = modified;
        c.size = size;
        c.entries.clear();
        if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("TriadFavorites"))
            for (auto* item : xml->getChildWithTagNameIterator ("Item"))
            {
                auto shape = mod::fromXml (item->getChildByName ("TriadLfoShape"));
                if (shape.size() < 2) continue;
                auto key = item->getStringAttribute ("key");
                if (key.isEmpty()) key = keyOf (shape);
                c.entries.push_back ({ item->getStringAttribute ("name"), std::move (shape), key });
            }
        ++c.version;
    }
    return c.entries;
}
bool write (const std::vector<Entry>& entries)
{
    juce::XmlElement root ("TriadFavorites");
    for (const auto& e : entries)
    {
        auto* item = root.createNewChildElement ("Item");
        item->setAttribute ("name", e.name);
        item->setAttribute ("key", e.key);
        item->addChildElement (mod::toXml (e.shape, "TriadLfoShape").release());
    }
    const auto file = TriadSettings::favouriteShapesFile();
    file.getParentDirectory().createDirectory();
    const bool ok = root.writeTo (file);
    cache().size = -1;   // read again (the new list), whatever the file system's time resolution
    current();
    return ok;
}
}
juce::String keyOf (const mod::Shape& shape) { return mod::toText (shape); }
std::vector<Entry> load() { return current(); }
bool contains (const mod::Shape& shape)
{
    const auto key = keyOf (shape);
    for (const auto& e : current()) if (e.key == key) return true;
    return false;
}
bool add (const juce::String& name, const mod::Shape& shape)
{
    if (shape.size() < 2 || contains (shape)) return false;
    auto list = current();
    list.insert (list.begin(), { name.isNotEmpty() ? name : juce::String ("Favorite"), shape, keyOf (shape) });   // newest first
    return write (list);
}
bool remove (const mod::Shape& shape)
{
    const auto key = keyOf (shape);
    auto list = current();
    const auto before = list.size();
    list.erase (std::remove_if (list.begin(), list.end(), [&key] (const Entry& e) { return e.key == key; }), list.end());
    return list.size() != before && write (list);
}
bool toggle (const juce::String& name, const mod::Shape& shape)
{
    if (contains (shape)) { remove (shape); return false; }
    add (name, shape);
    return contains (shape);
}
juce::int64 version() { current(); return cache().version; }
}

// ---- Shape panel: the grid, its toolbar and the palette ---------------------------------------------------
// ---- The browser (0.16.1) ---------------------------------------------------------------------------------------
struct ShapePanel::Browser final : public juce::Component
{
    explicit Browser (ShapePanel& o) : owner (o) { setTitle ("Presets"); }
    void resized() override { owner.layoutBrowser(); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
        if (! owner.presetButtons.empty()) return;
        const auto message = owner.search.isNotEmpty() ? "Nothing is called \"" + owner.search + "\""
                           : owner.paletteCategory == userFolder ? juce::String ("No saved shapes yet: press SAVE to keep the shape on the grid here")
                           : owner.paletteCategory == favouritesFolder ? juce::String ("No favorites yet: click the heart on a shape")
                                                                       : juce::String();
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (message, owner.paletteArea, juce::Justification::centred);
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        // The search box's magnifying glass.
        const auto box = owner.searchBox.getBounds().toFloat();
        if (box.isEmpty()) return;
        const auto glass = juce::Rectangle<float> (box.getX() + 12.0f, box.getCentreY() - 7.0f, 10.0f, 10.0f);
        g.setColour (accent);
        g.drawEllipse (glass, 2.0f);
        g.drawLine (glass.getRight() - 1.5f, glass.getBottom() - 1.5f, glass.getRight() + 4.0f, glass.getBottom() + 4.0f, 2.2f);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs (delta) > 0.0f) owner.setPalettePage (owner.palettePage + (delta < 0.0f ? 1 : -1));
    }
    ShapePanel& owner;
};
ShapePanel::ShapePanel()
{
    addAndMakeVisible (editor);
    editor.onChange = [this] { updateToolbar(); refreshCurrent(); if (onChange) onChange(); };
    lengthButton.setTooltip (lengthTooltip);
    lengthButton.onClick = [this] { showLengthMenu(); };
    prevLength.onClick = [this] { stepLength (-1); };
    nextLength.onClick = [this] { stepLength (1); };
    gridButton.setTooltip ("Grid size (right-click the grid for triplets)");
    gridButton.onClick = [this] { showGridMenu(); };
    snapButton.setClickingTogglesState (true);
    snapButton.setTooltip ("Snap points to the grid (hold Alt or Cmd while dragging to place freely)");
    snapButton.onClick = [this] { editor.snap = snapButton.getToggleState(); };
    doubleButton.setTooltip ("2x: repeat the pattern twice in the same length (Shift-click: 3x)");
    doubleButton.onClick = [this]
    {
        const int times = juce::ModifierKeys::currentModifiers.isShiftDown() ? 3 : 2;
        editor.apply ([times] (const mod::Shape& s) { return mod::repeated (s, times); });
    };
    halfButton.setTooltip ("1/2: stretch the first half over the whole length (undoes 2x)");
    halfButton.onClick = [this] { editor.apply (mod::halved); };
    clearButton.onClick = [this] { editor.apply ([] (const mod::Shape&) { return mod::flat (1.0f); }); };
    reverseButton.onClick = [this] { editor.apply (mod::reversed); };
    flipButton.onClick = [this] { editor.apply (mod::flippedVertical); };
    undoButton.onClick = [this] { if (editor.undo()) updateToolbar(); };
    redoButton.onClick = [this] { if (editor.redo()) updateToolbar(); };
    // Move the wave (or the points selected) by a grid step; Shift: an eighth of one (0.16.1).
    moveLeftButton.onClick = [this] { editor.moveWave (-1, juce::ModifierKeys::currentModifiers.isShiftDown()); };
    moveRightButton.onClick = [this] { editor.moveWave (1, juce::ModifierKeys::currentModifiers.isShiftDown()); };
    for (auto* c : std::initializer_list<juce::Component*> { &lengthButton, &prevLength, &nextLength, &gridButton, &snapButton, &clearButton,
                                                            &doubleButton, &halfButton, &moveLeftButton, &moveRightButton, &reverseButton, &flipButton,
                                                            &undoButton, &redoButton })
        addAndMakeVisible (*c);
    // The browser's FAVORITES and search (shown in the browser layout).
    favouritesButton.setButtonText (u8 ("\xe2\x99\xa5 FAVORITES"));
    favouritesButton.setTooltip ("The shapes and patterns you marked with the heart (click again: back to the folder)");
    favouritesButton.onClick = [this] { toggleFavouritesFolder(); };
    searchBox.setTextToShowWhenEmpty ("Search", dimTextColour);
    searchBox.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    searchBox.setIndents (34, 0);
    searchBox.setJustification (juce::Justification::centredLeft);
    searchBox.setTooltip ("Search every folder by name (a folder's name works too: \"octave down 12\"); Esc clears it");
    searchBox.onTextChange = [this] { setSearch (searchBox.getText()); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); setSearch ({}); };
    // Preset palette: one category at a time.
    categoryButton.setTooltip ("Preset shapes: choose a category");
    categoryButton.onClick = [this] { showCategoryMenu(); };
    addAndMakeVisible (categoryButton);
    {
        const int saved = settings->getPaletteCategory();
        paletteCategory = saved == userFolder ? userFolder : juce::jlimit (0, mod::numPresetCategories() - 1, saved);
    }
    saveButton.setTooltip ("Save this shape to the USER folder (it is kept for every project, in Triad and Triad Time)");
    saveButton.onClick = [this]
    {
        const auto initial = "My shape " + juce::String ((int) triad::userShapes::load().size() + 1);
        if (askName == nullptr) { saveShapeToUser (initial); return; }
        juce::Component::SafePointer<ShapePanel> safe (this);
        askName ("Save shape to USER", initial, [safe] (const juce::String& name) { if (safe != nullptr) safe->saveShapeToUser (name); });
    };
    addAndMakeVisible (saveButton);
    prevPage.onClick = [this] { setPalettePage (palettePage - 1); };
    nextPage.onClick = [this] { setPalettePage (palettePage + 1); };
    pageLabel.setJustificationType (juce::Justification::centred);
    pageLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    pageLabel.setTooltip ("Page: click to jump to a page (in ALL, to a folder); the mouse wheel over the shapes turns the pages");
    pageLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    pageLabel.addMouseListener (this, false);
    // In the browser: click the page number and type one (Thomas: "click where it has 1/200 and you can just pick a page").
    pageLabel.onEditorShow = [this]
    {
        if (auto* typing = pageLabel.getCurrentTextEditor())
        {
            typing->setInputRestrictions (5, "0123456789");
            typing->setText (juce::String (palettePage + 1), false);
            typing->setJustification (juce::Justification::centred);
            typing->selectAll();
        }
    };
    pageLabel.onTextChange = [this]
    {
        if (const int typed = pageLabel.getText().getIntValue(); typed >= 1) setPalettePage (typed - 1);
        pageLabel.setText (juce::String (palettePage + 1) + " / " + juce::String (getNumPalettePages()), juce::dontSendNotification);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &prevPage, &nextPage, &pageLabel }) addAndMakeVisible (*c);
    favouritesSeen = triad::favouriteShapes::version();
    rebuildPalette();
    startTimer (1000);
}
void ShapePanel::timerCallback()
{
    if (! isShowing()) return;
    const auto v = triad::favouriteShapes::version();
    if (v != favouritesSeen) { favouritesSeen = v; rebuildPalette(); }
}
void ShapePanel::toggleFavourite (int index)
{
    if (auto* b = dynamic_cast<ShapeButton*> (getPaletteButton (index)); b != nullptr && b->onHeart) b->onHeart();
}
bool ShapePanel::isPaletteFavourite (int index) const
{
    auto* b = dynamic_cast<ShapeButton*> (getPaletteButton (index));
    return b != nullptr && b->favourite;
}
ShapePanel::~ShapePanel() = default;
void ShapePanel::setHost (ShapeHost* h)
{
    const bool wasTime = timeHost();
    host = h;
    editor.setHost (h);
    // Triad Time's TIME envelope opens its own folders (and remembers the one chosen there); everything else Triad's.
    if (timeHost() != wasTime)
    {
        const int wanted = timeHost() ? settings->getTimePaletteCategory (timeFolder) : settings->getPaletteCategory();
        const int category = validCategory (wanted);
        if (category != paletteCategory) { paletteCategory = category; palettePage = 0; rebuildPalette(); }
    }
    updateToolbar();
}
int ShapePanel::validCategory (int category) const
{
    if (category == userFolder || category == allFolder || category == favouritesFolder) return category;
    if (isTimeFolder (category)) return juce::jlimit (timeFolder, timeFolder + triad::timePresets::numFolders - 1, category);
    return juce::jlimit (0, mod::numPresetCategories() - 1, category);
}
namespace
{
// The browser's search: every word typed is in the name, or in the folder's name and the name together.
struct SearchWords
{
    explicit SearchWords (const juce::String& text) { words.addTokens (text.toLowerCase(), " ", {}); words.removeEmptyStrings(); }
    bool matches (const juce::String& name, const juce::String& folder) const
    {
        const auto n = name.toLowerCase(), both = folder.toLowerCase() + " " + n;
        for (const auto& w : words) if (! both.contains (w)) return false;
        return ! words.isEmpty();
    }
    juce::StringArray words;
};
}
int ShapePanel::searchCount() const
{
    const SearchWords words (search);
    int count = 0;
    for (const auto& e : triad::userShapes::load()) count += words.matches (e.name, "USER") ? 1 : 0;
    for (int c = 0; c < mod::numPresetCategories(); ++c)
        for (const int i : mod::presetsIn (c)) count += words.matches (mod::presetName (i), mod::presetCategoryName (c)) ? 1 : 0;
    for (int f = 0; f < triad::timePresets::numFolders; ++f)
        for (const int i : triad::timePresets::presetsIn (f)) count += words.matches (triad::timePresets::entries[i].name, triad::timePresets::folderNames[f]) ? 1 : 0;
    return count;
}
std::vector<ShapePanel::PaletteShape> ShapePanel::paletteShapes() const
{
    std::vector<PaletteShape> list;
    if (search.isNotEmpty())
    {
        // Each plug-in's own shapes first: Triad Time's TIME patterns, Triad's LFO shapes.
        const SearchWords words (search);
        for (auto& e : triad::userShapes::load()) if (words.matches (e.name, "USER")) list.push_back ({ e.name, std::move (e.shape), e.file });
        const auto addTime = [&]
        {
            for (int f = 0; f < triad::timePresets::numFolders; ++f)
                for (const int i : triad::timePresets::presetsIn (f))
                    if (words.matches (triad::timePresets::entries[i].name, triad::timePresets::folderNames[f]))
                        list.push_back ({ triad::timePresets::entries[i].name, triad::timePresets::shape (i), {} });
        };
        const auto addShapes = [&]
        {
            for (int c = 0; c < mod::numPresetCategories(); ++c)
                for (const int i : mod::presetsIn (c))
                    if (words.matches (mod::presetName (i), mod::presetCategoryName (c))) list.push_back ({ mod::presetName (i), mod::preset (i), {} });
        };
        if (timeHost()) { addTime(); addShapes(); } else { addShapes(); addTime(); }
        return list;
    }
    if (paletteCategory == userFolder)
        for (auto& e : triad::userShapes::load()) list.push_back ({ e.name, std::move (e.shape), e.file });
    else if (paletteCategory == favouritesFolder)
        for (auto& e : triad::favouriteShapes::load()) list.push_back ({ e.name, std::move (e.shape), {} });
    else if (isTimeFolder (paletteCategory))
        for (const int i : triad::timePresets::presetsIn (paletteCategory - timeFolder))
            list.push_back ({ triad::timePresets::entries[i].name, triad::timePresets::shape (i), {} });
    else if (paletteCategory == allFolder)
    {
        // Every preset in one place: Triad's folders in order, then the TIME patterns (each named with its folder).
        for (int c = 0; c < mod::numPresetCategories(); ++c)
            for (const int i : mod::presetsIn (c)) list.push_back ({ mod::presetCategoryName (c) + " / " + mod::presetName (i), mod::preset (i), {} });
        for (int f = 0; f < triad::timePresets::numFolders; ++f)
            for (const int i : triad::timePresets::presetsIn (f))
                list.push_back ({ juce::String (triad::timePresets::folderNames[f]) + " / " + triad::timePresets::entries[i].name, triad::timePresets::shape (i), {} });
    }
    else
        for (const int i : mod::presetsIn (paletteCategory)) list.push_back ({ mod::presetName (i), mod::preset (i), {} });
    return list;
}
juce::String ShapePanel::paletteFolderName() const
{
    if (isTimeFolder (paletteCategory)) return triad::timePresets::folderNames[juce::jlimit (0, triad::timePresets::numFolders - 1, paletteCategory - timeFolder)];
    if (paletteCategory == allFolder) return "ALL";
    if (paletteCategory == favouritesFolder) return "FAVORITES";
    return paletteCategory == userFolder ? juce::String ("USER") : mod::presetCategoryName (paletteCategory);
}
void ShapePanel::rebuildPalette()
{
    for (auto& b : presetButtons) removeChildComponent (b.get());
    presetButtons.clear();
    const auto list = paletteShapes();
    const int size = pageSize();
    const int pages = juce::jmax (1, ((int) list.size() + size - 1) / size);
    palettePage = juce::jlimit (0, pages - 1, palettePage);
    const auto onGrid = triad::favouriteShapes::keyOf (editor.getShape());
    for (int k = palettePage * size; k < juce::jmin ((int) list.size(), (palettePage + 1) * size); ++k)
    {
        const auto& entry = list[(size_t) k];
        auto b = std::make_unique<ShapeButton> (entry.name, entry.shape, layout == Layout::browser && host != nullptr ? host->getColour() : accent);
        b->showName = layout == Layout::browser;
        b->current = triad::favouriteShapes::keyOf (entry.shape) == onGrid;
        b->onClick = [this, shape = entry.shape] { editor.apply ([shape] (const mod::Shape&) { return shape; }); };
        b->favourite = triad::favouriteShapes::contains (entry.shape);
        b->onHeart = [this, name = entry.name, shape = entry.shape, button = b.get()]
        {
            const bool now = triad::favouriteShapes::toggle (name, shape);
            favouritesSeen = triad::favouriteShapes::version();
            if (host != nullptr) host->setStatus (now ? "Added \"" + name + "\" to FAVORITES" : "Removed \"" + name + "\" from FAVORITES");
            if (paletteCategory == favouritesFolder)
            {
                juce::Component::SafePointer<ShapePanel> safe (this);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->rebuildPalette(); });   // it leaves the folder (not while its button is busy)
            }
            else { button->favourite = now; button->repaint(); }
        };
        if (entry.file != juce::File())
        {
            b->setTooltip (entry.name + " (replaces the shape; right-click to rename or delete)");
            b->onMenu = [this, file = entry.file] { showUserShapeMenu (file); };
        }
        paletteParent().addAndMakeVisible (*b);
        presetButtons.push_back (std::move (b));
    }
    categoryButton.setButtonText ((search.isNotEmpty() ? juce::String ("SEARCH") : paletteFolderName()) + u8 (" \xe2\x96\xbe"));
    favouritesButton.setToggleState (paletteCategory == favouritesFolder && search.isEmpty(), juce::dontSendNotification);
    pageLabel.setText (juce::String (palettePage + 1) + (layout == Layout::browser ? " / " : "/") + juce::String (pages), juce::dontSendNotification);
    prevPage.setEnabled (palettePage > 0);
    nextPage.setEnabled (palettePage < pages - 1);
    resized();
    if (browser != nullptr) { layoutBrowser(); browser->repaint(); }
    repaint();
}
int ShapePanel::getNumPalettePages() const
{
    // Counted without building the shapes (ALL has over 11,000).
    int count = 0;
    if (search.isNotEmpty()) count = searchCount();
    else if (paletteCategory == userFolder) count = (int) triad::userShapes::load().size();
    else if (paletteCategory == favouritesFolder) count = (int) triad::favouriteShapes::load().size();
    else if (isTimeFolder (paletteCategory)) count = (int) triad::timePresets::presetsIn (paletteCategory - timeFolder).size();
    else if (paletteCategory == allFolder) count = mod::numPresets() + triad::timePresets::numPresets;
    else count = (int) mod::presetsIn (paletteCategory).size();
    return juce::jmax (1, (count + pageSize() - 1) / pageSize());
}
void ShapePanel::setPaletteCategory (int category)
{
    category = validCategory (category);
    if (search.isNotEmpty())
    {
        // Choosing a folder ends a search.
        search = {};
        searchBox.clear();
        paletteCategory = -1;
    }
    if (category == paletteCategory && ! presetButtons.empty()) return;
    paletteCategory = category;
    palettePage = 0;
    if (timeHost()) settings->setTimePaletteCategory (category);
    else settings->setPaletteCategory (category);
    rebuildPalette();
}
void ShapePanel::setPalettePage (int page)
{
    page = juce::jlimit (0, getNumPalettePages() - 1, page);
    if (page == palettePage) return;
    palettePage = page;
    rebuildPalette();
}
void ShapePanel::mouseUp (const juce::MouseEvent& e)
{
    // The strip's page number opens the page menu; the browser's is typed into (a click edits it).
    if (e.eventComponent == &pageLabel && layout == Layout::strip && getNumPalettePages() > 1) showPageMenu();
}
void ShapePanel::showPageMenu()
{
    juce::PopupMenu menu;
    const int pages = getNumPalettePages();
    if (paletteCategory == allFolder)
    {
        // ALL: where each folder starts.
        menu.addSectionHeader ("JUMP TO A FOLDER");
        int start = 0;
        const auto addFolder = [&] (const juce::String& name, int count)
        {
            if (count <= 0) return;
            const int page = start / paletteSize;
            addItemWithRight (menu, page + 1, name, "p. " + juce::String (page + 1), true,
                              palettePage >= page && palettePage <= (start + count - 1) / paletteSize);
            start += count;
        };
        for (int c = 0; c < mod::numPresetCategories(); ++c) addFolder (mod::presetCategoryName (c), (int) mod::presetsIn (c).size());
        for (int f = 0; f < triad::timePresets::numFolders; ++f) addFolder (juce::String ("TIME: ") + triad::timePresets::folderNames[f], (int) triad::timePresets::presetsIn (f).size());
    }
    else
    {
        menu.addSectionHeader ("PAGE");
        for (int p = 0; p < pages; ++p)
        {
            if (p > 0 && p % 20 == 0) menu.addColumnBreak();
            menu.addItem (p + 1, juce::String (p + 1), true, p == palettePage);
        }
    }
    juce::Component::SafePointer<ShapePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pageLabel), [safe] (int choice)
    {
        if (safe != nullptr && choice > 0) safe->setPalettePage (choice - 1);
    });
}
void ShapePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (paletteArea.contains (e.getEventRelativeTo (this).getPosition()) && getNumPalettePages() > 1)
    {
        const float delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs (delta) > 0.0f) setPalettePage (palettePage + (delta < 0.0f ? 1 : -1));
        return;
    }
    Component::mouseWheelMove (e, wheel);
}
juce::File ShapePanel::saveShapeToUser (const juce::String& name)
{
    const auto file = triad::userShapes::save (name, host != nullptr ? host->getShape() : editor.getShape());
    if (file == juce::File())
    {
        if (host != nullptr) host->setStatus ("Could not save the shape");
        return {};
    }
    if (host != nullptr) host->setStatus ("Saved shape \"" + file.getFileNameWithoutExtension() + "\" to USER");
    paletteCategory = userFolder;
    if (timeHost()) settings->setTimePaletteCategory (userFolder);
    else settings->setPaletteCategory (userFolder);
    const auto list = paletteShapes();
    for (int k = 0; k < (int) list.size(); ++k)
        if (list[(size_t) k].file == file) palettePage = k / paletteSize;
    rebuildPalette();
    return file;
}
void ShapePanel::showUserShapeMenu (const juce::File& file)
{
    enum { renameId = 1, deleteId, folderId };
    juce::PopupMenu menu;
    menu.addSectionHeader (file.getFileNameWithoutExtension());
    menu.addItem (renameId, "Rename...", askName != nullptr);
    menu.addItem (deleteId, "Delete");
    menu.addSeparator();
    menu.addItem (folderId, "Show the USER folder");
    juce::Component::SafePointer<ShapePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, file] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == renameId && safe->askName != nullptr)
            safe->askName ("Rename shape", file.getFileNameWithoutExtension(), [safe, file] (const juce::String& name)
            {
                if (safe != nullptr && triad::userShapes::rename (file, name)) safe->rebuildPalette();
            });
        else if (choice == deleteId) { triad::userShapes::remove (file); safe->rebuildPalette(); }
        else if (choice == folderId) { TriadSettings::lfoShapeFolder().createDirectory(); TriadSettings::lfoShapeFolder().revealToUser(); }
    });
}
void ShapePanel::showCategoryMenu()
{
    enum { userId = userFolder + 1, saveId = 5000, folderId };
    juce::PopupMenu menu;
    // The count on the right (2,000); each plug-in's own shapes first, the other's one menu away (0.16.0, Thomas:
    // Triad Time opens on its TIME folders with the LFO shapes to the side; Triad's LFO tab the other way round).
    const auto counted = [] (int n) { return n >= 1000 ? juce::String (n / 1000) + "," + juce::String (n % 1000).paddedLeft ('0', 3) : juce::String (n); };
    auto add = [&] (juce::PopupMenu& m, int id, const juce::String& name, int count, bool ticked)
    {
        addItemWithRight (m, id, name, counted (count), true, ticked);
    };
    menu.addSectionHeader ("EVERYTHING");
    add (menu, allFolder + 1, "ALL", mod::numPresets() + triad::timePresets::numPresets, paletteCategory == allFolder);
    add (menu, favouritesFolder + 1, u8 ("\xe2\x99\xa5 FAVORITES"), (int) triad::favouriteShapes::load().size(), paletteCategory == favouritesFolder);
    const auto addTimeFolders = [&] (juce::PopupMenu& m)
    {
        m.addSectionHeader ("TRIAD TIME: PITCH AND CHOPS (TRAINED ON GROSS BEAT BANKS)");
        for (int f = 0; f < triad::timePresets::numFolders; ++f)
            add (m, timeFolder + f + 1, triad::timePresets::folderNames[f], (int) triad::timePresets::presetsIn (f).size(), paletteCategory == timeFolder + f);
    };
    const auto addShapeFolders = [&] (juce::PopupMenu& m)
    {
        m.addSectionHeader ("SHAPES BY TYPE");
        bool generatedHeader = false;
        for (int c = 0; c < mod::numPresetCategories(); ++c)
        {
            if (mod::isGeneratedCategory (c) && ! generatedHeader) { m.addSectionHeader ("GENERATED (FROM THE DICE)"); generatedHeader = true; }
            if (mod::isExtraCategory (c)) m.addSectionHeader ("EXTRA (EVERY PREVIEW SHAPE)");
            add (m, c + 1, mod::presetCategoryName (c), (int) mod::presetsIn (c).size(), c == paletteCategory);
        }
    };
    const bool inTime = paletteCategory >= timeFolder && paletteCategory < timeFolder + triad::timePresets::numFolders;
    const bool inShapes = paletteCategory >= 0 && paletteCategory < mod::numPresetCategories();
    if (timeHost())
    {
        addTimeFolders (menu);
        menu.addSeparator();
        juce::PopupMenu shapes;
        addShapeFolders (shapes);
        addSubMenu (menu, "LFO SHAPES", std::move (shapes), {}, true, inShapes);
    }
    else
    {
        addShapeFolders (menu);
        menu.addSeparator();
        juce::PopupMenu patterns;
        addTimeFolders (patterns);
        addSubMenu (menu, "TRIAD TIME PATTERNS", std::move (patterns), {}, true, inTime);
    }
    menu.addSeparator();
    menu.addSectionHeader ("YOUR SHAPES");
    add (menu, userId, "USER", (int) triad::userShapes::load().size(), paletteCategory == userFolder);
    menu.addItem (saveId, "Save this shape to USER...");
    menu.addItem (folderId, "Show the USER folder");
    juce::Component::SafePointer<ShapePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&categoryButton), [safe] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        if (choice == saveId) safe->saveButton.onClick();
        else if (choice == folderId) { TriadSettings::lfoShapeFolder().createDirectory(); TriadSettings::lfoShapeFolder().revealToUser(); }
        else if (choice == userId) safe->setPaletteCategory (userFolder);
        else safe->setPaletteCategory (choice - 1);
    });
}
void ShapePanel::updateToolbar()
{
    const auto& list = mod::lengths();
    const int length = host != nullptr ? juce::jlimit (0, (int) list.size() - 1, host->getLength()) : mod::defaultLength();
    lengthButton.setButtonText (juce::String (list[(size_t) length].label));
    lengthButton.setTooltip (lengthTooltip);
    gridButton.setButtonText ("GRID 1/" + juce::String (editor.gridDivision) + (editor.triplet ? "T" : ""));
    snapButton.setToggleState (editor.snap, juce::dontSendNotification);
    undoButton.setEnabled (host != nullptr && host->canUndo());
    redoButton.setEnabled (host != nullptr && host->canRedo());
}
void ShapePanel::stepLength (int direction)
{
    // Steps through the straight lengths (1/16 ... 32 bars), as ShaperBox's arrows do.
    if (host == nullptr) return;
    const auto& list = mod::lengths();
    const int current = host->getLength();
    int next = current;
    for (int i = current + direction; i >= 0 && i < (int) list.size(); i += direction)
        if (list[(size_t) i].column == 1 && host->allowsLength (i)) { next = i; break; }
    host->setLength (next);
    editor.repaint();
    updateToolbar();
    if (onChange) onChange();
}
void ShapePanel::showLengthMenu()
{
    if (host == nullptr) return;
    // Four columns like ShaperBox: TRIPLET | STRAIGHT | DOTTED | FREE.
    juce::PopupMenu menu;
    const auto& list = mod::lengths();
    const int current = host->getLength();
    const char* headers[] { "TRIPLET", "STRAIGHT", "DOTTED", "FREE (Hz)" };
    bool first = true;
    for (int column = 0; column < 4; ++column)
    {
        juce::Array<int> items;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].column == column && host->allowsLength (i)) items.add (i);
        if (items.isEmpty()) continue;
        if (! first) menu.addColumnBreak();
        first = false;
        menu.addSectionHeader (headers[column]);
        for (int i : items) menu.addItem (i + 1, list[(size_t) i].label, true, i == current);
    }
    juce::Component::SafePointer<ShapePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lengthButton), [safe] (int choice)
    {
        if (safe == nullptr || choice == 0 || safe->host == nullptr) return;
        safe->host->setLength (choice - 1);
        safe->editor.repaint();
        safe->updateToolbar();
        if (safe->onChange) safe->onChange();
    });
}
void ShapePanel::showGridMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("GRID");
    for (int d : { 4, 8, 16, 32, 64 }) menu.addItem (d, "1/" + juce::String (d), true, editor.gridDivision == d && ! editor.triplet);
    menu.addSeparator();
    menu.addItem (1000, "Triplet", true, editor.triplet);
    menu.addItem (1001, "Snap to grid", true, editor.snap);
    juce::Component::SafePointer<ShapePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridButton), [safe] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == 1000) safe->editor.triplet = ! safe->editor.triplet;
        else if (choice == 1001) safe->editor.snap = ! safe->editor.snap;
        else { safe->editor.gridDivision = choice; safe->editor.triplet = false; }
        safe->editor.repaint();
        safe->updateToolbar();
    });
}
void ShapePanel::resized()
{
    auto r = getLocalBounds();
    if (layout == Layout::browser)
    {
        // Triad Time's layout: the grid, then one toolbar (as ShaperBox's): length, grid, snap, SAVE, the window's own
        // boxes, and on the right delete, 2x, 1/2, the move buttons, the flips, undo and redo.
        auto toolbar = r.removeFromBottom (browserToolbarHeight);
        r.removeFromBottom (8);
        editor.setBounds (r);
        auto row = toolbar.withSizeKeepingCentre (toolbar.getWidth(), 34);
        const auto left = [&row] (juce::Component& c, int w, int gap) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); };
        const auto right = [&row] (juce::Component& c, int w, int gap) { c.setBounds (row.removeFromRight (w)); row.removeFromRight (gap); };
        left (lengthButton, 84, 4);
        left (prevLength, 30, 3);
        left (nextLength, 30, 10);
        left (gridButton, 96, 6);
        left (snapButton, 62, 6);
        left (saveButton, 62, 12);
        right (redoButton, 32, 4);
        right (undoButton, 32, 10);
        right (flipButton, 32, 4);
        right (reverseButton, 32, 10);
        right (moveRightButton, 32, 4);
        right (moveLeftButton, 32, 10);
        right (halfButton, 44, 4);
        right (doubleButton, 44, 4);
        right (clearButton, 32, 12);
        if (toolbarExtra != nullptr)
            toolbarExtra->setBounds (row.getX(), toolbar.getY(), juce::jmin (toolbarExtraWidth, row.getWidth()), toolbar.getHeight());
        return;
    }
    auto palette = r.removeFromBottom (30);
    r.removeFromBottom (6);
    auto toolbar = r.removeFromBottom (30);
    r.removeFromBottom (6);
    editor.setBounds (r);
    lengthButton.setBounds (toolbar.removeFromLeft (84));
    toolbar.removeFromLeft (3);
    prevLength.setBounds (toolbar.removeFromLeft (26));
    toolbar.removeFromLeft (2);
    nextLength.setBounds (toolbar.removeFromLeft (26));
    toolbar.removeFromLeft (10);
    gridButton.setBounds (toolbar.removeFromLeft (84));
    toolbar.removeFromLeft (3);
    snapButton.setBounds (toolbar.removeFromLeft (52));
    toolbar.removeFromLeft (10);
    saveButton.setBounds (toolbar.removeFromLeft (56));
    redoButton.setBounds (toolbar.removeFromRight (30));
    toolbar.removeFromRight (3);
    undoButton.setBounds (toolbar.removeFromRight (30));
    toolbar.removeFromRight (12);
    flipButton.setBounds (toolbar.removeFromRight (30));
    toolbar.removeFromRight (3);
    reverseButton.setBounds (toolbar.removeFromRight (30));
    toolbar.removeFromRight (12);
    moveRightButton.setBounds (toolbar.removeFromRight (30));
    toolbar.removeFromRight (3);
    moveLeftButton.setBounds (toolbar.removeFromRight (30));
    toolbar.removeFromRight (12);
    halfButton.setBounds (toolbar.removeFromRight (40));
    toolbar.removeFromRight (3);
    doubleButton.setBounds (toolbar.removeFromRight (40));
    toolbar.removeFromRight (3);
    clearButton.setBounds (toolbar.removeFromRight (30));
    paletteArea = palette;
    categoryButton.setBounds (palette.removeFromLeft (132).withTrimmedRight (4));
    prevPage.setBounds (palette.removeFromLeft (24));
    pageLabel.setBounds (palette.removeFromLeft (50));
    nextPage.setBounds (palette.removeFromLeft (24));
    palette.removeFromLeft (6);
    const int width = juce::jmin (52, palette.getWidth() / paletteSize);
    for (auto& b : presetButtons) b->setBounds (palette.removeFromLeft (width).withTrimmedRight (4));
}
void ShapePanel::paint (juce::Graphics& g)
{
    if (layout == Layout::strip && paletteCategory == userFolder && presetButtons.empty() && search.isEmpty())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("No saved shapes yet: press SAVE to keep this shape here", paletteArea.withTrimmedLeft (226),
                    juce::Justification::centredLeft);
    }
}

juce::Component& ShapePanel::paletteParent()
{
    if (layout == Layout::browser && browser != nullptr) return *browser;
    return *this;
}
juce::Component& ShapePanel::getBrowser()
{
    if (browser == nullptr) browser = std::make_unique<Browser> (*this);
    return *browser;
}
void ShapePanel::setLayout (Layout l)
{
    if (l == layout) return;
    layout = l;
    if (layout == Layout::browser) getBrowser();
    auto& parent = paletteParent();
    for (auto* c : std::initializer_list<juce::Component*> { &categoryButton, &prevPage, &nextPage, &pageLabel }) parent.addAndMakeVisible (*c);
    if (layout == Layout::browser)
    {
        browser->addAndMakeVisible (favouritesButton);
        browser->addAndMakeVisible (searchBox);
        pageLabel.setEditable (true, false, false);
        pageLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        pageLabel.setTooltip ("Page: click it and type a page number (the arrows or the mouse wheel step through)");
    }
    else
    {
        for (auto* c : std::initializer_list<juce::Component*> { &favouritesButton, &searchBox })
            if (auto* p = c->getParentComponent()) p->removeChildComponent (c);
        pageLabel.setEditable (false, false, false);
        pageLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    }
    palettePage = 0;
    rebuildPalette();
}
void ShapePanel::setToolbarExtra (juce::Component* c, int width)
{
    if (toolbarExtra != nullptr && toolbarExtra != c) removeChildComponent (toolbarExtra);
    toolbarExtra = c;
    toolbarExtraWidth = width;
    if (c != nullptr) addAndMakeVisible (*c);
    resized();
}
void ShapePanel::layoutBrowser()
{
    if (browser == nullptr || layout != Layout::browser) return;
    auto r = browser->getLocalBounds().reduced (14, 12);
    auto top = r.removeFromTop (34);
    categoryButton.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth() / 4)));
    top.removeFromLeft (10);
    favouritesButton.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (10);
    nextPage.setBounds (top.removeFromRight (34));
    top.removeFromRight (4);
    pageLabel.setBounds (top.removeFromRight (92));
    top.removeFromRight (4);
    prevPage.setBounds (top.removeFromRight (34));
    top.removeFromRight (14);
    searchBox.setBounds (top.withWidth (juce::jmin (340, top.getWidth())));
    r.removeFromTop (12);
    paletteArea = r;
    constexpr int columns = 5, gap = 12;
    const int w = (r.getWidth() - gap * (columns - 1)) / columns, h = (r.getHeight() - gap) / 2;
    for (int i = 0; i < (int) presetButtons.size(); ++i)
        presetButtons[(size_t) i]->setBounds (r.getX() + (i % columns) * (w + gap), r.getY() + (i / columns) * (h + gap), w, h);
}
void ShapePanel::setSearch (const juce::String& text)
{
    const auto next = text.trim();
    if (next == search) return;
    search = next;
    if (searchBox.getText().trim() != search) searchBox.setText (text, false);
    palettePage = 0;
    rebuildPalette();
}
void ShapePanel::toggleFavouritesFolder()
{
    if (paletteCategory == favouritesFolder && search.isEmpty()) setPaletteCategory (folderBeforeFavourites);
    else
    {
        if (paletteCategory != favouritesFolder) folderBeforeFavourites = paletteCategory;
        setPaletteCategory (favouritesFolder);
    }
}
bool ShapePanel::isPaletteCurrent (int index) const
{
    auto* b = dynamic_cast<ShapeButton*> (getPaletteButton (index));
    return b != nullptr && b->current;
}
void ShapePanel::refreshCurrent()
{
    if (presetButtons.empty()) return;
    const auto onGrid = triad::favouriteShapes::keyOf (editor.getShape());
    for (auto& button : presetButtons)
        if (auto* b = dynamic_cast<ShapeButton*> (button.get()))
            if (const bool now = triad::favouriteShapes::keyOf (b->getShape()) == onGrid; now != b->current) { b->current = now; b->repaint(); }
}

// ---- Generator ----------------------------------------------------------------------------------------------
GeneratorPanel::GeneratorPanel()
{
    struct SliderSetup { juce::Slider* slider; double reset; juce::Colour colour; const char* tip; };
    for (const auto& s : { SliderSetup { &angularSlider, 100.0, accent,
                                         "Angular: hard corners - vertical steps, squares, triangles and straight lines at 30, 45 and 60 degrees" },
                           SliderSetup { &softSlider, 0.0, juce::Colour (0xff67c6b8),
                                         "Soft: curves and waves. With Angular up too: hard jumps into curves and waves" },
                           SliderSetup { &chaosSlider, 35.0, juce::Colour (0xffe0a85a),
                                         "Chaos: more points, faster and less regular rhythms, more levels. Low chaos repeats a simple pattern" } })
    {
        s.slider->setSliderStyle (juce::Slider::LinearHorizontal);
        s.slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.slider->setRange (0.0, 100.0, 1.0);
        s.slider->setDoubleClickReturnValue (true, s.reset);
        s.slider->setColour (juce::Slider::trackColourId, s.colour);
        s.slider->setTooltip (s.tip);
        s.slider->onValueChange = [this, slider = s.slider]
        {
            repaint();
            if (! slider->isMouseButtonDown()) saveStyle();
        };
        s.slider->onDragEnd = [this] { saveStyle(); };
        addAndMakeVisible (*s.slider);
    }
    {
        float angular = 1.0f, soft = 0.0f, chaos = 0.35f;
        settings->getGenerator (angular, soft, chaos);
        angularSlider.setValue (angular * 100.0, juce::dontSendNotification);
        softSlider.setValue (soft * 100.0, juce::dontSendNotification);
        chaosSlider.setValue (chaos * 100.0, juce::dontSendNotification);
    }
    hardStyleButton.setTooltip ("Hard angles only: Angular 100%, Soft 0%");
    mixStyleButton.setTooltip ("Hard angles into curves: Angular 100%, Soft 60%");
    softStyleButton.setTooltip ("Soft curves only: Angular 0%, Soft 100%");
    hardStyleButton.onClick = [this] { setStyle (1.0f, 0.0f, (float) chaosSlider.getValue() * 0.01f); };
    mixStyleButton.onClick = [this] { setStyle (1.0f, 0.6f, (float) chaosSlider.getValue() * 0.01f); };
    softStyleButton.onClick = [this] { setStyle (0.0f, 1.0f, (float) chaosSlider.getValue() * 0.01f); };
    for (auto* b : { &hardStyleButton, &mixStyleButton, &softStyleButton }) addAndMakeVisible (*b);
    generateButton = std::make_unique<GenerateButton>();
    generateButton->onClick = [this] { generate(); };
    addAndMakeVisible (*generateButton);
    likeButton = std::make_unique<RateButton> (true);
    dislikeButton = std::make_unique<RateButton> (false);
    likeButton->setTooltip ("Like: you like the shape on the grid. The dice leans towards shapes like it (right-click: forget)");
    dislikeButton->setTooltip ("Dislike: you don't like it. The dice draws a new one and stays away from shapes like it, made with "
                               "similar Angular / Soft / Chaos (right-click: forget)");
    likeButton->onClick = [this] { rate (true); };
    dislikeButton->onClick = [this] { rate (false); };
    for (auto* b : { likeButton.get(), dislikeButton.get() })
    {
        b->addMouseListener (this, false);
        addAndMakeVisible (*b);
    }
    updateRatingButtons();
}
juce::File GeneratorPanel::tasteFile (const juce::String& key)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Triad";
    o.filenameSuffix = ".settings";
    o.folderName = "Triad";
    o.osxLibrarySubFolder = "Application Support";
    return o.getDefaultFile().getParentDirectory().getChildFile ("Generator taste - " + key + ".txt");
}
triad::mod::Taste GeneratorPanel::getTaste() const
{
    const auto file = tasteFile (tasteKey);
    return file.existsAsFile() ? triad::mod::Taste::fromText (file.loadFileAsString()) : triad::mod::Taste();
}
void GeneratorPanel::setTasteKey (const juce::String& key)
{
    if (key == tasteKey) return;
    tasteKey = key;
    updateRatingButtons();
}
void GeneratorPanel::setSources (const std::vector<mod::Source>& list)
{
    sources = list.empty() ? std::vector<mod::Source> { mod::Source::infiltrator } : list;
    for (auto& b : sourceButtons) removeChildComponent (b.get());
    sourceButtons.clear();
    for (const auto s : sources)
    {
        auto b = std::make_unique<juce::TextButton> (mod::sourceName (s));
        b->setClickingTogglesState (false);
        b->setTooltip (s == mod::Source::infiltrator ? "From your Infiltrator presets: hard gates, steps, stairs and angles (the dice as it was)"
                       : s == mod::Source::shaperBox ? "From your ShaperBox presets: their motifs and how they repeat them (AAAB, ABAB ...)"
                       : s == mod::Source::grossBeatTime ? "From your Gross Beat banks' time patterns: octave and reverse lines, tape stops and chops"
                       : s == mod::Source::timePresets ? "From Triad Time's 10,000 TIME patterns; Chaos splices in parts of others"
                                                       : "From your Gross Beat banks' volume patterns: their gates and how they repeat them");
        b->onClick = [this, s] { setSource (s); };
        addAndMakeVisible (*b);
        sourceButtons.push_back (std::move (b));
    }
    const int saved = settings->getDiceSource (tasteKey, (int) sources.front());
    source = std::find (sources.begin(), sources.end(), (mod::Source) saved) != sources.end() ? (mod::Source) saved : sources.front();
    updateSourceButtons();
    resized();
}
void GeneratorPanel::setSource (mod::Source s)
{
    if (std::find (sources.begin(), sources.end(), s) == sources.end()) return;
    source = s;
    settings->setDiceSource (tasteKey, (int) s);
    updateSourceButtons();
}
void GeneratorPanel::updateSourceButtons()
{
    for (size_t i = 0; i < sourceButtons.size(); ++i)
    {
        const bool on = sources[i] == source;
        sourceButtons[i]->setToggleState (on, juce::dontSendNotification);
        sourceButtons[i]->setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
        sourceButtons[i]->setColour (juce::TextButton::textColourOnId, inkColour);
    }
}
void GeneratorPanel::updateRatingButtons()
{
    const auto taste = getTaste();
    static_cast<RateButton*> (likeButton.get())->setCount (taste.countLiked());
    static_cast<RateButton*> (dislikeButton.get())->setCount (taste.countDisliked());
    const bool canRate = target != nullptr;
    likeButton->setEnabled (canRate);
    dislikeButton->setEnabled (canRate);
}
void GeneratorPanel::rate (bool liked)
{
    if (target == nullptr || target->getHost() == nullptr) return;
    auto taste = getTaste();
    taste.add (target->getShape(), getStyle(), liked);
    const auto file = tasteFile (tasteKey);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (taste.toText());
    static_cast<RateButton*> (liked ? likeButton.get() : dislikeButton.get())->flash();
    if (! liked) generate();
    updateRatingButtons();
}
void GeneratorPanel::forgetTaste()
{
    tasteFile (tasteKey).deleteFile();
    updateRatingButtons();
}
void GeneratorPanel::showTasteMenu()
{
    const auto taste = getTaste();
    juce::PopupMenu menu;
    const auto what = tasteKey == "time" ? juce::String ("Triad Time's TIME shapes") : tasteKey == "volume" ? juce::String ("Triad Time's VOLUME shapes")
                                                                                     : juce::String ("LFO shapes");
    menu.addSectionHeader ("YOUR TASTE IN " + what.toUpperCase() + ": " + juce::String (taste.countLiked()) + " LIKED, "
                           + juce::String (taste.countDisliked()) + " DISLIKED");
    menu.addItem (1, "Forget what I liked and disliked", ! taste.empty());
    juce::Component::SafePointer<GeneratorPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (likeButton.get()), [safe] (int choice)
    {
        if (safe != nullptr && choice == 1) safe->forgetTaste();
    });
}
void GeneratorPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && (e.eventComponent == likeButton.get() || e.eventComponent == dislikeButton.get())) showTasteMenu();
}
triad::mod::Style GeneratorPanel::getStyle() const
{
    return { (float) angularSlider.getValue() * 0.01f, (float) softSlider.getValue() * 0.01f, (float) chaosSlider.getValue() * 0.01f, source,
             tasteKey == "lfo" };   // Triad's LFOs: Chaos adds triangles, squares and points (not Triad Time's dice)
}
void GeneratorPanel::setStyle (float angular, float soft, float chaos)
{
    angularSlider.setValue (juce::jlimit (0.0f, 1.0f, angular) * 100.0, juce::dontSendNotification);
    softSlider.setValue (juce::jlimit (0.0f, 1.0f, soft) * 100.0, juce::dontSendNotification);
    chaosSlider.setValue (juce::jlimit (0.0f, 1.0f, chaos) * 100.0, juce::dontSendNotification);
    saveStyle();
    repaint();
}
void GeneratorPanel::saveStyle()
{
    const auto style = getStyle();
    settings->setGenerator (style.angular, style.soft, style.chaos);
}
void GeneratorPanel::generate()
{
    if (target == nullptr) return;
    mod::GridInfo grid;
    grid.columns = target->columns();
    grid.rows = 16;
    grid.aspect = target->plotAspect();
    // Never the shape that is there already, nor one of the last few the dice drew; with ratings (the check and the X),
    // one of the best of a batch for your taste.
    const auto current = target->getShape();
    const auto shape = getTaste().pick (getStyle(), grid, random, [this, &current] (const mod::Shape& s)
    {
        return ! (s == current) && ! recentShapes.contains (mod::toText (s));
    });
    recentShapes.add (mod::toText (shape));
    while (recentShapes.size() > 16) recentShapes.remove (0);
    target->apply ([shape] (const mod::Shape&) { return shape; });
}
void GeneratorPanel::resized()
{
    // Header with style chips, three sliders, the dice.
    auto gen = getLocalBounds().reduced (12, 10);
    auto header = gen.removeFromTop (18);
    for (auto* b : { &softStyleButton, &mixStyleButton, &hardStyleButton })
    {
        b->setBounds (header.removeFromRight (40));
        header.removeFromRight (4);
    }
    gen.removeFromTop (6);
    {
        auto row = gen.removeFromTop (20).withTrimmedLeft (64);
        const int width = sourceButtons.empty() ? 0 : (row.getWidth() - 4 * ((int) sourceButtons.size() - 1)) / (int) sourceButtons.size();
        for (auto& b : sourceButtons) { b->setBounds (row.removeFromLeft (width)); row.removeFromLeft (4); }
        gen.removeFromTop (4);
    }
    for (auto* slider : { &angularSlider, &softSlider, &chaosSlider })
        slider->setBounds (gen.removeFromTop (24).withTrimmedLeft (64).withTrimmedRight (40));
    gen.removeFromTop (8);
    if (generateButton != nullptr) generateButton->setBounds (gen.removeFromTop (32));
    gen.removeFromTop (6);
    auto rating = gen.removeFromTop (26);
    const int half = (rating.getWidth() - 6) / 2;
    if (likeButton != nullptr) likeButton->setBounds (rating.removeFromLeft (half));
    if (dislikeButton != nullptr) dislikeButton->setBounds (rating.removeFromRight (half));
}
void GeneratorPanel::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    auto gen = getLocalBounds().reduced (12, 10);
    auto top = gen.removeFromTop (18);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("GENERATE", top, juce::Justification::centredLeft);
    gen.removeFromTop (6);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (dimTextColour);
    g.drawText ("FROM", gen.removeFromTop (20).removeFromLeft (64), juce::Justification::centredLeft);
    gen.removeFromTop (4);
    const std::pair<const char*, const juce::Slider*> rows[] { { "ANGULAR", &angularSlider }, { "SOFT", &softSlider }, { "CHAOS", &chaosSlider } };
    for (const auto& row : rows)
    {
        auto line = gen.removeFromTop (24);
        g.setColour (dimTextColour);
        g.drawText (row.first, line.removeFromLeft (64), juce::Justification::centredLeft);
        g.setColour (textColour);
        g.drawText (juce::String (juce::roundToInt (row.second->getValue())) + "%", line.removeFromRight (36), juce::Justification::centredRight);
    }
}
