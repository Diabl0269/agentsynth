#pragma once

// App-layer, NOT Core: the default root is derived from userSettingsOptions() (Source/UserSettings.h),
// and Core must never depend on where settings live. Same placement and injection pattern as
// MidiRemote/ControllerProfileStore.h; never touches juce::ApplicationProperties.

#include "Modules/CardLayout.h"
#include "Plugin/Hosting/CardLayoutStore.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * Per-plugin-type card layouts on disk: `<root>/<format>-<uid>/default.json` plus sibling
 * `<preset>.json` files (docs/control/plugin-card-layout.md#persistence). All calls are
 * message-thread only.
 */
class PluginCardLayoutStore : private CardLayoutStore {
public:
    /** Notified on the message thread after a plugin's default layout was set or cleared. */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void layoutChangedForPlugin(const PluginIdentity& identity) = 0;
    };

    using LoadStatus = CardLayoutLoadStatus;
    struct LoadResult {
        LoadStatus status = LoadStatus::NotFound;
        CardLayout layout;
        juce::String pluginName; ///< The display name stored inside the file.
    };

    /** The real `<settings folder>/PluginCardLayouts` directory. */
    PluginCardLayoutStore();
    /** Test/injection constructor: `rootDir` is used verbatim and created on demand. */
    explicit PluginCardLayoutStore(juce::File rootDir);

    /** Pure path arithmetic, no I/O. */
    static juce::File resolveDefaultRootDirectory();

    using CardLayoutStore::getRootDirectory;
    juce::File getPluginDirectory(const PluginIdentity& identity) const;

    LoadResult loadDefault(const PluginIdentity& identity) const;
    /** Writes default.json and notifies. Clears no instance override -- that is the picker's job. */
    bool setDefault(const PluginIdentity& identity, const CardLayout& layout);
    /** Deletes default.json; notifies only if there was one. */
    bool clearDefault(const PluginIdentity& identity);

    /** Preset names (file stems), sorted, excluding the reserved "default". */
    juce::StringArray listPresets(const PluginIdentity& identity) const;
    /** Fails for an empty name or the reserved "default"; overwrites an existing preset. */
    bool savePreset(const PluginIdentity& identity, const juce::String& name, const CardLayout& layout);
    LoadResult loadPreset(const PluginIdentity& identity, const juce::String& name) const;
    bool deletePreset(const PluginIdentity& identity, const juce::String& name);

    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }

private:
    static juce::String directoryKey(const PluginIdentity& identity);
    static juce::var ownerOf(const PluginIdentity& identity);
    static LoadResult toLoadResult(CardLayoutLoadResult&& base);

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCardLayoutStore)
};

} // namespace synth
