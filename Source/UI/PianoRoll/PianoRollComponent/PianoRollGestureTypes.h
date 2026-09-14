// PianoRollGestureTypes.h — nested types for PianoRollComponent's drag/hit-test gestures
// (Move/Resize/VelocityScrub). #included mid-class-body from PianoRollComponent.h at the exact
// spot these used to be defined inline, so they stay nested (PianoRollComponent::NoteHit /
// PianoRollComponent::NoteOrigin) with no change to any qualified reference anywhere else.

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
