// CardBodyPlan.cpp -- what a card body shows and where: the parameter widgets a module gets (with the
// skip rules for parameters edited somewhere else on the card), and their placement from the resolved
// layout or, without one, the automatic layout that reproduces the generic card: sections (the footer
// last), swap groups and code dim rules. Conditions are read in CardBodyConditions.cpp.
// docs/layout/module-card-layout.md#rendering.
#include "CardBodyPlan.h"
#include "CardBodyGeometry.h"
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
// three curve amounts are edited on the envelope graph's bend handles. A parameter a registered view
// edits (the Threshold slider inside the Threshold view) gets no knob either. The ADSR's tempoSync and
// four note divisions are ordinary items: a layout swaps each division in for its time with a condition
// on tempoSync (one mechanism, the layout's), and the automatic layout shows them as plain controls.
bool isEditedElsewhere(juce::AudioProcessor& module, const juce::RangedAudioParameter& param) {
    const auto& id = param.paramID;
    if (dynamic_cast<const juce::AudioParameterBool*>(&param) != nullptr &&
        (id == "bypassed" || id == "muted" || id == "dualIO"))
        return true;
    if (isAdsr(module)) {
        if (id == "attackCurve" || id == "decayCurve" || id == "releaseCurve")
            return true;
    }
    if (const auto* threshold = findCardViewFactory(CardView::Threshold);
        threshold != nullptr && cardViewAvailableFor(CardView::Threshold, module))
        return id.isNotEmpty() && id == threshold->ownedParamId(module);
    return false;
}

// A segmented switch shows every value at once, so it only suits a few short ones; longer lists stay
// a combo.
constexpr int kMaxSegments = 6;
constexpr int kMaxSegmentChars = 10;
// A stepper walks one value at a time, so it only suits a small integer range.
constexpr int kMaxStepperSpan = 24;

bool isBool(const juce::RangedAudioParameter& param) {
    return dynamic_cast<const juce::AudioParameterBool*>(&param) != nullptr;
}

// A bool is a switch only when its module names its two states (the ADSR's Tempo Sync reads "Time" and
// "Tempo"): a plain "Off" and "On" is what a toggle already says, so it stays a toggle.
bool statesAreNamed(const juce::StringArray& values) {
    return !(values[0].equalsIgnoreCase("Off") && values[1].equalsIgnoreCase("On"));
}

bool suitsSegmented(const juce::RangedAudioParameter& param) {
    const auto values = cardBodySegmentLabels(param);
    if (isBool(param) && !(values.size() == 2 && statesAreNamed(values)))
        return false;
    if (values.size() < 2 || values.size() > kMaxSegments)
        return false;
    for (const auto& value : values)
        if (value.length() > kMaxSegmentChars)
            return false;
    return true;
}

bool suitsStepper(const juce::AudioParameterInt& param) {
    const auto range = param.getRange();
    return range.getLength() > 0 && range.getLength() <= kMaxStepperSpan;
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
    for (int index : section.items)
        plan.items[(size_t)index].section = 0;
    plan.sections.push_back(std::move(section));
}

// The height a kind's cell takes in its run (caption included): a swap group takes its tallest
// member's run, so every member fits the one cell whichever is shown.
int cellHeight(CardBodyItem::Kind kind) {
    using Kind = CardBodyItem::Kind;
    switch (kind) {
    case Kind::FaderV:
        return cardbody::kLabelHeight + cardbody::kFaderVHeight;
    case Kind::KnobLarge:
        return cardbody::kLabelHeight + cardbody::kKnobLargeHeight;
    case Kind::Knob:
        return cardbody::kLabelHeight + cardbody::kKnobHeight;
    case Kind::FaderH:
        return cardbody::kLabelHeight + cardbody::kFaderHHeight;
    case Kind::Choice:
    case Kind::Segmented:
    case Kind::Stepper:
        return cardbody::kLabelHeight + cardbody::kRowHeight;
    case Kind::Toggle:
        return cardbody::kRowHeight;
    case Kind::View:
        return 0;
    }
    return 0;
}

// A footer is one compact row: a toggle is a pill, anything continuous a horizontal fader.
void applyFooterKind(CardBodyItem& item) {
    if (item.kind == CardBodyItem::Kind::Toggle)
        item.pill = true;
    else if (isContinuousKind(item.kind))
        item.kind = CardBodyItem::Kind::FaderH;
}

// A choice that swaps with a vertical fader in one cell (the ADSR's note division in place of its time) is
// drawn as a vertical stepped fader over the choice's steps: a combo has no room in a 40 px wide, tall fader
// cell (its text truncates), while a fader's value box shows the choice's text whole. Called as each member
// joins the group, so the order of fader and choice does not matter.
void promoteChoicesBesideFaders(CardBodyPlan& plan, const CardBodyPlan::SwapGroup& group) {
    bool hasFader = false;
    for (int member : group.members)
        hasFader = hasFader || plan.items[(size_t)member].kind == CardBodyItem::Kind::FaderV;
    if (!hasFader)
        return;
    for (int member : group.members)
        if (auto& item = plan.items[(size_t)member]; item.kind == CardBodyItem::Kind::Choice)
            item.kind = CardBodyItem::Kind::FaderV;
}

// Placement state while one section is read: consecutive `show` items testing the same parameter join
// one swap group.
struct SectionPlacer {
    CardBodyPlan& plan;
    CardBodyPlan::Section& section;
    int sectionIndex;
    juce::String openGroupParam; // the condition parameter of the group still open, or empty

    // True when a member of the open group already shows under `source`'s own condition: two members
    // that show together are not alternatives for one cell, so `source` starts the next group (the ADSR's
    // four time/division pairs, all testing tempoSync, are four groups).
    bool repeatsMember(const CardParamItem& source) const {
        for (int member : plan.swapGroups.back().members) {
            const auto& when = plan.items[(size_t)member].when;
            if (when.has_value() && when->is == source.when->is)
                return true;
        }
        return false;
    }

    void place(int index, const CardParamItem& source) {
        auto& planned = plan.items[(size_t)index];
        planned.section = sectionIndex;
        planned.when = source.when;
        if (section.footer)
            applyFooterKind(planned);
        section.items.push_back(index);
        if (!source.when.has_value() || source.when->effect != CardConditionEffect::Show) {
            openGroupParam.clear();
            return;
        }
        if (openGroupParam.isEmpty() || openGroupParam != source.when->param || repeatsMember(source)) {
            plan.swapGroups.push_back({});
            openGroupParam = source.when->param;
        }
        auto& group = plan.swapGroups.back();
        group.members.push_back(index);
        promoteChoicesBesideFaders(plan, group);
        group.span = std::max(group.span, juce::jlimit(1, 6, source.span));
        if (group.members.size() == 1 || cellHeight(planned.kind) > cellHeight(group.cellKind))
            group.cellKind = planned.kind;
        planned.swapGroup = (int)plan.swapGroups.size() - 1;
    }
};

// One layout item: a parameter (its widget, caption, and place unless hidden) or a view. An id the
// module does not have, or one named a second time, is ignored.
void placeItem(juce::AudioProcessor& module, const CardLayout& layout, const CardItem& item, SectionPlacer& placer,
               std::set<int>& named) {
    auto& plan = placer.plan;
    if (const auto* p = std::get_if<CardParamItem>(&item)) {
        const int index = plan.findParam(p->paramId);
        if (index < 0 || !named.insert(index).second)
            return;
        // The widget applies to a hidden parameter too: it shows that way in the More row.
        auto& planned = plan.items[(size_t)index];
        planned.kind = cardBodyKindFor(*planned.param, p->widget).value_or(planned.kind);
        if (p->label.has_value() && p->label->trim().isNotEmpty())
            planned.caption = p->label->trim();
        if (!layout.hidden.contains(p->paramId))
            placer.place(index, *p);
        else
            named.erase(index);
    } else if (const auto* v = std::get_if<CardViewItem>(&item)) {
        placer.openGroupParam.clear();
        if (const int view = addViewItem(module, v->view, plan); view >= 0) {
            plan.items[(size_t)view].open = v->open;
            plan.items[(size_t)view].section = placer.sectionIndex;
            placer.section.items.push_back(view);
        }
    }
}

// The Poly toggle is card chrome on a card with a footer: unless the layout places or hides it, it
// joins the footer row (as Show Scope does) instead of the More row.
void placePolyInFooter(const CardLayout& layout, CardBodyPlan& plan, const std::set<int>& named) {
    const int poly = plan.findParam("poly");
    if (poly < 0 || named.count(poly) > 0 || layout.hidden.contains("poly"))
        return;
    for (int s = 0; s < (int)plan.sections.size(); ++s) {
        auto& section = plan.sections[(size_t)s];
        if (!section.footer)
            continue;
        SectionPlacer placer{plan, section, s, {}};
        placer.place(poly, CardParamItem{});
        return;
    }
}

// Sections in layout order, except that a footer section (CardSection::kFooterId) always comes last,
// wherever the layout lists it. A parameter listed in `hidden`, named nowhere, or named a second time
// goes to the More row.
void placeFromLayout(juce::AudioProcessor& module, const CardLayout& layout, CardBodyPlan& plan) {
    std::vector<const CardSection*> order;
    for (const auto& source : layout.sections)
        if (source.id != CardSection::kFooterId)
            order.push_back(&source);
    for (const auto& source : layout.sections)
        if (source.id == CardSection::kFooterId)
            order.push_back(&source);

    std::set<int> named;
    for (const auto* source : order) {
        CardBodyPlan::Section section;
        section.columns = juce::jlimit(1, 6, source->columns);
        section.title = source->title;
        section.visibleWhen = source->visibleWhen;
        section.footer = source->id == CardSection::kFooterId;
        plan.sections.push_back(std::move(section));
        const int sectionIndex = (int)plan.sections.size() - 1;
        SectionPlacer placer{plan, plan.sections.back(), sectionIndex, {}};
        for (const auto& item : source->items)
            placeItem(module, layout, item, placer, named);
    }
    placePolyInFooter(layout, plan, named);
    for (int i = 0; i < (int)plan.items.size(); ++i)
        if (plan.items[(size_t)i].kind != CardBodyItem::Kind::View && named.count(i) == 0 &&
            plan.items[(size_t)i].section < 0)
            plan.more.push_back(i);
}

void bindDimRules(juce::AudioProcessor& module, const std::vector<CardDimRule>& rules, CardBodyPlan& plan) {
    for (const auto& rule : rules) {
        const int index = plan.findParam(rule.paramId);
        if (index < 0 || !rule.dims)
            continue;
        CardBodyPlan::DimRule bound;
        bound.item = index;
        bound.dims = rule.dims;
        for (const auto& id : rule.watched)
            if (auto* param = findParameterByID(&module, id))
                bound.watched.push_back(param);
        plan.dimRules.push_back(std::move(bound));
    }
}

} // namespace

CardBodyPlan CardBodyPlan::forModule(juce::AudioProcessor& module, const std::optional<CardLayout>& layout,
                                     const std::vector<CardDimRule>& dimRules) {
    CardBodyPlan plan;
    addParameterItems(module, plan);
    if (layout.has_value() && !layout->sections.empty() && cardBodyLayoutIsDataDriven(module)) {
        placeFromLayout(module, *layout, plan);
        bindDimRules(module, dimRules, plan);
        plan.evaluateConditions(module);
    } else {
        placeAutomatically(module, plan);
    }
    return plan;
}

juce::StringArray cardBodySegmentLabels(const juce::RangedAudioParameter& param) {
    if (const auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(&param))
        return choice->choices;
    if (isBool(param))
        return {param.getText(0.0f, 100), param.getText(1.0f, 100)};
    return {};
}

bool isContinuousKind(CardBodyItem::Kind kind) {
    using Kind = CardBodyItem::Kind;
    return kind == Kind::Knob || kind == Kind::KnobLarge || kind == Kind::FaderV || kind == Kind::FaderH;
}

std::optional<CardBodyItem::Kind> cardBodyKindFor(const juce::RangedAudioParameter& param, CardWidget widget) {
    using Kind = CardBodyItem::Kind;
    const auto automatic = kindFor(param);
    const bool continuous = automatic == Kind::Knob;
    const auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(&param);
    const auto* integer = dynamic_cast<const juce::AudioParameterInt*>(&param);
    switch (widget) {
    case CardWidget::Knob:
        return continuous ? Kind::Knob : automatic;
    case CardWidget::KnobLarge:
        return continuous ? Kind::KnobLarge : automatic;
    case CardWidget::FaderV:
        return continuous ? Kind::FaderV : automatic;
    case CardWidget::FaderH:
        return continuous ? Kind::FaderH : automatic;
    case CardWidget::Segmented:
        return (choice != nullptr || isBool(param)) && suitsSegmented(param) ? Kind::Segmented : automatic;
    case CardWidget::Stepper:
        return integer != nullptr && suitsStepper(*integer) ? Kind::Stepper : automatic;
    case CardWidget::Auto:
    case CardWidget::Toggle:
    case CardWidget::Choice:
        return automatic;
    }
    return automatic;
}

juce::String CardBodyItem::captionText() const {
    if (caption.has_value())
        return *caption;
    return param != nullptr ? param->getName(100) : juce::String();
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
