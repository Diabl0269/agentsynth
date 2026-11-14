// EQCurveSelection.cpp -- which bands of an EQCurveComponent are selected: the shared PointSelectionModel (keyed by
// band slot), box select on empty space (PointBoxSelect), Shift+arrow extend, select all, delete-selects-next and the
// drag of a whole selection (docs/control/point-editing.md). The curve is declared in EQCurveComponent.h.
//
// The model's keys must run in order along the line, and a band's slot is not its place in frequency (a duplicate lands
// in the lowest free slot), so Shift+arrow and Cmd+A run the model on each band's rank in frequency among the bands
// that are on and translate back.

#include "EQCurveComponent.h"
#include "EQCurveHoldState.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {
using Module = ParametricEQModule;
using synth::ui::PointSelectionModel;
} // namespace

const std::vector<int>& EQCurveComponent::getSelectedBands() const noexcept { return hold_->selection.selected(); }

bool EQCurveComponent::isBandSelected(int band) const { return hold_->selection.contains(band); }

// The bands that are on, lowest frequency first.
std::vector<int> EQCurveComponent::bandsByFrequency() const {
    std::vector<int> bands;
    for (int b = 0; b < Module::kMaxBands; ++b)
        if (lastBands[(size_t)b].enabled)
            bands.push_back(b);
    std::stable_sort(bands.begin(), bands.end(),
                     [this](int a, int b) { return lastBands[(size_t)a].freqHz < lastBands[(size_t)b].freqHz; });
    return bands;
}

void EQCurveComponent::setSelection(std::vector<int> bands, int cursor, int anchor) {
    cursor = juce::jlimit(-1, Module::kMaxBands - 1, cursor);
    const auto optional = [](int band) { return band < 0 ? std::optional<int>() : std::optional<int>(band); };
    const bool changed = hold_->selection.assign(std::move(bands), optional(cursor), optional(anchor));
    if (!changed && cursor == selectedBand)
        return;
    selectedBand = cursor;
    refreshAccessibilityValue();
    if (ownsSolo_)
        followSelectionSolo();
    repaint();
    listeners_.call([](Listener& l) { l.eqCurveChanged(); });
}

void EQCurveComponent::setSelectedBand(int band) {
    band = juce::jlimit(-1, Module::kMaxBands - 1, band);
    setSelection(band < 0 ? std::vector<int>{} : std::vector<int>{band}, band, band);
}

namespace {
// The selection as ranks among `order` (the bands that are on, by frequency); a band that is off has no rank.
struct RankView {
    explicit RankView(const std::vector<int>& orderOf)
        : order(orderOf) {}
    std::optional<int> rankOf(int slot) const {
        const auto it = std::find(order.begin(), order.end(), slot);
        return it == order.end() ? std::nullopt : std::optional<int>((int)(it - order.begin()));
    }
    std::vector<int> pickable() const {
        std::vector<int> ranks(order.size());
        for (size_t i = 0; i < ranks.size(); ++i)
            ranks[i] = (int)i;
        return ranks;
    }
    const std::vector<int>& order;
};
} // namespace

void EQCurveComponent::extendSelectionRelative(int direction) {
    const auto order = bandsByFrequency();
    const RankView view(order);
    PointSelectionModel<int> model;
    std::vector<int> ranks;
    for (const int slot : hold_->selection.selected())
        if (const auto r = view.rankOf(slot))
            ranks.push_back(*r);
    const auto cursor = hold_->selection.cursor() ? view.rankOf(*hold_->selection.cursor()) : std::nullopt;
    const auto anchor = hold_->selection.anchor() ? view.rankOf(*hold_->selection.anchor()) : std::nullopt;
    model.assign(ranks, cursor, anchor);
    if (!model.extend(direction, view.pickable(), HoldState::kRules).has_value())
        return; // already at the end of the line
    std::vector<int> slots;
    for (const int r : model.selected())
        slots.push_back(order[(size_t)r]);
    setSelection(std::move(slots), order[(size_t)*model.cursor()],
                 order[(size_t)model.anchor().value_or(*model.cursor())]);
}

bool EQCurveComponent::selectAllBands() {
    const auto order = bandsByFrequency();
    const RankView view(order);
    PointSelectionModel<int> model;
    std::vector<int> ranks;
    for (const int slot : hold_->selection.selected())
        if (const auto r = view.rankOf(slot))
            ranks.push_back(*r);
    const auto cursor = hold_->selection.cursor() ? view.rankOf(*hold_->selection.cursor()) : std::nullopt;
    model.assign(ranks, cursor, std::nullopt);
    if (!model.selectAll(view.pickable(), HoldState::kRules))
        return false;
    std::vector<int> slots;
    for (const int r : model.selected())
        slots.push_back(order[(size_t)r]);
    setSelection(std::move(slots), order[(size_t)*model.cursor()], order[(size_t)*model.anchor()]);
    return true;
}

// Delete: every selected band that is on goes off in one undo step, and the selection moves to the band after them in
// frequency (else the last one left), like the other editors. Backspace keeps the selection on the switched-off bands,
// so Return can bring one back.
bool EQCurveComponent::removeSelectedBands(bool selectNext) {
    std::vector<int> doomed;
    for (const int band : hold_->selection.selected())
        if (eqModule.isBandEnabled(band))
            doomed.push_back(band);
    if (doomed.empty())
        return false;
    float highest = 0.0f;
    for (const int band : doomed)
        highest = std::max(highest, lastBands[(size_t)band].freqHz);
    if (std::find(doomed.begin(), doomed.end(), eqModule.getSoloBand()) != doomed.end()) {
        ownsSolo_ = false;
        eqModule.clearSolo(); // nothing would be left to hear
    }
    beginGesture();
    for (const int band : doomed)
        eqModule.setBandEnabled(band, false);
    endGesture();
    refreshFromModule();
    if (!selectNext)
        return true;
    int next = -1;
    for (const int band : bandsByFrequency()) {
        next = band; // ends on the first band above the removed ones, else on the last one left
        if (lastBands[(size_t)band].freqHz > highest)
            break;
    }
    setSelection(next < 0 ? std::vector<int>{} : std::vector<int>{next}, next, next);
    return true;
}

// Where bands are drawn, so a dot gliding after a move is boxed where it is seen.
std::vector<int> EQCurveComponent::bandsInBox(juce::Rectangle<float> box) const {
    std::vector<int> inside;
    for (int b = 0; b < Module::kMaxBands; ++b)
        if (lastBands[(size_t)b].enabled && box.contains(bandPosition(dotBands_[(size_t)b])))
            inside.push_back(b);
    return inside;
}

void EQCurveComponent::dragBoxTo(juce::Point<float> position) {
    const auto next =
        hold_->box.dragTo(position.roundToInt(), [this](juce::Rectangle<float> r) { return bandsInBox(r); });
    if (!next)
        return;
    std::vector<int> bands = *next;
    std::sort(bands.begin(), bands.end());
    bands.erase(std::unique(bands.begin(), bands.end()), bands.end());
    int last = -1; // the cursor is the band highest in frequency
    for (const int band : bands)
        if (last < 0 || lastBands[(size_t)band].freqHz >= lastBands[(size_t)last].freqHz)
            last = band;
    setSelection(std::move(bands), last, last);
}

// A drag of one of several selected bands: the dragged band follows the pointer and the others move by the same
// number of octaves and dB (each stops at its own limit).
void EQCurveComponent::dragSelectionTo(juce::Point<float> position) {
    float anchorFreq = 0.0f, anchorGain = 0.0f;
    for (const auto& [band, place] : dragStart_)
        if (band == dragBand) {
            anchorFreq = place.x;
            anchorGain = place.y;
        }
    if (anchorFreq <= 0.0f)
        return;
    moveBandsFrom(dragStart_, std::log2(freqAtX(position.x) / anchorFreq), gainAtY(position.y) - anchorGain);
    refreshFromModule();
}

bool EQCurveComponent::moveBandsFrom(const std::vector<std::pair<int, juce::Point<float>>>& start, float octaves,
                                     float db) {
    bool moved = false;
    for (const auto& [band, place] : start) {
        const float freq = juce::jlimit(Module::kMinFreq, Module::kMaxFreq, place.x * std::pow(2.0f, octaves));
        const float gain = juce::jlimit(-Module::kMaxGainDb, Module::kMaxGainDb, place.y + db);
        const auto& now = lastBands[(size_t)band];
        if (freq == now.freqHz && gain == now.gainDb)
            continue;
        eqModule.setBandFreq(band, freq);
        eqModule.setBandGain(band, gain);
        moved = true;
    }
    return moved;
}
