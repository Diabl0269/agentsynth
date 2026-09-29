// Concern: BottomDockComponent's tab strip, persistence and layout. Both panels'
// detach-to-window hosts (docs/mixer/panel.md) and the tab strip's own icon-only detach
// button live here too, with a user-reorderable tab order and "a detached tab leaves the
// strip, the active tab falls back to the next one" behaviour.
#include "BottomDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>
#include <utility>

namespace synth::ui {

namespace {
constexpr int kAddBusButtonWidth = 54;
constexpr int kResetMetersButtonWidth = 84;
// How far above its slot a dragged tab is drawn. The tab row starts PanelResizeHandle::kHeight (5)
// px below the dock's top edge, so a 4 px lift keeps the lifted tab and its 1 px shadow inside the
// dock's own bounds.
constexpr int kLiftedTabRise = 4;
constexpr float kLiftedTabRadius = 4.0f;
constexpr float kLiftedTabOpacity = 0.95f;
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
    , mixerMirror_(appProperties,
                   [&audioEngine, &doc, &undoManager, &graphEditor](MixerPanelComponent& panel) {
                       // The exact same configure() call mixer_ itself gets below, just
                       // against a second instance and the MixerMirror meter-reader slot -- these
                       // four references outlive `this` for the same reason mixer_.configure()'s
                       // own call already relies on (MainComponent's declaration order).
                       panel.configure(audioEngine.getGraph(), doc, graphEditor.getMacros(), undoManager, graphEditor,
                                       audioEngine, synth::MeterReader::MixerMirror);
                   })
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

    // Icon-only, lives in this strip rather than either host's own header -- see
    // DetachablePanelHost's class comment. Acts on whichever tab is active (activeHost()) --
    // OR, for the Mixer tab with "both places" on, toggles the mirror window instead (see
    // toggleActiveHostDetach()).
    detachButton_.setClickingTogglesState(false);
    detachButton_.onClick = [this] { toggleActiveHostDetach(); };
    addAndMakeVisible(detachButton_);

    // Keeps the detach button's own label/icon state (refreshDetachButton()) in sync with
    // the mirror opening/closing, the same way onEitherHostDetachStateChanged below does for a real
    // detach/redock.
    mixerMirror_.onOpenedOrClosed = [this] { refreshDetachButton(); };

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

    // "Add bus" sits on the tab strip and is visible only on the Mixer tab -- it has no meaning
    // while the Timeline tab is showing (see docs/mixer/sends-and-buses.md).
    addAndMakeVisible(addBusButton_);
    addBusButton_.setClickingTogglesState(false);
    addBusButton_.onClick = [this] {
        mixer_.createBus();
        mixer_.rebuild(); // the new bus's own column, without waiting for the owner's reconcile
    };

    // Sits beside "+ Bus", same Mixer-tab-only visibility -- resets every column's clip
    // readout to "-inf", not clipped.
    addAndMakeVisible(resetMetersButton_);
    resetMetersButton_.setClickingTogglesState(false);
    resetMetersButton_.onClick = [this] { mixer_.resetAllMeterReadouts(); };

    mixer_.configure(audioEngine.getGraph(), doc, graphEditor.getMacros(), undoManager, graphEditor, audioEngine);
    // The docked mixer's own live mute/solo/pan-law changes reach the "both places" mirror
    // (if open) the instant they happen -- the mirror's own symmetric half of this cross-wire lives
    // in MixerMirrorController::open() (mirror_->onLiveMixerStateChanged), which points back at
    // mixer_.refreshLiveMixerVisuals(). Safe unconditionally: refreshLiveVisualsIfOpen() is a no-op
    // while the mirror is closed.
    mixer_.onLiveMixerStateChanged = [this] { mixerMirror_.refreshLiveVisualsIfOpen(); };

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
    // "timeline,mixer,midiRemote" (comma-joined action-suffix names, reusing kActiveTabKey's
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
    // The plain initial read -- nothing is open yet, so there is no live transition to run
    // (applyDetachBothPlacesPreference() below is what re-reads this LIVE, on a later settings-file
    // write).
    detachBothPlacesEnabled_ = settings->getValue(kDetachBothPlacesKey, "move") == "both";
    applyTabVisibility();
}

void BottomDockComponent::applyDetachBothPlacesPreference() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    const bool enabled = appProperties_->getUserSettings()->getValue(kDetachBothPlacesKey, "move") == "both";
    if (enabled == detachBothPlacesEnabled_)
        return;
    detachBothPlacesEnabled_ = enabled;
    // Scoped to Tab placement (mixerTabEnabled_) -- Own-panel/Window placement's own detach state is
    // MixerPlacementController's, not this preference's, to transition (see this method's own doc
    // comment on BottomDockComponent.h).
    if (mixerTabEnabled_) {
        if (enabled && mixerHost_.isDetached()) {
            // Was "move" (the real host detached, tab gone from the strip) -- redock it, switch
            // back to it (redocking alone does not: applyTabVisibility()'s fallback pick never
            // un-does an earlier fallback just because Mixer became offered again) and open a
            // mirror instead, so both places show it as the preference now promises.
            mixerHost_.setDetached(false);
            setActiveTab(Tab::Mixer);
            toggleActiveHostDetach(); // Mixer is active + detachBothPlacesEnabled_ is already true -> opens the mirror
        } else if (!enabled && mixerMirror_.isOpen()) {
            // Was "both" -- close the mirror and detach the real host instead, which is exactly
            // the "a detached tab leaves the strip" behaviour the default preference restores.
            mixerMirror_.close();
            mixerHost_.setDetached(true);
        }
    } else if (mixerMirror_.isOpen()) {
        // A mirror should never outlive Tab placement -- see setMixerTabEnabled()'s own comment.
        mixerMirror_.close();
    }
    refreshDetachButton();
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
    // "both places" is scoped to Tab placement -- a mirror should never outlive it (Own
    // panel/Window placement have their own, unrelated detach state, MixerPlacementController's).
    if (!enabled && mixerMirror_.isOpen())
        mixerMirror_.close();
    // No separate "was Mixer active?" branch needed any more -- applyTabVisibility() picks
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

// Replaces the plain activeHost().setDetached(!isDetached()) toggle -- routes the Mixer
// tab through the mirror instead of a real detach while "both places" is on.
void BottomDockComponent::toggleActiveHostDetach() {
    if (usesMixerMirrorForDetach()) {
        if (mixerMirror_.isOpen())
            mixerMirror_.close();
        else
            openMixerMirror();
        return;
    }
    activeHost().setDetached(!activeHost().isDetached());
}

void BottomDockComponent::openMixerMirror() {
    // Same dynamic_cast-with-null-fallback convention refreshDetachButton()/applyIcon() use --
    // headless tests (no real LookAndFeel installed yet) leave the mirror window unthemed, matching
    // a real detach's own fallback.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    mixerMirror_.open(mixer_, lf, shortcutManager_, mixerHost_.onAppShortcutFallback,
                      mixerHost_.isCreatingNativeWindows());
}

void BottomDockComponent::refreshDetachButton() {
    // Same wording as the tooltip -- without an explicit setTitle(), the ctor's
    // "detachActiveTab" component name leaks as the AX title (ButtonAccessibilityHandler::
    // getTitle()'s getButtonText() fallback). While "both places" drives the Mixer tab, the
    // button's own state tracks the MIRROR (mixerHost_ itself never actually detaches then).
    const bool detached = usesMixerMirrorForDetach() ? mixerMirror_.isOpen() : activeHost().isDetached();
    const juce::String label = detached ? "Dock back" : "Open in window";
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
    timelineHost_.setVisible(timelineActive);
    mixerHost_.setVisible(mixerActive);
    midiRemoteHost_.setVisible(midiRemoteActive);
    // Also toggle each panel's OWN visible flag, preserving the contract
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

// A drag-reorder keeps the "Cmd+N opens the Nth tab" convention true by permuting the three
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
    // The lift follows the pointer by the same grab offset the press landed at inside the tab, and
    // is gated on the same drag threshold as the swap below, so a plain click never shows it.
    liftedTab_ = dragged;
    liftedLeft_ = pos.x - e.getMouseDownPosition().x;
    repaint(0, 0, getWidth(), kTabStripHeight);
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
    if (liftedTab_.has_value()) {
        liftedTab_.reset();
        repaint(0, 0, getWidth(), kTabStripHeight);
    }
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
    // Rightmost: the detach button (always present, acts on whichever tab is active). The
    // "+ Bus"/"Reset Meters" buttons are NOT carved from here: doing so only while Mixer was
    // active would shrink the tabs' shared area on Mixer alone, visibly resizing every tab button
    // on a tab switch. They live in their own slim toolbar row carved from the CONTENT area below
    // (see the Mixer-only block right after), so the tab strip's width split is identical
    // regardless of activeTab_.
    detachButton_.setBounds(tabStrip.removeFromRight(kTabStripHeight));

    // Whatever's left splits between the tabs currently offered (isTabOfferedInStrip --
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

    // The Mixer-only toolbar (Add bus/Reset Meters) is carved from the MIXER HOST'S OWN
    // content area only, below the tab strip -- Timeline and Controllers always get `bounds`
    // unmodified, so switching to Mixer never shrinks anything but the Mixer tab's own content, and
    // switching away never leaves a stale shrink behind (recomputed from `bounds` every resized()).
    auto mixerBounds = bounds;
    if (addBusButton_.isVisible() || resetMetersButton_.isVisible()) {
        auto toolbar = mixerBounds.removeFromTop(kMixerToolbarHeight);
        if (addBusButton_.isVisible())
            addBusButton_.setBounds(toolbar.removeFromRight(kAddBusButtonWidth));
        if (resetMetersButton_.isVisible())
            resetMetersButton_.setBounds(toolbar.removeFromRight(kResetMetersButtonWidth));
    }

    timelineHost_.setBounds(bounds);
    // In the Own-panel placement the host lives in MixerPlacementController's strip, which lays it
    // out itself; sizing it to the dock here would leave it at the wrong height until that strip's
    // next layout pass.
    if (mixerHost_.getParentComponent() == this)
        mixerHost_.setBounds(mixerBounds);
    midiRemoteHost_.setBounds(bounds);
}

std::vector<BottomDockComponent::StripTab> BottomDockComponent::getStripTabs() {
    std::vector<StripTab> tabs;
    for (Tab t : tabOrder_)
        if (isTabOfferedInStrip(t))
            tabs.push_back({&buttonForTab(t), actionIdForTab(t), buttonForTab(t).getButtonText()});
    return tabs;
}

void BottomDockComponent::DockTabButton::paint(juce::Graphics& g) {
    // The real tab steps aside for the dashed slot marker while it is the one being dragged.
    if (owner_.isTabLifted(tab_))
        owner_.paintTabSlot(g, getLocalBounds());
    else
        juce::TextButton::paint(g);
}

void BottomDockComponent::paintTabSlot(juce::Graphics& g, juce::Rectangle<int> bounds) const {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    juce::Path outline;
    outline.addRoundedRectangle(bounds.toFloat().reduced(0.5f), kLiftedTabRadius);
    juce::Path dashed;
    const float dashLengths[] = {3.0f, 2.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashLengths, 2);
    g.setColour(lf->getTheme().colors.border);
    g.fillPath(dashed);
}

// Painted over the children so the lifted tab sits above every tab button and the resize handle.
// The rectangle is the dragged tab's own slot, raised by kLiftedTabRise and slid to the pointer
// (clamped to the dock), so it stays the width of the tab it stands in for.
void BottomDockComponent::paintOverChildren(juce::Graphics& g) {
    if (!liftedTab_.has_value())
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    auto& button = buttonForTab(*liftedTab_);
    const auto& theme = lf->getTheme();
    const auto& c = theme.colors;
    const auto slot = button.getBounds();
    const int left = juce::jlimit(0, juce::jmax(0, getWidth() - slot.getWidth()), liftedLeft_);
    const auto tab = slot.withX(left).translated(0, -kLiftedTabRise).toFloat();

    // Cheap drop shadow, the same offset translucent copy AppLookAndFeel's fader cap uses.
    g.setColour(juce::Colours::black.withAlpha(0.35f * juce::jlimit(0.0f, 1.0f, theme.treatment.shadow)));
    g.fillRoundedRectangle(tab.translated(0.0f, 1.0f), kLiftedTabRadius);

    g.beginTransparencyLayer(kLiftedTabOpacity);
    g.setColour(c.surfaceHi);
    g.fillRoundedRectangle(tab, kLiftedTabRadius);
    g.setColour(c.accent);
    g.drawRoundedRectangle(tab.reduced(0.5f), kLiftedTabRadius - 0.5f, 1.0f);
    g.setColour(c.textPrimary);
    g.setFont(lf->getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), tab.toNearestInt().reduced(4, 0), juce::Justification::centred, 1);
    g.endTransparencyLayer();
}

void BottomDockComponent::lookAndFeelChanged() { refreshDetachButton(); }

} // namespace synth::ui
