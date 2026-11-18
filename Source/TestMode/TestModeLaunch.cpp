// Concern: bringing the test-mode server up for an automation launch -- the announcement line and the info file.

#include "TestMode/TestModeLaunch.h"
#include "UI/Layout/ReducedMotion.h"
#include <iostream>
#if JUCE_WINDOWS
#include <process.h>
#else
#include <unistd.h>
#endif

namespace synth::testmode {

std::unique_ptr<TestModeServer> startForLaunch(const LaunchOptions& options, juce::String& error) {
    auto server = std::make_unique<TestModeServer>(CommandTable::standard());
    if (!server->start(options.port, error))
        return nullptr;

    // A test-mode window may sit behind other windows (another app's, another instance's) and must still be clickable.
    juce::ComponentPeer::ignoreWindowOrderForHitTests = true;

    if (!options.keepAnimations)
        synth::ui::setAnimationMode(synth::ui::AnimationMode::off);

#if JUCE_WINDOWS
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    if (options.infoFile != juce::File()) {
        options.infoFile.getParentDirectory().createDirectory();
        options.infoFile.replaceWithText("{\"port\":" + juce::String(server->getPort()) +
                                         ",\"pid\":" + juce::String(pid) + "}");
    }
    // std::cout and not juce::Logger: the client parses this line, and the Logger is hijacked by the AI console in
    // Debug builds.
    std::cout << "AGENTSYNTH_AUTOMATION_PORT " << server->getPort() << " " << pid << std::endl;
    std::cout.flush();
    return server;
}

} // namespace synth::testmode
