// AdsrTimeTempo.cpp -- converting an ADSR layout between its Shared stages (a time and its division swap in
// one cell by tempoSync) and its Separate Time and Tempo looks (two sections in the same area, one shown by
// each Sync value). Items are copied from the source and only their condition, `at` and a division's fader
// widget change, so labels, spans and ranges survive a round trip.
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
constexpr const char* kSustain = "sustain";

bool isDivision(const juce::String& id) { return id.endsWith("Div"); }

bool isTime(const juce::String& id) {
    return id == "attack" || id == "hold" || id == "decay" || id == kSustain || id == "release";
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

// The test that shows a look: the Time look while the card's Sync is Time (tempoSync false), the Tempo look
// while it is Tempo.
CardCondition lookCondition(bool tempo) { return {kTempoSync, {tempo ? "true" : "false"}, CardConditionEffect::Show}; }

// The stages as two looks in the same area of the card, each shown by the Sync switch (a section's
// visibleWhen): the Time look holds the times and Sustain, the Tempo look the note divisions (drawn as
// faders) and Sustain again, each in the order the stages stand in the shared row.
CardLayout toSeparate(CardLayout layout, int index) {
    const auto stages = layout.sections[(size_t)index];
    std::vector<CardItem> time, tempo;
    for (const auto& item : stages.items) {
        const auto* param = paramOf(item);
        if (param == nullptr) {
            time.push_back(unplaced(item));
        } else if (isDivision(param->paramId)) {
            auto division = withoutCondition(*param);
            if (division.widget == CardWidget::Auto)
                division.widget = CardWidget::FaderV;
            tempo.emplace_back(std::move(division));
        } else {
            time.emplace_back(withoutCondition(*param));
            if (param->paramId == kSustain)
                tempo.emplace_back(withoutCondition(*param));
        }
    }
    auto timeSection = stages;
    auto tempoSection = stages;
    timeSection.id = kTimeId;
    timeSection.title = std::nullopt;
    timeSection.visibleWhen = lookCondition(false);
    timeSection.items = std::move(time);
    tempoSection.id = kTempoId;
    tempoSection.title = std::nullopt;
    tempoSection.visibleWhen = lookCondition(true);
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

bool isTempoSyncDim(const CardParamItem& item) {
    return item.when.has_value() && item.when->param == kTempoSync && item.when->effect == CardConditionEffect::Dim;
}

// A layout saved by the first Separate form (two groups, both always visible, each dimmed while the other
// mode is on, Sustain in the Time group only) read as the current one: each group gets its look's
// visibleWhen, the dims go, and the Tempo group gets the Sustain the Time group has so it stays reachable.
// A layout already in the current form comes back unchanged.
CardLayout currentSeparateForm(CardLayout layout) {
    const int time = indexOfSection(layout, kTimeId);
    const int tempo = indexOfSection(layout, kTempoId);
    for (const auto& [at, tempoLook] : {std::pair{time, false}, std::pair{tempo, true}}) {
        if (at < 0)
            continue;
        auto& section = layout.sections[(size_t)at];
        if (!section.visibleWhen.has_value())
            section.visibleWhen = lookCondition(tempoLook);
        for (auto& item : section.items)
            if (auto* param = std::get_if<CardParamItem>(&item); param != nullptr && isTempoSyncDim(*param))
                param->when = std::nullopt;
    }
    if (time < 0 || tempo < 0)
        return layout;
    const auto* sustain = findParam(layout.sections[(size_t)time].items, kSustain);
    auto& tempoItems = layout.sections[(size_t)tempo].items;
    if (sustain == nullptr || findParam(tempoItems, kSustain) != nullptr)
        return layout;
    const auto copy = CardItem(withoutCondition(*sustain));
    const auto release = std::find_if(tempoItems.begin(), tempoItems.end(), [](const CardItem& item) {
        const auto* param = paramOf(item);
        return param != nullptr && param->paramId == divisionOf("release");
    });
    tempoItems.insert(release, copy);
    return layout;
}

// Back to one group: each time item takes its division beside it as a swap pair (the pair is two items
// repeating one test, so a time with no division left stays plain, and so does Sustain). A division with no
// time goes last. A division's fader widget goes back to Auto: beside a fader a division is one anyway.
std::vector<CardItem> sharedItems(const std::vector<CardItem>& time, const std::vector<CardItem>& tempo) {
    std::vector<CardItem> out;
    juce::StringArray used;
    for (const auto& item : time) {
        const auto* param = paramOf(item);
        if (param == nullptr || !isTime(param->paramId)) {
            out.push_back(unplaced(item));
            continue;
        }
        const auto* division = param->paramId == kSustain ? nullptr : findParam(tempo, divisionOf(param->paramId));
        if (division == nullptr) {
            out.emplace_back(withoutCondition(*param));
            continue;
        }
        auto back = withoutCondition(*division);
        if (back.widget == CardWidget::FaderV)
            back.widget = CardWidget::Auto;
        out.emplace_back(showWhen(withoutCondition(*param), kTempoSync, {"false"}));
        out.emplace_back(showWhen(std::move(back), kTempoSync, {"true"}));
        used.add(division->paramId);
    }
    for (const auto& item : tempo) {
        const auto* param = paramOf(item);
        if (param == nullptr || used.contains(param->paramId) || param->paramId == kSustain)
            continue;
        out.emplace_back(withoutCondition(*param));
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
    stages.visibleWhen = std::nullopt;
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

CardLayout withAdsrSeparateLooks(CardLayout layout) {
    if (adsrTimeTempoOf(layout) != AdsrTimeTempo::Separate)
        return layout;
    return currentSeparateForm(std::move(layout));
}

CardLayout withAdsrTimeTempo(CardLayout layout, AdsrTimeTempo mode) {
    layout = withAdsrSeparateLooks(std::move(layout));
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
