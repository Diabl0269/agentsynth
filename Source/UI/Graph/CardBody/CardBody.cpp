// CardBody.cpp -- building a card body's widgets and keeping their bindings: one widget per plan item,
// created exactly as the generic card always created it (same widget class, componentID, style,
// attachment, MIDI Learn registration and modulation-amount gesture), plus the views and teardown.
// Layout is CardBodyLayout.cpp; the More row is CardBodyMoreRow.cpp.
#include "CardBody.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyMoreButton.h"
#include "CardBodyViews.h"
#include "CardLayoutOverride.h"
#include "ModuleCardLayoutResolver.h"
#include "Modules/ModuleBase.h"
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

CardBody::CardBody(ModuleComponent& card, juce::AudioProcessor& module, const std::optional<CardLayout>& layout)
    : card_(card)
    , module_(module)
    , plan_(CardBodyPlan::forModule(module, layout)) {}

CardBody::~CardBody() = default;

// The layout chain (docs/layout/module-card-layout.md#where-a-layout-comes-from): the node's own
// override, then the code default, then automatic. No per-type store is wired into the app yet, so
// that step is skipped. A card built on a processor that is not a graph node (tests) has no
// override, which resolves to the automatic layout.
std::unique_ptr<CardBody> CardBody::createFor(ModuleComponent& card, juce::AudioProcessor& module,
                                              const juce::AudioProcessorGraph& graph,
                                              juce::AudioProcessorGraph::NodeID nodeId) {
    if (!cardBodyBuildsWidgetsFor(module))
        return nullptr;
    juce::StringArray paramIds;
    for (auto* param : module.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            paramIds.add(ranged->paramID);
    const auto resolved =
        resolveModuleCardLayout(AIStateMapper::getFactoryTypeName(&module), getCardLayoutOverride(graph, nodeId),
                                nullptr, DefaultCardLayouts::builtIn(), &paramIds);
    return std::make_unique<CardBody>(card, module, resolved.layout);
}

void CardBody::createViews() {
    for (auto& item : plan_.items) {
        if (item.kind != CardBodyItem::Kind::View)
            continue;
        if (const auto* factory = findCardViewFactory(item.view)) {
            if (auto view = factory->create(module_)) {
                card_.addAndMakeVisible(view.get());
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
        else if (item.kind == CardBodyItem::Kind::Knob)
            createKnob(item, *item.param);
        else if (item.kind == CardBodyItem::Kind::Toggle)
            createToggle(item, *static_cast<juce::AudioParameterBool*>(item.param));
    }
    if (hasMoreRow())
        createMoreButton();
    applyMoreVisibility();
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

    auto* label = new juce::Label(param.getName(100), param.getName(100));
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
    card_.wireCardKnobModAmountGesture(*knob, &param);
    sliderAttachments_.add(new juce::SliderParameterAttachment(param, *knob));
    if (isFloat)
        applyAdsrTimeSkew(*knob, param);
    card_.sliderParams.add(&param);

    auto* label = new juce::Label(param.getName(100), param.getName(100));
    widgets_.add(label);
    card_.sliderLabels.add(label);
    label->setJustificationType(juce::Justification::centred);
    card_.addAndMakeVisible(label);
    item.widget = knob;
    item.label = label;
}

// A plain juce::ToggleButton toggles on a right click too, so a learnable toggle is right-click safe.
void CardBody::createToggle(CardBodyItem& item, juce::AudioParameterBool& param) {
    auto* toggle = new LearnableToggle(param.getName(100));
    widgets_.add(toggle);
    card_.toggles.add(toggle);
    toggle->setComponentID(param.getName(100));
    card_.addAndMakeVisible(toggle);
    toggle->addMouseListener(&card_, false);
    card_.registerMidiLearnable(*toggle, &param);
    buttonAttachments_.add(new juce::ButtonParameterAttachment(param, *toggle));
    item.widget = toggle;
}

juce::Component* CardBody::findWidget(const juce::String& paramId) const {
    const int index = plan_.findParam(paramId);
    return index >= 0 ? plan_.items[(size_t)index].widget : nullptr;
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
// attachment then touches freed memory, so they are released (leaked) instead.
void CardBody::releaseBindings(bool processorAlive) {
    if (processorAlive) {
        sliderAttachments_.clear();
        comboAttachments_.clear();
        buttonAttachments_.clear();
        return;
    }
    while (sliderAttachments_.size() > 0)
        (void)sliderAttachments_.removeAndReturn(sliderAttachments_.size() - 1);
    while (comboAttachments_.size() > 0)
        (void)comboAttachments_.removeAndReturn(comboAttachments_.size() - 1);
    while (buttonAttachments_.size() > 0)
        (void)buttonAttachments_.removeAndReturn(buttonAttachments_.size() - 1);
}

} // namespace synth
