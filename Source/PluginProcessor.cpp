#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
class EffectWindow final : public juce::DocumentWindow
{
public:
    explicit EffectWindow (juce::AudioPluginInstance& p)
        : DocumentWindow (p.getName(), juce::Colour (0xff141923), closeButton)
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
}

juce::AudioProcessorValueTreeState::ParameterLayout TriadProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto frequency = juce::NormalisableRange<float> (20.0f, 20000.0f, 0.01f);
    frequency.setSkewForCentre (1000.0f);
    p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "lowCut", 1 }, "Low / Mid crossover", frequency, 250.0f));
    p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "highCut", 1 }, "Mid / High crossover", frequency, 2500.0f));
    for (int i = 0; i < 3; ++i)
        p.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix" + juce::String (i), 1 },
              juce::StringArray { "Low mix", "Mid mix", "High mix" }[i], juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    return p;
}

TriadProcessor::TriadProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Parameters", makeParameters())
{
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
    lowValue = parameters.getRawParameterValue ("lowCut");
    highValue = parameters.getRawParameterValue ("highCut");
    for (int i = 0; i < 3; ++i) mixValues[(size_t) i] = parameters.getRawParameterValue ("mix" + juce::String (i));
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
void TriadProcessor::prepareToPlay (double sr, int block)
{
    const juce::ScopedLock lock (gate);
    rate.store (sr); capacity = juce::jmax (1, block);
    splitter.prepare (sr, capacity);
    splitter.setCutoffs (lowValue->load(), highValue->load(), true);
    bypassDelay.prepare();
    for (size_t i = 0; i < 3; ++i)
    {
        bands[i].setSize (2, capacity); dry[i].setSize (2, capacity);
        auto& s = slots[i];
        s.dryDelay.prepare(); s.wetDelay.prepare();
        s.mix.reset (sr, 0.02); s.mix.setCurrentAndTargetValue (mixValues[i]->load() * 0.01f);
        if (s.effect)
        {
            if (prepared) s.effect->releaseResources();
            s.effect->setRateAndBufferSizeDetails (sr, capacity);
            s.effect->prepareToPlay (sr, capacity);
        }
    }
    prepared = true;
    alignLatency();
}
void TriadProcessor::releaseResources()
{
    const juce::ScopedLock lock (gate);
    if (prepared)
        for (auto& s : slots) if (s.effect) s.effect->releaseResources();
    prepared = false;
}
std::pair<float, float> TriadProcessor::effectiveCutoffs() const
{
    auto hi = juce::jlimit (30.0f, (float) juce::jmin (20000.0, rate.load() * 0.45), highValue->load());
    return { juce::jlimit (20.0f, hi / 1.01f, lowValue->load()), hi };
}
void TriadProcessor::alignLatency()
{
    int longest = 0;
    double longestTail = 0.0;
    bool changed = false;
    for (auto& s : slots)
    {
        auto latency = s.effect ? s.effect->getLatencySamples() : 0;
        if (latency < 0 || latency > triad::maxLatency)
        {
            s.error = "Latency exceeds supported limit; effect bypassed";
            latency = 0;
        }
        else if (s.error.startsWith ("Latency exceeds")) s.error.clear();
        if (s.effect && (s.effect->getTotalNumInputChannels() != 2 || s.effect->getTotalNumOutputChannels() != 2))
        {
            s.error = "Unsupported channel layout; effect bypassed";
            latency = 0;
        }
        else if (s.error.startsWith ("Unsupported channel layout")) s.error.clear();
        changed |= s.latency != latency;
        s.latency = latency;
        longest = juce::jmax (longest, latency);
        if (s.effect) longestTail = juce::jmax (longestTail, s.effect->getTailLengthSeconds());
    }
    changed |= totalLatency.load() != longest;
    totalLatency.store (longest);
    tail.store (longestTail + (double) longest / rate.load());
    if (changed && prepared)
    {
        for (auto& s : slots) { s.dryDelay.reset(); s.wetDelay.reset(); }
        bypassDelay.reset();
    }
    if (getLatencySamples() != longest) setLatencySamples (longest);
}
void TriadProcessor::timerCallback()
{
    const juce::ScopedTryLock lock (gate);
    if (lock.isLocked()) alignLatency();
}
void TriadProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals denormals;
    midi.clear();
    const juce::ScopedTryLock lock (gate);
    if (! lock.isLocked() || ! prepared || restoring.load() || audio.getNumChannels() < 2)
    { audio.clear(); return; }
    // Never emit misaligned audio while a child changes its reported latency.
    for (auto& s : slots)
        if (s.effect && s.error.isEmpty()
            && (s.effect->getLatencySamples() != s.latency
                || s.effect->getTotalNumInputChannels() != 2
                || s.effect->getTotalNumOutputChannels() != 2))
        { audio.clear(); return; }
    process (audio);
}
void TriadProcessor::process (juce::AudioBuffer<float>& audio)
{
    splitter.setCutoffs (lowValue->load(), highValue->load());
    for (size_t i = 0; i < 3; ++i) slots[i].mix.setTargetValue (mixValues[i]->load() * 0.01f);
    const int latency = totalLatency.load();
    for (int offset = 0; offset < audio.getNumSamples(); offset += capacity)
    {
        const int n = juce::jmin (capacity, audio.getNumSamples() - offset);
        splitter.process (audio, offset, n, bands);
        for (int ch = 0; ch < 2; ++ch) audio.clear (ch, offset, n);
        for (size_t i = 0; i < 3; ++i)
        {
            auto& s = slots[i];
            const bool active = s.effect && s.error.isEmpty();
            for (int ch = 0; ch < 2; ++ch) dry[i].copyFrom (ch, 0, bands[i], ch, 0, n);
            if (active)
            {
                auto view = juce::AudioBuffer<float> (bands[i].getArrayOfWritePointers(), 2, n);
                juce::MidiBuffer events;
                s.effect->setPlayHead (getPlayHead());
                s.effect->setNonRealtime (isNonRealtime());
                s.effect->processBlock (view, events);
                if (s.effect->getLatencySamples() != s.latency)
                { audio.clear(); return; }
            }
            for (int frame = 0; frame < n; ++frame)
            {
                const float mix = s.mix.getNextValue();
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float d = s.dryDelay.tick (ch, dry[i].getSample (ch, frame), latency);
                    const float w = s.wetDelay.tick (ch, bands[i].getSample (ch, frame), latency - (active ? s.latency : 0));
                    audio.addSample (ch, offset + frame, active ? d + mix * (w - d) : d);
                }
                s.dryDelay.advance(); s.wetDelay.advance();
            }
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
TriadProcessor::SlotInfo TriadProcessor::getSlotInfo (int i)
{
    const juce::ScopedLock lock (gate);
    auto& s = slots[(size_t) i];
    return { s.description.name.isEmpty() ? "Choose an effect" : s.description.name, s.error, s.effect != nullptr };
}
juce::String TriadProcessor::getStatus() const
{
    const juce::ScopedLock lock (gate);
    return restoring.load() ? "Restoring effects..." : status;
}
juce::String TriadProcessor::install (int index, const juce::PluginDescription& desc, const juce::MemoryBlock& state)
{
    // Called on the message thread. The old instance stays alive until the new one is ready.
    if (desc.name == "Triad" || desc.descriptiveName == "Triad") return "Triad cannot host itself.";
    juce::String error;
    int preparedBlockSize;
    { const juce::ScopedLock lock (gate); preparedBlockSize = capacity; }
    auto instance = formats.createPluginInstance (desc, rate.load(), preparedBlockSize, error);
    if (! instance) return error.isNotEmpty() ? error : "Could not open this VST3 effect.";
    if (desc.isInstrument) return "Choose an audio effect, not an instrument.";
    instance->disableNonMainBuses();
    auto layout = instance->getBusesLayout();
    if (layout.inputBuses.size() != 1 || layout.outputBuses.size() != 1)
    {
        // Extra buses may exist but must remain disabled.
        for (int j = 1; j < layout.inputBuses.size(); ++j) layout.inputBuses.getReference (j) = juce::AudioChannelSet::disabled();
        for (int j = 1; j < layout.outputBuses.size(); ++j) layout.outputBuses.getReference (j) = juce::AudioChannelSet::disabled();
    }
    if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty()) return "This effect has no audio input or output.";
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (! instance->setBusesLayout (layout)) return "This version supports stereo-in / stereo-out effects.";
    if (state.getSize() > 0) instance->setStateInformation (state.getData(), (int) state.getSize());
    if (instance->getTotalNumInputChannels() != 2 || instance->getTotalNumOutputChannels() != 2)
        return "The effect changed to an unsupported channel layout.";
    const juce::ScopedLock lock (gate);
    instance->setRateAndBufferSizeDetails (rate.load(), capacity);
    if (prepared) instance->prepareToPlay (rate.load(), capacity);
    auto& s = slots[(size_t) index];
    s.window.reset();
    if (s.effect && prepared) s.effect->releaseResources();
    s.effect = std::move (instance); s.description = desc; s.savedState = state; s.error.clear();
    if (prepared) { s.dryDelay.reset(); s.wetDelay.reset(); }
    alignLatency();
    return {};
}
juce::String TriadProcessor::loadFile (int index, const juce::File& file)
{
    juce::OwnedArray<juce::PluginDescription> descriptions;
    formats.getFormat (0)->findAllTypesForFile (descriptions, file.getFullPathName());
    juce::String result = "No usable VST3 effect was found in this file.";
    for (auto* d : descriptions)
        if (! d->isInstrument)
        {
            result = install (index, *d, {});
            if (result.isEmpty()) break;
        }
    const juce::ScopedLock lock (gate);
    status = result.isEmpty() ? "Effect loaded" : result;
    return result;
}
void TriadProcessor::removeEffect (int index)
{
    const juce::ScopedLock lock (gate);
    auto& s = slots[(size_t) index];
    s.window.reset();
    if (s.effect && prepared) s.effect->releaseResources();
    s.effect.reset(); s.description = {}; s.savedState.reset(); s.error.clear();
    if (prepared) { s.dryDelay.reset(); s.wetDelay.reset(); }
    alignLatency(); status = "Effect removed";
}
void TriadProcessor::showEffect (int index)
{
    const juce::ScopedLock lock (gate);
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
    {
        status = "Cannot open effect editor: no display is available.";
        return;
    }
    auto& s = slots[(size_t) index];
    if (! s.effect) return;
    if (! s.window) s.window = std::make_unique<EffectWindow> (*s.effect);
    s.window->setVisible (true); s.window->toFront (true);
}
void TriadProcessor::closeWindows()
{
    const juce::ScopedLock lock (gate);
    for (auto& s : slots) s.window.reset();
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
    const juce::ScopedLock lock (gate);
    juce::XmlElement root ("TriadState"); root.setAttribute ("version", 1);
    root.addChildElement (parameters.copyState().createXml().release());
    for (size_t i = 0; i < 3; ++i)
    {
        auto& s = slots[i];
        if (s.description.name.isEmpty()) continue;
        auto* node = root.createNewChildElement ("Slot"); node->setAttribute ("index", (int) i);
        node->addChildElement (s.description.createXml().release());
        if (s.effect) s.effect->getStateInformation (s.savedState);
        node->createNewChildElement ("State")->addTextElement (s.savedState.toBase64Encoding());
    }
    copyXmlToBinary (root, output);
}
void TriadProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("TriadState") || xml->getIntAttribute ("version") != 1) return;
    {
        const juce::ScopedLock lock (stateGate);
        pendingState = std::move (xml); restoring.store (true);
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}
void TriadProcessor::handleAsyncUpdate()
{
    std::unique_ptr<juce::XmlElement> xml;
    {
        const juce::ScopedLock lock (stateGate);
        if (! pendingState) return;
        xml = std::make_unique<juce::XmlElement> (*pendingState);
    }
    if (auto* p = xml->getChildByName ("Parameters")) parameters.replaceState (juce::ValueTree::fromXml (*p));
    for (int i = 0; i < 3; ++i) removeEffect (i);
    for (auto* node : xml->getChildIterator())
    {
        if (! node->hasTagName ("Slot")) continue;
        int i = node->getIntAttribute ("index", -1);
        auto* descXml = node->getChildByName ("PLUGIN");
        if (i < 0 || i >= 3 || descXml == nullptr) continue;
        juce::PluginDescription desc;
        if (! desc.loadFromXml (*descXml)) continue;
        juce::MemoryBlock saved;
        if (auto* st = node->getChildByName ("State")) saved.fromBase64Encoding (st->getAllSubText());
        auto error = install (i, desc, saved);
        if (error.isNotEmpty())
        {
            const juce::ScopedLock lock (gate);
            slots[(size_t) i].description = desc;
            slots[(size_t) i].savedState = saved;
            slots[(size_t) i].error = "Missing/unavailable: " + error;
        }
    }
    {
        const juce::ScopedLock lock (stateGate);
        // A second restore request may have arrived while a plugin was loading.
        if (pendingState && pendingState->isEquivalentTo (xml.get(), false))
        { pendingState.reset(); restoring.store (false); }
        else triggerAsyncUpdate();
    }
    const juce::ScopedLock lock (gate);
    status = "Project restored";
}
juce::AudioProcessorEditor* TriadProcessor::createEditor() { return new TriadEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TriadProcessor(); }
