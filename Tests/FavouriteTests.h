#pragma once
#include "../Source/PluginEditor.h"
#include "../Source/TriadTime.h"
#include "../Source/ShapePanel.h"
#include <iostream>

// Favourites with a heart (0.15.0): shapes and Triad Time's patterns (the FAVORITES folder), and plug-ins in the search.
namespace favouriteTests
{
namespace mod = triad::mod;
namespace fav = triad::favouriteShapes;

inline void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
inline void clickAt (juce::Component& c, float x, float y)   // through Component (where the mouse functions are public)
{
    c.mouseDown (mouseAt (&c, x, y));
    c.mouseUp (mouseAt (&c, x, y));
}

inline void run (const juce::File& screenshots)
{
    TriadSettings::favouriteShapesFile().deleteFile();
    {
        // The list itself: newest first, kept in a file every window and plug-in reads.
        const auto a = mod::preset (3), b = mod::preset (7);
        require (fav::load().empty() && ! fav::contains (a), "No favourites to start with");
        const auto before = fav::version();
        require (fav::toggle ("Shape A", a) && fav::contains (a) && fav::version() != before, "The heart marks a shape as a favourite");
        require (fav::add ("Shape B", b) && fav::load().size() == 2 && fav::load().front().name == "Shape B", "...newest first");
        require (! fav::add ("Again", b) && fav::load().size() == 2, "...a shape is in FAVORITES once");
        require (TriadSettings::favouriteShapesFile().existsAsFile() && TriadSettings::favouriteShapesFile().loadFileAsString().contains ("Shape A"),
                 "...kept in a file (Favorites.triadfavs), for every Triad and Triad Time");
        require (! fav::toggle ("Shape A", a) && ! fav::contains (a) && fav::load().size() == 1, "Clicking the heart again takes it out");
        fav::remove (b);
        require (fav::load().empty(), "...and out of the folder");
    }
    {
        // In Triad's LFO tab: the heart on a palette button, the FAVORITES folder.
        TriadProcessor p;
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        editor->setVisible (true);
        editor->setTab (TriadEditor::Tab::lfo);
        pump (100);
        auto& shapes = editor->getLfoPage().getShapePanel();
        shapes.setPaletteCategory (1);
        require (shapes.getNumPaletteButtons() > 3 && ! shapes.isPaletteFavourite (2), "A folder of shapes, none a favourite yet");
        const auto shapeBefore = editor->getLfoPage().getEditor().getShape();
        shapes.toggleFavourite (2);
        require (shapes.isPaletteFavourite (2) && fav::load().size() == 1, "Clicking a shape's heart fills it (a favourite)");
        require (editor->getLfoPage().getEditor().getShape() == shapeBefore, "...without putting the shape on the grid");
        // A click on the heart itself (lower left corner) does the same; a click elsewhere puts the shape on the grid.
        auto* third = shapes.getPaletteButton (3);
        clickAt (*third, 8.0f, (float) third->getHeight() - 8.0f);
        require (shapes.isPaletteFavourite (3) && fav::load().size() == 2 && editor->getLfoPage().getEditor().getShape() == shapeBefore,
                 "...a click on the heart in the corner marks it (the shape stays)");
        shapes.setPaletteCategory (ShapePanel::favouritesFolder);
        require (shapes.getPaletteCategory() == ShapePanel::favouritesFolder && shapes.getNumPaletteButtons() == 2 && shapes.isPaletteFavourite (0),
                 "FAVORITES: a folder with every favourite");
        themeTests::snapshot (screenshots, "Triad-favorites.png", *editor);
        shapes.getPaletteButton (0)->onClick();
        require (editor->getLfoPage().getEditor().getShape() == fav::load().front().shape, "...a click puts it on the grid");
        shapes.toggleFavourite (0);
        pump (100);
        require (shapes.getNumPaletteButtons() == 1 && fav::load().size() == 1, "...and its heart takes it out of the folder");
        shapes.setPaletteCategory (1);
        pump (50);
        require (shapes.isPaletteFavourite (2) != shapes.isPaletteFavourite (3), "The other folders show which shapes are favourites");
    }
    {
        // Triad Time: hearts on the numbered patterns (the pattern as drawn now), shared with Triad.
        TriadTime t;
        t.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (t.createEditor());
        editor->setVisible (true);
        pump (100);
        juce::Button* pattern = nullptr;
        for (auto* c : editor->getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->getName() == "5") pattern = b;
        require (pattern != nullptr, "Triad Time's pattern 5");
        const auto shape = t.getShape (TriadTime::timeEnvelope, 4);
        const int slotBefore = t.getSlot (TriadTime::timeEnvelope);
        require (! fav::contains (shape), "...not a favourite yet");
        clickAt (*pattern, 9.0f, (float) pattern->getHeight() - 9.0f);
        require (fav::contains (shape) && fav::load().front().name == "TIME pattern 5", "The heart on a pattern puts it in FAVORITES (as it is drawn now)");
        require (t.getSlot (TriadTime::timeEnvelope) == slotBefore, "...the heart does not switch patterns");
        ShapePanel* panel = nullptr;
        for (auto* c : editor->getChildren()) if (auto* s = dynamic_cast<ShapePanel*> (c)) panel = s;
        panel->setPaletteCategory (ShapePanel::favouritesFolder);
        require (panel->getNumPaletteButtons() == 2, "Triad Time's FAVORITES has it, and the shape marked in Triad");
        pump (1200);   // another window (Triad's) sees it within a second
        themeTests::snapshot (screenshots, "Triad-Time-favorites.png", *editor);
        clickAt (*pattern, 9.0f, (float) pattern->getHeight() - 9.0f);
        require (! fav::contains (shape), "...clicking the heart again takes it out");
    }
    TriadSettings::favouriteShapesFile().deleteFile();
    std::cout << "Favorites (shapes and patterns): passed" << std::endl;
    {
        // Plug-ins: the heart on every row of the search, FAVORITES only, the same favourites as the effect menu.
        TriadProcessor p;
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        editor->setVisible (true);
        pump (100);
        auto& search = editor->getPluginSearch();
        editor->openPluginSearch (0, p.getSlotCount (0), "time");
        const auto list = editor->effectList();
        require (! search.getResults().empty() && ! search.isFavouriteRow (0), "The search finds Triad Time, not a favourite yet");
        search.toggleFavourite (0);
        require (search.isFavouriteRow (0) && editor->isFavourite (TriadTime::formatName, "Triad Time"), "Its heart makes it a favourite (as the effect menu's)");
        search.setQuery ("");
        search.setFavouritesOnly (true);
        require (search.getResults().size() == 1 && search.isFavouriteRow (0), "FAVORITES: only the favourites");
        themeTests::snapshot (screenshots, "Triad-plugin-search-favorites.png", *editor);
        search.toggleFavourite (0);
        require (search.getResults().empty() && ! editor->isFavourite (TriadTime::formatName, "Triad Time"), "...the heart again takes it out");
        search.close();
    }
    std::cout << "Favorites (plug-ins): passed" << std::endl;
}
}
