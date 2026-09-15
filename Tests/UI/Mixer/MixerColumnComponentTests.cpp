// MixerColumnComponentTests.cpp -- FRO16 (P9-10): the column-level half of the EQ curve thumbnail
// -- finding "the" EQ among a column's inserts (first in signal order) and forwarding a click
// through the column's existing onEditOnCanvas seam with the EQ's own uuid, never the strip's.
//
// Builds a minimal strip + insert(s) graph directly (the MixerFaderTests style: AudioEngine +
// AppUndoManager + a real GraphEditor, no MainComponent) rather than driving a full app/timeline
// rig -- MixerColumnComponent::setColumn() only ever reads column.inserts off the graph, so the
// nodes need not be wired into an actual signal chain for this.
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include <gtest/gtest.h>

namespace {

juce::AudioProcessorGraph::Node::Ptr addUuidNode(juce::AudioProcessorGraph& graph,
                                                 std::unique_ptr<juce::AudioProcessor> processor,
                                                 const juce::String& uuid) {
    auto node = graph.addNode(std::move(processor));
    node->properties.set("uuid", uuid);
    if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
        module->setNodeUuid(uuid);
    return node;
}

/** Fires `component`'s mouseUp handler with a synthesized event centred on it -- same recipe
 *  MixerPanelComponentTests.cpp's own click test uses. */
void synthesizeMouseUp(juce::Component& component) {
    const juce::Point<int> centre(component.getWidth() / 2, component.getHeight() / 2);
    component.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(),
                                       juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime(),
                                       centre.toFloat(), juce::Time::getCurrentTime(), 1, false));
}

} // namespace

TEST(MixerColumnComponentTests, ClickForwardsEqUuidThroughOnClicked) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);

    auto stripNode = addUuidNode(graph, std::make_unique<ChannelStripModule>(), "strip-uuid");
    dynamic_cast<ChannelStripModule*>(stripNode->getProcessor())->setShape(ChannelStripModule::Shape::Stereo);
    auto eqNode = addUuidNode(graph, std::make_unique<ParametricEQModule>(), "eq-uuid");

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, 300);

    synth::MixerColumn model;
    model.nodeId = stripNode->nodeID;
    model.uuid = "strip-uuid";
    model.name = "Strip";
    model.insertChainIsLinear = true;
    model.inserts.push_back({eqNode->nodeID, "eq-uuid", "Parametric EQ", false});
    column.setColumn(model, "");

    juce::String capturedTarget;
    column.onEditOnCanvas = [&](const juce::String& target) { capturedTarget = target; };

    auto& thumbnail = column.getEqThumbnailForTest();
    ASSERT_TRUE(thumbnail.isVisible()) << "a column with a Parametric EQ insert must show the thumbnail";
    synthesizeMouseUp(thumbnail);

    EXPECT_EQ(capturedTarget, "eq-uuid") << "the click must target the EQ, not the strip";
}

TEST(MixerColumnComponentTests, TwoEqsShowsOnlyFirstInSignalOrder) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);

    auto stripNode = addUuidNode(graph, std::make_unique<ChannelStripModule>(), "strip-uuid");
    dynamic_cast<ChannelStripModule*>(stripNode->getProcessor())->setShape(ChannelStripModule::Shape::Stereo);
    auto firstEqNode = addUuidNode(graph, std::make_unique<ParametricEQModule>(), "eq-first-uuid");
    auto secondEqNode = addUuidNode(graph, std::make_unique<ParametricEQModule>(), "eq-second-uuid");

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, 300);

    synth::MixerColumn model;
    model.nodeId = stripNode->nodeID;
    model.uuid = "strip-uuid";
    model.name = "Strip";
    model.insertChainIsLinear = true;
    // Signal order: firstEqNode, then secondEqNode -- only the first should get the thumbnail.
    model.inserts.push_back({firstEqNode->nodeID, "eq-first-uuid", "Parametric EQ", false});
    model.inserts.push_back({secondEqNode->nodeID, "eq-second-uuid", "Parametric EQ", false});
    column.setColumn(model, "");

    juce::String capturedTarget;
    column.onEditOnCanvas = [&](const juce::String& target) { capturedTarget = target; };

    auto& thumbnail = column.getEqThumbnailForTest();
    ASSERT_TRUE(thumbnail.isVisible());
    synthesizeMouseUp(thumbnail);

    EXPECT_EQ(capturedTarget, "eq-first-uuid") << "only the first EQ in signal order gets the thumbnail";
}

TEST(MixerColumnComponentTests, NoEqInsertHidesTheThumbnail) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);

    auto stripNode = addUuidNode(graph, std::make_unique<ChannelStripModule>(), "strip-uuid");
    dynamic_cast<ChannelStripModule*>(stripNode->getProcessor())->setShape(ChannelStripModule::Shape::Stereo);

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, 300);

    synth::MixerColumn model;
    model.nodeId = stripNode->nodeID;
    model.uuid = "strip-uuid";
    model.name = "Strip";
    model.insertChainIsLinear = true; // no inserts at all
    column.setColumn(model, "");

    EXPECT_FALSE(column.getEqThumbnailForTest().isVisible());
}
