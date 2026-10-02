#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::theme {
class AppLookAndFeel;
}

namespace synth::ui {

class MixerPanelComponent;

// MixerMirrorController.h (docs/mixer/panel.md#placement-and-detachable-windows): owns
// the OPTIONAL second live Mixer view the "detachedPanelBothPlaces" preference adds. Unlike a
// normal detach (DetachablePanelHost reparents the SAME panel instance between its dock slot and a
// window -- a juce::Component has exactly one parent, so that mechanism can only ever show a panel
// in ONE place), "both places" needs a genuinely second MixerPanelComponent instance, configured
// against the SAME graph/doc/macros/undoManager/graphEditor/audioEngine the docked mixer keeps
// using, so it is a second live view of the same model rather than a clone of the model itself.
//
// Fader and pan positions stay in sync for free: both views' MixerFader / pan
// juce::SliderParameterAttachment bind to the SAME live juce::AudioParameterFloat, and JUCE
// notifies every registered listener on a parameter change regardless of which attachment wrote it
// -- see MixerFader::parameterGestureChanged for why a gesture bracketed from EITHER view still
// produces exactly one undo step (AppUndoManager::capturedBeforeState is a single shared field,
// guarded by `isVoid()`, so a second start/end pair from the other view's own listener is a
// harmless no-op). Mute/Solo/pan-law are NOT a listened parameter -- they are plain module/graph
// state or engine-owned settings -- so they stay in sync live through a separate, explicit path:
// MixerPanelComponent::onLiveMixerStateChanged, fired at the end of a real interactive
// mute/solo/pan-law change, cross-wired here (open(), below) and in BottomDockComponent so each
// instance's change fires the OTHER instance's cheap refreshLiveMixerVisuals() (no rebuild).
// Inserts/sends/renames already sync through the pre-existing onGraphMutated -> rebuild() path,
// which copyWiringFrom() does carry across (both instances share the one callback).
//
// Meters use their own MeterReader::MixerMirror slot (Source/Mixer/PeakMeterLatch.h): a
// consume-on-read latch shared with the primary's MeterReader::Mixer would otherwise silently
// steal each other's peak reads, flickering both meters.
//
// The window itself reuses DetachedPanelWindow directly (not through a second DetachablePanelHost
// -- there is nothing to dock the mirror INTO; it only ever exists as a window). `headerButton_`/
// `headerTitle_` are owned here and borrowed by the window exactly the way DetachablePanelHost's
// own detachButton_/titleLabel_ are borrowed by a real detach -- see DetachedPanelWindow's class
// comment. `window_` is declared LAST so it is destroyed FIRST (reverse declaration order): its
// Content wrapper holds references into mirror_/headerButton_/headerTitle_ that must still be
// alive while it unwinds.
class MixerMirrorController {
public:
    /** Configures a freshly created mirror view against the same model the docked panel uses --
     *  called once, the first time open() actually builds mirror_. */
    using ConfigureFn = std::function<void(MixerPanelComponent&)>;

    MixerMirrorController(juce::ApplicationProperties& appProperties, ConfigureFn configure);
    ~MixerMirrorController();

    bool isOpen() const noexcept { return window_ != nullptr; }

    /** Opens the mirror window (building + configuring + copy-wiring mirror_ the first time only,
     *  then re-copying the wiring and rebuilding on every open() in case it changed since) or, if
     *  already open, just brings the existing window to front. `sourcePanel` is the docked panel
     *  copyWiringFrom() reads from. `createsNativeWindow` mirrors DetachablePanelHost's own
     *  setCreatesNativeWindows() contract -- false (every headless test's value) leaves this a pure
     *  Component build with no native peer. */
    void open(MixerPanelComponent& sourcePanel, synth::theme::AppLookAndFeel* lookAndFeel,
              ShortcutManager* shortcutManager, std::function<bool(const juce::KeyPress&)> appShortcutFallback,
              bool createsNativeWindow);
    /** Closes the window (mirror_ itself is kept alive, so a later open() need not rebuild its
     *  scroll/selection state from nothing) -- idempotent, a no-op when already closed. */
    void close();

    /** unbindAllColumns()/rebuildIfUnbound() MUST reach mirror_ whenever it exists -- Source/UI/CLAUDE.md's
     *  "unbind before a graph-replacing mutation frees the nodes a column is bound to" invariant
     *  applies to every live MixerPanelComponent, not only the docked one. A no-op while closed:
     *  mirror_ is never left holding a stale binding across a close() (rebuild()/unbindAllColumns()
     *  only mutate stripColumns_, which the next open() rebuilds from scratch anyway via rebuild()
     *  below). */
    void unbindIfOpen();
    void rebuildIfUnboundIfOpen();
    /** BottomDockComponent::rebuildMixer()'s own extension point -- called unconditionally from
     *  there (mirror_ may not exist yet; the null check makes that free). */
    void rebuildIfOpen();
    void refreshTrackColoursIfOpen();
    void refreshMetersIfOpen();
    /** The cheap per-strip refresh (no rebuild) BottomDockComponent's own mixer_'s
     *  onLiveMixerStateChanged drives this with -- see the class comment. */
    void refreshLiveVisualsIfOpen();
    /** BottomDockComponent's own theme re-skin pass -- mirrors DetachablePanelHost::refreshDetachedWindowTheme(). */
    void refreshThemeIfOpen() {
        if (window_ != nullptr)
            window_->sendLookAndFeelChange();
    }

    /** Fires after every open()/close() -- BottomDockComponent re-runs refreshDetachButton() (the
     *  tab strip's own detach button reflects the mirror's open state while this preference and tab
     *  are both active, see BottomDockComponent::usesMixerMirrorForDetach()). */
    std::function<void()> onOpenedOrClosed;

    // ---- Testing hooks (MixerMirrorControllerTests.cpp / BottomDockComponentTests.cpp) ----
    MixerPanelComponent* getMirrorPanelForTest() const noexcept { return mirror_.get(); }
    DetachedPanelWindow* getMirrorWindowForTest() const noexcept { return window_.get(); }

private:
    juce::ApplicationProperties& appProperties_;
    ConfigureFn configure_;
    std::unique_ptr<MixerPanelComponent> mirror_;
    juce::DrawableButton headerButton_{"closeMixerMirror", juce::DrawableButton::ImageFitted};
    juce::Label headerTitle_;
    // Declared LAST -- see the class comment on destruction order.
    std::unique_ptr<DetachedPanelWindow> window_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerMirrorController)
};

} // namespace synth::ui
