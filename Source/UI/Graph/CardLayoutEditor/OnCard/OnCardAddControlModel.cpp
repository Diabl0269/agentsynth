// OnCardAddControlModel.cpp -- the controls the Add control panel offers, their search and count, and the
// layout edit that puts one on the card.
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardLayoutQuickEdit.h"
#include "UI/Layout/SearchMatch.h"
#include <algorithm>

namespace synth::ui {

std::vector<AddableControl> addableControls(const synth::CardBody& body) {
    std::vector<AddableControl> controls;
    const auto& plan = body.getPlan();
    for (int index : plan.more) {
        const auto& item = plan.items[(size_t)index];
        if (item.param != nullptr) {
            const auto originalName = item.param->getName(100);
            const auto captionName = item.captionText();
            // If there's a custom label, show "OriginalName (CustomLabel)"; otherwise just the name.
            const auto displayName =
                originalName != captionName ? (originalName + " (" + captionName + ")") : captionName;
            controls.push_back({item.param->paramID, displayName});
        }
    }
    return controls;
}

std::vector<AddableControl> matchingControls(const std::vector<AddableControl>& controls, const juce::String& query) {
    std::vector<std::pair<int, AddableControl>> scored;
    for (const auto& control : controls)
        if (const int score = searchScore(control.name, query); score >= 0)
            scored.emplace_back(score, control);
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<AddableControl> matches;
    for (auto& entry : scored)
        matches.push_back(std::move(entry.second));
    return matches;
}

juce::String addCountText(int shown, int total, const juce::String& query) {
    const auto noun = total == 1 ? "hidden control" : "hidden controls";
    if (query.trim().isEmpty())
        return juce::String(total) + " " + noun;
    return shown == 0 ? juce::String("No control matches")
                      : juce::String(shown) + " of " + juce::String(total) + " " + noun;
}

int lastGridSectionIndex(const CardLayout& layout) {
    for (int i = (int)layout.sections.size(); --i >= 0;) {
        const auto& section = layout.sections[(size_t)i];
        if (section.id != CardSection::kFooterId && section.presentation == CardPresentation::Grid)
            return i;
    }
    return -1;
}

int planSectionIndexOf(const CardLayout& layout, int index) {
    int planIndex = 0;
    for (int i = 0; i < index; ++i)
        if (layout.sections[(size_t)i].id != CardSection::kFooterId)
            ++planIndex;
    return planIndex;
}

int layoutSectionIndexOfPlan(const CardLayout& layout, int planIndex) {
    int seen = 0;
    for (int i = 0; i < (int)layout.sections.size(); ++i)
        if (layout.sections[(size_t)i].id != CardSection::kFooterId && seen++ == planIndex)
            return i;
    return -1;
}

namespace {

// Takes `paramId`'s item out of every section and answers it, or a fresh one when the layout never had it.
CardParamItem takeItem(CardLayout& layout, const juce::String& paramId) {
    CardParamItem found;
    found.paramId = paramId;
    for (auto& section : layout.sections)
        std::erase_if(section.items, [&](const CardItem& item) {
            const auto* param = std::get_if<CardParamItem>(&item);
            if (param == nullptr || param->paramId != paramId)
                return false;
            found = *param;
            return true;
        });
    return found;
}

} // namespace

CardLayout withControlAdded(CardLayout layout, const juce::String& paramId, std::optional<juce::Point<int>> at,
                            int section, const CardLayout* codeDefault) {
    layout.hidden.removeString(paramId);
    if (section < 0 || section >= (int)layout.sections.size())
        section = homeSectionIndex(layout, codeDefault, paramId);
    if (section < 0)
        section = lastGridSectionIndex(layout);
    if (section < 0) {
        CardSection main;
        main.id = "main";
        layout.sections.insert(layout.sections.begin(), std::move(main));
        section = 0;
    }
    for (auto& entry : layout.sections[(size_t)section].items)
        if (auto* listed = std::get_if<CardParamItem>(&entry); listed != nullptr && listed->paramId == paramId) {
            listed->at = at;
            return layout;
        }
    auto item = takeItem(layout, paramId);
    item.at = at;
    layout.sections[(size_t)section].items.emplace_back(std::move(item));
    return layout;
}

} // namespace synth::ui
