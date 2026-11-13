#pragma once

#include "TestMode/TestModeServer.h"

// TestModeLaunch.h: what `--automation <port>` does once the app window exists (docs/development/test-mode.md#flags).
namespace synth::testmode {

struct LaunchOptions {
    int port = 0;
    juce::File infoFile;         // when set, {"port":N,"pid":P} is written here
    bool keepAnimations = false; // --automation-motion
};

/** Starts the server, switches animations off (unless asked not to), prints `AGENTSYNTH_AUTOMATION_PORT <port> <pid>`
 *  on stdout and writes the info file. Null (with `error`) when the port could not be bound. */
std::unique_ptr<TestModeServer> startForLaunch(const LaunchOptions& options, juce::String& error);

} // namespace synth::testmode
