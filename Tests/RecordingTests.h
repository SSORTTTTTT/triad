#pragma once
// Included by Stress.cpp (it uses that file's helpers: say, step, require, pump, settle, Transport, Pool...).
//
// 7. Recording through Triad, the extra-strong live test: a whole vocal session (a pitch corrector in PRE, a
//    vocal chain in three bands at 250 Hz / 2.5 kHz, LFOs and a knob link moving, Triad's window open) played for
//    minutes on an audio thread paced like an audio interface, at 32-128 samples and 44.1-96 kHz, while the
//    project is autosaved every second and someone keeps working in Triad. Then the same with layout changes
//    while recording (undo/redo, loading effects, LIVE, Low Latency...), measuring how long each interrupts.
// 8. Pitch correction with a sung vocal: every pitch corrector installed (the CI Mac installs Graillon 3 Free) on
//    a plain track, in Triad's PRE, and one copy in each band: latency reported vs heard, how well the notes are
//    pulled into tune, how much they wobble, and how far the sound is from the plain track.
#include <complex>

namespace vocal
{
struct Note { int start = 0, end = 0; double hz = 0.0; };   // the steady part of a sung note, and the note aimed at
struct Take { juce::AudioBuffer<float> audio; std::vector<Note> notes; double rate = 48000.0; };

inline double midiHz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
inline juce::String formatSamples (int samples, double rate) { return juce::String (samples) + " samples (" + juce::String (1000.0 * samples / rate, 1) + " ms)"; }

// A man's voice from A2 to G3 (110-196 Hz, the low notes either side of a 250 Hz divider's band 1): held notes,
// each sung up to 40 cents sharp or flat, with vibrato and a slow drift, a vowel's formants, then a breath or an
// "s" between notes.
inline Take sing (double rate, double seconds, int seed)
{
    Take take;
    take.rate = rate;
    const int length = (int) (rate * seconds);
    take.audio.setSize (2, length);
    take.audio.clear();
    juce::Random r (seed);
    const int melody[] { 45, 47, 48, 50, 52, 53, 55, 53, 52, 50, 48, 47 };
    const int noteLength = (int) (0.46 * rate), gap = (int) (0.14 * rate);
    std::vector<double> voiced ((size_t) length, 0.0), unvoiced ((size_t) length, 0.0);
    double phase = 0.0;
    int n = (int) (0.1 * rate), index = 0;
    while (n + noteLength + gap < length)
    {
        const double target = midiHz (melody[index % 12]);
        const double detune = (r.nextDouble() * 2.0 - 1.0) * 40.0;
        for (int i = 0; i < noteLength; ++i)
        {
            const double t = i / rate;
            const double vibrato = t > 0.15 ? 22.0 * std::sin (juce::MathConstants<double>::twoPi * 5.6 * t) : 0.0;
            const double drift = 6.0 * std::sin (juce::MathConstants<double>::twoPi * 0.7 * t + index);
            const double f0 = target * std::pow (2.0, (detune + vibrato + drift) / 1200.0);
            phase += f0 / rate;
            phase -= std::floor (phase);
            double source = 0.0;
            const int harmonics = juce::jmin (40, (int) (7000.0 / f0));
            for (int k = 1; k <= harmonics; ++k) source += std::sin (juce::MathConstants<double>::twoPi * k * phase) / std::pow ((double) k, 1.1);
            const double attack = juce::jmin (1.0, t / 0.025), release = juce::jmin (1.0, (noteLength - i) / rate / 0.04);
            voiced[(size_t) (n + i)] = source * attack * release;
        }
        take.notes.push_back ({ n + (int) (0.09 * rate), n + noteLength - (int) (0.06 * rate), target });
        n += noteLength;
        const bool ess = index % 3 == 2;
        for (int i = 0; i < gap; ++i)
        {
            const double t = i / (double) gap;
            unvoiced[(size_t) (n + i)] = (r.nextDouble() - 0.5) * (ess ? 1.0 : 0.08) * std::sin (juce::MathConstants<double>::pi * t);
        }
        n += gap;
        ++index;
    }
    // The vowel's formants on the voiced sound; the "s" high-passed.
    juce::IIRFilter f1, f2, f3, hiss;
    f1.setCoefficients (juce::IIRCoefficients::makeBandPass (rate, 650.0, 4.0));
    f2.setCoefficients (juce::IIRCoefficients::makeBandPass (rate, 1100.0, 7.0));
    f3.setCoefficients (juce::IIRCoefficients::makeBandPass (rate, 2450.0, 10.0));
    hiss.setCoefficients (juce::IIRCoefficients::makeHighPass (rate, 4500.0));
    std::vector<float> out ((size_t) length);
    float peak = 0.0f;
    for (int i = 0; i < length; ++i)
    {
        const auto v = (float) voiced[(size_t) i];
        const float s = 0.3f * v + 1.0f * f1.processSingleSampleRaw (v) + 0.6f * f2.processSingleSampleRaw (v) + 0.3f * f3.processSingleSampleRaw (v)
                      + hiss.processSingleSampleRaw ((float) unvoiced[(size_t) i]);
        out[(size_t) i] = s;
        peak = juce::jmax (peak, std::abs (s));
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < length; ++i) take.audio.setSample (c, i, out[(size_t) i] * 0.5f / juce::jmax (1.0e-6f, peak));
    return take;
}

// The pitch of a frame in Hz (YIN), or 0 when it is not a clear note.
inline double pitchOf (const float* x, int n, double rate)
{
    const int minLag = (int) (rate / 500.0), maxLag = juce::jmin (n / 2, (int) (rate / 70.0));
    const int w = n - maxLag;
    std::vector<double> cmnd ((size_t) maxLag + 1, 1.0);
    double running = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double d = 0.0;
        for (int i = 0; i < w; ++i) { const double e = (double) x[i] - x[i + lag]; d += e * e; }
        running += d;
        cmnd[(size_t) lag] = running > 1.0e-12 ? d * lag / running : 1.0;
    }
    int best = -1;
    for (int lag = minLag; lag < maxLag; ++lag)
        if (cmnd[(size_t) lag] < 0.12)
        {
            while (lag + 1 < maxLag && cmnd[(size_t) lag + 1] < cmnd[(size_t) lag]) ++lag;
            best = lag;
            break;
        }
    if (best <= minLag || best >= maxLag) return 0.0;
    const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
    const double denominator = a - 2.0 * b + c;
    const double refined = best + (std::abs (denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0);
    return rate / refined;
}
inline double centsFromSemitone (double hz)
{
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    return (midi - std::round (midi)) * 100.0;
}
struct PitchStats { double offCents = 0.0, wobbleCents = 0.0, voiced = 0.0; };
// How far the sung notes are from the nearest semitone (on average), and how much the pitch moves inside a
// held note (its standard deviation), measured in the steady part of each note (shifted by `delay`).
inline PitchStats pitchStats (const juce::AudioBuffer<float>& audio, const std::vector<Note>& notes, int delay, double rate)
{
    PitchStats result;
    constexpr int frame = 2048;
    const int hop = (int) (rate * 0.01);
    double off = 0.0, wobble = 0.0;
    int frames = 0, voicedFrames = 0, notesMeasured = 0;
    std::vector<float> mono ((size_t) frame);
    for (const auto& note : notes)
    {
        std::vector<double> cents;
        for (int start = note.start + delay; start + frame <= note.end + delay && start + frame <= audio.getNumSamples(); start += hop)
        {
            for (int i = 0; i < frame; ++i) mono[(size_t) i] = 0.5f * (audio.getSample (0, start + i) + audio.getSample (1, start + i));
            ++frames;
            const double hz = pitchOf (mono.data(), frame, rate);
            if (hz <= 0.0) continue;
            ++voicedFrames;
            const double c = 1200.0 * std::log2 (hz / note.hz);   // against the note aimed at (octave errors show)
            const double wrapped = c - 1200.0 * std::round (c / 1200.0);
            cents.push_back (wrapped);
            off += std::abs (centsFromSemitone (hz));
        }
        if (cents.size() < 4) continue;
        double mean = 0.0;
        for (auto c : cents) mean += c;
        mean /= (double) cents.size();
        double variance = 0.0;
        for (auto c : cents) variance += (c - mean) * (c - mean);
        wobble += std::sqrt (variance / (double) cents.size());
        ++notesMeasured;
    }
    result.offCents = voicedFrames > 0 ? off / voicedFrames : 0.0;
    result.wobbleCents = notesMeasured > 0 ? wobble / notesMeasured : 0.0;
    result.voiced = frames > 0 ? (double) voicedFrames / frames : 0.0;
    return result;
}
// How much later `out` is than `in`, as heard: the loudness envelopes (1 ms steps) lined up best (pitch
// correction changes the waveform, not when the words come).
inline int heardDelay (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, double rate, double maxMs = 500.0)
{
    const int hop = juce::jmax (1, (int) (rate / 1000.0));
    auto envelope = [hop] (const juce::AudioBuffer<float>& b)
    {
        std::vector<double> e ((size_t) (b.getNumSamples() / hop));
        for (size_t i = 0; i < e.size(); ++i)
        {
            double sum = 0.0;
            for (int k = 0; k < hop; ++k) { const double v = b.getSample (0, (int) i * hop + k); sum += v * v; }
            e[i] = std::sqrt (sum / hop);
        }
        double mean = 0.0;
        for (auto v : e) mean += v;
        mean /= juce::jmax ((size_t) 1, e.size());
        for (auto& v : e) v -= mean;
        return e;
    };
    const auto a = envelope (in), b = envelope (out);
    int best = 0;
    double bestScore = -1.0e300;
    for (int lag = 0; lag <= (int) maxMs && lag < (int) b.size(); ++lag)
    {
        double score = 0.0;
        for (size_t i = 0; i + (size_t) lag < b.size() && i < a.size(); ++i) score += a[i] * b[i + (size_t) lag];
        if (score > bestScore) { bestScore = score; best = lag; }
    }
    return best * hop;
}
// How different two takes sound (dB, phase ignored): the average difference of their spectra in the held notes,
// from 80 Hz to 8 kHz, each take lined up by its own delay.
inline double spectralDistanceDb (const juce::AudioBuffer<float>& a, int delayA, const juce::AudioBuffer<float>& b, int delayB,
                                  const std::vector<Note>& notes, double rate)
{
    constexpr int size = 4096;
    auto spectrum = [&rate] (const juce::AudioBuffer<float>& audio, int start)
    {
        std::vector<std::complex<float>> data ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
            const int at = start + i;
            data[(size_t) i] = { at < audio.getNumSamples() ? w * 0.5f * (audio.getSample (0, at) + audio.getSample (1, at)) : 0.0f, 0.0f };
        }
        triad::fft (data);
        std::vector<double> dB;
        const int lo = (int) (80.0 * size / rate), hi = (int) (8000.0 * size / rate);
        for (int k = lo; k <= hi; ++k) dB.push_back (20.0 * std::log10 (std::abs (data[(size_t) k]) + 1.0e-9));
        return dB;
    };
    double total = 0.0;
    int count = 0;
    for (const auto& note : notes)
    {
        const int centre = (note.start + note.end) / 2 - size / 2;
        if (centre + delayA + size > a.getNumSamples() || centre + delayB + size > b.getNumSamples()) continue;
        const auto sa = spectrum (a, centre + delayA), sb = spectrum (b, centre + delayB);
        double top = -1.0e9;
        for (auto v : sa) top = juce::jmax (top, v);
        for (size_t k = 0; k < sa.size(); ++k)
            if (sa[k] > top - 50.0) { total += std::abs (sa[k] - juce::jmax (sb[k], top - 80.0)); ++count; }
    }
    return count > 0 ? total / count : 0.0;
}
// The take through any processor (a plug-in on a plain track, or Triad), in blocks like a DAW.
inline juce::AudioBuffer<float> render (juce::AudioProcessor& processor, const juce::AudioBuffer<float>& input, int block)
{
    juce::AudioBuffer<float> out (2, input.getNumSamples());
    out.clear();
    const int channels = juce::jmax (2, processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    juce::AudioBuffer<float> audio (channels, block);
    juce::MidiBuffer midi;
    for (int start = 0; start + block <= input.getNumSamples(); start += block)
    {
        audio.clear();
        for (int c = 0; c < 2; ++c) audio.copyFrom (c, 0, input, c, start, block);
        processor.processBlock (audio, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, start, audio, c, 0, block);
    }
    return out;
}
inline double nullDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int skip)
{
    double difference = 0.0, energy = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int i = skip; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
        {
            const double x = a.getSample (c, i), y = b.getSample (c, i);
            difference += (x - y) * (x - y);
            energy += x * x;
        }
    return 10.0 * std::log10 ((difference + 1.0e-20) / (energy + 1.0e-20));
}
}

inline bool isPitchCorrector (const juce::String& name)
{
    const auto n = name.toLowerCase();
    if (n.contains ("aupitch") || n.contains ("aunewpitch")) return false;   // Apple's pitch shifters
    return n.contains ("graillon") || n.contains ("autopitch") || n.contains ("auto-tune") || n.contains ("autotune")
        || n.contains ("auto tune") || n.contains ("pitch correct") || n.contains ("melodyne");
}
// Full pitch correction where the corrector has a knob for it (its own default otherwise; the knobs are listed).
inline juce::StringArray configureCorrector (juce::AudioProcessor& fx)
{
    juce::StringArray notes;
    for (auto* p : fx.getParameters())
    {
        const auto name = p->getName (64), lower = name.toLowerCase();
        if (lower.contains ("correction") && (lower.contains ("amount") || lower.contains ("strength") || lower.contains ("amt")))
        {
            p->setValue (1.0f);
            notes.add (name + " = 100%");
        }
    }
    return notes;
}

namespace recording
{
struct Result
{
    int blocks = 0, late = 0, paused = 0, silenced = 0, dropouts = 0, invalid = 0;
    double average = 0.0, worstCpu = 0.0, p999 = 0.0;
    bool realtime = false;
    struct Action { int count = 0, silencedBlocks = 0, dropoutBlocks = 0, worst = 0; };
    std::map<juce::String, Action> actions;
};
// The audio thread: the vocal take looped through Triad, one block every block/rate seconds like an audio
// interface. Per block: Triad's CPU time, and whether the output fell silent while the voice was there (a dropout,
// for whatever reason), or was not a number.
class Player final : public juce::Thread
{
public:
    Player (TriadProcessor& proc, Transport& t, const juce::AudioBuffer<float>& vocal, double r, int b, double secs)
        : Thread ("Triad recording test"), p (proc), transport (t), take (vocal), rate (r), block (b), seconds (secs) {}
    Result result;
    std::atomic<int> dropouts { 0 };
    void run() override
    {
        juce::AudioBuffer<float> audio (2, block);
        juce::MidiBuffer midi;
        const double period = 1000.0 * block / rate;
        const int count = (int) (rate * seconds / block), warmUp = (int) (rate / block);   // the first second: effects' latency
        std::vector<double> times;
        times.reserve ((size_t) count);
        double next = juce::Time::getMillisecondCounterHiRes();
        int position = 0;
        for (int i = 0; i < count && ! threadShouldExit(); ++i)
        {
            float inputPeak = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < block; ++s)
                {
                    const float v = take.getSample (c, (position + s) % take.getNumSamples());
                    audio.setSample (c, s, v);
                    inputPeak = juce::jmax (inputPeak, std::abs (v));
                }
            position = (position + block) % take.getNumSamples();
            const auto c0 = live::threadCpuMs();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p.processBlock (audio, midi);
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto cpu = live::threadCpuMs() - c0;
            transport.advance (block, rate);
            bool finite = true, silent = true;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < block; ++s)
                {
                    const float v = audio.getSample (c, s);
                    if (! std::isfinite (v)) finite = false;
                    if (std::abs (v) > 0.0f) silent = false;
                }
            if (! finite) ++result.invalid;
            if (silent && inputPeak > 0.01f && i > warmUp) ++dropouts;
            times.push_back (cpu);
            if (cpu > period) ++result.late;
            else if (dt > period) ++result.paused;
            next += period;
            for (auto now = juce::Time::getMillisecondCounterHiRes(); now < next; now = juce::Time::getMillisecondCounterHiRes())
            {
                if (next - now > 1.5) juce::Thread::sleep (1);
                else std::this_thread::yield();
            }
            if (juce::Time::getMillisecondCounterHiRes() > next + 4.0 * period) next = juce::Time::getMillisecondCounterHiRes();
        }
        result.blocks = (int) times.size();
        if (times.empty()) return;
        double total = 0.0;
        for (auto t : times) { total += t; result.worstCpu = juce::jmax (result.worstCpu, t); }
        result.average = total / (double) times.size();
        std::sort (times.begin(), times.end());
        result.p999 = times[juce::jmin (times.size() - 1, (size_t) ((double) times.size() * 0.999))];
    }
private:
    TriadProcessor& p;
    Transport& transport;
    const juce::AudioBuffer<float>& take;
    double rate;
    int block;
    double seconds;
};
// Plays the take through Triad for `seconds`, saving the project every second (a DAW's autosave) and doing
// `action` on the message thread every `everyMs` (what the singer or engineer does meanwhile). Each action is
// charged with the blocks Triad silenced and the dropouts until the next one.
inline Result record (TriadProcessor& p, Transport& transport, const juce::AudioBuffer<float>& take, double rate, int block, double seconds,
                      double everyMs, const std::function<juce::String()>& action)
{
    Player player (p, transport, take, rate, block, seconds);
    const int silencedBefore = p.getSilencedBlockCount();
    const auto options = juce::Thread::RealtimeOptions().withPriority (10).withApproximateAudioProcessingTime (block, rate)
                                                        .withPeriodMs (1000.0 * block / rate);
    const bool realtime = player.startRealtimeThread (options);
    if (! realtime) player.startThread (juce::Thread::Priority::highest);
    Result result;
    auto lastSave = juce::Time::getMillisecondCounterHiRes(), lastAction = lastSave;
    juce::String pending;
    int pendingSilenced = 0, pendingDropouts = 0;
    auto charge = [&]
    {
        if (pending.isEmpty()) return;
        auto& a = result.actions[pending];
        const int silenced = p.getSilencedBlockCount() - pendingSilenced, dropped = player.dropouts.load() - pendingDropouts;
        ++a.count;
        a.silencedBlocks += silenced;
        a.dropoutBlocks += dropped;
        a.worst = juce::jmax (a.worst, juce::jmax (silenced, dropped));
    };
    while (player.isThreadRunning())
    {
        pump (5);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastSave > 1000.0)
        {
            juce::MemoryBlock state;
            p.getStateInformation (state);
            lastSave = now;
        }
        if (action != nullptr && now - lastAction > everyMs)
        {
            charge();
            pendingSilenced = p.getSilencedBlockCount();
            pendingDropouts = player.dropouts.load();
            pending = action();
            lastAction = juce::Time::getMillisecondCounterHiRes();
        }
    }
    player.stopThread (4000);
    charge();
    auto actions = std::move (result.actions);
    result = player.result;
    result.actions = std::move (actions);
    result.realtime = realtime;
    result.silenced = p.getSilencedBlockCount() - silencedBefore;
    result.dropouts = player.dropouts.load();
    return result;
}
}

// Zero-latency effects for a vocal chain (dynamics, EQ, saturation, delay, reverb first; no gates).
inline std::vector<size_t> pickVocalChain (const Pool& pool, size_t wanted)
{
    const juce::StringArray kinds { "Dynamics", "Compressor", "EQ", "MultiTool", "Tape", "Centaur", "TIME-12", "Delay", "Plate", "Reverb", "Room" };
    std::vector<size_t> order;
    for (const auto& kind : kinds)
        for (size_t i = 0; i < pool.effects.size(); ++i)
            if (pool.names[(int) i].containsIgnoreCase (kind) && std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
    for (size_t i = 0; i < pool.effects.size(); ++i)
        if (std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
    std::vector<size_t> chain;
    TriadProcessor probe;
    probe.prepareToPlay (48000.0, 64);
    for (size_t i : order)
    {
        if (chain.size() >= wanted) break;
        const auto& name = pool.names[(int) i];
        if (isPitchCorrector (name) || name.containsIgnoreCase ("gate") || name.containsIgnoreCase ("pitch")) continue;
        if (probe.loadEffect (0, 0, pool.effects[i], false).isNotEmpty()) continue;
        settle (probe);
        if (probe.getSlotInfo (0, 0).latency == 0) chain.push_back (i);
        probe.removeEffect (0, 0);
        settle (probe);
    }
    return chain;
}
inline int findCorrector (const Pool& pool)
{
    int found = -1;
    for (int i = 0; i < pool.names.size(); ++i)
        if (isPitchCorrector (pool.names[i]) && (found < 0 || pool.effects[(size_t) i].pluginFormatName == "VST3")) found = i;
    return found;
}
inline void setDividers (TriadProcessor& p, float low, float high, int order)
{
    for (int j = 0; j < 2 && j + 1 < p.getBandCount(); ++j)
        if (auto* c = p.crossoverParameter (j)) c->setValueNotifyingHost (c->convertTo0to1 (j == 0 ? low : high));
    p.setAllCrossoverSlopes (order);
}

// ---- 7. Recording through Triad: the extra-strong live test ---------------------------------------------------------
void recordingStress (const Pool& pool, bool real)
{
    say ("\n== 7. Recording through Triad: the extra-strong live test ==");
    step ("recording: choosing the session's effects", 600);
    const auto chain = pickVocalChain (pool, 5);
    const int corrector = findCorrector (pool);
    juce::StringArray chainNames;
    for (auto i : chain) chainNames.add (pool.names[(int) i]);
    say ("The session: " + (corrector >= 0 ? pool.names[corrector] + " in PRE" : juce::String ("no pitch corrector installed (PRE empty)"))
         + "; 3 bands at 250 Hz / 2.5 kHz (24 dB/oct) with " + (chainNames.isEmpty() ? juce::String ("no effects") : chainNames.joinIntoString (", "))
         + "; every LFO on (band volumes as a 60% tremolo) and an LFO linked to a knob inside an effect; Triad's window open; the project saved every second.");
    const bool display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr;
    juce::Random random (31);
    auto build = [&] (double rate, int block)
    {
        auto p = std::make_unique<TriadProcessor>();
        p->prepareToPlay (rate, block);
        setDividers (*p, 250.0f, 2500.0f, triad::CrossoverFilter::defaultOrder);
        if (corrector >= 0) require (p->loadEffect (TriadProcessor::preBand, 0, pool.effects[(size_t) corrector], false).isEmpty(), "Load the pitch corrector into PRE");
        for (size_t e = 0; e < chain.size(); ++e)
        {
            const int band = e == 0 ? 0 : (e < 3 ? 1 : 2);
            const int slot = e == 0 ? 0 : (e < 3 ? (int) e - 1 : (int) e - 3);
            require (p->loadEffect (band, slot, pool.effects[chain[e]], false).isEmpty(), "Load the vocal chain");
        }
        settle (*p);
        if (corrector >= 0)
            if (auto* fx = p->getHostedEffect (TriadProcessor::preBand, 0)) configureCorrector (*fx);
        activateAllLfos (*p, random);
        // The band-volume LFOs as a 60% tremolo: a band never goes fully quiet, so a silent block while the voice
        // is there is a real dropout, not three LFOs at the bottom of their shapes at once.
        for (int b = 0; b < p->getBandCount(); ++b)
            for (int r = 0; r < p->getRouteCount (b); ++r)
                if (p->getRouteInfo (b, r).destination.kind == TriadProcessor::Destination::bandVolume) p->setRouteAmount (b, r, 0.6f);
        // An LFO on a knob inside the first effect in band 2 (or band 1).
        for (int band : { 1, 0 })
            if (const auto knobs = p->getEffectParameters (band, 0); ! knobs.empty())
            {
                const int lfo = p->addLfo (band);
                p->setLfoShape (band, lfo, triad::mod::preset (3), false);
                p->addRoute (band, lfo, knobs.front().first);
                break;
            }
        return p;
    };
    // What someone does in Triad while recording, without changing the layout.
    auto calm = [&random] (TriadProcessor& p, TriadEditor* e) -> juce::String
    {
        const int band = random.nextInt (p.getBandCount());
        const int slot = random.nextInt (juce::jmax (1, p.getSlotCount (band)));
        switch (random.nextInt (9))
        {
            case 0: p.setSlotMix (band, slot, 40.0f + random.nextFloat() * 60.0f); return "turn a slot's mix knob";
            case 1:
                if (auto* gain = p.parameters.getParameter (TriadProcessor::bandGainID (band)))
                { gain->beginChangeGesture(); gain->setValueNotifyingHost (0.6f + random.nextFloat() * 0.3f); gain->endChangeGesture(); }
                return "move a band's volume";
            case 2:
                if (auto* c = p.crossoverParameter (random.nextInt (2)))
                { c->beginChangeGesture(); c->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, c->getValue() + (random.nextFloat() - 0.5f) * 0.02f)); c->endChangeGesture(); }
                return "drag a divider a little";
            case 3:
                p.setLfoShape (band, random.nextInt (juce::jmax (1, p.getLfoCount (band))), triad::mod::preset (random.nextInt (triad::mod::numPresets())), true);
                return "draw an LFO";
            case 4:
                if (e != nullptr) e->setTab ((TriadEditor::Tab) random.nextInt (3));
                return "switch tabs";
            case 5:
                if (random.nextBool()) p.showEffect (band, slot); else p.closeWindows();
                return "open or close an effect's window";
            case 6:
                if (auto* fx = p.getHostedEffect (band, slot); fx != nullptr && ! fx->getParameters().isEmpty())
                {
                    auto* knob = fx->getParameters()[random.nextInt (juce::jmin (8, fx->getParameters().size()))];
                    knob->beginChangeGesture();
                    knob->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, knob->getValue() + (random.nextFloat() - 0.5f) * 0.1f));
                    knob->endChangeGesture();
                }
                return "turn a knob in an effect's window";
            case 7: p.commitHistory(); return "keep an undo step (every effect's settings)";
            default:
                if (e != nullptr) { e->setTab (TriadEditor::Tab::bands); e->togglePre(); }
                return "show or hide PRE";
        }
    };
    struct Run { double rate; int block; double seconds; };
    std::vector<Run> runs;
    if (real) runs = { { 48000.0, 32, 30.0 }, { 48000.0, 64, 60.0 }, { 48000.0, 128, 30.0 }, { 44100.0, 64, 20.0 }, { 96000.0, 64, 20.0 } };
    else runs = { { 48000.0, 32, 6.0 }, { 48000.0, 64, 8.0 }, { 48000.0, 128, 6.0 }, { 44100.0, 64, 5.0 }, { 96000.0, 64, 5.0 } };
    say ("(a) Monitoring while you work: a knob turned, an LFO drawn, a window opened, an undo step kept... every 150 ms."
         " \"Silenced\": blocks Triad output as silence because its window was changing the bands at that moment;"
         " \"dropouts\": output blocks that were silent while the voice was there, for any reason; \"late\": Triad's work took longer than the buffer lasts.");
    for (const auto& run : runs)
    {
        step ("recording: " + juce::String (run.rate / 1000.0, 1) + " kHz, " + juce::String (run.block) + " samples", run.seconds + 300.0);
        auto p = build (run.rate, run.block);
        Transport transport;
        p->setPlayHead (&transport);
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        if (display) { editor.reset (p->createEditor()); editor->setVisible (true); }
        auto* e = dynamic_cast<TriadEditor*> (editor.get());
        const auto take = vocal::sing (run.rate, 8.0, 5);
        pump (300);
        const auto r = recording::record (*p, transport, take.audio, run.rate, run.block, run.seconds, 150.0, [&] { return calm (*p, e); });
        const double budget = 1000.0 * run.block / run.rate;
        int actions = 0;
        for (const auto& [name, a] : r.actions) actions += a.count;
        say ("  " + juce::String (run.rate / 1000.0, 1) + " kHz, " + juce::String (run.block).paddedLeft (' ', 3) + " samples (" + juce::String (budget, 2) + " ms), "
             + juce::String (run.seconds, 0) + " s, " + juce::String (actions) + " actions: Triad's work average " + juce::String (juce::roundToInt (r.average * 1000.0))
             + " us (" + juce::String (100.0 * r.average / budget, 1) + "% of the time), 99.9% under " + juce::String (juce::roundToInt (r.p999 * 1000.0))
             + " us; late " + juce::String (r.late) + " of " + juce::String (r.blocks) + ", paused by the system " + juce::String (r.paused)
             + "; silenced " + juce::String (r.silenced) + ", dropouts " + juce::String (r.dropouts) + ", not-a-number " + juce::String (r.invalid)
             + "; Triad latency " + vocal::formatSamples (p->getLatencySamples(), run.rate) + (r.realtime ? "" : " (normal-priority thread)"));
        require (r.invalid == 0, "No NaN/Inf while recording");
        require (r.silenced == 0, "No dropouts from working in Triad while recording (knobs, LFOs, windows, undo steps, autosave)");
        // (With real plug-ins a gate or expander in a chain may legitimately go silent in a breath: reported only.)
        if (! real) require (r.dropouts == 0, "The voice never drops out while recording and working in Triad");
        editor.reset();
        p->setPlayHead (nullptr);
        p.reset();
        pump (50);
    }
    // (b) Layout changes while recording: each is charged with the blocks it silenced.
    {
        const Run run { 48000.0, 64, real ? 60.0 : 12.0 };
        step ("recording: layout changes while recording", run.seconds + 600.0);
        auto p = build (run.rate, run.block);
        Transport transport;
        p->setPlayHead (&transport);
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        if (display) { editor.reset (p->createEditor()); editor->setVisible (true); }
        auto* e = dynamic_cast<TriadEditor*> (editor.get());
        juce::AudioProcessorParameter* lowLatency = nullptr;
        if (auto* fx = p->getHostedEffect (TriadProcessor::preBand, 0))
            for (auto* parameter : fx->getParameters())
                if (parameter->getName (64).containsIgnoreCase ("latency")) { lowLatency = parameter; break; }
        auto structural = [&] () -> juce::String
        {
            switch (random.nextInt (8))
            {
                case 0:
                {
                    const int band = random.nextInt (p->getBandCount());
                    if (p->getSlotCount (band) < 4 && ! chain.empty())
                    {
                        p->loadEffect (band, p->getSlotCount (band), pool.effects[chain[(size_t) random.nextInt ((int) chain.size())]], false);
                        return "load an effect into a new slot";
                    }
                    p->removeSlot (band, p->getSlotCount (band) - 1);
                    return "remove an effect";
                }
                case 1: if (e != nullptr) e->undo(); else p->undo(); return "undo";
                case 2: if (e != nullptr) e->redo(); else p->redo(); return "redo";
                case 3:
                {
                    const int band = random.nextInt (p->getBandCount());
                    if (p->getSlotCount (band) > 1) p->moveSlotTo (band, 0, p->getSlotCount (band) - 1);
                    return "drag an effect to another slot";
                }
                case 4: p->setLiveMode (! p->isLiveMode()); return "LIVE on or off";
                case 5:
                {
                    const int orders[] { 16, 4, 1 };
                    p->setAllCrossoverSlopes (orders[random.nextInt (3)]);
                    return "all dividers: hard wall / default / soft";
                }
                case 6:
                    if (lowLatency != nullptr)
                    {
                        lowLatency->beginChangeGesture();
                        lowLatency->setValueNotifyingHost (lowLatency->getValue() < 0.5f ? 1.0f : 0.0f);
                        lowLatency->endChangeGesture();
                        return "pitch corrector: its latency switch";
                    }
                    p->setSlotEnabled (1, 0, ! p->getSlotInfo (1, 0).enabled);
                    return "bypass / enable an effect";
                default:
                    if (p->getBandCount() < 4) p->addBand (5000.0f); else p->removeBand (3);
                    return "add / remove a band";
            }
        };
        const auto take = vocal::sing (run.rate, 8.0, 6);
        pump (300);
        p->commitHistory();
        const auto r = recording::record (*p, transport, take.audio, run.rate, run.block, run.seconds, 400.0, structural);
        say ("(b) Layout changes while recording (48 kHz, 64 samples, one every 400 ms for " + juce::String (run.seconds, 0) + " s): late " + juce::String (r.late)
             + " of " + juce::String (r.blocks) + ", not-a-number " + juce::String (r.invalid) + ". How long each kind of change interrupted the sound:");
        const double blockMs = 1000.0 * run.block / run.rate;
        for (const auto& [name, a] : r.actions)
            say ("  " + name + " (x" + juce::String (a.count) + "): Triad silenced " + juce::String (a.silencedBlocks * blockMs / juce::jmax (1, a.count), 1)
                 + " ms on average, the sound dropped out " + juce::String (a.dropoutBlocks * blockMs / juce::jmax (1, a.count), 1) + " ms on average, worst "
                 + juce::String (a.worst * blockMs, 1) + " ms");
        require (r.invalid == 0, "No NaN/Inf during layout changes while recording");
        if (! real) require (r.dropouts <= r.silenced, "Layout changes while recording: the only gaps are the blocks Triad silenced to swap the bands");
        editor.reset();
        p->setPlayHead (nullptr);
        p.reset();
        pump (100);
    }
}

// ---- 8. Pitch correction with a sung vocal ----------------------------------------------------------------------
void pitchCorrection (const Pool& pool)
{
    say ("\n== 8. Pitch correction with a sung vocal: a plain track, Triad's PRE, and a copy in each band (250 Hz / 2.5 kHz) ==");
    std::vector<size_t> correctors;
    for (size_t i = 0; i < pool.effects.size(); ++i) if (isPitchCorrector (pool.names[(int) i])) correctors.push_back (i);
    if (correctors.empty())
    {
        say ("No pitch corrector is installed on this computer (the CI Mac installs Graillon 3 Free for this): skipped");
        return;
    }
    constexpr double rate = 48000.0;
    constexpr int block = 128;
    const auto take = vocal::sing (rate, 12.0, 11);
    const auto sung = vocal::pitchStats (take.audio, take.notes, 0, rate);
    say ("The sung take: " + juce::String ((int) take.notes.size()) + " held notes from A2 to G3 (110-196 Hz), each up to 40 cents off, with vibrato,"
         " breaths and \"s\" sounds between them; measured: " + juce::String (sung.offCents, 1) + " cents from the nearest note on average, wobble "
         + juce::String (sung.wobbleCents, 1) + " cents inside a note (vibrato)");
    juce::AudioPluginFormatManager formats;
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC && JUCE_PLUGINHOST_AU
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    for (auto index : correctors)
    {
        const auto& description = pool.effects[index];
        const auto& name = pool.names[(int) index];
        step ("pitch correction: " + name, 600);
        // On a plain track, as a DAW hosts it.
        auto makePlain = [&]() -> std::unique_ptr<juce::AudioPluginInstance>
        {
            juce::String error;
            auto fx = formats.createPluginInstance (description, rate, block, error);
            if (fx == nullptr) return nullptr;
            fx->disableNonMainBuses();
            auto layout = fx->getBusesLayout();
            if (! layout.inputBuses.isEmpty()) layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            fx->setBusesLayout (layout);
            configureCorrector (*fx);
            fx->setRateAndBufferSizeDetails (rate, block);
            fx->prepareToPlay (rate, block);
            return fx;
        };
        auto plain = makePlain();
        if (plain == nullptr) { say ("  " + name + ": could not be opened on a plain track; skipped"); continue; }
        {
            juce::StringArray knobs;
            for (auto* parameter : plain->getParameters()) if (knobs.size() < 40) knobs.add (parameter->getName (40));
            const auto set = configureCorrector (*plain);
            say ("  " + name + ": knobs " + knobs.joinIntoString (", ") + (set.isEmpty() ? juce::String (" (left at its defaults)") : "; set " + set.joinIntoString (", ")));
        }
        const auto plainOut = vocal::render (*plain, take.audio, block);
        const int plainReported = plain->getLatencySamples(), plainHeard = vocal::heardDelay (take.audio, plainOut, rate);
        const auto plainPitch = vocal::pitchStats (plainOut, take.notes, plainHeard, rate);
        bool repeatable = false;
        if (auto again = makePlain())
            repeatable = vocal::nullDb (plainOut, vocal::render (*again, take.audio, block), (int) rate / 2) < -60.0;
        say ("    plain track: reports " + vocal::formatSamples (plainReported, rate) + ", heard " + vocal::formatSamples (plainHeard, rate)
             + "; notes " + juce::String (plainPitch.offCents, 1) + " cents off (sung: " + juce::String (sung.offCents, 1) + "), wobble "
             + juce::String (plainPitch.wobbleCents, 1) + " cents (sung: " + juce::String (sung.wobbleCents, 1) + "), clear pitch in "
             + juce::String (juce::roundToInt (100.0 * plainPitch.voiced)) + "% of frames" + (repeatable ? "" : "; it does not sound exactly the same twice"));
        plain.reset();
        struct Config { const char* label; int bands; int order; bool pre; };
        const Config configs[] { { "in Triad, 1 band (same as a plain track)", 1, 4, false },
                                 { "in Triad's PRE, then 3 bands at 24 dB/oct", 3, 4, true },
                                 { "a copy in each of 3 bands, 96 dB/oct", 3, 16, false },
                                 { "a copy in each of 3 bands, 24 dB/oct", 3, 4, false } };
        for (const auto& config : configs)
        {
            TriadProcessor t;
            t.prepareToPlay (rate, block);
            while (t.getBandCount() > config.bands) t.removeBand (t.getBandCount() - 1);
            setDividers (t, 250.0f, 2500.0f, config.order);
            if (config.pre)
                require (t.loadEffect (TriadProcessor::preBand, 0, description, false).isEmpty(), "Load the pitch corrector into PRE");
            else
                for (int b = 0; b < t.getBandCount(); ++b) require (t.loadEffect (b, 0, description, false).isEmpty(), "Load the pitch corrector into a band");
            settle (t);
            for (int b = -1; b < t.getBandCount(); ++b)
                if (auto* fx = t.getHostedEffect (b < 0 ? TriadProcessor::preBand : b, 0)) configureCorrector (*fx);
            pump (200);
            const auto out = vocal::render (t, take.audio, block);
            const int reported = t.getLatencySamples(), heard = vocal::heardDelay (take.audio, out, rate);
            const auto pitch = vocal::pitchStats (out, take.notes, heard, rate);
            const double apart = vocal::spectralDistanceDb (plainOut, plainHeard, out, heard, take.notes, rate);
            juce::String line ("    " + juce::String (config.label) + ": reports " + vocal::formatSamples (reported, rate) + ", heard "
                               + vocal::formatSamples (heard, rate) + "; notes " + juce::String (pitch.offCents, 1) + " cents off, wobble "
                               + juce::String (pitch.wobbleCents, 1) + " cents, clear pitch in " + juce::String (juce::roundToInt (100.0 * pitch.voiced))
                               + "% of frames; " + juce::String (apart, 1) + " dB from the plain track's sound");
            if (config.bands == 1 && repeatable)
            {
                const double difference = vocal::nullDb (plainOut, out, (int) rate / 2);
                line << " (sample for sample: " << juce::String (difference, 0) << " dB)";
                require (difference < -50.0, "In Triad (1 band) the pitch corrector sounds exactly as on a plain track");
            }
            say (line);
            require (reported == plainReported, "Triad reports the pitch corrector's own latency, nothing added (" + juce::String (config.label) + ")");
            if (config.bands == 1 || config.pre)
                require (std::abs (heard - plainHeard) <= (int) (0.002 * rate), "The voice is heard exactly as late through Triad as on a plain track ("
                                                                                 + juce::String (config.label) + ")");
        }
    }
}
