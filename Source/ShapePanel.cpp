#include "ShapePanel.h"

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
        setTooltip (name + " (replaces the shape)");
    }
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu) { onMenu(); return; }
        Button::mouseDown (e);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (down ? juce::Colour (0xff3d4a36) : over ? slotRowHover : juce::Colour (0xff1a1d1c));
        g.fillRoundedRectangle (r, 4.0f);
        const auto area = r.reduced (6.0f, 5.0f);
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
        g.setColour (over ? colour : textColour.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (shape.size() > 40 ? 1.1f : 1.6f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));
    }
private:
    mod::Shape shape;
    juce::Colour colour;
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
                               + "/" + juce::String (columns()) + "/" + juce::String (hoverPoint) + "/" + juce::String (hoverCurve)).hashCode64();
    auto mix = [&key] (int v) { key = key * 1099511628211ull + (juce::uint64) (juce::uint32) v; };
    for (const auto& p : shape) { mix (juce::roundToInt (p.x * 1.0e6f)); mix (juce::roundToInt (p.y * 1.0e6f)); mix (juce::roundToInt (p.curve * 1.0e4f)); }
    for (bool b : selected) mix (b ? 7 : 3);
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
    triad::ui::drawPixelExact (g, layer, scale);
    const auto colour = shapeColour();
    if (drag == Drag::select)
    {
        g.setColour (colour.withAlpha (0.12f)); g.fillRect (selection);
        g.setColour (colour.withAlpha (0.7f)); g.drawRect (selection, 1.0f);
    }
    // Playing position.
    if (phase >= 0.0f)
    {
        const auto r = plot();
        const auto p = toScreen ({ phase, mod::evaluate (shape, phase), 0.0f });
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (p.x - 0.75f, r.getY(), 1.5f, r.getHeight()));   // at sub-pixel positions: glides
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    }
}
void ShapeEditor::paintLayer (juce::Graphics& g)
{
    const auto r = plot();
    const auto colour = shapeColour();
    g.setColour (gridBackground);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const int cols = columns();
    if (isSquareGrid())
    {
        // Triad Time, laid out as ShaperBox's grid: the quarters of both directions make 4 x 4 big squares. Across, each
        // quarter is divided in 4 by bright lines and each of those in 4 again by faint ones (64 columns at GRID 1/64 for a
        // 1-bar pattern); down, each quarter in 4 by faint lines (16 rows).
        for (int c = 0; c <= cols; ++c)
        {
            const float alpha = (c * 4) % cols == 0 ? 0.40f : (c * 16) % cols == 0 ? 0.26f : 0.13f;
            g.setColour (juce::Colour (0xffcfd5cc).withAlpha (alpha));
            g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) c / (float) cols), r.getY(), r.getBottom());
        }
        for (int row = 0; row <= 16; ++row)
        {
            g.setColour (juce::Colour (0xffcfd5cc).withAlpha (row % 4 == 0 ? 0.30f : 0.10f));
            g.drawHorizontalLine (juce::roundToInt (r.getY() + r.getHeight() * (float) row / 16.0f), r.getX(), r.getRight());
        }
    }
    // Grid: columns, beats and quarters of the cycle; eight rows.
    else for (int c = 0; c <= cols; ++c)
    {
        const float x = r.getX() + r.getWidth() * (float) c / (float) cols;
        const bool quarter = (c * 4) % cols == 0;
        g.setColour (juce::Colour (0xffcfd5cc).withAlpha (quarter ? 0.22f : 0.07f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
    }
    if (! isSquareGrid())
        for (int row = 0; row <= 8; ++row)
        {
            const float y = r.getY() + r.getHeight() * (float) row / 8.0f;
            g.setColour (juce::Colour (0xffcfd5cc).withAlpha (row % 4 == 0 ? 0.2f : 0.07f));
            g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
        }
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (dimTextColour);
    for (int q = 1; q < 4; ++q)
        g.drawText (juce::String (q) + "/4", juce::Rectangle<float> (r.getX() + r.getWidth() * (float) q / 4.0f - 20.0f, 2.0f, 40.0f, 16.0f),
                    juce::Justification::centred);
    for (int v : { 100, 75, 50, 25, 0 })
        g.drawText (host != nullptr ? host->valueLabel (v) : juce::String (v) + "%", juce::Rectangle<float> (0.0f, r.getBottom() - r.getHeight() * (float) v / 100.0f - 8.0f, 34.0f, 16.0f),
                    juce::Justification::centredRight);
    // The shape: hatched fill under a solid line.
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
    g.setColour (colour.withAlpha (0.12f));
    g.fillPath (fill);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (fill);
        g.setColour (colour.withAlpha (0.1f));
        for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 9.0f)
            g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
    }
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        g.setColour (sel ? colour : juce::Colour (0xfff2f5ef));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
        if (sel) { g.setColour (juce::Colours::white); g.drawEllipse (juce::Rectangle<float> (radius * 2.0f + 3.0f, radius * 2.0f + 3.0f).withCentre (p), 1.2f); }
    }
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

// ---- Shape panel: the grid, its toolbar and the palette ---------------------------------------------------
ShapePanel::ShapePanel()
{
    addAndMakeVisible (editor);
    editor.onChange = [this] { updateToolbar(); if (onChange) onChange(); };
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
    for (auto* c : std::initializer_list<juce::Component*> { &lengthButton, &prevLength, &nextLength, &gridButton, &snapButton, &clearButton,
                                                            &doubleButton, &halfButton, &reverseButton, &flipButton, &undoButton, &redoButton })
        addAndMakeVisible (*c);
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
    pageLabel.setColour (juce::Label::textColourId, triad::ui::dimTextColour);
    pageLabel.setTooltip ("Page (the mouse wheel over the shapes turns the pages)");
    for (auto* c : std::initializer_list<juce::Component*> { &prevPage, &nextPage, &pageLabel }) addAndMakeVisible (*c);
    rebuildPalette();
}
ShapePanel::~ShapePanel() = default;
void ShapePanel::setHost (ShapeHost* h)
{
    host = h;
    editor.setHost (h);
    updateToolbar();
}
std::vector<ShapePanel::PaletteShape> ShapePanel::paletteShapes() const
{
    std::vector<PaletteShape> list;
    if (paletteCategory == userFolder)
        for (auto& e : triad::userShapes::load()) list.push_back ({ e.name, std::move (e.shape), e.file });
    else
        for (const int i : mod::presetsIn (paletteCategory)) list.push_back ({ mod::presetName (i), mod::preset (i), {} });
    return list;
}
juce::String ShapePanel::paletteFolderName() const
{
    return paletteCategory == userFolder ? juce::String ("USER") : mod::presetCategoryName (paletteCategory);
}
void ShapePanel::rebuildPalette()
{
    for (auto& b : presetButtons) removeChildComponent (b.get());
    presetButtons.clear();
    const auto list = paletteShapes();
    const int pages = juce::jmax (1, ((int) list.size() + paletteSize - 1) / paletteSize);
    palettePage = juce::jlimit (0, pages - 1, palettePage);
    for (int k = palettePage * paletteSize; k < juce::jmin ((int) list.size(), (palettePage + 1) * paletteSize); ++k)
    {
        const auto& entry = list[(size_t) k];
        auto b = std::make_unique<ShapeButton> (entry.name, entry.shape, accent);
        b->onClick = [this, shape = entry.shape] { editor.apply ([shape] (const mod::Shape&) { return shape; }); };
        if (entry.file != juce::File())
        {
            b->setTooltip (entry.name + " (replaces the shape; right-click to rename or delete)");
            b->onMenu = [this, file = entry.file] { showUserShapeMenu (file); };
        }
        addAndMakeVisible (*b);
        presetButtons.push_back (std::move (b));
    }
    categoryButton.setButtonText (paletteFolderName() + u8 (" \xe2\x96\xbe"));
    pageLabel.setText (juce::String (palettePage + 1) + "/" + juce::String (pages), juce::dontSendNotification);
    prevPage.setEnabled (palettePage > 0);
    nextPage.setEnabled (palettePage < pages - 1);
    resized();
    repaint();
}
int ShapePanel::getNumPalettePages() const
{
    return juce::jmax (1, ((int) paletteShapes().size() + paletteSize - 1) / paletteSize);
}
void ShapePanel::setPaletteCategory (int category)
{
    category = category == userFolder ? userFolder : juce::jlimit (0, mod::numPresetCategories() - 1, category);
    if (category == paletteCategory && ! presetButtons.empty()) return;
    paletteCategory = category;
    palettePage = 0;
    settings->setPaletteCategory (category);
    rebuildPalette();
}
void ShapePanel::setPalettePage (int page)
{
    page = juce::jlimit (0, getNumPalettePages() - 1, page);
    if (page == palettePage) return;
    palettePage = page;
    rebuildPalette();
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
    settings->setPaletteCategory (userFolder);
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
    auto add = [&] (int id, const juce::String& name, int count, bool ticked)
    {
        const int pages = (count + paletteSize - 1) / paletteSize;
        menu.addItem (id, name + "  (" + juce::String (count) + (pages > 1 ? ", " + juce::String (pages) + " pages)" : ")"), true, ticked);
    };
    menu.addSectionHeader ("SHAPES BY TYPE");
    bool generatedHeader = false;
    for (int c = 0; c < mod::numPresetCategories(); ++c)
    {
        if (mod::isGeneratedCategory (c) && ! generatedHeader) { menu.addSectionHeader ("GENERATED (FROM THE DICE)"); generatedHeader = true; }
        if (mod::isExtraCategory (c)) menu.addSectionHeader ("EXTRA (EVERY PREVIEW SHAPE)");
        add (c + 1, mod::presetCategoryName (c), (int) mod::presetsIn (c).size(), c == paletteCategory);
    }
    menu.addSectionHeader ("YOUR SHAPES");
    add (userId, "USER", (int) triad::userShapes::load().size(), paletteCategory == userFolder);
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
    toolbar.removeFromRight (3);
    halfButton.setBounds (toolbar.removeFromRight (40));
    toolbar.removeFromRight (3);
    doubleButton.setBounds (toolbar.removeFromRight (40));
    toolbar.removeFromRight (3);
    clearButton.setBounds (toolbar.removeFromRight (30));
    paletteArea = palette;
    categoryButton.setBounds (palette.removeFromLeft (132).withTrimmedRight (4));
    prevPage.setBounds (palette.removeFromLeft (24));
    pageLabel.setBounds (palette.removeFromLeft (34));
    nextPage.setBounds (palette.removeFromLeft (24));
    palette.removeFromLeft (6);
    const int width = juce::jmin (52, palette.getWidth() / paletteSize);
    for (auto& b : presetButtons) b->setBounds (palette.removeFromLeft (width).withTrimmedRight (4));
}
void ShapePanel::paint (juce::Graphics& g)
{
    if (paletteCategory == userFolder && presetButtons.empty())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("No saved shapes yet: press SAVE to keep this shape here", paletteArea.withTrimmedLeft (226),
                    juce::Justification::centredLeft);
    }
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
    for (auto* b : { &hardStyleButton, &mixStyleButton, &softStyleButton })
    {
        b->setColour (juce::TextButton::buttonColourId, slotRow);
        addAndMakeVisible (*b);
    }
    generateButton = std::make_unique<GenerateButton>();
    generateButton->onClick = [this] { generate(); };
    addAndMakeVisible (*generateButton);
}
triad::mod::Style GeneratorPanel::getStyle() const
{
    return { (float) angularSlider.getValue() * 0.01f, (float) softSlider.getValue() * 0.01f, (float) chaosSlider.getValue() * 0.01f };
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
    // Never the shape that is there already, nor one of the last few the dice drew.
    auto shape = mod::generate (getStyle(), grid, random);
    for (int tries = 0; tries < 8; ++tries)
    {
        const auto text = mod::toText (shape);
        if (! (shape == target->getShape()) && ! recentShapes.contains (text)) break;
        shape = mod::generate (getStyle(), grid, random);
    }
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
    for (auto* slider : { &angularSlider, &softSlider, &chaosSlider })
        slider->setBounds (gen.removeFromTop (24).withTrimmedLeft (64).withTrimmedRight (40));
    gen.removeFromTop (8);
    if (generateButton != nullptr) generateButton->setBounds (gen.removeFromTop (32));
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
