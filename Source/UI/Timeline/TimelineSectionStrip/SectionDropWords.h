#pragma once

// SectionDropWords.h: the one vocabulary for the two ways a section can land on other music, shared by the drop hint
// beside the drop line, the block menu's "Place again after this" item and the Preferences row, so the three cannot
// drift. "Insert" makes room (the music at and after the landing moves later); "replace" covers what is there; "place"
// is an insert that finds the span free, so nothing moves.

#include <juce_core/juce_core.h>

namespace synth::ui {

struct SectionDropWords {
    // Hint beside the drop line. `copy` is Option held (the originals stay); `moves` is true when a release will push
    // something later (make-room mode only).
    static juce::String hint(bool makeRoom, bool copy, bool moves = true) {
        if (!makeRoom)
            return copy ? "Release to replace it with a copy" : "Release to replace what is here";
        if (!moves)
            return copy ? "Release to place a copy here" : "Release to place here";
        return copy ? "Release to insert a copy here" : "Release to insert here";
    }
    // Every hint phrase one drag can show, so the pill is sized once and never resizes on a Shift cross-fade.
    static juce::StringArray hintsFor(bool copy) {
        return {hint(true, copy, true), hint(true, copy, false), hint(false, copy)};
    }
    // The block menu's "Place again after this" item, in the mode the setting (or Shift) picks.
    static juce::String placeAgainMenu(bool makeRoom) {
        return makeRoom ? "Place again after this (insert)" : "Place again after this (replace what is there)";
    }
    // The Preferences combo's two choices (ids 1 and 2) and the Shift line.
    static juce::String insertChoice() { return "Insert (the music after moves later)"; }
    static juce::String replaceChoice() { return "Replace what is there"; }
    static juce::String shiftLine() {
        return "Hold Shift while you drop to do the other one (insert or replace), that time only.";
    }
};

} // namespace synth::ui
