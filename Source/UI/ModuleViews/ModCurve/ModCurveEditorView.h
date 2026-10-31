#pragma once

// The curve editor both the small box and the enlarged window show: the shared CurveEditorComponent in Free mode over
// the attenuverter's curve. It writes through a ModCurveEditTarget, one undo step per drag, and follows that curve
// when another view, an undo or a redo changes it.

#include "ModCurveEditTarget.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <functional>
#include <memory>

namespace synth::ui {

class ModCurveEditorView final : public juce::Component {
public:
    explicit ModCurveEditorView(std::shared_ptr<ModCurveEditTarget> target);

    CurveEditorComponent& editor() noexcept { return editor_; }
    /** Re-reads the target's curve; reports `onTargetGone` once when the attenuverter has vanished. */
    void refreshFromTarget(bool glide = false);
    /** Runs when the attenuverter is gone (deleted, or undone away). */
    std::function<void()> onTargetGone;

    void resized() override;

private:
    void pushToTarget();

    std::shared_ptr<ModCurveEditTarget> target_;
    CurveEditorComponent editor_;
    synth::ModCurve shown_;
    bool dragging_ = false;
    bool goneReported_ = false;
    std::shared_ptr<void> restoreHandle_;
    std::shared_ptr<void> changeHandle_;
};

} // namespace synth::ui
