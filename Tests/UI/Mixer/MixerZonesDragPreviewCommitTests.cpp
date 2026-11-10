// MixerZonesDragPreviewCommitTests.cpp -- a row drag in the Zones pane: the group and slot each row stands in after the
// LAST drag event are the zone and order the release commits.
#include "../Layout/ReorderDragPreviewCommit.h"
#include "MixerZonesTestRig.h"
#include <gtest/gtest.h>

namespace {

using namespace reorder_preview_commit;
using Pt = juce::Point<float>;
using synth::MixerZone;

const char* zoneName(MixerZone zone) { return zone == MixerZone::Left ? "L" : zone == MixerZone::Right ? "R" : "S"; }

/** Every listed channel as "<zone>:<id>", in the order drawn (top to bottom) after the last drag event. A row's zone is
 *  the nearest group heading above it. */
std::vector<std::string> shownSlots(MixerZonesRig& rig) {
    auto& pane = rig.panel->getZonesPaneForTest();
    std::vector<std::pair<std::string, int>> items;
    for (int i = 0; i < pane.getRowCountForTest(); ++i) {
        auto* row = pane.getRowForTest(i);
        int headerY = -1;
        std::string zone = "L";
        for (const auto z : {MixerZone::Left, MixerZone::Scrolling, MixerZone::Right}) {
            const int y = pane.getGroupHeaderForTest(z).getY();
            if (y < row->getY() && y > headerY) {
                headerY = y;
                zone = zoneName(z);
            }
        }
        items.emplace_back(zone + ":" + row->getChannel().id.toStdString(), row->getY());
    }
    return orderedByPosition(items);
}

/** The same slots as the pane lists them after the release rebuilt them from the committed zone and order. */
std::vector<std::string> committedSlots(MixerZonesRig& rig) {
    auto& pane = rig.panel->getZonesPaneForTest();
    std::vector<std::string> slots;
    for (const auto z : {MixerZone::Left, MixerZone::Scrolling, MixerZone::Right})
        for (int i = 0; i < pane.getRowCountForTest(); ++i) {
            const auto& channel = pane.getRowForTest(i)->getChannel();
            if (channel.zone == z)
                slots.push_back(std::string(zoneName(z)) + ":" + channel.id.toStdString());
        }
    return slots;
}

/** A pin to another group keeps only the group the row lands in: the pinned group lists its channels in mixer order, so
 *  the slot inside it is not something a drop can commit. The rest of the list is compared slot by slot. */
std::vector<std::string> keepOnlyTheZoneOf(std::vector<std::string> slots, const juce::String& id) {
    const auto suffix = ":" + id.toStdString();
    std::string zoneOfId;
    for (auto it = slots.begin(); it != slots.end(); ++it)
        if (it->size() > suffix.size() && it->compare(it->size() - suffix.size(), suffix.size(), suffix) == 0) {
            zoneOfId = it->substr(0, 1);
            slots.erase(it);
            break;
        }
    slots.push_back(zoneOfId + "@" + id.toStdString());
    return slots;
}

/** Drags the row of `id` to list y `dropY` and checks the drop commits the slots the last drag event drew. */
void dragRowTo(MixerZonesRig& rig, const juce::String& id, float dropY, bool zoneOnly = false) {
    auto* row = rig.row(id);
    auto& list = rig.panel->getZonesPaneForTest().getListContentForTest();
    const Pt press(20.0f, (float)(row->getY() + row->getHeight() / 2));
    const auto before = zoneOnly ? keepOnlyTheZoneOf(shownSlots(rig), id) : shownSlots(rig);
    const auto local = [&](Pt p) { return row->getLocalPoint(&list, p); };
    Probe<std::string> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            row->mouseDown(makeClickEvent(*row, local(p)));
        else if (phase == Phase::Drag)
            row->mouseDrag(makeDragEvent(*row, local(p), local(press)));
        else
            row->mouseUp(makeClickEvent(*row, local(p)));
    };
    probe.shown = [&] { return zoneOnly ? keepOnlyTheZoneOf(shownSlots(rig), id) : shownSlots(rig); };
    probe.committed = [&] { return zoneOnly ? keepOnlyTheZoneOf(committedSlots(rig), id) : committedSlots(rig); };
    const auto outcome = dragAndRelease(probe, press, straightPath(press, Pt(20.0f, dropY)));
    expectCommitsWhatItPreviewed(outcome, before);
}

} // namespace

TEST(MixerZonesDragPreviewCommit, AWithinGroupDropCommitsTheOrderTheLastDragEventShowed) {
    MixerZonesRig rig(3);
    const auto last = rig.stripId(2);
    dragRowTo(rig, last, (float)rig.row(rig.stripId(0))->getY() + 6.0f);
    const auto first = rig.stripId(0);
    dragRowTo(rig, first, (float)rig.row(rig.stripId(2))->getBottom() + 4.0f);
}

TEST(MixerZonesDragPreviewCommit, ACrossGroupDropCommitsTheZoneAndSlotTheLastDragEventShowed) {
    MixerZonesRig rig(2);
    const auto id = rig.stripId(0);
    dragRowTo(rig, id, (float)rig.row(id)->getY() + 400.0f, true); // the Right group
    dragRowTo(rig, id, -200.0f, true);                             // and back above the first heading: the Left group
}
