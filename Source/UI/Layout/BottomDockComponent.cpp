// Concern: FRO11 (P9-5) -- BottomDockComponent's tab strip, persistence and layout. FRO12 (P9-6,
// docs/mixer/panel.md) extends this with both panels' detach-to-window hosts and the tab strip's
// own icon-only detach button. FRO333 extends it again with a user-reorderable tab order and
// "a detached tab leaves the strip, the active tab falls back to the next one" behaviour.
#include "BottomDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>
#include <utility>

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
    , midiRemoteHost_(midiRemotePanel_, "Controllers", "midiRemoteWindowBounds", &appProperties, lookAndFeel,
                      shortcutManager)
    , timelineTabButton_(*this, Tab::Timeline, "Timeline")
    , mixerTabButton_(*this, Tab::Mixer, "Mixer")
    , midiRemoteTabButton_(*this, Tab::MidiRemote, "Controllers")
    , shortcutManager_(shortcutManager) {
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
    // FRO333: applyTabVisibility() itself now resolves "which tab falls back into view" (a
    // detach/redock on either host, in either direction), so the three hosts can share the one
    // handler unchanged from before -- see applyTabVisibility()'s own comment.
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
    auto* settings = appProperties_->getUserSettings();
    const auto saved = settings->getValue(kActiveTabKey, "timeline");
    activeTab_ = saved == "mixer" ? Tab::Mixer : (saved == "midiRemote" ? Tab::MidiRemote : Tab::Timeline);
    // FRO333: "timeline,mixer,midiRemote" (comma-joined action-suffix names, reusing kActiveTabKey's
    // own vocabulary) -- absent or malformed (wrong length, an unrecognised or repeated name) falls
    // back to the default order rather than half-applying a corrupt permutation.
    const auto savedOrder = juce::StringArray::fromTokens(settings->getValue(kTabOrderKey, ""), ",", "");
    if (savedOrder.size() == 3) {
        std::vector<Tab> parsed;
        for (const auto& name : savedOrder)
            parsed.push_back(name == "mixer" ? Tab::Mixer : (name == "midiRemote" ? Tab::MidiRemote : Tab::Timeline));
        std::vector<Tab> sorted = parsed;
        std::sort(sorted.begin(), sorted.end());
        const std::vector<Tab> everyTab{Tab::Timeline, Tab::Mixer, Tab::MidiRemote};
        if (sorted == everyTab)
            tabOrder_ = parsed;
    }
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
    // FRO333: no separate "was Mixer active?" branch needed any more -- applyTabVisibility() picks
    // a fallback active tab itself whenever the current one stops being offered, which disabling
    // Mixer while it's active is just one more instance of (a detach is the other).
    applyTabVisibility();
}

void BottomDockComponent::refreshTabVisibility() { applyTabVisibility(false); }

DetachablePanelHost& BottomDockComponent::activeHost() noexcept { return hostForTab(activeTab_); }

DetachablePanelHost& BottomDockComponent::hostForTab(Tab tab) noexcept {
    return const_cast<DetachablePanelHost&>(std::as_const(*this).hostForTab(tab));
}

const DetachablePanelHost& BottomDockComponent::hostForTab(Tab tab) const noexcept {
    switch (tab) {
    case Tab::Mixer:
        return mixerHost_;
    case Tab::MidiRemote:
        return midiRemoteHost_;
    case Tab::Timeline:
        break;
    }
    return timelineHost_;
}

juce::TextButton& BottomDockComponent::buttonForTab(Tab tab) noexcept {
    switch (tab) {
    case Tab::Mixer:
        return mixerTabButton_;
    case Tab::MidiRemote:
        return midiRemoteTabButton_;
    case Tab::Timeline:
        break;
    }
    return timelineTabButton_;
}

bool BottomDockComponent::isTabOfferedInStrip(Tab tab) const noexcept {
    if (tab == Tab::Mixer && !mixerTabEnabled_)
        return false;
    return !hostForTab(tab).isDetached();
}

bool BottomDockComponent::hasAnyVisibleTab() const noexcept {
    for (Tab t : tabOrder_)
        if (isTabOfferedInStrip(t))
            return true;
    return false;
}

BottomDockComponent::Tab BottomDockComponent::pickFallbackActiveTab() const noexcept {
    if (isTabOfferedInStrip(activeTab_))
        return activeTab_;
    for (Tab t : tabOrder_)
        if (isTabOfferedInStrip(t))
            return t;
    return activeTab_; // hasAnyVisibleTab() is false -- nothing to fall back to.
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

// FRO333: the fallback pick is folded into every call (not just the detach-state callback) --
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

const char* BottomDockComponent::actionIdForTab(Tab tab) noexcept {
    switch (tab) {
    case Tab::Mixer:
        return "toggleMixerPanel";
    case Tab::MidiRemote:
        return "toggleMidiRemotePanel";
    case Tab::Timeline:
        break;
    }
    return "toggleTimelinePanel";
}

void BottomDockComponent::swapTabOrder(Tab a, Tab b) {
    auto ia = std::find(tabOrder_.begin(), tabOrder_.end(), a);
    auto ib = std::find(tabOrder_.begin(), tabOrder_.end(), b);
    if (ia != tabOrder_.end() && ib != tabOrder_.end())
        std::iter_swap(ia, ib);
}

void BottomDockComponent::persistTabOrder() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    juce::StringArray names;
    for (Tab t : tabOrder_)
        names.add(t == Tab::Mixer ? "mixer" : (t == Tab::MidiRemote ? "midiRemote" : "timeline"));
    appProperties_->getUserSettings()->setValue(kTabOrderKey, names.joinIntoString(","));
    appProperties_->getUserSettings()->saveIfNeeded();
}

// FRO333: a drag-reorder keeps the "Cmd+N opens the Nth tab" convention true by permuting the three
// actions' OWN key bindings to the new order -- but only when they still hold the {Cmd+1, Cmd+2,
// Cmd+3} set as a whole (any assignment), the same "only touch what's still at its convention"
// guard ShortcutManager::migrateSaveAsChordSwap uses. A binding a user rebound away from a bare
// Cmd+digit takes at least one of the three out of that set, so the whole permute is skipped and
// every one of the three keeps whatever the user last set it to.
void BottomDockComponent::permuteShortcutKeysForNewOrder() {
    if (shortcutManager_ == nullptr)
        return;
    std::vector<int> digits;
    for (Tab t : tabOrder_) {
        const auto key = shortcutManager_->getBinding(actionIdForTab(t));
        bool matched = false;
        for (int d = 1; d <= 3; ++d)
            if (key == juce::KeyPress('0' + d, juce::ModifierKeys::commandModifier, 0)) {
                digits.push_back(d);
                matched = true;
                break;
            }
        if (!matched)
            return; // at least one of the three has been rebound away from the convention.
    }
    std::vector<int> sortedDigits = digits;
    std::sort(sortedDigits.begin(), sortedDigits.end());
    if (sortedDigits != std::vector<int>{1, 2, 3})
        return; // not currently a {Cmd+1, Cmd+2, Cmd+3} set (shouldn't happen, belt-and-braces).
    for (size_t i = 0; i < tabOrder_.size(); ++i)
        shortcutManager_->setBinding(actionIdForTab(tabOrder_[i]),
                                     juce::KeyPress('1' + (int)i, juce::ModifierKeys::commandModifier, 0));
    shortcutManager_->saveToProperties();
}

void BottomDockComponent::dragTab(Tab dragged, const juce::MouseEvent& e) {
    if (!e.mouseWasDraggedSinceMouseDown())
        return;
    const auto pos = e.getEventRelativeTo(this).getPosition();
    for (Tab other : tabOrder_) {
        if (other == dragged || !isTabOfferedInStrip(other))
            continue;
        if (buttonForTab(other).getBounds().contains(pos)) {
            swapTabOrder(dragged, other);
            tabDragReordered_ = true;
            resized();
            break;
        }
    }
}

bool BottomDockComponent::endTabDrag() {
    const bool reordered = tabDragReordered_;
    if (reordered) {
        persistTabOrder();
        permuteShortcutKeysForNewOrder();
    }
    tabDragReordered_ = false;
    return reordered;
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
    // the FRO15 "+ Bus" button (only visible -- and so only carved -- on the Mixer tab).
    detachButton_.setBounds(tabStrip.removeFromRight(kTabStripHeight));
    if (addBusButton_.isVisible())
        addBusButton_.setBounds(tabStrip.removeFromRight(kAddBusButtonWidth));
    if (resetMetersButton_.isVisible())
        resetMetersButton_.setBounds(tabStrip.removeFromRight(kResetMetersButtonWidth));

    // FRO333: whatever's left splits between the tabs currently offered (isTabOfferedInStrip --
    // skips a Mixer disabled by placement AND any detached tab), in tabOrder_'s user-controlled
    // (drag-to-reorder) order; the last one absorbs the rounding remainder. A tab not offered is
    // hidden outright -- it has left the strip.
    std::vector<Tab> offered;
    for (Tab t : tabOrder_)
        if (isTabOfferedInStrip(t))
            offered.push_back(t);
    for (size_t i = 0; i < offered.size(); ++i) {
        auto& button = buttonForTab(offered[i]);
        button.setVisible(true);
        button.setBounds(i + 1 == offered.size()
                             ? tabStrip
                             : tabStrip.removeFromLeft(tabStrip.getWidth() / (int)(offered.size() - i)));
    }
    for (Tab t : tabOrder_)
        if (!isTabOfferedInStrip(t))
            buttonForTab(t).setVisible(false);

    timelineHost_.setBounds(bounds);
    mixerHost_.setBounds(bounds);
    midiRemoteHost_.setBounds(bounds);
}

void BottomDockComponent::lookAndFeelChanged() { refreshDetachButton(); }

} // namespace synth::ui
