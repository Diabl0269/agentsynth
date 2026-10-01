// FilterDynamicsDefaultLayoutTests.cpp
//
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFilterDynamics.cpp: the designed default cards
// of Filter, Compressor, Limiter and Gate, built from the code defaults the way the app builds them. Each
// card has its designed sections in order and its named widgets; the gain-reduction view is placed on the
// Compressor and Limiter; Key Track never dims; every control is a Tab stop with a title and tooltip; the
// size estimate equals the real card.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
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

TEST(FilterDynamicsDefaultLayout, EveryTypeHasADesignedDefaultAtRevisionOneWithNoDimRules) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const char* type : {"Filter", "Compressor", "Limiter", "Gate"}) {
        const auto* entry = defaults.find(type);
        ASSERT_NE(entry, nullptr) << type;
        EXPECT_EQ(entry->defaultRevision, 1) << type;
        EXPECT_TRUE(entry->dimRules.empty()) << type << ": nothing in this family dims";
        for (const auto& section : entry->layout.sections)
            if (section.title.has_value())
                EXPECT_FALSE(section.title->containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZ ")) << "sentence case";
    }
    EXPECT_EQ(sectionOrder(*defaults.find("Filter")),
              (std::vector<juce::String>{"type", "tone", "modulation", "footer"}));
    EXPECT_EQ(sectionOrder(*defaults.find("Compressor")), (std::vector<juce::String>{"main", "footer"}));
    EXPECT_EQ(sectionOrder(*defaults.find("Limiter")), (std::vector<juce::String>{"main"}));
    EXPECT_EQ(sectionOrder(*defaults.find("Gate")), (std::vector<juce::String>{"main", "footer"}));
}

TEST(FilterDynamicsDefaultLayout, TheFilterHasTypeLargeCutoffModulationAndAFooterWithItsChromePills) {
    Built built("Filter");
    ASSERT_NE(built.body, nullptr);
    ASSERT_TRUE(built.body->hasFooter());
    EXPECT_FALSE(built.body->hasMoreRow());
    EXPECT_NE(dynamic_cast<juce::ComboBox*>(built.body->findWidget("filterType")), nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::CardKnobSlider*>(built.body->findWidget("cutoff")), nullptr);

    const auto& plan = built.body->getPlan();
    EXPECT_EQ(plan.items[(size_t)plan.findParam("cutoff")].kind, synth::CardBodyItem::Kind::KnobLarge);
    const auto& modulation = plan.sections[2];
    ASSERT_TRUE(modulation.hasHeader());
    EXPECT_EQ(*modulation.title, "Modulation");
    EXPECT_EQ(modulation.items.size(), 2u);

    // Top to bottom: type, cutoff, resonance and drive, modulation, then the footer.
    const auto y = [&](const char* id) { return built.body->findWidget(id)->getY(); };
    EXPECT_LT(y("filterType"), y("cutoff"));
    EXPECT_LT(y("cutoff"), y("resonance"));
    EXPECT_LE(y("resonance"), y("drive"));
    EXPECT_LT(y("drive"), y("keyTrack"));
    EXPECT_LE(y("keyTrack"), y("outputLevel"));
    EXPECT_LT(y("outputLevel"), y("poly"));

    // Key Track never dims: the card knows no cable.
    EXPECT_FALSE(plan.items[(size_t)plan.findParam("keyTrack")].dimmed);
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*built.body->findWidget("keyTrack")));

    // Show Response is a footer pill; opening it adds the Show Spectrum pill and grows the card.
    juce::ToggleButton* response = nullptr;
    for (auto* child : built.card->getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
            toggle != nullptr && toggle->getButtonText() == synth::cardbody::kShowResponseText)
            response = toggle;
    ASSERT_NE(response, nullptr);
    EXPECT_TRUE((bool)response->getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty]);
    EXPECT_GE(response->getY(), built.body->findWidget("outputLevel")->getBottom());
    const int height = built.card->getHeight();
    response->setToggleState(true, juce::sendNotificationSync);
    response->onClick();
    EXPECT_GT(built.card->getHeight(), height);

    for (const char* id : {"filterType", "cutoff", "resonance", "drive", "keyTrack", "outputLevel"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, TheCompressorShowsTheGainReductionViewThenAThresholdFaderAndAKneeFooter) {
    Built built("Compressor");
    ASSERT_NE(built.body, nullptr);
    auto* meter = dynamic_cast<GainReductionMeterComponent*>(viewWidget(*built.body, synth::CardView::GainReduction));
    ASSERT_NE(meter, nullptr) << "the gain-reduction view is registered and placed";
    EXPECT_TRUE(meter->isVisible());
    EXPECT_FALSE(meter->getWantsKeyboardFocus()) << "read-only, not a Tab stop";
    EXPECT_TRUE(meter->getTitle().isNotEmpty());
    EXPECT_TRUE(meter->getTooltip().isNotEmpty());

    auto* threshold = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("threshold"));
    ASSERT_NE(threshold, nullptr);
    EXPECT_EQ(threshold->getSliderStyle(), juce::Slider::LinearVertical);
    auto* knee = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("knee"));
    ASSERT_NE(knee, nullptr) << "Knee is in the footer, a horizontal fader";
    EXPECT_EQ(knee->getSliderStyle(), juce::Slider::LinearHorizontal);
    EXPECT_GE(meter->getBottom(), 0);
    EXPECT_LE(meter->getBottom(), threshold->getY());
    EXPECT_LT(built.body->findWidget("ratio")->getY(), built.body->findWidget("release")->getY());
    EXPECT_GT(knee->getY(), built.body->findWidget("release")->getBottom());
    EXPECT_FALSE(built.body->hasMoreRow());
    for (const char* id : {"threshold", "ratio", "makeupGain", "attack", "release", "knee"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, TheLimiterReadsAsASignalPathInputMeterCeilingRelease) {
    Built built("Limiter");
    ASSERT_NE(built.body, nullptr);
    auto* meter = viewWidget(*built.body, synth::CardView::GainReduction);
    ASSERT_NE(meter, nullptr);
    auto* input = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("inputGain"));
    auto* ceiling = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("ceiling"));
    ASSERT_NE(input, nullptr);
    ASSERT_NE(ceiling, nullptr);
    EXPECT_EQ(input->getSliderStyle(), juce::Slider::LinearVertical);
    EXPECT_EQ(ceiling->getSliderStyle(), juce::Slider::LinearVertical);
    EXPECT_LE(input->getBottom(), meter->getY());
    EXPECT_LE(meter->getBottom(), ceiling->getY());
    EXPECT_LT(ceiling->getY(), built.body->findWidget("release")->getY());
    EXPECT_FALSE(built.body->hasMoreRow());
    for (const char* id : {"inputGain", "ceiling", "threshold", "release"})
        expectReachable(built, id);
}

TEST(FilterDynamicsDefaultLayout, TheGateHasThresholdAttackHoldReleaseAndARangeAndLevelFooter) {
    Built built("Gate");
    ASSERT_NE(built.body, nullptr);
    ASSERT_TRUE(built.body->hasFooter());
    EXPECT_FALSE(built.body->hasMoreRow());
    auto* range = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("range"));
    auto* level = dynamic_cast<synth::ui::CardFader*>(built.body->findWidget("outputLevel"));
    ASSERT_NE(range, nullptr);
    ASSERT_NE(level, nullptr);
    EXPECT_GT(range->getY(), built.body->findWidget("release")->getBottom());
    for (const char* id : {"threshold", "attack", "hold", "release", "range", "outputLevel"})
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
        EXPECT_EQ(*estimate, juce::Point<int>(built.card->getWidth(), built.card->getHeight()));
    }
}
