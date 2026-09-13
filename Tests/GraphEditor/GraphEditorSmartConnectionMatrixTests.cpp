// GraphEditor smart-connection TestWithParam matrices: every FX type insertable aiming at the gap,
// vertical aim across the whole destination card, role-named mono inputs, inserting into a
// summed-pair-fed jack, and the full gesture-matrix contract table.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "../../Source/AppUndoManager.h"
#include "../../Source/Modules/FX/BitcrusherModule.h"
#include "../../Source/Modules/FX/ChorusModule.h"
#include "../../Source/Modules/FX/DelayModule.h"
#include "../../Source/Modules/FX/DistortionModule.h"
#include "../../Source/Modules/FX/ReverbModule.h"
#include "../../Source/Modules/FX/RingModulatorModule.h"
#include "../../Source/Modules/ModuleBase.h"

// --- Every FX must be insertable, aiming AT THE GAP --------------------------
//
// The ghost is centred on the cursor for a library drag, and the insert path relaxes the jack-level
// left-to-right rule, so the natural aim works: put the cursor in the gap between two wired cards
// (or over the cable itself) and the insert is offered. This used to require aiming roughly one
// card-width to the LEFT of the destination, which nobody does — it read as "this module doesn't
// support insert", and a report of exactly that shape turned out to have no per-module cause.
//
// Fixture: upstream at 40 (280 wide, so 40..320), destination at 460 (460..740), leaving a 140px
// visible gap at 320..460. The cursor goes in the middle of that gap.

class SmartConnectionFxInsertTest : public ::testing::TestWithParam<const char*> {};

namespace {
constexpr int kFxUpstreamX = 40;
constexpr int kFxTargetX = 460;
constexpr int kFxGapCentreX = 390; // middle of the 320..460 gap
constexpr int kFxLaneY = 100;
} // namespace

/** Two wired cards with a visible gap, sized as the app sizes them. */
struct FxChainFixture {
    juce::AudioProcessorGraph::NodeID upstreamId, targetId;
};
static FxChainFixture makeFxChain(AudioEngine& engine, GraphEditor& editor) {
    FxChainFixture f;
    auto& graph = engine.getGraph();
    auto upstream = graph.addNode(std::make_unique<ReverbModule>());
    upstream->properties.set("x", kFxUpstreamX);
    upstream->properties.set("y", kFxLaneY);
    auto target = graph.addNode(std::make_unique<DelayModule>());
    target->properties.set("x", kFxTargetX);
    target->properties.set("y", kFxLaneY);
    editor.updateComponents();
    sizeModuleComponents(editor);
    f.upstreamId = upstream->nodeID;
    f.targetId = target->nodeID;
    editor.connectPorts(f.upstreamId, 0, f.targetId, 0, false, false);
    return f;
}

TEST_P(SmartConnectionFxInsertTest, CtrlDragAtTheGapInsertsThisFx) {
    const juce::String fxName(GetParam());

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held

    auto& graph = engine.getGraph();
    auto f = makeFxChain(engine, editor);
    ASSERT_GT(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0);

    std::set<juce::uint32> before;
    for (auto* n : graph.getNodes())
        before.insert(n->nodeID.uid);

    // Cursor IN THE GAP — the aim a user actually takes.
    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails details(juce::var(fxName), &dummySource,
                                                   juce::Point<int>(kFxGapCentreX, kFxLaneY));
    editor.itemDragEnter(details);
    editor.itemDragMove(details);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << fxName << " offered nothing with the cursor over the gap";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << fxName << " offered a plain connect instead of an insert";
        EXPECT_EQ(s.neighborId, f.targetId) << fxName << " aimed at the wrong neighbour";
        EXPECT_EQ(s.upstreamId, f.upstreamId) << fxName << " picked the wrong upstream";
    }

    editor.itemDropped(details);

    juce::AudioProcessorGraph::NodeID ghostId{};
    for (auto* n : graph.getNodes())
        if (before.find(n->nodeID.uid) == before.end())
            ghostId = n->nodeID;
    ASSERT_NE(ghostId.uid, 0u) << fxName << " was not created on drop";

    EXPECT_EQ(countAudioConnectionsBetween(graph, f.upstreamId, f.targetId), 0)
        << fxName << " left the original cable in place";
    EXPECT_GT(countAudioConnectionsBetween(graph, f.upstreamId, ghostId), 0) << fxName << " is not fed by the upstream";
    EXPECT_GT(countAudioConnectionsBetween(graph, ghostId, f.targetId), 0) << fxName << " does not feed the target";
}

INSTANTIATE_TEST_SUITE_P(AllFxTypes, SmartConnectionFxInsertTest,
                         ::testing::Values("Distortion", "Delay", "Reverb", "Chorus", "Phaser", "Flanger", "Compressor",
                                           "Limiter", "Gate", "Bitcrusher", "Pitch Shifter", "Parametric EQ",
                                           "Ring Modulator"));

TEST_F(GraphEditorTest, SmartConnectionInsertAimWindowSpansTheWholeGap) {
    // Every cursor position across the visible gap must offer the insert, and so must a cursor over
    // the destination's own left half (aiming at the cable that ends there). Dragged clean PAST the
    // destination must not.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto f = makeFxChain(engine, editor);
    DummyDragSource dummySource;

    auto insertsAt = [&](int cursorX) {
        juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &dummySource,
                                                 juce::Point<int>(cursorX, kFxLaneY));
        editor.itemDragEnter(d);
        editor.itemDragMove(d);
        int n = 0;
        for (const auto& s : editor.getSmartSuggestions())
            if (s.isInsert)
                ++n;
        editor.endDragPreview();
        return n;
    };

    // The whole visible gap, end to end.
    for (int x = 320; x <= 460; x += 10)
        EXPECT_GT(insertsAt(x), 0) << "no insert with the cursor at x=" << x << ", inside the gap";

    // Over the destination card itself — aiming at the cable's far end.
    EXPECT_GT(insertsAt(520), 0) << "no insert with the cursor over the destination's left half";

    // Dragged clean past the destination: its centre is beyond the card's right edge, so this is not
    // "insert into it" any more.
    EXPECT_EQ(insertsAt(900), 0) << "an insert was offered for a ghost dragged well past the destination";
}

TEST_F(GraphEditorTest, SmartConnectionPlainSuggestionKeepsTheLeftToRightFlowRule) {
    // The relaxation is scoped to the insert path. WITHOUT the modifier, a ghost sitting to the right
    // of a module must still not have that module's outputs wrapped back into it.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false); // no Ctrl

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 400);
    delayNode->properties.set("y", kFxLaneY);
    editor.updateComponents();
    sizeModuleComponents(editor);

    // Ghost centred to the RIGHT of the Delay: its own output must not be proposed backwards into
    // the Delay's input, and the Delay must not be proposed as a source into the ghost's left inputs
    // from a position the ghost has already passed.
    DummyDragSource dummySource;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &dummySource, juce::Point<int>(760, kFxLaneY));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_FALSE(s.isInsert) << "no modifier, so nothing may be rerouted";
        EXPECT_FALSE(s.ghostIsSource) << "a ghost to the right must not feed the module on its left";
    }
    editor.endDragPreview();
}

// --- Vertical aim: the whole destination card, both axes ---------------------
//
// Reported as "it doesn't always recognize the audio output, only when I dragged it a bit below".
// The vertical acceptance region was never the problem: it already spanned the card plus a generous
// margin. What actually happened is that only ONE neighbour's candidate group survives selection,
// and a ghost being spliced into a cable sits between TWO valid neighbours — the upstream it is
// being inserted after is a perfectly good plain "feed the new module" candidate. It was winning on
// proximity and discarding the insert, and which of the two won flipped with small cursor moves, so
// nudging down appeared to fix it. Inserts now outrank plain candidates.

/** Insert offered at each cursor Y over a vertical span, for a destination at destY. */
struct VerticalAimProbe {
    int firstHit = -1, lastHit = -1, hits = 0, gaps = 0;
};
static VerticalAimProbe probeVerticalAim(GraphEditor& editor, juce::AudioProcessorGraph::NodeID destId, int cursorX,
                                         int fromY, int toY, int step) {
    VerticalAimProbe p;
    DummyDragSource ds;
    bool wasHit = false;
    for (int cy = fromY; cy <= toY; cy += step) {
        juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &ds, juce::Point<int>(cursorX, cy));
        editor.itemDragEnter(d);
        editor.itemDragMove(d);
        bool hit = false;
        for (const auto& s : editor.getSmartSuggestions())
            if (s.isInsert && s.neighborId == destId)
                hit = true;
        editor.endDragPreview();

        if (hit) {
            if (p.firstHit < 0)
                p.firstHit = cy;
            p.lastHit = cy;
            ++p.hits;
        } else if (wasHit && cy < p.lastHit) {
            ++p.gaps;
        }
        wasHit = hit;
    }
    // A gap is a miss strictly between two hits.
    return p;
}

/** Sizes every card the way the app does, from the footprint table, rather than one uniform size.
 *  Load-bearing here: the Audio Output card is only ~100px tall, so a uniform 300px would not
 *  reproduce anything about it. */
static void sizeModuleComponentsRealistically(GraphEditor& editor) {
    auto* content = editor.getChildComponent(0);
    ASSERT_NE(content, nullptr);
    for (auto* child : content->getChildren())
        if (auto* mc = dynamic_cast<ModuleComponent*>(child)) {
            const auto size = GraphEditor::estimateModuleSize(mc->getModule()->getName());
            mc->setSize(size.x, size.y);
        }
}

class SmartConnectionVerticalAimTest : public ::testing::TestWithParam<bool> {};

TEST_P(SmartConnectionVerticalAimTest, InsertIsOfferedAcrossTheWholeDestinationCardHeight) {
    const bool destIsAudioOutput = GetParam();
    const bool nearUpstream = true; // the real shape: last FX sits right beside the destination

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1600);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    constexpr int kDestX = 460;
    constexpr int kLaneY = 400;

    juce::AudioProcessorGraph::NodeID destId;
    if (destIsAudioOutput) {
        destId = addAudioOutputNode(graph, kDestX, kLaneY)->nodeID;
    } else {
        auto d = graph.addNode(std::make_unique<DelayModule>());
        d->properties.set("x", kDestX);
        d->properties.set("y", kLaneY);
        destId = d->nodeID;
    }
    auto up = graph.addNode(std::make_unique<ReverbModule>());
    up->properties.set("x", nearUpstream ? 120 : 40);
    up->properties.set("y", kLaneY);
    editor.updateComponents();
    sizeModuleComponentsRealistically(editor);
    editor.connectPorts(up->nodeID, 0, destId, 0, false, false);

    int destTop = 0, destBottom = 0;
    for (auto* child : editor.getChildComponent(0)->getChildren())
        if (auto* mc = dynamic_cast<ModuleComponent*>(child))
            if (mc->getNodeId() == destId) {
                destTop = mc->getY();
                destBottom = mc->getBottom();
            }
    ASSERT_GT(destBottom, destTop);

    const auto probe = probeVerticalAim(editor, destId, /*cursorX=*/390, /*fromY=*/destTop - 300,
                                        /*toY=*/destBottom + 300, /*step=*/10);

    ASSERT_GT(probe.hits, 0) << "no insert offered at ANY cursor height over the destination";
    EXPECT_EQ(probe.gaps, 0) << "the vertical window has holes in it, which is what 'flaky' means";

    // Aiming anywhere over the card body must work, same rule as the horizontal axis.
    EXPECT_LE(probe.firstHit, destTop) << "aiming at the card's top edge is refused";
    EXPECT_GE(probe.lastHit, destBottom) << "aiming at the card's bottom edge is refused";
}

INSTANTIATE_TEST_SUITE_P(DestinationKinds, SmartConnectionVerticalAimTest, ::testing::Values(true, false),
                         [](const testing::TestParamInfo<bool>& i) {
                             return i.param ? "AudioOutput" : "OrdinaryModule";
                         });

TEST_F(GraphEditorTest, SmartConnectionInsertOutranksAPlainSuggestionFromTheUpstream) {
    // The arbitration bug in isolation: the upstream is a valid plain neighbour AND the destination
    // is a valid insert target. Only one group survives, and it must be the insert.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1200);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto outNode = addAudioOutputNode(graph, 460, 400);
    auto up = graph.addNode(std::make_unique<ReverbModule>());
    up->properties.set("x", 120); // close enough to be its own candidate
    up->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponentsRealistically(editor);
    editor.connectPorts(up->nodeID, 0, outNode->nodeID, 0, false, false);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &ds, juce::Point<int>(390, 450));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert) << "a plain suggestion from the upstream hid the insert";
        EXPECT_EQ(s.neighborId, outNode->nodeID);
        EXPECT_EQ(s.upstreamId, up->nodeID);
    }
    editor.endDragPreview();
}

// --- Ctrl with nothing to insert must still connect normally -----------------
//
// Regression: "an insert outranks every plain candidate" was an overcorrection. With Ctrl held, an
// insert into some occupied module in range stole the drop from the free module the user was aiming
// at, so Ctrl+drag stopped connecting anything ordinary. The demotion is now targeted: only a plain
// offer coming FROM the cable's own upstream loses to the insert.

TEST_F(GraphEditorTest, SmartConnectionCtrlWithNothingToInsertStillConnectsOnLibraryDrop) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true); // Ctrl held, but nothing is occupied

    auto& graph = engine.getGraph();
    auto dest = graph.addNode(std::make_unique<DelayModule>());
    dest->properties.set("x", 700);
    dest->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &ds,
                                             libraryCursorForGhostTopLeft("Chorus", {700 - 280 - 40, 400}));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "Ctrl must not suppress an ordinary connection";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_FALSE(s.isInsert) << "there is nothing occupied to insert into";
        EXPECT_EQ(s.neighborId, dest->nodeID);
    }
    editor.itemDropped(d);

    const auto ghostId = findNodeIdByName(graph, "Chorus");
    ASSERT_NE(ghostId.uid, 0u);
    EXPECT_GT(countAudioConnectionsBetween(graph, ghostId, dest->nodeID), 0) << "the plain cable was never applied";
}

TEST_F(GraphEditorTest, SmartConnectionCtrlWithNothingToInsertStillConnectsOnCanvasMove) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto dest = graph.addNode(std::make_unique<DelayModule>());
    dest->properties.set("x", 700);
    dest->properties.set("y", 400);
    auto ghost = graph.addNode(std::make_unique<ChorusModule>());
    ghost->properties.set("x", 40);
    ghost->properties.set("y", 900);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* ghostComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c->getNodeId() == ghost->nodeID)
            ghostComp = c;
    ASSERT_NE(ghostComp, nullptr);

    editor.beginDragPreview(ghostComp->getWidth(), ghostComp->getHeight(), ghost->nodeID);
    editor.updateDragPreview({700 - 280 - 40, 400}); // a move is top-left anchored
    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "Ctrl must not suppress an ordinary connection on a move";
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_FALSE(s.isInsert);
    editor.finalizeModuleDrag(ghostComp);
    editor.endDragPreview();

    EXPECT_GT(countAudioConnectionsBetween(graph, ghost->nodeID, dest->nodeID), 0);
}

TEST_F(GraphEditorTest, SmartConnectionInsertDoesNotStealFromTheModuleBeingAimedAt) {
    // An occupied module in range offers an insert; a FREE module right under the cursor offers a
    // plain connect. Aim must win — the insert may only outrank a plain offer from its own upstream.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2400, 1400);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto freeDest = graph.addNode(std::make_unique<DelayModule>());
    freeDest->properties.set("x", 700);
    freeDest->properties.set("y", 400);
    auto occUp = graph.addNode(std::make_unique<ReverbModule>());
    occUp->properties.set("x", 120);
    occUp->properties.set("y", 400);
    auto occDest = graph.addNode(std::make_unique<ChorusModule>());
    occDest->properties.set("x", 430);
    occDest->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.connectPorts(occUp->nodeID, 0, occDest->nodeID, 0, false, false);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Chorus"), &ds, juce::Point<int>(660, 450));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_EQ(s.neighborId, freeDest->nodeID) << "the insert stole the drop from the module under the cursor";
        EXPECT_FALSE(s.isInsert);
    }
    editor.endDragPreview();
}

// --- Role-named mono audio inputs (Ring Modulator Carrier / Modulator) -------
//
// Ring Modulator's input side is five discrete jacks: Carrier, Modulator, and three CV. Both audio
// jacks carry PortRole::Other from the inherited input map, so classification is label/role driven
// rather than index driven: collectSmartAudioLegs takes the FIRST audio-ish jack as the module's one
// mono audio input and never treats two unlabeled jacks as a stereo pair. Carrier is therefore the
// proximity target and Modulator is left for the user to patch deliberately.

TEST_F(GraphEditorTest, SmartConnectionDualUpstreamSumsBothLegsIntoRingModulatorCarrier) {
    // Previously only ONE leg landed: both pairs target Carrier's single raw channel, and the
    // redundancy dedupe keyed on the destination raw alone, so it discarded the second leg. Summing
    // two source legs into one dedicated mono input is the intent, not a duplicate.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto ring = graph.addNode(std::make_unique<RingModulatorModule>());
    ring->properties.set("x", 460);
    ring->properties.set("y", 400);
    auto dist = graph.addNode(std::make_unique<DistortionModule>());
    dist->properties.set("x", 40);
    dist->properties.set("y", 400);
    setDualIOParam(*dist->getProcessor(), true); // dual OUTPUT: separate Left/Right jacks
    editor.updateComponents();
    sizeModuleComponents(editor);

    auto* ringMb = dynamic_cast<ModuleBase*>(ring->getProcessor());
    ASSERT_NE(ringMb, nullptr);
    ASSERT_EQ(ringMb->getInputPortLabel(0), "Carrier");
    ASSERT_EQ(ringMb->getInputPortLabel(1), "Modulator");

    ModuleComponent* distComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c->getNodeId() == dist->nodeID)
            distComp = c;
    ASSERT_NE(distComp, nullptr);

    editor.beginDragPreview(distComp->getWidth(), distComp->getHeight(), dist->nodeID);
    editor.updateDragPreview({140, 400});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0);
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_EQ(s.neighborJack, 0) << "only Carrier may be proposed; Modulator is patched deliberately";
    editor.finalizeModuleDrag(distComp);
    editor.endDragPreview();

    // Both legs summed into Carrier (raw 0), and nothing on Modulator (raw 1).
    EXPECT_TRUE(graph.isConnected({{dist->nodeID, 0}, {ring->nodeID, 0}})) << "Left leg missing from Carrier";
    EXPECT_TRUE(graph.isConnected({{dist->nodeID, 1}, {ring->nodeID, 0}})) << "Right leg missing from Carrier";
    EXPECT_FALSE(graph.isConnected({{dist->nodeID, 0}, {ring->nodeID, 1}}));
    EXPECT_FALSE(graph.isConnected({{dist->nodeID, 1}, {ring->nodeID, 1}}));
    EXPECT_TRUE(editor.isInputJackFreeForTests(ring->nodeID, 1)) << "Modulator must never be auto-wired";
}

TEST_F(GraphEditorTest, SmartConnectionRingModulatorAsSourceIsUnchanged) {
    // Its OUTPUT side is an ordinary collapsible stereo pair; only the input side is special.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 900);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(false);

    auto& graph = engine.getGraph();
    auto dest = graph.addNode(std::make_unique<DelayModule>());
    dest->properties.set("x", 700);
    dest->properties.set("y", 400);
    auto ring = graph.addNode(std::make_unique<RingModulatorModule>());
    ring->properties.set("x", 40);
    ring->properties.set("y", 900);
    editor.updateComponents();
    sizeModuleComponents(editor);

    ModuleComponent* ringComp = nullptr;
    for (auto* c : editor.getModuleComponents())
        if (c->getNodeId() == ring->nodeID)
            ringComp = c;
    ASSERT_NE(ringComp, nullptr);

    editor.beginDragPreview(ringComp->getWidth(), ringComp->getHeight(), ring->nodeID);
    editor.updateDragPreview({700 - 280 - 40, 400});
    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "Ring Modulator must still work as a source";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.ghostIsSource);
        EXPECT_EQ(s.neighborId, dest->nodeID);
    }
    editor.finalizeModuleDrag(ringComp);
    editor.endDragPreview();
    EXPECT_GT(countAudioConnectionsBetween(graph, ring->nodeID, dest->nodeID), 0);
}

// --- Insert into a jack fed by a SUMMED PAIR from one upstream ---------------
//
// The user's shape: a DUAL Delay (separate L/R output jacks) feeding a COLLAPSED Bitcrusher's single
// Audio jack through TWO cables (Delay L -> Audio, Delay R -> Audio). That is not an exotic patch —
// it is what the Dual I/O toggle rewire and a hand-dragged pair both produce, and what our own
// dual-to-mono summing rules now yield routinely.
//
// findSingleUpstreamAudioLink refused ANY jack with more than one feed, so Ctrl+drag offered nothing
// there. A multi-feed jack whose feeds all come from ONE node's legs is our own canonical wiring;
// only feeds from DIFFERENT nodes are a hand-built mix worth protecting.

/** Dual `upstream`'s two legs SUMMED into `dest`'s collapsed audio input — the screenshot's wiring,
 *  and what the Dual I/O toggle rewire produces ("sums Audio R into a collapsed mono destination
 *  jack, matching hand-wiring").
 *
 *  Built at raw-edge level on purpose. Going through connectPorts twice does NOT reproduce it: its
 *  fan rules turn the first cable into a stereo pair and the second into a mono broadcast, landing
 *  three edges in a different layout. This fixture needs the exact state the user is in, so it
 *  states it directly rather than hoping two fans compose into it. */
static void wireDualUpstreamSummedIntoCollapsedInput(juce::AudioProcessorGraph& graph,
                                                     juce::AudioProcessorGraph::NodeID upstreamId,
                                                     juce::AudioProcessorGraph::NodeID destId) {
    graph.addConnection({{upstreamId, 0}, {destId, 0}}); // Left  -> Audio
    graph.addConnection({{upstreamId, 1}, {destId, 0}}); // Right -> Audio (summed onto the same leg)
}

TEST_F(GraphEditorTest, SmartConnectionInsertsIntoAJackFedByASummedDualPair) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(2000, 1200);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    delayNode->properties.set("x", 40);
    delayNode->properties.set("y", 400);
    setDualIOParam(*delayNode->getProcessor(), true);                       // dual: separate Left/Right output jacks
    auto crusherNode = graph.addNode(std::make_unique<BitcrusherModule>()); // collapsed single Audio in
    crusherNode->properties.set("x", 760);
    crusherNode->properties.set("y", 400);
    editor.updateComponents();
    sizeModuleComponents(editor);

    const auto delayId = delayNode->nodeID;
    const auto crusherId = crusherNode->nodeID;
    wireDualUpstreamSummedIntoCollapsedInput(graph, delayId, crusherId);
    ASSERT_TRUE(graph.isConnected({{delayId, 0}, {crusherId, 0}}));
    ASSERT_TRUE(graph.isConnected({{delayId, 1}, {crusherId, 0}}));
    // Two cables into one jack, from two legs of ONE node: the shape the guard used to refuse.
    ASSERT_EQ(countAudioConnectionsBetween(graph, delayId, crusherId), 2);

    std::set<juce::uint32> before;
    for (auto* n : graph.getNodes())
        before.insert(n->nodeID.uid);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Distortion"), &ds,
                                             libraryCursorForGhostTopLeft("Distortion", {760 - 280 - 40, 400}));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);

    ASSERT_GT(editor.getSmartSuggestionCount(), 0) << "a summed dual pair is our own wiring, not a hand-built mix";
    for (const auto& s : editor.getSmartSuggestions()) {
        EXPECT_TRUE(s.isInsert);
        EXPECT_EQ(s.neighborId, crusherId);
        EXPECT_EQ(s.upstreamId, delayId);
        EXPECT_EQ(s.doomedLinks.size(), 2u) << "both summed legs must be doomed, not just one";
    }
    editor.itemDropped(d);

    juce::AudioProcessorGraph::NodeID ghostId{};
    for (auto* n : graph.getNodes())
        if (before.find(n->nodeID.uid) == before.end())
            ghostId = n->nodeID;
    ASSERT_NE(ghostId.uid, 0u);

    // Both original cables gone, the ghost spliced in, nothing left summing into the destination.
    EXPECT_EQ(countAudioConnectionsBetween(graph, delayId, crusherId), 0)
        << "a surviving leg would keep summing in beside the ghost";
    EXPECT_GT(countAudioConnectionsBetween(graph, delayId, ghostId), 0) << "the upstream must feed the ghost";
    EXPECT_GT(countAudioConnectionsBetween(graph, ghostId, crusherId), 0) << "the ghost must feed the destination";
    // Neither upstream leg may be lost on the way into the ghost. Asserted as channel coverage
    // rather than exact raw pairs: which raw channel each leg lands on is the fan's business (and is
    // pinned by the dedicated dual/collapsed tests), but a leg with NO edge at all is a dropped
    // channel, which is the bug this shape used to have.
    bool leftReachesGhost = false, rightReachesGhost = false;
    for (const auto& conn : graph.getConnections()) {
        if (conn.source.nodeID != delayId || conn.destination.nodeID != ghostId)
            continue;
        if (conn.source.channelIndex == 0)
            leftReachesGhost = true;
        if (conn.source.channelIndex == 1)
            rightReachesGhost = true;
    }
    EXPECT_TRUE(leftReachesGhost) << "Left leg lost on the way into the ghost";
    EXPECT_TRUE(rightReachesGhost) << "Right leg lost on the way into the ghost";

    // One undo step restores the original summed pair exactly.
    ASSERT_TRUE(undoMgr.undo());
    const auto restoredDelay = findNodeIdByName(graph, "Delay");
    const auto restoredCrusher = findNodeIdByName(graph, "Bitcrusher");
    ASSERT_NE(restoredDelay.uid, 0u);
    ASSERT_NE(restoredCrusher.uid, 0u);
    EXPECT_EQ(countAudioConnectionsBetween(graph, restoredDelay, restoredCrusher), 2)
        << "one undo must put both summed cables back";
}

TEST_F(GraphEditorTest, SmartConnectionStillRefusesAJackMixedFromDifferentNodes) {
    // The guard that must stay: two DIFFERENT upstream nodes summed into one jack is a hand-built
    // mix, and rerouting it would silently change what sums where.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);
    editor.setInsertModifierOverrideForTests(true);

    auto& graph = engine.getGraph();
    auto crusher = graph.addNode(std::make_unique<BitcrusherModule>());
    crusher->properties.set("x", 760);
    crusher->properties.set("y", 400);
    auto upA = graph.addNode(std::make_unique<DelayModule>());
    upA->properties.set("x", 40);
    upA->properties.set("y", 1000);
    auto upB = graph.addNode(std::make_unique<ReverbModule>());
    upB->properties.set("x", 40);
    upB->properties.set("y", 1200);
    editor.updateComponents();
    sizeModuleComponents(editor);
    editor.connectPorts(upA->nodeID, 0, crusher->nodeID, 0, false, false);
    editor.connectPorts(upB->nodeID, 0, crusher->nodeID, 0, false, false);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails d(juce::var("Distortion"), &ds,
                                             libraryCursorForGhostTopLeft("Distortion", {760 - 280 - 40, 400}));
    editor.itemDragEnter(d);
    editor.itemDragMove(d);
    for (const auto& s : editor.getSmartSuggestions())
        EXPECT_FALSE(s.isInsert) << "a mix from two different nodes must never be rerouted";
    editor.endDragPreview();
}

// --- Gesture matrix: the whole smart-connect contract in one table -----------
//
// Two regressions in this subsystem shipped past a green suite (the round-6 aim anchoring, then the
// Ctrl-plain suppression) because each fix was covered only by tests for the case it fixed. This
// sweeps every modifier state against every destination kind on both gestures, so a behaviour change
// has to EDIT THE TABLE deliberately instead of being discovered by a user.
//
// The four modifier states collapse to two effective values at evaluation time, but they exercise
// different plumbing: held-before goes through the press-time path, pressed/released-mid-drag go
// through the live per-tick re-sample that had no coverage before round 6.
//
// Aim is the clear-left position for every cell, so the MODIFIER is the only variable. (Aiming at
// the gap is deliberately insert-only — see the note in the report; a plain suggestion still
// requires the flow rule, which an overlapping ghost fails.)

enum class GestureOutcome { None, Plain, Insert };

static const char* outcomeName(GestureOutcome o) {
    switch (o) {
    case GestureOutcome::None:
        return "None";
    case GestureOutcome::Plain:
        return "Plain";
    case GestureOutcome::Insert:
        return "Insert";
    }
    return "?";
}

enum class ModifierGesture { NoModifier, CtrlHeldBefore, CtrlPressedMidDrag, CtrlReleasedMidDrag };
enum class DestKind { FreeOrdinary, OccupiedOrdinary, OccupiedAudioOutput, FreeAudioOutput, OccupiedBySummedPair };

struct MatrixRow {
    DestKind dest;
    ModifierGesture modifier;
    GestureOutcome expected;
};

// EXPECTED BEHAVIOUR TABLE - edit deliberately.
//
//   destination            | no modifier | Ctrl (any of the three Ctrl-down-at-evaluation gestures)
//   -----------------------+-------------+--------------------------------------------------------
//   free ordinary          | Plain       | Plain   (nothing occupied to insert into)
//   occupied ordinary      | None        | Insert  (hard stop without the modifier)
//   occupied Audio Output  | Plain       | Insert  (the sink's parallel-add is that Plain)
//   free Audio Output      | Plain       | Plain
//   occupied by summed pair| None        | Insert  (one upstream's L+R summed into a collapsed in)
//
// CtrlReleasedMidDrag ends with the modifier UP, so it must match the no-modifier column.
static const MatrixRow kGestureMatrix[] = {
    {DestKind::FreeOrdinary, ModifierGesture::NoModifier, GestureOutcome::Plain},
    {DestKind::FreeOrdinary, ModifierGesture::CtrlHeldBefore, GestureOutcome::Plain},
    {DestKind::FreeOrdinary, ModifierGesture::CtrlPressedMidDrag, GestureOutcome::Plain},
    {DestKind::FreeOrdinary, ModifierGesture::CtrlReleasedMidDrag, GestureOutcome::Plain},

    {DestKind::OccupiedOrdinary, ModifierGesture::NoModifier, GestureOutcome::None},
    {DestKind::OccupiedOrdinary, ModifierGesture::CtrlHeldBefore, GestureOutcome::Insert},
    {DestKind::OccupiedOrdinary, ModifierGesture::CtrlPressedMidDrag, GestureOutcome::Insert},
    {DestKind::OccupiedOrdinary, ModifierGesture::CtrlReleasedMidDrag, GestureOutcome::None},

    {DestKind::OccupiedAudioOutput, ModifierGesture::NoModifier, GestureOutcome::Plain},
    {DestKind::OccupiedAudioOutput, ModifierGesture::CtrlHeldBefore, GestureOutcome::Insert},
    {DestKind::OccupiedAudioOutput, ModifierGesture::CtrlPressedMidDrag, GestureOutcome::Insert},
    {DestKind::OccupiedAudioOutput, ModifierGesture::CtrlReleasedMidDrag, GestureOutcome::Plain},

    {DestKind::FreeAudioOutput, ModifierGesture::NoModifier, GestureOutcome::Plain},
    {DestKind::FreeAudioOutput, ModifierGesture::CtrlHeldBefore, GestureOutcome::Plain},
    {DestKind::FreeAudioOutput, ModifierGesture::CtrlPressedMidDrag, GestureOutcome::Plain},
    {DestKind::FreeAudioOutput, ModifierGesture::CtrlReleasedMidDrag, GestureOutcome::Plain},

    // One upstream's Left AND Right legs summed into a collapsed mono input: our own canonical
    // dual-to-mono wiring, which the multi-feed guard used to refuse outright.
    {DestKind::OccupiedBySummedPair, ModifierGesture::NoModifier, GestureOutcome::None},
    {DestKind::OccupiedBySummedPair, ModifierGesture::CtrlHeldBefore, GestureOutcome::Insert},
    {DestKind::OccupiedBySummedPair, ModifierGesture::CtrlPressedMidDrag, GestureOutcome::Insert},
    {DestKind::OccupiedBySummedPair, ModifierGesture::CtrlReleasedMidDrag, GestureOutcome::None},
};

struct GestureMatrixCase {
    MatrixRow row;
    bool libraryDrop;
};

static std::vector<GestureMatrixCase> allGestureMatrixCases() {
    std::vector<GestureMatrixCase> cases;
    for (const auto& row : kGestureMatrix)
        for (bool libraryDrop : {true, false})
            cases.push_back({row, libraryDrop});
    return cases;
}

class SmartConnectionGestureMatrixTest : public ::testing::TestWithParam<GestureMatrixCase> {};

TEST_P(SmartConnectionGestureMatrixTest, MatchesTheExpectedOutcome) {
    const auto& c = GetParam();

    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(2000, 1400);
    editor.setSmartConnectionMode(GraphEditor::SmartConnectionMode::NewAndUnwired);

    auto& graph = engine.getGraph();
    constexpr int kDestX = 760;
    constexpr int kLaneY = 400;
    const bool destIsSink = c.row.dest == DestKind::OccupiedAudioOutput || c.row.dest == DestKind::FreeAudioOutput;
    const bool summedPair = c.row.dest == DestKind::OccupiedBySummedPair;
    const bool destOccupied =
        c.row.dest == DestKind::OccupiedOrdinary || c.row.dest == DestKind::OccupiedAudioOutput || summedPair;

    juce::AudioProcessorGraph::NodeID destId;
    if (destIsSink) {
        destId = addAudioOutputNode(graph, kDestX, kLaneY)->nodeID;
    } else {
        auto d = graph.addNode(std::make_unique<DelayModule>());
        d->properties.set("x", kDestX);
        d->properties.set("y", kLaneY);
        destId = d->nodeID;
    }

    juce::AudioProcessorGraph::NodeID upstreamId{};
    if (destOccupied) {
        // Parked far away so it is not itself a neighbour candidate: this matrix is about the
        // modifier, and arbitration between two neighbours has its own dedicated tests.
        if (summedPair) {
            auto up = graph.addNode(std::make_unique<DelayModule>());
            up->properties.set("x", 40);
            up->properties.set("y", 1100);
            setDualIOParam(*up->getProcessor(), true); // dual out, so it has two legs to sum
            upstreamId = up->nodeID;
        } else {
            auto up = graph.addNode(std::make_unique<ReverbModule>());
            up->properties.set("x", 40);
            up->properties.set("y", 1100);
            upstreamId = up->nodeID;
        }
    }

    juce::AudioProcessorGraph::NodeID ghostId{};
    if (!c.libraryDrop) {
        auto g = graph.addNode(std::make_unique<ChorusModule>());
        g->properties.set("x", 40);
        g->properties.set("y", 40);
        ghostId = g->nodeID;
    }

    editor.updateComponents();
    sizeModuleComponents(editor);
    if (summedPair)
        wireDualUpstreamSummedIntoCollapsedInput(graph, upstreamId, destId);
    else if (destOccupied)
        editor.connectPorts(upstreamId, 0, destId, 0, false, false);

    // Clear-left aim: the ghost's top-left one card-width plus a small gap before the destination.
    const juce::Point<int> ghostTopLeft{kDestX - 280 - 40, kLaneY};

    const bool startsWithCtrl =
        c.row.modifier == ModifierGesture::CtrlHeldBefore || c.row.modifier == ModifierGesture::CtrlReleasedMidDrag;
    editor.setInsertModifierOverrideForTests(startsWithCtrl);

    DummyDragSource ds;
    juce::DragAndDropTarget::SourceDetails details(juce::var("Chorus"), &ds,
                                                   libraryCursorForGhostTopLeft("Chorus", ghostTopLeft));
    ModuleComponent* ghostComp = nullptr;
    if (c.libraryDrop) {
        editor.itemDragEnter(details);
        editor.itemDragMove(details);
    } else {
        for (auto* mc : editor.getModuleComponents())
            if (mc->getNodeId() == ghostId)
                ghostComp = mc;
        ASSERT_NE(ghostComp, nullptr);
        editor.beginDragPreview(ghostComp->getWidth(), ghostComp->getHeight(), ghostId);
        editor.updateDragPreview(ghostTopLeft);
    }

    // Mid-drag modifier transitions go through the live per-tick re-sample, with NO mouse movement.
    if (c.row.modifier == ModifierGesture::CtrlPressedMidDrag) {
        editor.setInsertModifierOverrideForTests(true);
        editor.pumpDragModifierTickForTests();
    } else if (c.row.modifier == ModifierGesture::CtrlReleasedMidDrag) {
        editor.setInsertModifierOverrideForTests(false);
        editor.pumpDragModifierTickForTests();
    }

    GestureOutcome actual = GestureOutcome::None;
    for (const auto& s : editor.getSmartSuggestions()) {
        if (s.neighborId != destId)
            continue;
        actual = s.isInsert ? GestureOutcome::Insert : GestureOutcome::Plain;
        break;
    }

    EXPECT_EQ(actual, c.row.expected) << "gesture=" << (c.libraryDrop ? "libraryDrop" : "canvasMove")
                                      << " dest=" << (int)c.row.dest << " modifier=" << (int)c.row.modifier
                                      << " expected=" << outcomeName(c.row.expected)
                                      << " actual=" << outcomeName(actual);
    editor.endDragPreview();
}

INSTANTIATE_TEST_SUITE_P(EveryCell, SmartConnectionGestureMatrixTest, ::testing::ValuesIn(allGestureMatrixCases()));
