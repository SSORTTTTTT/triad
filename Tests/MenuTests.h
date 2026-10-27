#pragma once
#include "../Source/PluginEditor.h"
#include "../Source/PluginCatalog.h"
#include "../Source/Look.h"
#include "../Source/Theme.h"
#include "../Source/TriadTime.h"
#include <iostream>

// 0.16.0 (Thomas: "change it to that different style of what I really love"): every menu in Triad's own look, the
// FAVORITES by type, knobs drawn as value boxes, and the screenshots of real menus opened in the window.
namespace menuTests
{
namespace ui = triad::ui;

inline void pumpFor (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

inline void run (const juce::File& screenshots)
{
    // ---- What kind of effect a plug-in is (the FAVORITES by type) -------------------------------------------------
    require (triad::effectType ("Fx|Delay", "EchoBoy") == "DELAY", "A VST3 that says Delay is listed under DELAY");
    require (triad::effectType ("Fx", "ValhallaRoom") == "REVERB", "A plug-in named ...Room is a REVERB");
    require (triad::effectType ("Fx|Modulation", "ShaperBox 3") == "RHYTHM + TIME" && triad::effectType ("Effect", "Gross Beat") == "RHYTHM + TIME",
             "ShaperBox and Gross Beat are RHYTHM + TIME, whatever their category says");
    require (triad::effectType ("Fx|Spatial", "MStereoSpread") == "STEREO" && triad::effectType ("Fx", "Pro-Q 3") == "EQ + FILTER"
                 && triad::effectType ("Fx", "Decapitator") == "DISTORTION" && triad::effectType ("Fx", "Frequency Shifter") == "OTHER",
             "Stereo, EQ and distortion plug-ins are found; OTHER when nothing says");
    for (const auto& t : { "DELAY", "REVERB", "RHYTHM + TIME", "STEREO", "OTHER" })
        require (triad::effectTypes().contains (t), "Every type is in the list of types");

    // ---- The settings ------------------------------------------------------------------------------------------------
    juce::SharedResourcePointer<TriadSettings> settings;
    const bool byTypeBefore = settings->getFavouritesByType();
    settings->setFavouritesByType (false);
    require (! settings->getFavouritesByType(), "Favorites by type can be turned off (gear menu)");
    settings->setFavouritesByType (true);
    require (settings->getFavouritesByType(), "...and on again");
    const auto key = TriadSettings::favouriteKey ("VST3", "Triad menu test plug-in");
    settings->setFavouriteType (key, "REVERB");
    require (settings->getFavouriteType (key) == "REVERB", "A favorite moved to another type stays there");
    settings->setFavouriteType (key, {});
    require (settings->getFavouriteType (key).isEmpty(), "...and can go back to the plug-in's own type");
    settings->setFavouritesByType (byTypeBefore);

    // ---- The look of menus and knobs ---------------------------------------------------------------------------------
    {
        ui::TriadLook look;
        require (&juce::LookAndFeel::getDefaultLookAndFeel() == &look,
                 "While a Triad window is open, its look draws the menus and tooltips (no grey system menus)");
        const juce::PopupMenu::Options options;
        int w = 0, h = 0, plainW = 0, plainH = 0;
        // (a long name, so neither is the menus' narrowest width)
        const juce::String name ("A theme with quite a long name for this test");
        look.getIdealPopupMenuItemSizeWithOptions (name + "   " + ui::menuSwatches ({ juce::Colours::red, juce::Colours::green, juce::Colours::blue, juce::Colours::white }),
                                                   false, 0, w, h, options);
        look.getIdealPopupMenuItemSizeWithOptions (name, false, 0, plainW, plainH, options);
        require (h == ui::menuRowHeight && w >= plainW + 4 * 14, "A theme's item makes room for its four colour squares");
        int hw = 0, hh = 0, tw = 0, th = 0;
        look.getIdealPopupMenuSectionHeaderSizeWithOptions ("FAVORITES", 0, hw, hh, options);
        look.getIdealPopupMenuSectionHeaderSizeWithOptions ("Band 2  /  Slot 1", 0, tw, th, options);
        require (hh == ui::menuHeaderHeight && th == ui::menuTitleHeight, "Section titles are small capitals; a name is a larger title");
        require (! look.findColour (juce::PopupMenu::backgroundColourId).isOpaque(), "A menu's window is see-through around its rounded corners");
        const auto m = ui::menuColours();
        require (ui::colourDistance (m.chosen, m.back) > 20.0f, "The chosen item stands out from the menu");
        juce::Image row (juce::Image::ARGB, 300, ui::menuRowHeight, true);
        {
            juce::Graphics g (row);
            juce::PopupMenu::Item item ("Plum");
            item.isTicked = true;
            look.drawPopupMenuItemWithOptions (g, { 0, 0, 300, ui::menuRowHeight }, false, item, options);
        }
        require (ui::colourDistance (row.getPixelAt (200, ui::menuRowHeight / 2), m.chosen) < 3.0f, "The chosen item is filled (and ticked)");
        // A knob is a box with a line meter filled to its value.
        juce::Slider knob (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        knob.setRange (0.0, 100.0);
        knob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffff0000));
        knob.setLookAndFeel (&look);
        knob.setBounds (0, 0, 50, 24);
        knob.setValue (75.0);
        const auto meter = ui::valueBoxMeter (knob.getLocalBounds().toFloat(), false);
        const auto shot = knob.createComponentSnapshot (knob.getLocalBounds());
        const int y = (int) meter.getY() + 1;
        require (shot.getPixelAt ((int) (meter.getX() + meter.getWidth() * 0.5f), y).getRed() > 200
                     && shot.getPixelAt ((int) (meter.getX() + meter.getWidth() * 0.95f), y).getRed() < 150,
                 "A knob is a box whose line meter is filled to its value (75%)");
        knob.setLookAndFeel (nullptr);
    }

    // ---- Real menus, opened inside the window (so the screenshots show them) -----------------------------------------
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        TriadProcessor p;
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        require (editor != nullptr, "Triad's window opens");
        ui::menusInsideWindow = true;
        const auto open = [&] (const char* file, const char* what, const std::function<void()>& show)
        {
            const int before = editor->getNumChildComponents();
            show();
            pumpFor (300);
            require (editor->getNumChildComponents() > before, what);
            themeTests::snapshot (screenshots, file, *editor);
            juce::PopupMenu::dismissAllActiveMenus();
            pumpFor (200);
        };
        open ("Menu-gear.png", "The gear menu opens", [&] { editor->openGearMenu(); });
        const auto timeKey = TriadSettings::favouriteKey (TriadTime::formatName, "Triad Time");
        const bool wasFavourite = settings->isFavourite (timeKey);
        settings->setFavourite (timeKey, true);
        juce::Component& target = editor->getStrip (0) != nullptr ? static_cast<juce::Component&> (*editor->getStrip (0)) : *editor;
        open ("Menu-plugins.png", "The plug-in menu opens, with FAVORITES first", [&] { editor->chooseEffect (0, p.getSlotCount (0), target); });
        settings->setFavourite (timeKey, wasFavourite);
        open ("Menu-band.png", "A band's menu opens", [&] { editor->showBandMenu (0, target); });
        ui::menusInsideWindow = false;
    }
}
}
