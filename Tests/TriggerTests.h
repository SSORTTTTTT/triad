#pragma once
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>

// Audio trigger (0.14.0): a band's LFOs start from their start when sound comes in (TRIGGER: AUDIO), at the song's tempo.
namespace triggerTests
{
namespace mod = triad::mod;

inline int lengthIndex (const char* label)
{
    for (int i = 0; i < (int) mod::lengths().size(); ++i)
        if (juce::String (mod::lengths()[(size_t) i].label) == label) return i;
    return mod::defaultLength();
}
// Plays `signal` (mono, both channels) through Triad in blocks of `block`; `each` is called after every block with the
// sample the block ended at. Returns the output (left).
template <typename Each>
std::vector<float> play (TriadProcessor& p, const std::vector<float>& signal, int block, Each&& each)
{
    std::vector<float> out (signal.size());
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (size_t start = 0; start < signal.size(); start += (size_t) block)
    {
        const int n = (int) juce::jmin ((size_t) block, signal.size() - start);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, 0, n);
        for (int i = 0; i < n; ++i) view.setSample (0, i, signal[start + (size_t) i]), view.setSample (1, i, signal[start + (size_t) i]);
        p.processBlock (view, midi);
        for (int i = 0; i < n; ++i) out[start + (size_t) i] = view.getSample (0, i);
        each (start + (size_t) n);
    }
    return out;
}
inline void addTone (std::vector<float>& s, size_t from, size_t to, double hz, float level = 0.5f)
{
    for (size_t i = from; i < juce::jmin (to, s.size()); ++i)
        s[i] += level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (i - from) / 48000.0);
}
inline float rms (const std::vector<float>& s, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += (double) s[i] * s[i];
    return (float) std::sqrt (sum / (double) juce::jmax ((size_t) 1, to - from));
}

inline void run()
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    const int bar = lengthIndex ("1 Bar"), quarter = lengthIndex ("1/4");
    constexpr double samplesPerBar = rate * 2.0;   // 120 BPM, 4/4 (no playhead: Triad's default tempo)
    {
        // The LFO's position: waits at its start, starts where the sound comes in, restarts after a gap, waits again.
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        const int lfo = p.getBandLfo (0);
        p.setLfoLength (0, lfo, bar);
        require (! p.getTrigger (0).audio, "TRIGGER starts as SONG (the LFOs follow the song, as before)");
        TriadProcessor::TriggerSettings audio;
        audio.audio = true;
        audio.thresholdDb = -24.0f;
        p.setTrigger (0, audio);
        require (p.getTrigger (0) == audio && ! p.getTrigger (1).audio, "AUDIO is set for one band (the others keep SONG)");
        // 0.5 s of silence, a 60 Hz tone (band 1's) from sample 24,100 for a second, 100 ms of quiet, the tone again.
        const size_t hit = 24100, gapStart = hit + 48000, second = gapStart + 4800;
        std::vector<float> signal ((size_t) (second + 96000 + 2 * samplesPerBar + 48000), 0.0f);
        addTone (signal, hit, gapStart, 60.0);
        addTone (signal, second, second + 96000, 60.0);
        float waitingPhase = -1.0f, afterHit = -1.0f, oneSecond = -1.0f, afterSecond = -1.0f, idlePhase = -1.0f;
        bool waitingBefore = false, songMoved = false;
        const float otherBefore = p.getLfoPhase (1, p.getBandLfo (1));
        play (p, signal, block, [&] (size_t end)
        {
            const float phase = p.getLfoPhase (0, lfo);
            if (end == 23552) { waitingPhase = phase; waitingBefore = p.getTriggerStatus (0).waiting; }   // before the sound
            if (end == 24576) afterHit = phase;                                            // the block the sound came in
            if (end == 24576 + 46 * 1024) oneSecond = phase;
            if (end > second && afterSecond < 0.0f) afterSecond = phase;
            if (end == signal.size() - (signal.size() % block)) idlePhase = phase;
            songMoved |= ! juce::approximatelyEqual (p.getLfoPhase (1, p.getBandLfo (1)), otherBefore);
        });
        const double expectedAfterHit = (24576.0 - (double) hit) / samplesPerBar;
        std::cout << "Audio trigger: waiting at " << waitingPhase << ", " << afterHit << " after the sound came in (expected about " << expectedAfterHit
                  << "), " << oneSecond << " a second later, " << afterSecond << " after the second sound, " << idlePhase << " after two bars of quiet; "
                  << p.getTriggerStatus (0).count << " restarts" << std::endl;
        require (juce::exactlyEqual (waitingPhase, 0.0f) && waitingBefore, "AUDIO: the LFO waits at its start until sound comes in");
        // (Band 1 hears the tone through its crossover filter, which delays a 60 Hz tone by about 2 ms.)
        require (afterHit <= expectedAfterHit + 1.0e-6 && afterHit > expectedAfterHit - 160.0 / samplesPerBar,
                 "...and starts where the sound comes in (to within the band filter's delay, 160 samples)");
        require (std::abs ((oneSecond - afterHit) - (float) (46 * 1024 / samplesPerBar)) < 1.0e-4f, "...then plays at the song's tempo (a bar: 2 s at 120 BPM)");
        require (afterSecond >= 0.0f && afterSecond < (float) (block + 64) / (float) samplesPerBar, "A new sound after 100 ms of quiet starts it again");
        require (juce::exactlyEqual (idlePhase, 0.0f) && p.getTriggerStatus (0).waiting, "After two bars of quiet it goes back to its start and waits");
        require (p.getTriggerStatus (0).count == 2, "...two sounds, two starts");
        require (songMoved, "Bands on SONG keep following the song");
    }
    {
        // Sample-accurate: a square LFO on band 1's volume (on for the first half, off for the second), 1/4 note long.
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        const int lfo = p.getBandLfo (0);
        p.setLfoShape (0, lfo, { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } }, true);
        p.setLfoLength (0, lfo, quarter);
        require (p.addRoute (0, lfo, { TriadProcessor::Destination::bandVolume, 0 }) >= 0, "Route band 1's LFO to its volume");
        for (int r = 0; r < p.getRouteCount (0); ++r) p.setRouteMuted (0, r, false);
        TriadProcessor::TriggerSettings audio;
        audio.audio = true;
        p.setTrigger (0, audio);
        const size_t hit = 30000;
        std::vector<float> signal ((size_t) hit + 48000, 0.0f);
        addTone (signal, hit, signal.size(), 60.0);
        const auto out = play (p, signal, 64, [] (size_t) {});
        const float on = rms (out, hit + 600, hit + 12000 - 600), off = rms (out, hit + 12000 + 600, hit + 24000 - 600),
                    again = rms (out, hit + 24000 + 600, hit + 36000 - 600);
        std::cout << "Audio trigger, square LFO on the band's volume: " << on << " for the first eighth after the sound, " << off << " for the second, "
                  << again << " in the next cycle" << std::endl;
        require (on > 0.3f && off < 0.01f && again > 0.3f, "...the band's volume follows the LFO from the moment the sound comes in, in time");
        // Where the volume goes off: an eighth note (12,000 samples) after the sound came in.
        size_t drop = hit + 6000;
        while (drop < hit + 18000 && rms (out, drop, drop + 400) > 0.15f) drop += 50;
        std::cout << "  the volume went down " << (long long) drop - (long long) (hit + 12000) << " samples from an eighth after the sound" << std::endl;
        require (std::abs ((long long) drop - (long long) (hit + 12000)) < 400, "...an eighth note after the sound (to within 400 samples, the volume's smoothing)");
    }
    {
        // LISTEN: the band's own sound or the full sound; RESTART: every hit or after a pause; the threshold.
        const auto starts = [&] (TriadProcessor::TriggerSettings s, const std::vector<float>& signal)
        {
            TriadProcessor p;
            p.prepareToPlay (rate, block);
            p.setTrigger (0, s);
            play (p, signal, block, [] (size_t) {});
            return p.getTriggerStatus (0).count;
        };
        std::vector<float> high (48000, 0.0f), hits (96000, 0.0f), quiet (48000, 0.0f);
        addTone (high, 4800, 24000, 8000.0);                                  // a hi-hat-like tone: not band 1's
        for (size_t h = 4800; h + 2400 < hits.size(); h += 12000) addTone (hits, h, h + 2400, 60.0);   // 8 hits, 200 ms of quiet between
        addTone (quiet, 4800, 24000, 60.0, 0.02f);                            // -34 dB
        TriadProcessor::TriggerSettings s;
        s.audio = true;
        require (starts (s, high) == 0, "LISTEN: this band's sound: a sound in another band does not start band 1's LFOs");
        s.listenFull = true;
        require (starts (s, high) == 1, "LISTEN: the full sound: it does");
        s.listenFull = false;
        require (starts (s, hits) == 8, "RESTART: every hit: each of 8 hits starts the LFOs again");
        s.afterPause = true;
        require (starts (s, hits) == 1, "RESTART: after a pause: 200 ms of quiet is not a pause (a beat at 120 BPM is 500 ms)");
        s.afterPause = false;
        require (starts (s, quiet) == 0, "A sound under the threshold (-34 dB, threshold -24 dB) does not start them");
        s.thresholdDb = -42.0f;
        require (starts (s, quiet) == 1, "...lower the threshold and it does");
        {
            // Listening to the full sound (no filter), it starts on the very sample the sound comes in.
            TriadProcessor p;
            p.prepareToPlay (rate, block);
            const int lfo = p.getBandLfo (2);
            p.setLfoLength (2, lfo, bar);
            TriadProcessor::TriggerSettings full;
            full.audio = full.listenFull = true;
            p.setTrigger (2, full);
            std::vector<float> step (2048, 0.0f);
            for (size_t i = 1300; i < step.size(); ++i) step[i] = 0.5f;
            float phase = -1.0f;
            play (p, step, block, [&] (size_t end) { if (end == 1536) phase = p.getLfoPhase (2, lfo); });
            std::cout << "Audio trigger, the full sound: started " << 1536.0 - phase * samplesPerBar << " (sound at 1300)" << std::endl;
            require (std::abs (phase - (float) ((1536.0 - 1300.0) / samplesPerBar)) < 2.0 / samplesPerBar, "LISTEN: the full sound: it starts on the sample the sound comes in");
        }
    }
    {
        // Kept with the band: in the project, in presets and chains, with undo; PRE and POST have it too.
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        TriadProcessor::TriggerSettings s;
        s.audio = true;
        s.listenFull = true;
        s.afterPause = true;
        s.thresholdDb = -36.0f;
        p.setTrigger (1, s);
        p.setTrigger (TriadProcessor::postBand, s);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        TriadProcessor q;
        q.prepareToPlay (rate, block);
        q.setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) TriadProcessor::startupDelayMs + 250);
        require (q.getTrigger (1) == s && q.getTrigger (TriadProcessor::postBand) == s && ! q.getTrigger (0).audio, "The trigger settings are saved with the project");
        q.commitHistory();
        TriadProcessor::TriggerSettings song;
        q.setTrigger (1, song);
        q.commitHistory();
        require (! q.getTrigger (1).audio && q.undo() && q.getTrigger (1) == s, "...and changing them can be undone");
        require (q.redo() && ! q.getTrigger (1).audio, "...and redone");
    }
    std::cout << "Audio trigger: passed" << std::endl;
}
}
