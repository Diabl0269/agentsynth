// The socket server: framing, replies, steps that wait, timeouts, and shutting down with a client still connected.

#include "TestMode/TestModeServer.h"
#include "TestModeRig.h"

using namespace synth::testmode;
using testmode_rig::pump;

namespace {

CommandTable testTable() {
    auto table = CommandTable::standard();
    table.add("echo", {[](const juce::var& a, Scratch&, int) { return StepResult::done(a); }});
    table.add("countToThree", {[](const juce::var&, Scratch& s, int attempt) {
                  s.vars->setProperty("last", attempt);
                  return attempt < 3 ? StepResult::pending() : StepResult::done(s.vars->getProperty("last"));
              }});
    table.add("never", {[](const juce::var&, Scratch&, int) { return StepResult::pending(); }, 60000});
    table.add("boom", {[](const juce::var&, Scratch&, int) { return StepResult::failed("it broke"); }});
    return table;
}

struct Connected {
    TestModeServer server{testTable()};
    juce::StreamingSocket client;

    Connected() {
        juce::String error;
        EXPECT_TRUE(server.start(0, error)) << error;
        EXPECT_GT(server.getPort(), 0);
        EXPECT_TRUE(client.connect("127.0.0.1", server.getPort(), 2000));
    }

    // Sends one line and pumps the message loop (where the commands run) until the reply line arrives.
    juce::var ask(const juce::String& line, int timeoutMs = 5000) {
        const auto out = line + "\n";
        client.write(out.toRawUTF8(), static_cast<int>(out.getNumBytesAsUTF8()));
        std::string reply;
        const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
        while (juce::Time::getMillisecondCounter() < until && reply.find('\n') == std::string::npos) {
            pump(5);
            if (client.waitUntilReady(true, 0) == 1) {
                char buf[4096];
                const int n = client.read(buf, sizeof(buf), false);
                if (n <= 0)
                    break;
                reply.append(buf, static_cast<size_t>(n));
            }
        }
        return juce::JSON::parse(juce::String(reply).trim());
    }
};

} // namespace

TEST(TestModeServer, PingGetsAReplyCarryingTheRequestId) {
    Connected c;
    const auto reply = c.ask(R"({"id":11,"cmd":"ping"})");
    EXPECT_TRUE(static_cast<bool>(reply["ok"]));
    EXPECT_EQ(static_cast<int>(reply["id"]), 11);
    EXPECT_GT(static_cast<int>(reply["result"]["pid"]), 0);
}

TEST(TestModeServer, MalformedLinesAndUnknownCommandsGetErrorsAndTheConnectionSurvives) {
    Connected c;
    const auto bad = c.ask("this is not json");
    EXPECT_FALSE(static_cast<bool>(bad["ok"]));
    const auto unknown = c.ask(R"({"id":2,"cmd":"frobnicate"})");
    EXPECT_FALSE(static_cast<bool>(unknown["ok"]));
    EXPECT_NE(unknown["error"].toString().indexOf("ping"), -1); // lists the known commands
    EXPECT_TRUE(static_cast<bool>(c.ask(R"({"id":3,"cmd":"ping"})")["ok"]));
}

TEST(TestModeServer, FailedStepsReportTheirError) {
    Connected c;
    const auto reply = c.ask(R"({"id":1,"cmd":"boom"})");
    EXPECT_FALSE(static_cast<bool>(reply["ok"]));
    EXPECT_EQ(reply["error"].toString(), "it broke");
}

TEST(TestModeServer, ArgsReachTheCommand) {
    Connected c;
    const auto reply = c.ask(R"({"id":1,"cmd":"echo","args":{"a":[1,2],"s":"x"}})");
    EXPECT_EQ(reply["result"]["s"].toString(), "x");
    EXPECT_EQ(reply["result"]["a"].getArray()->size(), 2);
}

TEST(TestModeServer, ACommandThatWaitsIsCalledAgainUntilItIsDone) {
    Connected c;
    const auto reply = c.ask(R"({"id":1,"cmd":"countToThree"})");
    EXPECT_TRUE(static_cast<bool>(reply["ok"]));
    EXPECT_EQ(static_cast<int>(reply["result"]), 3);
}

TEST(TestModeServer, ACommandThatNeverFinishesTimesOut) {
    Connected c;
    const auto reply = c.ask(R"({"id":1,"cmd":"never","timeoutMs":300})");
    EXPECT_FALSE(static_cast<bool>(reply["ok"]));
    EXPECT_NE(reply["error"].toString().indexOf("timed out"), -1);
    EXPECT_TRUE(static_cast<bool>(c.ask(R"({"id":2,"cmd":"ping"})")["ok"])); // still serving
}

TEST(TestModeServer, DestroyingTheServerWithAConnectedClientAndAPendingCommandIsPrompt) {
    const auto start = juce::Time::getMillisecondCounter();
    {
        Connected c;
        const std::string out = "{\"id\":1,\"cmd\":\"never\"}\n";
        c.client.write(out.data(), static_cast<int>(out.size()));
        pump(100); // the command is now in flight and waiting
    }
    EXPECT_LT(juce::Time::getMillisecondCounter() - start, 2500u);
}

TEST(TestModeServer, AcceptsALoopbackConnection) {
    TestModeServer server(testTable());
    juce::String error;
    ASSERT_TRUE(server.start(0, error));
    juce::StreamingSocket other;
    EXPECT_TRUE(other.connect("127.0.0.1", server.getPort(), 1000));
}

TEST(TestModeServer, ATakenPortIsAnErrorNotACrash) {
    TestModeServer first(testTable());
    juce::String error;
    ASSERT_TRUE(first.start(0, error));
    TestModeServer second(testTable());
    // SO_REUSEADDR lets some systems bind twice; only a refusal must come with a message.
    if (!second.start(first.getPort(), error))
        EXPECT_FALSE(error.isEmpty());
}
