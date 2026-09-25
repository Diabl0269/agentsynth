#pragma once

#include "Branding.h"
#include <juce_data_structures/juce_data_structures.h>

namespace synth {

/**
 * The ONE user settings file this product reads and writes, plus the keys more than one owner
 * touches.
 *
 * Three owners open it independently: Main.cpp (first, to migrate a renamed folder forward),
 * MainComponent (the editor's own persistence) and AgentSynthAudioProcessor (the hosted-plugin scan
 * list, which a plugin instance must restore whether or not the host ever opens our editor). They
 * have to agree field for field: a copy that drifts means a plugin instance silently reading a
 * DIFFERENT file from the app that wrote it.
 *
 * This is storage LOCATION, not a settings reader — Core still never touches
 * juce::ApplicationProperties (see AudioEngine's device-state comment and PluginScanService's).
 */
inline juce::PropertiesFile::Options userSettingsOptions() {
    juce::PropertiesFile::Options options;
    options.applicationName = branding::kProductName;
    options.folderName = branding::kSettingsFolderName;
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

/** The scan list (PluginScanService::toXml) that the app writes after a scan and BOTH the app and
 *  the plugin processor restore on startup. */
inline constexpr const char* kPluginScanListSettingKey = "pluginScanList";

/** The recent-projects list (RecentProjects::toXml) the Load menu's "Recent Projects" section
 *  shows. Single-owner (MainComponent only — the plugin editor shares the same MainComponent, and
 *  a hosted plugin never opens its own Load dialog), unlike the scan list above. */
inline constexpr const char* kRecentProjectsSettingKey = "recentProjects";

/** MIDI Remote's Default takeover (docs/control/midi-remote-ui.md#settings): "jump" / "pickup" /
 *  "scale", the same spellings a Takeover takes in the project's "midiRemote" JSON. Absent or
 *  unrecognised reads as "scale". Written by PreferencesSettingsTab, read by MainComponent (which
 *  hands it to RemoteEngine::setDefaultTakeover) through MidiRemotePreferences.h. */
inline constexpr const char* kMidiRemoteDefaultTakeoverSettingKey = "midiRemoteDefaultTakeover";

/** MIDI Remote's "Show MIDI badges on mapped controls" (bool, default true). Written by
 *  PreferencesSettingsTab, read by MainComponent, which hands it to the badge painter. */
inline constexpr const char* kMidiRemoteShowBadgesSettingKey = "midiRemoteShowBadges";

/** One-time "Drop it on any knob to modulate that parameter" status-bar hint (FRO289): shown the
 *  first time a cable drag starts from a modulation source's output, never again once set. Bool,
 *  default false (unshown). Read/written by GraphEditor::beginConnectionDrag through
 *  GraphEditor::propertiesFile_ -- the same juce::PropertiesFile the macro recolour favourites and
 *  the piano roll's scale assist persist through (see those members' own comments). */
inline constexpr const char* kModDropHintShownSettingKey = "modDropHintShown";

} // namespace synth
