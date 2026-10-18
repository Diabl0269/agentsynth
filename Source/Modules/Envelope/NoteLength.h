#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

/** The three kinds of note length every rate menu offers: straight (1/8), triplet (1/8T, two thirds as long) and
 *  dotted (1/8D, one and a half times as long). */
enum class NoteKind { straight, triplet, dotted };

/** A note length written as `count`/`denominator` of a whole note (4 beats): "1/8" is {1, 8}, an LFO's "2/1" (two
 *  bars of 4/4) is {2, 1}. */
struct NoteLength {
    NoteKind kind = NoteKind::straight;
    int count = 1;
    int denominator = 4;
};

/** The denominators of the numbered chips, shortest note first (the order every saved list uses). */
inline const int (&noteDenominators())[8] {
    static const int denominators[8] = {128, 64, 32, 16, 8, 4, 2, 1};
    return denominators;
}

/** "" for straight, "T" for triplet, "D" for dotted. */
inline const char* noteKindSuffix(NoteKind kind) noexcept {
    return kind == NoteKind::triplet ? "T" : (kind == NoteKind::dotted ? "D" : "");
}

/** The saved name of a note length: "1/8", "1/8T", "1/8D", "2/1". Projects store this name, never an index. */
inline juce::String noteLengthName(const NoteLength& note) {
    return juce::String(note.count) + "/" + juce::String(note.denominator) + noteKindSuffix(note.kind);
}

/** Reads a name written by noteLengthName; nullopt for anything else ("Off", "Bar", "1/", "x/4", "0/4"). */
inline std::optional<NoteLength> parseNoteLength(const juce::String& name) {
    NoteLength note;
    juce::String body = name.trim();
    if (body.endsWithChar('T')) {
        note.kind = NoteKind::triplet;
        body = body.dropLastCharacters(1);
    } else if (body.endsWithChar('D')) {
        note.kind = NoteKind::dotted;
        body = body.dropLastCharacters(1);
    }
    const int slash = body.indexOfChar('/');
    if (slash <= 0 || slash >= body.length() - 1)
        return std::nullopt;
    const juce::String count = body.substring(0, slash);
    const juce::String denominator = body.substring(slash + 1);
    if (!count.containsOnly("0123456789") || !denominator.containsOnly("0123456789") || count.length() > 3 ||
        denominator.length() > 3)
        return std::nullopt;
    note.count = count.getIntValue();
    note.denominator = denominator.getIntValue();
    if (note.count < 1 || note.denominator < 1)
        return std::nullopt;
    return note;
}

/** Beats in `note`: a whole note is 4 beats, a triplet is two thirds of the straight length, a dotted note one and a
 *  half times it. */
inline double noteLengthBeats(const NoteLength& note) noexcept {
    const double straight = 4.0 * (double)note.count / (double)note.denominator;
    if (note.kind == NoteKind::triplet)
        return straight * 2.0 / 3.0;
    return note.kind == NoteKind::dotted ? straight * 1.5 : straight;
}

/** Beats of the note named `name`; 0 when the name is not a note length. */
inline double noteLengthBeats(const juce::String& name) {
    const auto note = parseNoteLength(name);
    return note.has_value() ? noteLengthBeats(*note) : 0.0;
}

/** The kind `name` is written in; straight for anything that is not a note length. */
inline NoteKind noteKindOf(const juce::String& name) {
    const auto note = parseNoteLength(name);
    return note.has_value() ? note->kind : NoteKind::straight;
}

} // namespace synth
