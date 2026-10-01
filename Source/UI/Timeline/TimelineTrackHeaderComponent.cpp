#include "TimelineTrackHeaderComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "TrackColour.h"
#include "TrackRoutingMenus.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

// The hardcoded fallback for a bare m/s/r action when no ShortcutManager is installed — same
// idiom as TimelinePanelComponent's own plainKey() (duplicated rather than shared; see
// TimelinePanelComponent::matchesAction's own comment on why this three-line check is copied per
// surface rather than factored out).
juce::KeyPress plainKey(int character) { return juce::KeyPress(character, juce::ModifierKeys::noModifiers, 0); }

constexpr int kSwatchWidth = 8;
// Widened from 20 (laid out edge-to-edge, no gap): the M/S/R toggles read as one fused
// block at that width, and this row was the worst offender in the timeline-panel button-size
// sweep. Paired with kToggleGap below rather than just grown, so the buttons are also visually
// separable now.
constexpr int kToggleWidth = 24;
// Inter-toggle gap, applied only BETWEEN adjacent M/S/R buttons (never before the first or after
// the last) — see TimelineTrackHeaderComponent::resized(). Growing kToggleWidth alone would have
// left them still touching.
constexpr int kToggleGap = 4;
constexpr int kRowPadding = 3;
// Pixel distance a background mouseDrag must cross before it commits to a track-reorder
// drag rather than staying a plain click-to-select — small enough to feel immediate, large enough
// that an ordinary click's jitter never starts one.
constexpr float kRowDragThreshold = 4.0f;
// Narrowed from 34 now that the badge draws a themed icon rather than "MIDI"/"AUD"/"AUTO" text —
// the icon needs far less width than the longest label did, and the freed space goes to the name.
constexpr int kKindBadgeWidth = 20;
constexpr float kKindBadgeIconSize = 14.0f;
constexpr int kLaneBadgeHeight = 14;

// Fixed per-TrackKind label. Never edited, never doc-driven beyond the kind itself. Kept as the
// fallback badge content for a headless build (no AppLookAndFeel) or one with no asset library —
// see getKindBadgeIcon()/paint().
juce::String kindBadgeText(synth::TrackKind kind) {
    switch (kind) {
    case synth::TrackKind::Midi:
        return "MIDI";
    case synth::TrackKind::Audio:
        return "AUD";
    case synth::TrackKind::Automation:
        return "Auto";
    }
    return {};
}

// Fixed per-TrackKind glyph. Automation has no dedicated colour-swatch analogue in the icon set
// beyond TrackAutomation itself, so the mapping is 1:1 with kindBadgeText's switch.
synth::theme::Icon kindBadgeIcon(synth::TrackKind kind) {
    switch (kind) {
    case synth::TrackKind::Midi:
        return synth::theme::Icon::TrackMidi;
    case synth::TrackKind::Audio:
        return synth::theme::Icon::TrackAudio;
    case synth::TrackKind::Automation:
        return synth::theme::Icon::TrackAutomation;
    }
    return synth::theme::Icon::TrackMidi;
}

// Themed colours with literal fallbacks — the headless test path installs no AppLookAndFeel (same
// pattern as TimelinePanelComponent::paint()).
struct HeaderColours {
    juce::Colour surface{juce::Colour(0xff1B1F26)};
    juce::Colour surfaceHi{juce::Colour(0xff232833)};
    juce::Colour border{juce::Colour(0xff2A2F38)};
    juce::Colour text{juce::Colour(0xffEAEEF3)};
    juce::Colour textMuted{juce::Colour(0xff8A93A0)};
    juce::Colour warning{juce::Colour(0xffE0A33D)};
    juce::Colour accent{juce::Colour(0xff00D1FF)};
    juce::Colour bg0{juce::Colour(0xff0B0D10)};
    juce::Colour muteOn{juce::Colour(0xffFFA033)};
    juce::Colour soloOn{juce::Colour(0xffFFD23D)};
    juce::Colour armOn{juce::Colour(0xffE5484D)};
};

HeaderColours coloursFor(const juce::Component& component) {
    HeaderColours result;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        result.surface = c.surface;
        result.surfaceHi = c.surfaceHi;
        result.border = c.border;
        result.text = c.textPrimary;
        result.textMuted = c.textMuted;
        result.warning = c.warning;
        result.accent = c.accent;
        result.bg0 = c.bg0;
        result.muteOn = c.trackMuteOn;
        result.soloOn = c.trackSoloOn;
        result.armOn = c.trackArmOn;
    }
    return result;
}

} // namespace

//==============================================================================
void TimelineTrackHeaderComponent::SwatchButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(highlighted ? colour.brighter(0.25f) : colour);
    g.fillRoundedRectangle(bounds, 2.0f);
}

//==============================================================================
TimelineTrackHeaderComponent::TimelineTrackHeaderComponent(synth::TimelineDoc& doc, synth::TrackId trackId,
                                                           TrackHeaderHost* host)
    : doc_(doc)
    , trackId_(trackId)
    , host_(host) {
    setComponentID("timelineTrackHeader");
    setMouseCursor(dragGrabCursor()); // the row background is a grab handle; child widgets keep their own cursor
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
    // with setAutomationExpanded(). Visibility (lanes or none) is set in refreshFromDoc().
    addChildComponent(foldArrow_);
    foldArrow_.onClick = [this] {
        if (onAutomationToggleRequested)
            onAutomationToggleRequested(trackId_);
    };
    foldArrow_.onPopupMenuRequested = [this] { showContextMenu(); };

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
    return t != nullptr ? kindBadgeText(t->kind) : juce::String();
}

int TimelineTrackHeaderComponent::getKindBadgeIconForTest() const {
    const auto* t = track();
    if (t == nullptr)
        return -1;
    auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr || lf->peekIcon(kindBadgeIcon(t->kind)) == nullptr)
        return -1; // no themed LnF, or the asset library isn't linked in — paint() falls back to text
    return (int)kindBadgeIcon(t->kind);
}

void TimelineTrackHeaderComponent::performEdit(const std::function<void()>& mutation) {
    if (host_ != nullptr)
        host_->performTrackEdit(mutation);
    else
        mutation();
}

void TimelineTrackHeaderComponent::toggleMuted() {
    const auto* t = track();
    if (t == nullptr)
        return;
    // docs/mixer/mixer.md#channels-follow-audio-not-tracks (c): a LINKED track's M IS the channel's mute -- one
    // mute, not two. The surface returns false for a shared channel (or no channel at all), and note gating below is
    // then exactly what it has always been. The refresh is explicit because a strip write is not a doc change: nothing
    // notifies the header otherwise.
    if (auto* link = linkSurface(); link != nullptr && link->toggleLinkedChannelMuted(trackId_)) {
        refreshFromDoc();
        return;
    }
    const bool next = !t->muted;
    performEdit([this, next] { doc_.setTrackMuted(trackId_, next); });
}

void TimelineTrackHeaderComponent::toggleSoloed() {
    const auto* t = track();
    if (t == nullptr)
        return;
    if (auto* link = linkSurface(); link != nullptr && link->toggleLinkedChannelSoloed(trackId_)) {
        refreshFromDoc(); // see toggleMuted
        return;
    }
    const bool next = !t->soloed;
    performEdit([this, next] { doc_.setTrackSoloed(trackId_, next); });
}

void TimelineTrackHeaderComponent::toggleArmed() {
    const auto* t = track();
    if (t == nullptr)
        return;
    const bool next = !t->armed;
    performEdit([this, next] { doc_.setTrackArmed(trackId_, next); });
}

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineTrackHeaderComponent::buildColourPicker() {
    const auto* t = track();
    if (t == nullptr)
        return nullptr;
    // The colour a no-net-change close restores, and what a "keep the final pick" undo step
    // restores TO (see the onCommit lambda below).
    const juce::uint32 originalColour = t->colourArgb;

    juce::ApplicationProperties* props = host_ != nullptr ? host_->getAppProperties() : nullptr;
    // docs/mixer/mixer.md#channels-follow-audio-not-tracks (b): a LINKED track's picker fans every preview
    // write out to the channel macro as well, and commits both as ONE undo step. Null for anything else -- the
    // single-target body below is then reached byte-for-byte as before.
    if (auto* link = linkSurface(); link != nullptr) {
        if (auto popup =
                link->buildLinkedChannelColourPicker(trackId_, props != nullptr ? props->getUserSettings() : nullptr))
            return popup;
    }
    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);

    return std::make_unique<synth::ui::ColourPickerPopup>(
        juce::Colour(originalColour), props != nullptr ? props->getUserSettings() : nullptr,
        [safeThis](juce::Colour c) {
            // Live preview: writes the doc directly, no undo — every drag/favourite click
            // repaints the row immediately, exactly like the old palette-cycle click did.
            if (auto* self = safeThis.getComponent())
                self->doc_.setTrackColour(self->trackId_, c.getARGB());
        },
        [safeThis, originalColour](juce::Colour finalColour) {
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return; // the header (or its window) is gone — nothing left to restore or undo
            if (finalColour.getARGB() == originalColour) {
                // No net change: put back exactly what was there (a preview may have nudged it)
                // and record no undo step — matching every other no-op edit in this file.
                self->doc_.setTrackColour(self->trackId_, originalColour);
                return;
            }
            // ONE undo step whose undo restores the ORIGINAL colour: silently put the original
            // back first (outside the undo-recorded mutation, so it does not itself become
            // undoable), then perform the real edit as the one recorded step.
            self->doc_.setTrackColour(self->trackId_, originalColour);
            self->performEdit(
                [self, finalColour] { self->doc_.setTrackColour(self->trackId_, finalColour.getARGB()); });
        });
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
    const auto* t = track();
    if (t == nullptr)
        return;

    nameLabel_.setText(t->name, juce::dontSendNotification);

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
    if (channelChip_.isVisible() != showChannelChip) {
        channelChip_.setVisible(showChannelChip);
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

    foldArrow_.setVisible(!t->lanes.empty());
    foldArrow_.setState(automationExpanded_, isSectionHeader() ? juce::String("Unassigned") : t->name,
                        (int)t->lanes.size());

    // The Automation track is the "Unassigned automation" section header: it hosts lanes no single
    // track owns, so a node binding, a colour, M/S/R and a rename all mean nothing for it.
    if (isSectionHeader()) {
        bindingChip_.setVisible(false);
        for (juce::Component* hidden :
             {static_cast<juce::Component*>(&colourSwatch_), static_cast<juce::Component*>(&muteButton_),
              static_cast<juce::Component*>(&soloButton_), static_cast<juce::Component*>(&armButton_)})
            hidden->setVisible(false);
        nameLabel_.setText("Unassigned automation", juce::dontSendNotification);
        nameLabel_.setEditable(false, false, false);
        nameLabel_.setTooltip("Automation lanes no single track plays");
    } else {
        bindingChip_.setVisible(true);

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

void TimelineTrackHeaderComponent::setAutomationExpanded(bool expanded) {
    if (automationExpanded_ == expanded)
        return;
    automationExpanded_ = expanded;
    refreshFromDoc();
}

//==============================================================================
// Every colour this component bakes via setColour rather than reading live in paint(): the
// binding chip's warning/normal treatment (moved here unchanged from refreshFromDoc(), which
// used to be the ONLY place that applied it — hence the theme-switch bug this fixes) plus the
// M/S/R buttons' active-state colours. `chipWarning_` and the chip's visibility are DOC state, so
// this only ever re-applies colours for whatever state refreshFromDoc() last computed; it never
// recomputes which state that is.
void TimelineTrackHeaderComponent::applyThemeDerivedColours() {
    const auto colours = coloursFor(*this);

    bindingChip_.setColour(juce::TextButton::buttonColourId, chipWarning_ ? colours.warning : colours.surface);
    bindingChip_.setColour(juce::TextButton::textColourOffId, chipWarning_ ? colours.surface : colours.text);

    // Active-state fill for each of M/S/R, with a dark contrasting label so the button text still
    // reads once the fill turns bright orange/yellow/red — the same buttonOnColourId/bg0 pairing
    // AppLookAndFeel's own defaults use for the accent-coloured "on" state everywhere else.
    muteButton_.setColour(juce::TextButton::buttonOnColourId, colours.muteOn);
    muteButton_.setColour(juce::TextButton::textColourOnId, colours.bg0);
    soloButton_.setColour(juce::TextButton::buttonOnColourId, colours.soloOn);
    soloButton_.setColour(juce::TextButton::textColourOnId, colours.bg0);
    armButton_.setColour(juce::TextButton::buttonOnColourId, colours.armOn);
    armButton_.setColour(juce::TextButton::textColourOnId, colours.bg0);
}

void TimelineTrackHeaderComponent::lookAndFeelChanged() { applyThemeDerivedColours(); }

//==============================================================================
void TimelineTrackHeaderComponent::resized() {
    if (isSectionHeader()) { // one short row: arrow, "Unassigned automation", lane badge
        auto row = getLocalBounds().reduced(kRowPadding, 0).withTrimmedLeft(kRowPadding);
        layoutFoldArrowAndBadges(row);
        nameLabel_.setBounds(row);
        return;
    }
    auto bounds = getLocalBounds().reduced(kRowPadding);

    colourSwatch_.setBounds(bounds.removeFromLeft(kSwatchWidth));
    bounds.removeFromLeft(kRowPadding);

    // Top row: (fold arrow) + kind badge + (folded lane badge) + name, then R/S/M right to left.
    // Bottom row: the binding chip, full width.
    auto topRow = bounds.removeFromTop(bounds.getHeight() / 2);
    armButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    topRow.removeFromRight(kToggleGap);
    soloButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    topRow.removeFromRight(kToggleGap);
    muteButton_.setBounds(topRow.removeFromRight(kToggleWidth));

    layoutFoldArrowAndBadges(topRow);
    nameLabel_.setBounds(topRow);

    // Bottom row: the binding chip, sharing with the channel chip when one is showing.
    auto bottomRow = bounds.reduced(0, 1);
    if (channelChip_.isVisible()) {
        channelChip_.setBounds(bottomRow.removeFromRight(bottomRow.getWidth() / 2));
        bottomRow.removeFromRight(kRowPadding);
    }
    bindingChip_.setBounds(bottomRow);
}

// The arrow leads the name row so it lines up with the lane rows indented beneath it. Only the
// section header draws a lane-count badge: a track row's header column is too narrow to fit one
// beside the name, so a folded track's arrow says the count in its name and tooltip instead.
void TimelineTrackHeaderComponent::layoutFoldArrowAndBadges(juce::Rectangle<int>& row) {
    const auto* t = track();
    const int laneCount = t != nullptr ? (int)t->lanes.size() : 0;
    if (foldArrow_.isVisible())
        foldArrow_.setBounds(row.removeFromLeft(TrackFoldArrow::kSize)
                                 .withSizeKeepingCentre(TrackFoldArrow::kSize, TrackFoldArrow::kSize));
    kindBadgeBounds_ = isSectionHeader() ? juce::Rectangle<int>() : row.removeFromLeft(kKindBadgeWidth);
    laneBadgeBounds_ = {};
    if (laneCount > 0 && isSectionHeader()) {
        const int width = laneCountBadgeWidth(laneCount);
        laneBadgeBounds_ = row.removeFromRight(width).withSizeKeepingCentre(width, kLaneBadgeHeight);
    }
}

void TimelineTrackHeaderComponent::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);

    if (isSectionHeader()) {
        g.fillAll(colours.surfaceHi);
        g.setColour(colours.border);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());
        if (const auto* t = track())
            paintLaneCountBadge(g, laneBadgeBounds_, (int)t->lanes.size(), *this);
        return;
    }

    g.fillAll(colours.surface);

    // Tinted left edge in the track's colour — the row's identity at a glance, even when the
    // swatch button itself is under the cursor.
    g.setColour(resolvedColour_.withMultipliedAlpha(0.35f));
    g.fillRect(0, 0, 3, getHeight());

    g.setColour(colours.border);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());

    // Track-kind badge: a themed glyph right of the swatch, before the name. Fixed per TrackKind
    // (see kindBadgeIcon above) — this is identity chrome, not a control. Falls back to the old
    // text pill when there's no themed LnF (headless) or the icon asset is absent, so a headless
    // build/test still gets a legible badge.
    if (const auto* t = track()) {
        auto badgeBounds = kindBadgeBounds_.reduced(1, 3).toFloat();
        g.setColour(colours.textMuted.withAlpha(0.15f));
        g.fillRoundedRectangle(badgeBounds, 3.0f);
        g.setColour(colours.textMuted.withAlpha(0.6f));
        g.drawRoundedRectangle(badgeBounds, 3.0f, 1.0f);

        const auto* icon = [this, t]() -> const juce::Drawable* {
            if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
                return lf->peekIcon(kindBadgeIcon(t->kind));
            return nullptr;
        }();

        if (icon != nullptr) {
            const auto iconArea =
                juce::Rectangle<float>(kKindBadgeIconSize, kKindBadgeIconSize).withCentre(badgeBounds.getCentre());
            icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred, 1.0f);
        } else {
            float microSize = 8.5f;
            if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
                microSize = lf->getTheme().type.micro;
            g.setColour(colours.textMuted);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
            g.drawText(kindBadgeText(t->kind), kindBadgeBounds_, juce::Justification::centred, false);
        }
        paintLaneCountBadge(g, laneBadgeBounds_, (int)t->lanes.size(), *this);
    }
}

// 0..1 strength of the lifted look (a light wash and an accent border) while the row is being
// reorder-dragged; a no-op when unchanged.
void TimelineTrackHeaderComponent::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

void TimelineTrackHeaderComponent::paintOverChildren(juce::Graphics& g) {
    if (lift_ > 0.0f) {
        const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const auto accent = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
        g.setColour(juce::Colours::white.withAlpha(0.05f * lift_));
        g.fillRect(getLocalBounds());
        g.setColour(accent.withMultipliedAlpha(lift_));
        g.drawRect(getLocalBounds(), 1);
    }
    // Reuses the region-root outline verbatim (same colour/alpha/thickness) rather than a
    // bespoke treatment — a track header row is now a real focusable leaf exactly the way a region
    // root is, just nested one level deeper (see docs/timeline/tracks.md#focus-outline).
    synth::ui::paintFocusRegionOutline(*this, g);
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
        onSelectRequested();
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
        return;
    }

    if (onRowDragged)
        onRowDragged(e.getScreenPosition().y);
}

void TimelineTrackHeaderComponent::mouseUp(const juce::MouseEvent& e) {
    if (!draggingRow_)
        return; // a plain click that never crossed the threshold — nothing to finish

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
    // A folds THIS row's lanes, like the arrow (a row without lanes still claims the key, so it never
    // falls through to something else).
    if (matchesAction(key, "timelineToggleTrackAutomation", plainKey('a'))) {
        if (foldArrow_.isVisible() && onAutomationToggleRequested)
            onAutomationToggleRequested(trackId_);
        return true;
    }

    // Everything else (J/L/P/F, the tool digits, Escape...) is not this row's to claim — it bubbles
    // to TimelinePanelComponent::keyPressed exactly like it already does from the clip lane area and
    // the piano roll.
    return false;
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
    if (menuId == kDeleteTrackMenuId)
        host_->deleteTrack(trackId_);
    else if (menuId == kMakeChannelMenuId && host_->canMakeChannelForTrack(trackId_))
        host_->makeChannelForTrack(trackId_);
    else if (menuId == kSaveTrackPresetMenuId && host_->canSaveTrackPresetForTrack(trackId_))
        host_->saveTrackAsPreset(trackId_);
    else if (menuId == kSetTrackPresetDefaultMenuId && host_->canSaveTrackPresetForTrack(trackId_))
        host_->setTrackPresetAsDefault(trackId_);
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
    menu.addItem(kDeleteTrackMenuId, "Delete Track");
    return menu;
}

bool TimelineTrackHeaderComponent::showContextMenuForKeyboardFocus() {
    showContextMenu();
    return true;
}

void TimelineTrackHeaderComponent::showContextMenu() {
    auto menu = buildContextMenu();
    if (showContextMenuHook_) {
        showContextMenuHook_(menu);
        return;
    }

    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyContextMenuChoice(result);
    });
}

} // namespace synth::ui
