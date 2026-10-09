#include "TimelineTrackHeaderComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "TimelineTrackHeaderInternal.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TrackColour.h"
#include "UI/Timeline/TrackColourPicker.h"
#include "UI/Timeline/TrackRoutingMenus.h"

namespace synth::ui {

namespace {

// The hardcoded fallback for a bare m/s/r action when no ShortcutManager is installed — same
// idiom as TimelinePanelComponent's own plainKey() (duplicated rather than shared; see
// TimelinePanelComponent::matchesAction's own comment on why this three-line check is copied per
// surface rather than factored out).
juce::KeyPress plainKey(int character) { return juce::KeyPress(character, juce::ModifierKeys::noModifiers, 0); }

const juce::KeyPress kDuplicateKey('d', juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kShowModuleKey('e', juce::ModifierKeys::ctrlModifier, 0);
// Fallback when no ShortcutManager is installed; mirrors ShortcutManagerDefaults (a real Cmd+Ctrl+E on the Mac, where
// Cmd is distinct from Ctrl; Ctrl+Alt+E elsewhere, where Cmd IS Ctrl).
#if JUCE_MAC
const juce::KeyPress kTogglePluginWindowKey('e', juce::ModifierKeys::commandModifier | juce::ModifierKeys::ctrlModifier,
                                            0);
#else
const juce::KeyPress kTogglePluginWindowKey('e', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
#endif
const juce::KeyPress kIncreaseHeightKey('=', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kDecreaseHeightKey('-', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kResetHeightKey('0', juce::ModifierKeys::altModifier, 0);
const juce::KeyPress kDeleteTrackKey(juce::KeyPress::backspaceKey, juce::ModifierKeys::commandModifier, 0);
// Pixel distance a background mouseDrag must cross before it commits to a track-reorder
// drag rather than staying a plain click-to-select — small enough to feel immediate, large enough
// that an ordinary click's jitter never starts one.
constexpr float kRowDragThreshold = 4.0f;
} // namespace

//==============================================================================
TimelineTrackHeaderComponent::TimelineTrackHeaderComponent(synth::TimelineDoc& doc, synth::TrackId trackId,
                                                           TrackHeaderHost* host)
    : doc_(doc)
    , trackId_(trackId)
    , host_(host) {
    setComponentID("timelineTrackHeader");
    // Makes this row a real focus target (Up/Down between rows, M/S/R for the row that holds
    // focus) — same setWantsKeyboardFocus(true) pattern TimelineClipLaneArea/PianoRollComponent
    // already use for the surfaces they own.
    setWantsKeyboardFocus(true);

    addAndMakeVisible(colourSwatch_);
    colourSwatch_.setComponentID("trackColourSwatch");
    colourSwatch_.setTooltip("Click to change this track's colour");
    colourSwatch_.onClick = [this] {
        if (draggingRow_)
            return; // the release of a row drag, not a click
        auto popup = buildColourPicker();
        if (popup == nullptr)
            return; // the track is gone — nothing to pick a colour for
        juce::CallOutBox::launchAsynchronously(std::move(popup), colourSwatch_.getScreenBounds(), nullptr);
    };

    addAndMakeVisible(nameLabel_);
    nameLabel_.setComponentID("trackNameLabel");
    nameLabel_.setTooltip("Double-click to rename this track");
    // Double-click to rename — a single click must stay free for selecting the track row (the
    // mouseDown()/onSelectRequested; the label forwards its presses to this row).
    nameLabel_.setEditable(false, true, false);
    nameLabel_.onTextChange = [this] {
        const juce::String newName = nameLabel_.getText();
        // docs/mixer/mixer.md#channels-follow-audio-not-tracks (a): a LINKED track renames its channel too, as
        // one undo step. Unlinked (or no host) falls through to the plain track-only edit, unchanged.
        if (auto* link = linkSurface(); link != nullptr && link->renameLinkedTrackAndChannel(trackId_, newName))
            return;
        performEdit([this, newName] { doc_.setTrackName(trackId_, newName); });
    };

    auto setUpToggle = [this](juce::TextButton& button, const char* componentId, const std::function<void()>& onClick) {
        addAndMakeVisible(button);
        button.setComponentID(componentId);
        button.setClickingTogglesState(false); // the doc is the truth; refreshFromDoc sets the state
        button.onClick = onClick;
        // juce::Button opts INTO keyboard focus by default, and a click grabs it — without
        // this, clicking M/S/R would silently move real focus off the row and onto the button,
        // leaving the row's own focusGained/focusLost (and TimelinePanelComponent::focusedTrackIndex_,
        // which they keep in sync) stale. Same fix TimelinePanelComponent's own tool-strip buttons
        // already apply for the identical reason.
        button.setWantsKeyboardFocus(false);
        button.setMouseClickGrabsKeyboardFocus(false);
    };

    setUpToggle(muteButton_, "trackMuteButton", [this] { toggleMuted(); });
    muteButton_.setTooltip("Mute this track");
    setUpToggle(soloButton_, "trackSoloButton", [this] { toggleSoloed(); });
    soloButton_.setTooltip("Solo this track");
    // Arm flips document state only. Arming is not recording: the record button (and the
    // MidiRecorder::startRecording call behind it) lives on the transport bar.
    setUpToggle(armButton_, "trackArmButton", [this] { toggleArmed(); });
    armButton_.setTooltip("Arm this track for recording");

    // The automation fold arrow only reports the press: the panel owns the fold state and answers
    // with setAutomationExpanded(). Visibility is set in refreshFromDoc().
    addChildComponent(foldArrow_);
    foldArrow_.onClick = [this] {
        if (onAutomationToggleRequested)
            onAutomationToggleRequested(trackId_);
    };
    foldArrow_.onPopupMenuRequested = [this] { showContextMenu(); };
    initHeightHandle();
    // The chips and the arrow fade as the track gains or loses them; their widths follow the fade so the row slides.
    for (auto* fade : {&foldArrowFade_, &channelChipFade_})
        fade->onFrame = [this] { resized(); };

    // Ctrl+E's pointer and Tab twin. Its tooltip names the binding, so refreshFromDoc rebuilds it.
    addAndMakeVisible(showModuleButton_);
    showModuleButton_.onClick = [this] {
        if (host_ != nullptr)
            host_->showTrackModule(trackId_);
    };

    addAndMakeVisible(bindingChip_);
    bindingChip_.setComponentID("trackBindingChip");
    bindingChip_.onClick = [this] { handleChipClick(true); };

    // The channel chip. Visibility and text are doc/graph-driven (refreshFromDoc); the click
    // goes straight back to the app, which owns "where is that channel" (it reveals the
    // channel's mixer column).
    addAndMakeVisible(channelChip_);
    channelChip_.onClick = [this] {
        if (auto* link = linkSurface())
            link->revealChannelForTrack(trackId_);
    };

    openMidiDestinationsPickerHook_ = [this] { openMidiDestinationsPicker(); };

    refreshFromDoc();
}

//==============================================================================
int TimelineTrackHeaderComponent::trackIndex() const {
    const auto& tracks = doc_.getTracks();
    for (int i = 0; i < (int)tracks.size(); ++i)
        if (tracks[(size_t)i].id == trackId_)
            return i;
    return -1;
}

juce::String TimelineTrackHeaderComponent::getKindBadgeTextForTest() const {
    const auto* t = track();
    return t != nullptr ? detail::kindBadgeText(t->kind) : juce::String();
}

int TimelineTrackHeaderComponent::getKindBadgeIconForTest() const {
    const auto* t = track();
    if (t == nullptr)
        return -1;
    auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr || lf->peekIcon(detail::kindBadgeIcon(t->kind)) == nullptr)
        return -1; // no themed LnF, or the asset library isn't linked in — paint() falls back to text
    return (int)detail::kindBadgeIcon(t->kind);
}

void TimelineTrackHeaderComponent::performEdit(const std::function<void()>& mutation) {
    if (host_ != nullptr)
        host_->performTrackEdit(mutation);
    else
        mutation();
}

void TimelineTrackHeaderComponent::toggleArmed() {
    const auto* t = track();
    if (t == nullptr)
        return;
    const bool next = !t->armed;
    performEdit([this, next] { doc_.setTrackArmed(trackId_, next); });
}

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineTrackHeaderComponent::buildColourPicker() {
    juce::ApplicationProperties* props = host_ != nullptr ? host_->getAppProperties() : nullptr;
    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    synth::ui::TrackColourPickerContext context;
    context.doc = &doc_;
    context.favourites = props != nullptr ? props->getUserSettings() : nullptr;
    context.link = linkSurface();
    context.targets = bulkTargets();
    context.performEdit = [safeThis](const std::function<void()>& mutation) {
        if (auto* self = safeThis.getComponent())
            self->performEdit(mutation);
    };
    context.isAlive = [safeThis] { return safeThis != nullptr; };
    return synth::ui::buildTrackColourPicker(context, trackId_);
}

bool TimelineTrackHeaderComponent::tickChannelMeter() {
    if (!channelChip_.isVisible())
        return false;
    auto* link = linkSurface();
    if (link == nullptr)
        return false;
    // The level is read fresh (the strip's meter atomics move every block) through the seam's CHEAP
    // read -- never getChannelInfo(), which re-walks the graph and is a per-click cost, not a
    // per-frame one. The CHIP then decides whether the new level is worth a repaint at all.
    return channelChip_.setMeterLevel(link->getChannelMeterPeak(trackId_));
}

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineTrackHeaderComponent::createColourPickerForTest() {
    return buildColourPicker();
}

std::unique_ptr<synth::ui::MidiDestinationPicker> TimelineTrackHeaderComponent::buildMidiDestinationPicker() {
    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    return buildTrackMidiDestinationPicker(
        [safeThis]() -> TrackHeaderHost* { return safeThis != nullptr ? safeThis->host_ : nullptr; }, trackId_);
}

std::unique_ptr<synth::ui::MidiDestinationPicker> TimelineTrackHeaderComponent::createMidiDestinationPickerForTest() {
    return buildMidiDestinationPicker();
}

void TimelineTrackHeaderComponent::openMidiDestinationsPicker() {
    auto popup = buildMidiDestinationPicker();
    if (popup == nullptr)
        return; // no host — nothing to build a picker against
    juce::CallOutBox::launchAsynchronously(std::move(popup), bindingChip_.getScreenBounds(), nullptr);
}

//==============================================================================
void TimelineTrackHeaderComponent::refreshFromDoc() {
    ++refreshCount_;
    const auto* t = track();
    if (t == nullptr)
        return;

    nameLabel_.setText(t->name, juce::dontSendNotification);
    refreshHeightHandleText();
    // The row is the keyboard stop (Up/Down walk the rows), so a screen reader names it by its track.
    setTitle(t->name);
    setTooltip("Right-click, Shift+F10 or Return for the track menu; " +
               bindingText("timelineDuplicateFocusedTrack", kDuplicateKey) + " duplicates the track, " +
               bindingText("timelineDeleteFocusedTrack", kDeleteTrackKey) + " deletes it, " +
               bindingText("timelineShowFocusedTrackModule", kShowModuleKey) + " shows its module, " +
               bindingText("timelineToggleFocusedTrackPluginWindow", kTogglePluginWindowKey) +
               " opens or closes a plugin's window");
    showModuleButton_.setTrack(t->name, bindingText("timelineShowFocusedTrackModule", kShowModuleKey));

    // Re-derived from the live graph on every refresh rather than cached across edits --
    // a cable drag elsewhere can form or break this track's link with no doc change at all.
    channelInfo_ = {};
    if (auto* link = linkSurface())
        channelInfo_ = link->getChannelInfo(trackId_);

    // A LINKED track's mute/solo live on its CHANNEL (5.2 (c)), so that is what the buttons show --
    // and what dims the row's colour. An unlinked track reads its own doc flags, exactly as before.
    const bool showMuted = channelInfo_.linked ? channelInfo_.channelMuted : t->muted;
    const bool showSoloed = channelInfo_.linked ? channelInfo_.channelSoloed : t->soloed;

    resolvedColour_ = resolveTrackColour(t->colourArgb, trackIndex(), showMuted);
    colourSwatch_.colour = resolvedColour_;

    muteButton_.setToggleState(showMuted, juce::dontSendNotification);
    soloButton_.setToggleState(showSoloed, juce::dontSendNotification);
    armButton_.setToggleState(t->armed, juce::dontSendNotification);

    // 5.2: every track whose notes/audio play into a channel shows the chip -- linked or not.
    const bool showChannelChip = channelInfo_.hasChannel && t->kind != synth::TrackKind::Automation;
    if (channelChipFade_.isShown() != showChannelChip) {
        channelChipFade_.setShown(showChannelChip);
        resized(); // the chip shares the bottom row with the binding chip
    }
    if (showChannelChip) {
        channelChip_.setChannelName(channelInfo_.channelName);
        channelChip_.setTooltip(channelInfo_.linked
                                    ? "This track is the only source of the '" + channelInfo_.channelName +
                                          "' channel. Click to find it in the graph."
                                    : "This track plays into the '" + channelInfo_.channelName +
                                          "' channel, shared with other tracks. Click to find it.");
    }

    // Every track has the arrow (open, an empty track shows just "+ Add automation..."); the Unassigned
    // section only while it holds lanes.
    foldArrowFade_.setShown(!isSectionHeader() || !t->lanes.empty());
    foldArrow_.setState(automationExpanded_, isSectionHeader() ? juce::String("Unassigned") : t->name,
                        shownLaneCount());

    // The Automation track is the "Unassigned automation" section header: it hosts lanes no single
    // track owns, so a node binding, a colour, M/S/R and a rename all mean nothing for it.
    if (isSectionHeader()) {
        bindingChipFade_.setShown(false);
        for (juce::Component* hidden :
             {static_cast<juce::Component*>(&colourSwatch_), static_cast<juce::Component*>(&muteButton_),
              static_cast<juce::Component*>(&soloButton_), static_cast<juce::Component*>(&armButton_),
              static_cast<juce::Component*>(&showModuleButton_)})
            hidden->setVisible(false);
        nameLabel_.setText("Unassigned automation", juce::dontSendNotification);
        setTitle("Unassigned automation");
        setTooltip("Right-click, Shift+F10 or Return for the menu");
        nameLabel_.setEditable(false, false, false);
        nameLabel_.setTooltip("Automation lanes no single track plays");
    } else {
        bindingChipFade_.setShown(true);

        // Chip text/state/tooltip. Three cases, two of them amber. The tooltip is what carries the
        // "this shows a binding, it does not add a module" explanation the button text has no room for.
        if (t->bindingUuid.isEmpty()) {
            bindingChip_.setButtonText("Unbound");
            chipWarning_ = true;
            bindingChip_.setTooltip("This track has no bound node. Click to choose one.");
        } else if (t->orphaned) {
            bindingChip_.setButtonText("Missing");
            chipWarning_ = true;
            bindingChip_.setTooltip("The node this track was bound to is gone. Click to re-bind.");
        } else {
            const juce::String name = host_ != nullptr ? host_->getNodeDisplayName(t->bindingUuid) : juce::String();
            const juce::String displayName = name.isNotEmpty() ? name : juce::String("Track In");
            bindingChip_.setButtonText(displayName);
            chipWarning_ = false;
            bindingChip_.setTooltip("This track plays through the '" + displayName +
                                    "' node in the graph. Click to choose a different node.");
        }
    }

    applyThemeDerivedColours();
    resized(); // the fold arrow and the lane badge come and go with the lanes
    repaint();
}

bool TimelineTrackHeaderComponent::isSectionHeader() const {
    const auto* t = track();
    return t != nullptr && t->kind == synth::TrackKind::Automation;
}

int TimelineTrackHeaderComponent::shownLaneCount() const {
    const auto* t = track();
    return t != nullptr ? std::max(0, (int)t->lanes.size() - hiddenLaneCount_) : 0;
}

void TimelineTrackHeaderComponent::setHiddenLaneCount(int count) {
    if (hiddenLaneCount_ == count)
        return;
    hiddenLaneCount_ = count;
    refreshFromDoc();
}

void TimelineTrackHeaderComponent::setAutomationExpanded(bool expanded) {
    if (automationExpanded_ == expanded)
        return;
    automationExpanded_ = expanded;
    refreshFromDoc();
}

//==============================================================================
void TimelineTrackHeaderComponent::mouseDown(const juce::MouseEvent& e) {
    // This only ever fires for a right-click that lands on the row's OWN background — a right-click
    // on nameLabel_, the M/S/R toggles or the fold arrow reaches showContextMenu() straight from THEIR OWN
    // mouseDown() instead (ContextMenuForwardingLabel/ContextMenuForwardingButton in the header),
    // since JUCE dispatches a click to whichever component is directly under the cursor and
    // never routes it through here first.
    if (e.mods.isPopupMenu()) {
        showContextMenu();
        return;
    }
    draggingRow_ = false; // a fresh gesture; see mouseDrag's threshold check
    // Reports the press (screen Y) so the panel captures the grab point once; the animator's own
    // 4 px threshold decides whether it ever becomes a drag.
    if (onRowPressed)
        onRowPressed(e.getScreenPosition().y);
    // Click-to-select.
    // grabKeyboardFocus() is what makes a subsequent Up/Down or M/S/R keystroke route here in the
    // real app; onSelectRequested tells the panel directly (see its own comment for why that can't
    // wait on a real focusGained() round trip).
    grabKeyboardFocus();
    if (onSelectRequested)
        onSelectRequested(e.mods);
}

void TimelineTrackHeaderComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;

    if (!draggingRow_) {
        // Small threshold so an ordinary click's few pixels of jitter never starts a drag —
        // see onRowDragStarted's own comment for why this row (rather than the panel) owns the
        // threshold check: it's the one component that actually sees the raw gesture.
        if (e.getDistanceFromDragStart() < kRowDragThreshold)
            return;
        draggingRow_ = true;
        if (onRowDragStarted)
            onRowDragStarted(e.getScreenPosition().y);
    } else if (onRowDragged) {
        onRowDragged(e.getScreenPosition().y);
    }
    // The normal arrow on hover and press; the grab hand only once the press is a real drag.
    followDragCursor(*this, isRowDragging ? isRowDragging() : draggingRow_);
}

void TimelineTrackHeaderComponent::mouseUp(const juce::MouseEvent& e) {
    endDragCursor(*this);
    if (!draggingRow_) { // a plain click that never crossed the threshold — nothing to finish
        if (onRowClickReleased)
            onRowClickReleased();
        return;
    }

    // Every member write happens BEFORE onRowDragEnded — see that callback's own ordering-hazard
    // comment: it can (and normally does) destroy this component before this function returns.
    draggingRow_ = false;
    const int screenY = e.getScreenPosition().y;
    if (onRowDragEnded)
        onRowDragEnded(screenY); // may destroy `this` — nothing may follow this call
}

//==============================================================================
bool TimelineTrackHeaderComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                                 const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), key);
}

bool TimelineTrackHeaderComponent::keyPressed(const juce::KeyPress& key) {
    // Up/Down move focus to the previous/next row — this component owns neither the sibling list
    // nor the shared scroll state, so it just reports the direction (see onFocusMoveRequested's own
    // comment). Deliberately NOT a ShortcutManager action (arrow-key row navigation isn't rebindable
    // anywhere else in this app either — see ModuleLibraryComponent's row navigation).
    // Shift+Up/Down grows the selection a row at a time; Cmd/Ctrl+Space toggles this row in or out of it.
    const auto mods = key.getModifiers();
    const bool shiftOnly = mods.isShiftDown() && !mods.isCommandDown() && !mods.isAltDown() && !mods.isCtrlDown();
    if (shiftOnly && (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey))) {
        if (onExtendSelectionRequested)
            onExtendSelectionRequested(key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::spaceKey) && mods.isCommandDown() && !mods.isShiftDown()) {
        if (onToggleSelectionRequested)
            onToggleSelectionRequested();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::upKey)) {
        if (onFocusMoveRequested)
            onFocusMoveRequested(-1);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::downKey)) {
        if (onFocusMoveRequested)
            onFocusMoveRequested(1);
        return true;
    }

    // Return opens the row's menu, the same one a right-click and Shift+F10 open (rename is a double-click on the
    // name).
    if (key.isKeyCode(juce::KeyPress::returnKey) &&
        !key.getModifiers().testFlags(juce::ModifierKeys::allKeyboardModifiers))
        return showContextMenuForKeyboardFocus();

    // The "Next Clip" key carries focus from the row into its track's clips.
    if (onEnterClipsRequested && matchesAction(key, "timelineClipNext", plainKey(juce::KeyPress::rightKey)))
        return onEnterClipsRequested();

    // M/S/R toggle THIS row's track — rebindable, bare-letter defaults matching the J/L/P/F
    // convention. With no ShortcutManager installed (headless embeddings, or a test that never
    // calls setShortcutManager) these fall back to the hardcoded bare letters.
    if (matchesAction(key, "timelineMuteFocusedTrack", plainKey('m'))) {
        toggleMuted();
        return true;
    }
    if (matchesAction(key, "timelineSoloFocusedTrack", plainKey('s'))) {
        toggleSoloed();
        return true;
    }
    if (matchesAction(key, "timelineArmFocusedTrack", plainKey('r'))) {
        toggleArmed();
        return true;
    }
    // Cmd+D copies THIS row below itself. Claimed here, ahead of the app-wide Cmd+D (duplicate the
    // selection), because the focused row is what the user is pointing at; the Automation section
    // header has nothing to copy and lets the key bubble.
    if (!isSectionHeader() && matchesAction(key, "timelineDuplicateFocusedTrack", kDuplicateKey)) {
        if (host_ != nullptr)
            host_->duplicateTrack(trackId_);
        return true;
    }
    if (handleTrackClipboardKey(key))
        return true;
    // Ctrl+E shows THIS row's module (the same request as its button); a section header has no module of its own.
    if (!isSectionHeader() && matchesAction(key, "timelineShowFocusedTrackModule", kShowModuleKey)) {
        if (host_ != nullptr)
            host_->showTrackModule(trackId_);
        return true;
    }
    // Ctrl+Cmd+E opens or closes THIS row's plugin window; the host does nothing for a track without a hosted plugin.
    if (!isSectionHeader() && matchesAction(key, "timelineToggleFocusedTrackPluginWindow", kTogglePluginWindowKey)) {
        if (host_ != nullptr)
            host_->toggleTrackPluginWindow(trackId_);
        return true;
    }
    // A folds THIS row's lanes, like the arrow (a row without an arrow still claims the key, so it never
    // falls through to something else).
    if (onHeightStepRequested && !isSectionHeader()) {
        int step = 2; // none
        if (matchesAction(key, "timelineIncreaseTrackHeight", kIncreaseHeightKey))
            step = 1;
        else if (matchesAction(key, "timelineDecreaseTrackHeight", kDecreaseHeightKey))
            step = -1;
        else if (matchesAction(key, "timelineResetTrackHeight", kResetHeightKey))
            step = 0;
        if (step != 2) {
            onHeightStepRequested(trackId_, step);
            return true;
        }
    }
    if (matchesAction(key, "timelineToggleTrackAutomation", plainKey('a'))) {
        if (foldArrowFade_.isShown() && onAutomationToggleRequested)
            onAutomationToggleRequested(trackId_);
        return true;
    }
    // Cmd+Backspace deletes THIS row's track (after the host's question). Only a focused row sees the key, so
    // Backspace and Delete on clips and the canvas are untouched; the Automation section header has no track to delete.
    if (!isSectionHeader() && matchesAction(key, "timelineDeleteFocusedTrack", kDeleteTrackKey)) {
        if (host_ != nullptr)
            host_->deleteTrackAfterConfirm(trackId_);
        return true;
    }

    // Everything else (J/L/P/F, the tool digits, Escape...) is not this row's to claim — it bubbles
    // to TimelinePanelComponent::keyPressed exactly like it already does from the clip lane area and
    // the piano roll.
    return false;
}

void TimelineTrackHeaderComponent::setSelected(bool selected) {
    if (selected == selected_)
        return;
    selected_ = selected;
    repaint();
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

namespace {
// The row is one selectable item of the track list; the stock handler has no selected state to report.
class TrackRowAccessibilityHandler : public juce::AccessibilityHandler {
public:
    TrackRowAccessibilityHandler(TimelineTrackHeaderComponent& row)
        : juce::AccessibilityHandler(row, juce::AccessibilityRole::listItem)
        , row_(row) {}

    juce::AccessibleState getCurrentState() const override {
        auto state = juce::AccessibilityHandler::getCurrentState().withSelectable().withMultiSelectable();
        return row_.isSelected() ? state.withSelected() : state;
    }

private:
    TimelineTrackHeaderComponent& row_;
};
} // namespace

std::unique_ptr<juce::AccessibilityHandler> TimelineTrackHeaderComponent::createAccessibilityHandler() {
    return std::make_unique<TrackRowAccessibilityHandler>(*this);
}

void TimelineTrackHeaderComponent::focusGained(juce::Component::FocusChangeType) { repaint(); }
void TimelineTrackHeaderComponent::focusLost(juce::Component::FocusChangeType) { repaint(); }
void TimelineTrackHeaderComponent::focusOfChildComponentChanged(juce::Component::FocusChangeType) { repaint(); }

//==============================================================================
std::vector<TrackHeaderHost::BindingOption> TimelineTrackHeaderComponent::collectBindingOptions() const {
    if (host_ == nullptr)
        return {};
    return host_->getAvailableTrackInNodes(trackId_);
}

void TimelineTrackHeaderComponent::applyBindingMenuChoice(int menuId) {
    if (host_ == nullptr)
        return;

    if (menuId == kMidiDestinationsMenuId) {
        if (openMidiDestinationsPickerHook_)
            openMidiDestinationsPickerHook_();
        return;
    }

    applyTrackBindingChoice(*host_, trackId_, collectBindingOptions(), menuId);
}

bool TimelineTrackHeaderComponent::offersMidiDestinationsMenuEntryForTest() const {
    const auto* t = track();
    return t != nullptr && t->kind == synth::TrackKind::Midi;
}

void TimelineTrackHeaderComponent::handleChipClick(bool showMenu) {
    const auto* t = track();
    if (t != nullptr && t->bindingUuid.isNotEmpty() && !t->orphaned && host_ != nullptr)
        host_->selectNodeInGraph(t->bindingUuid); // highlight only — no scroll, no focus change

    if (showMenu)
        showBindingMenu();
}

void TimelineTrackHeaderComponent::showBindingMenu() {
    const auto options = collectBindingOptions();

    const auto* t = track();
    const juce::String currentUuid = t != nullptr ? t->bindingUuid : juce::String();
    auto menu = buildTrackBindingMenu(options, currentUuid, t != nullptr && t->kind == synth::TrackKind::Midi);

    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&bindingChip_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyBindingMenuChoice(result);
    });
}

void TimelineTrackHeaderComponent::applyContextMenuChoice(int menuId) {
    if (host_ == nullptr)
        return;
    if (menuId == kDeleteTrackMenuId && bulkTargets().size() > 1)
        host_->deleteTrackAfterConfirm(trackId_); // every selected track, after one question
    else if (menuId == kDeleteTrackMenuId)
        host_->deleteTrack(trackId_);
    else if (menuId == kDuplicateTrackMenuId)
        host_->duplicateTrack(trackId_);
    else if (menuId == kMakeChannelMenuId && host_->canMakeChannelForTrack(trackId_))
        host_->makeChannelForTrack(trackId_);
    else if (menuId == kSaveTrackPresetMenuId && host_->canSaveTrackPresetForTrack(trackId_))
        host_->saveTrackAsPreset(trackId_);
    else if (menuId == kSetTrackPresetDefaultMenuId && host_->canSaveTrackPresetForTrack(trackId_))
        host_->setTrackPresetAsDefault(trackId_);
    else if (menuId == kAddAutomationMenuId && onAddAutomationRequested)
        onAddAutomationRequested(trackId_);
    else if (menuId == kIncreaseHeightMenuId && onHeightStepRequested)
        onHeightStepRequested(trackId_, 1);
    else if (menuId == kDecreaseHeightMenuId && onHeightStepRequested)
        onHeightStepRequested(trackId_, -1);
    else if (menuId == kResetHeightMenuId && onHeightStepRequested)
        onHeightStepRequested(trackId_, 0);
}

juce::PopupMenu TimelineTrackHeaderComponent::buildContextMenu() const {
    juce::PopupMenu menu;
    // Disabled rather than hidden once the track has a channel, so the row sits in
    // a stable place (the canvas menu's "Locate Master" idiom).
    menu.addItem(kMakeChannelMenuId, "Make Channel", host_ != nullptr && host_->canMakeChannelForTrack(trackId_));
    // Same disabled-not-hidden precedent, gated on the track already having a
    // channel of its own (canSaveTrackPresetForTrack).
    const bool canSavePreset = host_ != nullptr && host_->canSaveTrackPresetForTrack(trackId_);
    menu.addItem(kSaveTrackPresetMenuId, "Save Track as Preset...", canSavePreset);
    menu.addItem(kSetTrackPresetDefaultMenuId, "Set as Default Track Preset", canSavePreset);
    menu.addSeparator();
    // Disabled when the track plays nothing automatable (or there is no host to ask).
    menu.addItem(kAddAutomationMenuId, "Add automation...",
                 host_ != nullptr && !host_->getAutomatableParameters(trackId_).empty());
    menu.addSeparator();
    if (!isSectionHeader()) {
        juce::PopupMenu::Item duplicate("Duplicate Track");
        duplicate.itemID = kDuplicateTrackMenuId;
        duplicate.shortcutKeyDescription = bindingText("timelineDuplicateFocusedTrack", kDuplicateKey);
        menu.addItem(std::move(duplicate));
        menu.addSeparator();
    }
    // The keyboard path to the row height, each item naming its rebindable shortcut.
    if (!isSectionHeader()) {
        const auto addHeightItem = [&](int id, const juce::String& text, const juce::String& action,
                                       const juce::KeyPress& fallback) {
            juce::PopupMenu::Item item(text);
            item.itemID = id;
            item.shortcutKeyDescription = bindingText(action, fallback);
            menu.addItem(std::move(item));
        };
        addHeightItem(kIncreaseHeightMenuId, "Increase Track Height", "timelineIncreaseTrackHeight",
                      kIncreaseHeightKey);
        addHeightItem(kDecreaseHeightMenuId, "Decrease Track Height", "timelineDecreaseTrackHeight",
                      kDecreaseHeightKey);
        addHeightItem(kResetHeightMenuId, "Reset Track Height", "timelineResetTrackHeight", kResetHeightKey);
        menu.addSeparator();
    }
    juce::PopupMenu::Item deleteItem("Delete Track");
    deleteItem.itemID = kDeleteTrackMenuId;
    if (!isSectionHeader())
        deleteItem.shortcutKeyDescription = bindingText("timelineDeleteFocusedTrack", kDeleteTrackKey);
    menu.addItem(std::move(deleteItem));
    return menu;
}

bool TimelineTrackHeaderComponent::showContextMenuForKeyboardFocus() {
    showContextMenu(getScreenBounds());
    return true;
}

void TimelineTrackHeaderComponent::showContextMenu(std::optional<juce::Rectangle<int>> keyboardAnchor) {
    auto menu = buildContextMenu();
    if (showContextMenuHook_) {
        showContextMenuHook_(menu);
        return;
    }

    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    menu.showMenuAsync(synth::ui::contextMenuOptions(keyboardAnchor), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyContextMenuChoice(result);
    });
}

// The strip along the row's bottom edge resizes this track only. Mouse-only (the row itself is the
// Tab stop); the keyboard path is the three height actions on the focused row and the menu items.
void TimelineTrackHeaderComponent::initHeightHandle() {
    heightHandle_.setComponentID("trackHeightHandle");
    heightHandle_.onDragStarted = [this] {
        if (onHeightDragStarted)
            onHeightDragStarted(trackId_);
    };
    heightHandle_.onDragged = [this](int delta) {
        if (onHeightDragged)
            onHeightDragged(trackId_, delta);
    };
    heightHandle_.onDragEnded = [this] {
        if (onHeightDragEnded)
            onHeightDragEnded(trackId_);
    };
    heightHandle_.onResetRequested = [this] {
        if (onHeightStepRequested)
            onHeightStepRequested(trackId_, 0);
    };
    addAndMakeVisible(heightHandle_);
    refreshHeightHandleText();
}

void TimelineTrackHeaderComponent::refreshHeightHandleText() {
    const auto* t = track();
    const juce::String name = t != nullptr ? t->name : juce::String("track");
    heightHandle_.setTitle("Resize " + name);
    heightHandle_.setTooltip("Drag to resize " + name + "; double-click for the default height. Keys on the track: " +
                             bindingText("timelineIncreaseTrackHeight", kIncreaseHeightKey) + " taller, " +
                             bindingText("timelineDecreaseTrackHeight", kDecreaseHeightKey) + " shorter, " +
                             bindingText("timelineResetTrackHeight", kResetHeightKey) + " default.");
}

juce::String TimelineTrackHeaderComponent::bindingText(const juce::String& actionId,
                                                       const juce::KeyPress& fallback) const {
    const auto key = shortcuts_ != nullptr ? shortcuts_->getBinding(actionId) : fallback;
    return key.isValid() ? key.getTextDescriptionWithIcons() : juce::String("unbound");
}

} // namespace synth::ui
