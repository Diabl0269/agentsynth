// The melody a song request carries, and the plan card's line about it (docs/ai/song-steps.md#a-given-melody).
#include "SongSteps.h"

#include <algorithm>
#include <cmath>

namespace synth::songsteps {

namespace {

constexpr double kBeatsPerBar = 4.0;

// At most 3 decimals, trailing zeros trimmed: 0.5 -> "0.5", 2.0 -> "2", 0.3333 -> "0.333".
juce::String beatText(double beats) {
    auto text = juce::String(std::round(beats * 1000.0) / 1000.0, 3);
    while (text.endsWithChar('0'))
        text = text.dropLastCharacters(1);
    return text.endsWithChar('.') ? text.dropLastCharacters(1) : text;
}

} // namespace

juce::var GivenMelody::toVar() const {
    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("name", name);
    object->setProperty("bars", bars);
    object->setProperty("notes", notes);
    object->setProperty("bpm", bpm);
    return juce::var(object.get());
}

std::optional<GivenMelody> givenMelodyFromNotes(const juce::String& clipName, const std::vector<MelodyNoteIn>& notes,
                                                double clipLengthBeats, double bpm) {
    const int bars = juce::jlimit(1, kMaxMelodyBars, (int)std::ceil(clipLengthBeats / kBeatsPerBar));
    const double limit = bars * kBeatsPerBar;
    std::vector<MelodyNoteIn> kept;
    for (auto note : notes) {
        if (note.muted || note.start >= limit || note.start < 0.0)
            continue;
        note.length = std::min(note.length, limit - note.start);
        if (note.length <= 0.0)
            continue;
        note.pitch = juce::jlimit(0, 127, note.pitch);
        note.velocity = juce::jlimit(1, 127, note.velocity);
        kept.push_back(note);
    }
    if (kept.empty())
        return std::nullopt;
    std::stable_sort(kept.begin(), kept.end(), [](const MelodyNoteIn& a, const MelodyNoteIn& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    if ((int)kept.size() > kMaxMelodyNotes)
        kept.resize((size_t)kMaxMelodyNotes);

    GivenMelody melody;
    melody.name = clipName.substring(0, kMaxMelodyNameLength);
    melody.bars = bars;
    melody.bpm = bpm;
    melody.noteCount = (int)kept.size();
    juce::StringArray tokens;
    for (const auto& n : kept)
        tokens.add(beatText(n.start) + ":" + beatText(n.length) + ":" + juce::String(n.pitch) + ":" +
                   juce::String(n.velocity));
    melody.notes = tokens.joinIntoString(" ");
    return melody;
}

juce::String planMelodyText(const SongPlan& plan, const juce::String& sentMelodyName) {
    if (plan.melodyTrack.isNotEmpty())
        return "The \"" + plan.melodyTrack + "\" track plays your melody \"" + plan.melodyName + "\" (" +
               juce::String(plan.melodyBars) + (plan.melodyBars == 1 ? " bar" : " bars") + "), note for note.";
    if (sentMelodyName.isNotEmpty())
        return "This server can't build around a melody yet, so the song ignores \"" + sentMelodyName + "\".";
    return {};
}

} // namespace synth::songsteps
