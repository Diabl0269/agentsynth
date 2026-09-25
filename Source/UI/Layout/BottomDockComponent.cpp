// Concern: FRO11 (P9-5) -- BottomDockComponent's tab strip, persistence and layout. FRO12 (P9-6,
// docs/mixer/panel.md) extends this with both panels' detach-to-window hosts and the tab strip's
// own icon-only detach button.
#include "BottomDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

namespace {
constexpr int kAddBusButtonWidth = 54;
constexpr int kResetMetersButtonWidth = 84;
} // namespace

BottomDockComponent::BottomDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine,
                                         synth::TimelineDoc& doc, AppUndoManager& undoManager, GraphEditor& graphEditor,
                                         juce::ApplicationProperties& appProperties,
                                         synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager)
    : timelinePanel_(timelinePanel)
    , timelineHost_(timelinePanel_, "Timeline", "timelineWindowBounds", &appProperties, lookAndFeel, shortcutManager)
    , mixerHost_(mixer_, "Mixer", "mixerWindowBounds", &appProperties, lookAndFeel, shortcutManager)
    , midiRemoteHost_(midiRemotePanel_, "MIDI Remote", "midiRemoteWindowBounds", &appProperties, lookAndFeel,
                      shortcutManager) {
    addAndMakeVisible(timelineTabButton_);
    addAndMakeVisible(mixerTabButton_);
    addAndMakeVisible(midiRemoteTabButton_);
    timelineTabButton_.setClickingTogglesState(false);
    mixerTabButton_.setClickingTogglesState(false);
    midiRemoteTabButton_.setClickingTogglesState(false);
    timelineTabButton_.onClick = [this] { setActiveTab(Tab::Timeline); };
    mixerTabButton_.onClick = [this] { setActiveTab(Tab::Mixer); };
    midiRemoteTabButton_.onClick = [this] { setActiveTab(Tab::MidiRemote); };

    // FRO12: icon-only, lives in this strip rather than either host's own header -- see
    // DetachablePanelHost's class comment. Acts on whichever tab is active (activeHost()).
    detachButton_.setClickingTogglesState(false);
    detachButton_.onClick = [this] { activeHost().setDetached(!activeHost().isDetached()); };
    addAndMakeVisible(detachButton_);

    // Both hosts draw no header of their own while docked here -- this tab strip IS their header.
    // Each host's own constructor already parents its panel (timelinePanel_ / mixer_) into itself
    // via addAndMakeVisible -- FRO15's own direct addAndMakeVisible(timelinePanel_)/addAndMakeVisible(mixer_)
    // calls are folded into that (adding them a second time, directly to this component, would just
    // steal them back out from under their host and break detach/redock).
    timelineHost_.setEmbeddedHeader(true);
    mixerHost_.setEmbeddedHeader(true);
    midiRemoteHost_.setEmbeddedHeader(true);
    auto onEitherHostDetachStateChanged = [this] {
        // FRO146 follow-up: false -- see applyTabVisibility()'s own doc comment on why a pure
        // detach/redock must never rebuild the mixer's columns (it would silently wipe every
        // column's latched clip-readout state).
        applyTabVisibility(false);
        if (onPanelDetachStateChanged)
            onPanelDetachStateChanged();
    };
    timelineHost_.onDetachedStateChanged = onEitherHostDetachStateChanged;
    mixerHost_.onDetachedStateChanged = onEitherHostDetachStateChanged;
    midiRemoteHost_.onDetachedStateChanged = onEitherHostDetachStateChanged;
    addAndMakeVisible(timelineHost_);
    addAndMakeVisible(mixerHost_);
    addAndMakeVisible(midiRemoteHost_);

    // FRO15 (docs/mixer/sends-and-buses.md): "Add bus" sits on the tab strip and is visible only on the
    // Mixer tab -- it has no meaning while the Timeline tab is showing.
    addAndMakeVisible(addBusButton_);
    addBusButton_.setClickingTogglesState(false);
    addBusButton_.onClick = [this] {
        mixer_.createBus();
        mixer_.rebuild(); // the new bus's own column, without waiting for the owner's reconcile
    };

    // FRO146: sits beside "+ Bus", same Mixer-tab-only visibility -- resets every column's clip
    // readout to "-inf", not clipped.
    addAndMakeVisible(resetMetersButton_);
    resetMetersButton_.setClickingTogglesState(false);
    resetMetersButton_.onClick = [this] { mixer_.resetAllMeterReadouts(); };

    mixer_.configure(audioEngine.getGraph(), doc, graphEditor.getMacros(), undoManager, graphEditor, audioEngine);

    // Last, so the top few pixels always belong to the resize gesture whatever tab is showing. The
    // dock is the handle's owner, so the desired height it reports is already the TOTAL dock height.
    addAndMakeVisible(resizeHandle_);
    resizeHandle_.setComponentID("dockResizeHandle");
    resizeHandle_.onResize = [this](int desiredHeight) {
        if (onResizeHeight)
            onResizeHeight(desiredHeight);
    };
    resizeHandle_.onResizeCommitted = [this](int desiredHeight) {
        if (onResizeHeightCommitted)
            onResizeHeightCommitted(desiredHeight);
    };

    applyTabVisibility();
}

void BottomDockComponent::setApplicationProperties(juce::ApplicationProperties* properties) {
    appProperties_ = properties;
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    const auto saved = appProperties_->getUserSettings()->getValue(kActiveTabKey, "timeline");
    activeTab_ = saved == "mixer" ? Tab::Mixer : (saved == "midiRemote" ? Tab::MidiRemote : Tab::Timeline);
    applyTabVisibility();
}

void BottomDockComponent::setOnGraphTopologyChanged(std::function<void()> callback) {
    // Forwards straight through: MixerInsertList::onMutated -> MixerColumnComponent::onMutated ->
    // MixerPanelComponent::onGraphMutated (wired per-column in MixerPanelComponent::rebuild()) ->
    // this callback.
    mixer_.onGraphMutated = std::move(callback);
}

void BottomDockComponent::setOnMakeChannelForNode(std::function<void(juce::AudioProcessorGraph::NodeID)> callback) {
    mixer_.onMakeChannelForNode = std::move(callback);
}

void BottomDockComponent::setOnArmTrack(std::function<void(synth::TrackId)> callback) {
    mixer_.onArmTrack = std::move(callback);
}

void BottomDockComponent::setActiveTab(Tab tab) {
    if (activeTab_ == tab)
        return;
    activeTab_ = tab;
    applyTabVisibility();
    persistActiveTab();
    if (onActiveTabChanged)
        onActiveTabChanged();
}

void BottomDockComponent::setMixerTabEnabled(bool enabled) {
    if (mixerTabEnabled_ == enabled)
        return;
    mixerTabEnabled_ = enabled;
    if (!enabled && activeTab_ == Tab::Mixer)
        setActiveTab(Tab::Timeline); // also runs applyTabVisibility()+persistActiveTab()
    else
        applyTabVisibility();
}

DetachablePanelHost& BottomDockComponent::activeHost() noexcept {
    if (activeTab_ == Tab::Mixer && mixerTabEnabled_)
        return mixerHost_;
    if (activeTab_ == Tab::MidiRemote)
        return midiRemoteHost_;
    return timelineHost_;
}

void BottomDockComponent::refreshDetachButton() {
    // FRO228: same wording as the tooltip -- without an explicit setTitle(), the ctor's
    // "detachActiveTab" component name leaks as the AX title (ButtonAccessibilityHandler::
    // getTitle()'s getButtonText() fallback).
    const juce::String label = activeHost().isDetached() ? "Dock back" : "Open in window";
    detachButton_.setTooltip(label);
    detachButton_.setTitle(label);
    // Same dynamic_cast-with-null-fallback convention DetachablePanelHost::applyIcon uses.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    auto base = lf->getIcon(synth::theme::Icon::ActionDetachWindow);
    if (base == nullptr)
        return;
    const auto& colors = lf->getTheme().colors;
    auto hoverIcon = base->createCopy();
    hoverIcon->replaceColour(colors.textMuted, colors.textPrimary);
    detachButton_.setImages(base.get(), hoverIcon.get(), hoverIcon.get());
}

void BottomDockComponent::applyTabVisibility(bool allowMixerRebuild) {
    const bool mixerActive = activeTab_ == Tab::Mixer && mixerTabEnabled_;
    const bool midiRemoteActive = activeTab_ == Tab::MidiRemote;
    const bool timelineActive = !mixerActive && !midiRemoteActive;
    timelineHost_.setVisible(timelineActive);
    mixerHost_.setVisible(mixerActive);
    midiRemoteHost_.setVisible(midiRemoteActive);
    // Also toggle each panel's OWN visible flag, preserving the pre-FRO12 contract
    // TimelinePanelTestFixture.h's timelinePanelIsOpen() documents ("timelinePanel_.isVisible()
    // means the Timeline tab is selected") for the common (docked) case -- but ONLY while docked
    // here: a detached panel is no longer this host's child at all (it lives in its own
    // DetachedPanelWindow), so touching its visible flag would wrongly hide/show it inside that
    // window based on which dock tab happens to be "active" here.
    if (!timelineHost_.isDetached())
        timelinePanel_.setVisible(timelineActive);
    if (!mixerHost_.isDetached())
        mixer_.setVisible(mixerActive);
    if (!midiRemoteHost_.isDetached())
        midiRemotePanel_.setVisible(midiRemoteActive);
    mixerTabButton_.setVisible(mixerTabEnabled_);
    timelineTabButton_.setToggleState(timelineActive, juce::dontSendNotification);
    mixerTabButton_.setToggleState(mixerActive, juce::dontSendNotification);
    midiRemoteTabButton_.setToggleState(midiRemoteActive, juce::dontSendNotification);
    addBusButton_.setVisible(mixerActive);
    resetMetersButton_.setVisible(mixerActive);
    if (mixerActive && allowMixerRebuild)
        mixer_.rebuild();
    // FRO131: catches up on any profile/assignment change that happened while this tab was hidden,
    // same reasoning as the Mixer tab's own "coming back into view" rebuild above --
    // allowMixerRebuild's false-on-pure-detach exception applies here too, for the same reason (a
    // detach/redock changes nothing about which profile is selected). FRO263: belt-and-braces now
    // that MidiLearnController::onChanged and MainComponent::reconcileTimelineAfterGraphChange()
    // keep the panel live while it's SHOWING too (Learn/Forget/Undo/Redo/a panel edit) -- this call
    // still matters for the case those two don't cover: a change made while the tab was hidden,
    // between the last live refresh and now.
    if (midiRemoteActive && allowMixerRebuild)
        midiRemotePanel_.rebuildFromProfiles();
    refreshDetachButton();
    resized();
}

void BottomDockComponent::persistActiveTab() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    const char* value = "timeline";
    if (activeTab_ == Tab::Mixer)
        value = "mixer";
    else if (activeTab_ == Tab::MidiRemote)
        value = "midiRemote";
    appProperties_->getUserSettings()->setValue(kActiveTabKey, value);
    appProperties_->getUserSettings()->saveIfNeeded();
}

bool BottomDockComponent::revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId) {
    setActiveTab(Tab::Mixer);
    return mixer_.revealColumn(stripId);
}

void BottomDockComponent::resized() {
    // The grab strip runs the dock's full width along its top edge, OVERLAPPING the tab strip: the
    // strip keeps its full kTabStripHeight (the content below never moves), but its buttons are
    // laid out below the handle so a resize grab never lands on one.
    resizeHandle_.setBounds(0, 0, getWidth(), PanelResizeHandle::kHeight);

    auto bounds = getLocalBounds();
    auto tabStrip = bounds.removeFromTop(kTabStripHeight).withTrimmedTop(PanelResizeHandle::kHeight);
    // Rightmost: the FRO12 detach button (always present, acts on whichever tab is active), then
    // the FRO15 "+ Bus" button (only visible -- and so only carved -- on the Mixer tab), then
    // whatever's left splits between the two tab buttons, unless FRO12's Own-panel/Window
    // placement has disabled the Mixer tab entirely, in which case Timeline gets the full width.
    detachButton_.setBounds(tabStrip.removeFromRight(kTabStripHeight));
    if (addBusButton_.isVisible())
        addBusButton_.setBounds(tabStrip.removeFromRight(kAddBusButtonWidth));
    if (resetMetersButton_.isVisible())
        resetMetersButton_.setBounds(tabStrip.removeFromRight(kResetMetersButtonWidth));
    // FRO131: MidiRemote has no placement variant (unlike Mixer's mixerTabEnabled_), so its button
    // is always offered -- a three-way split when Mixer is also offered, two-way otherwise.
    if (mixerTabEnabled_) {
        const int third = tabStrip.getWidth() / 3;
        timelineTabButton_.setBounds(tabStrip.removeFromLeft(third));
        mixerTabButton_.setBounds(tabStrip.removeFromLeft(third));
        midiRemoteTabButton_.setBounds(tabStrip);
    } else {
        const int half = tabStrip.getWidth() / 2;
        timelineTabButton_.setBounds(tabStrip.removeFromLeft(half));
        mixerTabButton_.setBounds({});
        midiRemoteTabButton_.setBounds(tabStrip);
    }

    timelineHost_.setBounds(bounds);
    mixerHost_.setBounds(bounds);
    midiRemoteHost_.setBounds(bounds);
}

void BottomDockComponent::lookAndFeelChanged() { refreshDetachButton(); }

} // namespace synth::ui
