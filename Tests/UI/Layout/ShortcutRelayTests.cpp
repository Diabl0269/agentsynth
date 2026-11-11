// ShortcutRelayTests.cpp -- the rule for app shortcuts (undo, redo, play/stop) in dialogs that
// PopupMotion::launchDialog opened, pressed from any focused child and delivered in the order a real key takes (the
// focused component, then each parent up to the window, whose key listeners see it before its own keyPressed):
//   - a tool window that edits a module on the canvas (relaysAppShortcuts) hands them to the app, and keeps the keys
//     that act on the main window's selection (select all);
//   - a modal prompt keeps all of them, since an undo would change the canvas behind a question being answered.
// The keys the macOS menu owns (Cmd+Q, Cmd+H) are never used by a dialog's focus chain; the window's key
// listener then does the menu's job (JUCE's peer would swallow them while the dialog is modal), once, from every
// focused child. Dialogs come from the theme sweep's registry, so a new dialog has to pick a side. Windows that open on
// a real display are skipped on a headless runner.
#include "../../TestWait.h"
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "NativeWindowTestGuard.h"
#include "UI/Layout/DialogMenuKeys.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/BuiltInThemes.h"
#include <array>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>

using namespace synth::test::theme_sweep;
using synth::ui::PopupMotion;

namespace {

// A dialog that does not pass app shortcuts on yet. Key: "<dialog> / <key>" as the failure message prints it.
// The list may only shrink: an entry whose dialog now relays the key fails until it is removed.
struct KnownGap {
    const char* key;
    const char* ticket;
};

// Tool windows that do not relay yet.
const std::vector<KnownGap> kKnownGaps = {
    {"Sampler map / Cmd+Z", "FRO1036"},
    {"Sampler map / Cmd+Shift+Z", "FRO1036"},
    {"Sampler map / Space", "FRO1036"},
};

struct Sample {
    // The same on every platform (JUCE describes the command modifier as "ctrl" off macOS), so kKnownGaps holds.
    const char* name;
    juce::KeyPress key;
    bool relayed; // false: the key acts on the main window only, so even a tool window keeps it
    bool printable;
    int menuAction = -1; // 0 quit, 1 hide: the macOS menu item the key stands for
};

std::vector<Sample> samples() {
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    return {
        {"Space", juce::KeyPress(' ', juce::ModifierKeys(), ' '), true, true},
        {"Cmd+Z", juce::KeyPress('z', juce::ModifierKeys(cmd), 0), true, false},
        {"Cmd+Shift+Z", juce::KeyPress('z', juce::ModifierKeys(cmd | shift), 0), true, false},
        {"Cmd+A", juce::KeyPress('a', juce::ModifierKeys(cmd), 0), false, false}, // select all modules
        {"Cmd+Q", juce::KeyPress('q', juce::ModifierKeys(cmd), 0), false, false, 0},
        {"Cmd+H", juce::KeyPress('h', juce::ModifierKeys(cmd), 0), false, false, 1},
    };
}

// juce::ComponentPeer::handleKeyPress: each component from the focused one up offers a key to its key listeners and
// then to its own keyPressed; the first to use it ends the walk.
bool dispatchKey(juce::DialogWindow& dialog, juce::Component& origin, const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (target == &dialog && PopupMotion::offerKeyToDialogForTest(dialog, press))
            return true;
        if (target->keyPressed(press))
            return true;
    }
    return false;
}

bool isTextEditorPart(juce::Component& c) {
    return dynamic_cast<juce::TextEditor*>(&c) != nullptr ||
           c.findParentComponentOfClass<juce::TextEditor>() != nullptr;
}

// The relay hands a key to the app with a callAsync (an undo can delete the window whose handler is still running), so
// the count is read only after every message posted by the key has run. A fixed 3 x 10 ms pump overran on a loaded
// machine and read the count before the app had received the key.
void pumpMessages() { EXPECT_TRUE(synth::test::flushMessageQueue()) << "the message queue did not drain"; }

class ShortcutRelay : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        savedMenuActions_ = synth::ui::dialog_menu_keys::actions();
        synth::ui::dialog_menu_keys::actions() = {
            [this] { ++menuCalls[0]; },
            [this] { ++menuCalls[1]; },
        };
        wasEnabled_ = PopupMotion::isEnabled();
        PopupMotion::setEnabled(false); // a dialog shows at once
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        synth::ui::dialog_menu_keys::actions() = savedMenuActions_; // a test seam must not leak into the next test
        PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
    synth::ui::dialog_menu_keys::Actions savedMenuActions_;
    std::array<int, 2> menuCalls{0, 0}; // quit, hide
};

// Dialogs and keys that failed, compared with kKnownGaps.
void expectOnlyKnownGaps(const std::set<std::string>& found, const std::set<std::string>& tested) {
    std::set<std::string> listed;
    for (const auto& gap : kKnownGaps) {
        const std::string key = gap.key;
        listed.insert(key);
        if (tested.count(key.substr(0, key.find(" / "))) && !found.count(key))
            ADD_FAILURE() << "fixed - remove it from kKnownGaps: " << key << " (" << gap.ticket << ")";
    }
    for (const auto& key : found)
        if (!listed.count(key))
            ADD_FAILURE() << "not relayed: " << key;
}

} // namespace

TEST_F(ShortcutRelay, EveryDialogHandsAppShortcutsToTheAppFromItsFocusedChildren) {
    std::set<std::string> found, tested;
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        tested.insert(surface.name);
        // One environment per stop: each starts with no keys received and closes its own window.
        size_t stopCount = 1;
        for (size_t stop = 0; stop < stopCount; ++stop) {
            SweepEnvironment env(synth::theme::makeDaylight());
            auto* dialog = surface.open(env);
            ASSERT_NE(dialog, nullptr) << surface.name;
            auto stops = synth::ui::focusableDescendants(*dialog);
            if (stops.empty())
                stops.push_back(dialog); // nothing to focus: the key starts at the window itself
            stopCount = stops.size();
            for (const auto& sample : samples()) {
                if (sample.printable && isTextEditorPart(*stops[stop]))
                    continue; // a text field keeps a printable key as text
                env.appKeys().clear();
                menuCalls = {0, 0};
                dispatchKey(*dialog, *stops[stop], sample.key);
                pumpMessages();
#if JUCE_MAC
                for (int action = 0; action < 2; ++action)
                    if (menuCalls[static_cast<size_t>(action)] != (action == sample.menuAction ? 1 : 0))
                        found.insert(surface.name + " / " + sample.name + " (menu action count)");
#else
                if (menuCalls != std::array<int, 2>{0, 0})
                    found.insert(surface.name + " / " + sample.name + " (menu action off macOS)");
#endif
                const size_t expected = surface.relaysAppShortcuts && sample.relayed ? 1 : 0;
                if (env.appKeys().size() != expected)
                    found.insert(surface.name + " / " + sample.name);
            }
        }
    }
    expectOnlyKnownGaps(found, tested);
}
