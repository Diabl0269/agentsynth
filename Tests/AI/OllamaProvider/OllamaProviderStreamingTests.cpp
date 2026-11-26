// Edit-plan requests stream, so a model that starts writing only blank space is cut off early
// instead of running to the token cap.
#include "OllamaProviderTestFixture.h"

namespace {

// Serves a fixed NDJSON body in small reads and records whether it was aborted.
class RecordingChunkStream
    : public juce::InputStream
    , public synth::CancellableStream {
public:
    RecordingChunkStream(juce::String body, std::shared_ptr<std::atomic<bool>> abortedFlag)
        : data(std::move(body))
        , aborted(std::move(abortedFlag)) {}

    juce::int64 getTotalLength() override { return static_cast<juce::int64>(data.getNumBytesAsUTF8()); }
    juce::int64 getPosition() override { return position; }
    bool setPosition(juce::int64) override { return false; }
    bool isExhausted() override { return position >= getTotalLength(); }

    int read(void* dest, int maxBytes) override {
        const auto remaining = static_cast<int>(getTotalLength() - position);
        const auto count = juce::jmin(maxBytes, 64, remaining);
        if (count <= 0)
            return 0;
        std::memcpy(dest, data.toRawUTF8() + position, static_cast<size_t>(count));
        position += count;
        return count;
    }

    void cancelRead() override { aborted->store(true); }

private:
    juce::String data;
    juce::int64 position = 0;
    std::shared_ptr<std::atomic<bool>> aborted;
};

juce::var editPlanSchema() {
    auto* properties = new juce::DynamicObject();
    properties->setProperty("song", juce::var(new juce::DynamicObject()));
    auto* schema = new juce::DynamicObject();
    schema->setProperty("properties", juce::var(properties));
    return juce::var(schema);
}

juce::String chunk(const juce::String& content) {
    auto* message = new juce::DynamicObject();
    message->setProperty("role", "assistant");
    message->setProperty("content", content);
    auto* object = new juce::DynamicObject();
    object->setProperty("message", juce::var(message));
    object->setProperty("done", false);
    return juce::JSON::toString(juce::var(object), true) + "\n";
}

const juce::String kFinalChunk =
    R"({"message":{"role":"assistant","content":""},"done":true,"prompt_eval_count":12,"eval_count":34})"
    "\n";

struct StreamingRun {
    synth::AIProvider::AIResponse response;
    bool aborted = false;
    juce::String requestBody;
};

StreamingRun runEditPlan(const juce::String& ndjson) {
    auto aborted = std::make_shared<std::atomic<bool>>(false);
    auto body = std::make_shared<juce::String>();
    synth::OllamaProvider provider{
        "http://mock-host:11434",
        [=](const juce::URL& url, const juce::URL::InputStreamOptions&,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
            *body = url.getPostData();
            auto stream = std::make_unique<RecordingChunkStream>(ndjson, aborted);
            publish(stream.get());
            return stream;
        }};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "make a track"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); },
        editPlanSchema());
    StreamingRun run;
    run.response = callback.getResult();
    provider.stopThread(5000);
    run.aborted = aborted->load();
    run.requestBody = *body;
    return run;
}

} // namespace

TEST(OllamaProviderStreaming, EditPlanRequestAsksForAStreamAndOtherRequestsDoNot) {
    const auto run = runEditPlan(chunk("{}") + kFinalChunk);
    EXPECT_TRUE(static_cast<bool>(juce::JSON::parse(run.requestBody).getProperty("stream", false)));

    juce::String plainBody;
    synth::OllamaProvider plain{
        "http://mock-host:11434",
        [&](const juce::URL& url, const juce::URL::InputStreamOptions&,
            const synth::OllamaProvider::StreamPublisher&) -> std::unique_ptr<juce::InputStream> {
            plainBody = url.getPostData();
            return std::make_unique<MockInputStream>(R"({"message":{"role":"assistant","content":"ok"}})", false);
        }};
    plain.setTestMode(true);
    plain.setModel("mock-model:latest");
    MockPromptCallback callback;
    plain.sendPrompt({{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });
    callback.getResult();
    plain.stopThread(5000);
    EXPECT_FALSE(static_cast<bool>(juce::JSON::parse(plainBody).getProperty("stream", true)));
}

TEST(OllamaProviderStreaming, ChunksAddUpToTheAnswerAndTheFinalChunkCarriesTheCounts) {
    const auto run = runEditPlan(chunk("{\"song\":") + chunk("\"x\"}") + kFinalChunk);
    ASSERT_TRUE(run.response.success);
    EXPECT_EQ(run.response.content, "{\"song\":\"x\"}");
    EXPECT_EQ(run.response.usage.promptTokens, 12);
    EXPECT_EQ(run.response.usage.answerTokens, 34);
    EXPECT_FALSE(run.aborted);
}

TEST(OllamaProviderStreaming, ABlankRunawayEndsEarlyWithASchemaErrorAndAbortsTheStream) {
    juce::String ndjson = chunk("{\"song\":\"x\"}");
    for (int i = 0; i < 400; ++i)
        ndjson += chunk("\n");
    ndjson += kFinalChunk;

    const auto run = runEditPlan(ndjson);
    EXPECT_FALSE(run.response.success);
    EXPECT_EQ(run.response.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_TRUE(run.aborted) << "the stream must be closed, not read to the token cap";
}

TEST(OllamaProviderStreaming, BlankSpaceInsideAPlanBelowTheLimitIsKept) {
    const auto run = runEditPlan(chunk("{\"song\":") + chunk(juce::String::repeatedString(" ", 150)) + chunk("\"x\"}") +
                                 kFinalChunk);
    ASSERT_TRUE(run.response.success);
    EXPECT_FALSE(run.aborted);
}

TEST(OllamaProviderStreaming, AReplyWithNoMessageIsStillAnUnparseableSchemaError) {
    const auto run = runEditPlan("not json at all");
    EXPECT_FALSE(run.response.success);
    EXPECT_EQ(run.response.error.kind, synth::AIProvider::AIErrorKind::Schema);
}
