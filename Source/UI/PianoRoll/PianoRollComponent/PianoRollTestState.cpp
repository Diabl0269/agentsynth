// PianoRollComponent -- the test-mode probe: a hidden child that answers getState with the roll's own zoom. The class
// is declared in PianoRollComponent.h.

#include "PianoRollComponent.h"
#include "TestMode/TestModeStateSource.h"

namespace synth::ui {

namespace {

// A child that only answers test-mode getState, so the roll's own header stays out of TestModeStateSource.h.
class ZoomProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit ZoomProbe(const PianoRollComponent& roll)
        : roll_(roll) {
        setComponentID("pianoRollZoomState");
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }
    // `pixelsPerBeat` and `pixelsPerSemitone`: the roll's zoom on both axes.
    void addTestModeState(juce::DynamicObject& into) const override {
        into.setProperty("pixelsPerBeat", roll_.getPixelsPerBeat());
        into.setProperty("pixelsPerSemitone", roll_.getPixelsPerSemitone());
    }

private:
    const PianoRollComponent& roll_;
};

} // namespace

void PianoRollComponent::addTestModeProbe() {
    testModeProbe_ = std::make_unique<ZoomProbe>(*this);
    testModeProbe_->setBounds(0, 0, 1, 1);
    addAndMakeVisible(*testModeProbe_);
}

} // namespace synth::ui
