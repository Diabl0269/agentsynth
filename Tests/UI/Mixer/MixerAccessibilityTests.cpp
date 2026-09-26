// MixerAccessibilityTests.cpp -- FRO18: JUCE AccessibilityHandler names/values on the mixer's
// faders, pan knobs, meters and M/S buttons (plan (c)). Calls createAccessibilityHandler()
// directly on the component under test rather than going through getAccessibilityHandler() --
// the latter needs a native peer this suite never creates (the same headless-focus gap
// TimelineTrackFocusTests.cpp documents), where the former is a plain virtual callable either way
// and returns a fresh handler wrapping live state.
#include "../../TestSettingsHelpers.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Layout/DetachablePanelHost/DetachablePanelHost.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerColumnHeader.h"
#include "UI/Mixer/MixerDirectColumn.h"
#include "UI/Mixer/MixerFader.h"
#include "UI/Mixer/MixerInsertList.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include "UI/Mixer/MixerMeter.h"
#include "UI/Mixer/MixerSendList.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>
#include <optional>

namespace {

// The real on-disk settings file and the save/restore guard around it live in
// Tests/TestSettingsHelpers.h (FRO58) -- one copy for every test that opens it.
using synth::test::PersistedKeysGuard;
using synth::test::userSettingsTestOptions;

class MockProviderMACT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMACT"; }
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

} // namespace

TEST(MixerAccessibilityTest, FaderTextFromValueFunctionFormatsAsMinusThreeDb) {
    synth::ui::MixerFader fader;
    ASSERT_TRUE((bool)fader.getSlider().textFromValueFunction);
    EXPECT_EQ(fader.getSlider().textFromValueFunction(-3.0), "-3.0 dB")
        << "JUCE speaks the minus sign as \"minus\" -- VoiceOver reads this as \"minus 3 dB\"";
    EXPECT_EQ(fader.getSlider().textFromValueFunction(0.0), "0.0 dB");
}

TEST(MixerAccessibilityTest, PanTextFromValueFunctionFormatsAsPercentLeftRight) {
    synth::ui::MixerColumnComponent column;
    auto& fn = column.getPanSliderForTest().textFromValueFunction;
    ASSERT_TRUE((bool)fn);
    EXPECT_EQ(fn(0.0), "Center");
    EXPECT_EQ(fn(-0.5), "50% left");
    EXPECT_EQ(fn(0.5), "50% right");
    EXPECT_EQ(fn(-1.0), "100% left");
    EXPECT_EQ(fn(1.0), "100% right");
}

TEST(MixerAccessibilityTest, ColumnTitleIsTheChannelName) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();

    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    EXPECT_FALSE(column->getTitle().isEmpty());

    auto handler = column->createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::group);
    EXPECT_EQ(handler->getTitle(), column->getTitle())
        << "the default getTitle() implementation reads Component::getTitle() -- setColumn() must "
           "have called setTitle(column.name)";
}

TEST(MixerAccessibilityTest, MuteSoloButtonsMirrorToggleState) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    EXPECT_FALSE(column->getMuteButtonForTest().getToggleState());
    column->toggleMuted();
    EXPECT_TRUE(column->getMuteButtonForTest().getToggleState())
        << "the button's own PAINTED pressed state must track reality, not stay always-unpressed "
           "(the latent bug this mirroring fixes)";
    EXPECT_TRUE(column->getMuteButtonForTest().getTitle().contains("on"));

    EXPECT_FALSE(column->getSoloButtonForTest().getToggleState());
    column->toggleSoloed();
    EXPECT_TRUE(column->getSoloButtonForTest().getToggleState());
    EXPECT_TRUE(column->getSoloButtonForTest().getTitle().contains("on"));
}

TEST(MixerAccessibilityTest, MeterAccessibilityValueIsReadOnly) {
    synth::ui::MixerMeter meter;
    meter.peakProvider = [](int) { return 0.5f; };
    meter.refresh(1.0f); // attack is instant regardless of elapsed time -- see MixerMeterBallistics.h

    auto handler = meter.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::staticText);
    auto* value = handler->getValueInterface();
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(value->isReadOnly());
    // FRO146: dBFS text, not a percentage -- a linear 0.5 peak is ~-6.0 dBFS.
    EXPECT_EQ(value->getCurrentValueAsString(), "-6.0 dBFS");
}

// ============================================================================
// FRO228: header rename tooltip/help gated on setRenameEnabled(); Master's own title; the pan
// knob's AX value surviving its param attachment; insert/send rows and "Make channel" becoming
// real, named AX children; internal ids no longer leaking as button titles.
// ============================================================================

TEST(MixerAccessibilityTest, HeaderTooltipTracksRenameEnabled) {
    synth::ui::MixerColumnHeader header;
    EXPECT_TRUE(header.getNameLabelForTest().getTooltip().isNotEmpty())
        << "rename is enabled by default -- VoiceOver's help text must say so";

    header.setRenameEnabled(false);
    EXPECT_TRUE(header.getNameLabelForTest().getTooltip().isEmpty())
        << "Direct/Master disable rename -- the tooltip (read as AX help text) must not still offer "
           "\"Double-click to rename\" once the gesture is disabled";

    header.setRenameEnabled(true);
    EXPECT_TRUE(header.getNameLabelForTest().getTooltip().isNotEmpty())
        << "re-enabling rename must restore the tooltip";
}

TEST(MixerAccessibilityTest, MasterColumnHasOwnAccessibleTitle) {
    synth::ui::MixerMasterColumn master;
    EXPECT_EQ(master.getTitle(), "Master")
        << "Master's own group AccessibilityHandler (the default, unspecified-role one Component "
           "provides) reads Component::getTitle() -- without setTitle(\"Master\") in the ctor this "
           "was empty, unlike \"Bus 1\"/\"Direct\"";
}

TEST(MixerAccessibilityTest, PanAccessibilityValueStaysFormattedAfterBinding) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    // juce::SliderParameterAttachment's own constructor unconditionally overwrites
    // slider.textFromValueFunction with one built from the param's own getText() -- a raw
    // "0.0000000" for pan, which has no unit label. rebindControls() must reapply the "Center"/
    // "50% left"/"50% right" formatting AFTER constructing panAttachment_, or this (what a Slider's
    // own AccessibilityHandler actually reads via getTextFromValue()) regresses to the raw value.
    auto& pan = column->getPanSliderForTest();
    ASSERT_TRUE((bool)pan.textFromValueFunction) << "a real pan param must be bound by now";
    EXPECT_EQ(pan.textFromValueFunction(pan.getValue()), "Center") << "a freshly bound pan param defaults to center";
}

TEST(MixerAccessibilityTest, InsertListRowsExposeNameAndBypassedStateAsRealAxChildren) {
    synth::ui::MixerInsertList list;
    list.setEntries(
        {{{}, "gate-uuid", "Gate", /*bypassed=*/true}, {{}, "eq-uuid", "Parametric EQ", /*bypassed=*/false}},
        /*linear=*/true, {}, {}, {});
    list.setSize(140, list.getPreferredHeight());

    auto* row0 = list.getRowAccessibilityComponentForTest(0);
    auto* row1 = list.getRowAccessibilityComponentForTest(1);
    ASSERT_NE(row0, nullptr);
    ASSERT_NE(row1, nullptr);
    EXPECT_EQ(row0->getParentComponent(), &list) << "each row proxy must be a real child of the list";
    EXPECT_EQ(row0->getTitle(), "Gate, bypassed");
    EXPECT_EQ(row1->getTitle(), "Parametric EQ");
    EXPECT_EQ(list.getRowAccessibilityComponentForTest(2), nullptr) << "only one proxy per entry";
}

TEST(MixerAccessibilityTest, SendListExposesAddSendAsAReachableNamedControl) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);

    auto& sendList = column->getSendListForTest();
    ASSERT_TRUE(sendList.canAddSend()) << "a freshly added track has a free send slot";
    auto& addSend = sendList.getAddSendAccessibilityComponentForTest();
    EXPECT_EQ(addSend.getTitle(), "Add send");
    EXPECT_TRUE(addSend.isVisible())
        << "the +Send row painted its own text with no component behind it at all -- this proxy is "
           "what makes it reachable";
}

// FRO301: rebuildKnobs() gave every send-level knob no setTitle at all, and
// juce::SliderParameterAttachment's own constructor overwrites textFromValueFunction with the
// param's raw getText() (see MixerSendList.cpp's own comment on the fix).
TEST(MixerAccessibilityTest, SendKnobHasATitleNamingItsTargetAndReadsDbValues) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    mc.simulateAddAudioTrackClick();

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    const auto bus = mixerPanel.createBus();
    ASSERT_NE(bus, juce::AudioProcessorGraph::NodeID{}) << "createBus must build a channel";

    // Fetch the strip column only AFTER createBus(): it reports the new channel through
    // onGraphMutated, and MainComponent answers that with a synchronous
    // bottomDock.rebuildMixer(), which destroys and recreates every column. A pointer taken before
    // the call is dangling by here -- addSendTo() on it read freed memory, which passed by luck on
    // macOS/Windows Release and segfaulted on roughly every other Linux Debug+coverage run
    // (std::function::operator() on the freed MixerSendList's onMutated, agentsynth#498/#500).
    auto* column = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(column, nullptr);
    column->getSendListForTest().addSendTo(bus);
    mixerPanel.rebuild();

    auto* refreshedColumn = mixerPanel.getStripColumnForTest(0);
    ASSERT_NE(refreshedColumn, nullptr);
    auto& sendList = refreshedColumn->getSendListForTest();
    ASSERT_TRUE(sendList.isAttachedForTest(0));
    auto* knob = sendList.getKnobForTest(0);
    ASSERT_NE(knob, nullptr);

    EXPECT_EQ(knob->getTitle(), "Send to Bus 1");
    ASSERT_TRUE((bool)knob->textFromValueFunction) << "a real sendNLevel param must be bound by now";
    EXPECT_EQ(knob->textFromValueFunction(-6.0), "-6.0 dB");
}

TEST(MixerAccessibilityTest, DirectColumnMakeChannelButtonHasAnExplicitTitle) {
    synth::ui::MixerDirectColumn direct;
    EXPECT_EQ(direct.getMakeChannelButtonForTest().getTitle(), "Make channel");
}

TEST(MixerAccessibilityTest, StatusBarButtonsHaveHumanTitlesNotInternalIds) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    auto& statusBar = mc.getStatusBar();
    EXPECT_EQ(statusBar.getMasterMuteButton().getTitle(), "Mute master")
        << "without setTitle(), ButtonAccessibilityHandler::getTitle() falls back to "
           "getButtonText(), which is the ctor's \"MasterMute\" component-name argument";
    EXPECT_EQ(statusBar.getTransportButton().getTitle(), "Play / Stop")
        << "same fallback leak, for \"statusBarTransportPlayStop\"";
}

TEST(MixerAccessibilityTest, BottomDockDetachButtonHasAHumanTitleNotItsComponentId) {
    MainComponent mc(std::make_unique<MockProviderMACT>());
    auto& detachButton = mc.getBottomDock().getDetachButtonForTest();
    const auto title = detachButton.getTitle();
    EXPECT_NE(title, juce::String("detachActiveTab"))
        << "the ctor's component-name argument must never leak through as the AX title";
    EXPECT_TRUE(title == "Open in window" || title == "Dock back") << "got: " << title;
}

TEST(MixerAccessibilityTest, DetachedWindowRealWiringGetsAThemedLookAndFeel) {
    // FRO228: DetachedPanelWindow itself was already covered (DetachedPanelWindowTests.cpp) for a
    // directly-supplied LookAndFeel -- this instead exercises the REAL wiring path a live detach
    // goes through (MainComponent -> BottomDockComponent -> DetachablePanelHost::setDetached()),
    // which is what actually regressed: MainComponent's delegating/test ctor used to assign
    // `lookAndFeel` in the constructor BODY, too late for bottomDock's own in-class initializer
    // (which captures `lookAndFeel`'s value while ITS OWN init list is still running) to see
    // anything but null -- every DetachablePanelHost this ctor ever built then held a permanently
    // null lookAndFeel_, however themed getLookAndFeelForTest() looked immediately afterwards.
    PersistedKeysGuard keysGuard({"mixerWindowBounds"});

    MainComponent mc(std::make_unique<MockProviderMACT>());
    mc.setSize(1400, 900);

    auto& mixerHost = mc.getBottomDock().getMixerHost();
    mixerHost.setDetached(true);
    auto* window = mixerHost.getDetachedWindowForTest();
    ASSERT_NE(window, nullptr);
    const auto& colors = mc.getLookAndFeelForTest().getTheme().colors;
    EXPECT_EQ(window->getBackgroundColour(), colors.bg0.withAlpha(1.0f).overlaidWith(colors.surface))
        << "the real setDetached(true) path must hand the window the app's OWN AppLookAndFeel, not "
           "a null one silently falling back to the stock ctor colour";
    // FRO228: the redock button VoiceOver lands on inside the window is the SAME borrowed
    // DetachablePanelHost::detachButton_ -- it must not be unnamed just because it's icon-only.
    EXPECT_TRUE(mixerHost.getDetachButton().getTitle() == "Dock back")
        << "got: " << mixerHost.getDetachButton().getTitle();
    mixerHost.setDetached(false);
}
