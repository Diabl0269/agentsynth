#pragma once

#include <juce_core/juce_core.h>

#if JUCE_MAC

#include <memory>

namespace synth::update {

// Thin bridge to Sparkle's SPUStandardUpdaterController (Source/Update/SparkleUpdateManager.mm).
// Construction reads SUFeedURL/SUPublicEDKey from the app bundle's Info.plist (populated at
// configure time from SYNTH_UPDATE_FEED_URL/SYNTH_SPARKLE_PUBLIC_KEY in CMakeLists.txt) and only
// starts Sparkle's updater if both are non-empty. A build with no signing key configured yet
// (the default until someone runs Sparkle's generate_keys tool — see docs/distribution.md)
// therefore never starts Sparkle and never shows its "app is misconfigured" alert.
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

#endif // JUCE_MAC
