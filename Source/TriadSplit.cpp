#include "TriadSplit.h"
#include "CrossoverDisplay.h"
#include "Look.h"
#include <thread>

namespace L = triad::link;
using triad::maxBands;
using triad::maxCrossovers;

namespace
{
constexpr std::array<float, maxCrossovers> defaultCuts { 250.0f, 2500.0f, 6000.0f, 9000.0f, 12000.0f, 14000.0f, 16000.0f, 17500.0f, 19000.0f };

}

juce::AudioProcessor::BusesProperties TriadSplit::splitBuses()
{
    auto buses = BusesProperties()
                     .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    for (int b = 1; b <= maxBands; ++b)
        buses = buses.withOutput ("Band " + juce::String (b), juce::AudioChannelSet::stereo(), true);
    return buses;
}

namespace
{
// A structural edit, as kept in a group's log: a band split in two (+(band + 1): the new band is the upper part,
// above `band`) or a band removed (-(band + 1)). Every member moves its PLAY choice with the bands.
int playAfterEdit (int play, int code)
{
    if (play <= TriadSplit::playAll || play >= TriadSplit::playNone || code == 0) return play;
    const int index = play - 1;
    if (code > 0) return index > code - 1 ? play + 1 : play;
    const int removed = -code - 1;
    if (index == removed) return TriadSplit::playNone;   // its band is gone: it plays nothing rather than a neighbour twice
    return index > removed ? play - 1 : play;
}

// The values as the audio may use them (anything from the shared block is checked).
struct Sane
{
    int count = 3;
    std::array<float, maxCrossovers> cuts {};
    std::array<int, maxCrossovers> slopes {};
    explicit Sane (const L::Values& v)
    {
        count = juce::jlimit (1, maxBands, std::isfinite (v[(size_t) L::bandsIndex]) ? juce::roundToInt (v[(size_t) L::bandsIndex]) : 3);
        for (int j = 0; j < maxCrossovers; ++j)
        {
            const auto c = v[(size_t) L::cutIndex (j)], s = v[(size_t) L::slopeIndex (j)];
            cuts[(size_t) j] = std::isfinite (c) ? juce::jlimit (20.0f, 20000.0f, c) : defaultCuts[(size_t) j];
            slopes[(size_t) j] = juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder,
                                               std::isfinite (s) ? juce::roundToInt (s) : triad::CrossoverFilter::defaultOrder);
        }
    }
};
}

juce::String TriadSplit::playName (int play)
{
    if (play <= playAll) return "ALL";
    if (play >= playNone) return "NONE";
    return "BAND " + juce::String (play);
}

juce::AudioProcessorValueTreeState::ParameterLayout TriadSplit::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bands", 1 }, "Bands", 1, maxBands, 3));
    auto frequency = juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f);
    frequency.setSkewForCentre (1000.0f);
    for (int j = 0; j < maxCrossovers; ++j)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { crossoverID (j), 1 }, "Crossover " + juce::String (j + 1),
                                                            frequency, defaultCuts[(size_t) j], juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    for (int j = 0; j < maxCrossovers; ++j)
        p.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { slopeID (j), 1 }, "Crossover " + juce::String (j + 1) + " Slope",
                  triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, triad::CrossoverFilter::defaultOrder,
                  juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return triad::slopeText (v); })
                                                     .withValueFromStringFunction ([] (const juce::String& t)
                                                     { return juce::jlimit (1, 16, juce::roundToInt (t.getFloatValue() / 6.0f)); })));
    juce::StringArray plays { "All bands" };
    for (int b = 1; b <= maxBands; ++b) plays.add ("Band " + juce::String (b));
    plays.add ("None");
    p.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "play", 1 }, "Play", plays, playAll));
    return p;
}

TriadSplit::TriadSplit()
    : AudioProcessor (splitBuses()), parameters (*this, nullptr, "TriadSplit", makeParameters())
{
    bandsValue = parameters.getRawParameterValue ("bands");
    playValue = parameters.getRawParameterValue ("play");
    for (int j = 0; j < maxCrossovers; ++j)
    {
        cutValues[(size_t) j] = parameters.getRawParameterValue (crossoverID (j));
        slopeValues[(size_t) j] = parameters.getRawParameterValue (slopeID (j));
        appliedCuts[(size_t) j].store (defaultCuts[(size_t) j]);
    }
    lastOwn = readOwn();
    join (defaultGroup);
    updatePlayers();
    startTimerHz (60);   // linked Triad Splits follow each other on every frame of a 60 Hz screen
}
TriadSplit::~TriadSplit()
{
    stopTimer();
    {
        const juce::SpinLock::ScopedLockType lock (syncLock);
        leave();
    }
    updatePlayers();
}

// ---- Parameters -------------------------------------------------------------------------------------------
L::Values TriadSplit::readOwn() const noexcept
{
    L::Values v {};
    v[(size_t) L::bandsIndex] = (float) juce::roundToInt (bandsValue->load());
    for (int j = 0; j < maxCrossovers; ++j)
    {
        v[(size_t) L::cutIndex (j)] = cutValues[(size_t) j]->load();
        v[(size_t) L::slopeIndex (j)] = (float) juce::roundToInt (slopeValues[(size_t) j]->load());
    }
    return v;
}
void TriadSplit::setValue (juce::RangedAudioParameter* p, float value)
{
    if (p == nullptr || juce::exactlyEqual (p->convertFrom0to1 (p->getValue()), p->convertFrom0to1 (p->convertTo0to1 (value)))) return;
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
void TriadSplit::setOwn (const L::Values& v)
{
    setValue (parameters.getParameter ("bands"), v[(size_t) L::bandsIndex]);
    for (int j = 0; j < maxCrossovers; ++j)
    {
        setValue (crossoverParameter (j), v[(size_t) L::cutIndex (j)]);
        setValue (slopeParameter (j), v[(size_t) L::slopeIndex (j)]);
    }
}
juce::RangedAudioParameter* TriadSplit::crossoverParameter (int index) const
{
    return index >= 0 && index < maxCrossovers ? parameters.getParameter (crossoverID (index)) : nullptr;
}
juce::RangedAudioParameter* TriadSplit::slopeParameter (int index) const
{
    return index >= 0 && index < maxCrossovers ? parameters.getParameter (slopeID (index)) : nullptr;
}
int TriadSplit::getBandCount() const { return juce::jlimit (1, maxBands, juce::roundToInt (bandsValue->load())); }
int TriadSplit::getCrossoverSlope (int index) const
{
    if (index < 0 || index >= maxCrossovers) return triad::CrossoverFilter::defaultOrder;
    return juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, juce::roundToInt (slopeValues[(size_t) index]->load()));
}
std::vector<float> TriadSplit::effectiveCrossovers() const
{
    const int n = getBandCount() - 1;
    std::vector<float> cuts ((size_t) n);
    for (int j = 0; j < n; ++j) cuts[(size_t) j] = cutValues[(size_t) j]->load();
    triad::constrainCrossovers (cuts.data(), n, rate.load());
    return cuts;
}
int TriadSplit::getPlay() const { return juce::jlimit (playAll, playNone, juce::roundToInt (playValue->load())); }
void TriadSplit::setPlay (int play)
{
    auto* p = parameters.getParameter ("play");
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (playAll, playNone, play)));
    p->endChangeGesture();
    updatePlayers();
}

// ---- Edits from the window (each one edit, published to the group at once) -------------------------------
void TriadSplit::edit (const L::Values& next, int structuralCode)
{
    const juce::SpinLock::ScopedLockType lock (syncLock);
    auto* g = L::group (linkGroup.load());
    // The log first: a member that sees the new values always sees the edit too.
    if (structuralCode != 0 && g != nullptr)
    {
        const auto n = g->edits.load();
        g->editLog[(size_t) (n % L::editLogSize)].store (structuralCode);
        g->edits.store (n + 1, std::memory_order_release);
    }
    auto gesture = [this] (juce::RangedAudioParameter* p, float value)
    {
        if (p == nullptr || juce::exactlyEqual (p->convertFrom0to1 (p->getValue()), p->convertFrom0to1 (p->convertTo0to1 (value)))) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    };
    gesture (parameters.getParameter ("bands"), next[(size_t) L::bandsIndex]);
    for (int j = 0; j < maxCrossovers; ++j)
    {
        gesture (crossoverParameter (j), next[(size_t) L::cutIndex (j)]);
        gesture (slopeParameter (j), next[(size_t) L::slopeIndex (j)]);
    }
    publishLocalChanges();
    if (structuralCode != 0)
    {
        const int moved = playAfterEdit (getPlay(), structuralCode);
        if (moved != getPlay()) gesture (parameters.getParameter ("play"), (float) moved);
        if (g != nullptr) editsSeen = g->edits.load();
        editsFollowed.store (editsSeen);
    }
}
bool TriadSplit::addBand (float hz)
{
    const int count = getBandCount();
    if (count >= maxBands) return false;
    const auto cuts = effectiveCrossovers();
    std::vector<float> edges { triad::lowestCrossover };
    edges.insert (edges.end(), cuts.begin(), cuts.end());
    edges.push_back (triad::crossoverCeiling (rate.load()));
    int b = count - 1;
    if (hz > 0.0f)
    {
        for (int i = 0; i < count; ++i)
            if (hz < edges[(size_t) i + 1]) { b = i; break; }
    }
    else
    {
        float widest = -1.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto width = std::log (edges[(size_t) i + 1] / edges[(size_t) i]);
            if (width > widest) { widest = width; b = i; }
        }
    }
    const auto lo = edges[(size_t) b], hi = edges[(size_t) b + 1];
    const float margin = triad::minCrossoverRatio * 1.0005f;
    if (hi / lo < margin * margin) return false;
    const auto cut = juce::jlimit (lo * margin, hi / margin, hz > 0.0f ? hz : std::sqrt (lo * hi));
    auto next = readOwn();
    next[(size_t) L::bandsIndex] = (float) (count + 1);
    for (int j = 0; j < count; ++j)
    {
        const int from = j < b ? j : j - 1;
        next[(size_t) L::cutIndex (j)] = j == b ? cut : cuts[(size_t) from];
        next[(size_t) L::slopeIndex (j)] = j == b ? (float) triad::CrossoverFilter::defaultOrder : (float) getCrossoverSlope (from);
    }
    edit (next, b + 1);
    return true;
}
bool TriadSplit::removeBand (int band)
{
    const int count = getBandCount();
    if (count <= 1 || band < 0 || band >= count) return false;
    const auto cuts = effectiveCrossovers();
    // The band's range joins its lower neighbour (or the upper one, for the lowest band), as in Triad.
    const int removed = band == 0 ? 0 : band - 1;
    auto next = readOwn();
    next[(size_t) L::bandsIndex] = (float) (count - 1);
    for (int j = 0, k = 0; j < (int) cuts.size(); ++j)
        if (j != removed)
        {
            next[(size_t) L::cutIndex (k)] = cuts[(size_t) j];
            next[(size_t) L::slopeIndex (k)] = (float) getCrossoverSlope (j);
            ++k;
        }
    edit (next, -(band + 1));
    return true;
}
void TriadSplit::setAllCrossoverSlopes (int order)
{
    auto next = readOwn();
    for (int j = 0; j + 1 < getBandCount(); ++j)
        next[(size_t) L::slopeIndex (j)] = (float) juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, order);
    edit (next, 0);
}

// ---- Link groups --------------------------------------------------------------------------------------------
// Joining: with others already in the group, this instance takes the group's bands; alone, it brings its own.
void TriadSplit::join (int group)
{
    const juce::SpinLock::ScopedLockType lock (syncLock);
    joinLocked (group);
}
void TriadSplit::joinLocked (int group)
{
    leave();
    if (auto* g = L::group (group))
    {
        const int others = g->members.fetch_add (1);
        L::Values values {};
        uint32_t sequence = 0;
        if (others > 0 && L::read (*g, values, sequence))
        {
            setOwn (values);
            followed = sequence;
        }
        else
        {
            L::Mask all;
            all.fill (true);
            const auto own = readOwn();
            for (int attempt = 0; attempt < 10000 && ! L::write (*g, own, all); ++attempt) std::this_thread::yield();
            followed = L::sequenceOf (*g);
        }
        editsSeen = g->edits.load();
        editsFollowed.store (editsSeen);
        linkGroup.store (group);
    }
    lastOwn = readOwn();
    pending.fill (false);
}
void TriadSplit::leave()
{
    if (auto* g = L::group (linkGroup.load())) g->members.fetch_sub (1);
    linkGroup.store (0);
}
void TriadSplit::setLinkGroup (int group)
{
    group = juce::jlimit (0, L::numGroups, group);
    if (group == linkGroup.load()) return;
    join (group);
    updatePlayers();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}
int TriadSplit::getLinkedCount() const
{
    const auto* g = L::group (linkGroup.load());
    return g != nullptr ? juce::jmax (1, (int) g->members.load()) : 0;
}
int TriadSplit::getPlayers (int band) const
{
    const auto* g = L::group (linkGroup.load());
    return g != nullptr && band >= 0 && band <= L::anyBand ? juce::jmax (0, (int) g->players[(size_t) band].load()) : 0;
}
void TriadSplit::updatePlayers()
{
    const juce::SpinLock::ScopedLockType lock (playersLock);
    const int group = linkGroup.load();
    const int play = getPlay();
    const int band = group == 0 ? -1 : play == playAll || play == playNone ? L::anyBand : play - 1;
    if (group == playersGroup && band == playersBand) return;
    if (auto* g = L::group (playersGroup)) if (playersBand >= 0) g->players[(size_t) playersBand].fetch_sub (1);
    if (auto* g = L::group (group)) if (band >= 0) g->players[(size_t) band].fetch_add (1);
    playersGroup = group;
    playersBand = band;
}
// This instance's own changes since the last look (from its window, the DAW's automation or a restored state)
// go to the group. syncLock is held.
void TriadSplit::publishLocalChanges()
{
    auto* g = L::group (linkGroup.load());
    if (g == nullptr) return;
    const auto own = readOwn();
    L::Mask changed {};
    bool any = false;
    for (size_t i = 0; i < own.size(); ++i)
    {
        changed[i] = pending[i] || ! juce::exactlyEqual (own[i], lastOwn[i]);
        any |= changed[i];
    }
    lastOwn = own;
    if (! any) return;
    if (L::write (*g, own, changed)) pending.fill (false);
    else pending = changed;
}
void TriadSplit::syncLink()
{
    auto* g = L::group (linkGroup.load());
    if (g != nullptr)
    {
        const juce::SpinLock::ScopedLockType lock (syncLock);
        publishLocalChanges();
        // Band edits made on another member: move PLAY with the bands (before the new bands are heard here).
        const auto edits = g->edits.load (std::memory_order_acquire);
        if (edits != editsSeen)
        {
            int play = getPlay();
            if (edits - editsSeen <= (uint32_t) L::editLogSize)
                for (auto k = editsSeen; k != edits; ++k) play = playAfterEdit (play, g->editLog[(size_t) (k % L::editLogSize)].load());
            if (play != getPlay()) setValue (parameters.getParameter ("play"), (float) play);
            editsSeen = edits;
            editsFollowed.store (edits);
        }
        // The group's bands (unless a change of this one's still waits to be written).
        L::Values values {};
        uint32_t sequence = 0;
        const bool waiting = std::any_of (pending.begin(), pending.end(), [] (bool b) { return b; });
        if (! waiting && L::sequenceOf (*g) != followed && L::read (*g, values, sequence))
        {
            followed = sequence;
            setOwn (values);
            lastOwn = readOwn();
        }
    }
    updatePlayers();
}

// ---- Audio ----------------------------------------------------------------------------------------------------
bool TriadSplit::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto main = layouts.getMainOutputChannelSet();
    if (main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != main) return false;
    // Each band output: off, mono or stereo, whatever the main output is (Audio Units check them one by one).
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}
void TriadSplit::prepareToPlay (double sampleRate, int maximumBlock)
{
    rate.store (sampleRate > 0.0 ? sampleRate : 48000.0);
    capacity = juce::jmax (32, maximumBlock);
    for (auto& b : bands) b.setSize (2, capacity);
    input.setSize (2, capacity);
    gains.assign ((size_t) capacity, 1.0f);
    splitter.prepare (rate.load(), capacity);
    L::Values start = readOwn();
    L::Values values {};
    uint32_t sequence = 0;
    if (auto* g = L::group (linkGroup.load()))
        if (L::read (*g, values, sequence)) start = values;
    const Sane s (start);
    splitter.setBandCount (s.count, s.cuts.data(), s.slopes.data());
    publishApplied (s.cuts);
    fade = Fade::none;
    hold = false;
    fadeLength = juce::jmax (1, (int) (rate.load() * 0.005));
    playFadeLength = juce::jmax (1, (int) (rate.load() * 0.010));
    playFrom = playTo = getPlay();
    playFadeLeft = 0;
    audioGroup = -1;
    hasShared = false;
    prepared = true;
}
void TriadSplit::publishApplied (const std::array<float, maxCrossovers>& target)
{
    appliedBands.store (splitter.getBandCount());
    auto cuts = target;
    triad::constrainCrossovers (cuts.data(), splitter.getBandCount() - 1, rate.load());
    for (int j = 0; j < maxCrossovers; ++j) appliedCuts[(size_t) j].store (cuts[(size_t) j]);
}
void TriadSplit::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto started = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    if (! prepared || capacity <= 0)
    {
        buffer.clear();
        return;
    }
    // The bands to use: this instance's own, or (linked) the group's, after sending it this one's changes.
    auto wanted = readOwn();
    const int group = linkGroup.load();
    if (group != audioGroup) { audioGroup = group; hasShared = false; }
    auto* g = L::group (group);
    if (g != nullptr)
    {
        {
            const juce::SpinLock::ScopedTryLockType lock (syncLock);
            if (lock.isLocked()) publishLocalChanges();
        }
        L::Values values {};
        uint32_t sequence = 0;
        if (L::read (*g, values, sequence)) { shared = values; hasShared = true; }
        if (hasShared) wanted = shared;
    }
    const Sane target (wanted);
    if (fade == Fade::none && target.count != splitter.getBandCount()) { fade = Fade::out; fadePosition = fadeLength; }
    else if (fade == Fade::in && target.count != splitter.getBandCount()) fade = Fade::out;   // fades out from where it is
    if (fade == Fade::none || (fade == Fade::in && ! hold))
    {
        splitter.setCutoffs (target.cuts.data());
        splitter.setSlopes (target.slopes.data());
        publishApplied (target.cuts);
    }
    // PLAY: a new choice crossfades from the old one (or switches at once while the output is faded down).
    const int play = getPlay();
    if (play != playTo)
    {
        if (fade != Fade::none) { playFrom = playTo = play; playFadeLeft = 0; }
        else { playFrom = playTo; playTo = play; playFadeLeft = playFadeLength; }
    }
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int inChannels = mainIn.getNumChannels();
    float inPeak = 0.0f;
    for (int offset = 0; offset < n;)
    {
        int count = juce::jmin (n - offset, capacity);
        if (fade == Fade::out) count = juce::jmin (count, juce::jmax (1, fadePosition));
        for (int ch = 0; ch < 2; ++ch)
        {
            if (inChannels == 0) input.clear (ch, 0, count);
            else input.copyFrom (ch, 0, mainIn, juce::jmin (ch, inChannels - 1), offset, count);
            inPeak = juce::jmax (inPeak, input.getMagnitude (ch, 0, count));
        }
        spectrum.push (input, count);
        splitter.process (input, 0, count, bands);
        // The fade for a change in the number of bands, sample by sample.
        for (int s = 0; s < count; ++s)
        {
            if (fade == Fade::out) { gains[(size_t) s] = (float) fadePosition / (float) fadeLength; fadePosition = juce::jmax (0, fadePosition - 1); }
            else if (fade == Fade::in)
            {
                if (hold) { gains[(size_t) s] = 0.0f; continue; }
                gains[(size_t) s] = (float) fadePosition / (float) fadeLength;
                if (++fadePosition >= fadeLength) fade = Fade::none;
            }
            else gains[(size_t) s] = 1.0f;
        }
        writeOutputs (buffer, offset, count);
        if (fade == Fade::out && fadePosition == 0)
        {
            // Faded out: the new bands (the filters restart), then fade in. After a band was added or removed on
            // another linked Triad Split, wait (silent, up to 100 ms) until this one has moved its PLAY choice along.
            splitter.setBandCount (target.count, target.cuts.data(), target.slopes.data());
            publishApplied (target.cuts);
            fade = Fade::in;
            hold = g != nullptr && g->edits.load (std::memory_order_acquire) != editsFollowed.load();
            heldSamples = 0;
            playFrom = playTo = getPlay();
            playFadeLeft = 0;
        }
        else if (hold)
        {
            heldSamples += count;
            if (g == nullptr || g->edits.load (std::memory_order_acquire) == editsFollowed.load() || heldSamples > (int) (rate.load() * 0.1))
            {
                hold = false;
                playFrom = playTo = getPlay();
            }
        }
        offset += count;
    }
    inputPeak.store (juce::jmax (inputPeak.load(), inPeak));
    // Outputs the host gave but this layout does not use stay silent.
    for (int ch = getTotalNumOutputChannels(); ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
    cpuLoad.store ((float) (seconds * rate.load() / juce::jmax (1, n)));
}
void TriadSplit::writeOutputs (juce::AudioBuffer<float>& buffer, int offset, int count)
{
    const int numBands = splitter.getBandCount();
    auto source = [&] (int play, int ch, int s)
    {
        if (play == playAll)
        {
            float sum = 0.0f;
            for (int b = 0; b < numBands; ++b) sum += bands[(size_t) b].getSample (ch, s);
            return sum;
        }
        const int b = play - 1;
        return play == playNone || b >= numBands ? 0.0f : bands[(size_t) b].getSample (ch, s);
    };
    auto main = getBusBuffer (buffer, false, 0);
    const int mainChannels = main.getNumChannels();
    float peak = 0.0f;
    for (int s = 0; s < count; ++s)
    {
        float t = 1.0f;
        if (playFadeLeft > 0) t = 1.0f - (float) playFadeLeft-- / (float) playFadeLength;
        float out[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float to = source (playTo, ch, s);
            out[ch] = gains[(size_t) s] * (t >= 1.0f ? to : source (playFrom, ch, s) + t * (to - source (playFrom, ch, s)));
        }
        if (mainChannels >= 2) { main.setSample (0, offset + s, out[0]); main.setSample (1, offset + s, out[1]); }
        else if (mainChannels == 1) main.setSample (0, offset + s, 0.5f * (out[0] + out[1]));
        peak = juce::jmax (peak, std::abs (out[0]), std::abs (out[1]));
    }
    outputPeak.store (juce::jmax (outputPeak.load(), peak));
    // Every band on its own output (when the DAW turned that output on).
    for (int b = 0; b < maxBands && b + 1 < getBusCount (false); ++b)
    {
        auto out = getBusBuffer (buffer, false, b + 1);
        const int channels = out.getNumChannels();
        if (channels == 0) continue;
        if (b >= numBands) { out.clear (offset, count); continue; }
        const auto& band = bands[(size_t) b];
        for (int s = 0; s < count; ++s)
        {
            const float l = band.getSample (0, s) * gains[(size_t) s], r = band.getSample (1, s) * gains[(size_t) s];
            if (channels >= 2) { out.setSample (0, offset + s, l); out.setSample (1, offset + s, r); }
            else out.setSample (0, offset + s, 0.5f * (l + r));
        }
    }
}

// ---- State --------------------------------------------------------------------------------------------------------
void TriadSplit::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = parameters.copyState();
    state.setProperty ("link", linkGroup.load(), nullptr);
    state.setProperty ("version", TRIAD_VERSION, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}
void TriadSplit::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml (*xml);
    const int group = juce::jlimit (0, L::numGroups, (int) tree.getProperty ("link", defaultGroup));
    {
        // Nothing restored reaches the group until it is decided: with others already in the group, this one
        // takes the group's bands (they were linked when saved, so they are the same); alone, it brings its own.
        const juce::SpinLock::ScopedLockType lock (syncLock);
        parameters.replaceState (tree);
        joinLocked (group);
    }
    updatePlayers();
}

// ---- Window ---------------------------------------------------------------------------------------------------------
namespace
{
using namespace triad::ui;

class SplitCrossoverHost final : public CrossoverHost
{
public:
    explicit SplitCrossoverHost (TriadSplit& p) : split (p) {}
    juce::String getProductName() const override { return "Triad Split"; }
    int getBandCount() const override { return split.getBandCount(); }
    std::vector<float> effectiveCrossovers() const override { return split.effectiveCrossovers(); }
    int getCrossoverSlope (int index) const override { return split.getCrossoverSlope (index); }
    juce::RangedAudioParameter* crossoverParameter (int index) const override { return split.crossoverParameter (index); }
    juce::RangedAudioParameter* slopeParameter (int index) const override { return split.slopeParameter (index); }
    double getSampleRateForDisplay() const override { return split.getSampleRateForDisplay(); }
    void setAllCrossoverSlopes (int order) override { split.setAllCrossoverSlopes (order); }
    bool addBand (float hz) override { return split.addBand (hz); }
    bool removeBand (int band) override { return split.removeBand (band); }
    triad::SpectrumTap& getSpectrum() override { return split.spectrum; }
private:
    TriadSplit& split;
};

// A PLAY button: ALL, a band (in its colour) or NONE; a small number shows how many other linked Triad Splits play it.
class PlayButton final : public juce::Button
{
public:
    explicit PlayButton (int which) : juce::Button (TriadSplit::playName (which)), play (which) {}
    void setOthers (int n) { if (n != others) { others = n; repaint(); } }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto colour = play > TriadSplit::playAll && play < TriadSplit::playNone ? bandColour (play - 1) : juce::Colour (0xffd9dcd6);
        const bool on = getToggleState();
        g.setColour (on ? colour : over ? slotRowHover : slotRow);
        g.fillRoundedRectangle (r, 5.0f);
        if (! on) { g.setColour (colour.withAlpha (down ? 0.9f : 0.55f)); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.2f); }
        g.setColour (on ? juce::Colour (0xff111312) : colour);
        g.setFont (juce::FontOptions (play == TriadSplit::playAll || play == TriadSplit::playNone ? 13.0f : 16.0f, juce::Font::bold));
        const auto label = play == TriadSplit::playAll ? juce::String ("ALL") : play == TriadSplit::playNone ? juce::String ("NONE") : juce::String (play);
        g.drawText (label, r, juce::Justification::centred);
        if (others > 0)
        {
            const auto badge = juce::Rectangle<float> (16.0f, 16.0f).withPosition (r.getRight() - 18.0f, r.getY() + 2.0f);
            g.setColour (juce::Colour (0xff111312).withAlpha (on ? 0.75f : 1.0f));
            g.fillEllipse (badge);
            g.setColour (colour);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (others), badge, juce::Justification::centred);
        }
    }
    const int play;
private:
    int others = 0;
};

class TriadSplitEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TriadSplitEditor (TriadSplit& p) : AudioProcessorEditor (p), split (p), display (std::make_unique<SplitCrossoverHost> (p))
    {
        setLookAndFeel (&look);
        addAndMakeVisible (display);
        linkButton.setTooltip ("LINK: every Triad Split in the same group (A to H) shares these bands. Move a divider on one and "
                               "they all follow, on every track. OFF: this one keeps its own.");
        linkButton.onClick = [this] { showLinkMenu(); };
        addAndMakeVisible (linkButton);
        for (int play = TriadSplit::playAll; play <= TriadSplit::playNone; ++play)
        {
            auto b = std::make_unique<PlayButton> (play);
            b->setTooltip (play == TriadSplit::playAll ? "ALL: every band (the bands added back up: the sound as it came in)"
                           : play == TriadSplit::playNone ? "NONE: the main output is silent; every band still comes out on its own "
                                                            "output (Band 1, Band 2...), for FL's Patcher or routing the outputs to mixer inserts"
                                                          : "Play band " + juce::String (play) + " only");
            b->onClick = [this, play] { split.setPlay (play); refresh(); };
            addAndMakeVisible (*b);
            playButtons.push_back (std::move (b));
        }
        info.setInterceptsMouseClicks (false, false);
        // What moves (meters, dividers and slopes under automation or from a linked Triad Split) follows the screen,
        // up to 120 frames a second; the text below a few times a second.
        display.onFrame = [this] (double dt)
        {
            display.refreshIfChanged();
            display.getPacer().setAudioLoad (split.getCpuLoad());
            display.setMeters (split.takeInputPeak(), split.takeOutputPeak(), split.getCpuLoad(), dt);
            if (split.getPlay() != shownPlay || split.getBandCount() != laidOutBands) refresh();
        };
        setSize (980, 520);
        refresh();
        startTimerHz (15);
    }
    ~TriadSplitEditor() override { setLookAndFeel (nullptr); }
    CrossoverDisplay& getDisplay() noexcept { return display; }
    juce::Button* getPlayButton (int play) const { return play >= 0 && play < (int) playButtons.size() ? playButtons[(size_t) play].get() : nullptr; }
    juce::String getInfoText() const { return infoText; }
    juce::String getLinkText() const { return linkButton.getButtonText(); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        auto header = getLocalBounds().removeFromTop (48).reduced (16, 0);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText ("TRIAD SPLIT", header.removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("by Thomas Ross", header.removeFromLeft (110), juce::Justification::centredLeft);
        g.setColour (panel);
        g.fillRoundedRectangle (playArea.toFloat(), 7.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("PLAY", playArea.reduced (14, 0).removeFromTop (30), juce::Justification::centredLeft);
        g.setColour (dimTextColour);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("which band this track hears", playArea.reduced (14, 0).removeFromTop (30).withTrimmedLeft (44), juce::Justification::centredLeft);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (infoText, infoArea, juce::Justification::topLeft, 6, 1.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 0);
        auto header = r.removeFromTop (48);
        linkButton.setBounds (header.removeFromRight (170).withSizeKeepingCentre (170, 28));
        display.setBounds (r.removeFromTop (270));
        r.removeFromTop (10);
        playArea = r.removeFromTop (96);
        auto row = playArea.reduced (14, 0).withTrimmedTop (30).withTrimmedBottom (14);
        const int count = split.getBandCount();
        const int wide = 72, gap = 6;
        const int bandWidth = juce::jlimit (36, 84, (row.getWidth() - 2 * (wide + gap) - count * gap) / juce::jmax (1, count));
        for (auto& b : playButtons)
        {
            const bool band = b->play > TriadSplit::playAll && b->play < TriadSplit::playNone;
            b->setVisible (! band || b->play <= count);
            if (! b->isVisible()) continue;
            b->setBounds (row.removeFromLeft (band ? bandWidth : wide));
            row.removeFromLeft (gap);
        }
        r.removeFromTop (10);
        infoArea = r.withTrimmedBottom (12).reduced (4, 0);
        laidOutBands = count;
    }
private:
    void showLinkMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("LINK: SHARE THESE BANDS WITH");
        menu.addItem (1000, "OFF (this Triad Split keeps its own bands)", true, split.getLinkGroup() == 0);
        for (int group = 1; group <= L::numGroups; ++group)
        {
            const auto* g = L::group (group);
            const int members = g != nullptr ? (int) g->members.load() - (group == split.getLinkGroup() ? 1 : 0) : 0;
            menu.addItem (group, "Group " + L::groupName (group) + (members > 0 ? "  (" + juce::String (members) + " other Triad Split" + (members > 1 ? "s)" : ")")
                                                                                 : juce::String ("  (none yet)")),
                          true, group == split.getLinkGroup());
        }
        juce::Component::SafePointer<TriadSplitEditor> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&linkButton), [safe] (int choice)
        {
            if (safe == nullptr || choice <= 0) return;
            safe->split.setLinkGroup (choice == 1000 ? 0 : choice);
            safe->display.refresh();
            safe->refresh();
        });
    }
    juce::String rangeText (int band) const
    {
        const auto cuts = split.effectiveCrossovers();
        const int count = (int) cuts.size() + 1;
        if (band < 0 || band >= count) return {};
        if (count == 1) return "the whole sound";
        if (band == 0) return "below " + formatHz (cuts[0]);
        if (band == count - 1) return "above " + formatHz (cuts[(size_t) band - 1]);
        return formatHz (cuts[(size_t) band - 1]) + " to " + formatHz (cuts[(size_t) band]);
    }
public:
    void refresh()
    {
        const int group = split.getLinkGroup(), play = split.getPlay(), count = split.getBandCount();
        shownPlay = play;
        const int linked = split.getLinkedCount();
        const auto linkText = group == 0 ? juce::String ("LINK: OFF") : "LINK " + L::groupName (group) + u8 ("  \xc2\xb7  ") + juce::String (linked);
        if (linkButton.getButtonText() != linkText) linkButton.setButtonText (linkText);
        linkButton.setColour (juce::TextButton::buttonColourId, group == 0 ? slotRow : accent.withAlpha (0.85f));
        linkButton.setColour (juce::TextButton::textColourOffId, group == 0 ? textColour : juce::Colour (0xff111312));
        for (auto& b : playButtons)
        {
            b->setToggleState (b->play == play, juce::dontSendNotification);
            const bool band = b->play > TriadSplit::playAll && b->play < TriadSplit::playNone;
            const int others = group == 0 || ! band ? 0 : split.getPlayers (b->play - 1) - (b->play == play ? 1 : 0);
            b->setOthers (others);
        }
        if (count != laidOutBands) resized();
        // What this track hears, the link, and whether every band is on some track.
        juce::String text;
        if (play == TriadSplit::playAll) text = "This track hears every band (they add back up to the sound as it came in).";
        else if (play == TriadSplit::playNone) text = "The main output is silent. Every band comes out on its own output (Band 1 = outputs 3-4, Band 2 = 5-6...).";
        else if (play > count) text = "Band " + juce::String (play) + " does not exist (there are " + juce::String (count) + " bands): this track is silent.";
        else text = "This track hears band " + juce::String (play) + " only: " + rangeText (play - 1) + ".";
        text << "\n";
        if (group == 0) text << "Not linked: these bands are this Triad Split's own.";
        else
        {
            text << "Linked (group " << L::groupName (group) << "): " << (linked <= 1 ? juce::String ("no other Triad Split in this group yet.")
                                                                                    : juce::String (linked - 1) + (linked > 2 ? " other Triad Splits share these bands." : " other Triad Split shares these bands."));
            if (linked > 1 && split.getPlayers (L::anyBand) == 0)
            {
                juce::StringArray missing, twice;
                for (int b = 0; b < count; ++b)
                {
                    const int players = split.getPlayers (b);
                    if (players == 0) missing.add (juce::String (b + 1));
                    else if (players > 1) twice.add (juce::String (b + 1));
                }
                if (missing.isEmpty() && twice.isEmpty()) text << " Every band is on one track: together they add back up to the original.";
                if (! missing.isEmpty()) text << "\nNot on any track yet: band " << missing.joinIntoString (", ") << ".";
                if (! twice.isEmpty()) text << "\nOn more than one track: band " << twice.joinIntoString (", ") << " (it will be louder in the sum).";
            }
        }
        if (play != TriadSplit::playNone && count > 1)
            text << "\nEvery band also comes out on its own output (Band 1 = outputs 3-4, Band 2 = 5-6...), for FL's Patcher or mixer routing.";
        if (text != infoText) { infoText = text; repaint (infoArea); }
    }
private:
    void timerCallback() override
    {
        display.refreshIfChanged();   // (also every frame; this covers a window the screen does not draw frames for)
        display.setLatency (0, split.getSampleRateForDisplay(), {});
        refresh();
    }
    TriadSplit& split;
    triad::ui::TriadLook look;
    CrossoverDisplay display;
    juce::TextButton linkButton;
    std::vector<std::unique_ptr<PlayButton>> playButtons;
    juce::Component info;
    juce::Rectangle<int> playArea, infoArea;
    juce::String infoText;
    int laidOutBands = -1, shownPlay = -1;
    juce::TooltipWindow tooltips { this, 600 };
};
}

juce::AudioProcessorEditor* TriadSplit::createEditor() { return new TriadSplitEditor (*this); }

#if TRIAD_SPLIT_PLUGIN
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TriadSplit(); }
#endif
