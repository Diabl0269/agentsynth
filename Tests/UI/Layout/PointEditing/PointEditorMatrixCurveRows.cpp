// PointEditorMatrixCurveRows.cpp -- the matrix rows for the bare curve editor in Free mode and in Fixed (envelope)
// mode, the two shapes the envelope and LFO cards use, and the mod curve editor (a curve editor writing through an
// edit target into a real graph with an undo manager).
#include "../../ModuleViews/ModCurve/ModCurveTestRig.h"
#include "PointEditorMatrixCurveDriver.h"
#include "UI/ModuleViews/ModCurve/ModCurveEditorView.h"

namespace point_matrix {
namespace {

// A free curve with three loose points; one undo step per gesture the editor reports.
class FreeCurve : public CurveDriver {
public:
    FreeCurve() {
        curve_.setBounds(0, 0, 400, 160);
        curve_.setModel(looseCurveModel());
        curve_.onGestureStart = [this] { ++gestures_; };
    }
    synth::ui::CurveEditorComponent& curve() override { return curve_; }
    int editSteps() const override { return gestures_; }

private:
    synth::ui::CurveEditorComponent curve_;
    int gestures_ = 0;
};

// An envelope-shaped curve: origin and release end are pinned in time, only the sustain also moves in level.
class FixedCurve : public CurveDriver {
public:
    FixedCurve() {
        curve_.setBounds(0, 0, 400, 160);
        synth::ui::test::EnvelopeShape shape;
        shape.attack = 0.1;
        shape.hold = 0.1;
        shape.decay = 0.3;
        shape.release = 0.4;
        curve_.setModel(synth::ui::test::buildEnvelopeModel(shape));
        curve_.onGestureStart = [this] { ++gestures_; };
    }
    synth::ui::CurveEditorComponent& curve() override { return curve_; }
    int pinnedAtEnd() const override { return 0; }
    int movePoint() const override { return 2; } // the sustain
    int editSteps() const override { return gestures_; }

private:
    synth::ui::CurveEditorComponent curve_;
    int gestures_ = 0;
};

class ModCurveEditor : public CurveDriver {
public:
    ModCurveEditor()
        : target_(std::make_shared<synth::ui::ModCurveEditTarget>(*fixture_.editor, fixture_.attenId))
        , view_(target_) {
        view_.setBounds(0, 0, 400, 160);
        view_.editor().setModel(looseCurveModel());
        view_.showMenu = [](juce::PopupMenu&, juce::Rectangle<int>) {}; // no real popup in a test
    }
    ~ModCurveEditor() override { view_.onTargetGone = nullptr; }
    synth::ui::CurveEditorComponent& curve() override { return view_.editor(); }
    int editSteps() const override { return fixture_.undo.getEditSerial(); }
    synth::ui::CurveGeometryConfig geometryConfig() const override {
        synth::ui::CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        return config;
    }

private:
    Fixture fixture_;
    std::shared_ptr<synth::ui::ModCurveEditTarget> target_;
    synth::ui::ModCurveEditorView view_;
};

} // namespace

void addCurveRows(std::vector<MatrixRow>& rows) {
    rows.push_back({"Curve editor (free)", "CurveEditorComponent", [] { return std::make_unique<FreeCurve>(); }});
    rows.push_back({"Curve editor (fixed)", "", [] { return std::make_unique<FixedCurve>(); }});
    rows.push_back({"Mod curve editor", "", [] { return std::make_unique<ModCurveEditor>(); }});
}

} // namespace point_matrix
