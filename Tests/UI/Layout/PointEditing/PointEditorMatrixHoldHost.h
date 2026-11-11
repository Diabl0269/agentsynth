// PointEditorMatrixHoldHost.h -- installs the matrix's HoldKeys as an editor's key-glide host, so a held arrow reads
// the test's clock and key state.
#pragma once

#include "PointEditorMatrixDriver.h"
#include <memory>

namespace synth::ui {
class AutomationLaneEditor;
class CurveEditorComponent;
class MorphMap;
} // namespace synth::ui

namespace point_matrix {

void installHold(synth::ui::CurveEditorComponent& editor, const std::shared_ptr<HoldKeys>& keys);
void installHold(synth::ui::MorphMap& map, const std::shared_ptr<HoldKeys>& keys);
void installHold(synth::ui::AutomationLaneEditor& editor, const std::shared_ptr<HoldKeys>& keys);

} // namespace point_matrix
