#include "UpdateManager.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

namespace synth::update {

class UpdateManager::Impl {
public:
    Impl() {
        NSString* publicKey = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUPublicEDKey"];
        NSString* feedURL = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUFeedURL"];

        started = (publicKey.length > 0 && feedURL.length > 0);

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
