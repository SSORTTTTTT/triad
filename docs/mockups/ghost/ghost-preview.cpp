// Mock-ups (TRIAD_GHOST_STYLE): an LFO on the band edges shown as a ghost behind dividers that stay where they are set.
#include "Look.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"
#include <iostream>
static void save (juce::Component& c, const juce::File& file, juce::Rectangle<int> area = {})
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (c.createComponentSnapshot (area.isEmpty() ? c.getLocalBounds() : area, true, 2.0f), stream);
    std::cout << "wrote " << file.getFullPathName() << std::endl;
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static double t = 0.0;
static void play (TriadProcessor& p, int blocks)
{
    juce::MidiBuffer midi;
    juce::Random r (3);
    for (int i = 0; i < blocks; ++i)
    {
        juce::AudioBuffer<float> audio (2, 512);
        for (int s = 0; s < 512; ++s, t += 1.0 / 48000.0)
        {
            const double beat = std::fmod (t * 2.0, 1.0);
            float v = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 55.0 * t) + 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                    + 0.05f * (float) (std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) + std::sin (juce::MathConstants<double>::twoPi * 277.2 * t)
                                       + std::sin (juce::MathConstants<double>::twoPi * 329.6 * t) + std::sin (juce::MathConstants<double>::twoPi * 880.0 * t)
                                       + std::sin (juce::MathConstants<double>::twoPi * 1760.0 * t))
                    + (beat < 0.08 ? 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (60.0 + 120.0 * (0.08 - beat)) * t) : 0.0f)
                    + 0.05f * (r.nextFloat() - 0.5f);
            audio.setSample (0, s, v);
            audio.setSample (1, s, v);
        }
        p.processBlock (audio, midi);
    }
}
int main (int argc, char** argv)
{
    // ghost-preview <folder> [frames]: one picture per style, or with frames > 0 that many pictures across one LFO cycle
    // (for a GIF) of the panel alone.
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File folder (argc > 1 ? argv[1] : "/tmp");
    const int frames = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const juce::String style = juce::SystemStats::getEnvironmentVariable ("TRIAD_GHOST_STYLE", "2");
    if (juce::SystemStats::getEnvironmentVariable ("TRIAD_GHOST_OFF", "0") == "1") CrossoverDisplay::ghostMovement = false;
    triad::ui::applyTheme (triad::ui::themeNamed ("Plum"), true);
    TriadProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.addSlot (1);
    using D = TriadProcessor::Destination;
    namespace mod = triad::mod;
    std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
    editor->setVisible (true);
    if (juce::SystemStats::getEnvironmentVariable ("TRIAD_GHOST_OFF", "0") == "1") CrossoverDisplay::ghostMovement = false;
    pump (300);
    // LOCK (TRIAD_SCENE=lock, design): band 3's low cut on an LFO, kept inside the band; the LFO tab, the same after
    // its divider is moved on the BANDS tab, and the band menu.
    if (juce::SystemStats::getEnvironmentVariable ("TRIAD_SCENE", "bands") == "lock")
    {
        const bool locked = triad::ui::lockMock();
        const juce::String tag = locked ? "locked" : "unlocked";
        const int lfo = p.linkEdgesToLfo (2, D::lowEdge);
        p.setLfoShape (2, lfo, locked ? mod::Shape { { 0.0f, 0.08f, 0.0f }, { 0.5f, 0.75f, 2.5f }, { 1.0f, 0.08f, -2.5f } }
                                      : mod::Shape { { 0.0f, 0.70f, 0.0f }, { 0.5f, 0.92f, 2.5f }, { 1.0f, 0.70f, -2.5f } }, false);
        editor->setTab (TriadEditor::Tab::lfo);
        auto& page = editor->getLfoPage();
        page.select (2, lfo);
        pump (300);
        editor->createComponentSnapshot (editor->getLocalBounds());
        play (p, 70);
        const auto area = juce::Rectangle<int> (16, 92, 968, 296);
        const auto shot = [&] (const juce::String& name)
        {
            page.getEditor().frame (2000.0);
            pump (30);
            juce::FileOutputStream stream (folder.getChildFile (name));
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (area, true, 2.0f), stream);
            std::cout << "wrote " << name << std::endl;
        };
        shot ("lock-lfo-" + tag + ".png");
        if (! locked) return 0;
        // Move band 3's low edge (the 2.5 kHz divider) to 4 kHz on the BANDS tab: the locked range follows.
        if (auto* c = p.crossoverParameter (1)) c->setValueNotifyingHost (c->convertTo0to1 (4000.0f));
        play (p, 10);
        page.select (2, 0);   // the grid redrawn with the new range (in the plug-in it follows by itself)
        page.select (2, lfo);
        pump (100);
        play (p, 10);
        shot ("lock-lfo-moved-to-4k.png");
        if (auto* c = p.crossoverParameter (1)) c->setValueNotifyingHost (c->convertTo0to1 (2500.0f));
        editor->setTab (TriadEditor::Tab::bands);
        pump (200);
        triad::ui::menusInsideWindow = true;
        auto& display = editor->getDisplay();
        const auto at = editor->getLocalPoint (&display, juce::Point<int> ((int) display.xForFrequency (6000.0f), (int) display.plotBounds().getY() + 30));
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (at.toFloat());
        display.showBandMenu (2, 6000.0f);
        save (*editor, folder.getChildFile ("lock-band-menu.png"));
        juce::PopupMenu::dismissAllActiveMenus();
        pump (100);
        return 0;
    }
    // The LFO tab (TRIAD_SCENE=lfo-cut: band 2's high cut opening from 500 Hz to 12 kHz and back, frames for a GIF;
    // lfo-widen: band 2 widening and narrowing, one picture).
    const auto scene = juce::SystemStats::getEnvironmentVariable ("TRIAD_SCENE", "bands");
    if (scene.startsWith ("lfo"))
    {
        const bool widen = scene == "lfo-widen";
        const int lfo = p.linkEdgesToLfo (1, widen ? D::openEdges : D::highEdge);
        if (widen) p.setLfoShape (1, lfo, mod::Shape { { 0.0f, 0.15f, 0.0f }, { 0.5f, 0.85f, 3.0f }, { 1.0f, 0.15f, 0.0f } }, false);
        else p.setLfoShape (1, lfo, mod::Shape { { 0.0f, 0.466f, 0.0f }, { 0.5f, 0.926f, 2.5f }, { 1.0f, 0.466f, -2.5f } }, false);
        editor->setTab (TriadEditor::Tab::lfo);
        auto& page = editor->getLfoPage();
        page.select (1, lfo);
        pump (300);
        editor->createComponentSnapshot (editor->getLocalBounds());
        const auto area = juce::Rectangle<int> (16, 92, 968, 296);
        double clock = 1000.0;
        const auto shot = [&] (const juce::File& file, float scale)
        {
            page.getEditor().frame (clock += 0.05);
            pump (20);
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (area, true, scale), stream);
        };
        if (widen || frames <= 0)
        {
            play (p, 70);
            shot (folder.getChildFile (scene + ".png"), 2.0f);
            std::cout << "wrote " << scene << std::endl;
            return 0;
        }
        play (p, 20);
        for (int f = 0; f < frames; ++f)
        {
            play (p, juce::jmax (1, 188 / frames));
            shot (folder.getChildFile ("frame-" + juce::String (f).paddedLeft ('0', 3) + ".png"), 1.0f);
        }
        std::cout << "wrote " << frames << " frames" << std::endl;
        return 0;
    }
    // Band 3 (the top band): its low cut opens down and closes again, a filter sweep on the high end.
    const int cut = p.linkEdgesToLfo (2, D::lowEdge);
    p.setLfoShape (2, cut, mod::Shape { { 0.0f, 0.86f, 0.0f }, { 0.5f, 0.42f, 2.5f }, { 1.0f, 0.86f, -2.5f } }, false);
    editor->setTab (TriadEditor::Tab::bands);
    pump (200);
    auto& display = editor->getDisplay();
    editor->createComponentSnapshot (editor->getLocalBounds());   // the spectrum starts listening
    const auto stripEdges = [&]
    {
        for (int b = 0; b < p.getBandCount(); ++b)
            if (auto* strip = editor->getStrip (b))
                strip->updateEdges (p.effectiveCrossovers(), p.getBandCount(), p.nyquistFrequency());
    };
    if (frames <= 0)
    {
        play (p, 60);
        pump (200);
        display.updateSpectrum (0.05);
        display.refreshIfChanged();
        stripEdges();
        const auto heard = p.heardCrossovers(), set = p.effectiveCrossovers();
        std::cout << "set " << set[1] << "  heard " << heard[1] << std::endl;
        if (juce::SystemStats::getEnvironmentVariable ("TRIAD_GEAR", "0") == "1")
        {
            triad::ui::menusInsideWindow = true;
            // The gear button (its tooltip starts "Settings").
            std::function<juce::Button* (juce::Component&)> findGear = [&] (juce::Component& c) -> juce::Button*
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && b->getTooltip().startsWith ("Settings")) return b;
                    if (auto* deeper = findGear (*child)) return deeper;
                }
                return nullptr;
            };
            if (auto* gear = findGear (*editor))
            {
                const auto at = editor->getLocalPoint (gear, gear->getLocalBounds().getCentre());
                juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (at.toFloat());
                gear->triggerClick();
                pump (50);
            }
            save (*editor, folder.getChildFile ("ghost-gear-menu.png"));
            juce::PopupMenu::dismissAllActiveMenus();
            pump (100);
            triad::ui::menusInsideWindow = false;
            return 0;
        }
        save (*editor, folder.getChildFile ("ghost-style" + style + (CrossoverDisplay::ghostMovement ? "" : "-off") + ".png"));
        return 0;
    }
    // Frames across one cycle (1 bar at 120 BPM: 2 s, 188 blocks of 512).
    const auto area = editor->getLocalArea (&display, display.getLocalBounds());
    play (p, 20);
    for (int f = 0; f < frames; ++f)
    {
        play (p, juce::jmax (1, 188 / frames));
        display.updateSpectrum (0.05);
        display.refreshIfChanged();
        stripEdges();
        pump (20);
        juce::FileOutputStream stream (folder.getChildFile ("frame-" + juce::String (f).paddedLeft ('0', 3) + ".png"));
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (area, true, 1.0f), stream);
    }
    std::cout << "wrote " << frames << " frames" << std::endl;
    return 0;
}
