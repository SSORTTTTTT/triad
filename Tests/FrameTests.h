#pragma once
#include "../Source/UiCommon.h"
#include "../Source/Modulation.h"
#include "../Source/TriadTime.h"
#include <iostream>

// Smooth motion: playing positions carried on between audio blocks, and the frame rate (120 a second while the
// computer keeps up, else 60).
namespace frameTests
{
inline void run()
{
    // ---- A playing position moves on between the audio's updates ---------------------------------------------------
    {
        triad::PhaseClock clock;
        require (clock.now() < 0.0f, "Not playing yet: no position");
        // (Timed from both sides, so a test computer that pauses this thread for a moment cannot fail it.)
        const auto before = juce::Time::getMillisecondCounterHiRes();
        clock.set (0.25f, 2.0);   // two cycles a second
        juce::Thread::sleep (30);
        const auto shortest = (juce::Time::getMillisecondCounterHiRes() - before) * 0.001;
        const float position = clock.now();
        const auto longest = (juce::Time::getMillisecondCounterHiRes() - before) * 0.001;
        const float low = 0.25f + 2.0f * (float) juce::jmin (0.1, shortest) - 0.01f, high = 0.25f + 2.0f * (float) juce::jmin (0.1, longest) + 0.01f;
        require (position > 0.3f && position >= low && position <= high, "Between audio blocks the position carries on at its speed");
        require (std::abs (clock.last() - 0.25f) < 1.0e-6f, "...from where the audio left it");
        juce::Thread::sleep (250);
        require (std::abs (clock.now() - 0.45f) < 0.01f, "...for at most 0.1 s (a DAW that stopped sending audio leaves it standing)");
        clock.set (0.99f, 1.0);
        juce::Thread::sleep (30);
        require (clock.now() < 0.2f, "...and wraps round at the end of the cycle");
        clock.set (-1.0f, 1.0);
        require (clock.now() < 0.0f, "Stopped: no position");
    }
    // ---- Triad Time's playheads, between blocks --------------------------------------------------------------------------
    {
        TriadTime t;
        t.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> audio (2, 512);
        audio.clear();
        juce::MidiBuffer midi;
        t.processBlock (audio, midi);   // no DAW transport: it runs at 120 bpm
        const float at = t.getPhase (TriadTime::volumeEnvelope);
        juce::Thread::sleep (20);
        const float now = t.getPhaseNow (TriadTime::volumeEnvelope);
        float moved = now - at;
        if (moved < -0.5f) moved += 1.0f;
        require (at >= 0.0f && moved > 0.0f, "Triad Time's playhead moves on between audio blocks");
    }
    // ---- The frame rate ------------------------------------------------------------------------------------------------------
    {
        triad::ui::FramePacer pacer;
        double last = 0.0, time = 1000.0;
        auto run = [&] (double seconds, double period, double workMs)
        {
            int drawn = 0;
            for (double end = time + seconds; time < end; time += period)
            {
                pacer.addWork (workMs);
                if (pacer.due (last, time)) ++drawn;
            }
            return (double) drawn / seconds;
        };
        const double fast = run (2.0, 1.0 / 120.0, 0.5);
        require (pacer.isFast() && fast > 110.0, "A 120 Hz screen gets 120 frames a second while drawing is quick");
        const double strained = run (2.0, 1.0 / 120.0, 7.0);
        const double after = run (1.0, 1.0 / 120.0, 7.0);
        require (! pacer.isFast() && after < 70.0 && after > 50.0, "When frames take too long, it steps down to 60 a second");
        juce::ignoreUnused (strained);
        run (6.0, 1.0 / 120.0, 0.5);
        require (pacer.isFast(), "...and goes back to 120 once the computer keeps up again");
        const double sixty = run (2.0, 1.0 / 60.0, 0.5);
        require (sixty > 55.0 && sixty < 65.0, "A 60 Hz screen gets every frame");
        pacer.setAudioLoad (0.9f);
        run (2.0, 1.0 / 120.0, 0.5);
        require (! pacer.isFast(), "A busy audio thread (Triad's CPU meter above 75%) keeps the screen at 60");
        std::cout << "Frame rate: 120 a second while drawing is quick (" << juce::roundToInt (fast) << " measured), 60 when strained ("
                  << juce::roundToInt (after) << ")" << std::endl;
    }
    std::cout << "PASS: smooth motion between audio blocks, 120 / 60 frames a second" << std::endl;
}
}
