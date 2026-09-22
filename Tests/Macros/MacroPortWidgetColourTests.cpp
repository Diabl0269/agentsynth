// A per-port jack-colour change must repaint BOTH surfaces (the live collapsed
// MacroCardComponent AND the port's own docked ModuleComponent) in real time, plus the
// live-preview (view-layer-only) behaviour of the ColourPickerPopup-driven picker. Split out of
// MacroPortWidgetTests.cpp to stay under the 1,000-line cap; shares its helpers via
// MacroPortWidgetTestHelpers.h.
#include "MacroPortWidgetTestHelpers.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include <gtest/gtest.h>

// ============================================================================
// A port-colour change repaints BOTH surfaces in real time
// ============================================================================
// Per-port colour already makes the docked widget (ModuleComponent::paintMacroPortWidget) and the collapsed card
// (MacroCardComponent::paint) BOTH read a port's user colour, but a port-colour change is only a
// macro-set mutation, so neither surface has a listener to notice it: the reported symptom was that
// jack "only shows the new colour after a collapse/expand", which re-runs the layout and forces a
// fresh paint. changeMacroPortColour now forces a repaint of BOTH surfaces itself, via
// repaintMacroPortColourTargets() - the seam these tests pin. As StatusBarTests' gated-repaint
// comment notes, a headless test cannot intercept Component::repaint() (a no-op with no window), so
// the testable proof is that the fix reaches the two correct paint surfaces: the live collapsed card
// and the port's own docked ModuleComponent.

TEST(MacroPortWidget, ExpandsRecolourReachesBothTheCardAndTheDockedWidget) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());

    const juce::Colour userColour(0xff123456);
    editor.changeMacroPortColour(macroId, uuid, userColour);

    // The two surfaces the change must land on: the real collapsed card and the port's own docked
    // widget (found regardless of collapse state - the port node persists, just hidden when the
    // macro is folded, which is the harmless hidden-widget repaint).
    auto targets = editor.repaintMacroPortColourTargets(macroId, uuid);
    ASSERT_NE(targets.card, nullptr);
    ASSERT_NE(targets.widget, nullptr);

    // They are the SAME components the rest of the codebase reaches for this macro/port - the fix
    // hit the real paint surfaces, not a fabricated target.
    EXPECT_EQ(targets.card, editor.getMacroController().getMacroCardForTest(macroId))
        << "the card must be the macro's own live card";
    EXPECT_EQ(targets.widget, findComponent(editor, nodeIdForUuid(engine, uuid)))
        << "the widget must be this port's own docked ModuleComponent";

    // And the data paint reads is the new colour, so the forced repaint actually shows it.
    const GraphEditor::MacroPortOwner ownership =
        editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid));
    ASSERT_NE(ownership.port, nullptr);
    EXPECT_EQ(*ownership.port->colour, userColour);
    EXPECT_EQ(ModuleComponent::resolveMacroPortJackColour(ownership.port, juce::Colour(0xff00cc33)), userColour);
}

TEST(MacroPortWidget, EveryPortKindReachesItsDockedWidget) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    // A MIDI input and a stereo Audio output must each resolve to their own widget plus the shared
    // card, never falling through to a null on the MIDI branch or the wide-shape branch.
    const auto midiUuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::Midi,
                                                                   MacroPortShape::Mono, 1, "MIDI In");
    const auto stereoUuid = editor.getMacroController().addMacroPort(macroId, false, synth::MacroPortKind::AudioCV,
                                                                     MacroPortShape::Stereo, 2, "Out A");
    ASSERT_FALSE(midiUuid.isEmpty());
    ASSERT_FALSE(stereoUuid.isEmpty());

    // One check reused for both ports so a wide-shape and a MIDI port are proved identically,
    // without a tuple that would drag in <utility>.
    auto check = [&](const juce::String& portUuid, juce::Colour colour) {
        editor.changeMacroPortColour(macroId, portUuid, colour);

        auto targets = editor.repaintMacroPortColourTargets(macroId, portUuid);
        EXPECT_NE(targets.card, nullptr);
        EXPECT_NE(targets.widget, nullptr);
        EXPECT_EQ(targets.card, editor.getMacroController().getMacroCardForTest(macroId));
        EXPECT_EQ(targets.widget, findComponent(editor, nodeIdForUuid(engine, portUuid)));
        // The paint data this forced repaint reads is the port's own colour, not its neighbour's.
        EXPECT_EQ(editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, portUuid)).port->colour, colour);
    };
    check(midiUuid, juce::Colour(0xff112233));
    check(stereoUuid, juce::Colour(0xff445566));
}

TEST(MacroPortWidget, CollapsedRecolourStillTargetsTheCardAndTheHiddenWidget) {
    // The collapse/expand symptom: even before expanding, the card must be the surface that
    // shows the colour, and the (currently hidden) docked widget is still found so that when the user
    // expands, its first paint already reads the new colour without a manual fold/unfold dance.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    // NOTE: left COLLAPSED on purpose.

    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    editor.changeMacroPortColour(macroId, uuid, juce::Colour(0xff123456));

    auto targets = editor.repaintMacroPortColourTargets(macroId, uuid);
    EXPECT_NE(targets.card, nullptr) << "the collapsed card is the one surface that must repaint";
    EXPECT_EQ(targets.card, editor.getMacroController().getMacroCardForTest(macroId));
    // The port node persists while folded, so its widget is still found (and hidden) rather than null.
    EXPECT_EQ(targets.widget, findComponent(editor, nodeIdForUuid(engine, uuid)));
}

TEST(MacroPortWidget, MissingMacroIdReachesNoSurfacesAndDoesNotCrash) {
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);

    // A non-resolving macroId: no card, no widget, no crash - the guards short-circuit cleanly.
    auto targets = editor.repaintMacroPortColourTargets(juce::Uuid().toDashedString(), juce::Uuid().toDashedString());
    EXPECT_EQ(targets.card, nullptr);
    EXPECT_EQ(targets.widget, nullptr);

    // And the colour change itself is a guarded no-op for a missing macro.
    EXPECT_NO_THROW(editor.changeMacroPortColour(juce::Uuid().toDashedString(), juce::Uuid().toDashedString(),
                                                 juce::Colour(0xff123456)));
}

TEST(MacroPortWidget, ChangeMacroPortColourIsOneUndoStep) {
    // The recolor rides on the same single recorded undo step as the port-colour data path; one recolor is one
    // undo entry, and after undo the same live surfaces still resolve for a subsequent recolor.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());

    editor.changeMacroPortColour(macroId, uuid, juce::Colour(0xff123456));
    ASSERT_TRUE(editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port->colour.has_value());

    // The recolor recorded exactly one undo step (the colour only mutates `macros`, so a
    // MacroSnapshotAction, like every other port-metadatum edit).
    ASSERT_TRUE(undo.canUndo());
    undo.undo();
    EXPECT_FALSE(editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port->colour.has_value())
        << "undo cleared the user colour";

    // And the repaint targets still resolve to the same live surfaces after the round trip.
    editor.changeMacroPortColour(macroId, uuid, juce::Colour(0xffabcdef));
    auto targets = editor.repaintMacroPortColourTargets(macroId, uuid);
    EXPECT_EQ(targets.widget, findComponent(editor, nodeIdForUuid(engine, uuid)));
}

// --------------------------------------------------------------------------------------------
// A macro port's configured colour updates in REAL TIME on both surfaces as
// the Configure I/O picker's selector moves — the same live-preview behaviour the timeline track
// colour has — WITHOUT a per-pixel undo step. The preview is a view-layer-only override (a
// transient on the docked ModuleComponent and per-port on the collapsed MacroCardComponent); the
// stored `synth::MacroPort::colour` is written exactly once, on pick COMMIT (onClose).
//
// The seam, exactly as the commit repaint: no public repaint-count API in JUCE, so a test
// asserts the TARGETS and the RESOLVED COLOUR (via resolve/effective seams) rather than repaint()
// having painted. The three guarantees asserted below:
//   (1) previewMacroPortColour arms BOTH surfaces and repaints them, but writes NO stored colour,
//       pushes NO undo step, and dirties NO data;
//   (2) the docked ModuleComponent's jack paint uses the preview while armed, and the collapsed
//       card does per-port;
//   (3) the pick COMMIT writes the stored colour once, clears the preview, and leaves the jack
//       showing the now-stored colour (preview-clear == store, so the colour never glitches).
// --------------------------------------------------------------------------------------------

TEST(MacroPortWidget, PreviewArmsBothSurfacesButWritesNoStoredColourAndNoUndo) {
    // Expanded so a docked ModuleComponent fronts the port; a macro is collapsed-then-expanded, so
    // the card AND the docked widget both exist to receive the live preview.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);

    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());

    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    auto* widget = findComponent(editor, nodeIdForUuid(engine, uuid));
    auto* port = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_NE(card, nullptr);
    ASSERT_NE(widget, nullptr);
    ASSERT_NE(port, nullptr);
    const bool hadUndo = undo.canUndo();
    ASSERT_FALSE(port->colour.has_value()); // uncoloured port — the preview is what should win

    // Arm the live preview, the way the picker's onPreview would on every selector tick.
    const juce::Colour preview(0xffaa5500);
    editor.previewMacroPortColour(macroId, uuid, preview);

    // (1) Both surfaces now resolve to the preview colour — and no data was written, no undo pushed.
    EXPECT_TRUE(card->hasPortColourPreviewForTest(uuid));
    EXPECT_EQ(card->resolvePortJackColour(uuid, port->colour, juce::Colour(0xff000000)), preview);
    EXPECT_TRUE(widget->hasPortColourPreviewForTest());
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff000000)), preview);
    EXPECT_FALSE(port->colour.has_value());
    EXPECT_EQ(undo.canUndo(), hadUndo) << "a live preview must push no undo entry";

    // A second tick overwrites the armed colour (idempotent), still writing no data.
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::cyan);
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff000000)), juce::Colours::cyan);
    EXPECT_FALSE(port->colour.has_value());
    EXPECT_EQ(undo.canUndo(), hadUndo);
}

TEST(MacroPortWidget, DockedWidgetResolvesPreviewThenStoredThenKindTint) {
    // The docked ModuleComponent's jack paint source, exactly ModuleComponent::effectiveMacroPortJackColour:
    // a live preview wins, else the committed user colour, else the kind tint — so a committed colour
    // takes over the moment its preview is cleared, and an uncoloured port shows the tint.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* widget = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(widget, nullptr);

    auto* port = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_FALSE(port->colour.has_value());
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
    const juce::Colour kindTint(0xff00cc33);
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, kindTint), kindTint)
        << "no preview, no stored: the tint is the paint source";
    // A live preview overrides even the kind tint.
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::orange);
    EXPECT_TRUE(widget->hasPortColourPreviewForTest());
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff00cc33)), juce::Colours::orange);

    // Committing the pick stores the colour AND clears the preview in one step, so the jack now
    // resolves to the stored colour — no glitch, because preview and store converged on one value.
    editor.changeMacroPortColour(macroId, uuid, juce::Colours::cyan);
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
    auto* committedPort = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_TRUE(committedPort->colour.has_value());
    EXPECT_EQ(widget->effectiveMacroPortJackColour(committedPort, juce::Colour(0xff00cc33)), juce::Colours::cyan);
}

TEST(MacroPortWidget, CollapsedCardPreviewIsScopedToOnePort) {
    // The collapsed card previews ONE port at a time: a clear addressed at a different node must not
    // wipe the live preview (a stale picker for another port closing mid-drag is a no-op), and a
    // clear for the port's own node disarms it back to stored-or-tint.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    // Left collapsed on purpose — the card is the live collapsed surface here.
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    auto* port = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_NE(card, nullptr);
    ASSERT_NE(port, nullptr);

    editor.previewMacroPortColour(macroId, uuid, juce::Colours::orange);
    EXPECT_TRUE(card->hasPortColourPreviewForTest(uuid));
    EXPECT_EQ(card->resolvePortJackColour(uuid, port->colour, juce::Colour(0xff000000)), juce::Colours::orange);

    // A clear addressed at a node the card isn't previewing keeps the armed one.
    card->clearPortColourPreview(juce::Uuid().toDashedString());
    EXPECT_TRUE(card->hasPortColourPreviewForTest(uuid));

    // A clear for the port's own node disarms it back to stored-or-tint.
    card->clearPortColourPreview(uuid);
    EXPECT_FALSE(card->hasPortColourPreviewForTest(uuid));
    EXPECT_EQ(card->resolvePortJackColour(uuid, port->colour, juce::Colour(0xff00cc33)), juce::Colour(0xff00cc33));
}

TEST(MacroPortWidget, PreviewThenCommitIsOneUndoStepAndShowsStoredColour) {
    // The end-to-end guarantee: many preview ticks push zero undo steps and write no
    // data; the single commit pushes exactly one undo step and stores the colour, leaving the jack
    // showing it (preview-clear == store, so the colour never glitches).
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* widget = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(widget, nullptr);

    auto* port = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    const bool hadUndo = undo.canUndo();

    // A drag: several live preview ticks — no data written, no undo step.
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::red);
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::green);
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::blue);
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff000000)), juce::Colours::blue);
    EXPECT_FALSE(port->colour.has_value());
    EXPECT_EQ(undo.canUndo(), hadUndo) << "a drag of preview ticks must push no undo entry";

    // The commit: exactly one undo step, the colour stored, and the identical preview cleared.
    editor.changeMacroPortColour(macroId, uuid, juce::Colours::blue);
    auto* committedPort = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_TRUE(committedPort->colour.has_value());
    EXPECT_EQ(committedPort->colour.value(), juce::Colours::blue);
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
    EXPECT_EQ(widget->effectiveMacroPortJackColour(committedPort, juce::Colour(0xff000000)), juce::Colours::blue);
    EXPECT_TRUE(undo.canUndo()) << "the single commit must push exactly one undo step";
}

TEST(MacroPortWidget, ColourPickerFiresOnPreviewThenCommitsOnce) {
    // The picker's own wiring, as GraphEditor::promptConfigureMacroIO wires it: onPreview (a live
    // tick) previews without committing, and onCommit (on close) commits exactly once — the split
    // that keeps a slider drag undo-free while the jack still tracks the pick in real time.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* widget = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(widget, nullptr);

    auto* port = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    const bool hadUndo = undo.canUndo();

    synth::ui::MacroPortConfigDialog dialog(
        juce::String("Macro"),
        {synth::ui::MacroPortConfigDialog::PortRow{uuid, true, "In A", synth::MacroPortKind::AudioCV,
                                                   MacroPortShape::Mono, 1, port->colour}});
    int previews = 0;
    dialog.onPreviewPortColour = [&](const juce::String& u, juce::Colour c) {
        ++previews;
        editor.previewMacroPortColour(macroId, u, c);
    };
    dialog.onChangePortColour = [&](const juce::String& u, std::optional<juce::Colour> c) {
        editor.changeMacroPortColour(macroId, u, c);
    };

    auto picker = dialog.createRowColourPickerForTest(0);
    ASSERT_NE(picker, nullptr);

    // A slider drag drives the popup with several live preview ticks — each previews, none commits.
    picker->setCurrentColourForTest(juce::Colour(0xff111111));
    picker->setCurrentColourForTest(juce::Colour(0xff222222));
    EXPECT_EQ(previews, 2) << "each tick must fire onPreview";
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff000000)), juce::Colour(0xff222222));
    EXPECT_FALSE(port->colour.has_value());
    EXPECT_EQ(undo.canUndo(), hadUndo);

    // Closing the popup fires onCommit exactly once: one undo step, the colour stored, preview cleared.
    picker->commitForTest();
    auto* committedPort = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_TRUE(committedPort->colour.has_value());
    EXPECT_EQ(committedPort->colour.value(), juce::Colour(0xff222222));
    ASSERT_TRUE(undo.canUndo());
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
}

TEST(MacroPortWidget, AbandonedPickerTearsDownTheArmedPreview) {
    // The teardown backstop: a picker the user abandoned without committing leaves an armed preview on
    // both surfaces; cancelArmedMacroPortColourPreview() (wired into the dialog's onRequestClose, which
    // fires on a Close/Escape that its onCommit never reaches) disarms both -- with no node arg, by the
    // node the session cached -- so the jack falls back to stored-or-tint instead of freezing on a
    // half-selected colour.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* widget = findComponent(editor, nodeIdForUuid(engine, uuid));
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    auto* port = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_NE(widget, nullptr);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(port, nullptr);

    // A picker drag armed both surfaces.
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::purple);
    EXPECT_TRUE(widget->hasPortColourPreviewForTest());
    EXPECT_TRUE(card->hasPortColourPreviewForTest(uuid));
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff000000)), juce::Colours::purple);
    const int serialBefore = undo.getEditSerial();

    // The user closes without committing: the backstop tears the session down on BOTH surfaces.
    editor.cancelArmedMacroPortColourPreview();
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
    EXPECT_FALSE(card->hasPortColourPreviewForTest(uuid));
    EXPECT_EQ(widget->effectiveMacroPortJackColour(port, juce::Colour(0xff000000)),
              juce::Colour(0xff000000)); // purple was never stored -- back to the kind tint (the one passed here)
    EXPECT_EQ(undo.getEditSerial(), serialBefore) << "a torn-down preview must leave no undo entry";
}

TEST(MacroPortWidget, CancelWithNoArmedPreviewIsNoOp) {
    // cancelArmedMacroPortColourPreview() with nothing armed is a real no-op: it repaints nothing and
    // leaves both surfaces untouched, so an unrelated (or a never-previewed picker's) Close costs
    // zero repaints.
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    auto* widget = findComponent(editor, nodeIdForUuid(engine, uuid));
    ASSERT_NE(widget, nullptr);

    ASSERT_FALSE(widget->hasPortColourPreviewForTest());
    editor.cancelArmedMacroPortColourPreview(); // no session armed
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
    // And the public commit boundary is still a one-repaint, one-undo-step operation after a no-op
    // cancel: repaintMacroPortColourTargets forces a repaint, clearMacroPortColourPreview is the no-op.
    editor.changeMacroPortColour(macroId, uuid, juce::Colours::green);
    auto* committedPort = editor.getMacroController().macroPortOwnerFor(nodeIdForUuid(engine, uuid)).port;
    ASSERT_TRUE(committedPort->colour.has_value());
    EXPECT_FALSE(widget->hasPortColourPreviewForTest());
    EXPECT_TRUE(undo.canUndo());
}

TEST(MacroPortWidget, ArmedPreviewSurvivesTheSurfaceItArmedBeingDestroyed) {
    // The session resolves its two paint surfaces ONCE per picker (re-resolving on every tick was the
    // per-drag cost this replaced), so it must hold them WEAKLY. The modal does NOT freeze the graph:
    // the dialog's own callbacks run via callAsync and land while the picker's CallOutBox is still open
    // (see MacroPortConfigDialogInternal.h's buildColourPicker comment), and a removed or reshaped port
    // destroys the very ModuleComponent the session resolved. Held raw, the teardown backstop below
    // would touch freed memory; held through SafePointer the dead surface reads back null and is simply
    // skipped, exactly as re-resolving every tick would have skipped it. ASan (the CI test job) is what
    // turns a regression here into a red build rather than a silent one.
    AudioEngine engine;
    GraphEditor editor(engine);
    editor.setSize(1600, 1200);
    auto macroId = makeTwoMemberMacro(editor, engine);
    ASSERT_FALSE(macroId.isEmpty());
    editor.getMacroController().setMacroCollapsed(macroId, false);
    const auto uuid = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                               MacroPortShape::Mono, 1, "In A");
    ASSERT_FALSE(uuid.isEmpty());
    ASSERT_NE(findComponent(editor, nodeIdForUuid(engine, uuid)), nullptr);
    ASSERT_NE(editor.getMacroController().getMacroCardForTest(macroId), nullptr);

    // A picker drag arms both surfaces and caches them for the rest of this session.
    editor.previewMacroPortColour(macroId, uuid, juce::Colours::purple);
    ASSERT_TRUE(findComponent(editor, nodeIdForUuid(engine, uuid))->hasPortColourPreviewForTest());
    ASSERT_TRUE(editor.getMacroController().getMacroCardForTest(macroId)->hasPortColourPreviewForTest(uuid));

    // The port goes away underneath the still-open picker: its docked widget is destroyed.
    editor.getMacroController().removeMacroPort(macroId, uuid);
    editor.updateComponents();
    ASSERT_EQ(findComponent(editor, nodeIdForUuid(engine, uuid)), nullptr);

    // The abandoned-picker backstop still runs, reaching only the surface that is still alive.
    editor.cancelArmedMacroPortColourPreview();
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    ASSERT_NE(card, nullptr) << "the macro still has both of its module members, so its card outlives the port";
    EXPECT_FALSE(card->hasPortColourPreviewForTest(uuid));

    // And the next arm re-resolves from scratch rather than reusing anything the dead session cached.
    const auto second = editor.getMacroController().addMacroPort(macroId, true, synth::MacroPortKind::AudioCV,
                                                                 MacroPortShape::Mono, 1, "In B");
    ASSERT_FALSE(second.isEmpty());
    auto* secondWidget = findComponent(editor, nodeIdForUuid(engine, second));
    ASSERT_NE(secondWidget, nullptr);
    editor.previewMacroPortColour(macroId, second, juce::Colours::orange);
    EXPECT_TRUE(secondWidget->hasPortColourPreviewForTest());
    EXPECT_TRUE(editor.getMacroController().getMacroCardForTest(macroId)->hasPortColourPreviewForTest(second));
}
