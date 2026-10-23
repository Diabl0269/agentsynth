#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include <cmath>

namespace synth::ui {

using synth::soundbrowser::SoundEntry;
using synth::soundbrowser::SoundKind;

namespace {

juce::String middleDot() { return juce::String::fromUTF8("\xc2\xb7"); }

juce::String plural(int n, const char* one, const char* many) { return juce::String(n) + " " + (n == 1 ? one : many); }

} // namespace

juce::String formatSoundLength(double seconds) {
    if (seconds >= 60.0) {
        const int total = (int)std::lround(seconds);
        return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
    }
    if (seconds >= 10.0)
        return juce::String((int)std::lround(seconds)) + " s";
    return juce::String(seconds, 1) + " s";
}

int soundBarCount(double tempo, double seconds) {
    if (tempo <= 0.0 || seconds <= 0.0)
        return 0;
    return (int)std::lround(seconds * tempo / 60.0 / 4.0);
}

juce::String soundDetailText(const SoundEntry& entry) {
    if (entry.kind == SoundKind::Instrument)
        return "instrument";
    if (entry.kind == SoundKind::Loop) {
        const int bars = soundBarCount(entry.tempo, entry.lengthSeconds);
        if (entry.tempo <= 0.0 || bars <= 0)
            return "? " + middleDot() + " " + formatSoundLength(entry.lengthSeconds);
        return juce::String((int)std::lround(entry.tempo)) + " " + middleDot() + " " + plural(bars, "bar", "bars");
    }
    return formatSoundLength(entry.lengthSeconds);
}

juce::String describeSoundForAccessibility(const SoundEntry& entry, int position, int count) {
    juce::String text = entry.name + ", ";
    if (entry.kind == SoundKind::Instrument) {
        text += "instrument";
    } else if (entry.kind == SoundKind::Loop) {
        text += "loop";
        if (entry.tempo > 0.0)
            text += ", " + juce::String((int)std::lround(entry.tempo)) + " BPM";
        const int bars = soundBarCount(entry.tempo, entry.lengthSeconds);
        if (bars > 0)
            text += ", " + plural(bars, "bar", "bars");
        if (entry.key.isNotEmpty())
            text += ", " + entry.key;
    } else {
        text += "one-shot, " + juce::String(entry.lengthSeconds, 1) + " seconds";
    }
    return text + ", " + juce::String(position) + " of " + juce::String(count);
}

juce::String describeFolderForAccessibility(const juce::String& name, int soundCount, int position, int count) {
    return name + ", folder, " + plural(soundCount, "sound", "sounds") + ", " + juce::String(position) + " of " +
           juce::String(count);
}

juce::String describePlaceForAccessibility(const juce::String& name, int soundCount, int position, int count,
                                           const juce::String& status) {
    return name + ", " + (status.isNotEmpty() ? status.toLowerCase() : plural(soundCount, "sound", "sounds")) + ", " +
           juce::String(position) + " of " + juce::String(count);
}

juce::String describePackOfferForAccessibility(const juce::String& name, const juce::String& size, bool busy,
                                               int position, int count) {
    return name + (busy ? ", downloading" : ", not downloaded, " + size + ", press Return to download") + ", " +
           juce::String(position) + " of " + juce::String(count);
}

juce::String describeAddFolderForAccessibility(int position, int count) {
    return "Add folder, press Return to choose a folder, " + juce::String(position) + " of " + juce::String(count);
}

juce::String describeShapeForAccessibility(const juce::String& name, const juce::String& kindWord, bool factory,
                                           int position, int count) {
    return name + ", " + kindWord + " shape, " + (factory ? "built in" : "saved by you") + ", " +
           juce::String(position) + " of " + juce::String(count);
}

juce::String fileManagerName() {
#if JUCE_MAC
    return "Finder";
#elif JUCE_WINDOWS
    return "File Explorer";
#else
    return "the file manager";
#endif
}

juce::String describePathForAccessibility(const juce::StringArray& parts) {
    return "Path, " + parts.joinIntoString(", ");
}

} // namespace synth::ui
