// DrumMachineComponent: construction, opening and closing a clip, and the rows (lanes) it shows. Geometry, editing,
// motion, painting, mouse, keys and accessibility each live in a sibling DrumMachine<Concern>.cpp unit.
//
// Coordinates. The component's x origin is the clip-lane rect's left edge, so the panel's ruler can label the grid's
// beats through setMappingOverride(&getViewState(), leftGutterWidth()). Horizontally a beat maps to
// kLabelColumnWidth + view_.beatToX(beat); vertically rows stack from canvasTop() minus the scroll. Notes are
// clip-relative in the document, so every cell maps through clip->startBeat.

#include "DrumMachineComponent.h"

#include "Transport/TransportService.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace synth::ui {

namespace {

constexpr int kStepChoices[] = {8, 16, 32, 64};

// The row order: the default table, then the extras, then any other pitch by number.
int rankOfPitch(int pitch) {
    int rank = 0;
    for (const auto* table : {&defaultDrumLanes(), &extraDrumLanes()})
        for (const auto& lane : *table) {
            if (lane.pitch == pitch)
                return rank;
            ++rank;
        }
    return 1000 + pitch;
}

} // namespace

DrumMachineComponent::DrumMachineComponent()
    : viewSwitch_("View", juce::StringArray{"Drum machine", "Piano roll"}) {
    setComponentID("drumMachineComponent");
    setTitle("Drum machine grid");
    setTooltip("Drum grid: click a step to add a hit, click a hit to remove it, drag a hit up or down to change how "
               "loud it is, Shift-click for an accent");
    setWantsKeyboardFocus(true);
    setOpaque(true);

    addAndMakeVisible(labels_);
    labels_.setComponentID("drumRowLabels");
    labels_.onMuteToggled = [this](int pitch, bool on) { setMuted(pitch, on); };
    labels_.onSoloToggled = [this](int pitch, bool on) { setSoloed(pitch, on); };

    addAndMakeVisible(viewSwitch_);
    viewSwitch_.setComponentID("drumViewSwitch");
    viewSwitch_.setTooltip("Show this clip as a drum grid or as a piano roll");
    viewSwitch_.setSelectedIndex(0, juce::dontSendNotification);
    viewSwitch_.onChange = [this](int index) {
        if (index == 1 && clipId_.isValid() && onPianoRollRequested)
            onPianoRollRequested(clipId_);
    };

    addAndMakeVisible(stepsCombo_);
    stepsCombo_.setComponentID("drumStepsPerBar");
    stepsCombo_.setTitle("Steps per bar");
    stepsCombo_.setTooltip("Steps per bar: how many cells make up one bar. Existing hits keep their place in time.");
    for (int i = 0; i < 4; ++i)
        stepsCombo_.addItem(juce::String(kStepChoices[i]), i + 1);
    stepsCombo_.setSelectedId(2, juce::dontSendNotification);
    stepsCombo_.onChange = [this] {
        const int id = stepsCombo_.getSelectedId();
        if (id >= 1 && id <= 4)
            setStepsPerBar(kStepChoices[id - 1]);
    };

    addAndMakeVisible(showAllButton_);
    showAllButton_.setComponentID("drumShowAll");
    showAllButton_.setClickingTogglesState(true);
    showAllButton_.setTitle("Show all drums");
    showAllButton_.setTooltip("Show every General MIDI drum as a row, not only the common ones");
    showAllButton_.onClick = [this] { setShowAllDrums(showAllButton_.getToggleState()); };
}

DrumMachineComponent::~DrumMachineComponent() {
    wheelTween_.stop();
    motionAnim_.stop(vblank_);
    rowAnim_.stop(vblank_);
}

//==============================================================================
const synth::Clip* DrumMachineComponent::clip() const {
    return doc_ != nullptr && clipId_.isValid() ? doc_->getClip(clipId_) : nullptr;
}

double DrumMachineComponent::beatsPerBar() const { return beatsPerBarFor(transport_); }

int DrumMachineComponent::stepsPerBar() const {
    const auto* c = clip();
    return c != nullptr ? c->stepsPerBar : synth::Clip::kDefaultStepsPerBar;
}

double DrumMachineComponent::getStepBeats() const { return drumgrid::stepBeats(stepsPerBar(), beatsPerBar()); }

int DrumMachineComponent::getStepCount() const {
    const auto* c = clip();
    return c != nullptr ? drumgrid::stepCount(c->lengthBeats, getStepBeats()) : 0;
}

//==============================================================================
void DrumMachineComponent::openClip(synth::ClipId id) {
    wheelTween_.stop();
    gesture_ = {};
    hitMotions_.clear();
    flashes_.clear();
    motionAnim_.stop(vblank_);
    rowAnim_.stop(vblank_);
    hasPlayheadX_ = false;
    playStep_ = -1;
    clipId_ = {};

    const auto* c = doc_ != nullptr ? doc_->getClip(id) : nullptr;
    if (c == nullptr) {
        slots_.clear();
        rowTops_.clear();
        cells_.clear();
        pushRowsToLabels();
        repaint();
        return;
    }
    clipId_ = id;
    scrollY_ = 0.0;
    slots_.clear();
    syncLanes(false);
    rebuildCells(false); // the first build is the baseline: nothing grows in on open
    focus_ = {0, 0};
    viewSwitch_.setSelectedIndex(0, juce::dontSendNotification);
    stepsCombo_.setSelectedId(
        1 + static_cast<int>(std::find(std::begin(kStepChoices), std::end(kStepChoices), c->stepsPerBar) -
                             std::begin(kStepChoices)),
        juce::dontSendNotification);
    fitViewToClip();
    layoutRows();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    repaint();
}

void DrumMachineComponent::closeGrid() {
    wheelTween_.stop();
    endGesture(false);
    motionAnim_.stop(vblank_);
    rowAnim_.stop(vblank_);
    hitMotions_.clear();
    flashes_.clear();
    clipId_ = {};
    hasPlayheadX_ = false;
    playStep_ = -1;
    repaint();
}

void DrumMachineComponent::requestClose() {
    if (onAboutToClose)
        onAboutToClose();
    closeGrid();
    if (onCloseRequested)
        onCloseRequested();
}

// Frames the clip: its start at the label column's right edge, zoomed so the whole clip fits the grid width.
void DrumMachineComponent::fitViewToClip() {
    const auto* c = clip();
    if (c == nullptr)
        return;
    const double gridWidth = std::max(1.0, static_cast<double>(getWidth() - kLabelColumnWidth));
    const double fitted = c->lengthBeats > 0.0 ? gridWidth / c->lengthBeats : view_.pixelsPerBeat;
    setHorizontalView(fitted, c->startBeat);
}

void DrumMachineComponent::refreshFromDoc() {
    if (!clipId_.isValid())
        return;
    const auto* c = doc_ != nullptr ? doc_->getClip(clipId_) : nullptr;
    if (c == nullptr) {
        requestClose();
        return;
    }
    syncLanes(true);
    rebuildCells();
    const int combo = 1 + static_cast<int>(std::find(std::begin(kStepChoices), std::end(kStepChoices), c->stepsPerBar) -
                                           std::begin(kStepChoices));
    stepsCombo_.setSelectedId(combo, juce::dontSendNotification);
    const int steps = getStepCount();
    focus_.step = juce::jlimit(0, std::max(0, steps - 1), focus_.step);
    layoutRows();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    repaint();
}

//==============================================================================
// Merges the rows the clip needs into the rows on screen, in table order. A row that arrives grows in and one that
// goes shrinks out (startRowMotion); `animate` false, or no motion allowed, lands every row at once.
void DrumMachineComponent::syncLanes(bool animate) {
    const auto target = visibleDrumLanes(clip(), showAll_);
    const int focusPitch = slotIndexForRow(focus_.row) >= 0 ? slots_[(size_t)focus_.row].lane.pitch : -1;

    std::map<int, const LaneSlot*> old;
    for (const auto& slot : slots_)
        old[slot.lane.pitch] = &slot;
    std::map<int, const DrumLane*> wanted;
    for (const auto& lane : target)
        wanted[lane.pitch] = &lane;

    std::vector<int> pitches;
    for (const auto& [pitch, slot] : old)
        pitches.push_back(pitch);
    for (const auto& [pitch, lane] : wanted)
        if (old.count(pitch) == 0)
            pitches.push_back(pitch);
    std::sort(pitches.begin(), pitches.end(), [](int a, int b) { return rankOfPitch(a) < rankOfPitch(b); });

    const bool animating = animate && motionAllowed();
    std::vector<LaneSlot> merged;
    for (int pitch : pitches) {
        LaneSlot slot;
        if (old.count(pitch) > 0)
            slot = *old[pitch];
        else
            slot.lane = *wanted[pitch], slot.reveal = animating ? 0.0f : 1.0f;
        slot.from = slot.reveal;
        slot.to = wanted.count(pitch) > 0 ? 1.0f : 0.0f;
        if (!animating)
            slot.reveal = slot.to;
        merged.push_back(slot);
    }
    const bool changed = std::any_of(merged.begin(), merged.end(), [](const LaneSlot& s) { return s.reveal != s.to; });
    slots_ = std::move(merged);
    if (!animating)
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const LaneSlot& s) { return s.to <= 0.0f; }),
                     slots_.end());

    focus_.row = 0;
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].lane.pitch == focusPitch)
            focus_.row = static_cast<int>(i);
    if (changed)
        startRowMotion();
}

void DrumMachineComponent::layoutRows() {
    const double maxScroll = maxScrollY();
    scrollY_ = juce::jlimit(0.0, maxScroll, scrollY_);
    rowTops_.clear();
    int y = canvasTop() - static_cast<int>(std::lround(scrollY_));
    for (const auto& slot : slots_) {
        rowTops_.push_back(y);
        y += rowHeightOf(slot);
    }
    pushRowsToLabels();
}

void DrumMachineComponent::pushRowsToLabels() {
    labels_.setBounds(0, canvasTop(), kLabelColumnWidth, std::max(0, getHeight() - canvasTop()));
    std::vector<DrumRowLabels::Row> rows;
    const auto* c = clip();
    for (size_t i = 0; i < slots_.size(); ++i) {
        const auto& slot = slots_[i];
        DrumRowLabels::Row row;
        row.lane = slot.lane;
        row.top = (i < rowTops_.size() ? rowTops_[i] : canvasTop()) - canvasTop();
        row.height = rowHeightOf(slot);
        row.alpha = slot.reveal;
        if (c != nullptr) {
            row.muted = std::binary_search(c->mutedPitches.begin(), c->mutedPitches.end(), slot.lane.pitch);
            row.soloed = std::binary_search(c->soloPitches.begin(), c->soloPitches.end(), slot.lane.pitch);
            row.dimmed = rowSilenced(slot.lane.pitch);
        }
        rows.push_back(std::move(row));
    }
    labels_.setRows(std::move(rows));
}

void DrumMachineComponent::setShowAllDrums(bool showAll) {
    if (showAll_ == showAll)
        return;
    showAll_ = showAll;
    showAllButton_.setToggleState(showAll, juce::dontSendNotification);
    if (!clipId_.isValid())
        return;
    syncLanes(true);
    layoutRows();
    repaint();
}

int DrumMachineComponent::getLaneCount() const noexcept { return static_cast<int>(slots_.size()); }

int DrumMachineComponent::getLanePitch(int row) const noexcept {
    return slotIndexForRow(row) >= 0 ? slots_[(size_t)row].lane.pitch : -1;
}

int DrumMachineComponent::rowOfPitch(int pitch) const noexcept {
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].lane.pitch == pitch && slots_[i].to > 0.0f)
            return static_cast<int>(i);
    return -1;
}

void DrumMachineComponent::setFocusCell(Cell cell) {
    if (slots_.empty())
        return;
    focus_.row = juce::jlimit(0, static_cast<int>(slots_.size()) - 1, cell.row);
    focus_.step = juce::jlimit(0, std::max(0, getStepCount() - 1), cell.step);
    ensureCellVisible(focus_);
    announceFocus();
    repaint();
}

void DrumMachineComponent::visibilityChanged() {
    if (!isVisible()) {
        endGesture(false);
        wheelTween_.stop();
    }
}

} // namespace synth::ui
