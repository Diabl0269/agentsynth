// AutomationLanesLaneMainTests.cpp -- the lane menu's Change parameter and Duplicate against a real MainComponent:
// the host resolves the picked parameter's real range, Cmd+D reaches the lane through the app's own key handler, and
// each edit is one undo step.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AutomationLanesMenuFixture.h"
#include "MainComponent/MainComponent.h"
#include "UserSettings.h"

using namespace lane_menu_test;

namespace {

class QuietProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Quiet"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"Model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
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
    juce::String model = "Model";
    int requestTimeoutMs = 240000;
};

// Adding a lane opens the bottom dock, which persists "bottomDockVisible" to the properties file every
// MainComponent reads: reset it around each test so no outcome depends on execution order.
void resetBottomDockVisibleKey() {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    if (auto* s = props.getUserSettings()) {
        s->setValue("bottomDockVisible", "0");
        s->saveIfNeeded();
    }
}

class LaneMainTest : public ::testing::Test {
protected:
    void SetUp() override { resetBottomDockVisibleKey(); }
    void TearDown() override { resetBottomDockVisibleKey(); }
};

juce::AudioProcessorGraph::Node::Ptr withUuid(juce::AudioProcessorGraph::Node::Ptr node) {
    const auto uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node;
}

// A MIDI track playing Track In -> Oscillator -> Filter, with a lane on the filter's cutoff holding three points.
struct LaneScene {
    MainComponent mc{std::make_unique<QuietProvider>()};
    synth::TrackId track;
    synth::LaneId lane;
    juce::String filterUuid;

    LaneScene() {
        mc.simulateAddMidiTrackClick();
        auto& graph = mc.getAudioEngine().getGraph();
        track = mc.getTimelineDoc().getTracks().front().id;
        const auto trackInUuid = mc.getTimelineDoc().getTracks().front().bindingUuid;
        juce::AudioProcessorGraph::Node::Ptr trackIn;
        for (auto* node : graph.getNodes())
            if (node->properties["uuid"].toString() == trackInUuid)
                trackIn = node;
        auto osc = withUuid(graph.addNode(synth::AIStateMapper::createModule("Oscillator")));
        auto filter = withUuid(graph.addNode(synth::AIStateMapper::createModule("Filter")));
        filterUuid = filter->properties["uuid"].toString();
        EXPECT_TRUE(trackIn != nullptr);
        graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                             {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
        graph.addConnection({{osc->nodeID, 0}, {filter->nodeID, 0}});
        mc.automateParameter(filter->nodeID, "cutoff");
        lane = doc().getTrack(track)->lanes.front().id;
        const auto& range = doc().getLane(lane)->range;
        doc().addBreakpoint(lane, 0.0, range.minValue);
        doc().addBreakpoint(lane, 2.0, range.minValue + 0.25 * (range.maxValue - range.minValue));
        doc().addBreakpoint(lane, 4.0, range.maxValue);
        doc().setLaneRecordMode(lane, static_cast<int>(synth::LaneRecordMode::Touch));
        mc.getTimelinePanel().showAutomationLane(lane);
        if (auto* welcome = mc.getWelcomeScreenForTest())
            welcome->setVisible(false);
    }

    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }

    // Picks the first row matching `search` in `picker`.
    static void pick(synth::ui::ModMatrixPicker& picker, const juce::String& search) {
        picker.setSearchTextForTest(search);
        ASSERT_FALSE(picker.getVisibleItemTextsForTest().empty()) << "no picker row matches " << search;
        picker.chooseVisibleItemForTest(0);
    }
};

} // namespace

TEST_F(LaneMainTest, ChangeParameterReadsTheNewParametersRealRangeFromTheGraphInOneUndoStep) {
    LaneScene s;
    PickerCapture capture;
    const auto oldRange = s.doc().getLane(s.lane)->range;
    s.mc.getTimelinePanel().laneHeaderForTest(s.lane)->openChangeParameterPicker();
    ASSERT_NE(capture.picker, nullptr);

    LaneScene::pick(*capture.picker, "resonance");

    const auto* lane = s.doc().getLane(s.lane);
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->paramId, "resonance");
    EXPECT_EQ(lane->nodeUuid, s.filterUuid);
    EXPECT_FALSE(lane->range.minValue == oldRange.minValue && lane->range.maxValue == oldRange.maxValue)
        << "the range is the new parameter's own";
    ASSERT_EQ(lane->points.size(), 3u);
    const double span = (double)lane->range.maxValue - (double)lane->range.minValue;
    EXPECT_NEAR(lane->points[1].value, lane->range.minValue + 0.25 * span, 1.0e-4) << "the curve keeps its shape";
    EXPECT_EQ(lane->recordMode, static_cast<int>(synth::LaneRecordMode::Touch));

    ASSERT_TRUE(s.mc.getUndoManager().undo());
    EXPECT_EQ(s.doc().getLane(s.lane)->paramId, "cutoff");
    EXPECT_EQ(s.doc().getLane(s.lane)->points.size(), 3u);
}

TEST_F(LaneMainTest, CmdDOnTheLaneSurfaceOpensThePickerAndAPickMakesTheCopyBelowInOneUndoStep) {
    LaneScene s;
    PickerCapture capture;
    s.mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::AutomationLane);
    const auto before = s.doc().getTrack(s.track)->lanes.size();

    EXPECT_TRUE(s.mc.keyPressed(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    ASSERT_NE(capture.picker, nullptr) << "Cmd+D opens the picker for the focused lane";
    EXPECT_EQ(s.doc().getTrack(s.track)->lanes.size(), before) << "nothing exists until a parameter is picked";

    LaneScene::pick(*capture.picker, "resonance");

    const auto& lanes = s.doc().getTrack(s.track)->lanes;
    ASSERT_EQ(lanes.size(), before + 1);
    EXPECT_EQ(lanes[0].id, s.lane);
    EXPECT_EQ(lanes[1].paramId, "resonance") << "directly below the source";
    EXPECT_EQ(lanes[1].points.size(), lanes[0].points.size());
    EXPECT_EQ(lanes[1].recordMode, lanes[0].recordMode);
    EXPECT_NEAR(lanes[1].points[1].value,
                lanes[1].range.minValue + 0.25 * ((double)lanes[1].range.maxValue - (double)lanes[1].range.minValue),
                1.0e-4);

    ASSERT_TRUE(s.mc.getUndoManager().undo());
    EXPECT_EQ(s.doc().getTrack(s.track)->lanes.size(), before) << "one Cmd+Z removes the copy";
}

TEST_F(LaneMainTest, CmdDWithAPickerDismissedLeavesTheDocUntouched) {
    LaneScene s;
    PickerCapture capture;
    s.mc.setEditSurfaceOverrideForTest(MainComponent::EditSurface::AutomationLane);
    const auto revision = s.doc().getRevision();

    s.mc.keyPressed(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    ASSERT_NE(capture.picker, nullptr);
    capture.picker.reset();

    EXPECT_EQ(s.doc().getRevision(), revision);
}
