#pragma once

// Shared test infrastructure for MainComponent*Tests.cpp: the two headless AI provider mocks
// and the MainComponentTest fixture (panel-key reset + a scratch tempRoot for save/load tests).
#include "../../FakeAudioIODevice.h"
#include "AI/AIProvider.h"
#include "AI/AIProviderRegistry.h"
#include "MainComponent/MainComponent.h"
#include "Modules/MasterModule.h"
#include "UI/Chrome/ToolbarComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class MockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Mock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response.";
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

// Mock provider that records fetchAvailableModels() calls and honours setModel()/
// getCurrentModel() so tests can verify the post-setProvider() model-selection contract.
class ModelTrackingMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "ModelTrackingMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        ++fetchCallCount;
        callback({"mock-model-a", "mock-model-b"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{true, "Mock response.", {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

    int fetchCallCount = 0;

private:
    juce::String model;
    int requestTimeoutMs = 240000;
};

class MainComponentTest : public ::testing::Test {
protected:
    // MainComponent reads/writes the panel-visibility flags from the shared "Agent Synth"
    // ApplicationProperties (same on-disk file the app uses). To keep persistence tests
    // hermetic regardless of execution order, reset those keys to their documented defaults
    // before AND after every test. We open the same PropertiesFile location MainComponent uses.
    void resetPanelKeys() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1"); // default: visible
            s->setValue("aiPanelVisible", "0");        // default: hidden
            s->setValue("minimapVisible", "1");        // default: visible
            s->removeValue("aiRequestTimeoutMs");      // default: AIChatComponent::kDefaultRequestTimeoutMs
            // Autosave defaults ON in the app, but every test that dirties a doc and drives
            // timerCallback()/runAutosaveTickForTest() shares this SAME real settings file — leaving
            // it on would make an unrelated test start writing autosave.json into its temp bundle
            // dir. Forced OFF here (not just reset to the app default) so every pre-existing test's
            // behavior is unaffected; AutosaveTests.cpp turns it back on explicitly per test.
            s->setValue("autosaveEnabled", "0");
            s->removeValue("autosaveIntervalMinutes"); // default: 2
            s->removeValue("autosaveBackupCount");     // default: 5
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetPanelKeys();
        tempRoot =
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-maincomponent-tests");
        tempRoot.deleteRecursively();
        tempRoot.createDirectory();
    }
    void TearDown() override {
        resetPanelKeys();
        tempRoot.deleteRecursively();
    }

    // Save/load round-trip tests write here — same idiom Tests/Project/ProjectBundleTests.cpp uses.
    juce::File tempRoot;
};

namespace {

// Collect the 9 toolbar DrawableButtons (direct children of MainComponent). Shared between the
// layout tests and ToolbarButtonsHaveNonZeroBoundsAfterConstruction.
static std::vector<juce::Button*> collectToolbarButtons(MainComponent& mc) {
    static const char* ids[] = {"toggleLibrary", "saveButton",        "loadButton",      "settingsButton", "undoButton",
                                "redoButton",    "autoArrangeButton", "toggleModMatrix", "toggleAiPanel"};
    std::vector<juce::Button*> out;
    for (auto* child : mc.getChildren())
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            for (const char* id : ids)
                if (btn->getComponentID() == id)
                    out.push_back(btn);
    return out;
}

} // namespace
