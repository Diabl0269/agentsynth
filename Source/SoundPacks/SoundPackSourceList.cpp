#include "SoundPacks/SoundPackSourceList.h"

namespace synth::soundpacks {

namespace {

constexpr int kMaxFallbackLines = 12;

// "Upright piano (`upright-piano`)" -> "Upright piano".
juce::String withoutId(const juce::String& heading) {
    const auto open = heading.lastIndexOf(" (`");
    if (open > 0 && heading.endsWith(")"))
        return heading.substring(0, open).trim();
    return heading.trim();
}

// The text after "- <key>:" when `line` is that bullet, else an empty string.
bool bulletValue(const juce::String& line, const juce::String& key, juce::String& value) {
    const auto prefix = "- " + key;
    if (!line.startsWith(prefix))
        return false;
    value = line.substring(prefix.length()).trim();
    return true;
}

} // namespace

std::vector<SoundPackSourceEntry> parseSoundPackSources(const juce::String& markdown) {
    std::vector<SoundPackSourceEntry> entries;
    SoundPackSourceEntry current;
    bool inSection = false;
    bool hasSourcePack = false;
    const auto flush = [&] {
        if (inSection && hasSourcePack)
            entries.push_back(current);
        current = {};
        hasSourcePack = false;
    };
    for (const auto& raw : juce::StringArray::fromLines(markdown)) {
        const auto line = raw.trim();
        if (line.startsWith("## ")) {
            flush();
            inSection = true;
            current.instrument = withoutId(line.substring(3));
            continue;
        }
        if (!inSection)
            continue;
        juce::String value;
        if (bulletValue(line, "Source pack:", value)) {
            current.sourcePack = value;
            hasSourcePack = value.isNotEmpty();
        } else if (bulletValue(line, "URL:", value)) {
            current.url = value;
        } else if (bulletValue(line, "Files used (", value)) {
            current.fileCount = value.getIntValue(); // "18): a.wav, ..." reads as 18
        }
    }
    flush();
    return entries;
}

juce::StringArray soundPackSourceLines(const juce::String& markdown) {
    juce::StringArray lines;
    for (const auto& entry : parseSoundPackSources(markdown)) {
        juce::String line = entry.instrument + ": ";
        if (entry.fileCount > 0)
            line << entry.fileCount << (entry.fileCount == 1 ? " recording from " : " recordings from ");
        else
            line << "from ";
        line << entry.sourcePack;
        auto url = entry.url;
        if (url.startsWithIgnoreCase("https://"))
            url = url.substring(8);
        else if (url.startsWithIgnoreCase("http://"))
            url = url.substring(7);
        if (url.isNotEmpty())
            line << ", " << url;
        lines.add(line);
    }
    if (!lines.isEmpty())
        return lines;

    for (const auto& raw : juce::StringArray::fromLines(markdown)) {
        auto line = raw.trim();
        if (line.isEmpty() || line.startsWith("#"))
            continue;
        if (line.startsWith("- "))
            line = line.substring(2);
        line = line.removeCharacters("`").trim();
        if (line.isEmpty())
            continue;
        lines.add(line);
        if (lines.size() >= kMaxFallbackLines)
            break;
    }
    return lines;
}

} // namespace synth::soundpacks
