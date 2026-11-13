#include "AccountRequests.h"
#include "Auth/DeviceIdStore.h"
#include "Branding.h"
#include <atomic>
#include <memory>
#include <thread>

namespace synth {

AuthClient AccountRequests::makeClient() const {
    return testPerformer ? AuthClient(branding::resolveApiBaseUrl(), "synth-desktop", testPerformer,
                                      DeviceIdStore().getDeviceId())
                         : AuthClient(branding::resolveApiBaseUrl(), "synth-desktop", DeviceIdStore().getDeviceId());
}

void AccountRequests::run(std::function<void()> work, std::function<void()> deliver) const {
    if (runInline) {
        work();
        deliver();
        return;
    }
    // Detached: the worker owns copies of everything it touches. The delivery hops back to the message thread.
    std::thread([work = std::move(work), deliver = std::move(deliver)]() mutable {
        work();
        juce::MessageManager::callAsync(std::move(deliver));
    }).detach();
}

void AccountRequests::deleteAccount(AccountService& account, DeleteDone onDone) const {
    const auto token = account.getAccessToken();
    auto result = std::make_shared<AuthClient::DeleteAccountResult>();
    if (token.isEmpty()) {
        result->httpStatus = 401; // nobody to delete
        if (onDone)
            onDone(*result);
        return;
    }

    run(
        [client = makeClient(), token, result] {
            std::atomic<bool> cancelled{false};
            *result = client.deleteAccount(token, cancelled);
        },
        [result, onDone = std::move(onDone)] {
            if (onDone)
                onDone(*result);
        });
}

void AccountRequests::submitExitSurvey(AccountService& account, const juce::String& kind,
                                       const std::vector<juce::String>& reasons, const juce::String& comment,
                                       SurveyDone onDone) const {
    const auto token = account.getAccessToken();
    auto ok = std::make_shared<bool>(false);

    run(
        [client = makeClient(), token, kind, reasons, comment, ok] {
            std::atomic<bool> cancelled{false};
            *ok = client.submitExitSurvey(token, kind, reasons, comment, cancelled).ok;
        },
        [ok, onDone = std::move(onDone)] {
            if (onDone)
                onDone(*ok);
        });
}

} // namespace synth
