// CardLayoutEditorGripPreviewCommitTests.cpp -- the list editor's row grip: the order the rows are drawn in after the
// last drag event is the layout the drop stores, including a drop under a group header (the control joins that group).
// Real mouse down/drag/up on the grip (../../Layout/ReorderDragPreviewCommit.h).

#include "../../Layout/ReorderDragPreviewCommit.h"
#include "CardLayoutEditorTestHelpers.h"
#include "Modules/FilterModule.h"

using namespace cardlayouteditor_test;
using namespace reorder_preview_commit;

namespace {

using Pt = juce::Point<float>;

// A Filter whose card draws the automatic layout written out as one stored section, so there is one group to start.
NodeID addFilterWithOneGroup(EditorCanvas& rig) {
    const auto id = rig.add(std::make_unique<FilterModule>());
    EXPECT_TRUE(synth::setCardLayoutOverride(rig.canvas.engine.getGraph(), nullptr, id,
                                             cardbody_test::automaticLayoutWith(*rig.canvas.processor(id), {})));
    rig.canvas.editor.updateComponents();
    return id;
}

std::vector<juce::String> rowKeys(CardLayoutEditorComponent& editor) {
    std::vector<juce::String> keys;
    for (int i = 0; i < editor.getVisibleRowCountForTest(); ++i)
        keys.push_back(editor.getVisibleRowParamIdForTest(i));
    return keys;
}

juce::MouseEvent gripEvent(juce::Component& grip, Pt local, Pt down, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &grip, &grip, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

// Presses the grip of the row `key` and drags so the pointer ends at list y `dropY`, then releases there.
Outcome<juce::String> dragRow(CardLayoutEditorComponent& editor, const juce::String& key, float dropY) {
    const int row = editor.findRowForTest(key);
    EXPECT_GE(row, 0) << key;
    auto* grip = editor.getRowDragHandleForTest(row);
    auto& content = editor.getRowsContentForTest();
    const float x = content.getLocalPoint(grip, grip->getLocalBounds().toFloat().getCentre()).x;
    const Pt press(x, (float)editor.getRowBoundsForTest(row).getCentreY());
    const auto local = [&](Pt p) { return grip->getLocalPoint(&content, p); };

    // The rows are rebuilt by the drop, so the keys and the shown positions are read through the editor each time.
    const auto before = rowKeys(editor);
    Probe<juce::String> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            grip->mouseDown(gripEvent(*grip, local(p), local(p), false));
        else if (phase == Phase::Drag)
            grip->mouseDrag(gripEvent(*grip, local(p), local(press), true));
        else
            grip->mouseUp(gripEvent(*grip, local(p), local(press), true));
    };
    probe.shown = [&] {
        std::vector<std::pair<juce::String, int>> items;
        for (int i = 0; i < (int)before.size(); ++i)
            items.emplace_back(before[(size_t)i], editor.getRowBoundsForTest(i).getCentreY());
        return orderedByPosition(items);
    };
    probe.committed = [&] { return rowKeys(editor); };
    return dragAndRelease(probe, press, straightPath(press, Pt(x, dropY)));
}

} // namespace

TEST(CardLayoutEditorGripPreviewCommit, ADropCommitsTheRowOrderTheLastDragEventShowed) {
    EditorCanvas rig;
    const auto id = addFilterWithOneGroup(rig);
    auto* editor = rig.openList(id);
    ASSERT_NE(editor, nullptr);
    editor->triggerAddGroupForTest();

    // Across the new group's header: the control joins that group and sits under its header.
    {
        const auto before = rowKeys(*editor);
        const float dropY = (float)editor->getRowBoundsForTest(rowOf(*editor, "#1")).getBottom() + 4.0f;
        const auto outcome = dragRow(*editor, "cutoff", dropY);
        auto expected = before;
        expected.erase(std::find(expected.begin(), expected.end(), "cutoff"));
        expected.push_back("cutoff");
        EXPECT_EQ(outcome.preview, expected);
        expectCommitsWhatItPreviewed(outcome, before);
    }
    // Within a group: "drive" dragged up past "resonance".
    {
        const auto before = rowKeys(*editor);
        const int from = rowOf(*editor, "drive");
        const float dropY = (float)editor->getRowBoundsForTest(from - 1).getY() + 2.0f;
        const auto outcome = dragRow(*editor, "drive", dropY);
        auto expected = before;
        std::swap(expected[(size_t)from], expected[(size_t)from - 1]);
        EXPECT_EQ(outcome.preview, expected);
        expectCommitsWhatItPreviewed(outcome, before);
    }
}
