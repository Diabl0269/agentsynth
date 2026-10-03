// AdsrTimeTempo.cpp -- converting an ADSR layout between its Shared stages (a time and its division swap in
// one cell by tempoSync) and its Separate "Time" and "Tempo" groups. Items are copied from the source and
// only their condition and `at` change, so labels, widgets, spans and ranges survive a round trip.
// docs/layout/module-card-layout.md#default-layouts.
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include <algorithm>

namespace synth {

namespace {

using namespace cardlayout;

constexpr const char* kTempoSync = "tempoSync";
constexpr const char* kSharedId = "stages";
constexpr const char* kTimeId = "stages-time";
constexpr const char* kTempoId = "stages-tempo";

bool isDivision(const juce::String& id) { return id.endsWith("Div"); }

bool isTime(const juce::String& id) {
    return id == "attack" || id == "hold" || id == "decay" || id == "sustain" || id == "release";
}

// A parameter's time id ("attack" for "attackDiv") and its division id.
juce::String divisionOf(const juce::String& time) { return time + "Div"; }

const CardParamItem* paramOf(const CardItem& item) { return std::get_if<CardParamItem>(&item); }

CardItem unplaced(CardItem item) {
    std::visit([](auto& i) { i.at = std::nullopt; }, item);
    return item;
}

CardParamItem withoutCondition(CardParamItem item) {
    item.at = std::nullopt;
    item.when = std::nullopt;
    return item;
}

bool swapsOnTempoSync(const CardItem& item) {
    const auto* param = paramOf(item);
    return param != nullptr && param->when.has_value() && param->when->param == kTempoSync &&
           param->when->effect == CardConditionEffect::Show;
}

int indexOfSection(const CardLayout& layout, const juce::String& id) {
    for (int i = 0; i < (int)layout.sections.size(); ++i)
        if (layout.sections[(size_t)i].id == id)
            return i;
    return -1;
}

int indexOfSharedSection(const CardLayout& layout) {
    for (int i = 0; i < (int)layout.sections.size(); ++i)
        if (std::any_of(layout.sections[(size_t)i].items.begin(), layout.sections[(size_t)i].items.end(),
                        swapsOnTempoSync))
            return i;
    return -1;
}

// The stages as two groups: the times, undimmed items and anything else the user put in the
// group go to "Time" (each time dimmed once Tempo is on), the divisions to "Tempo" (dimmed until it is).
CardLayout toSeparate(CardLayout layout, int index) {
    const auto stages = layout.sections[(size_t)index];
    std::vector<CardItem> time, tempo;
    for (const auto& item : stages.items) {
        const auto* param = paramOf(item);
        if (param == nullptr)
            time.push_back(unplaced(item));
        else if (isDivision(param->paramId))
            tempo.emplace_back(dimUnless(withoutCondition(*param), kTempoSync, {"true"}));
        else if (isTime(param->paramId))
            time.emplace_back(dimUnless(withoutCondition(*param), kTempoSync, {"false"}));
        else
            time.push_back(unplaced(item));
    }
    auto timeSection = stages;
    auto tempoSection = stages;
    timeSection.id = kTimeId;
    timeSection.title = "Time";
    timeSection.items = std::move(time);
    tempoSection.id = kTempoId;
    tempoSection.title = "Tempo";
    tempoSection.items = std::move(tempo);
    layout.sections[(size_t)index] = std::move(timeSection);
    layout.sections.insert(layout.sections.begin() + index + 1, std::move(tempoSection));
    return layout;
}

// The param item named `id` in `items`, or null.
const CardParamItem* findParam(const std::vector<CardItem>& items, const juce::String& id) {
    for (const auto& item : items)
        if (const auto* param = paramOf(item); param != nullptr && param->paramId == id)
            return param;
    return nullptr;
}

bool isDimmedByTempoSync(const CardParamItem& item) {
    return item.when.has_value() && item.when->param == kTempoSync && item.when->effect == CardConditionEffect::Dim;
}

// Back to one group: each time item takes its division beside it as a swap pair (the pair is two items
// repeating one test, so a time with no division left stays plain). A division with no time goes last.
std::vector<CardItem> sharedItems(const std::vector<CardItem>& time, const std::vector<CardItem>& tempo) {
    std::vector<CardItem> out;
    juce::StringArray used;
    for (const auto& item : time) {
        const auto* param = paramOf(item);
        if (param == nullptr || !isDimmedByTempoSync(*param)) {
            out.push_back(unplaced(item));
            continue;
        }
        const auto* division = findParam(tempo, divisionOf(param->paramId));
        if (division == nullptr || param->paramId == "sustain") {
            out.emplace_back(withoutCondition(*param));
            continue;
        }
        out.emplace_back(showWhen(withoutCondition(*param), kTempoSync, {"false"}));
        out.emplace_back(showWhen(withoutCondition(*division), kTempoSync, {"true"}));
        used.add(division->paramId);
    }
    for (const auto& item : tempo) {
        const auto* param = paramOf(item);
        if (param == nullptr || !used.contains(param->paramId))
            out.push_back(param != nullptr ? CardItem(withoutCondition(*param)) : unplaced(item));
    }
    return out;
}

CardLayout toShared(CardLayout layout, int timeIndex, int tempoIndex) {
    const int keep = timeIndex >= 0 ? timeIndex : tempoIndex;
    const std::vector<CardItem> none;
    const auto& time = timeIndex >= 0 ? layout.sections[(size_t)timeIndex].items : none;
    const auto& tempo = tempoIndex >= 0 ? layout.sections[(size_t)tempoIndex].items : none;
    auto stages = layout.sections[(size_t)keep];
    stages.id = kSharedId;
    stages.title = std::nullopt;
    stages.items = sharedItems(time, tempo);
    layout.sections[(size_t)keep] = std::move(stages);
    if (timeIndex >= 0 && tempoIndex >= 0)
        layout.sections.erase(layout.sections.begin() + tempoIndex);
    return layout;
}

} // namespace

bool hasAdsrTimeTempo(const juce::String& moduleType) {
    return moduleType == "ADSR" || moduleType == "Amp Env" || moduleType == "Filter Env";
}

std::optional<AdsrTimeTempo> adsrTimeTempoOf(const CardLayout& layout) {
    if (indexOfSection(layout, kTimeId) >= 0 || indexOfSection(layout, kTempoId) >= 0)
        return AdsrTimeTempo::Separate;
    if (indexOfSharedSection(layout) >= 0)
        return AdsrTimeTempo::Shared;
    return std::nullopt;
}

CardLayout withAdsrTimeTempo(CardLayout layout, AdsrTimeTempo mode) {
    const auto current = adsrTimeTempoOf(layout);
    if (!current.has_value() || *current == mode)
        return layout;
    if (mode == AdsrTimeTempo::Separate) {
        const int shared = indexOfSharedSection(layout);
        return toSeparate(std::move(layout), shared);
    }
    const int time = indexOfSection(layout, kTimeId);
    const int tempo = indexOfSection(layout, kTempoId);
    return toShared(std::move(layout), time, tempo);
}

} // namespace synth
