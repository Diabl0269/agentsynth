// ModuleComponentHostedPluginCard.cpp -- the Hosted Plugin card body: the Open Editor / Edit Layout...
// chrome, and the knobs, toggles and choice combos the resolved layout puts on the card, each bound live
// to its hosted parameter through a HostedParameterAttachment. ModuleComponent is declared in
// ModuleComponent.h; this is the per-type unit next to ModuleComponentEQCard.cpp and
// ModuleComponentEnvelopeCard.cpp, and it takes the HostedPlugin branch OUT of createControls(), which is
// at the function-size ratchet ceiling. See docs/control/plugin-card-layout.md#card-rendering-as-built-fro128.
#include "ModuleComponentHostedPluginCard.h"
#include "AudioEngine/AudioEngine.h"
#include "CardKnobSlider.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/CardLayout.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardLayoutEditor/HostedCardLayoutSource.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/PluginKnobPicker/HostedParameterCardMenu.h"
#include "UI/Graph/PluginKnobPicker/PluginKnobPickerComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;
using synth::ui::HostedParameterAttachment;

namespace {

// Same rotary look as the generic auto-UI knob: label above, readout below.
void styleHostedKnob(juce::Slider& slider) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
}

juce::String slotText(const synth::ResolvedCardSlot& resolved) {
    if (resolved.slot.label.has_value() && resolved.slot.label->isNotEmpty())
        return *resolved.slot.label;
    return resolved.param->getName(100);
}

// A Choice slot needs at least two entries to be a combo; anything less is drawn as a knob instead.
synth::CardSlotKind resolvedKind(const synth::ResolvedCardSlot& resolved) {
    auto kind = synth::effectiveSlotKind(resolved.slot, resolved.param);
    if (kind == synth::CardSlotKind::Choice && HostedParameterAttachment::choiceStrings(*resolved.param).size() < 2)
        return synth::CardSlotKind::Knob;
    return kind == synth::CardSlotKind::Auto ? synth::CardSlotKind::Knob : kind;
}

constexpr float kHostedStatusFontSize = 12.0f;
constexpr int kHostedStatusLineHeight = 15; // fits one line of the font above; the label wraps and ellipsises at 2
constexpr int kHostedStatusLines = 2;

} // namespace

// ---- HostedCardBinding ------------------------------------------------------------------------------------

// Registers everything the card must follow. module.onCardLayoutChanged is a single slot the card owns
// (nothing else sets it); the observer and the store listener are multi-listener and are removed in
// shutdown(), which detachFromProcessor() always reaches before the card is destroyed.
ModuleComponent::HostedCardBinding::HostedCardBinding(ModuleComponent& card, synth::HostedPluginModule& module,
                                                      synth::PluginCardLayoutStore* store)
    : card_(card)
    , module_(&module)
    , store_(store) {
    module.addInstanceObserver(this);
    if (store_ != nullptr)
        store_->addListener(this);
    module.onCardLayoutChanged = [this] {
        card_.rebuildHostedPluginCard();
        card_.relayoutHostedPluginCard();
    };
    // Showing, hiding or joining an output re-lays the card the way a Dual I/O change does; the cables left on an
    // output that went hidden are dropped first (an invisible jack cannot be unplugged).
    module.onOutputsChanged = [this] {
        card_.owner.dropRoutingsOnHiddenJacks(card_.nodeId);
        card_.applyDualIOLayoutChange();
    };
    module.onParameterContextMenu = [this](const juce::String& paramId, juce::PopupMenu& menu) {
        appendParameterMenuItems(paramId, menu);
    };
    registered_ = true;
}

ModuleComponent::HostedCardBinding::~HostedCardBinding() { shutdown(); }

// The module is only touched through the weak reference: it may already have been destroyed (a node delete
// frees it before the card is torn down), in which case its observer list went with it and there is nothing
// to remove. The store is not owned here but outlives every card (MainComponent declares it before the
// GraphEditor), so its listener is always removed.
void ModuleComponent::HostedCardBinding::shutdown() {
    if (!registered_)
        return;
    registered_ = false;

    if (auto* module = module_.get()) {
        module->removeInstanceObserver(this);
        module->onCardLayoutChanged = nullptr;
        module->onOutputsChanged = nullptr;
        module->onParameterContextMenu = nullptr;
    }
    if (store_ != nullptr)
        store_->removeListener(this);
}

// The plugin window's right-click on one of its controls: "Add to card" (or "On the card") for that
// parameter. The item's action reaches the module weakly, so a menu left open past a node delete is harmless.
void ModuleComponent::HostedCardBinding::appendParameterMenuItems(const juce::String& paramId, juce::PopupMenu& menu) {
    auto* module = module_.get();
    if (module == nullptr)
        return;
    synth::ui::appendAddToCardMenuItem(menu, *module, store_, card_.owner.getAudioEngine().getGraph(), card_.nodeId,
                                       card_.undoManager, paramId);
}

// The instance is still alive for this whole call (HostedPluginModule fires the gone edge before it can be
// reaped, and from the module's destructor before it frees it), so unbinding here removes every parameter
// listener from live parameters -- no liveness question. The re-measure is deferred: when this edge comes
// from the module's destructor (a node delete / "Replace with...") the node is already out of the graph and
// the card is about to be destroyed by updateComponents(), so nothing here may lay out against the graph.
// The deferred step checks the module is still there. A swap to a new instance follows with the live edge,
// which rebuilds and re-measures itself; the extra pass is harmless.
void ModuleComponent::HostedCardBinding::hostedInstanceGone() {
    card_.setPluginTouchToAdd(false); // its capture listens to this instance's parameters
    card_.unbindHostedPluginCard(/*paramsAlive*/ true);
    if (card_.dualIOButton != nullptr)
        card_.dualIOButton->setVisible(false); // nothing to split until the next instance publishes

    juce::Component::SafePointer<ModuleComponent> safeCard(&card_);
    juce::MessageManager::callAsync([safeCard] {
        if (safeCard != nullptr && safeCard->hostedCard_ != nullptr && safeCard->hostedCard_->getModule() != nullptr)
            safeCard->relayoutHostedPluginCard();
    });
}

void ModuleComponent::HostedCardBinding::hostedInstanceLive() {
    card_.repaint(); // the shown name can change when the plugin publishes
    if (card_.dualIOButton != nullptr)
        card_.dualIOButton->setVisible(module_.get() != nullptr && module_->hasStereoPairInstance());
    card_.reconcileHostedModKnobs();
    card_.rebuildHostedPluginCard();
    card_.refreshHostedLoadStatus(); // a success clears the reason; it re-measures only when it changed
    card_.relayoutHostedPluginCard();
}

void ModuleComponent::HostedCardBinding::hostedLoadFailed() { card_.refreshHostedLoadStatus(); }

void ModuleComponent::HostedCardBinding::layoutChangedForPlugin(const synth::PluginIdentity& identity) {
    auto* module = module_.get();
    if (module == nullptr || identity != module->getIdentity())
        return;
    card_.rebuildHostedPluginCard();
    card_.relayoutHostedPluginCard();
}

// ---- One control per slot ---------------------------------------------------------------------------------

// An orphaned slot (the instance is live and its parameter no longer resolves) is hidden entirely, and so is
// one with no live parameter yet: only the picker mentions those. A Knob/Toggle/Choice slot builds the same
// widget the generic auto-UI would, added to the SAME member arrays so the generic knob-grid / combo /
// toggle layout places it and the card grows like any many-parameter module. sliderParams/comboParams get
// a null entry to stay index-parallel: a hosted parameter is not a RangedAudioParameter.
void ModuleComponent::HostedCardBinding::addControl(const synth::ResolvedCardSlot& resolved) {
    if (resolved.orphaned || resolved.param == nullptr)
        return;

    const auto text = slotText(resolved);
    switch (resolvedKind(resolved)) {
    case synth::CardSlotKind::Toggle:
        addToggle(resolved, text);
        break;
    case synth::CardSlotKind::Choice:
        addChoice(resolved, text);
        break;
    default:
        addKnob(resolved, text);
        break;
    }
}

void ModuleComponent::HostedCardBinding::addKnob(const synth::ResolvedCardSlot& resolved, const juce::String& text) {
    auto* slider = card_.adoptBespokeWidget(new synth::ui::CardKnobSlider()); // keyboard steps with gestures
    card_.sliders.add(slider);
    slider->setComponentID("hostedKnob:" + resolved.slot.paramId);
    styleHostedKnob(*slider);
    card_.addAndMakeVisible(slider);
    // Mirrors createControls()'s own addMouseListener(this) for a generic auto-UI slider --
    // without it a right-click never reaches ModuleComponent::mouseDown at all, which is exactly
    // why a hosted knob was inert to right-click before this (docs/control/plugin-card-layout.md's
    // "Not yet wired" note).
    slider->addMouseListener(&card_, false);
    card_.sliderParams.add(nullptr); // a hosted parameter is not a RangedAudioParameter
    slider->setTitle(text);          // named before registering: the MIDI Learn tooltip builds on this one
    slider->setTooltip(text);
    card_.registerHostedMidiLearnable(*slider, *resolved.param, resolved.slot.paramId);

    auto* label = card_.adoptBespokeWidget(new juce::Label(text, text));
    card_.sliderLabels.add(label);
    label->setJustificationType(juce::Justification::centred);
    card_.addAndMakeVisible(label);

    auto& attachment = *card_.hostedAttachments_.add(new HostedParameterAttachment(*resolved.param, *slider));
    wireGestures(attachment, *resolved.param);
    // Takes a mod cable like any card knob: drop target, ring, mod dot, ring drag and cable pickup.
    card_.wireCardControlGestures(*slider, *slider, nullptr);
    attachment.restingValue = [module = module_, paramId = resolved.slot.paramId]() -> std::optional<float> {
        if (auto* hosted = module.get())
            return hosted->modSlotBase(paramId);
        return std::nullopt;
    };
}

void ModuleComponent::HostedCardBinding::addToggle(const synth::ResolvedCardSlot& resolved, const juce::String& text) {
    auto* toggle = card_.adoptBespokeWidget(new detail::MidiLearnableToggleButton(text));
    card_.toggles.add(toggle);
    toggle->setComponentID("hostedToggle:" + resolved.slot.paramId);
    card_.addAndMakeVisible(toggle);
    toggle->addMouseListener(&card_, false); // See addKnob()'s own comment
    toggle->setTooltip(text);
    card_.registerHostedMidiLearnable(*toggle, *resolved.param, resolved.slot.paramId);

    wireGestures(*card_.hostedAttachments_.add(new HostedParameterAttachment(*resolved.param, *toggle)),
                 *resolved.param);
}

void ModuleComponent::HostedCardBinding::addChoice(const synth::ResolvedCardSlot& resolved, const juce::String& text) {
    auto* combo = card_.adoptBespokeWidget(new juce::ComboBox());
    card_.comboBoxes.add(combo);
    combo->setComponentID("hostedChoice:" + resolved.slot.paramId);
    card_.addAndMakeVisible(combo);
    combo->addMouseListener(&card_, false); // See addKnob()'s own comment
    card_.comboParams.add(nullptr);         // a hosted parameter is not a RangedAudioParameter
    combo->setTitle(text);
    combo->setTooltip(text);
    card_.registerHostedMidiLearnable(*combo, *resolved.param, resolved.slot.paramId);

    auto* label = card_.adoptBespokeWidget(new juce::Label(text, text));
    card_.comboLabels.add(label);
    card_.addAndMakeVisible(label);

    wireGestures(*card_.hostedAttachments_.add(new HostedParameterAttachment(*resolved.param, *combo)),
                 *resolved.param);
}

// The attachment reports every gesture on ITS parameter -- the card's own widgets, the plugin's editor, a
// MIDI controller -- straight off the parameter's listener list. Hosted parameters are NOT routed through
// ModuleComponent::parameterGestureChanged: that one keys on an int index into the module's OWN parameter
// array, where a hosted index 0 would collide with the module's own "muted" parameter.
void ModuleComponent::HostedCardBinding::wireGestures(HostedParameterAttachment& attachment,
                                                      const juce::AudioProcessorParameter& param) {
    ModuleComponent* card = &card_;
    const auto* key = &param;
    attachment.onGestureChanged = [card, key](bool starting) { card->handleHostedGesture(*key, starting); };
}

// ---- ModuleComponent: build / rebuild / unbind ------------------------------------------------------------

// createControls() runs in the constructor, before an async plugin load has published its instance, so the
// body starts as the two buttons only and the card fills in from hostedInstanceLive(). When the instance is
// already live (a card built for an existing node) it is built right here; createControls() ends with
// updateLayout(), so no re-measure is needed on this path.
void ModuleComponent::createHostedPluginControls(synth::HostedPluginModule& hosted) {
    openPluginEditorButton = std::make_unique<juce::TextButton>("Open Editor");
    openPluginEditorButton->setComponentID("openPluginEditor");
    openPluginEditorButton->setTooltip("Open this plugin's editor window");
    openPluginEditorButton->setEnabled(hosted.hasInstance());
    openPluginEditorButton->onClick = [this] {
        if (owner.onOpenPluginEditorRequested)
            owner.onOpenPluginEditorRequested(nodeId);
    };
    addAndMakeVisible(openPluginEditorButton.get());

    hostedStatusLabel = std::make_unique<juce::Label>("hostedLoadStatus");
    hostedStatusLabel->setComponentID("hostedLoadStatus");
    hostedStatusLabel->setTitle("Plugin load status");
    hostedStatusLabel->setFont(juce::FontOptions(kHostedStatusFontSize));
    hostedStatusLabel->setJustificationType(juce::Justification::topLeft);
    hostedStatusLabel->setMinimumHorizontalScale(1.0f);
    hostedStatusLabel->setInterceptsMouseClicks(false, false);
    hostedStatusLabel->setWantsKeyboardFocus(false);
    hostedStatusLabel->setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
    hostedStatusLabel->setVisible(false);
    addChildComponent(hostedStatusLabel.get());

    // "Turn on": the way back after "Open without it" switched this plugin off for the open. It reloads the
    // plugin into this module, which still holds its saved state, card layout and mod slots, so it is not an undo
    // step (it restores what the project already holds). Same button kind as Open Editor.
    turnPluginBackOnButton = std::make_unique<juce::TextButton>("Turn on");
    turnPluginBackOnButton->setComponentID("turnPluginBackOn");
    turnPluginBackOnButton->setTitle("Turn this plugin back on"); // named after the plugin when it shows
    turnPluginBackOnButton->setTooltip("Load this plugin again in this project");
    turnPluginBackOnButton->onClick = [this] {
        if (auto* hostedModule = hostedCard_ != nullptr ? hostedCard_->getModule() : nullptr) {
            hostedModule->turnBackOn();
            refreshHostedLoadStatus(); // loading now: the status and this button go, the card re-measures
        }
    };
    addChildComponent(turnPluginBackOnButton.get());

    // One split button for adding controls: the list picker on the left, "add by moving a control in the
    // plugin" on the right (the same split button as the mod dot panel's "Add source").
    addControlsSplit = std::make_unique<synth::ui::SplitButton>(
        synth::ui::SplitButtonHalfSpec{synth::ui::ModDotGlyph::List, {}, "Add from list", {}, "Add from list"},
        synth::ui::SplitButtonHalfSpec{synth::ui::ModDotGlyph::Hand,
                                       {},
                                       "Add by moving a control in the plugin",
                                       "Add by moving a control in the plugin, on",
                                       "Add by moving a control in the plugin"},
        "Add controls");
    addControlsSplit->setComponentID("addControls");
    addControlsSplit->leftHalf().setComponentID("addControlsFromList");
    addControlsSplit->rightHalf().setComponentID("addControlsByMoving");
    addControlsSplit->leftHalf().onClick = [this] {
        if (onChooseKnobsRequested)
            onChooseKnobsRequested();
    };
    addControlsSplit->rightHalf().onClick = [this] { setPluginTouchToAdd(!isPluginTouchToAdd()); };
    addAndMakeVisible(addControlsSplit.get());

    hostedCard_ = std::make_unique<HostedCardBinding>(*this, hosted, owner.getPluginCardLayoutStore());
    onChooseKnobsRequested = [this] { showPluginKnobPicker(); };
    if (hosted.hasInstance()) {
        reconcileHostedModKnobs();
        rebuildHostedPluginCard();
    } else
        refreshHostedLoadStatus(/*relayoutOnChange*/ false); // createControls() ends with the measure
}

// Load-time reconciliation, once per publish (not a user edit, so no undo step): a project saved before a mod cable on
// a plugin parameter always added its knob can hold a slot whose parameter is not on the card, so the cable would
// end at nothing. Every such parameter gets its knob, through the same source bindHostedModTarget uses. That writes
// this instance's layout override (the card only, never the plugin's shared default). The module's layout hook is
// off while it runs: the caller rebuilds and re-measures right after, once.
void ModuleComponent::reconcileHostedModKnobs() {
    auto* hosted = hostedCard_ != nullptr ? hostedCard_->getModule() : nullptr;
    if (hosted == nullptr || !hosted->hasInstance() || !hosted->takeModKnobReconcile())
        return;

    synth::ui::HostedCardLayoutSource source(*hosted, owner.getPluginCardLayoutStore(),
                                             owner.getAudioEngine().getGraph(), nodeId, /*undoManager=*/nullptr);
    auto onLayoutChanged = std::move(hosted->onCardLayoutChanged);
    hosted->onCardLayoutChanged = nullptr;
    for (int k = 0; k < synth::HostedPluginModule::kMaxModSlots; ++k) {
        const auto paramId = hosted->modSlotParamId(synth::HostedPluginModule::kModChannelBase + k);
        if (paramId.isNotEmpty() &&
            source.parameterState(paramId) == synth::ui::HostedCardLayoutSource::ParameterState::Available)
            source.showParameter(paramId);
    }
    hosted->onCardLayoutChanged = std::move(onLayoutChanged);
}

// The reason applies while there is no instance, no load is in flight and the module holds a message. It is
// the label under the Open Editor row and that button's tooltip; when the instance publishes both go back.
// Re-measures only when the label appears or disappears, so the card's 15 Hz poll can call it freely.
void ModuleComponent::refreshHostedLoadStatus(bool relayoutOnChange) {
    auto* hosted = hostedCard_ != nullptr ? hostedCard_->getModule() : nullptr;
    if (hosted == nullptr || hostedStatusLabel == nullptr || openPluginEditorButton == nullptr)
        return;

    const bool show = !hosted->hasInstance() && !hosted->isLoading() && hosted->getStatusMessage().isNotEmpty();
    const juce::String message = show ? hosted->getStatusMessage() : juce::String();
    const bool showTurnOn = show && hosted->isSwitchedOffAfterCrash() && turnPluginBackOnButton != nullptr;
    const bool turnOnChanged = turnPluginBackOnButton != nullptr && showTurnOn != turnPluginBackOnButton->isVisible();
    const bool visibilityChanged = show != hostedStatusLabel->isVisible() || turnOnChanged;

    if (hostedStatusLabel->getText() != message)
        hostedStatusLabel->setText(message, juce::dontSendNotification);
    const juce::String tooltip = show ? message : juce::String("Open this plugin's editor window");
    if (openPluginEditorButton->getTooltip() != tooltip)
        openPluginEditorButton->setTooltip(tooltip);
    if (visibilityChanged) {
        hostedStatusLabel->setVisible(show);
        if (turnPluginBackOnButton != nullptr) {
            turnPluginBackOnButton->setVisible(showTurnOn);
            if (showTurnOn) {
                turnPluginBackOnButton->setTitle("Turn " + hosted->getPluginName() + " back on");
                turnPluginBackOnButton->setTooltip("Load " + hosted->getPluginName() + " again in this project");
            }
        }
        if (relayoutOnChange)
            relayoutHostedPluginCard();
    }
}

// Opens the parameter list picker as a juce::CallOutBox anchored to the split button's list half. Reached from
// that half (createHostedPluginControls() above wires onChooseKnobsRequested to this) and from the context
// menu's "Edit Layout..." item (ModuleComponentInteraction.cpp), so this is the ONE place that builds the popover
// (docs/control/plugin-card-layout.md#choosing-knobs).
void ModuleComponent::showPluginKnobPicker() {
    if (hostedCard_ == nullptr)
        return;
    auto* hosted = hostedCard_->getModule();
    if (hosted == nullptr || !hosted->hasInstance())
        return;

    auto picker = std::make_unique<synth::ui::PluginKnobPickerComponent>(
        *hosted, owner.getPluginCardLayoutStore(), owner.getAudioEngine().getGraph(), nodeId, undoManager,
        owner.getCardKeyboard().getShortcutManager());
    const auto anchor =
        addControlsSplit != nullptr ? addControlsSplit->leftHalf().getScreenBounds() : getScreenBounds();
    launchCardLayoutEditorCallOutBox(std::move(picker), anchor);
}

bool ModuleComponent::isPluginTouchToAdd() const {
    return hostedCard_ != nullptr && hostedCard_->touchToAdd != nullptr;
}

// "Add by moving a control in the plugin". On: the plugin's own window opens through the owner's editor callback
// (no popover is built, so nothing covers the plugin) and the owner is told to show the window's tab. The mode
// stays on until it is turned off here: the hand half again, Done on the tab, Esc, or closing the window (the owner
// reaches this through GraphEditor::endPluginAddingControls). Every path ends in the split button showing the
// truth, including a refusal (no live instance).
void ModuleComponent::setPluginTouchToAdd(bool on) {
    if (hostedCard_ == nullptr)
        return;
    const bool wasOn = isPluginTouchToAdd();
    auto* hosted = hostedCard_->getModule();
    if (on && !wasOn && hosted != nullptr && hosted->hasInstance()) {
        hostedCard_->touchToAdd = std::make_unique<synth::ui::PluginTouchToAdd>(
            *hosted, owner.getPluginCardLayoutStore(), owner.getAudioEngine().getGraph(), nodeId, undoManager);
        hostedCard_->touchToAdd->onRequestOpenEditor = [this] {
            if (owner.onOpenPluginEditorRequested)
                owner.onOpenPluginEditorRequested(nodeId);
        };
        hostedCard_->touchToAdd->start(); // opens the window, so the tab has a window to hang from
        if (owner.onPluginAddingControlsChanged)
            owner.onPluginAddingControlsChanged(nodeId, true);
        juce::AccessibilityHandler::postAnnouncement(
            "Adding controls. Move a control in the plugin window to add it. Escape stops.",
            juce::AccessibilityHandler::AnnouncementPriority::medium);
    } else if (!on && wasOn) {
        hostedCard_->touchToAdd.reset();
        if (owner.onPluginAddingControlsChanged)
            owner.onPluginAddingControlsChanged(nodeId, false);
        juce::AccessibilityHandler::postAnnouncement("Stopped adding controls.",
                                                     juce::AccessibilityHandler::AnnouncementPriority::medium);
    }
    if (addControlsSplit != nullptr)
        addControlsSplit->setRightLit(isPluginTouchToAdd());
}

// Only ever runs with the instance the resolver reads still live: from the live edge, from a layout change
// on a live instance, or at construction. Attachments left over from the previous build are unbound first
// (their parameters are alive: a retired instance was unbound at its gone edge, which emptied them).
void ModuleComponent::rebuildHostedPluginCard() {
    if (hostedCard_ == nullptr)
        return;
    unbindHostedPluginCard(/*paramsAlive*/ true);

    auto* hosted = hostedCard_->getModule();
    if (hosted == nullptr || !hosted->hasInstance())
        return;

    const auto resolved = synth::resolveHostedCardLayout(*hosted, owner.getPluginCardLayoutStore());
    hostedCard_->boundInstance = hosted->getActiveInstanceForEditor();
    for (const auto& slot : resolved.slots)
        hostedCard_->addControl(slot);
}

// Attachments first, then the widgets they point at. `paramsAlive` false abandons each attachment instead of
// detaching it, so a parameter that may already be freed is never touched. Every slider/combo/toggle on a
// hosted card is a hosted control (the branch builds no generic ones), so the member arrays are emptied
// whole and the widgets they point at destroyed.
void ModuleComponent::unbindHostedPluginCard(bool paramsAlive) {
    for (auto* attachment : hostedAttachments_) {
        attachment->onGestureChanged = nullptr;
        if (!paramsAlive)
            attachment->abandon();
    }
    hostedAttachments_.clear();

    // Drop every hosted registry entry BEFORE the widgets below are destroyed, or an entry
    // is left pointing at a freed Component until the next rebuild's add() calls overwrite it (see
    // MidiLearnableRegistry::clearHosted()'s own comment).
    clearHostedMidiLearnable();

    sliderLabels.clear();
    sliders.clear();
    sliderParams.clear();
    comboLabels.clear();
    comboBoxes.clear();
    comboParams.clear();
    toggles.clear();
    ownedBespokeWidgets_.clear(); // a hosted card's widgets are the only bespoke ones it has

    if (hostedCard_ != nullptr) {
        hostedCard_->boundInstance = nullptr;
        hostedCard_->activeGestures.clear();
    }
}

// The card half of detachFromProcessor(). The module is reached only through the binding's weak reference:
// when it is already destroyed (the node was freed before the card was torn down) its gone edge already
// unbound everything, and if some path skipped that, the attachments are abandoned rather than detached.
// A live module whose ACTIVE instance is no longer the one bound is treated the same way.
void ModuleComponent::releaseHostedPluginCard() {
    if (hostedCard_ == nullptr)
        return;

    auto* hosted = hostedCard_->getModule();
    const bool paramsAlive = hosted != nullptr && hosted->getActiveInstanceForEditor() == hostedCard_->boundInstance;

    hostedCard_->shutdown();
    if (hostedCard_->touchToAdd != nullptr) {
        // Same teardown as turning it off, minus the split button, which goes with the card.
        hostedCard_->touchToAdd.reset();
        if (owner.onPluginAddingControlsChanged)
            owner.onPluginAddingControlsChanged(nodeId, false);
    }
    unbindHostedPluginCard(paramsAlive);
    addControlsSplit.reset();
    hostedCard_.reset();
}

// Same three steps as refreshPortLayout(): re-measure, let the canvas make room (and drop routings left on
// a jack that disappeared), repaint.
void ModuleComponent::relayoutHostedPluginCard() {
    if (module != nullptr)
        refreshPortLayout();
}

// One begin/end pair -> exactly ONE undo step, however the gesture was made. The snapshot is taken at the
// first gesture start and pushed at the last end, so two gestures overlapping (the plugin's own editor
// and this card) still collapse into one step instead of the second capture overwriting the first.
void ModuleComponent::handleHostedGesture(const juce::AudioProcessorParameter& param, bool starting) {
    if (undoManager == nullptr || module == nullptr || hostedCard_ == nullptr)
        return;

    auto& active = hostedCard_->activeGestures;
    auto& graph = owner.getAudioEngine().getGraph();
    if (starting) {
        if (active.empty())
            undoManager->captureBeforeState(graph);
        active.insert(&param);
    } else if (active.erase(&param) > 0 && active.empty()) {
        undoManager->pushSnapshotFromCapture(graph);
    }
}

// ---- Layout -----------------------------------------------------------------------------------------------

// Both buttons share one row at the top of the body, each half of the narrow band. The knobs, toggles and
// combos the layout put on the card are placed by the generic grid in layoutDefaultContent, below this.
int ModuleComponent::layoutHostedPluginChrome(int y, int narrowX, int narrowW, bool apply) {
    if (openPluginEditorButton == nullptr)
        return y;

    constexpr int kGap = 4;
    const int half = (narrowW - kGap) / 2;
    if (apply) {
        openPluginEditorButton->setBounds(narrowX, y, half, kRowHeight);
        if (addControlsSplit != nullptr)
            addControlsSplit->setBounds(narrowX + half + kGap, y, narrowW - half - kGap, kRowHeight);
    }
    y += kRowHeight + 6;
    if (hostedStatusLabel != nullptr && hostedStatusLabel->isVisible()) {
        if (apply)
            hostedStatusLabel->setBounds(narrowX, y, narrowW, kHostedStatusLines * kHostedStatusLineHeight);
        y += kHostedStatusLines * kHostedStatusLineHeight + 6;
    }
    if (turnPluginBackOnButton != nullptr && turnPluginBackOnButton->isVisible()) {
        if (apply)
            turnPluginBackOnButton->setBounds(narrowX, y, (narrowW - 4) / 2, kRowHeight);
        y += kRowHeight + 6;
    }
    return y;
}

void ModuleComponent::reflectHostedRestingValues() {
    auto* hosted = dynamic_cast<synth::HostedPluginModule*>(module);
    if (hosted == nullptr || hosted->getModulationTargets().empty())
        return;
    for (auto* attachment : hostedAttachments_)
        attachment->reflectRestingValue();
}
