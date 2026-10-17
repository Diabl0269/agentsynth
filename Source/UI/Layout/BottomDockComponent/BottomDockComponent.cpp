// Concern: BottomDockComponent construction, settings, tab selection and visibility, and the
// forwarders into the Mixer panel.
#include "BottomDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <array>
#include <utility>

namespace synth::ui {

BottomDockComponent::BottomDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine,
                                         synth::TimelineDoc& doc, AppUndoManager& undoManager, GraphEditor& graphEditor,
                                         juce::ApplicationProperties& appProperties,
                                         synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager)
    : timelinePanel_(timelinePanel)
    , mixer_(std::make_unique<MixerPanelComponent>())
    , timelineHost_(timelinePanel_, "Timeline", "timelineWindowBounds", &appProperties, lookAndFeel, shortcutManager)
    , mixerHost_(*mixer_, "Mixer", "mixerWindowBounds", &appProperties, lookAndFeel, shortcutManager)
    , midiRemoteHost_(midiRemotePanel_, "Controllers", "midiRemoteWindowBounds", &appProperties, lookAndFeel,
                      shortcutManager)
    , mixerMirror_(appProperties,
                   [&audioEngine, &doc, &undoManager, &graphEditor](MixerPanelComponent& panel) {
                       // The exact same configure() call mixer_ itself gets below, just
                       // against a second instance and the MixerMirror meter-reader slot -- these
                       // four references outlive `this` for the same reason mixer_.configure()'s
                       // own call already relies on (MainComponent's declaration order).
                       panel.configure(audioEngine.getGraph(), doc, graphEditor.getMacros(), undoManager, graphEditor,
                                       audioEngine, synth::MeterReader::MixerMirror);
                   })
    , timelineTabButton_(*this, Tab::Timeline, nameForTab(Tab::Timeline))
    , mixerTabButton_(*this, Tab::Mixer, nameForTab(Tab::Mixer))
    , midiRemoteTabButton_(*this, Tab::MidiRemote, nameForTab(Tab::MidiRemote))
    , canvasTabButton_(*this, Tab::Canvas, nameForTab(Tab::Canvas))
    , shortcutManager_(shortcutManager) {
    // The leaving tab's own panel flag follows once its host has faded out (see applyTabVisibility).
    timelineFade_.onHidden = [this] {
        if (!timelineHost_.isDetached() && activeTab_ != Tab::Timeline)
            timelinePanel_.setVisible(false);
    };
    mixerFade_.onHidden = [this] {
        if (!mixerHost_.isDetached() && activeTab_ != Tab::Mixer)
            mixer_->setVisible(false);
    };
    midiRemoteFade_.onHidden = [this] {
        if (!midiRemoteHost_.isDetached() && activeTab_ != Tab::MidiRemote)
            midiRemotePanel_.setVisible(false);
    };
    addAndMakeVisible(stripFocus_);
    addAndMakeVisible(timelineTabButton_);
    addAndMakeVisible(mixerTabButton_);
    addAndMakeVisible(midiRemoteTabButton_);
    addChildComponent(canvasTabButton_); // layoutTabButtons() shows it only while it is offered
    for (Tab t : tabModel_.getOrder()) {
        buttonForTab(t).setClickingTogglesState(false);
        buttonForTab(t).onClick = [this, t] { setActiveTab(t); };
    }

    // Keeps each tab's detach button name (refreshDetachIcons()) in sync with the mirror opening/closing, the same
    // way onEitherHostDetachStateChanged below does for a real detach/redock.
    mixerMirror_.onOpenedOrClosed = [this] { refreshDetachIcons(); };

    // Both hosts draw no header of their own while docked here -- this tab strip IS their header.
    // Each host's own constructor already parents its panel (timelinePanel_ / mixer_) into itself
    // via addAndMakeVisible -- a direct addAndMakeVisible(timelinePanel_)/addAndMakeVisible(mixer_)
    // calls are folded into that (adding them a second time, directly to this component, would just
    // steal them back out from under their host and break detach/redock).
    timelineHost_.setEmbeddedHeader(true);
    mixerHost_.setEmbeddedHeader(true);
    midiRemoteHost_.setEmbeddedHeader(true);
    // applyTabVisibility() itself now resolves "which tab falls back into view" (a
    // detach/redock on either host, in either direction), so the three hosts can share the one
    // handler unchanged from before -- see applyTabVisibility()'s own comment.
    auto onEitherHostDetachStateChanged = [this] {
        // False -- see applyTabVisibility()'s own doc comment on why a pure
        // detach/redock must never rebuild the mixer's columns (it would silently wipe every
        // column's latched clip-readout state).
        const bool mainViewChanged = refreshMainViewAvailability();
        applyTabVisibility(false);
        if (mainViewChanged && onMainViewChanged)
            onMainViewChanged();
        if (onPanelDetachStateChanged)
            onPanelDetachStateChanged();
    };
    timelineHost_.onDetachedStateChanged = onEitherHostDetachStateChanged;
    mixerHost_.onDetachedStateChanged = onEitherHostDetachStateChanged;
    midiRemoteHost_.onDetachedStateChanged = onEitherHostDetachStateChanged;
    addAndMakeVisible(timelineHost_);
    addAndMakeVisible(mixerHost_);
    addAndMakeVisible(midiRemoteHost_);

    mixer_->configure(audioEngine.getGraph(), doc, graphEditor.getMacros(), undoManager, graphEditor, audioEngine);
    // The docked mixer's own live mute/solo/pan-law changes reach the "both places" mirror
    // (if open) the instant they happen -- the mirror's own symmetric half of this cross-wire lives
    // in MixerMirrorController::open() (mirror_->onLiveMixerStateChanged), which points back at
    // mixer_.refreshLiveMixerVisuals(). Safe unconditionally: refreshLiveVisualsIfOpen() is a no-op
    // while the mirror is closed.
    mixer_->onLiveMixerStateChanged = [this] { mixerMirror_.refreshLiveVisualsIfOpen(); };
    // A pin / hide edit (or its undo) rebuilds the docked mixer and, if open, the mirror.
    mixer_->onMixerViewChanged = [this] { rebuildMixer(); };

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
    auto* settings = appProperties_->getUserSettings();
    // One vocabulary for all three keys (BottomDockTabModel::keyFor); a saved active tab that is no
    // longer offered (the main view, a detached tab) is resolved by applyTabVisibility() below.
    activeTab_ = BottomDockTabModel::viewForKey(settings->getValue(kActiveTabKey, "timeline")).value_or(Tab::Timeline);
    tabModel_.load(settings->getValue(kTabOrderKey, ""), settings->getValue(kMainViewKey, ""));
    refreshMainViewAvailability();
    placeMainViewHost();
    // The plain initial read -- nothing is open yet, so there is no live transition to run
    // (applyDetachBothPlacesPreference() below is what re-reads this LIVE, on a later settings-file
    // write).
    detachBothPlacesEnabled_ = settings->getValue(kDetachBothPlacesKey, "move") == "both";
    applyTabVisibility();
}

void BottomDockComponent::setOnGraphTopologyChanged(std::function<void()> callback) {
    // Forwards straight through: MixerInsertList::onMutated -> MixerColumnComponent::onMutated ->
    // MixerPanelComponent::onGraphMutated (wired per-column in MixerPanelComponent::rebuild()) ->
    // this callback.
    mixer_->onGraphMutated = std::move(callback);
}

void BottomDockComponent::setOnMakeChannelForNode(std::function<void(juce::AudioProcessorGraph::NodeID)> callback) {
    mixer_->onMakeChannelForNode = std::move(callback);
}

void BottomDockComponent::setOnArmTrack(std::function<void(synth::TrackId)> callback) {
    mixer_->onArmTrack = std::move(callback);
}

void BottomDockComponent::setOnMoveTrack(std::function<void(synth::TrackId, int)> callback) {
    mixer_->onMoveTrack = std::move(callback);
}

void BottomDockComponent::setOnDuplicateTrack(std::function<void(synth::TrackId)> callback) {
    mixer_->onDuplicateTrack = std::move(callback);
}

void BottomDockComponent::setOnBuildTrackColourPicker(
    std::function<std::unique_ptr<synth::ui::ColourPickerPopup>(synth::TrackId)> callback) {
    mixer_->buildTrackColourPicker = std::move(callback);
}

void BottomDockComponent::setActiveTab(Tab tab) {
    if (activeTab_ == tab || tabModel_.isInMain(tab))
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
    // "both places" is scoped to Tab placement -- a mirror should never outlive it (Own
    // panel/Window placement have their own, unrelated detach state, MixerPlacementController's).
    if (!enabled && mixerMirror_.isOpen())
        mixerMirror_.close();
    // The Mixer in the main area goes back to the dock first (the Canvas fills the main area, the setting stays), so
    // the placement controller takes its host from the dock, not from the main-area holder. Back in the Tab
    // placement the Mixer returns to the main area.
    const bool mainViewChanged = refreshMainViewAvailability();
    // No separate "was Mixer active?" branch needed any more -- applyTabVisibility() picks
    // a fallback active tab itself whenever the current one stops being offered, which disabling
    // Mixer while it's active is just one more instance of (a detach is the other).
    applyTabVisibility();
    if (mainViewChanged && onMainViewChanged)
        onMainViewChanged();
}

void BottomDockComponent::refreshTabVisibility() { applyTabVisibility(false); }

DetachablePanelHost* BottomDockComponent::hostForTab(Tab tab) noexcept {
    return const_cast<DetachablePanelHost*>(std::as_const(*this).hostForTab(tab));
}

// The canvas is never detached to a window of its own, so it has no host.
const DetachablePanelHost* BottomDockComponent::hostForTab(Tab tab) const noexcept {
    switch (tab) {
    case Tab::Mixer:
        return &mixerHost_;
    case Tab::MidiRemote:
        return &midiRemoteHost_;
    case Tab::Canvas:
        return nullptr;
    case Tab::Timeline:
        break;
    }
    return &timelineHost_;
}

BottomDockComponent::DockTabButton& BottomDockComponent::buttonForTab(Tab tab) noexcept {
    switch (tab) {
    case Tab::Mixer:
        return mixerTabButton_;
    case Tab::MidiRemote:
        return midiRemoteTabButton_;
    case Tab::Canvas:
        return canvasTabButton_;
    case Tab::Timeline:
        break;
    }
    return timelineTabButton_;
}

bool BottomDockComponent::isTabOfferedInStrip(Tab tab) const noexcept {
    if (tabModel_.isInMain(tab) || (tab == Tab::Mixer && !mixerTabEnabled_))
        return false;
    const auto* host = hostForTab(tab);
    return host == nullptr || !host->isDetached();
}

bool BottomDockComponent::hasAnyVisibleTab() const noexcept {
    for (Tab t : tabModel_.getOrder())
        if (isTabOfferedInStrip(t))
            return true;
    return false;
}

BottomDockComponent::~BottomDockComponent() = default;

void BottomDockComponent::rebuildMixer() {
    mixer_->rebuildWhenShown();
    mixerMirror_.rebuildIfOpen();
}

void BottomDockComponent::refreshMixerTrackColours() {
    mixer_->refreshTrackColours();
    mixerMirror_.refreshTrackColoursIfOpen();
}

void BottomDockComponent::refreshLiveMixerVisualsEverywhere() {
    mixer_->refreshLiveMixerVisuals();
    mixerMirror_.refreshLiveVisualsIfOpen();
}

void BottomDockComponent::unbindAllMixerViews() {
    mixer_->unbindAllColumns();
    mixerMirror_.unbindIfOpen();
}

void BottomDockComponent::unbindMixerViewsFor(const std::vector<juce::AudioProcessorGraph::NodeID>& doomed) {
    mixer_->unbindColumnsFor(doomed);
    mixerMirror_.unbindIfOpenFor(doomed);
}

void BottomDockComponent::rebuildIfStaleMixerViews() {
    mixer_->rebuildIfStale();
    mixerMirror_.rebuildIfStaleIfOpen();
}

void BottomDockComponent::refreshMeters() {
    mixer_->refreshMeters();
    mixerMirror_.refreshMetersIfOpen(); // Independent meter cadence, own MeterReader slot
}

MixerPanelComponent& BottomDockComponent::getMixerPanel() noexcept { return *mixer_; }

const MixerPanelComponent& BottomDockComponent::getMixerPanel() const noexcept { return *mixer_; }

BottomDockComponent::Tab BottomDockComponent::pickFallbackActiveTab() const noexcept {
    if (isTabOfferedInStrip(activeTab_))
        return activeTab_;
    for (Tab t : tabModel_.getOrder())
        if (isTabOfferedInStrip(t))
            return t;
    return activeTab_; // hasAnyVisibleTab() is false -- nothing to fall back to.
}

// The fallback pick is folded into every call (not just the detach-state callback) --
// pickFallbackActiveTab() is a no-op read when activeTab_ is already offered, so this costs nothing
// on the common "just an ordinary tab switch or a mixer rebuild" path, and it is what makes a
// detach, a redock and setMixerTabEnabled(false) all resolve the same way without three separate
// "which tab do I fall back to" implementations.
void BottomDockComponent::applyTabVisibility(bool allowMixerRebuild) {
    const Tab resolved = pickFallbackActiveTab();
    if (resolved != activeTab_) {
        activeTab_ = resolved;
        persistActiveTab();
        if (onActiveTabChanged)
            onActiveTabChanged();
    }

    const bool mixerActive = activeTab_ == Tab::Mixer;
    const bool midiRemoteActive = activeTab_ == Tab::MidiRemote;
    const bool timelineActive = activeTab_ == Tab::Timeline;
    // A host in the main area shows whatever tab is active here.
    const bool timelineShown = timelineActive || isInMainArea(Tab::Timeline);
    const bool mixerShown = mixerActive || isInMainArea(Tab::Mixer);
    const bool midiRemoteShown = midiRemoteActive || isInMainArea(Tab::MidiRemote);
    // A tab switch cross-fades: the leaving host fades out as the arriving one fades in (FadeVisibility, animation.md).
    // Whether a tab is selected lands at once (isShown()); the leaving panel keeps its own visible flag until its host
    // has faded out (onHidden), so the picture does not empty while it fades.
    timelineFade_.setShown(timelineShown);
    mixerFade_.setShown(mixerShown);
    midiRemoteFade_.setShown(midiRemoteShown);
    // Also toggle each panel's OWN visible flag, preserving the contract
    // TimelinePanelTestFixture.h's timelinePanelIsOpen() documents ("timelinePanel_.isVisible()
    // means the Timeline tab is selected") for the common (docked) case -- but ONLY while docked
    // here: a detached panel is no longer this host's child at all (it lives in its own
    // DetachedPanelWindow), so touching its visible flag would wrongly hide/show it inside that
    // window based on which dock tab happens to be "active" here.
    const auto syncPanel = [](synth::ui::DetachablePanelHost& host, synth::ui::FadeVisibility& fade,
                              juce::Component& panel, bool active) {
        if (host.isDetached())
            return;
        if (active || !fade.isFading())
            panel.setVisible(active);
    };
    syncPanel(timelineHost_, timelineFade_, timelinePanel_, timelineShown);
    syncPanel(mixerHost_, mixerFade_, *mixer_, mixerShown);
    syncPanel(midiRemoteHost_, midiRemoteFade_, midiRemotePanel_, midiRemoteShown);
    timelineTabButton_.setToggleState(timelineActive, juce::dontSendNotification);
    mixerTabButton_.setToggleState(mixerActive, juce::dontSendNotification);
    midiRemoteTabButton_.setToggleState(midiRemoteActive, juce::dontSendNotification);
    canvasTabButton_.setToggleState(activeTab_ == Tab::Canvas, juce::dontSendNotification);
    const std::array<juce::Component*, 5> announced{&timelineTabButton_, &mixerTabButton_, &midiRemoteTabButton_,
                                                    &canvasTabButton_, &stripFocus_};
    for (auto* c : announced)
        if (auto* handler = c->getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    if (mixerActive && allowMixerRebuild)
        mixer_->rebuild();
    // Catches up on any profile/assignment change that happened while this tab was hidden,
    // same reasoning as the Mixer tab's own "coming back into view" rebuild above --
    // allowMixerRebuild's false-on-pure-detach exception applies here too, for the same reason (a
    // detach/redock changes nothing about which profile is selected). Belt-and-braces now
    // that MidiLearnController::onChanged and MainComponent::reconcileTimelineAfterGraphChange()
    // keep the panel live while it's SHOWING too (Learn/Forget/Undo/Redo/a panel edit) -- this call
    // still matters for the case those two don't cover: a change made while the tab was hidden,
    // between the last live refresh and now.
    if (midiRemoteActive && allowMixerRebuild)
        midiRemotePanel_.rebuildFromProfiles();
    refreshDetachIcons();
    refreshSwapIcons();
    resized();
    // After the layout above, so a listener reading getCanvasBodyBounds() sees the new tab's geometry.
    if (canvasBodyWasShowing_ != isCanvasBodyShowing()) {
        canvasBodyWasShowing_ = isCanvasBodyShowing();
        if (onCanvasBodyChanged)
            onCanvasBodyChanged();
    }
}

void BottomDockComponent::persistActiveTab() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kActiveTabKey, BottomDockTabModel::keyFor(activeTab_));
    appProperties_->getUserSettings()->saveIfNeeded();
}

void BottomDockComponent::persistTabOrder() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTabOrderKey, BottomDockTabModel::serializeOrder(tabModel_.getOrder()));
    appProperties_->getUserSettings()->saveIfNeeded();
}

bool BottomDockComponent::toggleActiveSidePane(bool forceOpen) {
    if (activeTab_ == Tab::Mixer)
        return mixer_->toggleSidePane(forceOpen);
    return activeTab_ == Tab::Timeline && timelinePanel_.toggleSidePane(forceOpen);
}

bool BottomDockComponent::revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId) {
    setActiveTab(Tab::Mixer);
    return mixer_->revealColumn(stripId);
}

} // namespace synth::ui
