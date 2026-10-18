#include "LfoPage.h"

using namespace triad::ui;
namespace mod = triad::mod;

triad::mod::Shape LfoPage::clipboard;

namespace
{
constexpr float pointRadius = 4.5f;

// A tab per LFO: "LFO 2" over the name of what it moves, with a light when it is switched on.
class LfoTab final : public juce::Button
{
public:
    LfoTab (juce::String top, juce::String bottom, bool on, juce::Colour c)
        : Button (bottom), line1 (std::move (top)), line2 (std::move (bottom)), active (on), colour (c)
    {
        setRadioGroupId (4711);
        setClickingTogglesState (false);
        setTooltip (line1 + ": " + line2 + (active ? " (on)" : " (off until you draw on it)"));
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (getToggleState() ? colour.withAlpha (0.22f) : over ? slotRowHover : slotRow);
        g.fillRoundedRectangle (r, 5.0f);
        if (getToggleState()) { g.setColour (colour); g.fillRoundedRectangle (r.removeFromBottom (3.0f), 1.5f); }
        auto area = getLocalBounds().reduced (10, 4).withTrimmedRight (16);
        g.setColour (getToggleState() ? colour : dimTextColour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (line1, area.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (line2, area, juce::Justification::centredLeft, 1, 0.8f);
        const auto light = juce::Rectangle<float> ((float) getWidth() - 16.0f, 7.0f, 8.0f, 8.0f);
        g.setColour (active ? ledColour : juce::Colour (0xff4a504c));
        g.fillEllipse (light);
    }
private:
    juce::String line1, line2;
    bool active;
    juce::Colour colour;
};

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
ShapeEditor::ShapeEditor (TriadProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("LFO shape");
}
void ShapeEditor::setTarget (int b, int l)
{
    band = b; lfo = l;
    reload();
}
void ShapeEditor::reload()
{
    if (drag != Drag::none) return;
    shape = processor.getLfoInfo (band, lfo).shape;
    if (shape.size() < 2) shape = mod::flat();
    selected.assign (shape.size(), false);
    repaint();
}
void ShapeEditor::commit (bool undoable)
{
    processor.setLfoShape (band, lfo, shape, undoable);
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
    const auto length = processor.getLfoInfo (band, lfo).length;
    const double beats = mod::lengthInBeats (length, 4.0);
    if (beats <= 0.0) return 16;
    const double columnBeats = 4.0 / gridDivision * (triplet ? 2.0 / 3.0 : 1.0);
    return juce::jlimit (8, 128, (int) std::round (beats / columnBeats));
}
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
        p.y = std::round (p.y * 16.0f) / 16.0f;
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
    const double dt = lastFrame > 0.0 ? time - lastFrame : 1.0;
    if (lastFrame > 0.0 && dt < 1.0 / 65.0) return;   // at most 60 frames a second
    lastFrame = time;
    const auto next = processor.getLfoPhase (band, lfo);
    if (std::abs (next - phase) <= 1.0e-4f) return;
    // Repaint only the strips under the old and the new playing position.
    const auto strip = [this] (float p)
    {
        if (p < 0.0f) return juce::Rectangle<int>();
        const int x = juce::roundToInt (toScreen ({ p, 0.0f, 0.0f }).x);
        return juce::Rectangle<int> (x - 6, 0, 12, getHeight());
    };
    repaint (strip (phase));
    phase = next;
    repaint (strip (phase));
}
void ShapeEditor::paint (juce::Graphics& g)
{
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    auto key = (juce::uint64) (juce::String (getWidth()) + "x" + juce::String (getHeight()) + "@" + juce::String (scale) + "/" + juce::String (band)
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
    g.drawImageTransformed (layer, juce::AffineTransform::scale (1.0f / scale));
    const auto colour = bandColour (band);
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
        g.drawVerticalLine (juce::roundToInt (p.x), r.getY(), r.getBottom());
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    }
}
void ShapeEditor::paintLayer (juce::Graphics& g)
{
    const auto r = plot();
    const auto colour = bandColour (band);
    g.setColour (gridBackground);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    // Grid: columns, beats and quarters of the cycle; eight rows.
    const int cols = columns();
    for (int c = 0; c <= cols; ++c)
    {
        const float x = r.getX() + r.getWidth() * (float) c / (float) cols;
        const bool quarter = (c * 4) % cols == 0;
        g.setColour (juce::Colour (0xffcfd5cc).withAlpha (quarter ? 0.22f : 0.07f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
    }
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
        g.drawText (juce::String (v) + "%", juce::Rectangle<float> (0.0f, r.getBottom() - r.getHeight() * (float) v / 100.0f - 8.0f, 34.0f, 16.0f),
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
        processor.beginLfoEdit (band, lfo);
    }
    else if (const int c = hitCurve (e.position); c >= 0)
    {
        drag = Drag::curve;
        dragIndex = c;
        processor.beginLfoEdit (band, lfo);
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
        processor.setLfoShape (band, lfo, shape, false);
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
            processor.setLfoShape (band, lfo, shape, false);
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
        processor.setLfoShape (band, lfo, shape, false);
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
        processor.beginLfoEdit (band, lfo);
        shape.erase (shape.begin() + p);
    }
    else if (const int c = hitCurve (e.position); c >= 0)
    {
        processor.beginLfoEdit (band, lfo);
        shape[(size_t) c].curve = 0.0f;
    }
    else
    {
        const auto r = plot();
        const auto p2 = snapPoint ({ (e.position.x - r.getX()) / r.getWidth(), (r.getBottom() - e.position.y) / r.getHeight(), 0.0f },
                                   snap && ! e.mods.isAltDown() && ! e.mods.isCommandDown());
        if (p2.x <= 0.0f || p2.x >= 1.0f) return;
        processor.beginLfoEdit (band, lfo);
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
    processor.beginLfoEdit (band, lfo);
    shape = kept;
    selected.assign (shape.size(), false);
    commit (false);
}
void ShapeEditor::copyShape()
{
    LfoPage::clipboard = shape;
    juce::SystemClipboard::copyTextToClipboard (mod::toText (shape));
}
void ShapeEditor::pasteShape()
{
    auto pasted = mod::fromText (juce::SystemClipboard::getTextFromClipboard());
    if (pasted.size() < 2) pasted = LfoPage::clipboard;
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
        if (c == 'z' && ! mods.isShiftDown()) { if (processor.undoLfo (band, lfo)) { reload(); if (onChange) onChange(); } return true; }
        if ((c == 'z' && mods.isShiftDown()) || c == 'y') { if (processor.redoLfo (band, lfo)) { reload(); if (onChange) onChange(); } return true; }
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
    menu.addItem (pasteId, "Paste wave", LfoPage::clipboard.size() >= 2 || juce::SystemClipboard::getTextFromClipboard().contains ("TriadLfoShape"));
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

// ---- LFO page -----------------------------------------------------------------------------------------
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

LfoPage::LfoPage (TriadProcessor& p) : processor (p), editor (p)
{
    setTitle ("LFO");
    tabView.setViewedComponent (&tabHolder, false);
    tabView.setScrollBarsShown (false, true);
    tabView.setScrollBarThickness (5);
    addAndMakeVisible (tabView);
    addLfoButton.onClick = [this]
    {
        const int index = processor.addLfo (band);
        if (index >= 0) select (band, index);
    };
    addAndMakeVisible (addLfoButton);
    addAndMakeVisible (editor);
    editor.onChange = [this] { updateToolbar(); refresh(); };
    lengthButton.setTooltip ("LFO length: one cycle of the shape (synced to your song's tempo)");
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
    undoButton.onClick = [this] { if (processor.undoLfo (band, lfo)) { editor.reload(); updateToolbar(); } };
    redoButton.onClick = [this] { if (processor.redoLfo (band, lfo)) { editor.reload(); updateToolbar(); } };
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
    saveButton.setTooltip ("Save this LFO's shape to the USER folder (it is kept for every project)");
    saveButton.onClick = [this]
    {
        const auto initial = "My shape " + juce::String ((int) triad::userShapes::load().size() + 1);
        if (askName == nullptr) { saveShapeToUser (initial); return; }
        juce::Component::SafePointer<LfoPage> safe (this);
        askName ("Save LFO shape to USER", initial, [safe] (const juce::String& name) { if (safe != nullptr) safe->saveShapeToUser (name); });
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
    // Generator.
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
            repaint (generatorArea);
            if (! slider->isMouseButtonDown()) saveGeneratorStyle();
        };
        s.slider->onDragEnd = [this] { saveGeneratorStyle(); };
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
    hardStyleButton.onClick = [this] { setGeneratorStyle (1.0f, 0.0f, (float) chaosSlider.getValue() * 0.01f); };
    mixStyleButton.onClick = [this] { setGeneratorStyle (1.0f, 0.6f, (float) chaosSlider.getValue() * 0.01f); };
    softStyleButton.onClick = [this] { setGeneratorStyle (0.0f, 1.0f, (float) chaosSlider.getValue() * 0.01f); };
    for (auto* b : { &hardStyleButton, &mixStyleButton, &softStyleButton })
    {
        b->setColour (juce::TextButton::buttonColourId, slotRow);
        addAndMakeVisible (*b);
    }
    generateButton = std::make_unique<GenerateButton>();
    generateButton->onClick = [this] { generate(); };
    addAndMakeVisible (*generateButton);
    addRouteButton.setTooltip ("Send this LFO to another knob in this band: a mix, the band's volume, or a knob inside one of its effects "
                               "(from the list, or LEARN: move the knob in the effect's window)");
    addRouteButton.onClick = [this]
    {
        if (processor.isLearningParameter()) { processor.cancelParameterLearn(); updateToolbar(); return; }
        enum { learnId = 100000 };
        juce::PopupMenu menu;
        menu.addSectionHeader ("ROUTE " + processor.getLfoInfo (band, lfo).name.toUpperCase() + " TO");
        std::vector<TriadProcessor::Destination> list;
        addDestinationItems (menu, processor, band, nullptr, list);
        menu.addSeparator();
        menu.addItem (learnId, "LEARN: move a knob in an effect's window...", processor.getSlotCount (band) > 0);
        juce::Component::SafePointer<LfoPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addRouteButton), [safe, list] (int choice)
        {
            if (safe == nullptr) return;
            if (choice == learnId) { safe->startLearn(); return; }
            if (choice < 1 || choice > (int) list.size()) return;
            safe->processor.addRoute (safe->band, safe->lfo, list[(size_t) choice - 1]);
            safe->refresh();
        });
    };
    addAndMakeVisible (addRouteButton);
    removeLfoButton.onClick = [this]
    {
        if (processor.removeLfo (band, lfo)) select (band, juce::jmax (0, lfo - 1));
    };
    addChildComponent (removeLfoButton);
    select (0, processor.getBandLfo (0));
}
LfoPage::~LfoPage() = default;

std::vector<LfoPage::PaletteShape> LfoPage::paletteShapes() const
{
    std::vector<PaletteShape> list;
    if (paletteCategory == userFolder)
        for (auto& e : triad::userShapes::load()) list.push_back ({ e.name, std::move (e.shape), e.file });
    else
        for (const int i : mod::presetsIn (paletteCategory)) list.push_back ({ mod::presetName (i), mod::preset (i), {} });
    return list;
}
juce::String LfoPage::paletteFolderName() const
{
    return paletteCategory == userFolder ? juce::String ("USER") : mod::presetCategoryName (paletteCategory);
}
void LfoPage::rebuildPalette()
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
int LfoPage::getNumPalettePages() const
{
    return juce::jmax (1, ((int) paletteShapes().size() + paletteSize - 1) / paletteSize);
}
void LfoPage::setPaletteCategory (int category)
{
    category = category == userFolder ? userFolder : juce::jlimit (0, mod::numPresetCategories() - 1, category);
    if (category == paletteCategory && ! presetButtons.empty()) return;
    paletteCategory = category;
    palettePage = 0;
    settings->setPaletteCategory (category);
    rebuildPalette();
}
void LfoPage::setPalettePage (int page)
{
    page = juce::jlimit (0, getNumPalettePages() - 1, page);
    if (page == palettePage) return;
    palettePage = page;
    rebuildPalette();
}
void LfoPage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (paletteArea.contains (e.getEventRelativeTo (this).getPosition()) && getNumPalettePages() > 1)
    {
        const float delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs (delta) > 0.0f) setPalettePage (palettePage + (delta < 0.0f ? 1 : -1));
        return;
    }
    Component::mouseWheelMove (e, wheel);
}
juce::File LfoPage::saveShapeToUser (const juce::String& name)
{
    const auto file = triad::userShapes::save (name, processor.getLfoInfo (band, lfo).shape);
    if (file == juce::File())
    {
        processor.setStatus ("Could not save the LFO shape");
        return {};
    }
    processor.setStatus ("Saved LFO shape \"" + file.getFileNameWithoutExtension() + "\" to USER");
    paletteCategory = userFolder;
    settings->setPaletteCategory (userFolder);
    const auto list = paletteShapes();
    for (int k = 0; k < (int) list.size(); ++k)
        if (list[(size_t) k].file == file) palettePage = k / paletteSize;
    rebuildPalette();
    return file;
}
void LfoPage::showUserShapeMenu (const juce::File& file)
{
    enum { renameId = 1, deleteId, folderId };
    juce::PopupMenu menu;
    menu.addSectionHeader (file.getFileNameWithoutExtension());
    menu.addItem (renameId, "Rename...", askName != nullptr);
    menu.addItem (deleteId, "Delete");
    menu.addSeparator();
    menu.addItem (folderId, "Show the USER folder");
    juce::Component::SafePointer<LfoPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, file] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == renameId && safe->askName != nullptr)
            safe->askName ("Rename LFO shape", file.getFileNameWithoutExtension(), [safe, file] (const juce::String& name)
            {
                if (safe != nullptr && triad::userShapes::rename (file, name)) safe->rebuildPalette();
            });
        else if (choice == deleteId) { triad::userShapes::remove (file); safe->rebuildPalette(); }
        else if (choice == folderId) { TriadSettings::lfoShapeFolder().createDirectory(); TriadSettings::lfoShapeFolder().revealToUser(); }
    });
}
void LfoPage::showCategoryMenu()
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
    menu.addItem (saveId, "Save this LFO's shape to USER...");
    menu.addItem (folderId, "Show the USER folder");
    juce::Component::SafePointer<LfoPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&categoryButton), [safe] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        if (choice == saveId) safe->saveButton.onClick();
        else if (choice == folderId) { TriadSettings::lfoShapeFolder().createDirectory(); TriadSettings::lfoShapeFolder().revealToUser(); }
        else if (choice == userId) safe->setPaletteCategory (userFolder);
        else safe->setPaletteCategory (choice - 1);
    });
}
triad::mod::Style LfoPage::getGeneratorStyle() const
{
    return { (float) angularSlider.getValue() * 0.01f, (float) softSlider.getValue() * 0.01f, (float) chaosSlider.getValue() * 0.01f };
}
void LfoPage::setGeneratorStyle (float angular, float soft, float chaos)
{
    angularSlider.setValue (juce::jlimit (0.0f, 1.0f, angular) * 100.0, juce::dontSendNotification);
    softSlider.setValue (juce::jlimit (0.0f, 1.0f, soft) * 100.0, juce::dontSendNotification);
    chaosSlider.setValue (juce::jlimit (0.0f, 1.0f, chaos) * 100.0, juce::dontSendNotification);
    saveGeneratorStyle();
    repaint (generatorArea);
}
void LfoPage::saveGeneratorStyle()
{
    const auto style = getGeneratorStyle();
    settings->setGenerator (style.angular, style.soft, style.chaos);
}
void LfoPage::generate()
{
    mod::GridInfo grid;
    grid.columns = editor.columns();
    grid.rows = 16;
    grid.aspect = editor.plotAspect();
    // Never the shape that is there already, nor one of the last few the dice drew.
    auto shape = mod::generate (getGeneratorStyle(), grid, generatorRandom);
    for (int tries = 0; tries < 8; ++tries)
    {
        const auto text = mod::toText (shape);
        if (! (shape == editor.getShape()) && ! recentShapes.contains (text)) break;
        shape = mod::generate (getGeneratorStyle(), grid, generatorRandom);
    }
    recentShapes.add (mod::toText (shape));
    while (recentShapes.size() > 16) recentShapes.remove (0);
    editor.apply ([shape] (const mod::Shape&) { return shape; });
}

juce::String LfoPage::signature() const
{
    juce::String s;
    s << processor.getBandCount() << ":" << processor.getStructureVersion() << ":";
    for (int i = 0; i < processor.getLfoCount (band); ++i)
    {
        const auto l = processor.getLfoInfo (band, i);
        s << l.id << l.target << (l.active ? "+" : "-") << ";";
    }
    s << "|" << processor.getRouteCount (band);
    for (int r : processor.getRoutesFrom (band, lfo)) s << "," << r;
    return s;
}
void LfoPage::select (int b, int l)
{
    band = TriadProcessor::isExtraChain (b) ? b : juce::jlimit (0, juce::jmax (0, processor.getBandCount() - 1), b);
    lfo = juce::jlimit (0, juce::jmax (0, processor.getLfoCount (band) - 1), l);
    editor.setTarget (band, lfo);
    builtSignature.clear();
    refresh();
}
void LfoPage::startLearn()
{
    if (! processor.beginParameterLearn (band, lfo))
    {
        processor.setStatus (TriadProcessor::isExtraChain (band) ? "LEARN: " + TriadProcessor::chainName (band) + " has no effect loaded"
                                                                 : juce::String ("LEARN: this band has no effect loaded"));
        return;
    }
    // With a single effect in the band, open its window to move a knob in.
    int loaded = -1, count = 0;
    for (int s = 0; s < processor.getSlotCount (band); ++s)
        if (processor.getSlotInfo (band, s).loaded) { loaded = s; ++count; }
    if (count == 1) processor.showEffect (band, loaded);
    updateToolbar();
}
void LfoPage::refresh()
{
    if (processor.isLearningParameter())
    {
        if (processor.pollParameterLearn() >= 0) rebuild();
        updateToolbar();
    }
    if (! processor.isValidBand (band) || lfo >= processor.getLfoCount (band))
    {
        if (! processor.isValidBand (band)) band = juce::jmin (band, processor.getBandCount() - 1);
        lfo = juce::jlimit (0, juce::jmax (0, processor.getLfoCount (band) - 1), lfo);
        editor.setTarget (band, lfo);
    }
    if (signature() != builtSignature) rebuild();
    for (auto& row : routeRows) row->update();
    updateToolbar();
}
void LfoPage::rebuild()
{
    builtSignature = signature();
    editor.setTarget (band, lfo);
    bandButtons.clear();
    // PRE first, then the bands, then POST: each has its own LFOs, like a band.
    const int count = processor.getBandCount();
    for (int b = -1; b <= count; ++b)
    {
        const int index = b < 0 ? TriadProcessor::preBand : b == count ? TriadProcessor::postBand : b;
        auto button = std::make_unique<juce::TextButton> (TriadProcessor::isExtraChain (index) ? TriadProcessor::chainName (index)
                                                                                              : count > 8 ? "B" + juce::String (b + 1) : "BAND " + juce::String (b + 1));
        button->setColour (juce::TextButton::buttonColourId, index == band ? bandColour (index).withAlpha (0.85f) : slotRow);
        button->setColour (juce::TextButton::textColourOffId, index == band ? juce::Colour (0xff111312) : bandColour (index));
        button->onClick = [this, index] { select (index, juce::jmax (0, processor.getBandLfo (index))); };
        addAndMakeVisible (*button);
        bandButtons.push_back (std::move (button));
    }
    tabs.clear();
    for (int i = 0; i < processor.getLfoCount (band); ++i)
    {
        const auto info = processor.getLfoInfo (band, i);
        const auto top = info.name + (info.bandLfo ? u8 (" \xc2\xb7 BAND") : info.slot >= 0 ? u8 (" \xc2\xb7 SLOT ") + juce::String (info.slot + 1) : juce::String());
        auto tab = std::make_unique<LfoTab> (top, info.target, info.active, bandColour (band));
        tab->setToggleState (i == lfo, juce::dontSendNotification);
        tab->onClick = [this, i] { select (band, i); };
        tabHolder.addAndMakeVisible (*tab);
        tabs.push_back (std::move (tab));
    }
    routeRows.clear();
    for (int r : processor.getRoutesFrom (band, lfo))
    {
        auto row = std::make_unique<RouteRow> (processor, band, r, false, bandColour (band));
        addAndMakeVisible (*row);
        routeRows.push_back (std::move (row));
    }
    const auto info = processor.getLfoInfo (band, lfo);
    removeLfoButton.setVisible (! info.bandLfo && info.slot < 0);
    resized();
    repaint();
}
void LfoPage::updateToolbar()
{
    addRouteButton.setButtonText (processor.isLearningParameter() ? "LEARNING: MOVE A KNOB... (CLICK TO STOP)" : "+ ROUTE TO...");
    if (processor.isLearningParameter()) addRouteButton.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.35f));
    else addRouteButton.removeColour (juce::TextButton::buttonColourId);
    const auto info = processor.getLfoInfo (band, lfo);
    lengthButton.setButtonText (juce::String (mod::lengths()[(size_t) juce::jlimit (0, (int) mod::lengths().size() - 1, info.length)].label));
    gridButton.setButtonText ("GRID 1/" + juce::String (editor.gridDivision) + (editor.triplet ? "T" : ""));
    snapButton.setToggleState (editor.snap, juce::dontSendNotification);
    undoButton.setEnabled (info.canUndo);
    redoButton.setEnabled (info.canRedo);
}
void LfoPage::stepLength (int direction)
{
    // Steps through the straight lengths (1/16 ... 32 bars), as ShaperBox's arrows do.
    const auto& list = mod::lengths();
    const int current = processor.getLfoInfo (band, lfo).length;
    int next = current;
    for (int i = current + direction; i >= 0 && i < (int) list.size(); i += direction)
        if (list[(size_t) i].column == 1) { next = i; break; }
    processor.setLfoLength (band, lfo, next);
    editor.repaint();
    updateToolbar();
}
void LfoPage::showLengthMenu()
{
    // Four columns like ShaperBox: TRIPLET | STRAIGHT | DOTTED | FREE.
    juce::PopupMenu menu;
    const auto& list = mod::lengths();
    const int current = processor.getLfoInfo (band, lfo).length;
    const char* headers[] { "TRIPLET", "STRAIGHT", "DOTTED", "FREE (Hz)" };
    for (int column = 0; column < 4; ++column)
    {
        if (column > 0) menu.addColumnBreak();
        menu.addSectionHeader (headers[column]);
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].column == column) menu.addItem (i + 1, list[(size_t) i].label, true, i == current);
    }
    juce::Component::SafePointer<LfoPage> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lengthButton), [safe] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->processor.setLfoLength (safe->band, safe->lfo, choice - 1);
        safe->editor.repaint();
        safe->updateToolbar();
    });
}
void LfoPage::showGridMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("GRID");
    for (int d : { 4, 8, 16, 32, 64 }) menu.addItem (d, "1/" + juce::String (d), true, editor.gridDivision == d && ! editor.triplet);
    menu.addSeparator();
    menu.addItem (1000, "Triplet", true, editor.triplet);
    menu.addItem (1001, "Snap to grid", true, editor.snap);
    juce::Component::SafePointer<LfoPage> safe (this);
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
void LfoPage::resized()
{
    auto r = getLocalBounds();
    auto bandRow = r.removeFromTop (26);
    for (auto& b : bandButtons) { b->setBounds (bandRow.removeFromLeft (chipWidth()).withTrimmedRight (4)); }
    r.removeFromTop (6);
    auto tabRow = r.removeFromTop (44);
    addLfoButton.setBounds (tabRow.removeFromRight (30).withSizeKeepingCentre (28, 28));
    tabRow.removeFromRight (6);
    tabView.setBounds (tabRow);
    int x = 0;
    for (auto& t : tabs) { t->setBounds (x, 0, 156, 38); x += 160; }
    tabHolder.setSize (juce::jmax (x, tabRow.getWidth()), 38);
    r.removeFromTop (8);
    routingArea = r.removeFromRight (250);
    generatorArea = routingArea.removeFromBottom (158);
    routingArea.removeFromBottom (8);
    r.removeFromRight (12);
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
    // Generator panel: header with style chips, three sliders, the dice.
    auto gen = generatorArea.reduced (12, 10);
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
    auto panel = routingArea.reduced (10, 0).withTrimmedTop (48);
    for (auto& row : routeRows) { row->setBounds (panel.removeFromTop (54)); panel.removeFromTop (4); }
    addRouteButton.setBounds (panel.removeFromTop (28));
    removeLfoButton.setBounds (routingArea.reduced (10, 0).removeFromBottom (28).translated (0, -46));
}
void LfoPage::paint (juce::Graphics& g)
{
    if (paletteCategory == userFolder && presetButtons.empty())
    {
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("No saved shapes yet: press SAVE to keep this LFO's shape here", paletteArea.withTrimmedLeft (226),
                    juce::Justification::centredLeft);
    }
    g.setColour (panel);
    g.fillRoundedRectangle (routingArea.toFloat(), 7.0f);
    const auto info = processor.getLfoInfo (band, lfo);
    auto header = routingArea.reduced (12, 8);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("ROUTING", header.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ((TriadProcessor::isExtraChain (band) ? TriadProcessor::chainName (band) : "Band " + juce::String (band + 1)) + u8 (" \xc2\xb7 ") + info.name + u8 (" \xe2\x86\x92"), header.removeFromTop (18),
                juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (11.0f));
    auto notes = routingArea.reduced (12, 10).removeFromBottom (44);
    if (notes.getY() > addRouteButton.getBottom() + 6)
        g.drawFittedText ("Amount 100%: the knob follows the line (top = 100%, bottom = 0%). The light switches a route on or off.",
                          notes, juce::Justification::bottomLeft, 3);
    // Generator panel.
    g.setColour (panel);
    g.fillRoundedRectangle (generatorArea.toFloat(), 7.0f);
    {
        auto gen = generatorArea.reduced (12, 10);
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
    g.setColour (dimTextColour);
    // How to edit, under the band row.
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (u8 ("Double-click: add point  \xc2\xb7  Drag: move  \xc2\xb7  Double-click a point: delete  \xc2\xb7  Drag the small dot: curve"
                    "  \xc2\xb7  Right-click: more"),
                juce::Rectangle<int> ((int) bandButtons.size() * chipWidth() + 8, 0, getWidth() - (int) bandButtons.size() * chipWidth() - 8, 26),
                juce::Justification::centredRight);
}
