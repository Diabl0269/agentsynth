// DrumMachineComponent: the lanes under the grid. A row of lane chips (Loudness, Chance, Repeat, Nudge, Condition)
// with a "for <row>" note beside it; each chip grows or folds one `DrumHitLane` above the chips, the shown lanes
// stacked under the grid in chip order. Every lane edits the notes of the row picked by its name. A lane's gesture (a
// whole drag, or one key press) is ONE undo step through record(). docs/timeline/drum-machine.md#lanes-under-the-grid.

#include "DrumMachineComponent.h"

#include "UI/Layout/FadeVisibility.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

void DrumMachineComponent::setupLanes() {
    addAndMakeVisible(chips_);
    chips_.setComponentID("drumLaneChips");
    juce::StringArray names;
    for (int i = 0; i < kDrumLaneKindCount; ++i)
        names.add(drumLaneSpec(static_cast<DrumLaneKind>(i)).name);
    chips_.setLanes(names);
    chips_.onToggle = [this](int index, bool shown) { setLaneShown(static_cast<DrumLaneKind>(index), shown); };

    for (int i = 0; i < kDrumLaneKindCount; ++i) {
        const auto kind = static_cast<DrumLaneKind>(i);
        auto& band = bands_[(size_t)i];
        band.lane = std::make_unique<DrumHitLane>(kind);
        addChildComponent(*band.lane);
        DrumHitLane::Mapping mapping;
        mapping.gutterWidth = kLabelColumnWidth;
        mapping.stepRange = [this](int step) { return stepXRange(step); };
        mapping.stepAt = [this](int x) { return stepAtX(x); };
        band.lane->setMapping(std::move(mapping));
        band.lane->onCommit = [this, kind](const std::vector<DrumHitLane::Hit>& changed) { commitLane(kind, changed); };
    }
    labels_.onRowClicked = [this](int pitch) { setSelectedPitch(pitch); };
}

//==============================================================================
void DrumMachineComponent::setSelectedPitch(int pitch) {
    if (pitch < 0 || pitch > 127 || pitch == selectedPitch_)
        return;
    selectedPitch_ = pitch;
    pushRowsToLabels();
    refreshLane();
    repaint();
}

// Hands every lane the hits of the picked row (the value each lane shows is the first note's of its cell).
void DrumMachineComponent::refreshLane() {
    std::vector<std::pair<int, const drumgrid::CellHits*>> cells;
    for (const auto& [key, cell] : cells_)
        if (static_cast<int>(key / 1000000) == selectedPitch_ && !cell.empty())
            cells.emplace_back(static_cast<int>(key % 1000000), &cell);
    std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const auto row = drumLaneForPitch(selectedPitch_);
    for (int i = 0; i < kDrumLaneKindCount; ++i) {
        const auto kind = static_cast<DrumLaneKind>(i);
        std::vector<DrumHitLane::Hit> hits;
        for (const auto& [step, cell] : cells)
            hits.push_back({step, drumLaneValueOf(kind, *cell)});
        bands_[(size_t)i].lane->setHits(std::move(hits), row.name, row.colour);
    }
    repaint(forBounds_);
}

// One lane gesture as ONE undo step: every changed hit's notes get the lane's value.
void DrumMachineComponent::commitLane(DrumLaneKind kind, const std::vector<DrumHitLane::Hit>& changed) {
    std::vector<std::pair<std::vector<synth::NoteId>, int>> edits;
    for (const auto& hit : changed) {
        const auto it = cells_.find(drumgrid::cellKey(selectedPitch_, hit.step));
        if (it != cells_.end() && drumLaneValueOf(kind, it->second) != hit.value)
            edits.emplace_back(it->second.ids, hit.value);
    }
    if (edits.empty() || clip() == nullptr)
        return;
    record([this, kind, edits] {
        for (const auto& [ids, value] : edits)
            applyDrumLaneValue(*doc_, kind, ids, value);
    });
    refreshFromDoc();
}

juce::Range<int> DrumMachineComponent::stepXRange(int step) const {
    const auto* c = clip();
    if (c == nullptr || step < 0)
        return {};
    const double stepLen = getStepBeats();
    const int x0 = static_cast<int>(std::lround(beatToX(c->startBeat + step * stepLen)));
    const int x1 = static_cast<int>(std::lround(beatToX(c->startBeat + (step + 1) * stepLen)));
    return {x0, std::max(x0 + 1, x1)};
}

//==============================================================================
// Each shown lane is `laneUnitHeight()` tall (72 px, less when the panel is short so the rows keep a little room) times
// its reveal. The band under the rows is the stack of lanes, then the chips row.
int DrumMachineComponent::laneUnitHeight() const noexcept {
    float shown = 0.0f;
    for (const auto& band : bands_)
        shown += band.reveal;
    if (shown <= 0.0f)
        return kLaneHeight;
    const int room = getHeight() - canvasTop() - LaneChipRow::kHeight - 2 * kBottomPad - kMinGridHeight;
    return juce::jlimit(kMinLaneHeight, kLaneHeight, static_cast<int>(std::floor(static_cast<float>(room) / shown)));
}

int DrumMachineComponent::laneHeightOf(DrumLaneKind kind) const noexcept {
    return static_cast<int>(std::lround(static_cast<double>(laneUnitHeight()) * bands_[(size_t)kind].reveal));
}

int DrumMachineComponent::bottomBandHeight() const noexcept {
    int height = LaneChipRow::kHeight + 2 * kBottomPad;
    for (int i = 0; i < kDrumLaneKindCount; ++i)
        height += laneHeightOf(static_cast<DrumLaneKind>(i));
    return height;
}

int DrumMachineComponent::gridHeight() const noexcept {
    return std::max(0, getHeight() - canvasTop() - bottomBandHeight());
}

void DrumMachineComponent::layoutBottom() {
    const int chipsTop = getHeight() - LaneChipRow::kHeight - kBottomPad;
    chips_.setBounds(kBottomPad, chipsTop, std::max(0, getWidth() - 2 * kBottomPad), LaneChipRow::kHeight);
    int y = chipsTop - kBottomPad;
    for (int i = kDrumLaneKindCount - 1; i >= 0; --i)
        y -= laneHeightOf(static_cast<DrumLaneKind>(i));
    for (int i = 0; i < kDrumLaneKindCount; ++i) {
        const int height = laneHeightOf(static_cast<DrumLaneKind>(i));
        bands_[(size_t)i].lane->setBounds(0, y, getWidth(), height);
        bands_[(size_t)i].lane->setVisible(height > 0);
        y += height;
    }
    const int chipsRight =
        chips_.getNumChips() > 0 ? chips_.getX() + chips_.getChip(chips_.getNumChips() - 1)->getRight() : 0;
    forBounds_ = {chipsRight + 8, chipsTop, std::max(0, getWidth() - chipsRight - 16), LaneChipRow::kHeight};
}

void DrumMachineComponent::setLaneShown(DrumLaneKind kind, bool shown) {
    chips_.setShown(static_cast<int>(kind), shown);
    for (auto& band : bands_)
        band.from = band.reveal; // a lane already moving carries on from where it is
    bands_[(size_t)kind].to = shown ? 1.0f : 0.0f;
    startLaneMotion();
}

// The lanes grow under the grid (and fold back) over 160 ms; the rows' viewport follows each frame. They land at once
// when the grid is off screen or Animations is Off.
void DrumMachineComponent::startLaneMotion() {
    laneAnim_.stop(vblank_);
    const auto settle = [this] {
        for (auto& band : bands_)
            band.reveal = band.from = band.to;
        layoutBottom();
        layoutRows();
        repaint();
    };
    if (!motionAllowed()) {
        settle();
        return;
    }
    laneAnim_.start(
        vblank_, motionMs(kLaneMotionMs, kReducedMotionMs), easeOutCubic,
        [this](float t) {
            for (auto& band : bands_)
                band.reveal = band.from + (band.to - band.from) * t;
            layoutBottom();
            layoutRows();
            repaint();
        },
        settle);
}

} // namespace synth::ui
