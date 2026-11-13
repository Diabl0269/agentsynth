#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelineClipLaneArea/WaveformSampleCache.h"

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

const char* waveformSourceName(WaveformSource source) {
    switch (source) {
    case WaveformSource::Peaks:
        return "peaks";
    case WaveformSource::SampleColumns:
        return "samples";
    case WaveformSource::SampleLine:
        return "line";
    case WaveformSource::None:
        break;
    }
    return "none";
}

} // namespace

// `clipAudio`, "Clip 1:Audio/a.wav,0.000,4.000,Audio/orig.wav@0.000*1.500;...": every audio clip's asset, source start
// in seconds, length in beats and stretch origin ("-" when none), so a scenario can tell which file a clip plays.
// `clipWaveform`, "Clip 1:columns;Clip 2:summary": what each audio clip's waveform is drawn as (none, summary,
// columns). `waveformSource`: what the last waveform paint drew from (peaks, samples, line, none). `sizingReadout`: the
// pill an edge drag draws on the clip ("200%" while stretching), empty when none. `clipLengths`, "Clip 1:4.000;Clip
// 2:2.500": every clip's length in beats, so a scenario can see whether a drag committed. `clipPitches`, "Clip
// 1:60,64;Clip 2:61": every clip's note pitches in track order. Pitches rather than a count, because a scenario
// checking Fit to key needs to see that notes moved, and which way. `clipDrawn`, "Clip 1:80,10,160,50;...": every
// clip's rectangle as drawn right now (x,y,width,height; it trails the clip's place while a snapped drag step or a
// restore glides), and `clipGliding`: whether a clip glide runs.
void TimelineClipLaneArea::addTestModeProbe() {
    testModeProbe_ = std::make_unique<ClipPitchesProbe>([this](juce::DynamicObject& into) {
        juce::StringArray clips;
        juce::StringArray lengths;
        juce::StringArray audio;
        juce::StringArray shapes;
        juce::StringArray drawn;
        if (doc_ != nullptr)
            for (const auto& track : doc_->getTracks())
                for (const auto& clip : track.clips) {
                    juce::StringArray pitches;
                    for (const auto& note : clip.notes)
                        pitches.add(juce::String(note.pitch));
                    lengths.add(clip.name + ":" + juce::String(clip.lengthBeats, 3));
                    const auto rect = getDrawnClipRectForTest(clip.id);
                    drawn.add(clip.name + ":" + juce::String(rect.getX()) + "," + juce::String(rect.getY()) + "," +
                              juce::String(rect.getWidth()) + "," + juce::String(rect.getHeight()));
                    clips.add(clip.name + ":" + pitches.joinIntoString(","));
                    if (clip.assetRef.isNotEmpty()) {
                        shapes.add(clip.name + ":" + getWaveformShapeForTest(clip.id));
                        audio.add(clip.name + ":" + clip.assetRef + "," + juce::String(clip.sourceStartSeconds, 3) +
                                  "," + juce::String(clip.lengthBeats, 3) + "," + originText(clip));
                    }
                }
        into.setProperty("clipPitches", clips.joinIntoString(";"));
        into.setProperty("clipLengths", lengths.joinIntoString(";"));
        into.setProperty("clipDrawn", drawn.joinIntoString(";"));
        into.setProperty("clipGliding", clipGlide_->isAnimating());
        into.setProperty("clipAudio", audio.joinIntoString(";"));
        into.setProperty("clipWaveform", shapes.joinIntoString(";"));
        into.setProperty("waveformSource", waveformSourceName(lastWaveformSource_));
        into.setProperty("sizingReadout", sizingReadout());
    });
    testModeProbe_->setBounds(0, 0, 1, 1);
    addAndMakeVisible(*testModeProbe_);
}

juce::String TimelineClipLaneArea::getWaveformShapeForTest(synth::ClipId id) {
    const auto* clip = doc_ != nullptr ? doc_->getClip(id) : nullptr;
    if (clip == nullptr)
        return "none";
    switch (waveformShapeFor(*clip, getDrawnClipRectForTest(id).getWidth())) {
    case WaveformShape::Summary:
        return "summary";
    case WaveformShape::Columns:
        return "columns";
    case WaveformShape::None:
        break;
    }
    return "none";
}

} // namespace synth::ui
