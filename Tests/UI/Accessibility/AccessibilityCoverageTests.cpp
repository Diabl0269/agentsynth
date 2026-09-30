// AccessibilityCoverageTests.cpp -- the accessibility ratchet (docs/development/accessibility.md).
//
// 1. AccessibilityAuditTest proves the auditor itself on a tiny synthetic tree.
// 2. AccessibilityCoverageTest audits each real surface (a headless MainComponent, every Settings
//    tab, the Export Audio dialog) and compares the name/tooltip gap counts with
//    AccessibilityBaseline.h: more gaps fails (a new control lacks a name or tooltip), fewer gaps
//    fails too, so the baseline is lowered in the same change that fixed them.
#include "../../TestSettingsHelpers.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AccessibilityAudit.h"
#include "AccessibilityBaseline.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace {

using synth::test::auditAccessibility;
using synth::test::countGaps;
using synth::test::Gap;

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

// Strict ratchet: equal to the baseline passes; more or fewer fails with every gap path listed.
::testing::AssertionResult matchesBaseline(const juce::String& surface, const std::vector<Gap>& gaps) {
    const int names = countGaps(gaps, Gap::Kind::MissingName);
    const int tips = countGaps(gaps, Gap::Kind::MissingTooltip);
    const auto* entry = synth::test::findAccessibilityBaseline(surface.toRawUTF8());

    juce::String listing;
    for (const auto& g : gaps)
        listing << "\n  " << (g.kind == Gap::Kind::MissingName ? "missingName    " : "missingTooltip ") << g.path;

    if (entry == nullptr)
        return ::testing::AssertionFailure()
               << "no entry for surface \"" << surface << "\" in AccessibilityBaseline.h; add {\"" << surface << "\", "
               << names << ", " << tips << "}" << listing;

    if (names > entry->maxMissingName || tips > entry->maxMissingTooltip)
        return ::testing::AssertionFailure()
               << "new accessibility gaps on " << surface << ": missingName " << names << " (baseline "
               << entry->maxMissingName << "), missingTooltip " << tips << " (baseline " << entry->maxMissingTooltip
               << "). Give the control a setTitle name and a tooltip; never raise the baseline. All gaps:" << listing;

    if (names < entry->maxMissingName)
        return ::testing::AssertionFailure()
               << "accessibility gaps dropped from " << entry->maxMissingName << " to " << names << " on " << surface
               << ": lower the entry in AccessibilityBaseline.h";
    if (tips < entry->maxMissingTooltip)
        return ::testing::AssertionFailure()
               << "accessibility gaps dropped from " << entry->maxMissingTooltip << " to " << tips << " on " << surface
               << " (tooltips): lower the entry in AccessibilityBaseline.h";
    return ::testing::AssertionSuccess();
}

} // namespace

// ============================================================================
// The auditor, on a synthetic tree.
// ============================================================================

TEST(AccessibilityAuditTest, ButtonWithNoTextOrTitleIsMissingName) {
    juce::Component root;
    juce::TextButton button;
    button.setTooltip("Does a thing");
    root.addAndMakeVisible(button);

    const auto gaps = auditAccessibility(root);
    EXPECT_EQ(countGaps(gaps, Gap::Kind::MissingName), 1);
    EXPECT_EQ(countGaps(gaps, Gap::Kind::MissingTooltip), 0);
}

TEST(AccessibilityAuditTest, ButtonWithNoTooltipIsMissingTooltip) {
    juce::Component root;
    juce::TextButton button("Go");
    root.addAndMakeVisible(button);

    const auto gaps = auditAccessibility(root);
    EXPECT_EQ(countGaps(gaps, Gap::Kind::MissingName), 0);
    EXPECT_EQ(countGaps(gaps, Gap::Kind::MissingTooltip), 1);
}

TEST(AccessibilityAuditTest, TitledButtonWithTooltipCountsNothing) {
    juce::Component root;
    juce::TextButton button;
    button.setTitle("Play");
    button.setTooltip("Start playback (Space)");
    root.addAndMakeVisible(button);

    EXPECT_TRUE(auditAccessibility(root).empty());
}

TEST(AccessibilityAuditTest, InvisibleChildAndItsDescendantsAreSkipped) {
    juce::Component root, hiddenPanel;
    juce::TextButton inside;
    hiddenPanel.addAndMakeVisible(inside);
    root.addChildComponent(hiddenPanel); // not visible

    EXPECT_TRUE(auditAccessibility(root).empty());
}

TEST(AccessibilityAuditTest, NonAccessibleComponentIsSkipped) {
    juce::Component root;
    juce::TextButton button;
    button.setAccessible(false);
    root.addAndMakeVisible(button);

    EXPECT_TRUE(auditAccessibility(root).empty());
}

TEST(AccessibilityAuditTest, SliderInsideTooltippedWrapperNeedsNoTooltipOfItsOwn) {
    struct Wrapper
        : juce::Component
        , juce::SettableTooltipClient {};
    Wrapper wrapper;
    juce::Slider slider;
    slider.setTitle("Level");
    wrapper.setTooltip("Output level");
    wrapper.addAndMakeVisible(slider);
    juce::Component root;
    root.addAndMakeVisible(wrapper);

    EXPECT_TRUE(auditAccessibility(root).empty());
}

TEST(AccessibilityAuditTest, TextEditorNeedsNoTooltipAndIsNamedByItsDescription) {
    juce::Component root;
    juce::TextEditor editor;
    editor.setTitle("Search");
    root.addAndMakeVisible(editor);

    EXPECT_TRUE(auditAccessibility(root).empty());
}

// ============================================================================
// The real surfaces against the baseline.
// ============================================================================

TEST(AccessibilityCoverageTest, MainComponentDefaultProject) {
    MainComponent mc(std::make_unique<MockProviderACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    EXPECT_TRUE(matchesBaseline("MainComponent", auditAccessibility(mc)));
}

TEST(AccessibilityCoverageTest, ExportAudioDialog) {
    synth::ui::ExportAudioDialog dialog(16.0, false, 0.0, 0.0, 120.0, true,
                                        juce::File::getSpecialLocation(juce::File::tempDirectory), "Default");
    dialog.setSize(dialog.getWidth() > 0 ? dialog.getWidth() : 480, dialog.getHeight() > 0 ? dialog.getHeight() : 400);
    EXPECT_TRUE(matchesBaseline("ExportAudioDialog", auditAccessibility(dialog)));
}

class AccessibilitySettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "AccessibilityCoverageTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<synth::AIIntegrationService>(engine->getGraph());
        aiService->setProvider(std::make_unique<MockProviderACT>());
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

TEST_F(AccessibilitySettingsTest, EveryTabOfTheSettingsWindow) {
    SettingsWindow window(deviceManager, appProperties, *aiService, *aiChat, shortcutManager, themeManager, nullptr);
    window.setSize(800, 600);
    ASSERT_GT(window.getNumTabs(), 0);
    for (int i = 0; i < window.getNumTabs(); ++i) {
        window.getTabs().setCurrentTabIndex(i);
        auto* content = window.getTabs().getTabContentComponent(i);
        ASSERT_NE(content, nullptr);
        EXPECT_TRUE(matchesBaseline("Settings/" + window.getTabName(i), auditAccessibility(*content)));
    }
}
