// PanelAnimationAndLoadingTests.cpp
//
// Headless gtest coverage for UI Phase 5 deliverables owned by Owner D:
//   1. Panel target-bounds geometry (pure, no animation)
//   2. AI cancel state-transition (waiting → cancel resets flags & controls)
//   3. Tooltip-hint formatting (pure string helper)
//   4. Paint smoke tests for MainComponent and AIChatComponent
//
// Does NOT register in CMakeLists.txt — add it manually when ready.
// Mirrors the pattern in MainComponentTests.cpp and AIChatComponentTests.cpp.

#include "../Source/AI/AIProvider.h"
#include "../Source/AudioEngine.h"
#include "../Source/UI/AIChatComponent.h"
#include "../Source/UI/UIAnimation.h"
#include "MainComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
// Shared mock provider (same pattern as MainComponentTests.cpp)
// ============================================================================

class MockProviderPAL : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPAL"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        // Intentionally never completes on its own, so a test can decide how the request ends.
        pendingCallback = std::move(callback);
        return RequestId{++lastRequestId};
    }

    // Records what the UI asked us to cancel, so tests can prove the Cancel button and the Escape
    // key reach the provider instead of only tidying up the UI.
    void cancel(RequestId requestId) override {
        cancelledIds.push_back(requestId.value);
        if (!pendingCallback)
            return;

        auto callback = std::move(pendingCallback);
        pendingCallback = nullptr;

        AIResponse response;
        response.success = false;
        response.error.kind = AIErrorKind::Cancelled;
        response.error.message = "Request cancelled.";
        callback(response);
    }

    /** Resolves the held request normally, so a test can check what happens after a real response
        rather than only after a cancel. */
    void completePending(const juce::String& content) {
        if (!pendingCallback)
            return;

        auto callback = std::move(pendingCallback);
        pendingCallback = nullptr;

        AIResponse response;
        response.success = true;
        response.content = content;
        callback(response);
    }

    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

    std::vector<uint64_t> cancelledIds;
    uint64_t lastRequestId = 0;

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
    CompletionCallback pendingCallback;
};

// Fixture helper: owns an ApplicationProperties configured for tests.
struct TestPropsOwner {
    juce::ApplicationProperties props;
    TestPropsOwner() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "PALTest";
        opts.filenameSuffix = "test";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(opts);
    }
};

// Helper to reset panel-visibility keys so tests are hermetic.
static void resetPanelKeys() {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Agent Synth";
    opts.folderName = "Agent Synth";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;

    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    if (auto* s = props.getUserSettings()) {
        s->setValue("librarySidebarVisible", "1");
        s->setValue("aiPanelVisible", "0");
        s->setValue("timelinePanelVisible", "0");
        // Removed, not defaulted: absent is what makes the theme metric the default height.
        s->removeValue(MainComponent::kTimelinePanelHeightKey);
        s->saveIfNeeded();
    }
}

// ============================================================================
// 1. Panel target-bounds geometry
//    resized() is the one geometry authority: it derives every panel's size
//    from that panel's [0..1] open fraction (see section 5 below).
// ============================================================================

class PanelBoundsTest : public ::testing::Test {
protected:
    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

TEST_F(PanelBoundsTest, BothPanelsVisible_LibraryAndAiOccupyExpectedWidth) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);

    // Drive the real toggle handlers and read the resulting layout back.
    // Default: library visible, AI hidden. Show AI.
    mc.simulateToggleAiPanelClick(); // now both visible

    const int libW = mc.getGraphEditor().getBounds().getX(); // library width
    EXPECT_EQ(libW, 200) << "Library sidebar should be 200 px wide";

    const int aiRight = mc.getGraphEditor().getBounds().getRight();
    EXPECT_LT(aiRight, 1600 - 1) << "AI panel should push graph editor left of window right edge";
}

TEST_F(PanelBoundsTest, LibraryHidden_GraphEditorStartsAtX0) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);

    mc.simulateToggleLibraryClick(); // hide library
    // Graph editor must start at x=0 when library is hidden.
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 0);
}

TEST_F(PanelBoundsTest, AiPanelHidden_GraphEditorReachesRightEdge) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);

    // AI panel is hidden by default; library is visible.
    ASSERT_FALSE(mc.isAiPanelConfiguredVisible());
    // Graph editor right edge == window width (no AI panel taking space on right).
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), 1600);
}

TEST_F(PanelBoundsTest, BothPanelsHidden_GraphEditorFillsFullContentArea) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);

    mc.simulateToggleLibraryClick(); // hide library
    ASSERT_FALSE(mc.isLibraryConfiguredVisible());
    ASSERT_FALSE(mc.isAiPanelConfiguredVisible());

    const auto ge = mc.getGraphEditor().getBounds();
    EXPECT_EQ(ge.getX(), 0);
    EXPECT_EQ(ge.getRight(), 1600);
}

// ============================================================================
// 1b. Fraction-driven panel layout (the slide itself).
//
// resized() derives each panel's size from that panel's [0..1] open fraction, and a toggle only
// moves the fraction (MainComponent::beginPanelSlide -> synth::ui::PanelSlide). The component is
// never added to a real window here, so isShowing() is false and every toggle takes the SNAP
// path — which is exactly the synchronous-final-bounds contract section 1 above relies on, with
// no message pump anywhere in this file. The tween's own mid-flight geometry is exercised by
// writing a fraction through the test seam and reading the bounds back, the same idiom
// PianoRollTests uses for the scale panel's slide.
// ============================================================================

using SlidingPanel = MainComponent::SlidingPanel;

class PanelSlideLayoutTest : public ::testing::Test {
protected:
    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

TEST_F(PanelSlideLayoutTest, AnOffScreenToggleLandsSynchronouslyAndStartsNoAnimation) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    ASSERT_FALSE(mc.isShowing()) << "test premise: headless, no real window";

    mc.simulateToggleAiPanelClick();
    EXPECT_FLOAT_EQ(mc.getPanelOpenProgressForTest(SlidingPanel::AiChat), 1.0f);
    EXPECT_FALSE(mc.isPanelSlideAnimatingForTest()) << "no VBlank reaches an off-screen component";
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), 1600 - 300);

    mc.simulateToggleAiPanelClick();
    EXPECT_FLOAT_EQ(mc.getPanelOpenProgressForTest(SlidingPanel::AiChat), 0.0f);
    EXPECT_FALSE(mc.isPanelSlideAnimatingForTest());
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), 1600);
}

// The no-layout-drift oracle: at rest the fractions must reproduce the old binary carve exactly.
TEST_F(PanelSlideLayoutTest, EndpointFractionsReproduceTheBinaryLayoutExactly) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    mc.simulateToggleAiPanelClick(); // both side panels open
    ASSERT_TRUE(mc.isLibraryConfiguredVisible());

    const auto openCanvas = mc.getGraphEditor().getBounds();
    EXPECT_EQ(openCanvas.getX(), 200) << "librarySidebarWidth";
    EXPECT_EQ(openCanvas.getRight(), 1600 - 300) << "aiPanelWidth";

    // Both fractions written directly, without touching the visibility flags: same numbers.
    mc.setPanelOpenProgressForTest(SlidingPanel::Library, 1.0f);
    mc.setPanelOpenProgressForTest(SlidingPanel::AiChat, 1.0f);
    EXPECT_EQ(mc.getGraphEditor().getBounds(), openCanvas);

    mc.setPanelOpenProgressForTest(SlidingPanel::Library, 0.0f);
    mc.setPanelOpenProgressForTest(SlidingPanel::AiChat, 0.0f);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 0);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), 1600);
}

TEST_F(PanelSlideLayoutTest, AMidSlideFractionScalesThePanelAndGivesTheRestToTheCanvas) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    mc.simulateToggleAiPanelClick();

    mc.setPanelOpenProgressForTest(SlidingPanel::AiChat, 0.5f);
    EXPECT_EQ(mc.getAiChatComponent().getBounds().getWidth(), 150);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), 1600 - 150)
        << "the canvas must follow the panel every frame, not only at the endpoints";

    mc.setPanelOpenProgressForTest(SlidingPanel::Library, 0.25f);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 50);
}

TEST_F(PanelSlideLayoutTest, TheTimelineSlidesAgainstAPinnedBottomEdge) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    mc.simulateToggleTimelineClick();
    ASSERT_EQ(mc.getTimelinePanel().getBounds().getHeight(), 220);

    mc.setPanelOpenProgressForTest(SlidingPanel::Timeline, 0.5f);
    const auto panelBounds = mc.getTimelinePanel().getBounds();
    EXPECT_EQ(panelBounds.getHeight(), 110);
    EXPECT_EQ(panelBounds.getWidth(), 1600) << "full width at every point of the slide";
    EXPECT_EQ(panelBounds.getBottom(), mc.getStatusBar().getBounds().getY()) << "bottom edge pinned";
    EXPECT_EQ(mc.getGraphEditor().getBounds().getBottom(), panelBounds.getY());
}

// The jump this whole design removes: the tween starts from the fraction's CURRENT value, so
// catching a slide half-open and toggling back reverses from there instead of restarting.
TEST_F(PanelSlideLayoutTest, TogglingMidSlideReversesFromTheCurrentFraction) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    mc.simulateToggleAiPanelClick(); // opens; headless -> snaps to 1
    ASSERT_FLOAT_EQ(mc.getPanelOpenProgressForTest(SlidingPanel::AiChat), 1.0f);

    // Stand in for the VBlank frame that would have left the slide here, had one run.
    mc.setPanelOpenProgressForTest(SlidingPanel::AiChat, 0.4f);
    mc.simulateToggleAiPanelClick(); // request CLOSE now
    EXPECT_FLOAT_EQ(mc.getPanelSlideStartForTest(SlidingPanel::AiChat), 0.4f)
        << "the reversal's start point is the CURRENT width, never an extreme";
    EXPECT_FLOAT_EQ(mc.getPanelOpenProgressForTest(SlidingPanel::AiChat), 0.0f) << "still headless: lands at once";
}

// One driver for all three panels: a toggle must carry any slide already in flight to ITS target
// too, rather than stranding it half-open for the rest of the session.
TEST_F(PanelSlideLayoutTest, ASecondToggleCarriesAnUnfinishedSlideToItsOwnTarget) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    ASSERT_TRUE(mc.isLibraryConfiguredVisible());

    // The library is logically open but caught mid-slide; now the AI panel is toggled.
    mc.setPanelOpenProgressForTest(SlidingPanel::Library, 0.3f);
    mc.simulateToggleAiPanelClick();

    EXPECT_FLOAT_EQ(mc.getPanelOpenProgressForTest(SlidingPanel::Library), 1.0f)
        << "the library's own slide finished with the shared driver's restart";
    EXPECT_EQ(mc.getGraphEditor().getBounds().getX(), 200);
    EXPECT_FALSE(mc.isPanelSlideAnimatingForTest());
}

// resized() must be correct whenever it runs — that is what the fractions buy. A window resize
// mid-slide re-derives the same proportions instead of snapping the panel open or shut.
TEST_F(PanelSlideLayoutTest, AWindowResizeMidSlideKeepsTheFractionsProportions) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    mc.simulateToggleAiPanelClick();
    mc.setPanelOpenProgressForTest(SlidingPanel::AiChat, 0.5f);

    mc.setSize(1200, 800);
    EXPECT_EQ(mc.getAiChatComponent().getBounds().getWidth(), 150);
    EXPECT_EQ(mc.getAiChatComponent().getBounds().getRight(), 1200);
    EXPECT_EQ(mc.getGraphEditor().getBounds().getRight(), 1200 - 150);
}

// ============================================================================
// 2. AI cancel state-transition
//    Sending a message (with a never-completing mock) puts the component into
//    the "waiting" state.  Triggering the cancel button must:
//      - reset isWaiting() to false
//      - show the cancel button before cancel, hide it after
//      - re-enable the send button and input field
// ============================================================================

class AICancelTest : public ::testing::Test {
protected:
};

TEST_F(AICancelTest, CancelButtonHiddenBeforeSend) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockProviderPAL>());

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    EXPECT_FALSE(chat.isCancelVisible());
    EXPECT_FALSE(chat.isWaiting());
}

TEST_F(AICancelTest, CancelButtonVisibleAfterSend) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockProviderPAL>());

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    // Set text and send — MockProviderPAL never calls back, so we stay in waiting state.
    for (auto* child : chat.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            editor->setText("hello");
    }
    chat.triggerSend();

    EXPECT_TRUE(chat.isWaiting());
    EXPECT_TRUE(chat.isCancelVisible());
}

TEST_F(AICancelTest, CancelResetsWaitingStateAndHidesCancelButton) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockProviderPAL>());

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    // Enter waiting state.
    for (auto* child : chat.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            editor->setText("hello");
    }
    chat.triggerSend();
    ASSERT_TRUE(chat.isWaiting());

    // Trigger cancel synchronously via the test hook (triggerClick posts async via MessageManager).
    chat.simulateCancelClick();

    EXPECT_FALSE(chat.isWaiting());
    EXPECT_FALSE(chat.isCancelVisible());
}

TEST_F(AICancelTest, SendButtonReenabledAfterCancel) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockProviderPAL>());

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    for (auto* child : chat.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            editor->setText("hello");
    }
    chat.triggerSend();

    // Trigger cancel synchronously via the test hook (triggerClick posts async via MessageManager).
    chat.simulateCancelClick();

    // Send button must be re-enabled.
    juce::Button* sendBtn = nullptr;
    for (auto* child : chat.getChildren()) {
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            if (btn->getButtonText() == "Send")
                sendBtn = btn;
    }
    ASSERT_NE(sendBtn, nullptr);
    EXPECT_TRUE(sendBtn->isEnabled());
}

// ============================================================================
// 3. Tooltip-hint formatting (pure helper — no GUI needed)
// ============================================================================

TEST(TooltipHint, EmptyShortcutReturnsBaseUnchanged) {
    EXPECT_EQ(synth::ui::formatShortcutHint("Save", ""), juce::String("Save"));
}

TEST(TooltipHint, ShortcutAppended) {
    EXPECT_EQ(synth::ui::formatShortcutHint("Save", "Cmd+S"), juce::String("Save  (Cmd+S)"));
}

TEST(TooltipHint, BothEmptyReturnsEmpty) { EXPECT_EQ(synth::ui::formatShortcutHint("", ""), juce::String("")); }

// ============================================================================
// 4. Paint smoke tests
//    Constructs MainComponent and AIChatComponent and calls paint() in various
//    states to verify no crash.  Mirrors the headless-construction patterns in
//    existing tests.
// ============================================================================

class PaintSmokeTest : public ::testing::Test {
protected:
    void SetUp() override { resetPanelKeys(); }
    void TearDown() override { resetPanelKeys(); }
};

TEST_F(PaintSmokeTest, MainComponentPaintIdleState) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);

    juce::Image img(juce::Image::ARGB, 1600, 900, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(mc.paint(g));
}

TEST_F(PaintSmokeTest, MainComponentPaintWithAiPanelVisible) {
    MainComponent mc(std::make_unique<MockProviderPAL>());
    mc.setSize(1600, 900);
    mc.simulateToggleAiPanelClick();

    juce::Image img(juce::Image::ARGB, 1600, 900, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(mc.paint(g));
}

TEST_F(PaintSmokeTest, AIChatComponentPaintIdleState) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockProviderPAL>());

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    juce::Image img(juce::Image::ARGB, 400, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(chat.paint(g));
}

TEST_F(PaintSmokeTest, AIChatComponentPaintWaitingState) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockProviderPAL>());

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    for (auto* child : chat.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            editor->setText("hello");
    }
    chat.triggerSend();
    ASSERT_TRUE(chat.isWaiting());

    juce::Image img(juce::Image::ARGB, 400, 600, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(chat.paint(g));
}

namespace {
// Puts the chat component into the waiting state with a request outstanding.
void enterWaitingState(synth::AIChatComponent& chat) {
    for (auto* child : chat.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            editor->setText("hello");
    }
    chat.triggerSend();
}
} // namespace

// The whole point of the change: Cancel must reach the provider. Before this, cancelRequest() only
// tidied the UI while the HTTP request ran on -- billed, and blocking the next message.
TEST_F(AICancelTest, CancelButtonTellsProviderToCancelTheRequest) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto provider = std::make_unique<MockProviderPAL>();
    auto* rawProvider = provider.get();
    service.setProvider(std::move(provider));

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    enterWaitingState(chat);
    ASSERT_TRUE(chat.isWaiting());

    chat.simulateCancelClick();

    ASSERT_EQ(rawProvider->cancelledIds.size(), 1u) << "Cancel did not reach the provider -- it is still cosmetic";
    EXPECT_EQ(rawProvider->cancelledIds[0], rawProvider->lastRequestId) << "Cancel targeted the wrong request handle";
    EXPECT_FALSE(chat.isWaiting());
}

// Escape must do what the Cancel button does. There was no keyPressed override at all before, so
// this is new behaviour rather than a regression lock.
TEST_F(AICancelTest, EscapeKeyCancelsInFlightRequest) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto provider = std::make_unique<MockProviderPAL>();
    auto* rawProvider = provider.get();
    service.setProvider(std::move(provider));

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    enterWaitingState(chat);
    ASSERT_TRUE(chat.isWaiting());

    chat.simulateEscapeKey();

    EXPECT_EQ(rawProvider->cancelledIds.size(), 1u) << "Escape did not cancel the in-flight request";
    EXPECT_FALSE(chat.isWaiting());
    EXPECT_FALSE(chat.isCancelVisible());
}

// Escape when nothing is in flight must stay out of the way, so it keeps whatever meaning the
// enclosing window gives it (closing a panel, dismissing a dialog).
TEST_F(AICancelTest, EscapeKeyIsIgnoredWhenIdle) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto provider = std::make_unique<MockProviderPAL>();
    auto* rawProvider = provider.get();
    service.setProvider(std::move(provider));

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    ASSERT_FALSE(chat.isWaiting());
    chat.simulateEscapeKey();

    EXPECT_TRUE(rawProvider->cancelledIds.empty()) << "Escape cancelled something while idle";
}

// A completed request must not be cancelled afterwards: the handle is stale and, on a real
// provider, could name a request the user has since sent.
TEST_F(AICancelTest, CancelAfterResponseDoesNotCancelAnything) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto provider = std::make_unique<MockProviderPAL>();
    auto* rawProvider = provider.get();
    service.setProvider(std::move(provider));

    TestPropsOwner propsOwner;
    synth::AIChatComponent chat(service, propsOwner.props);
    chat.setSize(400, 600);

    enterWaitingState(chat);
    ASSERT_TRUE(chat.isWaiting());

    // The response handler posts through the message queue, so let it run.
    rawProvider->completePending("All done.");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    ASSERT_FALSE(chat.isWaiting()) << "the component never left the waiting state";
    EXPECT_TRUE(rawProvider->cancelledIds.empty()) << "a completed request was cancelled after the fact";
}
