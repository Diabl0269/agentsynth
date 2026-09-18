// Concern: FRO125's transport family (play, stop, the togglePlayback alias, toggle loop, record,
// toggle metronome, return to start) -- the docs/midi_remote.md §4.9 prerequisite that promotes
// every transport verb to a command-dispatched AppCommands id, so a MIDI Remote action target can
// invokeDirectly() it. Two halves: table-shape assertions against a bare ShortcutManager
// (ShortcutManagerTestFixture.h, mirroring the Export Patch Only block in
// ShortcutManagerActionRegressionTests.cpp), and invokeDirectly reachability against a real
// MainComponent + AudioEngine (mirroring FocusArbitrationPlaybackDeleteTests.cpp's
// SpaceTogglesPlayback), since a bare ShortcutManager has no ApplicationCommandManager to invoke
// through.
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManagerTestFixture.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>

namespace {

// Every new action id, unbound by design -- see ShortcutManager::getActionTable()'s own comment.
const juce::StringArray& transportActionIds() {
    static const juce::StringArray ids{
        "transportPlay",          "transportStop", "transportToggleLoop", "transportRecord", "transportToggleMetronome",
        "transportReturnToStart",
    };
    return ids;
}

// A provider that never touches the network -- this file only exercises transport/command
// plumbing. Mirrors FocusArbitrationTestFixture.h's FocusMockProvider.
class TransportTestProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "TransportTestMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (callback)
            callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{false, {}, {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    int requestTimeoutMs = 240000;
};

// TransportToggleMetronomeFlipsMetronomeAndPersists writes "timelineMetronomeEnabled"/
// "timelineCountInBars" into the SAME on-disk "Agent Synth" settings file every MainComponent in
// this process reads (see TimelineTransportBar::setApplicationProperties) -- reset before AND
// after, the same idiom FocusArbitrationTestFixture.h's resetTimelinePanelVisibleKey uses, so this
// test can never leak into another test's defaults or the developer's own real settings.
void resetMetronomeKeys() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("timelineMetronomeEnabled", "0");
        s->removeValue("timelineCountInBars");
        s->saveIfNeeded();
    }
}

} // namespace

// ============================================================================
// Table shape: action exists, has a command id, a display name, a category.
// ============================================================================

TEST_F(ShortcutManagerTest, TransportActionIdsExistAndHaveNoDefaultBinding) {
    for (const auto& actionId : transportActionIds()) {
        EXPECT_TRUE(manager.getActionIds().contains(actionId)) << actionId << " is not a registered action";
        EXPECT_FALSE(manager.getBinding(actionId).isValid()) << actionId << " should ship unbound by default";
    }
}

TEST_F(ShortcutManagerTest, TransportActionIdsResolveToTheirOwnCommand) {
    EXPECT_EQ(AppCommands::getCommandForAction("transportPlay"), AppCommands::transportPlay);
    EXPECT_EQ(AppCommands::getCommandForAction("transportStop"), AppCommands::transportStop);
    EXPECT_EQ(AppCommands::getCommandForAction("transportToggleLoop"), AppCommands::transportToggleLoop);
    EXPECT_EQ(AppCommands::getCommandForAction("transportRecord"), AppCommands::transportRecord);
    EXPECT_EQ(AppCommands::getCommandForAction("transportToggleMetronome"), AppCommands::transportToggleMetronome);
    EXPECT_EQ(AppCommands::getCommandForAction("transportReturnToStart"), AppCommands::transportReturnToStart);
}

TEST_F(ShortcutManagerTest, TransportActionIdsHaveDisplayNamesAndAreFiledUnderGeneral) {
    for (const auto& actionId : transportActionIds()) {
        EXPECT_NE(ShortcutManager::getActionDescription(actionId), actionId)
            << actionId << " has no human-readable description";
        EXPECT_EQ(ShortcutManager::getCategory(actionId), ShortcutCategory::General) << actionId;
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutCategory::General).contains(actionId))
            << actionId << " is not filed under General";
    }
}

// The alias (transportTogglePlayStop -> togglePlayback) is regression-tested in
// ShortcutManagerActionRegressionTests.cpp instead of here, per the ticket's own file split --
// this file just confirms its description reads sensibly for a MIDI Remote picker.
TEST_F(ShortcutManagerTest, TransportTogglePlayStopHasADisplayName) {
    EXPECT_NE(ShortcutManager::getActionDescription("transportTogglePlayStop"),
              juce::String("transportTogglePlayStop"));
}

// ============================================================================
// invokeDirectly reachability -- needs a real MainComponent + AudioEngine, unlike the bare
// ShortcutManager above.
// ============================================================================

class ShortcutManagerTransportActionsInvokeTest : public ::testing::Test {
protected:
    void SetUp() override { resetMetronomeKeys(); }
    void TearDown() override { resetMetronomeKeys(); }
};

TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportPlayStartsTransportAndIsIdempotent) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    engine.processHostBlock(buffer, midi);
    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportPlay, false));
    engine.processHostBlock(buffer, midi);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);

    // Idempotent: Play while already playing must not stop it (it is a direction, not a toggle).
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportPlay, false));
    engine.processHostBlock(buffer, midi);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
}

TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportStopStopsTransportAndIsIdempotent) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    transport.play();
    engine.processHostBlock(buffer, midi);
    ASSERT_TRUE(transport.getPositionSnapshot().playing);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportStop, false));
    engine.processHostBlock(buffer, midi);
    EXPECT_FALSE(transport.getPositionSnapshot().playing);

    // Idempotent: Stop while already stopped must not throw or restart it.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportStop, false));
    engine.processHostBlock(buffer, midi);
    EXPECT_FALSE(transport.getPositionSnapshot().playing);
}

TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportToggleLoopFlipsLooping) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    engine.processHostBlock(buffer, midi);
    const bool loopingBefore = transport.getPositionSnapshot().looping;

    // Routes through the transport bar's own loop button (triggerClick() POSTS its click) -- pump
    // the message loop before draining the resulting transport command, the same idiom
    // SpaceTogglesPlayback uses for togglePlayback's identical triggerClick() body.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportToggleLoop, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    engine.processHostBlock(buffer, midi);
    EXPECT_EQ(transport.getPositionSnapshot().looping, !loopingBefore);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportToggleLoop, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    engine.processHostBlock(buffer, midi);
    EXPECT_EQ(transport.getPositionSnapshot().looping, loopingBefore);
}

TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportToggleMetronomeFlipsMetronomeAndPersists) {
    MainComponent mc(std::make_unique<TransportTestProvider>());
    auto& cm = mc.getCommandManager();
    auto& metronome = mc.getAudioEngine().getMetronome();
    const bool enabledBefore = metronome.isEnabled();

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportToggleMetronome, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(metronome.isEnabled(), !enabledBefore);
    EXPECT_EQ(mc.getTimelinePanel().getTransportBar().getMetronomeButton().getToggleState(), !enabledBefore);
}

TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportRecordRoutesThroughTheArmedTrackGate) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    engine.processHostBlock(buffer, midi);
    ASSERT_FALSE(transport.getPositionSnapshot().playing);
    ASSERT_FALSE(mc.getMidiRecorderForTest().isRecording());

    // Nothing armed: transportRecord still reaches handleRecordToggle's "record implies roll"
    // behaviour (proving it went through the gate, not a bare toggle), but no take starts.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportRecord, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    engine.processHostBlock(buffer, midi);
    EXPECT_TRUE(transport.getPositionSnapshot().playing) << "record implies roll, even with nothing armed";
    EXPECT_TRUE(mc.getTimelinePanel().getTransportBar().isRecordingForTest());
    EXPECT_FALSE(mc.getMidiRecorderForTest().isRecording()) << "nothing armed -- no take should start";

    // Off, then arm a MIDI track and try again -- this time the take must actually start.
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportRecord, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    engine.processHostBlock(buffer, midi);
    transport.stop();
    engine.processHostBlock(buffer, midi);

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    doc.setTrackArmed(trackA, true);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportRecord, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    engine.processHostBlock(buffer, midi);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
    EXPECT_TRUE(mc.getMidiRecorderForTest().isRecording()) << "an armed MIDI track must actually start a take";
}

// Deterministic regression test for the FRO210 CI flake in the test above: handleRecordToggle()
// (MainComponentSetupTimeline.cpp) calls transport.play() -- which only POSTS a command, taking
// effect on the next processHostBlock -- and THEN MidiRecorder::startRecording(), synchronously.
// MainComponent's 10 Hz commit-on-stop poll (timerCallback(), MainComponentCallbacks.cpp) commits
// whenever wasTransportPlaying_ is stale true, the published snapshot still says not-playing and
// isRecording() is true. If that tick lands in the gap between startRecording() and the posted
// Play command actually landing, it sees exactly that combination and cancels the take it never
// saw start -- even though nothing is actually wrong. On CI this needed the real 10 Hz timer to
// land inside a ~50ms window (rare, hence the flake); stopping the real timer and firing
// timerCallback() by hand makes the same race land every run.
TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportRecordSurvivesATimerTickBetweenTakeStartAndTransportDrain) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    mc.stopTimer(); // drive the 10 Hz commit-on-stop poll by hand instead of by wall clock
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Get to "was playing" the way a prior roll would, and let the timer observe it -- this is
    // what makes wasTransportPlaying_ stale-true available for the race below.
    ASSERT_TRUE(transport.play());
    engine.processHostBlock(buffer, midi);
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    mc.timerCallback();

    // Stop directly, bypassing the record-off click path -- exactly like the flaky test's raw
    // transport.stop() between its two record cycles. Nothing re-observes this transition before
    // the explicit timerCallback() call below, so wasTransportPlaying_ stays stale true.
    ASSERT_TRUE(transport.stop());
    engine.processHostBlock(buffer, midi);
    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    doc.setTrackArmed(trackA, true);

    // Same reachability path as the test above: invoke the command, pump only far enough for the
    // posted click to run (the real timer is stopped, so it can't coincidentally self-heal here).
    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportRecord, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc.getMidiRecorderForTest().isRecording())
        << "the click must have started the take before the race window below";

    // The race: fire the commit-on-stop tick before the posted Play command has been drained.
    mc.timerCallback();

    engine.processHostBlock(buffer, midi);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
    EXPECT_TRUE(mc.getMidiRecorderForTest().isRecording())
        << "a commit-on-stop tick landing between take-start and the transport catching up must "
           "not cancel a take it never saw start";
}

// Companion to the test above, guarding the fix's other side: Record pressed while ALREADY
// playing takes the "no pre-roll, no transport.play()" branch in handleRecordToggle (see its
// count-in comment), so wasTransportPlaying_ must be re-anchored to true (snap.playing), not
// unconditionally cleared -- otherwise a Stop shortly after Record would never auto-commit, since
// the edge-detector would have no "was playing" observation to compare against.
TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportRecordEngagedMidRollStillAutoCommitsOnAQuickStop) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    mc.stopTimer(); // drive the 10 Hz commit-on-stop poll by hand instead of by wall clock
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    // Already rolling before Record is pressed -- the mid-performance branch.
    ASSERT_TRUE(transport.play());
    engine.processHostBlock(buffer, midi);
    ASSERT_TRUE(transport.getPositionSnapshot().playing);

    auto& doc = mc.getTimelineDoc();
    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "A");
    doc.setTrackArmed(trackA, true);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportRecord, false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_TRUE(mc.getMidiRecorderForTest().isRecording());

    // Stop right away, then a single tick -- no intervening tick ever observed "playing" for
    // this take, so only a correctly re-anchored wasTransportPlaying_ catches this edge.
    ASSERT_TRUE(transport.stop());
    engine.processHostBlock(buffer, midi);
    ASSERT_FALSE(transport.getPositionSnapshot().playing);

    mc.timerCallback();

    EXPECT_FALSE(mc.getMidiRecorderForTest().isRecording())
        << "record engaged mid-roll, then stopped within one tick, must still auto-commit";
}

TEST_F(ShortcutManagerTransportActionsInvokeTest, TransportReturnToStartLocatesToBeatZero) {
    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine(AudioEngine::HostMode::Hosted);
    engine.initialise();
    engine.prepareForHost(44100.0, 512, 0, 2);
    MainComponent mc(tm, lf, engine, std::make_unique<TransportTestProvider>());
    auto& cm = mc.getCommandManager();
    auto& transport = engine.getTransport();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;

    transport.locateBeat(8.0);
    engine.processHostBlock(buffer, midi);
    ASSERT_GT(transport.getPositionSnapshot().ppq, 0.0);

    ASSERT_TRUE(cm.invokeDirectly(AppCommands::transportReturnToStart, false));
    engine.processHostBlock(buffer, midi);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 0.0);
}
