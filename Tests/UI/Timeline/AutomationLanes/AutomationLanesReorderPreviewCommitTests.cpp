// AutomationLanesReorderPreviewCommitTests.cpp -- the three reorder drags on the automation lanes: a lane header, a
// control chip in edit mode, and a modulator row. After the LAST drag event the order drawn on screen is the order the
// drop commits. Real mouse down/drag/up through each component's own handlers
// (../../Layout/ReorderDragPreviewCommit.h); the drawn order is read from where the items stand, the committed order
// from the doc, the strip's set and the lane's rows.

#include "../../../TestSettingsHelpers.h"
#include "../../Layout/ReorderDragPreviewCommit.h"
#include "AutomationLanesModulatorHost.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"
#include <algorithm>
#include <cmath>

using namespace automation_lanes_test;
using namespace lane_menu_test;
using namespace modulator_rows_test;
using namespace reorder_preview_commit;
using synth::ui::AutomationLaneHeaderComponent;
using synth::ui::LaneControl;
using synth::ui::LaneControlKind;
using synth::ui::LaneControlStrip;
using synth::ui::ModulatorRow;

namespace {

using Pt = juce::Point<float>;

// Answers the first lane's parameter with an LFO routing, so that lane has a modulator row and a hidden amount lane.
struct LaneHostRPC : ModulatorHost {
    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String& param) override {
        return param == "cutoff" ? ModulatorHost::getModulators(uuid, param) : std::vector<ModulatorInfo>{};
    }
};

// Track "Bass": cutoff (with one modulator row, so its hidden amount lane sits in the doc right after it), res, detune.
struct LaneRigRPC {
    LaneHostRPC host;
    LanesPanel f;
    synth::TrackId bass;
    synth::LaneId cutoff, res, detune;

    LaneRigRPC() {
        host.modulators = {lfoInfo("1")};
        f.panel.setTrackHeaderHost(&host);
        bass = f.doc.addTrack(synth::TrackKind::Midi, "Bass");
        cutoff = f.addLane(bass, "cutoff");
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = 0.0f;
        range.maxValue = 1.0f;
        range.defaultValue = 0.5f;
        f.doc.addLane(bass, "atten-1", "amount", range);
        res = f.addLane(bass, "res");
        detune = f.addLane(bass, "detune");
        f.panel.setTrackAutomationExpanded(bass, true);
    }
    ~LaneRigRPC() { f.panel.setTrackHeaderHost(nullptr); }

    AutomationLaneHeaderComponent& header(synth::LaneId id) { return *f.panel.laneHeaderForTest(id); }

    // The doc's lane order without the hidden amount lane.
    std::vector<synth::LaneId> shownDocOrder() {
        std::vector<synth::LaneId> ids;
        for (const auto& lane : f.doc.getTrack(bass)->lanes)
            if (lane.id == cutoff || lane.id == res || lane.id == detune)
                ids.push_back(lane.id);
        return ids;
    }
};

// Presses lane `from`'s header and drags so the lifted header's top lands on lane `to`'s block top.
void dragLaneHeader(LaneRigRPC& rig, synth::LaneId from, synth::LaneId to, const std::vector<synth::LaneId>& expected) {
    const auto before = rig.shownDocOrder();
    auto& header = rig.header(from);
    const Pt pressLocal(40.0f, (float)header.getHeight() * 0.5f);
    const auto pressScreen = header.localPointToGlobal(pressLocal.toInt());
    const float grab = pressLocal.y;
    const float toTop = (float)rig.header(to).localPointToGlobal(juce::Point<int>(0, 0)).y;
    const float fromTop = (float)header.localPointToGlobal(juce::Point<int>(0, 0)).y;
    const Pt press((float)pressScreen.x, (float)pressScreen.y);
    const Pt drop((float)pressScreen.x, toTop + grab);
    ASSERT_NE(toTop, fromTop);
    const auto local = [&](Pt p) { return header.getLocalPoint(nullptr, p.toInt()).toFloat(); };

    Probe<synth::LaneId> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            header.mouseDown(makeClickEvent(header, pressLocal, leftButton()));
        else if (phase == Phase::Drag)
            header.mouseDrag(makeDragEvent(header, local(p), pressLocal, leftButton()));
        else
            header.mouseUp(makeDragEvent(header, local(p), pressLocal, leftButton()));
    };
    probe.shown = [&] {
        std::vector<std::pair<synth::LaneId, int>> items;
        for (const auto id : before)
            items.emplace_back(id, rig.header(id).getY());
        return orderedByPosition(items);
    };
    probe.committed = [&] { return rig.shownDocOrder(); };
    const auto outcome = dragAndRelease(probe, press, straightPath(press, drop));
    EXPECT_EQ(outcome.preview, expected);
    expectCommitsWhatItPreviewed(outcome, before);
}

// ---- Control chips ------------------------------------------------------------------------------------------------

struct ChipRigRPC {
    synth::test::PersistedKeysGuard keys{
        juce::StringArray{synth::ui::laneControlSettingsKey(LaneControlKind::Automation),
                          synth::ui::laneControlSettingsKey(LaneControlKind::Mod)}};
    PickHost host;
    LanesPanel f;
    synth::TrackId track;
    synth::LaneId lane;

    ChipRigRPC() {
        track = f.doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane = f.addLane(track, "cutoff");
        host.offer("Filter 1", "Cutoff", "cutoff", {0.0f, 100.0f, 50.0f});
        f.panel.setTrackHeaderHost(&host);
        f.panel.setTrackAutomationExpanded(track, true);
        f.panel.getViewState().firstVisibleBeat = 0.0;
    }
    ~ChipRigRPC() { f.panel.setTrackHeaderHost(nullptr); }
    LaneControlStrip& strip() { return f.panel.laneHeaderForTest(lane)->getControlStrip(); }
};

using Controls = std::vector<LaneControl>;

// The gap the open slot shows: the chips other than `dragged` stand one slot apart with one slot left empty; the
// dragged chip's place in the order is that empty slot.
Controls drawnOrderWithGap(LaneControlStrip& strip, LaneControl dragged, const Controls& all) {
    std::vector<std::pair<LaneControl, int>> items;
    for (const auto control : all)
        if (control != dragged)
            items.emplace_back(control, strip.chipFor(control)->getX());
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    int pitch = 1 << 20;
    for (size_t i = 1; i < items.size(); ++i)
        pitch = std::min(pitch, items[i].second - items[i - 1].second);
    Controls order;
    for (const auto& item : items)
        order.push_back(item.first);
    std::vector<bool> taken(items.size() + 1, false);
    for (const auto& item : items) {
        const int slot = (int)std::lround((double)item.second / (double)pitch);
        if (slot >= 0 && slot < (int)taken.size())
            taken[(size_t)slot] = true;
    }
    size_t gap = 0;
    while (gap < taken.size() && taken[gap])
        ++gap;
    order.insert(order.begin() + (long)std::min(gap, order.size()), dragged);
    return order;
}

// Presses chip `dragged` and drags it so that `after` neighbours stand left of it (strip coordinates).
void dragChip(ChipRigRPC& rig, LaneControl dragged, int after, const Controls& /*expected*/) {
    auto& strip = rig.strip();
    const auto before = strip.visibleControls();
    auto* chip = strip.chipFor(dragged);
    ASSERT_NE(chip, nullptr);
    const float pitch = (float)(strip.chipFor(before[1])->getX() - strip.chipFor(before[0])->getX());
    const float width = (float)chip->getWidth();
    const Pt press = chip->getLocalBounds().getCentre().toFloat();
    const Pt pressInStrip = Pt((float)chip->getX(), (float)chip->getY()) + press;
    const float x = after == 0 ? 1.0f : pitch * ((float)after - 0.5f) + width * 0.5f;
    const Pt drop(x, pressInStrip.y);
    const auto local = [&](Pt p) { return chip->getLocalPoint(&strip, p); };

    Probe<LaneControl> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            chip->mouseDown(makeClickEvent(*chip, press, leftButton()));
        else if (phase == Phase::Drag)
            chip->mouseDrag(makeDragEvent(*chip, local(p), press, leftButton()));
        else
            chip->mouseUp(makeDragEvent(*chip, local(p), press, leftButton()));
        if (phase == Phase::Drag)
            strip.stepForTest(1.0f); // lands the neighbours' slide
    };
    probe.shown = [&] { return drawnOrderWithGap(strip, dragged, before); };
    probe.committed = [&] { return strip.visibleControls(); };
    const auto outcome = dragAndRelease(probe, pressInStrip, straightPath(pressInStrip, drop, 10));
    expectCommitsWhatItPreviewed(outcome, before);
}

// ---- Modulator rows -----------------------------------------------------------------------------------------------

struct RowRigRPC {
    ModulatorHost host;
    LanesPanel f;
    synth::TrackId bass;
    synth::LaneId cutoff;

    RowRigRPC() {
        for (int i = 1; i <= 4; ++i)
            host.modulators.push_back(lfoInfo(juce::String(i)));
        f.panel.setTrackHeaderHost(&host);
        bass = f.doc.addTrack(synth::TrackKind::Midi, "Bass");
        cutoff = f.addLane(bass, "cutoff");
        f.panel.setTrackAutomationExpanded(bass, true);
    }
    ~RowRigRPC() { f.panel.setTrackHeaderHost(nullptr); }

    ModulatorRow* row(int i) { return f.panel.modulatorRowForTest(cutoff, i); }
    std::vector<juce::String> sources() {
        std::vector<juce::String> names;
        for (int i = 0; i < f.panel.getAutomationLanes().modulatorCount(cutoff); ++i)
            names.push_back(row(i)->getInfo().sourceUuid);
        return names;
    }
};

// Presses row `from`'s title and drags so the lifted row's top lands on row `to`'s slot.
void dragModulatorRow(RowRigRPC& rig, int from, int to, const std::vector<juce::String>& /*expected*/) {
    const auto before = rig.sources();
    std::vector<std::pair<juce::String, ModulatorRow*>> rows;
    for (int i = 0; i < (int)before.size(); ++i)
        rows.emplace_back(before[(size_t)i], rig.row(i));
    auto* row = rows[(size_t)from].second;
    const Pt pressLocal((float)ModulatorRow::kIndent + 30.0f, 5.0f);
    const auto pressScreen = row->localPointToGlobal(pressLocal.toInt());
    const float toTop = (float)rows[(size_t)to].second->localPointToGlobal(juce::Point<int>(0, 0)).y;
    const Pt press((float)pressScreen.x, (float)pressScreen.y);
    const Pt drop((float)pressScreen.x, toTop + pressLocal.y);
    const auto local = [&](Pt p) { return row->getLocalPoint(nullptr, p.toInt()).toFloat(); };

    Probe<juce::String> probe;
    probe.send = [&](Phase phase, Pt p) {
        if (phase == Phase::Down)
            row->mouseDown(makeClickEvent(*row, pressLocal, leftButton()));
        else if (phase == Phase::Drag)
            row->mouseDrag(makeDragEvent(*row, local(p), pressLocal, leftButton()));
        else
            row->mouseUp(makeDragEvent(*row, local(p), pressLocal, leftButton()));
    };
    probe.shown = [&] {
        std::vector<std::pair<juce::String, int>> items;
        for (const auto& [name, r] : rows)
            items.emplace_back(name, r->getY());
        return orderedByPosition(items);
    };
    probe.committed = [&] { return rig.sources(); };
    const auto outcome = dragAndRelease(probe, press, straightPath(press, drop));
    expectCommitsWhatItPreviewed(outcome, before);
}

} // namespace

TEST(AutomationLanesReorderPreviewCommit, ALaneHeaderDropCommitsTheOrderTheLastDragEventShowed) {
    LaneRigRPC rig;
    ASSERT_EQ(rig.shownDocOrder(), (std::vector<synth::LaneId>{rig.cutoff, rig.res, rig.detune}));
    // The first lane (the one with the hidden amount lane after it) down to the last slot, then the last back up.
    dragLaneHeader(rig, rig.cutoff, rig.detune, {rig.res, rig.detune, rig.cutoff});
    dragLaneHeader(rig, rig.cutoff, rig.res, {rig.cutoff, rig.res, rig.detune});
    dragLaneHeader(rig, rig.detune, rig.cutoff, {rig.detune, rig.cutoff, rig.res});
}

TEST(AutomationLanesReorderPreviewCommit, AControlChipDropCommitsTheOrderTheLastDragEventShowed) {
    ChipRigRPC rig;
    auto& strip = rig.strip();
    strip.enterEdit();
    strip.toggleControl(LaneControl::Mute);
    strip.toggleControl(LaneControl::Duplicate);
    strip.stepForTest(1.0f);
    ASSERT_EQ(strip.visibleControls(),
              (Controls{LaneControl::ReadWrite, LaneControl::AddMod, LaneControl::Mute, LaneControl::Duplicate}));
    // Right across two chips, then to the front, then the first to the end.
    dragChip(rig, LaneControl::ReadWrite, 2,
             {LaneControl::AddMod, LaneControl::Mute, LaneControl::ReadWrite, LaneControl::Duplicate});
    dragChip(rig, LaneControl::Duplicate, 0,
             {LaneControl::Duplicate, LaneControl::AddMod, LaneControl::Mute, LaneControl::ReadWrite});
    dragChip(rig, LaneControl::Duplicate, 3,
             {LaneControl::AddMod, LaneControl::Mute, LaneControl::ReadWrite, LaneControl::Duplicate});
}

TEST(AutomationLanesReorderPreviewCommit, AModulatorRowDropCommitsTheOrderTheLastDragEventShowed) {
    RowRigRPC rig;
    const auto s = rig.sources();
    ASSERT_EQ(s.size(), 4u);
    dragModulatorRow(rig, 0, 2, {s[1], s[2], s[0], s[3]});
    const auto now = rig.sources();
    dragModulatorRow(rig, 3, 1, {now[0], now[3], now[1], now[2]});
}
