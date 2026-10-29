#include "AutomationLaunch.h"
#include "UpdateManager.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>
#include <juce_events/juce_events.h>

namespace synth::update {

class UpdateManager::Impl {
public:
    Impl() {
        NSString* publicKey = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUPublicEDKey"];
        NSString* feedURL = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUFeedURL"];

        // An automation launch (test mode) never checks for updates.
        started = (publicKey.length > 0 && feedURL.length > 0) &&
                  !synth::isAutomationLaunch(juce::JUCEApplicationBase::getCommandLineParameterArray());

        if (started) {
            controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                                       updaterDelegate:nil
                                                                    userDriverDelegate:nil];
        }
    }

    bool started = false;
    SPUStandardUpdaterController* controller = nil;
};

UpdateManager::UpdateManager()
    : impl(std::make_unique<Impl>()) {}

UpdateManager::~UpdateManager() = default;

bool UpdateManager::isAvailable() const { return impl->started; }

void UpdateManager::checkForUpdates() {
    if (impl->started)
        [impl->controller checkForUpdates:nil];
}

} // namespace synth::update

#endif // JUCE_MAC
