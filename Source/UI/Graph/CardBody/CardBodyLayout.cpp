// CardBodyLayout.cpp -- laying a card body out: each section's items in card order, grouped into runs of
// one kind (a combo stack, toggle rows, a knob grid, a full-width view) placed by the shared run layouts.
// One function measures (apply = false) and places (apply = true), so the measured height and the real
// positions cannot drift apart; with null widgets (a plan never built) it measures statically.
#include "CardBody.h"
#include "CardBodyLayoutWalk.h"
#include "CardBodyViews.h"
#include "UI/Graph/CardWidgets/CardFader.h"

namespace synth {

namespace {

using Kind = CardBodyItem::Kind;

// The end of the run of items sharing `indices[from]`'s kind (views are a run of one each).
size_t runEnd(const CardBodyPlan& plan, const std::vector<int>& indices, size_t from) {
    const auto kind = plan.items[(size_t)indices[from]].kind;
    if (kind == Kind::View)
        return from + 1;
    size_t end = from + 1;
    while (end < indices.size() && plan.items[(size_t)indices[end]].kind == kind)
        ++end;
    return end;
}

int layoutRun(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<int>& run, int columns, int y,
              const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    const auto& first = plan.items[(size_t)run.front()];
    std::vector<cardbody::CaptionedWidget> captioned;
    std::vector<juce::Component*> plain;
    for (int index : run) {
        const auto& item = plan.items[(size_t)index];
        captioned.emplace_back(item.widget, item.label);
        plain.push_back(item.widget);
    }
    switch (first.kind) {
    case Kind::Choice:
        return tabbed ? y : cardbody::layoutChoiceRun(captioned, y, g, apply);
    case Kind::Toggle:
        return cardbody::layoutToggleRun(plain, y, g, apply);
    case Kind::Knob:
        return tabbed ? y : cardbody::layoutKnobRun(captioned, columns, y, g, apply);
    case Kind::KnobLarge:
        return tabbed ? y : cardbody::layoutGridRun(captioned, columns, cardbody::kKnobLargeHeight, 0, y, g, apply);
    case Kind::FaderV:
        return tabbed ? y
                      : cardbody::layoutGridRun(captioned, columns, cardbody::kFaderVHeight,
                                                synth::ui::CardFader::kVerticalWidth, y, g, apply);
    case Kind::FaderH:
        return tabbed ? y : cardbody::layoutCaptionedRows(captioned, cardbody::kFaderHHeight, true, y, g, apply);
    case Kind::Segmented:
        return tabbed ? y : cardbody::layoutCaptionedRows(captioned, cardbody::kRowHeight, true, y, g, apply);
    case Kind::Stepper:
        return tabbed ? y : cardbody::layoutCaptionedRows(captioned, cardbody::kRowHeight, false, y, g, apply);
    case Kind::View: {
        const auto* factory = findCardViewFactory(first.view);
        if (factory == nullptr || (apply && first.widget == nullptr))
            return y;
        return cardbody::layoutViewRow(first.widget, factory->preferredHeight(module), y, g, apply);
    }
    }
    return y;
}

} // namespace

int layoutCardBodyItems(const CardBodyPlan& plan, juce::AudioProcessor& module, const std::vector<int>& indices,
                        int columns, int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    for (size_t i = 0; i < indices.size();) {
        const size_t end = runEnd(plan, indices, i);
        const std::vector<int> run(indices.begin() + (long)i, indices.begin() + (long)end);
        y = layoutRun(plan, module, run, columns, y, g, apply, tabbed);
        i = end;
    }
    return y;
}

int layoutCardBodySections(const CardBodyPlan& plan, juce::AudioProcessor& module, int y,
                           const cardbody::BodyGeometry& g, bool apply, bool tabbed) {
    for (const auto& section : plan.sections)
        y = layoutCardBodyItems(plan, module, section.items, section.columns, y, g, apply, tabbed);
    return y;
}

int CardBody::layout(int y, const cardbody::BodyGeometry& g, bool apply, bool tabbed) const {
    return layoutCardBodySections(plan_, module_, y, g, apply, tabbed);
}

int CardBody::layoutItems(const std::vector<int>& indices, int columns, int y, const cardbody::BodyGeometry& g,
                          bool apply, bool tabbed) const {
    return layoutCardBodyItems(plan_, module_, indices, columns, y, g, apply, tabbed);
}

} // namespace synth
