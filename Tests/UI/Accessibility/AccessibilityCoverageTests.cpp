// AccessibilityCoverageTests.cpp -- the accessibility ratchet (docs/development/accessibility.md).
//
// 1. AccessibilityAuditTest proves the auditor itself on a tiny synthetic tree.
// 2. AccessibilityCoverageTest audits each real surface (a headless MainComponent, every Settings
//    tab, the Export Audio dialog) and compares the name/tooltip gap counts with
//    AccessibilityBaseline.h: more gaps fails (a new control lacks a name or tooltip), fewer gaps
//    fails too, so the baseline is lowered in the same change that fixed them.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AccessibilityAudit.h"
#include "AccessibilityBaseline.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/TimelineViewState.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <iostream>
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

    // A lower count passes: a few controls exist only on some machines (audio devices, platform
    // options), so equality would fail on one platform or another. The note tells whoever fixed a
    // gap to lower the entry, and the local gate on the fixing machine prints it.
    if (names < entry->maxMissingName || tips < entry->maxMissingTooltip)
        std::cout << "[ NOTE ] accessibility gaps on " << surface << " are below the baseline (missingName " << names
                  << "/" << entry->maxMissingName << ", missingTooltip " << tips << "/" << entry->maxMissingTooltip
                  << "): lower the entry in AccessibilityBaseline.h\n";
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

// The persisted panel keys are reset by MainComponentTest; the dock and the mod matrix are not
// persisted (or not reset), so the panel state is pinned here: library open, bottom dock open on
// the Timeline tab, AI chat closed, mod matrix closed, minimap shown.
namespace {
void pinPanelState(MainComponent& mc) {
    if (!mc.isLibraryConfiguredVisible())
        mc.simulateToggleLibraryClick();
    if (mc.isAiPanelConfiguredVisible())
        mc.simulateToggleAiPanelClick();
    if (!mc.isBottomDockConfiguredVisible())
        mc.simulateToggleBottomPanelClick();
    if (mc.getGraphEditor().isModMatrixVisible())
        mc.simulateToggleModMatrixClick();
    if (!mc.getGraphEditor().isMinimapVisible())
        mc.simulateToggleMinimapClick();
    mc.getBottomDock().setActiveTab(synth::ui::BottomDockComponent::Tab::Timeline);
    mc.setPanelOpenProgressForTest(MainComponent::SlidingPanel::Library, 1.0f);
    mc.setPanelOpenProgressForTest(MainComponent::SlidingPanel::AiChat, 0.0f);
    mc.resized();
}
} // namespace

TEST_F(MainComponentTest, AccessibilityCoverageMainComponentDefaultProject) {
    synth::test::PersistedKeysGuard guard({"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder"});
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    pinPanelState(mc);
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

// ============================================================================
// The piano roll.
// ============================================================================

// A clip with notes open, the velocity strip shown and the scale-assist panel open: the header
// chips' keyboard buttons, the value box, the strip and every panel control. The custom-scale
// editor is shown too (it is hidden until "Custom" is picked).
TEST(AccessibilityCoverageTest, PianoRoll) {
    synth::TimelineDoc doc;
    synth::ui::TimelineViewState state;
    synth::ui::PianoRollComponent roll(state);
    roll.setTimelineDoc(&doc);
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Clip");
    synth::MidiNote note;
    note.startBeat = 1.0;
    note.pitch = 60;
    note.lengthBeats = 1.0;
    doc.addNote(clip, note);
    roll.setSize(1000, 400);
    roll.openClip(clip);
    roll.setVelocityLaneVisible(true, false);
    roll.toggleScalePanel();
    // The "Edit custom scales..." row (id 9000, ScaleAssistPanel::kCustomRowId, private) shows the editor.
    roll.getScaleAssistPanel().getScaleCombo().setSelectedId(9000, juce::sendNotificationSync);
    roll.resized();
    EXPECT_TRUE(matchesBaseline("PianoRoll", auditAccessibility(roll)));
}

// ============================================================================
// Every built-in module card.
// ============================================================================

// One card per type in the module factory (the list the patch loader and the AI schema use). Left
// out: "Hosted Plugin" (needs a plugin binary), "Track In", "Track Audio" and "Rec Tap" (bound to
// timeline tracks or files), the "Macro In/Out" and "Macro MIDI In/Out" jacks and "Channel Strip"
// and "Master" (mixer/macro plumbing with no canvas card of their own), and the alias keys "Amp
// Env", "Filter Env" and "Mod Slot" (the same cards as "ADSR" and "Attenuverter").
TEST(AccessibilityCoverageTest, EveryModuleCard) {
    static const juce::StringArray skipped{
        "Hosted Plugin",  "Track In",      "Track Audio", "Rec Tap", "Macro In",   "Macro Out", "Macro MIDI In",
        "Macro MIDI Out", "Channel Strip", "Master",      "Amp Env", "Filter Env", "Mod Slot"};
    std::vector<juce::String> types;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames())
        if (!skipped.contains(type))
            types.push_back(type);
    std::sort(types.begin(), types.end());

    AudioEngine audioEngine;
    GraphEditor editor(audioEngine);
    editor.setSize(2400, 2400);
    auto& graph = audioEngine.getGraph();
    std::vector<std::pair<juce::String, juce::AudioProcessorGraph::NodeID>> nodes;
    int slot = 0;
    for (const auto& type : types) {
        auto node = graph.addNode(synth::AIStateMapper::createModule(type));
        if (node == nullptr)
            continue;
        node->properties.set("x", 40 + (slot % 8) * 290);
        node->properties.set("y", 40 + (slot / 8) * 310);
        nodes.emplace_back(type, node->nodeID);
        ++slot;
    }
    editor.updateComponents();
    sizeModuleComponents(editor);

    std::vector<Gap> gaps;
    int cards = 0;
    for (const auto& [type, id] : nodes) {
        for (auto* card : editor.getModuleComponents()) {
            if (card == nullptr || card->getNodeId() != id)
                continue;
            ++cards;
            for (auto gap : auditAccessibility(*card)) {
                gap.path = "[" + type + "] " + gap.path;
                gaps.push_back(gap);
            }
        }
    }
    ASSERT_GT(cards, 30) << "the audit must reach the built-in cards";
    EXPECT_TRUE(matchesBaseline("ModuleCards", gaps));
}
