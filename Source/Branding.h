#pragma once

// Single source of truth for product identity in application code. Renaming the product
// later should mean editing the values in this file (plus the mirrored CMake cache
// variables in CMakeLists.txt, which JUCE's PRODUCT_NAME/COMPANY_NAME/BUNDLE_ID need at
// configure time and can't read a C++ header for).

namespace synth::branding {

// Window title (via ProjectInfo::projectName, generated from CMake PRODUCT_NAME), About
// box, and installer/package display name.
constexpr const char* kProductName = "Agent Synth";

// JUCE COMPANY_NAME (juce_add_gui_app) and the macOS bundle vendor field.
constexpr const char* kCompanyName = "yourcompany";

// macOS/iOS CFBundleIdentifier (juce_add_gui_app BUNDLE_ID).
constexpr const char* kBundleIdentifier = "com.agentsynth.app";

// juce::PropertiesFile::Options::folderName — the on-disk ApplicationProperties folder,
// also the parent of the user Themes folder (ThemeManager::getUserThemesFolder()).
// Changing this value is now safe: SettingsMigration::migrateUserData() runs at startup
// (before ApplicationProperties is initialised, see Main.cpp) and copies data forward from
// the first matching entry in kLegacyFolderNames below. When renaming, prepend the OLD value
// of kSettingsFolderName to kLegacyFolderNames before changing this one.
constexpr const char* kSettingsFolderName = "Agent Synth";

// Folder name reserved for user-saved presets. Not yet referenced on disk (preset
// save/load currently goes through ad hoc juce::FileChooser dialogs with no fixed
// directory); seeded here so a future on-disk preset store has a name to use.
constexpr const char* kPresetFolderName = "Agent Synth";

// Previous kSettingsFolderName values, ordered newest-first. SettingsMigration walks this
// list at startup and copies forward the first entry that exists and has data, so a rename
// doesn't orphan existing users' settings/presets/themes. Seeded with the current name;
// a future rename should prepend the old value here (leaving older entries in place).
constexpr const char* kLegacyFolderNames[] = {kSettingsFolderName, "Gravisynth"};

// Product website, for the About box / support links. Not yet referenced anywhere.
constexpr const char* kWebsiteUrl = "https://agentsynth.app";

// Support contact address, for the About box / support links. Not yet referenced anywhere.
constexpr const char* kSupportEmail = "support@agentsynth.app";

} // namespace synth::branding
