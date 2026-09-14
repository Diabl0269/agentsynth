// PianoRollGeometryTypes.h — nested types for PianoRollComponent's note/grid geometry
// (paint()/effectiveGeometryFor/visibleLineRange). #included mid-class-body from
// PianoRollComponent.h at the exact spot these used to be defined inline, so they stay nested
// (PianoRollComponent::NoteGeometry / PianoRollComponent::LineRange) with no change to any
// qualified reference elsewhere.

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
