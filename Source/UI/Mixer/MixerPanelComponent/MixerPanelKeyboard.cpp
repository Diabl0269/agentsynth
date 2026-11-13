// Concern: MixerPanelComponent's keyboard dispatch (Left/Right column walk, Down into the rows,
// Shift+Up/Down fader nudge, Enter select-on-canvas, the rebindable M/S/R actions) and the focus-outline
// painting/visual sync that goes with it. MixerPanelComponent.h's own class comment explains why
// the panel, not a per-column leaf, is the single focusable region root (the keyboard-focus trap
// docs/control/shortcuts.md documents: a focused Slider/TextButton eats the very keys this file resolves).
#include "MixerPanelComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/RenameKey.h"
#include "UI/Mixer/MixerColumnComponent.h"

namespace synth::ui {

bool MixerPanelComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                        const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), key);
}

bool MixerPanelComponent::keyPressed(const juce::KeyPress& key) {
    if (topBarRing_.handleKey(key))
        return true;
    // Before the arrows and the row keys: the default chord shares Up's key code, and it works from row mode too.
    if (matchesAction(key, "mixerFocusTopBar",
                      juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0))) {
        if (!topBarRing_.enter())
            return false;
        if (zonesPane_.hasKeyboardFocus(true)) // the ring's keys come to the panel root, so the panel takes the keys
            grabKeyboardFocus();
        exitRows();
        return true;
    }
    if (rowFocus_.has_value() && handleRowKey(key))
        return true;
    // A Cmd, Ctrl or Alt arrow is another action's chord (Focus Transport Bar is Cmd+Shift+Up), so it bubbles on.
    // Before the arrows: the default pan chords are Shift+Left / Shift+Right.
    if (matchesAction(key, "mixerPanLeft",
                      juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)))
        return panFocused(-kPanStep);
    if (matchesAction(key, "mixerPanRight",
                      juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)))
        return panFocused(kPanStep);
    if (matchesAction(key, "mixerToggleChannelHidden", juce::KeyPress('h', juce::ModifierKeys::noModifiers, 0)))
        return hideFocusedChannel();
    if (const float delta = faderNudgeFor(key); delta != 0.0f)
        return nudgeFocusedFader(delta);
    // Rebindable, Mixer category: into the focused column's insert, send and EQ rows (Down by default; with nothing to
    // enter it is left unhandled). See MixerPanelRowKeyboard.cpp.
    if (matchesAction(key, "mixerEnterRows",
                      juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::noModifiers, 0)))
        return enterRows();
    const auto mods = key.getModifiers();
    const bool arrowIsOurs = !mods.isCommandDown() && !mods.isCtrlDown() && !mods.isAltDown();
    if (arrowIsOurs && key.isKeyCode(juce::KeyPress::leftKey))
        return moveFocus(-1);
    if (arrowIsOurs && key.isKeyCode(juce::KeyPress::rightKey))
        return moveFocus(1);
    // A plain Up on a column has nothing above it (the top bar is Cmd+Up): swallowed so it never reaches the window.
    if (!mods.isAnyModifierKeyDown() && key.isKeyCode(juce::KeyPress::upKey))
        return focusedColumnIndex_ >= 0;
    if (key.isKeyCode(juce::KeyPress::returnKey))
        return selectFocusedOnCanvas();

    // The Rename key (F2 / Option+R) renames the focused column's name in place; a text editor keeps its own keys.
    if (isRenameKeyPress(shortcuts_, key) && !isTextEditorFocused())
        return renameFocusedColumn();
    // The track row's own Duplicate action id (Cmd+D by default), so one rebind covers both; it copies the
    // focused strip's track, and a column with no single track lets the key bubble.
    if (matchesAction(key, "timelineDuplicateFocusedTrack",
                      juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)))
        return duplicateFocusedTrack();

    // Rebindable, Timeline category -- the SAME action ids the timeline track-header row already
    // binds (TimelineTrackHeaderComponent::matchesAction), deliberately: a new id would not
    // inherit a user's existing rebind (plan (b)).
    if (matchesAction(key, "timelineMuteFocusedTrack", juce::KeyPress('m', juce::ModifierKeys::noModifiers, 0)))
        return toggleFocusedMuted();
    if (matchesAction(key, "timelineSoloFocusedTrack", juce::KeyPress('s', juce::ModifierKeys::noModifiers, 0)))
        return toggleFocusedSoloed();
    if (matchesAction(key, "timelineArmFocusedTrack", juce::KeyPress('r', juce::ModifierKeys::noModifiers, 0)))
        return armFocusedTrack();

    // Rebindable, Mixer category: the focused column's EQ window and colour picker.
    if (matchesAction(key, "mixerOpenEq", juce::KeyPress('e', juce::ModifierKeys::noModifiers, 0)))
        return openFocusedEq();
    if (matchesAction(key, "mixerOpenColour", juce::KeyPress('c', juce::ModifierKeys::noModifiers, 0)))
        return openFocusedColour();

    // Rebindable, Mixer category: the toolbar's Inserts / Sends / EQ toggles, through the same
    // MixerSectionLayout::toggleHidden a click calls, so a key and a click can never diverge.
    for (const auto section : {MixerSection::Inserts, MixerSection::Sends, MixerSection::Eq})
        if (matchesAction(key, sectionToggleActionId(section), defaultSectionToggleKey(section))) {
            sectionLayout_.toggleHidden(section);
            return true; // onSectionGeometryChanged() has already re-checked the row focus
        }

    return false;
}

bool MixerPanelComponent::showContextMenuForKeyboardFocus() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    // A focused insert row opens its own menu (a split's "Add before/after all paths", a module's "Add after"), the
    // way a right-click on the row does; no row focused, or any other row, keeps the channel menu.
    if (rowFocus_.has_value() && rowFocus_->kind == MixerRowKind::Insert)
        if (auto* inserts = focusedInsertList();
            inserts != nullptr && inserts->showMenuForKeyboardRow(rowFocus_->index))
            return true;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    showChannelMenu(entry.channelId, entry.component->getScreenBounds());
    return true;
}

juce::String MixerPanelComponent::sectionToggleActionId(MixerSection section) {
    switch (section) {
    case MixerSection::Inserts:
        return "mixerToggleInserts";
    case MixerSection::Sends:
        return "mixerToggleSends";
    case MixerSection::Eq:
        break;
    }
    return "mixerToggleEq";
}

// Only used without a ShortcutManager (tests): the macOS default, see ShortcutManager's table.
juce::KeyPress MixerPanelComponent::defaultSectionToggleKey(MixerSection section) {
    const juce::juce_wchar letter = section == MixerSection::Inserts ? 'i' : section == MixerSection::Sends ? 's' : 'e';
    return juce::KeyPress(letter, juce::ModifierKeys::ctrlModifier, 0);
}

juce::String MixerPanelComponent::sectionToggleShortcutText(MixerSection section) const {
    if (shortcuts_ == nullptr)
        return {};
    return ShortcutManager::keyPressToDisplayString(shortcuts_->getBinding(sectionToggleActionId(section)));
}

bool MixerPanelComponent::moveFocus(int direction) {
    const int count = (int)columnEntries_.size();
    if (count == 0)
        return false;
    // From nothing focused, either direction seeds column 0 (plan (a): "from -1, Right lands on
    // column 0" -- Left mirrors it rather than leaving the key unclaimed). Otherwise clamp at
    // either end, never wrap -- the same rule TimelinePanelComponent::moveFocusedTrack uses.
    // Left of the first strip goes on into the side pane's channel list when it shows; it is the only way in by arrows.
    if (direction < 0 && focusedColumnIndex_ == 0 && zonesPane_.isVisible() && !zonesPane_.getBounds().isEmpty()) {
        zonesPane_.focusList();
        noteKeyboardMoved(); // the list has the keys now: the strip's ring goes
        return true;
    }
    const int newIndex = focusedColumnIndex_ < 0 ? 0 : juce::jlimit(0, count - 1, focusedColumnIndex_ + direction);
    setFocusedColumnIndex(newIndex);
    return true;
}

// The fader nudge keys (Shift+Up / Shift+Down by default) move 1 dB; the same chord with Alt added moves 0.1 dB, so a
// rebind carries its fine step along. Zero for any other key.
float MixerPanelComponent::faderNudgeFor(const juce::KeyPress& key) const {
    const auto fallbackFor = [](int code) { return juce::KeyPress(code, juce::ModifierKeys::shiftModifier, 0); };
    const auto mods = key.getModifiers();
    const juce::KeyPress withoutAlt(key.getKeyCode(), mods.withoutFlags(juce::ModifierKeys::altModifier),
                                    key.getTextCharacter());
    for (const auto& [actionId, code, sign] :
         {std::tuple<const char*, int, float>{"mixerNudgeFaderUp", juce::KeyPress::upKey, 1.0f},
          std::tuple<const char*, int, float>{"mixerNudgeFaderDown", juce::KeyPress::downKey, -1.0f}}) {
        if (matchesAction(key, actionId, fallbackFor(code)))
            return sign;
        if (mods.isAltDown() && matchesAction(withoutAlt, actionId, fallbackFor(code)))
            return sign * 0.1f;
    }
    return 0.0f;
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

// The column holding keyboard focus: the one whose component contains the focused widget (a click lands focus on a
// colour dot, fader or button inside a column, which never moves focusedColumnIndex_), else the arrow-key column.
int MixerPanelComponent::columnIndexContaining(const juce::Component* focused) const {
    const int index = columnIndexOf(focused);
    return index >= 0 ? index : focusedColumnIndex_;
}

int MixerPanelComponent::columnIndexOf(const juce::Component* c) const {
    for (; c != nullptr && c != this; c = c->getParentComponent())
        for (size_t i = 0; i < columnEntries_.size(); ++i)
            if (columnEntries_[i].component == c)
                return (int)i;
    return -1;
}

// No scroll: the column is under the pointer already. A pressed control that takes the keyboard itself (the rename
// editor, the colour dot) keeps it. Anything else hands the keyboard to the panel, so the column's keys work at
// once: left alone, JUCE would give it to the column's first focusable child, the colour dot.
void MixerPanelComponent::focusColumnFromClick(const juce::MouseEvent& e) {
    const int index = columnIndexOf(e.eventComponent);
    if (index < 0)
        return;
    if (index != focusedColumnIndex_) {
        exitRows();
        focusedColumnIndex_ = index;
        grabAccessibilityFocusForFocusedColumn();
    }
    keyboardAway_ = false;
    syncFocusVisuals();
    repaint();
    if (e.eventComponent == nullptr || !e.eventComponent->getWantsKeyboardFocus())
        grabKeyboardFocus();
}

// The copy's column arrives through the rebuild; focus stays on this strip (resolveFocusAfterRebuild matches it by
// uuid), so pressing it again copies the same track again.
bool MixerPanelComponent::duplicateFocusedTrack() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size() || !onDuplicateTrack)
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind != ColumnEntry::Kind::Strip || entry.feedingTracks.size() != 1)
        return false;
    const auto track = entry.feedingTracks.front();
    onDuplicateTrack(track);
    return true;
}

void MixerPanelComponent::fillFocusedColumnState(juce::DynamicObject& into) const {
    const bool shown = !keyboardAway_ && juce::isPositiveAndBelow(focusedColumnIndex_, (int)columnEntries_.size());
    into.setProperty("focusedColumn",
                     shown ? columnEntries_[(size_t)focusedColumnIndex_].component->getTitle() : juce::String());
    // The row the keys are on (empty in column mode): its index and the text a screen reader reads for it.
    const bool inRows = shown && rowFocus_.has_value();
    into.setProperty("focusedRow", inRows ? getDescription() : juce::String());
    into.setProperty("focusedRowIndex", inRows ? rowFocus_->index : -1);
    // The side pane's list holds the keys (Left from the first strip): the channel its cursor is on.
    into.setProperty("zonesCursor", zonesPane_.getKeyboardCursorName());
}

// Direct and Master have no name to edit, so the key falls through there.
bool MixerPanelComponent::renameFocusedColumn() {
    const int index = columnIndexContaining(juce::Component::getCurrentlyFocusedComponent());
    if (index < 0 || index >= (int)columnEntries_.size())
        return false;
    const auto& entry = columnEntries_[(size_t)index];
    return entry.kind == ColumnEntry::Kind::Strip &&
           static_cast<MixerColumnComponent*>(entry.component)->beginHeaderRename();
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

// A strip's pan; Direct has no pan and Master's is its pan law, so the key bubbles there.
bool MixerPanelComponent::panFocused(float delta) {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    return entry.kind == ColumnEntry::Kind::Strip &&
           static_cast<MixerColumnComponent*>(entry.component)->nudgePan(delta);
}

// Hides the focused column (Master cannot be hidden) and hands the focus to its right-hand neighbour, or the left one
// at the end, so the keys keep working; the side pane's eye or its H shows the channel again.
bool MixerPanelComponent::hideFocusedChannel() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return false;
    const auto& entry = columnEntries_[(size_t)focusedColumnIndex_];
    if (entry.kind == ColumnEntry::Kind::Master || entry.channelId.isEmpty())
        return false;
    const int neighbour =
        focusedColumnIndex_ + 1 < (int)columnEntries_.size() ? focusedColumnIndex_ + 1 : focusedColumnIndex_ - 1;
    if (neighbour >= 0)
        focusFallback_ = FocusIdentity{columnEntries_[(size_t)neighbour].kind, columnEntries_[(size_t)neighbour].uuid};
    setChannelHidden(entry.channelId, true);
    return true;
}

void MixerPanelComponent::resolveFocusAfterRebuild(bool hadFocus, ColumnEntry::Kind previousKind,
                                                   const juce::String& previousUuid) {
    const auto fallback = std::exchange(focusFallback_, std::nullopt);
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
    // The previously-focused column is gone: focus clears, never silently reattaching to whatever now sits at the old
    // index, unless the panel itself named where it goes (the hide key's neighbour).
    focusedColumnIndex_ = -1;
    if (!fallback.has_value())
        return;
    for (size_t i = 0; i < columnEntries_.size(); ++i)
        if (columnEntries_[i].kind == fallback->kind &&
            (fallback->kind == ColumnEntry::Kind::Direct || columnEntries_[i].uuid == fallback->uuid)) {
            focusedColumnIndex_ = (int)i;
            return;
        }
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
        const bool focused = (int)i == focusedColumnIndex_ && !keyboardAway_;
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
    syncRowVisuals();
}

void MixerPanelComponent::revealFocusedColumn() {
    if (focusedColumnIndex_ < 0 || focusedColumnIndex_ >= (int)columnEntries_.size())
        return;
    // Same viewport math revealColumn() uses for the channel-chip reveal.
    scrollToColumn(columnEntries_[(size_t)focusedColumnIndex_]);
}

void MixerPanelComponent::paintOverChildren(juce::Graphics& g) {
    // The REGION root's own outline (real OS keyboard focus, the shared focus-outline language) --
    // distinct from each column's own keyboardFocused_ outline (syncFocusVisuals above), which
    // tracks focusedColumnIndex_ instead since real hasKeyboardFocus() is always false headless
    // with no native peer (same accepted gap TimelineTrackFocusTests documents). The two may
    // co-paint, same as a region root and a focused row elsewhere in this app.
    synth::ui::paintFocusRegionOutline(*this, g);
    // No event can leave a ring that lies: a paint that finds the keys elsewhere takes the ring down (and repaints).
    if (isKeyboardAwayNow() != keyboardAway_)
        noteKeyboardMoved();
    paintRowFocusRing(g);
    topBarRing_.paintRing(g);
    paintDragBubble(g);
}

// Reading order: the toolbar (side pane, Inserts, Sends, EQ, Reset Meters, Add Bus, dock back), then the zones pane's
// filter and chips (hidden with the pane), then the master column's pan law. The ring skips what is not showing.
std::vector<juce::Component*> MixerPanelComponent::topBarStops() {
    auto stops = toolbar_.getKeyStops();
    stops.push_back(&zonesPane_.getFilterField());
    for (auto chip : {MixerZonesPane::Chip::All, MixerZonesPane::Chip::Tracks, MixerZonesPane::Chip::Buses})
        stops.push_back(&zonesPane_.getChip(chip));
    if (masterColumn_ != nullptr)
        stops.push_back(&masterColumn_->getPanLawButton());
    return stops;
}

} // namespace synth::ui
