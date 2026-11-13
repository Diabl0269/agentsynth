#pragma once

// App-layer, NOT Core: roots are derived from userSettingsOptions() (Source/UserSettings.h) by the
// concrete stores. The directory/preset/default file logic shared by PluginCardLayoutStore and
// ModuleCardLayoutStore; message-thread only.

#include "Modules/CardLayout.h"
#include <juce_core/juce_core.h>

namespace synth {

enum class CardLayoutLoadStatus { Ok, NotFound, Malformed, UnsupportedVersion };

struct CardLayoutLoadResult {
    CardLayoutLoadStatus status = CardLayoutLoadStatus::NotFound;
    CardLayout layout;
    juce::var file; ///< The whole parsed file, for the owner's identity properties.
};

/**
 * `<root>/<key>/default.json` plus sibling `<preset>.json` files. `key` is a directory name the
 * concrete store derives from its owner (a plugin identity, a module type); `owner` is merged into
 * every written file beside the layout and ignored by CardLayout::fromVar.
 */
class CardLayoutStore {
public:
    explicit CardLayoutStore(juce::File rootDir);
    virtual ~CardLayoutStore() = default;

    const juce::File& getRootDirectory() const { return rootDir_; }
    juce::File directoryFor(const juce::String& key) const;

    CardLayoutLoadResult loadDefault(const juce::String& key) const;
    bool writeDefault(const juce::String& key, const juce::var& owner, const CardLayout& layout);
    /** Deletes default.json; true when none remains. `removed` reports whether a file was deleted. */
    bool removeDefault(const juce::String& key, bool& removed);

    /** Preset names (file stems), sorted, excluding the reserved "default". */
    juce::StringArray listPresets(const juce::String& key) const;
    /** Fails for an empty name or the reserved "default"; overwrites an existing preset. */
    bool writePreset(const juce::String& key, const juce::var& owner, const juce::String& name,
                     const CardLayout& layout);
    CardLayoutLoadResult loadPreset(const juce::String& key, const juce::String& name) const;
    bool removePreset(const juce::String& key, const juce::String& name);

private:
    juce::File presetFile(const juce::String& key, const juce::String& name) const;
    juce::File defaultFile(const juce::String& key) const;
    static bool writeLayout(const juce::File& file, const juce::var& owner, const CardLayout& layout);
    static CardLayoutLoadResult readLayout(const juce::File& file);

    juce::File rootDir_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutStore)
};

} // namespace synth
