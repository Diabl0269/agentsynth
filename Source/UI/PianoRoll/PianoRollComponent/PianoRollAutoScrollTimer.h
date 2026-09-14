// PianoRollAutoScrollTimer.h — the edge-auto-scroll timer nested type. #included mid-class-body
// from PianoRollComponent.h at the exact spot it used to be defined inline, so it stays nested
// (PianoRollComponent::AutoScrollTimer) with no change to any qualified reference elsewhere.

// A NESTED juce::Timer rather than a second responsibility multiplexed onto the class's own
// private juce::Timer base (used above for the one-shot quantise flash, timerCallback()) —
// one juce::Timer answering to two unrelated reasons would need a mode flag in every callback,
// exactly the kind of "which timer is this tick for" bug a dedicated Timer avoids by
// construction. autoScrollTick() (the protected virtual seam a test overrides) does the real
// work; this struct only forwards juce::Timer's callback to it.
struct AutoScrollTimer final : public juce::Timer {
    explicit AutoScrollTimer(PianoRollComponent& ownerRef)
        : owner(ownerRef) {}
    void timerCallback() override { owner.autoScrollTick(); }
    PianoRollComponent& owner;
};
