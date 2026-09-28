// MidiLearnController's thin wiring around ControllerHandshakeCoordinator --
// setHandshakeFeedbackSink()/shutdownHandshakes(), and that every profiles_-mutating call
// (addProfile here) reconciles it via setProfilesAndReconcileHandshakes(). Same HostMode::Hosted +
// hostSourceKey() fixture shape as MidiLearnControllerTests.cpp, so a profile's device can be
// "open" with no real audio device or MIDI hardware. Suite name contains "MidiRemote" per the
// ship-task --gtest_filter convention (see
// docs/control/midi-remote-device-handshake.md#device-handshake).

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

class FakeFeedbackSink : public RemoteFeedbackSink {
public:
    std::vector<juce::MidiMessage> sent;
    void sendFeedback(const ControllerProfile::Input&, const juce::MidiMessage& message) override {
        sent.push_back(message);
    }
};

ControllerProfile makeHandshakeProfile(const juce::String& id) {
    ControllerProfile p;
    p.id = id;
    p.name = id;
    p.input.identifier = hostSourceKey(); // Hosted mode's only ever-"open" source -- see fixture comment
    p.input.name = hostSourceKey();
    p.handshake.openMessage = {0xF0, 0x7F, 0xF7};
    p.handshake.closeMessage = {0xF0, 0x00, 0xF7};
    return p;
}

class MidiLearnControllerHandshakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midilearncontroller-handshake-tests-" + juce::Uuid().toString());
        root_.deleteRecursively();
        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        graphEditor_ = std::make_unique<GraphEditor>(*engine_);
        controller_ = std::make_unique<MidiLearnController>(*engine_, *graphEditor_, remoteEngine_, doc_, undo_,
                                                            statusBar_, synth::ControllerProfileStore(root_));
        // A headless test process has no CoreMIDI entitlement/bundle -- see
        // setAvailableOutputsQueryForTest()'s own doc comment. Hosted mode's own hostSourceKey()
        // "output" is appended by reconcileHandshakes() itself regardless of what this returns, so
        // an empty list here is enough for every test in this fixture.
        controller_->setAvailableOutputsQueryForTest([] { return std::vector<ControllerProfile::Input>{}; });
    }

    void TearDown() override { root_.deleteRecursively(); }

    juce::File root_;
    RemoteEngine remoteEngine_;
    synth::MidiRemoteProjectDoc doc_;
    AppUndoManager undo_;
    StatusBarComponent statusBar_;
    std::unique_ptr<AudioEngine> engine_;
    std::unique_ptr<GraphEditor> graphEditor_;
    std::unique_ptr<MidiLearnController> controller_;
};

} // namespace

TEST_F(MidiLearnControllerHandshakeTest, NothingSentBeforeAFeedbackSinkIsWired) {
    // addProfile() reconciles unconditionally -- with no sink wired yet, that must be a no-op, not
    // a null-dereference.
    ASSERT_TRUE(controller_->addProfile(makeHandshakeProfile("p1")));
    controller_->shutdownHandshakes(); // likewise a no-op
    SUCCEED();
}

TEST_F(MidiLearnControllerHandshakeTest, AddProfileSendsOpenOnceTheSinkIsWired) {
    FakeFeedbackSink sink;
    controller_->setHandshakeFeedbackSink(sink);
    EXPECT_TRUE(sink.sent.empty()) << "no profile with a handshake exists yet";

    ASSERT_TRUE(controller_->addProfile(makeHandshakeProfile("p1")));
    ASSERT_EQ(sink.sent.size(), 1u);
    EXPECT_EQ(sink.sent[0].getRawData()[1], 0x7F); // the open byte
}

TEST_F(MidiLearnControllerHandshakeTest, ShutdownHandshakesSendsCloseForEveryOpenProfile) {
    FakeFeedbackSink sink;
    controller_->setHandshakeFeedbackSink(sink);
    ASSERT_TRUE(controller_->addProfile(makeHandshakeProfile("p1")));
    ASSERT_EQ(sink.sent.size(), 1u);

    controller_->shutdownHandshakes();
    ASSERT_EQ(sink.sent.size(), 2u);
    EXPECT_EQ(sink.sent[1].getRawData()[1], 0x00); // the close byte

    // A second call has nothing left to close.
    controller_->shutdownHandshakes();
    EXPECT_EQ(sink.sent.size(), 2u);
}

TEST_F(MidiLearnControllerHandshakeTest, GetHandshakeIssueForProfileIsEmptyBeforeASinkIsWiredAndForACleanProfile) {
    EXPECT_TRUE(controller_->getHandshakeIssueForProfile("p1").isEmpty()) << "no coordinator wired yet";

    FakeFeedbackSink sink;
    controller_->setHandshakeFeedbackSink(sink);
    ASSERT_TRUE(controller_->addProfile(makeHandshakeProfile("p1")));
    // hostSourceKey() resolves by identifier both as the input and (reconcileHandshakes()'s own
    // synthetic entry) the output -- no port hint, no mismatch, so there is nothing to report.
    EXPECT_TRUE(controller_->getHandshakeIssueForProfile("p1").isEmpty());
}

// ReconcileHandshakes() unconditionally adds a {hostSourceKey(), hostSourceKey()} output for Hosted
// mode (see its own comment), so a Hosted-mode profile's handshake always resolves by identity
// regardless of `input.name` -- but describeHandshakeIssue()'s port-hint check is independent of
// that, and still fires whenever the declared hint isn't in the input's own name. This proves that
// path reaches all the way through MidiLearnController's wiring, not just
// ControllerHandshakeCoordinator directly (already covered by ControllerHandshakeTests.cpp).
TEST_F(MidiLearnControllerHandshakeTest, GetHandshakeIssueForProfileReportsAPortHintMismatch) {
    FakeFeedbackSink sink;
    controller_->setHandshakeFeedbackSink(sink);
    auto profile = makeHandshakeProfile("p1");
    profile.handshake.port = "DAW"; // input.name (hostSourceKey()) doesn't contain "DAW"
    ASSERT_TRUE(controller_->addProfile(profile));

    ASSERT_EQ(sink.sent.size(), 1u) << "still resolves and sends -- the hint mismatch is a separate warning";
    EXPECT_FALSE(controller_->getHandshakeIssueForProfile("p1").isEmpty());
}
