// AudioSettingsTabTests.cpp: the Settings Audio tab in folding sections -- their order and fold state, the Inputs
// table and its pair button, the Remembered interfaces list and Forget, the layout at the tab's smallest size under
// every built-in theme, and the Tab order. Driven by a fake interface source, so none of it depends on the audio
// hardware the test runs on, and by real mouse and key events on the controls.
#include "../../TestWait.h"
#include "../Accessibility/AccessibilitySettingsFixture.h"
#include "../Accessibility/TabOrderHelpers.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsTab.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>

namespace {
using Section = AudioSettingsTab::Section;

const juce::String kType = "CoreAudio";

class FakeSource : public AudioSettingsSource {
public:
    struct UsedCall {
        int first, count;
        bool used;
    };

    FakeSource() {
        info.deviceType = kType;
        info.deviceName = "Volt 2 (studio)";
        info.numInputs = 4;
        info.usedMask = 0b1111;
        info.sampleRate = 48000.0;
        info.bufferSamples = 256;
        info.recordingLatencySamples = 197;
    }

    AudioInterfaceInfo currentInterface() override { return info; }
    void noteOpenInterface() override {
        if (info.deviceName.isNotEmpty() && !store.has(info.deviceType, info.deviceName))
            store.touch(info.deviceType, info.deviceName);
    }
    synth::AudioInputLayoutStore& layoutStore() override { return store; }
    float readInputPeak(int jack) override { return peaks[(size_t)jack]; }
    void setInputsUsed(int first, int count, bool used) override {
        usedCalls.push_back({first, count, used});
        for (int jack = first; jack < first + count; ++jack)
            info.usedMask = used ? (info.usedMask | (1u << jack)) : (info.usedMask & ~(1u << jack));
    }
    std::vector<AudioMidiDevice> midiInputs() override { return midiIn; }
    void setMidiInputEnabled(const juce::String&, bool) override {}
    std::vector<AudioMidiDevice> midiOutputs() override { return {}; }
    void setMidiOutput(const juce::String&) override {}

    AudioInterfaceInfo info;
    synth::AudioInputLayoutStore store;
    std::array<float, 8> peaks{};
    std::vector<UsedCall> usedCalls;
    std::vector<AudioMidiDevice> midiIn{{"a", "Keystation 49", true}, {"b", "nanoKONTROL2", false}};
};

// A press and release on the middle of `component`, delivered to its own mouse callbacks as the live mouse would
// (docs/development/test-patterns.md, "Test the real mouse path").
void clickThroughMouse(juce::Component& component) {
    const auto centre = component.getLocalBounds().toFloat().getCentre();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    const auto now = juce::Time::getCurrentTime();
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto event = [&](juce::ModifierKeys m) {
        return juce::MouseEvent(source, centre, m, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &component, &component, now, centre,
                                now, 1, false);
    };
    component.mouseEnter(event(juce::ModifierKeys()));
    component.mouseDown(event(mods));
    component.mouseUp(event(juce::ModifierKeys()));
}

// Every control a user could aim at: buttons, drop-downs, text fields and anything that takes keyboard focus.
void collectControls(juce::Component& parent, std::vector<juce::Component*>& out) {
    for (auto* child : parent.getChildren()) {
        if (!child->isVisible())
            continue;
        const bool control = dynamic_cast<juce::Button*>(child) != nullptr ||
                             dynamic_cast<juce::ComboBox*>(child) != nullptr ||
                             dynamic_cast<juce::TextEditor*>(child) != nullptr || child->getWantsKeyboardFocus();
        if (control && dynamic_cast<juce::Viewport*>(child) == nullptr)
            out.push_back(child);
        if (dynamic_cast<juce::Button*>(child) == nullptr && dynamic_cast<juce::ComboBox*>(child) == nullptr &&
            dynamic_cast<juce::TextEditor*>(child) == nullptr)
            collectControls(*child, out);
    }
}

} // namespace

class AudioSettingsTabTest : public AccessibilitySettingsTest {
protected:
    void SetUp() override {
        AccessibilitySettingsTest::SetUp();
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    }

    std::unique_ptr<AudioSettingsTab> makeTab(int width = 520, int height = 1600) {
        auto tab = std::make_unique<AudioSettingsTab>(deviceManager, std::vector<juce::String>{"nanoKONTROL2"}, &source,
                                                      &appProperties);
        tab->setSize(width, height);
        return tab;
    }

    FakeSource source;
};

TEST_F(AudioSettingsTabTest, TheSectionsAreInOrderWithOutputsRecordingAndMidiFolded) {
    auto tab = makeTab();
    const std::vector<std::pair<Section, juce::String>> expected = {{Section::Device, "Device"},
                                                                    {Section::Inputs, "Inputs"},
                                                                    {Section::Interfaces, "Remembered interfaces"},
                                                                    {Section::Outputs, "Outputs"},
                                                                    {Section::Recording, "Recording"},
                                                                    {Section::Midi, "MIDI"}};
    int previousY = -1;
    for (const auto& [section, name] : expected) {
        auto& header = tab->getSectionHeaderForTest(section);
        EXPECT_TRUE(header.getTitle().startsWith(name)) << name;
        EXPECT_GT(header.getY(), previousY) << name << " is below the section before it";
        previousY = header.getY();
        const bool openByDefault =
            section == Section::Device || section == Section::Inputs || section == Section::Interfaces;
        EXPECT_EQ(tab->isSectionOpen(section), openByDefault) << name;
        EXPECT_TRUE(header.getTitle().endsWith(openByDefault ? ", expanded" : ", collapsed")) << name;
        EXPECT_TRUE(header.getWantsKeyboardFocus());
    }
}

TEST_F(AudioSettingsTabTest, TheHeadersCarryTheirSummaries) {
    auto tab = makeTab();
    EXPECT_EQ(tab->getFoldHeaderForTest(Section::Device).getSummary(),
              juce::String("48000 Hz") + juce::String::fromUTF8(" \xC2\xB7 ") + "256 samples (5.3 ms)" +
                  juce::String::fromUTF8(" \xC2\xB7 ") + kType);
    EXPECT_EQ(tab->getFoldHeaderForTest(Section::Inputs).getSummary(),
              juce::String("Volt 2 (studio)") + juce::String::fromUTF8(" \xC2\xB7 ") + "4 jacks");
    const auto recording = tab->getFoldHeaderForTest(Section::Recording).getSummary();
    EXPECT_TRUE(recording.startsWith("WAV 32-bit float")) << recording;
    EXPECT_TRUE(
        recording.contains("latency fixed automatically (" + juce::String::fromUTF8("\xE2\x88\x92") + "4.1 ms)"))
        << recording;
}

TEST_F(AudioSettingsTabTest, FoldStatePersistsUnderAudioFoldedAndAnAllOpenChoiceIsRemembered) {
    {
        auto tab = makeTab();
        tab->setSectionOpen(Section::Outputs, true);
        tab->setSectionOpen(Section::Device, false);
    }
    EXPECT_TRUE(appProperties.getUserSettings()->containsKey("audioFolded"));
    {
        auto tab = makeTab();
        EXPECT_TRUE(tab->isSectionOpen(Section::Outputs));
        EXPECT_FALSE(tab->isSectionOpen(Section::Device));
        EXPECT_TRUE(tab->isSectionOpen(Section::Inputs));
        EXPECT_FALSE(tab->isSectionOpen(Section::Midi));
        for (int i = 0; i < AudioSettingsTab::kNumSections; ++i)
            tab->setSectionOpen(static_cast<Section>(i), true);
    }
    auto tab = makeTab();
    for (int i = 0; i < AudioSettingsTab::kNumSections; ++i)
        EXPECT_TRUE(tab->isSectionOpen(static_cast<Section>(i))) << "section " << i << " was unfolded by the user";
}

TEST_F(AudioSettingsTabTest, ClickingAHeaderFoldsItsBodyAndTheSectionsBelowMoveUp) {
    auto tab = makeTab();
    auto& inputsHeader = tab->getSectionHeaderForTest(Section::Inputs);
    const int before = tab->getSectionHeaderForTest(Section::Interfaces).getY();
    clickThroughMouse(inputsHeader);
    EXPECT_FALSE(tab->isSectionOpen(Section::Inputs));
    EXPECT_LT(tab->getSectionHeaderForTest(Section::Interfaces).getY(), before);
    EXPECT_TRUE(inputsHeader.getTitle().endsWith(", collapsed"));
    EXPECT_FALSE(tab->getSectionForTest(Section::Inputs).isShowing());
}

TEST_F(AudioSettingsTabTest, AFoldFadesAndSlidesTheSectionsBelowInsteadOfJumping) {
    auto tab = makeTab();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(true);
    const int open = tab->getSectionHeaderForTest(Section::Interfaces).getY();
    tab->setSectionOpen(Section::Inputs, false);
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    const int halfway = tab->getSectionHeaderForTest(Section::Interfaces).getY();
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    const int folded = tab->getSectionHeaderForTest(Section::Interfaces).getY();
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
    EXPECT_LT(halfway, open);
    EXPECT_GT(halfway, folded);
}

TEST_F(AudioSettingsTabTest, FoldAllAndExpandAllGoThroughTheSharedButton) {
    auto tab = makeTab();
    auto& button = tab->getFoldAllButtonForTest();
    clickThroughMouse(button);
    for (int i = 0; i < AudioSettingsTab::kNumSections; ++i)
        EXPECT_FALSE(tab->isSectionOpen(static_cast<Section>(i)));
    EXPECT_TRUE(button.isAllFolded());
    clickThroughMouse(button);
    for (int i = 0; i < AudioSettingsTab::kNumSections; ++i)
        EXPECT_TRUE(tab->isSectionOpen(static_cast<Section>(i)));
}

TEST_F(AudioSettingsTabTest, TheInputsTableHasOneRowPerMonoJackAndOnePerStereoPair) {
    source.info.numInputs = 8;
    auto tab = makeTab();
    auto& table = tab->getInputsTableForTest();
    ASSERT_EQ(table.getNumRowsForTest(), 8);
    EXPECT_EQ(table.getJacksTextForTest(0), "In 1");
    EXPECT_EQ(table.getKindTextForTest(0), "Mono");
    EXPECT_EQ(table.getNumPairButtonsForTest(), 4);

    source.store.setPaired(kType, source.info.deviceName, 1, true);
    ASSERT_EQ(table.getNumRowsForTest(), 7);
    EXPECT_EQ(table.getJacksTextForTest(2), "In 3 + 4");
    EXPECT_EQ(table.getKindTextForTest(2), "Stereo");
    EXPECT_EQ(table.getLevelCellForTest(2)->getChannelCount(), 2) << "a stereo input has two meter bars";
    EXPECT_EQ(table.getLevelCellForTest(0)->getChannelCount(), 1);
}

TEST_F(AudioSettingsTabTest, AnInterfaceWithThreeJacksLeavesTheLastOneMono) {
    source.info.numInputs = 3;
    auto tab = makeTab();
    EXPECT_EQ(tab->getInputsTableForTest().getNumRowsForTest(), 3);
    EXPECT_EQ(tab->getInputsTableForTest().getNumPairButtonsForTest(), 1);
}

TEST_F(AudioSettingsTabTest, ThePairButtonJoinsAndSplitsAPairThroughARealClick) {
    auto tab = makeTab();
    auto& table = tab->getInputsTableForTest();
    auto* button = table.getPairButtonForTest(0);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->getTitle(), "Pair In 1 and In 2 as one stereo input");
    EXPECT_EQ(button->getTooltip(), button->getTitle());
    EXPECT_FALSE(button->getToggleState());
    ASSERT_EQ(table.getNumRowsForTest(), 4);

    // The click lands on the button itself, not on a row beneath it.
    const auto point = table.getLocalPoint(button, button->getLocalBounds().getCentre());
    EXPECT_EQ(table.getComponentAt(point.x, point.y), button);

    clickThroughMouse(*button);
    EXPECT_TRUE(source.store.isPaired(kType, source.info.deviceName, 0));
    EXPECT_EQ(table.getNumRowsForTest(), 3);
    button = table.getPairButtonForTest(0);
    EXPECT_TRUE(button->getToggleState());
    EXPECT_EQ(button->getTitle(), "Split In 1 + 2 into two mono inputs");

    clickThroughMouse(*button);
    EXPECT_FALSE(source.store.isPaired(kType, source.info.deviceName, 0));
    EXPECT_EQ(table.getNumRowsForTest(), 4);
}

TEST_F(AudioSettingsTabTest, ANameTypedInTheFieldIsStoredOnReturnAndShownWhenTheTabIsReopened) {
    auto tab = makeTab();
    auto* editor = tab->getInputsTableForTest().getNameEditorForTest(0);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->getTitle(), "Name for In 1");
    editor->setText("Vocal mic", juce::dontSendNotification);
    const bool consumed = synth::test::sendReturn(*editor);
    EXPECT_TRUE(consumed);
    synth::test::waitUntil([&] { return (source.store.getName(kType, source.info.deviceName, 0)) == ("Vocal mic"); });
    EXPECT_EQ(source.store.getName(kType, source.info.deviceName, 0), "Vocal mic");

    auto again = makeTab();
    EXPECT_EQ(again->getInputsTableForTest().getNameEditorForTest(0)->getText(), "Vocal mic");
    EXPECT_EQ(again->getInputsTableForTest().getNameEditorForTest(1)->getText(), juce::String());
}

TEST_F(AudioSettingsTabTest, TheUseTickTurnsTheJackOnAndOffThroughTheSource) {
    auto tab = makeTab();
    auto& table = tab->getInputsTableForTest();
    auto* tick = table.getUseTickForTest(1);
    ASSERT_NE(tick, nullptr);
    EXPECT_EQ(tick->getTitle(), "Use In 2");
    EXPECT_TRUE(tick->getToggleState());
    clickThroughMouse(*tick);
    ASSERT_EQ(source.usedCalls.size(), 1u);
    EXPECT_EQ(source.usedCalls[0].first, 1);
    EXPECT_EQ(source.usedCalls[0].count, 1);
    EXPECT_FALSE(source.usedCalls[0].used);

    source.store.setPaired(kType, source.info.deviceName, 0, true);
    clickThroughMouse(*table.getUseTickForTest(0));
    EXPECT_EQ(source.usedCalls.back().count, 2) << "a stereo input switches both jacks";
}

TEST_F(AudioSettingsTabTest, TheLevelCellsReadTheEnginePeaksAndAreNamed) {
    source.peaks[0] = 0.5f;
    auto tab = makeTab();
    auto& table = tab->getInputsTableForTest();
    table.pollMetersForTest();
    auto* cell = table.getLevelCellForTest(0);
    EXPECT_GT(cell->getLevelFractionForTest(0), 0.0f);
    EXPECT_EQ(table.getLevelCellForTest(1)->getLevelFractionForTest(0), 0.0f);
    EXPECT_EQ(cell->getTitle(), "In 1 level");
    EXPECT_TRUE(cell->getWantsKeyboardFocus());
    EXPECT_EQ(cell->describeLevel(), "-6 dB");
}

TEST_F(AudioSettingsTabTest, RememberedInterfacesListsAnUnpluggedDeviceAndForgetRemovesItThroughARealClick) {
    source.store.setName(kType, "Scarlett 4i4", 0, "Guitar");
    source.store.setName(kType, source.info.deviceName, 0, "Vocal mic");
    source.store.setName(kType, source.info.deviceName, 1, "DI");
    auto tab = makeTab();
    auto& list = tab->getInterfacesForTest();
    ASSERT_EQ(list.getNumRowsForTest(), 2);

    AudioInterfacesList::Row* inUse = nullptr;
    AudioInterfacesList::Row* away = nullptr;
    for (int i = 0; i < list.getNumRowsForTest(); ++i)
        (list.getRowForTest(i).inUse ? inUse : away) = &list.getRowForTest(i);
    ASSERT_NE(inUse, nullptr);
    ASSERT_NE(away, nullptr);
    EXPECT_EQ(inUse->status, juce::String("in use") + juce::String::fromUTF8(" \xC2\xB7 ") + "2 inputs named");
    EXPECT_EQ(inUse->forget, nullptr) << "the interface in use cannot be forgotten";
    EXPECT_EQ(away->deviceName, "Scarlett 4i4");
    EXPECT_EQ(away->status, juce::String("not connected") + juce::String::fromUTF8(" \xC2\xB7 ") + "names kept");
    ASSERT_NE(away->forget, nullptr);
    EXPECT_EQ(away->forget->getTitle(), "Forget Scarlett 4i4");
    EXPECT_FALSE(away->forget->getTooltip().isEmpty());

    clickThroughMouse(*away->forget);
    EXPECT_FALSE(source.store.has(kType, "Scarlett 4i4"));
    EXPECT_TRUE(source.store.has(kType, source.info.deviceName));
    EXPECT_EQ(source.store.getName(kType, source.info.deviceName, 0), "Vocal mic")
        << "only that interface is forgotten";
    EXPECT_EQ(list.getNumRowsForTest(), 1);
}

// The hidden "no interface" note once kept its slot, so a single remembered interface was laid out below the
// section's bottom edge and the section looked empty.
TEST_F(AudioSettingsTabTest, EveryRememberedInterfaceRowSitsInsideTheList) {
    for (const int extra : {0, 1}) {
        if (extra == 1)
            source.store.setName(kType, "Scarlett 4i4", 0, "Guitar");
        source.store.setName(kType, source.info.deviceName, 0, "Vocal mic");
        auto tab = makeTab();
        auto& list = tab->getInterfacesForTest();
        list.setSize(400, list.preferredHeight(400));
        ASSERT_EQ(list.getNumRowsForTest(), 1 + extra);
        for (int i = 0; i < list.getNumRowsForTest(); ++i) {
            const auto& row = list.getRowForTest(i);
            EXPECT_TRUE(list.getLocalBounds().contains(row.getBounds()))
                << "row " << i << " of " << list.getNumRowsForTest() << " at " << row.getBounds().toString();
            EXPECT_TRUE(row.getTitle().startsWith(row.deviceName)) << "a row has a screen-reader name";
        }
    }
}

TEST_F(AudioSettingsTabTest, TheInUseStatusCountsNamedInputsSingularAndPlural) {
    source.store.setName(kType, source.info.deviceName, 0, "Vocal mic");
    auto tab = makeTab();
    EXPECT_TRUE(tab->getInterfacesForTest().getRowForTest(0).status.endsWith("1 input named"));
    source.store.setName(kType, source.info.deviceName, 0, "");
    EXPECT_TRUE(tab->getInterfacesForTest().getRowForTest(0).status.endsWith("no inputs named"));
}

TEST_F(AudioSettingsTabTest, AFirstSeenInterfaceGetsAnEntryWithNoPairsAndNoNames) {
    auto tab = makeTab();
    ASSERT_TRUE(source.store.has(kType, source.info.deviceName));
    const auto entries = source.store.entries();
    EXPECT_TRUE(entries[0].pairedPairs.empty());
    EXPECT_TRUE(entries[0].names.empty());
}

TEST_F(AudioSettingsTabTest, SwitchingInterfaceShowsItsOwnLayoutAndSwitchingBackRestoresNamesAndPairsWithoutReticking) {
    source.store.setPaired(kType, "Volt 2 (studio)", 0, true);
    source.store.setName(kType, "Volt 2 (studio)", 0, "Vocal mic");
    auto tab = makeTab();
    EXPECT_EQ(tab->getInputsTableForTest().getNumRowsForTest(), 3);

    source.info.deviceName = "Scarlett 4i4";
    tab->refreshAll();
    EXPECT_EQ(tab->getInputsTableForTest().getNumRowsForTest(), 4);
    EXPECT_EQ(tab->getInterfacesForTest().getNumRowsForTest(), 2);

    source.info.deviceName = "Volt 2 (studio)";
    tab->refreshAll();
    ASSERT_EQ(tab->getInputsTableForTest().getNumRowsForTest(), 3);
    EXPECT_EQ(tab->getInputsTableForTest().getNameEditorForTest(0)->getText(), "Vocal mic");
    EXPECT_TRUE(source.usedCalls.empty()) << "nothing was re-ticked";
}

TEST_F(AudioSettingsTabTest, RecordingShowsTheTakeFormatAndTheAutomaticLatencyReadOnly) {
    auto tab = makeTab();
    EXPECT_EQ(tab->getRecordingForTest().getFormatTextForTest(), "WAV 32-bit float");
    EXPECT_EQ(tab->getRecordingForTest().getLatencyTextForTest(),
              "fixed automatically (" + juce::String::fromUTF8("\xE2\x88\x92") + "4.1 ms)");
}

TEST_F(AudioSettingsTabTest, TheMidiSectionKeepsTheControllersCaptionAndOneTickPerInput) {
    auto tab = makeTab();
    EXPECT_TRUE(tab->getCaptionTextForTest().startsWith("Controllers: nanoKONTROL2."));
    EXPECT_EQ(tab->getMidiForTest().getNumInputTicksForTest(), 2);
}

TEST_F(AudioSettingsTabTest, NoVisibleControlsOverlapAtTheTabsSmallestSizeUnderEveryTheme) {
    source.info.numInputs = 8;
    source.store.setPaired(kType, source.info.deviceName, 1, true);
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel lookAndFeel;
        lookAndFeel.applyTheme(theme);
        auto tab = std::make_unique<AudioSettingsTab>(deviceManager, std::vector<juce::String>{"nanoKONTROL2"}, &source,
                                                      nullptr);
        tab->setLookAndFeel(&lookAndFeel);
        for (int i = 0; i < AudioSettingsTab::kNumSections; ++i)
            tab->setSectionOpen(static_cast<Section>(i), true);
        tab->setSize(SettingsWindow::kMinWidth, SettingsWindow::kMinHeight);

        auto& content = *tab->getViewportForTest().getViewedComponent();
        std::vector<juce::Component*> controls;
        collectControls(content, controls);
        ASSERT_GT(controls.size(), 20u) << theme.name;
        std::vector<juce::Rectangle<int>> bounds;
        for (auto* control : controls)
            bounds.push_back(content.getLocalArea(control, control->getLocalBounds()));
        for (size_t a = 0; a < bounds.size(); ++a) {
            EXPECT_GT(bounds[a].getWidth(), 0) << theme.name;
            EXPECT_GE(bounds[a].getX(), 0) << theme.name;
            EXPECT_LE(bounds[a].getRight(), content.getWidth())
                << theme.name << ": " << synth::test::tabStopName(*controls[a]);
            for (size_t b = a + 1; b < bounds.size(); ++b)
                EXPECT_FALSE(bounds[a].intersects(bounds[b]))
                    << theme.name << ": " << synth::test::tabStopName(*controls[a]) << " overlaps "
                    << synth::test::tabStopName(*controls[b]);
        }
        const auto image = tab->createComponentSnapshot(tab->getLocalBounds());
        EXPECT_TRUE(image.isValid()) << theme.name;
        tab->setLookAndFeel(nullptr);
    }
}

TEST_F(AudioSettingsTabTest, TabWalksTheTableRowByRowAndEveryStopIsNamed) {
    auto tab = makeTab();
    const auto walk = synth::test::walkTabOrder(*tab);
    EXPECT_TRUE(walk.isCompleteCycle());
    const auto names = walk.names();
    for (const auto& name : names)
        EXPECT_FALSE(name.startsWith("<unnamed")) << name;
    const int pairA = names.indexOf("Pair In 1 and In 2 as one stereo input");
    const int name1 = names.indexOf("Name for In 1");
    const int use1 = names.indexOf("Use In 1");
    const int name2 = names.indexOf("Name for In 2");
    const int pairB = names.indexOf("Pair In 3 and In 4 as one stereo input");
    ASSERT_GE(pairA, 0);
    EXPECT_LT(pairA, name1);
    EXPECT_LT(name1, use1);
    EXPECT_LT(use1, name2) << "a row is finished before the next one starts";
    EXPECT_LT(name2, pairB);
    const int level1 = names.indexOf("In 1 level");
    EXPECT_GT(level1, name1);
    EXPECT_LT(level1, use1);
}

TEST_F(AudioSettingsTabTest, EveryNewControlHasANameAndATooltipAndNoneFallsInTheAuditBaseline) {
    auto tab = makeTab();
    for (int i = 0; i < AudioSettingsTab::kNumSections; ++i)
        tab->setSectionOpen(static_cast<Section>(i), true);
    const auto gaps = synth::test::auditAccessibility(*tab);
    for (const auto& gap : gaps)
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "no name: " : "no tooltip: ") << gap.path;
}
