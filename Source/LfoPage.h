#pragma once
#include "Widgets.h"
#include "ShapePanel.h"

// An LFO of one of Triad's bands (or PRE / POST), as the shape panel edits it.
struct LfoShapeHost final : ShapeHost
{
    explicit LfoShapeHost (TriadProcessor& p) : processor (p) {}
    TriadProcessor& processor;
    int band = 0, lfo = 0;
    triad::mod::Shape getShape() const override { return processor.getLfoInfo (band, lfo).shape; }
    void setShape (const triad::mod::Shape& s, bool undoable) override { processor.setLfoShape (band, lfo, s, undoable); }
    void beginEdit() override { processor.beginLfoEdit (band, lfo); }
    bool canUndo() const override { return processor.getLfoInfo (band, lfo).canUndo; }
    bool canRedo() const override { return processor.getLfoInfo (band, lfo).canRedo; }
    bool undo() override { return processor.undoLfo (band, lfo); }
    bool redo() override { return processor.redoLfo (band, lfo); }
    int getLength() const override { return processor.getLfoInfo (band, lfo).length; }
    void setLength (int index) override { processor.setLfoLength (band, lfo, index); }
    float getPhase() const override { return processor.getLfoPhase (band, lfo); }
    juce::Colour getColour() const override { return triad::ui::bandColour (band); }
    void setStatus (const juce::String& text) override { processor.setStatus (text); }
};

// The LFO tab: band chooser, one tab per LFO (labelled with what it moves), the shape panel (grid, toolbar,
// palette), the routing list (where this LFO goes, how much) and the shape generator.
class LfoPage final : public juce::Component
{
public:
    explicit LfoPage (TriadProcessor&);
    ~LfoPage() override;
    void refresh();
    void select (int band, int lfo);
    int getBand() const noexcept { return band; }
    int getLfo() const noexcept { return lfo; }
    ShapeEditor& getEditor() noexcept { return shapes.getEditor(); }
    ShapePanel& getShapePanel() noexcept { return shapes; }
    // The generator: a new shape in the style of the sliders, on the editor's grid (undoable).
    void generate() { generator.generate(); }
    void setGeneratorStyle (float angular, float soft, float chaos) { generator.setStyle (angular, soft, chaos); }   // 0..1 each
    triad::mod::Style getGeneratorStyle() const { return generator.getStyle(); }
    void setGeneratorSeed (juce::int64 seed) { generator.setSeed (seed); }
    juce::Button& getGenerateButton() noexcept { return generator.getGenerateButton(); }
    juce::Slider& getChaosSlider() noexcept { return generator.getChaosSlider(); }
    // The preset palette (see ShapePanel).
    static constexpr int paletteSize = ShapePanel::paletteSize;
    static constexpr int userFolder = ShapePanel::userFolder;
    void setPaletteCategory (int category) { shapes.setPaletteCategory (category); }
    int getPaletteCategory() const noexcept { return shapes.getPaletteCategory(); }
    void setPalettePage (int page) { shapes.setPalettePage (page); }
    int getPalettePage() const noexcept { return shapes.getPalettePage(); }
    int getNumPalettePages() const { return shapes.getNumPalettePages(); }
    // SAVE: keeps this LFO's shape in the USER folder (asks for a name through askName, when set).
    std::function<void (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)>)> askName;
    juce::File saveShapeToUser (const juce::String& name) { return shapes.saveShapeToUser (name); }
    juce::Button& getSaveButton() noexcept { return shapes.getSaveButton(); }
    void showUserShapeMenu (const juce::File& file) { shapes.showUserShapeMenu (file); }
    void startLearn();   // LEARN: the next knob moved in one of the band's effect windows is linked to this LFO
    juce::Button& getAddRouteButton() noexcept { return addRouteButton; }
    int getNumPaletteButtons() const noexcept { return shapes.getNumPaletteButtons(); }
    juce::Button* getPaletteButton (int i) const { return shapes.getPaletteButton (i); }
    void resized() override;
    // The band / PRE / POST buttons above the grid: narrower when there are many.
    int chipWidth() const noexcept { return juce::jlimit (48, 76, (getWidth() - 220) / juce::jmax (1, (int) bandButtons.size())); }
    void paint (juce::Graphics&) override;

private:
    void rebuild();
    void updateToolbar();
    juce::String signature() const;

    TriadProcessor& processor;
    int band = 0, lfo = 0;
    juce::String builtSignature;
    std::vector<std::unique_ptr<juce::TextButton>> bandButtons;
    juce::Viewport tabView;
    juce::Component tabHolder;
    std::vector<std::unique_ptr<juce::Button>> tabs;
    triad::ui::IconButton addLfoButton { triad::ui::IconButton::Icon::plus, "Add an LFO to this band" };
    LfoShapeHost host { processor };
    ShapePanel shapes;
    GeneratorPanel generator;
    std::vector<std::unique_ptr<triad::ui::RouteRow>> routeRows;
    juce::TextButton addRouteButton { "+ ROUTE TO..." }, removeLfoButton { "REMOVE LFO" };
    juce::Rectangle<int> routingArea, generatorArea;
};
