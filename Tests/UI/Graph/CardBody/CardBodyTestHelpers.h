#pragma once

// Shared fixtures for Tests/UI/Graph/CardBody/CardBody*Tests.cpp. Header-only; not registered in
// Tests/CMakeLists.txt.

#include "CardCanvas.h"
#include "UI/Graph/CardBody/CardBody.h"
#include <gtest/gtest.h>
#include <map>

namespace cardbody_test {

/** The automatic layout written out as a stored layout, `widgets` choosing a widget per paramId and
 *  `hidden` moved to the More row. */
inline synth::CardLayout automaticLayoutWith(juce::AudioProcessor& module,
                                             const std::map<juce::String, synth::CardWidget>& widgets,
                                             const juce::StringArray& hidden = {}) {
    const auto plan = synth::CardBodyPlan::forModule(module, std::nullopt);
    synth::CardSection section;
    section.id = "main";
    for (int index : plan.sections.front().items) {
        const auto& item = plan.items[(size_t)index];
        if (item.kind == synth::CardBodyItem::Kind::View) {
            synth::CardViewItem view;
            view.view = item.view;
            section.items.emplace_back(view);
        } else {
            synth::CardParamItem param;
            param.paramId = item.param->paramID;
            if (const auto it = widgets.find(param.paramId); it != widgets.end())
                param.widget = it->second;
            section.items.emplace_back(param);
        }
    }
    synth::CardLayout layout;
    layout.sections = {section};
    layout.hidden = hidden;
    return layout;
}

/** The automatic layout written out as a stored layout, with `hidden` moved to the More row. */
inline synth::CardLayout automaticLayoutHiding(juce::AudioProcessor& module, const juce::StringArray& hidden) {
    return automaticLayoutWith(module, {}, hidden);
}

} // namespace cardbody_test
