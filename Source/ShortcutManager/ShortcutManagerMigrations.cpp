// Concern: the one-shot rewrites of saved key bindings, and loading saved bindings, which runs them.
// A new migration is one function and one call here; ShortcutManager.h does not change.
#include "ShortcutManager.h"

struct ShortcutManager::Migrations {
    /** A migration's flag lands in the settings file, so its rewritten bindings must land there too.
     *  They used to change in memory only: the next launch read the flag as "done" and the
     *  stored (old) keys back, so the Ctrl+R / Ctrl+M defaults were adopted for one run and then
     *  lost for good -- no key, and no shortcut in the tooltips. Writes only what a migration changed. */
    static void persistMigratedBindings(ShortcutManager& manager, juce::PropertiesFile& settings,
                                        const std::map<juce::String, juce::KeyPress>& before) {
        bool changed = false;
        for (const auto& actionId : manager.actionIds) {
            const auto it = manager.bindings.find(actionId);
            const auto old = before.find(actionId);
            if (it == manager.bindings.end() || old == before.end())
                continue;
            if (it->second.getKeyCode() == old->second.getKeyCode() &&
                it->second.getModifiers() == old->second.getModifiers())
                continue;
            settings.setValue("shortcut_" + actionId, ShortcutManager::encodeKeyPress(it->second));
            changed = true;
        }
        if (changed)
            settings.saveIfNeeded();
    }

    /** One-shot: Save Project As and Save Snippet swapped chords (Save As took the standard
     *  Cmd+Shift+S). saveToProperties() persists every action's key, so an install that ever saved
     *  its settings still holds the OLD pair and would keep Save As on Cmd+Opt+S with the snippet
     *  command shadowing the standard chord. Swap only when BOTH still hold the old defaults, so a
     *  user who rebound either one keeps their choice; the flag stops it re-firing if they later
     *  rebind back to the old pair on purpose. */
    static void migrateSaveAsChordSwap(ShortcutManager& manager, juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_saveAsCmdShiftS";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const auto cmdShiftS =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        const auto cmdOptS =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
        if (manager.bindings["saveSnippet"] == cmdShiftS && manager.bindings["saveProjectAs"] == cmdOptS) {
            manager.bindings["saveSnippet"] = cmdOptS;
            manager.bindings["saveProjectAs"] = cmdShiftS;
        }
    }

    /** One-shot, same shape as migrateSaveAsChordSwap above. The bottom-dock toggle used to
     *  be toggleTimelinePanel's own Cmd+T; a saved install still holds "shortcut_toggleTimelinePanel"
     *  = Cmd+T (saveToProperties() persists every action, not only rebound ones), which would
     *  otherwise collide with the new toggleBottomPanel action's own Cmd+T default -- both keyed to
     *  the same chord, resolved arbitrarily by getActionsForKeyPress()'s "first in actionIds order"
     *  rule. Move Cmd+T onto toggleBottomPanel and give toggleTimelinePanel its own new default
     *  (Cmd+1) instead, but ONLY when toggleTimelinePanel still holds the OLD default -- a user who
     *  rebound it keeps their choice, and toggleBottomPanel then just claims Cmd+T fresh (it is
     *  absent from every saved file, so it already has its own default with no rebind needed). */
    static void migrateBottomPanelToggleKeys(ShortcutManager& manager, juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_bottomPanelCmdT";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const auto cmdT = juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0);
        if (manager.bindings["toggleTimelinePanel"] == cmdT) {
            manager.bindings["toggleBottomPanel"] = cmdT;
            manager.bindings["toggleTimelinePanel"] = juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0);
        }
    }

    /** One-shot: the bottom-panel toggle moved from Cmd+T to Cmd+J (Cmd+T is released). A saved install holds
     *  the old default for it (saveToProperties() persists every action), so move it across -- but only when it
     *  still holds exactly Cmd+T and no other action already holds Cmd+J; any other binding is the user's
     *  choice and stays. Runs after migrateBottomPanelToggleKeys(), so an install that old still ends on Cmd+J.
     *  The flag stops it re-firing if they later bind Cmd+T on purpose. */
    static void migrateBottomPanelCmdJ(ShortcutManager& manager, juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_bottomPanelCmdJ";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const auto cmdT = juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0);
        const auto cmdJ = juce::KeyPress('j', juce::ModifierKeys::commandModifier, 0);
        if (manager.bindings["toggleBottomPanel"] == cmdT &&
            manager.getConflictingAction("toggleBottomPanel", cmdJ).isEmpty())
            manager.bindings["toggleBottomPanel"] = cmdJ;
    }

    /** One-shot: the digit chords moved from the per-view actions (Cmd+1 Timeline ... Cmd+4 Canvas, Opt+Cmd+1..4
     *  their swaps) to the positional ones (showDockTab1..3, swapDockTab1..3ToMain), which follow the tab order.
     *  A saved install still holds the old digits on the per-view actions, so each is cleared where it still holds
     *  its old default (a user's own rebind stays) and the positional action owns the chord. */
    static void migrateDockDigitKeys(ShortcutManager& manager, juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_dockDigitKeys";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const char* const views[] = {"toggleTimelinePanel", "toggleMixerPanel", "toggleMidiRemotePanel", "showCanvas"};
        const char* const swaps[] = {"swapTimelineToMain", "swapMixerToMain", "swapMidiRemoteToMain",
                                     "swapCanvasToMain"};
        const auto cmdAlt = juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier;
        for (int i = 0; i < 4; ++i) {
            if (manager.bindings[views[i]] == juce::KeyPress('1' + i, juce::ModifierKeys::commandModifier, 0))
                manager.bindings[views[i]] = juce::KeyPress();
            if (manager.bindings[swaps[i]] == juce::KeyPress('1' + i, cmdAlt, 0))
                manager.bindings[swaps[i]] = juce::KeyPress();
        }
    }

    /** One-shot: Record and Metronome gained default chords (Ctrl+R / Ctrl+M) on every platform, but
     *  saveToProperties() persists every action's key, so an install that ever saved its settings
     *  holds them as UNBOUND and would never see the new defaults. Adopt the default only where the
     *  stored key is still unbound -- a user who bound their own chord keeps it -- and the flag stops
     *  it re-firing if they later unbind on purpose.
     *
     *  Off the Mac, Ctrl is also Cmd, so an old install holds Ctrl+M on Toggle Mod Matrix and Ctrl+R
     *  on Repeat. Those two move to their new chords first (only if still on the old default), and a
     *  transport chord is adopted only when no other General action holds it, so the migration can
     *  never create two actions on one chord. */
    static void migrateTransportCtrlChords(ShortcutManager& manager, juce::PropertiesFile& settings) {
        // "2": v1 set its flag but never saved the bindings it adopted (see persistMigratedBindings), so
        // installs that already ran it hold Record / Metronome unbound -- run the adoption once more.
        constexpr auto flag = "shortcutMigration_transportCtrlChords2";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        const juce::KeyPress ctrlR('r', juce::ModifierKeys::ctrlModifier, 0);
        const juce::KeyPress ctrlM('m', juce::ModifierKeys::ctrlModifier, 0);
        if (manager.defaultsPlatform == DefaultsPlatform::Other) {
            // Off the Mac the old Cmd defaults ARE Ctrl+R / Ctrl+M.
            if (manager.bindings["repeatSelection"] == ctrlR)
                manager.bindings["repeatSelection"] = juce::KeyPress('r', otherPlatformRepeatModifiers(), 0);
            if (manager.bindings["toggleModMatrix"] == ctrlM)
                manager.bindings["toggleModMatrix"] = juce::KeyPress('m', otherPlatformModMatrixModifiers(), 0);
        }
        if (!manager.bindings["transportRecord"].isValid() &&
            manager.getConflictingAction("transportRecord", ctrlR).isEmpty())
            manager.bindings["transportRecord"] = ctrlR;
        if (!manager.bindings["transportToggleMetronome"].isValid() &&
            manager.getConflictingAction("transportToggleMetronome", ctrlM).isEmpty())
            manager.bindings["transportToggleMetronome"] = ctrlM;
    }

    /** One-shot, Mac only: Add Track moved from the physical Ctrl+T to Cmd+T (Ctrl+T now inserts a point in a
     *  focused point editor). A saved install holds the old default (saveToProperties() persists every action),
     *  so move it across -- but only when it still holds exactly Ctrl+T and no other action already holds Cmd+T;
     *  any other binding is the user's choice and stays. Runs after the migrations that release Cmd+T, so an
     *  install that old ends with Cmd+T free first. The flag stops it re-firing if they later bind Ctrl+T on
     *  purpose. */
    static void migrateAddTrackCmdT(ShortcutManager& manager, juce::PropertiesFile& settings) {
        constexpr auto flag = "shortcutMigration_addTrackCmdT";
        if (settings.getBoolValue(flag, false))
            return;
        settings.setValue(flag, true);
        if (manager.defaultsPlatform != DefaultsPlatform::Mac)
            return;
        const juce::KeyPress ctrlT('t', juce::ModifierKeys::ctrlModifier, 0);
        const juce::KeyPress cmdT('t', juce::ModifierKeys::commandModifier, 0);
        if (manager.bindings["openAddTrackMenu"] == ctrlT &&
            manager.getConflictingAction("openAddTrackMenu", cmdT).isEmpty())
            manager.bindings["openAddTrackMenu"] = cmdT;
    }
};

void ShortcutManager::loadFromProperties(juce::ApplicationProperties& props) {
    appProperties = &props;
    auto* settings = props.getUserSettings();
    if (settings == nullptr)
        return;

    for (auto& actionId : actionIds) {
        auto key = "shortcut_" + actionId;
        if (settings->containsKey(key))
            bindings[actionId] = parseKeyPress(settings->getValue(key));
    }
    const auto loaded = bindings;
    Migrations::migrateSaveAsChordSwap(*this, *settings);
    Migrations::migrateBottomPanelToggleKeys(*this, *settings);
    Migrations::migrateBottomPanelCmdJ(*this, *settings);
    Migrations::migrateTransportCtrlChords(*this, *settings);
    Migrations::migrateDockDigitKeys(*this, *settings);
    Migrations::migrateAddTrackCmdT(*this, *settings);
    Migrations::persistMigratedBindings(*this, *settings, loaded);
}

/** Where Repeat and Toggle Mod Matrix live off the Mac, once Record and Metronome own Ctrl+R and
 *  Ctrl+M (which there are also the Cmd chords). Ctrl+Shift+M is Locate Master's, so Mod Matrix
 *  takes Ctrl+Alt. */
juce::ModifierKeys ShortcutManager::otherPlatformRepeatModifiers() noexcept {
    return juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier);
}
juce::ModifierKeys ShortcutManager::otherPlatformModMatrixModifiers() noexcept {
    return juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier);
}
