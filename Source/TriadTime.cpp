#include "TriadTime.h"
#include "ShapePanel.h"
#include "Theme.h"
#include "TriadIdentity.h"

namespace mod = triad::mod;
using namespace triad::ui;

// ---- Patterns ---------------------------------------------------------------------------------------------
namespace
{
// TIME: y = 1 is live, y = 0 one pattern length back (as ShaperBox's "As LFO Length" and Gross Beat); one beat further back
// is 1 / (length in beats) lower. Presets are drawn for 1 bar.
constexpr float beatBack = 1.0f / 4.0f;
mod::Shape points (std::initializer_list<mod::Point> list) { mod::Shape s (list); mod::normalise (s); return s; }
// A staircase that repeats the piece starting at `from` (x) every `step` (x), each repeat `back` lower.
mod::Shape repeats (float from, float step, float back)
{
    mod::Shape s { { 0.0f, 1.0f, 0.0f } };
    float y = 1.0f;
    for (float x = from + step; x < 0.9999f; x += step)
    {
        s.push_back ({ x, y, 0.0f });
        y -= back;
        s.push_back ({ x, y, 0.0f });
    }
    s.push_back ({ 1.0f, y, 0.0f });
    mod::normalise (s);
    return s;
}
// VOLUME: on/off steps from a pattern string ('x' on, '.' off), each character an equal part of the bar.
mod::Shape gates (const char* pattern)
{
    const int n = (int) std::strlen (pattern);
    mod::Shape s;
    for (int i = 0; i < n; ++i)
    {
        const float y = pattern[i] == 'x' ? 1.0f : 0.0f;
        const float x0 = (float) i / (float) n, x1 = (float) (i + 1) / (float) n;
        if (s.empty() || ! juce::exactlyEqual (s.back().y, y)) { if (! s.empty()) s.push_back ({ x0, s.back().y, 0.0f }); s.push_back ({ x0, y, 0.0f }); }
        if (i == n - 1) s.push_back ({ x1, y, 0.0f });
    }
    mod::normalise (s);
    return s;
}
// One curved ramp per part: from `from` to `to`, `count` times in the bar.
mod::Shape ramps (int count, float from, float to, float midpoint)
{
    mod::Shape s;
    const float curve = mod::curveForMidpoint (midpoint);
    for (int i = 0; i < count; ++i)
    {
        const float x0 = (float) i / (float) count, x1 = (float) (i + 1) / (float) count;
        s.push_back ({ x0, from, curve });
        s.push_back ({ x1, to, 0.0f });
    }
    mod::normalise (s);
    return s;
}
struct Preset { const char* name; mod::Shape shape; };
const std::array<std::vector<Preset>, 2>& presets()
{
    static const std::array<std::vector<Preset>, 2> list = []
    {
        const float tape = mod::curveForMidpoint (0.25f);   // speed falling evenly to a stop
        std::array<std::vector<Preset>, 2> p;
        p[0] = {
            { "Off (live)", mod::flat (1.0f) },
            { "Half speed", points ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f - 2.0f * beatBack, 0.0f } }) },
            { "Half speed, 2nd half", points ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f - beatBack, 0.0f } }) },
            { "Tape stop, beat 4", points ({ { 0.0f, 1.0f, 0.0f }, { 0.75f, 1.0f, tape }, { 1.0f, 1.0f - 0.5f * beatBack, 0.0f } }) },
            { "Tape stop, whole bar", points ({ { 0.0f, 1.0f, tape }, { 1.0f, 1.0f - 2.0f * beatBack, 0.0f } }) },
            { "Repeat 1/16, beat 4", repeats (0.75f, 1.0f / 16.0f, 0.25f * beatBack) },
            { "Repeat 1/8, beats 3-4", repeats (0.5f, 1.0f / 8.0f, 0.5f * beatBack) },
            { "Repeat beat 1", repeats (0.0f, 0.25f, beatBack) },
            { "Reverse, beat 4", points ({ { 0.0f, 1.0f, 0.0f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f - 2.0f * beatBack, 0.0f } }) },
            { "Reverse, 2nd half", points ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f - 4.0f * beatBack, 0.0f } }) },
            { "Scratch, beats 3-4", points ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f },
                                             { 0.5625f, 0.875f, 0.0f }, { 0.625f, 0.9375f, 0.0f }, { 0.6875f, 0.8125f, 0.0f }, { 0.75f, 0.875f, 0.0f },
                                             { 0.8125f, 0.75f, 0.0f }, { 0.875f, 0.8125f, 0.0f }, { 0.9375f, 0.6875f, 0.0f }, { 1.0f, 0.75f, 0.0f } }) },
            { "Roll 1/32, beat 4", repeats (0.75f, 1.0f / 32.0f, 0.125f * beatBack) },
        };
        p[1] = {
            { "Full", mod::flat (1.0f) },
            { "Gate 1/8", gates ("x.x.x.x.x.x.x.x.") },
            { "Gate 1/16", gates ("x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.") },
            { "Pump 1/4", ramps (4, 0.0f, 1.0f, 0.78f) },
            { "Off-beats 1/8", gates (".x.x.x.x") },
            { "Trance gate", gates ("x.xx.xx.x.xx.x.x") },
            { "Triplet gate", gates ("x.x.x.x.x.x.x.x.x.x.x.x.") },
            { "Cut beat 4", gates ("xxx.") },
            { "Fade out", points ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }) },
            { "Swell 1/4", ramps (4, 0.0f, 1.0f, 0.25f) },
            { "Stutter, beat 4", gates ("xxxxxxxxxxxxxxxxxxxxxxxxx.x.x.x.") },
            { "Half-bar mute", gates ("x.") },
        };
        return p;
    }();
    return list;
}
juce::StringArray lengthLabels()
{
    juce::StringArray labels;
    for (int i : TriadTime::syncedLengths()) labels.add (mod::lengths()[(size_t) i].label);
    return labels;
}
int lengthChoice (int lengthIndex)
{
    const auto& list = TriadTime::syncedLengths();
    const auto it = std::find (list.begin(), list.end(), lengthIndex);
    return it == list.end() ? -1 : (int) std::distance (list.begin(), it);
}
}

juce::String TriadTime::slotName (int envelope, int slot)
{
    const auto& list = presets()[(size_t) juce::jlimit (0, 1, envelope)];
    return slot >= 0 && slot < (int) list.size() ? juce::String (list[(size_t) slot].name) : juce::String();
}
mod::Shape TriadTime::slotPreset (int envelope, int slot)
{
    const auto& list = presets()[(size_t) juce::jlimit (0, 1, envelope)];
    return slot >= 0 && slot < (int) list.size() ? list[(size_t) slot].shape : mod::flat();
}
const std::vector<int>& TriadTime::syncedLengths()
{
    static const std::vector<int> list = []
    {
        std::vector<int> l;
        for (int i = 0; i < (int) mod::lengths().size(); ++i)
            if (mod::lengths()[(size_t) i].column != 3) l.push_back (i);
        return l;
    }();
    return list;
}
bool TriadTime::isTimeLength (int i)
{
    if (i < 0 || i >= (int) mod::lengths().size() || mod::lengths()[(size_t) i].column == 3) return false;
    const auto beats = mod::lengthInBeats (i, 4.0);
    return beats >= 0.25 && beats <= 8.0;
}
bool TriadTime::isVolumeLength (int i)
{
    return i >= 0 && i < (int) mod::lengths().size() && mod::lengths()[(size_t) i].column != 3;
}

juce::PluginDescription TriadTime::makeDescription()
{
    juce::PluginDescription d;
    d.name = "Triad Time";
    d.descriptiveName = "Triad Time (time and volume gate)";
    d.pluginFormatName = formatName;
    d.category = "Fx";
    d.manufacturerName = "Thomas Ross";
    d.version = TRIAD_VERSION;
    d.fileOrIdentifier = identifier;
    d.uniqueId = d.deprecatedUid = 0x5472546d;   // 'TrTm'
    d.isInstrument = false;
    d.numInputChannels = d.numOutputChannels = 2;
    return d;
}

// ---- Processor ----------------------------------------------------------------------------------------------
TriadTime::TriadTime()
    : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    using namespace juce;
    const int bar = lengthChoice (mod::defaultLength());
    auto addInt = [this] (const char* id, const char* name, int initial)
    {
        auto p = std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, 1, numSlots, initial);
        auto* raw = p.get();
        addHostedParameter (std::move (p));
        return raw;
    };
    auto addFloat = [this] (const char* id, const char* name, NormalisableRange<float> range, float initial, const char* unit)
    {
        auto p = std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, initial, AudioParameterFloatAttributes().withLabel (unit));
        auto* raw = p.get();
        addHostedParameter (std::move (p));
        return raw;
    };
    auto addChoice = [this, bar] (const char* id, const char* name)
    {
        auto p = std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, lengthLabels(), juce::jmax (0, bar));
        auto* raw = p.get();
        addHostedParameter (std::move (p));
        return raw;
    };
    timeSlot = addInt ("timePattern", "Time pattern", 1);
    volumeSlot = addInt ("volumePattern", "Volume pattern", 1);
    timeLength = addChoice ("timeLength", "Time length");
    volumeLength = addChoice ("volumeLength", "Volume length");
    timeMix = addFloat ("timeMix", "Time mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%");
    volumeMix = addFloat ("volumeMix", "Volume mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%");
    smoothing = addFloat ("smoothing", "Smoothing", { 0.0f, 50.0f, 0.1f, 0.5f }, 8.0f, "ms");
    attack = addFloat ("attack", "Attack", { 0.0f, 500.0f, 0.1f, 0.35f }, 2.0f, "ms");
    release = addFloat ("release", "Release", { 0.0f, 1000.0f, 0.1f, 0.35f }, 12.0f, "ms");
    {
        auto p = std::make_unique<AudioParameterBool> (ParameterID { "volumeFirst", 1 }, "VOLUME before TIME", false);
        volumeFirst = p.get();
        addHostedParameter (std::move (p));
    }
    for (int e = 0; e < 2; ++e)
        for (int s = 0; s < numSlots; ++s) shapes[(size_t) e][(size_t) s] = slotPreset (e, s);
    for (auto& p : playing) p.reserve (2048);
}
TriadTime::~TriadTime() { cancelPendingUpdate(); }
void TriadTime::setVolumeFirst (bool first)
{
    if (volumeFirst == nullptr || volumeFirst->get() == first) return;
    volumeFirst->beginChangeGesture();
    volumeFirst->setValueNotifyingHost (first ? 1.0f : 0.0f);
    volumeFirst->endChangeGesture();
}

bool TriadTime::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet(), out = layout.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}
void TriadTime::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    historySize = (int) std::ceil (historySeconds * rate) + 8;
    history.setSize (2, historySize, false, true, false);
    history.clear();
    written = 0;
    previousDelay = -1.0;
    previousSpeed = 1.0;
    fadeLeft = 0;
    gate = 1.0f;
    for (auto& c : cursors) c = {};
    timeMixSmooth.reset (rate, 0.02);
    volumeMixSmooth.reset (rate, 0.02);
    timeMixSmooth.setCurrentAndTargetValue (timeMix->get() * 0.01f);
    volumeMixSmooth.setCurrentAndTargetValue (volumeMix->get() * 0.01f);
    pullShapes (true);
    prepared = true;
}
void TriadTime::releaseResources()
{
    prepared = false;
    history.setSize (2, 0);   // the buffer is the big part (12 s): give it back while not playing
    historySize = 0;
}

void TriadTime::pullShapes (bool force)
{
    const int version = shapeVersion.load();
    const int slots[2] { getSlot (timeEnvelope), getSlot (volumeEnvelope) };
    if (! force && version == playingVersion && slots[0] == playingSlot[0] && slots[1] == playingSlot[1]) return;
    const juce::SpinLock::ScopedTryLockType lock (shapeLock);
    if (! lock.isLocked()) return;   // the editor is changing a shape this very moment: next block
    for (int e = 0; e < 2; ++e)
    {
        const auto& source = shapes[(size_t) e][(size_t) slots[e]];
        auto& target = playing[(size_t) e];
        target.clear();
        const size_t n = juce::jmin (source.size(), target.capacity());
        target.insert (target.end(), source.begin(), source.begin() + (std::ptrdiff_t) n);
        if (target.size() < 2) target = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
        playingSlot[(size_t) e] = slots[e];
        cursors[(size_t) e] = {};
    }
    playingVersion = version;
}

float TriadTime::evaluate (const mod::Shape& s, float x, Cursor& cursor) noexcept
{
    // As mod::evaluate, but carrying on from the segment of the previous sample (one step per sample).
    if (s.empty()) return 1.0f;
    if (x <= s.front().x) { cursor.segment = 0; cursor.lastX = x; return s.front().y; }
    if (x < cursor.lastX || cursor.segment >= s.size()) cursor.segment = 0;
    cursor.lastX = x;
    size_t i = cursor.segment;
    while (i + 1 < s.size() && x >= s[i + 1].x) ++i;
    cursor.segment = i;
    if (i + 1 >= s.size()) return s.back().y;
    const auto& a = s[i];
    const auto& b = s[i + 1];
    const float width = b.x - a.x;
    if (width <= 1.0e-7f) return b.y;
    return a.y + (b.y - a.y) * mod::curveFraction ((x - a.x) / width, a.curve);
}

float TriadTime::readAt (int channel, double position) const noexcept
{
    // position: an absolute sample index (fractional), at most the newest sample written.
    if (position < 0.0 || historySize <= 0) return 0.0f;
    const auto* data = history.getReadPointer (channel);
    const auto newest = (double) (written - 1);
    position = juce::jmin (position, newest);
    const auto i = (juce::int64) std::floor (position);
    const float f = (float) (position - (double) i);
    auto at = [&] (juce::int64 k) { return k < 0 || k > written - 1 || written - 1 - k >= historySize ? 0.0f : data[(size_t) (k % historySize)]; };
    if (f <= 1.0e-6f) return at (i);
    if ((double) i + 2.0 > newest)   // the newest samples: no future ones to interpolate with
        return at (i) + (at (i + 1) - at (i)) * f;
    // 4-point Hermite.
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void TriadTime::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int n = audio.getNumSamples();
    const int channels = juce::jmin (2, audio.getNumChannels(), getTotalNumInputChannels() > 0 ? 2 : 0);
    if (! prepared || historySize <= 0 || channels == 0) return;
    pullShapes (false);
    spectrum.push (audio, n);   // the sound coming in (only while a window shows its spectrum)
    // Where the song is: the DAW's position while it plays, otherwise a clock of our own at the last tempo.
    double beats = freeBeats, bpm = lastBpm, beatsPerBar = 4.0;
    bool playingSong = false;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm(); b.hasValue() && *b > 1.0) bpm = *b;
            if (const auto sig = pos->getTimeSignature(); sig.hasValue() && sig->denominator > 0)
                beatsPerBar = sig->numerator * 4.0 / sig->denominator;
            if (pos->getIsPlaying())
                if (const auto ppq = pos->getPpqPosition(); ppq.hasValue()) { beats = *ppq; playingSong = true; }
        }
    lastBpm = bpm;
    const double beatsPerSample = bpm / 60.0 / rate, samplesPerBeat = rate * 60.0 / bpm;
    const double timeBeats = juce::jmax (0.25, mod::lengthInBeats (getLength (timeEnvelope), beatsPerBar));
    const double volumeBeats = juce::jmax (1.0 / 32.0, mod::lengthInBeats (getLength (volumeEnvelope), beatsPerBar));
    const double depth = timeDepthInBeats (timeBeats) * samplesPerBeat;
    const int smoothSamples = juce::roundToInt (smoothing->get() * 0.001f * rate);
    auto coefficient = [this] (float ms) { return ms <= 0.01f ? 0.0f : (float) std::exp (-1.0 / (ms * 0.001 * rate)); };
    const float rise = coefficient (attack->get()), fall = coefficient (release->get());
    timeMixSmooth.setTargetValue (timeMix->get() * 0.01f);
    volumeMixSmooth.setTargetValue (volumeMix->get() * 0.01f);
    const auto& timeShape = playing[timeEnvelope];
    const auto& volumeShape = playing[volumeEnvelope];
    const auto limit = (double) (historySize - 8);
    float timePhase = 0.0f, volumePhase = 0.0f;
    const int traced = tracedEnvelope.load (std::memory_order_relaxed);
    if (traced != tracing) { tracing = traced; trace.reset(); }
    const bool gateFirst = isVolumeFirst();
    for (int s = 0; s < n; ++s)
    {
        const double at = beats + s * beatsPerSample;
        // VOLUME: the gate, with attack and release (applied after TIME, or first: see below).
        volumePhase = (float) (at / volumeBeats - std::floor (at / volumeBeats));
        const float target = evaluate (volumeShape, volumePhase, cursors[volumeEnvelope]);
        gate = target + (gate - target) * (target > gate ? rise : fall);
        const float tm = timeMixSmooth.getNextValue(), vm = volumeMixSmooth.getNextValue();
        const float gain = 1.0f - vm + vm * gate;
        // Into the rolling buffer: the sound as it comes in, or gated first (VOLUME before TIME: TIME then plays back
        // the gated sound, gaps and all).
        const auto slot = (size_t) (written % historySize);
        float dry[2], level = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            dry[c] = audio.getSample (juce::jmin (c, channels - 1), s);
            level = juce::jmax (level, std::abs (dry[c]));
            if (gateFirst) dry[c] *= gain;
            history.setSample (c, (int) slot, dry[c]);
        }
        ++written;
        const auto newest = (double) (written - 1);
        // TIME: how far back to play.
        timePhase = (float) (at / timeBeats - std::floor (at / timeBeats));
        const float y = evaluate (timeShape, timePhase, cursors[timeEnvelope]);
        const double delay = juce::jlimit (0.0, limit, (1.0 - (double) y) * depth);
        if (previousDelay < 0.0) previousDelay = delay;
        const double change = delay - previousDelay;
        if (std::abs (change) > 8.0)
        {
            // A jump: the head that was playing carries on at its own speed and fades out while the new one fades in.
            if (fadeLeft == 0 && smoothSamples > 0)
            {
                fadePosition = newest - 1.0 - previousDelay + previousSpeed;
                fadeSpeed = previousSpeed;
                fadeLeft = fadeLength = smoothSamples;
            }
            jumps.fetch_add (1, std::memory_order_relaxed);
        }
        else previousSpeed = juce::jlimit (-4.0, 4.0, 1.0 - change);
        previousDelay = delay;
        const double position = newest - delay;
        float wet[2] { readAt (0, position), readAt (1, position) };
        if (fadeLeft > 0)
        {
            const float t = 1.0f - (float) fadeLeft / (float) fadeLength;
            const float in = std::sin (t * juce::MathConstants<float>::halfPi), out = std::cos (t * juce::MathConstants<float>::halfPi);
            for (int c = 0; c < 2; ++c) wet[c] = wet[c] * in + readAt (c, juce::jmin (fadePosition, newest)) * out;
            fadePosition += fadeSpeed;
            --fadeLeft;
        }
        if (traced >= 0) trace.add (traced == timeEnvelope ? timePhase : volumePhase, level);
        const float after = gateFirst ? 1.0f : gain;
        for (int c = 0; c < channels; ++c)
            audio.setSample (c, s, (dry[c] + (wet[c] - dry[c]) * tm) * after);
    }
    if (traced >= 0) trace.endBlock();
    if (! playingSong) freeBeats = beats + n * beatsPerSample;
    phases[timeEnvelope].store (timePhase, std::memory_order_relaxed);
    phases[volumeEnvelope].store (volumePhase, std::memory_order_relaxed);
    clocks[timeEnvelope].set (timePhase, beatsPerSample * rate / timeBeats);
    clocks[volumeEnvelope].set (volumePhase, beatsPerSample * rate / volumeBeats);
    for (int c = channels; c < audio.getNumChannels(); ++c) audio.clear (c, 0, n);
}

void TriadTime::setTracedEnvelope (int envelope)
{
    tracedEnvelope.store (envelope == timeEnvelope || envelope == volumeEnvelope ? envelope : -1);
}
double TriadTime::segmentSpeed (const mod::Point& from, const mod::Point& to)
{
    // How far back = (1 - y) x one length; time passes at one length per x. Speed = 1 - (change in delay / time).
    const double dx = (double) to.x - (double) from.x;
    if (dx <= 1.0e-9) return 1.0;
    return 1.0 + ((double) to.y - (double) from.y) / dx;
}
juce::String TriadTime::pitchText (double speed)
{
    // Which way it plays (FWD / REV) and how far its pitch moves: half speed is -12 st, a quarter -24, double +12.
    if (std::abs (speed - 1.0) < 1.0e-4) return {};          // live or a steady delay: as recorded
    if (std::abs (speed) < 1.0e-4) return "FREEZE";
    const double semitones = 12.0 * std::log2 (std::abs (speed));
    const double whole = std::round (semitones);
    const bool round = std::abs (semitones - whole) < 0.05;
    const double shown = round ? whole : std::round (semitones * 10.0) / 10.0;
    const auto number = round ? juce::String ((int) std::abs (whole)) : juce::String (std::abs (shown), 1);
    const juce::String sign = shown < 0.0 ? juce::String::fromUTF8 ("\xe2\x88\x92") : shown > 0.0 ? juce::String ("+") : juce::String();
    return juce::String (speed < 0.0 ? "REV " : "FWD ") + sign + number + " st";
}

// ---- Envelopes (message thread) ------------------------------------------------------------------------------
mod::Shape TriadTime::getShape (int envelope, int slot) const
{
    const juce::SpinLock::ScopedLockType lock (shapeLock);
    return shapes[(size_t) juce::jlimit (0, 1, envelope)][(size_t) juce::jlimit (0, numSlots - 1, slot)];
}
void TriadTime::setShape (int envelope, int slot, const mod::Shape& shape, bool undoable)
{
    envelope = juce::jlimit (0, 1, envelope);
    slot = juce::jlimit (0, numSlots - 1, slot);
    auto next = shape;
    mod::normalise (next);
    if (next.size() > 2048) next.resize (2048);
    if (undoable)
    {
        undoStack[(size_t) envelope].push_back ({ slot, getShape (envelope, slot) });
        if (undoStack[(size_t) envelope].size() > 100) undoStack[(size_t) envelope].erase (undoStack[(size_t) envelope].begin());
        redoStack[(size_t) envelope].clear();
    }
    {
        const juce::SpinLock::ScopedLockType lock (shapeLock);
        shapes[(size_t) envelope][(size_t) slot] = std::move (next);
    }
    ++shapeVersion;
    triggerAsyncUpdate();   // the DAW (or Triad) learns the effect changed: saved with the song, an undo step
}
void TriadTime::beginEdit (int envelope)
{
    envelope = juce::jlimit (0, 1, envelope);
    const int slot = getSlot (envelope);
    undoStack[(size_t) envelope].push_back ({ slot, getShape (envelope, slot) });
    redoStack[(size_t) envelope].clear();
}
bool TriadTime::undo (int envelope)
{
    auto& from = undoStack[(size_t) juce::jlimit (0, 1, envelope)];
    if (from.empty()) return false;
    const auto [slot, shape] = from.back();
    from.pop_back();
    redoStack[(size_t) envelope].push_back ({ slot, getShape (envelope, slot) });
    setShape (envelope, slot, shape, false);
    if (getSlot (envelope) != slot) setSlot (envelope, slot);
    return true;
}
bool TriadTime::redo (int envelope)
{
    auto& from = redoStack[(size_t) juce::jlimit (0, 1, envelope)];
    if (from.empty()) return false;
    const auto [slot, shape] = from.back();
    from.pop_back();
    undoStack[(size_t) envelope].push_back ({ slot, getShape (envelope, slot) });
    setShape (envelope, slot, shape, false);
    if (getSlot (envelope) != slot) setSlot (envelope, slot);
    return true;
}
int TriadTime::getSlot (int envelope) const
{
    const auto* p = envelope == timeEnvelope ? timeSlot : volumeSlot;
    return juce::jlimit (0, numSlots - 1, p->get() - 1);
}
void TriadTime::setSlot (int envelope, int slot)
{
    auto* p = envelope == timeEnvelope ? timeSlot : volumeSlot;
    p->beginChangeGesture();
    *p = juce::jlimit (1, numSlots, slot + 1);
    p->endChangeGesture();
}
int TriadTime::getLength (int envelope) const
{
    const auto* p = envelope == timeEnvelope ? timeLength : volumeLength;
    const auto& list = syncedLengths();
    return list[(size_t) juce::jlimit (0, (int) list.size() - 1, p->getIndex())];
}
void TriadTime::setLength (int envelope, int lengthIndex)
{
    if (! (envelope == timeEnvelope ? isTimeLength (lengthIndex) : isVolumeLength (lengthIndex))) return;
    const int choice = lengthChoice (lengthIndex);
    if (choice < 0) return;
    auto* p = envelope == timeEnvelope ? timeLength : volumeLength;
    p->beginChangeGesture();
    *p = choice;
    p->endChangeGesture();
}
float TriadTime::getPhase (int envelope) const { return phases[(size_t) juce::jlimit (0, 1, envelope)].load (std::memory_order_relaxed); }
float TriadTime::getPhaseNow (int envelope) const { return getPhase (envelope) < 0.0f ? -1.0f : clocks[(size_t) juce::jlimit (0, 1, envelope)].now(); }
void TriadTime::handleAsyncUpdate() { updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true)); }

// ---- State ----------------------------------------------------------------------------------------------------
void TriadTime::getStateInformation (juce::MemoryBlock& data)
{
    juce::XmlElement root ("TriadTime");
    root.setAttribute ("version", 2);   // 2: the TIME grid reaches one pattern length back (1: two)
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            root.setAttribute (withId->getParameterID(), (double) withId->getValue());
    for (int e = 0; e < 2; ++e)
        for (int s = 0; s < numSlots; ++s)
            if (auto x = mod::toXml (getShape (e, s), e == timeEnvelope ? "Time" : "Volume"))
            {
                x->setAttribute ("slot", s + 1);
                root.addChildElement (x.release());
            }
    copyXmlToBinary (root, data);
}
void TriadTime::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("TriadTime")) return;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (xml->hasAttribute (withId->getParameterID()))
                p->setValueNotifyingHost ((float) xml->getDoubleAttribute (withId->getParameterID()));
    for (int e = 0; e < 2; ++e)
        for (int s = 0; s < numSlots; ++s) setShape (e, s, slotPreset (e, s), false);
    for (auto* x : xml->getChildIterator())
    {
        const int e = x->hasTagName ("Time") ? timeEnvelope : x->hasTagName ("Volume") ? volumeEnvelope : -1;
        const int s = x->getIntAttribute ("slot") - 1;
        if (e < 0 || s < 0 || s >= numSlots) continue;
        auto shape = mod::fromXml (x);
        // Saved by Triad Time 0.8 / 0.9, whose TIME grid reached twice the pattern's length back: the same delays on
        // today's grid (one length back). Anything further back than one length stays at the bottom.
        if (e == timeEnvelope && xml->getIntAttribute ("version", 1) < 2)
            for (auto& p : shape) p.y = juce::jmax (0.0f, 2.0f * p.y - 1.0f);
        if (shape.size() >= 2) setShape (e, s, shape, false);
    }
    for (auto& u : undoStack) u.clear();
    for (auto& r : redoStack) r.clear();
}

// ---- Editor -----------------------------------------------------------------------------------------------------
namespace
{
// The TIME and VOLUME colours (lines, tabs, knobs): from the theme, any colour Thomas likes.
const juce::Colour& timeColour = currentPalette.timeLine;
const juce::Colour& volumeColour = currentPalette.volumeLine;

struct EnvelopeHost final : ShapeHost
{
    EnvelopeHost (TriadTime& p, int e) : processor (p), envelope (e) {}
    TriadTime& processor;
    int envelope;
    mod::Shape getShape() const override { return processor.getShape (envelope, processor.getSlot (envelope)); }
    void setShape (const mod::Shape& s, bool undoable) override { processor.setShape (envelope, processor.getSlot (envelope), s, undoable); }
    void beginEdit() override { processor.beginEdit (envelope); }
    bool canUndo() const override { return processor.canUndo (envelope); }
    bool canRedo() const override { return processor.canRedo (envelope); }
    bool undo() override { return processor.undo (envelope); }
    bool redo() override { return processor.redo (envelope); }
    int getLength() const override { return processor.getLength (envelope); }
    void setLength (int index) override { processor.setLength (envelope, index); }
    bool allowsLength (int index) const override
    {
        return envelope == TriadTime::timeEnvelope ? TriadTime::isTimeLength (index) : TriadTime::isVolumeLength (index);
    }
    float getPhase() const override { return processor.getPhase (envelope); }
    float getPhaseNow() const override { return processor.getPhaseNow (envelope); }
    juce::Colour getColour() const override { return envelope == TriadTime::timeEnvelope ? timeColour : volumeColour; }
    juce::String valueLabel (int percent) const override
    {
        if (envelope != TriadTime::timeEnvelope) return juce::String (percent) + "%";
        // How far back, in parts of the pattern (as ShaperBox: 0 at the top, -4/4 at the bottom, one length back).
        const int quarters = juce::roundToInt ((100 - percent) / 25.0);
        return quarters == 0 ? juce::String ("0") : "-" + juce::String (quarters) + "/4";
    }
    bool squareGrid() const override { return true; }
    // Behind the grid (0.15.0, the gear's BEHIND THE GRID): the sound coming in, across the pattern (the waveform), or
    // its spectrum, as Triad's (the whole range lit in the envelope's colour).
    bool hasBackdrop() const override { return backdrop != TriadSettings::Backdrop::off; }
    void paintBackdrop (juce::Graphics& g, juce::Rectangle<float> plot) override
    {
        if (backdrop == TriadSettings::Backdrop::spectrum)
        {
            processor.setTracedEnvelope (-1);
            if (spectrumView == nullptr) spectrumView = std::make_unique<triad::ui::SpectrumBackdrop>();
            spectrumView->paint (g, plot, processor.getSpectrumTap(), processor.getSampleRateForDisplay(), 20.0f, 20000.0f, getColour());
            return;
        }
        processor.setTracedEnvelope (envelope);
        processor.readTrace (level, age);
        triad::ui::drawWaveTrace (g, plot, level, age, getColour(), std::numeric_limits<float>::quiet_NaN());
    }
    TriadSettings::Backdrop backdrop = TriadSettings::Backdrop::off;   // the setting (Triad Time's gear, or Triad's)
    std::array<float, triad::WaveTrace::bins> level {};
    std::array<int, triad::WaveTrace::bins> age {};
    std::unique_ptr<triad::ui::SpectrumBackdrop> spectrumView;
    bool shadeBelowDiagonal() const override { return envelope == TriadTime::timeEnvelope; }
    bool offersTimePresets() const override { return envelope == TriadTime::timeEnvelope; }
    juce::String segmentLabel (const mod::Point& from, const mod::Point& to) const override
    {
        if (envelope != TriadTime::timeEnvelope) return {};
        return TriadTime::pitchText (TriadTime::segmentSpeed (from, to));
    }
    int segmentLabelMode() const override { return envelope == TriadTime::timeEnvelope ? labelMode : 0; }
    int labelMode = (int) TriadSettings::PitchLabels::dragging;   // the setting (Triad Time's gear, Triad's gear)
};

// MIX AND SMOOTHING (0.16.1, Thomas's layout): five small value boxes in the toolbar, right of SAVE.
class MixBoxes final : public juce::Component
{
public:
    void setKnobs (std::initializer_list<juce::Slider*> list)
    {
        knobs = list;
        for (auto* k : knobs) addAndMakeVisible (*k);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (currentPalette.grid.interpolatedWith (currentPalette.panel, 0.5f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold).withKerningFactor (0.06f));
        g.drawText ("MIX AND SMOOTHING", getLocalBounds().reduced (8, 3).removeFromTop (11), juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4).withTrimmedTop (12);
        const int n = juce::jmax (1, (int) knobs.size()), gap = 4, w = (r.getWidth() - gap * (n - 1)) / n;
        for (auto* k : knobs) { k->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
    }
private:
    std::vector<juce::Slider*> knobs;
};

class TriadTimeEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TriadTimeEditor (TriadTime& p) : AudioProcessorEditor (p), processor (p)
    {
        setLookAndFeel (&look);
        for (auto* tab : { &timeTab, &volumeTab })
        {
            tab->setClickingTogglesState (false);
            addAndMakeVisible (*tab);
        }
        timeTab.setTooltip ("TIME: how far back Triad Time plays (top = live). Steps repeat, slopes down slow down or reverse.");
        volumeTab.setTooltip ("VOLUME: the gate (top = 100%, bottom = silence).");
        timeTab.onClick = [this] { showEnvelope (TriadTime::timeEnvelope); };
        volumeTab.onClick = [this] { showEnvelope (TriadTime::volumeEnvelope); };
        // Drag one tab past the other to swap the order the sound goes through them (0.17.0): left first.
        for (auto* tab : { &timeTab, &volumeTab })
        {
            tab->addMouseListener (this, false);
            tab->setTooltip (tab->getTooltip() + " Drag it past the other tab to swap their order (the sound goes through the left one first).");
        }
        shownVolumeFirst = processor.isVolumeFirst();
        // The 12 patterns of each envelope (the DAW can switch them: "TIME pattern", "VOLUME pattern"): one small button.
        patternButton.setTooltip ("Pattern: which of the 12 patterns plays (your DAW can switch them with automation)");
        patternButton.onClick = [this] { showPatternMenu(); };
        addAndMakeVisible (patternButton);
        shapes.onChange = [this] { refreshSlots(); };
        shapes.getEditor().gridDivision = 64;   // ShaperBox's grid: 64 columns and 16 rows for a 1-bar pattern
        shapes.setLayout (ShapePanel::Layout::browser);   // 0.16.1: Thomas's layout (presets in a browser under the grid)
        addAndMakeVisible (shapes);
        addAndMakeVisible (shapes.getBrowser());
        generator.setTarget (&shapes.getEditor());
        addAndMakeVisible (generator);
        struct KnobSetup { juce::Slider* knob; juce::RangedAudioParameter* parameter; juce::Colour colour; bool ms; const char* tip; };
        for (const auto& k : { KnobSetup { &timeMixKnob, processor.timeMix, timeColour, false, "Time mix: 0% = the dry sound, 100% = what the TIME envelope plays" },
                               KnobSetup { &volumeMixKnob, processor.volumeMix, volumeColour, false, "Volume mix: how much of the VOLUME gate is applied" },
                               KnobSetup { &smoothingKnob, processor.smoothing, timeColour, true,
                                           "Smoothing (0-50 ms): a crossfade at every jump in time, so repeats and returns to live do not click" },
                               KnobSetup { &attackKnob, processor.attack, volumeColour, true, "Attack (0-500 ms): how fast the gate opens" },
                               KnobSetup { &releaseKnob, processor.release, volumeColour, true, "Release (0-1000 ms): how fast the gate closes" } })
        {
            k.knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.knob->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.knob->setColour (juce::Slider::rotarySliderFillColourId, k.colour);
            if (k.ms) k.knob->getProperties().set ("triadUnit", "ms");
            k.knob->setTooltip (k.tip);
            attachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*k.parameter, *k.knob));
        }
        timeMixKnob.getProperties().set ("triadLabel", "TIME");
        volumeMixKnob.getProperties().set ("triadLabel", "VOLUME");
        smoothingKnob.getProperties().set ("triadLabel", "SMOOTH");
        attackKnob.getProperties().set ("triadLabel", "ATTACK");
        releaseKnob.getProperties().set ("triadLabel", "RELEASE");
        for (auto* k : { &timeMixKnob, &volumeMixKnob, &smoothingKnob, &attackKnob, &releaseKnob }) k->setMouseDragSensitivity (160);
        mixBoxes.setKnobs ({ &timeMixKnob, &volumeMixKnob, &smoothingKnob, &attackKnob, &releaseKnob });
        shapes.setToolbarExtra (&mixBoxes, 310);
        gearButton.onClick = [this] { showGearMenu(); };
        addAndMakeVisible (gearButton);
        addChildComponent (themePanel);
        applyLabelMode (settings->getPitchLabels());
        applyBackdrop();
        showEnvelope (TriadTime::timeEnvelope);
        // The grid spans the whole window at ShaperBox's proportions (1 bar across is 3.48 x 1 bar down), so a line drawn at
        // ShaperBox's angle bends the pitch as much (a cut from 0 to -1/16 over 1/8 = an octave down).
        setSize (1100, windowHeightFor (1100));
        startTimerHz (15);
    }
    ~TriadTimeEditor() override
    {
        processor.setTracedEnvelope (-1);
        processor.getSpectrumTap().active.store (false);
        setLookAndFeel (nullptr);
    }
    void showEnvelope (int e)
    {
        envelope = e;
        generator.setTasteKey (e == TriadTime::timeEnvelope ? "time" : "volume");   // the check and the X train each its own taste
        // TIME's dice draws from the Gross Beat banks' time patterns or Triad Time's TIME presets; VOLUME's from the banks' volume ones.
        if (e == TriadTime::timeEnvelope) generator.setSources ({ mod::Source::grossBeatTime, mod::Source::timePresets });
        else generator.setSources ({ mod::Source::grossBeatVolume, mod::Source::infiltrator });
        timeTab.setToggleState (e == TriadTime::timeEnvelope, juce::dontSendNotification);
        volumeTab.setToggleState (e == TriadTime::volumeEnvelope, juce::dontSendNotification);
        for (auto* tab : { &timeTab, &volumeTab })
        {
            const auto c = tab == &timeTab ? timeColour : volumeColour;
            tab->setColour (juce::TextButton::buttonColourId, tab->getToggleState() ? c.withAlpha (0.85f) : slotRow);
            tab->setColour (juce::TextButton::buttonOnColourId, c.withAlpha (0.85f));
            tab->setColour (juce::TextButton::textColourOnId, inkColour);
            tab->setColour (juce::TextButton::textColourOffId, tab->getToggleState() ? inkColour : c);
        }
        shapes.lengthTooltip = e == TriadTime::timeEnvelope ? "TIME length: one cycle of the pattern (up to 2 bars; the grid reaches one cycle back, as in ShaperBox)"
                                                            : "VOLUME length: one cycle of the gate";
        shapes.setHost (&hostFor (e));
        refreshSlots();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText ("TRIAD TIME", header.removeFromLeft (170), juce::Justification::centredLeft);
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("by Thomas Ross", header.removeFromLeft (110), juce::Justification::centredLeft);
        // Between the tabs: which way the sound goes.
        {
            const auto& left = processor.isVolumeFirst() ? volumeTab : timeTab;
            const float x = (float) left.getRight() + 4.0f, y = (float) left.getBounds().getCentreY();
            juce::Path arrow;
            arrow.addTriangle (x, y - 4.0f, x, y + 4.0f, x + 6.0f, y);
            g.setColour (dimTextColour);
            g.fillPath (arrow);
        }
        // The tips, top right (Thomas's layout).
        auto tips = juce::Rectangle<int> (patternButton.getRight() + 16, 0, getWidth() - 16 - 44 - (patternButton.getRight() + 16), headerHeight);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (u8 ("Double-click: add point \xc2\xb7 Drag: move \xc2\xb7 Double-click a point: delete"), tips.removeFromTop (tips.getHeight() / 2).withTrimmedTop (8),
                    juce::Justification::bottomRight);
        g.drawText (u8 ("Drag the small dot: curve \xc2\xb7 Drag across: select points \xc2\xb7 Right-click: more"), tips.withTrimmedBottom (8),
                    juce::Justification::topRight);
    }
    // The pitch labels on the TIME lines: from the settings (shared with Triad's gear menu and every Triad Time).
    void applyLabelMode (TriadSettings::PitchLabels mode)
    {
        if (timeHost.labelMode == (int) mode) return;
        timeHost.labelMode = (int) mode;
        shapes.getEditor().repaint();
    }
    // The sound behind the grid, as set in the gear (here or in Triad's).
    void applyBackdrop()
    {
        const auto mode = settings->getTimeBackdrop();
        if (mode == timeHost.backdrop) return;
        timeHost.backdrop = volumeHost.backdrop = mode;
        if (mode != TriadSettings::Backdrop::waveform) processor.setTracedEnvelope (-1);
        if (mode != TriadSettings::Backdrop::spectrum) processor.getSpectrumTap().active.store (false);
        shapes.getEditor().repaint();
    }
public:
    TriadSettings::Backdrop getBackdrop() const noexcept { return timeHost.backdrop; }
private:
    void showGearMenu()
    {
        const auto mode = settings->getPitchLabels();
        juce::PopupMenu menu;
        menu.addSectionHeader ("PITCH LABELS (TIME)");
        menu.addItem (1, "Off", true, mode == TriadSettings::PitchLabels::off);
        menu.addItem (2, "While dragging a point", true, mode == TriadSettings::PitchLabels::dragging);
        menu.addItem (3, "Always, on every line", true, mode == TriadSettings::PitchLabels::always);
        menu.addItem (-1, "FWD / REV: which way it plays; st: semitones up or down", false);
        menu.addSectionHeader ("BEHIND THE GRID");
        const auto backdropNow = settings->getTimeBackdrop();
        menu.addItem (10, "Nothing", true, backdropNow == TriadSettings::Backdrop::off);
        menu.addItem (11, "The waveform (the sound coming in, across the pattern)", true, backdropNow == TriadSettings::Backdrop::waveform);
        menu.addItem (12, "The spectrum (as Triad's, Pro-Q style)", true, backdropNow == TriadSettings::Backdrop::spectrum);
        triad::ui::addThemeItems (menu, themeBase);
        juce::Component::SafePointer<TriadTimeEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gearButton), [safe] (int choice)
        {
            if (safe == nullptr) return;
            if (triad::ui::handleThemeChoice (choice, themeBase, [safe] { if (safe != nullptr) safe->openThemePanel(); })) return;
            if (choice >= 10 && choice <= 12)
            {
                safe->settings->setTimeBackdrop ((TriadSettings::Backdrop) (choice - 10));
                safe->applyBackdrop();
                return;
            }
            if (choice < 1 || choice > 3) return;
            const auto next = (TriadSettings::PitchLabels) (choice - 1);
            safe->settings->setPitchLabels (next);
            safe->applyLabelMode (next);
        });
    }
    // Colours (the gear's THEME: Edit colours...), over the whole window.
    void openThemePanel() { themePanel.open(); }
    void themeChanged()
    {
        // A new theme (from here or another plug-in): re-colour what was set up once, then redraw everything.
        look.applyTheme();
        for (auto* knob : { &timeMixKnob, &smoothingKnob }) knob->setColour (juce::Slider::rotarySliderFillColourId, timeColour);
        for (auto* knob : { &volumeMixKnob, &attackKnob, &releaseKnob }) knob->setColour (juce::Slider::rotarySliderFillColourId, volumeColour);
        showEnvelope (envelope);
        sendLookAndFeelChange();
        repaint();
    }
    static constexpr int themeBase = 100;
    // The header, the grid with its toolbar, then the presets and GENERATE; the window's height gives the grid ShaperBox's
    // proportions (0.16.1: Thomas's layout).
    static constexpr int headerHeight = 62, bottomRowHeight = GeneratorPanel::preferredHeight;
    static int shapesHeightFor (int shapesWidth)
    {
        // ShapePanel (browser layout): the grid's editor, then 8 and the toolbar; the editor trims 48 across and 30 down.
        return juce::roundToInt ((float) (shapesWidth - 48) / ShapeHost::shaperBoxAspect) + 30 + 8 + ShapePanel::browserToolbarHeight;
    }
    static int windowHeightFor (int width) { return headerHeight + shapesHeightFor (width - 32) + 12 + bottomRowHeight + 14; }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        auto header = r.removeFromTop (headerHeight);
        gearButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 30));
        themePanel.setBounds (getLocalBounds());
        header.removeFromLeft (290);   // TRIAD TIME by Thomas Ross
        auto tabs = header.withSizeKeepingCentre (header.getWidth(), 36);
        // In the order the sound goes through them, left first.
        auto& first = processor.isVolumeFirst() ? volumeTab : timeTab;
        auto& second = processor.isVolumeFirst() ? timeTab : volumeTab;
        first.setBounds (tabs.removeFromLeft (112));
        tabs.removeFromLeft (14);
        second.setBounds (tabs.removeFromLeft (112));
        tabs.removeFromLeft (12);
        patternButton.setBounds (tabs.removeFromLeft (124).reduced (0, 3));
        shapes.setBounds (r.removeFromTop (shapesHeightFor (r.getWidth())));
        r.removeFromTop (12);
        auto bottom = r.removeFromTop (bottomRowHeight);
        generator.setBounds (bottom.removeFromRight (270));
        bottom.removeFromRight (12);
        shapes.getBrowser().setBounds (bottom);
    }
    ShapePanel& getShapePanel() noexcept { return shapes; }
    // The heart on pattern button s (1-12) of the envelope shown: its shape into FAVORITES, or out again.
    void togglePatternFavourite (int s)
    {
        const auto shape = processor.getShape (envelope, s);
        const auto name = juce::String (envelope == TriadTime::timeEnvelope ? "TIME" : "VOLUME") + " pattern " + juce::String (s + 1);
        const bool now = triad::favouriteShapes::toggle (name, shape);
        favouritesSeen = triad::favouriteShapes::version();
        juce::ignoreUnused (now);
        refreshSlots();
    }
    bool isPatternFavourite (int s) const
    {
        return s >= 0 && s < TriadTime::numSlots && triad::favouriteShapes::contains (processor.getShape (envelope, s));
    }
private:
    juce::int64 favouritesSeen = 0;
    EnvelopeHost& hostFor (int e) { return e == TriadTime::timeEnvelope ? timeHost : volumeHost; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A tab dragged past the other one: swap the order (once per drag).
        const bool isTab = e.eventComponent == &timeTab || e.eventComponent == &volumeTab;
        if (! isTab || swappedThisDrag) return;
        const bool leftTab = e.eventComponent == (processor.isVolumeFirst() ? &volumeTab : &timeTab);
        const int dx = e.getDistanceFromDragStartX();
        if ((leftTab && dx > 60) || (! leftTab && dx < -60))
        {
            processor.setVolumeFirst (! processor.isVolumeFirst());
            swappedThisDrag = true;
            orderChanged();
        }
    }
    void mouseUp (const juce::MouseEvent&) override { swappedThisDrag = false; }
    void orderChanged()
    {
        shownVolumeFirst = processor.isVolumeFirst();
        resized();
        repaint();
    }
    void refreshSlots()
    {
        patternButton.setButtonText ("PATTERN " + juce::String (processor.getSlot (envelope) + 1) + u8 (" \xe2\x96\xbe"));
        shown[0] = processor.getSlot (0);
        shown[1] = processor.getSlot (1);
    }
    // The 12 patterns: the one playing ticked, a heart on the favorites; the DAW's automation switches them too.
    void showPatternMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (juce::String (envelope == TriadTime::timeEnvelope ? "TIME" : "VOLUME") + " PATTERN (YOUR DAW CAN SWITCH IT)");
        const int current = processor.getSlot (envelope);
        for (int s = 0; s < TriadTime::numSlots; ++s)
        {
            const bool favourite = triad::favouriteShapes::contains (processor.getShape (envelope, s));
            triad::ui::addItemWithRight (menu, s + 1, (favourite ? u8 ("\xe2\x99\xa5 ") : juce::String()) + "Pattern " + juce::String (s + 1),
                                         TriadTime::slotName (envelope, s), true, s == current);
        }
        juce::Component::SafePointer<TriadTimeEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternButton), [safe] (int choice)
        {
            if (safe == nullptr || choice < 1 || choice > TriadTime::numSlots) return;
            safe->processor.setSlot (safe->envelope, choice - 1);
            safe->refreshSlots();
            safe->shapes.setHost (&safe->hostFor (safe->envelope));
        });
    }
    void timerCallback() override
    {
        shapes.updateToolbar();
        if (++settingsTicks >= 8)
        {
            settingsTicks = 0;
            applyLabelMode (settings->getPitchLabels());   // changed in Triad's gear menu
            applyBackdrop();
            if (const auto v = triad::favouriteShapes::version(); v != favouritesSeen) { favouritesSeen = v; refreshSlots(); }   // hearts marked elsewhere
            if (processor.isVolumeFirst() != shownVolumeFirst) orderChanged();   // the DAW's automation or undo
        }
    }
    // Every frame of the screen (up to 120 a second): a pattern switched by automation (or one of Triad's LFOs)
    // shows at once.
    void frame (double time)
    {
        if (! pacer->due (lastFrame, time)) return;
        if (processor.getSlot (0) != shown[0] || processor.getSlot (1) != shown[1])
        {
            refreshSlots();
            shapes.setHost (&hostFor (envelope));
        }
    }
    TriadTime& processor;
    triad::ui::TriadLook look;
    int envelope = TriadTime::timeEnvelope;
    int shown[2] { -1, -1 };
    EnvelopeHost timeHost { processor, TriadTime::timeEnvelope }, volumeHost { processor, TriadTime::volumeEnvelope };
    juce::TextButton timeTab { "TIME" }, volumeTab { "VOLUME" };
    triad::ui::IconButton gearButton { triad::ui::IconButton::Icon::gear, "Settings: pitch labels on the TIME lines, theme and colours" };
    juce::SharedResourcePointer<TriadSettings> settings;
    int settingsTicks = 0;
    juce::TextButton patternButton;
    bool shownVolumeFirst = false, swappedThisDrag = false;
    MixBoxes mixBoxes;
    ShapePanel shapes;
    GeneratorPanel generator;
    juce::Slider timeMixKnob, volumeMixKnob, smoothingKnob, attackKnob, releaseKnob;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;
    juce::TooltipWindow tooltips { this, 600 };
    double lastFrame = 0.0;
    juce::SharedResourcePointer<triad::ui::FramePacer> pacer;
    juce::VBlankAttachment frames { this, [this] (double t) { frame (t); } };
    triad::ui::ThemePanel themePanel;
    triad::ui::ThemeWatcher themeWatcher { [this] { themeChanged(); } };   // last: the window is built when it first calls
};
}

juce::AudioProcessorEditor* TriadTime::createEditor() { return new TriadTimeEditor (*this); }

// ---- Triad's built-in format ---------------------------------------------------------------------------------------
std::vector<juce::PluginDescription> TriadBuiltInFormat::effects() { return { TriadTime::makeDescription() }; }
void TriadBuiltInFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& id)
{
    for (const auto& d : effects())
        if (d.fileOrIdentifier == id) results.add (new juce::PluginDescription (d));
}
juce::String TriadBuiltInFormat::getNameOfPluginFromIdentifier (const juce::String& id)
{
    for (const auto& d : effects())
        if (d.fileOrIdentifier == id) return d.name;
    return id;
}
void TriadBuiltInFormat::createPluginInstance (const juce::PluginDescription& d, double rate, int block, PluginCreationCallback callback)
{
    if (d.fileOrIdentifier != TriadTime::identifier) { callback (nullptr, "Unknown built-in effect: " + d.name); return; }
    auto instance = std::make_unique<TriadTime>();
    instance->setRateAndBufferSizeDetails (rate, block);
    callback (std::move (instance), {});
}

#if TRIAD_TIME_PLUGIN
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TriadTime(); }
#endif
