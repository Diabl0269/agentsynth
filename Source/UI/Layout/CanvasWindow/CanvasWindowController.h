#pragma once

#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/DetachablePanelHost/DockBackButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class ShortcutManager;

// CanvasWindowController.h (docs/layout/chrome.md#the-canvas-window): moves the canvas (its toolbar on top, the
// GraphEditor under it) between the app window and a DetachedPanelWindow of its own, like the Timeline's and the
// Mixer's windows. Both are borrowed by reference and only reparented, so the canvas's view, selection and cards stay
// the same instances. The owner puts them back in its own tree (onReattach) and destroys this controller before
// either of them.
namespace synth::ui {

class CanvasToolbar;

class CanvasWindowController {
public:
    CanvasWindowController(CanvasToolbar& toolbar, juce::Component& canvas, juce::ApplicationProperties* appProperties,
                           synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager);
    ~CanvasWindowController();

    /** Opens the canvas in its window (true) or docks it back (false). Idempotent. Docking back calls onReattach
     *  first, then onDetachedStateChanged; opening calls only the latter. */
    void setDetached(bool detached);
    bool isDetached() const noexcept { return window_ != nullptr; }
    /** Raises the window and focuses the canvas in it; a no-op while docked. */
    void bringToFront();

    /** Holds edits in the window while a project loads (DetachedPanelWindow::setEditsBlocked). Remembered, so a window
     *  opened mid-load starts blocked. */
    void setEditsBlocked(bool blocked, std::function<void()> onRefused);
    /** Re-skins the window after a theme switch (a separate top-level window the app's pass never reaches). */
    void refreshTheme();
    /** Same contract as DetachablePanelHost::setCreatesNativeWindows: only the app's and the plugin's real construction
     *  sites pass true, so a headless test never creates a native peer. */
    void setCreatesNativeWindows(bool shouldCreate) noexcept { createsNativeWindows_ = shouldCreate; }

    /** Puts the toolbar and the canvas back in the owner's tree at their old place, before the window goes. */
    std::function<void()> onReattach;
    std::function<void()> onDetachedStateChanged;
    /** The app-wide shortcuts the window retries a key through (DetachedPanelWindow::onAppShortcut). */
    std::function<bool(const juce::KeyPress&)> onAppShortcut;

    // ---- Testing hooks ----
    DetachedPanelWindow* getWindowForTest() const noexcept { return window_.get(); }
    DockBackButton& getDockBackButtonForTest() noexcept { return dockBackButton_; }

private:
    class Content;

    CanvasToolbar& toolbar_;
    juce::Component& canvas_;
    juce::ApplicationProperties* appProperties_ = nullptr;
    synth::theme::AppLookAndFeel* lookAndFeel_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;
    DockBackButton dockBackButton_;
    bool createsNativeWindows_ = false;
    bool editsBlocked_ = false;
    std::function<void()> onEditRefused_;
    std::unique_ptr<Content> content_;
    std::unique_ptr<DetachedPanelWindow> window_; // after content_: it goes first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CanvasWindowController)
};

} // namespace synth::ui
