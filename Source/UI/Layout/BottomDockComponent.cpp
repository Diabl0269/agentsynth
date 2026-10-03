// Concern: BottomDockComponent's tab strip, persistence and layout. Both panels'
// detach-to-window hosts (docs/mixer/panel.md) and the tab strip's own icon-only detach
// button live here too, with a user-reorderable tab order and "a detached tab leaves the
// strip, the active tab falls back to the next one" behaviour.
#include "BottomDockComponent.h"

#include "AudioEngine/AudioEngine.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReadOnlyTextValue.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Layout/TabStripKeys.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace synth::ui {

namespace {
// How far above its slot a dragged tab is drawn. The tab row starts PanelResizeHandle::kHeight (5)
// px below the dock's top edge, so a 4 px lift keeps the lifted tab and its 1 px shadow inside the
// dock's own bounds.
constexpr int kLiftedTabRise = 4;
constexpr float kLiftedTabRadius = 4.0f;
constexpr float kLiftedTabOpacity = 0.95f;

// A tab as a screen reader meets it: the shared role/name/selected data, plus a press that selects it.
// The state is read live, since the handler outlives every tab switch.
class DockTabHandler : public juce::AccessibilityHandler {
public:
    DockTabHandler(juce::Button& button, std::function<BottomDockComponent::TabAccessibility()> describe)
        : juce::AccessibilityHandler(button, describe().role,
                                     juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                                            [&button] {
                                                                                if (button.onClick)
                                                                                    button.onClick();
                                                                            }))
        , describe_(std::move(describe)) {}

    juce::AccessibleState getCurrentState() const override {
        const auto state = juce::AccessibilityHandler::getCurrentState().withSelectable().withCheckable();
        return describe_().selected ? state.withSelected().withChecked() : state;
    }

private:
    std::function<BottomDockComponent::TabAccessibility()> describe_;
};
} // namespace

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
    , shortcutManager_(shortcutManager) {
    addAndMakeVisible(stripFocus_);
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
    // The saved order and the Cmd+digit bindings are stored separately, so they can disagree (an order
    // that was never saved next to bindings that were, or the reverse); the order wins, under the same
    // "only while all three still use the default Cmd+digit set" guard a drag reorder applies.
    permuteShortcutKeysForNewOrder();
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

void BottomDockComponent::setOnBuildTrackColourPicker(
    std::function<std::unique_ptr<synth::ui::ColourPickerPopup>(synth::TrackId)> callback) {
    mixer_->buildTrackColourPicker = std::move(callback);
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

BottomDockComponent::~BottomDockComponent() = default;

void BottomDockComponent::rebuildMixer() {
    mixer_->rebuild();
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

void BottomDockComponent::rebuildIfUnboundMixerViews() {
    mixer_->rebuildIfUnbound();
    mixerMirror_.rebuildIfUnboundIfOpen();
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
    mixerMirror_.open(*mixer_, lf, shortcutManager_, mixerHost_.onAppShortcutFallback,
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
        mixer_->setVisible(mixerActive);
    if (!midiRemoteHost_.isDetached())
        midiRemotePanel_.setVisible(midiRemoteActive);
    timelineTabButton_.setToggleState(timelineActive, juce::dontSendNotification);
    mixerTabButton_.setToggleState(mixerActive, juce::dontSendNotification);
    midiRemoteTabButton_.setToggleState(midiRemoteActive, juce::dontSendNotification);
    const std::array<juce::Component*, 4> announced{&timelineTabButton_, &mixerTabButton_, &midiRemoteTabButton_,
                                                    &stripFocus_};
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
    if (digits == std::vector<int>{1, 2, 3})
        return; // already keyed to tabOrder_: nothing to write
    for (size_t i = 0; i < tabOrder_.size(); ++i)
        shortcutManager_->setBinding(actionIdForTab(tabOrder_[i]),
                                     juce::KeyPress('1' + (int)i, juce::ModifierKeys::commandModifier, 0));
    shortcutManager_->saveToProperties();
}

// The pointer's x in dock coordinates, read straight from the event: JUCE re-derives
// getMouseDownPosition() in the event component's CURRENT local space, and the pressed button moves
// under the pointer as tabs shift, so only a value captured once (and converted here) stays true.
float BottomDockComponent::pointerXInDock(const juce::MouseEvent& e) { return e.getEventRelativeTo(this).position.x; }

// Captures everything the gesture needs at press time, while the tabs are still where the user
// grabbed them: the offered tabs and their extents, and the grab offset inside the pressed tab.
void BottomDockComponent::beginTabDrag(Tab tab, const juce::MouseEvent& e) {
    reorderFrames_.stop();
    reorderCancelKey_.disarm();
    tabDragCancelled_ = false;
    liftedTab_.reset();
    reorderTabs_.clear();
    std::vector<ReorderDragAnimator::Slot> slots;
    int pressedKey = -1;
    for (Tab t : tabOrder_) {
        if (!isTabOfferedInStrip(t))
            continue;
        if (t == tab)
            pressedKey = static_cast<int>(reorderTabs_.size());
        const auto bounds = buttonForTab(t).getBounds();
        reorderTabs_.push_back(t);
        slots.push_back({static_cast<float>(bounds.getX()), static_cast<float>(bounds.getWidth())});
    }
    if (pressedKey < 0) {
        reorder_.cancel();
        return;
    }
    const float pointer = pointerXInDock(e);
    reorder_.begin(slots, pressedKey, pointer - slots[static_cast<size_t>(pressedKey)].start, pointer, isShowing());
}

void BottomDockComponent::dragTab(const juce::MouseEvent& e) {
    if (!reorder_.dragTo(pointerXInDock(e)))
        return;
    liftedTab_ = reorderTabs_[static_cast<size_t>(reorder_.getDraggedKey())];
    if (!reorderCancelKey_.isArmed())
        reorderCancelKey_.arm(*this, [this] { cancelTabDrag(); });
    startReorderFramesIfNeeded();
    applyReorderOffsets();
}

// A frame pump runs only while a tween is in flight; once the tabs are at rest there is no timer
// and no repaint until the next pointer event.
void BottomDockComponent::startReorderFramesIfNeeded() {
    if (reorder_.getTweenGeneration() == reorderGenerationSeen_)
        return;
    reorderGenerationSeen_ = reorder_.getTweenGeneration();
    if (reorder_.needsFrames())
        reorderFrames_.run(ReorderDragAnimator::kMakeRoomMs + 20.0, [this] { onReorderFrame(); });
}

void BottomDockComponent::onReorderFrame() {
    reorder_.finishIfSettled();
    if (reorder_.isReordering()) {
        applyReorderOffsets();
        return;
    }
    liftedTab_.reset();
    layoutTabButtons();
    repaint(0, 0, getWidth(), kTabStripHeight);
}

void BottomDockComponent::applyReorderOffsets() {
    if (!reorder_.isReordering())
        return;
    std::vector<Tab> offered;
    for (Tab t : tabOrder_)
        if (isTabOfferedInStrip(t))
            offered.push_back(t);
    if (offered.size() != reorderTabs_.size()) { // a tab was detached or redocked mid-gesture
        reorder_.cancel();
        liftedTab_.reset();
        layoutTabButtons();
        return;
    }
    for (size_t key = 0; key < reorderTabs_.size(); ++key) {
        auto& button = buttonForTab(reorderTabs_[key]);
        button.setTopLeftPosition(static_cast<int>(std::lround(reorder_.getLayoutStart(static_cast<int>(key)))),
                                  button.getY());
    }
    repaint(0, 0, getWidth(), kTabStripHeight);
}

// The order is committed only here, on release: while dragging, tabOrder_ never changes and the
// neighbours merely glide aside. Detached tabs keep their slots in tabOrder_; only the offered
// tabs are permuted among the positions they occupy.
void BottomDockComponent::commitTabDrag() {
    std::vector<Tab> offered;
    std::vector<size_t> positions;
    for (size_t i = 0; i < tabOrder_.size(); ++i)
        if (isTabOfferedInStrip(tabOrder_[i])) {
            offered.push_back(tabOrder_[i]);
            positions.push_back(i);
        }
    if (offered != reorderTabs_) { // a tab was detached mid-drag: abandon the gesture
        reorder_.cancel();
        liftedTab_.reset();
        resized();
        return;
    }

    const auto order = reorder_.getNewOrder();
    bool changed = false;
    for (size_t i = 0; i < positions.size(); ++i) {
        const Tab tab = reorderTabs_[static_cast<size_t>(order[i])];
        if (tabOrder_[positions[i]] != tab) {
            tabOrder_[positions[i]] = tab;
            changed = true;
        }
    }
    if (changed) {
        persistTabOrder();
        permuteShortcutKeysForNewOrder();
    }

    layoutTabButtons();
    std::vector<float> finalStarts;
    for (Tab t : reorderTabs_)
        finalStarts.push_back(static_cast<float>(buttonForTab(t).getX()));
    reorder_.release(finalStarts);
    if (!reorder_.isReordering())
        liftedTab_.reset();
    startReorderFramesIfNeeded();
    applyReorderOffsets();
    repaint(0, 0, getWidth(), kTabStripHeight);
}

// A cancelled gesture is over already; its mouse-up only has to swallow the click.
bool BottomDockComponent::endTabDrag() {
    reorderCancelKey_.disarm();
    if (tabDragCancelled_) {
        tabDragCancelled_ = false;
        return true;
    }
    if (!liftedTab_.has_value()) {
        reorder_.cancel();
        return false;
    }
    commitTabDrag();
    return true;
}

// Nothing is committed: tabOrder_ was never touched, so the animator only has to glide everything
// back to the pickup layout, and no persistence or key re-binding happens.
void BottomDockComponent::cancelTabDrag() {
    reorderCancelKey_.disarm();
    if (!reorder_.isReordering())
        return;
    tabDragCancelled_ = true;
    reorder_.abort();
    startReorderFramesIfNeeded();
    onReorderFrame();
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

// Splits tabButtonsArea_ between the tabs currently offered (isTabOfferedInStrip -- skips a Mixer
// disabled by placement AND any detached tab); the last one absorbs the rounding remainder. A tab
// not offered is hidden outright -- it has left the strip.
void BottomDockComponent::layoutTabButtons() {
    auto tabStrip = tabButtonsArea_;
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
}

void BottomDockComponent::resized() {
    // The grab strip runs the dock's full width along its top edge, OVERLAPPING the tab strip: the
    // strip keeps its full kTabStripHeight (the content below never moves), but its buttons are
    // laid out below the handle so a resize grab never lands on one.
    resizeHandle_.setBounds(0, 0, getWidth(), PanelResizeHandle::kHeight);

    auto bounds = getLocalBounds();
    auto tabStrip = bounds.removeFromTop(kTabStripHeight).withTrimmedTop(PanelResizeHandle::kHeight);
    // Rightmost: the detach button (always present, acts on whichever tab is active).
    detachButton_.setBounds(tabStrip.removeFromRight(kTabStripHeight));

    // Whatever's left splits between the tabs currently offered, in tabOrder_'s user-controlled
    // (drag-to-reorder) order -- see layoutTabButtons(). A reorder in flight then shifts the
    // buttons to their animated places.
    tabButtonsArea_ = tabStrip;
    stripFocus_.setBounds(tabButtonsArea_);
    layoutTabButtons();
    applyReorderOffsets();

    timelineHost_.setBounds(bounds);
    // In the Own-panel placement the host lives in MixerPlacementController's strip, which lays it
    // out itself; sizing it to the dock here would leave it at the wrong height until that strip's
    // next layout pass.
    if (mixerHost_.getParentComponent() == this)
        mixerHost_.setBounds(bounds);
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
// The rectangle is the dragged tab's own width, raised by kLiftedTabRise and placed where the
// animator says the dragged item is: at the pointer minus the grab offset while dragging, gliding
// into its slot after release. The raise and the shadow/border strength ease with getLift().
void BottomDockComponent::paintOverChildren(juce::Graphics& g) {
    paintTabFocusRing(g);
    if (!liftedTab_.has_value() || !reorder_.isReordering())
        return;
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;
    auto& button = buttonForTab(*liftedTab_);
    const auto& c = lf->getTheme().colors;
    const float lift = reorder_.getLift();
    const auto tab = button.getBounds()
                         .withX(static_cast<int>(std::lround(reorder_.getDraggedStart())))
                         .toFloat()
                         .translated(0.0f, -kLiftedTabRise * lift);

    // The shadow, raised fill and accent border are the one look every reorder list lifts its row with
    // (it insets its body by 1 px, so it is given the tab grown by 0.5 px to land the outline on the tab's edge).
    g.beginTransparencyLayer(kLiftedTabOpacity);
    paintReorderLift(g, tab.expanded(0.5f), lift, c.surfaceHi, c.accent);
    g.setColour(c.textPrimary);
    g.setFont(lf->getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), tab.toNearestInt().reduced(4, 0), juce::Justification::centred, 1);
    g.endTransparencyLayer();
}

void BottomDockComponent::lookAndFeelChanged() { refreshDetachButton(); }

// ---- Keyboard and screen-reader access to the tab strip ----

const char* BottomDockComponent::nameForTab(Tab tab) noexcept {
    switch (tab) {
    case Tab::Mixer:
        return "Mixer";
    case Tab::MidiRemote:
        return "Controllers";
    case Tab::Timeline:
        break;
    }
    return "Timeline";
}

BottomDockComponent::TabAccessibility BottomDockComponent::describeTab(Tab tab, Tab activeTab) {
    return {nameForTab(tab), juce::AccessibilityRole::radioButton, tab == activeTab};
}

std::unique_ptr<juce::AccessibilityHandler> BottomDockComponent::DockTabButton::createAccessibilityHandler() {
    return std::make_unique<DockTabHandler>(*this, [this] { return describeTab(tab_, owner_.activeTab_); });
}

BottomDockComponent::TabStripFocus::TabStripFocus(BottomDockComponent& owner)
    : owner_(owner) {
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(false, false);
    setTitle("Panel tabs");
}

// Role group with the selected tab as its value, so focus landing on the strip reads "Panel tabs,
// Mixer"; the tabs themselves are child elements.
std::unique_ptr<juce::AccessibilityHandler> BottomDockComponent::TabStripFocus::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ReadOnlyTextValue>(
            [this] { return juce::String(owner_.hasAnyVisibleTab() ? nameForTab(owner_.activeTab_) : ""); })});
}

juce::Component* BottomDockComponent::getActivePanelRoot() noexcept {
    if (!isTabOfferedInStrip(activeTab_))
        return nullptr;
    switch (activeTab_) {
    case Tab::Mixer:
        return mixer_.get();
    case Tab::MidiRemote:
        return &midiRemotePanel_;
    case Tab::Timeline:
        break;
    }
    return &timelinePanel_;
}

// Plain Left/Right/Home/End walk the tabs that are offered, in the user's tab order, and stop at the
// ends (tabStripKeyTarget); Return hands focus to the selected tab's panel root, and Down does the same
// then takes the panel's own first Down step (on the Timeline: "+ Track"), since the panel sits below
// the strip. Everything else (Cmd+1/2/3 included) is left for the app-wide shortcuts.
bool BottomDockComponent::handleTabStripKey(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    std::vector<Tab> offered;
    for (Tab t : tabOrder_)
        if (isTabOfferedInStrip(t))
            offered.push_back(t);
    if (offered.empty())
        return false;
    const int current = (int)(std::find(offered.begin(), offered.end(), activeTab_) - offered.begin());

    if (const auto target = tabStripKeyTarget(key, current, (int)offered.size())) {
        selectTabAt(*target);
        return true;
    }
    const bool down = key.isKeyCode(juce::KeyPress::downKey);
    if (!down && !key.isKeyCode(juce::KeyPress::returnKey))
        return false;
    if (auto* panel = getActivePanelRoot()) {
        if (panelFocusHook_)
            panelFocusHook_(*panel);
        else
            panel->grabKeyboardFocus();
        if (down)
            if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
                focused->keyPressed(key);
    }
    return true;
}

void BottomDockComponent::selectTabAt(int offeredIndex) {
    int index = 0;
    for (Tab t : tabOrder_) {
        if (!isTabOfferedInStrip(t))
            continue;
        if (index++ == offeredIndex) {
            setActiveTab(t);
            return;
        }
    }
}

void BottomDockComponent::paintTabFocusRing(juce::Graphics& g) {
    if (!isTabOfferedInStrip(activeTab_) || liftedTab_.has_value())
        return;
    synth::ui::paintFocusRing(g, buttonForTab(activeTab_).getBounds().toFloat(), stripFocus_, 3.0f);
}

} // namespace synth::ui
