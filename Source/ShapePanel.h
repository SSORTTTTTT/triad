#pragma once
#include "Look.h"
#include "Generator.h"
#include "Settings.h"

// Triad's shape editing, shared by the LFO tab and Triad Time: the grid editor, its toolbar (length, grid,
// snap, 2x, 1/2, flip, undo...), the preset palette (every folder, the USER folder) and the shape generator.
// What is edited (an LFO of a band, or one of Triad Time's envelopes) is a ShapeHost.

// The shape being edited and what goes with it.
struct ShapeHost
{
    virtual ~ShapeHost() = default;
    virtual triad::mod::Shape getShape() const = 0;
    virtual void setShape (const triad::mod::Shape&, bool undoable) = 0;   // undoable: a step of its own
    virtual void beginEdit() {}                       // a drag (or a double-click) starts: one undo step for all of it
    virtual bool canUndo() const { return false; }
    virtual bool canRedo() const { return false; }
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }
    virtual int getLength() const = 0;                // index into triad::mod::lengths()
    virtual void setLength (int) = 0;
    virtual bool allowsLength (int index) const { juce::ignoreUnused (index); return true; }
    virtual float getPhase() const = 0;               // the playing position 0..1 (< 0: not playing)
    virtual juce::Colour getColour() const = 0;
    virtual juce::String valueLabel (int percent) const { return juce::String (percent) + "%"; }   // the scale on the left
    virtual void setStatus (const juce::String&) {}
};

// The grid, in the spirit of ShaperBox / Gross Beat / Serum: points on a grid that snap by default,
// segments you can bend, and the playing position shown as it moves.
//   Add a point: double-click.        Move: drag a point (Alt / Cmd: no snapping).
//   Delete: double-click a point.     Curve: drag the small dot in the middle of a line.
//   Select: drag in empty space.      Right-click: flip, copy, paste, grid options.
class ShapeEditor final : public juce::Component
{
public:
    ShapeEditor() { setWantsKeyboardFocus (true); setTitle ("Shape"); }
    void setHost (ShapeHost*);
    ShapeHost* getHost() const noexcept { return host; }
    void reload();                                   // re-read the shape from the host
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
    bool undo();
    bool redo();
    // Grid: note value of one column (4 = 1/4, 8 = 1/8 ...), triplets, snapping.
    int gridDivision = 16;
    bool triplet = false, snap = true;
    int columns() const;
    juce::Rectangle<float> plot() const;
    float plotAspect() const;                        // the grid's width / height on screen
    juce::Point<float> toScreen (const triad::mod::Point&) const;
    const triad::mod::Shape& getShape() const noexcept { return shape; }
    std::function<void()> onChange;
    int getLayerRenders() const noexcept { return layerRenders; }
    void frame (double time);   // every frame of the screen (up to 60 a second): the playing position
    static triad::mod::Shape clipboard;
private:
    void paintLayer (juce::Graphics&);
    int hitPoint (juce::Point<float>) const;
    int hitCurve (juce::Point<float>) const;
    juce::Point<float> curveHandle (int segment) const;
    triad::mod::Point snapPoint (triad::mod::Point, bool snapping) const;
    void commit (bool undoable);
    juce::Colour shapeColour() const;

    ShapeHost* host = nullptr;
    triad::mod::Shape shape { triad::mod::flat() }, dragStart;
    std::vector<bool> selected;
    enum class Drag { none, points, curve, select } drag = Drag::none;
    int dragIndex = -1, hoverPoint = -1, hoverCurve = -1;
    juce::Point<float> mouseStart;
    juce::Rectangle<float> selection;
    std::vector<bool> selectionStart;
    float phase = -1.0f;
    // Everything but the playing position is drawn once into an image (until the shape, selection, hover,
    // grid or size changes), so following the playhead costs a narrow strip per frame.
    juce::Image layer;
    juce::int64 layerKey = 0;
    int layerRenders = 0;
    double lastFrame = 0.0;
    juce::VBlankAttachment frames { this, [this] (double t) { frame (t); } };
};

// The USER folder of the palette: shapes Thomas saved, one file each (.triadlfo) in ~/Music/Triad/LFO Shapes,
// so every Triad, every Triad Time and every project sees them.
namespace triad::userShapes
{
struct Entry { juce::String name; triad::mod::Shape shape; juce::File file; };
std::vector<Entry> load();                                              // sorted by name
juce::File save (const juce::String& name, const triad::mod::Shape&);   // a new name if it is taken; {} on failure
bool rename (const juce::File&, const juce::String& newName);
bool remove (const juce::File&);                                        // to the Trash when there is one
}

// The grid with its toolbar and the preset palette under it.
class ShapePanel final : public juce::Component
{
public:
    ShapePanel();
    ~ShapePanel() override;
    void setHost (ShapeHost*);                   // what is edited (reloads the grid)
    ShapeEditor& getEditor() noexcept { return editor; }
    void updateToolbar();
    // The preset palette shows one category (folder) at a time, a page of up to 11 shapes at a time;
    // big folders have several pages (< and >, or the mouse wheel over the palette).
    static constexpr int paletteSize = 11;
    static constexpr int userFolder = 1000;   // the USER folder (saved shapes), after the preset folders
    void setPaletteCategory (int);            // a preset category, or userFolder
    int getPaletteCategory() const noexcept { return paletteCategory; }
    void setPalettePage (int);
    int getPalettePage() const noexcept { return palettePage; }
    int getNumPalettePages() const;
    int getNumPaletteButtons() const noexcept { return (int) presetButtons.size(); }
    juce::Button* getPaletteButton (int i) const { return i >= 0 && i < (int) presetButtons.size() ? presetButtons[(size_t) i].get() : nullptr; }
    // SAVE: keeps the shape in the USER folder (asks for a name through askName, when set).
    std::function<void (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)>)> askName;
    juce::File saveShapeToUser (const juce::String& name);   // then shows USER on the page with it
    juce::Button& getSaveButton() noexcept { return saveButton; }
    void showUserShapeMenu (const juce::File&);
    std::function<void()> onChange;           // the shape or the length changed
    juce::String lengthTooltip { "Length: one cycle of the shape (synced to your song's tempo)" };
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void showLengthMenu();
    void showGridMenu();
    void stepLength (int direction);
    void showCategoryMenu();
    void rebuildPalette();
    struct PaletteShape { juce::String name; triad::mod::Shape shape; juce::File file; };
    std::vector<PaletteShape> paletteShapes() const;   // the shapes in the open folder
    juce::String paletteFolderName() const;

    ShapeHost* host = nullptr;
    ShapeEditor editor;
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton lengthButton, gridButton, snapButton { "SNAP" }, doubleButton { "2x" }, halfButton { "1/2" };
    triad::ui::IconButton prevLength { triad::ui::IconButton::Icon::left, "Shorter" },
                          nextLength { triad::ui::IconButton::Icon::right, "Longer" },
                          clearButton { triad::ui::IconButton::Icon::trash, "Reset to a flat line at 100%" },
                          reverseButton { triad::ui::IconButton::Icon::reverse, "Reverse (flip horizontally)" },
                          flipButton { triad::ui::IconButton::Icon::flipVertical, "Flip vertically" },
                          undoButton { triad::ui::IconButton::Icon::undo, "Undo" },
                          redoButton { triad::ui::IconButton::Icon::redo, "Redo" };
    std::vector<std::unique_ptr<juce::Button>> presetButtons;
    int paletteCategory = 0, palettePage = 0;
    juce::TextButton categoryButton;
    triad::ui::IconButton prevPage { triad::ui::IconButton::Icon::left, "Previous page" },
                          nextPage { triad::ui::IconButton::Icon::right, "Next page" };
    juce::Label pageLabel;
    juce::Rectangle<int> paletteArea;
    juce::SharedResourcePointer<TriadSettings> settings;
};

// The shape generator: Angular / Soft / Chaos, HARD / MIX / SOFT, and the dice that draws a new shape on the
// target editor's grid (undoable).
class GeneratorPanel final : public juce::Component
{
public:
    GeneratorPanel();
    void setTarget (ShapeEditor* e) noexcept { target = e; }
    void generate();
    void setStyle (float angular, float soft, float chaos);   // 0..1 each
    triad::mod::Style getStyle() const;
    void setSeed (juce::int64 seed) { random.setSeed (seed); }
    juce::Button& getGenerateButton() noexcept { return *generateButton; }
    juce::Slider& getChaosSlider() noexcept { return chaosSlider; }
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void saveStyle();
    ShapeEditor* target = nullptr;
    juce::Slider angularSlider, softSlider, chaosSlider;
    juce::TextButton hardStyleButton { "HARD" }, mixStyleButton { "MIX" }, softStyleButton { "SOFT" };
    std::unique_ptr<juce::Button> generateButton;
    juce::Random random;
    juce::StringArray recentShapes;
    juce::SharedResourcePointer<TriadSettings> settings;
};
