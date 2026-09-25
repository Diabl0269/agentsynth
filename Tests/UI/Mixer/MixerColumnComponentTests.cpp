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
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerEqThumbnail.h"
#include <gtest/gtest.h>

// buildLinearChannelRigMMT/connectStereoMMT -- a real, signal-connected TrackAudio->EQ->Strip
// chain for the removeRow() regression test below: MixerModel::spliceOutInsert (what removeRow
// calls before freeing the node) requires an actual predecessor/successor signal edge, which the
// rest of this file's hand-built MixerColumn models never wire.
#include "../../Mixer/MixerModel/MixerModelTestFixture.h"

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

// FRO225: fires `component`'s REAL mouseDoubleClick() handler -- for an editable-on-double-click
// juce::Label (setEditable(false, true, false), same as this file's synthesizeMouseUp() above does
// for a plain click) that's Label::showEditor()'s own trigger, exactly what a live double-click
// produces, not a shortcut that skips the gesture and calls showEditor() directly.
void synthesizeMouseDoubleClick(juce::Component& component) {
    const juce::Point<int> centre(component.getWidth() / 2, component.getHeight() / 2);
    const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), centre.toFloat(),
                                 juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f,
                                 0.0f, &component, &component, juce::Time::getCurrentTime(), centre.toFloat(),
                                 juce::Time::getCurrentTime(), 2, false);
    component.mouseDoubleClick(event);
}

// FRO15 in-app finding: at the dock's real Mixer-tab column height (~181px) a freshly created
// bus's insert list and EQ thumbnail must actually be visible -- not hidden behind the model bug
// (buildInsertsForColumn never looking at a bus's own chain; MixerModelBusColumnTests.cpp covers
// that half) or a resized() overlap. Checked at both the height the user actually saw (181) and a
// taller one (420) that was never in question, per the finding's own request.
void expectBusColumnShowsItsInserts(int height) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);
    synth::TimelineDoc doc;

    const synth::DefaultChannelLayout layout{{-100, 0}, {0, 0}, {100, 0}, {200, 0}, {300, 0}};
    const auto channel = synth::buildBusChannel(graph, layout);
    ASSERT_NE(channel.strip, nullptr);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, editor.getMacros());
    const synth::MixerColumn* busColumn = nullptr;
    for (const auto& column : snapshot.columns)
        if (column.nodeId == channel.strip->nodeID)
            busColumn = &column;
    ASSERT_NE(busColumn, nullptr);
    ASSERT_EQ(busColumn->inserts.size(), 3u) << "the model half of this fix must already hold";

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, height);
    column.setColumn(*busColumn, "");

    EXPECT_EQ(column.getInsertListForTest().getEntryCountForTest(), 3)
        << "at height " << height << ", the Gate, EQ and Compressor rows must show";
    EXPECT_TRUE(column.getInsertListForTest().isLinearForTest());
    EXPECT_TRUE(column.getEqThumbnailForTest().isVisible())
        << "at height " << height << ", the bus's own EQ must get the curve thumbnail";
}

} // namespace

TEST(MixerColumnComponentTests, BusColumnShowsInsertsAndEqThumbnailAtTheDockMixerTabHeight) {
    expectBusColumnShowsItsInserts(181);
}

TEST(MixerColumnComponentTests, BusColumnShowsInsertsAndEqThumbnailAtATallerHeight) {
    expectBusColumnShowsItsInserts(420);
}

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

// FRO16 review fix: removing the currently-thumbnailed EQ insert via the mixer's own row menu
// (MixerInsertList::removeRow) used to free the EQ's processor (graph.removeNode(), synchronous)
// with nothing unbinding eqThumbnail_ first -- the eventual MixerPanelComponent::rebuild() this
// mutation triggers (through onMutated) would then destroy this column, and ~MixerEqThumbnail's
// detachListeners() would dereference the already-freed module. No crash/UAF here is the primary
// assertion (this repro is exactly why the fix exists); the live-unbind counter proves the
// pre-removal hook actually ran, not that the test got lucky.
TEST(MixerColumnComponentTests, RemovingTheBoundEqRowUnbindsTheThumbnailBeforeTheNodeIsFreed) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);
    synth::TimelineDoc doc;

    // TrackAudio -> EQ -> Compressor -> Strip, all really wired -- spliceOutInsert (what
    // removeRow() calls before freeing the node) needs a real predecessor/successor signal edge.
    const auto track = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto rig = buildLinearChannelRigMMT(graph, doc, track);
    ASSERT_NE(rig.eq, nullptr);
    ASSERT_NE(rig.strip, nullptr);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, editor.getMacros());
    ASSERT_EQ(snapshot.columns.size(), 1u);
    const auto& columnModel = snapshot.columns[0];
    ASSERT_EQ(columnModel.inserts.size(), 2u);
    ASSERT_EQ(columnModel.inserts[0].name, "Parametric EQ") << "EQ must be first in signal order";

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, 300);
    column.setColumn(columnModel, "");

    auto& thumbnail = column.getEqThumbnailForTest();
    ASSERT_TRUE(thumbnail.isVisible()) << "the EQ insert must be bound before removal";

    const int liveUnbindsBefore = synth::ui::MixerEqThumbnail::getLiveUnbindCallCountForTest();

    // Row 0 is the EQ (asserted above) -- the exact repro named in the review finding.
    column.getInsertListForTest().removeRow(0);

    EXPECT_GT(synth::ui::MixerEqThumbnail::getLiveUnbindCallCountForTest(), liveUnbindsBefore)
        << "removing the bound EQ row must unbind the thumbnail from its (now freed) module";
    EXPECT_FALSE(thumbnail.isVisible()) << "the thumbnail must hide once its EQ insert is gone";
}

// ============================================================================
// FRO225 (docs/mixer/panel.md): the mixer header's inline rename. Named strips gets its own
// persisted name; a strip boxed in a macro reuses the macro's rename instead (never two competing
// names for one column) -- see MixerColumnComponent::commitHeaderRename's own comment.
// ============================================================================

TEST(MixerColumnComponentTests, DoubleClickingTheHeaderNameRenamesAnUnboxedStrip) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);

    auto stripNode = addUuidNode(graph, std::make_unique<ChannelStripModule>(), "strip-uuid");
    auto* strip = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(strip, nullptr);
    strip->setShape(ChannelStripModule::Shape::Stereo);
    ASSERT_TRUE(strip->getStripName().isEmpty()) << "unset, so the column shows today's fallback name";

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, 300);

    synth::MixerColumn model;
    model.nodeId = stripNode->nodeID;
    model.uuid = "strip-uuid";
    model.name = "Channel 1";
    column.setColumn(model, "");

    auto& nameLabel = column.getHeaderForTest().getNameLabelForTest();
    ASSERT_EQ(nameLabel.getCurrentTextEditor(), nullptr) << "not editing yet";

    // The real gesture, not a shortcut: double-click opens Label's own editor (Label::showEditor(),
    // armed by setEditable(false, true, false) in MixerColumnHeader's constructor).
    synthesizeMouseDoubleClick(nameLabel);
    auto* textEditor = nameLabel.getCurrentTextEditor();
    ASSERT_NE(textEditor, nullptr) << "double-click must open the inline editor";

    textEditor->setText("Lead Vox");
    nameLabel.hideEditor(false); // false = commit (Label::hideEditor's own discard/commit contract)

    EXPECT_EQ(strip->getStripName(), "Lead Vox")
        << "an unboxed strip's rename writes ChannelStripModule's own persisted name";
    EXPECT_EQ(nameLabel.getText(), "Lead Vox");
}

TEST(MixerColumnComponentTests, RenamingABoxedStripGoesToItsMacroNotASecondStripName) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    AppUndoManager undoManager;
    GraphEditor editor(engine, &undoManager);
    editor.setSize(900, 600);

    auto stripNode = addUuidNode(graph, std::make_unique<ChannelStripModule>(), "strip-uuid");
    auto* strip = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor());
    ASSERT_NE(strip, nullptr);
    strip->setShape(ChannelStripModule::Shape::Stereo);

    synth::Macro macro;
    macro.name = "Drum Bus";
    macro.members.push_back("strip-uuid");
    const auto macroId = editor.getMacros().add(macro);

    synth::ui::MixerColumnComponent column;
    column.configure(graph, undoManager, editor.getMacros(), editor, engine);
    column.setSize(140, 300);

    synth::MixerColumn model;
    model.nodeId = stripNode->nodeID;
    model.uuid = "strip-uuid";
    model.name = "Drum Bus"; // stripColumnName's macro-name priority, same as buildMixerSnapshot would give it
    column.setColumn(model, "");

    auto& nameLabel = column.getHeaderForTest().getNameLabelForTest();
    synthesizeMouseDoubleClick(nameLabel);
    auto* textEditor = nameLabel.getCurrentTextEditor();
    ASSERT_NE(textEditor, nullptr);
    textEditor->setText("Drums Bus 2");
    nameLabel.hideEditor(false);

    EXPECT_EQ(editor.getMacros().find(macroId)->name, "Drums Bus 2")
        << "a boxed strip's rename goes to its macro, the same one the column's own name already came from";
    EXPECT_TRUE(strip->getStripName().isEmpty())
        << "never ALSO written to the strip's own name -- that would be a second, competing name for one column";
}
