#pragma once

// PianoRollTypes.h — the small value types PianoRollComponent's gesture/clipboard/geometry/scale
// code shares, hoisted out of the class body so the header stays under the file-size cap. Defined
// at namespace scope inside `pianoroll` (nested under synth::ui) rather than left as free names in
// synth::ui itself, so generic names like LineRange can't collide with anything else in that
// namespace; PianoRollComponent.h re-exposes each one as a nested-type alias
// (`using NoteHit = pianoroll::NoteHit;` etc.) so every existing `PianoRollComponent::NoteHit`-style
// qualified reference elsewhere keeps compiling unchanged.

#include "Music/MusicalScale.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/UIAnimation.h"
#include <array>
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

// The copy ghost's short-lived settle after a drop or an Esc cancel: the dragged notes' origins plus
// the preview offset they were released at, and the eased 0..1 progress of the 140 ms tween.
struct GhostSettle {
    std::vector<NoteOrigin> notes;
    double deltaBeats = 0.0;
    int deltaRows = 0;
    bool dropped = true; // false = cancelled (the ghost flies back to its origin and fades)
    float t = 0.0f;
};

// A scale or key change fades the rows and the notes' out-of-scale tint from the picture on screen to the new one
// instead of popping them (the filter adds or removes rows too). One entry per pitch, as drawn at the moment the
// change landed; the live value is that entry moved towards the final state by the eased progress `t`. Starting from
// the drawn entry, never from the last settled state, is what lets a second change (or an undo) mid-fade continue
// without a jump. Paint-only: hit testing and every other reader already see the final rows.
struct ScaleFade {
    struct Row {
        float rel = 0.0f;   ///< rows below the top edge: the row's drawn y is canvasTop + rel * pixelsPerSemitone
        float alpha = 1.0f; ///< 1 a row that exists, 0 one the filter hides
        float out = 0.0f;   ///< 0 in scale, 1 out of scale: the note tint
    };
    std::array<Row, 128> from{};
    double topAtStart = 0.0; ///< topRowPosition_ when the fade began: a scroll during the fade moves `from` with it
    synth::ClipId clip;      ///< the clip the last scale context was pushed for: a clip switch never fades
    float t = 1.0f;
    bool active = false;
    synth::ui::AnimationDriver driver;
    void stop() {
        driver.stop();
        active = false;
        t = 1.0f;
    }
    void step(double ms) { driver.stepForTest(ms); } ///< test seams: no VBlank reaches an off-screen roll
    void finish() { driver.completeForTest(); }
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
    // Per-hit drum settings (see MidiNote): a pasted hit keeps its chance, roll, nudge and condition.
    int chance = 100;
    int repeat = 1;
    bool flam = false;
    double nudge = 0.0;
    int condHit = 1;
    int condOf = 1;
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

} // namespace synth::ui::pianoroll
