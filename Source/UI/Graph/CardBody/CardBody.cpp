// CardBody.cpp -- building a card body's widgets and keeping their bindings: one widget per plan item,
// created exactly as the generic card always created it (same widget class, componentID, style,
// attachment, MIDI Learn registration and modulation-amount gesture), the faders, segmented switches
// and steppers a layout may ask for, plus the views and teardown.
// Layout is CardBodyLayout.cpp; the More row is CardBodyMoreRow.cpp; tab strips are CardBodyTabs.cpp.
#include "CardBody.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyMoreButton.h"
#include "CardBodySwapMotion.h"
#include "CardBodyViews.h"
#include "CardLayoutOverride.h"
#include "ModuleCardLayoutResolver.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Graph/CardWidgets/CardTogglePill.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/ModuleViews/ThresholdControlComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth {

namespace {

using LearnableToggle = synth::ui::midilearn::RightClickSafeButton<juce::ToggleButton>;

bool isAdsr(juce::AudioProcessor& module) {
    auto* mb = dynamic_cast<ModuleBase*>(&module);
    return mb != nullptr && mb->getModuleType() == ModuleType::ADSR;
}

// Every float knob is a rotary. The ADSR's put their text box ABOVE the dial so the readout sits over
// its knob rather than under it as a caption; every other module keeps it below.
void setFloatKnobStyle(juce::Slider& slider, bool adsr) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(adsr ? juce::Slider::TextBoxAbove : juce::Slider::TextBoxBelow, false, 50, 20);
}

// The four ADSR time parameters keep a linear parameter range (a skewed one would worsen the untrusted
// in-[0,1] rescale heuristic for model-authored patches, docs/modules/modules.md#adsr-envelope-module),
// so the knob gets the 0.3 skew instead. It MUST run after the SliderParameterAttachment is built: the
// attachment installs a range driven by conversion lambdas, which ignore any skew set on it, so the
// range is replaced by a plain already-skewed one. getValue()/setValue() still exchange real units.
void applyAdsrTimeSkew(juce::Slider& slider, const juce::RangedAudioParameter& param) {
    const auto& id = param.paramID;
    if (id != "attack" && id != "hold" && id != "decay" && id != "release")
        return;
    const auto& r = param.getNormalisableRange();
    slider.setNormalisableRange(
        juce::NormalisableRange<double>((double)r.start, (double)r.end, (double)r.interval, 0.3));
}

// Narrows a slider to the layout's range within the parameter's own. Like applyAdsrTimeSkew it MUST run
// after the SliderParameterAttachment (and after that skew), which would otherwise replace the range; it
// keeps the slider's current skew so the ADSR times stay skewed. A range that misses the parameter's
// leaves the full one. The slider only clamps its display: the parameter keeps any value it is given.
void applyLayoutRange(juce::Slider& slider, const juce::RangedAudioParameter& param,
                      const std::optional<juce::Range<double>>& range) {
    if (!range.has_value() || dynamic_cast<const juce::AudioParameterFloat*>(&param) == nullptr)
        return;
    const auto& full = param.getNormalisableRange();
    const auto narrowed = range->getIntersectionWith(juce::Range<double>((double)full.start, (double)full.end));
    if (narrowed.isEmpty())
        return;
    slider.setNormalisableRange(juce::NormalisableRange<double>(
        narrowed.getStart(), narrowed.getEnd(), (double)full.interval, slider.getNormalisableRange().skew));
}

// The Oscillator waveform choice {"Sine", "Square", "Saw", "Triangle"} (in that order) gets a glyph per
// item; ModuleComponent::refreshWaveformComboIcons re-tints them on a theme switch, finding the combo by
// this same item set.
bool isWaveformChoice(const juce::StringArray& choices) {
    return choices.size() == 4 && choices[0] == "Sine" && choices[1] == "Square" && choices[2] == "Saw" &&
           choices[3] == "Triangle";
}

void fillWaveformItems(juce::ComboBox& combo, const juce::StringArray& choices, juce::LookAndFeel& lnf) {
    using synth::theme::Icon;
    const Icon icons[4] = {Icon::WaveformSine, Icon::WaveformSquare, Icon::WaveformSaw, Icon::WaveformTriangle};
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&lnf);
    for (int i = 0; i < 4; ++i) {
        std::unique_ptr<juce::Drawable> icon;
        if (lf != nullptr)
            icon = lf->getIcon(icons[i]); // may be null headless
        combo.getRootMenu()->addItem(i + 1, choices[i], true, false, std::move(icon));
    }
}

} // namespace

CardBody::CardBody(ModuleComponent& card, juce::AudioProcessor& module, const std::optional<CardLayout>& layout,
                   const std::vector<CardDimRule>& dimRules)
    : card_(card)
    , module_(module)
    , plan_(CardBodyPlan::forModule(module, layout, dimRules))
    , layout_(layout) {}

// The layout chain (docs/layout/module-card-layout.md#where-a-layout-comes-from): the node's own
// override, then the type's stored default in `store` (the app binds one per GraphEditor,
// ModuleCardLayoutBinding), then the code default, then automatic. A card built on a processor that
// is not a graph node (tests) has no override. The override and the store's revision are recorded so
// isStaleFor can tell, without reading the store, that the card must be rebuilt.
std::unique_ptr<CardBody> CardBody::createFor(ModuleComponent& card, juce::AudioProcessor& module,
                                              const juce::AudioProcessorGraph& graph,
                                              juce::AudioProcessorGraph::NodeID nodeId, ModuleCardLayoutStore* store) {
    if (!cardBodyBuildsWidgetsFor(module))
        return nullptr;
    juce::StringArray paramIds;
    for (auto* param : module.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            paramIds.add(ranged->paramID);
    const auto type = AIStateMapper::getFactoryTypeName(&module);
    const auto stored = getCardLayoutOverride(graph, nodeId);
    const auto resolved = resolveModuleCardLayout(type, stored, store, DefaultCardLayouts::builtIn(), &paramIds);
    auto body = std::make_unique<CardBody>(card, module, resolved.layout, resolved.dimRules);
    body->builtFromOverride_ = stored.isVoid() ? juce::String() : juce::JSON::toString(stored, true);
    body->store_ = store;
    body->builtFromRevision_ = store != nullptr ? store->getRevision(type) : 0;
    return body;
}

bool CardBody::isStaleFor(const juce::AudioProcessorGraph::Node& node) const {
    const auto& stored = node.properties[kCardLayoutNodeProperty];
    const juce::String current = stored.isObject() ? juce::JSON::toString(stored, true) : juce::String();
    if (current != builtFromOverride_)
        return true;
    const auto* store = store_.get();
    return store != nullptr && store->getRevision(AIStateMapper::getFactoryTypeName(&module_)) != builtFromRevision_;
}

void CardBody::createViews() {
    for (auto& item : plan_.items) {
        if (item.kind != CardBodyItem::Kind::View)
            continue;
        if (const auto* factory = findCardViewFactory(item.view)) {
            if (auto view = factory->create(module_)) {
                card_.addAndMakeVisible(view.get());
                view->setVisible(item.open);
                item.widget = views_.add(view.release());
            }
        }
    }
}

// Declaration order, each widget followed by its caption: the child order (and therefore Tab and
// accessibility order) the generic card has always had, whatever order the layout places them in.
void CardBody::createParameterWidgets() {
    for (auto& item : plan_.items) {
        if (item.kind == CardBodyItem::Kind::Choice)
            createChoice(item, *static_cast<juce::AudioParameterChoice*>(item.param));
        else if (item.kind == CardBodyItem::Kind::Knob || item.kind == CardBodyItem::Kind::KnobLarge)
            createKnob(item, *item.param);
        else if (item.kind == CardBodyItem::Kind::Toggle)
            createToggle(item, *static_cast<juce::AudioParameterBool*>(item.param));
        else if (item.kind == CardBodyItem::Kind::FaderV || item.kind == CardBodyItem::Kind::FaderH)
            createFader(item, *item.param);
        else if (item.kind == CardBodyItem::Kind::Segmented)
            createSegmented(item, *item.param);
        else if (item.kind == CardBodyItem::Kind::Stepper)
            createStepper(item, *static_cast<juce::AudioParameterInt*>(item.param));
    }
    styleFooterItems();
    createSectionHeaders();
    createTabStrips();
    if (hasMoreRow())
        createMoreButton();
    applyVisibility();
    startWatchingConditions();
}

// A footer toggle is the small pill; a footer caption sits inline, in the pill's text size.
void CardBody::styleFooterItems() {
    for (auto& item : plan_.items) {
        if (item.section < 0 || !plan_.sections[(size_t)item.section].footer)
            continue;
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(item.widget); toggle != nullptr && item.pill)
            synth::ui::setTogglePillStyle(*toggle, true);
        if (auto* label = dynamic_cast<juce::Label*>(item.label)) {
            label->setFont(synth::theme::AppLookAndFeel::uiFont(synth::theme::AppLookAndFeel::kTogglePillFontHeight));
            label->setJustificationType(juce::Justification::centredLeft);
        }
    }
}

// A titled section's header row: a small caption-style label, in the title's own case (UI text is
// never all caps). Created after every parameter widget so child, Tab and screen-reader order stay
// the declaration order; a header takes no focus.
void CardBody::createSectionHeaders() {
    for (auto& section : plan_.sections) {
        if (!section.hasHeader())
            continue;
        auto* header = new juce::Label("sectionHeader", section.title->trim());
        widgets_.add(header);
        header->setComponentID("cardSectionHeader");
        header->setJustificationType(juce::Justification::centredLeft);
        header->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        header->setInterceptsMouseClicks(false, false);
        header->setWantsKeyboardFocus(false);
        card_.addAndMakeVisible(header);
        section.header = header;
    }
}

// A renamed control keeps its full parameter name in the caption's tooltip, so the rename never hides
// what the control is.
static void nameRenamedCaption(juce::Label& label, const CardBodyItem& item, juce::RangedAudioParameter& param) {
    if (item.caption.has_value())
        label.setTooltip(param.getName(100));
}

// juce::ComboBox::mouseDown already refuses to open its popup on a right click, so a plain combo takes
// the card as a mouse listener for right-click MIDI Learn without a right-click-safe subclass.
void CardBody::createChoice(CardBodyItem& item, juce::AudioParameterChoice& param) {
    auto* combo = new juce::ComboBox();
    widgets_.add(combo);
    card_.comboBoxes.add(combo);
    if (isWaveformChoice(param.choices))
        fillWaveformItems(*combo, param.choices, card_.getLookAndFeel());
    else
        combo->addItemList(param.choices, 1);
    card_.addAndMakeVisible(combo);
    combo->addMouseListener(&card_, false);
    card_.registerMidiLearnable(*combo, &param);
    comboAttachments_.add(new juce::ComboBoxParameterAttachment(param, *combo));
    card_.comboParams.add(&param);

    auto* label = new juce::Label(param.getName(100), item.captionText());
    nameRenamedCaption(*label, item, param);
    widgets_.add(label);
    card_.comboLabels.add(label);
    card_.addAndMakeVisible(label);
    item.widget = combo;
    item.label = label;
}

// The componentID stays the parameter's display name: the bespoke cards (Sequencer, Macros, EQ,
// envelope) and the tests find knobs by it. The card is the knob's mouse listener for right-click
// MIDI Learn; it owns this body, so it outlives every widget here.
void CardBody::createKnob(CardBodyItem& item, juce::RangedAudioParameter& param) {
    const bool isFloat = dynamic_cast<juce::AudioParameterFloat*>(&param) != nullptr;
    auto* knob = new synth::ui::CardKnobSlider();
    widgets_.add(knob);
    card_.sliders.add(knob);
    knob->setComponentID(param.getName(100));
    if (isFloat) {
        setFloatKnobStyle(*knob, isAdsr(module_));
    } else {
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    }
    card_.addAndMakeVisible(knob);
    knob->addMouseListener(&card_, false);
    card_.registerMidiLearnable(*knob, &param);
    card_.wireCardControlGestures(*knob, *knob, &param);
    sliderAttachments_.add(new juce::SliderParameterAttachment(param, *knob));
    if (isFloat)
        applyAdsrTimeSkew(*knob, param);
    applyLayoutRange(*knob, param, item.range);
    card_.sliderParams.add(&param);

    auto* label = new juce::Label(param.getName(100), item.captionText());
    nameRenamedCaption(*label, item, param);
    widgets_.add(label);
    card_.sliderLabels.add(label);
    label->setJustificationType(juce::Justification::centred);
    card_.addAndMakeVisible(label);
    item.widget = knob;
    item.label = label;
}

// A plain juce::ToggleButton toggles on a right click too, so a learnable toggle is right-click safe.
void CardBody::createToggle(CardBodyItem& item, juce::AudioParameterBool& param) {
    auto* toggle = new LearnableToggle(item.captionText());
    widgets_.add(toggle);
    card_.toggles.add(toggle);
    toggle->setComponentID(param.getName(100));
    if (item.caption.has_value())
        toggle->setTooltip(param.getName(100));
    card_.addAndMakeVisible(toggle);
    toggle->addMouseListener(&card_, false);
    card_.registerMidiLearnable(*toggle, &param);
    buttonAttachments_.add(new juce::ButtonParameterAttachment(param, *toggle));
    item.widget = toggle;
}

// The caption above a fader, switch or stepper: the parameter's display name, as a knob's.
juce::Label* CardBody::addCaption(CardBodyItem& item, juce::RangedAudioParameter& param,
                                  juce::Justification justification) {
    auto* label = new juce::Label(param.getName(100), item.captionText());
    nameRenamedCaption(*label, item, param);
    widgets_.add(label);
    label->setJustificationType(justification);
    card_.addAndMakeVisible(label);
    item.label = label;
    return label;
}

// Everything a knob has -- the sliders/sliderParams entry (Automate, value reflection, modulation-target
// lookup), MIDI Learn, the card gestures, the attachment and the ADSR display skew -- plus the default
// a Cmd-click or double-click returns to. The painter picks the look from the fader's size. A choice drawn
// as a fader (a division swapping with its time, promoteChoicesBesideFaders) is a stepped one: the
// attachment gives it the choice's integer steps, its value box the step's text, and its default is the
// choice's default.
void CardBody::createFader(CardBodyItem& item, juce::RangedAudioParameter& param) {
    const bool vertical = item.kind == CardBodyItem::Kind::FaderV;
    auto* fader = new synth::ui::CardFader(vertical ? synth::ui::CardFader::Orientation::Vertical
                                                    : synth::ui::CardFader::Orientation::Horizontal);
    widgets_.add(fader);
    card_.sliders.add(fader);
    fader->setComponentID(param.getName(100));
    card_.addAndMakeVisible(fader);
    fader->addMouseListener(&card_, false);
    card_.registerMidiLearnable(*fader, &param);
    card_.wireCardControlGestures(*fader, *fader, &param);
    sliderAttachments_.add(new juce::SliderParameterAttachment(param, *fader));
    if (dynamic_cast<juce::AudioParameterFloat*>(&param) != nullptr)
        applyAdsrTimeSkew(*fader, param);
    applyLayoutRange(*fader, param, item.range);
    if (vertical) // 40 px wide: "1.00 s" would truncate, "1.00s" fits
        fader->useCompactValueText(param);
    fader->setDoubleClickReturnValue(true, param.convertFrom0to1(param.getDefaultValue()));
    card_.sliderParams.add(&param);
    card_.sliderLabels.add(
        addCaption(item, param, vertical ? juce::Justification::centred : juce::Justification::centredLeft));
    item.widget = fader;
}

// Bound through a plain ParameterAttachment: a pick is one complete gesture (one undo step), and a
// value from automation or undo moves the selection without notifying back.
void CardBody::createSegmented(CardBodyItem& item, juce::RangedAudioParameter& param) {
    auto* segmented = new synth::ui::CardSegmentedSwitch(param.getName(100), cardBodySegmentLabels(param));
    widgets_.add(segmented);
    segmented->setComponentID(param.getName(100));
    segmented->setTooltip(param.getName(100));
    card_.addAndMakeVisible(segmented);
    segmented->addMouseListener(&card_, false);
    card_.registerMidiLearnable(*segmented, &param);
    auto* attachment =
        paramAttachments_.add(new juce::ParameterAttachment(param, [segmented, alive = widgetsAlive_](float value) {
            if (*alive)
                segmented->setSelectedIndex(juce::roundToInt(value), juce::dontSendNotification);
        }));
    segmented->onChange = [attachment](int index) { attachment->setValueAsCompleteGesture((float)index); };
    attachment->sendInitialUpdate();
    addCaption(item, param, juce::Justification::centredLeft);
    item.widget = segmented;
}

// The card listens to the stepper and its buttons (a right click on either button opens the control
// menu: ModuleComponent::mouseDown resolves a registered ancestor).
void CardBody::createStepper(CardBodyItem& item, juce::AudioParameterInt& param) {
    auto* stepper = new synth::ui::CardStepper(param.getName(100));
    widgets_.add(stepper);
    stepper->setComponentID(param.getName(100));
    stepper->setTooltip(param.getName(100));
    card_.addAndMakeVisible(stepper);
    stepper->addMouseListener(&card_, true);
    card_.registerMidiLearnable(*stepper, &param);
    auto* attachment =
        paramAttachments_.add(new juce::ParameterAttachment(param, [stepper, &param, alive = widgetsAlive_](float) {
            if (*alive)
                stepper->setValueText(param.getCurrentValueAsText());
        }));
    stepper->onStep = [attachment, &param](int delta) {
        const auto range = param.getRange();
        const int next = juce::jlimit(range.getStart(), range.getEnd(), param.get() + delta);
        if (next != param.get())
            attachment->setValueAsCompleteGesture((float)next);
    };
    attachment->sendInitialUpdate();
    addCaption(item, param, juce::Justification::centredLeft);
    item.widget = stepper;
}

juce::Component* CardBody::findWidget(const juce::String& paramId) const {
    const int index = plan_.findParam(paramId);
    return index >= 0 ? plan_.items[(size_t)index].widget : nullptr;
}

juce::Component* CardBody::findView(CardView view) const {
    for (const auto& item : plan_.items)
        if (item.kind == CardBodyItem::Kind::View && item.view == view)
            return item.widget;
    return nullptr;
}

void CardBody::setViewOpen(CardView view, bool open) {
    for (auto& item : plan_.items)
        if (item.kind == CardBodyItem::Kind::View && item.view == view) {
            item.open = open;
            if (item.widget != nullptr)
                item.widget->setVisible(open);
        }
}

bool CardBody::isViewOpen(CardView view) const {
    for (const auto& item : plan_.items)
        if (item.kind == CardBodyItem::Kind::View && item.view == view)
            return item.open;
    return false;
}

ThresholdControlComponent* CardBody::getThresholdView() const {
    for (const auto& item : plan_.items)
        if (item.kind == CardBodyItem::Kind::View && item.view == CardView::Threshold)
            return dynamic_cast<ThresholdControlComponent*>(item.widget);
    return nullptr;
}

void CardBody::releaseViews() {
    for (auto& item : plan_.items)
        if (item.kind == CardBodyItem::Kind::View)
            item.widget = nullptr;
    views_.clear();
}

// During an undo the graph may already have freed the processor and its parameters; detaching an
// attachment then touches freed memory, so they are released (leaked) instead. A leaked attachment can
// still deliver an update it had already queued, so the switch and stepper callbacks check widgetsAlive_
// before touching a widget that is about to be deleted.
void CardBody::releaseBindings(bool processorAlive) {
    *widgetsAlive_ = false;
    stopWatchingConditions(processorAlive);
    if (processorAlive) {
        sliderAttachments_.clear();
        comboAttachments_.clear();
        buttonAttachments_.clear();
        paramAttachments_.clear();
        return;
    }
    while (sliderAttachments_.size() > 0)
        (void)sliderAttachments_.removeAndReturn(sliderAttachments_.size() - 1);
    while (comboAttachments_.size() > 0)
        (void)comboAttachments_.removeAndReturn(comboAttachments_.size() - 1);
    while (buttonAttachments_.size() > 0)
        (void)buttonAttachments_.removeAndReturn(buttonAttachments_.size() - 1);
    while (paramAttachments_.size() > 0)
        (void)paramAttachments_.removeAndReturn(paramAttachments_.size() - 1);
}

} // namespace synth
