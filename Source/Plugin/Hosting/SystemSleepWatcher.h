#pragma once

#include <functional>
#include <memory>

namespace synth {

/** Tells the app, on the message thread, just before the machine sleeps and just after it wakes. macOS only; on
 *  every other platform it never calls back. Both callbacks run synchronously from the OS notification. */
class SystemSleepWatcher {
public:
    SystemSleepWatcher(std::function<void()> willSleep, std::function<void()> didWake);
    ~SystemSleepWatcher();

    SystemSleepWatcher(const SystemSleepWatcher&) = delete;
    SystemSleepWatcher& operator=(const SystemSleepWatcher&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace synth
