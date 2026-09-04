// GraphEditor-level tests for the Macro bypass/mute fan-out (P8-15d, T142, docs/macros.md §5.6):
// "Bypass macro" / "Mute macro" fan ModuleBase::setBypassed/setMuted out over every member as ONE
// undo step. Both setters are already parameter writes via setValueNotifyingHost, so this is an
// ordinary parameter change -- no new mutation mechanism, and no macro-level reinterpretation of
// the per-module bypass/mute contract (each member's own processBlock keeps honouring it exactly
// as it does for a single-module toggle -- this suite never touches a processBlock).
//
//   • fan-out        -- setMacroBypassed/setMacroMuted lands on every member, as one undo step
//   • mute skip      -- a member with no "muted" parameter (Macro In/Out and their MIDI variants,
//                        matching Track In / Rec Tap / Track Audio) is left alone, not crashed
//   • tri-state read -- macroBypassState/macroMuteState report AllOff/AllOn/Mixed, and mute-only
//                        state ignores mute-ineligible members entirely
//   • convergence    -- toggleMacroBypassed/toggleMacroMuted converge a Mixed or AllOff state to
//                        ON, and an AllOn state to OFF, mirroring toggleSelectionMacrosCollapsed
//   • refusals       -- an unknown macro id, and a macro with no mute-eligible member, are no-ops
//                        (the latter via onStatusMessage, matching every other macro refusal)
//   • card layout    -- the collapsed card's title row never overlaps a badge slot, however long
//                        the macro's name is (both rectangles come from ONE layout definition,
//                        MacroCardComponent::getToggleBadgeBounds, the same principle T141's
//                        macroCardPortLayout applies to port jacks)

#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/MacroInletModule.h"
#include "../Source/Modules/MacroOutletModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

using NodeID = juce::AudioProcessorGraph::NodeID;

namespace {

NodeID addModuleAt(GraphEditor& editor, AudioEngine& engine, std::unique_ptr<juce::AudioProcessor> processor, int x,
                   int y) {
    auto node = engine.getGraph().addNode(std::move(processor));
    node->properties.set("x", x);
    node->properties.set("y", y);
    node->properties.set("uuid", juce::Uuid().toDashedString());
    editor.updateComponents();
    return node->nodeID;
}

NodeID nodeIdForUuid(AudioEngine& engine, const juce::String& uuid) {
    for (auto* node : engine.getGraph().getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

ModuleBase* moduleAt(AudioEngine& engine, NodeID id) {
    auto* node = engine.getGraph().getNodeForId(id);
    return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
}

/** Groups two fresh Oscillator/Filter modules (both mute-eligible) into a new collapsed macro
 *  and returns its id, plus the two members' node ids. */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine, NodeID& a, NodeID& b) {
    a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.groupSelectionIntoMacro();
}

/** Groups two mute-ineligible Macro In/Out nodes (no "muted" parameter -- see
 *  ModuleBase::hasMuteParameter, §7 item 1) into a macro. Every member IS still bypassable
 *  (ModuleBase's constructor adds "bypassed" unconditionally), so this is the fixture for
 *  "no mute-eligible member" without also being "no bypassable member". */
juce::String makeMuteIneligibleMacro(GraphEditor& editor, AudioEngine& engine, NodeID& a, NodeID& b) {
    a = addModuleAt(editor, engine, std::make_unique<MacroInletModule>(), 100, 100);
    b = addModuleAt(editor, engine, std::make_unique<MacroOutletModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.groupSelectionIntoMacro();
}

} // namespace

// ============================================================================
// Fan-out + one undo step
// ============================================================================

TEST(MacroBypassMute, SetMacroBypassedSetsEveryMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    editor.setMacroBypassed(macroId, true);

    EXPECT_TRUE(moduleAt(engine, a)->isBypassed());
    EXPECT_TRUE(moduleAt(engine, b)->isBypassed());
}

TEST(MacroBypassMute, SetMacroMutedSetsEveryMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    editor.setMacroMuted(macroId, true);

    EXPECT_TRUE(moduleAt(engine, a)->isMuted());
    EXPECT_TRUE(moduleAt(engine, b)->isMuted());
}

TEST(MacroBypassMute, BypassFanOutIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    editor.setMacroBypassed(macroId, true);
    ASSERT_TRUE(moduleAt(engine, a)->isBypassed());
    ASSERT_TRUE(moduleAt(engine, b)->isBypassed());

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    // ONE undo() reverted BOTH members -- not one undo per member.
    EXPECT_FALSE(moduleAt(engine, a)->isBypassed());
    EXPECT_FALSE(moduleAt(engine, b)->isBypassed());

    // ONE redo() re-applies BOTH members too -- "one undo step" implies one redo step.
    ASSERT_TRUE(undo.canRedo());
    undo.redo();
    EXPECT_TRUE(moduleAt(engine, a)->isBypassed());
    EXPECT_TRUE(moduleAt(engine, b)->isBypassed());
}

TEST(MacroBypassMute, MuteFanOutIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    editor.setMacroMuted(macroId, true);
    ASSERT_TRUE(moduleAt(engine, a)->isMuted());
    ASSERT_TRUE(moduleAt(engine, b)->isMuted());

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_FALSE(moduleAt(engine, a)->isMuted());
    EXPECT_FALSE(moduleAt(engine, b)->isMuted());

    // ONE redo() re-applies BOTH members too -- "one undo step" implies one redo step.
    ASSERT_TRUE(undo.canRedo());
    undo.redo();
    EXPECT_TRUE(moduleAt(engine, a)->isMuted());
    EXPECT_TRUE(moduleAt(engine, b)->isMuted());
}

// ============================================================================
// Mute skips members with no "muted" parameter
// ============================================================================

TEST(MacroBypassMute, MuteSkipsAMemberWithNoMuteParameterWithoutCrashing) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    // A Macro In port added to the macro is a member with NO "muted" parameter (§7 item 1).
    const auto portUuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(portUuid.isEmpty());
    auto* inlet = moduleAt(engine, nodeIdForUuid(engine, portUuid));
    ASSERT_NE(inlet, nullptr);
    ASSERT_FALSE(inlet->hasMuteParameter());

    editor.setMacroMuted(macroId, true); // must not crash on inlet's unset mutedParam

    EXPECT_TRUE(moduleAt(engine, a)->isMuted());
    EXPECT_TRUE(moduleAt(engine, b)->isMuted());
    // Nothing to assert on inlet's mute state -- it has none -- but macroMuteState (below)
    // confirms it was correctly excluded from the tally.
    EXPECT_EQ(editor.macroMuteState(macroId), GraphEditor::MacroToggleState::AllOn);
}

// ============================================================================
// Tri-state read
// ============================================================================

TEST(MacroBypassMute, BypassStateReportsAllOffThenAllOnThenMixed) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    EXPECT_EQ(editor.macroBypassState(macroId), GraphEditor::MacroToggleState::AllOff);

    editor.setMacroBypassed(macroId, true);
    EXPECT_EQ(editor.macroBypassState(macroId), GraphEditor::MacroToggleState::AllOn);

    moduleAt(engine, a)->setBypassed(false);
    EXPECT_EQ(editor.macroBypassState(macroId), GraphEditor::MacroToggleState::Mixed);
}

TEST(MacroBypassMute, MuteStateIgnoresMuteIneligibleMembersEntirely) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeMuteIneligibleMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    // Every member is mute-ineligible -> reads AllOff, matching macroMuteState's documented
    // "zero queryable members reads AllOff" rule -- NOT Mixed, and not a crash.
    EXPECT_EQ(editor.macroMuteState(macroId), GraphEditor::MacroToggleState::AllOff);
}

// ============================================================================
// Convergence (toggle)
// ============================================================================

TEST(MacroBypassMute, ToggleBypassedConvergesMixedAndAllOffToBypassingEveryMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    // AllOff -> toggle -> AllOn.
    editor.toggleMacroBypassed(macroId);
    EXPECT_TRUE(moduleAt(engine, a)->isBypassed());
    EXPECT_TRUE(moduleAt(engine, b)->isBypassed());

    // AllOn -> toggle -> AllOff (the opposite direction).
    editor.toggleMacroBypassed(macroId);
    EXPECT_FALSE(moduleAt(engine, a)->isBypassed());
    EXPECT_FALSE(moduleAt(engine, b)->isBypassed());

    // Mixed -> toggle -> AllOn (converges toward bypassing, not toward the un-bypassed member).
    moduleAt(engine, a)->setBypassed(true);
    ASSERT_EQ(editor.macroBypassState(macroId), GraphEditor::MacroToggleState::Mixed);
    editor.toggleMacroBypassed(macroId);
    EXPECT_TRUE(moduleAt(engine, a)->isBypassed());
    EXPECT_TRUE(moduleAt(engine, b)->isBypassed());
}

TEST(MacroBypassMute, ToggleMutedConvergesMixedAndAllOffToMutingEveryEligibleMember) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    editor.toggleMacroMuted(macroId);
    EXPECT_TRUE(moduleAt(engine, a)->isMuted());
    EXPECT_TRUE(moduleAt(engine, b)->isMuted());

    editor.toggleMacroMuted(macroId);
    EXPECT_FALSE(moduleAt(engine, a)->isMuted());
    EXPECT_FALSE(moduleAt(engine, b)->isMuted());
}

// ============================================================================
// Refusals
// ============================================================================

TEST(MacroBypassMute, SetMacroBypassedIsANoOpForAnUnknownMacroId) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    editor.setMacroBypassed("not-a-real-macro-id", true);

    EXPECT_FALSE(undo.canUndo());
}

TEST(MacroBypassMute, SetMacroMutedIsANoOpWhenNoMemberIsMuteEligible) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeMuteIneligibleMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());
    // Macro creation itself is already one undo step -- assert against the serial moving, not
    // canUndo() (which is already true), so this only fails if setMacroMuted pushes its own.
    const int serialBefore = undo.getEditSerial();

    editor.setMacroMuted(macroId, true);

    EXPECT_EQ(undo.getEditSerial(), serialBefore); // no undo entry for a mutation that touched nothing
}

TEST(MacroBypassMute, ToggleMutedRefusesWithStatusMessageWhenNoMemberIsMuteEligible) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeMuteIneligibleMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };

    editor.toggleMacroMuted(macroId);

    EXPECT_FALSE(lastStatus.isEmpty());
}

TEST(MacroBypassMute, ToggleBypassedRefusesWithStatusMessageForAnUnknownMacroId) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };

    editor.toggleMacroBypassed("not-a-real-macro-id");

    EXPECT_FALSE(lastStatus.isEmpty());
}

// ============================================================================
// Card layout: the title row must never overlap a badge slot
// ============================================================================

TEST(MacroBypassMute, TitleRowNeverOverlapsEitherToggleBadgeSlot) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    NodeID a, b;
    auto macroId = makeTwoMemberMacro(editor, engine, a, b);
    ASSERT_FALSE(macroId.isEmpty());

    // Long enough that, before getTitleRowBounds() reserved room for both badges, its drawn text
    // would have run under them -- the exact bug the badges' geometry has to not reintroduce.
    editor.renameMacro(macroId, "A Rather Long Descriptive Macro Name");

    auto* card = editor.getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr);

    const auto titleRow = card->getTitleRowBoundsForTest();
    const auto bypassBadge = card->getToggleBadgeBoundsForTest(/*mute=*/false);
    const auto muteBadge = card->getToggleBadgeBoundsForTest(/*mute=*/true);

    // The bypass slot is the leftmost (outer) of the two -- if the title row clears it, it clears
    // the mute slot too, but both are checked so a future re-ordering can't silently break this.
    EXPECT_LE((float)titleRow.getRight(), bypassBadge.getX());
    EXPECT_LE((float)titleRow.getRight(), muteBadge.getX());
    // And the two badge slots themselves never overlap each other.
    EXPECT_LE(bypassBadge.getRight(), muteBadge.getX());
}
