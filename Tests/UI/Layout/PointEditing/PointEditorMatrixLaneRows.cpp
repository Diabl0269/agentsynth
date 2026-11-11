// PointEditorMatrixLaneRows.cpp -- the matrix row for an automation lane (and a modulation lane, which is the same
// editor): three points at beats 1, 2 and 5 on an open lane of a real timeline panel with its own undo manager.
#include "../../Timeline/AutomationLanes/AutomationLanesTestFixture.h"
#include "PointEditorMatrixDriver.h"
#include "PointEditorMatrixHoldHost.h"
#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"

namespace point_matrix {
namespace {

using synth::ui::AutomationLaneEditor;

class Lane
    : public PointDriver
    , private automation_lanes_test::LanesPanel {
public:
    Lane() {
        const auto track = doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane_ = addLane(track, "cutoff");
        panel.setTrackAutomationExpanded(track, true);
        editor_ = panel.laneEditorForTest(lane_);
        editor_->setTool(AutomationLaneEditor::Tool::Pointer);
        doc.addBreakpoint(lane_, 1.0, 20.0);
        doc.addBreakpoint(lane_, 2.0, 60.0);
        doc.addBreakpoint(lane_, 5.0, 40.0);
    }

    juce::Component& surface() override { return *editor_; }
    int points() const override { return (int)pts().size(); }
    int selected() const override { return editor_->getPointSelection().size(); }
    int selectedIndex() const override {
        const auto cursor = editor_->getPointSelection().getCursor();
        for (int i = 0; cursor && i < points(); ++i)
            if (pts()[(size_t)i].beat == *cursor)
                return i;
        return -1;
    }
    bool selectedDot(int point) const override {
        return editor_->getPointSelection().contains(pts()[(size_t)point].beat);
    }
    double position(int point) const override { return pts()[(size_t)point].beat; }
    double value(int point) const override { return pts()[(size_t)point].value; }
    juce::Point<float> dot(int point) override {
        const auto& p = pts()[(size_t)point];
        return {(float)panel.getViewState().beatToX(p.beat), (float)editor_->valueToY(p.value)};
    }
    // A spot that snaps to a beat with no point on it, between the second point and the third.
    juce::Point<float> emptySpot() override {
        return {(float)panel.getViewState().beatToX(3.4), (float)editor_->valueToY(50.0)};
    }
    int editSteps() const override { return undo.getEditSerial(); }
    bool key(int code, int mods) override { return editor_->keyPressed(keyOf(code, mods)); }
    // Cmd+D reaches a lane as the app's edit command, not as a key.
    bool duplicate() override { return editor_->duplicateSelectedPoints(); }

    double heldPosition(int) const override { return editor_->getPreviewBeatForTest(); }
    double heldValue(int) const override { return editor_->getPreviewValueForTest(); }
    std::optional<HoldHooks> hold() override {
        HoldHooks h;
        auto* e = editor_;
        h.install = [e, keys = h.keys] { e->getKeyHoldForTest().host = hostOf(keys); };
        h.tick = [e] { e->getKeyHoldForTest().tick(); };
        h.active = [e] { return e->getKeyHoldForTest().active(); };
        h.keyUp = [e] { e->keyStateChanged(false); };
        h.tapMs = synth::ui::lane_key::kTapMs;
        return h;
    }

    std::optional<juce::Rectangle<int>> rightClick(int point) override {
        auto& hook = synth::ui::test_hooks::laneMenuHookForTest();
        std::optional<juce::Rectangle<int>> area;
        hook = [&area](const juce::PopupMenu&, const juce::PopupMenu::Options& options) {
            area = options.getTargetScreenArea();
        };
        rightPress(dot(point));
        hook = nullptr;
        return area;
    }

    std::string dragToEdge() override {
        const double before = panel.getViewState().firstVisibleBeat;
        const auto from = dot(movePoint());
        press(from);
        dragTo({(float)editor_->getWidth() - 2.0f, from.y});
        for (int i = 0; i < 30 && editor_->isEdgeScrollRunningForTest(); ++i)
            editor_->tickEdgeScrollForTest();
        const double after = panel.getViewState().firstVisibleBeat;
        release({(float)editor_->getWidth() - 2.0f, from.y});
        return after > before ? "" : "the view did not scroll";
    }

private:
    const std::vector<synth::ui::LaneBreakpoint>& pts() const { return doc.getLane(lane_)->points; }

    synth::LaneId lane_;
    AutomationLaneEditor* editor_ = nullptr;
};

} // namespace

void addLaneRows(std::vector<MatrixRow>& rows) {
    rows.push_back({"Automation lane", "AutomationLaneEditor", [] { return std::make_unique<Lane>(); }});
}

} // namespace point_matrix
