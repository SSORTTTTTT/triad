#pragma once
#include "Look.h"
#include "Generator.h"
#include "Settings.h"
#include "AudioEngine.h"

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
    virtual float getPhaseNow() const { return getPhase(); }   // the same, carried on to this very moment (smooth motion)
    virtual juce::Colour getColour() const = 0;
    virtual juce::String valueLabel (int percent) const { return juce::String (percent) + "%"; }   // the scale on the left
    // Instead of 0..100% on the left: labels at heights 0..1 (0.18.0, band-edge LFOs: 20, 50, 100 ... 20k Hz), each
    // with a faint line across.
    virtual std::vector<std::pair<float, juce::String>> valueMarks() const { return {}; }
    // Triad Time's grid, laid out as ShaperBox's: 4 x 4 big squares; across, every quarter divided in 4 (bright lines) and
    // those in 4 again (faint: 64 columns at GRID 1/64 for a 1-bar pattern); down, every quarter in 4 (16 rows), and
    // points snap to all of them. Triad's LFOs keep their own grid (false).
    virtual bool squareGrid() const { return false; }
    // Triad Time's TIME: shaded below the corner-to-corner diagonal (where it reaches back before the cycle began), as
    // ShaperBox's Time view, instead of under the line.
    virtual bool shadeBelowDiagonal() const { return false; }
    virtual bool fillsUnderLine() const { return true; }   // design: false for band-edge LFOs (their backdrop shows the band)
    // Triad Time's TIME: its own preset folders (pitch and chops, TimePresets.h) in the palette, first in the folder menu.
    virtual bool offersTimePresets() const { return false; }
    // The proportions ShaperBox draws its grid with (width / height of the plot, 1 bar across and 1 bar down).
    static constexpr float shaperBoxAspect = 3.48f;
    // A label on a straight line (Triad Time's TIME: what it plays, "FWD -12 st", "REV 0 st", "FREEZE"), and when the
    // labels show: 0 never, 1 on the lines either side of a point being dragged, 2 on every line (that has room).
    virtual juce::String segmentLabel (const triad::mod::Point& from, const triad::mod::Point& to) const { juce::ignoreUnused (from, to); return {}; }
    virtual int segmentLabelMode() const { return 0; }
    // A label over each point (design, band-edge LFOs: the frequency it takes the edge to, "1.23 kHz").
    virtual juce::String pointLabel (const triad::mod::Point&) const { return {}; }
    virtual void setStatus (const juce::String&) {}
    // Behind the grid, drawn every frame (0.15.0): the sound coming in, as a waveform across the cycle or as a spectrum.
    // hasBackdrop: whether there is one now; paintBackdrop draws it in the grid's area (under the grid lines and the line).
    virtual bool hasBackdrop() const { return false; }
    virtual void paintBackdrop (juce::Graphics&, juce::Rectangle<float> plot) { juce::ignoreUnused (plot); }
};

namespace triad::ui
{
// The sound across a cycle (WaveTrace) drawn as a filled waveform, up and down from the middle of `plot`: this cycle in
// `colour`, what is left of the one before fainter. Heights as a clip in the playlist (lifted a little: level^0.6).
// thresholdDb (when not NaN): the trigger's threshold, as two faint lines.
void drawWaveTrace (juce::Graphics&, juce::Rectangle<float> plot, const std::array<float, triad::WaveTrace::bins>& level,
                    const std::array<int, triad::WaveTrace::bins>& age, juce::Colour colour, float thresholdDb);
float waveHeight (float level);   // 0..1 of the half height

// A live spectrum behind a grid (as the BANDS tab's, Pro-Q style): 20 Hz .. 20 kHz across, -84 .. +6 dB up, falling
// 36 dB a second; the range low .. high Hz lit in `colour` (a band's), the rest faint. Reads a SpectrumTap.
class SpectrumBackdrop
{
public:
    SpectrumBackdrop();
    void paint (juce::Graphics&, juce::Rectangle<float> plot, triad::SpectrumTap&, double sampleRate, float lowHz, float highHz, juce::Colour colour);
private:
    static constexpr int fftSize = 4096;
    std::vector<float> samples, window, levels;
    std::vector<std::complex<float>> bins;
    float windowSum = 0.0f;
    uint32_t lastWrite = 0;
    double lastTime = 0.0;
};
}

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
    // Move the wave, or the points selected, one grid step left (-1) or right (+1); fine: an eighth of a step
    // (0.16.1, ShaperBox's move buttons). The whole wave wraps round the cycle; selected points stop at their neighbours.
    void moveWave (int direction, bool fine);
    int numSelected() const;
    void copyShape();
    void pasteShape();
    bool undo();
    bool redo();
    // Grid: note value of one column (4 = 1/4, 8 = 1/8 ...), triplets, snapping.
    int gridDivision = 16;
    bool triplet = false, snap = true;
    int columns() const;
    int rows() const;           // the grid's rows: 8 (Triad's LFOs), 16 (Triad Time, as ShaperBox)
    int snapRows() const;       // the steps points snap to up and down: 16
    juce::Rectangle<float> plot() const;
    float plotAspect() const;                        // the grid's width / height on screen
    juce::Point<float> toScreen (const triad::mod::Point&) const;
    const triad::mod::Shape& getShape() const noexcept { return shape; }
    std::function<void()> onChange;
    int getLayerRenders() const noexcept { return layerRenders; }
    void frame (double time);   // every frame of the screen (up to 120 a second): the playing position
    static triad::mod::Shape clipboard;
private:
    void paintLayer (juce::Graphics&);
    bool drawSegmentLabel (juce::Graphics&, int segment, bool onlyIfRoom) const;
    int hitPoint (juce::Point<float>) const;
    int hitCurve (juce::Point<float>) const;
    juce::Point<float> curveHandle (int segment) const;
    triad::mod::Point snapPoint (triad::mod::Point, bool snapping) const;
    void commit (bool undoable);
    juce::Colour shapeColour() const;
    bool isSquareGrid() const;

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
    double lastFrame = 0.0, lastBackdrop = 0.0;
    bool hadBackdrop = false;
    juce::SharedResourcePointer<triad::ui::FramePacer> pacer;
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

// FAVORITES (0.15.0): the shapes and patterns marked with the heart, in Triad and Triad Time (one list for the computer,
// in TriadSettings::favouriteShapesFile()). A favourite is the shape itself, so it stays if a folder changes later.
namespace triad::favouriteShapes
{
struct Entry { juce::String name; triad::mod::Shape shape; juce::String key; };
std::vector<Entry> load();                                    // newest first
juce::String keyOf (const triad::mod::Shape&);
bool contains (const triad::mod::Shape&);
bool add (const juce::String& name, const triad::mod::Shape&);
bool remove (const triad::mod::Shape&);
bool toggle (const juce::String& name, const triad::mod::Shape&);   // true: now a favourite
juce::int64 version();                                         // changes whenever the list does (any window, any plug-in)
}

// The grid with its toolbar and the preset palette under it.
class ShapePanel final : public juce::Component, private juce::Timer
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
    // The layout (0.16.1). `strip`: Triad's LFO tab, the toolbar and a row of 11 small shapes under the grid. `browser`:
    // Triad Time's approved layout, the toolbar with room for the window's own boxes (MIX AND SMOOTHING) right of SAVE
    // and the move buttons; the presets in a browser of their own (getBrowser(), placed by the window): the folder
    // button, FAVORITES, a search box, the pages (click the page number to type one) and 10 shapes with their names.
    enum class Layout { strip, browser };
    void setLayout (Layout);
    Layout getLayout() const noexcept { return layout; }
    juce::Component& getBrowser();
    static constexpr int browserPageSize = 10, browserToolbarHeight = 52;
    int pageSize() const noexcept { return layout == Layout::browser ? browserPageSize : paletteSize; }
    void setToolbarExtra (juce::Component*, int width);   // the window's own controls in the toolbar, right of SAVE
    void setGridInsetRight (int width);                    // room beside the grid for the window's own panel (LFO: ROUTING)
    // The browser's search: the shapes whose names have every word typed (from every folder and USER); "" = the folder.
    void setSearch (const juce::String&);
    juce::String getSearch() const { return search; }
    void toggleFavouritesFolder();                         // FAVORITES: open it, or back to the folder before
    juce::Button& getMoveLeftButton() noexcept { return moveLeftButton; }
    juce::Button& getMoveRightButton() noexcept { return moveRightButton; }
    bool isPaletteCurrent (int paletteButton) const;       // the shape on the grid (outlined)
    static constexpr int userFolder = 1000;   // the USER folder (saved shapes), after the preset folders
    static constexpr int timeFolder = 2000;   // Triad Time's TIME folders: timeFolder + 0 ... (first in the menu for TIME)
    static constexpr int allFolder = 3000;    // ALL: every preset together (Triad's shapes and the TIME patterns)
    static constexpr int favouritesFolder = 4000;   // FAVORITES: the shapes marked with the heart (newest first)
    // The heart in the corner of palette button i (as if clicked).
    void toggleFavourite (int paletteButton);
    bool isPaletteFavourite (int paletteButton) const;
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
    void mouseUp (const juce::MouseEvent&) override;   // on the page number: jump to a page (or, in ALL, to a folder)
    void showPageMenu();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;              // hearts marked in another window (or plug-in) show here too
    juce::int64 favouritesSeen = 0;
    void showLengthMenu();
    void showGridMenu();
    void stepLength (int direction);
    void showCategoryMenu();
    void rebuildPalette();
    struct PaletteShape { juce::String name; triad::mod::Shape shape; juce::File file; };
    std::vector<PaletteShape> paletteShapes() const;   // the shapes in the open folder
    juce::String paletteFolderName() const;
    bool isTimeFolder (int category) const noexcept { return category >= timeFolder && category < allFolder; }
    bool timeHost() const { return host != nullptr && host->offersTimePresets(); }
    int validCategory (int category) const;

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
    // 0.16.1: the browser layout
    struct Browser;
    friend struct Browser;
    void layoutBrowser();
    void refreshCurrent();
    juce::Component& paletteParent();
    int searchCount() const;
    Layout layout = Layout::strip;
    std::unique_ptr<Browser> browser;
    juce::Component* toolbarExtra = nullptr;
    int toolbarExtraWidth = 0, gridInsetRight = 0;
    juce::String search;
    int folderBeforeFavourites = 0;
    juce::Colour builtColour;
    triad::ui::IconButton moveLeftButton { triad::ui::IconButton::Icon::moveLeft, "Move wave or selection left. Shift-click for fine adjustment." },
                          moveRightButton { triad::ui::IconButton::Icon::moveRight, "Move wave or selection right. Shift-click for fine adjustment." };
    juce::TextButton favouritesButton;
    juce::TextEditor searchBox;
};

// The shape generator: Angular / Soft / Chaos, HARD / MIX / SOFT, and the dice that draws a new shape on the
// target editor's grid (undoable). Under the dice: the green check (you like the shape on the grid) and the red X
// (you do not: it draws a new one), which train the dice on your taste, one taste per kind of shape.
class GeneratorPanel final : public juce::Component
{
public:
    GeneratorPanel();
    static constexpr int preferredHeight = 216;
    // The presets the dice draws from (FROM: chips): Triad's LFOs INFILTRATOR or SHAPERBOX; Triad Time's TIME GROSS BEAT or
    // TIME PRESETS; its VOLUME GROSS BEAT or INFILTRATOR. The first is the default; each dice (taste key) remembers its own.
    void setSources (const std::vector<triad::mod::Source>&);
    void setSource (triad::mod::Source);
    triad::mod::Source getSource() const noexcept { return source; }
    void setTarget (ShapeEditor* e) { target = e; updateRatingButtons(); }
    void generate();
    // Which taste the check and the X train and the dice follows: "lfo" (Triad's LFOs), "time", "volume" (Triad Time).
    void setTasteKey (const juce::String&);
    juce::String getTasteKey() const { return tasteKey; }
    void rate (bool liked);                      // the shape on the grid; a dislike then draws a new one
    triad::mod::Taste getTaste() const;          // as saved
    void forgetTaste();
    juce::Button& getLikeButton() noexcept { return *likeButton; }
    juce::Button& getDislikeButton() noexcept { return *dislikeButton; }
    static juce::File tasteFile (const juce::String& key);   // next to Triad's settings
    void setStyle (float angular, float soft, float chaos);   // 0..1 each
    triad::mod::Style getStyle() const;
    void setSeed (juce::int64 seed) { random.setSeed (seed); }
    juce::Button& getGenerateButton() noexcept { return *generateButton; }
    void mouseDown (const juce::MouseEvent&) override;   // right-click the check or the X: forget the taste
    juce::Slider& getChaosSlider() noexcept { return chaosSlider; }
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void saveStyle();
    void updateRatingButtons();
    void showTasteMenu();
    ShapeEditor* target = nullptr;
    juce::String tasteKey { "lfo" };
    std::unique_ptr<juce::Button> likeButton, dislikeButton;
    juce::Slider angularSlider, softSlider, chaosSlider;
    juce::TextButton hardStyleButton { "HARD" }, mixStyleButton { "MIX" }, softStyleButton { "SOFT" };
    std::vector<triad::mod::Source> sources { triad::mod::Source::infiltrator };
    triad::mod::Source source = triad::mod::Source::infiltrator;
    std::vector<std::unique_ptr<juce::TextButton>> sourceButtons;
    void updateSourceButtons();
    std::unique_ptr<juce::Button> generateButton;
    juce::Random random;
    juce::StringArray recentShapes;
    juce::SharedResourcePointer<TriadSettings> settings;
};
