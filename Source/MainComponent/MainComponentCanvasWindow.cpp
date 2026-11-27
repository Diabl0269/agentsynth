// MainComponentCanvasWindow.cpp -- the canvas in its own window (docs/layout/chrome.md#the-canvas-window): building
// the controller, the dock's hooks into it, and putting the toolbar and canvas back where they were on dock back.
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/CanvasWindow/CanvasWindowController.h"

using synth::ui::BottomDockComponent;

void MainComponent::wireCanvasWindow() {
    canvasWindow_ = std::make_unique<synth::ui::CanvasWindowController>(canvasToolbar_, graphEditor, &appProperties,
                                                                        lookAndFeel, &shortcutManager);
    canvasWindow_->onReattach = [this] { reattachCanvas(); };
    // The same relay the three panel windows use: Cmd+Z, Space, Cmd+J, Cmd+digits and the canvas's own edit keys.
    canvasWindow_->onAppShortcut = [this](const juce::KeyPress& key) { return keyPressed(key); };
    // Docked back into its tab, the canvas is shown there: the user just asked for it back.
    canvasWindow_->onDetachedStateChanged = [this] {
        const bool detached = canvasWindow_->isDetached();
        bottomDock.setCanvasAway(detached);
        if (!detached && bottomDock.getMainView() != BottomDockComponent::Tab::Canvas)
            showBottomDockTab(BottomDockComponent::Tab::Canvas);
        resized();
    };
    bottomDock.onCanvasDetachToggle = [this] { setCanvasDetached(!canvasWindow_->isDetached()); };
    bottomDock.onCanvasWindowRaise = [this] { canvasWindow_->bringToFront(); };

    // The main area is never left empty: when a detach elsewhere or a Mixer placement change takes the last view that
    // could stand in for the canvas, the canvas docks back into the main area.
    const auto keepMainAreaFilled = [this](std::function<void()>& hook) {
        hook = [this, inner = std::move(hook)] {
            if (inner)
                inner();
            if (canvasWindow_->isDetached() && bottomDock.getMainView() == BottomDockComponent::Tab::Canvas)
                canvasWindow_->setDetached(false);
        };
    };
    keepMainAreaFilled(bottomDock.onMainViewChanged);
    keepMainAreaFilled(bottomDock.onPanelDetachStateChanged);
}

// The toolbar leaves the dock's tab strip first (captions back on), so the window shows it as the main area does.
void MainComponent::setCanvasDetached(bool detached) {
    if (detached == canvasWindow_->isDetached() || (detached && !bottomDock.canCanvasLeave()))
        return;
    if (detached) {
        bottomDock.setStripAccessoryWidth(0);
        if (canvasToolbar_.setInStrip(false))
            applyToolbarLabels(toolbarNarrowMode_);
    }
    canvasWindow_->setDetached(detached);
}

// Back under everything that was above them: the canvas at the bottom of the stack (where addCanvasAndPanels put it),
// the toolbar right above it; layoutCanvas() raises the toolbar above the dock again when the canvas is a panel tab.
void MainComponent::reattachCanvas() {
    addChildComponent(graphEditor, 0);
    addChildComponent(canvasToolbar_, 1);
}
