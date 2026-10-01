// Concern: MixerSendList's keyboard row focus -- scrolling a focused row into view, its screen-reader
// text, the level nudge behind Left and Right, and the tooltip for each part of a row.
#include "AppUndoManager.h"
#include "MixerDbAccessibilityText.h"
#include "MixerSendList.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Mixer/MixerSections/MixerSectionViewport.h"

namespace synth::ui {

namespace {
ChannelStripModule* stripOf(juce::AudioProcessorGraph* graph, juce::AudioProcessorGraph::NodeID id) {
    if (graph == nullptr)
        return nullptr;
    auto* node = graph->getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}
} // namespace

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
