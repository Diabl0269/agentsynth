#pragma once

// ThemeSweepSurfaces.h -- every surface the theme sweep renders: the dialogs (one per PopupMotion::launchDialog call
// site in Source), the other top-level windows (one per juce::DocumentWindow constructor), the alert box, and every
// module card. A call site that is not listed here fails ThemeSweep.EveryDialogCallSiteIsRegistered
// (docs/layout/theming.md, "Theme sweep guard").

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include <vector>

namespace synth::test::theme_sweep {

/** One theme made current the way the app does it: an AppLookAndFeel with the theme applied, installed as the
 *  Desktop default, and an anchor component that carries it (what a dialog centres around). Closing the dialogs and
 *  putting the previous default back happens in the destructor. */
class SweepEnvironment {
public:
    explicit SweepEnvironment(const synth::theme::Theme& theme);
    ~SweepEnvironment();
    SweepEnvironment(const SweepEnvironment&) = delete;
    SweepEnvironment& operator=(const SweepEnvironment&) = delete;

    const synth::theme::Theme& theme() const noexcept { return theme_; }
    synth::theme::AppLookAndFeel& lookAndFeel() noexcept { return lookAndFeel_; }
    juce::Component& anchor() noexcept { return anchor_; }

    /** Keeps `object` alive until the environment goes (after its dialogs are gone). */
    void keepAlive(std::shared_ptr<void> object) { kept_.push_back(std::move(object)); }

private:
    synth::theme::Theme theme_;
    synth::theme::AppLookAndFeel lookAndFeel_;
    juce::Component anchor_;
    juce::LookAndFeel* previousDefault_ = nullptr;
    std::vector<std::shared_ptr<void>> kept_;
};

/** A PopupMotion::launchDialog call site. `open` is empty for an exempt one, which then says why. */
struct DialogSurface {
    std::string name;
    std::string sourceFile; // repo-relative, e.g. "Source/UI/Assistant/SignInDialog.cpp"
    std::function<juce::DialogWindow*(SweepEnvironment&)> open;
    std::string exemptReason;
};

/** A juce::DocumentWindow constructor call. Only DetachedPanelWindow is swept (see buildDetachedPanelWindow). */
struct WindowSurface {
    std::string name;
    std::string sourceFile;
    bool swept = false;
    std::string exemptReason;
};

const std::vector<DialogSurface>& dialogSurfaces();
const std::vector<WindowSurface>& windowSurfaces();

/** A DetachedPanelWindow around a small panel (a label, a button, a toggle), kept alive by `env`. */
juce::DocumentWindow& buildDetachedPanelWindow(SweepEnvironment& env);

/** An alert box with a title, a message and two buttons, built by the app's look and feel. Owned by the caller. */
std::unique_ptr<juce::AlertWindow> buildAlertWindow(SweepEnvironment& env);

/** A module card: the engine, graph editor, processor and the ModuleComponent at its natural size. */
struct CardRig {
    virtual ~CardRig() = default;
    virtual juce::Component& card() = 0;
};

/** Every key of the AI module factory, sorted: each one is a card the sweep builds. */
std::vector<std::string> moduleTypeNames();

/** The card for module type `type`, or null with `whyNot` set when it cannot be built headless. */
std::unique_ptr<CardRig> buildCard(const std::string& type, std::string& whyNot);

} // namespace synth::test::theme_sweep
