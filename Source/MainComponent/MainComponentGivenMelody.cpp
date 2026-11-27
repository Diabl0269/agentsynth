#include "MainComponentGivenMelody.h"

namespace {
constexpr double kMelodyFallbackBpm = 120.0;
}

// A linked clip keeps its own copy of the shared notes, so the clip's notes are read directly.
std::optional<synth::songsteps::GivenMelody>
givenMelodyFromSelection(const synth::TimelineDoc& doc, const synth::ui::ClipSelectionModel& selection, double bpm) {
    if (selection.size() != 1)
        return std::nullopt;
    const auto* clip = doc.getClip(selection.getSelected().front());
    if (clip == nullptr || clip->assetRef.isNotEmpty() || clip->notes.empty())
        return std::nullopt;
    std::vector<synth::songsteps::MelodyNoteIn> notes;
    notes.reserve(clip->notes.size());
    for (const auto& n : clip->notes)
        notes.push_back({n.startBeat, n.lengthBeats, n.pitch, n.velocity, n.muted});
    return synth::songsteps::givenMelodyFromNotes(clip->name, notes, clip->lengthBeats,
                                                  bpm > 0.0 ? bpm : kMelodyFallbackBpm);
}
