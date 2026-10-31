#include "ModCurveEditorView.h"

#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"

namespace synth::ui {

ModCurveEditorView::ModCurveEditorView(std::shared_ptr<ModCurveEditTarget> target)
    : target_(std::move(target)) {
    editor_.setTitle("Curve editor");
    editor_.setLineClickEditing(true);
    // The x axis is the source's level, not time.
    editor_.setTimeLabelFormatter([](double) { return juce::String(); });
    addAndMakeVisible(editor_);
    if (auto curve = target_->read()) {
        shown_ = *curve;
        editor_.setModel(curveModelFromWave(shown_.shape));
    }

    editor_.onGestureStart = [this] {
        dragging_ = true;
        target_->beginGesture();
    };
    editor_.onGestureEnd = [this] {
        target_->commitGesture();
        dragging_ = false;
    };
    // The change callbacks fire inside the gesture, so each lands in the same undo step.
    editor_.onNodeChanged = [this](int) { pushToTarget(); };
    editor_.onBendChanged = [this](int) { pushToTarget(); };
    editor_.onPointsChanged = [this] { pushToTarget(); };
    editor_.onContextMenu = [this](const juce::MouseEvent&, CurveHitResult hit) {
        if (hit.kind == CurveHitKind::Node)
            editor_.removeNode(hit.index);
    };

    restoreHandle_ = target_->watchRestore([this] { refreshFromTarget(true); });
    changeHandle_ = target_->watchChanges([this] { refreshFromTarget(); });
}

void ModCurveEditorView::pushToTarget() {
    auto curve = target_->read();
    if (!curve)
        return;
    curve->shape = waveFromCurveModel(editor_.getModel());
    shown_ = *curve;
    target_->apply(*curve);
}

void ModCurveEditorView::refreshFromTarget(bool glide) {
    if (dragging_)
        return;
    const auto curve = target_->read();
    if (!curve) {
        if (!goneReported_ && onTargetGone) {
            goneReported_ = true;
            onTargetGone();
        }
        return;
    }
    // A range-only change (the box's handles) leaves the drawn shape alone.
    auto previous = shown_;
    previous.rangeMin = curve->rangeMin;
    previous.rangeMax = curve->rangeMax;
    shown_ = *curve;
    if (previous == *curve)
        return;
    editor_.setModel(curveModelFromWave(curve->shape), glide);
}

void ModCurveEditorView::resized() { editor_.setBounds(getLocalBounds()); }

} // namespace synth::ui
