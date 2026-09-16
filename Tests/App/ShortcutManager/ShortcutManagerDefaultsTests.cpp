// Concern: default bindings and the action-table integrity tripwires (uniqueness, category
// partitioning/contiguity, surface-vs-command resolution).
#include "ShortcutManagerTestFixture.h"

namespace {

// Every action id a COMPONENT resolves for itself, written out literally rather than derived from
// the manager. That is the whole point: this list is a transcription of what
// PianoRollComponent::keyPressed, TimelinePanelComponent::keyPressed and
// TimelineClipLaneArea::keyPressed actually ask for, so if one of them is missing from
// ShortcutManager::resetToDefaults() the test fails instead of the KEY silently going dead.
//
// The failure mode it guards is specific and invisible: with a manager installed, resolution is
// STRICT — getBinding() answers an unknown id with a default-constructed KeyPress, and every
// matchesAction() treats that as "this action has no key" rather than falling back to the hardcoded
// default. So a typo'd or unregistered id doesn't throw, doesn't warn, and doesn't fall back; the
// key just stops working.
const juce::StringArray& surfaceResolvedActionIds() {
    static const juce::StringArray ids{
        // PianoRollComponent::keyPressed
        "pianoRollQuantise",
        "pianoRollQuantiseLength",
        "pianoRollQuantisePitches",
        "pianoRollToggleScaleFilter",
        "pianoRollNavNextNote",
        "pianoRollNavPrevNote",
        "pianoRollNudgeRight",
        "pianoRollNudgeLeft",
        "pianoRollTransposeOctaveUp",
        "pianoRollTransposeOctaveDown",
        "pianoRollTransposeUp",
        "pianoRollTransposeDown",
        "pianoRollToggleScalePanel",
        // TimelinePanelComponent::keyPressed (also consults timelineSnapToggle, shared with the roll)
        "timelineSnapToggle",
        "timelineToggleLoop",
        "timelineLoopSelection",
        "timelineFollowPlayheadToggle",
        "timelineToolSelect",
        "timelineToolSplit",
        "timelineToolGlue",
        "timelineToolErase",
        "timelineToolMute",
        "timelineToolDraw",
        "timelineJumpToLocator1",
        "timelineJumpToLocator2",
        // TimelineClipLaneArea::keyPressed (its P shares timelineLoopSelection with the panel)
        // TimelineTrackHeaderComponent::keyPressed (T161)
        "timelineMuteFocusedTrack",
        "timelineSoloFocusedTrack",
        "timelineArmFocusedTrack",
    };
    return ids;
}

} // namespace

TEST_F(ShortcutManagerTest, DefaultBindingsCorrect) {
    EXPECT_EQ(manager.getBinding("openSettings").getKeyCode(), ',');
    EXPECT_EQ(manager.getBinding("savePreset").getKeyCode(), 's');
    EXPECT_EQ(manager.getBinding("openProject").getKeyCode(), 'o');
    EXPECT_EQ(manager.getBinding("undo").getKeyCode(), 'z');
    EXPECT_EQ(manager.getBinding("redo").getKeyCode(), 'z');
    EXPECT_TRUE(manager.getBinding("redo").getModifiers().isShiftDown());
}

// "Save Project As" lives on Cmd+Opt+S, deliberately one modifier away from BOTH "savePreset"
// (Cmd+S) and "saveSnippet" (Cmd+Shift+S) — and, in a different category entirely, the piano
// roll's bare Alt+S ("pianoRollToggleScaleFilter"). Pinned explicitly rather than relying only on
// the generic sweep tests, since a collision here would leave one of the two 's' actions
// permanently dead (MainComponent::keyPressed dispatches the FIRST bound action with a command).
TEST_F(ShortcutManagerTest, SaveProjectAsUsesCmdOptSAndDoesNotCollideWithPianoRollAltS) {
    const auto saveAs = manager.getBinding("saveProjectAs");
    EXPECT_EQ(saveAs.getKeyCode(), 's');
    EXPECT_TRUE(saveAs.getModifiers().isCommandDown());
    EXPECT_TRUE(saveAs.getModifiers().isAltDown());
    EXPECT_FALSE(saveAs.getModifiers().isShiftDown());
    EXPECT_TRUE(manager.getConflictingAction("saveProjectAs", saveAs).isEmpty());

    const auto scaleFilter = manager.getBinding("pianoRollToggleScaleFilter");
    EXPECT_EQ(scaleFilter.getKeyCode(), 's');
    EXPECT_TRUE(scaleFilter.getModifiers().isAltDown());
    EXPECT_FALSE(scaleFilter.getModifiers().isCommandDown());
    EXPECT_NE(saveAs, scaleFilter) << "same letter and Alt, but Cmd is the difference - genuinely distinct chords";
}

TEST_F(ShortcutManagerTest, CopyPasteDuplicateUseThePlatformStandardKeys) {
    EXPECT_EQ(manager.getBinding("copySelection").getKeyCode(), 'c');
    EXPECT_TRUE(manager.getBinding("copySelection").getModifiers().isCommandDown());
    EXPECT_FALSE(manager.getBinding("copySelection").getModifiers().isShiftDown());

    EXPECT_EQ(manager.getBinding("pasteSelection").getKeyCode(), 'v');
    EXPECT_TRUE(manager.getBinding("pasteSelection").getModifiers().isCommandDown());

    EXPECT_EQ(manager.getBinding("duplicateSelection").getKeyCode(), 'd');
    EXPECT_TRUE(manager.getBinding("duplicateSelection").getModifiers().isCommandDown());
}

TEST_F(ShortcutManagerTest, EveryDefaultBindingIsUnique) {
    // Adding an action with a binding that is already taken silently shadows one of the two, since
    // getActionForKeyPress returns whichever it reaches first.
    for (const auto& actionId : manager.getActionIds())
        EXPECT_TRUE(manager.getConflictingAction(actionId, manager.getBinding(actionId)).isEmpty())
            << actionId << " collides with " << manager.getConflictingAction(actionId, manager.getBinding(actionId));
}

// Every id needs a binding and a label. The COMMAND half of this invariant is no longer universal:
// surface actions (the timeline's own keys, the whole piano-roll block) are resolved by the
// component that owns the key, not dispatched through the command manager, so they map to
// AppCommands::kNoCommand by design — see the two tests below, which pin exactly which ids are
// allowed to do that.
TEST_F(ShortcutManagerTest, EveryActionIdHasABindingACategoryAndADescription) {
    for (const auto& actionId : manager.getActionIds()) {
        EXPECT_NE(manager.getBinding(actionId).getKeyCode(), 0) << actionId << " has no default binding";
        EXPECT_NE(ShortcutManager::getActionDescription(actionId), actionId)
            << actionId << " has no human-readable description";
        // getCategory() answers General for an id it has never heard of, so "has a category" is only
        // meaningful as "is IN the action table" — which is exactly what getActionIds() is built
        // from, so this asserts the round trip rather than the enum being non-empty.
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutManager::getCategory(actionId)).contains(actionId))
            << actionId << " is not filed under the category it reports";
    }
}

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------

// The four sections must partition the id list exactly: every id in one of them, none in two, and
// nothing left over. The Settings tab draws one section per category and indexes its rows by
// getActionIds() order, so an id missing from every category would simply never be shown.
TEST_F(ShortcutManagerTest, CategoriesPartitionEveryActionId) {
    juce::StringArray seen;
    for (auto category : ShortcutManager::getCategoryOrder())
        for (const auto& actionId : ShortcutManager::getActionIdsInCategory(category)) {
            EXPECT_FALSE(seen.contains(actionId)) << actionId << " appears in two categories";
            seen.add(actionId);
        }

    EXPECT_EQ(seen.size(), manager.getActionIds().size());
    for (const auto& actionId : manager.getActionIds())
        EXPECT_TRUE(seen.contains(actionId)) << actionId << " is in no category";
}

// Each category's ids must be a CONTIGUOUS run of getActionIds(): ShortcutsSettingsTab emits one
// header per category and lays the rows out in getActionIds() order, so an id filed out of place
// would split its section into two headers with the same name.
TEST_F(ShortcutManagerTest, CategoriesAreContiguousInActionIdOrder) {
    std::vector<ShortcutCategory> runs;
    for (const auto& actionId : manager.getActionIds()) {
        const auto category = ShortcutManager::getCategory(actionId);
        if (runs.empty() || runs.back() != category)
            runs.push_back(category);
    }
    EXPECT_EQ(runs.size(), ShortcutManager::getCategoryOrder().size())
        << "a category's ids are split into more than one run in getActionIds() order";
    EXPECT_EQ(runs, ShortcutManager::getCategoryOrder()) << "sections are not in getCategoryOrder() order";
}

TEST_F(ShortcutManagerTest, CategoryAssignmentsAreWhatTheSectionsPromise) {
    EXPECT_EQ(ShortcutManager::getCategory("undo"), ShortcutCategory::General);
    // Routed per focused surface, so General rather than Graph — one key, whichever editor has focus.
    EXPECT_EQ(ShortcutManager::getCategory("cutSelection"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("selectAllModules"), ShortcutCategory::General);
    EXPECT_EQ(ShortcutManager::getCategory("zoomInHorizontal"), ShortcutCategory::General);
    // The two verbs that mean nothing off the canvas.
    EXPECT_EQ(ShortcutManager::getCategory("autoArrange"), ShortcutCategory::Graph);
    EXPECT_EQ(ShortcutManager::getCategory("saveSnippet"), ShortcutCategory::Graph);
    EXPECT_EQ(ShortcutManager::getCategory("timelineToolSplit"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("snapSetEighth"), ShortcutCategory::Timeline);
    EXPECT_EQ(ShortcutManager::getCategory("pianoRollNudgeLeft"), ShortcutCategory::PianoRoll);
    // An id this build has never heard of falls back to General — the widest conflict scope, so an
    // unknown id can never quietly duplicate a real app-wide binding.
    EXPECT_EQ(ShortcutManager::getCategory("nonsenseActionId"), ShortcutCategory::General);
}

// ---------------------------------------------------------------------------
// Command vs surface actions
// ---------------------------------------------------------------------------

// THE ordering tripwire (see surfaceResolvedActionIds): every id a component resolves through the
// installed manager must exist in the defaults table. Installing the manager makes resolution
// strict, so an id that is missing here is a key that does nothing at all, silently.
TEST_F(ShortcutManagerTest, EverySurfaceResolvedIdExistsInTheDefaultsTable) {
    for (const auto& actionId : surfaceResolvedActionIds()) {
        EXPECT_TRUE(manager.getActionIds().contains(actionId))
            << actionId
            << " is consulted by a component but is not a registered action - with a ShortcutManager "
               "installed that key is INERT";
        EXPECT_TRUE(manager.getBinding(actionId).isValid()) << actionId << " has no default binding";
    }
}

// The complement: a surface action must NOT resolve to a command, or MainComponent::keyPressed would
// try to dispatch one and the component's own handling would be bypassed.
TEST_F(ShortcutManagerTest, SurfaceActionsMapToNoCommand) {
    for (const auto& actionId : surfaceResolvedActionIds())
        EXPECT_EQ(AppCommands::getCommandForAction(actionId), AppCommands::kNoCommand)
            << actionId << " is resolved by a component but also claims a command id";

    // And the grid commands, which live in the same Timeline category, DO have one — the category is
    // about conflict scope and Settings grouping, never about how an action is dispatched.
    EXPECT_EQ(AppCommands::getCommandForAction("snapSetQuarter"), AppCommands::snapSetQuarter);
    EXPECT_EQ(AppCommands::getCommandForAction("snapCycleNext"), AppCommands::snapCycleNext);
    EXPECT_EQ(AppCommands::getCommandForAction("zoomInVertical"), AppCommands::zoomInVertical);
}

// Every id that is NOT surface-resolved must have a command, for the original reason: a rebindable
// key with no command behind it fires and nothing happens.
TEST_F(ShortcutManagerTest, EveryNonSurfaceActionHasACommand) {
    for (const auto& actionId : manager.getActionIds()) {
        if (surfaceResolvedActionIds().contains(actionId))
            continue;
        EXPECT_NE(AppCommands::getCommandForAction(actionId), AppCommands::kNoCommand)
            << actionId << " maps to no command";
    }
}

// ---------------------------------------------------------------------------
// Category-scoped conflicts
// ---------------------------------------------------------------------------

// The whole reason the scope narrowed: the bare-key DAW conventions (Q/L/P, the tool digits, the
// piano roll's arrows) live on surfaces that can never hold keyboard focus at once, so sharing a key
