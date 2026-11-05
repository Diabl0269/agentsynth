// Guards the host-state bug kind: no test renders a graph by hand while a real audio device also renders it.
//
// A Standalone AudioEngine that calls initialise() opens the real audio device and becomes its callback, so a test
// that then calls graph.processBlock / prepareToPlay or renderNextBlock itself has two threads rendering one graph.
// Such a file must call setAudioDeviceDisabled(true), use only Hosted engines, or carry the marker
// `// live-device-render-ok: <reason>`.

#include "TestPaths.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <regex>

namespace {

bool matches(const std::string& text, const char* pattern) { return std::regex_search(text, std::regex(pattern)); }

} // namespace

TEST(TestsHygiene, NoTestRendersTheGraphByHandWhileTheRealDeviceRendersIt) {
    const juce::File root(TESTS_ROOT_DIR);
    ASSERT_TRUE(root.isDirectory());

    int scanned = 0;
    std::string offenders;
    for (const auto& entry : root.findChildFiles(juce::File::findFiles, true, "*.cpp")) {
        if (entry.getFileName() == "LiveDeviceRenderGuardTests.cpp")
            continue;
        const std::string raw = entry.loadFileAsString().toStdString();
        const std::string text = std::regex_replace(raw, std::regex(R"(//[^\n]*)"), ""); // comments never run
        ++scanned;
        const bool initialises = matches(text, R"(\.initialise\(\))");
        const bool rendersByHand =
            matches(text, R"((getGraph\(\)|\bgraph)\.(processBlock|prepareToPlay)\(|\.renderNextBlock\()");
        if (!initialises || !rendersByHand)
            continue;
        // A Standalone engine is one built with no mode, or with HostMode::Standalone.
        const bool standalone = matches(text, R"(HostMode::Standalone|\bAudioEngine\s+\w+\s*(;|\{\s*\}|\(\s*\)))");
        if (!standalone)
            continue;
        if (text.find("setAudioDeviceDisabled(true)") != std::string::npos ||
            raw.find("// live-device-render-ok:") != std::string::npos)
            continue;
        offenders += "\n  " + entry.getRelativePathFrom(root).toStdString();
    }
    EXPECT_GT(scanned, 100) << "the scan found almost no test files";
    EXPECT_TRUE(offenders.empty())
        << "These files initialise() a Standalone engine (opening the real audio device) and also render its graph "
           "by hand. Call engine.setAudioDeviceDisabled(true) before initialise(), or add "
           "`// live-device-render-ok: <reason>`:"
        << offenders;
}
