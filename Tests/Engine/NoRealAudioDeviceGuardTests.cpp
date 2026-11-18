// Guards the audible-test bug kind: the Tests binary never opens a real audio output device.
//
// Tests/TestMain.cpp turns the real-device switch off, so every engine's device manager holds only a type with no
// devices. A test that plays with the metronome on would otherwise click out loud on a developer Mac (CI has no device,
// which hid it). A test that builds its own bare juce::AudioDeviceManager and opens it must give it a device type of
// its own or carry the marker `// real-audio-device-ok: <reason>`.

// Regression test for FRO2034: the suite opened the machine's default output on every Standalone engine and
// MainComponent.

#include "../App/MainComponent/MainComponentTestFixture.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/NoAudioDeviceType.h"
#include "TestPaths.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <regex>

namespace {

bool matches(const std::string& text, const char* pattern) { return std::regex_search(text, std::regex(pattern)); }

void expectOnlyTheNoDeviceType(AudioEngine& engine) {
    auto& manager = engine.getDeviceManager();
    EXPECT_EQ(manager.getCurrentAudioDevice(), nullptr) << "a real audio device is open";
    const auto& types = manager.getAvailableDeviceTypes();
    EXPECT_GT(types.size(), 0);
    for (auto* type : types)
        EXPECT_EQ(type->getTypeName(), synth::NoAudioDeviceType::typeName());
    EXPECT_TRUE(engine.isReceivingDeviceCallbacks());
}

class RealDevicesAllowedScope {
public:
    RealDevicesAllowedScope() { AudioEngine::setRealAudioDevicesAllowedForTest(true); }
    ~RealDevicesAllowedScope() { AudioEngine::setRealAudioDevicesAllowedForTest(false); }
};

} // namespace

TEST(NoRealAudioDeviceTest, AStandaloneEngineOpensNoDeviceAndKnowsOnlyTheNoDeviceType) {
    ASSERT_FALSE(AudioEngine::realAudioDevicesAllowed());
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    engine.initialise();
    expectOnlyTheNoDeviceType(engine);
}

using NoRealAudioDeviceMainComponentTest = MainComponentTest;

TEST_F(NoRealAudioDeviceMainComponentTest, AMainComponentOpensNoDeviceAndKnowsOnlyTheNoDeviceType) {
    auto mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
    expectOnlyTheNoDeviceType(mc->getAudioEngine());
}

TEST(NoRealAudioDeviceTest, WithTheSwitchOnAnEngineGetsNoNoDeviceTypeBeforeItInitialises) {
    RealDevicesAllowedScope allowed;
    EXPECT_TRUE(AudioEngine::realAudioDevicesAllowed());
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    for (auto* type : engine.getDeviceManager().getAvailableDeviceTypes())
        EXPECT_NE(type->getTypeName(), synth::NoAudioDeviceType::typeName());
}

TEST(NoRealAudioDeviceTest, NoTestOpensABareDeviceManagerOrTurnsTheSwitchBackOn) {
    const juce::File root(TESTS_ROOT_DIR);
    ASSERT_TRUE(root.isDirectory());

    int scanned = 0;
    std::string unguarded;
    std::string switchedOn;
    for (const auto& entry : root.findChildFiles(juce::File::findFiles, true, "*.cpp;*.h")) {
        const auto name = entry.getFileName();
        if (name == "NoRealAudioDeviceGuardTests.cpp" || name == "TestMain.cpp")
            continue;
        const std::string raw = entry.loadFileAsString().toStdString();
        const std::string text = std::regex_replace(raw, std::regex(R"(//[^\n]*)"), ""); // comments never run
        ++scanned;
        const auto relative = "\n  " + entry.getRelativePathFrom(root).toStdString();
        if (text.find("setRealAudioDevicesAllowedForTest(true)") != std::string::npos)
            switchedOn += relative;
        const bool declaresManager = matches(text, R"(juce::AudioDeviceManager\s+\w+\s*(;|\{\s*\}|\(\s*\)))");
        const bool opensIt =
            matches(text, R"(\.(initialise|initialiseWithDefaultDevices|setAudioDeviceSetup)\(\s*[^)\s])");
        if (declaresManager && opensIt && text.find("addAudioDeviceType(") == std::string::npos &&
            raw.find("// real-audio-device-ok:") == std::string::npos)
            unguarded += relative;
    }
    EXPECT_GT(scanned, 100) << "the scan found almost no test files";
    EXPECT_TRUE(unguarded.empty()) << "These files declare a bare juce::AudioDeviceManager and open it, which opens "
                                      "the real audio device. Give it a device type with addAudioDeviceType(), or add "
                                      "`// real-audio-device-ok: <reason>`:"
                                   << unguarded;
    EXPECT_TRUE(switchedOn.empty()) << "Only TestMain.cpp and the guard may turn the real-device switch on:"
                                    << switchedOn;
}
