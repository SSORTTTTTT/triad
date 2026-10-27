#pragma once
#include "../Source/PluginSearch.h"
#include "../Source/PluginEditor.h"
#include <iostream>

// Plug-in search (0.14.0): the magnifying glass in the add / replace effect menu, or typing while it is open (no Cmd+F).
namespace searchTests
{
namespace search = triad::search;

inline EffectEntry entry (const char* name, const char* maker, const char* format, bool verified = true, bool crashed = false)
{
    EffectEntry e;
    e.name = name;
    e.manufacturer = maker;
    e.format = format;
    e.verified = verified;
    e.crashed = crashed;
    return e;
}
inline juce::StringArray namesOf (const std::vector<EffectEntry>& list, const std::vector<int>& found)
{
    juce::StringArray out;
    for (int i : found) out.add (list[(size_t) i].name + " (" + list[(size_t) i].format + ")");
    return out;
}

inline void run (const juce::File& screenshots)
{
    {
        const std::vector<EffectEntry> list {
            entry ("Pro-Q 3", "FabFilter", "VST3"), entry ("Pro-Q 3", "FabFilter", "AU"), entry ("Pro-L 2", "FabFilter", "VST3"),
            entry ("ValhallaVintageVerb", "Valhalla DSP, LLC", "VST3"), entry ("ValhallaRoom", "Valhalla DSP, LLC", "VST3"),
            entry ("Decapitator", "Soundtoys", "AU"), entry ("EchoBoy", "Soundtoys", "AU"), entry ("Crashy Effect", "Nobody", "VST3", false, true),
            entry ("Little AlterBoy", "Soundtoys", "VST3"), entry ("RC-20 Retro Color", "XLN Audio", "VST3", false),
            entry ("Triad Time", "Thomas Ross", "Built in") };
        const auto find = [&list] (const char* query, std::function<bool (const EffectEntry&)> favourite = {})
        {
            return namesOf (list, search::match (list, query, favourite)).joinIntoString (", ");
        };
        std::cout << "Search \"pro\": " << find ("pro") << std::endl;
        require (find ("pro") == "Pro-L 2 (VST3), Pro-Q 3 (VST3), Pro-Q 3 (AU)", "Search: names starting with what you type, A to Z, VST3 before AU");
        require (find ("PRO-Q") == "Pro-Q 3 (VST3), Pro-Q 3 (AU)", "...not case-sensitive");
        require (find ("proq3") == find ("pro q 3") && find ("proq3") == "Pro-Q 3 (VST3), Pro-Q 3 (AU)", "...spaces and dashes don't matter (\"proq3\", \"pro q 3\")");
        require (find ("room") == "ValhallaRoom (VST3)" && find ("valhalla room") == "ValhallaRoom (VST3)", "...a part of the name, and several words");
        require (find ("boy") == "Little AlterBoy (VST3), EchoBoy (AU)" || find ("boy") == "EchoBoy (AU), Little AlterBoy (VST3)", "...the end of a name");
        require (find ("alter") == "Little AlterBoy (VST3)", "...a word inside the name");
        require (find ("soundtoys") == "Decapitator (AU), EchoBoy (AU), Little AlterBoy (VST3)", "...or the maker");
        require (find ("e").startsWith ("EchoBoy (AU)"), "...names starting with it before names containing it");
        require (find ("crashy").isEmpty() && find ("xyz").isEmpty(), "...plug-ins that crashed Triad's scanner are not offered; nothing for no match");
        require (find ("rc-20") == "RC-20 Retro Color (VST3)", "...plug-ins Triad could not check are found too");
        require (search::match (list, "").size() == list.size() - 1, "An empty search lists every plug-in");
        require (find ("pro", [] (const EffectEntry& e) { return e.format == "AU"; }) == "Pro-Q 3 (AU), Pro-L 2 (VST3), Pro-Q 3 (VST3)", "...favourites first among equal matches");
        std::cout << "Plug-in search matching: passed" << std::endl;
        if (screenshots != juce::File())
        {
            // A picture with a fuller list (the test computer has few plug-ins).
            TriadProcessor p;
            p.prepareToPlay (48000.0, 512);
            std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
            editor->setVisible (true);
            auto many = std::make_shared<std::vector<EffectEntry>> (list);
            for (const auto& e : { entry ("Pro-R 2", "FabFilter", "VST3"), entry ("Pro-C 2", "FabFilter", "VST3"), entry ("Pro-MB", "FabFilter", "AU"),
                                   entry ("Saturn 2", "FabFilter", "VST3"), entry ("Timeless 3", "FabFilter", "VST3"), entry ("Portal", "Output", "VST3"),
                                   entry ("ProCrusher", "Wavesfactory", "VST3") })
                many->push_back (e);
            editor->getPluginSearch().open (many, "Band 2  /  new slot", "pro", [] (const EffectEntry& e) { return e.name == "Pro-Q 3" && e.format == "VST3"; },
                                            [] (int) {});
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            themeTests::snapshot (screenshots, "Triad-plugin-search-preview.png", *editor);
            editor->getPluginSearch().close();
            search::MenuItem item ([] (const juce::String&) {});
            item.setLookAndFeel (&editor->getLookAndFeel());
            item.setSize (300, 40);
            themeTests::snapshot (screenshots, "Triad-plugin-search-menu-item.png", item, 2.0f);
            item.setLookAndFeel (nullptr);
        }
    }
    {
        // In Triad's window: the effect menu's search, typing, Return loads into the slot.
        TriadProcessor p;
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        require (editor != nullptr, "Triad's window");
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto& panel = editor->getPluginSearch();
        const int band = 1, slots = p.getSlotCount (band);
        editor->openPluginSearch (band, slots, "t");
        require (panel.isVisible() && panel.getQuery() == "t" && ! panel.getResults().empty(), "Typing in the effect menu opens the search with what was typed");
        panel.getTextBox().insertTextAtCaret ("ime");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        require (panel.getQuery() == "time" && panel.getSelectedResult() == 0, "...it narrows as you type, the best match selected");
        const auto list = editor->effectList();
        require ((*list)[(size_t) panel.getResults().front()].name == "Triad Time", "...Triad Time (built in) found by name");
        themeTests::snapshot (screenshots, "Triad-plugin-search.png", *editor);
        panel.handleKey (juce::KeyPress (juce::KeyPress::downKey));
        require (panel.getSelectedResult() == juce::jmin (1, (int) panel.getResults().size() - 1), "Down moves the selection");
        panel.selectResult (0);
        require (panel.chooseSelected() && ! panel.isVisible(), "Return loads the selected plug-in and closes the search");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        require (p.getSlotCount (band) == slots + 1 && p.getSlotInfo (band, slots).name == "Triad Time", "...into a new slot of the band");
        // Opened again from the menu's magnifying glass (nothing typed yet); Esc closes it.
        editor->openPluginSearch (band, 0);
        const auto offered = std::count_if (list->begin(), list->end(), [] (const EffectEntry& e) { return ! e.crashed; });
        require (panel.isVisible() && panel.getQuery().isEmpty() && (int) panel.getResults().size() == (int) offered, "An empty search lists every plug-in");
        panel.getTextBox().setText ("zzzz-not-a-plugin", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        require (panel.getResults().empty() && ! panel.chooseSelected() && panel.isVisible(), "No match: nothing loads");
        panel.close();
        require (! panel.isVisible(), "Esc / CLOSE closes the search");
        std::cout << "Plug-in search in Triad's window: passed" << std::endl;
    }
}
}
