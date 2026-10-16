#include "../Source/PluginProcessor.h"
#include "../Source/PluginCatalog.h"
#include "../Source/PluginEditor.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void require (bool b, const char* message) { if (! b) throw std::runtime_error (message); }
#include "CrossoverTests.h"

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
                                    const std::vector<float>& cuts = threeBandCuts)
{
    triad::Splitter splitter;
    splitter.prepare (sr, in.getNumSamples());
    std::array<float, triad::maxCrossovers> c {};
    for (size_t i = 0; i < cuts.size(); ++i) c[i] = cuts[i];
    splitter.setBandCount ((int) gains.size(), c.data());
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
void runAudioCase (TriadProcessor& p, double sr, const std::vector<float>& gains, int latency, const char* name,
                   const std::vector<float>& cuts = threeBandCuts)
{
    p.prepareToPlay (sr, 128);
    auto signal = input (8192);
    auto expected = reference (signal, sr, gains, latency, cuts);
    juce::MidiBuffer midi;
    p.processBlock (signal, midi);   // deliberately larger than the prepared block size
    compare (signal, expected, name);
    require (p.getLatencySamples() == latency, "Host latency reporting");
}
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
juce::String writeImage (const juce::File& file, const juce::Image& image)
{
    juce::FileOutputStream stream (file);
    require (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk(), "Create fresh screenshot");
    juce::PNGImageFormat png;
    require (png.writeImageToStream (image, stream), "Write UI screenshot");
    return file.getFullPathName();
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        require (argc >= 5, "Usage: TriadTests <Test Effect.vst3> <Test Synth.vst3> <Crash Fixture.vst3> <Triad Scanner> [screenshot.png] [Triad.vst3]");
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        const juce::File synth (juce::String::fromUTF8 (argv[2]));
        const juce::File crashFixture (juce::String::fromUTF8 (argv[3]));
        const juce::File scannerHelper (juce::String::fromUTF8 (argv[4]));
        const juce::File screenshotFile = argc >= 6 ? juce::File (juce::String::fromUTF8 (argv[5])) : juce::File();
        const juce::File triadVst3 = argc >= 7 ? juce::File (juce::String::fromUTF8 (argv[6])) : juce::File();
        require (crashFixture.exists() && scannerHelper.existsAsFile(), "Crash fixture and scanner helper are built");
        runCrossoverTests();

        // ---- Built-in Linkwitz-Riley filter equals JUCE's -----------------------------------
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const auto signal = input (8192);
            triad::LinkwitzRiley mine, minePhase;
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
            std::cout << "Built-in LR4 filter vs JUCE dsp filter at " << sr << " Hz: max error " << error << std::endl;
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

        // ---- Per-slot mix (FL-style slot knobs) ----------------------------------------------
        for (int b = 0; b < 3; ++b) p.setSlotMix (b, 0, 50);
        require (p.getParameters().size() == triad::maxCrossovers + triad::maxBands, "Slot mixes are not DAW parameters");
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
        require (xml->getIntAttribute ("version") == 3 && xml->getIntAttribute ("bands") == 3, "State version 3 with band count");
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
            editor.reset();
        }
        std::cout << "PASS: all integration tests" << std::endl;
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
