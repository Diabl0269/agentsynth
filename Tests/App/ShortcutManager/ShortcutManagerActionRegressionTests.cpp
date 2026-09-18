// Concern: per-feature regression coverage for individually-added shortcut actions (minimap,
// macro group/ungroup/collapse, piano-roll scale toggles, snap/quantise, export-patch-only,
// focus-region, timeline focused-track mute/solo/arm).
#include "ShortcutManagerTestFixture.h"

// ---------------------------------------------------------------------------
// Minimap toggle (issue #159)
// ---------------------------------------------------------------------------

// Default binding for the minimap toggle must be Cmd+K, with no extra modifiers.
TEST_F(ShortcutManagerTest, ToggleMinimapDefaultBindingIsCmdK) {
    const auto kp = manager.getBinding("toggleMinimap");
    EXPECT_EQ(kp.getKeyCode(), 'k');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_FALSE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

// AppCommands::getCommandForAction must resolve "toggleMinimap" to the real command ID.
TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesToggleMinimap) {
    EXPECT_EQ(AppCommands::getCommandForAction("toggleMinimap"), AppCommands::toggleMinimap);
}

// "toggleMinimap" must be a registered action id (drives Settings' shortcut list).
TEST_F(ShortcutManagerTest, ActionIds_ContainsToggleMinimap) {
    EXPECT_TRUE(manager.getActionIds().contains("toggleMinimap"));
}

// The action needs a human-readable, non-empty description for the Settings UI.
TEST_F(ShortcutManagerTest, GetActionDescription_ToggleMinimapIsNonEmpty) {
    const auto description = ShortcutManager::getActionDescription("toggleMinimap");
    EXPECT_FALSE(description.isEmpty());
    EXPECT_EQ(description, "Toggle Minimap");
}

// ---------------------------------------------------------------------------
// Macro group/ungroup (P8-12)
// ---------------------------------------------------------------------------

// Default binding for grouping the selection into a macro must be Cmd+G, with no extra modifiers.
TEST_F(ShortcutManagerTest, GroupSelectionDefaultBindingIsCmdG) {
    const auto kp = manager.getBinding("groupSelection");
    EXPECT_EQ(kp.getKeyCode(), 'g');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_FALSE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

// Default binding for ungrouping must be Cmd+Shift+G.
TEST_F(ShortcutManagerTest, UngroupSelectionDefaultBindingIsCmdShiftG) {
    const auto kp = manager.getBinding("ungroupSelection");
    EXPECT_EQ(kp.getKeyCode(), 'g');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_TRUE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

// AppCommands::getCommandForAction must resolve "groupSelection"/"ungroupSelection" to the real
// command IDs.
TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesGroupSelection) {
    EXPECT_EQ(AppCommands::getCommandForAction("groupSelection"), AppCommands::groupSelection);
}

TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesUngroupSelection) {
    EXPECT_EQ(AppCommands::getCommandForAction("ungroupSelection"), AppCommands::ungroupSelection);
}

// "groupSelection"/"ungroupSelection" must be registered action ids (drives Settings' shortcut
// list).
TEST_F(ShortcutManagerTest, ActionIds_ContainsGroupSelection) {
    EXPECT_TRUE(manager.getActionIds().contains("groupSelection"));
}

TEST_F(ShortcutManagerTest, ActionIds_ContainsUngroupSelection) {
    EXPECT_TRUE(manager.getActionIds().contains("ungroupSelection"));
}

// Both actions need a human-readable, non-empty description for the Settings UI.
// P8-14: groupSelection (Cmd+G) dispatches to group-or-toggle, so its label is static and must
// not claim to be only one of the two verbs it now covers.
TEST_F(ShortcutManagerTest, GetActionDescription_GroupSelectionIsNonEmpty) {
    const auto description = ShortcutManager::getActionDescription("groupSelection");
    EXPECT_FALSE(description.isEmpty());
    EXPECT_EQ(description, "Group / Toggle Macro");
}

TEST_F(ShortcutManagerTest, GetActionDescription_UngroupSelectionIsNonEmpty) {
    const auto description = ShortcutManager::getActionDescription("ungroupSelection");
    EXPECT_FALSE(description.isEmpty());
    EXPECT_EQ(description, "Ungroup Macro");
}

// ---------------------------------------------------------------------------
// Macro collapse/expand toggle (P8-12 follow-up — GraphEditor::toggleSelectionMacrosCollapsed)
// ---------------------------------------------------------------------------

// Default binding must stay Cmd+Alt+G — unchanged by the collapse-only -> toggle behaviour
// change; only the verb behind the same action id/keybinding changed.
TEST_F(ShortcutManagerTest, CollapseMacroDefaultBindingIsCmdAltG) {
    const auto kp = manager.getBinding("collapseMacro");
    EXPECT_EQ(kp.getKeyCode(), 'g');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_TRUE(kp.getModifiers().isAltDown());
    EXPECT_FALSE(kp.getModifiers().isShiftDown());
}

TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesCollapseMacro) {
    EXPECT_EQ(AppCommands::getCommandForAction("collapseMacro"), AppCommands::collapseMacro);
}

TEST_F(ShortcutManagerTest, ActionIds_ContainsCollapseMacro) {
    EXPECT_TRUE(manager.getActionIds().contains("collapseMacro"));
}

// The label is now the single static toggle label ("Collapse / Expand Macro") rather than the
// old collapse-only "Collapse Macro" — the same static label is what makes a toggle safe to be
// ONE command rather than a collapse/expand pair (see AppCommands::collapseMacro's comment).
TEST_F(ShortcutManagerTest, GetActionDescription_CollapseMacroIsTheStaticToggleLabel) {
    const auto description = ShortcutManager::getActionDescription("collapseMacro");
    EXPECT_FALSE(description.isEmpty());
    EXPECT_EQ(description, "Collapse / Expand Macro");
}

// ---------------------------------------------------------------------------
// Piano roll: scale panel toggle (Ctrl+S)
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, PianoRollToggleScalePanelDefaultIsRealCtrlS) {
    const auto kp = manager.getBinding("pianoRollToggleScalePanel");
    EXPECT_EQ(kp.getKeyCode(), 's');
    EXPECT_TRUE(kp.getModifiers().isCtrlDown());
    EXPECT_FALSE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
#if JUCE_MAC
    // On macOS Ctrl and Cmd are different physical modifiers, so this must stay Ctrl-only —
    // "savePreset" already owns Cmd+S, and the two must never be the same chord.
    EXPECT_FALSE(kp.getModifiers().isCommandDown());
#endif
    EXPECT_EQ(ShortcutManager::getCategory("pianoRollToggleScalePanel"), ShortcutCategory::PianoRoll);
    EXPECT_EQ(ShortcutManager::getActionDescription("pianoRollToggleScalePanel"), "Toggle Scale Panel");
    EXPECT_TRUE(manager.getActionIds().contains("pianoRollToggleScalePanel"));
}

// The row filter's own key. Option+S, deliberately one modifier away from the scale PANEL's Ctrl+S:
// the two are adjacent verbs on adjacent chips, so the chords should rhyme — but they must not
// collide, and modifier equality is exact, so they cannot.
TEST_F(ShortcutManagerTest, PianoRollScaleFilterDefaultIsOptionSAndDoesNotCollideWithTheScalePanelToggle) {
    const auto filter = manager.getBinding("pianoRollToggleScaleFilter");
    const auto panel = manager.getBinding("pianoRollToggleScalePanel");
    EXPECT_EQ(filter.getKeyCode(), 's');
    EXPECT_TRUE(filter.getModifiers().isAltDown());
    EXPECT_FALSE(filter.getModifiers().isCtrlDown());
    EXPECT_FALSE(filter.getModifiers().isShiftDown());
    EXPECT_NE(filter, panel) << "same letter, different modifier - and that has to stay true";
    EXPECT_EQ(ShortcutManager::getCategory("pianoRollToggleScaleFilter"), ShortcutCategory::PianoRoll);
    EXPECT_EQ(ShortcutManager::getActionDescription("pianoRollToggleScaleFilter"), "Show Only Scale Notes");

    // The conflict detector agrees (it is exact on modifiers, by design — see bindingsCollide).
    EXPECT_NE(manager.getConflictingAction("pianoRollToggleScalePanel", filter), "pianoRollToggleScalePanel");
}

// The letters mean ONE verb each now, across both timeline surfaces: bare J is "snap" and bare Q is
// "quantise". Snap is a SINGLE shared action ("timelineSnapToggle") that the piano roll resolves too
// — deliberately not duplicated into a piano-roll action, since two rebindable "Toggle Snap" rows on
// the same key flipping the same TimelineViewState flag is a Settings list nobody could reason about.
TEST_F(ShortcutManagerTest, SnapIsJAndQuantiseIsQOnBothSurfacesWithoutColliding) {
    const juce::KeyPress bareJ('j', juce::ModifierKeys::noModifiers, 0);
    const juce::KeyPress bareQ('q', juce::ModifierKeys::noModifiers, 0);

    EXPECT_EQ(manager.getBinding("pianoRollQuantise"), bareQ);
    EXPECT_EQ(manager.getBinding("timelineSnapToggle"), bareJ) << "the snap key moved off Q, for both surfaces";
    EXPECT_EQ(ShortcutManager::getCategory("pianoRollQuantise"), ShortcutCategory::PianoRoll);
    EXPECT_EQ(ShortcutManager::getCategory("timelineSnapToggle"), ShortcutCategory::Timeline);

    // Exactly ONE snap action exists.
    EXPECT_FALSE(manager.getActionIds().contains("pianoRollSnapToggle"))
        << "snap must not gain a second, piano-roll-only action";

    // The two letters are clear of each other in both directions, which matters because the roll
    // tests quantise FIRST: had they collided, snap would be unreachable in the piano roll entirely.
    EXPECT_NE(manager.getBinding("timelineSnapToggle"), manager.getBinding("pianoRollQuantise"));
    EXPECT_TRUE(manager.getConflictingAction("pianoRollQuantise", bareJ).isEmpty());
    EXPECT_TRUE(manager.getConflictingAction("timelineSnapToggle", bareQ).isEmpty());

    const auto qMatches = manager.getActionsForKeyPress(bareQ);
    EXPECT_TRUE(qMatches.contains("pianoRollQuantise"));
    EXPECT_FALSE(qMatches.contains("timelineSnapToggle"));
    EXPECT_TRUE(manager.getActionsForKeyPress(bareJ).contains("timelineSnapToggle"));
}

// ---------------------------------------------------------------------------
// Export Patch Only (P8-20 / T133) -- the Cmd+Shift+P shortcut that saves just the patch as a
// legacy .json via GraphEditor::savePreset. The AppCommands entry and its File-menu row shipped with
// P8-5; this section pins the rebindable default binding P8-20 adds -- until then exportPatchOnly
// followed the checkForUpdates "menu-only, no chord" treatment (no action id, no binding).
// ---------------------------------------------------------------------------

// 'p' is free under any modifier set: the bare 'p' is the timeline's loop-selection surface key,
// which carries no modifiers and is matched with exact modifier equality, so it can never steal the
// chord, and no component keyPressed() override hardcodes Cmd+Shift+P. It pairs with Export Audio
// (Cmd+Shift+E) as a Cmd+Shift "export" family.
TEST_F(ShortcutManagerTest, ExportPatchOnlyDefaultBindingIsCmdShiftP) {
    auto kp = manager.getBinding("exportPatchOnly");
    EXPECT_EQ(kp.getKeyCode(), 'p');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_TRUE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
#if JUCE_MAC
    EXPECT_FALSE(kp.getModifiers().isCtrlDown());
#endif
}

// T133's "resolves to the export action": the binding must answer its own command unambiguously.
TEST_F(ShortcutManagerTest, GetActionForKeyPress_ResolvesCmdShiftPToExportPatchOnly) {
    auto kp = manager.getBinding("exportPatchOnly");
    ASSERT_TRUE(kp.isValid());
    auto matches = manager.getActionsForKeyPress(kp);
    EXPECT_EQ(matches.size(), 1) << "the Export Patch Only chord must be unambiguous";
    EXPECT_EQ(matches[0], "exportPatchOnly");
    EXPECT_EQ(manager.getActionForKeyPress(kp), "exportPatchOnly");
}

// The dispatch half: MainComponent::perform() sends this id to promptExportPatchOnly(), so it must
// map to a REAL command id (a surface action would resolve to kNoCommand instead).
TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesExportPatchOnlyToTheCommand) {
    EXPECT_EQ(AppCommands::getCommandForAction("exportPatchOnly"), AppCommands::exportPatchOnly);
}

// "exportPatchOnly" must be a registered action id -- that is what makes it appear in the Settings
// shortcut list and be rebindable at all.
TEST_F(ShortcutManagerTest, ActionIds_ContainsExportPatchOnly) {
    EXPECT_TRUE(manager.getActionIds().contains("exportPatchOnly"));
}

// A human-readable, non-empty description for the Settings row.
TEST_F(ShortcutManagerTest, GetActionDescription_ExportPatchOnlyIsNonEmpty) {
    auto description = ShortcutManager::getActionDescription("exportPatchOnly");
    EXPECT_FALSE(description.isEmpty());
    EXPECT_EQ(description, "Export Patch Only");
}

// T133's "does not collide": within its (General) category nothing else owns Cmd+Shift+P, and the
// rhyming export/save chords stay distinct -- Export Audio shares the Cmd+Shift modifier set on a
// different letter, Save Project As is Cmd+Opt+S, and Save Snippet is a Graph-category action so it
// is out of scope for a General conflict probe.
TEST_F(ShortcutManagerTest, ExportPatchOnlyDoesNotCollideWithNeighbouringChords) {
    auto bounce = manager.getBinding("exportPatchOnly"); // Cmd+Shift+P (General)
    EXPECT_EQ(ShortcutManager::getCategory("exportPatchOnly"), ShortcutCategory::General);

    // No other General action answers to this chord -- that is precisely "does not collide".
    EXPECT_TRUE(manager.getConflictingAction("exportPatchOnly", bounce).isEmpty())
        << "some other General action shares Cmd+Shift+P with Export Patch Only";

    // The nearest rhyming chords -- keep one letter or one modifier apart and distinct.
    EXPECT_NE(bounce, manager.getBinding("exportAudio")) << "Cmd+Shift+P vs Cmd+Shift+E";
    EXPECT_NE(bounce, manager.getBinding("saveProjectAs")) << "Cmd+Shift+P vs Cmd+Opt+S";

    // saveSnippet (Cmd+Shift+S) rhymes hardest by modifier set but is a Graph-category action, so
    // out of scope for a General conflict probe; it coexists by construction.
    EXPECT_EQ(ShortcutManager::getCategory("saveSnippet"), ShortcutCategory::Graph);
    EXPECT_NE(bounce, manager.getBinding("saveSnippet")) << "Cmd+Shift+P vs Cmd+Shift+S";
}

// ---------------------------------------------------------------------------
// T159: the focus-region framework's four new General actions — Tab/Shift+Tab cycling plus the two
// direct-focus shortcuts. See Source/UI/Layout/FocusRegion.h for the registry these dispatch into and
// Tests/UI/Layout/FocusRegionTests.cpp for the registry's own logic tests.
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, FocusNextRegionDefaultBindingIsBareTab) {
    auto kp = manager.getBinding("focusNextRegion");
    EXPECT_EQ(kp.getKeyCode(), juce::KeyPress::tabKey);
    EXPECT_TRUE(kp.getModifiers().isCommandDown() == false && kp.getModifiers().isShiftDown() == false &&
                kp.getModifiers().isAltDown() == false && kp.getModifiers().isCtrlDown() == false)
        << "bare Tab, no modifiers";
}

TEST_F(ShortcutManagerTest, FocusPrevRegionDefaultBindingIsShiftTab) {
    auto kp = manager.getBinding("focusPrevRegion");
    EXPECT_EQ(kp.getKeyCode(), juce::KeyPress::tabKey);
    EXPECT_TRUE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isCommandDown());
}

TEST_F(ShortcutManagerTest, FocusTimelineDefaultBindingIsCmdShiftT) {
    auto kp = manager.getBinding("focusTimeline");
    EXPECT_EQ(kp.getKeyCode(), 't');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_TRUE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

TEST_F(ShortcutManagerTest, FocusLibraryDefaultBindingIsCmdShiftL) {
    auto kp = manager.getBinding("focusLibrary");
    EXPECT_EQ(kp.getKeyCode(), 'l');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_TRUE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesTheFourFocusRegionActions) {
    EXPECT_EQ(AppCommands::getCommandForAction("focusNextRegion"), AppCommands::focusNextRegion);
    EXPECT_EQ(AppCommands::getCommandForAction("focusPrevRegion"), AppCommands::focusPrevRegion);
    EXPECT_EQ(AppCommands::getCommandForAction("focusTimeline"), AppCommands::focusTimeline);
    EXPECT_EQ(AppCommands::getCommandForAction("focusLibrary"), AppCommands::focusLibrary);
}

TEST_F(ShortcutManagerTest, ActionIds_ContainsTheFourFocusRegionActions) {
    EXPECT_TRUE(manager.getActionIds().contains("focusNextRegion"));
    EXPECT_TRUE(manager.getActionIds().contains("focusPrevRegion"));
    EXPECT_TRUE(manager.getActionIds().contains("focusTimeline"));
    EXPECT_TRUE(manager.getActionIds().contains("focusLibrary"));
}

TEST_F(ShortcutManagerTest, GetActionDescription_FocusRegionActionsAreNonEmpty) {
    EXPECT_EQ(ShortcutManager::getActionDescription("focusNextRegion"), "Focus Next Region");
    EXPECT_EQ(ShortcutManager::getActionDescription("focusPrevRegion"), "Focus Previous Region");
    EXPECT_EQ(ShortcutManager::getActionDescription("focusTimeline"), "Focus Timeline");
    EXPECT_EQ(ShortcutManager::getActionDescription("focusLibrary"), "Focus Library");
}

TEST_F(ShortcutManagerTest, FocusRegionActionsAreGeneralCategory) {
    EXPECT_EQ(ShortcutManager::getCategory("focusNextRegion"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("focusPrevRegion"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("focusTimeline"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("focusLibrary"), ShortcutCategory::General);
}

// The collision check the task's own acceptance criteria calls out explicitly: none of the four new
// chords collide with anything already in the (General) table, including each other and the
// deliberately-NOT-reused Cmd+M (toggleModMatrix). Cmd+Shift+M itself was reserved at the time for a
// future Mixer-focus shortcut and is now FRO45's "Locate Master" (Graph category) — see that action's
// own comment in ShortcutManager::resetToDefaults for why it claims the chord that reservation held.
TEST_F(ShortcutManagerTest, FocusRegionActionsDoNotCollideWithAnyExistingGeneralBinding) {
    for (const auto* actionId : {"focusNextRegion", "focusPrevRegion", "focusTimeline", "focusLibrary"}) {
        auto binding = manager.getBinding(actionId);
        ASSERT_TRUE(binding.isValid()) << actionId << " has no default binding";
        EXPECT_TRUE(manager.getConflictingAction(actionId, binding).isEmpty())
            << actionId << " collides with " << manager.getConflictingAction(actionId, binding);
    }

    // Cmd+Shift+M now belongs to locateMaster (FRO45) — Library/Timeline focus must not have
    // wandered onto it, and nothing else may have claimed it either.
    const auto cmdShiftM =
        juce::KeyPress('m', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    EXPECT_NE(manager.getBinding("focusLibrary"), cmdShiftM);
    EXPECT_NE(manager.getBinding("focusTimeline"), cmdShiftM);
    EXPECT_EQ(manager.getBinding("locateMaster"), cmdShiftM);
    for (const auto& actionId : manager.getActionIds())
        if (actionId != "locateMaster")
            EXPECT_NE(manager.getBinding(actionId), cmdShiftM)
                << actionId << " must not claim locateMaster's Cmd+Shift+M";
}

// ---------------------------------------------------------------------------
// T160: focusLibrarySearch — opens the Library search field specifically, distinct from
// focusLibrary's region-root destination. See Tests/UI/Library/ModuleLibraryComponentTests.cpp for the
// keyboard row-navigation this unlocks once focus lands in the field.
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, FocusLibrarySearchDefaultBindingIsCmdF) {
    auto kp = manager.getBinding("focusLibrarySearch");
    EXPECT_EQ(kp.getKeyCode(), 'f');
    EXPECT_TRUE(kp.getModifiers().isCommandDown());
    EXPECT_FALSE(kp.getModifiers().isShiftDown());
    EXPECT_FALSE(kp.getModifiers().isAltDown());
}

TEST_F(ShortcutManagerTest, GetCommandForAction_ResolvesFocusLibrarySearch) {
    EXPECT_EQ(AppCommands::getCommandForAction("focusLibrarySearch"), AppCommands::focusLibrarySearch);
}

TEST_F(ShortcutManagerTest, ActionIds_ContainsFocusLibrarySearch) {
    EXPECT_TRUE(manager.getActionIds().contains("focusLibrarySearch"));
}

TEST_F(ShortcutManagerTest, GetActionDescription_FocusLibrarySearchIsNonEmpty) {
    EXPECT_EQ(ShortcutManager::getActionDescription("focusLibrarySearch"), "Focus Library Search");
}

TEST_F(ShortcutManagerTest, FocusLibrarySearchIsGeneralCategory) {
    EXPECT_EQ(ShortcutManager::getCategory("focusLibrarySearch"), ShortcutCategory::General);
}

TEST_F(ShortcutManagerTest, FocusLibrarySearchDoesNotCollideWithAnyExistingGeneralBinding) {
    auto binding = manager.getBinding("focusLibrarySearch");
    ASSERT_TRUE(binding.isValid());
    EXPECT_TRUE(manager.getConflictingAction("focusLibrarySearch", binding).isEmpty())
        << "focusLibrarySearch collides with " << manager.getConflictingAction("focusLibrarySearch", binding);
}

// ---------------------------------------------------------------------------
// T161: timelineMuteFocusedTrack / timelineSoloFocusedTrack / timelineArmFocusedTrack — bare m/s/r,
// Timeline category, resolved by TimelineTrackHeaderComponent::keyPressed for whichever track header
// row currently holds keyboard focus. See Tests/UI/Timeline/TimelineTrackFocusTests.cpp for the row/panel-level
// behaviour these unlock. Deliberately not AppCommands/getCommandForAction entries — same pure
// "surface action" shape as timelineSnapToggle/timelineToggleLoop above.
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, TimelineMuteFocusedTrackDefaultBindingIsBareM) {
    auto kp = manager.getBinding("timelineMuteFocusedTrack");
    EXPECT_EQ(kp.getKeyCode(), 'm');
    EXPECT_TRUE(kp.getModifiers() == juce::ModifierKeys());
}

TEST_F(ShortcutManagerTest, TimelineSoloFocusedTrackDefaultBindingIsBareS) {
    auto kp = manager.getBinding("timelineSoloFocusedTrack");
    EXPECT_EQ(kp.getKeyCode(), 's');
    EXPECT_TRUE(kp.getModifiers() == juce::ModifierKeys());
}

TEST_F(ShortcutManagerTest, TimelineArmFocusedTrackDefaultBindingIsBareR) {
    auto kp = manager.getBinding("timelineArmFocusedTrack");
    EXPECT_EQ(kp.getKeyCode(), 'r');
    EXPECT_TRUE(kp.getModifiers() == juce::ModifierKeys());
}

TEST_F(ShortcutManagerTest, TimelineFocusedTrackActionsAreRegistered) {
    EXPECT_TRUE(manager.getActionIds().contains("timelineMuteFocusedTrack"));
    EXPECT_TRUE(manager.getActionIds().contains("timelineSoloFocusedTrack"));
    EXPECT_TRUE(manager.getActionIds().contains("timelineArmFocusedTrack"));
}

TEST_F(ShortcutManagerTest, TimelineFocusedTrackActionDescriptionsAreDistinctFromTheMuteTool) {
    EXPECT_EQ(ShortcutManager::getActionDescription("timelineMuteFocusedTrack"), "Mute Focused Track");
    EXPECT_EQ(ShortcutManager::getActionDescription("timelineSoloFocusedTrack"), "Solo Focused Track");
    EXPECT_EQ(ShortcutManager::getActionDescription("timelineArmFocusedTrack"), "Arm Focused Track");
    // The naming-collision risk these names exist to avoid — a Settings search for "mute" must not
    // read the tool-mode row and the track-toggle row as the same feature.
    EXPECT_EQ(ShortcutManager::getActionDescription("timelineToolMute"), "Mute Tool");
}

TEST_F(ShortcutManagerTest, TimelineFocusedTrackActionsAreTimelineCategory) {
    EXPECT_EQ(ShortcutManager::getCategory("timelineMuteFocusedTrack"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("timelineSoloFocusedTrack"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("timelineArmFocusedTrack"), ShortcutCategory::Timeline);
}

TEST_F(ShortcutManagerTest, TimelineFocusedTrackActionsDoNotCollideWithAnyExistingTimelineBinding) {
    for (const char* actionId : {"timelineMuteFocusedTrack", "timelineSoloFocusedTrack", "timelineArmFocusedTrack"}) {
        const auto binding = manager.getBinding(actionId);
        ASSERT_TRUE(binding.isValid());
        EXPECT_TRUE(manager.getConflictingAction(actionId, binding).isEmpty())
            << actionId << " collides with " << manager.getConflictingAction(actionId, binding);
    }
}

TEST_F(ShortcutManagerTest, GetCommandForAction_TimelineFocusedTrackActionsHaveNoCommand) {
    // Pure surface actions: MainComponent never dispatches these through ApplicationCommandManager.
    EXPECT_EQ(AppCommands::getCommandForAction("timelineMuteFocusedTrack"), AppCommands::kNoCommand);
    EXPECT_EQ(AppCommands::getCommandForAction("timelineSoloFocusedTrack"), AppCommands::kNoCommand);
    EXPECT_EQ(AppCommands::getCommandForAction("timelineArmFocusedTrack"), AppCommands::kNoCommand);
}

// ---------------------------------------------------------------------------
// FRO125: transportTogglePlayStop — the togglePlayback alias the MIDI Remote transport family
// (docs/control/midi-remote.md#action-targets) uses for the play/stop toggle. See
// Tests/App/ShortcutManager/ShortcutManagerTransportActionsTests.cpp for the rest of the family
// (transportPlay/Stop/ToggleLoop/Record/ToggleMetronome/ReturnToStart) and their invokeDirectly
// reachability tests. This one lives here because it is purely a regression against the EXISTING
// togglePlayback action, not a new one: the whole point of the alias is that adding it must never
// move togglePlayback's own persisted Space binding (docs/control/shortcuts.md's "moving a default does
// not move a persisted key" note is exactly the failure mode this guards).
// ---------------------------------------------------------------------------

TEST_F(ShortcutManagerTest, TransportTogglePlayStopResolvesToTheSameCommandAsTogglePlayback) {
    EXPECT_EQ(AppCommands::getCommandForAction("transportTogglePlayStop"), AppCommands::togglePlayback);
    EXPECT_EQ(AppCommands::getCommandForAction("transportTogglePlayStop"),
              AppCommands::getCommandForAction("togglePlayback"));
}

TEST_F(ShortcutManagerTest, TransportTogglePlayStopDoesNotShadowTogglePlaybackAsARegisteredAction) {
    // The alias is resolved only by getCommandForAction() above -- it must never become its own
    // rebindable row (a second "Toggle Playback"-shaped row in Settings would let a user rebind
    // the alias away from Space while togglePlayback itself stayed on it, which is exactly the
    // "two ids fight over one command" shape docs/control/shortcuts.md's alias note warns about).
    EXPECT_FALSE(manager.getActionIds().contains("transportTogglePlayStop"));
    EXPECT_TRUE(manager.getActionIds().contains("togglePlayback"));
}

TEST_F(ShortcutManagerTest, TogglePlaybackKeepsItsSpaceBindingAfterTheAliasWasAdded) {
    const auto space = manager.getBinding("togglePlayback");
    EXPECT_EQ(space.getKeyCode(), juce::KeyPress::spaceKey);
    EXPECT_TRUE(space.getModifiers() == juce::ModifierKeys());
}
