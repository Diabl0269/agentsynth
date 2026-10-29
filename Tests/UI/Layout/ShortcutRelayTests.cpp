// ShortcutRelayTests.cpp -- app shortcuts (undo, redo, play/stop) pressed from the deepest focused child of any
// dialog that PopupMotion::launchDialog opened reach the app, delivered in the order a real key takes (the focused
// component, then each parent up to the window, whose key listeners see it before its own keyPressed). Dialogs come
// from the theme sweep's registry, so a new dialog joins this guard on its own. A key that edits the canvas selection
// is not relayed. Windows that open on a real display are skipped on a headless runner.
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

// Every dialog but the shape editor window leaves app shortcuts unrelayed.
const std::vector<KnownGap> kKnownGaps = {
    {"EQ window / command + Z", "TICKET?"},
    {"EQ window / shift + command + Z", "TICKET?"},
    {"EQ window / spacebar", "TICKET?"},
    {"Export audio / command + Z", "TICKET?"},
    {"Export audio / shift + command + Z", "TICKET?"},
    {"Export audio / spacebar", "TICKET?"},
    {"Export stems / command + Z", "TICKET?"},
    {"Export stems / shift + command + Z", "TICKET?"},
    {"Export stems / spacebar", "TICKET?"},
    {"Macro configure I/O / command + Z", "TICKET?"},
    {"Macro configure I/O / shift + command + Z", "TICKET?"},
    {"Macro configure I/O / spacebar", "TICKET?"},
    {"Macro ports prompt / command + Z", "TICKET?"},
    {"Macro ports prompt / shift + command + Z", "TICKET?"},
    {"Macro ports prompt / spacebar", "TICKET?"},
    {"Render progress / command + Z", "TICKET?"},
    {"Render progress / shift + command + Z", "TICKET?"},
    {"Render progress / spacebar", "TICKET?"},
    {"Sampler map / command + Z", "FRO1036"},
    {"Sampler map / shift + command + Z", "FRO1036"},
    {"Sampler map / spacebar", "FRO1036"},
    {"Settings / command + Z", "TICKET?"},
    {"Settings / shift + command + Z", "TICKET?"},
    {"Settings / spacebar", "TICKET?"},
    {"Sign in / command + Z", "TICKET?"},
    {"Sign in / shift + command + Z", "TICKET?"},
    {"Sign in / spacebar", "TICKET?"},
};

struct Sample {
    juce::KeyPress key;
    bool relayed; // false: the key acts on the main window only, so a tool window must keep it
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
                const size_t expected = sample.relayed ? 1 : 0;
                if (env.appKeys().size() != expected)
                    found.insert(surface.name + " / " + sample.key.getTextDescription().toStdString());
            }
        }
    }
    expectOnlyKnownGaps(found, tested);
}
