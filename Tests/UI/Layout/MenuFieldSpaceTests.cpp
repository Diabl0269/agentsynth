// MenuFieldSpaceTests.cpp -- (bug kind key-double-fire, inverse) a menu-style field never swallows Space.
// Space is the app's play/stop, so on every select field of a real MainComponent (found by walking its tree,
// MainWindowSelects.h, not by listing classes) a Space delivered through the real key dispatch
// (juce::ComponentPeer::handleKeyPress: key listeners, then keyPressed up the parents) toggles the transport and
// opens no menu, and Return opens the menu.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "BottomDockActiveTabResetGuard.h"
#include "MainWindowSelects.h"
#include "NativeWindowTestGuard.h"
#include "UI/Layout/KeyField.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <optional>
#include <string>

namespace {

juce::KeyPress plain(int code) {
    return juce::KeyPress(code, juce::ModifierKeys(), code == juce::KeyPress::spaceKey ? ' ' : 0);
}

class MenuFieldSpaceTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        resetGuard_.emplace();
        engine_ = std::make_unique<AudioEngine>(AudioEngine::HostMode::Hosted);
        engine_->initialise();
        engine_->prepareForHost(44100.0, 512, 0, 2);
        mc_ = std::make_unique<MainComponent>(themeManager_, lookAndFeel_, *engine_, std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        mc_->setVisible(true);
    }
    void TearDown() override {
        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100); // a closing popup's deferred work runs first
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
    // The transport's click is posted and the engine takes the command on its next block: true when the playing state
    // reaches `want` within a second.
    bool playingBecomes(bool want) {
        for (int i = 0; i < 20 && playing() != want; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        return playing() == want;
    }
    // Gives `c` the keyboard and waits for it. A native window hands focus over asynchronously (on Windows the window
    // that a just-closed popup menu left focus on takes a few turns of the message loop to give it back), and the OS
    // delivers a key to whatever holds focus, so a key sent before the grab lands goes to the wrong component.
    static bool takeFocus(juce::Component& c) {
        for (int i = 0; i < 20 && !c.hasKeyboardFocus(false); ++i) {
            c.grabKeyboardFocus();
            if (!c.hasKeyboardFocus(false))
                juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        }
        return c.hasKeyboardFocus(false);
    }
    // Delivers `press` the way the OS does: to the focused component and up its parents, listeners first.
    bool deliver(juce::Component& focused, const juce::KeyPress& press) {
        auto* peer = focused.getPeer();
        EXPECT_NE(peer, nullptr);
        const bool handled = peer != nullptr && peer->handleKeyPress(press);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60); // the transport's click is posted
        return handled;
    }

    synth::theme::ThemeManager themeManager_;
    synth::theme::AppLookAndFeel lookAndFeel_;
    std::unique_ptr<AudioEngine> engine_;
    std::optional<BottomDockActiveTabResetGuardMDT> resetGuard_;
    std::unique_ptr<MainComponent> mc_;
};

// What a field opening looks like: a key field or note-length box reports through its own seam, any other combo box
// through its popup flag.
struct OpenProbe {
    explicit OpenProbe(juce::ComboBox& box)
        : combo(box)
        , keyField(dynamic_cast<synth::ui::KeyField*>(&box))
        , noteLength(dynamic_cast<synth::ui::NoteLengthBox*>(&box)) {
        if (keyField != nullptr) {
            previous = keyField->onOpen;
            keyField->onOpen = [this] { ++keyFieldOpens; };
        }
    }
    ~OpenProbe() {
        if (keyField != nullptr)
            keyField->onOpen = previous;
    }
    bool opened() const {
        return keyFieldOpens > 0 || combo.isPopupActive() ||
               (noteLength != nullptr && noteLength->getPopup() != nullptr);
    }
    void close() {
        if (noteLength != nullptr && noteLength->getPopup() != nullptr)
            noteLength->getPopup()->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        juce::PopupMenu::dismissAllActiveMenus();
        combo.hidePopup();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(40); // a closing popup's deferred work
        keyFieldOpens = 0;
    }

    juce::ComboBox& combo;
    synth::ui::KeyField* keyField;
    synth::ui::NoteLengthBox* noteLength;
    std::function<void()> previous;
    int keyFieldOpens = 0;
};

} // namespace

TEST_F(MenuFieldSpaceTest, SpaceTogglesPlaybackAndOpensNoMenuOnEverySelectFieldAndReturnOpensIt) {
    int exercised = 0, keyFields = 0, noteLengths = 0;
    std::vector<std::string> unfocusable;
    main_window_selects::forEachScene(*mc_, [&](const char* scene, std::vector<juce::ComboBox*>& selects) {
        for (auto* select : selects) {
            const std::string name =
                std::string(scene) + " / " + select->getTitle().toStdString() + " (" + typeid(*select).name() + ")";
            if (!select->getWantsKeyboardFocus())
                continue; // not a keyboard stop, so no key reaches it
            if (!takeFocus(*select)) {
                unfocusable.push_back(name);
                continue;
            }
            ++exercised;
            OpenProbe probe(*select);
            keyFields += probe.keyField != nullptr;
            noteLengths += probe.noteLength != nullptr;

            ASSERT_FALSE(playing()) << name;
            deliver(*select, plain(juce::KeyPress::spaceKey));
            EXPECT_TRUE(playingBecomes(true)) << name << ": Space did not start playback";
            EXPECT_FALSE(probe.opened()) << name << ": Space opened the menu";
            probe.close();
            deliver(*select, plain(juce::KeyPress::spaceKey));
            EXPECT_TRUE(playingBecomes(false)) << name << ": a second Space did not stop playback";
            EXPECT_FALSE(probe.opened()) << name << ": the second Space opened the menu";
            probe.close();

            ASSERT_TRUE(takeFocus(*select)) << name << ": lost keyboard focus, so Return would not reach it";
            // A popup menu closes itself on any platform where this process is not the foreground one (Windows CI): it
            // must be seen open the moment it opens, not after the dispatch loop has run on and closed it again.
            bool sawOpen = false;
            auto* peer = select->getPeer();
            ASSERT_NE(peer, nullptr) << name;
            peer->handleKeyPress(plain(juce::KeyPress::returnKey));
            sawOpen = probe.opened(); // a plain combo box has its flag up before it returns
            for (int i = 0; i < 30 && !sawOpen; ++i) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
                sawOpen = probe.opened();
            }
            EXPECT_TRUE(sawOpen) << name << ": Return did not open the menu";
            EXPECT_FALSE(playing()) << name << ": Return started playback";
            probe.close();
        }
    });
    for (const auto& name : unfocusable)
        std::cout << "[ menu fields ] could not take focus here: " << name << "\n";
    EXPECT_GE(exercised, 3) << "too few select fields took focus: the walk is not seeing the window";
    EXPECT_GE(keyFields, 1) << "the song Key field is a KeyField";
    EXPECT_GE(noteLengths, 1) << "a card's note-length box is a NoteLengthBox";
}
