#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace synth {

/**
 * The most-recently-opened project bundles, most-recent-first — what the Load menu's "Recent
 * Projects" section shows.
 *
 * Persistence is the OWNER's job, same shape as PluginScanService (see its header comment): this
 * class never touches juce::ApplicationProperties itself. MainComponent restores it from
 * `kRecentProjectsSettingKey` (UserSettings.h) on startup and writes it back after every add.
 */
class RecentProjects {
public:
    /** Oldest entry past this count is dropped on the next add. */
    static constexpr int kMaxEntries = 10;

    /** Moves `bundleDir` to the front, deduping an existing entry for the same path rather than
     *  adding a second one. Empty/invalid files are ignored. */
    void addProject(const juce::File& bundleDir);

    /** The raw list, most-recent-first, exactly as stored — including paths that may no longer
     *  exist on disk. Call pruneMissing() first to hide those from a menu. */
    std::vector<juce::File> getEntries() const;

    /** Drops every entry whose bundle directory no longer exists on disk, and returns how many
     *  were removed. */
    int pruneMissing();

    void clear();

    /** One XML document holding the list, in order — the format toXml()/loadFromXml() round-trip
     *  through a PropertiesFile string value, the same idiom PluginScanService uses. */
    std::unique_ptr<juce::XmlElement> toXml() const;

    /** Replaces the list from a document toXml() produced. Unrecognised/malformed children are
     *  skipped rather than aborting the whole load. */
    void loadFromXml(const juce::XmlElement& xml);

private:
    std::vector<juce::String> paths_; // absolute paths, most-recent-first
};

} // namespace synth
