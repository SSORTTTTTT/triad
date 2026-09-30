#include "../Source/PluginProcessor.h"
#include <iostream>
#include <stdexcept>

void require (bool b, const char* message) { if (! b) throw std::runtime_error (message); }
#include "CrossoverTests.h"

void mix (TriadProcessor& p, float percent)
{
    for (int i = 0; i < 3; ++i)
    {
        auto* parameter = p.parameters.getParameter ("mix" + juce::String (i));
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (percent));
    }
}
juce::AudioBuffer<float> input (int n)
{
    juce::AudioBuffer<float> b (2, n);
    juce::Random random (12345);
    for (int s = 0; s < n; ++s)
        for (int c = 0; c < 2; ++c) b.setSample (c, s, random.nextFloat() * 0.5f - 0.25f);
    return b;
}
juce::AudioBuffer<float> reference (const juce::AudioBuffer<float>& in, double sr, std::array<float, 3> gains, int delay)
{
    triad::Splitter splitter; splitter.prepare (sr, in.getNumSamples());
    std::array<juce::AudioBuffer<float>, 3> bands;
    for (auto& b : bands) b.setSize (2, in.getNumSamples());
    splitter.process (in, 0, in.getNumSamples(), bands);
    juce::AudioBuffer<float> out (2, in.getNumSamples()); out.clear();
    for (int s = delay; s < in.getNumSamples(); ++s)
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < 3; ++i) out.addSample (c, s, gains[i] * bands[i].getSample (c, s - delay));
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
void runAudioCase (TriadProcessor& p, double sr, std::array<float, 3> gains, int latency, const char* name)
{
    p.prepareToPlay (sr, 128);
    auto signal = input (8192);
    auto expected = reference (signal, sr, gains, latency);
    juce::MidiBuffer midi;
    // Deliberately exceed the maximum block size given to prepareToPlay.
    p.processBlock (signal, midi);
    compare (signal, expected, name);
    require (p.getLatencySamples() == latency, "Host latency reporting");
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        require (argc >= 2, "Supply the test VST3 bundle path");
        runCrossoverTests();
        const juce::File effect (juce::String::fromUTF8 (argv[1]));
        TriadProcessor p;
        runAudioCase (p, 44100, { 1, 1, 1 }, 0, "Empty slots, oversized block");
        // Independent reconstruction check: split sum equals two all-pass filters.
        {
            const auto signal = input (16384);
            auto actual = reference (signal, 48000, { 1, 1, 1 }, 0);
            auto expected = signal;
            juce::dsp::LinkwitzRileyFilter<float> a, b;
            for (auto* f : { &a, &b }) { f->prepare ({ 48000, 16384, 2 }); f->setType (juce::dsp::LinkwitzRileyFilterType::allpass); }
            a.setCutoffFrequency (250); b.setCutoffFrequency (2500);
            for (int s = 0; s < expected.getNumSamples(); ++s)
                for (int c = 0; c < 2; ++c) expected.setSample (c, s, b.processSample (c, a.processSample (c, signal.getSample (c, s))));
            compare (actual, expected, "Phase-correct crossover reconstruction");
        }
        require (p.loadFile (1, effect).isEmpty(), "Load third-party VST3");
        runAudioCase (p, 48000, { 1, 0.25f, 1 }, 73, "Unequal band latency compensation");
        for (int i : { 0, 2 }) require (p.loadFile (i, effect).isEmpty(), "Load remaining bands");
        runAudioCase (p, 48000, { .25f, .25f, .25f }, 73, "All bands wet");
        mix (p, 0); runAudioCase (p, 96000, { 1, 1, 1 }, 73, "All bands dry with latency retained");
        mix (p, 50); runAudioCase (p, 44100, { .625f, .625f, .625f }, 73, "50 percent linear wet/dry");
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        // Produce genuine opaque VST3 state through the wrapper, not raw processor bytes.
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
        for (auto* node : xml->getChildIterator())
            if (node->hasTagName ("Slot"))
            {
                node->getChildByName ("State")->deleteAllTextElements();
                node->getChildByName ("State")->addTextElement (editedChildState.toBase64Encoding());
            }
        juce::AudioProcessor::copyXmlToBinary (*xml, saved);
        TriadProcessor restored; restored.prepareToPlay (44100, 128);
        restored.setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        for (int i = 0; i < 3; ++i) require (restored.getSlotInfo (i).loaded, "Restore child plugin");
        runAudioCase (restored, 44100, { .875f, .875f, .875f }, 73, "Hosted state and mix recall");
        if (argc >= 4)
        {
            juce::AudioPluginFormatManager manager;
            manager.addFormat (std::make_unique<juce::VST3PluginFormat>());
            juce::OwnedArray<juce::PluginDescription> types;
            manager.getFormat (0)->findAllTypesForFile (types, juce::String::fromUTF8 (argv[3]));
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
            require (p.loadFile (0, juce::File (juce::String::fromUTF8 (argv[3]))).isNotEmpty(), "Reject recursive self-hosting");
            require (p.getSlotInfo (0).loaded, "Rejected load preserves existing effect");
        }
        // A missing plugin must preserve its stored identity and opaque state.
        auto missingXml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        for (auto* node : missingXml->getChildIterator())
            if (node->hasTagName ("Slot")) node->getChildByName ("PLUGIN")->setAttribute ("file", "/not-installed/Test.vst3");
        juce::MemoryBlock missingData;
        juce::AudioProcessor::copyXmlToBinary (*missingXml, missingData);
        TriadProcessor missing;
        missing.setStateInformation (missingData.getData(), (int) missingData.getSize());
        require (! missing.getSlotInfo (0).loaded && missing.getSlotInfo (0).status.isNotEmpty(), "Missing effect reported");
        runAudioCase (missing, 44100, { 1, 1, 1 }, 0, "Missing effects pass bands dry");
        juce::MemoryBlock resaved; missing.getStateInformation (resaved);
        auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        require (resavedXml->getChildByName ("Slot")->getChildByName ("State")->getAllSubText()
              == missingXml->getChildByName ("Slot")->getChildByName ("State")->getAllSubText(), "Missing child state preserved");
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        {
            restored.showEffect (1); restored.closeWindows();
            std::cout << "Hosted effect editor opened and closed" << std::endl;
        }
        else
        {
            restored.showEffect (1);
            require (restored.getStatus().contains ("display"), "Headless window request handled safely");
            std::cout << "SKIP: native effect window (no display server); safe headless handling passed" << std::endl;
        }
        for (int i = 0; i < 3; ++i) restored.removeEffect (i);
        runAudioCase (restored, 48000, { 1, 1, 1 }, 0, "Remove effects, clear latency");
        {
            TriadProcessor preview;
            std::unique_ptr<juce::AudioProcessorEditor> editor (preview.createEditor());
            editor->setVisible (true);
            auto screenshot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
            if (argc >= 3)
            {
                const juce::File imageFile (juce::String::fromUTF8 (argv[2]));
                auto writeImage = [] (const juce::File& file, const juce::Image& image)
                {
                    juce::FileOutputStream stream (file);
                    require (stream.openedOk() && stream.setPosition (0) && stream.truncate().wasOk(), "Create fresh screenshot");
                    juce::PNGImageFormat png;
                    require (png.writeImageToStream (image, stream), "Write UI screenshot");
                };
                writeImage (imageFile, screenshot);
                auto* display = dynamic_cast<CrossoverDisplay*> (editor->findChildWithID ("frequencyDisplay"));
                require (display != nullptr, "Frequency display embedded in editor");
                display->setCrossoverValue (1, 2525.0f);
                display->setCrossoverValue (0, 2500.0f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                writeImage (imageFile.getSiblingFile ("Triad-preview-narrow.png"),
                            editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f));
            }
        }
        std::cout << "PASS: all integration tests" << std::endl;
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
