#include "Telemetry/TelemetrySender.h"
#include "TelemetryTestFixture.h"

using namespace synth::telemetry;
using synth::telemetry::test::TelemetryTestFixture;

namespace {

struct Call {
    juce::String method;
    juce::String url;
    juce::StringPairArray headers;
    juce::String body;
};

class TelemetrySenderTest : public TelemetryTestFixture {
protected:
    void SetUp() override {
        TelemetryTestFixture::SetUp();
        recorder = makeRecorderPtr();
        sender = makeSender();
    }

    std::unique_ptr<TelemetrySender> makeSender() {
        auto performer = [this](const juce::String& method, const juce::String& url,
                                const juce::StringPairArray& headers, const juce::String& body, int,
                                const std::atomic<bool>&) {
            calls.push_back({method, url, headers, body});
            synth::AuthClient::HttpResult result;
            result.httpStatus = status;
            result.transportFailed = transportFails;
            return result;
        };
        auto inlineRun = [](TelemetrySender::Work work) { work(); };
        return std::make_unique<TelemetrySender>(performer, "https://api.example.test", inlineRun, inlineRun);
    }

    // Two past days and today in the queue.
    void queueTwoPastDaysAndToday() {
        recorder->setEnabled(true);
        for (int day : {5, 6, 7}) {
            setDay(2026, 10, day);
            recorder->noteSessionStart();
            recorder->countModuleAdded("Oscillator");
        }
    }

    std::unique_ptr<TelemetryRecorder> recorder;
    std::unique_ptr<TelemetrySender> sender;
    std::vector<Call> calls;
    int status = 204;
    bool transportFails = false;
};

} // namespace

TEST_F(TelemetrySenderTest, SendsOnlyDaysBeforeToday) {
    queueTwoPastDaysAndToday();
    sender->sendPending(*recorder);
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_TRUE(calls[0].body.contains("\"day\":\"2026-10-05\""));
    EXPECT_TRUE(calls[1].body.contains("\"day\":\"2026-10-06\""));
    EXPECT_EQ(recorder->getToday()->day, "2026-10-07");
}

TEST_F(TelemetrySenderTest, PostsToTheDailyEndpointWithOnlyAContentType) {
    queueTwoPastDaysAndToday();
    sender->sendPending(*recorder);
    ASSERT_FALSE(calls.empty());
    for (const auto& call : calls) {
        EXPECT_EQ(call.method, "POST");
        EXPECT_EQ(call.url, "https://api.example.test/v1/telemetry/daily");
        EXPECT_EQ(call.headers.size(), 1);
        EXPECT_EQ(call.headers["Content-Type"], "application/json");
        EXPECT_FALSE(call.headers.containsKey("Authorization"));
        EXPECT_FALSE(call.headers.containsKey("X-Device-Id"));
        EXPECT_TRUE(call.body.contains(recorder->getTelemetryId()));
    }
}

TEST_F(TelemetrySenderTest, AcceptedAndRefusedDaysLeaveTheQueue) {
    for (int answer : {204, 200, 400, 422}) {
        calls.clear();
        recorder->purge();
        queueTwoPastDaysAndToday();
        status = answer;
        sender->sendPending(*recorder);
        EXPECT_EQ(calls.size(), 2u) << answer;
        EXPECT_TRUE(recorder->getQueue().empty()) << answer;
    }
}

TEST_F(TelemetrySenderTest, RateLimitedServerErrorAndNetworkFailureKeepTheDays) {
    for (int answer : {429, 500, 503}) {
        calls.clear();
        recorder->purge();
        queueTwoPastDaysAndToday();
        status = answer;
        sender->sendPending(*recorder);
        EXPECT_EQ(recorder->getQueue().size(), 2u) << answer;
    }
    recorder->purge();
    queueTwoPastDaysAndToday();
    status = 204;
    transportFails = true;
    sender->sendPending(*recorder);
    EXPECT_EQ(recorder->getQueue().size(), 2u);
}

TEST_F(TelemetrySenderTest, NothingIsSentWhileDisabled) {
    queueTwoPastDaysAndToday();
    recorder->setEnabled(false);
    sender->sendPending(*recorder);
    EXPECT_TRUE(calls.empty());
}

TEST_F(TelemetrySenderTest, NothingIsSentWhenThereAreNoPastDays) {
    recorder->setEnabled(true);
    recorder->noteSessionStart();
    sender->sendPending(*recorder);
    EXPECT_TRUE(calls.empty());
}

TEST_F(TelemetrySenderTest, OptingOutDuringASendKeepsTheQueueGone) {
    queueTwoPastDaysAndToday();
    // The user switches the toggle off while the first request is still in flight.
    int requests = 0;
    auto performer = [this, &requests](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                       const juce::String&, int, const std::atomic<bool>&) {
        ++requests;
        recorder->setEnabled(false);
        synth::AuthClient::HttpResult result;
        result.httpStatus = 204;
        return result;
    };
    auto inlineRun = [](TelemetrySender::Work work) { work(); };
    TelemetrySender optOutSender(performer, "https://api.example.test", inlineRun, inlineRun);
    optOutSender.sendPending(*recorder);
    EXPECT_EQ(requests, 1); // the second day's request never goes out
    EXPECT_FALSE(recorder->isEnabled());
    EXPECT_TRUE(recorder->getQueue().empty());
    EXPECT_FALSE(queueFile().existsAsFile());
    EXPECT_FALSE(idFile().existsAsFile());
}

TEST_F(TelemetrySenderTest, ResultsPostedAfterTheSenderIsGoneDoNothing) {
    queueTwoPastDaysAndToday();
    std::vector<TelemetrySender::Work> posted;
    {
        auto performer = [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&,
                            int, const std::atomic<bool>&) {
            synth::AuthClient::HttpResult result;
            result.httpStatus = 204;
            return result;
        };
        TelemetrySender deferred(
            performer, "https://api.example.test", [](TelemetrySender::Work work) { work(); },
            [&posted](TelemetrySender::Work work) { posted.push_back(std::move(work)); });
        deferred.sendPending(*recorder);
    }
    for (auto& work : posted)
        work();
    EXPECT_EQ(recorder->getQueue().size(), 2u);
}
