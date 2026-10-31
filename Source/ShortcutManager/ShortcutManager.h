#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

/** Which part of the app an action belongs to: its Settings section and its conflict-check scope. */
enum class ShortcutCategory { General, Graph, Timeline, PianoRoll, DrumMachine, Mixer, LayoutEditor };

// Broadcasts every binding change, so several surfaces can each react. onBindingsChanged has one slot,
// which MainComponent holds.
class ShortcutManager : public juce::ChangeBroadcaster {
public:
    /** Which modifier convention the factory defaults follow. Mac: Cmd and Ctrl are two physical keys.
     *  Other: JUCE folds Cmd onto Ctrl. The host platform picks it; setDefaultsPlatform() lets a test
     *  build the other platform's table. */
    enum class DefaultsPlatform { Mac, Other };

    static constexpr DefaultsPlatform hostDefaultsPlatform() noexcept {
#if JUCE_MAC
        return DefaultsPlatform::Mac;
#else
        return DefaultsPlatform::Other;
#endif
    }

    DefaultsPlatform getDefaultsPlatform() const noexcept { return defaultsPlatform; }

    /** Rebuilds every binding from the defaults of `platform` (and makes migrations follow it). */
    void setDefaultsPlatform(DefaultsPlatform platform);

    ShortcutManager();

    /** Reads saved bindings and runs the one-shot migrations. Persistence is opt-in: without this, or
     *  with no user-settings file, saveToProperties() only notifies. */
    void loadFromProperties(juce::ApplicationProperties& props);

    /** Where Repeat and Toggle Mod Matrix live off the Mac. */
    static juce::ModifierKeys otherPlatformRepeatModifiers() noexcept;
    static juce::ModifierKeys otherPlatformModMatrixModifiers() noexcept;

    /** Persists every binding, then notifies onBindingsChanged and change listeners synchronously. */
    void saveToProperties();

    juce::KeyPress getBinding(const juce::String& actionId) const;

    /** Every action bound to `key`, in getActionIds() order. */
    juce::StringArray getActionsForKeyPress(const juce::KeyPress& key) const;

    /** The one "did this keystroke fire that binding?" rule. Modifiers match exactly and the key code
     *  case-insensitively; with Shift held on both sides, a US-layout shifted symbol also matches its
     *  base key. An invalid binding matches nothing. */
    static bool keyPressMatches(const juce::KeyPress& binding, const juce::KeyPress& pressed);

    juce::String getActionForKeyPress(const juce::KeyPress& key) const;

    /** Changes one binding in memory and notifies change listeners synchronously. */
    void setBinding(const juce::String& actionId, const juce::KeyPress& key);

    /** The action already using `key` in the same category as `actionId`, or an empty string. An unknown
     *  `actionId` counts as General. */
    juce::String getConflictingAction(const juce::String& actionId, const juce::KeyPress& key) const;

    /** Rebuilds the bindings from the factory defaults. Does not notify. */
    void resetToDefaults();

    // The per-category default bindings resetToDefaults() assembles (ShortcutManagerDefaults.cpp).
    void addGeneralDefaultBindings();
    void addGraphDefaultBindings();
    void addTimelineDefaultBindings();
    void addPianoRollDefaultBindings();
    void addDrumMachineDefaultBindings();
    void addLayoutEditorDefaultBindings();

    static juce::String keyPressToDisplayString(const juce::KeyPress& key);

    // Display text for `actionId`'s row in Settings -> Keyboard Shortcuts and the MIDI Remote
    // action picker (docs/control/midi-remote.md#action-targets).
    static juce::String getActionDescription(const juce::String& actionId);

    /** The category `actionId` belongs to; an unknown id answers General. */
    static ShortcutCategory getCategory(const juce::String& actionId);

    static juce::String getCategoryName(ShortcutCategory category);

    /** Settings sections are drawn in this order, and getActionIds() is grouped the same way. */
    static const std::vector<ShortcutCategory>& getCategoryOrder();

    /** The ids in `category`, in getActionIds() order. */
    static juce::StringArray getActionIdsInCategory(ShortcutCategory category);

    static juce::KeyPress parseKeyPress(const juce::String& encoded);
    static juce::String encodeKeyPress(const juce::KeyPress& key);

    const juce::StringArray& getActionIds() const { return actionIds; }

    std::function<void()> onBindingsChanged;

private:
    // The one-shot binding migrations (ShortcutManagerMigrations.cpp).
    struct Migrations;

    /** One row per rebindable action: the persisted id and the category that decides its Settings
     *  section and conflict scope. The rows live in ShortcutManagerActionTable.cpp: a new action is
     *  one row there plus a default binding, a description and (for a command action) an
     *  AppCommands entry. */
    struct ActionEntry {
        const char* id;
        ShortcutCategory category;
    };

    static const std::vector<ActionEntry>& getActionTable();

    // Binding-vs-binding equality, for conflict detection only.
    static bool bindingsCollide(const juce::KeyPress& binding, const juce::KeyPress& key);
    static int foldKeyCodeCase(int keyCode);
    static int usLayoutUnshiftedBase(int keyCode);

    std::map<juce::String, juce::KeyPress> bindings;
    juce::ApplicationProperties* appProperties = nullptr;

    // Built from getActionTable() in the constructor, so the order and the categories can never
    // drift apart.
    juce::StringArray actionIds;
    DefaultsPlatform defaultsPlatform = hostDefaultsPlatform();

    // Weak-referenceable so a holder can guard its teardown when a test destroys the manager first.
    JUCE_DECLARE_WEAK_REFERENCEABLE(ShortcutManager)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutManager)
};

/** Display text for `actionId`'s current binding, for any tooltip that names a rebindable key. A
 *  non-null `manager` uses its live binding, and a cleared binding gives an empty string; a null
 *  `manager` uses `fallback`. A bare letter is lowercase. */
juce::String shortcutHintFor(const ShortcutManager* manager, const juce::String& actionId,
                             const juce::KeyPress& fallback);

/** The platform's primary modifier key ("Cmd" on macOS, "Ctrl" elsewhere), for copy that names it alone. */
juce::String platformCommandKeyName();
