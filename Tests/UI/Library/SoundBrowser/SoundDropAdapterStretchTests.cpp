// A loop dragged out of the Sounds view and dropped on a target that takes stretched loops arrives as a copy at the
// song's tempo; a target that does not, and a one-shot, get the original file.
#include "../../../SoundBrowser/SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include <gtest/gtest.h>
#include <juce_audio_formats/juce_audio_formats.h>

using synth::soundbrowser::LoopTarget;
using synth::ui::SoundDragState;

namespace {

template <bool kStretched>
class AudioTarget
    : public juce::Component
    , public juce::FileDragAndDropTarget
    , public synth::ui::SoundDropAdapter {
public:
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override { dropped = files; }
    bool takesStretchedLoops() const override { return kStretched; }
    juce::StringArray dropped;
};

int lengthOf(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    return reader == nullptr ? -1 : (int)reader->lengthInSamples;
}

struct Fixture {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("drop-stretch-" + juce::Uuid().toDashedString());
    juce::File loop = dir.getChildFile("loop_120.wav");
    Fixture() { sound_browser_test::writeSignal(loop, 4.0, 44100.0, 0.5f, 220.0, 2); }
    ~Fixture() {
        SoundDragState::end();
        dir.deleteRecursively();
    }
    juce::DragAndDropTarget::SourceDetails details(const juce::File& file) const {
        return {synth::ui::makeSoundPayload(file.getFullPathName()), nullptr, {3, 4}};
    }
};

} // namespace

TEST(SoundDropAdapterStretchTest, ALoopDroppedOnAnAudioTrackArrivesAtTheSongTempo) {
    Fixture f;
    AudioTarget<true> track;
    SoundDragState::begin(f.loop, LoopTarget{120.0, 96.0, 0});
    SoundDragState::end();
    track.itemDropped(f.details(f.loop));
    ASSERT_EQ(track.dropped.size(), 1);
    const juce::File offered(track.dropped[0]);
    EXPECT_NE(offered, f.loop);
    EXPECT_TRUE(offered.hasFileExtension("wav"));
    EXPECT_NEAR(lengthOf(offered), 5.0 * 44100.0, 2.0) << "1.25 times the 4 s original";
    // Only its own copy: the folder is shared with whichever test shard is rendering into it now.
    offered.deleteFile();
}

TEST(SoundDropAdapterStretchTest, ATargetThatDoesNotTakeStretchedLoopsGetsTheOriginal) {
    Fixture f;
    AudioTarget<false> sampler;
    SoundDragState::begin(f.loop, LoopTarget{120.0, 96.0, 0});
    SoundDragState::end();
    sampler.itemDropped(f.details(f.loop));
    ASSERT_EQ(sampler.dropped.size(), 1);
    EXPECT_EQ(sampler.dropped[0], f.loop.getFullPathName());
}

TEST(SoundDropAdapterStretchTest, ASoundWithNothingToStretchGetsTheOriginal) {
    Fixture f;
    AudioTarget<true> track;
    SoundDragState::begin(f.loop, LoopTarget{}); // a one-shot, or a loop of unknown tempo
    SoundDragState::end();
    track.itemDropped(f.details(f.loop));
    EXPECT_EQ(track.dropped[0], f.loop.getFullPathName());

    SoundDragState::begin(f.loop, LoopTarget{120.0, 120.0, 0}); // already at the song's tempo
    SoundDragState::end();
    track.itemDropped(f.details(f.loop));
    EXPECT_EQ(track.dropped[0], f.loop.getFullPathName());
}

TEST(SoundDropAdapterStretchTest, AnotherFileIsNeverGivenAnEarlierDragsTarget) {
    Fixture f;
    const auto other = f.dir.getChildFile("other.wav");
    sound_browser_test::writeSignal(other, 2.0, 44100.0, 0.5f, 220.0, 1);
    AudioTarget<true> track;
    SoundDragState::begin(f.loop, LoopTarget{120.0, 96.0, 0});
    SoundDragState::end();
    track.itemDropped(f.details(other));
    EXPECT_EQ(track.dropped[0], other.getFullPathName());
}

TEST(SoundDropAdapterStretchTest, AKeyShiftAloneIsRenderedToo) {
    Fixture f;
    AudioTarget<true> track;
    SoundDragState::begin(f.loop, LoopTarget{120.0, 120.0, 3});
    SoundDragState::end();
    track.itemDropped(f.details(f.loop));
    const juce::File offered(track.dropped[0]);
    EXPECT_NE(offered, f.loop);
    EXPECT_NEAR(lengthOf(offered), 4.0 * 44100.0, 2.0);
}
