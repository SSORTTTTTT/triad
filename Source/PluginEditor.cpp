#include "PluginEditor.h"
namespace
{
const std::array<juce::Colour, 3> colours { juce::Colour (0xff9bd85c), juce::Colour (0xff67c6b8), juce::Colour (0xffa39ce8) };
juce::String hz (float x)
{
    return (x >= 1000.0f ? juce::String (x / 1000.0f, 3) : juce::String (x, 2))
        .trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + (x >= 1000.0f ? " kHz" : " Hz");
}
}
TriadLook::TriadLook()
{
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff303331));
    setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe7eae5));
    setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe7eae5));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::trackColourId, juce::Colour (0xff9bd85c));
    setColour (juce::Slider::thumbColourId, juce::Colour (0xff9bd85c));
    setColour (juce::Label::textColourId, juce::Colour (0xffa6afa5));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff202420));
}
void TriadLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (7);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
    value.addCentredArc (centre.x, centre.y, radius, radius, 0, start, start + pos * (end - start), true);
    g.setColour (juce::Colour (0xff3b413a)); g.strokePath (track, juce::PathStrokeType (4.0f));
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId)); g.strokePath (value, juce::PathStrokeType (4.0f));
    const auto angle = start + pos * (end - start) - juce::MathConstants<float>::halfPi;
    g.drawLine (centre.x + std::cos (angle) * radius * 0.4f, centre.y + std::sin (angle) * radius * 0.4f,
                centre.x + std::cos (angle) * radius * 0.8f, centre.y + std::sin (angle) * radius * 0.8f, 3.0f);
}
TriadEditor::TriadEditor (TriadProcessor& p) : AudioProcessorEditor (p), processor (p), frequencyDisplay (p)
{
    setLookAndFeel (&look);
    setSize (980, 710);
    addAndMakeVisible (frequencyDisplay);
    for (int i = 0; i < 3; ++i)
    {
        auto& c = cards[(size_t) i];
        for (auto* button : { &c.load, &c.open, &c.remove }) addAndMakeVisible (*button);
        addAndMakeVisible (c.plugin); addAndMakeVisible (c.detail); addAndMakeVisible (c.mix);
        c.plugin.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
        c.plugin.setColour (juce::Label::textColourId, juce::Colour (0xffeef0f5));
        c.detail.setFont (juce::Font (juce::FontOptions (12.0f)));
        c.mix.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        c.mix.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 75, 22);
        c.mix.setTextValueSuffix ("%"); c.mix.setDoubleClickReturnValue (true, 100.0);
        c.mix.setColour (juce::Slider::rotarySliderFillColourId, colours[(size_t) i]);
        c.mix.setTooltip ("Effect mix: 0% is the unprocessed band; 100% is the hosted effect.");
        c.mix.setTitle (juce::StringArray { "Low", "Mid", "High" }[i] + " effect mix");
        c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "mix" + juce::String (i), c.mix);
        c.load.onClick = [this, i] { choose (i); };
        c.open.onClick = [this, i] { processor.showEffect (i); };
        c.remove.onClick = [this, i] { processor.removeEffect (i); timerCallback(); };
    }
    for (auto* s : { &low, &high })
    {
        addAndMakeVisible (*s); s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 94, 24);
        s->setTextValueSuffix (" Hz");
    }
    low.setTitle ("Low / Mid crossover"); high.setTitle ("Mid / High crossover");
    low.onValueChange = [this]
    {
        if (low.isMouseButtonDown() && low.getValue() >= processor.effectiveCutoffs().second / 1.01)
            low.setValue (processor.effectiveCutoffs().second / 1.01);
    };
    high.onValueChange = [this]
    {
        if (high.isMouseButtonDown() && high.getValue() <= low.getValue() * 1.01)
            high.setValue (low.getValue() * 1.01);
    };
    lowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "lowCut", low);
    highAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters, "highCut", high);
    addAndMakeVisible (lowLabel); addAndMakeVisible (highLabel); addAndMakeVisible (status);
    lowLabel.setText ("LOW / MID", juce::dontSendNotification);
    highLabel.setText ("MID / HIGH", juce::dontSendNotification);
    status.setFont (juce::Font (juce::FontOptions (12.0f)));
    timerCallback(); startTimerHz (10);
}
TriadEditor::~TriadEditor() { stopTimer(); processor.closeWindows(); setLookAndFeel (nullptr); }
juce::Rectangle<int> TriadEditor::cardBounds (int i) const { return { 28 + i * 312, 366, 296, 284 }; }
void TriadEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff252729));
    g.setColour (juce::Colour (0xff303235)); g.fillRect (0, 0, getWidth(), 66);
    g.setColour (juce::Colour (0xff9bd85c)); g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("TRIAD", 28, 13, 150, 42, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffaeb6ab)); g.setFont (juce::FontOptions (12.0f));
    g.drawText ("MULTIBAND EFFECT HOST", 176, 20, 360, 30, juce::Justification::centredLeft);
    g.drawText ("VST3  /  0.1.1", 750, 20, 200, 30, juce::Justification::centredRight);
    const auto cuts = processor.effectiveCutoffs();
    const std::array<float, 4> endpoints { 0.0f, cuts.first, cuts.second, processor.nyquistFrequency() };
    g.setColour (juce::Colour (0xffa4afa2)); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("YOUR EFFECTS", 28, 342, 260, 20, juce::Justification::centredLeft);
    for (int i = 0; i < 3; ++i)
    {
        auto b = cardBounds (i);
        g.setColour (juce::Colour (0xff1b1e1c)); g.fillRoundedRectangle (b.toFloat(), 7);
        g.setColour (colours[(size_t) i].withAlpha (0.75f));
        g.fillRoundedRectangle ((float) b.getX() + 16, (float) b.getY() + 17, 4, 19, 2);
        g.setColour (colours[(size_t) i]); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::StringArray { "LOW", "MID", "HIGH" }[i], b.getX() + 29, b.getY() + 15, 180, 24, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.0f)); g.setColour (juce::Colour (0xff778275));
        g.drawText ("0" + juce::String (i + 1), b.getRight() - 53, b.getY() + 15, 35, 24, juce::Justification::centredRight);
        g.setColour (juce::Colour (0xffbac4b8)); g.setFont (juce::FontOptions (13.0f));
        g.drawText (hz (endpoints[(size_t) i]), b.getX() + 18, b.getY() + 45, 123, 23, juce::Justification::centredLeft);
        g.drawText (hz (endpoints[(size_t) i + 1]), b.getX() + 155, b.getY() + 45, 123, 23, juce::Justification::centredRight);
        g.setColour (colours[(size_t) i].withAlpha (0.45f));
        g.drawHorizontalLine (b.getY() + 73, (float) b.getX() + 18, (float) b.getRight() - 18);
        g.setColour (juce::Colour (0xffa5afa2)); g.setFont (juce::FontOptions (12.0f));
        g.drawText ("EFFECT MIX", b.getX() + 18, b.getY() + 205, 136, 24, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Dry 0%  /  Wet 100%", b.getX() + 18, b.getY() + 230, 145, 20, juce::Justification::centredLeft);
    }
}
void TriadEditor::resized()
{
    frequencyDisplay.setBounds (28, 86, 924, 210);
    lowLabel.setBounds (28, 306, 86, 28); low.setBounds (116, 304, 346, 32);
    highLabel.setBounds (515, 306, 86, 28); high.setBounds (603, 304, 349, 32);
    for (int i = 0; i < 3; ++i)
    {
        auto b = cardBounds (i); auto& c = cards[(size_t) i];
        c.plugin.setBounds (b.getX() + 14, b.getY() + 86, 268, 30);
        c.detail.setBounds (b.getX() + 14, b.getY() + 117, 268, 24);
        c.load.setBounds (b.getX() + 18, b.getY() + 156, 125, 30);
        c.open.setBounds (b.getX() + 150, b.getY() + 156, 55, 30);
        c.remove.setBounds (b.getX() + 212, b.getY() + 156, 66, 30);
        c.mix.setBounds (b.getX() + 187, b.getY() + 193, 88, 87);
    }
    status.setBounds (28, 665, 924, 32);
}
void TriadEditor::timerCallback()
{
    juce::String warning;
    for (int i = 0; i < 3; ++i)
    {
        auto info = processor.getSlotInfo (i); auto& c = cards[(size_t) i];
        c.plugin.setText (info.name, juce::dontSendNotification);
        c.detail.setText (info.status.isNotEmpty() ? "Unavailable - see status" : info.loaded ? "VST3 / Stereo" : "Empty slot passes audio through", juce::dontSendNotification);
        c.open.setEnabled (info.loaded); c.remove.setEnabled (info.loaded || info.status.isNotEmpty());
        if (info.status.isNotEmpty()) warning = info.status;
    }
    auto text = warning.isNotEmpty() ? warning : processor.getStatus();
    if (text.isEmpty()) text = "Load a VST3 effect on any band. Open its interface to adjust the effect.";
    status.setText (text, juce::dontSendNotification);
    frequencyDisplay.refresh(); repaint();
}
void TriadEditor::choose (int band)
{
    juce::StringArray paths;
   #if JUCE_MAC
    paths.add ("/Library/Audio/Plug-Ins/VST3");
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3").getFullPathName());
   #elif JUCE_WINDOWS
    paths.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files/VST3").getFullPathName());
   #else
    paths.add ("/usr/lib/vst3"); paths.add ("/usr/local/lib/vst3");
    paths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3").getFullPathName());
   #endif
    auto files = std::make_shared<juce::Array<juce::File>>();
    for (const auto& path : paths)
        juce::File (path).findChildFiles (*files, juce::File::findFilesAndDirectories, true, "*.vst3");
    juce::PopupMenu menu;
    menu.addSectionHeader ("Installed VST3 effects");
    int id = 1;
    for (const auto& f : *files) menu.addItem (id++, f.getFileNameWithoutExtension());
    if (files->isEmpty()) menu.addItem (0, "No plugins found in the standard folders", false);
    menu.addSeparator(); menu.addItem (1000000, "Browse for a VST3...");
    juce::Component::SafePointer<TriadEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cards[(size_t) band].load), [safe, files, band] (int selected)
    {
        if (! safe) return;
        if (selected == 1000000) { safe->browse (band); return; }
        if (selected > 0 && selected <= files->size())
        {
            safe->processor.loadFile (band, files->getReference (selected - 1)); safe->timerCallback();
        }
    });
}
void TriadEditor::browse (int band)
{
    chooser = std::make_unique<juce::FileChooser> ("Select a VST3 audio effect", juce::File(), "*.vst3");
    juce::Component::SafePointer<TriadEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
        [safe, band] (const juce::FileChooser& c)
        {
            if (! safe || c.getResult() == juce::File()) return;
            safe->processor.loadFile (band, c.getResult()); safe->timerCallback();
        });
}
