// OutputDockTests.cpp
// The output dock (docs/layout/layout.md#output-dock): Master, Rec Tap and Audio Output are always the rightmost
// cards, their x derived and their shared y Audio Output's own. Every case drives a real entry point (a body drag
// through ModuleComponent's mouse handlers, GraphEditor::updateComponents, makeRoomFor via setMacroCollapsed, the
// timeline's add-track flow, a project bundle load) rather than calling reflowOutputDock() directly. The pure
// geometry is covered in Tests/UI/Layout/LayoutUtilOutputDockTests.cpp.

#include "../../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/MasterSplice.h"
#include "Modules/OscillatorModule.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

using NodeID = juce::AudioProcessorGraph::NodeID;
namespace LU = synth::LayoutUtil;

namespace {

NodeID addModule(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                 int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

NodeID addAudioOutput(GraphEditor& editor, AudioEngine& engine, int x, int y) {
    return addModule(editor, engine, synth::AIStateMapper::createModule("Audio Output"), x, y);
}

ModuleComponent* compFor(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

int propInt(AudioEngine& engine, NodeID id, const char* key) {
    return static_cast<int>(engine.getGraph().getNodeForId(id)->properties[key]);
}

juce::MouseEvent mouseEvent(juce::Component& comp, juce::Point<int> pos, juce::Point<int> downPos, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos.toFloat(), juce::Time::getCurrentTime(),
                            1, dragged);
}

// A whole real body drag: press, one move by `delta`, release.
void dragCard(ModuleComponent& comp, juce::Point<int> delta) {
    const juce::Point<int> press(comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10);
    comp.mouseDown(mouseEvent(comp, press, press, false));
    comp.mouseDrag(mouseEvent(comp, press + delta, press, true));
    comp.mouseUp(mouseEvent(comp, press + delta, press, true));
}

int snapUp(int v) { return ((v + LU::kGridSize - 1) / LU::kGridSize) * LU::kGridSize; }

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
    }
    NodeID osc(int x, int y) { return addModule(editor, engine, std::make_unique<OscillatorModule>(), x, y); }
    juce::Rectangle<int> rect(NodeID id) { return compFor(editor, id)->getBounds(); }
};

} // namespace

TEST(OutputDock, AudioOutputSitsRightOfTheContentAfterAnyReconcile) {
    Canvas c;
    const auto out = addAudioOutput(c.editor, c.engine, 40, 40);
    const auto osc = c.osc(400, 300);

    const int expectedX = snapUp(c.rect(osc).getRight()) + LU::kLayerGapX;
    EXPECT_EQ(c.rect(out).getX(), expectedX);
    EXPECT_EQ(propInt(c.engine, out, "x"), expectedX) << "the node property follows the live bounds";
    EXPECT_EQ(c.rect(out).getY(), 40) << "y is Audio Output's own";
}

TEST(OutputDock, NewPatchSeedsAudioOutputAtTheDockOrigin) {
    Canvas c;
    c.editor.newPatch();
    auto* node = synth::outputDockNodes(c.engine.getGraph()).front();
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(compFor(c.editor, node->nodeID)->getPosition(),
              juce::Point<int>(LU::kArrangeOriginX, LU::kArrangeOriginY));
}

// A module dragged to the right of the output and released through the real mouse path pushes the dock along, inside
// the drag's own undo step.
TEST(OutputDock, ReleasingAModuleRightOfTheDockMovesTheDockRightOfIt) {
    Canvas c;
    const auto out = addAudioOutput(c.editor, c.engine, 40, 40);
    const auto osc = c.osc(400, 300);
    const int dockBefore = c.rect(out).getX();

    dragCard(*compFor(c.editor, osc), {1500, 0});

    const auto oscRect = c.rect(osc);
    ASSERT_GT(oscRect.getRight(), dockBefore + 280) << "sanity: dropped right of where the dock was";
    EXPECT_EQ(c.rect(out).getX(), snapUp(oscRect.getRight()) + LU::kLayerGapX);
    EXPECT_FALSE(c.rect(out).intersects(oscRect));

    ASSERT_TRUE(c.undo.undo());
    EXPECT_EQ(c.rect(out).getX(), dockBefore) << "the dock's move is part of the drag's undo step";
}

TEST(OutputDock, DockCardsStayPutWhenASelectionContainingThemIsDragged) {
    Canvas c;
    const auto out = addAudioOutput(c.editor, c.engine, 40, 40);
    const auto a = c.osc(400, 300);
    const auto b = c.osc(400, 700);
    const auto outBefore = c.rect(out);
    c.editor.setSelectedNodes({a, b, out});

    dragCard(*compFor(c.editor, a), {0, 200});

    EXPECT_GE(c.rect(a).getY(), 500) << "the group moved down";
    EXPECT_EQ(c.rect(b).getY() - c.rect(a).getY(), 400) << "as one rigid body";
    EXPECT_EQ(c.rect(out).getY(), outBefore.getY()) << "the output never travels with a selection";
}

// Expanding a macro grows its hull past the dock: make-room does not push the (pinned) dock, the dock is re-derived.
TEST(OutputDock, ExpandingAMacroThatGrowsPastTheDockMovesTheDock) {
    Canvas c;
    const auto out = addAudioOutput(c.editor, c.engine, 40, 40);
    const auto m1 = c.osc(400, 300);
    const auto m2 = c.osc(700, 300);
    c.editor.setSelectedNodes({m1, m2});
    const auto macroId = c.editor.getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    const auto* macro = c.editor.getMacros().find(macroId);
    ASSERT_TRUE(macro->collapsed);
    ASSERT_GE(c.rect(out).getX(),
              snapUp(c.editor.getMacroController().macroCableAnchorBounds(*macro).getRight()) + LU::kLayerGapX);

    c.editor.getMacroController().setMacroCollapsed(macroId, false);

    const auto hull = c.editor.getMacroController().macroHullBounds(macroId);
    EXPECT_FALSE(c.rect(out).intersects(hull));
    EXPECT_EQ(c.rect(out).getX(), snapUp(hull.getRight()) + LU::kLayerGapX);
}

TEST(OutputDock, AudioOutputCanNeverBeDeleted) {
    Canvas c;
    const auto out = addAudioOutput(c.editor, c.engine, 40, 40);
    const auto osc = c.osc(400, 300);

    c.editor.requestDeleteModule(out);
    EXPECT_NE(c.engine.getGraph().getNodeForId(out), nullptr) << "single delete refused";

    c.editor.setSelectedNodes({osc, out});
    c.editor.deleteSelection();
    EXPECT_NE(c.engine.getGraph().getNodeForId(out), nullptr) << "multi delete refused for the output";
    EXPECT_EQ(c.engine.getGraph().getNodeForId(osc), nullptr) << "the rest of the selection is still deleted";
}

// ---- With a real channel: the timeline's add-track flow, through MainComponent ----

class OutputDockMainTest : public ChannelFlowTest {};

TEST_F(OutputDockMainTest, AddingATrackPutsMasterLeftOfAudioOutputOnTheSameRow) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& editor = mc.getGraphEditor();

    addAudioTrack(mc);

    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);
    const auto nodes = synth::outputDockNodes(graph);
    ASSERT_GE(nodes.size(), 2u);
    auto* masterComp = compFor(editor, master->nodeID);
    auto* outComp = compFor(editor, nodes.back()->nodeID);
    ASSERT_NE(masterComp, nullptr);
    ASSERT_NE(outComp, nullptr);
    EXPECT_EQ(masterComp->getY(), outComp->getY());
    EXPECT_EQ(masterComp->getY(), LU::kArrangeOriginY) << "Master takes Audio Output's y";
    EXPECT_GE(outComp->getX(), masterComp->getRight() + LU::kOutputDockCardGapX);
    for (auto* comp : editor.getModuleComponents())
        if (comp != masterComp && comp != outComp && comp->isVisible())
            EXPECT_LE(comp->getRight(), masterComp->getX()) << "every other card is left of the dock";
    for (const auto& macro : editor.getMacros().getAll())
        EXPECT_LE(editor.getMacroController().macroCableAnchorBounds(macro).getRight(), masterComp->getX());
}

TEST_F(OutputDockMainTest, MasterCannotBeDeletedWhileTheMixerHasChannels) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    addAudioTrack(mc);
    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);
    const auto masterId = master->nodeID;

    mc.getGraphEditor().requestDeleteModule(masterId);

    EXPECT_NE(graph.getNodeForId(masterId), nullptr);
    EXPECT_FALSE(mc.getGraphEditor().outputDockDeleteRefusal(masterId).isEmpty());
}

// A vertical drag moves the whole dock (y only, snapped); the drag's x movement is ignored.
TEST_F(OutputDockMainTest, DraggingMasterMovesTheWholeDockVertically) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& editor = mc.getGraphEditor();
    addAudioTrack(mc);
    auto* master = synth::findMasterNode(graph);
    ASSERT_NE(master, nullptr);
    const auto nodes = synth::outputDockNodes(graph);
    auto* masterComp = compFor(editor, master->nodeID);
    auto* outComp = compFor(editor, nodes.back()->nodeID);
    const auto masterBefore = masterComp->getPosition();
    const auto outBefore = outComp->getPosition();

    dragCard(*masterComp, {90, 200});

    EXPECT_EQ(masterComp->getPosition(), juce::Point<int>(masterBefore.x, masterBefore.y + 200));
    EXPECT_EQ(outComp->getPosition(), juce::Point<int>(outBefore.x, outBefore.y + 200));
    EXPECT_EQ(static_cast<int>(nodes.back()->properties["y"]), outBefore.y + 200)
        << "Audio Output's own y is the truth";
    EXPECT_EQ(static_cast<int>(master->properties["x"]), masterBefore.x) << "x stays derived";
}

TEST_F(OutputDockMainTest, GoToOutputFramesTheWholeDock) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    auto& editor = mc.getGraphEditor();
    addAudioTrack(mc);

    editor.locateMasterOrOutput();

    juce::Rectangle<int> frame;
    for (auto* node : synth::outputDockNodes(mc.getAudioEngine().getGraph()))
        frame = frame.getUnion(compFor(editor, node->nodeID)->getBounds());
    const auto visibleCentre = editor.getVisibleCanvasRect().getCentre();
    EXPECT_NEAR(visibleCentre.x, frame.getCentreX(), 2.0f);
    EXPECT_NEAR(visibleCentre.y, frame.getCentreY(), 2.0f);
}

// ---- Project load: derived on open, with no undo step and no dirtiness ----

TEST_F(OutputDockMainTest, LoadingAProjectDerivesTheDockWithoutAnUndoStep) {
    const auto root = synth::userSettingsRootDirectory().getChildFile("agentsynth-outputdock-tests");
    root.deleteRecursively();
    root.createDirectory();
    auto dir = root.getChildFile("Dock" + juce::String(synth::ProjectBundle::kBundleExtension));
    dir.createDirectory();
    dir.getChildFile(synth::ProjectBundle::kAudioSubdirName).createDirectory();
    dir.getChildFile(synth::ProjectBundle::kPeaksSubdirName).createDirectory();
    dir.getChildFile(synth::ProjectBundle::kProjectFileName).replaceWithText(R"JSON(
{
  "schemaVersion": 1,
  "nodes": [
    { "id": 1, "type": "Audio Output", "uuid": "out", "position": { "x": 40, "y": 40 } },
    { "id": 2, "type": "Oscillator", "uuid": "osc", "position": { "x": 1000, "y": 300 } }
  ],
  "connections": []
}
)JSON");

    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    const int serialBefore = mc.getUndoManager().getEditSerial();

    ASSERT_TRUE(mc.openProjectForTest(dir));

    auto& editor = mc.getGraphEditor();
    auto* out = synth::outputDockNodes(mc.getAudioEngine().getGraph()).back();
    NodeID oscId;
    for (auto* node : mc.getAudioEngine().getGraph().getNodes())
        if (node->properties["uuid"].toString() == "osc")
            oscId = node->nodeID;
    ASSERT_NE(oscId.uid, 0u);
    EXPECT_EQ(compFor(editor, out->nodeID)->getX(), snapUp(compFor(editor, oscId)->getRight()) + LU::kLayerGapX);
    EXPECT_EQ(mc.getUndoManager().getEditSerial(), serialBefore);
    EXPECT_FALSE(mc.getUndoManager().canUndo());
    EXPECT_FALSE(mc.isProjectDirty());
    root.deleteRecursively();
}
