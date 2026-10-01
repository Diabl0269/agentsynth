// AutomationLanesLoadTests.cpp -- opening a saved project that has automation lanes on a MIDI track
// and on the Automation track, plus a macro holding an LFO (a module whose card sizes itself while
// it is built), lays the lane rows out with the Automation track last.

#include "../TimelinePanel/TimelinePanelTestFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "MacroSet.h"
#include "Modules/ModuleBase.h"

namespace {

juce::AudioProcessorGraph::Node::Ptr addNodeWithUuid(MainComponent& mc, const juce::String& type) {
    auto node = mc.getAudioEngine().getGraph().addNode(synth::AIStateMapper::createModule(type));
    if (node == nullptr)
        return node;
    const auto uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node;
}

} // namespace

TEST_F(TimelinePanelIntegrationTest, AProjectWithLanesOnATrackAndUnassignedAndAMacroLfoOpensWithRowsLaidOut) {
    const juce::File scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("agentsynth-lanes-load-" + juce::Uuid().toString());
    ASSERT_TRUE(scratch.createDirectory());
    const juce::File bundle = scratch.getChildFile("Project.agsproj");
    {
        MainComponent mc(std::make_unique<MockProviderTL>());
        mc.simulateAddMidiTrackClick();
        auto& doc = mc.getTimelineDoc();
        const synth::TrackId midi = doc.getTracks().front().id;

        // A lane on the MIDI track itself.
        auto filter = addNodeWithUuid(mc, "Filter");
        ASSERT_NE(filter, nullptr);
        const auto trackLane =
            doc.addLane(midi, filter->properties["uuid"].toString(), "cutoff", {20.0f, 20000.0f, 1000.0f});
        ASSERT_TRUE(trackLane.isValid());
        ASSERT_TRUE(doc.addBreakpoint(trackLane, 1.0, 500.0));

        // A module no track plays: its lane goes to the Automation track.
        auto loose = addNodeWithUuid(mc, "Filter");
        ASSERT_NE(loose, nullptr);
        mc.automateParameter(loose->nodeID, "resonance");

        // A macro holding an LFO.
        auto lfo = addNodeWithUuid(mc, "LFO");
        ASSERT_NE(lfo, nullptr);
        synth::Macro macro;
        macro.name = "Mod";
        macro.collapsed = false;
        macro.bounds = {200, 200, 300, 200};
        macro.members.push_back(lfo->properties["uuid"].toString());
        mc.getGraphEditor().getMacros().add(macro);
        mc.getGraphEditor().updateComponents();

        ASSERT_TRUE(mc.saveProjectForTest(bundle));
        ASSERT_TRUE(mc.openProjectForTest(bundle));

        auto& panel = mc.getTimelinePanel();
        const auto& tracks = doc.getTracks();
        ASSERT_EQ(tracks.size(), 2u);
        EXPECT_EQ(tracks.back().kind, synth::TrackKind::Automation) << "the Automation track is last";
        ASSERT_EQ(panel.getTrackHeaderCount(), 2);
        EXPECT_TRUE(panel.getTrackHeaderAt(1)->isSectionHeader());

        const auto* unassigned = doc.getLaneForParam(loose->properties["uuid"].toString(), "resonance");
        ASSERT_NE(unassigned, nullptr);
        EXPECT_NE(panel.laneHeaderForTest(unassigned->id), nullptr) << "the Unassigned section opens with its lanes";

        panel.setTrackAutomationExpanded(tracks.front().id, true);
        EXPECT_FALSE(panel.laneRowBoundsForTest(trackLane).isEmpty());
        ASSERT_NE(panel.laneEditorForTest(trackLane), nullptr);
        EXPECT_EQ(doc.getLane(trackLane)->points.size(), 1u);
        const auto layout = panel.getClipLaneArea().getRowLayout();
        EXPECT_EQ(layout.trackRowHeight(1), synth::ui::TimelineAutomationLanes::kSectionRowHeight);
        EXPECT_GT(layout.trackExtraHeight(0), 0);
    }
    scratch.deleteRecursively();
}
