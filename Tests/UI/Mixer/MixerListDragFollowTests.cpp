// MixerListDragFollowTests.cpp -- the mixer's row reorders inside a section frame (an insert row, a send row) scroll
// the frame while the pointer is held at its edge, faster nearer the edge, with the dragged
// row or column staying under the pointer; moving back in, the release and Esc stop it, and the drop is one step. The
// checklist is ../Layout/ViewportDragFollow.h; real mouse events go through each component's own handlers.

#include "../Layout/ViewportDragFollowEvents.h"
#include "MixerInsertListSplitFixture.h"
#include "UI/Mixer/MixerSections/MixerSectionViewport.h"
#include "UI/Mixer/MixerSendList.h"
#include <memory>

using viewport_drag_follow::DragTarget;
using viewport_drag_follow::surfaceOn;

namespace {

constexpr int kFrameWidth = 160;
constexpr int kFrameHeight = 120;

// A long insert chain in a short section frame, as a column puts it.
struct InsertFollowRig : mixer_split_test::SplitListRig {
    synth::ui::MixerSectionViewport frame;
    InsertFollowRig() {
        auto previous = source;
        for (int i = 0; i < 20; ++i) {
            const auto next = add("Delay", 300 + i * 20, 400);
            wire(previous, next);
            previous = next;
        }
        wire(previous, strip);
        refresh();
        frame.setList(list);
        frame.setSize(kFrameWidth, kFrameHeight);
        frame.setContentHeight(list.getPreferredHeight());
    }
};

// Twenty send rows in a short section frame; the move is reported to a counter instead of a graph.
struct SendFollowRig {
    synth::ui::MixerSendList list;
    synth::ui::MixerSectionViewport frame;
    int moves = 0;
    SendFollowRig() {
        std::vector<synth::MixerSendEntry> entries;
        for (int i = 0; i < 20; ++i) {
            synth::MixerSendEntry entry;
            entry.slot = i;
            entry.targetName = "Bus " + juce::String(i + 1);
            entries.push_back(entry);
        }
        list.moveSendRow = [this](juce::AudioProcessorGraph::NodeID, int, int) {
            ++moves;
            return true;
        };
        list.setEntries(entries, {});
        frame.setList(list);
        frame.setSize(kFrameWidth, kFrameHeight);
        frame.setContentHeight(list.getPreferredHeight());
    }
};

} // namespace

TEST(MixerListDragFollow, AnInsertRowHeldAtTheSectionEdgeScrollsTheSectionAndStaysUnderThePointer) {
    viewport_drag_follow::expectFollows([] {
        auto rig = std::make_shared<InsertFollowRig>();
        constexpr int kPressed = 2;
        DragTarget t;
        t.keepAlive = rig;
        t.viewport = &rig->frame;
        t.target = [r = rig.get()] { return static_cast<juce::Component*>(&r->list); };
        t.cross = 5.0f;
        t.pressAt = (float)(kPressed * 18 + 9);
        t.draggedPosition = [r = rig.get()] {
            return (float)r->frame
                .getLocalPoint(&r->list, juce::Point<int>(0, (int)r->list.getRowDrawTopForTest(kPressed)))
                .y;
        };
        t.commits = [r = rig.get()] { return r->mutations; };
        t.escape = [r = rig.get()] { return r->list.sendEscapeToRowDragForTest(); };
        return surfaceOn(t);
    });
}

TEST(MixerListDragFollow, ASendRowHeldAtTheSectionEdgeScrollsTheSectionAndStaysUnderThePointer) {
    viewport_drag_follow::expectFollows([] {
        auto rig = std::make_shared<SendFollowRig>();
        constexpr int kPressed = 2;
        DragTarget t;
        t.keepAlive = rig;
        t.viewport = &rig->frame;
        t.target = [r = rig.get()] { return static_cast<juce::Component*>(&r->list); };
        t.cross = 5.0f;
        t.pressAt = (float)(kPressed * 20 + 10);
        t.draggedPosition = [r = rig.get()] {
            return (float)r->frame.getLocalPoint(&r->list, juce::Point<int>(0, r->list.getRowTopForTest(kPressed))).y;
        };
        t.commits = [r = rig.get()] { return r->moves; };
        t.escape = [r = rig.get()] { return r->list.sendEscapeToRowDragForTest(); };
        return surfaceOn(t);
    });
}
