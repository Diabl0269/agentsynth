// Concern: BottomDockComponent tab strip -- tab buttons and layout, drag-to-reorder, painting, keyboard and
// screen-reader access.
#include "BottomDockComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReadOnlyTextValue.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Layout/TabStripKeys.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <cmath>

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

const char* BottomDockComponent::actionIdForTab(Tab tab) noexcept {
    switch (tab) {
    case Tab::Mixer:
        return "toggleMixerPanel";
    case Tab::MidiRemote:
        return "toggleMidiRemotePanel";
    case Tab::Canvas:
        return "showCanvas";
    case Tab::Timeline:
        break;
    }
    return "toggleTimelinePanel";
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
    for (Tab t : tabModel_.getOrder()) {
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
    for (Tab t : tabModel_.getOrder())
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

// The order is committed only here, on release: while dragging, the strip order never changes and the
// neighbours merely glide aside. The main view and detached tabs keep their slots in the order; only
// the offered tabs are permuted among the positions they occupy.
void BottomDockComponent::commitTabDrag() {
    const auto& tabOrder = tabModel_.getOrder();
    std::vector<Tab> offered;
    std::vector<size_t> positions;
    for (size_t i = 0; i < tabOrder.size(); ++i)
        if (isTabOfferedInStrip(tabOrder[i])) {
            offered.push_back(tabOrder[i]);
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
        if (tabOrder[positions[i]] != tab) {
            tabModel_.setSlot(positions[i], tab);
            changed = true;
        }
    }
    // Each view keeps its own shortcut key, so a reorder changes only the strip.
    if (changed)
        persistTabOrder();

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

// Nothing is committed: the strip order was never touched, so the animator only has to glide everything
// back to the pickup layout, and nothing is persisted.
void BottomDockComponent::cancelTabDrag() {
    reorderCancelKey_.disarm();
    if (!reorder_.isReordering())
        return;
    tabDragCancelled_ = true;
    reorder_.abort();
    startReorderFramesIfNeeded();
    onReorderFrame();
}

// Splits tabButtonsArea_ between the tabs currently offered (isTabOfferedInStrip -- skips a Mixer
// disabled by placement AND any detached tab); the last one absorbs the rounding remainder. A tab
// not offered is hidden outright -- it has left the strip.
void BottomDockComponent::layoutTabButtons() {
    auto tabStrip = tabButtonsArea_;
    std::vector<Tab> offered;
    for (Tab t : tabModel_.getOrder())
        if (isTabOfferedInStrip(t))
            offered.push_back(t);
    for (size_t i = 0; i < offered.size(); ++i) {
        auto& button = buttonForTab(offered[i]);
        button.setVisible(true);
        button.setBounds(i + 1 == offered.size()
                             ? tabStrip
                             : tabStrip.removeFromLeft(tabStrip.getWidth() / (int)(offered.size() - i)));
    }
    for (Tab t : tabModel_.getOrder())
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

    // Whatever's left splits between the tabs currently offered, in the user's
    // (drag-to-reorder) strip order -- see layoutTabButtons(). A reorder in flight then shifts the
    // buttons to their animated places.
    tabButtonsArea_ = tabStrip;
    stripFocus_.setBounds(tabButtonsArea_);
    layoutTabButtons();
    applyReorderOffsets();

    // A host parented elsewhere (the main-area holder, the Mixer's Own-panel strip) is laid out by that
    // parent; sizing it to the dock here would leave it at the wrong size until the parent's next layout.
    for (auto* host : {&timelineHost_, &mixerHost_, &midiRemoteHost_})
        if (host->getParentComponent() == this)
            host->setBounds(bounds);
}

std::vector<BottomDockComponent::StripTab> BottomDockComponent::getStripTabs() {
    std::vector<StripTab> tabs;
    for (Tab t : tabModel_.getOrder())
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

// ---- Keyboard and screen-reader access to the tab strip ----

const char* BottomDockComponent::nameForTab(Tab tab) noexcept {
    switch (tab) {
    case Tab::Mixer:
        return "Mixer";
    case Tab::MidiRemote:
        return "Controllers";
    case Tab::Canvas:
        return "Canvas";
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
    case Tab::Canvas:
        return nullptr; // no canvas panel lives in the dock yet
    case Tab::Timeline:
        break;
    }
    return &timelinePanel_;
}

// Plain Left/Right/Home/End walk the tabs that are offered, in the user's tab order, and stop at the
// ends (tabStripKeyTarget); Return hands focus to the selected tab's panel root, and Down does the same
// then takes the panel's own first Down step (on the Timeline: "+ Track"), since the panel sits below
// the strip. Everything else (the Cmd+digit tab keys included) is left for the app-wide shortcuts.
bool BottomDockComponent::handleTabStripKey(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    std::vector<Tab> offered;
    for (Tab t : tabModel_.getOrder())
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
    for (Tab t : tabModel_.getOrder()) {
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
