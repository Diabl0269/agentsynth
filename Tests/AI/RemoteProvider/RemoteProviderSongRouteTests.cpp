// Service routes outside /v1/capability/ and the streamed build: the URL and body, lines cut from chunked reads and
// delivered in order, a stream that ends early, cancellation, and the status and code a failure carries.
#include "RemoteProviderTestFixture.h"

namespace {

using Chunker = std::function<void(const char*, std::size_t)>;

synth::RemoteProvider::StreamPerformer streamingChunks(std::vector<std::string> chunks,
                                                       juce::String* capturedUrl = nullptr,
                                                       juce::String* capturedBody = nullptr) {
    return [chunks, capturedUrl, capturedBody](const juce::String& url, const juce::StringPairArray&,
                                               const juce::String& body, int, const std::atomic<bool>&,
                                               const Chunker& onBody) {
        if (capturedUrl != nullptr)
            *capturedUrl = url;
        if (capturedBody != nullptr)
            *capturedBody = body;
        for (const auto& chunk : chunks)
            onBody(chunk.data(), chunk.size());
        return synth::RemoteProvider::HttpResult{200, {}, {}, false, false, {}};
    };
}

juce::var songBody() {
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("stepsVersion", 1);
    body->setProperty("token", "abc.def");
    return juce::var(body.get());
}

} // namespace

TEST_F(RemoteProviderTest, ARouteRequestPostsToTheRouteWithProductNameAndReturnsData) {
    juce::String capturedUrl, capturedBody;
    auto performer = [&](const juce::String& url, const juce::StringPairArray&, const juce::String& body, int,
                         const std::atomic<bool>&) {
        capturedUrl = url;
        capturedBody = body;
        return makeSuccess(R"({"data":{"token":"t"}})");
    };
    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendRouteRequest("/v1/song/plan", songBody(),
                              [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });
    const auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(capturedUrl, kMockHost + "/v1/song/plan");
    EXPECT_EQ(juce::JSON::parse(capturedBody)["productName"].toString(), juce::String(synth::branding::kProductName));
    EXPECT_EQ(juce::JSON::parse(capturedBody)["token"].toString(), juce::String("abc.def"));
    EXPECT_EQ(juce::JSON::parse(result.content)["token"].toString(), juce::String("t"));
}

TEST_F(RemoteProviderTest, ARouteWithoutALeadingSlashOrAnObjectBodyFailsFastWithNoRequest) {
    int calls = 0;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) {
        ++calls;
        return makeSuccess("{}");
    };
    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback first, second;
    provider.sendRouteRequest("v1/song/plan", songBody(), [&](const synth::AIProvider::AIResponse& r) { first(r); });
    provider.sendRouteRequest("/v1/song/plan", juce::var("text"),
                              [&](const synth::AIProvider::AIResponse& r) { second(r); });
    EXPECT_EQ(first.getResult().error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_EQ(second.getResult().error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_EQ(calls, 0);
}

TEST_F(RemoteProviderTest, AStreamedRouteDeliversLinesCutAcrossReadsInOrderThenFinishesOnce) {
    juce::String url;
    synth::RemoteProvider provider{kMockHost, nullptr, "",
                                   streamingChunks({"{\"type\":\"ping\"}\n{\"type\":\"st", "ep\"}\n", "tail"}, &url)};
    provider.setTestMode(true);

    std::vector<juce::String> lines;
    int finished = 0;
    bool finishedOk = false;
    MockPromptCallback done;
    provider.streamRouteRequest(
        "/v1/song/build", songBody(), [&](const juce::String& line) { lines.push_back(line); },
        [&](const synth::AIProvider::AIResponse& r) {
            ++finished;
            finishedOk = r.success;
            done(r);
        });
    done.getResult();
    provider.stopThread(5000);

    EXPECT_EQ(url, kMockHost + "/v1/song/build");
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], juce::String(R"({"type":"ping"})"));
    EXPECT_EQ(lines[1], juce::String(R"({"type":"step"})"));
    EXPECT_EQ(lines[2], juce::String("tail")) << "a last line without a newline is still delivered";
    EXPECT_EQ(finished, 1);
    EXPECT_TRUE(finishedOk);
}

TEST_F(RemoteProviderTest, AStreamedRouteWithoutAStreamTransportStillReadsLinesFromThePlainOne) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) { return makeSuccess("{\"type\":\"ping\"}\n{\"type\":\"ping\"}\n"); };
    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    int lines = 0;
    MockPromptCallback done;
    provider.streamRouteRequest(
        "/v1/song/build", songBody(), [&](const juce::String&) { ++lines; },
        [&](const synth::AIProvider::AIResponse& r) { done(r); });
    EXPECT_TRUE(done.getResult().success);
    provider.stopThread(5000);
    EXPECT_EQ(lines, 2);
}

TEST_F(RemoteProviderTest, AFailedStreamReportsTheStatusAndTheServerCode) {
    auto streamer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                       const std::atomic<bool>&, const Chunker&) {
        return makeStatus(400, R"({"error":{"code":"PLAN_EXPIRED","message":"That plan has expired."}})");
    };
    synth::RemoteProvider provider{kMockHost, nullptr, "", streamer};
    provider.setTestMode(true);

    MockPromptCallback done;
    provider.streamRouteRequest("/v1/song/build", songBody(), nullptr,
                                [&](const synth::AIProvider::AIResponse& r) { done(r); });
    const auto result = done.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.httpStatus, 400);
    EXPECT_EQ(result.error.code, juce::String("PLAN_EXPIRED"));
    EXPECT_TRUE(result.error.message.contains("That plan has expired.")) << result.error.message;
}

TEST_F(RemoteProviderTest, AnExhaustedQuotaOnBuildIsTheQuotaKind) {
    auto streamer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                       const std::atomic<bool>&, const Chunker&) {
        return makeStatus(429, R"({"error":{"code":"QUOTA_EXCEEDED","message":"You have used all your requests."}})");
    };
    synth::RemoteProvider provider{kMockHost, nullptr, "", streamer};
    provider.setTestMode(true);

    MockPromptCallback done;
    provider.streamRouteRequest("/v1/song/build", songBody(), nullptr,
                                [&](const synth::AIProvider::AIResponse& r) { done(r); });
    const auto result = done.getResult();
    provider.stopThread(5000);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Quota);
    EXPECT_EQ(result.error.code, juce::String("QUOTA_EXCEEDED"));
}

TEST_F(RemoteProviderTest, APlanRequestToAServerWithoutTheRouteCarriesThe404) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) { return makeStatus(404, "{}"); };
    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendRouteRequest("/v1/song/plan", songBody(),
                              [&](const synth::AIProvider::AIResponse& r) { callback(r); });
    const auto result = callback.getResult();
    provider.stopThread(5000);
    EXPECT_EQ(result.error.httpStatus, 404);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
}

TEST_F(RemoteProviderTest, CancellingAStreamStopsLinesAndFinishesWithCancelled) {
    std::atomic<bool> inStream{false};
    auto streamer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>& cancelled, const Chunker& onBody) {
        const std::string first = "{\"type\":\"ping\"}\n";
        onBody(first.data(), first.size());
        inStream = true;
        const auto until = juce::Time::getMillisecondCounter() + 5000;
        while (!cancelled.load() && juce::Time::getMillisecondCounter() < until)
            juce::Thread::sleep(2);
        synth::RemoteProvider::HttpResult aborted;
        aborted.transportFailed = true;
        return aborted;
    };
    synth::RemoteProvider provider{kMockHost, nullptr, "", streamer};
    provider.setTestMode(true);

    std::atomic<int> lines{0};
    MockPromptCallback done;
    const auto id = provider.streamRouteRequest(
        "/v1/song/build", songBody(), [&](const juce::String&) { ++lines; },
        [&](const synth::AIProvider::AIResponse& r) { done(r); });
    while (!inStream.load())
        juce::Thread::sleep(2);
    provider.cancel(id);
    const auto result = done.getResult();
    provider.stopThread(5000);

    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Cancelled);
    EXPECT_EQ(lines.load(), 1);
}
