#include "../Source/PluginProcessor.h"
#include "../Source/PluginCatalog.h"
#include "../Source/PluginEditor.h"
#include "../Source/Settings.h"
#include <juce_dsp/juce_dsp.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#if ! JUCE_WINDOWS
 #include <execinfo.h>
 #include <unistd.h>
#endif
#include <limits>
#include <set>
#include <stdexcept>

void require (bool b, const char* message) { if (! b) throw std::runtime_error (message); }
#include "CrossoverTests.h"
#include "GeneratorTests.h"

namespace
{
const std::vector<float> threeBandCuts { 250.0f, 2500.0f };

void setParam (TriadProcessor& p, const juce::String& id, float plain)
{
    auto* parameter = p.parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}
float getParam (TriadProcessor& p, const juce::String& id) { return p.parameters.getRawParameterValue (id)->load(); }
void bandMix (TriadProcessor& p, float percent)
{
    for (int b = 0; b < triad::maxBands; ++b) setParam (p, TriadProcessor::bandMixID (b), percent);
}
juce::AudioBuffer<float> input (int n)
{
    juce::AudioBuffer<float> b (2, n);
    juce::Random random (12345);
    for (int s = 0; s < n; ++s)
        for (int c = 0; c < 2; ++c) b.setSample (c, s, random.nextFloat() * 0.5f - 0.25f);
    return b;
}
// Independent model: Triad's splitter, per-band gain, overall delay.
juce::AudioBuffer<float> reference (const juce::AudioBuffer<float>& in, double sr, const std::vector<float>& gains, int delay,
                                    const std::vector<float>& cuts = threeBandCuts, const std::vector<int>& slopes = {})
{
    triad::Splitter splitter;
    splitter.prepare (sr, in.getNumSamples());
    std::array<float, triad::maxCrossovers> c {};
    std::array<int, triad::maxCrossovers> o {};
    o.fill (triad::CrossoverFilter::defaultOrder);
    for (size_t i = 0; i < cuts.size(); ++i) c[i] = cuts[i];
    for (size_t i = 0; i < slopes.size(); ++i) o[i] = slopes[i];
    splitter.setBandCount ((int) gains.size(), c.data(), o.data());
    std::array<juce::AudioBuffer<float>, triad::maxBands> bands;
    for (auto& b : bands) b.setSize (2, in.getNumSamples());
    splitter.process (in, 0, in.getNumSamples(), bands);
    juce::AudioBuffer<float> out (2, in.getNumSamples()); out.clear();
    for (int s = delay; s < in.getNumSamples(); ++s)
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < gains.size(); ++i) out.addSample (ch, s, gains[i] * bands[i].getSample (ch, s - delay));
    return out;
}
void compare (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, const char* label)
{
    float error = 0;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < a.getNumSamples(); ++s) error = juce::jmax (error, std::abs (a.getSample (c, s) - b.getSample (c, s)));
    std::cout << label << ": max error " << error << std::endl;
    require (error < 2.0e-5f, label);
}
// `skip`: samples ignored at the start (an LFO-moved knob glides to its value in a few milliseconds).
void runAudioCase (TriadProcessor& p, double sr, const std::vector<float>& gains, int latency, const char* name,
                   const std::vector<float>& cuts = threeBandCuts, int skip = 0, const std::vector<int>& slopes = {})
{
    p.prepareToPlay (sr, 128);
    auto signal = input (8192);
    auto expected = reference (signal, sr, gains, latency, cuts, slopes);
    juce::MidiBuffer midi;
    p.processBlock (signal, midi);   // deliberately larger than the prepared block size
    if (skip > 0)
        for (int c = 0; c < 2; ++c) { signal.clear (c, 0, skip); expected.clear (c, 0, skip); }
    compare (signal, expected, name);
    require (p.getLatencySamples() == latency, "Host latency reporting");
}
// A hosted-effect stand-in with a stereo sidechain input that cannot be switched off (or, with
// monoOnly, an effect that only works in mono). Gain 0.25 and 73 samples of latency, like the test effect.
class SidechainEffect final : public juce::AudioPluginInstance
{
public:
    // postOnLoad: like some real plug-ins, it posts itself a message when made (holding a plain
    // pointer to itself), which must not arrive after the host has deleted it.
    explicit SidechainEffect (bool monoOnly = false, bool postOnLoad = false)
        : AudioPluginInstance (BusesProperties().withInput ("In", monoOnly ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(), true)
                                                .withOutput ("Out", monoOnly ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(), true)
                                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
          mono (monoOnly), posts (postOnLoad)
    {
        setLatencySamples (73);
        live().insert (this);
        if (postOnLoad)
            juce::MessageManager::callAsync ([self = this] { ++(live().count (self) > 0 ? delivered : deliveredAfterDeletion); });
    }
    ~SidechainEffect() override { live().erase (this); }
    static std::set<const void*>& live() { static std::set<const void*> s; return s; }
    static inline int delivered = 0, deliveredAfterDeletion = 0;
    // Its editor does the same when it opens.
    struct PostingEditor final : juce::GenericAudioProcessorEditor
    {
        explicit PostingEditor (juce::AudioProcessor& p) : GenericAudioProcessorEditor (p)
        {
            live().insert (this);
            juce::MessageManager::callAsync ([self = this] { ++(live().count (self) > 0 ? editorDelivered : editorDeliveredAfterDeletion); });
        }
        ~PostingEditor() override { live().erase (this); }
    };
    static inline int editorDelivered = 0, editorDeliveredAfterDeletion = 0;
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = mono ? "Mono Only" : "Sidechain Effect"; d.pluginFormatName = "Test"; d.manufacturerName = "Triad Tests";
        d.numInputChannels = 2; d.numOutputChannels = 2;
    }
    bool isBusesLayoutSupported (const BusesLayout& b) const override
    {
        const auto main = mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        return b.inputBuses.size() == 2 && b.inputBuses[1] == juce::AudioChannelSet::stereo()
            && b.getMainInputChannelSet() == main && b.getMainOutputChannelSet() == main;
    }
    const juce::String getName() const override { return mono ? "Mono Only" : "Sidechain Effect"; }
    void prepareToPlay (double, int) override { delay.setSize (2, 74); delay.clear(); pos = 0; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        if (b.getNumChannels() < 4) { b.clear(); return; }
        for (int s = 0; s < b.getNumSamples(); ++s)
        {
            for (int c = 0; c < 2; ++c)
            {
                delay.setSample (c, pos, b.getSample (c, s));
                b.setSample (c, s, delay.getSample (c, (pos + 1) % 74) * 0.25f + b.getSample (c + 2, s));
            }
            pos = (pos + 1) % 74;
        }
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return posts ? new PostingEditor (*this) : nullptr; }
    bool hasEditor() const override { return posts; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
private:
    bool mono, posts = false;
    juce::AudioBuffer<float> delay;
    int pos = 0;
};

// A broken effect: its output is NaN. Counts how often the host restarts it.
class NanEffect final : public juce::AudioPluginInstance
{
public:
    NanEffect() : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                                       .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = "NaN Effect"; d.pluginFormatName = "Test"; }
    const juce::String getName() const override { return "NaN Effect"; }
    void prepareToPlay (double, int) override { ++prepares; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, std::numeric_limits<float>::quiet_NaN());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    static inline int prepares = 0;
};

float maxError (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
{
    float error = 0;
    for (int c = 0; c < 2; ++c)
        for (int s = from; s < to; ++s) error = juce::jmax (error, std::abs (a.getSample (c, s) - b.getSample (c, s)));
    return error;
}
// A DAW transport for the tests: playing from bar 1 at 120 BPM, 4/4.
struct TestPlayHead final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setTimeInSamples ((juce::int64) 0);
        info.setIsPlaying (true);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};
std::vector<juce::XmlElement*> slotNodes (juce::XmlElement& root)
{
    std::vector<juce::XmlElement*> result;
    for (auto* band : root.getChildWithTagNameIterator ("Band"))
        for (auto* slot : band->getChildWithTagNameIterator ("Slot")) result.push_back (slot);
    return result;
}
// Copies a .vst3 bundle under another name (on Linux, the binary inside must match the bundle name).
bool copyBundle (const juce::File& source, const juce::File& dest)
{
    dest.getParentDirectory().createDirectory();
    if (! source.copyDirectoryTo (dest)) return false;
   #if JUCE_LINUX || JUCE_BSD
    for (auto& arch : dest.getChildFile ("Contents").findChildFiles (juce::File::findDirectories, false, "*-linux"))
        for (auto& so : arch.findChildFiles (juce::File::findFiles, false, "*.so"))
            so.moveFileTo (arch.getChildFile (dest.getFileNameWithoutExtension() + ".so"));
   #endif
    return true;
}
void fillTestNoise (juce::AudioBuffer<float>& b, juce::Random& r)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * 0.3f);
}
juce::String writeImage (const juce::File& file, const juce::Image& image)
{
    juce::FileOutputStream stream (file);
    require (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk(), "Create fresh screenshot");
    juce::PNGImageFormat png;
    require (png.writeImageToStream (image, stream), "Write UI screenshot");
    return file.getFullPathName();
}
}

// On a crash, says which part of the run it was in and prints the crashing thread's call stack.
const char* volatile testStage = "starting";
#if ! JUCE_WINDOWS
void onTestCrash (int signal)
{
    const char* parts[] { "\n*** CRASH (signal ", signal == SIGSEGV ? "SIGSEGV" : signal == SIGBUS ? "SIGBUS" : signal == SIGABRT ? "SIGABRT" : "other",
                          ") during: ", testStage, "\n" };
    for (const char* t : parts) { const auto ignored = ::write (2, t, std::strlen (t)); juce::ignoreUnused (ignored); }
    void* frames[128];
    backtrace_symbols_fd (frames, backtrace (frames, 128), 2);
    std::signal (signal, SIG_DFL);
    std::raise (signal);
}
#endif

int main (int argc, char** argv)
{
   #if ! JUCE_WINDOWS
    for (int signal : { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE }) std::signal (signal, onTestCrash);
   #endif
    testStage = "running the tests";
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
      {   // every Triad and effect made by the tests lives in this scope
        require (argc >= 5, "Usage: TriadTests <Test Effect.vst3> <Test Synth.vst3> <Crash Fixture.vst3> <Triad Scanner> [screenshot.png] [Triad.vst3]");
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        const juce::File synth (juce::String::fromUTF8 (argv[2]));
        const juce::File crashFixture (juce::String::fromUTF8 (argv[3]));
        const juce::File scannerHelper (juce::String::fromUTF8 (argv[4]));
        const juce::File screenshotFile = argc >= 6 ? juce::File (juce::String::fromUTF8 (argv[5])) : juce::File();
        const juce::File triadVst3 = argc >= 7 ? juce::File (juce::String::fromUTF8 (argv[6])) : juce::File();
        require (crashFixture.exists() && scannerHelper.existsAsFile(), "Crash fixture and scanner helper are built");
        runCrossoverTests();
        generatorTests::run();

        // ---- Built-in Linkwitz-Riley filter equals JUCE's -----------------------------------
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const auto signal = input (8192);
            triad::CrossoverFilter mine, minePhase;   // order 4: 24 dB/oct Linkwitz-Riley
            juce::dsp::LinkwitzRileyFilter<float> theirs, theirsPhase;
            mine.prepare (sr); minePhase.prepare (sr);
            for (auto* f : { &theirs, &theirsPhase }) f->prepare ({ sr, 8192, 2 });
            theirsPhase.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
            juce::AudioBuffer<float> a (6, 8192), b (6, 8192);
            for (int s = 0; s < 8192; ++s)
            {
                if (s % 1024 == 0)
                {
                    const auto hz = (float) (60.0 * std::pow (2.0, s / 1024));
                    mine.setCutoffFrequency (hz); minePhase.setCutoffFrequency (hz);
                    theirs.setCutoffFrequency (hz); theirsPhase.setCutoffFrequency (hz);
                }
                for (int c = 0; c < 2; ++c)
                {
                    const float x = signal.getSample (c, s);
                    float lo, hiOut, tlo, thi;
                    mine.processSample (c, x, lo, hiOut); theirs.processSample (c, x, tlo, thi);
                    a.setSample (c, s, lo); a.setSample (2 + c, s, hiOut); a.setSample (4 + c, s, minePhase.processAllpass (c, x));
                    b.setSample (c, s, tlo); b.setSample (2 + c, s, thi); b.setSample (4 + c, s, theirsPhase.processSample (c, x));
                }
            }
            float error = 0;
            for (int c = 0; c < 6; ++c)
                for (int s = 0; s < 8192; ++s) error = juce::jmax (error, std::abs (a.getSample (c, s) - b.getSample (c, s)));
            std::cout << "Built-in 24 dB/oct crossover (LR4) vs JUCE dsp filter at " << sr << " Hz: max error " << error << std::endl;
            require (error < 1.0e-6f, "Built-in Linkwitz-Riley filter matches JUCE");
        }

        // ---- N-band reconstruction: the bands sum to the cascade of all-passes (1..10 bands) ---
        {
            float worst = 0;
            for (int n = 1; n <= triad::maxBands; ++n)
            {
                std::vector<float> cuts;
                for (int j = 0; j < n - 1; ++j) cuts.push_back (TriadProcessor::defaultCrossover (j, n));
                const auto signal = input (16384);
                auto actual = reference (signal, 48000, std::vector<float> ((size_t) n, 1.0f), 0, cuts);
                auto expected = signal;
                std::vector<juce::dsp::LinkwitzRileyFilter<float>> ap ((size_t) juce::jmax (0, n - 1));
                for (size_t j = 0; j < ap.size(); ++j)
                {
                    ap[j].prepare ({ 48000, 16384, 2 });
                    ap[j].setType (juce::dsp::LinkwitzRileyFilterType::allpass);
                    ap[j].setCutoffFrequency (cuts[j]);
                }
                for (int s = 0; s < expected.getNumSamples(); ++s)
                    for (int c = 0; c < 2; ++c)
                    {
                        float x = signal.getSample (c, s);
                        for (auto& f : ap) x = f.processSample (c, x);
                        expected.setSample (c, s, x);
                    }
                float error = 0;
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < expected.getNumSamples(); ++s) error = juce::jmax (error, std::abs (actual.getSample (c, s) - expected.getSample (c, s)));
                worst = juce::jmax (worst, error);
                require (error < 2.0e-5f, "N-band crossover reconstruction");
            }
            std::cout << "Phase-correct reconstruction, 1 to 10 bands: max error " << worst << std::endl;
        }

        // ---- Crossover slopes, 6 to 96 dB/oct ----------------------------------------------------
        {
            constexpr int length = 16384;
            constexpr double sr = 48000.0;
            // Impulse responses of the bands, and their sum, as magnitude spectra.
            auto spectra = [] (int bandCount, const std::vector<float>& cuts, const std::vector<int>& orders)
            {
                triad::Splitter sp;
                sp.prepare (sr, length);
                std::array<float, triad::maxCrossovers> c {};
                std::array<int, triad::maxCrossovers> o {};
                o.fill (4);
                for (size_t i = 0; i < cuts.size(); ++i) c[i] = cuts[i];
                for (size_t i = 0; i < orders.size(); ++i) o[i] = orders[i];
                sp.setBandCount (bandCount, c.data(), o.data());
                juce::AudioBuffer<float> impulse (2, length);
                impulse.clear();
                impulse.setSample (0, 0, 1.0f); impulse.setSample (1, 0, 1.0f);
                std::array<juce::AudioBuffer<float>, triad::maxBands> bands;
                for (auto& b : bands) b.setSize (2, length);
                sp.process (impulse, 0, length, bands);
                std::vector<std::vector<float>> result ((size_t) bandCount + 1);
                std::vector<std::complex<float>> sum ((size_t) length);
                for (int b = 0; b <= bandCount; ++b)
                {
                    std::vector<std::complex<float>> bins ((size_t) length);
                    for (int i = 0; i < length; ++i)
                    {
                        const float v = b < bandCount ? bands[(size_t) b].getSample (0, i) : 0.0f;
                        bins[(size_t) i] = { b < bandCount ? v : sum[(size_t) i].real(), 0.0f };
                        if (b < bandCount) sum[(size_t) i] += v;
                    }
                    triad::fft (bins);
                    for (int k = 0; k <= length / 2; ++k) result[(size_t) b].push_back (std::abs (bins[(size_t) k]));
                }
                return result;   // [band...] then the sum
            };
            const float binHz = (float) (sr / length);
            for (int order = triad::CrossoverFilter::minOrder; order <= triad::CrossoverFilter::maxOrder; ++order)
            {
                const auto m = spectra (2, { 1000.0f }, { order });
                float flat = 0.0f, shape = 0.0f;
                for (int k = 1; k < length / 2 - 1; ++k)
                {
                    const float f = (float) k * binHz;
                    flat = juce::jmax (flat, std::abs (m[2][(size_t) k] - 1.0f));
                    shape = juce::jmax (shape, std::abs (m[0][(size_t) k] - triad::CrossoverFilter::lowMagnitude (order, f, 1000.0f, sr)));
                    shape = juce::jmax (shape, std::abs (m[1][(size_t) k] - triad::CrossoverFilter::highMagnitude (order, f, 1000.0f, sr)));
                }
                // Steepness: the low band two octaves above the crossover, and the high band two below.
                // (Two octaves for 6-48 dB/oct; one octave for the steeper ones, which are below float noise two octaves out.)
                const bool steep = order > triad::CrossoverFilter::steepestClassic;
                const float above = steep ? 2000.0f : 4000.0f, below = steep ? 500.0f : 250.0f;
                const auto kAbove = (size_t) juce::roundToInt (above / binHz), kBelow = (size_t) juce::roundToInt (below / binHz);
                const auto lowAbove = juce::Decibels::gainToDecibels (m[0][kAbove], -300.0f), highBelow = juce::Decibels::gainToDecibels (m[1][kBelow], -300.0f);
                std::cout << TriadProcessor::slopeText (order) << ": bands add up flat (max error " << flat << "), shapes match (max error "
                          << shape << "), low band at " << above << " Hz " << juce::String (lowAbove, 1) << " dB, high band at " << below << " Hz "
                          << juce::String (highBelow, 1) << " dB" << std::endl;
                require (flat < 1.0e-3f, "Bands add back flat at every slope");
                require (shape < 2.0e-3f, "Each slope has its Butterworth / Linkwitz-Riley shape");
                // About 6 dB per octave per 6 dB/oct of slope (exactly, far from the crossover).
                const float expectedDb = (steep ? -6.0f : -12.0f) * (float) order;
                require (std::abs (lowAbove - expectedDb) < 3.5f && std::abs (highBelow - expectedDb) < 3.5f, "Slope steepness");
                if (order == triad::CrossoverFilter::maxOrder)
                    require (lowAbove < -90.0f && highBelow < -90.0f, "The hardest slope is a near-brickwall cut: below -90 dB one octave away");
            }
            // Ten bands with every slope at once: still flat.
            {
                std::vector<float> cuts;
                std::vector<int> orders;
                for (int j = 0; j < 9; ++j) { cuts.push_back (TriadProcessor::defaultCrossover (j, 10)); orders.push_back (1 + (j * 3) % triad::CrossoverFilter::maxOrder); }
                const auto m = spectra (10, cuts, orders);
                float flat = 0.0f;
                for (int k = 1; k < length / 2 - 1; ++k) flat = juce::jmax (flat, std::abs (m[10][(size_t) k] - 1.0f));
                std::cout << "Ten bands with mixed slopes add up flat: max error " << flat << std::endl;
                require (flat < 2.0e-3f, "Ten bands with mixed slopes add back flat");
            }
            // Changing slopes while audio plays crossfades: no jumps, level kept, then flat again.
            {
                triad::Splitter sp;
                sp.prepare (sr, 512);
                std::array<float, triad::maxCrossovers> c { 300.0f, 3000.0f };
                std::array<int, triad::maxCrossovers> o {};
                o.fill (4);
                sp.setBandCount (3, c.data(), o.data());
                // Two sines (100 Hz and 1 kHz): any click would show as a sudden step between samples.
                juce::AudioBuffer<float> signal (2, 48000);
                for (int i = 0; i < signal.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        signal.setSample (ch, i, 0.25f * (float) (std::sin (juce::MathConstants<double>::twoPi * 100.0 * i / sr)
                                                                 + std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / sr)));
                const float smoothStep = 0.25f * juce::MathConstants<float>::twoPi * 1100.0f / (float) sr;
                std::array<juce::AudioBuffer<float>, triad::maxBands> bands;
                for (auto& b : bands) b.setSize (2, 512);
                float worstJump = 0.0f, inPeak = 0.0f, outPeak = 0.0f;
                float previous = 0.0f;
                for (int start = 0, step = 0; start + 512 <= signal.getNumSamples(); start += 512, ++step)
                {
                    if (step % 3 == 0) { o[0] = 1 + (step / 3) % 8; o[1] = 8 - (step / 3) % 8; sp.setSlopes (o.data()); }
                    sp.process (signal, start, 512, bands);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float x = bands[0].getSample (0, i) + bands[1].getSample (0, i) + bands[2].getSample (0, i);
                        worstJump = juce::jmax (worstJump, std::abs (x - previous));
                        previous = x;
                        outPeak = juce::jmax (outPeak, std::abs (x));
                        inPeak = juce::jmax (inPeak, std::abs (signal.getSample (0, start + i)));
                    }
                }
                std::cout << "Slopes changed 30 times while playing: output peak " << outPeak << " (input " << inPeak << "), largest step between samples "
                          << worstJump << " (the sines alone: " << smoothStep << ")" << std::endl;
                require (outPeak < 1.6f * inPeak && worstJump < 1.6f * smoothStep, "Slope changes crossfade without clicks");
            }
        }

        // ---- One effect per band (Triad 0.1 behaviour) ---------------------------------------
        TriadProcessor p;
        runAudioCase (p, 44100, { 1, 1, 1 }, 0, "Empty slots, oversized block");
        require (p.loadFile (1, effect).isEmpty(), "Load third-party VST3");
        runAudioCase (p, 48000, { 1, 0.25f, 1 }, 73, "Unequal band latency compensation");
        for (int i : { 0, 2 }) require (p.loadFile (i, effect).isEmpty(), "Load remaining bands");
        runAudioCase (p, 48000, { .25f, .25f, .25f }, 73, "All bands wet");
        bandMix (p, 0); runAudioCase (p, 96000, { 1, 1, 1 }, 73, "All bands dry with latency retained");
        bandMix (p, 50); runAudioCase (p, 44100, { .625f, .625f, .625f }, 73, "50 percent band mix");
        bandMix (p, 100);

        // ---- An effect whose sidechain input cannot be switched off (as some Audio Units) -------
        {
            // VST3 lets a host switch any bus off, so a format that cannot do so is stood in for by an
            // instance made here: gain 0.25, latency 73, plus whatever arrives on its sidechain.
            TriadProcessor sidechain;
            const auto error = sidechain.loadInstance (1, 0, std::make_unique<SidechainEffect>());
            require (error.isEmpty(), ("Effect with an always-on sidechain loads (" + error + ")").toRawUTF8());
            require (sidechain.getSlotInfo (1, 0).channels == 4,
                     ("Its sidechain input stays on (channels: " + juce::String (sidechain.getSlotInfo (1, 0).channels) + ")").toRawUTF8());
            runAudioCase (sidechain, 48000, { 1, 0.25f, 1 }, 73, "Always-on sidechain input is fed silence");
            // An effect Triad cannot host says what layout it offers.
            const auto refused = sidechain.loadInstance (0, 0, std::make_unique<SidechainEffect> (true));
            require (refused.contains ("Mono"), ("A mono-only effect is refused with its layout named (" + refused + ")").toRawUTF8());
            std::cout << "Mono-only effect refused: " << refused << std::endl;
        }
        // ---- A removed effect is deleted only after its own pending messages have arrived -------
        {
            TriadProcessor q;
            q.prepareToPlay (48000, 512);
            require (q.loadInstance (0, 0, std::make_unique<SidechainEffect> (false, true)).isEmpty(), "Load an effect that posts itself a message");
            q.removeSlot (0, 0);   // straight away, before its message has arrived
            const auto liveAfterRemoval = SidechainEffect::live().size();
            require (liveAfterRemoval >= 1, "The removed effect is not deleted on the spot");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1600);
            require (SidechainEffect::delivered == 1 && SidechainEffect::deliveredAfterDeletion == 0, "Its message arrives while it still exists");
            require (SidechainEffect::live().size() == 1, "A moment later only one idle copy of the effect is left");
            // The same when Triad itself is closed (a project closed, Triad removed from a track).
            {
                TriadProcessor closing;
                closing.prepareToPlay (48000, 512);
                require (closing.loadInstance (0, 0, std::make_unique<SidechainEffect> (false, true)).isEmpty(), "Load it into a Triad that is closed next");
            }
            require (! SidechainEffect::live().empty(), "Closing Triad does not delete its effects on the spot");
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1600);
            require (SidechainEffect::delivered == 2 && SidechainEffect::deliveredAfterDeletion == 0 && SidechainEffect::live().size() == 1,
                     "After Triad closes, its effects get their messages and are then deleted (one idle copy is kept)");
            // An effect window closed (with its slot removed) right after it opened.
            if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
            {
                TriadProcessor w;
                w.prepareToPlay (48000, 512);
                require (w.loadInstance (0, 0, std::make_unique<SidechainEffect> (false, true)).isEmpty(), "Load an effect whose editor posts a message");
                w.showEffect (0, 0);
                w.removeSlot (0, 0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1600);
                require (SidechainEffect::editorDelivered == 1 && SidechainEffect::editorDeliveredAfterDeletion == 0,
                         "A window closed right after opening gets its editor's message before it is deleted");
            }
            // Many copies removed at once: still only one idle copy stays loaded.
            {
                TriadProcessor many;
                many.prepareToPlay (48000, 512);
                for (int i = 0; i < 4; ++i)
                    require (many.loadInstance (0, i == 0 ? 0 : many.getSlotCount (0), std::make_unique<SidechainEffect>()).isEmpty(), "Load copies");
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1600);
            require (SidechainEffect::live().size() == 1, "One idle copy of each effect is kept, the rest are deleted");
            require (TriadProcessor::getIdleEffectCount() >= 1, "Idle copies are counted");
            std::cout << "Removed effects are deleted after their pending messages: passed" << std::endl;
        }
        // ---- An effect that outputs NaN cannot break the mix ------------------------------------
        {
            TriadProcessor q;
            q.prepareToPlay (48000, 512);
            require (q.loadInstance (1, 0, std::make_unique<NanEffect>()).isEmpty(), "Load an effect that outputs NaN");
            const int preparesAtLoad = NanEffect::prepares;
            juce::AudioBuffer<float> audio (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (5);
            float peak = 0.0f;
            for (int round = 0; round < 6; ++round)
            {
                for (int block = 0; block < 4; ++block)
                {
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i) audio.setSample (c, i, random.nextFloat() - 0.5f);
                    q.processBlock (audio, midi);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i)
                        {
                            require (std::isfinite (audio.getSample (c, i)), "Triad's output stays finite while a hosted effect outputs NaN");
                            peak = juce::jmax (peak, std::abs (audio.getSample (c, i)));
                        }
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
            }
            const auto info = q.getSlotInfo (1, 0);
            require (info.invalidAudio > 0 && NanEffect::prepares - preparesAtLoad == 3, "The effect is silenced and restarted three times");
            require (info.status.contains ("bypassed"), "Then it is bypassed");
            require (peak > 0.01f, "The other bands keep playing");
            std::cout << "NaN from a hosted effect: silenced, restarted 3 times, then bypassed (" << info.status << ")" << std::endl;
        }

        // ---- Crossover slopes as DAW parameters ---------------------------------------------
        {
            TriadProcessor q;
            q.prepareToPlay (48000, 128);
            require (q.getCrossoverSlope (0) == 4 && q.slopeParameter (0)->getCurrentValueAsText() == "24 dB/oct", "Slopes start at 24 dB/oct");
            q.setCrossoverSlope (0, 2);
            q.setCrossoverSlope (1, 7);
            require (q.slopeParameter (1)->getCurrentValueAsText() == "42 dB/oct", "Slope readout");
            runAudioCase (q, 48000, { 1, 1, 1 }, 0, "Soft and hard crossovers (12 and 42 dB/oct)", threeBandCuts, 0, { 2, 7 });
            juce::MemoryBlock state;
            q.getStateInformation (state);
            TriadProcessor r;
            r.setStateInformation (state.getData(), (int) state.getSize());
            require (r.getCrossoverSlope (0) == 2 && r.getCrossoverSlope (1) == 7, "Slopes are saved with the project");
            // A project from before 0.4.0 has no slope parameters: it opens at 24 dB/oct, sounding as before.
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
            {
                if (auto* params = xml->getChildByName ("Parameters"))
                    for (int i = params->getNumChildElements(); --i >= 0;)
                        if (params->getChildElement (i)->getStringAttribute ("id").startsWith ("slope"))
                            params->removeChildElement (params->getChildElement (i), true);
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
                r.setStateInformation (old.getData(), (int) old.getSize());
                require (r.getCrossoverSlope (0) == 4 && r.getCrossoverSlope (1) == 4, "Earlier projects open at 24 dB/oct");
            }
            // Each slope stays with its crossover when bands are added or removed below it.
            require (q.addBand (100.0f), "Add a band below the others");
            require (q.getCrossoverSlope (0) == 4 && q.getCrossoverSlope (1) == 2 && q.getCrossoverSlope (2) == 7, "Slopes move up with their crossovers");
            require (q.removeBand (0), "Remove it again");
            require (q.getCrossoverSlope (0) == 2 && q.getCrossoverSlope (1) == 7, "Slopes move back down");
            std::cout << "PASS: crossover slopes are DAW parameters, saved, recalled, and move with their crossovers" << std::endl;
        }

        // ---- Per-slot mix (FL-style slot knobs) ----------------------------------------------
        for (int b = 0; b < 3; ++b) p.setSlotMix (b, 0, 50);
        require (p.getParameters().size() == 2 * triad::maxCrossovers + 2 * triad::maxBands, "Slot mixes are not DAW parameters (band volumes and slopes are)");
        runAudioCase (p, 48000, { .625f, .625f, .625f }, 73, "50 percent slot mix");
        bandMix (p, 50);
        runAudioCase (p, 48000, { .8125f, .8125f, .8125f }, 73, "Slot mix and band mix together");
        bandMix (p, 100);
        for (int b = 0; b < 3; ++b) p.setSlotMix (b, 0, 100);

        // ---- Chains: several effects on one band, in order -----------------------------------
        require (p.getSlotCount (0) == 1, "A band starts with one slot");
        require (p.loadFile (0, 1, effect).isEmpty() && p.loadFile (0, 2, effect).isEmpty(), "Load a three-effect chain into new slots");
        require (p.getSlotCount (0) == 3 && p.getSlotCount (1) == 1, "Slots are added per band");
        require (p.loadFile (0, 7, effect).isNotEmpty() && p.getSlotCount (0) == 3, "Only the next slot can be added");
        runAudioCase (p, 48000, { .015625f, .25f, .25f }, 219, "Three-effect chain on one band, other bands aligned");
        p.setSlotEnabled (0, 2, false);
        require (! p.getSlotInfo (0, 2).enabled, "Slot light switches off");
        runAudioCase (p, 48000, { .0625f, .25f, .25f }, 146, "Bypassed slot leaves the chain and its latency");
        p.setSlotEnabled (0, 2, true);
        p.setSlotMix (0, 1, 0);
        runAudioCase (p, 48000, { .0625f, .25f, .25f }, 219, "Slot at 0 percent passes its input (latency kept)");
        require (p.moveSlot (0, 1, 1), "Move slot down");
        require (juce::exactlyEqual (p.getSlotMix (0, 2), 0.0f) && juce::exactlyEqual (p.getSlotMix (0, 1), 100.0f), "Slot mix travels with the moved effect");
        require (p.getSlotInfo (0, 1).loaded && p.getSlotInfo (0, 2).loaded, "Both moved slots keep their effects");
        runAudioCase (p, 48000, { .0625f, .25f, .25f }, 219, "Moving a slot keeps the chain's result");
        p.setSlotMix (0, 2, 100);
        p.removeEffect (0, 2);
        require (p.getSlotCount (0) == 3 && ! p.getSlotInfo (0, 2).hasContent, "Remove effect keeps an empty slot");
        p.removeSlot (0, 2); p.removeSlot (0, 1);
        require (p.getSlotCount (0) == 1 && p.getSlotInfo (0, 0).loaded, "Remove slot deletes the row");
        runAudioCase (p, 48000, { .25f, .25f, .25f }, 73, "Chain removed, latency back to one effect");

        // ---- Project recall with edited child state --------------------------------------------
        bandMix (p, 50);
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        juce::AudioPluginFormatManager fixtureManager;
        fixtureManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
        juce::OwnedArray<juce::PluginDescription> fixtureTypes;
        fixtureManager.getFormat (0)->findAllTypesForFile (fixtureTypes, effect.getFullPathName());
        juce::String fixtureError;
        auto fixture = fixtureManager.createPluginInstance (*fixtureTypes[0], 44100, 128, fixtureError);
        require (fixture != nullptr, "Create fixture for edited state");
        bool edited = false;
        for (auto* parameter : fixture->getParameters())
            if (parameter->getName (100) == "Gain") { parameter->setValueNotifyingHost (0.75f); edited = true; }
        require (edited, "Change hosted gain parameter");
        juce::MemoryBlock editedChildState;
        fixture->getStateInformation (editedChildState);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        require (xml->getIntAttribute ("version") == 4 && xml->getIntAttribute ("bands") == 3, "State version 4 with band count");
        require (slotNodes (*xml).size() == 3, "One saved slot per loaded effect");
        for (auto* node : slotNodes (*xml))
        {
            node->getChildByName ("State")->deleteAllTextElements();
            node->getChildByName ("State")->addTextElement (editedChildState.toBase64Encoding());
        }
        juce::AudioProcessor::copyXmlToBinary (*xml, saved);
        TriadProcessor restored; restored.prepareToPlay (44100, 128);
        restored.setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        for (int i = 0; i < 3; ++i)
        {
            const auto info = restored.getSlotInfo (i, 0);
            if (! info.loaded) std::cout << "Band " << i + 1 << ": '" << info.name << "' " << info.status << " (" << restored.getSlotCount (i) << " slots)" << std::endl;
            require (info.loaded, "Restore child plugin");
        }
        runAudioCase (restored, 44100, { .875f, .875f, .875f }, 73, "Hosted state and mix recall");

        // ---- Triad 0.1 projects still open -----------------------------------------------------
        {
            juce::XmlElement v1 ("TriadState");
            v1.setAttribute ("version", 1);
            auto* params = v1.createNewChildElement ("Parameters");
            for (auto [id, value] : std::vector<std::pair<const char*, float>> { { "lowCut", 300 }, { "highCut", 3000 }, { "mix0", 100 }, { "mix1", 100 }, { "mix2", 100 } })
            {
                auto* param = params->createNewChildElement ("PARAM");
                param->setAttribute ("id", id); param->setAttribute ("value", value);
            }
            auto* slot = v1.createNewChildElement ("Slot");
            slot->setAttribute ("index", 1);
            slot->addChildElement (fixtureTypes[0]->createXml().release());
            slot->createNewChildElement ("State")->addTextElement (juce::MemoryBlock().toBase64Encoding());
            juce::MemoryBlock v1Data;
            juce::AudioProcessor::copyXmlToBinary (v1, v1Data);
            TriadProcessor old;
            old.setStateInformation (v1Data.getData(), (int) v1Data.getSize());
            require (old.getBandCount() == 3 && old.getSlotInfo (1, 0).loaded && ! old.getSlotInfo (0, 0).hasContent, "0.1 project: effect lands in band 2, slot 1");
            runAudioCase (old, 48000, { 1, .25f, 1 }, 73, "Triad 0.1 project recall", { 300, 3000 });
        }

        // ---- Triad 0.2.0 / 0.2.1 projects (ten fixed slots, slot mixes were DAW parameters) ------
        {
            juce::XmlElement v2 ("TriadState");
            v2.setAttribute ("version", 2);
            v2.setAttribute ("bands", 3);
            auto* params = v2.createNewChildElement ("Parameters");
            for (auto [id, value] : std::vector<std::pair<const char*, float>> { { "lowCut", 250 }, { "highCut", 2500 }, { "slot0_1", 40 }, { "slot2_0", 20 } })
            {
                auto* param = params->createNewChildElement ("PARAM");
                param->setAttribute ("id", id); param->setAttribute ("value", value);
            }
            auto* band = v2.createNewChildElement ("Band");
            band->setAttribute ("index", 0);
            auto* slot = band->createNewChildElement ("Slot");
            slot->setAttribute ("index", 1);
            slot->setAttribute ("enabled", true);
            slot->addChildElement (fixtureTypes[0]->createXml().release());
            slot->createNewChildElement ("State")->addTextElement (juce::MemoryBlock().toBase64Encoding());
            juce::MemoryBlock v2Data;
            juce::AudioProcessor::copyXmlToBinary (v2, v2Data);
            TriadProcessor old;
            old.setStateInformation (v2Data.getData(), (int) v2Data.getSize());
            require (old.getSlotCount (0) == 2 && ! old.getSlotInfo (0, 0).hasContent && old.getSlotInfo (0, 1).loaded, "0.2 project: effect in band 1, slot 2");
            require (std::abs (old.getSlotMix (0, 1) - 40.0f) < 0.01f && std::abs (old.getSlotMix (2, 0) - 20.0f) < 0.01f, "0.2 slot mixes recalled");
            require (! old.parameters.state.getChildWithProperty ("id", "slot0_1").isValid(), "Old slot parameters dropped");
            runAudioCase (old, 48000, { .7f, 1, 1 }, 73, "Triad 0.2 project recall (40 percent slot mix)");
            juce::MemoryBlock v3; old.getStateInformation (v3);
            TriadProcessor again; again.setStateInformation (v3.getData(), (int) v3.getSize());
            require (again.getSlotCount (0) == 2 && std::abs (again.getSlotMix (0, 1) - 40.0f) < 0.01f, "Slot mix saved with the project");
        }

        if (triadVst3.exists())
        {
            juce::AudioPluginFormatManager manager;
            manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
            juce::OwnedArray<juce::PluginDescription> types;
            manager.getFormat (0)->findAllTypesForFile (types, triadVst3.getFullPathName());
            require (! types.isEmpty(), "Scan outer Triad VST3");
            juce::String error;
            auto outer = manager.createPluginInstance (*types[0], 44100, 128, error);
            require (outer != nullptr, "Load outer Triad VST3");
            juce::XmlElement wrapperState ("VST3PluginState");
            wrapperState.createNewChildElement ("IComponent")->addTextElement (saved.toBase64Encoding());
            juce::MemoryBlock wrappedState;
            juce::AudioProcessor::copyXmlToBinary (wrapperState, wrappedState);
            outer->setStateInformation (wrappedState.getData(), (int) wrappedState.getSize());
            juce::MemoryBlock outerSaved;
            outer->getStateInformation (outerSaved);
            outer.reset();
            outer = manager.createPluginInstance (*types[0], 44100, 128, error);
            require (outer != nullptr, "Reopen outer VST3");
            outer->setStateInformation (outerSaved.getData(), (int) outerSaved.getSize());
            outer->prepareToPlay (44100, 128);
            auto signal = input (4096);
            auto expected = reference (signal, 44100, { .875f, .875f, .875f }, 73);
            juce::MidiBuffer events;
            for (int offset = 0; offset < signal.getNumSamples(); offset += 128)
            {
                juce::AudioBuffer<float> chunk (signal.getArrayOfWritePointers(), 2, offset, 128);
                outer->processBlock (chunk, events);
            }
            compare (signal, expected, "Outer VST3 wrapper with nested effects and recalled state");
            outer->releaseResources();
            require (p.loadFile (0, triadVst3).isNotEmpty(), "Reject recursive self-hosting");
            require (p.getSlotInfo (0).loaded, "Rejected load preserves existing effect");
        }

        // ---- Missing plug-ins keep their identity and state -------------------------------------
        auto missingXml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        for (auto* node : slotNodes (*missingXml)) node->getChildByName ("PLUGIN")->setAttribute ("file", "/not-installed/Test.vst3");
        juce::MemoryBlock missingData;
        juce::AudioProcessor::copyXmlToBinary (*missingXml, missingData);
        TriadProcessor missing;
        missing.setStateInformation (missingData.getData(), (int) missingData.getSize());
        require (! missing.getSlotInfo (0, 0).loaded && missing.getSlotInfo (0, 0).status.isNotEmpty(), "Missing effect reported");
        bandMix (missing, 100);
        runAudioCase (missing, 44100, { 1, 1, 1 }, 0, "Missing effects pass bands dry");
        juce::MemoryBlock resaved; missing.getStateInformation (resaved);
        auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        require (slotNodes (*resavedXml).front()->getChildByName ("State")->getAllSubText()
              == slotNodes (*missingXml).front()->getChildByName ("State")->getAllSubText(), "Missing child state preserved");

        // ---- Adding and removing bands with effects loaded -------------------------------------
        {
            TriadProcessor q;
            q.prepareToPlay (48000, 128);
            require (q.loadFile (1, effect).isEmpty(), "Load effect for band tests");
            setParam (q, TriadProcessor::bandMixID (2), 30);
            require (q.addBand (1000.0f) && q.getBandCount() == 4, "Add a band at 1 kHz");
            const auto cuts = q.effectiveCrossovers();
            require (std::abs (cuts[1] - 1000.0f) < 0.5f, "New crossover at 1 kHz");
            require (q.getSlotInfo (1, 0).loaded && ! q.getSlotInfo (2, 0).hasContent, "Split band keeps its effect; the new band is empty");
            require (std::abs (getParam (q, TriadProcessor::bandMixID (3)) - 30.0f) < 0.01f
                     && std::abs (getParam (q, TriadProcessor::bandMixID (2)) - 100.0f) < 0.01f, "Band mixes move with their bands");
            runAudioCase (q, 48000, { 1, .25f, 1, 1 }, 73, "Added band keeps effects in place", { 250, 1000, 2500 });
            require (q.removeBand (1) && q.getBandCount() == 3, "Remove the band with the effect");
            require (! q.getSlotInfo (0, 0).hasContent && ! q.getSlotInfo (1, 0).hasContent && ! q.getSlotInfo (2, 0).hasContent,
                     "Removed band's effect is gone");
            require (std::abs (getParam (q, TriadProcessor::bandMixID (2)) - 30.0f) < 0.01f, "Remaining band mixes shift down");
            runAudioCase (q, 48000, { 1, 1, 1 }, 0, "Removing a band merges its range", { 1000, 2500 });
            while (q.addBand()) {}
            require (q.getBandCount() == 10, "Ten bands");
            bandMix (q, 100);
            constexpr int chainLength = 16;   // more than the old ten-slot limit
            for (int s = 0; s < chainLength; ++s) require (q.loadFile (9, s, effect).isEmpty(), "Keep adding slots to a band");
            require (q.getSlotCount (9) == chainLength, "Sixteen slots in one band");
            const auto ten = q.effectiveCrossovers();
            const auto chain = std::pow (0.25f, (float) chainLength);
            std::vector<float> gains (10, 1.0f); gains[9] = chain;
            runAudioCase (q, 48000, gains, 73 * chainLength, "Ten bands with a sixteen-effect chain", ten);
            juce::MemoryBlock tenState; q.getStateInformation (tenState);
            TriadProcessor tenRestored; tenRestored.setStateInformation (tenState.getData(), (int) tenState.getSize());
            require (tenRestored.getBandCount() == 10 && tenRestored.getSlotCount (9) == chainLength
                     && tenRestored.getSlotInfo (9, chainLength - 1).loaded, "Ten bands and sixteen slots recall");
            runAudioCase (tenRestored, 48000, gains, 73 * chainLength, "Ten-band project recall", ten);
        }

        // ---- LFO shapes: 2x and 1/2 undo each other; reverse and flip are exact ---------------------
        {
            namespace mod = triad::mod;
            const mod::Shape shape { { 0, 1, 0 }, { 0.3f, 0.2f, 2.5f }, { 0.6f, 0.9f, -1.5f }, { 1, 0, 0 } };
            auto difference = [] (const mod::Shape& a, const std::function<float (float)>& f)
            {
                float error = 0;
                for (int i = 0; i <= 2000; ++i)
                {
                    const float x = (float) i / 2000.0f * 0.99999f;
                    error = juce::jmax (error, std::abs (mod::evaluate (a, x) - f (x)));
                }
                return error;
            };
            const auto twice = mod::repeated (shape, 2);
            require (difference (twice, [&] (float x) { return mod::evaluate (shape, std::fmod (2.0f * x, 1.0f)); }) < 1.0e-4f, "2x repeats the pattern");
            require (difference (mod::halved (twice), [&] (float x) { return mod::evaluate (shape, x); }) < 1.0e-4f, "1/2 undoes 2x");
            require (difference (mod::halved (shape), [&] (float x) { return mod::evaluate (shape, x * 0.5f); }) < 1.0e-4f, "1/2 stretches the first half");
            require (difference (mod::reversed (shape), [&] (float x) { return mod::evaluate (shape, 1.0f - x); }) < 1.0e-4f, "Reverse mirrors the shape, curves included");
            require (difference (mod::flippedVertical (shape), [&] (float x) { return 1.0f - mod::evaluate (shape, x); }) < 1.0e-6f, "Flip vertical");
            require (std::abs (mod::curveFraction (0.5f, mod::curveForMidpoint (0.25f)) - 0.25f) < 1.0e-4f, "Dragging a curve puts its middle under the mouse");
            mod::Table table;
            table.render (shape);
            float tableError = 0;
            for (int i = 0; i < 1000; ++i) tableError = juce::jmax (tableError, std::abs (table.lookup (i / 1000.0) - mod::evaluate (shape, i / 1000.0f)));
            require (tableError < 2.0e-3f, "The audio thread's table follows the shape");
            require (mod::fromText (mod::toText (shape)).size() == shape.size(), "Copy and paste a wave");
            require (juce::String (mod::lengths()[(size_t) mod::defaultLength()].label) == "1 Bar", "LFOs start at 1 bar");
            std::cout << "PASS: LFO shape editing (2x, 1/2, reverse, flip, curves, copy/paste); table error " << tableError << std::endl;
        }

        // ---- LFOs move the knobs ---------------------------------------------------------------------------
        juce::MemoryBlock lfoState;
        {
            namespace mod = triad::mod;
            TriadProcessor q;
            q.prepareToPlay (48000, 128);
            require (q.loadFile (1, effect).isEmpty(), "Load effect for LFO tests");
            const int slotLfo = q.getSlotLfo (1, 0);
            require (slotLfo >= 0 && q.getLfoInfo (1, slotLfo).slot == 0 && ! q.getLfoInfo (1, slotLfo).active, "Each slot has its own LFO, off until drawn");
            require (q.getLfoInfo (1, slotLfo).target == "Triad Test Effect", "The LFO is named after its effect");
            require (q.getRouteCount (1) == 2 && q.getRouteInfo (1, 1).destinationName == juce::String::fromUTF8 ("1 \xc2\xb7 Triad Test Effect Mix"), "Band Mix and slot LFOs are routed already");
            runAudioCase (q, 48000, { 1, .25f, 1 }, 73, "Untouched LFOs change nothing");
            q.setLfoShape (1, slotLfo, mod::flat (0.5f), true);
            require (q.getLfoInfo (1, slotLfo).active && q.getSlotInfo (1, 0).modulated, "Drawing an LFO switches it on");
            runAudioCase (q, 48000, { 1, .625f, 1 }, 73, "LFO at 50% sets the slot mix to 50%", threeBandCuts, 2048);
            const int route = q.getRoutesFrom (1, slotLfo).front();
            q.setRouteAmount (1, route, 0.5f);
            runAudioCase (q, 48000, { 1, .4375f, 1 }, 73, "LFO amount 50%", threeBandCuts, 2048);
            q.setRouteAmount (1, route, 1.0f);
            q.setRouteInvert (1, route, true);
            q.setLfoShape (1, slotLfo, mod::flat (0.2f), true);
            runAudioCase (q, 48000, { 1, .4f, 1 }, 73, "Inverted LFO", threeBandCuts, 2048);
            q.setRouteMuted (1, route, true);
            runAudioCase (q, 48000, { 1, .25f, 1 }, 73, "Route switched off", threeBandCuts, 2048);
            q.setRouteMuted (1, route, false);
            q.setRouteInvert (1, route, false);
            require (q.undoLfo (1, slotLfo) && juce::exactlyEqual (q.getLfoInfo (1, slotLfo).shape.front().y, 0.5f), "Undo an LFO edit");
            require (q.redoLfo (1, slotLfo) && juce::exactlyEqual (q.getLfoInfo (1, slotLfo).shape.front().y, 0.2f), "Redo an LFO edit");
            q.setLfoShape (1, slotLfo, mod::flat (1.0f), true);
            setParam (q, TriadProcessor::bandGainID (0), -6.0f);
            runAudioCase (q, 48000, { juce::Decibels::decibelsToGain (-6.0f), .25f, 1 }, 73, "Band volume -6 dB");
            setParam (q, TriadProcessor::bandGainID (0), 0.0f);
            // An LFO on band 3's volume: a square wave, 1/4 note at 120 BPM, following the song position.
            const int extra = q.addLfo (2);
            require (extra >= 0 && q.addRoute (2, extra, { TriadProcessor::Destination::bandVolume, 0 }) >= 0, "Add an LFO and route it");
            q.setLfoShape (2, extra, { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } }, true);
            int quarter = 0;
            while (juce::String (mod::lengths()[(size_t) quarter].label) != "1/4") ++quarter;
            q.setLfoLength (2, extra, quarter);
            TestPlayHead head;
            q.setPlayHead (&head);
            q.prepareToPlay (48000, 512);
            auto signal = input (24000);
            const auto on = reference (signal, 48000, { 1, .25f, 1 }, 73), off = reference (signal, 48000, { 1, .25f, 0 }, 73);
            juce::MidiBuffer events;
            q.processBlock (signal, events);
            const float firstHalf = maxError (signal, on, 73 + 600, 12000 + 73 - 600), secondHalf = maxError (signal, off, 12000 + 73 + 600, 24000 - 600);
            std::cout << "Square LFO on band volume, in time with the song: errors " << firstHalf << " / " << secondHalf << std::endl;
            require (firstHalf < 2.0e-5f && secondHalf < 2.0e-5f, "A tempo-synced LFO shapes the band in time");
            require (std::abs (q.getLfoPhase (2, extra)) < 0.01f || std::abs (q.getLfoPhase (2, extra) - 1.0f) < 0.01f, "LFO position follows the song");
            q.setPlayHead (nullptr);
            q.getStateInformation (lfoState);
            // Moving a band's chain.
            setParam (q, TriadProcessor::bandMixID (1), 60.0f);
            require (q.moveBand (1, 2), "Move band 2's effects to band 3");
            require (! q.getSlotInfo (1, 0).hasContent && q.getSlotInfo (2, 0).loaded, "The chain moved with its effects");
            require (std::abs (getParam (q, TriadProcessor::bandMixID (2)) - 60.0f) < 0.01f && std::abs (getParam (q, TriadProcessor::bandMixID (1)) - 100.0f) < 0.01f,
                     "Band mixes move with their chains");
            require (q.getLfoCount (1) == 3 && q.getLfoCount (2) == 2, "LFOs move with their chains");
            require (q.moveBand (2, 1), "Move it back");
            // Chain presets: band 2's chain into band 3 of another Triad.
            auto chain = q.createChainXml (1);
            TriadProcessor other;
            other.prepareToPlay (48000, 128);
            require (chain != nullptr && other.loadChainXml (2, *chain).isEmpty(), "Load a chain preset");
            require (other.getSlotInfo (2, 0).loaded && other.getLfoCount (2) == 2 && std::abs (getParam (other, TriadProcessor::bandMixID (2)) - 60.0f) < 0.01f,
                     "Chain preset brings effects, LFOs and mixes");
            // Copy band state / Paste band state: within one Triad, and from one Triad to another.
            // (Two bands' chains compared, apart from when the effect's plug-in info was last read.)
            auto sameChain = [] (const std::unique_ptr<juce::XmlElement>& a, const std::unique_ptr<juce::XmlElement>& b)
            {
                std::function<void (juce::XmlElement&)> strip = [&strip] (juce::XmlElement& e)
                {
                    e.removeAttribute ("infoUpdateTime");
                    for (auto* child : e.getChildIterator()) strip (*child);
                };
                if (a == nullptr || b == nullptr) return false;
                juce::XmlElement x (*a), y (*b);
                strip (x); strip (y);
                return x.isEquivalentTo (&y, false);
            };
            TriadProcessor::clearCopiedBand();
            require (! TriadProcessor::hasCopiedBand() && other.pasteBandState (0).isNotEmpty(), "Nothing to paste before a copy");
            setParam (q, TriadProcessor::bandGainID (1), -4.5f);
            const int sourceLfos = q.getLfoCount (1);
            require (q.copyBandState (1) && TriadProcessor::hasCopiedBand(), "Copy band state");
            require (TriadProcessor::copiedBandDescription().startsWith ("band 2: "), "The copied state says where it came from");
            require (q.pasteBandState (0).isEmpty(), "Paste band state into another band");
            require (q.getSlotInfo (0, 0).loaded && q.getSlotInfo (0, 0).name == q.getSlotInfo (1, 0).name && q.getLfoCount (0) == q.getLfoCount (1),
                     "The pasted band has the same effects and LFOs");
            require (std::abs (getParam (q, TriadProcessor::bandMixID (0)) - 60.0f) < 0.01f && std::abs (getParam (q, TriadProcessor::bandGainID (0)) + 4.5f) < 0.01f,
                     "The pasted band has the same mix and volume");
            require (q.getSlotInfo (1, 0).loaded && q.getLfoCount (1) == sourceLfos, "Copying leaves the source band as it was");
            {
                require (sameChain (q.createChainXml (0), q.createChainXml (1)), "The pasted band has the same effect settings, slot mixes and LFOs");
            }
            require (other.pasteBandState (0).isEmpty() && other.getSlotInfo (0, 0).loaded, "Paste band state into another Triad");
            require (q.pasteBandState (9).isNotEmpty(), "Pasting into a band that does not exist is refused");
            {
                // The 3-dot menu and right-click menu items.
                std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (q.createEditor()));
                editor->bandMenuChoice (2, TriadEditor::bandMenuCopy);
                require (TriadProcessor::copiedBandDescription().startsWith ("band 3: "), "Band menu: Copy band state");
                editor->bandMenuChoice (0, TriadEditor::bandMenuPaste);
                require (sameChain (q.createChainXml (0), q.createChainXml (2)), "Band menu: Paste band state");
            }
            TriadProcessor::clearCopiedBand();
            std::cout << "PASS: LFOs on slot mixes and band volume, moving bands, chain presets, copy / paste band state" << std::endl;
        }
        {
            // Everything comes back with the project.
            TriadProcessor r;
            r.setStateInformation (lfoState.getData(), (int) lfoState.getSize());
            r.prepareToPlay (48000, 128);
            require (r.getLfoCount (2) == 3 && r.getRouteCount (2) == 3, "LFOs and routes recalled");
            const auto info = r.getLfoInfo (2, 2);
            require (info.shape.size() == 4 && juce::String (triad::mod::lengths()[(size_t) info.length].label) == "1/4", "LFO shape and length recalled");
            require (r.getRouteInfo (2, 2).destination.kind == TriadProcessor::Destination::bandVolume && ! r.getRouteInfo (2, 2).muted, "Route recalled");
            require (r.getLfoInfo (1, r.getSlotLfo (1, 0)).active, "Slot LFO recalled switched on");
            std::cout << "PASS: LFO project recall" << std::endl;
        }

        // ---- Startup preset, "Use the current settings", Initialize --------------------------------------
        {
            juce::SharedResourcePointer<TriadSettings> settings;
            const auto previousChoice = settings->getStartupPreset();
            const auto startupFile = TriadSettings::startupSettingsFile();
            const bool hadStartupFile = startupFile.existsAsFile();
            const auto previousStartupText = hadStartupFile ? startupFile.loadFileAsString() : juce::String();
            TriadSettings::presetFolder().createDirectory();
            const auto presetFile = TriadSettings::presetFolder().getChildFile ("Triad test startup.triadpreset");
            auto waitForStartup = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) TriadProcessor::startupDelayMs + 400); };
            {
                TriadProcessor source;
                source.prepareToPlay (48000, 512);
                source.addBand(); source.addBand();
                source.setCrossoverSlope (0, 12);
                require (source.loadFile (1, 0, effect).isEmpty(), "Load an effect for the startup preset");
                source.setPresetName ("Triad test startup");
                require (source.createPresetXml()->writeTo (presetFile), "Save a preset to start with");
            }
            settings->setStartupPreset ("Triad test startup");
            {
                TriadProcessor fresh;
                fresh.prepareToPlay (48000, 512);
                waitForStartup();
                require (fresh.getBandCount() == 5 && fresh.getCrossoverSlope (0) == 12 && fresh.getSlotInfo (1, 0).loaded
                         && fresh.getPresetName() == "Triad test startup", "A new Triad opens with the startup preset");
                fresh.initialize();
                require (fresh.getBandCount() == 3 && ! fresh.getSlotInfo (1, 0).hasContent && fresh.getCrossoverSlope (0) == 4
                         && fresh.getPresetName() == "Init" && fresh.getLatencySamples() == 0, "Initialize: an empty Triad");
                // "Use the current settings as the startup preset", from the gear menu.
                fresh.addBand();
                std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (fresh.createEditor()));
                require (editor->useCurrentSettingsAtStartup() && settings->getStartupPreset() == TriadSettings::currentSettings,
                         "Use the current settings as the startup preset");
            }
            {
                TriadProcessor fresh;
                fresh.prepareToPlay (48000, 512);
                waitForStartup();
                require (fresh.getBandCount() == 4, "A new Triad opens with the saved current settings");
            }
            {
                // A project the DAW restores right after creating Triad keeps its own settings.
                juce::MemoryBlock project;
                {
                    TriadProcessor saved;
                    saved.prepareToPlay (48000, 512);
                    saved.addBand(); saved.addBand(); saved.addBand(); saved.addBand();
                    saved.getStateInformation (project);
                }
                TriadProcessor restored;
                restored.setStateInformation (project.getData(), (int) project.getSize());
                restored.prepareToPlay (48000, 512);
                waitForStartup();
                require (restored.getBandCount() == 7, "A project's own settings win over the startup preset");
            }
            settings->setStartupPreset ({});
            {
                TriadProcessor fresh;
                fresh.prepareToPlay (48000, 512);
                waitForStartup();
                require (fresh.getBandCount() == 3 && ! fresh.getSlotInfo (0, 0).hasContent, "Startup preset Init: an empty Triad");
            }
            settings->setStartupPreset (previousChoice);
            presetFile.deleteFile();
            if (hadStartupFile) startupFile.replaceWithText (previousStartupText); else startupFile.deleteFile();
            std::cout << "PASS: startup preset (Init, a saved preset, the current settings), Initialize, projects keep their own settings" << std::endl;
        }

        // ---- Latency meter, CPU meter, LIVE ------------------------------------------------------------
        {
            TriadProcessor q;
            q.prepareToPlay (48000, 256);
            require (q.loadFile (1, 0, effect).isEmpty(), "Load an effect with latency");
            require (q.getLatencySamples() == 73 && q.getSlotInfo (1, 0).latency == 73 && q.getSlotInfo (1, 0).reportedLatency == 73,
                     "The effect's latency is reported per slot and in total");
            {
                std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (q.createEditor()));
                editor->refreshNow();
                require (editor->getDisplay().getLatencyText() == "1.5 ms", "The latency meter shows Triad's latency (73 samples at 48 kHz)");
                editor->getDisplay().getLiveButton().triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                require (q.isLiveMode(), "The LIVE button switches LIVE on");
                editor->refreshNow();
                require (editor->getDisplay().getLatencyText() == "0 ms", "With LIVE on the meter shows 0 ms");
            }
            require (q.getLatencySamples() == 0 && q.getSlotInfo (1, 0).liveBypassed && q.getSlotInfo (1, 0).latency == 0
                     && q.getSlotInfo (1, 0).reportedLatency == 73 && q.getSlotInfo (1, 0).enabled,
                     "LIVE: the effect with latency is passed by (still enabled), Triad adds no latency");
            {
                // With LIVE on, the sound comes straight through (the bands add back to the input).
                auto signal = input (4096);
                const auto expected = reference (signal, 48000, { 1, 1, 1 }, 0);
                juce::MidiBuffer midi;
                for (int offset = 0; offset < signal.getNumSamples(); offset += 256)
                {
                    juce::AudioBuffer<float> block (signal.getArrayOfWritePointers(), 2, offset, juce::jmin (256, signal.getNumSamples() - offset));
                    q.processBlock (block, midi);
                }
                require (maxError (signal, expected, 0, signal.getNumSamples()) < 1.0e-4f, "LIVE: no delay, the effect is passed by");
                require (q.takeMeters().cpu > 0.0f, "The CPU meter measures Triad's processing");
            }
            juce::MemoryBlock state;
            q.getStateInformation (state);
            {
                TriadProcessor restored;
                restored.setStateInformation (state.getData(), (int) state.getSize());
                restored.prepareToPlay (48000, 256);
                require (restored.isLiveMode() && restored.getLatencySamples() == 0, "LIVE is saved with the project");
            }
            q.setLiveMode (false);
            require (! q.isLiveMode() && q.getLatencySamples() == 73 && ! q.getSlotInfo (1, 0).liveBypassed, "LIVE off: the effect plays again");
            std::cout << "PASS: latency meter (total, per slot), CPU meter, LIVE switch" << std::endl;
        }

        // ---- Knob linking: LFOs on the knobs inside effects ------------------------------------------
        {
            namespace mod = triad::mod;
            TriadProcessor q;
            q.prepareToPlay (48000, 512);
            require (q.loadFile (1, 0, effect).isEmpty(), "Load an effect with a knob");
            const auto knobs = q.getEffectParameters (1, 0);
            require (! knobs.empty() && knobs.front().first.kind == TriadProcessor::Destination::effectParameter && knobs.front().second == "Gain",
                     "The effect's knobs are offered as destinations");
            {
                juce::PopupMenu menu;
                std::vector<TriadProcessor::Destination> chosen;
                triad::ui::addDestinationItems (menu, q, 1, nullptr, chosen);
                require (std::any_of (chosen.begin(), chosen.end(), [] (auto& d) { return d.kind == TriadProcessor::Destination::effectParameter; }),
                         "The route menu lists the knobs inside the effect");
            }
            auto* gain = q.getHostedEffect (1, 0)->getParameters()[knobs.front().first.parameter];
            const float base = gain->getValue();
            const int lfo = q.addLfo (1);
            q.setLfoShape (1, lfo, mod::preset (5), false);   // Square: top half a cycle, bottom the other half
            int quarter = 0;
            for (int i = 0; i < (int) mod::lengths().size(); ++i) if (juce::String (mod::lengths()[(size_t) i].label) == "1/16") quarter = i;
            q.setLfoLength (1, lfo, quarter);
            const int route = q.addRoute (1, lfo, knobs.front().first);
            require (route >= 0 && q.getRouteInfo (1, route).destinationName.endsWith (": Gain"), "Link an LFO to the effect's Gain knob");
            juce::Random r (3);
            juce::MidiBuffer midi;
            float lowest = 1.0f, highest = 0.0f;
            for (int i = 0; i < 200; ++i)
            {
                juce::AudioBuffer<float> audio (2, 128);
                fillTestNoise (audio, r);
                q.processBlock (audio, midi);
                lowest = juce::jmin (lowest, gain->getValue());
                highest = juce::jmax (highest, gain->getValue());
            }
            require (lowest < 0.02f && highest > 0.98f, "The knob follows the LFO (square: 0% and 100%)");
            q.setRouteMuted (1, route, true);
            { juce::AudioBuffer<float> audio (2, 128); audio.clear(); q.processBlock (audio, midi); }
            require (std::abs (gain->getValue() - base) < 1.0e-3f, "Switched off, the knob goes back to where it was");
            q.setRouteMuted (1, route, false);
            // Saved with the project, found again by the effect's own parameter ID.
            juce::MemoryBlock state;
            q.getStateInformation (state);
            {
                TriadProcessor restored;
                restored.setStateInformation (state.getData(), (int) state.getSize());
                restored.prepareToPlay (48000, 512);
                const auto info = restored.getRouteInfo (1, route);
                require (info.destination.kind == TriadProcessor::Destination::effectParameter && info.destinationName.endsWith (": Gain"),
                         "The link comes back with the project");
                auto* restoredGain = restored.getHostedEffect (1, 0)->getParameters()[info.destination.parameter];
                float lo = 1.0f, hi = 0.0f;
                for (int i = 0; i < 200; ++i)
                {
                    juce::AudioBuffer<float> audio (2, 128);
                    fillTestNoise (audio, r);
                    restored.processBlock (audio, midi);
                    lo = juce::jmin (lo, restoredGain->getValue()); hi = juce::jmax (hi, restoredGain->getValue());
                }
                require (lo < 0.02f && hi > 0.98f, "The restored link moves the knob");
            }
            // Copy / paste band state takes the link along, onto the pasted effect.
            require (q.copyBandState (1) && q.pasteBandState (0).isEmpty(), "Copy band 2 onto band 1");
            {
                bool linked = false;
                for (int i = 0; i < q.getRouteCount (0); ++i)
                    linked |= q.getRouteInfo (0, i).destination.kind == TriadProcessor::Destination::effectParameter;
                require (linked, "The pasted band keeps its knob link");
            }
            TriadProcessor::clearCopiedBand();
            q.removeRoute (1, route);
            require (std::abs (gain->getValue() - base) < 1.0e-3f, "Unlinked, the knob goes back to where it was");
            // LEARN: move a knob in the effect's window and it is linked.
            require (q.beginParameterLearn (1, lfo) && q.isLearningParameter(), "Start LEARN");
            require (q.pollParameterLearn() < 0, "Nothing linked before a knob moves");
            gain->beginChangeGesture();
            gain->setValueNotifyingHost (0.6f);
            gain->endChangeGesture();
            const int learned = q.pollParameterLearn();
            require (learned >= 0 && ! q.isLearningParameter() && q.getRouteInfo (1, learned).destinationName.endsWith (": Gain"),
                     "LEARN links the knob that was moved");
            require (q.beginParameterLearn (1, lfo), "LEARN again");
            q.removeEffect (1, 0);
            require (! q.isLearningParameter(), "Removing the effect ends LEARN (it stops listening first)");
            require (q.getRouteInfo (1, learned).destinationName.isNotEmpty(), "A link to an effect that is gone waits (no crash)");
            { juce::AudioBuffer<float> audio (2, 128); fillTestNoise (audio, r); q.processBlock (audio, midi); }
            require (q.loadFile (1, 0, effect).isEmpty(), "Load the effect again");
            {
                auto* again = q.getHostedEffect (1, 0)->getParameters()[0];
                float lo = 1.0f, hi = 0.0f;
                for (int i = 0; i < 200; ++i)
                {
                    juce::AudioBuffer<float> audio (2, 128);
                    fillTestNoise (audio, r);
                    q.processBlock (audio, midi);
                    lo = juce::jmin (lo, again->getValue()); hi = juce::jmax (hi, again->getValue());
                }
                require (hi - lo > 0.3f, "The waiting link finds the effect again");
            }
            std::cout << "PASS: knob linking (LFOs on knobs inside effects): menu, follows the LFO, off/unlink restore, saved, copy/paste, LEARN" << std::endl;
        }

        // ---- Undo / redo, Copy / Paste plugin state, dragging slots ------------------------------------
        {
            namespace mod = triad::mod;
            auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
            TriadProcessor q;
            q.prepareToPlay (48000, 512);
            q.loadPresetXml (*TriadProcessor::createInitXml(), false);   // as a project from the DAW: a fresh history
            pump ((int) TriadProcessor::startupDelayMs + 250);
            require (! q.canUndo() && ! q.canRedo(), "A fresh Triad has nothing to undo");
            require (q.loadFile (0, 0, effect).isEmpty(), "Undo test: load an effect");
            pump (700);   // a snapshot is taken a moment after the change
            require (q.canUndo() && q.getUndoSteps() == 1, "Loading an effect is one undo step (taken by itself)");
            auto* first = q.getHostedEffect (0, 0);
            auto* firstGain = first->getParameters()[0];
            // A knob turned in the effect's own window (a gesture on its parameter).
            firstGain->beginChangeGesture();
            firstGain->setValueNotifyingHost (0.8f);
            firstGain->endChangeGesture();
            pump (700);
            require (q.getUndoSteps() == 2, "Turning a knob inside an effect is an undo step");
            require (q.loadFile (0, 1, effect).isEmpty(), "Undo test: a second effect in a new slot");
            q.commitHistory();
            auto* second = q.getHostedEffect (0, 1);
            q.setSlotMix (0, 1, 40.0f);
            q.commitHistory();
            require (q.moveSlotTo (0, 1, 0) && q.getHostedEffect (0, 0) == second && q.getHostedEffect (0, 1) == first,
                     "Move the second effect above the first");
            q.commitHistory();
            auto* cross = q.crossoverParameter (0);
            const float crossBefore = cross->getValue();
            cross->beginChangeGesture();   // a crossover dragged on the display
            cross->setValueNotifyingHost (0.3f);
            cross->endChangeGesture();
            pump (700);
            require (q.addBand(), "Undo test: add a band");
            q.commitHistory();
            const int steps = q.getUndoSteps();
            require (steps == 7, ("Each change is one undo step (" + juce::String (steps) + ")").toRawUTF8());
            require (q.undo() && q.getBandCount() == 3, "Undo: the band that was added goes");
            require (q.undo() && std::abs (cross->getValue() - crossBefore) < 1.0e-4f, "Undo: the crossover goes back");
            require (q.undo() && q.getHostedEffect (0, 0) == first && q.getHostedEffect (0, 1) == second,
                     "Undo: the effects go back in their order, still loaded (not reloaded)");
            require (q.undo() && std::abs (q.getSlotMix (0, 1) - 100.0f) < 0.01f, "Undo: the slot mix goes back");
            require (q.undo() && q.getSlotCount (0) == 1 && q.getHostedEffect (0, 0) == first, "Undo: the second effect goes");
            require (q.undo() && std::abs (firstGain->getValue() - 0.25f) < 1.0e-3f && q.getHostedEffect (0, 0) == first,
                     "Undo: the knob inside the effect goes back (the same effect, still loaded)");
            require (q.undo() && ! q.getSlotInfo (0, 0).hasContent && ! q.canUndo(), "Undo: back to the start");
            require (! q.undo(), "Nothing more to undo");
            for (int i = 0; i < steps; ++i) require (q.redo(), "Redo, step by step");
            require (! q.canRedo() && q.getBandCount() == 4 && q.getSlotCount (0) == 2 && std::abs (q.getSlotMix (0, 0) - 40.0f) < 0.01f
                     && std::abs (q.crossoverParameter (0)->getValue() - 0.3f) < 1.0e-4f, "Redo: everything is back");
            require (std::abs (q.getHostedEffect (0, 1)->getParameters()[0]->getValue() - 0.8f) < 1.0e-3f,
                     "Redo: the effect that came back has its knob where it was");
            {
                juce::Random r (11);
                juce::MidiBuffer midi;
                for (int i = 0; i < 40; ++i)
                {
                    juce::AudioBuffer<float> audio (2, 512);
                    fillTestNoise (audio, r);
                    q.processBlock (audio, midi);
                    for (int c = 0; c < 2; ++c)
                        for (int k = 0; k < 512; ++k) require (std::isfinite (audio.getSample (c, k)), "Audio after undo/redo is finite");
                }
            }
            q.undo(); q.undo();
            require (q.canRedo() && q.getRedoSteps() == 2, "Two steps to redo");
            q.setSlotMix (0, 0, 10.0f);
            q.commitHistory();
            require (! q.canRedo(), "A new change after undo: the undone steps are gone");
            {
                // A knob the effect leaves out of its saved settings (the test effect's Tone) also goes back.
                auto* tone = q.getHostedEffect (0, 0)->getParameters()[1];
                const float toneBefore = tone->getValue();
                tone->beginChangeGesture();
                tone->setValueNotifyingHost (0.9f);
                tone->endChangeGesture();
                q.commitHistory();
                require (q.undo() && std::abs (tone->getValue() - toneBefore) < 1.0e-3f, "Undo puts back a knob the effect does not save in its settings");
                require (q.redo() && std::abs (tone->getValue() - 0.9f) < 1.0e-3f, "Redo sets that knob again");
            }
            // An LFO's own undo list stays with it through the main undo.
            const int lfo = q.getSlotLfo (0, 0);
            q.setLfoShape (0, lfo, mod::preset (3), true);
            q.commitHistory();
            q.setSlotMix (0, 0, 20.0f);
            q.commitHistory();
            require (q.undo() && q.getLfoInfo (0, q.getSlotLfo (0, 0)).canUndo, "The LFO's own undo survives a main undo");
            require (q.undo() && q.getLfoInfo (0, q.getSlotLfo (0, 0)).shape.size() != 3, "Undo: the LFO shape goes back");
            require (q.redo(), "Redo the LFO shape");
            // A preset picked in Triad is one step; a project opened by the DAW starts a fresh history.
            q.loadPresetXml (*TriadProcessor::createInitXml());
            require (! q.getSlotInfo (0, 0).hasContent && q.getSlotCount (0) == 1, "Initialize");
            require (q.undo() && q.getSlotCount (0) == 2 && q.getSlotInfo (0, 0).loaded && q.getSlotInfo (0, 1).loaded,
                     "Undo a preset (Initialize): the effects come back");
            juce::MemoryBlock project;
            q.getStateInformation (project);
            q.setStateInformation (project.getData(), (int) project.getSize());
            require (! q.canUndo() && ! q.canRedo(), "A project opened by the DAW starts a fresh history");
            q.commitHistory();
            for (int i = 0; i < 130; ++i) { q.setSlotMix (0, 0, (float) (i % 90)); q.commitHistory(); }
            require (q.getUndoSteps() == TriadProcessor::maxHistorySteps, "The history keeps the last 100 steps");
            require (q.isLiveMode() == false, "LIVE is off");
            q.setLiveMode (true);
            q.commitHistory();
            require (q.getUndoSteps() == TriadProcessor::maxHistorySteps && q.undo() && q.isLiveMode(), "LIVE is not an undo step and undo leaves it on");
            q.setLiveMode (false);
            std::cout << "PASS: undo / redo (effects kept loaded, knobs inside effects, crossovers, bands, LFOs, presets, history limit)" << std::endl;

            // Copy plugin state: the effect with its settings, pasted into a new slot, onto the same effect, into an empty slot.
            TriadProcessor c;
            c.prepareToPlay (48000, 512);
            require (c.pasteEffectState (0, 0).isNotEmpty() || TriadProcessor::hasCopiedEffect(), "Nothing to paste yet");
            TriadProcessor::clearCopiedEffect();
            require (c.pasteEffectState (0, 0).isNotEmpty(), "Pasting with nothing copied says so");
            require (c.loadFile (0, 0, effect).isEmpty() && c.loadFile (1, 0, effect).isEmpty(), "Copy test: two effects");
            c.getHostedEffect (0, 0)->getParameters()[0]->setValueNotifyingHost (0.7f);
            c.setSlotMix (0, 0, 55.0f);
            require (! c.copyEffectState (2, 0), "An empty slot has no plug-in state to copy");
            require (c.copyEffectState (0, 0) && TriadProcessor::hasCopiedEffect() && TriadProcessor::copiedEffectName() == "Triad Test Effect",
                     "Copy plugin state");
            auto* target = c.getHostedEffect (1, 0);
            require (c.pasteEffectState (1, 0).isEmpty() && c.getHostedEffect (1, 0) == target
                     && std::abs (target->getParameters()[0]->getValue() - 0.7f) < 1.0e-3f && std::abs (c.getSlotMix (1, 0) - 55.0f) < 0.01f,
                     "Paste onto the same effect: only its settings change (it stays loaded)");
            require (c.pasteEffectState (2, 0).isEmpty() && c.getSlotInfo (2, 0).loaded
                     && std::abs (c.getHostedEffect (2, 0)->getParameters()[0]->getValue() - 0.7f) < 1.0e-3f, "Paste into an empty slot");
            require (c.pasteEffectState (2, c.getSlotCount (2)).isEmpty() && c.getSlotCount (2) == 2
                     && std::abs (c.getHostedEffect (2, 1)->getParameters()[0]->getValue() - 0.7f) < 1.0e-3f, "Paste into a new slot");
            {
                TriadProcessor other;   // the clipboard is shared by every Triad in the session
                other.prepareToPlay (48000, 512);
                require (other.pasteEffectState (0, 0).isEmpty() && other.getSlotInfo (0, 0).loaded, "Paste into another Triad");
            }
            std::cout << "PASS: copy / paste plugin state" << std::endl;

            // Drag a slot (in the band strip) to another place in its band.
            std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (c.createEditor()));
            editor->setVisible (true);
            require (c.loadFile (0, 1, effect).isEmpty() && c.loadFile (0, 2, effect).isEmpty(), "Drag test: three effects in band 1");
            c.setSlotMix (0, 0, 11.0f); c.setSlotMix (0, 1, 22.0f); c.setSlotMix (0, 2, 33.0f);
            editor->refreshNow();
            auto* strip = editor->getStrip (0);
            require (strip->getNumRows() == 3, "Three rows");
            auto& row = strip->getRow (0);
            auto* holder = row.getParentComponent();
            const float x = 60.0f, startY = (float) row.getY() + 15.0f;
            auto at = [&] (float holderY, bool dragging) { return mouseAt (&row, x, holderY - (float) row.getY(), dragging); };
            row.mouseDown (at (startY, false));
            for (int i = 1; i <= 8; ++i) row.mouseDrag (at (startY + 9.0f * (float) i, true));
            require (strip->isDraggingSlot() && row.getY() > 30, "The row follows the mouse");
            row.mouseUp (at (startY + 72.0f, true));
            require (! strip->isDraggingSlot(), "Dropped");
            require (std::abs (c.getSlotMix (0, 0) - 22.0f) < 0.01f && std::abs (c.getSlotMix (0, 1) - 33.0f) < 0.01f && std::abs (c.getSlotMix (0, 2) - 11.0f) < 0.01f,
                     "Dragging the first effect to the bottom moves it there (the others move up)");
            juce::ignoreUnused (holder);
            // A short wobble is a click, not a drag.
            auto& top = strip->getRow (0);
            top.mouseDown (mouseAt (&top, x, 15.0f));
            top.mouseDrag (mouseAt (&top, x, 17.0f, true));
            require (! strip->isDraggingSlot(), "A small movement does not start a drag");
            top.mouseUp (mouseAt (&top, x, 17.0f, true));
            // Undo / redo buttons.
            c.commitHistory();
            editor->refreshNow();
            require (editor->getUndoButton().isEnabled() == c.canUndo(), "The undo arrow is lit when there is something to undo");
            require (c.moveSlotTo (0, 2, 0), "Move back with moveSlotTo");
            c.commitHistory();
            editor->undo();
            require (std::abs (c.getSlotMix (0, 2) - 11.0f) < 0.01f, "The undo arrow undoes the move");
            editor->refreshNow();
            require (editor->getRedoButton().isEnabled(), "The redo arrow is lit after an undo");
            editor->redo();
            require (std::abs (c.getSlotMix (0, 0) - 11.0f) < 0.01f, "The redo arrow redoes it");
            editor.reset();
            TriadProcessor::clearCopiedEffect();
            std::cout << "PASS: drag slots to reorder, undo / redo arrows" << std::endl;
        }

        // ---- Effects-only plug-in list, without ever loading a plug-in inside Triad ----------------
        {
            auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadCatalogTest");
            temp.deleteRecursively();
            auto folder = temp.getChildFile ("VST3");
            folder.createDirectory();
            auto withoutModuleInfo = [] (const juce::File& bundle)
            {
                for (auto name : { "Contents/Resources/moduleinfo.json", "Contents/moduleinfo.json" }) bundle.getChildFile (name).deleteFile();
            };
            require (copyBundle (effect, folder.getChildFile ("Triad Test Effect.vst3")), "Copy effect fixture");
            require (copyBundle (synth, folder.getChildFile ("Triad Test Synth.vst3")), "Copy instrument fixture");
            require (copyBundle (synth, folder.getChildFile ("Vendor/Hidden Synth.vst3")), "Copy instrument without metadata");
            require (copyBundle (effect, folder.getChildFile ("Vendor/Hidden Effect.vst3")), "Copy effect without metadata");
            withoutModuleInfo (folder.getChildFile ("Vendor/Hidden Synth.vst3"));
            withoutModuleInfo (folder.getChildFile ("Vendor/Hidden Effect.vst3"));
            require (copyBundle (crashFixture, folder.getChildFile ("Crashy Effect.vst3")), "Copy the plug-in that crashes when loaded");
            PluginCatalog::Options options;
            options.vst3Folders.add (folder);
            options.cacheFolder = temp.getChildFile ("cache");
            options.scanner = scannerHelper;
            options.includeAudioUnits = false;
            options.cacheFolder.createDirectory();
            // Files left by Triad 0.2.1's list are cleaned up.
            options.cacheFolder.getChildFile ("PluginCache.xml").replaceWithText ("<TriadPluginCache version=\"1\"/>");
            options.cacheFolder.getChildFile ("ScanInProgress.txt").replaceWithText ("x");
            std::cout << "moduleinfo.json in test effect: " << (effect.getChildFile ("Contents/Resources/moduleinfo.json").existsAsFile() ? "yes" : "no") << std::endl;
            EffectEntry byFile, scanned;
            {
                const auto started = juce::Time::getMillisecondCounterHiRes();
                PluginCatalog catalog (options);
                require (catalog.waitUntilIdle (120000), "Catalog scan finishes");
                const auto c = catalog.getCounts();
                std::cout << "Plug-in list: " << c.bundles << " bundles (" << c.fromModuleInfo << " from moduleinfo.json, " << c.fromScanner
                          << " checked by Triad Scanner, " << c.crashed << " crashed the scanner) in "
                          << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - started) << " ms" << std::endl;
                bool crashedListed = false;
                for (const auto& e : catalog.getEffects())
                {
                    std::cout << "  listed: " << e.format << " / " << e.manufacturer << " / " << e.name
                              << (e.verified ? "" : " [not checked]") << (e.crashed ? " [crashed]" : "") << std::endl;
                    require (! e.name.containsIgnoreCase ("Synth"), "Instruments are not listed");
                    if (e.name == "Triad Test Effect" && e.verified) (e.loadByFile ? byFile : scanned) = e;
                    if (e.name == "Crashy Effect") crashedListed = e.crashed && ! e.verified;
                }
                require (c.fromModuleInfo == 2 && c.fromScanner == 2 && c.crashed == 1, "Each plug-in classified the expected way");
                require (crashedListed, "The plug-in that crashes is marked and not offered");
                require (byFile.name.isNotEmpty() && scanned.name.isNotEmpty(), "Effects found from moduleinfo.json and by the scanner");
            }
            std::cout << "A plug-in that aborts when loaded only took down Triad Scanner; Triad kept running" << std::endl;
            require (! options.cacheFolder.getChildFile ("PluginCache.xml").exists() && ! options.cacheFolder.getChildFile ("ScanInProgress.txt").exists(),
                     "Old 0.2.1 list files removed");
            require (options.cacheFolder.getChildFile ("PluginCache-v2.xml").existsAsFile(), "Plug-in list is cached");
            {
                auto noHelper = options;   // the cached results need no helper
                noHelper.scanner = temp.getChildFile ("missing/Triad Scanner");
                PluginCatalog cached (noHelper);
                require (cached.waitUntilIdle (60000), "Cached catalog finishes");
                const auto c = cached.getCounts();
                int again = 0;
                for (const auto& e : cached.getEffects()) if (e.name == "Triad Test Effect" && e.verified) ++again;
                require (again == 2 && c.unchecked == 0 && c.crashed == 1, "Cached list matches");
            }
           #if ! JUCE_WINDOWS
            {
                setenv ("TRIAD_FIXTURE_HANG", "1", 1);   // the fixture now hangs instead of crashing
                auto hanging = options;
                hanging.timeoutMs = 2000;
                PluginCatalog catalog (hanging);
                catalog.rescanAll();
                require (catalog.waitUntilIdle (120000), "Scan with a hanging plug-in finishes");
                unsetenv ("TRIAD_FIXTURE_HANG");
                const auto c = catalog.getCounts();
                bool listed = false;
                for (const auto& e : catalog.getEffects()) if (e.name == "Crashy Effect") listed = ! e.verified && ! e.crashed;
                require (c.timedOut == 1 && listed, "A plug-in that hangs is given up on and listed as not checked");
                std::cout << "A plug-in that hangs when loaded was stopped after 2 s; Triad kept running" << std::endl;
            }
           #endif
            {
                auto damaged = options;   // no helper at all: unknown plug-ins are listed, never loaded
                damaged.scanner = temp.getChildFile ("missing/Triad Scanner");
                damaged.cacheFolder = temp.getChildFile ("cache-without-helper");
                PluginCatalog catalog (damaged);
                require (catalog.waitUntilIdle (60000), "Catalog without helper finishes");
                require (catalog.getCounts().unchecked == 3, "Without the helper, unknown plug-ins are listed as not checked");
            }
            TriadProcessor fromList;
            fromList.prepareToPlay (48000, 128);
            require (fromList.loadFile (0, 0, juce::File (byFile.description.fileOrIdentifier), false, byFile.name).isEmpty()
                     && fromList.getSlotInfo (0, 0).loaded, "Effect picked from the list loads");
            require (fromList.loadEffect (0, 1, scanned.description, false).isEmpty() && fromList.getSlotInfo (0, 1).loaded,
                     "Effect found by the scanner loads into a new slot");
            juce::PluginDescription instrument;
            instrument.name = "Some Synth"; instrument.isInstrument = true; instrument.pluginFormatName = "VST3";
            require (fromList.loadEffect (0, 2, instrument, false).isNotEmpty() && fromList.getSlotCount (0) == 2,
                     "Instruments are refused (no slot is left behind)");
           #if JUCE_MAC
            PluginCatalog::Options auOnly;
            auOnly.cacheFolder = temp.getChildFile ("au-cache");
            auOnly.scanner = scannerHelper;
            PluginCatalog audioUnits (auOnly);
            require (audioUnits.waitUntilIdle (120000), "AU list");
            bool appleDelay = false;
            for (const auto& e : audioUnits.getEffects())
            {
                require (! e.name.containsIgnoreCase ("DLSMusicDevice") && ! e.name.containsIgnoreCase ("MIDISynth"), "No AU instruments listed");
                if (e.format == "AU" && e.name == "AUDelay")
                {
                    appleDelay = true;
                    const int slot = fromList.getSlotCount (0);
                    require (fromList.loadEffect (0, slot, e.description, false).isEmpty(), "Host an Audio Unit effect");
                    for (int wait = 0; wait < 40 && ! fromList.getSlotInfo (0, slot).loaded; ++wait)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // AUv3 loads asynchronously
                    require (fromList.getSlotInfo (0, slot).loaded, "Audio Unit effect is running in its slot");
                    std::cout << "Hosted AU: " << e.description.fileOrIdentifier << ", latency " << fromList.getSlotInfo (0, slot).latency << std::endl;
                    fromList.removeSlot (0, slot);
                }
            }
            require (appleDelay, "Apple's AUDelay is listed as an effect");
           #endif
            temp.deleteRecursively();
            std::cout << "PASS: effect list excludes instruments, survives plug-ins that crash or hang, caches results" << std::endl;
        }

        // ---- Editor windows ---------------------------------------------------------------------
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        {
            restored.showEffect (1, 0); restored.closeWindows();
            require (restored.loadFile (1, 1, effect, true).isEmpty(), "Load opens the effect window straight away");
            restored.closeWindows();
            restored.removeEffect (1, 1);
            std::cout << "Hosted effect editor opened and closed (including auto-open on load)" << std::endl;
        }
        else
        {
            restored.showEffect (1, 0);
            require (restored.getStatus().contains ("display"), "Headless window request handled safely");
            std::cout << "SKIP: native effect window (no display server); safe headless handling passed" << std::endl;
        }
        for (int i = 0; i < 3; ++i) restored.removeEffect (i, 0);
        runAudioCase (restored, 48000, { 1, 1, 1 }, 0, "Remove effects, clear latency");

        // ---- The interface ------------------------------------------------------------------------
        {
            TriadProcessor preview;
            preview.prepareToPlay (48000, 512);
            require (preview.loadFile (0, 0, effect).isEmpty() && preview.loadFile (0, 1, effect).isEmpty()
                     && preview.loadFile (0, 2, effect).isEmpty() && preview.loadFile (1, 0, effect).isEmpty()
                     && preview.loadFile (2, 0, effect).isEmpty(), "Load effects for the preview");
            preview.addSlot (2);
            preview.setSlotMix (0, 1, 65);
            preview.setSlotMix (2, 0, 30);
            setParam (preview, TriadProcessor::bandMixID (1), 80);
            std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (preview.createEditor()));
            require (editor != nullptr, "Create editor");
            editor->setVisible (true);
            editor->refreshNow();
            require (editor->getNumStrips() == 3, "One strip per band");
            require (editor->getStrip (0)->getNumRows() == 3 && editor->getStrip (1)->getNumRows() == 1
                     && editor->getStrip (2)->getNumRows() == 2, "Each band shows its own slots");
            require (editor->getStrip (0)->getRow (1).knob.getValue() == 65.0, "Slot knob shows its mix");
            editor->getStrip (1)->getRow (0).knob.setValue (42.0, juce::sendNotificationSync);
            require (std::abs (preview.getSlotMix (1, 0) - 42.0f) < 0.01f, "Turning a slot knob sets its mix");
            preview.addSlot (1);
            editor->refreshNow();
            require (editor->getStrip (1)->getNumRows() == 2, "A new slot row appears under the last one");
            preview.removeSlot (1, 1);
            editor->refreshNow();
            require (editor->getStrip (1)->getNumRows() == 1, "A removed slot row disappears");
            preview.setSlotMix (1, 0, 100);
            require (editor->getStrip (1)->getMixKnob().getValue() == 80.0, "Band knob shows its mix");
            // Drag band 1's header onto band 3: its three effects move there.
            {
                auto* first = editor->getStrip (0);
                auto* holder = first->getParentComponent();
                const float step = (float) (first->getWidth() + 10);
                const float y = 12.0f, x = (float) first->getX() + 40.0f;
                editor->bandDrag (0, mouseAt (holder, x, y), 0);
                for (int i = 1; i <= 10; ++i) editor->bandDrag (0, mouseAt (holder, x + step * 0.2f * (float) i, y, true), 1);
                editor->bandDrag (0, mouseAt (holder, x + step * 2.0f, y, true), 2);
                require (preview.getSlotCount (2) == 3 && preview.getSlotCount (0) == 1, "Dragging a band moves its effects");
                require (preview.moveBand (2, 0), "And back");
                editor->refreshNow();
            }
            // Feed music-like noise so the live spectrum has something to draw.
            juce::Random random (7);
            juce::MidiBuffer midi;
            for (int block = 0; block < 12; ++block)
            {
                juce::AudioBuffer<float> audio (2, 512);
                for (int s = 0; s < 512; ++s)
                {
                    const float pink = (random.nextFloat() - 0.5f) * 0.3f + 0.2f * std::sin ((float) (block * 512 + s) * 0.01f);
                    audio.setSample (0, s, pink); audio.setSample (1, s, pink);
                }
                preview.processBlock (audio, midi);
            }
            editor->getDisplay().updateSpectrum();
            require (editor->getDisplay().isSpectrumVisible(), "Live spectrum shows incoming audio");
            if (screenshotFile != juce::File())
            {
                std::cout << "Screenshot: " << writeImage (screenshotFile, editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                preview.addBand (1000.0f);
                preview.addBand (6000.0f);
                editor->refreshNow();
                require (editor->getNumStrips() == 5, "Strips follow added bands");
                editor->getDisplay().updateSpectrum();
                std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-5bands.png"),
                                                           editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                // Soft and hard slopes, the first one being dragged (its two bands highlighted).
                preview.setCrossoverSlope (0, 1); preview.setCrossoverSlope (1, 2); preview.setCrossoverSlope (2, 6); preview.setCrossoverSlope (3, triad::CrossoverFilter::maxOrder);
                editor->getDisplay().beginSlopeGesture (0);
                editor->getDisplay().updateSpectrum();
                std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-slopes.png"),
                                                           editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                editor->getDisplay().endSlopeGesture();
                for (int j = 0; j < 4; ++j) preview.setCrossoverSlope (j, 4);
                for (int s = 0; s < 11; ++s) preview.addSlot (1);
                editor->refreshNow();
                std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-slots.png"),
                                                           editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                while (preview.addBand()) {}
                editor->refreshNow();
                require (editor->getNumStrips() == 10, "Ten strips");
                std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-10bands.png"),
                                                           editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
            }
            // The LFO tab: the slot's ~ opens its LFO; double-click adds a point.
            {
                namespace mod = triad::mod;
                editor->showLfo (0, 1);
                editor->refreshNow();
                const int lfoIndex = preview.getSlotLfo (0, 1);
                auto& page = editor->getLfoPage();
                require (editor->getTab() == TriadEditor::Tab::lfo && page.getBand() == 0 && page.getLfo() == lfoIndex, "The slot's LFO opens");
                auto& shapeEditor = page.getEditor();
                const auto plot = shapeEditor.plot();
                const auto before = preview.getLfoInfo (0, lfoIndex).shape.size();
                shapeEditor.mouseDoubleClick (mouseAt (&shapeEditor, plot.getX() + plot.getWidth() * 0.5f, plot.getY() + plot.getHeight() * 0.75f));
                const auto after = preview.getLfoInfo (0, lfoIndex).shape;
                require (after.size() == before + 1 && preview.getLfoInfo (0, lfoIndex).active, "Double-click adds a point and switches the LFO on");
                require (std::abs (mod::evaluate (after, 0.5f) - 0.25f) < 0.01f, "The point lands on the grid where clicked");
                // Drag it up to 75%.
                const auto point = shapeEditor.toScreen ({ 0.5f, 0.25f, 0.0f });
                shapeEditor.mouseDown (mouseAt (&shapeEditor, point.x, point.y));
                shapeEditor.mouseDrag (mouseAt (&shapeEditor, point.x, point.y - plot.getHeight() * 0.5f, true));
                shapeEditor.mouseUp (mouseAt (&shapeEditor, point.x, point.y - plot.getHeight() * 0.5f));
                require (std::abs (mod::evaluate (preview.getLfoInfo (0, lfoIndex).shape, 0.5f) - 0.75f) < 0.01f, "Dragging a point moves it");
                shapeEditor.apply ([] (const mod::Shape& s) { return mod::repeated (s, 2); });
                require (preview.getLfoInfo (0, lfoIndex).shape.size() >= 5, "2x from the toolbar");
                // The grid on screen has the proportions the presets and the generator draw their angles for.
                require (std::abs (shapeEditor.plotAspect() / mod::plotAspect - 1.0f) < 0.08f, "The LFO grid's proportions match the angles (30/45/60 degrees look right)");
                // The generator: the dice draws a new shape in the sliders' style; Undo brings the old one back.
                {
                    const auto shapeBefore = preview.getLfoInfo (0, lfoIndex).shape;
                    page.setGeneratorSeed (2);
                    page.setGeneratorStyle (1.0f, 0.0f, 0.35f);
                    page.getGenerateButton().onClick();
                    const auto made = preview.getLfoInfo (0, lfoIndex).shape;
                    require (! (made == shapeBefore) && generatorTests::validShape (made), "The dice draws a new shape");
                    require (generatorTests::look (made).curves == 0, "Angular 100% draws hard angles only");
                    require (preview.getLfoInfo (0, lfoIndex).active, "A generated shape switches the LFO on");
                    require (preview.undoLfo (0, lfoIndex) && preview.getLfoInfo (0, lfoIndex).shape == shapeBefore, "Undo brings the shape from before the dice back");
                    require (preview.redoLfo (0, lfoIndex) && preview.getLfoInfo (0, lfoIndex).shape == made, "Redo brings the generated shape back");
                    std::set<juce::String> drawn;
                    page.setGeneratorStyle (1.0f, 0.0f, 0.0f);
                    for (int i = 0; i < 12; ++i)
                    {
                        page.generate();
                        drawn.insert (mod::toText (preview.getLfoInfo (0, lfoIndex).shape));
                    }
                    require (drawn.size() == 12, "The dice never repeats a recent shape, even at 0% chaos");
                    page.setGeneratorStyle (0.0f, 1.0f, 0.5f);
                    page.generate();
                    require (generatorTests::look (preview.getLfoInfo (0, lfoIndex).shape).steps == 0, "Soft 100% draws curves only");
                }
                // The preset palette: a category at a time, a page at a time; a click replaces the shape.
                int generatedFolders = 0;
                for (int c = 0; c < mod::numPresetCategories(); ++c)
                {
                    page.setPaletteCategory (c);
                    const auto list = mod::presetsIn (c);
                    const int pages = ((int) list.size() + LfoPage::paletteSize - 1) / LfoPage::paletteSize;
                    require (page.getPalettePage() == 0 && page.getNumPalettePages() == pages, "A folder opens on page 1, with every page");
                    generatedFolders += mod::isGeneratedCategory (c);
                    if (mod::isGeneratedCategory (c)) require (pages >= 3, "Generated folders have several pages");
                    std::set<int> seen;
                    for (int pg = 0; pg < pages; ++pg)
                    {
                        page.setPalettePage (pg);
                        const int expected = juce::jmin (LfoPage::paletteSize, (int) list.size() - pg * LfoPage::paletteSize);
                        require (page.getPalettePage() == pg && page.getNumPaletteButtons() == expected, "The palette shows the chosen page");
                        auto* last = page.getPaletteButton (expected - 1);
                        require (last != nullptr && last->getRight() <= page.getEditor().getRight() + 1, "The palette row fits beside the grid");
                        last->onClick();
                        const int presetIndex = list[(size_t) (pg * LfoPage::paletteSize + expected - 1)];
                        require (preview.getLfoInfo (0, lfoIndex).shape == mod::preset (presetIndex), "A palette button replaces the shape");
                        for (int k = 0; k < expected; ++k) seen.insert (page.getPaletteButton (k)->getName().hashCode());
                    }
                    require ((int) seen.size() == (int) list.size(), "Turning the pages shows every shape in the folder once");
                    page.setPalettePage (pages + 3);
                    require (page.getPalettePage() == pages - 1, "Pages stop at the last one");
                }
                require (generatedFolders >= 5, "The palette has the generated folders");
                // USER: SAVE keeps the LFO's shape in the USER folder; rename, delete, pages.
                {
                    std::set<juce::String> existing;
                    for (const auto& e : triad::userShapes::load()) existing.insert (e.file.getFullPathName());
                    auto mine = [&existing]
                    {
                        std::vector<triad::userShapes::Entry> list;
                        for (auto& e : triad::userShapes::load()) if (existing.count (e.file.getFullPathName()) == 0) list.push_back (e);
                        return list;
                    };
                    page.getEditor().apply ([] (const mod::Shape&) { return mod::preset (5); });
                    const auto savedShape = preview.getLfoInfo (0, lfoIndex).shape;
                    const auto file = page.saveShapeToUser ("Triad test shape");
                    require (file.existsAsFile() && file.getFileNameWithoutExtension() == "Triad test shape", "SAVE writes the shape to the USER folder");
                    require (page.getPaletteCategory() == LfoPage::userFolder, "After saving, the palette shows USER");
                    bool shown = false;
                    for (int k = 0; k < page.getNumPaletteButtons(); ++k) shown = shown || page.getPaletteButton (k)->getName() == "Triad test shape";
                    require (shown, "The savedShape shape is on the USER page shown");
                    const auto second = page.saveShapeToUser ("Triad test shape");
                    require (second.getFileNameWithoutExtension() == "Triad test shape 2", "Saving under a taken name keeps both");
                    require (mine().size() == 2 && mine()[0].shape == savedShape, "USER shapes come back exactly as savedShape");
                    page.getEditor().apply ([] (const mod::Shape&) { return mod::flat (0.5f); });
                    for (int k = 0; k < page.getNumPaletteButtons(); ++k)
                        if (page.getPaletteButton (k)->getName() == "Triad test shape") page.getPaletteButton (k)->onClick();
                    require (preview.getLfoInfo (0, lfoIndex).shape == savedShape, "A USER shape replaces the LFO's shape");
                    require (triad::userShapes::rename (second, "Triad test renamed") && ! second.exists(), "Rename a USER shape");
                    for (const auto& e : mine()) require (triad::userShapes::remove (e.file), "Delete a USER shape");
                    require (mine().empty(), "Deleted USER shapes are gone");
                    page.setPaletteCategory (0);
                    page.setPaletteCategory (LfoPage::userFolder);
                    require (page.getPaletteCategory() == LfoPage::userFolder && page.getNumPaletteButtons() == (int) existing.size()
                             && page.getNumPalettePages() == juce::jmax (1, ((int) existing.size() + LfoPage::paletteSize - 1) / LfoPage::paletteSize),
                             "The USER folder lists the savedShape shapes, a page at a time");
                }
                // The mouse wheel over the palette turns the pages.
                for (int c = 0; c < mod::numPresetCategories(); ++c)
                    if (mod::isGeneratedCategory (c)) { page.setPaletteCategory (c); break; }
                {
                    auto* first = page.getPaletteButton (0);
                    juce::MouseWheelDetails wheel {};
                    wheel.deltaY = -0.2f;
                    const auto at = first->getBounds().getCentre().toFloat();
                    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                  &page, &page, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 0, false);
                    page.mouseWheelMove (event, wheel);
                    require (page.getPalettePage() == 1, "Mouse wheel down: next page");
                    wheel.deltaY = 0.2f;
                    page.mouseWheelMove (event, wheel);
                    require (page.getPalettePage() == 0, "Mouse wheel up: previous page");
                }
                // The screenshot shows a generated folder's second page.
                for (int c = 0; c < mod::numPresetCategories(); ++c)
                    if (mod::presetCategoryName (c) == "GEN: ANGLES") page.setPaletteCategory (c);
                page.setPalettePage (1);
                // A generated shape for the screenshot: hard angles into curves.
                page.setGeneratorSeed (132);
                page.setGeneratorStyle (1.0f, 0.0f, 0.35f);
                page.generate();
                editor->refreshNow();
                if (screenshotFile != juce::File())
                    std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-generator.png"),
                                                               editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                page.setPaletteCategory (0);
                preview.setLfoShape (0, lfoIndex, mod::preset (7), true);
                preview.setLfoShape (1, preview.getSlotLfo (1, 0), mod::preset (4), true);
                juce::MidiBuffer more;
                for (int block = 0; block < 4; ++block) { juce::AudioBuffer<float> audio (2, 512); audio.clear(); preview.processBlock (audio, more); }
                editor->showLfo (0, 1);
                editor->refreshNow();
                if (screenshotFile != juce::File())
                    std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-lfo.png"),
                                                               editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                editor->setTab (TriadEditor::Tab::matrix);
                editor->refreshNow();
                require (editor->getMatrixPage().getNumPanels() == preview.getBandCount(), "A matrix per band");
                require (editor->getMatrixPage().getPanel (0)->getNumRows() == preview.getRouteCount (0), "A row per route");
                if (screenshotFile != juce::File())
                    std::cout << "Screenshot: " << writeImage (screenshotFile.getSiblingFile ("Triad-preview-matrix.png"),
                                                               editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f)) << std::endl;
                // ---- Visual check: how long each part of the window takes to draw (1x, and 2x as on a Retina
                // screen), and close-ups of the knobs, sliders and dots, to judge how smooth the interface can be.
                if (screenshotFile != juce::File())
                {
                    auto timeArea = [] (juce::Component& c, juce::Rectangle<int> area, float scale)
                    {
                        double total = 0.0, worst = 0.0;
                        c.createComponentSnapshot (area, true, scale);   // warm-up (fonts, cached pictures)
                        for (int i = 0; i < 12; ++i)
                        {
                            const auto t0 = juce::Time::getMillisecondCounterHiRes();
                            c.createComponentSnapshot (area, true, scale);
                            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
                            total += dt; worst = juce::jmax (worst, dt);
                        }
                        return juce::String (total / 12.0, 1) + " ms (worst " + juce::String (worst, 1) + ")";
                    };
                    auto timePaint = [&timeArea] (juce::Component& c, float scale) { return timeArea (c, c.getLocalBounds(), scale); };
                    std::cout << "UI drawing times, whole window at 1x / 2x (a frame at 60 fps has 16.7 ms):" << std::endl;
                    for (auto tab : { TriadEditor::Tab::bands, TriadEditor::Tab::lfo, TriadEditor::Tab::matrix })
                    {
                        editor->setTab (tab);
                        editor->refreshNow();
                        std::cout << "  " << (tab == TriadEditor::Tab::bands ? "BANDS" : tab == TriadEditor::Tab::lfo ? "LFO" : "MATRIX")
                                  << ": " << timePaint (*editor, 1.0f) << " / " << timePaint (*editor, 2.0f) << std::endl;
                    }
                    editor->setTab (TriadEditor::Tab::bands);
                    editor->refreshNow();
                    std::cout << "What moves every frame (up to 60 a second), at 1x / 2x:" << std::endl;
                    auto& display = editor->getDisplay();
                    display.createComponentSnapshot (display.getLocalBounds(), true, 2.0f);
                    const int displayRenders = display.getStaticLayerRenders();
                    std::cout << "  Crossover display (spectrum and meters over its cached picture): " << timePaint (display, 1.0f) << " / "
                              << timePaint (display, 2.0f) << std::endl;
                    juce::ignoreUnused (displayRenders);
                    {
                        const int before = display.getStaticLayerRenders();
                        for (int i = 0; i < 5; ++i) { display.updateSpectrum(); display.createComponentSnapshot (display.getLocalBounds(), true, 2.0f); }
                        require (display.getStaticLayerRenders() == before, "The display's still parts are drawn once, not every frame");
                    }
                    auto& strip0 = *editor->getStrip (0);
                    std::cout << "  A band's level meter: " << timeArea (strip0, strip0.meterArea().expanded (4).getUnion (strip0.peakTextArea()), 1.0f) << " / "
                              << timeArea (strip0, strip0.meterArea().expanded (4).getUnion (strip0.peakTextArea()), 2.0f) << std::endl;
                    std::cout << "  A slot knob's LFO dot: " << timeArea (strip0.getRow (0), strip0.getRow (0).knob.getBounds().expanded (6), 1.0f) << " / "
                              << timeArea (strip0.getRow (0), strip0.getRow (0).knob.getBounds().expanded (6), 2.0f) << std::endl;
                    editor->showLfo (0, 1);
                    editor->refreshNow();
                    auto& grid = editor->getLfoPage().getEditor();
                    grid.createComponentSnapshot (grid.getLocalBounds(), true, 2.0f);
                    const int gridRenders = grid.getLayerRenders();
                    const auto playhead = juce::Rectangle<int> (grid.getWidth() / 2 - 6, 0, 12, grid.getHeight());
                    std::cout << "  LFO grid, the playing position (a 12-pixel strip over the cached grid): " << timeArea (grid, playhead, 1.0f) << " / "
                              << timeArea (grid, playhead, 2.0f) << "; the whole grid: " << timePaint (grid, 2.0f) << " at 2x" << std::endl;
                    juce::ignoreUnused (gridRenders);
                    {
                        const int before = grid.getLayerRenders();
                        for (int i = 0; i < 5; ++i) grid.createComponentSnapshot (playhead.translated (i * 20, 0), true, 2.0f);
                        require (grid.getLayerRenders() == before, "The LFO grid is drawn once; following the playhead only redraws a strip");
                    }
                    // Close-ups at 4x: a strip's footer (volume slider, meter, band mix knob), slot rows, the
                    // crossover dots and readouts, the LFO toolbar and palette.
                    editor->setTab (TriadEditor::Tab::bands);
                    editor->refreshNow();
                    auto closeUp = [&] (juce::Component& c, juce::Rectangle<int> area, const char* name)
                    {
                        std::cout << "Close-up: " << writeImage (screenshotFile.getSiblingFile (juce::String ("Triad-closeup-") + name + ".png"),
                                                                c.createComponentSnapshot (area, true, 4.0f)) << std::endl;
                    };
                    auto& strip = *editor->getStrip (0);
                    closeUp (strip, strip.getLocalBounds().removeFromBottom (96), "strip-footer");
                    closeUp (strip, strip.getLocalBounds().withTrimmedTop (BandStrip::headerHeight).withHeight (110), "slots");
                    closeUp (display, display.getLocalBounds().withTrimmedLeft (display.getWidth() / 3).withWidth (display.getWidth() / 3), "crossovers");
                    editor->showLfo (0, 1);
                    editor->refreshNow();
                    auto& page = editor->getLfoPage();
                    closeUp (page, page.getLocalBounds().removeFromBottom (78).withWidth (page.getEditor().getRight()), "lfo-toolbar");
                    editor->setTab (TriadEditor::Tab::bands);
                    editor->refreshNow();
                    // GUI stress: 400 random interface actions (tabs, window sizes, bands, slopes, LIVE, LFO folders
                    // and pages, the dice), drawing the whole window every few actions, while frames keep running.
                    {
                        juce::Random r (99);
                        int snapshots = 0;
                        double drawing = 0.0;
                        const auto t0 = juce::Time::getMillisecondCounterHiRes();
                        double frameTime = 1000.0;
                        for (int i = 0; i < 400; ++i)
                        {
                            switch (r.nextInt (10))
                            {
                                case 0: editor->setTab ((TriadEditor::Tab) r.nextInt (3)); break;
                                case 1: { const double scales[] { 0.75, 0.9, 1.0, 1.25, 1.5 }; editor->setScale (scales[r.nextInt (5)]); break; }
                                case 2: if (preview.getBandCount() < triad::maxBands) preview.addBand (40.0f * std::pow (400.0f, r.nextFloat())); break;
                                case 3: if (preview.getBandCount() > 3) preview.removeBand (r.nextInt (preview.getBandCount())); break;
                                case 4: preview.setCrossoverSlope (r.nextInt (preview.getBandCount() - 1), 1 + r.nextInt (triad::CrossoverFilter::maxOrder)); break;
                                case 5: preview.setLiveMode (! preview.isLiveMode()); break;
                                case 6:
                                {
                                    editor->showLfo (r.nextInt (preview.getBandCount()), -1);
                                    auto& lfoPage = editor->getLfoPage();
                                    lfoPage.setPaletteCategory (r.nextInt (mod::numPresetCategories()));
                                    lfoPage.setPalettePage (r.nextInt (lfoPage.getNumPalettePages()));
                                    if (lfoPage.getNumPaletteButtons() > 0) lfoPage.getPaletteButton (r.nextInt (lfoPage.getNumPaletteButtons()))->onClick();
                                    break;
                                }
                                case 7: editor->getLfoPage().generate(); break;
                                case 8: editor->getDisplay().setSlope (r.nextInt (preview.getBandCount() - 1), 1 + r.nextInt (triad::CrossoverFilter::maxOrder)); break;
                                default: { juce::MidiBuffer m; juce::AudioBuffer<float> audio (2, 512); fillTestNoise (audio, r); preview.processBlock (audio, m); break; }
                            }
                            editor->refreshNow();
                            editor->animate (frameTime += 1.0 / 60.0);
                            if (i % 20 == 19)
                            {
                                const auto d0 = juce::Time::getMillisecondCounterHiRes();
                                editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                                drawing += juce::Time::getMillisecondCounterHiRes() - d0;
                                ++snapshots;
                            }
                            require (editor->getNumStrips() == preview.getBandCount(), "GUI stress: a strip per band");
                        }
                        preview.setLiveMode (false);
                        editor->setScale (1.0);
                        std::cout << "GUI stress: 400 random interface actions in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1)
                                  << " s; the whole window drawn " << snapshots << " times, " << juce::String (drawing / juce::jmax (1, snapshots), 1)
                                  << " ms each: passed" << std::endl;
                    }
                }
                editor->setTab (TriadEditor::Tab::bands);
                editor->refreshNow();
            }
            editor.reset();
        }
      }
        // As in a DAW, the message loop keeps running after the Triads close: removed effects are
        // deleted from it; then the idle copies Triad keeps are released before JUCE shuts down.
        // When the last Triad closes, the effects it kept (removed ones, idle copies) are deleted at once,
        // while Triad's framework still runs (a host may unload Triad straight after).
        require (TriadProcessor::getIdleEffectCount() == 0 && SidechainEffect::live().empty(),
                 "Closing the last Triad deletes the effects it was keeping");
        std::cout << "Closing the last Triad deletes every effect it was keeping: passed" << std::endl;
        std::cout << "PASS: all integration tests" << std::endl;
        testStage = "shutting JUCE down, after all tests passed";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
