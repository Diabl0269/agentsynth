#pragma once

// PianoRollTypes.h — the small value types PianoRollComponent's gesture/clipboard/geometry/scale
// code shares, hoisted out of the class body so the header stays under the file-size cap. Defined
// at namespace scope inside `pianoroll` (nested under synth::ui) rather than left as free names in
// synth::ui itself, so generic names like LineRange can't collide with anything else in that
// namespace; PianoRollComponent.h re-exposes each one as a nested-type alias
// (`using NoteHit = pianoroll::NoteHit;` etc.) so every existing `PianoRollComponent::NoteHit`-style
// qualified reference elsewhere keeps compiling unchanged.

#include "Timeline/MusicalScale.h"
#include "Timeline/TimelineDoc.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui::pianoroll {

struct NoteHit {
    synth::NoteId id;
    juce::Rectangle<int> rect;
    bool onRightEdge = false;
};

// One dragged/scrubbed note's ORIGIN (pre-gesture) state — every preview/commit computation
// reads from this, never from the accumulating pointer position (TimelineClipLaneArea's
// DragOrigin comment: this is what keeps rounding from accumulating frame to frame).
struct NoteOrigin {
    synth::NoteId id;
    double startBeat = 0.0; // clip-relative
    double lengthBeats = 0.0;
    int pitch = 0;
    int velocity = 100;
};

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

// Effective (possibly mid-drag) clip-relative geometry / velocity for one note — read by
// paint() and by commit-on-mouseUp, exactly like TimelineClipLaneArea::effectiveGeometryFor.
struct NoteGeometry {
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    int pitch = 0;
    int velocity = 100;
};

// [first, last] multiples of `spacingBeats` visible in the grid region — empty (last < first)
// when the lines would be closer together than kMinGridLinePixels. paintGridLines and
// getGridLineCountForTest walk the SAME range, so the seam can never drift from the paint.
struct LineRange {
    long long first = 0;
    long long last = -1;
    int count() const noexcept { return last < first ? 0 : (int)(last - first + 1); }
};

// Per-clip scale memory: SESSION-ONLY, deliberately never persisted (mirrors rollView_ and
// firstVisiblePitch_, which are not persisted either) — see setScaleContext's class-comment
// discussion of why a clip's row/colour context is view state, not document state. A clip id
// absent from this map has never had a scale chosen for it and reads back as "No scale" /
// pitch-visibility off, per the class's "a clip never opened starts at No scale" contract.
struct ClipScaleMemory {
    std::optional<synth::MusicalScale> scale;
    bool pitchVisibilityOn = false;
};

} // namespace synth::ui::pianoroll
