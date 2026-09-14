// PianoRollScaleTypes.h — the per-clip scale-memory type. #included mid-class-body from
// PianoRollComponent.h at the exact spot it used to be defined inline, so it stays nested
// (PianoRollComponent::ClipScaleMemory) with no change to any qualified reference elsewhere.

// Per-clip scale memory: SESSION-ONLY, deliberately never persisted (mirrors rollView_ and
// firstVisiblePitch_, which are not persisted either) — see setScaleContext's class-comment
// discussion of why a clip's row/colour context is view state, not document state. A clip id
// absent from this map has never had a scale chosen for it and reads back as "No scale" /
// pitch-visibility off, per the class's "a clip never opened starts at No scale" contract.
struct ClipScaleMemory {
    std::optional<synth::MusicalScale> scale;
    bool pitchVisibilityOn = false;
};
