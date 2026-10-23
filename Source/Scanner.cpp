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
#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

int main (int argc, char* argv[])
{
   #if defined (_WIN32)
    // A plug-in that crashes here must end this helper at once (Triad reads that as "crashed"), never leave a
    // "has stopped working" or abort() dialog waiting on screen.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif
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
