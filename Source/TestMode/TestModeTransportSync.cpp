// Concern: keeping the transport snapshot current in a test-mode launch, which runs no audio thread.

#include "TestMode/TestModeTransportSync.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeAudioPump.h"
#include "TestMode/TestModeComponents.h"

namespace synth::testmode {

bool syncTransportWithoutAudio(AudioEngine& engine) {
    if (engine.isReceivingDeviceCallbacks() || audioPumpRunning())
        return false;
    engine.getTransport().syncWithoutAudioThread();
    return true;
}

namespace {

class TransportSync : private juce::Timer {
public:
    TransportSync() { startTimerHz(60); }
    ~TransportSync() override { stopTimer(); }

private:
    void timerCallback() override {
        if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
            if (auto* mc = dynamic_cast<MainComponent*>(window->getContentComponent()))
                syncTransportWithoutAudio(mc->getAudioEngine());
    }
};

// Held by a DeletedAtShutdown object, so the timer goes before JUCE does (never a plain static: exit order).
struct SyncHolder : public juce::DeletedAtShutdown {
    std::unique_ptr<TransportSync> sync;
};

} // namespace

void startTransportSync() {
    static SyncHolder* holder = new SyncHolder();
    if (holder->sync == nullptr)
        holder->sync = std::make_unique<TransportSync>();
}

} // namespace synth::testmode
