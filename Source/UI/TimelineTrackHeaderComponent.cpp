#include "TimelineTrackHeaderComponent.h"
#include "ColourPickerPopup.h"
#include "Theme/AppLookAndFeel.h"
#include "TrackColour.h"

namespace synth::ui {

namespace {

constexpr int kSwatchWidth = 8;
constexpr int kToggleWidth = 20;
constexpr int kRowPadding = 3;
// Narrowed from 34 now that the badge draws a themed icon rather than "MIDI"/"AUD"/"AUTO" text —
// the icon needs far less width than the longest label did, and the freed space goes to the name.
constexpr int kKindBadgeWidth = 20;
constexpr float kKindBadgeIconSize = 14.0f;

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
        return "AUTO";
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

    addAndMakeVisible(colourSwatch_);
    colourSwatch_.setComponentID("trackColourSwatch");
    colourSwatch_.setTooltip("Click to change this track's colour");
    colourSwatch_.onClick = [this] {
        auto popup = buildColourPicker();
        if (popup == nullptr)
            return; // the track is gone — nothing to pick a colour for
        juce::CallOutBox::launchAsynchronously(std::move(popup), colourSwatch_.getScreenBounds(), nullptr);
    };

    addAndMakeVisible(nameLabel_);
    nameLabel_.setComponentID("trackNameLabel");
    nameLabel_.setTooltip("Double-click to rename this track");
    // Double-click to rename — a single click must stay free for selecting the track row later.
    nameLabel_.setEditable(false, true, false);
    nameLabel_.onTextChange = [this] {
        const juce::String newName = nameLabel_.getText();
        performEdit([this, newName] { doc_.setTrackName(trackId_, newName); });
    };

    auto setUpToggle = [this](juce::TextButton& button, const char* componentId, const std::function<void()>& onClick) {
        addAndMakeVisible(button);
        button.setComponentID(componentId);
        button.setClickingTogglesState(false); // the doc is the truth; refreshFromDoc sets the state
        button.onClick = onClick;
    };

    setUpToggle(muteButton_, "trackMuteButton", [this] {
        const auto* t = track();
        if (t == nullptr)
            return;
        const bool next = !t->muted;
        performEdit([this, next] { doc_.setTrackMuted(trackId_, next); });
    });
    muteButton_.setTooltip("Mute this track");
    setUpToggle(soloButton_, "trackSoloButton", [this] {
        const auto* t = track();
        if (t == nullptr)
            return;
        const bool next = !t->soloed;
        performEdit([this, next] { doc_.setTrackSoloed(trackId_, next); });
    });
    soloButton_.setTooltip("Solo this track");
    // Arm flips document state only. Arming is not recording: the record button (and the
    // MidiRecorder::startRecording call behind it) lives on the transport bar.
    setUpToggle(armButton_, "trackArmButton", [this] {
        const auto* t = track();
        if (t == nullptr)
            return;
        const bool next = !t->armed;
        performEdit([this, next] { doc_.setTrackArmed(trackId_, next); });
    });
    armButton_.setTooltip("Arm this track for recording");

    // Automation open/close: a plain click button, not a toggle — the header never knows whether the
    // strip is open or which track it's showing, only the panel does. Visibility (lanes or none) is
    // set in refreshFromDoc(), including the call at the end of this constructor.
    addAndMakeVisible(automationButton_);
    automationButton_.setComponentID("trackHeaderAutomationButton");
    automationButton_.setClickingTogglesState(false);
    automationButton_.setTooltip("Show/hide this track's automation lane");
    automationButton_.onClick = [this] {
        if (onAutomationToggleRequested)
            onAutomationToggleRequested(trackId_);
    };

    addAndMakeVisible(bindingChip_);
    bindingChip_.setComponentID("trackBindingChip");
    bindingChip_.onClick = [this] { handleChipClick(true); };

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

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineTrackHeaderComponent::buildColourPicker() {
    const auto* t = track();
    if (t == nullptr)
        return nullptr;
    // The colour a no-net-change close restores, and what a "keep the final pick" undo step
    // restores TO (see the onCommit lambda below).
    const juce::uint32 originalColour = t->colourArgb;

    juce::ApplicationProperties* props = host_ != nullptr ? host_->getAppProperties() : nullptr;
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

std::unique_ptr<synth::ui::ColourPickerPopup> TimelineTrackHeaderComponent::createColourPickerForTest() {
    return buildColourPicker();
}

std::unique_ptr<synth::ui::MidiDestinationPicker> TimelineTrackHeaderComponent::buildMidiDestinationPicker() {
    if (host_ == nullptr)
        return nullptr;

    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    return std::make_unique<synth::ui::MidiDestinationPicker>(
        [safeThis]() -> std::vector<synth::ui::MidiDestinationPicker::Option> {
            auto* self = safeThis.getComponent();
            if (self == nullptr || self->host_ == nullptr)
                return {};
            // TrackHeaderHost::MidiDestinationOption and MidiDestinationPicker::Option carry the
            // same three fields by design (the header stays graph-free, so it can't hand the
            // picker anything richer) — converted here rather than sharing one type, so the
            // picker's header has no dependency on TimelineDoc/TrackHeaderHost at all.
            std::vector<synth::ui::MidiDestinationPicker::Option> options;
            for (const auto& option : self->host_->getMidiDestinationOptions(self->trackId_))
                options.push_back({option.displayName, option.nodeUid, option.connected});
            return options;
        },
        [safeThis](juce::uint32 nodeUid, bool connect) {
            auto* self = safeThis.getComponent();
            if (self == nullptr || self->host_ == nullptr)
                return;
            self->host_->setMidiDestinationConnected(self->trackId_, nodeUid, connect);
        });
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

    resolvedColour_ = resolveTrackColour(t->colourArgb, trackIndex(), t->muted);
    colourSwatch_.colour = resolvedColour_;

    muteButton_.setToggleState(t->muted, juce::dontSendNotification);
    soloButton_.setToggleState(t->soloed, juce::dontSendNotification);
    armButton_.setToggleState(t->armed, juce::dontSendNotification);

    automationButton_.setVisible(!t->lanes.empty());

    // An Automation-kind track hosts lanes; a node binding is meaningless for it, so the chip is
    // hidden outright rather than shown pointing at nothing. Midi/Audio tracks are unaffected.
    if (t->kind == synth::TrackKind::Automation) {
        bindingChip_.setVisible(false);
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
    repaint();
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
    auto bounds = getLocalBounds().reduced(kRowPadding);

    colourSwatch_.setBounds(bounds.removeFromLeft(kSwatchWidth));
    bounds.removeFromLeft(kRowPadding);

    // Top row: kind badge + name + (optional A) + R/S/M, right to left. Bottom row: the binding
    // chip, full width (hidden entirely for Automation-kind tracks — see refreshFromDoc()).
    auto topRow = bounds.removeFromTop(bounds.getHeight() / 2);
    armButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    soloButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    muteButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    if (automationButton_.isVisible())
        automationButton_.setBounds(topRow.removeFromRight(kToggleWidth));

    kindBadgeBounds_ = topRow.removeFromLeft(kKindBadgeWidth);
    nameLabel_.setBounds(topRow);

    bindingChip_.setBounds(bounds.reduced(0, 1));
}

void TimelineTrackHeaderComponent::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);

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
    }
}

//==============================================================================
void TimelineTrackHeaderComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        showContextMenu();
}

//==============================================================================
std::vector<TrackHeaderHost::BindingOption> TimelineTrackHeaderComponent::collectBindingOptions() const {
    if (host_ == nullptr)
        return {};
    return host_->getAvailableTrackInNodes(trackId_);
}

void TimelineTrackHeaderComponent::applyBindingMenuChoice(int menuId) {
    if (host_ == nullptr)
        return;

    if (menuId == kNewTrackInNodeMenuId) {
        host_->createAndBindTrackInNode(trackId_);
        return;
    }

    if (menuId == kMidiDestinationsMenuId) {
        if (openMidiDestinationsPickerHook_)
            openMidiDestinationsPickerHook_();
        return;
    }

    const auto options = collectBindingOptions();
    if (menuId < 1 || menuId > (int)options.size())
        return;
    // An explicit user choice — the ONLY way a binding ever changes. Never matched by name.
    host_->bindTrackTo(trackId_, options[(size_t)menuId - 1].uuid);
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

    juce::PopupMenu menu;
    const auto* t = track();
    const juce::String currentUuid = t != nullptr ? t->bindingUuid : juce::String();

    for (int i = 0; i < (int)options.size(); ++i) {
        const auto& option = options[(size_t)i];
        menu.addItem(i + 1, option.displayName, true, option.uuid == currentUuid);
    }
    if (!options.empty())
        menu.addSeparator();
    menu.addItem(kNewTrackInNodeMenuId, "New Track In node");

    // MIDI destinations only make sense for a MIDI-kind track — an Audio or Automation track's
    // binding feeds no MIDI-consuming node, so offering the entry there would open a picker with
    // nothing it could ever wire.
    if (t != nullptr && t->kind == synth::TrackKind::Midi) {
        menu.addSeparator();
        menu.addItem(kMidiDestinationsMenuId, "MIDI destinations...");
    }

    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&bindingChip_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyBindingMenuChoice(result);
    });
}

void TimelineTrackHeaderComponent::applyContextMenuChoice(int menuId) {
    if (menuId == kDeleteTrackMenuId && host_ != nullptr)
        host_->deleteTrack(trackId_);
}

void TimelineTrackHeaderComponent::showContextMenu() {
    juce::PopupMenu menu;
    menu.addItem(kDeleteTrackMenuId, "Delete Track");

    juce::Component::SafePointer<TimelineTrackHeaderComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyContextMenuChoice(result);
    });
}

} // namespace synth::ui
