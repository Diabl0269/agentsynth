// CardBodyPlan.cpp -- what a card body shows and where: the parameter widgets a module gets (with the
// skip rules for parameters edited somewhere else on the card), and their placement from the resolved
// layout or, without one, the automatic layout that reproduces the generic card.
// docs/layout/module-card-layout.md#rendering.
#include "CardBodyPlan.h"
#include "CardBodyViews.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include <set>

namespace synth {

namespace {

ModuleType typeOf(juce::AudioProcessor& module) {
    if (auto* mb = dynamic_cast<ModuleBase*>(&module))
        return mb->getModuleType();
    return ModuleType::Oscillator;
}

bool isAdsr(juce::AudioProcessor& module) { return typeOf(module) == ModuleType::ADSR; }

// Parameters with no widget of their own. bypassed/muted/dualIO are the header buttons. The ADSR's
// three curve amounts are edited on the envelope graph's bend handles, its four note divisions by the
// pickers the envelope card swaps in over each time knob in BPM mode, and its tempoSync by the card's
// MS|BPM switch. A parameter a registered view edits (the Threshold slider inside the Threshold view)
// gets no knob either.
bool isEditedElsewhere(juce::AudioProcessor& module, const juce::RangedAudioParameter& param) {
    const auto& id = param.paramID;
    if (dynamic_cast<const juce::AudioParameterBool*>(&param) != nullptr &&
        (id == "bypassed" || id == "muted" || id == "dualIO"))
        return true;
    if (isAdsr(module)) {
        if (id == "attackCurve" || id == "decayCurve" || id == "releaseCurve" || id == "tempoSync")
            return true;
        if (id == "attackDiv" || id == "holdDiv" || id == "decayDiv" || id == "releaseDiv")
            return true;
    }
    if (const auto* threshold = findCardViewFactory(CardView::Threshold);
        threshold != nullptr && cardViewAvailableFor(CardView::Threshold, module))
        return id.isNotEmpty() && id == threshold->ownedParamId(module);
    return false;
}

// A widget kind per JUCE parameter type, exactly as the generic card always chose: a choice is a combo,
// a float or int a knob, a bool a toggle. Anything else gets no widget.
std::optional<CardBodyItem::Kind> kindFor(const juce::RangedAudioParameter& param) {
    if (dynamic_cast<const juce::AudioParameterChoice*>(&param) != nullptr)
        return CardBodyItem::Kind::Choice;
    if (dynamic_cast<const juce::AudioParameterFloat*>(&param) != nullptr ||
        dynamic_cast<const juce::AudioParameterInt*>(&param) != nullptr)
        return CardBodyItem::Kind::Knob;
    if (dynamic_cast<const juce::AudioParameterBool*>(&param) != nullptr)
        return CardBodyItem::Kind::Toggle;
    return std::nullopt;
}

void addParameterItems(juce::AudioProcessor& module, CardBodyPlan& plan) {
    for (auto* base : module.getParameters()) {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(base);
        if (param == nullptr || isEditedElsewhere(module, *param))
            continue;
        if (const auto kind = kindFor(*param)) {
            CardBodyItem item;
            item.kind = *kind;
            item.param = param;
            plan.items.push_back(item);
        }
    }
}

int addViewItem(juce::AudioProcessor& module, CardView view, CardBodyPlan& plan) {
    if (!cardViewAvailableFor(view, module))
        return -1;
    for (int i = 0; i < (int)plan.items.size(); ++i)
        if (plan.items[(size_t)i].kind == CardBodyItem::Kind::View && plan.items[(size_t)i].view == view)
            return -1; // a view shows once
    CardBodyItem item;
    item.kind = CardBodyItem::Kind::View;
    item.view = view;
    plan.items.push_back(item);
    return (int)plan.items.size() - 1;
}

// One grid section: every combo, then every toggle, then the Threshold view, then every knob, each
// group in declaration order -- the generic card exactly.
void placeAutomatically(juce::AudioProcessor& module, CardBodyPlan& plan) {
    CardBodyPlan::Section section;
    const int paramCount = (int)plan.items.size();
    for (auto kind : {CardBodyItem::Kind::Choice, CardBodyItem::Kind::Toggle}) {
        for (int i = 0; i < paramCount; ++i)
            if (plan.items[(size_t)i].kind == kind)
                section.items.push_back(i);
    }
    if (const int view = addViewItem(module, CardView::Threshold, plan); view >= 0)
        section.items.push_back(view);
    for (int i = 0; i < paramCount; ++i)
        if (plan.items[(size_t)i].kind == CardBodyItem::Kind::Knob)
            section.items.push_back(i);
    plan.sections.push_back(std::move(section));
}

// A parameter listed in `hidden`, named nowhere, or named a second time goes to the More row; an id
// the module does not have (or that is edited elsewhere) is ignored.
void placeFromLayout(juce::AudioProcessor& module, const CardLayout& layout, CardBodyPlan& plan) {
    std::set<int> placed;
    for (const auto& source : layout.sections) {
        CardBodyPlan::Section section;
        section.columns = juce::jlimit(1, 6, source.columns);
        for (const auto& item : source.items) {
            if (const auto* p = std::get_if<CardParamItem>(&item)) {
                const int index = plan.findParam(p->paramId);
                if (index < 0 || layout.hidden.contains(p->paramId) || !placed.insert(index).second)
                    continue;
                section.items.push_back(index);
            } else if (const auto* v = std::get_if<CardViewItem>(&item)) {
                if (const int view = addViewItem(module, v->view, plan); view >= 0)
                    section.items.push_back(view);
            }
        }
        plan.sections.push_back(std::move(section));
    }
    for (int i = 0; i < (int)plan.items.size(); ++i)
        if (plan.items[(size_t)i].kind != CardBodyItem::Kind::View && placed.count(i) == 0)
            plan.more.push_back(i);
}

} // namespace

CardBodyPlan CardBodyPlan::forModule(juce::AudioProcessor& module, const std::optional<CardLayout>& layout) {
    CardBodyPlan plan;
    addParameterItems(module, plan);
    if (layout.has_value() && !layout->sections.empty() && cardBodyLayoutIsDataDriven(module))
        placeFromLayout(module, *layout, plan);
    else
        placeAutomatically(module, plan);
    return plan;
}

int CardBodyPlan::findParam(const juce::String& paramId) const {
    for (int i = 0; i < (int)items.size(); ++i)
        if (items[(size_t)i].param != nullptr && items[(size_t)i].param->paramID == paramId)
            return i;
    return -1;
}

bool cardBodyBuildsWidgetsFor(juce::AudioProcessor& module) {
    return dynamic_cast<MidiKeyboardModule*>(&module) == nullptr &&
           dynamic_cast<ExternalMidiModule*>(&module) == nullptr &&
           dynamic_cast<HostedPluginModule*>(&module) == nullptr;
}

// The bespoke cards lay their widgets out themselves (or page them, the Wavetable), so a stored
// layout never moves anything on them; they always build from the automatic plan.
bool cardBodyLayoutIsDataDriven(juce::AudioProcessor& module) {
    if (!cardBodyBuildsWidgetsFor(module) || dynamic_cast<MacroControlModule*>(&module) != nullptr)
        return false;
    switch (typeOf(module)) {
    case ModuleType::Sequencer:
    case ModuleType::PolySequencer:
    case ModuleType::Attenuverter:
    case ModuleType::ParametricEQ:
    case ModuleType::Wavetable:
    case ModuleType::MacroInlet:
    case ModuleType::MacroOutlet:
    case ModuleType::MacroMidiInlet:
    case ModuleType::MacroMidiOutlet:
        return false;
    default:
        return true;
    }
}

} // namespace synth
