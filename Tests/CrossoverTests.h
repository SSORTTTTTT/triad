#pragma once
#include "../Source/CrossoverDisplay.h"

struct GestureProbe final : juce::AudioProcessorParameter::Listener
{
    int starts = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { starting ? ++starts : ++ends; }
};

inline juce::MouseEvent mouseAt (juce::Component* target, float x, float y, bool dragging = false)
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, y },
        juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
        target, target, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), 1, dragging);
}

inline void requireIncreasing (const std::vector<float>& cuts, const char* label)
{
    for (size_t i = 1; i < cuts.size(); ++i)
        require (cuts[i] / cuts[i - 1] >= 1.0099f, label);
}

inline void runCrossoverTests()
{
    TriadProcessor processor;
    processor.prepareToPlay (48000, 128);
    require (processor.getBandCount() == 3, "Triad opens with three bands");
    auto* lo = processor.crossoverParameter (0);
    auto* hi = processor.crossoverParameter (1);
    GestureProbe lowProbe, highProbe;
    lo->addListener (&lowProbe); hi->addListener (&highProbe);
    {
        CrossoverDisplay display (processor);
        display.setBounds (0, 0, 952, 204);
        display.refresh();
        require (display.getNumHandles() == 2, "Three bands have two crossover handles");
        auto* lowHandle = display.getHandle (0);
        auto* highHandle = display.getHandle (1);
        for (float f : { 20.0f, 31.5f, 250.0f, 2500.0f, 20000.0f, 24000.0f })
            require (std::abs (display.frequencyForX (display.xForFrequency (f)) - f) < f * 0.000002f,
                     "Log frequency mapping round trip");
        require (display.frequencyForX (-10000) == 20.0f, "Left drag clamps to display edge");
        require (std::abs (display.frequencyForX (10000) - 24000.0f) < 0.01f, "Right drag clamps to display edge");
        // Grab the edge of the hit area and hold still: the cutoff must not jump.
        const auto before = processor.effectiveCutoffs().first;
        // (Low on the divider: higher up is the slope dot.)
        lowHandle->mouseDown (mouseAt (lowHandle, 5.0f, 120.0f));
        lowHandle->mouseDrag (mouseAt (lowHandle, 5.0f, 120.0f, true));
        lowHandle->mouseUp (mouseAt (lowHandle, 5.0f, 120.0f));
        require (std::abs (processor.effectiveCutoffs().first - before) < 0.02f, "Handle grab does not jump");
        require (lowProbe.starts == 1 && lowProbe.ends == 1, "Mouse gesture begin/end sent to host");
        lowHandle->mouseDown (mouseAt (lowHandle, display.xForFrequency (before) - (float) lowHandle->getX(), 120.0f));
        lowHandle->mouseDrag (mouseAt (lowHandle, display.xForFrequency (420.0f) - (float) lowHandle->getX(), 120.0f, true));
        lowHandle->mouseUp (mouseAt (lowHandle, 12.0f, 120.0f));
        require (std::abs (processor.effectiveCutoffs().first - 420.0f) < 0.03f, "Mouse drag moves the first crossover");
        require (lowProbe.changes > 0, "Dragging notifies parameter listeners");
        display.beginCrossoverGesture (0); display.dragCrossoverTo (10000); display.endCrossoverGesture();
        auto cuts = processor.effectiveCutoffs();
        require (cuts.first < cuts.second && cuts.second / cuts.first >= 1.0099f, "Lower handle cannot cross the next one");
        display.beginCrossoverGesture (1); display.dragCrossoverTo (-10000); display.endCrossoverGesture();
        cuts = processor.effectiveCutoffs();
        require (cuts.second / cuts.first >= 1.0099f, "Upper handle cannot cross the previous one");
        display.refresh();
        const auto lowHitX = juce::roundToInt (display.xForFrequency (cuts.first)) - 4;
        const auto highHitX = juce::roundToInt (display.xForFrequency (cuts.second)) + 4;
        require (lowHandle->hitTest (lowHitX - lowHandle->getX(), 50)
                 && ! highHandle->hitTest (lowHitX - highHandle->getX(), 50), "Narrow-band lower handle remains reachable");
        require (highHandle->hitTest (highHitX - highHandle->getX(), 50)
                 && ! lowHandle->hitTest (highHitX - lowHandle->getX(), 50), "Narrow-band upper handle remains reachable");
        display.setCrossoverValue (0, 250); display.setCrossoverValue (1, 2500);
        const juce::KeyPress right (juce::KeyPress::rightKey), fineLeft (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0);
        require (lowHandle->keyPressed (right), "Keyboard arrow handled");
        const float afterRight = processor.effectiveCutoffs().first;
        require (afterRight > 250, "Keyboard increases crossover");
        lowHandle->keyPressed (fineLeft);
        require (processor.effectiveCutoffs().first > 250 && processor.effectiveCutoffs().first < afterRight, "Shift provides finer keyboard step");
        lowHandle->mouseDoubleClick (mouseAt (lowHandle, 12, 120));
        require (std::abs (processor.effectiveCutoffs().first - 250) < 0.03f, "Double-click resets crossover");
        hi->setValueNotifyingHost (hi->convertTo0to1 (5000));
        display.refresh();
        require (std::abs ((float) highHandle->getBounds().getCentreX() - display.xForFrequency (5000)) < 1.0f,
                 "External automation updates handle position");
        juce::MemoryBlock state; processor.getStateInformation (state);
        TriadProcessor restored; restored.setStateInformation (state.getData(), (int) state.getSize());
        require (std::abs (restored.effectiveCutoffs().second - 5000) < 0.1f, "New crossover values recall with project");
        processor.prepareToPlay (8000, 128);
        display.beginCrossoverGesture (1); display.dragCrossoverTo (10000); display.endCrossoverGesture();
        require (processor.effectiveCutoffs().second <= 3600.01f, "Low sample-rate Nyquist guard");
        require (lowProbe.starts == lowProbe.ends && highProbe.starts == highProbe.ends, "Completed gestures remain balanced");

        // ---- Slope dot: up is soft, down is hard; readout; double-click resets; arrow keys ----------
        {
            processor.prepareToPlay (48000, 128);
            display.refresh();
            auto* slope = processor.slopeParameter (0);
            GestureProbe slopeProbe;
            slope->addListener (&slopeProbe);
            const float frequency = processor.effectiveCutoffs().first;
            const float step = CrossoverDisplay::slopeDragStep;
            const float dotY = display.slopeY (processor.getCrossoverSlope (0)) - (float) lowHandle->getY();
            lowHandle->mouseDown (mouseAt (lowHandle, 12.0f, dotY));
            lowHandle->mouseDrag (mouseAt (lowHandle, 12.0f, dotY + 2.0f * step, true));
            lowHandle->mouseUp (mouseAt (lowHandle, 12.0f, dotY + 2.0f * step));
            require (processor.getCrossoverSlope (0) == 6, "Dragging the slope dot down makes the slope harder");
            require (slopeProbe.starts == 1 && slopeProbe.ends == 1, "A slope drag is one gesture for the host");
            require (std::abs (processor.effectiveCutoffs().first - frequency) < 0.05f, "Dragging the slope dot leaves the frequency alone");
            const float dotY2 = display.slopeY (6) - (float) lowHandle->getY();
            lowHandle->mouseDown (mouseAt (lowHandle, 12.0f, dotY2));
            lowHandle->mouseDrag (mouseAt (lowHandle, 12.0f, dotY2 - 40.0f * step, true));
            lowHandle->mouseUp (mouseAt (lowHandle, 12.0f, dotY2 - 40.0f * step));
            require (processor.getCrossoverSlope (0) == 1, "Dragging it up makes it soft, down to 6 dB/oct");
            require (slope->getCurrentValueAsText() == "6 dB/oct", "Slope readout text");
            require (std::abs (display.slopeReadoutBounds (0).getCentreY() - display.slopeY (1)) < 0.5f, "The readout sits next to the dot");
            lowHandle->mouseDoubleClick (mouseAt (lowHandle, 12.0f, display.slopeY (1) - (float) lowHandle->getY()));
            require (processor.getCrossoverSlope (0) == 4, "Double-clicking the slope dot resets it to 24 dB/oct");
            require (lowHandle->keyPressed (juce::KeyPress (juce::KeyPress::downKey)) && processor.getCrossoverSlope (0) == 5, "Down arrow: harder");
            lowHandle->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
            require (processor.getCrossoverSlope (0) == 4, "Up arrow: softer");
            // All the way down: the near-brickwall 96 dB/oct.
            const float dotY3 = display.slopeY (4) - (float) lowHandle->getY();
            lowHandle->mouseDown (mouseAt (lowHandle, 12.0f, dotY3));
            lowHandle->mouseDrag (mouseAt (lowHandle, 12.0f, dotY3 + 60.0f * step, true));
            lowHandle->mouseUp (mouseAt (lowHandle, 12.0f, dotY3 + 60.0f * step));
            require (processor.getCrossoverSlope (0) == triad::CrossoverFilter::maxOrder && slope->getCurrentValueAsText() == "96 dB/oct",
                     "Dragging the slope dot to the bottom gives the hardest cut, 96 dB/oct");
            {
                juce::PopupMenu menu;
                CrossoverDisplay::addSlopeItems (menu, 4, 0);
                int items = 0, ticked = 0;
                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    if (! it.getItem().isSectionHeader) { ++items; ticked += it.getItem().isTicked ? it.getItem().itemID : 0; }
                require (items == 16 && ticked == 4, "The slope list has every step from 6 to 96 dB/oct, the current one ticked");
            }
            lowHandle->mouseDoubleClick (mouseAt (lowHandle, 12.0f, display.slopeY (triad::CrossoverFilter::maxOrder) - (float) lowHandle->getY()));
            require (processor.getCrossoverSlope (0) == 4, "Double-click resets the hardest slope to 24 dB/oct");
            require (slopeProbe.starts == slopeProbe.ends, "Slope gestures stay balanced");
            slope->removeListener (&slopeProbe);
        }

        // ---- Adding bands while working --------------------------------------------------------
        processor.prepareToPlay (48000, 128);
        display.setCrossoverValue (1, 2500); display.setCrossoverValue (0, 250);
        display.refresh();
        const auto plot = display.plotBounds();
        display.mouseDoubleClick (mouseAt (&display, display.xForFrequency (1000.0f), plot.getCentreY()));
        require (processor.getBandCount() == 4, "Double-clicking the display adds a band");
        display.refresh();
        require (display.getNumHandles() == 3, "A handle appears for the new crossover");
        auto split = processor.effectiveCrossovers();
        require (std::abs (split[0] - 250.0f) < 0.1f && std::abs (split[1] - 1000.0f) < 15.0f && std::abs (split[2] - 2500.0f) < 0.1f,
                 "New crossover lands where you double-clicked");
        display.getAddButton().onClick();
        require (processor.getBandCount() == 5, "+ BAND adds a band");
        while (processor.addBand()) {}
        display.refresh();
        require (processor.getBandCount() == triad::maxBands && display.getNumHandles() == triad::maxBands - 1, "Up to ten bands");
        split = processor.effectiveCrossovers();
        requireIncreasing (split, "Ten-band crossovers stay ordered");
        for (int j = 0; j < display.getNumHandles(); ++j)
        {
            auto* h = display.getHandle (j);
            const auto x = juce::roundToInt (display.xForFrequency (split[(size_t) j]));
            require (h->hitTest (x - h->getX(), 50), "Every crossover handle is reachable");
            for (int k = 0; k < display.getNumHandles(); ++k)
                if (k != j) require (! display.getHandle (k)->hitTest (x - display.getHandle (k)->getX(), 50), "Handles do not steal each other's dividers");
        }
        require (! processor.addBand(), "An eleventh band is refused");
        while (processor.getBandCount() > 3) processor.removeBand (processor.getBandCount() - 1);
        display.refresh();
        require (display.getNumHandles() == 2, "Removing bands removes handles");
        display.beginCrossoverGesture (0);
    }
    require (lowProbe.starts == lowProbe.ends, "Closing editor finishes an active gesture");
    lo->removeListener (&lowProbe); hi->removeListener (&highProbe);
    std::cout << "PASS: crossover dragging, frequency mapping, band order, automation gestures, keyboard, reset, recall, Nyquist guard, add/remove bands up to 10" << std::endl;
}
