// PointEditorMatrixHoldHost.h -- the key host a hold reads: the matrix's HoldKeys as the editor's KeyGlideHost.
#pragma once

#include "PointEditorMatrixDriver.h"
#include "UI/Layout/KeyGlide/KeyGlideHost.h"
#include <memory>

namespace point_matrix {

inline synth::ui::KeyGlideHost hostOf(const std::shared_ptr<HoldKeys>& keys) {
    synth::ui::KeyGlideHost h;
    h.nowMs = [keys] { return keys->now; };
    h.isArrowDown = [keys](int code) { return keys->down.count(code) > 0; };
    h.isShiftDown = [keys] { return keys->shift; };
    h.isAltDown = [keys] { return keys->alt; };
    return h;
}

} // namespace point_matrix
