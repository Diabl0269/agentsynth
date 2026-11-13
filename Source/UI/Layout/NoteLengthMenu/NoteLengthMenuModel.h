#pragma once

#include "Modules/Envelope/NoteLength.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

/** One choice of the note-length menu, as the popup shows it. */
struct NoteLengthChip {
    juce::String name;           ///< The choice it picks, exactly as the parameter lists it ("1/8T").
    juce::String label;          ///< The text on the chip: "8", "Off", "2".
    juce::String tooltip;        ///< The full name shown on hover.
    juce::String accessibleName; ///< What a screen reader says: "1/8 triplet".
};

/** How a list of choice names splits into the popup's groups, with no component involved (unit-tested on its own).
 *
 *  The names are read, not assumed: "1/8" is a straight value, "1/8T" a triplet, "1/8D" a dotted one, and "2/1" (a
 *  count above 1) belongs to the trailing group; any other name ("Off", "Bar") is an extra that sits in the leading
 *  group when it comes before the first note length in the list and in the trailing group after it. A kind with no
 *  name in the list is unavailable (its letter button is disabled). */
class NoteLengthMenuModel {
public:
    explicit NoteLengthMenuModel(const juce::StringArray& choices);

    const std::vector<NoteLengthChip>& leading() const noexcept { return leading_; }
    const std::vector<NoteLengthChip>& trailing() const noexcept { return trailing_; }

    /** The numbered chips of `kind`, longest note first (1, 2, 4 ... 128), only for the values the list has. */
    const std::vector<NoteLengthChip>& numbers(NoteKind kind) const noexcept {
        return numbers_[static_cast<int>(kind)];
    }

    /** True when the list has at least one value of `kind`. */
    bool hasKind(NoteKind kind) const noexcept { return !numbers(kind).empty(); }

    /** The kind the menu opens on for `currentName`: its own kind, straight for an extra or a bar length. */
    NoteKind kindOf(const juce::String& currentName) const;

    /** "Straight", "Triplet" or "Dotted". */
    static juce::String kindName(NoteKind kind);

    /** What a screen reader says for a choice name: "1/8 triplet", "1/4 dotted", "2 bars", "Off". */
    static juce::String accessibleNameFor(const juce::String& name);

private:
    std::vector<NoteLengthChip> leading_;
    std::vector<NoteLengthChip> trailing_;
    std::vector<NoteLengthChip> numbers_[3];
};

} // namespace synth::ui
