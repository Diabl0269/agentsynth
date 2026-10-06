// GraphEditor tests for "Connect side by side": a module placed beside a wired neighbour inserts
// itself into the chain with no key held (the default), or only while Ctrl is held (the
// OnlyWithCtrl preference), plus the fade-in of the preview cables and the settle glide on drop.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FX/ChorusModule.h"
#include "UI/Layout/ReducedMotion.h"

namespace {
using SideBySide = SmartConnectionEngine::SideBySideMode;

struct AnimationModeGuard {
    explicit AnimationModeGuard(synth::ui::AnimationMode mode) { synth::ui::setAnimationMode(mode); }
    ~AnimationModeGuard() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

juce::ModifierKeys leftButton() { return juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier); }

struct CardDragFixture {
    juce::AudioProcessorGraph::NodeID upstreamId, targetId, ghostId;
    ModuleComponent* ghostComp = nullptr;
};

// Reverb (40,600) -> Delay (760,100) wired, and an unwired Chorus card parked at (40,300).
CardDragFixture makeCardDragFixture(AudioEngine& engine, GraphEditor& editor) {
    CardDragFixture f;
    auto& graph = engine.getGraph();
    auto target = graph.addNode(std::make_unique<DelayModule>());
    target->properties.set("x", 760);
    target->properties.set("y", 100);
    auto upstream = graph.addNode(std::make_unique<ReverbModule>());
    upstream->properties.set("x", 40);
    upstream->properties.set("y", 600);
    auto ghost = graph.addNode(std::make_unique<ChorusModule>());
    ghost->properties.set("x", 40);
    ghost->properties.set("y", 300);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.targetId = target->nodeID;
    f.upstreamId = upstream->nodeID;
    f.ghostId = ghost->nodeID;
    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    f.ghostComp = findModuleComp(editor, ghost->getProcessor());
    return f;
}

// The real card drag: press on the body, drag by `delta`, release.
void dragCardBy(ModuleComponent& card, juce::Point<int> delta) {
    const juce::Point<int> body{card.getWidth() / 2, card.getHeight() / 2};
    card.mouseDown(makeModuleClickWithMods(card, body, leftButton()));
    card.mouseDrag(makeModuleClickWithMods(card, body + delta, leftButton()));
}
void releaseCard(ModuleComponent& card, juce::Point<int> delta) {
    const juce::Point<int> body{card.getWidth() / 2, card.getHeight() / 2};
    card.mouseUp(makeModuleClickWithMods(card, body + delta, leftButton()));
}
} // namespace

TEST_F(GraphEditorTest, SideBySideDefaultsToAlways) {
    AudioEngine engine;
    GraphEditor editor(engine);
    EXPECT_EQ(editor.getSmartConnections().getSideBySideMode(), SideBySide::Always);
    EXPECT_EQ(SmartConnectionEngine::sideBySideModeFromString("garbage"), SideBySide::Always);
    EXPECT_EQ(SmartConnectionEngine::sideBySideModeFromString(
                  SmartConnectionEngine::sideBySideModeToString(SideBySide::OnlyWithCtrl)),
              SideBySide::OnlyWithCtrl);
}

TEST_F(GraphEditorTest, SideBySideLibraryDropInsertsInSeriesWithNoModifierAsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false); // no key held

    auto& graph = engine.getGraph();
    auto f = makeWiredChain(engine, editor);

    DummyDragSource source;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &source,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << "beside a wired neighbour the default is to insert";
        EXPECT_EQ(s.neighborId, f.targetId);
        EXPECT_EQ(s.upstreamId, f.upstreamId);
    }
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, chorusId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, chorusId, f.targetId), 0);

    ASSERT_TRUE(undoMgr.undo());
    EXPECT_EQ(findNodeIdByName(graph, "Chorus").uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, findNodeIdByName(graph, "Reverb"), findNodeIdByName(graph, "Delay")),
              0)
        << "one undo removes the module and puts the original cable back";
}

TEST_F(GraphEditorTest, SideBySideOnlyWithCtrlLibraryDropDoesNothingWithoutCtrlAndInsertsWithIt) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setSideBySideMode(SideBySide::OnlyWithCtrl);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto f = makeWiredChain(engine, editor);

    DummyDragSource source;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &source,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0) << "no Ctrl: hard stop on the wired jack";

    // Ctrl pressed mid-drag WITHOUT moving the mouse: the next drag tick must pick it up.
    editor.getSmartConnections().setInsertModifierOverrideForTests(true);
    editor.pumpDragModifierTickForTests();
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0)
        << "pressing Ctrl without moving must update the preview";
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions())
        EXPECT_TRUE(s.isInsert);

    // ...and releasing it again takes the preview away, so what is drawn is what the drop applies.
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);
    editor.pumpDragModifierTickForTests();
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    editor.itemDropped(details);

    const auto chorusId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(chorusId.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0) << "the original cable is untouched";
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, chorusId), 0);
    EXPECT_EQ(countAudioConnectionsBetween(graph, chorusId, f.targetId), 0);
}

TEST_F(GraphEditorTest, SideBySideCardDragInsertsInSeriesWithNoModifierAsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(1400, 1000);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto f = makeCardDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> delta{400, -200}; // (40,300) -> (440,100), beside the Delay
    dragCardBy(*f.ghostComp, delta);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartConnections().getSmartSuggestions())
        EXPECT_TRUE(s.isInsert);
    releaseCard(*f.ghostComp, delta);

    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.ghostId), 0);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.ghostId, f.targetId), 0);

    ASSERT_TRUE(undoMgr.undo());
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0)
        << "one undo restores the original cable";
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.ghostId), 0);
}

TEST_F(GraphEditorTest, SideBySideOnlyWithCtrlCardDragDoesNothingWithoutCtrl) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setSideBySideMode(SideBySide::OnlyWithCtrl);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto f = makeCardDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);

    const juce::Point<int> delta{400, -200};
    dragCardBy(*f.ghostComp, delta);
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    releaseCard(*f.ghostComp, delta);
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);
    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.ghostId), 0);
}

TEST_F(GraphEditorTest, SideBySideGroupDragNeverConnects) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1400, 1000);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);

    auto f = makeCardDragFixture(engine, editor);
    ASSERT_NE(f.ghostComp, nullptr);
    // Selecting the ghost together with the (wired) upstream makes this a group drag.
    editor.setSelectedNodes({f.ghostId, f.upstreamId});
    const juce::Point<int> delta{400, -200};
    dragCardBy(*f.ghostComp, delta);
    EXPECT_EQ(editor.getSmartConnections().getSmartSuggestionCount(), 0) << "group drags never smart-connect";
    editor.getDragDropController().endDragPreview();
}

TEST_F(GraphEditorTest, SideBySidePreviewFadesInWhenItAppearsAndLandsAtOnceWithAnimationsOff) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);
    makeWiredChain(engine, editor);

    DummyDragSource source;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &source,
                                                   libraryCursorForGhostTopLeft("Chorus", {440, 100}));
    {
        AnimationModeGuard guard(synth::ui::AnimationMode::full);
        editor.itemDragEnter(details);
        editor.itemDragMove(details);
        ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
        EXPECT_FLOAT_EQ(editor.getSmartPreviewReveal(), 0.0f) << "the cables start invisible and fade in, not pop";
        editor.itemDragExit(details);
    }
    {
        AnimationModeGuard guard(synth::ui::AnimationMode::off);
        editor.itemDragEnter(details);
        editor.itemDragMove(details);
        ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
        EXPECT_FLOAT_EQ(editor.getSmartPreviewReveal(), 1.0f) << "Animations Off lands on the final frame at once";
        editor.itemDragExit(details);
    }
}

TEST_F(GraphEditorTest, SideBySideSnappedLibraryDropSettlesFromWhereTheCursorAimed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1200, 700);
    editor.getSmartConnections().setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.getSmartConnections().setInsertModifierOverrideForTests(false);
    makeWiredChain(engine, editor);

    DummyDragSource source;
    // Off the grid on purpose: the landing slot snaps, so the card has somewhere to glide from.
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &source,
                                                   libraryCursorForGhostTopLeft("Chorus", {447, 103}));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);
    ASSERT_GT(editor.getSmartConnections().getSmartSuggestionCount(), 0);
    const int armsBefore = editor.getCardGlide().armCount();
    editor.itemDropped(details);
    EXPECT_EQ(editor.getCardGlide().armCount(), armsBefore + 1) << "the snapped card settles with a glide";
    editor.getCardGlide().finish();
}
