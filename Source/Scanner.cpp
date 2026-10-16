// Triad Scanner: a tiny helper that lives inside Triad's bundle (Contents/Helpers).
//
//   Triad Scanner --scan <plug-in.vst3> <result.xml>
//
// It opens one VST3 plug-in in this separate process and writes what the plug-in contains
// (name, maker, effect or instrument) to <result.xml>. Triad runs it for plug-ins whose type
// cannot be read from disk, so a plug-in that crashes or hangs while being opened only takes
// this helper down, never the DAW.
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <cstdlib>

int main (int argc, char* argv[])
{
    if (argc != 4 || juce::String (argv[1]) != "--scan") return 2;
    const juce::File plugin (juce::String::fromUTF8 (argv[2]));
    const juce::File result (juce::String::fromUTF8 (argv[3]));
    juce::OwnedArray<juce::PluginDescription> found;
    {
        juce::ScopedJuceInitialiser_GUI messageThread;
        juce::VST3PluginFormatHeadless format;
        format.findAllTypesForFile (found, plugin.getFullPathName());
        juce::XmlElement root ("TriadScan");
        root.setAttribute ("version", 1);
        for (auto* d : found) root.addChildElement (d->createXml().release());
        const auto part = result.getSiblingFile (result.getFileName() + ".part");
        if (! root.writeTo (part) || ! part.moveFileTo (result)) std::_Exit (3);
    }
    // Leave without unloading the plug-in: some crash in their own shutdown code.
    std::_Exit (0);
}
