// What the scan child does about its surroundings: it runs from a scratch working directory so a
// plugin's stray files never land in the launch directory, and it ends when its parent does.

#include "Plugin/Hosting/PluginScanService.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#if JUCE_MAC || JUCE_LINUX
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

TEST(PluginScanChildEnvironmentTest, ScratchDirectoryBecomesTheWorkingDirectory) {
    const auto original = juce::File::getCurrentWorkingDirectory();
    const auto restore = [&original] { original.setAsCurrentWorkingDirectory(); };

    ASSERT_TRUE(synth::enterPluginScratchDirectory());
    const auto now = juce::File::getCurrentWorkingDirectory();
    restore();

    EXPECT_EQ(now.getFullPathName(), synth::pluginScratchDirectory().getFullPathName());
    EXPECT_TRUE(now.isDirectory());
    EXPECT_NE(now.getFullPathName(), original.getFullPathName());
}

TEST(PluginScanChildEnvironmentTest, ParentGoneIsDecidedByAChangedParentPid) {
    EXPECT_FALSE(synth::isParentGone(500, 500));
    EXPECT_TRUE(synth::isParentGone(500, 1));
    EXPECT_TRUE(synth::isParentGone(500, 501));
}

#if JUCE_MAC || JUCE_LINUX
// Forks parent A, which forks child B and exits once B's watchdog is running. B must then end
// itself: the pipe B holds open reaches EOF only when B is gone.
TEST(PluginScanChildEnvironmentTest, WatchdogEndsTheChildWhenItsParentExits) {
    int ready[2];
    int life[2];
    ASSERT_EQ(pipe(ready), 0);
    ASSERT_EQ(pipe(life), 0);

    const pid_t parent = fork();
    ASSERT_NE(parent, -1);
    if (parent == 0) {
        close(life[0]);
        const pid_t child = fork();
        if (child == 0) {
            close(ready[0]);
            synth::startParentWatchdog(20);
            const char byte = 1;
            if (write(ready[1], &byte, 1) != 1)
                _exit(2);
            sleep(60); // only the watchdog can end this early
            _exit(3);
        }
        close(life[1]);
        close(ready[1]);
        char byte = 0;
        if (read(ready[0], &byte, 1) != 1)
            _exit(4);
        _exit(0);
    }

    close(life[1]);
    close(ready[0]);
    close(ready[1]);
    int status = 0;
    ASSERT_EQ(waitpid(parent, &status, 0), parent);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    pollfd fd{life[0], POLLIN, 0};
    const int polled = poll(&fd, 1, 5000);
    close(life[0]);
    EXPECT_EQ(polled, 1) << "the orphaned child was still running 5 s after its parent exited";
}
#endif
