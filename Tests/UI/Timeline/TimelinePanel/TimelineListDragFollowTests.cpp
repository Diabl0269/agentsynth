// TimelineListDragFollowTests.cpp -- the three reorders in the timeline's track list (a track row, a lane header, a
// modulator row) scroll the list while the pointer is held at its top or bottom edge, faster nearer the edge, with the
// dragged row staying under the pointer; moving back in, the release and Esc stop it, and the drop is one step. The
// checklist is ../../Layout/ViewportDragFollow.h; real mouse events go through each row's own handlers.

#include "../../Layout/ViewportDragFollow.h"
#include "../AutomationLanes/AutomationLanesModulatorHost.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include <memory>

using namespace automation_lanes_test;
using namespace modulator_rows_test;
using viewport_drag_follow::Surface;

namespace {

// Counts the track edits that reach the host (a track reorder's commit goes there), one per undo step in the app.
struct CountingHost : ModulatorHost {
    int edits = 0;
    void performTrackEdit(const std::function<void()>& mutation) override {
        ++edits;
        ModulatorHost::performTrackEdit(mutation);
    }
};

// A short panel over one long list, so the track list scrolls.
struct ListRig {
    CountingHost host;
    LanesPanel f;
    ListRig() {
        f.panel.setTrackHeaderHost(&host);
        f.panel.setSize(1200, 300);
    }
    ~ListRig() { f.panel.setTrackHeaderHost(nullptr); }
    juce::Viewport& viewport() { return f.panel.getTrackHeaderViewport(); }
};

// The surface for a row that stays the same component through the gesture: events are built in its own coordinates
// from a viewport position, rebuilt every call because the row moves and the list scrolls.
template <class Row>
Surface surfaceFor(std::shared_ptr<ListRig> rig, std::function<Row*()> row, float grabX, float grabY,
                   std::function<int()> commits, std::function<bool()> escape) {
    auto& viewport = rig->viewport();
    Surface s;
    s.keepAlive = rig;
    s.extent = [&viewport] { return viewport.getMaximumVisibleHeight(); };
    s.viewPosition = [&viewport] { return viewport.getViewPositionY(); };
    auto pressLocal = std::make_shared<juce::Point<float>>();
    const auto local = [&viewport, row, grabX](float y) {
        return row()->getLocalPoint(&viewport, juce::Point<float>(grabX, y));
    };
    s.press = [=](float y) {
        *pressLocal = local(y);
        row()->mouseDown(makeClickEvent(*row(), *pressLocal, leftButton()));
    };
    s.drag = [=](float y) { row()->mouseDrag(makeDragEvent(*row(), local(y), *pressLocal, leftButton())); };
    s.release = [=](float y) { row()->mouseUp(makeDragEvent(*row(), local(y), *pressLocal, leftButton())); };
    s.escape = std::move(escape);
    s.draggedPosition = [&viewport, row] { return (float)viewport.getLocalPoint(row(), juce::Point<int>()).y; };
    s.commits = std::move(commits);
    s.pressAt = (float)viewport.getLocalPoint(row(), juce::Point<int>()).y + grabY;
    return s;
}

} // namespace

TEST(TimelineListDragFollow, ATrackRowHeldAtTheListEdgeScrollsTheListAndStaysUnderThePointer) {
    viewport_drag_follow::expectFollows([] {
        auto rig = std::make_shared<ListRig>();
        for (int i = 0; i < 16; ++i)
            rig->f.doc.addTrack(synth::TrackKind::Midi, "Track " + juce::String(i + 1));
        const auto first = rig->f.doc.getTracks().front().id;
        auto* panel = &rig->f.panel;
        // The commit rebuilds the rows, so the dragged one is found again by its track every time.
        const auto row = [panel, first]() -> synth::ui::TimelineTrackHeaderComponent* {
            for (int i = 0; i < panel->getTrackHeaderCount(); ++i)
                if (auto* header = panel->getTrackHeaderAt(i); header != nullptr && header->getTrackId() == first)
                    return header;
            return nullptr;
        };
        return surfaceFor<synth::ui::TimelineTrackHeaderComponent>(
            rig, row, 3.0f, 10.0f, [r = rig.get()] { return r->host.edits; },
            [panel] { return panel->sendEscapeToTrackDragForTest(); });
    });
}

TEST(TimelineListDragFollow, ALaneHeaderHeldAtTheListEdgeScrollsTheListAndStaysUnderThePointer) {
    viewport_drag_follow::expectFollows([] {
        auto rig = std::make_shared<ListRig>();
        const auto track = rig->f.doc.addTrack(synth::TrackKind::Midi, "Bass");
        std::vector<synth::LaneId> lanes;
        for (int i = 0; i < 14; ++i)
            lanes.push_back(rig->f.addLane(track, "param" + juce::String(i)));
        rig->f.panel.setTrackAutomationExpanded(track, true);
        auto* panel = &rig->f.panel;
        const auto lane = lanes.front();
        auto* header = panel->laneHeaderForTest(lane);
        const float grabY = header != nullptr ? (float)header->getHeight() * 0.5f : 0.0f;
        return surfaceFor<synth::ui::AutomationLaneHeaderComponent>(
            rig, [panel, lane] { return panel->laneHeaderForTest(lane); }, 40.0f, grabY,
            [r = rig.get()] { return r->f.undo.getEditSerial(); },
            [panel] { return panel->getAutomationLanes().sendLaneDragEscapeForTest(); });
    });
}

TEST(TimelineListDragFollow, AModulatorRowHeldAtTheListEdgeScrollsTheListAndStaysUnderThePointer) {
    viewport_drag_follow::expectFollows([] {
        auto rig = std::make_shared<ListRig>();
        for (int i = 1; i <= 14; ++i)
            rig->host.modulators.push_back(lfoInfo(juce::String(i)));
        const auto track = rig->f.doc.addTrack(synth::TrackKind::Midi, "Bass");
        const auto lane = rig->f.addLane(track, "cutoff");
        rig->f.panel.setTrackAutomationExpanded(track, true);
        auto* panel = &rig->f.panel;
        return surfaceFor<synth::ui::ModulatorRow>(
            rig, [panel, lane] { return panel->modulatorRowForTest(lane, 0); },
            (float)synth::ui::ModulatorRow::kIndent + 30.0f, 5.0f,
            [r = rig.get()] { return r->f.undo.getEditSerial(); },
            [panel] { return panel->getAutomationLanes().sendModulatorDragEscapeForTest(); });
    });
}
