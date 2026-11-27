// Concern: CanvasWindowController's detach/dock-back state machine and the window's content (toolbar over canvas).
#include "CanvasWindowController.h"
#include "UI/Chrome/CanvasToolbar/CanvasToolbar.h"
#include "UI/Layout/DetachablePanelHost/DockBackSlot.h"

namespace synth::ui {

// The window's one panel: the toolbar on top at its usual height, the canvas under it. It is the DockBackSlot the
// window's owner hands the button to, and forwards it to the toolbar's right end like the Controllers panel does.
class CanvasWindowController::Content
    : public juce::Component
    , public DockBackSlot {
public:
    Content(CanvasToolbar& toolbar, juce::Component& canvas)
        : toolbar_(toolbar)
        , canvas_(canvas) {
        setTitle("Canvas");
        addAndMakeVisible(canvas_);
        addAndMakeVisible(toolbar_);
    }
    // A child still here (the controller torn down with the window open) is let go, never deleted: both are borrowed.
    ~Content() override {
        toolbar_.setDockBackButton(nullptr);
        for (auto* c : {static_cast<juce::Component*>(&toolbar_), &canvas_})
            if (c->getParentComponent() == this)
                removeChildComponent(c);
    }
    void setDockBackButton(juce::Button* button) override { toolbar_.setDockBackButton(button); }
    void resized() override {
        auto area = getLocalBounds();
        toolbar_.setBounds(area.removeFromTop(juce::jmin(CanvasToolbar::kHeight, area.getHeight() / 2)));
        canvas_.setBounds(area);
    }

private:
    CanvasToolbar& toolbar_;
    juce::Component& canvas_;
};

CanvasWindowController::CanvasWindowController(CanvasToolbar& toolbar, juce::Component& canvas,
                                               juce::ApplicationProperties* appProperties,
                                               synth::theme::AppLookAndFeel* lookAndFeel,
                                               ShortcutManager* shortcutManager)
    : toolbar_(toolbar)
    , canvas_(canvas)
    , appProperties_(appProperties)
    , lookAndFeel_(lookAndFeel)
    , shortcutManager_(shortcutManager) {
    dockBackButton_.setComponentID("dockBack");
    dockBackButton_.onClick = [this] { setDetached(false); };
}

// The window goes before its content, and the content lets the borrowed toolbar and canvas go without touching the
// owner's tree: at teardown the owner is going away too.
CanvasWindowController::~CanvasWindowController() {
    window_.reset();
    content_.reset();
}

void CanvasWindowController::setDetached(bool detached) {
    if (detached == isDetached())
        return;
    if (detached) {
        content_ = std::make_unique<Content>(toolbar_, canvas_);
        window_ = std::make_unique<DetachedPanelWindow>(*content_, "Canvas", "canvasWindowBounds", appProperties_,
                                                        lookAndFeel_, shortcutManager_);
        window_->onCloseRequested = [this] { setDetached(false); };
        window_->onAppShortcut = onAppShortcut;
        // The canvas first: raiseAndFocusPanel() and Cmd+digit focus it; Tab then reaches the toolbar.
        window_->registerHostedPanelFocusRegion("canvas", canvas_);
        window_->addHostedPanelFocusRegion("canvasToolbar", toolbar_);
        content_->setDockBackButton(&dockBackButton_);
        if (editsBlocked_)
            window_->setEditsBlocked(true, onEditRefused_);
        // A real peer before setVisible, as DetachablePanelHost::setDetached does; never on a display-less runner.
        if (createsNativeWindows_ && juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
            window_->addToDesktop();
        window_->setVisible(true);
        window_->toFront(true);
    } else {
        content_->setDockBackButton(nullptr);
        // The owner takes both back into its tree (at their old z-order) before the window and its content go.
        if (onReattach)
            onReattach();
        window_.reset();
        content_.reset();
    }
    if (onDetachedStateChanged)
        onDetachedStateChanged();
}

void CanvasWindowController::bringToFront() {
    if (window_ != nullptr)
        window_->raiseAndFocusPanel();
}

void CanvasWindowController::setEditsBlocked(bool blocked, std::function<void()> onRefused) {
    editsBlocked_ = blocked;
    onEditRefused_ = std::move(onRefused);
    if (window_ != nullptr)
        window_->setEditsBlocked(editsBlocked_, onEditRefused_);
}

void CanvasWindowController::refreshTheme() {
    if (window_ != nullptr)
        window_->sendLookAndFeelChange();
}

} // namespace synth::ui
