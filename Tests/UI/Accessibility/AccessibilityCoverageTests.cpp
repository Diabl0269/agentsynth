// AccessibilityCoverageTests.cpp -- the accessibility ratchet (docs/development/accessibility.md).
//
// 1. AccessibilityAuditTest proves the auditor itself on a tiny synthetic tree.
// 2. AccessibilityCoverageTest audits each real surface (a headless MainComponent, every Settings
//    tab, the dialogs and popups) and compares the name/tooltip gap counts with
//    AccessibilityBaseline.h: more gaps fails (a new control lacks a name or tooltip), fewer gaps
//    fails too, so the baseline is lowered in the same change that fixed them.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "../MidiRemote/MidiRemotePanelTestFixture.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/AccountService.h"
#include "AccessibilityAudit.h"
#include "AccessibilityBaseline.h"
#include "AccessibilitySettingsFixture.h"
#include "Auth/InMemoryTokenStore.h"
#include "MainComponent/MainComponent.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/VisualBuffer.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Assistant/SignInDialog.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Graph/ModMatrixComponent.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Library/ModuleLibraryHelpPopup.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/SampleWaveformComponent.h"
#include "UI/ModuleViews/ScopeComponent.h"
#include "UI/ModuleViews/ThresholdControlComponent.h"
#include "UI/ModuleViews/WavetableDisplayComponent.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineViewState.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_audio_utils/juce_audio_utils.h>

namespace {

using synth::test::auditAccessibility;
using synth::test::countGaps;
using synth::test::Gap;

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

// Tab lands on a focus region's root, so the root is what a screen reader names: every region
// of the main window must have a title.
TEST_F(MainComponentTest, EveryFocusRegionRootHasAScreenReaderName) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1400, 900);
    for (const auto& region : mc.getFocusRegionsForTest().getRegions()) {
        ASSERT_NE(region.root, nullptr) << region.id;
        EXPECT_TRUE(region.root->getTitle().isNotEmpty()) << "focus region \"" << region.id << "\" has no title";
    }
}

// Two tracks, one bus, a send on the first track and each strip's default inserts (a Parametric EQ among
// them), every section shown, so
// each kind of strip control (and the send, insert and EQ rows) is on screen to be audited.
TEST_F(MainComponentTest, AccessibilityCoverageMixerPanel) {
    synth::test::PersistedKeysGuard guard({"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder",
                                           "mixerSectionInsertsHidden", "mixerSectionSendsHidden",
                                           "mixerSectionEqHidden"});
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();
    mc.simulateAddAudioTrackClick();
    auto& panel = mc.getBottomDock().getMixerPanel();
    const auto bus = panel.createBus();
    panel.rebuild();
    ASSERT_NE(panel.getStripColumnForTest(0), nullptr);
    panel.getStripColumnForTest(0)->getSendList().addSendTo(bus);
    panel.rebuild();
    for (const auto section :
         {synth::ui::MixerSection::Inserts, synth::ui::MixerSection::Sends, synth::ui::MixerSection::Eq})
        panel.getSectionLayout().setHidden(section, false);
    panel.setSize(1400, 700);
    panel.resized();
    ASSERT_NE(panel.getMasterColumnForTest(), nullptr);
    ASSERT_NE(panel.getStripColumnForTest(2), nullptr) << "two tracks and a bus";
    ASSERT_EQ(panel.getStripColumnForTest(0)->getSendList().getRowCount(), 1);
    ASSERT_GE(panel.getStripColumnForTest(0)->getInsertList().getRowCount(), 1);
    EXPECT_TRUE(matchesBaseline("Mixer", auditAccessibility(panel)));
}

TEST(AccessibilityCoverageTest, ExportAudioDialog) {
    synth::ui::ExportAudioDialog dialog(16.0, false, 0.0, 0.0, 120.0, true,
                                        juce::File::getSpecialLocation(juce::File::tempDirectory), "Default");
    dialog.setSize(dialog.getWidth() > 0 ? dialog.getWidth() : 480, dialog.getHeight() > 0 ? dialog.getHeight() : 400);
    EXPECT_TRUE(matchesBaseline("ExportAudioDialog", auditAccessibility(dialog)));
}

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
// Dialogs and popups
// =====================================================================}

// ============================================================================

TEST_F(AccessibilitySettingsTest, DualIOPerModulePopup) {
    PreferencesSettingsTab tab(appProperties);
    tab.setSize(500, 460);
    auto popup = tab.createDualIOPerModuleDefaultsPopupForTest();
    ASSERT_NE(popup, nullptr);
    EXPECT_TRUE(matchesBaseline("DualIOPerModulePopup", auditAccessibility(*popup)));
}

TEST(AccessibilityCoverageTest, SignInDialog) {
    synth::AccountService service(
        "http://mock-host:8787",
        [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&, int,
           const std::atomic<bool>&) { return synth::AuthClient::HttpResult{}; },
        std::make_unique<synth::InMemoryTokenStore>());
    synth::SignInDialog dialog(service);
    dialog.setSize(360, 220);
    EXPECT_TRUE(matchesBaseline("SignInDialog", auditAccessibility(dialog)));
}

namespace {
std::vector<synth::ui::MacroPortConfigDialog::PortRow> macroPortRows() {
    using Row = synth::ui::MacroPortConfigDialog::PortRow;
    Row mono;
    mono.nodeUuid = "in-mono";
    mono.isInput = true;
    mono.name = "Pitch In";
    Row poly;
    poly.nodeUuid = "in-poly";
    poly.isInput = true;
    poly.name = "Voices In";
    poly.shape = MacroPortShape::Poly;
    poly.voiceCount = 4;
    Row midi;
    midi.nodeUuid = "out-midi";
    midi.isInput = false;
    midi.name = "Gate Out";
    midi.kind = synth::MacroPortKind::Midi;
    return {mono, poly, midi};
}
} // namespace

TEST(AccessibilityCoverageTest, MacroPortConfigDialog) {
    synth::ui::MacroPortConfigDialog dialog("My Macro", macroPortRows());
    EXPECT_TRUE(matchesBaseline("MacroPortConfigDialog", auditAccessibility(dialog)));
}

TEST(AccessibilityCoverageTest, MacroAutoPortPromptDialog) {
    synth::ui::MacroAutoPortPromptDialog dialog(2);
    EXPECT_TRUE(matchesBaseline("MacroAutoPortPromptDialog", auditAccessibility(dialog)));
}

TEST(AccessibilityCoverageTest, EQWindow) {
    ParametricEQModule eq;
    EQWindow window(eq);
    EXPECT_TRUE(matchesBaseline("EQWindow", auditAccessibility(window)));
}

// Every module view built on its own, so the audit reaches the focusable editors (the EQ curve, the curve
// editor, the threshold slider) and the read-only visualizers sit beside them for the name/tooltip asserts
// in ModuleViewsKeyboardTests.cpp.
TEST(AccessibilityCoverageTest, ModuleViews) {
    ParametricEQModule eq;
    EQCurveComponent eqCurve(eq);
    synth::ui::CurveEditorComponent curveEditor;
    VisualBuffer buffer(256);
    ScopeComponent scope(buffer);
    FilterModule filter;
    FrequencyResponseComponent response(filter);
    WavetableOscillatorModule wavetable;
    WavetableDisplayComponent wavetableDisplay(wavetable);
    SamplerModule sampler;
    SampleWaveformComponent waveform(sampler);
    struct Source : ThresholdMeterSource {
        float getMeterLevel() const override { return 0.0f; }
        float getEffectiveThreshold() const override { return 0.5f; }
        bool isOverThreshold() const override { return false; }
        int getTriggerCount() const override { return 0; }
        ThresholdScale getThresholdScale() const override { return ThresholdScale::Unipolar; }
        juce::String getThresholdParamID() const override { return "trigThreshold"; }
    } source;
    juce::AudioParameterFloat threshold(juce::ParameterID("trigThreshold", 1), "Threshold", 0.0f, 1.0f, 0.5f);
    ThresholdControlComponent thresholdControl(source, &threshold);
    ThresholdControlComponent meterOnly(source);

    juce::Component holder;
    holder.setSize(600, 600);
    for (juce::Component* view : std::initializer_list<juce::Component*>{
             &eqCurve, &curveEditor, &scope, &response, &wavetableDisplay, &waveform, &thresholdControl, &meterOnly}) {
        holder.addAndMakeVisible(view);
        view->setBounds(0, 0, 200, 100);
    }
    EXPECT_TRUE(matchesBaseline("ModuleViews", auditAccessibility(holder)));
    EXPECT_TRUE(eqCurve.getWantsKeyboardFocus() && curveEditor.getWantsKeyboardFocus())
        << "the audit only sees the editors because they are Tab stops";
}

TEST(AccessibilityCoverageTest, WelcomeScreen) {
    synth::ui::WelcomeScreenComponent welcome;
    welcome.setSize(900, 700);
    welcome.setRecentProjects({juce::File("/tmp/Alpha.synthproj"), juce::File("/tmp/Beta.synthproj")});
    EXPECT_TRUE(matchesBaseline("WelcomeScreen", auditAccessibility(welcome)));
}

TEST(AccessibilityCoverageTest, ColourPickerPopup) {
    synth::ui::ColourPickerPopup popup(juce::Colours::red, nullptr, {}, {});
    EXPECT_TRUE(matchesBaseline("ColourPickerPopup", auditAccessibility(popup)));
}

TEST(AccessibilityCoverageTest, ModuleLibraryHelpPopup) {
    synth::ui::ModuleLibraryHelpPopup popup;
    EXPECT_TRUE(matchesBaseline("ModuleLibraryHelpPopup", auditAccessibility(popup)));
}

// ============================================================================
// The module library, the AI chat and the MIDI Remote panel.
// ============================================================================

TEST(AccessibilityCoverageTest, ModuleLibrary) {
    ModuleLibraryComponent library;
    library.setSize(260, 900);
    EXPECT_TRUE(matchesBaseline("ModuleLibrary", auditAccessibility(library)));
}

// A conversation with one message in it, so a bubble is audited too.
TEST_F(AccessibilitySettingsTest, AIChat) {
    aiChat->setLocalHistoryDirectoryForTesting(
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("a11y-chat-" + juce::Uuid().toString()));
    aiChat->setSize(400, 700);
    for (auto* child : aiChat->getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            if (editor->isVisible())
                editor->setText("Create a fat bass");
    aiChat->triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    EXPECT_TRUE(matchesBaseline("AIChat", auditAccessibility(*aiChat)));
}

// A controller with a transport-strip template, one control selected: the Controllers list, the
// surface, its toolbar and page strip, and the inspector with its assignment widgets.
TEST_F(MidiRemotePanelLiveRefreshTest, MidiRemotePanel) {
    synth::ui::AddControllerPopover::Choice choice;
    choice.deviceIdentifier = synth::midi::hostSourceKey();
    choice.deviceName = "Test device";
    choice.profileName = "Test device";
    choice.startWith = synth::ui::AddControllerPopover::StartWith::templateLayout;
    choice.templateId = "template-transport-strip";
    const auto profileId = panel_.createControllerFromChoice(choice);
    ASSERT_FALSE(profileId.isEmpty());
    const auto profiles = controller_->getProfiles();
    ASSERT_FALSE(profiles.empty());
    ASSERT_FALSE(profiles.front().controls.empty());
    panel_.setSize(1200, 320);
    panel_.selectForTest(profileId, profiles.front().controls.front().id);
    panel_.resized();
    EXPECT_TRUE(matchesBaseline("MidiRemote", auditAccessibility(panel_)));
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

// ============================================================================
// Timeline automation lanes: a lane row's header, the track header's fold arrow, the "+ Add automation..."
// row and the picker it opens, and a lane's modulator rows (an LFO's full row, another source's read-only one).
// ============================================================================

namespace {
// Offers one parameter so the "+ Add automation..." picker has a row to audit, and two modulators on every lane.
struct OneParameterHost : synth::ui::TrackHeaderHost {
    std::vector<synth::ui::ModulatorInfo> getModulators(const juce::String&, const juce::String&) override {
        synth::ui::ModulatorInfo lfo;
        lfo.sourceUuid = "lfo";
        lfo.sourceTitle = "LFO 1";
        lfo.isLfo = true;
        lfo.attenuverterUuid = "atten";
        auto env = lfo;
        env.sourceUuid = "env";
        env.sourceTitle = "Filter Env";
        env.isLfo = false;
        env.attenuverterUuid = "atten-env";
        return {lfo, env};
    }
    std::vector<AutomatableParameter> getAutomatableParameters(synth::TrackId) override {
        AutomatableParameter p;
        p.moduleTitle = "Filter 1";
        p.parameterName = "Cutoff";
        return {p};
    }
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};
} // namespace

TEST(AccessibilityCoverageTest, AutomationLanes) {
    synth::TimelineDoc doc;
    OneParameterHost host;
    synth::ui::TimelinePanelComponent panel;
    panel.setTimelineDoc(&doc);
    panel.setTrackHeaderHost(&host);
    panel.setSize(1200, 500);
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
    const auto lane = doc.addLane(track, "node-uuid", "cutoff", {});
    panel.showAutomationLane(lane);

    auto* laneHeader = panel.laneHeaderForTest(lane);
    ASSERT_NE(laneHeader, nullptr);
    auto gaps = auditAccessibility(*laneHeader);
    for (auto gap : auditAccessibility(panel.getTrackHeaderAt(0)->getFoldArrow())) {
        gap.path = "[fold arrow] " + gap.path;
        gaps.push_back(gap);
    }
    auto* addRow = panel.addAutomationRowForTest(track);
    EXPECT_NE(addRow, nullptr) << "the open track closes its lanes with the add row";
    if (addRow != nullptr) {
        EXPECT_TRUE(addRow->getWantsKeyboardFocus()) << "a Tab stop";
        for (auto gap : auditAccessibility(*addRow)) {
            gap.path = "[add automation row] " + gap.path;
            gaps.push_back(gap);
        }
    }
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    panel.setAddAutomationPickerHookForTest([&picker](auto p) { picker = std::move(p); });
    panel.openAddAutomationPicker(track, *panel.getTrackHeaderAt(0));
    EXPECT_NE(picker, nullptr);
    if (picker != nullptr) {
        picker->setSize(300, 200);
        for (auto gap : auditAccessibility(*picker)) {
            gap.path = "[add automation picker] " + gap.path;
            gaps.push_back(gap);
        }
    }
    panel.setAddAutomationPickerHookForTest(nullptr);
    for (int i = 0; i < 2; ++i) {
        auto* modulator = panel.modulatorRowForTest(lane, i);
        EXPECT_NE(modulator, nullptr) << "modulator row " << i;
        if (modulator == nullptr)
            continue;
        for (auto gap : auditAccessibility(*modulator)) {
            gap.path = "[modulator row] " + gap.path;
            gaps.push_back(gap);
        }
    }
    panel.setTrackHeaderHost(nullptr);
    EXPECT_TRUE(matchesBaseline("AutomationLanes", gaps));
}

// The Mod Matrix open with two routings (one wired, one empty), and its searchable picker. The main
// window audit closes the matrix, so its rows are only counted here.
TEST(AccessibilityCoverageTest, ModMatrix) {
    AudioEngine engine;
    auto* lfo = engine.getGraph().addNode(std::make_unique<LFOModule>()).get();
    auto* filter = engine.getGraph().addNode(std::make_unique<FilterModule>()).get();
    engine.addModRouting(lfo->nodeID, 0, filter->nodeID, 1);
    engine.addEmptyModRouting();
    ModMatrixComponent matrix(engine);
    matrix.setSize(600, 400);
    matrix.updateRowsFromGraph();
    ASSERT_EQ(matrix.getNumRowsForTest(), 2);
    EXPECT_TRUE(matchesBaseline("ModMatrix", auditAccessibility(matrix)));

    synth::ui::ModMatrixPicker picker("source", {{1, "LFOs", "LFO 1"}, {2, "Filters", "Filter 1"}}, 1, nullptr);
    EXPECT_TRUE(matchesBaseline("ModMatrixPicker", auditAccessibility(picker)));
}
