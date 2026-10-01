#pragma once

// App-layer, NOT Core: the default root is derived from userSettingsOptions() (Source/UserSettings.h).

#include "Plugin/Hosting/CardLayoutStore.h"
#include <map>

namespace synth {

/**
 * Per-module-type card layouts on disk: `<root>/<ModuleType>/default.json` plus sibling
 * `<preset>.json` files, the same shape as PluginCardLayoutStore (docs/layout/module-card-layout.md).
 * `moduleType` is the factory type name ("Filter", "Sample & Hold"). Message-thread only.
 */
class ModuleCardLayoutStore : private CardLayoutStore {
public:
    /** Notified on the message thread after a type's default layout was set or cleared. */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void layoutChangedForModuleType(const juce::String& moduleType) = 0;
    };

    using LoadStatus = CardLayoutLoadStatus;
    using LoadResult = CardLayoutLoadResult;

    /** The real `<settings folder>/ModuleCardLayouts` directory. */
    ModuleCardLayoutStore();
    /** Test/injection constructor: `rootDir` is used verbatim and created on demand. */
    explicit ModuleCardLayoutStore(juce::File rootDir);

    /** Pure path arithmetic, no I/O. */
    static juce::File resolveDefaultRootDirectory();

    using CardLayoutStore::getRootDirectory;
    juce::File getTypeDirectory(const juce::String& moduleType) const;

    LoadResult loadDefault(const juce::String& moduleType) const;
    /** Writes default.json and notifies. Clears no instance override. */
    bool setDefault(const juce::String& moduleType, const CardLayout& layout);
    /** Deletes default.json; notifies only if there was one. */
    bool clearDefault(const juce::String& moduleType);

    /** Preset names (file stems), sorted, excluding the reserved "default". */
    juce::StringArray listPresets(const juce::String& moduleType) const;
    /** Fails for an empty name or the reserved "default"; overwrites an existing preset. */
    bool savePreset(const juce::String& moduleType, const juce::String& name, const CardLayout& layout);
    LoadResult loadPreset(const juce::String& moduleType, const juce::String& name) const;
    bool deletePreset(const juce::String& moduleType, const juce::String& name);

    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }

    /** Counts this store's default writes for `moduleType` (in memory, starts at 0); no I/O. */
    int getRevision(const juce::String& moduleType) const;

private:
    static juce::String directoryKey(const juce::String& moduleType);
    static juce::var ownerOf(const juce::String& moduleType);
    void defaultChanged(const juce::String& moduleType);

    juce::ListenerList<Listener> listeners_;
    std::map<juce::String, int> revisions_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ModuleCardLayoutStore)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleCardLayoutStore)
};

} // namespace synth
