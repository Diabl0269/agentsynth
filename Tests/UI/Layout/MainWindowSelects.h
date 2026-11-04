#pragma once

// MainWindowSelects.h: the select fields (every juce::ComboBox, which covers KeyField, FollowKeyField, NoteLengthBox
// and the mod matrix's picker combos) of a real MainComponent, found by walking its component tree in each scene that
// shows some: the canvas with a few cards and the Mod Matrix, the timeline with a track, a clip, the routing pane and
// the piano roll's scale panel. A new select field joins the sweeps by existing; nothing here lists a class.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <functional>
#include <vector>

namespace main_window_selects {

inline bool isOnScreenInside(const juce::Component& c, const juce::Component& root) {
    for (const auto* p = &c; p != nullptr; p = p->getParentComponent()) {
        if (p == &root)
            return true; // the root itself (a window not yet shown) is not asked
        if (!p->isVisible() || p->getAlpha() <= 0.0f || p->getWidth() <= 0 || p->getHeight() <= 0)
            return false;
    }
    return false;
}

inline void collectInto(juce::Component& root, juce::Component& node, std::vector<juce::ComboBox*>& out) {
    for (auto* child : node.getChildren()) {
        if (!isOnScreenInside(*child, root))
            continue;
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child); combo != nullptr && combo->isEnabled()) {
            out.push_back(combo);
            continue; // a combo's own label is not a select
        }
        collectInto(root, *child, out);
    }
}

// Every visible, enabled combo box under `root`.
inline std::vector<juce::ComboBox*> collect(juce::Component& root) {
    std::vector<juce::ComboBox*> found;
    collectInto(root, root, found);
    return found;
}

// Shows each scene in turn and calls `visit(sceneName, selects)` with the selects it shows.
inline void forEachScene(MainComponent& mc,
                         const std::function<void(const char*, std::vector<juce::ComboBox*>&)>& visit) {
    using Tab = synth::ui::BottomDockComponent::Tab;
    synth::ui::FadeVisibility::setAnimateOffScreenForTest(false); // a hidden pane is hidden at once
    mc.setSize(1400, 900);
    mc.newPatchForTest();

    // Canvas: a card of each kind that carries a note-length box or a key field, and the Mod Matrix.
    mc.setMainViewForTest(Tab::Canvas);
    auto& editor = mc.getGraphEditor();
    int x = 40;
    for (const char* module : {"LFO", "Sequencer", "Delay", "Oscillator"}) {
        editor.addModuleAtCanvasPosition(module, {x, 60}, {});
        x += 360;
    }
    mc.simulateToggleModMatrixClick();
    mc.resized();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    {
        auto selects = collect(mc);
        visit("canvas", selects);
    }
    mc.simulateToggleModMatrixClick();

    // Timeline: the transport bar's Key field, the routing pane, then the piano roll's scale panel.
    mc.setMainViewForTest(Tab::Timeline);
    auto& doc = mc.getTimelineDoc();
    const auto track = doc.addTrack(synth::TrackKind::Midi, "Track 1");
    const auto clip = doc.addClip(track, 0.0, 8.0, "Clip");
    auto& panel = mc.getTimelinePanel();
    panel.selectTracks({track}, track);
    panel.toggleSidePane(true);
    mc.resized();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    {
        auto selects = collect(mc);
        visit("timeline", selects);
    }
    panel.openPianoRoll(clip);
    panel.getPianoRoll().toggleScalePanel();
    mc.resized();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    {
        auto selects = collect(mc);
        visit("piano roll", selects);
    }
}

} // namespace main_window_selects
