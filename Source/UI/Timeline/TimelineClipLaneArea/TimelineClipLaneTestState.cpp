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

// "ref@offset*ratio" of a stretched clip's origin, "-" when it has none.
juce::String originText(const synth::Clip& clip) {
    if (!clip.stretchOrigin)
        return "-";
    return clip.stretchOrigin->assetRef + "@" + juce::String(clip.stretchOrigin->offsetSeconds, 3) + "*" +
           juce::String(clip.stretchOrigin->ratio, 3);
}

} // namespace

// `clipAudio`, "Clip 1:Audio/a.wav,0.000,4.000,Audio/orig.wav@0.000*1.500;...": every audio clip's asset, source start
// in seconds, length in beats and stretch origin ("-" when none), so a scenario can tell which file a clip plays.
// `clipPitches`, "Clip 1:60,64;Clip 2:61": every clip's note pitches in track order. Pitches rather than a count,
// because a scenario checking Fit to key needs to see that notes moved, and which way.
void TimelineClipLaneArea::addTestModeProbe() {
    testModeProbe_ = std::make_unique<ClipPitchesProbe>([this](juce::DynamicObject& into) {
        juce::StringArray clips;
        juce::StringArray audio;
        if (doc_ != nullptr)
            for (const auto& track : doc_->getTracks())
                for (const auto& clip : track.clips) {
                    juce::StringArray pitches;
                    for (const auto& note : clip.notes)
                        pitches.add(juce::String(note.pitch));
                    clips.add(clip.name + ":" + pitches.joinIntoString(","));
                    if (clip.assetRef.isNotEmpty())
                        audio.add(clip.name + ":" + clip.assetRef + "," + juce::String(clip.sourceStartSeconds, 3) +
                                  "," + juce::String(clip.lengthBeats, 3) + "," + originText(clip));
                }
        into.setProperty("clipPitches", clips.joinIntoString(";"));
        into.setProperty("clipAudio", audio.joinIntoString(";"));
    });
    testModeProbe_->setBounds(0, 0, 1, 1);
    addAndMakeVisible(*testModeProbe_);
}

} // namespace synth::ui
