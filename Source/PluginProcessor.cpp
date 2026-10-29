#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PlatformWindows.h"
#include "Settings.h"
#include "TriadTime.h"
#include <cstring>
#include <map>
#include <numeric>
#include <set>

namespace
{
// Hosted effects that were removed, or whose Triad was closed, and closed effect windows (with the
// effect's editor) are deleted about a second later on the message thread. Some plug-ins and their
// editors post themselves messages (when they load or open, or from the audio thread) and crash if
// one arrives after they were deleted. Entries are deleted oldest first, so an editor always goes
// before its effect.
//
// One idle copy of each third-party effect is also kept loaded for the rest of the session (its
// "keeper"). Deleting the last copy of a plug-in makes it shut down its own internals, and some
// plug-ins (seen with FILT-R and REEV-R) occasionally crash doing that; with a keeper that only
// happens once, when the DAW unloads Triad.
//
// Shared by every Triad instance (message thread only). When the last Triad closes, everything left
// is deleted right away: Triad's own framework shuts down straight after, and hosted effects must
// be deleted while it still runs. Whenever something new is loaded the graveyard is re-made, so if
// it is still there at shutdown (JUCE deletes the newest objects first) it goes before the plug-in
// format caches and window managers that its contents need.
// The last effects and windows the graveyard deleted (for a crash report).
struct RecentDeletion { char name[112]; juce::uint32 at; };
constexpr int recentSize = 16;
std::array<RecentDeletion, recentSize> recent {};
std::atomic<int> recentCount { 0 };

class EffectGraveyard final : private juce::Timer, public juce::DeletedAtShutdown
{
public:
    static EffectGraveyard* get()
    {
        if (current == nullptr) current = new EffectGraveyard();
        return current;
    }
    static void renew()
    {
        if (current == nullptr) return;
        auto* old = current;
        auto* fresh = new EffectGraveyard();   // registered for shutdown after everything that exists now
        fresh->entries = std::move (old->entries);
        fresh->keepers = std::move (old->keepers);
        old->entries.clear();
        old->keepers.clear();
        current = fresh;
        delete old;
        if (! fresh->entries.empty()) fresh->startTimer (tickMs);
    }
    static int keeperCount() { return current != nullptr ? (int) current->keepers.size() : 0; }
    static void releaseAll() { delete current; }   // the destructor deletes everything, oldest first
    ~EffectGraveyard() override
    {
        stopTimer();
        if (current == this) current = nullptr;
        for (auto& e : entries) { e.window.reset(); e.effect.reset(); }   // oldest first, editor before effect
        entries.clear();
        keepers.clear();
    }
    void add (std::unique_ptr<juce::AudioPluginInstance> effect, std::unique_ptr<juce::DocumentWindow> window)
    {
        const auto now = juce::Time::getMillisecondCounter();
        // A window (the effect's editor) goes first; its effect at least `afterWindowMs` later, so whatever the
        // editor's deletion posted to the effect has been delivered before the effect itself goes.
        const auto due = now + (juce::uint32) (effect != nullptr ? delayMs + afterWindowMs : delayMs);
        entries.push_back ({ std::move (effect), std::move (window), now, due });
        if (! isTimerRunning()) startTimer (tickMs);
    }
    // Effects are deleted at least `delayMs` after they were retired, a few copies of each plug-in per tick (the message
    // loop runs in between): when a project reloads, dozens of effects go at once, and ChowMatrix crashed when many
    // went in the same moment. Within a time budget per tick; a backlog of more than 64 (a long session of fast
    // edits) is cleared without the budget, so removed effects never pile up holding memory.
    static constexpr int tickMs = 40, delayMs = 1500, afterWindowMs = 400, copiesPerTick = 4, backlog = 64;
    static constexpr double budgetMs = 25.0;
    static int waitingCount() { return current != nullptr ? (int) current->entries.size() : 0; }
    // Entries still waiting `lateMs` or more past their time (deletion falling behind).
    static int lateCount (int lateMs)
    {
        if (current == nullptr) return 0;
        const auto now = juce::Time::getMillisecondCounter();
        return (int) std::count_if (current->entries.begin(), current->entries.end(),
                                    [&] (const Entry& e) { return (juce::int32) (now - e.due) >= lateMs; });
    }
    // The last effects and windows deleted, newest first ("1234 ms ago: ChowMatrix (VST3)"), for a crash report:
    // fixed buffers only, so a signal handler can read them.
    static void describeRecent (char* out, size_t size)
    {
        if (size == 0) return;
        out[0] = 0;
        const auto now = juce::Time::getMillisecondCounter();
        const int count = recentCount.load();
        size_t used = 0;
        for (int i = count - 1; i >= juce::jmax (0, count - recentSize) && used + 140 < size; --i)
        {
            const auto& r = recent[(size_t) (i % recentSize)];
            used += (size_t) std::snprintf (out + used, size - used, "  %u ms ago: %s\n", (unsigned) (now - r.at), r.name);
        }
    }
private:
    EffectGraveyard() = default;
    struct Entry
    {
        std::unique_ptr<juce::AudioPluginInstance> effect;
        std::unique_ptr<juce::DocumentWindow> window;   // declared after the effect: deleted first
        juce::uint32 since = 0, due = 0;
    };
    static void remember (const juce::String& what)
    {
        const int i = recentCount.load();
        auto& r = recent[(size_t) (i % recentSize)];
        r.at = juce::Time::getMillisecondCounter();
        what.copyToUTF8 (r.name, sizeof (r.name));
        recentCount.store (i + 1);
    }
    static juce::String keeperKey (const juce::AudioPluginInstance& effect)
    {
        const auto d = effect.getPluginDescription();
        if (d.manufacturerName.startsWithIgnoreCase ("Apple")) return {};   // macOS's own effects need no keeper
        if (d.pluginFormatName == TriadTime::formatName) return {};          // nor Triad's own
        return d.pluginFormatName + "|" + d.fileOrIdentifier + "|" + d.name;
    }
    static juce::String pluginKey (const Entry& e)
    {
        if (e.effect == nullptr) return {};
        const auto d = e.effect->getPluginDescription();
        return d.pluginFormatName + "|" + d.fileOrIdentifier + "|" + d.name;
    }
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const bool catchUp = (int) entries.size() > backlog;
        std::map<juce::String, int> deletedNow;
        int deleted = 0;
        for (auto it = entries.begin(); it != entries.end();)
        {
            if ((juce::int32) (now - it->due) < 0) { ++it; continue; }   // not yet
            if (! catchUp && deleted > 0 && juce::Time::getMillisecondCounterHiRes() - start > budgetMs) break;
            const auto key = pluginKey (*it);
            if (! catchUp && key.isNotEmpty() && deletedNow[key] >= copiesPerTick) { ++it; continue; }   // its next copies: next tick
            ++deletedNow[key];
            ++deleted;
            auto e = std::move (*it);
            it = entries.erase (it);
            if (e.window != nullptr) { remember ("window " + e.window->getName()); e.window.reset(); }
            if (e.effect == nullptr) continue;
            const auto keep = keeperKey (*e.effect);
            if (keep.isNotEmpty() && keepers.find (keep) == keepers.end()) keepers[keep] = std::move (e.effect);   // kept idle
            else
            {
                remember (e.effect->getName() + " (" + e.effect->getPluginDescription().pluginFormatName + ")");
                e.effect.reset();
            }
        }
        if (entries.empty()) stopTimer();
        if (entries.empty() && keepers.empty()) delete this;   // `this` is gone after this line
    }
    std::vector<Entry> entries;
    std::map<juce::String, std::unique_ptr<juce::AudioPluginInstance>> keepers;
    static inline EffectGraveyard* current = nullptr;
};

// Triad instances alive in this copy of Triad (the last one to close empties the graveyard).
std::atomic<int> liveTriads { 0 };

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
    void closeButtonPressed() override { triad::platform::release (*this); setVisible (false); }
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
constexpr int parameterRate = 64; // knobs inside effects that LFOs move are set every 64 samples
juce::String windowTitle (int band, int slot, const juce::String& name)
{
    return (band == triad::maxBands ? juce::String ("PRE") : band == triad::maxBands + 1 ? juce::String ("POST") : "Band " + juce::String (band + 1))
         + juce::String::fromUTF8 (" \xc2\xb7 Slot ") + juce::String (slot + 1)
         + juce::String::fromUTF8 (" \xe2\x80\x94 ") + name;
}
bool isTriad (const juce::PluginDescription& d) { return triad::isTriadName (d.name) || triad::isTriadName (d.descriptiveName); }
void storeMax (std::atomic<float>& target, float value)
{
    if (value > target.load (std::memory_order_relaxed)) target.store (value, std::memory_order_relaxed);
}
// The channels a hosted effect processes, or 0 when Triad cannot host its layout. The main buses must
// be stereo; an extra bus that the format cannot switch off (an AU sidechain input, say) is fed silence.
constexpr int maxEffectChannels = 16;
int effectChannels (const juce::AudioPluginInstance& e)
{
    if (e.getMainBusNumInputChannels() != 2 || e.getMainBusNumOutputChannels() != 2) return 0;
    const int width = juce::jmax (e.getTotalNumInputChannels(), e.getTotalNumOutputChannels());
    return width <= maxEffectChannels ? width : 0;
}
juce::String describeLayout (const juce::AudioProcessor::BusesLayout& layout)
{
    auto list = [] (const juce::Array<juce::AudioChannelSet>& buses)
    {
        juce::StringArray names;
        for (auto& b : buses) names.add (b.isDisabled() ? "off" : b.getDescription());
        return names.isEmpty() ? juce::String ("none") : names.joinIntoString (" + ");
    };
    return "in: " + list (layout.inputBuses) + ", out: " + list (layout.outputBuses);
}

// True when every sample is a finite number (checked on the bits, so it holds even with fast math).
bool allFinite (const juce::AudioBuffer<float>& b, int channels, int samples)
{
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* data = b.getReadPointer (ch);
        juce::uint32 bad = 0;
        for (int i = 0; i < samples; ++i)
        {
            juce::uint32 bits;
            std::memcpy (&bits, data + i, sizeof (bits));
            bad |= (juce::uint32) ((bits & 0x7f800000u) == 0x7f800000u);
        }
        if (bad != 0) return false;
    }
    return true;
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
juce::String TriadProcessor::bandMuteID (int b) { return "mute" + juce::String (b); }
juce::String TriadProcessor::bandSoloID (int b) { return "solo" + juce::String (b); }
juce::String TriadProcessor::slopeID (int j) { return "slope" + juce::String (j); }

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
    // Added in 0.4.0 (after all earlier parameters, so existing automation keeps its place).
    for (int j = 0; j < triad::maxCrossovers; ++j)
        p.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { slopeID (j), 4 },
              "Crossover " + juce::String (j + 1) + " Slope", triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder,
              triad::CrossoverFilter::defaultOrder,
              juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return slopeText (v); })
                                                 .withValueFromStringFunction ([] (const juce::String& t)
                                                 { return juce::jlimit (1, 8, juce::roundToInt (t.getFloatValue() / 6.0f)); })));
    // Added in 0.8.0: a mute per band (hear only the bands you want).
    for (int b = 0; b < triad::maxBands; ++b)
        p.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandMuteID (b), 5 }, "Band " + juce::String (b + 1) + " Mute", false));
    // Added in 0.18.0: a solo per band (only the soloed bands are heard).
    for (int b = 0; b < triad::maxBands; ++b)
        p.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandSoloID (b), 6 }, "Band " + juce::String (b + 1) + " Solo", false));
    return p;
}

// ---- Band structure ---------------------------------------------------------------------------
namespace
{
std::atomic<int> serials { 0 };
int nextSerial() { return ++serials; }
}
TriadProcessor::Slot::Slot() : serial (nextSerial()) {}
TriadProcessor::Band::Band (bool withSlot, bool isExtra) : extra (isExtra), serial (nextSerial())
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
    if (! extra && std::none_of (lfos.begin(), lfos.end(), [] (auto& l) { return l->bandLfo; }))
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
    if (extra) lfos.erase (std::remove_if (lfos.begin(), lfos.end(), [] (auto& l) { return l->bandLfo; }), lfos.end());
    routes.erase (std::remove_if (routes.begin(), routes.end(), [this] (const std::unique_ptr<Route>& r)
    {
        const bool toSlot = r->destination.kind == Destination::slotMix || r->destination.kind == Destination::effectParameter;
        if (extra && ! toSlot) return true;   // PRE and POST have no band mix, volume or edges
        return findLfo (r->lfoId) == nullptr || (toSlot && findSlot (r->destination.slotId) == nullptr);
    }), routes.end());
    for (auto& s : slots) { s->modulators.clear(); s->parameterModulators.clear(); }
    mixModulators.clear();
    volumeModulators.clear();
    edgeModulators.clear();
    for (auto& r : routes)
    {
        r->lfo = findLfo (r->lfoId);
        r->parameter = nullptr;
        if (r->destination.kind == Destination::slotMix) findSlot (r->destination.slotId)->modulators.push_back (r.get());
        else if (r->destination.kind == Destination::effectParameter)
        {
            // Found by the effect's own parameter ID (stable across versions of the effect), else by index.
            // A link to an effect that is not loaded (yet) waits until it is.
            auto* s = findSlot (r->destination.slotId);
            r->parameter = resolveParameter (*s, r->destination);
            r->lastSent = -1.0f;
            if (r->parameter != nullptr) s->parameterModulators.push_back (r.get());
        }
        else if (r->destination.kind == Destination::bandMix) mixModulators.push_back (r.get());
        else if (r->destination.kind == Destination::bandEdges) edgeModulators.push_back (r.get());
        else volumeModulators.push_back (r.get());
    }
}
juce::AudioProcessorParameter* TriadProcessor::Band::resolveParameter (const Slot& s, Destination& d)
{
    if (s.effect == nullptr) return nullptr;
    const auto& list = s.effect->getParameters();
    if (d.parameterId.isNotEmpty())
        for (int i = 0; i < list.size(); ++i)
            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (list[i]); hosted != nullptr && hosted->getParameterID() == d.parameterId)
            {
                d.parameter = i;
                return list[i];
            }
    if (d.parameter >= 0 && d.parameter < list.size())
    {
        auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (list[d.parameter]);
        if (d.parameterId.isEmpty())
        {
            if (hosted != nullptr) d.parameterId = hosted->getParameterID();
            return list[d.parameter];
        }
        // Linked by an ID this effect no longer has (a different version of it): only a format without IDs
        // falls back to the index.
        return hosted == nullptr ? list[d.parameter] : nullptr;
    }
    return nullptr;
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
// The band clipboard (Copy / Paste band state) is shared by the Triads in this process; it is
// emptied when the last one closes.
namespace
{
struct BandClipboard
{
    std::unique_ptr<juce::XmlElement> xml;
    juce::String description;
};
BandClipboard& bandClipboard()
{
    static BandClipboard clipboard;
    return clipboard;
}
// "Copy plugin state": one effect with all its settings, and its slot's mix.
struct EffectClipboard
{
    juce::PluginDescription description;
    juce::MemoryBlock state;
    float mix = 100.0f;
    bool valid = false;
};
EffectClipboard& effectClipboard()
{
    static EffectClipboard clipboard;
    return clipboard;
}
juce::uint32 nowMs() { return juce::Time::getMillisecondCounter(); }
}

// ---- Undo: changes reported from other threads ------------------------------------------------------------
// Knob gestures (the mouse pressed on a knob, in Triad or inside an effect's window, and released) can be
// reported on any thread; they are counted here and picked up by the message thread's timer.
struct TriadProcessor::HistorySignal
{
    std::atomic<int> ended { 0 }, open { 0 };
    std::atomic<juce::uint32> lastGesture { 0 };
    void begin() { ++open; lastGesture.store (nowMs()); }
    void end()
    {
        for (int o = open.load(); o > 0 && ! open.compare_exchange_weak (o, o - 1);) {}
        ++ended;
        lastGesture.store (nowMs());
    }
};
// One listener for every hosted effect (in every Triad of this process; it is never deleted, so an effect
// that outlives its Triad in the graveyard can never call a deleted listener). It maps an effect to the
// Triad it plays in; effects are taken off it when they are retired.
struct TriadProcessor::EffectWatch final : juce::AudioProcessorListener
{
    static EffectWatch& get() { static auto* watch = new EffectWatch(); return *watch; }
    void add (juce::AudioProcessor& effect, std::shared_ptr<HistorySignal> signal)
    {
        {
            const juce::SpinLock::ScopedLockType hold (lock);
            owners[&effect] = std::move (signal);
        }
        effect.addListener (this);
    }
    void remove (juce::AudioProcessor& effect)
    {
        effect.removeListener (this);
        const juce::SpinLock::ScopedLockType hold (lock);
        owners.erase (&effect);
    }
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor* p, int) override { if (auto s = find (p)) s->begin(); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor* p, int) override { if (auto s = find (p)) s->end(); }
private:
    std::shared_ptr<HistorySignal> find (juce::AudioProcessor* p)
    {
        const juce::SpinLock::ScopedLockType hold (lock);
        const auto it = owners.find (p);
        return it != owners.end() ? it->second : nullptr;
    }
    juce::SpinLock lock;
    std::map<juce::AudioProcessor*, std::shared_ptr<HistorySignal>> owners;
};
// Triad's own knobs (crossovers, slopes, band mixes and volumes): a gesture that ends is a change.
struct TriadProcessor::OwnWatch final : juce::AudioProcessorParameter::Listener
{
    explicit OwnWatch (std::shared_ptr<HistorySignal> s) : signal (std::move (s)) {}
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { if (starting) signal->begin(); else signal->end(); }
    std::shared_ptr<HistorySignal> signal;
};

TriadProcessor::TriadProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Parameters", makeParameters())
{
    formats.addFormat (std::make_unique<TriadBuiltInFormat>());   // Triad Time
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #if JUCE_MAC && JUCE_PLUGINHOST_AU
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    for (int j = 0; j < triad::maxCrossovers; ++j)
    {
        crossoverValues[(size_t) j] = parameters.getRawParameterValue (crossoverID (j));
        slopeValues[(size_t) j] = parameters.getRawParameterValue (slopeID (j));
    }
    for (int b = 0; b < triad::maxBands; ++b)
    {
        bandMixValues[(size_t) b] = parameters.getRawParameterValue (bandMixID (b));
        bandGainValues[(size_t) b] = parameters.getRawParameterValue (bandGainID (b));
        bandMuteValues[(size_t) b] = parameters.getRawParameterValue (bandMuteID (b));
        bandSoloValues[(size_t) b] = parameters.getRawParameterValue (bandSoloID (b));
        bands[(size_t) b] = std::make_unique<Band>();
    }
    bands[(size_t) preBand] = std::make_unique<Band> (true, true);
    bands[(size_t) postBand] = std::make_unique<Band> (true, true);
    {
        const juce::ScopedLock lock (gate);
        publishInfo();
    }
    historySignal = std::make_shared<HistorySignal>();
    ownWatch = std::make_unique<OwnWatch> (historySignal);
    for (auto* parameter : getParameters()) parameter->addListener (ownWatch.get());
    createdAt = juce::Time::getMillisecondCounterHiRes();
    // 30 times a second: an effect that changes its latency (a pitch corrector switched to its low-latency mode)
    // is lined up again within about 35 ms; the check itself takes no lock unless something changed.
    startTimerHz (30);
    ++liveTriads;
}
TriadProcessor::~TriadProcessor()
{
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    stopTimer(); cancelPendingUpdate();
    for (auto* parameter : getParameters()) parameter->removeListener (ownWatch.get());
    closeWindows();
    releaseResources();
    // The effects are deleted a moment after Triad, once their own pending messages have arrived.
    for (auto& band : bands)
        if (band != nullptr)
            for (auto& s : band->slots) retire (std::move (s->effect));
    if (--liveTriads == 0) { bandClipboard() = {}; effectClipboard() = {}; EffectGraveyard::releaseAll(); }
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
void TriadProcessor::readSlopes (int* dest) const
{
    for (int j = 0; j < triad::maxCrossovers; ++j)
        dest[j] = juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, juce::roundToInt (slopeValues[(size_t) j]->load()));
}
juce::RangedAudioParameter* TriadProcessor::slopeParameter (int index) const
{
    return index >= 0 && index < triad::maxCrossovers ? parameters.getParameter (slopeID (index)) : nullptr;
}
int TriadProcessor::getCrossoverSlope (int index) const
{
    if (index < 0 || index >= triad::maxCrossovers) return triad::CrossoverFilter::defaultOrder;
    return juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, juce::roundToInt (slopeValues[(size_t) index]->load()));
}
void TriadProcessor::setCrossoverSlope (int index, int order)
{
    if (auto* p = slopeParameter (index))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, order)));
        p->endChangeGesture();
    }
}
void TriadProcessor::setAllCrossoverSlopes (int order)
{
    for (int j = 0; j + 1 < getBandCount(); ++j) setCrossoverSlope (j, order);
    setStatus ("Every divider: " + slopeText (juce::jlimit (triad::CrossoverFilter::minOrder, triad::CrossoverFilter::maxOrder, order)));
}
TriadProcessor::Slot* TriadProcessor::slotAt (int band, int slot) const
{
    if (! isExtraChain (band) && (band < 0 || band >= numBands)) return nullptr;
    const auto& slots = bands[(size_t) band]->slots;
    return slot >= 0 && slot < (int) slots.size() ? slots[(size_t) slot].get() : nullptr;
}
TriadProcessor::Lfo* TriadProcessor::lfoAt (int band, int lfo) const
{
    if (! isExtraChain (band) && (band < 0 || band >= numBands)) return nullptr;
    const auto& lfos = bands[(size_t) band]->lfos;
    return lfo >= 0 && lfo < (int) lfos.size() ? lfos[(size_t) lfo].get() : nullptr;
}
TriadProcessor::Route* TriadProcessor::routeAt (int band, int route) const
{
    if (! isExtraChain (band) && (band < 0 || band >= numBands)) return nullptr;
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
    const bool positional = index >= 0 && index < triad::maxBands;   // the PRE chain has no band mix or volume
    band.dry.setSize (2, capacity);
    band.slotDry.setSize (2, capacity);
    band.mix.reset (sr, 0.005);
    band.mix.setCurrentAndTargetValue (positional ? bandMixValues[(size_t) index]->load() * 0.01f : 1.0f);
    band.gain.reset (sr, 0.005);
    band.gain.setCurrentAndTargetValue (positional ? juce::Decibels::decibelsToGain (bandGainValues[(size_t) index]->load()) : 1.0f);
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
    edgeMoves.assign ((size_t) (capacity / controlRate + 2) * triad::maxCrossovers, 0.0f);
    std::array<float, triad::maxCrossovers> cuts {};
    std::array<int, triad::maxCrossovers> slopes {};
    readCrossovers (cuts.data());
    readSlopes (slopes.data());
    splitter.setBandCount (numBands, cuts.data(), slopes.data());
    for (auto& buffer : split) buffer.setSize (2, capacity);
    wide.setSize (maxEffectChannels, capacity);
    fullSound.setSize (2, capacity);
    childMidi.ensureSize (4096);
    for (int b = 0; b <= postBand; ++b) prepareBand (b, true);
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
    double longestTail = 0.0;
    bool changed = false;
    // One chain (a band, or PRE): each slot's latency, errors and LIVE state; returns the chain's latency.
    auto chainOf = [&] (Band& band)
    {
        int chain = 0;
        for (auto& slotPtr : band.slots)
        {
            auto& s = *slotPtr;
            int latency = 0;
            if (s.effect)
            {
                const auto reported = s.effect->getLatencySamples();
                const int width = effectChannels (*s.effect);
                const bool stereo = width > 0;
                if (stereo) s.channels = width;
                if (! stereo) s.error = "Unsupported channel layout; effect bypassed";
                else if (s.error.startsWith ("Unsupported channel layout")) s.error.clear();
                if (s.error.isEmpty() || s.error.startsWith ("Latency exceeds"))
                {
                    if (reported < 0 || reported > triad::maxLatency || (s.enabled && chain + reported > triad::maxLatency))
                        s.error = "Latency exceeds supported limit; effect bypassed";
                    else if (s.error.startsWith ("Latency exceeds")) s.error.clear();
                }
                s.reportedLatency = reported;
                s.liveBypassed = liveMode.load() && reported > 0 && s.enabled && s.error.isEmpty();
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
        return chain;
    };
    // The PRE chain runs first, on the full sound; then the bands side by side, lined up to the slowest one; then POST.
    const int pre = juce::jmin (chainOf (*bands[(size_t) preBand]), triad::maxLatency);
    int longest = 0;
    for (int b = 0; b < numBands; ++b) longest = juce::jmax (longest, chainOf (*bands[(size_t) b]));
    longest = juce::jmin (longest, triad::maxLatency);
    for (int b = 0; b < numBands; ++b) bands[(size_t) b]->alignDelay.ensureCapacity (longest - bands[(size_t) b]->chainLatency);
    const int post = juce::jmin (chainOf (*bands[(size_t) postBand]), triad::maxLatency);   // POST: after the bands
    const int total = pre + longest + post;
    bypassDelay.ensureCapacity (total);
    changed |= totalLatency.load() != total || bandsLatency != longest;
    bandsLatency = longest;
    preLatency.store (pre);
    postLatency.store (post);
    totalLatency.store (total);
    tail.store (longestTail + (double) total / rate.load());
    if (changed && prepared) resetDelays();
    alignedSignature.store (latencySignature());
    if (getLatencySamples() != total) setLatencySamples (total);
    publishInfo();
}
void TriadProcessor::timerCallback()
{
    // Effects can change their latency at any time; check without the lock (the bands only change on this
    // thread) and take it only when something changed, so the audio thread is not kept waiting 30 times a
    // second for nothing (it outputs silence rather than wait, which is a dropout while recording).
    if (! startupDecided && juce::Time::getMillisecondCounterHiRes() - createdAt > startupDelayMs)
    {
        startupDecided = true;
        if (! stateReceived.load()) applyStartupPreset();
    }
    if (latencySignature() != alignedSignature.load())
    {
        const juce::ScopedTryLock lock (gate);
        if (lock.isLocked()) alignLatency();
    }
    handleInvalidAudio();
    historyTick();
}
juce::int64 TriadProcessor::latencySignature() const
{
    juce::uint64 h = 1469598103934665603ull;
    auto add = [&h] (juce::uint64 v) { h ^= v; h *= 1099511628211ull; };
    auto addDouble = [&add] (double v) { juce::uint64 bits = 0; std::memcpy (&bits, &v, sizeof bits); add (bits); };
    add ((juce::uint64) numBands);
    add ((juce::uint64) prepared);
    add ((juce::uint64) liveMode.load());
    addDouble (rate.load());
    for (int k = 0; k < numBands + 2; ++k)
    {
        const auto& band = *bands[(size_t) chainAt (k)];   // PRE, the bands, POST
        add ((juce::uint64) band.slots.size());
        for (auto& slotPtr : band.slots)
        {
            const auto& s = *slotPtr;
            add ((juce::uint64) reinterpret_cast<std::uintptr_t> (s.effect.get()));
            add ((juce::uint64) s.enabled);
            add ((juce::uint64) s.error.hashCode64());
            if (s.effect != nullptr)
            {
                add ((juce::uint64) (juce::int64) s.effect->getLatencySamples());
                add ((juce::uint64) (juce::int64) effectChannels (*s.effect));
                addDouble (s.effect->getTailLengthSeconds());
            }
        }
    }
    return (juce::int64) h;
}
void TriadProcessor::handleInvalidAudio()
{
    // The structure only changes on this (message) thread, so it can be read here without the lock.
    bool any = false;
    for (int k = 0; k < numBands + 2 && ! any; ++k)
        for (auto& s : bands[(size_t) chainAt (k)]->slots)
            any |= s->effect != nullptr && s->invalidBlocks.load (std::memory_order_relaxed) != s->invalidHandled;
    if (! any) return;
    juce::String message;
    {
        const juce::ScopedLock lock (gate);
        for (int k = 0; k < numBands + 2; ++k)
            for (auto& slotPtr : bands[(size_t) chainAt (k)]->slots)
            {
                auto& s = *slotPtr;
                const int bad = s.invalidBlocks.load (std::memory_order_relaxed);
                if (s.effect == nullptr || bad == s.invalidHandled) continue;
                s.invalidHandled = bad;
                // Its state probably holds NaN now: restart it (as a host does), up to three times.
                if (++s.invalidResets <= 3)
                {
                    if (prepared)
                    {
                        s.effect->releaseResources();
                        s.effect->setRateAndBufferSizeDetails (rate.load(), capacity);
                        s.effect->prepareToPlay (rate.load(), capacity);
                    }
                    s.dryDelay.reset();
                    message = s.description.name + " produced invalid audio (NaN); Triad silenced it and restarted it";
                }
                else
                {
                    s.error = "Produced invalid audio (NaN) repeatedly; bypassed";
                    message = s.description.name + " kept producing invalid audio (NaN); Triad bypassed it";
                }
            }
        alignLatency();
        publishInfo();
    }
    setStatus (message);
}
int TriadProcessor::getIdleEffectCount() { return EffectGraveyard::keeperCount(); }
int TriadProcessor::getRetiredEffectCount() { return EffectGraveyard::waitingCount(); }
int TriadProcessor::getLateRetiredEffectCount() { return EffectGraveyard::lateCount (3000); }
void TriadProcessor::describeRecentDeletions (char* out, size_t size) { EffectGraveyard::describeRecent (out, size); }
void TriadProcessor::releaseIdleEffects() { EffectGraveyard::releaseAll(); }
void TriadProcessor::retire (std::unique_ptr<juce::AudioPluginInstance> effect)
{
    if (effect != nullptr) EffectWatch::get().remove (*effect);
    if (effect == nullptr) return;
    effect->setPlayHead (nullptr);
    EffectGraveyard::get()->add (std::move (effect), {});
}
void TriadProcessor::retire (std::unique_ptr<juce::DocumentWindow> window)
{
    if (window == nullptr) return;
    triad::platform::release (*window);
    window->setVisible (false);
    EffectGraveyard::get()->add ({}, std::move (window));
}
void TriadProcessor::startFadeIn() { fadeRemaining = fadeLength; }
bool TriadProcessor::isBandMuted (int band) const
{
    return band >= 0 && band < triad::maxBands && bandMuteValues[(size_t) band]->load() >= 0.5f;
}
bool TriadProcessor::isBandSoloed (int band) const
{
    return band >= 0 && band < triad::maxBands && bandSoloValues[(size_t) band]->load() >= 0.5f;
}
bool TriadProcessor::isAnyBandSoloed() const
{
    for (int b = 0; b < getBandCount(); ++b) if (isBandSoloed (b)) return true;
    return false;
}
void TriadProcessor::setBandSoloed (int band, bool on)
{
    if (band < 0 || band >= getBandCount()) return;
    if (auto* p = parameters.getParameter (bandSoloID (band)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}
void TriadProcessor::setBandMuted (int band, bool on)
{
    if (band < 0 || band >= getBandCount()) return;
    if (auto* p = parameters.getParameter (bandMuteID (band)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}
// ---- LFOs on the band edges (0.18.0) -------------------------------------------------------------------
namespace
{
// Where a divider is, 0..1 from 20 Hz to 20 kHz in octaves (as the frequency panel draws it).
float edgeU (float hz) { return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (1.0f, hz) / 20.0f) / std::log (1000.0f)); }
float edgeHz (float u) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, u)); }
// What an LFO's value v (0..1) at amount a does to a band's edges uL, uH (0..1; the lowest band's low edge is 0, the
// highest band's high edge 1): the moves (in the same units) of the low and the high edge.
std::pair<float, float> edgeMove (int edge, float v, float a, float uL, float uH, bool locked = false)
{
    using D = TriadProcessor::Destination;
    const float c = 0.5f * (uL + uH), w = uH - uL;
    // LOCK (0.19.0): the edge stays inside the band as set; the line's height is the band's range (low cut: bottom = as
    // set, top = the band's top; high cut: top = as set, bottom = the band's bottom; widen / narrow: top = as set,
    // bottom = closed to its middle).
    if (locked && edge == D::lowEdge) return { a * v * w, 0.0f };
    if (locked && edge == D::highEdge) return { 0.0f, a * (v - 1.0f) * w };
    if (locked && edge == D::openEdges) return { a * 0.5f * (1.0f - v) * w, -a * 0.5f * (1.0f - v) * w };
    switch (edge)
    {
        case D::lowEdge:  return { a * (v - uL), 0.0f };
        case D::highEdge: return { 0.0f, a * (v - uH) };
        case D::moveEdges:
        {
            // The band's middle follows the line (top: as high as it can go, bottom: as low), its width stays.
            const float target = 0.5f * w + v * (1.0f - w);
            return { a * (target - c), a * (target - c) };
        }
        default:
        {
            // Open / close around the band's middle: top as wide as it can open, bottom closed to its middle.
            const float half = v * juce::jmax (c, 1.0f - c);
            return { a * ((c - half) - uL), a * ((c + half) - uH) };
        }
    }
}
// The line value that leaves the edges where they are (a new edge LFO starts flat there).
float restingValue (int edge, float uL, float uH)
{
    using D = TriadProcessor::Destination;
    const float c = 0.5f * (uL + uH), w = uH - uL;
    if (edge == D::lowEdge) return uL;
    if (edge == D::highEdge) return uH;
    if (edge == D::moveEdges) return w < 0.999f ? juce::jlimit (0.0f, 1.0f, uL / (1.0f - w)) : 0.5f;
    return juce::jlimit (0.0f, 1.0f, 0.5f * w / juce::jmax (0.001f, juce::jmax (c, 1.0f - c)));
}
}
juce::String TriadProcessor::edgeName (int edge)
{
    return edge == Destination::lowEdge ? "Low cut" : edge == Destination::highEdge ? "High cut" : edge == Destination::moveEdges ? "Sweep the band" : "Widen / narrow";
}
double TriadProcessor::predictPhase (const Band& band, const Lfo& l, int offset, int frame) const noexcept
{
    // Where the LFO will be at `frame` of this chunk, worked out before the band's LFOs move on (they do after the
    // split). With AUDIO, a restart in this chunk shows from the next one on.
    const double sr = rate.load();
    const double beatsPerSample = blockBpm / 60.0 / sr;
    const double beats = triad::mod::lengthInBeats (l.length.load (std::memory_order_relaxed), blockBeatsPerBar);
    double p = 0.0;
    if (band.trigger.wasAudio)
    {
        if (band.trigger.waiting) return 0.0;
        const double speed = beats > 0.0 ? beatsPerSample / beats : triad::mod::lengthInHz (l.length.load (std::memory_order_relaxed)) / sr;
        p = l.runPhase + speed * frame;
    }
    else if (beats > 0.0)
        p = blockPlaying ? (blockPpq + (offset + frame) * beatsPerSample) / beats : l.runPhase + beatsPerSample / beats * frame;
    else
        p = l.runPhase + triad::mod::lengthInHz (l.length.load (std::memory_order_relaxed)) / sr * frame;
    return p - std::floor (p);
}
bool TriadProcessor::computeEdgeMoves (int offset, int n)
{
    const int crossovers = numBands - 1;
    if (crossovers <= 0) return false;
    bool any = false;
    for (int b = 0; b < numBands && ! any; ++b)
        for (auto* r : bands[(size_t) b]->edgeModulators) any = any || ! r->muted.load (std::memory_order_relaxed);
    const int pieces = (n + controlRate - 1) / controlRate;
    if (! any || (size_t) pieces * triad::maxCrossovers > edgeMoves.size()) return false;
    std::array<float, triad::maxCrossovers> knobHz {}, knobU {}, moved {};
    readCrossovers (knobHz.data());
    triad::constrainCrossovers (knobHz.data(), crossovers, rate.load());
    for (int j = 0; j < crossovers; ++j) knobU[(size_t) j] = edgeU (knobHz[(size_t) j]);
    for (int k = 0; k < pieces; ++k)
    {
        const int frame = juce::jmin ((k + 1) * controlRate, n);   // the dividers reach this piece's value at its end
        moved.fill (0.0f);
        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = *bands[(size_t) b];
            for (auto* r : band.edgeModulators)
            {
                if (r->muted.load (std::memory_order_relaxed) || r->lfo == nullptr) continue;
                float v = r->lfo->table.lookup (predictPhase (band, *r->lfo, offset, frame));
                if (r->invert.load (std::memory_order_relaxed)) v = 1.0f - v;
                const float uL = b > 0 ? knobU[(size_t) b - 1] : 0.0f, uH = b < crossovers ? knobU[(size_t) b] : 1.0f;
                const auto m = edgeMove (r->destination.slotId, v, r->amount.load (std::memory_order_relaxed), uL, uH, r->locked.load (std::memory_order_relaxed));
                if (b > 0) moved[(size_t) b - 1] += m.first;
                if (b < crossovers) moved[(size_t) b] += m.second;
            }
        }
        float* piece = edgeMoves.data() + (size_t) k * triad::maxCrossovers;
        for (int j = 0; j < crossovers; ++j)
            piece[j] = std::log2 (edgeHz (knobU[(size_t) j] + moved[(size_t) j]) / juce::jmax (1.0f, knobHz[(size_t) j]));
    }
    return true;
}
std::vector<float> TriadProcessor::heardCrossovers() const
{
    auto cuts = effectiveCrossovers();
    if (splitter.isMoving())
        for (int j = 0; j < (int) cuts.size(); ++j) cuts[(size_t) j] = splitter.getHeard (j);
    return cuts;
}
bool TriadProcessor::areCrossoversMoving() const { return prepared && splitter.isMoving(); }
std::pair<float, float> TriadProcessor::crossoverSweep (int index) const
{
    // Message thread: over the LFOs' whole cycles, where the divider goes (with every edge LFO of the bands on
    // either side of it).
    const int count = getBandCount();
    if (index < 0 || index >= count - 1) return {};
    const auto cuts = effectiveCrossovers();
    std::vector<float> knobU;
    for (auto hz : cuts) knobU.push_back (edgeU (hz));
    std::vector<std::pair<int, const Route*>> movers;
    for (int b : { index, index + 1 })
        for (auto& r : bands[(size_t) b]->routes)
            if (r->destination.kind == Destination::bandEdges && ! r->muted.load() && r->lfo != nullptr)
            {
                // Only what moves this divider: the band below's high edge, the band above's low edge, or both edges.
                const int edge = r->destination.slotId;
                if (edge == (b == index ? Destination::lowEdge : Destination::highEdge)) continue;
                movers.push_back ({ b, r.get() });
            }
    if (movers.empty()) return {};
    float low = 1.0f, high = 0.0f;
    for (int i = 0; i <= 64; ++i)
    {
        // Every LFO at the same point of its cycle (a fair picture when they share a length).
        float move = 0.0f;
        for (auto& [b, r] : movers)
        {
            float v = triad::mod::evaluate (r->lfo->shape, (float) i / 64.0f);
            if (r->invert.load()) v = 1.0f - v;
            const float uL = b > 0 ? knobU[(size_t) b - 1] : 0.0f, uH = b < count - 1 ? knobU[(size_t) b] : 1.0f;
            const auto m = edgeMove (r->destination.slotId, v, r->amount.load(), uL, uH, r->locked.load());
            move += b == index ? m.second : m.first;
        }
        const float u = juce::jlimit (0.0f, 1.0f, knobU[(size_t) index] + move);
        low = juce::jmin (low, u); high = juce::jmax (high, u);
    }
    return { edgeHz (low), edgeHz (high) };
}
int TriadProcessor::findEdgeLfo (int band, int edge) const
{
    if (band < 0 || band >= getBandCount()) return -1;
    const auto& b = *bands[(size_t) band];
    for (auto& r : b.routes)
        if (r->destination.kind == Destination::bandEdges && r->destination.slotId == edge) return b.lfoIndex (r->lfoId);
    return -1;
}
int TriadProcessor::linkEdgesToLfo (int band, int edge)
{
    if (band < 0 || band >= getBandCount() || getBandCount() < 2 || edge < Destination::lowEdge || edge > Destination::openEdges) return -1;
    if ((edge == Destination::lowEdge && band == 0) || (edge == Destination::highEdge && band == getBandCount() - 1)) return -1;
    if (const int existing = findEdgeLfo (band, edge); existing >= 0) return existing;
    const int lfo = addLfo (band);
    if (lfo < 0) return -1;
    // It starts flat where the edges are now, so nothing moves until its line is drawn.
    const auto cuts = effectiveCrossovers();
    const float uL = band > 0 ? edgeU (cuts[(size_t) band - 1]) : 0.0f, uH = band < (int) cuts.size() ? edgeU (cuts[(size_t) band]) : 1.0f;
    setLfoShape (band, lfo, triad::mod::flat (restingValue (edge, uL, uH)), false);
    addRoute (band, lfo, Destination { Destination::bandEdges, edge });
    setStatus ("Linked band " + juce::String (band + 1) + "'s " + edgeName (edge).toLowerCase() + " to LFO " + juce::String (lfo + 1));
    return lfo;
}
juce::Optional<juce::AudioPlayHead::PositionInfo> TriadProcessor::ChunkPlayHead::getPosition() const
{
    // The DAW's position at the first sample of the piece an effect is playing now (Triad plays its effects in
    // pieces of the DAW's block: knobs that follow LFOs are set every 64 samples), so tempo-synced effects stay in time.
    if (host == nullptr) return {};
    auto position = host->getPosition();
    if (! position.hasValue() || offset == 0 || ! position->getIsPlaying()) return position;
    auto info = *position;
    const double seconds = offset / juce::jmax (1.0, rate);
    if (const auto samples = info.getTimeInSamples()) info.setTimeInSamples (*samples + offset);
    if (const auto time = info.getTimeInSeconds()) info.setTimeInSeconds (*time + seconds);
    if (const auto ppq = info.getPpqPosition())
        if (const auto bpm = info.getBpm()) info.setPpqPosition (*ppq + seconds * *bpm / 60.0);
    return info;
}

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
void TriadProcessor::detectTrigger (Band& band, const juce::AudioBuffer<float>& source, int start, int frames)
{
    auto& t = band.trigger;
    t.numHits = 0;
    if (! t.audio.load (std::memory_order_relaxed)) { t.wasAudio = false; return; }
    if (! t.wasAudio)
    {
        // Just switched to AUDIO: wait for sound.
        t.wasAudio = true;
        t.armed = true;
        t.waiting = true;
        t.envelope = 0.0f;
        t.quiet = 0;
    }
    t.waitingAtStart = t.waiting;
    const double sr = rate.load();
    const double secondsPerBeat = 60.0 / blockBpm;
    const float threshold = juce::Decibels::decibelsToGain (juce::jlimit (minTriggerDb, maxTriggerDb, t.thresholdDb.load (std::memory_order_relaxed)));
    const float quietBelow = threshold * 0.5f;   // 6 dB under: quiet again (so a level hovering at the threshold is one sound)
    const float release = (float) std::exp (-1.0 / (0.01 * sr));   // the level falls back over about 10 ms
    const int rearm = (int) (sr * (t.afterPause.load (std::memory_order_relaxed) ? juce::jmax (0.1, secondsPerBeat) : 0.03));
    const int idle = (int) (sr * 2.0 * blockBeatsPerBar * secondsPerBeat);
    const float* left = source.getReadPointer (0, start);
    const float* right = source.getReadPointer (juce::jmin (1, source.getNumChannels() - 1), start);
    float loudest = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const float x = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        t.envelope = x > t.envelope ? x : t.envelope * release;
        loudest = juce::jmax (loudest, t.envelope);
        if (t.envelope >= threshold)
        {
            t.quiet = 0;
            if (t.armed)
            {
                t.armed = false;
                t.waiting = false;
                if (t.numHits < Lfo::maxRestarts) t.hits[(size_t) t.numHits++] = i;
                else t.hits[(size_t) Lfo::maxRestarts - 1] = i;
                t.count.fetch_add (1, std::memory_order_relaxed);
            }
        }
        else if (t.envelope < quietBelow)
        {
            if (t.quiet < std::numeric_limits<int>::max()) ++t.quiet;
            if (t.quiet >= rearm) t.armed = true;
            if (t.quiet >= idle) t.waiting = true;   // two bars of quiet: back to the start, waiting
        }
    }
    if (loudest > t.level.load (std::memory_order_relaxed)) t.level.store (loudest, std::memory_order_relaxed);
    t.idle.store (t.waiting, std::memory_order_relaxed);
}
void TriadProcessor::advanceLfos (Band& band, int offset, int frames)
{
    // While the song plays, LFOs follow the song position; when stopped they keep running at its tempo.
    // With the audio trigger they start from their start where sound comes in (detectTrigger), at the song's tempo.
    const double sr = rate.load();
    const double beatsPerSample = blockBpm / 60.0 / sr;
    const auto& t = band.trigger;
    const bool audioTrigger = t.wasAudio;
    for (auto& lfoPtr : band.lfos)
    {
        auto& l = *lfoPtr;
        const int length = l.length.load (std::memory_order_relaxed);
        const double beats = triad::mod::lengthInBeats (length, blockBeatsPerBar);
        l.numRestarts = 0;
        if (audioTrigger)
        {
            const double speed = beats > 0.0 ? beatsPerSample / beats : triad::mod::lengthInHz (length) / sr;
            l.increment = t.waitingAtStart ? 0.0 : speed;           // waiting: held at its start
            l.blockPhase = t.waitingAtStart ? 0.0 : l.runPhase;
            l.restartIncrement = speed;
            l.numRestarts = t.numHits;
            for (int k = 0; k < t.numHits; ++k) l.restarts[(size_t) k] = t.hits[(size_t) k];
            double end = t.numHits > 0 ? speed * (frames - t.hits[(size_t) t.numHits - 1]) : l.blockPhase + l.increment * frames;
            if (t.waiting) end = 0.0;
            l.runPhase = end - std::floor (end);
            l.displayPhase.store ((float) l.runPhase, std::memory_order_relaxed);
            l.clock.set ((float) l.runPhase, t.waiting ? 0.0 : speed * sr);
            continue;
        }
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
        l.clock.set ((float) l.runPhase, l.increment * sr);
    }
}
void TriadProcessor::applyParameterModulation (Slot& slot, int frame)
{
    for (auto* r : slot.parameterModulators)
    {
        const float base = r->base.load (std::memory_order_relaxed);
        if (r->muted.load (std::memory_order_relaxed))
        {
            // Switched off: the knob goes back to where it was when it was linked.
            if (r->lastSent >= 0.0f) { r->parameter->setValue (base); r->lastSent = -1.0f; }
            continue;
        }
        const float value = juce::jlimit (0.0f, 1.0f, base + r->amount.load (std::memory_order_relaxed) * (r->value (frame) - base));
        if (std::abs (value - r->lastSent) > 1.0e-4f) { r->parameter->setValue (value); r->lastSent = value; }
    }
}
void TriadProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    // CPU meter: the time this block took, against the time the audio lasts.
    struct Timed
    {
        TriadProcessor& p; int samples; juce::int64 start = juce::Time::getHighResolutionTicks();
        ~Timed()
        {
            p.busyTicks.fetch_add (juce::Time::getHighResolutionTicks() - start, std::memory_order_relaxed);
            p.audioSamples.fetch_add (samples, std::memory_order_relaxed);
        }
    } timed { *this, audio.getNumSamples() };
    juce::ScopedNoDenormals denormals;
    midi.clear();
    spectrum.push (audio, audio.getNumSamples());
    storeMax (inputPeak, peakOf (audio, 2));
    readSongPosition();
    const juce::ScopedTryLock lock (gate);
    if (! lock.isLocked() || ! prepared || restoring.load() || muted.load() || audio.getNumChannels() < 2)
    {
        // Counted: the bands were being changed (the lock was taken, or a band was added, removed or moved and its
        // dividers and knobs not yet set). Not counted: before the DAW prepares Triad, and while a project loads.
        if (prepared && (! lock.isLocked() || muted.load())) silencedBlocks.fetch_add (1, std::memory_order_relaxed);
        audio.clear();
        return;
    }
    // Never emit misaligned audio while a hosted effect changes its reported latency.
    for (int k = 0; k < numBands + 2; ++k)
        for (auto& s : bands[(size_t) chainAt (k)]->slots)
            if (s->isActive() && (s->effect->getLatencySamples() != s->latency || effectChannels (*s->effect) != s->channels))
            { audio.clear(); return; }
    process (audio);
    storeMax (outputPeak, peakOf (audio, 2));
}
bool TriadProcessor::runChain (Band& band, juce::AudioBuffer<float>& buffer, int n)
{
    // The band's chain: each active slot processes the output of the one above it.
    for (auto& slotPtr : band.slots)
    {
        auto& slot = *slotPtr;
        if (! slot.isActive()) { slot.mix.skip (n); continue; }
        for (int ch = 0; ch < 2; ++ch) band.slotDry.copyFrom (ch, 0, buffer, ch, 0, n);
        slot.effect->setPlayHead (getPlayHead() != nullptr ? &chunkHead : nullptr);   // the song position at its own samples
        slot.effect->setNonRealtime (isNonRealtime());
        auto runEffect = [&] (int start, int length)
        {
            chunkHead.offset = chunkBase + start;
            childMidi.clear();
            if (slot.channels > 2)
            {
                // An effect with an extra bus that stays on: give it silence there.
                for (int ch = 0; ch < slot.channels; ++ch)
                    if (ch < 2) wide.copyFrom (ch, start, buffer, ch, start, length);
                    else wide.clear (ch, start, length);
                juce::AudioBuffer<float> view (wide.getArrayOfWritePointers(), slot.channels, start, length);
                slot.effect->processBlock (view, childMidi);
                for (int ch = 0; ch < 2; ++ch) buffer.copyFrom (ch, start, wide, ch, start, length);
            }
            else
            {
                juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start, length);
                slot.effect->processBlock (view, childMidi);
            }
        };
        if (slot.parameterModulators.empty()) runEffect (0, n);
        else
        {
            // Knobs inside the effect follow their LFOs: set before every 64 samples it processes.
            for (int start = 0; start < n; start += parameterRate)
            {
                applyParameterModulation (slot, start);
                runEffect (start, juce::jmin (parameterRate, n - start));
            }
        }
        // An effect that outputs NaN/Inf would break the whole mix in the DAW: silence it.
        if (! allFinite (buffer, 2, n))
        {
            for (int ch = 0; ch < 2; ++ch) buffer.clear (ch, 0, n);
            slot.invalidBlocks.fetch_add (1, std::memory_order_relaxed);
        }
        if (slot.effect->getLatencySamples() != slot.latency) return false;
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
    return true;
}
void TriadProcessor::process (juce::AudioBuffer<float>& audio)
{
    std::array<float, triad::maxCrossovers> cuts {};
    std::array<int, triad::maxCrossovers> slopes {};
    readCrossovers (cuts.data());
    readSlopes (slopes.data());
    splitter.setCutoffs (cuts.data());
    splitter.setSlopes (slopes.data());
    const int longest = bandsLatency;   // the bands are lined up to the slowest one (PRE came before them)
    chunkHead.host = getPlayHead();
    chunkHead.rate = rate.load();
    for (int offset = 0; offset < audio.getNumSamples(); offset += capacity)
    {
        chunkBase = offset;
        const int n = juce::jmin (capacity, audio.getNumSamples() - offset);
        // The PRE chain, on the full-range sound, before it is split into bands.
        auto& pre = *bands[(size_t) preBand];
        if (std::any_of (pre.slots.begin(), pre.slots.end(), [] (const auto& slotPtr) { return slotPtr->isActive(); }))
        {
            detectTrigger (pre, audio, offset, n);   // PRE listens to Triad's input (BAND and FULL alike)
            advanceLfos (pre, offset, n);
            traceLfo (pre, preBand, audio, offset, n);
            for (int ch = 0; ch < 2; ++ch) pre.dry.copyFrom (ch, 0, audio, ch, offset, n);
            if (! runChain (pre, pre.dry, n)) { audio.clear(); return; }
            for (int ch = 0; ch < 2; ++ch) audio.copyFrom (ch, offset, pre.dry, ch, 0, n);
        }
        for (int ch = 0; ch < 2; ++ch) fullSound.copyFrom (ch, 0, audio, ch, offset, n);   // what LISTEN: FULL hears
        // LFOs on the band edges move the dividers as the sound is split (0.18.0).
        const bool edgesMove = computeEdgeMoves (offset, n);
        splitter.process (audio, offset, n, split, edgesMove ? edgeMoves.data() : nullptr, controlRate);
        for (int ch = 0; ch < 2; ++ch) audio.clear (ch, offset, n);
        // SOLO: while any band is soloed, the others go silent (smoothly, as MUTE).
        bool anySolo = false;
        for (int b = 0; b < numBands; ++b) anySolo = anySolo || bandSoloValues[(size_t) b]->load (std::memory_order_relaxed) >= 0.5f;
        for (int b = 0; b < numBands; ++b)
        {
            auto& band = *bands[(size_t) b];
            auto& buffer = split[(size_t) b];
            detectTrigger (band, band.trigger.listenFull.load (std::memory_order_relaxed) ? fullSound : buffer, 0, n);
            advanceLfos (band, offset, n);
            traceLfo (band, b, band.trigger.listenFull.load (std::memory_order_relaxed) ? fullSound : buffer, 0, n);
            for (int ch = 0; ch < 2; ++ch) band.dry.copyFrom (ch, 0, buffer, ch, 0, n);
            // The band's chain: each active slot processes the output of the one above it.
            if (! runChain (band, buffer, n)) { audio.clear(); return; }
            // Band mix (dry band vs. its chain) and volume, then align all bands to the longest chain.
            const int align = longest - band.chainLatency;
            const float mixKnob = bandMixValues[(size_t) b]->load (std::memory_order_relaxed) * 0.01f;
            // MUTE: the band's volume goes to silence (smoothly, as the volume knob does), its effects keep playing.
            const bool bandMuted = bandMuteValues[(size_t) b]->load (std::memory_order_relaxed) >= 0.5f
                                   || (anySolo && bandSoloValues[(size_t) b]->load (std::memory_order_relaxed) < 0.5f);
            const float gainKnob = bandMuted ? 0.0f
                                             : juce::Decibels::decibelsToGain (bandGainValues[(size_t) b]->load (std::memory_order_relaxed), minGainDb - 0.05f);
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
        // The POST chain, on the bands added back together.
        auto& post = *bands[(size_t) postBand];
        if (std::any_of (post.slots.begin(), post.slots.end(), [] (const auto& slotPtr) { return slotPtr->isActive(); }))
        {
            if (post.trigger.listenFull.load (std::memory_order_relaxed)) detectTrigger (post, fullSound, 0, n);
            else detectTrigger (post, audio, offset, n);   // POST's own sound: the bands added together
            advanceLfos (post, offset, n);
            if (post.trigger.listenFull.load (std::memory_order_relaxed)) traceLfo (post, postBand, fullSound, 0, n);
            else traceLfo (post, postBand, audio, offset, n);
            for (int ch = 0; ch < 2; ++ch) post.dry.copyFrom (ch, 0, audio, ch, offset, n);
            if (! runChain (post, post.dry, n)) { audio.clear(); return; }
            for (int ch = 0; ch < 2; ++ch) audio.copyFrom (ch, offset, post.dry, ch, 0, n);
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
    const auto samples = audioSamples.exchange (0);
    const double busy = juce::Time::highResolutionTicksToSeconds (busyTicks.exchange (0));
    m.cpu = samples > 0 ? (float) (busy / ((double) samples / juce::jmax (1.0, rate.load()))) : 0.0f;
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
void TriadProcessor::stateChanged (bool undoable)
{
    if (undoable) noteChange();
    // Slot mixes, LFOs and the layout are not DAW parameters: tell the host the project changed.
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

// ---- Bands --------------------------------------------------------------------------------------------
bool TriadProcessor::addBand (float hz)
{
    int index = 0, count = 0;
    std::array<float, triad::maxCrossovers> next {};
    std::array<int, triad::maxCrossovers> slopes {}, nextSlopes {};
    readSlopes (slopes.data());
    nextSlopes.fill (triad::CrossoverFilter::defaultOrder);
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
        for (int j = 0; j < (int) cuts.size(); ++j)
        {
            next[(size_t) (j < b ? j : j + 1)] = cuts[(size_t) j];
            nextSlopes[(size_t) (j < b ? j : j + 1)] = slopes[(size_t) j];
        }
        next[(size_t) b] = cut;
        nextSlopes[(size_t) b] = triad::CrossoverFilter::defaultOrder;
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
        splitter.setBandCount (numBands, next.data(), nextSlopes.data());
        alignLatency();
        startFadeIn();
        setStatus ("Added band " + juce::String (b + 2) + " at " + juce::String (juce::roundToInt (cut)) + " Hz");
    }
    for (int j = 0; j < count - 1; ++j) setParameterValue (crossoverID (j), next[(size_t) j]);
    for (int j = 0; j < triad::maxCrossovers; ++j) setParameterValue (slopeID (j), (float) nextSlopes[(size_t) j]);
    for (int p = count - 1; p > index + 1; --p)
    {
        setParameterValue (bandMixID (p), getParameterValue (bandMixID (p - 1)));
        setParameterValue (bandGainID (p), getParameterValue (bandGainID (p - 1)));
        setParameterValue (bandMuteID (p), getParameterValue (bandMuteID (p - 1)));
        setParameterValue (bandSoloID (p), getParameterValue (bandSoloID (p - 1)));
    }
    setParameterValue (bandMixID (index + 1), 100.0f);
    setParameterValue (bandGainID (index + 1), 0.0f);
    setParameterValue (bandMuteID (index + 1), getParameterValue (bandMuteID (index)));   // half of a muted band stays muted
    setParameterValue (bandSoloID (index + 1), getParameterValue (bandSoloID (index)));   // and of a soloed one, soloed
    muted.store (false);
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}
bool TriadProcessor::removeBand (int band)
{
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    std::unique_ptr<Band> removed;
    int count = 0;
    bool wasPrepared = false;
    std::array<float, triad::maxCrossovers> next {};
    std::array<int, triad::maxCrossovers> slopes {}, nextSlopes {};
    readSlopes (slopes.data());
    nextSlopes.fill (triad::CrossoverFilter::defaultOrder);
    {
        const juce::ScopedLock lock (gate);
        if (numBands <= 1 || band < 0 || band >= numBands) return false;
        const auto cuts = effectiveCrossovers();
        // The band's range joins its lower neighbour (or the upper one, for the lowest band).
        const int removedCrossover = band == 0 ? 0 : band - 1;
        for (int j = 0, k = 0; j < (int) cuts.size(); ++j)
            if (j != removedCrossover) { nextSlopes[(size_t) k] = slopes[(size_t) j]; next[(size_t) k++] = cuts[(size_t) j]; }
        muted.store (true);
        removed = std::move (bands[(size_t) band]);
        for (int p = band; p < numBands - 1; ++p) bands[(size_t) p] = std::move (bands[(size_t) p + 1]);
        bands[(size_t) numBands - 1] = std::make_unique<Band>();
        prepareBand (numBands - 1, false);
        --numBands;
        count = numBands;
        bandCount.store (numBands);
        wasPrepared = prepared;
        splitter.setBandCount (numBands, next.data(), nextSlopes.data());
        alignLatency();
        startFadeIn();
        setStatus ("Removed band " + juce::String (band + 1));
    }
    for (auto& s : removed->slots)
    {
        if (s->effect && wasPrepared) s->effect->releaseResources();   // out of the bands: no lock needed
        retire (std::move (s->window)); retire (std::move (s->effect));
    }
    removed.reset();
    for (int j = 0; j < count - 1; ++j) setParameterValue (crossoverID (j), next[(size_t) j]);
    for (int j = 0; j < triad::maxCrossovers; ++j) setParameterValue (slopeID (j), (float) nextSlopes[(size_t) j]);
    for (int p = band; p < count; ++p)
    {
        setParameterValue (bandMixID (p), getParameterValue (bandMixID (p + 1)));
        setParameterValue (bandGainID (p), getParameterValue (bandGainID (p + 1)));
        setParameterValue (bandMuteID (p), getParameterValue (bandMuteID (p + 1)));
        setParameterValue (bandSoloID (p), getParameterValue (bandSoloID (p + 1)));
    }
    setParameterValue (bandMixID (count), 100.0f);
    setParameterValue (bandGainID (count), 0.0f);
    setParameterValue (bandMuteID (count), 0.0f);
    setParameterValue (bandSoloID (count), 0.0f);
    muted.store (false);
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
    return true;
}
bool TriadProcessor::moveBand (int from, int to)
{
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    const int count = getBandCount();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) return false;
    std::vector<int> order ((size_t) count);
    std::iota (order.begin(), order.end(), 0);
    order.erase (order.begin() + from);
    order.insert (order.begin() + to, from);   // new position i holds old band order[i]
    std::array<float, triad::maxBands> mixes {}, gains {}, mutes {}, solos {};
    for (int i = 0; i < count; ++i)
    {
        mixes[(size_t) i] = getParameterValue (bandMixID (i));
        gains[(size_t) i] = getParameterValue (bandGainID (i));
        mutes[(size_t) i] = getParameterValue (bandMuteID (i));
        solos[(size_t) i] = getParameterValue (bandSoloID (i));
    }
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
        setParameterValue (bandMuteID (i), mutes[(size_t) order[(size_t) i]]);
        setParameterValue (bandSoloID (i), solos[(size_t) order[(size_t) i]]);
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
    std::array<std::vector<SlotInfo>, triad::maxBands + 2> next;
    for (int k = 0; k < numBands + 2; ++k)
        for (auto& slotPtr : bands[(size_t) chainAt (k)]->slots)
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
            n.reportedLatency = s.effect != nullptr ? s.reportedLatency : 0;
            n.liveBypassed = s.liveBypassed;
            n.channels = s.effect != nullptr ? s.channels : 2;
            n.invalidAudio = s.effect != nullptr ? s.invalidBlocks.load (std::memory_order_relaxed) : 0;
            n.mix = s.mixPercent.load();
            next[(size_t) chainAt (k)].push_back (std::move (n));
        }
    const juce::ScopedLock lock (infoLock);
    info = std::move (next);
}
juce::String TriadProcessor::chainName (int band)
{
    return band == preBand ? juce::String ("PRE") : band == postBand ? juce::String ("POST") : "band " + juce::String (band + 1);
}
bool TriadProcessor::hasChainEffects (int band) const
{
    if (band < 0 || band > postBand) return false;
    const juce::ScopedLock lock (infoLock);
    const auto& list = info[(size_t) band];
    return std::any_of (list.begin(), list.end(), [] (const SlotInfo& i) { return i.hasContent; });
}
int TriadProcessor::getSlotCount (int band) const
{
    const juce::ScopedLock lock (infoLock);
    return band >= 0 && band <= postBand ? (int) info[(size_t) band].size() : 0;
}
TriadProcessor::SlotInfo TriadProcessor::getSlotInfo (int band, int slot) const
{
    SlotInfo result;
    {
        const juce::ScopedLock lock (infoLock);
        if (band < 0 || band > postBand || slot < 0 || slot >= (int) info[(size_t) band].size()) return {};
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
juce::AudioPluginInstance* TriadProcessor::getHostedEffect (int band, int slot) const
{
    auto* s = slotAt (band, slot);
    return s != nullptr ? s->effect.get() : nullptr;
}
float TriadProcessor::getSlotLiveMix (int band, int slot, bool& modulated) const
{
    modulated = false;
    auto* s = slotAt (band, slot);
    if (s == nullptr) return 100.0f;
    modulated = std::any_of (s->modulators.begin(), s->modulators.end(), [] (Route* r) { return ! r->muted.load(); });
    return modulated && s->isActive() ? s->liveMix.load() : s->mixPercent.load();
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
        if (! isExtraChain (band) && (band < 0 || band >= numBands)) return -1;
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
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    std::unique_ptr<Slot> removed;
    bool wasPrepared = false;
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
        wasPrepared = prepared;
        alignLatency();
        setStatus (removed->description.name.isNotEmpty() ? "Removed " + removed->description.name : "Removed slot");
    }
    if (removed->effect && wasPrepared) removed->effect->releaseResources();   // out of the band: no lock needed
    retire (std::move (removed->window));   // an editor must be deleted before its processor (it is: oldest first)
    retire (std::move (removed->effect));
    ++structureVersion;
    refreshWindowTitles();
    stateChanged();
}
juce::String TriadProcessor::loadEffect (int band, int slot, const juce::PluginDescription& desc, bool openEditor)
{
    juce::String result;
    if (isTriad (desc)) result = "Triad cannot host itself.";
    else if (desc.isInstrument) result = "Choose an audio effect, not an instrument.";
    else if (! isValidBand (band)) result = "That band does not exist.";
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
juce::String TriadProcessor::loadInstance (int band, int slot, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr) return "No effect.";
    if (! isValidBand (band)) return "That band does not exist.";
    const bool append = slot == getSlotCount (band);
    if (append) slot = addSlot (band);
    if (slot < 0 || slot >= getSlotCount (band)) return "That slot does not exist.";
    const auto desc = instance->getPluginDescription();
    const auto result = finishInstall (band, slot, desc, {}, std::move (instance), {}, false);
    if (result.isNotEmpty() && append) removeSlot (band, slot);
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
            if (! self->findPending (request, b, s))
            {
                if (instance != nullptr) self->retire (std::move (instance));   // the slot was removed meanwhile
            }
            else
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
    for (int k = 0; k < numBands + 2; ++k)
    {
        const int index = chainAt (k);
        for (int s = 0; s < (int) bands[(size_t) index]->slots.size(); ++s)
            if (bands[(size_t) index]->slots[(size_t) s]->pendingRequest == request) { band = index; slot = s; return true; }
    }
    return false;
}
juce::String TriadProcessor::finishInstall (int band, int slotIndex, const juce::PluginDescription& desc, const juce::MemoryBlock& state,
                                            std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& creationError,
                                            bool openEditor)
{
    endParameterLearn();
    if (! instance) return creationError.isNotEmpty() ? creationError : "Could not open this effect.";
    // An effect that is turned down below (not stereo, the slot gone...) is deleted like any removed effect, after
    // a while, never in the same moment it was made: its own start-up messages are still on their way.
    struct RetireUnused
    {
        TriadProcessor& owner;
        std::unique_ptr<juce::AudioPluginInstance>& unused;
        ~RetireUnused() { if (unused != nullptr) owner.retire (std::move (unused)); }
    } retireUnused { *this, instance };
    const auto actual = instance->getPluginDescription();
    if (isTriad (actual)) return "Triad cannot host itself.";
    if (desc.isInstrument || actual.isInstrument) return "Choose an audio effect, not an instrument.";
    instance->disableNonMainBuses();
    const auto offered = instance->getBusesLayout();
    if (offered.inputBuses.isEmpty() || offered.outputBuses.isEmpty()) return "This effect has no audio input or output.";
    auto stereoMain = [] (juce::AudioProcessor::BusesLayout layout, bool extrasOff)
    {
        if (extrasOff)
        {
            for (int j = 1; j < layout.inputBuses.size(); ++j) layout.inputBuses.getReference (j) = juce::AudioChannelSet::disabled();
            for (int j = 1; j < layout.outputBuses.size(); ++j) layout.outputBuses.getReference (j) = juce::AudioChannelSet::disabled();
        }
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        return layout;
    };
    // Stereo in/out with any extra buses (sidechains) off. Some formats cannot switch an extra bus
    // off (an AU's sidechain input, say); then it stays on and Triad feeds it silence.
    if (! instance->setBusesLayout (stereoMain (offered, true)))
        instance->setBusesLayout (stereoMain (instance->getBusesLayout(), false));
    if (effectChannels (*instance) == 0)
        return "Triad hosts stereo effects; this one offers " + describeLayout (offered) + ".";
    if (state.getSize() > 0) instance->setStateInformation (state.getData(), (int) state.getSize());
    if (effectChannels (*instance) == 0)
        return "The effect changed to an unsupported channel layout (" + describeLayout (instance->getBusesLayout()) + ").";
    std::unique_ptr<juce::AudioPluginInstance> old;
    std::unique_ptr<juce::DocumentWindow> oldWindow;
    // The new effect is prepared before it goes in (it does not play yet), so the audio thread is not held up
    // while it starts (some effects take a while): loading an effect while recording does not cause a gap.
    double preparedRate = 0.0;
    int preparedCapacity = 0;
    bool wasPrepared = false;
    {
        const juce::ScopedLock lock (gate);
        preparedRate = rate.load(); preparedCapacity = capacity; wasPrepared = prepared;
    }
    instance->setRateAndBufferSizeDetails (preparedRate, preparedCapacity);
    if (wasPrepared) instance->prepareToPlay (preparedRate, preparedCapacity);
    {
        const juce::ScopedLock lock (gate);
        auto* s = slotAt (band, slotIndex);
        if (s == nullptr) return "That slot no longer exists.";
        if (prepared != wasPrepared || ! juce::exactlyEqual (rate.load(), preparedRate) || capacity != preparedCapacity)
        {
            // The DAW changed the sample rate or block size meanwhile (rare): prepare it again.
            if (wasPrepared) instance->releaseResources();
            instance->setRateAndBufferSizeDetails (rate.load(), capacity);
            if (prepared) instance->prepareToPlay (rate.load(), capacity);
            wasPrepared = prepared;
        }
        oldWindow = std::move (s->window);
        old = std::move (s->effect);
        s->effect = std::move (instance);
        s->description = actual.name.isNotEmpty() ? actual : desc;
        // Keep the location the effect was opened from (an instance may report the binary inside its bundle).
        if (desc.fileOrIdentifier.isNotEmpty()) s->description.fileOrIdentifier = desc.fileOrIdentifier;
        s->savedState = state;
        s->error.clear();
        s->pendingRequest = 0;
        s->channels = effectChannels (*s->effect);
        s->invalidBlocks = 0; s->invalidHandled = 0; s->invalidResets = 0;
        s->dryDelay.reset();
        bands[(size_t) band]->relink();   // links to knobs inside the effect find the new effect's parameters
        alignLatency();
        EffectWatch::get().add (*s->effect, historySignal);   // knobs turned in its window are undo steps
    }
    if (old != nullptr && wasPrepared) old->releaseResources();   // it no longer plays: no lock needed
    retire (std::move (oldWindow));   // an editor must be deleted before its processor (it is: oldest first)
    retire (std::move (old));
    EffectGraveyard::renew();   // after whatever this effect's format needed
    stateChanged();
    if (openEditor) showEffect (band, slotIndex);
    return {};
}
void TriadProcessor::removeEffect (int band, int slot)
{
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    std::unique_ptr<juce::AudioPluginInstance> old;
    std::unique_ptr<juce::DocumentWindow> window;
    bool wasPrepared = false;
    {
        const juce::ScopedLock lock (gate);
        auto* s = slotAt (band, slot);
        if (s == nullptr) return;
        window = std::move (s->window);
        wasPrepared = prepared;
        old = std::move (s->effect);
        s->description = {}; s->savedState.reset(); s->error.clear();
        s->pendingRequest = 0; s->enabled = true;
        s->dryDelay.reset();
        bands[(size_t) band]->relink();   // links to its knobs wait for an effect again
        alignLatency();
        setStatus ("Effect removed");
    }
    if (old != nullptr && wasPrepared) old->releaseResources();
    retire (std::move (window));
    retire (std::move (old));
    stateChanged();
}
void TriadProcessor::setLiveMode (bool on)
{
    if (liveMode.load() == on) return;
    {
        const juce::ScopedLock lock (gate);
        liveMode.store (on);
        alignLatency();
    }
    setStatus (on ? "LIVE: effects that add latency are passed by; Triad adds " + juce::String (getLatencySamples()) + " samples"
                  : "LIVE off: every effect plays again");
    stateChanged (false);   // a way of listening, not an edit: not an undo step
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
bool TriadProcessor::moveSlotTo (int band, int from, int to)
{
    {
        const juce::ScopedLock lock (gate);
        if (from == to || slotAt (band, from) == nullptr || slotAt (band, to) == nullptr) return false;
        auto& slots = bands[(size_t) band]->slots;
        auto moving = std::move (slots[(size_t) from]);   // the mix knob, LFO and links travel with the effect
        slots.erase (slots.begin() + from);
        slots.insert (slots.begin() + to, std::move (moving));
        for (auto& s : slots) s->dryDelay.reset();
        bands[(size_t) band]->relink();
        alignLatency();
        setStatus ("Moved " + (slots[(size_t) to]->description.name.isNotEmpty() ? slots[(size_t) to]->description.name : juce::String ("the slot"))
                   + " to slot " + juce::String (to + 1));
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
    EffectGraveyard::renew();   // after the window managers this window needed
}
bool TriadProcessor::isEffectWindowOpen (int band, int slot) const
{
    const auto* s = slotAt (band, slot);
    return s != nullptr && s->window != nullptr && s->window->isVisible();
}
bool TriadProcessor::showEffectIfClosed (int band, int slot)
{
    auto* s = slotAt (band, slot);
    if (s == nullptr || ! s->effect || isEffectWindowOpen (band, slot)) return false;
    showEffect (band, slot);
    return isEffectWindowOpen (band, slot);
}
void TriadProcessor::refreshWindowTitles()
{
    for (int b = 0; b <= postBand; ++b)
        for (int s = 0; s < (int) bands[(size_t) b]->slots.size(); ++s)
            if (auto& w = bands[(size_t) b]->slots[(size_t) s]->window)
                w->setName (windowTitle (b, s, bands[(size_t) b]->slots[(size_t) s]->description.name));
}
void TriadProcessor::closeWindows()
{
    for (auto& band : bands)
        for (auto& s : band->slots) retire (std::move (s->window));
}

// ---- LFOs ------------------------------------------------------------------------------------------------
int TriadProcessor::getLfoCount (int band) const
{
    return isValidBand (band) ? (int) bands[(size_t) band]->lfos.size() : 0;
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
            if (r->destination.kind == Destination::bandEdges && i.edge < 0) i.edge = r->destination.slotId;
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
    if (! isValidBand (band)) return -1;
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
        if (! isExtraChain (band) && (band < 0 || band >= numBands)) return -1;
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
void TriadProcessor::setTracedLfo (int band, int lfo)
{
    const int id = isValidBand (band) && lfo >= 0 && lfo < getLfoCount (band) ? bands[(size_t) band]->lfos[(size_t) lfo]->id : 0;
    const int b = id > 0 ? band : -1000;
    if (b == traceBand.load() && id == traceLfoId.load()) return;
    traceLfoId.store (id);
    traceBand.store (b);
    traceReset.store (true);
}
void TriadProcessor::traceLfo (Band& band, int bandIndex, const juce::AudioBuffer<float>& source, int start, int frames)
{
    if (traceBand.load (std::memory_order_relaxed) != bandIndex) return;
    const auto* l = band.findLfo (traceLfoId.load (std::memory_order_relaxed));
    if (l == nullptr) return;
    if (traceReset.exchange (false)) lfoTrace.reset();
    const float* left = source.getReadPointer (0, start);
    const float* right = source.getReadPointer (juce::jmin (1, source.getNumChannels() - 1), start);
    for (int i = 0; i < frames; ++i)
    {
        const double phase = l->phaseAt (i);
        if (phase < 0.0) continue;   // waiting for sound
        lfoTrace.add (phase, juce::jmax (std::abs (left[i]), std::abs (right[i])));
    }
    lfoTrace.endBlock();
}
TriadProcessor::TriggerSettings TriadProcessor::getTrigger (int band) const
{
    TriggerSettings s;
    if (! isValidBand (band)) return s;
    const auto& t = bands[(size_t) band]->trigger;
    s.audio = t.audio.load();
    s.listenFull = t.listenFull.load();
    s.afterPause = t.afterPause.load();
    s.thresholdDb = t.thresholdDb.load();
    return s;
}
void TriadProcessor::setTrigger (int band, const TriggerSettings& s)
{
    if (! isValidBand (band) || getTrigger (band) == s) return;
    auto& t = bands[(size_t) band]->trigger;
    t.listenFull.store (s.listenFull);
    t.afterPause.store (s.afterPause);
    t.thresholdDb.store (juce::jlimit (minTriggerDb, maxTriggerDb, s.thresholdDb));
    t.audio.store (s.audio);
    stateChanged();
}
TriadProcessor::TriggerStatus TriadProcessor::getTriggerStatus (int band) const
{
    TriggerStatus s;
    if (! isValidBand (band)) return s;
    auto& t = bands[(size_t) band]->trigger;
    s.waiting = ! t.audio.load() || t.idle.load();
    s.levelDb = juce::Decibels::gainToDecibels (t.level.exchange (0.0f), -100.0f);
    s.count = t.count.load();
    return s;
}
float TriadProcessor::getLfoPhase (int band, int index) const
{
    auto* l = lfoAt (band, index);
    return l != nullptr && prepared ? l->displayPhase.load() : -1.0f;
}
float TriadProcessor::getLfoPhaseNow (int band, int index) const
{
    auto* l = lfoAt (band, index);
    return l != nullptr && prepared && l->displayPhase.load() >= 0.0f ? l->clock.now() : -1.0f;
}

// ---- Matrix ------------------------------------------------------------------------------------------------
juce::String TriadProcessor::destinationName (const Band& b, const Destination& d) const
{
    if (d.kind == Destination::bandMix) return "Band Mix";
    if (d.kind == Destination::bandVolume) return "Band Volume";
    if (d.kind == Destination::bandEdges)
    {
        // "High edge 2.5 kHz", "Edges: move the band"
        int index = -1;
        for (int k = 0; k < getBandCount(); ++k) if (bands[(size_t) k].get() == &b) index = k;
        const auto cuts = effectiveCrossovers();
        if (d.slotId == Destination::lowEdge)
            return index > 0 && index - 1 < (int) cuts.size() ? "Low cut " + triad::ui::formatHz (cuts[(size_t) index - 1]) : juce::String ("Low cut (none)");
        if (d.slotId == Destination::highEdge)
            return index >= 0 && index < (int) cuts.size() ? "High cut " + triad::ui::formatHz (cuts[(size_t) index]) : juce::String ("High cut (none)");
        if (d.slotId == Destination::moveEdges && index >= 0)
        {
            const float lo = index > 0 ? cuts[(size_t) index - 1] : 20.0f, hi = index < (int) cuts.size() ? cuts[(size_t) index] : 20000.0f;
            return "Sweep " + triad::ui::formatHz (lo) + juce::String::fromUTF8 ("\xe2\x80\x93") + triad::ui::formatHz (hi);
        }
        return d.slotId == Destination::moveEdges ? juce::String ("Sweep the band") : juce::String ("Widen / narrow");
    }
    const int index = b.slotIndex (d.slotId);
    if (index < 0) return "(removed slot)";
    if (d.kind == Destination::effectParameter)
    {
        // "2 · Infiltrator: Mix"
        const auto& s = *b.slots[(size_t) index];
        const auto effect = s.description.name.isNotEmpty() ? s.description.name : "Slot " + juce::String (index + 1);
        juce::String knob = d.parameterId.isNotEmpty() ? d.parameterId : "knob " + juce::String (d.parameter + 1);
        if (s.effect != nullptr && d.parameter >= 0 && d.parameter < s.effect->getParameters().size())
            knob = s.effect->getParameters()[d.parameter]->getName (40);
        return juce::String (index + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + effect + ": " + knob;
    }
    // "2 · KHS Distortion Mix": the slot number tells two copies of one effect apart.
    const auto& name = b.slots[(size_t) index]->description.name;
    return name.isNotEmpty() ? juce::String (index + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") + name + " Mix"
                             : "Slot " + juce::String (index + 1) + " Mix";
}
int TriadProcessor::getRouteCount (int band) const
{
    return isValidBand (band) ? (int) bands[(size_t) band]->routes.size() : 0;
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
    i.locked = r->locked.load();
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
        if (b.extra && (destination.kind == Destination::bandMix || destination.kind == Destination::bandVolume || destination.kind == Destination::bandEdges)) return -1;
        auto r = std::make_unique<Route>();
        if (destination.kind == Destination::effectParameter)
        {
            // A knob inside an effect: the LFO moves it from where it is now.
            auto* s = b.findSlot (destination.slotId);
            auto* parameter = s != nullptr ? Band::resolveParameter (*s, destination) : nullptr;
            if (parameter == nullptr) return -1;
            r->base.store (parameter->getValue());
        }
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
        if (b.extra && (destination.kind == Destination::bandMix || destination.kind == Destination::bandVolume || destination.kind == Destination::bandEdges)) return;
        if (destination.kind == Destination::effectParameter)
        {
            auto* s = b.findSlot (destination.slotId);
            auto* parameter = s != nullptr ? Band::resolveParameter (*s, destination) : nullptr;
            if (parameter == nullptr) return;
            if (r->parameter != nullptr) r->parameter->setValue (r->base.load());
            r->base.store (parameter->getValue());
        }
        else if (r->parameter != nullptr) r->parameter->setValue (r->base.load());
        r->destination = destination;
        if (! canLock (destination)) r->locked.store (false);
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
void TriadProcessor::setRouteLocked (int band, int route, bool on)
{
    auto* r = routeAt (band, route);
    if (r == nullptr || ! canLock (r->destination) || r->locked.load() == on) return;
    // The line means another range now: its points are moved so the edges stay where they are (wherever that is inside
    // the band; outside it they go to the band's edge).
    const int lfo = bands[(size_t) band]->lfoIndex (r->lfoId);
    if (auto* l = lfoAt (band, lfo))
    {
        const auto cuts = effectiveCrossovers();
        const int count = getBandCount();
        const float uL = band > 0 && band - 1 < (int) cuts.size() ? edgeU (cuts[(size_t) band - 1]) : 0.0f;
        const float uH = band < count - 1 && band < (int) cuts.size() ? edgeU (cuts[(size_t) band]) : 1.0f;
        const float w = juce::jmax (0.001f, uH - uL), c = 0.5f * (uL + uH), reach = juce::jmax (0.001f, juce::jmax (c, 1.0f - c));
        const bool open = r->destination.slotId == Destination::openEdges, inverted = r->invert.load();
        const auto convert = [&] (float v)
        {
            if (inverted) v = 1.0f - v;
            float out = open ? (on ? v * reach / (0.5f * w) : v * 0.5f * w / reach)
                             : (on ? (v - uL) / w : uL + v * w);
            out = juce::jlimit (0.0f, 1.0f, out);
            return inverted ? 1.0f - out : out;
        };
        auto shape = l->shape;
        for (auto& point : shape) point.y = convert (point.y);
        setLfoShape (band, lfo, shape, false);
    }
    r->locked.store (on);
    stateChanged();
}
int TriadProcessor::getBandEdgeLock (int band) const
{
    if (band < 0 || band >= getBandCount()) return -1;
    int lockable = 0, locked = 0;
    for (auto& r : bands[(size_t) band]->routes)
        if (canLock (r->destination)) { ++lockable; locked += r->locked.load() ? 1 : 0; }
    return lockable == 0 ? -1 : locked == lockable ? 1 : 0;
}
void TriadProcessor::setBandEdgesLocked (int band, bool on)
{
    if (band < 0 || band >= getBandCount()) return;
    for (int r = 0; r < (int) bands[(size_t) band]->routes.size(); ++r)
        if (canLock (bands[(size_t) band]->routes[(size_t) r]->destination)) setRouteLocked (band, r, on);
}
bool TriadProcessor::isEdgeLfoLocked (int band, int lfo) const
{
    if (band < 0 || band >= getBandCount()) return false;
    const auto& b = *bands[(size_t) band];
    for (auto& r : b.routes)
        if (canLock (r->destination) && b.lfoIndex (r->lfoId) == lfo && r->locked.load()) return true;
    return false;
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
        auto* r = routeAt (band, route);
        if (r == nullptr) return;
        if (r->parameter != nullptr) r->parameter->setValue (r->base.load());   // the knob goes back to where it was
        auto& b = *bands[(size_t) band];
        b.routes.erase (b.routes.begin() + route);
        b.relink();
    }
    stateChanged();
}
std::vector<std::pair<TriadProcessor::Destination, juce::String>> TriadProcessor::getDestinations (int band) const
{
    std::vector<std::pair<Destination, juce::String>> list;
    if (! isValidBand (band)) return list;
    const auto& b = *bands[(size_t) band];
    if (! b.extra)
    {
        list.push_back ({ Destination { Destination::bandMix, 0 }, "Band Mix" });
        list.push_back ({ Destination { Destination::bandVolume, 0 }, "Band Volume" });
        // The band's edges: the dividers below and above it (the lowest band has no low edge, the highest no high one).
        if (getBandCount() > 1)
        {
            int index = -1;
            for (int k = 0; k < getBandCount(); ++k) if (bands[(size_t) k].get() == &b) index = k;
            for (int e : { (int) Destination::moveEdges, (int) Destination::openEdges, (int) Destination::lowEdge, (int) Destination::highEdge })
            {
                if ((e == Destination::lowEdge && index <= 0) || (e == Destination::highEdge && index >= getBandCount() - 1)) continue;
                list.push_back ({ Destination { Destination::bandEdges, e }, destinationName (b, Destination { Destination::bandEdges, e }) });
            }
        }
    }
    for (auto& s : b.slots) list.push_back ({ Destination { Destination::slotMix, s->id }, destinationName (b, Destination { Destination::slotMix, s->id }) });
    return list;
}

std::vector<std::pair<TriadProcessor::Destination, juce::String>> TriadProcessor::getEffectParameters (int band, int slot) const
{
    std::vector<std::pair<Destination, juce::String>> list;
    auto* s = slotAt (band, slot);
    if (s == nullptr || s->effect == nullptr) return list;
    const auto& knobs = s->effect->getParameters();
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto* p = knobs[i];
        const auto name = p->getName (60);
        if (! p->isAutomatable() || p->isMetaParameter() || name.isEmpty()) continue;
        Destination d { Destination::effectParameter, s->id, i, {} };
        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (p)) d.parameterId = hosted->getParameterID();
        list.push_back ({ d, name });
    }
    return list;
}

// ---- LEARN: link the next knob moved in an effect's window ------------------------------------------------
struct TriadProcessor::Learn
{
    // One listener per effect; the parameters call it on whatever thread moves them.
    struct Watch final : juce::AudioProcessorParameter::Listener
    {
        Learn& owner; int slotId; juce::AudioPluginInstance* effect;
        Watch (Learn& l, int id, juce::AudioPluginInstance* e) : owner (l), slotId (id), effect (e) {}
        void parameterValueChanged (int index, float) override { owner.moved (slotId, index, false); }
        void parameterGestureChanged (int index, bool starting) override { if (starting) owner.moved (slotId, index, true); }
    };
    int band = 0, lfoId = 0;
    std::set<std::pair<int, int>> skip;          // knobs Triad itself moves (their changes are not the user's)
    std::vector<std::unique_ptr<Watch>> watches;
    juce::SpinLock lock;
    std::map<std::pair<int, int>, int> changes;  // value changes seen per knob
    std::atomic<int> learnedSlot { 0 }, learnedIndex { -1 };
    double started = 0.0;
    void moved (int slotId, int index, bool gesture)
    {
        if (learnedIndex.load() >= 0 || skip.count ({ slotId, index }) > 0) return;
        // A gesture (the mouse on a knob) counts at once; plain value changes after a few in a row, so a
        // value an effect updates by itself once (on loading, say) is not taken for the user's knob.
        const juce::SpinLock::ScopedLockType hold (lock);
        if (! gesture && ++changes[{ slotId, index }] < 3) return;
        learnedSlot.store (slotId);
        learnedIndex.store (index);
    }
};
bool TriadProcessor::beginParameterLearn (int band, int lfo)
{
    cancelParameterLearn();
    auto* l = lfoAt (band, lfo);
    if (l == nullptr) return false;
    auto next = std::make_unique<Learn>();
    next->band = band;
    next->lfoId = l->id;
    next->started = juce::Time::getMillisecondCounterHiRes();
    auto& b = *bands[(size_t) band];
    for (auto& r : b.routes)
        if (r->destination.kind == Destination::effectParameter) next->skip.insert ({ r->destination.slotId, r->destination.parameter });
    for (auto& s : b.slots)
        if (s->effect != nullptr)
        {
            next->watches.push_back (std::make_unique<Learn::Watch> (*next, s->id, s->effect.get()));
            for (auto* p : s->effect->getParameters()) p->addListener (next->watches.back().get());
        }
    if (next->watches.empty()) return false;
    learn = std::move (next);
    setStatus ("LEARN: move a knob in one of " + chainName (band) + "'s effect windows (it is linked to LFO "
               + juce::String (lfo + 1) + ")");
    return true;
}
void TriadProcessor::endParameterLearn()
{
    if (learn == nullptr) return;
    // Stop listening to every watched effect (structure changes end LEARN first, so they are all still here).
    for (auto& w : learn->watches)
        for (auto& band : bands)
            if (band != nullptr)
                for (auto& s : band->slots)
                    if (s->effect.get() == w->effect)
                        for (auto* p : w->effect->getParameters()) p->removeListener (w.get());
    learn.reset();
}
void TriadProcessor::cancelParameterLearn()
{
    if (learn == nullptr) return;
    endParameterLearn();
    setStatus ("LEARN cancelled");
}
int TriadProcessor::pollParameterLearn()
{
    if (learn == nullptr) return -1;
    const int index = learn->learnedIndex.load();
    if (index < 0)
    {
        if (juce::Time::getMillisecondCounterHiRes() - learn->started > 60000.0) { endParameterLearn(); setStatus ("LEARN stopped: no knob moved"); }
        return -1;
    }
    const int band = learn->band, slotId = learn->learnedSlot.load(), lfoId = learn->lfoId;
    endParameterLearn();
    if (! isValidBand (band)) return -1;
    auto& b = *bands[(size_t) band];
    const int lfo = b.lfoIndex (lfoId);
    if (lfo < 0 || b.findSlot (slotId) == nullptr) return -1;
    Destination d { Destination::effectParameter, slotId, index, {} };
    const int route = addRoute (band, lfo, d);
    if (route >= 0) setStatus ("Linked LFO " + juce::String (lfo + 1) + " to " + getRouteInfo (band, route).destinationName);
    return route;
}

// ---- State ----------------------------------------------------------------------------------------------
// Version 4: <TriadState version="4" bands="n" preset=""> <Parameters/>
//   <Band index slots nextSlot nextLfo> <Slot index id enabled mix> <PLUGIN/> <State/> </Slot>
//     <Lfo id slot band length touched> <Shape> <P x y c/> </Shape> </Lfo>
//     <Route lfo kind slot amount invert muted auto/> </Band> </TriadState>
// Versions 1 (Triad 0.1), 2 (0.2.0/0.2.1) and 3 (0.2.2) still load.
void TriadProcessor::writeBand (juce::XmlElement& node, const Band& band, bool forHistory) const
{
    if (forHistory) node.setAttribute ("serial", band.serial);
    node.setAttribute ("slots", (int) band.slots.size());
    node.setAttribute ("nextSlot", band.nextSlotId);
    node.setAttribute ("nextLfo", band.nextLfoId);
    // TRIGGER (0.14.0): only when it is not the default (SONG), so older Triads read the band as before.
    const auto& t = band.trigger;
    if (t.audio.load() || t.listenFull.load() || t.afterPause.load() || ! juce::exactlyEqual (t.thresholdDb.load(), -24.0f))
    {
        node.setAttribute ("trigger", t.audio.load() ? "audio" : "song");
        node.setAttribute ("listen", t.listenFull.load() ? "full" : "band");
        node.setAttribute ("restart", t.afterPause.load() ? "pause" : "hit");
        node.setAttribute ("threshold", (double) t.thresholdDb.load());
    }
    for (int i = 0; i < (int) band.slots.size(); ++i)
    {
        auto& s = *band.slots[(size_t) i];
        auto* x = node.createNewChildElement ("Slot");
        x->setAttribute ("index", i);
        x->setAttribute ("id", s.id);
        if (forHistory) x->setAttribute ("serial", s.serial);
        x->setAttribute ("enabled", s.enabled);
        x->setAttribute ("mix", (double) s.mixPercent.load());
        if (s.description.name.isEmpty()) continue;
        x->addChildElement (s.description.createXml().release());
        if (s.effect) s.effect->getStateInformation (s.savedState);
        x->createNewChildElement ("State")->addTextElement (s.savedState.toBase64Encoding());
        if (forHistory && s.effect != nullptr)
        {
            // Undo also keeps where each knob is (in millionths; "x" for knobs an LFO moves, or that cannot be
            // automated): a few effects do not give every knob back through their saved settings.
            std::set<int> linked;
            for (auto& r : band.routes)
                if (r->destination.kind == Destination::effectParameter && r->destination.slotId == s.id) linked.insert (r->destination.parameter);
            const auto& knobs = s.effect->getParameters();
            juce::String values;
            values.preallocateBytes ((size_t) knobs.size() * 8);
            for (int k = 0; k < knobs.size(); ++k)
            {
                if (k > 0) values << ' ';
                auto* knob = knobs[k];
                if (linked.count (k) > 0 || ! knob->isAutomatable() || knob->isMetaParameter()) values << 'x';
                else values << juce::roundToInt (juce::jlimit (0.0f, 1.0f, knob->getValue()) * 1.0e6f);
            }
            x->createNewChildElement ("Knobs")->addTextElement (values);
        }
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
        if (r->destination.kind == Destination::effectParameter)
        {
            x->setAttribute ("param", r->destination.parameterId);
            x->setAttribute ("paramIndex", r->destination.parameter);
            x->setAttribute ("base", (double) r->base.load());
        }
        x->setAttribute ("amount", (double) r->amount.load());
        x->setAttribute ("invert", r->invert.load());
        if (r->locked.load()) x->setAttribute ("locked", true);
        x->setAttribute ("muted", r->muted.load());
        x->setAttribute ("auto", r->automatic);
    }
}
std::unique_ptr<TriadProcessor::Band> TriadProcessor::readBand (const juce::XmlElement* node, int version,
                                                                const std::function<float (int)>& legacyMix,
                                                                std::vector<PendingLoad>& loads, bool extra) const
{
    auto band = std::make_unique<Band> (false, extra);
    if (node != nullptr && node->hasAttribute ("serial")) band->serial = node->getIntAttribute ("serial");
    if (node != nullptr && node->hasAttribute ("trigger"))
    {
        band->trigger.audio.store (node->getStringAttribute ("trigger") == "audio");
        band->trigger.listenFull.store (node->getStringAttribute ("listen") == "full");
        band->trigger.afterPause.store (node->getStringAttribute ("restart") == "pause");
        band->trigger.thresholdDb.store (juce::jlimit (minTriggerDb, maxTriggerDb, (float) node->getDoubleAttribute ("threshold", -24.0)));
    }
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
            if (s->hasAttribute ("serial")) slot.serial = s->getIntAttribute ("serial");   // an undo snapshot
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
            r->destination.kind = juce::jlimit (0, (int) Destination::bandEdges, x->getIntAttribute ("kind", Destination::bandMix));
            r->destination.slotId = x->getIntAttribute ("slot");
            r->destination.parameterId = x->getStringAttribute ("param");
            r->destination.parameter = x->getIntAttribute ("paramIndex", -1);
            r->base.store (juce::jlimit (0.0f, 1.0f, (float) x->getDoubleAttribute ("base", 0.5)));
            r->amount.store (juce::jlimit (0.0f, 1.0f, (float) x->getDoubleAttribute ("amount", 1.0)));
            r->invert.store (x->getBoolAttribute ("invert"));
            r->locked.store (canLock (r->destination) && x->getBoolAttribute ("locked"));
            r->muted.store (x->getBoolAttribute ("muted", true));
            r->automatic = x->getBoolAttribute ("auto");
            band->routes.push_back (std::move (r));
        }
    }
    band->ensureModulation();
    band->relink();
    return band;
}
std::unique_ptr<juce::XmlElement> TriadProcessor::createPresetXml (bool forHistory)
{
    // No audio lock: the bands only change on the message thread, and this runs on it (or holding the
    // MessageManagerLock, from getStateInformation), so the audio keeps playing while a DAW saves or
    // autosaves the project; hosts expect effects to save their state while they play.
    JUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKED
    auto root = std::make_unique<juce::XmlElement> ("TriadState");
    root->setAttribute ("version", 4);
    root->setAttribute ("bands", numBands);
    root->setAttribute ("preset", getPresetName());
    if (liveMode.load() && ! forHistory) root->setAttribute ("live", true);
    root->addChildElement (parameters.copyState().createXml().release());
    for (int b = 0; b < numBands; ++b)
    {
        auto* bandNode = root->createNewChildElement ("Band");
        bandNode->setAttribute ("index", b);
        writeBand (*bandNode, *bands[(size_t) b], forHistory);
    }
    writeBand (*root->createNewChildElement ("Pre"), *bands[(size_t) preBand], forHistory);    // the PRE chain (0.7.0)
    writeBand (*root->createNewChildElement ("Post"), *bands[(size_t) postBand], forHistory);  // the POST chain (0.7.0)
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
    stateReceived.store (true);
    // A project or preset opened by the DAW (or the startup preset) starts a fresh undo history; a preset
    // picked in Triad is one undo step.
    if (! keepHistory.exchange (false)) historyReset.store (true);
    {
        const juce::ScopedLock lock (stateGate);
        pendingState = std::move (xml); restoring.store (true);
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}
void TriadProcessor::loadPresetXml (const juce::XmlElement& xml, bool undoable)
{
    if (undoable && juce::MessageManager::existsAndIsCurrentThread())
    {
        if (historyDirty) commitHistory();   // keep the state before the preset as its own step
        keepHistory.store (true);
    }
    juce::MemoryBlock data;
    copyXmlToBinary (xml, data);
    setStateInformation (data.getData(), (int) data.getSize());
}
std::unique_ptr<juce::XmlElement> TriadProcessor::createInitXml()
{
    TriadProcessor fresh;
    fresh.startupDecided = true;   // a template: never opens a startup preset itself
    fresh.setPresetName ("Init");
    return fresh.createPresetXml();
}
void TriadProcessor::initialize()
{
    loadPresetXml (*createInitXml());
    setStatus ("Initialized: an empty Triad");
}
bool TriadProcessor::applyStartupPreset()
{
    juce::SharedResourcePointer<TriadSettings> settings;
    const auto choice = settings->getStartupPreset();
    const auto file = TriadSettings::startupPresetFile (choice);
    if (file == juce::File()) return false;   // Init: as constructed
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("TriadState"))
    {
        setStatus ("The startup preset \"" + (choice == TriadSettings::currentSettings ? juce::String ("current settings") : choice)
                   + "\" could not be opened; Triad started empty");
        return false;
    }
    if (choice != TriadSettings::currentSettings) xml->setAttribute ("preset", choice);
    loadPresetXml (*xml, false);
    return true;
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
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    const int version = xml.getIntAttribute ("version");
    const int count = version >= 2 ? juce::jlimit (1, triad::maxBands, xml.getIntAttribute ("bands", 3)) : 3;
    std::array<std::unique_ptr<Band>, triad::maxBands + 2> old;   // the bands, PRE and POST
    bool wasPrepared = false;
    {
        const juce::ScopedLock lock (gate);
        for (int b = 0; b <= postBand; ++b)
        {
            old[(size_t) b] = std::move (bands[(size_t) b]);
            bands[(size_t) b] = std::make_unique<Band> (true, isExtraChain (b));
        }
        wasPrepared = prepared;
        publishInfo();
    }
    for (auto& band : old)
        for (auto& s : band->slots)
        {
            if (s->effect && wasPrepared) s->effect->releaseResources();   // no longer played: no lock needed
            retire (std::move (s->window)); retire (std::move (s->effect));
        }
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
        // Parameters a project from an earlier version did not have (crossover slopes before 0.4.0)
        // take their defaults, so the project sounds as it did.
        for (auto* parameter : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (! tree.getChildWithProperty ("id", ranged->getParameterID()).isValid())
                {
                    juce::ValueTree child ("PARAM");
                    child.setProperty ("id", ranged->getParameterID(), nullptr);
                    child.setProperty ("value", ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
                    tree.appendChild (child, nullptr);
                }
        parameters.replaceState (tree);
    }
    std::array<std::unique_ptr<Band>, triad::maxBands + 2> fresh;
    std::array<std::vector<PendingLoad>, triad::maxBands + 2> loads;
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
        // The PRE and POST chains (saved since 0.7.0).
        if (auto* preNode = xml.getChildByName ("Pre"))
            fresh[(size_t) preBand] = readBand (preNode, 4, [] (int) { return 100.0f; }, loads[(size_t) preBand], true);
        if (auto* postNode = xml.getChildByName ("Post"))
            fresh[(size_t) postBand] = readBand (postNode, 4, [] (int) { return 100.0f; }, loads[(size_t) postBand], true);
    }
    for (int b = 0; b <= postBand; ++b)
        if (fresh[(size_t) b] == nullptr) fresh[(size_t) b] = std::make_unique<Band> (true, isExtraChain (b));
    setPresetName (xml.getStringAttribute ("preset"));
    liveMode.store (xml.getBoolAttribute ("live", false));
    {
        const juce::ScopedLock lock (gate);
        numBands = count;
        bandCount.store (count);
        for (int b = 0; b <= postBand; ++b) bands[(size_t) b] = std::move (fresh[(size_t) b]);
        std::array<float, triad::maxCrossovers> cuts {};
        std::array<int, triad::maxCrossovers> slopes {};
        readCrossovers (cuts.data());
        readSlopes (slopes.data());
        splitter.setBandCount (numBands, cuts.data(), slopes.data());
        for (int b = 0; b <= postBand; ++b) prepareBand (b, false);
        alignLatency();
    }
    ++structureVersion;
    for (int b = 0; b <= postBand; ++b)
        if (b < count || isExtraChain (b))
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
    if (historyReset.exchange (false)) clearHistory();
    restoreFrom (*xml);
    noteChange();
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
    endParameterLearn();
    if (! xml.hasTagName ("TriadChain") || xml.getChildByName ("Band") == nullptr) return "This is not a Triad chain preset.";
    if (band < 0 || band >= getBandCount()) return "That band does not exist.";
    std::vector<PendingLoad> loads;
    auto fresh = readBand (xml.getChildByName ("Band"), 4, [] (int) { return 100.0f; }, loads);
    std::unique_ptr<Band> old;
    bool wasPrepared = false;
    {
        const juce::ScopedLock lock (gate);
        old = std::move (bands[(size_t) band]);
        bands[(size_t) band] = std::move (fresh);
        wasPrepared = prepared;
        prepareBand (band, false);
        alignLatency();
    }
    for (auto& s : old->slots)
    {
        if (s->effect && wasPrepared) s->effect->releaseResources();
        retire (std::move (s->window)); retire (std::move (s->effect));
    }
    old.reset();
    setParameterValue (bandMixID (band), (float) xml.getDoubleAttribute ("bandMix", 100.0));
    setParameterValue (bandGainID (band), (float) xml.getDoubleAttribute ("bandGain", 0.0));
    ++structureVersion;
    for (auto& load : loads) installFromXml (band, load.slot, *load.node, false);
    refreshWindowTitles();
    stateChanged();
    return {};
}

// ---- Band clipboard ---------------------------------------------------------------------------------------
bool TriadProcessor::copyBandState (int band)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto xml = createChainXml (band);
    if (xml == nullptr) return false;
    juce::StringArray names;
    for (int s = 0; s < getSlotCount (band); ++s)
        if (const auto slotInfo = getSlotInfo (band, s); slotInfo.name.isNotEmpty() && slotInfo.hasContent) names.add (slotInfo.name);
    auto& clipboard = bandClipboard();
    clipboard.xml = std::move (xml);
    clipboard.description = "band " + juce::String (band + 1) + ": " + (names.isEmpty() ? juce::String ("no effects") : names.joinIntoString (", "));
    setStatus ("Copied band " + juce::String (band + 1) + " state");
    return true;
}
juce::String TriadProcessor::pasteBandState (int band)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto& clipboard = bandClipboard();
    if (clipboard.xml == nullptr) return "No band state copied yet.";
    const auto error = loadChainXml (band, *clipboard.xml);
    setStatus (error.isEmpty() ? "Pasted " + clipboard.description + " into band " + juce::String (band + 1) : error);
    return error;
}
bool TriadProcessor::hasCopiedBand() { return bandClipboard().xml != nullptr; }
juce::String TriadProcessor::copiedBandDescription() { return bandClipboard().description; }
void TriadProcessor::clearCopiedBand() { bandClipboard() = {}; }

// ---- Plug-in clipboard ------------------------------------------------------------------------------------
bool TriadProcessor::copyEffectState (int band, int slot)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto* s = slotAt (band, slot);
    if (s == nullptr || s->effect == nullptr || s->pendingRequest != 0) return false;
    auto& clipboard = effectClipboard();
    clipboard.description = s->description;
    clipboard.state.reset();
    s->effect->getStateInformation (clipboard.state);
    clipboard.mix = s->mixPercent.load();
    clipboard.valid = true;
    setStatus ("Copied " + s->description.name + " with all its settings: right-click another slot and paste it");
    return true;
}
juce::String TriadProcessor::pasteEffectState (int band, int slot)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto clipboard = effectClipboard();   // a copy: the clipboard may change while this loads
    if (! clipboard.valid) return "No plug-in state copied yet.";
    if (! isValidBand (band)) return "That band does not exist.";
    const bool append = slot == getSlotCount (band);
    if (append) slot = addSlot (band);
    auto* s = slotAt (band, slot);
    if (s == nullptr) return "That slot does not exist.";
    juce::String error;
    if (s->effect != nullptr && s->pendingRequest == 0
        && s->description.createIdentifierString() == clipboard.description.createIdentifierString())
    {
        // The same effect: only its settings change (it stays loaded, its window stays open).
        if (clipboard.state.getSize() > 0) s->effect->setStateInformation (clipboard.state.getData(), (int) clipboard.state.getSize());
        stateChanged();
    }
    else error = requestInstall (band, slot, clipboard.description, clipboard.state, false, false);
    if (error.isNotEmpty())
    {
        if (append) removeSlot (band, slot);
        setStatus (error);
        return error;
    }
    setSlotMix (band, slot, clipboard.mix);
    setStatus ("Pasted " + clipboard.description.name + " (with its settings) into " + chainName (band)
               + ", slot " + juce::String (slot + 1));
    return {};
}
bool TriadProcessor::hasCopiedEffect() { return effectClipboard().valid; }
juce::String TriadProcessor::copiedEffectName() { return effectClipboard().valid ? effectClipboard().description.name : juce::String(); }
void TriadProcessor::clearCopiedEffect() { effectClipboard() = {}; }

// ---- Undo / redo ------------------------------------------------------------------------------------------
// The history is a list of snapshots of the whole Triad (createPresetXml with each band's and slot's serial),
// taken a moment after each change once nothing is moving: no mouse button down, no knob gesture open, no
// effect still loading. `historyCursor` is the snapshot showing now; undo applies the one before it.
namespace
{
// The chains in a snapshot: its bands and the PRE chain.
std::vector<const juce::XmlElement*> chainsIn (const juce::XmlElement& xml)
{
    std::vector<const juce::XmlElement*> chains;
    for (auto* child : xml.getChildIterator())
        if (child->hasTagName ("Band") || child->hasTagName ("Pre") || child->hasTagName ("Post")) chains.push_back (child);
    return chains;
}
size_t snapshotSize (const juce::XmlElement& xml)
{
    size_t bytes = 4096;
    for (auto* band : chainsIn (xml))
        for (auto* slot : band->getChildWithTagNameIterator ("Slot"))
            if (auto* state = slot->getChildByName ("State")) bytes += (size_t) state->getAllSubText().length() + 512;
    return bytes;
}
constexpr size_t maxHistoryBytes = (size_t) 192 * 1024 * 1024;
}
void TriadProcessor::noteChange()
{
    if (! juce::MessageManager::existsAndIsCurrentThread()) { ++historySignal->ended; return; }
    if (historyApplying || historyAwaitingLoads) return;
    historyDirty = true;
    historyRefresh = false;
    lastChange = juce::Time::getMillisecondCounterHiRes();
}
bool TriadProcessor::historyBusy() const
{
    if (restoring.load() || pendingCreations.load() > 0 || historySignal->open.load() > 0) return true;
    if (juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()) return true;
    for (int k = 0; k < numBands + 2; ++k)
        for (auto& s : bands[(size_t) chainAt (k)]->slots) if (s->pendingRequest != 0) return true;
    return false;
}
void TriadProcessor::historyTick()
{
    if (historySignal->ended.exchange (0) > 0) noteChange();
    // An effect that began a gesture and never ended it (some do not send the end): forgotten after 5 s.
    if (historySignal->open.load() > 0 && nowMs() - historySignal->lastGesture.load() > 5000) historySignal->open.store (0);
    if (! startupDecided) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (historyAwaitingLoads)
    {
        if (historyBusy()) return;
        historyAwaitingLoads = false;   // the effects an undo had to load are in: its snapshot is refreshed below
        historyRefresh = true;
        lastChange = now;
    }
    if (history.empty())
    {
        if (! historyBusy()) commitHistory();   // the starting point
        return;
    }
    if (now - lastChange < historyQuietMs || historyBusy()) return;
    if (historyDirty) { commitHistory(); return; }
    if (historyRefresh && historyCursor >= 0)
    {
        // After an undo or redo: keep the snapshot as the effects now report it (some write their settings a
        // little differently from how they were given), so the next change is compared with what is there.
        historyRefresh = false;
        auto snapshot = createPresetXml (true);
        historySizes[(size_t) historyCursor] = snapshotSize (*snapshot);
        history[(size_t) historyCursor] = std::move (snapshot);
    }
}
void TriadProcessor::commitHistory()
{
    JUCE_ASSERT_MESSAGE_THREAD
    historyDirty = false;
    historyRefresh = false;
    auto snapshot = createPresetXml (true);
    if (historyCursor >= 0 && history[(size_t) historyCursor]->isEquivalentTo (snapshot.get(), false)) return;   // nothing changed
    // Effects whose settings did not change share them with the previous snapshot (juce::String is shared, not
    // copied), so a step costs memory only for what changed.
    size_t size = snapshotSize (*snapshot);
    if (historyCursor >= 0)
    {
        for (const auto* tag : { "State", "Knobs" })
        {
            std::map<int, juce::String> before;
            for (auto* band : chainsIn (*history[(size_t) historyCursor]))
                for (auto* slot : band->getChildWithTagNameIterator ("Slot"))
                    if (auto* text = slot->getChildByName (tag); text != nullptr && text->getFirstChildElement() != nullptr)
                        before[slot->getIntAttribute ("serial")] = text->getFirstChildElement()->getText();
            for (auto* band : chainsIn (*snapshot))
                for (auto* slot : band->getChildWithTagNameIterator ("Slot"))
                    if (auto* text = slot->getChildByName (tag); text != nullptr && text->getFirstChildElement() != nullptr)
                        if (const auto it = before.find (slot->getIntAttribute ("serial")); it != before.end() && it->second == text->getFirstChildElement()->getText())
                        {
                            if (juce::String (tag) == "State") size -= juce::jmin (size, (size_t) it->second.length());
                            text->getFirstChildElement()->setText (it->second);
                        }
        }
    }
    history.resize ((size_t) (historyCursor + 1));   // a new change: the steps that were undone are gone
    historySizes.resize (history.size());
    historySizes.push_back (size);
    history.push_back (std::move (snapshot));
    size_t total = 0;
    for (auto bytes : historySizes) total += bytes;
    while (history.size() > 1 && ((int) history.size() > maxHistorySteps + 1 || total > maxHistoryBytes))
    {
        total -= historySizes.front();
        history.erase (history.begin());
        historySizes.erase (historySizes.begin());
    }
    historyCursor = (int) history.size() - 1;
}
void TriadProcessor::clearHistory()
{
    history.clear();
    historySizes.clear();
    historyCursor = -1;
    historyDirty = historyRefresh = historyAwaitingLoads = false;
}
bool TriadProcessor::canUndo() const { return historyCursor > 0 || (historyDirty && historyCursor >= 0); }
bool TriadProcessor::canRedo() const { return ! historyDirty && historyCursor >= 0 && historyCursor < (int) history.size() - 1; }
int TriadProcessor::getUndoSteps() const { return juce::jmax (0, historyCursor) + (historyDirty && historyCursor >= 0 ? 1 : 0); }
int TriadProcessor::getRedoSteps() const { return canRedo() ? (int) history.size() - 1 - historyCursor : 0; }
bool TriadProcessor::undo()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (historyDirty || history.empty()) commitHistory();   // the change not kept yet is the one undone
    if (historyCursor <= 0) { setStatus ("Nothing to undo"); return false; }
    --historyCursor;
    applyHistory (*history[(size_t) historyCursor]);
    setStatus ("Undo" + (historyCursor > 0 ? " (" + juce::String (historyCursor) + " more step" + (historyCursor == 1 ? "" : "s") + " back)"
                                           : juce::String (" (back to the start)")));
    return true;
}
bool TriadProcessor::redo()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (historyDirty) commitHistory();   // a change since the undo: there is nothing to redo any more
    if (! canRedo()) { setStatus ("Nothing to redo"); return false; }
    ++historyCursor;
    applyHistory (*history[(size_t) historyCursor]);
    const int left = (int) history.size() - 1 - historyCursor;
    setStatus ("Redo" + (left > 0 ? " (" + juce::String (left) + " more step" + (left == 1 ? "" : "s") + " forward)" : juce::String()));
    return true;
}
void TriadProcessor::applyHistory (const juce::XmlElement& xml)
{
    JUCE_ASSERT_MESSAGE_THREAD
    endParameterLearn();   // LEARN listens to the effects: stop before any of them moves or goes
    const juce::ScopedValueSetter<bool> applying (historyApplying, true);
    const int count = juce::jlimit (1, triad::maxBands, xml.getIntAttribute ("bands", 3));
    // Crossovers, slopes, band mixes and volumes.
    if (auto* p = xml.getChildByName ("Parameters")) parameters.replaceState (juce::ValueTree::fromXml (*p));
    std::array<std::unique_ptr<Band>, triad::maxBands + 2> fresh, old;   // the bands, then PRE and POST
    std::array<std::vector<PendingLoad>, triad::maxBands + 2> loads;
    for (int b = 0; b < count; ++b)
    {
        const juce::XmlElement* node = nullptr;
        for (auto* n : xml.getChildWithTagNameIterator ("Band"))
            if (n->getIntAttribute ("index", -1) == b) { node = n; break; }
        fresh[(size_t) b] = readBand (node, 4, [] (int) { return 100.0f; }, loads[(size_t) b]);
    }
    fresh[(size_t) preBand] = readBand (xml.getChildByName ("Pre"), 4, [] (int) { return 100.0f; }, loads[(size_t) preBand], true);
    fresh[(size_t) postBand] = readBand (xml.getChildByName ("Post"), 4, [] (int) { return 100.0f; }, loads[(size_t) postBand], true);
    for (int b = 0; b <= postBand; ++b)
        if (fresh[(size_t) b] == nullptr) fresh[(size_t) b] = std::make_unique<Band> (true, isExtraChain (b));
    auto inUse = [count] (int b) { return b < count || isExtraChain (b); };
    struct Kept { int band, slot; juce::MemoryBlock state; };
    std::vector<Kept> kept;
    bool wasPrepared = false;
    const auto withEffects = loads;   // every slot with an effect, for its knobs below
    {
        const juce::ScopedLock lock (gate);
        // Effects still loaded are taken over by the slot with the same serial, when it holds the same effect.
        std::map<int, Slot*> loaded;
        for (int k = 0; k < numBands + 2; ++k)
            for (auto& s : bands[(size_t) chainAt (k)]->slots)
                if (s->effect != nullptr && s->pendingRequest == 0) loaded[s->serial] = s.get();
        for (int b = 0; b <= postBand; ++b)
        {
            if (! inUse (b)) continue;
            auto& list = loads[(size_t) b];
            for (auto it = list.begin(); it != list.end();)
            {
                auto& target = *fresh[(size_t) b]->slots[(size_t) it->slot];
                juce::PluginDescription description;
                auto* descriptionXml = it->node->getChildByName ("PLUGIN");
                const auto found = loaded.find (target.serial);
                if (descriptionXml == nullptr || ! description.loadFromXml (*descriptionXml) || found == loaded.end()
                    || found->second->description.createIdentifierString() != description.createIdentifierString())
                {
                    ++it;
                    continue;
                }
                auto& from = *found->second;
                target.effect = std::move (from.effect);
                target.window = std::move (from.window);
                target.description = from.description;
                target.channels = from.channels;
                target.reportedLatency = from.reportedLatency;
                target.error = from.error;
                juce::MemoryBlock state;
                if (auto* st = it->node->getChildByName ("State")) state.fromBase64Encoding (st->getAllSubText());
                target.savedState = state;
                kept.push_back ({ b, it->slot, std::move (state) });
                loaded.erase (found);
                it = list.erase (it);
            }
            fresh[(size_t) b]->relink();   // links to knobs find the effects that were taken over
        }
        for (int b = 0; b <= postBand; ++b)
        {
            old[(size_t) b] = std::move (bands[(size_t) b]);
            bands[(size_t) b] = std::move (fresh[(size_t) b]);
        }
        wasPrepared = prepared;
        numBands = count;
        bandCount.store (count);
        std::array<float, triad::maxCrossovers> cuts {};
        std::array<int, triad::maxCrossovers> slopes {};
        readCrossovers (cuts.data());
        readSlopes (slopes.data());
        splitter.setBandCount (numBands, cuts.data(), slopes.data());
        for (int b = 0; b <= postBand; ++b) prepareBand (b, false);
        resetDelays();
        alignLatency();
        startFadeIn();
    }
    // LFO undo lists stay with their LFOs.
    for (auto& previous : old)
        for (int b = 0; b <= postBand; ++b)
            if (inUse (b) && bands[(size_t) b]->serial == previous->serial)
                for (auto& l : bands[(size_t) b]->lfos)
                    if (auto* before = previous->findLfo (l->id)) { l->undo = std::move (before->undo); l->redo = std::move (before->redo); }
    for (auto& band : old)
        for (auto& s : band->slots)
        {
            if (s->effect && wasPrepared) s->effect->releaseResources();   // no longer played: no lock needed
            retire (std::move (s->window)); retire (std::move (s->effect));
        }
    old = {};
    setPresetName (xml.getStringAttribute ("preset"));
    // Effects that were kept get their settings back, only where they differ (so nothing reloads needlessly).
    for (auto& k : kept)
        if (auto* s = slotAt (k.band, k.slot); s != nullptr && s->effect != nullptr && k.state.getSize() > 0)
        {
            juce::MemoryBlock current;
            s->effect->getStateInformation (current);
            if (current != k.state) s->effect->setStateInformation (k.state.getData(), (int) k.state.getSize());
        }
    // Effects that are gone are loaded again.
    for (int b = 0; b <= postBand; ++b)
        if (inUse (b))
            for (auto& load : loads[(size_t) b]) installFromXml (b, load.slot, *load.node, false);
    {
        const juce::ScopedLock lock (gate);
        for (int b = 0; b <= postBand; ++b) if (inUse (b)) bands[(size_t) b]->relink();
        alignLatency();
    }
    // Knobs the settings did not bring back (some effects keep a few out of them) are set as they were.
    for (int b = 0; b <= postBand; ++b)
        for (auto& load : withEffects[(size_t) b])
            if (auto* s = slotAt (b, load.slot); s != nullptr && s->effect != nullptr && s->pendingRequest == 0)
                if (auto* knobsXml = load.node->getChildByName ("Knobs"))
                {
                    std::set<int> linked;
                    for (auto& r : bands[(size_t) b]->routes)
                        if (r->destination.kind == Destination::effectParameter && r->destination.slotId == s->id) linked.insert (r->destination.parameter);
                    const auto values = juce::StringArray::fromTokens (knobsXml->getAllSubText(), " ", "");
                    const auto& knobs = s->effect->getParameters();
                    for (int k = 0; k < juce::jmin (values.size(), knobs.size()); ++k)
                    {
                        if (values[k] == "x" || linked.count (k) > 0) continue;
                        const float value = (float) values[k].getIntValue() * 1.0e-6f;
                        if (std::abs (knobs[k]->getValue() - value) > 2.0e-4f) knobs[k]->setValueNotifyingHost (value);
                    }
                }
    ++structureVersion;
    refreshWindowTitles();
    historyDirty = false;
    historyRefresh = true;
    lastChange = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b <= postBand && ! historyAwaitingLoads; ++b)
        if (inUse (b))
            for (auto& s : bands[(size_t) b]->slots) if (s->pendingRequest != 0) historyAwaitingLoads = true;
    stateChanged();   // the host's project changed (not a new undo step: historyApplying is set)
}

juce::AudioProcessorEditor* TriadProcessor::createEditor() { return new TriadEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TriadProcessor(); }
