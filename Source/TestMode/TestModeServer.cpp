// Concern: the test-mode socket thread -- accepting a client, framing lines, and handing each command step to the
// message thread. No component is touched here.

#include "TestMode/TestModeServer.h"
#include "TestMode/TestModeProtocol.h"

namespace synth::testmode {

namespace {

constexpr int kPollMs = 50;      // how often the socket thread re-checks that it should still be running
constexpr int kStepPauseMs = 15; // between the steps of a command that answered `pending`
constexpr size_t kMaxLineBytes = 4 * 1024 * 1024;

// One command step in flight. Shared with the message-thread callback, which may run after the waiting thread has
// given up (or after the server is gone), so it owns everything it touches.
struct Hop {
    CommandStep step;
    juce::var args;
    std::shared_ptr<Scratch> scratch;
    int attempt = 0;
    StepResult result;
    juce::WaitableEvent finished;
    std::atomic<bool> cancelled{false};
};

} // namespace

TestModeServer::TestModeServer(CommandTable commands)
    : juce::Thread("TestModeServer")
    , commands_(std::move(commands)) {}

TestModeServer::~TestModeServer() {
    signalThreadShouldExit();
    stopThread(3000);
    listener_.reset();
}

bool TestModeServer::start(int port, juce::String& error) {
    auto socket = std::make_unique<juce::StreamingSocket>();
    if (!socket->createListener(port, "127.0.0.1")) {
        error = "could not listen on 127.0.0.1:" + juce::String(port);
        return false;
    }
    boundPort_ = socket->getBoundPort();
    listener_ = std::move(socket);
    startThread();
    return true;
}

void TestModeServer::run() {
    while (!threadShouldExit()) {
        if (listener_->waitUntilReady(true, kPollMs) <= 0)
            continue;
        std::unique_ptr<juce::StreamingSocket> client(listener_->waitForNextConnection());
        if (client != nullptr)
            serve(*client);
    }
}

void TestModeServer::serve(juce::StreamingSocket& client) {
    std::string pending;
    char buffer[4096];
    while (!threadShouldExit()) {
        const int ready = client.waitUntilReady(true, kPollMs);
        if (ready < 0)
            return;
        if (ready == 0)
            continue;
        const int got = client.read(buffer, sizeof(buffer), false);
        if (got <= 0)
            return; // the client closed
        pending.append(buffer, static_cast<size_t>(got));
        if (pending.size() > kMaxLineBytes && pending.find('\n') == std::string::npos) {
            const auto reply = formatError({}, "request line too long") + "\n";
            client.write(reply.toRawUTF8(), static_cast<int>(reply.getNumBytesAsUTF8()));
            return;
        }
        size_t newline = 0;
        while ((newline = pending.find('\n')) != std::string::npos && !threadShouldExit()) {
            const auto line = juce::String::fromUTF8(pending.data(), static_cast<int>(newline)).trim();
            pending.erase(0, newline + 1);
            if (line.isEmpty())
                continue;
            const auto reply = handleLine(line) + "\n";
            const char* data = reply.toRawUTF8();
            int remaining = static_cast<int>(reply.getNumBytesAsUTF8());
            while (remaining > 0 && !threadShouldExit()) {
                const int written = client.write(data, remaining);
                if (written <= 0)
                    return;
                data += written;
                remaining -= written;
            }
        }
    }
}

juce::String TestModeServer::handleLine(const juce::String& line) {
    const auto parsed = parseRequestLine(line);
    if (!parsed.request.has_value())
        return formatError(parsed.id, parsed.error);
    const auto& request = *parsed.request;
    const auto* spec = commands_.find(request.cmd);
    if (spec == nullptr)
        return formatError(request.id,
                           "unknown command \"" + request.cmd + "\"; known: " + commands_.names().joinIntoString(", "));

    const int timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : spec->defaultTimeoutMs;
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    const auto scratch = std::make_shared<Scratch>();
    const auto timedOut = [&] { return formatError(request.id, "timed out after " + juce::String(timeoutMs) + " ms"); };

    for (int attempt = 0;; ++attempt) {
        auto hop = std::make_shared<Hop>();
        hop->step = spec->step;
        hop->args = request.args;
        hop->scratch = scratch;
        hop->attempt = attempt;
        juce::MessageManager::callAsync([hop] {
            if (!hop->cancelled.load())
                hop->result = hop->step(hop->args, *hop->scratch, hop->attempt);
            else
                hop->result = StepResult::failed("cancelled");
            hop->finished.signal();
        });
        while (!hop->finished.wait(kPollMs)) {
            if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline) {
                hop->cancelled = true;
                return threadShouldExit() ? formatError(request.id, "server is shutting down") : timedOut();
            }
        }
        switch (hop->result.state) {
        case StepResult::State::done:
            return formatOk(request.id, hop->result.value);
        case StepResult::State::failed:
            return formatError(request.id, hop->result.error);
        case StepResult::State::pending:
            break;
        }
        if (juce::Time::getMillisecondCounter() > deadline)
            return timedOut();
        wait(kStepPauseMs);
    }
}

} // namespace synth::testmode
