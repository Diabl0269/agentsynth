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
#include "AudioEngine/AudioEngine.h"
#include "KeyboardReachFixture.h"
#include "KeyboardSweepShell.h"
#include "RealKeyDelivery.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Chrome/Toast/ToastComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
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

using synth::test::GapPlatform;

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
//
// Whether Return reached the combo is read at once: JUCE sets isPopupActive() inside the key handler and only then
// posts the popup window. The window itself is waited for, to a long ceiling that costs nothing when it shows early. A
// popup that was shown and is already gone (JUCE dismisses a menu on its own when the process is not the foreground
// one, which a loaded Windows runner is for a moment, and one slow pump slice runs the show and the dismissal together)
// leaves isPopupActive() false and no window to measure: it is not a Return that failed, so its position is not judged.
void checkComboReturn(Findings& f, const std::string& surface, juce::ComboBox& combo) {
    constexpr int kMenuWaitMs = 5000;
    DesktopWatch watch;
    deliver(combo, kReturn);
    if (!combo.isPopupActive() && watch.opened().empty()) {
        f.add(surface, describe(combo), "Return does not open its menu");
        return;
    }
    synth::test::waitUntil([&] { return !watch.opened().empty() || !combo.isPopupActive(); }, kMenuWaitMs, 5);
    const auto menus = watch.opened();
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
        synth::test::waitUntil([&] { return !combo.isPopupActive() && watch.opened().empty(); }, 5000, 5);
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
    Findings findings;
    checkScenes(findings);
    findings.expectOnlyKnownGaps();
}

TEST_F(KeyStandardsDialogs, SpaceReturnEscapeAndUndoWorkOnEveryControlOfEveryDialog) {
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

// ---------------------------------------------------------------------------------------------------------------------
// Hidden timeline:  a key that acts on the closed timeline says so (bug kind hidden-but-plays). With the timeline
// closed the transport bar is off screen, so Loop, Record and Metronome change state nobody can see, and the
// timeline-only keys (snap grid, track step) do nothing at all: each answers with a toast over the canvas whose action
// opens the timeline. Keys are delivered the way the window does (RealKeyDelivery.h). The walk covers every command of
// the Transport and Timeline categories, so a new row has to be classified here before it ships.

namespace {

class HiddenTimelineToast : public KeyboardDesktopShell {
protected:
    void SetUp() override {
        KeyboardDesktopShell::SetUp();
        if (mc_ == nullptr)
            return;
        if (mc_->isBottomDockConfiguredVisible())
            mc_->toggleBottomPanelFromShortcut();
        mc_->getAudioEngine().getTransport().setLoop(0.0, 8.0, false); // a loop range, so Loop can switch on
        mc_->resized();
        for (auto* child : mc_->getChildren())
            if (toast_ == nullptr)
                toast_ = dynamic_cast<synth::ui::ToastComponent*>(child);
    }

    synth::ui::ToastComponent& toast() { return *toast_; }

    // The key of an action; an action with no default key (the track step) is given a free chord for the run.
    juce::KeyPress keyFor(const juce::String& actionId) {
        auto& shortcuts = mc_->getShortcutManager();
        auto key = shortcuts.getBinding(actionId);
        if (key.isValid())
            return key;
        for (int i = 0; i < 12; ++i) {
            const juce::KeyPress candidate(juce::KeyPress::F1Key + i,
                                           juce::ModifierKeys(juce::ModifierKeys::ctrlModifier |
                                                              juce::ModifierKeys::altModifier |
                                                              juce::ModifierKeys::shiftModifier),
                                           0);
            if (shortcuts.getConflictingAction(actionId, candidate).isEmpty()) {
                shortcuts.setBinding(actionId, candidate);
                temporaryBindings_.push_back(actionId);
                return candidate;
            }
        }
        return {};
    }

    void releaseTemporaryBindings() {
        for (const auto& actionId : temporaryBindings_)
            mc_->getShortcutManager().setBinding(actionId, juce::KeyPress());
        temporaryBindings_.clear();
    }

    void TearDown() override {
        if (mc_ != nullptr)
            releaseTemporaryBindings();
        KeyboardDesktopShell::TearDown();
    }

    // Presses `key` from the graph canvas and returns what the toast says afterwards, empty when none is shown.
    juce::String pressAndRead(const juce::KeyPress& key) {
        toast().dismissNow();
        // The canvas takes the key when it can; with it out of the main area the window's own root gets it.
        juce::Component& target = takeFocus(mc_->getGraphEditor())
                                      ? static_cast<juce::Component&>(mc_->getGraphEditor())
                                      : static_cast<juce::Component&>(*mc_);
        deliver(target, key);
        synth::test::flushMessageQueue(); // the click, then the toast queued behind it
        toast().finishMotionForTest();
        releaseTemporaryBindings();
        return toast().isToastShown() ? toast().getMessageForTest() : juce::String();
    }

    std::vector<juce::String> temporaryBindings_;
    synth::ui::ToastComponent* toast_ = nullptr;
};

juce::Button& toggleButton(MainComponent& mc, const juce::String& action) {
    auto& bar = mc.getTimelinePanel().getTransportBar();
    if (action == "transportToggleLoop")
        return bar.getLoopButton();
    if (action == "transportRecord")
        return bar.getRecordButton();
    return bar.getMetronomeButton();
}

// What the toggle says now: Loop's light follows the transport on the next poll, so its own state is read there.
bool stateOf(MainComponent& mc, const juce::String& action) {
    if (action == "transportToggleLoop")
        return mc.getAudioEngine().getTransport().getDocumentState().loopEnabled;
    return toggleButton(mc, action).getToggleState();
}

// Every command of the two categories is one of: a toggle of a control on the bar (says "<word> on" or "<word> off"), a
// timeline-only command (says the timeline is closed), or one of the plain transport moves that need no sentence.
const std::map<std::string, std::string> kToggles = {
    {"transportToggleLoop", "Loop"}, {"transportRecord", "Record"}, {"transportToggleMetronome", "Metronome"}};
const std::set<std::string> kPlainTransportMoves = {"togglePlayback",
                                                    "transportPlay",
                                                    "transportStop",
                                                    "setSongKey",
                                                    "transportReturnToStart",
                                                    "transportNudgeBackBeat",
                                                    "transportNudgeForwardBeat",
                                                    "transportNudgeBackBar",
                                                    "transportNudgeForwardBar",
                                                    "transportJumpToLoopStart",
                                                    "transportJumpToLoopEnd",
                                                    "transportJumpToNextMarker",
                                                    "transportJumpToPreviousMarker"};

bool isTimelineOnly(const std::string& category, const std::string& actionId) {
    return category == "Timeline" || actionId == "selectNextTrack" || actionId == "selectPreviousTrack";
}

} // namespace

TEST_F(HiddenTimelineToast, EveryTransportAndTimelineCommandIsClassifiedAndAnswersOnAClosedTimeline) {
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    int toggles = 0, timelineOnly = 0, plain = 0;
    for (const auto& spec : mc_->getCommandTableForTest()) {
        const std::string category = spec.category != nullptr ? spec.category : "";
        const std::string actionId = spec.actionId != nullptr ? spec.actionId : "";
        if (category != "Transport" && category != "Timeline" && !isTimelineOnly(category, actionId))
            continue;
        SCOPED_TRACE(actionId);
        if (kToggles.count(actionId) != 0) {
            ++toggles;
            const juce::String word = kToggles.at(actionId);
            const auto key = keyFor(actionId);
            ASSERT_TRUE(key.isValid());
            // Two presses: each toast names the state the press left, whichever way it started. (A headless run has
            // no audio thread, so Loop and Record may refuse; the toast then says "off", as the control does.)
            for (int press = 0; press < 2; ++press) {
                const auto message = pressAndRead(key);
                EXPECT_EQ(message, word + (stateOf(*mc_, actionId) ? " on" : " off")) << "press " << press;
                EXPECT_EQ(toast().getActionButton().getButtonText(), "Show timeline");
            }
        } else if (isTimelineOnly(category, actionId)) {
            ++timelineOnly;
            const auto key = keyFor(actionId);
            ASSERT_TRUE(key.isValid());
            EXPECT_EQ(pressAndRead(key), "Timeline is closed");
            EXPECT_EQ(toast().getActionButton().getButtonText(), "Show timeline");
        } else if (kPlainTransportMoves.count(actionId) != 0) {
            ++plain;
        } else {
            ADD_FAILURE() << "unclassified " << category << " command '" << actionId
                          << "': add it to kToggles (a state on the closed bar says what it did), the timeline-only "
                             "commands, or kPlainTransportMoves (with the reason it needs no sentence)";
        }
    }
    EXPECT_EQ(toggles, 3);
    EXPECT_GE(timelineOnly, 20); // the snap grid values and cycle, and the two track steps
    EXPECT_EQ(plain, (int)kPlainTransportMoves.size());
}

// A command that is active only while the timeline shows must be one this feature knows about: a new row gated on
// isTimelineReachable() that forgot it would be silent again.
TEST_F(HiddenTimelineToast, EveryCommandThatWaitsForTheTimelineAnswersOnAClosedTimeline) {
    const auto inactiveIds = [this] {
        std::set<std::string> ids;
        for (const auto& spec : mc_->getCommandTableForTest()) {
            juce::ApplicationCommandInfo info(spec.id);
            mc_->getCommandInfo(spec.id, info);
            if ((info.flags & juce::ApplicationCommandInfo::isDisabled) != 0)
                ids.insert(spec.actionId != nullptr ? spec.actionId : std::string("<menu only>"));
        }
        return ids;
    };
    const auto closed = inactiveIds();
    mc_->toggleBottomPanelFromShortcut();
    ASSERT_TRUE(mc_->isBottomDockConfiguredVisible());
    const auto open = inactiveIds();
    for (const auto& spec : mc_->getCommandTableForTest()) {
        const std::string actionId = spec.actionId != nullptr ? spec.actionId : "<menu only>";
        if (closed.count(actionId) == 0 || open.count(actionId) != 0)
            continue;
        const std::string category = spec.category != nullptr ? spec.category : "";
        EXPECT_TRUE(isTimelineOnly(category, actionId))
            << actionId << " is inactive only while the timeline is closed, so its key must say so; teach "
            << "MainComponent::announceClosedTimelineKey about it and list it in isTimelineOnly here";
    }
}

TEST_F(HiddenTimelineToast, NothingIsToastedWhileTheTimelineShows) {
    mc_->toggleBottomPanelFromShortcut();
    ASSERT_TRUE(mc_->isBottomDockConfiguredVisible());
    mc_->resized();
    for (const auto& [actionId, word] : kToggles) {
        const auto message = pressAndRead(keyFor(actionId));
        EXPECT_TRUE(message.isEmpty()) << actionId << " toasted '" << message << "' beside the bar that shows it";
        pressAndRead(keyFor(actionId)); // back to where it was (Record stops, so no take is left running)
    }
    for (const auto* actionId : {"snapSetWhole", "snapCycleNext", "selectNextTrack"})
        EXPECT_NE(pressAndRead(keyFor(actionId)), "Timeline is closed") << actionId;
}

TEST_F(HiddenTimelineToast, ATimelineInTheMainAreaCountsAsShowing) {
    ASSERT_TRUE(mc_->setMainViewForTest(Tab::Timeline));
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    mc_->resized();
    const auto message = pressAndRead(keyFor("transportToggleLoop"));
    EXPECT_TRUE(message.isEmpty()) << "toasted '" << message << "'";
    EXPECT_NE(pressAndRead(keyFor("snapSetWhole")), "Timeline is closed");
    pressAndRead(keyFor("transportToggleLoop")); // restore
}

TEST_F(HiddenTimelineToast, TheShowTimelineActionOpensTheTimeline) {
    const bool loopWas = stateOf(*mc_, "transportToggleLoop");
    EXPECT_EQ(pressAndRead(keyFor("transportToggleLoop")), loopWas ? "Loop off" : "Loop on");
    ASSERT_FALSE(mc_->isBottomDockConfiguredVisible());
    toast().getActionButton().triggerClick();
    synth::test::flushMessageQueue();
    EXPECT_TRUE(mc_->isBottomDockConfiguredVisible());
}
