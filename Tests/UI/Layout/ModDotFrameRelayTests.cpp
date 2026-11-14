// ModDotFrameRelayTests.cpp -- the shortcut-relay rule for the panels that open through launchInFrame
// (ModDotPanelFrame, a window of its own): the output jack's connections panel, the knob's mod dot panel and the
// modulation curve popover. Each opens through its real owner in a real MainComponent with no test launcher, and Space,
// Cmd+Z, Cmd+Shift+Z and Cmd+J are pressed through the frame's real key order (the focused child, its parents, the
// frame's own handler). A source scan fails on a window put on the desktop without a relay.
#include "../../TestWait.h"
#include "../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "../SourceScan.h"
#include "MainPopoverKeys.h"
#include "Modules/LFOModule.h"
#include "Modules/VCAModule.h"
#include "NativeWindowTestGuard.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"
#include <regex>
#include <set>

using synth::ui::ModDotPanelFrame;

namespace {

const auto kCmd = juce::ModifierKeys::commandModifier;
const juce::KeyPress kToggleDock('j', juce::ModifierKeys(kCmd), 0);

GraphEditor* findEditor(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (auto* hit = dynamic_cast<GraphEditor*>(child))
            return hit;
        if (auto* deeper = findEditor(*child))
            return deeper;
    }
    return nullptr;
}

ModDotPanelFrame* findFrame() {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* frame = dynamic_cast<ModDotPanelFrame*>(desktop.getComponent(i)))
            if (!frame->isClosing())
                return frame;
    return nullptr;
}

void closeAllFrames() {
    while (auto* frame = findFrame())
        frame->finishClosingNow(), frame->close(), frame->finishClosingNow();
    main_popover::pump(80);
}

/** A key's real path in the frame's window: the focused child, each parent up to the frame, then the frame's handler.
 */
bool dispatchInFrame(ModDotPanelFrame& frame, juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (target->keyPressed(press))
            return true;
        if (target == &frame)
            break;
    }
    return false;
}

class FrameRelay : public main_popover::MainPopoverTest {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        MainPopoverTest::SetUp();
        synth::testmode::setAppInFrontForTest(true);
        auto& graph = engine_->getGraph();
        lfo_ = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        vca_ = graph.addNode(std::make_unique<VCAModule>())->nodeID;
        synth::CardParamItem gain;
        gain.paramId = "gain";
        gain.widget = synth::CardWidget::Knob;
        synth::CardSection section;
        section.id = "main";
        section.items.emplace_back(gain);
        synth::CardLayout layout;
        layout.sections = {section};
        synth::setCardLayoutOverride(graph, nullptr, vca_, layout);
        for (const auto& t :
             dynamic_cast<ModuleBase*>(graph.getNodeForId(vca_)->getProcessor())->getModulationTargets())
            if (t.paramId == "gain")
                gainChannel_ = t.channelIndex;
        atten_ = engine_->addModRouting(lfo_, 0, vca_, gainChannel_);
        editor_ = findEditor(*mc_);
        ASSERT_NE(editor_, nullptr);
        editor_->updateComponents();
        sizeModuleComponents(*editor_);
        editor_->timerCallback();
        mc_->simulateAddAudioTrackClick(); // the undo step Cmd+Z takes back
        main_popover::pump();
    }
    void TearDown() override {
        closeAllFrames();
        synth::testmode::setAppInFrontForTest(std::nullopt);
        MainPopoverTest::TearDown();
    }

    ModuleComponent* card(juce::AudioProcessorGraph::NodeID id) {
        for (auto* c : editor_->getModuleComponents())
            if (c != nullptr && c->getNodeId() == id)
                return c;
        return nullptr;
    }

    void openJackPanel() {
        auto* lfoCard = card(lfo_);
        ASSERT_NE(lfoCard, nullptr);
        const auto centre = lfoCard->getPortCenter(0, false);
        editor_->getPortPanel().open(*lfoCard, juce::Rectangle<int>(0, 0, 14, 14).withCentre(centre),
                                     synth::ui::PortRef{lfo_, 0, false, false});
    }
    void openKnobPanel() {
        auto* vcaCard = card(vca_);
        ASSERT_NE(vcaCard, nullptr);
        juce::Slider* knob = nullptr;
        for (auto* child : vcaCard->getChildren())
            if (auto* s = dynamic_cast<juce::Slider*>(child); s != nullptr && s->getComponentID() == "Level")
                knob = s;
        ASSERT_NE(knob, nullptr);
        editor_->getModDot().openPopover(vca_, gainChannel_, *knob);
    }
    void openCurvePopover() {
        auto* vcaCard = card(vca_);
        ASSERT_NE(vcaCard, nullptr);
        synth::ui::openModCurvePopover(*editor_, atten_, *vcaCard, vcaCard->getScreenBounds().withSize(12, 12), true);
    }

    /** Space plays and stops, Cmd+J toggles the bottom panel, Cmd+Z undoes and Cmd+Shift+Z redoes: each through the
     *  open frame, which stays open (the connection is still there). */
    void expectKeysReachTheApp(const std::function<void()>& open, const std::string& name) {
        SCOPED_TRACE(name);
        open();
        auto* frame = findFrame();
        ASSERT_NE(frame, nullptr) << name << ": nothing opened";
        ASSERT_NE(frame->onAppShortcut, nullptr) << name << ": the window has no route to the app's shortcuts";
        juce::Component* origin = frame;
        for (auto* stop : synth::ui::focusableDescendants(*frame))
            if (dynamic_cast<juce::TextEditor*>(stop) == nullptr &&
                stop->findParentComponentOfClass<juce::TextEditor>() == nullptr) {
                origin = stop;
                break;
            }

        ASSERT_FALSE(playing()) << name;
        EXPECT_TRUE(dispatchInFrame(*frame, *origin, main_popover::kSpace)) << name << ": Space";
        EXPECT_TRUE(playingBecomes(true)) << name << ": Space did not start playback";
        EXPECT_TRUE(dispatchInFrame(*frame, *origin, main_popover::kSpace));
        EXPECT_TRUE(playingBecomes(false)) << name << ": a second Space did not stop playback";

        const bool dockWas = mc_->isBottomDockConfiguredVisible();
        EXPECT_TRUE(dispatchInFrame(*frame, *origin, kToggleDock)) << name << ": Cmd+J";
        EXPECT_TRUE(synth::test::waitUntil([&] { return mc_->isBottomDockConfiguredVisible() != dockWas; }))
            << name << ": Cmd+J did not toggle the bottom panel";

        ASSERT_TRUE(mc_->getUndoManager().canUndo());
        EXPECT_TRUE(dispatchInFrame(*frame, *origin, main_popover::kUndo)) << name << ": Cmd+Z";
        EXPECT_TRUE(synth::test::waitUntil([&] { return undone(); })) << name << ": Cmd+Z did not undo";
        EXPECT_TRUE(dispatchInFrame(*frame, *origin, main_popover::kRedo)) << name << ": Cmd+Shift+Z";
        EXPECT_TRUE(synth::test::waitUntil([&] { return !undone(); })) << name << ": Cmd+Shift+Z did not redo";
        EXPECT_NE(findFrame(), nullptr) << name << ": a key closed the panel";

        // Keys that act on the canvas selection stay with the panel.
        for (const auto& key :
             {juce::KeyPress('a', juce::ModifierKeys(kCmd), 0), juce::KeyPress('c', juce::ModifierKeys(kCmd), 0),
              juce::KeyPress(juce::KeyPress::deleteKey)})
            EXPECT_FALSE(frame->keyPressed(key)) << name << ": " << key.getTextDescription();
        closeAllFrames();
    }

    juce::AudioProcessorGraph::NodeID lfo_, vca_, atten_;
    int gainChannel_ = -1;
    GraphEditor* editor_ = nullptr;
};

} // namespace

// Regression test for FRO1759: Cmd+J (and Space, Cmd+Z) did nothing while a mod dot or output jack menu was open,
// because the frame is its own top-level window and declined every key but Esc.
TEST_F(FrameRelay, TheOutputJackPanelTakesSpaceUndoRedoAndCmdJFromTheApp) {
    expectKeysReachTheApp([&] { openJackPanel(); }, "output jack panel");
}

TEST_F(FrameRelay, TheKnobModDotPanelTakesSpaceUndoRedoAndCmdJFromTheApp) {
    expectKeysReachTheApp([&] { openKnobPanel(); }, "knob mod dot panel");
}

TEST_F(FrameRelay, TheModulationCurvePopoverTakesSpaceUndoRedoAndCmdJFromTheApp) {
    expectKeysReachTheApp([&] { openCurvePopover(); }, "curve popover");
}

TEST_F(FrameRelay, EscStillClosesThePanel) {
    openJackPanel();
    auto* frame = findFrame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(frame->isClosing());
}

// A window put on the desktop with showOnDesktop() outside ModDotPanelFrame itself is registered here with its relay:
// a new one fails by name until its keys reach the app (launchInFrame wires the relay for every caller of it).
TEST(FrameRelayScan, EveryFramedWindowGetsTheAppShortcutRelay) {
    using synth::test::source_scan::sourceTexts;
    const std::regex shows(R"(\bshowOnDesktop\s*\()");
    std::set<std::string> ownersOfRelay, windows;
    for (const auto& [file, text] : sourceTexts()) {
        if (std::regex_search(text, shows) && file.find("NoteLengthMenu/") == std::string::npos &&
            file != "Source/UI/Graph/ModDot/ModDotPanelFrame.cpp" &&
            file != "Source/UI/Graph/ModDot/ModDotPanelFrame.h" &&
            file != "Source/UI/Graph/ModDot/ModDotPanelLaunch.cpp")
            windows.insert(file);
        if (file == "Source/UI/Graph/ModDot/ModDotPanelLaunch.cpp" &&
            text.find("makeAppShortcutRelay") != std::string::npos && text.find("onAppShortcut") != std::string::npos &&
            std::regex_search(text, shows))
            ownersOfRelay.insert(file);
    }
    EXPECT_EQ(ownersOfRelay.size(), 1u) << "launchInFrame must wire frame->onAppShortcut with makeAppShortcutRelay";
    EXPECT_TRUE(windows.empty()) << "a window opened with showOnDesktop() outside launchInFrame has no relay: "
                                 << (windows.empty() ? "" : *windows.begin());
}
