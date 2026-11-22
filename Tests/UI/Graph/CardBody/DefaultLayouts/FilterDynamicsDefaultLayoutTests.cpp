// FilterDynamicsDefaultLayoutTests.cpp
//
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFilterDynamics.cpp: the designed default cards
// of Filter, Compressor, Limiter and Gate, built from the code defaults the way the app builds them. Each
// card has its designed sections in order and its named widgets; the gain-reduction view is placed on the
// Compressor and Limiter; Key Track never dims; every control is a Tab stop with a title and tooltip; the
// size estimate equals the real card. The Filter has its response picture on top with two icon toggles on its
// top-right corner, exactly 8 px above the mode row (Type, Poly, Show Scope), then the lanes.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardBody/CardBodyLayoutWalk.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/CardBodyQueries.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardIconToggle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/GainReductionMeterComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;

namespace {

struct Built {
    CardCanvas canvas;
    NodeID id;
    ModuleComponent* card = nullptr;
    synth::CardBody* body = nullptr;

    explicit Built(const juce::String& type) {
        id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        card = canvas.card(id);
        body = card != nullptr ? card->getCardBody() : nullptr;
    }
};

std::vector<juce::String> sectionOrder(const synth::DefaultCardLayouts::Entry& entry) {
    std::vector<juce::String> ids;
    for (const auto& section : entry.layout.sections)
        ids.push_back(section.id);
    return ids;
}

template <typename T>
T* childOfType(juce::Component& parent) {
    for (auto* child : parent.getChildren())
        if (auto* typed = dynamic_cast<T*>(child))
            return typed;
    return nullptr;
}

juce::Component* viewWidget(const synth::CardBody& body, synth::CardView view) {
    for (const auto& item : body.getPlan().items)
        if (item.kind == synth::CardBodyItem::Kind::View && item.view == view)
            return item.widget;
    return nullptr;
}

void expectReachable(const Built& built, const juce::String& paramId) {
    SCOPED_TRACE(paramId.toStdString());
    auto* widget = built.body->findWidget(paramId);
    ASSERT_NE(widget, nullptr);
    EXPECT_TRUE(widget->isVisible());
    EXPECT_TRUE(widget->getWantsKeyboardFocus());
    EXPECT_TRUE(widget->getTitle().isNotEmpty());
    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(widget);
    ASSERT_NE(tip, nullptr);
    EXPECT_TRUE(tip->getTooltip().isNotEmpty());
}

} // namespace

TEST(FilterDynamicsDefaultLayout, EveryTypeHasADesignedDefaultWithNoDimRules) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const char* type : {"Filter", "Compressor", "Limiter", "Gate"}) {
        const auto* entry = defaults.find(type);
        ASSERT_NE(entry, nullptr) << type;
        // The Filter is at revision 6: the lane grid with the picture on top replaced its sections; the others are
        // at 2.
        EXPECT_EQ(entry->defaultRevision, juce::String(type) == "Filter" ? 6 : 2) << type;
        EXPECT_TRUE(entry->dimRules.empty()) << type << ": nothing in this family dims";
        for (const auto& section : entry->layout.sections)
            if (section.title.has_value())
                EXPECT_FALSE(section.title->containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZ ")) << "sentence case";
    }
    using Ids = std::vector<juce::String>;
    EXPECT_EQ(sectionOrder(*defaults.find("Filter")), (Ids{synth::CardSection::kModeRowId, "hero", "shape", "motion",
                                                           "custom", synth::CardSection::kOutLaneId}));
    EXPECT_EQ(sectionOrder(*defaults.find("Compressor")),
              (Ids{"meter", "hero", "shape", "time", synth::CardSection::kOutLaneId}));
    EXPECT_EQ(sectionOrder(*defaults.find("Limiter")),
              (Ids{"meter", "hero", "shape", "time", synth::CardSection::kOutLaneId}));
    EXPECT_EQ(sectionOrder(*defaults.find("Gate")), (Ids{"hero", "shape", "time", synth::CardSection::kOutLaneId}));
}

juce::ToggleButton* iconToggle(ModuleComponent& card, const juce::String& title) {
    for (auto* child : card.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
            toggle != nullptr && (toggle->getTitle() == title || toggle->getButtonText() == title))
            return toggle;
    return nullptr;
}

TEST(FilterDynamicsDefaultLayout, TheFilterHasThePictureOnTopThenTheModeRowThenTheLanesWithOutLast) {
    Built built("Filter");
    ASSERT_NE(built.body, nullptr);
    EXPECT_FALSE(built.body->hasFooter());
    EXPECT_FALSE(built.body->hasMoreRow());
    EXPECT_TRUE(built.body->hasModeRow());
    EXPECT_NE(dynamic_cast<juce::ComboBox*>(built.body->findWidget("filterType")), nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::CardKnobSlider*>(built.body->findWidget("cutoff")), nullptr);
    const auto& plan = built.body->getPlan();
    EXPECT_EQ(plan.items[(size_t)plan.findParam("cutoff")].kind, synth::CardBodyItem::Kind::KnobLarge);

    // The picture is the top of the content, the mode row is exactly one block gap under it, the lanes follow.
    auto* picture = childOfType<FrequencyResponseComponent>(*built.card);
    ASSERT_NE(picture, nullptr);
    EXPECT_TRUE(picture->isVisible()) << "shown by default on a new card";
    EXPECT_EQ(picture->getHeight(), synth::cardbody::kFilterPictureHeight);
    EXPECT_EQ(picture->getWidth(), built.card->getWidth() - 2 * synth::cardbody::kContentMargin);
    const auto band = synth::cardBodyModeRowBand(*built.body);
    ASSERT_TRUE(band.has_value());
    EXPECT_EQ(band->getStart() - picture->getBottom(), 8) << "exactly 8 px from the picture to the mode row";
    const auto y = [&](const char* id) { return built.body->findWidget(id)->getY(); };
    const auto x = [&](const char* id) { return built.body->findWidget(id)->getX(); };
    EXPECT_GE(y("cutoff"), band->getEnd());

    // Type on the left of the mode row, Poly and Show Scope at its right end.
    auto* type = built.body->findWidget("filterType");
    auto* poly = built.body->findWidget("poly");
    auto* scope = iconToggle(*built.card, "Show Scope");
    ASSERT_NE(scope, nullptr) << "Show Scope is a pill of the mode row";
    EXPECT_LT(type->getRight(), poly->getX());
    EXPECT_LT(poly->getRight(), scope->getX());
    EXPECT_EQ(scope->getRight(), built.card->getWidth() - synth::cardbody::kContentMargin) << "the right end";
    EXPECT_TRUE((bool)scope->getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty]);

    // The lanes, left to right: Hero (Cutoff), Shape (Resonance over Drive), Motion (Key Track), Out (Mix, Pan,
    // Output).
    EXPECT_LT(x("cutoff"), x("resonance"));
    EXPECT_EQ(x("resonance"), x("drive"));
    EXPECT_LT(y("resonance"), y("drive"));
    EXPECT_LT(x("drive"), x("keyTrack"));
    EXPECT_LT(x("keyTrack"), x("mix"));
    EXPECT_EQ(x("mix"), x("pan"));
    EXPECT_EQ(x("pan"), x("outputLevel"));
    EXPECT_LT(y("mix"), y("pan"));
    EXPECT_LT(y("pan"), y("outputLevel"));
    EXPECT_GE(built.body->findWidget("outputLevel")->getBottom(), plan.sections.back().cellBottom - 14)
        << "the Out lane sits on the bottom of the band, at the right";
    EXPECT_EQ(plan.sections.back().id, synth::CardSection::kOutLaneId);

    // Shift, Depth, Morph X and Morph Y only exist in the Custom type; Key Track never dims.
    for (const char* id : {"shift", "depth"})
        EXPECT_FALSE(built.body->findWidget(id)->isVisible()) << id;
    EXPECT_FALSE(plan.items[(size_t)plan.findParam("keyTrack")].dimmed);
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*built.body->findWidget("keyTrack")));

    // The output control is captioned Output and says what the four names mean.
    auto* output = dynamic_cast<juce::SettableTooltipClient*>(built.body->findWidget("outputLevel"));
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->getTooltip(), "Output. " + juce::String(synth::cardbody::kOutputControlTip));
    EXPECT_EQ(dynamic_cast<juce::Label*>(plan.items[(size_t)plan.findParam("outputLevel")].label)->getText(), "Output");

    for (const char* id : {"filterType", "cutoff", "resonance", "drive", "keyTrack", "mix", "pan", "outputLevel"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, ThePictureHoldsItsTwoIconTogglesOnTheTopRightCornerAndFoldsToAStripThatKeepsThem) {
    Built built("Filter");
    ASSERT_NE(built.body, nullptr);
    auto* picture = childOfType<FrequencyResponseComponent>(*built.card);
    auto* response = iconToggle(*built.card, synth::cardbody::kShowResponseText);
    auto* spectrum = iconToggle(*built.card, synth::ui::actiontext::kShowSpectrum);
    ASSERT_NE(picture, nullptr);
    ASSERT_NE(response, nullptr);
    ASSERT_NE(spectrum, nullptr);
    EXPECT_TRUE(response->getButtonText().isEmpty()) << "an icon, not a pill";
    EXPECT_NE(dynamic_cast<synth::ui::CardIconToggle*>(response), nullptr);
    EXPECT_TRUE(response->getToggleState());
    EXPECT_FALSE(spectrum->getToggleState());
    EXPECT_TRUE(response->getWantsKeyboardFocus());
    EXPECT_TRUE(spectrum->getWantsKeyboardFocus());

    // Both sit inside the picture's top-right corner, the Response icon in the corner itself.
    EXPECT_TRUE(picture->getBounds().contains(response->getBounds()));
    EXPECT_TRUE(picture->getBounds().contains(spectrum->getBounds()));
    EXPECT_EQ(response->getY(), picture->getY() + 3);
    EXPECT_EQ(response->getRight(), picture->getRight() - 3);
    EXPECT_LT(spectrum->getRight(), response->getX());
    EXPECT_EQ(spectrum->getY(), response->getY());

    // The tooltips name the action first and never echo it twice; the title is what a screen reader hears.
    EXPECT_TRUE(response->getTooltip().startsWith("Show Response: "));
    EXPECT_TRUE(spectrum->getTooltip().startsWith("Show Spectrum: "));
    EXPECT_FALSE(response->getTooltip().containsIgnoreCase("Show Response: Show Response"));

    // Tab: the picture's icons come first, then the Type menu, then the lanes.
    const auto stops = built.card->getKeyboardControls();
    ASSERT_GE(stops.size(), 4u);
    EXPECT_EQ(stops[0], spectrum);
    EXPECT_EQ(stops[1], response);
    EXPECT_EQ(stops[2], built.body->findWidget("filterType"));

    // Hiding the picture folds it to a strip that keeps the Response icon; showing it again puts it back.
    const int shownHeight = built.card->getHeight();
    spectrum->setToggleState(true, juce::sendNotificationSync);
    response->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(picture->isVisible());
    EXPECT_FALSE(spectrum->isVisible());
    EXPECT_FALSE(spectrum->getToggleState()) << "hiding the picture clears the spectrum backdrop";
    EXPECT_TRUE(response->isVisible());
    EXPECT_EQ(built.card->getHeight(),
              shownHeight - (synth::cardbody::kFilterPictureHeight - synth::cardbody::kFilterPictureStripHeight));
    EXPECT_EQ(response->getY(), picture->getY() + 3) << "the icon stays where it was";
    EXPECT_EQ(response->getRight(), picture->getRight() - 3);
    EXPECT_EQ(synth::cardBodyModeRowBand(*built.body)->getStart() - picture->getBottom(), 8) << "the gap stays 8 px";
    response->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(picture->isVisible());
    EXPECT_EQ(built.card->getHeight(), shownHeight);
}

TEST(FilterDynamicsDefaultLayout, ACardThatHidTheResponseKeepsItHiddenAndANewOneStartsWithItShown) {
    CardCanvas canvas;
    auto hiddenModule = std::make_unique<FilterModule>();
    synth::CardViewState state;
    state.showResponse = false;
    hiddenModule->setCardViewState(state);
    const auto hiddenId = canvas.add(std::move(hiddenModule), 0, 0);
    const auto shownId = canvas.add(synth::AIStateMapper::createModule("Filter"), 400, 0);
    canvas.editor.updateComponents();
    auto* hidden = canvas.card(hiddenId);
    auto* shown = canvas.card(shownId);
    ASSERT_NE(hidden, nullptr);
    ASSERT_NE(shown, nullptr);
    EXPECT_FALSE(childOfType<FrequencyResponseComponent>(*hidden)->isVisible());
    EXPECT_FALSE(iconToggle(*hidden, synth::cardbody::kShowResponseText)->getToggleState());
    EXPECT_TRUE(childOfType<FrequencyResponseComponent>(*shown)->isVisible());
    EXPECT_EQ(shown->getHeight() - hidden->getHeight(),
              synth::cardbody::kFilterPictureHeight - synth::cardbody::kFilterPictureStripHeight);
}

TEST(FilterDynamicsDefaultLayout, TheCustomTypeAddsShiftAndDepthToTheShapeLaneAndKeepsTheCardReachable) {
    Built built("Filter");
    ASSERT_NE(built.body, nullptr);
    auto* type =
        dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(built.canvas.processor(built.id), "filterType"));
    ASSERT_NE(type, nullptr);
    const int plainWidth = built.card->getWidth();
    type->setValueNotifyingHost(type->convertTo0to1((float)FilterModule::kCustomTypeIndex));
    built.body->flushPendingConditionUpdate();
    for (const char* id : {"shift", "depth", "resonance", "drive", "cutoff", "keyTrack", "mix", "pan", "outputLevel"})
        expectReachable(built, id);
    EXPECT_EQ(built.body->findWidget("shift")->getY(), built.body->findWidget("resonance")->getY());
    EXPECT_GT(built.body->findWidget("shift")->getX(), built.body->findWidget("resonance")->getX())
        << "a second column of the Shape lane";
    EXPECT_GE(built.card->getWidth(), plainWidth);
    const auto inside = built.card->getLocalBounds();
    for (const char* id : {"shift", "depth", "outputLevel", "poly"})
        EXPECT_TRUE(inside.contains(built.body->findWidget(id)->getBounds())) << id;
}

TEST(FilterDynamicsDefaultLayout, TheCompressorShowsTheGainReductionViewThenAThresholdFaderAndTheLanesWithMakeupInOut) {
    Built built("Compressor");
    ASSERT_NE(built.body, nullptr);
    EXPECT_FALSE(built.body->hasModeRow()) << "no mode switch and no Show Scope: nothing would stand in a mode row";
    EXPECT_FALSE(built.body->hasFooter());
    auto* meter = dynamic_cast<GainReductionMeterComponent*>(viewWidget(*built.body, synth::CardView::GainReduction));
    ASSERT_NE(meter, nullptr) << "the gain-reduction view is registered and placed";
    EXPECT_TRUE(meter->isVisible());
    EXPECT_FALSE(meter->getWantsKeyboardFocus()) << "read-only, not a Tab stop";
    EXPECT_TRUE(meter->getTitle().isNotEmpty());
    EXPECT_TRUE(meter->getTooltip().isNotEmpty());

    auto* threshold = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("threshold"));
    ASSERT_NE(threshold, nullptr);
    EXPECT_EQ(threshold->getSliderStyle(), juce::Slider::LinearVertical);
    EXPECT_GE(threshold->getY(), meter->getBottom());
    EXPECT_GE(threshold->getHeight(), synth::cardbody::kFaderVLongHeight - 24) << "it spans both rows of its lane";
    const auto x = [&](const char* id) { return built.body->findWidget(id)->getX(); };
    const auto y = [&](const char* id) { return built.body->findWidget(id)->getY(); };
    EXPECT_LT(threshold->getRight(), x("ratio"));
    EXPECT_EQ(x("ratio"), x("knee"));
    EXPECT_LT(y("ratio"), y("knee"));
    EXPECT_LT(x("ratio"), x("attack"));
    EXPECT_EQ(x("attack"), x("release"));
    EXPECT_LT(y("attack"), y("release"));
    EXPECT_LT(x("attack"), x("mix"));
    EXPECT_EQ(x("mix"), x("pan"));
    EXPECT_EQ(x("pan"), x("makeupGain"));
    EXPECT_LT(y("mix"), y("pan"));
    EXPECT_LT(y("pan"), y("makeupGain"));
    // The relabelled Makeup keeps its full name in the caption's tooltip and says what the four names mean.
    const auto& plan = built.body->getPlan();
    auto* makeupLabel = dynamic_cast<juce::Label*>(plan.items[(size_t)plan.findParam("makeupGain")].label);
    ASSERT_NE(makeupLabel, nullptr);
    EXPECT_EQ(makeupLabel->getText(), "Makeup");
    EXPECT_EQ(makeupLabel->getTooltip(), "Makeup Gain");
    auto* makeup = dynamic_cast<juce::SettableTooltipClient*>(built.body->findWidget("makeupGain"));
    ASSERT_NE(makeup, nullptr);
    EXPECT_EQ(makeup->getTooltip(), "Makeup. " + juce::String(synth::cardbody::kOutputControlTip));
    EXPECT_FALSE(built.body->hasMoreRow());
    for (const char* id : {"threshold", "ratio", "knee", "attack", "release", "mix", "pan", "makeupGain"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, TheLimiterReadsAsMeterThenThresholdInputGainReleaseAndPanWithCeilingInOut) {
    Built built("Limiter");
    ASSERT_NE(built.body, nullptr);
    auto* meter = viewWidget(*built.body, synth::CardView::GainReduction);
    ASSERT_NE(meter, nullptr);
    auto* threshold = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("threshold"));
    ASSERT_NE(threshold, nullptr);
    EXPECT_EQ(threshold->getSliderStyle(), juce::Slider::LinearVertical);
    EXPECT_LE(meter->getBottom(), threshold->getY());
    const auto x = [&](const char* id) { return built.body->findWidget(id)->getX(); };
    const auto y = [&](const char* id) { return built.body->findWidget(id)->getY(); };
    EXPECT_LT(threshold->getRight(), x("inputGain"));
    EXPECT_LT(x("inputGain"), x("release"));
    EXPECT_LT(x("release"), x("pan"));
    EXPECT_EQ(x("pan"), x("ceiling"));
    EXPECT_LT(y("pan"), y("ceiling"));
    EXPECT_EQ(built.body->findWidget("mix"), nullptr) << "no Mix: the lookahead latency would misalign the dry signal";
    EXPECT_FALSE(built.body->hasMoreRow());
    auto* ceiling = dynamic_cast<juce::SettableTooltipClient*>(built.body->findWidget("ceiling"));
    ASSERT_NE(ceiling, nullptr);
    EXPECT_TRUE(ceiling->getTooltip().endsWith(synth::cardbody::kOutputControlTip));
    for (const char* id : {"threshold", "inputGain", "release", "pan", "ceiling"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, TheGateHasAThresholdKnobAsHeroThenRangeThenTimesAndTheOutLane) {
    Built built("Gate");
    ASSERT_NE(built.body, nullptr);
    EXPECT_FALSE(built.body->hasFooter());
    EXPECT_FALSE(built.body->hasMoreRow());
    const auto& plan = built.body->getPlan();
    EXPECT_EQ(plan.items[(size_t)plan.findParam("threshold")].kind, synth::CardBodyItem::Kind::KnobLarge);
    const auto x = [&](const char* id) { return built.body->findWidget(id)->getX(); };
    const auto y = [&](const char* id) { return built.body->findWidget(id)->getY(); };
    EXPECT_LT(x("threshold"), x("range"));
    EXPECT_LT(x("range"), x("attack"));
    EXPECT_EQ(x("attack"), x("hold")) << "two time knobs fill a column";
    EXPECT_LT(y("attack"), y("hold"));
    EXPECT_LT(x("attack"), x("release")) << "the third time knob starts a second column";
    EXPECT_LT(x("release"), x("mix"));
    EXPECT_EQ(x("mix"), x("outputLevel"));
    EXPECT_LT(y("mix"), y("pan"));
    EXPECT_LT(y("pan"), y("outputLevel"));
    for (const char* id : {"threshold", "range", "attack", "hold", "release", "mix", "pan", "outputLevel"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, TheGainReductionMeterOnlyRepaintsWhenTheShownReadingMoves) {
    using M = GainReductionMeterComponent;
    EXPECT_FALSE(M::needsRepaint(0.0f, 0.04f)) << "less than a tenth of a dB is the same reading";
    EXPECT_FALSE(M::needsRepaint(3.0f, 3.0f));
    EXPECT_TRUE(M::needsRepaint(3.0f, 3.2f));
    EXPECT_FALSE(M::needsRepaint(0.0f, -2.0f)) << "negative counts as 0";
    EXPECT_EQ(M::reductionToNormalized(0.0f), 0.0f);
    EXPECT_EQ(M::reductionToNormalized(M::getMaxDecibels()), 1.0f);
    EXPECT_EQ(M::reductionToNormalized(100.0f), 1.0f);
    EXPECT_EQ(M::readoutText(0.0f), "0.0 dB");
    EXPECT_EQ(M::readoutText(6.24f), "-6.2 dB");

    struct Fixed : GainReductionMeterSource {
        float db = 0.0f;
        float getGainReductionDb() const override { return db; }
    } source;
    M meter(source);
    meter.setBounds(0, 0, 200, M::getHeight());
    source.db = 6.0f;
    meter.timerCallback();
    EXPECT_EQ(meter.getShownReductionDb(), 6.0f);
    source.db = 6.02f;
    meter.timerCallback();
    EXPECT_EQ(meter.getShownReductionDb(), 6.0f) << "a change below a visible amount leaves the shown value alone";
}

TEST(FilterDynamicsDefaultLayout, TheEstimateEqualsTheRealCardForEveryTypeInTheFamily) {
    for (const char* type : {"Filter", "Compressor", "Limiter", "Gate"}) {
        SCOPED_TRACE(type);
        Built built(type);
        ASSERT_NE(built.card, nullptr);
        const auto estimate = synth::measureDataDrivenCardSize(type);
        ASSERT_TRUE(estimate.has_value());
        const juce::Point<int> real(built.card->getWidth(), built.card->getHeight());
        EXPECT_EQ(*estimate, real);
        EXPECT_EQ(GraphEditor::estimateModuleSize(type), real) << "the drag ghost and the drop placement";
    }
}

TEST(FilterDynamicsDefaultLayout, TheFilterMorphLaneShowsOnlyWhileCustomIsTheType) {
    const auto* entry = synth::DefaultCardLayouts::builtIn().find("Filter");
    ASSERT_NE(entry, nullptr);
    const synth::CardSection* custom = nullptr;
    for (const auto& section : entry->layout.sections)
        if (section.id == "custom")
            custom = &section;
    ASSERT_NE(custom, nullptr);
    ASSERT_TRUE(custom->visibleWhen.has_value());
    EXPECT_EQ(custom->visibleWhen->param, "filterType");
    EXPECT_EQ(custom->visibleWhen->is, juce::StringArray({"Custom"}));
    EXPECT_EQ(custom->visibleWhen->effect, synth::CardConditionEffect::Show);
    EXPECT_EQ(custom->items.size(), 2u) << "Morph X and Morph Y; the card hides them while the morph map shows";
}

// Regression test for FRO964: Custom uses Cutoff and Resonance, so they no longer swap out for Shift and Depth.
TEST(FilterDynamicsDefaultLayout, CustomKeepsCutoffResonanceAndDriveAndAddsShiftAndDepthToTheShapeLane) {
    const auto* entry = synth::DefaultCardLayouts::builtIn().find("Filter");
    ASSERT_NE(entry, nullptr);
    const auto sectionNamed = [&](const char* id) -> const synth::CardSection& {
        for (const auto& section : entry->layout.sections)
            if (section.id == id)
                return section;
        ADD_FAILURE() << id;
        return entry->layout.sections.front();
    };
    const auto param = [&](const synth::CardSection& sec, size_t i) -> const synth::CardParamItem& {
        return std::get<synth::CardParamItem>(sec.items[i]);
    };
    const auto& hero = sectionNamed("hero");
    ASSERT_EQ(hero.items.size(), 1u);
    EXPECT_EQ(param(hero, 0).paramId, "cutoff");
    EXPECT_FALSE(param(hero, 0).when.has_value()) << "shown in every type, Custom included";
    const auto& shape = sectionNamed("shape");
    ASSERT_EQ(shape.items.size(), 4u);
    EXPECT_EQ(param(shape, 0).paramId, "resonance");
    EXPECT_EQ(param(shape, 1).paramId, "drive");
    EXPECT_FALSE(param(shape, 0).when.has_value());
    EXPECT_FALSE(param(shape, 1).when.has_value());
    EXPECT_EQ(param(shape, 2).paramId, "shift");
    EXPECT_EQ(param(shape, 3).paramId, "depth");
    for (size_t i = 2; i < 4; ++i) {
        ASSERT_TRUE(param(shape, i).when.has_value());
        EXPECT_EQ(param(shape, i).when->is, juce::StringArray({"Custom"}));
    }
}
