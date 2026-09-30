#pragma once
#include "../Source/CrossoverDisplay.h"

struct GestureProbe final : juce::AudioProcessorParameter::Listener
{
    int starts = 0, ends = 0, changes = 0;
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool starting) override { starting ? ++starts : ++ends; }
};

inline void runCrossoverTests()
{
    TriadProcessor processor;
    processor.prepareToPlay (48000, 128);
    auto* lo = processor.parameters.getParameter ("lowCut");
    auto* hi = processor.parameters.getParameter ("highCut");
    GestureProbe lowProbe, highProbe;
    lo->addListener (&lowProbe); hi->addListener (&highProbe);
    {
        CrossoverDisplay display (processor);
        display.setBounds (0, 0, 924, 210);
        auto* lowHandle = display.getChildComponent (0);
        auto* highHandle = display.getChildComponent (1);
        require (lowHandle && highHandle, "Both crossover handles exist");
        for (float f : { 20.0f, 31.5f, 250.0f, 2500.0f, 20000.0f, 24000.0f })
            require (std::abs (display.frequencyForX (display.xForFrequency (f)) - f) < f * 0.000002f,
                     "Log frequency mapping round trip");
        require (display.frequencyForX (-10000) == 20.0f, "Left drag clamps to display edge");
        require (std::abs (display.frequencyForX (10000) - 24000.0f) < 0.01f, "Right drag clamps to display edge");
        auto event = [] (juce::Component* handle, float x, bool dragging = false)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 50.0f },
                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                handle, handle, juce::Time::getCurrentTime(), { x, 50.0f }, juce::Time::getCurrentTime(), 1, dragging);
        };
        // Grab the edge of the hit area and hold still: the cutoff must not jump.
        const auto before = processor.effectiveCutoffs().first;
        lowHandle->mouseDown (event (lowHandle, 5.0f));
        lowHandle->mouseDrag (event (lowHandle, 5.0f, true));
        lowHandle->mouseUp (event (lowHandle, 5.0f));
        require (std::abs (processor.effectiveCutoffs().first - before) < 0.02f, "Handle grab does not jump");
        require (lowProbe.starts == 1 && lowProbe.ends == 1, "Mouse gesture begin/end sent to host");
        // Exercise actual mouse callbacks, not just the frequency setter.
        lowHandle->mouseDown (event (lowHandle, display.xForFrequency (before) - (float) lowHandle->getX()));
        lowHandle->mouseDrag (event (lowHandle, display.xForFrequency (420.0f) - (float) lowHandle->getX(), true));
        lowHandle->mouseUp (event (lowHandle, 12.0f));
        require (std::abs (processor.effectiveCutoffs().first - 420.0f) < 0.03f, "Mouse drag moves low crossover");
        require (lowProbe.changes > 0, "Dragging notifies parameter listeners");
        display.beginCrossoverGesture (0); display.dragCrossoverTo (10000); display.endCrossoverGesture();
        auto cuts = processor.effectiveCutoffs();
        require (cuts.first < cuts.second && cuts.second / cuts.first >= 1.0099f, "Low handle cannot cross high handle");
        display.beginCrossoverGesture (1); display.dragCrossoverTo (-10000); display.endCrossoverGesture();
        cuts = processor.effectiveCutoffs();
        require (cuts.second / cuts.first >= 1.0099f, "High handle cannot cross low handle");
        const auto lowHitX = juce::roundToInt (display.xForFrequency (cuts.first)) - 4;
        const auto highHitX = juce::roundToInt (display.xForFrequency (cuts.second)) + 4;
        require (lowHandle->hitTest (lowHitX - lowHandle->getX(), 50)
                 && ! highHandle->hitTest (lowHitX - highHandle->getX(), 50), "Narrow-band low handle remains reachable");
        require (highHandle->hitTest (highHitX - highHandle->getX(), 50)
                 && ! lowHandle->hitTest (highHitX - lowHandle->getX(), 50), "Narrow-band high handle remains reachable");
        display.setCrossoverValue (0, 250); display.setCrossoverValue (1, 2500);
        const juce::KeyPress right (juce::KeyPress::rightKey), fineLeft (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0);
        require (lowHandle->keyPressed (right), "Keyboard arrow handled");
        const float afterRight = processor.effectiveCutoffs().first;
        require (afterRight > 250, "Keyboard increases crossover");
        lowHandle->keyPressed (fineLeft);
        require (processor.effectiveCutoffs().first > 250 && processor.effectiveCutoffs().first < afterRight, "Shift provides finer keyboard step");
        lowHandle->mouseDoubleClick (event (lowHandle, 12));
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
        display.beginCrossoverGesture (0);
    }
    require (lowProbe.starts == lowProbe.ends, "Closing editor finishes an active gesture");
    lo->removeListener (&lowProbe); hi->removeListener (&highProbe);
    std::cout << "PASS: crossover dragging, frequency mapping, band order, automation gestures, keyboard, reset, recall, and Nyquist guard" << std::endl;
}
