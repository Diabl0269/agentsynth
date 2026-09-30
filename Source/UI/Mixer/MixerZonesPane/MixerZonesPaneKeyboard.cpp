// Concern: MixerZonesPane's keyboard path -- a row cursor that Up/Down walk over the listed rows,
// Space to show or hide the cursor's channel, Alt+Up/Down to move it to the previous or next group,
// and Esc to hand focus back to the mixer. Every edit goes out through the same callbacks a click uses.
#include "MixerZonesPane.h"

namespace synth::ui {

namespace {
constexpr synth::MixerZone kZoneOrder[] = {synth::MixerZone::Left, synth::MixerZone::Scrolling,
                                           synth::MixerZone::Right};
} // namespace

void MixerZonesPane::focusList() {
    const auto ids = listedRowIds();
    if (std::find(ids.begin(), ids.end(), cursorId_) == ids.end())
        cursorId_ = ids.empty() ? juce::String() : ids.front();
    grabKeyboardFocus();
    setCursor(cursorId_, true);
}

// The cursor walks only rows the filter and chips leave listed, in list order (group by group).
std::vector<juce::String> MixerZonesPane::listedRowIds() const {
    std::vector<juce::String> ids;
    for (const auto& item : items_)
        if (!item.isHeader)
            ids.push_back(item.channelId);
    return ids;
}

bool MixerZonesPane::keyPressed(const juce::KeyPress& key) {
    const bool alt = key.getModifiers().isAltDown();
    if (key.isKeyCode(juce::KeyPress::upKey))
        return alt ? moveCursorZone(-1) : moveCursor(-1);
    if (key.isKeyCode(juce::KeyPress::downKey))
        return alt ? moveCursorZone(1) : moveCursor(1);
    if (key.isKeyCode(juce::KeyPress::spaceKey))
        return toggleCursorHidden();
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        giveAwayKeyboardFocus();
        if (onFocusReleased)
            onFocusReleased();
        return true;
    }
    return false;
}

// Clamped at either end, never wrapping, like the mixer's own column walk. From no cursor, either
// direction lands on the first listed row.
bool MixerZonesPane::moveCursor(int step) {
    const auto ids = listedRowIds();
    if (ids.empty())
        return false;
    const auto at = std::find(ids.begin(), ids.end(), cursorId_);
    const int index = at == ids.end() ? 0 : juce::jlimit(0, (int)ids.size() - 1, (int)(at - ids.begin()) + step);
    setCursor(ids[(size_t)index], true);
    return true;
}

// `announce` moves the screen reader's cursor too (keyboard navigation only, never a click, which
// already put it there).
void MixerZonesPane::setCursor(const juce::String& id, bool announce) {
    cursorId_ = id;
    syncCursorVisuals();
    const auto found = rows_.find(id);
    if (found == rows_.end())
        return;
    auto* row = found->second.get();
    if (row->getY() < viewport_.getViewPositionY())
        viewport_.setViewPosition(0, row->getY());
    else if (row->getBottom() > viewport_.getViewPositionY() + viewport_.getViewHeight())
        viewport_.setViewPosition(0, row->getBottom() - viewport_.getViewHeight());
    if (announce)
        if (auto* handler = row->getAccessibilityHandler())
            handler->grabFocus();
}

// Master's eye is disabled, and so is its Space.
bool MixerZonesPane::toggleCursorHidden() {
    const auto* channel = findChannel(cursorId_);
    if (channel == nullptr || channel->kind == MixerZoneChannelKind::Master || !onSetHidden)
        return channel != nullptr;
    onSetHidden(cursorId_, !channel->hidden);
    return true;
}

// The keyboard twin of dragging a row across one heading: Left zone <- Scrolling -> Right zone.
bool MixerZonesPane::moveCursorZone(int step) {
    const auto* channel = findChannel(cursorId_);
    if (channel == nullptr)
        return false;
    const int from = (int)(std::find(std::begin(kZoneOrder), std::end(kZoneOrder), channel->zone) - kZoneOrder);
    const int to = juce::jlimit(0, 2, from + step);
    if (to != from && onSetZone) {
        const auto id = cursorId_;
        onSetZone(id, kZoneOrder[to]);
        setCursor(id, true);
    }
    return true;
}

// The outline shows only while the list itself has focus, so a click elsewhere never leaves a stale
// cursor drawn.
void MixerZonesPane::syncCursorVisuals() {
    const bool focused = hasKeyboardFocus(false);
    for (auto& [id, row] : rows_)
        row->setKeyboardCursor(focused && id == cursorId_);
}

void MixerZonesPane::focusGained(FocusChangeType) { syncCursorVisuals(); }

void MixerZonesPane::focusLost(FocusChangeType) { syncCursorVisuals(); }

} // namespace synth::ui
