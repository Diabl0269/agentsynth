#include "TestMode/TestModeStateSource.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"

namespace synth::ui {

namespace {

// A child of the lane that only answers test-mode getState, so the lane's own header stays out of
// TestModeStateSource.h.
class ClipPitchesProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit ClipPitchesProbe(std::function<void(juce::DynamicObject&)> fill)
        : fill_(std::move(fill)) {
        setComponentID("timelineClipPitches");
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override { fill_(into); }

private:
    std::function<void(juce::DynamicObject&)> fill_;
};

} // namespace

// `clipPitches`, "Clip 1:60,64;Clip 2:61": every clip's note pitches in track order. Pitches rather than a count,
// because a scenario checking Fit to key needs to see that notes moved, and which way.
void TimelineClipLaneArea::addTestModeProbe() {
    testModeProbe_ = std::make_unique<ClipPitchesProbe>([this](juce::DynamicObject& into) {
        juce::StringArray clips;
        if (doc_ != nullptr)
            for (const auto& track : doc_->getTracks())
                for (const auto& clip : track.clips) {
                    juce::StringArray pitches;
                    for (const auto& note : clip.notes)
                        pitches.add(juce::String(note.pitch));
                    clips.add(clip.name + ":" + pitches.joinIntoString(","));
                }
        into.setProperty("clipPitches", clips.joinIntoString(";"));
    });
    testModeProbe_->setBounds(0, 0, 1, 1);
    addAndMakeVisible(*testModeProbe_);
}

} // namespace synth::ui
