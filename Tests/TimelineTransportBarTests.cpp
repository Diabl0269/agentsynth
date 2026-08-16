// TimelineTransportBarTests.cpp
//
// The timeline panel's transport bar — play/stop/record/loop + BPM/time-signature editors +
// the bar:beat readout — and the app-level MidiRecorder wiring the record button needs.
//
// Two groups of coverage, the same split TimelinePanelTests.cpp/TimelinePlayheadTests.cpp use:
//   1. synth::ui::TimelineTransportBar in isolation, driven with a raw synth::TransportService (or,
//      for the pure formatBarBeat table and the readout's repaint-count seam, no transport at all).
//      None of this is SYNTH_ENABLE_TIMELINE-gated — the bar itself always compiles.
//   2. MainComponent's record wiring — gated, because MidiRecorder's app-level wiring (the armed-
//      track lookup, the auto-commit-on-stop poll) compiles out with the flag off.

#include "../Source/AI/AIProvider.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Transport/TransportService.h"
#include "../Source/UI/TimelinePanelComponent.h"
#include "../Source/UI/TimelineTransportBar.h"
#include "MainComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// 1. synth::ui::TimelineTransportBar in isolation.
// ============================================================================

// ---- 1. FormatBarBeatTable ----

TEST(TimelineTransportBarTest, FormatBarBeatTable) {
    using synth::ui::TimelineTransportBar;

    EXPECT_EQ(TimelineTransportBar::formatBarBeat(0.0, 4, 4), "001.1.000");
    EXPECT_EQ(TimelineTransportBar::formatBarBeat(4.0, 4, 4), "002.1.000");
    EXPECT_EQ(TimelineTransportBar::formatBarBeat(5.5, 4, 4), "002.2.480");
    EXPECT_EQ(TimelineTransportBar::formatBarBeat(3.0, 3, 4), "002.1.000");
    EXPECT_EQ(TimelineTransportBar::formatBarBeat(0.25, 4, 4), "001.1.240");
}

// ---- 2. ButtonsDriveTransport ----

TEST(TimelineTransportBarTest, ButtonsDriveTransport) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineTransportBar bar;
    bar.setTransport(&transport);
    bar.setSize(500, 28);

    // Click play -> (tick) playing.
    bar.getPlayStopButton().onClick();
    transport.tick(512);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);

    // Click again -> stopped. The play/stop click reads the transport's CURRENT snapshot (not a
    // cached button state), so this needs no updateFromTransport() resync in between.
    bar.getPlayStopButton().onClick();
    transport.tick(512);
    EXPECT_FALSE(transport.getPositionSnapshot().playing);

    // Loop toggle with no bounds ever set on this transport -> its own [0, 4) default.
    bar.getLoopButton().onClick();
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 0.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 4.0);

    // Toggle off, set custom bounds directly, toggle on again -> preserves them rather than
    // resetting to the default.
    bar.getLoopButton().onClick();
    transport.tick(512);
    ASSERT_TRUE(transport.setLoop(8.0, 16.0, false));
    transport.tick(512);
    bar.getLoopButton().onClick();
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 8.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 16.0);
}

// ---- 3. BpmAndTimeSigEditorsApply ----

TEST(TimelineTransportBarTest, BpmAndTimeSigEditorsApply) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);

    synth::ui::TimelineTransportBar bar;
    bar.setTransport(&transport);
    bar.setSize(500, 28);

    bar.getBpmLabel().setText("140.0", juce::sendNotificationSync);
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().bpm, 140.0);

    bar.getTimeSigLabel().setText("3/4", juce::sendNotificationSync);
    transport.tick(512);
    auto snap = transport.getPositionSnapshot();
    EXPECT_EQ(snap.timeSigNumerator, 3);
    EXPECT_EQ(snap.timeSigDenominator, 4);

    // "4/5": denominator 5 is not an accepted note-value denominator, so setTimeSignature rejects
    // it outright (no command posted) and the label must revert to the last known-good text.
    bar.getTimeSigLabel().setText("4/5", juce::sendNotificationSync);
    EXPECT_EQ(bar.getTimeSigLabel().getText(), "3/4");
    transport.tick(512);
    snap = transport.getPositionSnapshot();
    EXPECT_EQ(snap.timeSigNumerator, 3);
    EXPECT_EQ(snap.timeSigDenominator, 4);
}

// ---- 4. ReadoutRepaintsOnlyOnStringChange ----

TEST(TimelineTransportBarTest, ReadoutRepaintsOnlyOnStringChange) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(800, 28);

    synth::TransportService::PositionSnapshot snap;
    snap.ppq = 0.0;
    snap.timeSigNumerator = 4;
    snap.timeSigDenominator = 4;

    bar.updateFromTransport(snap);
    EXPECT_EQ(bar.getReadoutRepaintCountForTest(), 1);
    EXPECT_EQ(bar.getReadoutTextForTest(), "001.1.000");

    // Same tick value again -> no additional repaint.
    bar.updateFromTransport(snap);
    EXPECT_EQ(bar.getReadoutRepaintCountForTest(), 1);

    // Crosses a tick boundary -> one more repaint.
    snap.ppq = 0.5;
    bar.updateFromTransport(snap);
    EXPECT_EQ(bar.getReadoutRepaintCountForTest(), 2);
}

// ---- 5. SnapshotSmoke ----

TEST(TimelineTransportBarTest, SnapshotSmoke) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(800, 28);

    auto stopped = bar.createComponentSnapshot(bar.getLocalBounds());
    EXPECT_FALSE(stopped.isNull());
    EXPECT_EQ(stopped.getWidth(), 800);
    EXPECT_EQ(stopped.getHeight(), 28);

    bar.getPlayStopButton().setToggleState(true, juce::dontSendNotification);
    bar.getLoopButton().setToggleState(true, juce::dontSendNotification);
    bar.setRecordingState(true);

    auto playingAndRecording = bar.createComponentSnapshot(bar.getLocalBounds());
    EXPECT_FALSE(playingAndRecording.isNull());
    EXPECT_EQ(playingAndRecording.getWidth(), 800);
    EXPECT_EQ(playingAndRecording.getHeight(), 28);
}

// ============================================================================
// 2. MainComponent's record wiring.
// ============================================================================

#if SYNTH_ENABLE_TIMELINE

class MockProviderTB : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTB"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }

private:
    juce::String model = "MockModel";
};

class TimelineTransportBarAppWiringTest : public ::testing::Test {
protected:
    // Same hermetic reset as TimelinePanelTests.cpp/TimelinePlayheadTests.cpp: MainComponent's
    // delegating ctor reads the shared on-disk "Agent Synth" properties.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("timelinePanelVisible", "0");
            s->saveIfNeeded();
        }
    }

    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }

    // Nothing here wants a real device clocking the graph underneath it: the transport is ticked
    // by hand and MidiRecorder is driven directly, exactly like MidiRecorderTests.cpp's own Harness.
    static void quiesceEngine(MainComponent& mc) { mc.getAudioEngine().suspendDeviceCallback(); }
};

// ---- 6. RecordRequiresArmedTrack ----

TEST_F(TimelineTransportBarAppWiringTest, RecordRequiresArmedTrack) {
    MainComponent mc(std::make_unique<MockProviderTB>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& transport = mc.getAudioEngine().getTransport();

    // No armed track anywhere in the doc: the click is refused, the button snaps back off, a
    // status message appears, and the transport is left untouched (no side effect on a rejection).
    bar.getRecordButton().onClick();
    EXPECT_FALSE(bar.isRecordingForTest());
    EXPECT_FALSE(mc.getMidiRecorderForTest().isRecording());
    EXPECT_EQ(mc.getStatusBar().getTransientMessageForTest(), "Arm a track to record");
    transport.tick(512);
    EXPECT_FALSE(transport.getPositionSnapshot().playing);

    // Arm a track: the SAME click now succeeds — capture starts and record implies roll.
    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackArmed(trackId, true));

    bar.getRecordButton().onClick();
    EXPECT_TRUE(bar.isRecordingForTest());
    EXPECT_TRUE(mc.getMidiRecorderForTest().isRecording());

    transport.tick(512); // drains the play() posted by "record implies roll"
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
}

// ---- 7. StopWhileRecordingCommitsOnce ----

TEST_F(TimelineTransportBarAppWiringTest, StopWhileRecordingCommitsOnce) {
    MainComponent mc(std::make_unique<MockProviderTB>());
    mc.setSize(1600, 900);
    quiesceEngine(mc);

    auto& doc = mc.getTimelineDoc();
    const auto trackId = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    ASSERT_TRUE(doc.setTrackArmed(trackId, true));

    auto& bar = mc.getTimelinePanel().getTransportBar();
    auto& transport = mc.getAudioEngine().getTransport();
    auto& recorder = mc.getMidiRecorderForTest();

    bar.getRecordButton().onClick();
    ASSERT_TRUE(bar.isRecordingForTest());
    transport.tick(512); // drains the play() posted by "record implies roll"
    mc.timerCallback();  // wasTransportPlaying_ catches up to true — still playing, no commit
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    ASSERT_TRUE(recorder.isRecording());

    // Capture one note across two blocks — driven exactly like MidiRecorderTests.cpp's Harness
    // (transport.tick() then recorder.captureBlock() against the returned BlockTimeInfo).
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 10);
        const auto& info = transport.tick(512);
        recorder.captureBlock(midi, info);
    }
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 20);
        const auto& info = transport.tick(512);
        recorder.captureBlock(midi, info);
    }

    ASSERT_TRUE(transport.stop());
    transport.tick(512); // drains stop()
    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    mc.timerCallback(); // playing -> stopped while recording: auto-commits exactly once
    EXPECT_FALSE(bar.isRecordingForTest());
    EXPECT_FALSE(recorder.isRecording());
    ASSERT_EQ(doc.getTrack(trackId)->clips.size(), 1u);
    EXPECT_TRUE(mc.getUndoManager().canUndo());

    // A second poll tick must not double-commit — the transport is already stopped, so the
    // wasTransportPlaying_ edge never re-fires.
    mc.timerCallback();
    EXPECT_EQ(doc.getTrack(trackId)->clips.size(), 1u);
}

#endif // SYNTH_ENABLE_TIMELINE
