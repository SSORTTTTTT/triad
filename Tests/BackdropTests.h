#pragma once
#include "../Source/PluginEditor.h"
#include "../Source/TriadTime.h"
#include "TriggerTests.h"
#include <iostream>

// Behind the grid (0.15.0): the sound coming in, as a waveform across the LFO's cycle (a new trace at each restart with
// TRIGGER: AUDIO) or as a spectrum (the band's range lit), and in Triad Time across the pattern.
namespace backdropTests
{
namespace mod = triad::mod;
using Trace = std::array<float, triad::WaveTrace::bins>;
using Ages = std::array<int, triad::WaveTrace::bins>;

// A drum loop at 120 BPM (48 kHz): kick on 1 and 3, clap on 2 and 4, hats on the eighths.
inline std::vector<float> drums (size_t length, size_t start = 0)
{
    std::vector<float> s (length, 0.0f);
    juce::Random r (5);
    constexpr double rate = 48000.0;
    for (size_t eighth = 0;; ++eighth)
    {
        const size_t at = start + eighth * 12000;
        if (at >= length) break;
        const int beat = (int) (eighth / 2) % 4;
        const bool onBeat = eighth % 2 == 0;
        for (size_t i = 0; at + i < length && i < 20000; ++i)
        {
            const double t = (double) i / rate;
            float v = 0.12f * (r.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t / 0.02);                          // hat
            if (onBeat && (beat == 0 || beat == 2))
                v += 0.85f * (float) (std::sin (juce::MathConstants<double>::twoPi * (45.0 * t + 75.0 * 0.04 * (1.0 - std::exp (-t / 0.04))))
                                      * std::exp (-t / 0.22));                                                            // kick
            if (onBeat && (beat == 1 || beat == 3))
            {
                const double burst = t < 0.03 ? std::exp (-std::fmod (t, 0.01) / 0.003) : std::exp (-(t - 0.03) / 0.12);
                v += 0.6f * (r.nextFloat() * 2.0f - 1.0f) * (float) burst;                                               // clap
            }
            s[at + i] += v;
        }
    }
    return s;
}

inline void run (const juce::File& screenshots)
{
    constexpr double rate = 48000.0;
    constexpr int block = 480;
    juce::SharedResourcePointer<TriadSettings> held;   // kept for the whole test (a temporary one would let go of it)
    auto& settings = *held;
    require (settings.getLfoBackdrop (true) == TriadSettings::Backdrop::waveform && settings.getLfoBackdrop (false) == TriadSettings::Backdrop::spectrum
                 && settings.getTimeBackdrop() == TriadSettings::Backdrop::off && ! settings.getLfoFullSpectrum(),
             "Behind the grid, to start with: the waveform with TRIGGER: AUDIO, the spectrum (the band's range) with SONG (0.21.0), nothing in Triad Time");
    {
        // SONG: a tone on for the first half of each 1/4-note cycle: the trace shows the first half of the grid loud.
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        const int lfo = p.getBandLfo (0);
        p.setLfoLength (0, lfo, triggerTests::lengthIndex ("1/4"));
        p.setTracedLfo (0, lfo);
        std::vector<float> signal (24000 * 4, 0.0f);
        for (size_t c = 0; c < 4; ++c) triggerTests::addTone (signal, c * 24000, c * 24000 + 12000, 60.0);
        triggerTests::play (p, signal, block, [] (size_t) {});
        Trace level {};
        Ages age {};
        p.readLfoTrace (level, age);
        float firstHalf = 1.0f, secondHalf = 0.0f;
        int fresh = 0;
        for (int i = 0; i < triad::WaveTrace::bins; ++i)
        {
            if (age[(size_t) i] == 0) ++fresh;
            if (i >= 12 && i < 190) firstHalf = juce::jmin (firstHalf, level[(size_t) i]);
            if (i >= 225 && i < 395) secondHalf = juce::jmax (secondHalf, level[(size_t) i]);
        }
        std::cout << "LFO waveform (SONG): the loudest in the quiet half " << secondHalf << ", the quietest in the loud half " << firstHalf
                  << ", " << fresh << " of " << triad::WaveTrace::bins << " steps from this cycle" << std::endl;
        require (firstHalf > 0.05f && secondHalf < 0.02f && fresh > 380, "The waveform behind the LFO follows the sound across its cycle");
        p.setTracedLfo (-1, -1);
    }
    {
        // AUDIO: nothing is traced while the LFO waits; each sound that restarts it starts a new trace at the left.
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        const int lfo = p.getBandLfo (0);
        p.setLfoLength (0, lfo, triggerTests::lengthIndex ("1 Bar"));
        TriadProcessor::TriggerSettings audio;
        audio.audio = audio.listenFull = true;
        p.setTrigger (0, audio);
        p.setTracedLfo (0, lfo);
        const auto cycleBefore = p.getLfoTraceCycle();
        std::vector<float> quiet (24000, 0.0f);
        triggerTests::play (p, quiet, block, [] (size_t) {});
        Trace level {};
        Ages age {};
        p.readLfoTrace (level, age);
        require (std::all_of (age.begin(), age.end(), [] (int a) { return a >= 2; }), "AUDIO: nothing traced while the LFO waits for sound");
        std::vector<float> hits (96000, 0.0f);
        triggerTests::addTone (hits, 1000, 6000, 60.0);
        triggerTests::addTone (hits, 30000, 35000, 60.0);
        triggerTests::play (p, hits, block, [] (size_t) {});
        p.readLfoTrace (level, age);
        require (p.getLfoTraceCycle() > cycleBefore && age[2] == 0 && level[2] > 0.2f, "...a sound starts a trace at the left edge");
        p.setTracedLfo (-1, -1);
    }
    {
        // Triad Time: the sound coming in across the pattern.
        TriadTime t;
        t.prepareToPlay (rate, block);
        t.setTracedEnvelope (TriadTime::timeEnvelope);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        const auto loop = drums (96000);
        for (size_t start = 0; start + block <= loop.size(); start += block)
        {
            for (int i = 0; i < block; ++i) buffer.setSample (0, i, loop[start + (size_t) i]), buffer.setSample (1, i, loop[start + (size_t) i]);
            t.processBlock (buffer, midi);
        }
        Trace level {};
        Ages age {};
        t.readTrace (level, age);
        const auto loud = std::count_if (level.begin(), level.end(), [] (float v) { return v > 0.05f; });
        require (loud > 100 && std::count (age.begin(), age.end(), 0) > 300, "Triad Time traces the sound coming in across its pattern");
        t.setTracedEnvelope (-1);
    }
    std::cout << "Behind the grid (traces): passed" << std::endl;
    if (screenshots == juce::File()) return;
    {
        // Pictures: TRIGGER: AUDIO with a drum loop (the waveform), and SONG with the spectrum.
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (p.createEditor()));
        editor->setVisible (true);
        editor->setTab (TriadEditor::Tab::lfo);
        const int lfo = p.getBandLfo (0);
        p.setLfoLength (0, lfo, triggerTests::lengthIndex ("1 Bar"));
        p.setLfoShape (0, lfo, mod::preset (22), true);
        TriadProcessor::TriggerSettings audio;
        audio.audio = audio.listenFull = audio.afterPause = true;
        p.setTrigger (0, audio);
        editor->getLfoPage().select (0, lfo);
        p.setTracedLfo (0, lfo);
        std::vector<float> signal (24000 + 96000 * 2, 0.0f);
        const auto loop = drums (96000 * 2);
        std::copy (loop.begin(), loop.end(), signal.begin() + 24000);
        triggerTests::play (p, signal, block, [] (size_t) {});
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        require (editor->getLfoPage().hasBackdrop(), "With TRIGGER: AUDIO the LFO tab shows the waveform");
        themeTests::snapshot (screenshots, "Triad-lfo-waveform.png", *editor);
        settings.setLfoBackdrop (false, TriadSettings::Backdrop::spectrum);
        TriadProcessor::TriggerSettings song;
        p.setTrigger (1, song);
        editor->getLfoPage().select (1, p.getBandLfo (1));
        p.setLfoShape (1, p.getBandLfo (1), mod::preset (40), true);
        std::vector<float> mix = drums (48000);
        for (double hz : { 220.0, 330.0, 440.0, 660.0, 1320.0, 2640.0 }) triggerTests::addTone (mix, 0, mix.size(), hz, 0.08f);
        triggerTests::play (p, mix, block, [] (size_t) {});
        juce::MessageManager::getInstance()->runDispatchLoopUntil (700);   // the setting is read twice a second
        require (editor->getLfoPage().getBackdrop() == TriadSettings::Backdrop::spectrum, "With SONG, the spectrum when chosen in the gear menu");
        themeTests::snapshot (screenshots, "Triad-lfo-spectrum.png", *editor);
        settings.setLfoBackdrop (false, TriadSettings::Backdrop::off);
    }
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        settings.setTimeBackdrop (TriadSettings::Backdrop::waveform);
        std::unique_ptr<juce::AudioProcessorEditor> editor (t.createEditor());
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        t.setTracedEnvelope (TriadTime::timeEnvelope);
        t.setShape (TriadTime::timeEnvelope, t.getSlot (TriadTime::timeEnvelope), triad::timePresets::shape (2005), false);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        const auto loop = drums (96000);
        for (size_t start = 0; start + block <= loop.size(); start += block)
        {
            for (int i = 0; i < block; ++i) buffer.setSample (0, i, loop[start + (size_t) i]), buffer.setSample (1, i, loop[start + (size_t) i]);
            t.processBlock (buffer, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        themeTests::snapshot (screenshots, "Triad-Time-waveform.png", *editor);
        settings.setTimeBackdrop (TriadSettings::Backdrop::off);
    }
    {
        // Triad Time's spectrum, as Triad's (the whole range lit in the envelope's colour).
        TriadTime t;
        t.prepareToPlay (rate, block);
        settings.setTimeBackdrop (TriadSettings::Backdrop::spectrum);
        std::unique_ptr<juce::AudioProcessorEditor> editor (t.createEditor());
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        themeTests::snapshot (screenshots, "Triad-Time-spectrum.png", *editor);   // drawing it starts the analyser
        require (t.getSpectrumTap().active.load(), "Triad Time: the spectrum behind the grid listens while it is shown");
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        auto mix = drums (48000);
        for (double hz : { 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0 }) triggerTests::addTone (mix, 0, mix.size(), hz, 0.06f);
        for (size_t start = 0; start + block <= mix.size(); start += block)
        {
            for (int i = 0; i < block; ++i) buffer.setSample (0, i, mix[start + (size_t) i]), buffer.setSample (1, i, mix[start + (size_t) i]);
            t.processBlock (buffer, midi);
        }
        require (t.getSpectrumTap().getWriteIndex() > 0, "...and hears the sound coming in");
        themeTests::snapshot (screenshots, "Triad-Time-spectrum.png", *editor);
        settings.setTimeBackdrop (TriadSettings::Backdrop::off);
        editor.reset();
        require (! t.getSpectrumTap().active.load(), "...and stops when the window closes");
    }
}
}
