#pragma once
#include "AudioEngine.h"
#include <array>
#include <atomic>

// Link groups for Triad Split. Every Triad Split in the same group (A to H) shares its bands: how many,
// where the dividers are and their slopes. Move a divider on one and every other in the group follows,
// on any track. The groups live in one small block of memory per DAW process that both the VST3 and the
// AU version (two separate binaries) find by name, so the two formats link with each other too.
//
// The block is plain atomics: the audio thread reads a group without locking (a sequence lock; a read
// that overlaps a write is simply retried, or the last good values are kept) and a writer that finds
// another writer busy tries again on its next block.
namespace triad::link
{
constexpr int numGroups = 8;                               // A to H (0 means "not linked" in a group number)
constexpr int numValues = 1 + 2 * maxCrossovers;           // band count, divider frequencies, slope orders
constexpr int bandsIndex = 0;
constexpr int cutIndex (int divider) { return 1 + divider; }
constexpr int slopeIndex (int divider) { return 1 + maxCrossovers + divider; }
constexpr int anyBand = maxBands;                          // players[anyBand]: members playing ALL or NONE
constexpr int editLogSize = 16;
using Values = std::array<float, numValues>;
using Mask = std::array<bool, numValues>;

struct Group
{
    std::atomic<uint32_t> sequence { 0 };                  // 0: nothing yet; odd: being written; even: settled
    std::atomic<int32_t> writing { 0 };
    std::atomic<int32_t> members { 0 };
    std::array<std::atomic<int32_t>, maxBands + 1> players {};   // per band: how many members play it
    std::array<std::atomic<float>, numValues> values {};
    // Bands added or removed (from a window): +(band + 1) a band split in two (the new band above it),
    // -(band + 1) a band removed. The log is written before the new values, and only on the message thread.
    std::atomic<uint32_t> edits { 0 };
    std::array<std::atomic<int32_t>, editLogSize> editLog {};
};

struct Block
{
    static constexpr uint32_t expectedMagic = 0x54535031u;     // "TSP1"; a new layout gets a new name
    std::atomic<uint32_t> magic { expectedMagic };
    std::array<Group, numGroups> groups;
};

// This process's groups (created by the first Triad Split, VST3 or AU, and kept until the DAW quits).
Block& block();
inline Group* group (int number) { return number >= 1 && number <= numGroups ? &block().groups[(size_t) number - 1] : nullptr; }
inline juce::String groupName (int number) { return number >= 1 && number <= numGroups ? juce::String::charToString ((juce::juce_wchar) ('A' + number - 1)) : juce::String ("OFF"); }

// The group's values. False when nothing was published yet (or a write was in progress the whole time).
inline bool read (const Group& g, Values& out, uint32_t& sequence) noexcept
{
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto before = g.sequence.load (std::memory_order_acquire);
        if (before == 0) return false;
        if ((before & 1u) != 0) continue;
        for (int i = 0; i < numValues; ++i) out[(size_t) i] = g.values[(size_t) i].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (g.sequence.load (std::memory_order_relaxed) == before) { sequence = before; return true; }
    }
    return false;
}
inline uint32_t sequenceOf (const Group& g) noexcept { return g.sequence.load (std::memory_order_acquire); }

// Writes the values marked in `changed` (all of them when the group has none yet); the others keep the
// group's. False when another writer is busy (try again later). Nothing changes when the values are equal.
inline bool write (Group& g, const Values& v, const Mask& changed) noexcept
{
    int32_t free = 0;
    if (! g.writing.compare_exchange_strong (free, 1, std::memory_order_acquire)) return false;
    const auto before = g.sequence.load (std::memory_order_relaxed);
    const bool first = before == 0;
    bool differs = first;
    for (int i = 0; i < numValues && ! differs; ++i)
        differs = changed[(size_t) i] && ! juce::exactlyEqual (g.values[(size_t) i].load (std::memory_order_relaxed), v[(size_t) i]);
    if (differs)
    {
        g.sequence.store (before + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (int i = 0; i < numValues; ++i)
            if (first || changed[(size_t) i]) g.values[(size_t) i].store (v[(size_t) i], std::memory_order_relaxed);
        g.sequence.store (before + 2 == 0 ? 2 : before + 2, std::memory_order_release);
    }
    g.writing.store (0, std::memory_order_release);
    return true;
}
}

// One block of zeroed memory per process, shared by every binary loaded in it, found by name (macOS: kept
// by the Objective-C runtime; elsewhere: per binary). `construct` runs once, on the new block.
void* triadProcessWideBlock (const char* name, size_t size, void (*construct) (void*));
