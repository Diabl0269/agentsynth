#pragma once

#include <juce_core/juce_core.h>

#if JUCE_MAC || JUCE_WINDOWS

#include <memory>

namespace synth::update {

// Thin bridge to Sparkle (macOS, Source/Update/SparkleUpdateManager.mm) or WinSparkle (Windows,
// Source/Update/WinSparkleUpdateManager.cpp). Construction reads the feed URL/public key baked in
// at configure time (macOS: SUFeedURL/SUPublicEDKey merged into the app bundle's Info.plist;
// Windows: SYNTH_UPDATE_FEED_URL_STR/SYNTH_WINSPARKLE_PUBLIC_KEY_STR compile definitions, since
// there's no Info.plist) and only starts the platform updater if both are non-empty. A build with
// no signing key configured yet (the default until someone runs the platform's key-generation
// tool — see docs/distribution.md) therefore never starts and never shows a "misconfigured" alert.
class UpdateManager {
public:
    UpdateManager();
    ~UpdateManager();

    // False if Sparkle didn't start — callers should hide/disable "Check for Updates" in that case.
    bool isAvailable() const;

    // Shows Sparkle's standard progress UI and reports results (up to date / update available /
    // check failed) directly to the user. No-op if !isAvailable().
    void checkForUpdates();

private:
    class Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateManager)
};

} // namespace synth::update

#endif // JUCE_MAC || JUCE_WINDOWS
