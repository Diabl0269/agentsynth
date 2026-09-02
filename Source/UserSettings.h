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

} // namespace synth
