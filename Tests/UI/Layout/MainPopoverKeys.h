#pragma once

// MainPopoverKeys.h -- shared by the tests that open a popover of the main window (timeline, mixer, MIDI Remote, key
// pickers) in a real MainComponent and press Cmd+Z, Cmd+Shift+Z and Space in it. Header-only; not registered in
// Tests/CMakeLists.txt. The popover opens through its real button; only the window is stood in for (a CallOutBox as a
// child of the main window), and a key travels the way a real one does: the focused child, each parent up to the box,
// where the box's key listeners see it before its own keyPressed (the shortcut relay).
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestWait.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "BottomDockActiveTabResetGuard.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>

namespace main_popover {

inline const juce::KeyPress kUndo('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
inline const juce::KeyPress
    kRedo('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0);
inline const juce::KeyPress kSpace(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ');

/** Catches the popover the next launchPopover opens, as a child of `parent`. */
class Capture {
public:
    explicit Capture(juce::Component& parent) {
        synth::ui::test_hooks::popoverLauncherForTest() = [this, &parent](std::unique_ptr<juce::Component> content,
                                                                          juce::Rectangle<int> area) {
            box_.reset();
            owned_ = std::move(content);
            box_ = std::make_unique<juce::CallOutBox>(*owned_, area, &parent);
            ++opens;
            return box_.get();
        };
    }
    ~Capture() {
        synth::ui::test_hooks::popoverLauncherForTest() = nullptr;
        box_.reset();
        owned_.reset();
    }
    juce::CallOutBox* box() const { return box_.get(); }
    juce::Component* content() const { return owned_.get(); }
    /** True once the popover was told to close (its fade-out runs) or is hidden. */
    bool closing() const {
        return box_ != nullptr && (synth::ui::PopupMotion::isDismissing(*box_) || !box_->isVisible());
    }
    void closeNow() {
        box_.reset();
        owned_.reset();
    }
    int opens = 0;

private:
    std::unique_ptr<juce::Component> owned_;
    std::unique_ptr<juce::CallOutBox> box_;
};

/** A real key's path from `origin` up to the box: each component's keyPressed, and at the box its key listeners first.
 *  Stops at the box (the real box has no parent, so nothing above it sees the key). */
inline bool dispatch(juce::CallOutBox& box, juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (target == &box)
            return synth::ui::test_hooks::offerKeyToPopoverForTest(box, press) || box.keyPressed(press);
        if (target->keyPressed(press))
            return true;
    }
    return false;
}

inline void pump(int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

/** A real MainComponent on a hosted engine, as the other whole-window key tests use. */
class MainPopoverTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        engine_->initialise();
        engine_->prepareForHost(44100.0, 512, 0, 2);
        mc_ = std::make_unique<MainComponent>(themeManager_, lookAndFeel_, *engine_, std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
    }
    void TearDown() override {
        mc_.reset();
        engine_.reset();
        resetGuard_.reset();
        MainComponentTest::TearDown();
    }

    bool playing() {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        engine_->processHostBlock(buffer, midi);
        return engine_->getTransport().getPositionSnapshot().playing;
    }
    bool playingBecomes(bool want) {
        for (int i = 0; i < 20 && playing() != want; ++i)
            pump(50);
        return playing() == want;
    }
    bool undone() { return mc_->getUndoManager().canRedo(); }

    /** Where a key is pressed: the first focusable child that is not a text field (a field keeps Space, Cmd+Z and
     *  Escape for its own typing and editing), or the content itself when it has no other. */
    static juce::Component* keyOrigin(Capture& popover) {
        for (auto* stop : synth::ui::focusableDescendants(*popover.content()))
            if (dynamic_cast<juce::TextEditor*>(stop) == nullptr &&
                stop->findParentComponentOfClass<juce::TextEditor>() == nullptr)
                return stop;
        return popover.content();
    }

    /** Presses Space in the open popover from each of its focusable children (or the content): the app's transport
     *  starts, then stops, and the popover is still there. */
    void expectSpacePlaysAndStops(Capture& popover, const std::string& name) {
        ASSERT_NE(popover.box(), nullptr) << name;
        auto* origin = keyOrigin(popover);
        ASSERT_FALSE(playing()) << name;
        EXPECT_TRUE(dispatch(*popover.box(), *origin, kSpace)) << name << ": Space was not handed to the app";
        EXPECT_TRUE(playingBecomes(true)) << name << ": Space did not start playback";
        EXPECT_TRUE(dispatch(*popover.box(), *origin, kSpace)) << name;
        EXPECT_TRUE(playingBecomes(false)) << name << ": a second Space did not stop playback";
        EXPECT_FALSE(popover.closing()) << name << ": Space closed the popover";
    }

    /** Keys that act on the canvas selection or are typed stay with the popover: the relay declines at once. */
    void expectCanvasKeysStay(Capture& popover, const std::string& name) {
        const auto cmd = juce::ModifierKeys::commandModifier;
        for (const auto& key :
             {juce::KeyPress('a', juce::ModifierKeys(cmd), 0), juce::KeyPress('c', juce::ModifierKeys(cmd), 0),
              juce::KeyPress(juce::KeyPress::deleteKey)})
            EXPECT_FALSE(synth::ui::test_hooks::offerKeyToPopoverForTest(*popover.box(), key))
                << name << ": " << key.getTextDescription();
    }

    /** Cmd+Z from the popover's first focusable child undoes the app's last step. A content that goes stale under an
     *  undo (AppUndoAware) closes without applying anything; any other stays open. Needs an undoable step in place. */
    void expectUndoReachesTheApp(Capture& popover, const std::string& name, bool contentGoesStale) {
        ASSERT_NE(popover.box(), nullptr) << name;
        ASSERT_TRUE(mc_->getUndoManager().canUndo()) << name << ": the scene needs an undo step";
        ASSERT_FALSE(undone()) << name;
        auto* origin = keyOrigin(popover);
        EXPECT_TRUE(dispatch(*popover.box(), *origin, kUndo)) << name << ": Cmd+Z was not handed to the app";
        EXPECT_TRUE(synth::test::waitUntil([&] { return undone(); })) << name << ": Cmd+Z did not undo";
        if (contentGoesStale) {
            EXPECT_TRUE(synth::test::waitUntil([&] { return popover.closing(); }))
                << name << ": the popover showed a project that no longer exists";
        } else {
            EXPECT_FALSE(popover.closing()) << name << ": Cmd+Z closed the popover";
        }
    }

    /** Cmd+Shift+Z from a popover (opened after the undo) redoes what Cmd+Z undid. */
    void expectRedoReachesTheApp(Capture& popover, const std::string& name) {
        ASSERT_NE(popover.box(), nullptr) << name;
        ASSERT_TRUE(undone()) << name;
        auto* origin = keyOrigin(popover);
        EXPECT_TRUE(dispatch(*popover.box(), *origin, kRedo)) << name << ": Cmd+Shift+Z was not handed to the app";
        EXPECT_TRUE(synth::test::waitUntil([&] { return !undone(); })) << name << ": Cmd+Shift+Z did not redo";
    }

    synth::theme::ThemeManager themeManager_;
    synth::theme::AppLookAndFeel lookAndFeel_;
    std::unique_ptr<AudioEngine> engine_;
    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

} // namespace main_popover
