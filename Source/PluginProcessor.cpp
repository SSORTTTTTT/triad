#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
class EffectWindow final : public juce::DocumentWindow
{
public:
    EffectWindow (juce::AudioPluginInstance& p, const juce::String& title)
        : DocumentWindow (title, juce::Colour (0xff141923), closeButton)
    {
        setUsingNativeTitleBar (true);
        auto* editor = p.createEditorIfNeeded();
        if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (p);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { setVisible (false); }
};

constexpr std::array<float, triad::maxCrossovers> parameterDefaults { 250.0f, 2500.0f, 6000.0f, 9000.0f, 12000.0f,
                                                                      14000.0f, 16000.0f, 17500.0f, 19000.0f };
juce::String windowTitle (int band, int slot, const juce::String& name)
{
    return "Band " + juce::String (band + 1) + " \xc2\xb7 Slot " + juce::String (slot + 1) + " \xe2\x80\x94 " + name;
}
bool isTriad (const juce::PluginDescription& d) { return triad::isTriadName (d.name) || triad::isTriadName (d.descriptiveName); }
}

juce::String TriadProcessor::crossoverID (int i) { return i == 0 ? "lowCut" : i == 1 ? "highCut" : "cross" + juce::String (i + 1); }
juce::String TriadProcessor::bandMixID (int b) { return "mix" + juce::String (b); }

juce::AudioProcessorValueTreeState::ParameterLayout TriadProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto frequency = juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f);
    frequency.setSkewForCentre (1000.0f);
    const auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    const auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");
    // IDs from Triad 0.1 (lowCut, highCut, mix0-2) are kept so existing projects and automation recall.
    for (int i = 0; i < triad::maxCrossovers; ++i)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { crossoverID (i), i < 2 ? 1 : 2 },
              "Crossover " + juce::String (i + 1), frequency, parameterDefaults[(size_t) i], hz));
    for (int b = 0; b < triad::maxBands; ++b)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandMixID (b), b < 3 ? 1 : 2 },
              "Band " + juce::String (b + 1) + " Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));
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
void TriadProcessor::prepareSlot (Slot& slot)
{
    slot.mix.reset (rate.load(), 0.02);
    slot.mix.setCurrentAndTargetValue (slot.mixPercent.load() * 0.01f);
}
void TriadProcessor::prepareBand (int index, bool prepareEffects)
{
    auto& band = *bands[(size_t) index];
    const auto sr = rate.load();
    band.dry.setSize (2, capacity);
    band.slotDry.setSize (2, capacity);
    band.mix.reset (sr, 0.02);
    band.mix.setCurrentAndTargetValue (bandMixValues[(size_t) index]->load() * 0.01f);
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
    for (auto& band : bands)
    {
        band->dryDelay.reset(); band->alignDelay.reset();
        for (auto& s : band->slots) s->dryDelay.reset();
    }
    bypassDelay.reset();
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
    if (changed && prepared)
    {
        for (auto& band : bands)
        {
            band->dryDelay.reset(); band->alignDelay.reset();
            for (auto& s : band->slots) s->dryDelay.reset();
        }
        bypassDelay.reset();
    }
    if (getLatencySamples() != longest) setLatencySamples (longest);
    publishInfo();
}
void TriadProcessor::timerCallback()
{
    const juce::ScopedTryLock lock (gate);
    if (lock.isLocked()) alignLatency();
}
void TriadProcessor::startFadeIn() { fadeRemaining = fadeLength; }

void TriadProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals denormals;
    midi.clear();
    spectrum.push (audio, audio.getNumSamples());
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
}
void TriadProcessor::process (juce::AudioBuffer<float>& audio)
{
    std::array<float, triad::maxCrossovers> cuts {};
    readCrossovers (cuts.data());
    splitter.setCutoffs (cuts.data());
    for (int b = 0; b < numBands; ++b)
    {
        auto& band = *bands[(size_t) b];
        band.mix.setTargetValue (bandMixValues[(size_t) b]->load() * 0.01f);
        for (auto& s : band.slots) s->mix.setTargetValue (s->mixPercent.load() * 0.01f);
    }
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
                // Slot dry/wet: the dry signal is delayed by the slot's latency to stay aligned.
                for (int frame = 0; frame < n; ++frame)
                {
                    const float mix = slot.mix.getNextValue();
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float d = slot.dryDelay.tick (ch, band.slotDry.getSample (ch, frame), slot.latency);
                        const float w = buffer.getSample (ch, frame);
                        buffer.setSample (ch, frame, d + mix * (w - d));
                    }
                    slot.dryDelay.advance();
                }
            }
            // Band mix (dry band vs. its processed chain), then align all bands to the longest chain.
            const int align = total - band.chainLatency;
            for (int frame = 0; frame < n; ++frame)
            {
                const float mix = band.mix.getNextValue();
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float d = band.dryDelay.tick (ch, band.dry.getSample (ch, frame), band.chainLatency);
                    const float w = buffer.getSample (ch, frame);
                    audio.addSample (ch, offset + frame, band.alignDelay.tick (ch, d + mix * (w - d), align));
                }
                band.dryDelay.advance(); band.alignDelay.advance();
            }
        }
    }
    if (fadeRemaining > 0)
    {
        // Short fade-in after the band layout changes, so adding/removing bands does not click.
        for (int i = 0; i < audio.getNumSamples() && fadeRemaining > 0; ++i, --fadeRemaining)
        {
            const float gain = 1.0f - (float) fadeRemaining / (float) fadeLength;
            for (int ch = 0; ch < 2; ++ch) audio.setSample (ch, i, audio.getSample (ch, i) * gain);
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

// ---- Parameters ---------------------------------------------------------------------------
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
    // Slot mixes and the slot/band layout are not DAW parameters: tell the host the project changed.
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

// ---- Bands ----------------------------------------------------------------------------------
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
        splitter.setBandCount (numBands, next.data());
        alignLatency();
        startFadeIn();
        setStatus ("Added band " + juce::String (b + 2) + " at " + juce::String (juce::roundToInt (cut)) + " Hz");
    }
    for (int j = 0; j < count - 1; ++j) setParameterValue (crossoverID (j), next[(size_t) j]);
    for (int p = count - 1; p > index + 1; --p) setParameterValue (bandMixID (p), getParameterValue (bandMixID (p - 1)));
    setParameterValue (bandMixID (index + 1), 100.0f);
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
    for (int p = band; p < count; ++p) setParameterValue (bandMixID (p), getParameterValue (bandMixID (p + 1)));
    setParameterValue (bandMixID (count), 100.0f);
    muted.store (false);
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}

// ---- Slots ----------------------------------------------------------------------------------
void TriadProcessor::publishInfo()
{
    std::array<std::vector<SlotInfo>, triad::maxBands> next;
    for (int b = 0; b < numBands; ++b)
        for (auto& slotPtr : bands[(size_t) b]->slots)
        {
            const auto& s = *slotPtr;
            SlotInfo n;
            n.name = s.description.name;
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
    const juce::ScopedLock lock (infoLock);
    if (band < 0 || band >= triad::maxBands || slot < 0 || slot >= (int) info[(size_t) band].size()) return {};
    return info[(size_t) band][(size_t) slot];
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
        auto slot = std::make_unique<Slot>();
        prepareSlot (*slot);
        auto& slots = bands[(size_t) band]->slots;
        slots.push_back (std::move (slot));
        index = (int) slots.size() - 1;
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
        auto& slots = bands[(size_t) band]->slots;
        removed = std::move (slots[(size_t) slot]);
        slots.erase (slots.begin() + slot);
        if (slots.empty())
        {
            auto fresh = std::make_unique<Slot>();
            prepareSlot (*fresh);
            slots.push_back (std::move (fresh));
        }
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
        auto& slots = bands[(size_t) band]->slots;
        std::swap (slots[(size_t) slot], slots[(size_t) target]);   // the mix knob travels with its effect
        slots[(size_t) slot]->dryDelay.reset();
        slots[(size_t) target]->dryDelay.reset();
        alignLatency();
    }
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
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
    if (! s->window) s->window = std::make_unique<EffectWindow> (*s->effect, windowTitle (band, slot, s->description.name));
    s->window->setVisible (true);
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

// ---- State ----------------------------------------------------------------------------------
// Version 3: <TriadState version="3" bands="n"> <Parameters/> <Band index slots> <Slot index enabled mix>
//   <PLUGIN/> <State/> </Slot> </Band> </TriadState>. Versions 1 (Triad 0.1) and 2 (0.2.0) still load.
void TriadProcessor::getStateInformation (juce::MemoryBlock& output)
{
    {
        const juce::ScopedLock lock (stateGate);
        if (pendingState) { copyXmlToBinary (*pendingState, output); return; }
    }
    // Match the VST3 host's message-lock-before-state-lock order. In particular,
    // do not hold gate while waiting for the GUI thread during a background save.
    const juce::MessageManagerLock messageLock;
    const juce::ScopedLock lock (gate);
    juce::XmlElement root ("TriadState");
    root.setAttribute ("version", 3);
    root.setAttribute ("bands", numBands);
    root.addChildElement (parameters.copyState().createXml().release());
    for (int b = 0; b < numBands; ++b)
    {
        auto& slots = bands[(size_t) b]->slots;
        auto* bandNode = root.createNewChildElement ("Band");
        bandNode->setAttribute ("index", b);
        bandNode->setAttribute ("slots", (int) slots.size());
        for (int i = 0; i < (int) slots.size(); ++i)
        {
            auto& s = *slots[(size_t) i];
            const bool hasPlugin = s.description.name.isNotEmpty();
            if (! hasPlugin && s.enabled && juce::exactlyEqual (s.mixPercent.load(), 100.0f)) continue;
            auto* node = bandNode->createNewChildElement ("Slot");
            node->setAttribute ("index", i);
            node->setAttribute ("enabled", s.enabled);
            node->setAttribute ("mix", (double) s.mixPercent.load());
            if (! hasPlugin) continue;
            node->addChildElement (s.description.createXml().release());
            if (s.effect) s.effect->getStateInformation (s.savedState);
            node->createNewChildElement ("State")->addTextElement (s.savedState.toBase64Encoding());
        }
    }
    copyXmlToBinary (root, output);
}
void TriadProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("TriadState")) return;
    const auto version = xml->getIntAttribute ("version");
    if (version < 1 || version > 3) return;
    {
        const juce::ScopedLock lock (stateGate);
        pendingState = std::move (xml); restoring.store (true);
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
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
    // Parameters, minus any that no longer exist (Triad 0.2.0 had per-slot mix parameters).
    juce::ValueTree savedSlotMixes;
    if (auto* p = xml.getChildByName ("Parameters"))
    {
        auto tree = juce::ValueTree::fromXml (*p);
        savedSlotMixes = tree.createCopy();
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (parameters.getParameter (tree.getChild (i).getProperty ("id").toString()) == nullptr)
                tree.removeChild (i, nullptr);
        parameters.replaceState (tree);
    }
    auto legacySlotMix = [&savedSlotMixes] (int b, int s)
    {
        const auto child = savedSlotMixes.getChildWithProperty ("id", "slot" + juce::String (b) + "_" + juce::String (s));
        return child.isValid() ? (float) child.getProperty ("value", 100.0) : 100.0f;
    };
    // Gather every slot to restore, and size each band's slot list.
    struct Pending { int band, slot; const juce::XmlElement* node; };
    std::vector<Pending> pending;
    std::array<int, triad::maxBands> slotCounts {};
    slotCounts.fill (1);
    if (version == 1)
    {
        for (auto* node : xml.getChildIterator())
            if (node->hasTagName ("Slot")) pending.push_back ({ node->getIntAttribute ("index", -1), 0, node });
    }
    else
    {
        for (auto* bandNode : xml.getChildIterator())
            if (bandNode->hasTagName ("Band"))
            {
                const int b = bandNode->getIntAttribute ("index", -1);
                if (b < 0 || b >= count) continue;
                slotCounts[(size_t) b] = juce::jmax (slotCounts[(size_t) b], juce::jlimit (1, 4096, bandNode->getIntAttribute ("slots", 1)));
                for (auto* node : bandNode->getChildIterator())
                    if (node->hasTagName ("Slot"))
                    {
                        const int s = node->getIntAttribute ("index", -1);
                        if (s < 0 || s >= 4096) continue;
                        slotCounts[(size_t) b] = juce::jmax (slotCounts[(size_t) b], s + 1);
                        pending.push_back ({ b, s, node });
                    }
            }
    }
    {
        const juce::ScopedLock lock (gate);
        numBands = count;
        bandCount.store (count);
        for (int b = 0; b < triad::maxBands; ++b)
        {
            auto& slots = bands[(size_t) b]->slots;
            while ((int) slots.size() < slotCounts[(size_t) b]) slots.push_back (std::make_unique<Slot>());
            if (version == 2)
                for (int s = 0; s < (int) slots.size(); ++s) slots[(size_t) s]->mixPercent.store (legacySlotMix (b, s));
        }
        for (auto& p : pending)
            if (auto* s = slotAt (p.band, p.slot))
            {
                s->enabled = p.node->getBoolAttribute ("enabled", true);
                if (p.node->hasAttribute ("mix")) s->mixPercent.store (juce::jlimit (0.0f, 100.0f, (float) p.node->getDoubleAttribute ("mix")));
            }
        std::array<float, triad::maxCrossovers> cuts {};
        readCrossovers (cuts.data());
        splitter.setBandCount (numBands, cuts.data());
        for (int b = 0; b < triad::maxBands; ++b) prepareBand (b, false);
        alignLatency();
    }
    ++structureVersion;
    for (auto& p : pending)
    {
        auto* descXml = p.node->getChildByName ("PLUGIN");
        if (slotAt (p.band, p.slot) == nullptr || descXml == nullptr) continue;
        juce::PluginDescription desc;
        if (! desc.loadFromXml (*descXml)) continue;
        juce::MemoryBlock saved;
        if (auto* st = p.node->getChildByName ("State")) saved.fromBase64Encoding (st->getAllSubText());
        auto error = isTriad (desc) ? juce::String ("Triad cannot host itself.") : requestInstall (p.band, p.slot, desc, saved, false, true);
        const juce::ScopedLock lock (gate);
        if (auto* slot = slotAt (p.band, p.slot); slot != nullptr && error.isNotEmpty())
        {
            slot->description = desc;
            slot->savedState = saved;
            slot->error = "Missing/unavailable: " + error;
        }
        alignLatency();
    }
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
    setStatus ("Project restored");
}
juce::AudioProcessorEditor* TriadProcessor::createEditor() { return new TriadEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TriadProcessor(); }
