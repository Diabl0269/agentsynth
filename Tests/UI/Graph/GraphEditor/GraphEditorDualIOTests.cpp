// GraphEditor Dual I/O tests: splitting/collapsing wiring across split-block and mid-chain voice
// modules (migrating summed pairs rather than duplicating cables, one-undo-step guarantees), and
// render-identity checks that the rendered mix does not move sideways when Dual I/O flips.
// Shared GraphEditorTest fixture and helpers live in GraphEditorTestHelpers.h.

#include "GraphEditorTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/ADSRModule.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/FX/DistortionModule.h"
#include "Modules/FX/RingModulatorModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"

// ---------------------------------------------------------------------------
// Dual I/O toggle: wiring the right leg (issue: "toggling Dual I/O on leaves Audio R dangling")
//
// A split-block source (its Audio R on a kRightBase block) is the case the FX-pair tests above do
// not cover, and it is the one that reached a user: an Oscillator split into Audio L / Audio R with
// only Audio L wired, and no cable on Audio R at all.
// ---------------------------------------------------------------------------

namespace {
// Every card the editor built, by the processor it fronts.
ModuleComponent* cardFor(GraphEditor& editor, juce::AudioProcessor* proc) {
    if (auto* content = editor.getChildComponent(0))
        for (auto* child : content->getChildren())
            if (auto* mod = dynamic_cast<ModuleComponent*>(child))
                if (mod->getModule() == proc)
                    return mod;
    return nullptr;
}

bool graphHasEdge(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID src, int srcCh,
                  juce::AudioProcessorGraph::NodeID dst, int dstCh) {
    for (const auto& c : graph.getConnections())
        if (c.source.nodeID == src && c.source.channelIndex == srcCh && c.destination.nodeID == dst &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

int feedCount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID dst, int dstCh) {
    int n = 0;
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == dst && !c.destination.isMIDI() && c.destination.channelIndex == dstCh)
            ++n;
    return n;
}
} // namespace

TEST_F(GraphEditorTest, SplittingASplitBlockSourceWiresAudioRIntoACollapsedFXDestination) {
    // Oscillator (Audio R at kRightBase) into a collapsed Delay, wired while the Oscillator was
    // collapsed. Splitting it must move the Delay's right raw leg over to Audio R — and the
    // duplicate of Audio L that was standing in for it must go, or Right carries L+R.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    editor.updateComponents();

    auto* oscComp = cardFor(editor, oscNode->getProcessor());
    auto* delayComp = cardFor(editor, delayNode->getProcessor());
    ASSERT_NE(oscComp, nullptr);
    ASSERT_NE(delayComp, nullptr);
    oscComp->setBounds(0, 0, 200, 400);
    delayComp->setBounds(300, 0, 200, 400);

    editor.beginConnectionDrag(oscComp, 0, false, false, juce::Point<int>(0, 0));
    editor.dragConnection(juce::Point<int>(50, 0));
    editor.endConnectionDrag(delayComp->getBounds().getPosition() + delayComp->getPortCenter(0, true));

    ASSERT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 0));
    ASSERT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 1))
        << "the collapsed pair starts out fed twice from the one Audio jack";

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 0)) << "Audio L keeps the left leg";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, delayNode->nodeID, 1))
        << "Audio R must be wired onto the collapsed destination's second raw leg, not left dangling";
    EXPECT_EQ(feedCount(graph, delayNode->nodeID, 1), 1)
        << "the destination's right leg must be fed exactly once — Audio R, not Audio L + Audio R";
}

TEST_F(GraphEditorTest, SplittingASourceWiresAudioRIntoADualDestinationsOwnRightBlock) {
    // Both ends split-block: the right leg lands on the DESTINATION's kRightBase, never on its ch1
    // (which is the Filter's Cutoff CV).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(filterNode->getProcessor())->isDualIO()) << "Filter defaults to dual";
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID,
                             FilterModule::kRightBase))
        << "Audio R must reach the destination's own Audio R block";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 1))
        << "ch1 is the Filter's Cutoff CV and must never be wired as audio";
}

TEST_F(GraphEditorTest, SplittingASourceSumsAudioRIntoACollapsedPeersMonoJackNeverItsHiddenBlock) {
    // The reported screenshot: a collapsed Oscillator wired into a COLLAPSED Filter's single Audio
    // jack, then split. Audio R used to come up visibly dangling because the Filter has no second
    // audio input jack to pair with.
    //
    // USER RULING: wire it into that same mono jack — a summed second cable, exactly what dragging
    // both legs there by hand produces. Two things the ruling did NOT change, both still asserted
    // here: the destination's hidden kRightBase block stays unwired (no audible, unpluggable cable),
    // and a destination that HAS a right leg still gets a real pair
    // (SplittingAMidChainVoiceModuleWiresAllFourLegsWhenTheDownstreamCanTakeIt, and dual->dual in
    // SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "the left leg is untouched";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 0))
        << "Audio R must be wired into the collapsed destination's mono jack, not left dangling";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 2) << "exactly one extra cable: Audio L plus Audio R";
    for (int ch = FilterModule::kRightBase; ch < filterNode->getProcessor()->getTotalNumInputChannels(); ++ch)
        EXPECT_EQ(feedCount(graph, filterNode->nodeID, ch), 0)
            << "nothing may be wired onto a hidden right block (channel " << ch << ")";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 1))
        << "ch1 is the Filter's Cutoff CV and must never be wired as audio";

    // ...and toggling back off round-trips exactly: the extra cable rides the hidden right block, so
    // the collapse-drops rule takes it away and the single mono cable is all that is left.
    setDualIOParam(*oscNode->getProcessor(), false);
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1) << "collapsing must remove the summed second cable";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "leaving the original cable";
}

// ---------------------------------------------------------------------------
// Expanding a DESTINATION that already carries a summed pair: migrate, never add
//
// The sum above is a cable aimed at a jack that no longer exists once the destination splits. The
// reported sequence made that concrete: Osc to dual left two cables on the Filter's mono jack, and
// then splitting the FILTER wired a third (L->L, the old R->L sum, plus a new R->R). Expanding has
// to MOVE the second feed onto the new Audio R, so every jack ends up with exactly one cable.
// ---------------------------------------------------------------------------

TEST_F(GraphEditorTest, ExpandingADestinationMigratesASummedPairInsteadOfAddingAThirdCable) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    auto edgesBetween = [&] {
        int n = 0;
        for (const auto& c : graph.getConnections())
            if (c.source.nodeID == oscNode->nodeID && c.destination.nodeID == filterNode->nodeID && !c.source.isMIDI())
                ++n;
        return n;
    };

    // Step 1 - split the SOURCE: its Audio R is summed into the Filter's still-mono jack.
    setDualIOParam(*oscNode->getProcessor(), true);
    ASSERT_EQ(edgesBetween(), 2);
    ASSERT_EQ(feedCount(graph, filterNode->nodeID, 0), 2);

    // Step 2 - split the DESTINATION. This is the bug: it used to add a third cable.
    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_EQ(edgesBetween(), 2) << "expanding the destination must migrate the sum, not add to it";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "L to L";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID,
                             FilterModule::kRightBase))
        << "R to R, moved off the mono jack rather than duplicated";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1) << "exactly one cable per jack";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 1);
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 0))
        << "the old summed cable must be gone, not left alongside the new pair";
}

TEST_F(GraphEditorTest, SplittingASourceSumsAudioRIntoADedicatedMonoAudioInput) {
    // The Ring Modulator's Carrier (ch0) and Modulator (ch1) are two mono audio inputs with distinct
    // roles - never a stereo pair. Splitting an Oscillator that feeds Modulator used to wire Audio L
    // and stop, because the rewire only looked at cables landing on the destination's ch0. Same
    // ruling as the collapsed mono jack: sum the right leg into that very input.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto ringNode = graph.addNode(std::make_unique<RingModulatorModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {ringNode->nodeID, 1}})) << "Osc into Modulator";
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, ringNode->nodeID, 1)) << "Audio L keeps the Modulator input";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, ringNode->nodeID, 1))
        << "Audio R must land on the same Modulator input, not dangle";
    EXPECT_EQ(feedCount(graph, ringNode->nodeID, 1), 2) << "exactly one extra cable";
    EXPECT_EQ(feedCount(graph, ringNode->nodeID, 0), 0) << "Carrier is a different jack and must stay empty";
    for (int ch = 2; ch < 5; ++ch)
        EXPECT_EQ(feedCount(graph, ringNode->nodeID, ch), 0)
            << "and no audio may be dumped onto a CV jack (channel " << ch << ")";

    // Collapse round-trips: the extra cable hangs off the hidden right block.
    setDualIOParam(*oscNode->getProcessor(), false);
    EXPECT_EQ(feedCount(graph, ringNode->nodeID, 1), 1);
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, ringNode->nodeID, 1));
}

TEST_F(GraphEditorTest, SplittingASourceNeverSumsAudioRIntoAModulationInput) {
    // The counterpart guard: a cable the user aimed at a CV jack must not gain an audio-rate copy of
    // the right leg. Filter ch2 is Resonance CV.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 2}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 2), 1) << "a CV jack gains nothing from a split";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID, 2));
}

TEST_F(GraphEditorTest, ExpandingADestinationMigratesOneRawOfACollapsedUpstreamJack) {
    // The duplicate-of-one-jack variant: a collapsed Delay's single Audio jack owns raw0 AND raw1, and
    // both are wired onto a collapsed Filter's mono jack. Expanding the Filter moves the raw1 feed
    // onto Audio R, because that raw is the upstream's right leg even though it fronts one jack.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(delayNode->getProcessor())->isDualIO());
    ASSERT_TRUE(graph.addConnection({{delayNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{delayNode->nodeID, 1}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, delayNode->nodeID, 0, filterNode->nodeID, 0)) << "raw0 stays on Audio L";
    EXPECT_TRUE(graphHasEdge(graph, delayNode->nodeID, 1, filterNode->nodeID, FilterModule::kRightBase))
        << "raw1 migrates to Audio R";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1);
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 1);
}

TEST_F(GraphEditorTest, ExpandingADestinationLeavesAHandBuiltMixOfTwoSourcesAlone) {
    // Two feeds from two DIFFERENT modules is a mix the user built. Splitting must not move half of
    // it onto the new jack, and with the mono jack no longer single-fed it does not broadcast either.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscA = graph.addNode(std::make_unique<OscillatorModule>());
    auto oscB = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscA->getProcessor(), false);
    setDualIOParam(*oscB->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscA->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscB->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 2) << "the hand-built mix stays intact";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 0)
        << "and nothing is moved or copied onto Audio R";
}

TEST_F(GraphEditorTest, TheWholeSplitThenCollapseSequenceReturnsToOneMonoCable) {
    // Split source, split destination, collapse destination, collapse source: back to the single
    // cable the patch started with, with nothing stranded on either hidden right block.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);
    setDualIOParam(*filterNode->getProcessor(), true);
    setDualIOParam(*filterNode->getProcessor(), false);
    setDualIOParam(*oscNode->getProcessor(), false);

    int audioEdges = 0;
    for (const auto& c : graph.getConnections())
        if (!c.source.isMIDI() && !c.destination.isMIDI())
            ++audioEdges;
    EXPECT_EQ(audioEdges, 1) << "the sequence must land back on exactly one cable";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "and it is the original one";
}

// ---------------------------------------------------------------------------
// Dual I/O toggle on a SPLIT-BLOCK module that was wired while collapsed
//
// Reported shape: a Filter sitting mid-chain, flipped to Dual I/O while patched. Audio L in and out
// stayed wired and BOTH right jacks came up dangling, because a collapsed neighbour exposes no
// second audio jack for rightAudioLegOf() to find. The ruling: a module the user just split must
// arrive with both legs live.
//
// Both sides end up wired, by different means:
//   * INPUT  - copy the feed the left leg already has onto the right leg. Driving one more
//              destination from the same source channel cannot change the mix.
//   * OUTPUT - wire the right leg to the destination's right leg when the destination HAS one
//              (collapsed FX pair raw1, or a dual peer's own block). When it has none, wire it into
//              the destination's mono jack as a summed second cable, per user ruling: a dangling
//              Audio R was the complaint, and stereo-into-mono summing is what hand-wiring both
//              legs there already does. While the legs are still identical that sum is +6 dB, which
//              is transient - it lasts until the legs differ, which is why one splits.
//
// One thing neither side may do: wire the peer's HIDDEN kRightBase block. A cable there is audible
// and impossible to unplug, and dropHiddenRightLegConnections exists to keep it that way.
// ---------------------------------------------------------------------------

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModuleFeedsItsRightInputFromAMonoUpstream) {
    // The screenshot, exactly: collapsed Oscillator -> Filter -> collapsed VCA, and the Filter is the
    // one being toggled. Audio R in is broadcast-fed from the mono upstream; Audio R out is summed
    // into the collapsed VCA's mono jack (see the header comment).
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    setDualIOParam(*vcaNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "Audio L in survives";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, 0, vcaNode->nodeID, 0)) << "Audio L out survives";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "Audio R in must be fed by the mono upstream, not left dangling";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 1)
        << "and fed exactly once, so a second toggle cannot stack feeds";

    // The output side. This assertion was the reverse until the user ruled on it: it used to require
    // exactly one feed on the VCA's mono jack, on the grounds that summing two identical legs makes a
    // layout toggle +6 dB louder. The ruling accepted that transient jump in exchange for both jacks
    // being wired, so the same shape now expects the second cable - while still never touching the
    // VCA's hidden right block.
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, vcaNode->nodeID, 0))
        << "Audio R out must be summed into the collapsed VCA's mono jack";
    EXPECT_EQ(feedCount(graph, vcaNode->nodeID, 0), 2) << "exactly one extra cable, not a stack of them";
    for (int ch = VCAModule::kRightBase; ch < vcaNode->getProcessor()->getTotalNumInputChannels(); ++ch)
        EXPECT_EQ(feedCount(graph, vcaNode->nodeID, ch), 0) << "no cable onto the collapsed VCA's hidden block";

    // Toggling back off leaves the mono chain exactly as it started, on both sides: the broadcast and
    // the summed cable both hang off the Filter's hidden right block, so the collapse-drops rule
    // takes them with it.
    setDualIOParam(*filterNode->getProcessor(), false);
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0));
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 0)
        << "collapsing unhooks the hidden right leg again";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1) << "and does not re-point the broadcast onto the left leg";
    EXPECT_EQ(feedCount(graph, vcaNode->nodeID, 0), 1) << "and the summed second cable is gone";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, 0, vcaNode->nodeID, 0)) << "leaving the original cable";
}

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModuleWiresAllFourLegsWhenTheDownstreamCanTakeIt) {
    // Same shape with an FX downstream: a collapsed Distortion's one Audio jack owns raw0 AND raw1,
    // so the right leg has a legal, visible target and all four of the Filter's jacks end up wired.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_FALSE(dynamic_cast<ModuleBase*>(distNode->getProcessor())->isDualIO()) << "FX default to collapsed";
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {distNode->nodeID, 1}}))
        << "a mono feed into a collapsed FX pair fans onto both raw legs";
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, 0)) << "Audio L in";
    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "Audio R in, broadcast from the mono upstream";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, 0, distNode->nodeID, 0)) << "Audio L out";
    EXPECT_TRUE(graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, distNode->nodeID, 1))
        << "Audio R out must reach the collapsed pair's second raw leg";
    EXPECT_FALSE(graphHasEdge(graph, filterNode->nodeID, 0, distNode->nodeID, 1))
        << "and the left leg's stand-in copy must go with it, or the right leg carries L+R";
    EXPECT_EQ(feedCount(graph, distNode->nodeID, 1), 1);
}

TEST_F(GraphEditorTest, SplittingAnOutputOnlyVoiceModuleTouchesOnlyItsOutputSide) {
    // The Oscillator has no audio input at all (its 14 inputs are pitch and mod CV), so the input
    // half of the rewire must not fire, and its kRightBase must not be mistaken for an audio in.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto delayNode = graph.addNode(std::make_unique<DelayModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {delayNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {delayNode->nodeID, 1}}));
    editor.updateComponents();

    setDualIOParam(*oscNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, delayNode->nodeID, 1))
        << "Audio R out pairs with the collapsed Delay's second raw leg";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, 0, delayNode->nodeID, 1)) << "no double feed";

    for (int ch = 0; ch < oscNode->getProcessor()->getTotalNumInputChannels(); ++ch)
        EXPECT_EQ(feedCount(graph, oscNode->nodeID, ch), 0)
            << "an output-only module must gain no input cables (channel " << ch << ")";
}

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModulePrefersRealRightLegsOverABroadcast) {
    // Dual neighbours on both sides: the real Audio R blocks win, and no broadcast happens. This is
    // the rule that keeps TogglingDualIOKeepsBothStereoLegs true - a copy of Audio L stands in for
    // the right leg only while there is no right leg to be had.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(oscNode->getProcessor())->isDualIO()) << "voice modules default to dual";
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->isDualIO());
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{filterNode->nodeID, 0}, {vcaNode->nodeID, 0}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), true);

    EXPECT_TRUE(graphHasEdge(graph, oscNode->nodeID, OscillatorModule::kRightBase, filterNode->nodeID,
                             FilterModule::kRightBase))
        << "Audio R in comes from the upstream's own right block";
    EXPECT_FALSE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "so no broadcast of Audio L";
    EXPECT_TRUE(
        graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, vcaNode->nodeID, VCAModule::kRightBase))
        << "Audio R out reaches the downstream's own right block, never its ch1 CV";
    EXPECT_FALSE(graphHasEdge(graph, filterNode->nodeID, FilterModule::kRightBase, vcaNode->nodeID, 1))
        << "ch1 is the VCA gain CV";
}

TEST_F(GraphEditorTest, SplittingAMidChainVoiceModuleIsOneUndoableStep) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    setDualIOParam(*oscNode->getProcessor(), false);
    setDualIOParam(*filterNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    editor.updateComponents();
    ASSERT_NE(cardFor(editor, filterNode->getProcessor()), nullptr) << "the card is what listens to the gesture";

    juce::AudioProcessorParameter* dualParam = nullptr;
    for (auto* p : filterNode->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            dualParam = p;
    ASSERT_NE(dualParam, nullptr);

    dualParam->beginChangeGesture();
    dualParam->setValueNotifyingHost(1.0f);
    dualParam->endChangeGesture();

    ASSERT_TRUE(graphHasEdge(graph, oscNode->nodeID, 0, filterNode->nodeID, FilterModule::kRightBase))
        << "the broadcast happened";

    ASSERT_TRUE(undoMgr.undo());

    juce::AudioProcessorGraph::NodeID osc, filter;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<OscillatorModule*>(node->getProcessor()) != nullptr)
            osc = node->nodeID;
        if (dynamic_cast<FilterModule*>(node->getProcessor()) != nullptr)
            filter = node->nodeID;
    }
    ASSERT_NE(filter.uid, 0u);

    auto* restoredFilter = dynamic_cast<ModuleBase*>(graph.getNodeForId(filter)->getProcessor());
    ASSERT_NE(restoredFilter, nullptr);
    EXPECT_FALSE(restoredFilter->isDualIO()) << "undo restores the parameter";
    EXPECT_TRUE(graphHasEdge(graph, osc, 0, filter, 0)) << "and the mono cable it was wired with";
    EXPECT_EQ(feedCount(graph, filter, FilterModule::kRightBase), 0) << "undo takes the broadcast back out";
}

// ---------------------------------------------------------------------------
// Dual I/O toggle: collapsing (issue: "turning Dual I/O off biases the mix left")
// ---------------------------------------------------------------------------

TEST_F(GraphEditorTest, CollapsingRePointsTheRightLegCableOntoTheSurvivingLeftLeg) {
    // The default patch's shape: VCA's Audio R block feeds the FX chain's ch1. Collapsing the VCA
    // hides that block, so the cable moves to the leg that survives instead of vanishing — without
    // it, the whole collapsed FX tail renders silence on the right.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    ASSERT_TRUE(dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->isDualIO());
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, VCAModule::kRightBase}, {distNode->nodeID, 1}}));
    editor.updateComponents();

    setDualIOParam(*vcaNode->getProcessor(), false);

    EXPECT_FALSE(graphHasEdge(graph, vcaNode->nodeID, VCAModule::kRightBase, distNode->nodeID, 1))
        << "the hidden right block must not keep a cable";
    EXPECT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, distNode->nodeID, 1))
        << "the cable must re-point onto the surviving left leg, or the right channel goes silent";
    EXPECT_EQ(feedCount(graph, distNode->nodeID, 1), 1);
}

TEST_F(GraphEditorTest, CollapsingRePointsTheRightLegCableOntoAudioOutput) {
    // Same rule when the far end is the graph's Audio Output node, which is not a ModuleBase: its
    // Right channel is still a real, visible jack, so the cable moves there.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto outNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {outNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, VCAModule::kRightBase}, {outNode->nodeID, 1}}));
    editor.updateComponents();

    setDualIOParam(*vcaNode->getProcessor(), false);

    EXPECT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, outNode->nodeID, 0));
    EXPECT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, outNode->nodeID, 1))
        << "collapsing must not leave the hardware's right channel unfed";
    EXPECT_FALSE(graphHasEdge(graph, vcaNode->nodeID, VCAModule::kRightBase, outNode->nodeID, 1));
}

TEST_F(GraphEditorTest, CollapsingDoesNotDoubleFeedACollapsedInputJack) {
    // The input-side mirror is deliberately NOT symmetric: our left input is already fed by the
    // same upstream, and re-pointing there too would sum L+R into one mono jack.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto filterNode = graph.addNode(std::make_unique<FilterModule>());
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {filterNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection(
        {{oscNode->nodeID, OscillatorModule::kRightBase}, {filterNode->nodeID, FilterModule::kRightBase}}));
    editor.updateComponents();

    setDualIOParam(*filterNode->getProcessor(), false);

    EXPECT_EQ(feedCount(graph, filterNode->nodeID, 0), 1)
        << "the surviving mono input must keep exactly one feed, not L + R summed";
    EXPECT_EQ(feedCount(graph, filterNode->nodeID, FilterModule::kRightBase), 0);
}

TEST_F(GraphEditorTest, CollapsingIsOneUndoableStepIncludingTheRewire) {
    // The rewire rides inside the parameter gesture's own snapshot, so one undo puts back both the
    // parameter and every cable the collapse moved.
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, VCAModule::kRightBase}, {distNode->nodeID, 1}}));
    editor.updateComponents();
    ASSERT_NE(cardFor(editor, vcaNode->getProcessor()), nullptr) << "the card is what listens to the gesture";

    juce::AudioProcessorParameter* dualParam = nullptr;
    for (auto* p : vcaNode->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            dualParam = p;
    ASSERT_NE(dualParam, nullptr);

    dualParam->beginChangeGesture();
    dualParam->setValueNotifyingHost(0.0f);
    dualParam->endChangeGesture();

    ASSERT_FALSE(dynamic_cast<ModuleBase*>(vcaNode->getProcessor())->isDualIO());
    ASSERT_TRUE(graphHasEdge(graph, vcaNode->nodeID, 0, distNode->nodeID, 1)) << "the re-point happened";

    ASSERT_TRUE(undoMgr.undo());

    auto* restoredVca = [&]() -> juce::AudioProcessorGraph::Node* {
        for (auto* node : graph.getNodes())
            if (dynamic_cast<VCAModule*>(node->getProcessor()) != nullptr)
                return node;
        return nullptr;
    }();
    auto* restoredDist = [&]() -> juce::AudioProcessorGraph::Node* {
        for (auto* node : graph.getNodes())
            if (dynamic_cast<DistortionModule*>(node->getProcessor()) != nullptr)
                return node;
        return nullptr;
    }();
    ASSERT_NE(restoredVca, nullptr);
    ASSERT_NE(restoredDist, nullptr);

    EXPECT_TRUE(dynamic_cast<ModuleBase*>(restoredVca->getProcessor())->isDualIO()) << "undo restores the parameter";
    EXPECT_TRUE(graphHasEdge(graph, restoredVca->nodeID, VCAModule::kRightBase, restoredDist->nodeID, 1))
        << "undo restores the right-leg cable the collapse moved";
    EXPECT_FALSE(graphHasEdge(graph, restoredVca->nodeID, 0, restoredDist->nodeID, 1))
        << "and takes the re-pointed duplicate back out";
}

TEST_F(GraphEditorTest, SplittingIsOneUndoableStepIncludingTheRewire) {
    AudioEngine engine;
    AppUndoManager undoMgr;
    GraphEditor editor(engine, &undoMgr);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    auto vcaNode = graph.addNode(std::make_unique<VCAModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    setDualIOParam(*vcaNode->getProcessor(), false);
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{vcaNode->nodeID, 0}, {distNode->nodeID, 1}}));
    editor.updateComponents();

    juce::AudioProcessorParameter* dualParam = nullptr;
    for (auto* p : vcaNode->getProcessor()->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == "dualIO")
            dualParam = p;
    ASSERT_NE(dualParam, nullptr);

    dualParam->beginChangeGesture();
    dualParam->setValueNotifyingHost(1.0f);
    dualParam->endChangeGesture();

    ASSERT_TRUE(graphHasEdge(graph, vcaNode->nodeID, VCAModule::kRightBase, distNode->nodeID, 1));
    ASSERT_FALSE(graphHasEdge(graph, vcaNode->nodeID, 0, distNode->nodeID, 1));

    ASSERT_TRUE(undoMgr.undo());

    juce::AudioProcessorGraph::NodeID vca, dist;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<VCAModule*>(node->getProcessor()) != nullptr)
            vca = node->nodeID;
        if (dynamic_cast<DistortionModule*>(node->getProcessor()) != nullptr)
            dist = node->nodeID;
    }
    EXPECT_TRUE(graphHasEdge(graph, vca, 0, dist, 1)) << "undo puts the collapsed jack's duplicate back";
    EXPECT_FALSE(graphHasEdge(graph, vca, VCAModule::kRightBase, dist, 1));
}

// ---------------------------------------------------------------------------
// …and what all of the above is FOR: the rendered mix must not move sideways when Dual I/O flips.
// ---------------------------------------------------------------------------

namespace {
constexpr double kRenderSampleRate = 44100.0;
constexpr int kRenderBlockSize = 512;

struct StereoLevels {
    float left = 0.0f;
    float right = 0.0f;
};

StereoLevels renderStereoRms(juce::AudioProcessorGraph& graph, int totalSamples) {
    graph.prepareToPlay(kRenderSampleRate, kRenderBlockSize);

    for (auto* node : graph.getNodes())
        if (auto* kb = dynamic_cast<MidiKeyboardModule*>(node->getProcessor()))
            kb->getKeyboardState().noteOn(1, 60, 1.0f);

    juce::AudioBuffer<float> result(2, totalSamples);
    result.clear();
    int rendered = 0;
    while (rendered < totalSamples) {
        const int n = std::min(kRenderBlockSize, totalSamples - rendered);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        graph.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom(ch, rendered, block, ch, 0, n);
        rendered += n;
    }

    // Drop the first block: the note starts there and the FX tail has not filled yet.
    const int skip = std::min(kRenderBlockSize, totalSamples - 1);
    return {result.getRMSLevel(0, skip, totalSamples - skip), result.getRMSLevel(1, skip, totalSamples - skip)};
}
} // namespace

TEST_F(GraphEditorTest, CollapsingDualIOAcrossTheDefaultPatchKeepsLeftAndRightLevelsEqual) {
    // The reported bug, end to end: "turning Dual I/O off puts more sound on the left, which should
    // not happen". The default patch carries both legs from the Oscillator down to the FX tail
    // (Distortion → Delay → Reverb → Audio Output); collapsing every module used to drop the VCA's
    // right-leg cable into Distortion ch1 and leave every right channel after it silent.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kRenderSampleRate, kRenderBlockSize);
    ASSERT_TRUE(synth::PresetManager::loadDefaultPreset(graph));
    editor.updateComponents();

    const auto before = renderStereoRms(graph, static_cast<int>(kRenderSampleRate));
    ASSERT_GT(before.left, 1.0e-4f) << "nothing was rendered, so nothing is proven";
    ASSERT_GT(before.right, 1.0e-4f) << "the patch is not stereo before the toggle";

    editor.applyDualIOToExistingModules(false);

    const auto after = renderStereoRms(graph, static_cast<int>(kRenderSampleRate));
    ASSERT_GT(after.left, 1.0e-4f) << "the collapse silenced the patch outright";
    EXPECT_GT(after.right, 1.0e-4f) << "the right channel went silent — the mix collapsed to the left";
    EXPECT_NEAR(after.right / after.left, 1.0f, 0.35f)
        << "L/R must stay level through a collapse (L=" << after.left << " R=" << after.right << ")";
}

TEST_F(GraphEditorTest, CollapsingDualIOKeepsAMonoChainBitEqualInBothChannels) {
    // The same claim with the Reverb's stereo width taken out of the picture: MIDI → Oscillator →
    // Distortion → Audio Output has no channel-dependent DSP at all, so after a collapse the two
    // output channels must be identical, not merely close. The Oscillator is the split-block end
    // here — collapsing it is what drops the kRightBase cable that feeds Distortion ch1.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(800, 600);

    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, kRenderSampleRate, kRenderBlockSize);

    auto keysNode = graph.addNode(std::make_unique<MidiKeyboardModule>());
    auto oscNode = graph.addNode(std::make_unique<OscillatorModule>());
    auto distNode = graph.addNode(std::make_unique<DistortionModule>());
    auto outNode = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    ASSERT_TRUE(graph.addConnection({{keysNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                     {oscNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, 0}, {distNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{oscNode->nodeID, OscillatorModule::kRightBase}, {distNode->nodeID, 1}}));
    ASSERT_TRUE(graph.addConnection({{distNode->nodeID, 0}, {outNode->nodeID, 0}}));
    ASSERT_TRUE(graph.addConnection({{distNode->nodeID, 1}, {outNode->nodeID, 1}}));
    editor.updateComponents();

    editor.applyDualIOToExistingModules(false);

    const auto after = renderStereoRms(graph, kRenderBlockSize * 4);
    ASSERT_GT(after.left, 1.0e-4f);
    EXPECT_NEAR(after.right, after.left, 1.0e-6f)
        << "a collapsed mono chain must arrive at the same level in both output channels";
}

TEST_F(GraphEditorTest, ResolvePolyLinkFansCollapsedStereoSourceOntoStereoDest) {
    DelayModule src;
    DelayModule dst;

    auto toOutput = GraphEditor::resolvePolyLink(&src, 0, nullptr, 0);
    EXPECT_EQ(toOutput.sourceRawChannel, 0);
    EXPECT_EQ(toOutput.destRawChannel, 0);
    EXPECT_EQ(toOutput.voiceCount, 2);
    EXPECT_EQ(toOutput.sourceStride, 1);

    auto toFx = GraphEditor::resolvePolyLink(&src, 0, &dst, 0);
    EXPECT_EQ(toFx.voiceCount, 2);
    EXPECT_EQ(toFx.sourceStride, 1);
}

TEST_F(GraphEditorTest, ResolvePolyLinkDoesNotBroadcastAudioOrPitchFans) {
    // Broadcasting is limited to ModCV. Audio would build a paraphonic voice stack, and Pitch/Gate
    // would make all eight voices sound the same note — both stay single head-to-head wires.
    OscillatorModule monoOsc; // poly defaults to false
    FilterModule polyFilter;
    setPolyParam(polyFilter, true);

    auto audioLink = GraphEditor::resolvePolyLink(&monoOsc, 0, &polyFilter, 0);
    EXPECT_EQ(audioLink.voiceCount, 1);
    EXPECT_EQ(audioLink.sourceStride, 1);

    LFOModule lfo;
    OscillatorModule polyOsc;
    setPolyParam(polyOsc, true);

    auto pitchLink = GraphEditor::resolvePolyLink(&lfo, 0, &polyOsc, 0);
    EXPECT_EQ(pitchLink.voiceCount, 1);
    EXPECT_EQ(pitchLink.sourceStride, 1);

    ADSRModule polyAdsr;
    setPolyParam(polyAdsr, true);

    auto gateLink = GraphEditor::resolvePolyLink(&lfo, 0, &polyAdsr, 0);
    EXPECT_EQ(gateLink.voiceCount, 1);
    EXPECT_EQ(gateLink.sourceStride, 1);
}
