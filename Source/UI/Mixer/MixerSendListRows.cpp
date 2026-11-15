// Concern: MixerSendList's keyboard row focus -- scrolling a focused row into view, its screen-reader
// text, the level nudge behind Left and Right, and the tooltip for each part of a row.
#include "AppUndoManager.h"
#include "MixerDbAccessibilityText.h"
#include "MixerRowLookGlide.h"
#include "MixerRowsMotion.h"
#include "MixerSendList.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerSections/MixerSectionViewport.h"
#include <algorithm>

namespace synth::ui {

namespace {
ChannelStripModule* stripOf(juce::AudioProcessorGraph* graph, juce::AudioProcessorGraph::NodeID id) {
    if (graph == nullptr)
        return nullptr;
    auto* node = graph->getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}
} // namespace

std::vector<juce::String> MixerSendList::entryKeys() const {
    std::vector<juce::String> keys;
    for (const auto& entry : entries_)
        keys.push_back(MixerRowsMotion::sendKey(entry));
    return keys;
}

std::vector<ExitEnterListRow> MixerSendList::motionRows() const {
    std::vector<ExitEnterListRow> rows;
    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto bounds = getRowBounds(i);
        rows.push_back(
            {MixerRowsMotion::sendKey(entries_[(size_t)i]), (float)bounds.getY(), (float)bounds.getHeight()});
    }
    return rows;
}

MixerSendList::MixerSendList() {
    initLookGlide();
    setUpChildren();
}

MixerSendList::~MixerSendList() = default;

void MixerSendList::initLookGlide() {
    lookGlide_ = std::make_unique<MixerRowLookGlide>(*this, [this] { placeRows(); });
}
void MixerSendList::finishLookGlide() { lookGlide_->finishNow(); }
float MixerSendList::rowGlideYOffset(int row) const { return lookGlide_->yOffsetFor(row); }
float MixerSendList::rowGlideAlpha(int row) const { return lookGlide_->alphaFor(row); }
float MixerSendList::rowGlideDim(int row, float restingDim) const { return lookGlide_->dimFor(row, restingDim); }
float MixerSendList::rowGlideOldNameWeight(int row) const { return lookGlide_->oldNameWeightFor(row); }
juce::String MixerSendList::rowGlideOldName(int row) const { return lookGlide_->oldNameFor(row); }

std::vector<MixerRowLook> MixerSendList::drawnRowLooks() const { return lookGlide_->drawn(rowLooks()); }

void MixerSendList::toggleBypassForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    const int slot = entry.slot;
    const bool newBypassed = !entry.bypassed;
    const auto looksBefore = drawnRowLooks();
    mutateAndNotify([&] {
        if (graph_ == nullptr || !synth::setSendBypassed(*graph_, stripNodeId_, slot, newBypassed))
            return false;
        // The row reads right even where nothing rebuilds the list afterwards (a bare list in a test).
        entries_[(size_t)rowIndex].bypassed = newBypassed;
        startLookGlide(looksBefore, true); // the dim eases to its new look, it does not pop
        applyBypassLook(rowIndex);
        return true;
    });
}

bool MixerSendList::startLookGlide(const std::vector<MixerRowLook>& before, bool dimOnly) {
    // A send's key is its slot, and a move swaps what the slots hold. A row whose name now sits in another old slot
    // is that send, moved: it leaves from there. A name nobody had before is a retarget, which stays in its slot.
    const auto now = rowLooks();
    std::vector<MixerRowLook> from;
    for (const auto& look : now) {
        const auto same = std::find_if(before.begin(), before.end(), [&](const auto& r) { return r.key == look.key; });
        auto moved = std::find_if(before.begin(), before.end(), [&](const auto& r) { return r.name == look.name; });
        if (same != before.end() && same->name == look.name)
            moved = same;
        if (moved != before.end() && (same == before.end() || same->name != look.name)) {
            auto leaving = *moved;
            leaving.key = look.key;
            from.push_back(leaving);
        } else if (same != before.end()) {
            from.push_back(*same);
        }
    }
    const bool started = lookGlide_->start(from, now, dimOnly);
    placeRows();
    return started;
}

std::vector<MixerRowLook> MixerSendList::rowLooks() const {
    std::vector<MixerRowLook> looks;
    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto& entry = entries_[(size_t)i];
        looks.push_back(
            {MixerRowsMotion::sendKey(entry), (float)(i * kRowHeight), entry.bypassed ? 1.0f : 0.0f, entry.targetName});
    }
    return looks;
}

juce::Rectangle<int> MixerSendList::getRowBounds(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return {};
    return getLocalBounds().withY(rowIndex * kRowHeight).withHeight(kRowHeight);
}

void MixerSendList::setFocusedRow(int rowIndex) {
    focusedRow_ = rowIndex >= 0 && rowIndex < (int)entries_.size() ? rowIndex : -1;
    if (focusedRow_ >= 0)
        if (auto* viewport = findParentComponentOfClass<MixerSectionViewport>()) {
            const auto row = getRowBounds(focusedRow_);
            viewport->revealRange(row.getY(), row.getBottom());
        }
}

juce::String MixerSendList::describeRow(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return {};
    const auto& entry = entries_[(size_t)rowIndex];
    const bool hasTarget = entry.targetNodeId != juce::AudioProcessorGraph::NodeID{};
    auto text = hasTarget ? "Send to " + entry.targetName : "Send " + juce::String(entry.slot + 1) + " (no target)";
    if (const auto* strip = stripOf(graph_, stripNodeId_))
        if (const auto* level = strip->getSendLevelParameter(entry.slot))
            text << ", " << juce::String(level->get(), 1) << " dB";
    if (entry.muted)
        text << ", muted";
    if (entry.bypassed)
        text << ", bypassed";
    return text;
}

// One undo step per call: the before-state is captured around the parameter's own change gesture, the
// bracket SliderParameterAttachment produces for a knob drag. A move that clamps to the current value
// changes nothing and records nothing.
bool MixerSendList::nudgeLevel(int rowIndex, float deltaDb) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return false;
    auto* strip = stripOf(graph_, stripNodeId_);
    auto* level = strip != nullptr ? strip->getSendLevelParameter(entries_[(size_t)rowIndex].slot) : nullptr;
    if (level == nullptr)
        return false;
    const auto range = level->getNormalisableRange();
    const float target = juce::jlimit(range.start, range.end, level->get() + deltaDb);
    if (target == level->get())
        return true;
    if (undoManager_ != nullptr)
        undoManager_->captureBeforeState(*graph_);
    level->beginChangeGesture();
    level->setValueNotifyingHost(range.convertTo0to1(target));
    level->endChangeGesture();
    if (undoManager_ != nullptr)
        undoManager_->pushSnapshotFromCapture(*graph_);
    return true;
}

juce::String MixerSendList::getTooltip() {
    const auto position = getMouseXYRelative();
    const int row = rowIndexAt(position);
    if (row < 0)
        return canAddSend() && position.y >= (int)entries_.size() * kRowHeight ? "Add a send to a bus" : juce::String();
    const int fromRight = getWidth() - position.x;
    if (fromRight <= kRemoveWidth)
        return "Remove this send";
    if (fromRight <= kRemoveWidth + kToggleWidth)
        return "Switch this send between pre-fader and post-fader";
    return "Send to " + entries_[(size_t)row].targetName + ": click to change the target, drag to reorder";
}

} // namespace synth::ui
