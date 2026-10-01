// CardBodyMeasure.cpp -- a card's size before any component exists (the library drag ghost, drop
// placement), measured from the same card-body plan and layout walk the real card uses, so the
// estimate and the real card agree by construction. The port gutter and the chrome rows around the
// body follow ModuleComponent's own rules (getContentTopY, getPortCenter, the knob-bound jack rule,
// and the rows layoutDefaultContent adds), keyed on the same module predicates its constructor uses.
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
// sliderIndexForModTarget): the knob of its bound parameter, else the knob named like the jack.
bool targetHasShownKnob(const CardBodyPlan& plan, ModuleBase& module, const ModulationTarget& target) {
    const auto* bound = module.parameterForModTarget(target);
    for (const auto& item : plan.items) {
        if (!isContinuousKind(item.kind))
            continue;
        if (bound != nullptr ? item.param == bound : item.param->getName(100) == target.name)
            return true;
    }
    return false;
}

// ModuleComponent::isInputJackKnobBound: the FIRST target on visible jack `jack` decides, through the
// first target sharing its channel. An automatic plan places every knob, so a knob is always shown.
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

// The fresh card's height: port gutter, Sampler chrome, the body, then the rows layoutDefaultContent
// adds below it (the ADSR graph's disclosure row, the Filter's Show Response row, Show Scope). Nullopt
// when the fresh module would open a section this does not model (a remembered view, a Custom LFO).
std::optional<int> measureHeight(ModuleBase& module) {
    const auto view = module.getCardViewState();
    if (view.showScope || view.showResponse)
        return std::nullopt;
    if (dynamic_cast<LFOModule*>(&module) != nullptr)
        if (auto* shape = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&module, "shape"));
            shape != nullptr && shape->getIndex() == LFOModule::kCustomShapeIndex)
            return std::nullopt;

    const auto plan = CardBodyPlan::forModule(module, std::nullopt);
    const auto g = BodyGeometry::forCardWidth(synth::LayoutUtil::kSingleWidth);
    int y = contentTopY(plan, module);
    if (dynamic_cast<SamplerModule*>(&module) != nullptr)
        y += kWaveformHeight + 8 + kRowHeight + 8;
    y = layoutCardBodySections(plan, module, y, g, /*apply*/ false, /*tabbed*/ false);
    if (module.getModuleType() == ModuleType::ADSR)
        y += kRowHeight + 2;
    if (dynamic_cast<FilterModule*>(&module) != nullptr)
        y += kRowHeight + 2;
    if (hasScopeToggle(module))
        y += kRowHeight + 2;
    return std::max(100, y + kBottomPadding);
}

std::optional<juce::Point<int>> measureUncached(const juce::String& typeName) {
    auto processor = AIStateMapper::createModule(typeName);
    auto* module = dynamic_cast<ModuleBase*>(processor.get());
    if (module == nullptr || !cardBodyLayoutIsDataDriven(*module) || module->getModuleType() == ModuleType::AudioInput)
        return std::nullopt;
    if (const auto height = measureHeight(*module))
        return juce::Point<int>(synth::LayoutUtil::kSingleWidth, *height);
    return std::nullopt;
}

} // namespace

std::optional<juce::Point<int>> measureDataDrivenCardSize(const juce::String& typeName) {
    static std::map<juce::String, std::optional<juce::Point<int>>> cache;
    const auto it = cache.find(typeName);
    if (it != cache.end())
        return it->second;
    return cache[typeName] = measureUncached(typeName);
}

} // namespace synth
