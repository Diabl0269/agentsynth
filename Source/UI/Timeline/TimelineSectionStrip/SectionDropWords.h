#pragma once

// SectionDropWords.h: the one vocabulary for the two ways a section can land on other music, shared by the drop hint
// beside the drop line, the block menu's "Place again after this" item and the Preferences row, so the three cannot
// drift. "Insert" makes room (the music at and after the landing moves later); "replace" takes the place of the
// sections it lands on; "place" is an insert that finds the span free, so nothing moves.

#include <juce_core/juce_core.h>

namespace synth::ui {

struct SectionDropWords {
    // Hint beside the drop line. `copy` is Option held (the originals stay); `moves` is true when a release will push
    // something later (make-room mode only); `between` counts the unselected sections between the dragged ones, which
    // an insert move takes along (a copy or a replace leaves them where they are).
    static juce::String hint(bool makeRoom, bool copy, bool moves = true, int between = 0) {
        if (!makeRoom)
            return copy ? "Release to replace them with a copy" : "Release to replace the sections under it";
        if (copy)
            return moves ? "Release to insert a copy here" : "Release to place a copy here";
        const juce::String words = moves ? "Release to insert here" : "Release to place here";
        if (between <= 0)
            return words;
        return words + (between == 1 ? ", the section between comes too"
                                     : ", the " + juce::String(between) + " sections between come too");
    }
    // Every hint phrase one drag can show, so the pill is sized once and never resizes on a Shift cross-fade.
    static juce::StringArray hintsFor(bool copy, int between = 0) {
        return {hint(true, copy, true, between), hint(true, copy, false, between), hint(false, copy)};
    }
    // The block menu's "Place again after this" item, in the mode the setting (or Shift) picks.
    static juce::String placeAgainMenu(bool makeRoom) {
        return makeRoom ? "Place again after this (insert)" : "Place again after this (replace the sections there)";
    }
    // The Preferences combo's two choices (ids 1 and 2) and the Shift line.
    static juce::String insertChoice() { return "Insert (the music after moves later)"; }
    static juce::String replaceChoice() { return "Replace the sections under it"; }
    static juce::String shiftLine() {
        return "Hold Shift while you drop to do the other one (insert or replace), that time only.";
    }
    // The Preferences combo's hover tip: the two choices in the same terms as the hint, then the Shift line.
    static juce::String settingTooltip() {
        return "Insert (the default): the section goes in at the drop point and the music after it moves later. "
               "Replace: the section takes the place of the sections it lands on. Hold Shift while you drop to do the "
               "other one.";
    }
};

} // namespace synth::ui
