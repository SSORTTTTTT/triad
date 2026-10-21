#pragma once
#include "../Source/Theme.h"
#include "../Source/TriadTime.h"
#include "../Source/TriadSplit.h"
#include "../Source/ShapePanel.h"
#include <iostream>
#include <set>

// Colour themes (0.12.0): ready-made themes and a custom one picked colour by colour, for Triad, Triad Time and Triad
// Split; the band colours never change and no theme colour comes close to them.
namespace themeTests
{
namespace ui = triad::ui;
using ui::Theme;

inline bool sameColour (juce::Colour a, juce::Colour b) { return a.getARGB() == b.getARGB(); }
inline void snapshot (const juce::File& folder, const juce::String& name, juce::Component& c, float scale = 1.0f)
{
    if (folder == juce::File()) return;
    const auto file = folder.getChildFile (name);
    juce::FileOutputStream stream (file);
    if (! stream.openedOk()) return;
    stream.setPosition (0);
    stream.truncate();
    juce::PNGImageFormat png;
    png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, scale), stream);
    std::cout << "Screenshot: " << file.getFullPathName() << std::endl;
}
template <typename T> T* find (juce::Component& parent)
{
    for (auto* c : parent.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c)) return t;
        if (auto* deeper = find<T> (*c)) return deeper;
    }
    return nullptr;
}

inline void run (const juce::File& screenshots)
{
    juce::SharedResourcePointer<TriadSettings> settings;
    const auto& themes = ui::builtInThemes();
    // ---- The themes themselves ----------------------------------------------------------------------------------------
    require (themes.size() >= 5 && themes.front().name == "Triad", "Ready-made themes, Triad's own first");
    {
        const auto legacy = ui::legacyPalette(), drawn = ui::paletteFor (ui::defaultTheme());
        require (sameColour (legacy.background, drawn.background) && sameColour (legacy.controlsOn, drawn.controlsOn) && sameColour (legacy.marker, drawn.marker),
                 "Triad's own theme is drawn exactly as before");
    }
    float closest = 100.0f, weakestBand = 100.0f;
    for (size_t i = 1; i < themes.size(); ++i)
    {
        const auto& t = themes[i];
        require (ui::readabilityProblems (t).isEmpty(), "Every ready-made theme is readable");
        for (int role : { Theme::gridLines, Theme::text, Theme::accent, Theme::timeLine, Theme::volumeLine })
        {
            float d = 0.0f;
            ui::nearestBand (t[role], &d);
            closest = juce::jmin (closest, d);
        }
        for (int b = 0; b < 10; ++b)
            for (int role : { Theme::panels, Theme::buttons, Theme::grids })
                weakestBand = juce::jmin (weakestBand, ui::contrast (ui::bandColour (b), t[role]));
        require (ui::keepClearOfBands (t).sameColours (t), "A ready-made theme needs no moving away from the band colours");
    }
    std::cout << "Themes: closest theme colour to a band colour " << closest << " (bands from each other: 18.1 or more); "
              << "weakest band on a theme's panels / grids / buttons: contrast " << weakestBand << std::endl;
    require (closest >= ui::bandClearance, "No ready-made theme's accent, lines or text comes close to a band colour");
    require (weakestBand >= 2.2f, "Every band colour shows on every ready-made theme's panels, grids and buttons");
    {
        auto t = themes[2];
        t.name = "Custom";
        const auto back = Theme::fromText (t.toText(), "Custom");
        require (back.sameColours (t), "A theme written as text reads back the same");
    }
    // ---- Keeping clear of the band colours ----------------------------------------------------------------------------------
    {
        float moved = 0.0f, clearance = 0.0f;
        for (int b = 0; b < 10; ++b)
        {
            const auto picked = ui::bandColour (b);
            for (int role : { Theme::accent, Theme::timeLine, Theme::volumeLine, Theme::gridLines, Theme::text })
            {
                const auto kept = ui::keepClearOfBands (role, picked);
                float d = 0.0f;
                ui::nearestBand (kept, &d);
                clearance = b == 0 && role == Theme::accent ? d : juce::jmin (clearance, d);
                moved = juce::jmax (moved, ui::colourDistance (picked, kept));
            }
        }
        std::cout << "Picking a band's own colour for an accent or a line: moved by up to " << moved << ", then at least " << clearance
                  << " from every band" << std::endl;
        require (clearance >= ui::bandClearance, "Picking a band's colour for a theme colour moves it clear of every band");
        require (moved < 45.0f, "...only as far as needed (it stays a similar colour)");
        const auto clear = juce::Colour (0xff7c4dff);
        require (sameColour (ui::keepClearOfBands (Theme::accent, clear), clear), "A colour already clear of the bands is kept as picked");
        const auto amberBackground = ui::keepClearOfBands (Theme::panels, ui::bandColour (3));
        float weakest = 100.0f;
        for (int b = 0; b < 10; ++b) weakest = juce::jmin (weakest, ui::contrast (ui::bandColour (b), amberBackground));
        require (weakest >= 2.2f, "A background picked in a band colour is made dark enough for every band to show on it");
    }
    // ---- Random colourways ---------------------------------------------------------------------------------------------------
    {
        juce::Random random (20261021);
        int count = 0, bad = 0;
        float closestToBand = 100.0f, closestPair = 100.0f;
        std::set<juce::uint32> backgrounds;
        for (int i = 0; i < 300; ++i)
        {
            const auto t = ui::randomTheme (random);
            ++count;
            backgrounds.insert (t[Theme::background].getARGB());
            if (! ui::readabilityProblems (t).isEmpty() || ! ui::keepClearOfBands (t).sameColours (t) || t.name != "Custom") ++bad;
            for (int role : { Theme::gridLines, Theme::text, Theme::accent, Theme::timeLine, Theme::volumeLine })
            {
                float d = 0.0f;
                ui::nearestBand (t[role], &d);
                closestToBand = juce::jmin (closestToBand, d);
            }
            closestPair = std::min ({ closestPair, ui::colourDistance (t[Theme::accent], t[Theme::timeLine]), ui::colourDistance (t[Theme::accent], t[Theme::volumeLine]),
                                        ui::colourDistance (t[Theme::timeLine], t[Theme::volumeLine]) });
        }
        std::cout << "Random colourways: " << count << " made, " << bad << " unreadable or too close to a band, " << backgrounds.size()
                  << " different backgrounds; closest to a band colour " << closestToBand << ", accent and lines at least " << closestPair << " apart" << std::endl;
        require (bad == 0 && closestToBand >= ui::bandClearance, "Every random colourway is readable and clear of the band colours");
        require (closestPair >= ui::bandClearance, "...its accent and two lines tell apart");
        require ((int) backgrounds.size() > 250, "...and they differ from each other");
    }
    // ---- Applying a theme: every window follows, and Triad.settings keeps it --------------------------------------------------
    {
        TriadProcessor p;
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* triadEditor = dynamic_cast<TriadEditor*> (editor.get());
        require (triadEditor != nullptr, "Triad's window");
        TriadTime time;
        std::unique_ptr<juce::AudioProcessorEditor> timeEditor (time.createEditor());
        TriadSplit split;
        std::unique_ptr<juce::AudioProcessorEditor> splitEditor (split.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        const auto& midnight = themes[1];
        ui::applyTheme (midnight, true);
        require (ui::currentTheme().name == "Midnight" && settings->getThemeName() == "Midnight", "Choosing a theme stores it in Triad.settings");
        require (sameColour (ui::background, midnight[Theme::background]) && sameColour (ui::panel, midnight[Theme::panels]),
                 "...and every colour drawn next is the theme's");
        require (sameColour (editor->getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId), midnight[Theme::panels])
                     && sameColour (timeEditor->getLookAndFeel().findColour (juce::TextEditor::textColourId), midnight[Theme::text])
                     && sameColour (splitEditor->getLookAndFeel().findColour (juce::ComboBox::backgroundColourId), midnight[Theme::buttons]),
                 "Open windows of Triad, Triad Time and Triad Split take the new theme at once");
        auto* shapes = find<ShapePanel> (*timeEditor);
        require (shapes != nullptr && sameColour (shapes->getEditor().getHost()->getColour(), midnight[Theme::timeLine]),
                 "Triad Time's TIME line takes the theme's colour");
        snapshot (screenshots, "Theme-Midnight-Triad.png", *editor);
        snapshot (screenshots, "Theme-Midnight-Triad-Time.png", *timeEditor);
        // Another plug-in (Triad Time, Triad Split, another DAW) chose a theme: open windows follow within half a second.
        settings->setTheme ("Plum", {});
        const auto start = juce::Time::getMillisecondCounter();
        while (ui::currentTheme().name != "Plum" && juce::Time::getMillisecondCounter() - start < 3000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        require (ui::currentTheme().name == "Plum" && sameColour (editor->getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId), themes[4][Theme::panels]),
                 "A theme chosen in another plug-in reaches open windows (through Triad.settings)");
        snapshot (screenshots, "Theme-Plum-Triad-Split.png", *splitEditor);
        // Edit colours: pick colours one by one; a band's own colour is moved clear of it.
        auto& panel = triadEditor->getThemePanel();
        triadEditor->openThemePanel();
        require (panel.isVisible() && panel.getEditedTheme().name == "Plum", "Edit colours opens over the window, starting from the current theme");
        panel.setRoleColour (Theme::background, juce::Colour (0xff102a2a));
        require (sameColour (ui::background, juce::Colour (0xff102a2a)) && ui::currentTheme().name == "Custom", "A picked colour shows at once (a Custom theme)");
        panel.setRoleColour (Theme::accent, ui::bandColour (0));
        float d = 0.0f;
        ui::nearestBand (ui::accent, &d);
        require (d >= ui::bandClearance && panel.getNote().contains ("Band 1"), "Band 1's green as the accent: moved clear of it, and the window says so");
        panel.setRoleColour (Theme::timeLine, juce::Colour (0xffff3d7f));
        require (sameColour (ui::currentPalette.timeLine, juce::Colour (0xffff3d7f)) && sameColour (shapes->getEditor().getHost()->getColour(), juce::Colour (0xffff3d7f)),
                 "Triad Time's line can be any colour clear of the bands");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        require (settings->getThemeName() == "Custom" && ui::themeNamed ("Custom").sameColours (panel.getEditedTheme()),
                 "The Custom theme is stored a moment later (the other plug-ins follow it)");
        snapshot (screenshots, "Theme-editor.png", *editor);
        // RANDOM COLOURWAY, then CANCEL: back to the theme there was.
        const auto beforeRandom = ui::currentTheme();
        panel.randomise();
        require (ui::currentTheme().name == "Custom" && ! ui::currentTheme().sameColours (beforeRandom) && panel.getNote().contains ("random"),
                 "RANDOM COLOURWAY makes a new colour scheme, shown at once");
        snapshot (screenshots, "Theme-random-Triad-Time.png", *timeEditor);
        snapshot (screenshots, "Theme-random-editor.png", *editor);
        panel.cancel();
        require (! panel.isVisible() && ui::currentTheme().name == "Plum" && settings->getThemeName() == "Plum",
                 "CANCEL goes back to the theme there was when it opened (Plum)");
        triadEditor->openThemePanel();
        panel.setRoleColour (Theme::background, juce::Colour (0xff102a2a));
        panel.setRoleColour (Theme::accent, ui::bandColour (0));
        panel.setRoleColour (Theme::timeLine, juce::Colour (0xffff3d7f));
        // Save it as a theme of its own.
        const auto name = juce::String ("Triad test theme");
        require (ui::saveTheme (name, panel.getEditedTheme()) && ui::savedThemeNames().contains (name)
                     && ui::themeNamed ("My: " + name).sameColours (panel.getEditedTheme()), "A theme saved by name is listed and reads back the same");
        TriadSettings::themeFolder().getChildFile (name + ".triadtheme").deleteFile();
        panel.close();
        require (! panel.isVisible(), "DONE closes it");
        // Not readable: MAKE READABLE fixes it.
        auto unreadable = ui::defaultTheme();
        unreadable.name = "Custom";
        unreadable[Theme::text] = juce::Colour (0xff2a2c2e);
        require (! ui::readabilityProblems (unreadable).isEmpty() && ui::readabilityProblems (ui::makeReadable (unreadable)).isEmpty(),
                 "Dark text on a dark theme is caught, and MAKE READABLE fixes it");
        // Back to Triad's own.
        ui::applyTheme (ui::defaultTheme(), true);
        require (sameColour (ui::background, ui::legacyPalette().background) && settings->getThemeName() == "Triad", "Back to Triad's own theme");
        snapshot (screenshots, "Theme-Triad-Triad-Time.png", *timeEditor);
    }
}
}
