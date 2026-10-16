#pragma once
#include "Widgets.h"

// The LFO grid, in the spirit of ShaperBox / Gross Beat / Serum: points on a grid that snap by
// default, segments you can bend, and the playing position shown as it moves.
//   Add a point: double-click.        Move: drag a point (Alt / Cmd: no snapping).
//   Delete: double-click a point.     Curve: drag the small dot in the middle of a line.
//   Select: drag in empty space.      Right-click: flip, copy, paste, grid options.
class ShapeEditor final : public juce::Component, private juce::Timer
{
public:
    explicit ShapeEditor (TriadProcessor&);
    void setTarget (int band, int lfo);
    void reload();                                   // re-read the shape from the processor
    void apply (const std::function<triad::mod::Shape (const triad::mod::Shape&)>&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void showMenu();
    void selectAll();
    void deleteSelected();
    void copyShape();
    void pasteShape();
    // Grid: note value of one column (4 = 1/4, 8 = 1/8 ...), triplets, snapping.
    int gridDivision = 16;
    bool triplet = false, snap = true;
    int columns() const;
    juce::Rectangle<float> plot() const;
    juce::Point<float> toScreen (const triad::mod::Point&) const;
    const triad::mod::Shape& getShape() const noexcept { return shape; }
    std::function<void()> onChange;
private:
    void timerCallback() override;
    int hitPoint (juce::Point<float>) const;
    int hitCurve (juce::Point<float>) const;
    juce::Point<float> curveHandle (int segment) const;
    triad::mod::Point snapPoint (triad::mod::Point, bool snapping) const;
    void commit (bool undoable);

    TriadProcessor& processor;
    int band = 0, lfo = 0;
    triad::mod::Shape shape, dragStart;
    std::vector<bool> selected;
    enum class Drag { none, points, curve, select } drag = Drag::none;
    int dragIndex = -1, hoverPoint = -1, hoverCurve = -1;
    juce::Point<float> mouseStart;
    juce::Rectangle<float> selection;
    std::vector<bool> selectionStart;
    float phase = -1.0f;
};

// The LFO tab: band chooser, one tab per LFO (labelled with what it moves), the grid with its
// toolbar and shape palette, and the routing list (where this LFO goes, how much).
class LfoPage final : public juce::Component
{
public:
    explicit LfoPage (TriadProcessor&);
    ~LfoPage() override;
    void refresh();
    void select (int band, int lfo);
    int getBand() const noexcept { return band; }
    int getLfo() const noexcept { return lfo; }
    ShapeEditor& getEditor() noexcept { return editor; }
    void resized() override;
    void paint (juce::Graphics&) override;
    static triad::mod::Shape clipboard;

private:
    void rebuild();
    void updateToolbar();
    void showLengthMenu();
    void showGridMenu();
    void stepLength (int direction);
    juce::String signature() const;

    TriadProcessor& processor;
    int band = 0, lfo = 0;
    juce::String builtSignature;
    std::vector<std::unique_ptr<juce::TextButton>> bandButtons;
    juce::Viewport tabView;
    juce::Component tabHolder;
    std::vector<std::unique_ptr<juce::Button>> tabs;
    triad::ui::IconButton addLfoButton { triad::ui::IconButton::Icon::plus, "Add an LFO to this band" };
    ShapeEditor editor;
    juce::TextButton lengthButton, gridButton, snapButton { "SNAP" }, doubleButton { "2x" }, halfButton { "1/2" };
    triad::ui::IconButton prevLength { triad::ui::IconButton::Icon::left, "Shorter" },
                          nextLength { triad::ui::IconButton::Icon::right, "Longer" },
                          clearButton { triad::ui::IconButton::Icon::trash, "Reset to a flat line at 100%" },
                          reverseButton { triad::ui::IconButton::Icon::reverse, "Reverse (flip horizontally)" },
                          flipButton { triad::ui::IconButton::Icon::flipVertical, "Flip vertically" },
                          undoButton { triad::ui::IconButton::Icon::undo, "Undo" },
                          redoButton { triad::ui::IconButton::Icon::redo, "Redo" };
    std::vector<std::unique_ptr<juce::Button>> presetButtons;
    std::vector<std::unique_ptr<triad::ui::RouteRow>> routeRows;
    juce::TextButton addRouteButton { "+ ROUTE TO..." }, removeLfoButton { "REMOVE LFO" };
    juce::Rectangle<int> routingArea;
};
