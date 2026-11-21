#pragma once

// Reads a pack's LICENSES/SOURCES.md into one line per instrument, for the list Settings > Sound packs shows under
// "Show sources". No UI.

#include <juce_core/juce_core.h>
#include <vector>

namespace synth::soundpacks {

struct SoundPackSourceEntry {
    juce::String instrument, sourcePack, url;
    int fileCount = 0;
};

/** The instruments of a SOURCES.md: each is a `## Name (`id`)` heading with `- Source pack:`, `- URL:` and
 *  `- Files used (N):` bullets. A section without a `Source pack:` bullet is skipped. */
std::vector<SoundPackSourceEntry> parseSoundPackSources(const juce::String& markdown);

/** One plain line per instrument ("Name: 18 recordings from Pack, host/path"). A file with no such sections falls back
 *  to its own first lines (at most 12), so an unusual file still shows something. */
juce::StringArray soundPackSourceLines(const juce::String& markdown);

} // namespace synth::soundpacks
