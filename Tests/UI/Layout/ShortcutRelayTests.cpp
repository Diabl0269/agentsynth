// ShortcutRelayTests.cpp -- the rule for app shortcuts (undo, redo, play/stop) in dialogs that
// PopupMotion::launchDialog opened, pressed from any focused child and delivered in the order a real key takes (the
// focused component, then each parent up to the window, whose key listeners see it before its own keyPressed):
//   - a tool window that edits a module on the canvas (relaysAppShortcuts) hands them to the app, and keeps the keys
//     that act on the main window's selection (select all);
//   - a modal prompt keeps all of them, since an undo would change the canvas behind a question being answered.
// Dialogs come from the theme sweep's registry, so a new dialog has to pick a side. Windows that open on a real
// display are skipped on a headless runner.
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "NativeWindowTestGuard.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/BuiltInThemes.h"
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
    {"Sampler map / command + Z", "FRO1036"},       {"Sampler map / shift + command + Z", "FRO1036"},
    {"Sampler map / spacebar", "FRO1036"},          {"EQ window / command + Z", "FRO1309"},
    {"EQ window / shift + command + Z", "FRO1309"}, {"EQ window / spacebar", "FRO1309"},
};

struct Sample {
    juce::KeyPress key;
    bool relayed; // false: the key acts on the main window only, so even a tool window keeps it
    bool printable;
};

std::vector<Sample> samples() {
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    return {
        {juce::KeyPress(' ', juce::ModifierKeys(), ' '), true, true},
        {juce::KeyPress('z', juce::ModifierKeys(cmd), 0), true, false},
        {juce::KeyPress('z', juce::ModifierKeys(cmd | shift), 0), true, false},
        {juce::KeyPress('a', juce::ModifierKeys(cmd), 0), false, false}, // select all modules
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

void pumpMessages() {
    for (int i = 0; i < 3; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

class ShortcutRelay : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = PopupMotion::isEnabled();
        PopupMotion::setEnabled(false); // a dialog shows at once
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
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
                dispatchKey(*dialog, *stops[stop], sample.key);
                pumpMessages();
                const size_t expected = surface.relaysAppShortcuts && sample.relayed ? 1 : 0;
                if (env.appKeys().size() != expected)
                    found.insert(surface.name + " / " + sample.key.getTextDescription().toStdString());
            }
        }
    }
    expectOnlyKnownGaps(found, tested);
}
