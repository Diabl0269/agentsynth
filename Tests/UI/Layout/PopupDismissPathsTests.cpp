// PopupDismissPathsTests.cpp -- every popup the app animates leaves with a visible fade on every way it can close:
// Escape, a click outside it, and the app being deactivated. Each test closes a real popup by one of those paths and
// reads the frames of the leave: the first frame is not already transparent (the popup did not pop out), a frame in
// between is part way (it faded, and a scaled one shrank), and the fade runs above the app window (the click that
// closes a popup also raises the app window, which buried the fade). The popup kinds stand for the users listed in
// PopupMotionRegistryTests.cpp; a kind that is modal by design (alerts, dialogs) must instead stay open on a click
// outside and on deactivation. docs/layout/animation.md#popup-windows.
#include "NativeWindowTestGuard.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthPopup.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

using synth::ui::ModDotPanelFrame;
using synth::ui::NoteLengthPopup;
using synth::ui::PopupMotion;

namespace {

bool pumpUntil(const std::function<bool()>& done) {
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (!done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    return done();
}

juce::KeyPress escape() { return juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0); }

// A press on `target`, as the global mouse listener of a popup sees it.
juce::MouseEvent pressOn(juce::Component& target) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {5.0f, 5.0f},
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &target, &target, juce::Time::getCurrentTime(), {5.0f, 5.0f}, juce::Time::getCurrentTime(),
                            1, false);
}

float scaleOf(juce::Component* body) {
    return body != nullptr && body->isTransformed() ? body->getTransform().getScaleFactor() : 1.0f;
}

struct Frame {
    float alpha;
    float scale;
};

// The app leaving the front, restored on every exit. The fixture holds it in front otherwise: a CI runner's test
// process can lose the foreground at any moment, and a panel would close itself before the path under test ran.
struct AppDeactivated {
    AppDeactivated() { synth::testmode::setAppInFrontForTest(false); }
    ~AppDeactivated() { synth::testmode::setAppInFrontForTest(true); }
};

// Steps a live window's leaving tween and reads each frame. `total` is the tween's length in ms.
std::vector<Frame> stepLeave(juce::Component& window, juce::Component* body, double total) {
    std::vector<Frame> frames;
    for (const double fraction : {0.0, 0.25, 0.5, 0.75, 0.99}) {
        PopupMotion::stepMotionForTest(window, total * fraction);
        frames.push_back({window.getAlpha(), scaleOf(body)});
    }
    return frames;
}

void expectFadedOut(const std::vector<Frame>& frames, bool scaled) {
    ASSERT_GE(frames.size(), 3u);
    EXPECT_GT(frames.front().alpha, 0.5f) << "the leave starts opaque, it does not pop out";
    for (size_t i = 1; i < frames.size(); ++i)
        EXPECT_LE(frames[i].alpha, frames[i - 1].alpha + 1e-4f) << "only fainter, frame " << i;
    const auto& mid = frames[frames.size() / 2];
    EXPECT_GT(mid.alpha, 0.0f) << "part way through it is still there";
    EXPECT_LT(mid.alpha, 1.0f) << "and already fading";
    if (scaled) {
        EXPECT_LT(mid.scale, 1.0f) << "it shrinks back toward where it came from";
        EXPECT_GT(mid.scale, 0.0f);
    }
}

// What a closing popup must show, whichever way it was closed.
struct LiveLeave {
    juce::Component* window = nullptr;
    juce::Component* body = nullptr;
    juce::Component* app = nullptr; // a normal window the popup must stay above while it fades
    double totalMs = 0.0;
    std::function<bool()> closed;
};

void expectLeavesByFading(const LiveLeave& leave, const std::function<void()>& closeIt) {
    closeIt();
    ASSERT_TRUE(PopupMotion::isDismissing(*leave.window)) << "it closed at once instead of fading";
    EXPECT_FALSE(leave.closed()) << "the window is still there while it fades";
    EXPECT_GT(leave.window->getAlpha(), 0.0f) << "the first frame is not already transparent";
    EXPECT_TRUE(PopupMotion::isAboveForTest(*leave.window, *leave.app))
        << "the app window the click raised would bury the fade";
    expectFadedOut(stepLeave(*leave.window, leave.body, leave.totalMs), leave.body != nullptr);
    EXPECT_TRUE(pumpUntil(leave.closed));
}

class PopupDismissPaths : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        synth::ui::setReducedMotionForTest(false);
        synth::testmode::setAppInFrontForTest(true);
        app.setBounds(0, 0, 600, 400);
        app.addToDesktop(0);
        app.setVisible(true);
    }
    void TearDown() override {
        synth::testmode::setAppInFrontForTest(std::nullopt);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }

    juce::Component app; // stands for the main window: a normal top-level window
};

// ---- the mod dot panel (also the port panel) ----------------------------------------------------------------------

struct PanelRig {
    juce::Component anchor;
    std::unique_ptr<ModDotPanelFrame> frame;
    int closed = 0;
    PanelRig() {
        auto content = std::make_unique<juce::Component>();
        content->setSize(280, 100);
        frame = std::make_unique<ModDotPanelFrame>(std::move(content), anchor, juce::Rectangle<int>(100, 200, 12, 12),
                                                   juce::Rectangle<int>(0, 0, 800, 600));
        frame->onClosed = [this] { ++closed; };
        frame->showOnDesktop();
    }
    bool introDone() const { return frame->getAlpha() >= 0.999f && !frame->body().isTransformed(); }
    LiveLeave leave(juce::Component& app) {
        return {frame.get(), &frame->body(), &app, ModDotPanelFrame::kOutMs, [this] { return closed == 1; }};
    }
};

#if JUCE_MAC
// Regression test for FRO1159: the press outside a popup raises the app window before the app hears of the press, so a
// popup that only rose when its close began vanished for the first frames, came back, then faded.
TEST_F(PopupDismissPaths, ThePanelStaysAboveTheWindowItOpenedOverWhenThatWindowIsRaised) {
    juce::DocumentWindow main("Main", juce::Colours::white, 0, true);
    main.setBounds(0, 0, 600, 400);
    main.setVisible(true);
    main.toFront(true);
    ASSERT_TRUE(pumpUntil([&] { return juce::TopLevelWindow::getActiveTopLevelWindow() == &main; }));
    PanelRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    main.toFront(false);
    EXPECT_TRUE(PopupMotion::isAboveForTest(*rig.frame, main));
    rig.frame->keyPressed(escape());
    EXPECT_TRUE(pumpUntil([&] { return rig.closed == 1; }));
}
#endif

TEST_F(PopupDismissPaths, TheModDotPanelShrinksIntoItsDotOnEscape) {
    PanelRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    expectLeavesByFading(rig.leave(app), [&] { rig.frame->keyPressed(escape()); });
}

TEST_F(PopupDismissPaths, TheModDotPanelShrinksIntoItsDotOnAClickOutside) {
    PanelRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    expectLeavesByFading(rig.leave(app), [&] {
        app.toFront(true); // the press that lands on the app window raises it first
        static_cast<juce::MouseListener&>(*rig.frame).mouseDown(pressOn(app));
    });
}

TEST_F(PopupDismissPaths, TheModDotPanelShrinksIntoItsDotWhenTheAppIsDeactivated) {
    PanelRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    expectLeavesByFading(rig.leave(app), [&] {
        const AppDeactivated deactivated;
        ASSERT_TRUE(pumpUntil([&] { return rig.frame->isClosing(); })) << "the panel stayed open";
    });
}

// ---- the note length popup ----------------------------------------------------------------------------------------

struct NoteLengthRig {
    juce::Component anchor;
    std::unique_ptr<NoteLengthPopup> popup;
    int closed = 0;
    NoteLengthRig() {
        anchor.setBounds(100, 100, 80, 22);
        NoteLengthPopup::Options options;
        options.choices = juce::StringArray{"1/8", "1/4", "1/2"};
        options.current = "1/4";
        options.title = "Test note length";
        popup = std::make_unique<NoteLengthPopup>(anchor, options);
        popup->onClosed = [this] { ++closed; };
        popup->showOnDesktop();
    }
    bool introDone() const { return popup->getAlpha() >= 0.999f; }
    LiveLeave leave(juce::Component& app) {
        // The popup's leave is the shared 110 ms (it sets no duration of its own).
        return {popup.get(), nullptr, &app, synth::ui::popup_motion::kOutMs, [this] { return closed == 1; }};
    }
};

TEST_F(PopupDismissPaths, TheNoteLengthPopupFadesOnEscape) {
    NoteLengthRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    expectLeavesByFading(rig.leave(app), [&] { rig.popup->keyPressed(escape()); });
}

TEST_F(PopupDismissPaths, TheNoteLengthPopupFadesOnAClickOutside) {
    NoteLengthRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    expectLeavesByFading(rig.leave(app), [&] {
        app.toFront(true);
        rig.popup->mouseDown(pressOn(app));
    });
}

TEST_F(PopupDismissPaths, TheNoteLengthPopupFadesWhenTheAppIsDeactivated) {
    NoteLengthRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.introDone(); }));
    expectLeavesByFading(rig.leave(app), [&] {
        const AppDeactivated deactivated;
        ASSERT_TRUE(pumpUntil([&] { return rig.popup->isClosing(); })) << "the popup stayed open";
    });
}

// ---- call-outs ----------------------------------------------------------------------------------------------------

// A call-out box on the desktop with a content component, attached as the app's look and feel attaches every one.
struct CallOutRig {
    juce::Component content;
    std::unique_ptr<juce::CallOutBox> box;
    CallOutRig() {
        content.setSize(120, 60);
        box = std::make_unique<juce::CallOutBox>(content, juce::Rectangle<int>(200, 200, 20, 20), nullptr);
        PopupMotion::attach(*box);
        box->setVisible(true);
    }
    // JUCE ignores a click-away in a call-out's first 200 ms.
    bool settled() const { return box->getAlpha() >= 0.999f; }
};

// Reads the leaving picture frame by frame: JUCE hides its own click-away and deactivate closes at once, so the fade is
// a picture of the window that the app puts in its place.
void expectGhostFades() {
    ASSERT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() > 0; })) << "the call-out just vanished";
    auto ghosts = PopupMotion::getLeavingGhostsForTest();
    ASSERT_FALSE(ghosts.empty());
    // The pump above can run a frame or two of the fade on a visible display, so "opaque" is only roughly so here.
    EXPECT_GT(ghosts.front()->getAlpha(), 0.5f) << "the picture starts opaque";
    EXPECT_TRUE(ghosts.front()->isAlwaysOnTop()) << "above the app window the click raised";
    PopupMotion::stepLeavingGhostsForTest(40.0);
    ASSERT_FALSE(PopupMotion::getLeavingGhostsForTest().empty());
    const float mid = PopupMotion::getLeavingGhostsForTest().front()->getAlpha();
    EXPECT_GT(mid, 0.0f);
    EXPECT_LT(mid, 1.0f);
    EXPECT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; }));
}

TEST_F(PopupDismissPaths, ACallOutFadesOnEscapeThroughItsLiveWindow) {
    CallOutRig rig;
    ASSERT_TRUE(pumpUntil([&] { return rig.settled(); }));
    bool gone = false;
    struct Watch : juce::ComponentListener {
        bool* flag;
        void componentVisibilityChanged(juce::Component& c) override { *flag = !c.isVisible(); }
    } watch;
    watch.flag = &gone;
    rig.box->addComponentListener(&watch);
    ASSERT_TRUE(synth::ui::closeHostingWindow(rig.content));
    ASSERT_TRUE(PopupMotion::isDismissing(*rig.box));
    EXPECT_FALSE(gone);
    EXPECT_GT(rig.box->getAlpha(), 0.0f);
    EXPECT_TRUE(PopupMotion::isAboveForTest(*rig.box, app));
    expectFadedOut(stepLeave(*rig.box, nullptr, synth::ui::popup_motion::kOutMs), false);
    pumpUntil([&] { return gone; });
    rig.box->removeComponentListener(&watch);
}

TEST_F(PopupDismissPaths, ACallOutFadesOnAClickOutsideAndWhenTheAppIsDeactivated) {
    // JUCE closes a call-out on both through inputAttemptWhenModal: a press elsewhere, and the app resigning key.
    for (const char* path : {"click outside", "app deactivated"}) {
        SCOPED_TRACE(path);
        CallOutRig rig;
        ASSERT_TRUE(pumpUntil([&] { return rig.settled(); }));
        const auto opened = juce::Time::getMillisecondCounter();
        pumpUntil([&] { return juce::Time::getMillisecondCounter() > opened + 250; }); // JUCE ignores the first 200 ms
        app.toFront(true);
        rig.box->inputAttemptWhenModal();
        expectGhostFades();
    }
}

struct KeyAlert : juce::AlertWindow {
    using juce::AlertWindow::AlertWindow;
    using juce::AlertWindow::keyPressed;
};

// ---- alert prompts and dialogs ------------------------------------------------------------------------------------

TEST_F(PopupDismissPaths, AnAlertPromptFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation) {
    bool answered = false;
    auto* alert = new KeyAlert("Prompt", "Name:", juce::AlertWindow::NoIcon);
    alert->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    PopupMotion::attach(*alert);
    alert->enterModalState(true, juce::ModalCallbackFunction::create([&answered, alert](int) {
                               std::unique_ptr<juce::AlertWindow> owned(alert);
                               answered = true;
                           }));
    ASSERT_TRUE(pumpUntil([&] { return alert->getAlpha() >= 0.999f; }));

    {
        const AppDeactivated deactivated;
        app.toFront(true);
        alert->inputAttemptWhenModal(); // a press outside a modal prompt, or the app resigning key
        pumpUntil([&] { return answered; });
        EXPECT_FALSE(answered) << "a modal prompt does not close on a click outside or on deactivation";
        EXPECT_TRUE(alert->isVisible());
    }

    alert->keyPressed(escape());
    expectGhostFades();
    EXPECT_TRUE(pumpUntil([&] { return answered; }));
}

TEST_F(PopupDismissPaths, ADialogFadesOnEscapeAndStaysOpenOnAClickOutsideOrDeactivation) {
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new juce::Component());
    options.content->setSize(200, 100);
    options.dialogTitle = "Dialog";
    options.useNativeTitleBar = false;
    options.resizable = false;
    auto* dialog = PopupMotion::launchDialog(options);
    ASSERT_NE(dialog, nullptr);
    juce::Component::SafePointer<juce::DialogWindow> safe(dialog);
    ASSERT_TRUE(pumpUntil([&] { return dialog->getAlpha() >= 0.999f; }));

    {
        const AppDeactivated deactivated;
        app.toFront(true);
        dialog->inputAttemptWhenModal();
        pumpUntil([&] { return PopupMotion::isDismissing(*dialog); });
        EXPECT_FALSE(PopupMotion::isDismissing(*dialog)) << "a dialog does not close on a click outside";
        EXPECT_TRUE(dialog->isVisible());
    }

    ASSERT_TRUE(PopupMotion::offerKeyToDialogForTest(*dialog, escape()));
    ASSERT_TRUE(PopupMotion::isDismissing(*dialog)) << "the dialog closed at once";
    EXPECT_GT(dialog->getAlpha(), 0.0f);
    EXPECT_TRUE(PopupMotion::isAboveForTest(*dialog, app));
    expectFadedOut(stepLeave(*dialog, nullptr, synth::ui::popup_motion::kOutMs), false);
    pumpUntil([&] { return safe == nullptr || !safe->isVisible(); });
    synth::ui::deleteOpenModalDialogs();
    // A fading dialog has left the modal list, and no frame may have reached it on a machine with no display refresh.
    delete safe.getComponent();
}

} // namespace
