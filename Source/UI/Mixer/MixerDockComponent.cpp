// Concern: FRO11 (P9-5) -- MixerDockComponent's tab strip, persistence and layout. FRO12 (P9-6,
// docs/mixer.md §5.9) extends this with both panels' detach-to-window hosts and the tab strip's
// own icon-only detach button.
#include "MixerDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

namespace {
constexpr int kAddBusButtonWidth = 54;
constexpr int kResetMetersButtonWidth = 84;
} // namespace

MixerDockComponent::MixerDockComponent(TimelinePanelComponent& timelinePanel, AudioEngine& audioEngine,
                                       synth::TimelineDoc& doc, AppUndoManager& undoManager, GraphEditor& graphEditor,
                                       juce::ApplicationProperties& appProperties,
                                       synth::theme::AppLookAndFeel* lookAndFeel, ShortcutManager* shortcutManager)
    : timelinePanel_(timelinePanel)
    , timelineHost_(timelinePanel_, "Timeline", "timelineWindowBounds", &appProperties, lookAndFeel, shortcutManager)
    , mixerHost_(mixer_, "Mixer", "mixerWindowBounds", &appProperties, lookAndFeel, shortcutManager) {
    addAndMakeVisible(timelineTabButton_);
    addAndMakeVisible(mixerTabButton_);
    timelineTabButton_.setClickingTogglesState(false);
    mixerTabButton_.setClickingTogglesState(false);
    timelineTabButton_.onClick = [this] { setActiveTab(Tab::Timeline); };
    mixerTabButton_.onClick = [this] { setActiveTab(Tab::Mixer); };

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
    addAndMakeVisible(timelineHost_);
    addAndMakeVisible(mixerHost_);

    // FRO15 (docs/mixer.md §5.15): "Add bus" sits on the tab strip and is visible only on the
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

    applyTabVisibility();
}

void MixerDockComponent::setApplicationProperties(juce::ApplicationProperties* properties) {
    appProperties_ = properties;
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    const auto saved = appProperties_->getUserSettings()->getValue(kActiveTabKey, "timeline");
    activeTab_ = saved == "mixer" ? Tab::Mixer : Tab::Timeline;
    applyTabVisibility();
}

void MixerDockComponent::setOnGraphTopologyChanged(std::function<void()> callback) {
    // Forwards straight through: MixerInsertList::onMutated -> MixerColumnComponent::onMutated ->
    // MixerPanelComponent::onGraphMutated (wired per-column in MixerPanelComponent::rebuild()) ->
    // this callback.
    mixer_.onGraphMutated = std::move(callback);
}

void MixerDockComponent::setOnMakeChannelForNode(std::function<void(juce::AudioProcessorGraph::NodeID)> callback) {
    mixer_.onMakeChannelForNode = std::move(callback);
}

void MixerDockComponent::setOnArmTrack(std::function<void(synth::TrackId)> callback) {
    mixer_.onArmTrack = std::move(callback);
}

void MixerDockComponent::setActiveTab(Tab tab) {
    if (activeTab_ == tab)
        return;
    activeTab_ = tab;
    applyTabVisibility();
    persistActiveTab();
}

void MixerDockComponent::setMixerTabEnabled(bool enabled) {
    if (mixerTabEnabled_ == enabled)
        return;
    mixerTabEnabled_ = enabled;
    if (!enabled && activeTab_ == Tab::Mixer)
        setActiveTab(Tab::Timeline); // also runs applyTabVisibility()+persistActiveTab()
    else
        applyTabVisibility();
}

DetachablePanelHost& MixerDockComponent::activeHost() noexcept {
    return (activeTab_ == Tab::Mixer && mixerTabEnabled_) ? mixerHost_ : timelineHost_;
}

void MixerDockComponent::refreshDetachButton() {
    detachButton_.setTooltip(activeHost().isDetached() ? "Dock back" : "Open in window");
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

void MixerDockComponent::applyTabVisibility(bool allowMixerRebuild) {
    const bool mixerActive = activeTab_ == Tab::Mixer && mixerTabEnabled_;
    timelineHost_.setVisible(!mixerActive);
    mixerHost_.setVisible(mixerActive);
    // Also toggle each panel's OWN visible flag, preserving the pre-FRO12 contract
    // TimelinePanelTestFixture.h's timelinePanelIsOpen() documents ("timelinePanel_.isVisible()
    // means the Timeline tab is selected") for the common (docked) case -- but ONLY while docked
    // here: a detached panel is no longer this host's child at all (it lives in its own
    // DetachedPanelWindow), so touching its visible flag would wrongly hide/show it inside that
    // window based on which dock tab happens to be "active" here.
    if (!timelineHost_.isDetached())
        timelinePanel_.setVisible(!mixerActive);
    if (!mixerHost_.isDetached())
        mixer_.setVisible(mixerActive);
    mixerTabButton_.setVisible(mixerTabEnabled_);
    timelineTabButton_.setToggleState(!mixerActive, juce::dontSendNotification);
    mixerTabButton_.setToggleState(mixerActive, juce::dontSendNotification);
    addBusButton_.setVisible(mixerActive);
    resetMetersButton_.setVisible(mixerActive);
    if (mixerActive && allowMixerRebuild)
        mixer_.rebuild();
    refreshDetachButton();
    resized();
}

void MixerDockComponent::persistActiveTab() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kActiveTabKey, activeTab_ == Tab::Mixer ? "mixer" : "timeline");
    appProperties_->getUserSettings()->saveIfNeeded();
}

bool MixerDockComponent::revealColumnForStrip(juce::AudioProcessorGraph::NodeID stripId) {
    setActiveTab(Tab::Mixer);
    return mixer_.revealColumn(stripId);
}

void MixerDockComponent::resized() {
    auto bounds = getLocalBounds();
    auto tabStrip = bounds.removeFromTop(kTabStripHeight);
    // Rightmost: the FRO12 detach button (always present, acts on whichever tab is active), then
    // the FRO15 "+ Bus" button (only visible -- and so only carved -- on the Mixer tab), then
    // whatever's left splits between the two tab buttons, unless FRO12's Own-panel/Window
    // placement has disabled the Mixer tab entirely, in which case Timeline gets the full width.
    detachButton_.setBounds(tabStrip.removeFromRight(kTabStripHeight));
    if (addBusButton_.isVisible())
        addBusButton_.setBounds(tabStrip.removeFromRight(kAddBusButtonWidth));
    if (resetMetersButton_.isVisible())
        resetMetersButton_.setBounds(tabStrip.removeFromRight(kResetMetersButtonWidth));
    if (mixerTabEnabled_) {
        timelineTabButton_.setBounds(tabStrip.removeFromLeft(tabStrip.getWidth() / 2));
        mixerTabButton_.setBounds(tabStrip);
    } else {
        timelineTabButton_.setBounds(tabStrip);
        mixerTabButton_.setBounds({});
    }

    timelineHost_.setBounds(bounds);
    mixerHost_.setBounds(bounds);
}

void MixerDockComponent::lookAndFeelChanged() { refreshDetachButton(); }

} // namespace synth::ui
