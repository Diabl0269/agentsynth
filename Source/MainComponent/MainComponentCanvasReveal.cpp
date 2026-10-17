// MainComponentCanvasReveal.cpp -- where the canvas sits when it is not the main view: laid into the
// bottom panel's Canvas tab body, revealed on demand by the code that selects or frames a card, and the
// Minimap toggle that follows the canvas's size. The canvas stays a MainComponent child throughout: the
// dock only reports the rectangle (BottomDockComponent::getCanvasBodyBounds).
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

using synth::ui::BottomDockComponent;

// Cheap enough to run on every slide frame: GraphEditor::resized() re-fits the minimap, re-applies the
// content transform and clips the card accessibility to the new viewport, which is the same work a window
// drag already does on every frame.
void MainComponent::layoutCanvas(juce::Rectangle<int> mainArea) {
    if (bottomDock.getMainView() == BottomDockComponent::Tab::Canvas) {
        canvasInDockLayout_ = false;
        graphEditor.setVisible(true);
        graphEditor.setBounds(mainArea);
    } else {
        // Hidden, not zero-sized, while the Canvas tab is not showing: its timers and predicates then
        // read isShowing() as false and land synchronously.
        const bool showing = bottomDock.isCanvasBodyShowing() && bottomDock.isVisible();
        if (!canvasInDockLayout_) {
            canvasInDockLayout_ = true;
            canvasRecentrePending_ = true;
        }
        if (showing)
            graphEditor.setBounds(bottomDock.getCanvasBodyBounds() + bottomDock.getPosition());
        graphEditor.setVisible(showing);
        // Once, after the panel has finished sliding, so the fit measures its real height.
        if (showing && canvasRecentrePending_ && timelineSlide_.getProgress() >= 1.0f && graphEditor.getHeight() > 0) {
            canvasRecentrePending_ = false;
            graphEditor.fitViewToModules();
        }
    }
    refreshMinimapButton();
}

void MainComponent::ensureCanvasShowing() {
    if (bottomDock.getMainView() == BottomDockComponent::Tab::Canvas)
        return;
    ensureBottomDockOpen();
    bottomDock.setActiveTab(BottomDockComponent::Tab::Canvas);
}

juce::String MainComponent::getMainViewFocusRegionId() const {
    switch (bottomDock.getMainView()) {
    case BottomDockComponent::Tab::Timeline:
        return "timeline";
    case BottomDockComponent::Tab::Mixer:
        return "mixer";
    case BottomDockComponent::Tab::MidiRemote:
        return "midiRemote";
    case BottomDockComponent::Tab::Canvas:
        break;
    }
    return "canvas";
}

// The toggle stays a real toggle (the preference is untouched) except while the size floor hides the map:
// then there is nothing for it to show, and the tooltip says why.
void MainComponent::refreshMinimapButton() {
    const bool suppressed = graphEditor.isMinimapSuppressedBySize();
    toggleMinimapButton.setEnabled(!suppressed);
    if (suppressed) {
        toggleMinimapButton.setTooltip("Needs a taller canvas");
        return;
    }
    const juce::String base = graphEditor.isMinimapVisible() ? "Hide Minimap" : "Show Minimap";
    toggleMinimapButton.setTooltip(synth::ui::formatShortcutHint(
        base, ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding("toggleMinimap"))));
}
