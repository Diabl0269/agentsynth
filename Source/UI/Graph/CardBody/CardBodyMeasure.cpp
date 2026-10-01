// CardBodyMeasure.cpp -- a card's size before any component exists (the library drag ghost, drop
// placement), measured from the same card-body plan and layout walk the real card uses, so the
// estimate and the real card agree by construction. The port gutter and the chrome rows around the
// body follow ModuleComponent's own rules (getContentTopY, getPortCenter, the knob-bound jack rule,
// and the rows layoutDefaultContent adds), keyed on the same module predicates its constructor uses.
// The plan is built from the type's code default (as a fresh card resolves it with no stored layout),
// its conditions read at the fresh module's default values.
// ModuleComponentTest.EstimatedModuleSizesMatchTheRealComponents pins every library type.
#include "CardBodyMeasure.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyLayoutWalk.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/LayoutUtil.h"
#include <map>

namespace synth {

namespace {

using namespace cardbody;

// The knob a modulation target lands on, by the card's own rule (ModuleComponent::
// sliderIndexForModTarget): the knob of its bound parameter, else the knob named like the jack. Only a
// knob on the card counts: placed in a shown section (a swapped-out one keeps its cell), never one in
// the folded More row.
bool targetHasShownKnob(const CardBodyPlan& plan, ModuleBase& module, const ModulationTarget& target) {
    const auto* bound = module.parameterForModTarget(target);
    for (int i = 0; i < (int)plan.items.size(); ++i) {
        const auto& item = plan.items[(size_t)i];
        if (!isContinuousKind(item.kind) || !plan.isOnCard(i))
            continue;
        if (bound != nullptr ? item.param == bound : item.param->getName(100) == target.name)
            return true;
    }
    return false;
}

// ModuleComponent::isInputJackKnobBound: the FIRST target on visible jack `jack` decides, through the
// first target sharing its channel.
bool jackIsKnobBound(const CardBodyPlan& plan, ModuleBase& module, int jack) {
    const auto targets = module.getModulationTargets();
    for (const auto& target : targets) {
        if (module.mapInputChannel(target.channelIndex).visibleJackIndex != jack)
            continue;
        for (const auto& sameChannel : targets)
            if (sameChannel.channelIndex == target.channelIndex)
                return targetHasShownKnob(plan, module, sameChannel);
        return false;
    }
    return false;
}

// ModuleComponent::getContentTopY on a single-width card (one input column).
int contentTopY(const CardBodyPlan& plan, ModuleBase& module) {
    const int header = ModuleComponent::kPortGutterHeaderHeight;
    int y = header + (module.acceptsMidi() ? 30 : 0);
    const int portOffset = module.producesMidi() ? 20 : 0;
    int drawn = 0;
    for (int i = 0; i < module.getVisibleInputPortCount(); ++i)
        if (!jackIsKnobBound(plan, module, i))
            ++drawn;
    if (drawn > 0)
        y = std::max(y, header + portOffset + (drawn - 1) * 20 + 20 + kPortLabelClearance);
    if (const int outs = module.getVisibleOutputPortCount(); outs > 0)
        y = std::max(y, header + portOffset + (outs - 1) * 20 + 20 + kPortLabelClearance);
    return y;
}

bool hasScopeToggle(ModuleBase& module) {
    const auto type = module.getModuleType();
    return module.getVisualBuffer() != nullptr && type != ModuleType::ExternalMidi && type != ModuleType::ParametricEQ;
}

// The chrome toggles a fresh card shows, in footerChromeToggles' order (Show Spectrum stays hidden until
// the response view opens, which a fresh card never has).
std::vector<CardFooterExtra> freshChromeToggles(ModuleBase& module) {
    std::vector<CardFooterExtra> toggles;
    if (dynamic_cast<FilterModule*>(&module) != nullptr)
        toggles.push_back({nullptr, kShowResponseText});
    if (hasScopeToggle(module))
        toggles.push_back({nullptr, kShowScopeText});
    return toggles;
}

// The rows layoutDefaultContent adds below the body: the ADSR graph's disclosure row, then the Filter's
// Show Response row and Show Scope (or, on a card with a footer, the footer row holding them), then the
// folded More row.
int measureRowsBelowBody(const CardBodyPlan& plan, ModuleBase& module, int y, const BodyGeometry& g) {
    if (module.getModuleType() == ModuleType::ADSR)
        y += kRowHeight + 2;
    if (plan.hasFooter()) {
        y = layoutCardBodyFooter(plan, freshChromeToggles(module), y, g, /*apply*/ false);
    } else {
        if (dynamic_cast<FilterModule*>(&module) != nullptr)
            y += kRowHeight + 2;
        if (hasScopeToggle(module))
            y += kRowHeight + 2;
    }
    if (!plan.more.empty())
        y += kRowHeight + 2;
    return y;
}

// The fresh card's height: port gutter, Sampler chrome, the body, then the rows below it. Nullopt when
// the fresh module would open a section this does not model (a remembered view, a Custom LFO).
std::optional<int> measureHeight(ModuleBase& module, const DefaultCardLayouts& defaults) {
    const auto view = module.getCardViewState();
    if (view.showScope || view.showResponse)
        return std::nullopt;
    if (dynamic_cast<LFOModule*>(&module) != nullptr)
        if (auto* shape = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&module, "shape"));
            shape != nullptr && shape->getIndex() == LFOModule::kCustomShapeIndex)
            return std::nullopt;

    const auto* entry = defaults.find(AIStateMapper::getFactoryTypeName(&module));
    const auto plan = entry != nullptr ? CardBodyPlan::forModule(module, entry->layout, entry->dimRules)
                                       : CardBodyPlan::forModule(module, std::nullopt);
    const auto g = BodyGeometry::forCardWidth(synth::LayoutUtil::kSingleWidth);
    int y = contentTopY(plan, module);
    if (dynamic_cast<SamplerModule*>(&module) != nullptr)
        y += kWaveformHeight + 8 + kRowHeight + 8;
    y = layoutCardBodySections(plan, module, y, g, /*apply*/ false, /*tabbed*/ false);
    y = measureRowsBelowBody(plan, module, y, g);
    return std::max(100, y + kBottomPadding);
}

} // namespace

std::optional<juce::Point<int>> measureDataDrivenCardSize(const juce::String& typeName) {
    static std::map<juce::String, std::optional<juce::Point<int>>> cache;
    const auto it = cache.find(typeName);
    if (it != cache.end())
        return it->second;
    return cache[typeName] = measureDataDrivenCardSizeWith(typeName, DefaultCardLayouts::builtIn());
}

std::optional<juce::Point<int>> measureDataDrivenCardSizeWith(const juce::String& typeName,
                                                              const DefaultCardLayouts& defaults) {
    auto processor = AIStateMapper::createModule(typeName);
    auto* module = dynamic_cast<ModuleBase*>(processor.get());
    if (module == nullptr || !cardBodyLayoutIsDataDriven(*module) || module->getModuleType() == ModuleType::AudioInput)
        return std::nullopt;
    if (const auto height = measureHeight(*module, defaults))
        return juce::Point<int>(synth::LayoutUtil::kSingleWidth, *height);
    return std::nullopt;
}

} // namespace synth
