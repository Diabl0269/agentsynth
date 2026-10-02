// BottomDockMixerMirrorTests.cpp (docs/mixer/panel.md#placement-and-detachable-windows):
// "When a panel opens in its own window: move it there (default) / show it in both places". With
// "both", detaching the Mixer keeps its tab in the bottom dock's own strip (unlike the default
// "a detached tab leaves the strip") and opens a SECOND, independently live MixerPanelComponent in
// the window instead of reparenting the docked one -- MixerMirrorController.h's own class comment
// explains why a real second instance is required (a juce::Component has exactly one parent).
//
// Drives a real, off-screen MainComponent, same rig style as MixerPlacementControllerTests.cpp /
// MixerFaderDragTests.cpp; the real settings-file save/restore guard lives in
// Tests/TestSettingsHelpers.h.
#include "../../TestSettingsHelpers.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>

namespace {

class MockProviderBDMMT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockBDMMT"; }
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

using synth::test::PersistedKeysGuard;
using synth::test::userSettingsTestOptions;
using Tab = synth::ui::BottomDockComponent::Tab;

// Every key a test in this file writes to the real on-disk settings file -- restored exactly
// (including "absent") by PersistedKeysGuard's destructor, same contract as every other test file
// touching this file (docs/mixer/panel.md's "Restored window bounds" section explains why).
juce::StringArray keysUnderTest() {
    return {"detachedPanelBothPlaces", "mixerMirrorWindowBounds", "bottomDockActiveTab", "bottomDockTabOrder"};
}

void writePanelDetachMode(const juce::String& value) {
    juce::ApplicationProperties props;
    props.setStorageParameters(userSettingsTestOptions());
    auto* s = props.getUserSettings();
    ASSERT_NE(s, nullptr);
    s->setValue("detachedPanelBothPlaces", value);
    s->saveIfNeeded();
}

juce::MouseEvent sliderEvent(juce::Component& comp, juce::Point<float> pos, juce::Point<float> mouseDownPos,
                             bool wasDragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}

} // namespace

TEST(BottomDockMixerMirrorTests, DetachWithBothPlacesKeepsTheTabAndOpensASecondLiveView) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both"); // persisted BEFORE construction -- setApplicationProperties() reads it at launch

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    auto& mixerPanel = dock.getMixerPanel();

    const auto busId = mixerPanel.createBus();
    ASSERT_NE(busId, juce::AudioProcessorGraph::NodeID{});
    mixerPanel.rebuild();
    ASSERT_GT(mixerPanel.getColumnCount(), 0);

    ASSERT_TRUE(dock.isDetachBothPlacesEnabledForTest());
    dock.setActiveTab(Tab::Mixer);
    ASSERT_FALSE(dock.isMixerMirrorOpenForTest()) << "not open until the detach button is actually pressed";

    dock.getDetachButtonForTest().onClick();

    EXPECT_TRUE(dock.isMixerMirrorOpenForTest());
    EXPECT_FALSE(dock.getMixerHost().isDetached()) << "\"both places\", not moved -- the real host stays docked";
    EXPECT_EQ(dock.getMixerHost().getParentComponent(), &dock) << "the tab strip is still the mixer host's parent";
    EXPECT_TRUE(dock.isMixerTabActive()) << "the Mixer tab stays in the strip and active, unlike a real detach";

    auto* mirrorPanel = dock.getMixerMirrorPanelForTest();
    ASSERT_NE(mirrorPanel, nullptr);
    mirrorPanel->setSize(1400, 300);
    mirrorPanel->resized();
    EXPECT_EQ(mirrorPanel->getColumnCount(), mixerPanel.getColumnCount())
        << "the mirror is a second live view of the SAME graph, not an empty clone";

    // Pressing the detach button again closes the mirror (still "both places" -- the tab and the
    // docked view are never touched).
    dock.getDetachButtonForTest().onClick();
    EXPECT_FALSE(dock.isMixerMirrorOpenForTest());
    EXPECT_TRUE(dock.isMixerTabActive());
}

TEST(BottomDockMixerMirrorTests, AFaderGestureInEitherViewIsReflectedInBothAsExactlyOneUndoStep) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both");

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    auto& mixerPanel = dock.getMixerPanel();

    ASSERT_NE(mixerPanel.createBus(), juce::AudioProcessorGraph::NodeID{});
    mixerPanel.rebuild();
    mixerPanel.setSize(1400, 300);
    mixerPanel.resized();
    ASSERT_GT(mixerPanel.getColumnCount(), 0);

    dock.setActiveTab(Tab::Mixer);
    dock.getDetachButtonForTest().onClick();
    ASSERT_TRUE(dock.isMixerMirrorOpenForTest());
    auto* mirrorPanel = dock.getMixerMirrorPanelForTest();
    ASSERT_NE(mirrorPanel, nullptr);
    mirrorPanel->setSize(1400, 300);
    mirrorPanel->resized();
    ASSERT_EQ(mirrorPanel->getColumnCount(), mixerPanel.getColumnCount());

    auto* primaryColumn = mixerPanel.getStripColumnForTest(0);
    auto* mirrorColumn = mirrorPanel->getStripColumnForTest(0);
    ASSERT_NE(primaryColumn, nullptr);
    ASSERT_NE(mirrorColumn, nullptr);
    auto& primarySlider = primaryColumn->getFaderForTest().getSlider();
    auto& mirrorSliderBefore = mirrorColumn->getFaderForTest().getSlider();
    const double before = primarySlider.getValue();
    EXPECT_DOUBLE_EQ(mirrorSliderBefore.getValue(), before) << "both views start bound to the same live parameter";

    const int serialBeforeDrag = mc.getUndoManager().getEditSerial();

    // A REAL drag through MixerFaderSlider's own mouseDown/mouseDrag/mouseUp overrides -- the same
    // "synthesized real mouse path, never juce::Slider's own internal drag machinery" convention
    // MixerFaderDragTests.cpp documents and uses.
    const auto centre = primarySlider.getLocalBounds().getCentre().toFloat();
    const auto draggedTo = centre.translated(0.0f, -30.0f);
    primarySlider.mouseDown(sliderEvent(primarySlider, centre, centre, false));
    primarySlider.mouseDrag(sliderEvent(primarySlider, draggedTo, centre, true));
    primarySlider.mouseUp(sliderEvent(primarySlider, draggedTo, centre, true));

    const double after = primarySlider.getValue();
    EXPECT_NE(after, before) << "the drag actually moved the primary's own fader";
    auto& mirrorSliderAfter = mirrorColumn->getFaderForTest().getSlider();
    EXPECT_DOUBLE_EQ(mirrorSliderAfter.getValue(), after)
        << "the mirror's fader reflects the primary's drag -- same juce::AudioParameterFloat, same "
           "juce::SliderParameterAttachment notification, no extra wiring needed";

    // Exactly one undo step, not two: AppUndoManager::capturedBeforeState is a single shared field
    // guarded by isVoid() (see MixerMirrorController.h's class comment) -- a second start/end pair
    // from the mirror's OWN parameter listener (bound to the same param) is a harmless no-op.
    EXPECT_EQ(mc.getUndoManager().getEditSerial() - serialBeforeDrag, 1);
}

TEST(BottomDockMixerMirrorTests, SwitchingBackToMoveWhileBothPlacesIsOpenClosesTheMirrorAndDetachesTheRealHost) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both");

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();

    dock.setActiveTab(Tab::Mixer);
    dock.getDetachButtonForTest().onClick();
    ASSERT_TRUE(dock.isMixerMirrorOpenForTest());
    ASSERT_FALSE(dock.getMixerHost().isDetached());

    // Live settings-file switch back to the default, same as a Preferences change while the app is
    // running (MainComponent::changeListenerCallback -> applyDetachBothPlacesPreference()). Through
    // mc's OWN ApplicationProperties, not a second instance pointed at the same file -- a
    // juce::PropertiesFile caches its values in memory, so a write from an unrelated instance (as
    // writePanelDetachMode() does for the "persist before construction" tests above) would never be
    // seen by dock's already-loaded appProperties_ until the real Settings window, sharing the SAME
    // instance, ever produces a write worth re-reading.
    auto* liveSettings = mc.getAppPropertiesForTest().getUserSettings();
    ASSERT_NE(liveSettings, nullptr);
    liveSettings->setValue("detachedPanelBothPlaces", "move");
    liveSettings->saveIfNeeded();
    dock.applyDetachBothPlacesPreference();

    EXPECT_FALSE(dock.isMixerMirrorOpenForTest());
    EXPECT_TRUE(dock.getMixerHost().isDetached()) << "FRO333's own behaviour: the real host detaches";
    EXPECT_FALSE(dock.isMixerTabActive()) << "the tab leaves the panel again -- applyTabVisibility() falls back";
}

// A real click on either view's own M button must update the OTHER view's M button
// the instant it happens -- not "on the next rebuild()". The notification path is
// MixerColumnComponent::onLiveStateChanged -> MixerPanelComponent::onLiveMixerStateChanged, cross-
// wired symmetrically in MixerMirrorController::open() (mirror -> dock) and BottomDockComponent's
// constructor (dock -> mirror) -- see MixerMirrorController.h's class comment.
TEST(BottomDockMixerMirrorTests, ClickingMuteInEitherViewUpdatesTheOtherViewsMuteButton) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both");

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    auto& mixerPanel = dock.getMixerPanel();

    ASSERT_NE(mixerPanel.createBus(), juce::AudioProcessorGraph::NodeID{});
    mixerPanel.rebuild();
    mixerPanel.setSize(1400, 300);
    mixerPanel.resized();
    ASSERT_GT(mixerPanel.getColumnCount(), 0);

    dock.setActiveTab(Tab::Mixer);
    dock.getDetachButtonForTest().onClick();
    ASSERT_TRUE(dock.isMixerMirrorOpenForTest());
    auto* mirrorPanel = dock.getMixerMirrorPanelForTest();
    ASSERT_NE(mirrorPanel, nullptr);
    mirrorPanel->setSize(1400, 300);
    mirrorPanel->resized();
    ASSERT_EQ(mirrorPanel->getColumnCount(), mixerPanel.getColumnCount());

    auto* primaryColumn = mixerPanel.getStripColumnForTest(0);
    auto* mirrorColumn = mirrorPanel->getStripColumnForTest(0);
    ASSERT_NE(primaryColumn, nullptr);
    ASSERT_NE(mirrorColumn, nullptr);
    ASSERT_FALSE(primaryColumn->getMuteButtonForTest().getToggleState());
    ASSERT_FALSE(mirrorColumn->getMuteButtonForTest().getToggleState());

    // A real click on the MIRROR's own M button.
    mirrorColumn->getMuteButtonForTest().onClick();

    EXPECT_TRUE(mirrorColumn->getMuteButtonForTest().getToggleState());
    EXPECT_TRUE(primaryColumn->getMuteButtonForTest().getToggleState())
        << "the dock's own M button lights up the instant the mirror's is clicked, not on the next rebuild()";

    // And the reverse direction, through the opposite half of the same cross-wire
    // (BottomDockComponent's constructor: mixer_.onLiveMixerStateChanged -> mixerMirror_.refreshLiveVisualsIfOpen()).
    primaryColumn->getMuteButtonForTest().onClick();
    EXPECT_FALSE(primaryColumn->getMuteButtonForTest().getToggleState());
    EXPECT_FALSE(mirrorColumn->getMuteButtonForTest().getToggleState());
}

// The mirror
// window read "Pan: Comp." while the dock still read "Pan: Bal." at the same moment. Master's
// onLiveStateChanged (fired from setPanLaw(), MixerMasterColumn.cpp) closes it the same way mute
// does above.
TEST(BottomDockMixerMirrorTests, PanLawLabelIsIdenticalInBothViewsAfterAChangeInEither) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both");

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    auto& mixerPanel = dock.getMixerPanel();

    // A brand-new, still-empty patch has no Master node yet (GraphEditor::newPatch() seeds only an
    // Audio Output -- Master is spliced in on the first channel strip), so MixerMasterColumn::
    // setColumn() -- and this fix's refreshPanLawButton() call inside it -- never runs until one
    // exists. createBus() splices Master in, same precondition the mute/undo tests above use.
    ASSERT_NE(mixerPanel.createBus(), juce::AudioProcessorGraph::NodeID{});
    mixerPanel.rebuild();

    dock.setActiveTab(Tab::Mixer);
    dock.getDetachButtonForTest().onClick();
    ASSERT_TRUE(dock.isMixerMirrorOpenForTest());
    auto* mirrorPanel = dock.getMixerMirrorPanelForTest();
    ASSERT_NE(mirrorPanel, nullptr);

    auto* primaryMaster = mixerPanel.getMasterColumnForTest();
    auto* mirrorMaster = mirrorPanel->getMasterColumnForTest();
    ASSERT_NE(primaryMaster, nullptr);
    ASSERT_NE(mirrorMaster, nullptr);
    // A brand-new project always starts Compensated (MainComponent::clearTimelineForNewPatch's
    // caller) -- both views' rebuild() picks that up already (MixerMasterColumn::setColumn() now
    // re-reads the engine's law on every rebuild, another stale-label gap this same verification pass
    // found and closed alongside the live cross-view case below).
    ASSERT_EQ(primaryMaster->getPanLawButtonForTest().getButtonText(), "Pan: Comp.");
    ASSERT_EQ(mirrorMaster->getPanLawButtonForTest().getButtonText(), "Pan: Comp.");

    // Picks "Balance" via the MIRROR's own pan-law menu -- same click-driven menu idiom
    // MixerMasterColumnPanLawTests.cpp uses for the single-view case.
    juce::PopupMenu captured;
    mirrorMaster->setShowPanLawMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    mirrorMaster->getPanLawButtonForTest().onClick();
    mirrorMaster->setShowPanLawMenuHookForTest(nullptr);

    bool picked = false;
    juce::PopupMenu::MenuItemIterator it(captured);
    while (it.next())
        if (it.getItem().text.startsWith("Balance")) {
            it.getItem().action();
            picked = true;
            break;
        }
    ASSERT_TRUE(picked);

    EXPECT_EQ(mirrorMaster->getPanLawButtonForTest().getButtonText(), "Pan: Bal.");
    EXPECT_EQ(primaryMaster->getPanLawButtonForTest().getButtonText(), "Pan: Bal.")
        << "both views read the same AudioEngine::getMixerPanLaw(), but only a live refresh actually "
           "repaints the OTHER view's cached button text -- this is the reported bug";
}

// A regression pin, not a new mechanism -- undo of a mute is a graph-snapshot
// restore (AppUndoManager::pushSnapshotFromCapture), which already reaches BOTH views through the
// pre-existing unbind-before/rebuild-after hooks (unbindAllMixerViews()/rebuildIfUnboundMixerViews(),
// wired to GraphEditor::onBeforeDetachAllModuleComponents/onGraphStructureChanged) that
// already extends to the mirror -- see MixerPanelUndoUnbindTests.cpp for the same
// undo-triggers-unbind-then-rebuild contract on the single-view case.
TEST(BottomDockMixerMirrorTests, UndoOfAMuteRestoresBothViews) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both");

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    mc.newPatchForTest();
    auto& dock = mc.getBottomDock();
    auto& mixerPanel = dock.getMixerPanel();

    ASSERT_NE(mixerPanel.createBus(), juce::AudioProcessorGraph::NodeID{});
    mixerPanel.rebuild();
    mixerPanel.setSize(1400, 300);
    mixerPanel.resized();
    ASSERT_GT(mixerPanel.getColumnCount(), 0);

    dock.setActiveTab(Tab::Mixer);
    dock.getDetachButtonForTest().onClick();
    ASSERT_TRUE(dock.isMixerMirrorOpenForTest());
    auto* mirrorPanel = dock.getMixerMirrorPanelForTest();
    ASSERT_NE(mirrorPanel, nullptr);
    mirrorPanel->setSize(1400, 300);
    mirrorPanel->resized();
    ASSERT_EQ(mirrorPanel->getColumnCount(), mixerPanel.getColumnCount());

    auto* primaryColumn = mixerPanel.getStripColumnForTest(0);
    auto* mirrorColumn = mirrorPanel->getStripColumnForTest(0);
    ASSERT_NE(primaryColumn, nullptr);
    ASSERT_NE(mirrorColumn, nullptr);

    primaryColumn->getMuteButtonForTest().onClick();
    ASSERT_TRUE(primaryColumn->getMuteButtonForTest().getToggleState());
    ASSERT_TRUE(mirrorColumn->getMuteButtonForTest().getToggleState());

    ASSERT_TRUE(mc.getUndoManager().undo());

    // The restore destroys and rebuilds both views' column sets -- re-fetch, never reuse the
    // pre-undo pointers (same convention as MixerPanelUndoUnbindTests.cpp).
    auto* primaryColumnAfterUndo = mixerPanel.getStripColumnForTest(0);
    auto* mirrorColumnAfterUndo = mirrorPanel->getStripColumnForTest(0);
    ASSERT_NE(primaryColumnAfterUndo, nullptr);
    ASSERT_NE(mirrorColumnAfterUndo, nullptr);
    EXPECT_FALSE(primaryColumnAfterUndo->getMuteButtonForTest().getToggleState());
    EXPECT_FALSE(mirrorColumnAfterUndo->getMuteButtonForTest().getToggleState());
}

TEST(BottomDockMixerMirrorTests, PreferencePersistsAcrossApplicationPropertiesReload) {
    PersistedKeysGuard guard(keysUnderTest());
    writePanelDetachMode("both");

    MainComponent mc(std::make_unique<MockProviderBDMMT>());
    mc.setSize(1400, 900);
    EXPECT_TRUE(mc.getBottomDock().isDetachBothPlacesEnabledForTest());

    // A second, independent MainComponent reading the SAME on-disk settings file must restore it too.
    MainComponent mc2(std::make_unique<MockProviderBDMMT>());
    mc2.setSize(1400, 900);
    EXPECT_TRUE(mc2.getBottomDock().isDetachBothPlacesEnabledForTest());
}
