#include "SplitLink.h"
#include <cstdlib>
#include <map>
#include <mutex>
#include <new>
#include <string>

#if ! JUCE_MAC
// Without a process-wide registry (Windows and Linux builds), each binary keeps its own groups:
// VST3 instances link with each other.
void* triadProcessWideBlock (const char* name, size_t size, void (*construct) (void*))
{
    static std::mutex lock;
    static std::map<std::string, void*> blocks;
    const std::lock_guard<std::mutex> guard (lock);
    auto& block = blocks[name];
    if (block == nullptr)
    {
        block = std::calloc (1, size);
        construct (block);
    }
    return block;
}
#endif

namespace triad::link
{
Block& block()
{
    // The layout is part of the name, so a later Triad Split with a different layout never shares it.
    static Block* const shared = static_cast<Block*> (triadProcessWideBlock (
        ("audio.thomasross.triadsplit.links.v1." + juce::String ((int) sizeof (Block))).toRawUTF8(), sizeof (Block),
        [] (void* memory) { new (memory) Block(); }));
    jassert (shared->magic.load() == Block::expectedMagic);
    return *shared;
}
}
