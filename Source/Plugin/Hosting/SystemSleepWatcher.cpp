// Concern: SystemSleepWatcher on platforms with no sleep notification wired up (everything but macOS, where
// SystemSleepWatcherMac.mm provides it): the watcher exists and never calls back.
#include "SystemSleepWatcher.h"
#include <juce_core/juce_core.h>

#if !JUCE_MAC

namespace synth {

class SystemSleepWatcher::Impl {};

SystemSleepWatcher::SystemSleepWatcher(std::function<void()>, std::function<void()>)
    : impl_(std::make_unique<Impl>()) {}

SystemSleepWatcher::~SystemSleepWatcher() = default;

} // namespace synth

#endif
