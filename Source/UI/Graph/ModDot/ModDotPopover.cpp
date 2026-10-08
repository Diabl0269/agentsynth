#include "ModDotPopover.h"
#include "UI/Layout/PopupMotion.h"

#include "AudioEngine/AudioEngine.h"
#include "ModDotButton.h"
#include "ModDotController.h"
#include "ModDotMotion.h"
#include "ModSourceCatalog.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include <algorithm>
#include <set>

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
    , updater_(this)
    , controllerForClose_(&controller) {
    setComponentID("modDotPopover");
    setWantsKeyboardFocus(true); // a fallback target: keys still reach the panel when a control it held goes away
    setTitle(target_.paramName + " modulation");
    addAndMakeVisible(sourcesPage_);
    addChildComponent(addPage_);
    sourcesPage_.onAddSourceRequested = [this] { toggleList(); };
    sourcesPage_.onPickOnCanvasRequested = [this] { togglePick(); };
    sourcesPage_.onHeightChanged = [this] { layoutParts(); };
    addPage_.onCollapseRequested = [this] { closeList(); };
    addPage_.onPick = [this](const ModSourceItem& item) { pickSource(item); };
    addPage_.onPickNew = [this](const juce::String& typeName, int channel) { pickNewModule(typeName, channel); };
    addPage_.onHeightChanged = [this] { layoutParts(); };
    if (auto* dot = dynamic_cast<ModDotButton*>(anchor_.getComponent()))
        dot->setMenuOpen(true);
    layoutParts();
}

ModDotPopover::~ModDotPopover() {
    openAnim_.stop(updater_);
    if (controllerForClose_ != nullptr)
        controllerForClose_->popoverClosed(this);
    // A panel reopened on the same dot (a double-click) is already up by the time this one is deleted: leave the
    // dot's ring and the focus to it.
    if (controllerForClose_ != nullptr && controllerForClose_->getPopover() != nullptr)
        return;
    if (auto* dot = dynamic_cast<ModDotButton*>(anchor_.getComponent()))
        dot->setMenuOpen(false);
    // Focus goes back to the dot once the window is gone.
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

void ModDotPopover::syncFromGraph() {
    auto* node = editor_.getAudioEngine().getGraph().getNodeForId(card_);
    if (node == nullptr) {
        dismiss();
        return;
    }
    sourcesPage_.sync(/*fresh=*/false);
    if (listOpen_ || picker_ != nullptr)
        refreshChoicesIfChanged(false);
    ensureFocusInside(); // a removed row or a folded list must not leave the keyboard with nothing
}

void ModDotPopover::setMaxHeight(int height) {
    maxHeight_ = height;
    layoutParts();
}

int ModDotPopover::settledHeight(bool listOpen) const {
    return sourcesPage_.preferredHeight() + (listOpen ? addPage_.preferredHeight() : 0);
}

// The panel is the rows page with the list page stacked under it. The list page keeps its own height and the panel's
// edge reveals as much of it as the fold has reached, so unfolding is a growing panel, never a squashed list. The
// list's height cap is what screen room is left under the rows.
void ModDotPopover::layoutParts() {
    const int sourcesHeight = sourcesPage_.preferredHeight();
    addPage_.setMaxHeight(maxHeight_ > 0 ? juce::jmax(0, maxHeight_ - sourcesHeight) : 0);
    const int listHeight = addPage_.preferredHeight();
    sourcesPage_.setBounds(0, 0, ModDotPage::kWidth, sourcesHeight);
    addPage_.setBounds(0, sourcesHeight, ModDotPage::kWidth, listHeight);
    addPage_.setVisible(listAmount_ > 0.0f);
    const int total = sourcesHeight + juce::roundToInt((float)listHeight * listAmount_);
    if (total != getHeight() || getWidth() != ModDotPage::kWidth)
        setSize(ModDotPage::kWidth, total);
}

void ModDotPopover::resized() {
    sourcesPage_.setBounds(0, 0, getWidth(), sourcesPage_.getHeight());
    addPage_.setBounds(0, sourcesPage_.getHeight(), getWidth(), addPage_.getHeight());
}

void ModDotPopover::openList() { setListTarget(true); }
void ModDotPopover::closeList() { setListTarget(false); }
void ModDotPopover::toggleList() { setListTarget(!listOpen_); }

// Unfolds the list under the rows over 160 ms (easeOutCubic) and folds it back over 110 ms (easeInCubic); the panel's
// height follows frame by frame. At once when not on screen or under Reduce Motion.
void ModDotPopover::setListTarget(bool open) {
    if (listOpen_ == open)
        return;
    listOpen_ = open;
    sourcesPage_.splitButton().setLeftLit(open);
    const bool focusWasInList = addPage_.isParentOf(juce::Component::getCurrentlyFocusedComponent());
    if (open) {
        choiceSignature_.clear();
        refreshChoicesIfChanged(/*fresh=*/true);
        addPage_.reset();
        addPage_.setInterceptsMouseClicks(true, true);
    } else {
        addPage_.setInterceptsMouseClicks(false, false);
    }
    const float from = listAmount_;
    const float to = open ? 1.0f : 0.0f;
    const auto settle = [this, open, focusWasInList] {
        listAmount_ = open ? 1.0f : 0.0f;
        layoutParts();
        if (open)
            addPage_.focusEntry();
        else if (focusWasInList)
            sourcesPage_.addButton().grabKeyboardFocus();
    };
    if (!modDotMotionAllowed(*this)) {
        openAnim_.stop(updater_);
        settle();
        return;
    }
    if (open)
        addPage_.setVisible(true);
    openAnim_.start(
        updater_, open ? kOpenMs : kCloseMs, open ? easeOutCubic : easeInCubic,
        [this, from, to](float t) {
            listAmount_ = from + (to - from) * t;
            layoutParts();
        },
        settle);
    if (open)
        addPage_.focusEntry();
}

void ModDotPopover::startPick() {
    if (picker_ != nullptr)
        return;
    retiredPicker_.reset();
    refreshChoicesIfChanged(/*fresh=*/true);
    picker_ = std::make_unique<ModDotCanvasPicker>(
        editor_, [this](juce::AudioProcessorGraph::NodeID node) { return pickable_.count(node.uid) > 0; });
    picker_->onPicked = [this](juce::AudioProcessorGraph::NodeID node) { pickNode(node); };
    picker_->onEscape = [this] { stopPick(); };
    picker_->begin();
    sourcesPage_.splitButton().setRightLit(true);
    juce::AccessibilityHandler::postAnnouncement("Pick on canvas. Click a module to add it as a source. Escape stops.",
                                                 juce::AccessibilityHandler::AnnouncementPriority::medium);
}

// The layer is parked, not deleted: this can run from inside one of its own event handlers.
void ModDotPopover::stopPick() {
    sourcesPage_.splitButton().setRightLit(false);
    if (picker_ == nullptr)
        return;
    picker_->setVisible(false);
    if (auto* parent = picker_->getParentComponent())
        parent->removeChildComponent(picker_.get());
    retiredPicker_ = std::move(picker_);
}

void ModDotPopover::togglePick() {
    if (picker_ != nullptr)
        stopPick();
    else
        startPick();
}

// Every source the Mod Matrix offers minus the card itself, macro ports and hidden attenuverters, each with what it
// already moves, then (for a typed query) the module types the search can create.
std::vector<ModDotAddSourcePage::Choice> ModDotPopover::buildChoices(bool fresh) const {
    auto& graph = editor_.getAudioEngine().getGraph();
    const auto existing = knobModSources(editor_, card_, destChannel_, fresh);
    const auto targets = modSourceTargetCounts(editor_, fresh);
    std::vector<ModDotAddSourcePage::Choice> choices;
    for (auto& item : enumerateModSources(graph)) {
        if (item.node == card_ || editor_.getMacroController().nodeIsMacroPort(item.node))
            continue;
        if (auto* node = graph.getNodeForId(item.node);
            node != nullptr && dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr)
            continue;
        ModDotAddSourcePage::Choice choice;
        choice.added = std::any_of(existing.begin(), existing.end(), [&item](const KnobModSource& s) {
            return s.sourceNodeId == item.node && s.sourceChannel == item.channel;
        });
        if (const auto it = targets.find(item.itemId()); it != targets.end())
            choice.targets = it->second;
        choice.item = std::move(item);
        choices.push_back(std::move(choice));
    }
    for (const auto& type : newModuleSources()) {
        ModDotAddSourcePage::Choice choice;
        choice.item.moduleTitle = "New " + type.typeName;
        choice.item.channel = type.channel;
        choice.item.group = ModSourceGroup::NewModule;
        choice.newType = type.typeName;
        choices.push_back(std::move(choice));
    }
    return choices;
}

// Re-reads the list when what it shows changed (a source added elsewhere, a target count moved) and the nodes a
// canvas pick may land on; a list that is still the same keeps its rows, folds and focus.
void ModDotPopover::refreshChoicesIfChanged(bool fresh) {
    const auto choices = buildChoices(fresh);
    std::vector<int> signature;
    pickable_.clear();
    for (const auto& c : choices) {
        if (c.newType.isNotEmpty())
            continue;
        signature.push_back(c.item.itemId());
        signature.push_back(c.added ? -1 - c.targets : c.targets);
        if (!c.added)
            pickable_.insert(c.item.node.uid);
    }
    if (listOpen_ && signature != choiceSignature_)
        addPage_.setChoices(choices);
    choiceSignature_ = std::move(signature);
}

void ModDotPopover::pickNode(juce::AudioProcessorGraph::NodeID node) {
    for (const auto& choice : buildChoices(true))
        if (choice.item.node == node && choice.newType.isEmpty() && !choice.added) {
            pickSource(choice.item);
            return;
        }
}

// One undo step: the cable, its attenuverter at the new-source depth and any macro ports it crosses.
void ModDotPopover::pickSource(const ModSourceItem& item) {
    finishPick(editor_.connectModulationSource(item.node, item.channel, card_, destChannel_, kModDotNewSourceDepth));
}

// One undo step: the new module beside the card, its cable and depth (and a macro join when the card is in one).
void ModDotPopover::pickNewModule(const juce::String& typeName, int channel) {
    finishPick(editor_.addModulationSourceModule(typeName, channel, card_, destChannel_, kModDotNewSourceDepth));
}

// After a source was added: the list folds, a canvas pick ends, and the new row grows in already selected, so a
// later drag on the dot edits it.
void ModDotPopover::finishPick(juce::AudioProcessorGraph::NodeID attenuverter) {
    if (attenuverter.uid != 0)
        controller_.setLastChosen(card_, destChannel_, attenuverter);
    stopPick();
    closeList();
    sourcesPage_.sync();
    if (attenuverter.uid != 0)
        sourcesPage_.select(attenuverter);
}

void ModDotPopover::dismiss() {
    stopTimer(); // a leaving panel must not go back for the keyboard
    if (onDismiss)
        onDismiss();
}

bool ModDotPopover::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (picker_ != nullptr) {
        stopPick();
        return true;
    }
    if (listOpen_) {
        addPage_.stepBack();
        return true;
    }
    dismiss();
    return true;
}

bool ModDotPopover::focusIsInside() const {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && (focused == this || isParentOf(focused));
}

void ModDotPopover::ensureFocusInside() {
    auto* peer = getPeer();
    if (peer == nullptr || !isShowing() || focusIsInside())
        return;
    // A click on the canvas moves the keys to the app window while the panel fades out; pulling the panel's window
    // to the front now would re-show it mid-fade (the double flicker on click-away).
    if (auto* window = getTopLevelComponent(); window != nullptr && PopupMotion::isDismissing(*window))
        return;
    if (!peer->isFocused()) {
        if (auto* window = getTopLevelComponent())
            window->toFront(true); // the panel's window must be the key one before a control can take the keys
        return;
    }
    if (listOpen_)
        addPage_.focusEntry();
    else
        sourcesPage_.focusEntry();
}

void ModDotPopover::timerCallback() {
    ensureFocusInside();
    if (focusIsInside() || ++focusTries_ > 60)
        stopTimer();
}

// The panel's window only becomes the key window a moment after it is shown, so focus is taken as soon as it can
// give it.
void ModDotPopover::parentHierarchyChanged() {
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
