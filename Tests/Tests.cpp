#include "../Source/PluginProcessor.h"
#include "../Source/PluginCatalog.h"
#include "../Source/PluginEditor.h"
#include <juce_dsp/juce_dsp.h>
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
        require (argc >= 3, "Usage: TriadTests <Test Effect.vst3> <Test Synth.vst3> [screenshot.png] [Triad.vst3]");
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        const juce::File synth (juce::String::fromUTF8 (argv[2]));
        const juce::File screenshotFile = argc >= 4 ? juce::File (juce::String::fromUTF8 (argv[3])) : juce::File();
        const juce::File triadVst3 = argc >= 5 ? juce::File (juce::String::fromUTF8 (argv[4])) : juce::File();
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
        for (int b = 0; b < 3; ++b) setParam (p, TriadProcessor::slotMixID (b, 0), 50);
        runAudioCase (p, 48000, { .625f, .625f, .625f }, 73, "50 percent slot mix");
        bandMix (p, 50);
        runAudioCase (p, 48000, { .8125f, .8125f, .8125f }, 73, "Slot mix and band mix together");
        bandMix (p, 100);
        for (int b = 0; b < 3; ++b) setParam (p, TriadProcessor::slotMixID (b, 0), 100);

        // ---- Chains: several effects on one band, in order -----------------------------------
        require (p.loadFile (0, 1, effect).isEmpty() && p.loadFile (0, 2, effect).isEmpty(), "Load a three-effect chain");
        runAudioCase (p, 48000, { .015625f, .25f, .25f }, 219, "Three-effect chain on one band, other bands aligned");
        p.setSlotEnabled (0, 2, false);
        require (! p.getSlotInfo (0, 2).enabled, "Slot light switches off");
        runAudioCase (p, 48000, { .0625f, .25f, .25f }, 146, "Bypassed slot leaves the chain and its latency");
        p.setSlotEnabled (0, 2, true);
        setParam (p, TriadProcessor::slotMixID (0, 1), 0);
        runAudioCase (p, 48000, { .0625f, .25f, .25f }, 219, "Slot at 0 percent passes its input (latency kept)");
        require (p.moveSlot (0, 1, 1), "Move slot down");
        require (getParam (p, TriadProcessor::slotMixID (0, 2)) == 0.0f && getParam (p, TriadProcessor::slotMixID (0, 1)) == 100.0f,
                 "Slot mix travels with the moved effect");
        require (p.getSlotInfo (0, 1).loaded && p.getSlotInfo (0, 2).loaded, "Both moved slots keep their effects");
        runAudioCase (p, 48000, { .0625f, .25f, .25f }, 219, "Moving a slot keeps the chain's result");
        setParam (p, TriadProcessor::slotMixID (0, 2), 100);
        p.removeEffect (0, 1); p.removeEffect (0, 2);
        require (! p.getSlotInfo (0, 1).hasContent && ! p.getSlotInfo (0, 2).hasContent, "Remove clears slots");
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
        require (xml->getIntAttribute ("version") == 2 && xml->getIntAttribute ("bands") == 3, "State version 2 with band count");
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
        for (int i = 0; i < 3; ++i) require (restored.getSlotInfo (i, 0).loaded, "Restore child plugin");
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
            for (int s = 0; s < triad::maxSlots; ++s) require (q.loadFile (9, s, effect).isEmpty(), "Fill all ten slots of a band");
            const auto ten = q.effectiveCrossovers();
            const auto chain = std::pow (0.25f, 10.0f);
            std::vector<float> gains (10, 1.0f); gains[9] = chain;
            runAudioCase (q, 48000, gains, 730, "Ten bands with a ten-effect chain", ten);
            juce::MemoryBlock tenState; q.getStateInformation (tenState);
            TriadProcessor tenRestored; tenRestored.setStateInformation (tenState.getData(), (int) tenState.getSize());
            require (tenRestored.getBandCount() == 10 && tenRestored.getSlotInfo (9, 9).loaded, "Ten bands and slots recall");
            runAudioCase (tenRestored, 48000, gains, 730, "Ten-band project recall", ten);
        }

        // ---- Effects-only plug-in list ------------------------------------------------------------
        {
            auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TriadCatalogTest");
            temp.deleteRecursively();
            auto folder = temp.getChildFile ("VST3");
            folder.createDirectory();
            require (effect.copyDirectoryTo (folder.getChildFile ("Triad Test Effect.vst3")), "Copy effect fixture");
            require (synth.copyDirectoryTo (folder.getChildFile ("Triad Test Synth.vst3")), "Copy instrument fixture");
            require (synth.copyDirectoryTo (folder.getChildFile ("Vendor/Hidden Synth.vst3")), "Copy instrument without metadata");
            for (auto name : { "Contents/Resources/moduleinfo.json", "Contents/moduleinfo.json" })
                folder.getChildFile ("Vendor/Hidden Synth.vst3").getChildFile (name).deleteFile();
            require (effect.copyDirectoryTo (folder.getChildFile ("Crashy Effect.vst3")), "Copy crash-marker fixture");
            PluginCatalog::Options options;
            options.vst3Folders.add (folder);
            options.cacheFolder = temp.getChildFile ("cache");
            options.includeAudioUnits = false;
            options.cacheFolder.createDirectory();
            options.cacheFolder.getChildFile ("ScanInProgress.txt").replaceWithText (folder.getChildFile ("Crashy Effect.vst3").getFullPathName());
            std::cout << "moduleinfo.json in test effect: " << (effect.getChildFile ("Contents/Resources/moduleinfo.json").existsAsFile() ? "yes" : "no") << std::endl;
            int effects = 0;
            juce::PluginDescription catalogEntry;
            {
                PluginCatalog catalog (options);
                require (catalog.scanUntilIdle (60000), "Catalog scan finishes");
                for (const auto& e : catalog.getEffects())
                {
                    std::cout << "  listed: " << e.format << " / " << e.manufacturer << " / " << e.name << std::endl;
                    require (! e.name.containsIgnoreCase ("Synth"), "Instruments are not listed");
                    if (e.name == "Triad Test Effect") { ++effects; catalogEntry = e.description; }
                }
            }
            require (effects == 1, "Effect listed once; the plug-in that crashed during a scan is skipped");
            require (options.cacheFolder.getChildFile ("PluginCache.xml").existsAsFile(), "Plug-in list is cached");
            {
                PluginCatalog cached (options);
                require (cached.scanUntilIdle (60000), "Cached catalog finishes");
                int again = 0;
                for (const auto& e : cached.getEffects()) if (e.name == "Triad Test Effect") ++again;
                require (again == 1, "Cached list matches");
            }
            TriadProcessor fromList;
            fromList.prepareToPlay (48000, 128);
            require (fromList.loadEffect (0, 0, catalogEntry, false).isEmpty() && fromList.getSlotInfo (0, 0).loaded, "Effect picked from the list loads");
            juce::PluginDescription instrument;
            instrument.name = "Some Synth"; instrument.isInstrument = true; instrument.pluginFormatName = "VST3";
            require (fromList.loadEffect (0, 1, instrument, false).isNotEmpty(), "Instruments are refused");
           #if JUCE_MAC
            PluginCatalog::Options auOnly;
            auOnly.cacheFolder = temp.getChildFile ("au-cache");
            PluginCatalog audioUnits (auOnly);
            require (audioUnits.scanUntilIdle (60000), "AU list");
            bool appleDelay = false;
            for (const auto& e : audioUnits.getEffects())
            {
                require (! e.name.containsIgnoreCase ("DLSMusicDevice") && ! e.name.containsIgnoreCase ("MIDISynth"), "No AU instruments listed");
                if (e.format == "AU" && e.name == "AUDelay")
                {
                    appleDelay = true;
                    require (fromList.loadEffect (0, 2, e.description, false).isEmpty(), "Host an Audio Unit effect");
                    for (int wait = 0; wait < 40 && ! fromList.getSlotInfo (0, 2).loaded; ++wait)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // AUv3 loads asynchronously
                    require (fromList.getSlotInfo (0, 2).loaded, "Audio Unit effect is running in its slot");
                    std::cout << "Hosted AU: " << e.description.fileOrIdentifier << ", latency " << fromList.getSlotInfo (0, 2).latency << std::endl;
                }
            }
            require (appleDelay, "Apple's AUDelay is listed as an effect");
            fromList.removeEffect (0, 2);
           #endif
            temp.deleteRecursively();
            std::cout << "PASS: effect list excludes instruments, skips crashed plug-ins, caches results" << std::endl;
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
                     && preview.loadFile (1, 0, effect).isEmpty() && preview.loadFile (2, 0, effect).isEmpty(), "Load effects for the preview");
            setParam (preview, TriadProcessor::slotMixID (0, 1), 65);
            setParam (preview, TriadProcessor::slotMixID (2, 0), 30);
            setParam (preview, TriadProcessor::bandMixID (1), 80);
            std::unique_ptr<TriadEditor> editor (dynamic_cast<TriadEditor*> (preview.createEditor()));
            require (editor != nullptr, "Create editor");
            editor->setVisible (true);
            editor->refreshNow();
            require (editor->getNumStrips() == 3, "One strip per band");
            require (editor->getStrip (0)->getRow (1).knob.getValue() == 65.0, "Slot knob shows its mix");
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
