#include "ModDotPopover.h"

#include "AudioEngine/AudioEngine.h"
#include "ModDotButton.h"
#include "ModDotController.h"
#include "ModDotMotion.h"
#include "ModDotPalette.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Layout/PopupMotion.h"
#include <algorithm>

namespace synth::ui {

ModDotPopover::ModDotPopover(GraphEditor& editor, ModDotController& controller, juce::AudioProcessorGraph::NodeID card,
                             int destChannel, juce::Component& anchor)
    : editor_(editor)
    , controller_(controller)
    , card_(card)
    , destChannel_(destChannel)
    , anchor_(&anchor)
    , target_(knobModTarget(editor, card, destChannel))
    , sourcesPage_(editor, controller, card, destChannel, target_)
    , addPage_(target_.paramName)
    , reveal_(*this)
    , updater_(this)
    , controllerForClose_(&controller) {
    setComponentID("modDotPopover");
    setWantsKeyboardFocus(true); // a fallback target: keys still reach the panel when a control it held goes away
    setTitle(target_.paramName + " modulation");
    addAndMakeVisible(sourcesPage_);
    addChildComponent(addPage_);
    sourcesPage_.onAddSourceRequested = [this] { showAddSource(); };
    sourcesPage_.onHeightChanged = [this] { pageHeightChanged(Page::Sources); };
    addPage_.onBackRequested = [this] { showSources(); };
    addPage_.onPick = [this](const ModSourceItem& item) { pickSource(item); };
    addPage_.onHeightChanged = [this] { pageHeightChanged(Page::AddSource); };
    if (auto* dot = dynamic_cast<ModDotButton*>(anchor_.getComponent()))
        dot->setMenuOpen(true);
    setSize(ModDotPage::kWidth, sourcesPage_.preferredHeight());
}

ModDotPopover::~ModDotPopover() {
    pageAnim_.stop(updater_);
    if (controllerForClose_ != nullptr)
        controllerForClose_->popoverClosed(this);
    // A panel reopened on the same dot (a double-click) is already up by the time this one is deleted: leave the
    // dot's ring and the focus to it.
    if (controllerForClose_ != nullptr && controllerForClose_->getPopover() != nullptr)
        return;
    if (auto* dot = dynamic_cast<ModDotButton*>(anchor_.getComponent()))
        dot->setMenuOpen(false);
    // Focus goes back to the dot once the callout is gone.
    juce::Component::SafePointer<juce::Component> anchor = anchor_;
    juce::MessageManager::callAsync([anchor] {
        if (anchor != nullptr && anchor->isShowing())
            anchor->grabKeyboardFocus();
    });
}

void ModDotPopover::setRemoveHighlighted(bool on) {
    if (sourcesPage_.isRemoveHighlighted() == on)
        return;
    sourcesPage_.setRemoveHighlighted(on);
    if (on)
        juce::AccessibilityHandler::postAnnouncement("Choose a source to remove",
                                                     juce::AccessibilityHandler::AnnouncementPriority::medium);
}

ModDotPage& ModDotPopover::pageComponent(Page page) {
    return page == Page::Sources ? static_cast<ModDotPage&>(sourcesPage_) : static_cast<ModDotPage&>(addPage_);
}

void ModDotPopover::syncFromGraph() {
    auto* node = editor_.getAudioEngine().getGraph().getNodeForId(card_);
    if (node == nullptr) {
        dismiss();
        return;
    }
    sourcesPage_.sync(/*fresh=*/false);
    ensureFocusInside(); // a removed row or a switched page must not leave the keyboard with nothing
}

int ModDotPopover::roomOnSide(juce::Rectangle<int> box, juce::Rectangle<int> dot, juce::Rectangle<int> area,
                              int borderSize) {
    const int chrome = borderSize + 16; // the callout's border and its (default 16 px) arrow
    if (box.getCentreY() > dot.getBottom())
        return area.getBottom() - dot.getBottom() - chrome;
    if (box.getCentreY() < dot.getY())
        return dot.getY() - area.getY() - chrome;
    return area.getHeight() - 2 * borderSize; // beside the dot: the whole height
}

void ModDotPopover::keepSideOf(const juce::CallOutBox& box, juce::Rectangle<int> dot, juce::Rectangle<int> area) {
    // The box's bounds are in the area's frame (screen, or the parent a test gives it).
    setMaxHeight(juce::jmax(0, roomOnSide(box.getBounds(), dot, area, box.getBorderSize())));
}

void ModDotPopover::setMaxHeight(int height) {
    addPage_.setMaxHeight(height);
    pageHeightChanged(Page::AddSource);
}

// A page's content moved (a row grew, a group folded): the page follows its content and, when it is the one shown,
// so does the panel.
void ModDotPopover::pageHeightChanged(Page which) {
    auto& page = pageComponent(which);
    page.setSize(ModDotPage::kWidth, page.preferredHeight());
    if (page_ == which && !pageAnim_.isRunning())
        applyHeight(page.getHeight());
}

void ModDotPopover::applyHeight(int height) {
    if (height != getHeight())
        setSize(getWidth(), height);
}

void ModDotPopover::resized() {
    sourcesPage_.setBounds(0, 0, getWidth(), sourcesPage_.getHeight());
    addPage_.setBounds(0, 0, getWidth(), addPage_.getHeight());
}

void ModDotPopover::showSources() {
    sourcesPage_.sync();
    switchTo(Page::Sources);
}

void ModDotPopover::showAddSource() {
    addPage_.setChoices(buildChoices());
    addPage_.reset();
    switchTo(Page::AddSource);
}

// Cross-fade the two pages over 110 ms while the panel's height settles over 160 ms; the pages keep their own
// heights, so the fading page is simply revealed or cut off by the panel's edge. At once when not on screen.
void ModDotPopover::switchTo(Page page) {
    if (page == page_ && !pageAnim_.isRunning())
        return;
    auto& in = pageComponent(page);
    auto& out = pageComponent(page == Page::Sources ? Page::AddSource : Page::Sources);
    const int fromHeight = getHeight();
    page_ = page;
    in.setSize(ModDotPage::kWidth, in.preferredHeight());
    const int toHeight = in.preferredHeight();
    in.setVisible(true);
    out.setInterceptsMouseClicks(false, false);
    in.setInterceptsMouseClicks(true, true);

    const auto finish = [this, &in, &out] {
        out.setVisible(false);
        out.setAlpha(1.0f);
        in.setAlpha(1.0f);
        applyHeight(in.preferredHeight());
        in.focusEntry();
    };
    if (!modDotMotionAllowed(*this)) {
        pageAnim_.stop(updater_);
        finish();
        return;
    }
    in.setAlpha(0.0f);
    const double fadeShare = kFadeMs / kSettleMs;
    pageAnim_.start(
        updater_, kSettleMs, [](float t) { return t; },
        [this, &in, &out, fromHeight, toHeight, fadeShare](float t) {
            const float fade = juce::jlimit(0.0f, 1.0f, (float)(t / fadeShare));
            in.setAlpha(fade);
            out.setAlpha(1.0f - fade);
            applyHeight(fromHeight + juce::roundToInt((float)(toHeight - fromHeight) * easeOutCubic(t)));
        },
        [finish] { finish(); });
}

std::vector<ModDotAddSourcePage::Choice> ModDotPopover::buildChoices() const {
    auto& graph = editor_.getAudioEngine().getGraph();
    const auto existing = knobModSources(editor_, card_, destChannel_, true);
    std::vector<ModDotAddSourcePage::Choice> choices;
    for (auto& item : enumerateModSources(graph)) {
        // The card itself, macro ports (looked through) and the hidden attenuverters ("Mod Slot") of other
        // routings are not sources to offer here.
        if (item.node == card_ || editor_.getMacroController().nodeIsMacroPort(item.node))
            continue;
        if (auto* node = graph.getNodeForId(item.node);
            node != nullptr && dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            continue;
        ModDotAddSourcePage::Choice choice;
        choice.added = std::any_of(existing.begin(), existing.end(), [&item](const KnobModSource& s) {
            return s.sourceNodeId == item.node && s.sourceChannel == item.channel;
        });
        choice.item = std::move(item);
        choices.push_back(std::move(choice));
    }
    return choices;
}

// One undo step: the cable, its attenuverter at the new-source depth and any macro ports it crosses. Back on the
// sources page the new row grows in already selected, and a later drag on the dot edits it.
void ModDotPopover::pickSource(const ModSourceItem& item) {
    const auto atten =
        editor_.connectModulationSource(item.node, item.channel, card_, destChannel_, kModDotNewSourceDepth);
    if (atten.uid != 0)
        controller_.setLastChosen(card_, destChannel_, atten);
    switchTo(Page::Sources); // first, so the new row grows in on a page that is showing
    sourcesPage_.sync();
    if (atten.uid != 0)
        sourcesPage_.select(atten);
}

void ModDotPopover::dismiss() {
    if (onDismiss) {
        onDismiss();
        return;
    }
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        synth::ui::PopupMotion::dismissCallOut(*box);
}

bool ModDotPopover::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (page_ == Page::AddSource) {
        addPage_.stepBack();
        return true;
    }
    dismiss();
    return true;
}

void ModDotPopover::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(p.panel);
    g.fillRoundedRectangle(bounds, p.radius);
    g.setColour(p.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), p.radius, 1.0f);
}

bool ModDotPopover::focusIsInside() const {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && (focused == this || isParentOf(focused));
}

void ModDotPopover::ensureFocusInside() {
    auto* peer = getPeer();
    if (peer == nullptr || !isShowing() || focusIsInside())
        return;
    if (!peer->isFocused()) {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->toFront(true); // the callout's window must be the key one before a control can take the keys
        return;
    }
    pageComponent(page_).focusEntry();
}

void ModDotPopover::timerCallback() {
    ensureFocusInside();
    if (focusIsInside() || ++focusTries_ > 60)
        stopTimer();
}

// A CallOutBox attaches its content after construction and only becomes the key window a moment after it is shown,
// so the entrance starts once this has a parent and the focus is taken as soon as the window can give it.
void ModDotPopover::parentHierarchyChanged() {
    reveal_.startIfInCallout();
    if (getParentComponent() != nullptr && !focusIsInside())
        startTimerHz(30);
}

void ModDotPopover::visibilityChanged() {
    if (isShowing() && !focusIsInside() && !isTimerRunning()) {
        focusTries_ = 0;
        startTimerHz(30);
    }
}

} // namespace synth::ui
