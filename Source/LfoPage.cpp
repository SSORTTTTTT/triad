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

// A preset shape in the palette under the grid, drawn as a small picture.
class ShapeButton final : public juce::Button
{
public:
    ShapeButton (int index, juce::Colour c) : Button (mod::presetName (index)), shape (mod::preset (index)), colour (c)
    {
        setTooltip (mod::presetName (index) + " (replaces the shape)");
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (down ? juce::Colour (0xff3d4a36) : over ? slotRowHover : juce::Colour (0xff1a1d1c));
        g.fillRoundedRectangle (r, 4.0f);
        const auto area = r.reduced (6.0f, 5.0f);
        juce::Path p;
        for (int i = 0; i <= 48; ++i)
        {
            const float x = (float) i / 48.0f;
            const auto point = juce::Point<float> (area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * mod::evaluate (shape, x));
            if (i == 0) p.startNewSubPath (point); else p.lineTo (point);
        }
        g.setColour (over ? colour : textColour.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }
private:
    mod::Shape shape;
    juce::Colour colour;
};
}

// ---- Shape editor -----------------------------------------------------------------------------------
ShapeEditor::ShapeEditor (TriadProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("LFO shape");
    startTimerHz (30);
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

void ShapeEditor::timerCallback()
{
    const auto next = processor.getLfoPhase (band, lfo);
    if (std::abs (next - phase) > 1.0e-4f) { phase = next; repaint(); }
}
void ShapeEditor::paint (juce::Graphics& g)
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
    // Playing position.
    if (phase >= 0.0f)
    {
        const auto p = toScreen ({ phase, mod::evaluate (shape, phase), 0.0f });
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (p.x), r.getY(), r.getBottom());
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    }
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
    if (drag == Drag::select)
    {
        g.setColour (colour.withAlpha (0.12f)); g.fillRect (selection);
        g.setColour (colour.withAlpha (0.7f)); g.drawRect (selection, 1.0f);
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
    for (int i = 0; i < mod::numPresets(); ++i)
    {
        auto b = std::make_unique<ShapeButton> (i, accent);
        b->onClick = [this, i] { editor.apply ([i] (const mod::Shape&) { return mod::preset (i); }); };
        addAndMakeVisible (*b);
        presetButtons.push_back (std::move (b));
    }
    addRouteButton.setTooltip ("Send this LFO to another knob in this band");
    addRouteButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("ROUTE " + processor.getLfoInfo (band, lfo).name.toUpperCase() + " TO");
        const auto list = processor.getDestinations (band);
        for (int i = 0; i < (int) list.size(); ++i) menu.addItem (i + 1, list[(size_t) i].second);
        juce::Component::SafePointer<LfoPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addRouteButton), [safe, list] (int choice)
        {
            if (safe == nullptr || choice < 1 || choice > (int) list.size()) return;
            safe->processor.addRoute (safe->band, safe->lfo, list[(size_t) choice - 1].first);
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
    band = juce::jlimit (0, juce::jmax (0, processor.getBandCount() - 1), b);
    lfo = juce::jlimit (0, juce::jmax (0, processor.getLfoCount (band) - 1), l);
    editor.setTarget (band, lfo);
    builtSignature.clear();
    refresh();
}
void LfoPage::refresh()
{
    if (band >= processor.getBandCount() || lfo >= processor.getLfoCount (band))
    {
        band = juce::jmin (band, processor.getBandCount() - 1);
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
    for (int b = 0; b < processor.getBandCount(); ++b)
    {
        auto button = std::make_unique<juce::TextButton> ("BAND " + juce::String (b + 1));
        button->setColour (juce::TextButton::buttonColourId, b == band ? bandColour (b).withAlpha (0.85f) : slotRow);
        button->setColour (juce::TextButton::textColourOffId, b == band ? juce::Colour (0xff111312) : bandColour (b));
        button->onClick = [this, b] { select (b, processor.getBandLfo (b)); };
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
    for (auto& b : bandButtons) { b->setBounds (bandRow.removeFromLeft (76).withTrimmedRight (4)); }
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
    const int width = juce::jmin (52, palette.getWidth() / juce::jmax (1, (int) presetButtons.size()));
    for (auto& b : presetButtons) b->setBounds (palette.removeFromLeft (width).withTrimmedRight (4));
    auto panel = routingArea.reduced (10, 0).withTrimmedTop (48);
    for (auto& row : routeRows) { row->setBounds (panel.removeFromTop (54)); panel.removeFromTop (4); }
    addRouteButton.setBounds (panel.removeFromTop (28));
    removeLfoButton.setBounds (routingArea.reduced (10, 0).removeFromBottom (28).translated (0, -46));
}
void LfoPage::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (routingArea.toFloat(), 7.0f);
    const auto info = processor.getLfoInfo (band, lfo);
    auto header = routingArea.reduced (12, 8);
    g.setColour (accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("ROUTING", header.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Band " + juce::String (band + 1) + u8 (" \xc2\xb7 ") + info.name + u8 (" \xe2\x86\x92"), header.removeFromTop (18),
                juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (11.0f));
    auto notes = routingArea.reduced (12, 10).removeFromBottom (44);
    g.drawFittedText ("Amount 100%: the knob follows the line (top = 100%, bottom = 0%). The light switches a route on or off.",
                      notes, juce::Justification::bottomLeft, 3);
    // How to edit, under the band row.
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (u8 ("Double-click: add point  \xc2\xb7  Drag: move  \xc2\xb7  Double-click a point: delete  \xc2\xb7  Drag the small dot: curve"
                    "  \xc2\xb7  Right-click: more"),
                juce::Rectangle<int> ((int) bandButtons.size() * 76 + 8, 0, getWidth() - (int) bandButtons.size() * 76 - 8, 26),
                juce::Justification::centredRight);
}
