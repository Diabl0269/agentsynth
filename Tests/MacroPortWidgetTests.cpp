// GraphEditor/ModuleComponent-level tests for the compact docked port widget (P8-15
// founder-review fix F2, docs/macros.md §5.3/§5.4): a macro port renders as a small named row
// docked to its macro's hull edge instead of an ordinary 280x111 module card.
//
//   • hull    — macroHullBounds() excludes port members from the union it computes, so docking a
//               port cannot grow the very hull it docks against (the feedback-loop trap)
//   • docking — a port widget's real canvas position derives from the hull + MacroPort::order,
//               inputs down the left edge, outputs down the right, both starting near the top
//   • naming  — the port's name resolves live through GraphEditor (macroPortOwnerFor, what
//               paintMacroPortWidget reads) and appears on the collapsed card's jack layout;
//               renaming updates both with nothing to invalidate
//   • shape   — a Stereo port's widget carries two distinct jacks a side and grows a second row;
//               a MIDI port's widget carries a MIDI jack at the compact header position

#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/MacroInletModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/UI/GraphEditor.h"
#include "../Source/UI/MacroCardComponent.h"
#include "../Source/UI/ModuleComponent.h"
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

ModuleComponent* findComponent(GraphEditor& editor, NodeID id) {
    for (auto* c : editor.getModuleComponents())
        if (c != nullptr && c->getNodeId() == id)
            return c;
    return nullptr;
}

/** Groups two fresh Oscillator/Filter modules into a new collapsed macro (the min-2 rule) and
 *  returns its id. Callers that need a docked widget must expand it first
 *  (editor.setMacroCollapsed(id, false)) — a port's ModuleComponent is hidden, same as any other
 *  member, while its macro is collapsed. */
juce::String makeTwoMemberMacro(GraphEditor& editor, AudioEngine& engine) {
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    return editor.groupSelectionIntoMacro();
}

} // namespace

// ============================================================================
// Hull excludes ports (the feedback-loop trap)
// ============================================================================

TEST(MacroPortWidget, HullBoundsExcludesPortMembersFromTheUnion) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto hullBefore = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hullBefore.isEmpty());

    editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");

    const auto hullAfter = editor.macroHullBounds(macroId);
    EXPECT_EQ(hullAfter, hullBefore) << "a port's own node must not grow the hull it then docks against";
}

// ============================================================================
// Docking: derives from the hull + MacroPort::order
// ============================================================================

TEST(MacroPortWidget, InputWidgetsDockLeftOfTheHullOutputsRightInOrder) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto in0Uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    const auto in1Uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In B");
    const auto outUuid =
        editor.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out A");
    ASSERT_FALSE(in0Uuid.isEmpty());
    ASSERT_FALSE(in1Uuid.isEmpty());
    ASSERT_FALSE(outUuid.isEmpty());

    const auto hull = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hull.isEmpty());

    auto* in0Comp = findComponent(editor, nodeIdForUuid(engine, in0Uuid));
    auto* in1Comp = findComponent(editor, nodeIdForUuid(engine, in1Uuid));
    auto* outComp = findComponent(editor, nodeIdForUuid(engine, outUuid));
    ASSERT_NE(in0Comp, nullptr);
    ASSERT_NE(in1Comp, nullptr);
    ASSERT_NE(outComp, nullptr);

    // Inputs sit entirely LEFT of the hull's own left edge; outputs entirely RIGHT of its right.
    EXPECT_LE(in0Comp->getRight(), hull.getX());
    EXPECT_LE(in1Comp->getRight(), hull.getX());
    EXPECT_GE(outComp->getX(), hull.getRight());

    // order 0 (In A, added first) docks above order 1 (In B) — MacroPort::order, not add order,
    // is what a redundant add/remove/reorder must keep agreeing with.
    EXPECT_LT(in0Comp->getY(), in1Comp->getY());

    // Both sides start near the hull's own top edge.
    EXPECT_GE(in0Comp->getY(), hull.getY());
    EXPECT_GE(outComp->getY(), hull.getY());
}

TEST(MacroPortWidget, DockingSurvivesAnUpdateComponentsPassUnchanged) {
    // Regression guard for the "vestigial resolvePlacement() fights the docking" trap: adding a
    // port must not leave it at some stray free-placed spot that only happens to look right.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);
    const auto dockedBounds = comp->getBounds();

    editor.updateComponents(); // an unrelated reconcile pass — must be idempotent
    EXPECT_EQ(comp->getBounds(), dockedBounds);
}

// ============================================================================
// Naming: resolved live through GraphEditor, renaming updates both surfaces
// ============================================================================

TEST(MacroPortWidget, WidgetResolvesThePortNameThroughTheOwningMacro) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Pitch In");
    ASSERT_FALSE(uuid.isEmpty());
    const auto nodeId = nodeIdForUuid(engine, uuid);

    const auto ownership = editor.macroPortOwnerFor(nodeId);
    ASSERT_NE(ownership.macro, nullptr);
    ASSERT_NE(ownership.port, nullptr);
    EXPECT_EQ(ownership.port->name, "Pitch In");
    EXPECT_TRUE(ownership.port->isInput);
    EXPECT_EQ(ownership.macro->id, macroId);
}

TEST(MacroPortWidget, RenamingAPortUpdatesBothTheWidgetsResolutionAndTheCollapsedCardsJackLayout) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Old Name");
    ASSERT_FALSE(uuid.isEmpty());
    const auto nodeId = nodeIdForUuid(engine, uuid);

    editor.renameMacroPort(macroId, uuid, "New Name");

    // The docked widget (ModuleComponent::paintMacroPortWidget) resolves the name via this call
    // at paint time, with nothing cached — so this alone proves a rename is reflected immediately.
    const auto ownership = editor.macroPortOwnerFor(nodeId);
    ASSERT_NE(ownership.port, nullptr);
    EXPECT_EQ(ownership.port->name, "New Name");

    // MacroCardComponent::paint's collapsed-card jack label reads the SAME macroCardPortLayout()
    // this asserts against — item 4's "the collapsed card too" half of the fix.
    bool foundOnCard = false;
    for (const auto& port : editor.macroCardPortLayout(macroId)) {
        if (port.nodeUuid == uuid) {
            EXPECT_EQ(port.name, "New Name");
            foundOnCard = true;
        }
    }
    EXPECT_TRUE(foundOnCard);
}

// ============================================================================
// Shape: Stereo shows two jacks a side, MIDI shows a MIDI jack, size always matches the estimate
// ============================================================================

TEST(MacroPortWidget, StereoPortWidgetShowsTwoDistinctJacksAndGrowsASecondRow) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1, "Stereo In");
    ASSERT_FALSE(uuid.isEmpty());
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);

    auto* mb = dynamic_cast<ModuleBase*>(comp->getModule());
    ASSERT_NE(mb, nullptr);
    ASSERT_EQ(mb->getVisibleInputPortCount(), 2);

    const auto jack0 = comp->getPortCenter(0, true);
    const auto jack1 = comp->getPortCenter(1, true);
    EXPECT_EQ(jack0.x, jack1.x) << "both legs stay on the same (left/input) edge";
    EXPECT_NE(jack0.y, jack1.y) << "Stereo's two visible jacks must be distinct rows, not overlapping";

    // The widget grows to fit the second row, matching layoutMacroPortWidget's own formula.
    EXPECT_EQ(comp->getWidth(), ModuleComponent::kMacroPortWidgetWidth);
    EXPECT_EQ(comp->getHeight(), ModuleComponent::kMacroPortWidgetHeaderY + ModuleComponent::kMacroPortWidgetRowStep +
                                     ModuleComponent::kMacroPortWidgetBottomPad);
}

TEST(MacroPortWidget, MidiPortWidgetShowsAMidiJackAtTheCompactHeaderPosition) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::Midi, MacroPortShape::Mono, 1, "MIDI In");
    ASSERT_FALSE(uuid.isEmpty());
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);
    ASSERT_TRUE(comp->getModule()->acceptsMidi());

    auto port = comp->getPortForPoint({10, ModuleComponent::kMacroPortWidgetHeaderY});
    ASSERT_TRUE(port.has_value());
    EXPECT_TRUE(port->isMidi);
    EXPECT_TRUE(port->isInput);

    // A single-row widget: no jack-count growth for MIDI (it has no Mono/Stereo/Poly-N shape).
    EXPECT_EQ(comp->getHeight(), ModuleComponent::kMacroPortWidgetHeaderY + ModuleComponent::kMacroPortWidgetBottomPad);
}

TEST(MacroPortWidget, AudioJackHitTestRoundTripsForMonoAndStereo) {
    // paintMacroPortWidget's own comment claims getPortForPoint/getPortCenter never diverge for a
    // macro-port widget ("just compacted") — this is the round-trip check that actually proves it:
    // whatever getPortCenter says a jack's drawn position is, getPortForPoint must recognise a
    // click there as that same jack, or a cable becomes undroppable on the visible dot.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto monoUuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Mono In");
    auto* monoComp = findComponent(editor, nodeIdForUuid(engine, monoUuid));
    ASSERT_NE(monoComp, nullptr);
    {
        auto p = monoComp->getPortCenter(0, true);
        auto port = monoComp->getPortForPoint(p);
        ASSERT_TRUE(port.has_value());
        EXPECT_TRUE(port->isInput);
        EXPECT_FALSE(port->isMidi);
        EXPECT_EQ(port->index, 0);
    }

    const auto stereoUuid =
        editor.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1, "Stereo Out");
    auto* stereoComp = findComponent(editor, nodeIdForUuid(engine, stereoUuid));
    ASSERT_NE(stereoComp, nullptr);
    for (int i = 0; i < 2; ++i) {
        auto p = stereoComp->getPortCenter(i, false);
        auto port = stereoComp->getPortForPoint(p);
        ASSERT_TRUE(port.has_value()) << "jack " << i;
        EXPECT_FALSE(port->isInput);
        EXPECT_EQ(port->index, i);
    }
}

// ============================================================================
// Group drag: a partial (non-whole-macro) selection must not desync a port widget
// ============================================================================

TEST(MacroPortWidget, PartialSelectionDragReDocksThePortWidgetAfterFinalize) {
    // The whole-macro drag path (selectMacro -> beginSelectionDrag/dragSelectionBy/
    // finalizeSelectionDrag) keeps a port widget consistent for free because every member,
    // ports included, moves by the identical delta. A PARTIAL selection has no such guarantee: a
    // marquee can catch a port widget plus some unrelated module without the macro's other
    // (non-port) members, so the hull does not move by the same delta the port just moved by.
    // finalizeSelectionDrag() must re-dock explicitly to cover this gap.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto portUuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(portUuid.isEmpty());
    auto portNodeId = nodeIdForUuid(engine, portUuid);
    auto* portComp = findComponent(editor, portNodeId);
    ASSERT_NE(portComp, nullptr);

    const auto hullBefore = editor.macroHullBounds(macroId);
    ASSERT_FALSE(hullBefore.isEmpty());
    const auto dockedPos = portComp->getPosition();

    // An unrelated third module, entirely outside the macro.
    auto outsider = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 1000, 900);
    auto* outsiderComp = findComponent(editor, outsider);
    ASSERT_NE(outsiderComp, nullptr);

    // Select ONLY the port widget + the outsider — never the macro's own non-port members — then
    // drag as a group, exactly the marquee-then-drag shape the trap describes.
    editor.setSelectedNodes({portNodeId, outsider});
    editor.beginSelectionDrag();
    editor.dragSelectionBy({300, 300}, nullptr);
    EXPECT_NE(portComp->getPosition(), dockedPos) << "sanity: the drag actually moved it mid-gesture";
    editor.finalizeSelectionDrag();

    EXPECT_EQ(portComp->getPosition(), dockedPos) << "finalize must re-dock a partially-selected port widget";
    EXPECT_EQ(editor.macroHullBounds(macroId), hullBefore) << "the macro's own members never moved";
}

// ============================================================================
// All-ports macro: the hull falls back to the macro's own persisted bounds
// ============================================================================

TEST(MacroPortWidget, AllPortsMacroFallsBackToMacroBoundsForTheHull) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto portUuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(portUuid.isEmpty());

    // Delete BOTH ordinary members (MacroDelete.DeletingDownToOneMemberDoesNotDissolve's own
    // pattern) — the macro survives at one member, which is now nothing but the port.
    editor.setSelectedNodes({a});
    editor.deleteSelection();
    editor.setSelectedNodes({b});
    editor.deleteSelection();

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "one remaining member (the port) must not dissolve the macro";
    EXPECT_EQ(macro->members.size(), 1u);

    const auto hull = editor.macroHullBounds(macroId);
    EXPECT_FALSE(hull.isEmpty()) << "an all-ports macro must fall back to macro->bounds, not divide by zero";

    auto* portComp = findComponent(editor, nodeIdForUuid(engine, portUuid));
    ASSERT_NE(portComp, nullptr);
    EXPECT_LE(portComp->getRight(), hull.getX()) << "the port still docks against the fallback hull";
}

// ============================================================================
// Collapsing seeds the card from the non-port members, never the docked port widget
// ============================================================================

TEST(MacroPortWidget, CollapsingSeedsCardBoundsFromNonPortMembersOnly) {
    // applyMacroCollapsed's own groupBounds union must exclude port members the same way
    // macroHullBounds does: an input port widget docks LEFT of the hull, so folding it into "the
    // group" would seed the collapsed card noticeably left of where the real members sit.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 400);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 700, 400);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto portUuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(portUuid.isEmpty());
    auto* portComp = findComponent(editor, nodeIdForUuid(engine, portUuid));
    ASSERT_NE(portComp, nullptr);
    // The docked input widget sits well left of the ordinary members' own left edge.
    EXPECT_LT(portComp->getRight(), findComponent(editor, a)->getX());
    const int portRightBeforeCollapse = portComp->getRight(); // read BEFORE collapse hides members

    editor.setMacroCollapsed(macroId, true);

    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    // The seeded card must land at (or right of) the non-port members' own left edge, not out at
    // the port widget's left edge.
    EXPECT_GE(macro->bounds.getX(), portRightBeforeCollapse);
}

TEST(MacroPortWidget, MonoPortWidgetSizeMatchesEstimateModuleSize) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, false, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "Out A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);

    const auto estimate = GraphEditor::estimateModuleSize("Macro Out");
    EXPECT_EQ(comp->getWidth(), estimate.x);
    EXPECT_EQ(comp->getHeight(), estimate.y);
}

// ============================================================================
// Founder-review fix G4 ("make the routing more elegant and sleek, it's currently too large"):
// the widget got smaller, but geometry self-consistency, hit-test generosity and name legibility
// all have to survive the shrink.
// ============================================================================

TEST(MacroPortWidgetG4, GeometryIsSelfConsistentForMonoStereoPolyAndMidi) {
    // For every shape the widget can take: the widget's own height must derive exactly from its
    // visible jack-row count (layoutMacroPortWidget's formula), and every drawn jack row
    // (getPortCenter) must land strictly inside the widget's own bounds — a shrink that trims
    // padding too far would push the last row's dot into the border or past it.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    struct Case {
        const char* label;
        synth::MacroPortKind kind;
        MacroPortShape shape;
        int voiceCount;
        int expectedRows;
    };
    const Case cases[] = {
        {"Mono", synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, 1},
        {"Stereo", synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1, 2},
        {"Poly-4", synth::MacroPortKind::AudioCV, MacroPortShape::Poly, 4, 1},
        {"Midi", synth::MacroPortKind::Midi, MacroPortShape::Mono, 1, 1},
    };

    for (const auto& c : cases) {
        const auto uuid = editor.addMacroPort(macroId, /*isInput=*/true, c.kind, c.shape, c.voiceCount, c.label);
        ASSERT_FALSE(uuid.isEmpty()) << c.label;
        auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
        ASSERT_NE(comp, nullptr) << c.label;

        const int expectedHeight = ModuleComponent::kMacroPortWidgetHeaderY +
                                   (c.expectedRows - 1) * ModuleComponent::kMacroPortWidgetRowStep +
                                   ModuleComponent::kMacroPortWidgetBottomPad;
        EXPECT_EQ(comp->getWidth(), ModuleComponent::kMacroPortWidgetWidth) << c.label;
        EXPECT_EQ(comp->getHeight(), expectedHeight) << c.label;

        const bool isMidi = c.kind == synth::MacroPortKind::Midi;
        for (int row = 0; row < c.expectedRows; ++row) {
            const auto p = isMidi ? comp->getPortCenter(0, true) : comp->getPortCenter(row, true);
            EXPECT_GE(p.y, 0) << c.label << " row " << row;
            EXPECT_LT(p.y, comp->getHeight()) << c.label << " row " << row << " jack falls outside the widget";
            EXPECT_GE(p.x, 0) << c.label << " row " << row;
            EXPECT_LT(p.x, comp->getWidth()) << c.label << " row " << row;
        }
    }
}

TEST(MacroPortWidgetG4, EstimateModuleSizeMatchesTheRealWidgetForEveryPortTypeName) {
    // The anti-drift test the kPortGutterHeaderHeight comment wishes had existed, for the other
    // geometry pair that can drift apart: GraphEditor's own drag-ghost estimate vs the real,
    // freshly-constructed (Mono/one-row) widget, for all four type names at once.
    for (const juce::String& typeName : {"Macro In", "Macro Out", "Macro MIDI In", "Macro MIDI Out"}) {
        auto processor = synth::AIStateMapper::createModule(typeName);
        ASSERT_NE(processor, nullptr) << typeName;

        AudioEngine engine;
        GraphEditor editor(engine);
        ModuleComponent comp(processor.get(), NodeID(1), editor);

        const auto estimate = GraphEditor::estimateModuleSize(typeName);
        EXPECT_EQ(estimate.x, comp.getWidth()) << typeName;
        EXPECT_EQ(estimate.y, comp.getHeight()) << typeName;
    }
}

TEST(MacroPortWidgetG4, HitTestStaysGenerousAroundTheShrunkJackDot) {
    // The drawn dot shrank to a 7px diameter (fix G4), but getPortForPoint's hit test is a fixed
    // "< 10" distance check, general to every module's jack, and untouched by this fix. Prove a
    // click dead on-centre AND a few pixels off in every direction all still resolve to the same
    // jack — a click that only lands within the drawn dot's own tiny radius would be a regression
    // a "sleek" pass could introduce without meaning to.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);

    const auto centre = comp->getPortCenter(0, true);
    const juce::Point<int> offsets[] = {{0, 0}, {4, 0}, {-4, 0}, {0, 4}, {0, -4}, {3, -3}, {-3, 3}};
    for (const auto& offset : offsets) {
        const auto probe = centre + offset;
        auto port = comp->getPortForPoint(probe);
        ASSERT_TRUE(port.has_value()) << "offset (" << offset.x << ", " << offset.y << ")";
        EXPECT_TRUE(port->isInput);
        EXPECT_EQ(port->index, 0);
    }
}

TEST(MacroPortWidgetG4, RealisticPortNameFitsWithinTheWidgetAtFullUnscaledSize) {
    // A shrink that quietly forces every longer name into drawFittedText's compress-or-ellipsise
    // fallback would technically satisfy "never overflow the bounds" (drawFittedText guarantees
    // that structurally) while still costing the founder the port-name legibility he asked for in
    // the first review pass. This checks the STRONGER property: at the widget's tuned width, a
    // realistic name like "Delay 1 Audio" measures narrower than the available text area at the
    // widget's own (unscaled) 9.5f font, so paintMacroPortWidget draws it at full size, not
    // shrunk.
    const juce::String realisticName = "Delay 1 Audio";
    const juce::Font nameFont(juce::FontOptions(9.5f));
    const float textWidth = juce::GlyphArrangement::getStringWidth(nameFont, realisticName);

    // Mirrors paintMacroPortWidget's own `getLocalBounds().reduced(12, 2)` textArea inset.
    const int availableWidth = ModuleComponent::kMacroPortWidgetWidth - 2 * 12;
    EXPECT_LE(textWidth, (float)availableWidth)
        << "\"" << realisticName << "\" no longer fits the docked widget at full size; widen "
        << "kMacroPortWidgetWidth rather than let it silently shrink";
}

// ============================================================================
// A port node's own right-click context menu (founder-review fix G7: "they cannot be removed" —
// a port node had NO delete affordance at all once its macro was gone, and Configure I/O was
// otherwise the ONE surface for it). Every test drives a REAL synthesised juce::MouseEvent into
// the real ModuleComponent::mouseDown() (memory/test-the-real-mouse-path-for-ui-gestures) and
// intercepts the menu via setShowContextMenuHookForTest() rather than letting a real
// PopupMenu::showMenuAsync() open — that segfaults on a headless (no-display) Linux CI runner
// (Tests/MacroContainerTests.cpp's MacroMemberContextMenu suite hit exactly this; G1 added the
// hook for this reason). No test here ever lets a real AlertWindow/DialogWindow open either — the
// Rename/Configure I/O items are asserted present by NAME, never invoked, since both open one.
// ============================================================================

namespace {

juce::MouseEvent makePortRightClick(juce::Component& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

juce::MouseEvent makePortLeftClick(juce::Component& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

const juce::PopupMenu::Item* findMenuItemByText(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

bool hasConnection(AudioEngine& engine, NodeID srcId, int srcCh, NodeID dstId, int dstCh) {
    for (const auto& c : engine.getGraph().getConnections())
        if (c.source.nodeID == srcId && c.source.channelIndex == srcCh && c.destination.nodeID == dstId &&
            c.destination.channelIndex == dstCh)
            return true;
    return false;
}

juce::Point<int> centreOf(const juce::Component& comp) { return {comp.getWidth() / 2, comp.getHeight() / 2}; }

} // namespace

TEST(MacroPortContextMenu, RightClickOffersRenameConfigureAndDeleteWhileTheMacroIsAlive) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);

    juce::PopupMenu capturedMenu;
    comp->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });

    comp->mouseDown(makePortRightClick(*comp, centreOf(*comp)));

    EXPECT_NE(findMenuItemByText(capturedMenu, "Rename Port..."), nullptr);
    EXPECT_NE(findMenuItemByText(capturedMenu, "Configure I/O..."), nullptr);
    EXPECT_NE(findMenuItemByText(capturedMenu, "Delete Port"), nullptr);
}

TEST(MacroPortContextMenu, RightClickStillWorksAfterItsMacroIsGoneRegressionForTheBugItself) {
    // The exact scenario the founder's second complaint names: a port that survives its macro
    // (before this fix, ungroup left it in place) had no delete path at all — Configure I/O was
    // the ONE surface, and promptConfigureMacroIO()'s very first line is `macros.find(macroId)`,
    // which is nullptr the instant the macro is gone. After G7 no ORPHAN port should exist in
    // practice (ungroup removes them), but the menu's own defensive fallback is still real
    // behaviour worth pinning: a port node with no resolvable macro still offers Delete.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    editor.setMacroCollapsed(macroId, false);
    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, uuid);
    auto* comp = findComponent(editor, portId);
    ASSERT_NE(comp, nullptr);

    // Simulate the port outliving its macro record directly (macros.remove, bypassing
    // spliceOutMacroPort — deliberately a raw metadata removal, to exercise the menu's own
    // defensive branch rather than re-testing ungroup's own splice-out).
    editor.getMacros().remove(macroId);
    ASSERT_EQ(editor.macroPortOwnerFor(portId).macro, nullptr);

    juce::PopupMenu capturedMenu;
    comp->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });
    comp->mouseDown(makePortRightClick(*comp, centreOf(*comp)));

    EXPECT_EQ(findMenuItemByText(capturedMenu, "Rename Port..."), nullptr) << "no macro left to rename a port on";
    EXPECT_EQ(findMenuItemByText(capturedMenu, "Configure I/O..."), nullptr);
    const auto* deleteItem = findMenuItemByText(capturedMenu, "Delete Port");
    ASSERT_NE(deleteItem, nullptr) << "Delete must still be offered even with no macro to resolve";

    ASSERT_TRUE((bool)deleteItem->action);
    deleteItem->action();
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the fallback path still deletes the node";
}

TEST(MacroPortContextMenu, LeftClickOnAPortBodyIsStillANoOp) {
    // The G7 fix relaxes the early return ONLY for the right button — a left click must remain a
    // no-op (no selection, no drag): dockMacroPortWidgets() repositions this widget every layout
    // pass, so a draggable port would snap straight back the moment the user let go.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    editor.setMacroCollapsed(macroId, false);
    const auto uuid =
        editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In A");
    auto* comp = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(comp, nullptr);

    ASSERT_FALSE(editor.isNodeSelected(comp->getNodeId()));
    comp->mouseDown(makePortLeftClick(*comp, centreOf(*comp)));
    EXPECT_FALSE(editor.isNodeSelected(comp->getNodeId())) << "a left click on a port body must not select it";
}

TEST(MacroPortContextMenu, DeleteFromTheMenuSplicesTheCableBackAndRemovesTheNode) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());
    editor.setMacroCollapsed(macroId, false);

    const auto uuid =
        editor.addMacroPort(macroId, /*isInput=*/true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(uuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, uuid);

    // `a` is an Oscillator (a pure source, no audio input jack); the internal destination for an
    // INLET port must actually accept audio, so this wires to `b` (the Filter) instead.
    auto ext = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    editor.connectPorts(ext, 0, portId, 0, /*isMidi=*/false, /*recordUndo=*/false);
    editor.connectPorts(portId, 0, b, 0, /*isMidi=*/false, /*recordUndo=*/false);
    ASSERT_TRUE(hasConnection(engine, ext, 0, portId, 0));
    ASSERT_TRUE(hasConnection(engine, portId, 0, b, 0));

    auto* comp = findComponent(editor, portId);
    ASSERT_NE(comp, nullptr);

    juce::PopupMenu capturedMenu;
    comp->setShowContextMenuHookForTest([&capturedMenu](juce::PopupMenu& m) { capturedMenu = m; });
    comp->mouseDown(makePortRightClick(*comp, centreOf(*comp)));

    const auto* deleteItem = findMenuItemByText(capturedMenu, "Delete Port");
    ASSERT_NE(deleteItem, nullptr);
    ASSERT_TRUE((bool)deleteItem->action);
    deleteItem->action();

    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr) << "the port node itself is gone";
    EXPECT_TRUE(hasConnection(engine, ext, 0, b, 0)) << "the cable is spliced back, not dropped";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr) << "two real modules keep the macro alive";
    EXPECT_TRUE(macro->ports.empty());
}

TEST(MacroPortContextMenu, DeletingThePortNodeDirectlyIsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);

    auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 500, 100);
    editor.setSelectedNodes({a, b});
    auto macroId = editor.groupSelectionIntoMacro();
    ASSERT_FALSE(macroId.isEmpty());

    const auto uuid = editor.addMacroPort(macroId, true, synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1, "In");
    ASSERT_FALSE(uuid.isEmpty());
    const auto portId = nodeIdForUuid(engine, uuid);
    // `a` is an Oscillator (no audio input jack) -- wire the port to `b` (the Filter) instead.
    auto ext = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 900, 100);
    editor.connectPorts(ext, 0, portId, 0, false, false);
    editor.connectPorts(portId, 0, b, 0, false, false);

    editor.deleteMacroPortNode(macroId, uuid);
    EXPECT_EQ(engine.getGraph().getNodeForId(portId), nullptr);
    EXPECT_TRUE(hasConnection(engine, ext, 0, b, 0));

    ASSERT_TRUE(undo.canUndo());
    undo.undo();

    EXPECT_NE(engine.getGraph().getNodeForId(portId), nullptr) << "one undo brings back the port node...";
    EXPECT_TRUE(hasConnection(engine, ext, 0, portId, 0)) << "...and its original cable...";
    EXPECT_TRUE(hasConnection(engine, portId, 0, b, 0));
    EXPECT_FALSE(hasConnection(engine, ext, 0, b, 0))
        << "...replacing the spliced direct cable, not coexisting with it";
    auto* macro = editor.getMacros().find(macroId);
    ASSERT_NE(macro, nullptr);
    EXPECT_EQ(macro->ports.size(), 1u);
}
