// PointEditorMatrixHoldHost.cpp -- the one unit that sees KeyGlideHost, so the matrix's row files don't widen its
// reach (scripts/check-header-reach.sh).
#include "PointEditorMatrixHoldHost.h"
#include "UI/Layout/KeyGlide/KeyGlideHost.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/Timeline/AutomationLaneEditor.h"

namespace point_matrix {
namespace {
synth::ui::KeyGlideHost hostOf(const std::shared_ptr<HoldKeys>& keys) {
    synth::ui::KeyGlideHost h;
    h.nowMs = [keys] { return keys->now; };
    h.isArrowDown = [keys](int code) { return keys->down.count(code) > 0; };
    h.isShiftDown = [keys] { return keys->shift; };
    h.isAltDown = [keys] { return keys->alt; };
    return h;
}
} // namespace

void installHold(synth::ui::CurveEditorComponent& editor, const std::shared_ptr<HoldKeys>& keys) {
    editor.getKeyHoldForTest().host = hostOf(keys);
}

void installHold(EQCurveComponent& curve, const std::shared_ptr<HoldKeys>& keys) {
    curve.getKeyHoldForTest().host = hostOf(keys);
}

void installHold(synth::ui::MorphMap& map, const std::shared_ptr<HoldKeys>& keys) {
    map.setGlideHostForTest(hostOf(keys));
}

void installHold(synth::ui::AutomationLaneEditor& editor, const std::shared_ptr<HoldKeys>& keys) {
    editor.getKeyHoldForTest().host = hostOf(keys);
}

} // namespace point_matrix
