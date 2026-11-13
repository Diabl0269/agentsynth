// The harness builds a real MainComponent (--save-projects), whose header pulls in synth::update::UpdateManager.
// The real one is the Sparkle bridge, which needs the Sparkle framework; a headless eval run has no updater, so
// this inert stand-in satisfies the linker. isAvailable() is false, exactly like a build with no signing key.

#include "Update/UpdateManager.h"

#if JUCE_MAC || JUCE_WINDOWS

namespace synth::update {

class UpdateManager::Impl {};

UpdateManager::UpdateManager() = default;
UpdateManager::~UpdateManager() = default;

bool UpdateManager::isAvailable() const { return false; }

void UpdateManager::checkForUpdates() {}

} // namespace synth::update

#endif
