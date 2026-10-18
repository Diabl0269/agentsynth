#include "UI/Layout/NoteLengthMenu/NoteLengthMenuModel.h"

#include <algorithm>

// Concern: sorting a list of choice names into the note-length popup's groups.

namespace synth::ui {

juce::String NoteLengthMenuModel::kindName(NoteKind kind) {
    return kind == NoteKind::triplet ? "Triplet" : (kind == NoteKind::dotted ? "Dotted" : "Straight");
}

juce::String NoteLengthMenuModel::accessibleNameFor(const juce::String& name) {
    const auto note = parseNoteLength(name);
    if (!note.has_value())
        return name;
    if (note->kind == NoteKind::straight && note->count > 1 && note->denominator == 1)
        return juce::String(note->count) + " bars";
    juce::String text = juce::String(note->count) + "/" + juce::String(note->denominator);
    if (note->kind != NoteKind::straight)
        text << " " << kindName(note->kind).toLowerCase();
    return text;
}

NoteLengthMenuModel::NoteLengthMenuModel(const juce::StringArray& choices) {
    bool seenNote = false;
    for (const auto& name : choices) {
        const auto note = parseNoteLength(name);
        NoteLengthChip chip;
        chip.name = name;
        chip.accessibleName = accessibleNameFor(name);
        if (note.has_value() && note->count == 1) {
            seenNote = true;
            chip.label = juce::String(note->denominator);
            chip.tooltip = name;
            numbers_[static_cast<int>(note->kind)].push_back(chip);
        } else if (note.has_value()) {
            seenNote = true;
            const bool bars = note->denominator == 1;
            chip.label = bars ? juce::String(note->count) : name;
            chip.tooltip = bars ? name + " (" + chip.accessibleName + ")" : name;
            trailing_.push_back(chip);
        } else {
            chip.label = name;
            chip.tooltip = name;
            (seenNote ? trailing_ : leading_).push_back(chip);
        }
    }
    // Longest note first, whatever order the list stores them in (the lists are shortest first).
    for (auto& chips : numbers_)
        std::stable_sort(chips.begin(), chips.end(), [](const NoteLengthChip& a, const NoteLengthChip& b) {
            return noteLengthBeats(a.name) > noteLengthBeats(b.name);
        });
}

NoteKind NoteLengthMenuModel::kindOf(const juce::String& currentName) const {
    const auto note = parseNoteLength(currentName);
    return note.has_value() && note->count == 1 ? note->kind : NoteKind::straight;
}

} // namespace synth::ui
