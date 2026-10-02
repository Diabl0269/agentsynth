// CardBodyFooterTests.cpp
//
// Source/UI/Graph/CardBody/CardBodyFooter.cpp: a layout's footer section (CardSection::kFooterId) is
// one compact row at the bottom of the body, above the More row. A toggle is the small pill, a level a
// horizontal fader, and the card's chrome toggles (Show Scope, Show Response) join the row as pills; a
// card without a footer keeps its chrome rows. Also the size estimate against a real card for layouts
// with conditions, a footer and a More row.

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using namespace synth::cardlayout;

namespace {

synth::CardLayout layoutOf(std::vector<synth::CardSection> sections) {
    synth::CardLayout layout;
    layout.sections = std::move(sections);
    return layout;
}

bool isPill(const juce::Component& component) {
    return (bool)component.getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty];
}

juce::ToggleButton* chromeToggle(ModuleComponent& card, const juce::String& text) {
    for (auto* child : card.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
            toggle != nullptr && toggle->getButtonText() == text)
            return toggle;
    return nullptr;
}

// The Oscillator with its pitch knobs in the body and Level in the footer; Poly joins the footer on its
// own, the rest folds into More. The footer is listed first to show it is drawn last regardless.
synth::CardLayout oscillatorWithFooter() {
    return layoutOf(
        {footer({param("level")}),
         section("main", std::nullopt, {param("waveform"), param("octave"), param("coarse"), param("fine")})});
}

} // namespace

// The pill width is measured in the embedded Inter, not the system typeface, so it is the same fixed
// number of pixels on macOS, Windows and Linux (it was 79 on macOS and 81 on Windows when it used the
// system font). A different value here means the text measure left the embedded font.
TEST(CardBodyFooter, ThePillWidthComesFromTheEmbeddedFontSoItIsTheSameOnEveryPlatform) {
    EXPECT_EQ(synth::theme::AppLookAndFeel::togglePillWidth("Show Scope"), 79);
}

TEST(CardBodyFooter, TheFooterRowHoldsAPillAFaderAndTheChromeToggles) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<OscillatorModule>(), 0, 0, oscillatorWithFooter());
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    ASSERT_TRUE(body->hasFooter());

    auto* poly = dynamic_cast<juce::ToggleButton*>(body->findWidget("poly"));
    auto* level = dynamic_cast<synth::ui::CardFader*>(body->findWidget("level"));
    auto* scope = chromeToggle(*card, synth::cardbody::kShowScopeText);
    ASSERT_NE(poly, nullptr);
    ASSERT_NE(level, nullptr) << "a footer level is a horizontal fader";
    ASSERT_NE(scope, nullptr);
    EXPECT_TRUE(isPill(*poly)) << "Poly joins the footer as a pill";
    EXPECT_TRUE(isPill(*scope));
    EXPECT_EQ(level->getSliderStyle(), juce::Slider::LinearHorizontal);

    // One row: every footer item's vertical centre on one line, below the body, inside the card.
    const int rowCentre = level->getBounds().getCentreY();
    for (auto* item : std::initializer_list<juce::Component*>{poly, scope}) {
        EXPECT_TRUE(item->isVisible());
        EXPECT_EQ(item->getBounds().getCentreY(), rowCentre);
        EXPECT_TRUE(card->getLocalBounds().contains(item->getBounds()));
        EXPECT_FALSE(item->getBounds().intersects(level->getBounds()));
    }
    EXPECT_FALSE(poly->getBounds().intersects(scope->getBounds()));
    EXPECT_GT(level->getY(), body->findWidget("fine")->getBottom()) << "the footer sits under the body";
    ASSERT_TRUE(body->hasMoreRow()) << "unison, detune, pan, pulse width and glide fold into More";
    EXPECT_GE(body->getMoreButton()->getY(), level->getBottom()) << "the More row stays under the footer";

    // Keyboard, focus ring, name and tooltip are the toggle's own.
    for (auto* pill : {poly, scope}) {
        EXPECT_TRUE(pill->getWantsKeyboardFocus());
        EXPECT_TRUE(pill->getTitle().isNotEmpty());
        EXPECT_TRUE(pill->getTooltip().isNotEmpty());
    }

    // A footer toggle still drives its parameter.
    poly->setToggleState(true, juce::sendNotificationSync);
    EXPECT_TRUE(findParameterByID(canvas.processor(id), "poly")->getValue() > 0.5f);
}

TEST(CardBodyFooter, ACardWithoutAFooterKeepsItsChromeRows) {
    CardCanvas canvas;
    const auto plain = canvas.add(std::make_unique<OscillatorModule>(), 0, 0,
                                  layoutOf({section("main", std::nullopt, {param("waveform"), param("octave")})}));
    const auto withFooter = canvas.add(std::make_unique<OscillatorModule>(), 400, 0, oscillatorWithFooter());
    canvas.editor.updateComponents();
    auto* scope = chromeToggle(*canvas.card(plain), synth::cardbody::kShowScopeText);
    ASSERT_NE(scope, nullptr);
    EXPECT_FALSE(isPill(*scope));
    EXPECT_EQ(scope->getWidth(), canvas.card(plain)->getWidth() - 2 * synth::cardbody::kContentMargin)
        << "a full-width row, as before";
    EXPECT_FALSE(isPill(*canvas.card(plain)->getCardBody()->findWidget("poly")));
    EXPECT_TRUE(isPill(*chromeToggle(*canvas.card(withFooter), synth::cardbody::kShowScopeText)));
}

TEST(CardBodyFooter, TheFilterFooterHoldsShowResponseAndOpensItsViewAboveTheRows) {
    CardCanvas canvas;
    const auto layout =
        layoutOf({section("main", std::nullopt, {param("filterType"), param("cutoff"), param("resonance")}),
                  footer({param("outputLevel")})});
    const auto id = canvas.add(std::make_unique<FilterModule>(), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* response = chromeToggle(*card, synth::cardbody::kShowResponseText);
    auto* spectrum = chromeToggle(*card, synth::cardbody::kShowSpectrumText);
    ASSERT_NE(response, nullptr);
    ASSERT_NE(spectrum, nullptr);
    EXPECT_TRUE(isPill(*response));
    const int height = card->getHeight();

    response->setToggleState(true, juce::sendNotificationSync);
    response->onClick();
    EXPECT_TRUE(spectrum->isVisible()) << "Show Spectrum joins the row once the response is open";
    EXPECT_GT(card->getHeight(), height);
    auto* level = card->getCardBody()->findWidget("outputLevel");
    EXPECT_GE(spectrum->getY(), level->getY()) << "in the footer (wrapped onto a second row when it is full)";
    EXPECT_TRUE(card->getLocalBounds().contains(spectrum->getBounds()));
    EXPECT_GE(level->getY(), card->getCardBody()->findWidget("resonance")->getBottom());
}

// The size estimate measures the code default exactly as the card draws it: a swap group, a section
// shown and one hidden by their conditions, a More row and the footer with its chrome pills.
TEST(CardBodyFooter, TheEstimateMatchesTheRealCardWithConditionsAFooterAndMore) {
    synth::CardSection freeRunning = section("free", juce::String("Free running"), {param("glide"), param("phase")});
    freeRunning.visibleWhen = synth::CardCondition{"mode", {"false"}, synth::CardConditionEffect::Show};
    synth::CardSection synced = section("synced", std::nullopt, {param("fadeIn")});
    synced.visibleWhen = synth::CardCondition{"mode", {"true"}, synth::CardConditionEffect::Show};
    const std::map<juce::String, synth::CardLayout> layouts{
        {"LFO",
         layoutOf({section("main", std::nullopt,
                           {param("shape", synth::CardWidget::Segmented), showWhen(param("rateHz"), "mode", {"false"}),
                            showWhen(param("rateSync"), "mode", {"true"}), param("level")}),
                   freeRunning, synced, footer({param("bipolar"), param("retrig")})})},
        {"Filter", layoutOf({section("main", std::nullopt, {param("cutoff"), param("resonance")}),
                             footer({param("outputLevel")})})},
        {"Oscillator", oscillatorWithFooter()}};

    synth::DefaultCardLayouts defaults;
    CardCanvas canvas;
    std::map<juce::String, NodeID> ids;
    int x = 0;
    for (const auto& [type, layout] : layouts) {
        defaults.add(type, layout, 1);
        ids[type] = canvas.add(synth::AIStateMapper::createModule(type), x, 0, layout);
        x += 400;
    }
    canvas.editor.updateComponents();
    for (const auto& [type, id] : ids) {
        SCOPED_TRACE(type.toStdString());
        const auto estimate = synth::measureDataDrivenCardSizeWith(type, defaults);
        ASSERT_TRUE(estimate.has_value());
        auto* card = canvas.card(id);
        EXPECT_EQ(*estimate, juce::Point<int>(card->getWidth(), card->getHeight()));
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(card->getWidth());
        EXPECT_EQ(card->getCardBody()->layout(100, g, false, false), card->getCardBody()->layout(100, g, true, false));
    }
}
