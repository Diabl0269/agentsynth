#pragma once

// The curve editor both the small box and the enlarged window show: the shared CurveEditorComponent in Free mode over
// the attenuverter's curve. It writes through a ModCurveEditTarget, one undo step per drag, and follows that curve
// when another view, an undo or a redo changes it.

#include "ModCurveEditTarget.h"
#include <functional>
#include <memory>

namespace synth::ui {

struct CurveHitResult;      // by value in showPointMenu; complete in UI/ModuleViews/CurveEditor/CurveEditorGeometry.h
class CurveEditorComponent; // held behind a pointer so the views that hold a box do not reach the whole editor

class ModCurveEditorView final : public juce::Component {
public:
    explicit ModCurveEditorView(std::shared_ptr<ModCurveEditTarget> target);
    ~ModCurveEditorView() override;

    /** Needs UI/ModuleViews/CurveEditor/CurveEditorComponent.h where it is used. */
    CurveEditorComponent& editor() noexcept { return *editor_; }
    /** Re-reads the target's curve; reports `onTargetGone` once when the attenuverter has vanished. */
    void refreshFromTarget(bool glide = false);
    /** Runs when the attenuverter is gone (deleted, or undone away). */
    std::function<void()> onTargetGone;

    /** Shows the right-click point menu (a seam for tests: it sees the menu and the screen area it opens at); unset
     *  opens it at the pointer. */
    std::function<void(juce::PopupMenu&, juce::Rectangle<int>)> showMenu;

    void resized() override;

private:
    void pushToTarget();
    void showPointMenu(const juce::MouseEvent& e, CurveHitResult hit);

    std::shared_ptr<ModCurveEditTarget> target_;
    std::unique_ptr<CurveEditorComponent> editor_;
    synth::ModCurve shown_;
    bool dragging_ = false;
    bool goneReported_ = false;
    std::shared_ptr<void> restoreHandle_;
    std::shared_ptr<void> changeHandle_;
};

} // namespace synth::ui
