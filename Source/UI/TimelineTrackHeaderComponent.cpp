#include "TimelineTrackHeaderComponent.h"
#include "Theme/AppLookAndFeel.h"
#include "TrackColour.h"

namespace synth::ui {

namespace {

constexpr int kSwatchWidth = 8;
constexpr int kToggleWidth = 20;
constexpr int kRowPadding = 3;
constexpr int kKindBadgeWidth = 34;

// Fixed per-TrackKind label. Never edited, never doc-driven beyond the kind itself.
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

// Themed colours with literal fallbacks — the headless test path installs no AppLookAndFeel (same
// pattern as TimelinePanelComponent::paint()).
struct HeaderColours {
    juce::Colour surface{juce::Colour(0xff1B1F26)};
    juce::Colour border{juce::Colour(0xff2A2F38)};
    juce::Colour text{juce::Colour(0xffEAEEF3)};
    juce::Colour textMuted{juce::Colour(0xff8A93A0)};
    juce::Colour warning{juce::Colour(0xffE0A33D)};
    juce::Colour accent{juce::Colour(0xff00D1FF)};
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
        const auto* t = track();
        if (t == nullptr)
            return;
        const juce::uint32 next = nextTrackPaletteColour(t->colourArgb);
        performEdit([this, next] { doc_.setTrackColour(trackId_, next); });
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

void TimelineTrackHeaderComponent::performEdit(const std::function<void()>& mutation) {
    if (host_ != nullptr)
        host_->performTrackEdit(mutation);
    else
        mutation();
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

        const auto colours = coloursFor(*this);
        bindingChip_.setColour(juce::TextButton::buttonColourId, chipWarning_ ? colours.warning : colours.surface);
        bindingChip_.setColour(juce::TextButton::textColourOffId, chipWarning_ ? colours.surface : colours.text);
    }

    repaint();
}

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

    // Track-kind badge: a muted micro-font pill right of the swatch, before the name. Fixed per
    // TrackKind (see kindBadgeText above) — this is identity chrome, not a control.
    if (const auto* t = track()) {
        float microSize = 8.5f;
        if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
            microSize = lf->getTheme().type.micro;

        auto badgeBounds = kindBadgeBounds_.reduced(1, 3).toFloat();
        g.setColour(colours.textMuted.withAlpha(0.15f));
        g.fillRoundedRectangle(badgeBounds, 3.0f);
        g.setColour(colours.textMuted.withAlpha(0.6f));
        g.drawRoundedRectangle(badgeBounds, 3.0f, 1.0f);

        g.setColour(colours.textMuted);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
        g.drawText(kindBadgeText(t->kind), kindBadgeBounds_, juce::Justification::centred, false);
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

    const auto options = collectBindingOptions();
    if (menuId < 1 || menuId > (int)options.size())
        return;
    // An explicit user choice — the ONLY way a binding ever changes. Never matched by name.
    host_->bindTrackTo(trackId_, options[(size_t)menuId - 1].uuid);
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
