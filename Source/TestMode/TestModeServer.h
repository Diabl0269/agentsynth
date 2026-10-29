#pragma once

#include "TestMode/TestModeCommands.h"
#include <juce_events/juce_events.h>
#include <memory>

// TestModeServer.h: the local socket the test-mode client talks to (docs/development/test-mode.md#protocol).
namespace synth::testmode {

/** Listens on 127.0.0.1 only, one client at a time, newline-delimited JSON both ways. The socket thread does I/O
 *  and nothing else: every command step runs on the message thread (MessageManager::callAsync), the thread then
 *  waits for it with a deadline. Destroying the server never needs the message thread. */
class TestModeServer : private juce::Thread {
public:
    explicit TestModeServer(CommandTable commands);
    ~TestModeServer() override;

    /** Binds and starts listening; port 0 picks a free one. False (with `error`) if it could not bind. */
    bool start(int port, juce::String& error);

    /** The port actually bound, or 0 before start(). */
    int getPort() const noexcept { return boundPort_; }

private:
    void run() override;
    void serve(juce::StreamingSocket& client);
    juce::String handleLine(const juce::String& line);

    CommandTable commands_;
    std::unique_ptr<juce::StreamingSocket> listener_;
    int boundPort_ = 0;
};

} // namespace synth::testmode
