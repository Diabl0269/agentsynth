// AutomationLanesAddMainTests.cpp -- adding a lane from the timeline against a real MainComponent: what a
// track's "Add automation..." picker lists (the modules the track plays, grouped by module, without the hidden
// helper nodes or what already has a lane), what the Unassigned section lists, and that a pick lands the lane
// on the track the picker was opened for. The panel's own row and menu are in AutomationLanesAddRowTests.cpp.

#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

using synth::ui::TrackHeaderHost;

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

class AddFromTimelineMainTest : public ::testing::Test {
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

// Track 1 (Track In -> Oscillator -> Filter) built the way the app's own gestures leave it, plus an
// Attenuverter cabled into the chain (a hidden helper the picker must never offer).
struct Chain {
    juce::AudioProcessorGraph::Node::Ptr osc, filter, attenuverter, loose;
    synth::TrackId track;
};

Chain buildChain(MainComponent& mc) {
    mc.simulateAddMidiTrackClick();
    auto& graph = mc.getAudioEngine().getGraph();
    Chain chain;
    chain.track = mc.getTimelineDoc().getTracks().front().id;
    const auto trackInUuid = mc.getTimelineDoc().getTracks().front().bindingUuid;
    juce::AudioProcessorGraph::Node::Ptr trackIn;
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == trackInUuid)
            trackIn = node;
    chain.osc = withUuid(graph.addNode(synth::AIStateMapper::createModule("Oscillator")));
    chain.filter = withUuid(graph.addNode(synth::AIStateMapper::createModule("Filter")));
    chain.attenuverter = withUuid(graph.addNode(synth::AIStateMapper::createModule("Attenuverter")));
    chain.loose = withUuid(graph.addNode(synth::AIStateMapper::createModule("Filter")));
    EXPECT_TRUE(trackIn != nullptr);
    EXPECT_TRUE(graph.addConnection({{trackIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {chain.osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    EXPECT_TRUE(graph.addConnection({{chain.osc->nodeID, 0}, {chain.filter->nodeID, 0}}));
    EXPECT_TRUE(graph.addConnection({{chain.filter->nodeID, 0}, {chain.attenuverter->nodeID, 0}}));
    return chain;
}

std::set<juce::String> titlesOf(const std::vector<TrackHeaderHost::AutomatableParameter>& params) {
    std::set<juce::String> titles;
    for (const auto& p : params)
        titles.insert(p.moduleTitle);
    return titles;
}

bool hasParam(const std::vector<TrackHeaderHost::AutomatableParameter>& params, const juce::String& uuid,
              const juce::String& paramId) {
    for (const auto& p : params)
        if (p.nodeUuid == uuid && p.paramId == paramId)
            return true;
    return false;
}

} // namespace

TEST_F(AddFromTimelineMainTest, ATrackOffersItsOwnModulesParametersGroupedByModuleAndNoHiddenHelpers) {
    MainComponent mc(std::make_unique<QuietProvider>());
    const auto chain = buildChain(mc);
    const auto offered = mc.getAutomatableParametersForTest(chain.track);

    EXPECT_TRUE(hasParam(offered, chain.filter->properties["uuid"].toString(), "cutoff"));
    const auto titles = titlesOf(offered);
    EXPECT_GE(titles.size(), 2u) << "the chain's modules (oscillator, filter) are listed";
    EXPECT_FALSE(hasParam(offered, chain.loose->properties["uuid"].toString(), "cutoff")) << "a module no track plays";
    for (const auto& p : offered) {
        EXPECT_NE(p.nodeUuid, chain.attenuverter->properties["uuid"].toString()) << "attenuverters are plumbing";
        EXPECT_FALSE(p.moduleTitle.containsIgnoreCase("Track In")) << "the Track In source is plumbing";
        EXPECT_NE(p.moduleTitle, "") << "every row has a module header";
        EXPECT_NE(p.parameterName, "");
    }

    // Grouped: once a module's run ends its title never comes back.
    std::set<juce::String> closed;
    juce::String current;
    for (const auto& p : offered) {
        if (p.moduleTitle != current) {
            EXPECT_EQ(closed.count(p.moduleTitle), 0u) << p.moduleTitle << " reappears after another module";
            closed.insert(current);
            current = p.moduleTitle;
        }
    }
}

TEST_F(AddFromTimelineMainTest, AlreadyAutomatedParametersAreLeftOut) {
    MainComponent mc(std::make_unique<QuietProvider>());
    const auto chain = buildChain(mc);
    const auto uuid = chain.filter->properties["uuid"].toString();
    ASSERT_TRUE(hasParam(mc.getAutomatableParametersForTest(chain.track), uuid, "cutoff"));

    mc.automateParameter(chain.filter->nodeID, "cutoff");

    EXPECT_FALSE(hasParam(mc.getAutomatableParametersForTest(chain.track), uuid, "cutoff"));
    EXPECT_TRUE(hasParam(mc.getAutomatableParametersForTest(chain.track), uuid, "resonance"));
}

TEST_F(AddFromTimelineMainTest, TheUnassignedListHoldsOnlyModulesNoSingleTrackPlays) {
    MainComponent mc(std::make_unique<QuietProvider>());
    const auto chain = buildChain(mc);
    mc.automateParameter(chain.loose->nodeID, "cutoff");
    const synth::Track* unassigned = nullptr;
    for (const auto& track : mc.getTimelineDoc().getTracks())
        if (track.kind == synth::TrackKind::Automation)
            unassigned = &track;
    ASSERT_NE(unassigned, nullptr) << "automating the loose module made the section";

    const auto offered = mc.getAutomatableParametersForTest(unassigned->id);
    ASSERT_FALSE(offered.empty());
    const auto ownOffered = mc.getAutomatableParametersForTest(chain.track);
    const auto looseUuid = chain.loose->properties["uuid"].toString();
    for (const auto& p : offered) {
        EXPECT_FALSE(hasParam(ownOffered, p.nodeUuid, p.paramId)) << p.moduleTitle << " is offered twice";
        EXPECT_NE(p.nodeUuid, chain.filter->properties["uuid"].toString()) << "owned modules are not unassigned";
        EXPECT_NE(p.nodeUuid, chain.osc->properties["uuid"].toString());
    }
    bool sawLoose = false;
    for (const auto& p : offered)
        sawLoose = sawLoose || p.nodeUuid == looseUuid;
    EXPECT_TRUE(sawLoose);
}

TEST_F(AddFromTimelineMainTest, PickingLandsTheLaneOnTheTrackTheMenuWasOpenedForInOneUndoStep) {
    MainComponent mc(std::make_unique<QuietProvider>());
    const auto chain = buildChain(mc);
    auto& panel = mc.getTimelinePanel();
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    panel.setAddAutomationPickerHookForTest(
        [&picker](std::unique_ptr<synth::ui::ModMatrixPicker> p) { picker = std::move(p); });

    auto* header = panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    const auto menu = header->buildContextMenu();
    bool enabled = false;
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == "Add automation...")
            enabled = it.getItem().isEnabled;
    EXPECT_TRUE(enabled) << "Track 1 plays modules with parameters, so the entry is enabled";
    header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kAddAutomationMenuId);
    ASSERT_NE(picker, nullptr);
    picker->setSearchTextForTest("cutoff");
    ASSERT_FALSE(picker->getVisibleItemTextsForTest().empty());

    auto& doc = mc.getTimelineDoc();
    const auto lanesBefore = doc.getTrack(chain.track)->lanes.size();
    picker->chooseVisibleItemForTest(0);

    const auto& lanes = doc.getTrack(chain.track)->lanes;
    ASSERT_EQ(lanes.size(), lanesBefore + 1);
    EXPECT_EQ(lanes.back().paramId, "cutoff");
    EXPECT_EQ(lanes.back().nodeUuid, chain.filter->properties["uuid"].toString());
    EXPECT_GT(lanes.back().range.maxValue, lanes.back().range.minValue) << "the parameter's real range";
    EXPECT_EQ(panel.getSelectedAutomationLane(), lanes.back().id);
    EXPECT_TRUE(panel.isTrackAutomationExpandedForTest(chain.track));

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(doc.getTrack(chain.track)->lanes.size(), lanesBefore) << "ONE undo step";
    panel.setAddAutomationPickerHookForTest(nullptr);
}

TEST_F(AddFromTimelineMainTest, AnExplicitTrackWinsOverTheOwnershipRule) {
    MainComponent mc(std::make_unique<QuietProvider>());
    const auto chain = buildChain(mc);
    mc.simulateAddMidiTrackClick();
    const auto second = mc.getTimelineDoc().getTracks().back().id;
    ASSERT_NE(second, chain.track);

    TrackHeaderHost::AutomatableParameter p;
    p.nodeUuid = chain.filter->properties["uuid"].toString();
    p.paramId = "cutoff";
    const auto lane = mc.addAutomationLaneForTest(second, p);

    ASSERT_TRUE(lane.isValid());
    EXPECT_EQ(mc.getTimelineDoc().getTrackForLane(lane)->id, second) << "the track asked for, not the owner";
    EXPECT_EQ(mc.addAutomationLaneForTest(second, p), lane) << "a repeat finds the same lane";
}
