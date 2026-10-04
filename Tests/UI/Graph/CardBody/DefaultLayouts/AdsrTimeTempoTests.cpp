// AdsrTimeTempoTests.cpp -- the pure conversions between the ADSR default's Shared stages and its Separate
// Time and Tempo groups (Source/UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.cpp).
// docs/layout/module-card-layout.md#default-layouts.

#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include <gtest/gtest.h>

using namespace synth;

namespace {

CardLayout adsrDefault() { return DefaultCardLayouts::builtIn().find("ADSR")->layout; }

const CardSection* sectionNamed(const CardLayout& layout, const juce::String& id) {
    for (const auto& section : layout.sections)
        if (section.id == id)
            return &section;
    return nullptr;
}

std::vector<CardParamItem> paramsOf(const CardSection& section) {
    std::vector<CardParamItem> out;
    for (const auto& item : section.items)
        out.push_back(std::get<CardParamItem>(item));
    return out;
}

juce::StringArray idsOf(const CardSection& section) {
    juce::StringArray ids;
    for (const auto& item : paramsOf(section))
        ids.add(item.paramId);
    return ids;
}

const CardParamItem* itemIn(const CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<CardParamItem>(&item); param != nullptr && param->paramId == paramId)
                return param;
    return nullptr;
}

juce::StringArray sectionIds(const CardLayout& layout) {
    juce::StringArray ids;
    for (const auto& section : layout.sections)
        ids.add(section.id);
    return ids;
}

} // namespace

TEST(AdsrTimeTempo, TheDefaultIsSharedAndOnlyTheThreeAdsrTypesHaveTheSwitch) {
    EXPECT_EQ(adsrTimeTempoOf(adsrDefault()), AdsrTimeTempo::Shared);
    for (const char* type : {"ADSR", "Amp Env", "Filter Env"})
        EXPECT_TRUE(hasAdsrTimeTempo(type)) << type;
    EXPECT_FALSE(hasAdsrTimeTempo("Filter"));
    EXPECT_FALSE(hasAdsrTimeTempo("LFO"));
}

TEST(AdsrTimeTempo, SeparateNamesTimeAndTempoGroupsInPlaceOfTheStages) {
    const auto shared = adsrDefault();
    const auto separate = withAdsrTimeTempo(shared, AdsrTimeTempo::Separate);
    EXPECT_EQ(adsrTimeTempoOf(separate), AdsrTimeTempo::Separate);
    EXPECT_EQ(sectionIds(separate),
              juce::StringArray({"envelope", "time", "stages-time", "stages-tempo", "trigger", "footer"}));
    EXPECT_EQ(sectionNamed(separate, "stages"), nullptr);

    const auto& time = *sectionNamed(separate, "stages-time");
    const auto& tempo = *sectionNamed(separate, "stages-tempo");
    EXPECT_EQ(time.title, std::optional<juce::String>("Time"));
    EXPECT_EQ(tempo.title, std::optional<juce::String>("Tempo"));
    EXPECT_EQ(idsOf(time), juce::StringArray({"attack", "hold", "decay", "sustain", "release"}));
    EXPECT_EQ(idsOf(tempo), juce::StringArray({"attackDiv", "holdDiv", "decayDiv", "releaseDiv"}));
    EXPECT_FALSE(time.visibleWhen.has_value());
    EXPECT_FALSE(tempo.visibleWhen.has_value());
}

TEST(AdsrTimeTempo, EachGroupDimsItsItemsByTempoSyncAndKeepsTheCaptions) {
    const auto separate = withAdsrTimeTempo(adsrDefault(), AdsrTimeTempo::Separate);
    const CardCondition dimWhenTempo{"tempoSync", {"false"}, CardConditionEffect::Dim};
    const CardCondition dimWhenTime{"tempoSync", {"true"}, CardConditionEffect::Dim};
    const std::pair<const char*, const char*> captions[] = {
        {"attack", "Atk"}, {"hold", "Hold"}, {"decay", "Dec"}, {"sustain", "Sus"}, {"release", "Rel"}};
    for (const auto& [id, caption] : captions) {
        const auto* item = itemIn(separate, id);
        ASSERT_NE(item, nullptr) << id;
        EXPECT_EQ(item->when, std::optional(dimWhenTempo)) << id;
        EXPECT_EQ(item->label, std::optional<juce::String>(caption)) << id;
        EXPECT_EQ(item->widget, CardWidget::FaderV) << id;
        if (juce::String(id) == "sustain")
            continue;
        const auto* division = itemIn(separate, juce::String(id) + "Div");
        ASSERT_NE(division, nullptr) << id;
        EXPECT_EQ(division->when, std::optional(dimWhenTime)) << id;
        EXPECT_EQ(division->label, std::optional<juce::String>(caption)) << id;
    }
}

TEST(AdsrTimeTempo, SharedToSeparateToSharedIsTheDefaultStagesSectionExactly) {
    const auto shared = adsrDefault();
    EXPECT_EQ(withAdsrTimeTempo(withAdsrTimeTempo(shared, AdsrTimeTempo::Separate), AdsrTimeTempo::Shared), shared);
}

TEST(AdsrTimeTempo, AConversionToTheModeTheLayoutIsInChangesNothing) {
    const auto shared = adsrDefault();
    EXPECT_EQ(withAdsrTimeTempo(shared, AdsrTimeTempo::Shared), shared);
    const auto separate = withAdsrTimeTempo(shared, AdsrTimeTempo::Separate);
    EXPECT_EQ(withAdsrTimeTempo(separate, AdsrTimeTempo::Separate), separate);
}

TEST(AdsrTimeTempo, LabelsWidgetsRangesAndSpansCarryOverAndPositionsAreDropped) {
    auto shared = adsrDefault();
    auto& stages = shared.sections[2];
    ASSERT_EQ(stages.id, "stages");
    for (auto& item : stages.items) {
        auto& param = std::get<CardParamItem>(item);
        param.at = juce::Point<int>(4, 9);
        if (param.paramId == "attack") {
            param.widget = CardWidget::Knob;
            param.label = "Attack time";
            param.range = juce::Range<double>(0.1, 2.0);
        }
        if (param.paramId == "attackDiv")
            param.span = 2;
    }
    const auto separate = withAdsrTimeTempo(shared, AdsrTimeTempo::Separate);
    const auto* attack = itemIn(separate, "attack");
    ASSERT_NE(attack, nullptr);
    EXPECT_EQ(attack->widget, CardWidget::Knob);
    EXPECT_EQ(attack->label, std::optional<juce::String>("Attack time"));
    EXPECT_EQ(attack->range, std::optional(juce::Range<double>(0.1, 2.0)));
    EXPECT_EQ(itemIn(separate, "attackDiv")->span, 2);
    for (const auto& section : {*sectionNamed(separate, "stages-time"), *sectionNamed(separate, "stages-tempo")})
        for (const auto& item : paramsOf(section))
            EXPECT_FALSE(item.at.has_value()) << item.paramId;

    const auto back = withAdsrTimeTempo(separate, AdsrTimeTempo::Shared);
    EXPECT_EQ(itemIn(back, "attack")->widget, CardWidget::Knob);
    EXPECT_EQ(itemIn(back, "attack")->range, std::optional(juce::Range<double>(0.1, 2.0)));
    EXPECT_EQ(itemIn(back, "attackDiv")->span, 2);
}

TEST(AdsrTimeTempo, AHiddenControlStaysHiddenInBothModes) {
    auto shared = adsrDefault();
    auto& items = shared.sections[2].items;
    std::erase_if(items, [](const CardItem& item) {
        const auto* param = std::get_if<CardParamItem>(&item);
        return param != nullptr && (param->paramId == "hold" || param->paramId == "holdDiv");
    });
    shared.hidden.add("hold");
    shared.hidden.add("holdDiv");

    const auto separate = withAdsrTimeTempo(shared, AdsrTimeTempo::Separate);
    EXPECT_EQ(itemIn(separate, "hold"), nullptr);
    EXPECT_EQ(itemIn(separate, "holdDiv"), nullptr);
    EXPECT_EQ(separate.hidden, shared.hidden);
    EXPECT_EQ(withAdsrTimeTempo(separate, AdsrTimeTempo::Shared), shared);
}

TEST(AdsrTimeTempo, EverySectionButTheStagesIsUntouched) {
    const auto shared = adsrDefault();
    const auto separate = withAdsrTimeTempo(shared, AdsrTimeTempo::Separate);
    for (const char* id : {"envelope", "time", "trigger", "footer"})
        EXPECT_EQ(*sectionNamed(separate, id), *sectionNamed(shared, id)) << id;
}

TEST(AdsrTimeTempo, DetectionTellsTheModesAndAnUnrelatedLayoutApart) {
    EXPECT_EQ(adsrTimeTempoOf(withAdsrTimeTempo(adsrDefault(), AdsrTimeTempo::Separate)), AdsrTimeTempo::Separate);
    const auto filter = DefaultCardLayouts::builtIn().find("VCA")->layout;
    EXPECT_FALSE(adsrTimeTempoOf(filter).has_value());
    EXPECT_FALSE(adsrTimeTempoOf(CardLayout{}).has_value());
    EXPECT_EQ(withAdsrTimeTempo(filter, AdsrTimeTempo::Separate), filter);
}
