#pragma once

// Single source of truth for product identity in application code. Renaming the product
// later should mean editing the values in this file (plus the mirrored CMake cache
// variables in CMakeLists.txt, which JUCE's PRODUCT_NAME/COMPANY_NAME/BUNDLE_ID need at
// configure time and can't read a C++ header for).

namespace synth::branding {

// Window title (via ProjectInfo::projectName, generated from CMake PRODUCT_NAME), About
// box, and installer/package display name.
constexpr const char* kProductName = "Gravisynth";

// JUCE COMPANY_NAME (juce_add_gui_app) and the macOS bundle vendor field.
constexpr const char* kCompanyName = "yourcompany";

// macOS/iOS CFBundleIdentifier (juce_add_gui_app BUNDLE_ID).
constexpr const char* kBundleIdentifier = "com.gravisynth.app";

// juce::PropertiesFile::Options::folderName — the on-disk ApplicationProperties folder,
// also the parent of the user Themes folder (ThemeManager::getUserThemesFolder()).
// WARNING: do NOT change this value. It is the folder name existing users' settings,
// presets, and themes already live under; changing it orphans that data. A follow-up task
// adds a migration shim — only after that lands is it safe to change.
constexpr const char* kSettingsFolderName = "Gravisynth";

// Folder name reserved for user-saved presets. Not yet referenced on disk (preset
// save/load currently goes through ad hoc juce::FileChooser dialogs with no fixed
// directory); seeded here so a future on-disk preset store has a name to use.
constexpr const char* kPresetFolderName = "Gravisynth";

// Product website, for the About box / support links. Not yet referenced anywhere.
constexpr const char* kWebsiteUrl = "https://gravisynth.app";

// Support contact address, for the About box / support links. Not yet referenced anywhere.
constexpr const char* kSupportEmail = "support@gravisynth.app";

} // namespace synth::branding
