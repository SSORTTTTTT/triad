#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PlatformWindows.h"
#include <map>
#include <numeric>
#include <set>

namespace
{
// A hosted effect's window. It stays in front of Triad's window, and keys the effect does not use
// (the spacebar for play/stop, DAW shortcuts) are passed back to the DAW.
class EffectWindow final : public juce::DocumentWindow
{
public:
    EffectWindow (juce::AudioPluginInstance& p, const juce::String& title, std::function<bool()> keyToHost)
        : DocumentWindow (title, juce::Colour (0xff141923), closeButton), forwardKey (std::move (keyToHost))
    {
        setUsingNativeTitleBar (true);
        auto* editor = p.createEditorIfNeeded();
        if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (p);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setWantsKeyboardFocus (true);
    }
    void closeButtonPressed() override { setVisible (false); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (forwardKey != nullptr && forwardKey()) return true;
        return DocumentWindow::keyPressed (key);
    }
private:
    std::function<bool()> forwardKey;
};

constexpr std::array<float, triad::maxCrossovers> parameterDefaults { 250.0f, 2500.0f, 6000.0f, 9000.0f, 12000.0f,
                                                                      14000.0f, 16000.0f, 17500.0f, 19000.0f };
constexpr int controlRate = 32;   // LFO values are updated every 32 samples, then smoothed
juce::String windowTitle (int band, int slot, const juce::String& name)
{
    return "Band " + juce::String (band + 1) + juce::String::fromUTF8 (" \xc2\xb7 Slot ") + juce::String (slot + 1)
         + juce::String::fromUTF8 (" \xe2\x80\x94 ") + name;
}
bool isTriad (const juce::PluginDescription& d) { return triad::isTriadName (d.name) || triad::isTriadName (d.descriptiveName); }
void storeMax (std::atomic<float>& target, float value)
{
    if (value > target.load (std::memory_order_relaxed)) target.store (value, std::memory_order_relaxed);
}
float peakOf (const juce::AudioBuffer<float>& b, int channels)
{
    float peak = 0.0f;
    for (int c = 0; c < juce::jmin (channels, b.getNumChannels()); ++c) peak = juce::jmax (peak, b.getMagnitude (c, 0, b.getNumSamples()));
    return peak;
}
}

juce::String TriadProcessor::crossoverID (int i) { return i == 0 ? "lowCut" : i == 1 ? "highCut" : "cross" + juce::String (i + 1); }
juce::String TriadProcessor::bandMixID (int b) { return "mix" + juce::String (b); }
juce::String TriadProcessor::bandGainID (int b) { return "gain" + juce::String (b); }

juce::AudioProcessorValueTreeState::ParameterLayout TriadProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto frequency = juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f);
    frequency.setSkewForCentre (1000.0f);
    const auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    const auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");
    const auto db = juce::AudioParameterFloatAttributes().withLabel ("dB")
                        .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1); });
    // IDs from Triad 0.1 (lowCut, highCut, mix0-2) are kept so existing projects and automation recall.
    for (int i = 0; i < triad::maxCrossovers; ++i)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { crossoverID (i), i < 2 ? 1 : 2 },
              "Crossover " + juce::String (i + 1), frequency, parameterDefaults[(size_t) i], hz));
    for (int b = 0; b < triad::maxBands; ++b)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandMixID (b), b < 3 ? 1 : 2 },
              "Band " + juce::String (b + 1) + " Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));
    auto gainRange = juce::NormalisableRange<float> (minGainDb, maxGainDb, 0.1f);
    gainRange.setSkewForCentre (-6.0f);
    for (int b = 0; b < triad::maxBands; ++b)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandGainID (b), 3 },
              "Band " + juce::String (b + 1) + " Volume", gainRange, 0.0f, db));
    return p;
}

float TriadProcessor::defaultCrossover (int index, int count)
{
    static const std::vector<std::vector<float>> table { {}, {}, { 1000.0f }, { 250.0f, 2500.0f },
                                                          { 200.0f, 1000.0f, 5000.0f }, { 120.0f, 500.0f, 2000.0f, 7000.0f } };
    if (count < (int) table.size() && index < (int) table[(size_t) count].size())
        return table[(size_t) count][(size_t) index];
    const auto t = (float) index / (float) juce::jmax (1, count - 2);
    return 80.0f * std::pow (12000.0f / 80.0f, t);
}

// ---- Band structure ---------------------------------------------------------------------------
TriadProcessor::Band::Band (bool withSlot)
{
    if (! withSlot) return;
    appendSlot();
    ensureModulation();
    relink();
}
TriadProcessor::Slot& TriadProcessor::Band::appendSlot()
{
    slots.push_back (std::make_unique<Slot>());
    slots.back()->id = nextSlotId++;
    return *slots.back();
}
TriadProcessor::Lfo& TriadProcessor::Band::appendLfo (int slotId, bool isBandLfo)
{
    lfos.push_back (std::make_unique<Lfo>());
    auto& l = *lfos.back();
    l.id = nextLfoId++;
    l.slotId = slotId;
    l.bandLfo = isBandLfo;
    return l;
}
void TriadProcessor::Band::ensureModulation()
{
    auto addAutomatic = [this] (Lfo& l, Destination d)
    {
        auto r = std::make_unique<Route>();
        r->lfoId = l.id;
        r->destination = d;
        r->automatic = true;
        r->muted.store (! l.touched);
        routes.push_back (std::move (r));
    };
    if (std::none_of (lfos.begin(), lfos.end(), [] (auto& l) { return l->bandLfo; }))
        addAutomatic (appendLfo (0, true), Destination { Destination::bandMix, 0 });
    for (auto& s : slots)
        if (std::none_of (lfos.begin(), lfos.end(), [&] (auto& l) { return l->slotId == s->id; }))
            addAutomatic (appendLfo (s->id, false), Destination { Destination::slotMix, s->id });
}
void TriadProcessor::Band::relink()
{
    // LFOs in display order: Band Mix, then one per slot (in slot order), then extra LFOs.
    std::vector<std::unique_ptr<Lfo>> ordered;
    auto take = [&] (auto predicate)
    {
        for (auto& l : lfos) if (l != nullptr && predicate (*l)) ordered.push_back (std::move (l));
    };
    take ([] (const Lfo& l) { return l.bandLfo; });
    for (auto& s : slots) take ([&] (const Lfo& l) { return ! l.bandLfo && l.slotId == s->id; });
    take ([] (const Lfo&) { return true; });   // extra LFOs, and LFOs of slots that no longer exist
    lfos = std::move (ordered);
    routes.erase (std::remove_if (routes.begin(), routes.end(), [this] (const std::unique_ptr<Route>& r)
    {
        return findLfo (r->lfoId) == nullptr || (r->destination.kind == Destination::slotMix && findSlot (r->destination.slotId) == nullptr);
    }), routes.end());
    for (auto& s : slots) s->modulators.clear();
    mixModulators.clear();
    volumeModulators.clear();
    for (auto& r : routes)
    {
        r->lfo = findLfo (r->lfoId);
        if (r->destination.kind == Destination::slotMix) findSlot (r->destination.slotId)->modulators.push_back (r.get());
        else if (r->destination.kind == Destination::bandMix) mixModulators.push_back (r.get());
        else volumeModulators.push_back (r.get());
    }
}
TriadProcessor::Lfo* TriadProcessor::Band::findLfo (int id) const
{
    for (auto& l : lfos) if (l->id == id) return l.get();
    return nullptr;
}
TriadProcessor::Slot* TriadProcessor::Band::findSlot (int id) const
{
    for (auto& s : slots) if (s->id == id) return s.get();
    return nullptr;
}
int TriadProcessor::Band::lfoIndex (int id) const
{
    for (size_t i = 0; i < lfos.size(); ++i) if (lfos[i]->id == id) return (int) i;
    return -1;
}
int TriadProcessor::Band::slotIndex (int id) const
{
    for (size_t i = 0; i < slots.size(); ++i) if (slots[i]->id == id) return (int) i;
    return -1;
}

// ---- Construction ---------------------------------------------------------------------------------
TriadProcessor::TriadProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Parameters", makeParameters())
{
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC && JUCE_PLUGINHOST_AU
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    for (int j = 0; j < triad::maxCrossovers; ++j) crossoverValues[(size_t) j] = parameters.getRawParameterValue (crossoverID (j));
    for (int b = 0; b < triad::maxBands; ++b)
    {
        bandMixValues[(size_t) b] = parameters.getRawParameterValue (bandMixID (b));
        bandGainValues[(size_t) b] = parameters.getRawParameterValue (bandGainID (b));
        bands[(size_t) b] = std::make_unique<Band>();
    }
    {
        const juce::ScopedLock lock (gate);
        publishInfo();
    }
    startTimerHz (10);
}
TriadProcessor::~TriadProcessor()
{
    stopTimer(); cancelPendingUpdate();
    closeWindows();
    releaseResources();
}
bool TriadProcessor::isBusesLayoutSupported (const BusesLayout& b) const
{
    return b.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
void TriadProcessor::readCrossovers (float* dest) const
{
    for (int j = 0; j < triad::maxCrossovers; ++j) dest[j] = crossoverValues[(size_t) j]->load();
}
TriadProcessor::Slot* TriadProcessor::slotAt (int band, int slot) const
{
    if (band < 0 || band >= numBands) return nullptr;
    const auto& slots = bands[(size_t) band]->slots;
    return slot >= 0 && slot < (int) slots.size() ? slots[(size_t) slot].get() : nullptr;
}
TriadProcessor::Lfo* TriadProcessor::lfoAt (int band, int lfo) const
{
    if (band < 0 || band >= numBands) return nullptr;
    const auto& lfos = bands[(size_t) band]->lfos;
    return lfo >= 0 && lfo < (int) lfos.size() ? lfos[(size_t) lfo].get() : nullptr;
}
TriadProcessor::Route* TriadProcessor::routeAt (int band, int route) const
{
    if (band < 0 || band >= numBands) return nullptr;
    const auto& routes = bands[(size_t) band]->routes;
    return route >= 0 && route < (int) routes.size() ? routes[(size_t) route].get() : nullptr;
}
void TriadProcessor::prepareSlot (Slot& slot)
{
    slot.mix.reset (rate.load(), 0.005);
    slot.mix.setCurrentAndTargetValue (slot.mixPercent.load() * 0.01f);
}
void TriadProcessor::prepareBand (int index, bool prepareEffects)
{
    auto& band = *bands[(size_t) index];
    const auto sr = rate.load();
    band.dry.setSize (2, capacity);
    band.slotDry.setSize (2, capacity);
    band.mix.reset (sr, 0.005);
    band.mix.setCurrentAndTargetValue (bandMixValues[(size_t) index]->load() * 0.01f);
    band.gain.reset (sr, 0.005);
    band.gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (bandGainValues[(size_t) index]->load()));
    for (auto& slot : band.slots)
    {
        prepareSlot (*slot);
        if (prepareEffects && slot->effect)
        {
            if (prepared) slot->effect->releaseResources();
            slot->effect->setRateAndBufferSizeDetails (sr, capacity);
            slot->effect->prepareToPlay (sr, capacity);
        }
    }
}
void TriadProcessor::resetDelays()
{
    for (auto& band : bands)
    {
        band->dryDelay.reset(); band->alignDelay.reset();
        for (auto& s : band->slots) s->dryDelay.reset();
    }
    bypassDelay.reset();
}
void TriadProcessor::prepareToPlay (double sr, int block)
{
    const juce::ScopedLock lock (gate);
    rate.store (sr); capacity = juce::jmax (1, block);
    splitter.prepare (sr, capacity);
    std::array<float, triad::maxCrossovers> cuts {};
    readCrossovers (cuts.data());
    splitter.setBandCount (numBands, cuts.data());
    for (auto& buffer : split) buffer.setSize (2, capacity);
    childMidi.ensureSize (4096);
    for (int b = 0; b < triad::maxBands; ++b) prepareBand (b, true);
    fadeLength = juce::jmax (1, (int) (sr * 0.01));
    fadeRemaining = 0;
    prepared = true;
    alignLatency();
    // A new playback session must not replay audio left in the compensation delays.
    resetDelays();
}
void TriadProcessor::releaseResources()
{
    const juce::ScopedLock lock (gate);
    if (prepared)
        for (auto& band : bands)
            for (auto& s : band->slots) if (s->effect) s->effect->releaseResources();
    prepared = false;
}
std::vector<float> TriadProcessor::effectiveCrossovers() const
{
    const int n = juce::jmax (0, getBandCount() - 1);
    std::vector<float> cuts ((size_t) n);
    for (int j = 0; j < n; ++j) cuts[(size_t) j] = crossoverValues[(size_t) j]->load();
    triad::constrainCrossovers (cuts.data(), n, rate.load());
    return cuts;
}
std::pair<float, float> TriadProcessor::effectiveCutoffs() const
{
    const auto c = effectiveCrossovers();
    return { c.empty() ? 0.0f : c[0], c.size() > 1 ? c[1] : nyquistFrequency() };
}
juce::RangedAudioParameter* TriadProcessor::crossoverParameter (int index) const
{
    return index >= 0 && index < triad::maxCrossovers ? parameters.getParameter (crossoverID (index)) : nullptr;
}
void TriadProcessor::alignLatency()
{
    int longest = 0;
    double longestTail = 0.0;
    bool changed = false;
    for (int b = 0; b < numBands; ++b)
    {
        auto& band = *bands[(size_t) b];
        int chain = 0;
        for (auto& slotPtr : band.slots)
        {
            auto& s = *slotPtr;
            int latency = 0;
            if (s.effect)
            {
                const auto reported = s.effect->getLatencySamples();
                const bool stereo = s.effect->getTotalNumInputChannels() == 2 && s.effect->getTotalNumOutputChannels() == 2;
                if (! stereo) s.error = "Unsupported channel layout; effect bypassed";
                else if (s.error.startsWith ("Unsupported channel layout")) s.error.clear();
                if (s.error.isEmpty() || s.error.startsWith ("Latency exceeds"))
                {
                    if (reported < 0 || reported > triad::maxLatency || (s.enabled && chain + reported > triad::maxLatency))
                        s.error = "Latency exceeds supported limit; effect bypassed";
                    else if (s.error.startsWith ("Latency exceeds")) s.error.clear();
                }
                if (s.isActive()) latency = reported;
                longestTail = juce::jmax (longestTail, s.effect->getTailLengthSeconds());
            }
            changed |= s.latency != latency;
            s.latency = latency;
            s.dryDelay.ensureCapacity (latency);
            chain += latency;
        }
        changed |= band.chainLatency != chain;
        band.chainLatency = chain;
        band.dryDelay.ensureCapacity (chain);
        longest = juce::jmax (longest, chain);
    }
    for (int b = 0; b < numBands; ++b) bands[(size_t) b]->alignDelay.ensureCapacity (longest - bands[(size_t) b]->chainLatency);
    bypassDelay.ensureCapacity (longest);
    changed |= totalLatency.load() != longest;
    totalLatency.store (longest);
    tail.store (longestTail + (double) longest / rate.load());
    if (changed && prepared) resetDelays();
    if (getLatencySamples() != longest) setLatencySamples (longest);
    publishInfo();
}
void TriadProcessor::timerCallback()
{
    const juce::ScopedTryLock lock (gate);
    if (lock.isLocked()) alignLatency();
}
void TriadProcessor::startFadeIn() { fadeRemaining = fadeLength; }

// ---- Audio ------------------------------------------------------------------------------------------
void TriadProcessor::readSongPosition()
{
    blockPlaying = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto bpm = position->getBpm()) blockBpm = juce::jlimit (20.0, 999.0, *bpm);
            if (const auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    blockBeatsPerBar = signature->numerator * 4.0 / signature->denominator;
            if (const auto ppq = position->getPpqPosition())
            {
                blockPpq = *ppq;
                blockPlaying = position->getIsPlaying();
            }
        }
}
void TriadProcessor::advanceLfos (Band& band, int offset, int frames)
{
    // While the song plays, LFOs follow the song position; when stopped they keep running at its tempo.
    const double sr = rate.load();
    const double beatsPerSample = blockBpm / 60.0 / sr;
    for (auto& lfoPtr : band.lfos)
    {
        auto& l = *lfoPtr;
        const int length = l.length.load (std::memory_order_relaxed);
        const double beats = triad::mod::lengthInBeats (length, blockBeatsPerBar);
        if (beats > 0.0)
        {
            l.increment = beatsPerSample / beats;
            if (blockPlaying)
            {
                const double p = (blockPpq + offset * beatsPerSample) / beats;
                l.blockPhase = p - std::floor (p);
            }
            else l.blockPhase = l.runPhase;
        }
        else
        {
            l.increment = triad::mod::lengthInHz (length) / sr;
            l.blockPhase = l.runPhase;
        }
        const double end = l.blockPhase + l.increment * frames;
        l.runPhase = end - std::floor (end);
        l.displayPhase.store ((float) l.runPhase, std::memory_order_relaxed);
    }
}
void TriadProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals denormals;
    midi.clear();
    spectrum.push (audio, audio.getNumSamples());
    storeMax (inputPeak, peakOf (audio, 2));
    readSongPosition();
    const juce::ScopedTryLock lock (gate);
    if (! lock.isLocked() || ! prepared || restoring.load() || muted.load() || audio.getNumChannels() < 2)
    { audio.clear(); return; }
    // Never emit misaligned audio while a hosted effect changes its reported latency.
    for (int b = 0; b < numBands; ++b)
        for (auto& s : bands[(size_t) b]->slots)
            if (s->isActive() && (s->effect->getLatencySamples() != s->latency
                                  || s->effect->getTotalNumInputChannels() != 2
                                  || s->effect->getTotalNumOutputChannels() != 2))
            { audio.clear(); return; }
    process (audio);
    storeMax (outputPeak, peakOf (audio, 2));
}
void TriadProcessor::process (juce::AudioBuffer<float>& audio)
{
    std::array<float, triad::maxCrossovers> cuts {};
    readCrossovers (cuts.data());
    splitter.setCutoffs (cuts.data());
    const int total = totalLatency.load();
    for (int offset = 0; offset < audio.getNumSamples(); offset += capacity)
    {
        const int n = juce::jmin (capacity, audio.getNumSamples() - offset);
        splitter.process (audio, offset, n, split);
        for (int ch = 0; ch < 2; ++ch) audio.clear (ch, offset, n);
        for (int b = 0; b < numBands; ++b)
        {
            auto& band = *bands[(size_t) b];
            auto& buffer = split[(size_t) b];
            advanceLfos (band, offset, n);
            for (int ch = 0; ch < 2; ++ch) band.dry.copyFrom (ch, 0, buffer, ch, 0, n);
            // The band's chain: each active slot processes the output of the one above it.
            for (auto& slotPtr : band.slots)
            {
                auto& slot = *slotPtr;
                if (! slot.isActive()) { slot.mix.skip (n); continue; }
                for (int ch = 0; ch < 2; ++ch) band.slotDry.copyFrom (ch, 0, buffer, ch, 0, n);
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
                childMidi.clear();
                slot.effect->setPlayHead (getPlayHead());
                slot.effect->setNonRealtime (isNonRealtime());
                slot.effect->processBlock (view, childMidi);
                if (slot.effect->getLatencySamples() != slot.latency)
                { audio.clear(); return; }
                // Slot dry/wet (knob, moved by its LFOs); the dry signal is delayed by the slot's latency.
                const float knob = slot.mixPercent.load (std::memory_order_relaxed) * 0.01f;
                for (int frame = 0; frame < n; ++frame)
                {
                    if (frame % controlRate == 0)
                    {
                        float target = knob;
                        for (auto* r : slot.modulators)
                            if (! r->muted.load (std::memory_order_relaxed))
                                target += r->amount.load (std::memory_order_relaxed) * (r->value (frame) - target);
                        slot.mix.setTargetValue (juce::jlimit (0.0f, 1.0f, target));
                    }
                    const float mix = slot.mix.getNextValue();
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float d = slot.dryDelay.tick (ch, band.slotDry.getSample (ch, frame), slot.latency);
                        const float w = buffer.getSample (ch, frame);
                        buffer.setSample (ch, frame, d + mix * (w - d));
                    }
                    slot.dryDelay.advance();
                }
                slot.liveMix.store (slot.mix.getCurrentValue() * 100.0f, std::memory_order_relaxed);
            }
            // Band mix (dry band vs. its chain) and volume, then align all bands to the longest chain.
            const int align = total - band.chainLatency;
            const float mixKnob = bandMixValues[(size_t) b]->load (std::memory_order_relaxed) * 0.01f;
            const float gainKnob = juce::Decibels::decibelsToGain (bandGainValues[(size_t) b]->load (std::memory_order_relaxed), minGainDb - 0.05f);
            float peak = 0.0f;
            for (int frame = 0; frame < n; ++frame)
            {
                if (frame % controlRate == 0)
                {
                    float mixTarget = mixKnob, gainTarget = gainKnob;
                    for (auto* r : band.mixModulators)
                        if (! r->muted.load (std::memory_order_relaxed))
                            mixTarget += r->amount.load (std::memory_order_relaxed) * (r->value (frame) - mixTarget);
                    for (auto* r : band.volumeModulators)
                        if (! r->muted.load (std::memory_order_relaxed))
                            gainTarget *= 1.0f + r->amount.load (std::memory_order_relaxed) * (r->value (frame) - 1.0f);
                    band.mix.setTargetValue (juce::jlimit (0.0f, 1.0f, mixTarget));
                    band.gain.setTargetValue (juce::jmax (0.0f, gainTarget));
                }
                const float mix = band.mix.getNextValue();
                const float gain = band.gain.getNextValue();
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float d = band.dryDelay.tick (ch, band.dry.getSample (ch, frame), band.chainLatency);
                    const float w = buffer.getSample (ch, frame);
                    const float out = gain * (d + mix * (w - d));
                    peak = juce::jmax (peak, std::abs (out));
                    audio.addSample (ch, offset + frame, band.alignDelay.tick (ch, out, align));
                }
                band.dryDelay.advance(); band.alignDelay.advance();
            }
            storeMax (bandPeaks[(size_t) b], peak);
        }
    }
    if (fadeRemaining > 0)
    {
        // Short fade-in after the band layout changes, so adding/removing bands does not click.
        for (int i = 0; i < audio.getNumSamples() && fadeRemaining > 0; ++i, --fadeRemaining)
        {
            const float g = 1.0f - (float) fadeRemaining / (float) fadeLength;
            for (int ch = 0; ch < 2; ++ch) audio.setSample (ch, i, audio.getSample (ch, i) * g);
        }
    }
    for (int ch = 2; ch < audio.getNumChannels(); ++ch) audio.clear (ch, 0, audio.getNumSamples());
}
void TriadProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    m.clear();
    const juce::ScopedTryLock lock (gate);
    if (! lock.isLocked() || ! prepared || restoring.load()) { b.clear(); return; }
    for (int s = 0; s < b.getNumSamples(); ++s)
    {
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            b.setSample (ch, s, bypassDelay.tick (ch, b.getSample (ch, s), totalLatency.load()));
        bypassDelay.advance();
    }
}
TriadProcessor::Meters TriadProcessor::takeMeters()
{
    Meters m;
    m.input = inputPeak.exchange (0.0f);
    m.output = outputPeak.exchange (0.0f);
    for (size_t b = 0; b < m.bands.size(); ++b) m.bands[b] = bandPeaks[b].exchange (0.0f);
    return m;
}

// ---- Parameters -------------------------------------------------------------------------------------
void TriadProcessor::setParameterValue (const juce::String& id, float plainValue)
{
    if (auto* p = parameters.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}
float TriadProcessor::getParameterValue (const juce::String& id) const
{
    auto* value = parameters.getRawParameterValue (id);
    return value != nullptr ? value->load() : 0.0f;
}
void TriadProcessor::stateChanged()
{
    // Slot mixes, LFOs and the layout are not DAW parameters: tell the host the project changed.
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

// ---- Bands --------------------------------------------------------------------------------------------
bool TriadProcessor::addBand (float hz)
{
    int index = 0, count = 0;
    std::array<float, triad::maxCrossovers> next {};
    {
        const juce::ScopedLock lock (gate);
        if (numBands >= triad::maxBands) { setStatus ("Triad supports up to 10 bands."); return false; }
        const auto cuts = effectiveCrossovers();
        std::vector<float> edges { triad::lowestCrossover };
        edges.insert (edges.end(), cuts.begin(), cuts.end());
        edges.push_back (triad::crossoverCeiling (rate.load()));
        int b = -1;
        if (hz > 0.0f)
        {
            b = numBands - 1;
            for (int i = 0; i < numBands; ++i)
                if (hz < edges[(size_t) i + 1]) { b = i; break; }
        }
        else
        {
            float widest = -1.0f;
            for (int i = 0; i < numBands; ++i)
            {
                const auto width = std::log (edges[(size_t) i + 1] / edges[(size_t) i]);
                if (width > widest) { widest = width; b = i; }
            }
        }
        const auto lo = edges[(size_t) b], hi = edges[(size_t) b + 1];
        const float margin = triad::minCrossoverRatio * 1.0005f;
        if (hi / lo < margin * margin) { setStatus ("That band is too narrow to split."); return false; }
        const auto target = hz > 0.0f ? hz : std::sqrt (lo * hi);
        const auto cut = juce::jlimit (lo * margin, hi / margin, target);
        index = b;
        for (int j = 0; j < (int) cuts.size(); ++j) next[(size_t) (j < b ? j : j + 1)] = cuts[(size_t) j];
        next[(size_t) b] = cut;
        muted.store (true);
        // The lower part keeps band b's effects; the new band b+1 (one empty slot) takes the upper part.
        for (int p = numBands; p > b + 1; --p) bands[(size_t) p] = std::move (bands[(size_t) p - 1]);
        bands[(size_t) b + 1] = std::make_unique<Band>();
        ++numBands;
        count = numBands;
        bandCount.store (numBands);
        prepareBand (b + 1, false);
        bands[(size_t) b + 1]->mix.setCurrentAndTargetValue (1.0f);
        bands[(size_t) b + 1]->gain.setCurrentAndTargetValue (1.0f);
        splitter.setBandCount (numBands, next.data());
        alignLatency();
        startFadeIn();
        setStatus ("Added band " + juce::String (b + 2) + " at " + juce::String (juce::roundToInt (cut)) + " Hz");
    }
    for (int j = 0; j < count - 1; ++j) setParameterValue (crossoverID (j), next[(size_t) j]);
    for (int p = count - 1; p > index + 1; --p)
    {
        setParameterValue (bandMixID (p), getParameterValue (bandMixID (p - 1)));
        setParameterValue (bandGainID (p), getParameterValue (bandGainID (p - 1)));
    }
    setParameterValue (bandMixID (index + 1), 100.0f);
    setParameterValue (bandGainID (index + 1), 0.0f);
    muted.store (false);
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}
bool TriadProcessor::removeBand (int band)
{
    std::unique_ptr<Band> removed;
    int count = 0;
    std::array<float, triad::maxCrossovers> next {};
    {
        const juce::ScopedLock lock (gate);
        if (numBands <= 1 || band < 0 || band >= numBands) return false;
        const auto cuts = effectiveCrossovers();
        // The band's range joins its lower neighbour (or the upper one, for the lowest band).
        const int removedCrossover = band == 0 ? 0 : band - 1;
        for (int j = 0, k = 0; j < (int) cuts.size(); ++j)
            if (j != removedCrossover) next[(size_t) k++] = cuts[(size_t) j];
        muted.store (true);
        removed = std::move (bands[(size_t) band]);
        for (int p = band; p < numBands - 1; ++p) bands[(size_t) p] = std::move (bands[(size_t) p + 1]);
        bands[(size_t) numBands - 1] = std::make_unique<Band>();
        prepareBand (numBands - 1, false);
        --numBands;
        count = numBands;
        bandCount.store (numBands);
        if (prepared)
            for (auto& s : removed->slots) if (s->effect) s->effect->releaseResources();
        splitter.setBandCount (numBands, next.data());
        alignLatency();
        startFadeIn();
        setStatus ("Removed band " + juce::String (band + 1));
    }
    for (auto& s : removed->slots) { s->window.reset(); s->effect.reset(); }
    removed.reset();
    for (int j = 0; j < count - 1; ++j) setParameterValue (crossoverID (j), next[(size_t) j]);
    for (int p = band; p < count; ++p)
    {
        setParameterValue (bandMixID (p), getParameterValue (bandMixID (p + 1)));
        setParameterValue (bandGainID (p), getParameterValue (bandGainID (p + 1)));
    }
    setParameterValue (bandMixID (count), 100.0f);
    setParameterValue (bandGainID (count), 0.0f);
    muted.store (false);
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}
bool TriadProcessor::moveBand (int from, int to)
{
    const int count = getBandCount();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return false;
    std::vector<int> order ((size_t) count);
    std::iota (order.begin(), order.end(), 0);
    order.erase (order.begin() + from);
    order.insert (order.begin() + to, from);   // new position i holds old band order[i]
    std::array<float, triad::maxBands> mixes {}, gains {};
    for (int i = 0; i < count; ++i) { mixes[(size_t) i] = getParameterValue (bandMixID (i)); gains[(size_t) i] = getParameterValue (bandGainID (i)); }
    {
        const juce::ScopedLock lock (gate);
        muted.store (true);
        std::array<std::unique_ptr<Band>, triad::maxBands> moved;
        for (int i = 0; i < count; ++i) moved[(size_t) i] = std::move (bands[(size_t) order[(size_t) i]]);
        for (int i = 0; i < count; ++i) bands[(size_t) i] = std::move (moved[(size_t) i]);
        resetDelays();
        alignLatency();
        startFadeIn();
        setStatus ("Moved band " + juce::String (from + 1) + " to band " + juce::String (to + 1));
    }
    for (int i = 0; i < count; ++i)
    {
        setParameterValue (bandMixID (i), mixes[(size_t) order[(size_t) i]]);
        setParameterValue (bandGainID (i), gains[(size_t) order[(size_t) i]]);
    }
    muted.store (false);
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}

// ---- Slots --------------------------------------------------------------------------------------------
void TriadProcessor::publishInfo()
{
    std::array<std::vector<SlotInfo>, triad::maxBands> next;
    for (int b = 0; b < numBands; ++b)
        for (auto& slotPtr : bands[(size_t) b]->slots)
        {
            const auto& s = *slotPtr;
            SlotInfo n;
            n.name = s.description.name;
            n.maker = s.description.manufacturerName;
            n.status = s.error;
            n.format = s.description.pluginFormatName == "AudioUnit" ? "AU" : s.description.pluginFormatName;
            n.loaded = s.effect != nullptr;
            n.hasContent = s.hasContent() || s.pendingRequest != 0;
            n.enabled = s.enabled;
            n.pending = s.pendingRequest != 0;
            n.latency = s.latency;
            n.mix = s.mixPercent.load();
            next[(size_t) b].push_back (std::move (n));
        }
    const juce::ScopedLock lock (infoLock);
    info = std::move (next);
}
int TriadProcessor::getSlotCount (int band) const
{
    const juce::ScopedLock lock (infoLock);
    return band >= 0 && band < triad::maxBands ? (int) info[(size_t) band].size() : 0;
}
TriadProcessor::SlotInfo TriadProcessor::getSlotInfo (int band, int slot) const
{
    SlotInfo result;
    {
        const juce::ScopedLock lock (infoLock);
        if (band < 0 || band >= triad::maxBands || slot < 0 || slot >= (int) info[(size_t) band].size()) return {};
        result = info[(size_t) band][(size_t) slot];
    }
    if (juce::MessageManager::existsAndIsCurrentThread())
        if (auto* s = slotAt (band, slot))
        {
            result.mix = s->mixPercent.load();
            result.modulated = std::any_of (s->modulators.begin(), s->modulators.end(), [] (Route* r) { return ! r->muted.load(); });
            result.liveMix = result.modulated && s->isActive() ? s->liveMix.load() : result.mix;
        }
    return result;
}
juce::String TriadProcessor::getStatus() const
{
    const juce::ScopedLock lock (infoLock);
    return restoring.load() ? "Restoring effects..." : status;
}
void TriadProcessor::setStatus (const juce::String& text)
{
    const juce::ScopedLock lock (infoLock);
    status = text;
}
int TriadProcessor::addSlot (int band)
{
    int index = -1;
    {
        const juce::ScopedLock lock (gate);
        if (band < 0 || band >= numBands) return -1;
        auto& b = *bands[(size_t) band];
        prepareSlot (b.appendSlot());
        b.ensureModulation();
        b.relink();
        index = (int) b.slots.size() - 1;
        publishInfo();
    }
    ++structureVersion;
    stateChanged();
    return index;
}
void TriadProcessor::removeSlot (int band, int slot)
{
    std::unique_ptr<Slot> removed;
    {
        const juce::ScopedLock lock (gate);
        if (slotAt (band, slot) == nullptr) return;
        auto& b = *bands[(size_t) band];
        removed = std::move (b.slots[(size_t) slot]);
        b.slots.erase (b.slots.begin() + slot);
        // Its own LFO goes with it; routes to it are dropped by relink().
        b.lfos.erase (std::remove_if (b.lfos.begin(), b.lfos.end(), [&] (auto& l) { return ! l->bandLfo && l->slotId == removed->id; }), b.lfos.end());
        if (b.slots.empty()) prepareSlot (b.appendSlot());
        b.ensureModulation();
        b.relink();
        if (removed->effect && prepared) removed->effect->releaseResources();
        alignLatency();
        setStatus (removed->description.name.isNotEmpty() ? "Removed " + removed->description.name : "Removed slot");
    }
    removed->window.reset();   // an editor must be deleted before its processor
    removed->effect.reset();
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
}
juce::String TriadProcessor::loadEffect (int band, int slot, const juce::PluginDescription& desc, bool openEditor)
{
    juce::String result;
    if (isTriad (desc)) result = "Triad cannot host itself.";
    else if (desc.isInstrument) result = "Choose an audio effect, not an instrument.";
    else if (band < 0 || band >= getBandCount()) result = "That band does not exist.";
    else
    {
        const bool append = slot == getSlotCount (band);
        if (append) slot = addSlot (band);
        if (slot < 0 || slot >= getSlotCount (band)) result = "That slot does not exist.";
        else
        {
            result = requestInstall (band, slot, desc, {}, openEditor, false);
            if (result.isNotEmpty() && append) removeSlot (band, slot);
        }
    }
    setStatus (result.isEmpty() ? "Loaded " + desc.name : result);
    return result;
}
juce::String TriadProcessor::loadFile (int band, int slot, const juce::File& file, bool openEditor, const juce::String& preferredName)
{
    juce::OwnedArray<juce::PluginDescription> descriptions;
    for (auto* format : formats.getFormats())
        if (format->fileMightContainThisPluginType (file.getFullPathName()))
            format->findAllTypesForFile (descriptions, file.getFullPathName());
    std::vector<juce::PluginDescription*> order;
    for (auto* d : descriptions) if (d->name == preferredName) order.push_back (d);
    for (auto* d : descriptions) if (d->name != preferredName) order.push_back (d);
    juce::String result = "No usable effect was found in this file.";
    for (auto* d : order)
        if (! d->isInstrument)
        {
            result = loadEffect (band, slot, *d, openEditor);
            if (result.isEmpty()) break;
        }
    setStatus (result.isEmpty() ? "Effect loaded" : result);
    return result;
}
juce::String TriadProcessor::requestInstall (int band, int slot, const juce::PluginDescription& desc,
                                             const juce::MemoryBlock& state, bool openEditor, bool restoringSlot)
{
    if (slotAt (band, slot) == nullptr) return "That slot does not exist.";
    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == desc.pluginFormatName) format = f;
    if (format == nullptr)
        return "The " + desc.pluginFormatName + " plug-in format is not available here.";
    double sr; int block;
    { const juce::ScopedLock lock (gate); sr = rate.load(); block = capacity; }
    if (! format->requiresUnblockedMessageThreadDuringCreation (desc))
    {
        juce::String error;
        auto instance = formats.createPluginInstance (desc, sr, block, error);
        return finishInstall (band, slot, desc, state, std::move (instance), error, openEditor);
    }
    // Plug-ins such as AUv3 must be created asynchronously; the slot shows as loading meanwhile.
    juce::int64 request = 0;
    {
        const juce::ScopedLock lock (gate);
        request = nextRequest++;
        auto* s = slotAt (band, slot);
        s->pendingRequest = request;
        if (s->effect == nullptr) { s->description = desc; s->savedState = state; }
        publishInfo();
    }
    if (restoringSlot) ++pendingCreations;
    juce::WeakReference<TriadProcessor> weak (this);
    formats.createPluginInstanceAsync (desc, sr, block,
        [weak, request, desc, state, openEditor, restoringSlot] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            auto* self = weak.get();
            if (self == nullptr) return;
            int b = 0, s = 0;
            if (self->findPending (request, b, s))
            {
                auto failure = self->finishInstall (b, s, desc, state, std::move (instance), error, openEditor);
                const juce::ScopedLock lock (self->gate);
                if (auto* slotRef = self->slotAt (b, s))
                {
                    slotRef->pendingRequest = 0;
                    if (failure.isNotEmpty())
                    {
                        slotRef->description = desc; slotRef->savedState = state;
                        slotRef->error = (restoringSlot ? "Missing/unavailable: " : "") + failure;
                        self->setStatus (failure);
                    }
                    self->alignLatency();
                }
            }
            if (restoringSlot && --self->pendingCreations == 0)
            {
                const juce::ScopedLock lock (self->stateGate);
                if (self->pendingState == nullptr) self->restoring.store (false);
            }
        });
    return {};
}
bool TriadProcessor::findPending (juce::int64 request, int& band, int& slot) const
{
    const juce::ScopedLock lock (gate);
    for (int b = 0; b < numBands; ++b)
        for (int s = 0; s < (int) bands[(size_t) b]->slots.size(); ++s)
            if (bands[(size_t) b]->slots[(size_t) s]->pendingRequest == request) { band = b; slot = s; return true; }
    return false;
}
juce::String TriadProcessor::finishInstall (int band, int slotIndex, const juce::PluginDescription& desc, const juce::MemoryBlock& state,
                                            std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& creationError,
                                            bool openEditor)
{
    if (! instance) return creationError.isNotEmpty() ? creationError : "Could not open this effect.";
    const auto actual = instance->getPluginDescription();
    if (isTriad (actual)) return "Triad cannot host itself.";
    if (desc.isInstrument || actual.isInstrument) return "Choose an audio effect, not an instrument.";
    instance->disableNonMainBuses();
    auto layout = instance->getBusesLayout();
    for (int j = 1; j < layout.inputBuses.size(); ++j) layout.inputBuses.getReference (j) = juce::AudioChannelSet::disabled();
    for (int j = 1; j < layout.outputBuses.size(); ++j) layout.outputBuses.getReference (j) = juce::AudioChannelSet::disabled();
    if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty()) return "This effect has no audio input or output.";
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! instance->setBusesLayout (layout)) return "This version supports stereo-in / stereo-out effects.";
    if (state.getSize() > 0) instance->setStateInformation (state.getData(), (int) state.getSize());
    if (instance->getTotalNumInputChannels() != 2 || instance->getTotalNumOutputChannels() != 2)
        return "The effect changed to an unsupported channel layout.";
    std::unique_ptr<juce::AudioPluginInstance> old;
    std::unique_ptr<juce::DocumentWindow> oldWindow;
    {
        const juce::ScopedLock lock (gate);
        auto* s = slotAt (band, slotIndex);
        if (s == nullptr) return "That slot no longer exists.";
        instance->setRateAndBufferSizeDetails (rate.load(), capacity);
        if (prepared) instance->prepareToPlay (rate.load(), capacity);
        oldWindow = std::move (s->window);
        if (s->effect && prepared) s->effect->releaseResources();
        old = std::move (s->effect);
        s->effect = std::move (instance);
        s->description = actual.name.isNotEmpty() ? actual : desc;
        // Keep the location the effect was opened from (an instance may report the binary inside its bundle).
        if (desc.fileOrIdentifier.isNotEmpty()) s->description.fileOrIdentifier = desc.fileOrIdentifier;
        s->savedState = state;
        s->error.clear();
        s->pendingRequest = 0;
        s->dryDelay.reset();
        alignLatency();
    }
    oldWindow.reset();   // an editor must be deleted before its processor
    old.reset();
    stateChanged();
    if (openEditor) showEffect (band, slotIndex);
    return {};
}
void TriadProcessor::removeEffect (int band, int slot)
{
    std::unique_ptr<juce::AudioPluginInstance> old;
    std::unique_ptr<juce::DocumentWindow> window;
    {
        const juce::ScopedLock lock (gate);
        auto* s = slotAt (band, slot);
        if (s == nullptr) return;
        window = std::move (s->window);
        if (s->effect && prepared) s->effect->releaseResources();
        old = std::move (s->effect);
        s->description = {}; s->savedState.reset(); s->error.clear();
        s->pendingRequest = 0; s->enabled = true;
        s->dryDelay.reset();
        alignLatency();
        setStatus ("Effect removed");
    }
    window.reset();
    old.reset();
    stateChanged();
}
void TriadProcessor::setSlotEnabled (int band, int slot, bool enabled)
{
    {
        const juce::ScopedLock lock (gate);
        auto* s = slotAt (band, slot);
        if (s == nullptr) return;
        s->enabled = enabled;
        s->dryDelay.reset();
        alignLatency();
        setStatus (juce::String (enabled ? "Enabled " : "Bypassed ") + (s->description.name.isNotEmpty() ? s->description.name : "slot"));
    }
    stateChanged();
}
void TriadProcessor::setSlotMix (int band, int slot, float percent)
{
    auto* s = slotAt (band, slot);   // message thread: the slot list cannot change underneath us
    if (s == nullptr) return;
    percent = juce::jlimit (0.0f, 100.0f, percent);
    if (juce::exactlyEqual (s->mixPercent.exchange (percent), percent)) return;
    {
        const juce::ScopedLock lock (infoLock);
        if (slot < (int) info[(size_t) band].size()) info[(size_t) band][(size_t) slot].mix = percent;
    }
    stateChanged();
}
float TriadProcessor::getSlotMix (int band, int slot) const { return getSlotInfo (band, slot).mix; }
bool TriadProcessor::moveSlot (int band, int slot, int direction)
{
    const int target = slot + (direction < 0 ? -1 : 1);
    {
        const juce::ScopedLock lock (gate);
        if (slotAt (band, slot) == nullptr || slotAt (band, target) == nullptr) return false;
        auto& b = *bands[(size_t) band];
        std::swap (b.slots[(size_t) slot], b.slots[(size_t) target]);   // the mix knob and LFO travel with the effect
        b.slots[(size_t) slot]->dryDelay.reset();
        b.slots[(size_t) target]->dryDelay.reset();
        b.relink();
        alignLatency();
    }
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}
bool TriadProcessor::sendKeyToHost()
{
    if (auto* editor = getActiveEditor()) return triad::platform::sendKeyToHost (*editor);
    return false;
}
void TriadProcessor::showEffect (int band, int slot)
{
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    {
        setStatus ("Cannot open effect editor: no display is available.");
        return;
    }
    auto* s = slotAt (band, slot);
    if (s == nullptr || ! s->effect) return;
    auto* triadWindow = getActiveEditor();
    if (! s->window)
    {
        juce::WeakReference<TriadProcessor> weak (this);
        s->window = std::make_unique<EffectWindow> (*s->effect, windowTitle (band, slot, s->description.name),
                                                    [weak] { return weak != nullptr && weak->sendKeyToHost(); });
        // Open over Triad, so Triad is visible behind it.
        if (triadWindow != nullptr && triadWindow->isShowing())
            s->window->centreAroundComponent (triadWindow, s->window->getWidth(), s->window->getHeight());
        else
            s->window->centreWithSize (s->window->getWidth(), s->window->getHeight());
    }
    s->window->setVisible (true);
    triad::platform::keepAbove (*s->window, triadWindow);
    s->window->toFront (true);
}
void TriadProcessor::refreshWindowTitles()
{
    for (int b = 0; b < triad::maxBands; ++b)
        for (int s = 0; s < (int) bands[(size_t) b]->slots.size(); ++s)
            if (auto& w = bands[(size_t) b]->slots[(size_t) s]->window)
                w->setName (windowTitle (b, s, bands[(size_t) b]->slots[(size_t) s]->description.name));
}
void TriadProcessor::closeWindows()
{
    for (auto& band : bands)
        for (auto& s : band->slots) s->window.reset();
}

// ---- LFOs ------------------------------------------------------------------------------------------------
int TriadProcessor::getLfoCount (int band) const
{
    return band >= 0 && band < getBandCount() ? (int) bands[(size_t) band]->lfos.size() : 0;
}
TriadProcessor::LfoInfo TriadProcessor::getLfoInfo (int band, int index) const
{
    LfoInfo i;
    auto* l = lfoAt (band, index);
    if (l == nullptr) return i;
    const auto& b = *bands[(size_t) band];
    i.id = l->id;
    i.name = "LFO " + juce::String (index + 1);
    i.length = l->length.load();
    i.shape = l->shape;
    i.bandLfo = l->bandLfo;
    i.slot = l->bandLfo ? -1 : b.slotIndex (l->slotId);
    i.canUndo = ! l->undo.empty();
    i.canRedo = ! l->redo.empty();
    juce::StringArray targets;
    for (auto& r : b.routes)
        if (r->lfoId == l->id)
        {
            i.active |= ! r->muted.load();
            if (! r->muted.load()) targets.addIfNotAlreadyThere (destinationName (b, r->destination));
        }
    if (l->bandLfo) i.target = "Band Mix";
    else if (i.slot >= 0)
    {
        const auto& s = *b.slots[(size_t) i.slot];
        i.target = s.description.name.isNotEmpty() ? s.description.name : "Slot " + juce::String (i.slot + 1);
    }
    else i.target = targets.isEmpty() ? juce::String ("Not routed") : targets.joinIntoString (", ");
    return i;
}
int TriadProcessor::getSlotLfo (int band, int slot) const
{
    auto* s = slotAt (band, slot);
    if (s == nullptr) return -1;
    const auto& lfos = bands[(size_t) band]->lfos;
    for (size_t i = 0; i < lfos.size(); ++i) if (! lfos[i]->bandLfo && lfos[i]->slotId == s->id) return (int) i;
    return -1;
}
int TriadProcessor::getBandLfo (int band) const
{
    if (band < 0 || band >= getBandCount()) return -1;
    const auto& lfos = bands[(size_t) band]->lfos;
    for (size_t i = 0; i < lfos.size(); ++i) if (lfos[i]->bandLfo) return (int) i;
    return -1;
}
void TriadProcessor::beginLfoEdit (int band, int index)
{
    auto* l = lfoAt (band, index);
    if (l == nullptr) return;
    l->undo.push_back (l->shape);
    if (l->undo.size() > 200) l->undo.erase (l->undo.begin());
    l->redo.clear();
    stateChanged();
}
void TriadProcessor::setLfoShape (int band, int index, const triad::mod::Shape& shape, bool undoable)
{
    auto* l = lfoAt (band, index);
    if (l == nullptr) return;
    if (undoable) beginLfoEdit (band, index);
    auto s = shape;
    triad::mod::normalise (s);
    l->shape = std::move (s);
    l->table.render (l->shape);
    if (! l->touched)
    {
        // Drawing an LFO for the first time switches on the routes Triad made for it.
        l->touched = true;
        for (auto& r : bands[(size_t) band]->routes)
            if (r->lfoId == l->id && r->automatic) r->muted.store (false);
    }
    if (undoable) stateChanged();
}
bool TriadProcessor::undoLfo (int band, int index)
{
    auto* l = lfoAt (band, index);
    if (l == nullptr || l->undo.empty()) return false;
    l->redo.push_back (l->shape);
    l->shape = l->undo.back();
    l->undo.pop_back();
    l->table.render (l->shape);
    stateChanged();
    return true;
}
bool TriadProcessor::redoLfo (int band, int index)
{
    auto* l = lfoAt (band, index);
    if (l == nullptr || l->redo.empty()) return false;
    l->undo.push_back (l->shape);
    l->shape = l->redo.back();
    l->redo.pop_back();
    l->table.render (l->shape);
    stateChanged();
    return true;
}
void TriadProcessor::setLfoLength (int band, int index, int length)
{
    if (auto* l = lfoAt (band, index))
    {
        l->length.store (juce::jlimit (0, (int) triad::mod::lengths().size() - 1, length));
        stateChanged();
    }
}
int TriadProcessor::addLfo (int band)
{
    int index = -1;
    {
        const juce::ScopedLock lock (gate);
        if (band < 0 || band >= numBands) return -1;
        auto& b = *bands[(size_t) band];
        const int id = b.appendLfo (0, false).id;
        b.relink();
        index = b.lfoIndex (id);
    }
    stateChanged();
    return index;
}
bool TriadProcessor::removeLfo (int band, int index)
{
    {
        const juce::ScopedLock lock (gate);
        auto* l = lfoAt (band, index);
        if (l == nullptr || l->bandLfo || l->slotId != 0) return false;
        auto& b = *bands[(size_t) band];
        b.lfos.erase (b.lfos.begin() + index);
        b.relink();
    }
    stateChanged();
    return true;
}
float TriadProcessor::getLfoPhase (int band, int index) const
{
    auto* l = lfoAt (band, index);
    return l != nullptr && prepared ? l->displayPhase.load() : -1.0f;
}

// ---- Matrix ------------------------------------------------------------------------------------------------
juce::String TriadProcessor::destinationName (const Band& b, const Destination& d) const
{
    if (d.kind == Destination::bandMix) return "Band Mix";
    if (d.kind == Destination::bandVolume) return "Band Volume";
    const int index = b.slotIndex (d.slotId);
    if (index < 0) return "(removed slot)";
    // "2 · KHS Distortion Mix": the slot number tells two copies of one effect apart.
    const auto& name = b.slots[(size_t) index]->description.name;
    return name.isNotEmpty() ? juce::String (index + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + name + " Mix"
                             : "Slot " + juce::String (index + 1) + " Mix";
}
int TriadProcessor::getRouteCount (int band) const
{
    return band >= 0 && band < getBandCount() ? (int) bands[(size_t) band]->routes.size() : 0;
}
TriadProcessor::RouteInfo TriadProcessor::getRouteInfo (int band, int index) const
{
    RouteInfo i;
    auto* r = routeAt (band, index);
    if (r == nullptr) return i;
    const auto& b = *bands[(size_t) band];
    i.lfo = b.lfoIndex (r->lfoId);
    i.destination = r->destination;
    i.amount = r->amount.load();
    i.invert = r->invert.load();
    i.muted = r->muted.load();
    i.sourceName = "LFO " + juce::String (i.lfo + 1);
    i.destinationName = destinationName (b, r->destination);
    return i;
}
std::vector<int> TriadProcessor::getRoutesFrom (int band, int lfo) const
{
    std::vector<int> result;
    auto* l = lfoAt (band, lfo);
    if (l == nullptr) return result;
    const auto& routes = bands[(size_t) band]->routes;
    for (size_t i = 0; i < routes.size(); ++i) if (routes[i]->lfoId == l->id) result.push_back ((int) i);
    return result;
}
int TriadProcessor::addRoute (int band, int lfo, Destination destination)
{
    int index = -1;
    {
        const juce::ScopedLock lock (gate);
        auto* l = lfoAt (band, lfo);
        if (l == nullptr) return -1;
        auto& b = *bands[(size_t) band];
        if (destination.kind == Destination::slotMix && b.findSlot (destination.slotId) == nullptr) return -1;
        auto r = std::make_unique<Route>();
        r->lfoId = l->id;
        r->destination = destination;
        r->muted.store (false);
        b.routes.push_back (std::move (r));
        b.relink();
        index = (int) b.routes.size() - 1;
    }
    stateChanged();
    return index;
}
void TriadProcessor::setRouteSource (int band, int route, int lfo)
{
    {
        const juce::ScopedLock lock (gate);
        auto* r = routeAt (band, route);
        auto* l = lfoAt (band, lfo);
        if (r == nullptr || l == nullptr) return;
        r->lfoId = l->id;
        r->automatic = false;
        bands[(size_t) band]->relink();
    }
    stateChanged();
}
void TriadProcessor::setRouteDestination (int band, int route, Destination destination)
{
    {
        const juce::ScopedLock lock (gate);
        auto* r = routeAt (band, route);
        if (r == nullptr) return;
        auto& b = *bands[(size_t) band];
        if (destination.kind == Destination::slotMix && b.findSlot (destination.slotId) == nullptr) return;
        r->destination = destination;
        r->automatic = false;
        b.relink();
    }
    stateChanged();
}
void TriadProcessor::setRouteAmount (int band, int route, float amount)
{
    if (auto* r = routeAt (band, route)) { r->amount.store (juce::jlimit (0.0f, 1.0f, amount)); stateChanged(); }
}
void TriadProcessor::setRouteInvert (int band, int route, bool invert)
{
    if (auto* r = routeAt (band, route)) { r->invert.store (invert); stateChanged(); }
}
void TriadProcessor::setRouteMuted (int band, int route, bool isMuted)
{
    if (auto* r = routeAt (band, route))
    {
        r->muted.store (isMuted);
        if (! isMuted) if (auto* l = bands[(size_t) band]->findLfo (r->lfoId)) l->touched = true;
        stateChanged();
    }
}
void TriadProcessor::removeRoute (int band, int route)
{
    {
        const juce::ScopedLock lock (gate);
        if (routeAt (band, route) == nullptr) return;
        auto& b = *bands[(size_t) band];
        b.routes.erase (b.routes.begin() + route);
        b.relink();
    }
    stateChanged();
}
std::vector<std::pair<TriadProcessor::Destination, juce::String>> TriadProcessor::getDestinations (int band) const
{
    std::vector<std::pair<Destination, juce::String>> list;
    if (band < 0 || band >= getBandCount()) return list;
    const auto& b = *bands[(size_t) band];
    list.push_back ({ Destination { Destination::bandMix, 0 }, "Band Mix" });
    list.push_back ({ Destination { Destination::bandVolume, 0 }, "Band Volume" });
    for (auto& s : b.slots) list.push_back ({ Destination { Destination::slotMix, s->id }, destinationName (b, Destination { Destination::slotMix, s->id }) });
    return list;
}

// ---- State ----------------------------------------------------------------------------------------------
// Version 4: <TriadState version="4" bands="n" preset=""> <Parameters/>
//   <Band index slots nextSlot nextLfo> <Slot index id enabled mix> <PLUGIN/> <State/> </Slot>
//     <Lfo id slot band length touched> <Shape> <P x y c/> </Shape> </Lfo>
//     <Route lfo kind slot amount invert muted auto/> </Band> </TriadState>
// Versions 1 (Triad 0.1), 2 (0.2.0/0.2.1) and 3 (0.2.2) still load.
void TriadProcessor::writeBand (juce::XmlElement& node, const Band& band) const
{
    node.setAttribute ("slots", (int) band.slots.size());
    node.setAttribute ("nextSlot", band.nextSlotId);
    node.setAttribute ("nextLfo", band.nextLfoId);
    for (int i = 0; i < (int) band.slots.size(); ++i)
    {
        auto& s = *band.slots[(size_t) i];
        auto* x = node.createNewChildElement ("Slot");
        x->setAttribute ("index", i);
        x->setAttribute ("id", s.id);
        x->setAttribute ("enabled", s.enabled);
        x->setAttribute ("mix", (double) s.mixPercent.load());
        if (s.description.name.isEmpty()) continue;
        x->addChildElement (s.description.createXml().release());
        if (s.effect) s.effect->getStateInformation (s.savedState);
        x->createNewChildElement ("State")->addTextElement (s.savedState.toBase64Encoding());
    }
    for (auto& l : band.lfos)
    {
        auto* x = node.createNewChildElement ("Lfo");
        x->setAttribute ("id", l->id);
        x->setAttribute ("slot", l->slotId);
        x->setAttribute ("band", l->bandLfo);
        x->setAttribute ("length", l->length.load());
        x->setAttribute ("touched", l->touched);
        x->addChildElement (triad::mod::toXml (l->shape, "Shape").release());
    }
    for (auto& r : band.routes)
    {
        auto* x = node.createNewChildElement ("Route");
        x->setAttribute ("lfo", r->lfoId);
        x->setAttribute ("kind", r->destination.kind);
        x->setAttribute ("slot", r->destination.slotId);
        x->setAttribute ("amount", (double) r->amount.load());
        x->setAttribute ("invert", r->invert.load());
        x->setAttribute ("muted", r->muted.load());
        x->setAttribute ("auto", r->automatic);
    }
}
std::unique_ptr<TriadProcessor::Band> TriadProcessor::readBand (const juce::XmlElement* node, int version,
                                                                const std::function<float (int)>& legacyMix,
                                                                std::vector<PendingLoad>& loads) const
{
    auto band = std::make_unique<Band> (false);
    int count = node != nullptr ? juce::jlimit (1, 4096, node->getIntAttribute ("slots", 1)) : 1;
    if (node != nullptr)
        for (auto* s : node->getChildWithTagNameIterator ("Slot"))
            if (const int index = s->getIntAttribute ("index", -1); index >= 0 && index < 4096) count = juce::jmax (count, index + 1);
    for (int i = 0; i < count; ++i) band->appendSlot();
    if (version == 2)
        for (int i = 0; i < count; ++i) band->slots[(size_t) i]->mixPercent.store (legacyMix (i));
    std::map<int, int> slotIds;   // index -> saved id
    if (node != nullptr)
        for (auto* s : node->getChildWithTagNameIterator ("Slot"))
        {
            const int index = s->getIntAttribute ("index", -1);
            if (index < 0 || index >= count) continue;
            auto& slot = *band->slots[(size_t) index];
            slot.enabled = s->getBoolAttribute ("enabled", true);
            if (s->hasAttribute ("mix")) slot.mixPercent.store (juce::jlimit (0.0f, 100.0f, (float) s->getDoubleAttribute ("mix")));
            if (version >= 4 && s->getIntAttribute ("id") > 0) slotIds[index] = s->getIntAttribute ("id");
            if (s->getChildByName ("PLUGIN") != nullptr) loads.push_back ({ index, s });
        }
    // Ids from the file where they are valid and unique; fresh ones otherwise.
    std::set<int> used;
    for (auto& [index, id] : slotIds) if (used.insert (id).second) band->slots[(size_t) index]->id = id;
    int nextId = used.empty() ? 1 : *used.rbegin() + 1;
    for (int i = 0; i < count; ++i)
        if (slotIds.count (i) == 0 || band->slots[(size_t) i]->id != slotIds[i]) band->slots[(size_t) i]->id = nextId++;
    band->nextSlotId = juce::jmax (nextId, node != nullptr ? node->getIntAttribute ("nextSlot", 1) : 1);
    if (version >= 4 && node != nullptr)
    {
        std::set<int> lfoIds;
        for (auto* x : node->getChildWithTagNameIterator ("Lfo"))
        {
            const int id = x->getIntAttribute ("id");
            if (id <= 0 || ! lfoIds.insert (id).second || band->lfos.size() >= 4096) continue;
            band->lfos.push_back (std::make_unique<Lfo>());
            auto& l = *band->lfos.back();
            l.id = id;
            l.slotId = x->getIntAttribute ("slot");
            l.bandLfo = x->getBoolAttribute ("band");
            l.touched = x->getBoolAttribute ("touched");
            l.length.store (juce::jlimit (0, (int) triad::mod::lengths().size() - 1, x->getIntAttribute ("length", triad::mod::defaultLength())));
            l.shape = triad::mod::fromXml (x->getChildByName ("Shape"));
            l.table.render (l.shape);
        }
        band->nextLfoId = juce::jmax (lfoIds.empty() ? 1 : *lfoIds.rbegin() + 1, node->getIntAttribute ("nextLfo", 1));
        for (auto* x : node->getChildWithTagNameIterator ("Route"))
        {
            if (band->routes.size() >= 4096) break;
            auto r = std::make_unique<Route>();
            r->lfoId = x->getIntAttribute ("lfo");
            r->destination.kind = juce::jlimit (0, 2, x->getIntAttribute ("kind", Destination::bandMix));
            r->destination.slotId = x->getIntAttribute ("slot");
            r->amount.store (juce::jlimit (0.0f, 1.0f, (float) x->getDoubleAttribute ("amount", 1.0)));
            r->invert.store (x->getBoolAttribute ("invert"));
            r->muted.store (x->getBoolAttribute ("muted", true));
            r->automatic = x->getBoolAttribute ("auto");
            band->routes.push_back (std::move (r));
        }
    }
    band->ensureModulation();
    band->relink();
    return band;
}
std::unique_ptr<juce::XmlElement> TriadProcessor::createPresetXml()
{
    const juce::ScopedLock lock (gate);
    auto root = std::make_unique<juce::XmlElement> ("TriadState");
    root->setAttribute ("version", 4);
    root->setAttribute ("bands", numBands);
    root->setAttribute ("preset", getPresetName());
    root->addChildElement (parameters.copyState().createXml().release());
    for (int b = 0; b < numBands; ++b)
    {
        auto* bandNode = root->createNewChildElement ("Band");
        bandNode->setAttribute ("index", b);
        writeBand (*bandNode, *bands[(size_t) b]);
    }
    return root;
}
void TriadProcessor::getStateInformation (juce::MemoryBlock& output)
{
    {
        const juce::ScopedLock lock (stateGate);
        if (pendingState) { copyXmlToBinary (*pendingState, output); return; }
    }
    // Match the VST3 host's message-lock-before-state-lock order. In particular,
    // do not hold gate while waiting for the GUI thread during a background save.
    const juce::MessageManagerLock messageLock;
    copyXmlToBinary (*createPresetXml(), output);
}
void TriadProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("TriadState")) return;
    const auto version = xml->getIntAttribute ("version");
    if (version < 1 || version > 4) return;
    {
        const juce::ScopedLock lock (stateGate);
        pendingState = std::move (xml); restoring.store (true);
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}
void TriadProcessor::loadPresetXml (const juce::XmlElement& xml)
{
    juce::MemoryBlock data;
    copyXmlToBinary (xml, data);
    setStateInformation (data.getData(), (int) data.getSize());
}
juce::String TriadProcessor::getPresetName() const
{
    const juce::ScopedLock lock (infoLock);
    return presetName;
}
void TriadProcessor::setPresetName (const juce::String& name)
{
    const juce::ScopedLock lock (infoLock);
    presetName = name;
}
void TriadProcessor::installFromXml (int band, int slot, const juce::XmlElement& node, bool restoringSlot)
{
    auto* descXml = node.getChildByName ("PLUGIN");
    if (slotAt (band, slot) == nullptr || descXml == nullptr) return;
    juce::PluginDescription desc;
    if (! desc.loadFromXml (*descXml)) return;
    juce::MemoryBlock saved;
    if (auto* st = node.getChildByName ("State")) saved.fromBase64Encoding (st->getAllSubText());
    auto error = isTriad (desc) ? juce::String ("Triad cannot host itself.") : requestInstall (band, slot, desc, saved, false, restoringSlot);
    const juce::ScopedLock lock (gate);
    if (auto* s = slotAt (band, slot); s != nullptr && error.isNotEmpty())
    {
        s->description = desc;
        s->savedState = saved;
        s->error = "Missing/unavailable: " + error;
    }
    alignLatency();
}
void TriadProcessor::restoreFrom (const juce::XmlElement& xml)
{
    const int version = xml.getIntAttribute ("version");
    const int count = version >= 2 ? juce::jlimit (1, triad::maxBands, xml.getIntAttribute ("bands", 3)) : 3;
    std::array<std::unique_ptr<Band>, triad::maxBands> old;
    {
        const juce::ScopedLock lock (gate);
        for (int b = 0; b < triad::maxBands; ++b)
        {
            old[(size_t) b] = std::move (bands[(size_t) b]);
            bands[(size_t) b] = std::make_unique<Band>();
            if (prepared)
                for (auto& s : old[(size_t) b]->slots) if (s->effect) s->effect->releaseResources();
        }
        publishInfo();
    }
    for (auto& band : old)
        for (auto& s : band->slots) { s->window.reset(); s->effect.reset(); }
    old = {};
    // Parameters, minus any that no longer exist (Triad 0.2.0 had per-slot mix parameters).
    juce::ValueTree savedParameters;
    if (auto* p = xml.getChildByName ("Parameters"))
    {
        auto tree = juce::ValueTree::fromXml (*p);
        savedParameters = tree.createCopy();
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (parameters.getParameter (tree.getChild (i).getProperty ("id").toString()) == nullptr)
                tree.removeChild (i, nullptr);
        parameters.replaceState (tree);
    }
    std::array<std::unique_ptr<Band>, triad::maxBands> fresh;
    std::array<std::vector<PendingLoad>, triad::maxBands> loads;
    if (version == 1)
    {
        for (auto* node : xml.getChildWithTagNameIterator ("Slot"))
            if (const int b = node->getIntAttribute ("index", -1); b >= 0 && b < count) loads[(size_t) b].push_back ({ 0, node });
    }
    else
    {
        for (int b = 0; b < count; ++b)
        {
            const juce::XmlElement* bandNode = nullptr;
            for (auto* node : xml.getChildWithTagNameIterator ("Band"))
                if (node->getIntAttribute ("index", -1) == b) { bandNode = node; break; }
            auto legacyMix = [&savedParameters, b] (int s)
            {
                const auto child = savedParameters.getChildWithProperty ("id", "slot" + juce::String (b) + "_" + juce::String (s));
                return child.isValid() ? (float) child.getProperty ("value", 100.0) : 100.0f;
            };
            fresh[(size_t) b] = readBand (bandNode, version, legacyMix, loads[(size_t) b]);
        }
    }
    for (auto& f : fresh) if (f == nullptr) f = std::make_unique<Band>();
    setPresetName (xml.getStringAttribute ("preset"));
    {
        const juce::ScopedLock lock (gate);
        numBands = count;
        bandCount.store (count);
        for (int b = 0; b < triad::maxBands; ++b) bands[(size_t) b] = std::move (fresh[(size_t) b]);
        std::array<float, triad::maxCrossovers> cuts {};
        readCrossovers (cuts.data());
        splitter.setBandCount (numBands, cuts.data());
        for (int b = 0; b < triad::maxBands; ++b) prepareBand (b, false);
        alignLatency();
    }
    ++structureVersion;
    for (int b = 0; b < count; ++b)
        for (auto& load : loads[(size_t) b]) installFromXml (b, load.slot, *load.node, true);
}
void TriadProcessor::handleAsyncUpdate()
{
    std::unique_ptr<juce::XmlElement> xml;
    {
        const juce::ScopedLock lock (stateGate);
        if (! pendingState) return;
        xml = std::make_unique<juce::XmlElement> (*pendingState);
    }
    restoreFrom (*xml);
    {
        const juce::ScopedLock lock (stateGate);
        // A second restore request may have arrived while a plugin was loading.
        if (pendingState && pendingState->isEquivalentTo (xml.get(), false))
        {
            pendingState.reset();
            if (pendingCreations.load() == 0) restoring.store (false);
        }
        else triggerAsyncUpdate();
    }
    setStatus (getPresetName().isNotEmpty() ? "Loaded preset " + getPresetName() : "Project restored");
}

// ---- Chain presets ------------------------------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> TriadProcessor::createChainXml (int band)
{
    if (band < 0 || band >= getBandCount()) return nullptr;
    auto root = std::make_unique<juce::XmlElement> ("TriadChain");
    root->setAttribute ("version", 1);
    root->setAttribute ("bandMix", (double) getParameterValue (bandMixID (band)));
    root->setAttribute ("bandGain", (double) getParameterValue (bandGainID (band)));
    const juce::ScopedLock lock (gate);
    writeBand (*root->createNewChildElement ("Band"), *bands[(size_t) band]);
    return root;
}
juce::String TriadProcessor::loadChainXml (int band, const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("TriadChain") || xml.getChildByName ("Band") == nullptr) return "This is not a Triad chain preset.";
    if (band < 0 || band >= getBandCount()) return "That band does not exist.";
    std::vector<PendingLoad> loads;
    auto fresh = readBand (xml.getChildByName ("Band"), 4, [] (int) { return 100.0f; }, loads);
    std::unique_ptr<Band> old;
    {
        const juce::ScopedLock lock (gate);
        old = std::move (bands[(size_t) band]);
        bands[(size_t) band] = std::move (fresh);
        if (prepared)
            for (auto& s : old->slots) if (s->effect) s->effect->releaseResources();
        prepareBand (band, false);
        alignLatency();
    }
    for (auto& s : old->slots) { s->window.reset(); s->effect.reset(); }
    old.reset();
    setParameterValue (bandMixID (band), (float) xml.getDoubleAttribute ("bandMix", 100.0));
    setParameterValue (bandGainID (band), (float) xml.getDoubleAttribute ("bandGain", 0.0));
    ++structureVersion;
    for (auto& load : loads) installFromXml (band, load.slot, *load.node, false);
    refreshWindowTitles();
    stateChanged();
    return {};
}

juce::AudioProcessorEditor* TriadProcessor::createEditor() { return new TriadEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TriadProcessor(); }
