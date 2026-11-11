// DialogListDragFollowTests.cpp -- the row reorders in the list dialogs (a macro's port rows here, the card layout
// editor's control rows in CardLayoutEditorGripPreviewCommitTests.cpp) scroll the list while the pointer is held at its
// top or bottom edge, faster nearer the edge, with the dragged row staying under the pointer; moving back in, the
// release and Esc stop it, and the drop is one step. The checklist is ../../Layout/ViewportDragFollow.h; real mouse
// events go through each row's grab handle.

#include "../../Layout/ViewportDragFollowEvents.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include <memory>

using viewport_drag_follow::DragTarget;
using viewport_drag_follow::surfaceOn;

namespace {

struct PortDialogRig {
    synth::ui::MacroPortConfigDialog dialog;
    int reorders = 0;

    static std::vector<synth::ui::MacroPortConfigDialog::PortRow> ports() {
        std::vector<synth::ui::MacroPortConfigDialog::PortRow> rows;
        for (int i = 0; i < 30; ++i) {
            synth::ui::MacroPortConfigDialog::PortRow row;
            row.nodeUuid = "uuid-" + juce::String(i);
            row.isInput = true;
            row.name = "Port " + juce::String(i + 1);
            rows.push_back(row);
        }
        return rows;
    }

    PortDialogRig()
        : dialog("My Macro", ports()) {
        dialog.onReorderPortTo = [this](const juce::String&, int) { ++reorders; };
    }
};

} // namespace

TEST(DialogListDragFollow, APortRowHeldAtTheListEdgeScrollsTheListAndStaysUnderThePointer) {
    viewport_drag_follow::expectFollows([] {
        auto rig = std::make_shared<PortDialogRig>();
        auto& dialog = rig->dialog;
        auto& content = dialog.getRowsContentForTest();
        auto* viewport = content.findParentComponentOfClass<juce::Viewport>();
        EXPECT_NE(viewport, nullptr);
        const auto middleOf = [&](int row) {
            return viewport->getLocalPoint(&content, juce::Point<int>(0, dialog.getRowBoundsForTest(row).getCentreY()))
                .y;
        };
        const int pressed = viewport_drag_follow::firstRowClearOfTheEdges(
            dialog.getRowCountForTest(), viewport->getMaximumVisibleHeight(), middleOf, [](int) { return true; });
        EXPECT_GE(pressed, 0);
        auto* handle = dialog.getRowDragHandleForTest(pressed);
        DragTarget t;
        t.keepAlive = rig;
        t.viewport = viewport;
        t.target = [d = &dialog, pressed] { return d->getRowDragHandleForTest(pressed); };
        t.cross = (float)viewport->getLocalPoint(handle, handle->getLocalBounds().getCentre()).x;
        t.pressAt = (float)middleOf(pressed);
        t.draggedPosition = [d = &dialog, viewport, pressed] {
            return (float)viewport
                ->getLocalPoint(&d->getRowsContentForTest(),
                                juce::Point<int>(0, d->getRowBoundsForTest(pressed).getY()))
                .y;
        };
        t.commits = [r = rig.get()] { return r->reorders; };
        t.escape = [d = &dialog] { return d->sendEscapeToRowDragForTest(); };
        return surfaceOn(t);
    });
}
