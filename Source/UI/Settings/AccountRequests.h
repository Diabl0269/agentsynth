#pragma once

#include "AI/AccountService.h"
#include "AI/AuthClient.h"
#include <functional>
#include <vector>

namespace synth {

/**
 * @class AccountRequests
 * @brief The two account calls the Account tab makes (DELETE /v1/account, POST /v1/exit-survey),
 *        run off the message thread on a detached worker with the result handed back on the message
 *        thread. Same shape as FeedbackSettingsTab::sendFeedback: the worker owns a copy of a
 *        stateless AuthClient and the access token, and never touches a component.
 *
 * Callbacks run on the message thread and may outlive whoever asked: capture a SafePointer, not
 * `this`.
 */
class AccountRequests {
public:
    using DeleteDone = std::function<void(const AuthClient::DeleteAccountResult&)>;
    using SurveyDone = std::function<void(bool ok)>;

    // A signed-out service answers at once with a 401-shaped result.
    void deleteAccount(AccountService& account, DeleteDone onDone) const;

    // `comment` is omitted from the request when empty. `onDone` may be empty.
    void submitExitSurvey(AccountService& account, const juce::String& kind, const std::vector<juce::String>& reasons,
                          const juce::String& comment, SurveyDone onDone = {}) const;

    // Testing hooks: a fake transport, and running the work and its callback on the calling thread.
    void setHttpPerformerForTesting(AuthClient::HttpPerformer performer) { testPerformer = std::move(performer); }
    void setRunInlineForTesting(bool inlineRun) { runInline = inlineRun; }

private:
    AuthClient makeClient() const;
    void run(std::function<void()> work, std::function<void()> deliver) const;

    AuthClient::HttpPerformer testPerformer;
    bool runInline = false;
};

} // namespace synth
