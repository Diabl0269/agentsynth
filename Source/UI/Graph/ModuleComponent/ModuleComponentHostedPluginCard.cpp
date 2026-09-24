// ModuleComponentHostedPluginCard.cpp -- the Hosted Plugin card body: the Open Editor / Choose knobs...
// chrome, and the knobs, toggles and choice combos the resolved layout puts on the card, each bound live
// to its hosted parameter through a HostedParameterAttachment. ModuleComponent is declared in
// ModuleComponent.h; this is the per-type unit next to ModuleComponentEQCard.cpp and
// ModuleComponentEnvelopeCard.cpp, and it takes the HostedPlugin branch OUT of createControls(), which is
// at the function-size ratchet ceiling. See docs/control/plugin-card-layout.md#card-rendering-as-built-fro128.
#include "ModuleComponentHostedPluginCard.h"
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/CardLayout.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

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
    }
    if (store_ != nullptr)
        store_->removeListener(this);
}

// The instance is still alive for this whole call (HostedPluginModule fires the gone edge before it can be
// reaped, and from the module's destructor before it frees it), so unbinding here removes every parameter
// listener from live parameters -- no liveness question. The re-measure is deferred: when this edge comes
// from the module's destructor (a node delete / "Replace with...") the node is already out of the graph and
// the card is about to be destroyed by updateComponents(), so nothing here may lay out against the graph.
// The deferred step checks the module is still there. A swap to a new instance follows with the live edge,
// which rebuilds and re-measures itself; the extra pass is harmless.
void ModuleComponent::HostedCardBinding::hostedInstanceGone() {
    card_.unbindHostedPluginCard(/*paramsAlive*/ true);

    juce::Component::SafePointer<ModuleComponent> safeCard(&card_);
    juce::MessageManager::callAsync([safeCard] {
        if (safeCard != nullptr && safeCard->hostedCard_ != nullptr && safeCard->hostedCard_->getModule() != nullptr)
            safeCard->relayoutHostedPluginCard();
    });
}

void ModuleComponent::HostedCardBinding::hostedInstanceLive() {
    card_.rebuildHostedPluginCard();
    card_.relayoutHostedPluginCard();
}

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
    auto* slider = card_.sliders.add(new juce::Slider());
    slider->setComponentID("hostedKnob:" + resolved.slot.paramId);
    styleHostedKnob(*slider);
    card_.addAndMakeVisible(slider);
    card_.sliderParams.add(nullptr);

    auto* label = card_.sliderLabels.add(new juce::Label(text, text));
    label->setJustificationType(juce::Justification::centred);
    card_.addAndMakeVisible(label);

    wireGestures(*card_.hostedAttachments_.add(new HostedParameterAttachment(*resolved.param, *slider)),
                 *resolved.param);
}

void ModuleComponent::HostedCardBinding::addToggle(const synth::ResolvedCardSlot& resolved, const juce::String& text) {
    auto* toggle = card_.toggles.add(new detail::MidiLearnableToggleButton(text));
    toggle->setComponentID("hostedToggle:" + resolved.slot.paramId);
    card_.addAndMakeVisible(toggle);

    wireGestures(*card_.hostedAttachments_.add(new HostedParameterAttachment(*resolved.param, *toggle)),
                 *resolved.param);
}

void ModuleComponent::HostedCardBinding::addChoice(const synth::ResolvedCardSlot& resolved, const juce::String& text) {
    auto* combo = card_.comboBoxes.add(new juce::ComboBox());
    combo->setComponentID("hostedChoice:" + resolved.slot.paramId);
    card_.addAndMakeVisible(combo);
    card_.comboParams.add(nullptr);

    card_.addAndMakeVisible(card_.comboLabels.add(new juce::Label(text, text)));

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

    chooseKnobsButton = std::make_unique<juce::TextButton>("Choose knobs...");
    chooseKnobsButton->setComponentID("chooseKnobs");
    chooseKnobsButton->setTooltip("Choose which plugin parameters show on this card");
    chooseKnobsButton->onClick = [this] {
        if (onChooseKnobsRequested)
            onChooseKnobsRequested();
    };
    addAndMakeVisible(chooseKnobsButton.get());

    hostedCard_ = std::make_unique<HostedCardBinding>(*this, hosted, owner.getPluginCardLayoutStore());
    if (hosted.hasInstance())
        rebuildHostedPluginCard();
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
// whole.
void ModuleComponent::unbindHostedPluginCard(bool paramsAlive) {
    for (auto* attachment : hostedAttachments_) {
        attachment->onGestureChanged = nullptr;
        if (!paramsAlive)
            attachment->abandon();
    }
    hostedAttachments_.clear();

    sliderLabels.clear();
    sliders.clear();
    sliderParams.clear();
    comboLabels.clear();
    comboBoxes.clear();
    comboParams.clear();
    toggles.clear();

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
    unbindHostedPluginCard(paramsAlive);
    chooseKnobsButton.reset();
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
        if (chooseKnobsButton != nullptr)
            chooseKnobsButton->setBounds(narrowX + half + kGap, y, narrowW - half - kGap, kRowHeight);
    }
    return y + kRowHeight + 6;
}
