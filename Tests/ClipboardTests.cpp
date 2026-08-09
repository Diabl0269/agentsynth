// ClipboardTests.cpp
// Copy / paste / duplicate for a multi-module selection.
//
//   • ModuleClipboard  — pure clipboard state: payload, module count, cascade positions
//   • copy             — snippet-dialect payload, ineligible nodes filtered, params + extra state
//   • paste            — internal wiring rebuilt between the COPIES, nothing spliced into the patch
//   • duplicate        — same insert, offset from the original, clipboard left untouched
//   • undo             — a paste or duplicate is ONE undoable change for the whole group
//
// The wiring assertions are the point of the feature: a pasted group must be wired to itself, must
// not steal or share the originals' connections, and must not gain wires to the surrounding patch.

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/AttenuverterModule.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/LFOModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/SamplerModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/SnippetManager.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/ModuleClipboard.h"
#include "../Source/UI/ModuleComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::ui::ModuleClipboard;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    editor.updateComponents();
    return node->nodeID;
}

/** Every node currently in the graph whose processor reports `typeName`, in graph order. */
std::vector<NodeID> nodesOfType(juce::AudioProcessorGraph& graph, const juce::String& typeName) {
    std::vector<NodeID> ids;
    for (auto* node : graph.getNodes())
        if (node->getProcessor() != nullptr && node->getProcessor()->getName() == typeName)
            ids.push_back(node->nodeID);
    return ids;
}

int countOfType(juce::AudioProcessorGraph& graph, const juce::String& typeName) {
    return (int)nodesOfType(graph, typeName).size();
}

bool hasConnectionBetween(juce::AudioProcessorGraph& graph, NodeID src, NodeID dst) {
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == src && conn.destination.nodeID == dst)
            return true;
    return false;
}

/** Any wire at all touching `id`, in either direction. */
int connectionCountFor(juce::AudioProcessorGraph& graph, NodeID id) {
    int count = 0;
    for (const auto& conn : graph.getConnections())
        if (conn.source.nodeID == id || conn.destination.nodeID == id)
            ++count;
    return count;
}

juce::Point<int> positionOf(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    if (node == nullptr)
        return {};
    return {(int)node->properties["x"], (int)node->properties["y"]};
}

juce::RangedAudioParameter* findParam(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == paramId)
                return ranged;
    return nullptr;
}

void setDenormalised(juce::AudioProcessor* processor, const juce::String& paramId, float value) {
    auto* param = findParam(processor, paramId);
    ASSERT_NE(param, nullptr);
    param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(value));
}

float getDenormalised(juce::AudioProcessor* processor, const juce::String& paramId) {
    auto* param = findParam(processor, paramId);
    if (param == nullptr)
        return std::numeric_limits<float>::quiet_NaN();
    return param->getNormalisableRange().convertFrom0to1(param->getValue());
}

/** A minimal valid WAV, so a Sampler has something real to have loaded. */
bool writeSilentWav(const juce::File& file, int numFrames) {
    file.deleteFile();
    juce::AudioBuffer<float> buffer(1, numFrames);
    buffer.clear();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release(); // the writer owns it now
    writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
    writer.reset(); // flush
    return file.existsAsFile();
}

/** The one node of `typeName` that is NOT `original` — i.e. the copy a paste just produced. */
NodeID theOtherOfType(juce::AudioProcessorGraph& graph, const juce::String& typeName, NodeID original) {
    for (auto id : nodesOfType(graph, typeName))
        if (id != original)
            return id;
    return {};
}

} // namespace

// ============================================================================
// ModuleClipboard — pure state
// ============================================================================

TEST(ModuleClipboardState, StartsEmpty) {
    ModuleClipboard clipboard;
    EXPECT_TRUE(clipboard.isEmpty());
    EXPECT_EQ(clipboard.getModuleCount(), 0);
}

TEST(ModuleClipboardState, PayloadWithNoNodesStillCountsAsEmpty) {
    // Copying a selection of nothing but graph I/O produces well-formed JSON with an empty `nodes`
    // array. That must not enable Paste, or the menu would offer an action that does nothing.
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", juce::var(juce::Array<juce::var>()));

    ModuleClipboard clipboard;
    clipboard.set(juce::var(root.get()), {100, 100});
    EXPECT_TRUE(clipboard.isEmpty());
}

TEST(ModuleClipboardState, SetStoresThePayloadAndItsModuleCount) {
    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    node->setProperty("id", 1);
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node.get()));
    nodes.add(juce::var(node.get()));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", nodes);

    ModuleClipboard clipboard;
    clipboard.set(juce::var(root.get()), {40, 80});

    EXPECT_FALSE(clipboard.isEmpty());
    EXPECT_EQ(clipboard.getModuleCount(), 2);
    EXPECT_EQ(clipboard.getAnchor(), juce::Point<int>(40, 80));
}

TEST(ModuleClipboardState, ClearEmptiesEverything) {
    juce::DynamicObject::Ptr node = new juce::DynamicObject();
    juce::Array<juce::var> nodes;
    nodes.add(juce::var(node.get()));
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", nodes);

    ModuleClipboard clipboard;
    clipboard.set(juce::var(root.get()), {40, 80});
    ASSERT_FALSE(clipboard.isEmpty());

    clipboard.clear();
    EXPECT_TRUE(clipboard.isEmpty());
    EXPECT_EQ(clipboard.getAnchor(), juce::Point<int>());
}

TEST(ModuleClipboardState, EachPasteStepsFurtherFromTheAnchor) {
    ModuleClipboard clipboard;
    clipboard.set(juce::var(), {100, 200});

    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(clipboard.nextPastePosition(), juce::Point<int>(100 + step, 200 + step));
    EXPECT_EQ(clipboard.nextPastePosition(), juce::Point<int>(100 + 2 * step, 200 + 2 * step));
    EXPECT_EQ(clipboard.nextPastePosition(), juce::Point<int>(100 + 3 * step, 200 + 3 * step));
}

TEST(ModuleClipboardState, TheCascadeStepStaysOnTheLayoutGrid) {
    // An offset copy must not knock the group off-grid, or a snapped drop point would move it.
    EXPECT_EQ(ModuleClipboard::kOffsetStep % synth::LayoutUtil::kGridSize, 0);
    EXPECT_GT(ModuleClipboard::kOffsetStep, 0);
}

TEST(ModuleClipboardState, ANewCopyRestartsTheCascade) {
    ModuleClipboard clipboard;
    clipboard.set(juce::var(), {0, 0});
    clipboard.nextPastePosition();
    clipboard.nextPastePosition();

    clipboard.set(juce::var(), {500, 500});
    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(clipboard.nextPastePosition(), juce::Point<int>(500 + step, 500 + step));
}

TEST(ModuleClipboardState, AnchoringReAimsTheCascadeAtTheNewPoint) {
    ModuleClipboard clipboard;
    clipboard.set(juce::var(), {0, 0});
    clipboard.nextPastePosition();

    clipboard.anchorAt({300, 400});
    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(clipboard.getCascadeCount(), 0);
    EXPECT_EQ(clipboard.nextPastePosition(), juce::Point<int>(300 + step, 400 + step));
}

// ============================================================================
// Copy
// ============================================================================

TEST(ClipboardCopy, WithNothingSelectedIsRefusedAndLeavesTheClipboardEmpty) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    EXPECT_FALSE(editor.copySelection());
    EXPECT_FALSE(editor.canPaste());
    EXPECT_EQ(editor.getClipboardModuleCount(), 0);
}

TEST(ClipboardCopy, StoresOneEntryPerSelectedModule) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.setSelectedNodes({a, b});
    EXPECT_TRUE(editor.copySelection());
    EXPECT_TRUE(editor.canPaste());
    EXPECT_EQ(editor.getClipboardModuleCount(), 2);
}

TEST(ClipboardCopy, KeepsThePreviousContentsWhenTheNewSelectionHasNothingCopyable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.setSelectedNodes({osc});
    ASSERT_TRUE(editor.copySelection());

    // An Attenuverter is never copyable on its own — it is the hidden half of a mod routing, stored
    // as intent rather than as a node. A selection of nothing but those has nothing to copy, and
    // must not wipe what the user already had on the clipboard.
    auto atten = addModuleAt(editor, engine, std::make_unique<AttenuverterModule>(), 300, 100);
    editor.setSelectedNodes({atten});
    EXPECT_FALSE(editor.copySelection());
    EXPECT_EQ(editor.getClipboardModuleCount(), 1);
}

TEST(ClipboardCopy, SurvivesDeletingTheModulesItCameFrom) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.copySelection());

    editor.deleteSelection();
    ASSERT_EQ(countOfType(engine.getGraph(), "Oscillator"), 0);

    // The payload is a self-contained JSON snapshot, not a reference to live nodes.
    EXPECT_TRUE(editor.pasteClipboard());
    EXPECT_EQ(countOfType(engine.getGraph(), "Oscillator"), 1);
    EXPECT_EQ(countOfType(engine.getGraph(), "Filter"), 1);
}

TEST(ClipboardCopy, CarriesParameterValuesThroughToThePastedCopy) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto lfoId = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto* lfo = engine.getGraph().getNodeForId(lfoId)->getProcessor();
    // 0.5 Hz on a range that extends past 1.0 — the value the untrusted-apply rescale heuristic
    // would corrupt. It must arrive verbatim.
    setDenormalised(lfo, "rateHz", 0.5f);

    editor.setSelectedNodes({lfoId});
    ASSERT_TRUE(editor.copySelection());
    ASSERT_TRUE(editor.pasteClipboard());

    auto copyId = theOtherOfType(engine.getGraph(), "LFO", lfoId);
    ASSERT_NE(copyId.uid, 0u);
    EXPECT_NEAR(getDenormalised(engine.getGraph().getNodeForId(copyId)->getProcessor(), "rateHz"), 0.5f, 1e-4f);
}

TEST(ClipboardCopy, CarriesNonParameterModuleStateSoADuplicatedSamplerKeepsItsSample) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("clipboard-sampler.wav");
    ASSERT_TRUE(writeSilentWav(file, 512)) << "could not stage a sample file";

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto samplerId = addModuleAt(editor, engine, std::make_unique<SamplerModule>(), 100, 100);
    auto* sampler = dynamic_cast<SamplerModule*>(engine.getGraph().getNodeForId(samplerId)->getProcessor());
    ASSERT_NE(sampler, nullptr);
    ASSERT_TRUE(sampler->loadSampleFile(file));
    ASSERT_FALSE(sampler->getExtraState().isVoid());

    editor.setSelectedNodes({samplerId});
    ASSERT_TRUE(editor.duplicateSelection());

    auto copyId = theOtherOfType(engine.getGraph(), sampler->getName(), samplerId);
    ASSERT_NE(copyId.uid, 0u);
    auto* copy = dynamic_cast<SamplerModule*>(engine.getGraph().getNodeForId(copyId)->getProcessor());
    ASSERT_NE(copy, nullptr);

    // A duplicate that lost the loaded sample would not be a duplicate. The clipboard opts into
    // carrying `state`, which the on-disk snippet path deliberately does not.
    EXPECT_EQ(juce::JSON::toString(copy->getExtraState()), juce::JSON::toString(sampler->getExtraState()));

    file.deleteFile();
}

// ============================================================================
// Paste — wiring
// ============================================================================

TEST(ClipboardPaste, RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto& graph = engine.getGraph();
    ASSERT_TRUE(graph.addConnection({{osc, 0}, {filter, 0}}));

    editor.setSelectedNodes({osc, filter});
    ASSERT_TRUE(editor.copySelection());
    ASSERT_TRUE(editor.pasteClipboard());

    auto oscCopy = theOtherOfType(graph, "Oscillator", osc);
    auto filterCopy = theOtherOfType(graph, "Filter", filter);
    ASSERT_NE(oscCopy.uid, 0u);
    ASSERT_NE(filterCopy.uid, 0u);

    EXPECT_TRUE(hasConnectionBetween(graph, oscCopy, filterCopy)) << "the copy must carry the group's own wiring";
    EXPECT_TRUE(hasConnectionBetween(graph, osc, filter)) << "the original wiring must be untouched";
    EXPECT_FALSE(hasConnectionBetween(graph, osc, filterCopy)) << "no wire may cross between original and copy";
    EXPECT_FALSE(hasConnectionBetween(graph, oscCopy, filter)) << "no wire may cross between copy and original";
}

TEST(ClipboardPaste, DropsConnectionsThatLeftTheCopiedSelection) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto& graph = engine.getGraph();
    ASSERT_TRUE(graph.addConnection({{osc, 0}, {filter, 0}}));

    // Only the Filter is copied — its input comes from outside the group.
    editor.setSelectedNodes({filter});
    ASSERT_TRUE(editor.copySelection());
    ASSERT_TRUE(editor.pasteClipboard());

    auto filterCopy = theOtherOfType(graph, "Filter", filter);
    ASSERT_NE(filterCopy.uid, 0u);

    EXPECT_FALSE(hasConnectionBetween(graph, osc, filterCopy))
        << "a half-selected wire must not be recreated onto the copy";
    EXPECT_EQ(connectionCountFor(graph, filterCopy), 0) << "the copy must arrive with no wires at all";
}

TEST(ClipboardPaste, DoesNotSpliceTheCopiesIntoTheSurroundingPatch) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);

    editor.setSelectedNodes({osc});
    ASSERT_TRUE(editor.copySelection());

    const int connectionsBefore = (int)graph.getConnections().size();
    ASSERT_TRUE(editor.pasteClipboard());

    auto oscCopy = theOtherOfType(graph, "Oscillator", osc);
    ASSERT_NE(oscCopy.uid, 0u);
    EXPECT_EQ(connectionCountFor(graph, oscCopy), 0)
        << "merge-mode auto-connect must stay off: a pasted module must not wire itself to Audio Output";
    EXPECT_EQ((int)graph.getConnections().size(), connectionsBefore);
}

TEST(ClipboardPaste, RebuildsModulationChainsBetweenTheCopies) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto& graph = engine.getGraph();
    auto lfo = addModuleAt(editor, engine, std::make_unique<LFOModule>(), 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto atten = addModuleAt(editor, engine, std::make_unique<AttenuverterModule>(), 300, 100);

    // LFO -> attenuverter -> Filter cutoff CV, the shape a live mod routing takes in the graph.
    ASSERT_TRUE(graph.addConnection({{lfo, 0}, {atten, 0}}));
    ASSERT_TRUE(graph.addConnection({{atten, 0}, {filter, 2}}));
    ASSERT_EQ(countOfType(graph, "Attenuverter"), 1);

    // The attenuverter is never selected by the user — it is stored as modulation intent.
    editor.setSelectedNodes({lfo, filter});
    ASSERT_TRUE(editor.copySelection());
    EXPECT_EQ(editor.getClipboardModuleCount(), 2) << "the attenuverter must not become a clipboard node";

    ASSERT_TRUE(editor.pasteClipboard());
    EXPECT_EQ(countOfType(graph, "Attenuverter"), 2) << "the modulation chain must be recreated for the copy";
    EXPECT_EQ(countOfType(graph, "LFO"), 2);
    EXPECT_EQ(countOfType(graph, "Filter"), 2);
}

// ============================================================================
// Paste — placement, selection, undo
// ============================================================================

TEST(ClipboardPaste, WithAnEmptyClipboardIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    const int before = engine.getGraph().getNumNodes();
    EXPECT_FALSE(editor.pasteClipboard());
    EXPECT_FALSE(editor.pasteClipboardAt({400, 400}));
    EXPECT_EQ(engine.getGraph().getNumNodes(), before);
}

TEST(ClipboardPaste, LeavesTheCopiesSelectedAndTheOriginalsAlone) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.copySelection());

    const int before = engine.getGraph().getNumNodes();
    ASSERT_TRUE(editor.pasteClipboard());

    EXPECT_EQ(engine.getGraph().getNumNodes(), before + 2);
    EXPECT_EQ(editor.getSelectionCount(), 2);
    EXPECT_FALSE(editor.isNodeSelected(a));
    EXPECT_FALSE(editor.isNodeSelected(b));
}

TEST(ClipboardPaste, PreservesTheGroupsRelativeLayout) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 560, 400);
    const auto originalDelta = positionOf(graph, filter) - positionOf(graph, osc);

    editor.setSelectedNodes({osc, filter});
    ASSERT_TRUE(editor.copySelection());
    ASSERT_TRUE(editor.pasteClipboard());

    auto oscCopy = theOtherOfType(graph, "Oscillator", osc);
    auto filterCopy = theOtherOfType(graph, "Filter", filter);
    ASSERT_NE(oscCopy.uid, 0u);
    ASSERT_NE(filterCopy.uid, 0u);

    EXPECT_EQ(positionOf(graph, filterCopy) - positionOf(graph, oscCopy), originalDelta);
}

TEST(ClipboardPaste, LandsOffsetFromTheOriginalRatherThanOnTopOfIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    editor.setSelectedNodes({osc});
    ASSERT_TRUE(editor.copySelection());
    ASSERT_TRUE(editor.pasteClipboard());

    auto copy = theOtherOfType(graph, "Oscillator", osc);
    ASSERT_NE(copy.uid, 0u);

    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(positionOf(graph, copy), positionOf(graph, osc) + juce::Point<int>(step, step));
}

TEST(ClipboardPaste, RepeatedPastesCascadeInsteadOfStacking) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2400, 2000);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    editor.setSelectedNodes({osc});
    ASSERT_TRUE(editor.copySelection());

    std::vector<juce::Point<int>> positions;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(editor.pasteClipboard());
        auto added = editor.getSelectedNodes();
        ASSERT_EQ(added.size(), 1u);
        positions.push_back(positionOf(graph, added[0]));
    }

    EXPECT_NE(positions[0], positions[1]);
    EXPECT_NE(positions[1], positions[2]);
    EXPECT_NE(positions[0], positions[2]);

    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(positions[1] - positions[0], juce::Point<int>(step, step));
    EXPECT_EQ(positions[2] - positions[1], juce::Point<int>(step, step));
}

TEST(ClipboardPaste, PasteAtPlacesTheGroupAtTheRequestedGridSnappedPoint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    editor.setSelectedNodes({osc});
    ASSERT_TRUE(editor.copySelection());

    ASSERT_TRUE(editor.pasteClipboardAt({1003, 707})); // off-grid drop point
    auto added = editor.getSelectedNodes();
    ASSERT_EQ(added.size(), 1u);

    auto pos = positionOf(graph, added[0]);
    EXPECT_EQ(pos, synth::LayoutUtil::snap(juce::Point<int>(1003, 707)));
    EXPECT_EQ(pos.x % synth::LayoutUtil::kGridSize, 0);
    EXPECT_EQ(pos.y % synth::LayoutUtil::kGridSize, 0);
}

TEST(ClipboardPaste, PasteAtReAnchorsTheCascadeForTheNextKeyboardPaste) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2400, 2000);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    editor.setSelectedNodes({osc});
    ASSERT_TRUE(editor.copySelection());

    ASSERT_TRUE(editor.pasteClipboardAt({800, 800}));
    ASSERT_TRUE(editor.pasteClipboard());

    auto added = editor.getSelectedNodes();
    ASSERT_EQ(added.size(), 1u);
    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(positionOf(graph, added[0]), juce::Point<int>(800 + step, 800 + step));
}

TEST(ClipboardPaste, IsOneUndoableChangeForTheWholeGroup) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    ASSERT_TRUE(editor.copySelection());

    const int before = engine.getGraph().getNumNodes();
    ASSERT_TRUE(editor.pasteClipboard());
    ASSERT_EQ(engine.getGraph().getNumNodes(), before + 2);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(engine.getGraph().getNumNodes(), before) << "one Cmd+Z must remove the whole pasted group";
}

// ============================================================================
// Duplicate
// ============================================================================

TEST(ClipboardDuplicate, WithNothingSelectedIsANoOp) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 900);

    const int before = engine.getGraph().getNumNodes();
    EXPECT_FALSE(editor.duplicateSelection());
    EXPECT_EQ(engine.getGraph().getNumNodes(), before);
}

TEST(ClipboardDuplicate, CopiesTheGroupOffsetFromTheOriginalAndSelectsTheCopies) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 560, 240);
    ASSERT_TRUE(graph.addConnection({{osc, 0}, {filter, 0}}));

    editor.setSelectedNodes({osc, filter});
    const int before = graph.getNumNodes();
    ASSERT_TRUE(editor.duplicateSelection());

    EXPECT_EQ(graph.getNumNodes(), before + 2);
    EXPECT_EQ(editor.getSelectionCount(), 2);
    EXPECT_FALSE(editor.isNodeSelected(osc));
    EXPECT_FALSE(editor.isNodeSelected(filter));

    auto oscCopy = theOtherOfType(graph, "Oscillator", osc);
    auto filterCopy = theOtherOfType(graph, "Filter", filter);
    ASSERT_NE(oscCopy.uid, 0u);
    ASSERT_NE(filterCopy.uid, 0u);

    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(positionOf(graph, oscCopy), positionOf(graph, osc) + juce::Point<int>(step, step));
    EXPECT_TRUE(hasConnectionBetween(graph, oscCopy, filterCopy)) << "the duplicate keeps its own internal wiring";
    EXPECT_FALSE(hasConnectionBetween(graph, osc, filterCopy));
}

TEST(ClipboardDuplicate, LeavesTheClipboardUntouched) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    auto vca = addModuleAt(editor, engine, std::make_unique<VCAModule>(), 900, 100);

    editor.setSelectedNodes({osc, filter});
    ASSERT_TRUE(editor.copySelection());
    ASSERT_EQ(editor.getClipboardModuleCount(), 2);

    // Duplicating something else entirely must not cost the user what they had copied.
    editor.setSelectedNodes({vca});
    ASSERT_TRUE(editor.duplicateSelection());
    EXPECT_EQ(editor.getClipboardModuleCount(), 2);

    ASSERT_TRUE(editor.pasteClipboard());
    EXPECT_EQ(countOfType(engine.getGraph(), "Oscillator"), 2);
    EXPECT_EQ(countOfType(engine.getGraph(), "Filter"), 2);
}

TEST(ClipboardDuplicate, WorksWithoutAnyPriorCopy) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    editor.setSelectedNodes({osc});

    ASSERT_FALSE(editor.canPaste());
    EXPECT_TRUE(editor.duplicateSelection());
    EXPECT_EQ(countOfType(engine.getGraph(), "Oscillator"), 2);
    EXPECT_FALSE(editor.canPaste()) << "duplicate must not implicitly fill the clipboard";
}

TEST(ClipboardDuplicate, RepeatsFromTheNewSelectionSoAChainWalksAcrossTheCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2400, 2000);

    auto& graph = engine.getGraph();
    auto osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 160, 240);
    editor.setSelectedNodes({osc});

    ASSERT_TRUE(editor.duplicateSelection());
    auto first = editor.getSelectedNodes();
    ASSERT_EQ(first.size(), 1u);

    // The second duplicate acts on the first copy (which is what is now selected), so it steps on
    // again rather than landing back on top of the previous one.
    ASSERT_TRUE(editor.duplicateSelection());
    auto second = editor.getSelectedNodes();
    ASSERT_EQ(second.size(), 1u);

    const int step = ModuleClipboard::kOffsetStep;
    EXPECT_EQ(positionOf(graph, first[0]), positionOf(graph, osc) + juce::Point<int>(step, step));
    EXPECT_EQ(positionOf(graph, second[0]), positionOf(graph, first[0]) + juce::Point<int>(step, step));
}

TEST(ClipboardDuplicate, IsOneUndoableChangeForTheWholeGroup) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});

    const int before = engine.getGraph().getNumNodes();
    ASSERT_TRUE(editor.duplicateSelection());
    ASSERT_EQ(engine.getGraph().getNumNodes(), before + 2);

    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_EQ(engine.getGraph().getNumNodes(), before);
}
