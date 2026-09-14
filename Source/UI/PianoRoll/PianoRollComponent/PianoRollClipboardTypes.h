// PianoRollClipboardTypes.h — the note-clipboard entry type. #included mid-class-body from
// PianoRollComponent.h at the exact spot it used to be defined inline, so it stays nested
// (PianoRollComponent::ClipboardNote) with no change to any qualified reference elsewhere.

/** One copied note, stored RELATIVE to the earliest note in the copied block rather than in
 *  absolute (or even clip-relative) beats. That is what lets a copy survive being pasted into a
 *  different clip at a different position — the block keeps its internal shape and only its
 *  anchor moves. Every field a note carries is captured, `muted` included: a muted note pastes
 *  back muted, the same way a split or a duplicate carries the flag (see MidiNote::muted). */
struct ClipboardNote {
    double offsetFromEarliest = 0.0;
    double lengthBeats = 1.0;
    int pitch = 60;
    int velocity = 100;
    int channel = 1;
    bool muted = false;
};
