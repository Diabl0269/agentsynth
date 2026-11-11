// PointEditorMatrixCurveDriver.h -- the driver every host of CurveEditorComponent shares (the bare editor, the shape
// window's curve, the Fixed envelope curve, the mod curve editor): the host supplies the editor, its undo count and
// how many nodes at the end are pinned. The curve editor's own mouse and key handlers do the work.
#pragma once

#include "../../ModuleViews/CurveEditor/CurveEditorTestHelpers.h"
#include "PointEditorMatrixDriver.h"
#include "PointEditorMatrixHoldHost.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>

namespace point_matrix {

/** Three loose points between two fixed ends, no two on a line, all well inside the range. */
inline synth::ui::CurveModel looseCurveModel() {
    using synth::ui::CurveMode;
    using synth::ui::CurveNode;
    synth::ui::CurveModel model(CurveMode::Free);
    model.setNodes({CurveNode{0.0, 0.5f, false, false}, CurveNode{0.25, 0.2f, true, true},
                    CurveNode{0.5, 0.6f, true, true}, CurveNode{0.75, 0.4f, true, true},
                    CurveNode{1.0, 0.5f, false, false}});
    return model;
}

class CurveDriver : public PointDriver {
public:
    virtual synth::ui::CurveEditorComponent& curve() = 0;
    /** Nodes at the start and the end that are not points (a free curve has a fixed end on each side). */
    virtual int pinnedAtStart() const { return 1; }
    virtual int pinnedAtEnd() const { return 1; }

    /** How the host draws the curve, which decides where its dots are on screen. */
    virtual synth::ui::CurveGeometryConfig geometryConfig() const { return {}; }

    juce::Component& surface() override { return curve(); }
    int points() const override {
        return const_cast<CurveDriver*>(this)->curve().getModel().getNumNodes() - pinnedAtStart() - pinnedAtEnd();
    }
    int selected() const override { return (int)const_cast<CurveDriver*>(this)->curve().getSelectedIndices().size(); }
    int selectedIndex() const override {
        const int i = const_cast<CurveDriver*>(this)->curve().getSelectedIndex();
        return i < 0 ? -1 : i - pinnedAtStart();
    }
    bool selectedDot(int point) const override {
        const auto& s = const_cast<CurveDriver*>(this)->curve().getSelectedIndices();
        return std::find(s.begin(), s.end(), point + pinnedAtStart()) != s.end();
    }
    double position(int point) const override {
        return const_cast<CurveDriver*>(this)->curve().getModel().getNode(point + pinnedAtStart()).x;
    }
    double value(int point) const override {
        return const_cast<CurveDriver*>(this)->curve().getModel().getNode(point + pinnedAtStart()).y;
    }
    juce::Point<float> dot(int point) override {
        auto& c = curve();
        return synth::ui::CurveEditorGeometry(c.getModel(), c.getLocalBounds().toFloat(), geometryConfig())
            .nodePosition(point + pinnedAtStart());
    }
    bool key(int code, int mods) override { return curve().keyPressed(keyOf(code, mods)); }

    std::optional<HoldHooks> hold() override {
        auto& c = curve();
        HoldHooks h;
        h.install = [&c, keys = h.keys] { installHold(c, keys); };
        h.tick = [&c] { c.getKeyHoldForTest().tick(); };
        h.active = [&c] { return c.getKeyHoldForTest().active(); };
        h.keyUp = [&c] { c.keyStateChanged(false); };
        h.tapMs = synth::ui::lane_key::kTapMs;
        return h;
    }

    // The editor reports a right-click with what was hit; the owner opens the menu where the pointer is.
    std::optional<juce::Rectangle<int>> rightClick(int point) override {
        auto& c = curve();
        std::optional<juce::Rectangle<int>> area;
        const auto hosted = c.onContextMenu;
        c.onContextMenu = [&](const juce::MouseEvent& e, synth::ui::CurveHitResult hit) {
            if (hit.kind == synth::ui::CurveHitKind::Node) {
                const auto at = c.localPointToGlobal(e.position).roundToInt();
                area = juce::Rectangle<int>(at.x, at.y, 1, 1);
            }
            if (hosted)
                hosted(e, hit);
        };
        rightPress(dot(point));
        c.onContextMenu = hosted;
        return area;
    }
};

} // namespace point_matrix
