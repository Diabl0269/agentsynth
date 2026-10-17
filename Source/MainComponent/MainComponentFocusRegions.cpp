// MainComponentFocusRegions.cpp -- the focus-region registry's contents and Tab-cycle order. The order follows the
// layout: toolbar, library, the view in the main area, the dock's tab strip, the views in the dock, then the AI
// panel and the Mod Matrix. Rebuilt on every detach, redock and main-view change.
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerPanelComponent/MixerFocusRegion.h"

void MainComponent::rebuildFocusRegions() {
    // Cleared and rebuilt on every call so re-running it after a detach/redock or a swap never duplicates entries.
    // A hosted panel's detached-window focus region is registered once on its host, not here
    // (DetachablePanelHost::setHostedPanelFocusRegion).
    focusRegions_.clear();
    using Tab = synth::ui::BottomDockComponent::Tab;

    // The toolbar is chrome, always visible, with no closed state; no direct-focus shortcut targets it.
    focusRegions_.addRegion({"toolbar", &toolbar, nullptr, nullptr});
    focusRegions_.addRegion(
        {"library", &moduleLibrary, [this] { return isLibraryVisible; }, [this] { setLibraryVisible(true); }});

    // Open while the canvas is on screen: in the main area, or in the dock only on its Canvas tab.
    const auto addCanvas = [this] {
        // The canvas's own toolbar: one stop ahead of the canvas, open while the canvas is on screen.
        focusRegions_.addRegion({"canvasToolbar", &canvasToolbar_, [this] { return canvasToolbar_.isVisible(); },
                                 [this] { ensureCanvasShowing(); }});
        focusRegions_.addRegion(
            {"canvas", &graphEditor, [this] { return graphEditor.isVisible(); }, [this] { ensureCanvasShowing(); }});
    };
    // A view detached to its own window has nothing docked here to cycle to; Tab inside that window cycles its OWN
    // one-region registry instead (DetachedPanelWindow::keyPressed). In the main area a view is open whatever the
    // dock does (isViewOnScreen), and opening it there changes nothing.
    const auto addTimeline = [this] {
        if (bottomDock.getTimelineHost().isDetached())
            return;
        focusRegions_.addRegion({"timeline", &timelinePanel, [this] { return isViewOnScreen(Tab::Timeline); },
                                 [this] { showBottomDockTab(Tab::Timeline); }});
        // The side panels are Tab stops right after the track list, each only while on screen; Up/Down walk their
        // controls (FocusStepWithin.h).
        const auto timelineShowing = [this] { return isViewOnScreen(Tab::Timeline); };
        focusRegions_.addRegion(
            {"timelineRouting", &timelinePanel.getRoutingPane(),
             [this, timelineShowing] { return timelineShowing() && timelinePanel.getSidePane().isOpen(); }, nullptr});
        focusRegions_.addRegion({"scaleAssist", &timelinePanel.getPianoRoll().getScaleAssistPanel(),
                                 [this, timelineShowing] {
                                     return timelineShowing() && timelinePanel.getPianoRoll().isScalePanelShowing();
                                 },
                                 nullptr});
    };
    // Three placements, three shapes. Tab: open while the dock is open on the Mixer tab, or the Mixer is in the main
    // area. Window: the detached window's own registry covers it. Own panel: the same top-level window, so it needs
    // its own region here (the helper hardcodes the dock's Mixer tab, which is disabled in that placement).
    const auto addMixer = [this] {
        if (bottomDock.getMixerHost().isDetached())
            return;
        if (mixerPlacement_.getPlacement() == synth::ui::MixerPlacementController::Placement::Tab)
            synth::ui::registerMixerFocusRegion(focusRegions_, bottomDock, [this] { return isBottomDockVisible; });
        else if (mixerPlacement_.getPlacement() == synth::ui::MixerPlacementController::Placement::OwnPanel)
            focusRegions_.addRegion({"mixer", &bottomDock.getMixerPanel(),
                                     [this] { return mixerPlacement_.isOwnPanelShowing(); }, nullptr});
    };
    const auto addMidiRemote = [this] {
        if (bottomDock.getMidiRemoteHost().isDetached())
            return;
        focusRegions_.addRegion({"midiRemote", &bottomDock.getMidiRemotePanel(),
                                 [this] { return isViewOnScreen(Tab::MidiRemote); },
                                 [this] { showBottomDockTab(Tab::MidiRemote); }});
    };
    const auto addView = [&](Tab view) {
        switch (view) {
        case Tab::Canvas:
            return addCanvas();
        case Tab::Timeline:
            return addTimeline();
        case Tab::Mixer:
            return addMixer();
        case Tab::MidiRemote:
            return addMidiRemote();
        }
    };

    const Tab mainView = bottomDock.getMainView();
    addView(mainView);
    // The tab strip is one stop of its own, ahead of the views it selects. Open whenever the dock is showing with at
    // least one docked tab; no direct-focus shortcut targets it.
    focusRegions_.addRegion({"dockTabs", &bottomDock.getTabStripFocus(),
                             [this] { return isBottomDockVisible && bottomDock.hasAnyVisibleTab(); }, nullptr});
    for (const Tab view : {Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote})
        if (view != mainView)
            addView(view);

    focusRegions_.addRegion({"aiPanel", &aiChatComponent, [this] { return isAiPanelVisible; },
                             [this] {
                                 if (!isAiPanelVisible && toggleAiPanelButton.onClick)
                                     toggleAiPanelButton.onClick();
                             }});
    // No `open` callback: no direct-focus shortcut targets the Mod Matrix, and Tab-cycling never opens a region.
    focusRegions_.addRegion(
        {"modMatrix", &graphEditor.getModMatrix(), [this] { return graphEditor.isModMatrixVisible(); }, nullptr});
}
