// KeyStandardsSweepTests.cpp -- the app's key standards hold on every focusable control of every keyboard focus region
// and every dialog. Each control is given real keyboard focus and keys are delivered the way the operating system does
// (RealKeyDelivery.h: the window's peer, key listeners then keyPressed up the parents), never by calling keyPressed on
// a component. Regions come from a real MainComponent's FocusRegionRegistry (each main view in turn, a piano roll and a
// drum machine open on a clip), dialogs from the theme sweep registry, so a new surface is swept without being listed.
//
// Per control (docs/control/shortcuts.md, docs/development/accessibility.md):
//  - Space reaches the transport (the main window: the togglePlayback command fires; a tool window: the app received
//    the key; a modal prompt keeps it, as the shortcut relay rules say) and opens no menu or popup. A text editor keeps
//    Space for typing and is skipped. A focused button that presses itself on Space and consumes it (the accessibility
//    doc's Space on a focused button rule) is not a gap.
//  - Cmd+Z reaches the app's undo (a no-op step is put on the undo stack first and must be undone; a dialog must hand
//    the key to the app, except a modal prompt). A text editor keeps its own undo and is skipped, and so is the
//    Controllers panel, whose Cmd+Z undoes the controller edits (shortcuts.md).
//  - Return activates a button (its click handler, swapped for a counter for the length of the check, runs once) and
//    opens a combo box's menu. Other controls are skipped. The menu appears touching the control: its screen rectangle
//    meets the control's, widened by kMenuReach px.
//  - Escape closes the topmost popup the control opened. In a dialog it then closes the dialog (a text field keeps
//    Escape for its own editing and is skipped).
// A failure prints "<region id or dialog name> / <control>: <what>"; a control is named by its class and title, button
// text or component id, with digits written as '#'. A gap that is known and ticketed lives in kKnownGaps; the list may
// only shrink. A control that cannot take keyboard focus here is listed in the output and not judged.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestSettingsHelpers.h"
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "AppUndoManager.h"
#include "KeyboardReachFixture.h"
#include "KeyboardSweepShell.h"
#include "RealKeyDelivery.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ThemeManager.h"
#if defined(__GNUG__)
#include <cxxabi.h>
#endif
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <typeinfo>

using namespace synth::test::keyboard_sweep;
using namespace synth::test::real_keys;
using synth::test::theme_sweep::dialogSurfaces;
using synth::test::theme_sweep::SweepEnvironment;
using Tab = synth::ui::BottomDockComponent::Tab;

namespace {

const std::vector<KnownGap> kKnownGaps = {};

constexpr int kMenuReach = 6; // how far from a control a menu it opened may start and still touch it

bool isTextEditorPart(juce::Component& c) {
    return dynamic_cast<juce::TextEditor*>(&c) != nullptr ||
           c.findParentComponentOfClass<juce::TextEditor>() != nullptr;
}

class Findings : public SweepFindings {
public:
    Findings()
        : SweepFindings("key standards", kKnownGaps) {}
};

// What "the key reached the app" means on a surface, and what the surface should do with it.
struct AppProbe {
    std::function<void()> beforeSpace;
    std::function<bool()> spaceReached;
    std::function<void()> afterSpace;
    std::function<void()> beforeUndo;
    std::function<bool()> undoReached;
    bool expectApp = true; // false: a modal prompt keeps Space and Cmd+Z
    bool undoToApp = true; // false: Cmd+Z is routed to a history of its own (the Controllers panel's edit history)
};

struct NoopStep : juce::UndoableAction {
    bool perform() override { return true; }
    bool undo() override { return true; }
};

std::string whoTook(const Delivery& d) {
    if (d.handledBy != nullptr)
        return "taken by " + describe(*d.handledBy);
    return d.handled ? "taken by a component that is gone" : "not handled";
}

void checkSpace(Findings& f, const std::string& surface, juce::Component& control, const AppProbe& app) {
    if (isTextEditorPart(control) || !takeFocus(control))
        return;
    app.beforeSpace();
    DesktopWatch watch;
    juce::Component::SafePointer<juce::Component> self(&control);
    const auto delivery = deliver(control, kSpace);
    const bool reached = synth::test::waitUntil(app.spaceReached, app.expectApp ? 1000 : 20, 5);
    if (self == nullptr) { // the press rebuilt the surface and took the control with it
        app.afterSpace();
        return;
    }
    // A focused button presses itself on Space and consumes it (SpaceClaimingButton, accessibility.md): not a gap.
    const bool buttonClaimed = dynamic_cast<juce::Button*>(&control) != nullptr && delivery.handledBy == &control;
    if (app.expectApp && !reached && !buttonClaimed)
        f.add(surface, describe(control), "Space does not reach the transport", whoTook(delivery));
    if (!app.expectApp && reached)
        f.add(surface, describe(control), "Space reached the app from a modal prompt");
    if (!watch.opened().empty()) {
        f.add(surface, describe(control), "Space opened a menu or popup");
        closeMenus();
    }
    app.afterSpace();
}

void checkUndo(Findings& f, const std::string& surface, juce::Component& control, const AppProbe& app) {
    if (!app.undoToApp || isTextEditorPart(control) || !takeFocus(control))
        return;
    app.beforeUndo();
    juce::Component::SafePointer<juce::Component> self(&control);
    const auto delivery = deliver(control, kUndo);
    const bool reached = synth::test::waitUntil(app.undoReached, app.expectApp ? 1000 : 20, 5);
    if (self == nullptr)
        return;
    if (app.expectApp && !reached)
        f.add(surface, describe(control), "Cmd+Z does not reach the app's undo", whoTook(delivery));
    if (!app.expectApp && reached)
        f.add(surface, describe(control), "Cmd+Z reached the app from a modal prompt");
}

// Return on a combo box: the menu opens, touches the control and the next Escape closes it.
void checkComboReturn(Findings& f, const std::string& surface, juce::ComboBox& combo) {
    DesktopWatch watch;
    deliver(combo, kReturn);
    synth::test::waitUntil([&] { return combo.isPopupActive() || !watch.opened().empty(); }, 300, 5);
    const auto menus = watch.opened();
    if (!combo.isPopupActive() && menus.empty()) {
        f.add(surface, describe(combo), "Return does not open its menu");
        return;
    }
    // A menu is kept inside its display, so a control on or past the screen's edge (a small CI display under a 1400 x
    // 900 window) gets it moved: only judge where the control is fully on screen.
    const auto onScreen =
        juce::Desktop::getInstance().getDisplays().getTotalBounds(true).contains(combo.getScreenBounds());
    if (!menus.empty() && !onScreen)
        std::cout << "[ key standards ] menu position not judged, control is off the display: " << surface << " / "
                  << describe(combo) << "\n";
    if (!menus.empty() && onScreen) {
        const auto control = combo.getScreenBounds().expanded(kMenuReach);
        bool touches = false;
        for (auto* menu : menus)
            touches = touches || menu->getScreenBounds().intersects(control);
        if (!touches)
            f.add(surface, describe(combo), "its menu does not appear next to it");
    }
    deliver(combo, kEscape);
    const bool closed =
        synth::test::waitUntil([&] { return !combo.isPopupActive() && watch.opened().empty(); }, 500, 5);
    if (!closed)
        f.add(surface, describe(combo), "Escape does not close its menu");
    closeMenus();
    combo.hidePopup();
}

// Return on a button: its click handler runs once. The handler is swapped for a counter, so Save, Open and the like
// never run, and the toggle state is put back.
void checkButtonReturn(Findings& f, const std::string& surface, juce::Button& button) {
    const auto saved = button.onClick;
    const bool toggled = button.getToggleState();
    int clicks = 0;
    button.onClick = [&clicks] { ++clicks; };
    DesktopWatch watch;
    juce::Component::SafePointer<juce::Button> alive(&button);
    deliver(button, kReturn);
    synth::test::waitUntil([&] { return clicks > 0 || alive == nullptr; }, 200, 5);
    if (alive == nullptr)
        return; // a listener rebuilt the surface and took the button with it: it was activated
    button.onClick = saved;
    button.setToggleState(toggled, juce::dontSendNotification);
    if (clicks != 1)
        f.add(surface, describe(button), clicks == 0 ? "Return does not activate it" : "Return activates it twice");
    if (!watch.opened().empty())
        closeMenus();
}

void checkReturn(Findings& f, const std::string& surface, juce::Component& control) {
    auto* combo = dynamic_cast<juce::ComboBox*>(&control);
    auto* button = dynamic_cast<juce::Button*>(&control);
    if ((combo == nullptr && button == nullptr) || !control.isEnabled() || !takeFocus(control))
        return;
    if (combo != nullptr)
        checkComboReturn(f, surface, *combo);
    else
        checkButtonReturn(f, surface, *button);
}

void checkControl(Findings& f, const std::string& surface, juce::Component& control, const AppProbe& app) {
    // A key can rebuild the surface (an undo rebuilds the mixer columns), so the control may be gone after any step.
    juce::Component::SafePointer<juce::Component> alive(&control);
    checkSpace(f, surface, control, app);
    if (alive == nullptr)
        return;
    checkUndo(f, surface, control, app);
    if (alive == nullptr)
        return;
    checkReturn(f, surface, control);
}

// Counts the app commands that fire.
class CommandCounter : public juce::ApplicationCommandManagerListener {
public:
    void applicationCommandInvoked(const juce::ApplicationCommandTarget::InvocationInfo& info) override {
        if (!muted)
            ++counts[info.commandID];
    }
    void applicationCommandListChanged() override {}
    std::map<int, int> counts;
    bool muted = false;
};

class KeyStandardsShell : public KeyboardDesktopShell {
protected:
    void SetUp() override {
        KeyboardDesktopShell::SetUp();
        if (mc_ != nullptr)
            mc_->getCommandManager().addListener(&commands_);
    }
    void TearDown() override {
        if (mc_ != nullptr)
            mc_->getCommandManager().removeListener(&commands_);
        KeyboardDesktopShell::TearDown();
    }

    AppProbe mainProbe() {
        auto& um = mc_->getUndoManager();
        AppProbe app;
        app.beforeSpace = [this] { commands_.counts.clear(); };
        app.spaceReached = [this] { return commands_.counts[AppCommands::togglePlayback] > 0; };
        app.afterSpace = [this] {
            if (commands_.counts[AppCommands::togglePlayback] == 0)
                return;
            commands_.muted = true; // put the transport back
            mc_->getCommandManager().invokeDirectly(AppCommands::togglePlayback, false);
            synth::test::flushMessageQueue();
            commands_.muted = false;
        };
        app.beforeUndo = [&um] {
            um.getUndoManager().beginNewTransaction();
            um.getUndoManager().perform(new NoopStep());
        };
        app.undoReached = [&um] { return um.canRedo(); };
        return app;
    }

    void checkRegions(Findings& findings, const std::string& suffix) {
        const auto& registry = mc_->getFocusRegionsForTest();
        auto app = mainProbe();
        for (const auto& region : openRegions()) {
            app.undoToApp = region.id != "midiRemote"; // its Cmd+Z undoes the controller edits (shortcuts.md)
            const auto id = region.id;
            const auto surface = (id + suffix).toStdString();
            std::vector<juce::Component::SafePointer<juce::Component>> controls;
            for (auto* c : synth::ui::focusableDescendants(*region.root)) {
                const auto* owner = registry.regionContaining(c);
                if (owner != nullptr && owner->id == id)
                    controls.emplace_back(c);
            }
            findings.walked(surface, controls.size());
            for (auto& control : controls) {
                if (control == nullptr)
                    continue;
                if (!takeFocus(*control)) {
                    findings.unfocusable(surface + " / " + describe(*control));
                    continue;
                }
                checkControl(findings, surface, *control, app);
            }
        }
    }

    void checkScenes(Findings& findings) {
        const auto firstTrack = mc_->getTimelineDoc().getTracks().front().id;
        mc_->getTimelinePanel().selectTracks({firstTrack}, firstTrack);
        mc_->getTimelinePanel().toggleSidePane(true);
        for (const Tab view : {Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
            mc_->setMainViewForTest(view);
            checkRegions(findings, "");
        }
        auto& panel = mc_->getTimelinePanel();
        mc_->setMainViewForTest(Tab::Timeline);
        const auto clip = mc_->getTimelineDoc().addClip(firstTrack, 0.0, 8.0, "Clip");
        panel.openPianoRoll(clip);
        panel.getPianoRoll().toggleScalePanel();
        mc_->resized();
        checkRegions(findings, " [piano roll open]");
        panel.closePianoRoll();
        panel.openDrumMachine(clip);
        mc_->resized();
        checkRegions(findings, " [drum machine open]");
        panel.closeDrumMachine();
        mc_->resized();
    }

    CommandCounter commands_;
};

class KeyStandardsDialogs : public KeyboardReachDialogFixture {};

} // namespace

TEST_F(KeyStandardsShell, SpaceReturnEscapeAndUndoWorkOnEveryControlOfEveryRegion) {
#if !JUCE_MAC
    GTEST_SKIP() << "the known-gap list is measured on macOS; fonts, audio devices and menus differ on other platforms";
#endif
    Findings findings;
    checkScenes(findings);
    findings.expectOnlyKnownGaps();
}

TEST_F(KeyStandardsDialogs, SpaceReturnEscapeAndUndoWorkOnEveryControlOfEveryDialog) {
#if !JUCE_MAC
    GTEST_SKIP() << "the known-gap list is measured on macOS; fonts, audio devices and menus differ on other platforms";
#endif
    Findings findings;
    const auto theme = synth::theme::builtInThemes().front();
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        size_t stopCount = 1;
        // One environment per stop: Escape closes the dialog, and each stop starts with no keys received.
        for (size_t stop = 0; stop < stopCount; ++stop) {
            SweepEnvironment env(theme);
            auto* window = surface.open(env);
            ASSERT_NE(window, nullptr) << surface.name;
            synth::test::flushMessageQueue();
            auto stops = synth::ui::focusableDescendants(*window);
            if (stops.empty())
                stops.push_back(window);
            stopCount = stops.size();
            auto* control = stops[stop];
            findings.walked(surface.name, stop == 0 ? stopCount : 0);
            if (!takeFocus(*control)) {
                findings.unfocusable(surface.name + " / " + describe(*control));
                continue;
            }
            AppProbe app;
            app.expectApp = surface.relaysAppShortcuts;
            app.beforeSpace = [&env] { env.appKeys().clear(); };
            app.spaceReached = [&env] { return !env.appKeys().empty(); };
            app.afterSpace = [] {};
            app.beforeUndo = [&env] { env.appKeys().clear(); };
            app.undoReached = [&env] { return !env.appKeys().empty(); };
            juce::Component::SafePointer<juce::Component> dialog(window);
            checkControl(findings, surface.name, *control, app);
            if (dialog == nullptr || !takeFocus(*control))
                continue;
            if (isTextEditorPart(*control))
                continue; // a text field keeps Escape for its own editing
            const auto escape = deliver(*control, kEscape);
            synth::test::waitUntil([&] { return dialog == nullptr || !dialog->isVisible(); }, 500, 5);
            if (dialog != nullptr && dialog->isVisible())
                findings.add(surface.name, describe(*control), "Escape does not close the dialog", whoTook(escape));
        }
    }
    findings.expectOnlyKnownGaps();
}
