// EditRefreshWorkTests.cpp -- what an edit, and its undo, refresh in the UI is the part that changed, each
// whole-project pass runs once per edit, and the end state is the one a full refresh would give. Counted with the
// panels' test counters (TimelinePanelComponent::getTrackHeadersBuiltForTest,
// TimelineAutomationLanes::getRoutingDerivationsForTest).
#include "AI/AIProvider.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/AutomationLanes/TimelineAutomationLanes/TimelineAutomationLanes.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <map>

namespace {

class MockProviderERW : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockERW"; }
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

struct EditRig {
    MainComponent mc{std::make_unique<MockProviderERW>()};

    EditRig() {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < 3; ++i)
            mc.simulateAddAudioTrackClick();
    }
    synth::ui::TimelinePanelComponent& panel() { return mc.getTimelinePanel(); }
    int derivations() { return panel().getAutomationLanes().getRoutingDerivationsForTest(); }
    std::map<std::int64_t, synth::ui::TimelineTrackHeaderComponent*> headers() {
        std::map<std::int64_t, synth::ui::TimelineTrackHeaderComponent*> out;
        for (int i = 0; i < panel().getTrackHeaderCount(); ++i)
            if (auto* header = panel().getTrackHeaderAt(i))
                out[header->getTrackId().value] = header;
        return out;
    }
    size_t publishedTracks() { return mc.getAudioEngine().getTimelineSnapshots().beginAudioBlock().tracks.size(); }
    void duplicateFirst() {
        // TrackHeaderHost is a private base of MainComponent; the C-style cast is the header's own call path.
        ((synth::ui::TrackHeaderHost&)mc).duplicateTrack(mc.getTimelineDoc().getTracks().front().id);
    }
    juce::AudioProcessorParameter* firstStripGain() {
        for (auto* node : mc.getAudioEngine().getGraph().getNodes())
            if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
                for (auto* param : node->getProcessor()->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param); r && r->getParameterID() == "gain")
                        return param;
        return nullptr;
    }
};

} // namespace

TEST(EditRefreshWork, AKnobUndoDerivesTheModulatorRowsOnce) {
    EditRig rig;
    auto* gain = rig.firstStripGain();
    ASSERT_NE(gain, nullptr);
    auto& undo = rig.mc.getUndoManager();
    auto& graph = rig.mc.getAudioEngine().getGraph();
    undo.captureBeforeState(graph);
    gain->setValueNotifyingHost(gain->getValue() > 0.5f ? 0.1f : 0.9f);
    undo.pushSnapshotFromCapture(graph);

    const int before = rig.derivations();
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(rig.derivations() - before, 1) << "once, by the reconcile after the step, not again per restored action";
}

TEST(EditRefreshWork, ADuplicateAndItsUndoBuildOnlyTheCopysHeaderRow) {
    EditRig rig;
    const auto before = rig.headers();
    ASSERT_EQ(before.size(), 3u);
    const int built = rig.panel().getTrackHeadersBuiltForTest();

    rig.duplicateFirst();
    auto after = rig.headers();
    ASSERT_EQ(after.size(), 4u);
    EXPECT_EQ(rig.panel().getTrackHeadersBuiltForTest(), built + 1) << "only the copy's row is built";
    for (const auto& [id, header] : before)
        EXPECT_EQ(after[id], header) << "every other row is kept";

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_EQ(rig.panel().getTrackHeadersBuiltForTest(), built + 1) << "the undo builds no row";
    EXPECT_EQ(rig.headers(), before);
    for (int i = 0; i < rig.panel().getTrackHeaderCount(); ++i)
        EXPECT_EQ(rig.panel().getTrackHeaderAt(i)->getTrackId(), rig.mc.getTimelineDoc().getTracks()[(size_t)i].id)
            << "rows in track order";
}

TEST(EditRefreshWork, ADuplicateDerivesRoutingsTwiceAndEndsPublished) {
    EditRig rig;
    const int before = rig.derivations();
    rig.duplicateFirst();
    EXPECT_EQ(rig.derivations() - before, 2) << "the doc change's sync and the reconcile, never per updateComponents";
    EXPECT_EQ(rig.publishedTracks(), rig.mc.getTimelineDoc().getTracks().size()) << "the reconcile published the copy";

    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    EXPECT_EQ(rig.publishedTracks(), 3u) << "the undo's doc change is published by the reconcile after the step";
    ASSERT_TRUE(rig.mc.getUndoManager().redo());
    EXPECT_EQ(rig.publishedTracks(), 4u);
}

TEST(EditRefreshWork, ADuplicateAndItsUndoReReadEachKeptRowOnce) {
    EditRig rig;
    std::map<std::int64_t, int> refreshes;
    for (const auto& [id, header] : rig.headers())
        refreshes[id] = header->getRefreshCountForTest();

    rig.duplicateFirst();
    for (const auto& [id, header] : rig.headers())
        if (refreshes.count(id) != 0)
            EXPECT_EQ(header->getRefreshCountForTest() - refreshes[id], 1)
                << "the reconcile re-reads each row; the doc change in the middle of the edit does not";

    for (const auto& [id, header] : rig.headers())
        refreshes[id] = header->getRefreshCountForTest();
    ASSERT_TRUE(rig.mc.getUndoManager().undo());
    for (const auto& [id, header] : rig.headers())
        EXPECT_EQ(header->getRefreshCountForTest() - refreshes[id], 1) << "and once for the undo step";
}

// An undo step of actions that fire no restore hooks (here a macro-only change) has no reconcile after it, so the
// catch-all after its canvas refresh must still run its passes rather than wait for one.
TEST(EditRefreshWork, AMacroOnlyUndoStillRunsTheCatchAll) {
    EditRig rig;
    auto& macros = rig.mc.getGraphEditor().getMacros();
    ASSERT_FALSE(macros.getAll().empty()) << "each audio track is boxed in its channel macro";
    const auto macroId = macros.getAll().front().id;
    auto& undo = rig.mc.getUndoManager();
    undo.recordGraphAndMacroChange(rig.mc.getAudioEngine().getGraph(), macros, [&] {
        if (auto* macro = macros.find(macroId))
            macro->name = "Renamed";
    });

    const int before = rig.derivations();
    ASSERT_TRUE(undo.undo());
    EXPECT_NE(macros.find(macroId)->name, juce::String("Renamed"));
    EXPECT_EQ(rig.derivations() - before, 1) << "the catch-all re-derived the modulator rows; no reconcile follows";
}
