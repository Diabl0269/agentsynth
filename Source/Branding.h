#pragma once

#include <cstdlib>

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

// Default subdirectory under the user's Music folder for project bundles (.agsproj).
// Project save/open dialogs start here via ProjectBundle::getDefaultProjectsDirectory().
constexpr const char* kProjectsFolderName = "AgentSynth";

// Previous kSettingsFolderName values, ordered newest-first. SettingsMigration walks this
// list at startup and copies forward the first entry that exists and has data, so a rename
// doesn't orphan existing users' settings/presets/themes. Seeded with the current name;
// a future rename should prepend the old value here (leaving older entries in place).
constexpr const char* kLegacyFolderNames[] = {kSettingsFolderName, "Gravisynth"};

// Product website, for the About box / support links. Not yet referenced anywhere.
constexpr const char* kWebsiteUrl = "https://agentsynth.app";

// Support contact address, for the About box / support links. Not yet referenced anywhere.
constexpr const char* kSupportEmail = "support@agentsynth.app";

// Static Polar checkout link (covers both the monthly and yearly Pro products) opened by the
// "Upgrade to Pro" button on a Quota-kind AI error (P4-4, Source/UI/AIChatComponent.cpp).
// Deliberately static rather than a dynamically-created checkout session — see docs/billing.md's
// "what this deliberately does not do" for why P4-4 doesn't create checkout sessions.
constexpr const char* kUpgradeUrl = "https://buy.polar.sh/polar_cl_DkiJlmel2CXVtpl236TvS52omgYaZM26HGe1U0rbD75";

// Production base URL for the synth-platform inference/auth/billing service (Cloud Run,
// apps/infra/Pulumi.prod.yaml's authPublicBaseUrl in the synth-platform repo). Default host for
// RemoteProvider, AuthClient and AccountService — see AIProviderRegistry.cpp and
// MainComponent.h/.cpp. As of P4-6 the Cloud Run service still has no `allUsers` invoker binding
// (tracked separately under P4-7), so requests here 403 at the IAM layer until that follow-up
// infra step makes it public — expected, not a bug in this client.
constexpr const char* kApiBaseUrl = "https://synth-api-6eft3t2kxq-uc.a.run.app";

// Debug-only override for kApiBaseUrl above — lets a locally-built app point its
// auth/entitlement/cloud-history traffic (AccountService/AuthClient, see MainComponent.h) at a
// locally-run synth-platform server, so testing Pro-gated flows end-to-end (e.g. cloud conversation
// history) doesn't require hand-editing this file and rebuilding per URL change. See
// docs/testing.md "Testing Cloud-Gated Features Locally". Compiled out of Release builds entirely
// (#ifndef NDEBUG) so a tampered environment variable can never redirect a shipped binary's auth
// traffic away from production — same trust boundary as every other "never trust the client"
// invariant in this codebase (see CLAUDE.md).
inline const char* resolveApiBaseUrl() {
#ifndef NDEBUG
    if (const char* override = std::getenv("AGENTSYNTH_LOCAL_API_URL"))
        if (override[0] != '\0')
            return override;
#endif
    return kApiBaseUrl;
}

} // namespace synth::branding
