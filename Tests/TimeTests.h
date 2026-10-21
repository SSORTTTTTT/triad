#pragma once
#include "../Source/TriadTime.h"
#include "../Source/ShapePanel.h"
#include "../Source/TimePresets.h"
#include <cmath>
#include <iostream>

// Triad Time: the time / volume gate (built into Triad, and a plug-in of its own), and Triad's band MUTE.
namespace timeTests
{
namespace mod = triad::mod;

// A DAW transport: tempo, position, playing.
struct Head final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0, rate = 48000.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setTimeInSamples ((juce::int64) std::llround (ppq * 60.0 / bpm * rate));
        info.setTimeInSeconds (ppq * 60.0 / bpm);
        return info;
    }
    void advance (int samples) { if (playing) ppq += samples / rate * bpm / 60.0; }
};

// Plays `input` (stereo, same on both channels) through `p` in blocks of `block`; returns the left output.
inline std::vector<float> run (juce::AudioProcessor& p, Head& head, const std::vector<float>& input, int block)
{
    std::vector<float> out (input.size());
    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    p.setPlayHead (&head);
    for (size_t start = 0; start < input.size(); start += (size_t) block)
    {
        const int n = (int) juce::jmin ((size_t) block, input.size() - start);
        audio.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < n; ++s) audio.setSample (c, s, input[start + (size_t) s]);
        p.processBlock (audio, midi);
        for (int s = 0; s < n; ++s) out[start + (size_t) s] = audio.getSample (0, s);
        head.advance (n);
    }
    p.setPlayHead (nullptr);
    return out;
}
inline std::vector<float> noise (size_t n, int seed = 7)
{
    juce::Random r (seed);
    std::vector<float> v (n);
    for (auto& x : v) x = (r.nextFloat() * 2.0f - 1.0f) * 0.4f;
    return v;
}
inline std::vector<float> sine (size_t n, double hz, double rate, float level = 0.5f)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate);
    return v;
}
// Frequency from upward zero crossings in [from, to).
inline double frequency (const std::vector<float>& v, size_t from, size_t to, double rate)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f) { if (crossings == 0) first = i; last = i; ++crossings; }
    return crossings > 1 ? (crossings - 1) * rate / (double) (last - first) : 0.0;
}
inline float biggestStep (const std::vector<float>& v, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = juce::jmax ((size_t) 1, from); i < to; ++i) m = juce::jmax (m, std::abs (v[i] - v[i - 1]));
    return m;
}
inline void setPlain (juce::RangedAudioParameter* p, float plain) { p->setValueNotifyingHost (p->convertTo0to1 (plain)); }
// Clicks the TIME or VOLUME tab of a Triad Time window.
inline void editorShowEnvelope (juce::Component& editor, int envelope)
{
    for (auto* c : editor.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
            if (b->getButtonText() == (envelope == TriadTime::timeEnvelope ? "TIME" : "VOLUME")) { b->onClick(); return; }
}

inline void run (const juce::File& screenshotFolder)
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    const size_t samplesPerBeat = 24000;   // 120 bpm
    // ---- Untouched: the sound passes exactly ------------------------------------------------------------------
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        Head head;
        const auto in = noise (96000);
        const auto out = run (t, head, in, block);
        float worst = 0.0f;
        for (size_t i = 0; i < in.size(); ++i) worst = juce::jmax (worst, std::abs (out[i] - in[i]));
        require (worst < 1.0e-6f && t.getLatencySamples() == 0, "Triad Time with pattern 1 (live, full volume) passes the sound exactly, no latency");
        require (t.getJumpCount() == 0, "No time jumps while live");
    }
    // ---- TIME: one beat back is exactly one beat late -----------------------------------------------------------
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        t.setShape (TriadTime::timeEnvelope, 0, mod::flat (1.0f - 1.0f / 4.0f), false);   // 1 bar pattern: the bottom is 4 beats back
        Head head;
        const auto in = noise (samplesPerBeat * 3);
        const auto out = run (t, head, in, block);
        float worst = 0.0f;
        for (size_t i = samplesPerBeat; i < in.size(); ++i) worst = juce::jmax (worst, std::abs (out[i] - in[i - samplesPerBeat]));
        require (worst < 1.0e-5f, "A flat TIME line one beat down plays the sound exactly one beat (24000 samples at 120 bpm) late");
        float first = 0.0f;
        for (size_t i = 0; i < samplesPerBeat; ++i) first = juce::jmax (first, std::abs (out[i]));
        require (first < 1.0e-6f, "Before there is a beat of history, it is silent (nothing to play yet)");
    }
    // ---- TIME: half speed is an octave down; reverse plays backwards ---------------------------------------------
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        setPlain (t.timeSlot, 2);   // "Half speed"
        require (TriadTime::slotName (TriadTime::timeEnvelope, 1) == "Half speed", "Pattern 2 of TIME is Half speed");
        Head head;
        const auto in = sine (samplesPerBeat * 8, 440.0, rate);
        const auto out = run (t, head, in, block);
        const double f = frequency (out, samplesPerBeat * 5, samplesPerBeat * 7, rate);
        std::cout << "Triad Time half speed: 440 Hz in, " << f << " Hz out" << std::endl;
        require (std::abs (f - 220.0) < 3.0, "Half speed plays a 440 Hz tone at 220 Hz");
    }
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        setPlain (t.timeSlot, 10);   // "Reverse, 2nd half"
        setPlain (t.smoothing, 0.0f);
        Head head;
        std::vector<float> ramp (samplesPerBeat * 8);
        for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) ((double) (i % (samplesPerBeat * 4)) * 1.0e-6);
        const auto out = run (t, head, ramp, block);
        // Second bar, beats 3-4 (reversed): each sample is one sample earlier than the one before.
        int backwards = 0, total = 0;
        for (size_t i = samplesPerBeat * 6 + 100; i < samplesPerBeat * 8 - 100; ++i, ++total)
            if (std::abs ((out[i] - out[i - 1]) + 1.0e-6f) < 2.0e-7f) ++backwards;
        require (backwards > total * 0.98, "Reverse plays the second half of the bar backwards at normal speed");
    }
    // ---- The grid as ShaperBox's: a line one grid cell down per cell across holds the sound still ------------------------
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        setPlain (t.smoothing, 0.0f);
        // Thomas's teeth: from 0 down to -1/16 over a 1/16, twice (as drawn in ShaperBox 3).
        t.setShape (TriadTime::timeEnvelope, 0, { { 0.0f, 1.0f, 0.0f }, { 0.0625f, 0.9375f, 0.0f }, { 0.0625f, 1.0f, 0.0f }, { 0.125f, 0.9375f, 0.0f },
                                                  { 0.125f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, false);
        Head head;
        std::vector<float> ramp (samplesPerBeat * 8);
        for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) ((double) i * 1.0e-6);
        const auto out = run (t, head, ramp, block);
        // Second bar, its first 1/16 (6,000 samples at 120 bpm): the sound stands still at where the 1/16 began.
        const size_t bar = samplesPerBeat * 4, sixteenth = samplesPerBeat / 4;
        float moved = 0.0f;
        for (size_t i = bar + 50; i < bar + sixteenth - 50; ++i) moved = juce::jmax (moved, std::abs (out[i] - out[bar + 50]));
        require (moved < 1.0e-6f, "One grid cell down per cell across holds the sound still, as in ShaperBox");
        require (std::abs (out[bar + sixteenth + 3000] - ramp[bar + sixteenth + 3000]) > 1.0e-4f, "...and the second tooth holds it again");
    }
    // ---- ShaperBox's octave cut: from 0 down to -1/16 over 1/8 is an octave down (-12 semitones) ---------------------------
    {
        TriadTime t;
        t.prepareToPlay (rate, block);
        // As Thomas drew it in ShaperBox 3 (1 bar, As LFO Length): the first cut of his pattern.
        t.setShape (TriadTime::timeEnvelope, 0, { { 0.0f, 1.0f, 0.0f }, { 0.125f, 0.9375f, 0.0f }, { 0.125f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, false);
        Head head;
        const auto in = sine (samplesPerBeat * 8, 440.0, rate);
        const auto out = run (t, head, in, block);
        const size_t bar = samplesPerBeat * 4, eighth = samplesPerBeat / 2;
        const double f = frequency (out, bar + 1000, bar + eighth - 1000, rate);
        std::cout << "Triad Time octave cut (0 to -1/16 over 1/8): 440 Hz in, " << f << " Hz out" << std::endl;
        require (std::abs (f - 220.0) < 2.0, "A cut from 0 down to -1/16 over 1/8 plays a 440 Hz tone at 220 Hz: an octave down, as in ShaperBox");
        const double after = frequency (out, bar + eighth + 2000, bar + samplesPerBeat * 2, rate);
        require (std::abs (after - 440.0) < 2.0, "...and back at the top it plays live again");
        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
        const auto label = [] (float x0, float y0, float x1, float y1) { return TriadTime::pitchText (TriadTime::segmentSpeed ({ x0, y0, 0.0f }, { x1, y1, 0.0f })); };
        const auto cut = TriadTime::segmentSpeed ({ 0.0f, 1.0f, 0.0f }, { 0.125f, 0.9375f, 0.0f });
        require (std::abs (cut - 0.5) < 1.0e-9 && TriadTime::pitchText (cut) == "FWD " + minus + "12 st", "That cut's label reads FWD -12 st");
        require (label (0.0f, 1.0f, 0.25f, 0.9375f) == "FWD " + minus + "5 st", "Half as steep: FWD -5 st");
        require (label (0.0f, 1.0f, 0.25f, 0.8125f) == "FWD " + minus + "24 st", "A quarter speed (3/16 down over 1/4): FWD -24 st");
        require (label (0.0f, 0.25f, 0.25f, 1.0f) == "FWD +24 st" && label (0.0f, 0.5f, 0.25f, 0.75f) == "FWD +12 st", "Rising lines play faster: FWD +12 st, FWD +24 st");
        require (label (0.0f, 1.0f, 0.0625f, 0.9375f) == "FREEZE", "One cell down per cell across: FREEZE");
        require (label (0.0f, 1.0f, 0.25f, 0.5f) == "REV 0 st", "Twice as steep: REV 0 st (backwards at normal pitch)");
        require (label (0.0f, 1.0f, 0.25f, 0.625f) == "REV " + minus + "12 st" && label (0.0f, 1.0f, 0.125f, 0.625f) == "REV +12 st",
                 "Reversed and pitched: REV -12 st, REV +12 st");
        require (label (0.0f, 0.75f, 0.5f, 0.75f).isEmpty(), "A flat line (live or a steady delay) has no label");
        require (std::abs (TriadTime::segmentSpeed ({ 0.0f, 1.0f, 0.0f }, { 0.25f, 0.625f, 0.0f }) + 0.5) < 1.0e-9, "...REV -12 st is backwards at half speed");
    }
    // ---- The TIME folders trained on the Gross Beat banks: octave repitches forwards and backwards, and chops ---------------
    {
        namespace tp = triad::timePresets;
        require (tp::numFolders == 5 && tp::numPresets == 120, "Five TIME folders, 120 patterns");
        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
        int onGrid = 0, inRange = 0;
        std::array<int, 5> good {};
        struct Line { double speed; float from, to; };
        std::vector<std::pair<int, Line>> octaveLines;   // (preset, its longest octave line)
        for (int i = 0; i < tp::numPresets; ++i)
        {
            const auto shape = tp::shape (i);
            bool grid = juce::exactlyEqual (shape.front().x, 0.0f) && juce::exactlyEqual (shape.back().x, 1.0f), range = true;
            int chops = 0;
            bool down = false, up = false, rev = false;
            Line longest { 1.0, 0.0f, 0.0f };
            for (size_t k = 0; k < shape.size(); ++k)
            {
                const auto& p = shape[k];
                grid &= std::abs (p.x * 64.0f - std::round (p.x * 64.0f)) < 1.0e-4f && std::abs (p.y * 16.0f - std::round (p.y * 16.0f)) < 1.0e-4f;
                range &= p.y >= -1.0e-6f && p.y <= 1.0f + 1.0e-6f && (k == 0 || p.x >= shape[k - 1].x);
                if (k == 0) continue;
                const auto& a = shape[k - 1];
                if (juce::exactlyEqual (a.x, p.x) && p.y < a.y - 1.0e-4f) ++chops;   // a jump further back: a chop
                if (p.x - a.x < 1.0f / 16.0f - 1.0e-6f || std::abs (a.curve) > 1.0e-3f) continue;   // straight lines of a 1/16 bar or more
                const auto text = TriadTime::pitchText (TriadTime::segmentSpeed (a, p));
                down |= text == "FWD " + minus + "12 st" || text == "FWD " + minus + "24 st";
                up |= text == "FWD +12 st" || text == "FWD +24 st";
                rev |= text.startsWith ("REV");
                const bool octave = text == "FWD " + minus + "12 st" || text == "FWD " + minus + "24 st" || text == "FWD +12 st" || text == "REV 0 st"
                                    || text == "REV " + minus + "12 st";
                if (octave && p.x - a.x > longest.to - longest.from) longest = { TriadTime::segmentSpeed (a, p), a.x, p.x };
            }
            onGrid += grid ? 1 : 0;
            inRange += range ? 1 : 0;
            const int folder = tp::entries[i].folder;
            const bool ok = folder == 0 ? down : folder == 1 ? up : folder == 2 ? rev : folder == 3 ? chops >= 3 : (chops >= 2 && (down || up || rev));
            good[(size_t) folder] += ok ? 1 : 0;
            if (longest.to - longest.from >= 0.124f) octaveLines.push_back ({ i, longest });
        }
        std::cout << "TIME folders: " << onGrid << " of 120 on the grid, " << inRange << " in range; octave down " << good[0] << "/24, octave up " << good[1]
                  << "/24, reverse " << good[2] << "/24, chops " << good[3] << "/24, pitch + chops " << good[4] << "/24" << std::endl;
        require (onGrid == 120 && inRange == 120, "Every TIME folder pattern is on Triad Time's grid (1/64 bar, 1/16 bar back) and within one bar back");
        require (good[0] == 24 && good[1] == 24 && good[2] == 24 && good[3] == 24 && good[4] == 24,
                 "Each folder has what it is named for: a straight FWD -12 / -24, FWD +12 / +24, REV line; 3 chops; or 2 chops and an octave or reverse line");
        // Played: the octave lines bend a 440 Hz tone by an octave (forwards or backwards).
        int checked = 0, right = 0;
        for (size_t k = 0; k < octaveLines.size() && checked < 12; k += juce::jmax ((size_t) 1, octaveLines.size() / 12), ++checked)
        {
            const auto& [index, line] = octaveLines[k];
            TriadTime t;
            t.prepareToPlay (rate, block);
            setPlain (t.smoothing, 0.0f);
            t.setShape (TriadTime::timeEnvelope, 0, tp::shape (index), false);
            Head head;
            const auto in = sine (samplesPerBeat * 8, 440.0, rate);
            const auto out = run (t, head, in, block);
            const auto bar = (double) samplesPerBeat * 4.0;
            const auto from = (size_t) (bar * 1.0 + bar * line.from) + 600, to = (size_t) (bar * 1.0 + bar * line.to) - 600;
            const double f = frequency (out, from, to, rate), expected = 440.0 * std::abs (line.speed);
            if (std::abs (f - expected) < expected * 0.02) ++right;
            else std::cout << "  " << tp::entries[index].name << ": expected " << expected << " Hz, got " << f << std::endl;
        }
        std::cout << "TIME folders played: " << right << " of " << checked << " octave / reverse lines bend 440 Hz as labelled" << std::endl;
        require (checked >= 10 && right == checked, "The folders' octave and reverse lines bend the pitch as labelled when played");
    }
    // ---- SMOOTHING: repeats do not click ---------------------------------------------------------------------------------
    {
        auto clickiness = [&] (float smoothingMs, int& jumps)
        {
            TriadTime t;
            t.prepareToPlay (rate, block);
            setPlain (t.timeSlot, 6);   // "Repeat 1/16, beat 4"
            setPlain (t.smoothing, smoothingMs);
            Head head;
            // 441 Hz: a jump back of a 1/16 note (6000 samples) lands at another point of the wave (440 Hz would line up exactly).
            const auto in = sine (samplesPerBeat * 8, 441.0, rate);
            const auto out = run (t, head, in, block);
            jumps = t.getJumpCount();
            return biggestStep (out, samplesPerBeat, out.size());
        };
        int jumpsSmooth = 0, jumpsHard = 0;
        const float smooth = clickiness (8.0f, jumpsSmooth), hard = clickiness (0.0f, jumpsHard);
        const float natural = 0.5f * 2.0f * juce::MathConstants<float>::pi * 441.0f / (float) rate;
        std::cout << "Triad Time repeats: biggest step " << smooth << " with 8 ms smoothing, " << hard << " without (the tone itself: " << natural
                  << "); " << jumpsSmooth << " jumps" << std::endl;
        require (jumpsSmooth >= 6, "The 1/16 repeats jump back in time (3 per bar, and back to live)");
        require (smooth < natural * 1.6f, "With smoothing, the jumps are crossfaded: no step bigger than the tone's own");
        require (hard > natural * 3.0f, "Without smoothing the same jumps click (so the test can tell)");
    }
    // ---- VOLUME: the gate, attack and release, mix -----------------------------------------------------------------------
    {
        auto gated = [&] (float attackMs, float releaseMs, float mix, double bpm)
        {
            TriadTime t;
            t.prepareToPlay (rate, block);
            setPlain (t.volumeSlot, 2);   // "Gate 1/8": on for each 1/16, off for the next
            setPlain (t.attack, attackMs);
            setPlain (t.release, releaseMs);
            setPlain (t.volumeMix, mix);
            Head head;
            head.bpm = bpm;
            const std::vector<float> dc ((size_t) (rate * 4.0), 0.5f);
            return run (t, head, dc, block);
        };
        {
            const auto out = gated (0.0f, 0.0f, 100.0f, 120.0);
            const size_t sixteenth = samplesPerBeat / 4;
            require (std::abs (out[sixteenth / 2] - 0.5f) < 1.0e-6f && std::abs (out[sixteenth + sixteenth / 2]) < 1.0e-6f
                     && std::abs (out[2 * sixteenth + sixteenth / 2] - 0.5f) < 1.0e-6f, "Gate 1/8: open for a 1/16, closed for the next (120 bpm)");
        }
        {
            const auto out = gated (0.0f, 0.0f, 100.0f, 90.0);
            const auto sixteenth = (size_t) (rate * 60.0 / 90.0 / 4.0);
            require (std::abs (out[sixteenth / 2] - 0.5f) < 1.0e-6f && std::abs (out[sixteenth + sixteenth / 2]) < 1.0e-6f,
                     "The gate follows the song's tempo (90 bpm)");
        }
        {
            const auto out = gated (4.0f, 8.0f, 100.0f, 120.0);
            const float step = biggestStep (out, 1, out.size());
            require (step < 0.5f * 0.02f, "With attack and release the gate opens and closes smoothly (no click)");
            const size_t sixteenth = samplesPerBeat / 4;
            require (std::abs (out[sixteenth - 10] - 0.5f) < 0.01f && out[2 * sixteenth - 10] < 0.01f, "...and still opens fully and closes fully");
        }
        {
            const auto out = gated (0.0f, 0.0f, 50.0f, 120.0);
            require (std::abs (out[samplesPerBeat / 4 + samplesPerBeat / 8] - 0.25f) < 1.0e-5f, "Volume mix 50%: the closed gate lets half through");
        }
    }
    // ---- State: patterns and knobs come back ---------------------------------------------------------------------------
    {
        TriadTime a;
        auto drawn = mod::flat (0.5f);
        drawn.insert (drawn.begin() + 1, { 0.5f, 0.9f, 1.5f });
        mod::normalise (drawn);
        a.setShape (TriadTime::volumeEnvelope, 4, drawn, true);
        setPlain (a.timeSlot, 3);
        setPlain (a.release, 120.0f);
        a.setLength (TriadTime::volumeEnvelope, mod::defaultLength() - 2);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        TriadTime b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        require (b.getShape (TriadTime::volumeEnvelope, 4) == a.getShape (TriadTime::volumeEnvelope, 4), "A drawn pattern is saved and restored");
        require (b.getSlot (TriadTime::timeEnvelope) == 2 && std::abs (b.release->get() - 120.0f) < 0.2f
                 && b.getLength (TriadTime::volumeEnvelope) == a.getLength (TriadTime::volumeEnvelope), "Its knobs, pattern choices and lengths are restored");
        require (a.canUndo (TriadTime::volumeEnvelope) && a.undo (TriadTime::volumeEnvelope)
                 && a.getShape (TriadTime::volumeEnvelope, 4) == TriadTime::slotPreset (TriadTime::volumeEnvelope, 4), "Undo puts a pattern back");
        bool freeLength = false;
        for (int i = 0; i < (int) mod::lengths().size(); ++i) freeLength |= mod::lengths()[(size_t) i].column == 3 && TriadTime::isTimeLength (i);
        require (TriadTime::isTimeLength (mod::defaultLength()) && ! freeLength, "TIME lengths follow the song's tempo (1 bar by default, no free Hz lengths)");
        // A project saved by Triad Time 0.8 / 0.9 (its TIME grid reached two lengths back): the same delays on today's grid.
        juce::XmlElement old ("TriadTime");
        old.setAttribute ("version", 1);
        auto* time = mod::toXml (mod::flat (0.875f), "Time").release();   // one beat back then (1 bar pattern)
        time->setAttribute ("slot", 1);
        old.addChildElement (time);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (old, oldState);
        TriadTime c;
        c.setStateInformation (oldState.getData(), (int) oldState.getSize());
        require (std::abs (c.getShape (TriadTime::timeEnvelope, 0)[0].y - 0.75f) < 1.0e-6f, "Patterns saved before 0.10 keep their delays (one beat back is still one beat)");
    }
    // ---- Inside Triad: listed, loads, saves, reloads; LFO links keep the song position -------------------------------
    {
        const auto builtIns = TriadBuiltInFormat::effects();
        require (builtIns.size() == 1 && builtIns[0].name == "Triad Time" && builtIns[0].manufacturerName == "Thomas Ross",
                 "Triad lists its built-in effect: Triad Time by Thomas Ross");
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        require (p.loadEffect (0, 0, builtIns[0], false).isEmpty() && p.getSlotInfo (0, 0).loaded && p.getSlotInfo (0, 0).name == "Triad Time",
                 "Triad Time loads into a band like any effect");
        auto* fx = dynamic_cast<TriadTime*> (p.getHostedEffect (0, 0));
        require (fx != nullptr && p.getLatencySamples() == 0, "It is Triad's own (no latency)");
        fx->setShape (TriadTime::timeEnvelope, 7, mod::flat (0.25f), false);
        setPlain (fx->volumeSlot, 4);
        bool knob = false;
        for (const auto& [destination, name] : p.getEffectParameters (0, 0)) knob |= name == "Time mix";
        require (knob, "Its knobs can be linked to Triad's LFOs");
        juce::MemoryBlock project;
        p.getStateInformation (project);
        TriadProcessor reopened;
        reopened.prepareToPlay (rate, block);
        reopened.setStateInformation (project.getData(), (int) project.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto* back = dynamic_cast<TriadTime*> (reopened.getHostedEffect (0, 0));
        require (back != nullptr && back->getSlot (TriadTime::volumeEnvelope) == 3
                 && back->getShape (TriadTime::timeEnvelope, 7) == fx->getShape (TriadTime::timeEnvelope, 7), "A project with Triad Time reopens with its patterns");
    }
    {
        // An LFO linked to one of its knobs makes Triad play it in 64-sample pieces: the pieces must see the song move on,
        // so the gate lands exactly where it does on its own.
        TriadTime alone;
        alone.prepareToPlay (rate, block);
        setPlain (alone.volumeSlot, 3);   // Gate 1/16
        setPlain (alone.attack, 0.0f);
        setPlain (alone.release, 0.0f);
        Head head1;
        const auto in = noise (samplesPerBeat * 4, 9);
        const auto reference = run (alone, head1, in, block);
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        while (p.getBandCount() > 1) p.removeBand (p.getBandCount() - 1);
        require (p.loadEffect (0, 0, TriadTime::makeDescription(), false).isEmpty(), "Load Triad Time into a 1-band Triad");
        auto* fx = dynamic_cast<TriadTime*> (p.getHostedEffect (0, 0));
        setPlain (fx->volumeSlot, 3);
        setPlain (fx->attack, 0.0f);
        setPlain (fx->release, 0.0f);
        int release = -1;
        const auto knobs = p.getEffectParameters (0, 0);
        for (size_t k = 0; k < knobs.size(); ++k) if (knobs[k].second == "Smoothing") release = (int) k;
        require (release >= 0, "Smoothing is a linkable knob");
        const int lfo = p.addLfo (0);
        p.setLfoShape (0, lfo, mod::flat (1.0f), false);   // drawn on: the LFO is on
        const int route = p.addRoute (0, lfo, knobs[(size_t) release].first);
        p.setRouteAmount (0, route, 0.0f);   // linked, but leaves the knob where it is
        require (route >= 0, "Link an LFO to Triad Time's Smoothing knob");
        Head head2;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto inside = run (p, head2, in, block);
        float worst = 0.0f;
        for (size_t i = 2400; i < in.size(); ++i) worst = juce::jmax (worst, std::abs (inside[i] - reference[i]));   // after Triad's 10 ms fade-in
        std::cout << "Triad Time in Triad with a knob link: largest difference from Triad Time alone " << worst << std::endl;
        require (worst < 1.0e-4f, "Inside Triad, with a knob linked (played in 64-sample pieces), the gate lands where it does on its own");
    }
    // ---- Its window: the LFO system (palette, generator, USER) on the TIME and VOLUME envelopes -------------------------
    {
        // (Drawn off screen: no window needed.)
        TriadTime t;
        std::unique_ptr<juce::AudioProcessorEditor> editor (t.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        ShapePanel* shapes = nullptr;
        GeneratorPanel* generator = nullptr;
        for (auto* c : editor->getChildren())
        {
            if (auto* s = dynamic_cast<ShapePanel*> (c)) shapes = s;
            if (auto* g = dynamic_cast<GeneratorPanel*> (c)) generator = g;
        }
        require (shapes != nullptr && generator != nullptr, "Triad Time's window has the LFO grid, palette and generator");
        {
            // The grid laid out as ShaperBox's: 64 columns and 16 rows for a 1-bar pattern (4 x 4 big squares), points snap to all.
            auto& grid = shapes->getEditor();
            require (grid.gridDivision == 64 && grid.columns() == 64 && grid.rows() == 16 && grid.snapRows() == 16,
                     "Triad Time's grid: 64 columns and 16 rows for a 1-bar pattern, as ShaperBox's");
            grid.gridDivision = 16;
            require (grid.columns() == 16 && grid.rows() == 16, "...and the GRID button still sets the columns");
            grid.gridDivision = 64;
            require (std::abs (TriadTime::timeDepthInBeats (4.0) - 4.0) < 1.0e-9, "The TIME grid reaches one pattern length back (ShaperBox's \"As LFO Length\")");
            // ShaperBox's proportions: the same angle on screen is the same pitch (a cut drawn as in ShaperBox is an octave down).
            std::cout << "Triad Time grid: " << grid.plot().getWidth() << " x " << grid.plot().getHeight() << " (aspect " << grid.plotAspect()
                      << ", ShaperBox " << ShapeHost::shaperBoxAspect << "), window " << editor->getWidth() << " x " << editor->getHeight() << std::endl;
            require (std::abs (grid.plotAspect() - ShapeHost::shaperBoxAspect) < 0.03f, "Triad Time's grid has ShaperBox's proportions (3.48 : 1)");
            require (grid.plot().getWidth() > 0.9f * (float) editor->getWidth(), "...across the whole window, as ShaperBox's");
        }
        require (shapes->getPaletteCategory() >= ShapePanel::timeFolder || shapes->getPaletteCategory() < 1000 || shapes->getPaletteCategory() == ShapePanel::userFolder,
                 "TIME opens a folder");
        shapes->setPaletteCategory (ShapePanel::timeFolder + 2);
        require (shapes->getPaletteCategory() == ShapePanel::timeFolder + 2 && shapes->getNumPaletteButtons() == 11, "TIME has its own folders (REVERSE: 24 patterns, 11 a page)");
        {
            const auto beforeFolder = t.getShape (TriadTime::timeEnvelope, 0);
            shapes->getPaletteButton (0)->onClick();
            require (t.getShape (TriadTime::timeEnvelope, 0) == triad::timePresets::shape (triad::timePresets::presetsIn (2).front())
                         && ! (beforeFolder == t.getShape (TriadTime::timeEnvelope, 0)), "A TIME folder pattern goes onto the grid");
            t.setShape (TriadTime::timeEnvelope, 0, beforeFolder, false);
            shapes->getEditor().reload();
        }
        editorShowEnvelope (*editor, TriadTime::volumeEnvelope);
        require (shapes->getPaletteCategory() < ShapePanel::timeFolder, "VOLUME shows Triad's folders (the TIME folders are TIME's)");
        editorShowEnvelope (*editor, TriadTime::timeEnvelope);
        require (shapes->getPaletteCategory() == ShapePanel::timeFolder + 2, "...and back on TIME, the TIME folder chosen there");
        shapes->setPaletteCategory (1);
        require (shapes->getNumPaletteButtons() > 0, "The preset palette shows Triad's shapes");
        const auto before = t.getShape (TriadTime::timeEnvelope, 0);
        shapes->getPaletteButton (0)->onClick();
        require (! (t.getShape (TriadTime::timeEnvelope, 0) == before) || shapes->getPaletteButton (0) == nullptr, "A palette shape replaces the TIME pattern");
        // The green check and the red X under the dice (a test taste, forgotten afterwards).
        generator->setTasteKey ("test");
        generator->forgetTaste();
        generator->rate (true);
        require (generator->getTaste().countLiked() == 1, "The green check rates the shape on the grid as liked");
        const auto ratedAway = t.getShape (TriadTime::timeEnvelope, 0);
        generator->rate (false);
        require (generator->getTaste().countDisliked() == 1 && ! (t.getShape (TriadTime::timeEnvelope, 0) == ratedAway),
                 "The red X rates it as disliked and draws a new one");
        require (generator->getLikeButton().isShowing() || ! editor->isShowing(), "The check and the X are under the dice");
        generator->forgetTaste();
        generator->setTasteKey ("time");
        generator->setSeed (5);
        generator->generate();
        const auto generated = t.getShape (TriadTime::timeEnvelope, 0);
        require (generated.size() >= 2, "The dice draws a new TIME pattern");
        require (t.canUndo (TriadTime::timeEnvelope), "...as an undo step");
        // The picture: Thomas's ShaperBox 3 pattern, point for point, with the first point held as if being dragged.
        t.setShape (TriadTime::timeEnvelope, 0, { { 0.0f, 1.0f, 0.0f }, { 0.125f, 0.9375f, 0.0f }, { 0.125f, 1.0f, 0.0f }, { 0.25f, 0.9375f, 0.0f },
                                                  { 0.375f, 0.75f, 0.0f }, { 0.375f, 1.0f, 0.0f }, { 0.625f, 1.0f, 0.0f }, { 0.75f, 0.75f, 0.0f },
                                                  { 0.75f, 0.875f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, false);
        auto& grid = shapes->getEditor();
        grid.reload();
        {
            // The pitch labels setting (Triad Time's gear and Triad's gear share it): while dragging by default; Off; Always.
            // An open window picks a change up within half a second; allow 5 s on a busy machine.
            juce::SharedResourcePointer<TriadSettings> settings;
            const auto pickedUp = [&grid] (int mode)
            {
                const auto start = juce::Time::getMillisecondCounter();
                while (grid.getHost()->segmentLabelMode() != mode && juce::Time::getMillisecondCounter() - start < 5000)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                return grid.getHost()->segmentLabelMode() == mode;
            };
            settings->setPitchLabels (TriadSettings::PitchLabels::dragging);
            require (pickedUp (1), "TIME labels show while dragging a point (the default)");
            settings->setPitchLabels (TriadSettings::PitchLabels::off);
            require (pickedUp (0), "...the setting turns them off (picked up by an open window)");
            settings->setPitchLabels (TriadSettings::PitchLabels::always);
            require (pickedUp (2), "...or shows them on every line");
        }
        const auto held = grid.toScreen (grid.getShape()[1]);
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent press (juce::Desktop::getInstance().getMainMouseSource(), held, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &grid, &grid, now, held, now, 1, false);
        grid.mouseDown (press);
        grid.mouseDrag (press);
        require (std::abs (grid.getShape()[1].x - 0.125f) < 1.0e-6f && std::abs (grid.getShape()[1].y - 0.9375f) < 1.0e-6f,
                 "A point snaps to ShaperBox's 1/8 across and -1/16 down");
        if (screenshotFolder != juce::File())
        {
            const auto file = screenshotFolder.getChildFile ("Triad-Time-preview.png");
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
            {
                stream.setPosition (0);
                stream.truncate();
                juce::PNGImageFormat png;
                png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), stream);
                std::cout << "Screenshot: " << file.getFullPathName() << std::endl;
            }
        }
        grid.mouseUp (press);
        // The picture of the new folders: PITCH + CHOPS open, one of its patterns on the grid, every line labelled.
        shapes->setPaletteCategory (ShapePanel::timeFolder + 4);
        shapes->getPaletteButton (2)->onClick();
        grid.reload();
        if (screenshotFolder != juce::File())
        {
            const auto file = screenshotFolder.getChildFile ("Triad-Time-folders.png");
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
            {
                stream.setPosition (0);
                stream.truncate();
                juce::PNGImageFormat png;
                png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), stream);
                std::cout << "Screenshot: " << file.getFullPathName() << std::endl;
            }
        }
        juce::SharedResourcePointer<TriadSettings>()->setPitchLabels (TriadSettings::PitchLabels::dragging);
    }
    // ---- Triad's band MUTE --------------------------------------------------------------------------------------------------
    {
        TriadProcessor p;
        p.prepareToPlay (rate, block);
        require (p.getBandCount() == 3 && ! p.isBandMuted (1), "Bands start unmuted");
        const auto in = noise (48000, 11);
        Head head;
        auto energy = [] (const std::vector<float>& v, size_t from) { double e = 0.0; for (size_t i = from; i < v.size(); ++i) e += (double) v[i] * v[i]; return e; };
        const auto all = run (p, head, in, block);
        for (int b = 0; b < 3; ++b) p.setBandMuted (b, true);
        const auto none = run (p, head, in, block);
        require (energy (none, 4800) < 1.0e-10 * energy (all, 4800), "All three bands muted: silence");
        p.setBandMuted (0, false);
        p.setBandMuted (2, false);
        const auto withoutMid = run (p, head, in, block);
        std::vector<float> difference (all.size());
        for (size_t i = 0; i < all.size(); ++i) difference[i] = withoutMid[i] - all[i];
        const double removed = energy (difference, 4800) / energy (all, 4800), left = energy (withoutMid, 4800) / energy (all, 4800);
        std::cout << "Band 2 (250 Hz - 2.5 kHz) muted: " << 100.0 * removed << "% of the noise's energy taken out, " << 100.0 * left << "% left" << std::endl;
        require (removed > 0.03 && removed < 0.3 && left > 0.6, "Muting the middle band takes only its part out, the rest plays");
        // Adding a band keeps each mute with its band.
        require (p.addBand (100.0f), "Add a band below the first divider");
        require (! p.isBandMuted (0) && ! p.isBandMuted (1) && p.isBandMuted (2) && ! p.isBandMuted (3), "The muted band keeps its mute when a band is added before it");
        require (p.removeBand (0) && p.isBandMuted (1) && ! p.isBandMuted (0), "...and when one is removed");
    }
}
}
