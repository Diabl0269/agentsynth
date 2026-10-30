// Concern: the Song key picker's Fit project to key button end to end: its enabled state, the one undo step, the
// message and the clips it leaves alone.
// Regression test for FRO1237: fitting the project changed a clip that has its own key.
#include "../../TestWait.h"
#include "../PianoRoll/PianoRollTestHelpers.h"
#include "AppUndoManager.h"
#include "Music/SongKey.h"
#include "TestMode/TestModeStateSource.h"
#include "Transport/TransportService.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Timeline/SongKeyField.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <gtest/gtest.h>

namespace {

using namespace synth;
using synth::ui::KeyPicker;

struct DrumHost : AuditionRecordingHost {
    TrackId drumTrack;
    bool drumKitRows(TrackId t, synth::ui::KitRows*) const override { return t == drumTrack; }
};

struct FitProjectFixture {
    TimelineDoc doc;
    AppUndoManager undo;
    TransportService transport;
    DrumHost host;
    synth::ui::TimelinePanelComponent panel;
    std::unique_ptr<juce::Component> launched;
    std::vector<juce::String> messages;
    ClipId followA, followB, own, kit, audio;

    FitProjectFixture() {
        panel.setSize(1200, 400);
        panel.setTimelineDoc(&doc);
        panel.setUndoManager(&undo);
        panel.setTransport(&transport);
        panel.setTrackHeaderHost(&host);
        panel.onShowMessage = [this](const juce::String& m) { messages.push_back(m); };
        field().pickerLauncher = [this](std::unique_ptr<juce::Component> p, juce::Rectangle<int>) {
            launched = std::move(p);
        };

        const auto lead = doc.addTrack(TrackKind::Midi, "Lead");
        const auto drums = doc.addTrack(TrackKind::Midi, "Drums");
        const auto audioTrack = doc.addTrack(TrackKind::Audio, "Audio");
        host.drumTrack = drums;
        followA = doc.addClip(lead, 0.0, 4.0, "A");
        followB = doc.addClip(lead, 4.0, 4.0, "B");
        own = doc.addClip(lead, 8.0, 4.0, "Own");
        kit = doc.addClip(drums, 0.0, 4.0, "Kit");
        audio = doc.addClip(audioTrack, 0.0, 4.0, "Take");
        for (const auto id : {followA, followB, own, kit})
            doc.addNote(id, MidiNote{{}, 0.0, 1.0, 61});
        doc.setClipScale(own, makeScale(1, scalePresetIndex("Major")), false);
        doc.setClipAsset(audio, "Audio/take.wav", 0.0);
    }
    ~FitProjectFixture() { panel.setTrackHeaderHost(nullptr); }

    synth::ui::SongKeyField& field() { return panel.getTransportBar().getSongKeyField(); }
    KeyPicker& openPicker() {
        field().openPicker();
        auto* picker = dynamic_cast<KeyPicker*>(launched.get());
        EXPECT_NE(picker, nullptr);
        return *picker;
    }
    int pitch(ClipId id) const { return doc.getClip(id)->notes.front().pitch; }
};

} // namespace

TEST(FitProjectToKeyTest, TheButtonIsGreyedWithNoSongKeyAndDoesNothing) {
    FitProjectFixture f;
    auto& picker = f.openPicker();
    EXPECT_TRUE(picker.isFitProjectShown());
    EXPECT_FALSE(picker.isFitProjectEnabled());
    picker.clickFitProject();
    EXPECT_EQ(f.pitch(f.followA), 61);
    EXPECT_FALSE(f.undo.canUndo());
    EXPECT_TRUE(f.messages.empty());
}

TEST(FitProjectToKeyTest, FitsTheClipsFollowingTheSongKeyAsOneUndoStepAndSaysSo) {
    FitProjectFixture f;
    f.transport.setSongKey(makeScale(0, scalePresetIndex("Major")));
    auto& picker = f.openPicker();
    ASSERT_TRUE(picker.isFitProjectEnabled());
    picker.clickFitProject();

    EXPECT_EQ(f.pitch(f.followA), 60);
    EXPECT_EQ(f.pitch(f.followB), 60);
    EXPECT_EQ(f.pitch(f.own), 61);
    EXPECT_EQ(f.pitch(f.kit), 61) << "a drum kit track is not fitted";
    EXPECT_EQ(f.doc.getClip(f.audio)->assetRef, "Audio/take.wav");
    ASSERT_EQ(f.messages.size(), 1u);
    EXPECT_EQ(f.messages.front(),
              "Fitted 2 clips to C Major. Skipped 1 with their own key. Audio clips were left alone.");

    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    EXPECT_EQ(f.pitch(f.followA), 61);
    EXPECT_EQ(f.pitch(f.followB), 61);
    EXPECT_FALSE(f.undo.canUndo()) << "one step restored everything";
}

TEST(FitProjectToKeyTest, FittingTwiceRecordsNoSecondStepAndSaysNothingMoved) {
    FitProjectFixture f;
    f.transport.setSongKey(makeScale(0, scalePresetIndex("Major")));
    f.openPicker().clickFitProject();
    f.launched.reset();
    f.undo.undo();
    f.doc.moveNote(f.doc.getClip(f.followA)->notes.front().id, 0.0, 60);
    f.doc.moveNote(f.doc.getClip(f.followB)->notes.front().id, 0.0, 60);
    f.messages.clear();

    f.openPicker().clickFitProject();
    EXPECT_FALSE(f.undo.canUndo());
    ASSERT_EQ(f.messages.size(), 1u);
    EXPECT_TRUE(f.messages.front().startsWith("Nothing moved")) << f.messages.front().toStdString();
}

TEST(FitProjectToKeyTest, TheClipLaneReportsEveryClipsPitchesToTestMode) {
    FitProjectFixture f;
    auto* probe = f.panel.getClipLaneArea().findChildWithID("timelineClipPitches");
    ASSERT_NE(probe, nullptr);
    juce::DynamicObject state;
    dynamic_cast<synth::testmode::StateSource*>(probe)->addTestModeState(state);
    EXPECT_EQ(state.getProperty("clipPitches").toString(), "A:61;B:61;Own:61;Kit:61;Take:");
}
