// Concern: the macOS half of SystemSleepWatcher -- NSWorkspace's will-sleep and did-wake notifications.
#include "SystemSleepWatcher.h"

#import <AppKit/AppKit.h>

namespace synth {

// Both observers are delivered on the main queue, which is the JUCE message thread. The will-sleep one arrives
// before the machine goes down and the callback unloads synchronously, so the plugins are gone before sleep starts.
class SystemSleepWatcher::Impl {
public:
    Impl(std::function<void()> willSleep, std::function<void()> didWake) {
        NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
        auto sleepCallback = std::make_shared<std::function<void()>>(std::move(willSleep));
        auto wakeCallback = std::make_shared<std::function<void()>>(std::move(didWake));
        sleepObserver = [center addObserverForName:NSWorkspaceWillSleepNotification
                                            object:nil
                                             queue:[NSOperationQueue mainQueue]
                                        usingBlock:^(NSNotification*) {
                                            if (*sleepCallback)
                                                (*sleepCallback)();
                                        }];
        wakeObserver = [center addObserverForName:NSWorkspaceDidWakeNotification
                                           object:nil
                                            queue:[NSOperationQueue mainQueue]
                                       usingBlock:^(NSNotification*) {
                                           if (*wakeCallback)
                                               (*wakeCallback)();
                                       }];
    }

    ~Impl() {
        NSNotificationCenter* center = [[NSWorkspace sharedWorkspace] notificationCenter];
        [center removeObserver:sleepObserver];
        [center removeObserver:wakeObserver];
    }

private:
    id sleepObserver = nil;
    id wakeObserver = nil;
};

SystemSleepWatcher::SystemSleepWatcher(std::function<void()> willSleep, std::function<void()> didWake)
    : impl_(std::make_unique<Impl>(std::move(willSleep), std::move(didWake))) {}

SystemSleepWatcher::~SystemSleepWatcher() = default;

} // namespace synth
