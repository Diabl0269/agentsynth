#include "UpdateManager.h"

#if JUCE_WINDOWS

#include <juce_events/juce_events.h>
#include <winsparkle.h>

namespace synth::update {

namespace {
// WinSparkle calls this off the main thread right after it launches the installer, and only if
// win_sparkle_set_can_shutdown_callback() (below) returned true — JUCEApplicationBase::quit() is
// safe to call from any thread (it posts a quit message), matching the doc's thread-safety note.
void __cdecl handleShutdownRequest() { juce::JUCEApplicationBase::quit(); }

// Always safe to shut down: there's no unsaved-document state in this app that would make a
// silent, WinSparkle-driven quit unsafe.
int __cdecl handleCanShutdown() { return TRUE; }
} // namespace

class UpdateManager::Impl {
public:
    Impl() {
        const juce::String feedURL(SYNTH_UPDATE_FEED_URL_STR);
        const juce::String publicKey(SYNTH_WINSPARKLE_PUBLIC_KEY_STR);

        started = (!feedURL.isEmpty() && !publicKey.isEmpty());

        if (started) {
            win_sparkle_set_appcast_url(feedURL.toRawUTF8());
            win_sparkle_set_eddsa_public_key(publicKey.toRawUTF8());
            win_sparkle_set_can_shutdown_callback(&handleCanShutdown);
            win_sparkle_set_shutdown_request_callback(&handleShutdownRequest);
            win_sparkle_init();
        }
    }

    ~Impl() {
        if (started)
            win_sparkle_cleanup();
    }

    bool started = false;
};

UpdateManager::UpdateManager()
    : impl(std::make_unique<Impl>()) {}

UpdateManager::~UpdateManager() = default;

bool UpdateManager::isAvailable() const { return impl->started; }

void UpdateManager::checkForUpdates() {
    if (impl->started)
        win_sparkle_check_update_with_ui();
}

} // namespace synth::update

#endif // JUCE_WINDOWS
