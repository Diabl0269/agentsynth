#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "Music/SongKey.h"
#include "SoundBrowser/SoundSearch.h"
#include <cmath>

namespace synth::ui {

using synth::soundbrowser::SoundEntry;
using synth::soundbrowser::SoundKind;

namespace {

juce::String middleDot() { return juce::String::fromUTF8("\xc2\xb7"); }

// "Am", "C", "F#m": the root, and an m for a minor key; a key that does not read gives "".
juce::String shortKeyName(const juce::String& key) {
    const auto id = synth::soundbrowser::parseSoundKey(key);
    if (id.pitchClass < 0)
        return {};
    return synth::keyRootName(id.pitchClass) + (id.mode == 2 ? "m" : "");
}

juce::String plural(int n, const char* one, const char* many) { return juce::String(n) + " " + (n == 1 ? one : many); }

// The fields a loop's row and spoken description share; 0 means unknown (bpm) or under half a bar (bars).
struct LoopFields {
    int bpm = 0;
    int bars = 0;
};

LoopFields loopFields(const SoundEntry& entry) {
    LoopFields fields;
    if (entry.tempo > 0.0) {
        fields.bpm = (int)std::lround(entry.tempo);
        fields.bars = soundBarCount(entry.tempo, entry.lengthSeconds);
    }
    return fields;
}

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

juce::String soundDetailText(const SoundEntry& entry, int maxParts) {
    if (entry.kind == SoundKind::Instrument)
        return "instrument";
    juce::StringArray parts;
    if (entry.kind == SoundKind::Loop) {
        const auto fields = loopFields(entry);
        if (fields.bpm > 0)
            parts.add(juce::String(fields.bpm) + " BPM");
        if (fields.bars > 0)
            parts.add(plural(fields.bars, "bar", "bars"));
    }
    parts.add(formatSoundLength(entry.lengthSeconds));
    while (parts.size() > juce::jmax(1, maxParts))
        parts.remove(0);
    return parts.joinIntoString(" " + middleDot() + " ");
}

float soundDetailMaxWidth(int rowWidth) { return 0.6f * (float)(rowWidth - 2 * kSoundRowSideMargin); }

juce::String soundDetailTextFitting(const SoundEntry& entry, const juce::Font& font, float maxWidth) {
    juce::String text;
    for (int parts = 3; parts >= 1; --parts) {
        text = soundDetailText(entry, parts);
        if (juce::GlyphArrangement::getStringWidth(font, text) <= maxWidth)
            break;
    }
    return text;
}

juce::String soundKeyMetaText(const SoundEntry& entry, const juce::String& songKey) {
    if (entry.kind != SoundKind::Loop)
        return {};
    const auto own = shortKeyName(entry.key);
    if (own.isEmpty())
        return {};
    auto text = "Key " + own;
    if (const auto song = shortKeyName(songKey); song.isNotEmpty())
        text += " " + middleDot() + " song " + song;
    return text;
}

juce::String matchKeyText(const synth::soundbrowser::LoopTarget& target) {
    using synth::soundbrowser::KeyMatch;
    switch (target.keyMatch) {
    case KeyMatch::Shifted:
        return "Plays " + plural(std::abs(target.semitones), "semitone", "semitones") +
               (target.semitones > 0 ? " up" : " down") + ", to " + target.targetKey;
    case KeyMatch::InKey:
        return "Already in " + target.targetKey;
    case KeyMatch::NoSongKey:
        return "No key in the song";
    case KeyMatch::NoLoopKey:
        return "This loop has no key";
    case KeyMatch::Off:
        break;
    }
    return {};
}

juce::String describeSoundForAccessibility(const SoundEntry& entry, int position, int count) {
    juce::String text = entry.name + ", ";
    if (entry.kind == SoundKind::Instrument) {
        text += "instrument";
    } else {
        if (entry.kind == SoundKind::Loop) {
            const auto fields = loopFields(entry);
            text += "loop, ";
            if (fields.bpm > 0)
                text += juce::String(fields.bpm) + " BPM, ";
            if (fields.bars > 0)
                text += plural(fields.bars, "bar", "bars") + ", ";
        } else {
            text += "one-shot, ";
        }
        text += juce::String(entry.lengthSeconds, 1) + " seconds";
        if (entry.kind == SoundKind::Loop && entry.key.isNotEmpty())
            text += ", " + entry.key;
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

juce::String shapeKindAndSource(const synth::shapes::LibraryShape& shape) {
    return juce::String(shape.kind == synth::shapes::ShapeKind::Filter ? "Filter" : "LFO") + " shape, " +
           (shape.factory ? "factory" : "yours");
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
