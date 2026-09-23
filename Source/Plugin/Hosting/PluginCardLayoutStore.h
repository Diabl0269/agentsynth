#pragma once

// App-layer, NOT Core: the default root is derived from userSettingsOptions() (Source/UserSettings.h),
// and Core must never depend on where settings live. Same placement and injection pattern as
// MidiRemote/ControllerProfileStore.h; never touches juce::ApplicationProperties.

#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginBackend.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * Per-plugin-type card layouts on disk: `<root>/<format>-<uid>/default.json` plus sibling
 * `<preset>.json` files (docs/control/plugin-card-layout.md#persistence). All calls are
 * message-thread only.
 */
class PluginCardLayoutStore {
public:
    /** Notified on the message thread after a plugin's default layout was set or cleared. */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void layoutChangedForPlugin(const PluginIdentity& identity) = 0;
    };

    enum class LoadStatus { Ok, NotFound, Malformed, UnsupportedVersion };
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

    const juce::File& getRootDirectory() const { return rootDir_; }
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
    juce::File presetFile(const PluginIdentity& identity, const juce::String& name) const;
    bool writeLayout(const juce::File& file, const PluginIdentity& identity, const CardLayout& layout) const;
    static LoadResult readLayout(const juce::File& file);

    juce::File rootDir_;
    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginCardLayoutStore)
};

} // namespace synth
