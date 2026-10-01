// Concern: the modulator band's keyboard -- Left/Right walk the blocks, Delete removes the selected one,
// Return adds a bar at the playhead, Escape lets go. All standard navigation keys, none of them a
// rebindable action, and none of them taken when there is nothing for them to do (they then fall
// through to the panel).
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

#include "Transport/TransportService.h"
#include <algorithm>

namespace synth::ui {

bool ModulatorBand::keyPressed(const juce::KeyPress& key) {
    if (!info_.isLfo || doc_ == nullptr)
        return false;
    if (key == juce::KeyPress::escapeKey) {
        if (drag_ != Drag::None) {
            cancelDrag();
            return true;
        }
        if (!selectedStart_.has_value())
            return false;
        selectedStart_.reset();
        repaint();
        return true;
    }
    if (key == juce::KeyPress::leftKey)
        return moveSelection(-1);
    if (key == juce::KeyPress::rightKey)
        return moveSelection(1);
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return removeSelected();
    if (key == juce::KeyPress::returnKey)
        return addBarAtPlayhead();
    return false;
}

// With nothing selected Right takes the first block and Left the last; at either end the selection stays.
bool ModulatorBand::moveSelection(int direction) {
    const auto blocks = currentBlocks();
    if (blocks.empty())
        return false;
    const int current = selectedStart_.has_value() ? blockIndexStartingAt(blocks, *selectedStart_) : -1;
    const int next = current < 0 ? (direction > 0 ? 0 : (int)blocks.size() - 1)
                                 : juce::jlimit(0, (int)blocks.size() - 1, current + direction);
    selectedStart_ = blocks[(size_t)next].start;
    repaint();
    return true;
}

bool ModulatorBand::removeSelected() {
    if (!selectedStart_.has_value())
        return false;
    const auto blocks = currentBlocks();
    const int index = blockIndexStartingAt(blocks, *selectedStart_);
    if (index < 0)
        return false;
    commit(withoutBlock(blocks, index), std::nullopt);
    return true;
}

bool ModulatorBand::addBarAtPlayhead() {
    const double playhead = transport_ != nullptr ? transport_->getPositionSnapshot().ppq : 0.0;
    addBarAt(snappedBeat(playhead));
    return true;
}

} // namespace synth::ui
