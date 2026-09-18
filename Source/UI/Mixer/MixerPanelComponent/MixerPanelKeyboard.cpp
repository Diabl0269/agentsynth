// Concern: FRO18 -- MixerPanelComponent's keyboard dispatch (Left/Right column walk, Up/Down
// fader nudge, Enter select-on-canvas, the rebindable M/S/R actions) and the focus-outline
// painting/visual sync that goes with it. MixerPanelComponent.h's own class comment explains why
// the panel, not a per-column leaf, is the single focusable region root (the T160 trap
// docs/control/shortcuts.md documents: a focused Slider/TextButton eats the very keys this file resolves).
#include "MixerPanelComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

bool MixerPanelComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                        const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), key);
}

bool MixerPanelComponent::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::leftKey))
        return moveFocus(-1);
    if (key.isKeyCode(juce::KeyPress::rightKey))
        return moveFocus(1);
    if (key.isKeyCode(juce::KeyPress::upKey))
        return nudgeFocusedFader(key.getModifiers().isShiftDown() ? 0.1f : 1.0f);
    if (key.isKeyCode(juce::KeyPress::downKey))
        return nudgeFocusedFader(key.getModifiers().isShiftDown() ? -0.1f : -1.0f);
    if (key.isKeyCode(juce::KeyPress::returnKey))
        return selectFocusedOnCanvas();

    // Rebindable, Timeline category -- the SAME action ids the timeline track-header row already
    // binds (TimelineTrackHeaderComponent::matchesAction), deliberately: a new id would not
    // inherit a user's existing rebind (plan (b)).
    if (matchesAction(key, "timelineMuteFocusedTrack", juce::KeyPress('m', juce::ModifierKeys::noModifiers, 0)))
        return toggleFocusedMuted();
    if (matchesAction(key, "timelineSoloFocusedTrack", juce::KeyPress('s', juce::ModifierKeys::noModifiers, 0)))
        return toggleFocusedSoloed();
    if (matchesAction(key, "timelineArmFocusedTrack", juce::KeyPress('r', juce::ModifierKeys::noModifiers, 0)))
        return armFocusedTrack();

    return false;
}

bool MixerPanelComponent::moveFocus(int direction) {
    const int count = (int)columnEntries_.size();
    if (count == 0)
        return false;
    // From nothing focused, either direction seeds column 0 (plan (a): "from -1, Right lands on
    // column 0" -- Left mirrors it rather than leaving the key unclaimed). Otherwise clamp at
    // either end, never wrap -- the same rule TimelinePanelComponent::moveFocusedTrack uses.
    const int newIndex = focusedColumnIndex_ < 0 ? 0 : juce::jlimit(0, count - 1, focusedColumnIndex_ + direction);
    setFocusedColumnIndex(newIndex);
    return true;
}

bool MixerPanelComponent::nudgeFocusedFader(float deltaDb) {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind == ColumnEntry::Kind::Strip)
        return static_cast<MixerColumnComponent*>(entry.component)->nudgeFader(deltaDb);
    if (entry.kind == ColumnEntry::Kind::Master)
        return static_cast<MixerMasterColumn*>(entry.component)->nudgeFader(deltaDb);
    return false; // Direct has no fader of its own.
}

bool MixerPanelComponent::selectFocusedOnCanvas() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind == ColumnEntry::Kind::Direct || entry.uuid.isEmpty())
        return false; // Direct boxes no macro/node -- nothing to select.
    selectOnCanvas(entry.uuid);
    return true;
}

bool MixerPanelComponent::toggleFocusedMuted() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind == ColumnEntry::Kind::Strip) {
        static_cast<MixerColumnComponent*>(entry.component)->toggleMuted();
        return true;
    }
    if (entry.kind == ColumnEntry::Kind::Master) {
        static_cast<MixerMasterColumn*>(entry.component)->toggleMuted();
        return true;
    }
    return false; // Direct has no mute of its own.
}

bool MixerPanelComponent::toggleFocusedSoloed() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind != ColumnEntry::Kind::Strip)
        return false; // Master/Direct have no solo button (docs/mixer/panel.md#what-the-mixer-shows).
    static_cast<MixerColumnComponent*>(entry.component)->toggleSoloed();
    return true;
}

bool MixerPanelComponent::armFocusedTrack() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind != ColumnEntry::Kind::Strip || !entry.linkedToTrack || entry.feedingTracks.empty())
        return false;
    if (!onArmTrack)
        return false;
    onArmTrack(entry.feedingTracks.front());
    return true;
}

void MixerPanelComponent::resolveFocusAfterRebuild(bool hadFocus, ColumnEntry::Kind previousKind,
                                                   const juce::String& previousUuid) {
    if (!hadFocus)
        return;
    for (size_t i = 0; i < columnEntries_.size(); ++i) {
        const auto& entry = columnEntries_[i];
        if (entry.kind != previousKind)
            continue;
        // Direct carries no uuid (there is only ever one) -- kind alone is identity for it;
        // Strip/Master match by their stable node uuid.
        if (previousKind == ColumnEntry::Kind::Direct || entry.uuid == previousUuid) {
            focusedColumnIndex_ = (int)i;
            return;
        }
    }
    focusedColumnIndex_ = -1; // the previously-focused column is gone -- focus clears, never
                              // silently reattaches to whatever now sits at the old index.
}

void MixerPanelComponent::setFocusedColumnIndex(int index) {
    if (focusedColumnIndex_ == index)
        return;
    focusedColumnIndex_ = index;
    syncFocusVisuals();
    revealFocusedColumn();
    grabAccessibilityFocusForFocusedColumn();
    repaint(); // globalFocusChanged never fires for this -- it isn't real OS focus movement.
}

juce::Component* MixerPanelComponent::getAccessibilityFocusTargetForTest() const {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return nullptr;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    switch (entry.kind) {
    case ColumnEntry::Kind::Strip:
        return &static_cast<MixerColumnComponent*>(entry.component)->getAccessibilityFocusTargetForTest();
    case ColumnEntry::Kind::Master:
        return &static_cast<MixerMasterColumn*>(entry.component)->getAccessibilityFocusTargetForTest();
    case ColumnEntry::Kind::Direct:
        return &static_cast<MixerDirectColumn*>(entry.component)->getAccessibilityFocusTargetForTest();
    }
    return nullptr;
}

void MixerPanelComponent::grabAccessibilityFocusForFocusedColumn() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return;
    auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    switch (entry.kind) {
    case ColumnEntry::Kind::Strip:
        static_cast<MixerColumnComponent*>(entry.component)->grabAccessibilityFocus();
        return;
    case ColumnEntry::Kind::Master:
        static_cast<MixerMasterColumn*>(entry.component)->grabAccessibilityFocus();
        return;
    case ColumnEntry::Kind::Direct:
        static_cast<MixerDirectColumn*>(entry.component)->grabAccessibilityFocus();
        return;
    }
}

void MixerPanelComponent::syncFocusVisuals() {
    for (size_t i = 0; i < columnEntries_.size(); ++i) {
        const bool focused = (int)i == focusedColumnIndex_;
        auto& entry = columnEntries_[i];
        if (entry.component == nullptr)
            continue;
        switch (entry.kind) {
        case ColumnEntry::Kind::Strip:
            static_cast<MixerColumnComponent*>(entry.component)->setKeyboardFocused(focused);
            break;
        case ColumnEntry::Kind::Direct:
            static_cast<MixerDirectColumn*>(entry.component)->setKeyboardFocused(focused);
            break;
        case ColumnEntry::Kind::Master:
            static_cast<MixerMasterColumn*>(entry.component)->setKeyboardFocused(focused);
            break;
        }
    }
}

void MixerPanelComponent::revealFocusedColumn() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return;
    // Same viewport math revealColumn() uses for the channel-chip reveal.
    if (auto* comp = columnEntries_[(size_t)focusedColumnIndex_].component)
        viewport_.setViewPosition(comp->getX(), 0);
}

void MixerPanelComponent::paintOverChildren(juce::Graphics& g) {
    // The REGION root's own outline (real OS keyboard focus, T159's shared visual language) --
    // distinct from each column's own keyboardFocused_ outline (syncFocusVisuals above), which
    // tracks focusedColumnIndex_ instead since real hasKeyboardFocus() is always false headless
    // with no native peer (same accepted gap TimelineTrackFocusTests documents). The two may
    // co-paint, same as a region root and a focused row elsewhere in this app.
    synth::ui::paintFocusRegionOutline(*this, g);
}

} // namespace synth::ui
