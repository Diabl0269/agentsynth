// TimelineTrackDeleteKeepMacroTests.cpp: deleting a track whose macro would go with it asks Remove macro / Keep macro /
// Cancel (Remember my choice) from every route (the row menu, Cmd+Backspace, several selected rows). Keep macro drops
// only the track row: the macro, its modules, its bound node, its mixer strip and every cable stay, the strip shows as
// a channel no track feeds, and Cmd+Z brings the track back in one step. Drives a real off-screen MainComponent through
// the row's real menu item and key, with the dialog answered by hand through its test hook. The plain question for a
// track with no macro is TimelineTrackDeleteConfirmTests.cpp; the reverse question (delete the macro of a track) is in
// TimelineTrackDeleteMacroTests.cpp.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <set>

namespace {

using synth::ui::DeleteTrackMacroChoice;

constexpr const char* kKeepAskKey = "timelineAskBeforeDeletingTrack";
constexpr const char* kKeepChoiceKey = "timelineDeleteTrackMacroChoice";

class MockProviderKM : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockKM"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct Snapshot {
    std::set<juce::String> nodes;
    std::set<juce::String> cables;
    juce::var macros;
    juce::var timeline;
};

// Holds the macro question open like a window would, counts it and keeps its words; the plain question is counted too,
// so a test can tell which of the two a gesture raised. Clears the two shared settings before and after.
class KeepMacroFixture : public ::testing::Test {
protected:
    void SetUp() override {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc.simulateAddAudioTrackClick();
        mc.getTimelinePanel().setSize(1400, 360);
        clearSettings();
        synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest() = [this](const auto& t, auto done) {
            ++macroAsked;
            macroText = t;
            macroPending = std::move(done);
        };
        synth::ui::test_hooks::deleteTrackConfirmHookForTest() = [this](const auto&, auto done) {
            ++plainAsked;
            plainPending = std::move(done);
        };
    }
    void TearDown() override {
        synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest() = nullptr;
        synth::ui::test_hooks::deleteTrackConfirmHookForTest() = nullptr;
        clearSettings();
    }

    juce::PropertiesFile& settings() { return *mc.getAppPropertiesForTest().getUserSettings(); }
    void clearSettings() {
        settings().removeValue(kKeepAskKey);
        settings().removeValue(kKeepChoiceKey);
        settings().saveIfNeeded();
    }
    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }
    synth::MacroSet& macros() { return mc.getGraphEditor().getMacros(); }
    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }
    size_t trackCount() { return doc().getTracks().size(); }
    juce::String boundUuid(int trackIndex) { return doc().getTracks()[(size_t)trackIndex].bindingUuid; }
    synth::TrackId trackId(int trackIndex) { return doc().getTracks()[(size_t)trackIndex].id; }
    juce::AudioProcessorGraph::Node* nodeByUuid(const juce::String& uuid) {
        for (auto* node : graph().getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return node;
        return nullptr;
    }
    Snapshot snapshot() {
        Snapshot s;
        const auto uuidOf = [this](juce::AudioProcessorGraph::NodeID id) {
            auto* node = graph().getNodeForId(id);
            return node != nullptr ? node->properties["uuid"].toString() : juce::String();
        };
        for (auto* node : graph().getNodes())
            s.nodes.insert(node->properties["uuid"].toString());
        for (const auto& c : graph().getConnections())
            s.cables.insert(uuidOf(c.source.nodeID) + ":" + juce::String(c.source.channelIndex) + ">" +
                            uuidOf(c.destination.nodeID) + ":" + juce::String(c.destination.channelIndex));
        s.macros = macros().toVar();
        s.timeline = doc().toVar();
        return s;
    }
    // A header's context-menu Delete item, then Cmd+Backspace on a header.
    void clickDeleteMenuItem(int index) {
        auto* header = mc.getTimelinePanel().getTrackHeaderAt(index);
        ASSERT_NE(header, nullptr);
        header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kDeleteTrackMenuId);
    }
    bool pressCmdBackspace(int index) {
        auto* header = mc.getTimelinePanel().getTrackHeaderAt(index);
        return header != nullptr &&
               header->keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0));
    }
    void answerMacro(DeleteTrackMacroChoice choice, bool remember = false) {
        ASSERT_TRUE(macroPending) << "no macro question is open";
        auto done = std::move(macroPending);
        macroPending = nullptr;
        done(choice, remember);
    }
    // Every channel strip the mixer lists, and how many of them no track feeds.
    int stripColumns() {
        int n = 0;
        for (const auto& c : synth::buildMixerSnapshot(graph(), doc(), macros()).columns)
            n += c.kind == synth::MixerColumn::Kind::Strip || c.kind == synth::MixerColumn::Kind::Bus ? 1 : 0;
        return n;
    }
    int trackLessStripColumns() {
        int n = 0;
        for (const auto& c : synth::buildMixerSnapshot(graph(), doc(), macros()).columns)
            n += (c.kind == synth::MixerColumn::Kind::Strip || c.kind == synth::MixerColumn::Kind::Bus) &&
                         c.feedingTracks.empty()
                     ? 1
                     : 0;
        return n;
    }

    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderKM>()};
    int macroAsked = 0;
    int plainAsked = 0;
    synth::ui::DeleteTrackConfirmText macroText;
    std::function<void(DeleteTrackMacroChoice, bool)> macroPending;
    std::function<void(bool, bool)> plainPending;
};

} // namespace

TEST_F(KeepMacroFixture, TheMenuItemAsksAboutTheMacroAndChangesNothingUntilAnswered) {
    const auto before = snapshot();
    clickDeleteMenuItem(1);
    EXPECT_EQ(macroAsked, 1);
    EXPECT_EQ(plainAsked, 0) << "one question, the macro one";
    EXPECT_EQ(macroText.title, "Delete " + doc().getTracks()[1].name + "?");
    EXPECT_TRUE(macroText.message.contains("Keep macro")) << "the words name both answers";
    EXPECT_EQ(trackCount(), 3u) << "the dialog is still open";
    EXPECT_EQ(snapshot().nodes, before.nodes);
}

TEST_F(KeepMacroFixture, RemoveMacroTakesTheTrackItsMacroAndItsModulesAsOneUndoStep) {
    const auto before = snapshot();
    const auto macroId = macros().findByMember(boundUuid(1))->id;
    clickDeleteMenuItem(1);
    answerMacro(DeleteTrackMacroChoice::removeMacro);

    EXPECT_EQ(trackCount(), 2u);
    EXPECT_EQ(macros().find(macroId), nullptr);
    EXPECT_LT(snapshot().nodes.size(), before.nodes.size());

    ASSERT_TRUE(mc.getUndoManager().undo()) << "one step";
    const auto restored = snapshot();
    EXPECT_EQ(restored.nodes, before.nodes);
    EXPECT_EQ(restored.cables, before.cables);
    EXPECT_TRUE(synth::sameJson(restored.macros, before.macros));
    EXPECT_TRUE(synth::sameJson(restored.timeline, before.timeline));
}

TEST_F(KeepMacroFixture, KeepMacroDropsOnlyTheTrackAndLeavesTheMacroItsModulesAndItsChannel) {
    const auto before = snapshot();
    const auto macroId = macros().findByMember(boundUuid(1))->id;
    const auto boundNode = boundUuid(1);
    const int stripsBefore = stripColumns();
    ASSERT_EQ(trackLessStripColumns(), 0);

    clickDeleteMenuItem(1);
    answerMacro(DeleteTrackMacroChoice::keepMacro);

    EXPECT_EQ(trackCount(), 2u) << "the track row is gone";
    const auto after = snapshot();
    EXPECT_EQ(after.nodes, before.nodes) << "every module stays";
    EXPECT_EQ(after.cables, before.cables) << "every cable stays";
    EXPECT_TRUE(synth::sameJson(after.macros, before.macros)) << "the macro keeps its members, ports, place and fold";
    ASSERT_NE(macros().find(macroId), nullptr);
    ASSERT_NE(nodeByUuid(boundNode), nullptr) << "its Track In / Track Audio node stays, unclaimed";
    for (const auto& track : doc().getTracks())
        EXPECT_NE(track.bindingUuid, boundNode) << "no track plays it any more";
    EXPECT_EQ(stripColumns(), stripsBefore) << "the mixer still lists the channel";
    EXPECT_EQ(trackLessStripColumns(), 1) << "as a channel no track feeds";
}

TEST_F(KeepMacroFixture, KeepMacroIsOneUndoStepAndRedoDropsTheTrackAgain) {
    const auto before = snapshot();
    clickDeleteMenuItem(0);
    answerMacro(DeleteTrackMacroChoice::keepMacro);
    const auto kept = snapshot();

    ASSERT_TRUE(mc.getUndoManager().undo()) << "one step";
    EXPECT_EQ(trackCount(), 3u);
    const auto restored = snapshot();
    EXPECT_EQ(restored.nodes, before.nodes);
    EXPECT_TRUE(synth::sameJson(restored.timeline, before.timeline)) << "the track is back with its lanes";
    EXPECT_TRUE(synth::sameJson(restored.macros, before.macros));
    EXPECT_EQ(trackLessStripColumns(), 0) << "the channel is fed by its track again";

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(trackCount(), 2u);
    EXPECT_TRUE(synth::sameJson(snapshot().timeline, kept.timeline));
}

TEST_F(KeepMacroFixture, CancelChangesNothingAndStoresNothingEvenWithRememberTicked) {
    const auto before = snapshot();
    clickDeleteMenuItem(1);
    answerMacro(DeleteTrackMacroChoice::cancel, true);
    EXPECT_EQ(trackCount(), 3u);
    EXPECT_TRUE(synth::sameJson(snapshot().timeline, before.timeline));
    EXPECT_TRUE(settings().getBoolValue(kKeepAskKey, true));
    EXPECT_FALSE(settings().containsKey(kKeepChoiceKey));
}

TEST_F(KeepMacroFixture, CmdBackspaceAsksTheMacroQuestionNotThePlainOne) {
    EXPECT_TRUE(pressCmdBackspace(2));
    EXPECT_EQ(macroAsked, 1);
    EXPECT_EQ(plainAsked, 0);
    answerMacro(DeleteTrackMacroChoice::keepMacro);
    EXPECT_EQ(trackCount(), 2u);
    EXPECT_EQ(stripColumns(), 3);
}

TEST_F(KeepMacroFixture, ATrackWithNoMacroKeepsItsOldBehaviour) {
    mc.simulateAddMidiTrackClick(); // a MIDI track's node sits in no macro
    const int last = static_cast<int>(trackCount()) - 1;
    ASSERT_EQ(macros().findByMember(boundUuid(last)), nullptr);

    clickDeleteMenuItem(last);
    EXPECT_EQ(macroAsked + plainAsked, 0) << "the menu item deletes it at once, as it always did";
    EXPECT_EQ(trackCount(), 3u);

    mc.simulateAddMidiTrackClick();
    EXPECT_TRUE(pressCmdBackspace(static_cast<int>(trackCount()) - 1));
    EXPECT_EQ(macroAsked, 0);
    EXPECT_EQ(plainAsked, 1) << "Cmd+Backspace asks the plain question";
}

TEST_F(KeepMacroFixture, RememberKeepStopsTheQuestionAndKeepsTheMacroEveryTime) {
    clickDeleteMenuItem(2);
    answerMacro(DeleteTrackMacroChoice::keepMacro, true);
    EXPECT_FALSE(settings().getBoolValue(kKeepAskKey, true));
    EXPECT_EQ(settings().getValue(kKeepChoiceKey), "keep");

    const auto nodes = snapshot().nodes;
    clickDeleteMenuItem(1);
    EXPECT_EQ(macroAsked, 1) << "not asked again";
    EXPECT_EQ(trackCount(), 1u);
    EXPECT_EQ(snapshot().nodes, nodes) << "the macro stayed";
}

TEST_F(KeepMacroFixture, RememberRemoveRemovesTheMacroWithoutAsking) {
    clickDeleteMenuItem(2);
    answerMacro(DeleteTrackMacroChoice::removeMacro, true);
    EXPECT_EQ(settings().getValue(kKeepChoiceKey), "remove");
    const auto nodes = snapshot().nodes.size();
    clickDeleteMenuItem(1);
    EXPECT_EQ(macroAsked, 1);
    EXPECT_LT(snapshot().nodes.size(), nodes);
}

TEST_F(KeepMacroFixture, TheTrackDeleteSettingAloneMeansRemoveAsItAlwaysDid) {
    settings().setValue(kKeepAskKey, "0"); // "Don't ask again" on the plain question, no macro answer ever stored
    const auto nodes = snapshot().nodes.size();
    clickDeleteMenuItem(1);
    EXPECT_EQ(macroAsked, 0);
    EXPECT_EQ(trackCount(), 2u);
    EXPECT_LT(snapshot().nodes.size(), nodes);
}

TEST_F(KeepMacroFixture, ThePreferencesSwitchForgetsTheRememberedAnswerAndAsksAgain) {
    clickDeleteMenuItem(2);
    answerMacro(DeleteTrackMacroChoice::keepMacro, true);
    PreferencesSettingsTab tab(mc.getAppPropertiesForTest());
    ASSERT_FALSE(tab.isAskBeforeDeletingTrackEnabled());
    tab.setAskBeforeDeletingTrackEnabled(true);
    EXPECT_FALSE(settings().containsKey(kKeepChoiceKey));

    clickDeleteMenuItem(1);
    EXPECT_EQ(macroAsked, 2) << "asks again";
    EXPECT_EQ(trackCount(), 2u) << "and deleted nothing yet";
}

TEST_F(KeepMacroFixture, SeveralSelectedTracksAskOnceAndKeepMacroKeepsEveryMacro) {
    const auto before = snapshot();
    mc.getTimelinePanel().selectTracks({trackId(0), trackId(1)}, trackId(1));
    clickDeleteMenuItem(1);
    EXPECT_EQ(macroAsked, 1);
    EXPECT_EQ(macroText.title, "Delete 2 tracks?");
    answerMacro(DeleteTrackMacroChoice::keepMacro);
    EXPECT_EQ(trackCount(), 1u);
    EXPECT_EQ(snapshot().nodes, before.nodes);
    EXPECT_EQ(trackLessStripColumns(), 2);

    ASSERT_TRUE(mc.getUndoManager().undo()) << "both tracks back in one step";
    EXPECT_EQ(trackCount(), 3u);
}

TEST_F(KeepMacroFixture, SeveralSelectedTracksRemoveMacroTakesEveryMacroInOneUndoStep) {
    const auto before = snapshot();
    mc.getTimelinePanel().selectTracks({trackId(0), trackId(2)}, trackId(0));
    EXPECT_TRUE(pressCmdBackspace(0));
    EXPECT_EQ(macroAsked, 1);
    answerMacro(DeleteTrackMacroChoice::removeMacro);
    EXPECT_EQ(trackCount(), 1u);
    EXPECT_EQ(macros().size(), 1);

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(trackCount(), 3u);
    EXPECT_EQ(snapshot().nodes, before.nodes);
    EXPECT_TRUE(synth::sameJson(snapshot().macros, before.macros));
}
