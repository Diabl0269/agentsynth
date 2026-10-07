#pragma once

// AccessibilitySettingsFixture.h -- a headless SettingsWindow's dependencies (device manager,
// properties, AI service with a mock provider, shortcut manager, theme manager), shared by the
// accessibility coverage test and the dialog keyboard tests.
//
// Test-only (kept out of Source/ because nothing in the app calls it).

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

namespace synth::test {

class MockProviderACT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockACT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace synth::test

class AccessibilitySettingsTest : public ::testing::Test {
public:
    // Kept in this run's own settings folder: CI runs shards in parallel, and a file in the shared
    // Application Support folder let one shard's clear() land between another's save and reload.
    // An absolute folder makes the "all users" file the same file, so it is detached as the
    // fallback: otherwise a cleared key would still read back through it.
    static void useRunSettingsFile(juce::ApplicationProperties& props) {
        juce::PropertiesFile::Options options;
        options.applicationName = "AccessibilityCoverageTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.folderName = synth::userSettingsRootDirectory().getFullPathName();
        props.setStorageParameters(options);
        props.getUserSettings()->setFallbackPropertySet(nullptr);
    }

protected:
    void SetUp() override {
        useRunSettingsFile(appProperties);
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<synth::AIIntegrationService>(engine->getGraph());
        aiService->setProvider(std::make_unique<synth::test::MockProviderACT>());
        aiChat = std::make_unique<synth::AIChatComponent>(*aiService, appProperties);
    }

    void TearDown() override {
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<synth::AIIntegrationService> aiService;
    std::unique_ptr<synth::AIChatComponent> aiChat;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    ShortcutManager shortcutManager;
    synth::theme::ThemeManager themeManager;
};
