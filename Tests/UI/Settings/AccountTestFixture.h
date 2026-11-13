// Shared by the Account tab, its flow panel and the leaving-question tests: a fake auth/billing server behind
// AuthClient::HttpPerformer, a signed-in AccountService on it, and a real-mouse button click.
#pragma once

#include "AI/AccountService.h"
#include "Auth/InMemoryTokenStore.h"
#include <chrono>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <mutex>
#include <vector>

namespace account_test {

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds{10000}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

struct RecordedRequest {
    juce::String method;
    juce::String url;
    juce::String body;
};

// Answers sign-in, /me, /entitlement, revoke, DELETE /v1/account and POST /v1/exit-survey; records every request.
class FakeAccountServer {
public:
    juce::String plan = "pro";
    juce::String email = "jane@example.com";
    juce::String periodEnd = "2026-11-18T12:00:00Z";
    bool cancelAtPeriodEnd = false;
    int requestsUsed = 42;
    int monthlyLimit = 500;

    int deleteStatus = 204;
    juce::String deleteBody;
    bool deleteTransportDown = false;
    int surveyStatus = 201;

    synth::AuthClient::HttpPerformer performer() {
        return [this](const juce::String& method, const juce::String& url, const juce::StringPairArray&,
                      const juce::String& body, int,
                      const std::atomic<bool>&) -> synth::AuthClient::HttpResult { return handle(method, url, body); };
    }

    std::vector<RecordedRequest> requests() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return log_;
    }

    int count(const juce::String& method, const juce::String& urlSuffix) const {
        int n = 0;
        for (const auto& r : requests())
            if (r.method == method && r.url.endsWith(urlSuffix))
                ++n;
        return n;
    }

    // Index of the first matching request in arrival order, or -1.
    int indexOf(const juce::String& method, const juce::String& urlSuffix) const {
        const auto all = requests();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].method == method && all[i].url.endsWith(urlSuffix))
                return (int)i;
        return -1;
    }

    juce::String bodyOf(const juce::String& method, const juce::String& urlSuffix) const {
        for (const auto& r : requests())
            if (r.method == method && r.url.endsWith(urlSuffix))
                return r.body;
        return {};
    }

private:
    static synth::AuthClient::HttpResult reply(int status, const juce::String& body) {
        synth::AuthClient::HttpResult r;
        r.httpStatus = status;
        r.body = body;
        return r;
    }

    synth::AuthClient::HttpResult handle(const juce::String& method, const juce::String& url,
                                         const juce::String& body) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            log_.push_back({method, url, body});
        }
        if (url.endsWith("/v1/auth/token")) {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty("access_token", "at1");
            o->setProperty("token_type", "Bearer");
            o->setProperty("expires_in", 3600);
            o->setProperty("refresh_token", "rt1");
            return reply(200, juce::JSON::toString(juce::var(o.get())));
        }
        if (url.endsWith("/v1/auth/me")) {
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty("id", "user-1");
            o->setProperty("email", email);
            return reply(200, juce::JSON::toString(juce::var(o.get())));
        }
        if (url.endsWith("/v1/entitlement")) {
            juce::DynamicObject::Ptr usage = new juce::DynamicObject();
            usage->setProperty("requests_used", requestsUsed);
            juce::DynamicObject::Ptr limits = new juce::DynamicObject();
            limits->setProperty("monthly_requests", monthlyLimit);
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty("plan", plan);
            o->setProperty("status", "active");
            o->setProperty("period_end", periodEnd.isEmpty() ? juce::var() : juce::var(periodEnd));
            o->setProperty("cancel_at_period_end", cancelAtPeriodEnd);
            o->setProperty("limits", juce::var(limits.get()));
            o->setProperty("usage", juce::var(usage.get()));
            return reply(200, juce::JSON::toString(juce::var(o.get())));
        }
        if (url.endsWith("/v1/auth/revoke"))
            return reply(200, "");
        if (url.endsWith("/v1/account") && method == "DELETE") {
            if (deleteTransportDown) {
                synth::AuthClient::HttpResult r;
                r.transportFailed = true;
                r.errorMessage = "offline";
                return r;
            }
            return reply(deleteStatus, deleteBody);
        }
        if (url.endsWith("/v1/exit-survey"))
            return reply(surveyStatus, "");
        return reply(404, "");
    }

    mutable std::mutex mutex_;
    std::vector<RecordedRequest> log_;
};

inline std::unique_ptr<synth::AccountService> makeSignedInService(FakeAccountServer& server,
                                                                  synth::InMemoryTokenStore** storeOut = nullptr) {
    auto store = std::make_unique<synth::InMemoryTokenStore>();
    store->save("stored-refresh-token");
    if (storeOut != nullptr)
        *storeOut = store.get();
    auto service =
        std::make_unique<synth::AccountService>("http://mock-host:8787", server.performer(), std::move(store));
    service->attemptSilentSignIn();
    EXPECT_TRUE(waitUntil([&] { return service->getSnapshot().entitlementKnown; }));
    return service;
}

// A press and release delivered to the button's own mouse callbacks (docs/development/test-patterns.md, "Test the
// real mouse path"): returns whether the button's onClick ran.
inline bool realClick(juce::Button& button) {
    bool clicked = false;
    juce::Component::SafePointer<juce::Button> alive(&button); // the click may delete the button
    auto previous = button.onClick;
    button.onClick = [&] {
        clicked = true;
        if (previous)
            previous();
    };
    const auto pos = juce::Point<float>((float)button.getWidth() * 0.5f, (float)button.getHeight() * 0.5f);
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    const auto now = juce::Time::getCurrentTime();
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    juce::Component& asComponent = button; // Button narrows the mouse callbacks to protected
    asComponent.mouseDown(
        juce::MouseEvent(source, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &button, &button, now, pos, now, 1, false));
    asComponent.mouseUp(juce::MouseEvent(source, pos, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &button,
                                         &button, now, pos, now, 1, false));
    if (alive != nullptr)
        alive->onClick = previous;
    return clicked;
}

} // namespace account_test
